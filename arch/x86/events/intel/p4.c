/*
 * Netburst Performance Events (P4, old Xeon)
 *
 *  Copyright (C) 2010 Parallels, Inc., Cyrill Gorcunov <gorcunov@openvz.org>
 *  Copyright (C) 2010 Intel Corporation, Lin Ming <ming.m.lin@intel.com>
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/perf_event.h>

#include <asm/perf_event_p4.h>
#include <asm/hardirq.h>
#include <asm/apic.h>

#include "../perf_event.h"

#define P4_CNTR_LIMIT 3
/*
 * array indices: 0,1 - HT threads, used with HT enabled cpu
 */
struct p4_event_bind {
	unsigned int opcode;			/* Event code and ESCR selector */
	unsigned int escr_msr[2];		/* ESCR MSR for this event */
	unsigned int escr_emask;		/* valid ESCR EventMask bits */
	unsigned int shared;			/* event is shared across threads */
	char cntr[2][P4_CNTR_LIMIT];		/* counter index (offset), -1 on abscence */
};

struct p4_pebs_bind {
	unsigned int metric_pebs;
	unsigned int metric_vert;
};

/* it sets P4_PEBS_ENABLE_UOP_TAG as well */
#define P4_GEN_PEBS_BIND(name, pebs, vert)			\
	[P4_PEBS_METRIC__##name] = {				\
		.metric_pebs = pebs | P4_PEBS_ENABLE_UOP_TAG,	\
		.metric_vert = vert,				\
	}

/*
 * note we have P4_PEBS_ENABLE_UOP_TAG always set here
 *
 * it's needed for mapping P4_PEBS_CONFIG_METRIC_MASK bits of
 * event configuration to find out which values are to be
 * written into MSR_IA32_PEBS_ENABLE and MSR_P4_PEBS_MATRIX_VERT
 * resgisters
 */
static struct p4_pebs_bind p4_pebs_bind_map[] = {
	P4_GEN_PEBS_BIND(1stl_cache_load_miss_retired,	0x0000001, 0x0000001),
	P4_GEN_PEBS_BIND(2ndl_cache_load_miss_retired,	0x0000002, 0x0000001),
	P4_GEN_PEBS_BIND(dtlb_load_miss_retired,	0x0000004, 0x0000001),
	P4_GEN_PEBS_BIND(dtlb_store_miss_retired,	0x0000004, 0x0000002),
	P4_GEN_PEBS_BIND(dtlb_all_miss_retired,		0x0000004, 0x0000003),
	P4_GEN_PEBS_BIND(tagged_mispred_branch,		0x0018000, 0x0000010),
	P4_GEN_PEBS_BIND(mob_load_replay_retired,	0x0000200, 0x0000001),
	P4_GEN_PEBS_BIND(split_load_retired,		0x0000400, 0x0000001),
	P4_GEN_PEBS_BIND(split_store_retired,		0x0000400, 0x0000002),
};

/*
 * Note that we don't use CCCR1 here, there is an
 * exception for P4_BSQ_ALLOCATION but we just have
 * no workaround
 *
 * consider this binding as resources which particular
 * event may borrow, it doesn't contain EventMask,
 * Tags and friends -- they are left to a caller
 */
static struct p4_event_bind p4_event_bind_map[] = {
	[P4_EVENT_TC_DELIVER_MODE] = {
		.opcode		= P4_OPCODE(P4_EVENT_TC_DELIVER_MODE),
		.escr_msr	= { MSR_P4_TC_ESCR0, MSR_P4_TC_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_TC_DELIVER_MODE, DD)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_TC_DELIVER_MODE, DB)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_TC_DELIVER_MODE, DI)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_TC_DELIVER_MODE, BD)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_TC_DELIVER_MODE, BB)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_TC_DELIVER_MODE, BI)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_TC_DELIVER_MODE, ID),
		.shared		= 1,
		.cntr		= { {4, 5, -1}, {6, 7, -1} },
	},
	[P4_EVENT_BPU_FETCH_REQUEST] = {
		.opcode		= P4_OPCODE(P4_EVENT_BPU_FETCH_REQUEST),
		.escr_msr	= { MSR_P4_BPU_ESCR0, MSR_P4_BPU_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_BPU_FETCH_REQUEST, TCMISS),
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_ITLB_REFERENCE] = {
		.opcode		= P4_OPCODE(P4_EVENT_ITLB_REFERENCE),
		.escr_msr	= { MSR_P4_ITLB_ESCR0, MSR_P4_ITLB_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_ITLB_REFERENCE, HIT)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_ITLB_REFERENCE, MISS)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_ITLB_REFERENCE, HIT_UK),
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_MEMORY_CANCEL] = {
		.opcode		= P4_OPCODE(P4_EVENT_MEMORY_CANCEL),
		.escr_msr	= { MSR_P4_DAC_ESCR0, MSR_P4_DAC_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_MEMORY_CANCEL, ST_RB_FULL)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_MEMORY_CANCEL, 64K_CONF),
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_MEMORY_COMPLETE] = {
		.opcode		= P4_OPCODE(P4_EVENT_MEMORY_COMPLETE),
		.escr_msr	= { MSR_P4_SAAT_ESCR0 , MSR_P4_SAAT_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_MEMORY_COMPLETE, LSC)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_MEMORY_COMPLETE, SSC),
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_LOAD_PORT_REPLAY] = {
		.opcode		= P4_OPCODE(P4_EVENT_LOAD_PORT_REPLAY),
		.escr_msr	= { MSR_P4_SAAT_ESCR0, MSR_P4_SAAT_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_LOAD_PORT_REPLAY, SPLIT_LD),
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_STORE_PORT_REPLAY] = {
		.opcode		= P4_OPCODE(P4_EVENT_STORE_PORT_REPLAY),
		.escr_msr	= { MSR_P4_SAAT_ESCR0 ,  MSR_P4_SAAT_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_STORE_PORT_REPLAY, SPLIT_ST),
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_MOB_LOAD_REPLAY] = {
		.opcode		= P4_OPCODE(P4_EVENT_MOB_LOAD_REPLAY),
		.escr_msr	= { MSR_P4_MOB_ESCR0, MSR_P4_MOB_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_MOB_LOAD_REPLAY, NO_STA)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_MOB_LOAD_REPLAY, NO_STD)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_MOB_LOAD_REPLAY, PARTIAL_DATA)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_MOB_LOAD_REPLAY, UNALGN_ADDR),
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_PAGE_WALK_TYPE] = {
		.opcode		= P4_OPCODE(P4_EVENT_PAGE_WALK_TYPE),
		.escr_msr	= { MSR_P4_PMH_ESCR0, MSR_P4_PMH_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_PAGE_WALK_TYPE, DTMISS)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_PAGE_WALK_TYPE, ITMISS),
		.shared		= 1,
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_BSQ_CACHE_REFERENCE] = {
		.opcode		= P4_OPCODE(P4_EVENT_BSQ_CACHE_REFERENCE),
		.escr_msr	= { MSR_P4_BSU_ESCR0, MSR_P4_BSU_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_2ndL_HITS)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_2ndL_HITE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_2ndL_HITM)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_3rdL_HITS)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_3rdL_HITE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_3rdL_HITM)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_2ndL_MISS)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_3rdL_MISS)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, WR_2ndL_MISS),
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_IOQ_ALLOCATION] = {
		.opcode		= P4_OPCODE(P4_EVENT_IOQ_ALLOCATION),
		.escr_msr	= { MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, DEFAULT)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, ALL_READ)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, ALL_WRITE)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, MEM_UC)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, MEM_WC)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, MEM_WT)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, MEM_WP)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, MEM_WB)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, OWN)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, OTHER)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ALLOCATION, PREFETCH),
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_IOQ_ACTIVE_ENTRIES] = {	/* shared ESCR */
		.opcode		= P4_OPCODE(P4_EVENT_IOQ_ACTIVE_ENTRIES),
		.escr_msr	= { MSR_P4_FSB_ESCR1,  MSR_P4_FSB_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, DEFAULT)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, ALL_READ)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, ALL_WRITE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, MEM_UC)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, MEM_WC)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, MEM_WT)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, MEM_WP)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, MEM_WB)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, OWN)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, OTHER)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_IOQ_ACTIVE_ENTRIES, PREFETCH),
		.cntr		= { {2, -1, -1}, {3, -1, -1} },
	},
	[P4_EVENT_FSB_DATA_ACTIVITY] = {
		.opcode		= P4_OPCODE(P4_EVENT_FSB_DATA_ACTIVITY),
		.escr_msr	= { MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_FSB_DATA_ACTIVITY, DRDY_DRV)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_FSB_DATA_ACTIVITY, DRDY_OWN)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_FSB_DATA_ACTIVITY, DRDY_OTHER)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_FSB_DATA_ACTIVITY, DBSY_DRV)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_FSB_DATA_ACTIVITY, DBSY_OWN)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_FSB_DATA_ACTIVITY, DBSY_OTHER),
		.shared		= 1,
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_BSQ_ALLOCATION] = {		/* shared ESCR, broken CCCR1 */
		.opcode		= P4_OPCODE(P4_EVENT_BSQ_ALLOCATION),
		.escr_msr	= { MSR_P4_BSU_ESCR0, MSR_P4_BSU_ESCR0 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_TYPE0)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_TYPE1)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_LEN0)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_LEN1)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_IO_TYPE)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_LOCK_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_CACHE_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_SPLIT_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_DEM_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, REQ_ORD_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, MEM_TYPE0)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, MEM_TYPE1)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ALLOCATION, MEM_TYPE2),
		.cntr		= { {0, -1, -1}, {1, -1, -1} },
	},
	[P4_EVENT_BSQ_ACTIVE_ENTRIES] = {	/* shared ESCR */
		.opcode		= P4_OPCODE(P4_EVENT_BSQ_ACTIVE_ENTRIES),
		.escr_msr	= { MSR_P4_BSU_ESCR1 , MSR_P4_BSU_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_TYPE0)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_TYPE1)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_LEN0)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_LEN1)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_IO_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_LOCK_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_CACHE_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_SPLIT_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_DEM_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, REQ_ORD_TYPE)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, MEM_TYPE0)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, MEM_TYPE1)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_ACTIVE_ENTRIES, MEM_TYPE2),
		.cntr		= { {2, -1, -1}, {3, -1, -1} },
	},
	[P4_EVENT_SSE_INPUT_ASSIST] = {
		.opcode		= P4_OPCODE(P4_EVENT_SSE_INPUT_ASSIST),
		.escr_msr	= { MSR_P4_FIRM_ESCR0, MSR_P4_FIRM_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_SSE_INPUT_ASSIST, ALL),
		.shared		= 1,
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_PACKED_SP_UOP] = {
		.opcode		= P4_OPCODE(P4_EVENT_PACKED_SP_UOP),
		.escr_msr	= { MSR_P4_FIRM_ESCR0, MSR_P4_FIRM_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_PACKED_SP_UOP, ALL),
		.shared		= 1,
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_PACKED_DP_UOP] = {
		.opcode		= P4_OPCODE(P4_EVENT_PACKED_DP_UOP),
		.escr_msr	= { MSR_P4_FIRM_ESCR0, MSR_P4_FIRM_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_PACKED_DP_UOP, ALL),
		.shared		= 1,
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_SCALAR_SP_UOP] = {
		.opcode		= P4_OPCODE(P4_EVENT_SCALAR_SP_UOP),
		.escr_msr	= { MSR_P4_FIRM_ESCR0, MSR_P4_FIRM_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_SCALAR_SP_UOP, ALL),
		.shared		= 1,
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_SCALAR_DP_UOP] = {
		.opcode		= P4_OPCODE(P4_EVENT_SCALAR_DP_UOP),
		.escr_msr	= { MSR_P4_FIRM_ESCR0, MSR_P4_FIRM_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_SCALAR_DP_UOP, ALL),
		.shared		= 1,
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_64BIT_MMX_UOP] = {
		.opcode		= P4_OPCODE(P4_EVENT_64BIT_MMX_UOP),
		.escr_msr	= { MSR_P4_FIRM_ESCR0, MSR_P4_FIRM_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_64BIT_MMX_UOP, ALL),
		.shared		= 1,
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_128BIT_MMX_UOP] = {
		.opcode		= P4_OPCODE(P4_EVENT_128BIT_MMX_UOP),
		.escr_msr	= { MSR_P4_FIRM_ESCR0, MSR_P4_FIRM_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_128BIT_MMX_UOP, ALL),
		.shared		= 1,
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_X87_FP_UOP] = {
		.opcode		= P4_OPCODE(P4_EVENT_X87_FP_UOP),
		.escr_msr	= { MSR_P4_FIRM_ESCR0, MSR_P4_FIRM_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_X87_FP_UOP, ALL),
		.shared		= 1,
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_TC_MISC] = {
		.opcode		= P4_OPCODE(P4_EVENT_TC_MISC),
		.escr_msr	= { MSR_P4_TC_ESCR0, MSR_P4_TC_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_TC_MISC, FLUSH),
		.cntr		= { {4, 5, -1}, {6, 7, -1} },
	},
	[P4_EVENT_GLOBAL_POWER_EVENTS] = {
		.opcode		= P4_OPCODE(P4_EVENT_GLOBAL_POWER_EVENTS),
		.escr_msr	= { MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_GLOBAL_POWER_EVENTS, RUNNING),
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_TC_MS_XFER] = {
		.opcode		= P4_OPCODE(P4_EVENT_TC_MS_XFER),
		.escr_msr	= { MSR_P4_MS_ESCR0, MSR_P4_MS_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_TC_MS_XFER, CISC),
		.cntr		= { {4, 5, -1}, {6, 7, -1} },
	},
	[P4_EVENT_UOP_QUEUE_WRITES] = {
		.opcode		= P4_OPCODE(P4_EVENT_UOP_QUEUE_WRITES),
		.escr_msr	= { MSR_P4_MS_ESCR0, MSR_P4_MS_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_UOP_QUEUE_WRITES, FROM_TC_BUILD)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_UOP_QUEUE_WRITES, FROM_TC_DELIVER)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_UOP_QUEUE_WRITES, FROM_ROM),
		.cntr		= { {4, 5, -1}, {6, 7, -1} },
	},
	[P4_EVENT_RETIRED_MISPRED_BRANCH_TYPE] = {
		.opcode		= P4_OPCODE(P4_EVENT_RETIRED_MISPRED_BRANCH_TYPE),
		.escr_msr	= { MSR_P4_TBPU_ESCR0 , MSR_P4_TBPU_ESCR0 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_MISPRED_BRANCH_TYPE, CONDITIONAL)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_MISPRED_BRANCH_TYPE, CALL)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_MISPRED_BRANCH_TYPE, RETURN)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_MISPRED_BRANCH_TYPE, INDIRECT),
		.cntr		= { {4, 5, -1}, {6, 7, -1} },
	},
	[P4_EVENT_RETIRED_BRANCH_TYPE] = {
		.opcode		= P4_OPCODE(P4_EVENT_RETIRED_BRANCH_TYPE),
		.escr_msr	= { MSR_P4_TBPU_ESCR0 , MSR_P4_TBPU_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_BRANCH_TYPE, CONDITIONAL)	|
			P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_BRANCH_TYPE, CALL)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_BRANCH_TYPE, RETURN)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_BRANCH_TYPE, INDIRECT),
		.cntr		= { {4, 5, -1}, {6, 7, -1} },
	},
	[P4_EVENT_RESOURCE_STALL] = {
		.opcode		= P4_OPCODE(P4_EVENT_RESOURCE_STALL),
		.escr_msr	= { MSR_P4_ALF_ESCR0, MSR_P4_ALF_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_RESOURCE_STALL, SBFULL),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_WC_BUFFER] = {
		.opcode		= P4_OPCODE(P4_EVENT_WC_BUFFER),
		.escr_msr	= { MSR_P4_DAC_ESCR0, MSR_P4_DAC_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_WC_BUFFER, WCB_EVICTS)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_WC_BUFFER, WCB_FULL_EVICTS),
		.shared		= 1,
		.cntr		= { {8, 9, -1}, {10, 11, -1} },
	},
	[P4_EVENT_B2B_CYCLES] = {
		.opcode		= P4_OPCODE(P4_EVENT_B2B_CYCLES),
		.escr_msr	= { MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1 },
		.escr_emask	= 0,
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_BNR] = {
		.opcode		= P4_OPCODE(P4_EVENT_BNR),
		.escr_msr	= { MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1 },
		.escr_emask	= 0,
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_SNOOP] = {
		.opcode		= P4_OPCODE(P4_EVENT_SNOOP),
		.escr_msr	= { MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1 },
		.escr_emask	= 0,
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_RESPONSE] = {
		.opcode		= P4_OPCODE(P4_EVENT_RESPONSE),
		.escr_msr	= { MSR_P4_FSB_ESCR0, MSR_P4_FSB_ESCR1 },
		.escr_emask	= 0,
		.cntr		= { {0, -1, -1}, {2, -1, -1} },
	},
	[P4_EVENT_FRONT_END_EVENT] = {
		.opcode		= P4_OPCODE(P4_EVENT_FRONT_END_EVENT),
		.escr_msr	= { MSR_P4_CRU_ESCR2, MSR_P4_CRU_ESCR3 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_FRONT_END_EVENT, NBOGUS)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_FRONT_END_EVENT, BOGUS),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_EXECUTION_EVENT] = {
		.opcode		= P4_OPCODE(P4_EVENT_EXECUTION_EVENT),
		.escr_msr	= { MSR_P4_CRU_ESCR2, MSR_P4_CRU_ESCR3 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, NBOGUS0)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, NBOGUS1)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, NBOGUS2)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, NBOGUS3)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, BOGUS0)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, BOGUS1)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, BOGUS2)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, BOGUS3),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_REPLAY_EVENT] = {
		.opcode		= P4_OPCODE(P4_EVENT_REPLAY_EVENT),
		.escr_msr	= { MSR_P4_CRU_ESCR2, MSR_P4_CRU_ESCR3 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_REPLAY_EVENT, NBOGUS)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_REPLAY_EVENT, BOGUS),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_INSTR_RETIRED] = {
		.opcode		= P4_OPCODE(P4_EVENT_INSTR_RETIRED),
		.escr_msr	= { MSR_P4_CRU_ESCR0, MSR_P4_CRU_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_INSTR_RETIRED, NBOGUSNTAG)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_INSTR_RETIRED, NBOGUSTAG)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_INSTR_RETIRED, BOGUSNTAG)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_INSTR_RETIRED, BOGUSTAG),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_UOPS_RETIRED] = {
		.opcode		= P4_OPCODE(P4_EVENT_UOPS_RETIRED),
		.escr_msr	= { MSR_P4_CRU_ESCR0, MSR_P4_CRU_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_UOPS_RETIRED, NBOGUS)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_UOPS_RETIRED, BOGUS),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_UOP_TYPE] = {
		.opcode		= P4_OPCODE(P4_EVENT_UOP_TYPE),
		.escr_msr	= { MSR_P4_RAT_ESCR0, MSR_P4_RAT_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_UOP_TYPE, TAGLOADS)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_UOP_TYPE, TAGSTORES),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_BRANCH_RETIRED] = {
		.opcode		= P4_OPCODE(P4_EVENT_BRANCH_RETIRED),
		.escr_msr	= { MSR_P4_CRU_ESCR2, MSR_P4_CRU_ESCR3 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_BRANCH_RETIRED, MMNP)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BRANCH_RETIRED, MMNM)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BRANCH_RETIRED, MMTP)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_BRANCH_RETIRED, MMTM),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_MISPRED_BRANCH_RETIRED] = {
		.opcode		= P4_OPCODE(P4_EVENT_MISPRED_BRANCH_RETIRED),
		.escr_msr	= { MSR_P4_CRU_ESCR0, MSR_P4_CRU_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_MISPRED_BRANCH_RETIRED, NBOGUS),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_X87_ASSIST] = {
		.opcode		= P4_OPCODE(P4_EVENT_X87_ASSIST),
		.escr_msr	= { MSR_P4_CRU_ESCR2, MSR_P4_CRU_ESCR3 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_X87_ASSIST, FPSU)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_X87_ASSIST, FPSO)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_X87_ASSIST, POAO)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_X87_ASSIST, POAU)			|
			P4_ESCR_EMASK_BIT(P4_EVENT_X87_ASSIST, PREA),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_MACHINE_CLEAR] = {
		.opcode		= P4_OPCODE(P4_EVENT_MACHINE_CLEAR),
		.escr_msr	= { MSR_P4_CRU_ESCR2, MSR_P4_CRU_ESCR3 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_MACHINE_CLEAR, CLEAR)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_MACHINE_CLEAR, MOCLEAR)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_MACHINE_CLEAR, SMCLEAR),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
	[P4_EVENT_INSTR_COMPLETED] = {
		.opcode		= P4_OPCODE(P4_EVENT_INSTR_COMPLETED),
		.escr_msr	= { MSR_P4_CRU_ESCR0, MSR_P4_CRU_ESCR1 },
		.escr_emask	=
			P4_ESCR_EMASK_BIT(P4_EVENT_INSTR_COMPLETED, NBOGUS)		|
			P4_ESCR_EMASK_BIT(P4_EVENT_INSTR_COMPLETED, BOGUS),
		.cntr		= { {12, 13, 16}, {14, 15, 17} },
	},
};

#define P4_GEN_CACHE_EVENT(event, bit, metric)				  \
	p4_config_pack_escr(P4_ESCR_EVENT(event)			| \
			    P4_ESCR_EMASK_BIT(event, bit))		| \
	p4_config_pack_cccr(metric					| \
			    P4_CCCR_ESEL(P4_OPCODE_ESEL(P4_OPCODE(event))))

static __initconst const u64 p4_hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
 [ C(L1D ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0,
		[ C(RESULT_MISS)   ] = P4_GEN_CACHE_EVENT(P4_EVENT_REPLAY_EVENT, NBOGUS,
						P4_PEBS_METRIC__1stl_cache_load_miss_retired),
	},
 },
 [ C(LL  ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0,
		[ C(RESULT_MISS)   ] = P4_GEN_CACHE_EVENT(P4_EVENT_REPLAY_EVENT, NBOGUS,
						P4_PEBS_METRIC__2ndl_cache_load_miss_retired),
	},
},
 [ C(DTLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0,
		[ C(RESULT_MISS)   ] = P4_GEN_CACHE_EVENT(P4_EVENT_REPLAY_EVENT, NBOGUS,
						P4_PEBS_METRIC__dtlb_load_miss_retired),
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0x0,
		[ C(RESULT_MISS)   ] = P4_GEN_CACHE_EVENT(P4_EVENT_REPLAY_EVENT, NBOGUS,
						P4_PEBS_METRIC__dtlb_store_miss_retired),
	},
 },
 [ C(ITLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = P4_GEN_CACHE_EVENT(P4_EVENT_ITLB_REFERENCE, HIT,
						P4_PEBS_METRIC__none),
		[ C(RESULT_MISS)   ] = P4_GEN_CACHE_EVENT(P4_EVENT_ITLB_REFERENCE, MISS,
						P4_PEBS_METRIC__none),
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
 [ C(NODE) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
};

/*
 * Because of Netburst being quite restricted in how many
 * identical events may run simultaneously, we introduce event aliases,
 * ie the different events which have the same functionality but
 * utilize non-intersected resources (ESCR/CCCR/counter registers).
 *
 * This allow us to relax restrictions a bit and run two or more
 * identical events together.
 *
 * Never set any custom internal bits such as P4_CONFIG_HT,
 * P4_CONFIG_ALIASABLE or bits for P4_PEBS_METRIC, they are
 * either up to date automatically or not applicable at all.
 */
static struct p4_event_alias {
	u64 original;
	u64 alternative;
} p4_event_aliases[] = {
	{
		/*
		 * Non-halted cycles can be substituted with non-sleeping cycles (see
		 * Intel SDM Vol3b for details). We need this alias to be able
		 * to run nmi-watchdog and 'perf top' (or any other user space tool
		 * which is interested in running PERF_COUNT_HW_CPU_CYCLES)
		 * simultaneously.
		 */
	.original	=
		p4_config_pack_escr(P4_ESCR_EVENT(P4_EVENT_GLOBAL_POWER_EVENTS)		|
				    P4_ESCR_EMASK_BIT(P4_EVENT_GLOBAL_POWER_EVENTS, RUNNING)),
	.alternative	=
		p4_config_pack_escr(P4_ESCR_EVENT(P4_EVENT_EXECUTION_EVENT)		|
				    P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, NBOGUS0)|
				    P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, NBOGUS1)|
				    P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, NBOGUS2)|
				    P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, NBOGUS3)|
				    P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, BOGUS0)	|
				    P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, BOGUS1)	|
				    P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, BOGUS2)	|
				    P4_ESCR_EMASK_BIT(P4_EVENT_EXECUTION_EVENT, BOGUS3))|
		p4_config_pack_cccr(P4_CCCR_THRESHOLD(15) | P4_CCCR_COMPLEMENT		|
				    P4_CCCR_COMPARE),
	},
};

static u64 p4_get_alias_event(u64 config)
{
	u64 config_match;
	int i;

	/*
	 * Only event with special mark is allowed,
	 * we're to be sure it didn't come as malformed
	 * RAW event.
	 */
	if (!(config & P4_CONFIG_ALIASABLE))
		return 0;

	config_match = config & P4_CONFIG_EVENT_ALIAS_MASK;

	for (i = 0; i < ARRAY_SIZE(p4_event_aliases); i++) {
		if (config_match == p4_event_aliases[i].original) {
			config_match = p4_event_aliases[i].alternative;
			break;
		} else if (config_match == p4_event_aliases[i].alternative) {
			config_match = p4_event_aliases[i].original;
			break;
		}
	}

	if (i >= ARRAY_SIZE(p4_event_aliases))
		return 0;

	return config_match | (config & P4_CONFIG_EVENT_ALIAS_IMMUTABLE_BITS);
}

static u64 p4_general_events[PERF_COUNT_HW_MAX] = {
  /* non-halted CPU clocks */
  [PERF_COUNT_HW_CPU_CYCLES] =
	p4_config_pack_escr(P4_ESCR_EVENT(P4_EVENT_GLOBAL_POWER_EVENTS)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_GLOBAL_POWER_EVENTS, RUNNING))	|
		P4_CONFIG_ALIASABLE,

  /*
   * retired instructions
   * in a sake of simplicity we don't use the FSB tagging
   */
  [PERF_COUNT_HW_INSTRUCTIONS] =
	p4_config_pack_escr(P4_ESCR_EVENT(P4_EVENT_INSTR_RETIRED)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_INSTR_RETIRED, NBOGUSNTAG)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_INSTR_RETIRED, BOGUSNTAG)),

  /* cache hits */
  [PERF_COUNT_HW_CACHE_REFERENCES] =
	p4_config_pack_escr(P4_ESCR_EVENT(P4_EVENT_BSQ_CACHE_REFERENCE)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_2ndL_HITS)	|
		P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_2ndL_HITE)	|
		P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_2ndL_HITM)	|
		P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_3rdL_HITS)	|
		P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_3rdL_HITE)	|
		P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_3rdL_HITM)),

  /* cache misses */
  [PERF_COUNT_HW_CACHE_MISSES] =
	p4_config_pack_escr(P4_ESCR_EVENT(P4_EVENT_BSQ_CACHE_REFERENCE)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_2ndL_MISS)	|
		P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, RD_3rdL_MISS)	|
		P4_ESCR_EMASK_BIT(P4_EVENT_BSQ_CACHE_REFERENCE, WR_2ndL_MISS)),

  /* branch instructions retired */
  [PERF_COUNT_HW_BRANCH_INSTRUCTIONS] =
	p4_config_pack_escr(P4_ESCR_EVENT(P4_EVENT_RETIRED_BRANCH_TYPE)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_BRANCH_TYPE, CONDITIONAL)	|
		P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_BRANCH_TYPE, CALL)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_BRANCH_TYPE, RETURN)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_RETIRED_BRANCH_TYPE, INDIRECT)),

  /* mispredicted branches retired */
  [PERF_COUNT_HW_BRANCH_MISSES]	=
	p4_config_pack_escr(P4_ESCR_EVENT(P4_EVENT_MISPRED_BRANCH_RETIRED)	|
		P4_ESCR_EMASK_BIT(P4_EVENT_MISPRED_BRANCH_RETIRED, NBOGUS)),

  /* bus ready clocks (cpu is driving #DRDY_DRV\#DRDY_OWN):  */
  [PERF_COUNT_HW_BUS_CYCLES] =
	p4_config_pack_escr(P4_ESCR_EVENT(P4_EVENT_FSB_DATA_ACTIVITY)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_FSB_DATA_ACTIVITY, DRDY_DRV)		|
		P4_ESCR_EMASK_BIT(P4_EVENT_FSB_DATA_ACTIVITY, DRDY_OWN))	|
	p4_config_pack_cccr(P4_CCCR_EDGE | P4_CCCR_COMPARE),
};

static struct p4_event_bind *p4_config_get_bind(u64 config)
{
	unsigned int evnt = p4_config_unpack_event(config);
	struct p4_event_bind *bind = NULL;

	if (evnt < ARRAY_SIZE(p4_event_bind_map))
		bind = &p4_event_bind_map[evnt];

	return bind;
}

static u64 p4_pmu_event_map(int hw_event)
{
	struct p4_event_bind *bind;
	unsigned int esel;
	u64 config;

	config = p4_general_events[hw_event];
	bind = p4_config_get_bind(config);
	esel = P4_OPCODE_ESEL(bind->opcode);
	config |= p4_config_pack_cccr(P4_CCCR_ESEL(esel));

	return config;
}

/* check cpu model specifics */
static bool p4_event_match_cpu_model(unsigned int event_idx)
{
	/* INSTR_COMPLETED event only exist for model 3, 4, 6 (Prescott) */
	if (event_idx == P4_EVENT_INSTR_COMPLETED) {
		if (boot_cpu_data.x86_model != 3 &&
			boot_cpu_data.x86_model != 4 &&
			boot_cpu_data.x86_model != 6)
			return false;
	}

	/*
	 * For info
	 * - IQ_ESCR0, IQ_ESCR1 only for models 1 and 2
	 */

	return true;
}

static int p4_validate_raw_event(struct perf_event *event)
{
	unsigned int v, emask;

	/* User data may have out-of-bound event index */
	v = p4_config_unpack_event(event->attr.config);
	if (v >= ARRAY_SIZE(p4_event_bind_map))
		return -EINVAL;

	/* It may be unsupported: */
	if (!p4_event_match_cpu_model(v))
		return -EINVAL;

	/*
	 * NOTE: P4_CCCR_THREAD_ANY has not the same meaning as
	 * in Architectural Performance Monitoring, it means not
	 * on _which_ logical cpu to count but rather _when_, ie it
	 * depends on logical cpu state -- count event if one cpu active,
	 * none, both or any, so we just allow user to pass any value
	 * desired.
	 *
	 * In turn we always set Tx_OS/Tx_USR bits bound to logical
	 * cpu without their propagation to another cpu
	 */

	/*
	 * if an event is shared across the logical threads
	 * the user needs special permissions to be able to use it
	 */
	if (p4_ht_active() && p4_event_bind_map[v].shared) {
		v = perf_allow_cpu(&event->attr);
		if (v)
			return v;
	}

	/* ESCR EventMask bits may be invalid */
	emask = p4_config_unpack_escr(event->attr.config) & P4_ESCR_EVENTMASK_MASK;
	if (emask & ~p4_event_bind_map[v].escr_emask)
		return -EINVAL;

	/*
	 * it may have some invalid PEBS bits
	 */
	if (p4_config_pebs_has(event->attr.config, P4_PEBS_CONFIG_ENABLE))
		return -EINVAL;

	v = p4_config_unpack_metric(event->attr.config);
	if (v >= ARRAY_SIZE(p4_pebs_bind_map))
		return -EINVAL;

	return 0;
}

static int p4_hw_config(struct perf_event *event)
{
	int cpu = get_cpu();
	int rc = 0;
	u32 escr, cccr;

	/*
	 * the reason we use cpu that early is that: if we get scheduled
	 * first time on the same cpu -- we will not need swap thread
	 * specific flags in config (and will save some cpu cycles)
	 */

	cccr = p4_default_cccr_conf(cpu);
	escr = p4_default_escr_conf(cpu, event->attr.exclude_kernel,
					 event->attr.exclude_user);
	event->hw.config = p4_config_pack_escr(escr) |
			   p4_config_pack_cccr(cccr);

	if (p4_ht_active() && p4_ht_thread(cpu))
		event->hw.config = p4_set_ht_bit(event->hw.config);

	if (event->attr.type == PERF_TYPE_RAW) {
		struct p4_event_bind *bind;
		unsigned int esel;
		/*
		 * Clear bits we reserve to be managed by kernel itself
		 * and never allowed from a user space
		 */
		event->attr.config &= P4_CONFIG_MASK;

		rc = p4_validate_raw_event(event);
		if (rc)
			goto out;

		/*
		 * Note that for RAW events we allow user to use P4_CCCR_RESERVED
		 * bits since we keep additional info here (for cache events and etc)
		 */
		event->hw.config |= event->attr.config;
		bind = p4_config_get_bind(event->attr.config);
		if (!bind) {
			rc = -EINVAL;
			goto out;
		}
		esel = P4_OPCODE_ESEL(bind->opcode);
		event->hw.config |= p4_config_pack_cccr(P4_CCCR_ESEL(esel));
	}

	rc = x86_setup_perfctr(event);
out:
	put_cpu();
	return rc;
}

static inline int p4_pmu_clear_cccr_ovf(struct hw_perf_event *hwc)
{
	u64 v;

	/* an official way for overflow indication */
	rdmsrl(hwc->config_base, v);
	if (v & P4_CCCR_OVF) {
		wrmsrl(hwc->config_base, v & ~P4_CCCR_OVF);
		return 1;
	}

	/*
	 * In some circumstances the overflow might issue an NMI but did
	 * not set P4_CCCR_OVF bit. Because a counter holds a negative value
	 * we simply check for high bit being set, if it's cleared it means
	 * the counter has reached zero value and continued counting before
	 * real NMI signal was received:
	 */
	rdmsrl(hwc->event_base, v);
	if (!(v & ARCH_P4_UNFLAGGED_BIT))
		return 1;

	return 0;
}

static void p4_pmu_disable_pebs(void)
{
	/*
	 * FIXME
	 *
	 * It's still allowed that two threads setup same cache
	 * events so we can't simply clear metrics until we knew
	 * no one is depending on us, so we need kind of counter
	 * for "ReplayEvent" users.
	 *
	 * What is more complex -- RAW events, if user (for some
	 * reason) will pass some cache event metric with improper
	 * event opcode -- it's fine from hardware point of view
	 * but completely nonsense from "meaning" of such action.
	 *
	 * So at moment let leave metrics turned on forever -- it's
	 * ok for now but need to be revisited!
	 *
	 * (void)wrmsrl_safe(MSR_IA32_PEBS_ENABLE, 0);
	 * (void)wrmsrl_safe(MSR_P4_PEBS_MATRIX_VERT, 0);
	 */
}

static inline void p4_pmu_disable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/*
	 * If event gets disabled while counter is in overflowed
	 * state we need to clear P4_CCCR_OVF, otherwise interrupt get
	 * asserted again and again
	 */
	(void)wrmsrl_safe(hwc->config_base,
		p4_config_unpack_cccr(hwc->config) & ~P4_CCCR_ENABLE & ~P4_CCCR_OVF & ~P4_CCCR_RESERVED);
}

static void p4_pmu_disable_all(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		struct perf_event *event = cpuc->events[idx];
		if (!test_bit(idx, cpuc->active_mask))
			continue;
		p4_pmu_disable_event(event);
	}

	p4_pmu_disable_pebs();
}

/* configuration must be valid */
static void p4_pmu_enable_pebs(u64 config)
{
	struct p4_pebs_bind *bind;
	unsigned int idx;

	BUILD_BUG_ON(P4_PEBS_METRIC__max > P4_PEBS_CONFIG_METRIC_MASK);

	idx = p4_config_unpack_metric(config);
	if (idx == P4_PEBS_METRIC__none)
		return;

	bind = &p4_pebs_bind_map[idx];

	(void)wrmsrl_safe(MSR_IA32_PEBS_ENABLE,	(u64)bind->metric_pebs);
	(void)wrmsrl_safe(MSR_P4_PEBS_MATRIX_VERT,	(u64)bind->metric_vert);
}

static void p4_pmu_enable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int thread = p4_ht_config_thread(hwc->config);
	u64 escr_conf = p4_config_unpack_escr(p4_clear_ht_bit(hwc->config));
	unsigned int idx = p4_config_unpack_event(hwc->config);
	struct p4_event_bind *bind;
	u64 escr_addr, cccr;

	bind = &p4_event_bind_map[idx];
	escr_addr = bind->escr_msr[thread];

	/*
	 * - we dont support cascaded counters yet
	 * - and counter 1 is broken (erratum)
	 */
	WARN_ON_ONCE(p4_is_event_cascaded(hwc->config));
	WARN_ON_ONCE(hwc->idx == 1);

	/* we need a real Event value */
	escr_conf &= ~P4_ESCR_EVENT_MASK;
	escr_conf |= P4_ESCR_EVENT(P4_OPCODE_EVNT(bind->opcode));

	cccr = p4_config_unpack_cccr(hwc->config);

	/*
	 * it could be Cache event so we need to write metrics
	 * into additional MSRs
	 */
	p4_pmu_enable_pebs(hwc->config);

	(void)wrmsrl_safe(escr_addr, escr_conf);
	(void)wrmsrl_safe(hwc->config_base,
				(cccr & ~P4_CCCR_RESERVED) | P4_CCCR_ENABLE);
}

static void p4_pmu_enable_all(int added)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx;

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		struct perf_event *event = cpuc->events[idx];
		if (!test_bit(idx, cpuc->active_mask))
			continue;
		p4_pmu_enable_event(event);
	}
}

static int p4_pmu_handle_irq(struct pt_regs *regs)
{
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct perf_event *event;
	struct hw_perf_event *hwc;
	int idx, handled = 0;
	u64 val;

	cpuc = this_cpu_ptr(&cpu_hw_events);

	for (idx = 0; idx < x86_pmu.num_counters; idx++) {
		int overflow;

		if (!test_bit(idx, cpuc->active_mask)) {
			/* catch in-flight IRQs */
			if (__test_and_clear_bit(idx, cpuc->running))
				handled++;
			continue;
		}

		event = cpuc->events[idx];
		hwc = &event->hw;

		WARN_ON_ONCE(hwc->idx != idx);

		/* it might be unflagged overflow */
		overflow = p4_pmu_clear_cccr_ovf(hwc);

		val = x86_perf_event_update(event);
		if (!overflow && (val & (1ULL << (x86_pmu.cntval_bits - 1))))
			continue;

		handled += overflow;

		/* event overflow for sure */
		perf_sample_data_init(&data, 0, hwc->last_period);

		if (!x86_perf_event_set_period(event))
			continue;


		if (perf_event_overflow(event, &data, regs))
			x86_pmu_stop(event, 0);
	}

	if (handled)
		inc_irq_stat(apic_perf_irqs);

	/*
	 * When dealing with the unmasking of the LVTPC on P4 perf hw, it has
	 * been observed that the OVF bit flag has to be cleared first _before_
	 * the LVTPC can be unmasked.
	 *
	 * The reason is the NMI line will continue to be asserted while the OVF
	 * bit is set.  This causes a second NMI to generate if the LVTPC is
	 * unmasked before the OVF bit is cleared, leading to unknown NMI
	 * messages.
	 */
	apic_write(APIC_LVTPC, APIC_DM_NMI);

	return handled;
}

/*
 * swap thread specific fields according to a thread
 * we are going to run on
 */
static void p4_pmu_swap_config_ts(struct hw_perf_event *hwc, int cpu)
{
	u32 escr, cccr;

	/*
	 * we either lucky and continue on same cpu or no HT support
	 */
	if (!p4_should_swap_ts(hwc->config, cpu))
		return;

	/*
	 * the event is migrated from an another logical
	 * cpu, so we need to swap thread specific flags
	 */

	escr = p4_config_unpack_escr(hwc->config);
	cccr = p4_config_unpack_cccr(hwc->config);

	if (p4_ht_thread(cpu)) {
		cccr &= ~P4_CCCR_OVF_PMI_T0;
		cccr |= P4_CCCR_OVF_PMI_T1;
		if (escr & P4_ESCR_T0_OS) {
			escr &= ~P4_ESCR_T0_OS;
			escr |= P4_ESCR_T1_OS;
		}
		if (escr & P4_ESCR_T0_USR) {
			escr &= ~P4_ESCR_T0_USR;
			escr |= P4_ESCR_T1_USR;
		}
		hwc->config  = p4_config_pack_escr(escr);
		hwc->config |= p4_config_pack_cccr(cccr);
		hwc->config |= P4_CONFIG_HT;
	} else {
		cccr &= ~P4_CCCR_OVF_PMI_T1;
		cccr |= P4_CCCR_OVF_PMI_T0;
		if (escr & P4_ESCR_T1_OS) {
			escr &= ~P4_ESCR_T1_OS;
			escr |= P4_ESCR_T0_OS;
		}
		if (escr & P4_ESCR_T1_USR) {
			escr &= ~P4_ESCR_T1_USR;
			escr |= P4_ESCR_T0_USR;
		}
		hwc->config  = p4_config_pack_escr(escr);
		hwc->config |= p4_config_pack_cccr(cccr);
		hwc->config &= ~P4_CONFIG_HT;
	}
}

/*
 * ESCR address hashing is tricky, ESCRs are not sequential
 * in memory but all starts from MSR_P4_BSU_ESCR0 (0x03a0) and
 * the metric between any ESCRs is laid in range [0xa0,0xe1]
 *
 * so we make ~70% filled hashtable
 */

#define P4_ESCR_MSR_BASE		0x000003a0
#define P4_ESCR_MSR_MAX			0x000003e1
#define P4_ESCR_MSR_TABLE_SIZE		(P4_ESCR_MSR_MAX - P4_ESCR_MSR_BASE + 1)
#define P4_ESCR_MSR_IDX(msr)		(msr - P4_ESCR_MSR_BASE)
#define P4_ESCR_MSR_TABLE_ENTRY(msr)	[P4_ESCR_MSR_IDX(msr)] = msr

static const unsigned int p4_escr_table[P4_ESCR_MSR_TABLE_SIZE] = {
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_ALF_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_ALF_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_BPU_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_BPU_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_BSU_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_BSU_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_CRU_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_CRU_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_CRU_ESCR2),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_CRU_ESCR3),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_CRU_ESCR4),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_CRU_ESCR5),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_DAC_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_DAC_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_FIRM_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_FIRM_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_FLAME_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_FLAME_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_FSB_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_FSB_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_IQ_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_IQ_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_IS_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_IS_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_ITLB_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_ITLB_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_IX_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_IX_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_MOB_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_MOB_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_MS_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_MS_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_PMH_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_PMH_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_RAT_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_RAT_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_SAAT_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_SAAT_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_SSU_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_SSU_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_TBPU_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_TBPU_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_TC_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_TC_ESCR1),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_U2L_ESCR0),
	P4_ESCR_MSR_TABLE_ENTRY(MSR_P4_U2L_ESCR1),
};

static int p4_get_escr_idx(unsigned int addr)
{
	unsigned int idx = P4_ESCR_MSR_IDX(addr);

	if (unlikely(idx >= P4_ESCR_MSR_TABLE_SIZE	||
			!p4_escr_table[idx]		||
			p4_escr_table[idx] != addr)) {
		WARN_ONCE(1, "P4 PMU: Wrong address passed: %x\n", addr);
		return -1;
	}

	return idx;
}

static int p4_next_cntr(int thread, unsigned long *used_mask,
			struct p4_event_bind *bind)
{
	int i, j;

	for (i = 0; i < P4_CNTR_LIMIT; i++) {
		j = bind->cntr[thread][i];
		if (j != -1 && !test_bit(j, used_mask))
			return j;
	}

	return -1;
}

static int p4_pmu_schedule_events(struct cpu_hw_events *cpuc, int n, int *assign)
{
	unsigned long used_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long escr_mask[BITS_TO_LONGS(P4_ESCR_MSR_TABLE_SIZE)];
	int cpu = smp_processor_id();
	struct hw_perf_event *hwc;
	struct p4_event_bind *bind;
	unsigned int i, thread, num;
	int cntr_idx, escr_idx;
	u64 config_alias;
	int pass;

	bitmap_zero(used_mask, X86_PMC_IDX_MAX);
	bitmap_zero(escr_mask, P4_ESCR_MSR_TABLE_SIZE);

	for (i = 0, num = n; i < n; i++, num--) {

		hwc = &cpuc->event_list[i]->hw;
		thread = p4_ht_thread(cpu);
		pass = 0;

again:
		/*
		 * It's possible to hit a circular lock
		 * between original and alternative events
		 * if both are scheduled already.
		 */
		if (pass > 2)
			goto done;

		bind = p4_config_get_bind(hwc->config);
		escr_idx = p4_get_escr_idx(bind->escr_msr[thread]);
		if (unlikely(escr_idx == -1))
			goto done;

		if (hwc->idx != -1 && !p4_should_swap_ts(hwc->config, cpu)) {
			cntr_idx = hwc->idx;
			if (assign)
				assign[i] = hwc->idx;
			goto reserve;
		}

		cntr_idx = p4_next_cntr(thread, used_mask, bind);
		if (cntr_idx == -1 || test_bit(escr_idx, escr_mask)) {
			/*
			 * Check whether an event alias is still available.
			 */
			config_alias = p4_get_alias_event(hwc->config);
			if (!config_alias)
				goto done;
			hwc->config = config_alias;
			pass++;
			goto again;
		}
		/*
		 * Perf does test runs to see if a whole group can be assigned
		 * together succesfully.  There can be multiple rounds of this.
		 * Unfortunately, p4_pmu_swap_config_ts touches the hwc->config
		 * bits, such that the next round of group assignments will
		 * cause the above p4_should_swap_ts to pass instead of fail.
		 * This leads to counters exclusive to thread0 being used by
		 * thread1.
		 *
		 * Solve this with a cheap hack, reset the idx back to -1 to
		 * force a new lookup (p4_next_cntr) to get the right counter
		 * for the right thread.
		 *
		 * This probably doesn't comply with the general spirit of how
		 * perf wants to work, but P4 is special. :-(
		 */
		if (p4_should_swap_ts(hwc->config, cpu))
			hwc->idx = -1;
		p4_pmu_swap_config_ts(hwc, cpu);
		if (assign)
			assign[i] = cntr_idx;
reserve:
		set_bit(cntr_idx, used_mask);
		set_bit(escr_idx, escr_mask);
	}

done:
	return num ? -EINVAL : 0;
}

PMU_FORMAT_ATTR(cccr, "config:0-31" );
PMU_FORMAT_ATTR(escr, "config:32-62");
PMU_FORMAT_ATTR(ht,   "config:63"   );

static struct attribute *intel_p4_formats_attr[] = {
	&format_attr_cccr.attr,
	&format_attr_escr.attr,
	&format_attr_ht.attr,
	NULL,
};

static __initconst const struct x86_pmu p4_pmu = {
	.name			= "Netburst P4/Xeon",
	.handle_irq		= p4_pmu_handle_irq,
	.disable_all		= p4_pmu_disable_all,
	.enable_all		= p4_pmu_enable_all,
	.enable			= p4_pmu_enable_event,
	.disable		= p4_pmu_disable_event,
	.eventsel		= MSR_P4_BPU_CCCR0,
	.perfctr		= MSR_P4_BPU_PERFCTR0,
	.event_map		= p4_pmu_event_map,
	.max_events		= ARRAY_SIZE(p4_general_events),
	.get_event_constraints	= x86_get_event_constraints,
	/*
	 * IF HT disabled we may need to use all
	 * ARCH_P4_MAX_CCCR counters simulaneously
	 * though leave it restricted at moment assuming
	 * HT is on
	 */
	.num_counters		= ARCH_P4_MAX_CCCR,
	.apic			= 1,
	.cntval_bits		= ARCH_P4_CNTRVAL_BITS,
	.cntval_mask		= ARCH_P4_CNTRVAL_MASK,
	.max_period		= (1ULL << (ARCH_P4_CNTRVAL_BITS - 1)) - 1,
	.hw_config		= p4_hw_config,
	.schedule_events	= p4_pmu_schedule_events,
	/*
	 * This handles erratum N15 in intel doc 249199-029,
	 * the counter may not be updated correctly on write
	 * so we need a second write operation to do the trick
	 * (the official workaround didn't work)
	 *
	 * the former idea is taken from OProfile code
	 */
	.perfctr_second_write	= 1,

	.format_attrs		= intel_p4_formats_attr,
};

__init int p4_pmu_init(void)
{
	unsigned int low, high;
	int i, reg;

	/* If we get stripped -- indexing fails */
	BUILD_BUG_ON(ARCH_P4_MAX_CCCR > INTEL_PMC_MAX_GENERIC);

	rdmsr(MSR_IA32_MISC_ENABLE, low, high);
	if (!(low & (1 << 7))) {
		pr_cont("unsupported Netburst CPU model %d ",
			boot_cpu_data.x86_model);
		return -ENODEV;
	}

	memcpy(hw_cache_event_ids, p4_hw_cache_event_ids,
		sizeof(hw_cache_event_ids));

	pr_cont("Netburst events, ");

	x86_pmu = p4_pmu;

	/*
	 * Even though the counters are configured to interrupt a particular
	 * logical processor when an overflow happens, testing has shown that
	 * on kdump kernels (which uses a single cpu), thread1's counter
	 * continues to run and will report an NMI on thread0.  Due to the
	 * overflow bug, this leads to a stream of unknown NMIs.
	 *
	 * Solve this by zero'ing out the registers to mimic a reset.
	 */
	for (i = 0; i < x86_pmu.num_counters; i++) {
		reg = x86_pmu_config_addr(i);
		wrmsrl_safe(reg, 0ULL);
	}

	return 0;
}
