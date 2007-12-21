/*
 * Copyright (C) 2001 Mike Corrigan  IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/paca.h>
#include <asm/firmware.h>
#include <asm/iseries/it_lp_queue.h>
#include <asm/iseries/hv_lp_event.h>
#include <asm/iseries/hv_call_event.h>
#include "it_lp_naca.h"

/*
 * The LpQueue is used to pass event data from the hypervisor to
 * the partition.  This is where I/O interrupt events are communicated.
 *
 * It is written to by the hypervisor so cannot end up in the BSS.
 */
struct hvlpevent_queue hvlpevent_queue __attribute__((__section__(".data")));

DEFINE_PER_CPU(unsigned long[HvLpEvent_Type_NumTypes], hvlpevent_counts);

static char *event_types[HvLpEvent_Type_NumTypes] = {
	"Hypervisor",
	"Machine Facilities",
	"Session Manager",
	"SPD I/O",
	"Virtual Bus",
	"PCI I/O",
	"RIO I/O",
	"Virtual Lan",
	"Virtual I/O"
};

/* Array of LpEvent handler functions */
static LpEventHandler lpEventHandler[HvLpEvent_Type_NumTypes];
static unsigned lpEventHandlerPaths[HvLpEvent_Type_NumTypes];

static struct HvLpEvent * get_next_hvlpevent(void)
{
	struct HvLpEvent * event;
	event = (struct HvLpEvent *)hvlpevent_queue.hq_current_event;

	if (hvlpevent_is_valid(event)) {
		/* rmb() needed only for weakly consistent machines (regatta) */
		rmb();
		/* Set pointer to next potential event */
		hvlpevent_queue.hq_current_event += ((event->xSizeMinus1 +
				IT_LP_EVENT_ALIGN) / IT_LP_EVENT_ALIGN) *
					IT_LP_EVENT_ALIGN;

		/* Wrap to beginning if no room at end */
		if (hvlpevent_queue.hq_current_event >
				hvlpevent_queue.hq_last_event) {
			hvlpevent_queue.hq_current_event =
				hvlpevent_queue.hq_event_stack;
		}
	} else {
		event = NULL;
	}

	return event;
}

static unsigned long spread_lpevents = NR_CPUS;

int hvlpevent_is_pending(void)
{
	struct HvLpEvent *next_event;

	if (smp_processor_id() >= spread_lpevents)
		return 0;

	next_event = (struct HvLpEvent *)hvlpevent_queue.hq_current_event;

	return hvlpevent_is_valid(next_event) ||
		hvlpevent_queue.hq_overflow_pending;
}

static void hvlpevent_clear_valid(struct HvLpEvent * event)
{
	/* Tell the Hypervisor that we're done with this event.
	 * Also clear bits within this event that might look like valid bits.
	 * ie. on 64-byte boundaries.
	 */
	struct HvLpEvent *tmp;
	unsigned extra = ((event->xSizeMinus1 + IT_LP_EVENT_ALIGN) /
				IT_LP_EVENT_ALIGN) - 1;

	switch (extra) {
	case 3:
		tmp = (struct HvLpEvent*)((char*)event + 3 * IT_LP_EVENT_ALIGN);
		hvlpevent_invalidate(tmp);
	case 2:
		tmp = (struct HvLpEvent*)((char*)event + 2 * IT_LP_EVENT_ALIGN);
		hvlpevent_invalidate(tmp);
	case 1:
		tmp = (struct HvLpEvent*)((char*)event + 1 * IT_LP_EVENT_ALIGN);
		hvlpevent_invalidate(tmp);
	}

	mb();

	hvlpevent_invalidate(event);
}

void process_hvlpevents(void)
{
	struct HvLpEvent * event;

 restart:
	/* If we have recursed, just return */
	if (!spin_trylock(&hvlpevent_queue.hq_lock))
		return;

	for (;;) {
		event = get_next_hvlpevent();
		if (event) {
			/* Call appropriate handler here, passing
			 * a pointer to the LpEvent.  The handler
			 * must make a copy of the LpEvent if it
			 * needs it in a bottom half. (perhaps for
			 * an ACK)
			 *
			 *  Handlers are responsible for ACK processing
			 *
			 * The Hypervisor guarantees that LpEvents will
			 * only be delivered with types that we have
			 * registered for, so no type check is necessary
			 * here!
			 */
			if (event->xType < HvLpEvent_Type_NumTypes)
				__get_cpu_var(hvlpevent_counts)[event->xType]++;
			if (event->xType < HvLpEvent_Type_NumTypes &&
					lpEventHandler[event->xType])
				lpEventHandler[event->xType](event);
			else {
				u8 type = event->xType;

				/*
				 * Don't printk in the spinlock as printk
				 * may require ack events form the HV to send
				 * any characters there.
				 */
				hvlpevent_clear_valid(event);
				spin_unlock(&hvlpevent_queue.hq_lock);
				printk(KERN_INFO
					"Unexpected Lp Event type=%d\n", type);
				goto restart;
			}

			hvlpevent_clear_valid(event);
		} else if (hvlpevent_queue.hq_overflow_pending)
			/*
			 * No more valid events. If overflow events are
			 * pending process them
			 */
			HvCallEvent_getOverflowLpEvents(hvlpevent_queue.hq_index);
		else
			break;
	}

	spin_unlock(&hvlpevent_queue.hq_lock);
}

static int set_spread_lpevents(char *str)
{
	unsigned long val = simple_strtoul(str, NULL, 0);

	/*
	 * The parameter is the number of processors to share in processing
	 * lp events.
	 */
	if (( val > 0) && (val <= NR_CPUS)) {
		spread_lpevents = val;
		printk("lpevent processing spread over %ld processors\n", val);
	} else {
		printk("invalid spread_lpevents %ld\n", val);
	}

	return 1;
}
__setup("spread_lpevents=", set_spread_lpevents);

void __init setup_hvlpevent_queue(void)
{
	void *eventStack;

	spin_lock_init(&hvlpevent_queue.hq_lock);

	/* Allocate a page for the Event Stack. */
	eventStack = alloc_bootmem_pages(IT_LP_EVENT_STACK_SIZE);
	memset(eventStack, 0, IT_LP_EVENT_STACK_SIZE);

	/* Invoke the hypervisor to initialize the event stack */
	HvCallEvent_setLpEventStack(0, eventStack, IT_LP_EVENT_STACK_SIZE);

	hvlpevent_queue.hq_event_stack = eventStack;
	hvlpevent_queue.hq_current_event = eventStack;
	hvlpevent_queue.hq_last_event = (char *)eventStack +
		(IT_LP_EVENT_STACK_SIZE - IT_LP_EVENT_MAX_SIZE);
	hvlpevent_queue.hq_index = 0;
}

/* Register a handler for an LpEvent type */
int HvLpEvent_registerHandler(HvLpEvent_Type eventType, LpEventHandler handler)
{
	if (eventType < HvLpEvent_Type_NumTypes) {
		lpEventHandler[eventType] = handler;
		return 0;
	}
	return 1;
}
EXPORT_SYMBOL(HvLpEvent_registerHandler);

int HvLpEvent_unregisterHandler(HvLpEvent_Type eventType)
{
	might_sleep();

	if (eventType < HvLpEvent_Type_NumTypes) {
		if (!lpEventHandlerPaths[eventType]) {
			lpEventHandler[eventType] = NULL;
			/*
			 * We now sleep until all other CPUs have scheduled.
			 * This ensures that the deletion is seen by all
			 * other CPUs, and that the deleted handler isn't
			 * still running on another CPU when we return.
			 */
			synchronize_sched();
			return 0;
		}
	}
	return 1;
}
EXPORT_SYMBOL(HvLpEvent_unregisterHandler);

/*
 * lpIndex is the partition index of the target partition.
 * needed only for VirtualIo, VirtualLan and SessionMgr.  Zero
 * indicates to use our partition index - for the other types.
 */
int HvLpEvent_openPath(HvLpEvent_Type eventType, HvLpIndex lpIndex)
{
	if ((eventType < HvLpEvent_Type_NumTypes) &&
			lpEventHandler[eventType]) {
		if (lpIndex == 0)
			lpIndex = itLpNaca.xLpIndex;
		HvCallEvent_openLpEventPath(lpIndex, eventType);
		++lpEventHandlerPaths[eventType];
		return 0;
	}
	return 1;
}

int HvLpEvent_closePath(HvLpEvent_Type eventType, HvLpIndex lpIndex)
{
	if ((eventType < HvLpEvent_Type_NumTypes) &&
			lpEventHandler[eventType] &&
			lpEventHandlerPaths[eventType]) {
		if (lpIndex == 0)
			lpIndex = itLpNaca.xLpIndex;
		HvCallEvent_closeLpEventPath(lpIndex, eventType);
		--lpEventHandlerPaths[eventType];
		return 0;
	}
	return 1;
}

static int proc_lpevents_show(struct seq_file *m, void *v)
{
	int cpu, i;
	unsigned long sum;
	static unsigned long cpu_totals[NR_CPUS];

	/* FIXME: do we care that there's no locking here? */
	sum = 0;
	for_each_online_cpu(cpu) {
		cpu_totals[cpu] = 0;
		for (i = 0; i < HvLpEvent_Type_NumTypes; i++) {
			cpu_totals[cpu] += per_cpu(hvlpevent_counts, cpu)[i];
		}
		sum += cpu_totals[cpu];
	}

	seq_printf(m, "LpEventQueue 0\n");
	seq_printf(m, "  events processed:\t%lu\n", sum);

	for (i = 0; i < HvLpEvent_Type_NumTypes; ++i) {
		sum = 0;
		for_each_online_cpu(cpu) {
			sum += per_cpu(hvlpevent_counts, cpu)[i];
		}

		seq_printf(m, "    %-20s %10lu\n", event_types[i], sum);
	}

	seq_printf(m, "\n  events processed by processor:\n");

	for_each_online_cpu(cpu) {
		seq_printf(m, "    CPU%02d  %10lu\n", cpu, cpu_totals[cpu]);
	}

	return 0;
}

static int proc_lpevents_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_lpevents_show, NULL);
}

static const struct file_operations proc_lpevents_operations = {
	.open		= proc_lpevents_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_lpevents_init(void)
{
	struct proc_dir_entry *e;

	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		return 0;

	e = create_proc_entry("iSeries/lpevents", S_IFREG|S_IRUGO, NULL);
	if (e)
		e->proc_fops = &proc_lpevents_operations;

	return 0;
}
__initcall(proc_lpevents_init);

