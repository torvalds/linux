/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ST stm32 Chrom-Art - 2D Graphics Accelerator Driver
 *
 * Copyright (c) 2021 Dillon Min
 * Dillon Min, <dillon.minfei@gmail.com>
 *
 * based on s5p-g2d
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 */

#ifndef __DMA2D_REGS_H__
#define __DMA2D_REGS_H__

#define DMA2D_CR_REG		0x0000
#define CR_MODE_MASK		GENMASK(17, 16)
#define CR_MODE_SHIFT		16
#define CR_M2M			0x0000
#define CR_M2M_PFC		BIT(16)
#define CR_M2M_BLEND		BIT(17)
#define CR_R2M			(BIT(17) | BIT(16))
#define CR_CEIE			BIT(13)
#define CR_CTCIE		BIT(12)
#define CR_CAEIE		BIT(11)
#define CR_TWIE			BIT(10)
#define CR_TCIE			BIT(9)
#define CR_TEIE			BIT(8)
#define CR_ABORT		BIT(2)
#define CR_SUSP			BIT(1)
#define CR_START		BIT(0)

#define DMA2D_ISR_REG		0x0004
#define ISR_CEIF		BIT(5)
#define ISR_CTCIF		BIT(4)
#define ISR_CAEIF		BIT(3)
#define ISR_TWIF		BIT(2)
#define ISR_TCIF		BIT(1)
#define ISR_TEIF		BIT(0)

#define DMA2D_IFCR_REG		0x0008
#define IFCR_CCEIF		BIT(5)
#define IFCR_CCTCIF		BIT(4)
#define IFCR_CAECIF		BIT(3)
#define IFCR_CTWIF		BIT(2)
#define IFCR_CTCIF		BIT(1)
#define IFCR_CTEIF		BIT(0)

#define DMA2D_FGMAR_REG		0x000c
#define DMA2D_FGOR_REG		0x0010
#define FGOR_LO_MASK		GENMASK(13, 0)

#define DMA2D_BGMAR_REG		0x0014
#define DMA2D_BGOR_REG		0x0018
#define BGOR_LO_MASK		GENMASK(13, 0)

#define DMA2D_FGPFCCR_REG	0x001c
#define FGPFCCR_ALPHA_MASK	GENMASK(31, 24)
#define FGPFCCR_AM_MASK		GENMASK(17, 16)
#define FGPFCCR_CS_MASK		GENMASK(15, 8)
#define FGPFCCR_START		BIT(5)
#define FGPFCCR_CCM_RGB888	BIT(4)
#define FGPFCCR_CM_MASK		GENMASK(3, 0)

#define DMA2D_FGCOLR_REG	0x0020
#define FGCOLR_REG_MASK		GENMASK(23, 16)
#define FGCOLR_GREEN_MASK	GENMASK(15, 8)
#define FGCOLR_BLUE_MASK	GENMASK(7, 0)

#define DMA2D_BGPFCCR_REG	0x0024
#define BGPFCCR_ALPHA_MASK	GENMASK(31, 24)
#define BGPFCCR_AM_MASK		GENMASK(17, 16)
#define BGPFCCR_CS_MASK		GENMASK(15, 8)
#define BGPFCCR_START		BIT(5)
#define BGPFCCR_CCM_RGB888	BIT(4)
#define BGPFCCR_CM_MASK		GENMASK(3, 0)

#define DMA2D_BGCOLR_REG	0x0028
#define BGCOLR_REG_MASK		GENMASK(23, 16)
#define BGCOLR_GREEN_MASK	GENMASK(15, 8)
#define BGCOLR_BLUE_MASK	GENMASK(7, 0)

#define DMA2D_OPFCCR_REG	0x0034
#define OPFCCR_CM_MASK		GENMASK(2, 0)

#define DMA2D_OCOLR_REG		0x0038
#define OCOLR_ALPHA_MASK	GENMASK(31, 24)
#define OCOLR_RED_MASK		GENMASK(23, 16)
#define OCOLR_GREEN_MASK	GENMASK(15, 8)
#define OCOLR_BLUE_MASK		GENMASK(7, 0)

#define DMA2D_OMAR_REG		0x003c

#define DMA2D_OOR_REG		0x0040
#define OOR_LO_MASK		GENMASK(13, 0)

#define DMA2D_NLR_REG		0x0044
#define NLR_PL_MASK		GENMASK(29, 16)
#define NLR_NL_MASK		GENMASK(15, 0)

/* Hardware limits */
#define MAX_WIDTH		2592
#define MAX_HEIGHT		2592

#define DEFAULT_WIDTH		240
#define DEFAULT_HEIGHT		320
#define DEFAULT_SIZE		307200

#define CM_MODE_ARGB8888	0x00
#define CM_MODE_ARGB4444	0x04
#define CM_MODE_A4		0x0a
#endif /* __DMA2D_REGS_H__ */
