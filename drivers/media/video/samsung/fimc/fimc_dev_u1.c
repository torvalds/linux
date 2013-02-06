/* linux/drivers/media/video/samsung/fimc/fimc_dev.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Core file for Samsung Camera Interface (FIMC) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <media/v4l2-device.h>
#include <linux/io.h>
#include <linux/memory.h>
#include <linux/ctype.h>
#include <linux/workqueue.h>
#include <plat/clock.h>
#if defined(CONFIG_CMA)
#include <linux/cma.h>
#elif defined(CONFIG_S5P_MEM_BOOTMEM)
#include <plat/media.h>
#include <mach/media.h>
#endif
#include <plat/fimc.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/videodev2_exynos_camera.h>

#include <mach/regs-pmu.h>
#include <linux/delay.h>

#include "fimc.h"

char buf[32];
struct fimc_global *fimc_dev;
void __iomem			*qos_regs0 , *qos_regs1;

void s3c_fimc_irq_work(struct work_struct *work)
{
	struct fimc_control *ctrl = container_of(work, struct fimc_control,
			work_struct);
	int ret, irq_cnt;

	irq_cnt = atomic_read(&ctrl->irq_cnt);
	if (irq_cnt > 0) {
		do {
			ret = atomic_dec_and_test((atomic_t *)&ctrl->irq_cnt);
			pm_runtime_put_sync(ctrl->dev);
		} while (ret != 1);
	}
}

int fimc_dma_alloc(struct fimc_control *ctrl, struct fimc_buf_set *bs,
							int i, int align)
{
	dma_addr_t end, *curr;

	mutex_lock(&ctrl->lock);

	end = ctrl->mem.base + ctrl->mem.size;
	curr = &ctrl->mem.curr;

	if (!bs->length[i]) {
		mutex_unlock(&ctrl->lock);
		return -EINVAL;
	}

	if (!align) {
		if (*curr + bs->length[i] > end) {
			goto overflow;
		} else {
			bs->base[i] = *curr;
			bs->garbage[i] = 0;
			*curr += bs->length[i];
		}
	} else {
		if (ALIGN(*curr, align) + bs->length[i] > end) {
			goto overflow;
		} else {
			bs->base[i] = ALIGN(*curr, align);
			bs->garbage[i] = ALIGN(*curr, align) - *curr;
			*curr += (bs->length[i] + bs->garbage[i]);
		}
	}

	mutex_unlock(&ctrl->lock);

	return 0;

overflow:
	bs->base[i] = 0;
	bs->length[i] = 0;
	bs->garbage[i] = 0;

	mutex_unlock(&ctrl->lock);

	return -ENOMEM;
}

void fimc_dma_free(struct fimc_control *ctrl, struct fimc_buf_set *bs, int i)
{
	int total = bs->length[i] + bs->garbage[i];
	mutex_lock(&ctrl->lock);

	if (bs->base[i]) {
		if (ctrl->mem.curr - total >= ctrl->mem.base)
			ctrl->mem.curr -= total;

		bs->base[i] = 0;
		bs->length[i] = 0;
		bs->garbage[i] = 0;
	}

	mutex_unlock(&ctrl->lock);
}

static inline u32 fimc_irq_out_single_buf(struct fimc_control *ctrl,
					  struct fimc_ctx *ctx)
{
	int ret = -1, ctx_num, next;
	u32 wakeup = 1;

	if (ctx->status == FIMC_READY_OFF || ctx->status == FIMC_STREAMOFF) {
		ctrl->out->idxs.active.ctx = -1;
		ctrl->out->idxs.active.idx = -1;
		ctx->status = FIMC_STREAMOFF;
		ctrl->status = FIMC_STREAMOFF;

		return wakeup;
	}
	ctx->status = FIMC_STREAMON_IDLE;

	/* Attach done buffer to outgoing queue. */
	ret = fimc_push_outq(ctrl, ctx, ctrl->out->idxs.active.idx);
	if (ret < 0)
		fimc_err("%s:Failed: fimc_push_outq\n", __func__);

	/* Detach buffer from incomming queue. */
	ret = fimc_pop_inq(ctrl, &ctx_num, &next);
	if (ret == 0) {		/* There is a buffer in incomming queue. */
		if (ctx_num != ctrl->out->last_ctx) {
			ctx = &ctrl->out->ctx[ctx_num];
			ctrl->out->last_ctx = ctx->ctx_num;
			fimc_outdev_set_ctx_param(ctrl, ctx);
		}

		fimc_outdev_set_src_addr(ctrl, ctx->src[next].base);
		ret = fimc_output_set_dst_addr(ctrl, ctx, next);
		if (ret < 0)
			fimc_err("%s:Fail: fimc_output_set_dst_addr\n", __func__);

		ctrl->out->idxs.active.ctx = ctx_num;
		ctrl->out->idxs.active.idx = next;

		ctx->status = FIMC_STREAMON;
		ctrl->status = FIMC_STREAMON;

		ret = fimc_outdev_start_camif(ctrl);
		if (ret < 0)
			fimc_err("%s:Fail: fimc_start_camif\n", __func__);

	} else {	/* There is no buffer in incomming queue. */
		ctrl->out->idxs.active.ctx = -1;
		ctrl->out->idxs.active.idx = -1;
		ctx->status = FIMC_STREAMON_IDLE;
		ctrl->status = FIMC_STREAMON_IDLE;
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
		ctrl->out->last_ctx = -1;
#endif
	}

	return wakeup;
}

static inline u32 fimc_irq_out_multi_buf(struct fimc_control *ctrl,
					 struct fimc_ctx *ctx)
{
	int ret = -1, ctx_num, next;
	u32 wakeup = 1;

	if (ctx->status == FIMC_READY_OFF) {
		if (ctrl->out->idxs.active.ctx == ctx->ctx_num) {
			ctrl->out->idxs.active.ctx = -1;
			ctrl->out->idxs.active.idx = -1;
		}

		ctx->status = FIMC_STREAMOFF;

		return wakeup;
	}
	ctx->status = FIMC_STREAMON_IDLE;

	/* Attach done buffer to outgoing queue. */
	ret = fimc_push_outq(ctrl, ctx, ctrl->out->idxs.active.idx);
	if (ret < 0)
		fimc_err("%s:Failed: fimc_push_outq\n", __func__);

	/* Detach buffer from incomming queue. */
	ret = fimc_pop_inq(ctrl, &ctx_num, &next);
	if (ret == 0) {		/* There is a buffer in incomming queue. */
		if (ctx_num != ctrl->out->last_ctx) {
			ctx = &ctrl->out->ctx[ctx_num];
			ctrl->out->last_ctx = ctx->ctx_num;
			fimc_outdev_set_ctx_param(ctrl, ctx);
		}

		fimc_outdev_set_src_addr(ctrl, ctx->src[next].base);
		ret = fimc_output_set_dst_addr(ctrl, ctx, next);
		if (ret < 0)
			fimc_err("%s:Fail: fimc_output_set_dst_addr\n", __func__);

		ctrl->out->idxs.active.ctx = ctx_num;
		ctrl->out->idxs.active.idx = next;

		ctx->status = FIMC_STREAMON;
		ctrl->status = FIMC_STREAMON;

		ret = fimc_outdev_start_camif(ctrl);
		if (ret < 0)
			fimc_err("%s:Fail: fimc_start_camif\n", __func__);

	} else {	/* There is no buffer in incomming queue. */
		ctrl->out->idxs.active.ctx = -1;
		ctrl->out->idxs.active.idx = -1;
		ctx->status = FIMC_STREAMON_IDLE;
		ctrl->status = FIMC_STREAMON_IDLE;
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
		ctrl->out->last_ctx = -1;
#endif
	}

	return wakeup;
}

static inline u32 fimc_irq_out_dma(struct fimc_control *ctrl,
				   struct fimc_ctx *ctx)
{
	struct fimc_buf_set buf_set;
	int idx = ctrl->out->idxs.active.idx;
	int ret = -1, i, ctx_num, next;
	int cfg;
	u32 wakeup = 1;

	if (ctx->status == FIMC_READY_OFF
			|| ctx->status == FIMC_STREAMOFF) {
		ctrl->out->idxs.active.ctx = -1;
		ctrl->out->idxs.active.idx = -1;
		ctx->status = FIMC_STREAMOFF;
		ctrl->status = FIMC_STREAMOFF;
		return wakeup;
	}

	/* Attach done buffer to outgoing queue. */
	ret = fimc_push_outq(ctrl, ctx, idx);
	if (ret < 0)
		fimc_err("Failed: fimc_push_outq\n");

	if (ctx->overlay.mode == FIMC_OVLY_DMA_AUTO) {
			ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_SET_WIN_ADDR,
				(unsigned long)ctx->dst[idx].base[FIMC_ADDR_Y]);

		if (ret < 0) {
			fimc_err("direct_ioctl(S3CFB_SET_WIN_ADDR) fail\n");
			return -EINVAL;
		}

		if (ctrl->fb.is_enable == 0) {
			ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_SET_WIN_ON,
							(unsigned long)NULL);
			if (ret < 0) {
				fimc_err("direct_ioctl(S3CFB_SET_WIN_ON)"\
						" fail\n");
				return -EINVAL;
			}

			ctrl->fb.is_enable = 1;
		}
	}

	/* Detach buffer from incomming queue. */
	ret = fimc_pop_inq(ctrl, &ctx_num, &next);
	if (ret == 0) {		/* There is a buffer in incomming queue. */
		ctx = &ctrl->out->ctx[ctx_num];
		fimc_outdev_set_src_addr(ctrl, ctx->src[next].base);

		memset(&buf_set, 0x00, sizeof(buf_set));
		buf_set.base[FIMC_ADDR_Y] = ctx->dst[next].base[FIMC_ADDR_Y];

		cfg = fimc_hwget_output_buf_sequence(ctrl);

		for (i = 0; i < FIMC_PHYBUFS; i++) {
			if (check_bit(cfg, i))
				fimc_hwset_output_address(ctrl, &buf_set, i);
		}

		ctrl->out->idxs.active.ctx = ctx_num;
		ctrl->out->idxs.active.idx = next;

		ctx->status = FIMC_STREAMON;
		ctrl->status = FIMC_STREAMON;

		ret = fimc_outdev_start_camif(ctrl);
		if (ret < 0)
			fimc_err("Fail: fimc_start_camif\n");

	} else {		/* There is no buffer in incomming queue. */
		ctrl->out->idxs.active.ctx = -1;
		ctrl->out->idxs.active.idx = -1;

		ctx->status = FIMC_STREAMON_IDLE;
		ctrl->status = FIMC_STREAMON_IDLE;
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
		ctrl->out->last_ctx = -1;
#endif
	}

	return wakeup;
}

static inline u32 fimc_irq_out_fimd(struct fimc_control *ctrl,
				    struct fimc_ctx *ctx)
{
	struct fimc_idx prev;
	int ret = -1, ctx_num, next;
	u32 wakeup = 0;

	/* Attach done buffer to outgoing queue. */
	if (ctrl->out->idxs.prev.idx != -1) {
		ret = fimc_push_outq(ctrl, ctx, ctrl->out->idxs.prev.idx);
		if (ret < 0) {
			fimc_err("Failed: fimc_push_outq\n");
		} else {
			ctrl->out->idxs.prev.ctx = -1;
			ctrl->out->idxs.prev.idx = -1;
			wakeup = 1;	/* To wake up fimc_v4l2_dqbuf */
		}
	}

	/* Update index structure. */
	if (ctrl->out->idxs.next.idx != -1) {
		ctrl->out->idxs.active.ctx = ctrl->out->idxs.next.ctx;
		ctrl->out->idxs.active.idx = ctrl->out->idxs.next.idx;
		ctrl->out->idxs.next.idx = -1;
		ctrl->out->idxs.next.ctx = -1;
	}

	/* Detach buffer from incomming queue. */
	ret = fimc_pop_inq(ctrl, &ctx_num, &next);
	if (ret == 0) { /* There is a buffer in incomming queue. */
		prev.ctx = ctrl->out->idxs.active.ctx;
		prev.idx = ctrl->out->idxs.active.idx;

		ctrl->out->idxs.prev.ctx = prev.ctx;
		ctrl->out->idxs.prev.idx = prev.idx;

		ctrl->out->idxs.next.ctx = ctx_num;
		ctrl->out->idxs.next.idx = next;

		/* set source address */
		fimc_outdev_set_src_addr(ctrl, ctx->src[next].base);
	}

	return wakeup;
}

static inline void fimc_irq_out(struct fimc_control *ctrl)
{
	struct fimc_ctx *ctx;
	u32 wakeup = 1;
	int ctx_num = ctrl->out->idxs.active.ctx;

	/* Interrupt pendding clear */
	fimc_hwset_clear_irq(ctrl);

	/* check context num */
	if (ctx_num < 0 || ctx_num >= FIMC_MAX_CTXS) {
		fimc_err("fimc_irq_out: invalid ctx (ctx=%d)\n", ctx_num);
		wake_up(&ctrl->wq);
		return;
	}

	ctx = &ctrl->out->ctx[ctx_num];

	switch (ctx->overlay.mode) {
	case FIMC_OVLY_NONE_SINGLE_BUF:
		wakeup = fimc_irq_out_single_buf(ctrl, ctx);
		break;
	case FIMC_OVLY_NONE_MULTI_BUF:
		wakeup = fimc_irq_out_multi_buf(ctrl, ctx);
		break;
	case FIMC_OVLY_DMA_AUTO:	/* fall through */
	case FIMC_OVLY_DMA_MANUAL:
		wakeup = fimc_irq_out_dma(ctrl, ctx);
		break;
	case FIMC_OVLY_FIFO:
		if (ctx->status != FIMC_READY_OFF)
			wakeup = fimc_irq_out_fimd(ctrl, ctx);
		break;
	default:
		fimc_err("[ctx=%d] fimc_irq_out: wrong overlay.mode (%d)\n",
				ctx_num, ctx->overlay.mode);
		break;
	}

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	atomic_inc((atomic_t *)&ctrl->irq_cnt);
	queue_work(ctrl->fimc_irq_wq, &ctrl->work_struct);
#endif

	if (wakeup == 1)
		wake_up(&ctrl->wq);
}

int fimc_hwget_number_of_bits(u32 framecnt_seq)
{
	u32 bits = 0;
	while (framecnt_seq) {
		framecnt_seq = framecnt_seq & (framecnt_seq - 1);
		bits++;
	}
	return bits;
}

static int fimc_add_outgoing_queue(struct fimc_control *ctrl, int i)
{
	struct fimc_capinfo *cap = ctrl->cap;
	struct fimc_buf_set *tmp_buf;
	struct list_head *count;

	spin_lock(&ctrl->outq_lock);

	list_for_each(count, &cap->outgoing_q) {
		tmp_buf = list_entry(count, struct fimc_buf_set, list);
		if (tmp_buf->id == i) {
			fimc_info1("%s: Exist id in outqueue\n", __func__);

			spin_unlock(&ctrl->outq_lock);
			return 0;
		}
	}
	list_add_tail(&cap->bufs[i].list, &cap->outgoing_q);
	spin_unlock(&ctrl->outq_lock);

	return 0;
}

static inline void fimc_irq_cap(struct fimc_control *ctrl)
{
	struct fimc_capinfo *cap = ctrl->cap;
	int pp;
	int buf_index;
	int framecnt_seq;
	int available_bufnum;

	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
#ifdef DEBUG
	static struct timeval curr_time, before_time;
	if (ctrl->id == FIMC2) {
		do_gettimeofday(&curr_time);
		printk(KERN_INFO "%s : time : %ld\n", __func__,
				curr_time.tv_usec - before_time.tv_usec);
		before_time.tv_usec = curr_time.tv_usec;
	}
#endif
	fimc_hwset_clear_irq(ctrl);
	if (fimc_hwget_overflow_state(ctrl))
		return;

	if (pdata->hw_ver >= 0x51) {
		pp = fimc_hwget_before_frame_count(ctrl);
		if (cap->cnt < 20) {
			printk(KERN_INFO "%s[%d], fimc%d, cnt[%d]\n", __func__,
							pp, ctrl->id, cap->cnt);
			cap->cnt++;
		}
		if (pp == 0 || cap->cnt == 1) {
			if (ctrl->cap->nr_bufs == 1)
				pp = fimc_hwget_present_frame_count(ctrl);
			else
				return;
		}
		buf_index = pp - 1;
		fimc_add_outgoing_queue(ctrl, buf_index);
		fimc_hwset_output_buf_sequence(ctrl, buf_index,
				FIMC_FRAMECNT_SEQ_DISABLE);

		framecnt_seq = fimc_hwget_output_buf_sequence(ctrl);
		available_bufnum = fimc_hwget_number_of_bits(framecnt_seq);
		fimc_info2("%s[%d] : framecnt_seq: %d, available_bufnum: %d\n",
			__func__, ctrl->id, framecnt_seq, available_bufnum);

		if (ctrl->status != FIMC_BUFFER_STOP) {
			if (available_bufnum == 1 || ctrl->cap->nr_bufs == 1) {
				cap->cnt=0;
				ctrl->cap->lastirq = 0;
				fimc_stop_capture(ctrl);
				ctrl->status = FIMC_BUFFER_STOP;
				printk(KERN_INFO "fimc_irq_cap[%d] available_bufnum = %d\n",
					ctrl->id, available_bufnum);
			}
		} else {
			fimc_info1("%s : Aleady fimc stop\n", __func__);
		}
	} else
		pp = ((fimc_hwget_frame_count(ctrl) + 2) % 4);

	if (cap->fmt.field == V4L2_FIELD_INTERLACED_TB) {
		/* odd value of pp means one frame is made with top/bottom */
		if (pp & 0x1) {
			cap->irq = 1;
			wake_up(&ctrl->wq);
		}
	} else {
		cap->irq = 1;
		wake_up(&ctrl->wq);
	}
}

static irqreturn_t fimc_irq(int irq, void *dev_id)
{
	struct fimc_control *ctrl = (struct fimc_control *) dev_id;
	struct s3c_platform_fimc *pdata;

	if (ctrl->cap)
		fimc_irq_cap(ctrl);
	else if (ctrl->out)
		fimc_irq_out(ctrl);
	else {
		printk(KERN_ERR "%s this message must not be shown!!!"
				" fimc%d\n", __func__, ctrl->id);
		pdata = to_fimc_plat(ctrl->dev);
		pdata->clk_on(to_platform_device(ctrl->dev),
					&ctrl->clk);
		fimc_hwset_clear_irq(ctrl);
		pdata->clk_off(to_platform_device(ctrl->dev),
					&ctrl->clk);
	}

	return IRQ_HANDLED;
}

static
struct fimc_control *fimc_register_controller(struct platform_device *pdev)
{
	struct s3c_platform_fimc *pdata;
	struct fimc_control *ctrl;
	struct resource *res;
	int id, err;
	struct cma_info mem_info;
	struct clk *sclk_fimc_lclk = NULL;
	struct clk *fimc_src_clk = NULL;

	id = pdev->id;
	pdata = to_fimc_plat(&pdev->dev);

	ctrl = get_fimc_ctrl(id);
	ctrl->id = id;
	ctrl->dev = &pdev->dev;
	ctrl->vd = &fimc_video_device[id];
	ctrl->vd->minor = id;
	ctrl->log = FIMC_LOG_DEFAULT;
	ctrl->power_status = FIMC_POWER_OFF;

	/* CMA */
#ifdef CONFIG_ION_EXYNOS
	if  (id != 2) {
#endif
		sprintf(ctrl->cma_name, "%s%d", FIMC_CMA_NAME, ctrl->id);
	err = cma_info(&mem_info, ctrl->dev, 0);
	fimc_info1("%s : [cma_info] start_addr : 0x%x, end_addr : 0x%x, "
			"total_size : 0x%x, free_size : 0x%x\n",
			__func__, mem_info.lower_bound, mem_info.upper_bound,
			mem_info.total_size, mem_info.free_size);
	if (err) {
		fimc_err("%s: get cma info failed\n", __func__);
		ctrl->mem.size = 0;
		ctrl->mem.base = 0;
	} else {
		ctrl->mem.size = mem_info.total_size;
		ctrl->mem.base = (dma_addr_t)cma_alloc
			(ctrl->dev, ctrl->cma_name, (size_t)ctrl->mem.size, 0);
	}
#ifdef CONFIG_ION_EXYNOS
	}
#endif
	printk(KERN_INFO "ctrl->mem.size = 0x%x\n", ctrl->mem.size);
	printk(KERN_INFO "ctrl->mem.base = 0x%x\n", ctrl->mem.base);

	ctrl->mem.curr = ctrl->mem.base;
	ctrl->status = FIMC_STREAMOFF;

	switch (pdata->hw_ver) {
	case 0x40:
		ctrl->limit = &fimc40_limits[id];
		break;
	case 0x43:
	case 0x45:
		ctrl->limit = &fimc43_limits[id];
		break;
	case 0x50:
		ctrl->limit = &fimc50_limits[id];
		break;
	case 0x51:
		ctrl->limit = &fimc51_limits[id];
		break;
	}

	sprintf(ctrl->name, "%s%d", FIMC_NAME, id);
	strcpy(ctrl->vd->name, ctrl->name);

	atomic_set(&ctrl->in_use, 0);
	mutex_init(&ctrl->lock);
	mutex_init(&ctrl->v4l2_lock);
	spin_lock_init(&ctrl->outq_lock);
	init_waitqueue_head(&ctrl->wq);

	/* get resource for io memory */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		fimc_err("%s: failed to get io memory region\n", __func__);
		return NULL;
	}

	/* request mem region */
	res = request_mem_region(res->start, res->end - res->start + 1,
			pdev->name);
	if (!res) {
		fimc_err("%s: failed to request io memory region\n", __func__);
		return NULL;
	}

	/* ioremap for register block */
	ctrl->regs = ioremap(res->start, res->end - res->start + 1);
	if (!ctrl->regs) {
		fimc_err("%s: failed to remap io region\n", __func__);
		return NULL;
	}

	fimc_dev->backup_regs[id] = ctrl->regs;
	/* irq */
	ctrl->irq = platform_get_irq(pdev, 0);
	if (request_irq(ctrl->irq, fimc_irq, IRQF_DISABLED, ctrl->name, ctrl))
		fimc_err("%s: request_irq failed\n", __func__);

	if (soc_is_exynos4210())
		fimc_src_clk = clk_get(&pdev->dev, "mout_mpll");
	else
		fimc_src_clk = clk_get(&pdev->dev, "mout_mpll_user");

	if (IS_ERR(fimc_src_clk)) {
		dev_err(&pdev->dev, "failed to get parent clock\n");
		iounmap(ctrl->regs);
		return NULL;
	}

	sclk_fimc_lclk = clk_get(&pdev->dev, FIMC_CORE_CLK);
	if (IS_ERR(sclk_fimc_lclk)) {
		dev_err(&pdev->dev, "failed to get sclk_fimc_lclk\n");
		iounmap(ctrl->regs);
		clk_put(fimc_src_clk);
		return NULL;
	}

	if (clk_set_parent(sclk_fimc_lclk, fimc_src_clk)) {
		dev_err(&pdev->dev, "unable to set parent %s of clock %s.\n",
				fimc_src_clk->name, sclk_fimc_lclk->name);
		iounmap(ctrl->regs);
		clk_put(sclk_fimc_lclk);
		clk_put(fimc_src_clk);
		return NULL;
	}
	clk_set_rate(sclk_fimc_lclk, fimc_clk_rate());
	clk_put(sclk_fimc_lclk);
	clk_put(fimc_src_clk);

#if (!defined(CONFIG_EXYNOS_DEV_PD) || !defined(CONFIG_PM_RUNTIME))
	fimc_hwset_reset(ctrl);
#endif

	return ctrl;
}

static int fimc_unregister_controller(struct platform_device *pdev)
{
	struct s3c_platform_fimc *pdata;
	struct fimc_control *ctrl;
	int id = pdev->id;

	pdata = to_fimc_plat(&pdev->dev);
	ctrl = get_fimc_ctrl(id);

	free_irq(ctrl->irq, ctrl);
	mutex_destroy(&ctrl->lock);
	mutex_destroy(&ctrl->v4l2_lock);

	if (pdata->clk_off)
		pdata->clk_off(pdev, &ctrl->clk);

	iounmap(ctrl->regs);
	memset(ctrl, 0, sizeof(*ctrl));

	return 0;
}

static void fimc_mmap_open(struct vm_area_struct *vma)
{
	struct fimc_global *dev = fimc_dev;
	int pri_data    = (int)vma->vm_private_data;
	u32 id          = pri_data / 0x100;
	u32 ctx         = (pri_data - (id * 0x100)) / 0x10;
	u32 idx         = pri_data % 0x10;

	BUG_ON(id >= FIMC_DEVICES);
	BUG_ON(ctx >= FIMC_MAX_CTXS);
	BUG_ON(idx >= FIMC_OUTBUFS);

	atomic_inc(&dev->ctrl[id].out->ctx[ctx].src[idx].mapped_cnt);
}

static void fimc_mmap_close(struct vm_area_struct *vma)
{
	struct fimc_global *dev = fimc_dev;
	int pri_data	= (int)vma->vm_private_data;
	u32 id		= pri_data / 0x100;
	u32 ctx		= (pri_data - (id * 0x100)) / 0x10;
	u32 idx		= pri_data % 0x10;

	BUG_ON(id >= FIMC_DEVICES);
	BUG_ON(ctx >= FIMC_MAX_CTXS);
	BUG_ON(idx >= FIMC_OUTBUFS);

	atomic_dec(&dev->ctrl[id].out->ctx[ctx].src[idx].mapped_cnt);
}

static struct vm_operations_struct fimc_mmap_ops = {
	.open	= fimc_mmap_open,
	.close	= fimc_mmap_close,
};

static inline
int fimc_mmap_out_src(struct file *filp, struct vm_area_struct *vma)
{
	struct fimc_prv_data *prv_data =
				(struct fimc_prv_data *)filp->private_data;
	struct fimc_control *ctrl = prv_data->ctrl;
	int ctx_id = prv_data->ctx_id;
	struct fimc_ctx *ctx = &ctrl->out->ctx[ctx_id];
	u32 start_phy_addr = 0;
	u32 size = vma->vm_end - vma->vm_start;
	u32 pfn, idx = vma->vm_pgoff;
	u32 buf_length = 0;
	int pri_data = 0;

	buf_length = PAGE_ALIGN(ctx->src[idx].length[FIMC_ADDR_Y] +
				ctx->src[idx].length[FIMC_ADDR_CB] +
				ctx->src[idx].length[FIMC_ADDR_CR]);
	if (size > PAGE_ALIGN(buf_length)) {
		fimc_err("Requested mmap size is too big\n");
		return -EINVAL;
	}

	pri_data = (ctrl->id * 0x100) + (ctx_id * 0x10) + idx;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &fimc_mmap_ops;
	vma->vm_private_data = (void *)pri_data;

	if ((vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_SHARED)) {
		fimc_err("writable mapping must be shared\n");
		return -EINVAL;
	}

	start_phy_addr = ctx->src[idx].base[FIMC_ADDR_Y];
	pfn = __phys_to_pfn(start_phy_addr);

	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
		fimc_err("mmap fail\n");
		return -EINVAL;
	}

	vma->vm_ops->open(vma);

	ctx->src[idx].flags |= V4L2_BUF_FLAG_MAPPED;

	return 0;
}

static inline
int fimc_mmap_out_dst(struct file *filp, struct vm_area_struct *vma, u32 idx)
{
	struct fimc_prv_data *prv_data =
				(struct fimc_prv_data *)filp->private_data;
	struct fimc_control *ctrl = prv_data->ctrl;
	int ctx_id = prv_data->ctx_id;
	unsigned long pfn = 0, size;
	int ret = 0;

	size = vma->vm_end - vma->vm_start;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_RESERVED;

	if (ctrl->out->ctx[ctx_id].dst[idx].base[0])
		pfn = __phys_to_pfn(ctrl->out->ctx[ctx_id].dst[idx].base[0]);
	else
		pfn = __phys_to_pfn(ctrl->mem.curr);

	ret = remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
	if (ret != 0)
		fimc_err("remap_pfn_range fail.\n");

	return ret;
}

static inline int fimc_mmap_out(struct file *filp, struct vm_area_struct *vma)
{
	struct fimc_prv_data *prv_data =
				(struct fimc_prv_data *)filp->private_data;
	struct fimc_control *ctrl = prv_data->ctrl;
	int ctx_id = prv_data->ctx_id;

	int idx = ctrl->out->ctx[ctx_id].overlay.req_idx;
	int ret = -1;

	if (idx >= 0)
		ret = fimc_mmap_out_dst(filp, vma, idx);
	else if (idx == FIMC_MMAP_IDX)
		ret = fimc_mmap_out_src(filp, vma);

	return ret;
}

static inline int fimc_mmap_cap(struct file *filp, struct vm_area_struct *vma)
{
	struct fimc_prv_data *prv_data =
				(struct fimc_prv_data *)filp->private_data;
	struct fimc_control *ctrl = prv_data->ctrl;
	u32 size = vma->vm_end - vma->vm_start;
	u32 pfn, idx = vma->vm_pgoff;

	if (ctrl->cap->fmt.priv != V4L2_PIX_FMT_MODE_HDR && !ctrl->cap->cacheable)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_RESERVED;

	/*
	 * page frame number of the address for a source frame
	 * to be stored at.
	 */
	pfn = __phys_to_pfn(ctrl->cap->bufs[idx].base[0]);

	if ((vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_SHARED)) {
		fimc_err("%s: writable mapping must be shared\n", __func__);
		return -EINVAL;
	}

	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
		fimc_err("%s: mmap fail\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int fimc_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct fimc_prv_data *prv_data =
				(struct fimc_prv_data *)filp->private_data;
	struct fimc_control *ctrl = prv_data->ctrl;
	int ret;

	if (ctrl->cap)
		ret = fimc_mmap_cap(filp, vma);
	else
		ret = fimc_mmap_out(filp, vma);

	return ret;
}

static u32 fimc_poll(struct file *filp, poll_table *wait)
{
	struct fimc_prv_data *prv_data =
		(struct fimc_prv_data *)filp->private_data;
	struct fimc_control *ctrl = prv_data->ctrl;
	struct fimc_capinfo *cap = ctrl->cap;
	u32 mask = 0;

	if (!cap)
		return 0;

	if (!list_empty(&cap->outgoing_q)) {
		mask = POLLIN | POLLRDNORM;
		cap->poll_cnt = 0;
	} else {
		poll_wait(filp, &ctrl->wq, wait);
		if (++cap->poll_cnt > 15)
			fimc_hwset_sw_reset(ctrl);
	}

	return mask;
}

static
ssize_t fimc_read(struct file *filp, char *buf, size_t count, loff_t *pos)
{
	return 0;
}

static
ssize_t fimc_write(struct file *filp, const char *b, size_t c, loff_t *offset)
{
	return 0;
}

u32 fimc_mapping_rot_flip(u32 rot, u32 flip)
{
	u32 ret = 0;

	switch (rot) {
	case 0:
		if (flip & FIMC_XFLIP)
			ret |= FIMC_XFLIP;

		if (flip & FIMC_YFLIP)
			ret |= FIMC_YFLIP;
		break;

	case 90:
		ret = FIMC_ROT;
		if (flip & FIMC_XFLIP)
			ret |= FIMC_XFLIP;

		if (flip & FIMC_YFLIP)
			ret |= FIMC_YFLIP;
		break;

	case 180:
		ret = (FIMC_XFLIP | FIMC_YFLIP);
		if (flip & FIMC_XFLIP)
			ret &= ~FIMC_XFLIP;

		if (flip & FIMC_YFLIP)
			ret &= ~FIMC_YFLIP;
		break;

	case 270:
		ret = (FIMC_XFLIP | FIMC_YFLIP | FIMC_ROT);
		if (flip & FIMC_XFLIP)
			ret &= ~FIMC_XFLIP;

		if (flip & FIMC_YFLIP)
			ret &= ~FIMC_YFLIP;
		break;
	}

	return ret;
}

int fimc_get_scaler_factor(u32 src, u32 tar, u32 *ratio, u32 *shift)
{
	if (src >= tar * 64) {
		return -EINVAL;
	} else if (src >= tar * 32) {
		*ratio = 32;
		*shift = 5;
	} else if (src >= tar * 16) {
		*ratio = 16;
		*shift = 4;
	} else if (src >= tar * 8) {
		*ratio = 8;
		*shift = 3;
	} else if (src >= tar * 4) {
		*ratio = 4;
		*shift = 2;
	} else if (src >= tar * 2) {
		*ratio = 2;
		*shift = 1;
	} else {
		*ratio = 1;
		*shift = 0;
	}

	return 0;
}

void fimc_get_nv12t_size(int img_hres, int img_vres,
				int *y_size, int *cb_size)
{
	int remain;
	int y_hres_byte, y_vres_byte;
	int cb_hres_byte, cb_vres_byte;
	int y_hres_roundup, y_vres_roundup;
	int cb_hres_roundup, cb_vres_roundup;

	/* to make 'img_hres and img_vres' be 16 multiple */
	remain = img_hres % 16;
	if (remain != 0) {
		remain = 16 - remain;
		img_hres = img_hres + remain;
	}
	remain = img_vres % 16;
	if (remain != 0) {
		remain = 16 - remain;
		img_vres = img_vres + remain;
	}

	cb_hres_byte = img_hres;
	cb_vres_byte = img_vres;

	y_hres_byte = img_hres - 1;
	y_vres_byte = img_vres - 1;
	y_hres_roundup = ((y_hres_byte >> 4) >> 3) + 1;
	y_vres_roundup = ((y_vres_byte >> 4) >> 2) + 1;
	if ((y_vres_byte & 0x20) == 0) {
		y_hres_byte = y_hres_byte & 0x7f00;
		y_hres_byte = y_hres_byte >> 8;
		y_hres_byte = y_hres_byte & 0x7f;

		y_vres_byte = y_vres_byte & 0x7fc0;
		y_vres_byte = y_vres_byte >> 6;
		y_vres_byte = y_vres_byte & 0x1ff;

		*y_size = y_hres_byte +\
		(y_vres_byte * y_hres_roundup) + 1;
	} else {
		*y_size = y_hres_roundup * y_vres_roundup;
	}

	*y_size = *(y_size) << 13;

	cb_hres_byte = img_hres - 1;
	cb_vres_byte = (img_vres >> 1) - 1;
	cb_hres_roundup = ((cb_hres_byte >> 4) >> 3) + 1;
	cb_vres_roundup = ((cb_vres_byte >> 4) >> 2) + 1;
	if ((cb_vres_byte & 0x20) == 0) {
		cb_hres_byte = cb_hres_byte & 0x7f00;
		cb_hres_byte = cb_hres_byte >> 8;
		cb_hres_byte = cb_hres_byte & 0x7f;

		cb_vres_byte = cb_vres_byte & 0x7fc0;
		cb_vres_byte = cb_vres_byte >> 6;
		cb_vres_byte = cb_vres_byte & 0x1ff;

		*cb_size = cb_hres_byte + (cb_vres_byte * cb_hres_roundup) + 1;
	} else {
		*cb_size = cb_hres_roundup * cb_vres_roundup;
	}
	*cb_size = (*cb_size) << 13;

}

static int fimc_open(struct file *filp)
{
	struct fimc_control *ctrl;
	struct s3c_platform_fimc *pdata;
	struct fimc_prv_data *prv_data;
	int in_use;
	int ret;
	int i;

	ctrl = video_get_drvdata(video_devdata(filp));
	pdata = to_fimc_plat(ctrl->dev);

	mutex_lock(&ctrl->lock);

	in_use = atomic_read(&ctrl->in_use);
	if (in_use > FIMC_MAX_CTXS) {
		ret = -EBUSY;
		goto resource_busy;
	} else {
		atomic_inc(&ctrl->in_use);
		fimc_warn("FIMC%d %d opened.\n",
			 ctrl->id, atomic_read(&ctrl->in_use));
	}
	in_use = atomic_read(&ctrl->in_use);

	prv_data = kzalloc(sizeof(struct fimc_prv_data), GFP_KERNEL);
	if (!prv_data) {
		fimc_err("%s: not enough memory\n", __func__);
		ret = -ENOMEM;
		goto kzalloc_err;
	}

	if (in_use == 1) {
#if (!defined(CONFIG_EXYNOS_DEV_PD) || !defined(CONFIG_PM_RUNTIME))
		if (pdata->clk_on)
			pdata->clk_on(to_platform_device(ctrl->dev),
					&ctrl->clk);

		if (pdata->hw_ver == 0x40)
			fimc_hw_reset_camera(ctrl);

		/* Apply things to interface register */
		fimc_hwset_reset(ctrl);
#endif
		ctrl->fb.open_fifo = s3cfb_open_fifo;
		ctrl->fb.close_fifo = s3cfb_close_fifo;

		ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_GET_LCD_WIDTH,
					(unsigned long)&ctrl->fb.lcd_hres);
		if (ret < 0) {
			fimc_err("Fail: S3CFB_GET_LCD_WIDTH\n");
			goto resource_busy;
		}

		ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_GET_LCD_HEIGHT,
					(unsigned long)&ctrl->fb.lcd_vres);
		if (ret < 0) {
			fimc_err("Fail: S3CFB_GET_LCD_HEIGHT\n");
			goto resource_busy;
		}

		ctrl->mem.curr = ctrl->mem.base;
		ctrl->status = FIMC_STREAMOFF;

	}

	prv_data->ctrl = ctrl;
	if (prv_data->ctrl->out != NULL) {
		for (i = 0; i < FIMC_MAX_CTXS; i++)
			if (prv_data->ctrl->out->ctx_used[i] == false) {
				prv_data->ctx_id = i;
				prv_data->ctrl->out->ctx_used[i] = true;
				break;
			}
	} else
		prv_data->ctx_id = in_use - 1;

	filp->private_data = prv_data;

	mutex_unlock(&ctrl->lock);

	return 0;

kzalloc_err:
	atomic_dec(&ctrl->in_use);

resource_busy:
	mutex_unlock(&ctrl->lock);
	return ret;
}

static int fimc_release(struct file *filp)
{
	struct fimc_prv_data *prv_data =
				(struct fimc_prv_data *)filp->private_data;
	struct fimc_control *ctrl = prv_data->ctrl;
	struct fimc_capinfo *cap;

	int ctx_id = prv_data->ctx_id;
	struct s3c_platform_fimc *pdata;
	struct fimc_overlay_buf *buf;
	struct mm_struct *mm = current->mm;
	struct fimc_ctx *ctx;
	int ret = 0, i;
	ctx = &ctrl->out->ctx[ctx_id];

	pdata = to_fimc_plat(ctrl->dev);

	atomic_dec(&ctrl->in_use);

	if (ctrl->cap && (ctrl->status != FIMC_STREAMOFF))
		fimc_streamoff_capture((void *)ctrl);

	/* FIXME: turning off actual working camera */
	if (ctrl->cam && ctrl->id != FIMC2) {
		/* Unload the subdev (camera sensor) module,
		 * reset related status flags */
		fimc_release_subdev(ctrl);
	}

	if (atomic_read(&ctrl->in_use) == 0) {
#if (!defined(CONFIG_EXYNOS_DEV_PD) || !defined(CONFIG_PM_RUNTIME))
		if (pdata->clk_off) {
			pdata->clk_off(to_platform_device(ctrl->dev),
					&ctrl->clk);
			ctrl->power_status = FIMC_POWER_OFF;
		}
#endif

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
/* #ifdef SYSMMU_FIMC */
		if (ctrl->power_status == FIMC_POWER_ON) {
			pm_runtime_put_sync(ctrl->dev);
		}
/* #endif */
#endif

	}
	if (ctrl->out) {
		if (ctx->status != FIMC_STREAMOFF) {
			ret = fimc_outdev_stop_streaming(ctrl, ctx);
			if (ret < 0) {
				fimc_err("Fail: fimc_stop_streaming\n");
				return -EINVAL;
			}

			ret = fimc_init_in_queue(ctrl, ctx);
			if (ret < 0) {
				fimc_err("Fail: fimc_init_in_queue\n");
				return -EINVAL;
			}

			ret = fimc_init_out_queue(ctrl, ctx);
			if (ret < 0) {
				fimc_err("Fail: fimc_init_out_queue\n");
				return -EINVAL;
			}

			/* Make all buffers DQUEUED state. */
			for (i = 0; i < FIMC_OUTBUFS; i++) {
				ctx->src[i].state = VIDEOBUF_IDLE;
				ctx->src[i].flags = V4L2_BUF_FLAG_MAPPED;
			}

			if (ctx->overlay.mode == FIMC_OVLY_DMA_AUTO) {
				ctrl->mem.curr = ctx->dst[0].base[FIMC_ADDR_Y];

				for (i = 0; i < FIMC_OUTBUFS; i++) {
					ctx->dst[i].base[FIMC_ADDR_Y] = 0;
					ctx->dst[i].length[FIMC_ADDR_Y] = 0;

					ctx->dst[i].base[FIMC_ADDR_CB] = 0;
					ctx->dst[i].length[FIMC_ADDR_CB] = 0;

					ctx->dst[i].base[FIMC_ADDR_CR] = 0;
					ctx->dst[i].length[FIMC_ADDR_CR] = 0;
				}
			}

			ctx->status = FIMC_STREAMOFF;
		}

		ctx->is_requested = 0;
		buf = &ctx->overlay.buf;
		for (i = 0; i < FIMC_OUTBUFS; i++) {
			if (buf->vir_addr[i]) {
				ret = do_munmap(mm, buf->vir_addr[i],
						buf->size[i]);
				if (ret < 0)
					fimc_err("%s: do_munmap fail\n",
							__func__);
			}
		}

		/* reset inq & outq of context */
		for (i = 0; i < FIMC_OUTBUFS; i++) {
			ctx->inq[i] = -1;
			ctx->outq[i] = -1;
		}

		if (atomic_read(&ctrl->in_use) == 0) {
			ctrl->status = FIMC_STREAMOFF;
			fimc_outdev_init_idxs(ctrl);

			ctrl->mem.curr = ctrl->mem.base;

			kfree(ctrl->out);
			ctrl->out = NULL;

			kfree(filp->private_data);
			filp->private_data = NULL;
		} else {
			ctrl->out->ctx_used[ctx_id] = false;
		}
	}

	if (ctrl->cap) {
		cap = ctrl->cap;
		ctrl->mem.curr = ctrl->mem.base;
		kfree(filp->private_data);
		filp->private_data = NULL;
		if (pdata->hw_ver >= 0x51)
			INIT_LIST_HEAD(&cap->outgoing_q);
		for (i = 0; i < FIMC_CAPBUFS; i++) {
			fimc_dma_free(ctrl, &ctrl->cap->bufs[i], 0);
			fimc_dma_free(ctrl, &ctrl->cap->bufs[i], 1);
			fimc_dma_free(ctrl, &ctrl->cap->bufs[i], 2);
		}
		kfree(ctrl->cap);
		ctrl->cap = NULL;
	}

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	flush_workqueue(ctrl->fimc_irq_wq);
#endif

	/*
	 * Close window for FIMC if window is enabled.
	 */
	if (ctrl->fb.is_enable == 1) {
		fimc_warn("WIN_OFF for FIMC%d\n", ctrl->id);
		ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_SET_WIN_OFF,
						(unsigned long)NULL);
		if (ret < 0) {
			fimc_err("direct_ioctl(S3CFB_SET_WIN_OFF) fail\n");
			return -EINVAL;
		}

		ctrl->fb.is_enable = 0;
	}

	fimc_warn("FIMC%d %d released.\n",
			ctrl->id, atomic_read(&ctrl->in_use));

	return 0;
}

static const struct v4l2_file_operations fimc_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_open,
	.release	= fimc_release,
	.ioctl		= video_ioctl2,
	.read		= fimc_read,
	.write		= fimc_write,
	.mmap		= fimc_mmap,
	.poll		= fimc_poll,
};

static void fimc_vdev_release(struct video_device *vdev)
{
	kfree(vdev);
}

struct video_device fimc_video_device[FIMC_DEVICES] = {
	[0] = {
		.fops = &fimc_fops,
		.ioctl_ops = &fimc_v4l2_ops,
		.release = fimc_vdev_release,
	},
	[1] = {
		.fops = &fimc_fops,
		.ioctl_ops = &fimc_v4l2_ops,
		.release = fimc_vdev_release,
	},
	[2] = {
		.fops = &fimc_fops,
		.ioctl_ops = &fimc_v4l2_ops,
		.release = fimc_vdev_release,
	},
#ifdef CONFIG_ARCH_EXYNOS4
	[3] = {
		.fops = &fimc_fops,
		.ioctl_ops = &fimc_v4l2_ops,
		.release = fimc_vdev_release,
	},
#endif
};

static int fimc_init_global(struct platform_device *pdev)
{
	struct fimc_control *ctrl;
	struct s3c_platform_fimc *pdata;
	struct s3c_platform_camera *cam;
	struct clk *srclk;
	int id, i;

	pdata = to_fimc_plat(&pdev->dev);
	id = pdev->id;
	ctrl = get_fimc_ctrl(id);

	/* Registering external camera modules. re-arrange order to be sure */
	for (i = 0; i < FIMC_MAXCAMS; i++) {
		cam = pdata->camera[i];
		if (!cam)
			break;
		/* WriteBack doesn't need clock setting */
		if ((cam->id == CAMERA_WB) || (cam->id == CAMERA_WB_B)) {
			fimc_dev->camera[i] = cam;
			fimc_dev->camera_isvalid[i] = 1;
			fimc_dev->camera[i]->initialized = 0;
			continue;
		}

		/* source clk for MCLK*/
		srclk = clk_get(&pdev->dev, cam->srclk_name);
		if (IS_ERR(srclk)) {
			fimc_err("%s: failed to get srclk source\n", __func__);
			return -EINVAL;
		}

		/* mclk */
		cam->clk = clk_get(&pdev->dev, cam->clk_name);
		if (IS_ERR(cam->clk)) {
			fimc_err("%s: failed to get mclk source\n", __func__);
			return -EINVAL;
		}

		if (clk_set_parent(cam->clk, srclk)) {
			dev_err(&pdev->dev, "unable to set parent %s of clock %s.\n",
					srclk->name, cam->clk->name);
			clk_put(srclk);
			clk_put(cam->clk);
			return -EINVAL;
		}

		/* Assign camera device to fimc */
		fimc_dev->camera[i] = cam;
		fimc_dev->camera_isvalid[i] = 1;
		fimc_dev->camera[i]->initialized = 0;
	}

	fimc_dev->mclk_status = CAM_MCLK_OFF;
	fimc_dev->active_camera = -1;
	fimc_dev->initialized = 1;

	return 0;
}

#ifdef CONFIG_DRM_EXYNOS_FIMD_WB
static BLOCKING_NOTIFIER_HEAD(fimc_notifier_client_list);

int fimc_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(
			&fimc_notifier_client_list, nb);
}
EXPORT_SYMBOL(fimc_register_client);

int fimc_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(
			&fimc_notifier_client_list, nb);
}
EXPORT_SYMBOL(fimc_unregister_client);

int fimc_send_event(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(
			&fimc_notifier_client_list, val, v);
}
#endif

static int fimc_show_log_level(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fimc_control *ctrl;
	struct platform_device *pdev;
	int id = -1;

	char temp[150];

	pdev = to_platform_device(dev);
	id = pdev->id;
	ctrl = get_fimc_ctrl(id);

	sprintf(temp, "\t");
	strcat(buf, temp);
	if (ctrl->log & FIMC_LOG_DEBUG) {
		sprintf(temp, "FIMC_LOG_DEBUG | ");
		strcat(buf, temp);
	}

	if (ctrl->log & FIMC_LOG_INFO_L2) {
		sprintf(temp, "FIMC_LOG_INFO_L2 | ");
		strcat(buf, temp);
	}

	if (ctrl->log & FIMC_LOG_INFO_L1) {
		sprintf(temp, "FIMC_LOG_INFO_L1 | ");
		strcat(buf, temp);
	}

	if (ctrl->log & FIMC_LOG_WARN) {
		sprintf(temp, "FIMC_LOG_WARN | ");
		strcat(buf, temp);
	}

	if (ctrl->log & FIMC_LOG_ERR) {
		sprintf(temp, "FIMC_LOG_ERR\n");
		strcat(buf, temp);
	}

	return strlen(buf);
}

static int fimc_store_log_level(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct fimc_control *ctrl;
	struct platform_device *pdev;

	const char *p = buf;
	char msg[150] = {0, };
	int id = -1;
	u32 match = 0;

	pdev = to_platform_device(dev);
	id = pdev->id;
	ctrl = get_fimc_ctrl(id);

	while (*p != '\0') {
		if (!isspace(*p))
			strncat(msg, p, 1);
		p++;
	}

	ctrl->log = 0;
	printk(KERN_INFO "FIMC.%d log level is set as below.\n", id);

	if (strstr(msg, "FIMC_LOG_ERR") != NULL) {
		ctrl->log |= FIMC_LOG_ERR;
		match = 1;
		printk(KERN_INFO "\tFIMC_LOG_ERR\n");
	}

	if (strstr(msg, "FIMC_LOG_WARN") != NULL) {
		ctrl->log |= FIMC_LOG_WARN;
		match = 1;
		printk(KERN_INFO "\tFIMC_LOG_WARN\n");
	}

	if (strstr(msg, "FIMC_LOG_INFO_L1") != NULL) {
		ctrl->log |= FIMC_LOG_INFO_L1;
		match = 1;
		printk(KERN_INFO "\tFIMC_LOG_INFO_L1\n");
	}

	if (strstr(msg, "FIMC_LOG_INFO_L2") != NULL) {
		ctrl->log |= FIMC_LOG_INFO_L2;
		match = 1;
		printk(KERN_INFO "\tFIMC_LOG_INFO_L2\n");
	}

	if (strstr(msg, "FIMC_LOG_DEBUG") != NULL) {
		ctrl->log |= FIMC_LOG_DEBUG;
		match = 1;
		printk(KERN_INFO "\tFIMC_LOG_DEBUG\n");
	}

	if (!match) {
		printk(KERN_INFO "FIMC_LOG_ERR		\t: Error condition.\n");
		printk(KERN_INFO "FIMC_LOG_WARN		\t: WARNING condition.\n");
		printk(KERN_INFO "FIMC_LOG_INFO_L1	\t: V4L2 API without QBUF, DQBUF.\n");
		printk(KERN_INFO "FIMC_LOG_INFO_L2	\t: V4L2 API QBUF, DQBUF.\n");
		printk(KERN_INFO "FIMC_LOG_DEBUG	\t: Queue status report.\n");
	}

	return len;
}

static DEVICE_ATTR(log_level, 0644, \
			fimc_show_log_level,
			fimc_store_log_level);

static int fimc_show_range_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fimc_control *ctrl;
	struct platform_device *pdev;
	int id = -1;

	char temp[150];

	pdev = to_platform_device(dev);
	id = pdev->id;
	ctrl = get_fimc_ctrl(id);

	sprintf(temp, "\t");
	strcat(buf, temp);
	if (ctrl->range == FIMC_RANGE_NARROW) {
		sprintf(temp, "FIMC_RANGE_NARROW\n");
		strcat(buf, temp);
	} else {
		sprintf(temp, "FIMC_RANGE_WIDE\n");
		strcat(buf, temp);
	}

	return strlen(buf);
}

static int fimc_store_range_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct fimc_control *ctrl;
	struct platform_device *pdev;

	const char *p = buf;
	char msg[150] = {0, };
	int id = -1;
	u32 match = 0;

	pdev = to_platform_device(dev);
	id = pdev->id;
	ctrl = get_fimc_ctrl(id);

	while (*p != '\0') {
		if (!isspace(*p))
			strncat(msg, p, 1);
		p++;
	}

	ctrl->range = 0;
	printk(KERN_INFO "FIMC.%d range mode is set as below.\n", id);

	if (strstr(msg, "FIMC_RANGE_WIDE") != NULL) {
		ctrl->range = FIMC_RANGE_WIDE;
		match = 1;
		printk(KERN_INFO "\tFIMC_RANGE_WIDE\n");
	}

	if (strstr(msg, "FIMC_RANGE_NARROW") != NULL) {
		ctrl->range = FIMC_RANGE_NARROW;
		match = 1;
		printk(KERN_INFO "\tFIMC_RANGE_NARROW\n");
	}

	return len;
}

static DEVICE_ATTR(range_mode, 0644, \
			fimc_show_range_mode,
			fimc_store_range_mode);

static int __devinit fimc_probe(struct platform_device *pdev)
{
	struct s3c_platform_fimc *pdata;
	struct fimc_control *ctrl;
	int ret;

	if (!fimc_dev) {
		fimc_dev = kzalloc(sizeof(*fimc_dev), GFP_KERNEL);
		if (!fimc_dev) {
			dev_err(&pdev->dev, "%s: not enough memory\n",
				__func__);
			return -ENOMEM;
		}
	}

	ctrl = fimc_register_controller(pdev);
	if (!ctrl) {
		printk(KERN_ERR "%s: cannot register fimc\n", __func__);
		goto err_alloc;
	}

	pdata = to_fimc_plat(&pdev->dev);
	if ((ctrl->id == FIMC0) && (pdata->cfg_gpio))
		pdata->cfg_gpio(pdev);

	/* V4L2 device-subdev registration */
	ret = v4l2_device_register(&pdev->dev, &ctrl->v4l2_dev);
	if (ret) {
		fimc_err("%s: v4l2 device register failed\n", __func__);
		goto err_fimc;
	}
	ctrl->vd->v4l2_dev = &ctrl->v4l2_dev;

	/* things to initialize once */
	if (!fimc_dev->initialized) {
		ret = fimc_init_global(pdev);
		if (ret)
			goto err_v4l2;
	}

	/* video device register */
	ret = video_register_device(ctrl->vd, VFL_TYPE_GRABBER, ctrl->id);
	if (ret) {
		fimc_err("%s: cannot register video driver\n", __func__);
		goto err_v4l2;
	}

	video_set_drvdata(ctrl->vd, ctrl);

#ifdef CONFIG_VIDEO_FIMC_RANGE_WIDE
	ctrl->range = FIMC_RANGE_WIDE;
#else
	ctrl->range = FIMC_RANGE_NARROW;
#endif

	ret = device_create_file(&(pdev->dev), &dev_attr_log_level);
	if (ret < 0) {
		fimc_err("failed to add sysfs entries for log level\n");
		goto err_global;
	}
	ret = device_create_file(&(pdev->dev), &dev_attr_range_mode);
	if (ret < 0) {
		fimc_err("failed to add sysfs entries for range mode\n");
		goto err_global;
	}
	printk(KERN_INFO "FIMC%d registered successfully\n", ctrl->id);
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	ctrl->power_status = FIMC_POWER_OFF;
	pm_runtime_enable(&pdev->dev);

	sprintf(buf, "fimc%d_iqr_wq_name", ctrl->id);
	ctrl->fimc_irq_wq = create_workqueue(buf);

	if (ctrl->fimc_irq_wq == NULL) {
		fimc_err("Cannot create workqueue for fimc driver\n");
		goto err_global;
	}

	INIT_WORK(&ctrl->work_struct, s3c_fimc_irq_work);
	atomic_set(&ctrl->irq_cnt, 0);
#endif

	return 0;

err_global:
	video_unregister_device(ctrl->vd);

err_v4l2:
	v4l2_device_unregister(&ctrl->v4l2_dev);

err_fimc:
	fimc_unregister_controller(pdev);

err_alloc:
	kfree(fimc_dev);
	return -EINVAL;

}

static int fimc_remove(struct platform_device *pdev)
{
	fimc_unregister_controller(pdev);

	device_remove_file(&(pdev->dev), &dev_attr_log_level);

	kfree(fimc_dev);
	fimc_dev = NULL;

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	pm_runtime_disable(&pdev->dev);
#endif
	return 0;
}

#ifdef CONFIG_PM
static inline void fimc_suspend_out_ctx(struct fimc_control *ctrl,
					struct fimc_ctx *ctx)
{
	switch (ctx->overlay.mode) {
	case FIMC_OVLY_DMA_AUTO:		/* fall through */
	case FIMC_OVLY_DMA_MANUAL:		/* fall through */
	case FIMC_OVLY_NONE_MULTI_BUF:		/* fall through */
	case FIMC_OVLY_NONE_SINGLE_BUF:
		if (ctx->status == FIMC_STREAMON) {
			if (ctx->inq[0] != -1)
				fimc_err("%s : %d in queue unstable\n",
					__func__, __LINE__);

			fimc_outdev_stop_streaming(ctrl, ctx);
			ctx->status = FIMC_ON_SLEEP;
		} else if (ctx->status == FIMC_STREAMON_IDLE) {
			fimc_outdev_stop_streaming(ctrl, ctx);
			ctx->status = FIMC_ON_IDLE_SLEEP;
		} else {
			ctx->status = FIMC_OFF_SLEEP;
		}

		break;
	case FIMC_OVLY_FIFO:
		if (ctx->status == FIMC_STREAMON) {
			if (ctx->inq[0] != -1)
				fimc_err("%s: %d in queue unstable\n",
					 __func__, __LINE__);

			if ((ctrl->out->idxs.next.idx != -1) ||
			    (ctrl->out->idxs.prev.idx != -1))
				fimc_err("%s: %d FIMC unstable\n",
					__func__, __LINE__);

			fimc_outdev_stop_streaming(ctrl, ctx);
			ctx->status = FIMC_ON_SLEEP;
		} else {
			ctx->status = FIMC_OFF_SLEEP;
		}

		break;
	case FIMC_OVLY_NOT_FIXED:
		ctx->status = FIMC_OFF_SLEEP;
		break;
	}
}

static inline int fimc_suspend_out(struct fimc_control *ctrl)
{
	struct fimc_ctx *ctx;
	int i, on_sleep = 0, idle_sleep = 0, off_sleep = 0;

	for (i = 0; i < FIMC_MAX_CTXS; i++) {
		ctx = &ctrl->out->ctx[i];
		fimc_suspend_out_ctx(ctrl, ctx);

		switch (ctx->status) {
		case FIMC_ON_SLEEP:
			on_sleep++;
			break;
		case FIMC_ON_IDLE_SLEEP:
			idle_sleep++;
			break;
		case FIMC_OFF_SLEEP:
			off_sleep++;
			break;
		default:
			break;
		}
	}

	if (on_sleep)
		ctrl->status = FIMC_ON_SLEEP;
	else if (idle_sleep)
		ctrl->status = FIMC_ON_IDLE_SLEEP;
	else
		ctrl->status = FIMC_OFF_SLEEP;

	ctrl->out->last_ctx = -1;

	return 0;
}

static inline int fimc_suspend_cap(struct fimc_control *ctrl)
{
	struct fimc_global *fimc = get_fimc_dev();

	fimc_dbg("%s\n", __func__);

	if (ctrl->cam->id == CAMERA_WB || ctrl->cam->id == CAMERA_WB_B)	{
		fimc_dbg("%s\n", __func__);
		ctrl->suspend_framecnt = fimc_hwget_output_buf_sequence(ctrl);
		fimc_streamoff_capture((void *)ctrl);
		fimc_info1("%s : framecnt_seq : %d\n",
				__func__, ctrl->suspend_framecnt);
	} else {
		if (ctrl->id == FIMC0 && ctrl->cam->initialized) {
			ctrl->cam->initialized = 0;

			v4l2_subdev_call(ctrl->cam->sd, core, s_power, 0);

			if (ctrl->cam->cam_power)
				ctrl->cam->cam_power(0);

			/* shutdown the MCLK */
			clk_disable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_OFF;
		}
	}
	ctrl->power_status = FIMC_POWER_OFF;

	return 0;
}

int fimc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct fimc_control *ctrl;
	struct s3c_platform_fimc *pdata;
	int id;

	id = pdev->id;
	ctrl = get_fimc_ctrl(id);
	pdata = to_fimc_plat(ctrl->dev);

	if (ctrl->out)
		fimc_suspend_out(ctrl);

	else if (ctrl->cap)
		fimc_suspend_cap(ctrl);
	else
		ctrl->status = FIMC_OFF_SLEEP;

#if (!defined(CONFIG_EXYNOS_DEV_PD) || !defined(CONFIG_PM_RUNTIME))
	if (atomic_read(&ctrl->in_use) && pdata->clk_off)
		pdata->clk_off(pdev, &ctrl->clk);
#endif

	return 0;
}

int fimc_suspend_pd(struct device *dev)
{
	struct platform_device *pdev;
	int ret;
	pm_message_t state;

	state.event = 0;
	pdev = to_platform_device(dev);
	ret = fimc_suspend(pdev, state);

	return 0;
}

static inline void fimc_resume_out_ctx(struct fimc_control *ctrl,
				       struct fimc_ctx *ctx)
{
	int index = -1, ret = -1;

	switch (ctx->overlay.mode) {
	case FIMC_OVLY_FIFO:
		if (ctx->status == FIMC_ON_SLEEP) {
			ctx->status = FIMC_READY_ON;

			ret = fimc_outdev_set_ctx_param(ctrl, ctx);
			if (ret < 0)
				fimc_err("Fail: fimc_outdev_set_ctx_param\n");

#if defined(CONFIG_VIDEO_IPC)
			if (ctx->pix.field == V4L2_FIELD_INTERLACED_TB)
				ipc_start();
#endif
			index = ctrl->out->idxs.active.idx;
			fimc_outdev_set_src_addr(ctrl, ctx->src[index].base);

			ret = fimc_start_fifo(ctrl, ctx);
			if (ret < 0)
				fimc_err("Fail: fimc_start_fifo\n");

			ctx->status = FIMC_STREAMON;
		} else if (ctx->status == FIMC_OFF_SLEEP) {
			ctx->status = FIMC_STREAMOFF;
		} else {
			fimc_err("%s: Abnormal (%d)\n", __func__, ctx->status);
		}

		break;
	case FIMC_OVLY_DMA_AUTO:
		if (ctx->status == FIMC_ON_IDLE_SLEEP) {
			fimc_outdev_resume_dma(ctrl, ctx);
			ret = fimc_outdev_set_ctx_param(ctrl, ctx);
			if (ret < 0)
				fimc_err("Fail: fimc_outdev_set_ctx_param\n");

			ctx->status = FIMC_STREAMON_IDLE;
		} else if (ctx->status == FIMC_OFF_SLEEP) {
			ctx->status = FIMC_STREAMOFF;
		} else {
			fimc_err("%s: Abnormal (%d)\n", __func__, ctx->status);
		}

		break;
	case FIMC_OVLY_DMA_MANUAL:
		if (ctx->status == FIMC_ON_IDLE_SLEEP) {
			ret = fimc_outdev_set_ctx_param(ctrl, ctx);
			if (ret < 0)
				fimc_err("Fail: fimc_outdev_set_ctx_param\n");

			ctx->status = FIMC_STREAMON_IDLE;

		} else if (ctx->status == FIMC_OFF_SLEEP) {
			ctx->status = FIMC_STREAMOFF;
		} else {
			fimc_err("%s: Abnormal (%d)\n", __func__, ctx->status);
		}

		break;
	case FIMC_OVLY_NONE_SINGLE_BUF:		/* fall through */
	case FIMC_OVLY_NONE_MULTI_BUF:
		if (ctx->status == FIMC_ON_IDLE_SLEEP) {
			ret = fimc_outdev_set_ctx_param(ctrl, ctx);
			if (ret < 0)
				fimc_err("Fail: fimc_outdev_set_ctx_param\n");

			ctx->status = FIMC_STREAMON_IDLE;
		} else if (ctx->status == FIMC_OFF_SLEEP) {
			ctx->status = FIMC_STREAMOFF;
		} else {
			fimc_err("%s: Abnormal (%d)\n", __func__, ctx->status);
		}

		break;
	default:
		ctx->status = FIMC_STREAMOFF;
		break;
	}
}

static inline int fimc_resume_out(struct fimc_control *ctrl)
{
	struct fimc_ctx *ctx;
	int i;
	u32 state = 0;
	u32 timeout;
	u32 tmp;
	struct s3c_platform_fimc *pdata;

	pdata = to_fimc_plat(ctrl->dev);

	tmp = __raw_readl(S5P_PMU_CAM_CONF + 0x4) & S5P_INT_LOCAL_PWR_EN;

	if (tmp != S5P_INT_LOCAL_PWR_EN) {
		__raw_writel(S5P_INT_LOCAL_PWR_EN, S5P_PMU_CAM_CONF);

		/*  Wait max 1ms */
		timeout = 10;
		while ((__raw_readl(S5P_PMU_CAM_CONF + 0x4) & S5P_INT_LOCAL_PWR_EN)
					!= S5P_INT_LOCAL_PWR_EN) {
			if (timeout == 0) {
				printk(KERN_ERR "Power domain CAM enable failed.\n");
				break;
			}
			timeout--;
			udelay(100);
		}
	}

	for (i = 0; i < FIMC_MAX_CTXS; i++) {
		ctx = &ctrl->out->ctx[i];

		if (pdata->clk_on) {
			pdata->clk_on(to_platform_device(ctrl->dev),
					&ctrl->clk);
		}

		fimc_resume_out_ctx(ctrl, ctx);

		if (pdata->clk_off) {
			pdata->clk_off(to_platform_device(ctrl->dev),
					&ctrl->clk);
		}

		switch (ctx->status) {
		case FIMC_STREAMON:
			state |= FIMC_STREAMON;
			break;
		case FIMC_STREAMON_IDLE:
			state |= FIMC_STREAMON_IDLE;
			break;
		case FIMC_STREAMOFF:
			state |= FIMC_STREAMOFF;
			break;
		default:
			break;
		}
	}

	if (tmp != S5P_INT_LOCAL_PWR_EN) {
		__raw_writel(0, S5P_PMU_CAM_CONF);

		/* Wait max 1ms */
		timeout = 10;
		while (__raw_readl(S5P_PMU_CAM_CONF + 0x4) & S5P_INT_LOCAL_PWR_EN) {
			if (timeout == 0) {
				printk(KERN_ERR "Power domain CAM disable failed.\n");
				break;
			}
			timeout--;
			udelay(100);
		}
	}

	if ((state & FIMC_STREAMON) == FIMC_STREAMON)
		ctrl->status = FIMC_STREAMON;
	else if ((state & FIMC_STREAMON_IDLE) == FIMC_STREAMON_IDLE)
		ctrl->status = FIMC_STREAMON_IDLE;
	else
		ctrl->status = FIMC_STREAMOFF;

	return 0;
}

static inline int fimc_resume_cap(struct fimc_control *ctrl)
{
	struct fimc_global *fimc = get_fimc_dev();
	int tmp;
	u32 timeout;

	fimc_dbg("%s\n", __func__);

	__raw_writel(S5P_INT_LOCAL_PWR_EN, S5P_PMU_CAM_CONF);
	/*  Wait max 1ms */
	timeout = 10;
	while ((__raw_readl(S5P_PMU_CAM_CONF + 0x4) & S5P_INT_LOCAL_PWR_EN)
			!= S5P_INT_LOCAL_PWR_EN) {
		if (timeout == 0) {
			printk(KERN_ERR "Power domain CAM enable failed.\n");
			break;
		}
		timeout--;
		udelay(100);
	}

	if (ctrl->cam->id == CAMERA_WB || ctrl->cam->id == CAMERA_WB_B) {
		fimc_info1("%s : framecnt_seq : %d\n",
				__func__, ctrl->suspend_framecnt);
		fimc_hwset_output_buf_sequence_all(ctrl,
				ctrl->suspend_framecnt);
		tmp = fimc_hwget_output_buf_sequence(ctrl);
		fimc_info1("%s : real framecnt_seq : %d\n", __func__, tmp);

		fimc_streamon_capture((void *)ctrl);
	} else {
		if (ctrl->id == FIMC0 && ctrl->cam->initialized == 0) {
			clk_set_rate(ctrl->cam->clk, ctrl->cam->clk_rate);
			clk_enable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_ON;
			fimc_info1("clock for camera: %d\n", ctrl->cam->clk_rate);

			if (ctrl->cam->cam_power)
				ctrl->cam->cam_power(1);

			v4l2_subdev_call(ctrl->cam->sd, core, s_power, 1);

			ctrl->cam->initialized = 1;
		}
	}
	/* fimc_streamon_capture((void *)ctrl); */
	ctrl->power_status = FIMC_POWER_ON;

	return 0;
}

int fimc_resume(struct platform_device *pdev)
{
	struct fimc_control *ctrl;
	struct s3c_platform_fimc *pdata;
	int id = pdev->id;

	ctrl = get_fimc_ctrl(id);
	pdata = to_fimc_plat(ctrl->dev);

	if (atomic_read(&ctrl->in_use) && pdata->clk_on)
		pdata->clk_on(pdev, &ctrl->clk);

	if (ctrl->out)
		fimc_resume_out(ctrl);

	else if (ctrl->cap)
		fimc_resume_cap(ctrl);
	else
		ctrl->status = FIMC_STREAMOFF;

	return 0;
}

int fimc_resume_pd(struct device *dev)
{
	struct platform_device *pdev;
	int ret;

	pdev = to_platform_device(dev);
	ret = fimc_resume(pdev);
	return 0;
}


#else
#define fimc_suspend	NULL
#define fimc_resume	NULL
#endif

static int fimc_runtime_suspend_out(struct fimc_control *ctrl)
{
	struct s3c_platform_fimc *pdata;
	int ret;

	pdata = to_fimc_plat(ctrl->dev);

	if (pdata->clk_off) {
		ret = pdata->clk_off(to_platform_device(ctrl->dev), &ctrl->clk);
		if (ret == 0)
			ctrl->power_status = FIMC_POWER_OFF;
	}

	return 0;
}
static int fimc_runtime_suspend_cap(struct fimc_control *ctrl)
{
	struct s3c_platform_fimc *pdata	= to_fimc_plat(ctrl->dev);
	struct platform_device *pdev = to_platform_device(ctrl->dev);
	struct clk *pxl_async = NULL;
	int ret = 0;
	fimc_dbg("%s FIMC%d\n", __func__, ctrl->id);

	ctrl->power_status = FIMC_POWER_SUSPEND;

	if (ctrl->cap && (ctrl->status != FIMC_STREAMOFF)) {
		fimc_streamoff_capture((void *)ctrl);
		ctrl->status = FIMC_STREAMOFF;
	}

	if (pdata->clk_off) {
		ret = pdata->clk_off(pdev, &ctrl->clk);
		if (ret == 0)
			ctrl->power_status = FIMC_POWER_OFF;
	}

	fimc_dbg("%s\n", __func__);

	if (!ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if (ctrl->cam->id == CAMERA_WB) {
		fimc_info1("%s : writeback 0 suspend\n", __func__);
		pxl_async = clk_get(&pdev->dev, "pxl_async0");
		if (IS_ERR(pxl_async)) {
			dev_err(&pdev->dev, "failed to get pxl_async\n");
			return -ENODEV;
		}

		clk_disable(pxl_async);
		clk_put(pxl_async);
	} else if (ctrl->cam->id == CAMERA_WB_B) {
		fimc_info1("%s : writeback 1 suspend\n", __func__);
		pxl_async = clk_get(&pdev->dev, "pxl_async1");
		if (IS_ERR(pxl_async)) {
			dev_err(&pdev->dev, "failed to get pxl_async\n");
			return -ENODEV;
		}

		clk_disable(pxl_async);
		clk_put(pxl_async);
	}


	return 0;
}
static int fimc_runtime_suspend(struct device *dev)
{
	struct fimc_control *ctrl;
	struct platform_device *pdev;
	int id;

	pdev = to_platform_device(dev);
	id = pdev->id;
	ctrl = get_fimc_ctrl(id);

	fimc_dbg("%s FIMC%d\n", __func__, ctrl->id);

	if (ctrl->out) {
		fimc_info1("%s: fimc m2m\n", __func__);
		fimc_runtime_suspend_out(ctrl);
	} else if (ctrl->cap) {
		fimc_info1("%s: fimc capture\n", __func__);
		fimc_runtime_suspend_cap(ctrl);
	} else
		fimc_err("%s : invalid fimc control\n", __func__);

	fimc_dev->backup_regs[id] = ctrl->regs;
	ctrl->regs = NULL;

	return 0;
}

static int fimc_runtime_resume_cap(struct fimc_control *ctrl)
{
	struct platform_device *pdev = to_platform_device(ctrl->dev);
	struct clk *pxl_async = NULL;
	fimc_dbg("%s\n", __func__);

	if (!ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if (ctrl->cam->id == CAMERA_WB) {
		fimc_info1("%s : writeback 0 resume\n", __func__);
		pxl_async = clk_get(&pdev->dev, "pxl_async0");
		if (IS_ERR(pxl_async)) {
			dev_err(&pdev->dev, "failed to get pxl_async\n");
			return -ENODEV;
		}

		clk_enable(pxl_async);
		clk_put(pxl_async);
	} else if (ctrl->cam->id == CAMERA_WB_B) {
		fimc_info1("%s : writeback 1 resume\n", __func__);
		pxl_async = clk_get(&pdev->dev, "pxl_async1");
		if (IS_ERR(pxl_async)) {
			dev_err(&pdev->dev, "failed to get pxl_async\n");
			return -ENODEV;
		}

		clk_enable(pxl_async);
		clk_put(pxl_async);
	}

	return 0;
}
static int fimc_runtime_resume(struct device *dev)
{
	struct fimc_control *ctrl;
	struct s3c_platform_fimc *pdata;
	struct platform_device *pdev;
	int id, ret = 0;

	pdev = to_platform_device(dev);
	id = pdev->id;
	ctrl = get_fimc_ctrl(id);

	ctrl->regs = fimc_dev->backup_regs[id];
	fimc_dev->backup_regs[id] = NULL;

	fimc_dbg("%s\n", __func__);

	pdata = to_fimc_plat(ctrl->dev);
	if (pdata->clk_on) {
		ret = pdata->clk_on(pdev, &ctrl->clk);
		if (ret == 0)
			ctrl->power_status = FIMC_POWER_ON;
	}

	/* if status is FIMC_PROBE, not need to know differlence of out or
	 * cap */

	if (ctrl->out) {
		/* do not need to sub function in m2m mode */
		fimc_info1("%s: fimc m2m\n", __func__);
	} else if (ctrl->cap) {
		fimc_info1("%s: fimc cap\n", __func__);
		fimc_runtime_resume_cap(ctrl);
	} else {
		fimc_err("%s: runtime resume error\n", __func__);
	}
	return 0;
}
static const struct dev_pm_ops fimc_pm_ops = {
	.suspend	= fimc_suspend_pd,
	.resume		= fimc_resume_pd,
	.runtime_suspend = fimc_runtime_suspend,
	.runtime_resume = fimc_runtime_resume,
};

static struct platform_driver fimc_driver = {
	.probe		= fimc_probe,
	.remove		= fimc_remove,
#if (!defined(CONFIG_EXYNOS_DEV_PD) || !defined(CONFIG_PM_RUNTIME))
	.suspend	= fimc_suspend,
	.resume		= fimc_resume,
#endif
	.driver		= {
		.name	= FIMC_NAME,
		.owner	= THIS_MODULE,
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
		.pm = &fimc_pm_ops,
#else
		.pm = NULL,
#endif

	},
};

static int fimc_register(void)
{
	platform_driver_register(&fimc_driver);

	return 0;
}

static void fimc_unregister(void)
{
	platform_driver_unregister(&fimc_driver);
}

late_initcall(fimc_register);
module_exit(fimc_unregister);

MODULE_AUTHOR("Dongsoo,	Kim <dongsoo45.kim@samsung.com>");
MODULE_AUTHOR("Jinsung,	Yang <jsgood.yang@samsung.com>");
MODULE_AUTHOR("Jonghun,	Han <jonghun.han@samsung.com>");
MODULE_DESCRIPTION("Samsung Camera Interface (FIMC) driver");
MODULE_LICENSE("GPL");
