// SPDX-License-Identifier: GPL-2.0
/*
 * Performance events support for SH-4A performance counters
 *
 *  Copyright (C) 2009, 2010  Paul Mundt
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/perf_event.h>
#include <asm/processor.h>

#define PPC_CCBR(idx)	(0xff200800 + (sizeof(u32) * idx))
#define PPC_PMCTR(idx)	(0xfc100000 + (sizeof(u32) * idx))

#define CCBR_CIT_MASK	(0x7ff << 6)
#define CCBR_DUC	(1 << 3)
#define CCBR_CMDS	(1 << 1)
#define CCBR_PPCE	(1 << 0)

#ifdef CONFIG_CPU_SHX3
/*
 * The PMCAT location for SH-X3 CPUs was quietly moved, while the CCBR
 * and PMCTR locations remains tentatively constant. This change remains
 * wholly undocumented, and was simply found through trial and error.
 *
 * Early cuts of SH-X3 still appear to use the SH-X/SH-X2 locations, and
 * it's unclear when this ceased to be the case. For now we always use
 * the new location (if future parts keep up with this trend then
 * scanning for them at runtime also remains a viable option.)
 *
 * The gap in the register space also suggests that there are other
 * undocumented counters, so this will need to be revisited at a later
 * point in time.
 */
#define PPC_PMCAT	0xfc100240
#else
#define PPC_PMCAT	0xfc100080
#endif

#define PMCAT_OVF3	(1 << 27)
#define PMCAT_CNN3	(1 << 26)
#define PMCAT_CLR3	(1 << 25)
#define PMCAT_OVF2	(1 << 19)
#define PMCAT_CLR2	(1 << 17)
#define PMCAT_OVF1	(1 << 11)
#define PMCAT_CNN1	(1 << 10)
#define PMCAT_CLR1	(1 << 9)
#define PMCAT_OVF0	(1 << 3)
#define PMCAT_CLR0	(1 << 1)

static struct sh_pmu sh4a_pmu;

/*
 * Supported raw event codes:
 *
 *	Event Code	Description
 *	----------	-----------
 *
 *	0x0000		number of elapsed cycles
 *	0x0200		number of elapsed cycles in privileged mode
 *	0x0280		number of elapsed cycles while SR.BL is asserted
 *	0x0202		instruction execution
 *	0x0203		instruction execution in parallel
 *	0x0204		number of unconditional branches
 *	0x0208		number of exceptions
 *	0x0209		number of interrupts
 *	0x0220		UTLB miss caused by instruction fetch
 *	0x0222		UTLB miss caused by operand access
 *	0x02a0		number of ITLB misses
 *	0x0028		number of accesses to instruction memories
 *	0x0029		number of accesses to instruction cache
 *	0x002a		instruction cache miss
 *	0x022e		number of access to instruction X/Y memory
 *	0x0030		number of reads to operand memories
 *	0x0038		number of writes to operand memories
 *	0x0031		number of operand cache read accesses
 *	0x0039		number of operand cache write accesses
 *	0x0032		operand cache read miss
 *	0x003a		operand cache write miss
 *	0x0236		number of reads to operand X/Y memory
 *	0x023e		number of writes to operand X/Y memory
 *	0x0237		number of reads to operand U memory
 *	0x023f		number of writes to operand U memory
 *	0x0337		number of U memory read buffer misses
 *	0x02b4		number of wait cycles due to operand read access
 *	0x02bc		number of wait cycles due to operand write access
 *	0x0033		number of wait cycles due to operand cache read miss
 *	0x003b		number of wait cycles due to operand cache write miss
 */

/*
 * Special reserved bits used by hardware emulators, read values will
 * vary, but writes must always be 0.
 */
#define PMCAT_EMU_CLR_MASK	((1 << 24) | (1 << 16) | (1 << 8) | (1 << 0))

static const int sh4a_general_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= 0x0000,
	[PERF_COUNT_HW_INSTRUCTIONS]		= 0x0202,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= 0x0029,	/* I-cache */
	[PERF_COUNT_HW_CACHE_MISSES]		= 0x002a,	/* I-cache */
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x0204,
	[PERF_COUNT_HW_BRANCH_MISSES]		= -1,
	[PERF_COUNT_HW_BUS_CYCLES]		= -1,
};

#define C(x)	PERF_COUNT_HW_CACHE_##x

static const int sh4a_cache_events
			[PERF_COUNT_HW_CACHE_MAX]
			[PERF_COUNT_HW_CACHE_OP_MAX]
			[PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
	[ C(L1D) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0x0031,
			[ C(RESULT_MISS)   ] = 0x0032,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = 0x0039,
			[ C(RESULT_MISS)   ] = 0x003a,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
		},
	},

	[ C(L1I) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0x0029,
			[ C(RESULT_MISS)   ] = 0x002a,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = -1,
			[ C(RESULT_MISS)   ] = -1,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
		},
	},

	[ C(LL) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0x0030,
			[ C(RESULT_MISS)   ] = 0,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = 0x0038,
			[ C(RESULT_MISS)   ] = 0,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
		},
	},

	[ C(DTLB) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0x0222,
			[ C(RESULT_MISS)   ] = 0x0220,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
		},
	},

	[ C(ITLB) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0x02a0,
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

	[ C(BPU) ] = {
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

static int sh4a_event_map(int event)
{
	return sh4a_general_events[event];
}

static u64 sh4a_pmu_read(int idx)
{
	return __raw_readl(PPC_PMCTR(idx));
}

static void sh4a_pmu_disable(struct hw_perf_event *hwc, int idx)
{
	unsigned int tmp;

	tmp = __raw_readl(PPC_CCBR(idx));
	tmp &= ~(CCBR_CIT_MASK | CCBR_DUC);
	__raw_writel(tmp, PPC_CCBR(idx));
}

static void sh4a_pmu_enable(struct hw_perf_event *hwc, int idx)
{
	unsigned int tmp;

	tmp = __raw_readl(PPC_PMCAT);
	tmp &= ~PMCAT_EMU_CLR_MASK;
	tmp |= idx ? PMCAT_CLR1 : PMCAT_CLR0;
	__raw_writel(tmp, PPC_PMCAT);

	tmp = __raw_readl(PPC_CCBR(idx));
	tmp |= (hwc->config << 6) | CCBR_CMDS | CCBR_PPCE;
	__raw_writel(tmp, PPC_CCBR(idx));

	__raw_writel(__raw_readl(PPC_CCBR(idx)) | CCBR_DUC, PPC_CCBR(idx));
}

static void sh4a_pmu_disable_all(void)
{
	int i;

	for (i = 0; i < sh4a_pmu.num_events; i++)
		__raw_writel(__raw_readl(PPC_CCBR(i)) & ~CCBR_DUC, PPC_CCBR(i));
}

static void sh4a_pmu_enable_all(void)
{
	int i;

	for (i = 0; i < sh4a_pmu.num_events; i++)
		__raw_writel(__raw_readl(PPC_CCBR(i)) | CCBR_DUC, PPC_CCBR(i));
}

static struct sh_pmu sh4a_pmu = {
	.name		= "sh4a",
	.num_events	= 2,
	.event_map	= sh4a_event_map,
	.max_events	= ARRAY_SIZE(sh4a_general_events),
	.raw_event_mask	= 0x3ff,
	.cache_events	= &sh4a_cache_events,
	.read		= sh4a_pmu_read,
	.disable	= sh4a_pmu_disable,
	.enable		= sh4a_pmu_enable,
	.disable_all	= sh4a_pmu_disable_all,
	.enable_all	= sh4a_pmu_enable_all,
};

static int __init sh4a_pmu_init(void)
{
	/*
	 * Make sure this CPU actually has perf counters.
	 */
	if (!(boot_cpu_data.flags & CPU_HAS_PERF_COUNTER)) {
		pr_notice("HW perf events unsupported, software events only.\n");
		return -ENODEV;
	}

	return register_sh_pmu(&sh4a_pmu);
}
early_initcall(sh4a_pmu_init);
