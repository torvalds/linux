// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga: " fmt

#include "rga2_reg_info.h"
#include "rga3_reg_info.h"
#include "rga_dma_buf.h"
#include "rga_mm.h"

#include "rga_job.h"
#include "rga_fence.h"
#include "rga_hw_config.h"

#include "rga_iommu.h"
#include "rga_debugger.h"
#include "rga_common.h"

struct rga_drvdata_t *rga_drvdata;

/* set hrtimer */
static struct hrtimer timer;
static ktime_t kt;

static const struct rga_backend_ops rga3_ops = {
	.get_version = rga3_get_version,
	.set_reg = rga3_set_reg,
	.init_reg = rga3_init_reg,
	.soft_reset = rga3_soft_reset
};

static const struct rga_backend_ops rga2_ops = {
	.get_version = rga2_get_version,
	.set_reg = rga2_set_reg,
	.init_reg = rga2_init_reg,
	.soft_reset = rga2_soft_reset
};

static struct rga_session *rga_session_init(void);
static int rga_session_deinit(struct rga_session *session);

static int rga_mpi_set_channel_buffer(struct dma_buf *dma_buf,
				      struct rga_img_info_t *channel_info,
				      struct rga_session *session)
{
	struct rga_external_buffer buffer;

	buffer.memory = (unsigned long)dma_buf;
	buffer.type = RGA_DMA_BUFFER_PTR;
	buffer.memory_parm.width = channel_info->vir_w;
	buffer.memory_parm.height = channel_info->vir_h;
	buffer.memory_parm.format = channel_info->format;

	buffer.handle = rga_mm_import_buffer(&buffer, session);
	if (buffer.handle == 0) {
		pr_err("can not import dma_buf %p\n", dma_buf);
		return -EFAULT;
	}
	channel_info->yrgb_addr = buffer.handle;

	return 0;
}

static void rga_mpi_set_channel_info(uint32_t flags_mask, uint32_t flags,
				     struct rga_video_frame_info *mpi_frame,
				     struct rga_img_info_t *channel_info,
				     struct rga_img_info_t *cache_info)
{
	uint32_t fix_enable_flag, cache_info_flag;

	switch (flags_mask) {
	case RGA_CONTEXT_SRC_MASK:
		fix_enable_flag = RGA_CONTEXT_SRC_FIX_ENABLE;
		cache_info_flag = RGA_CONTEXT_SRC_CACHE_INFO;
		break;
	case RGA_CONTEXT_PAT_MASK:
		fix_enable_flag = RGA_CONTEXT_PAT_FIX_ENABLE;
		cache_info_flag = RGA_CONTEXT_PAT_CACHE_INFO;
		break;
	case RGA_CONTEXT_DST_MASK:
		fix_enable_flag = RGA_CONTEXT_DST_FIX_ENABLE;
		cache_info_flag = RGA_CONTEXT_DST_CACHE_INFO;
		break;
	default:
		return;
	}

	if (flags & fix_enable_flag) {
		channel_info->x_offset = mpi_frame->x_offset;
		channel_info->y_offset = mpi_frame->y_offset;
		channel_info->act_w = mpi_frame->width;
		channel_info->act_h = mpi_frame->height;
		channel_info->vir_w = mpi_frame->vir_w;
		channel_info->vir_h = mpi_frame->vir_h;
		channel_info->rd_mode = mpi_frame->rd_mode;
		channel_info->format = mpi_frame->format;

		if (flags & cache_info_flag) {
			/* Replace the config of src in ctx with the config of mpi src. */
			cache_info->x_offset = mpi_frame->x_offset;
			cache_info->y_offset = mpi_frame->y_offset;
			cache_info->act_w = mpi_frame->width;
			cache_info->act_h = mpi_frame->height;
			cache_info->vir_w = mpi_frame->vir_w;
			cache_info->vir_h = mpi_frame->vir_h;
			cache_info->rd_mode = mpi_frame->rd_mode;
			cache_info->format = mpi_frame->format;

		}
	}
}

int rga_mpi_commit(struct rga_mpi_job_t *mpi_job)
{
	int ret = 0;
	struct rga_pending_request_manager *request_manager;
	struct rga_request *request;
	struct rga_req *cached_cmd;
	struct rga_req mpi_cmd;
	unsigned long flags;

	request_manager = rga_drvdata->pend_request_manager;

	mutex_lock(&request_manager->lock);
	request = rga_request_lookup(request_manager, mpi_job->ctx_id);
	if (IS_ERR_OR_NULL(request)) {
		pr_err("can not find request from id[%d]", mpi_job->ctx_id);
		mutex_unlock(&request_manager->lock);
		return -EINVAL;
	}

	if (request->task_count > 1) {
		/* TODO */
		pr_err("Currently request does not support multiple tasks!");
		mutex_unlock(&request_manager->lock);
		return -EINVAL;
	}

	/*
	 * The mpi commit will use the request repeatedly, so an additional
	 * get() is added here.
	 */
	rga_request_get(request);
	mutex_unlock(&request_manager->lock);

	spin_lock_irqsave(&request->lock, flags);

	/* TODO: batch mode need mpi async mode */
	request->sync_mode = RGA_BLIT_SYNC;

	cached_cmd = request->task_list;
	memcpy(&mpi_cmd, cached_cmd, sizeof(mpi_cmd));

	spin_unlock_irqrestore(&request->lock, flags);

	/* set channel info */
	if ((mpi_job->src != NULL) && (request->flags & RGA_CONTEXT_SRC_MASK))
		rga_mpi_set_channel_info(RGA_CONTEXT_SRC_MASK,
					 request->flags,
					 mpi_job->src,
					 &mpi_cmd.src,
					 &cached_cmd->src);

	if ((mpi_job->pat != NULL) && (request->flags & RGA_CONTEXT_PAT_MASK))
		rga_mpi_set_channel_info(RGA_CONTEXT_PAT_MASK,
					 request->flags,
					 mpi_job->pat,
					 &mpi_cmd.pat,
					 &cached_cmd->pat);

	if ((mpi_job->dst != NULL) && (request->flags & RGA_CONTEXT_DST_MASK))
		rga_mpi_set_channel_info(RGA_CONTEXT_DST_MASK,
					 request->flags,
					 mpi_job->dst,
					 &mpi_cmd.dst,
					 &cached_cmd->dst);

	/* set buffer handle */
	if (mpi_job->dma_buf_src0 != NULL) {
		ret = rga_mpi_set_channel_buffer(mpi_job->dma_buf_src0,
						 &mpi_cmd.src,
						 request->session);
		if (ret < 0) {
			pr_err("src channel set buffer handle failed!\n");
			goto err_put_request;
		}
	}

	if (mpi_job->dma_buf_src1 != NULL) {
		ret = rga_mpi_set_channel_buffer(mpi_job->dma_buf_src1,
						 &mpi_cmd.pat,
						 request->session);
		if (ret < 0) {
			pr_err("src1 channel set buffer handle failed!\n");
			goto err_put_request;
		}
	}

	if (mpi_job->dma_buf_dst != NULL) {
		ret = rga_mpi_set_channel_buffer(mpi_job->dma_buf_dst,
						 &mpi_cmd.dst,
						 request->session);
		if (ret < 0) {
			pr_err("dst channel set buffer handle failed!\n");
			goto err_put_request;
		}
	}

	mpi_cmd.handle_flag = 1;
	mpi_cmd.mmu_info.mmu_en = 0;
	mpi_cmd.mmu_info.mmu_flag = 0;

	if (DEBUGGER_EN(MSG))
		rga_cmd_print_debug_info(&mpi_cmd);

	ret = rga_request_mpi_submit(&mpi_cmd, request);
	if (ret < 0) {
		if (ret == -ERESTARTSYS) {
			if (DEBUGGER_EN(MSG))
				pr_err("%s, commit mpi job failed, by a software interrupt.\n",
					__func__);
		} else {
			pr_err("%s, commit mpi job failed\n", __func__);
		}

		goto err_put_request;
	}

	if ((mpi_job->dma_buf_src0 != NULL) && (mpi_cmd.src.yrgb_addr > 0))
		rga_mm_release_buffer(mpi_cmd.src.yrgb_addr);
	if ((mpi_job->dma_buf_src1 != NULL) && (mpi_cmd.pat.yrgb_addr > 0))
		rga_mm_release_buffer(mpi_cmd.pat.yrgb_addr);
	if ((mpi_job->dma_buf_dst != NULL) && (mpi_cmd.dst.yrgb_addr > 0))
		rga_mm_release_buffer(mpi_cmd.dst.yrgb_addr);

	/* copy dst info to mpi job for next node */
	if (mpi_job->output != NULL) {
		mpi_job->output->x_offset = mpi_cmd.dst.x_offset;
		mpi_job->output->y_offset = mpi_cmd.dst.y_offset;
		mpi_job->output->width = mpi_cmd.dst.act_w;
		mpi_job->output->height = mpi_cmd.dst.act_h;
		mpi_job->output->vir_w = mpi_cmd.dst.vir_w;
		mpi_job->output->vir_h = mpi_cmd.dst.vir_h;
		mpi_job->output->rd_mode = mpi_cmd.dst.rd_mode;
		mpi_job->output->format = mpi_cmd.dst.format;
	}

	return 0;

err_put_request:
	mutex_lock(&request_manager->lock);
	rga_request_put(request);
	mutex_unlock(&request_manager->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(rga_mpi_commit);

int rga_kernel_commit(struct rga_req *cmd)
{
	int ret = 0;
	int request_id;
	struct rga_user_request kernel_request;
	struct rga_request *request = NULL;
	struct rga_session *session = NULL;
	struct rga_pending_request_manager *request_manager = rga_drvdata->pend_request_manager;

	session = rga_session_init();
	if (IS_ERR(session))
		return PTR_ERR(session);

	request_id = rga_request_alloc(0, session);
	if (request_id < 0) {
		pr_err("request alloc error!\n");
		ret = request_id;
		return ret;
	}

	memset(&kernel_request, 0, sizeof(kernel_request));
	kernel_request.id = request_id;
	kernel_request.task_ptr = (uint64_t)(unsigned long)cmd;
	kernel_request.task_num = 1;
	kernel_request.sync_mode = RGA_BLIT_SYNC;

	ret = rga_request_check(&kernel_request);
	if (ret < 0) {
		pr_err("user request check error!\n");
		goto err_free_request_by_id;
	}

	request = rga_request_kernel_config(&kernel_request);
	if (IS_ERR(request)) {
		pr_err("request[%d] config failed!\n", kernel_request.id);
		ret = -EFAULT;
		goto err_free_request_by_id;
	}

	if (DEBUGGER_EN(MSG)) {
		pr_info("kernel blit mode: request id = %d", kernel_request.id);
		rga_cmd_print_debug_info(cmd);
	}

	ret = rga_request_submit(request);
	if (ret < 0) {
		pr_err("request[%d] submit failed!\n", kernel_request.id);
		goto err_put_request;
	}

err_put_request:
	mutex_lock(&request_manager->lock);
	rga_request_put(request);
	mutex_unlock(&request_manager->lock);

	rga_session_deinit(session);

	return ret;

err_free_request_by_id:
	mutex_lock(&request_manager->lock);

	request = rga_request_lookup(request_manager, request_id);
	if (IS_ERR_OR_NULL(request)) {
		pr_err("can not find request from id[%d]", request_id);
		mutex_unlock(&request_manager->lock);
		return -EINVAL;
	}

	rga_request_free(request);

	mutex_unlock(&request_manager->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(rga_kernel_commit);

static enum hrtimer_restart hrtimer_handler(struct hrtimer *timer)
{
	struct rga_drvdata_t *rga = rga_drvdata;
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_job *job = NULL;
	unsigned long flags;
	int i;

	ktime_t now = ktime_get();

	for (i = 0; i < rga->num_of_scheduler; i++) {
		scheduler = rga->scheduler[i];

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		/* if timer action on job running */
		job = scheduler->running_job;
		if (job) {
			scheduler->timer.busy_time += ktime_us_delta(now, job->hw_recoder_time);
			job->hw_recoder_time = now;
		}

		scheduler->timer.busy_time_record = scheduler->timer.busy_time;
		scheduler->timer.busy_time = 0;

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
	}

	hrtimer_forward_now(timer, kt);
	return HRTIMER_RESTART;
}

static void rga_init_timer(void)
{
	kt = ktime_set(0, RGA_TIMER_INTERVAL_NS);
	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	timer.function = hrtimer_handler;

	hrtimer_start(&timer, kt, HRTIMER_MODE_REL);
}

static void rga_cancel_timer(void)
{
	hrtimer_cancel(&timer);
}

#ifndef RGA_DISABLE_PM
int rga_power_enable(struct rga_scheduler_t *scheduler)
{
	int ret = -EINVAL;
	int i;
	unsigned long flags;

	pm_runtime_get_sync(scheduler->dev);
	pm_stay_awake(scheduler->dev);

	for (i = 0; i < scheduler->num_clks; i++) {
		if (!IS_ERR(scheduler->clks[i])) {
			ret = clk_prepare_enable(scheduler->clks[i]);
			if (ret < 0)
				goto err_enable_clk;
		}
	}

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	scheduler->pd_refcount++;
	if (scheduler->status == RGA_SCHEDULER_IDLE)
		scheduler->status = RGA_SCHEDULER_WORKING;

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	return 0;

err_enable_clk:
	for (--i; i >= 0; --i)
		if (!IS_ERR(scheduler->clks[i]))
			clk_disable_unprepare(scheduler->clks[i]);

	pm_relax(scheduler->dev);
	pm_runtime_put_sync_suspend(scheduler->dev);

	return ret;
}

int rga_power_disable(struct rga_scheduler_t *scheduler)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	if (scheduler->status == RGA_SCHEDULER_IDLE ||
	    scheduler->pd_refcount == 0) {
		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
		WARN(true, "%s already idle!\n", dev_driver_string(scheduler->dev));
		return -1;
	}

	scheduler->pd_refcount--;
	if (scheduler->pd_refcount == 0)
		scheduler->status = RGA_SCHEDULER_IDLE;

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	for (i = scheduler->num_clks - 1; i >= 0; i--)
		if (!IS_ERR(scheduler->clks[i]))
			clk_disable_unprepare(scheduler->clks[i]);

	pm_relax(scheduler->dev);
	pm_runtime_put_sync_suspend(scheduler->dev);

	return 0;
}

static void rga_power_enable_all(void)
{
	struct rga_scheduler_t *scheduler = NULL;
	int ret = 0;
	int i;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];
		ret = rga_power_enable(scheduler);
		if (ret < 0)
			pr_err("power enable failed");
	}
}

static void rga_power_disable_all(void)
{
	struct rga_scheduler_t *scheduler = NULL;
	int i;

	for (i = 0; i < rga_drvdata->num_of_scheduler; i++) {
		scheduler = rga_drvdata->scheduler[i];
		rga_power_disable(scheduler);
	}
}

#else
int rga_power_enable(struct rga_scheduler_t *scheduler)
{
	return 0;
}

int rga_power_disable(struct rga_scheduler_t *scheduler)
{
	return 0;
}

static inline void rga_power_enable_all(void) {}
static inline void rga_power_disable_all(void) {}
#endif /* #ifndef RGA_DISABLE_PM */

static int rga_session_manager_init(struct rga_session_manager **session_manager_ptr)
{
	struct rga_session_manager *session_manager = NULL;

	*session_manager_ptr = kzalloc(sizeof(struct rga_session_manager), GFP_KERNEL);
	if (*session_manager_ptr == NULL) {
		pr_err("can not kzalloc for rga_session_manager\n");
		return -ENOMEM;
	}

	session_manager = *session_manager_ptr;

	mutex_init(&session_manager->lock);

	idr_init_base(&session_manager->ctx_id_idr, 1);

	return 0;
}

/*
 * Called at driver close to release the rga session's id references.
 */
static int rga_session_free_remove_idr_cb(int id, void *ptr, void *data)
{
	struct rga_session *session = ptr;

	idr_remove(&rga_drvdata->session_manager->ctx_id_idr, session->id);
	kfree(session);

	return 0;
}

static int rga_session_free_remove_idr(struct rga_session *session)
{
	struct rga_session_manager *session_manager;

	session_manager = rga_drvdata->session_manager;

	mutex_lock(&session_manager->lock);

	session_manager->session_cnt--;
	idr_remove(&session_manager->ctx_id_idr, session->id);

	mutex_unlock(&session_manager->lock);

	return 0;
}

static int rga_session_manager_remove(struct rga_session_manager **session_manager_ptr)
{
	struct rga_session_manager *session_manager = *session_manager_ptr;

	mutex_lock(&session_manager->lock);

	idr_for_each(&session_manager->ctx_id_idr, &rga_session_free_remove_idr_cb, session_manager);
	idr_destroy(&session_manager->ctx_id_idr);

	mutex_unlock(&session_manager->lock);

	kfree(*session_manager_ptr);

	*session_manager_ptr = NULL;

	return 0;
}

static struct rga_session *rga_session_init(void)
{
	int new_id;

	struct rga_session_manager *session_manager = NULL;
	struct rga_session *session = NULL;

	session_manager = rga_drvdata->session_manager;
	if (session_manager == NULL) {
		pr_err("rga_session_manager is null!\n");
		return ERR_PTR(-EFAULT);
	}

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session) {
		pr_err("rga_session alloc failed\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&session_manager->lock);

	idr_preload(GFP_KERNEL);
	new_id = idr_alloc_cyclic(&session_manager->ctx_id_idr, session, 1, 0, GFP_NOWAIT);
	idr_preload_end();
	if (new_id < 0) {
		mutex_unlock(&session_manager->lock);

		pr_err("rga_session alloc id failed!\n");
		kfree(session);
		return ERR_PTR(new_id);
	}

	session->id = new_id;
	session_manager->session_cnt++;

	mutex_unlock(&session_manager->lock);

	session->tgid = current->tgid;
	session->pname = kstrdup_quotable_cmdline(current, GFP_KERNEL);

	return session;
}

static int rga_session_deinit(struct rga_session *session)
{
	pid_t pid;
	int request_id;
	struct rga_pending_request_manager *request_manager;
	struct rga_request *request;

	pid = current->pid;

	request_manager = rga_drvdata->pend_request_manager;

	mutex_lock(&request_manager->lock);

	idr_for_each_entry(&request_manager->request_idr, request, request_id) {

		if (session == request->session) {
			pr_err("[pid:%d] destroy request[%d] when the user exits",
			       pid, request->id);
			rga_request_put(request);
		}
	}

	mutex_unlock(&request_manager->lock);

	rga_job_session_destroy(session);
	rga_mm_session_release_buffer(session);

	rga_session_free_remove_idr(session);

	kfree(session->pname);
	kfree(session);

	return 0;
}

static long rga_ioctl_import_buffer(unsigned long arg, struct rga_session *session)
{
	int i;
	int ret = 0;
	struct rga_buffer_pool buffer_pool;
	struct rga_external_buffer *external_buffer = NULL;

	if (unlikely(copy_from_user(&buffer_pool,
				    (struct rga_buffer_pool *)arg,
				    sizeof(buffer_pool)))) {
		pr_err("rga_buffer_pool copy_from_user failed!\n");
		return -EFAULT;
	}

	if (buffer_pool.size > RGA_BUFFER_POOL_SIZE_MAX) {
		pr_err("Cannot import more than %d buffers at a time!\n",
		       RGA_BUFFER_POOL_SIZE_MAX);
		return -EFBIG;
	}

	if (buffer_pool.buffers_ptr == 0) {
		pr_err("Import buffers is NULL!\n");
		return -EFAULT;
	}

	external_buffer = kmalloc(sizeof(struct rga_external_buffer) * buffer_pool.size,
				  GFP_KERNEL);
	if (external_buffer == NULL) {
		pr_err("external buffer list alloc error!\n");
		return -ENOMEM;
	}

	if (unlikely(copy_from_user(external_buffer,
				    u64_to_user_ptr(buffer_pool.buffers_ptr),
				    sizeof(struct rga_external_buffer) * buffer_pool.size))) {
		pr_err("rga_buffer_pool external_buffer list copy_from_user failed\n");
		ret = -EFAULT;

		goto err_free_external_buffer;
	}

	for (i = 0; i < buffer_pool.size; i++) {
		if (DEBUGGER_EN(MSG)) {
			pr_info("import buffer info:\n");
			rga_dump_external_buffer(&external_buffer[i]);
		}

		ret = rga_mm_import_buffer(&external_buffer[i], session);
		if (ret == 0) {
			pr_err("buffer[%d] mm import buffer failed! memory = 0x%lx, type = 0x%x\n",
			       i, (unsigned long)external_buffer[i].memory,
			       external_buffer[i].type);

			goto err_free_external_buffer;
		}

		external_buffer[i].handle = ret;
	}

	if (unlikely(copy_to_user(u64_to_user_ptr(buffer_pool.buffers_ptr),
				  external_buffer,
				  sizeof(struct rga_external_buffer) * buffer_pool.size))) {
		pr_err("rga_buffer_pool external_buffer list copy_to_user failed\n");
		ret = -EFAULT;

		goto err_free_external_buffer;
	}

err_free_external_buffer:
	kfree(external_buffer);
	return ret;
}

static long rga_ioctl_release_buffer(unsigned long arg)
{
	int i;
	int ret = 0;
	struct rga_buffer_pool buffer_pool;
	struct rga_external_buffer *external_buffer = NULL;

	if (unlikely(copy_from_user(&buffer_pool,
				    (struct rga_buffer_pool *)arg,
				    sizeof(buffer_pool)))) {
		pr_err("rga_buffer_pool  copy_from_user failed!\n");
		return -EFAULT;
	}

	if (buffer_pool.size > RGA_BUFFER_POOL_SIZE_MAX) {
		pr_err("Cannot release more than %d buffers at a time!\n",
		       RGA_BUFFER_POOL_SIZE_MAX);
		return -EFBIG;
	}

	if (buffer_pool.buffers_ptr == 0) {
		pr_err("Release buffers is NULL!\n");
		return -EFAULT;
	}

	external_buffer = kmalloc(sizeof(struct rga_external_buffer) * buffer_pool.size,
				  GFP_KERNEL);
	if (external_buffer == NULL) {
		pr_err("external buffer list alloc error!\n");
		return -ENOMEM;
	}

	if (unlikely(copy_from_user(external_buffer,
				    u64_to_user_ptr(buffer_pool.buffers_ptr),
				    sizeof(struct rga_external_buffer) * buffer_pool.size))) {
		pr_err("rga_buffer_pool external_buffer list copy_from_user failed\n");
		ret = -EFAULT;

		goto err_free_external_buffer;
	}

	for (i = 0; i < buffer_pool.size; i++) {
		if (DEBUGGER_EN(MSG))
			pr_info("release buffer handle[%d]\n", external_buffer[i].handle);

		ret = rga_mm_release_buffer(external_buffer[i].handle);
		if (ret < 0) {
			pr_err("buffer[%d] mm release buffer failed!\n", i);

			goto err_free_external_buffer;
		}
	}

err_free_external_buffer:
	kfree(external_buffer);
	return ret;
}

static long rga_ioctl_request_create(unsigned long arg, struct rga_session *session)
{
	uint32_t id;
	uint32_t flags;

	if (copy_from_user(&flags, (void *)arg, sizeof(uint32_t))) {
		pr_err("%s failed to copy from usrer!\n", __func__);
		return -EFAULT;
	}

	id = rga_request_alloc(flags, session);

	if (copy_to_user((void *)arg, &id, sizeof(uint32_t))) {
		pr_err("%s failed to copy to usrer!\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static long rga_ioctl_request_submit(unsigned long arg, bool run_enbale)
{
	int ret = 0;
	struct rga_pending_request_manager *request_manager = NULL;
	struct rga_user_request user_request;
	struct rga_request *request = NULL;

	request_manager = rga_drvdata->pend_request_manager;

	if (unlikely(copy_from_user(&user_request,
				    (struct rga_user_request *)arg,
				    sizeof(user_request)))) {
		pr_err("%s copy_from_user failed!\n", __func__);
		return -EFAULT;
	}

	ret = rga_request_check(&user_request);
	if (ret < 0) {
		pr_err("user request check error!\n");
		return ret;
	}

	if (DEBUGGER_EN(MSG))
		pr_info("config request id = %d", user_request.id);

	request = rga_request_config(&user_request);
	if (IS_ERR_OR_NULL(request)) {
		pr_err("request[%d] config failed!\n", user_request.id);
		return -EFAULT;
	}

	if (run_enbale) {
		ret = rga_request_submit(request);
		if (ret < 0) {
			pr_err("request[%d] submit failed!\n", user_request.id);
			return -EFAULT;
		}

		if (request->sync_mode == RGA_BLIT_ASYNC) {
			user_request.release_fence_fd = request->release_fence_fd;
			if (copy_to_user((struct rga_req *)arg,
					 &user_request, sizeof(user_request))) {
				pr_err("copy_to_user failed\n");
				return -EFAULT;
			}
		}
	}

	mutex_lock(&request_manager->lock);
	rga_request_put(request);
	mutex_unlock(&request_manager->lock);

	return 0;
}

static long rga_ioctl_request_cancel(unsigned long arg)
{
	uint32_t id;
	struct rga_pending_request_manager *request_manager;
	struct rga_request *request;

	request_manager = rga_drvdata->pend_request_manager;
	if (request_manager == NULL) {
		pr_err("rga_pending_request_manager is null!\n");
		return -EFAULT;
	}

	if (unlikely(copy_from_user(&id, (uint32_t *)arg, sizeof(uint32_t)))) {
		pr_err("request id copy_from_user failed!\n");
		return -EFAULT;
	}

	if (DEBUGGER_EN(MSG))
		pr_info("config cancel request id = %d", id);

	mutex_lock(&request_manager->lock);

	request = rga_request_lookup(request_manager, id);
	if (IS_ERR_OR_NULL(request)) {
		pr_err("can not find request from id[%d]", id);
		mutex_unlock(&request_manager->lock);
		return -EINVAL;
	}

	rga_request_put(request);

	mutex_unlock(&request_manager->lock);

	return 0;
}

static long rga_ioctl_blit(unsigned long arg, uint32_t cmd, struct rga_session *session)
{
	int ret = 0;
	int request_id;
	struct rga_user_request user_request;
	struct rga_req *rga_req;
	struct rga_request *request = NULL;
	struct rga_pending_request_manager *request_manager = rga_drvdata->pend_request_manager;

	request_id = rga_request_alloc(0, session);
	if (request_id < 0) {
		pr_err("request alloc error!\n");
		ret = request_id;
		return ret;
	}

	memset(&user_request, 0, sizeof(user_request));
	user_request.id = request_id;
	user_request.task_ptr = arg;
	user_request.task_num = 1;
	user_request.sync_mode = cmd;

	ret = rga_request_check(&user_request);
	if (ret < 0) {
		pr_err("user request check error!\n");
		goto err_free_request_by_id;
	}

	request = rga_request_config(&user_request);
	if (IS_ERR(request)) {
		pr_err("request[%d] config failed!\n", user_request.id);
		ret = -EFAULT;
		goto err_free_request_by_id;
	}

	rga_req = request->task_list;
	/* In the BLIT_SYNC/BLIT_ASYNC command, in_fence_fd needs to be set. */
	request->acquire_fence_fd = rga_req->in_fence_fd;

	if (DEBUGGER_EN(MSG)) {
		pr_info("Blit mode: request id = %d", user_request.id);
		rga_cmd_print_debug_info(rga_req);
	}

	ret = rga_request_submit(request);
	if (ret < 0) {
		pr_err("request[%d] submit failed!\n", user_request.id);
		goto err_put_request;
	}

	if (request->sync_mode == RGA_BLIT_ASYNC) {
		rga_req->out_fence_fd = request->release_fence_fd;
		if (copy_to_user((struct rga_req *)arg, rga_req, sizeof(struct rga_req))) {
			pr_err("copy_to_user failed\n");
			ret = -EFAULT;
			goto err_put_request;
		}
	}

err_put_request:
	mutex_lock(&request_manager->lock);
	rga_request_put(request);
	mutex_unlock(&request_manager->lock);

	return ret;

err_free_request_by_id:
	mutex_lock(&request_manager->lock);

	request = rga_request_lookup(request_manager, request_id);
	if (IS_ERR_OR_NULL(request)) {
		pr_err("can not find request from id[%d]", request_id);
		mutex_unlock(&request_manager->lock);
		return -EINVAL;
	}

	rga_request_free(request);

	mutex_unlock(&request_manager->lock);

	return ret;
}

static long rga_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
	int ret = 0;
	int i = 0;
	int major_version = 0, minor_version = 0;
	char version[16] = { 0 };
	struct rga_version_t driver_version;
	struct rga_hw_versions_t hw_versions;
	struct rga_drvdata_t *rga = rga_drvdata;
	struct rga_session *session = file->private_data;

	if (!rga) {
		pr_err("rga_drvdata is null, rga is not init\n");
		return -ENODEV;
	}

	if (DEBUGGER_EN(NONUSE))
		return 0;

	switch (cmd) {
	case RGA_BLIT_SYNC:
	case RGA_BLIT_ASYNC:
		ret = rga_ioctl_blit(arg, cmd, session);

		break;
	case RGA_CACHE_FLUSH:
	case RGA_FLUSH:
	case RGA_GET_RESULT:
		break;
	case RGA_GET_VERSION:
		sscanf(rga->scheduler[i]->version.str, "%x.%x.%*x",
			 &major_version, &minor_version);
		snprintf(version, 5, "%x.%02x", major_version, minor_version);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		/* TODO: userspcae to get version */
		if (copy_to_user((void *)arg, version, sizeof(version)))
			ret = -EFAULT;
#else
		if (copy_to_user((void *)arg, RGA3_VERSION,
				 sizeof(RGA3_VERSION)))
			ret = -EFAULT;
#endif
		break;
	case RGA2_GET_VERSION:
		for (i = 0; i < rga->num_of_scheduler; i++) {
			if (rga->scheduler[i]->ops == &rga2_ops) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
				if (copy_to_user((void *)arg, rga->scheduler[i]->version.str,
					sizeof(rga->scheduler[i]->version.str)))
					ret = -EFAULT;
#else
				if (copy_to_user((void *)arg, RGA3_VERSION,
						sizeof(RGA3_VERSION)))
					ret = -EFAULT;
#endif
				else
					ret = true;

				break;
			}
		}

		/* This will indicate that the RGA2 version number cannot be obtained. */
		if (ret != true)
			ret = -EFAULT;

		break;

	case RGA_IOC_GET_HW_VERSION:
		/* RGA hardware version */
		hw_versions.size = rga->num_of_scheduler > RGA_HW_SIZE ?
			RGA_HW_SIZE : rga->num_of_scheduler;

		for (i = 0; i < hw_versions.size; i++) {
			memcpy(&hw_versions.version[i], &rga->scheduler[i]->version,
				sizeof(rga->scheduler[i]->version));
		}

		if (copy_to_user((void *)arg, &hw_versions, sizeof(hw_versions)))
			ret = -EFAULT;
		else
			ret = true;

		break;

	case RGA_IOC_GET_DRVIER_VERSION:
		/* Driver version */
		driver_version.major = DRIVER_MAJOR_VERISON;
		driver_version.minor = DRIVER_MINOR_VERSION;
		driver_version.revision = DRIVER_REVISION_VERSION;
		strncpy((char *)driver_version.str, DRIVER_VERSION, sizeof(driver_version.str));

		if (copy_to_user((void *)arg, &driver_version, sizeof(driver_version)))
			ret = -EFAULT;
		else
			ret = true;

		break;

	case RGA_IOC_IMPORT_BUFFER:
		rga_power_enable_all();

		ret = rga_ioctl_import_buffer(arg, session);

		rga_power_disable_all();

		break;

	case RGA_IOC_RELEASE_BUFFER:
		rga_power_enable_all();

		ret = rga_ioctl_release_buffer(arg);

		rga_power_disable_all();

		break;

	case RGA_IOC_REQUEST_CREATE:
		ret = rga_ioctl_request_create(arg, session);

		break;

	case RGA_IOC_REQUEST_SUBMIT:
		ret = rga_ioctl_request_submit(arg, true);

		break;

	case RGA_IOC_REQUEST_CONFIG:
		ret = rga_ioctl_request_submit(arg, false);

		break;

	case RGA_IOC_REQUEST_CANCEL:
		ret = rga_ioctl_request_cancel(arg);

		break;

	case RGA_IMPORT_DMA:
	case RGA_RELEASE_DMA:
	default:
		pr_err("unknown ioctl cmd!\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef CONFIG_ROCKCHIP_RGA_DEBUGGER
static int rga_debugger_init(struct rga_debugger **debugger_p)
{
	struct rga_debugger *debugger;

	*debugger_p = kzalloc(sizeof(struct rga_debugger), GFP_KERNEL);
	if (*debugger_p == NULL) {
		pr_err("can not alloc for rga debugger\n");
		return -ENOMEM;
	}

	debugger = *debugger_p;

#ifdef CONFIG_ROCKCHIP_RGA_DEBUG_FS
	mutex_init(&debugger->debugfs_lock);
	INIT_LIST_HEAD(&debugger->debugfs_entry_list);
#endif

#ifdef CONFIG_ROCKCHIP_RGA_PROC_FS
	mutex_init(&debugger->procfs_lock);
	INIT_LIST_HEAD(&debugger->procfs_entry_list);
#endif

	rga_debugfs_init();
	rga_procfs_init();

	return 0;
}

static int rga_debugger_remove(struct rga_debugger **debugger_p)
{
	rga_debugfs_remove();
	rga_procfs_remove();

	kfree(*debugger_p);
	*debugger_p = NULL;

	return 0;
}
#endif

static int rga_open(struct inode *inode, struct file *file)
{
	struct rga_session *session = NULL;

	session = rga_session_init();
	if (IS_ERR(session))
		return PTR_ERR(session);

	file->private_data = (void *)session;

	return nonseekable_open(inode, file);
}

static int rga_release(struct inode *inode, struct file *file)
{
	struct rga_session *session = file->private_data;

	rga_session_deinit(session);

	return 0;
}

static irqreturn_t rga3_irq_handler(int irq, void *data)
{
	struct rga_scheduler_t *scheduler = data;

	if (DEBUGGER_EN(INT_FLAG))
		pr_info("irqthread INT[%x],STATS0[%x], STATS1[%x]\n",
			rga_read(RGA3_INT_RAW, scheduler),
			rga_read(RGA3_STATUS0, scheduler),
			rga_read(RGA3_STATUS1, scheduler));

	/* TODO: if error interrupt then soft reset hardware */
	//scheduler->ops->soft_reset(job->core);

	/*clear INT */
	rga_write(1, RGA3_INT_CLR, scheduler);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rga3_irq_thread(int irq, void *data)
{
	struct rga_scheduler_t *scheduler = data;
	struct rga_job *job;

	job = scheduler->running_job;

	if (!job) {
		pr_err("running job is invaild on irq thread\n");
		return IRQ_HANDLED;
	}

	if (DEBUGGER_EN(INT_FLAG))
		pr_info("irq INT[%x], STATS0[%x], STATS1[%x]\n",
			rga_read(RGA3_INT_RAW, scheduler),
			rga_read(RGA3_STATUS0, scheduler),
			rga_read(RGA3_STATUS1, scheduler));

	rga_job_done(scheduler, 0);

	return IRQ_HANDLED;
}

static irqreturn_t rga2_irq_handler(int irq, void *data)
{
	struct rga_scheduler_t *scheduler = data;

	if (DEBUGGER_EN(INT_FLAG))
		pr_info("irqthread INT[%x],STATS0[%x]\n",
			rga_read(RGA2_INT, scheduler), rga_read(RGA2_STATUS,
								 scheduler));

	/*if error interrupt then soft reset hardware */
	//warning
	if (rga_read(RGA2_INT, scheduler) & 0x01) {
		pr_err("err irq! INT[%x],STATS0[%x]\n",
			 rga_read(RGA2_INT, scheduler),
			 rga_read(RGA2_STATUS, scheduler));
		scheduler->ops->soft_reset(scheduler);
	}

	/*clear INT */
	rga_write(rga_read(RGA2_INT, scheduler) |
		  (0x1 << 4) | (0x1 << 5) | (0x1 << 6) | (0x1 << 7) |
		  (0x1 << 15) | (0x1 << 16), RGA2_INT, scheduler);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rga2_irq_thread(int irq, void *data)
{
	struct rga_scheduler_t *scheduler = data;
	struct rga_job *job;

	job = scheduler->running_job;

	if (!job)
		return IRQ_HANDLED;

	if (DEBUGGER_EN(INT_FLAG))
		pr_info("irq INT[%x], STATS0[%x]\n",
			rga_read(RGA2_INT, scheduler), rga_read(RGA2_STATUS,
								 scheduler));

	job->rga_command_base.osd_info.cur_flags0 = rga_read(RGA2_OSD_CUR_FLAGS0_OFFSET,
							     scheduler);
	job->rga_command_base.osd_info.cur_flags1 = rga_read(RGA2_OSD_CUR_FLAGS1_OFFSET,
							     scheduler);

	rga_job_done(scheduler, 0);

	return IRQ_HANDLED;
}

const struct file_operations rga_fops = {
	.owner = THIS_MODULE,
	.open = rga_open,
	.release = rga_release,
	.unlocked_ioctl = rga_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = rga_ioctl,
#endif
};

static struct miscdevice rga_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rga",
	.fops = &rga_fops,
};

static const char *const old_rga2_clks[] = {
	"aclk_rga",
	"hclk_rga",
	"clk_rga",
};

static const char *const rk3588_rga2_clks[] = {
	"aclk_rga2",
	"hclk_rga2",
	"clk_rga2",
};

static const char *const rga3_core_0_clks[] = {
	"aclk_rga3_0",
	"hclk_rga3_0",
	"clk_rga3_0",
};

static const char *const rga3_core_1_clks[] = {
	"aclk_rga3_1",
	"hclk_rga3_1",
	"clk_rga3_1",
};

static const struct rga_irqs_data_t single_rga2_irqs[] = {
	{"rga2_irq", rga2_irq_handler, rga2_irq_thread}
};

static const struct rga_irqs_data_t rga3_core0_irqs[] = {
	{"rga3_core0_irq", rga3_irq_handler, rga3_irq_thread}
};

static const struct rga_irqs_data_t rga3_core1_irqs[] = {
	{"rga3_core1_irq", rga3_irq_handler, rga3_irq_thread}
};

static const struct rga_match_data_t old_rga2_match_data = {
	.clks = old_rga2_clks,
	.num_clks = ARRAY_SIZE(old_rga2_clks),
	.irqs = single_rga2_irqs,
	.num_irqs = ARRAY_SIZE(single_rga2_irqs)
};

static const struct rga_match_data_t rk3588_rga2_match_data = {
	.clks = rk3588_rga2_clks,
	.num_clks = ARRAY_SIZE(rk3588_rga2_clks),
	.irqs = single_rga2_irqs,
	.num_irqs = ARRAY_SIZE(single_rga2_irqs)
};

static const struct rga_match_data_t rga3_core0_match_data = {
	.clks = rga3_core_0_clks,
	.num_clks = ARRAY_SIZE(rga3_core_0_clks),
	.irqs = rga3_core0_irqs,
	.num_irqs = ARRAY_SIZE(rga3_core0_irqs)
};

static const struct rga_match_data_t rga3_core1_match_data = {
	.clks = rga3_core_1_clks,
	.num_clks = ARRAY_SIZE(rga3_core_1_clks),
	.irqs = rga3_core1_irqs,
	.num_irqs = ARRAY_SIZE(rga3_core1_irqs)
};

static const struct of_device_id rga3_core0_dt_ids[] = {
	{
	 .compatible = "rockchip,rga3_core0",
	 .data = &rga3_core0_match_data,
	},
	{},
};

static const struct of_device_id rga3_core1_dt_ids[] = {
	{
	 .compatible = "rockchip,rga3_core1",
	 .data = &rga3_core1_match_data,
	},
	{},
};

static const struct of_device_id rga2_dt_ids[] = {
	{
	 .compatible = "rockchip,rga2_core0",
	 .data = &rk3588_rga2_match_data,
	},
	{
	 .compatible = "rockchip,rga2",
	 .data = &old_rga2_match_data,
	},
	{},
};

static void init_scheduler(struct rga_scheduler_t *scheduler,
			 const char *name)
{
	spin_lock_init(&scheduler->irq_lock);
	INIT_LIST_HEAD(&scheduler->todo_list);
	init_waitqueue_head(&scheduler->job_done_wq);

	if (!strcmp(name, "rga3_core0")) {
		scheduler->ops = &rga3_ops;
		/* TODO: get by hw version */
		scheduler->core = RGA3_SCHEDULER_CORE0;
	} else if (!strcmp(name, "rga3_core1")) {
		scheduler->ops = &rga3_ops;
		scheduler->core = RGA3_SCHEDULER_CORE1;
	} else if (!strcmp(name, "rga2")) {
		scheduler->ops = &rga2_ops;
		scheduler->core = RGA2_SCHEDULER_CORE0;
	}
}

static int rga_drv_probe(struct platform_device *pdev)
{
#ifndef RGA_DISABLE_PM
	int i;
#endif
	int ret = 0;
	int irq;
	struct resource *res;
	const struct rga_match_data_t *match_data;
	const struct of_device_id *match;
	struct rga_scheduler_t *scheduler;
	struct device *dev = &pdev->dev;
	struct rga_drvdata_t *data = rga_drvdata;

	if (!dev->of_node)
		return -EINVAL;

	if (!strcmp(dev_driver_string(dev), "rga3_core0"))
		match = of_match_device(rga3_core0_dt_ids, dev);
	else if (!strcmp(dev_driver_string(dev), "rga3_core1"))
		match = of_match_device(rga3_core1_dt_ids, dev);
	else if (!strcmp(dev_driver_string(dev), "rga2"))
		match = of_match_device(rga2_dt_ids, dev);
	else
		match = NULL;

	if (!match) {
		dev_err(dev, "%s missing DT entry!\n", dev_driver_string(dev));
		return -EINVAL;
	}

	scheduler = devm_kzalloc(dev, sizeof(struct rga_scheduler_t), GFP_KERNEL);
	if (scheduler == NULL) {
		pr_err("failed to allocate scheduler. dev name = %s\n", dev_driver_string(dev));
		return -ENOMEM;
	}

	init_scheduler(scheduler, dev_driver_string(dev));

	scheduler->dev = dev;

	/* map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("get memory resource failed.\n");
		return -ENXIO;
	}

	scheduler->rga_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!scheduler->rga_base) {
		pr_err("ioremap failed\n");
		ret = -ENOENT;
		return ret;
	}

	/* get the IRQ */
	match_data = match->data;

	/* there are irq names in dts */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq %s in dts\n", match_data->irqs[0].name);
		return irq;
	}

	scheduler->irq = irq;

	pr_info("%s, irq = %d, match scheduler\n", match_data->irqs[0].name, irq);

	ret = devm_request_threaded_irq(dev, irq,
					match_data->irqs[0].irq_hdl,
					match_data->irqs[0].irq_thread,
					IRQF_SHARED,
					dev_driver_string(dev), scheduler);
	if (ret < 0) {
		pr_err("request irq name: %s failed: %d\n", match_data->irqs[0].name, ret);
		return ret;
	}


#ifndef RGA_DISABLE_PM
	/* clk init */
	for (i = 0; i < match_data->num_clks; i++) {
		struct clk *clk = devm_clk_get(dev, match_data->clks[i]);

		if (IS_ERR(clk))
			pr_err("failed to get %s\n", match_data->clks[i]);

		scheduler->clks[i] = clk;
	}
	scheduler->num_clks = match_data->num_clks;

	/* PM init */
	device_init_wakeup(dev, true);
	pm_runtime_enable(scheduler->dev);

	ret = pm_runtime_get_sync(scheduler->dev);
	if (ret < 0) {
		pr_err("failed to get pm runtime, ret = %d\n", ret);
		goto pm_disable;
	}

	for (i = 0; i < scheduler->num_clks; i++) {
		if (!IS_ERR(scheduler->clks[i])) {
			ret = clk_prepare_enable(scheduler->clks[i]);
			if (ret < 0) {
				pr_err("failed to enable clk\n");
				goto pm_disable;
			}
		}
	}
#endif /* #ifndef RGA_DISABLE_PM */

	scheduler->ops->get_version(scheduler);
	pr_info("%s hardware loaded successfully, hw_version:%s.\n",
		dev_driver_string(dev), scheduler->version.str);

	/* TODO: get by hw version, Currently only supports judgment 1106. */
	if (scheduler->core == RGA3_SCHEDULER_CORE0 ||
	    scheduler->core == RGA3_SCHEDULER_CORE1) {
		scheduler->data = &rga3_data;
	} else if (scheduler->core == RGA2_SCHEDULER_CORE0) {
		if (!strcmp(scheduler->version.str, "3.3.87975"))
			scheduler->data = &rga2e_1106_data;
		else if (!strcmp(scheduler->version.str, "3.6.92812") ||
			 !strcmp(scheduler->version.str, "3.7.93215"))
			scheduler->data = &rga2e_iommu_data;
		else
			scheduler->data = &rga2e_data;
	}

	data->scheduler[data->num_of_scheduler] = scheduler;

	data->num_of_scheduler++;

#ifndef RGA_DISABLE_PM
	for (i = scheduler->num_clks - 1; i >= 0; i--)
		if (!IS_ERR(scheduler->clks[i]))
			clk_disable_unprepare(scheduler->clks[i]);

	pm_runtime_put_sync(dev);
#endif /* #ifndef RGA_DISABLE_PM */

	if (scheduler->data->mmu == RGA_IOMMU) {
		scheduler->iommu_info = rga_iommu_probe(dev);
		if (IS_ERR(scheduler->iommu_info)) {
			dev_err(dev, "failed to attach iommu\n");
			scheduler->iommu_info = NULL;
		}
	}

	platform_set_drvdata(pdev, scheduler);

	pr_info("%s probe successfully\n", dev_driver_string(dev));

	return 0;

#ifndef RGA_DISABLE_PM
pm_disable:
	device_init_wakeup(dev, false);
	pm_runtime_disable(dev);
#endif /* #ifndef RGA_DISABLE_PM */

	return ret;
}

static int rga_drv_remove(struct platform_device *pdev)
{
#ifndef RGA_DISABLE_PM
	device_init_wakeup(&pdev->dev, false);
	pm_runtime_disable(&pdev->dev);
#endif /* #ifndef RGA_DISABLE_PM */

	return 0;
}

static struct platform_driver rga3_core0_driver = {
	.probe = rga_drv_probe,
	.remove = rga_drv_remove,
	.driver = {
		 .name = "rga3_core0",
		 .of_match_table = of_match_ptr(rga3_core0_dt_ids),
		 },
};

static struct platform_driver rga3_core1_driver = {
	.probe = rga_drv_probe,
	.remove = rga_drv_remove,
	.driver = {
		 .name = "rga3_core1",
		 .of_match_table = of_match_ptr(rga3_core1_dt_ids),
		 },
};

static struct platform_driver rga2_driver = {
	.probe = rga_drv_probe,
	.remove = rga_drv_remove,
	.driver = {
		 .name = "rga2",
		 .of_match_table = of_match_ptr(rga2_dt_ids),
		 },
};

static int __init rga_init(void)
{
	int ret;

	rga_drvdata = kzalloc(sizeof(struct rga_drvdata_t), GFP_KERNEL);
	if (rga_drvdata == NULL) {
		pr_err("failed to allocate driver data.\n");
		return -ENOMEM;
	}

	mutex_init(&rga_drvdata->lock);

	ret = platform_driver_register(&rga3_core0_driver);
	if (ret != 0) {
		pr_err("Platform device rga3_core0_driver register failed (%d).\n", ret);
		goto err_free_drvdata;
	}

	ret = platform_driver_register(&rga3_core1_driver);
	if (ret != 0) {
		pr_err("Platform device rga3_core1_driver register failed (%d).\n", ret);
		goto err_unregister_rga3_core0;
	}

	ret = platform_driver_register(&rga2_driver);
	if (ret != 0) {
		pr_err("Platform device rga2_driver register failed (%d).\n", ret);
		goto err_unregister_rga3_core1;
	}

	ret = rga_iommu_bind();
	if (ret < 0) {
		pr_err("rga iommu bind failed!\n");
		goto err_unregister_rga2;
	}

	ret = misc_register(&rga_dev);
	if (ret) {
		pr_err("cannot register miscdev (%d)\n", ret);
		goto err_unbind_iommu;
	}

	rga_init_timer();

	rga_mm_init(&rga_drvdata->mm);

	rga_request_manager_init(&rga_drvdata->pend_request_manager);

	rga_session_manager_init(&rga_drvdata->session_manager);

#ifdef CONFIG_ROCKCHIP_RGA_ASYNC
	rga_fence_context_init(&rga_drvdata->fence_ctx);
#endif

#ifdef CONFIG_ROCKCHIP_RGA_DEBUGGER
	rga_debugger_init(&rga_drvdata->debugger);
#endif

	pr_info("Module initialized. v%s\n", DRIVER_VERSION);

	return 0;

err_unbind_iommu:
	rga_iommu_unbind();

err_unregister_rga2:
	platform_driver_unregister(&rga2_driver);

err_unregister_rga3_core1:
	platform_driver_unregister(&rga3_core1_driver);

err_unregister_rga3_core0:
	platform_driver_unregister(&rga3_core0_driver);

err_free_drvdata:
	kfree(rga_drvdata);

	return ret;
}

static void __exit rga_exit(void)
{
#ifdef CONFIG_ROCKCHIP_RGA_DEBUGGER
	rga_debugger_remove(&rga_drvdata->debugger);
#endif

#ifdef CONFIG_ROCKCHIP_RGA_ASYNC
	rga_fence_context_remove(&rga_drvdata->fence_ctx);
#endif

	rga_mm_remove(&rga_drvdata->mm);

	rga_request_manager_remove(&rga_drvdata->pend_request_manager);

	rga_session_manager_remove(&rga_drvdata->session_manager);

	rga_cancel_timer();

	rga_iommu_unbind();

	platform_driver_unregister(&rga3_core0_driver);
	platform_driver_unregister(&rga3_core1_driver);
	platform_driver_unregister(&rga2_driver);

	misc_deregister(&rga_dev);

	kfree(rga_drvdata);

	pr_info("Module exited. v%s\n", DRIVER_VERSION);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_init(rga_init);
#else
late_initcall(rga_init);
#endif
#else
fs_initcall(rga_init);
#endif
module_exit(rga_exit);

/* Module information */
MODULE_AUTHOR("putin.li@rock-chips.com");
MODULE_DESCRIPTION("Driver for rga device");
MODULE_LICENSE("GPL");
#ifdef MODULE_IMPORT_NS
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
