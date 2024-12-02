// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <drm/lima_drm.h>

#include "lima_device.h"
#include "lima_gp.h"
#include "lima_regs.h"
#include "lima_gem.h"
#include "lima_vm.h"

#define gp_write(reg, data) writel(data, ip->iomem + reg)
#define gp_read(reg) readl(ip->iomem + reg)

static irqreturn_t lima_gp_irq_handler(int irq, void *data)
{
	struct lima_ip *ip = data;
	struct lima_device *dev = ip->dev;
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_gp;
	struct lima_sched_task *task = pipe->current_task;
	u32 state = gp_read(LIMA_GP_INT_STAT);
	u32 status = gp_read(LIMA_GP_STATUS);
	bool done = false;

	/* for shared irq case */
	if (!state)
		return IRQ_NONE;

	if (state & LIMA_GP_IRQ_MASK_ERROR) {
		if ((state & LIMA_GP_IRQ_MASK_ERROR) ==
		    LIMA_GP_IRQ_PLBU_OUT_OF_MEM) {
			dev_dbg(dev->dev, "gp out of heap irq status=%x\n",
				status);
		} else {
			dev_err(dev->dev, "gp error irq state=%x status=%x\n",
				state, status);
			if (task)
				task->recoverable = false;
		}

		/* mask all interrupts before hard reset */
		gp_write(LIMA_GP_INT_MASK, 0);

		pipe->error = true;
		done = true;
	} else {
		bool valid = state & (LIMA_GP_IRQ_VS_END_CMD_LST |
				      LIMA_GP_IRQ_PLBU_END_CMD_LST);
		bool active = status & (LIMA_GP_STATUS_VS_ACTIVE |
					LIMA_GP_STATUS_PLBU_ACTIVE);
		done = valid && !active;
		pipe->error = false;
	}

	gp_write(LIMA_GP_INT_CLEAR, state);

	if (done)
		lima_sched_pipe_task_done(pipe);

	return IRQ_HANDLED;
}

static void lima_gp_soft_reset_async(struct lima_ip *ip)
{
	if (ip->data.async_reset)
		return;

	gp_write(LIMA_GP_INT_MASK, 0);
	gp_write(LIMA_GP_INT_CLEAR, LIMA_GP_IRQ_RESET_COMPLETED);
	gp_write(LIMA_GP_CMD, LIMA_GP_CMD_SOFT_RESET);
	ip->data.async_reset = true;
}

static int lima_gp_soft_reset_async_wait(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;
	u32 v;

	if (!ip->data.async_reset)
		return 0;

	err = readl_poll_timeout(ip->iomem + LIMA_GP_INT_RAWSTAT, v,
				 v & LIMA_GP_IRQ_RESET_COMPLETED,
				 0, 100);
	if (err) {
		dev_err(dev->dev, "gp soft reset time out\n");
		return err;
	}

	gp_write(LIMA_GP_INT_CLEAR, LIMA_GP_IRQ_MASK_ALL);
	gp_write(LIMA_GP_INT_MASK, LIMA_GP_IRQ_MASK_USED);

	ip->data.async_reset = false;
	return 0;
}

static int lima_gp_task_validate(struct lima_sched_pipe *pipe,
				 struct lima_sched_task *task)
{
	struct drm_lima_gp_frame *frame = task->frame;
	u32 *f = frame->frame;
	(void)pipe;

	if (f[LIMA_GP_VSCL_START_ADDR >> 2] >
	    f[LIMA_GP_VSCL_END_ADDR >> 2] ||
	    f[LIMA_GP_PLBUCL_START_ADDR >> 2] >
	    f[LIMA_GP_PLBUCL_END_ADDR >> 2] ||
	    f[LIMA_GP_PLBU_ALLOC_START_ADDR >> 2] >
	    f[LIMA_GP_PLBU_ALLOC_END_ADDR >> 2])
		return -EINVAL;

	if (f[LIMA_GP_VSCL_START_ADDR >> 2] ==
	    f[LIMA_GP_VSCL_END_ADDR >> 2] &&
	    f[LIMA_GP_PLBUCL_START_ADDR >> 2] ==
	    f[LIMA_GP_PLBUCL_END_ADDR >> 2])
		return -EINVAL;

	return 0;
}

static void lima_gp_task_run(struct lima_sched_pipe *pipe,
			     struct lima_sched_task *task)
{
	struct lima_ip *ip = pipe->processor[0];
	struct drm_lima_gp_frame *frame = task->frame;
	u32 *f = frame->frame;
	u32 cmd = 0;
	int i;

	/* update real heap buffer size for GP */
	for (i = 0; i < task->num_bos; i++) {
		struct lima_bo *bo = task->bos[i];

		if (bo->heap_size &&
		    lima_vm_get_va(task->vm, bo) ==
		    f[LIMA_GP_PLBU_ALLOC_START_ADDR >> 2]) {
			f[LIMA_GP_PLBU_ALLOC_END_ADDR >> 2] =
				f[LIMA_GP_PLBU_ALLOC_START_ADDR >> 2] +
				bo->heap_size;
			task->recoverable = true;
			task->heap = bo;
			break;
		}
	}

	if (f[LIMA_GP_VSCL_START_ADDR >> 2] !=
	    f[LIMA_GP_VSCL_END_ADDR >> 2])
		cmd |= LIMA_GP_CMD_START_VS;
	if (f[LIMA_GP_PLBUCL_START_ADDR >> 2] !=
	    f[LIMA_GP_PLBUCL_END_ADDR >> 2])
		cmd |= LIMA_GP_CMD_START_PLBU;

	/* before any hw ops, wait last success task async soft reset */
	lima_gp_soft_reset_async_wait(ip);

	for (i = 0; i < LIMA_GP_FRAME_REG_NUM; i++)
		writel(f[i], ip->iomem + LIMA_GP_VSCL_START_ADDR + i * 4);

	gp_write(LIMA_GP_CMD, LIMA_GP_CMD_UPDATE_PLBU_ALLOC);
	gp_write(LIMA_GP_CMD, cmd);
}

static int lima_gp_bus_stop_poll(struct lima_ip *ip)
{
	return !!(gp_read(LIMA_GP_STATUS) & LIMA_GP_STATUS_BUS_STOPPED);
}

static int lima_gp_hard_reset_poll(struct lima_ip *ip)
{
	gp_write(LIMA_GP_PERF_CNT_0_LIMIT, 0xC01A0000);
	return gp_read(LIMA_GP_PERF_CNT_0_LIMIT) == 0xC01A0000;
}

static int lima_gp_hard_reset(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int ret;

	gp_write(LIMA_GP_PERF_CNT_0_LIMIT, 0xC0FFE000);
	gp_write(LIMA_GP_INT_MASK, 0);

	gp_write(LIMA_GP_CMD, LIMA_GP_CMD_STOP_BUS);
	ret = lima_poll_timeout(ip, lima_gp_bus_stop_poll, 10, 100);
	if (ret) {
		dev_err(dev->dev, "%s bus stop timeout\n", lima_ip_name(ip));
		return ret;
	}
	gp_write(LIMA_GP_CMD, LIMA_GP_CMD_RESET);
	ret = lima_poll_timeout(ip, lima_gp_hard_reset_poll, 10, 100);
	if (ret) {
		dev_err(dev->dev, "gp hard reset timeout\n");
		return ret;
	}

	gp_write(LIMA_GP_PERF_CNT_0_LIMIT, 0);
	gp_write(LIMA_GP_INT_CLEAR, LIMA_GP_IRQ_MASK_ALL);
	gp_write(LIMA_GP_INT_MASK, LIMA_GP_IRQ_MASK_USED);
	return 0;
}

static void lima_gp_task_fini(struct lima_sched_pipe *pipe)
{
	lima_gp_soft_reset_async(pipe->processor[0]);
}

static void lima_gp_task_error(struct lima_sched_pipe *pipe)
{
	struct lima_ip *ip = pipe->processor[0];

	dev_err(ip->dev->dev, "gp task error int_state=%x status=%x\n",
		gp_read(LIMA_GP_INT_STAT), gp_read(LIMA_GP_STATUS));

	lima_gp_hard_reset(ip);
}

static void lima_gp_task_mmu_error(struct lima_sched_pipe *pipe)
{
	lima_sched_pipe_task_done(pipe);
}

static void lima_gp_task_mask_irq(struct lima_sched_pipe *pipe)
{
	struct lima_ip *ip = pipe->processor[0];

	gp_write(LIMA_GP_INT_MASK, 0);
}

static int lima_gp_task_recover(struct lima_sched_pipe *pipe)
{
	struct lima_ip *ip = pipe->processor[0];
	struct lima_sched_task *task = pipe->current_task;
	struct drm_lima_gp_frame *frame = task->frame;
	u32 *f = frame->frame;
	size_t fail_size =
		f[LIMA_GP_PLBU_ALLOC_END_ADDR >> 2] -
		f[LIMA_GP_PLBU_ALLOC_START_ADDR >> 2];

	if (fail_size == task->heap->heap_size) {
		int ret;

		ret = lima_heap_alloc(task->heap, task->vm);
		if (ret < 0)
			return ret;
	}

	gp_write(LIMA_GP_INT_MASK, LIMA_GP_IRQ_MASK_USED);
	/* Resume from where we stopped, i.e. new start is old end */
	gp_write(LIMA_GP_PLBU_ALLOC_START_ADDR,
		 f[LIMA_GP_PLBU_ALLOC_END_ADDR >> 2]);
	f[LIMA_GP_PLBU_ALLOC_END_ADDR >> 2] =
		f[LIMA_GP_PLBU_ALLOC_START_ADDR >> 2] + task->heap->heap_size;
	gp_write(LIMA_GP_PLBU_ALLOC_END_ADDR,
		 f[LIMA_GP_PLBU_ALLOC_END_ADDR >> 2]);
	gp_write(LIMA_GP_CMD, LIMA_GP_CMD_UPDATE_PLBU_ALLOC);
	return 0;
}

static void lima_gp_print_version(struct lima_ip *ip)
{
	u32 version, major, minor;
	char *name;

	version = gp_read(LIMA_GP_VERSION);
	major = (version >> 8) & 0xFF;
	minor = version & 0xFF;
	switch (version >> 16) {
	case 0xA07:
	    name = "mali200";
		break;
	case 0xC07:
		name = "mali300";
		break;
	case 0xB07:
		name = "mali400";
		break;
	case 0xD07:
		name = "mali450";
		break;
	default:
		name = "unknown";
		break;
	}
	dev_info(ip->dev->dev, "%s - %s version major %d minor %d\n",
		 lima_ip_name(ip), name, major, minor);
}

static struct kmem_cache *lima_gp_task_slab;
static int lima_gp_task_slab_refcnt;

static int lima_gp_hw_init(struct lima_ip *ip)
{
	ip->data.async_reset = false;
	lima_gp_soft_reset_async(ip);
	return lima_gp_soft_reset_async_wait(ip);
}

int lima_gp_resume(struct lima_ip *ip)
{
	return lima_gp_hw_init(ip);
}

void lima_gp_suspend(struct lima_ip *ip)
{

}

int lima_gp_init(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;

	lima_gp_print_version(ip);

	err = lima_gp_hw_init(ip);
	if (err)
		return err;

	err = devm_request_irq(dev->dev, ip->irq, lima_gp_irq_handler,
			       IRQF_SHARED, lima_ip_name(ip), ip);
	if (err) {
		dev_err(dev->dev, "gp %s fail to request irq\n",
			lima_ip_name(ip));
		return err;
	}

	dev->gp_version = gp_read(LIMA_GP_VERSION);

	return 0;
}

void lima_gp_fini(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;

	devm_free_irq(dev->dev, ip->irq, ip);
}

int lima_gp_pipe_init(struct lima_device *dev)
{
	int frame_size = sizeof(struct drm_lima_gp_frame);
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_gp;

	if (!lima_gp_task_slab) {
		lima_gp_task_slab = kmem_cache_create_usercopy(
			"lima_gp_task", sizeof(struct lima_sched_task) + frame_size,
			0, SLAB_HWCACHE_ALIGN, sizeof(struct lima_sched_task),
			frame_size, NULL);
		if (!lima_gp_task_slab)
			return -ENOMEM;
	}
	lima_gp_task_slab_refcnt++;

	pipe->frame_size = frame_size;
	pipe->task_slab = lima_gp_task_slab;

	pipe->task_validate = lima_gp_task_validate;
	pipe->task_run = lima_gp_task_run;
	pipe->task_fini = lima_gp_task_fini;
	pipe->task_error = lima_gp_task_error;
	pipe->task_mmu_error = lima_gp_task_mmu_error;
	pipe->task_recover = lima_gp_task_recover;
	pipe->task_mask_irq = lima_gp_task_mask_irq;

	return 0;
}

void lima_gp_pipe_fini(struct lima_device *dev)
{
	if (!--lima_gp_task_slab_refcnt) {
		kmem_cache_destroy(lima_gp_task_slab);
		lima_gp_task_slab = NULL;
	}
}
