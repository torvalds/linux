/* mga_state.c -- State support for MGA G200/G400 -*- linux-c -*-
 * Created: Thu Jan 27 02:53:43 2000 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Jeff Hartmann <jhartmann@valinux.com>
 *    Keith Whitwell <keith@tungstengraphics.com>
 *
 * Rewritten by:
 *    Gareth Hughes <gareth@valinux.com>
 */

#include "drmP.h"
#include "drm.h"
#include "mga_drm.h"
#include "mga_drv.h"

/* ================================================================
 * DMA hardware state programming functions
 */

static void mga_emit_clip_rect(drm_mga_private_t * dev_priv,
			       struct drm_clip_rect * box)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	unsigned int pitch = dev_priv->front_pitch;
	DMA_LOCALS;

	BEGIN_DMA(2);

	/* Force reset of DWGCTL on G400 (eliminates clip disable bit).
	 */
	if (dev_priv->chipset >= MGA_CARD_TYPE_G400) {
		DMA_BLOCK(MGA_DWGCTL, ctx->dwgctl,
			  MGA_LEN + MGA_EXEC, 0x80000000,
			  MGA_DWGCTL, ctx->dwgctl,
			  MGA_LEN + MGA_EXEC, 0x80000000);
	}
	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_CXBNDRY, ((box->x2 - 1) << 16) | box->x1,
		  MGA_YTOP, box->y1 * pitch, MGA_YBOT, (box->y2 - 1) * pitch);

	ADVANCE_DMA();
}

static __inline__ void mga_g200_emit_context(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	DMA_LOCALS;

	BEGIN_DMA(3);

	DMA_BLOCK(MGA_DSTORG, ctx->dstorg,
		  MGA_MACCESS, ctx->maccess,
		  MGA_PLNWT, ctx->plnwt, MGA_DWGCTL, ctx->dwgctl);

	DMA_BLOCK(MGA_ALPHACTRL, ctx->alphactrl,
		  MGA_FOGCOL, ctx->fogcolor,
		  MGA_WFLAG, ctx->wflag, MGA_ZORG, dev_priv->depth_offset);

	DMA_BLOCK(MGA_FCOL, ctx->fcol,
		  MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000, MGA_DMAPAD, 0x00000000);

	ADVANCE_DMA();
}

static __inline__ void mga_g400_emit_context(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	DMA_LOCALS;

	BEGIN_DMA(4);

	DMA_BLOCK(MGA_DSTORG, ctx->dstorg,
		  MGA_MACCESS, ctx->maccess,
		  MGA_PLNWT, ctx->plnwt, MGA_DWGCTL, ctx->dwgctl);

	DMA_BLOCK(MGA_ALPHACTRL, ctx->alphactrl,
		  MGA_FOGCOL, ctx->fogcolor,
		  MGA_WFLAG, ctx->wflag, MGA_ZORG, dev_priv->depth_offset);

	DMA_BLOCK(MGA_WFLAG1, ctx->wflag,
		  MGA_TDUALSTAGE0, ctx->tdualstage0,
		  MGA_TDUALSTAGE1, ctx->tdualstage1, MGA_FCOL, ctx->fcol);

	DMA_BLOCK(MGA_STENCIL, ctx->stencil,
		  MGA_STENCILCTL, ctx->stencilctl,
		  MGA_DMAPAD, 0x00000000, MGA_DMAPAD, 0x00000000);

	ADVANCE_DMA();
}

static __inline__ void mga_g200_emit_tex0(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_texture_regs_t *tex = &sarea_priv->tex_state[0];
	DMA_LOCALS;

	BEGIN_DMA(4);

	DMA_BLOCK(MGA_TEXCTL2, tex->texctl2,
		  MGA_TEXCTL, tex->texctl,
		  MGA_TEXFILTER, tex->texfilter,
		  MGA_TEXBORDERCOL, tex->texbordercol);

	DMA_BLOCK(MGA_TEXORG, tex->texorg,
		  MGA_TEXORG1, tex->texorg1,
		  MGA_TEXORG2, tex->texorg2, MGA_TEXORG3, tex->texorg3);

	DMA_BLOCK(MGA_TEXORG4, tex->texorg4,
		  MGA_TEXWIDTH, tex->texwidth,
		  MGA_TEXHEIGHT, tex->texheight, MGA_WR24, tex->texwidth);

	DMA_BLOCK(MGA_WR34, tex->texheight,
		  MGA_TEXTRANS, 0x0000ffff,
		  MGA_TEXTRANSHIGH, 0x0000ffff, MGA_DMAPAD, 0x00000000);

	ADVANCE_DMA();
}

static __inline__ void mga_g400_emit_tex0(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_texture_regs_t *tex = &sarea_priv->tex_state[0];
	DMA_LOCALS;

/*	printk("mga_g400_emit_tex0 %x %x %x\n", tex->texorg, */
/*	       tex->texctl, tex->texctl2); */

	BEGIN_DMA(6);

	DMA_BLOCK(MGA_TEXCTL2, tex->texctl2 | MGA_G400_TC2_MAGIC,
		  MGA_TEXCTL, tex->texctl,
		  MGA_TEXFILTER, tex->texfilter,
		  MGA_TEXBORDERCOL, tex->texbordercol);

	DMA_BLOCK(MGA_TEXORG, tex->texorg,
		  MGA_TEXORG1, tex->texorg1,
		  MGA_TEXORG2, tex->texorg2, MGA_TEXORG3, tex->texorg3);

	DMA_BLOCK(MGA_TEXORG4, tex->texorg4,
		  MGA_TEXWIDTH, tex->texwidth,
		  MGA_TEXHEIGHT, tex->texheight, MGA_WR49, 0x00000000);

	DMA_BLOCK(MGA_WR57, 0x00000000,
		  MGA_WR53, 0x00000000,
		  MGA_WR61, 0x00000000, MGA_WR52, MGA_G400_WR_MAGIC);

	DMA_BLOCK(MGA_WR60, MGA_G400_WR_MAGIC,
		  MGA_WR54, tex->texwidth | MGA_G400_WR_MAGIC,
		  MGA_WR62, tex->texheight | MGA_G400_WR_MAGIC,
		  MGA_DMAPAD, 0x00000000);

	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_TEXTRANS, 0x0000ffff, MGA_TEXTRANSHIGH, 0x0000ffff);

	ADVANCE_DMA();
}

static __inline__ void mga_g400_emit_tex1(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_texture_regs_t *tex = &sarea_priv->tex_state[1];
	DMA_LOCALS;

/*	printk("mga_g400_emit_tex1 %x %x %x\n", tex->texorg,  */
/*	       tex->texctl, tex->texctl2); */

	BEGIN_DMA(5);

	DMA_BLOCK(MGA_TEXCTL2, (tex->texctl2 |
				MGA_MAP1_ENABLE |
				MGA_G400_TC2_MAGIC),
		  MGA_TEXCTL, tex->texctl,
		  MGA_TEXFILTER, tex->texfilter,
		  MGA_TEXBORDERCOL, tex->texbordercol);

	DMA_BLOCK(MGA_TEXORG, tex->texorg,
		  MGA_TEXORG1, tex->texorg1,
		  MGA_TEXORG2, tex->texorg2, MGA_TEXORG3, tex->texorg3);

	DMA_BLOCK(MGA_TEXORG4, tex->texorg4,
		  MGA_TEXWIDTH, tex->texwidth,
		  MGA_TEXHEIGHT, tex->texheight, MGA_WR49, 0x00000000);

	DMA_BLOCK(MGA_WR57, 0x00000000,
		  MGA_WR53, 0x00000000,
		  MGA_WR61, 0x00000000,
		  MGA_WR52, tex->texwidth | MGA_G400_WR_MAGIC);

	DMA_BLOCK(MGA_WR60, tex->texheight | MGA_G400_WR_MAGIC,
		  MGA_TEXTRANS, 0x0000ffff,
		  MGA_TEXTRANSHIGH, 0x0000ffff,
		  MGA_TEXCTL2, tex->texctl2 | MGA_G400_TC2_MAGIC);

	ADVANCE_DMA();
}

static __inline__ void mga_g200_emit_pipe(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int pipe = sarea_priv->warp_pipe;
	DMA_LOCALS;

	BEGIN_DMA(3);

	DMA_BLOCK(MGA_WIADDR, MGA_WMODE_SUSPEND,
		  MGA_WVRTXSZ, 0x00000007,
		  MGA_WFLAG, 0x00000000, MGA_WR24, 0x00000000);

	DMA_BLOCK(MGA_WR25, 0x00000100,
		  MGA_WR34, 0x00000000,
		  MGA_WR42, 0x0000ffff, MGA_WR60, 0x0000ffff);

	/* Padding required to to hardware bug.
	 */
	DMA_BLOCK(MGA_DMAPAD, 0xffffffff,
		  MGA_DMAPAD, 0xffffffff,
		  MGA_DMAPAD, 0xffffffff,
		  MGA_WIADDR, (dev_priv->warp_pipe_phys[pipe] |
			       MGA_WMODE_START | dev_priv->wagp_enable));

	ADVANCE_DMA();
}

static __inline__ void mga_g400_emit_pipe(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int pipe = sarea_priv->warp_pipe;
	DMA_LOCALS;

/*	printk("mga_g400_emit_pipe %x\n", pipe); */

	BEGIN_DMA(10);

	DMA_BLOCK(MGA_WIADDR2, MGA_WMODE_SUSPEND,
		  MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000, MGA_DMAPAD, 0x00000000);

	if (pipe & MGA_T2) {
		DMA_BLOCK(MGA_WVRTXSZ, 0x00001e09,
			  MGA_DMAPAD, 0x00000000,
			  MGA_DMAPAD, 0x00000000, MGA_DMAPAD, 0x00000000);

		DMA_BLOCK(MGA_WACCEPTSEQ, 0x00000000,
			  MGA_WACCEPTSEQ, 0x00000000,
			  MGA_WACCEPTSEQ, 0x00000000,
			  MGA_WACCEPTSEQ, 0x1e000000);
	} else {
		if (dev_priv->warp_pipe & MGA_T2) {
			/* Flush the WARP pipe */
			DMA_BLOCK(MGA_YDST, 0x00000000,
				  MGA_FXLEFT, 0x00000000,
				  MGA_FXRIGHT, 0x00000001,
				  MGA_DWGCTL, MGA_DWGCTL_FLUSH);

			DMA_BLOCK(MGA_LEN + MGA_EXEC, 0x00000001,
				  MGA_DWGSYNC, 0x00007000,
				  MGA_TEXCTL2, MGA_G400_TC2_MAGIC,
				  MGA_LEN + MGA_EXEC, 0x00000000);

			DMA_BLOCK(MGA_TEXCTL2, (MGA_DUALTEX |
						MGA_G400_TC2_MAGIC),
				  MGA_LEN + MGA_EXEC, 0x00000000,
				  MGA_TEXCTL2, MGA_G400_TC2_MAGIC,
				  MGA_DMAPAD, 0x00000000);
		}

		DMA_BLOCK(MGA_WVRTXSZ, 0x00001807,
			  MGA_DMAPAD, 0x00000000,
			  MGA_DMAPAD, 0x00000000, MGA_DMAPAD, 0x00000000);

		DMA_BLOCK(MGA_WACCEPTSEQ, 0x00000000,
			  MGA_WACCEPTSEQ, 0x00000000,
			  MGA_WACCEPTSEQ, 0x00000000,
			  MGA_WACCEPTSEQ, 0x18000000);
	}

	DMA_BLOCK(MGA_WFLAG, 0x00000000,
		  MGA_WFLAG1, 0x00000000,
		  MGA_WR56, MGA_G400_WR56_MAGIC, MGA_DMAPAD, 0x00000000);

	DMA_BLOCK(MGA_WR49, 0x00000000,	/* tex0              */
		  MGA_WR57, 0x00000000,	/* tex0              */
		  MGA_WR53, 0x00000000,	/* tex1              */
		  MGA_WR61, 0x00000000);	/* tex1              */

	DMA_BLOCK(MGA_WR54, MGA_G400_WR_MAGIC,	/* tex0 width        */
		  MGA_WR62, MGA_G400_WR_MAGIC,	/* tex0 height       */
		  MGA_WR52, MGA_G400_WR_MAGIC,	/* tex1 width        */
		  MGA_WR60, MGA_G400_WR_MAGIC);	/* tex1 height       */

	/* Padding required to to hardware bug */
	DMA_BLOCK(MGA_DMAPAD, 0xffffffff,
		  MGA_DMAPAD, 0xffffffff,
		  MGA_DMAPAD, 0xffffffff,
		  MGA_WIADDR2, (dev_priv->warp_pipe_phys[pipe] |
				MGA_WMODE_START | dev_priv->wagp_enable));

	ADVANCE_DMA();
}

static void mga_g200_emit_state(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;

	if (sarea_priv->warp_pipe != dev_priv->warp_pipe) {
		mga_g200_emit_pipe(dev_priv);
		dev_priv->warp_pipe = sarea_priv->warp_pipe;
	}

	if (dirty & MGA_UPLOAD_CONTEXT) {
		mga_g200_emit_context(dev_priv);
		sarea_priv->dirty &= ~MGA_UPLOAD_CONTEXT;
	}

	if (dirty & MGA_UPLOAD_TEX0) {
		mga_g200_emit_tex0(dev_priv);
		sarea_priv->dirty &= ~MGA_UPLOAD_TEX0;
	}
}

static void mga_g400_emit_state(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;
	int multitex = sarea_priv->warp_pipe & MGA_T2;

	if (sarea_priv->warp_pipe != dev_priv->warp_pipe) {
		mga_g400_emit_pipe(dev_priv);
		dev_priv->warp_pipe = sarea_priv->warp_pipe;
	}

	if (dirty & MGA_UPLOAD_CONTEXT) {
		mga_g400_emit_context(dev_priv);
		sarea_priv->dirty &= ~MGA_UPLOAD_CONTEXT;
	}

	if (dirty & MGA_UPLOAD_TEX0) {
		mga_g400_emit_tex0(dev_priv);
		sarea_priv->dirty &= ~MGA_UPLOAD_TEX0;
	}

	if ((dirty & MGA_UPLOAD_TEX1) && multitex) {
		mga_g400_emit_tex1(dev_priv);
		sarea_priv->dirty &= ~MGA_UPLOAD_TEX1;
	}
}

/* ================================================================
 * SAREA state verification
 */

/* Disallow all write destinations except the front and backbuffer.
 */
static int mga_verify_context(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;

	if (ctx->dstorg != dev_priv->front_offset &&
	    ctx->dstorg != dev_priv->back_offset) {
		DRM_ERROR("*** bad DSTORG: %x (front %x, back %x)\n\n",
			  ctx->dstorg, dev_priv->front_offset,
			  dev_priv->back_offset);
		ctx->dstorg = 0;
		return -EINVAL;
	}

	return 0;
}

/* Disallow texture reads from PCI space.
 */
static int mga_verify_tex(drm_mga_private_t * dev_priv, int unit)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_texture_regs_t *tex = &sarea_priv->tex_state[unit];
	unsigned int org;

	org = tex->texorg & (MGA_TEXORGMAP_MASK | MGA_TEXORGACC_MASK);

	if (org == (MGA_TEXORGMAP_SYSMEM | MGA_TEXORGACC_PCI)) {
		DRM_ERROR("*** bad TEXORG: 0x%x, unit %d\n", tex->texorg, unit);
		tex->texorg = 0;
		return -EINVAL;
	}

	return 0;
}

static int mga_verify_state(drm_mga_private_t * dev_priv)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;
	int ret = 0;

	if (sarea_priv->nbox > MGA_NR_SAREA_CLIPRECTS)
		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	if (dirty & MGA_UPLOAD_CONTEXT)
		ret |= mga_verify_context(dev_priv);

	if (dirty & MGA_UPLOAD_TEX0)
		ret |= mga_verify_tex(dev_priv, 0);

	if (dev_priv->chipset >= MGA_CARD_TYPE_G400) {
		if (dirty & MGA_UPLOAD_TEX1)
			ret |= mga_verify_tex(dev_priv, 1);

		if (dirty & MGA_UPLOAD_PIPE)
			ret |= (sarea_priv->warp_pipe > MGA_MAX_G400_PIPES);
	} else {
		if (dirty & MGA_UPLOAD_PIPE)
			ret |= (sarea_priv->warp_pipe > MGA_MAX_G200_PIPES);
	}

	return (ret == 0);
}

static int mga_verify_iload(drm_mga_private_t * dev_priv,
			    unsigned int dstorg, unsigned int length)
{
	if (dstorg < dev_priv->texture_offset ||
	    dstorg + length > (dev_priv->texture_offset +
			       dev_priv->texture_size)) {
		DRM_ERROR("*** bad iload DSTORG: 0x%x\n", dstorg);
		return -EINVAL;
	}

	if (length & MGA_ILOAD_MASK) {
		DRM_ERROR("*** bad iload length: 0x%x\n",
			  length & MGA_ILOAD_MASK);
		return -EINVAL;
	}

	return 0;
}

static int mga_verify_blit(drm_mga_private_t * dev_priv,
			   unsigned int srcorg, unsigned int dstorg)
{
	if ((srcorg & 0x3) == (MGA_SRCACC_PCI | MGA_SRCMAP_SYSMEM) ||
	    (dstorg & 0x3) == (MGA_SRCACC_PCI | MGA_SRCMAP_SYSMEM)) {
		DRM_ERROR("*** bad blit: src=0x%x dst=0x%x\n", srcorg, dstorg);
		return -EINVAL;
	}
	return 0;
}

/* ================================================================
 *
 */

static void mga_dma_dispatch_clear(struct drm_device * dev, drm_mga_clear_t * clear)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	struct drm_clip_rect *pbox = sarea_priv->boxes;
	int nbox = sarea_priv->nbox;
	int i;
	DMA_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_DMA(1);

	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_DWGSYNC, 0x00007100, MGA_DWGSYNC, 0x00007000);

	ADVANCE_DMA();

	for (i = 0; i < nbox; i++) {
		struct drm_clip_rect *box = &pbox[i];
		u32 height = box->y2 - box->y1;

		DRM_DEBUG("   from=%d,%d to=%d,%d\n",
			  box->x1, box->y1, box->x2, box->y2);

		if (clear->flags & MGA_FRONT) {
			BEGIN_DMA(2);

			DMA_BLOCK(MGA_DMAPAD, 0x00000000,
				  MGA_PLNWT, clear->color_mask,
				  MGA_YDSTLEN, (box->y1 << 16) | height,
				  MGA_FXBNDRY, (box->x2 << 16) | box->x1);

			DMA_BLOCK(MGA_DMAPAD, 0x00000000,
				  MGA_FCOL, clear->clear_color,
				  MGA_DSTORG, dev_priv->front_offset,
				  MGA_DWGCTL + MGA_EXEC, dev_priv->clear_cmd);

			ADVANCE_DMA();
		}

		if (clear->flags & MGA_BACK) {
			BEGIN_DMA(2);

			DMA_BLOCK(MGA_DMAPAD, 0x00000000,
				  MGA_PLNWT, clear->color_mask,
				  MGA_YDSTLEN, (box->y1 << 16) | height,
				  MGA_FXBNDRY, (box->x2 << 16) | box->x1);

			DMA_BLOCK(MGA_DMAPAD, 0x00000000,
				  MGA_FCOL, clear->clear_color,
				  MGA_DSTORG, dev_priv->back_offset,
				  MGA_DWGCTL + MGA_EXEC, dev_priv->clear_cmd);

			ADVANCE_DMA();
		}

		if (clear->flags & MGA_DEPTH) {
			BEGIN_DMA(2);

			DMA_BLOCK(MGA_DMAPAD, 0x00000000,
				  MGA_PLNWT, clear->depth_mask,
				  MGA_YDSTLEN, (box->y1 << 16) | height,
				  MGA_FXBNDRY, (box->x2 << 16) | box->x1);

			DMA_BLOCK(MGA_DMAPAD, 0x00000000,
				  MGA_FCOL, clear->clear_depth,
				  MGA_DSTORG, dev_priv->depth_offset,
				  MGA_DWGCTL + MGA_EXEC, dev_priv->clear_cmd);

			ADVANCE_DMA();
		}

	}

	BEGIN_DMA(1);

	/* Force reset of DWGCTL */
	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_PLNWT, ctx->plnwt, MGA_DWGCTL, ctx->dwgctl);

	ADVANCE_DMA();

	FLUSH_DMA();
}

static void mga_dma_dispatch_swap(struct drm_device * dev)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	struct drm_clip_rect *pbox = sarea_priv->boxes;
	int nbox = sarea_priv->nbox;
	int i;
	DMA_LOCALS;
	DRM_DEBUG("\n");

	sarea_priv->last_frame.head = dev_priv->prim.tail;
	sarea_priv->last_frame.wrap = dev_priv->prim.last_wrap;

	BEGIN_DMA(4 + nbox);

	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_DWGSYNC, 0x00007100, MGA_DWGSYNC, 0x00007000);

	DMA_BLOCK(MGA_DSTORG, dev_priv->front_offset,
		  MGA_MACCESS, dev_priv->maccess,
		  MGA_SRCORG, dev_priv->back_offset,
		  MGA_AR5, dev_priv->front_pitch);

	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_PLNWT, 0xffffffff, MGA_DWGCTL, MGA_DWGCTL_COPY);

	for (i = 0; i < nbox; i++) {
		struct drm_clip_rect *box = &pbox[i];
		u32 height = box->y2 - box->y1;
		u32 start = box->y1 * dev_priv->front_pitch;

		DRM_DEBUG("   from=%d,%d to=%d,%d\n",
			  box->x1, box->y1, box->x2, box->y2);

		DMA_BLOCK(MGA_AR0, start + box->x2 - 1,
			  MGA_AR3, start + box->x1,
			  MGA_FXBNDRY, ((box->x2 - 1) << 16) | box->x1,
			  MGA_YDSTLEN + MGA_EXEC, (box->y1 << 16) | height);
	}

	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_PLNWT, ctx->plnwt,
		  MGA_SRCORG, dev_priv->front_offset, MGA_DWGCTL, ctx->dwgctl);

	ADVANCE_DMA();

	FLUSH_DMA();

	DRM_DEBUG("... done.\n");
}

static void mga_dma_dispatch_vertex(struct drm_device * dev, struct drm_buf * buf)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	u32 address = (u32) buf->bus_address;
	u32 length = (u32) buf->used;
	int i = 0;
	DMA_LOCALS;
	DRM_DEBUG("buf=%d used=%d\n", buf->idx, buf->used);

	if (buf->used) {
		buf_priv->dispatched = 1;

		MGA_EMIT_STATE(dev_priv, sarea_priv->dirty);

		do {
			if (i < sarea_priv->nbox) {
				mga_emit_clip_rect(dev_priv,
						   &sarea_priv->boxes[i]);
			}

			BEGIN_DMA(1);

			DMA_BLOCK(MGA_DMAPAD, 0x00000000,
				  MGA_DMAPAD, 0x00000000,
				  MGA_SECADDRESS, (address |
						   MGA_DMA_VERTEX),
				  MGA_SECEND, ((address + length) |
					       dev_priv->dma_access));

			ADVANCE_DMA();
		} while (++i < sarea_priv->nbox);
	}

	if (buf_priv->discard) {
		AGE_BUFFER(buf_priv);
		buf->pending = 0;
		buf->used = 0;
		buf_priv->dispatched = 0;

		mga_freelist_put(dev, buf);
	}

	FLUSH_DMA();
}

static void mga_dma_dispatch_indices(struct drm_device * dev, struct drm_buf * buf,
				     unsigned int start, unsigned int end)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	u32 address = (u32) buf->bus_address;
	int i = 0;
	DMA_LOCALS;
	DRM_DEBUG("buf=%d start=%d end=%d\n", buf->idx, start, end);

	if (start != end) {
		buf_priv->dispatched = 1;

		MGA_EMIT_STATE(dev_priv, sarea_priv->dirty);

		do {
			if (i < sarea_priv->nbox) {
				mga_emit_clip_rect(dev_priv,
						   &sarea_priv->boxes[i]);
			}

			BEGIN_DMA(1);

			DMA_BLOCK(MGA_DMAPAD, 0x00000000,
				  MGA_DMAPAD, 0x00000000,
				  MGA_SETUPADDRESS, address + start,
				  MGA_SETUPEND, ((address + end) |
						 dev_priv->dma_access));

			ADVANCE_DMA();
		} while (++i < sarea_priv->nbox);
	}

	if (buf_priv->discard) {
		AGE_BUFFER(buf_priv);
		buf->pending = 0;
		buf->used = 0;
		buf_priv->dispatched = 0;

		mga_freelist_put(dev, buf);
	}

	FLUSH_DMA();
}

/* This copies a 64 byte aligned agp region to the frambuffer with a
 * standard blit, the ioctl needs to do checking.
 */
static void mga_dma_dispatch_iload(struct drm_device * dev, struct drm_buf * buf,
				   unsigned int dstorg, unsigned int length)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	drm_mga_context_regs_t *ctx = &dev_priv->sarea_priv->context_state;
	u32 srcorg =
	    buf->bus_address | dev_priv->dma_access | MGA_SRCMAP_SYSMEM;
	u32 y2;
	DMA_LOCALS;
	DRM_DEBUG("buf=%d used=%d\n", buf->idx, buf->used);

	y2 = length / 64;

	BEGIN_DMA(5);

	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_DWGSYNC, 0x00007100, MGA_DWGSYNC, 0x00007000);

	DMA_BLOCK(MGA_DSTORG, dstorg,
		  MGA_MACCESS, 0x00000000, MGA_SRCORG, srcorg, MGA_AR5, 64);

	DMA_BLOCK(MGA_PITCH, 64,
		  MGA_PLNWT, 0xffffffff,
		  MGA_DMAPAD, 0x00000000, MGA_DWGCTL, MGA_DWGCTL_COPY);

	DMA_BLOCK(MGA_AR0, 63,
		  MGA_AR3, 0,
		  MGA_FXBNDRY, (63 << 16) | 0, MGA_YDSTLEN + MGA_EXEC, y2);

	DMA_BLOCK(MGA_PLNWT, ctx->plnwt,
		  MGA_SRCORG, dev_priv->front_offset,
		  MGA_PITCH, dev_priv->front_pitch, MGA_DWGSYNC, 0x00007000);

	ADVANCE_DMA();

	AGE_BUFFER(buf_priv);

	buf->pending = 0;
	buf->used = 0;
	buf_priv->dispatched = 0;

	mga_freelist_put(dev, buf);

	FLUSH_DMA();
}

static void mga_dma_dispatch_blit(struct drm_device * dev, drm_mga_blit_t * blit)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	struct drm_clip_rect *pbox = sarea_priv->boxes;
	int nbox = sarea_priv->nbox;
	u32 scandir = 0, i;
	DMA_LOCALS;
	DRM_DEBUG("\n");

	BEGIN_DMA(4 + nbox);

	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_DWGSYNC, 0x00007100, MGA_DWGSYNC, 0x00007000);

	DMA_BLOCK(MGA_DWGCTL, MGA_DWGCTL_COPY,
		  MGA_PLNWT, blit->planemask,
		  MGA_SRCORG, blit->srcorg, MGA_DSTORG, blit->dstorg);

	DMA_BLOCK(MGA_SGN, scandir,
		  MGA_MACCESS, dev_priv->maccess,
		  MGA_AR5, blit->ydir * blit->src_pitch,
		  MGA_PITCH, blit->dst_pitch);

	for (i = 0; i < nbox; i++) {
		int srcx = pbox[i].x1 + blit->delta_sx;
		int srcy = pbox[i].y1 + blit->delta_sy;
		int dstx = pbox[i].x1 + blit->delta_dx;
		int dsty = pbox[i].y1 + blit->delta_dy;
		int h = pbox[i].y2 - pbox[i].y1;
		int w = pbox[i].x2 - pbox[i].x1 - 1;
		int start;

		if (blit->ydir == -1) {
			srcy = blit->height - srcy - 1;
		}

		start = srcy * blit->src_pitch + srcx;

		DMA_BLOCK(MGA_AR0, start + w,
			  MGA_AR3, start,
			  MGA_FXBNDRY, ((dstx + w) << 16) | (dstx & 0xffff),
			  MGA_YDSTLEN + MGA_EXEC, (dsty << 16) | h);
	}

	/* Do something to flush AGP?
	 */

	/* Force reset of DWGCTL */
	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_PLNWT, ctx->plnwt,
		  MGA_PITCH, dev_priv->front_pitch, MGA_DWGCTL, ctx->dwgctl);

	ADVANCE_DMA();
}

/* ================================================================
 *
 */

static int mga_dma_clear(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_clear_t *clear = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (sarea_priv->nbox > MGA_NR_SAREA_CLIPRECTS)
		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	WRAP_TEST_WITH_RETURN(dev_priv);

	mga_dma_dispatch_clear(dev, clear);

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CONTEXT;

	return 0;
}

static int mga_dma_swap(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (sarea_priv->nbox > MGA_NR_SAREA_CLIPRECTS)
		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	WRAP_TEST_WITH_RETURN(dev_priv);

	mga_dma_dispatch_swap(dev);

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CONTEXT;

	return 0;
}

static int mga_dma_vertex(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	struct drm_device_dma *dma = dev->dma;
	struct drm_buf *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_vertex_t *vertex = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (vertex->idx < 0 || vertex->idx > dma->buf_count)
		return -EINVAL;
	buf = dma->buflist[vertex->idx];
	buf_priv = buf->dev_private;

	buf->used = vertex->used;
	buf_priv->discard = vertex->discard;

	if (!mga_verify_state(dev_priv)) {
		if (vertex->discard) {
			if (buf_priv->dispatched == 1)
				AGE_BUFFER(buf_priv);
			buf_priv->dispatched = 0;
			mga_freelist_put(dev, buf);
		}
		return -EINVAL;
	}

	WRAP_TEST_WITH_RETURN(dev_priv);

	mga_dma_dispatch_vertex(dev, buf);

	return 0;
}

static int mga_dma_indices(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	struct drm_device_dma *dma = dev->dma;
	struct drm_buf *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_indices_t *indices = data;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (indices->idx < 0 || indices->idx > dma->buf_count)
		return -EINVAL;

	buf = dma->buflist[indices->idx];
	buf_priv = buf->dev_private;

	buf_priv->discard = indices->discard;

	if (!mga_verify_state(dev_priv)) {
		if (indices->discard) {
			if (buf_priv->dispatched == 1)
				AGE_BUFFER(buf_priv);
			buf_priv->dispatched = 0;
			mga_freelist_put(dev, buf);
		}
		return -EINVAL;
	}

	WRAP_TEST_WITH_RETURN(dev_priv);

	mga_dma_dispatch_indices(dev, buf, indices->start, indices->end);

	return 0;
}

static int mga_dma_iload(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_device_dma *dma = dev->dma;
	drm_mga_private_t *dev_priv = dev->dev_private;
	struct drm_buf *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_iload_t *iload = data;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

#if 0
	if (mga_do_wait_for_idle(dev_priv) < 0) {
		if (MGA_DMA_DEBUG)
			DRM_INFO("-EBUSY\n");
		return -EBUSY;
	}
#endif
	if (iload->idx < 0 || iload->idx > dma->buf_count)
		return -EINVAL;

	buf = dma->buflist[iload->idx];
	buf_priv = buf->dev_private;

	if (mga_verify_iload(dev_priv, iload->dstorg, iload->length)) {
		mga_freelist_put(dev, buf);
		return -EINVAL;
	}

	WRAP_TEST_WITH_RETURN(dev_priv);

	mga_dma_dispatch_iload(dev, buf, iload->dstorg, iload->length);

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CONTEXT;

	return 0;
}

static int mga_dma_blit(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_blit_t *blit = data;
	DRM_DEBUG("\n");

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (sarea_priv->nbox > MGA_NR_SAREA_CLIPRECTS)
		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	if (mga_verify_blit(dev_priv, blit->srcorg, blit->dstorg))
		return -EINVAL;

	WRAP_TEST_WITH_RETURN(dev_priv);

	mga_dma_dispatch_blit(dev, blit);

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CONTEXT;

	return 0;
}

static int mga_getparam(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_getparam_t *param = data;
	int value;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	DRM_DEBUG("pid=%d\n", DRM_CURRENTPID);

	switch (param->param) {
	case MGA_PARAM_IRQ_NR:
		value = dev->irq;
		break;
	case MGA_PARAM_CARD_TYPE:
		value = dev_priv->chipset;
		break;
	default:
		return -EINVAL;
	}

	if (DRM_COPY_TO_USER(param->value, &value, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return -EFAULT;
	}

	return 0;
}

static int mga_set_fence(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	u32 *fence = data;
	DMA_LOCALS;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	DRM_DEBUG("pid=%d\n", DRM_CURRENTPID);

	/* I would normal do this assignment in the declaration of fence,
	 * but dev_priv may be NULL.
	 */

	*fence = dev_priv->next_fence_to_post;
	dev_priv->next_fence_to_post++;

	BEGIN_DMA(1);
	DMA_BLOCK(MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000,
		  MGA_DMAPAD, 0x00000000, MGA_SOFTRAP, 0x00000000);
	ADVANCE_DMA();

	return 0;
}

static int mga_wait_fence(struct drm_device *dev, void *data, struct drm_file *
file_priv)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	u32 *fence = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	DRM_DEBUG("pid=%d\n", DRM_CURRENTPID);

	mga_driver_fence_wait(dev, fence);
	return 0;
}

struct drm_ioctl_desc mga_ioctls[] = {
	DRM_IOCTL_DEF(DRM_MGA_INIT, mga_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_MGA_FLUSH, mga_dma_flush, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_RESET, mga_dma_reset, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_SWAP, mga_dma_swap, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_CLEAR, mga_dma_clear, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_VERTEX, mga_dma_vertex, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_INDICES, mga_dma_indices, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_ILOAD, mga_dma_iload, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_BLIT, mga_dma_blit, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_GETPARAM, mga_getparam, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_SET_FENCE, mga_set_fence, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_WAIT_FENCE, mga_wait_fence, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_MGA_DMA_BOOTSTRAP, mga_dma_bootstrap, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
};

int mga_max_ioctl = DRM_ARRAY_SIZE(mga_ioctls);
