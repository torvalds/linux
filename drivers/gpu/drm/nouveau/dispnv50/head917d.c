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
#include "core.h"

#include <nvif/push507c.h>

#include <nvhw/class/cl917d.h>

static int
head917d_dither(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV917D, HEAD_SET_DITHER_CONTROL(i),
		  NVVAL(NV917D, HEAD_SET_DITHER_CONTROL, ENABLE, asyh->dither.enable) |
		  NVVAL(NV917D, HEAD_SET_DITHER_CONTROL, BITS, asyh->dither.bits) |
		  NVVAL(NV917D, HEAD_SET_DITHER_CONTROL, MODE, asyh->dither.mode) |
		  NVVAL(NV917D, HEAD_SET_DITHER_CONTROL, PHASE, 0));
	return 0;
}

static int
head917d_base(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u32 bounds = 0;
	int ret;

	if (asyh->base.cpp) {
		switch (asyh->base.cpp) {
		case 8: bounds |= NVDEF(NV917D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, PIXEL_DEPTH, BPP_64); break;
		case 4: bounds |= NVDEF(NV917D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, PIXEL_DEPTH, BPP_32); break;
		case 2: bounds |= NVDEF(NV917D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, PIXEL_DEPTH, BPP_16); break;
		case 1: bounds |= NVDEF(NV917D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, PIXEL_DEPTH, BPP_8); break;
		default:
			WARN_ON(1);
			break;
		}
		bounds |= NVDEF(NV917D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, USABLE, TRUE);
		bounds |= NVDEF(NV917D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS, BASE_LUT, USAGE_1025);
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV917D, HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS(i), bounds);
	return 0;
}

int
head917d_curs_layout(struct nv50_head *head, struct nv50_wndw_atom *asyw,
		     struct nv50_head_atom *asyh)
{
	switch (asyw->state.fb->width) {
	case  32: asyh->curs.layout = NV917D_HEAD_SET_CONTROL_CURSOR_SIZE_W32_H32; break;
	case  64: asyh->curs.layout = NV917D_HEAD_SET_CONTROL_CURSOR_SIZE_W64_H64; break;
	case 128: asyh->curs.layout = NV917D_HEAD_SET_CONTROL_CURSOR_SIZE_W128_H128; break;
	case 256: asyh->curs.layout = NV917D_HEAD_SET_CONTROL_CURSOR_SIZE_W256_H256; break;
	default:
		return -EINVAL;
	}
	return 0;
}

const struct nv50_head_func
head917d = {
	.view = head907d_view,
	.mode = head907d_mode,
	.olut = head907d_olut,
	.olut_size = 1024,
	.olut_set = head907d_olut_set,
	.olut_clr = head907d_olut_clr,
	.core_calc = head507d_core_calc,
	.core_set = head907d_core_set,
	.core_clr = head907d_core_clr,
	.curs_layout = head917d_curs_layout,
	.curs_format = head507d_curs_format,
	.curs_set = head907d_curs_set,
	.curs_clr = head907d_curs_clr,
	.base = head917d_base,
	.ovly = head907d_ovly,
	.dither = head917d_dither,
	.procamp = head907d_procamp,
	.or = head907d_or,
};
