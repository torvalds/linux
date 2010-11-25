/*
 * Performance events support for SH7750-style performance counters
 *
 *  Copyright (C) 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/perf_event.h>
#include <asm/processor.h>

#define PM_CR_BASE	0xff000084	/* 16-bit */
#define PM_CTR_BASE	0xff100004	/* 32-bit */

#define PMCR(n)		(PM_CR_BASE + ((n) * 0x04))
#define PMCTRH(n)	(PM_CTR_BASE + 0x00 + ((n) * 0x08))
#define PMCTRL(n)	(PM_CTR_BASE + 0x04 + ((n) * 0x08))

#define PMCR_PMM_MASK	0x0000003f

#define PMCR_CLKF	0x00000100
#define PMCR_PMCLR	0x00002000
#define PMCR_PMST	0x00004000
#define PMCR_PMEN	0x00008000

static struct sh_pmu sh7750_pmu;

/*
 * There are a number of events supported by each counter (33 in total).
 * Since we have 2 counters, each counter will take the event code as it
 * corresponds to the PMCR PMM setting. Each counter can be configured
 * independently.
 *
 *	Event Code	Description
 *	----------	-----------
 *
 *	0x01		Operand read access
 *	0x02		Operand write access
 *	0x03		UTLB miss
 *	0x04		Operand cache read miss
 *	0x05		Operand cache write miss
 *	0x06		Instruction fetch (w/ cache)
 *	0x07		Instruction TLB miss
 *	0x08		Instruction cache miss
 *	0x09		All operand accesses
 *	0x0a		All instruction accesses
 *	0x0b		OC RAM operand access
 *	0x0d		On-chip I/O space access
 *	0x0e		Operand access (r/w)
 *	0x0f		Operand cache miss (r/w)
 *	0x10		Branch instruction
 *	0x11		Branch taken
 *	0x12		BSR/BSRF/JSR
 *	0x13		Instruction execution
 *	0x14		Instruction execution in parallel
 *	0x15		FPU Instruction execution
 *	0x16		Interrupt
 *	0x17		NMI
 *	0x18		trapa instruction execution
 *	0x19		UBCA match
 *	0x1a		UBCB match
 *	0x21		Instruction cache fill
 *	0x22		Operand cache fill
 *	0x23		Elapsed time
 *	0x24		Pipeline freeze by I-cache miss
 *	0x25		Pipeline freeze by D-cache miss
 *	0x27		Pipeline freeze by branch instruction
 *	0x28		Pipeline freeze by CPU register
 *	0x29		Pipeline freeze by FPU
 */

static const int sh7750_general_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= 0x0023,
	[PERF_COUNT_HW_INSTRUCTIONS]		= 0x000a,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= 0x0006,	/* I-cache */
	[PERF_COUNT_HW_CACHE_MISSES]		= 0x0008,	/* I-cache */
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x0010,
	[PERF_COUNT_HW_BRANCH_MISSES]		= -1,
	[PERF_COUNT_HW_BUS_CYCLES]		= -1,
};

#define C(x)	PERF_COUNT_HW_CACHE_##x

static const int sh7750_cache_events
			[PERF_COUNT_HW_CACHE_MAX]
			[PERF_COUNT_HW_CACHE_OP_MAX]
			[PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
	[ C(L1D) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0x0001,
			[ C(RESULT_MISS)   ] = 0x0004,
		},
		[ C(OP_WRITE) ] = {
			[ C(RESULT_ACCESS) ] = 0x0002,
			[ C(RESULT_MISS)   ] = 0x0005,
		},
		[ C(OP_PREFETCH) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
		},
	},

	[ C(L1I) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0x0006,
			[ C(RESULT_MISS)   ] = 0x0008,
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
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0,
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

	[ C(DTLB) ] = {
		[ C(OP_READ) ] = {
			[ C(RESULT_ACCESS) ] = 0,
			[ C(RESULT_MISS)   ] = 0x0003,
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
			[ C(RESULT_MISS)   ] = 0x0007,
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
};

static int sh7750_event_map(int event)
{
	return sh7750_general_events[event];
}

static u64 sh7750_pmu_read(int idx)
{
	return (u64)((u64)(__raw_readl(PMCTRH(idx)) & 0xffff) << 32) |
			   __raw_readl(PMCTRL(idx));
}

static void sh7750_pmu_disable(struct hw_perf_event *hwc, int idx)
{
	unsigned int tmp;

	tmp = __raw_readw(PMCR(idx));
	tmp &= ~(PMCR_PMM_MASK | PMCR_PMEN);
	__raw_writew(tmp, PMCR(idx));
}

static void sh7750_pmu_enable(struct hw_perf_event *hwc, int idx)
{
	__raw_writew(__raw_readw(PMCR(idx)) | PMCR_PMCLR, PMCR(idx));
	__raw_writew(hwc->config | PMCR_PMEN | PMCR_PMST, PMCR(idx));
}

static void sh7750_pmu_disable_all(void)
{
	int i;

	for (i = 0; i < sh7750_pmu.num_events; i++)
		__raw_writew(__raw_readw(PMCR(i)) & ~PMCR_PMEN, PMCR(i));
}

static void sh7750_pmu_enable_all(void)
{
	int i;

	for (i = 0; i < sh7750_pmu.num_events; i++)
		__raw_writew(__raw_readw(PMCR(i)) | PMCR_PMEN, PMCR(i));
}

static struct sh_pmu sh7750_pmu = {
	.name		= "sh7750",
	.num_events	= 2,
	.event_map	= sh7750_event_map,
	.max_events	= ARRAY_SIZE(sh7750_general_events),
	.raw_event_mask	= PMCR_PMM_MASK,
	.cache_events	= &sh7750_cache_events,
	.read		= sh7750_pmu_read,
	.disable	= sh7750_pmu_disable,
	.enable		= sh7750_pmu_enable,
	.disable_all	= sh7750_pmu_disable_all,
	.enable_all	= sh7750_pmu_enable_all,
};

static int __init sh7750_pmu_init(void)
{
	/*
	 * Make sure this CPU actually has perf counters.
	 */
	if (!(boot_cpu_data.flags & CPU_HAS_PERF_COUNTER)) {
		pr_notice("HW perf events unsupported, software events only.\n");
		return -ENODEV;
	}

	return register_sh_pmu(&sh7750_pmu);
}
early_initcall(sh7750_pmu_init);
