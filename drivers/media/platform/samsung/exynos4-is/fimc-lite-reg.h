/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 */

#ifndef FIMC_LITE_REG_H_
#define FIMC_LITE_REG_H_

#include <linux/bitops.h>

#include "fimc-lite.h"

/* Camera Source size */
#define FLITE_REG_CISRCSIZE			0x00
#define FLITE_REG_CISRCSIZE_ORDER422_IN_YCBYCR	(0 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_YCRYCB	(1 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_CBYCRY	(2 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_IN_CRYCBY	(3 << 14)
#define FLITE_REG_CISRCSIZE_ORDER422_MASK	(0x3 << 14)
#define FLITE_REG_CISRCSIZE_SIZE_CAM_MASK	(0x3fff << 16 | 0x3fff)

/* Global control */
#define FLITE_REG_CIGCTRL			0x04
#define FLITE_REG_CIGCTRL_YUV422_1P		(0x1e << 24)
#define FLITE_REG_CIGCTRL_RAW8			(0x2a << 24)
#define FLITE_REG_CIGCTRL_RAW10			(0x2b << 24)
#define FLITE_REG_CIGCTRL_RAW12			(0x2c << 24)
#define FLITE_REG_CIGCTRL_RAW14			(0x2d << 24)
/* User defined formats. x = 0...15 */
#define FLITE_REG_CIGCTRL_USER(x)		((0x30 + x - 1) << 24)
#define FLITE_REG_CIGCTRL_FMT_MASK		(0x3f << 24)
#define FLITE_REG_CIGCTRL_SHADOWMASK_DISABLE	BIT(21)
#define FLITE_REG_CIGCTRL_ODMA_DISABLE		BIT(20)
#define FLITE_REG_CIGCTRL_SWRST_REQ		BIT(19)
#define FLITE_REG_CIGCTRL_SWRST_RDY		BIT(18)
#define FLITE_REG_CIGCTRL_SWRST			BIT(17)
#define FLITE_REG_CIGCTRL_TEST_PATTERN_COLORBAR	BIT(15)
#define FLITE_REG_CIGCTRL_INVPOLPCLK		BIT(14)
#define FLITE_REG_CIGCTRL_INVPOLVSYNC		BIT(13)
#define FLITE_REG_CIGCTRL_INVPOLHREF		BIT(12)
/* Interrupts mask bits (1 disables an interrupt) */
#define FLITE_REG_CIGCTRL_IRQ_LASTEN		BIT(8)
#define FLITE_REG_CIGCTRL_IRQ_ENDEN		BIT(7)
#define FLITE_REG_CIGCTRL_IRQ_STARTEN		BIT(6)
#define FLITE_REG_CIGCTRL_IRQ_OVFEN		BIT(5)
#define FLITE_REG_CIGCTRL_IRQ_DISABLE_MASK	(0xf << 5)
#define FLITE_REG_CIGCTRL_SELCAM_MIPI		BIT(3)

/* Image Capture Enable */
#define FLITE_REG_CIIMGCPT			0x08
#define FLITE_REG_CIIMGCPT_IMGCPTEN		BIT(31)
#define FLITE_REG_CIIMGCPT_CPT_FREN		BIT(25)
#define FLITE_REG_CIIMGCPT_CPT_MOD_FRCNT	(1 << 18)
#define FLITE_REG_CIIMGCPT_CPT_MOD_FREN		(0 << 18)

/* Capture Sequence */
#define FLITE_REG_CICPTSEQ			0x0c

/* Camera Window Offset */
#define FLITE_REG_CIWDOFST			0x10
#define FLITE_REG_CIWDOFST_WINOFSEN		BIT(31)
#define FLITE_REG_CIWDOFST_CLROVIY		BIT(31)
#define FLITE_REG_CIWDOFST_CLROVFICB		BIT(15)
#define FLITE_REG_CIWDOFST_CLROVFICR		BIT(14)
#define FLITE_REG_CIWDOFST_OFST_MASK		((0x1fff << 16) | 0x1fff)

/* Camera Window Offset2 */
#define FLITE_REG_CIWDOFST2			0x14

/* Camera Output DMA Format */
#define FLITE_REG_CIODMAFMT			0x18
#define FLITE_REG_CIODMAFMT_RAW_CON		BIT(15)
#define FLITE_REG_CIODMAFMT_PACK12		BIT(14)
#define FLITE_REG_CIODMAFMT_YCBYCR		(0 << 4)
#define FLITE_REG_CIODMAFMT_YCRYCB		(1 << 4)
#define FLITE_REG_CIODMAFMT_CBYCRY		(2 << 4)
#define FLITE_REG_CIODMAFMT_CRYCBY		(3 << 4)
#define FLITE_REG_CIODMAFMT_YCBCR_ORDER_MASK	(0x3 << 4)

/* Camera Output Canvas */
#define FLITE_REG_CIOCAN			0x20
#define FLITE_REG_CIOCAN_MASK			((0x3fff << 16) | 0x3fff)

/* Camera Output DMA Offset */
#define FLITE_REG_CIOOFF			0x24
#define FLITE_REG_CIOOFF_MASK			((0x3fff << 16) | 0x3fff)

/* Camera Output DMA Start Address */
#define FLITE_REG_CIOSA				0x30

/* Camera Status */
#define FLITE_REG_CISTATUS			0x40
#define FLITE_REG_CISTATUS_MIPI_VVALID		BIT(22)
#define FLITE_REG_CISTATUS_MIPI_HVALID		BIT(21)
#define FLITE_REG_CISTATUS_MIPI_DVALID		BIT(20)
#define FLITE_REG_CISTATUS_ITU_VSYNC		BIT(14)
#define FLITE_REG_CISTATUS_ITU_HREFF		BIT(13)
#define FLITE_REG_CISTATUS_OVFIY		BIT(10)
#define FLITE_REG_CISTATUS_OVFICB		BIT(9)
#define FLITE_REG_CISTATUS_OVFICR		BIT(8)
#define FLITE_REG_CISTATUS_IRQ_SRC_OVERFLOW	BIT(7)
#define FLITE_REG_CISTATUS_IRQ_SRC_LASTCAPEND	BIT(6)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMSTART	BIT(5)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMEND	BIT(4)
#define FLITE_REG_CISTATUS_IRQ_CAM		BIT(0)
#define FLITE_REG_CISTATUS_IRQ_MASK		(0xf << 4)

/* Camera Status2 */
#define FLITE_REG_CISTATUS2			0x44
#define FLITE_REG_CISTATUS2_LASTCAPEND		BIT(1)
#define FLITE_REG_CISTATUS2_FRMEND		BIT(0)

/* Qos Threshold */
#define FLITE_REG_CITHOLD			0xf0
#define FLITE_REG_CITHOLD_W_QOS_EN		BIT(30)

/* Camera General Purpose */
#define FLITE_REG_CIGENERAL			0xfc
/* b0: 1 - camera B, 0 - camera A */
#define FLITE_REG_CIGENERAL_CAM_B		BIT(0)

#define FLITE_REG_CIFCNTSEQ			0x100
#define FLITE_REG_CIOSAN(x)			(0x200 + (4 * (x)))

/* ----------------------------------------------------------------------------
 * Function declarations
 */
void flite_hw_reset(struct fimc_lite *dev);
void flite_hw_clear_pending_irq(struct fimc_lite *dev);
u32 flite_hw_get_interrupt_source(struct fimc_lite *dev);
void flite_hw_clear_last_capture_end(struct fimc_lite *dev);
void flite_hw_set_interrupt_mask(struct fimc_lite *dev);
void flite_hw_capture_start(struct fimc_lite *dev);
void flite_hw_capture_stop(struct fimc_lite *dev);
void flite_hw_set_camera_bus(struct fimc_lite *dev,
			     const struct fimc_source_info *s_info);
void flite_hw_set_window_offset(struct fimc_lite *dev, const struct flite_frame *f);
void flite_hw_set_source_format(struct fimc_lite *dev, const struct flite_frame *f);

void flite_hw_set_output_dma(struct fimc_lite *dev, const struct flite_frame *f,
			     bool enable);
void flite_hw_set_dma_window(struct fimc_lite *dev, const struct flite_frame *f);
void flite_hw_set_test_pattern(struct fimc_lite *dev, bool on);
void flite_hw_dump_regs(struct fimc_lite *dev, const char *label);
void flite_hw_set_dma_buffer(struct fimc_lite *dev, struct flite_buffer *buf);
void flite_hw_mask_dma_buffer(struct fimc_lite *dev, u32 index);

static inline void flite_hw_set_dma_buf_mask(struct fimc_lite *dev, u32 mask)
{
	writel(mask, dev->regs + FLITE_REG_CIFCNTSEQ);
}

#endif /* FIMC_LITE_REG_H */
