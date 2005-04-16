/* $Id: ffb_context.c,v 1.5 2001/08/09 17:47:51 davem Exp $
 * ffb_context.c: Creator/Creator3D DRI/DRM context switching.
 *
 * Copyright (C) 2000 David S. Miller (davem@redhat.com)
 *
 * Almost entirely stolen from tdfx_context.c, see there
 * for authors.
 */

#include <linux/sched.h>
#include <asm/upa.h>

#include "ffb.h"
#include "drmP.h"

#include "ffb_drv.h"

static int DRM(alloc_queue)(drm_device_t *dev, int is_2d_only)
{
	ffb_dev_priv_t *fpriv = (ffb_dev_priv_t *) dev->dev_private;
	int i;

	for (i = 0; i < FFB_MAX_CTXS; i++) {
		if (fpriv->hw_state[i] == NULL)
			break;
	}
	if (i == FFB_MAX_CTXS)
		return -1;

	fpriv->hw_state[i] = kmalloc(sizeof(struct ffb_hw_context), GFP_KERNEL);
	if (fpriv->hw_state[i] == NULL)
		return -1;

	fpriv->hw_state[i]->is_2d_only = is_2d_only;

	/* Plus one because 0 is the special DRM_KERNEL_CONTEXT. */
	return i + 1;
}

static void ffb_save_context(ffb_dev_priv_t *fpriv, int idx)
{
	ffb_fbcPtr ffb = fpriv->regs;
	struct ffb_hw_context *ctx;
	int i;

	ctx = fpriv->hw_state[idx - 1];
	if (idx == 0 || ctx == NULL)
		return;

	if (ctx->is_2d_only) {
		/* 2D applications only care about certain pieces
		 * of state.
		 */
		ctx->drawop = upa_readl(&ffb->drawop);
		ctx->ppc = upa_readl(&ffb->ppc);
		ctx->wid = upa_readl(&ffb->wid);
		ctx->fg = upa_readl(&ffb->fg);
		ctx->bg = upa_readl(&ffb->bg);
		ctx->xclip = upa_readl(&ffb->xclip);
		ctx->fbc = upa_readl(&ffb->fbc);
		ctx->rop = upa_readl(&ffb->rop);
		ctx->cmp = upa_readl(&ffb->cmp);
		ctx->matchab = upa_readl(&ffb->matchab);
		ctx->magnab = upa_readl(&ffb->magnab);
		ctx->pmask = upa_readl(&ffb->pmask);
		ctx->xpmask = upa_readl(&ffb->xpmask);
		ctx->lpat = upa_readl(&ffb->lpat);
		ctx->fontxy = upa_readl(&ffb->fontxy);
		ctx->fontw = upa_readl(&ffb->fontw);
		ctx->fontinc = upa_readl(&ffb->fontinc);

		/* stencil/stencilctl only exists on FFB2+ and later
		 * due to the introduction of 3DRAM-III.
		 */
		if (fpriv->ffb_type == ffb2_vertical_plus ||
		    fpriv->ffb_type == ffb2_horizontal_plus) {
			ctx->stencil = upa_readl(&ffb->stencil);
			ctx->stencilctl = upa_readl(&ffb->stencilctl);
		}

		for (i = 0; i < 32; i++)
			ctx->area_pattern[i] = upa_readl(&ffb->pattern[i]);
		ctx->ucsr = upa_readl(&ffb->ucsr);
		return;
	}

	/* Fetch drawop. */
	ctx->drawop = upa_readl(&ffb->drawop);

	/* If we were saving the vertex registers, this is where
	 * we would do it.  We would save 32 32-bit words starting
	 * at ffb->suvtx.
	 */

	/* Capture rendering attributes. */

	ctx->ppc = upa_readl(&ffb->ppc);		/* Pixel Processor Control */
	ctx->wid = upa_readl(&ffb->wid);		/* Current WID */
	ctx->fg = upa_readl(&ffb->fg);			/* Constant FG color */
	ctx->bg = upa_readl(&ffb->bg);			/* Constant BG color */
	ctx->consty = upa_readl(&ffb->consty);		/* Constant Y */
	ctx->constz = upa_readl(&ffb->constz);		/* Constant Z */
	ctx->xclip = upa_readl(&ffb->xclip);		/* X plane clip */
	ctx->dcss = upa_readl(&ffb->dcss);		/* Depth Cue Scale Slope */
	ctx->vclipmin = upa_readl(&ffb->vclipmin);	/* Primary XY clip, minimum */
	ctx->vclipmax = upa_readl(&ffb->vclipmax);	/* Primary XY clip, maximum */
	ctx->vclipzmin = upa_readl(&ffb->vclipzmin);	/* Primary Z clip, minimum */
	ctx->vclipzmax = upa_readl(&ffb->vclipzmax);	/* Primary Z clip, maximum */
	ctx->dcsf = upa_readl(&ffb->dcsf);		/* Depth Cue Scale Front Bound */
	ctx->dcsb = upa_readl(&ffb->dcsb);		/* Depth Cue Scale Back Bound */
	ctx->dczf = upa_readl(&ffb->dczf);		/* Depth Cue Scale Z Front */
	ctx->dczb = upa_readl(&ffb->dczb);		/* Depth Cue Scale Z Back */
	ctx->blendc = upa_readl(&ffb->blendc);		/* Alpha Blend Control */
	ctx->blendc1 = upa_readl(&ffb->blendc1);	/* Alpha Blend Color 1 */
	ctx->blendc2 = upa_readl(&ffb->blendc2);	/* Alpha Blend Color 2 */
	ctx->fbc = upa_readl(&ffb->fbc);		/* Frame Buffer Control */
	ctx->rop = upa_readl(&ffb->rop);		/* Raster Operation */
	ctx->cmp = upa_readl(&ffb->cmp);		/* Compare Controls */
	ctx->matchab = upa_readl(&ffb->matchab);	/* Buffer A/B Match Ops */
	ctx->matchc = upa_readl(&ffb->matchc);		/* Buffer C Match Ops */
	ctx->magnab = upa_readl(&ffb->magnab);		/* Buffer A/B Magnitude Ops */
	ctx->magnc = upa_readl(&ffb->magnc);		/* Buffer C Magnitude Ops */
	ctx->pmask = upa_readl(&ffb->pmask);		/* RGB Plane Mask */
	ctx->xpmask = upa_readl(&ffb->xpmask);		/* X Plane Mask */
	ctx->ypmask = upa_readl(&ffb->ypmask);		/* Y Plane Mask */
	ctx->zpmask = upa_readl(&ffb->zpmask);		/* Z Plane Mask */

	/* Auxiliary Clips. */
	ctx->auxclip0min = upa_readl(&ffb->auxclip[0].min);
	ctx->auxclip0max = upa_readl(&ffb->auxclip[0].max);
	ctx->auxclip1min = upa_readl(&ffb->auxclip[1].min);
	ctx->auxclip1max = upa_readl(&ffb->auxclip[1].max);
	ctx->auxclip2min = upa_readl(&ffb->auxclip[2].min);
	ctx->auxclip2max = upa_readl(&ffb->auxclip[2].max);
	ctx->auxclip3min = upa_readl(&ffb->auxclip[3].min);
	ctx->auxclip3max = upa_readl(&ffb->auxclip[3].max);

	ctx->lpat = upa_readl(&ffb->lpat);		/* Line Pattern */
	ctx->fontxy = upa_readl(&ffb->fontxy);		/* XY Font Coordinate */
	ctx->fontw = upa_readl(&ffb->fontw);		/* Font Width */
	ctx->fontinc = upa_readl(&ffb->fontinc);	/* Font X/Y Increment */

	/* These registers/features only exist on FFB2 and later chips. */
	if (fpriv->ffb_type >= ffb2_prototype) {
		ctx->dcss1 = upa_readl(&ffb->dcss1);	/* Depth Cue Scale Slope 1 */
		ctx->dcss2 = upa_readl(&ffb->dcss2);	/* Depth Cue Scale Slope 2 */
		ctx->dcss2 = upa_readl(&ffb->dcss3);	/* Depth Cue Scale Slope 3 */
		ctx->dcs2  = upa_readl(&ffb->dcs2);	/* Depth Cue Scale 2 */
		ctx->dcs3  = upa_readl(&ffb->dcs3);	/* Depth Cue Scale 3 */
		ctx->dcs4  = upa_readl(&ffb->dcs4);	/* Depth Cue Scale 4 */
		ctx->dcd2  = upa_readl(&ffb->dcd2);	/* Depth Cue Depth 2 */
		ctx->dcd3  = upa_readl(&ffb->dcd3);	/* Depth Cue Depth 3 */
		ctx->dcd4  = upa_readl(&ffb->dcd4);	/* Depth Cue Depth 4 */

		/* And stencil/stencilctl only exists on FFB2+ and later
		 * due to the introduction of 3DRAM-III.
		 */
		if (fpriv->ffb_type == ffb2_vertical_plus ||
		    fpriv->ffb_type == ffb2_horizontal_plus) {
			ctx->stencil = upa_readl(&ffb->stencil);
			ctx->stencilctl = upa_readl(&ffb->stencilctl);
		}
	}

	/* Save the 32x32 area pattern. */
	for (i = 0; i < 32; i++)
		ctx->area_pattern[i] = upa_readl(&ffb->pattern[i]);

	/* Finally, stash away the User Constol/Status Register. */
	ctx->ucsr = upa_readl(&ffb->ucsr);
}

static void ffb_restore_context(ffb_dev_priv_t *fpriv, int old, int idx)
{
	ffb_fbcPtr ffb = fpriv->regs;
	struct ffb_hw_context *ctx;
	int i;

	ctx = fpriv->hw_state[idx - 1];
	if (idx == 0 || ctx == NULL)
		return;

	if (ctx->is_2d_only) {
		/* 2D applications only care about certain pieces
		 * of state.
		 */
		upa_writel(ctx->drawop, &ffb->drawop);

		/* If we were restoring the vertex registers, this is where
		 * we would do it.  We would restore 32 32-bit words starting
		 * at ffb->suvtx.
		 */

		upa_writel(ctx->ppc, &ffb->ppc);
		upa_writel(ctx->wid, &ffb->wid);
		upa_writel(ctx->fg,  &ffb->fg);
		upa_writel(ctx->bg, &ffb->bg);
		upa_writel(ctx->xclip, &ffb->xclip);
		upa_writel(ctx->fbc, &ffb->fbc);
		upa_writel(ctx->rop, &ffb->rop);
		upa_writel(ctx->cmp, &ffb->cmp);
		upa_writel(ctx->matchab, &ffb->matchab);
		upa_writel(ctx->magnab, &ffb->magnab);
		upa_writel(ctx->pmask, &ffb->pmask);
		upa_writel(ctx->xpmask, &ffb->xpmask);
		upa_writel(ctx->lpat, &ffb->lpat);
		upa_writel(ctx->fontxy, &ffb->fontxy);
		upa_writel(ctx->fontw, &ffb->fontw);
		upa_writel(ctx->fontinc, &ffb->fontinc);

		/* stencil/stencilctl only exists on FFB2+ and later
		 * due to the introduction of 3DRAM-III.
		 */
		if (fpriv->ffb_type == ffb2_vertical_plus ||
		    fpriv->ffb_type == ffb2_horizontal_plus) {
			upa_writel(ctx->stencil, &ffb->stencil);
			upa_writel(ctx->stencilctl, &ffb->stencilctl);
			upa_writel(0x80000000, &ffb->fbc);
			upa_writel((ctx->stencilctl | 0x80000),
				   &ffb->rawstencilctl);
			upa_writel(ctx->fbc, &ffb->fbc);
		}

		for (i = 0; i < 32; i++)
			upa_writel(ctx->area_pattern[i], &ffb->pattern[i]);
		upa_writel((ctx->ucsr & 0xf0000), &ffb->ucsr);
		return;
	}

	/* Restore drawop. */
	upa_writel(ctx->drawop, &ffb->drawop);

	/* If we were restoring the vertex registers, this is where
	 * we would do it.  We would restore 32 32-bit words starting
	 * at ffb->suvtx.
	 */

	/* Restore rendering attributes. */

	upa_writel(ctx->ppc, &ffb->ppc);		/* Pixel Processor Control */
	upa_writel(ctx->wid, &ffb->wid);		/* Current WID */
	upa_writel(ctx->fg, &ffb->fg);			/* Constant FG color */
	upa_writel(ctx->bg, &ffb->bg);			/* Constant BG color */
	upa_writel(ctx->consty, &ffb->consty);		/* Constant Y */
	upa_writel(ctx->constz, &ffb->constz);		/* Constant Z */
	upa_writel(ctx->xclip, &ffb->xclip);		/* X plane clip */
	upa_writel(ctx->dcss, &ffb->dcss);		/* Depth Cue Scale Slope */
	upa_writel(ctx->vclipmin, &ffb->vclipmin);	/* Primary XY clip, minimum */
	upa_writel(ctx->vclipmax, &ffb->vclipmax);	/* Primary XY clip, maximum */
	upa_writel(ctx->vclipzmin, &ffb->vclipzmin);	/* Primary Z clip, minimum */
	upa_writel(ctx->vclipzmax, &ffb->vclipzmax);	/* Primary Z clip, maximum */
	upa_writel(ctx->dcsf, &ffb->dcsf);		/* Depth Cue Scale Front Bound */
	upa_writel(ctx->dcsb, &ffb->dcsb);		/* Depth Cue Scale Back Bound */
	upa_writel(ctx->dczf, &ffb->dczf);		/* Depth Cue Scale Z Front */
	upa_writel(ctx->dczb, &ffb->dczb);		/* Depth Cue Scale Z Back */
	upa_writel(ctx->blendc, &ffb->blendc);		/* Alpha Blend Control */
	upa_writel(ctx->blendc1, &ffb->blendc1);	/* Alpha Blend Color 1 */
	upa_writel(ctx->blendc2, &ffb->blendc2);	/* Alpha Blend Color 2 */
	upa_writel(ctx->fbc, &ffb->fbc);		/* Frame Buffer Control */
	upa_writel(ctx->rop, &ffb->rop);		/* Raster Operation */
	upa_writel(ctx->cmp, &ffb->cmp);		/* Compare Controls */
	upa_writel(ctx->matchab, &ffb->matchab);	/* Buffer A/B Match Ops */
	upa_writel(ctx->matchc, &ffb->matchc);		/* Buffer C Match Ops */
	upa_writel(ctx->magnab, &ffb->magnab);		/* Buffer A/B Magnitude Ops */
	upa_writel(ctx->magnc, &ffb->magnc);		/* Buffer C Magnitude Ops */
	upa_writel(ctx->pmask, &ffb->pmask);		/* RGB Plane Mask */
	upa_writel(ctx->xpmask, &ffb->xpmask);		/* X Plane Mask */
	upa_writel(ctx->ypmask, &ffb->ypmask);		/* Y Plane Mask */
	upa_writel(ctx->zpmask, &ffb->zpmask);		/* Z Plane Mask */

	/* Auxiliary Clips. */
	upa_writel(ctx->auxclip0min, &ffb->auxclip[0].min);
	upa_writel(ctx->auxclip0max, &ffb->auxclip[0].max);
	upa_writel(ctx->auxclip1min, &ffb->auxclip[1].min);
	upa_writel(ctx->auxclip1max, &ffb->auxclip[1].max);
	upa_writel(ctx->auxclip2min, &ffb->auxclip[2].min);
	upa_writel(ctx->auxclip2max, &ffb->auxclip[2].max);
	upa_writel(ctx->auxclip3min, &ffb->auxclip[3].min);
	upa_writel(ctx->auxclip3max, &ffb->auxclip[3].max);

	upa_writel(ctx->lpat, &ffb->lpat);		/* Line Pattern */
	upa_writel(ctx->fontxy, &ffb->fontxy);		/* XY Font Coordinate */
	upa_writel(ctx->fontw, &ffb->fontw);		/* Font Width */
	upa_writel(ctx->fontinc, &ffb->fontinc);	/* Font X/Y Increment */

	/* These registers/features only exist on FFB2 and later chips. */
	if (fpriv->ffb_type >= ffb2_prototype) {
		upa_writel(ctx->dcss1, &ffb->dcss1);	/* Depth Cue Scale Slope 1 */
		upa_writel(ctx->dcss2, &ffb->dcss2);	/* Depth Cue Scale Slope 2 */
		upa_writel(ctx->dcss3, &ffb->dcss2);	/* Depth Cue Scale Slope 3 */
		upa_writel(ctx->dcs2, &ffb->dcs2);	/* Depth Cue Scale 2 */
		upa_writel(ctx->dcs3, &ffb->dcs3);	/* Depth Cue Scale 3 */
		upa_writel(ctx->dcs4, &ffb->dcs4);	/* Depth Cue Scale 4 */
		upa_writel(ctx->dcd2, &ffb->dcd2);	/* Depth Cue Depth 2 */
		upa_writel(ctx->dcd3, &ffb->dcd3);	/* Depth Cue Depth 3 */
		upa_writel(ctx->dcd4, &ffb->dcd4);	/* Depth Cue Depth 4 */

		/* And stencil/stencilctl only exists on FFB2+ and later
		 * due to the introduction of 3DRAM-III.
		 */
		if (fpriv->ffb_type == ffb2_vertical_plus ||
		    fpriv->ffb_type == ffb2_horizontal_plus) {
			/* Unfortunately, there is a hardware bug on
			 * the FFB2+ chips which prevents a normal write
			 * to the stencil control register from working
			 * as it should.
			 *
			 * The state controlled by the FFB stencilctl register
			 * really gets transferred to the per-buffer instances
			 * of the stencilctl register in the 3DRAM chips.
			 *
			 * The bug is that FFB does not update buffer C correctly,
			 * so we have to do it by hand for them.
			 */

			/* This will update buffers A and B. */
			upa_writel(ctx->stencil, &ffb->stencil);
			upa_writel(ctx->stencilctl, &ffb->stencilctl);

			/* Force FFB to use buffer C 3dram regs. */
			upa_writel(0x80000000, &ffb->fbc);
			upa_writel((ctx->stencilctl | 0x80000),
				   &ffb->rawstencilctl);

			/* Now restore the correct FBC controls. */
			upa_writel(ctx->fbc, &ffb->fbc);
		}
	}

	/* Restore the 32x32 area pattern. */
	for (i = 0; i < 32; i++)
		upa_writel(ctx->area_pattern[i], &ffb->pattern[i]);

	/* Finally, stash away the User Constol/Status Register.
	 * The only state we really preserve here is the picking
	 * control.
	 */
	upa_writel((ctx->ucsr & 0xf0000), &ffb->ucsr);
}

#define FFB_UCSR_FB_BUSY       0x01000000
#define FFB_UCSR_RP_BUSY       0x02000000
#define FFB_UCSR_ALL_BUSY      (FFB_UCSR_RP_BUSY|FFB_UCSR_FB_BUSY)

static void FFBWait(ffb_fbcPtr ffb)
{
	int limit = 100000;

	do {
		u32 regval = upa_readl(&ffb->ucsr);

		if ((regval & FFB_UCSR_ALL_BUSY) == 0)
			break;
	} while (--limit);
}

int ffb_driver_context_switch(drm_device_t *dev, int old, int new)
{
	ffb_dev_priv_t *fpriv = (ffb_dev_priv_t *) dev->dev_private;

#ifdef DRM_DMA_HISTOGRAM
        dev->ctx_start = get_cycles();
#endif
        
        DRM_DEBUG("Context switch from %d to %d\n", old, new);

        if (new == dev->last_context ||
	    dev->last_context == 0) {
		dev->last_context = new;
                return 0;
	}
        
	FFBWait(fpriv->regs);
	ffb_save_context(fpriv, old);
	ffb_restore_context(fpriv, old, new);
	FFBWait(fpriv->regs);
        
	dev->last_context = new;

        return 0;
}

int ffb_driver_resctx(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	drm_ctx_res_t	res;
	drm_ctx_t	ctx;
	int		i;

	DRM_DEBUG("%d\n", DRM_RESERVED_CONTEXTS);
	if (copy_from_user(&res, (drm_ctx_res_t __user *)arg, sizeof(res)))
		return -EFAULT;
	if (res.count >= DRM_RESERVED_CONTEXTS) {
		memset(&ctx, 0, sizeof(ctx));
		for (i = 0; i < DRM_RESERVED_CONTEXTS; i++) {
			ctx.handle = i;
			if (copy_to_user(&res.contexts[i],
					 &i,
					 sizeof(i)))
				return -EFAULT;
		}
	}
	res.count = DRM_RESERVED_CONTEXTS;
	if (copy_to_user((drm_ctx_res_t __user *)arg, &res, sizeof(res)))
		return -EFAULT;
	return 0;
}


int ffb_driver_addctx(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_t	ctx;
	int idx;

	if (copy_from_user(&ctx, (drm_ctx_t __user *)arg, sizeof(ctx)))
		return -EFAULT;
	idx = DRM(alloc_queue)(dev, (ctx.flags & _DRM_CONTEXT_2DONLY));
	if (idx < 0)
		return -ENFILE;

	DRM_DEBUG("%d\n", ctx.handle);
	ctx.handle = idx;
	if (copy_to_user((drm_ctx_t __user *)arg, &ctx, sizeof(ctx)))
		return -EFAULT;
	return 0;
}

int ffb_driver_modctx(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	ffb_dev_priv_t	*fpriv	= (ffb_dev_priv_t *) dev->dev_private;
	struct ffb_hw_context *hwctx;
	drm_ctx_t ctx;
	int idx;

	if (copy_from_user(&ctx, (drm_ctx_t __user *)arg, sizeof(ctx)))
		return -EFAULT;

	idx = ctx.handle;
	if (idx <= 0 || idx >= FFB_MAX_CTXS)
		return -EINVAL;

	hwctx = fpriv->hw_state[idx - 1];
	if (hwctx == NULL)
		return -EINVAL;

	if ((ctx.flags & _DRM_CONTEXT_2DONLY) == 0)
		hwctx->is_2d_only = 0;
	else
		hwctx->is_2d_only = 1;

	return 0;
}

int ffb_driver_getctx(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	ffb_dev_priv_t	*fpriv	= (ffb_dev_priv_t *) dev->dev_private;
	struct ffb_hw_context *hwctx;
	drm_ctx_t ctx;
	int idx;

	if (copy_from_user(&ctx, (drm_ctx_t __user *)arg, sizeof(ctx)))
		return -EFAULT;

	idx = ctx.handle;
	if (idx <= 0 || idx >= FFB_MAX_CTXS)
		return -EINVAL;

	hwctx = fpriv->hw_state[idx - 1];
	if (hwctx == NULL)
		return -EINVAL;

	if (hwctx->is_2d_only != 0)
		ctx.flags = _DRM_CONTEXT_2DONLY;
	else
		ctx.flags = 0;

	if (copy_to_user((drm_ctx_t __user *)arg, &ctx, sizeof(ctx)))
		return -EFAULT;

	return 0;
}

int ffb_driver_switchctx(struct inode *inode, struct file *filp, unsigned int cmd,
		   unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_t	ctx;

	if (copy_from_user(&ctx, (drm_ctx_t  __user *)arg, sizeof(ctx)))
		return -EFAULT;
	DRM_DEBUG("%d\n", ctx.handle);
	return ffb_driver_context_switch(dev, dev->last_context, ctx.handle);
}

int ffb_driver_newctx(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	drm_ctx_t	ctx;

	if (copy_from_user(&ctx, (drm_ctx_t  __user *)arg, sizeof(ctx)))
		return -EFAULT;
	DRM_DEBUG("%d\n", ctx.handle);

	return 0;
}

int ffb_driver_rmctx(struct inode *inode, struct file *filp, unsigned int cmd,
	       unsigned long arg)
{
	drm_ctx_t	ctx;
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	ffb_dev_priv_t	*fpriv	= (ffb_dev_priv_t *) dev->dev_private;
	int idx;

	if (copy_from_user(&ctx, (drm_ctx_t __user *)arg, sizeof(ctx)))
		return -EFAULT;
	DRM_DEBUG("%d\n", ctx.handle);

	idx = ctx.handle - 1;
	if (idx < 0 || idx >= FFB_MAX_CTXS)
		return -EINVAL;

	if (fpriv->hw_state[idx] != NULL) {
		kfree(fpriv->hw_state[idx]);
		fpriv->hw_state[idx] = NULL;
	}
	return 0;
}

void ffb_set_context_ioctls(void)
{
	DRM(ioctls)[DRM_IOCTL_NR(DRM_IOCTL_ADD_CTX)].func = ffb_driver_addctx;
	DRM(ioctls)[DRM_IOCTL_NR(DRM_IOCTL_RM_CTX)].func = ffb_driver_rmctx;
	DRM(ioctls)[DRM_IOCTL_NR(DRM_IOCTL_MOD_CTX)].func = ffb_driver_modctx;
	DRM(ioctls)[DRM_IOCTL_NR(DRM_IOCTL_GET_CTX)].func = ffb_driver_getctx;
	DRM(ioctls)[DRM_IOCTL_NR(DRM_IOCTL_SWITCH_CTX)].func = ffb_driver_switchctx;
	DRM(ioctls)[DRM_IOCTL_NR(DRM_IOCTL_NEW_CTX)].func = ffb_driver_newctx;
	DRM(ioctls)[DRM_IOCTL_NR(DRM_IOCTL_RES_CTX)].func = ffb_driver_resctx;

}
