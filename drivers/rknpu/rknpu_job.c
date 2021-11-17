// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sync_file.h>
#include <linux/io.h>

#include "rknpu_ioctl.h"
#include "rknpu_drv.h"
#include "rknpu_reset.h"
#include "rknpu_gem.h"
#include "rknpu_fence.h"
#include "rknpu_job.h"

#define _REG_READ(base, offset) readl(base + (offset))
#define _REG_WRITE(base, value, offset) writel(value, base + (offset))

#define REG_READ(offset) _REG_READ(rknpu_core_base, offset)
#define REG_WRITE(value, offset) _REG_WRITE(rknpu_core_base, value, offset)

static int rknpu_core_index(int core_mask)
{
	int index = 0;

	if (core_mask & RKNPU_CORE0_MASK)
		index = 0;
	else if (core_mask & RKNPU_CORE1_MASK)
		index = 1;
	else if (core_mask & RKNPU_CORE2_MASK)
		index = 2;

	return index;
}

static int rknpu_core_mask(int core_index)
{
	int core_mask[3] = { RKNPU_CORE0_MASK, RKNPU_CORE1_MASK,
			     RKNPU_CORE2_MASK };
	return core_mask[core_index];
}

static int rknn_get_task_number(struct rknpu_job *job, int core_index)
{
	int task_num = job->args->task_number;

	if (job->use_core_num == 2)
		task_num = job->args->subcore_task[core_index].task_number;
	else if (job->use_core_num == 3)
		task_num = job->args->subcore_task[core_index + 2].task_number;

	return task_num;
}

static void rknpu_job_free(struct rknpu_job *job)
{
	struct rknpu_gem_object *task_obj = NULL;

	if (job->fence)
		dma_fence_put(job->fence);

	task_obj =
		(struct rknpu_gem_object *)(uintptr_t)job->args->task_obj_addr;
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

	if (rknpu_dev->config->num_irqs == 1)
		args->core_mask = RKNPU_CORE0_MASK;

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return NULL;

	job->timestamp = ktime_get();
	job->rknpu_dev = rknpu_dev;
	job->use_core_num = (args->core_mask & RKNPU_CORE0_MASK) +
			    ((args->core_mask & RKNPU_CORE1_MASK) >> 1) +
			    ((args->core_mask & RKNPU_CORE2_MASK) >> 2);
	job->run_count = job->use_core_num;
	job->interrupt_count = job->use_core_num;

	task_obj = (struct rknpu_gem_object *)(uintptr_t)args->task_obj_addr;
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
	struct rknpu_subcore_data *subcore_data = NULL;
	void __iomem *rknpu_core_base = NULL;
	int core_index = rknpu_core_index(job->args->core_mask);
	int ret = -EINVAL;

	subcore_data = &rknpu_dev->subcore_datas[core_index];
	ret = wait_event_interruptible_timeout(subcore_data->job_done_wq,
					       job->flags & RKNPU_JOB_DONE,
					       msecs_to_jiffies(args->timeout));

	last_task = job->last_task;
	if (!last_task)
		return -EINVAL;

	last_task->int_status = job->int_status[core_index];

	if (ret <= 0) {
		args->task_counter = 0;
		rknpu_core_base = rknpu_dev->base[core_index];
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

static inline int rknpu_job_commit_pc(struct rknpu_job *job, int core_index)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_submit *args = job->args;
	struct rknpu_gem_object *task_obj =
		(struct rknpu_gem_object *)(uintptr_t)args->task_obj_addr;
	struct rknpu_task *task_base = NULL;
	struct rknpu_task *first_task = NULL;
	struct rknpu_task *last_task = NULL;
	void __iomem *rknpu_core_base = rknpu_dev->base[core_index];
	int task_start = args->task_start;
	int task_end = args->task_start + args->task_number - 1;
	int task_number = args->task_number;
	int task_pp_en = args->flags & RKNPU_JOB_PINGPONG ? 1 : 0;
	int i = 0;

	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		if (i == core_index) {
			REG_WRITE((0xe + 0x10000000 * i), 0x1004);
			REG_WRITE((0xe + 0x10000000 * i), 0x3004);
		}
	}

	if (!task_obj)
		return -EINVAL;

	if (job->use_core_num == 1) {
		task_start = args->subcore_task[core_index].task_start;
		task_end = args->subcore_task[core_index].task_start +
			   args->subcore_task[core_index].task_end - 1;
		task_number = args->subcore_task[core_index].task_number;
	} else if (job->use_core_num == 2) {
		task_start = args->subcore_task[core_index].task_start;
		task_end = args->subcore_task[core_index].task_start +
			   args->subcore_task[core_index].task_end - 1;
		task_number = args->subcore_task[core_index].task_number;
	} else if (job->use_core_num == 3) {
		task_start = args->subcore_task[core_index + 2].task_start;
		task_end = args->subcore_task[core_index + 2].task_start +
			   args->subcore_task[core_index + 2].task_end - 1;
		task_number = args->subcore_task[core_index + 2].task_number;
	}

	task_base = task_obj->kv_addr;

	first_task = &task_base[task_start];
	last_task = &task_base[task_end];

	REG_WRITE(first_task->regcmd_data, RKNPU_OFFSET_PC_DATA_ADDR);

	REG_WRITE(first_task->regcfg_amount +
			  rknpu_dev->config->pc_data_extra_amount - 1,
		  RKNPU_OFFSET_PC_DATA_AMOUNT);

	REG_WRITE(last_task->int_mask, RKNPU_OFFSET_INT_MASK);

	REG_WRITE(first_task->int_mask, RKNPU_OFFSET_INT_CLEAR);

	REG_WRITE(((0x6 | task_pp_en) << 12) | task_number,
		  RKNPU_OFFSET_PC_TASK_CONTROL);

	REG_WRITE(0x0, RKNPU_OFFSET_PC_DMA_BASE_ADDR);

	job->first_task = first_task;
	job->last_task = last_task;
	job->int_mask[core_index] = last_task->int_mask;

	REG_WRITE(0x1, RKNPU_OFFSET_PC_OP_EN);
	REG_WRITE(0x0, RKNPU_OFFSET_PC_OP_EN);

	return 0;
}

static int rknpu_job_commit(struct rknpu_job *job, int core_index)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_submit *args = job->args;
	void __iomem *rknpu_core_base = rknpu_dev->base[core_index];

	// switch to slave mode
	REG_WRITE(0x1, RKNPU_OFFSET_PC_DATA_ADDR);

	if (!(args->flags & RKNPU_JOB_PC))
		return -EINVAL;

	return rknpu_job_commit_pc(job, core_index);
}

static void rknpu_job_next(struct rknpu_device *rknpu_dev, int core_index)
{
	struct rknpu_job *job = NULL;
	struct rknpu_subcore_data *subcore_data = NULL;
	unsigned long flags;

	subcore_data = &rknpu_dev->subcore_datas[core_index];

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);

	if (subcore_data->job || list_empty(&subcore_data->todo_list)) {
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
		return;
	}

	job = list_first_entry(&subcore_data->todo_list, struct rknpu_job,
			       head[core_index]);

	list_del_init(&job->head[core_index]);

	subcore_data->job = job;
	job->run_count--;

	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	if (job->run_count == 0) {
		if (job->args->core_mask & RKNPU_CORE0_MASK)
			job->ret = rknpu_job_commit(job, 0);
		if (job->args->core_mask & RKNPU_CORE1_MASK)
			job->ret = rknpu_job_commit(job, 1);
		if (job->args->core_mask & RKNPU_CORE2_MASK)
			job->ret = rknpu_job_commit(job, 2);
	}
}

static void rknpu_job_done(struct rknpu_job *job, int ret, int core_index)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_subcore_data *subcore_data = NULL;
	unsigned long flags;
	int task_num = 0;

	subcore_data = &rknpu_dev->subcore_datas[core_index];
	task_num = rknn_get_task_number(job, core_index);
	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
	subcore_data->job = NULL;
	subcore_data->task_num = subcore_data->task_num - task_num;
	job->interrupt_count--;
	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	if (job->interrupt_count == 0) {
		job->flags |= RKNPU_JOB_DONE;
		job->ret = ret;

		if (job->fence)
			dma_fence_signal(job->fence);

		if (job->use_core_num > 1)
			wake_up(&(&rknpu_dev->subcore_datas[0])->job_done_wq);
		else
			wake_up(&subcore_data->job_done_wq);
	}

	rknpu_job_next(rknpu_dev, core_index);

	if (job->flags & RKNPU_JOB_ASYNC)
		schedule_work(&job->cleanup_work);
}

static void rknpu_job_schedule(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_subcore_data *subcore_data = NULL;
	int i = 0, core_index = 0;
	unsigned long flags;
	int task_num_list[3] = { 0, 1, 2 };
	int task_num = 0;
	int tmp = 0;

	if ((job->args->core_mask & 0x07) == RKNPU_CORE_AUTO_MASK) {
		if (rknpu_dev->subcore_datas[0].task_num >
		    rknpu_dev->subcore_datas[1].task_num) {
			tmp = task_num_list[1];
			task_num_list[1] = task_num_list[0];
			task_num_list[0] = tmp;
		}
		if (rknpu_dev->subcore_datas[task_num_list[0]].task_num >
		    rknpu_dev->subcore_datas[2].task_num) {
			tmp = task_num_list[2];
			task_num_list[2] = task_num_list[1];
			task_num_list[1] = task_num_list[0];
			task_num_list[0] = tmp;
		} else if (rknpu_dev->subcore_datas[task_num_list[1]].task_num >
			   rknpu_dev->subcore_datas[2].task_num) {
			tmp = task_num_list[2];
			task_num_list[2] = task_num_list[1];
			task_num_list[1] = tmp;
		}
		if (!rknpu_dev->subcore_datas[task_num_list[0]].job)
			core_index = task_num_list[0];
		else if (!rknpu_dev->subcore_datas[task_num_list[1]].job)
			core_index = task_num_list[1];
		else if (!rknpu_dev->subcore_datas[task_num_list[2]].job)
			core_index = task_num_list[2];
		else
			core_index = task_num_list[0];

		job->args->core_mask = rknpu_core_mask(core_index);
		job->use_core_num = 1;
		job->interrupt_count = 1;
		job->run_count = 1;
	}

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		if (job->args->core_mask & rknpu_core_mask(i)) {
			subcore_data = &rknpu_dev->subcore_datas[i];
			task_num = rknn_get_task_number(job, i);
			subcore_data->task_num =
				subcore_data->task_num + task_num;
			list_add_tail(&job->head[i], &subcore_data->todo_list);
		}
	}
	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		if (job->args->core_mask & rknpu_core_mask(i))
			rknpu_job_next(rknpu_dev, i);
	}
}

static void rknpu_job_abort(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_subcore_data *subcore_data = NULL;
	int core_index = rknpu_core_index(job->args->core_mask);
	void __iomem *rknpu_core_base = rknpu_dev->base[core_index];
	unsigned long flags;
	int i = 0;
	int task_num = 0;

	msleep(100);
	if (job->ret == -ETIMEDOUT) {
		LOG_ERROR(
			"job timeout, irq status: %#x, raw status: %#x, require mask: %#x\n",
			REG_READ(RKNPU_OFFSET_INT_STATUS),
			REG_READ(RKNPU_OFFSET_INT_RAW_STATUS),
			job->int_mask[core_index]);
		rknpu_soft_reset(rknpu_dev);
	}
	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		if (job->args->core_mask & rknpu_core_mask(i)) {
			subcore_data = &rknpu_dev->subcore_datas[i];
			if (job == subcore_data->job) {
				spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
				task_num = rknn_get_task_number(job, i);
				subcore_data->job = NULL;
				subcore_data->task_num =
					subcore_data->task_num - task_num;
				spin_unlock_irqrestore(&rknpu_dev->irq_lock,
						       flags);
			}
		}
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

static inline irqreturn_t rknpu_irq_handler(int irq, void *data, int core_index)
{
	struct rknpu_device *rknpu_dev = data;
	struct rknpu_job *job = rknpu_dev->subcore_datas[core_index].job;
	void __iomem *rknpu_core_base = rknpu_dev->base[core_index];
	uint32_t status = 0;

	if (!job)
		return IRQ_HANDLED;

	status = REG_READ(RKNPU_OFFSET_INT_STATUS);
	REG_WRITE(RKNPU_INT_CLEAR, RKNPU_OFFSET_INT_CLEAR);

	job->int_status[core_index] = status;

	if (rknpu_fuzz_status(status) != job->int_mask[core_index]) {
		LOG_ERROR(
			"invalid irq status: %#x, raw status: %#x, require mask: %#x\n",
			status, REG_READ(RKNPU_OFFSET_INT_RAW_STATUS),
			job->int_mask[core_index]);
		return IRQ_HANDLED;
	}

	rknpu_job_done(job, 0, core_index);

	return IRQ_HANDLED;
}

irqreturn_t rknpu_core0_irq_handler(int irq, void *data)
{
	return rknpu_irq_handler(irq, data, 0);
}

irqreturn_t rknpu_core1_irq_handler(int irq, void *data)
{
	return rknpu_irq_handler(irq, data, 1);
}

irqreturn_t rknpu_core2_irq_handler(int irq, void *data)
{
	return rknpu_irq_handler(irq, data, 2);
}

static void rknpu_job_timeout_clean(struct rknpu_device *rknpu_dev,
				    int core_mask)
{
	struct rknpu_job *job = NULL;
	unsigned long flags;
	ktime_t now = ktime_get();
	struct rknpu_subcore_data *subcore_data = NULL;
	int i = 0;

	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		if (core_mask & rknpu_core_mask(i)) {
			subcore_data = &rknpu_dev->subcore_datas[i];
			job = subcore_data->job;
			if (job &&
			    ktime_to_ms(ktime_sub(now, job->timestamp)) >=
				    job->args->timeout) {
				rknpu_soft_reset(rknpu_dev);

				spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
				subcore_data->job = NULL;
				spin_unlock_irqrestore(&rknpu_dev->irq_lock,
						       flags);

				do {
					schedule_work(&job->cleanup_work);

					spin_lock_irqsave(&rknpu_dev->irq_lock,
							  flags);

					if (!list_empty(
						    &subcore_data->todo_list)) {
						job = list_first_entry(
							&subcore_data->todo_list,
							struct rknpu_job,
							head[i]);
						list_del_init(&job->head[i]);
					} else {
						job = NULL;
					}

					spin_unlock_irqrestore(
						&rknpu_dev->irq_lock, flags);
				} while (job);
			}
		}
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

	if (args->flags & RKNPU_JOB_FENCE_IN) {
		struct dma_fence *in_fence;

		in_fence = sync_file_get_fence(args->fence_fd);

		if (!in_fence) {
			LOG_ERROR("invalid fence in fd, fd = %d\n",
				  args->fence_fd);
			return -EINVAL;
		}
		args->fence_fd = -1;

		/*
		 * Wait if the fence is from a foreign context, or if the fence
		 * array contains any fence from a foreign context.
		 */
		ret = 0;
		if (!dma_fence_match_context(in_fence,
					     rknpu_dev->fence_ctx->context))
			ret = dma_fence_wait_timeout(in_fence, true,
						     args->timeout);
		dma_fence_put(in_fence);
		if (ret < 0) {
			if (ret != -ERESTARTSYS)
				LOG_ERROR("Error (%d) waiting for fence!\n",
					  ret);

			return ret;
		}
	}

	if (args->flags & RKNPU_JOB_FENCE_OUT) {
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
		rknpu_job_timeout_clean(rknpu_dev, job->args->core_mask);
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
	void __iomem *rknpu_core_base = rknpu_dev->base[0];

	if (version != NULL)
		*version = REG_READ(RKNPU_OFFSET_VERSION);

	return 0;
}

int rknpu_get_bw_priority(struct rknpu_device *rknpu_dev, uint32_t *priority,
			  uint32_t *expect, uint32_t *tw)
{
	void __iomem *base = rknpu_dev->bw_priority_base;

	if (!rknpu_dev->config->bw_enable) {
		LOG_WARN("Get bw_priority is not supported on this device!\n");
		return 0;
	}

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

	if (!rknpu_dev->config->bw_enable) {
		LOG_WARN("Set bw_priority is not supported on this device!\n");
		return 0;
	}

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
	void __iomem *rknpu_core_base = rknpu_dev->base[0];

	if (!rknpu_dev->config->bw_enable) {
		LOG_WARN("Clear rw_amount is not supported on this device!\n");
		return 0;
	}

	spin_lock(&rknpu_dev->lock);

	REG_WRITE(0x80000101, RKNPU_OFFSET_CLR_ALL_RW_AMOUNT);
	REG_WRITE(0x00000101, RKNPU_OFFSET_CLR_ALL_RW_AMOUNT);

	spin_unlock(&rknpu_dev->lock);

	return 0;
}

int rknpu_get_rw_amount(struct rknpu_device *rknpu_dev, uint32_t *dt_wr,
			uint32_t *dt_rd, uint32_t *wd_rd)
{
	void __iomem *rknpu_core_base = rknpu_dev->base[0];

	if (!rknpu_dev->config->bw_enable) {
		LOG_WARN("Get rw_amount is not supported on this device!\n");
		return 0;
	}

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

	if (!rknpu_dev->config->bw_enable) {
		LOG_WARN(
			"Get total_rw_amount is not supported on this device!\n");
		return 0;
	}

	ret = rknpu_get_rw_amount(rknpu_dev, &dt_wr, &dt_rd, &wd_rd);

	if (amount != NULL)
		*amount = dt_wr + dt_rd + wd_rd;

	return ret;
}
