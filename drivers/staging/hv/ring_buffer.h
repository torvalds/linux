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

struct hv_ring_buffer {
	/* Offset in bytes from the start of ring data below */
	volatile u32 write_index;

	/* Offset in bytes from the start of ring data below */
	volatile u32 read_index;

	volatile u32 interrupt_mask;

	/* Pad it to PAGE_SIZE so that data starts on page boundary */
	u8	reserved[4084];

	/* NOTE:
	 * The interrupt_mask field is used only for channels but since our
	 * vmbus connection also uses this data structure and its data starts
	 * here, we commented out this field.
	 */
	/* volatile u32 InterruptMask; */

	/*
	 * Ring data starts here + RingDataStartOffset
	 * !!! DO NOT place any fields below this !!!
	 */
	u8 buffer[0];
} __packed;

struct hv_ring_buffer_info {
	struct hv_ring_buffer *ring_buffer;
	u32 ring_size;			/* Include the shared header */
	spinlock_t ring_lock;

	u32 ring_datasize;		/* < ring_size */
	u32 ring_data_startoffset;
};

struct hv_ring_buffer_debug_info {
	u32 current_interrupt_mask;
	u32 current_read_index;
	u32 current_write_index;
	u32 bytes_avail_toread;
	u32 bytes_avail_towrite;
};



/* Interface */


int ringbuffer_init(struct hv_ring_buffer_info *ring_info, void *buffer,
		   u32 buflen);

void ringbuffer_cleanup(struct hv_ring_buffer_info *ring_info);

int ringbuffer_write(struct hv_ring_buffer_info *ring_info,
		    struct scatterlist *sglist,
		    u32 sgcount);

int ringbuffer_peek(struct hv_ring_buffer_info *ring_info, void *buffer,
		   u32 buflen);

int ringbuffer_read(struct hv_ring_buffer_info *ring_info,
		   void *buffer,
		   u32 buflen,
		   u32 offset);

u32 get_ringbuffer_interrupt_mask(struct hv_ring_buffer_info *ring_info);

void dump_ring_info(struct hv_ring_buffer_info *ring_info, char *prefix);

void ringbuffer_get_debuginfo(struct hv_ring_buffer_info *ring_info,
			    struct hv_ring_buffer_debug_info *debug_info);

#endif /* _RING_BUFFER_H_ */
