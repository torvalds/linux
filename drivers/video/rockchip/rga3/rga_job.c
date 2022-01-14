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
#include "rga_mm.h"
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

struct rga_scheduler_t *rga_job_get_scheduler(int core)
{
	struct rga_scheduler_t *scheduler = NULL;
	int i;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		if (core == rga_drvdata->rga_scheduler[i]->core) {
			scheduler = rga_drvdata->rga_scheduler[i];

			if (DEBUGGER_EN(MSG))
				pr_info("job choose core: %d\n",
					rga_drvdata->rga_scheduler[i]->core);
			break;
		}
	}

	return scheduler;
}

static int rga_job_get_current_mm(struct rga_job *job)
{
	int mmu_flag;

	struct rga_img_info_t *src0 = NULL;
	struct rga_img_info_t *src1 = NULL;
	struct rga_img_info_t *dst = NULL;
	struct rga_img_info_t *els = NULL;

	src0 = &job->rga_command_base.src;
	dst = &job->rga_command_base.dst;
	if (job->rga_command_base.render_mode != UPDATE_PALETTE_TABLE_MODE)
		src1 = &job->rga_command_base.pat;
	else
		els = &job->rga_command_base.pat;

	if (likely(src0 != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 8) & 1);
		if (mmu_flag && src0->uv_addr)
			goto get_current_mm;
	}

	if (likely(dst != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 10) & 1);
		if (mmu_flag && dst->uv_addr)
			goto get_current_mm;
	}

	if (src1 != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 9) & 1);
		if (mmu_flag && src1->yrgb_addr)
			goto get_current_mm;
	}

	if (els != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 11) & 1);
		if (mmu_flag && els->yrgb_addr)
			goto get_current_mm;
	}

	return 0;

get_current_mm:
	mmgrab(current->mm);
	mmget(current->mm);
	job->mm = current->mm;

	return 1;
}

static void rga_job_put_current_mm(struct rga_job *job)
{
	if (job->mm == NULL)
		return;

	mmput(job->mm);
	mmdrop(job->mm);
	job->mm = NULL;
}

static void rga_job_free(struct rga_job *job)
{
	if (job->out_fence)
		dma_fence_put(job->out_fence);

	if (~job->flags & RGA_JOB_USE_HANDLE)
		rga_job_put_current_mm(job);

	free_page((unsigned long)job);
}

static int rga_job_cleanup(struct rga_job *job)
{
	rga_job_free(job);

	return 0;
}

static int rga_job_judgment_support_core(struct rga_job *job)
{
	int ret = 0;
	uint32_t mm_flag;
	struct rga_req *req;
	struct rga_mm *mm;

	req = &job->rga_command_base;
	mm = rga_drvdata->mm;
	if (mm == NULL) {
		pr_err("rga mm is null!\n");
		return -EFAULT;
	}

	mutex_lock(&mm->lock);

	if (likely(req->src.yrgb_addr > 0)) {
		ret = rga_mm_lookup_flag(mm, req->src.yrgb_addr);
		if (ret < 0)
			goto out_finish;
		else
			mm_flag = (uint32_t)ret;

		if (~mm_flag & RGA_MM_UNDER_4G) {
			job->flags |= RGA_JOB_UNSUPPORT_RGA2;
			goto out_finish;
		}
	}

	if (likely(req->dst.yrgb_addr > 0)) {
		ret = rga_mm_lookup_flag(mm, req->dst.yrgb_addr);
		if (ret < 0)
			goto out_finish;
		else
			mm_flag = (uint32_t)ret;

		if (~mm_flag & RGA_MM_UNDER_4G) {
			job->flags |= RGA_JOB_UNSUPPORT_RGA2;
			goto out_finish;
		}
	}

	if (req->pat.yrgb_addr > 0) {
		ret = rga_mm_lookup_flag(mm, req->pat.yrgb_addr);
		if (ret < 0)
			goto out_finish;
		else
			mm_flag = (uint32_t)ret;

		if (~mm_flag & RGA_MM_UNDER_4G) {
			job->flags |= RGA_JOB_UNSUPPORT_RGA2;
			goto out_finish;
		}
	}

out_finish:
	mutex_unlock(&mm->lock);

	return ret;
}

static struct rga_job *rga_job_alloc(struct rga_req *rga_command_base)
{
	struct rga_job *job = NULL;

	job = (struct rga_job *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
	if (!job)
		return NULL;

	spin_lock_init(&job->fence_lock);
	INIT_LIST_HEAD(&job->head);

	job->timestamp = ktime_get();
	job->running_time = ktime_get();

	job->rga_command_base = *rga_command_base;

	if (rga_command_base->priority > 0) {
		if (rga_command_base->priority > RGA_SCHED_PRIORITY_MAX)
			job->priority = RGA_SCHED_PRIORITY_MAX;
		else
			job->priority = rga_command_base->priority;
	}

	if (job->rga_command_base.handle_flag & 1) {
		job->flags |= RGA_JOB_USE_HANDLE;

		rga_job_judgment_support_core(job);
	} else {
		rga_job_get_current_mm(job);
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

	if (job->flags & RGA_JOB_USE_HANDLE) {
		ret = rga_mm_get_handle_info(job);
		if (ret < 0) {
			pr_err("%s: failed to get buffer from handle\n", __func__);
			goto failed;
		}
	} else {
		ret = rga_dma_get_info(job);
		if (ret < 0) {
			pr_err("dma buf get failed");
			goto failed;
		}
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
	if (DEBUGGER_EN(MSG))
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

		if (job->flags & RGA_JOB_USE_HANDLE)
			rga_mm_put_handle_info(job);
		else
			rga_dma_put_info(job);

		if (job->out_fence)
			dma_fence_signal(job->out_fence);

		if (job->flags & RGA_JOB_ASYNC)
			rga_job_cleanup(job);
		else {
			job->flags |= RGA_JOB_DONE;
			wake_up(&rga_scheduler->job_done_wq);
		}

		goto next_job;
	}
}

void rga_job_done(struct rga_scheduler_t *rga_scheduler, int ret)
{
	struct rga_job *job;
	unsigned long flags;

	ktime_t now = ktime_get();

	spin_lock_irqsave(&rga_scheduler->irq_lock, flags);

	job = rga_scheduler->running_job;
	rga_scheduler->running_job = NULL;

	rga_scheduler->timer.busy_time += ktime_us_delta(now, job->timestamp);

	spin_unlock_irqrestore(&rga_scheduler->irq_lock, flags);

	job->flags |= RGA_JOB_DONE;
	job->ret = ret;

	if (DEBUGGER_EN(TIME))
		pr_err("%s use time = %lld\n", __func__,
			ktime_us_delta(now, job->running_time));

	job->running_time = now;

	if (job->core == RGA2_SCHEDULER_CORE0)
		rga2_dma_flush_cache_for_virtual_address(&job->vir_page_table,
			rga_scheduler);

	if (job->flags & RGA_JOB_USE_HANDLE)
		rga_mm_put_handle_info(job);
	else
		rga_dma_put_info(job);

	if (job->out_fence)
		dma_fence_signal(job->out_fence);

	if (job->flags & RGA_JOB_ASYNC)
		rga_job_cleanup(job);

	wake_up(&rga_scheduler->job_done_wq);

	rga_job_next(rga_scheduler);

	rga_power_disable(rga_scheduler);
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

		if (job->flags & RGA_JOB_USE_HANDLE)
			rga_mm_put_handle_info(job);
		else
			rga_dma_put_info(job);

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

	scheduler = rga_job_get_scheduler(job->core);
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
		rga_scheduler->ops->soft_reset(rga_scheduler);
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

	if (DEBUGGER_EN(TIME))
		pr_err("%s use time = %lld\n", __func__,
			 ktime_to_us(ktime_sub(now, job->running_time)));

	return ret;
}

static void rga_input_fence_signaled(struct dma_fence *fence,
					 struct dma_fence_cb *_waiter)
{
	struct rga_fence_waiter *waiter = (struct rga_fence_waiter *)_waiter;
	struct rga_scheduler_t *scheduler = NULL;

	ktime_t now;

	now = ktime_get();

	if (DEBUGGER_EN(TIME))
		pr_err("rga job wait in_fence signal use time = %lld\n",
			ktime_to_us(ktime_sub(now, waiter->job->timestamp)));

	scheduler = rga_job_schedule(waiter->job);

	if (scheduler == NULL)
		pr_err("failed to get scheduler, %s(%d)\n", __func__, __LINE__);

	kfree(waiter);
}

int rga_job_commit(struct rga_req *rga_command_base, int flags)
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

	if (flags == RGA_BLIT_ASYNC) {
		ret = rga_out_fence_alloc(job);
		if (ret) {
			rga_job_free(job);
			return ret;
		}
		job->flags |= RGA_JOB_ASYNC;
		rga_command_base->out_fence_fd = rga_out_fence_get_fd(job);

		if (DEBUGGER_EN(MSG))
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

int rga_job_mpi_commit(struct rga_req *rga_command_base,
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

	if (mpi_job != NULL) {
		job->dma_buf_src0 = mpi_job->dma_buf_src0;
		job->dma_buf_src1 = mpi_job->dma_buf_src1;
		job->dma_buf_dst = mpi_job->dma_buf_dst;
	}

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
