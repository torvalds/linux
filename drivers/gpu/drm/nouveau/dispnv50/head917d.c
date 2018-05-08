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

static void
head917d_dither(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x04a0 + (head->base.index * 0x0300), 1);
		evo_data(push, asyh->dither.mode << 3 |
			       asyh->dither.bits << 1 |
			       asyh->dither.enable);
		evo_kick(push, core);
	}
}

const struct nv50_head_func
head917d = {
	.view = head907d_view,
	.mode = head907d_mode,
	.ilut_set = head907d_ilut_set,
	.ilut_clr = head907d_ilut_clr,
	.core_calc = head507d_core_calc,
	.core_set = head907d_core_set,
	.core_clr = head907d_core_clr,
	.curs_set = head907d_curs_set,
	.curs_clr = head907d_curs_clr,
	.base = head907d_base,
	.ovly = head907d_ovly,
	.dither = head917d_dither,
	.procamp = head907d_procamp,
	.or = head907d_or,
};
