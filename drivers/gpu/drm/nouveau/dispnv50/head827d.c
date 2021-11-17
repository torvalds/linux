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

#include <nvhw/class/cl827d.h>

static int
head827d_curs_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	PUSH_MTHD(push, NV827D, HEAD_SET_CONTROL_CURSOR(i),
		  NVDEF(NV827D, HEAD_SET_CONTROL_CURSOR, ENABLE, DISABLE) |
		  NVDEF(NV827D, HEAD_SET_CONTROL_CURSOR, FORMAT, A8R8G8B8) |
		  NVDEF(NV827D, HEAD_SET_CONTROL_CURSOR, SIZE, W64_H64));

	PUSH_MTHD(push, NV827D, HEAD_SET_CONTEXT_DMA_CURSOR(i), 0x00000000);
	return 0;
}

static int
head827d_curs_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 5)))
		return ret;

	PUSH_MTHD(push, NV827D, HEAD_SET_CONTROL_CURSOR(i),
		  NVDEF(NV827D, HEAD_SET_CONTROL_CURSOR, ENABLE, ENABLE) |
		  NVVAL(NV827D, HEAD_SET_CONTROL_CURSOR, FORMAT, asyh->curs.format) |
		  NVVAL(NV827D, HEAD_SET_CONTROL_CURSOR, SIZE, asyh->curs.layout) |
		  NVVAL(NV827D, HEAD_SET_CONTROL_CURSOR, HOT_SPOT_X, 0) |
		  NVVAL(NV827D, HEAD_SET_CONTROL_CURSOR, HOT_SPOT_Y, 0) |
		  NVDEF(NV827D, HEAD_SET_CONTROL_CURSOR, COMPOSITION, ALPHA_BLEND) |
		  NVDEF(NV827D, HEAD_SET_CONTROL_CURSOR, SUB_OWNER, NONE),

				HEAD_SET_OFFSET_CURSOR(i), asyh->curs.offset >> 8);

	PUSH_MTHD(push, NV827D, HEAD_SET_CONTEXT_DMA_CURSOR(i), asyh->curs.handle);
	return 0;
}

static int
head827d_core_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 9)))
		return ret;

	PUSH_MTHD(push, NV827D, HEAD_SET_OFFSET(i, 0),
		  NVVAL(NV827D, HEAD_SET_OFFSET, ORIGIN, asyh->core.offset >> 8));

	PUSH_MTHD(push, NV827D, HEAD_SET_SIZE(i),
		  NVVAL(NV827D, HEAD_SET_SIZE, WIDTH, asyh->core.w) |
		  NVVAL(NV827D, HEAD_SET_SIZE, HEIGHT, asyh->core.h),

				HEAD_SET_STORAGE(i),
		  NVVAL(NV827D, HEAD_SET_STORAGE, BLOCK_HEIGHT, asyh->core.blockh) |
		  NVVAL(NV827D, HEAD_SET_STORAGE, PITCH, asyh->core.pitch >> 8) |
		  NVVAL(NV827D, HEAD_SET_STORAGE, PITCH, asyh->core.blocks) |
		  NVVAL(NV827D, HEAD_SET_STORAGE, MEMORY_LAYOUT, asyh->core.layout),

				HEAD_SET_PARAMS(i),
		  NVVAL(NV827D, HEAD_SET_PARAMS, FORMAT, asyh->core.format) |
		  NVDEF(NV827D, HEAD_SET_PARAMS, SUPER_SAMPLE, X1_AA) |
		  NVDEF(NV827D, HEAD_SET_PARAMS, GAMMA, LINEAR),

				HEAD_SET_CONTEXT_DMAS_ISO(i, 0),
		  NVVAL(NV827D, HEAD_SET_CONTEXT_DMAS_ISO, HANDLE, asyh->core.handle));

	PUSH_MTHD(push, NV827D, HEAD_SET_VIEWPORT_POINT_IN(i, 0),
		  NVVAL(NV827D, HEAD_SET_VIEWPORT_POINT_IN, X, asyh->core.x) |
		  NVVAL(NV827D, HEAD_SET_VIEWPORT_POINT_IN, Y, asyh->core.y));
	return 0;
}

static int
head827d_olut_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	PUSH_MTHD(push, NV827D, HEAD_SET_BASE_LUT_LO(i),
		  NVDEF(NV827D, HEAD_SET_BASE_LUT_LO, ENABLE, DISABLE));

	PUSH_MTHD(push, NV827D, HEAD_SET_CONTEXT_DMA_LUT(i), 0x00000000);
	return 0;
}

static int
head827d_olut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 5)))
		return ret;

	PUSH_MTHD(push, NV827D, HEAD_SET_BASE_LUT_LO(i),
		  NVDEF(NV827D, HEAD_SET_BASE_LUT_LO, ENABLE, ENABLE) |
		  NVVAL(NV827D, HEAD_SET_BASE_LUT_LO, MODE, asyh->olut.mode) |
		  NVVAL(NV827D, HEAD_SET_BASE_LUT_LO, ORIGIN, 0),

				HEAD_SET_BASE_LUT_HI(i),
		  NVVAL(NV827D, HEAD_SET_BASE_LUT_HI, ORIGIN, asyh->olut.offset >> 8));

	PUSH_MTHD(push, NV827D, HEAD_SET_CONTEXT_DMA_LUT(i), asyh->olut.handle);
	return 0;
}

const struct nv50_head_func
head827d = {
	.view = head507d_view,
	.mode = head507d_mode,
	.olut = head507d_olut,
	.olut_size = 256,
	.olut_set = head827d_olut_set,
	.olut_clr = head827d_olut_clr,
	.core_calc = head507d_core_calc,
	.core_set = head827d_core_set,
	.core_clr = head507d_core_clr,
	.curs_layout = head507d_curs_layout,
	.curs_format = head507d_curs_format,
	.curs_set = head827d_curs_set,
	.curs_clr = head827d_curs_clr,
	.base = head507d_base,
	.ovly = head507d_ovly,
	.dither = head507d_dither,
	.procamp = head507d_procamp,
};
