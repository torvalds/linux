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

#include "rga2_mmu_info.h"
#include "rga_debugger.h"

struct rga2_mmu_info_t rga2_mmu_info;

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

int rga_mpi_commit(struct rga_mpi_job_t *mpi_job)
{
	int ret = 0;
	struct rga_pending_ctx_manager *ctx_manager;
	struct rga_internal_ctx_t *ctx;
	struct rga_req *cached_cmd;
	unsigned long flags;
	int i;

	ctx_manager = rga_drvdata->pend_ctx_manager;

	ctx = rga_internal_ctx_lookup(ctx_manager, mpi_job->ctx_id);
	if (IS_ERR_OR_NULL(ctx)) {
		pr_err("can not find internal ctx from id[%d]", mpi_job->ctx_id);
		return -EINVAL;
	}

	spin_lock_irqsave(&ctx->lock, flags);

	ctx->sync_mode = RGA_BLIT_SYNC;
	/* TODO: batch mode need mpi async mode */
	ctx->use_batch_mode = false;

	cached_cmd = ctx->cached_cmd;

	spin_unlock_irqrestore(&ctx->lock, flags);

	/* change src cmd config by mpi frame info */
	if (!ctx->mpi_config_flags) {
		cached_cmd->src.x_offset = mpi_job->src.x_offset;
		cached_cmd->src.y_offset = mpi_job->src.y_offset;
		cached_cmd->src.act_w = mpi_job->src.width;
		cached_cmd->src.act_h = mpi_job->src.height;
		cached_cmd->src.vir_w = mpi_job->src.vir_w;
		cached_cmd->src.vir_h = mpi_job->src.vir_h;
		cached_cmd->src.rd_mode = mpi_job->src.rd_mode;
		cached_cmd->src.format = mpi_job->src.format;
	}

	/* copy dst info to mpi job */
	mpi_job->dst.x_offset = cached_cmd->dst.x_offset;
	mpi_job->dst.y_offset = cached_cmd->dst.y_offset;
	mpi_job->dst.width = cached_cmd->dst.act_w;
	mpi_job->dst.height = cached_cmd->dst.act_h;
	mpi_job->dst.vir_w = cached_cmd->dst.vir_w;
	mpi_job->dst.vir_h = cached_cmd->dst.vir_h;
	mpi_job->dst.rd_mode = cached_cmd->dst.rd_mode;
	mpi_job->dst.format = cached_cmd->dst.format;

	for (i = 0; i < ctx->cmd_num; i++) {
		if (DEBUGGER_EN(MSG))
			rga_cmd_print_debug_info(&(cached_cmd[i]));

		ret = rga_job_mpi_commit(&(cached_cmd[i]), mpi_job, ctx);
		if (ret < 0) {
			if (ret == -ERESTARTSYS) {
				if (DEBUGGER_EN(MSG))
					pr_err("%s, commit mpi job failed, by a software interrupt.\n",
						__func__);
			} else {
				pr_err("%s, commit mpi job failed\n", __func__);
			}

			return ret;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(rga_mpi_commit);

int rga_kernel_commit(struct rga_req *cmd)
{
	int ret = 0;
	struct rga_internal_ctx_t ctx;

	if (DEBUGGER_EN(MSG))
		rga_cmd_print_debug_info(cmd);

	ctx.sync_mode = RGA_BLIT_SYNC;
	ctx.use_batch_mode = false;

	ret = rga_job_commit(cmd, &ctx);
	if (ret < 0) {
		if (ret == -ERESTARTSYS) {
			if (DEBUGGER_EN(MSG))
				pr_err("%s, commit kernel job failed, by a software interrupt.\n",
				       __func__);
		} else {
			pr_err("%s, commit kernel job failed\n", __func__);
		}

		return ret;
	}

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
		scheduler = rga->rga_scheduler[i];

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
	kt = ktime_set(0, RGA_LOAD_INTERVAL);
	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_start(&timer, kt, HRTIMER_MODE_REL);
	timer.function = hrtimer_handler;
}

static void rga_cancel_timer(void)
{
	hrtimer_cancel(&timer);
}

#ifndef CONFIG_ROCKCHIP_FPGA
int rga_power_enable(struct rga_scheduler_t *rga_scheduler)
{
	int ret = -EINVAL;
	int i;

	pm_runtime_get_sync(rga_scheduler->dev);
	pm_stay_awake(rga_scheduler->dev);

	for (i = 0; i < rga_scheduler->num_clks; i++) {
		if (!IS_ERR(rga_scheduler->clks[i])) {
			ret = clk_prepare_enable(rga_scheduler->clks[i]);
			if (ret < 0)
				goto err_enable_clk;
		}
	}

	return 0;

err_enable_clk:
	for (--i; i >= 0; --i)
		if (!IS_ERR(rga_scheduler->clks[i]))
			clk_disable_unprepare(rga_scheduler->clks[i]);

	pm_relax(rga_scheduler->dev);
	pm_runtime_put_sync_suspend(rga_scheduler->dev);

	rga_scheduler->pd_refcount++;

	return ret;
}

int rga_power_disable(struct rga_scheduler_t *rga_scheduler)
{
	int i;

	for (i = rga_scheduler->num_clks - 1; i >= 0; i--)
		if (!IS_ERR(rga_scheduler->clks[i]))
			clk_disable_unprepare(rga_scheduler->clks[i]);

	pm_relax(rga_scheduler->dev);
	pm_runtime_put_sync_suspend(rga_scheduler->dev);

	rga_scheduler->pd_refcount--;

	return 0;
}

#endif //CONFIG_ROCKCHIP_FPGA

static long rga_ioctl_import_buffer(unsigned long arg)
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
		ret = rga_mm_import_buffer(&external_buffer[i]);
		if (ret < 0) {
			pr_err("buffer[%d] mm import buffer failed!\n", i);

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

static long rga_ioctl_cmd_start(unsigned long arg)
{
	uint32_t rga_user_ctx_id;
	int ret = 0;

	rga_user_ctx_id = rga_internal_ctx_alloc_to_get_idr_id();

	if (copy_to_user((void *)arg, &rga_user_ctx_id, sizeof(uint32_t)))
		ret = -EFAULT;

	return ret;
}

static long rga_ioctl_cmd_config(unsigned long arg)
{
	struct rga_user_ctx_t rga_user_ctx;
	int ret = 0;

	if (unlikely(copy_from_user(&rga_user_ctx, (struct rga_user_ctx_t *)arg,
			sizeof(rga_user_ctx)))) {
		pr_err("rga_user_ctx copy_from_user failed!\n");
		return -EFAULT;
	}

	if (rga_user_ctx.cmd_num > RGA_CMD_NUM_MAX) {
		pr_err("Cannot import more than %d buffers at a time!\n",
			RGA_CMD_NUM_MAX);
		return -EFBIG;
	}

	if (rga_user_ctx.cmd_ptr == 0) {
		pr_err("Cmd is NULL");
		return -EINVAL;
	}

	if (rga_user_ctx.id <= 0) {
		pr_err("ctx id[%d] is invalid", rga_user_ctx.id);
		return -EINVAL;
	}

	if (DEBUGGER_EN(MSG))
		pr_err("config cmd id = %d", rga_user_ctx.id);

	/* find internal_ctx to set cmd by user ctx (internal ctx id) */
	ret = rga_job_config_by_user_ctx(&rga_user_ctx);
	if (ret < 0) {
		pr_err("config ctx id[%d] failed!\n", rga_user_ctx.id);
		return -EFAULT;
	}

	return ret;
}

static long rga_ioctl_cmd_end(unsigned long arg)
{
	struct rga_user_ctx_t rga_user_ctx;
	int ret = 0;

	if (unlikely(copy_from_user(&rga_user_ctx, (struct rga_user_ctx_t *)arg,
			sizeof(rga_user_ctx)))) {
		pr_err("rga_user_ctx copy_from_user failed!\n");
		return -EFAULT;
	}

	if (DEBUGGER_EN(MSG))
		pr_err("config end id = %d", rga_user_ctx.id);

	/* find internal_ctx to set cmd by user ctx (internal ctx id) */
	ret = rga_job_commit_by_user_ctx(&rga_user_ctx);
	if (ret < 0) {
		pr_err("commit ctx id[%d] failed!\n", rga_user_ctx.id);
		return -EFAULT;
	}

	if (copy_to_user((struct rga_user_ctx_t *)arg,
			&rga_user_ctx, sizeof(struct rga_user_ctx_t))) {
		pr_err("copy_to_user failed\n");
		return -EFAULT;
	}

	return ret;
}

static long rga_ioctl_cmd_cancel(unsigned long arg)
{
	uint32_t rga_user_ctx_id;
	int ret = 0;

	if (unlikely(copy_from_user(&rga_user_ctx_id, (uint32_t *)arg,
			sizeof(uint32_t)))) {
		pr_err("rga_user_ctx_id copy_from_user failed!\n");
		return -EFAULT;
	}

	if (DEBUGGER_EN(MSG))
		pr_err("config cancel id = %d", rga_user_ctx_id);

	/* find internal_ctx to set cmd by user ctx (internal ctx id) */
	ret = rga_job_cancel_by_user_ctx(rga_user_ctx_id);
	if (ret < 0) {
		pr_err("cancel ctx id[%d] failed!\n", rga_user_ctx_id);
		return -EFAULT;
	}

	return ret;
}

static long rga_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
	struct rga_drvdata_t *rga = rga_drvdata;
	struct rga_req req_rga;
	int ret = 0;
	int i = 0;
	int major_version = 0, minor_version = 0;
	char version[16] = { 0 };
	struct rga_version_t driver_version;
	struct rga_hw_versions_t hw_versions;
	struct rga_internal_ctx_t ctx;

	if (!rga) {
		pr_err("rga_drvdata is null, rga is not init\n");
		return -ENODEV;
	}

	if (DEBUGGER_EN(NONUSE))
		return 0;

	switch (cmd) {
	case RGA_BLIT_SYNC:
	case RGA_BLIT_ASYNC:
		if (unlikely(copy_from_user(&req_rga,
			(struct rga_req *)arg, sizeof(struct rga_req)))) {
			pr_err("copy_from_user failed\n");
			ret = -EFAULT;
			break;
		}

		if (DEBUGGER_EN(MSG))
			rga_cmd_print_debug_info(&req_rga);

		ctx.sync_mode = cmd;
		ctx.use_batch_mode = false;

		ret = rga_job_commit(&req_rga, &ctx);
		if (ret < 0) {
			if (ret == -ERESTARTSYS) {
				if (DEBUGGER_EN(MSG))
					pr_err("rga_job_commit failed, by a software interrupt.\n");
			} else {
				pr_err("rga_job_commit failed\n");
			}

			break;
		}

		if (copy_to_user((struct rga_req *)arg,
				&req_rga, sizeof(struct rga_req))) {
			pr_err("copy_to_user failed\n");
			ret = -EFAULT;
			break;
		}

		break;
	case RGA_CACHE_FLUSH:
	case RGA_FLUSH:
	case RGA_GET_RESULT:
		break;
	case RGA_GET_VERSION:
		sscanf(rga->rga_scheduler[i]->version.str, "%x.%x.%*x",
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
			if (rga->rga_scheduler[i]->ops == &rga2_ops) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
				if (copy_to_user((void *)arg, rga->rga_scheduler[i]->version.str,
					sizeof(rga->rga_scheduler[i]->version.str)))
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
			memcpy(&hw_versions.version[i], &rga->rga_scheduler[i]->version,
				sizeof(rga->rga_scheduler[i]->version));
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
		ret = rga_ioctl_import_buffer(arg);

		break;

	case RGA_IOC_RELEASE_BUFFER:
		ret = rga_ioctl_release_buffer(arg);

		break;

	case RGA_START_CONFIG:
		ret = rga_ioctl_cmd_start(arg);

		break;

	case RGA_END_CONFIG:
		ret = rga_ioctl_cmd_end(arg);

		break;

	case RGA_CMD_CONFIG:
		ret = rga_ioctl_cmd_config(arg);

		break;

	case RGA_CANCEL_CONFIG:
		ret = rga_ioctl_cmd_cancel(arg);

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
	return nonseekable_open(inode, file);
}

static int rga_release(struct inode *inode, struct file *file)
{
	pid_t pid;
	int ctx_id;
	struct rga_pending_ctx_manager *ctx_manager;
	struct rga_internal_ctx_t *ctx;

	pid = current->pid;

	ctx_manager = rga_drvdata->pend_ctx_manager;

	mutex_lock(&ctx_manager->lock);

	idr_for_each_entry(&ctx_manager->ctx_id_idr, ctx, ctx_id) {

		mutex_unlock(&ctx_manager->lock);

		if (pid == ctx->pid) {
			pr_err("[pid:%d] destroy ctx[%d] when the user exits", pid, ctx->id);
			kref_put(&ctx->refcount, rga_internel_ctx_kref_release);
		}

		mutex_lock(&ctx_manager->lock);
	}

	mutex_unlock(&ctx_manager->lock);

	return 0;
}

static irqreturn_t rga3_irq_handler(int irq, void *data)
{
	struct rga_scheduler_t *rga_scheduler = data;

	if (DEBUGGER_EN(INT_FLAG))
		pr_info("irqthread INT[%x],STATS0[%x], STATS1[%x]\n",
			rga_read(RGA3_INT_RAW, rga_scheduler),
			rga_read(RGA3_STATUS0, rga_scheduler),
			rga_read(RGA3_STATUS1, rga_scheduler));

	/* TODO: if error interrupt then soft reset hardware */
	//rga_scheduler->ops->soft_reset(job->core);

	/*clear INT */
	rga_write(1, RGA3_INT_CLR, rga_scheduler);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rga3_irq_thread(int irq, void *data)
{
	struct rga_scheduler_t *rga_scheduler = data;
	struct rga_job *job;

	job = rga_scheduler->running_job;

	if (!job) {
		pr_err("running job is invaild on irq thread\n");
		return IRQ_HANDLED;
	}

	if (DEBUGGER_EN(INT_FLAG))
		pr_info("irq INT[%x], STATS0[%x], STATS1[%x]\n",
			rga_read(RGA3_INT_RAW, rga_scheduler),
			rga_read(RGA3_STATUS0, rga_scheduler),
			rga_read(RGA3_STATUS1, rga_scheduler));

	rga_job_done(rga_scheduler, 0);

	return IRQ_HANDLED;
}

static irqreturn_t rga2_irq_handler(int irq, void *data)
{
	struct rga_scheduler_t *rga_scheduler = data;

	if (DEBUGGER_EN(INT_FLAG))
		pr_info("irqthread INT[%x],STATS0[%x]\n",
			rga_read(RGA2_INT, rga_scheduler), rga_read(RGA2_STATUS,
								 rga_scheduler));

	/*if error interrupt then soft reset hardware */
	//warning
	if (rga_read(RGA2_INT, rga_scheduler) & 0x01) {
		pr_err("err irq! INT[%x],STATS0[%x]\n",
			 rga_read(RGA2_INT, rga_scheduler),
			 rga_read(RGA2_STATUS, rga_scheduler));
		rga_scheduler->ops->soft_reset(rga_scheduler);
	}

	/*clear INT */
	rga_write(rga_read(RGA2_INT, rga_scheduler) | (0x1 << 4) | (0x1 << 5) |
		 (0x1 << 6) | (0x1 << 7), RGA2_INT, rga_scheduler);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rga2_irq_thread(int irq, void *data)
{
	struct rga_scheduler_t *rga_scheduler = data;
	struct rga_job *job;

	job = rga_scheduler->running_job;

	if (!job)
		return IRQ_HANDLED;

	if (DEBUGGER_EN(INT_FLAG))
		pr_info("irq INT[%x], STATS0[%x]\n",
			rga_read(RGA2_INT, rga_scheduler), rga_read(RGA2_STATUS,
								 rga_scheduler));

	rga_job_done(rga_scheduler, 0);

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

static void init_scheduler(struct rga_scheduler_t *rga_scheduler,
			 const char *name)
{
	spin_lock_init(&rga_scheduler->irq_lock);
	INIT_LIST_HEAD(&rga_scheduler->todo_list);
	init_waitqueue_head(&rga_scheduler->job_done_wq);

	if (!strcmp(name, "rga3_core0")) {
		rga_scheduler->ops = &rga3_ops;
		/* TODO: get by hw version */
		rga_scheduler->data = &rga3_data;
		rga_scheduler->core = RGA3_SCHEDULER_CORE0;
	} else if (!strcmp(name, "rga3_core1")) {
		rga_scheduler->ops = &rga3_ops;
		rga_scheduler->data = &rga3_data;
		rga_scheduler->core = RGA3_SCHEDULER_CORE1;
	} else if (!strcmp(name, "rga2")) {
		rga_scheduler->ops = &rga2_ops;
		rga_scheduler->data = &rga2e_data;
		rga_scheduler->core = RGA2_SCHEDULER_CORE0;
	}
}

static int rga_drv_probe(struct platform_device *pdev)
{
	struct rga_drvdata_t *data = rga_drvdata;
	struct resource *res;
	int ret = 0;
	const struct of_device_id *match = NULL;
	struct device *dev = &pdev->dev;
	const struct rga_match_data_t *match_data;
	int i, irq;
	struct rga_scheduler_t *rga_scheduler = NULL;

	if (!pdev->dev.of_node)
		return -EINVAL;

	if (!strcmp(dev_driver_string(dev), "rga3_core0"))
		match = of_match_device(rga3_core0_dt_ids, dev);
	else if (!strcmp(dev_driver_string(dev), "rga3_core1"))
		match = of_match_device(rga3_core1_dt_ids, dev);
	else if (!strcmp(dev_driver_string(dev), "rga2"))
		match = of_match_device(rga2_dt_ids, dev);

	if (!match) {
		dev_err(dev, "%s missing DT entry!\n", dev_driver_string(dev));
		return -EINVAL;
	}

	rga_scheduler =
		devm_kzalloc(&pdev->dev, sizeof(struct rga_scheduler_t),
			GFP_KERNEL);
	if (rga_scheduler == NULL) {
		pr_err("failed to allocate scheduler. dev name = %s\n",
			dev_driver_string(dev));
		return -ENOMEM;
	}

	init_scheduler(rga_scheduler,
		dev_driver_string(dev));

	rga_scheduler->dev = &pdev->dev;

	/* map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("get memory resource failed.\n");
		return -ENXIO;
	}

	rga_scheduler->rga_base =
		devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!rga_scheduler->rga_base) {
		pr_err("ioremap failed\n");
		ret = -ENOENT;
		return ret;
	}

	/* get the IRQ */
	match_data = match->data;

	/* there are irq names in dts */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq %s in dts\n",
			match_data->irqs[0].name);
		return irq;
	}

	rga_scheduler->irq = irq;

	pr_err("%s, irq = %d, match scheduler\n",
			match_data->irqs[0].name, irq);

	ret = devm_request_threaded_irq(dev, irq,
			match_data->irqs[0].irq_hdl,
			match_data->irqs[0].irq_thread, IRQF_SHARED,
			dev_driver_string(dev),
			rga_scheduler);
	if (ret < 0) {
		pr_err("request irq name: %s failed: %d\n",
				match_data->irqs[0].name, ret);
		return ret;
	}

#ifndef CONFIG_ROCKCHIP_FPGA
	for (i = 0; i < match_data->num_clks; i++) {
		struct clk *clk = devm_clk_get(dev, match_data->clks[i]);

		if (IS_ERR(clk))
			pr_err("failed to get %s\n", match_data->clks[i]);

		rga_scheduler->clks[i] = clk;
	}
	rga_scheduler->num_clks = match_data->num_clks;
#endif

	platform_set_drvdata(pdev, rga_scheduler);

	device_init_wakeup(dev, true);

	/* PM init */
#ifndef CONFIG_ROCKCHIP_FPGA
	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_get_sync(rga_scheduler->dev);
	if (ret < 0) {
		pr_err("failed to get pm runtime, ret = %d\n",
			 ret);
		goto failed;
	}

	for (i = 0; i < rga_scheduler->num_clks; i++) {
		if (!IS_ERR(rga_scheduler->clks[i])) {
			ret = clk_prepare_enable(rga_scheduler->clks[i]);
			if (ret < 0) {
				pr_err("failed to enable clk\n");
				goto failed;
			}
		}
	}
#endif //CONFIG_ROCKCHIP_FPGA

	rga_scheduler->ops->get_version(rga_scheduler);
	pr_err("Driver loaded successfully rga[%d] ver:%s\n", i,
		rga_scheduler->version.str);

	data->rga_scheduler[data->num_of_scheduler] = rga_scheduler;

	data->num_of_scheduler++;

	for (i = rga_scheduler->num_clks - 1; i >= 0; i--)
		if (!IS_ERR(rga_scheduler->clks[i]))
			clk_disable_unprepare(rga_scheduler->clks[i]);

	pm_runtime_put_sync(&pdev->dev);

	pr_err("probe successfully\n");

	return 0;

failed:
	device_init_wakeup(dev, false);
	pm_runtime_disable(dev);

	return ret;
}

static int rga_drv_remove(struct platform_device *pdev)
{
	device_init_wakeup(&pdev->dev, false);
#ifndef CONFIG_ROCKCHIP_FPGA
	pm_runtime_disable(&pdev->dev);
#endif //CONFIG_ROCKCHIP_FPGA

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
	int order = 0;

	uint32_t *buf_p;
	uint32_t *buf;

	/*
	 * malloc pre scale mid buf mmu table:
	 * RGA2_PHY_PAGE_SIZE * channel_num * address_size
	 */
	order = get_order(RGA2_PHY_PAGE_SIZE * 3 * sizeof(buf_p));
	buf_p = (uint32_t *) __get_free_pages(GFP_KERNEL | GFP_DMA32, order);
	if (buf_p == NULL) {
		pr_err("Can not alloc pages for mmu_page_table\n");
		return -ENOMEM;
	}

	rga2_mmu_info.buf_virtual = buf_p;
	rga2_mmu_info.buf_order = order;

#if (defined(CONFIG_ARM) && defined(CONFIG_ARM_LPAE))
	buf =
		(uint32_t *) (uint32_t)
		virt_to_phys((void *)((unsigned long)buf_p));
#else
	buf = (uint32_t *) virt_to_phys((void *)((unsigned long)buf_p));
#endif
	rga2_mmu_info.buf = buf;
	rga2_mmu_info.front = 0;
	rga2_mmu_info.back = RGA2_PHY_PAGE_SIZE * 3;
	rga2_mmu_info.size = RGA2_PHY_PAGE_SIZE * 3;

	order = get_order(RGA2_PHY_PAGE_SIZE * sizeof(struct page *));
	rga2_mmu_info.pages =
		(struct page **)__get_free_pages(GFP_KERNEL | GFP_DMA32, order);
	if (rga2_mmu_info.pages == NULL)
		pr_err("Can not alloc pages for rga2_mmu_info.pages\n");

	rga2_mmu_info.pages_order = order;

	rga_drvdata = kzalloc(sizeof(struct rga_drvdata_t), GFP_KERNEL);
	if (rga_drvdata == NULL) {
		pr_err("failed to allocate driver data.\n");
		return -ENOMEM;
	}

	mutex_init(&rga_drvdata->lock);

	wake_lock_init(&rga_drvdata->wake_lock, WAKE_LOCK_SUSPEND, "rga");

	ret = platform_driver_register(&rga3_core0_driver);
	if (ret != 0) {
		pr_err("Platform device rga3_core0_driver register failed (%d).\n", ret);
		return ret;
	}

	ret = platform_driver_register(&rga3_core1_driver);
	if (ret != 0) {
		pr_err("Platform device rga3_core1_driver register failed (%d).\n", ret);
		return ret;
	}

	ret = platform_driver_register(&rga2_driver);
	if (ret != 0) {
		pr_err("Platform device rga2_driver register failed (%d).\n", ret);
		return ret;
	}

	rga_init_timer();

	rga_drvdata->fence_ctx = rga_fence_context_alloc();
	if (IS_ERR(rga_drvdata->fence_ctx)) {
		pr_err("failed to allocate fence context for RGA\n");
		ret = PTR_ERR(rga_drvdata->fence_ctx);
		return ret;
	}

	ret = misc_register(&rga_dev);
	if (ret) {
		pr_err("cannot register miscdev (%d)\n", ret);
		return ret;
	}

	rga_mm_init(&rga_drvdata->mm);

	rga_ctx_manager_init(&rga_drvdata->pend_ctx_manager);

#ifdef CONFIG_ROCKCHIP_RGA_DEBUGGER
	rga_debugger_init(&rga_drvdata->debugger);
#endif

	pr_info("Module initialized. v%s\n", DRIVER_VERSION);

	return 0;
}

static void __exit rga_exit(void)
{
	free_pages((unsigned long)rga2_mmu_info.buf_virtual,
		 rga2_mmu_info.buf_order);
	free_pages((unsigned long)rga2_mmu_info.pages, rga2_mmu_info.pages_order);

#ifdef CONFIG_ROCKCHIP_RGA_DEBUGGER
	rga_debugger_remove(&rga_drvdata->debugger);
#endif

	rga_mm_remove(&rga_drvdata->mm);

	rga_ctx_manager_remove(&rga_drvdata->pend_ctx_manager);

	wake_lock_destroy(&rga_drvdata->wake_lock);

	rga_fence_context_free(rga_drvdata->fence_ctx);

	rga_cancel_timer();

	platform_driver_unregister(&rga3_core0_driver);
	platform_driver_unregister(&rga3_core1_driver);
	platform_driver_unregister(&rga2_driver);

	misc_deregister(&(rga_drvdata->miscdev));

	kfree(rga_drvdata);
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
