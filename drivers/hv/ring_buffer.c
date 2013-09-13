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
 *   K. Y. Srinivasan <kys@microsoft.com>
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/hyperv.h>

#include "hyperv_vmbus.h"

void hv_begin_read(struct hv_ring_buffer_info *rbi)
{
	rbi->ring_buffer->interrupt_mask = 1;
	mb();
}

u32 hv_end_read(struct hv_ring_buffer_info *rbi)
{
	u32 read;
	u32 write;

	rbi->ring_buffer->interrupt_mask = 0;
	mb();

	/*
	 * Now check to see if the ring buffer is still empty.
	 * If it is not, we raced and we need to process new
	 * incoming messages.
	 */
	hv_get_ringbuffer_availbytes(rbi, &read, &write);

	return read;
}

/*
 * When we write to the ring buffer, check if the host needs to
 * be signaled. Here is the details of this protocol:
 *
 *	1. The host guarantees that while it is draining the
 *	   ring buffer, it will set the interrupt_mask to
 *	   indicate it does not need to be interrupted when
 *	   new data is placed.
 *
 *	2. The host guarantees that it will completely drain
 *	   the ring buffer before exiting the read loop. Further,
 *	   once the ring buffer is empty, it will clear the
 *	   interrupt_mask and re-check to see if new data has
 *	   arrived.
 */

static bool hv_need_to_signal(u32 old_write, struct hv_ring_buffer_info *rbi)
{
	mb();
	if (rbi->ring_buffer->interrupt_mask)
		return false;

	/* check interrupt_mask before read_index */
	rmb();
	/*
	 * This is the only case we need to signal when the
	 * ring transitions from being empty to non-empty.
	 */
	if (old_write == rbi->ring_buffer->read_index)
		return true;

	return false;
}

/*
 * To optimize the flow management on the send-side,
 * when the sender is blocked because of lack of
 * sufficient space in the ring buffer, potential the
 * consumer of the ring buffer can signal the producer.
 * This is controlled by the following parameters:
 *
 * 1. pending_send_sz: This is the size in bytes that the
 *    producer is trying to send.
 * 2. The feature bit feat_pending_send_sz set to indicate if
 *    the consumer of the ring will signal when the ring
 *    state transitions from being full to a state where
 *    there is room for the producer to send the pending packet.
 */

static bool hv_need_to_signal_on_read(u32 old_rd,
					 struct hv_ring_buffer_info *rbi)
{
	u32 prev_write_sz;
	u32 cur_write_sz;
	u32 r_size;
	u32 write_loc = rbi->ring_buffer->write_index;
	u32 read_loc = rbi->ring_buffer->read_index;
	u32 pending_sz = rbi->ring_buffer->pending_send_sz;

	/*
	 * If the other end is not blocked on write don't bother.
	 */
	if (pending_sz == 0)
		return false;

	r_size = rbi->ring_datasize;
	cur_write_sz = write_loc >= read_loc ? r_size - (write_loc - read_loc) :
			read_loc - write_loc;

	prev_write_sz = write_loc >= old_rd ? r_size - (write_loc - old_rd) :
			old_rd - write_loc;


	if ((prev_write_sz < pending_sz) && (cur_write_sz >= pending_sz))
		return true;

	return false;
}

/*
 * hv_get_next_write_location()
 *
 * Get the next write location for the specified ring buffer
 *
 */
static inline u32
hv_get_next_write_location(struct hv_ring_buffer_info *ring_info)
{
	u32 next = ring_info->ring_buffer->write_index;

	return next;
}

/*
 * hv_set_next_write_location()
 *
 * Set the next write location for the specified ring buffer
 *
 */
static inline void
hv_set_next_write_location(struct hv_ring_buffer_info *ring_info,
		     u32 next_write_location)
{
	ring_info->ring_buffer->write_index = next_write_location;
}

/*
 * hv_get_next_read_location()
 *
 * Get the next read location for the specified ring buffer
 */
static inline u32
hv_get_next_read_location(struct hv_ring_buffer_info *ring_info)
{
	u32 next = ring_info->ring_buffer->read_index;

	return next;
}

/*
 * hv_get_next_readlocation_withoffset()
 *
 * Get the next read location + offset for the specified ring buffer.
 * This allows the caller to skip
 */
static inline u32
hv_get_next_readlocation_withoffset(struct hv_ring_buffer_info *ring_info,
				 u32 offset)
{
	u32 next = ring_info->ring_buffer->read_index;

	next += offset;
	next %= ring_info->ring_datasize;

	return next;
}

/*
 *
 * hv_set_next_read_location()
 *
 * Set the next read location for the specified ring buffer
 *
 */
static inline void
hv_set_next_read_location(struct hv_ring_buffer_info *ring_info,
		    u32 next_read_location)
{
	ring_info->ring_buffer->read_index = next_read_location;
}


/*
 *
 * hv_get_ring_buffer()
 *
 * Get the start of the ring buffer
 */
static inline void *
hv_get_ring_buffer(struct hv_ring_buffer_info *ring_info)
{
	return (void *)ring_info->ring_buffer->buffer;
}


/*
 *
 * hv_get_ring_buffersize()
 *
 * Get the size of the ring buffer
 */
static inline u32
hv_get_ring_buffersize(struct hv_ring_buffer_info *ring_info)
{
	return ring_info->ring_datasize;
}

/*
 *
 * hv_get_ring_bufferindices()
 *
 * Get the read and write indices as u64 of the specified ring buffer
 *
 */
static inline u64
hv_get_ring_bufferindices(struct hv_ring_buffer_info *ring_info)
{
	return (u64)ring_info->ring_buffer->write_index << 32;
}

/*
 *
 * hv_copyfrom_ringbuffer()
 *
 * Helper routine to copy to source from ring buffer.
 * Assume there is enough room. Handles wrap-around in src case only!!
 *
 */
static u32 hv_copyfrom_ringbuffer(
	struct hv_ring_buffer_info	*ring_info,
	void				*dest,
	u32				destlen,
	u32				start_read_offset)
{
	void *ring_buffer = hv_get_ring_buffer(ring_info);
	u32 ring_buffer_size = hv_get_ring_buffersize(ring_info);

	u32 frag_len;

	/* wrap-around detected at the src */
	if (destlen > ring_buffer_size - start_read_offset) {
		frag_len = ring_buffer_size - start_read_offset;

		memcpy(dest, ring_buffer + start_read_offset, frag_len);
		memcpy(dest + frag_len, ring_buffer, destlen - frag_len);
	} else

		memcpy(dest, ring_buffer + start_read_offset, destlen);


	start_read_offset += destlen;
	start_read_offset %= ring_buffer_size;

	return start_read_offset;
}


/*
 *
 * hv_copyto_ringbuffer()
 *
 * Helper routine to copy from source to ring buffer.
 * Assume there is enough room. Handles wrap-around in dest case only!!
 *
 */
static u32 hv_copyto_ringbuffer(
	struct hv_ring_buffer_info	*ring_info,
	u32				start_write_offset,
	void				*src,
	u32				srclen)
{
	void *ring_buffer = hv_get_ring_buffer(ring_info);
	u32 ring_buffer_size = hv_get_ring_buffersize(ring_info);
	u32 frag_len;

	/* wrap-around detected! */
	if (srclen > ring_buffer_size - start_write_offset) {
		frag_len = ring_buffer_size - start_write_offset;
		memcpy(ring_buffer + start_write_offset, src, frag_len);
		memcpy(ring_buffer, src + frag_len, srclen - frag_len);
	} else
		memcpy(ring_buffer + start_write_offset, src, srclen);

	start_write_offset += srclen;
	start_write_offset %= ring_buffer_size;

	return start_write_offset;
}

/*
 *
 * hv_ringbuffer_get_debuginfo()
 *
 * Get various debug metrics for the specified ring buffer
 *
 */
void hv_ringbuffer_get_debuginfo(struct hv_ring_buffer_info *ring_info,
			    struct hv_ring_buffer_debug_info *debug_info)
{
	u32 bytes_avail_towrite;
	u32 bytes_avail_toread;

	if (ring_info->ring_buffer) {
		hv_get_ringbuffer_availbytes(ring_info,
					&bytes_avail_toread,
					&bytes_avail_towrite);

		debug_info->bytes_avail_toread = bytes_avail_toread;
		debug_info->bytes_avail_towrite = bytes_avail_towrite;
		debug_info->current_read_index =
			ring_info->ring_buffer->read_index;
		debug_info->current_write_index =
			ring_info->ring_buffer->write_index;
		debug_info->current_interrupt_mask =
			ring_info->ring_buffer->interrupt_mask;
	}
}

/*
 *
 * hv_ringbuffer_init()
 *
 *Initialize the ring buffer
 *
 */
int hv_ringbuffer_init(struct hv_ring_buffer_info *ring_info,
		   void *buffer, u32 buflen)
{
	if (sizeof(struct hv_ring_buffer) != PAGE_SIZE)
		return -EINVAL;

	memset(ring_info, 0, sizeof(struct hv_ring_buffer_info));

	ring_info->ring_buffer = (struct hv_ring_buffer *)buffer;
	ring_info->ring_buffer->read_index =
		ring_info->ring_buffer->write_index = 0;

	ring_info->ring_size = buflen;
	ring_info->ring_datasize = buflen - sizeof(struct hv_ring_buffer);

	spin_lock_init(&ring_info->ring_lock);

	return 0;
}

/*
 *
 * hv_ringbuffer_cleanup()
 *
 * Cleanup the ring buffer
 *
 */
void hv_ringbuffer_cleanup(struct hv_ring_buffer_info *ring_info)
{
}

/*
 *
 * hv_ringbuffer_write()
 *
 * Write to the ring buffer
 *
 */
int hv_ringbuffer_write(struct hv_ring_buffer_info *outring_info,
		    struct scatterlist *sglist, u32 sgcount, bool *signal)
{
	int i = 0;
	u32 bytes_avail_towrite;
	u32 bytes_avail_toread;
	u32 totalbytes_towrite = 0;

	struct scatterlist *sg;
	u32 next_write_location;
	u32 old_write;
	u64 prev_indices = 0;
	unsigned long flags;

	for_each_sg(sglist, sg, sgcount, i)
	{
		totalbytes_towrite += sg->length;
	}

	totalbytes_towrite += sizeof(u64);

	spin_lock_irqsave(&outring_info->ring_lock, flags);

	hv_get_ringbuffer_availbytes(outring_info,
				&bytes_avail_toread,
				&bytes_avail_towrite);


	/* If there is only room for the packet, assume it is full. */
	/* Otherwise, the next time around, we think the ring buffer */
	/* is empty since the read index == write index */
	if (bytes_avail_towrite <= totalbytes_towrite) {
		spin_unlock_irqrestore(&outring_info->ring_lock, flags);
		return -EAGAIN;
	}

	/* Write to the ring buffer */
	next_write_location = hv_get_next_write_location(outring_info);

	old_write = next_write_location;

	for_each_sg(sglist, sg, sgcount, i)
	{
		next_write_location = hv_copyto_ringbuffer(outring_info,
						     next_write_location,
						     sg_virt(sg),
						     sg->length);
	}

	/* Set previous packet start */
	prev_indices = hv_get_ring_bufferindices(outring_info);

	next_write_location = hv_copyto_ringbuffer(outring_info,
					     next_write_location,
					     &prev_indices,
					     sizeof(u64));

	/* Issue a full memory barrier before updating the write index */
	mb();

	/* Now, update the write location */
	hv_set_next_write_location(outring_info, next_write_location);


	spin_unlock_irqrestore(&outring_info->ring_lock, flags);

	*signal = hv_need_to_signal(old_write, outring_info);
	return 0;
}


/*
 *
 * hv_ringbuffer_peek()
 *
 * Read without advancing the read index
 *
 */
int hv_ringbuffer_peek(struct hv_ring_buffer_info *Inring_info,
		   void *Buffer, u32 buflen)
{
	u32 bytes_avail_towrite;
	u32 bytes_avail_toread;
	u32 next_read_location = 0;
	unsigned long flags;

	spin_lock_irqsave(&Inring_info->ring_lock, flags);

	hv_get_ringbuffer_availbytes(Inring_info,
				&bytes_avail_toread,
				&bytes_avail_towrite);

	/* Make sure there is something to read */
	if (bytes_avail_toread < buflen) {

		spin_unlock_irqrestore(&Inring_info->ring_lock, flags);

		return -EAGAIN;
	}

	/* Convert to byte offset */
	next_read_location = hv_get_next_read_location(Inring_info);

	next_read_location = hv_copyfrom_ringbuffer(Inring_info,
						Buffer,
						buflen,
						next_read_location);

	spin_unlock_irqrestore(&Inring_info->ring_lock, flags);

	return 0;
}


/*
 *
 * hv_ringbuffer_read()
 *
 * Read and advance the read index
 *
 */
int hv_ringbuffer_read(struct hv_ring_buffer_info *inring_info, void *buffer,
		   u32 buflen, u32 offset, bool *signal)
{
	u32 bytes_avail_towrite;
	u32 bytes_avail_toread;
	u32 next_read_location = 0;
	u64 prev_indices = 0;
	unsigned long flags;
	u32 old_read;

	if (buflen <= 0)
		return -EINVAL;

	spin_lock_irqsave(&inring_info->ring_lock, flags);

	hv_get_ringbuffer_availbytes(inring_info,
				&bytes_avail_toread,
				&bytes_avail_towrite);

	old_read = bytes_avail_toread;

	/* Make sure there is something to read */
	if (bytes_avail_toread < buflen) {
		spin_unlock_irqrestore(&inring_info->ring_lock, flags);

		return -EAGAIN;
	}

	next_read_location =
		hv_get_next_readlocation_withoffset(inring_info, offset);

	next_read_location = hv_copyfrom_ringbuffer(inring_info,
						buffer,
						buflen,
						next_read_location);

	next_read_location = hv_copyfrom_ringbuffer(inring_info,
						&prev_indices,
						sizeof(u64),
						next_read_location);

	/* Make sure all reads are done before we update the read index since */
	/* the writer may start writing to the read area once the read index */
	/*is updated */
	mb();

	/* Update the read index */
	hv_set_next_read_location(inring_info, next_read_location);

	spin_unlock_irqrestore(&inring_info->ring_lock, flags);

	*signal = hv_need_to_signal_on_read(old_read, inring_info);

	return 0;
}
