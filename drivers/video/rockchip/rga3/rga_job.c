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
#include "rga_debugger.h"

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

struct rga_scheduler_t *rga_job_get_scheduler(struct rga_job *job)
{
	return job->scheduler;
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
	if (~job->flags & RGA_JOB_USE_HANDLE)
		rga_job_put_current_mm(job);

	free_page((unsigned long)job);
}

void rga_job_session_destroy(struct rga_session *session)
{
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_job *job_pos, *job_q;
	int i;

	unsigned long flags;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		list_for_each_entry_safe(job_pos, job_q, &scheduler->todo_list, head) {
			if (session == job_pos->session) {
				list_del(&job_pos->head);

				spin_unlock_irqrestore(&scheduler->irq_lock, flags);

				rga_job_free(job_pos);

				spin_lock_irqsave(&scheduler->irq_lock, flags);
			}
		}

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
	}
}

static int rga_job_cleanup(struct rga_job *job)
{
	if (DEBUGGER_EN(TIME))
		pr_err("(pid:%d) job clean use time = %lld\n", job->pid,
			ktime_us_delta(ktime_get(), job->timestamp));

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

		if (~mm_flag & RGA_MEM_UNDER_4G) {
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

		if (~mm_flag & RGA_MEM_UNDER_4G) {
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

		if (~mm_flag & RGA_MEM_UNDER_4G) {
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
	job->pid = current->pid;

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

static void rga_job_dump_info(struct rga_job *job)
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
		ret = rga_mm_map_buffer_info(job);
		if (ret < 0) {
			pr_err("%s: failed to map buffer\n", __func__);
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
		rga_job_dump_info(job);

	return ret;

failed:
	rga_power_disable(scheduler);

	return ret;
}

static void rga_job_next(struct rga_scheduler_t *scheduler)
{
	struct rga_job *job = NULL;
	unsigned long flags;

next_job:
	spin_lock_irqsave(&scheduler->irq_lock, flags);

	if (scheduler->running_job ||
		list_empty(&scheduler->todo_list)) {
		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
		return;
	}

	job = list_first_entry(&scheduler->todo_list, struct rga_job, head);

	list_del_init(&job->head);

	scheduler->job_count--;

	scheduler->running_job = job;

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	job->ret = rga_job_run(job, scheduler);

	/* If some error before hw run */
	if (job->ret < 0) {
		pr_err("some error on rga_job_run before hw start, %s(%d)\n",
			__func__, __LINE__);

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		scheduler->running_job = NULL;

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);

		if (job->flags & RGA_JOB_USE_HANDLE) {
			rga_mm_put_handle_info(job);
		} else {
			rga_mm_unmap_buffer_info(job);
			rga_mm_put_external_buffer(job);
		}

		if (job->use_batch_mode) {
			rga_request_release_signal(scheduler, job);
		} else {
			rga_dma_fence_signal(job->out_fence);

			job->flags |= RGA_JOB_DONE;

			if (job->flags & RGA_JOB_ASYNC)
				rga_job_cleanup(job);

			wake_up(&scheduler->job_done_wq);
		}

		goto next_job;
	}
}

static void rga_job_finish_and_next(struct rga_scheduler_t *scheduler,
		struct rga_job *job, int ret)
{
	ktime_t now;

	job->ret = ret;

	if (DEBUGGER_EN(TIME)) {
		now = ktime_get();
		pr_info("hw use time = %lld\n", ktime_us_delta(now, job->hw_running_time));
		pr_info("(pid:%d) job done use time = %lld\n", job->pid,
			ktime_us_delta(now, job->timestamp));
	}

	if (job->flags & RGA_JOB_USE_HANDLE) {
		rga_mm_put_handle_info(job);
	} else {
		rga_mm_unmap_buffer_info(job);
		rga_mm_put_external_buffer(job);
	}

	if (job->use_batch_mode)
		rga_request_release_signal(scheduler, job);
	else {
		rga_dma_fence_signal(job->out_fence);

		job->flags |= RGA_JOB_DONE;

		if (job->flags & RGA_JOB_ASYNC)
			rga_job_cleanup(job);

		wake_up(&scheduler->job_done_wq);
	}

	rga_job_next(scheduler);

	rga_power_disable(scheduler);
}

void rga_job_done(struct rga_scheduler_t *scheduler, int ret)
{
	struct rga_job *job;
	unsigned long flags;
	ktime_t now = ktime_get();

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	job = scheduler->running_job;
	scheduler->running_job = NULL;

	scheduler->timer.busy_time += ktime_us_delta(now, job->hw_recoder_time);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	if (DEBUGGER_EN(DUMP_IMAGE))
		rga_dump_job_image(job);

	rga_job_finish_and_next(scheduler, job, ret);
}

static void rga_job_timeout_clean(struct rga_scheduler_t *scheduler)
{
	unsigned long flags;
	struct rga_job *job = NULL;
	ktime_t now = ktime_get();

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	job = scheduler->running_job;
	if (job && (job->flags & RGA_JOB_ASYNC) &&
	   (ktime_ms_delta(now, job->hw_running_time) >= RGA_ASYNC_TIMEOUT_DELAY)) {
		scheduler->running_job = NULL;

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);

		scheduler->ops->soft_reset(scheduler);

		if (job->flags & RGA_JOB_USE_HANDLE) {
			rga_mm_put_handle_info(job);
		} else {
			rga_mm_unmap_buffer_info(job);
			rga_mm_put_external_buffer(job);
		}

		if (job->use_batch_mode)
			rga_request_release_signal(scheduler, job);
		else {
			rga_dma_fence_signal(job->out_fence);

			rga_job_cleanup(job);
		}

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
		job->core = rga_drvdata->scheduler[0]->core;
		job->scheduler = rga_drvdata->scheduler[0];
	}

	scheduler = rga_job_get_scheduler(job);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__, __LINE__);
		return NULL;
	}

	/* Only async will timeout clean */
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
				 struct rga_scheduler_t *scheduler)
{
	unsigned long flags;

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	/* invalid job */
	if (job == scheduler->running_job)
		scheduler->running_job = NULL;

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	rga_job_cleanup(job);
}

static void rga_invalid_job_abort(struct rga_job *job)
{
	rga_job_cleanup(job);
}

static inline int rga_job_wait(struct rga_job *job)
{
	int left_time;
	int ret;

	left_time = wait_event_timeout(job->scheduler->job_done_wq,
				       job->flags & RGA_JOB_DONE, RGA_SYNC_TIMEOUT_DELAY);

	switch (left_time) {
	case 0:
		pr_err("%s timeout", __func__);
		job->scheduler->ops->soft_reset(job->scheduler);
		ret = -EBUSY;
		break;
	case -ERESTARTSYS:
		ret = -ERESTARTSYS;
		break;
	default:
		ret = 0;
		break;
	}

	if (DEBUGGER_EN(TIME))
		pr_info("%s use time = %lld\n", __func__,
			ktime_us_delta(ktime_get(), job->hw_running_time));

	return ret;
}

static int rga_job_alloc_release_fence(struct dma_fence **release_fence)
{
	struct dma_fence *fence;

	fence = rga_dma_fence_alloc();
	if (IS_ERR(fence)) {
		pr_err("Can not alloc release fence!\n");
		return IS_ERR(fence);
	}

	*release_fence = fence;

	return rga_dma_fence_get_fd(fence);
}

static int rga_job_add_acquire_fence_callback(int acquire_fence_fd, void *private,
					      dma_fence_func_t cb_func)
{
	int ret;
	struct dma_fence *acquire_fence = NULL;

	if (DEBUGGER_EN(MSG))
		pr_info("acquire_fence_fd = %d", acquire_fence_fd);

	acquire_fence = rga_get_dma_fence_from_fd(acquire_fence_fd);
	if (IS_ERR_OR_NULL(acquire_fence)) {
		pr_err("%s: failed to get acquire dma_fence from[%d]\n",
		       __func__, acquire_fence_fd);
		return -EINVAL;
	}
	/* close acquire fence fd */
	ksys_close(acquire_fence_fd);

	ret = rga_dma_fence_get_status(acquire_fence);
	if (ret == 0) {
		ret = rga_dma_fence_add_callback(acquire_fence, cb_func, private);
		if (ret < 0) {
			if (ret == -ENOENT)
				return 1;

			pr_err("%s: failed to add fence callback\n", __func__);
			return ret;
		}
	} else {
		return ret;
	}

	return 0;
}

struct rga_job *rga_job_commit(struct rga_req *rga_command_base, struct rga_request *request)
{
	int ret;
	struct rga_job *job = NULL;
	struct rga_scheduler_t *scheduler = NULL;

	job = rga_job_alloc(rga_command_base);
	if (!job) {
		pr_err("failed to alloc rga job!\n");
		return ERR_PTR(-ENOMEM);
	}

	job->use_batch_mode = request->use_batch_mode;
	job->request_id = request->id;
	job->session = request->session;
	job->out_fence = request->release_fence;

	if (!(job->flags & RGA_JOB_USE_HANDLE)) {
		ret = rga_mm_get_external_buffer(job);
		if (ret < 0) {
			pr_err("%s: failed to get external buffer from job_cmd!\n", __func__);
			goto free_job;
		}
	}

	scheduler = rga_job_schedule(job);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__, __LINE__);
		ret = -EINVAL;
		goto invalid_job;
	}

	ret = job->ret;
	if (ret < 0) {
		pr_err("some error on job, %s(%d)\n", __func__,
			__LINE__);
		goto running_job_abort;
	}

	return job;

free_job:
	rga_job_free(job);
	return ERR_PTR(ret);

invalid_job:
	rga_invalid_job_abort(job);
	return ERR_PTR(ret);

/* only used by SYNC mode */
running_job_abort:
	rga_running_job_abort(job, scheduler);
	return ERR_PTR(ret);
}

int rga_job_mpi_commit(struct rga_req *rga_command_base, struct rga_request *request)
{
	struct rga_job *job = NULL;
	struct rga_scheduler_t *scheduler = NULL;
	int ret = 0;

	job = rga_job_alloc(rga_command_base);
	if (!job) {
		pr_err("failed to alloc rga job!\n");
		return -ENOMEM;
	}

	job->request_id = request->id;

	if (request->sync_mode == RGA_BLIT_ASYNC) {
		//TODO: mpi async mode
		pr_err("rk-debug TODO\n");
	} else if (request->sync_mode == RGA_BLIT_SYNC) {
		scheduler = rga_job_schedule(job);
		if (scheduler == NULL) {
			pr_err("failed to get scheduler, %s(%d)\n", __func__,
				 __LINE__);
			ret = -EINVAL;
			goto invalid_job;
		}

		ret = job->ret;
		if (ret < 0) {
			pr_err("some error on job, %s(%d)\n", __func__,
				 __LINE__);
			goto running_job_abort;
		}

		ret = rga_job_wait(job);
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

int rga_request_check(struct rga_user_request *req)
{
	if (req->id <= 0) {
		pr_err("user request id[%d] is invalid", req->id);
		return -EINVAL;
	}

	if (req->task_num <= 0) {
		pr_err("invalied user request!\n");
		return -EINVAL;
	}

	if (req->task_ptr == 0) {
		pr_err("task_ptr is NULL!\n");
		return -EINVAL;
	}

	if (req->task_num > RGA_TASK_NUM_MAX) {
		pr_err("Only supports running %d tasks, now %d\n",
		       RGA_TASK_NUM_MAX, req->task_num);
		return -EFBIG;
	}

	return 0;
}

struct rga_request *rga_request_lookup(struct rga_pending_request_manager *manager, uint32_t id)
{
	struct rga_request *request = NULL;

	WARN_ON(!mutex_is_locked(&manager->lock));

	request = idr_find(&manager->request_idr, id);

	return request;
}

static int rga_request_wait(struct rga_request *request)
{
	int left_time;
	int ret;

	left_time = wait_event_interruptible_timeout(request->finished_wq,
						     request->finished_task_count ==
						     request->task_count,
						     RGA_SYNC_TIMEOUT_DELAY * request->task_count);

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

	return ret;
}

int rga_request_commit(struct rga_request *request)
{
	int ret;
	int i = 0;
	struct rga_job *job = NULL;

	if (request->use_batch_mode) {
		for (i = 0; i < request->task_count; i++) {
			job = rga_job_commit(&(request->task_list[i]), request);
			if (IS_ERR_OR_NULL(job)) {
				pr_err("failed to commit job!\n");
				return job ? PTR_ERR(job) : -EFAULT;
			}
		}

		if (request->sync_mode == RGA_BLIT_SYNC) {
			ret = rga_request_wait(request);
			if (ret < 0) {
				return ret;
			}
		}
	} else {
		job = rga_job_commit(request->task_list, request);
		if (IS_ERR_OR_NULL(job)) {
			pr_err("failed to commit job!\n");
			return job ? PTR_ERR(job) : -EFAULT;
		}

		if (request->sync_mode == RGA_BLIT_SYNC) {
			ret = rga_job_wait(job);
			if (ret < 0) {
				rga_running_job_abort(job, job->scheduler);
				return ret;
			}

			rga_job_cleanup(job);
		}
	}

	return 0;
}

static void rga_request_acquire_fence_signaled_cb(struct dma_fence *fence,
						  struct dma_fence_cb *_waiter)
{
	struct rga_fence_waiter *waiter = (struct rga_fence_waiter *)_waiter;

	if (rga_request_commit((struct rga_request *)waiter->private))
		pr_err("rga request commit failed!\n");

	kfree(waiter);
}

int rga_request_release_signal(struct rga_scheduler_t *scheduler, struct rga_job *job)
{
	struct rga_pending_request_manager *request_manager;
	struct rga_request *request;
	int finished_task_count;
	unsigned long flags;

	request_manager = rga_drvdata->pend_request_manager;
	if (request_manager == NULL) {
		pr_err("rga_pending_request_manager is null!\n");
		return -EFAULT;
	}

	mutex_lock(&request_manager->lock);

	request = rga_request_lookup(request_manager, job->request_id);
	if (IS_ERR_OR_NULL(request)) {
		pr_err("can not find internal request from id[%d]", job->request_id);
		mutex_unlock(&request_manager->lock);
		return -EINVAL;
	}

	rga_request_get(request);
	mutex_unlock(&request_manager->lock);

	rga_job_cleanup(job);

	spin_lock_irqsave(&request->lock, flags);

	finished_task_count = ++request->finished_task_count;

	spin_unlock_irqrestore(&request->lock, flags);

	if (finished_task_count >= request->task_count) {
		rga_dma_fence_signal(request->release_fence);

		wake_up(&request->finished_wq);

		spin_lock_irqsave(&request->lock, flags);

		request->is_running = false;

		spin_unlock_irqrestore(&request->lock, flags);

		/* current submit request put */
		mutex_lock(&request_manager->lock);
		rga_request_put(request);
		mutex_unlock(&request_manager->lock);
	}

	mutex_lock(&request_manager->lock);
	rga_request_put(request);
	mutex_unlock(&request_manager->lock);

	return 0;
}

struct rga_request *rga_request_config(struct rga_user_request *user_request)
{
	unsigned long flags;
	struct rga_pending_request_manager *request_manager;
	struct rga_request *request;
	struct rga_req *task_list;

	request_manager = rga_drvdata->pend_request_manager;
	if (request_manager == NULL) {
		pr_err("rga_pending_request_manager is null!\n");
		return ERR_PTR(-EFAULT);
	}

	mutex_lock(&request_manager->lock);

	request = rga_request_lookup(request_manager, user_request->id);
	if (IS_ERR_OR_NULL(request)) {
		pr_err("can not find request from id[%d]", user_request->id);
		mutex_unlock(&request_manager->lock);
		return ERR_PTR(-EINVAL);
	}

	rga_request_get(request);
	mutex_unlock(&request_manager->lock);

	task_list = kmalloc_array(user_request->task_num, sizeof(struct rga_req), GFP_KERNEL);
	if (task_list == NULL) {
		pr_err("task_req list alloc error!\n");
		return ERR_PTR(-ENOMEM);
	}

	if (unlikely(copy_from_user(task_list, u64_to_user_ptr(user_request->task_ptr),
				    sizeof(struct rga_req) * user_request->task_num))) {
		pr_err("rga_user_request task list copy_from_user failed\n");
		kfree(task_list);
		return ERR_PTR(-EFAULT);
	}

	spin_lock_irqsave(&request->lock, flags);

	request->use_batch_mode = true;
	request->task_list = task_list;
	request->task_count = user_request->task_num;
	request->sync_mode = user_request->sync_mode;
	request->mpi_config_flags = user_request->mpi_config_flags;
	request->acquire_fence_fd = user_request->acquire_fence_fd;

	spin_unlock_irqrestore(&request->lock, flags);

	return request;
}

int rga_request_submit(struct rga_request *request)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&request->lock, flags);

	if (request->is_running) {
		pr_err("can not re-config when request is running");
		spin_unlock_irqrestore(&request->lock, flags);
		return -EFAULT;
	}

	/* Reset */
	request->finished_task_count = 0;

	if (request->task_list == NULL) {
		pr_err("can not find task list from id[%d]", request->id);
		spin_unlock_irqrestore(&request->lock, flags);
		return -EINVAL;
	}

	request->is_running = true;

	spin_unlock_irqrestore(&request->lock, flags);

	if (request->sync_mode == RGA_BLIT_ASYNC) {
		ret = rga_job_alloc_release_fence(&request->release_fence);
		if (ret < 0) {
			pr_err("Failed to alloc release fence fd!\n");
			return ret;
		}
		request->release_fence_fd = ret;

		if (request->acquire_fence_fd > 0) {
			ret = rga_job_add_acquire_fence_callback(
				request->acquire_fence_fd,
				(void *)request,
				rga_request_acquire_fence_signaled_cb);
			if (ret == 0) {
				return ret;
			} else if (ret == 1) {
				goto request_commit;
			} else {
				pr_err("Failed to add callback with acquire fence fd[%d]!\n",
				       request->acquire_fence_fd);
				goto error_release_fence_put;
			}
		}

	}

request_commit:
	ret = rga_request_commit(request);
	if (ret < 0) {
		pr_err("rga request commit failed!\n");
		goto error_release_fence_put;
	}

	return 0;

error_release_fence_put:
	rga_dma_fence_put(request->release_fence);
	return ret;
}

static int rga_request_free(struct rga_request *request)
{
	struct rga_pending_request_manager *request_manager;
	struct rga_req *task_list;
	unsigned long flags;

	request_manager = rga_drvdata->pend_request_manager;
	if (request_manager == NULL) {
		pr_err("rga_pending_request_manager is null!\n");
		return -EFAULT;
	}

	WARN_ON(!mutex_is_locked(&request_manager->lock));

	if (IS_ERR_OR_NULL(request)) {
		pr_err("request already freed");
		return -EFAULT;
	}

	request_manager->request_count--;
	idr_remove(&request_manager->request_idr, request->id);

	spin_lock_irqsave(&request->lock, flags);

	task_list = request->task_list;

	spin_unlock_irqrestore(&request->lock, flags);

	if (task_list != NULL)
		kfree(task_list);

	kfree(request);

	return 0;
}

static void rga_request_kref_release(struct kref *ref)
{
	struct rga_request *request;
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_job *job_pos, *job_q, *job;
	int i;
	bool need_reset = false;
	unsigned long flags;
	ktime_t now = ktime_get();

	request = container_of(ref, struct rga_request, refcount);

	spin_lock_irqsave(&request->lock, flags);

	rga_dma_fence_put(request->release_fence);

	if (!request->is_running || request->finished_task_count >= request->task_count) {
		spin_unlock_irqrestore(&request->lock, flags);
		goto free_request;
	}

	spin_unlock_irqrestore(&request->lock, flags);

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		list_for_each_entry_safe(job_pos, job_q, &scheduler->todo_list, head) {
			if (request->id == job_pos->request_id) {
				job = job_pos;
				list_del_init(&job_pos->head);

				scheduler->job_count--;
			}
		}

		if (scheduler->running_job) {
			job = scheduler->running_job;

			if (job->request_id == request->id) {
				scheduler->running_job = NULL;
				scheduler->timer.busy_time += ktime_us_delta(now, job->hw_recoder_time);
				need_reset = true;
			}
		}

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);

		if (need_reset) {
			pr_info("reset core[%d] by user cancel", scheduler->core);
			scheduler->ops->soft_reset(scheduler);

			rga_job_finish_and_next(scheduler, job, 0);
		}
	}

free_request:
	rga_request_free(request);
}

/*
 * Called at driver close to release the request's id references.
 */
static int rga_request_free_cb(int id, void *ptr, void *data)
{
	return rga_request_free((struct rga_request *)ptr);
}

uint32_t rga_request_alloc(uint32_t flags, struct rga_session *session)
{
	struct rga_pending_request_manager *request_manager;
	struct rga_request *request;

	request_manager = rga_drvdata->pend_request_manager;
	if (request_manager == NULL) {
		pr_err("rga_pending_request_manager is null!\n");
		return -EFAULT;
	}

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (request == NULL) {
		pr_err("can not kzalloc for rga_request\n");
		return -ENOMEM;
	}

	spin_lock_init(&request->lock);
	init_waitqueue_head(&request->finished_wq);

	request->pid = current->pid;
	request->flags = flags;
	request->session = session;
	kref_init(&request->refcount);

	/*
	 * Get the user-visible handle using idr. Preload and perform
	 * allocation under our spinlock.
	 */
	mutex_lock(&request_manager->lock);

	idr_preload(GFP_KERNEL);
	request->id = idr_alloc(&request_manager->request_idr, request, 1, 0, GFP_KERNEL);
	idr_preload_end();

	if (request->id <= 0) {
		pr_err("alloc request_id failed!\n");

		mutex_unlock(&request_manager->lock);
		kfree(request);
		return -EFAULT;
	}

	request_manager->request_count++;

	mutex_unlock(&request_manager->lock);

	return (uint32_t)request->id;
}

int rga_request_put(struct rga_request *request)
{
	return kref_put(&request->refcount, rga_request_kref_release);
}

void rga_request_get(struct rga_request *request)
{
	kref_get(&request->refcount);
}

int rga_request_manager_init(struct rga_pending_request_manager **request_manager_session)
{
	struct rga_pending_request_manager *request_manager = NULL;

	*request_manager_session = kzalloc(sizeof(struct rga_pending_request_manager), GFP_KERNEL);
	if (*request_manager_session == NULL) {
		pr_err("can not kzalloc for rga_pending_request_manager\n");
		return -ENOMEM;
	}

	request_manager = *request_manager_session;

	mutex_init(&request_manager->lock);

	idr_init_base(&request_manager->request_idr, 1);

	return 0;
}

int rga_request_manager_remove(struct rga_pending_request_manager **request_manager_session)
{
	struct rga_pending_request_manager *request_manager = *request_manager_session;

	mutex_lock(&request_manager->lock);

	idr_for_each(&request_manager->request_idr, &rga_request_free_cb, request_manager);
	idr_destroy(&request_manager->request_idr);

	mutex_unlock(&request_manager->lock);

	kfree(*request_manager_session);

	*request_manager_session = NULL;

	return 0;
}
