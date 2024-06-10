// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <linux/device.h>
#include <linux/slab.h>

#include "dcss-dev.h"

#define DCSS_DPR_SYSTEM_CTRL0			0x000
#define   RUN_EN				BIT(0)
#define   SOFT_RESET				BIT(1)
#define   REPEAT_EN				BIT(2)
#define   SHADOW_LOAD_EN			BIT(3)
#define   SW_SHADOW_LOAD_SEL			BIT(4)
#define   BCMD2AXI_MSTR_ID_CTRL			BIT(16)
#define DCSS_DPR_IRQ_MASK			0x020
#define DCSS_DPR_IRQ_MASK_STATUS		0x030
#define DCSS_DPR_IRQ_NONMASK_STATUS		0x040
#define   IRQ_DPR_CTRL_DONE			BIT(0)
#define   IRQ_DPR_RUN				BIT(1)
#define   IRQ_DPR_SHADOW_LOADED			BIT(2)
#define   IRQ_AXI_READ_ERR			BIT(3)
#define   DPR2RTR_YRGB_FIFO_OVFL		BIT(4)
#define   DPR2RTR_UV_FIFO_OVFL			BIT(5)
#define   DPR2RTR_FIFO_LD_BUF_RDY_YRGB_ERR	BIT(6)
#define   DPR2RTR_FIFO_LD_BUF_RDY_UV_ERR	BIT(7)
#define DCSS_DPR_MODE_CTRL0			0x050
#define   RTR_3BUF_EN				BIT(0)
#define   RTR_4LINE_BUF_EN			BIT(1)
#define   TILE_TYPE_POS				2
#define   TILE_TYPE_MASK			GENMASK(4, 2)
#define   YUV_EN				BIT(6)
#define   COMP_2PLANE_EN			BIT(7)
#define   PIX_SIZE_POS				8
#define   PIX_SIZE_MASK				GENMASK(9, 8)
#define   PIX_LUMA_UV_SWAP			BIT(10)
#define   PIX_UV_SWAP				BIT(11)
#define   B_COMP_SEL_POS			12
#define   B_COMP_SEL_MASK			GENMASK(13, 12)
#define   G_COMP_SEL_POS			14
#define   G_COMP_SEL_MASK			GENMASK(15, 14)
#define   R_COMP_SEL_POS			16
#define   R_COMP_SEL_MASK			GENMASK(17, 16)
#define   A_COMP_SEL_POS			18
#define   A_COMP_SEL_MASK			GENMASK(19, 18)
#define DCSS_DPR_FRAME_CTRL0			0x070
#define   HFLIP_EN				BIT(0)
#define   VFLIP_EN				BIT(1)
#define   ROT_ENC_POS				2
#define   ROT_ENC_MASK				GENMASK(3, 2)
#define   ROT_FLIP_ORDER_EN			BIT(4)
#define   PITCH_POS				16
#define   PITCH_MASK				GENMASK(31, 16)
#define DCSS_DPR_FRAME_1P_CTRL0			0x090
#define DCSS_DPR_FRAME_1P_PIX_X_CTRL		0x0A0
#define DCSS_DPR_FRAME_1P_PIX_Y_CTRL		0x0B0
#define DCSS_DPR_FRAME_1P_BASE_ADDR		0x0C0
#define DCSS_DPR_FRAME_2P_CTRL0			0x0E0
#define DCSS_DPR_FRAME_2P_PIX_X_CTRL		0x0F0
#define DCSS_DPR_FRAME_2P_PIX_Y_CTRL		0x100
#define DCSS_DPR_FRAME_2P_BASE_ADDR		0x110
#define DCSS_DPR_STATUS_CTRL0			0x130
#define   STATUS_MUX_SEL_MASK			GENMASK(2, 0)
#define   STATUS_SRC_SEL_POS			16
#define   STATUS_SRC_SEL_MASK			GENMASK(18, 16)
#define DCSS_DPR_STATUS_CTRL1			0x140
#define DCSS_DPR_RTRAM_CTRL0			0x200
#define   NUM_ROWS_ACTIVE			BIT(0)
#define   THRES_HIGH_POS			1
#define   THRES_HIGH_MASK			GENMASK(3, 1)
#define   THRES_LOW_POS				4
#define   THRES_LOW_MASK			GENMASK(6, 4)
#define   ABORT_SEL				BIT(7)

enum dcss_tile_type {
	TILE_LINEAR = 0,
	TILE_GPU_STANDARD,
	TILE_GPU_SUPER,
	TILE_VPU_YUV420,
	TILE_VPU_VP9,
};

enum dcss_pix_size {
	PIX_SIZE_8,
	PIX_SIZE_16,
	PIX_SIZE_32,
};

struct dcss_dpr_ch {
	struct dcss_dpr *dpr;
	void __iomem *base_reg;
	u32 base_ofs;

	struct drm_format_info format;
	enum dcss_pix_size pix_size;
	enum dcss_tile_type tile;
	bool rtram_4line_en;
	bool rtram_3buf_en;

	u32 frame_ctrl;
	u32 mode_ctrl;
	u32 sys_ctrl;
	u32 rtram_ctrl;

	bool sys_ctrl_chgd;

	int ch_num;
	int irq;
};

struct dcss_dpr {
	struct device *dev;
	struct dcss_ctxld *ctxld;
	u32  ctx_id;

	struct dcss_dpr_ch ch[3];
};

static void dcss_dpr_write(struct dcss_dpr_ch *ch, u32 val, u32 ofs)
{
	struct dcss_dpr *dpr = ch->dpr;

	dcss_ctxld_write(dpr->ctxld, dpr->ctx_id, val, ch->base_ofs + ofs);
}

static int dcss_dpr_ch_init_all(struct dcss_dpr *dpr, unsigned long dpr_base)
{
	struct dcss_dpr_ch *ch;
	int i;

	for (i = 0; i < 3; i++) {
		ch = &dpr->ch[i];

		ch->base_ofs = dpr_base + i * 0x1000;

		ch->base_reg = devm_ioremap(dpr->dev, ch->base_ofs, SZ_4K);
		if (!ch->base_reg) {
			dev_err(dpr->dev, "dpr: unable to remap ch %d base\n",
				i);
			return -ENOMEM;
		}

		ch->dpr = dpr;
		ch->ch_num = i;

		dcss_writel(0xff, ch->base_reg + DCSS_DPR_IRQ_MASK);
	}

	return 0;
}

int dcss_dpr_init(struct dcss_dev *dcss, unsigned long dpr_base)
{
	struct dcss_dpr *dpr;

	dpr = devm_kzalloc(dcss->dev, sizeof(*dpr), GFP_KERNEL);
	if (!dpr)
		return -ENOMEM;

	dcss->dpr = dpr;
	dpr->dev = dcss->dev;
	dpr->ctxld = dcss->ctxld;
	dpr->ctx_id = CTX_SB_HP;

	if (dcss_dpr_ch_init_all(dpr, dpr_base))
		return -ENOMEM;

	return 0;
}

void dcss_dpr_exit(struct dcss_dpr *dpr)
{
	int ch_no;

	/* stop DPR on all channels */
	for (ch_no = 0; ch_no < 3; ch_no++) {
		struct dcss_dpr_ch *ch = &dpr->ch[ch_no];

		dcss_writel(0, ch->base_reg + DCSS_DPR_SYSTEM_CTRL0);
	}
}

static u32 dcss_dpr_x_pix_wide_adjust(struct dcss_dpr_ch *ch, u32 pix_wide,
				      u32 pix_format)
{
	u8 pix_in_64byte_map[3][5] = {
		/* LIN, GPU_STD, GPU_SUP, VPU_YUV420, VPU_VP9 */
		{   64,       8,       8,          8,     16}, /* PIX_SIZE_8  */
		{   32,       8,       8,          8,      8}, /* PIX_SIZE_16 */
		{   16,       4,       4,          8,      8}, /* PIX_SIZE_32 */
	};
	u32 offset;
	u32 div_64byte_mod, pix_in_64byte;

	pix_in_64byte = pix_in_64byte_map[ch->pix_size][ch->tile];

	div_64byte_mod = pix_wide % pix_in_64byte;
	offset = (div_64byte_mod == 0) ? 0 : (pix_in_64byte - div_64byte_mod);

	return pix_wide + offset;
}

static u32 dcss_dpr_y_pix_high_adjust(struct dcss_dpr_ch *ch, u32 pix_high,
				      u32 pix_format)
{
	u8 num_rows_buf = ch->rtram_4line_en ? 4 : 8;
	u32 offset, pix_y_mod;

	pix_y_mod = pix_high % num_rows_buf;
	offset = pix_y_mod ? (num_rows_buf - pix_y_mod) : 0;

	return pix_high + offset;
}

void dcss_dpr_set_res(struct dcss_dpr *dpr, int ch_num, u32 xres, u32 yres)
{
	struct dcss_dpr_ch *ch = &dpr->ch[ch_num];
	u32 pix_format = ch->format.format;
	u32 gap = DCSS_DPR_FRAME_2P_BASE_ADDR - DCSS_DPR_FRAME_1P_BASE_ADDR;
	int plane, max_planes = 1;
	u32 pix_x_wide, pix_y_high;

	if (pix_format == DRM_FORMAT_NV12 ||
	    pix_format == DRM_FORMAT_NV21)
		max_planes = 2;

	for (plane = 0; plane < max_planes; plane++) {
		yres = plane == 1 ? yres >> 1 : yres;

		pix_x_wide = dcss_dpr_x_pix_wide_adjust(ch, xres, pix_format);
		pix_y_high = dcss_dpr_y_pix_high_adjust(ch, yres, pix_format);

		dcss_dpr_write(ch, pix_x_wide,
			       DCSS_DPR_FRAME_1P_PIX_X_CTRL + plane * gap);
		dcss_dpr_write(ch, pix_y_high,
			       DCSS_DPR_FRAME_1P_PIX_Y_CTRL + plane * gap);

		dcss_dpr_write(ch, 2, DCSS_DPR_FRAME_1P_CTRL0 + plane * gap);
	}
}

void dcss_dpr_addr_set(struct dcss_dpr *dpr, int ch_num, u32 luma_base_addr,
		       u32 chroma_base_addr, u16 pitch)
{
	struct dcss_dpr_ch *ch = &dpr->ch[ch_num];

	dcss_dpr_write(ch, luma_base_addr, DCSS_DPR_FRAME_1P_BASE_ADDR);

	dcss_dpr_write(ch, chroma_base_addr, DCSS_DPR_FRAME_2P_BASE_ADDR);

	ch->frame_ctrl &= ~PITCH_MASK;
	ch->frame_ctrl |= (((u32)pitch << PITCH_POS) & PITCH_MASK);
}

static void dcss_dpr_argb_comp_sel(struct dcss_dpr_ch *ch, int a_sel, int r_sel,
				   int g_sel, int b_sel)
{
	u32 sel;

	sel = ((a_sel << A_COMP_SEL_POS) & A_COMP_SEL_MASK) |
	      ((r_sel << R_COMP_SEL_POS) & R_COMP_SEL_MASK) |
	      ((g_sel << G_COMP_SEL_POS) & G_COMP_SEL_MASK) |
	      ((b_sel << B_COMP_SEL_POS) & B_COMP_SEL_MASK);

	ch->mode_ctrl &= ~(A_COMP_SEL_MASK | R_COMP_SEL_MASK |
			   G_COMP_SEL_MASK | B_COMP_SEL_MASK);
	ch->mode_ctrl |= sel;
}

static void dcss_dpr_pix_size_set(struct dcss_dpr_ch *ch,
				  const struct drm_format_info *format)
{
	u32 val;

	switch (format->format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		val = PIX_SIZE_8;
		break;

	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
		val = PIX_SIZE_16;
		break;

	default:
		val = PIX_SIZE_32;
		break;
	}

	ch->pix_size = val;

	ch->mode_ctrl &= ~PIX_SIZE_MASK;
	ch->mode_ctrl |= ((val << PIX_SIZE_POS) & PIX_SIZE_MASK);
}

static void dcss_dpr_uv_swap(struct dcss_dpr_ch *ch, bool swap)
{
	ch->mode_ctrl &= ~PIX_UV_SWAP;
	ch->mode_ctrl |= (swap ? PIX_UV_SWAP : 0);
}

static void dcss_dpr_y_uv_swap(struct dcss_dpr_ch *ch, bool swap)
{
	ch->mode_ctrl &= ~PIX_LUMA_UV_SWAP;
	ch->mode_ctrl |= (swap ? PIX_LUMA_UV_SWAP : 0);
}

static void dcss_dpr_2plane_en(struct dcss_dpr_ch *ch, bool en)
{
	ch->mode_ctrl &= ~COMP_2PLANE_EN;
	ch->mode_ctrl |= (en ? COMP_2PLANE_EN : 0);
}

static void dcss_dpr_yuv_en(struct dcss_dpr_ch *ch, bool en)
{
	ch->mode_ctrl &= ~YUV_EN;
	ch->mode_ctrl |= (en ? YUV_EN : 0);
}

void dcss_dpr_enable(struct dcss_dpr *dpr, int ch_num, bool en)
{
	struct dcss_dpr_ch *ch = &dpr->ch[ch_num];
	u32 sys_ctrl;

	sys_ctrl = (en ? REPEAT_EN | RUN_EN : 0);

	if (en) {
		dcss_dpr_write(ch, ch->mode_ctrl, DCSS_DPR_MODE_CTRL0);
		dcss_dpr_write(ch, ch->frame_ctrl, DCSS_DPR_FRAME_CTRL0);
		dcss_dpr_write(ch, ch->rtram_ctrl, DCSS_DPR_RTRAM_CTRL0);
	}

	if (ch->sys_ctrl != sys_ctrl)
		ch->sys_ctrl_chgd = true;

	ch->sys_ctrl = sys_ctrl;
}

struct rgb_comp_sel {
	u32 drm_format;
	int a_sel;
	int r_sel;
	int g_sel;
	int b_sel;
};

static struct rgb_comp_sel comp_sel_map[] = {
	{DRM_FORMAT_ARGB8888, 3, 2, 1, 0},
	{DRM_FORMAT_XRGB8888, 3, 2, 1, 0},
	{DRM_FORMAT_ABGR8888, 3, 0, 1, 2},
	{DRM_FORMAT_XBGR8888, 3, 0, 1, 2},
	{DRM_FORMAT_RGBA8888, 0, 3, 2, 1},
	{DRM_FORMAT_RGBX8888, 0, 3, 2, 1},
	{DRM_FORMAT_BGRA8888, 0, 1, 2, 3},
	{DRM_FORMAT_BGRX8888, 0, 1, 2, 3},
};

static int to_comp_sel(u32 pix_fmt, int *a_sel, int *r_sel, int *g_sel,
		       int *b_sel)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(comp_sel_map); i++) {
		if (comp_sel_map[i].drm_format == pix_fmt) {
			*a_sel = comp_sel_map[i].a_sel;
			*r_sel = comp_sel_map[i].r_sel;
			*g_sel = comp_sel_map[i].g_sel;
			*b_sel = comp_sel_map[i].b_sel;

			return 0;
		}
	}

	return -1;
}

static void dcss_dpr_rtram_set(struct dcss_dpr_ch *ch, u32 pix_format)
{
	u32 val, mask;

	switch (pix_format) {
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV12:
		ch->rtram_3buf_en = true;
		ch->rtram_4line_en = false;
		break;

	default:
		ch->rtram_3buf_en = true;
		ch->rtram_4line_en = true;
		break;
	}

	val = (ch->rtram_4line_en ? RTR_4LINE_BUF_EN : 0);
	val |= (ch->rtram_3buf_en ? RTR_3BUF_EN : 0);
	mask = RTR_4LINE_BUF_EN | RTR_3BUF_EN;

	ch->mode_ctrl &= ~mask;
	ch->mode_ctrl |= (val & mask);

	val = (ch->rtram_4line_en ? 0 : NUM_ROWS_ACTIVE);
	val |= (3 << THRES_LOW_POS) & THRES_LOW_MASK;
	val |= (4 << THRES_HIGH_POS) & THRES_HIGH_MASK;
	mask = THRES_LOW_MASK | THRES_HIGH_MASK | NUM_ROWS_ACTIVE;

	ch->rtram_ctrl &= ~mask;
	ch->rtram_ctrl |= (val & mask);
}

static void dcss_dpr_setup_components(struct dcss_dpr_ch *ch,
				      const struct drm_format_info *format)
{
	int a_sel, r_sel, g_sel, b_sel;
	bool uv_swap, y_uv_swap;

	switch (format->format) {
	case DRM_FORMAT_YVYU:
		uv_swap = true;
		y_uv_swap = true;
		break;

	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_NV21:
		uv_swap = true;
		y_uv_swap = false;
		break;

	case DRM_FORMAT_YUYV:
		uv_swap = false;
		y_uv_swap = true;
		break;

	default:
		uv_swap = false;
		y_uv_swap = false;
		break;
	}

	dcss_dpr_uv_swap(ch, uv_swap);

	dcss_dpr_y_uv_swap(ch, y_uv_swap);

	if (!format->is_yuv) {
		if (!to_comp_sel(format->format, &a_sel, &r_sel,
				 &g_sel, &b_sel)) {
			dcss_dpr_argb_comp_sel(ch, a_sel, r_sel, g_sel, b_sel);
		} else {
			dcss_dpr_argb_comp_sel(ch, 3, 2, 1, 0);
		}
	} else {
		dcss_dpr_argb_comp_sel(ch, 0, 0, 0, 0);
	}
}

static void dcss_dpr_tile_set(struct dcss_dpr_ch *ch, uint64_t modifier)
{
	switch (ch->ch_num) {
	case 0:
		switch (modifier) {
		case DRM_FORMAT_MOD_LINEAR:
			ch->tile = TILE_LINEAR;
			break;
		case DRM_FORMAT_MOD_VIVANTE_TILED:
			ch->tile = TILE_GPU_STANDARD;
			break;
		case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
			ch->tile = TILE_GPU_SUPER;
			break;
		default:
			WARN_ON(1);
			break;
		}
		break;
	case 1:
	case 2:
		ch->tile = TILE_LINEAR;
		break;
	default:
		WARN_ON(1);
		return;
	}

	ch->mode_ctrl &= ~TILE_TYPE_MASK;
	ch->mode_ctrl |= ((ch->tile << TILE_TYPE_POS) & TILE_TYPE_MASK);
}

void dcss_dpr_format_set(struct dcss_dpr *dpr, int ch_num,
			 const struct drm_format_info *format, u64 modifier)
{
	struct dcss_dpr_ch *ch = &dpr->ch[ch_num];

	ch->format = *format;

	dcss_dpr_yuv_en(ch, format->is_yuv);

	dcss_dpr_pix_size_set(ch, format);

	dcss_dpr_setup_components(ch, format);

	dcss_dpr_2plane_en(ch, format->num_planes == 2);

	dcss_dpr_rtram_set(ch, format->format);

	dcss_dpr_tile_set(ch, modifier);
}

/* This function will be called from interrupt context. */
void dcss_dpr_write_sysctrl(struct dcss_dpr *dpr)
{
	int chnum;

	dcss_ctxld_assert_locked(dpr->ctxld);

	for (chnum = 0; chnum < 3; chnum++) {
		struct dcss_dpr_ch *ch = &dpr->ch[chnum];

		if (ch->sys_ctrl_chgd) {
			dcss_ctxld_write_irqsafe(dpr->ctxld, dpr->ctx_id,
						 ch->sys_ctrl,
						 ch->base_ofs +
						 DCSS_DPR_SYSTEM_CTRL0);
			ch->sys_ctrl_chgd = false;
		}
	}
}

void dcss_dpr_set_rotation(struct dcss_dpr *dpr, int ch_num, u32 rotation)
{
	struct dcss_dpr_ch *ch = &dpr->ch[ch_num];

	ch->frame_ctrl &= ~(HFLIP_EN | VFLIP_EN | ROT_ENC_MASK);

	ch->frame_ctrl |= rotation & DRM_MODE_REFLECT_X ? HFLIP_EN : 0;
	ch->frame_ctrl |= rotation & DRM_MODE_REFLECT_Y ? VFLIP_EN : 0;

	if (rotation & DRM_MODE_ROTATE_90)
		ch->frame_ctrl |= 1 << ROT_ENC_POS;
	else if (rotation & DRM_MODE_ROTATE_180)
		ch->frame_ctrl |= 2 << ROT_ENC_POS;
	else if (rotation & DRM_MODE_ROTATE_270)
		ch->frame_ctrl |= 3 << ROT_ENC_POS;
}
