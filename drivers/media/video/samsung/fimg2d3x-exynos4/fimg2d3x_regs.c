/* drivers/media/video/samsung/fimg2d3x/fimg2d3x_regs.c
 *
 * Copyright  2010 Samsung Electronics Co, Ltd. All Rights Reserved. 
 *		      http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file implements fimg2d register control functions.
 */

#include <mach/map.h>
#include <asm/io.h>
#include <mach/regs-fimg2d3x.h>

#include "fimg2d3x_regs.h"
#include "fimg2d.h"

void get_rot_config(unsigned int rotate_value, u32 *rot, u32 *src_dir, u32 *dst_dir)
{
	switch(rotate_value) {
	case G2D_ROT_90: 
		*rot = 1;   	/* rotation = 1, src_y_dir == dst_y_dir, src_x_dir == dst_x_dir */
		*src_dir = 0;		
		*dst_dir = 0;		
		break;

	case G2D_ROT_270: 
		*rot = 1;   	/* rotation = 1, src_y_dir != dst_y_dir, src_x_dir != dst_x_dir */
		*src_dir = 0;
		*dst_dir = 0x3;
		break;			

	case G2D_ROT_180: 
		*rot = 0;    	/* rotation = 0, src_y_dir != dst_y_dir, src_x_dir != dst_x_dir */
		*src_dir = 0;
		*dst_dir = 0x3;
		break;

	case G2D_ROT_X_FLIP: 
		*rot = 0;    	/* rotation = 0, src_y_dir != dst_y_dir */
		*src_dir = 0;
		*dst_dir = 0x2;
		break;

	case G2D_ROT_Y_FLIP: 
		*rot = 0;    	/* rotation = 0, src_x_dir != dst_y_dir */
		*src_dir = 0;
		*dst_dir = 0x1;
		break;

	default :
		*rot = 0;   	/* rotation = 0; */
		*src_dir = 0;
		*dst_dir = 0;
		break;
	}
	
	return ;
}

int g2d_check_params(g2d_params *params)
{
	g2d_rect * src_rect = &params->src_rect;
	g2d_rect * dst_rect = &params->dst_rect;
	g2d_flag * flag     = &params->flag;

	/* source */
	if (0 > src_rect->x || 0 > src_rect->y) {
		return -1;
	}

	if (0 == src_rect->h || 0 == src_rect->w) {
		return -1;
	}

	if (8000 < src_rect->x+src_rect->w || 8000 < src_rect->y+src_rect->h) {
		return -1;
	}

	/* destination */
	if (0 > dst_rect->x || 0 > dst_rect->y) {
		return -1;
	}

	if (0 == dst_rect->h || 0 == dst_rect->w) {
		return -1;
	}

	if (8000 < dst_rect->x+dst_rect->w || 8000 < dst_rect->y+dst_rect->h) {
		return -1;
	}

	if (flag->alpha_val > G2D_ALPHA_BLENDING_OPAQUE) {
		return -1; 
	}

	return 0;
}

void g2d_check_fifo_state_wait(struct g2d_global *g2d_dev)
{
	/* 1 = The graphics engine finishes the execution of command. */
	/* 0 = in the middle of rendering process. */
	while(!(readl(g2d_dev->base + FIFO_STAT_REG) & 0x1));

	return;
}


u32 g2d_set_src_img(struct g2d_global *g2d_dev, g2d_rect * rect, g2d_flag * flag)
{
	u32 data    = 0;
	u32 blt_cmd = 0;

	/* set  source to one color */
	//if(rect == NULL)
	if (flag->potterduff_mode == G2D_Clear_Mode) {
		/* select source */
		writel(G2D_SRC_SELECT_R_USE_FG_COLOR, g2d_dev->base + SRC_SELECT_REG);

		/* foreground color */
		// writel(flag->src_color, g2d_dev->base + FG_COLOR_REG);		
		writel(0, g2d_dev->base + FG_COLOR_REG);	
	} else {
		/* select source */
		writel(G2D_SRC_SELECT_R_NORMAL, g2d_dev->base + SRC_SELECT_REG);

		/* set base address of source image */
		writel((u32)rect->addr,   g2d_dev->base + SRC_BASE_ADDR_REG);

		/* set stride */
		writel(rect->full_w * rect->bytes_per_pixel, g2d_dev->base + SRC_STRIDE_REG);

		/* set color mode */
		writel(rect->color_format, g2d_dev->base + SRC_COLOR_MODE_REG);

		/* set coordinate of source image */
		data = (rect->y << 16) | (rect->x);
		writel(data, g2d_dev->base + SRC_LEFT_TOP_REG);

		data = ((rect->y + rect->h) << 16) | (rect->x + rect->w);
		writel(data, g2d_dev->base + SRC_RIGHT_BOTTOM_REG);
        
	}

	return blt_cmd;
}

u32 g2d_set_dst_img(struct g2d_global *g2d_dev, g2d_rect * rect)
{
	u32 data    = 0;
	u32 blt_cmd = 0;
	
	/* select destination */
	writel(G2D_DST_SELECT_R_NORMAL, g2d_dev->base + DST_SELECT_REG);

	/* set base address of destination image */
	writel((u32)rect->addr,   g2d_dev->base + DST_BASE_ADDR_REG);

	/* set stride */
	writel(rect->full_w * rect->bytes_per_pixel, g2d_dev->base + DST_STRIDE_REG);

	/* set color mode */
	writel(rect->color_format, g2d_dev->base + DST_COLOR_MODE_REG);

	/* set coordinate of destination image */
	data = (rect->y << 16) | (rect->x);
	writel(data, g2d_dev->base + DST_LEFT_TOP_REG);

	data = ((rect->y + rect->h) << 16) | (rect->x + rect->w);
	writel(data, g2d_dev->base + DST_RIGHT_BOTTOM_REG);

	return blt_cmd;
}

u32 g2d_set_rotation(struct g2d_global *g2d_dev, g2d_flag * flag)
{
	u32 blt_cmd = 0;
	u32 rot=0, src_dir=0, dst_dir=0;

	get_rot_config(flag->rotate_val, &rot, &src_dir, &dst_dir);

	writel(rot,     g2d_dev->base + ROTATE_REG);
	writel(src_dir, g2d_dev->base + SRC_MSK_DIRECT_REG);
	writel(dst_dir, g2d_dev->base + DST_PAT_DIRECT_REG);

	return blt_cmd;
}

u32 g2d_set_clip_win(struct g2d_global *g2d_dev, g2d_clip * clip)
{
	u32 blt_cmd = 0;

        //blt_cmd |= G2D_BLT_CMD_R_CW_ENABLE;
	writel((clip->t << 16) | (clip->l), g2d_dev->base + CW_LEFT_TOP_REG);
	writel((clip->b << 16) | (clip->r), g2d_dev->base + CW_RIGHT_BOTTOM_REG);

	return blt_cmd;
}

u32 g2d_set_color_key(struct g2d_global *g2d_dev, g2d_flag * flag)
{
	u32 blt_cmd = 0;

	/* Transparent Selection */
	switch(flag->blue_screen_mode) {
	case G2D_BLUE_SCREEN_TRANSPARENT :
		writel(flag->color_key_val, g2d_dev->base + BS_COLOR_REG);

		blt_cmd |= G2D_BLT_CMD_R_TRANSPARENT_MODE_TRANS;
		break;

	case G2D_BLUE_SCREEN_WITH_COLOR :
		writel(flag->color_switch_val,     g2d_dev->base + BG_COLOR_REG);
		writel(flag->color_key_val, g2d_dev->base + BS_COLOR_REG);

		blt_cmd |= G2D_BLT_CMD_R_TRANSPARENT_MODE_BLUESCR;
		break;

	case G2D_BLUE_SCREEN_NONE :
	default:
		blt_cmd |= G2D_BLT_CMD_R_TRANSPARENT_MODE_OPAQUE;
		break;
	}

	blt_cmd |= G2D_BLT_CMD_R_COLOR_KEY_DISABLE;

	return blt_cmd;
}

u32 g2d_set_pattern(struct g2d_global *g2d_dev, g2d_rect * rect, g2d_flag * flag)
{
	u32 data    = 0;
	u32 blt_cmd = 0;

	/* Third Operand Selection */
	switch(flag->third_op_mode) {	
	case G2D_THIRD_OP_PATTERN :
		/* set base address of pattern image */
		writel((u32)rect->addr, g2d_dev->base + PAT_BASE_ADDR_REG);

		/* set size of pattern image */
		data =   ((rect->y + rect->h) << 16) | (rect->x + rect->w);
		writel(data, g2d_dev->base + PAT_SIZE_REG);

		/* set stride */
		writel(rect->full_w * rect->bytes_per_pixel, g2d_dev->base + PAT_STRIDE_REG);

		/* set color mode */
		writel(rect->color_format, g2d_dev->base + PAT_COLOR_MODE_REG);

		data =   (rect->y << 16) | rect->x;
		writel(data, g2d_dev->base + PAT_OFFSET_REG);

		data = G2D_THIRD_OP_REG_PATTERN;
		break;
	case G2D_THIRD_OP_FG :
		data = G2D_THIRD_OP_REG_FG_COLOR;
		break;
	case G2D_THIRD_OP_BG :
		data = G2D_THIRD_OP_REG_BG_COLOR;
		break;
	case G2D_THIRD_OP_NONE :
	default:
		data = G2D_THIRD_OP_REG_NONE;
		break;
	}

	writel(data, g2d_dev->base + THIRD_OPERAND_REG);
	
	if(flag->third_op_mode == G2D_THIRD_OP_NONE) {
		data = ((G2D_ROP_REG_SRC << 8) | G2D_ROP_REG_SRC);
	} else {
		switch(flag->rop_mode) {	
		case G2D_ROP_DST:
			data = ((G2D_ROP_REG_DST << 8) | G2D_ROP_REG_DST);
			break;
		case G2D_ROP_SRC_AND_DST:
			data = ((G2D_ROP_REG_SRC_AND_DST << 8) | G2D_ROP_REG_SRC_AND_DST);
			break;
		case G2D_ROP_SRC_OR_DST:
			data = ((G2D_ROP_REG_SRC_OR_DST << 8) | G2D_ROP_REG_SRC_OR_DST);
			break;
		case G2D_ROP_3RD_OPRND:
			data = ((G2D_ROP_REG_3RD_OPRND << 8) | G2D_ROP_REG_3RD_OPRND);
			break;
		case G2D_ROP_SRC_AND_3RD_OPRND:
			data = ((G2D_ROP_REG_SRC_AND_3RD_OPRND << 8) | G2D_ROP_REG_SRC_AND_3RD_OPRND);
			break;
		case G2D_ROP_SRC_OR_3RD_OPRND:
			data = ((G2D_ROP_REG_SRC_OR_3RD_OPRND << 8) | G2D_ROP_REG_SRC_OR_3RD_OPRND);
			break;
		case G2D_ROP_SRC_XOR_3RD_OPRND:
			data = ((G2D_ROP_REG_SRC_XOR_3RD_OPRND << 8) | G2D_ROP_REG_SRC_XOR_3RD_OPRND);
			break;
		case G2D_ROP_DST_OR_3RD:
			data = ((G2D_ROP_REG_DST_OR_3RD_OPRND << 8) | G2D_ROP_REG_DST_OR_3RD_OPRND);
			break;
		case G2D_ROP_SRC:
		default:
			data = ((G2D_ROP_REG_SRC << 8) | G2D_ROP_REG_SRC);
			break;
		}
	}
	writel(data, g2d_dev->base + ROP4_REG);

	/* Mask Operation */
	if(flag->mask_mode == TRUE) {
		writel((u32)rect->addr, g2d_dev->base + MASK_BASE_ADDR_REG);
		writel(rect->full_w * rect->bytes_per_pixel, g2d_dev->base + MASK_STRIDE_REG);

		blt_cmd |= G2D_BLT_CMD_R_MASK_ENABLE;
	}

	return blt_cmd;
}

u32 g2d_set_alpha(struct g2d_global *g2d_dev, g2d_flag * flag)
{
	u32 blt_cmd = 0;

	/* Alpha Value */
	if(flag->alpha_val <= G2D_ALPHA_VALUE_MAX) {
		if ((flag->potterduff_mode == G2D_Clear_Mode) || (flag->potterduff_mode == G2D_Src_Mode))
			blt_cmd |= G2D_BLT_CMD_R_ALPHA_BLEND_NONE;
		else
			blt_cmd |= G2D_BLT_CMD_R_ALPHA_BLEND_ALPHA_BLEND;
		writel((flag->alpha_val & 0xff), g2d_dev->base + ALPHA_REG);
	} else {
		blt_cmd |= G2D_BLT_CMD_R_ALPHA_BLEND_NONE;
	}

	return blt_cmd;
}

void g2d_set_bitblt_cmd(struct g2d_global *g2d_dev, g2d_rect * src_rect, g2d_rect * dst_rect, g2d_clip * clip, u32 blt_cmd)
{
	if ((src_rect->w  != dst_rect->w)
		|| (src_rect->h  != dst_rect->h)) {
		blt_cmd |= G2D_BLT_CMD_R_STRETCH_ENABLE;
	}
	
	if ((clip->t != dst_rect->y) || (clip->b != dst_rect->y + dst_rect->h) 
		|| (clip->l != dst_rect->x) || (clip->r != dst_rect->x + dst_rect->w)) {
		blt_cmd |= G2D_BLT_CMD_R_CW_ENABLE;
	}
	writel(blt_cmd, g2d_dev->base + BITBLT_COMMAND_REG);
}

void g2d_reset(struct g2d_global *g2d_dev)
{
	writel(G2D_SWRESET_R_RESET, g2d_dev->base + SOFT_RESET_REG);
}

void g2d_disable_int(struct g2d_global *g2d_dev)
{
	writel(G2D_INTEN_R_CF_DISABLE, g2d_dev->base + INTEN_REG);
}

void g2d_set_int_finish(struct g2d_global *g2d_dev)
{
	writel(G2D_INTC_PEND_R_INTP_CMD_FIN, g2d_dev->base + INTC_PEND_REG);
}

void g2d_start_bitblt(struct g2d_global *g2d_dev, g2d_params *params)
{
	if (!(params->flag.render_mode & G2D_POLLING)) {
		writel(G2D_INTEN_R_CF_ENABLE, g2d_dev->base + INTEN_REG);
	}
	writel(0x7, g2d_dev->base + CACHECTL_REG);

	writel(G2D_BITBLT_R_START, g2d_dev->base + BITBLT_START_REG);
}

