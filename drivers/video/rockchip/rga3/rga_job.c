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
	ktime_t now = ktime_get();

	if (DEBUGGER_EN(TIME)) {
		pr_err("(pid:%d) job clean use time = %lld\n", job->pid,
			ktime_us_delta(now, job->timestamp));
	}

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

struct rga_internal_ctx_t *
rga_internal_ctx_lookup(struct rga_pending_ctx_manager *ctx_manager, uint32_t id)
{
	struct rga_internal_ctx_t *ctx = NULL;

	mutex_lock(&ctx_manager->lock);

	ctx = idr_find(&ctx_manager->ctx_id_idr, id);

	mutex_unlock(&ctx_manager->lock);

	return ctx;
}

/*
 * Called at driver close to release the internal ctx's id references.
 */
static int rga_internal_ctx_free_remove_idr_cb(int id, void *ptr, void *data)
{
	struct rga_internal_ctx_t *ctx = ptr;

	idr_remove(&rga_drvdata->pend_ctx_manager->ctx_id_idr, ctx->id);
	if (ctx->cached_cmd != NULL) {
		kfree(ctx->cached_cmd);
		ctx->cached_cmd = NULL;
	}
	kfree(ctx);

	return 0;
}

static int rga_internal_ctx_free_remove_idr(struct rga_internal_ctx_t *ctx)
{
	struct rga_pending_ctx_manager *ctx_manager;
	struct rga_req *cached_cmd;
	unsigned long flags;

	ctx_manager = rga_drvdata->pend_ctx_manager;

	if (IS_ERR_OR_NULL(ctx)) {
		pr_err("ctx already freed");
		return -EFAULT;
	}

	mutex_lock(&ctx_manager->lock);

	ctx_manager->ctx_count--;
	idr_remove(&ctx_manager->ctx_id_idr, ctx->id);

	mutex_unlock(&ctx_manager->lock);

	spin_lock_irqsave(&ctx->lock, flags);

	cached_cmd = ctx->cached_cmd;

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (cached_cmd != NULL)
		kfree(cached_cmd);

	kfree(ctx);

	return 0;
}

static int rga_internal_ctx_signal(struct rga_scheduler_t *scheduler, struct rga_job *job)
{
	struct rga_pending_ctx_manager *ctx_manager;
	struct rga_internal_ctx_t *ctx;
	int finished_job_count;
	unsigned long flags;

	ctx_manager = rga_drvdata->pend_ctx_manager;

	ctx = rga_internal_ctx_lookup(ctx_manager, job->ctx_id);
	if (IS_ERR_OR_NULL(ctx)) {
		pr_err("can not find internal ctx from id[%d]", job->ctx_id);
		return -EINVAL;
	}

	spin_lock_irqsave(&ctx->lock, flags);

	finished_job_count = ++ctx->finished_job_count;

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (finished_job_count >= ctx->cmd_num) {
		if (ctx->out_fence)
			dma_fence_signal(ctx->out_fence);

		job->flags |= RGA_JOB_DONE;

		if (job->flags & RGA_JOB_ASYNC)
			rga_job_cleanup(job);

		wake_up(&scheduler->job_done_wq);

		spin_lock_irqsave(&ctx->lock, flags);

		ctx->is_running = false;

		spin_unlock_irqrestore(&ctx->lock, flags);
	}

	return 0;
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
		rga_job_dump_info(job);

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

		if (job->use_batch_mode) {
			rga_internal_ctx_signal(rga_scheduler, job);
		} else {
			if (job->out_fence)
				dma_fence_signal(job->out_fence);

			job->flags |= RGA_JOB_DONE;

			if (job->flags & RGA_JOB_ASYNC)
				rga_job_cleanup(job);

			wake_up(&rga_scheduler->job_done_wq);
		}

		goto next_job;
	}
}

static void rga_job_finish_and_next(struct rga_scheduler_t *rga_scheduler,
		struct rga_job *job, int ret)
{
	ktime_t now;

	job->ret = ret;

	if (DEBUGGER_EN(TIME)) {
		now = ktime_get();
		pr_err("hw use time = %lld\n", ktime_us_delta(now, job->hw_running_time));
		pr_err("(pid:%d) job done use time = %lld\n", job->pid,
			ktime_us_delta(now, job->timestamp));
	}

	if (job->core == RGA2_SCHEDULER_CORE0)
		rga2_dma_flush_cache_for_virtual_address(&job->vir_page_table,
			rga_scheduler);

	if (job->flags & RGA_JOB_USE_HANDLE)
		rga_mm_put_handle_info(job);
	else
		rga_dma_put_info(job);

	if (job->use_batch_mode)
		rga_internal_ctx_signal(rga_scheduler, job);
	else {
		if (job->out_fence)
			dma_fence_signal(job->out_fence);

		job->flags |= RGA_JOB_DONE;

		if (job->flags & RGA_JOB_ASYNC)
			rga_job_cleanup(job);

		wake_up(&rga_scheduler->job_done_wq);
	}

	rga_job_next(rga_scheduler);

	rga_power_disable(rga_scheduler);
}

void rga_job_done(struct rga_scheduler_t *rga_scheduler, int ret)
{
	struct rga_job *job;
	unsigned long flags;
	ktime_t now = ktime_get();

	spin_lock_irqsave(&rga_scheduler->irq_lock, flags);

	job = rga_scheduler->running_job;
	rga_scheduler->running_job = NULL;

	rga_scheduler->timer.busy_time += ktime_us_delta(now, job->hw_recoder_time);

	spin_unlock_irqrestore(&rga_scheduler->irq_lock, flags);

	rga_job_finish_and_next(rga_scheduler, job, ret);
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

		if (job->flags & RGA_JOB_USE_HANDLE)
			rga_mm_put_handle_info(job);
		else
			rga_dma_put_info(job);

		if (job->use_batch_mode)
			rga_internal_ctx_signal(scheduler, job);
		else {
			if (job->out_fence)
				dma_fence_signal(job->out_fence);

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
		job->core = rga_drvdata->rga_scheduler[0]->core;
	}

	scheduler = rga_job_get_scheduler(job->core);
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
			ktime_us_delta(now, job->hw_running_time));

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
			ktime_us_delta(now, waiter->job->timestamp));

	scheduler = rga_job_schedule(waiter->job);

	if (scheduler == NULL)
		pr_err("failed to get scheduler, %s(%d)\n", __func__, __LINE__);

	kfree(waiter);
}

uint32_t rga_internal_ctx_alloc_to_get_idr_id(void)
{
	struct rga_pending_ctx_manager *ctx_manager;
	struct rga_internal_ctx_t *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL) {
		pr_err("can not kzalloc for rga_pending_ctx_manager\n");
		return -ENOMEM;
	}

	ctx_manager = rga_drvdata->pend_ctx_manager;
	if (ctx_manager == NULL) {
		pr_err("rga_pending_ctx_manager is null!\n");
		kfree(ctx);
		return -EFAULT;
	}

	spin_lock_init(&ctx->lock);

	/*
	 * Get the user-visible handle using idr. Preload and perform
	 * allocation under our spinlock.
	 */

	mutex_lock(&ctx_manager->lock);

	idr_preload(GFP_KERNEL);
	ctx->id = idr_alloc(&ctx_manager->ctx_id_idr, ctx, 1, 0, GFP_KERNEL);
	idr_preload_end();

	ctx_manager->ctx_count++;

	kref_init(&ctx->refcount);
	ctx->pid = current->pid;

	mutex_unlock(&ctx_manager->lock);

	if (ctx->id < 0)
		pr_err("[pid: %d]alloc ctx_id failed", ctx->pid);

	return (uint32_t)ctx->id;
}

int rga_job_config_by_user_ctx(struct rga_user_ctx_t *user_ctx)
{
	struct rga_pending_ctx_manager *ctx_manager;
	struct rga_internal_ctx_t *ctx;
	struct rga_req *cached_cmd;
	int ret = 0;
	bool first_config = false;
	unsigned long flags;

	ctx_manager = rga_drvdata->pend_ctx_manager;

	ctx = rga_internal_ctx_lookup(ctx_manager, user_ctx->id);
	if (IS_ERR_OR_NULL(ctx)) {
		pr_err("can not find internal ctx from id[%d]", user_ctx->id);
		return -EINVAL;
	}

	spin_lock_irqsave(&ctx->lock, flags);

	cached_cmd = ctx->cached_cmd;

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (cached_cmd == NULL) {
		cached_cmd = kmalloc_array(user_ctx->cmd_num, sizeof(struct rga_req), GFP_KERNEL);
		if (cached_cmd == NULL) {
			pr_err("cmd_cached list alloc error!\n");
			return -ENOMEM;
		}

		first_config = true;
	}

	if (unlikely(copy_from_user(cached_cmd, u64_to_user_ptr(user_ctx->cmd_ptr),
				    sizeof(struct rga_req) * user_ctx->cmd_num))) {
		pr_err("rga_user_ctx cmd list copy_from_user failed\n");
		if (first_config)
			kfree(cached_cmd);
		return -EFAULT;
	}

	spin_lock_irqsave(&ctx->lock, flags);

	ctx->sync_mode = user_ctx->sync_mode;
	ctx->cmd_num = user_ctx->cmd_num;
	ctx->cached_cmd = cached_cmd;
	ctx->mpi_config_flags = user_ctx->mpi_config_flags;

	spin_unlock_irqrestore(&ctx->lock, flags);

	return ret;
}

int rga_job_commit_by_user_ctx(struct rga_user_ctx_t *user_ctx)
{
	struct rga_pending_ctx_manager *ctx_manager;
	struct rga_internal_ctx_t *ctx;
	struct rga_req *cached_cmd;
	int i;
	int ret = 0;
	unsigned long flags;

	ctx_manager = rga_drvdata->pend_ctx_manager;

	ctx = rga_internal_ctx_lookup(ctx_manager, user_ctx->id);
	if (IS_ERR_OR_NULL(ctx)) {
		pr_err("can not find internal ctx from id[%d]", user_ctx->id);
		return -EINVAL;
	}

	spin_lock_irqsave(&ctx->lock, flags);

	if (ctx->is_running) {
		pr_err("can not re-config when ctx is running");
		spin_unlock_irqrestore(&ctx->lock, flags);
		return -EFAULT;
	}

	/* Reset */
	ctx->finished_job_count = 0;

	cached_cmd = ctx->cached_cmd;
	if (cached_cmd == NULL) {
		pr_err("can not find cached cmd from id[%d]", user_ctx->id);
		spin_unlock_irqrestore(&ctx->lock, flags);
		return -EINVAL;
	}

	ctx->use_batch_mode = true;
	ctx->is_running = true;

	spin_unlock_irqrestore(&ctx->lock, flags);

	for (i = 0; i < ctx->cmd_num; i++) {
		ret = rga_job_commit(&(cached_cmd[i]), ctx);
		if (ret < 0) {
			pr_err("rga_job_commit failed\n");
			return -EFAULT;
		}
	}

	user_ctx->out_fence_fd = ctx->out_fence_fd;

	return ret;
}

void rga_internel_ctx_kref_release(struct kref *ref)
{
	struct rga_internal_ctx_t *ctx;
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_job *job_pos, *job_q, *job;
	int i;
	bool need_reset = false;
	unsigned long flags;
	ktime_t now = ktime_get();

	ctx = container_of(ref, struct rga_internal_ctx_t, refcount);

	spin_lock_irqsave(&ctx->lock, flags);

	if (!ctx->is_running || ctx->finished_job_count >= ctx->cmd_num) {
		spin_unlock_irqrestore(&ctx->lock, flags);
		goto free_ctx;
	}

	spin_unlock_irqrestore(&ctx->lock, flags);

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->rga_scheduler[i];

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		list_for_each_entry_safe(job_pos, job_q, &scheduler->todo_list, head) {
			if (ctx->id == job_pos->ctx_id) {
				job = job_pos;
				list_del_init(&job_pos->head);

				scheduler->job_count--;
			}
		}

		if (scheduler->running_job) {
			job = scheduler->running_job;

			if (job->ctx_id == ctx->id) {
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

free_ctx:
	rga_internal_ctx_free_remove_idr(ctx);
}

int rga_job_cancel_by_user_ctx(uint32_t ctx_id)
{
	struct rga_pending_ctx_manager *ctx_manager;
	struct rga_internal_ctx_t *ctx;
	int ret = 0;

	ctx_manager = rga_drvdata->pend_ctx_manager;

	ctx = rga_internal_ctx_lookup(ctx_manager, ctx_id);
	if (IS_ERR_OR_NULL(ctx)) {
		pr_err("can not find internal ctx from id[%d]", ctx_id);
		return -EINVAL;
	}

	kref_put(&ctx->refcount, rga_internel_ctx_kref_release);

	return ret;
}

int rga_job_commit(struct rga_req *rga_command_base, struct rga_internal_ctx_t *ctx)
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

	job->use_batch_mode = ctx->use_batch_mode;
	job->ctx_id = ctx->id;

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

	if (ctx->sync_mode == RGA_BLIT_ASYNC) {
		job->flags |= RGA_JOB_ASYNC;

		if (ctx->out_fence) {
			job->out_fence = ctx->out_fence;
		} else {
			ret = rga_out_fence_alloc(job);
			if (ret) {
				rga_job_free(job);
				return ret;
			}

			/* on batch mode, only first job need to alloc fence */
			if (ctx->use_batch_mode)
				ctx->out_fence = job->out_fence;
		}

		rga_command_base->out_fence_fd = rga_out_fence_get_fd(job);
		ctx->out_fence_fd = rga_command_base->out_fence_fd;

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
					ret = -EINVAL;
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
				ret = -EINVAL;
				goto invalid_job;
			}
		}

		return ret;

	/* RGA_BLIT_SYNC: wait until job finish */
	} else if (ctx->sync_mode == RGA_BLIT_SYNC) {
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
		       struct rga_mpi_job_t *mpi_job, struct rga_internal_ctx_t *ctx)
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

	/* Increments the reference count on the dma-buf */
	rga_get_dma_buf(job);

	job->ctx_id = ctx->id;

	if (ctx->sync_mode == RGA_BLIT_ASYNC) {
		//TODO: mpi async mode
		pr_err("rk-debug TODO\n");
	} else if (ctx->sync_mode == RGA_BLIT_SYNC) {
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

int rga_ctx_manager_init(struct rga_pending_ctx_manager **ctx_manager_session)
{
	struct rga_pending_ctx_manager *ctx_manager = NULL;

	*ctx_manager_session = kzalloc(sizeof(struct rga_pending_ctx_manager), GFP_KERNEL);
	if (*ctx_manager_session == NULL) {
		pr_err("can not kzalloc for rga_pending_ctx_manager\n");
		return -ENOMEM;
	}

	ctx_manager = *ctx_manager_session;

	mutex_init(&ctx_manager->lock);

	idr_init_base(&ctx_manager->ctx_id_idr, 1);

	return 0;
}

int rga_ctx_manager_remove(struct rga_pending_ctx_manager **ctx_manager_session)
{
	struct rga_pending_ctx_manager *ctx_manager = *ctx_manager_session;

	mutex_lock(&ctx_manager->lock);

	idr_for_each(&ctx_manager->ctx_id_idr, &rga_internal_ctx_free_remove_idr_cb, ctx_manager);
	idr_destroy(&ctx_manager->ctx_id_idr);

	mutex_unlock(&ctx_manager->lock);

	kfree(*ctx_manager_session);

	*ctx_manager_session = NULL;

	return 0;
}
