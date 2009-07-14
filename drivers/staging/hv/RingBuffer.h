/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _RING_BUFFER_H_
#define _RING_BUFFER_H_

#include "include/osd.h"

typedef struct _SG_BUFFER_LIST {
	PVOID	Data;
	UINT32	Length;
} SG_BUFFER_LIST;

typedef struct _RING_BUFFER {
    volatile UINT32	WriteIndex;     // Offset in bytes from the start of ring data below
    volatile UINT32	ReadIndex;      // Offset in bytes from the start of ring data below

	volatile UINT32 InterruptMask;
	UINT8	Reserved[4084];			// Pad it to PAGE_SIZE so that data starts on page boundary
	// NOTE: The InterruptMask field is used only for channels but since our vmbus connection
	// also uses this data structure and its data starts here, we commented out this field.
	// volatile UINT32 InterruptMask;
	// Ring data starts here + RingDataStartOffset !!! DO NOT place any fields below this !!!
    UINT8		Buffer[0];
} STRUCT_PACKED RING_BUFFER;

typedef struct _RING_BUFFER_INFO {
    RING_BUFFER*	RingBuffer;
    UINT32			RingSize;			// Include the shared header
	HANDLE			RingLock;

    UINT32			RingDataSize;		// < ringSize
	UINT32			RingDataStartOffset;

} RING_BUFFER_INFO;


typedef struct _RING_BUFFER_DEBUG_INFO {
	UINT32		CurrentInterruptMask;
	UINT32		CurrentReadIndex;
	UINT32		CurrentWriteIndex;
	UINT32		BytesAvailToRead;
	UINT32		BytesAvailToWrite;
}RING_BUFFER_DEBUG_INFO;


//
// Interface
//

static int
RingBufferInit(
	RING_BUFFER_INFO	*RingInfo,
	PVOID				Buffer,
	UINT32				BufferLen
	);

static void
RingBufferCleanup(
	RING_BUFFER_INFO	*RingInfo
	);

static int
RingBufferWrite(
	RING_BUFFER_INFO	*RingInfo,
	SG_BUFFER_LIST		SgBuffers[],
	UINT32				SgBufferCount
	);

static int
RingBufferPeek(
	RING_BUFFER_INFO	*RingInfo,
	PVOID				Buffer,
	UINT32				BufferLen
	);

static int
RingBufferRead(
	RING_BUFFER_INFO	*RingInfo,
	PVOID				Buffer,
	UINT32				BufferLen,
	UINT32				Offset
	);

static UINT32
GetRingBufferInterruptMask(
	RING_BUFFER_INFO *RingInfo
	);

static void
DumpRingInfo(
	RING_BUFFER_INFO* RingInfo,
	char *Prefix
	);

static void
RingBufferGetDebugInfo(
	RING_BUFFER_INFO		*RingInfo,
	RING_BUFFER_DEBUG_INFO	*DebugInfo
	);

#endif // _RING_BUFFER_H_
