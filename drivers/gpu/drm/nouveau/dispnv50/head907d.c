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

	PUSH_NVSQ(push, NV907D, 0x0404 + (i * 0x300), asyh->or.depth  << 6 |
						      asyh->or.nvsync << 4 |
						      asyh->or.nhsync << 3 |
						      asyh->or.crc_raster,
				0x0408 + (i * 0x300), 0x31ec6000 |
						      head->base.index << 25 |
						      asyh->mode.interlace);
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

	PUSH_NVSQ(push, NV907D, 0x0498 + (i * 0x300), asyh->procamp.sat.sin << 20 |
						      asyh->procamp.sat.cos << 8);
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

	PUSH_NVSQ(push, NV907D, 0x0490 + (i * 0x300), asyh->dither.mode << 3 |
						      asyh->dither.bits << 1 |
						      asyh->dither.enable);
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
		case 8: bounds |= 0x00000500; break;
		case 4: bounds |= 0x00000300; break;
		case 2: bounds |= 0x00000100; break;
		default:
			WARN_ON(1);
			break;
		}
		bounds |= 0x00000001;
	} else {
		bounds |= 0x00000100;
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NV907D, 0x04d4 + (i * 0x300), bounds);
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
		case 8: bounds |= 0x00000500; break;
		case 4: bounds |= 0x00000300; break;
		case 2: bounds |= 0x00000100; break;
		case 1: bounds |= 0x00000000; break;
		default:
			WARN_ON(1);
			break;
		}
		bounds |= 0x00000001;
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NV907D, 0x04d0 + (i * 0x300), bounds);
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

	PUSH_NVSQ(push, NV907D, 0x0480 + (i * 0x300), 0x05000000);
	PUSH_NVSQ(push, NV907D, 0x048c + (i * 0x300), 0x00000000);
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

	PUSH_NVSQ(push, NV907D, 0x0480 + (i * 0x300), 0x80000000 |
						      asyh->curs.layout << 26 |
						      asyh->curs.format << 24,
				0x0484 + (i * 0x300), asyh->curs.offset >> 8);
	PUSH_NVSQ(push, NV907D, 0x048c + (i * 0x300), asyh->curs.handle);
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

	PUSH_NVSQ(push, NV907D, 0x0474 + (i * 0x300), 0x00000000);
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

	PUSH_NVSQ(push, NV907D, 0x0460 + (i * 0x300), asyh->core.offset >> 8);
	PUSH_NVSQ(push, NV907D, 0x0468 + (i * 0x300), asyh->core.h << 16 | asyh->core.w,
				0x046c + (i * 0x300), asyh->core.layout << 24 |
						     (asyh->core.pitch >> 8) << 8 |
						      asyh->core.blocks << 8 |
						      asyh->core.blockh,
				0x0470 + (i * 0x300), asyh->core.format << 8,
				0x0474 + (i * 0x300), asyh->core.handle);
	PUSH_NVSQ(push, NV907D, 0x04b0 + (i * 0x300), asyh->core.y << 16 | asyh->core.x);
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

	PUSH_NVSQ(push, NV907D, 0x0448 + (i * 0x300), 0x00000000);
	PUSH_NVSQ(push, NV907D, 0x045c + (i * 0x300), 0x00000000);
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

	PUSH_NVSQ(push, NV907D, 0x0448 + (i * 0x300), 0x80000000 | asyh->olut.mode << 24,
				0x044c + (i * 0x300), asyh->olut.offset >> 8);
	PUSH_NVSQ(push, NV907D, 0x045c + (i * 0x300), asyh->olut.handle);
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

	asyh->olut.mode = size == 1024 ? 4 : 7;
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

	if ((ret = PUSH_WAIT(push, 14)))
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
		  NVVAL(NV907D, HEAD_SET_DEFAULT_BASE_COLOR, BLUE, 0),

				HEAD_SET_CRC_CONTROL(i),
		  NVDEF(NV907D, HEAD_SET_CRC_CONTROL, CONTROLLING_CHANNEL, CORE) |
		  NVDEF(NV907D, HEAD_SET_CRC_CONTROL, EXPECT_BUFFER_COLLAPSE, FALSE) |
		  NVDEF(NV907D, HEAD_SET_CRC_CONTROL, TIMESTAMP_MODE, FALSE) |
		  NVDEF(NV907D, HEAD_SET_CRC_CONTROL, PRIMARY_OUTPUT, NONE) |
		  NVDEF(NV907D, HEAD_SET_CRC_CONTROL, SECONDARY_OUTPUT, NONE));

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
