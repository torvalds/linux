/* Performance event support for sparc64.
 *
 * Copyright (C) 2009, 2010 David S. Miller <davem@davemloft.net>
 *
 * This code is based almost entirely upon the x86 perf event
 * code, which is:
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2009 Jaswinder Singh Rajput
 *  Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 */

#include <linux/perf_event.h>
#include <linux/kprobes.h>
#include <linux/ftrace.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/mutex.h>

#include <asm/stacktrace.h>
#include <asm/cpudata.h>
#include <asm/uaccess.h>
#include <linux/atomic.h>
#include <asm/nmi.h>
#include <asm/pcr.h>
#include <asm/cacheflush.h>

#include "kernel.h"
#include "kstack.h"

/* Two classes of sparc64 chips currently exist.  All of which have
 * 32-bit counters which can generate overflow interrupts on the
 * transition from 0xffffffff to 0.
 *
 * All chips upto and including SPARC-T3 have two performance
 * counters.  The two 32-bit counters are accessed in one go using a
 * single 64-bit register.
 *
 * On these older chips both counters are controlled using a single
 * control register.  The only way to stop all sampling is to clear
 * all of the context (user, supervisor, hypervisor) sampling enable
 * bits.  But these bits apply to both counters, thus the two counters
 * can't be enabled/disabled individually.
 *
 * Furthermore, the control register on these older chips have two
 * event fields, one for each of the two counters.  It's thus nearly
 * impossible to have one counter going while keeping the other one
 * stopped.  Therefore it is possible to get overflow interrupts for
 * counters not currently "in use" and that condition must be checked
 * in the overflow interrupt handler.
 *
 * So we use a hack, in that we program inactive counters with the
 * "sw_count0" and "sw_count1" events.  These count how many times
 * the instruction "sethi %hi(0xfc000), %g0" is executed.  It's an
 * unusual way to encode a NOP and therefore will not trigger in
 * normal code.
 *
 * Starting with SPARC-T4 we have one control register per counter.
 * And the counters are stored in individual registers.  The registers
 * for the counters are 64-bit but only a 32-bit counter is
 * implemented.  The event selections on SPARC-T4 lack any
 * restrictions, therefore we can elide all of the complicated
 * conflict resolution code we have for SPARC-T3 and earlier chips.
 */

#define MAX_HWEVENTS			4
#define MAX_PCRS			4
#define MAX_PERIOD			((1UL << 32) - 1)

#define PIC_UPPER_INDEX			0
#define PIC_LOWER_INDEX			1
#define PIC_NO_INDEX			-1

struct cpu_hw_events {
	/* Number of events currently scheduled onto this cpu.
	 * This tells how many entries in the arrays below
	 * are valid.
	 */
	int			n_events;

	/* Number of new events added since the last hw_perf_disable().
	 * This works because the perf event layer always adds new
	 * events inside of a perf_{disable,enable}() sequence.
	 */
	int			n_added;

	/* Array of events current scheduled on this cpu.  */
	struct perf_event	*event[MAX_HWEVENTS];

	/* Array of encoded longs, specifying the %pcr register
	 * encoding and the mask of PIC counters this even can
	 * be scheduled on.  See perf_event_encode() et al.
	 */
	unsigned long		events[MAX_HWEVENTS];

	/* The current counter index assigned to an event.  When the
	 * event hasn't been programmed into the cpu yet, this will
	 * hold PIC_NO_INDEX.  The event->hw.idx value tells us where
	 * we ought to schedule the event.
	 */
	int			current_idx[MAX_HWEVENTS];

	/* Software copy of %pcr register(s) on this cpu.  */
	u64			pcr[MAX_HWEVENTS];

	/* Enabled/disable state.  */
	int			enabled;

	unsigned int		group_flag;
};
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events) = { .enabled = 1, };

/* An event map describes the characteristics of a performance
 * counter event.  In particular it gives the encoding as well as
 * a mask telling which counters the event can be measured on.
 *
 * The mask is unused on SPARC-T4 and later.
 */
struct perf_event_map {
	u16	encoding;
	u8	pic_mask;
#define PIC_NONE	0x00
#define PIC_UPPER	0x01
#define PIC_LOWER	0x02
};

/* Encode a perf_event_map entry into a long.  */
static unsigned long perf_event_encode(const struct perf_event_map *pmap)
{
	return ((unsigned long) pmap->encoding << 16) | pmap->pic_mask;
}

static u8 perf_event_get_msk(unsigned long val)
{
	return val & 0xff;
}

static u64 perf_event_get_enc(unsigned long val)
{
	return val >> 16;
}

#define C(x) PERF_COUNT_HW_CACHE_##x

#define CACHE_OP_UNSUPPORTED	0xfffe
#define CACHE_OP_NONSENSE	0xffff

typedef struct perf_event_map cache_map_t
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX];

struct sparc_pmu {
	const struct perf_event_map	*(*event_map)(int);
	const cache_map_t		*cache_map;
	int				max_events;
	u32				(*read_pmc)(int);
	void				(*write_pmc)(int, u64);
	int				upper_shift;
	int				lower_shift;
	int				event_mask;
	int				user_bit;
	int				priv_bit;
	int				hv_bit;
	int				irq_bit;
	int				upper_nop;
	int				lower_nop;
	unsigned int			flags;
#define SPARC_PMU_ALL_EXCLUDES_SAME	0x00000001
#define SPARC_PMU_HAS_CONFLICTS		0x00000002
	int				max_hw_events;
	int				num_pcrs;
	int				num_pic_regs;
};

static u32 sparc_default_read_pmc(int idx)
{
	u64 val;

	val = pcr_ops->read_pic(0);
	if (idx == PIC_UPPER_INDEX)
		val >>= 32;

	return val & 0xffffffff;
}

static void sparc_default_write_pmc(int idx, u64 val)
{
	u64 shift, mask, pic;

	shift = 0;
	if (idx == PIC_UPPER_INDEX)
		shift = 32;

	mask = ((u64) 0xffffffff) << shift;
	val <<= shift;

	pic = pcr_ops->read_pic(0);
	pic &= ~mask;
	pic |= val;
	pcr_ops->write_pic(0, pic);
}

static const struct perf_event_map ultra3_perfmon_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES] = { 0x0000, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_INSTRUCTIONS] = { 0x0001, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_CACHE_REFERENCES] = { 0x0009, PIC_LOWER },
	[PERF_COUNT_HW_CACHE_MISSES] = { 0x0009, PIC_UPPER },
};

static const struct perf_event_map *ultra3_event_map(int event_id)
{
	return &ultra3_perfmon_event_map[event_id];
}

static const cache_map_t ultra3_cache_map = {
[C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { 0x09, PIC_LOWER, },
		[C(RESULT_MISS)] = { 0x09, PIC_UPPER, },
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = { 0x0a, PIC_LOWER },
		[C(RESULT_MISS)] = { 0x0a, PIC_UPPER },
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { 0x09, PIC_LOWER, },
		[C(RESULT_MISS)] = { 0x09, PIC_UPPER, },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_NONSENSE },
		[ C(RESULT_MISS)   ] = { CACHE_OP_NONSENSE },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { 0x0c, PIC_LOWER, },
		[C(RESULT_MISS)] = { 0x0c, PIC_UPPER, },
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = { 0x0c, PIC_LOWER },
		[C(RESULT_MISS)] = { 0x0c, PIC_UPPER },
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0x12, PIC_UPPER, },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0x11, PIC_UPPER, },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(NODE)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)  ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
};

static const struct sparc_pmu ultra3_pmu = {
	.event_map	= ultra3_event_map,
	.cache_map	= &ultra3_cache_map,
	.max_events	= ARRAY_SIZE(ultra3_perfmon_event_map),
	.read_pmc	= sparc_default_read_pmc,
	.write_pmc	= sparc_default_write_pmc,
	.upper_shift	= 11,
	.lower_shift	= 4,
	.event_mask	= 0x3f,
	.user_bit	= PCR_UTRACE,
	.priv_bit	= PCR_STRACE,
	.upper_nop	= 0x1c,
	.lower_nop	= 0x14,
	.flags		= (SPARC_PMU_ALL_EXCLUDES_SAME |
			   SPARC_PMU_HAS_CONFLICTS),
	.max_hw_events	= 2,
	.num_pcrs	= 1,
	.num_pic_regs	= 1,
};

/* Niagara1 is very limited.  The upper PIC is hard-locked to count
 * only instructions, so it is free running which creates all kinds of
 * problems.  Some hardware designs make one wonder if the creator
 * even looked at how this stuff gets used by software.
 */
static const struct perf_event_map niagara1_perfmon_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES] = { 0x00, PIC_UPPER },
	[PERF_COUNT_HW_INSTRUCTIONS] = { 0x00, PIC_UPPER },
	[PERF_COUNT_HW_CACHE_REFERENCES] = { 0, PIC_NONE },
	[PERF_COUNT_HW_CACHE_MISSES] = { 0x03, PIC_LOWER },
};

static const struct perf_event_map *niagara1_event_map(int event_id)
{
	return &niagara1_perfmon_event_map[event_id];
}

static const cache_map_t niagara1_cache_map = {
[C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0x03, PIC_LOWER, },
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0x03, PIC_LOWER, },
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { 0x00, PIC_UPPER },
		[C(RESULT_MISS)] = { 0x02, PIC_LOWER, },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_NONSENSE },
		[ C(RESULT_MISS)   ] = { CACHE_OP_NONSENSE },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0x07, PIC_LOWER, },
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0x07, PIC_LOWER, },
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0x05, PIC_LOWER, },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0x04, PIC_LOWER, },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(NODE)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)  ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
};

static const struct sparc_pmu niagara1_pmu = {
	.event_map	= niagara1_event_map,
	.cache_map	= &niagara1_cache_map,
	.max_events	= ARRAY_SIZE(niagara1_perfmon_event_map),
	.read_pmc	= sparc_default_read_pmc,
	.write_pmc	= sparc_default_write_pmc,
	.upper_shift	= 0,
	.lower_shift	= 4,
	.event_mask	= 0x7,
	.user_bit	= PCR_UTRACE,
	.priv_bit	= PCR_STRACE,
	.upper_nop	= 0x0,
	.lower_nop	= 0x0,
	.flags		= (SPARC_PMU_ALL_EXCLUDES_SAME |
			   SPARC_PMU_HAS_CONFLICTS),
	.max_hw_events	= 2,
	.num_pcrs	= 1,
	.num_pic_regs	= 1,
};

static const struct perf_event_map niagara2_perfmon_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES] = { 0x02ff, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_INSTRUCTIONS] = { 0x02ff, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_CACHE_REFERENCES] = { 0x0208, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_CACHE_MISSES] = { 0x0302, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = { 0x0201, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_BRANCH_MISSES] = { 0x0202, PIC_UPPER | PIC_LOWER },
};

static const struct perf_event_map *niagara2_event_map(int event_id)
{
	return &niagara2_perfmon_event_map[event_id];
}

static const cache_map_t niagara2_cache_map = {
[C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { 0x0208, PIC_UPPER | PIC_LOWER, },
		[C(RESULT_MISS)] = { 0x0302, PIC_UPPER | PIC_LOWER, },
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = { 0x0210, PIC_UPPER | PIC_LOWER, },
		[C(RESULT_MISS)] = { 0x0302, PIC_UPPER | PIC_LOWER, },
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { 0x02ff, PIC_UPPER | PIC_LOWER, },
		[C(RESULT_MISS)] = { 0x0301, PIC_UPPER | PIC_LOWER, },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_NONSENSE },
		[ C(RESULT_MISS)   ] = { CACHE_OP_NONSENSE },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { 0x0208, PIC_UPPER | PIC_LOWER, },
		[C(RESULT_MISS)] = { 0x0330, PIC_UPPER | PIC_LOWER, },
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = { 0x0210, PIC_UPPER | PIC_LOWER, },
		[C(RESULT_MISS)] = { 0x0320, PIC_UPPER | PIC_LOWER, },
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0x0b08, PIC_UPPER | PIC_LOWER, },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { 0xb04, PIC_UPPER | PIC_LOWER, },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(NODE)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)  ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
};

static const struct sparc_pmu niagara2_pmu = {
	.event_map	= niagara2_event_map,
	.cache_map	= &niagara2_cache_map,
	.max_events	= ARRAY_SIZE(niagara2_perfmon_event_map),
	.read_pmc	= sparc_default_read_pmc,
	.write_pmc	= sparc_default_write_pmc,
	.upper_shift	= 19,
	.lower_shift	= 6,
	.event_mask	= 0xfff,
	.user_bit	= PCR_UTRACE,
	.priv_bit	= PCR_STRACE,
	.hv_bit		= PCR_N2_HTRACE,
	.irq_bit	= 0x30,
	.upper_nop	= 0x220,
	.lower_nop	= 0x220,
	.flags		= (SPARC_PMU_ALL_EXCLUDES_SAME |
			   SPARC_PMU_HAS_CONFLICTS),
	.max_hw_events	= 2,
	.num_pcrs	= 1,
	.num_pic_regs	= 1,
};

static const struct perf_event_map niagara4_perfmon_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES] = { (26 << 6) },
	[PERF_COUNT_HW_INSTRUCTIONS] = { (3 << 6) | 0x3f },
	[PERF_COUNT_HW_CACHE_REFERENCES] = { (3 << 6) | 0x04 },
	[PERF_COUNT_HW_CACHE_MISSES] = { (16 << 6) | 0x07 },
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = { (4 << 6) | 0x01 },
	[PERF_COUNT_HW_BRANCH_MISSES] = { (25 << 6) | 0x0f },
};

static const struct perf_event_map *niagara4_event_map(int event_id)
{
	return &niagara4_perfmon_event_map[event_id];
}

static const cache_map_t niagara4_cache_map = {
[C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { (3 << 6) | 0x04 },
		[C(RESULT_MISS)] = { (16 << 6) | 0x07 },
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = { (3 << 6) | 0x08 },
		[C(RESULT_MISS)] = { (16 << 6) | 0x07 },
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { (3 << 6) | 0x3f },
		[C(RESULT_MISS)] = { (11 << 6) | 0x03 },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_NONSENSE },
		[ C(RESULT_MISS)   ] = { CACHE_OP_NONSENSE },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { (3 << 6) | 0x04 },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = { (3 << 6) | 0x08 },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { (17 << 6) | 0x3f },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { (6 << 6) | 0x3f },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
[C(NODE)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = { CACHE_OP_UNSUPPORTED },
		[C(RESULT_MISS)  ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = { CACHE_OP_UNSUPPORTED },
		[ C(RESULT_MISS)   ] = { CACHE_OP_UNSUPPORTED },
	},
},
};

static u32 sparc_vt_read_pmc(int idx)
{
	u64 val = pcr_ops->read_pic(idx);

	return val & 0xffffffff;
}

static void sparc_vt_write_pmc(int idx, u64 val)
{
	u64 pcr;

	/* There seems to be an internal latch on the overflow event
	 * on SPARC-T4 that prevents it from triggering unless you
	 * update the PIC exactly as we do here.  The requirement
	 * seems to be that you have to turn off event counting in the
	 * PCR around the PIC update.
	 *
	 * For example, after the following sequence:
	 *
	 * 1) set PIC to -1
	 * 2) enable event counting and overflow reporting in PCR
	 * 3) overflow triggers, softint 15 handler invoked
	 * 4) clear OV bit in PCR
	 * 5) write PIC to -1
	 *
	 * a subsequent overflow event will not trigger.  This
	 * sequence works on SPARC-T3 and previous chips.
	 */
	pcr = pcr_ops->read_pcr(idx);
	pcr_ops->write_pcr(idx, PCR_N4_PICNPT);

	pcr_ops->write_pic(idx, val & 0xffffffff);

	pcr_ops->write_pcr(idx, pcr);
}

static const struct sparc_pmu niagara4_pmu = {
	.event_map	= niagara4_event_map,
	.cache_map	= &niagara4_cache_map,
	.max_events	= ARRAY_SIZE(niagara4_perfmon_event_map),
	.read_pmc	= sparc_vt_read_pmc,
	.write_pmc	= sparc_vt_write_pmc,
	.upper_shift	= 5,
	.lower_shift	= 5,
	.event_mask	= 0x7ff,
	.user_bit	= PCR_N4_UTRACE,
	.priv_bit	= PCR_N4_STRACE,

	/* We explicitly don't support hypervisor tracing.  The T4
	 * generates the overflow event for precise events via a trap
	 * which will not be generated (ie. it's completely lost) if
	 * we happen to be in the hypervisor when the event triggers.
	 * Essentially, the overflow event reporting is completely
	 * unusable when you have hypervisor mode tracing enabled.
	 */
	.hv_bit		= 0,

	.irq_bit	= PCR_N4_TOE,
	.upper_nop	= 0,
	.lower_nop	= 0,
	.flags		= 0,
	.max_hw_events	= 4,
	.num_pcrs	= 4,
	.num_pic_regs	= 4,
};

static const struct sparc_pmu *sparc_pmu __read_mostly;

static u64 event_encoding(u64 event_id, int idx)
{
	if (idx == PIC_UPPER_INDEX)
		event_id <<= sparc_pmu->upper_shift;
	else
		event_id <<= sparc_pmu->lower_shift;
	return event_id;
}

static u64 mask_for_index(int idx)
{
	return event_encoding(sparc_pmu->event_mask, idx);
}

static u64 nop_for_index(int idx)
{
	return event_encoding(idx == PIC_UPPER_INDEX ?
			      sparc_pmu->upper_nop :
			      sparc_pmu->lower_nop, idx);
}

static inline void sparc_pmu_enable_event(struct cpu_hw_events *cpuc, struct hw_perf_event *hwc, int idx)
{
	u64 enc, val, mask = mask_for_index(idx);
	int pcr_index = 0;

	if (sparc_pmu->num_pcrs > 1)
		pcr_index = idx;

	enc = perf_event_get_enc(cpuc->events[idx]);

	val = cpuc->pcr[pcr_index];
	val &= ~mask;
	val |= event_encoding(enc, idx);
	cpuc->pcr[pcr_index] = val;

	pcr_ops->write_pcr(pcr_index, cpuc->pcr[pcr_index]);
}

static inline void sparc_pmu_disable_event(struct cpu_hw_events *cpuc, struct hw_perf_event *hwc, int idx)
{
	u64 mask = mask_for_index(idx);
	u64 nop = nop_for_index(idx);
	int pcr_index = 0;
	u64 val;

	if (sparc_pmu->num_pcrs > 1)
		pcr_index = idx;

	val = cpuc->pcr[pcr_index];
	val &= ~mask;
	val |= nop;
	cpuc->pcr[pcr_index] = val;

	pcr_ops->write_pcr(pcr_index, cpuc->pcr[pcr_index]);
}

static u64 sparc_perf_event_update(struct perf_event *event,
				   struct hw_perf_event *hwc, int idx)
{
	int shift = 64 - 32;
	u64 prev_raw_count, new_raw_count;
	s64 delta;

again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = sparc_pmu->read_pmc(idx);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			     new_raw_count) != prev_raw_count)
		goto again;

	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);

	return new_raw_count;
}

static int sparc_perf_event_set_period(struct perf_event *event,
				       struct hw_perf_event *hwc, int idx)
{
	s64 left = local64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int ret = 0;

	if (unlikely(left <= -period)) {
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}
	if (left > MAX_PERIOD)
		left = MAX_PERIOD;

	local64_set(&hwc->prev_count, (u64)-left);

	sparc_pmu->write_pmc(idx, (u64)(-left) & 0xffffffff);

	perf_event_update_userpage(event);

	return ret;
}

static void read_in_all_counters(struct cpu_hw_events *cpuc)
{
	int i;

	for (i = 0; i < cpuc->n_events; i++) {
		struct perf_event *cp = cpuc->event[i];

		if (cpuc->current_idx[i] != PIC_NO_INDEX &&
		    cpuc->current_idx[i] != cp->hw.idx) {
			sparc_perf_event_update(cp, &cp->hw,
						cpuc->current_idx[i]);
			cpuc->current_idx[i] = PIC_NO_INDEX;
		}
	}
}

/* On this PMU all PICs are programmed using a single PCR.  Calculate
 * the combined control register value.
 *
 * For such chips we require that all of the events have the same
 * configuration, so just fetch the settings from the first entry.
 */
static void calculate_single_pcr(struct cpu_hw_events *cpuc)
{
	int i;

	if (!cpuc->n_added)
		goto out;

	/* Assign to counters all unassigned events.  */
	for (i = 0; i < cpuc->n_events; i++) {
		struct perf_event *cp = cpuc->event[i];
		struct hw_perf_event *hwc = &cp->hw;
		int idx = hwc->idx;
		u64 enc;

		if (cpuc->current_idx[i] != PIC_NO_INDEX)
			continue;

		sparc_perf_event_set_period(cp, hwc, idx);
		cpuc->current_idx[i] = idx;

		enc = perf_event_get_enc(cpuc->events[i]);
		cpuc->pcr[0] &= ~mask_for_index(idx);
		if (hwc->state & PERF_HES_STOPPED)
			cpuc->pcr[0] |= nop_for_index(idx);
		else
			cpuc->pcr[0] |= event_encoding(enc, idx);
	}
out:
	cpuc->pcr[0] |= cpuc->event[0]->hw.config_base;
}

/* On this PMU each PIC has it's own PCR control register.  */
static void calculate_multiple_pcrs(struct cpu_hw_events *cpuc)
{
	int i;

	if (!cpuc->n_added)
		goto out;

	for (i = 0; i < cpuc->n_events; i++) {
		struct perf_event *cp = cpuc->event[i];
		struct hw_perf_event *hwc = &cp->hw;
		int idx = hwc->idx;
		u64 enc;

		if (cpuc->current_idx[i] != PIC_NO_INDEX)
			continue;

		sparc_perf_event_set_period(cp, hwc, idx);
		cpuc->current_idx[i] = idx;

		enc = perf_event_get_enc(cpuc->events[i]);
		cpuc->pcr[idx] &= ~mask_for_index(idx);
		if (hwc->state & PERF_HES_STOPPED)
			cpuc->pcr[idx] |= nop_for_index(idx);
		else
			cpuc->pcr[idx] |= event_encoding(enc, idx);
	}
out:
	for (i = 0; i < cpuc->n_events; i++) {
		struct perf_event *cp = cpuc->event[i];
		int idx = cp->hw.idx;

		cpuc->pcr[idx] |= cp->hw.config_base;
	}
}

/* If performance event entries have been added, move existing events
 * around (if necessary) and then assign new entries to counters.
 */
static void update_pcrs_for_enable(struct cpu_hw_events *cpuc)
{
	if (cpuc->n_added)
		read_in_all_counters(cpuc);

	if (sparc_pmu->num_pcrs == 1) {
		calculate_single_pcr(cpuc);
	} else {
		calculate_multiple_pcrs(cpuc);
	}
}

static void sparc_pmu_enable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int i;

	if (cpuc->enabled)
		return;

	cpuc->enabled = 1;
	barrier();

	if (cpuc->n_events)
		update_pcrs_for_enable(cpuc);

	for (i = 0; i < sparc_pmu->num_pcrs; i++)
		pcr_ops->write_pcr(i, cpuc->pcr[i]);
}

static void sparc_pmu_disable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int i;

	if (!cpuc->enabled)
		return;

	cpuc->enabled = 0;
	cpuc->n_added = 0;

	for (i = 0; i < sparc_pmu->num_pcrs; i++) {
		u64 val = cpuc->pcr[i];

		val &= ~(sparc_pmu->user_bit | sparc_pmu->priv_bit |
			 sparc_pmu->hv_bit | sparc_pmu->irq_bit);
		cpuc->pcr[i] = val;
		pcr_ops->write_pcr(i, cpuc->pcr[i]);
	}
}

static int active_event_index(struct cpu_hw_events *cpuc,
			      struct perf_event *event)
{
	int i;

	for (i = 0; i < cpuc->n_events; i++) {
		if (cpuc->event[i] == event)
			break;
	}
	BUG_ON(i == cpuc->n_events);
	return cpuc->current_idx[i];
}

static void sparc_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx = active_event_index(cpuc, event);

	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));
		sparc_perf_event_set_period(event, &event->hw, idx);
	}

	event->hw.state = 0;

	sparc_pmu_enable_event(cpuc, &event->hw, idx);
}

static void sparc_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx = active_event_index(cpuc, event);

	if (!(event->hw.state & PERF_HES_STOPPED)) {
		sparc_pmu_disable_event(cpuc, &event->hw, idx);
		event->hw.state |= PERF_HES_STOPPED;
	}

	if (!(event->hw.state & PERF_HES_UPTODATE) && (flags & PERF_EF_UPDATE)) {
		sparc_perf_event_update(event, &event->hw, idx);
		event->hw.state |= PERF_HES_UPTODATE;
	}
}

static void sparc_pmu_del(struct perf_event *event, int _flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	unsigned long flags;
	int i;

	local_irq_save(flags);

	for (i = 0; i < cpuc->n_events; i++) {
		if (event == cpuc->event[i]) {
			/* Absorb the final count and turn off the
			 * event.
			 */
			sparc_pmu_stop(event, PERF_EF_UPDATE);

			/* Shift remaining entries down into
			 * the existing slot.
			 */
			while (++i < cpuc->n_events) {
				cpuc->event[i - 1] = cpuc->event[i];
				cpuc->events[i - 1] = cpuc->events[i];
				cpuc->current_idx[i - 1] =
					cpuc->current_idx[i];
			}

			perf_event_update_userpage(event);

			cpuc->n_events--;
			break;
		}
	}

	local_irq_restore(flags);
}

static void sparc_pmu_read(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx = active_event_index(cpuc, event);
	struct hw_perf_event *hwc = &event->hw;

	sparc_perf_event_update(event, hwc, idx);
}

static atomic_t active_events = ATOMIC_INIT(0);
static DEFINE_MUTEX(pmc_grab_mutex);

static void perf_stop_nmi_watchdog(void *unused)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int i;

	stop_nmi_watchdog(NULL);
	for (i = 0; i < sparc_pmu->num_pcrs; i++)
		cpuc->pcr[i] = pcr_ops->read_pcr(i);
}

static void perf_event_grab_pmc(void)
{
	if (atomic_inc_not_zero(&active_events))
		return;

	mutex_lock(&pmc_grab_mutex);
	if (atomic_read(&active_events) == 0) {
		if (atomic_read(&nmi_active) > 0) {
			on_each_cpu(perf_stop_nmi_watchdog, NULL, 1);
			BUG_ON(atomic_read(&nmi_active) != 0);
		}
		atomic_inc(&active_events);
	}
	mutex_unlock(&pmc_grab_mutex);
}

static void perf_event_release_pmc(void)
{
	if (atomic_dec_and_mutex_lock(&active_events, &pmc_grab_mutex)) {
		if (atomic_read(&nmi_active) == 0)
			on_each_cpu(start_nmi_watchdog, NULL, 1);
		mutex_unlock(&pmc_grab_mutex);
	}
}

static const struct perf_event_map *sparc_map_cache_event(u64 config)
{
	unsigned int cache_type, cache_op, cache_result;
	const struct perf_event_map *pmap;

	if (!sparc_pmu->cache_map)
		return ERR_PTR(-ENOENT);

	cache_type = (config >>  0) & 0xff;
	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return ERR_PTR(-EINVAL);

	cache_op = (config >>  8) & 0xff;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return ERR_PTR(-EINVAL);

	cache_result = (config >> 16) & 0xff;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return ERR_PTR(-EINVAL);

	pmap = &((*sparc_pmu->cache_map)[cache_type][cache_op][cache_result]);

	if (pmap->encoding == CACHE_OP_UNSUPPORTED)
		return ERR_PTR(-ENOENT);

	if (pmap->encoding == CACHE_OP_NONSENSE)
		return ERR_PTR(-EINVAL);

	return pmap;
}

static void hw_perf_event_destroy(struct perf_event *event)
{
	perf_event_release_pmc();
}

/* Make sure all events can be scheduled into the hardware at
 * the same time.  This is simplified by the fact that we only
 * need to support 2 simultaneous HW events.
 *
 * As a side effect, the evts[]->hw.idx values will be assigned
 * on success.  These are pending indexes.  When the events are
 * actually programmed into the chip, these values will propagate
 * to the per-cpu cpuc->current_idx[] slots, see the code in
 * maybe_change_configuration() for details.
 */
static int sparc_check_constraints(struct perf_event **evts,
				   unsigned long *events, int n_ev)
{
	u8 msk0 = 0, msk1 = 0;
	int idx0 = 0;

	/* This case is possible when we are invoked from
	 * hw_perf_group_sched_in().
	 */
	if (!n_ev)
		return 0;

	if (n_ev > sparc_pmu->max_hw_events)
		return -1;

	if (!(sparc_pmu->flags & SPARC_PMU_HAS_CONFLICTS)) {
		int i;

		for (i = 0; i < n_ev; i++)
			evts[i]->hw.idx = i;
		return 0;
	}

	msk0 = perf_event_get_msk(events[0]);
	if (n_ev == 1) {
		if (msk0 & PIC_LOWER)
			idx0 = 1;
		goto success;
	}
	BUG_ON(n_ev != 2);
	msk1 = perf_event_get_msk(events[1]);

	/* If both events can go on any counter, OK.  */
	if (msk0 == (PIC_UPPER | PIC_LOWER) &&
	    msk1 == (PIC_UPPER | PIC_LOWER))
		goto success;

	/* If one event is limited to a specific counter,
	 * and the other can go on both, OK.
	 */
	if ((msk0 == PIC_UPPER || msk0 == PIC_LOWER) &&
	    msk1 == (PIC_UPPER | PIC_LOWER)) {
		if (msk0 & PIC_LOWER)
			idx0 = 1;
		goto success;
	}

	if ((msk1 == PIC_UPPER || msk1 == PIC_LOWER) &&
	    msk0 == (PIC_UPPER | PIC_LOWER)) {
		if (msk1 & PIC_UPPER)
			idx0 = 1;
		goto success;
	}

	/* If the events are fixed to different counters, OK.  */
	if ((msk0 == PIC_UPPER && msk1 == PIC_LOWER) ||
	    (msk0 == PIC_LOWER && msk1 == PIC_UPPER)) {
		if (msk0 & PIC_LOWER)
			idx0 = 1;
		goto success;
	}

	/* Otherwise, there is a conflict.  */
	return -1;

success:
	evts[0]->hw.idx = idx0;
	if (n_ev == 2)
		evts[1]->hw.idx = idx0 ^ 1;
	return 0;
}

static int check_excludes(struct perf_event **evts, int n_prev, int n_new)
{
	int eu = 0, ek = 0, eh = 0;
	struct perf_event *event;
	int i, n, first;

	if (!(sparc_pmu->flags & SPARC_PMU_ALL_EXCLUDES_SAME))
		return 0;

	n = n_prev + n_new;
	if (n <= 1)
		return 0;

	first = 1;
	for (i = 0; i < n; i++) {
		event = evts[i];
		if (first) {
			eu = event->attr.exclude_user;
			ek = event->attr.exclude_kernel;
			eh = event->attr.exclude_hv;
			first = 0;
		} else if (event->attr.exclude_user != eu ||
			   event->attr.exclude_kernel != ek ||
			   event->attr.exclude_hv != eh) {
			return -EAGAIN;
		}
	}

	return 0;
}

static int collect_events(struct perf_event *group, int max_count,
			  struct perf_event *evts[], unsigned long *events,
			  int *current_idx)
{
	struct perf_event *event;
	int n = 0;

	if (!is_software_event(group)) {
		if (n >= max_count)
			return -1;
		evts[n] = group;
		events[n] = group->hw.event_base;
		current_idx[n++] = PIC_NO_INDEX;
	}
	list_for_each_entry(event, &group->sibling_list, group_entry) {
		if (!is_software_event(event) &&
		    event->state != PERF_EVENT_STATE_OFF) {
			if (n >= max_count)
				return -1;
			evts[n] = event;
			events[n] = event->hw.event_base;
			current_idx[n++] = PIC_NO_INDEX;
		}
	}
	return n;
}

static int sparc_pmu_add(struct perf_event *event, int ef_flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int n0, ret = -EAGAIN;
	unsigned long flags;

	local_irq_save(flags);

	n0 = cpuc->n_events;
	if (n0 >= sparc_pmu->max_hw_events)
		goto out;

	cpuc->event[n0] = event;
	cpuc->events[n0] = event->hw.event_base;
	cpuc->current_idx[n0] = PIC_NO_INDEX;

	event->hw.state = PERF_HES_UPTODATE;
	if (!(ef_flags & PERF_EF_START))
		event->hw.state |= PERF_HES_STOPPED;

	/*
	 * If group events scheduling transaction was started,
	 * skip the schedulability test here, it will be performed
	 * at commit time(->commit_txn) as a whole
	 */
	if (cpuc->group_flag & PERF_EVENT_TXN)
		goto nocheck;

	if (check_excludes(cpuc->event, n0, 1))
		goto out;
	if (sparc_check_constraints(cpuc->event, cpuc->events, n0 + 1))
		goto out;

nocheck:
	cpuc->n_events++;
	cpuc->n_added++;

	ret = 0;
out:
	local_irq_restore(flags);
	return ret;
}

static int sparc_pmu_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct perf_event *evts[MAX_HWEVENTS];
	struct hw_perf_event *hwc = &event->hw;
	unsigned long events[MAX_HWEVENTS];
	int current_idx_dmy[MAX_HWEVENTS];
	const struct perf_event_map *pmap;
	int n;

	if (atomic_read(&nmi_active) < 0)
		return -ENODEV;

	/* does not support taken branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	switch (attr->type) {
	case PERF_TYPE_HARDWARE:
		if (attr->config >= sparc_pmu->max_events)
			return -EINVAL;
		pmap = sparc_pmu->event_map(attr->config);
		break;

	case PERF_TYPE_HW_CACHE:
		pmap = sparc_map_cache_event(attr->config);
		if (IS_ERR(pmap))
			return PTR_ERR(pmap);
		break;

	case PERF_TYPE_RAW:
		pmap = NULL;
		break;

	default:
		return -ENOENT;

	}

	if (pmap) {
		hwc->event_base = perf_event_encode(pmap);
	} else {
		/*
		 * User gives us "(encoding << 16) | pic_mask" for
		 * PERF_TYPE_RAW events.
		 */
		hwc->event_base = attr->config;
	}

	/* We save the enable bits in the config_base.  */
	hwc->config_base = sparc_pmu->irq_bit;
	if (!attr->exclude_user)
		hwc->config_base |= sparc_pmu->user_bit;
	if (!attr->exclude_kernel)
		hwc->config_base |= sparc_pmu->priv_bit;
	if (!attr->exclude_hv)
		hwc->config_base |= sparc_pmu->hv_bit;

	n = 0;
	if (event->group_leader != event) {
		n = collect_events(event->group_leader,
				   sparc_pmu->max_hw_events - 1,
				   evts, events, current_idx_dmy);
		if (n < 0)
			return -EINVAL;
	}
	events[n] = hwc->event_base;
	evts[n] = event;

	if (check_excludes(evts, n, 1))
		return -EINVAL;

	if (sparc_check_constraints(evts, events, n + 1))
		return -EINVAL;

	hwc->idx = PIC_NO_INDEX;

	/* Try to do all error checking before this point, as unwinding
	 * state after grabbing the PMC is difficult.
	 */
	perf_event_grab_pmc();
	event->destroy = hw_perf_event_destroy;

	if (!hwc->sample_period) {
		hwc->sample_period = MAX_PERIOD;
		hwc->last_period = hwc->sample_period;
		local64_set(&hwc->period_left, hwc->sample_period);
	}

	return 0;
}

/*
 * Start group events scheduling transaction
 * Set the flag to make pmu::enable() not perform the
 * schedulability test, it will be performed at commit time
 */
static void sparc_pmu_start_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

	perf_pmu_disable(pmu);
	cpuhw->group_flag |= PERF_EVENT_TXN;
}

/*
 * Stop group events scheduling transaction
 * Clear the flag and pmu::enable() will perform the
 * schedulability test.
 */
static void sparc_pmu_cancel_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

	cpuhw->group_flag &= ~PERF_EVENT_TXN;
	perf_pmu_enable(pmu);
}

/*
 * Commit group events scheduling transaction
 * Perform the group schedulability test as a whole
 * Return 0 if success
 */
static int sparc_pmu_commit_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int n;

	if (!sparc_pmu)
		return -EINVAL;

	cpuc = this_cpu_ptr(&cpu_hw_events);
	n = cpuc->n_events;
	if (check_excludes(cpuc->event, 0, n))
		return -EINVAL;
	if (sparc_check_constraints(cpuc->event, cpuc->events, n))
		return -EAGAIN;

	cpuc->group_flag &= ~PERF_EVENT_TXN;
	perf_pmu_enable(pmu);
	return 0;
}

static struct pmu pmu = {
	.pmu_enable	= sparc_pmu_enable,
	.pmu_disable	= sparc_pmu_disable,
	.event_init	= sparc_pmu_event_init,
	.add		= sparc_pmu_add,
	.del		= sparc_pmu_del,
	.start		= sparc_pmu_start,
	.stop		= sparc_pmu_stop,
	.read		= sparc_pmu_read,
	.start_txn	= sparc_pmu_start_txn,
	.cancel_txn	= sparc_pmu_cancel_txn,
	.commit_txn	= sparc_pmu_commit_txn,
};

void perf_event_print_debug(void)
{
	unsigned long flags;
	int cpu, i;

	if (!sparc_pmu)
		return;

	local_irq_save(flags);

	cpu = smp_processor_id();

	pr_info("\n");
	for (i = 0; i < sparc_pmu->num_pcrs; i++)
		pr_info("CPU#%d: PCR%d[%016llx]\n",
			cpu, i, pcr_ops->read_pcr(i));
	for (i = 0; i < sparc_pmu->num_pic_regs; i++)
		pr_info("CPU#%d: PIC%d[%016llx]\n",
			cpu, i, pcr_ops->read_pic(i));

	local_irq_restore(flags);
}

static int __kprobes perf_event_nmi_handler(struct notifier_block *self,
					    unsigned long cmd, void *__args)
{
	struct die_args *args = __args;
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct pt_regs *regs;
	int i;

	if (!atomic_read(&active_events))
		return NOTIFY_DONE;

	switch (cmd) {
	case DIE_NMI:
		break;

	default:
		return NOTIFY_DONE;
	}

	regs = args->regs;

	cpuc = this_cpu_ptr(&cpu_hw_events);

	/* If the PMU has the TOE IRQ enable bits, we need to do a
	 * dummy write to the %pcr to clear the overflow bits and thus
	 * the interrupt.
	 *
	 * Do this before we peek at the counters to determine
	 * overflow so we don't lose any events.
	 */
	if (sparc_pmu->irq_bit &&
	    sparc_pmu->num_pcrs == 1)
		pcr_ops->write_pcr(0, cpuc->pcr[0]);

	for (i = 0; i < cpuc->n_events; i++) {
		struct perf_event *event = cpuc->event[i];
		int idx = cpuc->current_idx[i];
		struct hw_perf_event *hwc;
		u64 val;

		if (sparc_pmu->irq_bit &&
		    sparc_pmu->num_pcrs > 1)
			pcr_ops->write_pcr(idx, cpuc->pcr[idx]);

		hwc = &event->hw;
		val = sparc_perf_event_update(event, hwc, idx);
		if (val & (1ULL << 31))
			continue;

		perf_sample_data_init(&data, 0, hwc->last_period);
		if (!sparc_perf_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, &data, regs))
			sparc_pmu_stop(event, 0);
	}

	return NOTIFY_STOP;
}

static __read_mostly struct notifier_block perf_event_nmi_notifier = {
	.notifier_call		= perf_event_nmi_handler,
};

static bool __init supported_pmu(void)
{
	if (!strcmp(sparc_pmu_type, "ultra3") ||
	    !strcmp(sparc_pmu_type, "ultra3+") ||
	    !strcmp(sparc_pmu_type, "ultra3i") ||
	    !strcmp(sparc_pmu_type, "ultra4+")) {
		sparc_pmu = &ultra3_pmu;
		return true;
	}
	if (!strcmp(sparc_pmu_type, "niagara")) {
		sparc_pmu = &niagara1_pmu;
		return true;
	}
	if (!strcmp(sparc_pmu_type, "niagara2") ||
	    !strcmp(sparc_pmu_type, "niagara3")) {
		sparc_pmu = &niagara2_pmu;
		return true;
	}
	if (!strcmp(sparc_pmu_type, "niagara4") ||
	    !strcmp(sparc_pmu_type, "niagara5")) {
		sparc_pmu = &niagara4_pmu;
		return true;
	}
	return false;
}

static int __init init_hw_perf_events(void)
{
	int err;

	pr_info("Performance events: ");

	err = pcr_arch_init();
	if (err || !supported_pmu()) {
		pr_cont("No support for PMU type '%s'\n", sparc_pmu_type);
		return 0;
	}

	pr_cont("Supported PMU type is '%s'\n", sparc_pmu_type);

	perf_pmu_register(&pmu, "cpu", PERF_TYPE_RAW);
	register_die_notifier(&perf_event_nmi_notifier);

	return 0;
}
pure_initcall(init_hw_perf_events);

void perf_callchain_kernel(struct perf_callchain_entry *entry,
			   struct pt_regs *regs)
{
	unsigned long ksp, fp;
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	int graph = 0;
#endif

	stack_trace_flush();

	perf_callchain_store(entry, regs->tpc);

	ksp = regs->u_regs[UREG_I6];
	fp = ksp + STACK_BIAS;
	do {
		struct sparc_stackf *sf;
		struct pt_regs *regs;
		unsigned long pc;

		if (!kstack_valid(current_thread_info(), fp))
			break;

		sf = (struct sparc_stackf *) fp;
		regs = (struct pt_regs *) (sf + 1);

		if (kstack_is_trap_frame(current_thread_info(), regs)) {
			if (user_mode(regs))
				break;
			pc = regs->tpc;
			fp = regs->u_regs[UREG_I6] + STACK_BIAS;
		} else {
			pc = sf->callers_pc;
			fp = (unsigned long)sf->fp + STACK_BIAS;
		}
		perf_callchain_store(entry, pc);
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
		if ((pc + 8UL) == (unsigned long) &return_to_handler) {
			int index = current->curr_ret_stack;
			if (current->ret_stack && index >= graph) {
				pc = current->ret_stack[index - graph].ret;
				perf_callchain_store(entry, pc);
				graph++;
			}
		}
#endif
	} while (entry->nr < PERF_MAX_STACK_DEPTH);
}

static void perf_callchain_user_64(struct perf_callchain_entry *entry,
				   struct pt_regs *regs)
{
	unsigned long ufp;

	ufp = regs->u_regs[UREG_I6] + STACK_BIAS;
	do {
		struct sparc_stackf __user *usf;
		struct sparc_stackf sf;
		unsigned long pc;

		usf = (struct sparc_stackf __user *)ufp;
		if (__copy_from_user_inatomic(&sf, usf, sizeof(sf)))
			break;

		pc = sf.callers_pc;
		ufp = (unsigned long)sf.fp + STACK_BIAS;
		perf_callchain_store(entry, pc);
	} while (entry->nr < PERF_MAX_STACK_DEPTH);
}

static void perf_callchain_user_32(struct perf_callchain_entry *entry,
				   struct pt_regs *regs)
{
	unsigned long ufp;

	ufp = regs->u_regs[UREG_I6] & 0xffffffffUL;
	do {
		unsigned long pc;

		if (thread32_stack_is_64bit(ufp)) {
			struct sparc_stackf __user *usf;
			struct sparc_stackf sf;

			ufp += STACK_BIAS;
			usf = (struct sparc_stackf __user *)ufp;
			if (__copy_from_user_inatomic(&sf, usf, sizeof(sf)))
				break;
			pc = sf.callers_pc & 0xffffffff;
			ufp = ((unsigned long) sf.fp) & 0xffffffff;
		} else {
			struct sparc_stackf32 __user *usf;
			struct sparc_stackf32 sf;
			usf = (struct sparc_stackf32 __user *)ufp;
			if (__copy_from_user_inatomic(&sf, usf, sizeof(sf)))
				break;
			pc = sf.callers_pc;
			ufp = (unsigned long)sf.fp;
		}
		perf_callchain_store(entry, pc);
	} while (entry->nr < PERF_MAX_STACK_DEPTH);
}

void
perf_callchain_user(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	perf_callchain_store(entry, regs->tpc);

	if (!current->mm)
		return;

	flushw_user();
	if (test_thread_flag(TIF_32BIT))
		perf_callchain_user_32(entry, regs);
	else
		perf_callchain_user_64(entry, regs);
}
