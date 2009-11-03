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

#include <linux/scatterlist.h>

typedef struct _RING_BUFFER {
	/* Offset in bytes from the start of ring data below */
	volatile u32 WriteIndex;

	/* Offset in bytes from the start of ring data below */
	volatile u32 ReadIndex;

	volatile u32 InterruptMask;

	/* Pad it to PAGE_SIZE so that data starts on page boundary */
	u8	Reserved[4084];

	/* NOTE:
	 * The InterruptMask field is used only for channels but since our
	 * vmbus connection also uses this data structure and its data starts
	 * here, we commented out this field.
	 */
	/* volatile u32 InterruptMask; */

	/*
	 * Ring data starts here + RingDataStartOffset
	 * !!! DO NOT place any fields below this !!!
	 */
	u8 Buffer[0];
} __attribute__((packed)) RING_BUFFER;

typedef struct _RING_BUFFER_INFO {
	RING_BUFFER *RingBuffer;
	u32 RingSize;			/* Include the shared header */
	spinlock_t ring_lock;

	u32 RingDataSize;		/* < ringSize */
	u32 RingDataStartOffset;

} RING_BUFFER_INFO;

typedef struct _RING_BUFFER_DEBUG_INFO {
	u32 CurrentInterruptMask;
	u32 CurrentReadIndex;
	u32 CurrentWriteIndex;
	u32 BytesAvailToRead;
	u32 BytesAvailToWrite;
} RING_BUFFER_DEBUG_INFO;



/* Interface */


int RingBufferInit(RING_BUFFER_INFO *RingInfo, void *Buffer, u32 BufferLen);

void RingBufferCleanup(RING_BUFFER_INFO *RingInfo);

int RingBufferWrite(RING_BUFFER_INFO *RingInfo,
		    struct scatterlist *sglist,
		    u32 sgcount);

int RingBufferPeek(RING_BUFFER_INFO *RingInfo, void *Buffer, u32 BufferLen);

int RingBufferRead(RING_BUFFER_INFO *RingInfo,
		   void *Buffer,
		   u32 BufferLen,
		   u32 Offset);

u32 GetRingBufferInterruptMask(RING_BUFFER_INFO *RingInfo);

void DumpRingInfo(RING_BUFFER_INFO *RingInfo, char *Prefix);

void RingBufferGetDebugInfo(RING_BUFFER_INFO *RingInfo,
			    RING_BUFFER_DEBUG_INFO *DebugInfo);

#endif /* _RING_BUFFER_H_ */
