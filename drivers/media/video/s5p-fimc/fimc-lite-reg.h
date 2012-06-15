/*
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_LITE_REG_H_
#define FIMC_LITE_REG_H_

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
#define FLITE_REG_CIGCTRL_SHADOWMASK_DISABLE	(1 << 21)
#define FLITE_REG_CIGCTRL_ODMA_DISABLE		(1 << 20)
#define FLITE_REG_CIGCTRL_SWRST_REQ		(1 << 19)
#define FLITE_REG_CIGCTRL_SWRST_RDY		(1 << 18)
#define FLITE_REG_CIGCTRL_SWRST			(1 << 17)
#define FLITE_REG_CIGCTRL_TEST_PATTERN_COLORBAR	(1 << 15)
#define FLITE_REG_CIGCTRL_INVPOLPCLK		(1 << 14)
#define FLITE_REG_CIGCTRL_INVPOLVSYNC		(1 << 13)
#define FLITE_REG_CIGCTRL_INVPOLHREF		(1 << 12)
/* Interrupts mask bits (1 disables an interrupt) */
#define FLITE_REG_CIGCTRL_IRQ_LASTEN		(1 << 8)
#define FLITE_REG_CIGCTRL_IRQ_ENDEN		(1 << 7)
#define FLITE_REG_CIGCTRL_IRQ_STARTEN		(1 << 6)
#define FLITE_REG_CIGCTRL_IRQ_OVFEN		(1 << 5)
#define FLITE_REG_CIGCTRL_IRQ_DISABLE_MASK	(0xf << 5)
#define FLITE_REG_CIGCTRL_SELCAM_MIPI		(1 << 3)

/* Image Capture Enable */
#define FLITE_REG_CIIMGCPT			0x08
#define FLITE_REG_CIIMGCPT_IMGCPTEN		(1 << 31)
#define FLITE_REG_CIIMGCPT_CPT_FREN		(1 << 25)
#define FLITE_REG_CIIMGCPT_CPT_MOD_FRCNT	(1 << 18)
#define FLITE_REG_CIIMGCPT_CPT_MOD_FREN		(0 << 18)

/* Capture Sequence */
#define FLITE_REG_CICPTSEQ			0x0c

/* Camera Window Offset */
#define FLITE_REG_CIWDOFST			0x10
#define FLITE_REG_CIWDOFST_WINOFSEN		(1 << 31)
#define FLITE_REG_CIWDOFST_CLROVIY		(1 << 31)
#define FLITE_REG_CIWDOFST_CLROVFICB		(1 << 15)
#define FLITE_REG_CIWDOFST_CLROVFICR		(1 << 14)
#define FLITE_REG_CIWDOFST_OFST_MASK		((0x1fff << 16) | 0x1fff)

/* Camera Window Offset2 */
#define FLITE_REG_CIWDOFST2			0x14

/* Camera Output DMA Format */
#define FLITE_REG_CIODMAFMT			0x18
#define FLITE_REG_CIODMAFMT_RAW_CON		(1 << 15)
#define FLITE_REG_CIODMAFMT_PACK12		(1 << 14)
#define FLITE_REG_CIODMAFMT_CRYCBY		(0 << 4)
#define FLITE_REG_CIODMAFMT_CBYCRY		(1 << 4)
#define FLITE_REG_CIODMAFMT_YCRYCB		(2 << 4)
#define FLITE_REG_CIODMAFMT_YCBYCR		(3 << 4)
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
#define FLITE_REG_CISTATUS_MIPI_VVALID		(1 << 22)
#define FLITE_REG_CISTATUS_MIPI_HVALID		(1 << 21)
#define FLITE_REG_CISTATUS_MIPI_DVALID		(1 << 20)
#define FLITE_REG_CISTATUS_ITU_VSYNC		(1 << 14)
#define FLITE_REG_CISTATUS_ITU_HREFF		(1 << 13)
#define FLITE_REG_CISTATUS_OVFIY		(1 << 10)
#define FLITE_REG_CISTATUS_OVFICB		(1 << 9)
#define FLITE_REG_CISTATUS_OVFICR		(1 << 8)
#define FLITE_REG_CISTATUS_IRQ_SRC_OVERFLOW	(1 << 7)
#define FLITE_REG_CISTATUS_IRQ_SRC_LASTCAPEND	(1 << 6)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMSTART	(1 << 5)
#define FLITE_REG_CISTATUS_IRQ_SRC_FRMEND	(1 << 4)
#define FLITE_REG_CISTATUS_IRQ_CAM		(1 << 0)
#define FLITE_REG_CISTATUS_IRQ_MASK		(0xf << 4)

/* Camera Status2 */
#define FLITE_REG_CISTATUS2			0x44
#define FLITE_REG_CISTATUS2_LASTCAPEND		(1 << 1)
#define FLITE_REG_CISTATUS2_FRMEND		(1 << 0)

/* Qos Threshold */
#define FLITE_REG_CITHOLD			0xf0
#define FLITE_REG_CITHOLD_W_QOS_EN		(1 << 30)

/* Camera General Purpose */
#define FLITE_REG_CIGENERAL			0xfc
/* b0: 1 - camera B, 0 - camera A */
#define FLITE_REG_CIGENERAL_CAM_B		(1 << 0)

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
			     struct s5p_fimc_isp_info *s_info);
void flite_hw_set_camera_polarity(struct fimc_lite *dev,
				  struct s5p_fimc_isp_info *cam);
void flite_hw_set_window_offset(struct fimc_lite *dev, struct flite_frame *f);
void flite_hw_set_source_format(struct fimc_lite *dev, struct flite_frame *f);

void flite_hw_set_output_dma(struct fimc_lite *dev, struct flite_frame *f,
			     bool enable);
void flite_hw_set_dma_window(struct fimc_lite *dev, struct flite_frame *f);
void flite_hw_set_test_pattern(struct fimc_lite *dev, bool on);
void flite_hw_dump_regs(struct fimc_lite *dev, const char *label);

static inline void flite_hw_set_output_addr(struct fimc_lite *dev, u32 paddr)
{
	writel(paddr, dev->regs + FLITE_REG_CIOSA);
}
#endif /* FIMC_LITE_REG_H */
