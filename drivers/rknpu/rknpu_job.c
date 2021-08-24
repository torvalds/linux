// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/slab.h>
#include <linux/delay.h>

#include "rknpu_ioctl.h"
#include "rknpu_drv.h"
#include "rknpu_reset.h"
#include "rknpu_gem.h"
#include "rknpu_fence.h"
#include "rknpu_job.h"

#define _REG_READ(base, offset) readl(base + (offset))
#define _REG_WRITE(base, value, offset) writel(value, base + (offset))

#define REG_READ(offset) _REG_READ(rknpu_dev->base, offset)
#define REG_WRITE(value, offset) _REG_WRITE(rknpu_dev->base, value, offset)

static void rknpu_job_free(struct rknpu_job *job)
{
	struct rknpu_gem_object *task_obj = NULL;

	if (job->fence)
		dma_fence_put(job->fence);

	task_obj = (struct rknpu_gem_object *)job->args->task_obj_addr;
	if (task_obj)
		rknpu_gem_object_put(&task_obj->base);

	if (job->args_owner)
		kfree(job->args);

	kfree(job);
}

static int rknpu_job_cleanup(struct rknpu_job *job)
{
	rknpu_job_free(job);

	return 0;
}

static void rknpu_job_cleanup_work(struct work_struct *work)
{
	struct rknpu_job *job =
		container_of(work, struct rknpu_job, cleanup_work);

	rknpu_job_cleanup(job);
}

static inline struct rknpu_job *rknpu_job_alloc(struct rknpu_device *rknpu_dev,
						struct rknpu_submit *args)
{
	struct rknpu_job *job = NULL;
	struct rknpu_gem_object *task_obj = NULL;

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return NULL;

	job->timestamp = ktime_get();
	job->rknpu_dev = rknpu_dev;

	task_obj = (struct rknpu_gem_object *)args->task_obj_addr;
	if (task_obj)
		rknpu_gem_object_get(&task_obj->base);

	if (!(args->flags & RKNPU_JOB_NONBLOCK)) {
		job->args = args;
		job->args_owner = false;
		return job;
	}

	job->args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!job->args) {
		kfree(job);
		return NULL;
	}
	*job->args = *args;
	job->args_owner = true;

	INIT_WORK(&job->cleanup_work, rknpu_job_cleanup_work);

	return job;
}

static inline int rknpu_job_wait(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_submit *args = job->args;
	struct rknpu_task *last_task = NULL;
	int ret = -EINVAL;

	ret = wait_event_interruptible_timeout(rknpu_dev->job_done_wq,
					       job->flags & RKNPU_JOB_DONE,
					       msecs_to_jiffies(args->timeout));

	last_task = job->last_task;
	if (!last_task)
		return -EINVAL;

	last_task->int_status = job->int_status;

	if (ret <= 0) {
		args->task_counter = 0;
		if (args->flags & RKNPU_JOB_PC) {
			uint32_t task_status =
				REG_READ(RKNPU_OFFSET_PC_TASK_STATUS);
			args->task_counter = (task_status & 0xfff);
		}
		return ret < 0 ? ret : -ETIMEDOUT;
	}

	args->task_counter = args->task_number;

	return 0;
}

static inline int rknpu_job_commit_pc(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_submit *args = job->args;
	struct rknpu_gem_object *task_obj =
		(struct rknpu_gem_object *)args->task_obj_addr;
	struct rknpu_task *task_base = NULL;
	struct rknpu_task *first_task = NULL;
	struct rknpu_task *last_task = NULL;
	int task_start = args->task_start;
	int task_end = args->task_start + args->task_number - 1;
	int task_pp_en = args->flags & RKNPU_JOB_PINGPONG ? 1 : 0;

	if (!task_obj)
		return -EINVAL;

	if ((task_start + 1) * sizeof(*task_base) > task_obj->size ||
	    (task_end + 1) * sizeof(*task_base) > task_obj->size)
		return -EINVAL;

	task_base = task_obj->kv_addr;

	first_task = &task_base[task_start];
	last_task = &task_base[task_end];

	REG_WRITE(first_task->regcmd_data, RKNPU_OFFSET_PC_DATA_ADDR);

	REG_WRITE(first_task->regcfg_amount + RKNPU_PC_DATA_EXTRA_AMOUNT - 1,
		  RKNPU_OFFSET_PC_DATA_AMOUNT);

	REG_WRITE(last_task->int_mask, RKNPU_OFFSET_INT_MASK);

	REG_WRITE(first_task->int_mask, RKNPU_OFFSET_INT_CLEAR);

	REG_WRITE(((0x6 | task_pp_en) << 12) | args->task_number,
		  RKNPU_OFFSET_PC_TASK_CONTROL);

	REG_WRITE(0x0, RKNPU_OFFSET_PC_DMA_BASE_ADDR);

	job->first_task = first_task;
	job->last_task = last_task;
	job->int_mask = last_task->int_mask;

	REG_WRITE(0x1, RKNPU_OFFSET_PC_OP_EN);
	REG_WRITE(0x0, RKNPU_OFFSET_PC_OP_EN);

	return 0;
}

static int rknpu_job_commit(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_submit *args = job->args;

	// switch to slave mode
	REG_WRITE(0x1, RKNPU_OFFSET_PC_DATA_ADDR);

	if (!(args->flags & RKNPU_JOB_PC))
		return -EINVAL;

	return rknpu_job_commit_pc(job);
}

static void rknpu_job_next(struct rknpu_device *rknpu_dev)
{
	struct rknpu_job *job = NULL;
	unsigned long flags;

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);

	if (rknpu_dev->job || list_empty(&rknpu_dev->todo_list)) {
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
		return;
	}

	job = list_first_entry(&rknpu_dev->todo_list, struct rknpu_job, head);

	list_del_init(&job->head);

	rknpu_dev->job = job;

	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	job->ret = rknpu_job_commit(job);
}

static void rknpu_job_done(struct rknpu_job *job, int ret)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	unsigned long flags;

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
	rknpu_dev->job = NULL;
	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	job->flags |= RKNPU_JOB_DONE;
	job->ret = ret;

	if (job->fence)
		dma_fence_signal(job->fence);

	wake_up(&rknpu_dev->job_done_wq);
	rknpu_job_next(rknpu_dev);

	if (job->flags & RKNPU_JOB_ASYNC)
		schedule_work(&job->cleanup_work);
}

static void rknpu_job_schedule(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	unsigned long flags;

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
	list_add_tail(&job->head, &rknpu_dev->todo_list);
	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	rknpu_job_next(rknpu_dev);
}

static void rknpu_job_abort(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	unsigned long flags;

	msleep(100);
	if (job->ret == -ETIMEDOUT)
		rknpu_soft_reset(rknpu_dev);

	if (job == rknpu_dev->job) {
		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
		rknpu_dev->job = NULL;
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
	}

	rknpu_job_cleanup(job);
}

static inline uint32_t rknpu_fuzz_status(uint32_t status)
{
	uint32_t fuzz_status = 0;

	if ((status & 0x3) != 0)
		fuzz_status |= 0x3;

	if ((status & 0xc) != 0)
		fuzz_status |= 0xc;

	if ((status & 0x30) != 0)
		fuzz_status |= 0x30;

	if ((status & 0xc0) != 0)
		fuzz_status |= 0xc0;

	if ((status & 0x300) != 0)
		fuzz_status |= 0x300;

	if ((status & 0xc00) != 0)
		fuzz_status |= 0xc00;

	return fuzz_status;
}

irqreturn_t rknpu_irq_handler(int irq, void *data)
{
	struct rknpu_device *rknpu_dev = data;
	struct rknpu_job *job = rknpu_dev->job;
	uint32_t status = 0;

	if (!job)
		return IRQ_HANDLED;

	status = REG_READ(RKNPU_OFFSET_INT_STATUS);
	REG_WRITE(RKNPU_INT_CLEAR, RKNPU_OFFSET_INT_CLEAR);

	job->int_status = status;

	if (rknpu_fuzz_status(status) != job->int_mask) {
		LOG_DEBUG("irq: status = %#x, mask = %#x\n", status,
			  job->int_mask);
		return IRQ_HANDLED;
	}

	rknpu_job_done(job, 0);

	return IRQ_HANDLED;
}

static void rknpu_job_timeout_clean(struct rknpu_device *rknpu_dev)
{
	struct rknpu_job *job = NULL;
	unsigned long flags;
	ktime_t now = ktime_get();

	job = rknpu_dev->job;
	if (job &&
	    ktime_to_ms(ktime_sub(now, job->timestamp)) >= job->args->timeout) {
		rknpu_soft_reset(rknpu_dev);

		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
		rknpu_dev->job = NULL;
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

		do {
			schedule_work(&job->cleanup_work);

			spin_lock_irqsave(&rknpu_dev->irq_lock, flags);

			if (!list_empty(&rknpu_dev->todo_list)) {
				job = list_first_entry(&rknpu_dev->todo_list,
						       struct rknpu_job, head);
				list_del_init(&job->head);
			} else {
				job = NULL;
			}

			spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
		} while (job);
	}
}

int rknpu_submit_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev->dev);
	struct rknpu_submit *args = data;
	struct rknpu_job *job = NULL;
	int ret = -EINVAL;

	if (args->task_number == 0) {
		LOG_ERROR("invalid rknpu task number!\n");
		return -EINVAL;
	}

	job = rknpu_job_alloc(rknpu_dev, args);
	if (!job) {
		LOG_ERROR("failed to allocate rknpu job!\n");
		return -ENOMEM;
	}

	if (args->flags & RKNPU_JOB_FENCE) {
		ret = rknpu_fence_alloc(job);
		if (ret) {
			rknpu_job_free(job);
			return ret;
		}
		job->args->fence_fd = rknpu_fence_get_fd(job);
		args->fence_fd = job->args->fence_fd;
	}

	if (args->flags & RKNPU_JOB_NONBLOCK) {
		job->flags |= RKNPU_JOB_ASYNC;
		rknpu_job_timeout_clean(rknpu_dev);
		rknpu_job_schedule(job);
		ret = job->ret;
		if (ret) {
			rknpu_job_abort(job);
			return ret;
		}
	} else {
		rknpu_job_schedule(job);
		if (args->flags & RKNPU_JOB_PC)
			job->ret = rknpu_job_wait(job);

		args->task_counter = job->args->task_counter;
		ret = job->ret;
		if (!ret)
			rknpu_job_cleanup(job);
		else
			rknpu_job_abort(job);
	}

	return ret;
}

int rknpu_get_hw_version(struct rknpu_device *rknpu_dev, uint32_t *version)
{
	if (version != NULL)
		*version = REG_READ(RKNPU_OFFSET_VERSION);

	return 0;
}

int rknpu_get_bw_priority(struct rknpu_device *rknpu_dev, uint32_t *priority,
			  uint32_t *expect, uint32_t *tw)
{
	void __iomem *base = rknpu_dev->bw_priority_base;

	if (!base)
		return -EINVAL;

	spin_lock(&rknpu_dev->lock);

	if (priority != NULL)
		*priority = _REG_READ(base, 0x0);

	if (expect != NULL)
		*expect = _REG_READ(base, 0x8);

	if (tw != NULL)
		*tw = _REG_READ(base, 0xc);

	spin_unlock(&rknpu_dev->lock);

	return 0;
}

int rknpu_set_bw_priority(struct rknpu_device *rknpu_dev, uint32_t priority,
			  uint32_t expect, uint32_t tw)
{
	void __iomem *base = rknpu_dev->bw_priority_base;

	if (!base)
		return -EINVAL;

	spin_lock(&rknpu_dev->lock);

	if (priority != 0)
		_REG_WRITE(base, priority, 0x0);

	if (expect != 0)
		_REG_WRITE(base, expect, 0x8);

	if (tw != 0)
		_REG_WRITE(base, tw, 0xc);

	spin_unlock(&rknpu_dev->lock);

	return 0;
}

int rknpu_clear_rw_amount(struct rknpu_device *rknpu_dev)
{
	spin_lock(&rknpu_dev->lock);

	REG_WRITE(0x80000101, RKNPU_OFFSET_CLR_ALL_RW_AMOUNT);
	REG_WRITE(0x00000101, RKNPU_OFFSET_CLR_ALL_RW_AMOUNT);

	spin_unlock(&rknpu_dev->lock);

	return 0;
}

int rknpu_get_rw_amount(struct rknpu_device *rknpu_dev, uint32_t *dt_wr,
			uint32_t *dt_rd, uint32_t *wd_rd)
{
	spin_lock(&rknpu_dev->lock);

	if (dt_wr != NULL)
		*dt_wr = REG_READ(RKNPU_OFFSET_DT_WR_AMOUNT);

	if (dt_rd != NULL)
		*dt_rd = REG_READ(RKNPU_OFFSET_DT_RD_AMOUNT);

	if (wd_rd != NULL)
		*wd_rd = REG_READ(RKNPU_OFFSET_WT_RD_AMOUNT);

	spin_unlock(&rknpu_dev->lock);

	return 0;
}

int rknpu_get_total_rw_amount(struct rknpu_device *rknpu_dev, uint32_t *amount)
{
	uint32_t dt_wr = 0;
	uint32_t dt_rd = 0;
	uint32_t wd_rd = 0;
	int ret = -EINVAL;

	ret = rknpu_get_rw_amount(rknpu_dev, &dt_wr, &dt_rd, &wd_rd);

	if (amount != NULL)
		*amount = dt_wr + dt_rd + wd_rd;

	return ret;
}
