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
EVENT(PM_BRU_CMPL,				0x4d05e)
EVENT(PM_BR_MPRED_CMPL,				0x400f6)

/* All L1 D cache load references counted at finish, gated by reject */
EVENT(PM_LD_REF_L1,				0x100fc)
/* Load Missed L1 */
EVENT(PM_LD_MISS_L1_FIN,			0x2c04e)
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
/* Run_cycles */
EVENT(PM_RUN_CYC,				0x600f4)
/* Instruction Dispatched */
EVENT(PM_INST_DISP,				0x200f2)
EVENT(PM_INST_DISP_ALT,				0x300f2)
/* Alternate Branch event code */
EVENT(PM_BR_CMPL_ALT,				0x10012)
