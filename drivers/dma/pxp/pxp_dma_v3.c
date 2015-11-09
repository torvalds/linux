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

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/dmaengine.h>
#include <linux/pxp_dma.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/of.h>

#include "regs-pxp_v3.h"

#ifdef CONFIG_MXC_FPGA_M4_TEST
#include "cm4_image.c"
#define FPGA_TCML_ADDR        0x0C7F8000
#define PINCTRL               0x0C018000
#define PIN_DOUT              0x700
void __iomem *fpga_tcml_base;
void __iomem *pinctrl_base;
#endif


#define PXP_FILL_TIMEOUT	3000
#define busy_wait(cond)							\
	({                                                              \
	unsigned long end_jiffies = jiffies + 				\
			msecs_to_jiffies(PXP_FILL_TIMEOUT);         	\
	bool succeeded = false;                                         \
	do {                                                            \
		if (cond) {                                             \
			succeeded = true;                               \
			break;                                          \
		}                                                       \
		cpu_relax();                                            \
	} while (time_after(end_jiffies, jiffies));                   	\
		succeeded;                                              \
	})

#define	PXP_DOWNSCALE_THRESHOLD		0x4000

#define CONFIG_FB_MXC_EINK_FPGA

static LIST_HEAD(head);
static int timeout_in_ms = 600;
static unsigned int block_size;
static struct pxp_collision_info col_info;

struct pxp_dma {
	struct dma_device dma;
};

struct pxps {
	struct platform_device *pdev;
	struct clk *clk;
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
	struct semaphore sema;
};

#define to_pxp_dma(d) container_of(d, struct pxp_dma, dma)
#define to_tx_desc(tx) container_of(tx, struct pxp_tx_desc, txd)
#define to_pxp_channel(d) container_of(d, struct pxp_channel, dma_chan)
#define to_pxp(id) container_of(id, struct pxps, pxp_dma)

#define PXP_DEF_BUFS	2
#define PXP_MIN_PIX	8
static void __iomem *pxp_reg_base;

static __attribute__((aligned (1024*4))) unsigned int active_matrix_data_8x8[64]={
   0x06050100, 0x04030207, 0x06050100, 0x04030207,
   0x00040302, 0x07060501, 0x00040302, 0x07060501,
   0x02070605, 0x01000403, 0x02070605, 0x01000403,
   0x05010004, 0x03020706, 0x05010004, 0x03020706,
   0x04030207, 0x06050100, 0x04030207, 0x06050100,
   0x07060501, 0x00040302, 0x07060501, 0x00040302,
   0x01000403, 0x02070605, 0x01000403, 0x02070605,
   0x03020706, 0x05010004, 0x03020706, 0x05010004,
   0x06050100, 0x04030207, 0x06050100, 0x04030207,
   0x00040302, 0x07060501, 0x00040302, 0x07060501,
   0x02070605, 0x01000403, 0x02070605, 0x01000403,
   0x05010004, 0x03020706, 0x05010004, 0x03020706,
   0x04030207, 0x06050100, 0x04030207, 0x06050100,
   0x07060501, 0x00040302, 0x07060501, 0x00040302,
   0x01000403, 0x02070605, 0x01000403, 0x02070605,
   0x03020706, 0x05010004, 0x03020706, 0x05010004
    };

static __attribute__((aligned (1024*4))) unsigned int dither_data_8x8[64]={
		1,
		49*2,
		13*2,
		61*2,
		4*2,
		52*2,
		16*2,
		64*2,
		33*2,
		17*2,
		45*2,
		29*2,
		36*2,
		20*2,
		48*2,
		32*2,
		9*2,
		57*2,
		5*2,
		53*2,
		12*2,
		60*2,
		8*2,
		56*2,
		41*2,
		25*2,
		37*2,
		21*2,
		44*2,
		28*2,
		40*2,
		24*2,
		3*2,
		51*2,
		15*2,
		63*2,
		2*2,
		50*2,
		14*2,
		62*2,
		35*2,
		19*2,
		47*2,
		31*2,
		34*2,
		18*2,
		46*2,
		30*2,
		11*2,
		59*2,
		7*2,
		55*2,
		10*2,
		58*2,
		6*2,
		54*2,
		43*2,
		27*2,
		39*2,
		23*2,
		42*2,
		26*2,
		38*2,
		22*2
		};

static void pxp_dithering_process(struct pxps *pxp);
static void pxp_wfe_a_process(struct pxps *pxp);
static void pxp_wfe_a_configure(struct pxps *pxp);
static void pxp_wfe_b_process(struct pxps *pxp);
static void pxp_wfe_b_configure(struct pxps *pxp);
static void pxp_start2(struct pxps *pxp);
static void pxp_soft_reset(struct pxps *pxp);
static void pxp_collision_detection_disable(struct pxps *pxp);
static void pxp_collision_detection_enable(struct pxps *pxp,
					   unsigned int width,
					   unsigned int height);
static void pxp_luts_activate(struct pxps *pxp, u64 lut_status);
static bool pxp_collision_status_report(struct pxps *pxp, struct pxp_collision_info *info);
static void pxp_histogram_status_report(struct pxps *pxp, u32 *hist_status);
static void pxp_histogram_enable(struct pxps *pxp,
				 unsigned int width,
				 unsigned int height);
static void pxp_histogram_disable(struct pxps *pxp);

enum {
	DITHER0_LUT = 0x0,	/* Select the LUT memory for access */
	DITHER0_ERR0 = 0x1,	/* Select the ERR0 memory for access */
	DITHER0_ERR1 = 0x2,	/* Select the ERR1 memory for access */
	DITHER1_LUT = 0x3,	/* Select the LUT memory for access */
	DITHER2_LUT = 0x4,	/* Select the LUT memory for access */
	ALU_A = 0x5,		/* Select the ALU instr memory for access */
	ALU_b = 0x6,		/* Select the ALU instr memory for access */
	WFE_A = 0x7,		/* Select the WFE_A instr memory for access */
	WFE_B = 0x8,		/* Select the WFE_B instr memory for access */
	RESERVED = 0x15,
};

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
	dev_dbg(pxp->dev, "PXP_PS_BACKGROUND_0 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_BACKGROUND_0));
	dev_dbg(pxp->dev, "PXP_PS_SCALE 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_SCALE));
	dev_dbg(pxp->dev, "PXP_PS_OFFSET 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_OFFSET));
	dev_dbg(pxp->dev, "PXP_PS_CLRKEYLOW_0 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_CLRKEYLOW_0));
	dev_dbg(pxp->dev, "PXP_PS_CLRKEYHIGH 0x%x",
		__raw_readl(pxp->base + HW_PXP_PS_CLRKEYHIGH_0));
	dev_dbg(pxp->dev, "PXP_AS_CTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_CTRL));
	dev_dbg(pxp->dev, "PXP_AS_BUF 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_BUF));
	dev_dbg(pxp->dev, "PXP_AS_PITCH 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_PITCH));
	dev_dbg(pxp->dev, "PXP_AS_CLRKEYLOW 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_CLRKEYLOW_0));
	dev_dbg(pxp->dev, "PXP_AS_CLRKEYHIGH 0x%x",
		__raw_readl(pxp->base + HW_PXP_AS_CLRKEYHIGH_0));
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
	dev_dbg(pxp->dev, "PXP_POWER_REG0 0x%x",
		__raw_readl(pxp->base + HW_PXP_POWER_REG0));
	dev_dbg(pxp->dev, "PXP_NEXT 0x%x",
		__raw_readl(pxp->base + HW_PXP_NEXT));
	dev_dbg(pxp->dev, "PXP_DEBUGCTRL 0x%x",
		__raw_readl(pxp->base + HW_PXP_DEBUGCTRL));
	dev_dbg(pxp->dev, "PXP_DEBUG 0x%x",
		__raw_readl(pxp->base + HW_PXP_DEBUG));
	dev_dbg(pxp->dev, "PXP_VERSION 0x%x",
		__raw_readl(pxp->base + HW_PXP_VERSION));
}

static void dump_pxp_reg2(struct pxps *pxp)
{
#ifdef DEBUG
	int i = 0;

	for (i=0; i< ((0x33C0/0x10) + 1);i++) {
		printk("0x%08x: 0x%08x\n", 0x10*i, __raw_readl(pxp->base + 0x10*i));
	}
#endif
}

static void print_param(struct pxp_layer_param *p, char *s)
{
	pr_debug("%s: t/l/w/h/s %d/%d/%d/%d/%d, addr %x\n", s,
		p->top, p->left, p->width, p->height, p->stride, p->paddr);
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

	ctrl = BF_PXP_PS_CTRL_FORMAT(fmt_ctrl) |
		(need_swap ? BM_PXP_PS_CTRL_WB_SWAP : 0);
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
		ctrl |= BM_PXP_CTRL_VFLIP0;
	if (proc_data->hflip)
		ctrl |= BM_PXP_CTRL_HFLIP0;
	if (proc_data->rotate) {
		ctrl |= BF_PXP_CTRL_ROTATE0(proc_data->rotate / 90);
#if 0
		if (proc_data->rot_pos)
			ctrl |= BM_PXP_CTRL_ROT_POS;
#endif
	}

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
	__raw_writel(BM_PXP_CTRL_ENABLE | BM_PXP_CTRL_ENABLE_CSC2 |
		BM_PXP_CTRL_ENABLE_LUT | BM_PXP_CTRL_ENABLE_ROTATE0 |
		BM_PXP_CTRL_ENABLE_PS_AS_OUT, pxp->base + HW_PXP_CTRL_SET);
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
		__raw_writel(0xFFFFFF, pxp->base + HW_PXP_PS_CLRKEYLOW_0);
		__raw_writel(0, pxp->base + HW_PXP_PS_CLRKEYHIGH_0);
	} else {
		__raw_writel(s0_params->color_key,
			     pxp->base + HW_PXP_PS_CLRKEYLOW_0);
		__raw_writel(s0_params->color_key,
			     pxp->base + HW_PXP_PS_CLRKEYHIGH_0);
	}
}

static void pxp_set_olcolorkey(int layer_no, struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_layer_param *ol_params = &pxp_conf->ol_param[layer_no];

	/* Low and high are set equal. V4L does not allow a chromakey range */
	if (ol_params->color_key_enable != 0 && ol_params->color_key != -1) {
		__raw_writel(ol_params->color_key,
			     pxp->base + HW_PXP_AS_CLRKEYLOW_0);
		__raw_writel(ol_params->color_key,
			     pxp->base + HW_PXP_AS_CLRKEYHIGH_0);
	} else {
		/* disable color key */
		__raw_writel(0xFFFFFF, pxp->base + HW_PXP_AS_CLRKEYLOW_0);
		__raw_writel(0, pxp->base + HW_PXP_AS_CLRKEYHIGH_0);
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
			olparam |= BM_PXP_AS_CTRL_ALPHA0_INVERT;
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
		     pxp->base + HW_PXP_PS_BACKGROUND_0);
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
	struct pxp_proc_data *proc_data = &pxp_conf_data->proc_data;
	int ol_nr;
	int i;

	if ((proc_data->working_mode & PXP_MODE_STANDARD) == PXP_MODE_STANDARD) {

		/* now only test dithering feature */
		if ((proc_data->engine_enable & PXP_ENABLE_DITHER) == PXP_ENABLE_DITHER)
			pxp_dithering_process(pxp);

		if ((proc_data->engine_enable & PXP_ENABLE_WFE_A) == PXP_ENABLE_WFE_A)
		{
			/* We should enable histogram in standard mode 
			 * in wfe_a processing for waveform mode selection
			 */
			pxp_histogram_enable(pxp, pxp_conf_data->wfe_a_fetch_param[0].width,
					pxp_conf_data->wfe_a_fetch_param[0].height);

			/* collision detection should be always enable in standard mode */
			pxp_luts_activate(pxp, (u64)proc_data->lut_status_1 |
					((u64)proc_data->lut_status_2 << 32));

			pxp_collision_detection_enable(pxp, pxp_conf_data->wfe_a_fetch_param[0].width,
						pxp_conf_data->wfe_a_fetch_param[0].height);

			pxp_wfe_a_configure(pxp);
			pxp_wfe_a_process(pxp);
		}

		if ((proc_data->engine_enable & PXP_ENABLE_WFE_B) == PXP_ENABLE_WFE_B) {
			pxp_wfe_b_configure(pxp);
			pxp_wfe_b_process(pxp);
		}

		pxp_start2(pxp);

		return 0;
	}

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

	pxp_start(pxp);

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

static struct pxp_tx_desc *pxpdma_first_active(struct pxp_channel *pxp_chan)
{
	return list_entry(pxp_chan->active_list.next, struct pxp_tx_desc, list);
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
	struct pxp_config_data *config_data = &pxp->pxp_conf_state;
	struct pxp_proc_data *proc_data = &config_data->proc_data;
	struct pxp_tx_desc *desc;
	struct pxp_tx_desc *child;
	int i = 0;

	/* so far we presume only one transaction on active_list */
	/* S0 */
	desc = pxpdma_first_active(pxp_chan);
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
		} else if (i == 1) {	/* Overlay */
			memcpy(&pxp->pxp_conf_state.ol_param[i - 1],
			       &child->layer_param.ol_param,
			       sizeof(struct pxp_layer_param));
		}

		if (proc_data->engine_enable & PXP_ENABLE_DITHER) {
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_DITHER_FETCH0)
				memcpy(&pxp->pxp_conf_state.dither_fetch_param[0],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_DITHER_FETCH1)
				memcpy(&pxp->pxp_conf_state.dither_fetch_param[1],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_DITHER_STORE0)
				memcpy(&pxp->pxp_conf_state.dither_store_param[0],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_DITHER_STORE1)
				memcpy(&pxp->pxp_conf_state.dither_store_param[1],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
		}

		if (proc_data->engine_enable & PXP_ENABLE_WFE_A) {
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_WFE_A_FETCH0)
				memcpy(&pxp->pxp_conf_state.wfe_a_fetch_param[0],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_WFE_A_FETCH1)
				memcpy(&pxp->pxp_conf_state.wfe_a_fetch_param[1],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_WFE_A_STORE0)
				memcpy(&pxp->pxp_conf_state.wfe_a_store_param[0],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_WFE_A_STORE1)
				memcpy(&pxp->pxp_conf_state.wfe_a_store_param[1],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
		}

		if (proc_data->engine_enable & PXP_ENABLE_WFE_B) {
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_WFE_B_FETCH0)
				memcpy(&pxp->pxp_conf_state.wfe_b_fetch_param[0],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_WFE_B_FETCH1)
				memcpy(&pxp->pxp_conf_state.wfe_b_fetch_param[1],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_WFE_B_STORE0)
				memcpy(&pxp->pxp_conf_state.wfe_b_store_param[0],
				       &child->layer_param.processing_param,
				       sizeof(struct pxp_layer_param));
			if (child->layer_param.processing_param.flag & PXP_BUF_FLAG_WFE_B_STORE1)
				memcpy(&pxp->pxp_conf_state.wfe_b_store_param[1],
				       &child->layer_param.processing_param,
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
	unsigned long flags, flags1;

	spin_lock_irqsave(&pxp->lock, flags);
	if (list_empty(&head)) {
		pxp->pxp_ongoing = 0;
		spin_unlock_irqrestore(&pxp->lock, flags);
		return;
	}

	pxp_chan = list_entry(head.next, struct pxp_channel, list);

	spin_lock_irqsave(&pxp_chan->lock, flags1);
	if (!list_empty(&pxp_chan->active_list)) {
		struct pxp_tx_desc *desc;
		/* REVISIT */
		desc = pxpdma_first_active(pxp_chan);
		__pxpdma_dostart(pxp_chan);
	}
	spin_unlock_irqrestore(&pxp_chan->lock, flags1);

	/* Configure PxP */
	pxp_config(pxp, pxp_chan);

	spin_unlock_irqrestore(&pxp->lock, flags);
}

static void pxpdma_dequeue(struct pxp_channel *pxp_chan, struct list_head *list)
{
	struct pxp_tx_desc *desc = NULL;
	do {
		desc = pxpdma_first_queued(pxp_chan);
		list_move_tail(&desc->list, list);
	} while (!list_empty(&pxp_chan->queue));
}

static dma_cookie_t pxp_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct pxp_tx_desc *desc = to_tx_desc(tx);
	struct pxp_channel *pxp_chan = to_pxp_channel(tx->chan);
	dma_cookie_t cookie;
	unsigned long flags;

	dev_dbg(&pxp_chan->dma_chan.dev->device, "received TX\n");

	mutex_lock(&pxp_chan->chan_mutex);

	cookie = pxp_chan->dma_chan.cookie;

	if (++cookie < 0)
		cookie = 1;

	/* from dmaengine.h: "last cookie value returned to client" */
	pxp_chan->dma_chan.cookie = cookie;
	tx->cookie = cookie;

	/* pxp_chan->lock can be taken under ichan->lock, but not v.v. */
	spin_lock_irqsave(&pxp_chan->lock, flags);

	/* Here we add the tx descriptor to our PxP task queue. */
	list_add_tail(&desc->list, &pxp_chan->queue);

	spin_unlock_irqrestore(&pxp_chan->lock, flags);

	dev_dbg(&pxp_chan->dma_chan.dev->device, "done TX\n");

	mutex_unlock(&pxp_chan->chan_mutex);
	return cookie;
}

/* Called with pxp_chan->chan_mutex held */
static int pxp_desc_alloc(struct pxp_channel *pxp_chan, int n)
{
	struct pxp_tx_desc *desc = vmalloc(n * sizeof(struct pxp_tx_desc));

	if (!desc)
		return -ENOMEM;

	memset(desc, 0, n * sizeof(struct pxp_tx_desc));

	pxp_chan->n_tx_desc = n;
	pxp_chan->desc = desc;
	INIT_LIST_HEAD(&pxp_chan->active_list);
	INIT_LIST_HEAD(&pxp_chan->queue);
	INIT_LIST_HEAD(&pxp_chan->free_list);

	while (n--) {
		struct dma_async_tx_descriptor *txd = &desc->txd;

		memset(txd, 0, sizeof(*txd));
		INIT_LIST_HEAD(&desc->tx_list);
		dma_async_tx_descriptor_init(txd, &pxp_chan->dma_chan);
		txd->tx_submit = pxp_tx_submit;

		list_add(&desc->list, &pxp_chan->free_list);

		desc++;
	}

	return 0;
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
	unsigned long flags;
	struct pxps *pxp = to_pxp(pxp_dma);
	int ret = 0, n_desc = 0;

	/*
	 * We are using _virtual_ channel here.
	 * Each channel contains all parameters of corresponding layers
	 * for one transaction; each layer is represented as one descriptor
	 * (i.e., pxp_tx_desc) here.
	 */

	spin_lock_irqsave(&pxp->lock, flags);

	/* max desc nr: S0+OL+OUT = 1+8+1 */
	n_desc = 24;

	spin_unlock_irqrestore(&pxp->lock, flags);

	if (n_desc && !pxp_chan->desc)
		ret = pxp_desc_alloc(pxp_chan, n_desc);

	return ret;
}

/**
 * pxp_uninit_channel() - uninitialize a PXP channel.
 * @pxp_dma:   PXP DMA context.
 * @pchan:  pointer to the channel object.
 * @return      0 on success or negative error code on failure.
 */
static int pxp_uninit_channel(struct pxp_dma *pxp_dma,
			      struct pxp_channel *pxp_chan)
{
	int ret = 0;

	if (pxp_chan->desc)
		vfree(pxp_chan->desc);

	pxp_chan->desc = NULL;

	return ret;
}

static irqreturn_t pxp_irq(int irq, void *dev_id)
{
	struct pxps *pxp = dev_id;
	struct pxp_channel *pxp_chan;
	struct pxp_tx_desc *desc;
	dma_async_tx_callback callback;
	void *callback_param;
	unsigned long flags, flags0;
	u32 hist_status;
	int pxp_irq_status = 0;

	dump_pxp_reg(pxp);

	spin_lock_irqsave(&pxp->lock, flags);

	if (__raw_readl(pxp->base + HW_PXP_STAT) & BM_PXP_STAT_IRQ0)
		__raw_writel(BM_PXP_STAT_IRQ0, pxp->base + HW_PXP_STAT_CLR);
	else {
		int irq_clr = 0;

		pxp_irq_status = __raw_readl(pxp->base + HW_PXP_IRQ);
		BUG_ON(!pxp_irq_status);

		if (pxp_irq_status & BM_PXP_IRQ_WFE_B_STORE_IRQ)
			irq_clr |= BM_PXP_IRQ_WFE_B_STORE_IRQ;
		if (pxp_irq_status & BM_PXP_IRQ_WFE_A_STORE_IRQ)
			irq_clr |= BM_PXP_IRQ_WFE_A_STORE_IRQ;
		if (pxp_irq_status & BM_PXP_IRQ_DITHER_STORE_IRQ)
			irq_clr |= BM_PXP_IRQ_DITHER_STORE_IRQ;

		if (pxp_irq_status & BM_PXP_IRQ_WFE_A_CH0_STORE_IRQ)
			irq_clr |= BM_PXP_IRQ_WFE_A_CH0_STORE_IRQ;
		if (pxp_irq_status & BM_PXP_IRQ_WFE_A_CH1_STORE_IRQ)
			irq_clr |= BM_PXP_IRQ_WFE_A_CH1_STORE_IRQ;

		if (pxp_irq_status & BM_PXP_IRQ_WFE_B_CH0_STORE_IRQ)
			irq_clr |= BM_PXP_IRQ_WFE_B_CH0_STORE_IRQ;
		if (pxp_irq_status & BM_PXP_IRQ_WFE_B_CH1_STORE_IRQ)
			irq_clr |= BM_PXP_IRQ_WFE_B_CH1_STORE_IRQ;

		if (pxp_irq_status & BM_PXP_IRQ_DITHER_CH0_PREFETCH_IRQ)
			irq_clr |= BM_PXP_IRQ_DITHER_CH0_PREFETCH_IRQ;
		if (pxp_irq_status & BM_PXP_IRQ_DITHER_CH1_PREFETCH_IRQ)
			irq_clr |= BM_PXP_IRQ_DITHER_CH1_PREFETCH_IRQ;
		if (pxp_irq_status & BM_PXP_IRQ_DITHER_CH0_STORE_IRQ)
			irq_clr |= BM_PXP_IRQ_DITHER_CH0_STORE_IRQ;
		if (pxp_irq_status & BM_PXP_IRQ_DITHER_CH1_STORE_IRQ)
			irq_clr |= BM_PXP_IRQ_DITHER_CH1_STORE_IRQ;
		/*XXX other irqs status clear should be added below */

		__raw_writel(irq_clr, pxp->base + HW_PXP_IRQ_CLR);
	}
	pxp_collision_status_report(pxp, &col_info);
	pxp_histogram_status_report(pxp, &hist_status);
	/*XXX before a new update operation, we should
	 * always clear all the collision information
	 */
	pxp_collision_detection_disable(pxp);
	pxp_histogram_disable(pxp);

	pxp_soft_reset(pxp);
	__raw_writel(0xffff, pxp->base + HW_PXP_IRQ_MASK);

	if (list_empty(&head)) {
		pxp->pxp_ongoing = 0;
		spin_unlock_irqrestore(&pxp->lock, flags);
		return IRQ_NONE;
	}

	pxp_chan = list_entry(head.next, struct pxp_channel, list);
	spin_lock_irqsave(&pxp_chan->lock, flags0);
	list_del_init(&pxp_chan->list);

	if (list_empty(&pxp_chan->active_list)) {
		pr_debug("PXP_IRQ pxp_chan->active_list empty. chan_id %d\n",
			 pxp_chan->dma_chan.chan_id);
		pxp->pxp_ongoing = 0;
		spin_unlock_irqrestore(&pxp_chan->lock, flags0);
		spin_unlock_irqrestore(&pxp->lock, flags);
		return IRQ_NONE;
	}

	/* Get descriptor and call callback */
	desc = pxpdma_first_active(pxp_chan);

	pxp_chan->completed = desc->txd.cookie;

	callback = desc->txd.callback;
	callback_param = desc->txd.callback_param;

	/* Send histogram status back to caller */
	desc->hist_status = hist_status;

	if ((desc->txd.flags & DMA_PREP_INTERRUPT) && callback)
		callback(callback_param);

	pxp_chan->status = PXP_CHANNEL_INITIALIZED;

	list_splice_init(&desc->tx_list, &pxp_chan->free_list);
	list_move(&desc->list, &pxp_chan->free_list);
	spin_unlock_irqrestore(&pxp_chan->lock, flags0);

	up(&pxp->sema);
	pxp->pxp_ongoing = 0;
	mod_timer(&pxp->clk_timer, jiffies + msecs_to_jiffies(timeout_in_ms));

	spin_unlock_irqrestore(&pxp->lock, flags);

	return IRQ_HANDLED;
}

/* called with pxp_chan->lock held */
static struct pxp_tx_desc *pxpdma_desc_get(struct pxp_channel *pxp_chan)
{
	struct pxp_tx_desc *desc, *_desc;
	struct pxp_tx_desc *ret = NULL;

	list_for_each_entry_safe(desc, _desc, &pxp_chan->free_list, list) {
		list_del_init(&desc->list);
		ret = desc;
		break;
	}

	return ret;
}

/* called with pxp_chan->lock held */
static void pxpdma_desc_put(struct pxp_channel *pxp_chan,
			    struct pxp_tx_desc *desc)
{
	if (desc) {
		struct device *dev = &pxp_chan->dma_chan.dev->device;
		struct pxp_tx_desc *child;

		list_for_each_entry(child, &desc->tx_list, list)
		    dev_info(dev, "moving child desc %p to freelist\n", child);
		list_splice_init(&desc->tx_list, &pxp_chan->free_list);
		dev_info(dev, "moving desc %p to freelist\n", desc);
		list_add(&desc->list, &pxp_chan->free_list);
	}
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
	unsigned long flags;
	dma_addr_t phys_addr;
	int i;

	if (direction != DMA_DEV_TO_MEM && direction != DMA_MEM_TO_DEV) {
		dev_err(chan->device->dev, "Invalid DMA direction %d!\n",
			direction);
		return NULL;
	}

	if (unlikely(sg_len < 2))
		return NULL;

	spin_lock_irqsave(&pxp_chan->lock, flags);
	for_each_sg(sgl, sg, sg_len, i) {
		desc = pxpdma_desc_get(pxp_chan);
		if (!desc) {
			pxpdma_desc_put(pxp_chan, first);
			dev_err(chan->device->dev, "Can't get DMA desc.\n");
			spin_unlock_irqrestore(&pxp_chan->lock, flags);
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
	spin_unlock_irqrestore(&pxp_chan->lock, flags);

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
	unsigned long flags0, flags;

	spin_lock_irqsave(&pxp->lock, flags0);
	spin_lock_irqsave(&pxp_chan->lock, flags);

	if (!list_empty(&pxp_chan->queue)) {
		pxpdma_dequeue(pxp_chan, &pxp_chan->active_list);
		pxp_chan->status = PXP_CHANNEL_READY;
		list_add_tail(&pxp_chan->list, &head);
	} else {
		spin_unlock_irqrestore(&pxp_chan->lock, flags);
		spin_unlock_irqrestore(&pxp->lock, flags0);
		return;
	}
	spin_unlock_irqrestore(&pxp_chan->lock, flags);
	spin_unlock_irqrestore(&pxp->lock, flags0);

	pxp_clk_enable(pxp);
	down(&pxp->sema);

	spin_lock_irqsave(&pxp->lock, flags);
	pxp->pxp_ongoing = 1;
	spin_unlock_irqrestore(&pxp->lock, flags);
	pxpdma_dostart_work(pxp);
}

static void __pxp_terminate_all(struct dma_chan *chan)
{
	struct pxp_channel *pxp_chan = to_pxp_channel(chan);
	unsigned long flags;

	/* pchan->queue is modified in ISR, have to spinlock */
	spin_lock_irqsave(&pxp_chan->lock, flags);
	list_splice_init(&pxp_chan->queue, &pxp_chan->free_list);
	list_splice_init(&pxp_chan->active_list, &pxp_chan->free_list);

	spin_unlock_irqrestore(&pxp_chan->lock, flags);

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
	struct pxp_dma *pxp_dma = to_pxp_dma(chan->device);

	mutex_lock(&pxp_chan->chan_mutex);

	__pxp_terminate_all(chan);

	pxp_chan->status = PXP_CHANNEL_FREE;

	pxp_uninit_channel(pxp_dma, pxp_chan);

	mutex_unlock(&pxp_chan->chan_mutex);
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

static void pxp_sram_init(struct pxps *pxp, u32 select,
			u32 buffer_addr, u32 length)
{
	u32 i;

	__raw_writel(
		BF_PXP_INIT_MEM_CTRL_ADDR(0) |
		BF_PXP_INIT_MEM_CTRL_SELECT(select) |
		BF_PXP_INIT_MEM_CTRL_START(1),
		pxp->base + HW_PXP_INIT_MEM_CTRL);

	if ((select == WFE_A) || (select == WFE_B)) {
		for (i = 0; i < length / 2; i++) {
			__raw_writel(*(((u32*)buffer_addr) + 2 * i + 1),
				pxp->base + HW_PXP_INIT_MEM_DATA_HIGH);

			__raw_writel(*(((u32*)buffer_addr) + 2 * i),
				pxp->base + HW_PXP_INIT_MEM_DATA);
		}
	} else {
		for (i = 0; i < length; i++) {
			__raw_writel(*(((u32*) buffer_addr) + i),
				pxp->base + HW_PXP_INIT_MEM_DATA);
		}
	}

	__raw_writel(
		BF_PXP_INIT_MEM_CTRL_ADDR(0) |
		BF_PXP_INIT_MEM_CTRL_SELECT(select) |
		BF_PXP_INIT_MEM_CTRL_START(0),
		pxp->base + HW_PXP_INIT_MEM_CTRL);
}

/*
 * wfe a configuration
 * configure wfe a engine for waveform processing
 * including its fetch and store module
 */
static void pxp_wfe_a_configure(struct pxps *pxp)
{
	/* FETCH */
	__raw_writel(
		BF_PXP_WFA_FETCH_CTRL_BF1_EN(1) |
		BF_PXP_WFA_FETCH_CTRL_BF1_HSK_MODE(0) |
		BF_PXP_WFA_FETCH_CTRL_BF1_BYTES_PP(0) |
		BF_PXP_WFA_FETCH_CTRL_BF1_LINE_MODE(0) |
		BF_PXP_WFA_FETCH_CTRL_BF1_SRAM_IF(0) |
		BF_PXP_WFA_FETCH_CTRL_BF1_BURST_LEN(0) |
		BF_PXP_WFA_FETCH_CTRL_BF1_BYPASS_MODE(0) |
		BF_PXP_WFA_FETCH_CTRL_BF2_EN(1) |
		BF_PXP_WFA_FETCH_CTRL_BF2_HSK_MODE(0) |
		BF_PXP_WFA_FETCH_CTRL_BF2_BYTES_PP(1) |
		BF_PXP_WFA_FETCH_CTRL_BF2_LINE_MODE(0) |
		BF_PXP_WFA_FETCH_CTRL_BF2_SRAM_IF(0) |
		BF_PXP_WFA_FETCH_CTRL_BF2_BURST_LEN(0) |
		BF_PXP_WFA_FETCH_CTRL_BF2_BYPASS_MODE(0),
		pxp->base + HW_PXP_WFA_FETCH_CTRL);

	__raw_writel(
		BF_PXP_WFA_ARRAY_PIXEL0_MASK_SIGN_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL0_MASK_OFFSET_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL0_MASK_SIGN_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL0_MASK_OFFSET_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL0_MASK_BUF_SEL(1) |
		BF_PXP_WFA_ARRAY_PIXEL0_MASK_H_OFS(0) |
		BF_PXP_WFA_ARRAY_PIXEL0_MASK_L_OFS(3),
		pxp->base + HW_PXP_WFA_ARRAY_PIXEL0_MASK);

	 __raw_writel(
		BF_PXP_WFA_ARRAY_PIXEL1_MASK_SIGN_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL1_MASK_OFFSET_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL1_MASK_SIGN_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL1_MASK_OFFSET_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL1_MASK_BUF_SEL(1) |
		BF_PXP_WFA_ARRAY_PIXEL1_MASK_H_OFS(4) |
		BF_PXP_WFA_ARRAY_PIXEL1_MASK_L_OFS(7),
		pxp->base + HW_PXP_WFA_ARRAY_PIXEL1_MASK);

	 __raw_writel(
		BF_PXP_WFA_ARRAY_PIXEL3_MASK_SIGN_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL3_MASK_OFFSET_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL3_MASK_SIGN_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL3_MASK_OFFSET_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL3_MASK_BUF_SEL(1) |
		BF_PXP_WFA_ARRAY_PIXEL3_MASK_H_OFS(8) |
		BF_PXP_WFA_ARRAY_PIXEL3_MASK_L_OFS(9),
		pxp->base + HW_PXP_WFA_ARRAY_PIXEL2_MASK);

	__raw_writel(
		BF_PXP_WFA_ARRAY_PIXEL4_MASK_SIGN_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL4_MASK_OFFSET_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL4_MASK_SIGN_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL4_MASK_OFFSET_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL4_MASK_BUF_SEL(1) |
		BF_PXP_WFA_ARRAY_PIXEL4_MASK_H_OFS(10) |
		BF_PXP_WFA_ARRAY_PIXEL4_MASK_L_OFS(15),
		pxp->base + HW_PXP_WFA_ARRAY_PIXEL3_MASK);

	__raw_writel(
		BF_PXP_WFA_ARRAY_PIXEL2_MASK_SIGN_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL2_MASK_OFFSET_Y(0) |
		BF_PXP_WFA_ARRAY_PIXEL2_MASK_SIGN_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL2_MASK_OFFSET_X(0) |
		BF_PXP_WFA_ARRAY_PIXEL2_MASK_BUF_SEL(0) |
		BF_PXP_WFA_ARRAY_PIXEL2_MASK_H_OFS(4) |
		BF_PXP_WFA_ARRAY_PIXEL2_MASK_L_OFS(7),
		pxp->base + HW_PXP_WFA_ARRAY_PIXEL4_MASK);

	__raw_writel(1, pxp->base + HW_PXP_WFA_ARRAY_REG2);

	/* STORE */
	__raw_writel(
		BF_PXP_WFE_A_STORE_CTRL_CH0_CH_EN(1)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_BLOCK_EN(0)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_BLOCK_16(0)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_HANDSHAKE_EN(0)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_ARRAY_EN(0)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_ARRAY_LINE_NUM(0)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_STORE_BYPASS_EN(0)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_STORE_MEMORY_EN(1)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_PACK_IN_SEL(1)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_FILL_DATA_EN(0)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_WR_NUM_BYTES(8)|
		BF_PXP_WFE_A_STORE_CTRL_CH0_COMBINE_2CHANNEL(1) |
		BF_PXP_WFE_A_STORE_CTRL_CH0_ARBIT_EN(0),
		pxp->base + HW_PXP_WFE_A_STORE_CTRL_CH0);

	__raw_writel(
		 BF_PXP_WFE_A_STORE_CTRL_CH1_CH_EN(1)|
		 BF_PXP_WFE_A_STORE_CTRL_CH1_BLOCK_EN(0)|
		 BF_PXP_WFE_A_STORE_CTRL_CH1_BLOCK_16(0)|
		 BF_PXP_WFE_A_STORE_CTRL_CH1_HANDSHAKE_EN(0)|
		 BF_PXP_WFE_A_STORE_CTRL_CH1_ARRAY_EN(0)|
		 BF_PXP_WFE_A_STORE_CTRL_CH1_ARRAY_LINE_NUM(0)|
		 BF_PXP_WFE_A_STORE_CTRL_CH1_STORE_BYPASS_EN(0)|
		 BF_PXP_WFE_A_STORE_CTRL_CH1_STORE_MEMORY_EN(1)|
		 BF_PXP_WFE_A_STORE_CTRL_CH1_PACK_IN_SEL(1)|
		 BF_PXP_WFE_A_STORE_CTRL_CH1_WR_NUM_BYTES(16),
		pxp->base + HW_PXP_WFE_A_STORE_CTRL_CH1);

	__raw_writel(
		BF_PXP_WFE_A_STORE_SHIFT_CTRL_CH0_OUTPUT_ACTIVE_BPP(0)|
		BF_PXP_WFE_A_STORE_SHIFT_CTRL_CH0_OUT_YUV422_1P_EN(0)|
		BF_PXP_WFE_A_STORE_SHIFT_CTRL_CH0_OUT_YUV422_2P_EN(0)|
		BF_PXP_WFE_A_STORE_SHIFT_CTRL_CH0_SHIFT_BYPASS(0),
		pxp->base + HW_PXP_WFE_A_STORE_SHIFT_CTRL_CH0);


	__raw_writel(
		BF_PXP_WFE_A_STORE_SHIFT_CTRL_CH1_OUTPUT_ACTIVE_BPP(1)|
		BF_PXP_WFE_A_STORE_SHIFT_CTRL_CH1_OUT_YUV422_1P_EN(0)|
		BF_PXP_WFE_A_STORE_SHIFT_CTRL_CH1_OUT_YUV422_2P_EN(0),
		pxp->base + HW_PXP_WFE_A_STORE_SHIFT_CTRL_CH1);

	__raw_writel(BF_PXP_WFE_A_STORE_FILL_DATA_CH0_FILL_DATA_CH0(0),
		pxp->base + HW_PXP_WFE_A_STORE_FILL_DATA_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK0_H_CH0_D_MASK0_H_CH0(0x0),
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK0_H_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK0_L_CH0_D_MASK0_L_CH0(0xf), /* fetch CP */
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK0_L_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK1_H_CH0_D_MASK1_H_CH0(0x0),
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK1_H_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK1_L_CH0_D_MASK1_L_CH0(0xf00), /* fetch NP */
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK1_L_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK2_H_CH0_D_MASK2_H_CH0(0x0),
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK2_H_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK2_L_CH0_D_MASK2_L_CH0(0x00000),
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK2_L_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK3_H_CH0_D_MASK3_H_CH0(0x0),
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK3_H_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK3_L_CH0_D_MASK3_L_CH0(0x3f000000), /* fetch LUT */
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK3_L_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK4_H_CH0_D_MASK4_H_CH0(0xf),
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK4_H_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_D_MASK4_L_CH0_D_MASK4_L_CH0(0x0), /* fetch Y4 */
		pxp->base + HW_PXP_WFE_A_STORE_D_MASK4_L_CH0);

	__raw_writel(
		BF_PXP_WFE_A_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH0(32) |
		BF_PXP_WFE_A_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG0(1) |
		BF_PXP_WFE_A_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH1(28)|
		BF_PXP_WFE_A_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG1(1) |
		BF_PXP_WFE_A_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH2(24)|
		BF_PXP_WFE_A_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG2(1)|
		BF_PXP_WFE_A_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH3(18)|
		BF_PXP_WFE_A_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG3(1),
		pxp->base + HW_PXP_WFE_A_STORE_D_SHIFT_L_CH0);

	__raw_writel(
		BF_PXP_WFE_A_STORE_D_SHIFT_H_CH0_D_SHIFT_WIDTH4(28) |
		BF_PXP_WFE_A_STORE_D_SHIFT_H_CH0_D_SHIFT_FLAG4(0) |
		BF_PXP_WFE_A_STORE_D_SHIFT_H_CH0_D_SHIFT_WIDTH5(0)|
		BF_PXP_WFE_A_STORE_D_SHIFT_H_CH0_D_SHIFT_FLAG5(0) |
		BF_PXP_WFE_A_STORE_D_SHIFT_H_CH0_D_SHIFT_WIDTH6(0)|
		BF_PXP_WFE_A_STORE_D_SHIFT_H_CH0_D_SHIFT_FLAG6(0) |
		BF_PXP_WFE_A_STORE_D_SHIFT_H_CH0_D_SHIFT_WIDTH7(0),
		pxp->base + HW_PXP_WFE_A_STORE_D_SHIFT_H_CH0);

	__raw_writel(
		BF_PXP_WFE_A_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH0(1)|
		BF_PXP_WFE_A_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG0(1)|
		BF_PXP_WFE_A_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH1(1)|
		BF_PXP_WFE_A_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG1(0)|
		BF_PXP_WFE_A_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH2(32+6)|
		BF_PXP_WFE_A_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG2(1)|
		BF_PXP_WFE_A_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH3(32+6)|
		BF_PXP_WFE_A_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG3(1),
		pxp->base + HW_PXP_WFE_A_STORE_F_SHIFT_L_CH0);

	__raw_writel(
		BF_PXP_WFE_A_STORE_F_MASK_H_CH0_F_MASK4(0)|
		BF_PXP_WFE_A_STORE_F_MASK_H_CH0_F_MASK5(0)|
		BF_PXP_WFE_A_STORE_F_MASK_H_CH0_F_MASK6(0)|
		BF_PXP_WFE_A_STORE_F_MASK_H_CH0_F_MASK7(0),
		pxp->base + HW_PXP_WFE_A_STORE_F_MASK_H_CH0);


	__raw_writel(
		BF_PXP_WFE_A_STORE_F_MASK_L_CH0_F_MASK0(0x1) |
		BF_PXP_WFE_A_STORE_F_MASK_L_CH0_F_MASK1(0x2) |
		BF_PXP_WFE_A_STORE_F_MASK_L_CH0_F_MASK2(0x4) |
		BF_PXP_WFE_A_STORE_F_MASK_L_CH0_F_MASK3(0x8),
		pxp->base + HW_PXP_WFE_A_STORE_F_MASK_L_CH0);

	/* ALU */
	__raw_writel(BF_PXP_ALU_A_INST_ENTRY_ENTRY_ADDR(0),
		pxp->base + HW_PXP_ALU_A_INST_ENTRY);

	__raw_writel(BF_PXP_ALU_A_PARAM_PARAM0(0) |
		BF_PXP_ALU_A_PARAM_PARAM1(0),
		pxp->base + HW_PXP_ALU_A_PARAM);

	__raw_writel(BF_PXP_ALU_A_CONFIG_BUF_ADDR(0),
		pxp->base + HW_PXP_ALU_A_CONFIG);

	__raw_writel(BF_PXP_ALU_A_LUT_CONFIG_MODE(0) |
		BF_PXP_ALU_A_LUT_CONFIG_EN(0),
		pxp->base + HW_PXP_ALU_A_LUT_CONFIG);

	__raw_writel(BF_PXP_ALU_A_LUT_DATA0_LUT_DATA_L(0),
		pxp->base + HW_PXP_ALU_A_LUT_DATA0);

	__raw_writel(BF_PXP_ALU_A_LUT_DATA1_LUT_DATA_H(0),
		pxp->base + HW_PXP_ALU_A_LUT_DATA1);

	__raw_writel(BF_PXP_ALU_A_CTRL_BYPASS    (1) |
		BF_PXP_ALU_A_CTRL_ENABLE    (1) |
		BF_PXP_ALU_A_CTRL_START     (0) |
		BF_PXP_ALU_A_CTRL_SW_RESET  (0),
		pxp->base + HW_PXP_ALU_A_CTRL);

	/* WFE A */
	__raw_writel(0x3F3F3F03, pxp->base + HW_PXP_WFE_A_STAGE1_MUX0);
	__raw_writel(0x0C00000C, pxp->base + HW_PXP_WFE_A_STAGE1_MUX1);
	__raw_writel(0x01040000, pxp->base + HW_PXP_WFE_A_STAGE1_MUX2);
	__raw_writel(0x0A0A0904, pxp->base + HW_PXP_WFE_A_STAGE1_MUX3);
	__raw_writel(0x00000B0B, pxp->base + HW_PXP_WFE_A_STAGE1_MUX4);

	__raw_writel(0x1800280E, pxp->base + HW_PXP_WFE_A_STAGE2_MUX0);
	__raw_writel(0x00280E00, pxp->base + HW_PXP_WFE_A_STAGE2_MUX1);
	__raw_writel(0x280E0018, pxp->base + HW_PXP_WFE_A_STAGE2_MUX2);
	__raw_writel(0x00001800, pxp->base + HW_PXP_WFE_A_STAGE2_MUX3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STAGE2_MUX4);
	__raw_writel(0x1800280E, pxp->base + HW_PXP_WFE_A_STAGE2_MUX5);
	__raw_writel(0x00280E00, pxp->base + HW_PXP_WFE_A_STAGE2_MUX6);
	__raw_writel(0x1A0E0018, pxp->base + HW_PXP_WFE_A_STAGE2_MUX7);
	__raw_writel(0x1B002911, pxp->base + HW_PXP_WFE_A_STAGE2_MUX8);
	__raw_writel(0x00002911, pxp->base + HW_PXP_WFE_A_STAGE2_MUX9);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STAGE2_MUX10);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STAGE2_MUX11);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STAGE2_MUX12);

	__raw_writel(0x07060504, pxp->base + HW_PXP_WFE_A_STAGE3_MUX0);
	__raw_writel(0x3F3F3F08, pxp->base + HW_PXP_WFE_A_STAGE3_MUX1);
	__raw_writel(0x03020100, pxp->base + HW_PXP_WFE_A_STAGE3_MUX2);
	__raw_writel(0x3F3F3F3F, pxp->base + HW_PXP_WFE_A_STAGE3_MUX3);

	__raw_writel(0x000F0F0F, pxp->base + HW_PXP_WFE_A_STAGE2_5X6_MASKS_0);
	__raw_writel(0x3f030100, pxp->base + HW_PXP_WFE_A_STAGE2_5X6_ADDR_0);

	__raw_writel(0x00000700, pxp->base + HW_PXP_WFE_A_STG2_5X1_OUT0);
	__raw_writel(0x0000F000, pxp->base + HW_PXP_WFE_A_STG2_5X1_OUT1);
	__raw_writel(0x0000A000, pxp->base + HW_PXP_WFE_A_STG2_5X1_OUT2);
	__raw_writel(0x000000C0, pxp->base + HW_PXP_WFE_A_STG2_5X1_OUT3);
	__raw_writel(0x070F0F0F, pxp->base + HW_PXP_WFE_A_STG2_5X1_MASKS);

	__raw_writel(0xFFFFFFFF, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT0_2);
	__raw_writel(0xFFFFFFFF, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT0_3);
	__raw_writel(0xFFFFFFFF, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT0_4);
	__raw_writel(0xFFFFFFFF, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT0_5);
	__raw_writel(0xFFFFFFFF, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT0_6);
	__raw_writel(0xFFFFFFFF, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT0_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT1_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT1_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT1_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT1_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT1_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT1_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT1_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT1_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT2_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT2_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT2_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT2_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT2_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT2_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT2_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT2_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT3_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT3_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT3_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT3_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT3_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT3_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT3_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT3_7);

	__raw_writel(0x04040404, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT0_0);
	__raw_writel(0x04040404, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT0_1);
	__raw_writel(0x04050505, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT0_2);
	__raw_writel(0x04040404, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT0_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT0_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT0_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT0_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT0_7);

	__raw_writel(0x05050505, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT1_0);
	__raw_writel(0x05050505, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT1_1);
	__raw_writel(0x05080808, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT1_2);
	__raw_writel(0x05050505, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT1_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT1_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT1_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT1_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT1_7);

	__raw_writel(0x07070707, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT2_0);
	__raw_writel(0x07070707, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT2_1);
	__raw_writel(0x070C0C0C, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT2_2);
	__raw_writel(0x07070707, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT2_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT2_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT2_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT2_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT2_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT3_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT3_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT3_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT3_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT3_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT3_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT3_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_A_STG2_5X6_OUT3_7);
}

/*
 *  wfe a processing
 * use wfe a to process an update
 * x,y,width,height:
 *         coordinate and size of the update region
 * wb:
 *         working buffer, 16bpp
 * upd:
 *         update buffer, in Y4 with or without alpha, 8bpp
 * twb:
 *         temp working buffer, 16bpp
 *         only used when reagl_en is 1
 * y4c:
 *         y4c buffer, {Y4[3:0],3'b000,collision}, 8bpp
 * lut:
 *         valid value 0-63
 *         set to the lut used for next update
 * partial:
 *         0 - full update
 *         1 - partial update
 * reagl_en:
 *         0 - use normal waveform algorithm
 *         1 - enable reagl/-d waveform algorithm
 * detection_only:
 *         0 - write working buffer
 *         1 - do no write working buffer, detection only
 * alpha_en:
 *         0 - upd is {Y4[3:0],4'b0000} format
 *         1 - upd is {Y4[3:0],3'b000,alpha} format
 */
static void pxp_wfe_a_process(struct pxps *pxp)
{
	struct pxp_config_data *config_data = &pxp->pxp_conf_state;
	struct pxp_proc_data *proc_data = &config_data->proc_data;
	struct pxp_layer_param *fetch_ch0 = &config_data->wfe_a_fetch_param[0];
	struct pxp_layer_param *fetch_ch1 = &config_data->wfe_a_fetch_param[1];
	struct pxp_layer_param *store_ch0 = &config_data->wfe_a_store_param[0];
	struct pxp_layer_param *store_ch1 = &config_data->wfe_a_store_param[1];
	int v;

	if (fetch_ch0->width != fetch_ch1->width ||
		fetch_ch0->height != fetch_ch1->height) {
		dev_err(pxp->dev, "width/height should be same for two fetch "
				"channels\n");
	}

	print_param(fetch_ch0, "wfe_a fetch_ch0");
	print_param(fetch_ch1, "wfe_a fetch_ch1");
	print_param(store_ch0, "wfe_a store_ch0");
	print_param(store_ch1, "wfe_a store_ch1");

	/* Fetch */
	__raw_writel(fetch_ch0->paddr, pxp->base + HW_PXP_WFA_FETCH_BUF1_ADDR);

	__raw_writel(BF_PXP_WFA_FETCH_BUF1_CORD_YCORD(fetch_ch0->top) |
		BF_PXP_WFA_FETCH_BUF1_CORD_XCORD(fetch_ch0->left),
		pxp->base + HW_PXP_WFA_FETCH_BUF1_CORD);

	__raw_writel(fetch_ch0->stride, pxp->base + HW_PXP_WFA_FETCH_BUF1_PITCH);

	__raw_writel(BF_PXP_WFA_FETCH_BUF1_SIZE_BUF_HEIGHT(fetch_ch0->height - 1) |
		BF_PXP_WFA_FETCH_BUF1_SIZE_BUF_WIDTH(fetch_ch0->width - 1),
		pxp->base + HW_PXP_WFA_FETCH_BUF1_SIZE);

	__raw_writel(fetch_ch1->paddr, pxp->base + HW_PXP_WFA_FETCH_BUF2_ADDR);

	__raw_writel(BF_PXP_WFA_FETCH_BUF2_CORD_YCORD(fetch_ch1->top) |
		BF_PXP_WFA_FETCH_BUF2_CORD_XCORD(fetch_ch1->left),
		pxp->base + HW_PXP_WFA_FETCH_BUF2_CORD);

	__raw_writel(fetch_ch1->stride * 2, pxp->base + HW_PXP_WFA_FETCH_BUF2_PITCH);

	__raw_writel(BF_PXP_WFA_FETCH_BUF2_SIZE_BUF_HEIGHT(fetch_ch1->height - 1) |
		BF_PXP_WFA_FETCH_BUF2_SIZE_BUF_WIDTH(fetch_ch1->width - 1),
		pxp->base + HW_PXP_WFA_FETCH_BUF2_SIZE);

	/* Store */
	__raw_writel(BF_PXP_WFE_A_STORE_SIZE_CH0_OUT_WIDTH(store_ch0->width - 1) |
		BF_PXP_WFE_A_STORE_SIZE_CH0_OUT_HEIGHT(store_ch0->height - 1),
		pxp->base + HW_PXP_WFE_A_STORE_SIZE_CH0);


	__raw_writel(BF_PXP_WFE_A_STORE_SIZE_CH1_OUT_WIDTH(store_ch1->width - 1) |
		BF_PXP_WFE_A_STORE_SIZE_CH1_OUT_HEIGHT(store_ch1->height - 1),
		pxp->base + HW_PXP_WFE_A_STORE_SIZE_CH1);

	__raw_writel(BF_PXP_WFE_A_STORE_PITCH_CH0_OUT_PITCH(store_ch0->stride) |
		BF_PXP_WFE_A_STORE_PITCH_CH1_OUT_PITCH(store_ch1->stride * 2),
		pxp->base + HW_PXP_WFE_A_STORE_PITCH);

	__raw_writel(BF_PXP_WFE_A_STORE_ADDR_0_CH0_OUT_BASE_ADDR0(store_ch0->paddr),
		pxp->base + HW_PXP_WFE_A_STORE_ADDR_0_CH0);
	__raw_writel(BF_PXP_WFE_A_STORE_ADDR_1_CH0_OUT_BASE_ADDR1(0),
		pxp->base + HW_PXP_WFE_A_STORE_ADDR_1_CH0);

	__raw_writel(BF_PXP_WFE_A_STORE_ADDR_0_CH1_OUT_BASE_ADDR0(
		store_ch1->paddr + (store_ch1->left + store_ch1->top *
		store_ch1->stride) * 2),
		pxp->base + HW_PXP_WFE_A_STORE_ADDR_0_CH1);

	__raw_writel(BF_PXP_WFE_A_STORE_ADDR_1_CH1_OUT_BASE_ADDR1(0),
		pxp->base + HW_PXP_WFE_A_STORE_ADDR_1_CH1);

	/* ALU */
	__raw_writel(BF_PXP_ALU_A_BUF_SIZE_BUF_WIDTH(fetch_ch0->width) |
	        BF_PXP_ALU_A_BUF_SIZE_BUF_HEIGHT(fetch_ch0->height),
		pxp->base + HW_PXP_ALU_A_BUF_SIZE);

	/* WFE */
	__raw_writel(BF_PXP_WFE_A_DIMENSIONS_WIDTH(fetch_ch0->width) |
		BF_PXP_WFE_A_DIMENSIONS_HEIGHT(fetch_ch0->height),
		pxp->base + HW_PXP_WFE_A_DIMENSIONS);

	/* Here it should be fetch_ch1 */
	__raw_writel(BF_PXP_WFE_A_OFFSET_X_OFFSET(fetch_ch1->left) |
		BF_PXP_WFE_A_OFFSET_Y_OFFSET(fetch_ch1->top),
		pxp->base + HW_PXP_WFE_A_OFFSET);

	__raw_writel((proc_data->lut & 0x000000FF) | 0x00000F00,
			pxp->base + HW_PXP_WFE_A_SW_DATA_REGS);
	__raw_writel((proc_data->partial_update | (proc_data->reagl_en << 1)),
			pxp->base + HW_PXP_WFE_A_SW_FLAG_REGS);

	__raw_writel(
		BF_PXP_WFE_A_CTRL_ENABLE(1) |
		BF_PXP_WFE_A_CTRL_SW_RESET(1),
		pxp->base + HW_PXP_WFE_A_CTRL);

	/* disable CH1 when only doing detection */
	v = __raw_readl(pxp->base + HW_PXP_WFE_A_STORE_CTRL_CH1);
	if (proc_data->detection_only) {
		v &= ~BF_PXP_WFE_A_STORE_CTRL_CH1_CH_EN(1);
		printk(KERN_EMERG "%s: detection only happens\n", __func__);
	} else
		v |= BF_PXP_WFE_A_STORE_CTRL_CH1_CH_EN(1);
	__raw_writel(v, pxp->base + HW_PXP_WFE_A_STORE_CTRL_CH1);
}

/*
 * wfe b configuration
 *
 * configure wfe b engnine for reagl/-d waveform processing
 */
static void pxp_wfe_b_configure(struct pxps *pxp)
{
	/* Fetch */
	__raw_writel(
		BF_PXP_WFB_FETCH_CTRL_BF1_EN(1) |
		BF_PXP_WFB_FETCH_CTRL_BF1_HSK_MODE(0) |
		BF_PXP_WFB_FETCH_CTRL_BF1_BYTES_PP(0) |
		BF_PXP_WFB_FETCH_CTRL_BF1_LINE_MODE(1) |
		BF_PXP_WFB_FETCH_CTRL_BF1_SRAM_IF(1) |
		BF_PXP_WFB_FETCH_CTRL_BF1_BURST_LEN(0) |
		BF_PXP_WFB_FETCH_CTRL_BF1_BYPASS_MODE(0) |
		BF_PXP_WFB_FETCH_CTRL_BF1_BORDER_MODE(1) |
		BF_PXP_WFB_FETCH_CTRL_BF2_EN(1) |
		BF_PXP_WFB_FETCH_CTRL_BF2_HSK_MODE(0) |
		BF_PXP_WFB_FETCH_CTRL_BF2_BYTES_PP(1) |
		BF_PXP_WFB_FETCH_CTRL_BF2_LINE_MODE(1) |
		BF_PXP_WFB_FETCH_CTRL_BF2_SRAM_IF(0) |
		BF_PXP_WFB_FETCH_CTRL_BF2_BURST_LEN(0) |
		BF_PXP_WFB_FETCH_CTRL_BF2_BORDER_MODE(1) |
		BF_PXP_WFB_FETCH_CTRL_BF2_BYPASS_MODE(0),
		pxp->base + HW_PXP_WFB_FETCH_CTRL);

	__raw_writel(
		BF_PXP_WFB_ARRAY_PIXEL0_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL0_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL0_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL0_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL0_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_PIXEL0_MASK_H_OFS(0) |
		BF_PXP_WFB_ARRAY_PIXEL0_MASK_L_OFS(7),
		pxp->base + HW_PXP_WFB_ARRAY_PIXEL0_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_PIXEL1_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL1_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL1_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL1_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL1_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_PIXEL1_MASK_H_OFS(10) |
		BF_PXP_WFB_ARRAY_PIXEL1_MASK_L_OFS(15),
		pxp->base + HW_PXP_WFB_ARRAY_PIXEL1_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_PIXEL2_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL2_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL2_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL2_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL2_MASK_BUF_SEL(0) |
		BF_PXP_WFB_ARRAY_PIXEL2_MASK_H_OFS(2) |
		BF_PXP_WFB_ARRAY_PIXEL2_MASK_L_OFS(7),
		pxp->base + HW_PXP_WFB_ARRAY_PIXEL2_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_PIXEL3_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL3_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL3_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL3_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL3_MASK_BUF_SEL(0) |
		BF_PXP_WFB_ARRAY_PIXEL3_MASK_H_OFS(0) |
		BF_PXP_WFB_ARRAY_PIXEL3_MASK_L_OFS(7),
		pxp->base + HW_PXP_WFB_ARRAY_PIXEL3_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_PIXEL4_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL4_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL4_MASK_SIGN_X(1) |
		BF_PXP_WFB_ARRAY_PIXEL4_MASK_OFFSET_X(1) |
		BF_PXP_WFB_ARRAY_PIXEL4_MASK_BUF_SEL(0) |
		BF_PXP_WFB_ARRAY_PIXEL4_MASK_H_OFS(0) |
		BF_PXP_WFB_ARRAY_PIXEL4_MASK_L_OFS(7),
		pxp->base + HW_PXP_WFB_ARRAY_PIXEL4_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_PIXEL5_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL5_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL5_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL5_MASK_OFFSET_X(1) |
		BF_PXP_WFB_ARRAY_PIXEL5_MASK_BUF_SEL(0) |
		BF_PXP_WFB_ARRAY_PIXEL5_MASK_H_OFS(0) |
		BF_PXP_WFB_ARRAY_PIXEL5_MASK_L_OFS(7),
		pxp->base + HW_PXP_WFB_ARRAY_PIXEL5_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_PIXEL6_MASK_SIGN_Y(1) |
		BF_PXP_WFB_ARRAY_PIXEL6_MASK_OFFSET_Y(1) |
		BF_PXP_WFB_ARRAY_PIXEL6_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL6_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL6_MASK_BUF_SEL(0) |
		BF_PXP_WFB_ARRAY_PIXEL6_MASK_H_OFS(0) |
		BF_PXP_WFB_ARRAY_PIXEL6_MASK_L_OFS(7),
		pxp->base + HW_PXP_WFB_ARRAY_PIXEL6_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_PIXEL7_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_PIXEL7_MASK_OFFSET_Y(1) |
		BF_PXP_WFB_ARRAY_PIXEL7_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL7_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_PIXEL7_MASK_BUF_SEL(0) |
		BF_PXP_WFB_ARRAY_PIXEL7_MASK_H_OFS(0) |
		BF_PXP_WFB_ARRAY_PIXEL7_MASK_L_OFS(7),
		pxp->base + HW_PXP_WFB_ARRAY_PIXEL7_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG0_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG0_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG0_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_FLAG0_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_FLAG0_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG0_MASK_H_OFS(8) |
		BF_PXP_WFB_ARRAY_FLAG0_MASK_L_OFS(8),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG0_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG1_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG1_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG1_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_FLAG1_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_FLAG1_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG1_MASK_H_OFS(9) |
		BF_PXP_WFB_ARRAY_FLAG1_MASK_L_OFS(9),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG1_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG2_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG2_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG2_MASK_SIGN_X(1) |
		BF_PXP_WFB_ARRAY_FLAG2_MASK_OFFSET_X(1) |
		BF_PXP_WFB_ARRAY_FLAG2_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG2_MASK_H_OFS(8) |
		BF_PXP_WFB_ARRAY_FLAG2_MASK_L_OFS(8),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG2_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG3_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG3_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG3_MASK_SIGN_X(1) |
		BF_PXP_WFB_ARRAY_FLAG3_MASK_OFFSET_X(1) |
		BF_PXP_WFB_ARRAY_FLAG3_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG3_MASK_H_OFS(9) |
		BF_PXP_WFB_ARRAY_FLAG3_MASK_L_OFS(9),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG3_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG4_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG4_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG4_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_FLAG4_MASK_OFFSET_X(1) |
		BF_PXP_WFB_ARRAY_FLAG4_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG4_MASK_H_OFS(8) |
		BF_PXP_WFB_ARRAY_FLAG4_MASK_L_OFS(8),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG4_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG5_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG5_MASK_OFFSET_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG5_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_FLAG5_MASK_OFFSET_X(1) |
		BF_PXP_WFB_ARRAY_FLAG5_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG5_MASK_H_OFS(9) |
		BF_PXP_WFB_ARRAY_FLAG5_MASK_L_OFS(9),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG5_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG6_MASK_SIGN_Y(1) |
		BF_PXP_WFB_ARRAY_FLAG6_MASK_OFFSET_Y(1) |
		BF_PXP_WFB_ARRAY_FLAG6_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_FLAG6_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_FLAG6_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG6_MASK_H_OFS(8) |
		BF_PXP_WFB_ARRAY_FLAG6_MASK_L_OFS(8),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG6_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG7_MASK_SIGN_Y(1) |
		BF_PXP_WFB_ARRAY_FLAG7_MASK_OFFSET_Y(1) |
		BF_PXP_WFB_ARRAY_FLAG7_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_FLAG7_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_FLAG7_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG7_MASK_H_OFS(9) |
		BF_PXP_WFB_ARRAY_FLAG7_MASK_L_OFS(9),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG7_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG8_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG8_MASK_OFFSET_Y(1) |
		BF_PXP_WFB_ARRAY_FLAG8_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_FLAG8_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_FLAG8_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG8_MASK_H_OFS(8) |
		BF_PXP_WFB_ARRAY_FLAG8_MASK_L_OFS(8),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG8_MASK);

	__raw_writel(
		BF_PXP_WFB_ARRAY_FLAG9_MASK_SIGN_Y(0) |
		BF_PXP_WFB_ARRAY_FLAG9_MASK_OFFSET_Y(1) |
		BF_PXP_WFB_ARRAY_FLAG9_MASK_SIGN_X(0) |
		BF_PXP_WFB_ARRAY_FLAG9_MASK_OFFSET_X(0) |
		BF_PXP_WFB_ARRAY_FLAG9_MASK_BUF_SEL(1) |
		BF_PXP_WFB_ARRAY_FLAG9_MASK_H_OFS(9) |
		BF_PXP_WFB_ARRAY_FLAG9_MASK_L_OFS(9),
		pxp->base + HW_PXP_WFB_ARRAY_FLAG9_MASK);

	pxp_sram_init(pxp, WFE_B, (u32)active_matrix_data_8x8, 64);


	/* Store */
	__raw_writel(
		BF_PXP_WFE_B_STORE_CTRL_CH0_CH_EN(1)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_BLOCK_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_BLOCK_16(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_HANDSHAKE_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_ARRAY_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_ARRAY_LINE_NUM(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_STORE_BYPASS_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_STORE_MEMORY_EN(1)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_PACK_IN_SEL(1)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_FILL_DATA_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_WR_NUM_BYTES(32)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_COMBINE_2CHANNEL(1) |
		BF_PXP_WFE_B_STORE_CTRL_CH0_ARBIT_EN(0),
		pxp->base + HW_PXP_WFE_B_STORE_CTRL_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_CTRL_CH1_CH_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_BLOCK_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_BLOCK_16(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_HANDSHAKE_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_ARRAY_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_ARRAY_LINE_NUM(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_STORE_BYPASS_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_STORE_MEMORY_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_PACK_IN_SEL(1)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_WR_NUM_BYTES(32),
		pxp->base + HW_PXP_WFE_B_STORE_CTRL_CH1);

	__raw_writel(
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_OUTPUT_ACTIVE_BPP(1)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_OUT_YUV422_1P_EN(0)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_OUT_YUV422_2P_EN(0)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_SHIFT_BYPASS(0),
		pxp->base + HW_PXP_WFE_B_STORE_SHIFT_CTRL_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_OUTPUT_ACTIVE_BPP(1)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_OUT_YUV422_1P_EN(0)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_OUT_YUV422_2P_EN(0)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_SHIFT_BYPASS(0),
		pxp->base + HW_PXP_WFE_B_STORE_SHIFT_CTRL_CH1);

	__raw_writel(BF_PXP_WFE_B_STORE_ADDR_1_CH0_OUT_BASE_ADDR1(0),
		pxp->base + HW_PXP_WFE_B_STORE_ADDR_1_CH0);

	__raw_writel(BF_PXP_WFE_B_STORE_ADDR_0_CH1_OUT_BASE_ADDR0(0),
		pxp->base + HW_PXP_WFE_B_STORE_ADDR_0_CH1);

	__raw_writel(BF_PXP_WFE_B_STORE_ADDR_1_CH1_OUT_BASE_ADDR1(0),
		pxp->base + HW_PXP_WFE_B_STORE_ADDR_1_CH1);

	__raw_writel(BF_PXP_WFE_B_STORE_FILL_DATA_CH0_FILL_DATA_CH0(0),
		pxp->base + HW_PXP_WFE_B_STORE_FILL_DATA_CH0);

	__raw_writel(BF_PXP_WFE_B_STORE_D_MASK0_H_CH0_D_MASK0_H_CH0(0x00000000),
		pxp->base + HW_PXP_WFE_B_STORE_D_MASK0_H_CH0);

	__raw_writel(BF_PXP_WFE_B_STORE_D_MASK0_L_CH0_D_MASK0_L_CH0(0xff),
		pxp->base + HW_PXP_WFE_B_STORE_D_MASK0_L_CH0);

	__raw_writel(BF_PXP_WFE_B_STORE_D_MASK1_H_CH0_D_MASK1_H_CH0(0x0),
		pxp->base + HW_PXP_WFE_B_STORE_D_MASK1_H_CH0);

	__raw_writel(BF_PXP_WFE_B_STORE_D_MASK1_L_CH0_D_MASK1_L_CH0(0x3f00),
		pxp->base + HW_PXP_WFE_B_STORE_D_MASK1_L_CH0);

	__raw_writel(BF_PXP_WFE_B_STORE_D_MASK2_H_CH0_D_MASK2_H_CH0(0x0),
		pxp->base + HW_PXP_WFE_B_STORE_D_MASK2_H_CH0);

	__raw_writel(BF_PXP_WFE_B_STORE_D_MASK2_L_CH0_D_MASK2_L_CH0(0x0),
		pxp->base + HW_PXP_WFE_B_STORE_D_MASK2_L_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_D_SHIFT_H_CH0_D_SHIFT_WIDTH4(0) |
		BF_PXP_WFE_B_STORE_D_SHIFT_H_CH0_D_SHIFT_FLAG4(0) |
		BF_PXP_WFE_B_STORE_D_SHIFT_H_CH0_D_SHIFT_WIDTH5(0)|
		BF_PXP_WFE_B_STORE_D_SHIFT_H_CH0_D_SHIFT_FLAG5(0) |
		BF_PXP_WFE_B_STORE_D_SHIFT_H_CH0_D_SHIFT_WIDTH6(0)|
		BF_PXP_WFE_B_STORE_D_SHIFT_H_CH0_D_SHIFT_FLAG6(0)|
		BF_PXP_WFE_B_STORE_D_SHIFT_H_CH0_D_SHIFT_WIDTH7(0)|
		BF_PXP_WFE_B_STORE_D_SHIFT_H_CH0_D_SHIFT_FLAG7(0),
		pxp->base + HW_PXP_WFE_B_STORE_D_SHIFT_H_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH0(0) |
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG0(0) |
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH1(2)|
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG1(1) |
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH2(6)|
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG2(0)|
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH3(0)|
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG3(0),
		pxp->base + HW_PXP_WFE_B_STORE_D_SHIFT_L_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH0(8)|
		BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG0(1)|
		BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH1(0)|
		BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG1(0)|
		BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH2(0)|
		BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG2(0)|
		BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH3(0)|
		BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG3(0),
		pxp->base + HW_PXP_WFE_B_STORE_F_SHIFT_L_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_F_MASK_H_CH0_F_MASK4(0)|
		BF_PXP_WFE_B_STORE_F_MASK_H_CH0_F_MASK5(0)|
		BF_PXP_WFE_B_STORE_F_MASK_H_CH0_F_MASK6(0)|
		BF_PXP_WFE_B_STORE_F_MASK_H_CH0_F_MASK7(0),
		pxp->base + HW_PXP_WFE_B_STORE_F_MASK_H_CH0);

	/* ALU */
	__raw_writel(BF_PXP_ALU_B_INST_ENTRY_ENTRY_ADDR(0),
		pxp->base + HW_PXP_ALU_B_INST_ENTRY);

	__raw_writel(BF_PXP_ALU_B_PARAM_PARAM0(0) |
		BF_PXP_ALU_B_PARAM_PARAM1(0),
		pxp->base + HW_PXP_ALU_B_PARAM);

	__raw_writel(BF_PXP_ALU_B_CONFIG_BUF_ADDR(0),
		pxp->base + HW_PXP_ALU_B_CONFIG);

	__raw_writel(BF_PXP_ALU_B_LUT_CONFIG_MODE(0) |
		BF_PXP_ALU_B_LUT_CONFIG_EN(0),
		pxp->base + HW_PXP_ALU_B_LUT_CONFIG);

	__raw_writel(BF_PXP_ALU_B_LUT_DATA0_LUT_DATA_L(0),
		pxp->base + HW_PXP_ALU_B_LUT_DATA0);

	__raw_writel(BF_PXP_ALU_B_LUT_DATA1_LUT_DATA_H(0),
		pxp->base + HW_PXP_ALU_B_LUT_DATA1);

	__raw_writel(
		BF_PXP_ALU_B_CTRL_BYPASS    (1) |
		BF_PXP_ALU_B_CTRL_ENABLE    (1) |
		BF_PXP_ALU_B_CTRL_START     (0) |
		BF_PXP_ALU_B_CTRL_SW_RESET  (0),
		pxp->base + HW_PXP_ALU_B_CTRL);

	/* WFE */
	__raw_writel(0x00000402, pxp->base + HW_PXP_WFE_B_SW_DATA_REGS);

	__raw_writel(0x02040608, pxp->base + HW_PXP_WFE_B_STAGE1_MUX0);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE1_MUX1);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE1_MUX2);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE1_MUX3);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE1_MUX4);
	__raw_writel(0x03000000, pxp->base + HW_PXP_WFE_B_STAGE1_MUX5);
	__raw_writel(0x050A040A, pxp->base + HW_PXP_WFE_B_STAGE1_MUX6);
	__raw_writel(0x070A060A, pxp->base + HW_PXP_WFE_B_STAGE1_MUX7);
	__raw_writel(0x0000000A, pxp->base + HW_PXP_WFE_B_STAGE1_MUX8);

	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE2_MUX0);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE2_MUX1);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE2_MUX2);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE2_MUX3);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE2_MUX4);
	__raw_writel(0x1C1E2022, pxp->base + HW_PXP_WFE_B_STAGE2_MUX5);
	__raw_writel(0x1215181A, pxp->base + HW_PXP_WFE_B_STAGE2_MUX6);
	__raw_writel(0x00000C0F, pxp->base + HW_PXP_WFE_B_STAGE2_MUX7);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE2_MUX8);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE2_MUX9);
	__raw_writel(0x01000000, pxp->base + HW_PXP_WFE_B_STAGE2_MUX10);
	__raw_writel(0x000C010B, pxp->base + HW_PXP_WFE_B_STAGE2_MUX11);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE2_MUX12);

	__raw_writel(0x09000C01, pxp->base + HW_PXP_WFE_B_STAGE3_MUX0);
	__raw_writel(0x003A2A1D, pxp->base + HW_PXP_WFE_B_STAGE3_MUX1);
	__raw_writel(0x09000C01, pxp->base + HW_PXP_WFE_B_STAGE3_MUX2);
	__raw_writel(0x003A2A1D, pxp->base + HW_PXP_WFE_B_STAGE3_MUX3);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE3_MUX4);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE3_MUX5);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE3_MUX6);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STAGE3_MUX7);
	__raw_writel(0x07060504, pxp->base + HW_PXP_WFE_B_STAGE3_MUX8);
	__raw_writel(0x00000008, pxp->base + HW_PXP_WFE_B_STAGE3_MUX9);
	__raw_writel(0x00001211, pxp->base + HW_PXP_WFE_B_STAGE3_MUX10);

	__raw_writel(0x02010100, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT0_0);
	__raw_writel(0x03020201, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT0_1);
	__raw_writel(0x03020201, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT0_2);
	__raw_writel(0x04030302, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT0_3);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT0_4);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT0_5);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT0_6);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT0_7);

	__raw_writel(0x02010100, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT1_0);
	__raw_writel(0x03020201, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT1_1);
	__raw_writel(0x03020201, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT1_2);
	__raw_writel(0x04030302, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT1_3);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT1_4);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT1_5);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT1_6);
	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STG1_5X8_OUT1_7);

	__raw_writel(0x0000000F, pxp->base + HW_PXP_WFE_B_STAGE1_5X8_MASKS_0);

	__raw_writel(0x00000000, pxp->base + HW_PXP_WFE_B_STG1_5X1_OUT0);
	__raw_writel(0x0000000F, pxp->base + HW_PXP_WFE_B_STG1_5X1_MASKS);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT0_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT0_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT0_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT0_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT0_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT0_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT0_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT0_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT1_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT1_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT1_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT1_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT1_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT1_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT1_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT1_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT2_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT2_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT2_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT2_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT2_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT2_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT2_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT2_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT3_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT3_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT3_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT3_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT3_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT3_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT3_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT3_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT4_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT4_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT4_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT4_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT4_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT4_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT4_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG1_8X1_OUT4_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STAGE2_5X6_MASKS_0);
	__raw_writel(0x3F3F3F3F, pxp->base + HW_PXP_WFE_B_STAGE2_5X6_ADDR_0);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT0_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT0_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT0_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT0_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT0_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT0_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT0_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT0_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT1_0);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT1_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT1_2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT1_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT1_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT1_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT1_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X6_OUT1_7);

	__raw_writel(0x00008000, pxp->base + HW_PXP_WFE_B_STG2_5X1_OUT0);
	__raw_writel(0x0000FFFE, pxp->base + HW_PXP_WFE_B_STG2_5X1_OUT1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X1_OUT2);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG2_5X1_OUT3);
	__raw_writel(0x00000F0F, pxp->base + HW_PXP_WFE_B_STG2_5X1_MASKS);

	__raw_writel(0x00007F7F, pxp->base + HW_PXP_WFE_B_STG3_F8X1_MASKS);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT0_0);
	__raw_writel(0x00FF00FF, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT0_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT0_2);
	__raw_writel(0x000000FF, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT0_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT0_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT0_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT0_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT0_7);

	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT1_0);
	__raw_writel(0xFF3FFF3F, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT1_1);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT1_2);
	__raw_writel(0xFFFFFF1F, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT1_3);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT1_4);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT1_5);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT1_6);
	__raw_writel(0, pxp->base + HW_PXP_WFE_B_STG3_F8X1_OUT1_7);

	__raw_writel(
		BF_PXP_WFE_B_CTRL_ENABLE(1) |
		BF_PXP_WFE_B_CTRL_SW_RESET(1),
		pxp->base + HW_PXP_WFE_B_CTRL);
}

/* wfe b processing
 * use wfe b to process an update
 * call this function only after pxp_wfe_a_processing
 * x,y,width,height:
 *         coordinate and size of the update region
 * twb:
 *         temp working buffer, 16bpp
 *         only used when reagl_en is 1
 * wb:
 *         working buffer, 16bpp
 * lut:
 *         lut buffer, 8bpp
 * lut_update:
 *         0 - wfe_b is used for reagl/reagl-d operation
 *         1 - wfe_b is used for lut update operation
 * reagl_d_en:
 *         0 - use reagl waveform algorithm
 *         1 - use reagl/-d waveform algorithm
 */
static void pxp_wfe_b_process(struct pxps *pxp)
{
	struct pxp_config_data *config_data = &pxp->pxp_conf_state;
	struct pxp_proc_data *proc_data = &config_data->proc_data;
	struct pxp_layer_param *fetch_ch0 = &config_data->wfe_b_fetch_param[0];
	struct pxp_layer_param *fetch_ch1 = &config_data->wfe_b_fetch_param[1];
	struct pxp_layer_param *store_ch0 = &config_data->wfe_b_store_param[0];
	struct pxp_layer_param *store_ch1 = &config_data->wfe_b_store_param[1];
	static int comp_mask;
	/* Fetch */

	print_param(fetch_ch0, "wfe_b fetch_ch0");
	print_param(fetch_ch1, "wfe_b fetch_ch1");
	print_param(store_ch0, "wfe_b store_ch0");
	print_param(store_ch1, "wfe_b store_ch1");

	__raw_writel(fetch_ch0->paddr, pxp->base + HW_PXP_WFB_FETCH_BUF1_ADDR);

	__raw_writel(
		BF_PXP_WFB_FETCH_BUF1_CORD_YCORD(fetch_ch0->top) |
		BF_PXP_WFB_FETCH_BUF1_CORD_XCORD(fetch_ch0->left),
		pxp->base + HW_PXP_WFB_FETCH_BUF1_CORD);

	__raw_writel(fetch_ch0->stride,
		pxp->base + HW_PXP_WFB_FETCH_BUF1_PITCH);

	__raw_writel(
		BF_PXP_WFB_FETCH_BUF1_SIZE_BUF_HEIGHT(fetch_ch0->height-1) |
		BF_PXP_WFB_FETCH_BUF1_SIZE_BUF_WIDTH(fetch_ch0->width-1),
		pxp->base + HW_PXP_WFB_FETCH_BUF1_SIZE);

	__raw_writel(fetch_ch1->paddr, pxp->base + HW_PXP_WFB_FETCH_BUF2_ADDR);

	__raw_writel(fetch_ch1->stride * 2,
			pxp->base + HW_PXP_WFB_FETCH_BUF2_PITCH);

	__raw_writel(
		BF_PXP_WFB_FETCH_BUF2_CORD_YCORD(fetch_ch1->top) |
		BF_PXP_WFB_FETCH_BUF2_CORD_XCORD(fetch_ch1->left),
		pxp->base + HW_PXP_WFB_FETCH_BUF2_CORD);

	__raw_writel(
		BF_PXP_WFB_FETCH_BUF2_SIZE_BUF_HEIGHT(fetch_ch1->height-1) |
		BF_PXP_WFB_FETCH_BUF2_SIZE_BUF_WIDTH(fetch_ch1->width-1),
		pxp->base + HW_PXP_WFB_FETCH_BUF2_SIZE);

	if (!proc_data->lut_update) {
		__raw_writel(
			BF_PXP_WFB_FETCH_CTRL_BF1_EN(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_HSK_MODE(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BYTES_PP(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_LINE_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_SRAM_IF(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BURST_LEN(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BYPASS_MODE(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BORDER_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_EN(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_HSK_MODE(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BYTES_PP(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_LINE_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_SRAM_IF(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BURST_LEN(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BORDER_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BYPASS_MODE(0),
			pxp->base + HW_PXP_WFB_FETCH_CTRL);
	} else {
		__raw_writel(
			BF_PXP_WFB_FETCH_CTRL_BF1_EN(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_HSK_MODE(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BYTES_PP(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_LINE_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_SRAM_IF(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BURST_LEN(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BORDER_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BYPASS_MODE(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_EN(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_HSK_MODE(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BYTES_PP(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_LINE_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_SRAM_IF(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BURST_LEN(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BORDER_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BYPASS_MODE(0),
			pxp->base + HW_PXP_WFB_FETCH_CTRL);
	}

#ifdef	CONFIG_REAGLD_ALGO_CHECK
	__raw_writel(
			(__raw_readl(pxp->base + HW_PXP_WFE_B_SW_DATA_REGS) & 0x0000FFFF) | ((fetch_ch0->comp_mask&0x000000FF)<<16),
			pxp->base + HW_PXP_WFE_B_SW_DATA_REGS);
#else
	__raw_writel(
			(__raw_readl(pxp->base + HW_PXP_WFE_B_SW_DATA_REGS) & 0x0000FFFF) | ((comp_mask&0x000000FF)<<16),
			pxp->base + HW_PXP_WFE_B_SW_DATA_REGS);

	/* comp_mask only need to be updated upon REAGL-D, 0,1,...7, 0,1,...  */
	if (proc_data->reagl_d_en) {
		comp_mask++;
		if (comp_mask>7)
			comp_mask = 0;
	}
#endif

	/* Store */
	__raw_writel(
		BF_PXP_WFE_B_STORE_SIZE_CH0_OUT_WIDTH(store_ch0->width-1)|
		BF_PXP_WFE_B_STORE_SIZE_CH0_OUT_HEIGHT(store_ch0->height-1),
		pxp->base + HW_PXP_WFE_B_STORE_SIZE_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_SIZE_CH1_OUT_WIDTH(store_ch1->width-1)|
		BF_PXP_WFE_B_STORE_SIZE_CH1_OUT_HEIGHT(store_ch1->height-1),
		pxp->base + HW_PXP_WFE_B_STORE_SIZE_CH1);

	__raw_writel(
		BF_PXP_WFE_B_STORE_PITCH_CH0_OUT_PITCH(store_ch0->stride * 2)|
		BF_PXP_WFE_B_STORE_PITCH_CH1_OUT_PITCH(store_ch1->stride * 2),
		pxp->base + HW_PXP_WFE_B_STORE_PITCH);

	__raw_writel(
		BF_PXP_WFE_B_STORE_ADDR_0_CH0_OUT_BASE_ADDR0(store_ch0->paddr
			+ (store_ch0->left + store_ch0->top * store_ch0->stride) * 2),
		pxp->base + HW_PXP_WFE_B_STORE_ADDR_0_CH0);

	if (proc_data->lut_update) {
		__raw_writel(
			BF_PXP_WFE_B_STORE_D_MASK1_H_CH0_D_MASK1_H_CH0(0x0),
			pxp->base + HW_PXP_WFE_B_STORE_D_MASK1_H_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_D_MASK1_L_CH0_D_MASK1_L_CH0(0x0),
			pxp->base + HW_PXP_WFE_B_STORE_D_MASK1_L_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_D_MASK2_H_CH0_D_MASK2_H_CH0(0x0),
			pxp->base + HW_PXP_WFE_B_STORE_D_MASK2_H_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_D_MASK2_L_CH0_D_MASK2_L_CH0(0x3f0000),
			pxp->base + HW_PXP_WFE_B_STORE_D_MASK2_L_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_F_MASK_L_CH0_F_MASK0(0x30)|
			BF_PXP_WFE_B_STORE_F_MASK_L_CH0_F_MASK1(0)|
			BF_PXP_WFE_B_STORE_F_MASK_L_CH0_F_MASK2(0)|
			BF_PXP_WFE_B_STORE_F_MASK_L_CH0_F_MASK3(0),
			pxp->base + HW_PXP_WFE_B_STORE_F_MASK_L_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH0(4)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG0(1)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH1(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG1(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH2(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG2(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH3(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG3(0),
			pxp->base + HW_PXP_WFE_B_STORE_F_SHIFT_L_CH0);
	} else {
		__raw_writel(
			BF_PXP_WFE_B_STORE_D_MASK1_H_CH0_D_MASK1_H_CH0(0x0),
			pxp->base + HW_PXP_WFE_B_STORE_D_MASK1_H_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_D_MASK1_L_CH0_D_MASK1_L_CH0(0x3f00),
			pxp->base + HW_PXP_WFE_B_STORE_D_MASK1_L_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_D_MASK2_H_CH0_D_MASK2_H_CH0(0x0),
			pxp->base + HW_PXP_WFE_B_STORE_D_MASK2_H_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_D_MASK2_L_CH0_D_MASK2_L_CH0(0x0),
			pxp->base + HW_PXP_WFE_B_STORE_D_MASK2_L_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_F_MASK_L_CH0_F_MASK0(3)|
			BF_PXP_WFE_B_STORE_F_MASK_L_CH0_F_MASK1(0)|
			BF_PXP_WFE_B_STORE_F_MASK_L_CH0_F_MASK2(0)|
			BF_PXP_WFE_B_STORE_F_MASK_L_CH0_F_MASK3(0),
			pxp->base + HW_PXP_WFE_B_STORE_F_MASK_L_CH0);

		__raw_writel(
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH0(8)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG0(1)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH1(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG1(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH2(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG2(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_WIDTH3(0)|
			BF_PXP_WFE_B_STORE_F_SHIFT_L_CH0_F_SHIFT_FLAG3(0),
			pxp->base + HW_PXP_WFE_B_STORE_F_SHIFT_L_CH0);
	}

	/* ALU */
	__raw_writel(
		BF_PXP_ALU_B_BUF_SIZE_BUF_WIDTH(fetch_ch0->width) |
		BF_PXP_ALU_B_BUF_SIZE_BUF_HEIGHT(fetch_ch0->height),
		pxp->base + HW_PXP_ALU_B_BUF_SIZE);

	/* WFE */
	__raw_writel(
		BF_PXP_WFE_B_DIMENSIONS_WIDTH(fetch_ch0->width) |
		BF_PXP_WFE_B_DIMENSIONS_HEIGHT(fetch_ch0->height),
		pxp->base + HW_PXP_WFE_B_DIMENSIONS);

	__raw_writel(	/*TODO check*/
		BF_PXP_WFE_B_OFFSET_X_OFFSET(fetch_ch0->left) |
		BF_PXP_WFE_B_OFFSET_Y_OFFSET(fetch_ch0->top),
		pxp->base + HW_PXP_WFE_B_OFFSET);

	__raw_writel(proc_data->reagl_d_en, pxp->base + HW_PXP_WFE_B_SW_FLAG_REGS);
}

void pxp_fill(
        u32 bpp,
        u32 value,
        u32 width,
        u32 height,
        u32 output_buffer,
        u32 output_pitch)
{
	u32 active_bpp;
	u32 pitch;

	if (bpp == 8) {
		active_bpp = 0;
		pitch = output_pitch;
	} else if(bpp == 16) {
		active_bpp = 1;
		pitch = output_pitch * 2;
	} else {
		active_bpp = 2;
		pitch = output_pitch * 4;
	}

	__raw_writel(
		BF_PXP_WFE_B_STORE_CTRL_CH0_CH_EN(1)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_BLOCK_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_BLOCK_16(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_HANDSHAKE_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_ARRAY_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_ARRAY_LINE_NUM(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_STORE_BYPASS_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_STORE_MEMORY_EN(1)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_PACK_IN_SEL(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_FILL_DATA_EN(1)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_WR_NUM_BYTES(32)|
		BF_PXP_WFE_B_STORE_CTRL_CH0_COMBINE_2CHANNEL(0) |
		BF_PXP_WFE_B_STORE_CTRL_CH0_ARBIT_EN(0),
		pxp_reg_base + HW_PXP_WFE_B_STORE_CTRL_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_CTRL_CH1_CH_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_BLOCK_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_BLOCK_16(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_HANDSHAKE_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_ARRAY_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_ARRAY_LINE_NUM(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_STORE_BYPASS_EN(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_STORE_MEMORY_EN(1)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_PACK_IN_SEL(0)|
		BF_PXP_WFE_B_STORE_CTRL_CH1_WR_NUM_BYTES(16),
		pxp_reg_base + HW_PXP_WFE_B_STORE_CTRL_CH1);

	__raw_writel(
		BF_PXP_WFE_B_STORE_SIZE_CH0_OUT_WIDTH(width-1)|
		BF_PXP_WFE_B_STORE_SIZE_CH0_OUT_HEIGHT(height-1),
		pxp_reg_base + HW_PXP_WFE_B_STORE_SIZE_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_SIZE_CH1_OUT_WIDTH(width-1)|
		BF_PXP_WFE_B_STORE_SIZE_CH1_OUT_HEIGHT(height-1),
		pxp_reg_base + HW_PXP_WFE_B_STORE_SIZE_CH1);

	__raw_writel(
		BF_PXP_WFE_B_STORE_PITCH_CH0_OUT_PITCH(pitch)|
		BF_PXP_WFE_B_STORE_PITCH_CH1_OUT_PITCH(pitch),
		pxp_reg_base + HW_PXP_WFE_B_STORE_PITCH);

	__raw_writel(
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_OUTPUT_ACTIVE_BPP(active_bpp)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_OUT_YUV422_1P_EN(0)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_OUT_YUV422_2P_EN(0)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH0_SHIFT_BYPASS(1),
		pxp_reg_base + HW_PXP_WFE_B_STORE_SHIFT_CTRL_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH1_OUTPUT_ACTIVE_BPP(active_bpp)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH1_OUT_YUV422_1P_EN(0)|
		BF_PXP_WFE_B_STORE_SHIFT_CTRL_CH1_OUT_YUV422_2P_EN(0),
		pxp_reg_base + HW_PXP_WFE_B_STORE_SHIFT_CTRL_CH1);

	__raw_writel(
		BF_PXP_WFE_B_STORE_ADDR_0_CH0_OUT_BASE_ADDR0(output_buffer),
		pxp_reg_base + HW_PXP_WFE_B_STORE_ADDR_0_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_ADDR_1_CH0_OUT_BASE_ADDR1(0),
		pxp_reg_base + HW_PXP_WFE_B_STORE_ADDR_1_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_ADDR_0_CH1_OUT_BASE_ADDR0(output_buffer),
		pxp_reg_base + HW_PXP_WFE_B_STORE_ADDR_0_CH1);

	__raw_writel(
		BF_PXP_WFE_B_STORE_ADDR_1_CH1_OUT_BASE_ADDR1(0),
		pxp_reg_base + HW_PXP_WFE_B_STORE_ADDR_1_CH1);

	__raw_writel(
		BF_PXP_WFE_B_STORE_FILL_DATA_CH0_FILL_DATA_CH0(value),
		pxp_reg_base + HW_PXP_WFE_B_STORE_FILL_DATA_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_D_MASK0_H_CH0_D_MASK0_H_CH0(0x00000000),
		pxp_reg_base + HW_PXP_WFE_B_STORE_D_MASK0_H_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_D_MASK0_L_CH0_D_MASK0_L_CH0(0x000000ff),
		pxp_reg_base + HW_PXP_WFE_B_STORE_D_MASK0_L_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_D_MASK1_H_CH0_D_MASK1_H_CH0(0x00000000),
		pxp_reg_base + HW_PXP_WFE_B_STORE_D_MASK1_H_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_D_MASK1_L_CH0_D_MASK1_L_CH0(0x000000ff),
		pxp_reg_base + HW_PXP_WFE_B_STORE_D_MASK1_L_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_D_MASK2_H_CH0_D_MASK2_H_CH0(0x00000000),
		pxp_reg_base + HW_PXP_WFE_B_STORE_D_MASK2_H_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_D_MASK2_L_CH0_D_MASK2_L_CH0(0x000000ff),
		pxp_reg_base + HW_PXP_WFE_B_STORE_D_MASK2_L_CH0);

	__raw_writel(
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH0(0) |
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG0(0) |
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH1(32)|
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG1(1) |
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH2(40)|
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG2(1)|
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH3(0)|
		BF_PXP_WFE_B_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG3(0),
		pxp_reg_base +  HW_PXP_WFE_B_STORE_D_SHIFT_L_CH0);

	__raw_writel(
		BF_PXP_CTRL2_ENABLE                   (1) |
		BF_PXP_CTRL2_ROTATE0                  (0) |
		BF_PXP_CTRL2_HFLIP0                   (0) |
		BF_PXP_CTRL2_VFLIP0                   (0) |
		BF_PXP_CTRL2_ROTATE1                  (0) |
		BF_PXP_CTRL2_HFLIP1                   (0) |
		BF_PXP_CTRL2_VFLIP1                   (0) |
		BF_PXP_CTRL2_ENABLE_DITHER            (0) |
		BF_PXP_CTRL2_ENABLE_WFE_A             (0) |
		BF_PXP_CTRL2_ENABLE_WFE_B             (1) |
		BF_PXP_CTRL2_ENABLE_INPUT_FETCH_STORE (0) |
		BF_PXP_CTRL2_ENABLE_ALPHA_B           (0) |
		BF_PXP_CTRL2_BLOCK_SIZE               (0) |
		BF_PXP_CTRL2_ENABLE_CSC2              (0) |
		BF_PXP_CTRL2_ENABLE_LUT               (0) |
		BF_PXP_CTRL2_ENABLE_ROTATE0           (0) |
		BF_PXP_CTRL2_ENABLE_ROTATE1           (0),
		pxp_reg_base + HW_PXP_CTRL2);

	if (busy_wait(BM_PXP_IRQ_WFE_B_CH0_STORE_IRQ &
			__raw_readl(pxp_reg_base + HW_PXP_IRQ)) == false)
		printk("%s: wait for completion timeout\n", __func__);
}
EXPORT_SYMBOL(pxp_fill);

#ifdef CONFIG_MXC_FPGA_M4_TEST
void m4_process(void)
{
	__raw_writel(0x7, pinctrl_base + PIN_DOUT);	/* M4 Start */

	while (!(__raw_readl(pxp_reg_base + HW_PXP_HANDSHAKE_CPU_STORE) & BM_PXP_HANDSHAKE_CPU_STORE_SW0_B0_READY));

	__raw_writel(0x3, pinctrl_base + PIN_DOUT);	/* M4 Stop */


}
#else
void m4_process(void) {}
#endif
EXPORT_SYMBOL(m4_process);

static void pxp_lut_status_set(struct pxps *pxp, unsigned int lut)
{
	if(lut<32)
		__raw_writel(
				__raw_readl(pxp_reg_base + HW_PXP_WFE_A_STG1_8X1_OUT0_0) | (1 << lut),
				pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT0_0);
	else {
		lut = lut -32;
		__raw_writel(
				__raw_readl(pxp_reg_base + HW_PXP_WFE_A_STG1_8X1_OUT0_1) | (1 << lut),
				pxp->base + HW_PXP_WFE_A_STG1_8X1_OUT0_1);
	}
}

static void pxp_luts_activate(struct pxps *pxp, u64 lut_status)
{
	int i = 0;

	if (!lut_status)
		return;

	for (i = 0; i < 64; i++) {
		if (lut_status & (1ULL << i))
			pxp_lut_status_set(pxp, i);
	}
}

static void pxp_lut_status_clr(unsigned int lut)
{
	if(lut<32)
		__raw_writel(
				__raw_readl(pxp_reg_base + HW_PXP_WFE_A_STG1_8X1_OUT0_0) & (~(1 << lut)),
				pxp_reg_base + HW_PXP_WFE_A_STG1_8X1_OUT0_0);
	else
	{
		lut = lut -32;
		__raw_writel(
				__raw_readl(pxp_reg_base + HW_PXP_WFE_A_STG1_8X1_OUT0_1) & (~(1 << lut)),
				pxp_reg_base + HW_PXP_WFE_A_STG1_8X1_OUT0_1);
	}
}

/* this function should be called in the epdc
 * driver explicitly when some epdc lut becomes
 * idle. So it should be exported.
 */
void pxp_luts_deactivate(u64 lut_status)
{
	int i = 0;

	if (!lut_status)
		return;

	for (i = 0; i < 64; i++) {
		if (lut_status & (1ULL << i))
			pxp_lut_status_clr(i);
	}
}

/* use histogram_B engine to calculate histogram status */
static void pxp_histogram_enable(struct pxps *pxp,
				 unsigned int width,
				 unsigned int height)
{
	__raw_writel(
			BF_PXP_HIST_B_BUF_SIZE_HEIGHT(height)|
			BF_PXP_HIST_B_BUF_SIZE_WIDTH(width),
			pxp->base + HW_PXP_HIST_B_BUF_SIZE);

	__raw_writel(
			BF_PXP_HIST_B_MASK_MASK_EN(1)|
			BF_PXP_HIST_B_MASK_MASK_MODE(0)|
			BF_PXP_HIST_B_MASK_MASK_OFFSET(64)|
			BF_PXP_HIST_B_MASK_MASK_WIDTH(0)|
			BF_PXP_HIST_B_MASK_MASK_VALUE0(1) |
			BF_PXP_HIST_B_MASK_MASK_VALUE1(0),
			pxp->base + HW_PXP_HIST_B_MASK);

	__raw_writel(
			BF_PXP_HIST_B_CTRL_PIXEL_WIDTH(3)|
			BF_PXP_HIST_B_CTRL_PIXEL_OFFSET(8)|
			BF_PXP_HIST_B_CTRL_CLEAR(0)|
			BF_PXP_HIST_B_CTRL_ENABLE(1),
			pxp->base + HW_PXP_HIST_B_CTRL);
}

static void pxp_histogram_status_report(struct pxps *pxp, u32 *hist_status)
{
	BUG_ON(!hist_status);

	*hist_status = (__raw_readl(pxp->base + HW_PXP_HIST_B_CTRL) & BM_PXP_HIST_B_CTRL_STATUS)
			>> BP_PXP_HIST_B_CTRL_STATUS;
	dev_dbg(pxp->dev, "%d pixels are used to calculate histogram status %d\n",
			__raw_readl(pxp->base + HW_PXP_HIST_B_TOTAL_PIXEL), *hist_status);
}

static void pxp_histogram_disable(struct pxps *pxp)
{
	__raw_writel(
			BF_PXP_HIST_B_CTRL_PIXEL_WIDTH(3)|
			BF_PXP_HIST_B_CTRL_PIXEL_OFFSET(4)|
			BF_PXP_HIST_B_CTRL_CLEAR(1)|
			BF_PXP_HIST_B_CTRL_ENABLE(0),
			pxp->base + HW_PXP_HIST_B_CTRL);
}

/* the collision detection function will be
 * called by epdc driver when required
 */
static void pxp_collision_detection_enable(struct pxps *pxp,
					   unsigned int width,
					   unsigned int height)
{
	__raw_writel(
			BF_PXP_HIST_A_BUF_SIZE_HEIGHT(height)|
			BF_PXP_HIST_A_BUF_SIZE_WIDTH(width),
			pxp_reg_base + HW_PXP_HIST_A_BUF_SIZE);

	__raw_writel(
			BF_PXP_HIST_A_MASK_MASK_EN(1)|
			BF_PXP_HIST_A_MASK_MASK_MODE(0)|
			BF_PXP_HIST_A_MASK_MASK_OFFSET(65)|
			BF_PXP_HIST_A_MASK_MASK_WIDTH(0)|
			BF_PXP_HIST_A_MASK_MASK_VALUE0(1) |
			BF_PXP_HIST_A_MASK_MASK_VALUE1(0),
			pxp_reg_base + HW_PXP_HIST_A_MASK);

	__raw_writel(
			BF_PXP_HIST_A_CTRL_PIXEL_WIDTH(6)|
			BF_PXP_HIST_A_CTRL_PIXEL_OFFSET(24)|
			BF_PXP_HIST_A_CTRL_CLEAR(0)|
			BF_PXP_HIST_A_CTRL_ENABLE(1),
			pxp_reg_base + HW_PXP_HIST_A_CTRL);
}

static void pxp_collision_detection_disable(struct pxps *pxp)
{
	__raw_writel(
			BF_PXP_HIST_A_CTRL_PIXEL_WIDTH(6)|
			BF_PXP_HIST_A_CTRL_PIXEL_OFFSET(24)|
			BF_PXP_HIST_A_CTRL_CLEAR(1)|
			BF_PXP_HIST_A_CTRL_ENABLE(0),
			pxp_reg_base + HW_PXP_HIST_A_CTRL);
}

/* this function can be called in the epdc callback
 * function in the pxp_irq() to let the epdc know
 * the collision information for the previous working
 * buffer update.
 */
static bool pxp_collision_status_report(struct pxps *pxp, struct pxp_collision_info *info)
{
	unsigned int count;

	BUG_ON(!info);
	memset(info, 0x0, sizeof(*info));

	info->pixel_cnt = count = __raw_readl(pxp->base + HW_PXP_HIST_A_TOTAL_PIXEL);
	if (!count)
		return false;

	dev_dbg(pxp->dev, "%s: pixel_cnt = %d\n", __func__, info->pixel_cnt);
	info->rect_min_x = __raw_readl(pxp->base + HW_PXP_HIST_A_ACTIVE_AREA_X) & 0xffff;
	dev_dbg(pxp->dev, "%s: rect_min_x = %d\n", __func__, info->rect_min_x);
	info->rect_max_x = (__raw_readl(pxp->base + HW_PXP_HIST_A_ACTIVE_AREA_X) >> 16) & 0xffff;
	dev_dbg(pxp->dev, "%s: rect_max_x = %d\n", __func__, info->rect_max_x);
	info->rect_min_y = __raw_readl(pxp->base + HW_PXP_HIST_A_ACTIVE_AREA_Y) & 0xffff;
	dev_dbg(pxp->dev, "%s: rect_min_y = %d\n", __func__, info->rect_min_y);
	info->rect_max_y = (__raw_readl(pxp->base + HW_PXP_HIST_A_ACTIVE_AREA_Y) >> 16) & 0xffff;
	dev_dbg(pxp->dev, "%s: rect_max_y = %d\n", __func__, info->rect_max_y);

	info->victim_luts[0] = __raw_readl(pxp->base + HW_PXP_HIST_A_RAW_STAT0);
	dev_dbg(pxp->dev, "%s: victim_luts[0] = 0x%x\n", __func__, info->victim_luts[0]);
	info->victim_luts[1] = __raw_readl(pxp->base + HW_PXP_HIST_A_RAW_STAT1);
	dev_dbg(pxp->dev, "%s: victim_luts[1] = 0x%x\n", __func__, info->victim_luts[1]);

	return true;
}

void pxp_get_collision_info(struct pxp_collision_info *info)
{
	BUG_ON(!info);

	memcpy(info, &col_info, sizeof(struct pxp_collision_info));
}
EXPORT_SYMBOL(pxp_get_collision_info);

static void dither_prefetch_config(struct pxps *pxp)
{
	struct pxp_config_data *config_data = &pxp->pxp_conf_state;
	struct pxp_layer_param *fetch_ch0 = &config_data->dither_fetch_param[0];
	struct pxp_layer_param *fetch_ch1 = &config_data->dither_fetch_param[1];

	print_param(fetch_ch0, "dither fetch_ch0");
	print_param(fetch_ch1, "dither fetch_ch1");
	__raw_writel(
			BF_PXP_DITHER_FETCH_CTRL_CH0_CH_EN(1) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_BLOCK_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_BLOCK_16(0)|
			BF_PXP_DITHER_FETCH_CTRL_CH0_HANDSHAKE_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_BYPASS_PIXEL_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_HIGH_BYTE(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_HFLIP(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_VFLIP(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_ROTATION_ANGLE(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_RD_NUM_BYTES(32) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_HANDSHAKE_SCAN_LINE_NUM(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_ARBIT_EN(0),
			pxp->base + HW_PXP_DITHER_FETCH_CTRL_CH0);

	__raw_writel(
			BF_PXP_DITHER_FETCH_CTRL_CH1_CH_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH1_BLOCK_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH1_BLOCK_16(0)|
			BF_PXP_DITHER_FETCH_CTRL_CH1_HANDSHAKE_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH1_BYPASS_PIXEL_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH1_HFLIP(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH1_VFLIP(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH1_ROTATION_ANGLE(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH1_RD_NUM_BYTES(2) |
			BF_PXP_DITHER_FETCH_CTRL_CH1_HANDSHAKE_SCAN_LINE_NUM(0),
			pxp->base + HW_PXP_DITHER_FETCH_CTRL_CH1);

	__raw_writel(
			BF_PXP_DITHER_FETCH_ACTIVE_SIZE_ULC_CH0_ACTIVE_SIZE_ULC_X(0) |
			BF_PXP_DITHER_FETCH_ACTIVE_SIZE_ULC_CH0_ACTIVE_SIZE_ULC_Y(0),
			pxp->base + HW_PXP_DITHER_FETCH_ACTIVE_SIZE_ULC_CH0);
	__raw_writel(
			BF_PXP_DITHER_FETCH_ACTIVE_SIZE_LRC_CH0_ACTIVE_SIZE_LRC_X(fetch_ch0->width - 1) |
			BF_PXP_DITHER_FETCH_ACTIVE_SIZE_LRC_CH0_ACTIVE_SIZE_LRC_Y(fetch_ch0->height - 1),
			pxp->base + HW_PXP_DITHER_FETCH_ACTIVE_SIZE_LRC_CH0);

	__raw_writel(
			BF_PXP_DITHER_FETCH_ACTIVE_SIZE_ULC_CH1_ACTIVE_SIZE_ULC_X(0) |
			BF_PXP_DITHER_FETCH_ACTIVE_SIZE_ULC_CH1_ACTIVE_SIZE_ULC_Y(0),
			pxp->base + HW_PXP_DITHER_FETCH_ACTIVE_SIZE_ULC_CH1);
	__raw_writel(
			BF_PXP_DITHER_FETCH_ACTIVE_SIZE_LRC_CH1_ACTIVE_SIZE_LRC_X(fetch_ch1->width - 1) |
			BF_PXP_DITHER_FETCH_ACTIVE_SIZE_LRC_CH1_ACTIVE_SIZE_LRC_Y(fetch_ch1->height - 1),
			pxp->base + HW_PXP_DITHER_FETCH_ACTIVE_SIZE_LRC_CH1);
	__raw_writel(
			BF_PXP_DITHER_FETCH_SIZE_CH0_INPUT_TOTAL_WIDTH(fetch_ch0->width - 1) |
			BF_PXP_DITHER_FETCH_SIZE_CH0_INPUT_TOTAL_HEIGHT(fetch_ch0->height - 1),
			pxp->base + HW_PXP_DITHER_FETCH_SIZE_CH0);

	__raw_writel(
			BF_PXP_DITHER_FETCH_SIZE_CH1_INPUT_TOTAL_WIDTH(fetch_ch1->width - 1) |
			BF_PXP_DITHER_FETCH_SIZE_CH1_INPUT_TOTAL_HEIGHT(fetch_ch1->height - 1),
			pxp->base + HW_PXP_DITHER_FETCH_SIZE_CH1);

	__raw_writel(
			BF_PXP_DITHER_FETCH_PITCH_CH0_INPUT_PITCH(fetch_ch0->stride) |
			BF_PXP_DITHER_FETCH_PITCH_CH1_INPUT_PITCH(fetch_ch1->stride),
			pxp->base + HW_PXP_DITHER_FETCH_PITCH);

	__raw_writel(
			BF_PXP_DITHER_FETCH_SHIFT_CTRL_CH0_INPUT_ACTIVE_BPP(0) |
			BF_PXP_DITHER_FETCH_SHIFT_CTRL_CH0_EXPAND_FORMAT(0) |
			BF_PXP_DITHER_FETCH_SHIFT_CTRL_CH0_EXPAND_EN(0) |
			BF_PXP_DITHER_FETCH_SHIFT_CTRL_CH0_SHIFT_BYPASS(1),
			pxp->base + HW_PXP_DITHER_FETCH_SHIFT_CTRL_CH0);

	__raw_writel(
                        BF_PXP_DITHER_FETCH_SHIFT_CTRL_CH1_INPUT_ACTIVE_BPP(0) |
                        BF_PXP_DITHER_FETCH_SHIFT_CTRL_CH1_EXPAND_FORMAT(0) |
                        BF_PXP_DITHER_FETCH_SHIFT_CTRL_CH1_EXPAND_EN(0) |
                        BF_PXP_DITHER_FETCH_SHIFT_CTRL_CH1_SHIFT_BYPASS(1),
			pxp->base + HW_PXP_DITHER_FETCH_SHIFT_CTRL_CH1);

	__raw_writel(
                        BF_PXP_DITHER_FETCH_SHIFT_OFFSET_CH0_OFFSET0(0) |
                        BF_PXP_DITHER_FETCH_SHIFT_OFFSET_CH0_OFFSET1(0) |
                        BF_PXP_DITHER_FETCH_SHIFT_OFFSET_CH0_OFFSET2(0) |
                        BF_PXP_DITHER_FETCH_SHIFT_OFFSET_CH0_OFFSET3(0),
			pxp->base + HW_PXP_DITHER_FETCH_SHIFT_OFFSET_CH0);

	__raw_writel(
                        BF_PXP_DITHER_FETCH_SHIFT_OFFSET_CH1_OFFSET0(0) |
                        BF_PXP_DITHER_FETCH_SHIFT_OFFSET_CH1_OFFSET1(0) |
                        BF_PXP_DITHER_FETCH_SHIFT_OFFSET_CH1_OFFSET2(0) |
                        BF_PXP_DITHER_FETCH_SHIFT_OFFSET_CH1_OFFSET3(0),
			pxp->base + HW_PXP_DITHER_FETCH_SHIFT_OFFSET_CH1);

	__raw_writel(
                        BF_PXP_DITHER_FETCH_SHIFT_WIDTH_CH0_WIDTH0(7) |
                        BF_PXP_DITHER_FETCH_SHIFT_WIDTH_CH0_WIDTH1(7) |
                        BF_PXP_DITHER_FETCH_SHIFT_WIDTH_CH0_WIDTH2(7) |
                        BF_PXP_DITHER_FETCH_SHIFT_WIDTH_CH0_WIDTH3(7),
			pxp->base + HW_PXP_DITHER_FETCH_SHIFT_WIDTH_CH0);

	__raw_writel(
                        BF_PXP_DITHER_FETCH_SHIFT_WIDTH_CH1_WIDTH0(7) |
                        BF_PXP_DITHER_FETCH_SHIFT_WIDTH_CH1_WIDTH1(7) |
                        BF_PXP_DITHER_FETCH_SHIFT_WIDTH_CH1_WIDTH2(7) |
                        BF_PXP_DITHER_FETCH_SHIFT_WIDTH_CH1_WIDTH3(7),
			pxp->base + HW_PXP_DITHER_FETCH_SHIFT_WIDTH_CH1);

	__raw_writel(
			BF_PXP_DITHER_FETCH_ADDR_0_CH0_INPUT_BASE_ADDR0(fetch_ch0->paddr),
			pxp->base + HW_PXP_DITHER_FETCH_ADDR_0_CH0);

	__raw_writel(
			BF_PXP_DITHER_FETCH_ADDR_1_CH0_INPUT_BASE_ADDR1(0),
			pxp->base + HW_PXP_DITHER_FETCH_ADDR_1_CH0);

	__raw_writel(
			BF_PXP_DITHER_FETCH_ADDR_0_CH1_INPUT_BASE_ADDR0(fetch_ch1->paddr),
			pxp->base + HW_PXP_DITHER_FETCH_ADDR_0_CH1);

	__raw_writel(
			BF_PXP_DITHER_FETCH_ADDR_1_CH1_INPUT_BASE_ADDR1(0),
			pxp->base + HW_PXP_DITHER_FETCH_ADDR_1_CH1);
}

static void dither_store_config(struct pxps *pxp)
{
	struct pxp_config_data *config_data = &pxp->pxp_conf_state;
	struct pxp_layer_param *store_ch0 = &config_data->dither_store_param[0];
	struct pxp_layer_param *store_ch1 = &config_data->dither_store_param[1];

	print_param(store_ch0, "dither store_ch0");
	print_param(store_ch1, "dither store_ch1");

	__raw_writel(
			BF_PXP_DITHER_STORE_CTRL_CH0_CH_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH0_BLOCK_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_BLOCK_16(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_HANDSHAKE_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_ARRAY_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_ARRAY_LINE_NUM(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_STORE_BYPASS_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_STORE_MEMORY_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH0_PACK_IN_SEL(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_FILL_DATA_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_WR_NUM_BYTES(32)|
			BF_PXP_DITHER_STORE_CTRL_CH0_COMBINE_2CHANNEL(0) |
			BF_PXP_DITHER_STORE_CTRL_CH0_ARBIT_EN(0),
			pxp->base + HW_PXP_DITHER_STORE_CTRL_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_CTRL_CH1_CH_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH1_BLOCK_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH1_BLOCK_16(0)|
			BF_PXP_DITHER_STORE_CTRL_CH1_HANDSHAKE_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH1_ARRAY_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH1_ARRAY_LINE_NUM(0)|
			BF_PXP_DITHER_STORE_CTRL_CH1_STORE_BYPASS_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH1_STORE_MEMORY_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH1_PACK_IN_SEL(0)|
			BF_PXP_DITHER_STORE_CTRL_CH1_WR_NUM_BYTES(32),
			pxp->base + HW_PXP_DITHER_STORE_CTRL_CH1);

	__raw_writel(
			BF_PXP_DITHER_STORE_SIZE_CH0_OUT_WIDTH(store_ch0->width - 1) |
			BF_PXP_DITHER_STORE_SIZE_CH0_OUT_HEIGHT(store_ch0->height - 1),
			pxp->base + HW_PXP_DITHER_STORE_SIZE_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_SIZE_CH1_OUT_WIDTH(store_ch1->width - 1) |
			BF_PXP_DITHER_STORE_SIZE_CH1_OUT_HEIGHT(store_ch1->height - 1),
			pxp->base + HW_PXP_DITHER_STORE_SIZE_CH1);

	__raw_writel(
			BF_PXP_DITHER_STORE_PITCH_CH0_OUT_PITCH(store_ch0->stride) |
			BF_PXP_DITHER_STORE_PITCH_CH1_OUT_PITCH(store_ch1->stride),
			pxp->base + HW_PXP_DITHER_STORE_PITCH);

	__raw_writel(
			BF_PXP_DITHER_STORE_SHIFT_CTRL_CH0_OUTPUT_ACTIVE_BPP(0)|
			BF_PXP_DITHER_STORE_SHIFT_CTRL_CH0_OUT_YUV422_1P_EN(0)|
			BF_PXP_DITHER_STORE_SHIFT_CTRL_CH0_OUT_YUV422_2P_EN(0)|
			BF_PXP_DITHER_STORE_SHIFT_CTRL_CH0_SHIFT_BYPASS(1),
			pxp->base + HW_PXP_DITHER_STORE_SHIFT_CTRL_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_SHIFT_CTRL_CH1_OUTPUT_ACTIVE_BPP(0)|
			BF_PXP_DITHER_STORE_SHIFT_CTRL_CH1_OUT_YUV422_1P_EN(0)|
			BF_PXP_DITHER_STORE_SHIFT_CTRL_CH1_OUT_YUV422_2P_EN(0),
			pxp->base + HW_PXP_DITHER_STORE_SHIFT_CTRL_CH1);

	__raw_writel(
			BF_PXP_DITHER_STORE_ADDR_0_CH0_OUT_BASE_ADDR0(store_ch0->paddr),
			pxp->base + HW_PXP_DITHER_STORE_ADDR_0_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_ADDR_1_CH0_OUT_BASE_ADDR1(0),
			pxp->base + HW_PXP_DITHER_STORE_ADDR_1_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_ADDR_0_CH1_OUT_BASE_ADDR0(store_ch1->paddr),
			pxp->base + HW_PXP_DITHER_STORE_ADDR_0_CH1);

	__raw_writel(
			BF_PXP_DITHER_STORE_ADDR_1_CH1_OUT_BASE_ADDR1(0),
			pxp->base + HW_PXP_DITHER_STORE_ADDR_1_CH1);

	__raw_writel(
			BF_PXP_DITHER_STORE_FILL_DATA_CH0_FILL_DATA_CH0(0),
			pxp->base + HW_PXP_DITHER_STORE_FILL_DATA_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_D_MASK0_H_CH0_D_MASK0_H_CH0(0xffffff),
			pxp->base + HW_PXP_DITHER_STORE_D_MASK0_H_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_D_MASK0_L_CH0_D_MASK0_L_CH0(0x0),
			pxp->base + HW_PXP_DITHER_STORE_D_MASK0_L_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_D_MASK1_H_CH0_D_MASK1_H_CH0(0x0),
			pxp->base + HW_PXP_DITHER_STORE_D_MASK1_H_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_D_MASK1_L_CH0_D_MASK1_L_CH0(0xff),
			pxp->base + HW_PXP_DITHER_STORE_D_MASK1_L_CH0);

	__raw_writel(
			BF_PXP_DITHER_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH0(32) |
			BF_PXP_DITHER_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG0(0) |
			BF_PXP_DITHER_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH1(32)|
			BF_PXP_DITHER_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG1(1) |
			BF_PXP_DITHER_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH2(0)|
			BF_PXP_DITHER_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG2(0)|
			BF_PXP_DITHER_STORE_D_SHIFT_L_CH0_D_SHIFT_WIDTH3(0)|
			BF_PXP_DITHER_STORE_D_SHIFT_L_CH0_D_SHIFT_FLAG3(0),
			pxp->base + HW_PXP_DITHER_STORE_D_SHIFT_L_CH0);
}

static void pxp_set_final_lut_data(struct pxps *pxp)
{
	__raw_writel(
			BF_PXP_DITHER_FINAL_LUT_DATA0_DATA0(0x0) |
			BF_PXP_DITHER_FINAL_LUT_DATA0_DATA1(0x0) |
			BF_PXP_DITHER_FINAL_LUT_DATA0_DATA2(0x0) |
			BF_PXP_DITHER_FINAL_LUT_DATA0_DATA3(0x0),
			pxp->base + HW_PXP_DITHER_FINAL_LUT_DATA0);

	__raw_writel(
			BF_PXP_DITHER_FINAL_LUT_DATA1_DATA4(0x0) |
			BF_PXP_DITHER_FINAL_LUT_DATA1_DATA5(0x0) |
			BF_PXP_DITHER_FINAL_LUT_DATA1_DATA6(0x0) |
			BF_PXP_DITHER_FINAL_LUT_DATA1_DATA7(0x0),
			pxp->base + HW_PXP_DITHER_FINAL_LUT_DATA1);

	__raw_writel(
			BF_PXP_DITHER_FINAL_LUT_DATA2_DATA8(0xff) |
			BF_PXP_DITHER_FINAL_LUT_DATA2_DATA9(0xff) |
			BF_PXP_DITHER_FINAL_LUT_DATA2_DATA10(0xff)|
			BF_PXP_DITHER_FINAL_LUT_DATA2_DATA11(0xff),
			pxp->base + HW_PXP_DITHER_FINAL_LUT_DATA2);

	__raw_writel(
			BF_PXP_DITHER_FINAL_LUT_DATA3_DATA12(0xff) |
			BF_PXP_DITHER_FINAL_LUT_DATA3_DATA13(0xff) |
			BF_PXP_DITHER_FINAL_LUT_DATA3_DATA14(0xff) |
			BF_PXP_DITHER_FINAL_LUT_DATA3_DATA15(0xff),
			pxp->base + HW_PXP_DITHER_FINAL_LUT_DATA3);
}

static void pxp_dithering_process(struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_proc_data *proc_data = &pxp_conf->proc_data;

	dither_prefetch_config(pxp);
	dither_store_config(pxp);
	pxp_sram_init(pxp, DITHER0_LUT, (u32)dither_data_8x8, 64);

	__raw_writel(
			BF_PXP_INIT_MEM_CTRL_ADDR(0) |
			BF_PXP_INIT_MEM_CTRL_SELECT(0) |/*select the lut memory for access */
			BF_PXP_INIT_MEM_CTRL_START(1),
			pxp->base + HW_PXP_INIT_MEM_CTRL);


	{
		int i;
		for (i = 0; i < 64; i++)
			__raw_writel(
					BF_PXP_INIT_MEM_DATA_DATA(dither_data_8x8[i]),
					pxp->base + HW_PXP_INIT_MEM_DATA);
	}

	__raw_writel(
			BF_PXP_INIT_MEM_CTRL_ADDR(0) |
			BF_PXP_INIT_MEM_CTRL_SELECT(0) |/*select the lut memory for access*/
			BF_PXP_INIT_MEM_CTRL_START(0),
			pxp->base + HW_PXP_INIT_MEM_CTRL);

	__raw_writel(
			BF_PXP_DITHER_CTRL_ENABLE0            (1) |
			BF_PXP_DITHER_CTRL_ENABLE1            (0) |
			BF_PXP_DITHER_CTRL_ENABLE2            (0) |
			BF_PXP_DITHER_CTRL_DITHER_MODE2       (0) |
			BF_PXP_DITHER_CTRL_DITHER_MODE1       (0) |
			BF_PXP_DITHER_CTRL_DITHER_MODE0       (proc_data->dither_mode) |
			BF_PXP_DITHER_CTRL_LUT_MODE           (0) |
			BF_PXP_DITHER_CTRL_IDX_MATRIX0_SIZE   (1) |
			BF_PXP_DITHER_CTRL_IDX_MATRIX1_SIZE   (0) |
			BF_PXP_DITHER_CTRL_IDX_MATRIX2_SIZE   (0) |
			BF_PXP_DITHER_CTRL_BUSY2              (0) |
			BF_PXP_DITHER_CTRL_BUSY1              (0) |
			BF_PXP_DITHER_CTRL_BUSY0              (0),
			pxp->base + HW_PXP_DITHER_CTRL);

	switch(proc_data->dither_mode) {
		case PXP_DITHER_PASS_THROUGH:
			/* no more settings required */
			break;
		case PXP_DITHER_FLOYD:
		case PXP_DITHER_ATKINSON:
		case PXP_DITHER_ORDERED:
			if(!proc_data->quant_bit || proc_data->quant_bit > 7) {
				dev_err(pxp->dev, "unsupported quantization bit number!\n");
				return;
			}
			__raw_writel(
					BF_PXP_DITHER_CTRL_FINAL_LUT_ENABLE(1) |
					BF_PXP_DITHER_CTRL_NUM_QUANT_BIT(proc_data->quant_bit),
					pxp->base + HW_PXP_DITHER_CTRL_SET);
			pxp_set_final_lut_data(pxp);

			break;
		case PXP_DITHER_QUANT_ONLY:
			if(!proc_data->quant_bit || proc_data->quant_bit > 7) {
				dev_err(pxp->dev, "unsupported quantization bit number!\n");
				return;
			}
			__raw_writel(
					BF_PXP_DITHER_CTRL_NUM_QUANT_BIT(proc_data->quant_bit),
					pxp->base + HW_PXP_DITHER_CTRL_SET);
			break;
		default:
			/* unknown mode */
			dev_err(pxp->dev, "unknown dithering mode passed!\n");
			__raw_writel(0x0, pxp->base + HW_PXP_DITHER_CTRL);
			return;
	}
}

static void pxp_start2(struct pxps *pxp)
{
	struct pxp_config_data *pxp_conf = &pxp->pxp_conf_state;
	struct pxp_proc_data *proc_data = &pxp_conf->proc_data;
	int dither_wfe_a_handshake = 0;
	int wfe_a_b_handshake = 0;
	int count = 0;

	int wfe_a_enable = ((proc_data->engine_enable & PXP_ENABLE_WFE_A) == PXP_ENABLE_WFE_A);
	int wfe_b_enable = ((proc_data->engine_enable & PXP_ENABLE_WFE_B) == PXP_ENABLE_WFE_B);
	int dither_enable = ((proc_data->engine_enable & PXP_ENABLE_DITHER) == PXP_ENABLE_DITHER);
	int handshake = ((proc_data->engine_enable & PXP_ENABLE_HANDSHAKE) == PXP_ENABLE_HANDSHAKE);
	int dither_bypass = ((proc_data->engine_enable & PXP_ENABLE_DITHER_BYPASS) == PXP_ENABLE_DITHER_BYPASS);

	if (dither_enable)
		count++;
	if (wfe_a_enable)
		count++;
	if (wfe_b_enable)
		count++;

	if (count == 0)
		return;
	if (handshake && (count == 1)) {
		dev_warn(pxp->dev, "Warning: Can not use handshake mode when "
				"only one sub-block is enabled!\n");
		handshake = 0;
	}

	if (handshake && wfe_b_enable && (wfe_a_enable == 0)) {
		dev_err(pxp->dev, "WFE_B only works when WFE_A is enabled!\n");
		return;
	}

	if (handshake && dither_enable && wfe_a_enable)
		dither_wfe_a_handshake = 1;
	if (handshake && wfe_a_enable && wfe_b_enable)
		wfe_a_b_handshake = 1;

	dev_dbg(pxp->dev, "handshake %d, dither_wfe_a_handshake %d, "
				"wfe_a_b_handshake %d, dither_bypass %d\n",
				handshake,
				dither_wfe_a_handshake,
				wfe_a_b_handshake,
				dither_bypass);

	if (handshake) {
		/* for handshake, we only enable the last completion INT */
		if (wfe_b_enable)
			__raw_writel(0x8000, pxp->base + HW_PXP_IRQ_MASK);
		else if (wfe_a_enable)
			__raw_writel(0x4000, pxp->base + HW_PXP_IRQ_MASK);

		/* Dither fetch */
		__raw_writel(
			BF_PXP_DITHER_FETCH_CTRL_CH0_CH_EN(1) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_BLOCK_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_BLOCK_16(0)|
			BF_PXP_DITHER_FETCH_CTRL_CH0_HANDSHAKE_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_BYPASS_PIXEL_EN(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_HIGH_BYTE(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_HFLIP(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_VFLIP(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_ROTATION_ANGLE(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_RD_NUM_BYTES(32) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_HANDSHAKE_SCAN_LINE_NUM(0) |
			BF_PXP_DITHER_FETCH_CTRL_CH0_ARBIT_EN(0),
			pxp->base + HW_PXP_DITHER_FETCH_CTRL_CH0);

		if (dither_bypass) {
			/* Dither store */
			__raw_writel(
			BF_PXP_DITHER_STORE_CTRL_CH0_CH_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH0_BLOCK_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_BLOCK_16(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_HANDSHAKE_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_ARRAY_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_ARRAY_LINE_NUM(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_STORE_BYPASS_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH0_STORE_MEMORY_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH0_PACK_IN_SEL(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_FILL_DATA_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_WR_NUM_BYTES(32)|
			BF_PXP_DITHER_STORE_CTRL_CH0_COMBINE_2CHANNEL(0) |
			BF_PXP_DITHER_STORE_CTRL_CH0_ARBIT_EN(0),
			pxp->base + HW_PXP_DITHER_STORE_CTRL_CH0);

			/* WFE_A fetch */
			__raw_writel(
			BF_PXP_WFA_FETCH_CTRL_BF1_EN(1) |
			BF_PXP_WFA_FETCH_CTRL_BF1_HSK_MODE(0) |
			BF_PXP_WFA_FETCH_CTRL_BF1_BYTES_PP(2) |
			BF_PXP_WFA_FETCH_CTRL_BF1_LINE_MODE(0) |
			BF_PXP_WFA_FETCH_CTRL_BF1_SRAM_IF(0) |
			BF_PXP_WFA_FETCH_CTRL_BF1_BURST_LEN(0) |
			BF_PXP_WFA_FETCH_CTRL_BF1_BYPASS_MODE(1) |
			BF_PXP_WFA_FETCH_CTRL_BF2_EN(1) |
			BF_PXP_WFA_FETCH_CTRL_BF2_HSK_MODE(0) |
			BF_PXP_WFA_FETCH_CTRL_BF2_BYTES_PP(1) |
			BF_PXP_WFA_FETCH_CTRL_BF2_LINE_MODE(0) |
			BF_PXP_WFA_FETCH_CTRL_BF2_SRAM_IF(0) |
			BF_PXP_WFA_FETCH_CTRL_BF2_BURST_LEN(0) |
			BF_PXP_WFA_FETCH_CTRL_BF2_BYPASS_MODE(0),
			pxp->base + HW_PXP_WFA_FETCH_CTRL);

		} else if (dither_wfe_a_handshake) {
			/* Dither store */
			__raw_writel(
			BF_PXP_DITHER_STORE_CTRL_CH0_CH_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH0_BLOCK_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_BLOCK_16(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_HANDSHAKE_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH0_ARRAY_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH0_ARRAY_LINE_NUM(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_STORE_BYPASS_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_STORE_MEMORY_EN(1)|
			BF_PXP_DITHER_STORE_CTRL_CH0_PACK_IN_SEL(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_FILL_DATA_EN(0)|
			BF_PXP_DITHER_STORE_CTRL_CH0_WR_NUM_BYTES(32)|
			BF_PXP_DITHER_STORE_CTRL_CH0_COMBINE_2CHANNEL(0) |
			BF_PXP_DITHER_STORE_CTRL_CH0_ARBIT_EN(0),
			pxp->base + HW_PXP_DITHER_STORE_CTRL_CH0);

			/* WFE_A fetch */
			__raw_writel(
			BF_PXP_WFA_FETCH_CTRL_BF1_EN(1) |
			BF_PXP_WFA_FETCH_CTRL_BF1_HSK_MODE(1) |
			BF_PXP_WFA_FETCH_CTRL_BF1_BYTES_PP(0) |
			BF_PXP_WFA_FETCH_CTRL_BF1_LINE_MODE(0) |
			BF_PXP_WFA_FETCH_CTRL_BF1_SRAM_IF(0) |
			BF_PXP_WFA_FETCH_CTRL_BF1_BURST_LEN(0) |
			BF_PXP_WFA_FETCH_CTRL_BF1_BYPASS_MODE(0) |
			BF_PXP_WFA_FETCH_CTRL_BF2_EN(1) |
			BF_PXP_WFA_FETCH_CTRL_BF2_HSK_MODE(0) |
			BF_PXP_WFA_FETCH_CTRL_BF2_BYTES_PP(1) |
			BF_PXP_WFA_FETCH_CTRL_BF2_LINE_MODE(0) |
			BF_PXP_WFA_FETCH_CTRL_BF2_SRAM_IF(0) |
			BF_PXP_WFA_FETCH_CTRL_BF2_BURST_LEN(0) |
			BF_PXP_WFA_FETCH_CTRL_BF2_BYPASS_MODE(0),
			pxp->base + HW_PXP_WFA_FETCH_CTRL);
		}

		if (wfe_a_b_handshake) {
			/* WFE_A Store */
			__raw_writel(
			BF_PXP_WFE_A_STORE_CTRL_CH1_CH_EN(1)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_BLOCK_EN(0)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_BLOCK_16(0)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_HANDSHAKE_EN(1)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_ARRAY_EN(1)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_ARRAY_LINE_NUM(1)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_STORE_BYPASS_EN(0)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_STORE_MEMORY_EN(1)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_PACK_IN_SEL(1)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_WR_NUM_BYTES(16),
			pxp->base + HW_PXP_WFE_A_STORE_CTRL_CH1);

			/* WFE_B fetch */
			__raw_writel(
			BF_PXP_WFB_FETCH_CTRL_BF1_EN(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_HSK_MODE(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BYTES_PP(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_LINE_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_SRAM_IF(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BURST_LEN(0) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BORDER_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF1_BYPASS_MODE(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_EN(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_HSK_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BYTES_PP(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_LINE_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_SRAM_IF(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BURST_LEN(0) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BORDER_MODE(1) |
			BF_PXP_WFB_FETCH_CTRL_BF2_BYPASS_MODE(0),
			pxp->base + HW_PXP_WFB_FETCH_CTRL);
		} else {
			/* WFE_A Store */
			__raw_writel(
			BF_PXP_WFE_A_STORE_CTRL_CH1_CH_EN(1)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_BLOCK_EN(0)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_BLOCK_16(0)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_HANDSHAKE_EN(0)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_ARRAY_EN(0)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_ARRAY_LINE_NUM(0)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_STORE_BYPASS_EN(0)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_STORE_MEMORY_EN(1)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_PACK_IN_SEL(1)|
			BF_PXP_WFE_A_STORE_CTRL_CH1_WR_NUM_BYTES(16),
			pxp->base + HW_PXP_WFE_A_STORE_CTRL_CH1);
		}

		/* trigger operation */
		__raw_writel(
		BF_PXP_CTRL_ENABLE(1) |
		BF_PXP_CTRL_IRQ_ENABLE(0) |
		BF_PXP_CTRL_NEXT_IRQ_ENABLE(0) |
		BF_PXP_CTRL_LUT_DMA_IRQ_ENABLE(0) |
		BF_PXP_CTRL_ENABLE_LCD0_HANDSHAKE(1) |
		BF_PXP_CTRL_HANDSHAKE_ABORT_SKIP(1) |
		BF_PXP_CTRL_ROTATE0(0) |
		BF_PXP_CTRL_HFLIP0(0) |
		BF_PXP_CTRL_VFLIP0(0) |
		BF_PXP_CTRL_ROTATE1(0) |
		BF_PXP_CTRL_HFLIP1(0) |
		BF_PXP_CTRL_VFLIP1(0) |
		BF_PXP_CTRL_ENABLE_PS_AS_OUT(0) |
		BF_PXP_CTRL_ENABLE_DITHER(dither_enable) |
		BF_PXP_CTRL_ENABLE_WFE_A(wfe_a_enable) |
		BF_PXP_CTRL_ENABLE_WFE_B(wfe_b_enable) |
		BF_PXP_CTRL_ENABLE_INPUT_FETCH_STORE(0) |
		BF_PXP_CTRL_ENABLE_ALPHA_B(0) |
		BF_PXP_CTRL_BLOCK_SIZE(1) |
		BF_PXP_CTRL_ENABLE_CSC2(0) |
		BF_PXP_CTRL_ENABLE_LUT(1) |
		BF_PXP_CTRL_ENABLE_ROTATE0(0) |
		BF_PXP_CTRL_ENABLE_ROTATE1(0) |
		BF_PXP_CTRL_EN_REPEAT(0),
		pxp->base + HW_PXP_CTRL);

		return;
	}

	__raw_writel(
			BF_PXP_CTRL_ENABLE(1) |
			BF_PXP_CTRL_IRQ_ENABLE(0) |
			BF_PXP_CTRL_NEXT_IRQ_ENABLE(0) |
			BF_PXP_CTRL_LUT_DMA_IRQ_ENABLE(0) |
			BF_PXP_CTRL_ENABLE_LCD0_HANDSHAKE(0) |
			BF_PXP_CTRL_ROTATE0(0) |
			BF_PXP_CTRL_HFLIP0(0) |
			BF_PXP_CTRL_VFLIP0(0) |
			BF_PXP_CTRL_ROTATE1(0) |
			BF_PXP_CTRL_HFLIP1(0) |
			BF_PXP_CTRL_VFLIP1(0) |
			BF_PXP_CTRL_ENABLE_PS_AS_OUT(0) |
			BF_PXP_CTRL_ENABLE_DITHER(dither_enable) |
			BF_PXP_CTRL_ENABLE_WFE_A(wfe_a_enable) |
			BF_PXP_CTRL_ENABLE_WFE_B(wfe_b_enable) |
			BF_PXP_CTRL_ENABLE_INPUT_FETCH_STORE(0) |
			BF_PXP_CTRL_ENABLE_ALPHA_B(0) |
			BF_PXP_CTRL_BLOCK_SIZE(0) |
			BF_PXP_CTRL_ENABLE_CSC2(0) |
			BF_PXP_CTRL_ENABLE_LUT(0) |
			BF_PXP_CTRL_ENABLE_ROTATE0(0) |
			BF_PXP_CTRL_ENABLE_ROTATE1(0) |
			BF_PXP_CTRL_EN_REPEAT(0),
			pxp->base + HW_PXP_CTRL);

	__raw_writel(
			BF_PXP_CTRL2_ENABLE                   (0) |
			BF_PXP_CTRL2_ROTATE0                  (0) |
			BF_PXP_CTRL2_HFLIP0                   (0) |
			BF_PXP_CTRL2_VFLIP0                   (0) |
			BF_PXP_CTRL2_ROTATE1                  (0) |
			BF_PXP_CTRL2_HFLIP1                   (0) |
			BF_PXP_CTRL2_VFLIP1                   (0) |
			BF_PXP_CTRL2_ENABLE_DITHER            (0) |
			BF_PXP_CTRL2_ENABLE_WFE_A             (0) |
			BF_PXP_CTRL2_ENABLE_WFE_B             (0) |
			BF_PXP_CTRL2_ENABLE_INPUT_FETCH_STORE (0) |
			BF_PXP_CTRL2_ENABLE_ALPHA_B           (0) |
			BF_PXP_CTRL2_BLOCK_SIZE               (0) |
			BF_PXP_CTRL2_ENABLE_CSC2              (0) |
			BF_PXP_CTRL2_ENABLE_LUT               (0) |
			BF_PXP_CTRL2_ENABLE_ROTATE0           (0) |
			BF_PXP_CTRL2_ENABLE_ROTATE1           (0),
			pxp->base + HW_PXP_CTRL2);

	dump_pxp_reg2(pxp);
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
		mutex_init(&pxp_chan->chan_mutex);

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
	{ .compatible = "fsl,imx7d-pxp-dma", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_pxpdma_dt_ids);

static int pxp_probe(struct platform_device *pdev)
{
	struct pxps *pxp;
	struct resource *res;
	int irq, std_irq;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0) {
		err = -ENODEV;
		goto exit;
	}

	std_irq = platform_get_irq(pdev, 1);
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
	sema_init(&pxp->sema, 1);

	pxp->base = devm_ioremap_resource(&pdev->dev, res);
	if (pxp->base == NULL) {
		dev_err(&pdev->dev, "Couldn't ioremap regs\n");
		err = -ENODEV;
		goto exit;
	}
	pxp_reg_base = pxp->base;

	pxp->pdev = pdev;

	pxp->clk = devm_clk_get(&pdev->dev, "pxp-axi");

	err = devm_request_irq(&pdev->dev, pxp->irq, pxp_irq, 0,
				"pxp-dmaengine", pxp);
	if (err)
		goto exit;

	err = devm_request_irq(&pdev->dev, std_irq, pxp_irq, 0,
				"pxp-dmaengine-std", pxp);
	if (err)
		goto exit;

	/* enable all the possible irq raised by PXP */
	__raw_writel(0xffff, pxp->base + HW_PXP_IRQ_MASK);

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

#ifdef	CONFIG_MXC_FPGA_M4_TEST
	fpga_tcml_base = ioremap(FPGA_TCML_ADDR, SZ_32K);
	if (fpga_tcml_base == NULL) {
		dev_err(&pdev->dev,
			"get fpga_tcml_base error.\n");
		goto exit;
	}
	pinctrl_base = ioremap(PINCTRL, SZ_4K);
	if (pinctrl_base == NULL) {
		dev_err(&pdev->dev,
			"get fpga_tcml_base error.\n");
		goto exit;
	}

	__raw_writel(0xC0000000, pinctrl_base + 0x08);
	__raw_writel(0x3, pinctrl_base + PIN_DOUT);
	int i;
	for (i = 0; i < 1024 * 32 / 4; i++) {
		*(((unsigned int *)(fpga_tcml_base)) + i) = cm4_image[i];
	}
#endif
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
	cancel_work_sync(&pxp->work);
	del_timer_sync(&pxp->clk_timer);
	clk_disable_unprepare(pxp->clk);
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
			.name = "imx-pxp-v3",
			.of_match_table = of_match_ptr(imx_pxpdma_dt_ids),
			.pm = &pxp_pm_ops,
		   },
	.probe = pxp_probe,
	.remove = pxp_remove,
};

static int __init pxp_init(void)
{
        return platform_driver_register(&pxp_driver);
}
late_initcall(pxp_init);

static void __exit pxp_exit(void)
{
        platform_driver_unregister(&pxp_driver);
}
module_exit(pxp_exit);


MODULE_DESCRIPTION("i.MX PxP driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
