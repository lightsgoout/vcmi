#define VCMI_DLL
#include <algorithm>
#include <queue>
#include <fstream>
#include "CGameState.h"
#include <boost/random/linear_congruential.hpp>
#include "../hch/CDefObjInfoHandler.h"
#include "../hch/CArtHandler.h"
#include "../hch/CBuildingHandler.h"
#include "../hch/CGeneralTextHandler.h"
#include "../hch/CTownHandler.h"
#include "../hch/CSpellHandler.h"
#include "../hch/CHeroHandler.h"
#include "../hch/CObjectHandler.h"
#include "../hch/CCreatureHandler.h"
#include "VCMI_Lib.h"
#include "Connection.h"
#include "map.h"
#include "../StartInfo.h"
#include "NetPacks.h"
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <boost/thread/shared_mutex.hpp>
#include "RegisterTypes.cpp"

boost::rand48 ran;

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

/*
 * CGameState.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

void foofoofoo()
{
	//never called function to force instantation of templates
	int *ccc = NULL;
	registerTypes((CISer<CConnection>&)*ccc);
	registerTypes((COSer<CConnection>&)*ccc);
	registerTypes((CSaveFile&)*ccc);
	registerTypes((CLoadFile&)*ccc);
	registerTypes((CTypeList&)*ccc);
}


class CBaseForGSApply
{
public:
	virtual void applyOnGS(CGameState *gs, void *pack) const =0; 
};
template <typename T> class CApplyOnGS : public CBaseForGSApply
{
public:
	void applyOnGS(CGameState *gs, void *pack) const
	{
		T *ptr = static_cast<T*>(pack);
		while(!gs->mx->try_lock())
			boost::this_thread::sleep(boost::posix_time::milliseconds(50)); //give other threads time to finish
		ptr->applyGs(gs);
		gs->mx->unlock();
	}
};

class CGSApplier
{
public:
	std::map<ui16,CBaseForGSApply*> apps; 

	CGSApplier()
	{
		registerTypes2(*this);
	}
	template<typename T> void registerType(const T * t=NULL)
	{
		ui16 ID = typeList.registerType(t);
		apps[ID] = new CApplyOnGS<T>;
	}

} *applierGs = NULL;

class IObjectCaller
{
public:
	virtual void preInit()=0;
	virtual void postInit()=0;
};

template <typename T>
class CObjectCaller : public IObjectCaller
{
public:
	void preInit()
	{
		T::preInit();
	}
	void postInit()
	{
		T::postInit();
	}
};

class CObjectCallersHandler
{
public:
	std::vector<IObjectCaller*> apps; 

	template<typename T> void registerType(const T * t=NULL)
	{
		apps.push_back(new CObjectCaller<T>);
	}

	CObjectCallersHandler()
	{
		registerTypes1(*this);
	}

	~CObjectCallersHandler()
	{
		for (size_t i = 0; i < apps.size(); i++)
			delete apps[i];
	}

	void preInit()
	{
		for (size_t i = 0; i < apps.size(); i++)
			apps[i]->preInit();
	}

	void postInit()
	{
		for (size_t i = 0; i < apps.size(); i++)
			apps[i]->postInit();
	}
} *objCaller = NULL;

void MetaString::getLocalString(const std::pair<ui8,ui32> &txt, std::string &dst) const
{
	int type = txt.first, ser = txt.second;

	if(type == ART_NAMES)
	{
		dst = VLC->arth->artifacts[ser].Name();
	}
	else if(type == CRE_PL_NAMES)
	{
		dst = VLC->creh->creatures[ser].namePl;
	}
	else if(type == MINE_NAMES)
	{
		dst = VLC->generaltexth->mines[ser].first;
	}
	else if(type == MINE_EVNTS)
	{
		dst = VLC->generaltexth->mines[ser].second;
	}
	else if(type == SPELL_NAME)
	{
		dst = VLC->spellh->spells[ser].name;
	}
	else if(type == CRE_SING_NAMES)
	{
		dst = VLC->creh->creatures[ser].nameSing;
	}
	else
	{
		std::vector<std::string> *vec;
		switch(type)
		{
		case GENERAL_TXT:
			vec = &VLC->generaltexth->allTexts;
			break;
		case XTRAINFO_TXT:
			vec = &VLC->generaltexth->xtrainfo;
			break;
		case OBJ_NAMES:
			vec = &VLC->generaltexth->names;
			break;
		case RES_NAMES:
			vec = &VLC->generaltexth->restypes;
			break;
		case ARRAY_TXT:
			vec = &VLC->generaltexth->arraytxt;
			break;
		case CREGENS:
			vec = &VLC->generaltexth->creGens;
			break;
		case CREGENS4:
			vec = &VLC->generaltexth->creGens4;
			break;
		case ADVOB_TXT:
			vec = &VLC->generaltexth->advobtxt;
			break;
		case ART_EVNTS:
			vec = &VLC->generaltexth->artifEvents;
			break;
		case SEC_SKILL_NAME:
			vec = &VLC->generaltexth->skillName;
			break;
		}
		dst = (*vec)[ser];
	}
}

DLL_EXPORT void MetaString::toString(std::string &dst) const
{
	size_t exSt = 0, loSt = 0, nums = 0;
	dst.clear();

	for(size_t i=0;i<message.size();++i)
	{//TEXACT_STRING, TLOCAL_STRING, TNUMBER, TREPLACE_ESTRING, TREPLACE_LSTRING, TREPLACE_NUMBER
		switch(message[i])
		{
		case TEXACT_STRING:
			dst += exactStrings[exSt++];
			break;
		case TLOCAL_STRING:
			{
				std::string hlp;
				getLocalString(localStrings[loSt++], hlp);
				dst += hlp;
			}
			break;
		case TNUMBER:
			dst += boost::lexical_cast<std::string>(numbers[nums++]);
			break;
		case TREPLACE_ESTRING:
			dst.replace (dst.find("%s"), 2, exactStrings[exSt++]);
			break;
		case TREPLACE_LSTRING:
			{
				std::string hlp;
				getLocalString(localStrings[loSt++], hlp);
				dst.replace (dst.find("%s"), 2, hlp);
			}
			break;
		case TREPLACE_NUMBER:
			dst.replace (dst.find("%d"), 2, boost::lexical_cast<std::string>(numbers[nums++]));
			break;
		default:
			tlog1 << "MetaString processing error!\n";
			break;
		}
	}
}

DLL_EXPORT std::string MetaString::buildList () const
///used to handle loot from creature bank
{

	size_t exSt = 0, loSt = 0, nums = 0;
	std::string lista;		
	for (int i = 0; i < message.size(); ++i)
	{
		if (i > 0 && message[i] == TEXACT_STRING || message[i] == TLOCAL_STRING)
		{
			if (exSt == exactStrings.size() - 1)
				lista += VLC->generaltexth->allTexts[141]; //" and "
			else
				lista += ", ";
		}
		switch (message[i])
		{
			case TEXACT_STRING:
				lista += exactStrings[exSt++];
				break;
			case TLOCAL_STRING:
			{
				std::string hlp;
				getLocalString (localStrings[loSt++], hlp);
				lista += hlp;
			}
				break;
			case TNUMBER:
				lista += boost::lexical_cast<std::string>(numbers[nums++]);
				break;
			case TREPLACE_ESTRING:
				lista.replace (lista.find("%s"), 2, exactStrings[exSt++]);
				break;
			case TREPLACE_LSTRING:
			{
				std::string hlp;
				getLocalString (localStrings[loSt++], hlp);
				lista.replace (lista.find("%s"), 2, hlp);
			}
				break;
			case TREPLACE_NUMBER:
				lista.replace (lista.find("%d"), 2, boost::lexical_cast<std::string>(numbers[nums++]));
				break;
			default:
				tlog1 << "MetaString processing error!\n";
		}

	}
	return lista;
}

static CGObjectInstance * createObject(int id, int subid, int3 pos, int owner)
{
	CGObjectInstance * nobj;
	switch(id)
	{
	case HEROI_TYPE: //hero
		{
			CGHeroInstance * nobj = new CGHeroInstance();
			nobj->pos = pos;
			nobj->tempOwner = owner;
			nobj->subID = subid;
			//nobj->initHero(ran);
			return nobj;
		}
	case TOWNI_TYPE: //town
		nobj = new CGTownInstance;
		break;
	default: //rest of objects
		nobj = new CGObjectInstance;
		nobj->defInfo = VLC->dobjinfo->gobjs[id][subid];
		break;
	}
	nobj->ID = id;
	nobj->subID = subid;
	if(!nobj->defInfo)
		tlog3 <<"No def declaration for " <<id <<" "<<subid<<std::endl;
	nobj->pos = pos;
	//nobj->state = NULL;//new CLuaObjectScript();
	nobj->tempOwner = owner;
	nobj->info = NULL;
	nobj->defInfo->id = id;
	nobj->defInfo->subid = subid;

	//assigning defhandler
	if(nobj->ID==HEROI_TYPE || nobj->ID==TOWNI_TYPE)
		return nobj;
	nobj->defInfo = VLC->dobjinfo->gobjs[id][subid];
	return nobj;
}

CStack * BattleInfo::getStack(int stackID, bool onlyAlive)
{
	for(unsigned int g=0; g<stacks.size(); ++g)
	{
		if(stacks[g]->ID == stackID && (!onlyAlive || stacks[g]->alive()))
			return stacks[g];
	}
	return NULL;
}

const CStack * BattleInfo::getStack(int stackID, bool onlyAlive) const
{
	return const_cast<BattleInfo * const>(this)->getStack(stackID, onlyAlive);
}

CStack * BattleInfo::getStackT(int tileID, bool onlyAlive)
{
	for(unsigned int g=0; g<stacks.size(); ++g)
	{
		if(stacks[g]->position == tileID 
			|| (stacks[g]->hasFeatureOfType(StackFeature::DOUBLE_WIDE) && stacks[g]->attackerOwned && stacks[g]->position-1 == tileID)
			|| (stacks[g]->hasFeatureOfType(StackFeature::DOUBLE_WIDE) && !stacks[g]->attackerOwned && stacks[g]->position+1 == tileID))
		{
			if(!onlyAlive || stacks[g]->alive())
			{
				return stacks[g];
			}
		}
	}
	return NULL;
}

const CStack * BattleInfo::getStackT(int tileID, bool onlyAlive) const
{
	return const_cast<BattleInfo * const>(this)->getStackT(tileID, onlyAlive);
}

void BattleInfo::getAccessibilityMap(bool *accessibility, bool twoHex, bool attackerOwned, bool addOccupiable, std::set<int> & occupyable, bool flying, int stackToOmmit) const
{
	memset(accessibility, 1, BFIELD_SIZE); //initialize array with trues

	//removing accessibility for side columns of hexes
	for(int v = 0; v < BFIELD_SIZE; ++v)
	{
		if( v % BFIELD_WIDTH == 0 || v % BFIELD_WIDTH == (BFIELD_WIDTH - 1) )
			accessibility[v] = false;
	}

	for(unsigned int g=0; g<stacks.size(); ++g)
	{
		if(!stacks[g]->alive() || stacks[g]->ID==stackToOmmit || stacks[g]->position < 0) //we don't want to lock position of this stack (eg. if it's a turret)
			continue;

		accessibility[stacks[g]->position] = false;
		if(stacks[g]->hasFeatureOfType(StackFeature::DOUBLE_WIDE)) //if it's a double hex creature
		{
			if(stacks[g]->attackerOwned)
				accessibility[stacks[g]->position-1] = false;
			else
				accessibility[stacks[g]->position+1] = false;
		}
	}
	//obstacles
	for(unsigned int b=0; b<obstacles.size(); ++b)
	{
		std::vector<int> blocked = VLC->heroh->obstacles[obstacles[b].ID].getBlocked(obstacles[b].pos);
		for(unsigned int c=0; c<blocked.size(); ++c)
		{
			if(blocked[c] >=0 && blocked[c] < BFIELD_SIZE)
				accessibility[blocked[c]] = false;
		}
	}

	//walls
	if(siege > 0)
	{
		static const int permanentlyLocked[] = {12, 45, 78, 112, 147, 165};
		for(int b=0; b<ARRAY_COUNT(permanentlyLocked); ++b)
		{
			accessibility[permanentlyLocked[b]] = false;
		}

		static const std::pair<int, int> lockedIfNotDestroyed[] = //(which part of wall, which hex is blocked if this part of wall is not destroyed
			{std::make_pair(2, 182), std::make_pair(3, 130), std::make_pair(4, 62), std::make_pair(5, 29)};
		for(int b=0; b<ARRAY_COUNT(lockedIfNotDestroyed); ++b)
		{
			if(si.wallState[lockedIfNotDestroyed[b].first] < 3)
			{
				accessibility[lockedIfNotDestroyed[b].second] = false;
			}
		}

		//gate
		if(attackerOwned && si.wallState[7] < 3) //if it attacker's unit and gate is not destroyed
		{
			accessibility[95] = accessibility[96] = false; //block gate's hexes
		}
	}

	//occupyability
	if(addOccupiable && twoHex)
	{
		std::set<int> rem; //tiles to unlock
		for(int h=0; h<BFIELD_HEIGHT; ++h)
		{
			for(int w=1; w<BFIELD_WIDTH-1; ++w)
			{
				int hex = h * BFIELD_WIDTH + w;
				if(!isAccessible(hex, accessibility, twoHex, attackerOwned, flying, true)
					&& (attackerOwned ? isAccessible(hex+1, accessibility, twoHex, attackerOwned, flying, true) : isAccessible(hex-1, accessibility, twoHex, attackerOwned, flying, true) )
					)
					rem.insert(hex);
			}
		}
		occupyable = rem;
		/*for(std::set<int>::const_iterator it = rem.begin(); it != rem.end(); ++it)
		{
			accessibility[*it] = true;
		}*/
	}
}

bool BattleInfo::isAccessible(int hex, bool * accessibility, bool twoHex, bool attackerOwned, bool flying, bool lastPos)
{
	if(flying && !lastPos)
		return true;

	if(twoHex)
	{
		//if given hex is accessible and appropriate adjacent one is free too
		return accessibility[hex] && accessibility[hex + (attackerOwned ? -1 : 1 )];
	}
	else
	{
		return accessibility[hex];
	}
}

void BattleInfo::makeBFS(int start, bool *accessibility, int *predecessor, int *dists, bool twoHex, bool attackerOwned, bool flying, bool fillPredecessors) const //both pointers must point to the at least 187-elements int arrays
{
	//inits
	for(int b=0; b<BFIELD_SIZE; ++b)
		predecessor[b] = -1;
	for(int g=0; g<BFIELD_SIZE; ++g)
		dists[g] = 100000000;	
	
	std::queue< std::pair<int, bool> > hexq; //bfs queue <hex, accessible> (second filed used only if fillPredecessors is true)
	hexq.push(std::make_pair(start, true));
	dists[hexq.front().first] = 0;
	int curNext = -1; //for bfs loop only (helper var)
	while(!hexq.empty()) //bfs loop
	{
		std::pair<int, bool> curHex = hexq.front();
		std::vector<int> neighbours = neighbouringTiles(curHex.first);
		hexq.pop();
		for(unsigned int nr=0; nr<neighbours.size(); nr++)
		{
			curNext = neighbours[nr]; //if(!accessibility[curNext] || (dists[curHex]+1)>=dists[curNext])
			bool accessible = isAccessible(curNext, accessibility, twoHex, attackerOwned, flying, dists[curHex.first]+1 == dists[curNext]);
			if( dists[curHex.first]+1 >= dists[curNext] )
				continue;
			if(accessible && curHex.second)
			{
				hexq.push(std::make_pair(curNext, true));
				dists[curNext] = dists[curHex.first] + 1;
			}
			else if(fillPredecessors && !(accessible && !curHex.second))
			{
				hexq.push(std::make_pair(curNext, false));
				dists[curNext] = dists[curHex.first] + 1;
			}
			predecessor[curNext] = curHex.first;
		}
	}
};

std::vector<int> BattleInfo::getAccessibility(int stackID, bool addOccupiable) const
{
	std::vector<int> ret;
	bool ac[BFIELD_SIZE];
	const CStack *s = getStack(stackID);

	if(s->position < 0) //turrets
		return std::vector<int>();

	std::set<int> occupyable;

	getAccessibilityMap(ac, s->hasFeatureOfType(StackFeature::DOUBLE_WIDE), s->attackerOwned, addOccupiable, occupyable, s->hasFeatureOfType(StackFeature::FLYING), stackID);

	int pr[BFIELD_SIZE], dist[BFIELD_SIZE];
	makeBFS(s->position, ac, pr, dist, s->hasFeatureOfType(StackFeature::DOUBLE_WIDE), s->attackerOwned, s->hasFeatureOfType(StackFeature::FLYING), false);

	if(s->hasFeatureOfType(StackFeature::DOUBLE_WIDE))
	{
		if(!addOccupiable)
		{
			std::vector<int> rem;
			for(int b=0; b<BFIELD_SIZE; ++b)
			{
				//don't take into account most left and most right columns of hexes
				if( b % BFIELD_WIDTH == 0 || b % BFIELD_WIDTH == BFIELD_WIDTH - 1 )
					continue;

				if( ac[b] && !(s->attackerOwned ? ac[b-1] : ac[b+1]) )
				{
					rem.push_back(b);
				}
			}

			for(unsigned int g=0; g<rem.size(); ++g)
			{
				ac[rem[g]] = false;
			}

			//removing accessibility for side hexes
			for(int v=0; v<BFIELD_SIZE; ++v)
				if(s->attackerOwned ? (v%BFIELD_WIDTH)==1 : (v%BFIELD_WIDTH)==(BFIELD_WIDTH - 2))
					ac[v] = false;
		}
	}
	
	for (int i=0; i < BFIELD_SIZE ; ++i) {
		if(
			( ( !addOccupiable && dist[i] <= s->Speed() && ac[i] ) || ( addOccupiable && dist[i] <= s->Speed() && isAccessible(i, ac, s->hasFeatureOfType(StackFeature::DOUBLE_WIDE), s->attackerOwned, s->hasFeatureOfType(StackFeature::FLYING), true) ) )//we can reach it
			|| (vstd::contains(occupyable, i) && ( dist[ i + (s->attackerOwned ? 1 : -1 ) ] <= s->Speed() ) &&
				ac[i + (s->attackerOwned ? 1 : -1 )] ) //it's occupyable and we can reach adjacent hex
			)
		{
			ret.push_back(i);
		}
	}

	return ret;
}
bool BattleInfo::isStackBlocked(int ID)
{
	CStack *our = getStack(ID);
	if(our->hasFeatureOfType(StackFeature::SIEGE_WEAPON)) //siege weapons cannot be blocked
		return false;

	for(unsigned int i=0; i<stacks.size();i++)
	{
		if( !stacks[i]->alive()
			|| stacks[i]->owner==our->owner
		  )
			continue; //we omit dead and allied stacks
		if(stacks[i]->hasFeatureOfType(StackFeature::DOUBLE_WIDE))
		{
			if( mutualPosition(stacks[i]->position, our->position) >= 0  
			  || mutualPosition(stacks[i]->position + (stacks[i]->attackerOwned ? -1 : 1), our->position) >= 0)
				return true;
		}
		else
		{
			if( mutualPosition(stacks[i]->position, our->position) >= 0 )
				return true;
		}
	}
	return false;
}

signed char BattleInfo::mutualPosition(int hex1, int hex2)
{
	if(hex2 == hex1 - ( (hex1/17)%2 ? 18 : 17 )) //top left
		return 0;
	if(hex2 == hex1 - ( (hex1/17)%2 ? 17 : 16 )) //top right
		return 1;
	if(hex2 == hex1 - 1 && hex1%17 != 0) //left
		return 5;
	if(hex2 == hex1 + 1 && hex1%17 != 16) //right
		return 2;
	if(hex2 == hex1 + ( (hex1/17)%2 ? 16 : 17 )) //bottom left
		return 4;
	if(hex2 == hex1 + ( (hex1/17)%2 ? 17 : 18 )) //bottom right
		return 3;
	return -1;
}

std::vector<int> BattleInfo::neighbouringTiles(int hex)
{
#define CHECK_AND_PUSH(tile) {int hlp = (tile); if(hlp>=0 && hlp<BFIELD_SIZE && (hlp%BFIELD_WIDTH!=16) && hlp%BFIELD_WIDTH) ret.push_back(hlp);}
	std::vector<int> ret;
	CHECK_AND_PUSH(hex - ( (hex/17)%2 ? 18 : 17 ));
	CHECK_AND_PUSH(hex - ( (hex/17)%2 ? 17 : 16 ));
	CHECK_AND_PUSH(hex - 1);
	CHECK_AND_PUSH(hex + 1);
	CHECK_AND_PUSH(hex + ( (hex/17)%2 ? 16 : 17 ));
	CHECK_AND_PUSH(hex + ( (hex/17)%2 ? 17 : 18 ));
#undef CHECK_AND_PUSH
	return ret;
}
std::pair< std::vector<int>, int > BattleInfo::getPath(int start, int dest, bool*accessibility, bool flyingCreature, bool twoHex, bool attackerOwned)
{							
	int predecessor[BFIELD_SIZE]; //for getting the Path
	int dist[BFIELD_SIZE]; //calculated distances

	makeBFS(start, accessibility, predecessor, dist, twoHex, attackerOwned, flyingCreature, false);
	
	if(predecessor[dest] == -1) //cannot reach destination
	{
		return std::make_pair(std::vector<int>(), 0);
	}

	//making the Path
	std::vector<int> path;
	int curElem = dest;
	while(curElem != start)
	{
		path.push_back(curElem);
		curElem = predecessor[curElem];
	}

	return std::make_pair(path, dist[dest]);
}

int CStack::valOfFeatures(StackFeature::ECombatFeatures type, int subtype, int turn) const
{
	int ret = 0;
	if(subtype == -1024) //any subtype
	{
		for(std::vector<StackFeature>::const_iterator i=features.begin(); i != features.end(); i++)
			if(i->type == type	&&  (!turn || i->turnsRemain > turn))
				ret += i->value;
	}
	else //given subtype
	{
		for(std::vector<StackFeature>::const_iterator i=features.begin(); i != features.end(); i++)
			if(i->type == type && i->subtype == subtype && (!turn || i->turnsRemain > turn))
				ret += i->value;
	}
	return ret;
}

bool CStack::hasFeatureOfType(StackFeature::ECombatFeatures type, int subtype, int turn) const
{
	if(subtype == -1024) //any subtype
	{
		for(std::vector<StackFeature>::const_iterator i=features.begin(); i != features.end(); i++)
			if(i->type == type && (!turn || i->turnsRemain > turn))
				return true;
	}
	else //given subtype
	{
		for(std::vector<StackFeature>::const_iterator i=features.begin(); i != features.end(); i++)
			if(i->type == type && i->subtype == subtype && (!turn || i->turnsRemain > turn))
				return true;
	}
	return false;
}

CStack::CStack(CCreature * C, int A, int O, int I, bool AO, int S)
	:ID(I), creature(C), amount(A), baseAmount(A), firstHPleft(C->hitPoints), owner(O), slot(S), attackerOwned(AO), position(-1),   
	counterAttacks(1), shots(C->shots), features(C->abilities)
{
	//additional retaliations
	for(int h=0; h<C->abilities.size(); ++h)
	{
		if(C->abilities[h].type == StackFeature::ADDITIONAL_RETALIATION)
		{
			counterAttacks += C->abilities[h].value;
		}
	}
	//alive state indication
	state.insert(ALIVE);
}

ui32 CStack::Speed( int turn /*= 0*/ ) const
{
	if(hasFeatureOfType(StackFeature::SIEGE_WEAPON, -1024, turn)) //war machnes cannot move
		return 0;

	int speed = creature->speed;

	speed += valOfFeatures(StackFeature::SPEED_BONUS, -1024, turn);

	int percentBonus = 0;
	for(int g=0; g<features.size(); ++g)
	{
		if(features[g].type == StackFeature::SPEED_BONUS, -1024, turn)
		{
			percentBonus += features[g].additionalInfo;
		}
	}

	if(percentBonus < 0)
	{
		speed = (abs(percentBonus) * speed)/100;
	}
	else
	{
		speed = ((100 + percentBonus) * speed)/100;
	}

	//bind effect check
	if(getEffect(72)) 
	{
		return 0;
	}

	return speed;
}

const CStack::StackEffect * CStack::getEffect( ui16 id, int turn /*= 0*/ ) const
{
	for (unsigned int i=0; i< effects.size(); i++)
		if(effects[i].id == id)
			if(!turn || effects[i].turnsRemain > turn)
				return &effects[i];
	return NULL;
}

ui8 CStack::howManyEffectsSet(ui16 id) const
{
	ui8 ret = 0;
	for (unsigned int i=0; i< effects.size(); i++)
		if(effects[i].id == id) //effect found
		{
			++ret;
		}
	return ret;
}

si8 CStack::Morale() const
{
	si8 ret = morale;

	if(hasFeatureOfType(StackFeature::NON_LIVING) || hasFeatureOfType(StackFeature::UNDEAD) || hasFeatureOfType(StackFeature::NO_MORALE))
		return 0;
	
	ret += valOfFeatures(StackFeature::MORALE_BONUS); //mirth & sorrow & other

	if(hasFeatureOfType(StackFeature::SELF_MORALE)) //eg. minotaur
	{
		ret = std::max<si8>(ret, +1);
	}

	if(ret > 3) ret = 3;
	if(ret < -3) ret = -3;
	return ret;
}

si8 CStack::Luck() const
{
	si8 ret = luck;

	if(hasFeatureOfType(StackFeature::NO_LUCK))
		return 0;
	
	ret += valOfFeatures(StackFeature::LUCK_BONUS); //fortune & misfortune & other

	if(hasFeatureOfType(StackFeature::SELF_LUCK)) //eg. halfling
	{
		ret = std::max<si8>(ret, +1);
	}

	if(ret > 3) ret = 3;
	if(ret < -3) ret = -3;
	return ret;
}

si32 CStack::Attack() const
{
	si32 ret = creature->attack; //value to be returned

	if(hasFeatureOfType(StackFeature::IN_FRENZY)) //frenzy for attacker
	{
		ret += si32(VLC->spellh->spells[56].powers[getEffect(56)->level]/100.0) * Defense(false);
	}

	ret += valOfFeatures(StackFeature::ATTACK_BONUS);

	return ret;
}

si32 CStack::Defense(bool withFrenzy /*= true*/) const
{
	si32 ret = creature->defence;

	if(withFrenzy && getEffect(56)) //frenzy for defender
	{
		return 0;
	}

	ret += valOfFeatures(StackFeature::DEFENCE_BONUS);

	return ret;
}

ui16 CStack::MaxHealth() const
{
	return creature->hitPoints + valOfFeatures(StackFeature::HP_BONUS);
}

bool CStack::willMove(int turn /*= 0*/) const
{
	return ( turn ? true : !vstd::contains(state, DEFENDING) )
		&& !moved(turn)
		&& canMove(turn);
}

bool CStack::canMove( int turn /*= 0*/ ) const
{
	return alive()
		&& !hasFeatureOfType(StackFeature::NOT_ACTIVE, -1024, turn); //eg. Ammo Cart
}

bool CStack::moved( int turn /*= 0*/ ) const
{
	if(!turn)
		return vstd::contains(state, MOVED);
	else
		return false;
}

CGHeroInstance * CGameState::HeroesPool::pickHeroFor(bool native, int player, const CTown *town, std::map<ui32,CGHeroInstance *> &available) const
{
	CGHeroInstance *ret = NULL;

	if(player<0 || player>=PLAYER_LIMIT)
	{
		tlog1 << "Cannot pick hero for " << town->Name() << ". Wrong owner!\n";
		return NULL;
	}

	std::vector<CGHeroInstance *> pool;

	if(native)
	{
		for(std::map<ui32,CGHeroInstance *>::iterator i=available.begin(); i!=available.end(); i++)
		{
			if(pavailable.find(i->first)->second & 1<<player
				&& i->second->type->heroType/2 == town->typeID)
			{
				pool.push_back(i->second);
			}
		}
		if(!pool.size())
		{
			tlog1 << "Cannot pick native hero for " << player << ". Picking any...\n";
			return pickHeroFor(false, player, town, available);
		}
		else
		{
			ret = pool[rand()%pool.size()];
		}
	}
	else
	{
		int sum=0, r;

		for(std::map<ui32,CGHeroInstance *>::iterator i=available.begin(); i!=available.end(); i++)
		{
			if(pavailable.find(i->first)->second & 1<<player)
			{
				pool.push_back(i->second);
				sum += i->second->type->heroClass->selectionProbability[town->typeID];
			}
		}
		if(!pool.size())
		{
			tlog1 << "There are no heroes available for player " << player<<"!\n";
			return NULL;
		}

		r = rand()%sum;
		for(unsigned int i=0; i<pool.size(); i++)
		{
			r -= pool[i]->type->heroClass->selectionProbability[town->typeID];
			if(r<0)
				ret = pool[i];
		}
		if(!ret)
			ret = pool.back();
	}

	available.erase(ret->subID);
	return ret;
}



//void CGameState::apply(CPack * pack)
//{
//	while(!mx->try_lock())
//		boost::this_thread::sleep(boost::posix_time::milliseconds(50)); //give other threads time to finish
//	//applyNL(pack);
//	mx->unlock();
//}
int CGameState::pickHero(int owner)
{
	int h=-1;
	if(!map->getHero(h = scenarioOps->getIthPlayersSettings(owner).hero,0)  &&  h>=0) //we haven't used selected hero
		return h;
	int f = scenarioOps->getIthPlayersSettings(owner).castle;
	int i=0;
	do //try to find free hero of our faction
	{
		i++;
		h = scenarioOps->getIthPlayersSettings(owner).castle*HEROES_PER_TYPE*2+(ran()%(HEROES_PER_TYPE*2));//->scenarioOps->playerInfos[pru].hero = VLC->
	} while( map->getHero(h)  &&  i<175);
	if(i>174) //probably no free heroes - there's no point in further search, we'll take first free
	{
		tlog3 << "Warning: cannot find free hero - trying to get first available..."<<std::endl;
		for(int j=0; j<HEROES_PER_TYPE * 2 * F_NUMBER; j++)
			if(!map->getHero(j))
				h=j;
	}
	return h;
}
CGHeroInstance *CGameState::getHero(int objid)
{
	if(objid<0 || objid>=map->objects.size() || map->objects[objid]->ID!=HEROI_TYPE)
		return NULL;
	return static_cast<CGHeroInstance *>(map->objects[objid]);
}
CGTownInstance *CGameState::getTown(int objid)
{
	if(objid<0 || objid>=map->objects.size())
		return NULL;
	return static_cast<CGTownInstance *>(map->objects[objid]);
}
std::pair<int,int> CGameState::pickObject(CGObjectInstance *obj)
{
	switch(obj->ID)
	{
	case 65: //random artifact
		return std::pair<int,int>(5,(ran()%136)+7); //tylko sensowny zakres - na poczatku sa katapulty itp, na koncu specjalne i blanki
	case 66: //random treasure artifact
		return std::pair<int,int>(5,VLC->arth->treasures[ran()%VLC->arth->treasures.size()]->id);
	case 67: //random minor artifact
		return std::pair<int,int>(5,VLC->arth->minors[ran()%VLC->arth->minors.size()]->id);
	case 68: //random major artifact
		return std::pair<int,int>(5,VLC->arth->majors[ran()%VLC->arth->majors.size()]->id);
	case 69: //random relic artifact
		return std::pair<int,int>(5,VLC->arth->relics[ran()%VLC->arth->relics.size()]->id);
	case 70: //random hero
		{
			return std::pair<int,int>(HEROI_TYPE,pickHero(obj->tempOwner));
		}
	case 71: //random monster
		{
			int r;
			do 
			{
				r = ran()%197;
			} while (vstd::contains(VLC->creh->notUsedMonsters,r));
			return std::pair<int,int>(54,r); 
		}
	case 72: //random monster lvl1
		return std::pair<int,int>(54,VLC->creh->levelCreatures[1][ran()%VLC->creh->levelCreatures[1].size()]->idNumber); 
	case 73: //random monster lvl2
		return std::pair<int,int>(54,VLC->creh->levelCreatures[2][ran()%VLC->creh->levelCreatures[2].size()]->idNumber);
	case 74: //random monster lvl3
		return std::pair<int,int>(54,VLC->creh->levelCreatures[3][ran()%VLC->creh->levelCreatures[3].size()]->idNumber);
	case 75: //random monster lvl4
		return std::pair<int,int>(54,VLC->creh->levelCreatures[4][ran()%VLC->creh->levelCreatures[4].size()]->idNumber);
	case 76: //random resource
		return std::pair<int,int>(79,ran()%7); //now it's OH3 style, use %8 for mithril 
	case 77: //random town
		{
			int align = (static_cast<CGTownInstance*>(obj))->alignment,
				f;
			if(align>PLAYER_LIMIT-1)//same as owner / random
			{
				if(obj->tempOwner > PLAYER_LIMIT-1)
					f = -1; //random
				else
					f = scenarioOps->getIthPlayersSettings(obj->tempOwner).castle;
			}
			else
			{
				f = scenarioOps->getIthPlayersSettings(align).castle;
			}
			if(f<0) f = ran()%VLC->townh->towns.size();
			return std::pair<int,int>(TOWNI_TYPE,f); 
		}
	case 162: //random monster lvl5
		return std::pair<int,int>(54,VLC->creh->levelCreatures[5][ran()%VLC->creh->levelCreatures[5].size()]->idNumber);
	case 163: //random monster lvl6
		return std::pair<int,int>(54,VLC->creh->levelCreatures[6][ran()%VLC->creh->levelCreatures[6].size()]->idNumber);
	case 164: //random monster lvl7
		return std::pair<int,int>(54,VLC->creh->levelCreatures[7][ran()%VLC->creh->levelCreatures[7].size()]->idNumber); 
	case 216: //random dwelling
		{
			int faction = ran()%F_NUMBER;
			CCreGen2ObjInfo* info = static_cast<CCreGen2ObjInfo*>(obj->info);
			if (info->asCastle)
			{
				for(unsigned int i=0;i<map->objects.size();i++)
				{
					if(map->objects[i]->ID==77 && dynamic_cast<CGTownInstance*>(map->objects[i])->identifier == info->identifier)
					{
						randomizeObject(map->objects[i]); //we have to randomize the castle first
						faction = map->objects[i]->subID;
						break;
					}
					else if(map->objects[i]->ID==TOWNI_TYPE && dynamic_cast<CGTownInstance*>(map->objects[i])->identifier == info->identifier)
					{
						faction = map->objects[i]->subID;
						break;
					}
				}
			}
			else
			{
				while((!(info->castles[0]&(1<<faction))))
				{
					if((faction>7) && (info->castles[1]&(1<<(faction-8))))
						break;
					faction = ran()%F_NUMBER;
				}
			}
			int level = ((info->maxLevel-info->minLevel) ? (ran()%(info->maxLevel-info->minLevel)+info->minLevel) : (info->minLevel));
			int cid = VLC->townh->towns[faction].basicCreatures[level];
			for(unsigned int i=0;i<VLC->objh->cregens.size();i++)
				if(VLC->objh->cregens[i]==cid)
					return std::pair<int,int>(17,i); 
			tlog3 << "Cannot find a dwelling for creature "<<cid <<std::endl;
			return std::pair<int,int>(17,0); 
		}
	case 217:
		{
			int faction = ran()%F_NUMBER;
			CCreGenObjInfo* info = static_cast<CCreGenObjInfo*>(obj->info);
			if (info->asCastle)
			{
				for(unsigned int i=0;i<map->objects.size();i++)
				{
					if(map->objects[i]->ID==77 && dynamic_cast<CGTownInstance*>(map->objects[i])->identifier == info->identifier)
					{
						randomizeObject(map->objects[i]); //we have to randomize the castle first
						faction = map->objects[i]->subID;
						break;
					}
					else if(map->objects[i]->ID==TOWNI_TYPE && dynamic_cast<CGTownInstance*>(map->objects[i])->identifier == info->identifier)
					{
						faction = map->objects[i]->subID;
						break;
					}
				}
			}
			else
			{
				while((!(info->castles[0]&(1<<faction))))
				{
					if((faction>7) && (info->castles[1]&(1<<(faction-8))))
						break;
					faction = ran()%F_NUMBER;
				}
			}
			int cid = VLC->townh->towns[faction].basicCreatures[obj->subID];
			for(unsigned int i=0;i<VLC->objh->cregens.size();i++)
				if(VLC->objh->cregens[i]==cid)
					return std::pair<int,int>(17,i); 
			tlog3 << "Cannot find a dwelling for creature "<<cid <<std::endl;
			return std::pair<int,int>(17,0); 
		}
	case 218:
		{
			CCreGen3ObjInfo* info = static_cast<CCreGen3ObjInfo*>(obj->info);
			int level = ((info->maxLevel-info->minLevel) ? (ran()%(info->maxLevel-info->minLevel)+info->minLevel) : (info->minLevel));
			int cid = VLC->townh->towns[obj->subID].basicCreatures[level];
			for(unsigned int i=0;i<VLC->objh->cregens.size();i++)
				if(VLC->objh->cregens[i]==cid)
					return std::pair<int,int>(17,i); 
			tlog3 << "Cannot find a dwelling for creature "<<cid <<std::endl;
			return std::pair<int,int>(17,0); 
		}
	}
	return std::pair<int,int>(-1,-1);
}
void CGameState::randomizeObject(CGObjectInstance *cur)
{		
	std::pair<int,int> ran = pickObject(cur);
	if(ran.first<0 || ran.second<0) //this is not a random object, or we couldn't find anything
	{
		if(cur->ID==TOWNI_TYPE) //town - set def
		{
			CGTownInstance *t = dynamic_cast<CGTownInstance*>(cur);
			if(t->hasCapitol())
				t->defInfo = capitols[t->subID];
			else if(t->hasFort())
				t->defInfo = forts[t->subID];
			else
				t->defInfo = villages[t->subID]; 
		}
		return;
	}
	else if(ran.first==HEROI_TYPE)//special code for hero
	{
		CGHeroInstance *h = dynamic_cast<CGHeroInstance *>(cur);
		if(!h) {tlog2<<"Wrong random hero at "<<cur->pos<<std::endl; return;}
		cur->ID = ran.first;
		h->portrait = cur->subID = ran.second;
		h->type = VLC->heroh->heroes[ran.second];
		map->heroes.push_back(h);
		return; //TODO: maybe we should do something with definfo?
	}
	else if(ran.first==TOWNI_TYPE)//special code for town
	{
		CGTownInstance *t = dynamic_cast<CGTownInstance*>(cur);
		if(!t) {tlog2<<"Wrong random town at "<<cur->pos<<std::endl; return;}
		cur->ID = ran.first;
		cur->subID = ran.second;
		t->town = &VLC->townh->towns[ran.second];
		if(t->hasCapitol())
			t->defInfo = capitols[t->subID];
		else if(t->hasFort())
			t->defInfo = forts[t->subID];
		else
			t->defInfo = villages[t->subID]; 
		map->towns.push_back(t);
		return;
	}
	//we have to replace normal random object
	cur->ID = ran.first;
	cur->subID = ran.second;
	map->removeBlockVisTiles(cur); //recalculate blockvis tiles - picked object might have different than random placeholder
	map->defy.push_back(cur->defInfo = VLC->dobjinfo->gobjs[ran.first][ran.second]);
	if(!cur->defInfo)
	{
		tlog1<<"*BIG* WARNING: Missing def declaration for "<<cur->ID<<" "<<cur->subID<<std::endl;
		return;
	}

	map->addBlockVisTiles(cur);
}

int CGameState::getDate(int mode) const
{
	int temp;
	switch (mode)
	{
	case 0:
		return day;
		break;
	case 1:
		temp = (day)%7;
		if (temp)
			return temp;
		else return 7;
		break;
	case 2:
		temp = ((day-1)/7)+1;
		if (!(temp%4))
			return 4;
		else 
			return (temp%4);
		break;
	case 3:
		return ((day-1)/28)+1;
		break;
	}
	return 0;
}
CGameState::CGameState()
{
	mx = new boost::shared_mutex();
	map = NULL;
	curB = NULL;
	scenarioOps = NULL;
	applierGs = new CGSApplier;
	objCaller = new CObjectCallersHandler;
}
CGameState::~CGameState()
{
	delete mx;
	delete map;
	delete curB;
	delete scenarioOps;
	delete applierGs;
	delete objCaller;
}
void CGameState::init(StartInfo * si, Mapa * map, int Seed)
{
	day = 0;
	seed = Seed;
	ran.seed((boost::int32_t)seed);
	scenarioOps = si;
	this->map = map;
	loadTownDInfos();
	//picking random factions for players
	for(unsigned int i=0;i<scenarioOps->playerInfos.size();i++)
	{
		if(scenarioOps->playerInfos[i].castle==-1)
		{
			int f;
			do
			{
				f = ran()%F_NUMBER;
			}while(!(map->players[scenarioOps->playerInfos[i].color].allowedFactions  &  1<<f));
			scenarioOps->playerInfos[i].castle = f;
		}
	}
	//randomizing objects
	for(unsigned int no=0; no<map->objects.size(); ++no)
	{
		randomizeObject(map->objects[no]);
		if(map->objects[no]->ID==26)
		{
			map->objects[no]->defInfo->handler=NULL;
		}
		map->objects[no]->hoverName = VLC->generaltexth->names[map->objects[no]->ID];
	}
	//std::cout<<"\tRandomizing objects: "<<th.getDif()<<std::endl;

	/*********give starting hero****************************************/
	for(int i=0;i<PLAYER_LIMIT;i++)
	{
		if((map->players[i].generateHeroAtMainTown && map->players[i].hasMainTown) ||  (map->players[i].hasMainTown && map->version==CMapHeader::RoE))
		{
			int3 hpos = map->players[i].posOfMainTown;
			hpos.x+=1;// hpos.y+=1;
			int j;
			for(j=0; j<scenarioOps->playerInfos.size(); j++) //don't add unsigned here - we are refering to the variable above
				if(scenarioOps->playerInfos[j].color == i)
					break;
			if(j == scenarioOps->playerInfos.size())
				continue;

			int h=pickHero(i);
			if(scenarioOps->playerInfos[j].hero == -1)
				scenarioOps->playerInfos[j].hero = h;

			CGHeroInstance * nnn =  static_cast<CGHeroInstance*>(createObject(HEROI_TYPE,h,hpos,i));
			nnn->id = map->objects.size();
			hpos = map->players[i].posOfMainTown;hpos.x+=2;
			for(unsigned int o=0;o<map->towns.size();o++) //find main town
			{
				if(map->towns[o]->pos == hpos)
				{
					map->towns[o]->visitingHero = nnn;
					nnn->visitedTown = map->towns[o];
					nnn->inTownGarrison = false;
					break;
				}
			}
			nnn->initHero();
			map->heroes.push_back(nnn);
			map->objects.push_back(nnn);
			map->addBlockVisTiles(nnn);
		}
	}

	/*********creating players entries in gs****************************************/
	for (unsigned int i=0; i<scenarioOps->playerInfos.size();i++)
	{
		std::pair<int,PlayerState> ins(scenarioOps->playerInfos[i].color,PlayerState());
		ins.second.color=ins.first;
		ins.second.serial=i;
		ins.second.human = scenarioOps->playerInfos[i].human;
		players.insert(ins);
	}
	/******************RESOURCES****************************************************/
	//TODO: computer player should receive other amount of resource than computer (depending on difficulty)
	std::vector<int> startres;
	std::ifstream tis("config/startres.txt");
	int k;
	for (int j=0;j<scenarioOps->difficulty;j++)
	{
		tis >> k;
		for (int z=0;z<RESOURCE_QUANTITY;z++)
			tis>>k;
	}
	tis >> k;
	for (int i=0;i<RESOURCE_QUANTITY;i++)
	{
		tis >> k;
		startres.push_back(k);
	}
	tis.close();
	tis.clear();
	for (std::map<ui8,PlayerState>::iterator i = players.begin(); i!=players.end(); i++)
	{
		(*i).second.resources.resize(RESOURCE_QUANTITY);
		for (int x=0;x<RESOURCE_QUANTITY;x++)
			(*i).second.resources[x] = startres[x];
	}

	tis.open("config/resources.txt");
	tis >> k;
	int pom;
	for(int i=0;i<k;i++)
	{
		tis >> pom;
		resVals.push_back(pom);
	}

	/*************************HEROES************************************************/
	std::set<int> hids;
	for(unsigned int i=0; i<map->allowedHeroes.size(); i++) //add to hids all allowed heroes
		if(map->allowedHeroes[i])
			hids.insert(i);
	for (unsigned int i=0; i<map->heroes.size();i++) //heroes instances initialization
	{
		if (map->heroes[i]->getOwner()<0)
		{
			tlog2 << "Warning - hero with uninitialized owner!\n";
			continue;
		}
		CGHeroInstance * vhi = (map->heroes[i]);
		vhi->initHero();
		players.find(vhi->getOwner())->second.heroes.push_back(vhi);
		hids.erase(vhi->subID);
	}
	for(unsigned int i=0; i<map->predefinedHeroes.size(); i++)
	{
		if(!vstd::contains(hids,map->predefinedHeroes[i]->subID))
			continue;
		map->predefinedHeroes[i]->initHero();
		hpool.heroesPool[map->predefinedHeroes[i]->subID] = map->predefinedHeroes[i];
		hpool.pavailable[map->predefinedHeroes[i]->subID] = 0xff;
		hids.erase(map->predefinedHeroes[i]->subID);
	}
	BOOST_FOREACH(int hid, hids) //all not used allowed heroes go into the pool
	{
		CGHeroInstance * vhi = new CGHeroInstance();
		vhi->initHero(hid);
		hpool.heroesPool[hid] = vhi;
		hpool.pavailable[hid] = 0xff;
	}
	for(unsigned int i=0; i<map->disposedHeroes.size(); i++)
	{
		hpool.pavailable[map->disposedHeroes[i].ID] = map->disposedHeroes[i].players;
	}
	/*************************FOG**OF**WAR******************************************/		
	for(std::map<ui8, PlayerState>::iterator k=players.begin(); k!=players.end(); ++k)
	{
		k->second.fogOfWarMap.resize(map->width);
		for(int g=0; g<map->width; ++g)
			k->second.fogOfWarMap[g].resize(map->height);

		for(int g=-0; g<map->width; ++g)
			for(int h=0; h<map->height; ++h)
				k->second.fogOfWarMap[g][h].resize(map->twoLevel+1, 0);

		for(int g=0; g<map->width; ++g)
			for(int h=0; h<map->height; ++h)
				for(int v=0; v<map->twoLevel+1; ++v)
					k->second.fogOfWarMap[g][h][v] = 0;

		BOOST_FOREACH(CGObjectInstance *obj, map->objects)
		{
			if(obj->tempOwner != k->first) continue; //not a flagged object

			int3 objCenter = obj->getSightCenter();
			int radious = obj->getSightRadious();

			for (int xd = std::max<int>(objCenter.x - radious , 0); xd <= std::min<int>(objCenter.x + radious, map->width - 1); xd++)
			{
				for (int yd = std::max<int>(objCenter.y - radious, 0); yd <= std::min<int>(objCenter.y + radious, map->height - 1); yd++)
				{
					double distance = objCenter.dist2d(int3(xd,yd,objCenter.z)) - 0.5;
					if(distance <= radious)
						k->second.fogOfWarMap[xd][yd][objCenter.z] = 1;
				}
			}
		}

		//for(int xd=0; xd<map->width; ++xd) //revealing part of map around heroes
		//{
		//	for(int yd=0; yd<map->height; ++yd)
		//	{
		//		for(int ch=0; ch<k->second.heroes.size(); ++ch)
		//		{
		//			int deltaX = (k->second.heroes[ch]->getPosition(false).x-xd)*(k->second.heroes[ch]->getPosition(false).x-xd);
		//			int deltaY = (k->second.heroes[ch]->getPosition(false).y-yd)*(k->second.heroes[ch]->getPosition(false).y-yd);
		//			if(deltaX+deltaY<k->second.heroes[ch]->getSightDistance()*k->second.heroes[ch]->getSightDistance())
		//				k->second.fogOfWarMap[xd][yd][k->second.heroes[ch]->getPosition(false).z] = 1;
		//		}
		//	}
		//}

		//starting bonus
		if(si->playerInfos[k->second.serial].bonus==brandom)
			si->playerInfos[k->second.serial].bonus = ran()%3;
		switch(si->playerInfos[k->second.serial].bonus)
		{
		case bgold:
			k->second.resources[6] += 500 + (ran()%6)*100;
			break;
		case bresource:
			{
				int res = VLC->townh->towns[si->playerInfos[k->second.serial].castle].primaryRes;
				if(res == 127)
				{
					k->second.resources[0] += 5 + ran()%6;
					k->second.resources[2] += 5 + ran()%6;
				}
				else
				{
					k->second.resources[res] += 3 + ran()%4;
				}
				break;
			}
		case bartifact:
			{
				if(!k->second.heroes.size())
				{
					tlog5 << "Cannot give starting artifact - no heroes!" << std::endl;
					break;
				}
				CArtifact *toGive;
				do 
				{
					toGive = VLC->arth->treasures[ran() % VLC->arth->treasures.size()];
				} while (!map->allowedArtifact[toGive->id]);
				CGHeroInstance *hero = k->second.heroes[0];
				std::vector<ui16>::iterator slot = vstd::findFirstNot(hero->artifWorn,toGive->possibleSlots);
				if(slot!=toGive->possibleSlots.end())
				{
					hero->artifWorn[*slot] = toGive->id;
					hero->recreateArtBonuses();
				}
				else
					hero->artifacts.push_back(toGive->id);
			}
		}
	}
	/****************************TOWNS************************************************/
	for (unsigned int i=0;i<map->towns.size();i++)
	{
		CGTownInstance * vti =(map->towns[i]);
		if(!vti->town)
			vti->town = &VLC->townh->towns[vti->subID];
		if (vti->name.length()==0) // if town hasn't name we draw it
			vti->name = vti->town->Names()[ran()%vti->town->Names().size()];

		//init buildings
		if(vti->builtBuildings.find(-50)!=vti->builtBuildings.end()) //give standard set of buildings
		{
			vti->builtBuildings.erase(-50);
			vti->builtBuildings.insert(10);
			vti->builtBuildings.insert(5);
			vti->builtBuildings.insert(30);
			if(ran()%2)
				vti->builtBuildings.insert(31);
		}

		//init spells
		vti->spells.resize(SPELL_LEVELS);
		CSpell *s;
		for(unsigned int z=0; z<vti->obligatorySpells.size();z++)
		{
			s = &VLC->spellh->spells[vti->obligatorySpells[z]];
			vti->spells[s->level-1].push_back(s->id);
			vti->possibleSpells -= s->id;
		}
		while(vti->possibleSpells.size())
		{
			ui32 total=0, sel=-1;
			for(unsigned int ps=0;ps<vti->possibleSpells.size();ps++)
				total += VLC->spellh->spells[vti->possibleSpells[ps]].probabilities[vti->subID];
			int r = (total)? ran()%total : -1;
			for(unsigned int ps=0; ps<vti->possibleSpells.size();ps++)
			{
				r -= VLC->spellh->spells[vti->possibleSpells[ps]].probabilities[vti->subID];
				if(r<0)
				{
					sel = ps;
					break;
				}
			}
			if(sel<0)
				sel=0;

			CSpell *s = &VLC->spellh->spells[vti->possibleSpells[sel]];
			vti->spells[s->level-1].push_back(s->id);
			vti->possibleSpells -= s->id;
		}

		//init garrisons
		for (std::map<si32,std::pair<ui32,si32> >::iterator j=vti->army.slots.begin(); j!=vti->army.slots.end();j++)
		{
			if(j->second.first > 196 && j->second.first < 211)
			{
				if(j->second.first%2)
					j->second.first = vti->town->basicCreatures[ (j->second.first-197) / 2 ];
				else
					j->second.first = vti->town->upgradedCreatures[ (j->second.first-197) / 2 ];
			}
		}
		if(vti->getOwner() != 255)
			getPlayer(vti->getOwner())->towns.push_back(vti);
	}

	for(std::map<ui8, PlayerState>::iterator k=players.begin(); k!=players.end(); ++k)
	{
		if(k->first==-1 || k->first==255)
			continue;

		//init visiting and garrisoned heroes
		for(unsigned int l=0; l<k->second.heroes.size();l++)
		{ 
			CGHeroInstance *h = k->second.heroes[l];
			for(unsigned int m=0; m<k->second.towns.size();m++)
			{
				CGTownInstance *t = k->second.towns[m];
				int3 vistile = t->pos; vistile.x--; //tile next to the entrance
				if(vistile == h->pos || h->pos==t->pos)
				{
					t->visitingHero = h;
					h->visitedTown = t;
					h->inTownGarrison = false;
					if(h->pos == t->pos) //visiting hero placed in the editor has same pos as the town - we need to correct it
					{
						map->removeBlockVisTiles(h);
						h->pos.x -= 1;
						map->addBlockVisTiles(h);
					}
					break;
				}
			}
		}
	}

	for(unsigned int i=0; i<map->defy.size(); i++)
	{
		map->defy[i]->serial = i;
	}

	objCaller->preInit();
	for(unsigned int i=0; i<map->objects.size(); i++)
	{
		map->objects[i]->initObj();
		if(map->objects[i]->ID == 62) //prison also needs to initialize hero
			static_cast<CGHeroInstance*>(map->objects[i])->initHero();
	}
	objCaller->postInit();
}

bool CGameState::battleShootCreatureStack(int ID, int dest)
{
	return true;
}

bool CGameState::battleCanFlee(int player)
{
	if(!curB) //there is no battle
		return false;

	const CGHeroInstance *h1 = getHero(curB->hero1);
	const CGHeroInstance *h2 = getHero(curB->hero2);

	if(h1 && h1->hasBonusOfType(HeroBonus::ENEMY_CANT_ESCAPE) //eg. one of heroes is wearing shakles of war
		|| h2 && h2->hasBonusOfType(HeroBonus::ENEMY_CANT_ESCAPE))
		return false;

	return true;
}

int CGameState::battleGetStack(int pos, bool onlyAlive)
{
	if(!curB)
		return -1;
	for(unsigned int g=0; g<curB->stacks.size(); ++g)
	{
		if((curB->stacks[g]->position == pos 
			  || (curB->stacks[g]->hasFeatureOfType(StackFeature::DOUBLE_WIDE) 
					&&( (curB->stacks[g]->attackerOwned && curB->stacks[g]->position-1 == pos) 
					||	(!curB->stacks[g]->attackerOwned && curB->stacks[g]->position+1 == pos)	)
			 ))
			 && (!onlyAlive || curB->stacks[g]->alive())
		  )
			return curB->stacks[g]->ID;
	}
	return -1;
}

int CGameState::battleGetBattlefieldType(int3 tile)
{
	if(tile==int3() && curB)
		tile = curB->tile;
	else if(tile==int3() && !curB)
		return -1;

	const std::vector <CGObjectInstance*> & objs = map->objects;
	for(int g=0; g<objs.size(); ++g)
	{
		if( !objs[g] || objs[g]->pos.x - tile.x < 0  ||  objs[g]->pos.x - tile.x >= 8  
			||  tile.y - objs[g]->pos.y + 5 < 0  ||  tile.y - objs[g]->pos.y + 5 >=6 
			|| !objs[g]->coveringAt(objs[g]->pos.x - tile.x, tile.y - objs[g]->pos.y + 5)
			) //look only for objects covering given tile
			continue;
		switch(objs[g]->ID)
		{
		case 222: //clover field
			return 19;
		case 21: case 223: //cursed ground
			return 22;
		case 224: //evil fog
			return 20;
		case 225: //favourable winds
			return 21;
		case 226: //fiery fields
			return 14;
		case 227: //holy ground
			return 18;
		case 228: //lucid pools
			return 17;
		case 229: //magic clouds
			return 16;
		case 46: case 230: //magic plains
			return 9;
		case 231: //rocklands
			return 15;
		}
	}

	switch(map->terrain[tile.x][tile.y][tile.z].tertype)
	{
	case TerrainTile::dirt:
		return rand()%3+3;
	case TerrainTile::sand:
		return 2; //TODO: coast support
	case TerrainTile::grass:
		return rand()%2+6;
	case TerrainTile::snow:
		return rand()%2+10;
	case TerrainTile::swamp:
		return 13;
	case TerrainTile::rough:
		return 23;
	case TerrainTile::subterranean:
		return 12;
	case TerrainTile::lava:
		return 8;
	case TerrainTile::water:
		return 25;
	case TerrainTile::rock:
		return 15;
	default:
		return -1;
	}
}

const CGHeroInstance * CGameState::battleGetOwner(int stackID)
{
	if(!curB)
		return NULL;

	si32 ourHero = curB->getStack(stackID)->attackerOwned ? curB->hero1 : curB->hero2;
	return getHero(ourHero);

}

UpgradeInfo CGameState::getUpgradeInfo(const CArmedInstance *obj, int stackPos)
{
	UpgradeInfo ret;
	const CCreature *base = &VLC->creh->creatures[obj->army.slots.find(stackPos)->first];
	if((obj->ID == TOWNI_TYPE)  ||  ((obj->ID == HEROI_TYPE) && static_cast<const CGHeroInstance*>(obj)->visitedTown))
	{
		const CGTownInstance * t;
		if(obj->ID == TOWNI_TYPE)
			t = static_cast<const CGTownInstance *>(obj);
		else
			t = static_cast<const CGHeroInstance*>(obj)->visitedTown;
		for(std::set<si32>::const_iterator i=t->builtBuildings.begin();  i!=t->builtBuildings.end(); i++)
		{
			if( (*i) >= 37   &&   (*i) < 44 ) //upgraded creature dwelling
			{
				int nid = t->town->upgradedCreatures[(*i)-37]; //upgrade offered by that building
				if(base->upgrades.find(nid) != base->upgrades.end()) //possible upgrade
				{
					ret.newID.push_back(nid);
					ret.cost.push_back(std::set<std::pair<int,int> >());
					for(int j=0;j<RESOURCE_QUANTITY;j++)
					{
						int dif = VLC->creh->creatures[nid].cost[j] - base->cost[j];
						if(dif)
							ret.cost[ret.cost.size()-1].insert(std::make_pair(j,dif));
					}
				}
			}
		}//end for
	}
	//TODO: check if hero ability makes some upgrades possible

	if(ret.newID.size())
		ret.oldID = base->idNumber;

	return ret;
}

float CGameState::getMarketEfficiency( int player, int mode/*=0*/ )
{
	boost::shared_lock<boost::shared_mutex> lock(*mx);
	if(mode) return -1; //todo - support other modes
	int mcount = 0;
	for(unsigned int i=0;i<getPlayer(player)->towns.size();i++)
		if(vstd::contains(getPlayer(player)->towns[i]->builtBuildings,14))
			mcount++;
	float ret = std::min(((float)mcount+1.0f)/20.0f,0.5f);
	return ret;
}

void CGameState::loadTownDInfos()
{
	for(int i=0;i<F_NUMBER;i++)
	{
		villages[i] = new CGDefInfo(*VLC->dobjinfo->castles[i]);
		forts[i] = VLC->dobjinfo->castles[i];
		capitols[i] = new CGDefInfo(*VLC->dobjinfo->castles[i]);
	}
}

void CGameState::getNeighbours( const TerrainTile &srct, int3 tile, std::vector<int3> &vec, const boost::logic::tribool &onLand )
{
	static int3 dirs[] = { int3(0,1,0),int3(0,-1,0),int3(-1,0,0),int3(+1,0,0),
					int3(1,1,0),int3(-1,1,0),int3(1,-1,0),int3(-1,-1,0) };

	vec.clear();
	for (size_t i = 0; i < ARRAY_COUNT(dirs); i++)
	{
		const int3 hlp = tile + dirs[i];
		if(!map->isInTheMap(hlp)) 
			continue;

		const TerrainTile &hlpt = map->getTile(hlp);

		if((indeterminate(onLand)  ||  onLand == (hlpt.tertype!=8) ) 
			&& hlpt.tertype!=9) 
		{
			vec.push_back(hlp);
		}
	}
}

int CGameState::getMovementCost(const CGHeroInstance *h, const int3 &src, const int3 &dest, int remainingMovePoints, bool checkLast)
{
	if(src == dest) //same tile
		return 0;

	TerrainTile &s = map->terrain[src.x][src.y][src.z],
		&d = map->terrain[dest.x][dest.y][dest.z];

	//get basic cost
	int ret = h->getTileCost(d,s);

	if(src.x != dest.x  &&  src.y != dest.y) //it's diagonal move
	{
		int old = ret;
		ret *= 1.414213;
		//diagonal move costs too much but normal move is possible - allow diagonal move for remaining move points
		if(ret > remainingMovePoints  &&  remainingMovePoints >= old)
		{
			return remainingMovePoints;
		}
	}


	int left = remainingMovePoints-ret;
	if(checkLast  &&  left > 0  &&  remainingMovePoints-ret < 250) //it might be the last tile - if no further move possible we take all move points
	{
		std::vector<int3> vec;
		getNeighbours(d, dest, vec, s.tertype != TerrainTile::water);
		for(size_t i=0; i < vec.size(); i++)
		{
			int fcost = getMovementCost(h,dest,vec[i],left,false);
			if(fcost <= left)
			{
				return ret;
			}
		}
		ret = remainingMovePoints;
	}
	return ret;
}

int CGameState::canBuildStructure( const CGTownInstance *t, int ID )
{
	int ret = 7; //allowed by default
	//checking resources
	CBuilding * pom = VLC->buildh->buildings[t->subID][ID];
	
	if(!pom)return 8;
	if(pom->Name().size()==0||pom->resources.size()==0)return 2;//TODO: why does this happen?

	for(int res=0;res<pom->resources.size();res++) //TODO: support custom amount of resources
	{
		if(pom->resources[res] > getPlayer(t->tempOwner)->resources[res])
			ret = 6; //lack of res
	}

	//checking for requirements
	for( std::set<int>::iterator ri  =  VLC->townh->requirements[t->subID][ID].begin();
		ri != VLC->townh->requirements[t->subID][ID].end();
		ri++ )
	{
		if(t->builtBuildings.find(*ri)==t->builtBuildings.end())
			ret = 8; //lack of requirements - cannot build
	}

	//can we build it?
	if(t->forbiddenBuildings.find(ID)!=t->forbiddenBuildings.end())
		ret = 2; //forbidden
	else if(t->builded >= MAX_BUILDING_PER_TURN)
		ret = 5; //building limit

	if(ID == 13) //capitol
	{
		for(unsigned int in = 0; in < map->towns.size(); in++)
		{
			if(map->towns[in]->tempOwner==t->tempOwner  &&  vstd::contains(map->towns[in]->builtBuildings,13))
			{
				ret = 0; //no more than one capitol
				break;
			}
		}
	}
	else if(ID == 6) //shipyard
	{
		int3 t1(t->pos + int3(-1,3,0)),
			t2(t->pos + int3(-3,3,0));
		if(map->isInTheMap(t1) && map->getTile(t1).tertype != TerrainTile::water  
			&&  (map->isInTheMap(t2) && map->getTile(t2).tertype != TerrainTile::water))
			ret = 1; //lack of water
	}

	if(t->builtBuildings.find(ID)!=t->builtBuildings.end())	//already built
		ret = 4;
	return ret;
}

void CGameState::apply(CPack *pack)
{
	ui16 typ = typeList.getTypeID(pack);
	assert(typ >= 0);
	applierGs->apps[typ]->applyOnGS(this,pack);
}

PlayerState * CGameState::getPlayer( ui8 color )
{
	if(vstd::contains(players,color))
	{
		return &players[color];
	}
	else 
	{
		tlog2 << "Warning: Cannot find info for player " << int(color) << std::endl;
		return NULL;
	}
}

bool CGameState::getPath(int3 src, int3 dest, const CGHeroInstance * hero, CPath &ret)
{
	if(!map->isInTheMap(src) || !map->isInTheMap(dest)) //check input
		return false;

	int3 hpos = hero->getPosition(false);
	tribool blockLandSea; //true - blocks sea, false - blocks land, indeterminate - allows all

	if (!hero->canWalkOnSea())
		blockLandSea = (map->getTile(hpos).tertype != TerrainTile::water); //block land if hero is on water and vice versa
	else
		blockLandSea = boost::logic::indeterminate;

	const std::vector<std::vector<std::vector<ui8> > > &FoW = getPlayer(hero->tempOwner)->fogOfWarMap;

	//graph initialization
	std::vector< std::vector<CPathNode> > graph;
	graph.resize(map->width);
	for(size_t i=0; i<graph.size(); ++i)
	{
		graph[i].resize(map->height);
		for(size_t j=0; j<graph[i].size(); ++j)
		{
			const TerrainTile *tinfo = &map->terrain[i][j][src.z];
			CPathNode &node = graph[i][j];

			node.accesible = !tinfo->blocked;
			node.dist = -1;
			node.theNodeBefore = NULL;
			node.visited = false;
			node.coord.x = i;
			node.coord.y = j;
			node.coord.z = dest.z;

			if ((tinfo->tertype == TerrainTile::rock) //it's rock
				|| ((blockLandSea) && (tinfo->tertype == TerrainTile::water)) //it's sea and we cannot walk on sea
				|| ((!blockLandSea) && (tinfo->tertype != TerrainTile::water)) //it's land and we cannot walk on land
				|| !FoW[i][j][src.z] //tile is covered by the FoW
			)
			{
				node.accesible = false;
			}
		}
	}


	//Special rules for the destination tile
	{
		const TerrainTile *t = &map->terrain[dest.x][dest.y][dest.z];
		CPathNode &d = graph[dest.x][dest.y];

		//tile may be blocked by blockvis / normal vis obj but it still must be accessible
		if(t->visitable) 
		{
			d.accesible = true; //for allowing visiting objects
		}

		if(blockLandSea && t->tertype == TerrainTile::water) //hero can walk only on land and dst lays on the water
		{
			size_t i = 0;
			for(; i < t->visitableObjects.size(); i++)
				if(t->visitableObjects[i]->ID == 8  ||  t->visitableObjects[i]->ID == HEROI_TYPE) //it's a Boat
					break;

			d.accesible = (i < t->visitableObjects.size()); //dest is accessible only if there is boat/hero
		}
		else if(!blockLandSea && t->tertype != TerrainTile::water) //hero is moving by water
		{
			d.accesible = (t->siodmyTajemniczyBajt & 64) && !t->blocked; //tile is accessible if it's coastal and not blocked
		}
	}


	//graph initialized

	//initial tile - set cost on 0 and add to the queue
	graph[src.x][src.y].dist = 0; 
	std::queue<CPathNode> mq;
	mq.push(graph[src.x][src.y]);

	ui32 curDist = 0xffffffff; //total cost of path - init with max possible val

	std::vector<int3> neighbours;
	neighbours.reserve(8);

	while(!mq.empty())
	{
		CPathNode &cp = graph[mq.front().coord.x][mq.front().coord.y];
		mq.pop();
		if (cp.coord == dest) //it's destination tile
		{
			if (cp.dist < curDist) //that path is better than previous one
				curDist = cp.dist;
			continue;
		}
		else
		{
			if (cp.dist > curDist) //it's not dest and current length is greater than cost of already found path 
				continue;
		}

		//add accessible neighbouring nodes to the queue
		getNeighbours(map->getTile(cp.coord), cp.coord, neighbours, boost::logic::indeterminate);
		for(unsigned int i=0; i < neighbours.size(); i++)
		{
			CPathNode & dp = graph[neighbours[i].x][neighbours[i].y];
			if(dp.accesible)
			{
				int cost = getMovementCost(hero,cp.coord,dp.coord,hero->movement - cp.dist);
				if((dp.dist==-1 || (dp.dist > cp.dist + cost)) && dp.accesible && checkForVisitableDir(cp.coord, dp.coord) && checkForVisitableDir(dp.coord, cp.coord))
				{
					dp.dist = cp.dist + cost;
					dp.theNodeBefore = &cp;
					mq.push(dp);
				}
			}
		}
	}

	CPathNode *curNode = &graph[dest.x][dest.y];
	if(!curNode->theNodeBefore) //destination is not accessible
		return false;


	//fill ret with found path
	ret.nodes.clear();
	while(curNode->coord != graph[src.x][src.y].coord)
	{
		ret.nodes.push_back(*curNode);
		curNode = curNode->theNodeBefore;
	}
	ret.nodes.push_back(graph[src.x][src.y]);

	return true;
}

void CGameState::calculatePaths(const CGHeroInstance *hero, CPathsInfo &out, int3 src, int movement)
{
	assert(hero);
	if(src.x < 0)
		src = hero->getPosition(false);
	if(movement < 0)
		movement = hero->movement;

	out.hero = hero;
	out.hpos = src;

	if(!map->isInTheMap(src)/* || !map->isInTheMap(dest)*/) //check input
	{
		tlog1 << "CGameState::calculatePaths: Hero outside the map? How dare you...\n";
		return;
	}

	tribool onLand; //true - blocks sea, false - blocks land, indeterminate - allows all

	if (!hero->canWalkOnSea())
		onLand = (map->getTile(src).tertype != TerrainTile::water); //block land if hero is on water and vice versa
	else
		onLand = boost::logic::indeterminate;

	const std::vector<std::vector<std::vector<ui8> > > &FoW = getPlayer(hero->tempOwner)->fogOfWarMap;

	//graph initialization
	CGPathNode ***graph = out.nodes;
	for(size_t i=0; i < out.sizes.x; ++i)
	{
		for(size_t j=0; j < out.sizes.y; ++j)
		{
			for(size_t k=0; k < out.sizes.z; ++k)
			{
				const TerrainTile *tinfo = &map->terrain[i][j][k];
				CGPathNode &node = graph[i][j][k];

				node.accessible = (tinfo->blocked ? CGPathNode::BLOCKED : CGPathNode::ACCESSIBLE);
				node.turns = 0xff;
				node.moveRemains = 0;
				node.coord.x = i;
				node.coord.y = j;
				node.coord.z = k;
				node.land = tinfo->tertype != TerrainTile::water;
				node.theNodeBefore = NULL;

				if ( tinfo->tertype == TerrainTile::rock//it's rock
					|| onLand  && !node.land		//it's sea and we cannot walk on sea
					|| !onLand && node.land		//it's land and we cannot walk on land
					|| !FoW[i][j][k]					//tile is covered by the FoW
				)
				{
					node.accessible = CGPathNode::BLOCKED;
				}
				else if(tinfo->visitable)
				{
					for(size_t ii = 0; ii < tinfo->visitableObjects.size(); ii++)
					{
						if(tinfo->visitableObjects[ii]->blockVisit)
						{
							node.accessible = CGPathNode::BLOCKVIS;
							break;
						}
						else
							node.accessible = CGPathNode::VISITABLE;
					}
				}

				if(onLand && !node.land) //hero can walk only on land and tile lays on the water
				{
					size_t i = 0;
					for(; i < tinfo->visitableObjects.size(); i++)
						if(tinfo->visitableObjects[i]->ID == 8  ||  tinfo->visitableObjects[i]->ID == HEROI_TYPE) //it's a Boat
							break;
					if(i < tinfo->visitableObjects.size())
						node.accessible = CGPathNode::BLOCKVIS; //dest is accessible only if there is boat/hero
				}
				else if(!onLand && tinfo->tertype != TerrainTile::water) //hero is moving by water
				{
					if((tinfo->siodmyTajemniczyBajt & 64) && !tinfo->blocked)
						node.accessible = CGPathNode::VISITABLE; //tile is accessible if it's coastal and not blocked
				}
			}
		}
	}
	//graph initialized


	//initial tile - set cost on 0 and add to the queue
	graph[src.x][src.y][src.z].turns = 0; 
	graph[src.x][src.y][src.z].moveRemains = movement;
	std::queue<CGPathNode*> mq;
	mq.push(&graph[src.x][src.y][src.z]);

	ui32 curDist = 0xffffffff; //total cost of path - init with max possible val

	std::vector<int3> neighbours;
	neighbours.reserve(8);

	while(!mq.empty())
	{
		CGPathNode *cp = mq.front();
		mq.pop();

		const TerrainTile &ct = map->getTile(cp->coord);
		int movement = cp->moveRemains, turn = cp->turns;
		if(!movement)
		{
			movement = hero->maxMovePoints(ct.tertype != TerrainTile::water);
			turn++;
		}

		//add accessible neighbouring nodes to the queue
		getNeighbours(ct, cp->coord, neighbours, boost::logic::indeterminate);
		for(unsigned int i=0; i < neighbours.size(); i++)
		{
			const int3 &n = neighbours[i]; //current neighbor
			CGPathNode & dp = graph[n.x][n.y][n.z];
			if( !checkForVisitableDir(cp->coord, dp.coord) 
				|| !checkForVisitableDir(dp.coord, cp->coord)
				|| dp.accessible == CGPathNode::BLOCKED )
			{
				continue;
			}

			int cost = getMovementCost(hero, cp->coord, dp.coord, movement);
			int remains = movement - cost;

			if(remains < 0)
			{
				//occurs rarely, when hero with low movepoints tries to go leave the road
				turn++;
				movement = hero->maxMovePoints(ct.tertype != TerrainTile::water);
				cost = getMovementCost(hero, cp->coord, dp.coord, movement); //cost must be updated, movement points changed :(
				remains = movement - cost;
			}

			if(dp.turns==0xff		//we haven't been here before
				|| dp.turns > turn
				|| (dp.turns >= turn  &&  dp.moveRemains < remains)) //this route is faster
			{
				assert(&dp != cp->theNodeBefore); //two tiles can't point to each other
				dp.moveRemains = remains;
				dp.turns = turn;
				dp.theNodeBefore = cp;
				if(dp.accessible == CGPathNode::ACCESSIBLE)
				{
					mq.push(&dp);
				}
			}
		} //neighbours loop
	} //queue loop
}

bool CGameState::isVisible(int3 pos, int player)
{
	if(player == 255) //neutral player
		return false;
	return players[player].fogOfWarMap[pos.x][pos.y][pos.z];
}

bool CGameState::isVisible( const CGObjectInstance *obj, int player )
{
	if(player == 255) //neutral player
		return false;
	//object is visible when at least one blocked tile is visible
	for(int fx=0; fx<8; ++fx)
	{
		for(int fy=0; fy<6; ++fy)
		{
			int3 pos = obj->pos + int3(fx-7,fy-5,0);
			if(map->isInTheMap(pos) 
				&& !((obj->defInfo->blockMap[fy] >> (7 - fx)) & 1) 
				&& isVisible(pos, player)  )
				return true;
		}
	}
	return false;
}

bool CGameState::checkForVisitableDir(const int3 & src, const int3 & dst) const
{
	const TerrainTile * pom = &map->getTile(dst);
	return checkForVisitableDir(src, pom, dst);
}

bool CGameState::checkForVisitableDir( const int3 & src, const TerrainTile *pom, const int3 & dst ) const
{
	for(unsigned int b=0; b<pom->visitableObjects.size(); ++b) //checking destination tile
	{
		if(!vstd::contains(pom->blockingObjects, pom->visitableObjects[b])) //this visitable object is not blocking, ignore
			continue;

		CGDefInfo * di = pom->visitableObjects[b]->defInfo;
		if( (dst.x == src.x-1 && dst.y == src.y-1) && !(di->visitDir & (1<<4)) )
		{
			return false;
		}
		if( (dst.x == src.x && dst.y == src.y-1) && !(di->visitDir & (1<<5)) )
		{
			return false;
		}
		if( (dst.x == src.x+1 && dst.y == src.y-1) && !(di->visitDir & (1<<6)) )
		{
			return false;
		}
		if( (dst.x == src.x+1 && dst.y == src.y) && !(di->visitDir & (1<<7)) )
		{
			return false;
		}
		if( (dst.x == src.x+1 && dst.y == src.y+1) && !(di->visitDir & (1<<0)) )
		{
			return false;
		}
		if( (dst.x == src.x && dst.y == src.y+1) && !(di->visitDir & (1<<1)) )
		{
			return false;
		}
		if( (dst.x == src.x-1 && dst.y == src.y+1) && !(di->visitDir & (1<<2)) )
		{
			return false;
		}
		if( (dst.x == src.x-1 && dst.y == src.y) && !(di->visitDir & (1<<3)) )
		{
			return false;
		}
	}
	return true;
}
std::pair<ui32, ui32> BattleInfo::calculateDmgRange(const CStack* attacker, const CStack* defender, const CGHeroInstance * attackerHero, const CGHeroInstance * defendingHero, bool shooting, ui8 charge)
{
	float attackDefenseBonus,
		minDmg = attacker->creature->damageMin * attacker->amount, 
		maxDmg = attacker->creature->damageMax * attacker->amount;

	if(attacker->creature->idNumber == 149) //arrow turret
	{
		switch(attacker->position)
		{
		case -2: //keep
			minDmg = 15;
			maxDmg = 15;
			break;
		case -3: case -4: //turrets
			minDmg = 7.5f;
			maxDmg = 7.5f;
			break;
		}
	}

	if(attacker->hasFeatureOfType(StackFeature::SIEGE_WEAPON) && attacker->creature->idNumber != 149) //any siege weapon, but only ballista can attack (second condition - not arrow turret)
	{ //minDmg and maxDmg are multiplied by hero attack + 1
		minDmg *= attackerHero->getPrimSkillLevel(0) + 1; 
		maxDmg *= attackerHero->getPrimSkillLevel(0) + 1; 
	}

	if(attacker->hasFeatureOfType(StackFeature::GENERAL_ATTACK_REDUCTION))
	{
		attackDefenseBonus = attacker->Attack() * (attacker->valOfFeatures(StackFeature::GENERAL_ATTACK_REDUCTION, -1024) / 100.0f) - defender->Defense();
	}
	else
	{
		attackDefenseBonus = attacker->Attack() - defender->Defense();
	}

	//calculating total attack/defense skills modifier

	if(!shooting && attacker->hasFeatureOfType(StackFeature::ATTACK_BONUS, 0)) //bloodlust handling (etc.)
	{
		attackDefenseBonus += attacker->valOfFeatures(StackFeature::ATTACK_BONUS, 0);
	}

	if(shooting && attacker->hasFeatureOfType(StackFeature::ATTACK_BONUS, 1)) //precision handling (etc.)
	{
		attackDefenseBonus += attacker->valOfFeatures(StackFeature::ATTACK_BONUS, 1);
	}


	if(attacker->getEffect(55)) //slayer handling
	{
		std::vector<int> affectedIds;
		switch(attacker->getEffect(55)->level)
		{
		case 3: //expert
			{
				affectedIds.push_back(40); //giant
				affectedIds.push_back(41); //titan
				affectedIds.push_back(152); //lord of thunder
			} //continue adding ...
		case 2: //advanced
			{
				affectedIds.push_back(12); //angel
				affectedIds.push_back(13); //archangel
				affectedIds.push_back(54); //devil
				affectedIds.push_back(55); //arch devil
				affectedIds.push_back(150); //supreme archangel
				affectedIds.push_back(153); //antichrist
			} //continue adding ...
		case 0: case 1: //none and basic
			{
				affectedIds.push_back(26); //green dragon
				affectedIds.push_back(27); //gold dragon
				affectedIds.push_back(82); //red dragon
				affectedIds.push_back(83); //black dragon
				affectedIds.push_back(96); //behemot
				affectedIds.push_back(97); //ancient behemot
				affectedIds.push_back(110); //hydra
				affectedIds.push_back(111); //chaos hydra
				affectedIds.push_back(132); //azure dragon
				affectedIds.push_back(133); //crystal dragon
				affectedIds.push_back(134); //faerie dragon
				affectedIds.push_back(135); //rust dragon
				affectedIds.push_back(151); //diamond dragon
				affectedIds.push_back(154); //blood dragon
				affectedIds.push_back(155); //darkness dragon
				affectedIds.push_back(156); //ghost behemoth
				affectedIds.push_back(157); //hell hydra
				break;
			}
		}
		for(unsigned int g=0; g<affectedIds.size(); ++g)
		{
			if(defender->creature->idNumber == affectedIds[g])
			{
				attackDefenseBonus += VLC->spellh->spells[55].powers[attacker->getEffect(55)->level];
				break;
			}
		}
	}

	float dmgBonusMultiplier = 1.0f;

	//applying jousting bonus
	if( attacker->hasFeatureOfType(StackFeature::JOUSTING) && !defender->hasFeatureOfType(StackFeature::CHARGE_IMMUNITY) )
		dmgBonusMultiplier += charge * 0.05f;

	//bonus from attack/defense skills
	if(attackDefenseBonus < 0) //decreasing dmg
	{
		if(0.02f * (-attackDefenseBonus) > 0.3f)
		{
			dmgBonusMultiplier += -0.3f;
		}
		else
		{
			dmgBonusMultiplier += 0.02f * attackDefenseBonus;
		}
	}
	else //increasing dmg
	{
		if(0.05f * attackDefenseBonus > 4.0f)
		{
			dmgBonusMultiplier += 4.0f;
		}
		else
		{
			dmgBonusMultiplier += 0.05f * attackDefenseBonus;
		}
	}

	//handling secondary abilities and artifacts giving premies to them
	if(attackerHero)
	{
		if(shooting)
		{
			switch(attackerHero->getSecSkillLevel(1)) //archery
			{
			case 1: //basic
				dmgBonusMultiplier += 0.1f;
				break;
			case 2: //advanced
				dmgBonusMultiplier += 0.25f;
				break;
			case 3: //expert
				dmgBonusMultiplier += 0.5f;
				break;
			}

			if(attackerHero->getSecSkillLevel(1) > 0) //non-none level
			{
				//apply artifact premy to archery
				dmgBonusMultiplier += attackerHero->valOfBonuses(HeroBonus::SECONDARY_SKILL_PREMY, 1) / 100.0f;
			}
		}
		else
		{
			switch(attackerHero->getSecSkillLevel(22)) //offense
			{
			case 1: //basic
				dmgBonusMultiplier += 0.1f;
				break;
			case 2: //advanced
				dmgBonusMultiplier += 0.2f;
				break;
			case 3: //expert
				dmgBonusMultiplier += 0.3f;
				break;
			}
		}
	}
	if(defendingHero)
	{
		switch(defendingHero->getSecSkillLevel(23)) //armorer
		{
		case 1: //basic
			dmgBonusMultiplier *= 0.95f;
			break;
		case 2: //advanced
			dmgBonusMultiplier *= 0.9f;
			break;
		case 3: //expert
			dmgBonusMultiplier *= 0.85f;
			break;
		}
	}

	//handling hate effect
	if( attacker->hasFeatureOfType(StackFeature::HATE, defender->creature->idNumber) )
		dmgBonusMultiplier += 0.5f;

	//handling spell effects
	if(!shooting && defender->hasFeatureOfType(StackFeature::GENERAL_DAMAGE_REDUCTION, 0)) //eg. shield
	{
		dmgBonusMultiplier *= float(defender->valOfFeatures(StackFeature::GENERAL_DAMAGE_REDUCTION, 0)) / 100.0f;
	}
	else if(shooting && defender->hasFeatureOfType(StackFeature::GENERAL_DAMAGE_REDUCTION, 1)) //eg. air shield
	{
		dmgBonusMultiplier *= float(defender->valOfFeatures(StackFeature::GENERAL_DAMAGE_REDUCTION, 1)) / 100.0f;
	}
	if(attacker->getEffect(42)) //curse handling (partial, the rest is below)
	{
		dmgBonusMultiplier *= 0.8f * float(VLC->spellh->spells[42].powers[attacker->getEffect(42)->level]); //the second factor is 1 or 0
	}


	minDmg *= dmgBonusMultiplier;
	maxDmg *= dmgBonusMultiplier;

	if(attacker->getEffect(42)) //curse handling (rest)
	{
		minDmg -= VLC->spellh->spells[42].powers[attacker->getEffect(42)->level];
		return std::make_pair(int(minDmg), int(minDmg));
	}
	else if(attacker->getEffect(41)) //bless handling
	{
		maxDmg += VLC->spellh->spells[41].powers[attacker->getEffect(41)->level];
		return std::make_pair(int(maxDmg), int(maxDmg));
	}
	else
	{
		return std::make_pair(int(minDmg), int(maxDmg));
	}

	tlog1 << "We are too far in calculateDmg...\n";
	return std::make_pair(0, 0);
}

ui32 BattleInfo::calculateDmg(const CStack* attacker, const CStack* defender, const CGHeroInstance * attackerHero, const CGHeroInstance * defendingHero, bool shooting, ui8 charge)
{
	std::pair<ui32, ui32> range = calculateDmgRange(attacker, defender, attackerHero, defendingHero, shooting, charge);

	if(range.first != range.second)
		return range.first  +  rand() % (range.second - range.first + 1);
	else
		return range.first;
}

void BattleInfo::calculateCasualties( std::map<ui32,si32> *casualties ) const
{
	for(unsigned int i=0; i<stacks.size();i++)//setting casualties
	{
		const CStack * const st = stacks[i];
		si32 killed = (st->alive() ? st->baseAmount - st->amount : st->baseAmount);
		amax(killed, 0);
		casualties[!st->attackerOwned][st->creature->idNumber] += killed;
	}
}

si8 CGameState::battleMaxSpellLevel()
{
	if(!curB) //there is not battle
	{
		tlog1 << "si8 CGameState::maxSpellLevel() call when there is no battle!" << std::endl;
		throw "si8 CGameState::maxSpellLevel() call when there is no battle!";
	}

	si8 levelLimit = SPELL_LEVELS;

	const CGHeroInstance *h1 =  getHero(curB->hero1); 
	if(h1)
	{
		for(std::list<HeroBonus>::const_iterator i = h1->bonuses.begin(); i != h1->bonuses.end(); i++)
			if(i->type == HeroBonus::BLOCK_SPELLS_ABOVE_LEVEL)
				amin(levelLimit, i->val);
	}

	const CGHeroInstance *h2 = getHero(curB->hero2); 
	if(h2)
	{
		for(std::list<HeroBonus>::const_iterator i = h2->bonuses.begin(); i != h2->bonuses.end(); i++)
			if(i->type == HeroBonus::BLOCK_SPELLS_ABOVE_LEVEL)
				amin(levelLimit, i->val);
	}

	return levelLimit;
}

std::set<CStack*> BattleInfo::getAttackedCreatures(const CSpell * s, const CGHeroInstance * caster, int destinationTile)
{
	std::set<ui16> attackedHexes = s->rangeInHexes(destinationTile, caster->getSpellSchoolLevel(s));
	std::set<CStack*> attackedCres; /*std::set to exclude multiple occurrences of two hex creatures*/

	bool onlyAlive = s->id != 38 && s->id != 39; //when casting resurrection or animate dead we should be allow to select dead stack

	if(s->id == 24 || s->id == 25 || s->id == 26) //death ripple, destroy undead and Armageddon
	{
		for(int it=0; it<stacks.size(); ++it)
		{
			if((s->id == 24 && !stacks[it]->creature->isUndead()) //death ripple
				|| (s->id == 25 && stacks[it]->creature->isUndead()) //destroy undead
				|| (s->id == 26) //Armageddon
				)
			{
				if(stacks[it]->alive())
					attackedCres.insert(stacks[it]);
			}
		}
	}
	else if(VLC->spellh->spells[s->id].attributes.find("CREATURE_TARGET_1") != std::string::npos
		|| VLC->spellh->spells[s->id].attributes.find("CREATURE_TARGET_2") != std::string::npos) //spell to be cast on a specific creature but massive on expert
	{
		if(caster->getSpellSchoolLevel(s) < 3)  /*not expert */
		{
			CStack * st = getStackT(destinationTile, onlyAlive);
			if(st)
				attackedCres.insert(st);
		}
		else
		{
			for(int it=0; it<stacks.size(); ++it)
			{
				/*if it's non negative spell and our unit or non positive spell and hostile unit */
				if((VLC->spellh->spells[s->id].positiveness >= 0 && stacks[it]->owner == caster->tempOwner)
					||(VLC->spellh->spells[s->id].positiveness <= 0 && stacks[it]->owner != caster->tempOwner )
					)
				{
					if(!onlyAlive || stacks[it]->alive())
						attackedCres.insert(stacks[it]);
				}
			}
		} //if(caster->getSpellSchoolLevel(s) < 3)
	}
	else if(VLC->spellh->spells[s->id].attributes.find("CREATURE_TARGET") != std::string::npos) //spell to be cast on one specific creature
	{
		CStack * st = getStackT(destinationTile, onlyAlive);
		if(st)
			attackedCres.insert(st);
	}
	else //custom range from attackedHexes
	{
		for(std::set<ui16>::iterator it = attackedHexes.begin(); it != attackedHexes.end(); ++it)
		{
			CStack * st = getStackT(*it, onlyAlive);
			if(st)
				attackedCres.insert(st);
		}
	}
	return attackedCres;
}

int BattleInfo::calculateSpellDuration(const CSpell * spell, const CGHeroInstance * caster)
{
	switch(spell->id)
	{
	case 56: //frenzy
		return 1;
	default: //other spells
		return caster->getPrimSkillLevel(2) + caster->valOfBonuses(HeroBonus::SPELL_DURATION);
	}
}

CStack * BattleInfo::generateNewStack(const CGHeroInstance * owner, int creatureID, int amount, int stackID, bool attackerOwned, int slot, int /*TerrainTile::EterrainType*/ terrain, int position) const
{
	CStack * ret = new CStack(&VLC->creh->creatures[creatureID], amount, attackerOwned ? side1 : side2, stackID, attackerOwned, slot);
	if(owner)
	{
		ret->features.push_back(makeFeature(StackFeature::SPEED_BONUS, StackFeature::WHOLE_BATTLE, 0, owner->valOfBonuses(HeroBonus::STACKS_SPEED), StackFeature::BONUS_FROM_HERO));
		//base luck/morale calculations
		ret->morale = owner->getCurrentMorale(slot, false);
		ret->luck = owner->getCurrentLuck(slot, false);
		//other bonuses
		ret->features.push_back(makeFeature(StackFeature::ATTACK_BONUS, StackFeature::WHOLE_BATTLE, 0, owner->getPrimSkillLevel(0), StackFeature::BONUS_FROM_HERO));
		ret->features.push_back(makeFeature(StackFeature::DEFENCE_BONUS, StackFeature::WHOLE_BATTLE, 0, owner->getPrimSkillLevel(1), StackFeature::BONUS_FROM_HERO));

		if ( owner->hasBonusOfType(HeroBonus::STACK_HEALTH_PERCENT) ) // e.g. Elixir of Life
			ret->features.push_back(makeFeature(StackFeature::HP_BONUS, StackFeature::WHOLE_BATTLE, 0, 
				(ret->creature->hitPoints * owner->valOfBonuses(HeroBonus::STACK_HEALTH_PERCENT)) / 100, 
				StackFeature::BONUS_FROM_HERO));
		if (owner->hasBonusOfType(HeroBonus::HP_REGENERATION)) // e.g. Elixir of Life
			ret->features.push_back(makeFeature(StackFeature::HP_REGENERATION, StackFeature::WHOLE_BATTLE, 0,
				owner->valOfBonuses(HeroBonus::HP_REGENERATION), StackFeature::BONUS_FROM_HERO));

		if (owner->hasBonusOfType(HeroBonus::LEVEL_SPELL_IMMUNITY)) // e.g. Power of the Dragon Father
			ret->features.push_back(makeFeature(StackFeature::LEVEL_SPELL_IMMUNITY, StackFeature::WHOLE_BATTLE, 0,
				owner->valOfBonuses(HeroBonus::LEVEL_SPELL_IMMUNITY), StackFeature::BONUS_FROM_HERO));

		ret->features.push_back(makeFeature(StackFeature::HP_BONUS, StackFeature::WHOLE_BATTLE, 0, owner->valOfBonuses(HeroBonus::STACK_HEALTH), StackFeature::BONUS_FROM_HERO));
		ret->firstHPleft = ret->MaxHealth();
	}
	else
	{
		ret->morale = 0;
		ret->luck = 0;
	}

	//native terrain bonuses
	int faction = ret->creature->faction;
	if(faction >= 0 && VLC->heroh->nativeTerrains[faction] == terrain)
	{
		ret->features.push_back(makeFeature(StackFeature::SPEED_BONUS, StackFeature::WHOLE_BATTLE, 0, 1, StackFeature::OTHER_SOURCE));
		ret->features.push_back(makeFeature(StackFeature::ATTACK_BONUS, StackFeature::WHOLE_BATTLE, 0, 1, StackFeature::OTHER_SOURCE));
		ret->features.push_back(makeFeature(StackFeature::DEFENCE_BONUS, StackFeature::WHOLE_BATTLE, 0, 1, StackFeature::OTHER_SOURCE));
	}

	ret->position = position;

	return ret;
}

ui32 BattleInfo::getSpellCost(const CSpell * sp, const CGHeroInstance * caster) const
{
	ui32 ret = VLC->spellh->spells[sp->id].costs[caster->getSpellSchoolLevel(sp)];

	//checking for friendly stacks reducing cost of the spell
	si32 manaReduction = 0;
	for(int g=0; g<stacks.size(); ++g)
	{
		if( stacks[g]->owner == caster->tempOwner && stacks[g]->hasFeatureOfType(StackFeature::CHANGES_SPELL_COST_FOR_ALLY) )
		{
			amin(manaReduction, stacks[g]->valOfFeatures(StackFeature::CHANGES_SPELL_COST_FOR_ALLY));
		}
	}

	return ret + manaReduction;
}

int BattleInfo::hexToWallPart(int hex) const
{
	if(siege == 0) //there is no battle!
		return -1;

	static const std::pair<int, int> attackable[] = //potentially attackable parts of wall
	{std::make_pair(50, 0), std::make_pair(183, 1), std::make_pair(182, 2), std::make_pair(130, 3),
	std::make_pair(62, 4), std::make_pair(29, 5), std::make_pair(12, 6), std::make_pair(95, 7), std::make_pair(96, 7)};

	for(int g = 0; g < ARRAY_COUNT(attackable); ++g)
	{
		if(attackable[g].first == hex)
			return attackable[g].second;
	}

	return -1; //not found!
}

std::pair<const CStack *, int> BattleInfo::getNearestStack(const CStack * closest, boost::logic::tribool attackerOwned) const
{	
	bool ac[BFIELD_SIZE];
	std::set<int> occupyable;

	getAccessibilityMap(ac, closest->hasFeatureOfType(StackFeature::DOUBLE_WIDE), closest->attackerOwned, false, occupyable, closest->hasFeatureOfType(StackFeature::FLYING), closest->ID);

	int predecessor[BFIELD_SIZE], dist[BFIELD_SIZE];
	makeBFS(closest->position, ac, predecessor, dist, closest->hasFeatureOfType(StackFeature::DOUBLE_WIDE), closest->attackerOwned, closest->hasFeatureOfType(StackFeature::FLYING), true);

	std::vector< std::pair< std::pair<int, int>, const CStack *> > stackPairs; //pairs <<distance, hex>, stack>
	for(int g=0; g<BFIELD_SIZE; ++g)
	{
		const CStack * atG = getStackT(g);
		if(!atG || atG->ID == closest->ID) //if there is not stack or we are the closest one
			continue;
		if(boost::logic::indeterminate(attackerOwned) || atG->attackerOwned == attackerOwned)
		{
			if(predecessor[g] == -1) //TODO: is it really the best solution?
				continue;
			stackPairs.push_back( std::make_pair( std::make_pair(dist[predecessor[g]], g), atG) );
		}
	}

	if(stackPairs.size() > 0)
	{
		std::vector< std::pair< std::pair<int, int>, const CStack *> > minimalPairs;
		minimalPairs.push_back(stackPairs[0]);
	
		for(int b=1; b<stackPairs.size(); ++b)
		{
			if(stackPairs[b].first.first < minimalPairs[0].first.first)
			{
				minimalPairs.clear();
				minimalPairs.push_back(stackPairs[b]);
			}
			else if(stackPairs[b].first.first == minimalPairs[0].first.first)
			{
				minimalPairs.push_back(stackPairs[b]);
			}
		}

		std::pair< std::pair<int, int>, const CStack *> minPair = minimalPairs[minimalPairs.size()/2];

		return std::make_pair(minPair.second, predecessor[minPair.first.second]);
	}

	return std::make_pair<const CStack * , int>(NULL, -1);
}

bool CGameState::battleCanShoot(int ID, int dest)
{
	if(!curB)
		return false;

	const CStack *our = curB->getStack(ID),
		*dst = curB->getStackT(dest);

	if(!our || !dst) return false;

	const CGHeroInstance * ourHero = battleGetOwner(our->ID);

	if(our->hasFeatureOfType(StackFeature::FORGETFULL)) //forgetfulness
		return false;

	if(our->hasFeatureOfType(StackFeature::SHOOTER)//it's shooter
		&& our->owner != dst->owner
		&& dst->alive()
		&& (!curB->isStackBlocked(ID) || 
			( ourHero && ourHero->hasBonusOfType(HeroBonus::FREE_SHOOTING) ) )
		&& our->shots
		)
		return true;
	return false;
}

const CStack * BattleInfo::getNextStack() const
{
	std::vector<const CStack *> hlp;
	getStackQueue(hlp, 1, -1);

	if(hlp.size())
		return hlp[0];
	else
		return NULL;
}

static const CStack *takeStack(std::vector<const CStack *> &st, int &curside, int turn)
{
	const CStack *ret = NULL;
	unsigned i, //fastest stack
		j; //fastest stack of the other side
	for(i = 0; i < st.size(); i++)
		if(st[i])
			break;

	//no stacks left
	if(i == st.size())
		return NULL;

	const CStack *fastest = st[i], *other = NULL;
	int bestSpeed = fastest->Speed(turn);

	if(fastest->attackerOwned != curside)
	{
		ret = fastest;
	}
	else
	{
		for(j = i + 1; j < st.size(); j++)
		{
			if(!st[j]) continue;
			if(st[j]->attackerOwned != curside || st[j]->Speed(turn) != bestSpeed)
				break;
		}

		if(j >= st.size())
		{
			ret = fastest;
		}
		else
		{
			other = st[j];
			if(other->Speed(turn) != bestSpeed)
				ret = fastest;
			else
				ret = other;
		}
	}

	assert(ret);
	if(ret == fastest)
		st[i] = NULL;
	else
		st[j] = NULL;

	curside = ret->attackerOwned;
	return ret;
}

void BattleInfo::getStackQueue( std::vector<const CStack *> &out, int howMany, int turn /*= 0*/, int lastMoved /*= -1*/ ) const
{
	//we'll split creatures with remaining movement to 4 parts
	std::vector<const CStack *> phase[4]; //0 - turrets/catapult, 1 - normal (unmoved) creatures, other war machines, 2 - waited cres that had morale, 3 - rest of waited cres
	int toMove = 0; //how many stacks still has move
	const CStack *active = getStack(activeStack);

	//active stack hasn't taken any action yet - must be placed at the beginning of queue, no matter what
	if(!turn && active && active->willMove() && !vstd::contains(active->state, WAITING))
	{
		out.push_back(active);
		if(out.size() == howMany)
			return;
	}


	for(unsigned int i=0; i<stacks.size(); ++i)
	{
		const CStack * const s = stacks[i];
		if(turn <= 0 && !s->willMove() //we are considering current round and stack won't move
			|| turn > 0 && !s->canMove(turn) //stack won't be able to move in later rounds
			|| turn <= 0 && s == active) //it's active stack already added at the beginning of queue
		{
			continue;
		}

		int p = -1; //in which phase this tack will move?
		if(turn <= 0 && vstd::contains(s->state, WAITING)) //consider waiting state only for ongoing round
		{
			if(vstd::contains(s->state, HAD_MORALE))
				p = 2;
			else
				p = 3;
		}
		else if(s->creature->idNumber == 145  ||  s->creature->idNumber == 149) //catapult and turrets are first
		{
			p = 0;
		}
		else
		{
			p = 1;
		}

		phase[p].push_back(s);
		toMove++;
	}

	for(int i = 0; i < 4; i++)
		std::sort(phase[i].begin(), phase[i].end(), CMP_stack(i, turn > 0 ? turn : 0));

	for(size_t i = 0; i < phase[0].size() && i < howMany; i++)
		out.push_back(phase[0][i]);

	if(out.size() == howMany)
		return;

	if(lastMoved == -1)
	{
		if(active)
		{
			if(out.size() && out.front() == active)
				lastMoved = active->attackerOwned;
			else
				lastMoved = active->attackerOwned;
		}
		else
		{
			lastMoved = 0;
		}
	}

	int pi = 1;
	while(out.size() < howMany)
	{
		const CStack *hlp = takeStack(phase[pi], lastMoved, turn);
		if(!hlp)
		{
			pi++;
			if(pi > 3)
			{
				//if(turn != 2)
					getStackQueue(out, howMany, turn + 1, lastMoved);
				return;
			}
		}
		else
		{
			out.push_back(hlp);
		}
	}
}

int3 CPath::startPos() const
{
	return nodes[nodes.size()-1].coord;
}
void CPath::convert(ui8 mode) //mode=0 -> from 'manifest' to 'object'
{
	if (mode==0)
	{
		for (unsigned int i=0;i<nodes.size();i++)
		{
			nodes[i].coord = CGHeroInstance::convertPosition(nodes[i].coord,true);
		}
	}
}

int3 CPath::endPos() const
{
	return nodes[0].coord;
}

CGPathNode::CGPathNode()
:coord(-1,-1,-1)
{
	accessible = 0;
	land = 0;
	moveRemains = 0;
	turns = 255;
	theNodeBefore = NULL;
}

bool CPathsInfo::getPath( const int3 &dst, CGPath &out )
{
	out.nodes.clear();
	const CGPathNode *curnode = &nodes[dst.x][dst.y][dst.z];
	if(!curnode->theNodeBefore)
		return false;

	while(curnode)
	{
		out.nodes.push_back(*curnode);
		curnode = curnode->theNodeBefore;
	}
	return true;
}

CPathsInfo::CPathsInfo( const int3 &Sizes )
:sizes(Sizes)
{
	nodes = new CGPathNode**[sizes.x];
	for(int i = 0; i < sizes.x; i++)
	{
		nodes[i] = new CGPathNode*[sizes.y];
		for (int j = 0; j < sizes.y; j++)
		{
			nodes[i][j] = new CGPathNode[sizes.z];
		}
	}
}

CPathsInfo::~CPathsInfo()
{
	for(int i = 0; i < sizes.x; i++)
	{
		for (int j = 0; j < sizes.y; j++)
		{
			delete [] nodes[i][j];
		}
		delete [] nodes[i];
	}
	delete [] nodes;
}

int3 CGPath::startPos() const
{
	return nodes[nodes.size()-1].coord;
}

int3 CGPath::endPos() const
{
	return nodes[0].coord;
}

void CGPath::convert( ui8 mode )
{
	if(mode==0)
	{
		for(unsigned int i=0;i<nodes.size();i++)
		{
			nodes[i].coord = CGHeroInstance::convertPosition(nodes[i].coord,true);
		}
	}
}

bool CMP_stack::operator()( const CStack* a, const CStack* b )
{
	switch(phase)
	{
	case 0: //catapult moves after turrets
		return a->creature->idNumber < b->creature->idNumber; //catapult is 145 and turrets are 149
		//TODO? turrets order
	case 1: //fastest first, upper slot first
		{
			int as = a->Speed(turn), bs = b->Speed(turn);
			if(as != bs)
				return as > bs;
			else
				return a->slot < b->slot;
		}
	case 2: //fastest last, upper slot first
		//TODO: should be replaced with order of receiving morale!
	case 3: //fastest last, upper slot first
		{
			int as = a->Speed(turn), bs = b->Speed(turn);
			if(as != bs)
				return as < bs;
			else
				return a->slot < b->slot;
		}
	default:
		assert(0);
		return false;
	}

}

CMP_stack::CMP_stack( int Phase /*= 1*/, int Turn )
{
	phase = Phase;
	turn = Turn;
}

PlayerState::PlayerState() 
 : color(-1), currentSelection(0xffffffff)
{

}