/*
 * Performance counter support for POWER9 processors.
 *
 * Copyright 2016 Madhavan Srinivasan, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * Power9 event codes.
 */
EVENT(PM_CYC,					0x0001e)
EVENT(PM_ICT_NOSLOT_CYC,			0x100f8)
EVENT(PM_CMPLU_STALL,				0x1e054)
EVENT(PM_INST_CMPL,				0x00002)
EVENT(PM_BR_CMPL,				0x4d05e)
EVENT(PM_BR_MPRED_CMPL,				0x400f6)

/* All L1 D cache load references counted at finish, gated by reject */
EVENT(PM_LD_REF_L1,				0x100fc)
/* Load Missed L1 */
EVENT(PM_LD_MISS_L1_FIN,			0x2c04e)
EVENT(PM_LD_MISS_L1,				0x3e054)
/* Alternate event code for PM_LD_MISS_L1 */
EVENT(PM_LD_MISS_L1_ALT,			0x400f0)
/* Store Missed L1 */
EVENT(PM_ST_MISS_L1,				0x300f0)
/* L1 cache data prefetches */
EVENT(PM_L1_PREF,				0x20054)
/* Instruction fetches from L1 */
EVENT(PM_INST_FROM_L1,				0x04080)
/* Demand iCache Miss */
EVENT(PM_L1_ICACHE_MISS,			0x200fd)
/* Instruction Demand sectors wriittent into IL1 */
EVENT(PM_L1_DEMAND_WRITE,			0x0408c)
/* Instruction prefetch written into IL1 */
EVENT(PM_IC_PREF_WRITE,				0x0488c)
/* The data cache was reloaded from local core's L3 due to a demand load */
EVENT(PM_DATA_FROM_L3,				0x4c042)
/* Demand LD - L3 Miss (not L2 hit and not L3 hit) */
EVENT(PM_DATA_FROM_L3MISS,			0x300fe)
/* All successful D-side store dispatches for this thread */
EVENT(PM_L2_ST,					0x16880)
/* All successful D-side store dispatches for this thread that were L2 Miss */
EVENT(PM_L2_ST_MISS,				0x26880)
/* Total HW L3 prefetches(Load+store) */
EVENT(PM_L3_PREF_ALL,				0x4e052)
/* Data PTEG reload */
EVENT(PM_DTLB_MISS,				0x300fc)
/* ITLB Reloaded */
EVENT(PM_ITLB_MISS,				0x400fc)
/* Run_Instructions */
EVENT(PM_RUN_INST_CMPL,				0x500fa)
/* Alternate event code for PM_RUN_INST_CMPL */
EVENT(PM_RUN_INST_CMPL_ALT,			0x400fa)
/* Run_cycles */
EVENT(PM_RUN_CYC,				0x600f4)
/* Alternate event code for Run_cycles */
EVENT(PM_RUN_CYC_ALT,				0x200f4)
/* Instruction Dispatched */
EVENT(PM_INST_DISP,				0x200f2)
EVENT(PM_INST_DISP_ALT,				0x300f2)
/* Branch event that are not strongly biased */
EVENT(PM_BR_2PATH,				0x20036)
/* ALternate branch event that are not strongly biased */
EVENT(PM_BR_2PATH_ALT,				0x40036)

/* Blacklisted events */
EVENT(PM_MRK_ST_DONE_L2,			0x10134)
EVENT(PM_RADIX_PWC_L1_HIT,			0x1f056)
EVENT(PM_FLOP_CMPL,				0x100f4)
EVENT(PM_MRK_NTF_FIN,				0x20112)
EVENT(PM_RADIX_PWC_L2_HIT,			0x2d024)
EVENT(PM_IFETCH_THROTTLE,			0x3405e)
EVENT(PM_MRK_L2_TM_ST_ABORT_SISTER,		0x3e15c)
EVENT(PM_RADIX_PWC_L3_HIT,			0x3f056)
EVENT(PM_RUN_CYC_SMT2_MODE,			0x3006c)
EVENT(PM_TM_TX_PASS_RUN_INST,			0x4e014)
EVENT(PM_DISP_HELD_SYNC_HOLD,			0x4003c)
EVENT(PM_DTLB_MISS_16G,				0x1c058)
EVENT(PM_DERAT_MISS_2M,				0x1c05a)
EVENT(PM_DTLB_MISS_2M,				0x1c05c)
EVENT(PM_MRK_DTLB_MISS_1G,			0x1d15c)
EVENT(PM_DTLB_MISS_4K,				0x2c056)
EVENT(PM_DERAT_MISS_1G,				0x2c05a)
EVENT(PM_MRK_DERAT_MISS_2M,			0x2d152)
EVENT(PM_MRK_DTLB_MISS_4K,			0x2d156)
EVENT(PM_MRK_DTLB_MISS_16G,			0x2d15e)
EVENT(PM_DTLB_MISS_64K,				0x3c056)
EVENT(PM_MRK_DERAT_MISS_1G,			0x3d152)
EVENT(PM_MRK_DTLB_MISS_64K,			0x3d156)
EVENT(PM_DTLB_MISS_16M,				0x4c056)
EVENT(PM_DTLB_MISS_1G,				0x4c05a)
EVENT(PM_MRK_DTLB_MISS_16M,			0x4c15e)

/*
 * Memory Access Events
 *
 * Primary PMU event used here is PM_MRK_INST_CMPL (0x401e0)
 * To enable capturing of memory profiling, these MMCRA bits
 * needs to be programmed and corresponding raw event format
 * encoding.
 *
 * MMCRA bits encoding needed are
 *     SM (Sampling Mode)
 *     EM (Eligibility for Random Sampling)
 *     TECE (Threshold Event Counter Event)
 *     TS (Threshold Start Event)
 *     TE (Threshold End Event)
 *
 * Corresponding Raw Encoding bits:
 *     sample [EM,SM]
 *     thresh_sel (TECE)
 *     thresh start (TS)
 *     thresh end (TE)
 */
EVENT(MEM_LOADS,				0x34340401e0)
EVENT(MEM_STORES,				0x343c0401e0)
