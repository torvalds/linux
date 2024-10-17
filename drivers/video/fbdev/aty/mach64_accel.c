// SPDX-License-Identifier: GPL-2.0

/*
 *  ATI Mach64 Hardware Acceleration
 */

#include <linux/delay.h>
#include <linux/unaligned.h>
#include <linux/fb.h>
#include <video/mach64.h>
#include "atyfb.h"

    /*
     *  Generic Mach64 routines
     */

/* this is for DMA GUI engine! work in progress */
typedef struct {
	u32 frame_buf_offset;
	u32 system_mem_addr;
	u32 command;
	u32 reserved;
} BM_DESCRIPTOR_ENTRY;

#define LAST_DESCRIPTOR (1 << 31)
#define SYSTEM_TO_FRAME_BUFFER 0

static u32 rotation24bpp(u32 dx, u32 direction)
{
	u32 rotation;
	if (direction & DST_X_LEFT_TO_RIGHT) {
		rotation = (dx / 4) % 6;
	} else {
		rotation = ((dx + 2) / 4) % 6;
	}

	return ((rotation << 8) | DST_24_ROTATION_ENABLE);
}

void aty_reset_engine(struct atyfb_par *par)
{
	/* reset engine */
	aty_st_le32(GEN_TEST_CNTL,
		aty_ld_le32(GEN_TEST_CNTL, par) &
		~(GUI_ENGINE_ENABLE | HWCURSOR_ENABLE), par);
	/* enable engine */
	aty_st_le32(GEN_TEST_CNTL,
		aty_ld_le32(GEN_TEST_CNTL, par) | GUI_ENGINE_ENABLE, par);
	/* ensure engine is not locked up by clearing any FIFO or */
	/* HOST errors */
	aty_st_le32(BUS_CNTL,
		aty_ld_le32(BUS_CNTL, par) | BUS_HOST_ERR_ACK | BUS_FIFO_ERR_ACK, par);

	par->fifo_space = 0;
}

static void reset_GTC_3D_engine(const struct atyfb_par *par)
{
	aty_st_le32(SCALE_3D_CNTL, 0xc0, par);
	mdelay(GTC_3D_RESET_DELAY);
	aty_st_le32(SETUP_CNTL, 0x00, par);
	mdelay(GTC_3D_RESET_DELAY);
	aty_st_le32(SCALE_3D_CNTL, 0x00, par);
	mdelay(GTC_3D_RESET_DELAY);
}

void aty_init_engine(struct atyfb_par *par, struct fb_info *info)
{
	u32 pitch_value;
	u32 vxres;

	/* determine modal information from global mode structure */
	pitch_value = info->fix.line_length / (info->var.bits_per_pixel / 8);
	vxres = info->var.xres_virtual;

	if (info->var.bits_per_pixel == 24) {
		/* In 24 bpp, the engine is in 8 bpp - this requires that all */
		/* horizontal coordinates and widths must be adjusted */
		pitch_value *= 3;
		vxres *= 3;
	}

	/* On GTC (RagePro), we need to reset the 3D engine before */
	if (M64_HAS(RESET_3D))
		reset_GTC_3D_engine(par);

	/* Reset engine, enable, and clear any engine errors */
	aty_reset_engine(par);
	/* Ensure that vga page pointers are set to zero - the upper */
	/* page pointers are set to 1 to handle overflows in the */
	/* lower page */
	aty_st_le32(MEM_VGA_WP_SEL, 0x00010000, par);
	aty_st_le32(MEM_VGA_RP_SEL, 0x00010000, par);

	/* ---- Setup standard engine context ---- */

	/* All GUI registers here are FIFOed - therefore, wait for */
	/* the appropriate number of empty FIFO entries */
	wait_for_fifo(14, par);

	/* enable all registers to be loaded for context loads */
	aty_st_le32(CONTEXT_MASK, 0xFFFFFFFF, par);

	/* set destination pitch to modal pitch, set offset to zero */
	aty_st_le32(DST_OFF_PITCH, (pitch_value / 8) << 22, par);

	/* zero these registers (set them to a known state) */
	aty_st_le32(DST_Y_X, 0, par);
	aty_st_le32(DST_HEIGHT, 0, par);
	aty_st_le32(DST_BRES_ERR, 0, par);
	aty_st_le32(DST_BRES_INC, 0, par);
	aty_st_le32(DST_BRES_DEC, 0, par);

	/* set destination drawing attributes */
	aty_st_le32(DST_CNTL, DST_LAST_PEL | DST_Y_TOP_TO_BOTTOM |
		    DST_X_LEFT_TO_RIGHT, par);

	/* set source pitch to modal pitch, set offset to zero */
	aty_st_le32(SRC_OFF_PITCH, (pitch_value / 8) << 22, par);

	/* set these registers to a known state */
	aty_st_le32(SRC_Y_X, 0, par);
	aty_st_le32(SRC_HEIGHT1_WIDTH1, 1, par);
	aty_st_le32(SRC_Y_X_START, 0, par);
	aty_st_le32(SRC_HEIGHT2_WIDTH2, 1, par);

	/* set source pixel retrieving attributes */
	aty_st_le32(SRC_CNTL, SRC_LINE_X_LEFT_TO_RIGHT, par);

	/* set host attributes */
	wait_for_fifo(13, par);
	aty_st_le32(HOST_CNTL, HOST_BYTE_ALIGN, par);

	/* set pattern attributes */
	aty_st_le32(PAT_REG0, 0, par);
	aty_st_le32(PAT_REG1, 0, par);
	aty_st_le32(PAT_CNTL, 0, par);

	/* set scissors to modal size */
	aty_st_le32(SC_LEFT, 0, par);
	aty_st_le32(SC_TOP, 0, par);
	aty_st_le32(SC_BOTTOM, par->crtc.vyres - 1, par);
	aty_st_le32(SC_RIGHT, vxres - 1, par);

	/* set background color to minimum value (usually BLACK) */
	aty_st_le32(DP_BKGD_CLR, 0, par);

	/* set foreground color to maximum value (usually WHITE) */
	aty_st_le32(DP_FRGD_CLR, 0xFFFFFFFF, par);

	/* set write mask to effect all pixel bits */
	aty_st_le32(DP_WRITE_MASK, 0xFFFFFFFF, par);

	/* set foreground mix to overpaint and background mix to */
	/* no-effect */
	aty_st_le32(DP_MIX, FRGD_MIX_S | BKGD_MIX_D, par);

	/* set primary source pixel channel to foreground color */
	/* register */
	aty_st_le32(DP_SRC, FRGD_SRC_FRGD_CLR, par);

	/* set compare functionality to false (no-effect on */
	/* destination) */
	wait_for_fifo(3, par);
	aty_st_le32(CLR_CMP_CLR, 0, par);
	aty_st_le32(CLR_CMP_MASK, 0xFFFFFFFF, par);
	aty_st_le32(CLR_CMP_CNTL, 0, par);

	/* set pixel depth */
	wait_for_fifo(2, par);
	aty_st_le32(DP_PIX_WIDTH, par->crtc.dp_pix_width, par);
	aty_st_le32(DP_CHAIN_MASK, par->crtc.dp_chain_mask, par);

	wait_for_fifo(5, par);
 	aty_st_le32(SCALE_3D_CNTL, 0, par);
	aty_st_le32(Z_CNTL, 0, par);
	aty_st_le32(CRTC_INT_CNTL, aty_ld_le32(CRTC_INT_CNTL, par) & ~0x20,
		    par);
	aty_st_le32(GUI_TRAJ_CNTL, 0x100023, par);

	/* insure engine is idle before leaving */
	wait_for_idle(par);
}

    /*
     *  Accelerated functions
     */

static inline void draw_rect(s16 x, s16 y, u16 width, u16 height,
			     struct atyfb_par *par)
{
	/* perform rectangle fill */
	wait_for_fifo(2, par);
	aty_st_le32(DST_Y_X, (x << 16) | y, par);
	aty_st_le32(DST_HEIGHT_WIDTH, (width << 16) | height, par);
	par->blitter_may_be_busy = 1;
}

void atyfb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 dy = area->dy, sy = area->sy, direction = DST_LAST_PEL;
	u32 sx = area->sx, dx = area->dx, width = area->width, rotation = 0;

	if (par->asleep)
		return;
	if (!area->width || !area->height)
		return;
	if (!par->accel_flags) {
		cfb_copyarea(info, area);
		return;
	}

	if (info->var.bits_per_pixel == 24) {
		/* In 24 bpp, the engine is in 8 bpp - this requires that all */
		/* horizontal coordinates and widths must be adjusted */
		sx *= 3;
		dx *= 3;
		width *= 3;
	}

	if (area->sy < area->dy) {
		dy += area->height - 1;
		sy += area->height - 1;
	} else
		direction |= DST_Y_TOP_TO_BOTTOM;

	if (sx < dx) {
		dx += width - 1;
		sx += width - 1;
	} else
		direction |= DST_X_LEFT_TO_RIGHT;

	if (info->var.bits_per_pixel == 24) {
		rotation = rotation24bpp(dx, direction);
	}

	wait_for_fifo(5, par);
	aty_st_le32(DP_PIX_WIDTH, par->crtc.dp_pix_width, par);
	aty_st_le32(DP_SRC, FRGD_SRC_BLIT, par);
	aty_st_le32(SRC_Y_X, (sx << 16) | sy, par);
	aty_st_le32(SRC_HEIGHT1_WIDTH1, (width << 16) | area->height, par);
	aty_st_le32(DST_CNTL, direction | rotation, par);
	draw_rect(dx, dy, width, area->height, par);
}

void atyfb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 color, dx = rect->dx, width = rect->width, rotation = 0;

	if (par->asleep)
		return;
	if (!rect->width || !rect->height)
		return;
	if (!par->accel_flags) {
		cfb_fillrect(info, rect);
		return;
	}

	if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR)
		color = ((u32 *)(info->pseudo_palette))[rect->color];
	else
		color = rect->color;

	if (info->var.bits_per_pixel == 24) {
		/* In 24 bpp, the engine is in 8 bpp - this requires that all */
		/* horizontal coordinates and widths must be adjusted */
		dx *= 3;
		width *= 3;
		rotation = rotation24bpp(dx, DST_X_LEFT_TO_RIGHT);
	}

	wait_for_fifo(4, par);
	aty_st_le32(DP_PIX_WIDTH, par->crtc.dp_pix_width, par);
	aty_st_le32(DP_FRGD_CLR, color, par);
	aty_st_le32(DP_SRC,
		    BKGD_SRC_BKGD_CLR | FRGD_SRC_FRGD_CLR | MONO_SRC_ONE,
		    par);
	aty_st_le32(DST_CNTL,
		    DST_LAST_PEL | DST_Y_TOP_TO_BOTTOM |
		    DST_X_LEFT_TO_RIGHT | rotation, par);
	draw_rect(dx, rect->dy, width, rect->height, par);
}

void atyfb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 src_bytes, dx = image->dx, dy = image->dy, width = image->width;
	u32 pix_width, rotation = 0, src, mix;

	if (par->asleep)
		return;
	if (!image->width || !image->height)
		return;
	if (!par->accel_flags ||
	    (image->depth != 1 && info->var.bits_per_pixel != image->depth)) {
		cfb_imageblit(info, image);
		return;
	}

	pix_width = par->crtc.dp_pix_width;

	switch (image->depth) {
	case 1:
	    pix_width &= ~(BYTE_ORDER_MASK | HOST_MASK);
	    pix_width |= (BYTE_ORDER_MSB_TO_LSB | HOST_1BPP);
	    break;
	case 4:
	    pix_width &= ~(BYTE_ORDER_MASK | HOST_MASK);
	    pix_width |= (BYTE_ORDER_MSB_TO_LSB | HOST_4BPP);
	    break;
	case 8:
	    pix_width &= ~HOST_MASK;
	    pix_width |= HOST_8BPP;
	    break;
	case 15:
	    pix_width &= ~HOST_MASK;
	    pix_width |= HOST_15BPP;
	    break;
	case 16:
	    pix_width &= ~HOST_MASK;
	    pix_width |= HOST_16BPP;
	    break;
	case 24:
	    pix_width &= ~HOST_MASK;
	    pix_width |= HOST_24BPP;
	    break;
	case 32:
	    pix_width &= ~HOST_MASK;
	    pix_width |= HOST_32BPP;
	    break;
	}

	if (info->var.bits_per_pixel == 24) {
		/* In 24 bpp, the engine is in 8 bpp - this requires that all */
		/* horizontal coordinates and widths must be adjusted */
		dx *= 3;
		width *= 3;

		rotation = rotation24bpp(dx, DST_X_LEFT_TO_RIGHT);

		pix_width &= ~DST_MASK;
		pix_width |= DST_8BPP;

		/*
		 * since Rage 3D IIc we have DP_HOST_TRIPLE_EN bit
		 * this hwaccelerated triple has an issue with not aligned data
		 */
		if (image->depth == 1 && M64_HAS(HW_TRIPLE) && image->width % 8 == 0)
			pix_width |= DP_HOST_TRIPLE_EN;
	}

	if (image->depth == 1) {
		u32 fg, bg;
		if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
		    info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
			fg = ((u32*)(info->pseudo_palette))[image->fg_color];
			bg = ((u32*)(info->pseudo_palette))[image->bg_color];
		} else {
			fg = image->fg_color;
			bg = image->bg_color;
		}

		wait_for_fifo(2, par);
		aty_st_le32(DP_BKGD_CLR, bg, par);
		aty_st_le32(DP_FRGD_CLR, fg, par);
		src = MONO_SRC_HOST | FRGD_SRC_FRGD_CLR | BKGD_SRC_BKGD_CLR;
		mix = FRGD_MIX_S | BKGD_MIX_S;
	} else {
		src = MONO_SRC_ONE | FRGD_SRC_HOST;
		mix = FRGD_MIX_D_XOR_S | BKGD_MIX_D;
	}

	wait_for_fifo(5, par);
	aty_st_le32(DP_PIX_WIDTH, pix_width, par);
	aty_st_le32(DP_MIX, mix, par);
	aty_st_le32(DP_SRC, src, par);
	aty_st_le32(HOST_CNTL, HOST_BYTE_ALIGN, par);
	aty_st_le32(DST_CNTL, DST_Y_TOP_TO_BOTTOM | DST_X_LEFT_TO_RIGHT | rotation, par);

	draw_rect(dx, dy, width, image->height, par);
	src_bytes = (((image->width * image->depth) + 7) / 8) * image->height;

	/* manual triple each pixel */
	if (image->depth == 1 && info->var.bits_per_pixel == 24 && !(pix_width & DP_HOST_TRIPLE_EN)) {
		int inbit, outbit, mult24, byte_id_in_dword, width;
		u8 *pbitmapin = (u8*)image->data, *pbitmapout;
		u32 hostdword;

		for (width = image->width, inbit = 7, mult24 = 0; src_bytes; ) {
			for (hostdword = 0, pbitmapout = (u8*)&hostdword, byte_id_in_dword = 0;
				byte_id_in_dword < 4 && src_bytes;
				byte_id_in_dword++, pbitmapout++) {
				for (outbit = 7; outbit >= 0; outbit--) {
					*pbitmapout |= (((*pbitmapin >> inbit) & 1) << outbit);
					mult24++;
					/* next bit */
					if (mult24 == 3) {
						mult24 = 0;
						inbit--;
						width--;
					}

					/* next byte */
					if (inbit < 0 || width == 0) {
						src_bytes--;
						pbitmapin++;
						inbit = 7;

						if (width == 0) {
						    width = image->width;
						    outbit = 0;
						}
					}
				}
			}
			wait_for_fifo(1, par);
			aty_st_le32(HOST_DATA0, le32_to_cpu(hostdword), par);
		}
	} else {
		u32 *pbitmap, dwords = (src_bytes + 3) / 4;
		for (pbitmap = (u32*)(image->data); dwords; dwords--, pbitmap++) {
			wait_for_fifo(1, par);
			aty_st_le32(HOST_DATA0, get_unaligned_le32(pbitmap), par);
		}
	}
}
