/*
 * Tegra host1x Command DMA
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __HOST1X_CDMA_H
#define __HOST1X_CDMA_H

#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/list.h>

struct host1x_syncpt;
struct host1x_userctx_timeout;
struct host1x_job;

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

struct push_buffer {
	void *mapped;			/* mapped pushbuffer memory */
	dma_addr_t phys;		/* physical address of pushbuffer */
	u32 fence;			/* index we've written */
	u32 pos;			/* index to write to */
	u32 size_bytes;
};

struct buffer_timeout {
	struct delayed_work wq;		/* work queue */
	bool initialized;		/* timer one-time setup flag */
	struct host1x_syncpt *syncpt;	/* buffer completion syncpt */
	u32 syncpt_val;			/* syncpt value when completed */
	ktime_t start_ktime;		/* starting time */
	/* context timeout information */
	int client;
};

enum cdma_event {
	CDMA_EVENT_NONE,		/* not waiting for any event */
	CDMA_EVENT_SYNC_QUEUE_EMPTY,	/* wait for empty sync queue */
	CDMA_EVENT_PUSH_BUFFER_SPACE	/* wait for space in push buffer */
};

struct host1x_cdma {
	struct mutex lock;		/* controls access to shared state */
	struct semaphore sem;		/* signalled when event occurs */
	enum cdma_event event;		/* event that sem is waiting for */
	unsigned int slots_used;	/* pb slots used in current submit */
	unsigned int slots_free;	/* pb slots free in current submit */
	unsigned int first_get;		/* DMAGET value, where submit begins */
	unsigned int last_pos;		/* last value written to DMAPUT */
	struct push_buffer push_buffer;	/* channel's push buffer */
	struct list_head sync_queue;	/* job queue */
	struct buffer_timeout timeout;	/* channel's timeout state/wq */
	bool running;
	bool torndown;
};

#define cdma_to_channel(cdma) container_of(cdma, struct host1x_channel, cdma)
#define cdma_to_host1x(cdma) dev_get_drvdata(cdma_to_channel(cdma)->dev->parent)
#define pb_to_cdma(pb) container_of(pb, struct host1x_cdma, push_buffer)

int host1x_cdma_init(struct host1x_cdma *cdma);
int host1x_cdma_deinit(struct host1x_cdma *cdma);
void host1x_cdma_stop(struct host1x_cdma *cdma);
int host1x_cdma_begin(struct host1x_cdma *cdma, struct host1x_job *job);
void host1x_cdma_push(struct host1x_cdma *cdma, u32 op1, u32 op2);
void host1x_cdma_end(struct host1x_cdma *cdma, struct host1x_job *job);
void host1x_cdma_update(struct host1x_cdma *cdma);
void host1x_cdma_peek(struct host1x_cdma *cdma, u32 dmaget, int slot,
		      u32 *out);
unsigned int host1x_cdma_wait_locked(struct host1x_cdma *cdma,
				     enum cdma_event event);
void host1x_cdma_update_sync_queue(struct host1x_cdma *cdma,
				   struct device *dev);
#endif
