/*
 * Tegra host1x Channel
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

#include <linux/host1x.h>
#include <linux/slab.h>

#include <trace/events/host1x.h>

#include "../channel.h"
#include "../dev.h"
#include "../intr.h"
#include "../job.h"

#define HOST1X_CHANNEL_SIZE 16384
#define TRACE_MAX_LENGTH 128U

static void trace_write_gather(struct host1x_cdma *cdma, struct host1x_bo *bo,
			       u32 offset, u32 words)
{
	struct device *dev = cdma_to_channel(cdma)->dev;
	void *mem = NULL;

	if (host1x_debug_trace_cmdbuf)
		mem = host1x_bo_mmap(bo);

	if (mem) {
		u32 i;
		/*
		 * Write in batches of 128 as there seems to be a limit
		 * of how much you can output to ftrace at once.
		 */
		for (i = 0; i < words; i += TRACE_MAX_LENGTH) {
			u32 num_words = min(words - i, TRACE_MAX_LENGTH);
			offset += i * sizeof(u32);

			trace_host1x_cdma_push_gather(dev_name(dev), bo,
						      num_words, offset,
						      mem);
		}

		host1x_bo_munmap(bo, mem);
	}
}

static void submit_gathers(struct host1x_job *job)
{
	struct host1x_cdma *cdma = &job->channel->cdma;
	unsigned int i;

	for (i = 0; i < job->num_gathers; i++) {
		struct host1x_job_gather *g = &job->gathers[i];
		u32 op1 = host1x_opcode_gather(g->words);
		u32 op2 = g->base + g->offset;
		trace_write_gather(cdma, g->bo, g->offset, op1 & 0xffff);
		host1x_cdma_push(cdma, op1, op2);
	}
}

static inline void synchronize_syncpt_base(struct host1x_job *job)
{
	struct host1x *host = dev_get_drvdata(job->channel->dev->parent);
	struct host1x_syncpt *sp = host->syncpt + job->syncpt_id;
	u32 id, value;

	value = host1x_syncpt_read_max(sp);
	id = sp->base->id;

	host1x_cdma_push(&job->channel->cdma,
			 host1x_opcode_setclass(HOST1X_CLASS_HOST1X,
				HOST1X_UCLASS_LOAD_SYNCPT_BASE, 1),
			 HOST1X_UCLASS_LOAD_SYNCPT_BASE_BASE_INDX_F(id) |
			 HOST1X_UCLASS_LOAD_SYNCPT_BASE_VALUE_F(value));
}

static int channel_submit(struct host1x_job *job)
{
	struct host1x_channel *ch = job->channel;
	struct host1x_syncpt *sp;
	u32 user_syncpt_incrs = job->syncpt_incrs;
	u32 prev_max = 0;
	u32 syncval;
	int err;
	struct host1x_waitlist *completed_waiter = NULL;
	struct host1x *host = dev_get_drvdata(ch->dev->parent);

	sp = host->syncpt + job->syncpt_id;
	trace_host1x_channel_submit(dev_name(ch->dev),
				    job->num_gathers, job->num_relocs,
				    job->num_waitchk, job->syncpt_id,
				    job->syncpt_incrs);

	/* before error checks, return current max */
	prev_max = job->syncpt_end = host1x_syncpt_read_max(sp);

	/* get submit lock */
	err = mutex_lock_interruptible(&ch->submitlock);
	if (err)
		goto error;

	completed_waiter = kzalloc(sizeof(*completed_waiter), GFP_KERNEL);
	if (!completed_waiter) {
		mutex_unlock(&ch->submitlock);
		err = -ENOMEM;
		goto error;
	}

	/* begin a CDMA submit */
	err = host1x_cdma_begin(&ch->cdma, job);
	if (err) {
		mutex_unlock(&ch->submitlock);
		goto error;
	}

	if (job->serialize) {
		/*
		 * Force serialization by inserting a host wait for the
		 * previous job to finish before this one can commence.
		 */
		host1x_cdma_push(&ch->cdma,
				 host1x_opcode_setclass(HOST1X_CLASS_HOST1X,
					host1x_uclass_wait_syncpt_r(), 1),
				 host1x_class_host_wait_syncpt(job->syncpt_id,
					host1x_syncpt_read_max(sp)));
	}

	/* Synchronize base register to allow using it for relative waiting */
	if (sp->base)
		synchronize_syncpt_base(job);

	syncval = host1x_syncpt_incr_max(sp, user_syncpt_incrs);

	job->syncpt_end = syncval;

	/* add a setclass for modules that require it */
	if (job->class)
		host1x_cdma_push(&ch->cdma,
				 host1x_opcode_setclass(job->class, 0, 0),
				 HOST1X_OPCODE_NOP);

	submit_gathers(job);

	/* end CDMA submit & stash pinned hMems into sync queue */
	host1x_cdma_end(&ch->cdma, job);

	trace_host1x_channel_submitted(dev_name(ch->dev), prev_max, syncval);

	/* schedule a submit complete interrupt */
	err = host1x_intr_add_action(host, job->syncpt_id, syncval,
				     HOST1X_INTR_ACTION_SUBMIT_COMPLETE, ch,
				     completed_waiter, NULL);
	completed_waiter = NULL;
	WARN(err, "Failed to set submit complete interrupt");

	mutex_unlock(&ch->submitlock);

	return 0;

error:
	kfree(completed_waiter);
	return err;
}

static int host1x_channel_init(struct host1x_channel *ch, struct host1x *dev,
			       unsigned int index)
{
	ch->id = index;
	mutex_init(&ch->reflock);
	mutex_init(&ch->submitlock);

	ch->regs = dev->regs + index * HOST1X_CHANNEL_SIZE;
	return 0;
}

static const struct host1x_channel_ops host1x_channel_ops = {
	.init = host1x_channel_init,
	.submit = channel_submit,
};
