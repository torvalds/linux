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
#include <drm/drm_connector.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_vblank.h>
#include "nouveau_drv.h"
#include "nouveau_bios.h"
#include "nouveau_connector.h"
#include "head.h"
#include "core.h"
#include "crc.h"

#include <nvif/push507c.h>

#include <nvhw/class/cl907d.h>

int
head907d_or(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 3)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTROL_OUTPUT_RESOURCE(i),
		  NVVAL(NV907D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, CRC_MODE, asyh->or.crc_raster) |
		  NVVAL(NV907D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, HSYNC_POLARITY, asyh->or.nhsync) |
		  NVVAL(NV907D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, VSYNC_POLARITY, asyh->or.nvsync) |
		  NVVAL(NV907D, HEAD_SET_CONTROL_OUTPUT_RESOURCE, PIXEL_DEPTH, asyh->or.depth),

				HEAD_SET_CONTROL(i), 0x31ec6000 | head->base.index << 25 |
		  NVVAL(NV907D, HEAD_SET_CONTROL, STRUCTURE, asyh->mode.interlace));
	return 0;
}

int
head907d_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_PROCAMP(i),
		  NVDEF(NV907D, HEAD_SET_PROCAMP, COLOR_SPACE, RGB) |
		  NVDEF(NV907D, HEAD_SET_PROCAMP, CHROMA_LPF, AUTO) |
		  NVVAL(NV907D, HEAD_SET_PROCAMP, SAT_COS, asyh->procamp.sat.cos) |
		  NVVAL(NV907D, HEAD_SET_PROCAMP, SAT_SINE, asyh->procamp.sat.sin) |
		  NVDEF(NV907D, HEAD_SET_PROCAMP, DYNAMIC_RANGE, VESA) |
		  NVDEF(NV907D, HEAD_SET_PROCAMP, RANGE_COMPRESSION, DISABLE));
	return 0;
}

static int
head907d_dither(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_DITHER_CONTROL(i),
		  NVVAL(NV907D, HEAD_SET_DITHER_CONTROL, ENABLE, asyh->dither.enable) |
		  NVVAL(NV907D, HEAD_SET_DITHER_CONTROL, BITS, asyh->dither.bits) |
		  NVVAL(NV907D, HEAD_SET_DITHER_CONTROL, MODE, asyh->dither.mode) |
		  NVVAL(NV907D, HEAD_SET_DITHER_CONTROL, PHASE, 0));
	return 0;
}

int
head907d_ovly(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u32 bounds = 0;
	int ret;

	if (asyh->ovly.cpp) {
		switch (asyh->ovly.cpp) {
		case 8: bounds |= NVDEF(NV907D, HEAD_SET_OVERLAY_USAGE_BOUNDS, PIXEL_DEPTH, BPP_64); break;
		case 4: bounds |= NVDEF(NV907D, HEAD_SET_OVERLAY_USAGE_BOUNDS, PIXEL_DEPTH, BPP_32); break;
		case 2: bounds |= NVDEF(NV907D, HEAD_SET_OVERLAY_USAGE_BOUNDS, PIXEL_DEPTH, BPP_16); break;
		default:
			WARN_ON(1);
			break;
		}
		bounds |= NVDEF(NV907D, HEAD_SET_OVERLAY_USAGE_BOUNDS, USABLE, TRUE);
	} else {
		bounds |= NVDEF(NV907D, HEAD_SET_OVERLAY_USAGE_BOUNDS, PIXEL_DEPTH, BPP_16);
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_OVERLAY_USAGE_BOUNDS(i), bounds);
	return 0;
}

static int
head907d_base(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u32 bounds = 0;
	int ret;

	if (asyh->base.cpp) {
		switch (asyh->base.cpp) {
		case 8: bounds |= NVDEF(NV907D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, PIXEL_DEPTH, BPP_64); break;
		case 4: bounds |= NVDEF(NV907D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, PIXEL_DEPTH, BPP_32); break;
		case 2: bounds |= NVDEF(NV907D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, PIXEL_DEPTH, BPP_16); break;
		case 1: bounds |= NVDEF(NV907D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, PIXEL_DEPTH, BPP_8); break;
		default:
			WARN_ON(1);
			break;
		}
		bounds |= NVDEF(NV907D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, USABLE, TRUE);
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS(i), bounds);
	return 0;
}

int
head907d_curs_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTROL_CURSOR(i),
		  NVDEF(NV907D, HEAD_SET_CONTROL_CURSOR, ENABLE, DISABLE) |
		  NVDEF(NV907D, HEAD_SET_CONTROL_CURSOR, FORMAT, A8R8G8B8) |
		  NVDEF(NV907D, HEAD_SET_CONTROL_CURSOR, SIZE, W64_H64));

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTEXT_DMA_CURSOR(i), 0x00000000);
	return 0;
}

int
head907d_curs_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 5)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTROL_CURSOR(i),
		  NVDEF(NV907D, HEAD_SET_CONTROL_CURSOR, ENABLE, ENABLE) |
		  NVVAL(NV907D, HEAD_SET_CONTROL_CURSOR, FORMAT, asyh->curs.format) |
		  NVVAL(NV907D, HEAD_SET_CONTROL_CURSOR, SIZE, asyh->curs.layout) |
		  NVVAL(NV907D, HEAD_SET_CONTROL_CURSOR, HOT_SPOT_X, 0) |
		  NVVAL(NV907D, HEAD_SET_CONTROL_CURSOR, HOT_SPOT_Y, 0) |
		  NVDEF(NV907D, HEAD_SET_CONTROL_CURSOR, COMPOSITION, ALPHA_BLEND),

				HEAD_SET_OFFSET_CURSOR(i), asyh->curs.offset >> 8);

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTEXT_DMA_CURSOR(i), asyh->curs.handle);
	return 0;
}

int
head907d_core_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTEXT_DMAS_ISO(i), 0x00000000);
	return 0;
}

int
head907d_core_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 9)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_OFFSET(i),
		  NVVAL(NV907D, HEAD_SET_OFFSET, ORIGIN, asyh->core.offset >> 8));

	PUSH_MTHD(push, NV907D, HEAD_SET_SIZE(i),
		  NVVAL(NV907D, HEAD_SET_SIZE, WIDTH, asyh->core.w) |
		  NVVAL(NV907D, HEAD_SET_SIZE, HEIGHT, asyh->core.h),

				HEAD_SET_STORAGE(i),
		  NVVAL(NV907D, HEAD_SET_STORAGE, BLOCK_HEIGHT, asyh->core.blockh) |
		  NVVAL(NV907D, HEAD_SET_STORAGE, PITCH, asyh->core.pitch >> 8) |
		  NVVAL(NV907D, HEAD_SET_STORAGE, PITCH, asyh->core.blocks) |
		  NVVAL(NV907D, HEAD_SET_STORAGE, MEMORY_LAYOUT, asyh->core.layout),

				HEAD_SET_PARAMS(i),
		  NVVAL(NV907D, HEAD_SET_PARAMS, FORMAT, asyh->core.format) |
		  NVDEF(NV907D, HEAD_SET_PARAMS, SUPER_SAMPLE, X1_AA) |
		  NVDEF(NV907D, HEAD_SET_PARAMS, GAMMA, LINEAR),

				HEAD_SET_CONTEXT_DMAS_ISO(i),
		  NVVAL(NV907D, HEAD_SET_CONTEXT_DMAS_ISO, HANDLE, asyh->core.handle));

	PUSH_MTHD(push, NV907D, HEAD_SET_VIEWPORT_POINT_IN(i),
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_POINT_IN, X, asyh->core.x) |
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_POINT_IN, Y, asyh->core.y));
	return 0;
}

int
head907d_olut_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_OUTPUT_LUT_LO(i),
		  NVDEF(NV907D, HEAD_SET_OUTPUT_LUT_LO, ENABLE, DISABLE));

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTEXT_DMA_LUT(i), 0x00000000);
	return 0;
}

int
head907d_olut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 5)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_OUTPUT_LUT_LO(i),
		  NVDEF(NV907D, HEAD_SET_OUTPUT_LUT_LO, ENABLE, ENABLE) |
		  NVVAL(NV907D, HEAD_SET_OUTPUT_LUT_LO, MODE, asyh->olut.mode) |
		  NVDEF(NV907D, HEAD_SET_OUTPUT_LUT_LO, NEVER_YIELD_TO_BASE, DISABLE),

				HEAD_SET_OUTPUT_LUT_HI(i),
		  NVVAL(NV907D, HEAD_SET_OUTPUT_LUT_HI, ORIGIN, asyh->olut.offset >> 8));

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTEXT_DMA_LUT(i), asyh->olut.handle);
	return 0;
}

void
head907d_olut_load(struct drm_color_lut *in, int size, void __iomem *mem)
{
	for (; size--; in++, mem += 8) {
		writew(drm_color_lut_extract(in->  red, 14) + 0x6000, mem + 0);
		writew(drm_color_lut_extract(in->green, 14) + 0x6000, mem + 2);
		writew(drm_color_lut_extract(in-> blue, 14) + 0x6000, mem + 4);
	}

	/* INTERPOLATE modes require a "next" entry to interpolate with,
	 * so we replicate the last entry to deal with this for now.
	 */
	writew(readw(mem - 8), mem + 0);
	writew(readw(mem - 6), mem + 2);
	writew(readw(mem - 4), mem + 4);
}

bool
head907d_olut(struct nv50_head *head, struct nv50_head_atom *asyh, int size)
{
	if (size != 256 && size != 1024)
		return false;

	if (size == 1024)
		asyh->olut.mode = NV907D_HEAD_SET_OUTPUT_LUT_LO_MODE_INTERPOLATE_1025_UNITY_RANGE;
	else
		asyh->olut.mode = NV907D_HEAD_SET_OUTPUT_LUT_LO_MODE_INTERPOLATE_257_UNITY_RANGE;

	asyh->olut.load = head907d_olut_load;
	return true;
}

int
head907d_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	struct nv50_head_mode *m = &asyh->mode;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 13)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_OVERSCAN_COLOR(i),
		  NVVAL(NV907D, HEAD_SET_OVERSCAN_COLOR, RED, 0) |
		  NVVAL(NV907D, HEAD_SET_OVERSCAN_COLOR, GRN, 0) |
		  NVVAL(NV907D, HEAD_SET_OVERSCAN_COLOR, BLU, 0),

				HEAD_SET_RASTER_SIZE(i),
		  NVVAL(NV907D, HEAD_SET_RASTER_SIZE, WIDTH, m->h.active) |
		  NVVAL(NV907D, HEAD_SET_RASTER_SIZE, HEIGHT, m->v.active),

				HEAD_SET_RASTER_SYNC_END(i),
		  NVVAL(NV907D, HEAD_SET_RASTER_SYNC_END, X, m->h.synce) |
		  NVVAL(NV907D, HEAD_SET_RASTER_SYNC_END, Y, m->v.synce),

				HEAD_SET_RASTER_BLANK_END(i),
		  NVVAL(NV907D, HEAD_SET_RASTER_BLANK_END, X, m->h.blanke) |
		  NVVAL(NV907D, HEAD_SET_RASTER_BLANK_END, Y, m->v.blanke),

				HEAD_SET_RASTER_BLANK_START(i),
		  NVVAL(NV907D, HEAD_SET_RASTER_BLANK_START, X, m->h.blanks) |
		  NVVAL(NV907D, HEAD_SET_RASTER_BLANK_START, Y, m->v.blanks),

				HEAD_SET_RASTER_VERT_BLANK2(i),
		  NVVAL(NV907D, HEAD_SET_RASTER_VERT_BLANK2, YSTART, m->v.blank2s) |
		  NVVAL(NV907D, HEAD_SET_RASTER_VERT_BLANK2, YEND, m->v.blank2e));

	PUSH_MTHD(push, NV907D, HEAD_SET_DEFAULT_BASE_COLOR(i),
		  NVVAL(NV907D, HEAD_SET_DEFAULT_BASE_COLOR, RED, 0) |
		  NVVAL(NV907D, HEAD_SET_DEFAULT_BASE_COLOR, GREEN, 0) |
		  NVVAL(NV907D, HEAD_SET_DEFAULT_BASE_COLOR, BLUE, 0));

	PUSH_MTHD(push, NV907D, HEAD_SET_PIXEL_CLOCK_FREQUENCY(i),
		  NVVAL(NV907D, HEAD_SET_PIXEL_CLOCK_FREQUENCY, HERTZ, m->clock * 1000) |
		  NVDEF(NV907D, HEAD_SET_PIXEL_CLOCK_FREQUENCY, ADJ1000DIV1001, FALSE),

				HEAD_SET_PIXEL_CLOCK_CONFIGURATION(i),
		  NVDEF(NV907D, HEAD_SET_PIXEL_CLOCK_CONFIGURATION, MODE, CLK_CUSTOM) |
		  NVDEF(NV907D, HEAD_SET_PIXEL_CLOCK_CONFIGURATION, NOT_DRIVER, FALSE) |
		  NVDEF(NV907D, HEAD_SET_PIXEL_CLOCK_CONFIGURATION, ENABLE_HOPPING, FALSE),

				HEAD_SET_PIXEL_CLOCK_FREQUENCY_MAX(i),
		  NVVAL(NV907D, HEAD_SET_PIXEL_CLOCK_FREQUENCY_MAX, HERTZ, m->clock * 1000) |
		  NVDEF(NV907D, HEAD_SET_PIXEL_CLOCK_FREQUENCY_MAX, ADJ1000DIV1001, FALSE));
	return 0;
}

int
head907d_view(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 8)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTROL_OUTPUT_SCALER(i),
		  NVDEF(NV907D, HEAD_SET_CONTROL_OUTPUT_SCALER, VERTICAL_TAPS, TAPS_1) |
		  NVDEF(NV907D, HEAD_SET_CONTROL_OUTPUT_SCALER, HORIZONTAL_TAPS, TAPS_1) |
		  NVVAL(NV907D, HEAD_SET_CONTROL_OUTPUT_SCALER, HRESPONSE_BIAS, 0) |
		  NVVAL(NV907D, HEAD_SET_CONTROL_OUTPUT_SCALER, VRESPONSE_BIAS, 0));

	PUSH_MTHD(push, NV907D, HEAD_SET_VIEWPORT_SIZE_IN(i),
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_SIZE_IN, WIDTH, asyh->view.iW) |
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_SIZE_IN, HEIGHT, asyh->view.iH));

	PUSH_MTHD(push, NV907D, HEAD_SET_VIEWPORT_SIZE_OUT(i),
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_SIZE_OUT, WIDTH, asyh->view.oW) |
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_SIZE_OUT, HEIGHT, asyh->view.oH),

				HEAD_SET_VIEWPORT_SIZE_OUT_MIN(i),
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_SIZE_OUT_MIN, WIDTH, asyh->view.oW) |
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_SIZE_OUT_MIN, HEIGHT, asyh->view.oH),

				HEAD_SET_VIEWPORT_SIZE_OUT_MAX(i),
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_SIZE_OUT_MAX, WIDTH, asyh->view.oW) |
		  NVVAL(NV907D, HEAD_SET_VIEWPORT_SIZE_OUT_MAX, HEIGHT, asyh->view.oH));
	return 0;
}

const struct nv50_head_func
head907d = {
	.view = head907d_view,
	.mode = head907d_mode,
	.olut = head907d_olut,
	.olut_size = 1024,
	.olut_set = head907d_olut_set,
	.olut_clr = head907d_olut_clr,
	.core_calc = head507d_core_calc,
	.core_set = head907d_core_set,
	.core_clr = head907d_core_clr,
	.curs_layout = head507d_curs_layout,
	.curs_format = head507d_curs_format,
	.curs_set = head907d_curs_set,
	.curs_clr = head907d_curs_clr,
	.base = head907d_base,
	.ovly = head907d_ovly,
	.dither = head907d_dither,
	.procamp = head907d_procamp,
	.or = head907d_or,
};
