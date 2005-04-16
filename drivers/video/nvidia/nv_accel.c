 /***************************************************************************\
|*                                                                           *|
|*       Copyright 1993-2003 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 1993-2003 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/

/*
 * GPL Licensing Note - According to Mark Vojkovich, author of the Xorg/
 * XFree86 'nv' driver, this source code is provided under MIT-style licensing
 * where the source code is provided "as is" without warranty of any kind.
 * The only usage restriction is for the copyright notices to be retained
 * whenever code is used.
 *
 * Antonino Daplas <adaplas@pol.net> 2005-03-11
 */

#include <linux/fb.h>
#include "nv_type.h"
#include "nv_proto.h"
#include "nv_dma.h"
#include "nv_local.h"

/* There is a HW race condition with videoram command buffers.
   You can't jump to the location of your put offset.  We write put
   at the jump offset + SKIPS dwords with noop padding in between
   to solve this problem */
#define SKIPS  8

static const int NVCopyROP[16] = {
	0xCC,			/* copy   */
	0x55			/* invert */
};

static const int NVCopyROP_PM[16] = {
	0xCA,			/* copy  */
	0x5A,			/* invert */
};

static inline void NVFlush(struct nvidia_par *par)
{
	int count = 1000000000;

	while (--count && READ_GET(par) != par->dmaPut) ;

	if (!count) {
		printk("nvidiafb: DMA Flush lockup\n");
		par->lockup = 1;
	}
}

static inline void NVSync(struct nvidia_par *par)
{
	int count = 1000000000;

	while (--count && NV_RD32(par->PGRAPH, 0x0700)) ;

	if (!count) {
		printk("nvidiafb: DMA Sync lockup\n");
		par->lockup = 1;
	}
}

static void NVDmaKickoff(struct nvidia_par *par)
{
	if (par->dmaCurrent != par->dmaPut) {
		par->dmaPut = par->dmaCurrent;
		WRITE_PUT(par, par->dmaPut);
	}
}

static void NVDmaWait(struct nvidia_par *par, int size)
{
	int dmaGet;
	int count = 1000000000, cnt;
	size++;

	while (par->dmaFree < size && --count && !par->lockup) {
		dmaGet = READ_GET(par);

		if (par->dmaPut >= dmaGet) {
			par->dmaFree = par->dmaMax - par->dmaCurrent;
			if (par->dmaFree < size) {
				NVDmaNext(par, 0x20000000);
				if (dmaGet <= SKIPS) {
					if (par->dmaPut <= SKIPS)
						WRITE_PUT(par, SKIPS + 1);
					cnt = 1000000000;
					do {
						dmaGet = READ_GET(par);
					} while (--cnt && dmaGet <= SKIPS);
					if (!cnt) {
						printk("DMA Get lockup\n");
						par->lockup = 1;
					}
				}
				WRITE_PUT(par, SKIPS);
				par->dmaCurrent = par->dmaPut = SKIPS;
				par->dmaFree = dmaGet - (SKIPS + 1);
			}
		} else
			par->dmaFree = dmaGet - par->dmaCurrent - 1;
	}

	if (!count) {
		printk("DMA Wait Lockup\n");
		par->lockup = 1;
	}
}

static void NVSetPattern(struct nvidia_par *par, u32 clr0, u32 clr1,
			 u32 pat0, u32 pat1)
{
	NVDmaStart(par, PATTERN_COLOR_0, 4);
	NVDmaNext(par, clr0);
	NVDmaNext(par, clr1);
	NVDmaNext(par, pat0);
	NVDmaNext(par, pat1);
}

static void NVSetRopSolid(struct nvidia_par *par, u32 rop, u32 planemask)
{
	if (planemask != ~0) {
		NVSetPattern(par, 0, planemask, ~0, ~0);
		if (par->currentRop != (rop + 32)) {
			NVDmaStart(par, ROP_SET, 1);
			NVDmaNext(par, NVCopyROP_PM[rop]);
			par->currentRop = rop + 32;
		}
	} else if (par->currentRop != rop) {
		if (par->currentRop >= 16)
			NVSetPattern(par, ~0, ~0, ~0, ~0);
		NVDmaStart(par, ROP_SET, 1);
		NVDmaNext(par, NVCopyROP[rop]);
		par->currentRop = rop;
	}
}

static void NVSetClippingRectangle(struct fb_info *info, int x1, int y1,
				   int x2, int y2)
{
	struct nvidia_par *par = info->par;
	int h = y2 - y1 + 1;
	int w = x2 - x1 + 1;

	NVDmaStart(par, CLIP_POINT, 2);
	NVDmaNext(par, (y1 << 16) | x1);
	NVDmaNext(par, (h << 16) | w);
}

void NVResetGraphics(struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	u32 surfaceFormat, patternFormat, rectFormat, lineFormat;
	int pitch, i;

	pitch = info->fix.line_length;

	par->dmaBase = (u32 __iomem *) (&par->FbStart[par->FbUsableSize]);

	for (i = 0; i < SKIPS; i++)
		NV_WR32(&par->dmaBase[i], 0, 0x00000000);

	NV_WR32(&par->dmaBase[0x0 + SKIPS], 0, 0x00040000);
	NV_WR32(&par->dmaBase[0x1 + SKIPS], 0, 0x80000010);
	NV_WR32(&par->dmaBase[0x2 + SKIPS], 0, 0x00042000);
	NV_WR32(&par->dmaBase[0x3 + SKIPS], 0, 0x80000011);
	NV_WR32(&par->dmaBase[0x4 + SKIPS], 0, 0x00044000);
	NV_WR32(&par->dmaBase[0x5 + SKIPS], 0, 0x80000012);
	NV_WR32(&par->dmaBase[0x6 + SKIPS], 0, 0x00046000);
	NV_WR32(&par->dmaBase[0x7 + SKIPS], 0, 0x80000013);
	NV_WR32(&par->dmaBase[0x8 + SKIPS], 0, 0x00048000);
	NV_WR32(&par->dmaBase[0x9 + SKIPS], 0, 0x80000014);
	NV_WR32(&par->dmaBase[0xA + SKIPS], 0, 0x0004A000);
	NV_WR32(&par->dmaBase[0xB + SKIPS], 0, 0x80000015);
	NV_WR32(&par->dmaBase[0xC + SKIPS], 0, 0x0004C000);
	NV_WR32(&par->dmaBase[0xD + SKIPS], 0, 0x80000016);
	NV_WR32(&par->dmaBase[0xE + SKIPS], 0, 0x0004E000);
	NV_WR32(&par->dmaBase[0xF + SKIPS], 0, 0x80000017);

	par->dmaPut = 0;
	par->dmaCurrent = 16 + SKIPS;
	par->dmaMax = 8191;
	par->dmaFree = par->dmaMax - par->dmaCurrent;

	switch (info->var.bits_per_pixel) {
	case 32:
	case 24:
		surfaceFormat = SURFACE_FORMAT_DEPTH24;
		patternFormat = PATTERN_FORMAT_DEPTH24;
		rectFormat = RECT_FORMAT_DEPTH24;
		lineFormat = LINE_FORMAT_DEPTH24;
		break;
	case 16:
		surfaceFormat = SURFACE_FORMAT_DEPTH16;
		patternFormat = PATTERN_FORMAT_DEPTH16;
		rectFormat = RECT_FORMAT_DEPTH16;
		lineFormat = LINE_FORMAT_DEPTH16;
		break;
	default:
		surfaceFormat = SURFACE_FORMAT_DEPTH8;
		patternFormat = PATTERN_FORMAT_DEPTH8;
		rectFormat = RECT_FORMAT_DEPTH8;
		lineFormat = LINE_FORMAT_DEPTH8;
		break;
	}

	NVDmaStart(par, SURFACE_FORMAT, 4);
	NVDmaNext(par, surfaceFormat);
	NVDmaNext(par, pitch | (pitch << 16));
	NVDmaNext(par, 0);
	NVDmaNext(par, 0);

	NVDmaStart(par, PATTERN_FORMAT, 1);
	NVDmaNext(par, patternFormat);

	NVDmaStart(par, RECT_FORMAT, 1);
	NVDmaNext(par, rectFormat);

	NVDmaStart(par, LINE_FORMAT, 1);
	NVDmaNext(par, lineFormat);

	par->currentRop = ~0;	/* set to something invalid */
	NVSetRopSolid(par, ROP_COPY, ~0);

	NVSetClippingRectangle(info, 0, 0, info->var.xres_virtual,
			       info->var.yres_virtual);

	NVDmaKickoff(par);
}

u8 byte_rev[256] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
	0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
	0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
	0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
	0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
	0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
	0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
	0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
	0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
	0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
	0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
	0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
	0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
	0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
	0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
	0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
	0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
	0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
};

int nvidiafb_sync(struct fb_info *info)
{
	struct nvidia_par *par = info->par;

	if (!par->lockup)
		NVFlush(par);

	if (!par->lockup)
		NVSync(par);

	return 0;
}

void nvidiafb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct nvidia_par *par = info->par;

	if (par->lockup)
		return cfb_copyarea(info, region);

	NVDmaStart(par, BLIT_POINT_SRC, 3);
	NVDmaNext(par, (region->sy << 16) | region->sx);
	NVDmaNext(par, (region->dy << 16) | region->dx);
	NVDmaNext(par, (region->height << 16) | region->width);

	NVDmaKickoff(par);
}

void nvidiafb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nvidia_par *par = info->par;
	u32 color;

	if (par->lockup)
		return cfb_fillrect(info, rect);

	if (info->var.bits_per_pixel == 8)
		color = rect->color;
	else
		color = ((u32 *) info->pseudo_palette)[rect->color];

	if (rect->rop != ROP_COPY)
		NVSetRopSolid(par, rect->rop, ~0);

	NVDmaStart(par, RECT_SOLID_COLOR, 1);
	NVDmaNext(par, color);

	NVDmaStart(par, RECT_SOLID_RECTS(0), 2);
	NVDmaNext(par, (rect->dx << 16) | rect->dy);
	NVDmaNext(par, (rect->width << 16) | rect->height);

	NVDmaKickoff(par);

	if (rect->rop != ROP_COPY)
		NVSetRopSolid(par, ROP_COPY, ~0);
}

static void nvidiafb_mono_color_expand(struct fb_info *info,
				       const struct fb_image *image)
{
	struct nvidia_par *par = info->par;
	u32 fg, bg, mask = ~(~0 >> (32 - info->var.bits_per_pixel));
	u32 dsize, width, *data = (u32 *) image->data, tmp;
	int j, k = 0;

	width = (image->width + 31) & ~31;
	dsize = (width * image->height) >> 5;

	if (info->var.bits_per_pixel == 8) {
		fg = image->fg_color | mask;
		bg = image->bg_color | mask;
	} else {
		fg = ((u32 *) info->pseudo_palette)[image->fg_color] | mask;
		bg = ((u32 *) info->pseudo_palette)[image->bg_color] | mask;
	}

	NVDmaStart(par, RECT_EXPAND_TWO_COLOR_CLIP, 7);
	NVDmaNext(par, (image->dy << 16) | (image->dx & 0xffff));
	NVDmaNext(par, ((image->dy + image->height) << 16) |
		  ((image->dx + image->width) & 0xffff));
	NVDmaNext(par, bg);
	NVDmaNext(par, fg);
	NVDmaNext(par, (image->height << 16) | width);
	NVDmaNext(par, (image->height << 16) | width);
	NVDmaNext(par, (image->dy << 16) | (image->dx & 0xffff));

	while (dsize >= RECT_EXPAND_TWO_COLOR_DATA_MAX_DWORDS) {
		NVDmaStart(par, RECT_EXPAND_TWO_COLOR_DATA(0),
			   RECT_EXPAND_TWO_COLOR_DATA_MAX_DWORDS);

		for (j = RECT_EXPAND_TWO_COLOR_DATA_MAX_DWORDS; j--;) {
			tmp = data[k++];
			reverse_order(&tmp);
			NVDmaNext(par, tmp);
		}

		dsize -= RECT_EXPAND_TWO_COLOR_DATA_MAX_DWORDS;
	}

	if (dsize) {
		NVDmaStart(par, RECT_EXPAND_TWO_COLOR_DATA(0), dsize);

		for (j = dsize; j--;) {
			tmp = data[k++];
			reverse_order(&tmp);
			NVDmaNext(par, tmp);
		}
	}

	NVDmaKickoff(par);
}

void nvidiafb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct nvidia_par *par = info->par;

	if (image->depth == 1 && !par->lockup)
		nvidiafb_mono_color_expand(info, image);
	else
		cfb_imageblit(info, image);
}
