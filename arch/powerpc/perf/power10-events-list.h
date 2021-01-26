/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Performance counter support for POWER10 processors.
 *
 * Copyright 2020 Madhavan Srinivasan, IBM Corporation.
 * Copyright 2020 Athira Rajeev, IBM Corporation.
 */

/*
 * Power10 event codes.
 */
EVENT(PM_RUN_CYC,				0x600f4);
EVENT(PM_DISP_STALL_CYC,			0x100f8);
EVENT(PM_EXEC_STALL,				0x30008);
EVENT(PM_RUN_INST_CMPL,				0x500fa);
EVENT(PM_BR_CMPL,                               0x4d05e);
EVENT(PM_BR_MPRED_CMPL,                         0x400f6);

/* All L1 D cache load references counted at finish, gated by reject */
EVENT(PM_LD_REF_L1,				0x100fc);
/* Load Missed L1 */
EVENT(PM_LD_MISS_L1,				0x3e054);
/* Store Missed L1 */
EVENT(PM_ST_MISS_L1,				0x300f0);
/* L1 cache data prefetches */
EVENT(PM_LD_PREFETCH_CACHE_LINE_MISS,		0x1002c);
/* Demand iCache Miss */
EVENT(PM_L1_ICACHE_MISS,			0x200fc);
/* Instruction fetches from L1 */
EVENT(PM_INST_FROM_L1,				0x04080);
/* Instruction Demand sectors wriittent into IL1 */
EVENT(PM_INST_FROM_L1MISS,			0x03f00000001c040);
/* Instruction prefetch written into IL1 */
EVENT(PM_IC_PREF_REQ,				0x040a0);
/* The data cache was reloaded from local core's L3 due to a demand load */
EVENT(PM_DATA_FROM_L3,				0x01340000001c040);
/* Demand LD - L3 Miss (not L2 hit and not L3 hit) */
EVENT(PM_DATA_FROM_L3MISS,			0x300fe);
/* Data PTEG reload */
EVENT(PM_DTLB_MISS,				0x300fc);
/* ITLB Reloaded */
EVENT(PM_ITLB_MISS,				0x400fc);

EVENT(PM_RUN_CYC_ALT,				0x0001e);
EVENT(PM_RUN_INST_CMPL_ALT,			0x00002);

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

EVENT(MEM_LOADS,				0x34340401e0);
EVENT(MEM_STORES,				0x343c0401e0);
