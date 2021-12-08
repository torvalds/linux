// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_job: " fmt

#include "rga_job.h"
#include "rga_fence.h"
#include "rga_dma_buf.h"
#include "rga_hw_config.h"
#include "rga2_mmu_info.h"

struct rga_job *
rga_scheduler_get_pending_job_list(struct rga_scheduler_t *scheduler)
{
	unsigned long flags;
	struct rga_job *job;

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	job = list_first_entry_or_null(&scheduler->todo_list,
		struct rga_job, head);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	return job;
}

struct rga_job *
rga_scheduler_get_running_job(struct rga_scheduler_t *scheduler)
{
	unsigned long flags;
	struct rga_job *job;

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	job = scheduler->running_job;

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	return job;
}

static struct rga_scheduler_t *get_scheduler(struct rga_job *job)
{
	struct rga_scheduler_t *scheduler = NULL;
	int i;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		if (job->core == rga_drvdata->rga_scheduler[i]->core) {
			scheduler = rga_drvdata->rga_scheduler[i];

			if (RGA_DEBUG_MSG)
				pr_info("job choose core: %d\n",
					rga_drvdata->rga_scheduler[i]->core);
			break;
		}
	}

	return scheduler;
}

static void rga_job_free(struct rga_job *job)
{
	if (job->out_fence)
		dma_fence_put(job->out_fence);

	free_page((unsigned long)job);
}

static int rga_job_cleanup(struct rga_job *job)
{
	rga_job_free(job);

	return 0;
}

static struct rga_job *rga_job_alloc(struct rga_req *rga_command_base)
{
	struct rga_job *job = NULL;

	job = (struct rga_job *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
	if (!job)
		return NULL;

	job->timestamp = ktime_get();

	job->rga_command_base = *rga_command_base;

	if (rga_command_base->priority > 0) {
		if (rga_command_base->priority > RGA_SCHED_PRIORITY_MAX)
			job->priority = RGA_SCHED_PRIORITY_MAX;
		else
			job->priority = rga_command_base->priority;
	}

	return job;
}

static void print_job_info(struct rga_job *job)
{
	pr_info("job: priority = %d, core = %d\n",
		job->priority, job->core);
}

static int rga_job_run(struct rga_job *job, struct rga_scheduler_t *scheduler)
{
	int ret = 0;

	/* enable power */
	ret = rga_power_enable(scheduler);
	if (ret < 0) {
		pr_err("power enable failed");
		return ret;
	}

	ret = rga_dma_get_info(job);
	if (ret < 0) {
		pr_err("dma buf get failed");
		goto failed;
	}

	ret = scheduler->ops->init_reg(job);
	if (ret < 0) {
		pr_err("init reg failed");
		goto failed;
	}

	ret = scheduler->ops->set_reg(job, scheduler);
	if (ret < 0) {
		pr_err("set reg failed");
		goto failed;
	}

	/* for debug */
	if (RGA_DEBUG_MSG)
		print_job_info(job);

	return ret;

failed:
	rga_power_disable(scheduler);

	return ret;
}

static void rga_job_next(struct rga_scheduler_t *rga_scheduler)
{
	struct rga_job *job = NULL;
	unsigned long flags;

next_job:
	spin_lock_irqsave(&rga_scheduler->irq_lock, flags);

	if (rga_scheduler->running_job ||
		list_empty(&rga_scheduler->todo_list)) {
		spin_unlock_irqrestore(&rga_scheduler->irq_lock, flags);
		return;
	}

	job = list_first_entry(&rga_scheduler->todo_list, struct rga_job, head);

	list_del_init(&job->head);

	rga_scheduler->job_count--;

	rga_scheduler->running_job = job;

	spin_unlock_irqrestore(&rga_scheduler->irq_lock, flags);

	job->ret = rga_job_run(job, rga_scheduler);

	/* If some error before hw run */
	if (job->ret < 0) {
		pr_err("some error on rga_job_run before hw start, %s(%d)\n",
			__func__, __LINE__);

		spin_lock_irqsave(&rga_scheduler->irq_lock, flags);

		rga_scheduler->running_job = NULL;

		spin_unlock_irqrestore(&rga_scheduler->irq_lock, flags);

		if (job->out_fence)
			dma_fence_signal(job->out_fence);

		if (job->flags & RGA_JOB_ASYNC)
			rga_job_cleanup(job);
		else {
			job->flags |= RGA_JOB_DONE;
			wake_up(&rga_scheduler->job_done_wq);
		}

		rga_dma_put_info(job);

		goto next_job;
	}
}

void rga_job_done(struct rga_scheduler_t *rga_scheduler, int ret)
{
	struct rga_job *job;
	unsigned long flags;

	ktime_t now;

	spin_lock_irqsave(&rga_scheduler->irq_lock, flags);

	job = rga_scheduler->running_job;
	rga_scheduler->running_job = NULL;

	spin_unlock_irqrestore(&rga_scheduler->irq_lock, flags);

	job->flags |= RGA_JOB_DONE;
	job->ret = ret;

	now = ktime_get();

	if (RGA_DEBUG_TIME)
		pr_err("%s use time = %lld\n", __func__,
			ktime_to_us(ktime_sub(now, job->timestamp)));

	if (job->core == RGA2_SCHEDULER_CORE0)
		rga2_dma_flush_cache_for_virtual_address(&job->vir_page_table,
			rga_scheduler);

	rga_dma_put_info(job);

	mmput(job->mm);
	mmdrop(job->mm);

	if (job->out_fence)
		dma_fence_signal(job->out_fence);

	if (job->flags & RGA_JOB_ASYNC)
		rga_job_cleanup(job);

	wake_up(&rga_scheduler->job_done_wq);

	rga_job_next(rga_scheduler);

	rga_power_disable(rga_scheduler);
}

static int rga_set_feature(struct rga_req *rga_base)
{
	int feature = 0;

	if (rga_base->render_mode == COLOR_FILL_MODE)
		feature |= RGA_COLOR_FILL;

	if (rga_base->render_mode == COLOR_PALETTE_MODE)
		feature |= RGA_COLOR_PALETTE;

	if (rga_base->color_key_max > 0 || rga_base->color_key_min > 0)
		feature |= RGA_COLOR_KEY;

	if ((rga_base->alpha_rop_flag >> 1) & 1)
		feature |= RGA_ROP_CALCULATE;

	if ((rga_base->alpha_rop_flag >> 8) & 1)
		feature |= RGA_NN_QUANTIZE;

	return feature;
}

static bool rga_check_src0(const struct rga_hw_data *data,
			 struct rga_img_info_t *src0)
{
	int i;
	bool matched = false;
	int format;

	user_format_convert(&format, src0->format);

	if (src0->act_w < data->min_input.w ||
		src0->act_h < data->min_input.h)
		return false;

	if (src0->act_w > data->max_input.w ||
		src0->act_h > data->max_input.h)
		return false;

	for (i = 0; i < data->win[0].num_of_raster_formats; i++) {
		if (format == data->win[0].raster_formats[i]) {
			matched = true;
			break;
		}
	}

	if (!matched)
		return false;

	return true;
}

static bool rga_check_src1(const struct rga_hw_data *data,
			 struct rga_img_info_t *src1)
{
	int i;
	bool matched = false;
	int format;

	user_format_convert(&format, src1->format);

	if (src1->act_w < data->min_input.w ||
		src1->act_h < data->min_input.h)
		return false;

	if (src1->act_w > data->max_input.w ||
		src1->act_h > data->max_input.h)
		return false;

	for (i = 0; i < data->win[1].num_of_raster_formats; i++) {
		if (format == data->win[1].raster_formats[i]) {
			matched = true;
			break;
		}
	}

	if (!matched)
		return false;

	return true;
}

static bool rga_check_dst(const struct rga_hw_data *data,
			 struct rga_img_info_t *dst)
{
	int i;
	bool matched = false;
	int format;

	user_format_convert(&format, dst->format);

	if (dst->act_w < data->min_output.w ||
		dst->act_h < data->min_output.h)
		return false;

	if (dst->act_w > data->max_output.w ||
		dst->act_h > data->max_output.h)
		return false;

	for (i = 0; i < data->win[2].num_of_raster_formats; i++) {
		if (format == data->win[2].raster_formats[i]) {
			matched = true;
			break;
		}
	}

	if (!matched)
		return false;

	return true;
}

static bool rga_check_scale(const struct rga_hw_data *data,
				struct rga_req *rga_base)
{
	struct rga_img_info_t *src0 = &rga_base->src;
	struct rga_img_info_t *dst = &rga_base->dst;

	int sw, sh;
	int dw, dh;

	sw = src0->act_w;
	sh = src0->act_h;

	if ((rga_base->sina == 65536 && rga_base->cosa == 0)
		|| (rga_base->sina == -65536 && rga_base->cosa == 0)) {
		dw = dst->act_h;
		dh = dst->act_w;
	} else {
		dw = dst->act_w;
		dh = dst->act_h;
	}

	if (sw > dw) {
		if ((sw >> data->max_downscale_factor) > dw)
			return false;
	} else if (sw < dw) {
		if ((sw << data->max_upscale_factor) < dw)
			return false;
	}

	if (sh > dh) {
		if ((sh >> data->max_downscale_factor) > dh)
			return false;
	} else if (sh < dh) {
		if ((sh << data->max_upscale_factor) < dh)
			return false;
	}

	return true;
}

static int rga_job_assign(struct rga_job *job)
{
	struct rga_img_info_t *src0 = &job->rga_command_base.src;
	struct rga_img_info_t *src1 = &job->rga_command_base.pat;
	struct rga_img_info_t *dst = &job->rga_command_base.dst;

	struct rga_req *rga_base = &job->rga_command_base;
	const struct rga_hw_data *data;
	struct rga_scheduler_t *scheduler = NULL;

	int feature;
	int core = RGA_NONE_CORE;
	int optional_cores = RGA_NONE_CORE;
	int i;
	int min_of_job_count = 0;
	unsigned long flags;

	/* assigned by userspace */
	if (rga_base->core > RGA_NONE_CORE) {
		if (rga_base->core > RGA_CORE_MASK) {
			pr_err("invalid setting core by user\n");
			goto finish;
		} else if (rga_base->core & RGA_CORE_MASK) {
			optional_cores = rga_base->core;
			goto skip_functional_policy;
		}
	}

	feature = rga_set_feature(rga_base);

	/* function */
	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		data = rga_drvdata->rga_scheduler[i]->data;
		scheduler = rga_drvdata->rga_scheduler[i];

		if (RGA_DEBUG_MSG)
			pr_err("start policy on core = %d", scheduler->core);

		if ((scheduler->core != RGA2_SCHEDULER_CORE0) &&
			(src0->uv_addr > 0 || src1->uv_addr > 0 ||
			dst->uv_addr > 0)) {
			if (RGA_DEBUG_MSG)
				pr_err("rga3 can not support viraddr\n");
			continue;
		}

		if (feature > 0) {
			if (!(feature & data->feature)) {
				if (RGA_DEBUG_MSG)
					pr_err("core = %d, break on feature",
						scheduler->core);
				continue;
			}
		}

		/* only colorfill need single win (colorpalette?) */
		if (!(feature & 1)) {
			if (src1->yrgb_addr > 0) {
				if ((!(src0->rd_mode & data->win[0].rd_mode)) ||
					(!(src1->rd_mode & data->win[1].rd_mode)) ||
					(!(dst->rd_mode & data->win[2].rd_mode))) {
					if (RGA_DEBUG_MSG)
						pr_err("core = %d, ABC break on rd_mode",
							scheduler->core);
					continue;
				}
			} else {
				if ((!(src0->rd_mode & data->win[0].rd_mode)) ||
					(!(dst->rd_mode & data->win[2].rd_mode))) {
					if (RGA_DEBUG_MSG)
						pr_err("core = %d, ABB break on rd_mode",
							scheduler->core);
					continue;
				}
			}

			if (!rga_check_scale(data, rga_base)) {
				if (RGA_DEBUG_MSG)
					pr_err("core = %d, break on rga_check_scale",
						scheduler->core);
				continue;
			}

			if (!rga_check_src0(data, src0)) {
				if (RGA_DEBUG_MSG)
					pr_err("core = %d, break on rga_check_src0",
						scheduler->core);
				continue;
			}

			if (src1->yrgb_addr > 0) {
				if (!rga_check_src1(data, src1)) {
					if (RGA_DEBUG_MSG)
						pr_err("core = %d, break on rga_check_src1",
						scheduler->core);
					continue;
				}
			}
		}

		if (!rga_check_dst(data, dst)) {
			if (RGA_DEBUG_MSG)
				pr_err("core = %d, break on rga_check_dst",
					scheduler->core);
			continue;
		}

		optional_cores |= scheduler->core;
	}

	if (RGA_DEBUG_MSG)
		pr_info("optional_cores = %d\n", optional_cores);

	if (optional_cores == 0) {
		core = -1;
		pr_err("invalid function policy\n");
		goto finish;
	}

skip_functional_policy:
	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->rga_scheduler[i];

		if (optional_cores & scheduler->core) {
			spin_lock_irqsave(&scheduler->irq_lock, flags);

			if (scheduler->running_job == NULL) {
				core = scheduler->core;
				spin_unlock_irqrestore(&scheduler->irq_lock,
							 flags);
				break;
			} else {
				if ((min_of_job_count > scheduler->job_count) ||
					(min_of_job_count == 0)) {
					min_of_job_count = scheduler->job_count;
					core = scheduler->core;
				}
			}

			spin_unlock_irqrestore(&scheduler->irq_lock, flags);
		}
	}

	/* TODO: need consider full load */
finish:
	if (RGA_DEBUG_MSG)
		pr_info("assign core: %d\n", core);

	return core;
}

static void rga_job_timeout_clean(struct rga_scheduler_t *scheduler)
{
	unsigned long flags;
	struct rga_job *job = NULL;
	ktime_t now = ktime_get();

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	job = scheduler->running_job;
	if (job && (job->flags & RGA_JOB_ASYNC) &&
	   (ktime_to_ms(ktime_sub(now, job->timestamp)) >= RGA_ASYNC_TIMEOUT_DELAY)) {
		scheduler->running_job = NULL;

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);

		scheduler->ops->soft_reset(scheduler);

		rga_dma_put_info(job);

		mmput(job->mm);
		mmdrop(job->mm);

		if (job->out_fence)
			dma_fence_signal(job->out_fence);

		rga_job_cleanup(job);

		rga_power_disable(scheduler);
	} else {
		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
	}
}

static struct rga_scheduler_t *rga_job_schedule(struct rga_job *job)
{
	unsigned long flags;
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_job *job_pos;
	bool first_match = 0;

	if (rga_drvdata->num_of_scheduler > 1) {
		job->core = rga_job_assign(job);
		if (job->core <= 0) {
			pr_err("job assign failed");
			return NULL;
		}
	} else {
		job->core = rga_drvdata->rga_scheduler[0]->core;
	}

	scheduler = get_scheduler(job);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__, __LINE__);
		return NULL;
	}

	rga_job_timeout_clean(scheduler);

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	/* priority policy set by userspace */
	if (list_empty(&scheduler->todo_list)
		|| (job->priority == RGA_SCHED_PRIORITY_DEFAULT)) {
		list_add_tail(&job->head, &scheduler->todo_list);
	} else {
		list_for_each_entry(job_pos, &scheduler->todo_list, head) {
			if (job->priority > job_pos->priority &&
					(!first_match)) {
				list_add(&job->head, &job_pos->head);
				first_match = true;
			}

			/*
			 * Increase the priority of subsequent tasks
			 * after inserting into the list
			 */
			if (first_match)
				job_pos->priority++;
		}

		if (!first_match)
			list_add_tail(&job->head, &scheduler->todo_list);
	}

	scheduler->job_count++;

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	rga_job_next(scheduler);

	return scheduler;
}

static void rga_running_job_abort(struct rga_job *job,
				 struct rga_scheduler_t *rga_scheduler)
{
	unsigned long flags;

	spin_lock_irqsave(&rga_scheduler->irq_lock, flags);

	/* invalid job */
	if (job == rga_scheduler->running_job) {
		rga_scheduler->running_job = NULL;
	}

	spin_unlock_irqrestore(&rga_scheduler->irq_lock, flags);

	rga_job_cleanup(job);
}

static void rga_invalid_job_abort(struct rga_job *job)
{
	rga_job_cleanup(job);
}

static inline int rga_job_wait(struct rga_scheduler_t *rga_scheduler,
				 struct rga_job *job)
{
	int left_time;
	ktime_t now;
	int ret;

	left_time = wait_event_interruptible_timeout(rga_scheduler->job_done_wq,
		job->flags & RGA_JOB_DONE, RGA_SYNC_TIMEOUT_DELAY);

	switch (left_time) {
	case 0:
		pr_err("%s timeout", __func__);
		ret = -EBUSY;
		break;
	case -ERESTARTSYS:
		ret = -ERESTARTSYS;
		break;
	default:
		ret = 0;
		break;
	}

	now = ktime_get();

	if (RGA_DEBUG_TIME)
		pr_err("%s use time = %lld\n", __func__,
			 ktime_to_us(ktime_sub(now, job->timestamp)));

	return ret;
}

static void rga_input_fence_signaled(struct dma_fence *fence,
					 struct dma_fence_cb *_waiter)
{
	struct rga_fence_waiter *waiter = (struct rga_fence_waiter *)_waiter;
	struct rga_scheduler_t *scheduler = NULL;

	ktime_t now;

	now = ktime_get();

	if (RGA_DEBUG_TIME)
		pr_err("rga job wait in_fence signal use time = %lld\n",
			ktime_to_us(ktime_sub(now, waiter->job->timestamp)));

	scheduler = rga_job_schedule(waiter->job);

	if (scheduler == NULL)
		pr_err("failed to get scheduler, %s(%d)\n", __func__, __LINE__);

	kfree(waiter);
}

int rga_commit(struct rga_req *rga_command_base, int flags)
{
	struct rga_job *job = NULL;
	struct rga_scheduler_t *scheduler = NULL;
	struct dma_fence *in_fence;
	int ret = 0;

	job = rga_job_alloc(rga_command_base);
	if (!job) {
		pr_err("failed to alloc rga job!\n");
		return -ENOMEM;
	}

	/*
	 * because fd can not pass on other thread,
	 * so need to get dma_buf first.
	 */
	ret = rga_dma_buf_get(job);
	if (ret < 0) {
		pr_err("%s: failed to get dma buf from fd\n",
				__func__);
		rga_job_free(job);
		return ret;
	}

	mmgrab(current->mm);
	mmget(current->mm);
	job->mm = current->mm;

	if (flags == RGA_BLIT_ASYNC) {
		ret = rga_out_fence_alloc(job);
		if (ret) {
			rga_job_free(job);
			return ret;
		}
		job->flags |= RGA_JOB_ASYNC;
		rga_command_base->out_fence_fd = rga_out_fence_get_fd(job);

		if (RGA_DEBUG_MSG)
			pr_err("in_fence_fd = %d",
				rga_command_base->in_fence_fd);

		/* if input fence is valiable */
		if (rga_command_base->in_fence_fd > 0) {
			in_fence = rga_get_input_fence(
				rga_command_base->in_fence_fd);
			if (!in_fence) {
				pr_err("%s: failed to get input dma_fence\n",
					 __func__);
				rga_job_free(job);
				return ret;
			}

			/* close input fence fd */
			ksys_close(rga_command_base->in_fence_fd);

			ret = dma_fence_get_status(in_fence);
			/* ret = 1: fence has been signaled */
			if (ret == 1) {
				scheduler = rga_job_schedule(job);

				if (scheduler == NULL) {
					pr_err("failed to get scheduler, %s(%d)\n",
						 __func__, __LINE__);
					goto invalid_job;
				}
				/* if input fence is valid */
			} else if (ret == 0) {
				ret = rga_add_dma_fence_callback(job,
					in_fence, rga_input_fence_signaled);
				if (ret < 0) {
					pr_err("%s: failed to add fence callback\n",
						 __func__);
					rga_job_free(job);
					return ret;
				}
			} else {
				pr_err("%s: fence status error\n", __func__);
				rga_job_free(job);
				return ret;
			}
		} else {
			scheduler = rga_job_schedule(job);

			if (scheduler == NULL) {
				pr_err("failed to get scheduler, %s(%d)\n",
					 __func__, __LINE__);
				goto invalid_job;
			}
		}

		return ret;
		/* sync mode: wait utill job finish */
	} else if (flags == RGA_BLIT_SYNC) {
		scheduler = rga_job_schedule(job);

		if (scheduler == NULL) {
			pr_err("failed to get scheduler, %s(%d)\n", __func__,
				 __LINE__);
			goto invalid_job;
		}

		ret = job->ret;
		if (ret < 0) {
			pr_err("some error on job, %s(%d)\n", __func__,
				 __LINE__);
			goto running_job_abort;
		}

		ret = rga_job_wait(scheduler, job);
		if (ret < 0) {
			goto running_job_abort;
		}

		rga_job_cleanup(job);
	}
	return ret;

invalid_job:
	rga_invalid_job_abort(job);
	return ret;

/* only used by SYNC mode */
running_job_abort:
	rga_running_job_abort(job, scheduler);
	return ret;
}

int rga_kernel_commit(struct rga_req *rga_command_base,
			 struct rga_mpi_job_t *mpi_job, int flags)
{
	struct rga_job *job = NULL;
	struct rga_scheduler_t *scheduler = NULL;
	int ret = 0;

	job = rga_job_alloc(rga_command_base);
	if (!job) {
		pr_err("failed to alloc rga job!\n");
		return -ENOMEM;
	}

	job->dma_buf_src0 = mpi_job->dma_buf_src0;
	job->dma_buf_src1 = mpi_job->dma_buf_src1;
	job->dma_buf_dst = mpi_job->dma_buf_dst;

	if (flags == RGA_BLIT_ASYNC) {
		//TODO:
		pr_err("rk-debug TODO\n");
	} else if (flags == RGA_BLIT_SYNC) {
		scheduler = rga_job_schedule(job);

		if (scheduler == NULL) {
			pr_err("failed to get scheduler, %s(%d)\n", __func__,
				 __LINE__);
			goto invalid_job;
		}

		ret = job->ret;
		if (ret < 0) {
			pr_err("some error on job, %s(%d)\n", __func__,
				 __LINE__);
			goto running_job_abort;
		}

		ret = rga_job_wait(scheduler, job);
		if (ret < 0) {
			goto running_job_abort;
		}
		rga_job_cleanup(job);
	}
	return ret;

invalid_job:
	rga_invalid_job_abort(job);
	return ret;

/* only used by SYNC mode */
running_job_abort:
	rga_running_job_abort(job, scheduler);
	return ret;
}
