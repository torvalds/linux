/*
 * ItLpQueue.c
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
#include <asm/system.h>
#include <asm/paca.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvCallEvent.h>

static char *event_types[9] = {
	"Hypervisor\t\t",
	"Machine Facilities\t",
	"Session Manager\t",
	"SPD I/O\t\t",
	"Virtual Bus\t\t",
	"PCI I/O\t\t",
	"RIO I/O\t\t",
	"Virtual Lan\t\t",
	"Virtual I/O\t\t"
};

static __inline__ int set_inUse(void)
{
	int t;
	u32 * inUseP = &xItLpQueue.xInUseWord;

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%2		\n\
	cmpwi	0,%0,0		\n\
	li	%0,0		\n\
	bne-	2f		\n\
	addi	%0,%0,1		\n\
	stwcx.	%0,0,%2		\n\
	bne-	1b		\n\
2:	eieio"
	: "=&r" (t), "=m" (xItLpQueue.xInUseWord)
	: "r" (inUseP), "m" (xItLpQueue.xInUseWord)
	: "cc");

	return t;
}

static __inline__ void clear_inUse(void)
{
	xItLpQueue.xInUseWord = 0;
}

/* Array of LpEvent handler functions */
extern LpEventHandler lpEventHandler[HvLpEvent_Type_NumTypes];
unsigned long ItLpQueueInProcess = 0;

static struct HvLpEvent * ItLpQueue_getNextLpEvent(void)
{
	struct HvLpEvent * nextLpEvent = 
		(struct HvLpEvent *)xItLpQueue.xSlicCurEventPtr;
	if ( nextLpEvent->xFlags.xValid ) {
		/* rmb() needed only for weakly consistent machines (regatta) */
		rmb();
		/* Set pointer to next potential event */
		xItLpQueue.xSlicCurEventPtr += ((nextLpEvent->xSizeMinus1 +
				      LpEventAlign ) /
				      LpEventAlign ) *
				      LpEventAlign;
		/* Wrap to beginning if no room at end */
		if (xItLpQueue.xSlicCurEventPtr > xItLpQueue.xSlicLastValidEventPtr)
			xItLpQueue.xSlicCurEventPtr = xItLpQueue.xSlicEventStackPtr;
	}
	else 
		nextLpEvent = NULL;

	return nextLpEvent;
}

static unsigned long spread_lpevents = NR_CPUS;

int ItLpQueue_isLpIntPending(void)
{
	struct HvLpEvent *next_event;

	if (smp_processor_id() >= spread_lpevents)
		return 0;

	next_event = (struct HvLpEvent *)xItLpQueue.xSlicCurEventPtr;
	return next_event->xFlags.xValid | xItLpQueue.xPlicOverflowIntPending;
}

static void ItLpQueue_clearValid( struct HvLpEvent * event )
{
	/* Clear the valid bit of the event
	 * Also clear bits within this event that might
	 * look like valid bits (on 64-byte boundaries)
   	 */
	unsigned extra = (( event->xSizeMinus1 + LpEventAlign ) /
						 LpEventAlign ) - 1;
	switch ( extra ) {
	  case 3:
	   ((struct HvLpEvent*)((char*)event+3*LpEventAlign))->xFlags.xValid=0;
	  case 2:
	   ((struct HvLpEvent*)((char*)event+2*LpEventAlign))->xFlags.xValid=0;
	  case 1:
	   ((struct HvLpEvent*)((char*)event+1*LpEventAlign))->xFlags.xValid=0;
	  case 0:
	   ;	
	}
	mb();
	event->xFlags.xValid = 0;
}

unsigned ItLpQueue_process(struct pt_regs *regs)
{
	unsigned numIntsProcessed = 0;
	struct HvLpEvent * nextLpEvent;

	/* If we have recursed, just return */
	if ( !set_inUse() )
		return 0;
	
	if (ItLpQueueInProcess == 0)
		ItLpQueueInProcess = 1;
	else
		BUG();

	for (;;) {
		nextLpEvent = ItLpQueue_getNextLpEvent();
		if ( nextLpEvent ) {
			/* Count events to return to caller
			 * and count processed events in xItLpQueue
 			 */
			++numIntsProcessed;
			xItLpQueue.xLpIntCount++;
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
			if ( nextLpEvent->xType < HvLpEvent_Type_NumTypes )
				xItLpQueue.xLpIntCountByType[nextLpEvent->xType]++;
			if ( nextLpEvent->xType < HvLpEvent_Type_NumTypes &&
			     lpEventHandler[nextLpEvent->xType] ) 
				lpEventHandler[nextLpEvent->xType](nextLpEvent, regs);
			else
				printk(KERN_INFO "Unexpected Lp Event type=%d\n", nextLpEvent->xType );
			
			ItLpQueue_clearValid( nextLpEvent );
		} else if ( xItLpQueue.xPlicOverflowIntPending )
			/*
			 * No more valid events. If overflow events are
			 * pending process them
			 */
			HvCallEvent_getOverflowLpEvents( xItLpQueue.xIndex);
		else
			break;
	}

	ItLpQueueInProcess = 0;
	mb();
	clear_inUse();

	get_paca()->lpevent_count += numIntsProcessed;

	return numIntsProcessed;
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

void setup_hvlpevent_queue(void)
{
	void *eventStack;

	/*
	 * Allocate a page for the Event Stack. The Hypervisor needs the
	 * absolute real address, so we subtract out the KERNELBASE and add
	 * in the absolute real address of the kernel load area.
	 */
	eventStack = alloc_bootmem_pages(LpEventStackSize);
	memset(eventStack, 0, LpEventStackSize);

	/* Invoke the hypervisor to initialize the event stack */
	HvCallEvent_setLpEventStack(0, eventStack, LpEventStackSize);

	xItLpQueue.xSlicEventStackPtr = (char *)eventStack;
	xItLpQueue.xSlicCurEventPtr = (char *)eventStack;
	xItLpQueue.xSlicLastValidEventPtr = (char *)eventStack +
					(LpEventStackSize - LpEventMaxSize);
	xItLpQueue.xIndex = 0;
}

static int proc_lpevents_show(struct seq_file *m, void *v)
{
	unsigned int i;

	seq_printf(m, "LpEventQueue 0\n");
	seq_printf(m, "  events processed:\t%lu\n",
		   (unsigned long)xItLpQueue.xLpIntCount);

	for (i = 0; i < 9; ++i)
		seq_printf(m, "    %s %10lu\n", event_types[i],
			   (unsigned long)xItLpQueue.xLpIntCountByType[i]);

	seq_printf(m, "\n  events processed by processor:\n");

	for_each_online_cpu(i)
		seq_printf(m, "    CPU%02d  %10u\n", i, paca[i].lpevent_count);

	return 0;
}

static int proc_lpevents_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_lpevents_show, NULL);
}

static struct file_operations proc_lpevents_operations = {
	.open		= proc_lpevents_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_lpevents_init(void)
{
	struct proc_dir_entry *e;

	e = create_proc_entry("iSeries/lpevents", S_IFREG|S_IRUGO, NULL);
	if (e)
		e->proc_fops = &proc_lpevents_operations;

	return 0;
}
__initcall(proc_lpevents_init);

