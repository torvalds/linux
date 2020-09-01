// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <drm/lima_drm.h>

#include "lima_device.h"
#include "lima_pp.h"
#include "lima_dlbu.h"
#include "lima_bcast.h"
#include "lima_vm.h"
#include "lima_regs.h"

#define pp_write(reg, data) writel(data, ip->iomem + reg)
#define pp_read(reg) readl(ip->iomem + reg)

static void lima_pp_handle_irq(struct lima_ip *ip, u32 state)
{
	struct lima_device *dev = ip->dev;
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_pp;

	if (state & LIMA_PP_IRQ_MASK_ERROR) {
		u32 status = pp_read(LIMA_PP_STATUS);

		dev_err(dev->dev, "pp error irq state=%x status=%x\n",
			state, status);

		pipe->error = true;

		/* mask all interrupts before hard reset */
		pp_write(LIMA_PP_INT_MASK, 0);
	}

	pp_write(LIMA_PP_INT_CLEAR, state);
}

static irqreturn_t lima_pp_irq_handler(int irq, void *data)
{
	struct lima_ip *ip = data;
	struct lima_device *dev = ip->dev;
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_pp;
	u32 state = pp_read(LIMA_PP_INT_STATUS);

	/* for shared irq case */
	if (!state)
		return IRQ_NONE;

	lima_pp_handle_irq(ip, state);

	if (atomic_dec_and_test(&pipe->task))
		lima_sched_pipe_task_done(pipe);

	return IRQ_HANDLED;
}

static irqreturn_t lima_pp_bcast_irq_handler(int irq, void *data)
{
	int i;
	irqreturn_t ret = IRQ_NONE;
	struct lima_ip *pp_bcast = data;
	struct lima_device *dev = pp_bcast->dev;
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_pp;
	struct drm_lima_m450_pp_frame *frame;

	/* for shared irq case */
	if (!pipe->current_task)
		return IRQ_NONE;

	frame = pipe->current_task->frame;

	for (i = 0; i < frame->num_pp; i++) {
		struct lima_ip *ip = pipe->processor[i];
		u32 status, state;

		if (pipe->done & (1 << i))
			continue;

		/* status read first in case int state change in the middle
		 * which may miss the interrupt handling
		 */
		status = pp_read(LIMA_PP_STATUS);
		state = pp_read(LIMA_PP_INT_STATUS);

		if (state) {
			lima_pp_handle_irq(ip, state);
			ret = IRQ_HANDLED;
		} else {
			if (status & LIMA_PP_STATUS_RENDERING_ACTIVE)
				continue;
		}

		pipe->done |= (1 << i);
		if (atomic_dec_and_test(&pipe->task))
			lima_sched_pipe_task_done(pipe);
	}

	return ret;
}

static void lima_pp_soft_reset_async(struct lima_ip *ip)
{
	if (ip->data.async_reset)
		return;

	pp_write(LIMA_PP_INT_MASK, 0);
	pp_write(LIMA_PP_INT_RAWSTAT, LIMA_PP_IRQ_MASK_ALL);
	pp_write(LIMA_PP_CTRL, LIMA_PP_CTRL_SOFT_RESET);
	ip->data.async_reset = true;
}

static int lima_pp_soft_reset_poll(struct lima_ip *ip)
{
	return !(pp_read(LIMA_PP_STATUS) & LIMA_PP_STATUS_RENDERING_ACTIVE) &&
		pp_read(LIMA_PP_INT_RAWSTAT) == LIMA_PP_IRQ_RESET_COMPLETED;
}

static int lima_pp_soft_reset_async_wait_one(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int ret;

	ret = lima_poll_timeout(ip, lima_pp_soft_reset_poll, 0, 100);
	if (ret) {
		dev_err(dev->dev, "pp %s reset time out\n", lima_ip_name(ip));
		return ret;
	}

	pp_write(LIMA_PP_INT_CLEAR, LIMA_PP_IRQ_MASK_ALL);
	pp_write(LIMA_PP_INT_MASK, LIMA_PP_IRQ_MASK_USED);
	return 0;
}

static int lima_pp_soft_reset_async_wait(struct lima_ip *ip)
{
	int i, err = 0;

	if (!ip->data.async_reset)
		return 0;

	if (ip->id == lima_ip_pp_bcast) {
		struct lima_device *dev = ip->dev;
		struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_pp;
		struct drm_lima_m450_pp_frame *frame = pipe->current_task->frame;

		for (i = 0; i < frame->num_pp; i++)
			err |= lima_pp_soft_reset_async_wait_one(pipe->processor[i]);
	} else
		err = lima_pp_soft_reset_async_wait_one(ip);

	ip->data.async_reset = false;
	return err;
}

static void lima_pp_write_frame(struct lima_ip *ip, u32 *frame, u32 *wb)
{
	int i, j, n = 0;

	for (i = 0; i < LIMA_PP_FRAME_REG_NUM; i++)
		writel(frame[i], ip->iomem + LIMA_PP_FRAME + i * 4);

	for (i = 0; i < 3; i++) {
		for (j = 0; j < LIMA_PP_WB_REG_NUM; j++)
			writel(wb[n++], ip->iomem + LIMA_PP_WB(i) + j * 4);
	}
}

static int lima_pp_hard_reset_poll(struct lima_ip *ip)
{
	pp_write(LIMA_PP_PERF_CNT_0_LIMIT, 0xC01A0000);
	return pp_read(LIMA_PP_PERF_CNT_0_LIMIT) == 0xC01A0000;
}

static int lima_pp_hard_reset(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int ret;

	pp_write(LIMA_PP_PERF_CNT_0_LIMIT, 0xC0FFE000);
	pp_write(LIMA_PP_INT_MASK, 0);
	pp_write(LIMA_PP_CTRL, LIMA_PP_CTRL_FORCE_RESET);
	ret = lima_poll_timeout(ip, lima_pp_hard_reset_poll, 10, 100);
	if (ret) {
		dev_err(dev->dev, "pp hard reset timeout\n");
		return ret;
	}

	pp_write(LIMA_PP_PERF_CNT_0_LIMIT, 0);
	pp_write(LIMA_PP_INT_CLEAR, LIMA_PP_IRQ_MASK_ALL);
	pp_write(LIMA_PP_INT_MASK, LIMA_PP_IRQ_MASK_USED);
	return 0;
}

static void lima_pp_print_version(struct lima_ip *ip)
{
	u32 version, major, minor;
	char *name;

	version = pp_read(LIMA_PP_VERSION);
	major = (version >> 8) & 0xFF;
	minor = version & 0xFF;
	switch (version >> 16) {
	case 0xC807:
	    name = "mali200";
		break;
	case 0xCE07:
		name = "mali300";
		break;
	case 0xCD07:
		name = "mali400";
		break;
	case 0xCF07:
		name = "mali450";
		break;
	default:
		name = "unknown";
		break;
	}
	dev_info(ip->dev->dev, "%s - %s version major %d minor %d\n",
		 lima_ip_name(ip), name, major, minor);
}

static int lima_pp_hw_init(struct lima_ip *ip)
{
	ip->data.async_reset = false;
	lima_pp_soft_reset_async(ip);
	return lima_pp_soft_reset_async_wait(ip);
}

int lima_pp_resume(struct lima_ip *ip)
{
	return lima_pp_hw_init(ip);
}

void lima_pp_suspend(struct lima_ip *ip)
{

}

int lima_pp_init(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;

	lima_pp_print_version(ip);

	err = lima_pp_hw_init(ip);
	if (err)
		return err;

	err = devm_request_irq(dev->dev, ip->irq, lima_pp_irq_handler,
			       IRQF_SHARED, lima_ip_name(ip), ip);
	if (err) {
		dev_err(dev->dev, "pp %s fail to request irq\n",
			lima_ip_name(ip));
		return err;
	}

	dev->pp_version = pp_read(LIMA_PP_VERSION);

	return 0;
}

void lima_pp_fini(struct lima_ip *ip)
{

}

int lima_pp_bcast_resume(struct lima_ip *ip)
{
	/* PP has been reset by individual PP resume */
	ip->data.async_reset = false;
	return 0;
}

void lima_pp_bcast_suspend(struct lima_ip *ip)
{

}

int lima_pp_bcast_init(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;

	err = devm_request_irq(dev->dev, ip->irq, lima_pp_bcast_irq_handler,
			       IRQF_SHARED, lima_ip_name(ip), ip);
	if (err) {
		dev_err(dev->dev, "pp %s fail to request irq\n",
			lima_ip_name(ip));
		return err;
	}

	return 0;
}

void lima_pp_bcast_fini(struct lima_ip *ip)
{

}

static int lima_pp_task_validate(struct lima_sched_pipe *pipe,
				 struct lima_sched_task *task)
{
	u32 num_pp;

	if (pipe->bcast_processor) {
		struct drm_lima_m450_pp_frame *f = task->frame;

		num_pp = f->num_pp;

		if (f->_pad)
			return -EINVAL;
	} else {
		struct drm_lima_m400_pp_frame *f = task->frame;

		num_pp = f->num_pp;
	}

	if (num_pp == 0 || num_pp > pipe->num_processor)
		return -EINVAL;

	return 0;
}

static void lima_pp_task_run(struct lima_sched_pipe *pipe,
			     struct lima_sched_task *task)
{
	if (pipe->bcast_processor) {
		struct drm_lima_m450_pp_frame *frame = task->frame;
		struct lima_device *dev = pipe->bcast_processor->dev;
		struct lima_ip *ip = pipe->bcast_processor;
		int i;

		pipe->done = 0;
		atomic_set(&pipe->task, frame->num_pp);

		if (frame->use_dlbu) {
			lima_dlbu_enable(dev, frame->num_pp);

			frame->frame[LIMA_PP_FRAME >> 2] = LIMA_VA_RESERVE_DLBU;
			lima_dlbu_set_reg(dev->ip + lima_ip_dlbu, frame->dlbu_regs);
		} else
			lima_dlbu_disable(dev);

		lima_bcast_enable(dev, frame->num_pp);

		lima_pp_soft_reset_async_wait(ip);

		lima_pp_write_frame(ip, frame->frame, frame->wb);

		for (i = 0; i < frame->num_pp; i++) {
			struct lima_ip *ip = pipe->processor[i];

			pp_write(LIMA_PP_STACK, frame->fragment_stack_address[i]);
			if (!frame->use_dlbu)
				pp_write(LIMA_PP_FRAME, frame->plbu_array_address[i]);
		}

		pp_write(LIMA_PP_CTRL, LIMA_PP_CTRL_START_RENDERING);
	} else {
		struct drm_lima_m400_pp_frame *frame = task->frame;
		int i;

		atomic_set(&pipe->task, frame->num_pp);

		for (i = 0; i < frame->num_pp; i++) {
			struct lima_ip *ip = pipe->processor[i];

			frame->frame[LIMA_PP_FRAME >> 2] =
				frame->plbu_array_address[i];
			frame->frame[LIMA_PP_STACK >> 2] =
				frame->fragment_stack_address[i];

			lima_pp_soft_reset_async_wait(ip);

			lima_pp_write_frame(ip, frame->frame, frame->wb);

			pp_write(LIMA_PP_CTRL, LIMA_PP_CTRL_START_RENDERING);
		}
	}
}

static void lima_pp_task_fini(struct lima_sched_pipe *pipe)
{
	if (pipe->bcast_processor)
		lima_pp_soft_reset_async(pipe->bcast_processor);
	else {
		int i;

		for (i = 0; i < pipe->num_processor; i++)
			lima_pp_soft_reset_async(pipe->processor[i]);
	}
}

static void lima_pp_task_error(struct lima_sched_pipe *pipe)
{
	int i;

	for (i = 0; i < pipe->num_processor; i++) {
		struct lima_ip *ip = pipe->processor[i];

		dev_err(ip->dev->dev, "pp task error %d int_state=%x status=%x\n",
			i, pp_read(LIMA_PP_INT_STATUS), pp_read(LIMA_PP_STATUS));

		lima_pp_hard_reset(ip);
	}
}

static void lima_pp_task_mmu_error(struct lima_sched_pipe *pipe)
{
	if (atomic_dec_and_test(&pipe->task))
		lima_sched_pipe_task_done(pipe);
}

static struct kmem_cache *lima_pp_task_slab;
static int lima_pp_task_slab_refcnt;

int lima_pp_pipe_init(struct lima_device *dev)
{
	int frame_size;
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_pp;

	if (dev->id == lima_gpu_mali400)
		frame_size = sizeof(struct drm_lima_m400_pp_frame);
	else
		frame_size = sizeof(struct drm_lima_m450_pp_frame);

	if (!lima_pp_task_slab) {
		lima_pp_task_slab = kmem_cache_create_usercopy(
			"lima_pp_task", sizeof(struct lima_sched_task) + frame_size,
			0, SLAB_HWCACHE_ALIGN, sizeof(struct lima_sched_task),
			frame_size, NULL);
		if (!lima_pp_task_slab)
			return -ENOMEM;
	}
	lima_pp_task_slab_refcnt++;

	pipe->frame_size = frame_size;
	pipe->task_slab = lima_pp_task_slab;

	pipe->task_validate = lima_pp_task_validate;
	pipe->task_run = lima_pp_task_run;
	pipe->task_fini = lima_pp_task_fini;
	pipe->task_error = lima_pp_task_error;
	pipe->task_mmu_error = lima_pp_task_mmu_error;

	return 0;
}

void lima_pp_pipe_fini(struct lima_device *dev)
{
	if (!--lima_pp_task_slab_refcnt) {
		kmem_cache_destroy(lima_pp_task_slab);
		lima_pp_task_slab = NULL;
	}
}
