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
#include "ring_buffer.h"


/* #defines */


/* Amount of space to write to */
#define BYTES_AVAIL_TO_WRITE(r, w, z) ((w) >= (r)) ? ((z) - ((w) - (r))) : ((r) - (w))


/*++

Name:
	GetRingBufferAvailBytes()

Description:
	Get number of bytes available to read and to write to
	for the specified ring buffer

--*/
static inline void
GetRingBufferAvailBytes(struct hv_ring_buffer_info *rbi, u32 *read, u32 *write)
{
	u32 read_loc, write_loc;

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
GetNextWriteLocation(struct hv_ring_buffer_info *RingInfo)
{
	u32 next = RingInfo->RingBuffer->WriteIndex;

	/* ASSERT(next < RingInfo->RingDataSize); */

	return next;
}

/*++

Name:
	SetNextWriteLocation()

Description:
	Set the next write location for the specified ring buffer

--*/
static inline void
SetNextWriteLocation(struct hv_ring_buffer_info *RingInfo,
		     u32 NextWriteLocation)
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
GetNextReadLocation(struct hv_ring_buffer_info *RingInfo)
{
	u32 next = RingInfo->RingBuffer->ReadIndex;

	/* ASSERT(next < RingInfo->RingDataSize); */

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
GetNextReadLocationWithOffset(struct hv_ring_buffer_info *RingInfo, u32 Offset)
{
	u32 next = RingInfo->RingBuffer->ReadIndex;

	/* ASSERT(next < RingInfo->RingDataSize); */
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
SetNextReadLocation(struct hv_ring_buffer_info *RingInfo, u32 NextReadLocation)
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
GetRingBuffer(struct hv_ring_buffer_info *RingInfo)
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
GetRingBufferSize(struct hv_ring_buffer_info *RingInfo)
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
GetRingBufferIndices(struct hv_ring_buffer_info *RingInfo)
{
	return (u64)RingInfo->RingBuffer->WriteIndex << 32;
}


/*++

Name:
	DumpRingInfo()

Description:
	Dump out to console the ring buffer info

--*/
void DumpRingInfo(struct hv_ring_buffer_info *RingInfo, char *Prefix)
{
	u32 bytesAvailToWrite;
	u32 bytesAvailToRead;

	GetRingBufferAvailBytes(RingInfo,
	&bytesAvailToRead,
	&bytesAvailToWrite);

	DPRINT(VMBUS,
		DEBUG_RING_LVL,
		"%s <<ringinfo %p buffer %p avail write %u "
		"avail read %u read idx %u write idx %u>>",
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
	struct hv_ring_buffer_info	*RingInfo,
	u32				StartWriteOffset,
	void				*Src,
	u32				SrcLen);

static u32
CopyFromRingBuffer(
	struct hv_ring_buffer_info	*RingInfo,
	void				*Dest,
	u32				DestLen,
	u32				StartReadOffset);



/*++

Name:
	RingBufferGetDebugInfo()

Description:
	Get various debug metrics for the specified ring buffer

--*/
void RingBufferGetDebugInfo(struct hv_ring_buffer_info *RingInfo,
			    struct hv_ring_buffer_debug_info *debug_info)
{
	u32 bytesAvailToWrite;
	u32 bytesAvailToRead;

	if (RingInfo->RingBuffer) {
		GetRingBufferAvailBytes(RingInfo,
					&bytesAvailToRead,
					&bytesAvailToWrite);

		debug_info->BytesAvailToRead = bytesAvailToRead;
		debug_info->BytesAvailToWrite = bytesAvailToWrite;
		debug_info->CurrentReadIndex = RingInfo->RingBuffer->ReadIndex;
		debug_info->CurrentWriteIndex = RingInfo->RingBuffer->WriteIndex;
		debug_info->CurrentInterruptMask = RingInfo->RingBuffer->InterruptMask;
	}
}


/*++

Name:
	GetRingBufferInterruptMask()

Description:
	Get the interrupt mask for the specified ring buffer

--*/
u32 GetRingBufferInterruptMask(struct hv_ring_buffer_info *rbi)
{
	return rbi->RingBuffer->InterruptMask;
}

/*++

Name:
	RingBufferInit()

Description:
	Initialize the ring buffer

--*/
int RingBufferInit(struct hv_ring_buffer_info *RingInfo, void *Buffer, u32 BufferLen)
{
	if (sizeof(struct hv_ring_buffer) != PAGE_SIZE)
		return -EINVAL;

	memset(RingInfo, 0, sizeof(struct hv_ring_buffer_info));

	RingInfo->RingBuffer = (struct hv_ring_buffer *)Buffer;
	RingInfo->RingBuffer->ReadIndex = RingInfo->RingBuffer->WriteIndex = 0;

	RingInfo->RingSize = BufferLen;
	RingInfo->RingDataSize = BufferLen - sizeof(struct hv_ring_buffer);

	spin_lock_init(&RingInfo->ring_lock);

	return 0;
}

/*++

Name:
	RingBufferCleanup()

Description:
	Cleanup the ring buffer

--*/
void RingBufferCleanup(struct hv_ring_buffer_info *RingInfo)
{
}

/*++

Name:
	RingBufferWrite()

Description:
	Write to the ring buffer

--*/
int RingBufferWrite(struct hv_ring_buffer_info *OutRingInfo,
		    struct scatterlist *sglist, u32 sgcount)
{
	int i = 0;
	u32 byteAvailToWrite;
	u32 byteAvailToRead;
	u32 totalBytesToWrite = 0;

	struct scatterlist *sg;
	volatile u32 nextWriteLocation;
	u64 prevIndices = 0;
	unsigned long flags;

	for_each_sg(sglist, sg, sgcount, i)
	{
		totalBytesToWrite += sg->length;
	}

	totalBytesToWrite += sizeof(u64);

	spin_lock_irqsave(&OutRingInfo->ring_lock, flags);

	GetRingBufferAvailBytes(OutRingInfo,
				&byteAvailToRead,
				&byteAvailToWrite);

	DPRINT_DBG(VMBUS, "Writing %u bytes...", totalBytesToWrite);

	/* DumpRingInfo(OutRingInfo, "BEFORE "); */

	/* If there is only room for the packet, assume it is full. */
	/* Otherwise, the next time around, we think the ring buffer */
	/* is empty since the read index == write index */
	if (byteAvailToWrite <= totalBytesToWrite) {
		DPRINT_DBG(VMBUS,
			"No more space left on outbound ring buffer "
			"(needed %u, avail %u)",
			totalBytesToWrite,
			byteAvailToWrite);

		spin_unlock_irqrestore(&OutRingInfo->ring_lock, flags);
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
	return 0;
}


/*++

Name:
	RingBufferPeek()

Description:
	Read without advancing the read index

--*/
int RingBufferPeek(struct hv_ring_buffer_info *InRingInfo, void *Buffer, u32 BufferLen)
{
	u32 bytesAvailToWrite;
	u32 bytesAvailToRead;
	u32 nextReadLocation = 0;
	unsigned long flags;

	spin_lock_irqsave(&InRingInfo->ring_lock, flags);

	GetRingBufferAvailBytes(InRingInfo,
				&bytesAvailToRead,
				&bytesAvailToWrite);

	/* Make sure there is something to read */
	if (bytesAvailToRead < BufferLen) {
		/* DPRINT_DBG(VMBUS,
			"got callback but not enough to read "
			"<avail to read %d read size %d>!!",
			bytesAvailToRead,
			BufferLen); */

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
int RingBufferRead(struct hv_ring_buffer_info *InRingInfo, void *Buffer,
		   u32 BufferLen, u32 Offset)
{
	u32 bytesAvailToWrite;
	u32 bytesAvailToRead;
	u32 nextReadLocation = 0;
	u64 prevIndices = 0;
	unsigned long flags;

	if (BufferLen <= 0)
		return -EINVAL;

	spin_lock_irqsave(&InRingInfo->ring_lock, flags);

	GetRingBufferAvailBytes(InRingInfo,
				&bytesAvailToRead,
				&bytesAvailToWrite);

	DPRINT_DBG(VMBUS, "Reading %u bytes...", BufferLen);

	/* DumpRingInfo(InRingInfo, "BEFORE "); */

	/* Make sure there is something to read */
	if (bytesAvailToRead < BufferLen) {
		DPRINT_DBG(VMBUS,
			"got callback but not enough to read "
			"<avail to read %d read size %d>!!",
			bytesAvailToRead,
			BufferLen);

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
	/* the writer may start writing to the read area once the read index */
	/*is updated */
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
	struct hv_ring_buffer_info	*RingInfo,
	u32				StartWriteOffset,
	void				*Src,
	u32				SrcLen)
{
	void *ringBuffer = GetRingBuffer(RingInfo);
	u32 ringBufferSize = GetRingBufferSize(RingInfo);
	u32 fragLen;

	/* wrap-around detected! */
	if (SrcLen > ringBufferSize - StartWriteOffset) {
		DPRINT_DBG(VMBUS, "wrap-around detected!");

		fragLen = ringBufferSize - StartWriteOffset;
		memcpy(ringBuffer + StartWriteOffset, Src, fragLen);
		memcpy(ringBuffer, Src + fragLen, SrcLen - fragLen);
	} else
		memcpy(ringBuffer + StartWriteOffset, Src, SrcLen);

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
	struct hv_ring_buffer_info	*RingInfo,
	void				*Dest,
	u32				DestLen,
	u32				StartReadOffset)
{
	void *ringBuffer = GetRingBuffer(RingInfo);
	u32 ringBufferSize = GetRingBufferSize(RingInfo);

	u32 fragLen;

	/* wrap-around detected at the src */
	if (DestLen > ringBufferSize - StartReadOffset) {
		DPRINT_DBG(VMBUS, "src wrap-around detected!");

		fragLen = ringBufferSize - StartReadOffset;

		memcpy(Dest, ringBuffer + StartReadOffset, fragLen);
		memcpy(Dest + fragLen, ringBuffer, DestLen - fragLen);
	} else

		memcpy(Dest, ringBuffer + StartReadOffset, DestLen);


	StartReadOffset += DestLen;
	StartReadOffset %= ringBufferSize;

	return StartReadOffset;
}


/* eof */
