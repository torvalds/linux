/*
 * Copyright (C) 2010-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
/*
 * Based on STMP378X PxP driver
 * Copyright 2008-2009 Embedded Alley Solutions, Inc All Rights Reserved.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/freezer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pxp_dma.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include "regs-pxp_v2.h"

#define	PXP_DOWNSCALE_THRESHOLD		0x4000

static LIST_HEAD(head);
static int timeout_in_ms = 600;
static unsigned int block_size;
static struct kmem_cache *tx_desc_cache;

struct pxp_dma {
	struct dma_device dma;
};

struct pxps {
	struct platform_device *pdev;
	struct clk *clk;
	struct clk *clk_disp_axi;	/* may exist on some SoC for gating */
	void __iomem *base;
	int irq;		/* PXP IRQ to the CPU */

	spinlock_t lock;
	struct mutex clk_mutex;
	int clk_stat;
#define	CLK_STAT_OFF		0
#define	CLK_STAT_ON		1
	int pxp_ongoing;
	int lut_state;

	struct device *dev;
	struct pxp_dma pxp_dma;
	struct pxp_channel channel[NR_PXP_VIRT_CHANNEL];
	struct work_struct work;

	/* describes most recent processing configuration */
	struct pxp_config_data pxp_conf_state;

	/* to turn clock off when pxp is inactive */
	struct timer_list clk_timer;

	/* for pxp config dispatch asynchronously*/
	struct task_struct *dispatch;
	wait_queue_head_t thread_waitq;
	struct completion complete;
};

#define to_pxp_dma(d) container_of(d, struct pxp_dma, dma)
#define to_tx_desc(tx) container_of(tx, struct pxp_tx_desc, txd)
#define to_pxp_channel(d) container_of(d, struct pxp_channel, dma_chan)
#define to_pxp(id) container_of(id, struct pxps, pxp_dma)

#define PXP_DEF_BUFS	2
#define PXP_MIN_PIX	8

/*
 * PXP common functions
 */
static void dump_pxp_reg(struct pxps *pxp)
{
	dev_dbg(pxp->dev, "PXP_CTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_CTRL));
	dev_dbg(pxp->dev, "PXP_STAT 0x%x",
		__raw_readl(pxp->base + HW_PXP_STAT));
	dev_dbg(pxp->dev, "PXP_OUT_CTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_OUT_CTRL));
	dev_dbg(pxp->dev, "PXP_OUT_BUF 0x%x",
		__raw_readl(pxp->base + HW_PXP_OUT_BUF));
	dev_dbg(pxp->dev, "PXP_OUT_BUF2 0x%x",
		__raw_readl(pxp->base + HW_PXP_OUT_BUF2));
	dev_dbg(pxp->dev, "PXP_OUT_PITCH 0x%x",
		__raw_readl(pxp->base + HW_PXP_OUT_PITCH));
	dev_dbg(pxp->dev, "PXP_OUT_LRC 0x%x",
		__raw_readl(pxp->base + HW_PXP_OUT_LRC));
	dev_dbg(pxp->dev, "PXP_OUT_PS_ULC 0x%x",
		__raw_readl(pxp->base + HW_PXP_OUT_PS_ULC));
	dev_dbg(pxp->dev, "PXP_OUT_PS_LRC 0x%x",
		__raw_readl(pxp->base + HW_PXP_OUT_PS_LRC));
	dev_dbg(pxp->dev, "PXP_OUT_AS_ULC 0x%x",
		__raw_readl(pxp->base + HW_PXP_OUT_AS_ULC));
	dev_dbg(pxp->dev, "PXP_OUT_AS_LRC 0x%x",
		__raw_readl(pxp->base + HW_PXP_OUT_AS_LRC));
	dev_dbg(pxp->dev, "PXP_PS_CTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_CTRL));
	dev_dbg(pxp->dev, "PXP_PS_BUF 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_BUF));
	dev_dbg(pxp->dev, "PXP_PS_UBUF 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_UBUF));
	dev_dbg(pxp->dev, "PXP_PS_VBUF 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_VBUF));
	dev_dbg(pxp->dev, "PXP_PS_PITCH 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_PITCH));
	dev_dbg(pxp->dev, "PXP_PS_BACKGROUND 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_BACKGROUND));
	dev_dbg(pxp->dev, "PXP_PS_SCALE 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_SCALE));
	dev_dbg(pxp->dev, "PXP_PS_OFFSET 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_OFFSET));
	dev_dbg(pxp->dev, "PXP_PS_CLRKEYLOW 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_CLRKEYLOW));
	dev_dbg(pxp->dev, "PXP_PS_CLRKEYHIGH 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_CLRKEYHIGH));
	dev_dbg(pxp->dev, "PXP_AS_CTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_CTRL));
	dev_dbg(pxp->dev, "PXP_AS_BUF 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_BUF));
	dev_dbg(pxp->dev, "PXP_AS_PITCH 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_PITCH));
	dev_dbg(pxp->dev, "PXP_AS_CLRKEYLOW 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_CLRKEYLOW));
	dev_dbg(pxp->dev, "PXP_AS_CLRKEYHIGH 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_CLRKEYHIGH));
	dev_dbg(pxp->dev, "PXP_CSC1_COEF0 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC1_COEF0));
	dev_dbg(pxp->dev, "PXP_CSC1_COEF1 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC1_COEF1));
	dev_dbg(pxp->dev, "PXP_CSC1_COEF2 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC1_COEF2));
	dev_dbg(pxp->dev, "PXP_CSC2_CTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC2_CTRL));
	dev_dbg(pxp->dev, "PXP_CSC2_COEF0 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC2_COEF0));
	dev_dbg(pxp->dev, "PXP_CSC2_COEF1 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC2_COEF1));
	dev_dbg(pxp->dev, "PXP_CSC2_COEF2 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC2_COEF2));
	dev_dbg(pxp->dev, "PXP_CSC2_COEF3 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC2_COEF3));
	dev_dbg(pxp->dev, "PXP_CSC2_COEF4 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC2_COEF4));
	dev_dbg(pxp->dev, "PXP_CSC2_COEF5 0x%x",
		__raw_readl(pxp->base + HW_PXP_CSC2_COEF5));
	dev_dbg(pxp->dev, "PXP_LUT_CTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_LUT_CTRL));
	dev_dbg(pxp->dev, "PXP_LUT_ADDR 0x%x",
		__raw_readl(pxp->base + HW_PXP_LUT_ADDR));
	dev_dbg(pxp->dev, "PXP_LUT_DATA 0x%x",
		__raw_readl(pxp->base + HW_PXP_LUT_DATA));
	dev_dbg(pxp->dev, "PXP_LUT_EXTMEM 0x%x",
		__raw_readl(pxp->base + HW_PXP_LUT_EXTMEM));
	dev_dbg(pxp->dev, "PXP_CFA 0x%x",
		__raw_readl(pxp->base + HW_PXP_CFA));
	dev_dbg(pxp->dev, "PXP_HIST_CTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_HIST_CTRL));
	dev_dbg(pxp->dev, "PXP_HIST2_PARAM 0x%x",
		__raw_readl(pxp->base + HW_PXP_HIST2_PARAM));
	dev_dbg(pxp->dev, "PXP_HIST4_PARAM 0x%x",
		__raw_readl(pxp->base + HW_PXP_HIST4_PARAM));
	dev_dbg(pxp->dev, "PXP_HIST8_PARAM0 0x%x",
		__raw_readl(pxp->base + HW_PXP_HIST8_PARAM0));
	dev_dbg(pxp->dev, "PXP_HIST8_PARAM1 0x%x",
		__raw_readl(pxp->base + HW_PXP_HIST8_PARAM1));
	dev_dbg(pxp->dev, "PXP_HIST16_PARAM0 0x%x",
		__raw_readl(pxp->base + HW_PXP_HIST16_PARAM0));
	dev_dbg(pxp->dev, "PXP_HIST16_PARAM1 0x%x",
		__raw_readl(pxp->base + HW_PXP_HIST16_PARAM1));
	dev_dbg(pxp->dev, "PXP_HIST16_PARAM2 0x%x",
		__raw_readl(pxp->base + HW_PXP_HIST16_PARAM2));
	dev_dbg(pxp->dev, "PXP_HIST16_PARAM3 0x%x",
		__raw_readl(pxp->base + HW_PXP_HIST16_PARAM3));
	dev_dbg(pxp->dev, "PXP_POWER 0x%x",
		__raw_readl(pxp->base + HW_PXP_POWER));
	dev_dbg(pxp->dev, "PXP_NEXT 0x%x",
		__raw_readl(pxp->base + HW_PXP_NEXT));
	dev_dbg(pxp->dev, "PXP_DEBUGCTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_DEBUGCTRL));
	dev_dbg(pxp->dev, "PXP_DEBUG 0x%x",
		__raw_readl(pxp->base + HW_PXP_DEBUG));
	dev_dbg(pxp->dev, "PXP_VERSION 0x%x",
		__raw_readl(pxp->base + HW_PXP_VERSION));
}

static bool is_yuv(u32 pix_fmt)
{
	if ((pix_fmt == PXP_PIX_FMT_YUYV) |
	    (pix_fmt == PXP_PIX_FMT_UYVY) |
	    (pix_fmt == PXP_PIX_FMT_YVYU) |
	    (pix_fmt == PXP_PIX_FMT_VYUY) |
	    (pix_fmt == PXP_PIX_FMT_Y41P) |
	    (pix_fmt == PXP_PIX_FMT_VUY444) |
	    (pix_fmt == PXP_PIX_FMT_NV12) |
	    (pix_fmt == PXP_PIX_FMT_NV21) |
	    (pix_fmt == PXP_PIX_FMT_NV16) |
	    (pix_fmt == PXP_PIX_FMT_NV61) |
	    (pix_fmt == PXP_PIX_FMT_GREY) |
	    (pix_fmt == PXP_PIX_FMT_GY04) |
	    (pix_fmt == PXP_PIX_FMT_YVU410P) |
	    (pix_fmt == PXP_PIX_FMT_YUV410P) |
	    (pix_fmt == PXP_PIX_FMT_YVU420P) |
	    (pix_fmt == PXP_PIX_FMT_YUV420P) |
	    (pix_fmt == PXP_PIX_FMT_YUV420P2) |
	    (pix_fmt == PXP_PIX_FMT_YVU422P) |
	    (pix_fmt == PXP_PIX_FMT_YUV422P)) {
		return true;
	} else {
		return false;
	}
}

static void pxp_soft_reset(struct pxps *pxp)
{
	__raw_writel(BM_PXP_CTRL_SFTRST, pxp->base + HW_PXP_CTRL_CLR);
	__raw_writel(BM_PXP_CTRL_CLKGATE, pxp->base + HW_PXP_CTRL_CLR);

	__raw_writel(BM_PXP_CTRL_SFTRST, pxp->base + HW_PXP_CTRL_SET);
	while (!(__raw_readl(pxp->base + HW_PXP_CTRL) & BM_PXP_CTRL_CLKGATE))
		dev_dbg(pxp->dev, "%s: wait for clock gate off", __func__);

	__raw_writel(BM_PXP_CTRL_SFTRST, pxp->base + HW_PXP_CTRL_CLR);
	__raw_writel(BM_PXP_CTRL_CLKGATE, pxp->base + HW_PXP_CTRL_CLR);
}

static void pxp_set_ctrl(struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_proc_data *proc_data = &pxp_conf->proc_data;
	u32 ctrl;
	u32 fmt_ctrl;
	int need_swap = 0;   /* to support YUYV and YVYU formats */

	/* Configure S0 input format */
	switch (pxp_conf->s0_param.pixel_fmt) {
	case PXP_PIX_FMT_RGB32:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__RGB888;
		break;
	case PXP_PIX_FMT_RGB565:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__RGB565;
		break;
	case PXP_PIX_FMT_RGB555:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__RGB555;
		break;
	case PXP_PIX_FMT_YUV420P:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__YUV420;
		break;
	case PXP_PIX_FMT_YVU420P:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__YUV420;
		break;
	case PXP_PIX_FMT_GREY:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__Y8;
		break;
	case PXP_PIX_FMT_GY04:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__Y4;
		break;
	case PXP_PIX_FMT_VUY444:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__YUV1P444;
		break;
	case PXP_PIX_FMT_YUV422P:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__YUV422;
		break;
	case PXP_PIX_FMT_UYVY:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__UYVY1P422;
		break;
	case PXP_PIX_FMT_YUYV:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__UYVY1P422;
		need_swap = 1;
		break;
	case PXP_PIX_FMT_VYUY:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__VYUY1P422;
		break;
	case PXP_PIX_FMT_YVYU:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__VYUY1P422;
		need_swap = 1;
		break;
	case PXP_PIX_FMT_NV12:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__YUV2P420;
		break;
	case PXP_PIX_FMT_NV21:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__YVU2P420;
		break;
	case PXP_PIX_FMT_NV16:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__YUV2P422;
		break;
	case PXP_PIX_FMT_NV61:
		fmt_ctrl = BV_PXP_PS_CTRL_FORMAT__YVU2P422;
		break;
	default:
		fmt_ctrl = 0;
	}

	ctrl = BF_PXP_PS_CTRL_FORMAT(fmt_ctrl) | BF_PXP_PS_CTRL_SWAP(need_swap);
	__raw_writel(ctrl, pxp->base + HW_PXP_PS_CTRL_SET);

	/* Configure output format based on out_channel format */
	switch (pxp_conf->out_param.pixel_fmt) {
	case PXP_PIX_FMT_RGB32:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__RGB888;
		break;
	case PXP_PIX_FMT_BGRA32:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__ARGB8888;
		break;
	case PXP_PIX_FMT_RGB24:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__RGB888P;
		break;
	case PXP_PIX_FMT_RGB565:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__RGB565;
		break;
	case PXP_PIX_FMT_RGB555:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__RGB555;
		break;
	case PXP_PIX_FMT_GREY:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__Y8;
		break;
	case PXP_PIX_FMT_GY04:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__Y4;
		break;
	case PXP_PIX_FMT_UYVY:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__UYVY1P422;
		break;
	case PXP_PIX_FMT_VYUY:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__VYUY1P422;
		break;
	case PXP_PIX_FMT_NV12:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__YUV2P420;
		break;
	case PXP_PIX_FMT_NV21:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__YVU2P420;
		break;
	case PXP_PIX_FMT_NV16:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__YUV2P422;
		break;
	case PXP_PIX_FMT_NV61:
		fmt_ctrl = BV_PXP_OUT_CTRL_FORMAT__YVU2P422;
		break;
	default:
		fmt_ctrl = 0;
	}

	ctrl = BF_PXP_OUT_CTRL_FORMAT(fmt_ctrl);
	__raw_writel(ctrl, pxp->base + HW_PXP_OUT_CTRL);

	ctrl = 0;
	if (proc_data->scaling)
		;
	if (proc_data->vflip)
		ctrl |= BM_PXP_CTRL_VFLIP;
	if (proc_data->hflip)
		ctrl |= BM_PXP_CTRL_HFLIP;
	if (proc_data->rotate)
		ctrl |= BF_PXP_CTRL_ROTATE(proc_data->rotate / 90);

	/* In default, the block size is set to 8x8
	 * But block size can be set to 16x16 due to
	 * blocksize variable modification
	 */
	ctrl |= block_size << 23;

	__raw_writel(ctrl, pxp->base + HW_PXP_CTRL);
}

static int pxp_start(struct pxps *pxp)
{
	__raw_writel(BM_PXP_CTRL_IRQ_ENABLE, pxp->base + HW_PXP_CTRL_SET);
	__raw_writel(BM_PXP_CTRL_ENABLE, pxp->base + HW_PXP_CTRL_SET);
	dump_pxp_reg(pxp);

	return 0;
}

static void pxp_set_outbuf(struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_layer_param *out_params = &pxp_conf->out_param;
	struct pxp_proc_data *proc_data = &pxp_conf->proc_data;

	__raw_writel(out_params->paddr, pxp->base + HW_PXP_OUT_BUF);

	if ((out_params->pixel_fmt == PXP_PIX_FMT_NV12) ||
		(out_params->pixel_fmt == PXP_PIX_FMT_NV21) ||
		(out_params->pixel_fmt == PXP_PIX_FMT_NV16) ||
		(out_params->pixel_fmt == PXP_PIX_FMT_NV61)) {
		dma_addr_t Y, U;

		Y = out_params->paddr;
		U = Y + (out_params->width * out_params->height);

		__raw_writel(U, pxp->base + HW_PXP_OUT_BUF2);
	}

	if (proc_data->rotate == 90 || proc_data->rotate == 270)
		__raw_writel(BF_PXP_OUT_LRC_X(out_params->height - 1) |
				BF_PXP_OUT_LRC_Y(out_params->width - 1),
				pxp->base + HW_PXP_OUT_LRC);
	else
		__raw_writel(BF_PXP_OUT_LRC_X(out_params->width - 1) |
				BF_PXP_OUT_LRC_Y(out_params->height - 1),
				pxp->base + HW_PXP_OUT_LRC);

	if (out_params->pixel_fmt == PXP_PIX_FMT_RGB24) {
		__raw_writel(out_params->stride * 3,
				pxp->base + HW_PXP_OUT_PITCH);
	} else if (out_params->pixel_fmt == PXP_PIX_FMT_BGRA32 ||
		out_params->pixel_fmt == PXP_PIX_FMT_RGB32) {
		__raw_writel(out_params->stride << 2,
				pxp->base + HW_PXP_OUT_PITCH);
	} else if ((out_params->pixel_fmt == PXP_PIX_FMT_RGB565) ||
		   (out_params->pixel_fmt == PXP_PIX_FMT_RGB555)) {
		__raw_writel(out_params->stride << 1,
				pxp->base + HW_PXP_OUT_PITCH);
	} else if (out_params->pixel_fmt == PXP_PIX_FMT_UYVY ||
		(out_params->pixel_fmt == PXP_PIX_FMT_VYUY)) {
		__raw_writel(out_params->stride << 1,
				pxp->base + HW_PXP_OUT_PITCH);
	} else if (out_params->pixel_fmt == PXP_PIX_FMT_GREY ||
		   out_params->pixel_fmt == PXP_PIX_FMT_NV12 ||
		   out_params->pixel_fmt == PXP_PIX_FMT_NV21 ||
		   out_params->pixel_fmt == PXP_PIX_FMT_NV16 ||
		   out_params->pixel_fmt == PXP_PIX_FMT_NV61) {
		__raw_writel(out_params->stride,
				pxp->base + HW_PXP_OUT_PITCH);
	} else if (out_params->pixel_fmt == PXP_PIX_FMT_GY04) {
		__raw_writel(out_params->stride >> 1,
				pxp->base + HW_PXP_OUT_PITCH);
	} else {
		__raw_writel(0, pxp->base + HW_PXP_OUT_PITCH);
	}

	/* set global alpha if necessary */
	if (out_params->global_alpha_enable) {
		__raw_writel(out_params->global_alpha << 24,
				pxp->base + HW_PXP_OUT_CTRL_SET);
		__raw_writel(BM_PXP_OUT_CTRL_ALPHA_OUTPUT,
				pxp->base + HW_PXP_OUT_CTRL_SET);
	}
}

static void pxp_set_s0colorkey(struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_layer_param *s0_params = &pxp_conf->s0_param;

	/* Low and high are set equal. V4L does not allow a chromakey range */
	if (s0_params->color_key_enable == 0 || s0_params->color_key == -1) {
		/* disable color key */
		__raw_writel(0xFFFFFF, pxp->base + HW_PXP_PS_CLRKEYLOW);
		__raw_writel(0, pxp->base + HW_PXP_PS_CLRKEYHIGH);
	} else {
		__raw_writel(s0_params->color_key,
			     pxp->base + HW_PXP_PS_CLRKEYLOW);
		__raw_writel(s0_params->color_key,
			     pxp->base + HW_PXP_PS_CLRKEYHIGH);
	}
}

static void pxp_set_olcolorkey(int layer_no, struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_layer_param *ol_params = &pxp_conf->ol_param[layer_no];

	/* Low and high are set equal. V4L does not allow a chromakey range */
	if (ol_params->color_key_enable != 0 && ol_params->color_key != -1) {
		__raw_writel(ol_params->color_key,
			     pxp->base + HW_PXP_AS_CLRKEYLOW);
		__raw_writel(ol_params->color_key,
			     pxp->base + HW_PXP_AS_CLRKEYHIGH);
	} else {
		/* disable color key */
		__raw_writel(0xFFFFFF, pxp->base + HW_PXP_AS_CLRKEYLOW);
		__raw_writel(0, pxp->base + HW_PXP_AS_CLRKEYHIGH);
	}
}

static void pxp_set_oln(int layer_no, struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_layer_param *olparams_data = &pxp_conf->ol_param[layer_no];
	dma_addr_t phys_addr = olparams_data->paddr;
	u32 pitch = olparams_data->stride ? olparams_data->stride :
					    olparams_data->width;

	__raw_writel(phys_addr, pxp->base + HW_PXP_AS_BUF);

	/* Fixme */
	if (olparams_data->width == 0 && olparams_data->height == 0) {
		__raw_writel(0xffffffff, pxp->base + HW_PXP_OUT_AS_ULC);
		__raw_writel(0x0, pxp->base + HW_PXP_OUT_AS_LRC);
	} else {
		__raw_writel(0x0, pxp->base + HW_PXP_OUT_AS_ULC);
		__raw_writel(BF_PXP_OUT_AS_LRC_X(olparams_data->width - 1) |
				BF_PXP_OUT_AS_LRC_Y(olparams_data->height - 1),
				pxp->base + HW_PXP_OUT_AS_LRC);
	}

	if ((olparams_data->pixel_fmt == PXP_PIX_FMT_BGRA32) ||
		 (olparams_data->pixel_fmt == PXP_PIX_FMT_RGB32)) {
		__raw_writel(pitch << 2,
				pxp->base + HW_PXP_AS_PITCH);
	} else if ((olparams_data->pixel_fmt == PXP_PIX_FMT_RGB565) ||
		   (olparams_data->pixel_fmt == PXP_PIX_FMT_RGB555)) {
		__raw_writel(pitch << 1,
				pxp->base + HW_PXP_AS_PITCH);
	} else {
		__raw_writel(0, pxp->base + HW_PXP_AS_PITCH);
	}
}

static void pxp_set_olparam(int layer_no, struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_layer_param *olparams_data = &pxp_conf->ol_param[layer_no];
	u32 olparam;

	olparam = BF_PXP_AS_CTRL_ALPHA(olparams_data->global_alpha);
	if (olparams_data->pixel_fmt == PXP_PIX_FMT_RGB32) {
		olparam |=
		    BF_PXP_AS_CTRL_FORMAT(BV_PXP_AS_CTRL_FORMAT__RGB888);
	} else if (olparams_data->pixel_fmt == PXP_PIX_FMT_BGRA32) {
		olparam |=
		    BF_PXP_AS_CTRL_FORMAT(BV_PXP_AS_CTRL_FORMAT__ARGB8888);
		if (!olparams_data->combine_enable) {
			olparam |=
				BF_PXP_AS_CTRL_ALPHA_CTRL
				(BV_PXP_AS_CTRL_ALPHA_CTRL__ROPs);
			olparam |= 0x3 << 16;
		}
	} else if (olparams_data->pixel_fmt == PXP_PIX_FMT_RGB565) {
		olparam |=
		    BF_PXP_AS_CTRL_FORMAT(BV_PXP_AS_CTRL_FORMAT__RGB565);
	} else if (olparams_data->pixel_fmt == PXP_PIX_FMT_RGB555) {
		olparam |=
		    BF_PXP_AS_CTRL_FORMAT(BV_PXP_AS_CTRL_FORMAT__RGB555);
	}

	if (olparams_data->global_alpha_enable) {
		if (olparams_data->global_override) {
			olparam |=
				BF_PXP_AS_CTRL_ALPHA_CTRL
				(BV_PXP_AS_CTRL_ALPHA_CTRL__Override);
		} else {
			olparam |=
				BF_PXP_AS_CTRL_ALPHA_CTRL
				(BV_PXP_AS_CTRL_ALPHA_CTRL__Multiply);
		}
		if (olparams_data->alpha_invert)
			olparam |= BM_PXP_AS_CTRL_ALPHA_INVERT;
	}
	if (olparams_data->color_key_enable)
		olparam |= BM_PXP_AS_CTRL_ENABLE_COLORKEY;

	__raw_writel(olparam, pxp->base + HW_PXP_AS_CTRL);
}

static void pxp_set_s0param(struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_proc_data *proc_data = &pxp_conf->proc_data;
	struct pxp_layer_param *out_params = &pxp_conf->out_param;
	u32 s0param_ulc, s0param_lrc;

	/* contains the coordinate for the PS in the OUTPUT buffer. */
	if ((pxp_conf->s0_param).width == 0 &&
		(pxp_conf->s0_param).height == 0) {
		__raw_writel(0xffffffff, pxp->base + HW_PXP_OUT_PS_ULC);
		__raw_writel(0x0, pxp->base + HW_PXP_OUT_PS_LRC);
	} else {
		switch (proc_data->rotate) {
		case 0:
			s0param_ulc = BF_PXP_OUT_PS_ULC_X(proc_data->drect.left);
			s0param_ulc |= BF_PXP_OUT_PS_ULC_Y(proc_data->drect.top);
			s0param_lrc = BF_PXP_OUT_PS_LRC_X(((s0param_ulc & BM_PXP_OUT_PS_ULC_X) >> 16) + proc_data->drect.width - 1);
			s0param_lrc |= BF_PXP_OUT_PS_LRC_Y((s0param_ulc & BM_PXP_OUT_PS_ULC_Y) + proc_data->drect.height - 1);
			break;
		case 90:
			s0param_ulc = BF_PXP_OUT_PS_ULC_Y(out_params->width - (proc_data->drect.left + proc_data->drect.width));
			s0param_ulc |= BF_PXP_OUT_PS_ULC_X(proc_data->drect.top);
			s0param_lrc = BF_PXP_OUT_PS_LRC_X(((s0param_ulc & BM_PXP_OUT_PS_ULC_X) >> 16) + proc_data->drect.height - 1);
			s0param_lrc |= BF_PXP_OUT_PS_LRC_Y((s0param_ulc & BM_PXP_OUT_PS_ULC_Y) + proc_data->drect.width - 1);
			break;
		case 180:
			s0param_ulc = BF_PXP_OUT_PS_ULC_X(out_params->width - (proc_data->drect.left + proc_data->drect.width));
			s0param_ulc |= BF_PXP_OUT_PS_ULC_Y(out_params->height - (proc_data->drect.top + proc_data->drect.height));
			s0param_lrc = BF_PXP_OUT_PS_LRC_X(((s0param_ulc & BM_PXP_OUT_PS_ULC_X) >> 16) + proc_data->drect.width - 1);
			s0param_lrc |= BF_PXP_OUT_PS_LRC_Y((s0param_ulc & BM_PXP_OUT_PS_ULC_Y) + proc_data->drect.height - 1);
			break;
		case 270:
			s0param_ulc = BF_PXP_OUT_PS_ULC_X(out_params->height - (proc_data->drect.top + proc_data->drect.height));
			s0param_ulc |= BF_PXP_OUT_PS_ULC_Y(proc_data->drect.left);
			s0param_lrc = BF_PXP_OUT_PS_LRC_X(((s0param_ulc & BM_PXP_OUT_PS_ULC_X) >> 16) + proc_data->drect.height - 1);
			s0param_lrc |= BF_PXP_OUT_PS_LRC_Y((s0param_ulc & BM_PXP_OUT_PS_ULC_Y) + proc_data->drect.width - 1);
			break;
		default:
			return;
		}
		__raw_writel(s0param_ulc, pxp->base + HW_PXP_OUT_PS_ULC);
		__raw_writel(s0param_lrc, pxp->base + HW_PXP_OUT_PS_LRC);
	}

	/* Since user apps always pass the rotated drect
	 * to this driver, we need to first swap the width
	 * and height which is used to calculate the scale
	 * factors later.
	 */
	if (proc_data->rotate == 90 || proc_data->rotate == 270) {
		int temp;
		temp = proc_data->drect.width;
		proc_data->drect.width = proc_data->drect.height;
		proc_data->drect.height = temp;
	}
}

/* crop behavior is re-designed in h/w. */
static void pxp_set_s0crop(struct pxps *pxp)
{
	/*
	 * place-holder, it's implemented in other functions in this driver.
	 * Refer to "Clipping source images" section in RM for detail.
	 */
}

static int pxp_set_scaling(struct pxps *pxp)
{
	int ret = 0;
	u32 xscale, yscale, s0scale;
	u32 decx, decy, xdec = 0, ydec = 0;
	struct pxp_proc_data *proc_data = &pxp->pxp_conf_state.proc_data;
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_layer_param *s0_params = &pxp_conf->s0_param;
	struct pxp_layer_param *out_params = &pxp_conf->out_param;

	proc_data->scaling = 1;
	decx = proc_data->srect.width / proc_data->drect.width;
	decy = proc_data->srect.height / proc_data->drect.height;
	if (decx > 1) {
		if (decx >= 2 && decx < 4) {
			decx = 2;
			xdec = 1;
		} else if (decx >= 4 && decx < 8) {
			decx = 4;
			xdec = 2;
		} else if (decx >= 8) {
			decx = 8;
			xdec = 3;
		}
		xscale = proc_data->srect.width * 0x1000 /
			 (proc_data->drect.width * decx);
	} else {
		if (!is_yuv(s0_params->pixel_fmt) ||
		    (is_yuv(s0_params->pixel_fmt) ==
		     is_yuv(out_params->pixel_fmt)) ||
		    (s0_params->pixel_fmt == PXP_PIX_FMT_GREY) ||
		    (s0_params->pixel_fmt == PXP_PIX_FMT_GY04) ||
		    (s0_params->pixel_fmt == PXP_PIX_FMT_VUY444)) {
			if ((proc_data->srect.width > 1) &&
			    (proc_data->drect.width > 1))
				xscale = (proc_data->srect.width - 1) * 0x1000 /
					 (proc_data->drect.width - 1);
			else
				xscale = proc_data->srect.width * 0x1000 /
					 proc_data->drect.width;
		} else {
			if ((proc_data->srect.width > 2) &&
			    (proc_data->drect.width > 1))
				xscale = (proc_data->srect.width - 2) * 0x1000 /
					 (proc_data->drect.width - 1);
			else
				xscale = proc_data->srect.width * 0x1000 /
					 proc_data->drect.width;
		}
	}
	if (decy > 1) {
		if (decy >= 2 && decy < 4) {
			decy = 2;
			ydec = 1;
		} else if (decy >= 4 && decy < 8) {
			decy = 4;
			ydec = 2;
		} else if (decy >= 8) {
			decy = 8;
			ydec = 3;
		}
		yscale = proc_data->srect.height * 0x1000 /
			 (proc_data->drect.height * decy);
	} else {
		if ((proc_data->srect.height > 1) &&
		    (proc_data->drect.height > 1))
			yscale = (proc_data->srect.height - 1) * 0x1000 /
				 (proc_data->drect.height - 1);
		else
			yscale = proc_data->srect.height * 0x1000 /
				 proc_data->drect.height;
	}

	__raw_writel((xdec << 10) | (ydec << 8), pxp->base + HW_PXP_PS_CTRL);

	if (xscale > PXP_DOWNSCALE_THRESHOLD)
		xscale = PXP_DOWNSCALE_THRESHOLD;
	if (yscale > PXP_DOWNSCALE_THRESHOLD)
		yscale = PXP_DOWNSCALE_THRESHOLD;
	s0scale = BF_PXP_PS_SCALE_YSCALE(yscale) |
		BF_PXP_PS_SCALE_XSCALE(xscale);
	__raw_writel(s0scale, pxp->base + HW_PXP_PS_SCALE);

	pxp_set_ctrl(pxp);

	return ret;
}

static void pxp_set_bg(struct pxps *pxp)
{
	__raw_writel(pxp->pxp_conf_state.proc_data.bgcolor,
		     pxp->base + HW_PXP_PS_BACKGROUND);
}

static void pxp_set_lut(struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	int lut_op = pxp_conf->proc_data.lut_transform;
	u32 reg_val;
	int i;
	bool use_cmap = (lut_op & PXP_LUT_USE_CMAP) ? true : false;
	u8 *cmap = pxp_conf->proc_data.lut_map;
	u32 entry_src;
	u32 pix_val;
	u8 entry[4];

	/*
	 * If LUT already configured as needed, return...
	 * Unless CMAP is needed and it has been updated.
	 */
	if ((pxp->lut_state == lut_op) &&
		!(use_cmap && pxp_conf->proc_data.lut_map_updated))
		return;

	if (lut_op == PXP_LUT_NONE) {
		__raw_writel(BM_PXP_LUT_CTRL_BYPASS,
			     pxp->base + HW_PXP_LUT_CTRL);
	} else if (((lut_op & PXP_LUT_INVERT) != 0)
		&& ((lut_op & PXP_LUT_BLACK_WHITE) != 0)) {
		/* Fill out LUT table with inverted monochromized values */

		/* clear bypass bit, set lookup mode & out mode */
		__raw_writel(BF_PXP_LUT_CTRL_LOOKUP_MODE
				(BV_PXP_LUT_CTRL_LOOKUP_MODE__DIRECT_Y8) |
				BF_PXP_LUT_CTRL_OUT_MODE
				(BV_PXP_LUT_CTRL_OUT_MODE__Y8),
				pxp->base + HW_PXP_LUT_CTRL);

		/* Initialize LUT address to 0 and set NUM_BYTES to 0 */
		__raw_writel(0, pxp->base + HW_PXP_LUT_ADDR);

		/* LUT address pointer auto-increments after each data write */
		for (pix_val = 0; pix_val < 256; pix_val += 4) {
			for (i = 0; i < 4; i++) {
				entry_src = use_cmap ?
					cmap[pix_val + i] : pix_val + i;
				entry[i] = (entry_src < 0x80) ? 0xFF : 0x00;
			}
			reg_val = (entry[3] << 24) | (entry[2] << 16) |
				(entry[1] << 8) | entry[0];
			__raw_writel(reg_val, pxp->base + HW_PXP_LUT_DATA);
		}
	} else if ((lut_op & PXP_LUT_INVERT) != 0) {
		/* Fill out LUT table with 8-bit inverted values */

		/* clear bypass bit, set lookup mode & out mode */
		__raw_writel(BF_PXP_LUT_CTRL_LOOKUP_MODE
				(BV_PXP_LUT_CTRL_LOOKUP_MODE__DIRECT_Y8) |
				BF_PXP_LUT_CTRL_OUT_MODE
				(BV_PXP_LUT_CTRL_OUT_MODE__Y8),
				pxp->base + HW_PXP_LUT_CTRL);

		/* Initialize LUT address to 0 and set NUM_BYTES to 0 */
		__raw_writel(0, pxp->base + HW_PXP_LUT_ADDR);

		/* LUT address pointer auto-increments after each data write */
		for (pix_val = 0; pix_val < 256; pix_val += 4) {
			for (i = 0; i < 4; i++) {
				entry_src = use_cmap ?
					cmap[pix_val + i] : pix_val + i;
				entry[i] = ~entry_src & 0xFF;
			}
			reg_val = (entry[3] << 24) | (entry[2] << 16) |
				(entry[1] << 8) | entry[0];
			__raw_writel(reg_val, pxp->base + HW_PXP_LUT_DATA);
		}
	} else if ((lut_op & PXP_LUT_BLACK_WHITE) != 0) {
		/* Fill out LUT table with 8-bit monochromized values */

		/* clear bypass bit, set lookup mode & out mode */
		__raw_writel(BF_PXP_LUT_CTRL_LOOKUP_MODE
				(BV_PXP_LUT_CTRL_LOOKUP_MODE__DIRECT_Y8) |
				BF_PXP_LUT_CTRL_OUT_MODE
				(BV_PXP_LUT_CTRL_OUT_MODE__Y8),
				pxp->base + HW_PXP_LUT_CTRL);

		/* Initialize LUT address to 0 and set NUM_BYTES to 0 */
		__raw_writel(0, pxp->base + HW_PXP_LUT_ADDR);

		/* LUT address pointer auto-increments after each data write */
		for (pix_val = 0; pix_val < 256; pix_val += 4) {
			for (i = 0; i < 4; i++) {
				entry_src = use_cmap ?
					cmap[pix_val + i] : pix_val + i;
				entry[i] = (entry_src < 0x80) ? 0x00 : 0xFF;
			}
			reg_val = (entry[3] << 24) | (entry[2] << 16) |
				(entry[1] << 8) | entry[0];
			__raw_writel(reg_val, pxp->base + HW_PXP_LUT_DATA);
		}
	} else if (use_cmap) {
		/* Fill out LUT table using colormap values */

		/* clear bypass bit, set lookup mode & out mode */
		__raw_writel(BF_PXP_LUT_CTRL_LOOKUP_MODE
				(BV_PXP_LUT_CTRL_LOOKUP_MODE__DIRECT_Y8) |
				BF_PXP_LUT_CTRL_OUT_MODE
				(BV_PXP_LUT_CTRL_OUT_MODE__Y8),
				pxp->base + HW_PXP_LUT_CTRL);

		/* Initialize LUT address to 0 and set NUM_BYTES to 0 */
		__raw_writel(0, pxp->base + HW_PXP_LUT_ADDR);

		/* LUT address pointer auto-increments after each data write */
		for (pix_val = 0; pix_val < 256; pix_val += 4) {
			for (i = 0; i < 4; i++)
				entry[i] = cmap[pix_val + i];
			reg_val = (entry[3] << 24) | (entry[2] << 16) |
				(entry[1] << 8) | entry[0];
			__raw_writel(reg_val, pxp->base + HW_PXP_LUT_DATA);
		}
	}

	pxp->lut_state = lut_op;
}

static void pxp_set_csc(struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_layer_param *s0_params = &pxp_conf->s0_param;
	struct pxp_layer_param *ol_params = &pxp_conf->ol_param[0];
	struct pxp_layer_param *out_params = &pxp_conf->out_param;

	bool input_is_YUV = is_yuv(s0_params->pixel_fmt);
	bool output_is_YUV = is_yuv(out_params->pixel_fmt);

	if (input_is_YUV && output_is_YUV) {
		/*
		 * Input = YUV, Output = YUV
		 * No CSC unless we need to do combining
		 */
		if (ol_params->combine_enable) {
			/* Must convert to RGB for combining with RGB overlay */

			/* CSC1 - YUV->RGB */
			__raw_writel(0x04030000, pxp->base + HW_PXP_CSC1_COEF0);
			__raw_writel(0x01230208, pxp->base + HW_PXP_CSC1_COEF1);
			__raw_writel(0x076b079c, pxp->base + HW_PXP_CSC1_COEF2);

			/* CSC2 - RGB->YUV */
			__raw_writel(0x4, pxp->base + HW_PXP_CSC2_CTRL);
			__raw_writel(0x0096004D, pxp->base + HW_PXP_CSC2_COEF0);
			__raw_writel(0x05DA001D, pxp->base + HW_PXP_CSC2_COEF1);
			__raw_writel(0x007005B6, pxp->base + HW_PXP_CSC2_COEF2);
			__raw_writel(0x057C009E, pxp->base + HW_PXP_CSC2_COEF3);
			__raw_writel(0x000005E6, pxp->base + HW_PXP_CSC2_COEF4);
			__raw_writel(0x00000000, pxp->base + HW_PXP_CSC2_COEF5);
		} else {
			/* Input & Output both YUV, so bypass both CSCs */

			/* CSC1 - Bypass */
			__raw_writel(0x40000000, pxp->base + HW_PXP_CSC1_COEF0);

			/* CSC2 - Bypass */
			__raw_writel(0x1, pxp->base + HW_PXP_CSC2_CTRL);
		}
	} else if (input_is_YUV && !output_is_YUV) {
		/*
		 * Input = YUV, Output = RGB
		 * Use CSC1 to convert to RGB
		 */

		/* CSC1 - YUV->RGB */
		__raw_writel(0x84ab01f0, pxp->base + HW_PXP_CSC1_COEF0);
		__raw_writel(0x01980204, pxp->base + HW_PXP_CSC1_COEF1);
		__raw_writel(0x0730079c, pxp->base + HW_PXP_CSC1_COEF2);

		/* CSC2 - Bypass */
		__raw_writel(0x1, pxp->base + HW_PXP_CSC2_CTRL);
	} else if (!input_is_YUV && output_is_YUV) {
		/*
		 * Input = RGB, Output = YUV
		 * Use CSC2 to convert to YUV
		 */

		/* CSC1 - Bypass */
		__raw_writel(0x40000000, pxp->base + HW_PXP_CSC1_COEF0);

		/* CSC2 - RGB->YUV */
		__raw_writel(0x4, pxp->base + HW_PXP_CSC2_CTRL);
		__raw_writel(0x0096004D, pxp->base + HW_PXP_CSC2_COEF0);
		__raw_writel(0x05DA001D, pxp->base + HW_PXP_CSC2_COEF1);
		__raw_writel(0x007005B6, pxp->base + HW_PXP_CSC2_COEF2);
		__raw_writel(0x057C009E, pxp->base + HW_PXP_CSC2_COEF3);
		__raw_writel(0x000005E6, pxp->base + HW_PXP_CSC2_COEF4);
		__raw_writel(0x00000000, pxp->base + HW_PXP_CSC2_COEF5);
	} else {
		/*
		 * Input = RGB, Output = RGB
		 * Input & Output both RGB, so bypass both CSCs
		 */

		/* CSC1 - Bypass */
		__raw_writel(0x40000000, pxp->base + HW_PXP_CSC1_COEF0);

		/* CSC2 - Bypass */
		__raw_writel(0x1, pxp->base + HW_PXP_CSC2_CTRL);
	}

	/* YCrCb colorspace */
	/* Not sure when we use this...no YCrCb formats are defined for PxP */
	/*
	   __raw_writel(0x84ab01f0, HW_PXP_CSCCOEFF0_ADDR);
	   __raw_writel(0x01230204, HW_PXP_CSCCOEFF1_ADDR);
	   __raw_writel(0x0730079c, HW_PXP_CSCCOEFF2_ADDR);
	 */

}

static void pxp_set_s0buf(struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_layer_param *s0_params = &pxp_conf->s0_param;
	struct pxp_proc_data *proc_data = &pxp_conf->proc_data;
	dma_addr_t Y, U, V;
	dma_addr_t Y1, U1, V1;
	u32 offset, bpp = 1;
	u32 pitch = s0_params->stride ? s0_params->stride :
					s0_params->width;

	Y = s0_params->paddr;

	if ((s0_params->pixel_fmt == PXP_PIX_FMT_RGB565) ||
		(s0_params->pixel_fmt == PXP_PIX_FMT_RGB555))
		bpp = 2;
	else if (s0_params->pixel_fmt == PXP_PIX_FMT_RGB32)
		bpp = 4;
	offset = (proc_data->srect.top * s0_params->width +
		 proc_data->srect.left) * bpp;
	/* clipping or cropping */
	Y1 = Y + offset;
	__raw_writel(Y1, pxp->base + HW_PXP_PS_BUF);
	if ((s0_params->pixel_fmt == PXP_PIX_FMT_YUV420P) ||
	    (s0_params->pixel_fmt == PXP_PIX_FMT_YVU420P) ||
	    (s0_params->pixel_fmt == PXP_PIX_FMT_GREY)    ||
	    (s0_params->pixel_fmt == PXP_PIX_FMT_YUV422P)) {
		/* Set to 1 if YUV format is 4:2:2 rather than 4:2:0 */
		int s = 2;
		if (s0_params->pixel_fmt == PXP_PIX_FMT_YUV422P)
			s = 1;

		offset = proc_data->srect.top * s0_params->width / 4 +
			 proc_data->srect.left / 2;
		U = Y + (s0_params->width * s0_params->height);
		U1 = U + offset;
		V = U + ((s0_params->width * s0_params->height) >> s);
		V1 = V + offset;
		if (s0_params->pixel_fmt == PXP_PIX_FMT_YVU420P) {
			__raw_writel(V1, pxp->base + HW_PXP_PS_UBUF);
			__raw_writel(U1, pxp->base + HW_PXP_PS_VBUF);
		} else {
			__raw_writel(U1, pxp->base + HW_PXP_PS_UBUF);
			__raw_writel(V1, pxp->base + HW_PXP_PS_VBUF);
		}
	} else if ((s0_params->pixel_fmt == PXP_PIX_FMT_NV12) ||
		 (s0_params->pixel_fmt == PXP_PIX_FMT_NV21) ||
		 (s0_params->pixel_fmt == PXP_PIX_FMT_NV16) ||
		 (s0_params->pixel_fmt == PXP_PIX_FMT_NV61)) {
		int s = 2;
		if ((s0_params->pixel_fmt == PXP_PIX_FMT_NV16) ||
		    (s0_params->pixel_fmt == PXP_PIX_FMT_NV61))
			s = 1;

		offset = (proc_data->srect.top * s0_params->width +
			  proc_data->srect.left) / s;
		U = Y + (s0_params->width * s0_params->height);
		U1 = U + offset;

		__raw_writel(U1, pxp->base + HW_PXP_PS_UBUF);
	}

	/* TODO: only support RGB565, Y8, Y4, YUV420 */
	if (s0_params->pixel_fmt == PXP_PIX_FMT_GREY ||
	    s0_params->pixel_fmt == PXP_PIX_FMT_YUV420P ||
	    s0_params->pixel_fmt == PXP_PIX_FMT_YVU420P ||
	    s0_params->pixel_fmt == PXP_PIX_FMT_NV12 ||
	    s0_params->pixel_fmt == PXP_PIX_FMT_NV21 ||
	    s0_params->pixel_fmt == PXP_PIX_FMT_NV16 ||
	    s0_params->pixel_fmt == PXP_PIX_FMT_NV61 ||
	    s0_params->pixel_fmt == PXP_PIX_FMT_YUV422P) {
		__raw_writel(pitch, pxp->base + HW_PXP_PS_PITCH);
	}
	else if (s0_params->pixel_fmt == PXP_PIX_FMT_GY04)
		__raw_writel(pitch >> 1,
				pxp->base + HW_PXP_PS_PITCH);
	else if (s0_params->pixel_fmt == PXP_PIX_FMT_RGB32 ||
			 s0_params->pixel_fmt == PXP_PIX_FMT_VUY444)
		__raw_writel(pitch << 2,
				pxp->base + HW_PXP_PS_PITCH);
	else if (s0_params->pixel_fmt == PXP_PIX_FMT_UYVY ||
		 s0_params->pixel_fmt == PXP_PIX_FMT_YUYV ||
		 s0_params->pixel_fmt == PXP_PIX_FMT_VYUY ||
		 s0_params->pixel_fmt == PXP_PIX_FMT_YVYU)
		__raw_writel(pitch << 1,
				pxp->base + HW_PXP_PS_PITCH);
	else if ((s0_params->pixel_fmt == PXP_PIX_FMT_RGB565) ||
		 (s0_params->pixel_fmt == PXP_PIX_FMT_RGB555))
		__raw_writel(pitch << 1,
				pxp->base + HW_PXP_PS_PITCH);
	else
		__raw_writel(0, pxp->base + HW_PXP_PS_PITCH);
}

/**
 * pxp_config() - configure PxP for a processing task
 * @pxps:	PXP context.
 * @pxp_chan:	PXP channel.
 * @return:	0 on success or negative error code on failure.
 */
static int pxp_config(struct pxps *pxp, struct pxp_channel *pxp_chan)
{
	struct pxp_config_data *pxp_conf_data = &pxp->pxp_conf_state;
	int ol_nr;
	int i;

	/* Configure PxP regs */
	pxp_set_ctrl(pxp);
	pxp_set_s0param(pxp);
	pxp_set_s0crop(pxp);
	pxp_set_scaling(pxp);
	ol_nr = pxp_conf_data->layer_nr - 2;
	while (ol_nr > 0) {
		i = pxp_conf_data->layer_nr - 2 - ol_nr;
		pxp_set_oln(i, pxp);
		pxp_set_olparam(i, pxp);
		/* only the color key in higher overlay will take effect. */
		pxp_set_olcolorkey(i, pxp);
		ol_nr--;
	}
	pxp_set_s0colorkey(pxp);
	pxp_set_csc(pxp);
	pxp_set_bg(pxp);
	pxp_set_lut(pxp);

	pxp_set_s0buf(pxp);
	pxp_set_outbuf(pxp);

	return 0;
}

static void pxp_clk_enable(struct pxps *pxp)
{
	mutex_lock(&pxp->clk_mutex);

	if (pxp->clk_stat == CLK_STAT_ON) {
		mutex_unlock(&pxp->clk_mutex);
		return;
	}

	pm_runtime_get_sync(pxp->dev);

	if (pxp->clk_disp_axi)
		clk_prepare_enable(pxp->clk_disp_axi);
	clk_prepare_enable(pxp->clk);
	pxp->clk_stat = CLK_STAT_ON;

	mutex_unlock(&pxp->clk_mutex);
}

static void pxp_clk_disable(struct pxps *pxp)
{
	unsigned long flags;

	mutex_lock(&pxp->clk_mutex);

	if (pxp->clk_stat == CLK_STAT_OFF) {
		mutex_unlock(&pxp->clk_mutex);
		return;
	}

	spin_lock_irqsave(&pxp->lock, flags);
	if ((pxp->pxp_ongoing == 0) && list_empty(&head)) {
		spin_unlock_irqrestore(&pxp->lock, flags);
		clk_disable_unprepare(pxp->clk);
		if (pxp->clk_disp_axi)
			clk_disable_unprepare(pxp->clk_disp_axi);
		pxp->clk_stat = CLK_STAT_OFF;
	} else
		spin_unlock_irqrestore(&pxp->lock, flags);

	pm_runtime_put_sync_suspend(pxp->dev);

	mutex_unlock(&pxp->clk_mutex);
}

static inline void clkoff_callback(struct work_struct *w)
{
	struct pxps *pxp = container_of(w, struct pxps, work);

	pxp_clk_disable(pxp);
}

static void pxp_clkoff_timer(unsigned long arg)
{
	struct pxps *pxp = (struct pxps *)arg;

	if ((pxp->pxp_ongoing == 0) && list_empty(&head))
		schedule_work(&pxp->work);
	else
		mod_timer(&pxp->clk_timer,
			  jiffies + msecs_to_jiffies(timeout_in_ms));
}

static struct pxp_tx_desc *pxpdma_first_queued(struct pxp_channel *pxp_chan)
{
	return list_entry(pxp_chan->queue.next, struct pxp_tx_desc, list);
}

/* called with pxp_chan->lock held */
static void __pxpdma_dostart(struct pxp_channel *pxp_chan)
{
	struct pxp_dma *pxp_dma = to_pxp_dma(pxp_chan->dma_chan.device);
	struct pxps *pxp = to_pxp(pxp_dma);
	struct pxp_tx_desc *desc;
	struct pxp_tx_desc *child;
	int i = 0;

	memset(&pxp->pxp_conf_state.s0_param, 0,  sizeof(struct pxp_layer_param));
	memset(&pxp->pxp_conf_state.out_param, 0,  sizeof(struct pxp_layer_param));
	memset(pxp->pxp_conf_state.ol_param, 0,  sizeof(struct pxp_layer_param) * 8);
	memset(&pxp->pxp_conf_state.proc_data, 0,  sizeof(struct pxp_proc_data));
	/* S0 */
	desc = list_first_entry(&head, struct pxp_tx_desc, list);
	memcpy(&pxp->pxp_conf_state.s0_param,
	       &desc->layer_param.s0_param, sizeof(struct pxp_layer_param));
	memcpy(&pxp->pxp_conf_state.proc_data,
	       &desc->proc_data, sizeof(struct pxp_proc_data));

	/* Save PxP configuration */
	list_for_each_entry(child, &desc->tx_list, list) {
		if (i == 0) {	/* Output */
			memcpy(&pxp->pxp_conf_state.out_param,
			       &child->layer_param.out_param,
			       sizeof(struct pxp_layer_param));
		} else {	/* Overlay */
			memcpy(&pxp->pxp_conf_state.ol_param[i - 1],
			       &child->layer_param.ol_param,
			       sizeof(struct pxp_layer_param));
		}

		i++;
	}
	pr_debug("%s:%d S0 w/h %d/%d paddr %08x\n", __func__, __LINE__,
		 pxp->pxp_conf_state.s0_param.width,
		 pxp->pxp_conf_state.s0_param.height,
		 pxp->pxp_conf_state.s0_param.paddr);
	pr_debug("%s:%d OUT w/h %d/%d paddr %08x\n", __func__, __LINE__,
		 pxp->pxp_conf_state.out_param.width,
		 pxp->pxp_conf_state.out_param.height,
		 pxp->pxp_conf_state.out_param.paddr);
}

static void pxpdma_dostart_work(struct pxps *pxp)
{
	struct pxp_channel *pxp_chan = NULL;
	unsigned long flags;
	struct pxp_tx_desc *desc = NULL;

	spin_lock_irqsave(&pxp->lock, flags);

	desc = list_entry(head.next, struct pxp_tx_desc, list);
	pxp_chan = to_pxp_channel(desc->txd.chan);

	__pxpdma_dostart(pxp_chan);

	/* Configure PxP */
	pxp_config(pxp, pxp_chan);

	pxp_start(pxp);

	spin_unlock_irqrestore(&pxp->lock, flags);
}

static void pxpdma_dequeue(struct pxp_channel *pxp_chan, struct pxps *pxp)
{
	unsigned long flags;
	struct pxp_tx_desc *desc = NULL;

	do {
		desc = pxpdma_first_queued(pxp_chan);
		spin_lock_irqsave(&pxp->lock, flags);
		list_move_tail(&desc->list, &head);
		spin_unlock_irqrestore(&pxp->lock, flags);
	} while (!list_empty(&pxp_chan->queue));
}

static dma_cookie_t pxp_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct pxp_tx_desc *desc = to_tx_desc(tx);
	struct pxp_channel *pxp_chan = to_pxp_channel(tx->chan);
	dma_cookie_t cookie;

	dev_dbg(&pxp_chan->dma_chan.dev->device, "received TX\n");

	/* pxp_chan->lock can be taken under ichan->lock, but not v.v. */
	spin_lock(&pxp_chan->lock);

	cookie = pxp_chan->dma_chan.cookie;

	if (++cookie < 0)
		cookie = 1;

	/* from dmaengine.h: "last cookie value returned to client" */
	pxp_chan->dma_chan.cookie = cookie;
	tx->cookie = cookie;

	/* Here we add the tx descriptor to our PxP task queue. */
	list_add_tail(&desc->list, &pxp_chan->queue);

	spin_unlock(&pxp_chan->lock);

	dev_dbg(&pxp_chan->dma_chan.dev->device, "done TX\n");

	return cookie;
}

/**
 * pxp_init_channel() - initialize a PXP channel.
 * @pxp_dma:   PXP DMA context.
 * @pchan:  pointer to the channel object.
 * @return      0 on success or negative error code on failure.
 */
static int pxp_init_channel(struct pxp_dma *pxp_dma,
			    struct pxp_channel *pxp_chan)
{
	int ret = 0;

	/*
	 * We are using _virtual_ channel here.
	 * Each channel contains all parameters of corresponding layers
	 * for one transaction; each layer is represented as one descriptor
	 * (i.e., pxp_tx_desc) here.
	 */

	INIT_LIST_HEAD(&pxp_chan->queue);

	return ret;
}

static irqreturn_t pxp_irq(int irq, void *dev_id)
{
	struct pxps *pxp = dev_id;
	struct pxp_channel *pxp_chan;
	struct pxp_tx_desc *desc;
	struct pxp_tx_desc *child, *_child;
	dma_async_tx_callback callback;
	void *callback_param;
	unsigned long flags;
	u32 hist_status;

	dump_pxp_reg(pxp);

	hist_status =
	    __raw_readl(pxp->base + HW_PXP_HIST_CTRL) & BM_PXP_HIST_CTRL_STATUS;

	__raw_writel(BM_PXP_STAT_IRQ, pxp->base + HW_PXP_STAT_CLR);

	/* set the SFTRST bit to be 1 to reset
	 * the PXP block to its default state.
	 */
	pxp_soft_reset(pxp);

	spin_lock_irqsave(&pxp->lock, flags);

	if (list_empty(&head)) {
		pxp->pxp_ongoing = 0;
		spin_unlock_irqrestore(&pxp->lock, flags);
		return IRQ_NONE;
	}

	/* Get descriptor and call callback */
	desc = list_entry(head.next, struct pxp_tx_desc, list);
	pxp_chan = to_pxp_channel(desc->txd.chan);

	pxp_chan->completed = desc->txd.cookie;

	callback = desc->txd.callback;
	callback_param = desc->txd.callback_param;

	/* Send histogram status back to caller */
	desc->hist_status = hist_status;

	if ((desc->txd.flags & DMA_PREP_INTERRUPT) && callback)
		callback(callback_param);

	pxp_chan->status = PXP_CHANNEL_INITIALIZED;

	list_for_each_entry_safe(child, _child, &desc->tx_list, list) {
		list_del_init(&child->list);
		kmem_cache_free(tx_desc_cache, (void *)child);
	}
	list_del_init(&desc->list);
	kmem_cache_free(tx_desc_cache, (void *)desc);

	complete(&pxp->complete);
	pxp->pxp_ongoing = 0;
	mod_timer(&pxp->clk_timer, jiffies + msecs_to_jiffies(timeout_in_ms));

	spin_unlock_irqrestore(&pxp->lock, flags);

	return IRQ_HANDLED;
}

/* allocate/free dma tx descriptor dynamically*/
static struct pxp_tx_desc *pxpdma_desc_alloc(struct pxp_channel *pxp_chan)
{
	struct pxp_tx_desc *desc = NULL;
	struct dma_async_tx_descriptor *txd = NULL;

	desc = kmem_cache_alloc(tx_desc_cache, GFP_KERNEL | __GFP_ZERO);
	if (desc == NULL)
		return NULL;

	INIT_LIST_HEAD(&desc->list);
	INIT_LIST_HEAD(&desc->tx_list);
	txd = &desc->txd;
	dma_async_tx_descriptor_init(txd, &pxp_chan->dma_chan);
	txd->tx_submit = pxp_tx_submit;

	return desc;
}

/* Allocate and initialise a transfer descriptor. */
static struct dma_async_tx_descriptor *pxp_prep_slave_sg(struct dma_chan *chan,
							 struct scatterlist
							 *sgl,
							 unsigned int sg_len,
							 enum
							 dma_transfer_direction
							 direction,
							 unsigned long tx_flags,
							 void *context)
{
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);
	struct pxp_dma *pxp_dma = to_pxp_dma(chan->device);
	struct pxps *pxp = to_pxp(pxp_dma);
	struct pxp_tx_desc *desc = NULL;
	struct pxp_tx_desc *first = NULL, *prev = NULL;
	struct scatterlist *sg;
	dma_addr_t phys_addr;
	int i;

	if (direction != DMA_DEV_TO_MEM && direction != DMA_MEM_TO_DEV) {
		dev_err(chan->device->dev, "Invalid DMA direction %d!\n",
			direction);
		return NULL;
	}

	if (unlikely(sg_len < 2))
		return NULL;

	for_each_sg(sgl, sg, sg_len, i) {
		desc = pxpdma_desc_alloc(pxp_chan);
		if (!desc) {
			dev_err(chan->device->dev, "no enough memory to allocate tx descriptor\n");
			return NULL;
		}

		phys_addr = sg_dma_address(sg);

		if (!first) {
			first = desc;

			desc->layer_param.s0_param.paddr = phys_addr;
		} else {
			list_add_tail(&desc->list, &first->tx_list);
			prev->next = desc;
			desc->next = NULL;

			if (i == 1)
				desc->layer_param.out_param.paddr = phys_addr;
			else
				desc->layer_param.ol_param.paddr = phys_addr;
		}

		prev = desc;
	}

	pxp->pxp_conf_state.layer_nr = sg_len;
	first->txd.flags = tx_flags;
	first->len = sg_len;
	pr_debug("%s:%d first %p, first->len %d, flags %08x\n",
		 __func__, __LINE__, first, first->len, first->txd.flags);

	return &first->txd;
}

static void pxp_issue_pending(struct dma_chan *chan)
{
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);
	struct pxp_dma *pxp_dma = to_pxp_dma(chan->device);
	struct pxps *pxp = to_pxp(pxp_dma);

	spin_lock(&pxp_chan->lock);

	if (list_empty(&pxp_chan->queue)) {
		spin_unlock(&pxp_chan->lock);
		return;
	}

	pxpdma_dequeue(pxp_chan, pxp);
	pxp_chan->status = PXP_CHANNEL_READY;

	spin_unlock(&pxp_chan->lock);

	pxp_clk_enable(pxp);
	wake_up_interruptible(&pxp->thread_waitq);
}

static void __pxp_terminate_all(struct dma_chan *chan)
{
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);

	pxp_chan->status = PXP_CHANNEL_INITIALIZED;
}

static int pxp_device_terminate_all(struct dma_chan *chan)
{
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);

	spin_lock(&pxp_chan->lock);
	__pxp_terminate_all(chan);
	spin_unlock(&pxp_chan->lock);

	return 0;
}

static int pxp_alloc_chan_resources(struct dma_chan *chan)
{
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);
	struct pxp_dma *pxp_dma = to_pxp_dma(chan->device);
	int ret;

	/* dmaengine.c now guarantees to only offer free channels */
	BUG_ON(chan->client_count > 1);
	WARN_ON(pxp_chan->status != PXP_CHANNEL_FREE);

	chan->cookie = 1;
	pxp_chan->completed = -ENXIO;

	pr_debug("%s dma_chan.chan_id %d\n", __func__, chan->chan_id);
	ret = pxp_init_channel(pxp_dma, pxp_chan);
	if (ret < 0)
		goto err_chan;

	pxp_chan->status = PXP_CHANNEL_INITIALIZED;

	dev_dbg(&chan->dev->device, "Found channel 0x%x, irq %d\n",
		chan->chan_id, pxp_chan->eof_irq);

	return ret;

err_chan:
	return ret;
}

static void pxp_free_chan_resources(struct dma_chan *chan)
{
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);

	spin_lock(&pxp_chan->lock);

	__pxp_terminate_all(chan);

	pxp_chan->status = PXP_CHANNEL_FREE;

	spin_unlock(&pxp_chan->lock);
}

static enum dma_status pxp_tx_status(struct dma_chan *chan,
				     dma_cookie_t cookie,
				     struct dma_tx_state *txstate)
{
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);

	if (cookie != chan->cookie)
		return DMA_ERROR;

	if (txstate) {
		txstate->last = pxp_chan->completed;
		txstate->used = chan->cookie;
		txstate->residue = 0;
	}
	return DMA_COMPLETE;
}

static int pxp_dma_init(struct pxps *pxp)
{
	struct pxp_dma *pxp_dma = &pxp->pxp_dma;
	struct dma_device *dma = &pxp_dma->dma;
	int i;

	dma_cap_set(DMA_SLAVE, dma->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma->cap_mask);

	/* Compulsory common fields */
	dma->dev = pxp->dev;
	dma->device_alloc_chan_resources = pxp_alloc_chan_resources;
	dma->device_free_chan_resources = pxp_free_chan_resources;
	dma->device_tx_status = pxp_tx_status;
	dma->device_issue_pending = pxp_issue_pending;

	/* Compulsory for DMA_SLAVE fields */
	dma->device_prep_slave_sg = pxp_prep_slave_sg;
	dma->device_terminate_all = pxp_device_terminate_all;

	/* Initialize PxP Channels */
	INIT_LIST_HEAD(&dma->channels);
	for (i = 0; i < NR_PXP_VIRT_CHANNEL; i++) {
		struct pxp_channel *pxp_chan = pxp->channel + i;
		struct dma_chan *dma_chan = &pxp_chan->dma_chan;

		spin_lock_init(&pxp_chan->lock);

		/* Only one EOF IRQ for PxP, shared by all channels */
		pxp_chan->eof_irq = pxp->irq;
		pxp_chan->status = PXP_CHANNEL_FREE;
		pxp_chan->completed = -ENXIO;
		snprintf(pxp_chan->eof_name, sizeof(pxp_chan->eof_name),
			 "PXP EOF %d", i);

		dma_chan->device = &pxp_dma->dma;
		dma_chan->cookie = 1;
		dma_chan->chan_id = i;
		list_add_tail(&dma_chan->device_node, &dma->channels);
	}

	return dma_async_device_register(&pxp_dma->dma);
}

static ssize_t clk_off_timeout_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", timeout_in_ms);
}

static ssize_t clk_off_timeout_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int val;
	if (sscanf(buf, "%d", &val) > 0) {
		timeout_in_ms = val;
		return count;
	}
	return -EINVAL;
}

static DEVICE_ATTR(clk_off_timeout, 0644, clk_off_timeout_show,
		   clk_off_timeout_store);

static ssize_t block_size_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return sprintf(buf, "%d\n", block_size);
}

static ssize_t block_size_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	char **last = NULL;

	block_size = simple_strtoul(buf, last, 0);
	if (block_size > 1)
		block_size = 1;

	return count;
}
static DEVICE_ATTR(block_size, S_IWUSR | S_IRUGO,
		   block_size_show, block_size_store);

static const struct of_device_id imx_pxpdma_dt_ids[] = {
	{ .compatible = "fsl,imx6dl-pxp-dma", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_pxpdma_dt_ids);

static int has_pending_task(struct pxps *pxp, struct pxp_channel *task)
{
	int found;
	unsigned long flags;

	spin_lock_irqsave(&pxp->lock, flags);
	found = !list_empty(&head);
	spin_unlock_irqrestore(&pxp->lock, flags);

	return found;
}

static int pxp_dispatch_thread(void *argv)
{
	struct pxps *pxp = (struct pxps *)argv;
	struct pxp_channel *pending = NULL;
	unsigned long flags;

	set_freezable();

	while (!kthread_should_stop()) {
		int ret;
		ret = wait_event_freezable(pxp->thread_waitq,
					has_pending_task(pxp, pending) ||
					kthread_should_stop());
		if (ret < 0)
			continue;

		if (kthread_should_stop())
			break;

		spin_lock_irqsave(&pxp->lock, flags);
		pxp->pxp_ongoing = 1;
		spin_unlock_irqrestore(&pxp->lock, flags);
		init_completion(&pxp->complete);
		pxpdma_dostart_work(pxp);
		ret = wait_for_completion_timeout(&pxp->complete, 2 * HZ);
		if (ret == 0) {
			printk(KERN_EMERG "%s: task is timeout\n\n", __func__);
			break;
		}
	}

	return 0;
}

static int pxp_probe(struct platform_device *pdev)
{
	struct pxps *pxp;
	struct resource *res;
	int irq;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0) {
		err = -ENODEV;
		goto exit;
	}

	pxp = devm_kzalloc(&pdev->dev, sizeof(*pxp), GFP_KERNEL);
	if (!pxp) {
		dev_err(&pdev->dev, "failed to allocate control object\n");
		err = -ENOMEM;
		goto exit;
	}

	pxp->dev = &pdev->dev;

	platform_set_drvdata(pdev, pxp);
	pxp->irq = irq;

	pxp->pxp_ongoing = 0;
	pxp->lut_state = 0;

	spin_lock_init(&pxp->lock);
	mutex_init(&pxp->clk_mutex);

	pxp->base = devm_ioremap_resource(&pdev->dev, res);
	if (pxp->base == NULL) {
		dev_err(&pdev->dev, "Couldn't ioremap regs\n");
		err = -ENODEV;
		goto exit;
	}

	pxp->pdev = pdev;

	pxp->clk_disp_axi = devm_clk_get(&pdev->dev, "disp-axi");
	if (IS_ERR(pxp->clk_disp_axi))
		pxp->clk_disp_axi = NULL;
	pxp->clk = devm_clk_get(&pdev->dev, "pxp-axi");

	err = devm_request_irq(&pdev->dev, pxp->irq, pxp_irq, 0,
				"pxp-dmaengine", pxp);
	if (err)
		goto exit;
	/* Initialize DMA engine */
	err = pxp_dma_init(pxp);
	if (err < 0)
		goto exit;

	if (device_create_file(&pdev->dev, &dev_attr_clk_off_timeout)) {
		dev_err(&pdev->dev,
			"Unable to create file from clk_off_timeout\n");
		goto exit;
	}

	device_create_file(&pdev->dev, &dev_attr_block_size);
	pxp_clk_enable(pxp);
	dump_pxp_reg(pxp);
	pxp_clk_disable(pxp);

	INIT_WORK(&pxp->work, clkoff_callback);
	init_timer(&pxp->clk_timer);
	pxp->clk_timer.function = pxp_clkoff_timer;
	pxp->clk_timer.data = (unsigned long)pxp;

	init_waitqueue_head(&pxp->thread_waitq);
	/* allocate a kernel thread to dispatch pxp conf */
	pxp->dispatch = kthread_run(pxp_dispatch_thread, pxp, "pxp_dispatch");
	if (IS_ERR(pxp->dispatch)) {
		err = PTR_ERR(pxp->dispatch);
		goto exit;
	}
	tx_desc_cache = kmem_cache_create("tx_desc", sizeof(struct pxp_tx_desc),
					  0, SLAB_HWCACHE_ALIGN, NULL);
	if (!tx_desc_cache) {
		err = -ENOMEM;
		goto exit;
	}

	register_pxp_device();

	pm_runtime_enable(pxp->dev);

exit:
	if (err)
		dev_err(&pdev->dev, "Exiting (unsuccessfully) pxp_probe()\n");
	return err;
}

static int pxp_remove(struct platform_device *pdev)
{
	struct pxps *pxp = platform_get_drvdata(pdev);

	unregister_pxp_device();
	kmem_cache_destroy(tx_desc_cache);
	kthread_stop(pxp->dispatch);
	cancel_work_sync(&pxp->work);
	del_timer_sync(&pxp->clk_timer);
	clk_disable_unprepare(pxp->clk);
	if (pxp->clk_disp_axi)
		clk_disable_unprepare(pxp->clk_disp_axi);
	device_remove_file(&pdev->dev, &dev_attr_clk_off_timeout);
	device_remove_file(&pdev->dev, &dev_attr_block_size);
	dma_async_device_unregister(&(pxp->pxp_dma.dma));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pxp_suspend(struct device *dev)
{
	struct pxps *pxp = dev_get_drvdata(dev);

	pxp_clk_enable(pxp);
	while (__raw_readl(pxp->base + HW_PXP_CTRL) & BM_PXP_CTRL_ENABLE)
		;

	__raw_writel(BM_PXP_CTRL_SFTRST, pxp->base + HW_PXP_CTRL);
	pxp_clk_disable(pxp);

	return 0;
}

static int pxp_resume(struct device *dev)
{
	struct pxps *pxp = dev_get_drvdata(dev);

	pxp_clk_enable(pxp);
	/* Pull PxP out of reset */
	__raw_writel(0, pxp->base + HW_PXP_CTRL);
	pxp_clk_disable(pxp);

	return 0;
}
#else
#define	pxp_suspend	NULL
#define	pxp_resume	NULL
#endif

#ifdef CONFIG_PM
static int pxp_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "pxp busfreq high release.\n");
	return 0;
}

static int pxp_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "pxp busfreq high request.\n");
	return 0;
}
#else
#define	pxp_runtime_suspend	NULL
#define	pxp_runtime_resume	NULL
#endif

static const struct dev_pm_ops pxp_pm_ops = {
	SET_RUNTIME_PM_OPS(pxp_runtime_suspend, pxp_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pxp_suspend, pxp_resume)
};

static struct platform_driver pxp_driver = {
	.driver = {
			.name = "imx-pxp",
			.of_match_table = of_match_ptr(imx_pxpdma_dt_ids),
			.pm = &pxp_pm_ops,
		   },
	.probe = pxp_probe,
	.remove = pxp_remove,
};

module_platform_driver(pxp_driver);


MODULE_DESCRIPTION("i.MX PxP driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
