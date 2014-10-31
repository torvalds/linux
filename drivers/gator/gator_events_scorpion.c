/**
 * Copyright (C) ARM Limited 2011-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "gator.h"

/* gator_events_perf_pmu.c is used if perf is supported */
#if GATOR_NO_PERF_SUPPORT

static const char *pmnc_name;
static int pmnc_counters;

/* Per-CPU PMNC: config reg */
#define PMNC_E		(1 << 0)	/* Enable all counters */
#define PMNC_P		(1 << 1)	/* Reset all counters */
#define PMNC_C		(1 << 2)	/* Cycle counter reset */
#define PMNC_D		(1 << 3)	/* CCNT counts every 64th cpu cycle */
#define PMNC_X		(1 << 4)	/* Export to ETM */
#define PMNC_DP		(1 << 5)	/* Disable CCNT if non-invasive debug */
#define	PMNC_MASK	0x3f	/* Mask for writable bits */

/* ccnt reg */
#define CCNT_REG	(1 << 31)

#define CCNT		0
#define CNT0		1
#define CNTMAX		(4+1)

static unsigned long pmnc_enabled[CNTMAX];
static unsigned long pmnc_event[CNTMAX];
static unsigned long pmnc_key[CNTMAX];

static DEFINE_PER_CPU(int[CNTMAX * 2], perfCnt);

enum scorpion_perf_types {
	SCORPION_ICACHE_EXPL_INV = 0x4c,
	SCORPION_ICACHE_MISS = 0x4d,
	SCORPION_ICACHE_ACCESS = 0x4e,
	SCORPION_ICACHE_CACHEREQ_L2 = 0x4f,
	SCORPION_ICACHE_NOCACHE_L2 = 0x50,
	SCORPION_HIQUP_NOPED = 0x51,
	SCORPION_DATA_ABORT = 0x52,
	SCORPION_IRQ = 0x53,
	SCORPION_FIQ = 0x54,
	SCORPION_ALL_EXCPT = 0x55,
	SCORPION_UNDEF = 0x56,
	SCORPION_SVC = 0x57,
	SCORPION_SMC = 0x58,
	SCORPION_PREFETCH_ABORT = 0x59,
	SCORPION_INDEX_CHECK = 0x5a,
	SCORPION_NULL_CHECK = 0x5b,
	SCORPION_EXPL_ICIALLU = 0x5c,
	SCORPION_IMPL_ICIALLU = 0x5d,
	SCORPION_NONICIALLU_BTAC_INV = 0x5e,
	SCORPION_ICIMVAU_IMPL_ICIALLU = 0x5f,
	SCORPION_SPIPE_ONLY_CYCLES = 0x60,
	SCORPION_XPIPE_ONLY_CYCLES = 0x61,
	SCORPION_DUAL_CYCLES = 0x62,
	SCORPION_DISPATCH_ANY_CYCLES = 0x63,
	SCORPION_FIFO_FULLBLK_CMT = 0x64,
	SCORPION_FAIL_COND_INST = 0x65,
	SCORPION_PASS_COND_INST = 0x66,
	SCORPION_ALLOW_VU_CLK = 0x67,
	SCORPION_VU_IDLE = 0x68,
	SCORPION_ALLOW_L2_CLK = 0x69,
	SCORPION_L2_IDLE = 0x6a,
	SCORPION_DTLB_IMPL_INV_SCTLR_DACR = 0x6b,
	SCORPION_DTLB_EXPL_INV = 0x6c,
	SCORPION_DTLB_MISS = 0x6d,
	SCORPION_DTLB_ACCESS = 0x6e,
	SCORPION_ITLB_MISS = 0x6f,
	SCORPION_ITLB_IMPL_INV = 0x70,
	SCORPION_ITLB_EXPL_INV = 0x71,
	SCORPION_UTLB_D_MISS = 0x72,
	SCORPION_UTLB_D_ACCESS = 0x73,
	SCORPION_UTLB_I_MISS = 0x74,
	SCORPION_UTLB_I_ACCESS = 0x75,
	SCORPION_UTLB_INV_ASID = 0x76,
	SCORPION_UTLB_INV_MVA = 0x77,
	SCORPION_UTLB_INV_ALL = 0x78,
	SCORPION_S2_HOLD_RDQ_UNAVAIL = 0x79,
	SCORPION_S2_HOLD = 0x7a,
	SCORPION_S2_HOLD_DEV_OP = 0x7b,
	SCORPION_S2_HOLD_ORDER = 0x7c,
	SCORPION_S2_HOLD_BARRIER = 0x7d,
	SCORPION_VIU_DUAL_CYCLE = 0x7e,
	SCORPION_VIU_SINGLE_CYCLE = 0x7f,
	SCORPION_VX_PIPE_WAR_STALL_CYCLES = 0x80,
	SCORPION_VX_PIPE_WAW_STALL_CYCLES = 0x81,
	SCORPION_VX_PIPE_RAW_STALL_CYCLES = 0x82,
	SCORPION_VX_PIPE_LOAD_USE_STALL = 0x83,
	SCORPION_VS_PIPE_WAR_STALL_CYCLES = 0x84,
	SCORPION_VS_PIPE_WAW_STALL_CYCLES = 0x85,
	SCORPION_VS_PIPE_RAW_STALL_CYCLES = 0x86,
	SCORPION_EXCEPTIONS_INV_OPERATION = 0x87,
	SCORPION_EXCEPTIONS_DIV_BY_ZERO = 0x88,
	SCORPION_COND_INST_FAIL_VX_PIPE = 0x89,
	SCORPION_COND_INST_FAIL_VS_PIPE = 0x8a,
	SCORPION_EXCEPTIONS_OVERFLOW = 0x8b,
	SCORPION_EXCEPTIONS_UNDERFLOW = 0x8c,
	SCORPION_EXCEPTIONS_DENORM = 0x8d,
#ifdef CONFIG_ARCH_MSM_SCORPIONMP
	SCORPIONMP_NUM_BARRIERS = 0x8e,
	SCORPIONMP_BARRIER_CYCLES = 0x8f,
#else
	SCORPION_BANK_AB_HIT = 0x8e,
	SCORPION_BANK_AB_ACCESS = 0x8f,
	SCORPION_BANK_CD_HIT = 0x90,
	SCORPION_BANK_CD_ACCESS = 0x91,
	SCORPION_BANK_AB_DSIDE_HIT = 0x92,
	SCORPION_BANK_AB_DSIDE_ACCESS = 0x93,
	SCORPION_BANK_CD_DSIDE_HIT = 0x94,
	SCORPION_BANK_CD_DSIDE_ACCESS = 0x95,
	SCORPION_BANK_AB_ISIDE_HIT = 0x96,
	SCORPION_BANK_AB_ISIDE_ACCESS = 0x97,
	SCORPION_BANK_CD_ISIDE_HIT = 0x98,
	SCORPION_BANK_CD_ISIDE_ACCESS = 0x99,
	SCORPION_ISIDE_RD_WAIT = 0x9a,
	SCORPION_DSIDE_RD_WAIT = 0x9b,
	SCORPION_BANK_BYPASS_WRITE = 0x9c,
	SCORPION_BANK_AB_NON_CASTOUT = 0x9d,
	SCORPION_BANK_AB_L2_CASTOUT = 0x9e,
	SCORPION_BANK_CD_NON_CASTOUT = 0x9f,
	SCORPION_BANK_CD_L2_CASTOUT = 0xa0,
#endif
	MSM_MAX_EVT
};

struct scorp_evt {
	u32 evt_type;
	u32 val;
	u8 grp;
	u32 evt_type_act;
};

static const struct scorp_evt sc_evt[] = {
	{SCORPION_ICACHE_EXPL_INV, 0x80000500, 0, 0x4d},
	{SCORPION_ICACHE_MISS, 0x80050000, 0, 0x4e},
	{SCORPION_ICACHE_ACCESS, 0x85000000, 0, 0x4f},
	{SCORPION_ICACHE_CACHEREQ_L2, 0x86000000, 0, 0x4f},
	{SCORPION_ICACHE_NOCACHE_L2, 0x87000000, 0, 0x4f},
	{SCORPION_HIQUP_NOPED, 0x80080000, 0, 0x4e},
	{SCORPION_DATA_ABORT, 0x8000000a, 0, 0x4c},
	{SCORPION_IRQ, 0x80000a00, 0, 0x4d},
	{SCORPION_FIQ, 0x800a0000, 0, 0x4e},
	{SCORPION_ALL_EXCPT, 0x8a000000, 0, 0x4f},
	{SCORPION_UNDEF, 0x8000000b, 0, 0x4c},
	{SCORPION_SVC, 0x80000b00, 0, 0x4d},
	{SCORPION_SMC, 0x800b0000, 0, 0x4e},
	{SCORPION_PREFETCH_ABORT, 0x8b000000, 0, 0x4f},
	{SCORPION_INDEX_CHECK, 0x8000000c, 0, 0x4c},
	{SCORPION_NULL_CHECK, 0x80000c00, 0, 0x4d},
	{SCORPION_EXPL_ICIALLU, 0x8000000d, 0, 0x4c},
	{SCORPION_IMPL_ICIALLU, 0x80000d00, 0, 0x4d},
	{SCORPION_NONICIALLU_BTAC_INV, 0x800d0000, 0, 0x4e},
	{SCORPION_ICIMVAU_IMPL_ICIALLU, 0x8d000000, 0, 0x4f},

	{SCORPION_SPIPE_ONLY_CYCLES, 0x80000600, 1, 0x51},
	{SCORPION_XPIPE_ONLY_CYCLES, 0x80060000, 1, 0x52},
	{SCORPION_DUAL_CYCLES, 0x86000000, 1, 0x53},
	{SCORPION_DISPATCH_ANY_CYCLES, 0x89000000, 1, 0x53},
	{SCORPION_FIFO_FULLBLK_CMT, 0x8000000d, 1, 0x50},
	{SCORPION_FAIL_COND_INST, 0x800d0000, 1, 0x52},
	{SCORPION_PASS_COND_INST, 0x8d000000, 1, 0x53},
	{SCORPION_ALLOW_VU_CLK, 0x8000000e, 1, 0x50},
	{SCORPION_VU_IDLE, 0x80000e00, 1, 0x51},
	{SCORPION_ALLOW_L2_CLK, 0x800e0000, 1, 0x52},
	{SCORPION_L2_IDLE, 0x8e000000, 1, 0x53},

	{SCORPION_DTLB_IMPL_INV_SCTLR_DACR, 0x80000001, 2, 0x54},
	{SCORPION_DTLB_EXPL_INV, 0x80000100, 2, 0x55},
	{SCORPION_DTLB_MISS, 0x80010000, 2, 0x56},
	{SCORPION_DTLB_ACCESS, 0x81000000, 2, 0x57},
	{SCORPION_ITLB_MISS, 0x80000200, 2, 0x55},
	{SCORPION_ITLB_IMPL_INV, 0x80020000, 2, 0x56},
	{SCORPION_ITLB_EXPL_INV, 0x82000000, 2, 0x57},
	{SCORPION_UTLB_D_MISS, 0x80000003, 2, 0x54},
	{SCORPION_UTLB_D_ACCESS, 0x80000300, 2, 0x55},
	{SCORPION_UTLB_I_MISS, 0x80030000, 2, 0x56},
	{SCORPION_UTLB_I_ACCESS, 0x83000000, 2, 0x57},
	{SCORPION_UTLB_INV_ASID, 0x80000400, 2, 0x55},
	{SCORPION_UTLB_INV_MVA, 0x80040000, 2, 0x56},
	{SCORPION_UTLB_INV_ALL, 0x84000000, 2, 0x57},
	{SCORPION_S2_HOLD_RDQ_UNAVAIL, 0x80000800, 2, 0x55},
	{SCORPION_S2_HOLD, 0x88000000, 2, 0x57},
	{SCORPION_S2_HOLD_DEV_OP, 0x80000900, 2, 0x55},
	{SCORPION_S2_HOLD_ORDER, 0x80090000, 2, 0x56},
	{SCORPION_S2_HOLD_BARRIER, 0x89000000, 2, 0x57},

	{SCORPION_VIU_DUAL_CYCLE, 0x80000001, 4, 0x5c},
	{SCORPION_VIU_SINGLE_CYCLE, 0x80000100, 4, 0x5d},
	{SCORPION_VX_PIPE_WAR_STALL_CYCLES, 0x80000005, 4, 0x5c},
	{SCORPION_VX_PIPE_WAW_STALL_CYCLES, 0x80000500, 4, 0x5d},
	{SCORPION_VX_PIPE_RAW_STALL_CYCLES, 0x80050000, 4, 0x5e},
	{SCORPION_VX_PIPE_LOAD_USE_STALL, 0x80000007, 4, 0x5c},
	{SCORPION_VS_PIPE_WAR_STALL_CYCLES, 0x80000008, 4, 0x5c},
	{SCORPION_VS_PIPE_WAW_STALL_CYCLES, 0x80000800, 4, 0x5d},
	{SCORPION_VS_PIPE_RAW_STALL_CYCLES, 0x80080000, 4, 0x5e},
	{SCORPION_EXCEPTIONS_INV_OPERATION, 0x8000000b, 4, 0x5c},
	{SCORPION_EXCEPTIONS_DIV_BY_ZERO, 0x80000b00, 4, 0x5d},
	{SCORPION_COND_INST_FAIL_VX_PIPE, 0x800b0000, 4, 0x5e},
	{SCORPION_COND_INST_FAIL_VS_PIPE, 0x8b000000, 4, 0x5f},
	{SCORPION_EXCEPTIONS_OVERFLOW, 0x8000000c, 4, 0x5c},
	{SCORPION_EXCEPTIONS_UNDERFLOW, 0x80000c00, 4, 0x5d},
	{SCORPION_EXCEPTIONS_DENORM, 0x8c000000, 4, 0x5f},

#ifdef CONFIG_ARCH_MSM_SCORPIONMP
	{SCORPIONMP_NUM_BARRIERS, 0x80000e00, 3, 0x59},
	{SCORPIONMP_BARRIER_CYCLES, 0x800e0000, 3, 0x5a},
#else
	{SCORPION_BANK_AB_HIT, 0x80000001, 3, 0x58},
	{SCORPION_BANK_AB_ACCESS, 0x80000100, 3, 0x59},
	{SCORPION_BANK_CD_HIT, 0x80010000, 3, 0x5a},
	{SCORPION_BANK_CD_ACCESS, 0x81000000, 3, 0x5b},
	{SCORPION_BANK_AB_DSIDE_HIT, 0x80000002, 3, 0x58},
	{SCORPION_BANK_AB_DSIDE_ACCESS, 0x80000200, 3, 0x59},
	{SCORPION_BANK_CD_DSIDE_HIT, 0x80020000, 3, 0x5a},
	{SCORPION_BANK_CD_DSIDE_ACCESS, 0x82000000, 3, 0x5b},
	{SCORPION_BANK_AB_ISIDE_HIT, 0x80000003, 3, 0x58},
	{SCORPION_BANK_AB_ISIDE_ACCESS, 0x80000300, 3, 0x59},
	{SCORPION_BANK_CD_ISIDE_HIT, 0x80030000, 3, 0x5a},
	{SCORPION_BANK_CD_ISIDE_ACCESS, 0x83000000, 3, 0x5b},
	{SCORPION_ISIDE_RD_WAIT, 0x80000009, 3, 0x58},
	{SCORPION_DSIDE_RD_WAIT, 0x80090000, 3, 0x5a},
	{SCORPION_BANK_BYPASS_WRITE, 0x8000000a, 3, 0x58},
	{SCORPION_BANK_AB_NON_CASTOUT, 0x8000000c, 3, 0x58},
	{SCORPION_BANK_AB_L2_CASTOUT, 0x80000c00, 3, 0x59},
	{SCORPION_BANK_CD_NON_CASTOUT, 0x800c0000, 3, 0x5a},
	{SCORPION_BANK_CD_L2_CASTOUT, 0x8c000000, 3, 0x5b},
#endif
};

static inline void scorpion_pmnc_write(u32 val)
{
	val &= PMNC_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 0" : : "r" (val));
}

static inline u32 scorpion_pmnc_read(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (val));
	return val;
}

static inline u32 scorpion_ccnt_read(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));
	return val;
}

static inline u32 scorpion_cntn_read(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (val));
	return val;
}

static inline u32 scorpion_pmnc_enable_counter(unsigned int cnt)
{
	u32 val;

	if (cnt >= CNTMAX) {
		pr_err("gator: CPU%u enabling wrong PMNC counter %d\n", smp_processor_id(), cnt);
		return -1;
	}

	if (cnt == CCNT)
		val = CCNT_REG;
	else
		val = (1 << (cnt - CNT0));

	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (val));

	return cnt;
}

static inline u32 scorpion_pmnc_disable_counter(unsigned int cnt)
{
	u32 val;

	if (cnt >= CNTMAX) {
		pr_err("gator: CPU%u disabling wrong PMNC counter %d\n", smp_processor_id(), cnt);
		return -1;
	}

	if (cnt == CCNT)
		val = CCNT_REG;
	else
		val = (1 << (cnt - CNT0));

	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (val));

	return cnt;
}

static inline int scorpion_pmnc_select_counter(unsigned int cnt)
{
	u32 val;

	if ((cnt == CCNT) || (cnt >= CNTMAX)) {
		pr_err("gator: CPU%u selecting wrong PMNC counter %d\n", smp_processor_id(), cnt);
		return -1;
	}

	val = (cnt - CNT0);
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (val));

	return cnt;
}

static u32 scorpion_read_lpm0(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c15, c0, 0" : "=r" (val));
	return val;
}

static void scorpion_write_lpm0(u32 val)
{
	asm volatile("mcr p15, 0, %0, c15, c0, 0" : : "r" (val));
}

static u32 scorpion_read_lpm1(void)
{
	u32 val;

	asm volatile("mrc p15, 1, %0, c15, c0, 0" : "=r" (val));
	return val;
}

static void scorpion_write_lpm1(u32 val)
{
	asm volatile("mcr p15, 1, %0, c15, c0, 0" : : "r" (val));
}

static u32 scorpion_read_lpm2(void)
{
	u32 val;

	asm volatile("mrc p15, 2, %0, c15, c0, 0" : "=r" (val));
	return val;
}

static void scorpion_write_lpm2(u32 val)
{
	asm volatile("mcr p15, 2, %0, c15, c0, 0" : : "r" (val));
}

static u32 scorpion_read_l2lpm(void)
{
	u32 val;

	asm volatile("mrc p15, 3, %0, c15, c2, 0" : "=r" (val));
	return val;
}

static void scorpion_write_l2lpm(u32 val)
{
	asm volatile("mcr p15, 3, %0, c15, c2, 0" : : "r" (val));
}

static u32 scorpion_read_vlpm(void)
{
	u32 val;

	asm volatile("mrc p10, 7, %0, c11, c0, 0" : "=r" (val));
	return val;
}

static void scorpion_write_vlpm(u32 val)
{
	asm volatile("mcr p10, 7, %0, c11, c0, 0" : : "r" (val));
}

struct scorpion_access_funcs {
	u32 (*read)(void);
	void (*write)(u32);
};

struct scorpion_access_funcs scor_func[] = {
	{scorpion_read_lpm0, scorpion_write_lpm0},
	{scorpion_read_lpm1, scorpion_write_lpm1},
	{scorpion_read_lpm2, scorpion_write_lpm2},
	{scorpion_read_l2lpm, scorpion_write_l2lpm},
	{scorpion_read_vlpm, scorpion_write_vlpm},
};

u32 venum_orig_val;
u32 fp_orig_val;

static void scorpion_pre_vlpm(void)
{
	u32 venum_new_val;
	u32 fp_new_val;

	/* CPACR Enable CP10 access */
	asm volatile("mrc p15, 0, %0, c1, c0, 2" : "=r" (venum_orig_val));
	venum_new_val = venum_orig_val | 0x00300000;
	asm volatile("mcr p15, 0, %0, c1, c0, 2" : : "r" (venum_new_val));
	/* Enable FPEXC */
	asm volatile("mrc p10, 7, %0, c8, c0, 0" : "=r" (fp_orig_val));
	fp_new_val = fp_orig_val | 0x40000000;
	asm volatile("mcr p10, 7, %0, c8, c0, 0" : : "r" (fp_new_val));
}

static void scorpion_post_vlpm(void)
{
	/* Restore FPEXC */
	asm volatile("mcr p10, 7, %0, c8, c0, 0" : : "r" (fp_orig_val));
	/* Restore CPACR */
	asm volatile("mcr p15, 0, %0, c1, c0, 2" : : "r" (venum_orig_val));
}

#define COLMN0MASK 0x000000ff
#define COLMN1MASK 0x0000ff00
#define COLMN2MASK 0x00ff0000
static u32 scorpion_get_columnmask(u32 setval)
{
	if (setval & COLMN0MASK)
		return 0xffffff00;
	if (setval & COLMN1MASK)
		return 0xffff00ff;
	if (setval & COLMN2MASK)
		return 0xff00ffff;
	return 0x80ffffff;
}

static void scorpion_evt_setup(u32 gr, u32 setval)
{
	u32 val;

	if (gr == 4)
		scorpion_pre_vlpm();
	val = scorpion_get_columnmask(setval) & scor_func[gr].read();
	val = val | setval;
	scor_func[gr].write(val);
	if (gr == 4)
		scorpion_post_vlpm();
}

static int get_scorpion_evtinfo(unsigned int evt_type, struct scorp_evt *evtinfo)
{
	u32 idx;

	if ((evt_type < 0x4c) || (evt_type >= MSM_MAX_EVT))
		return 0;
	idx = evt_type - 0x4c;
	if (sc_evt[idx].evt_type == evt_type) {
		evtinfo->val = sc_evt[idx].val;
		evtinfo->grp = sc_evt[idx].grp;
		evtinfo->evt_type_act = sc_evt[idx].evt_type_act;
		return 1;
	}
	return 0;
}

static inline void scorpion_pmnc_write_evtsel(unsigned int cnt, u32 val)
{
	if (scorpion_pmnc_select_counter(cnt) == cnt) {
		if (val < 0x40) {
			asm volatile("mcr p15, 0, %0, c9, c13, 1" : : "r" (val));
		} else {
			u32 zero = 0;
			struct scorp_evt evtinfo;
			/* extract evtinfo.grp and evtinfo.tevt_type_act from val */
			if (get_scorpion_evtinfo(val, &evtinfo) == 0)
				return;
			asm volatile("mcr p15, 0, %0, c9, c13, 1" : : "r" (evtinfo.evt_type_act));
			asm volatile("mcr p15, 0, %0, c9, c15, 0" : : "r" (zero));
			scorpion_evt_setup(evtinfo.grp, val);
		}
	}
}

static void scorpion_pmnc_reset_counter(unsigned int cnt)
{
	u32 val = 0;

	if (cnt == CCNT) {
		scorpion_pmnc_disable_counter(cnt);

		asm volatile("mcr p15, 0, %0, c9, c13, 0" : : "r" (val));

		if (pmnc_enabled[cnt] != 0)
			scorpion_pmnc_enable_counter(cnt);

	} else if (cnt >= CNTMAX) {
		pr_err("gator: CPU%u resetting wrong PMNC counter %d\n", smp_processor_id(), cnt);
	} else {
		scorpion_pmnc_disable_counter(cnt);

		if (scorpion_pmnc_select_counter(cnt) == cnt)
			asm volatile("mcr p15, 0, %0, c9, c13, 2" : : "r" (val));

		if (pmnc_enabled[cnt] != 0)
			scorpion_pmnc_enable_counter(cnt);
	}
}

static int gator_events_scorpion_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	int i;

	for (i = 0; i < pmnc_counters; i++) {
		char buf[40];

		if (i == 0)
			snprintf(buf, sizeof(buf), "%s_ccnt", pmnc_name);
		else
			snprintf(buf, sizeof(buf), "%s_cnt%d", pmnc_name, i - 1);
		dir = gatorfs_mkdir(sb, root, buf);
		if (!dir)
			return -1;
		gatorfs_create_ulong(sb, dir, "enabled", &pmnc_enabled[i]);
		gatorfs_create_ro_ulong(sb, dir, "key", &pmnc_key[i]);
		if (i > 0)
			gatorfs_create_ulong(sb, dir, "event", &pmnc_event[i]);
	}

	return 0;
}

static int gator_events_scorpion_online(int **buffer, bool migrate)
{
	unsigned int cnt, len = 0, cpu = smp_processor_id();

	if (scorpion_pmnc_read() & PMNC_E)
		scorpion_pmnc_write(scorpion_pmnc_read() & ~PMNC_E);

	/* Initialize & Reset PMNC: C bit and P bit */
	scorpion_pmnc_write(PMNC_P | PMNC_C);

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		unsigned long event;

		if (!pmnc_enabled[cnt])
			continue;

		/* disable counter */
		scorpion_pmnc_disable_counter(cnt);

		event = pmnc_event[cnt] & 255;

		/* Set event (if destined for PMNx counters), We don't need to set the event if it's a cycle count */
		if (cnt != CCNT)
			scorpion_pmnc_write_evtsel(cnt, event);

		/* reset counter */
		scorpion_pmnc_reset_counter(cnt);

		/* Enable counter, do not enable interrupt for this counter */
		scorpion_pmnc_enable_counter(cnt);
	}

	/* enable */
	scorpion_pmnc_write(scorpion_pmnc_read() | PMNC_E);

	/* read the counters and toss the invalid data, return zero instead */
	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		if (pmnc_enabled[cnt]) {
			if (cnt == CCNT)
				scorpion_ccnt_read();
			else if (scorpion_pmnc_select_counter(cnt) == cnt)
				scorpion_cntn_read();
			scorpion_pmnc_reset_counter(cnt);

			per_cpu(perfCnt, cpu)[len++] = pmnc_key[cnt];
			per_cpu(perfCnt, cpu)[len++] = 0;
		}
	}

	if (buffer)
		*buffer = per_cpu(perfCnt, cpu);

	return len;
}

static int gator_events_scorpion_offline(int **buffer, bool migrate)
{
	scorpion_pmnc_write(scorpion_pmnc_read() & ~PMNC_E);
	return 0;
}

static void gator_events_scorpion_stop(void)
{
	unsigned int cnt;

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
	}
}

static int gator_events_scorpion_read(int **buffer, bool sched_switch)
{
	int cnt, len = 0;
	int cpu = smp_processor_id();

	/* a context switch may occur before the online hotplug event, thus need to check that the pmu is enabled */
	if (!(scorpion_pmnc_read() & PMNC_E))
		return 0;

	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		if (pmnc_enabled[cnt]) {
			int value;

			if (cnt == CCNT)
				value = scorpion_ccnt_read();
			else if (scorpion_pmnc_select_counter(cnt) == cnt)
				value = scorpion_cntn_read();
			else
				value = 0;
			scorpion_pmnc_reset_counter(cnt);

			per_cpu(perfCnt, cpu)[len++] = pmnc_key[cnt];
			per_cpu(perfCnt, cpu)[len++] = value;
		}
	}

	if (buffer)
		*buffer = per_cpu(perfCnt, cpu);

	return len;
}

static struct gator_interface gator_events_scorpion_interface = {
	.create_files = gator_events_scorpion_create_files,
	.stop = gator_events_scorpion_stop,
	.online = gator_events_scorpion_online,
	.offline = gator_events_scorpion_offline,
	.read = gator_events_scorpion_read,
};

int gator_events_scorpion_init(void)
{
	unsigned int cnt;

	switch (gator_cpuid()) {
	case SCORPION:
		pmnc_name = "Scorpion";
		pmnc_counters = 4;
		break;
	case SCORPIONMP:
		pmnc_name = "ScorpionMP";
		pmnc_counters = 4;
		break;
	default:
		return -1;
	}

	/* CNT[n] + CCNT */
	pmnc_counters++;

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
		pmnc_key[cnt] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_scorpion_interface);
}

#endif
