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
	uint16_t *bmp_logo_palette;
	uint32_t size;
	uint16_t linesize;
	int stride;
	char *cmap_base;
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
	cmap_base = src + sizeof(infoheader);
	src = bmp_addr + header.offset;

	switch (infoheader.bitcount) {
	case 8:
		bmp_logo_palette = kmalloc(sizeof(bmp_logo_palette) * 256, GFP_KERNEL);

		/* Set color map */
		for (i = 0; i < 256; i++) {
			ushort colreg = ((cmap_base[2] << 8) & 0xf800) |
					((cmap_base[1] << 3) & 0x07e0) |
					((cmap_base[0] >> 3) & 0x001f) ;
			cmap_base += 4;
			bmp_logo_palette[i] = colreg;
		}

		/*
		 * only support convert 8bit bmap file to RGB565.
		 */
		decode_rle8_bitmap(src, dst, bmp_logo_palette,
				   infoheader.width, infoheader.height,
				   infoheader.bitcount,	0, 0, flip);
		kfree(bmp_logo_palette);
		*bits = 16;
		break;
	case 16:
		/*
		 * Todo
		 */
		pr_info("unsupport bit=%d now\n", infoheader.bitcount);
		break;
	case 24:
		stride = ALIGN(*width * 3, 4);
		if (flip)
			src += stride * (*height - 1);

		for (i = 0; i < *height; i++) {
			memcpy(dst, src, 3 * (*width));
			dst += stride;
			src += stride;
			if (flip)
				src -= stride * 2;
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
