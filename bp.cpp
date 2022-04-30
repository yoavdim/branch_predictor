#include "bp_api.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
using namespace std;


//// defines for FSM table
#define SNT 0 // 00
#define WNT 1 // 01
#define WT 2 // 10
#define ST 3 // 11

// jump as the array type size
#define MEMSET(arr, val, num) for(int temp_i=0; temp_i<num; arr[temp_i]=(val), temp_i++)

/** 
* @brief define the BTB data structure
*/
typedef struct BTB_data {
	public:
	unsigned history_LH;
	unsigned tag;
	int* FSM_poiner; // for local history only, an array
	uint32_t jumpLabeL; // target
	bool valid;
	//bool isGlobalHist;
	//bool isGlobalTable;
	//int Shared;
}BTB_data;


/** 
* @brief define the BTB envelope
*/
class BTB_Struct {
	public:
	unsigned tagNumBits;
	unsigned historySizeBits;
	unsigned BTB_sizeLines;
	int FsmDefault;
	BTB_data* BTB_pointer; // array
	int num_of_updates;
	int num_of_fails;
	// SIM_stats* status;
	//bool isGlobalHist_;
	//bool isGlobalTable_;
	//int Shared_;

 	//Initialize the information in BTB && reset the data
	BTB_Struct(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState) {
		BTB_pointer= new BTB_data[btbSize];//each BTB_data[i] is a line in the BTB table 
		BTB_sizeLines=btbSize;
		tagNumBits=tagNumBits;
		FsmDefault=fsmState;
		historySizeBits=historySize;
		num_of_updates=0;
		num_of_fails=0;

		for (int i =0 ; i < BTB_sizeLines; i++) { 
			BTB_pointer[i].history_LH=0;
			BTB_pointer[i].tag=0;
			BTB_pointer[i].valid=0;
			BTB_pointer[i].jumpLabeL=0;
			BTB_pointer[i].FSM_poiner=NULL;
		}
	}

 	~BTB_Struct() {
		 //need to complete 
 	}


	// clear the previews state before the update of a different tag
	BTB_data *reset_entry(uint32_t pc) { 
		unsigned tag;
		int index;
		BTB_data* entry;

		get_tag_n_index(pc, tag, index);
		assert(index < BTB_sizeLines);
		entry = BTB_pointer + index;

		entry->tag = tag;
		entry->history_LH = 0;
		entry->jumpLabeL = 0;
		
		if (! entry->valid)
			entry->FSM_poiner = local_fsm_array_create();
		else if(entry->FSM_poiner) {
			MEMSET(entry->FSM_poiner, FsmDefault, 1<<historySizeBits);
		}
		entry->valid = 1;
		
		return entry;
	}

	BTB_data* get_entry(uint32_t pc) {
		unsigned tag;
		int index;
		BTB_data* entry;

		get_tag_n_index(pc, tag, index);
		assert(index < BTB_sizeLines);
		entry = BTB_pointer + index;

		if (entry->valid && entry->tag == tag) 
			return entry;
		return nullptr;
	}
	
	//implement the sub-functions
	bool BP_predict_My(uint32_t pc, uint32_t* dst) {
		BTB_data *entry = get_entry(pc);  // in the BTB
		if (!entry) { *dst = pc+4; return 0; } // predict not taken / not branch

		unsigned history = get_history(entry);
		int *state_array = get_state_array(entry);
		int state = state_array[run_share(history, pc)];

		if (state >= 2) {
			*dst = entry->jumpLabeL;
			return 1;
		} else {
			*dst = pc+4;
			return 0;
		}
	}

	void BP_update_My(uint32_t pc, uint32_t targetPc, bool taken, uint32_t pred_dst) {
		BTB_data *entry = get_entry(pc); // notice, btb entry could have been evicted since the fetch stage
		if (!entry) 
			entry = reset_entry(pc);
		unsigned &history = get_history(entry); // a reference
		int *state_array = get_state_array(entry);
		int &state = state_array[run_share(history, pc)];

		num_of_updates++;
		if (pred_dst != (taken ? targetPc : pc+4))
			num_of_fails++;

		// FSM
		if (taken && state != ST)  
			state++;
		else if ((!taken) && state != SNT)
			state--;

		// history
		history = (history<<1) & ((1<<historySizeBits)-1) | (taken?1:0); // shift left, mask only the relevant length to remove oldest event, add the recent event 

		// target
		entry->jumpLabeL = targetPc;
	}



	void get_tag_n_index(uint32_t pc, unsigned &tag, int &index){...} // does not check existance, only translating the bits TODO implement (pc=pc>>2; )
	protected:
	// use in prediction, not update
	virtual unsigned& get_history(BTB_data *entry); // return GlobalHistory  or entry->history_LH	
	virtual int* get_state_array(BTB_data *entry);  // return GlobalFSMTable or entry->FSM_poiner
	virtual int* local_fsm_array_create();          // return new int[1<<historySizeBits](FsmDefault) or nullptr if using GlobalFSMTable
	virtual int  run_share(unsigned history, uint32_t pc); // if lshare or gshare, xor history with some pc bits TODO understand which bits (different for l&g)
}


//************part2***********************

//create&define Local History and Local Table
class LH_LT : public BTB_Struct {
public:
	// unsigned FSM_num; - remove line?
	LH_LT(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState)
		:BTB_Struct(btbSize, historySize, tagSize, fsmState){
			unsigned FSM_num=pow(2,historySize);
			for (int i = 0; i < btbSize; i++) {
				LH_LT::BTB_pointer[i].FSM_poiner = new int[FSM_num];//define local state for each line in the BTB,not sure if its work!
				for (int j = 0; j < FSM_num ; j++) { // reset the FSM table
					LH_LT::BTB_pointer[i].FSM_poiner[j] = fsmState;
				}
			}
	}

	unsigned& get_history(BTB_data *entry) override {
		return entry->history_LH;
	} 
	int* get_state_array(BTB_data *entry) override {
		return entry->FSM_poiner;
	}
	int* local_fsm_array_create() override {
		int* ans = new int[1<<historySizeBits];
		MEMSET(ans, FsmDefault, 1<<historySizeBits);
		return ans;
	}
}

//create&define Local History and Global Table
class LH_GT : public BTB_Struct {
	public:
	int* GlobalFSMTable;
	bool is_share;

	LH_GT(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState, bool is_share)
		:BTB_Struct(btbSize, historySize, tagSize, fsmState) {
			GlobalFSMTable = new int[1<<historySizeBits];
			MEMSET(GlobalFSMTable, FsmDefault, 1<<historySizeBits);
			LH_GT::is_share = is_share;
			// LH already initialized
	}

	unsigned& get_history(BTB_data *entry) override {
		return entry->history_LH;
	} 
	int* get_state_array(BTB_data *entry) override {
		return GlobalFSMTable;
	}
	int* local_fsm_array_create() override {
		return nullptr;
	}
}


//create&define Global History and Local Table
class GH_LT : public BTB_Struct {
	public:
	unsigned GlobalHistory=0;

	GH_LT(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState)
		:BTB_Struct(btbSize, historySize, tagSize, fsmState){
			unsigned FSM_num=pow(2,historySize);
			for (int i = 0; i < btbSize; i++) {
				GH_LT::BTB_pointer[i].FSM_poiner = new int[FSM_num];//define local state for each line in the BTB,not sure if its work!
				for (int j = 0; j < FSM_num ; j++) { // reset the FSM table
					GH_LT::BTB_pointer[i].FSM_poiner[j] = fsmState;
				}
			}
	}

	unsigned& get_history(BTB_data *entry) override {
		return GlobalHistory;
	} 
	int* get_state_array(BTB_data *entry) override {
		return entry->FSM_poiner;
	}
	int* local_fsm_array_create() override {
		int* ans = new int[1<<historySizeBits];
		MEMSET(ans, FsmDefault, 1<<historySizeBits);
		return ans;
	}
}

//create&define Global History and Global Table
class GH_GT : public BTB_Struct {
	public:
	unsigned GlobalHistory=0;
	int* GlobalFSMTable;
	bool is_share;

	GH_GT(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState, bool is_share)
		:BTB_Struct(btbSize, historySize, tagSize, fsmState) {
			GlobalFSMTable = new int[1<<historySizeBits];
			MEMSET(GlobalFSMTable, FsmDefault, 1<<historySizeBits);
			GH_GT::is_share = is_share;
	}

	unsigned& get_history(BTB_data *entry) override {
		return GlobalHistory;	
	} 
	int* get_state_array(BTB_data *entry) override {
		return GlobalFSMTable;
	}
	int* local_fsm_array_create() override {
		return nullptr;
	}
}









/** 
* @brief According to the parameters we will decide who we will turn on
*/
int GG=0;//global history,global table(fsm)
int GL=0;//global history,local table(fsm)
int LG=0;//local history,global table(fsm)
int LL=0;//local history,local table(fsm)
BTB_Struct* init_bp_struct; 
int BP_init(unsigned btbSize, unsigned historySize, unsigned tagSize, unsigned fsmState,
			bool isGlobalHist, bool isGlobalTable, int Shared){
				if (isGlobalHist == 0)	{
					if (isGlobalTable == 0)	{//DEFINE LOCAL HISTORY && LOCAL TABLE
						init_bp_struct=new LH_LT(btbSize,historySize,tagSize,fsmState);
						LL=1;
					}
					else{//DEFINE LOCAL HISTORY && GLOBAL TABLE
						init_bp_struct=new LH_GT(btbSize,historySize,tagSize,fsmState,Shared);
						LG=1;
					}
				}				
				else {
					if (isGlobalTable == 0)	{//DEFINE GLOBAL HISTORY && LOCAL TABLE
						init_bp_struct=new GH_LT(btbSize,historySize,tagSize,fsmState);
						int GL=1;
					}
					else {//DEFINE GLOBAL HISTORY && GLOBAL TABLE
						init_bp_struct=new GH_GT(btbSize,historySize,tagSize,fsmState,Shared);
						int GG=1;
					}
				}				
	return 0;
}








bool BP_predict(uint32_t pc, uint32_t *dst){
	bool a=init_bp_struct->BP_predict_My(pc,*dst);
	}
	return a;
}

void BP_update(uint32_t pc, uint32_t targetPc, bool taken, uint32_t pred_dst){
	return;
}

void BP_GetStats(SIM_stats *curStats){
	return;
}