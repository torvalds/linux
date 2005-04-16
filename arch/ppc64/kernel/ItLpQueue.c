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
#include <asm/system.h>
#include <asm/paca.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvCallEvent.h>
#include <asm/iSeries/LparData.h>

static __inline__ int set_inUse( struct ItLpQueue * lpQueue )
{
	int t;
	u32 * inUseP = &(lpQueue->xInUseWord);

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%2		\n\
	cmpwi	0,%0,0		\n\
	li	%0,0		\n\
	bne-	2f		\n\
	addi	%0,%0,1		\n\
	stwcx.	%0,0,%2		\n\
	bne-	1b		\n\
2:	eieio"
	: "=&r" (t), "=m" (lpQueue->xInUseWord)
	: "r" (inUseP), "m" (lpQueue->xInUseWord)
	: "cc");

	return t;
}

static __inline__ void clear_inUse( struct ItLpQueue * lpQueue )
{
	lpQueue->xInUseWord = 0;
}

/* Array of LpEvent handler functions */
extern LpEventHandler lpEventHandler[HvLpEvent_Type_NumTypes];
unsigned long ItLpQueueInProcess = 0;

struct HvLpEvent * ItLpQueue_getNextLpEvent( struct ItLpQueue * lpQueue )
{
	struct HvLpEvent * nextLpEvent = 
		(struct HvLpEvent *)lpQueue->xSlicCurEventPtr;
	if ( nextLpEvent->xFlags.xValid ) {
		/* rmb() needed only for weakly consistent machines (regatta) */
		rmb();
		/* Set pointer to next potential event */
		lpQueue->xSlicCurEventPtr += ((nextLpEvent->xSizeMinus1 +
				      LpEventAlign ) /
				      LpEventAlign ) *
				      LpEventAlign;
		/* Wrap to beginning if no room at end */
		if (lpQueue->xSlicCurEventPtr > lpQueue->xSlicLastValidEventPtr)
			lpQueue->xSlicCurEventPtr = lpQueue->xSlicEventStackPtr;
	}
	else 
		nextLpEvent = NULL;

	return nextLpEvent;
}

int ItLpQueue_isLpIntPending( struct ItLpQueue * lpQueue )
{
	int retval = 0;
	struct HvLpEvent * nextLpEvent;
	if ( lpQueue ) {
		nextLpEvent = (struct HvLpEvent *)lpQueue->xSlicCurEventPtr;
		retval = nextLpEvent->xFlags.xValid | lpQueue->xPlicOverflowIntPending;
	}
	return retval;
}

void ItLpQueue_clearValid( struct HvLpEvent * event )
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

unsigned ItLpQueue_process( struct ItLpQueue * lpQueue, struct pt_regs *regs )
{
	unsigned numIntsProcessed = 0;
	struct HvLpEvent * nextLpEvent;

	/* If we have recursed, just return */
	if ( !set_inUse( lpQueue ) )
		return 0;
	
	if (ItLpQueueInProcess == 0)
		ItLpQueueInProcess = 1;
	else
		BUG();

	for (;;) {
		nextLpEvent = ItLpQueue_getNextLpEvent( lpQueue );
		if ( nextLpEvent ) {
			/* Count events to return to caller
			 * and count processed events in lpQueue
 			 */
			++numIntsProcessed;
			lpQueue->xLpIntCount++;		
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
				lpQueue->xLpIntCountByType[nextLpEvent->xType]++;
			if ( nextLpEvent->xType < HvLpEvent_Type_NumTypes &&
			     lpEventHandler[nextLpEvent->xType] ) 
				lpEventHandler[nextLpEvent->xType](nextLpEvent, regs);
			else
				printk(KERN_INFO "Unexpected Lp Event type=%d\n", nextLpEvent->xType );
			
			ItLpQueue_clearValid( nextLpEvent );
		} else if ( lpQueue->xPlicOverflowIntPending )
			/*
			 * No more valid events. If overflow events are
			 * pending process them
			 */
			HvCallEvent_getOverflowLpEvents( lpQueue->xIndex);
		else
			break;
	}

	ItLpQueueInProcess = 0;
	mb();
	clear_inUse( lpQueue );

	get_paca()->lpevent_count += numIntsProcessed;

	return numIntsProcessed;
}
