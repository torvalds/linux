// SPDX-License-Identifier: GPL-2.0-or-later
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

#include <linux/io.h>

#include "dma2d.h"
#include "dma2d-regs.h"

static inline u32 reg_read(void __iomem *base, u32 reg)
{
	return readl_relaxed(base + reg);
}

static inline void reg_write(void __iomem *base, u32 reg, u32 val)
{
	writel_relaxed(val, base + reg);
}

static inline void reg_set(void __iomem *base, u32 reg, u32 mask)
{
	reg_write(base, reg, reg_read(base, reg) | mask);
}

static inline void reg_clear(void __iomem *base, u32 reg, u32 mask)
{
	reg_write(base, reg, reg_read(base, reg) & ~mask);
}

static inline void reg_update_bits(void __iomem *base, u32 reg, u32 mask,
				   u32 val)
{
	reg_write(base, reg, (reg_read(base, reg) & ~mask) | val);
}

void dma2d_start(struct dma2d_dev *d)
{
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_START, CR_START);
}

u32 dma2d_get_int(struct dma2d_dev *d)
{
	return reg_read(d->regs, DMA2D_ISR_REG);
}

void dma2d_clear_int(struct dma2d_dev *d)
{
	u32 isr_val = reg_read(d->regs, DMA2D_ISR_REG);

	reg_write(d->regs, DMA2D_IFCR_REG, isr_val & 0x003f);
}

void dma2d_config_common(struct dma2d_dev *d, enum dma2d_op_mode op_mode,
			 u16 width, u16 height)
{
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_MODE_MASK,
			op_mode << CR_MODE_SHIFT);

	reg_write(d->regs, DMA2D_NLR_REG, (width << 16) | height);
}

void dma2d_config_out(struct dma2d_dev *d, struct dma2d_frame *frm,
		      dma_addr_t o_addr)
{
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_CEIE, CR_CEIE);
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_CTCIE, CR_CTCIE);
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_CAEIE, CR_CAEIE);
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_TCIE, CR_TCIE);
	reg_update_bits(d->regs, DMA2D_CR_REG, CR_TEIE, CR_TEIE);

	if (frm->fmt->cmode >= CM_MODE_ARGB8888 &&
	    frm->fmt->cmode <= CM_MODE_ARGB4444)
		reg_update_bits(d->regs, DMA2D_OPFCCR_REG, OPFCCR_CM_MASK,
				frm->fmt->cmode);

	reg_write(d->regs, DMA2D_OMAR_REG, o_addr);

	reg_write(d->regs, DMA2D_OCOLR_REG,
		  (frm->a_rgb[3] << 24) |
		  (frm->a_rgb[2] << 16) |
		  (frm->a_rgb[1] << 8) |
		  frm->a_rgb[0]);

	reg_update_bits(d->regs, DMA2D_OOR_REG, OOR_LO_MASK,
			frm->line_offset & 0x3fff);
}

void dma2d_config_fg(struct dma2d_dev *d, struct dma2d_frame *frm,
		     dma_addr_t f_addr)
{
	reg_write(d->regs, DMA2D_FGMAR_REG, f_addr);
	reg_update_bits(d->regs, DMA2D_FGOR_REG, FGOR_LO_MASK,
			frm->line_offset);

	if (frm->fmt->cmode >= CM_MODE_ARGB8888 &&
	    frm->fmt->cmode <= CM_MODE_A4)
		reg_update_bits(d->regs, DMA2D_FGPFCCR_REG, FGPFCCR_CM_MASK,
				frm->fmt->cmode);

	reg_update_bits(d->regs, DMA2D_FGPFCCR_REG, FGPFCCR_AM_MASK,
			(frm->a_mode << 16) & 0x03);

	reg_update_bits(d->regs, DMA2D_FGPFCCR_REG, FGPFCCR_ALPHA_MASK,
			frm->a_rgb[3] << 24);

	reg_write(d->regs, DMA2D_FGCOLR_REG,
		  (frm->a_rgb[2] << 16) |
		  (frm->a_rgb[1] << 8) |
		  frm->a_rgb[0]);
}

void dma2d_config_bg(struct dma2d_dev *d, struct dma2d_frame *frm,
		     dma_addr_t b_addr)
{
	reg_write(d->regs, DMA2D_BGMAR_REG, b_addr);
	reg_update_bits(d->regs, DMA2D_BGOR_REG, BGOR_LO_MASK,
			frm->line_offset);

	if (frm->fmt->cmode >= CM_MODE_ARGB8888 &&
	    frm->fmt->cmode <= CM_MODE_A4)
		reg_update_bits(d->regs, DMA2D_BGPFCCR_REG, BGPFCCR_CM_MASK,
				frm->fmt->cmode);

	reg_update_bits(d->regs, DMA2D_BGPFCCR_REG, BGPFCCR_AM_MASK,
			(frm->a_mode << 16) & 0x03);

	reg_update_bits(d->regs, DMA2D_BGPFCCR_REG, BGPFCCR_ALPHA_MASK,
			frm->a_rgb[3] << 24);

	reg_write(d->regs, DMA2D_BGCOLR_REG,
		  (frm->a_rgb[2] << 16) |
		  (frm->a_rgb[1] << 8) |
		  frm->a_rgb[0]);
}
