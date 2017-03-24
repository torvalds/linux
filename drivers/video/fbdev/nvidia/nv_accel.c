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
#include <linux/nmi.h>

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

static inline void nvidiafb_safe_mode(struct fb_info *info)
{
	struct nvidia_par *par = info->par;

	touch_softlockup_watchdog();
	info->pixmap.scan_align = 1;
	par->lockup = 1;
}

static inline void NVFlush(struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	int count = 1000000000;

	while (--count && READ_GET(par) != par->dmaPut) ;

	if (!count) {
		printk("nvidiafb: DMA Flush lockup\n");
		nvidiafb_safe_mode(info);
	}
}

static inline void NVSync(struct fb_info *info)
{
	struct nvidia_par *par = info->par;
	int count = 1000000000;

	while (--count && NV_RD32(par->PGRAPH, 0x0700)) ;

	if (!count) {
		printk("nvidiafb: DMA Sync lockup\n");
		nvidiafb_safe_mode(info);
	}
}

static void NVDmaKickoff(struct nvidia_par *par)
{
	if (par->dmaCurrent != par->dmaPut) {
		par->dmaPut = par->dmaCurrent;
		WRITE_PUT(par, par->dmaPut);
	}
}

static void NVDmaWait(struct fb_info *info, int size)
{
	struct nvidia_par *par = info->par;
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
		printk("nvidiafb: DMA Wait Lockup\n");
		nvidiafb_safe_mode(info);
	}
}

static void NVSetPattern(struct fb_info *info, u32 clr0, u32 clr1,
			 u32 pat0, u32 pat1)
{
	struct nvidia_par *par = info->par;

	NVDmaStart(info, par, PATTERN_COLOR_0, 4);
	NVDmaNext(par, clr0);
	NVDmaNext(par, clr1);
	NVDmaNext(par, pat0);
	NVDmaNext(par, pat1);
}

static void NVSetRopSolid(struct fb_info *info, u32 rop, u32 planemask)
{
	struct nvidia_par *par = info->par;

	if (planemask != ~0) {
		NVSetPattern(info, 0, planemask, ~0, ~0);
		if (par->currentRop != (rop + 32)) {
			NVDmaStart(info, par, ROP_SET, 1);
			NVDmaNext(par, NVCopyROP_PM[rop]);
			par->currentRop = rop + 32;
		}
	} else if (par->currentRop != rop) {
		if (par->currentRop >= 16)
			NVSetPattern(info, ~0, ~0, ~0, ~0);
		NVDmaStart(info, par, ROP_SET, 1);
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

	NVDmaStart(info, par, CLIP_POINT, 2);
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

	NVDmaStart(info, par, SURFACE_FORMAT, 4);
	NVDmaNext(par, surfaceFormat);
	NVDmaNext(par, pitch | (pitch << 16));
	NVDmaNext(par, 0);
	NVDmaNext(par, 0);

	NVDmaStart(info, par, PATTERN_FORMAT, 1);
	NVDmaNext(par, patternFormat);

	NVDmaStart(info, par, RECT_FORMAT, 1);
	NVDmaNext(par, rectFormat);

	NVDmaStart(info, par, LINE_FORMAT, 1);
	NVDmaNext(par, lineFormat);

	par->currentRop = ~0;	/* set to something invalid */
	NVSetRopSolid(info, ROP_COPY, ~0);

	NVSetClippingRectangle(info, 0, 0, info->var.xres_virtual,
			       info->var.yres_virtual);

	NVDmaKickoff(par);
}

int nvidiafb_sync(struct fb_info *info)
{
	struct nvidia_par *par = info->par;

	if (info->state != FBINFO_STATE_RUNNING)
		return 0;

	if (!par->lockup)
		NVFlush(info);

	if (!par->lockup)
		NVSync(info);

	return 0;
}

void nvidiafb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct nvidia_par *par = info->par;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (par->lockup) {
		cfb_copyarea(info, region);
		return;
	}

	NVDmaStart(info, par, BLIT_POINT_SRC, 3);
	NVDmaNext(par, (region->sy << 16) | region->sx);
	NVDmaNext(par, (region->dy << 16) | region->dx);
	NVDmaNext(par, (region->height << 16) | region->width);

	NVDmaKickoff(par);
}

void nvidiafb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct nvidia_par *par = info->par;
	u32 color;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (par->lockup) {
		cfb_fillrect(info, rect);
		return;
	}

	if (info->var.bits_per_pixel == 8)
		color = rect->color;
	else
		color = ((u32 *) info->pseudo_palette)[rect->color];

	if (rect->rop != ROP_COPY)
		NVSetRopSolid(info, rect->rop, ~0);

	NVDmaStart(info, par, RECT_SOLID_COLOR, 1);
	NVDmaNext(par, color);

	NVDmaStart(info, par, RECT_SOLID_RECTS(0), 2);
	NVDmaNext(par, (rect->dx << 16) | rect->dy);
	NVDmaNext(par, (rect->width << 16) | rect->height);

	NVDmaKickoff(par);

	if (rect->rop != ROP_COPY)
		NVSetRopSolid(info, ROP_COPY, ~0);
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

	NVDmaStart(info, par, RECT_EXPAND_TWO_COLOR_CLIP, 7);
	NVDmaNext(par, (image->dy << 16) | (image->dx & 0xffff));
	NVDmaNext(par, ((image->dy + image->height) << 16) |
		  ((image->dx + image->width) & 0xffff));
	NVDmaNext(par, bg);
	NVDmaNext(par, fg);
	NVDmaNext(par, (image->height << 16) | width);
	NVDmaNext(par, (image->height << 16) | width);
	NVDmaNext(par, (image->dy << 16) | (image->dx & 0xffff));

	while (dsize >= RECT_EXPAND_TWO_COLOR_DATA_MAX_DWORDS) {
		NVDmaStart(info, par, RECT_EXPAND_TWO_COLOR_DATA(0),
			   RECT_EXPAND_TWO_COLOR_DATA_MAX_DWORDS);

		for (j = RECT_EXPAND_TWO_COLOR_DATA_MAX_DWORDS; j--;) {
			tmp = data[k++];
			reverse_order(&tmp);
			NVDmaNext(par, tmp);
		}

		dsize -= RECT_EXPAND_TWO_COLOR_DATA_MAX_DWORDS;
	}

	if (dsize) {
		NVDmaStart(info, par, RECT_EXPAND_TWO_COLOR_DATA(0), dsize);

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

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (image->depth == 1 && !par->lockup)
		nvidiafb_mono_color_expand(info, image);
	else
		cfb_imageblit(info, image);
}
