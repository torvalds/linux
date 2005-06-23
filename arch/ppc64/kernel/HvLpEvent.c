/*
 * Copyright 2001 Mike Corrigan IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/system.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvCallEvent.h>
#include <asm/iSeries/ItLpNaca.h>

/* Array of LpEvent handler functions */
LpEventHandler lpEventHandler[HvLpEvent_Type_NumTypes];
unsigned lpEventHandlerPaths[HvLpEvent_Type_NumTypes];

/* Register a handler for an LpEvent type */

int HvLpEvent_registerHandler( HvLpEvent_Type eventType, LpEventHandler handler )
{
	int rc = 1;
	if ( eventType < HvLpEvent_Type_NumTypes ) {
		lpEventHandler[eventType] = handler;
		rc = 0;
	}
	return rc;
	
}

int HvLpEvent_unregisterHandler( HvLpEvent_Type eventType )
{
	int rc = 1;

	might_sleep();

	if ( eventType < HvLpEvent_Type_NumTypes ) {
		if ( !lpEventHandlerPaths[eventType] ) {
			lpEventHandler[eventType] = NULL;
			rc = 0;

			/* We now sleep until all other CPUs have scheduled. This ensures that
			 * the deletion is seen by all other CPUs, and that the deleted handler
			 * isn't still running on another CPU when we return. */
			synchronize_rcu();
		}
	}
	return rc;
}
EXPORT_SYMBOL(HvLpEvent_registerHandler);
EXPORT_SYMBOL(HvLpEvent_unregisterHandler);

/* (lpIndex is the partition index of the target partition.  
 * needed only for VirtualIo, VirtualLan and SessionMgr.  Zero
 * indicates to use our partition index - for the other types)
 */
int HvLpEvent_openPath( HvLpEvent_Type eventType, HvLpIndex lpIndex )
{
	int rc = 1;
	if ( eventType < HvLpEvent_Type_NumTypes &&
	     lpEventHandler[eventType] ) {
		if ( lpIndex == 0 )
			lpIndex = itLpNaca.xLpIndex;
		HvCallEvent_openLpEventPath( lpIndex, eventType );
		++lpEventHandlerPaths[eventType];
		rc = 0;
	}
	return rc;
}

int HvLpEvent_closePath( HvLpEvent_Type eventType, HvLpIndex lpIndex )
{
	int rc = 1;
	if ( eventType < HvLpEvent_Type_NumTypes &&
	     lpEventHandler[eventType] &&
	     lpEventHandlerPaths[eventType] ) {
		if ( lpIndex == 0 )
			lpIndex = itLpNaca.xLpIndex;
		HvCallEvent_closeLpEventPath( lpIndex, eventType );
		--lpEventHandlerPaths[eventType];
		rc = 0;
	}
	return rc;
}

