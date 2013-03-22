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

#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

#include "cdma.h"
#include "channel.h"
#include "dev.h"
#include "debug.h"

/*
 * Put the restart at the end of pushbuffer memor
 */
static void push_buffer_init(struct push_buffer *pb)
{
	*(pb->mapped + (pb->size_bytes >> 2)) = host1x_opcode_restart(0);
}

/*
 * Increment timedout buffer's syncpt via CPU.
 */
static void cdma_timeout_cpu_incr(struct host1x_cdma *cdma, u32 getptr,
				u32 syncpt_incrs, u32 syncval, u32 nr_slots)
{
	struct host1x *host1x = cdma_to_host1x(cdma);
	struct push_buffer *pb = &cdma->push_buffer;
	u32 i;

	for (i = 0; i < syncpt_incrs; i++)
		host1x_syncpt_cpu_incr(cdma->timeout.syncpt);

	/* after CPU incr, ensure shadow is up to date */
	host1x_syncpt_load(cdma->timeout.syncpt);

	/* NOP all the PB slots */
	while (nr_slots--) {
		u32 *p = (u32 *)((u32)pb->mapped + getptr);
		*(p++) = HOST1X_OPCODE_NOP;
		*(p++) = HOST1X_OPCODE_NOP;
		dev_dbg(host1x->dev, "%s: NOP at 0x%x\n", __func__,
			pb->phys + getptr);
		getptr = (getptr + 8) & (pb->size_bytes - 1);
	}
	wmb();
}

/*
 * Start channel DMA
 */
static void cdma_start(struct host1x_cdma *cdma)
{
	struct host1x_channel *ch = cdma_to_channel(cdma);

	if (cdma->running)
		return;

	cdma->last_pos = cdma->push_buffer.pos;

	host1x_ch_writel(ch, HOST1X_CHANNEL_DMACTRL_DMASTOP,
			 HOST1X_CHANNEL_DMACTRL);

	/* set base, put and end pointer */
	host1x_ch_writel(ch, cdma->push_buffer.phys, HOST1X_CHANNEL_DMASTART);
	host1x_ch_writel(ch, cdma->push_buffer.pos, HOST1X_CHANNEL_DMAPUT);
	host1x_ch_writel(ch, cdma->push_buffer.phys +
			 cdma->push_buffer.size_bytes + 4,
			 HOST1X_CHANNEL_DMAEND);

	/* reset GET */
	host1x_ch_writel(ch, HOST1X_CHANNEL_DMACTRL_DMASTOP |
			 HOST1X_CHANNEL_DMACTRL_DMAGETRST |
			 HOST1X_CHANNEL_DMACTRL_DMAINITGET,
			 HOST1X_CHANNEL_DMACTRL);

	/* start the command DMA */
	host1x_ch_writel(ch, 0, HOST1X_CHANNEL_DMACTRL);

	cdma->running = true;
}

/*
 * Similar to cdma_start(), but rather than starting from an idle
 * state (where DMA GET is set to DMA PUT), on a timeout we restore
 * DMA GET from an explicit value (so DMA may again be pending).
 */
static void cdma_timeout_restart(struct host1x_cdma *cdma, u32 getptr)
{
	struct host1x *host1x = cdma_to_host1x(cdma);
	struct host1x_channel *ch = cdma_to_channel(cdma);

	if (cdma->running)
		return;

	cdma->last_pos = cdma->push_buffer.pos;

	host1x_ch_writel(ch, HOST1X_CHANNEL_DMACTRL_DMASTOP,
			 HOST1X_CHANNEL_DMACTRL);

	/* set base, end pointer (all of memory) */
	host1x_ch_writel(ch, cdma->push_buffer.phys, HOST1X_CHANNEL_DMASTART);
	host1x_ch_writel(ch, cdma->push_buffer.phys +
			 cdma->push_buffer.size_bytes,
			 HOST1X_CHANNEL_DMAEND);

	/* set GET, by loading the value in PUT (then reset GET) */
	host1x_ch_writel(ch, getptr, HOST1X_CHANNEL_DMAPUT);
	host1x_ch_writel(ch, HOST1X_CHANNEL_DMACTRL_DMASTOP |
			 HOST1X_CHANNEL_DMACTRL_DMAGETRST |
			 HOST1X_CHANNEL_DMACTRL_DMAINITGET,
			 HOST1X_CHANNEL_DMACTRL);

	dev_dbg(host1x->dev,
		"%s: DMA GET 0x%x, PUT HW 0x%x / shadow 0x%x\n", __func__,
		host1x_ch_readl(ch, HOST1X_CHANNEL_DMAGET),
		host1x_ch_readl(ch, HOST1X_CHANNEL_DMAPUT),
		cdma->last_pos);

	/* deassert GET reset and set PUT */
	host1x_ch_writel(ch, HOST1X_CHANNEL_DMACTRL_DMASTOP,
			 HOST1X_CHANNEL_DMACTRL);
	host1x_ch_writel(ch, cdma->push_buffer.pos, HOST1X_CHANNEL_DMAPUT);

	/* start the command DMA */
	host1x_ch_writel(ch, 0, HOST1X_CHANNEL_DMACTRL);

	cdma->running = true;
}

/*
 * Kick channel DMA into action by writing its PUT offset (if it has changed)
 */
static void cdma_flush(struct host1x_cdma *cdma)
{
	struct host1x_channel *ch = cdma_to_channel(cdma);

	if (cdma->push_buffer.pos != cdma->last_pos) {
		host1x_ch_writel(ch, cdma->push_buffer.pos,
				 HOST1X_CHANNEL_DMAPUT);
		cdma->last_pos = cdma->push_buffer.pos;
	}
}

static void cdma_stop(struct host1x_cdma *cdma)
{
	struct host1x_channel *ch = cdma_to_channel(cdma);

	mutex_lock(&cdma->lock);
	if (cdma->running) {
		host1x_cdma_wait_locked(cdma, CDMA_EVENT_SYNC_QUEUE_EMPTY);
		host1x_ch_writel(ch, HOST1X_CHANNEL_DMACTRL_DMASTOP,
				 HOST1X_CHANNEL_DMACTRL);
		cdma->running = false;
	}
	mutex_unlock(&cdma->lock);
}

/*
 * Stops both channel's command processor and CDMA immediately.
 * Also, tears down the channel and resets corresponding module.
 */
static void cdma_freeze(struct host1x_cdma *cdma)
{
	struct host1x *host = cdma_to_host1x(cdma);
	struct host1x_channel *ch = cdma_to_channel(cdma);
	u32 cmdproc_stop;

	if (cdma->torndown && !cdma->running) {
		dev_warn(host->dev, "Already torn down\n");
		return;
	}

	dev_dbg(host->dev, "freezing channel (id %d)\n", ch->id);

	cmdproc_stop = host1x_sync_readl(host, HOST1X_SYNC_CMDPROC_STOP);
	cmdproc_stop |= BIT(ch->id);
	host1x_sync_writel(host, cmdproc_stop, HOST1X_SYNC_CMDPROC_STOP);

	dev_dbg(host->dev, "%s: DMA GET 0x%x, PUT HW 0x%x / shadow 0x%x\n",
		__func__, host1x_ch_readl(ch, HOST1X_CHANNEL_DMAGET),
		host1x_ch_readl(ch, HOST1X_CHANNEL_DMAPUT),
		cdma->last_pos);

	host1x_ch_writel(ch, HOST1X_CHANNEL_DMACTRL_DMASTOP,
			 HOST1X_CHANNEL_DMACTRL);

	host1x_sync_writel(host, BIT(ch->id), HOST1X_SYNC_CH_TEARDOWN);

	cdma->running = false;
	cdma->torndown = true;
}

static void cdma_resume(struct host1x_cdma *cdma, u32 getptr)
{
	struct host1x *host1x = cdma_to_host1x(cdma);
	struct host1x_channel *ch = cdma_to_channel(cdma);
	u32 cmdproc_stop;

	dev_dbg(host1x->dev,
		"resuming channel (id %d, DMAGET restart = 0x%x)\n",
		ch->id, getptr);

	cmdproc_stop = host1x_sync_readl(host1x, HOST1X_SYNC_CMDPROC_STOP);
	cmdproc_stop &= ~(BIT(ch->id));
	host1x_sync_writel(host1x, cmdproc_stop, HOST1X_SYNC_CMDPROC_STOP);

	cdma->torndown = false;
	cdma_timeout_restart(cdma, getptr);
}

/*
 * If this timeout fires, it indicates the current sync_queue entry has
 * exceeded its TTL and the userctx should be timed out and remaining
 * submits already issued cleaned up (future submits return an error).
 */
static void cdma_timeout_handler(struct work_struct *work)
{
	struct host1x_cdma *cdma;
	struct host1x *host1x;
	struct host1x_channel *ch;

	u32 syncpt_val;

	u32 prev_cmdproc, cmdproc_stop;

	cdma = container_of(to_delayed_work(work), struct host1x_cdma,
			    timeout.wq);
	host1x = cdma_to_host1x(cdma);
	ch = cdma_to_channel(cdma);

	host1x_debug_dump(cdma_to_host1x(cdma));

	mutex_lock(&cdma->lock);

	if (!cdma->timeout.client) {
		dev_dbg(host1x->dev,
			"cdma_timeout: expired, but has no clientid\n");
		mutex_unlock(&cdma->lock);
		return;
	}

	/* stop processing to get a clean snapshot */
	prev_cmdproc = host1x_sync_readl(host1x, HOST1X_SYNC_CMDPROC_STOP);
	cmdproc_stop = prev_cmdproc | BIT(ch->id);
	host1x_sync_writel(host1x, cmdproc_stop, HOST1X_SYNC_CMDPROC_STOP);

	dev_dbg(host1x->dev, "cdma_timeout: cmdproc was 0x%x is 0x%x\n",
		prev_cmdproc, cmdproc_stop);

	syncpt_val = host1x_syncpt_load(cdma->timeout.syncpt);

	/* has buffer actually completed? */
	if ((s32)(syncpt_val - cdma->timeout.syncpt_val) >= 0) {
		dev_dbg(host1x->dev,
			"cdma_timeout: expired, but buffer had completed\n");
		/* restore */
		cmdproc_stop = prev_cmdproc & ~(BIT(ch->id));
		host1x_sync_writel(host1x, cmdproc_stop,
				   HOST1X_SYNC_CMDPROC_STOP);
		mutex_unlock(&cdma->lock);
		return;
	}

	dev_warn(host1x->dev, "%s: timeout: %d (%s), HW thresh %d, done %d\n",
		__func__, cdma->timeout.syncpt->id, cdma->timeout.syncpt->name,
		syncpt_val, cdma->timeout.syncpt_val);

	/* stop HW, resetting channel/module */
	host1x_hw_cdma_freeze(host1x, cdma);

	host1x_cdma_update_sync_queue(cdma, ch->dev);
	mutex_unlock(&cdma->lock);
}

/*
 * Init timeout resources
 */
static int cdma_timeout_init(struct host1x_cdma *cdma, u32 syncpt_id)
{
	INIT_DELAYED_WORK(&cdma->timeout.wq, cdma_timeout_handler);
	cdma->timeout.initialized = true;

	return 0;
}

/*
 * Clean up timeout resources
 */
static void cdma_timeout_destroy(struct host1x_cdma *cdma)
{
	if (cdma->timeout.initialized)
		cancel_delayed_work(&cdma->timeout.wq);
	cdma->timeout.initialized = false;
}

static const struct host1x_cdma_ops host1x_cdma_ops = {
	.start = cdma_start,
	.stop = cdma_stop,
	.flush = cdma_flush,

	.timeout_init = cdma_timeout_init,
	.timeout_destroy = cdma_timeout_destroy,
	.freeze = cdma_freeze,
	.resume = cdma_resume,
	.timeout_cpu_incr = cdma_timeout_cpu_incr,
};

static const struct host1x_pushbuffer_ops host1x_pushbuffer_ops = {
	.init = push_buffer_init,
};
