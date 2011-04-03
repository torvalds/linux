/* drivers/video/msm/mdp_ppp.c
 *
 * Copyright (C) 2007 QUALCOMM Incorporated
 * Copyright (C) 2007 Google Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/fb.h>
#include <linux/file.h>
#include <linux/delay.h>
#include <linux/msm_mdp.h>
#include <mach/msm_fb.h>

#include "mdp_hw.h"
#include "mdp_scale_tables.h"

#define DLOG(x...) do {} while (0)

#define MDP_DOWNSCALE_BLUR (MDP_DOWNSCALE_MAX + 1)
static int downscale_y_table = MDP_DOWNSCALE_MAX;
static int downscale_x_table = MDP_DOWNSCALE_MAX;

struct mdp_regs {
	uint32_t src0;
	uint32_t src1;
	uint32_t dst0;
	uint32_t dst1;
	uint32_t src_cfg;
	uint32_t dst_cfg;
	uint32_t src_pack;
	uint32_t dst_pack;
	uint32_t src_rect;
	uint32_t dst_rect;
	uint32_t src_ystride;
	uint32_t dst_ystride;
	uint32_t op;
	uint32_t src_bpp;
	uint32_t dst_bpp;
	uint32_t edge;
	uint32_t phasex_init;
	uint32_t phasey_init;
	uint32_t phasex_step;
	uint32_t phasey_step;
};

static uint32_t pack_pattern[] = {
	PPP_ARRAY0(PACK_PATTERN)
};

static uint32_t src_img_cfg[] = {
	PPP_ARRAY1(CFG, SRC)
};

static uint32_t dst_img_cfg[] = {
	PPP_ARRAY1(CFG, DST)
};

static uint32_t bytes_per_pixel[] = {
	[MDP_RGB_565] = 2,
	[MDP_RGB_888] = 3,
	[MDP_XRGB_8888] = 4,
	[MDP_ARGB_8888] = 4,
	[MDP_RGBA_8888] = 4,
	[MDP_BGRA_8888] = 4,
	[MDP_RGBX_8888] = 4,
	[MDP_Y_CBCR_H2V1] = 1,
	[MDP_Y_CBCR_H2V2] = 1,
	[MDP_Y_CRCB_H2V1] = 1,
	[MDP_Y_CRCB_H2V2] = 1,
	[MDP_YCRYCB_H2V1] = 2
};

static uint32_t dst_op_chroma[] = {
	PPP_ARRAY1(CHROMA_SAMP, DST)
};

static uint32_t src_op_chroma[] = {
	PPP_ARRAY1(CHROMA_SAMP, SRC)
};

static uint32_t bg_op_chroma[] = {
	PPP_ARRAY1(CHROMA_SAMP, BG)
};

static void rotate_dst_addr_x(struct mdp_blit_req *req, struct mdp_regs *regs)
{
	regs->dst0 += (req->dst_rect.w -
		       min((uint32_t)16, req->dst_rect.w)) * regs->dst_bpp;
	regs->dst1 += (req->dst_rect.w -
		       min((uint32_t)16, req->dst_rect.w)) * regs->dst_bpp;
}

static void rotate_dst_addr_y(struct mdp_blit_req *req, struct mdp_regs *regs)
{
	regs->dst0 += (req->dst_rect.h -
		       min((uint32_t)16, req->dst_rect.h)) *
		       regs->dst_ystride;
	regs->dst1 += (req->dst_rect.h -
		       min((uint32_t)16, req->dst_rect.h)) *
		       regs->dst_ystride;
}

static void blit_rotate(struct mdp_blit_req *req,
			struct mdp_regs *regs)
{
	if (req->flags == MDP_ROT_NOP)
		return;

	regs->op |= PPP_OP_ROT_ON;
	if ((req->flags & MDP_ROT_90 || req->flags & MDP_FLIP_LR) &&
	    !(req->flags & MDP_ROT_90 && req->flags & MDP_FLIP_LR))
		rotate_dst_addr_x(req, regs);
	if (req->flags & MDP_ROT_90)
		regs->op |= PPP_OP_ROT_90;
	if (req->flags & MDP_FLIP_UD) {
		regs->op |= PPP_OP_FLIP_UD;
		rotate_dst_addr_y(req, regs);
	}
	if (req->flags & MDP_FLIP_LR)
		regs->op |= PPP_OP_FLIP_LR;
}

static void blit_convert(struct mdp_blit_req *req, struct mdp_regs *regs)
{
	if (req->src.format == req->dst.format)
		return;
	if (IS_RGB(req->src.format) && IS_YCRCB(req->dst.format)) {
		regs->op |= PPP_OP_CONVERT_RGB2YCBCR | PPP_OP_CONVERT_ON;
	} else if (IS_YCRCB(req->src.format) && IS_RGB(req->dst.format)) {
		regs->op |= PPP_OP_CONVERT_YCBCR2RGB | PPP_OP_CONVERT_ON;
		if (req->dst.format == MDP_RGB_565)
			regs->op |= PPP_OP_CONVERT_MATRIX_SECONDARY;
	}
}

#define GET_BIT_RANGE(value, high, low) \
	(((1 << (high - low + 1)) - 1) & (value >> low))
static uint32_t transp_convert(struct mdp_blit_req *req)
{
	uint32_t transp = 0;
	if (req->src.format == MDP_RGB_565) {
		/* pad each value to 8 bits by copying the high bits into the
		 * low end, convert RGB to RBG by switching low 2 components */
		transp |= ((GET_BIT_RANGE(req->transp_mask, 15, 11) << 3) |
			   (GET_BIT_RANGE(req->transp_mask, 15, 13))) << 16;

		transp |= ((GET_BIT_RANGE(req->transp_mask, 4, 0) << 3) |
			   (GET_BIT_RANGE(req->transp_mask, 4, 2))) << 8;

		transp |= (GET_BIT_RANGE(req->transp_mask, 10, 5) << 2) |
			  (GET_BIT_RANGE(req->transp_mask, 10, 9));
	} else {
		/* convert RGB to RBG */
		transp |= (GET_BIT_RANGE(req->transp_mask, 15, 8)) |
			  (GET_BIT_RANGE(req->transp_mask, 23, 16) << 16) |
			  (GET_BIT_RANGE(req->transp_mask, 7, 0) << 8);
	}
	return transp;
}
#undef GET_BIT_RANGE

static void blit_blend(struct mdp_blit_req *req, struct mdp_regs *regs)
{
	/* TRANSP BLEND */
	if (req->transp_mask != MDP_TRANSP_NOP) {
		req->transp_mask = transp_convert(req);
		if (req->alpha != MDP_ALPHA_NOP) {
			/* use blended transparancy mode
			 * pixel = (src == transp) ? dst : blend
			 * blend is combo of blend_eq_sel and
			 * blend_alpha_sel */
			regs->op |= PPP_OP_ROT_ON | PPP_OP_BLEND_ON |
				PPP_OP_BLEND_ALPHA_BLEND_NORMAL |
				PPP_OP_BLEND_CONSTANT_ALPHA |
				PPP_BLEND_ALPHA_TRANSP;
		} else {
			/* simple transparancy mode
			 * pixel = (src == transp) ? dst : src */
			regs->op |= PPP_OP_ROT_ON | PPP_OP_BLEND_ON |
				PPP_OP_BLEND_SRCPIXEL_TRANSP;
		}
	}

	req->alpha &= 0xff;
	/* ALPHA BLEND */
	if (HAS_ALPHA(req->src.format)) {
		regs->op |= PPP_OP_ROT_ON | PPP_OP_BLEND_ON |
			PPP_OP_BLEND_SRCPIXEL_ALPHA;
	} else if (req->alpha < MDP_ALPHA_NOP) {
		/* just blend by alpha */
		regs->op |= PPP_OP_ROT_ON | PPP_OP_BLEND_ON |
			PPP_OP_BLEND_ALPHA_BLEND_NORMAL |
			PPP_OP_BLEND_CONSTANT_ALPHA;
	}

	regs->op |= bg_op_chroma[req->dst.format];
}

#define ONE_HALF	(1LL << 32)
#define ONE		(1LL << 33)
#define TWO		(2LL << 33)
#define THREE		(3LL << 33)
#define FRAC_MASK (ONE - 1)
#define INT_MASK (~FRAC_MASK)

static int scale_params(uint32_t dim_in, uint32_t dim_out, uint32_t origin,
			uint32_t *phase_init, uint32_t *phase_step)
{
	/* to improve precicsion calculations are done in U31.33 and converted
	 * to U3.29 at the end */
	int64_t k1, k2, k3, k4, tmp;
	uint64_t n, d, os, os_p, od, od_p, oreq;
	unsigned rpa = 0;
	int64_t ip64, delta;

	if (dim_out % 3 == 0)
		rpa = !(dim_in % (dim_out / 3));

	n = ((uint64_t)dim_out) << 34;
	d = dim_in;
	if (!d)
		return -1;
	do_div(n, d);
	k3 = (n + 1) >> 1;
	if ((k3 >> 4) < (1LL << 27) || (k3 >> 4) > (1LL << 31)) {
		DLOG("crap bad scale\n");
		return -1;
	}
	n = ((uint64_t)dim_in) << 34;
	d = (uint64_t)dim_out;
	if (!d)
		return -1;
	do_div(n, d);
	k1 = (n + 1) >> 1;
	k2 = (k1 - ONE) >> 1;

	*phase_init = (int)(k2 >> 4);
	k4 = (k3 - ONE) >> 1;

	if (rpa) {
		os = ((uint64_t)origin << 33) - ONE_HALF;
		tmp = (dim_out * os) + ONE_HALF;
		if (!dim_in)
			return -1;
		do_div(tmp, dim_in);
		od = tmp - ONE_HALF;
	} else {
		os = ((uint64_t)origin << 1) - 1;
		od = (((k3 * os) >> 1) + k4);
	}

	od_p = od & INT_MASK;
	if (od_p != od)
		od_p += ONE;

	if (rpa) {
		tmp = (dim_in * od_p) + ONE_HALF;
		if (!dim_in)
			return -1;
		do_div(tmp, dim_in);
		os_p = tmp - ONE_HALF;
	} else {
		os_p = ((k1 * (od_p >> 33)) + k2);
	}

	oreq = (os_p & INT_MASK) - ONE;

	ip64 = os_p - oreq;
	delta = ((int64_t)(origin) << 33) - oreq;
	ip64 -= delta;
	/* limit to valid range before the left shift */
	delta = (ip64 & (1LL << 63)) ? 4 : -4;
	delta <<= 33;
	while (abs((int)(ip64 >> 33)) > 4)
		ip64 += delta;
	*phase_init = (int)(ip64 >> 4);
	*phase_step = (uint32_t)(k1 >> 4);
	return 0;
}

static void load_scale_table(const struct mdp_info *mdp,
			     struct mdp_table_entry *table, int len)
{
	int i;
	for (i = 0; i < len; i++)
		mdp_writel(mdp, table[i].val, table[i].reg);
}

enum {
IMG_LEFT,
IMG_RIGHT,
IMG_TOP,
IMG_BOTTOM,
};

static void get_edge_info(uint32_t src, uint32_t src_coord, uint32_t dst,
			  uint32_t *interp1, uint32_t *interp2,
			  uint32_t *repeat1, uint32_t *repeat2) {
	if (src > 3 * dst) {
		*interp1 = 0;
		*interp2 = src - 1;
		*repeat1 = 0;
		*repeat2 = 0;
	} else if (src == 3 * dst) {
		*interp1 = 0;
		*interp2 = src;
		*repeat1 = 0;
		*repeat2 = 1;
	} else if (src > dst && src < 3 * dst) {
		*interp1 = -1;
		*interp2 = src;
		*repeat1 = 1;
		*repeat2 = 1;
	} else if (src == dst) {
		*interp1 = -1;
		*interp2 = src + 1;
		*repeat1 = 1;
		*repeat2 = 2;
	} else {
		*interp1 = -2;
		*interp2 = src + 1;
		*repeat1 = 2;
		*repeat2 = 2;
	}
	*interp1 += src_coord;
	*interp2 += src_coord;
}

static int get_edge_cond(struct mdp_blit_req *req, struct mdp_regs *regs)
{
	int32_t luma_interp[4];
	int32_t luma_repeat[4];
	int32_t chroma_interp[4];
	int32_t chroma_bound[4];
	int32_t chroma_repeat[4];
	uint32_t dst_w, dst_h;

	memset(&luma_interp, 0, sizeof(int32_t) * 4);
	memset(&luma_repeat, 0, sizeof(int32_t) * 4);
	memset(&chroma_interp, 0, sizeof(int32_t) * 4);
	memset(&chroma_bound, 0, sizeof(int32_t) * 4);
	memset(&chroma_repeat, 0, sizeof(int32_t) * 4);
	regs->edge = 0;

	if (req->flags & MDP_ROT_90) {
		dst_w = req->dst_rect.h;
		dst_h = req->dst_rect.w;
	} else {
		dst_w = req->dst_rect.w;
		dst_h = req->dst_rect.h;
	}

	if (regs->op & (PPP_OP_SCALE_Y_ON | PPP_OP_SCALE_X_ON)) {
		get_edge_info(req->src_rect.h, req->src_rect.y, dst_h,
			      &luma_interp[IMG_TOP], &luma_interp[IMG_BOTTOM],
			      &luma_repeat[IMG_TOP], &luma_repeat[IMG_BOTTOM]);
		get_edge_info(req->src_rect.w, req->src_rect.x, dst_w,
			      &luma_interp[IMG_LEFT], &luma_interp[IMG_RIGHT],
			      &luma_repeat[IMG_LEFT], &luma_repeat[IMG_RIGHT]);
	} else {
		luma_interp[IMG_LEFT] = req->src_rect.x;
		luma_interp[IMG_RIGHT] = req->src_rect.x + req->src_rect.w - 1;
		luma_interp[IMG_TOP] = req->src_rect.y;
		luma_interp[IMG_BOTTOM] = req->src_rect.y + req->src_rect.h - 1;
		luma_repeat[IMG_LEFT] = 0;
		luma_repeat[IMG_TOP] = 0;
		luma_repeat[IMG_RIGHT] = 0;
		luma_repeat[IMG_BOTTOM] = 0;
	}

	chroma_interp[IMG_LEFT] = luma_interp[IMG_LEFT];
	chroma_interp[IMG_RIGHT] = luma_interp[IMG_RIGHT];
	chroma_interp[IMG_TOP] = luma_interp[IMG_TOP];
	chroma_interp[IMG_BOTTOM] = luma_interp[IMG_BOTTOM];

	chroma_bound[IMG_LEFT] = req->src_rect.x;
	chroma_bound[IMG_RIGHT] = req->src_rect.x + req->src_rect.w - 1;
	chroma_bound[IMG_TOP] = req->src_rect.y;
	chroma_bound[IMG_BOTTOM] = req->src_rect.y + req->src_rect.h - 1;

	if (IS_YCRCB(req->src.format)) {
		chroma_interp[IMG_LEFT] = chroma_interp[IMG_LEFT] >> 1;
		chroma_interp[IMG_RIGHT] = (chroma_interp[IMG_RIGHT] + 1) >> 1;

		chroma_bound[IMG_LEFT] = chroma_bound[IMG_LEFT] >> 1;
		chroma_bound[IMG_RIGHT] = chroma_bound[IMG_RIGHT] >> 1;
	}

	if (req->src.format == MDP_Y_CBCR_H2V2 ||
	    req->src.format == MDP_Y_CRCB_H2V2) {
		chroma_interp[IMG_TOP] = (chroma_interp[IMG_TOP] - 1) >> 1;
		chroma_interp[IMG_BOTTOM] = (chroma_interp[IMG_BOTTOM] + 1)
					    >> 1;
		chroma_bound[IMG_TOP] = (chroma_bound[IMG_TOP] + 1) >> 1;
		chroma_bound[IMG_BOTTOM] = chroma_bound[IMG_BOTTOM] >> 1;
	}

	chroma_repeat[IMG_LEFT] = chroma_bound[IMG_LEFT] -
				  chroma_interp[IMG_LEFT];
	chroma_repeat[IMG_RIGHT] = chroma_interp[IMG_RIGHT] -
				  chroma_bound[IMG_RIGHT];
	chroma_repeat[IMG_TOP] = chroma_bound[IMG_TOP] -
				  chroma_interp[IMG_TOP];
	chroma_repeat[IMG_BOTTOM] = chroma_interp[IMG_BOTTOM] -
				  chroma_bound[IMG_BOTTOM];

	if (chroma_repeat[IMG_LEFT] < 0 || chroma_repeat[IMG_LEFT] > 3 ||
	    chroma_repeat[IMG_RIGHT] < 0 || chroma_repeat[IMG_RIGHT] > 3 ||
	    chroma_repeat[IMG_TOP] < 0 || chroma_repeat[IMG_TOP] > 3 ||
	    chroma_repeat[IMG_BOTTOM] < 0 || chroma_repeat[IMG_BOTTOM] > 3 ||
	    luma_repeat[IMG_LEFT] < 0 || luma_repeat[IMG_LEFT] > 3 ||
	    luma_repeat[IMG_RIGHT] < 0 || luma_repeat[IMG_RIGHT] > 3 ||
	    luma_repeat[IMG_TOP] < 0 || luma_repeat[IMG_TOP] > 3 ||
	    luma_repeat[IMG_BOTTOM] < 0 || luma_repeat[IMG_BOTTOM] > 3)
		return -1;

	regs->edge |= (chroma_repeat[IMG_LEFT] & 3) << MDP_LEFT_CHROMA;
	regs->edge |= (chroma_repeat[IMG_RIGHT] & 3) << MDP_RIGHT_CHROMA;
	regs->edge |= (chroma_repeat[IMG_TOP] & 3) << MDP_TOP_CHROMA;
	regs->edge |= (chroma_repeat[IMG_BOTTOM] & 3) << MDP_BOTTOM_CHROMA;
	regs->edge |= (luma_repeat[IMG_LEFT] & 3) << MDP_LEFT_LUMA;
	regs->edge |= (luma_repeat[IMG_RIGHT] & 3) << MDP_RIGHT_LUMA;
	regs->edge |= (luma_repeat[IMG_TOP] & 3) << MDP_TOP_LUMA;
	regs->edge |= (luma_repeat[IMG_BOTTOM] & 3) << MDP_BOTTOM_LUMA;
	return 0;
}

static int blit_scale(const struct mdp_info *mdp, struct mdp_blit_req *req,
		      struct mdp_regs *regs)
{
	uint32_t phase_init_x, phase_init_y, phase_step_x, phase_step_y;
	uint32_t scale_factor_x, scale_factor_y;
	uint32_t downscale;
	uint32_t dst_w, dst_h;

	if (req->flags & MDP_ROT_90) {
		dst_w = req->dst_rect.h;
		dst_h = req->dst_rect.w;
	} else {
		dst_w = req->dst_rect.w;
		dst_h = req->dst_rect.h;
	}
	if ((req->src_rect.w == dst_w)  && (req->src_rect.h == dst_h) &&
	    !(req->flags & MDP_BLUR)) {
		regs->phasex_init = 0;
		regs->phasey_init = 0;
		regs->phasex_step = 0;
		regs->phasey_step = 0;
		return 0;
	}

	if (scale_params(req->src_rect.w, dst_w, 1, &phase_init_x,
			 &phase_step_x) ||
	    scale_params(req->src_rect.h, dst_h, 1, &phase_init_y,
			 &phase_step_y))
		return -1;

	scale_factor_x = (dst_w * 10) / req->src_rect.w;
	scale_factor_y = (dst_h * 10) / req->src_rect.h;

	if (scale_factor_x > 8)
		downscale = MDP_DOWNSCALE_PT8TO1;
	else if (scale_factor_x > 6)
		downscale = MDP_DOWNSCALE_PT6TOPT8;
	else if (scale_factor_x > 4)
		downscale = MDP_DOWNSCALE_PT4TOPT6;
	else
		downscale = MDP_DOWNSCALE_PT2TOPT4;
	if (downscale != downscale_x_table) {
		load_scale_table(mdp, mdp_downscale_x_table[downscale], 64);
		downscale_x_table = downscale;
	}

	if (scale_factor_y > 8)
		downscale = MDP_DOWNSCALE_PT8TO1;
	else if (scale_factor_y > 6)
		downscale = MDP_DOWNSCALE_PT6TOPT8;
	else if (scale_factor_y > 4)
		downscale = MDP_DOWNSCALE_PT4TOPT6;
	else
		downscale = MDP_DOWNSCALE_PT2TOPT4;
	if (downscale != downscale_y_table) {
		load_scale_table(mdp, mdp_downscale_y_table[downscale], 64);
		downscale_y_table = downscale;
	}

	regs->phasex_init = phase_init_x;
	regs->phasey_init = phase_init_y;
	regs->phasex_step = phase_step_x;
	regs->phasey_step = phase_step_y;
	regs->op |= (PPP_OP_SCALE_Y_ON | PPP_OP_SCALE_X_ON);
	return 0;

}

static void blit_blur(const struct mdp_info *mdp, struct mdp_blit_req *req,
		      struct mdp_regs *regs)
{
	if (!(req->flags & MDP_BLUR))
		return;

	if (!(downscale_x_table == MDP_DOWNSCALE_BLUR &&
	      downscale_y_table == MDP_DOWNSCALE_BLUR)) {
		load_scale_table(mdp, mdp_gaussian_blur_table, 128);
		downscale_x_table = MDP_DOWNSCALE_BLUR;
		downscale_y_table = MDP_DOWNSCALE_BLUR;
	}

	regs->op |= (PPP_OP_SCALE_Y_ON | PPP_OP_SCALE_X_ON);
}


#define IMG_LEN(rect_h, w, rect_w, bpp) (((rect_h) * w) * bpp)

#define Y_TO_CRCB_RATIO(format) \
	((format == MDP_Y_CBCR_H2V2 || format == MDP_Y_CRCB_H2V2) ?  2 :\
	 (format == MDP_Y_CBCR_H2V1 || format == MDP_Y_CRCB_H2V1) ?  1 : 1)

static void get_len(struct mdp_img *img, struct mdp_rect *rect, uint32_t bpp,
		    uint32_t *len0, uint32_t *len1)
{
	*len0 = IMG_LEN(rect->h, img->width, rect->w, bpp);
	if (IS_PSEUDOPLNR(img->format))
		*len1 = *len0/Y_TO_CRCB_RATIO(img->format);
	else
		*len1 = 0;
}

static int valid_src_dst(unsigned long src_start, unsigned long src_len,
			 unsigned long dst_start, unsigned long dst_len,
			 struct mdp_blit_req *req, struct mdp_regs *regs)
{
	unsigned long src_min_ok = src_start;
	unsigned long src_max_ok = src_start + src_len;
	unsigned long dst_min_ok = dst_start;
	unsigned long dst_max_ok = dst_start + dst_len;
	uint32_t src0_len, src1_len, dst0_len, dst1_len;
	get_len(&req->src, &req->src_rect, regs->src_bpp, &src0_len,
		 &src1_len);
	get_len(&req->dst, &req->dst_rect, regs->dst_bpp, &dst0_len,
		 &dst1_len);

	if (regs->src0 < src_min_ok || regs->src0 > src_max_ok ||
	    regs->src0 + src0_len > src_max_ok) {
		DLOG("invalid_src %x %x %lx %lx\n", regs->src0,
		      src0_len, src_min_ok, src_max_ok);
		return 0;
	}
	if (regs->src_cfg & PPP_SRC_PLANE_PSEUDOPLNR) {
		if (regs->src1 < src_min_ok || regs->src1 > src_max_ok ||
		    regs->src1 + src1_len > src_max_ok) {
			DLOG("invalid_src1");
			return 0;
		}
	}
	if (regs->dst0 < dst_min_ok || regs->dst0 > dst_max_ok ||
	    regs->dst0 + dst0_len > dst_max_ok) {
		DLOG("invalid_dst");
		return 0;
	}
	if (regs->dst_cfg & PPP_SRC_PLANE_PSEUDOPLNR) {
		if (regs->dst1 < dst_min_ok || regs->dst1 > dst_max_ok ||
		    regs->dst1 + dst1_len > dst_max_ok) {
			DLOG("invalid_dst1");
			return 0;
		}
	}
	return 1;
}


static void flush_imgs(struct mdp_blit_req *req, struct mdp_regs *regs,
		       struct file *src_file, struct file *dst_file)
{
}

static void get_chroma_addr(struct mdp_img *img, struct mdp_rect *rect,
			    uint32_t base, uint32_t bpp, uint32_t cfg,
			    uint32_t *addr, uint32_t *ystride)
{
	uint32_t compress_v = Y_TO_CRCB_RATIO(img->format);
	uint32_t compress_h = 2;
	uint32_t  offset;

	if (IS_PSEUDOPLNR(img->format)) {
		offset = (rect->x / compress_h) * compress_h;
		offset += rect->y == 0 ? 0 :
			  ((rect->y + 1) / compress_v) * img->width;
		*addr = base + (img->width * img->height * bpp);
		*addr += offset * bpp;
		*ystride |= *ystride << 16;
	} else {
		*addr = 0;
	}
}

static int send_blit(const struct mdp_info *mdp, struct mdp_blit_req *req,
		     struct mdp_regs *regs, struct file *src_file,
		     struct file *dst_file)
{
	mdp_writel(mdp, 1, 0x060);
	mdp_writel(mdp, regs->src_rect, PPP_ADDR_SRC_ROI);
	mdp_writel(mdp, regs->src0, PPP_ADDR_SRC0);
	mdp_writel(mdp, regs->src1, PPP_ADDR_SRC1);
	mdp_writel(mdp, regs->src_ystride, PPP_ADDR_SRC_YSTRIDE);
	mdp_writel(mdp, regs->src_cfg, PPP_ADDR_SRC_CFG);
	mdp_writel(mdp, regs->src_pack, PPP_ADDR_SRC_PACK_PATTERN);

	mdp_writel(mdp, regs->op, PPP_ADDR_OPERATION);
	mdp_writel(mdp, regs->phasex_init, PPP_ADDR_PHASEX_INIT);
	mdp_writel(mdp, regs->phasey_init, PPP_ADDR_PHASEY_INIT);
	mdp_writel(mdp, regs->phasex_step, PPP_ADDR_PHASEX_STEP);
	mdp_writel(mdp, regs->phasey_step, PPP_ADDR_PHASEY_STEP);

	mdp_writel(mdp, (req->alpha << 24) | (req->transp_mask & 0xffffff),
	       PPP_ADDR_ALPHA_TRANSP);

	mdp_writel(mdp, regs->dst_cfg, PPP_ADDR_DST_CFG);
	mdp_writel(mdp, regs->dst_pack, PPP_ADDR_DST_PACK_PATTERN);
	mdp_writel(mdp, regs->dst_rect, PPP_ADDR_DST_ROI);
	mdp_writel(mdp, regs->dst0, PPP_ADDR_DST0);
	mdp_writel(mdp, regs->dst1, PPP_ADDR_DST1);
	mdp_writel(mdp, regs->dst_ystride, PPP_ADDR_DST_YSTRIDE);

	mdp_writel(mdp, regs->edge, PPP_ADDR_EDGE);
	if (regs->op & PPP_OP_BLEND_ON) {
		mdp_writel(mdp, regs->dst0, PPP_ADDR_BG0);
		mdp_writel(mdp, regs->dst1, PPP_ADDR_BG1);
		mdp_writel(mdp, regs->dst_ystride, PPP_ADDR_BG_YSTRIDE);
		mdp_writel(mdp, src_img_cfg[req->dst.format], PPP_ADDR_BG_CFG);
		mdp_writel(mdp, pack_pattern[req->dst.format],
			   PPP_ADDR_BG_PACK_PATTERN);
	}
	flush_imgs(req, regs, src_file, dst_file);
	mdp_writel(mdp, 0x1000, MDP_DISPLAY0_START);
	return 0;
}

int mdp_ppp_blit(const struct mdp_info *mdp, struct mdp_blit_req *req,
		 struct file *src_file, unsigned long src_start, unsigned long src_len,
		 struct file *dst_file, unsigned long dst_start, unsigned long dst_len)
{
	struct mdp_regs regs = {0};

	if (unlikely(req->src.format >= MDP_IMGTYPE_LIMIT ||
		     req->dst.format >= MDP_IMGTYPE_LIMIT)) {
		printk(KERN_ERR "mpd_ppp: img is of wrong format\n");
		return -EINVAL;
	}

	if (unlikely(req->src_rect.x > req->src.width ||
		     req->src_rect.y > req->src.height ||
		     req->dst_rect.x > req->dst.width ||
		     req->dst_rect.y > req->dst.height)) {
		printk(KERN_ERR "mpd_ppp: img rect is outside of img!\n");
		return -EINVAL;
	}

	/* set the src image configuration */
	regs.src_cfg = src_img_cfg[req->src.format];
	regs.src_cfg |= (req->src_rect.x & 0x1) ? PPP_SRC_BPP_ROI_ODD_X : 0;
	regs.src_cfg |= (req->src_rect.y & 0x1) ? PPP_SRC_BPP_ROI_ODD_Y : 0;
	regs.src_rect = (req->src_rect.h << 16) | req->src_rect.w;
	regs.src_pack = pack_pattern[req->src.format];

	/* set the dest image configuration */
	regs.dst_cfg = dst_img_cfg[req->dst.format] | PPP_DST_OUT_SEL_AXI;
	regs.dst_rect = (req->dst_rect.h << 16) | req->dst_rect.w;
	regs.dst_pack = pack_pattern[req->dst.format];

	/* set src, bpp, start pixel and ystride */
	regs.src_bpp = bytes_per_pixel[req->src.format];
	regs.src0 = src_start + req->src.offset;
	regs.src_ystride = req->src.width * regs.src_bpp;
	get_chroma_addr(&req->src, &req->src_rect, regs.src0, regs.src_bpp,
			regs.src_cfg, &regs.src1, &regs.src_ystride);
	regs.src0 += (req->src_rect.x + (req->src_rect.y * req->src.width)) *
		      regs.src_bpp;

	/* set dst, bpp, start pixel and ystride */
	regs.dst_bpp = bytes_per_pixel[req->dst.format];
	regs.dst0 = dst_start + req->dst.offset;
	regs.dst_ystride = req->dst.width * regs.dst_bpp;
	get_chroma_addr(&req->dst, &req->dst_rect, regs.dst0, regs.dst_bpp,
			regs.dst_cfg, &regs.dst1, &regs.dst_ystride);
	regs.dst0 += (req->dst_rect.x + (req->dst_rect.y * req->dst.width)) *
		      regs.dst_bpp;

	if (!valid_src_dst(src_start, src_len, dst_start, dst_len, req,
			   &regs)) {
		printk(KERN_ERR "mpd_ppp: final src or dst location is "
			"invalid, are you trying to make an image too large "
			"or to place it outside the screen?\n");
		return -EINVAL;
	}

	/* set up operation register */
	regs.op = 0;
	blit_rotate(req, &regs);
	blit_convert(req, &regs);
	if (req->flags & MDP_DITHER)
		regs.op |= PPP_OP_DITHER_EN;
	blit_blend(req, &regs);
	if (blit_scale(mdp, req, &regs)) {
		printk(KERN_ERR "mpd_ppp: error computing scale for img.\n");
		return -EINVAL;
	}
	blit_blur(mdp, req, &regs);
	regs.op |= dst_op_chroma[req->dst.format] |
		   src_op_chroma[req->src.format];

	/* if the image is YCRYCB, the x and w must be even */
	if (unlikely(req->src.format == MDP_YCRYCB_H2V1)) {
		req->src_rect.x = req->src_rect.x & (~0x1);
		req->src_rect.w = req->src_rect.w & (~0x1);
		req->dst_rect.x = req->dst_rect.x & (~0x1);
		req->dst_rect.w = req->dst_rect.w & (~0x1);
	}
	if (get_edge_cond(req, &regs))
		return -EINVAL;

	send_blit(mdp, req, &regs, src_file, dst_file);
	return 0;
}
