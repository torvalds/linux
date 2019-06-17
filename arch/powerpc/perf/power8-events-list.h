/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Performance counter support for POWER8 processors.
 *
 * Copyright 2014 Sukadev Bhattiprolu, IBM Corporation.
 */

/*
 * Power8 event codes.
 */
EVENT(PM_CYC,					0x0001e)
EVENT(PM_GCT_NOSLOT_CYC,			0x100f8)
EVENT(PM_CMPLU_STALL,				0x4000a)
EVENT(PM_INST_CMPL,				0x00002)
EVENT(PM_BRU_FIN,				0x10068)
EVENT(PM_BR_MPRED_CMPL,				0x400f6)

/* All L1 D cache load references counted at finish, gated by reject */
EVENT(PM_LD_REF_L1,				0x100ee)
/* Load Missed L1 */
EVENT(PM_LD_MISS_L1,				0x3e054)
/* Store Missed L1 */
EVENT(PM_ST_MISS_L1,				0x300f0)
/* L1 cache data prefetches */
EVENT(PM_L1_PREF,				0x0d8b8)
/* Instruction fetches from L1 */
EVENT(PM_INST_FROM_L1,				0x04080)
/* Demand iCache Miss */
EVENT(PM_L1_ICACHE_MISS,			0x200fd)
/* Instruction Demand sectors wriittent into IL1 */
EVENT(PM_L1_DEMAND_WRITE,			0x0408c)
/* Instruction prefetch written into IL1 */
EVENT(PM_IC_PREF_WRITE,				0x0408e)
/* The data cache was reloaded from local core's L3 due to a demand load */
EVENT(PM_DATA_FROM_L3,				0x4c042)
/* Demand LD - L3 Miss (not L2 hit and not L3 hit) */
EVENT(PM_DATA_FROM_L3MISS,			0x300fe)
/* All successful D-side store dispatches for this thread */
EVENT(PM_L2_ST,					0x17080)
/* All successful D-side store dispatches for this thread that were L2 Miss */
EVENT(PM_L2_ST_MISS,				0x17082)
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
/* Marked store completed */
EVENT(PM_MRK_ST_CMPL,				0x10134)
/* Alternate event code for Marked store completed */
EVENT(PM_MRK_ST_CMPL_ALT,			0x301e2)
/* Marked two path branch */
EVENT(PM_BR_MRK_2PATH,				0x10138)
/* Alternate event code for PM_BR_MRK_2PATH */
EVENT(PM_BR_MRK_2PATH_ALT,			0x40138)
/* L3 castouts in Mepf state */
EVENT(PM_L3_CO_MEPF,				0x18082)
/* Alternate event code for PM_L3_CO_MEPF */
EVENT(PM_L3_CO_MEPF_ALT,			0x3e05e)
/* Data cache was reloaded from a location other than L2 due to a marked load */
EVENT(PM_MRK_DATA_FROM_L2MISS,			0x1d14e)
/* Alternate event code for PM_MRK_DATA_FROM_L2MISS */
EVENT(PM_MRK_DATA_FROM_L2MISS_ALT,		0x401e8)
/* Alternate event code for  PM_CMPLU_STALL */
EVENT(PM_CMPLU_STALL_ALT,			0x1e054)
/* Two path branch */
EVENT(PM_BR_2PATH,				0x20036)
/* Alternate event code for PM_BR_2PATH */
EVENT(PM_BR_2PATH_ALT,				0x40036)
/* # PPC Dispatched */
EVENT(PM_INST_DISP,				0x200f2)
/* Alternate event code for PM_INST_DISP */
EVENT(PM_INST_DISP_ALT,				0x300f2)
/* Marked filter Match */
EVENT(PM_MRK_FILT_MATCH,			0x2013c)
/* Alternate event code for PM_MRK_FILT_MATCH */
EVENT(PM_MRK_FILT_MATCH_ALT,			0x3012e)
/* Alternate event code for PM_LD_MISS_L1 */
EVENT(PM_LD_MISS_L1_ALT,			0x400f0)
/*
 * Memory Access Event -- mem_access
 * Primary PMU event used here is PM_MRK_INST_CMPL, along with
 * Random Load/Store Facility Sampling (RIS) in Random sampling mode (MMCRA[SM]).
 */
EVENT(MEM_ACCESS,				0x10401e0)
