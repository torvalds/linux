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

#include <linux/kernel.h>
#include <linux/mm.h>
#include "osd.h"
#include "logging.h"
#include "RingBuffer.h"


/* #defines */


/* Amount of space to write to */
#define BYTES_AVAIL_TO_WRITE(r, w, z) ((w) >= (r))?((z) - ((w) - (r))):((r) - (w))


/*++

Name:
	GetRingBufferAvailBytes()

Description:
	Get number of bytes available to read and to write to
	for the specified ring buffer

--*/
static inline void
GetRingBufferAvailBytes(RING_BUFFER_INFO *rbi, u32 *read, u32 *write)
{
	u32 read_loc,write_loc;

	/* Capture the read/write indices before they changed */
	read_loc = rbi->RingBuffer->ReadIndex;
	write_loc = rbi->RingBuffer->WriteIndex;

	*write = BYTES_AVAIL_TO_WRITE(read_loc, write_loc, rbi->RingDataSize);
	*read = rbi->RingDataSize - *write;
}

/*++

Name:
	GetNextWriteLocation()

Description:
	Get the next write location for the specified ring buffer

--*/
static inline u32
GetNextWriteLocation(RING_BUFFER_INFO* RingInfo)
{
	u32 next = RingInfo->RingBuffer->WriteIndex;

	ASSERT(next < RingInfo->RingDataSize);

	return next;
}

/*++

Name:
	SetNextWriteLocation()

Description:
	Set the next write location for the specified ring buffer

--*/
static inline void
SetNextWriteLocation(RING_BUFFER_INFO* RingInfo, u32 NextWriteLocation)
{
	RingInfo->RingBuffer->WriteIndex = NextWriteLocation;
}

/*++

Name:
	GetNextReadLocation()

Description:
	Get the next read location for the specified ring buffer

--*/
static inline u32
GetNextReadLocation(RING_BUFFER_INFO* RingInfo)
{
	u32 next = RingInfo->RingBuffer->ReadIndex;

	ASSERT(next < RingInfo->RingDataSize);

	return next;
}

/*++

Name:
	GetNextReadLocationWithOffset()

Description:
	Get the next read location + offset for the specified ring buffer.
	This allows the caller to skip

--*/
static inline u32
GetNextReadLocationWithOffset(RING_BUFFER_INFO* RingInfo, u32 Offset)
{
	u32 next = RingInfo->RingBuffer->ReadIndex;

	ASSERT(next < RingInfo->RingDataSize);
	next += Offset;
	next %= RingInfo->RingDataSize;

	return next;
}

/*++

Name:
	SetNextReadLocation()

Description:
	Set the next read location for the specified ring buffer

--*/
static inline void
SetNextReadLocation(RING_BUFFER_INFO* RingInfo, u32 NextReadLocation)
{
	RingInfo->RingBuffer->ReadIndex = NextReadLocation;
}


/*++

Name:
	GetRingBuffer()

Description:
	Get the start of the ring buffer

--*/
static inline void *
GetRingBuffer(RING_BUFFER_INFO* RingInfo)
{
	return (void *)RingInfo->RingBuffer->Buffer;
}


/*++

Name:
	GetRingBufferSize()

Description:
	Get the size of the ring buffer

--*/
static inline u32
GetRingBufferSize(RING_BUFFER_INFO* RingInfo)
{
	return RingInfo->RingDataSize;
}

/*++

Name:
	GetRingBufferIndices()

Description:
	Get the read and write indices as u64 of the specified ring buffer

--*/
static inline u64
GetRingBufferIndices(RING_BUFFER_INFO* RingInfo)
{
	return (u64)RingInfo->RingBuffer->WriteIndex << 32;
}


/*++

Name:
	DumpRingInfo()

Description:
	Dump out to console the ring buffer info

--*/
void DumpRingInfo(RING_BUFFER_INFO *RingInfo, char *Prefix)
{
	u32 bytesAvailToWrite;
	u32 bytesAvailToRead;

	GetRingBufferAvailBytes(RingInfo, &bytesAvailToRead, &bytesAvailToWrite);

	DPRINT(VMBUS, DEBUG_RING_LVL, "%s <<ringinfo %p buffer %p avail write %u avail read %u read idx %u write idx %u>>",
		Prefix,
		RingInfo,
		RingInfo->RingBuffer->Buffer,
		bytesAvailToWrite,
		bytesAvailToRead,
		RingInfo->RingBuffer->ReadIndex,
		RingInfo->RingBuffer->WriteIndex);
}


/* Internal routines */

static u32
CopyToRingBuffer(
	RING_BUFFER_INFO	*RingInfo,
	u32				StartWriteOffset,
	void *				Src,
	u32				SrcLen);

static u32
CopyFromRingBuffer(
	RING_BUFFER_INFO	*RingInfo,
	void *				Dest,
	u32				DestLen,
	u32				StartReadOffset);



/*++

Name:
	RingBufferGetDebugInfo()

Description:
	Get various debug metrics for the specified ring buffer

--*/
void RingBufferGetDebugInfo(RING_BUFFER_INFO *RingInfo,
			    RING_BUFFER_DEBUG_INFO *DebugInfo)
{
	u32 bytesAvailToWrite;
	u32 bytesAvailToRead;

	if (RingInfo->RingBuffer)
	{
		GetRingBufferAvailBytes(RingInfo, &bytesAvailToRead, &bytesAvailToWrite);

		DebugInfo->BytesAvailToRead = bytesAvailToRead;
		DebugInfo->BytesAvailToWrite = bytesAvailToWrite;
		DebugInfo->CurrentReadIndex = RingInfo->RingBuffer->ReadIndex;
		DebugInfo->CurrentWriteIndex = RingInfo->RingBuffer->WriteIndex;

		DebugInfo->CurrentInterruptMask = RingInfo->RingBuffer->InterruptMask;
	}
}


/*++

Name:
	GetRingBufferInterruptMask()

Description:
	Get the interrupt mask for the specified ring buffer

--*/
u32 GetRingBufferInterruptMask(RING_BUFFER_INFO *rbi)
{
	return rbi->RingBuffer->InterruptMask;
}

/*++

Name:
	RingBufferInit()

Description:
	Initialize the ring buffer

--*/
int RingBufferInit(RING_BUFFER_INFO *RingInfo, void *Buffer, u32 BufferLen)
{
	ASSERT(sizeof(RING_BUFFER) == PAGE_SIZE);

	memset(RingInfo, 0, sizeof(RING_BUFFER_INFO));

	RingInfo->RingBuffer = (RING_BUFFER*)Buffer;
	RingInfo->RingBuffer->ReadIndex = RingInfo->RingBuffer->WriteIndex = 0;

	RingInfo->RingSize = BufferLen;
	RingInfo->RingDataSize = BufferLen - sizeof(RING_BUFFER);

	spin_lock_init(&RingInfo->ring_lock);

	return 0;
}

/*++

Name:
	RingBufferCleanup()

Description:
	Cleanup the ring buffer

--*/
void RingBufferCleanup(RING_BUFFER_INFO* RingInfo)
{
}

/*++

Name:
	RingBufferWrite()

Description:
	Write to the ring buffer

--*/
int RingBufferWrite(RING_BUFFER_INFO *OutRingInfo,
		    struct scatterlist *sglist, u32 sgcount)
{
	int i=0;
	u32 byteAvailToWrite;
	u32 byteAvailToRead;
	u32 totalBytesToWrite=0;

	struct scatterlist *sg;
	volatile u32 nextWriteLocation;
	u64 prevIndices=0;
	unsigned long flags;

	DPRINT_ENTER(VMBUS);

	for_each_sg(sglist, sg, sgcount, i)
	{
		totalBytesToWrite += sg->length;
	}

	totalBytesToWrite += sizeof(u64);

	spin_lock_irqsave(&OutRingInfo->ring_lock, flags);

	GetRingBufferAvailBytes(OutRingInfo, &byteAvailToRead, &byteAvailToWrite);

	DPRINT_DBG(VMBUS, "Writing %u bytes...", totalBytesToWrite);

	/* DumpRingInfo(OutRingInfo, "BEFORE "); */

	/* If there is only room for the packet, assume it is full. Otherwise, the next time around, we think the ring buffer */
	/* is empty since the read index == write index */
	if (byteAvailToWrite <= totalBytesToWrite)
	{
		DPRINT_DBG(VMBUS, "No more space left on outbound ring buffer (needed %u, avail %u)", totalBytesToWrite, byteAvailToWrite);

		spin_unlock_irqrestore(&OutRingInfo->ring_lock, flags);

		DPRINT_EXIT(VMBUS);

		return -1;
	}

	/* Write to the ring buffer */
	nextWriteLocation = GetNextWriteLocation(OutRingInfo);

	for_each_sg(sglist, sg, sgcount, i)
	{
		nextWriteLocation = CopyToRingBuffer(OutRingInfo,
						     nextWriteLocation,
						     sg_virt(sg),
						     sg->length);
	}

	/* Set previous packet start */
	prevIndices = GetRingBufferIndices(OutRingInfo);

	nextWriteLocation = CopyToRingBuffer(OutRingInfo,
					     nextWriteLocation,
					     &prevIndices,
					     sizeof(u64));

	/* Make sure we flush all writes before updating the writeIndex */
	mb();

	/* Now, update the write location */
	SetNextWriteLocation(OutRingInfo, nextWriteLocation);

	/* DumpRingInfo(OutRingInfo, "AFTER "); */

	spin_unlock_irqrestore(&OutRingInfo->ring_lock, flags);

	DPRINT_EXIT(VMBUS);

	return 0;
}


/*++

Name:
	RingBufferPeek()

Description:
	Read without advancing the read index

--*/
int RingBufferPeek(RING_BUFFER_INFO *InRingInfo, void *Buffer, u32 BufferLen)
{
	u32 bytesAvailToWrite;
	u32 bytesAvailToRead;
	u32 nextReadLocation=0;
	unsigned long flags;

	spin_lock_irqsave(&InRingInfo->ring_lock, flags);

	GetRingBufferAvailBytes(InRingInfo, &bytesAvailToRead, &bytesAvailToWrite);

	/* Make sure there is something to read */
	if (bytesAvailToRead < BufferLen )
	{
		/* DPRINT_DBG(VMBUS, "got callback but not enough to read <avail to read %d read size %d>!!", bytesAvailToRead, BufferLen); */

		spin_unlock_irqrestore(&InRingInfo->ring_lock, flags);

		return -1;
	}

	/* Convert to byte offset */
	nextReadLocation = GetNextReadLocation(InRingInfo);

	nextReadLocation = CopyFromRingBuffer(InRingInfo,
											Buffer,
											BufferLen,
											nextReadLocation);

	spin_unlock_irqrestore(&InRingInfo->ring_lock, flags);

	return 0;
}


/*++

Name:
	RingBufferRead()

Description:
	Read and advance the read index

--*/
int RingBufferRead(RING_BUFFER_INFO *InRingInfo, void *Buffer,
		   u32 BufferLen, u32 Offset)
{
	u32 bytesAvailToWrite;
	u32 bytesAvailToRead;
	u32 nextReadLocation=0;
	u64 prevIndices=0;
	unsigned long flags;

	ASSERT(BufferLen > 0);

	spin_lock_irqsave(&InRingInfo->ring_lock, flags);

	GetRingBufferAvailBytes(InRingInfo, &bytesAvailToRead, &bytesAvailToWrite);

	DPRINT_DBG(VMBUS, "Reading %u bytes...", BufferLen);

	/* DumpRingInfo(InRingInfo, "BEFORE "); */

	/* Make sure there is something to read */
	if (bytesAvailToRead < BufferLen )
	{
		DPRINT_DBG(VMBUS, "got callback but not enough to read <avail to read %d read size %d>!!", bytesAvailToRead, BufferLen);

		spin_unlock_irqrestore(&InRingInfo->ring_lock, flags);

		return -1;
	}

	nextReadLocation = GetNextReadLocationWithOffset(InRingInfo, Offset);

	nextReadLocation = CopyFromRingBuffer(InRingInfo,
											Buffer,
											BufferLen,
											nextReadLocation);

	nextReadLocation = CopyFromRingBuffer(InRingInfo,
											&prevIndices,
											sizeof(u64),
											nextReadLocation);

	/* Make sure all reads are done before we update the read index since */
	/* the writer may start writing to the read area once the read index is updated */
	mb();

	/* Update the read index */
	SetNextReadLocation(InRingInfo, nextReadLocation);

	/* DumpRingInfo(InRingInfo, "AFTER "); */

	spin_unlock_irqrestore(&InRingInfo->ring_lock, flags);

	return 0;
}


/*++

Name:
	CopyToRingBuffer()

Description:
	Helper routine to copy from source to ring buffer.
	Assume there is enough room. Handles wrap-around in dest case only!!

--*/
static u32
CopyToRingBuffer(
	RING_BUFFER_INFO	*RingInfo,
	u32				StartWriteOffset,
	void *				Src,
	u32				SrcLen)
{
	void * ringBuffer=GetRingBuffer(RingInfo);
	u32 ringBufferSize=GetRingBufferSize(RingInfo);
	u32 fragLen;

	if (SrcLen > ringBufferSize - StartWriteOffset) /* wrap-around detected! */
	{
		DPRINT_DBG(VMBUS, "wrap-around detected!");

		fragLen = ringBufferSize - StartWriteOffset;
		memcpy(ringBuffer + StartWriteOffset, Src, fragLen);
		memcpy(ringBuffer, Src + fragLen, SrcLen - fragLen);
	}
	else
	{
		memcpy(ringBuffer + StartWriteOffset, Src, SrcLen);
	}

	StartWriteOffset += SrcLen;
	StartWriteOffset %= ringBufferSize;

	return StartWriteOffset;
}


/*++

Name:
	CopyFromRingBuffer()

Description:
	Helper routine to copy to source from ring buffer.
	Assume there is enough room. Handles wrap-around in src case only!!

--*/
static u32
CopyFromRingBuffer(
	RING_BUFFER_INFO	*RingInfo,
	void *				Dest,
	u32				DestLen,
	u32				StartReadOffset)
{
	void * ringBuffer=GetRingBuffer(RingInfo);
	u32 ringBufferSize=GetRingBufferSize(RingInfo);

	u32 fragLen;

	if (DestLen > ringBufferSize - StartReadOffset) /* wrap-around detected at the src */
	{
		DPRINT_DBG(VMBUS, "src wrap-around detected!");

		fragLen = ringBufferSize - StartReadOffset;

		memcpy(Dest, ringBuffer + StartReadOffset, fragLen);
		memcpy(Dest + fragLen, ringBuffer, DestLen - fragLen);
	}
	else
	{
		memcpy(Dest, ringBuffer + StartReadOffset, DestLen);
	}

	StartReadOffset += DestLen;
	StartReadOffset %= ringBufferSize;

	return StartReadOffset;
}


/* eof */
