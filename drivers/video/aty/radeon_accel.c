#include "radeonfb.h"

/* the accelerated functions here are patterned after the 
 * "ACCEL_MMIO" ifdef branches in XFree86
 * --dte
 */

#define FLUSH_CACHE_WORKAROUND	1

void radeon_fifo_update_and_wait(struct radeonfb_info *rinfo, int entries)
{
	int i;

	for (i=0; i<2000000; i++) {
		rinfo->fifo_free = INREG(RBBM_STATUS) & 0x7f;
		if (rinfo->fifo_free >= entries)
			return;
		udelay(10);
	}
	printk(KERN_ERR "radeonfb: FIFO Timeout !\n");
	/* XXX Todo: attempt to reset the engine */
}

static inline void radeon_fifo_wait(struct radeonfb_info *rinfo, int entries)
{
	if (entries <= rinfo->fifo_free)
		rinfo->fifo_free -= entries;
	else
		radeon_fifo_update_and_wait(rinfo, entries);
}

static inline void radeonfb_set_creg(struct radeonfb_info *rinfo, u32 reg,
				     u32 *cache, u32 new_val)
{
	if (new_val == *cache)
		return;
	*cache = new_val;
	radeon_fifo_wait(rinfo, 1);
	OUTREG(reg, new_val);
}

static void radeonfb_prim_fillrect(struct radeonfb_info *rinfo, 
				   const struct fb_fillrect *region)
{
	radeonfb_set_creg(rinfo, DP_GUI_MASTER_CNTL, &rinfo->dp_gui_mc_cache,
			  rinfo->dp_gui_mc_base | GMC_BRUSH_SOLID_COLOR | ROP3_P);
	radeonfb_set_creg(rinfo, DP_CNTL, &rinfo->dp_cntl_cache,
			  DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM);
	radeonfb_set_creg(rinfo, DP_BRUSH_FRGD_CLR, &rinfo->dp_brush_fg_cache,
			  region->color);

	/* Ensure the dst cache is flushed and the engine idle before
	 * issuing the operation.
	 *
	 * This works around engine lockups on some cards
	 */
#if FLUSH_CACHE_WORKAROUND
	radeon_fifo_wait(rinfo, 2);
	OUTREG(DSTCACHE_CTLSTAT, RB2D_DC_FLUSH_ALL);
	OUTREG(WAIT_UNTIL, (WAIT_2D_IDLECLEAN | WAIT_DMA_GUI_IDLE));
#endif
	radeon_fifo_wait(rinfo, 2);
	OUTREG(DST_Y_X, (region->dy << 16) | region->dx);
	OUTREG(DST_WIDTH_HEIGHT, (region->width << 16) | region->height);
}

void radeonfb_fillrect(struct fb_info *info, const struct fb_fillrect *region)
{
	struct radeonfb_info *rinfo = info->par;
	struct fb_fillrect modded;
	int vxres, vyres;
  
	WARN_ON(rinfo->gfx_mode);
	if (info->state != FBINFO_STATE_RUNNING || rinfo->gfx_mode)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_fillrect(info, region);
		return;
	}

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	memcpy(&modded, region, sizeof(struct fb_fillrect));

	if(!modded.width || !modded.height ||
	   modded.dx >= vxres || modded.dy >= vyres)
		return;
  
	if(modded.dx + modded.width  > vxres) modded.width  = vxres - modded.dx;
	if(modded.dy + modded.height > vyres) modded.height = vyres - modded.dy;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR )
		modded.color = ((u32 *) (info->pseudo_palette))[region->color];

	radeonfb_prim_fillrect(rinfo, &modded);
}

static void radeonfb_prim_copyarea(struct radeonfb_info *rinfo, 
				   const struct fb_copyarea *area)
{
	int xdir, ydir;
	u32 sx, sy, dx, dy, w, h;

	w = area->width; h = area->height;
	dx = area->dx; dy = area->dy;
	sx = area->sx; sy = area->sy;
	xdir = sx - dx;
	ydir = sy - dy;

	if ( xdir < 0 ) { sx += w-1; dx += w-1; }
	if ( ydir < 0 ) { sy += h-1; dy += h-1; }

	radeonfb_set_creg(rinfo, DP_GUI_MASTER_CNTL, &rinfo->dp_gui_mc_cache,
			  rinfo->dp_gui_mc_base |
			  GMC_BRUSH_NONE |
			  GMC_SRC_DATATYPE_COLOR |
			  ROP3_S |
			  DP_SRC_SOURCE_MEMORY);
	radeonfb_set_creg(rinfo, DP_CNTL, &rinfo->dp_cntl_cache,
			  (xdir>=0 ? DST_X_LEFT_TO_RIGHT : 0) |
			  (ydir>=0 ? DST_Y_TOP_TO_BOTTOM : 0));

#if FLUSH_CACHE_WORKAROUND
	radeon_fifo_wait(rinfo, 2);
	OUTREG(DSTCACHE_CTLSTAT, RB2D_DC_FLUSH_ALL);
	OUTREG(WAIT_UNTIL, (WAIT_2D_IDLECLEAN | WAIT_DMA_GUI_IDLE));
#endif
	radeon_fifo_wait(rinfo, 3);
	OUTREG(SRC_Y_X, (sy << 16) | sx);
	OUTREG(DST_Y_X, (dy << 16) | dx);
	OUTREG(DST_HEIGHT_WIDTH, (h << 16) | w);
}


void radeonfb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct radeonfb_info *rinfo = info->par;
	struct fb_copyarea modded;
	u32 vxres, vyres;
	modded.sx = area->sx;
	modded.sy = area->sy;
	modded.dx = area->dx;
	modded.dy = area->dy;
	modded.width  = area->width;
	modded.height = area->height;
  
	WARN_ON(rinfo->gfx_mode);
	if (info->state != FBINFO_STATE_RUNNING || rinfo->gfx_mode)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(info, area);
		return;
	}

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	if(!modded.width || !modded.height ||
	   modded.sx >= vxres || modded.sy >= vyres ||
	   modded.dx >= vxres || modded.dy >= vyres)
		return;
  
	if(modded.sx + modded.width > vxres)  modded.width = vxres - modded.sx;
	if(modded.dx + modded.width > vxres)  modded.width = vxres - modded.dx;
	if(modded.sy + modded.height > vyres) modded.height = vyres - modded.sy;
	if(modded.dy + modded.height > vyres) modded.height = vyres - modded.dy;
  
	radeonfb_prim_copyarea(rinfo, &modded);
}

static void radeonfb_prim_imageblit(struct radeonfb_info *rinfo,
				    const struct fb_image *image,
				    u32 fg, u32 bg)
{
	unsigned int src_bytes, dwords;
	u32 *bits;

	radeonfb_set_creg(rinfo, DP_GUI_MASTER_CNTL, &rinfo->dp_gui_mc_cache,
			  rinfo->dp_gui_mc_base |
			  GMC_BRUSH_NONE |
			  GMC_SRC_DATATYPE_MONO_FG_BG |
			  ROP3_S |
			  GMC_BYTE_ORDER_MSB_TO_LSB |
			  DP_SRC_SOURCE_HOST_DATA);
	radeonfb_set_creg(rinfo, DP_CNTL, &rinfo->dp_cntl_cache,
			  DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM);
	radeonfb_set_creg(rinfo, DP_SRC_FRGD_CLR, &rinfo->dp_src_fg_cache, fg);
	radeonfb_set_creg(rinfo, DP_SRC_BKGD_CLR, &rinfo->dp_src_bg_cache, bg);

	radeon_fifo_wait(rinfo, 1);
	OUTREG(DST_Y_X, (image->dy << 16) | image->dx);

	/* Ensure the dst cache is flushed and the engine idle before
	 * issuing the operation.
	 *
	 * This works around engine lockups on some cards
	 */
#if FLUSH_CACHE_WORKAROUND
	radeon_fifo_wait(rinfo, 2);
	OUTREG(DSTCACHE_CTLSTAT, RB2D_DC_FLUSH_ALL);
	OUTREG(WAIT_UNTIL, (WAIT_2D_IDLECLEAN | WAIT_DMA_GUI_IDLE));
#endif

	/* X here pads width to a multiple of 32 and uses the clipper to
	 * adjust the result. Is that really necessary ? Things seem to
	 * work ok for me without that and the doco doesn't seem to imply
	 * there is such a restriction.
	 */
	OUTREG(DST_WIDTH_HEIGHT, (image->width << 16) | image->height);

	src_bytes = (((image->width * image->depth) + 7) / 8) * image->height;
	dwords = (src_bytes + 3) / 4;
	bits = (u32*)(image->data);

	while(dwords >= 8) {
		radeon_fifo_wait(rinfo, 8);
#if BITS_PER_LONG == 64
		__raw_writeq(*((u64 *)(bits)), rinfo->mmio_base + HOST_DATA0);
		__raw_writeq(*((u64 *)(bits+2)), rinfo->mmio_base + HOST_DATA2);
		__raw_writeq(*((u64 *)(bits+4)), rinfo->mmio_base + HOST_DATA4);
		__raw_writeq(*((u64 *)(bits+6)), rinfo->mmio_base + HOST_DATA6);
		bits += 8;
#else
		__raw_writel(*(bits++), rinfo->mmio_base + HOST_DATA0);
		__raw_writel(*(bits++), rinfo->mmio_base + HOST_DATA1);
		__raw_writel(*(bits++), rinfo->mmio_base + HOST_DATA2);
		__raw_writel(*(bits++), rinfo->mmio_base + HOST_DATA3);
		__raw_writel(*(bits++), rinfo->mmio_base + HOST_DATA4);
		__raw_writel(*(bits++), rinfo->mmio_base + HOST_DATA5);
		__raw_writel(*(bits++), rinfo->mmio_base + HOST_DATA6);
		__raw_writel(*(bits++), rinfo->mmio_base + HOST_DATA7);
#endif
		dwords -= 8;
	}
	while(dwords--) {
		radeon_fifo_wait(rinfo, 1);
		__raw_writel(*(bits++), rinfo->mmio_base + HOST_DATA0);
	}
}

void radeonfb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct radeonfb_info *rinfo = info->par;
	u32 fg, bg;

	WARN_ON(rinfo->gfx_mode);
	if (info->state != FBINFO_STATE_RUNNING || rinfo->gfx_mode)
		return;

	if (!image->width || !image->height)
		return;

	/* We only do 1 bpp color expansion for now */
	if (info->flags & FBINFO_HWACCEL_DISABLED || image->depth != 1)
		goto fallback;

	/* Fallback if running out of the screen. We may do clipping
	 * in the future */
	if ((image->dx + image->width) > info->var.xres_virtual ||
	    (image->dy + image->height) > info->var.yres_virtual)
		goto fallback;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		fg = ((u32*)(info->pseudo_palette))[image->fg_color];
		bg = ((u32*)(info->pseudo_palette))[image->bg_color];
	} else {
		fg = image->fg_color;
		bg = image->bg_color;
	}

	radeonfb_prim_imageblit(rinfo, image, fg, bg);
	return;

 fallback:
	radeon_engine_idle(rinfo);

	cfb_imageblit(info, image);
}

int radeonfb_sync(struct fb_info *info)
{
	struct radeonfb_info *rinfo = info->par;

	if (info->state != FBINFO_STATE_RUNNING)
		return 0;

	radeon_engine_idle(rinfo);

	return 0;
}

void radeonfb_engine_reset(struct radeonfb_info *rinfo)
{
	u32 clock_cntl_index, mclk_cntl, rbbm_soft_reset;
	u32 host_path_cntl;

	radeon_engine_flush (rinfo);

	clock_cntl_index = INREG(CLOCK_CNTL_INDEX);
	mclk_cntl = INPLL(MCLK_CNTL);

	OUTPLL(MCLK_CNTL, (mclk_cntl |
			   FORCEON_MCLKA |
			   FORCEON_MCLKB |
			   FORCEON_YCLKA |
			   FORCEON_YCLKB |
			   FORCEON_MC |
			   FORCEON_AIC));

	host_path_cntl = INREG(HOST_PATH_CNTL);
	rbbm_soft_reset = INREG(RBBM_SOFT_RESET);

	if (IS_R300_VARIANT(rinfo)) {
		u32 tmp;

		OUTREG(RBBM_SOFT_RESET, (rbbm_soft_reset |
					 SOFT_RESET_CP |
					 SOFT_RESET_HI |
					 SOFT_RESET_E2));
		INREG(RBBM_SOFT_RESET);
		OUTREG(RBBM_SOFT_RESET, 0);
		tmp = INREG(RB2D_DSTCACHE_MODE);
		OUTREG(RB2D_DSTCACHE_MODE, tmp | (1 << 17)); /* FIXME */
	} else {
		OUTREG(RBBM_SOFT_RESET, rbbm_soft_reset |
					SOFT_RESET_CP |
					SOFT_RESET_HI |
					SOFT_RESET_SE |
					SOFT_RESET_RE |
					SOFT_RESET_PP |
					SOFT_RESET_E2 |
					SOFT_RESET_RB);
		INREG(RBBM_SOFT_RESET);
		OUTREG(RBBM_SOFT_RESET, rbbm_soft_reset & (u32)
					~(SOFT_RESET_CP |
					  SOFT_RESET_HI |
					  SOFT_RESET_SE |
					  SOFT_RESET_RE |
					  SOFT_RESET_PP |
					  SOFT_RESET_E2 |
					  SOFT_RESET_RB));
		INREG(RBBM_SOFT_RESET);
	}

	OUTREG(HOST_PATH_CNTL, host_path_cntl | HDP_SOFT_RESET);
	INREG(HOST_PATH_CNTL);
	OUTREG(HOST_PATH_CNTL, host_path_cntl);

	if (!IS_R300_VARIANT(rinfo))
		OUTREG(RBBM_SOFT_RESET, rbbm_soft_reset);

	OUTREG(CLOCK_CNTL_INDEX, clock_cntl_index);
	OUTPLL(MCLK_CNTL, mclk_cntl);
}

void radeonfb_engine_init (struct radeonfb_info *rinfo)
{
	unsigned long temp;

	/* disable 3D engine */
	OUTREG(RB3D_CNTL, 0);

	rinfo->fifo_free = 0;
	radeonfb_engine_reset(rinfo);

	radeon_fifo_wait(rinfo, 1);
	if (IS_R300_VARIANT(rinfo)) {
		OUTREG(RB2D_DSTCACHE_MODE, INREG(RB2D_DSTCACHE_MODE) |
		       RB2D_DC_AUTOFLUSH_ENABLE |
		       RB2D_DC_DC_DISABLE_IGNORE_PE);
	} else {
		/* This needs to be double checked with ATI. Latest X driver
		 * completely "forgets" to set this register on < r3xx, and
		 * we used to just write 0 there... I'll keep the 0 and update
		 * that when we have sorted things out on X side.
		 */
		OUTREG(RB2D_DSTCACHE_MODE, 0);
	}

	radeon_fifo_wait(rinfo, 3);
	/* We re-read MC_FB_LOCATION from card as it can have been
	 * modified by XFree drivers (ouch !)
	 */
	rinfo->fb_local_base = INREG(MC_FB_LOCATION) << 16;

	OUTREG(DEFAULT_PITCH_OFFSET, (rinfo->pitch << 0x16) |
				     (rinfo->fb_local_base >> 10));
	OUTREG(DST_PITCH_OFFSET, (rinfo->pitch << 0x16) | (rinfo->fb_local_base >> 10));
	OUTREG(SRC_PITCH_OFFSET, (rinfo->pitch << 0x16) | (rinfo->fb_local_base >> 10));

	radeon_fifo_wait(rinfo, 1);
#ifdef __BIG_ENDIAN
	OUTREGP(DP_DATATYPE, HOST_BIG_ENDIAN_EN, ~HOST_BIG_ENDIAN_EN);
#else
	OUTREGP(DP_DATATYPE, 0, ~HOST_BIG_ENDIAN_EN);
#endif
	radeon_fifo_wait(rinfo, 2);
	OUTREG(DEFAULT_SC_TOP_LEFT, 0);
	OUTREG(DEFAULT_SC_BOTTOM_RIGHT, (DEFAULT_SC_RIGHT_MAX |
					 DEFAULT_SC_BOTTOM_MAX));

	/* set default DP_GUI_MASTER_CNTL */
	temp = radeon_get_dstbpp(rinfo->depth);
	rinfo->dp_gui_mc_base = ((temp << 8) | GMC_CLR_CMP_CNTL_DIS);

	rinfo->dp_gui_mc_cache = rinfo->dp_gui_mc_base |
		GMC_BRUSH_SOLID_COLOR |
		GMC_SRC_DATATYPE_COLOR;
	radeon_fifo_wait(rinfo, 1);
	OUTREG(DP_GUI_MASTER_CNTL, rinfo->dp_gui_mc_cache);


	/* clear line drawing regs */
	radeon_fifo_wait(rinfo, 2);
	OUTREG(DST_LINE_START, 0);
	OUTREG(DST_LINE_END, 0);

	/* set brush and source color regs */
	rinfo->dp_brush_fg_cache = 0xffffffff;
	rinfo->dp_brush_bg_cache = 0x00000000;
	rinfo->dp_src_fg_cache = 0xffffffff;
	rinfo->dp_src_bg_cache = 0x00000000;
	radeon_fifo_wait(rinfo, 4);
	OUTREG(DP_BRUSH_FRGD_CLR, rinfo->dp_brush_fg_cache);
	OUTREG(DP_BRUSH_BKGD_CLR, rinfo->dp_brush_bg_cache);
	OUTREG(DP_SRC_FRGD_CLR, rinfo->dp_src_fg_cache);
	OUTREG(DP_SRC_BKGD_CLR, rinfo->dp_src_bg_cache);

	/* Default direction */
	rinfo->dp_cntl_cache = DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM;
	radeon_fifo_wait(rinfo, 1);
	OUTREG(DP_CNTL, rinfo->dp_cntl_cache);

	/* default write mask */
	radeon_fifo_wait(rinfo, 1);
	OUTREG(DP_WRITE_MSK, 0xffffffff);

	/* Default to no swapping of host data */
	radeon_fifo_wait(rinfo, 1);
	OUTREG(RBBM_GUICNTL, RBBM_GUICNTL_HOST_DATA_SWAP_NONE);

	/* Make sure it's settled */
	radeon_engine_idle(rinfo);
}
