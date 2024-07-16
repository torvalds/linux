/*
 * Copyright 2018 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "head.h"
#include "atom.h"
#include "core.h"

#include <nvif/pushc37b.h>

#include <nvhw/class/clc57d.h>

static int
headc57d_or(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u8 depth;
	int ret;

	/*XXX: This is a dirty hack until OR depth handling is
	 *     improved later for deep colour etc.
	 */
	switch (asyh->or.depth) {
	case 6: depth = 5; break;
	case 5: depth = 4; break;
	case 2: depth = 1; break;
	case 0:	depth = 4; break;
	default:
		depth = asyh->or.depth;
		WARN_ON(1);
		break;
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE(i),
		  NVVAL(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, CRC_MODE, asyh->or.crc_raster) |
		  NVVAL(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, HSYNC_POLARITY, asyh->or.nhsync) |
		  NVVAL(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, VSYNC_POLARITY, asyh->or.nvsync) |
		  NVVAL(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, PIXEL_DEPTH, depth) |
		  NVDEF(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, COLOR_SPACE_OVERRIDE, DISABLE) |
		  NVDEF(NVC57D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, EXT_PACKET_WIN, NONE));
	return 0;
}

static int
headc57d_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	//TODO:
	PUSH_MTHD(push, NVC57D, HEAD_SET_PROCAMP(i),
		  NVDEF(NVC57D, HEAD_SET_PROCAMP, COLOR_SPACE, RGB) |
		  NVDEF(NVC57D, HEAD_SET_PROCAMP, CHROMA_LPF, DISABLE) |
		  NVDEF(NVC57D, HEAD_SET_PROCAMP, DYNAMIC_RANGE, VESA));
	return 0;
}

static int
headc57d_olut_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NVC57D, HEAD_SET_CONTEXT_DMA_OLUT(i), 0x00000000);
	return 0;
}

static int
headc57d_olut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 5)))
		return ret;

	PUSH_MTHD(push, NVC57D, HEAD_SET_OLUT_CONTROL(i),
		  NVVAL(NVC57D, HEAD_SET_OLUT_CONTROL, INTERPOLATE, asyh->olut.output_mode) |
		  NVDEF(NVC57D, HEAD_SET_OLUT_CONTROL, MIRROR, DISABLE) |
		  NVVAL(NVC57D, HEAD_SET_OLUT_CONTROL, MODE, asyh->olut.mode) |
		  NVVAL(NVC57D, HEAD_SET_OLUT_CONTROL, SIZE, asyh->olut.size),

				HEAD_SET_OLUT_FP_NORM_SCALE(i), 0xffffffff,
				HEAD_SET_CONTEXT_DMA_OLUT(i), asyh->olut.handle,
				HEAD_SET_OFFSET_OLUT(i), asyh->olut.offset >> 8);
	return 0;
}

static void
headc57d_olut_load_8(struct drm_color_lut *in, int size, void __iomem *mem)
{
	memset_io(mem, 0x00, 0x20); /* VSS header. */
	mem += 0x20;

	while (size--) {
		u16 r = drm_color_lut_extract(in->  red + 0, 16);
		u16 g = drm_color_lut_extract(in->green + 0, 16);
		u16 b = drm_color_lut_extract(in-> blue + 0, 16);
		u16 ri = 0, gi = 0, bi = 0, i;

		if (in++, size) {
			ri = (drm_color_lut_extract(in->  red, 16) - r) / 4;
			gi = (drm_color_lut_extract(in->green, 16) - g) / 4;
			bi = (drm_color_lut_extract(in-> blue, 16) - b) / 4;
		}

		for (i = 0; i < 4; i++, mem += 8) {
			writew(r + ri * i, mem + 0);
			writew(g + gi * i, mem + 2);
			writew(b + bi * i, mem + 4);
		}
	}

	/* INTERPOLATE modes require a "next" entry to interpolate with,
	 * so we replicate the last entry to deal with this for now.
	 */
	writew(readw(mem - 8), mem + 0);
	writew(readw(mem - 6), mem + 2);
	writew(readw(mem - 4), mem + 4);
}

static void
headc57d_olut_load(struct drm_color_lut *in, int size, void __iomem *mem)
{
	memset_io(mem, 0x00, 0x20); /* VSS header. */
	mem += 0x20;

	for (; size--; in++, mem += 0x08) {
		writew(drm_color_lut_extract(in->  red, 16), mem + 0);
		writew(drm_color_lut_extract(in->green, 16), mem + 2);
		writew(drm_color_lut_extract(in-> blue, 16), mem + 4);
	}

	/* INTERPOLATE modes require a "next" entry to interpolate with,
	 * so we replicate the last entry to deal with this for now.
	 */
	writew(readw(mem - 8), mem + 0);
	writew(readw(mem - 6), mem + 2);
	writew(readw(mem - 4), mem + 4);
}

static bool
headc57d_olut(struct nv50_head *head, struct nv50_head_atom *asyh, int size)
{
	if (size != 0 && size != 256 && size != 1024)
		return false;

	asyh->olut.mode = NVC57D_HEAD_SET_OLUT_CONTROL_MODE_DIRECT10;
	asyh->olut.size = 4 /* VSS header. */ + 1024 + 1 /* Entries. */;
	asyh->olut.output_mode = NVC57D_HEAD_SET_OLUT_CONTROL_INTERPOLATE_ENABLE;
	if (size == 256)
		asyh->olut.load = headc57d_olut_load_8;
	else
		asyh->olut.load = headc57d_olut_load;
	return true;
}

static int
headc57d_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	struct nv50_head_mode *m = &asyh->mode;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 15)))
		return ret;

	PUSH_MTHD(push, NVC57D, HEAD_SET_RASTER_SIZE(i),
		  NVVAL(NVC57D, HEAD_SET_RASTER_SIZE, WIDTH, m->h.active) |
		  NVVAL(NVC57D, HEAD_SET_RASTER_SIZE, HEIGHT, m->v.active),

				HEAD_SET_RASTER_SYNC_END(i),
		  NVVAL(NVC57D, HEAD_SET_RASTER_SYNC_END, X, m->h.synce) |
		  NVVAL(NVC57D, HEAD_SET_RASTER_SYNC_END, Y, m->v.synce),

				HEAD_SET_RASTER_BLANK_END(i),
		  NVVAL(NVC57D, HEAD_SET_RASTER_BLANK_END, X, m->h.blanke) |
		  NVVAL(NVC57D, HEAD_SET_RASTER_BLANK_END, Y, m->v.blanke),

				HEAD_SET_RASTER_BLANK_START(i),
		  NVVAL(NVC57D, HEAD_SET_RASTER_BLANK_START, X, m->h.blanks) |
		  NVVAL(NVC57D, HEAD_SET_RASTER_BLANK_START, Y, m->v.blanks));

	//XXX:
	PUSH_NVSQ(push, NVC57D, 0x2074 + (i * 0x400), m->v.blank2e << 16 | m->v.blank2s);
	PUSH_NVSQ(push, NVC57D, 0x2008 + (i * 0x400), m->interlace);

	PUSH_MTHD(push, NVC57D, HEAD_SET_PIXEL_CLOCK_FREQUENCY(i),
		  NVVAL(NVC57D, HEAD_SET_PIXEL_CLOCK_FREQUENCY, HERTZ, m->clock * 1000));

	PUSH_MTHD(push, NVC57D, HEAD_SET_PIXEL_CLOCK_FREQUENCY_MAX(i),
		  NVVAL(NVC57D, HEAD_SET_PIXEL_CLOCK_FREQUENCY_MAX, HERTZ, m->clock * 1000));

	/*XXX: HEAD_USAGE_BOUNDS, doesn't belong here. */
	PUSH_MTHD(push, NVC57D, HEAD_SET_HEAD_USAGE_BOUNDS(i),
		  NVDEF(NVC57D, HEAD_SET_HEAD_USAGE_BOUNDS, CURSOR, USAGE_W256_H256) |
		  NVDEF(NVC57D, HEAD_SET_HEAD_USAGE_BOUNDS, OLUT_ALLOWED, TRUE) |
		  NVDEF(NVC57D, HEAD_SET_HEAD_USAGE_BOUNDS, OUTPUT_SCALER_TAPS, TAPS_2) |
		  NVDEF(NVC57D, HEAD_SET_HEAD_USAGE_BOUNDS, UPSCALING_ALLOWED, TRUE));
	return 0;
}

const struct nv50_head_func
headc57d = {
	.view = headc37d_view,
	.mode = headc57d_mode,
	.olut = headc57d_olut,
	.ilut_check = head907d_ilut_check,
	.olut_identity = true,
	.olut_size = 1024,
	.olut_set = headc57d_olut_set,
	.olut_clr = headc57d_olut_clr,
	.curs_layout = head917d_curs_layout,
	.curs_format = headc37d_curs_format,
	.curs_set = headc37d_curs_set,
	.curs_clr = headc37d_curs_clr,
	.dither = headc37d_dither,
	.procamp = headc57d_procamp,
	.or = headc57d_or,
	/* TODO: flexible window mappings */
	.static_wndw_map = headc37d_static_wndw_map,
};
