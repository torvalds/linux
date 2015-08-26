/*
 * Copyright 2005-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __INCLUDE_IPU_PARAM_MEM_H__
#define __INCLUDE_IPU_PARAM_MEM_H__

#include <linux/bitrev.h>
#include <linux/types.h>

#include "ipu_prv.h"

extern u32 *ipu_cpmem_base;

struct ipu_ch_param_word {
	uint32_t data[5];
	uint32_t res[3];
};

struct ipu_ch_param {
	struct ipu_ch_param_word word[2];
};

#define ipu_ch_param_addr(ipu, ch) (((struct ipu_ch_param *)ipu->cpmem_base) + (ch))

#define _param_word(base, w) \
	(((struct ipu_ch_param *)(base))->word[(w)].data)

#define ipu_ch_param_set_field(base, w, bit, size, v) { \
	int i = (bit) / 32; \
	int off = (bit) % 32; \
	_param_word(base, w)[i] |= (v) << off; \
	if (((bit)+(size)-1)/32 > i) { \
		_param_word(base, w)[i + 1] |= (v) >> (off ? (32 - off) : 0); \
	} \
}

#define ipu_ch_param_set_field_io(base, w, bit, size, v) { \
	int i = (bit) / 32; \
	int off = (bit) % 32; \
	unsigned reg_offset; \
	u32 temp; \
	reg_offset = sizeof(struct ipu_ch_param_word) * w / 4; \
	reg_offset += i; \
	temp = readl((u32 *)base + reg_offset); \
	temp |= (v) << off; \
	writel(temp, (u32 *)base + reg_offset); \
	if (((bit)+(size)-1)/32 > i) { \
		reg_offset++; \
		temp = readl((u32 *)base + reg_offset); \
		temp |= (v) >> (off ? (32 - off) : 0); \
		writel(temp, (u32 *)base + reg_offset); \
	} \
}

#define ipu_ch_param_mod_field(base, w, bit, size, v) { \
	int i = (bit) / 32; \
	int off = (bit) % 32; \
	u32 mask = (1UL << size) - 1; \
	u32 temp = _param_word(base, w)[i]; \
	temp &= ~(mask << off); \
	_param_word(base, w)[i] = temp | (v) << off; \
	if (((bit)+(size)-1)/32 > i) { \
		temp = _param_word(base, w)[i + 1]; \
		temp &= ~(mask >> (32 - off)); \
		_param_word(base, w)[i + 1] = \
			temp | ((v) >> (off ? (32 - off) : 0)); \
	} \
}

#define ipu_ch_param_mod_field_io(base, w, bit, size, v) { \
	int i = (bit) / 32; \
	int off = (bit) % 32; \
	u32 mask = (1UL << size) - 1; \
	unsigned reg_offset; \
	u32 temp; \
	reg_offset = sizeof(struct ipu_ch_param_word) * w / 4; \
	reg_offset += i; \
	temp = readl((u32 *)base + reg_offset); \
	temp &= ~(mask << off); \
	temp |= (v) << off; \
	writel(temp, (u32 *)base + reg_offset); \
	if (((bit)+(size)-1)/32 > i) { \
		reg_offset++; \
		temp = readl((u32 *)base + reg_offset); \
		temp &= ~(mask >> (32 - off)); \
		temp |= ((v) >> (off ? (32 - off) : 0)); \
		writel(temp, (u32 *)base + reg_offset); \
	} \
}

#define ipu_ch_param_read_field(base, w, bit, size) ({ \
	u32 temp2; \
	int i = (bit) / 32; \
	int off = (bit) % 32; \
	u32 mask = (1UL << size) - 1; \
	u32 temp1 = _param_word(base, w)[i]; \
	temp1 = mask & (temp1 >> off); \
	if (((bit)+(size)-1)/32 > i) { \
		temp2 = _param_word(base, w)[i + 1]; \
		temp2 &= mask >> (off ? (32 - off) : 0); \
		temp1 |= temp2 << (off ? (32 - off) : 0); \
	} \
	temp1; \
})

#define ipu_ch_param_read_field_io(base, w, bit, size) ({ \
	u32 temp1, temp2; \
	int i = (bit) / 32; \
	int off = (bit) % 32; \
	u32 mask = (1UL << size) - 1; \
	unsigned reg_offset; \
	reg_offset = sizeof(struct ipu_ch_param_word) * w / 4; \
	reg_offset += i; \
	temp1 = readl((u32 *)base + reg_offset); \
	temp1 = mask & (temp1 >> off); \
	if (((bit)+(size)-1)/32 > i) { \
		reg_offset++; \
		temp2 = readl((u32 *)base + reg_offset); \
		temp2 &= mask >> (off ? (32 - off) : 0); \
		temp1 |= temp2 << (off ? (32 - off) : 0); \
	} \
	temp1; \
})

static inline int __ipu_ch_get_third_buf_cpmem_num(int ch)
{
	switch (ch) {
	case 8:
		return 64;
	case 9:
		return 65;
	case 10:
		return 66;
	case 13:
		return 67;
	case 21:
		return 68;
	case 23:
		return 69;
	case 27:
		return 70;
	case 28:
		return 71;
	default:
		return -EINVAL;
	}
	return 0;
}

static inline void _ipu_ch_params_set_packing(struct ipu_ch_param *p,
					      int red_width, int red_offset,
					      int green_width, int green_offset,
					      int blue_width, int blue_offset,
					      int alpha_width, int alpha_offset)
{
	/* Setup red width and offset */
	ipu_ch_param_set_field(p, 1, 116, 3, red_width - 1);
	ipu_ch_param_set_field(p, 1, 128, 5, red_offset);
	/* Setup green width and offset */
	ipu_ch_param_set_field(p, 1, 119, 3, green_width - 1);
	ipu_ch_param_set_field(p, 1, 133, 5, green_offset);
	/* Setup blue width and offset */
	ipu_ch_param_set_field(p, 1, 122, 3, blue_width - 1);
	ipu_ch_param_set_field(p, 1, 138, 5, blue_offset);
	/* Setup alpha width and offset */
	ipu_ch_param_set_field(p, 1, 125, 3, alpha_width - 1);
	ipu_ch_param_set_field(p, 1, 143, 5, alpha_offset);
}

static inline void _ipu_ch_param_dump(struct ipu_soc *ipu, int ch)
{
	struct ipu_ch_param *p = ipu_ch_param_addr(ipu, ch);
	dev_dbg(ipu->dev, "ch %d word 0 - %08X %08X %08X %08X %08X\n", ch,
		 p->word[0].data[0], p->word[0].data[1], p->word[0].data[2],
		 p->word[0].data[3], p->word[0].data[4]);
	dev_dbg(ipu->dev, "ch %d word 1 - %08X %08X %08X %08X %08X\n", ch,
		 p->word[1].data[0], p->word[1].data[1], p->word[1].data[2],
		 p->word[1].data[3], p->word[1].data[4]);
	dev_dbg(ipu->dev, "PFS 0x%x, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 85, 4));
	dev_dbg(ipu->dev, "BPP 0x%x, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 107, 3));
	dev_dbg(ipu->dev, "NPB 0x%x\n",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 78, 7));

	dev_dbg(ipu->dev, "FW %d, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 125, 13));
	dev_dbg(ipu->dev, "FH %d, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 138, 12));
	dev_dbg(ipu->dev, "EBA0 0x%x\n",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 0, 29) << 3);
	dev_dbg(ipu->dev, "EBA1 0x%x\n",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 29, 29) << 3);
	dev_dbg(ipu->dev, "Stride %d\n",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 102, 14));
	dev_dbg(ipu->dev, "scan_order %d\n",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 113, 1));
	dev_dbg(ipu->dev, "uv_stride %d\n",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 128, 14));
	dev_dbg(ipu->dev, "u_offset 0x%x\n",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 46, 22) << 3);
	dev_dbg(ipu->dev, "v_offset 0x%x\n",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 68, 22) << 3);

	dev_dbg(ipu->dev, "Width0 %d+1, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 116, 3));
	dev_dbg(ipu->dev, "Width1 %d+1, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 119, 3));
	dev_dbg(ipu->dev, "Width2 %d+1, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 122, 3));
	dev_dbg(ipu->dev, "Width3 %d+1, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 125, 3));
	dev_dbg(ipu->dev, "Offset0 %d, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 128, 5));
	dev_dbg(ipu->dev, "Offset1 %d, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 133, 5));
	dev_dbg(ipu->dev, "Offset2 %d, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 138, 5));
	dev_dbg(ipu->dev, "Offset3 %d\n",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 143, 5));
}

static inline void fill_cpmem(struct ipu_soc *ipu, int ch, struct ipu_ch_param *params)
{
	int i, w;
	void *addr = ipu_ch_param_addr(ipu, ch);

	/* 2 words, 5 valid data */
	for (w = 0; w < 2; w++) {
		for (i = 0; i < 5; i++) {
			writel(params->word[w].data[i], addr);
			addr += 4;
		}
		addr += 12;
	}
}

static inline void _ipu_ch_param_init(struct ipu_soc *ipu, int ch,
				      uint32_t pixel_fmt, uint32_t width,
				      uint32_t height, uint32_t stride,
				      uint32_t u, uint32_t v,
				      uint32_t uv_stride, dma_addr_t addr0,
				      dma_addr_t addr1, dma_addr_t addr2)
{
	uint32_t u_offset = 0;
	uint32_t v_offset = 0;
	uint32_t bs = 0;
	int32_t sub_ch = 0;
	struct ipu_ch_param params;

	memset(&params, 0, sizeof(params));

	ipu_ch_param_set_field(&params, 0, 125, 13, width - 1);

	if (((ch == 8) || (ch == 9) || (ch == 10)) && !ipu->vdoa_en) {
		ipu_ch_param_set_field(&params, 0, 138, 12, (height / 2) - 1);
		ipu_ch_param_set_field(&params, 1, 102, 14, (stride * 2) - 1);
	} else {
		/* note: for vdoa+vdi- ch8/9/10, always use band mode */
		ipu_ch_param_set_field(&params, 0, 138, 12, height - 1);
		ipu_ch_param_set_field(&params, 1, 102, 14, stride - 1);
	}

	/* EBA is 8-byte aligned */
	ipu_ch_param_set_field(&params, 1, 0, 29, addr0 >> 3);
	ipu_ch_param_set_field(&params, 1, 29, 29, addr1 >> 3);
	if (addr0%8)
		dev_warn(ipu->dev,
			 "IDMAC%d's EBA0 is not 8-byte aligned\n", ch);
	if (addr1%8)
		dev_warn(ipu->dev,
			 "IDMAC%d's EBA1 is not 8-byte aligned\n", ch);

	switch (pixel_fmt) {
	case IPU_PIX_FMT_GENERIC:
		/*Represents 8-bit Generic data */
		ipu_ch_param_set_field(&params, 0, 107, 3, 5);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 6);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 63);	/* burst size */

		break;
	case IPU_PIX_FMT_GENERIC_16:
		/* Represents 16-bit generic data */
		ipu_ch_param_set_field(&params, 0, 107, 3, 3);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 6);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */

		break;
	case IPU_PIX_FMT_GENERIC_32:
		/*Represents 32-bit Generic data */
		break;
	case IPU_PIX_FMT_RGB565:
		ipu_ch_param_set_field(&params, 0, 107, 3, 3);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */

		_ipu_ch_params_set_packing(&params, 5, 0, 6, 5, 5, 11, 8, 16);
		break;
	case IPU_PIX_FMT_BGRA4444:
		ipu_ch_param_set_field(&params, 0, 107, 3, 3);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */

		_ipu_ch_params_set_packing(&params, 4, 4, 4, 8, 4, 12, 4, 0);
		break;
	case IPU_PIX_FMT_BGRA5551:
		ipu_ch_param_set_field(&params, 0, 107, 3, 3);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */

		_ipu_ch_params_set_packing(&params, 5, 1, 5, 6, 5, 11, 1, 0);
		break;
	case IPU_PIX_FMT_BGR24:
		ipu_ch_param_set_field(&params, 0, 107, 3, 1);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 19);	/* burst size */

		_ipu_ch_params_set_packing(&params, 8, 0, 8, 8, 8, 16, 8, 24);
		break;
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_YUV444:
		ipu_ch_param_set_field(&params, 0, 107, 3, 1);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 19);	/* burst size */

		_ipu_ch_params_set_packing(&params, 8, 16, 8, 8, 8, 0, 8, 24);
		break;
	case IPU_PIX_FMT_VYU444:
		ipu_ch_param_set_field(&params, 0, 107, 3, 1);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 19);	/* burst size */

		_ipu_ch_params_set_packing(&params, 8, 8, 8, 0, 8, 16, 8, 24);
		break;
	case IPU_PIX_FMT_AYUV:
		ipu_ch_param_set_field(&params, 0, 107, 3, 0);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 15);	/* burst size */

		_ipu_ch_params_set_packing(&params, 8, 8, 8, 16, 8, 24, 8, 0);
		break;
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_BGR32:
		ipu_ch_param_set_field(&params, 0, 107, 3, 0);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 15);	/* burst size */

		_ipu_ch_params_set_packing(&params, 8, 8, 8, 16, 8, 24, 8, 0);
		break;
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_RGB32:
		ipu_ch_param_set_field(&params, 0, 107, 3, 0);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 15);	/* burst size */

		_ipu_ch_params_set_packing(&params, 8, 24, 8, 16, 8, 8, 8, 0);
		break;
	case IPU_PIX_FMT_ABGR32:
		ipu_ch_param_set_field(&params, 0, 107, 3, 0);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 7);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 15);	/* burst size */

		_ipu_ch_params_set_packing(&params, 8, 0, 8, 8, 8, 16, 8, 24);
		break;
	case IPU_PIX_FMT_UYVY:
		ipu_ch_param_set_field(&params, 0, 107, 3, 3);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 0xA);	/* pix format */
		if ((ch == 8) || (ch == 9) || (ch == 10)) {
			ipu_ch_param_set_field(&params, 1, 78, 7, 15);  /* burst size */
		} else {
			ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */
		}
		break;
	case IPU_PIX_FMT_YUYV:
		ipu_ch_param_set_field(&params, 0, 107, 3, 3);	/* bits/pixel */
		ipu_ch_param_set_field(&params, 1, 85, 4, 0x8);	/* pix format */
		if ((ch == 8) || (ch == 9) || (ch == 10)) {
			if (ipu->vdoa_en) {
				ipu_ch_param_set_field(&params, 1, 78, 7, 31);
			} else {
				ipu_ch_param_set_field(&params, 1, 78, 7, 15);
			}
		} else {
			ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */
		}
		break;
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YUV420P:
		ipu_ch_param_set_field(&params, 1, 85, 4, 2);	/* pix format */

		if (uv_stride < stride / 2)
			uv_stride = stride / 2;

		u_offset = stride * height;
		v_offset = u_offset + (uv_stride * height / 2);
		if ((ch == 8) || (ch == 9) || (ch == 10)) {
			ipu_ch_param_set_field(&params, 1, 78, 7, 15);  /* burst size */
			uv_stride = uv_stride*2;
		} else {
			if (_ipu_is_smfc_chan(ch) &&
				ipu->smfc_idmac_12bit_3planar_bs_fixup)
				bs = 15;
			else
				bs = 31;
			ipu_ch_param_set_field(&params, 1, 78, 7, bs);  /* burst size */
		}
		break;
	case IPU_PIX_FMT_YVU420P:
		ipu_ch_param_set_field(&params, 1, 85, 4, 2);	/* pix format */

		if (uv_stride < stride / 2)
			uv_stride = stride / 2;

		v_offset = stride * height;
		u_offset = v_offset + (uv_stride * height / 2);
		if ((ch == 8) || (ch == 9) || (ch == 10)) {
			ipu_ch_param_set_field(&params, 1, 78, 7, 15);  /* burst size */
			uv_stride = uv_stride*2;
		} else {
			if (_ipu_is_smfc_chan(ch) &&
				ipu->smfc_idmac_12bit_3planar_bs_fixup)
				bs = 15;
			else
				bs = 31;
			ipu_ch_param_set_field(&params, 1, 78, 7, bs);  /* burst size */
		}
		break;
	case IPU_PIX_FMT_YVU422P:
		/* BPP & pixel format */
		ipu_ch_param_set_field(&params, 1, 85, 4, 1);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */

		if (uv_stride < stride / 2)
			uv_stride = stride / 2;

		v_offset = (v == 0) ? stride * height : v;
		u_offset = (u == 0) ? v_offset + v_offset / 2 : u;
		break;
	case IPU_PIX_FMT_YUV422P:
		/* BPP & pixel format */
		ipu_ch_param_set_field(&params, 1, 85, 4, 1);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */

		if (uv_stride < stride / 2)
			uv_stride = stride / 2;

		u_offset = (u == 0) ? stride * height : u;
		v_offset = (v == 0) ? u_offset + u_offset / 2 : v;
		break;
	case IPU_PIX_FMT_YUV444P:
		/* BPP & pixel format */
		ipu_ch_param_set_field(&params, 1, 85, 4, 0);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */
		uv_stride = stride;
		u_offset = (u == 0) ? stride * height : u;
		v_offset = (v == 0) ? u_offset * 2 : v;
		break;
	case IPU_PIX_FMT_NV16:
		ipu_ch_param_set_field(&params, 1, 85, 4, 1);	/* pix format */
		ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */
		uv_stride = stride;
		u_offset = (u == 0) ? stride * height : u;
		break;
	case IPU_PIX_FMT_NV12:
		/* BPP & pixel format */
		ipu_ch_param_set_field(&params, 1, 85, 4, 4);	/* pix format */
		uv_stride = stride;
		u_offset = (u == 0) ? stride * height : u;
		if ((ch == 8) || (ch == 9) || (ch == 10)) {
			if (ipu->vdoa_en) {
				 /* one field buffer, memory width 64bits */
				ipu_ch_param_set_field(&params, 1, 78, 7, 63);
			} else {
				ipu_ch_param_set_field(&params, 1, 78, 7, 15);
				 /* top/bottom field in one buffer*/
				uv_stride = uv_stride*2;
			}
		} else {
			ipu_ch_param_set_field(&params, 1, 78, 7, 31);	/* burst size */
		}
		break;
	default:
		dev_err(ipu->dev, "mxc ipu: unimplemented pixel format\n");
		break;
	}
	/*set burst size to 16*/


	if (uv_stride)
		ipu_ch_param_set_field(&params, 1, 128, 14, uv_stride - 1);

	/* Get the uv offset from user when need cropping */
	if (u || v) {
		u_offset = u;
		v_offset = v;
	}

	/* UBO and VBO are 22-bit and 8-byte aligned */
	if (u_offset/8 > 0x3fffff)
		dev_warn(ipu->dev,
			 "IDMAC%d's U offset exceeds IPU limitation\n", ch);
	if (v_offset/8 > 0x3fffff)
		dev_warn(ipu->dev,
			 "IDMAC%d's V offset exceeds IPU limitation\n", ch);
	if (u_offset%8)
		dev_warn(ipu->dev,
			 "IDMAC%d's U offset is not 8-byte aligned\n", ch);
	if (v_offset%8)
		dev_warn(ipu->dev,
			 "IDMAC%d's V offset is not 8-byte aligned\n", ch);

	ipu_ch_param_set_field(&params, 0, 46, 22, u_offset / 8);
	ipu_ch_param_set_field(&params, 0, 68, 22, v_offset / 8);

	dev_dbg(ipu->dev, "initializing idma ch %d @ %p\n", ch, ipu_ch_param_addr(ipu, ch));
	fill_cpmem(ipu, ch, &params);
	if (addr2) {
		sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
		if (sub_ch <= 0)
			return;

		ipu_ch_param_set_field(&params, 1, 0, 29, addr2 >> 3);
		ipu_ch_param_set_field(&params, 1, 29, 29, 0);
		if (addr2%8)
			dev_warn(ipu->dev,
				 "IDMAC%d's sub-CPMEM entry%d EBA0 is not "
				 "8-byte aligned\n", ch, sub_ch);

		dev_dbg(ipu->dev, "initializing idma ch %d @ %p sub cpmem\n", ch,
					ipu_ch_param_addr(ipu, sub_ch));
		fill_cpmem(ipu, sub_ch, &params);
	}
};

static inline void _ipu_ch_param_set_burst_size(struct ipu_soc *ipu,
						uint32_t ch,
						uint16_t burst_pixels)
{
	int32_t sub_ch = 0;

	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 1, 78, 7,
			       burst_pixels - 1);

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;
	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 1, 78, 7,
			       burst_pixels - 1);
};

static inline int _ipu_ch_param_get_burst_size(struct ipu_soc *ipu, uint32_t ch)
{
	return ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 78, 7) + 1;
};

static inline int _ipu_ch_param_get_bpp(struct ipu_soc *ipu, uint32_t ch)
{
	return ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 107, 3);
};

static inline void _ipu_ch_param_set_buffer(struct ipu_soc *ipu, uint32_t ch,
					int bufNum, dma_addr_t phyaddr)
{
	if (bufNum == 2) {
		ch = __ipu_ch_get_third_buf_cpmem_num(ch);
		if (ch <= 0)
			return;
		bufNum = 0;
	}

	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 1, 29 * bufNum, 29,
			       phyaddr / 8);
};

static inline void _ipu_ch_param_set_rotation(struct ipu_soc *ipu, uint32_t ch,
					      ipu_rotate_mode_t rot)
{
	u32 temp_rot = bitrev8(rot) >> 5;
	int32_t sub_ch = 0;

	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 0, 119, 3, temp_rot);

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;
	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 0, 119, 3, temp_rot);
};

static inline void _ipu_ch_param_set_block_mode(struct ipu_soc *ipu, uint32_t ch)
{
	int32_t sub_ch = 0;

	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 0, 117, 2, 1);

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;
	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 0, 117, 2, 1);
};

static inline void _ipu_ch_param_set_alpha_use_separate_channel(struct ipu_soc *ipu,
								uint32_t ch,
								bool option)
{
	int32_t sub_ch = 0;

	if (option) {
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 1, 89, 1, 1);
	} else {
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 1, 89, 1, 0);
	}

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;

	if (option) {
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 1, 89, 1, 1);
	} else {
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 1, 89, 1, 0);
	}
};

static inline void _ipu_ch_param_set_alpha_condition_read(struct ipu_soc *ipu, uint32_t ch)
{
	int32_t sub_ch = 0;

	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 1, 149, 1, 1);

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;
	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 1, 149, 1, 1);
};

static inline void _ipu_ch_param_set_alpha_buffer_memory(struct ipu_soc *ipu, uint32_t ch)
{
	int alp_mem_idx;
	int32_t sub_ch = 0;

	switch (ch) {
	case 14: /* PRP graphic */
		alp_mem_idx = 0;
		break;
	case 15: /* PP graphic */
		alp_mem_idx = 1;
		break;
	case 23: /* DP BG SYNC graphic */
		alp_mem_idx = 4;
		break;
	case 27: /* DP FG SYNC graphic */
		alp_mem_idx = 2;
		break;
	default:
		dev_err(ipu->dev, "unsupported correlative channel of local "
			"alpha channel\n");
		return;
	}

	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 1, 90, 3, alp_mem_idx);

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;
	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 1, 90, 3, alp_mem_idx);
};

static inline void _ipu_ch_param_set_interlaced_scan(struct ipu_soc *ipu, uint32_t ch)
{
	u32 stride;
	int32_t sub_ch = 0;

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);

	ipu_ch_param_set_field_io(ipu_ch_param_addr(ipu, ch), 0, 113, 1, 1);
	if (sub_ch > 0)
		ipu_ch_param_set_field_io(ipu_ch_param_addr(ipu, sub_ch), 0, 113, 1, 1);
	stride = ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 102, 14) + 1;
	/* ILO is 20-bit and 8-byte aligned */
	if (stride/8 > 0xfffff)
		dev_warn(ipu->dev,
			 "IDMAC%d's ILO exceeds IPU limitation\n", ch);
	if (stride%8)
		dev_warn(ipu->dev,
			 "IDMAC%d's ILO is not 8-byte aligned\n", ch);
	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 1, 58, 20, stride / 8);
	if (sub_ch > 0)
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 1, 58, 20,
				       stride / 8);
	stride *= 2;
	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 1, 102, 14, stride - 1);
	if (sub_ch > 0)
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 1, 102, 14,
				       stride - 1);
};

static inline void _ipu_ch_param_set_axi_id(struct ipu_soc *ipu, uint32_t ch, uint32_t id)
{
	int32_t sub_ch = 0;

	id %= 4;

	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 1, 93, 2, id);

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;
	ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 1, 93, 2, id);
};

static inline int _ipu_ch_param_get_axi_id(struct ipu_soc *ipu, uint32_t ch)
{
	return ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 1, 93, 2);
}

static inline int __ipu_ch_offset_calc(uint32_t pixel_fmt,
				       uint32_t width,
				       uint32_t height,
				       uint32_t stride,
				       uint32_t u,
				       uint32_t v,
				       uint32_t uv_stride,
				       uint32_t vertical_offset,
				       uint32_t horizontal_offset,
				       uint32_t *u_offset,
				       uint32_t *v_offset)
{
	uint32_t u_fix = 0;
	uint32_t v_fix = 0;

	switch (pixel_fmt) {
	case IPU_PIX_FMT_GENERIC:
	case IPU_PIX_FMT_GENERIC_16:
	case IPU_PIX_FMT_GENERIC_32:
	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_YUV444:
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_RGB32:
	case IPU_PIX_FMT_ABGR32:
	case IPU_PIX_FMT_UYVY:
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_GPU32_SB_ST:
	case IPU_PIX_FMT_GPU32_SB_SRT:
	case IPU_PIX_FMT_GPU32_ST:
	case IPU_PIX_FMT_GPU32_SRT:
	case IPU_PIX_FMT_GPU16_SB_ST:
	case IPU_PIX_FMT_GPU16_SB_SRT:
	case IPU_PIX_FMT_GPU16_ST:
	case IPU_PIX_FMT_GPU16_SRT:
		*u_offset = 0;
		*v_offset = 0;
		break;
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YUV420P:
		if (uv_stride < stride / 2)
			uv_stride = stride / 2;

		*u_offset = stride * (height - vertical_offset - 1) +
					(stride - horizontal_offset) +
					(uv_stride * vertical_offset / 2) +
					horizontal_offset / 2;
		*v_offset = *u_offset + (uv_stride * height / 2);
		u_fix = u ? (u + (uv_stride * vertical_offset / 2) +
					(horizontal_offset / 2) -
					(stride * vertical_offset) - (horizontal_offset)) :
					*u_offset;
		v_fix = v ? (v + (uv_stride * vertical_offset / 2) +
					(horizontal_offset / 2) -
					(stride * vertical_offset) - (horizontal_offset)) :
					*v_offset;
		break;
	case IPU_PIX_FMT_YVU420P:
		if (uv_stride < stride / 2)
			uv_stride = stride / 2;

		*v_offset = stride * (height - vertical_offset - 1) +
					(stride - horizontal_offset) +
					(uv_stride * vertical_offset / 2) +
					horizontal_offset / 2;
		*u_offset = *v_offset + (uv_stride * height / 2);
		u_fix = u ? (u + (uv_stride * vertical_offset / 2) +
					(horizontal_offset / 2) -
					(stride * vertical_offset) - (horizontal_offset)) :
					*u_offset;
		v_fix = v ? (v + (uv_stride * vertical_offset / 2) +
					(horizontal_offset / 2) -
					(stride * vertical_offset) - (horizontal_offset)) :
					*v_offset;
		break;
	case IPU_PIX_FMT_YVU422P:
		if (uv_stride < stride / 2)
			uv_stride = stride / 2;

		*v_offset = stride * (height - vertical_offset - 1) +
					(stride - horizontal_offset) +
					(uv_stride * vertical_offset) +
					horizontal_offset / 2;
		*u_offset = *v_offset + uv_stride * height;
		u_fix = u ? (u + (uv_stride * vertical_offset) +
					horizontal_offset / 2 -
					(stride * vertical_offset) - (horizontal_offset)) :
					*u_offset;
		v_fix = v ? (v + (uv_stride * vertical_offset) +
					horizontal_offset / 2 -
					(stride * vertical_offset) - (horizontal_offset)) :
					*v_offset;
		break;
	case IPU_PIX_FMT_YUV422P:
		if (uv_stride < stride / 2)
			uv_stride = stride / 2;

		*u_offset = stride * (height - vertical_offset - 1) +
					(stride - horizontal_offset) +
					(uv_stride * vertical_offset) +
					horizontal_offset / 2;
		*v_offset = *u_offset + uv_stride * height;
		u_fix = u ? (u + (uv_stride * vertical_offset) +
					horizontal_offset / 2 -
					(stride * vertical_offset) - (horizontal_offset)) :
					*u_offset;
		v_fix = v ? (v + (uv_stride * vertical_offset) +
					horizontal_offset / 2 -
					(stride * vertical_offset) - (horizontal_offset)) :
					*v_offset;
		break;
	case IPU_PIX_FMT_YUV444P:
		uv_stride = stride;
		*u_offset = stride * (height - vertical_offset - 1) +
					(stride - horizontal_offset) +
					(uv_stride * vertical_offset) +
					horizontal_offset;
		*v_offset = *u_offset + uv_stride * height;
		u_fix = u ? (u + (uv_stride * vertical_offset) +
					horizontal_offset -
					(stride * vertical_offset) -
					(horizontal_offset)) :
					*u_offset;
		v_fix = v ? (v + (uv_stride * vertical_offset) +
					horizontal_offset -
					(stride * vertical_offset) -
					(horizontal_offset)) :
					*v_offset;
		break;
	case IPU_PIX_FMT_NV12:
	case IPU_PIX_FMT_NV16:
	case PRE_PIX_FMT_NV21:
	case PRE_PIX_FMT_NV61:
		uv_stride = stride;
		*u_offset = stride * (height - vertical_offset - 1) +
					(stride - horizontal_offset) +
					(uv_stride * vertical_offset / 2) +
					horizontal_offset;
		*v_offset = 0;
		u_fix = u ? (u + (uv_stride * vertical_offset / 2) +
					horizontal_offset -
					(stride * vertical_offset) - (horizontal_offset)) :
					*u_offset;
		break;
	default:
		return -EINVAL;
	}

	if (u_fix > *u_offset)
		*u_offset = u_fix;

	if (v_fix > *v_offset)
		*v_offset = v_fix;

	return 0;
}

/* IDMAC U/V offset changing support */
/* U and V input is not affected, */
/* the update is done by new calculation according to */
/* vertical_offset and horizontal_offset */
static inline void _ipu_ch_offset_update(struct ipu_soc *ipu,
					 int ch,
					 uint32_t pixel_fmt,
					 uint32_t width,
					 uint32_t height,
					 uint32_t stride,
					 uint32_t u,
					 uint32_t v,
					 uint32_t uv_stride,
					 uint32_t vertical_offset,
					 uint32_t horizontal_offset)
{
	uint32_t u_offset = 0;
	uint32_t v_offset = 0;
	uint32_t old_offset = 0;
	int32_t sub_ch = 0;
	int ret;

	ret = __ipu_ch_offset_calc(pixel_fmt, width, height, stride,
				   u, v, uv_stride,
				   vertical_offset, horizontal_offset,
				   &u_offset, &v_offset);
	if (ret) {
		dev_err(ipu->dev, "mxc ipu: unimplemented pixel format\n");
		return;
	}

	/* UBO and VBO are 22-bit and 8-byte aligned */
	if (u_offset/8 > 0x3fffff)
		dev_warn(ipu->dev,
			"IDMAC%d's U offset exceeds IPU limitation\n", ch);
	if (v_offset/8 > 0x3fffff)
		dev_warn(ipu->dev,
			"IDMAC%d's V offset exceeds IPU limitation\n", ch);
	if (u_offset%8)
		dev_warn(ipu->dev,
			"IDMAC%d's U offset is not 8-byte aligned\n", ch);
	if (v_offset%8)
		dev_warn(ipu->dev,
			"IDMAC%d's V offset is not 8-byte aligned\n", ch);

	old_offset = ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 46, 22);
	if (old_offset != u_offset / 8)
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 0, 46, 22, u_offset / 8);
	old_offset = ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 68, 22);
	if (old_offset != v_offset / 8)
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, ch), 0, 68, 22, v_offset / 8);

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;
	old_offset = ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, sub_ch), 0, 46, 22);
	if (old_offset != u_offset / 8)
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 0, 46, 22, u_offset / 8);
	old_offset = ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, sub_ch), 0, 68, 22);
	if (old_offset != v_offset / 8)
		ipu_ch_param_mod_field_io(ipu_ch_param_addr(ipu, sub_ch), 0, 68, 22, v_offset / 8);
};

static inline void _ipu_ch_params_set_alpha_width(struct ipu_soc *ipu, uint32_t ch, int alpha_width)
{
	int32_t sub_ch = 0;

	ipu_ch_param_set_field_io(ipu_ch_param_addr(ipu, ch), 1, 125, 3, alpha_width - 1);

	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;
	ipu_ch_param_set_field_io(ipu_ch_param_addr(ipu, sub_ch), 1, 125, 3, alpha_width - 1);
};

static inline void _ipu_ch_param_set_bandmode(struct ipu_soc *ipu,
			uint32_t ch, uint32_t band_height)
{
	int32_t sub_ch = 0;

	ipu_ch_param_set_field_io(ipu_ch_param_addr(ipu, ch),
					0, 114, 3, band_height - 1);
	sub_ch = __ipu_ch_get_third_buf_cpmem_num(ch);
	if (sub_ch <= 0)
		return;
	ipu_ch_param_set_field_io(ipu_ch_param_addr(ipu, sub_ch),
					0, 114, 3, band_height - 1);

	dev_dbg(ipu->dev, "BNDM 0x%x, ",
		 ipu_ch_param_read_field_io(ipu_ch_param_addr(ipu, ch), 0, 114, 3));
}

/*
 * The IPUv3 IDMAC has a bug to read 32bpp pixels from a graphics plane
 * whose alpha component is at the most significant 8 bits. The bug only
 * impacts on cases in which the relevant separate alpha channel is enabled.
 *
 * Return true on bad alpha component position, otherwise, return false.
 */
static inline bool _ipu_ch_param_bad_alpha_pos(uint32_t pixel_fmt)
{
	switch (pixel_fmt) {
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_RGB32:
		return true;
	}

	return false;
}
#endif
