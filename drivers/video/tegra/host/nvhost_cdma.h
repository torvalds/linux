/*
 * drivers/video/tegra/host/nvhost_cdma.h
 *
 * Tegra Graphics Host Command DMA
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __NVHOST_CDMA_H
#define __NVHOST_CDMA_H

#include <linux/sched.h>
#include <linux/semaphore.h>

#include <mach/nvhost.h>
#include <mach/nvmap.h>

#include "nvhost_acm.h"

/*
 * cdma
 *
 * This is in charge of a host command DMA channel.
 * Sends ops to a push buffer, and takes responsibility for unpinning
 * (& possibly freeing) of memory after those ops have completed.
 * Producer:
 *	begin
 *		push - send ops to the push buffer
 *	end - start command DMA and enqueue handles to be unpinned
 * Consumer:
 *	update - call to update sync queue and push buffer, unpin memory
 */

/* Size of the sync queue. If it is too small, we won't be able to queue up
 * many command buffers. If it is too large, we waste memory. */
#define NVHOST_SYNC_QUEUE_SIZE 8192

/* Number of gathers we allow to be queued up per channel. Must be a
   power of two. Currently sized such that pushbuffer is 4KB (512*8B). */
#define NVHOST_GATHER_QUEUE_SIZE 512

struct push_buffer {
	struct nvmap_handle_ref *mem; /* handle to pushbuffer memory */
	u32 *mapped;		/* mapped pushbuffer memory */
	u32 phys;		/* physical address of pushbuffer */
	u32 fence;		/* index we've written */
	u32 cur;		/* index to write to */
};

struct sync_queue {
	unsigned int read;		    /* read position within buffer */
	unsigned int write;		    /* write position within buffer */
	u32 buffer[NVHOST_SYNC_QUEUE_SIZE]; /* queue data */
};

enum cdma_event {
	CDMA_EVENT_NONE,		/* not waiting for any event */
	CDMA_EVENT_SYNC_QUEUE_EMPTY,	/* wait for empty sync queue */
	CDMA_EVENT_SYNC_QUEUE_SPACE,	/* wait for space in sync queue */
	CDMA_EVENT_PUSH_BUFFER_SPACE	/* wait for space in push buffer */
};

struct nvhost_cdma {
	struct mutex lock;		/* controls access to shared state */
	struct semaphore sem;		/* signalled when event occurs */
	enum cdma_event event;		/* event that sem is waiting for */
	unsigned int slots_used;	/* pb slots used in current submit */
	unsigned int slots_free;	/* pb slots free in current submit */
	unsigned int last_put;		/* last value written to DMAPUT */
	struct push_buffer push_buffer;	/* channel's push buffer */
	struct sync_queue sync_queue;	/* channel's sync queue */
	bool running;
};

int	nvhost_cdma_init(struct nvhost_cdma *cdma);
void	nvhost_cdma_deinit(struct nvhost_cdma *cdma);
void	nvhost_cdma_stop(struct nvhost_cdma *cdma);
void	nvhost_cdma_begin(struct nvhost_cdma *cdma);
void	nvhost_cdma_push(struct nvhost_cdma *cdma, u32 op1, u32 op2);
void	nvhost_cdma_end(struct nvmap_client *user_nvmap,
			struct nvhost_cdma *cdma,
			u32 sync_point_id, u32 sync_point_value,
			struct nvmap_handle **handles, unsigned int nr_handles);
void	nvhost_cdma_update(struct nvhost_cdma *cdma);
void	nvhost_cdma_flush(struct nvhost_cdma *cdma);
void    nvhost_cdma_find_gather(struct nvhost_cdma *cdma, u32 dmaget,
                u32 *addr, u32 *size);

#endif
