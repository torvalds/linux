/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mach/videonode.h>
#ifdef CONFIG_BUSFREQ_OPP
#ifdef CONFIG_CPU_EXYNOS5250
#include <mach/dev.h>
#endif
#endif
#include <media/exynos_mc.h>
#include <linux/cma.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/v4l2-mediabus.h>
#include <linux/bug.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include "fimc-is-time.h"
#include "fimc-is-core.h"
#include "fimc-is-regs.h"
#include "fimc-is-interface.h"
#include "fimc-is-device-flite.h"

#define FIMCLITE0_REG_BASE	(S5P_VA_FIMCLITE0)  /* phy : 0x13c0_0000 */
#define FIMCLITE1_REG_BASE	(S5P_VA_FIMCLITE1)  /* phy : 0x13c1_0000 */
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
#define FLITE_REG_CIGCTRL_OLOCAL_DISABLE		(1 << 22)
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

/* Camera Status3 */
#define FLITE_REG_CISTATUS3				0x48
#define FLITE_REG_CISTATUS3_PRESENT_MASK		(0x3F)

/* Qos Threshold */
#define FLITE_REG_CITHOLD				(0xF0)
#define FLITE_REG_CITHOLD_W_QOS_EN			(1 << 30)
#define FLITE_REG_CITHOLD_WTH_QOS(x)			((x) << 0)

/* Camera General Purpose */
#define FLITE_REG_CIGENERAL				(0xFC)
#define FLITE_REG_CIGENERAL_CAM_A			(0)
#define FLITE_REG_CIGENERAL_CAM_B			(1)
#define FLITE_REG_CIGENERAL_CAM_C			(2)

#define FLITE_REG_CIFCNTSEQ				0x100

static void flite_hw_set_cam_source_size(unsigned long flite_reg_base,
	struct fimc_is_frame_info *f_frame)
{
	u32 cfg = 0;

	cfg |= FLITE_REG_CISRCSIZE_SIZE_H(f_frame->o_width);
	cfg |= FLITE_REG_CISRCSIZE_SIZE_V(f_frame->o_height);

	writel(cfg, flite_reg_base + FLITE_REG_CISRCSIZE);
}

static void flite_hw_set_dma_offset(unsigned long flite_reg_base,
	struct fimc_is_frame_info *f_frame,
	struct fimc_is_queue *queue)
{
	u32 cfg = 0;

	if (queue->framecfg.format.pixelformat == V4L2_PIX_FMT_SBGGR12)
		cfg |= FLITE_REG_CIOCAN_OCAN_H(roundup(f_frame->o_width, 10));
	else
		cfg |= FLITE_REG_CIOCAN_OCAN_H(f_frame->o_width);

	cfg |= FLITE_REG_CIOCAN_OCAN_V(f_frame->o_height);

	writel(cfg, flite_reg_base + FLITE_REG_CIOCAN);
}

static void flite_hw_set_cam_channel(unsigned long flite_reg_base)
{
	u32 cfg = 0;

	/*
	 * this register decide that otf is used to a channel
	 * only cam A support otf interface
	 * hardware can not support several otf at same time
	 */

#ifdef USE_OTF_INTERFACE
	if (flite_reg_base == (unsigned long)FIMCLITE0_REG_BASE) {
		cfg = FLITE_REG_CIGENERAL_CAM_A;
		writel(cfg, flite_reg_base + FLITE_REG_CIGENERAL);
	}
#endif
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

static int flite_hw_get_capture_status(unsigned long flite_reg_base)
{
	return (readl(flite_reg_base + FLITE_REG_CIIMGCPT)
		& FLITE_REG_CIIMGCPT_IMGCPTEN) ? 1 : 0;
}

static int flite_hw_set_source_format(unsigned long flite_reg_base)
{
	u32 cfg = 0;

	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);
	cfg |= FLITE_REG_CIGCTRL_RAW10;
	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);

	return 0;
}

static void flite_hw_set_dma_fmt(unsigned long flite_reg_base,
	struct fimc_is_queue *queue)
{
	u32 cfg = 0;

	BUG_ON(!queue);

	cfg = readl(flite_reg_base + FLITE_REG_CIODMAFMT);

	if (queue->framecfg.format.pixelformat == V4L2_PIX_FMT_SBGGR12)
		cfg |= FLITE_REG_CIODMAFMT_PACK12;
	else
		cfg |= FLITE_REG_CIODMAFMT_NORMAL;

	writel(cfg, flite_reg_base + FLITE_REG_CIODMAFMT);
}

static void flite_hw_set_output_dma(unsigned long flite_reg_base, bool enable,
	struct fimc_is_queue *queue)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);

	if (enable) {
		cfg &= ~FLITE_REG_CIGCTRL_ODMA_DISABLE;
		flite_hw_set_dma_fmt(flite_reg_base, queue);
	} else {
		cfg |= FLITE_REG_CIGCTRL_ODMA_DISABLE;
	}

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_output_local(unsigned long flite_reg_base, bool enable)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);

	if (enable)
		cfg &= ~FLITE_REG_CIGCTRL_OLOCAL_DISABLE;
	else
		cfg |= FLITE_REG_CIGCTRL_OLOCAL_DISABLE;

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

	/* for checking stop complete */
	cfg &= ~FLITE_REG_CIGCTRL_IRQ_LASTEN0_DISABLE;

	/* for checking frame start */
	cfg &= ~FLITE_REG_CIGCTRL_IRQ_STARTEN0_DISABLE;

	/* for checking frame end */
	cfg &= ~FLITE_REG_CIGCTRL_IRQ_ENDEN0_DISABLE;

	/* for checking overflow */
	cfg &= ~FLITE_REG_CIGCTRL_IRQ_OVFEN0_DISABLE;

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}

static void flite_hw_clr_interrupt_source(unsigned long flite_reg_base)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);

	/* for checking stop complete */
	cfg |= FLITE_REG_CIGCTRL_IRQ_LASTEN0_DISABLE;

	/* for checking frame start */
	cfg |= FLITE_REG_CIGCTRL_IRQ_STARTEN0_DISABLE;

	/* for checking frame end */
	cfg |= FLITE_REG_CIGCTRL_IRQ_ENDEN0_DISABLE;

	/* for checking overflow */
	cfg |= FLITE_REG_CIGCTRL_IRQ_OVFEN0_DISABLE;

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}

static void flite_hw_force_reset(unsigned long flite_reg_base)
{
	u32 cfg = 0, retry = 100;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);

	/* request sw reset */
	cfg |= FLITE_REG_CIGCTRL_SWRST_REQ;
	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);

	/* checking reset ready */
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);
	while (retry-- && !(cfg & FLITE_REG_CIGCTRL_SWRST_RDY))
		cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);

	if (!(cfg & FLITE_REG_CIGCTRL_SWRST_RDY))
		warn("[CamIF] sw reset is not read but forcelly");

	/* sw reset */
	cfg |= FLITE_REG_CIGCTRL_SWRST;
	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
	warn("[CamIF] sw reset");
}

static void flite_hw_set_camera_type(unsigned long flite_reg_base)
{
	u32 cfg = 0;
	cfg = readl(flite_reg_base + FLITE_REG_CIGCTRL);

	cfg |= FLITE_REG_CIGCTRL_SELCAM_MIPI;

	writel(cfg, flite_reg_base + FLITE_REG_CIGCTRL);
}

static void flite_hw_set_window_offset(unsigned long flite_reg_base,
					struct fimc_is_frame_info *f_frame)
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

int flite_hw_get_present_frame_buffer(unsigned long flite_reg_base)
{
	u32 status = 0;

	status = readl(flite_reg_base + FLITE_REG_CISTATUS3);
	status &= FLITE_REG_CISTATUS3_PRESENT_MASK;

	return status;
}

int flite_hw_get_status2(unsigned long flite_reg_base)
{
	u32 status = 0;

	status = readl(flite_reg_base + FLITE_REG_CISTATUS2);

	return status;
}

void flite_hw_set_status1(unsigned long flite_reg_base, u32 val)
{
	writel(val, flite_reg_base + FLITE_REG_CISTATUS);
}

int flite_hw_get_status1(unsigned long flite_reg_base)
{
	u32 status = 0;

	status = readl(flite_reg_base + FLITE_REG_CISTATUS);

	return status;
}

int flite_hw_getnclr_status1(unsigned long flite_reg_base)
{
	u32 status = 0;

	status = readl(flite_reg_base + FLITE_REG_CISTATUS);
	writel(0, flite_reg_base + FLITE_REG_CISTATUS);

	return status;
}

void flite_hw_set_status2(unsigned long flite_reg_base, u32 val)
{
	writel(val, flite_reg_base + FLITE_REG_CISTATUS2);
}

void flite_hw_set_start_addr(unsigned long flite_reg_base, u32 number, u32 addr)
{
	u32 target;
	if (number == 0) {
		target = flite_reg_base + 0x30;
	} else if (number >= 1) {
		number--;
		target = flite_reg_base + 0x200 + (0x4*number);
	} else
		target = 0;

	writel(addr, target);
}

u32 flite_hw_get_start_addr(unsigned long flite_reg_base, u32 number)
{
	u32 target;
	if (number == 0) {
		target = flite_reg_base + 0x30;
	} else if (number >= 1) {
		number--;
		target = flite_reg_base + 0x200 + (0x4*number);
	} else
		target = 0;

	return readl(target);
}

void flite_hw_set_use_buffer(unsigned long flite_reg_base, u32 number)
{
	u32 buffer;
	buffer = readl(flite_reg_base + FLITE_REG_CIFCNTSEQ);
	buffer |= 1<<number;
	writel(buffer, flite_reg_base + FLITE_REG_CIFCNTSEQ);
}

void flite_hw_set_unuse_buffer(unsigned long flite_reg_base, u32 number)
{
	u32 buffer;
	buffer = readl(flite_reg_base + FLITE_REG_CIFCNTSEQ);
	buffer &= ~(1<<number);
	writel(buffer, flite_reg_base + FLITE_REG_CIFCNTSEQ);
}

u32 flite_hw_get_buffer_seq(unsigned long flite_reg_base)
{
	u32 buffer;
	buffer = readl(flite_reg_base + FLITE_REG_CIFCNTSEQ);
	return buffer;
}

int init_fimc_lite(unsigned long mipi_reg_base)
{
	int i;

	writel(0, mipi_reg_base + FLITE_REG_CIFCNTSEQ);

	for (i = 0; i < 32; i++)
		flite_hw_set_start_addr(mipi_reg_base , i, 0xffffffff);

	return 0;
}

int start_fimc_lite(unsigned long mipi_reg_base,
	struct fimc_is_frame_info *f_frame,
	struct fimc_is_queue *queue)
{
	flite_hw_set_cam_channel(mipi_reg_base);
	flite_hw_set_cam_source_size(mipi_reg_base, f_frame);
	flite_hw_set_dma_offset(mipi_reg_base, f_frame, queue);
	flite_hw_set_camera_type(mipi_reg_base);
	flite_hw_set_source_format(mipi_reg_base);
	/*flite_hw_set_output_dma(mipi_reg_base, false);
	flite_hw_set_output_local(base_reg, false);*/

	flite_hw_set_interrupt_source(mipi_reg_base);
	/*flite_hw_set_interrupt_starten0_disable(mipi_reg_base);*/
	flite_hw_set_config_irq(mipi_reg_base);
	flite_hw_set_window_offset(mipi_reg_base, f_frame);
	/* flite_hw_set_test_pattern_enable(); */

	flite_hw_set_last_capture_end_clear(mipi_reg_base);
	flite_hw_set_capture_start(mipi_reg_base);

	/*dbg_front("lite config : %08X\n",
		*((unsigned int*)(base_reg + FLITE_REG_CIFCNTSEQ)));*/

	return 0;
}

static inline void stop_fimc_lite(unsigned long mipi_reg_base)
{
	flite_hw_set_capture_stop(mipi_reg_base);
}

void resize_fimc_lite(unsigned long flite_reg_base,
	struct fimc_is_frame_info *f_frame,
	struct fimc_is_queue *queue)
{

	flite_hw_set_cam_source_size(flite_reg_base, f_frame);
	flite_hw_set_dma_offset(flite_reg_base, f_frame, queue);
	flite_hw_set_window_offset(flite_reg_base, f_frame);
}

static inline void flite_s_buffer_addr(struct fimc_is_device_flite *device,
	u32 bindex, u32 baddr)
{
	flite_hw_set_start_addr(device->regs, bindex, baddr);
}

static inline int flite_s_use_buffer(struct fimc_is_device_flite *device,
	u32 bindex)
{
	int ret = 0;
	struct fimc_is_queue *queue;

	queue = GET_DST_QUEUE(device->vctx);

	if (!atomic_read(&device->bcount)) {
		if (flite_hw_get_status1(device->regs) && (7 << 20)) {
			err("over vblank");
			ret = -EINVAL;
			goto p_err;
		}

		flite_hw_set_use_buffer(device->regs, bindex);
		atomic_inc(&device->bcount);
		flite_hw_set_output_dma(device->regs, true, queue);
	} else {
		flite_hw_set_use_buffer(device->regs, bindex);
		atomic_inc(&device->bcount);
	}

p_err:
	return ret;
}

static inline int flite_s_unuse_buffer(struct fimc_is_device_flite *device,
	u32 bindex)
{
	int ret = 0;
	struct fimc_is_queue *queue;

	queue = GET_DST_QUEUE(device->vctx);

	if (atomic_read(&device->bcount) == 1) {
		if (flite_hw_get_status1(device->regs) && (7 << 20)) {
			err("over vblank");
			ret = -EINVAL;
			goto p_err;
		}

		flite_hw_set_output_dma(device->regs, false, queue);
		flite_hw_set_unuse_buffer(device->regs, bindex);
		atomic_dec(&device->bcount);
	} else {
		flite_hw_set_unuse_buffer(device->regs, bindex);
		atomic_dec(&device->bcount);
	}

p_err:
	return ret;
}

static u32 g_print_cnt = 0;
#define LOG_INTERVAL_OF_DROPS 30
static void tasklet_flite_str0(unsigned long data)
{
	struct fimc_is_device_flite *flite;
	struct fimc_is_device_sensor *sensor;
	struct fimc_is_device_ischain *ischain;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;
	struct fimc_is_groupmgr *groupmgr;
	struct fimc_is_group *group_3ax, *group_isp;
	u32 bstart, fcount, present;

	flite = (struct fimc_is_device_flite *)data;
	present = flite_hw_get_present_frame_buffer(flite->regs) - 1;
	sensor = (struct fimc_is_device_sensor *)flite->private_data;
	ischain = sensor->ischain;
	framemgr = GET_DST_FRAMEMGR(flite->vctx);

	bstart = flite->tasklet_param_str;
	fcount = atomic_read(&flite->fcount);

#ifdef TASKLET_MSG
	pr_info("S%d %d\n", bstart, fcount);
#endif

	/* comparing sw state and hw state */
	if (atomic_read(&flite->bcount) == 2) {
		if ((bstart == FLITE_A_SLOT_VALID) &&
			(present != FLITE_A_SLOT_VALID)) {
			err("invalid state1(sw:%d != hw:%d)", bstart, present);
			flite->sw_trigger = bstart = FLITE_B_SLOT_VALID;
		}

		if ((bstart == FLITE_B_SLOT_VALID) &&
			(present != FLITE_B_SLOT_VALID)) {
			err("invalid state2(sw:%d != hw:%d)", bstart, present);
			flite->sw_trigger = bstart = FLITE_A_SLOT_VALID;
		}
	}

	groupmgr = ischain->groupmgr;
	group_3ax = &ischain->group_3ax;
	group_isp = &ischain->group_isp;
	if (unlikely(list_empty(&group_3ax->smp_trigger.wait_list))) {
		atomic_set(&group_3ax->sensor_fcount,
				fcount + group_3ax->async_shots);

		if (((g_print_cnt % LOG_INTERVAL_OF_DROPS) == 0) ||
			(g_print_cnt < LOG_INTERVAL_OF_DROPS)) {
			warn("grp1(res %d, rcnt %d, scnt %d), "
				"grp2(res %d, rcnt %d, scnt %d), "
				"fcount %d(%d, %d)",
				groupmgr->group_smp_res[group_3ax->id].count,
				atomic_read(&group_3ax->rcount),
				atomic_read(&group_3ax->scount),
				groupmgr->group_smp_res[group_isp->id].count,
				atomic_read(&group_isp->rcount),
				atomic_read(&group_isp->scount),
				fcount + group_3ax->async_shots,
				*last_fcount0, *last_fcount1);
		}
		g_print_cnt++;
	} else {
		g_print_cnt = 0;
		atomic_set(&group_3ax->sensor_fcount, fcount + group_3ax->async_shots);
		up(&group_3ax->smp_trigger);
	}

	framemgr_e_barrier(framemgr, FMGR_IDX_0 + bstart);

	fimc_is_frame_process_head(framemgr, &frame);
	if (frame) {
#ifdef MEASURE_TIME
#ifndef INTERNAL_TIME
		do_gettimeofday(&frame->tzone[TM_FLITE_STR]);
#endif
#endif
		frame->fcount = fcount;
		fimc_is_ischain_camctl(ischain, frame, fcount);
		fimc_is_ischain_tag(ischain, frame);
	} else {
		fimc_is_ischain_camctl(ischain, NULL, fcount);

#ifdef TASKLET_MSG
		merr("[SEN] process is empty", sensor);
		fimc_is_frame_print_all(framemgr);
#endif
	}

	framemgr_x_barrier(framemgr, FMGR_IDX_0 + bstart);
}

static void tasklet_flite_str1(unsigned long data)
{
	struct fimc_is_device_flite *flite;
	struct fimc_is_device_sensor *sensor;
	struct fimc_is_device_ischain *ischain;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;
	u32 bstart, fcount, present;

	flite = (struct fimc_is_device_flite *)data;
	present = flite_hw_get_present_frame_buffer(flite->regs) - 1;
	sensor = (struct fimc_is_device_sensor *)flite->private_data;
	ischain = sensor->ischain;
	framemgr = GET_DST_FRAMEMGR(flite->vctx);

	bstart = flite->tasklet_param_str;
	fcount = atomic_read(&flite->fcount);

#ifdef TASKLET_MSG
	pr_info("S%d %d\n", bstart, fcount);
#endif

	/* comparing sw state and hw state */
	if (atomic_read(&flite->bcount) == 2) {
		if ((bstart == FLITE_A_SLOT_VALID) &&
			(present != FLITE_A_SLOT_VALID)) {
			err("invalid state1(sw:%d != hw:%d)", bstart, present);
			flite->sw_trigger = bstart = FLITE_B_SLOT_VALID;
		}

		if ((bstart == FLITE_B_SLOT_VALID) &&
			(present != FLITE_B_SLOT_VALID)) {
			err("invalid state2(sw:%d != hw:%d)", bstart, present);
			flite->sw_trigger = bstart = FLITE_A_SLOT_VALID;
		}
	}

	framemgr_e_barrier(framemgr, FMGR_IDX_0 + bstart);

	fimc_is_frame_process_head(framemgr, &frame);
	if (frame) {
#ifdef MEASURE_TIME
#ifndef INTERNAL_TIME
		do_gettimeofday(&frame->tzone[TM_FLITE_STR]);
#endif
#endif
		frame->fcount = fcount;
		fimc_is_ischain_camctl(ischain, frame, fcount);
		fimc_is_ischain_tag(ischain, frame);
	} else {
		fimc_is_ischain_camctl(ischain, NULL, fcount);

#ifdef TASKLET_MSG
		merr("[SEN] process is empty", sensor);
		fimc_is_frame_print_all(framemgr);
#endif
	}

	framemgr_x_barrier(framemgr, FMGR_IDX_0 + bstart);
}

static void tasklet_flite_end(unsigned long data)
{
	struct fimc_is_device_flite *flite;
	struct fimc_is_device_sensor *sensor;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;
	u32 index, bdone;

	flite = (struct fimc_is_device_flite *)data;
	sensor = (struct fimc_is_device_sensor *)flite->private_data;
	framemgr = GET_DST_FRAMEMGR(flite->vctx);
	bdone = flite->tasklet_param_end;

#ifdef TASKLET_MSG
	pr_info("E%d %d\n", bdone, atomic_read(&flite->fcount));
#endif

	framemgr_e_barrier(framemgr, FMGR_IDX_1 + bdone);

	if (test_bit(bdone, &flite->state)) {
		fimc_is_frame_process_head(framemgr, &frame);
		if (frame) {
#ifdef MEASURE_TIME
#ifndef INTERNAL_TIME
			do_gettimeofday(&frame->tzone[TM_FLITE_END]);
#endif
#endif
			/* 1. current frame transition to completion */
			index = frame->index;
			fimc_is_frame_trans_pro_to_com(framemgr, frame);

			/* 2. next frame ready */
			fimc_is_frame_request_head(framemgr, &frame);
			if (frame) {
				flite_s_buffer_addr(flite, bdone,
					frame->dvaddr_buffer[0]);
				set_bit(bdone, &flite->state);
				fimc_is_frame_trans_req_to_pro(framemgr, frame);
			} else {
				if (!flite_s_unuse_buffer(flite, bdone)) {
					clear_bit(bdone, &flite->state);
#ifdef TASKLET_MSG
					merr("[SEN] request is empty0(%d slot)",
						sensor, bdone);
#endif
				}
			}

			/* 3. current frame done */
			buffer_done(flite->vctx, index);
		} else {
#ifdef TASKLET_MSG
			merr("[SEN] process is empty(invalid state(%d, %ld))",
				sensor, bdone, flite->state);
			fimc_is_frame_print_all(framemgr);
#endif
		}
	} else {
		fimc_is_frame_request_head(framemgr, &frame);
		if (frame) {
			flite_s_buffer_addr(flite, bdone,
				frame->dvaddr_buffer[0]);
			if (!flite_s_use_buffer(flite, bdone)) {
				set_bit(bdone, &flite->state);
				fimc_is_frame_trans_req_to_pro(framemgr, frame);
			}
		} else {
#ifdef TASKLET_MSG
			merr("request is empty1(%d slot)", sensor, bdone);
			fimc_is_frame_print_all(framemgr);
#endif
		}
	}

	framemgr_x_barrier(framemgr, FMGR_IDX_1 + bdone);
}

static inline void notify_fcount(u32 channel, u32 fcount)
{
	if (channel == FLITE_ID_A)
		*notify_fcount_sen0 = fcount;
	else if (channel == FLITE_ID_B)
		*notify_fcount_sen1 = fcount;
	else if (channel == FLITE_ID_C)
		*notify_fcount_sen2 = fcount;
	else
		err("unresolved channel(%d)", channel);
}

static irqreturn_t fimc_is_flite_irq_handler(int irq, void *data)
{
	u32 status, status1, status2, i;
	struct fimc_is_device_flite *flite;
	struct fimc_is_device_sensor *sensor;
	int instance;

	flite = data;
	sensor = (struct fimc_is_device_sensor *)flite->private_data;
	instance = sensor->instance;
	status1 = flite_hw_getnclr_status1(flite->regs);
	status = status1 & (3 << 4);

	if (test_bit(FIMC_IS_FLITE_LAST_CAPTURE, &flite->state)) {
		if (status1) {
			pr_info("[CamIF%d] last status1 : 0x%08X\n",
				flite->channel, status1);
			goto clear_status;
		}

		err("[CamIF%d] unintended intr is occured", flite->channel);

		for (i = 0; i < 278; i += 4)
			pr_info("REG[%X] : 0x%08X\n", i,
				readl(flite->regs + i));

		flite_hw_force_reset(flite->regs);

		goto clear_status;
	}

	if (status) {
		if (status == (3 << 4)) {
#ifdef DBG_FLITEISR
			printk(KERN_CONT "*");
#endif
			/* frame both interrupt since latency */
			if (flite->sw_checker) {
#ifdef DBG_FLITEISR
				printk(KERN_CONT ">");
#endif
				/* frame end interrupt */
				flite->sw_checker = EXPECT_FRAME_START;
				flite->tasklet_param_end = flite->sw_trigger;
				tasklet_schedule(&flite->tasklet_flite_end);

#ifdef DBG_FLITEISR
				printk(KERN_CONT "<");
#endif
				/* frame start interrupt */
				flite->sw_checker = EXPECT_FRAME_END;
				if (flite->sw_trigger)
					flite->sw_trigger = FLITE_A_SLOT_VALID;
				else
					flite->sw_trigger = FLITE_B_SLOT_VALID;
				flite->tasklet_param_str = flite->sw_trigger;
				atomic_inc(&flite->fcount);
				notify_fcount(flite->channel, atomic_read(&flite->fcount));
				tasklet_schedule(&flite->tasklet_flite_str);
			} else {
#ifdef DBG_FLITEISR
				printk(KERN_CONT "<");
#endif
				/* frame start interrupt */
				flite->sw_checker = EXPECT_FRAME_END;
				if (flite->sw_trigger)
					flite->sw_trigger = FLITE_A_SLOT_VALID;
				else
					flite->sw_trigger = FLITE_B_SLOT_VALID;
				flite->tasklet_param_str = flite->sw_trigger;
				atomic_inc(&flite->fcount);
				notify_fcount(flite->channel, atomic_read(&flite->fcount));
				tasklet_schedule(&flite->tasklet_flite_str);
#ifdef DBG_FLITEISR
				printk(KERN_CONT ">");
#endif
				/* frame end interrupt */
				flite->sw_checker = EXPECT_FRAME_START;
				flite->tasklet_param_end = flite->sw_trigger;
				tasklet_schedule(&flite->tasklet_flite_end);
			}
		} else if (status == (2 << 4)) {
#ifdef DBG_FLITEISR
			printk(KERN_CONT "<");
#endif
			/* frame start interrupt */
			flite->sw_checker = EXPECT_FRAME_END;
			if (flite->sw_trigger)
				flite->sw_trigger = FLITE_A_SLOT_VALID;
			else
				flite->sw_trigger = FLITE_B_SLOT_VALID;
			flite->tasklet_param_str = flite->sw_trigger;
			atomic_inc(&flite->fcount);
			notify_fcount(flite->channel, atomic_read(&flite->fcount));
			tasklet_schedule(&flite->tasklet_flite_str);
		} else {
#ifdef DBG_FLITEISR
			printk(KERN_CONT ">");
#endif
			/* frame end interrupt */
			flite->sw_checker = EXPECT_FRAME_START;
			flite->tasklet_param_end = flite->sw_trigger;
			tasklet_schedule(&flite->tasklet_flite_end);
		}
	}

clear_status:
	if (status1 & (1 << 6)) {
		/* Last Frame Capture Interrupt */
		printk(KERN_INFO "[CamIF%d] Last Frame Capture(fcount : %d)\n",
			instance, atomic_read(&flite->fcount));

		/* Clear LastCaptureEnd bit */
		status2 = flite_hw_get_status2(flite->regs);
		status2 &= ~(0x1 << 1);
		flite_hw_set_status2(flite->regs, status2);

		/* Notify last capture */
		set_bit(FIMC_IS_FLITE_LAST_CAPTURE, &flite->state);
		wake_up(&flite->wait_queue);
	}

	if (status1 & (1 << 8)) {
		u32 ciwdofst;

		/* err("[CamIF_0]Overflow Cr\n"); */
		pr_err("[CamIF%d] OFCR\n", instance);
		ciwdofst = readl(flite->regs + 0x10);
		ciwdofst  |= (0x1 << 14);
		writel(ciwdofst, flite->regs + 0x10);
		ciwdofst  &= ~(0x1 << 14);
		writel(ciwdofst, flite->regs + 0x10);
	}

	if (status1 & (1 << 9)) {
		u32 ciwdofst;

		/* err("[CamIF_0]Overflow Cb\n"); */
		pr_err("[CamIF%d] OFCB\n", instance);
		ciwdofst = readl(flite->regs + 0x10);
		ciwdofst  |= (0x1 << 15);
		writel(ciwdofst, flite->regs + 0x10);
		ciwdofst  &= ~(0x1 << 15);
		writel(ciwdofst, flite->regs + 0x10);
	}

	if (status1 & (1 << 10)) {
		u32 ciwdofst;

		/* err("[CamIF_0]Overflow Y\n"); */
		pr_err("[CamIF%d] OFY\n", instance);
		ciwdofst = readl(flite->regs + 0x10);
		ciwdofst  |= (0x1 << 30);
		writel(ciwdofst, flite->regs + 0x10);
		ciwdofst  &= ~(0x1 << 30);
		writel(ciwdofst, flite->regs + 0x10);
	}

	return IRQ_HANDLED;
}

int fimc_is_flite_probe(struct fimc_is_device_flite *flite, u32 data)
{
	int ret = 0;

	flite->private_data = data;
	init_waitqueue_head(&flite->wait_queue);

	if (flite->channel == FLITE_ID_A) {
		flite->regs = (unsigned long)S5P_VA_FIMCLITE0;

#ifdef USE_OTF_INTERFACE
		tasklet_init(&flite->tasklet_flite_str,
			tasklet_flite_str0,
			(unsigned long)flite);

		tasklet_init(&flite->tasklet_flite_end,
			tasklet_flite_end,
			(unsigned long)flite);
#else
		tasklet_init(&flite->tasklet_flite_str,
			tasklet_flite_str1,
			(unsigned long)flite);

		tasklet_init(&flite->tasklet_flite_end,
			tasklet_flite_end,
			(unsigned long)flite);
#endif
	} else if (flite->channel == FLITE_ID_B) {
		flite->regs = (unsigned long)S5P_VA_FIMCLITE1;

		tasklet_init(&flite->tasklet_flite_str,
			tasklet_flite_str1,
			(unsigned long)flite);

		tasklet_init(&flite->tasklet_flite_end,
			tasklet_flite_end,
			(unsigned long)flite);
	} else if (flite->channel == FLITE_ID_C) {
		flite->regs = (unsigned long)S5P_VA_FIMCLITE2;

		tasklet_init(&flite->tasklet_flite_str,
			tasklet_flite_str1,
			(unsigned long)flite);

		tasklet_init(&flite->tasklet_flite_end,
			tasklet_flite_end,
			(unsigned long)flite);
	} else
		err("unresolved channel input");

	return ret;
}

int fimc_is_flite_open(struct fimc_is_device_flite *flite,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;

	BUG_ON(!flite);
	BUG_ON(!vctx);

	flite->vctx = vctx;
	atomic_set(&flite->fcount, 0);
	atomic_set(&flite->bcount, 0);

	clear_bit(FIMC_IS_FLITE_LAST_CAPTURE, &flite->state);
	clear_bit(FLITE_A_SLOT_VALID, &flite->state);
	clear_bit(FLITE_B_SLOT_VALID, &flite->state);

	if (flite->channel == FLITE_ID_A) {
		ret = request_irq(IRQ_FIMC_LITE0,
			fimc_is_flite_irq_handler,
			IRQF_SHARED,
			"fimc-lite0",
			flite);
		if (ret)
			err("request_irq(L0) failed\n");
	} else if (flite->channel == FLITE_ID_B) {
		ret = request_irq(IRQ_FIMC_LITE1,
			fimc_is_flite_irq_handler,
			IRQF_SHARED,
			"fimc-lite1",
			flite);
		if (ret)
			err("request_irq(L1) failed\n");
	} else if (flite->channel == FLITE_ID_C) {
		ret = request_irq(IRQ_FIMC_LITE2,
			fimc_is_flite_irq_handler,
			IRQF_SHARED,
			"fimc-lite2",
			flite);
		if (ret)
			err("request_irq(L2) failed\n");
	} else
		err("unresolved channel input");

	return ret;
}

int fimc_is_flite_close(struct fimc_is_device_flite *this)
{
	int ret = 0;

	if (this->channel == FLITE_ID_A) {
		free_irq(IRQ_FIMC_LITE0, this);
	} else if (this->channel == FLITE_ID_B) {
		free_irq(IRQ_FIMC_LITE1, this);
	} else if (this->channel == FLITE_ID_C) {
		free_irq(IRQ_FIMC_LITE2, this);
	} else {
		err("FLITE%d is invalid", this->channel);
		ret = -EINVAL;
	}

	return ret;
}

int fimc_is_flite_start(struct fimc_is_device_flite *flite,
	struct fimc_is_frame_info *finfo,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	u32 buffer;
	bool buffer_ready;
	unsigned long flags;
	struct fimc_is_frame *item;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_queue *queue;

	BUG_ON(!flite);
	BUG_ON(!finfo);
	BUG_ON(!vctx);

	queue = GET_DST_QUEUE(vctx);
	framemgr = GET_DST_FRAMEMGR(vctx);
	buffer = 0;
	buffer_ready = false;

	flite->sw_trigger = FLITE_B_SLOT_VALID;
	flite->sw_checker = EXPECT_FRAME_START;
	flite->tasklet_param_str = 0;
	flite->tasklet_param_end = 0;
	atomic_set(&flite->bcount, 0);
	clear_bit(FIMC_IS_FLITE_LAST_CAPTURE, &flite->state);
	clear_bit(FLITE_A_SLOT_VALID, &flite->state);
	clear_bit(FLITE_B_SLOT_VALID, &flite->state);

	flite_hw_force_reset(flite->regs);
	init_fimc_lite(flite->regs);

	framemgr_e_barrier_irqs(framemgr, 0, flags);

	if (framemgr->frame_req_cnt >= 1) {
		buffer = queue->buf_dva[0][0];
		buffer_ready = true;
		flite_s_use_buffer(flite, FLITE_A_SLOT_VALID);
		flite_s_buffer_addr(flite, FLITE_A_SLOT_VALID, buffer);
		set_bit(FLITE_A_SLOT_VALID, &flite->state);
		fimc_is_frame_request_head(framemgr, &item);
		fimc_is_frame_trans_req_to_pro(framemgr, item);
	}

	if (framemgr->frame_req_cnt >= 1) {
		buffer = queue->buf_dva[1][0];
		buffer_ready = true;
		flite_s_use_buffer(flite, FLITE_B_SLOT_VALID);
		flite_s_buffer_addr(flite, FLITE_B_SLOT_VALID, buffer);
		set_bit(FLITE_B_SLOT_VALID, &flite->state);
		fimc_is_frame_request_head(framemgr, &item);
		fimc_is_frame_trans_req_to_pro(framemgr, item);
	}

	framemgr_x_barrier_irqr(framemgr, 0, flags);

	flite_hw_set_output_dma(flite->regs, buffer_ready, queue);
#ifdef USE_OTF_INTERFACE
	flite_hw_set_output_local(flite->regs, true);
#else
	flite_hw_set_output_local(flite->regs, false);
#endif

	start_fimc_lite(flite->regs, finfo, queue);

	return ret;
}

int fimc_is_flite_stop(struct fimc_is_device_flite *flite, bool wait)
{
	int ret = 0;

	BUG_ON(!flite);

	stop_fimc_lite(flite->regs);

	dbg_back("waiting last capture\n");
	ret = wait_event_timeout(flite->wait_queue,
		(!wait || test_bit(FIMC_IS_FLITE_LAST_CAPTURE, &flite->state)),
		FIMC_IS_FLITE_STOP_TIMEOUT);
	if (!ret) {
		/* forcely stop */
		stop_fimc_lite(flite->regs);
		set_bit(FIMC_IS_FLITE_LAST_CAPTURE, &flite->state);
		err("last capture timeout:%s", __func__);
		msleep(200);
		flite_hw_force_reset(flite->regs);
		ret = -ETIME;
	} else {
		ret = 0;
	}

	/* for preventing invalid memory access */
	flite_hw_set_unuse_buffer(flite->regs, FLITE_A_SLOT_VALID);
	flite_hw_set_unuse_buffer(flite->regs, FLITE_B_SLOT_VALID);
	flite_hw_set_output_dma(flite->regs, false, NULL);
	flite_hw_clr_interrupt_source(flite->regs);

	return ret;
}

void fimc_is_flite_restart(struct fimc_is_device_flite *this,
	struct fimc_is_frame_info *frame,
	struct fimc_is_video_ctx *vctx)
{
	struct fimc_is_queue *queue = GET_DST_QUEUE(vctx);
	int capturing = flite_hw_get_capture_status(this->regs);

	if (capturing)
		stop_fimc_lite(this->regs);

	resize_fimc_lite(this->regs, frame, queue);

	if (test_bit(FIMC_IS_FLITE_LAST_CAPTURE, &this->state)) {
		/* need to check other device state & value */
		this->sw_trigger = FLITE_B_SLOT_VALID;
		this->sw_checker = EXPECT_FRAME_START;
		this->tasklet_param_str = 0;
		this->tasklet_param_end = 0;

		clear_bit(FIMC_IS_FLITE_LAST_CAPTURE, &this->state);
	}

	flite_hw_set_interrupt_source(this->regs);
	flite_hw_set_output_dma(this->regs, true, queue);
	flite_hw_set_last_capture_end_clear(this->regs);
	flite_hw_set_capture_start(this->regs);
}

int fimc_is_flite_set_clk(int channel,
	struct fimc_is_core *core,
	struct fimc_is_device_flite *device_flite)
{
	int ret = 0;
	char sensor_mclk[20];
	struct platform_device *pdev;
	struct clk *sclk_mout_isp_sensor = NULL;
	struct clk *sclk_isp_sensor = NULL;
	unsigned long isp_sensor;

	pdev = core->pdev;

	if (test_and_set_bit(channel, &device_flite->clk_state)) {
		pr_debug("%s : already clk on", __func__);
		goto exit;
	}

	/* SENSOR */
	snprintf(sensor_mclk, sizeof(sensor_mclk), "sclk_isp_sensor%d", channel);
	sclk_mout_isp_sensor = clk_get(&pdev->dev, "sclk_mout_isp_sensor");
	if (IS_ERR(sclk_mout_isp_sensor)) {
		pr_err("%s : clk_get(sclk_mout_isp_sensor) failed\n", __func__);
		return PTR_ERR(sclk_mout_isp_sensor);
	}

	sclk_isp_sensor = clk_get(&pdev->dev, sensor_mclk);
	if (IS_ERR(sclk_isp_sensor)) {
		pr_err("%s : clk_get(sclk_isp_sensor0) failed\n", __func__);
		return PTR_ERR(sclk_isp_sensor);
	}

	clk_set_parent(sclk_mout_isp_sensor, clk_get(&pdev->dev, "mout_ipll"));
	clk_set_rate(sclk_isp_sensor, 24 * 1000000);

	isp_sensor = clk_get_rate(sclk_isp_sensor);
	pr_debug("isp_sensor : %ld\n", isp_sensor);

	clk_put(sclk_mout_isp_sensor);
	clk_put(sclk_isp_sensor);

exit:
	return ret;
}

int fimc_is_flite_put_clk(int channel,
	struct fimc_is_core *core,
	struct fimc_is_device_flite *device_flite)
{
	int ret = 0;
	struct platform_device *pdev;

	pdev = core->pdev;

	if (!test_and_clear_bit(channel, &device_flite->clk_state)) {
		pr_debug("%s : already clk off", __func__);
		goto exit;
	}

exit:
	return ret;
}
