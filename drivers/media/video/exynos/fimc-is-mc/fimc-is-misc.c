/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is misc functions(mipi, fimc-lite control)
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
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
#include <media/exynos_fimc_is.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/map.h>
#include <mach/regs-clock.h>

#include "fimc-is-core.h"
#include "fimc-is-helper.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-misc.h"

/* PMU for FIMC-IS*/
#define FIMCLITE0_REG_BASE	(S5P_VA_FIMCLITE0)  /* phy : 0x13c0_0000 */
#define FIMCLITE1_REG_BASE	(S5P_VA_FIMCLITE1)  /* phy : 0x13c1_0000 */
#define MIPICSI0_REG_BASE	(S5P_VA_MIPICSI0)   /* phy : 0x13c2_0000 */
#define MIPICSI1_REG_BASE	(S5P_VA_MIPICSI1)   /* phy : 0x13c3_0000 */
#define FIMCLITE2_REG_BASE	(S5P_VA_FIMCLITE2)  /* phy : 0x13c9_0000 */

#define FLITE_MAX_RESET_READY_TIME	(20) /* 100ms */
#define FLITE_MAX_WIDTH_SIZE		(8192)
#define FLITE_MAX_HEIGHT_SIZE		(8192)


/*FIMCLite*/
/* Camera Source size */
#define FLITE_REG_CISRCSIZE				(0x00)
#define FLITE_REG_CISRCSIZE_SIZE_H(x)			((x) << 16)
#define FLITE_REG_CISRCSIZE_SIZE_V(x)			((x) << 0)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_YCBYCR		(0 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_YCRYCB		(1 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_CBYCRY		(2 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_CRYCBY		(3 << 14)

/* Global control */
#define FLITE_REG_CIGCTRL				0x04
#define FLITE_REG_CIGCTRL_YUV422_1P			(0x1E << 24)
#define FLITE_REG_CIGCTRL_RAW8				(0x2A << 24)
#define FLITE_REG_CIGCTRL_RAW10				(0x2B << 24)
#define FLITE_REG_CIGCTRL_RAW12				(0x2C << 24)
#define FLITE_REG_CIGCTRL_RAW14				(0x2D << 24)

/* User defined formats. x = 0...0xF. */
#define FLITE_REG_CIGCTRL_USER(x)			(0x30 + x - 1)
#define FLITE_REG_CIGCTRL_SHADOWMASK_DISABLE		(1 << 21)
#define FLITE_REG_CIGCTRL_ODMA_DISABLE			(1 << 20)
#define FLITE_REG_CIGCTRL_SWRST_REQ			(1 << 19)
#define FLITE_REG_CIGCTRL_SWRST_RDY			(1 << 18)
#define FLITE_REG_CIGCTRL_SWRST				(1 << 17)
#define FLITE_REG_CIGCTRL_TEST_PATTERN_COLORBAR		(1 << 15)
#define FLITE_REG_CIGCTRL_INVPOLPCLK			(1 << 14)
#define FLITE_REG_CIGCTRL_INVPOLVSYNC			(1 << 13)
#define FLITE_REG_CIGCTRL_INVPOLHREF			(1 << 12)
#define FLITE_REG_CIGCTRL_IRQ_LASTEN0_ENABLE		(0 << 8)
#define FLITE_REG_CIGCTRL_IRQ_LASTEN0_DISABLE		(1 << 8)
#define FLITE_REG_CIGCTRL_IRQ_ENDEN0_ENABLE		(0 << 7)
#define FLITE_REG_CIGCTRL_IRQ_ENDEN0_DISABLE		(1 << 7)
#define FLITE_REG_CIGCTRL_IRQ_STARTEN0_ENABLE		(0 << 6)
#define FLITE_REG_CIGCTRL_IRQ_STARTEN0_DISABLE		(1 << 6)
#define FLITE_REG_CIGCTRL_IRQ_OVFEN0_ENABLE		(0 << 5)
#define FLITE_REG_CIGCTRL_IRQ_OVFEN0_DISABLE		(1 << 5)
#define FLITE_REG_CIGCTRL_SELCAM_MIPI			(1 << 3)

/* Image Capture Enable */
#define FLITE_REG_CIIMGCPT				(0x08)
#define FLITE_REG_CIIMGCPT_IMGCPTEN			(1 << 31)
#define FLITE_REG_CIIMGCPT_CPT_FREN			(1 << 25)
#define FLITE_REG_CIIMGCPT_CPT_FRPTR(x)			((x) << 19)
#define FLITE_REG_CIIMGCPT_CPT_MOD_FRCNT		(1 << 18)
#define FLITE_REG_CIIMGCPT_CPT_MOD_FREN			(0 << 18)
#define FLITE_REG_CIIMGCPT_CPT_FRCNT(x)			((x) << 10)

/* Capture Sequence */
#define FLITE_REG_CICPTSEQ				(0x0C)
#define FLITE_REG_CPT_FRSEQ(x)				((x) << 0)

/* Camera Window Offset */
#define FLITE_REG_CIWDOFST				(0x10)
#define FLITE_REG_CIWDOFST_WINOFSEN			(1 << 31)
#define FLITE_REG_CIWDOFST_CLROVIY			(1 << 31)
#define FLITE_REG_CIWDOFST_WINHOROFST(x)		((x) << 16)
#define FLITE_REG_CIWDOFST_HOROFF_MASK			(0x1fff << 16)
#define FLITE_REG_CIWDOFST_CLROVFICB			(1 << 15)
#define FLITE_REG_CIWDOFST_CLROVFICR			(1 << 14)
#define FLITE_REG_CIWDOFST_WINVEROFST(x)		((x) << 0)
#define FLITE_REG_CIWDOFST_VEROFF_MASK			(0x1fff << 0)

/* Cmaera Window Offset2 */
#define FLITE_REG_CIWDOFST2				(0x14)
#define FLITE_REG_CIWDOFST2_WINHOROFST2(x)		((x) << 16)
#define FLITE_REG_CIWDOFST2_WINVEROFST2(x)		((x) << 0)

/* Camera Output DMA Format */
#define FLITE_REG_CIODMAFMT				(0x18)
#define FLITE_REG_CIODMAFMT_1D_DMA			(1 << 15)
#define FLITE_REG_CIODMAFMT_2D_DMA			(0 << 15)
#define FLITE_REG_CIODMAFMT_PACK12			(1 << 14)
#define FLITE_REG_CIODMAFMT_NORMAL			(0 << 14)
#define FLITE_REG_CIODMAFMT_CRYCBY			(0 << 4)
#define FLITE_REG_CIODMAFMT_CBYCRY			(1 << 4)
#define FLITE_REG_CIODMAFMT_YCRYCB			(2 << 4)
#define FLITE_REG_CIODMAFMT_YCBYCR			(3 << 4)

/* Camera Output Canvas */
#define FLITE_REG_CIOCAN				(0x20)
#define FLITE_REG_CIOCAN_OCAN_V(x)			((x) << 16)
#define FLITE_REG_CIOCAN_OCAN_H(x)			((x) << 0)

/* Camera Output DMA Offset */
#define FLITE_REG_CIOOFF				(0x24)
#define FLITE_REG_CIOOFF_OOFF_V(x)			((x) << 16)
#define FLITE_REG_CIOOFF_OOFF_H(x)			((x) << 0)

/* Camera Output DMA Address */
#define FLITE_REG_CIOSA					(0x30)
#define FLITE_REG_CIOSA_OSA(x)				((x) << 0)

/* Camera Status */
#define FLITE_REG_CISTATUS				(0x40)
#define FLITE_REG_CISTATUS_MIPI_VVALID			(1 << 22)
#define FLITE_REG_CISTATUS_MIPI_HVALID			(1 << 21)
#define FLITE_REG_CISTATUS_MIPI_DVALID			(1 << 20)
#define FLITE_REG_CISTATUS_ITU_VSYNC			(1 << 14)
#define FLITE_REG_CISTATUS_ITU_HREFF			(1 << 13)
#define FLITE_REG_CISTATUS_OVFIY			(1 << 10)
#define FLITE_REG_CISTATUS_OVFICB			(1 << 9)
#define FLITE_REG_CISTATUS_OVFICR			(1 << 8)
#define FLITE_REG_CISTATUS_IRQ_SRC_OVERFLOW		(1 << 7)
#define FLITE_REG_CISTATUS_IRQ_SRC_LASTCAPEND		(1 << 6)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMSTART		(1 << 5)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMEND		(1 << 4)
#define FLITE_REG_CISTATUS_IRQ_CAM			(1 << 0)
#define FLITE_REG_CISTATUS_IRQ_MASK			(0xf << 4)

/* Camera Status2 */
#define FLITE_REG_CISTATUS2				(0x44)
#define FLITE_REG_CISTATUS2_LASTCAPEND			(1 << 1)
#define FLITE_REG_CISTATUS2_FRMEND			(1 << 0)

/* Qos Threshold */
#define FLITE_REG_CITHOLD				(0xF0)
#define FLITE_REG_CITHOLD_W_QOS_EN			(1 << 30)
#define FLITE_REG_CITHOLD_WTH_QOS(x)			((x) << 0)

/* Camera General Purpose */
#define FLITE_REG_CIGENERAL				(0xFC)
#define FLITE_REG_CIGENERAL_CAM_A			(0 << 0)
#define FLITE_REG_CIGENERAL_CAM_B			(1 << 0)


/*MIPI*/
/* CSIS global control */
#define S5PCSIS_CTRL					(0x00)
#define S5PCSIS_CTRL_DPDN_DEFAULT			(0 << 31)
#define S5PCSIS_CTRL_DPDN_SWAP				(1 << 31)
#define S5PCSIS_CTRL_ALIGN_32BIT			(1 << 20)
#define S5PCSIS_CTRL_UPDATE_SHADOW			(1 << 16)
#define S5PCSIS_CTRL_WCLK_EXTCLK			(1 << 8)
#define S5PCSIS_CTRL_RESET				(1 << 4)
#define S5PCSIS_CTRL_ENABLE				(1 << 0)

/* D-PHY control */
#define S5PCSIS_DPHYCTRL				(0x04)
#define S5PCSIS_DPHYCTRL_HSS_MASK			(0x1f << 27)
#define S5PCSIS_DPHYCTRL_ENABLE				(0x7 << 0)

#define S5PCSIS_CONFIG					(0x08)
#define S5PCSIS_CFG_FMT_YCBCR422_8BIT			(0x1e << 2)
#define S5PCSIS_CFG_FMT_RAW8				(0x2a << 2)
#define S5PCSIS_CFG_FMT_RAW10				(0x2b << 2)
#define S5PCSIS_CFG_FMT_RAW12				(0x2c << 2)
/* User defined formats, x = 1...4 */
#define S5PCSIS_CFG_FMT_USER(x)				((0x30 + x - 1) << 2)
#define S5PCSIS_CFG_FMT_MASK				(0x3f << 2)
#define S5PCSIS_CFG_NR_LANE_MASK			(3)

/* Interrupt mask. */
#define S5PCSIS_INTMSK					(0x10)
#define S5PCSIS_INTMSK_EN_ALL				(0xfc00103f)
#define S5PCSIS_INTSRC					(0x14)

/* Pixel resolution */
#define S5PCSIS_RESOL					(0x2c)
#define CSIS_MAX_PIX_WIDTH				(0xffff)
#define CSIS_MAX_PIX_HEIGHT				(0xffff)

static void flite_hw_set_cam_source_size(unsigned long flite_reg_base,
					struct flite_frame *f_frame)
{
	u32 cfg = 0;

	cfg = readl(flite_reg_base + FLITE_REG_CISRCSIZE);

	cfg |= FLITE_REG_CISRCSIZE_SIZE_H(f_frame->o_width);
	cfg |= FLITE_REG_CISRCSIZE_SIZE_V(f_frame->o_height);

	writel(cfg, flite_reg_base + FLITE_REG_CISRCSIZE);

	cfg = readl(flite_reg_base + FLITE_REG_CIOCAN);
	cfg |= FLITE_REG_CIOCAN_OCAN_H(f_frame->o_width);
	cfg |= FLITE_REG_CIOCAN_OCAN_V(f_frame->o_height);

	writel(cfg, flite_reg_base + FLITE_REG_CIOCAN);
}

static void flite_hw_set_cam_channel(unsigned long flite_reg_base)
{
	u32 cfg = 0;

	if (flite_reg_base == (unsigned long)FIMCLITE0_REG_BASE) {
		cfg = FLITE_REG_CIGENERAL_CAM_A;
		writel(cfg, FIMCLITE0_REG_BASE + FLITE_REG_CIGENERAL);
		writel(cfg, FIMCLITE1_REG_BASE + FLITE_REG_CIGENERAL);
		writel(cfg, FIMCLITE2_REG_BASE + FLITE_REG_CIGENERAL);
	} else {
		cfg = FLITE_REG_CIGENERAL_CAM_B;
		writel(cfg, FIMCLITE0_REG_BASE + FLITE_REG_CIGENERAL);
		writel(cfg, FIMCLITE1_REG_BASE + FLITE_REG_CIGENERAL);
		writel(cfg, FIMCLITE2_REG_BASE + FLITE_REG_CIGENERAL);
	}
}

static void flite_hw_set_capture_start(unsigned long flite_reg_base)
{
	u32 cfg = 0;

	cfg = readl(flite_reg_base + FLITE_REG_CIIMGCPT);
	cfg |= FLITE_REG_CIIMGCPT_IMGCPTEN;

	writel(cfg, flite_reg_base + FLITE_REG_CIIMGCPT);
}

static void flite_hw_set_capture_stop(unsigned long flite_reg_base)
{
	u32 cfg = 0;

	cfg = readl(flite_reg_base + FLITE_REG_CIIMGCPT);
	cfg &= ~FLITE_REG_CIIMGCPT_IMGCPTEN;

	writel(cfg, flite_reg_base + FLITE_REG_CIIMGCPT);
}

static int flite_hw_set_source_format(unsigned long flite_reg_base)
{
	u32 cfg = 0;

	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_RAW10;
	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);

	return 0;
}

static void flite_hw_set_output_dma(unsigned long flite_reg_base, bool enable)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);

	if (enable)
		cfg &= ~FLITE_REG_CIGCTRL_ODMA_DISABLE;
	else
		cfg |= FLITE_REG_CIGCTRL_ODMA_DISABLE;

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}

/* will use for pattern generation testing
static void flite_hw_set_test_pattern_enable(void)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_TEST_PATTERN_COLORBAR;

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}
*/

static void flite_hw_set_config_irq(unsigned long flite_reg_base)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);
	cfg &= ~(FLITE_REG_CIGCTRL_INVPOLPCLK | FLITE_REG_CIGCTRL_INVPOLVSYNC
			| FLITE_REG_CIGCTRL_INVPOLHREF);

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_interrupt_source(unsigned long flite_reg_base)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_IRQ_LASTEN0_ENABLE;

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_interrupt_starten0_disable
					(unsigned long flite_reg_base)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_IRQ_STARTEN0_DISABLE;

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_camera_type(unsigned long flite_reg_base)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);

	cfg |= FLITE_REG_CIGCTRL_SELCAM_MIPI;

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_window_offset(unsigned long flite_reg_base,
					struct flite_frame *f_frame)
{
	u32 cfg = 0;
	u32 hoff2, voff2;

	cfg = readl(flite_reg_base + FLITE_REG_CIWDOFST);
	cfg &= ~(FLITE_REG_CIWDOFST_HOROFF_MASK |
		FLITE_REG_CIWDOFST_VEROFF_MASK);
	cfg |= FLITE_REG_CIWDOFST_WINOFSEN |
		FLITE_REG_CIWDOFST_WINHOROFST(f_frame->offs_h) |
		FLITE_REG_CIWDOFST_WINVEROFST(f_frame->offs_v);

	writel(cfg, flite_reg_base + FLITE_REG_CIWDOFST);

	hoff2 = f_frame->o_width - f_frame->width - f_frame->offs_h;
	voff2 = f_frame->o_height - f_frame->height - f_frame->offs_v;
	cfg = FLITE_REG_CIWDOFST2_WINHOROFST2(hoff2) |
		FLITE_REG_CIWDOFST2_WINVEROFST2(voff2);

	writel(cfg, flite_reg_base + FLITE_REG_CIWDOFST2);
}

static void flite_hw_set_last_capture_end_clear(unsigned long flite_reg_base)
{
	u32 cfg = 0;

	cfg = readl(flite_reg_base + FLITE_REG_CISTATUS2);
	cfg &= ~FLITE_REG_CISTATUS2_LASTCAPEND;

	writel(cfg, flite_reg_base + FLITE_REG_CISTATUS2);
}

static void s5pcsis_enable_interrupts(unsigned long mipi_reg_base, bool on)
{
	u32 val = readl(mipi_reg_base + S5PCSIS_INTMSK);

	val = on ? val | S5PCSIS_INTMSK_EN_ALL :
		   val & ~S5PCSIS_INTMSK_EN_ALL;
	writel(val, mipi_reg_base + S5PCSIS_INTMSK);
}

static void s5pcsis_reset(unsigned long mipi_reg_base)
{
	u32 val = readl(mipi_reg_base + S5PCSIS_CTRL);

	writel(val | S5PCSIS_CTRL_RESET, mipi_reg_base + S5PCSIS_CTRL);
	udelay(10);
}

static void s5pcsis_system_enable(unsigned long mipi_reg_base, int on)
{
	u32 val;

	val = readl(mipi_reg_base + S5PCSIS_CTRL);
	if (on) {
		val |= S5PCSIS_CTRL_ENABLE;
		val |= S5PCSIS_CTRL_WCLK_EXTCLK;
	} else
		val &= ~S5PCSIS_CTRL_ENABLE;
	writel(val, mipi_reg_base + S5PCSIS_CTRL);

	val = readl(mipi_reg_base + S5PCSIS_DPHYCTRL);
	if (on)
		val |= S5PCSIS_DPHYCTRL_ENABLE;
	else
		val &= ~S5PCSIS_DPHYCTRL_ENABLE;
	writel(val, mipi_reg_base + S5PCSIS_DPHYCTRL);
}

/* Called with the state.lock mutex held */
static void __s5pcsis_set_format(unsigned long mipi_reg_base,
				struct flite_frame *f_frame)
{
	u32 val;

	/* Color format */
	val = readl(mipi_reg_base + S5PCSIS_CONFIG);
	val = (val & ~S5PCSIS_CFG_FMT_MASK) | S5PCSIS_CFG_FMT_RAW10;
	writel(val, mipi_reg_base + S5PCSIS_CONFIG);

	/* Pixel resolution */
	val = (f_frame->o_width << 16) | f_frame->o_height;
	writel(val, mipi_reg_base + S5PCSIS_RESOL);
}

static void s5pcsis_set_hsync_settle(unsigned long mipi_reg_base)
{
	u32 val = readl(mipi_reg_base + S5PCSIS_DPHYCTRL);

	val = (val & ~S5PCSIS_DPHYCTRL_HSS_MASK) | (0x6 << 28);
	writel(val, mipi_reg_base + S5PCSIS_DPHYCTRL);
}

static void s5pcsis_set_params(unsigned long mipi_reg_base,
				struct flite_frame *f_frame)
{
	u32 val;

	val = readl(mipi_reg_base + S5PCSIS_CONFIG);
	val = (val & ~S5PCSIS_CFG_NR_LANE_MASK) | (2 - 1);
	writel(val, mipi_reg_base + S5PCSIS_CONFIG);

	__s5pcsis_set_format(mipi_reg_base, f_frame);
	s5pcsis_set_hsync_settle(mipi_reg_base);

	val = readl(mipi_reg_base + S5PCSIS_CTRL);
	val &= ~S5PCSIS_CTRL_ALIGN_32BIT;

	/* Not using external clock. */
	val &= ~S5PCSIS_CTRL_WCLK_EXTCLK;

	writel(val, mipi_reg_base + S5PCSIS_CTRL);

	/* Update the shadow register. */
	val = readl(mipi_reg_base + S5PCSIS_CTRL);
	writel(val | S5PCSIS_CTRL_UPDATE_SHADOW, mipi_reg_base + S5PCSIS_CTRL);
}

int start_fimc_lite(int channel, struct flite_frame *f_frame)
{
	unsigned long base_reg = (unsigned long)FIMCLITE0_REG_BASE;

	if (channel == FLITE_ID_A)
		base_reg = (unsigned long)FIMCLITE0_REG_BASE;
	else if (channel == FLITE_ID_B)
		base_reg = (unsigned long)FIMCLITE1_REG_BASE;

	flite_hw_set_cam_channel(base_reg);
	flite_hw_set_cam_source_size(base_reg, f_frame);
	flite_hw_set_camera_type(base_reg);
	flite_hw_set_source_format(base_reg);
	flite_hw_set_output_dma(base_reg, false);

	flite_hw_set_interrupt_source(base_reg);
	flite_hw_set_interrupt_starten0_disable(base_reg);
	flite_hw_set_config_irq(base_reg);
	flite_hw_set_window_offset(base_reg, f_frame);
	/* flite_hw_set_test_pattern_enable(); */

	flite_hw_set_last_capture_end_clear(base_reg);
	flite_hw_set_capture_start(base_reg);

	return 0;
}

int stop_fimc_lite(int channel)
{
	unsigned long base_reg = (unsigned long)FIMCLITE0_REG_BASE;

	if (channel == FLITE_ID_A)
		base_reg = (unsigned long)FIMCLITE0_REG_BASE;
	else if (channel == FLITE_ID_B)
		base_reg = (unsigned long)FIMCLITE1_REG_BASE;

	flite_hw_set_capture_stop(base_reg);
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
	} else if (!(cfg & (S5P_MIPI_DPHY_SRESETN | S5P_MIPI_DPHY_MRESETN)
			& (~S5P_MIPI_DPHY_SRESETN))) {
		cfg &= ~S5P_MIPI_DPHY_ENABLE;
	}

	__raw_writel(cfg, addr);


	addr = S5P_MIPI_DPHY_CONTROL(1);

	cfg = __raw_readl(addr);
	cfg = (cfg | S5P_MIPI_DPHY_SRESETN);
	__raw_writel(cfg, addr);

	if (1) {
		cfg |= S5P_MIPI_DPHY_ENABLE;
	} else if (!(cfg & (S5P_MIPI_DPHY_SRESETN | S5P_MIPI_DPHY_MRESETN)
			& (~S5P_MIPI_DPHY_SRESETN))) {
		cfg &= ~S5P_MIPI_DPHY_ENABLE;
	}

	__raw_writel(cfg, addr);
	return 0;

}

int start_mipi_csi(int channel, struct flite_frame *f_frame)
{
	unsigned long base_reg = (unsigned long)MIPICSI0_REG_BASE;

	if (channel == CSI_ID_A)
		base_reg = (unsigned long)MIPICSI0_REG_BASE;
	else if (channel == CSI_ID_B)
		base_reg = (unsigned long)MIPICSI1_REG_BASE;

	s5pcsis_reset(base_reg);
	s5pcsis_set_params(base_reg, f_frame);
	s5pcsis_system_enable(base_reg, true);
	s5pcsis_enable_interrupts(base_reg, true);

	return 0;
}

int stop_mipi_csi(int channel)
{
	unsigned long base_reg = (unsigned long)MIPICSI0_REG_BASE;

	if (channel == CSI_ID_A)
		base_reg = (unsigned long)MIPICSI0_REG_BASE;
	else if (channel == CSI_ID_B)
		base_reg = (unsigned long)MIPICSI1_REG_BASE;

	s5pcsis_enable_interrupts(base_reg, false);
	s5pcsis_system_enable(base_reg, false);

	return 0;
}

/*
* will be move to setting file
*/

int fimc_is_ctrl_odc(struct fimc_is_dev *dev, int value)
{
	int ret;

	if (value == CAMERA_ODC_ON) {
		/* buffer addr setting */
		dev->back.odc_on = 1;
		dev->is_p_region->shared[250] = (u32)dev->mem.dvaddr_odc;

		IS_ODC_SET_PARAM_CONTROL_BUFFERNUM(dev,
			SIZE_ODC_INTERNAL_BUF * NUM_ODC_INTERNAL_BUF);
		IS_ODC_SET_PARAM_CONTROL_BUFFERADDR(dev,
			(u32)dev->mem.dvaddr_shared + 250 * sizeof(u32));
		IS_ODC_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_DISABLE);

	} else if (value == CAMERA_ODC_OFF) {
		dev->back.odc_on = 0;
		IS_ODC_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_ENABLE);
	} else {
		err("invalid ODC setting\n");
		return -1;
	}

	if (test_bit(FIMC_IS_STATE_HW_STREAM_ON, &dev->pipe_state)) {
		clear_bit(IS_ST_STREAM_OFF, &dev->state);
		fimc_is_hw_set_stream(dev, 0); /*stream off */
		ret = wait_event_timeout(dev->irq_queue,
			test_bit(IS_ST_STREAM_OFF, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		if (!ret) {
			dev_err(&dev->pdev->dev,
				"wait timeout : %s\n", __func__);
			if (!ret)
				err("s_power off failed!!\n");
			return -EBUSY;
		}
	}

	IS_SET_PARAM_BIT(dev, PARAM_ODC_CONTROL);
	IS_INC_PARAM_NUM(dev);

	fimc_is_mem_cache_clean((void *)dev->is_p_region,
		IS_PARAM_SIZE);

	dev->scenario_id = ISS_PREVIEW_STILL;
	set_bit(IS_ST_INIT_PREVIEW_STILL,	&dev->state);
	clear_bit(IS_ST_INIT_CAPTURE_STILL, &dev->state);
	clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &dev->state);
	fimc_is_hw_set_param(dev);
	ret = wait_event_timeout(dev->irq_queue,
		test_bit(IS_ST_INIT_PREVIEW_VIDEO, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev,
			"wait timeout : %s\n", __func__);
		return -EBUSY;
	}

	if (test_bit(FIMC_IS_STATE_HW_STREAM_ON, &dev->pipe_state)) {
		clear_bit(IS_ST_RUN, &dev->state);
		set_bit(IS_ST_CHANGE_MODE, &dev->state);
		fimc_is_hw_change_mode(dev, IS_MODE_PREVIEW_STILL);
		ret = wait_event_timeout(dev->irq_queue,
				test_bit(IS_ST_CHANGE_MODE_DONE, &dev->state),
				(3*HZ)/*FIMC_IS_SHUTDOWN_TIMEOUT*/);

		if (!ret) {
			dev_err(&dev->pdev->dev,
				"Mode change timeout:%s\n", __func__);
			return -EBUSY;
		}

		clear_bit(IS_ST_STREAM_ON, &dev->state);
		fimc_is_hw_set_stream(dev, 1);

		ret = wait_event_timeout(dev->irq_queue,
		test_bit(IS_ST_STREAM_ON, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
		if (!ret) {
			dev_err(&dev->pdev->dev,
				"wait timeout : %s\n", __func__);
			return -EBUSY;
		}
	}
	clear_bit(IS_ST_STREAM_ON, &dev->state);

	return 0;
}

int fimc_is_ctrl_dis(struct fimc_is_dev *dev, int value)
{
	int ret;

	if (value == CAMERA_DIS_ON) {
		/* buffer addr setting */
		dev->back.dis_on = 1;
		dev->is_p_region->shared[300] = (u32)dev->mem.dvaddr_dis;

		IS_DIS_SET_PARAM_CONTROL_BUFFERNUM(dev,
			SIZE_DIS_INTERNAL_BUF * NUM_DIS_INTERNAL_BUF);
		IS_DIS_SET_PARAM_CONTROL_BUFFERADDR(dev,
			(u32)dev->mem.dvaddr_shared + 300 * sizeof(u32));
		IS_DIS_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_DISABLE);
	} else if (value == CAMERA_DIS_OFF) {
		dev->back.dis_on = 0;
		IS_DIS_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_ENABLE);
	} else {
		err("invalid DIS setting\n");
		return -1;
	}

	if (test_bit(FIMC_IS_STATE_HW_STREAM_ON, &dev->pipe_state)) {
		clear_bit(IS_ST_STREAM_OFF, &dev->state);
		fimc_is_hw_set_stream(dev, 0); /*stream off */
		ret = wait_event_timeout(dev->irq_queue,
			test_bit(IS_ST_STREAM_OFF, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT);
		if (!ret) {
			dev_err(&dev->pdev->dev,
				"wait timeout : %s\n", __func__);
			if (!ret)
				err("s_power off failed!!\n");
			return -EBUSY;
		}
	}

	IS_SET_PARAM_BIT(dev, PARAM_DIS_CONTROL);
	IS_INC_PARAM_NUM(dev);

	fimc_is_hw_change_size(dev);

	fimc_is_mem_cache_clean((void *)dev->is_p_region,
		IS_PARAM_SIZE);

	dev->scenario_id = ISS_PREVIEW_STILL;
	set_bit(IS_ST_INIT_PREVIEW_STILL,	&dev->state);
	clear_bit(IS_ST_INIT_CAPTURE_STILL, &dev->state);
	clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &dev->state);
	fimc_is_hw_set_param(dev);
#ifdef DIS_ENABLE
	/* FW bug - should be wait */
	ret = wait_event_timeout(dev->irq_queue,
		test_bit(IS_ST_INIT_PREVIEW_VIDEO, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev,
			"set param wait timeout : %s\n", __func__);
		return -EBUSY;
	}
#endif

	if (test_bit(FIMC_IS_STATE_HW_STREAM_ON, &dev->pipe_state)) {
		clear_bit(IS_ST_RUN, &dev->state);
		set_bit(IS_ST_CHANGE_MODE, &dev->state);
		fimc_is_hw_change_mode(dev, IS_MODE_PREVIEW_STILL);
		ret = wait_event_timeout(dev->irq_queue,
				test_bit(IS_ST_CHANGE_MODE_DONE, &dev->state),
				(3*HZ)/*FIMC_IS_SHUTDOWN_TIMEOUT*/);

		if (!ret) {
			dev_err(&dev->pdev->dev,
				"Mode change timeout:%s\n", __func__);
			return -EBUSY;
		}

		clear_bit(IS_ST_STREAM_ON, &dev->state);
		fimc_is_hw_set_stream(dev, 1);

		ret = wait_event_timeout(dev->irq_queue,
		test_bit(IS_ST_STREAM_ON, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
		if (!ret) {
			dev_err(&dev->pdev->dev,
				"stream on wait timeout : %s\n", __func__);
			return -EBUSY;
		}
		clear_bit(IS_ST_STREAM_ON, &dev->state);
	}

	return 0;
}

int fimc_is_ctrl_3dnr(struct fimc_is_dev *dev, int value)
{
	int ret;

	if (value == CAMERA_3DNR_ON) {
		/* buffer addr setting */
		dev->back.tdnr_on = 1;
		dev->is_p_region->shared[350] = (u32)dev->mem.dvaddr_3dnr;
		dbg("3dnr buf:0x%08x size : 0x%08x\n",
			dev->is_p_region->shared[350],
			SIZE_3DNR_INTERNAL_BUF*NUM_3DNR_INTERNAL_BUF);

		IS_TDNR_SET_PARAM_CONTROL_BUFFERNUM(dev,
			SIZE_3DNR_INTERNAL_BUF * NUM_3DNR_INTERNAL_BUF);
		IS_TDNR_SET_PARAM_CONTROL_BUFFERADDR(dev,
			(u32)dev->mem.dvaddr_shared + 350 * sizeof(u32));
		IS_TDNR_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_DISABLE);

	} else if (value == CAMERA_3DNR_OFF) {
		dbg("disable 3DNR\n");
		dev->back.tdnr_on = 0;
		IS_TDNR_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_ENABLE);
	} else {
		err("invalid ODC setting\n");
		return -1;
	}

	dbg("IS_ST_STREAM_OFF\n");
	clear_bit(IS_ST_STREAM_OFF, &dev->state);
	fimc_is_hw_set_stream(dev, 0); /*stream off */
	ret = wait_event_timeout(dev->irq_queue,
		test_bit(IS_ST_STREAM_OFF, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev,
			"wait timeout : %s\n", __func__);
		if (!ret)
			err("s_power off failed!!\n");
		return -EBUSY;
	}

	IS_SET_PARAM_BIT(dev, PARAM_TDNR_CONTROL);
	IS_INC_PARAM_NUM(dev);

	fimc_is_mem_cache_clean((void *)dev->is_p_region,
		IS_PARAM_SIZE);

	dev->scenario_id = ISS_PREVIEW_STILL;
	set_bit(IS_ST_INIT_PREVIEW_STILL,	&dev->state);
	clear_bit(IS_ST_INIT_CAPTURE_STILL, &dev->state);
	clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &dev->state);
	fimc_is_hw_set_param(dev);
	ret = wait_event_timeout(dev->irq_queue,
		test_bit(IS_ST_INIT_PREVIEW_VIDEO, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev,
			"wait timeout : %s\n", __func__);
		return -EBUSY;
	}

	dbg("IS change mode\n");
	clear_bit(IS_ST_RUN, &dev->state);
	set_bit(IS_ST_CHANGE_MODE, &dev->state);
	fimc_is_hw_change_mode(dev, IS_MODE_PREVIEW_STILL);
	ret = wait_event_timeout(dev->irq_queue,
			test_bit(IS_ST_CHANGE_MODE_DONE, &dev->state),
			(3*HZ)/*FIMC_IS_SHUTDOWN_TIMEOUT*/);

	if (!ret) {
		dev_err(&dev->pdev->dev,
			"Mode change timeout:%s\n", __func__);
		return -EBUSY;
	}

	dbg("IS_ST_STREAM_ON\n");
	clear_bit(IS_ST_STREAM_ON, &dev->state);
	fimc_is_hw_set_stream(dev, 1);

	ret = wait_event_timeout(dev->irq_queue,
	test_bit(IS_ST_STREAM_ON, &dev->state),
	FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev,
			"wait timeout : %s\n", __func__);
		return -EBUSY;
	}
	clear_bit(IS_ST_STREAM_ON, &dev->state);

	return 0;
}

int fimc_is_digital_zoom(struct fimc_is_dev *dev, int value)
{
	u32 back_width, back_height;
	u32 crop_width, crop_height, crop_x, crop_y;
	u32 zoom;
	int ret;

	if (dev->back.dis_on) {
		back_width = dev->back.dis_width;
		back_height = dev->back.dis_height;
	} else {
		back_width = dev->back.width;
		back_height = dev->back.height;
	}

	zoom = value+10;

	crop_width = back_width*10/zoom;
	crop_height = back_height*10/zoom;

	crop_width &= 0xffe;
	crop_height &= 0xffe;

	crop_x = (back_width - crop_width)/2;
	crop_y = (back_height - crop_height)/2;

	crop_x &= 0xffe;
	crop_y &= 0xffe;

	dbg("crop_width : %d crop_height: %d\n", crop_width, crop_height);
	dbg("crop_x:%d crop_y: %d\n", crop_x, crop_y);

	IS_SCALERC_SET_PARAM_INPUT_CROP_WIDTH(dev,
		crop_width);
	IS_SCALERC_SET_PARAM_INPUT_CROP_HEIGHT(dev,
		crop_height);
	IS_SCALERC_SET_PARAM_INPUT_CROP_POS_X(dev,
		crop_x);
	IS_SCALERC_SET_PARAM_INPUT_CROP_POS_Y(dev,
		crop_y);
	IS_SET_PARAM_BIT(dev, PARAM_SCALERC_INPUT_CROP);
	IS_INC_PARAM_NUM(dev);

	fimc_is_mem_cache_clean((void *)dev->is_p_region,
		IS_PARAM_SIZE);

	dev->scenario_id = ISS_PREVIEW_STILL;
	set_bit(IS_ST_INIT_PREVIEW_STILL,	&dev->state);
	clear_bit(IS_ST_INIT_CAPTURE_STILL, &dev->state);
	clear_bit(IS_ST_INIT_PREVIEW_VIDEO, &dev->state);
	fimc_is_hw_set_param(dev);
	ret = wait_event_timeout(dev->irq_queue,
		test_bit(IS_ST_INIT_PREVIEW_VIDEO, &dev->state),
		FIMC_IS_SHUTDOWN_TIMEOUT);
	if (!ret) {
		dev_err(&dev->pdev->dev,
			"wait timeout : %s\n", __func__);
		return -EBUSY;
	}
	return 0;
}


int fimc_is_v4l2_isp_scene_mode(struct fimc_is_dev *dev, int mode)
{
	int ret = 0;
	switch (mode) {
	case SCENE_MODE_NONE:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
				ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_AUTO);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_ENABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_PORTRAIT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, -1);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, -1);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_AUTO);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_ENABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_LANDSCAPE:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_MATRIX);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_SPORTS:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 400);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_PARTY_INDOOR:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 200);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_AUTO);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_ENABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_BEACH_SNOW:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 50);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 1);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_SUNSET:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev, ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev,
				ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_DUSK_DAWN:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev,
				ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_FLUORESCENT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_FALL_COLOR:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 2);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_NIGHTSHOT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_BACK_LIGHT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	/* FIXME add with SCENE_MODE_BACK_LIGHT (FLASH mode) */
	case SCENE_MODE_FIREWORKS:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 50);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
				ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_TEXT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
			ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 2);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 2);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
		IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case SCENE_MODE_CANDLE_LIGHT:
		/* ISO */
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		IS_ISP_SET_PARAM_ISO_ERR(dev, ISP_ISO_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		/* Metering */
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		IS_ISP_SET_PARAM_METERING_WIN_POS_X(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_POS_Y(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_WIDTH(dev, 0);
		IS_ISP_SET_PARAM_METERING_WIN_HEIGHT(dev, 0);
		IS_ISP_SET_PARAM_METERING_ERR(dev,
				ISP_METERING_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		/* AWB */
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_DAYLIGHT);
		IS_ISP_SET_PARAM_AWB_ERR(dev, ISP_AWB_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		/* Adjust */
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_MANUAL_ALL);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		IS_ISP_SET_PARAM_ADJUST_ERR(dev, ISP_ADJUST_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		/* Flash */
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		IS_ISP_SET_PARAM_FLASH_ERR(dev, ISP_FLASH_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		/* AF */
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
		IS_ISP_SET_PARAM_AA_SCENE(dev, ISP_AF_SCENE_NORMAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_ISP_SET_PARAM_AA_ERR(dev, ISP_AF_ERROR_NO);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	default:
		break;
	}
	return ret;
}

int fimc_is_af_face(struct fimc_is_dev *dev)
{
	int ret = 0, max_confidence = 0, i = 0;
	int width, height;
	u32 touch_x = 0, touch_y = 0;

	for (i = dev->fd_header.index;
		i < (dev->fd_header.index + dev->fd_header.count); i++) {
		if (max_confidence < dev->is_p_region->face[i].confidence) {
			max_confidence = dev->is_p_region->face[i].confidence;
			touch_x = dev->is_p_region->face[i].face.offset_x +
				(dev->is_p_region->face[i].face.width / 2);
			touch_y = dev->is_p_region->face[i].face.offset_y +
				(dev->is_p_region->face[i].face.height / 2);
		}
	}
	width = dev->sensor.width;
	height = dev->sensor.height;
	touch_x = 1024 *  touch_x / (u32)width;
	touch_y = 1024 *  touch_y / (u32)height;

	if ((touch_x == 0) || (touch_y == 0) || (max_confidence < 50))
		return ret;

	if (dev->af.prev_pos_x == 0 && dev->af.prev_pos_y == 0) {
		dev->af.prev_pos_x = touch_x;
		dev->af.prev_pos_y = touch_y;
	} else {
		if (abs(dev->af.prev_pos_x - touch_x) < 100 &&
			abs(dev->af.prev_pos_y - touch_y) < 100) {
			return ret;
		}
		dbg("AF Face level = %d\n", max_confidence);
		dbg("AF Face = <%d, %d>\n", touch_x, touch_y);
		dbg("AF Face = prev <%d, %d>\n",
				dev->af.prev_pos_x, dev->af.prev_pos_y);
		dev->af.prev_pos_x = touch_x;
		dev->af.prev_pos_y = touch_y;
	}

	IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
	IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
	IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_TOUCH);
	IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
	IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
	IS_ISP_SET_PARAM_AA_TOUCH_X(dev, touch_x);
	IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, touch_y);
	IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
	IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
	IS_INC_PARAM_NUM(dev);
	dev->af.af_state = FIMC_IS_AF_SETCONFIG;
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	fimc_is_hw_set_param(dev);

	return ret;
}

int fimc_is_v4l2_af_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case FOCUS_MODE_AUTO:
		dev->af.mode = IS_FOCUS_MODE_AUTO;
		break;
	case FOCUS_MODE_MACRO:
		dev->af.mode = IS_FOCUS_MODE_MACRO;
		break;
	case FOCUS_MODE_INFINITY:
		dev->af.mode = IS_FOCUS_MODE_INFINITY;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_MANUAL);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case FOCUS_MODE_CONTINOUS:
		dev->af.mode = IS_FOCUS_MODE_CONTINUOUS;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_CONTINUOUS);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		dev->af.af_lock_state = 0;
		dev->af.ae_lock_state = 0;
		dev->af.awb_lock_state = 0;
		dev->af.prev_pos_x = 0;
		dev->af.prev_pos_y = 0;
		break;
	case FOCUS_MODE_TOUCH:
		dev->af.mode = IS_FOCUS_MODE_TOUCH;
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
		IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_TOUCH);
		IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
		IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
		IS_ISP_SET_PARAM_AA_TOUCH_X(dev, dev->af.pos_x);
		IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, dev->af.pos_y);
		IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		dev->af.af_state = FIMC_IS_AF_SETCONFIG;
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		dev->af.af_lock_state = 0;
		dev->af.ae_lock_state = 0;
		dev->af.awb_lock_state = 0;
		break;
	case FOCUS_MODE_FACEDETECT:
		dev->af.mode = IS_FOCUS_MODE_FACEDETECT;
		dev->af.af_lock_state = 0;
		dev->af.ae_lock_state = 0;
		dev->af.awb_lock_state = 0;
		dev->af.prev_pos_x = 0;
		dev->af.prev_pos_y = 0;
		break;
	default:
		return ret;
	}
	return ret;
}

int fimc_is_v4l2_af_start_stop(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case AUTO_FOCUS_OFF:
		if (!is_af_use(dev)) {
			/* 6A3 can't support AF */
			dev->af.af_state = FIMC_IS_AF_IDLE;
		} else {
			if (dev->af.af_state == FIMC_IS_AF_IDLE)
				return ret;
			/* Abort or lock AF */
			dev->af.af_state = FIMC_IS_AF_ABORT;
			IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
			IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
			switch (dev->af.mode) {
			case IS_FOCUS_MODE_AUTO:
				IS_ISP_SET_PARAM_AA_MODE(dev,
					ISP_AF_MODE_SINGLE);
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_NORMAL);
				IS_ISP_SET_PARAM_AA_SLEEP(dev,
					ISP_AF_SLEEP_OFF);
				IS_ISP_SET_PARAM_AA_FACE(dev,
					ISP_AF_FACE_DISABLE);
				IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
				IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
				IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
					break;
			case IS_FOCUS_MODE_MACRO:
				IS_ISP_SET_PARAM_AA_MODE(dev,
					ISP_AF_MODE_SINGLE);
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_MACRO);
				IS_ISP_SET_PARAM_AA_SLEEP(dev,
					ISP_AF_SLEEP_OFF);
				IS_ISP_SET_PARAM_AA_FACE(dev,
					ISP_AF_FACE_DISABLE);
				IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
				IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
				IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
				ret = wait_event_timeout(dev->irq_queue,
				(dev->af.af_state == FIMC_IS_AF_IDLE), HZ/5);
				if (!ret) {
					dev_err(&dev->pdev->dev,
					"Focus change timeout:%s\n", __func__);
					return -EBUSY;
				}
				break;
			case IS_FOCUS_MODE_CONTINUOUS:
				IS_ISP_SET_PARAM_AA_MODE(dev,
					ISP_AF_MODE_CONTINUOUS);
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_NORMAL);
				IS_ISP_SET_PARAM_AA_SLEEP(dev,
						ISP_AF_SLEEP_OFF);
				IS_ISP_SET_PARAM_AA_FACE(dev,
					ISP_AF_FACE_DISABLE);
				IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
				IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
				IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
				ret = wait_event_timeout(dev->irq_queue,
				(dev->af.af_state == FIMC_IS_AF_IDLE), HZ/5);
				if (!ret) {
					dev_err(&dev->pdev->dev,
					"Focus change timeout:%s\n", __func__);
					return -EBUSY;
				}
				break;
			default:
				/* If other AF mode, there is no
				cancelation process*/
				break;
			}
			dev->af.mode = IS_FOCUS_MODE_IDLE;
		}
		break;
	case AUTO_FOCUS_ON:
		if (!is_af_use(dev)) {
			/* 6A3 can't support AF */
			dev->af.af_state = FIMC_IS_AF_LOCK;
			dev->af.af_lock_state = FIMC_IS_AF_LOCKED;
			dev->is_shared_region->af_status = 1;
			fimc_is_mem_cache_clean((void *)IS_SHARED(dev),
			(unsigned long)(sizeof(struct is_share_region)));
		} else {
			dev->af.af_lock_state = 0;
			dev->af.ae_lock_state = 0;
			dev->af.awb_lock_state = 0;
			dev->is_shared_region->af_status = 0;
			fimc_is_mem_cache_clean((void *)IS_SHARED(dev),
			(unsigned long)(sizeof(struct is_share_region)));
			IS_ISP_SET_PARAM_AA_CMD(dev,
			ISP_AA_COMMAND_START);
			IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
			IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_SINGLE);
			IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
			IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
			IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
			IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
			IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
			switch (dev->af.mode) {
			case IS_FOCUS_MODE_AUTO:
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_NORMAL);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				dev->af.af_state =
						FIMC_IS_AF_SETCONFIG;
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
				ret = wait_event_timeout(dev->irq_queue,
				(dev->af.af_state == FIMC_IS_AF_RUNNING), HZ/5);
				if (!ret) {
					dev_err(&dev->pdev->dev,
					"Focus change timeout:%s\n", __func__);
					return -EBUSY;
				}
				break;
			case IS_FOCUS_MODE_MACRO:
				IS_ISP_SET_PARAM_AA_SCENE(dev,
					ISP_AF_SCENE_MACRO);
				IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
				IS_INC_PARAM_NUM(dev);
				dev->af.af_state =
						FIMC_IS_AF_SETCONFIG;
				fimc_is_mem_cache_clean(
					(void *)dev->is_p_region,
					IS_PARAM_SIZE);
				fimc_is_hw_set_param(dev);
				ret = wait_event_timeout(dev->irq_queue,
				(dev->af.af_state == FIMC_IS_AF_RUNNING), HZ/5);
				if (!ret) {
					dev_err(&dev->pdev->dev,
					"Focus change timeout:%s\n", __func__);
					return -EBUSY;
				}
				break;
			default:
				break;
			}
		}
		break;
	default:
		break;
	}
	return ret;
}

int fimc_is_v4l2_touch_af_start_stop(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case TOUCH_AF_STOP:
		if (!is_af_use(dev)) {
			/* 6A3 can't support AF */
			dev->af.af_state = FIMC_IS_AF_IDLE;
		} else {
			if (dev->af.af_state == FIMC_IS_AF_IDLE)
				return ret;
			/* Abort or lock CAF */
			dev->af.af_state = FIMC_IS_AF_ABORT;
			IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
			IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);

			IS_ISP_SET_PARAM_AA_MODE(dev,
				ISP_AF_MODE_TOUCH);
			IS_ISP_SET_PARAM_AA_SCENE(dev,
				ISP_AF_SCENE_NORMAL);
			IS_ISP_SET_PARAM_AA_SLEEP(dev,
					ISP_AF_SLEEP_OFF);
			IS_ISP_SET_PARAM_AA_FACE(dev,
				ISP_AF_FACE_DISABLE);
			IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
			IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
			IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean(
				(void *)dev->is_p_region,
				IS_PARAM_SIZE);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue,
			(dev->af.af_state == FIMC_IS_AF_IDLE), HZ/5);
			if (!ret) {
				dev_err(&dev->pdev->dev,
				"Focus change timeout:%s\n", __func__);
				return -EBUSY;
			}
		}
		break;
	case TOUCH_AF_START:
		if (!is_af_use(dev)) {
			/* 6A3 can't support AF */
			dev->af.af_state = FIMC_IS_AF_LOCK;
			dev->af.af_lock_state = FIMC_IS_AF_LOCKED;
			dev->is_shared_region->af_status = 1;
			fimc_is_mem_cache_clean((void *)IS_SHARED(dev),
			(unsigned long)(sizeof(struct is_share_region)));
		} else {
			dev->af.mode = IS_FOCUS_MODE_TOUCH;
			IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
			IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
			IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_TOUCH);
			IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
			IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
			IS_ISP_SET_PARAM_AA_TOUCH_X(dev, dev->af.pos_x);
			IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, dev->af.pos_y);
			IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
			IS_INC_PARAM_NUM(dev);
			dev->af.af_state = FIMC_IS_AF_SETCONFIG;
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
				IS_PARAM_SIZE);
			fimc_is_hw_set_param(dev);
			dev->af.af_lock_state = 0;
			dev->af.ae_lock_state = 0;
			dev->af.awb_lock_state = 0;
		}
		break;
	default:
		break;
	}
	return ret;
}

int fimc_is_v4l2_caf_start_stop(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case CAF_STOP:
		if (!is_af_use(dev)) {
			/* 6A3 can't support AF */
			dev->af.af_state = FIMC_IS_AF_IDLE;
		} else {
			if (dev->af.af_state == FIMC_IS_AF_IDLE)
				return ret;
			/* Abort or lock CAF */
			dev->af.af_state = FIMC_IS_AF_ABORT;
			IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
			IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);

			IS_ISP_SET_PARAM_AA_MODE(dev,
				ISP_AF_MODE_CONTINUOUS);
			IS_ISP_SET_PARAM_AA_SCENE(dev,
				ISP_AF_SCENE_NORMAL);
			IS_ISP_SET_PARAM_AA_SLEEP(dev,
					ISP_AF_SLEEP_OFF);
			IS_ISP_SET_PARAM_AA_FACE(dev,
				ISP_AF_FACE_DISABLE);
			IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
			IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
			IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean(
				(void *)dev->is_p_region,
				IS_PARAM_SIZE);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue,
			(dev->af.af_state == FIMC_IS_AF_IDLE), HZ/5);
			if (!ret) {
				dev_err(&dev->pdev->dev,
				"Focus change timeout:%s\n", __func__);
				return -EBUSY;
			}
		}
		break;
	case CAF_START:
		if (!is_af_use(dev)) {
			/* 6A3 can't support AF */
			dev->af.af_state = FIMC_IS_AF_LOCK;
			dev->af.af_lock_state = FIMC_IS_AF_LOCKED;
			dev->is_shared_region->af_status = 1;
			fimc_is_mem_cache_clean((void *)IS_SHARED(dev),
			(unsigned long)(sizeof(struct is_share_region)));
		} else {
			dev->af.mode = IS_FOCUS_MODE_CONTINUOUS;
			IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
			IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AF);
			IS_ISP_SET_PARAM_AA_MODE(dev, ISP_AF_MODE_CONTINUOUS);
			IS_ISP_SET_PARAM_AA_SLEEP(dev, ISP_AF_SLEEP_OFF);
			IS_ISP_SET_PARAM_AA_FACE(dev, ISP_AF_FACE_DISABLE);
			IS_ISP_SET_PARAM_AA_TOUCH_X(dev, 0);
			IS_ISP_SET_PARAM_AA_TOUCH_Y(dev, 0);
			IS_ISP_SET_PARAM_AA_MANUAL_AF(dev, 0);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
			IS_INC_PARAM_NUM(dev);
			dev->af.af_state = FIMC_IS_AF_SETCONFIG;
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
				IS_PARAM_SIZE);
			fimc_is_hw_set_param(dev);
			dev->af.af_lock_state = 0;
			dev->af.ae_lock_state = 0;
			dev->af.awb_lock_state = 0;
			dev->af.prev_pos_x = 0;
			dev->af.prev_pos_y = 0;
		}
		break;
	default:
		break;
	}
	return ret;
}

int fimc_is_v4l2_isp_iso(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case ISO_AUTO:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_AUTO);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 0);
		break;
	case ISO_100:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 100);
		break;
	case ISO_200:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 200);
		break;
	case ISO_400:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 400);
		break;
	case ISO_800:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 800);
		break;
	case ISO_1600:
		IS_ISP_SET_PARAM_ISO_CMD(dev, ISP_ISO_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_ISO_VALUE(dev, 1600);
		break;
	default:
		return ret;
	}
	if (value >= ISO_AUTO && value < ISO_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ISO);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_effect(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_IMAGE_EFFECT_DISABLE:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_DISABLE);
		break;
	case IS_IMAGE_EFFECT_MONOCHROME:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_MONOCHROME);
		break;
	case IS_IMAGE_EFFECT_NEGATIVE_MONO:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			ISP_IMAGE_EFFECT_NEGATIVE_MONO);
		break;
	case IS_IMAGE_EFFECT_NEGATIVE_COLOR:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			ISP_IMAGE_EFFECT_NEGATIVE_COLOR);
		break;
	case IS_IMAGE_EFFECT_SEPIA:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_SEPIA);
		break;
	}
	/* only ISP effect in Pegasus */
	if (value >= 0 && value < 5) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_effect_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IMAGE_EFFECT_NONE:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_DISABLE);
		break;
	case IMAGE_EFFECT_BNW:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_MONOCHROME);
		break;
	case IMAGE_EFFECT_NEGATIVE:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev,
			ISP_IMAGE_EFFECT_NEGATIVE_COLOR);
		break;
	case IMAGE_EFFECT_SEPIA:
		IS_ISP_SET_PARAM_EFFECT_CMD(dev, ISP_IMAGE_EFFECT_SEPIA);
		break;
	default:
		return ret;
	}
	/* only ISP effect in Pegasus */
	if (value > IMAGE_EFFECT_BASE && value < IMAGE_EFFECT_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_IMAGE_EFFECT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_flash_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case FLASH_MODE_OFF:
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		break;
	case FLASH_MODE_AUTO:
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_AUTO);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_ENABLE);
		break;
	case FLASH_MODE_ON:
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_MANUALON);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		break;
	case FLASH_MODE_TORCH:
		IS_ISP_SET_PARAM_FLASH_CMD(dev, ISP_FLASH_COMMAND_TORCH);
		IS_ISP_SET_PARAM_FLASH_REDEYE(dev, ISP_FLASH_REDEYE_DISABLE);
		break;
	default:
		return ret;
	}
	if (value > FLASH_MODE_BASE && value < FLASH_MODE_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_FLASH);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_awb_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_AWB_AUTO:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev, 0);
		break;
	case IS_AWB_DAYLIGHT:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		break;
	case IS_AWB_CLOUDY:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_CLOUDY);
		break;
	case IS_AWB_TUNGSTEN:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_TUNGSTEN);
		break;
	case IS_AWB_FLUORESCENT:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_FLUORESCENT);
		break;
	}
	if (value >= IS_AWB_AUTO && value < IS_AWB_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_awb_mode_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case WHITE_BALANCE_AUTO:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev, 0);
		break;
	case WHITE_BALANCE_SUNNY:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_DAYLIGHT);
		break;
	case WHITE_BALANCE_CLOUDY:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
				ISP_AWB_ILLUMINATION_CLOUDY);
		break;
	case WHITE_BALANCE_TUNGSTEN:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_TUNGSTEN);
		break;
	case WHITE_BALANCE_FLUORESCENT:
		IS_ISP_SET_PARAM_AWB_CMD(dev, ISP_AWB_COMMAND_ILLUMINATION);
		IS_ISP_SET_PARAM_AWB_ILLUMINATION(dev,
			ISP_AWB_ILLUMINATION_FLUORESCENT);
		break;
	}
	if (value > WHITE_BALANCE_BASE && value < WHITE_BALANCE_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AWB);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_contrast(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_CONTRAST_AUTO:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev, ISP_ADJUST_COMMAND_AUTO);
		break;
	case IS_CONTRAST_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, -2);
		break;
	case IS_CONTRAST_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, -1);
		break;
	case IS_CONTRAST_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		break;
	case IS_CONTRAST_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 1);
		break;
	case IS_CONTRAST_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_CONTRAST_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_contrast_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case CONTRAST_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, -2);
		break;
	case CONTRAST_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, -1);
		break;
	case CONTRAST_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 0);
		break;
	case CONTRAST_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 1);
		break;
	case CONTRAST_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_CONTRAST);
		IS_ISP_SET_PARAM_ADJUST_CONTRAST(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < CONTRAST_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_saturation(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case SATURATION_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, -2);
		break;
	case SATURATION_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, -1);
		break;
	case SATURATION_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 0);
		break;
	case SATURATION_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 1);
		break;
	case SATURATION_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SATURATION);
		IS_ISP_SET_PARAM_ADJUST_SATURATION(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < SATURATION_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_sharpness(struct fimc_is_dev *dev, int value)
{
	int ret = 0;

	switch (value) {
	case SHARPNESS_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, -2);
		break;
	case SHARPNESS_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, -1);
		break;
	case SHARPNESS_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 0);
		break;
	case SHARPNESS_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 1);
		break;
	case SHARPNESS_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_SHARPNESS);
		IS_ISP_SET_PARAM_ADJUST_SHARPNESS(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < SHARPNESS_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_exposure(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_EXPOSURE_MINUS_4:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, -4);
		break;
	case IS_EXPOSURE_MINUS_3:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, -3);
		break;
	case IS_EXPOSURE_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, -2);
		break;
	case IS_EXPOSURE_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, -1);
		break;
	case IS_EXPOSURE_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 0);
		break;
	case IS_EXPOSURE_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 1);
		break;
	case IS_EXPOSURE_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 2);
		break;
	case IS_EXPOSURE_PLUS_3:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 3);
		break;
	case IS_EXPOSURE_PLUS_4:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, 4);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_EXPOSURE_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_exposure_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	if (value >= -4 && value < 5) {
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_EXPOSURE);
		IS_ISP_SET_PARAM_ADJUST_EXPOSURE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_brightness(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_BRIGHTNESS_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, -2);
		break;
	case IS_BRIGHTNESS_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, -1);
		break;
	case IS_BRIGHTNESS_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 0);
		break;
	case IS_BRIGHTNESS_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 1);
		break;
	case IS_BRIGHTNESS_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_BRIGHTNESS);
		IS_ISP_SET_PARAM_ADJUST_BRIGHTNESS(dev, 2);
		break;
	}
	if (value >= 0 && value < IS_BRIGHTNESS_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_hue(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_HUE_MINUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, -2);
		break;
	case IS_HUE_MINUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, -1);
		break;
	case IS_HUE_DEFAULT:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 0);
		break;
	case IS_HUE_PLUS_1:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 1);
		break;
	case IS_HUE_PLUS_2:
		IS_ISP_SET_PARAM_ADJUST_CMD(dev,
					ISP_ADJUST_COMMAND_MANUAL_HUE);
		IS_ISP_SET_PARAM_ADJUST_HUE(dev, 2);
		break;
	default:
		return ret;
	}
	if (value >= IS_HUE_MINUS_2 && value < IS_HUE_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_ADJUST);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_metering(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_METERING_AVERAGE:
		IS_ISP_SET_PARAM_METERING_CMD(dev,
			ISP_METERING_COMMAND_AVERAGE);
		break;
	case IS_METERING_SPOT:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_SPOT);
		break;
	case IS_METERING_MATRIX:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_MATRIX);
		break;
	case IS_METERING_CENTER:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_CENTER);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_METERING_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_metering_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case METERING_CENTER:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_CENTER);
		break;
	case METERING_SPOT:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_SPOT);
		break;
	case METERING_MATRIX:
		IS_ISP_SET_PARAM_METERING_CMD(dev, ISP_METERING_COMMAND_MATRIX);
		break;
	default:
		return ret;
	}
	if (value > METERING_BASE && value < METERING_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_METERING);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_afc(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_AFC_DISABLE:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, 0);
		break;
	case IS_AFC_AUTO:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, 0);
		break;
	case IS_AFC_MANUAL_50HZ:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, ISP_AFC_MANUAL_50HZ);
		break;
	case IS_AFC_MANUAL_60HZ:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, ISP_AFC_MANUAL_60HZ);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_AFC_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AFC);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_isp_afc_legacy(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case ANTI_BANDING_OFF:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_DISABLE);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, 0);
		break;
	case ANTI_BANDING_AUTO:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_AUTO);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, 0);
		break;
	case ANTI_BANDING_50HZ:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, ISP_AFC_MANUAL_50HZ);
		break;
	case ANTI_BANDING_60HZ:
		IS_ISP_SET_PARAM_AFC_CMD(dev, ISP_AFC_COMMAND_MANUAL);
		IS_ISP_SET_PARAM_AFC_MANUAL(dev, ISP_AFC_MANUAL_60HZ);
		break;
	default:
		return ret;
	}
	if (value >= ANTI_BANDING_OFF && value <= ANTI_BANDING_60HZ) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AFC);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_fd_angle_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_FD_ROLL_ANGLE_BASIC:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_ROLL_ANGLE);
		IS_FD_SET_PARAM_FD_CONFIG_ROLL_ANGLE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_YAW_ANGLE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_YAW_ANGLE);
		IS_FD_SET_PARAM_FD_CONFIG_YAW_ANGLE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_SMILE_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_SMILE_MODE);
		IS_FD_SET_PARAM_FD_CONFIG_SMILE_MODE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_BLINK_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_BLINK_MODE);
		IS_FD_SET_PARAM_FD_CONFIG_BLINK_MODE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_EYE_DETECT_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_EYES_DETECT);
		IS_FD_SET_PARAM_FD_CONFIG_EYE_DETECT(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_MOUTH_DETECT_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_MOUTH_DETECT);
		IS_FD_SET_PARAM_FD_CONFIG_MOUTH_DETECT(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_ORIENTATION_MODE:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_ORIENTATION);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case V4L2_CID_IS_FD_SET_ORIENTATION:
		IS_FD_SET_PARAM_FD_CONFIG_CMD(dev,
			FD_CONFIG_COMMAND_ORIENTATION_VALUE);
		IS_FD_SET_PARAM_FD_CONFIG_ORIENTATION_VALUE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONFIG);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	default:
		break;
	}
	return ret;
}

int fimc_is_v4l2_frame_rate(struct fimc_is_dev *dev, int value)
{
	int ret = 0;

	switch (value) {
	case 0: /* AUTO Mode */
		IS_SENSOR_SET_FRAME_RATE(dev, 30);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout 1 : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(FIMC_IS_STATE_HW_STREAM_ON, &dev->pipe_state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout 2: %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev, 66666);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout 3: %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(FIMC_IS_STATE_HW_STREAM_ON, &dev->pipe_state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout 4: %s\n", __func__);
				return -EINVAL;
			}
		}
		break;
	default:
		IS_SENSOR_SET_FRAME_RATE(dev, value);
		IS_SET_PARAM_BIT(dev, PARAM_SENSOR_FRAME_RATE);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
			FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout 1 : %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(FIMC_IS_STATE_HW_STREAM_ON, &dev->pipe_state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout 2: %s\n", __func__);
				return -EINVAL;
			}
		}
		IS_ISP_SET_PARAM_OTF_INPUT_CMD(dev, OTF_INPUT_COMMAND_ENABLE);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MIN(dev, 0);
		IS_ISP_SET_PARAM_OTF_INPUT_FRAMETIME_MAX(dev,
							(u32)(1000000/value));
		IS_SET_PARAM_BIT(dev, PARAM_ISP_OTF_INPUT);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
		fimc_is_hw_set_param(dev);
		ret = wait_event_timeout(dev->irq_queue,
			test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
		if (!ret) {
			err("wait timeout 3: %s\n", __func__);
			return -EINVAL;
		}
		if (test_bit(FIMC_IS_STATE_HW_STREAM_ON, &dev->pipe_state)) {
			IS_ISP_SET_PARAM_CONTROL_CMD(dev,
						CONTROL_COMMAND_START);
			IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
			IS_INC_PARAM_NUM(dev);
			fimc_is_mem_cache_clean((void *)dev->is_p_region,
								IS_PARAM_SIZE);
			clear_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state);
			fimc_is_hw_set_param(dev);
			ret = wait_event_timeout(dev->irq_queue,
				test_bit(IS_ST_BLOCK_CMD_CLEARED, &dev->state),
					FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR);
			if (!ret) {
				err("wait timeout 4: %s\n", __func__);
				return -EINVAL;
			}
		}
	}
	return 0;
}

int fimc_is_v4l2_ae_awb_lockunlock(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case AE_UNLOCK_AWB_UNLOCK:
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
							IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case AE_LOCK_AWB_UNLOCK:
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AE);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AWB);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case AE_UNLOCK_AWB_LOCK:
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_START);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AE);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AWB);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	case AE_LOCK_AWB_LOCK:
		IS_ISP_SET_PARAM_AA_CMD(dev, ISP_AA_COMMAND_STOP);
		IS_ISP_SET_PARAM_AA_TARGET(dev, ISP_AA_TARGET_AE |
						ISP_AA_TARGET_AWB);
		IS_SET_PARAM_BIT(dev, PARAM_ISP_AA);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
		break;
	default:
		break;
	}
	return ret;
}

int fimc_is_v4l2_set_isp(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_ISP_BYPASS_DISABLE:
		IS_ISP_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_DISABLE);
		break;
	case IS_ISP_BYPASS_ENABLE:
		IS_ISP_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_ENABLE);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_ISP_BYPASS_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_set_drc(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_DRC_BYPASS_DISABLE:
		IS_DRC_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_DISABLE);
		IS_DRC_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	case IS_DRC_BYPASS_ENABLE:
		IS_DRC_SET_PARAM_CONTROL_BYPASS(dev, CONTROL_BYPASS_ENABLE);
		IS_DRC_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_DRC_BYPASS_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_cmd_isp(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_ISP_COMMAND_STOP:
		IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
		break;
	case IS_ISP_COMMAND_START:
		IS_ISP_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	default:
		return ret;
	}
	if (value >= 0 && value < IS_ISP_COMMAND_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_ISP_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_cmd_drc(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_DRC_COMMAND_STOP:
		IS_DRC_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
		break;
	case IS_DRC_COMMAND_START:
		IS_DRC_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	}
	if (value >= 0 && value < IS_ISP_COMMAND_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_DRC_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_cmd_fd(struct fimc_is_dev *dev, int value)
{
	int ret = 0;
	switch (value) {
	case IS_FD_COMMAND_STOP:
		dbg("IS_FD_COMMAND_STOP\n");
		IS_FD_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_STOP);
		break;
	case IS_FD_COMMAND_START:
		dbg("IS_FD_COMMAND_START\n");
		IS_FD_SET_PARAM_CONTROL_CMD(dev, CONTROL_COMMAND_START);
		break;
	}
	if (value >= 0 && value < IS_ISP_COMMAND_MAX) {
		IS_SET_PARAM_BIT(dev, PARAM_FD_CONTROL);
		IS_INC_PARAM_NUM(dev);
		fimc_is_mem_cache_clean((void *)dev->is_p_region,
			IS_PARAM_SIZE);
		fimc_is_hw_set_param(dev);
	}
	return ret;
}

int fimc_is_v4l2_shot_mode(struct fimc_is_dev *dev, int value)
{
	int ret = 0;

	dbg("%s\n", __func__);
	IS_SET_PARAM_GLOBAL_SHOTMODE_CMD(dev, value);
	IS_SET_PARAM_BIT(dev, PARAM_GLOBAL_SHOTMODE);
	IS_INC_PARAM_NUM(dev);
	fimc_is_mem_cache_clean((void *)dev->is_p_region, IS_PARAM_SIZE);
	fimc_is_hw_set_param(dev);
	return ret;
}
