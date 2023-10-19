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
#include "rga_iommu.h"
#include "rga_debugger.h"

static void rga_job_free(struct rga_job *job)
{
	free_page((unsigned long)job);
}

static void rga_job_kref_release(struct kref *ref)
{
	struct rga_job *job;

	job = container_of(ref, struct rga_job, refcount);

	rga_job_free(job);
}

static int rga_job_put(struct rga_job *job)
{
	return kref_put(&job->refcount, rga_job_kref_release);
}

static void rga_job_get(struct rga_job *job)
{
	kref_get(&job->refcount);
}

static int rga_job_cleanup(struct rga_job *job)
{
	if (DEBUGGER_EN(TIME))
		pr_err("(pid:%d) job clean use time = %lld\n", job->pid,
			ktime_us_delta(ktime_get(), job->timestamp));

	rga_job_put(job);

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
			job->flags |= RGA_JOB_UNSUPPORT_RGA_MMU;
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
			job->flags |= RGA_JOB_UNSUPPORT_RGA_MMU;
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
			job->flags |= RGA_JOB_UNSUPPORT_RGA_MMU;
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

	INIT_LIST_HEAD(&job->head);
	kref_init(&job->refcount);

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
	}

	return job;
}

static void rga_job_dump_info(struct rga_job *job)
{
	pr_info("job: reqeust_id = %d, priority = %d, core = %d\n",
		job->request_id, job->priority, job->core);
}

void rga_job_scheduler_dump_info(struct rga_scheduler_t *scheduler)
{
	struct rga_job *job_pos;

	lockdep_assert_held(&scheduler->irq_lock);

	pr_info("===============================================================\n");
	pr_info("%s core = %d job_count = %d status = %d\n",
		dev_driver_string(scheduler->dev),
		scheduler->core, scheduler->job_count, scheduler->status);

	if (scheduler->running_job)
		rga_job_dump_info(scheduler->running_job);

	list_for_each_entry(job_pos, &scheduler->todo_list, head) {
		rga_job_dump_info(job_pos);
	}

	pr_info("===============================================================\n");
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

	ret = scheduler->ops->set_reg(job, scheduler);
	if (ret < 0) {
		pr_err("set reg failed");
		rga_power_disable(scheduler);
		return ret;
	}

	set_bit(RGA_JOB_STATE_RUNNING, &job->state);

	/* for debug */
	if (DEBUGGER_EN(MSG))
		rga_job_dump_info(job);

	return ret;
}

void rga_job_next(struct rga_scheduler_t *scheduler)
{
	int ret;
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
	set_bit(RGA_JOB_STATE_PREPARE, &job->state);
	rga_job_get(job);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	ret = rga_job_run(job, scheduler);
	/* If some error before hw run */
	if (ret < 0) {
		pr_err("some error on rga_job_run before hw start, %s(%d)\n", __func__, __LINE__);

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		scheduler->running_job = NULL;
		rga_job_put(job);

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);

		job->ret = ret;
		rga_request_release_signal(scheduler, job);

		goto next_job;
	}

	rga_job_put(job);
}

struct rga_job *rga_job_done(struct rga_scheduler_t *scheduler)
{
	struct rga_job *job;
	unsigned long flags;
	ktime_t now = ktime_get();

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	job = scheduler->running_job;
	if (job == NULL) {
		pr_err("core[0x%x] running job has been cleanup.\n", scheduler->core);

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
		return NULL;
	}
	scheduler->running_job = NULL;

	scheduler->timer.busy_time += ktime_us_delta(now, job->hw_recoder_time);
	set_bit(RGA_JOB_STATE_DONE, &job->state);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	if (scheduler->ops->read_back_reg)
		scheduler->ops->read_back_reg(job, scheduler);

	if (DEBUGGER_EN(DUMP_IMAGE))
		rga_dump_job_image(job);

	if (DEBUGGER_EN(TIME)) {
		pr_info("hw use time = %lld\n", ktime_us_delta(now, job->hw_running_time));
		pr_info("(pid:%d) job done use time = %lld\n", job->pid,
			ktime_us_delta(now, job->timestamp));
	}

	rga_mm_unmap_job_info(job);

	return job;
}

static void rga_job_scheduler_timeout_clean(struct rga_scheduler_t *scheduler)
{
	unsigned long flags;
	struct rga_job *job = NULL;

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	if (scheduler->running_job == NULL || scheduler->running_job->hw_running_time == 0) {
		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
		return;
	}

	job = scheduler->running_job;
	if (ktime_ms_delta(ktime_get(), job->hw_running_time) >= RGA_JOB_TIMEOUT_DELAY) {
		scheduler->running_job = NULL;
		scheduler->status = RGA_SCHEDULER_ABORT;
		scheduler->ops->soft_reset(scheduler);

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);

		rga_mm_unmap_job_info(job);

		job->ret = -EBUSY;
		rga_request_release_signal(scheduler, job);

		rga_power_disable(scheduler);
	} else {
		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
	}
}

static void rga_job_insert_todo_list(struct rga_job *job)
{
	bool first_match = 0;
	unsigned long flags;
	struct rga_job *job_pos;
	struct rga_scheduler_t *scheduler = job->scheduler;

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
	set_bit(RGA_JOB_STATE_PENDING, &job->state);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);
}

static struct rga_scheduler_t *rga_job_schedule(struct rga_job *job)
{
	int i;
	struct rga_scheduler_t *scheduler = NULL;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];
		rga_job_scheduler_timeout_clean(scheduler);
	}

	if (rga_drvdata->num_of_scheduler > 1) {
		job->core = rga_job_assign(job);
		if (job->core <= 0) {
			pr_err("job assign failed");
			job->ret = -EINVAL;
			return NULL;
		}
	} else {
		job->core = rga_drvdata->scheduler[0]->core;
		job->scheduler = rga_drvdata->scheduler[0];
	}

	scheduler = job->scheduler;
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__, __LINE__);
		job->ret = -EFAULT;
		return NULL;
	}

	return scheduler;
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
	job->mm = request->current_mm;

	scheduler = rga_job_schedule(job);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__, __LINE__);
		goto err_free_job;
	}

	/* Memory mapping needs to keep pd enabled. */
	if (rga_power_enable(scheduler) < 0) {
		pr_err("power enable failed");
		job->ret = -EFAULT;
		goto err_free_job;
	}

	ret = rga_mm_map_job_info(job);
	if (ret < 0) {
		pr_err("%s: failed to map job info\n", __func__);
		job->ret = ret;
		goto err_power_disable;
	}

	ret = scheduler->ops->init_reg(job);
	if (ret < 0) {
		pr_err("%s: init reg failed", __func__);
		job->ret = ret;
		goto err_unmap_job_info;
	}

	rga_job_insert_todo_list(job);

	rga_job_next(scheduler);

	rga_power_disable(scheduler);

	return job;

err_unmap_job_info:
	rga_mm_unmap_job_info(job);

err_power_disable:
	rga_power_disable(scheduler);

err_free_job:
	ret = job->ret;
	rga_request_release_signal(scheduler, job);

	return ERR_PTR(ret);
}

static bool rga_is_need_current_mm(struct rga_req *req)
{
	int mmu_flag;
	struct rga_img_info_t *src0 = NULL;
	struct rga_img_info_t *src1 = NULL;
	struct rga_img_info_t *dst = NULL;
	struct rga_img_info_t *els = NULL;

	src0 = &req->src;
	dst = &req->dst;
	if (req->render_mode != UPDATE_PALETTE_TABLE_MODE)
		src1 = &req->pat;
	else
		els = &req->pat;

	if (likely(src0 != NULL)) {
		mmu_flag = ((req->mmu_info.mmu_flag >> 8) & 1);
		if (mmu_flag && src0->uv_addr)
			return true;
	}

	if (likely(dst != NULL)) {
		mmu_flag = ((req->mmu_info.mmu_flag >> 10) & 1);
		if (mmu_flag && dst->uv_addr)
			return true;
	}

	if (src1 != NULL) {
		mmu_flag = ((req->mmu_info.mmu_flag >> 9) & 1);
		if (mmu_flag && src1->uv_addr)
			return true;
	}

	if (els != NULL) {
		mmu_flag = ((req->mmu_info.mmu_flag >> 11) & 1);
		if (mmu_flag && els->uv_addr)
			return true;
	}

	return false;
}

static int rga_request_get_current_mm(struct rga_request *request)
{
	int i;

	for (i = 0; i < request->task_count; i++) {
		if (rga_is_need_current_mm(&(request->task_list[i]))) {
			mmgrab(current->mm);
			mmget(current->mm);
			request->current_mm = current->mm;

			break;
		}
	}

	return 0;
}

static void rga_request_put_current_mm(struct rga_request *request)
{
	if (request->current_mm == NULL)
		return;

	mmput(request->current_mm);
	mmdrop(request->current_mm);
	request->current_mm = NULL;
}

static int rga_request_add_acquire_fence_callback(int acquire_fence_fd,
						  struct rga_request *request,
						  dma_fence_func_t cb_func)
{
	int ret;
	struct dma_fence *acquire_fence = NULL;
	struct rga_pending_request_manager *request_manager = rga_drvdata->pend_request_manager;

	if (DEBUGGER_EN(MSG))
		pr_info("acquire_fence_fd = %d", acquire_fence_fd);

	acquire_fence = rga_get_dma_fence_from_fd(acquire_fence_fd);
	if (IS_ERR_OR_NULL(acquire_fence)) {
		pr_err("%s: failed to get acquire dma_fence from[%d]\n",
		       __func__, acquire_fence_fd);
		return -EINVAL;
	}

	if (!request->feature.user_close_fence) {
		/* close acquire fence fd */
#ifdef CONFIG_NO_GKI
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		close_fd(acquire_fence_fd);
#else
		ksys_close(acquire_fence_fd);
#endif
#else
		pr_err("Please update the driver to v1.2.28 to prevent acquire_fence_fd leaks.");
		return -EFAULT;
#endif
	}


	ret = rga_dma_fence_get_status(acquire_fence);
	if (ret < 0) {
		pr_err("%s: Current acquire fence unexpectedly has error status before signal\n",
		       __func__);
		return ret;
	} else if (ret > 0) {
		/* has been signaled */
		return ret;
	}

	/*
	 * Ensure that the request will not be free early when
	 * the callback is called.
	 */
	mutex_lock(&request_manager->lock);
	rga_request_get(request);
	mutex_unlock(&request_manager->lock);

	ret = rga_dma_fence_add_callback(acquire_fence, cb_func, (void *)request);
	if (ret < 0) {
		if (ret != -ENOENT)
			pr_err("%s: failed to add fence callback\n", __func__);

		mutex_lock(&request_manager->lock);
		rga_request_put(request);
		mutex_unlock(&request_manager->lock);
		return ret;
	}

	return 0;
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

static int rga_request_scheduler_job_abort(struct rga_request *request)
{
	int i;
	unsigned long flags;
	enum rga_scheduler_status scheduler_status;
	int running_abort_count = 0, todo_abort_count = 0;
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_job *job, *job_q;
	LIST_HEAD(list_to_free);

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];
		spin_lock_irqsave(&scheduler->irq_lock, flags);

		list_for_each_entry_safe(job, job_q, &scheduler->todo_list, head) {
			if (request->id == job->request_id) {
				list_move(&job->head, &list_to_free);
				scheduler->job_count--;

				todo_abort_count++;
			}
		}

		job = NULL;
		if (scheduler->running_job) {
			if (request->id == scheduler->running_job->request_id) {
				job = scheduler->running_job;
				scheduler_status = scheduler->status;
				scheduler->running_job = NULL;
				scheduler->status = RGA_SCHEDULER_ABORT;
				list_add_tail(&job->head, &list_to_free);

				if (job->hw_running_time != 0) {
					scheduler->timer.busy_time +=
						ktime_us_delta(ktime_get(), job->hw_recoder_time);
					scheduler->ops->soft_reset(scheduler);
				}

				pr_err("reset core[%d] by request[%d] abort",
				       scheduler->core, request->id);
				running_abort_count++;
			}
		}

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);

		if (job && scheduler_status == RGA_SCHEDULER_WORKING)
			rga_power_disable(scheduler);
	}

	/* Clean up the jobs in the todo list that need to be free. */
	list_for_each_entry_safe(job, job_q, &list_to_free, head) {
		rga_mm_unmap_job_info(job);

		job->ret = -EBUSY;
		rga_job_cleanup(job);
	}

	/* This means it has been cleaned up. */
	if (running_abort_count + todo_abort_count == 0)
		return 1;

	pr_err("request[%d] abort! finished %d failed %d running_abort %d todo_abort %d\n",
	       request->id, request->finished_task_count, request->failed_task_count,
	       running_abort_count, todo_abort_count);

	return 0;
}

static void rga_request_release_abort(struct rga_request *request, int err_code)
{
	unsigned long flags;
	struct rga_pending_request_manager *request_manager = rga_drvdata->pend_request_manager;

	if (rga_request_scheduler_job_abort(request) > 0)
		return;

	spin_lock_irqsave(&request->lock, flags);

	if (request->is_done) {
		spin_unlock_irqrestore(&request->lock, flags);
		return;
	}

	request->is_running = false;
	request->is_done = false;

	rga_request_put_current_mm(request);

	spin_unlock_irqrestore(&request->lock, flags);

	rga_dma_fence_signal(request->release_fence, err_code);

	mutex_lock(&request_manager->lock);
	/* current submit request put */
	rga_request_put(request);
	mutex_unlock(&request_manager->lock);
}

void rga_request_session_destroy_abort(struct rga_session *session)
{
	int request_id;
	struct rga_request *request;
	struct rga_pending_request_manager *request_manager;

	request_manager = rga_drvdata->pend_request_manager;
	if (request_manager == NULL) {
		pr_err("rga_pending_request_manager is null!\n");
		return;
	}

	mutex_lock(&request_manager->lock);

	idr_for_each_entry(&request_manager->request_idr, request, request_id) {
		if (session == request->session) {
			pr_err("[tgid:%d pid:%d] destroy request[%d] when the user exits",
			       session->tgid, current->pid, request->id);
			rga_request_put(request);
		}
	}

	mutex_unlock(&request_manager->lock);
}

static int rga_request_timeout_query_state(struct rga_request *request)
{
	int i;
	unsigned long flags;
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_job *job = NULL;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		if (scheduler->running_job) {
			job = scheduler->running_job;
			if (request->id == job->request_id) {
				if (test_bit(RGA_JOB_STATE_DONE, &job->state) &&
				    test_bit(RGA_JOB_STATE_FINISH, &job->state)) {
					spin_unlock_irqrestore(&scheduler->irq_lock, flags);
					return request->ret;
				} else if (!test_bit(RGA_JOB_STATE_DONE, &job->state) &&
					   test_bit(RGA_JOB_STATE_FINISH, &job->state)) {
					spin_unlock_irqrestore(&scheduler->irq_lock, flags);
					pr_err("request[%d] hardware has finished, but the software has timeout!\n",
					       request->id);
					return -EBUSY;
				} else if (!test_bit(RGA_JOB_STATE_DONE, &job->state) &&
					   !test_bit(RGA_JOB_STATE_FINISH, &job->state)) {
					spin_unlock_irqrestore(&scheduler->irq_lock, flags);
					pr_err("request[%d] hardware has timeout.\n", request->id);
					return -EBUSY;
				}
			}
		}

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
	}

	return request->ret;
}

static int rga_request_wait(struct rga_request *request)
{
	int left_time;
	int ret;

	left_time = wait_event_timeout(request->finished_wq, request->is_done,
				       RGA_JOB_TIMEOUT_DELAY * request->task_count);

	switch (left_time) {
	case 0:
		ret = rga_request_timeout_query_state(request);
		goto err_request_abort;
	case -ERESTARTSYS:
		ret = -ERESTARTSYS;
		goto err_request_abort;
	default:
		ret = request->ret;
		break;
	}

	return ret;

err_request_abort:
	rga_request_release_abort(request, ret);

	return ret;
}

int rga_request_commit(struct rga_request *request)
{
	int ret;
	int i = 0;
	struct rga_job *job;

	for (i = 0; i < request->task_count; i++) {
		struct rga_req *req = &(request->task_list[i]);

		if (DEBUGGER_EN(MSG)) {
			pr_info("commit request[%d] task[%d]:\n", request->id, i);
			rga_cmd_print_debug_info(req);
		}

		job = rga_job_commit(req, request);
		if (IS_ERR(job)) {
			pr_err("request[%d] task[%d] job_commit failed.\n", request->id, i);
			rga_request_release_abort(request, PTR_ERR(job));

			return PTR_ERR(job);
		}
	}

	if (request->sync_mode == RGA_BLIT_SYNC) {
		ret = rga_request_wait(request);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void rga_request_acquire_fence_signaled_cb(struct dma_fence *fence,
						  struct dma_fence_cb *_waiter)
{
	struct rga_fence_waiter *waiter = (struct rga_fence_waiter *)_waiter;
	struct rga_request *request = (struct rga_request *)waiter->private;
	struct rga_pending_request_manager *request_manager = rga_drvdata->pend_request_manager;

	if (rga_request_commit(request))
		pr_err("rga request[%d] commit failed!\n", request->id);

	mutex_lock(&request_manager->lock);
	rga_request_put(request);
	mutex_unlock(&request_manager->lock);

	kfree(waiter);
}

int rga_request_release_signal(struct rga_scheduler_t *scheduler, struct rga_job *job)
{
	struct rga_pending_request_manager *request_manager;
	struct rga_request *request;
	int finished_count, failed_count;
	bool is_finished = false;
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

	spin_lock_irqsave(&request->lock, flags);

	if (job->ret < 0) {
		request->failed_task_count++;
		request->ret = job->ret;
	} else {
		request->finished_task_count++;
	}

	failed_count = request->failed_task_count;
	finished_count = request->finished_task_count;

	spin_unlock_irqrestore(&request->lock, flags);

	rga_job_cleanup(job);

	if ((failed_count + finished_count) >= request->task_count) {
		spin_lock_irqsave(&request->lock, flags);

		request->is_running = false;
		request->is_done = true;

		rga_request_put_current_mm(request);

		spin_unlock_irqrestore(&request->lock, flags);

		rga_dma_fence_signal(request->release_fence, request->ret);

		is_finished = true;

		if (DEBUGGER_EN(MSG))
			pr_info("request[%d] finished %d failed %d\n",
				request->id, finished_count, failed_count);

		/* current submit request put */
		mutex_lock(&request_manager->lock);
		rga_request_put(request);
		mutex_unlock(&request_manager->lock);
	}

	mutex_lock(&request_manager->lock);

	if (is_finished)
		wake_up(&request->finished_wq);

	rga_request_put(request);

	mutex_unlock(&request_manager->lock);

	return 0;
}

struct rga_request *rga_request_config(struct rga_user_request *user_request)
{
	int ret;
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
		ret = -ENOMEM;
		goto err_put_request;
	}

	if (unlikely(copy_from_user(task_list, u64_to_user_ptr(user_request->task_ptr),
				    sizeof(struct rga_req) * user_request->task_num))) {
		pr_err("rga_user_request task list copy_from_user failed\n");
		ret = -EFAULT;
		goto err_free_task_list;
	}

	spin_lock_irqsave(&request->lock, flags);

	request->use_batch_mode = true;
	request->task_list = task_list;
	request->task_count = user_request->task_num;
	request->sync_mode = user_request->sync_mode;
	request->mpi_config_flags = user_request->mpi_config_flags;
	request->acquire_fence_fd = user_request->acquire_fence_fd;
	request->feature = task_list[0].feature;

	spin_unlock_irqrestore(&request->lock, flags);

	return request;

err_free_task_list:
	kfree(task_list);
err_put_request:
	mutex_lock(&request_manager->lock);
	rga_request_put(request);
	mutex_unlock(&request_manager->lock);

	return ERR_PTR(ret);
}

struct rga_request *rga_request_kernel_config(struct rga_user_request *user_request)
{
	int ret = 0;
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
		ret = -ENOMEM;
		goto err_put_request;
	}

	memcpy(task_list, u64_to_user_ptr(user_request->task_ptr),
	       sizeof(struct rga_req) * user_request->task_num);

	spin_lock_irqsave(&request->lock, flags);

	request->use_batch_mode = true;
	request->task_list = task_list;
	request->task_count = user_request->task_num;
	request->sync_mode = user_request->sync_mode;
	request->mpi_config_flags = user_request->mpi_config_flags;
	request->acquire_fence_fd = user_request->acquire_fence_fd;

	spin_unlock_irqrestore(&request->lock, flags);

	return request;

err_put_request:
	mutex_lock(&request_manager->lock);
	rga_request_put(request);
	mutex_unlock(&request_manager->lock);

	return ERR_PTR(ret);
}

int rga_request_submit(struct rga_request *request)
{
	int ret = 0;
	unsigned long flags;
	struct dma_fence *release_fence;

	spin_lock_irqsave(&request->lock, flags);

	if (request->is_running) {
		spin_unlock_irqrestore(&request->lock, flags);

		pr_err("can not re-config when request is running\n");
		return -EFAULT;
	}

	if (request->task_list == NULL) {
		spin_unlock_irqrestore(&request->lock, flags);

		pr_err("can not find task list from id[%d]\n", request->id);
		return -EINVAL;
	}

	/* Reset */
	request->is_running = true;
	request->is_done = false;
	request->finished_task_count = 0;
	request->failed_task_count = 0;

	rga_request_get_current_mm(request);

	/* Unlock after ensuring that the current request will not be resubmitted. */
	spin_unlock_irqrestore(&request->lock, flags);

	if (request->sync_mode == RGA_BLIT_ASYNC) {
		release_fence = rga_dma_fence_alloc();
		if (IS_ERR(release_fence)) {
			pr_err("Can not alloc release fence!\n");
			ret = IS_ERR(release_fence);
			goto error_put_current_mm;
		}
		request->release_fence = release_fence;

		if (request->acquire_fence_fd > 0) {
			ret = rga_request_add_acquire_fence_callback(
				request->acquire_fence_fd, request,
				rga_request_acquire_fence_signaled_cb);
			if (ret == 0) {
				/* acquire fence active */
				goto export_release_fence_fd;
			} else if (ret > 0) {
				/* acquire fence has been signaled */
				goto request_commit;
			} else {
				pr_err("Failed to add callback with acquire fence fd[%d]!\n",
				       request->acquire_fence_fd);
				goto err_put_release_fence;
			}
		}
	}

request_commit:
	ret = rga_request_commit(request);
	if (ret < 0) {
		pr_err("rga request[%d] commit failed!\n", request->id);
		goto err_put_release_fence;
	}

export_release_fence_fd:
	if (request->release_fence != NULL) {
		ret = rga_dma_fence_get_fd(request->release_fence);
		if (ret < 0) {
			pr_err("Failed to alloc release fence fd!\n");
			rga_request_release_abort(request, ret);
			return ret;
		}

		request->release_fence_fd = ret;
	}

	return 0;

err_put_release_fence:
	if (request->release_fence != NULL) {
		rga_dma_fence_put(request->release_fence);
		request->release_fence = NULL;
	}

error_put_current_mm:
	spin_lock_irqsave(&request->lock, flags);

	rga_request_put_current_mm(request);
	request->is_running = false;

	spin_unlock_irqrestore(&request->lock, flags);

	return ret;
}

int rga_request_mpi_submit(struct rga_req *req, struct rga_request *request)
{
	int ret = 0;
	struct rga_job *job = NULL;
	unsigned long flags;

	if (request->sync_mode == RGA_BLIT_ASYNC) {
		pr_err("mpi unsupported async mode!\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&request->lock, flags);

	if (request->is_running) {
		pr_err("can not re-config when request is running");
		spin_unlock_irqrestore(&request->lock, flags);
		return -EFAULT;
	}

	if (request->task_list == NULL) {
		pr_err("can not find task list from id[%d]", request->id);
		spin_unlock_irqrestore(&request->lock, flags);
		return -EINVAL;
	}

	/* Reset */
	request->is_running = true;
	request->is_done = false;
	request->finished_task_count = 0;
	request->failed_task_count = 0;

	spin_unlock_irqrestore(&request->lock, flags);

	job = rga_job_commit(req, request);
	if (IS_ERR_OR_NULL(job)) {
		pr_err("failed to commit job!\n");
		return job ? PTR_ERR(job) : -EFAULT;
	}

	ret = rga_request_wait(request);
	if (ret < 0)
		return ret;

	return 0;
}

int rga_request_free(struct rga_request *request)
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
	unsigned long flags;

	request = container_of(ref, struct rga_request, refcount);

	if (rga_dma_fence_get_status(request->release_fence) == 0)
		rga_dma_fence_signal(request->release_fence, -EFAULT);

	spin_lock_irqsave(&request->lock, flags);

	rga_request_put_current_mm(request);
	rga_dma_fence_put(request->release_fence);

	if (!request->is_running || request->is_done) {
		spin_unlock_irqrestore(&request->lock, flags);
		goto free_request;
	}

	spin_unlock_irqrestore(&request->lock, flags);

	rga_request_scheduler_job_abort(request);

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

int rga_request_alloc(uint32_t flags, struct rga_session *session)
{
	int new_id;
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
	new_id = idr_alloc_cyclic(&request_manager->request_idr, request, 1, 0, GFP_NOWAIT);
	idr_preload_end();
	if (new_id < 0) {
		pr_err("request alloc id failed!\n");

		mutex_unlock(&request_manager->lock);
		kfree(request);
		return new_id;
	}

	request->id = new_id;
	request_manager->request_count++;

	mutex_unlock(&request_manager->lock);

	return request->id;
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
