/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is misc functions(mipi, fimc-lite control)
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Jiyoung Shin<idon.shin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/memory.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <linux/jiffies.h>

#include <media/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <linux/gpio.h>
#include <linux/gpio_event.h>
#include <plat/gpio-cfg.h>
#include <mach/map.h>
#include <mach/regs-clock.h>

#include "fimc-is-core.h"

/* PMU for FIMC-IS*/
#define FIMCLITE_REG_BASE		(S5P_VA_FIMCLITE0)  /* phy : 0x13c0_0000 */
#define MIPICSI_REG_BASE		(S5P_VA_MIPICSI0)   /* phy : 0x13c2_0000 */

#define FLITE_MAX_RESET_READY_TIME	(20) /* 100ms */
#define FLITE_MAX_WIDTH_SIZE			(8192)
#define FLITE_MAX_HEIGHT_SIZE			(8192)


/*FIMCLite*/
/* Camera Source size */
#define FLITE_REG_CISRCSIZE								0x00
#define FLITE_REG_CISRCSIZE_SIZE_H(x)					((x) << 16)
#define FLITE_REG_CISRCSIZE_SIZE_V(x)					((x) << 0)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_YCBYCR		(0 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_YCRYCB		(1 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_CBYCRY		(2 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_CRYCBY		(3 << 14)

/* Global control */
#define FLITE_REG_CIGCTRL								0x04
#define FLITE_REG_CIGCTRL_YUV422_1P					(0x1E << 24)
#define FLITE_REG_CIGCTRL_RAW8						(0x2A << 24)
#define FLITE_REG_CIGCTRL_RAW10						(0x2B << 24)
#define FLITE_REG_CIGCTRL_RAW12						(0x2C << 24)
#define FLITE_REG_CIGCTRL_RAW14						(0x2D << 24)

/* User defined formats. x = 0...0xF. */
#define FLITE_REG_CIGCTRL_USER(x)						(0x30 + x - 1)
#define FLITE_REG_CIGCTRL_SHADOWMASK_DISABLE		(1 << 21)
#define FLITE_REG_CIGCTRL_ODMA_DISABLE				(1 << 20)
#define FLITE_REG_CIGCTRL_SWRST_REQ					(1 << 19)
#define FLITE_REG_CIGCTRL_SWRST_RDY					(1 << 18)
#define FLITE_REG_CIGCTRL_SWRST						(1 << 17)
#define FLITE_REG_CIGCTRL_TEST_PATTERN_COLORBAR		(1 << 15)
#define FLITE_REG_CIGCTRL_INVPOLPCLK					(1 << 14)
#define FLITE_REG_CIGCTRL_INVPOLVSYNC					(1 << 13)
#define FLITE_REG_CIGCTRL_INVPOLHREF					(1 << 12)
#define FLITE_REG_CIGCTRL_IRQ_LASTEN0_ENABLE			(0 << 8)
#define FLITE_REG_CIGCTRL_IRQ_LASTEN0_DISABLE		(1 << 8)
#define FLITE_REG_CIGCTRL_IRQ_ENDEN0_ENABLE			(0 << 7)
#define FLITE_REG_CIGCTRL_IRQ_ENDEN0_DISABLE			(1 << 7)
#define FLITE_REG_CIGCTRL_IRQ_STARTEN0_ENABLE		(0 << 6)
#define FLITE_REG_CIGCTRL_IRQ_STARTEN0_DISABLE		(1 << 6)
#define FLITE_REG_CIGCTRL_IRQ_OVFEN0_ENABLE			(0 << 5)
#define FLITE_REG_CIGCTRL_IRQ_OVFEN0_DISABLE			(1 << 5)
#define FLITE_REG_CIGCTRL_SELCAM_MIPI					(1 << 3)

/* Image Capture Enable */
#define FLITE_REG_CIIMGCPT								0x08
#define FLITE_REG_CIIMGCPT_IMGCPTEN					(1 << 31)
#define FLITE_REG_CIIMGCPT_CPT_FREN					(1 << 25)
#define FLITE_REG_CIIMGCPT_CPT_FRPTR(x)				((x) << 19)
#define FLITE_REG_CIIMGCPT_CPT_MOD_FRCNT				(1 << 18)
#define FLITE_REG_CIIMGCPT_CPT_MOD_FREN				(0 << 18)
#define FLITE_REG_CIIMGCPT_CPT_FRCNT(x)				((x) << 10)

/* Capture Sequence */
#define FLITE_REG_CICPTSEQ								0x0C
#define FLITE_REG_CPT_FRSEQ(x)							((x) << 0)

/* Camera Window Offset */
#define FLITE_REG_CIWDOFST								0x10
#define FLITE_REG_CIWDOFST_WINOFSEN					(1 << 31)
#define FLITE_REG_CIWDOFST_CLROVIY					(1 << 31)
#define FLITE_REG_CIWDOFST_WINHOROFST(x)				((x) << 16)
#define FLITE_REG_CIWDOFST_HOROFF_MASK				(0x1fff << 16)
#define FLITE_REG_CIWDOFST_CLROVFICB					(1 << 15)
#define FLITE_REG_CIWDOFST_CLROVFICR					(1 << 14)
#define FLITE_REG_CIWDOFST_WINVEROFST(x)				((x) << 0)
#define FLITE_REG_CIWDOFST_VEROFF_MASK				(0x1fff << 0)

/* Cmaera Window Offset2 */
#define FLITE_REG_CIWDOFST2							0x14
#define FLITE_REG_CIWDOFST2_WINHOROFST2(x)			((x) << 16)
#define FLITE_REG_CIWDOFST2_WINVEROFST2(x)			((x) << 0)

/* Camera Output DMA Format */
#define FLITE_REG_CIODMAFMT							0x18
#define FLITE_REG_CIODMAFMT_1D_DMA					(1 << 15)
#define FLITE_REG_CIODMAFMT_2D_DMA					(0 << 15)
#define FLITE_REG_CIODMAFMT_PACK12					(1 << 14)
#define FLITE_REG_CIODMAFMT_NORMAL					(0 << 14)
#define FLITE_REG_CIODMAFMT_CRYCBY					(0 << 4)
#define FLITE_REG_CIODMAFMT_CBYCRY					(1 << 4)
#define FLITE_REG_CIODMAFMT_YCRYCB					(2 << 4)
#define FLITE_REG_CIODMAFMT_YCBYCR					(3 << 4)

/* Camera Output Canvas */
#define FLITE_REG_CIOCAN								0x20
#define FLITE_REG_CIOCAN_OCAN_V(x)					((x) << 16)
#define FLITE_REG_CIOCAN_OCAN_H(x)					((x) << 0)

/* Camera Output DMA Offset */
#define FLITE_REG_CIOOFF								0x24
#define FLITE_REG_CIOOFF_OOFF_V(x)						((x) << 16)
#define FLITE_REG_CIOOFF_OOFF_H(x)						((x) << 0)

/* Camera Output DMA Address */
#define FLITE_REG_CIOSA									0x30
#define FLITE_REG_CIOSA_OSA(x)							((x) << 0)

/* Camera Status */
#define FLITE_REG_CISTATUS								0x40
#define FLITE_REG_CISTATUS_MIPI_VVALID				(1 << 22)
#define FLITE_REG_CISTATUS_MIPI_HVALID				(1 << 21)
#define FLITE_REG_CISTATUS_MIPI_DVALID				(1 << 20)
#define FLITE_REG_CISTATUS_ITU_VSYNC					(1 << 14)
#define FLITE_REG_CISTATUS_ITU_HREFF					(1 << 13)
#define FLITE_REG_CISTATUS_OVFIY						(1 << 10)
#define FLITE_REG_CISTATUS_OVFICB						(1 << 9)
#define FLITE_REG_CISTATUS_OVFICR						(1 << 8)
#define FLITE_REG_CISTATUS_IRQ_SRC_OVERFLOW			(1 << 7)
#define FLITE_REG_CISTATUS_IRQ_SRC_LASTCAPEND		(1 << 6)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMSTART			(1 << 5)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMEND			(1 << 4)
#define FLITE_REG_CISTATUS_IRQ_CAM					(1 << 0)
#define FLITE_REG_CISTATUS_IRQ_MASK					(0xf << 4)

/* Camera Status2 */
#define FLITE_REG_CISTATUS2							0x44
#define FLITE_REG_CISTATUS2_LASTCAPEND				(1 << 1)
#define FLITE_REG_CISTATUS2_FRMEND					(1 << 0)

/* Qos Threshold */
#define FLITE_REG_CITHOLD								0xF0
#define FLITE_REG_CITHOLD_W_QOS_EN					(1 << 30)
#define FLITE_REG_CITHOLD_WTH_QOS(x)					((x) << 0)

/* Camera General Purpose */
#define FLITE_REG_CIGENERAL							0xFC
#define FLITE_REG_CIGENERAL_CAM_A						(0 << 0)
#define FLITE_REG_CIGENERAL_CAM_B						(1 << 0)


/*MIPI*/
/* CSIS global control */
#define S5PCSIS_CTRL									0x00
#define S5PCSIS_CTRL_DPDN_DEFAULT						(0 << 31)
#define S5PCSIS_CTRL_DPDN_SWAP						(1 << 31)
#define S5PCSIS_CTRL_ALIGN_32BIT						(1 << 20)
#define S5PCSIS_CTRL_UPDATE_SHADOW					(1 << 16)
#define S5PCSIS_CTRL_WCLK_EXTCLK						(1 << 8)
#define S5PCSIS_CTRL_RESET								(1 << 4)
#define S5PCSIS_CTRL_ENABLE							(1 << 0)

/* D-PHY control */
#define S5PCSIS_DPHYCTRL								0x04
#define S5PCSIS_DPHYCTRL_HSS_MASK						(0x1f << 27)
#define S5PCSIS_DPHYCTRL_ENABLE						(0x7 << 0)

#define S5PCSIS_CONFIG									0x08
#define S5PCSIS_CFG_FMT_YCBCR422_8BIT					(0x1e << 2)
#define S5PCSIS_CFG_FMT_RAW8							(0x2a << 2)
#define S5PCSIS_CFG_FMT_RAW10							(0x2b << 2)
#define S5PCSIS_CFG_FMT_RAW12							(0x2c << 2)
/* User defined formats, x = 1...4 */
#define S5PCSIS_CFG_FMT_USER(x)						((0x30 + x - 1) << 2)
#define S5PCSIS_CFG_FMT_MASK							(0x3f << 2)
#define S5PCSIS_CFG_NR_LANE_MASK						3

/* Interrupt mask. */
#define S5PCSIS_INTMSK									0x10
#define S5PCSIS_INTMSK_EN_ALL							0xf000103f
#define S5PCSIS_INTSRC									0x14

/* Pixel resolution */
#define S5PCSIS_RESOL									0x2c
#define CSIS_MAX_PIX_WIDTH								0xffff
#define CSIS_MAX_PIX_HEIGHT							0xffff

static void flite_hw_set_cam_source_size(struct flite_frame *f_frame)
{
	u32 cfg = 0;

	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CISRCSIZE);

	cfg |= FLITE_REG_CISRCSIZE_SIZE_H(f_frame->o_width);
	cfg |= FLITE_REG_CISRCSIZE_SIZE_V(f_frame->o_height);

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CISRCSIZE);

	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIOCAN);
	cfg |= FLITE_REG_CIOCAN_OCAN_H(f_frame->o_width);
	cfg |= FLITE_REG_CIOCAN_OCAN_V(f_frame->o_height);

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIOCAN);
}

static void flite_hw_set_cam_channel(void)
{
	u32 cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIGENERAL);

	cfg &= FLITE_REG_CIGENERAL_CAM_A;

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIGENERAL);
}

static void flite_hw_set_capture_start(void)
{
	u32 cfg = 0;

	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIIMGCPT);
	cfg |= FLITE_REG_CIIMGCPT_IMGCPTEN;

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIIMGCPT);
}

static void flite_hw_set_capture_stop(void)
{
	u32 cfg = 0;

	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIIMGCPT);
	cfg &= ~FLITE_REG_CIIMGCPT_IMGCPTEN;

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIIMGCPT);
}

static int flite_hw_set_source_format(void)
{
	u32 cfg = 0;

	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_RAW10;
	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);

	return 0;
}

static void flite_hw_set_output_dma(bool enable)
{
	u32 cfg = 0;
	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);

	if (enable)
		cfg &= ~FLITE_REG_CIGCTRL_ODMA_DISABLE;
	else
		cfg |= FLITE_REG_CIGCTRL_ODMA_DISABLE;

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
}

/* will use for pattern generation testing
static void flite_hw_set_test_pattern_enable(void)
{
	u32 cfg = 0;
	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_TEST_PATTERN_COLORBAR;

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
}
*/

static void flite_hw_set_config_irq(void)
{
	u32 cfg = 0;
	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
	cfg &= ~(FLITE_REG_CIGCTRL_INVPOLPCLK | FLITE_REG_CIGCTRL_INVPOLVSYNC
			| FLITE_REG_CIGCTRL_INVPOLHREF);

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_interrupt_source(void)
{
	u32 cfg = 0;
	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_IRQ_LASTEN0_ENABLE;

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_interrupt_starten0_disable(void)
{
	u32 cfg = 0;
	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_IRQ_STARTEN0_DISABLE;

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_camera_type(void)
{
	u32 cfg = 0;
	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);

	cfg |= FLITE_REG_CIGCTRL_SELCAM_MIPI;

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_window_offset(struct flite_frame *f_frame)
{
	u32 cfg = 0;
	u32 hoff2, voff2;

	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CIWDOFST);
	cfg &= ~(FLITE_REG_CIWDOFST_HOROFF_MASK |
		FLITE_REG_CIWDOFST_VEROFF_MASK);
	cfg |= FLITE_REG_CIWDOFST_WINOFSEN |
		FLITE_REG_CIWDOFST_WINHOROFST(f_frame->offs_h) |
		FLITE_REG_CIWDOFST_WINVEROFST(f_frame->offs_v);

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIWDOFST);

	hoff2 = f_frame->o_width - f_frame->width - f_frame->offs_h;
	voff2 = f_frame->o_height - f_frame->height - f_frame->offs_v;
	cfg = FLITE_REG_CIWDOFST2_WINHOROFST2(hoff2) |
		FLITE_REG_CIWDOFST2_WINVEROFST2(voff2);

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CIWDOFST2);
}

static void flite_hw_set_last_capture_end_clear(void)
{
	u32 cfg = 0;

	cfg = readl(FIMCLITE_REG_BASE + FLITE_REG_CISTATUS2);
	cfg &= ~FLITE_REG_CISTATUS2_LASTCAPEND;

	writel(cfg, FIMCLITE_REG_BASE + FLITE_REG_CISTATUS2);
}

static void s5pcsis_enable_interrupts(bool on)
{
	u32 val = readl(MIPICSI_REG_BASE + S5PCSIS_INTMSK);

	val = on ? val | S5PCSIS_INTMSK_EN_ALL :
		   val & ~S5PCSIS_INTMSK_EN_ALL;
	writel(val, MIPICSI_REG_BASE + S5PCSIS_INTMSK);
}

static void s5pcsis_reset(void)
{
	u32 val = readl(MIPICSI_REG_BASE + S5PCSIS_CTRL);

	writel(val | S5PCSIS_CTRL_RESET, MIPICSI_REG_BASE + S5PCSIS_CTRL);
	udelay(10);
}

static void s5pcsis_system_enable(int on)
{
	u32 val;

	val = readl(MIPICSI_REG_BASE + S5PCSIS_CTRL);
	if (on) {
		val |= S5PCSIS_CTRL_ENABLE;
		val |= S5PCSIS_CTRL_WCLK_EXTCLK;
	} else
		val &= ~S5PCSIS_CTRL_ENABLE;
	writel(val, MIPICSI_REG_BASE + S5PCSIS_CTRL);

	val = readl(MIPICSI_REG_BASE + S5PCSIS_DPHYCTRL);
	if (on)
		val |= S5PCSIS_DPHYCTRL_ENABLE;
	else
		val &= ~S5PCSIS_DPHYCTRL_ENABLE;
	writel(val, MIPICSI_REG_BASE + S5PCSIS_DPHYCTRL);
}

/* Called with the state.lock mutex held */
static void __s5pcsis_set_format(struct flite_frame *f_frame)
{
	u32 val;

	/* Color format */
	val = readl(MIPICSI_REG_BASE + S5PCSIS_CONFIG);
	val = (val & ~S5PCSIS_CFG_FMT_MASK) | S5PCSIS_CFG_FMT_RAW10;
	writel(val, MIPICSI_REG_BASE + S5PCSIS_CONFIG);

	/* Pixel resolution */
	val = (f_frame->o_width << 16) | f_frame->o_height;
	writel(val, MIPICSI_REG_BASE + S5PCSIS_RESOL);
}

static void s5pcsis_set_hsync_settle(void)
{
	u32 val = readl(MIPICSI_REG_BASE + S5PCSIS_DPHYCTRL);

	val = (val & ~S5PCSIS_DPHYCTRL_HSS_MASK) | (0x6 << 28);
	writel(val, MIPICSI_REG_BASE + S5PCSIS_DPHYCTRL);
}

static void s5pcsis_set_params(struct flite_frame *f_frame)
{
	u32 val;

	val = readl(MIPICSI_REG_BASE + S5PCSIS_CONFIG);
	val = (val & ~S5PCSIS_CFG_NR_LANE_MASK) | (2 - 1);
	writel(val, MIPICSI_REG_BASE + S5PCSIS_CONFIG);

	__s5pcsis_set_format(f_frame);
	s5pcsis_set_hsync_settle();

	val = readl(MIPICSI_REG_BASE + S5PCSIS_CTRL);
	val &= ~S5PCSIS_CTRL_ALIGN_32BIT;

	/* Not using external clock. */
	val &= ~S5PCSIS_CTRL_WCLK_EXTCLK;

	writel(val, MIPICSI_REG_BASE + S5PCSIS_CTRL);

	/* Update the shadow register. */
	val = readl(MIPICSI_REG_BASE + S5PCSIS_CTRL);
	writel(val | S5PCSIS_CTRL_UPDATE_SHADOW, MIPICSI_REG_BASE + S5PCSIS_CTRL);
}

int start_fimc_lite(struct flite_frame *f_frame)
{
	flite_hw_set_cam_channel();
	flite_hw_set_cam_source_size(f_frame);
	flite_hw_set_camera_type();
	flite_hw_set_source_format();
	flite_hw_set_output_dma(false);

	flite_hw_set_interrupt_source();
	flite_hw_set_interrupt_starten0_disable();
	flite_hw_set_config_irq();
	flite_hw_set_window_offset(f_frame);
	/* flite_hw_set_test_pattern_enable(); */

	flite_hw_set_last_capture_end_clear();
	flite_hw_set_capture_start();

	return 0;
}

int stop_fimc_lite(void)
{
	flite_hw_set_capture_stop();
	return 0;
}

int enable_mipi(void)
{
	void __iomem *addr;
	u32 cfg;

	addr = S5P_MIPI_DPHY_CONTROL(0);

	cfg = __raw_readl(addr);
	cfg = (cfg | S5P_MIPI_DPHY_SRESETN);
	__raw_writel(cfg, addr);

	if (1) {
		cfg |= S5P_MIPI_DPHY_ENABLE;
	} else if (!(cfg & (S5P_MIPI_DPHY_SRESETN |
				S5P_MIPI_DPHY_MRESETN) & (~S5P_MIPI_DPHY_SRESETN))) {
		cfg &= ~S5P_MIPI_DPHY_ENABLE;
	}

	__raw_writel(cfg, addr);

	return 0;

}

int start_mipi_csi(struct flite_frame *f_frame)
{
	s5pcsis_reset();
	s5pcsis_set_params(f_frame);
	s5pcsis_system_enable(true);
	s5pcsis_enable_interrupts(true);

	return 0;
}

int stop_mipi_csi(void)
{
	s5pcsis_enable_interrupts(false);
	s5pcsis_system_enable(false);

	return 0;
}

