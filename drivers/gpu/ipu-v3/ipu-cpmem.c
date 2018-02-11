/*
 * Copyright (C) 2012 Mentor Graphics Inc.
 * Copyright 2005-2012 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/types.h>
#include <linux/bitrev.h>
#include <linux/io.h>
#include <drm/drm_fourcc.h>
#include "ipu-prv.h"

struct ipu_cpmem_word {
	u32 data[5];
	u32 res[3];
};

struct ipu_ch_param {
	struct ipu_cpmem_word word[2];
};

struct ipu_cpmem {
	struct ipu_ch_param __iomem *base;
	u32 module;
	spinlock_t lock;
	int use_count;
	struct ipu_soc *ipu;
};

#define IPU_CPMEM_WORD(word, ofs, size) ((((word) * 160 + (ofs)) << 8) | (size))

#define IPU_FIELD_UBO		IPU_CPMEM_WORD(0, 46, 22)
#define IPU_FIELD_VBO		IPU_CPMEM_WORD(0, 68, 22)
#define IPU_FIELD_IOX		IPU_CPMEM_WORD(0, 90, 4)
#define IPU_FIELD_RDRW		IPU_CPMEM_WORD(0, 94, 1)
#define IPU_FIELD_SO		IPU_CPMEM_WORD(0, 113, 1)
#define IPU_FIELD_SLY		IPU_CPMEM_WORD(1, 102, 14)
#define IPU_FIELD_SLUV		IPU_CPMEM_WORD(1, 128, 14)

#define IPU_FIELD_XV		IPU_CPMEM_WORD(0, 0, 10)
#define IPU_FIELD_YV		IPU_CPMEM_WORD(0, 10, 9)
#define IPU_FIELD_XB		IPU_CPMEM_WORD(0, 19, 13)
#define IPU_FIELD_YB		IPU_CPMEM_WORD(0, 32, 12)
#define IPU_FIELD_NSB_B		IPU_CPMEM_WORD(0, 44, 1)
#define IPU_FIELD_CF		IPU_CPMEM_WORD(0, 45, 1)
#define IPU_FIELD_SX		IPU_CPMEM_WORD(0, 46, 12)
#define IPU_FIELD_SY		IPU_CPMEM_WORD(0, 58, 11)
#define IPU_FIELD_NS		IPU_CPMEM_WORD(0, 69, 10)
#define IPU_FIELD_SDX		IPU_CPMEM_WORD(0, 79, 7)
#define IPU_FIELD_SM		IPU_CPMEM_WORD(0, 86, 10)
#define IPU_FIELD_SCC		IPU_CPMEM_WORD(0, 96, 1)
#define IPU_FIELD_SCE		IPU_CPMEM_WORD(0, 97, 1)
#define IPU_FIELD_SDY		IPU_CPMEM_WORD(0, 98, 7)
#define IPU_FIELD_SDRX		IPU_CPMEM_WORD(0, 105, 1)
#define IPU_FIELD_SDRY		IPU_CPMEM_WORD(0, 106, 1)
#define IPU_FIELD_BPP		IPU_CPMEM_WORD(0, 107, 3)
#define IPU_FIELD_DEC_SEL	IPU_CPMEM_WORD(0, 110, 2)
#define IPU_FIELD_DIM		IPU_CPMEM_WORD(0, 112, 1)
#define IPU_FIELD_BNDM		IPU_CPMEM_WORD(0, 114, 3)
#define IPU_FIELD_BM		IPU_CPMEM_WORD(0, 117, 2)
#define IPU_FIELD_ROT		IPU_CPMEM_WORD(0, 119, 1)
#define IPU_FIELD_ROT_HF_VF	IPU_CPMEM_WORD(0, 119, 3)
#define IPU_FIELD_HF		IPU_CPMEM_WORD(0, 120, 1)
#define IPU_FIELD_VF		IPU_CPMEM_WORD(0, 121, 1)
#define IPU_FIELD_THE		IPU_CPMEM_WORD(0, 122, 1)
#define IPU_FIELD_CAP		IPU_CPMEM_WORD(0, 123, 1)
#define IPU_FIELD_CAE		IPU_CPMEM_WORD(0, 124, 1)
#define IPU_FIELD_FW		IPU_CPMEM_WORD(0, 125, 13)
#define IPU_FIELD_FH		IPU_CPMEM_WORD(0, 138, 12)
#define IPU_FIELD_EBA0		IPU_CPMEM_WORD(1, 0, 29)
#define IPU_FIELD_EBA1		IPU_CPMEM_WORD(1, 29, 29)
#define IPU_FIELD_ILO		IPU_CPMEM_WORD(1, 58, 20)
#define IPU_FIELD_NPB		IPU_CPMEM_WORD(1, 78, 7)
#define IPU_FIELD_PFS		IPU_CPMEM_WORD(1, 85, 4)
#define IPU_FIELD_ALU		IPU_CPMEM_WORD(1, 89, 1)
#define IPU_FIELD_ALBM		IPU_CPMEM_WORD(1, 90, 3)
#define IPU_FIELD_ID		IPU_CPMEM_WORD(1, 93, 2)
#define IPU_FIELD_TH		IPU_CPMEM_WORD(1, 95, 7)
#define IPU_FIELD_SL		IPU_CPMEM_WORD(1, 102, 14)
#define IPU_FIELD_WID0		IPU_CPMEM_WORD(1, 116, 3)
#define IPU_FIELD_WID1		IPU_CPMEM_WORD(1, 119, 3)
#define IPU_FIELD_WID2		IPU_CPMEM_WORD(1, 122, 3)
#define IPU_FIELD_WID3		IPU_CPMEM_WORD(1, 125, 3)
#define IPU_FIELD_OFS0		IPU_CPMEM_WORD(1, 128, 5)
#define IPU_FIELD_OFS1		IPU_CPMEM_WORD(1, 133, 5)
#define IPU_FIELD_OFS2		IPU_CPMEM_WORD(1, 138, 5)
#define IPU_FIELD_OFS3		IPU_CPMEM_WORD(1, 143, 5)
#define IPU_FIELD_SXYS		IPU_CPMEM_WORD(1, 148, 1)
#define IPU_FIELD_CRE		IPU_CPMEM_WORD(1, 149, 1)
#define IPU_FIELD_DEC_SEL2	IPU_CPMEM_WORD(1, 150, 1)

static inline struct ipu_ch_param __iomem *
ipu_get_cpmem(struct ipuv3_channel *ch)
{
	struct ipu_cpmem *cpmem = ch->ipu->cpmem_priv;

	return cpmem->base + ch->num;
}

static void ipu_ch_param_write_field(struct ipuv3_channel *ch, u32 wbs, u32 v)
{
	struct ipu_ch_param __iomem *base = ipu_get_cpmem(ch);
	u32 bit = (wbs >> 8) % 160;
	u32 size = wbs & 0xff;
	u32 word = (wbs >> 8) / 160;
	u32 i = bit / 32;
	u32 ofs = bit % 32;
	u32 mask = (1 << size) - 1;
	u32 val;

	pr_debug("%s %d %d %d\n", __func__, word, bit , size);

	val = readl(&base->word[word].data[i]);
	val &= ~(mask << ofs);
	val |= v << ofs;
	writel(val, &base->word[word].data[i]);

	if ((bit + size - 1) / 32 > i) {
		val = readl(&base->word[word].data[i + 1]);
		val &= ~(mask >> (ofs ? (32 - ofs) : 0));
		val |= v >> (ofs ? (32 - ofs) : 0);
		writel(val, &base->word[word].data[i + 1]);
	}
}

static u32 ipu_ch_param_read_field(struct ipuv3_channel *ch, u32 wbs)
{
	struct ipu_ch_param __iomem *base = ipu_get_cpmem(ch);
	u32 bit = (wbs >> 8) % 160;
	u32 size = wbs & 0xff;
	u32 word = (wbs >> 8) / 160;
	u32 i = bit / 32;
	u32 ofs = bit % 32;
	u32 mask = (1 << size) - 1;
	u32 val = 0;

	pr_debug("%s %d %d %d\n", __func__, word, bit , size);

	val = (readl(&base->word[word].data[i]) >> ofs) & mask;

	if ((bit + size - 1) / 32 > i) {
		u32 tmp;

		tmp = readl(&base->word[word].data[i + 1]);
		tmp &= mask >> (ofs ? (32 - ofs) : 0);
		val |= tmp << (ofs ? (32 - ofs) : 0);
	}

	return val;
}

/*
 * The V4L2 spec defines packed RGB formats in memory byte order, which from
 * point of view of the IPU corresponds to little-endian words with the first
 * component in the least significant bits.
 * The DRM pixel formats and IPU internal representation are ordered the other
 * way around, with the first named component ordered at the most significant
 * bits. Further, V4L2 formats are not well defined:
 *     https://linuxtv.org/downloads/v4l-dvb-apis/packed-rgb.html
 * We choose the interpretation which matches GStreamer behavior.
 */
static int v4l2_pix_fmt_to_drm_fourcc(u32 pixelformat)
{
	switch (pixelformat) {
	case V4L2_PIX_FMT_RGB565:
		/*
		 * Here we choose the 'corrected' interpretation of RGBP, a
		 * little-endian 16-bit word with the red component at the most
		 * significant bits:
		 * g[2:0]b[4:0] r[4:0]g[5:3] <=> [16:0] R:G:B
		 */
		return DRM_FORMAT_RGB565;
	case V4L2_PIX_FMT_BGR24:
		/* B G R <=> [24:0] R:G:B */
		return DRM_FORMAT_RGB888;
	case V4L2_PIX_FMT_RGB24:
		/* R G B <=> [24:0] B:G:R */
		return DRM_FORMAT_BGR888;
	case V4L2_PIX_FMT_BGR32:
		/* B G R A <=> [32:0] A:B:G:R */
		return DRM_FORMAT_XRGB8888;
	case V4L2_PIX_FMT_RGB32:
		/* R G B A <=> [32:0] A:B:G:R */
		return DRM_FORMAT_XBGR8888;
	case V4L2_PIX_FMT_UYVY:
		return DRM_FORMAT_UYVY;
	case V4L2_PIX_FMT_YUYV:
		return DRM_FORMAT_YUYV;
	case V4L2_PIX_FMT_YUV420:
		return DRM_FORMAT_YUV420;
	case V4L2_PIX_FMT_YUV422P:
		return DRM_FORMAT_YUV422;
	case V4L2_PIX_FMT_YVU420:
		return DRM_FORMAT_YVU420;
	case V4L2_PIX_FMT_NV12:
		return DRM_FORMAT_NV12;
	case V4L2_PIX_FMT_NV16:
		return DRM_FORMAT_NV16;
	}

	return -EINVAL;
}

void ipu_cpmem_zero(struct ipuv3_channel *ch)
{
	struct ipu_ch_param __iomem *p = ipu_get_cpmem(ch);
	void __iomem *base = p;
	int i;

	for (i = 0; i < sizeof(*p) / sizeof(u32); i++)
		writel(0, base + i * sizeof(u32));
}
EXPORT_SYMBOL_GPL(ipu_cpmem_zero);

void ipu_cpmem_set_resolution(struct ipuv3_channel *ch, int xres, int yres)
{
	ipu_ch_param_write_field(ch, IPU_FIELD_FW, xres - 1);
	ipu_ch_param_write_field(ch, IPU_FIELD_FH, yres - 1);
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_resolution);

void ipu_cpmem_skip_odd_chroma_rows(struct ipuv3_channel *ch)
{
	ipu_ch_param_write_field(ch, IPU_FIELD_RDRW, 1);
}
EXPORT_SYMBOL_GPL(ipu_cpmem_skip_odd_chroma_rows);

void ipu_cpmem_set_stride(struct ipuv3_channel *ch, int stride)
{
	ipu_ch_param_write_field(ch, IPU_FIELD_SLY, stride - 1);
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_stride);

void ipu_cpmem_set_high_priority(struct ipuv3_channel *ch)
{
	struct ipu_soc *ipu = ch->ipu;
	u32 val;

	if (ipu->ipu_type == IPUV3EX)
		ipu_ch_param_write_field(ch, IPU_FIELD_ID, 1);

	val = ipu_idmac_read(ipu, IDMAC_CHA_PRI(ch->num));
	val |= 1 << (ch->num % 32);
	ipu_idmac_write(ipu, val, IDMAC_CHA_PRI(ch->num));
};
EXPORT_SYMBOL_GPL(ipu_cpmem_set_high_priority);

void ipu_cpmem_set_buffer(struct ipuv3_channel *ch, int bufnum, dma_addr_t buf)
{
	if (bufnum)
		ipu_ch_param_write_field(ch, IPU_FIELD_EBA1, buf >> 3);
	else
		ipu_ch_param_write_field(ch, IPU_FIELD_EBA0, buf >> 3);
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_buffer);

void ipu_cpmem_set_uv_offset(struct ipuv3_channel *ch, u32 u_off, u32 v_off)
{
	ipu_ch_param_write_field(ch, IPU_FIELD_UBO, u_off / 8);
	ipu_ch_param_write_field(ch, IPU_FIELD_VBO, v_off / 8);
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_uv_offset);

void ipu_cpmem_interlaced_scan(struct ipuv3_channel *ch, int stride)
{
	ipu_ch_param_write_field(ch, IPU_FIELD_SO, 1);
	ipu_ch_param_write_field(ch, IPU_FIELD_ILO, stride / 8);
	ipu_ch_param_write_field(ch, IPU_FIELD_SLY, (stride * 2) - 1);
};
EXPORT_SYMBOL_GPL(ipu_cpmem_interlaced_scan);

void ipu_cpmem_set_axi_id(struct ipuv3_channel *ch, u32 id)
{
	id &= 0x3;
	ipu_ch_param_write_field(ch, IPU_FIELD_ID, id);
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_axi_id);

int ipu_cpmem_get_burstsize(struct ipuv3_channel *ch)
{
	return ipu_ch_param_read_field(ch, IPU_FIELD_NPB) + 1;
}
EXPORT_SYMBOL_GPL(ipu_cpmem_get_burstsize);

void ipu_cpmem_set_burstsize(struct ipuv3_channel *ch, int burstsize)
{
	ipu_ch_param_write_field(ch, IPU_FIELD_NPB, burstsize - 1);
};
EXPORT_SYMBOL_GPL(ipu_cpmem_set_burstsize);

void ipu_cpmem_set_block_mode(struct ipuv3_channel *ch)
{
	ipu_ch_param_write_field(ch, IPU_FIELD_BM, 1);
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_block_mode);

void ipu_cpmem_set_rotation(struct ipuv3_channel *ch,
			    enum ipu_rotate_mode rot)
{
	u32 temp_rot = bitrev8(rot) >> 5;

	ipu_ch_param_write_field(ch, IPU_FIELD_ROT_HF_VF, temp_rot);
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_rotation);

int ipu_cpmem_set_format_rgb(struct ipuv3_channel *ch,
			     const struct ipu_rgb *rgb)
{
	int bpp = 0, npb = 0, ro, go, bo, to;

	ro = rgb->bits_per_pixel - rgb->red.length - rgb->red.offset;
	go = rgb->bits_per_pixel - rgb->green.length - rgb->green.offset;
	bo = rgb->bits_per_pixel - rgb->blue.length - rgb->blue.offset;
	to = rgb->bits_per_pixel - rgb->transp.length - rgb->transp.offset;

	ipu_ch_param_write_field(ch, IPU_FIELD_WID0, rgb->red.length - 1);
	ipu_ch_param_write_field(ch, IPU_FIELD_OFS0, ro);
	ipu_ch_param_write_field(ch, IPU_FIELD_WID1, rgb->green.length - 1);
	ipu_ch_param_write_field(ch, IPU_FIELD_OFS1, go);
	ipu_ch_param_write_field(ch, IPU_FIELD_WID2, rgb->blue.length - 1);
	ipu_ch_param_write_field(ch, IPU_FIELD_OFS2, bo);

	if (rgb->transp.length) {
		ipu_ch_param_write_field(ch, IPU_FIELD_WID3,
				rgb->transp.length - 1);
		ipu_ch_param_write_field(ch, IPU_FIELD_OFS3, to);
	} else {
		ipu_ch_param_write_field(ch, IPU_FIELD_WID3, 7);
		ipu_ch_param_write_field(ch, IPU_FIELD_OFS3,
				rgb->bits_per_pixel);
	}

	switch (rgb->bits_per_pixel) {
	case 32:
		bpp = 0;
		npb = 15;
		break;
	case 24:
		bpp = 1;
		npb = 19;
		break;
	case 16:
		bpp = 3;
		npb = 31;
		break;
	case 8:
		bpp = 5;
		npb = 63;
		break;
	default:
		return -EINVAL;
	}
	ipu_ch_param_write_field(ch, IPU_FIELD_BPP, bpp);
	ipu_ch_param_write_field(ch, IPU_FIELD_NPB, npb);
	ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 7); /* rgb mode */

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_format_rgb);

int ipu_cpmem_set_format_passthrough(struct ipuv3_channel *ch, int width)
{
	int bpp = 0, npb = 0;

	switch (width) {
	case 32:
		bpp = 0;
		npb = 15;
		break;
	case 24:
		bpp = 1;
		npb = 19;
		break;
	case 16:
		bpp = 3;
		npb = 31;
		break;
	case 8:
		bpp = 5;
		npb = 63;
		break;
	default:
		return -EINVAL;
	}

	ipu_ch_param_write_field(ch, IPU_FIELD_BPP, bpp);
	ipu_ch_param_write_field(ch, IPU_FIELD_NPB, npb);
	ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 6); /* raw mode */

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_format_passthrough);

void ipu_cpmem_set_yuv_interleaved(struct ipuv3_channel *ch, u32 pixel_format)
{
	switch (pixel_format) {
	case V4L2_PIX_FMT_UYVY:
		ipu_ch_param_write_field(ch, IPU_FIELD_BPP, 3); /* bits/pixel */
		ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 0xA);/* pix fmt */
		ipu_ch_param_write_field(ch, IPU_FIELD_NPB, 31);/* burst size */
		break;
	case V4L2_PIX_FMT_YUYV:
		ipu_ch_param_write_field(ch, IPU_FIELD_BPP, 3); /* bits/pixel */
		ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 0x8);/* pix fmt */
		ipu_ch_param_write_field(ch, IPU_FIELD_NPB, 31);/* burst size */
		break;
	}
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_yuv_interleaved);

void ipu_cpmem_set_yuv_planar_full(struct ipuv3_channel *ch,
				   unsigned int uv_stride,
				   unsigned int u_offset, unsigned int v_offset)
{
	ipu_ch_param_write_field(ch, IPU_FIELD_SLUV, uv_stride - 1);
	ipu_ch_param_write_field(ch, IPU_FIELD_UBO, u_offset / 8);
	ipu_ch_param_write_field(ch, IPU_FIELD_VBO, v_offset / 8);
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_yuv_planar_full);

static const struct ipu_rgb def_xrgb_32 = {
	.red	= { .offset = 16, .length = 8, },
	.green	= { .offset =  8, .length = 8, },
	.blue	= { .offset =  0, .length = 8, },
	.transp = { .offset = 24, .length = 8, },
	.bits_per_pixel = 32,
};

static const struct ipu_rgb def_xbgr_32 = {
	.red	= { .offset =  0, .length = 8, },
	.green	= { .offset =  8, .length = 8, },
	.blue	= { .offset = 16, .length = 8, },
	.transp = { .offset = 24, .length = 8, },
	.bits_per_pixel = 32,
};

static const struct ipu_rgb def_rgbx_32 = {
	.red	= { .offset = 24, .length = 8, },
	.green	= { .offset = 16, .length = 8, },
	.blue	= { .offset =  8, .length = 8, },
	.transp = { .offset =  0, .length = 8, },
	.bits_per_pixel = 32,
};

static const struct ipu_rgb def_bgrx_32 = {
	.red	= { .offset =  8, .length = 8, },
	.green	= { .offset = 16, .length = 8, },
	.blue	= { .offset = 24, .length = 8, },
	.transp = { .offset =  0, .length = 8, },
	.bits_per_pixel = 32,
};

static const struct ipu_rgb def_rgb_24 = {
	.red	= { .offset = 16, .length = 8, },
	.green	= { .offset =  8, .length = 8, },
	.blue	= { .offset =  0, .length = 8, },
	.transp = { .offset =  0, .length = 0, },
	.bits_per_pixel = 24,
};

static const struct ipu_rgb def_bgr_24 = {
	.red	= { .offset =  0, .length = 8, },
	.green	= { .offset =  8, .length = 8, },
	.blue	= { .offset = 16, .length = 8, },
	.transp = { .offset =  0, .length = 0, },
	.bits_per_pixel = 24,
};

static const struct ipu_rgb def_rgb_16 = {
	.red	= { .offset = 11, .length = 5, },
	.green	= { .offset =  5, .length = 6, },
	.blue	= { .offset =  0, .length = 5, },
	.transp = { .offset =  0, .length = 0, },
	.bits_per_pixel = 16,
};

static const struct ipu_rgb def_bgr_16 = {
	.red	= { .offset =  0, .length = 5, },
	.green	= { .offset =  5, .length = 6, },
	.blue	= { .offset = 11, .length = 5, },
	.transp = { .offset =  0, .length = 0, },
	.bits_per_pixel = 16,
};

static const struct ipu_rgb def_argb_16 = {
	.red	= { .offset = 10, .length = 5, },
	.green	= { .offset =  5, .length = 5, },
	.blue	= { .offset =  0, .length = 5, },
	.transp = { .offset = 15, .length = 1, },
	.bits_per_pixel = 16,
};

static const struct ipu_rgb def_argb_16_4444 = {
	.red	= { .offset =  8, .length = 4, },
	.green	= { .offset =  4, .length = 4, },
	.blue	= { .offset =  0, .length = 4, },
	.transp = { .offset = 12, .length = 4, },
	.bits_per_pixel = 16,
};

static const struct ipu_rgb def_abgr_16 = {
	.red	= { .offset =  0, .length = 5, },
	.green	= { .offset =  5, .length = 5, },
	.blue	= { .offset = 10, .length = 5, },
	.transp = { .offset = 15, .length = 1, },
	.bits_per_pixel = 16,
};

static const struct ipu_rgb def_rgba_16 = {
	.red	= { .offset = 11, .length = 5, },
	.green	= { .offset =  6, .length = 5, },
	.blue	= { .offset =  1, .length = 5, },
	.transp = { .offset =  0, .length = 1, },
	.bits_per_pixel = 16,
};

static const struct ipu_rgb def_bgra_16 = {
	.red	= { .offset =  1, .length = 5, },
	.green	= { .offset =  6, .length = 5, },
	.blue	= { .offset = 11, .length = 5, },
	.transp = { .offset =  0, .length = 1, },
	.bits_per_pixel = 16,
};

#define Y_OFFSET(pix, x, y)	((x) + pix->width * (y))
#define U_OFFSET(pix, x, y)	((pix->width * pix->height) +		\
				 (pix->width * (y) / 4) + (x) / 2)
#define V_OFFSET(pix, x, y)	((pix->width * pix->height) +		\
				 (pix->width * pix->height / 4) +	\
				 (pix->width * (y) / 4) + (x) / 2)
#define U2_OFFSET(pix, x, y)	((pix->width * pix->height) +		\
				 (pix->width * (y) / 2) + (x) / 2)
#define V2_OFFSET(pix, x, y)	((pix->width * pix->height) +		\
				 (pix->width * pix->height / 2) +	\
				 (pix->width * (y) / 2) + (x) / 2)
#define UV_OFFSET(pix, x, y)	((pix->width * pix->height) +	\
				 (pix->width * (y) / 2) + (x))
#define UV2_OFFSET(pix, x, y)	((pix->width * pix->height) +	\
				 (pix->width * y) + (x))

#define NUM_ALPHA_CHANNELS	7

/* See Table 37-12. Alpha channels mapping. */
static int ipu_channel_albm(int ch_num)
{
	switch (ch_num) {
	case IPUV3_CHANNEL_G_MEM_IC_PRP_VF:	return 0;
	case IPUV3_CHANNEL_G_MEM_IC_PP:		return 1;
	case IPUV3_CHANNEL_MEM_FG_SYNC:		return 2;
	case IPUV3_CHANNEL_MEM_FG_ASYNC:	return 3;
	case IPUV3_CHANNEL_MEM_BG_SYNC:		return 4;
	case IPUV3_CHANNEL_MEM_BG_ASYNC:	return 5;
	case IPUV3_CHANNEL_MEM_VDI_PLANE1_COMB: return 6;
	default:
		return -EINVAL;
	}
}

static void ipu_cpmem_set_separate_alpha(struct ipuv3_channel *ch)
{
	struct ipu_soc *ipu = ch->ipu;
	int albm;
	u32 val;

	albm = ipu_channel_albm(ch->num);
	if (albm < 0)
		return;

	ipu_ch_param_write_field(ch, IPU_FIELD_ALU, 1);
	ipu_ch_param_write_field(ch, IPU_FIELD_ALBM, albm);
	ipu_ch_param_write_field(ch, IPU_FIELD_CRE, 1);

	val = ipu_idmac_read(ipu, IDMAC_SEP_ALPHA);
	val |= BIT(ch->num);
	ipu_idmac_write(ipu, val, IDMAC_SEP_ALPHA);
}

int ipu_cpmem_set_fmt(struct ipuv3_channel *ch, u32 drm_fourcc)
{
	switch (drm_fourcc) {
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		/* pix format */
		ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 2);
		/* burst size */
		ipu_ch_param_write_field(ch, IPU_FIELD_NPB, 31);
		break;
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
		/* pix format */
		ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 1);
		/* burst size */
		ipu_ch_param_write_field(ch, IPU_FIELD_NPB, 31);
		break;
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU444:
		/* pix format */
		ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 0);
		/* burst size */
		ipu_ch_param_write_field(ch, IPU_FIELD_NPB, 31);
		break;
	case DRM_FORMAT_NV12:
		/* pix format */
		ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 4);
		/* burst size */
		ipu_ch_param_write_field(ch, IPU_FIELD_NPB, 31);
		break;
	case DRM_FORMAT_NV16:
		/* pix format */
		ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 3);
		/* burst size */
		ipu_ch_param_write_field(ch, IPU_FIELD_NPB, 31);
		break;
	case DRM_FORMAT_UYVY:
		/* bits/pixel */
		ipu_ch_param_write_field(ch, IPU_FIELD_BPP, 3);
		/* pix format */
		ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 0xA);
		/* burst size */
		ipu_ch_param_write_field(ch, IPU_FIELD_NPB, 31);
		break;
	case DRM_FORMAT_YUYV:
		/* bits/pixel */
		ipu_ch_param_write_field(ch, IPU_FIELD_BPP, 3);
		/* pix format */
		ipu_ch_param_write_field(ch, IPU_FIELD_PFS, 0x8);
		/* burst size */
		ipu_ch_param_write_field(ch, IPU_FIELD_NPB, 31);
		break;
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
		ipu_cpmem_set_format_rgb(ch, &def_xbgr_32);
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		ipu_cpmem_set_format_rgb(ch, &def_xrgb_32);
		break;
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBX8888_A8:
		ipu_cpmem_set_format_rgb(ch, &def_rgbx_32);
		break;
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRX8888_A8:
		ipu_cpmem_set_format_rgb(ch, &def_bgrx_32);
		break;
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_BGR888_A8:
		ipu_cpmem_set_format_rgb(ch, &def_bgr_24);
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_RGB888_A8:
		ipu_cpmem_set_format_rgb(ch, &def_rgb_24);
		break;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_RGB565_A8:
		ipu_cpmem_set_format_rgb(ch, &def_rgb_16);
		break;
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_BGR565_A8:
		ipu_cpmem_set_format_rgb(ch, &def_bgr_16);
		break;
	case DRM_FORMAT_ARGB1555:
		ipu_cpmem_set_format_rgb(ch, &def_argb_16);
		break;
	case DRM_FORMAT_ABGR1555:
		ipu_cpmem_set_format_rgb(ch, &def_abgr_16);
		break;
	case DRM_FORMAT_RGBA5551:
		ipu_cpmem_set_format_rgb(ch, &def_rgba_16);
		break;
	case DRM_FORMAT_BGRA5551:
		ipu_cpmem_set_format_rgb(ch, &def_bgra_16);
		break;
	case DRM_FORMAT_ARGB4444:
		ipu_cpmem_set_format_rgb(ch, &def_argb_16_4444);
		break;
	default:
		return -EINVAL;
	}

	switch (drm_fourcc) {
	case DRM_FORMAT_RGB565_A8:
	case DRM_FORMAT_BGR565_A8:
	case DRM_FORMAT_RGB888_A8:
	case DRM_FORMAT_BGR888_A8:
	case DRM_FORMAT_RGBX8888_A8:
	case DRM_FORMAT_BGRX8888_A8:
		ipu_ch_param_write_field(ch, IPU_FIELD_WID3, 7);
		ipu_cpmem_set_separate_alpha(ch);
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_fmt);

int ipu_cpmem_set_image(struct ipuv3_channel *ch, struct ipu_image *image)
{
	struct v4l2_pix_format *pix = &image->pix;
	int offset, u_offset, v_offset;
	int ret = 0;

	pr_debug("%s: resolution: %dx%d stride: %d\n",
		 __func__, pix->width, pix->height,
		 pix->bytesperline);

	ipu_cpmem_set_resolution(ch, image->rect.width, image->rect.height);
	ipu_cpmem_set_stride(ch, pix->bytesperline);

	ipu_cpmem_set_fmt(ch, v4l2_pix_fmt_to_drm_fourcc(pix->pixelformat));

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUV420:
		offset = Y_OFFSET(pix, image->rect.left, image->rect.top);
		u_offset = U_OFFSET(pix, image->rect.left,
				    image->rect.top) - offset;
		v_offset = V_OFFSET(pix, image->rect.left,
				    image->rect.top) - offset;

		ipu_cpmem_set_yuv_planar_full(ch, pix->bytesperline / 2,
					      u_offset, v_offset);
		break;
	case V4L2_PIX_FMT_YVU420:
		offset = Y_OFFSET(pix, image->rect.left, image->rect.top);
		u_offset = U_OFFSET(pix, image->rect.left,
				    image->rect.top) - offset;
		v_offset = V_OFFSET(pix, image->rect.left,
				    image->rect.top) - offset;

		ipu_cpmem_set_yuv_planar_full(ch, pix->bytesperline / 2,
					      v_offset, u_offset);
		break;
	case V4L2_PIX_FMT_YUV422P:
		offset = Y_OFFSET(pix, image->rect.left, image->rect.top);
		u_offset = U2_OFFSET(pix, image->rect.left,
				     image->rect.top) - offset;
		v_offset = V2_OFFSET(pix, image->rect.left,
				     image->rect.top) - offset;

		ipu_cpmem_set_yuv_planar_full(ch, pix->bytesperline / 2,
					      u_offset, v_offset);
		break;
	case V4L2_PIX_FMT_NV12:
		offset = Y_OFFSET(pix, image->rect.left, image->rect.top);
		u_offset = UV_OFFSET(pix, image->rect.left,
				     image->rect.top) - offset;
		v_offset = 0;

		ipu_cpmem_set_yuv_planar_full(ch, pix->bytesperline,
					      u_offset, v_offset);
		break;
	case V4L2_PIX_FMT_NV16:
		offset = Y_OFFSET(pix, image->rect.left, image->rect.top);
		u_offset = UV2_OFFSET(pix, image->rect.left,
				      image->rect.top) - offset;
		v_offset = 0;

		ipu_cpmem_set_yuv_planar_full(ch, pix->bytesperline,
					      u_offset, v_offset);
		break;
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_RGB565:
		offset = image->rect.left * 2 +
			image->rect.top * pix->bytesperline;
		break;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR32:
		offset = image->rect.left * 4 +
			image->rect.top * pix->bytesperline;
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
		offset = image->rect.left * 3 +
			image->rect.top * pix->bytesperline;
		break;
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
		offset = image->rect.left + image->rect.top * pix->bytesperline;
		break;
	case V4L2_PIX_FMT_SBGGR16:
	case V4L2_PIX_FMT_SGBRG16:
	case V4L2_PIX_FMT_SGRBG16:
	case V4L2_PIX_FMT_SRGGB16:
		offset = image->rect.left * 2 +
			 image->rect.top * pix->bytesperline;
		break;
	default:
		/* This should not happen */
		WARN_ON(1);
		offset = 0;
		ret = -EINVAL;
	}

	ipu_cpmem_set_buffer(ch, 0, image->phys0 + offset);
	ipu_cpmem_set_buffer(ch, 1, image->phys1 + offset);

	return ret;
}
EXPORT_SYMBOL_GPL(ipu_cpmem_set_image);

void ipu_cpmem_dump(struct ipuv3_channel *ch)
{
	struct ipu_ch_param __iomem *p = ipu_get_cpmem(ch);
	struct ipu_soc *ipu = ch->ipu;
	int chno = ch->num;

	dev_dbg(ipu->dev, "ch %d word 0 - %08X %08X %08X %08X %08X\n", chno,
		readl(&p->word[0].data[0]),
		readl(&p->word[0].data[1]),
		readl(&p->word[0].data[2]),
		readl(&p->word[0].data[3]),
		readl(&p->word[0].data[4]));
	dev_dbg(ipu->dev, "ch %d word 1 - %08X %08X %08X %08X %08X\n", chno,
		readl(&p->word[1].data[0]),
		readl(&p->word[1].data[1]),
		readl(&p->word[1].data[2]),
		readl(&p->word[1].data[3]),
		readl(&p->word[1].data[4]));
	dev_dbg(ipu->dev, "PFS 0x%x, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_PFS));
	dev_dbg(ipu->dev, "BPP 0x%x, ",
		ipu_ch_param_read_field(ch, IPU_FIELD_BPP));
	dev_dbg(ipu->dev, "NPB 0x%x\n",
		 ipu_ch_param_read_field(ch, IPU_FIELD_NPB));

	dev_dbg(ipu->dev, "FW %d, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_FW));
	dev_dbg(ipu->dev, "FH %d, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_FH));
	dev_dbg(ipu->dev, "EBA0 0x%x\n",
		 ipu_ch_param_read_field(ch, IPU_FIELD_EBA0) << 3);
	dev_dbg(ipu->dev, "EBA1 0x%x\n",
		 ipu_ch_param_read_field(ch, IPU_FIELD_EBA1) << 3);
	dev_dbg(ipu->dev, "Stride %d\n",
		 ipu_ch_param_read_field(ch, IPU_FIELD_SL));
	dev_dbg(ipu->dev, "scan_order %d\n",
		 ipu_ch_param_read_field(ch, IPU_FIELD_SO));
	dev_dbg(ipu->dev, "uv_stride %d\n",
		 ipu_ch_param_read_field(ch, IPU_FIELD_SLUV));
	dev_dbg(ipu->dev, "u_offset 0x%x\n",
		 ipu_ch_param_read_field(ch, IPU_FIELD_UBO) << 3);
	dev_dbg(ipu->dev, "v_offset 0x%x\n",
		 ipu_ch_param_read_field(ch, IPU_FIELD_VBO) << 3);

	dev_dbg(ipu->dev, "Width0 %d+1, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_WID0));
	dev_dbg(ipu->dev, "Width1 %d+1, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_WID1));
	dev_dbg(ipu->dev, "Width2 %d+1, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_WID2));
	dev_dbg(ipu->dev, "Width3 %d+1, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_WID3));
	dev_dbg(ipu->dev, "Offset0 %d, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_OFS0));
	dev_dbg(ipu->dev, "Offset1 %d, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_OFS1));
	dev_dbg(ipu->dev, "Offset2 %d, ",
		 ipu_ch_param_read_field(ch, IPU_FIELD_OFS2));
	dev_dbg(ipu->dev, "Offset3 %d\n",
		 ipu_ch_param_read_field(ch, IPU_FIELD_OFS3));
}
EXPORT_SYMBOL_GPL(ipu_cpmem_dump);

int ipu_cpmem_init(struct ipu_soc *ipu, struct device *dev, unsigned long base)
{
	struct ipu_cpmem *cpmem;

	cpmem = devm_kzalloc(dev, sizeof(*cpmem), GFP_KERNEL);
	if (!cpmem)
		return -ENOMEM;

	ipu->cpmem_priv = cpmem;

	spin_lock_init(&cpmem->lock);
	cpmem->base = devm_ioremap(dev, base, SZ_128K);
	if (!cpmem->base)
		return -ENOMEM;

	dev_dbg(dev, "CPMEM base: 0x%08lx remapped to %p\n",
		base, cpmem->base);
	cpmem->ipu = ipu;

	return 0;
}

void ipu_cpmem_exit(struct ipu_soc *ipu)
{
}
