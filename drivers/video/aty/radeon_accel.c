#include "radeonfb.h"

/* the accelerated functions here are patterned after the 
 * "ACCEL_MMIO" ifdef branches in XFree86
 * --dte
 */

static void radeon_fixup_offset(struct radeonfb_info *rinfo)
{
	u32 local_base;

	/* *** Ugly workaround *** */
	/*
	 * On some platforms, the video memory is mapped at 0 in radeon chip space
	 * (like PPCs) by the firmware. X will always move it up so that it's seen
	 * by the chip to be at the same address as the PCI BAR.
	 * That means that when switching back from X, there is a mismatch between
	 * the offsets programmed into the engine. This means that potentially,
	 * accel operations done before radeonfb has a chance to re-init the engine
	 * will have incorrect offsets, and potentially trash system memory !
	 *
	 * The correct fix is for fbcon to never call any accel op before the engine
	 * has properly been re-initialized (by a call to set_var), but this is a
	 * complex fix. This workaround in the meantime, called before every accel
	 * operation, makes sure the offsets are in sync.
	 */

	radeon_fifo_wait (1);
	local_base = INREG(MC_FB_LOCATION) << 16;
	if (local_base == rinfo->fb_local_base)
		return;

	rinfo->fb_local_base = local_base;

	radeon_fifo_wait (3);
	OUTREG(DEFAULT_PITCH_OFFSET, (rinfo->pitch << 0x16) |
				     (rinfo->fb_local_base >> 10));
	OUTREG(DST_PITCH_OFFSET, (rinfo->pitch << 0x16) | (rinfo->fb_local_base >> 10));
	OUTREG(SRC_PITCH_OFFSET, (rinfo->pitch << 0x16) | (rinfo->fb_local_base >> 10));
}

static void radeonfb_prim_fillrect(struct radeonfb_info *rinfo, 
				   const struct fb_fillrect *region)
{
	radeon_fifo_wait(4);  
  
	OUTREG(DP_GUI_MASTER_CNTL,  
		rinfo->dp_gui_master_cntl  /* contains, like GMC_DST_32BPP */
                | GMC_BRUSH_SOLID_COLOR
                | ROP3_P);
	if (radeon_get_dstbpp(rinfo->depth) != DST_8BPP)
		OUTREG(DP_BRUSH_FRGD_CLR, rinfo->pseudo_palette[region->color]);
	else
		OUTREG(DP_BRUSH_FRGD_CLR, region->color);
	OUTREG(DP_WRITE_MSK, 0xffffffff);
	OUTREG(DP_CNTL, (DST_X_LEFT_TO_RIGHT | DST_Y_TOP_TO_BOTTOM));

	radeon_fifo_wait(2);
	OUTREG(DSTCACHE_CTLSTAT, RB2D_DC_FLUSH_ALL);
	OUTREG(WAIT_UNTIL, (WAIT_2D_IDLECLEAN | WAIT_DMA_GUI_IDLE));

	radeon_fifo_wait(2);  
	OUTREG(DST_Y_X, (region->dy << 16) | region->dx);
	OUTREG(DST_WIDTH_HEIGHT, (region->width << 16) | region->height);
}

void radeonfb_fillrect(struct fb_info *info, const struct fb_fillrect *region)
{
	struct radeonfb_info *rinfo = info->par;
	struct fb_fillrect modded;
	int vxres, vyres;
  
	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_fillrect(info, region);
		return;
	}

	radeon_fixup_offset(rinfo);

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	memcpy(&modded, region, sizeof(struct fb_fillrect));

	if(!modded.width || !modded.height ||
	   modded.dx >= vxres || modded.dy >= vyres)
		return;
  
	if(modded.dx + modded.width  > vxres) modded.width  = vxres - modded.dx;
	if(modded.dy + modded.height > vyres) modded.height = vyres - modded.dy;

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

	radeon_fifo_wait(3);
	OUTREG(DP_GUI_MASTER_CNTL,
		rinfo->dp_gui_master_cntl /* i.e. GMC_DST_32BPP */
		| GMC_BRUSH_NONE
		| GMC_SRC_DSTCOLOR
		| ROP3_S 
		| DP_SRC_SOURCE_MEMORY );
	OUTREG(DP_WRITE_MSK, 0xffffffff);
	OUTREG(DP_CNTL, (xdir>=0 ? DST_X_LEFT_TO_RIGHT : 0)
			| (ydir>=0 ? DST_Y_TOP_TO_BOTTOM : 0));

	radeon_fifo_wait(2);
	OUTREG(DSTCACHE_CTLSTAT, RB2D_DC_FLUSH_ALL);
	OUTREG(WAIT_UNTIL, (WAIT_2D_IDLECLEAN | WAIT_DMA_GUI_IDLE));

	radeon_fifo_wait(3);
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
  
	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(info, area);
		return;
	}

	radeon_fixup_offset(rinfo);

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

void radeonfb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct radeonfb_info *rinfo = info->par;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	radeon_engine_idle();

	cfb_imageblit(info, image);
}

int radeonfb_sync(struct fb_info *info)
{
	struct radeonfb_info *rinfo = info->par;

	if (info->state != FBINFO_STATE_RUNNING)
		return 0;
	radeon_engine_idle();

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

	if (rinfo->family == CHIP_FAMILY_R300 ||
	    rinfo->family == CHIP_FAMILY_R350 ||
	    rinfo->family == CHIP_FAMILY_RV350) {
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

	if (rinfo->family != CHIP_FAMILY_R300 &&
	    rinfo->family != CHIP_FAMILY_R350 &&
	    rinfo->family != CHIP_FAMILY_RV350)
		OUTREG(RBBM_SOFT_RESET, rbbm_soft_reset);

	OUTREG(CLOCK_CNTL_INDEX, clock_cntl_index);
	OUTPLL(MCLK_CNTL, mclk_cntl);
}

void radeonfb_engine_init (struct radeonfb_info *rinfo)
{
	unsigned long temp;

	/* disable 3D engine */
	OUTREG(RB3D_CNTL, 0);

	radeonfb_engine_reset(rinfo);

	radeon_fifo_wait (1);
	if ((rinfo->family != CHIP_FAMILY_R300) &&
	    (rinfo->family != CHIP_FAMILY_R350) &&
	    (rinfo->family != CHIP_FAMILY_RV350))
		OUTREG(RB2D_DSTCACHE_MODE, 0);

	radeon_fifo_wait (3);
	/* We re-read MC_FB_LOCATION from card as it can have been
	 * modified by XFree drivers (ouch !)
	 */
	rinfo->fb_local_base = INREG(MC_FB_LOCATION) << 16;

	OUTREG(DEFAULT_PITCH_OFFSET, (rinfo->pitch << 0x16) |
				     (rinfo->fb_local_base >> 10));
	OUTREG(DST_PITCH_OFFSET, (rinfo->pitch << 0x16) | (rinfo->fb_local_base >> 10));
	OUTREG(SRC_PITCH_OFFSET, (rinfo->pitch << 0x16) | (rinfo->fb_local_base >> 10));

	radeon_fifo_wait (1);
#if defined(__BIG_ENDIAN)
	OUTREGP(DP_DATATYPE, HOST_BIG_ENDIAN_EN, ~HOST_BIG_ENDIAN_EN);
#else
	OUTREGP(DP_DATATYPE, 0, ~HOST_BIG_ENDIAN_EN);
#endif
	radeon_fifo_wait (2);
	OUTREG(DEFAULT_SC_TOP_LEFT, 0);
	OUTREG(DEFAULT_SC_BOTTOM_RIGHT, (DEFAULT_SC_RIGHT_MAX |
					 DEFAULT_SC_BOTTOM_MAX));

	temp = radeon_get_dstbpp(rinfo->depth);
	rinfo->dp_gui_master_cntl = ((temp << 8) | GMC_CLR_CMP_CNTL_DIS);

	radeon_fifo_wait (1);
	OUTREG(DP_GUI_MASTER_CNTL, (rinfo->dp_gui_master_cntl |
				    GMC_BRUSH_SOLID_COLOR |
				    GMC_SRC_DATATYPE_COLOR));

	radeon_fifo_wait (7);

	/* clear line drawing regs */
	OUTREG(DST_LINE_START, 0);
	OUTREG(DST_LINE_END, 0);

	/* set brush color regs */
	OUTREG(DP_BRUSH_FRGD_CLR, 0xffffffff);
	OUTREG(DP_BRUSH_BKGD_CLR, 0x00000000);

	/* set source color regs */
	OUTREG(DP_SRC_FRGD_CLR, 0xffffffff);
	OUTREG(DP_SRC_BKGD_CLR, 0x00000000);

	/* default write mask */
	OUTREG(DP_WRITE_MSK, 0xffffffff);

	radeon_engine_idle ();
}
