/*
 * linux/drivers/video/rockchip/bmp_helper.c
 *
 * Copyright (C) 2012 Rockchip Corporation
 * Author: Mark Yao <mark.yao@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/rk_fb.h>

#include "bmp_helper.h"

unsigned short bmp_logo_palette[] = {
	0x0000, 0x0021, 0x0840, 0x0841, 0x0041, 0x0862, 0x0043, 0x1060,
	0x1061, 0x1081, 0x18a1, 0x1082, 0x18c3, 0x18c2, 0x18e4, 0x1904,
	0x20e2, 0x2901, 0x2123, 0x2902, 0x3123, 0x3983, 0x2104, 0x2945,
	0x2924, 0x2966, 0x3144, 0x3185, 0x3984, 0x39a5, 0x31a6, 0x39c7,
	0x51a2, 0x41c4, 0x61e4, 0x4a02, 0x4a24, 0x5a65, 0x5245, 0x5226,
	0x5a66, 0x62a3, 0x7202, 0x6ac4, 0x62c7, 0x72c5, 0x7285, 0x7b03,
	0x6b05, 0x6b07, 0x7b46, 0x7326, 0x4228, 0x4a69, 0x5a88, 0x528a,
	0x5aeb, 0x62a8, 0x7b4a, 0x7ba9, 0x630c, 0x6b4d, 0x73ae, 0x7bcf,
	0x92c4, 0x8b25, 0x83a4, 0x8bc6, 0x9b65, 0x9ba6, 0xa2a2, 0xa364,
	0xa324, 0xabe5, 0xb364, 0xb3a4, 0xb386, 0x8369, 0x83a8, 0x8b8b,
	0x93a8, 0x8bcc, 0xc3c4, 0xebc1, 0x9c23, 0x9c04, 0x9406, 0x9427,
	0xac23, 0xb483, 0xa445, 0xa407, 0xacc7, 0xbc64, 0xb4c4, 0xbd26,
	0x9c2d, 0xac4c, 0xbd29, 0xc4c2, 0xc4e4, 0xc4a4, 0xdce5, 0xcc44,
	0xc563, 0xdd02, 0xdd03, 0xdd83, 0xc544, 0xcd87, 0xd544, 0xdd24,
	0xdd84, 0xddc4, 0xd5c7, 0xe462, 0xe463, 0xe4c1, 0xeca3, 0xecc2,
	0xecc2, 0xf442, 0xf4a3, 0xe444, 0xec65, 0xe485, 0xeca5, 0xecc4,
	0xecc5, 0xe4a4, 0xf465, 0xf4a4, 0xed22, 0xed23, 0xed62, 0xed63,
	0xe522, 0xedc2, 0xfd20, 0xfd02, 0xfde1, 0xfdc1, 0xf5c3, 0xf5c3,
	0xe5c1, 0xed04, 0xed25, 0xed64, 0xed65, 0xe505, 0xed66, 0xed26,
	0xed84, 0xeda5, 0xede4, 0xedc5, 0xe5e4, 0xf525, 0xf5e4, 0xf5e5,
	0xf5a4, 0xed49, 0xeda8, 0xedab, 0xf5eb, 0xf5cb, 0xedac, 0xf5cc,
	0xf5ce, 0xee21, 0xee42, 0xee22, 0xfe21, 0xf602, 0xfe63, 0xfe22,
	0xfea0, 0xfea3, 0xfee2, 0xfec3, 0xf682, 0xee65, 0xe624, 0xee85,
	0xf625, 0xf664, 0xf645, 0xfe64, 0xf624, 0xf606, 0xf684, 0xf685,
	0xfea4, 0xfee4, 0xf6c4, 0xf6a6, 0xff03, 0xff02, 0xee2f, 0xf60d,
	0xf62e, 0xf64f, 0xf64e, 0x8410, 0x8c51, 0x94b2, 0x9cd3, 0xa4b0,
	0xbd30, 0xbd72, 0xa534, 0xad55, 0xb596, 0xbdd7, 0xde75, 0xf671,
	0xf691, 0xf692, 0xf6b3, 0xf6d3, 0xfeb3, 0xf673, 0xe6b6, 0xf6d4,
	0xf6f5, 0xfef5, 0xf6f6, 0xfef6, 0xff15, 0xf716, 0xff16, 0xff17,
	0xc618, 0xce79, 0xd69a, 0xdefb, 0xef19, 0xff38, 0xff58, 0xff79,
	0xf718, 0xff7a, 0xf75a, 0xff99, 0xff9a, 0xffbb, 0xffdb, 0xe73c,
	0xef5d, 0xfffc, 0xf7be, 0xffff, 0x0000, 0x0000, 0x0000, 0x0000,
};

static void draw_unencoded_bitmap(uint16_t **dst, uint8_t *bmap, uint16_t *cmap,
				  uint32_t cnt)
{
	while (cnt > 0) {
		*(*dst)++ = cmap[*bmap++];
		cnt--;
	}
}

static void draw_encoded_bitmap(uint16_t **dst, uint16_t c, uint32_t cnt)
{
	uint16_t *fb = *dst;
	int cnt_8copy = cnt >> 3;

	cnt -= cnt_8copy << 3;
	while (cnt_8copy > 0) {
		*fb++ = c;
		*fb++ = c;
		*fb++ = c;
		*fb++ = c;
		*fb++ = c;
		*fb++ = c;
		*fb++ = c;
		*fb++ = c;
		cnt_8copy--;
	}
	while (cnt > 0) {
		*fb++ = c;
		cnt--;
	}
	*dst = fb;
}

static void yuv_to_rgb(int y, int u, int v, int *r, int *g, int *b)
{
	int rdif, invgdif, bdif;

	u -= 128;
	v -= 128;
	rdif = v + ((v * 103) >> 8);
	invgdif = ((u * 88) >> 8) + ((v * 183) >> 8);
	bdif = u + ((u*198) >> 8);
	*r = range(y + rdif, 0, 0xff);
	*g = range(y - invgdif, 0, 0xff);
	*b = range(y + bdif, 0, 0xff);
}

int bmpencoder(void *__iomem *vaddr, int width, int height, u8 data_format,
	       void *data, void (*fn)(void *, void *, int))
{
	uint32_t *d, *d1, *d2;
	uint8_t *dst, *yrgb, *uv, *y1, *y2;
	int y, u, v, r, g, b;

	int yu = width * 4 % 4;
	int byteperline;
	unsigned int size;
	BITMAPHEADER header;
	BITMAPINFOHEADER infoheader;
	void *buf;
	int i, j;

	yu = yu != 0 ? 4 - yu : yu;
	byteperline = width * 4 + yu;
	size = byteperline * height + 54;
	memset(&header, 0, sizeof(header));
	memset(&infoheader, 0, sizeof(infoheader));
	header.type = 'M'<<8|'B';
	header.size = size;
	header.offset = 54;

	infoheader.size = 40;
	infoheader.width = width;
	infoheader.height = 0 - height;
	infoheader.bitcount = 4 * 8;
	infoheader.compression = 0;
	infoheader.imagesize = byteperline * height;
	infoheader.xpelspermeter = 0;
	infoheader.ypelspermeter = 0;
	infoheader.colors = 0;
	infoheader.colorsimportant = 0;
	fn(data, (void *)&header, sizeof(header));
	fn(data, (void *)&infoheader, sizeof(infoheader));

	/*
	 * if data_format is ARGB888 or XRGB888, not need convert.
	 */
	if (data_format == ARGB888 || data_format == XRGB888) {
		fn(data, (char *)vaddr, width * height * 4);
		return 0;
	}
	/*
	 * alloc 2 line buffer.
	 */
	buf = kmalloc(width * 2 * 4, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	yrgb = (uint8_t *)vaddr;
	uv = yrgb + width * height;
	for (j = 0; j < height; j++) {
		if (j % 2 == 0) {
			dst = buf;
			y1 = yrgb + j * width;
			y2 = y1 + width;
			d1 = buf;
			d2 = d1 + width;
		}

		for (i = 0; i < width; i++) {
			switch (data_format) {
			case XBGR888:
			case ABGR888:
				dst[0] = yrgb[2];
				dst[1] = yrgb[1];
				dst[2] = yrgb[0];
				dst[3] = yrgb[3];
				dst += 4;
				yrgb += 4;
				break;
			case RGB888:
				dst[0] = yrgb[0];
				dst[1] = yrgb[1];
				dst[2] = yrgb[2];
				dst[3] = 0xff;
				dst += 4;
				yrgb += 3;
				break;
			case RGB565:
				dst[0] = (yrgb[0] & 0x1f) << 3;
				dst[1] = (yrgb[0] & 0xe0) >> 3 |
						(yrgb[1] & 0x7) << 5;
				dst[2] = yrgb[1] & 0xf8;
				dst[3] = 0xff;
				dst += 4;
				yrgb += 2;
				break;
			case YUV420:
			case YUV422:
			case YUV444:
				if (data_format == YUV420) {
					if (i % 2 == 0) {
						d = d1++;
						y = *y1++;
					} else {
						d = d2++;
						y = *y2++;
					}
					if (i % 4 == 0) {
						u = *uv++;
						v = *uv++;
					}
				} else if (data_format == YUV422) {
					if (i % 2 == 0) {
						u = *uv++;
						v = *uv++;
					}
					d = d1++;
				} else {
					u = *uv++;
					v = *uv++;
					d = d1++;
				}
				yuv_to_rgb(y, u, v, &r, &g, &b);
				*d = 0xff<<24 | r << 16 | g << 8 | b;
				break;
			case YUV422_A:
			case YUV444_A:
			default:
				pr_err("unsupport now\n");
				return -EINVAL;
			}
		}
		if (j % 2 == 1)
			fn(data, (char *)buf, 2 * width * 4);
	}

	return 0;
}

static void decode_rle8_bitmap(uint8_t *psrc, uint8_t *pdst, uint16_t *cmap,
			       unsigned int width, unsigned int height,
			       int bits, int x_off, int y_off, bool flip)
{
	uint32_t cnt, runlen;
	int x = 0, y = 0;
	int decode = 1;
	uint8_t *bmap = psrc;
	uint8_t *dst = pdst;
	int linesize = width * 2;

	if (flip) {
		y = height - 1;
		dst = pdst + y * linesize;
	}

	while (decode) {
		if (bmap[0] == BMP_RLE8_ESCAPE) {
			switch (bmap[1]) {
			case BMP_RLE8_EOL:
				/* end of line */
				bmap += 2;
				x = 0;
				if (flip) {
					y--;
					dst -= linesize * 2;
				} else {
					y++;
				}
				break;
			case BMP_RLE8_EOBMP:
				/* end of bitmap */
				decode = 0;
				break;
			case BMP_RLE8_DELTA:
				/* delta run */
				x += bmap[2];
				if (flip) {
					y -= bmap[3];
					dst -= bmap[3] * linesize;
					dst += bmap[2] * 2;
				} else {
					y += bmap[3];
					dst += bmap[3] * linesize;
					dst += bmap[2] * 2;
				}
				bmap += 4;
				break;
			default:
				/* unencoded run */
				runlen = bmap[1];
				bmap += 2;
				if (y >= height || x >= width) {
					decode = 0;
					break;
				}
				if (x + runlen > width)
					cnt = width - x;
				else
					cnt = runlen;
				draw_unencoded_bitmap((uint16_t **)&dst, bmap,
						      cmap, cnt);
				x += runlen;
				bmap += runlen;
				if (runlen & 1)
					bmap++;
			}
		} else {
			/* encoded run */
			if (y < height) {
				runlen = bmap[0];
				if (x < width) {
					/* aggregate the same code */
					while (bmap[0] == 0xff &&
					       bmap[2] != BMP_RLE8_ESCAPE &&
					       bmap[1] == bmap[3]) {
						runlen += bmap[2];
						bmap += 2;
					}
					if (x + runlen > width)
						cnt = width - x;
					else
						cnt = runlen;
					draw_encoded_bitmap((uint16_t **)&dst,
							    cmap[bmap[1]], cnt);
				}
				x += runlen;
			}
			bmap += 2;
		}
	}
}

int bmpdecoder(void *bmp_addr, void *pdst, int *width, int *height, int *bits)
{
	BITMAPHEADER header;
	BITMAPINFOHEADER infoheader;
	uint32_t size;
	uint16_t linesize;
	char *src = bmp_addr;
	char *dst = pdst;
	int i;
	bool flip = false;

	memcpy(&header, src, sizeof(header));
	src += sizeof(header);

	if (header.type != 0x4d42) {
		pr_err("not bmp file type[%x], can't support\n", header.type);
		return -1;
	}
	memcpy(&infoheader, src, sizeof(infoheader));
	*width = infoheader.width;
	*height = infoheader.height;

	if (*height < 0)
		*height = 0 - *height;
	else
		flip = true;

	size = header.size - header.offset;
	linesize = *width * infoheader.bitcount >> 3;
	src = bmp_addr + header.offset;

	switch (infoheader.bitcount) {
	case 8:
		/*
		 * only support convert 8bit bmap file to RGB565.
		 */
		decode_rle8_bitmap(src, dst, bmp_logo_palette,
				   infoheader.width, infoheader.height,
				   infoheader.bitcount,	0, 0, flip);
		*bits = 16;
		break;
	case 16:
		/*
		 * Todo
		 */
		pr_info("unsupport bit=%d now\n", infoheader.bitcount);
		break;
	case 24:
		if (flip)
			src += (*width) * (*height - 1) * 3;

		for (i = 0; i < *height; i++) {
			memcpy(dst, src, 3 * (*width));
			dst += *width * 3;
			src += *width * 3;
			if (flip)
				src -= *width * 3 * 2;
		}

		*bits = 24;
		break;
	case 32:
		/*
		 * Todo
		 */
		pr_info("unsupport bit=%d now\n", infoheader.bitcount);
		break;
	default:
		pr_info("unsupport bit=%d now\n", infoheader.bitcount);
		break;
	}

	return 0;
}
