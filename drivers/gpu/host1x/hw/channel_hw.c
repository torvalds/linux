// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra host1x Channel
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 */

#include <linux/host1x.h>
#include <linux/iommu.h>
#include <linux/slab.h>

#include <trace/events/host1x.h>

#include "../channel.h"
#include "../dev.h"
#include "../intr.h"
#include "../job.h"

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

static void submit_wait(struct host1x_job *job, u32 id, u32 threshold,
			u32 next_class)
{
	struct host1x_cdma *cdma = &job->channel->cdma;

#if HOST1X_HW >= 6
	u32 stream_id;

	/*
	 * If a memory context has been set, use it. Otherwise
	 * (if context isolation is disabled) use the engine's
	 * firmware stream ID.
	 */
	if (job->memory_context)
		stream_id = job->memory_context->stream_id;
	else
		stream_id = job->engine_fallback_streamid;

	host1x_cdma_push_wide(cdma,
		host1x_opcode_setclass(
			HOST1X_CLASS_HOST1X,
			HOST1X_UCLASS_LOAD_SYNCPT_PAYLOAD_32,
			/* WAIT_SYNCPT_32 is at SYNCPT_PAYLOAD_32+2 */
			BIT(0) | BIT(2)
		),
		threshold,
		id,
		HOST1X_OPCODE_NOP
	);
	host1x_cdma_push_wide(&job->channel->cdma,
		host1x_opcode_setclass(job->class, 0, 0),
		host1x_opcode_setpayload(stream_id),
		host1x_opcode_setstreamid(job->engine_streamid_offset / 4),
		HOST1X_OPCODE_NOP);
#elif HOST1X_HW >= 2
	host1x_cdma_push_wide(cdma,
		host1x_opcode_setclass(
			HOST1X_CLASS_HOST1X,
			HOST1X_UCLASS_LOAD_SYNCPT_PAYLOAD_32,
			/* WAIT_SYNCPT_32 is at SYNCPT_PAYLOAD_32+2 */
			BIT(0) | BIT(2)
		),
		threshold,
		id,
		host1x_opcode_setclass(next_class, 0, 0)
	);
#else
	/* TODO add waitchk or use waitbases or other mitigation */
	host1x_cdma_push(cdma,
		host1x_opcode_setclass(
			HOST1X_CLASS_HOST1X,
			host1x_uclass_wait_syncpt_r(),
			BIT(0)
		),
		host1x_class_host_wait_syncpt(id, threshold)
	);
	host1x_cdma_push(cdma,
		host1x_opcode_setclass(next_class, 0, 0),
		HOST1X_OPCODE_NOP
	);
#endif
}

static void submit_gathers(struct host1x_job *job, u32 job_syncpt_base)
{
	struct host1x_cdma *cdma = &job->channel->cdma;
#if HOST1X_HW < 6
	struct device *dev = job->channel->dev;
#endif
	unsigned int i;
	u32 threshold;

	for (i = 0; i < job->num_cmds; i++) {
		struct host1x_job_cmd *cmd = &job->cmds[i];

		if (cmd->is_wait) {
			if (cmd->wait.relative)
				threshold = job_syncpt_base + cmd->wait.threshold;
			else
				threshold = cmd->wait.threshold;

			submit_wait(job, cmd->wait.id, threshold, cmd->wait.next_class);
		} else {
			struct host1x_job_gather *g = &cmd->gather;

			dma_addr_t addr = g->base + g->offset;
			u32 op2, op3;

			op2 = lower_32_bits(addr);
			op3 = upper_32_bits(addr);

			trace_write_gather(cdma, g->bo, g->offset, g->words);

			if (op3 != 0) {
#if HOST1X_HW >= 6
				u32 op1 = host1x_opcode_gather_wide(g->words);
				u32 op4 = HOST1X_OPCODE_NOP;

				host1x_cdma_push_wide(cdma, op1, op2, op3, op4);
#else
				dev_err(dev, "invalid gather for push buffer %pad\n",
					&addr);
				continue;
#endif
			} else {
				u32 op1 = host1x_opcode_gather(g->words);

				host1x_cdma_push(cdma, op1, op2);
			}
		}
	}
}

static inline void synchronize_syncpt_base(struct host1x_job *job)
{
	struct host1x_syncpt *sp = job->syncpt;
	unsigned int id;
	u32 value;

	value = host1x_syncpt_read_max(sp);
	id = sp->base->id;

	host1x_cdma_push(&job->channel->cdma,
			 host1x_opcode_setclass(HOST1X_CLASS_HOST1X,
				HOST1X_UCLASS_LOAD_SYNCPT_BASE, 1),
			 HOST1X_UCLASS_LOAD_SYNCPT_BASE_BASE_INDX_F(id) |
			 HOST1X_UCLASS_LOAD_SYNCPT_BASE_VALUE_F(value));
}

static void host1x_channel_set_streamid(struct host1x_channel *channel)
{
#if HOST1X_HW >= 6
	u32 stream_id;

	if (!tegra_dev_iommu_get_stream_id(channel->dev->parent, &stream_id))
		stream_id = TEGRA_STREAM_ID_BYPASS;

	host1x_ch_writel(channel, stream_id, HOST1X_CHANNEL_SMMU_STREAMID);
#endif
}

static void host1x_enable_gather_filter(struct host1x_channel *ch)
{
#if HOST1X_HW >= 6
	struct host1x *host = dev_get_drvdata(ch->dev->parent);
	u32 val;

	if (!host->hv_regs)
		return;

	val = host1x_hypervisor_readl(
		host, HOST1X_HV_CH_KERNEL_FILTER_GBUFFER(ch->id / 32));
	val |= BIT(ch->id % 32);
	host1x_hypervisor_writel(
		host, val, HOST1X_HV_CH_KERNEL_FILTER_GBUFFER(ch->id / 32));
#elif HOST1X_HW >= 4
	host1x_ch_writel(ch,
			 HOST1X_CHANNEL_CHANNELCTRL_KERNEL_FILTER_GBUFFER(1),
			 HOST1X_CHANNEL_CHANNELCTRL);
#endif
}

static void channel_program_cdma(struct host1x_job *job)
{
	struct host1x_cdma *cdma = &job->channel->cdma;
	struct host1x_syncpt *sp = job->syncpt;

#if HOST1X_HW >= 6
	u32 fence;

	/* Enter engine class with invalid stream ID. */
	host1x_cdma_push_wide(cdma,
		host1x_opcode_acquire_mlock(job->class),
		host1x_opcode_setclass(job->class, 0, 0),
		host1x_opcode_setpayload(0),
		host1x_opcode_setstreamid(job->engine_streamid_offset / 4));

	/* Before switching stream ID to real stream ID, ensure engine is idle. */
	fence = host1x_syncpt_incr_max(sp, 1);
	host1x_cdma_push(&job->channel->cdma,
		host1x_opcode_nonincr(HOST1X_UCLASS_INCR_SYNCPT, 1),
		HOST1X_UCLASS_INCR_SYNCPT_INDX_F(job->syncpt->id) |
			HOST1X_UCLASS_INCR_SYNCPT_COND_F(4));
	submit_wait(job, job->syncpt->id, fence, job->class);

	/* Submit work. */
	job->syncpt_end = host1x_syncpt_incr_max(sp, job->syncpt_incrs);
	submit_gathers(job, job->syncpt_end - job->syncpt_incrs);

	/* Before releasing MLOCK, ensure engine is idle again. */
	fence = host1x_syncpt_incr_max(sp, 1);
	host1x_cdma_push(&job->channel->cdma,
		host1x_opcode_nonincr(HOST1X_UCLASS_INCR_SYNCPT, 1),
		HOST1X_UCLASS_INCR_SYNCPT_INDX_F(job->syncpt->id) |
			HOST1X_UCLASS_INCR_SYNCPT_COND_F(4));
	submit_wait(job, job->syncpt->id, fence, job->class);

	/* Release MLOCK. */
	host1x_cdma_push(cdma,
		HOST1X_OPCODE_NOP, host1x_opcode_release_mlock(job->class));
#else
	if (job->serialize) {
		/*
		 * Force serialization by inserting a host wait for the
		 * previous job to finish before this one can commence.
		 */
		host1x_cdma_push(cdma,
				 host1x_opcode_setclass(HOST1X_CLASS_HOST1X,
					host1x_uclass_wait_syncpt_r(), 1),
				 host1x_class_host_wait_syncpt(job->syncpt->id,
					host1x_syncpt_read_max(sp)));
	}

	/* Synchronize base register to allow using it for relative waiting */
	if (sp->base)
		synchronize_syncpt_base(job);

	/* add a setclass for modules that require it */
	if (job->class)
		host1x_cdma_push(cdma,
				 host1x_opcode_setclass(job->class, 0, 0),
				 HOST1X_OPCODE_NOP);

	job->syncpt_end = host1x_syncpt_incr_max(sp, job->syncpt_incrs);

	submit_gathers(job, job->syncpt_end - job->syncpt_incrs);
#endif
}

static void job_complete_callback(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct host1x_job *job = container_of(cb, struct host1x_job, fence_cb);

	/* Schedules CDMA update. */
	host1x_cdma_update(&job->channel->cdma);
}

static int channel_submit(struct host1x_job *job)
{
	struct host1x_channel *ch = job->channel;
	struct host1x_syncpt *sp = job->syncpt;
	u32 prev_max = 0;
	u32 syncval;
	int err;
	struct host1x *host = dev_get_drvdata(ch->dev->parent);

	trace_host1x_channel_submit(dev_name(ch->dev),
				    job->num_cmds, job->num_relocs,
				    job->syncpt->id, job->syncpt_incrs);

	/* before error checks, return current max */
	prev_max = job->syncpt_end = host1x_syncpt_read_max(sp);

	/* get submit lock */
	err = mutex_lock_interruptible(&ch->submitlock);
	if (err)
		return err;

	host1x_channel_set_streamid(ch);
	host1x_enable_gather_filter(ch);
	host1x_hw_syncpt_assign_to_channel(host, sp, ch);

	/* begin a CDMA submit */
	err = host1x_cdma_begin(&ch->cdma, job);
	if (err) {
		mutex_unlock(&ch->submitlock);
		return err;
	}

	channel_program_cdma(job);
	syncval = host1x_syncpt_read_max(sp);

	/*
	 * Create fence before submitting job to HW to avoid job completing
	 * before the fence is set up.
	 */
	job->fence = host1x_fence_create(sp, syncval, true);
	if (WARN(IS_ERR(job->fence), "Failed to create submit complete fence")) {
		job->fence = NULL;
	} else {
		err = dma_fence_add_callback(job->fence, &job->fence_cb,
					     job_complete_callback);
	}

	/* end CDMA submit & stash pinned hMems into sync queue */
	host1x_cdma_end(&ch->cdma, job);

	trace_host1x_channel_submitted(dev_name(ch->dev), prev_max, syncval);

	mutex_unlock(&ch->submitlock);

	if (err == -ENOENT)
		host1x_cdma_update(&ch->cdma);
	else
		WARN(err, "Failed to set submit complete interrupt");

	return 0;
}

static int host1x_channel_init(struct host1x_channel *ch, struct host1x *dev,
			       unsigned int index)
{
#if HOST1X_HW < 6
	ch->regs = dev->regs + index * 0x4000;
#else
	ch->regs = dev->regs + index * 0x100;
#endif
	return 0;
}

static const struct host1x_channel_ops host1x_channel_ops = {
	.init = host1x_channel_init,
	.submit = channel_submit,
};
