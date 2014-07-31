/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 * Module Name:
 *  controlframework.h
 *
 * Abstract: This file defines common structures in the unmanaged
 *	     Ultravisor (mostly EFI) space.
 *
 */

#ifndef _CONTROL_FRAMEWORK_H_
#define _CONTROL_FRAMEWORK_H_

#include "commontypes.h"
#include "channel.h"

#define ULTRA_MEMORY_COUNT_Ki 1024

/* Scale order 0 is one 32-bit (4-byte) word (in 64 or 128-bit
 * architecture potentially 64 or 128-bit word) */
#define ULTRA_MEMORY_PAGE_WORD 4

/* Define Ki scale page to be traditional 4KB page */
#define ULTRA_MEMORY_PAGE_Ki (ULTRA_MEMORY_PAGE_WORD * ULTRA_MEMORY_COUNT_Ki)
typedef struct _ULTRA_SEGMENT_STATE  {
	u16 Enabled:1;		/* Bit 0: May enter other states */
	u16 Active:1;		/* Bit 1: Assigned to active partition */
	u16 Alive:1;		/* Bit 2: Configure message sent to
				 * service/server */
	u16 Revoked:1;		/* Bit 3: similar to partition state
				 * ShuttingDown */
	u16 Allocated:1;	/* Bit 4: memory (device/port number)
				 * has been selected by Command */
	u16 Known:1;		/* Bit 5: has been introduced to the
				 * service/guest partition */
	u16 Ready:1;		/* Bit 6: service/Guest partition has
				 * responded to introduction */
	u16 Operating:1;	/* Bit 7: resource is configured and
				 * operating */
	/* Note: don't use high bit unless we need to switch to ushort
	 * which is non-compliant */
} ULTRA_SEGMENT_STATE;
static const ULTRA_SEGMENT_STATE SegmentStateRunning = {
	1, 1, 1, 0, 1, 1, 1, 1
};
static const ULTRA_SEGMENT_STATE SegmentStatePaused = {
	1, 1, 1, 0, 1, 1, 1, 0
};
static const ULTRA_SEGMENT_STATE SegmentStateStandby = {
	1, 1, 0, 0, 1, 1, 1, 0
};
typedef union {
	U64 Full;
	struct {
		u8 Major;	/* will be 1 for the first release and
				 * increment thereafter  */
		u8 Minor;
		u16 Maintenance;
		U32 Revision;	/* Subversion revision */
	} Part;
} ULTRA_COMPONENT_VERSION;

#endif				/* _CONTROL_FRAMEWORK_H_ not defined */
