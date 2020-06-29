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

static void
headc37d_or(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		/*XXX: This is a dirty hack until OR depth handling is
		 *     improved later for deep colour etc.
		 */
		switch (asyh->or.depth) {
		case 6: asyh->or.depth = 5; break;
		case 5: asyh->or.depth = 4; break;
		case 2: asyh->or.depth = 1; break;
		case 0:	asyh->or.depth = 4; break;
		default:
			WARN_ON(1);
			break;
		}

		evo_mthd(push, 0x2004 + (head->base.index * 0x400), 1);
		evo_data(push, 0x00000001 |
			       asyh->or.depth << 4 |
			       asyh->or.nvsync << 3 |
			       asyh->or.nhsync << 2);
		evo_kick(push, core);
	}
}

static void
headc37d_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x2000 + (head->base.index * 0x400), 1);
		evo_data(push, 0x80000000 |
			       asyh->procamp.sat.sin << 16 |
			       asyh->procamp.sat.cos << 4);
		evo_kick(push, core);
	}
}

void
headc37d_dither(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x2018 + (head->base.index * 0x0400), 1);
		evo_data(push, asyh->dither.mode << 8 |
			       asyh->dither.bits << 4 |
			       asyh->dither.enable);
		evo_kick(push, core);
	}
}

void
headc37d_curs_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		evo_mthd(push, 0x209c + head->base.index * 0x400, 1);
		evo_data(push, 0x000000cf);
		evo_mthd(push, 0x2088 + head->base.index * 0x400, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, core);
	}
}

void
headc37d_curs_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 7))) {
		evo_mthd(push, 0x209c + head->base.index * 0x400, 2);
		evo_data(push, 0x80000000 |
			       asyh->curs.layout << 8 |
			       asyh->curs.format << 0);
		evo_data(push, 0x000072ff);
		evo_mthd(push, 0x2088 + head->base.index * 0x400, 1);
		evo_data(push, asyh->curs.handle);
		evo_mthd(push, 0x2090 + head->base.index * 0x400, 1);
		evo_data(push, asyh->curs.offset >> 8);
		evo_kick(push, core);
	}
}

int
headc37d_curs_format(struct nv50_head *head, struct nv50_wndw_atom *asyw,
		     struct nv50_head_atom *asyh)
{
	asyh->curs.format = asyw->image.format;
	return 0;
}

static void
headc37d_olut_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x20ac + (head->base.index * 0x400), 1);
		evo_data(push, 0x00000000);
		evo_kick(push, core);
	}
}

static void
headc37d_olut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		evo_mthd(push, 0x20a4 + (head->base.index * 0x400), 3);
		evo_data(push, asyh->olut.output_mode << 8 |
			       asyh->olut.range << 4 |
			       asyh->olut.size);
		evo_data(push, asyh->olut.offset >> 8);
		evo_data(push, asyh->olut.handle);
		evo_kick(push, core);
	}
}

static bool
headc37d_olut(struct nv50_head *head, struct nv50_head_atom *asyh, int size)
{
	if (size != 256 && size != 1024)
		return false;

	asyh->olut.mode = 2;
	asyh->olut.size = size == 1024 ? 2 : 0;
	asyh->olut.range = 0;
	asyh->olut.output_mode = 1;
	asyh->olut.load = head907d_olut_load;
	return true;
}

static void
headc37d_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	struct nv50_head_mode *m = &asyh->mode;
	u32 *push;
	if ((push = evo_wait(core, 13))) {
		evo_mthd(push, 0x2064 + (head->base.index * 0x400), 5);
		evo_data(push, (m->v.active  << 16) | m->h.active );
		evo_data(push, (m->v.synce   << 16) | m->h.synce  );
		evo_data(push, (m->v.blanke  << 16) | m->h.blanke );
		evo_data(push, (m->v.blanks  << 16) | m->h.blanks );
		evo_data(push, (m->v.blank2e << 16) | m->v.blank2s);
		evo_mthd(push, 0x2008 + (head->base.index * 0x400), 2);
		evo_data(push, m->interlace);
		evo_data(push, m->clock * 1000);
		evo_mthd(push, 0x2028 + (head->base.index * 0x400), 1);
		evo_data(push, m->clock * 1000);
		/*XXX: HEAD_USAGE_BOUNDS, doesn't belong here. */
		evo_mthd(push, 0x2030 + (head->base.index * 0x400), 1);
		evo_data(push, 0x00000124);
		evo_kick(push, core);
	}
}

void
headc37d_view(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		evo_mthd(push, 0x204c + (head->base.index * 0x400), 1);
		evo_data(push, (asyh->view.iH << 16) | asyh->view.iW);
		evo_mthd(push, 0x2058 + (head->base.index * 0x400), 1);
		evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
		evo_kick(push, core);
	}
}

const struct nv50_head_func
headc37d = {
	.view = headc37d_view,
	.mode = headc37d_mode,
	.olut = headc37d_olut,
	.olut_size = 1024,
	.olut_set = headc37d_olut_set,
	.olut_clr = headc37d_olut_clr,
	.curs_layout = head917d_curs_layout,
	.curs_format = headc37d_curs_format,
	.curs_set = headc37d_curs_set,
	.curs_clr = headc37d_curs_clr,
	.dither = headc37d_dither,
	.procamp = headc37d_procamp,
	.or = headc37d_or,
};
