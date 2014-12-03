/*
 * drivers/video/rockchip/bmp_helper.h
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

#ifndef _BMP_HELPER_H_
#define _BMP_HELPER_H_

typedef struct bmpheader {
	unsigned short type;
	unsigned int size;
	unsigned int reserved;
	unsigned int offset;
}__attribute__((packed)) BITMAPHEADER;

typedef struct bmpinfoheader {
	unsigned int size;
	unsigned int width;
	unsigned int height;
	unsigned short planes;
	unsigned short bitcount;
	unsigned int compression;
	unsigned int imagesize;
	unsigned int xpelspermeter;
	unsigned int ypelspermeter;
	unsigned int colors;
	unsigned int colorsimportant;
}__attribute__((packed)) BITMAPINFOHEADER;

#define range(x, min, max) ((x) < (min)) ? (min) : (((x) > (max)) ? (max) : (x))

int datatobmp(void *__iomem *vaddr,int width, int height, u8 data_format,
	      void *data, void (*fn)(void *, void *, int));
#endif /* _BMP_HELPER_H_ */
