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

static void
head827d_curs_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		evo_mthd(push, 0x0880 + head->base.index * 0x400, 1);
		evo_data(push, 0x05000000);
		evo_mthd(push, 0x089c + head->base.index * 0x400, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, core);
	}
}

static int
head827d_curs_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 5)))
		return ret;

	PUSH_NVSQ(push, NV827D, 0x0880 + (i * 0x400), 0x80000000 |
						      asyh->curs.layout << 26 |
						      asyh->curs.format << 24,
				0x0884 + (i * 0x400), asyh->curs.offset >> 8);
	PUSH_NVSQ(push, NV827D, 0x089c + (i * 0x400), asyh->curs.handle);
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

	PUSH_NVSQ(push, NV827D, 0x0860 + (i * 0x400), asyh->core.offset >> 8);
	PUSH_NVSQ(push, NV827D, 0x0868 + (i * 0x400), asyh->core.h << 16 | asyh->core.w,
				0x086c + (i * 0x400), asyh->core.layout << 20 |
						     (asyh->core.pitch >> 8) << 8 |
						      asyh->core.blocks << 8 |
						      asyh->core.blockh,
				0x0870 + (i * 0x400), asyh->core.format << 8,
				0x0874 + (i * 0x400), asyh->core.handle);
	PUSH_NVSQ(push, NV827D, 0x08c0 + (i * 0x400), asyh->core.y << 16 | asyh->core.x);
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

	PUSH_NVSQ(push, NV827D, 0x0840 + (i * 0x400), 0x00000000);
	PUSH_NVSQ(push, NV827D, 0x085c + (i * 0x400), 0x00000000);
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

	PUSH_NVSQ(push, NV827D, 0x0840 + (i * 0x400), 0x80000000 | asyh->olut.mode << 30,
				0x0844 + (i * 0x400), asyh->olut.offset >> 8);
	PUSH_NVSQ(push, NV827D, 0x085c + (i * 0x400), asyh->olut.handle);
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
