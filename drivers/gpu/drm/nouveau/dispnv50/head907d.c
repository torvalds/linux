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

void
head907d_or(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 3))) {
		evo_mthd(push, 0x0404 + (head->base.index * 0x300), 2);
		evo_data(push, 0x00000001 | asyh->or.depth  << 6 |
					    asyh->or.nvsync << 4 |
					    asyh->or.nhsync << 3);
		evo_data(push, 0x31ec6000 | head->base.index << 25 |
					    asyh->mode.interlace);
		evo_kick(push, core);
	}
}

void
head907d_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x0498 + (head->base.index * 0x300), 1);
		evo_data(push, asyh->procamp.sat.sin << 20 |
			       asyh->procamp.sat.cos << 8);
		evo_kick(push, core);
	}
}

static void
head907d_dither(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x0490 + (head->base.index * 0x0300), 1);
		evo_data(push, asyh->dither.mode << 3 |
			       asyh->dither.bits << 1 |
			       asyh->dither.enable);
		evo_kick(push, core);
	}
}

void
head907d_ovly(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 bounds = 0;
	u32 *push;

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

	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x04d4 + head->base.index * 0x300, 1);
		evo_data(push, bounds);
		evo_kick(push, core);
	}
}

static void
head907d_base(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 bounds = 0;
	u32 *push;

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

	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x04d0 + head->base.index * 0x300, 1);
		evo_data(push, bounds);
		evo_kick(push, core);
	}
}

void
head907d_curs_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		evo_mthd(push, 0x0480 + head->base.index * 0x300, 1);
		evo_data(push, 0x05000000);
		evo_mthd(push, 0x048c + head->base.index * 0x300, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, core);
	}
}

void
head907d_curs_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 5))) {
		evo_mthd(push, 0x0480 + head->base.index * 0x300, 2);
		evo_data(push, 0x80000000 | asyh->curs.layout << 26 |
					    asyh->curs.format << 24);
		evo_data(push, asyh->curs.offset >> 8);
		evo_mthd(push, 0x048c + head->base.index * 0x300, 1);
		evo_data(push, asyh->curs.handle);
		evo_kick(push, core);
	}
}

void
head907d_core_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x0474 + head->base.index * 0x300, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, core);
	}
}

void
head907d_core_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 9))) {
		evo_mthd(push, 0x0460 + head->base.index * 0x300, 1);
		evo_data(push, asyh->core.offset >> 8);
		evo_mthd(push, 0x0468 + head->base.index * 0x300, 4);
		evo_data(push, asyh->core.h << 16 | asyh->core.w);
		evo_data(push, asyh->core.layout << 24 |
			       (asyh->core.pitch >> 8) << 8 |
			       asyh->core.blocks << 8 |
			       asyh->core.blockh);
		evo_data(push, asyh->core.format << 8);
		evo_data(push, asyh->core.handle);
		evo_mthd(push, 0x04b0 + head->base.index * 0x300, 1);
		evo_data(push, asyh->core.y << 16 | asyh->core.x);
		evo_kick(push, core);
	}
}

void
head907d_olut_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		evo_mthd(push, 0x0448 + (head->base.index * 0x300), 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x045c + (head->base.index * 0x300), 1);
		evo_data(push, 0x00000000);
		evo_kick(push, core);
	}
}

void
head907d_olut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 5))) {
		evo_mthd(push, 0x0448 + (head->base.index * 0x300), 2);
		evo_data(push, 0x80000000 | asyh->olut.mode << 24);
		evo_data(push, asyh->olut.offset >> 8);
		evo_mthd(push, 0x045c + (head->base.index * 0x300), 1);
		evo_data(push, asyh->olut.handle);
		evo_kick(push, core);
	}
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

void
head907d_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	struct nv50_head_mode *m = &asyh->mode;
	u32 *push;
	if ((push = evo_wait(core, 14))) {
		evo_mthd(push, 0x0410 + (head->base.index * 0x300), 6);
		evo_data(push, 0x00000000);
		evo_data(push, m->v.active  << 16 | m->h.active );
		evo_data(push, m->v.synce   << 16 | m->h.synce  );
		evo_data(push, m->v.blanke  << 16 | m->h.blanke );
		evo_data(push, m->v.blanks  << 16 | m->h.blanks );
		evo_data(push, m->v.blank2e << 16 | m->v.blank2s);
		evo_mthd(push, 0x042c + (head->base.index * 0x300), 2);
		evo_data(push, 0x00000000); /* ??? */
		evo_data(push, 0xffffff00);
		evo_mthd(push, 0x0450 + (head->base.index * 0x300), 3);
		evo_data(push, m->clock * 1000);
		evo_data(push, 0x00200000); /* ??? */
		evo_data(push, m->clock * 1000);
		evo_kick(push, core);
	}
}

void
head907d_view(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 8))) {
		evo_mthd(push, 0x0494 + (head->base.index * 0x300), 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x04b8 + (head->base.index * 0x300), 1);
		evo_data(push, asyh->view.iH << 16 | asyh->view.iW);
		evo_mthd(push, 0x04c0 + (head->base.index * 0x300), 3);
		evo_data(push, asyh->view.oH << 16 | asyh->view.oW);
		evo_data(push, asyh->view.oH << 16 | asyh->view.oW);
		evo_data(push, asyh->view.oH << 16 | asyh->view.oW);
		evo_kick(push, core);
	}
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
