// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Samsung S5P G2D - 2D Graphics Accelerator Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 */

#include <linux/io.h>

#include "g2d.h"
#include "g2d-regs.h"

#define w(x, a)	writel((x), d->regs + (a))
#define r(a)	readl(d->regs + (a))

/* g2d_reset clears all g2d registers */
void g2d_reset(struct g2d_dev *d)
{
	w(1, SOFT_RESET_REG);
}

void g2d_set_src_size(struct g2d_dev *d, struct g2d_frame *f)
{
	u32 n;

	w(0, SRC_SELECT_REG);
	w(f->stride & 0xFFFF, SRC_STRIDE_REG);

	n = f->o_height & 0xFFF;
	n <<= 16;
	n |= f->o_width & 0xFFF;
	w(n, SRC_LEFT_TOP_REG);

	n = f->bottom & 0xFFF;
	n <<= 16;
	n |= f->right & 0xFFF;
	w(n, SRC_RIGHT_BOTTOM_REG);

	w(f->fmt->hw, SRC_COLOR_MODE_REG);
}

void g2d_set_src_addr(struct g2d_dev *d, dma_addr_t a)
{
	w(a, SRC_BASE_ADDR_REG);
}

void g2d_set_dst_size(struct g2d_dev *d, struct g2d_frame *f)
{
	u32 n;

	w(0, DST_SELECT_REG);
	w(f->stride & 0xFFFF, DST_STRIDE_REG);

	n = f->o_height & 0xFFF;
	n <<= 16;
	n |= f->o_width & 0xFFF;
	w(n, DST_LEFT_TOP_REG);

	n = f->bottom & 0xFFF;
	n <<= 16;
	n |= f->right & 0xFFF;
	w(n, DST_RIGHT_BOTTOM_REG);

	w(f->fmt->hw, DST_COLOR_MODE_REG);
}

void g2d_set_dst_addr(struct g2d_dev *d, dma_addr_t a)
{
	w(a, DST_BASE_ADDR_REG);
}

void g2d_set_rop4(struct g2d_dev *d, u32 r)
{
	w(r, ROP4_REG);
}

void g2d_set_flip(struct g2d_dev *d, u32 r)
{
	w(r, SRC_MSK_DIRECT_REG);
}

void g2d_set_v41_stretch(struct g2d_dev *d, struct g2d_frame *src,
					struct g2d_frame *dst)
{
	w(DEFAULT_SCALE_MODE, SRC_SCALE_CTRL_REG);

	/* inversed scaling factor: src is numerator */
	w((src->c_width << 16) / dst->c_width, SRC_XSCALE_REG);
	w((src->c_height << 16) / dst->c_height, SRC_YSCALE_REG);
}

void g2d_set_cmd(struct g2d_dev *d, u32 c)
{
	w(c, BITBLT_COMMAND_REG);
}

void g2d_start(struct g2d_dev *d)
{
	/* Clear cache */
	if (d->variant->hw_rev == TYPE_G2D_3X)
		w(0x7, CACHECTL_REG);

	/* Enable interrupt */
	w(1, INTEN_REG);
	/* Start G2D engine */
	w(1, BITBLT_START_REG);
}

void g2d_clear_int(struct g2d_dev *d)
{
	w(1, INTC_PEND_REG);
}
