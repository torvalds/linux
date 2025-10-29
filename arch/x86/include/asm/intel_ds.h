#ifndef _ASM_INTEL_DS_H
#define _ASM_INTEL_DS_H

#include <linux/percpu-defs.h>

#define BTS_BUFFER_SIZE		(PAGE_SIZE << 4)
#define PEBS_BUFFER_SHIFT	4
#define PEBS_BUFFER_SIZE	(PAGE_SIZE << PEBS_BUFFER_SHIFT)

/*
 * The largest PEBS record could consume a page, ensure
 * a record at least can be written after triggering PMI.
 */
#define ARCH_PEBS_THRESH_MULTI	((PEBS_BUFFER_SIZE - PAGE_SIZE) >> PEBS_BUFFER_SHIFT)
#define ARCH_PEBS_THRESH_SINGLE	1

/* The maximal number of PEBS events: */
#define MAX_PEBS_EVENTS_FMT4	8
#define MAX_PEBS_EVENTS		32
#define MAX_PEBS_EVENTS_MASK	GENMASK_ULL(MAX_PEBS_EVENTS - 1, 0)
#define MAX_FIXED_PEBS_EVENTS	16

/*
 * A debug store configuration.
 *
 * We only support architectures that use 64bit fields.
 */
struct debug_store {
	u64	bts_buffer_base;
	u64	bts_index;
	u64	bts_absolute_maximum;
	u64	bts_interrupt_threshold;
	u64	pebs_buffer_base;
	u64	pebs_index;
	u64	pebs_absolute_maximum;
	u64	pebs_interrupt_threshold;
	u64	pebs_event_reset[MAX_PEBS_EVENTS + MAX_FIXED_PEBS_EVENTS];
} __aligned(PAGE_SIZE);

DECLARE_PER_CPU_PAGE_ALIGNED(struct debug_store, cpu_debug_store);

struct debug_store_buffers {
	char	bts_buffer[BTS_BUFFER_SIZE];
	char	pebs_buffer[PEBS_BUFFER_SIZE];
};

#endif
