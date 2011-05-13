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


#ifndef _RING_BUFFER_H_
#define _RING_BUFFER_H_

#include <linux/scatterlist.h>


/* Interface */


int hv_ringbuffer_init(struct hv_ring_buffer_info *ring_info, void *buffer,
		   u32 buflen);

void hv_ringbuffer_cleanup(struct hv_ring_buffer_info *ring_info);

int hv_ringbuffer_write(struct hv_ring_buffer_info *ring_info,
		    struct scatterlist *sglist,
		    u32 sgcount);

int hv_ringbuffer_peek(struct hv_ring_buffer_info *ring_info, void *buffer,
		   u32 buflen);

int hv_ringbuffer_read(struct hv_ring_buffer_info *ring_info,
		   void *buffer,
		   u32 buflen,
		   u32 offset);

u32 hv_get_ringbuffer_interrupt_mask(struct hv_ring_buffer_info *ring_info);

void hv_dump_ring_info(struct hv_ring_buffer_info *ring_info, char *prefix);

void hv_ringbuffer_get_debuginfo(struct hv_ring_buffer_info *ring_info,
			    struct hv_ring_buffer_debug_info *debug_info);

#endif /* _RING_BUFFER_H_ */
