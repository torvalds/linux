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

#include <nvif/class.h>

static void
head907d_or(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if (core->base.user.oclass >= GF110_DISP_CORE_CHANNEL_DMA &&
	    (push = evo_wait(core, 3))) {
		evo_mthd(push, 0x0404 + (head->base.index * 0x300), 2);
		evo_data(push, 0x00000001 | (asyh->or.depth  << 6) |
					    (asyh->or.nvsync << 4) |
					    (asyh->or.nhsync << 3));
		evo_data(push, 0x31ec6000 | (head->base.index << 25) |
					     asyh->mode.interlace);
		evo_kick(push, core);
	}
}

static void
head507d_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x08a8 + (head->base.index * 0x400), 1);
		else
			evo_mthd(push, 0x0498 + (head->base.index * 0x300), 1);
		evo_data(push, (asyh->procamp.sat.sin << 20) |
			       (asyh->procamp.sat.cos << 8));
		evo_kick(push, core);
	}
}

static void
head507d_dither(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x08a0 + (head->base.index * 0x0400), 1);
		else
		if (core->base.user.oclass < GK104_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x0490 + (head->base.index * 0x0300), 1);
		else
			evo_mthd(push, 0x04a0 + (head->base.index * 0x0300), 1);
		evo_data(push, (asyh->dither.mode << 3) |
			       (asyh->dither.bits << 1) |
			        asyh->dither.enable);
		evo_kick(push, core);
	}
}

static void
head507d_ovly(struct nv50_head *head, struct nv50_head_atom *asyh)
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
	}

	if ((push = evo_wait(core, 2))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x0904 + head->base.index * 0x400, 1);
		else
			evo_mthd(push, 0x04d4 + head->base.index * 0x300, 1);
		evo_data(push, bounds);
		evo_kick(push, core);
	}
}

static void
head507d_base(struct nv50_head *head, struct nv50_head_atom *asyh)
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
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x0900 + head->base.index * 0x400, 1);
		else
			evo_mthd(push, 0x04d0 + head->base.index * 0x300, 1);
		evo_data(push, bounds);
		evo_kick(push, core);
	}
}

static void
head507d_curs_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		if (core->base.user.oclass < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + head->base.index * 0x400, 1);
			evo_data(push, 0x05000000);
		} else
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + head->base.index * 0x400, 1);
			evo_data(push, 0x05000000);
			evo_mthd(push, 0x089c + head->base.index * 0x400, 1);
			evo_data(push, 0x00000000);
		} else {
			evo_mthd(push, 0x0480 + head->base.index * 0x300, 1);
			evo_data(push, 0x05000000);
			evo_mthd(push, 0x048c + head->base.index * 0x300, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, core);
	}
}

static void
head507d_curs_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 5))) {
		if (core->base.user.oclass < G82_DISP_BASE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + head->base.index * 0x400, 2);
			evo_data(push, 0x80000000 | (asyh->curs.layout << 26) |
						    (asyh->curs.format << 24));
			evo_data(push, asyh->curs.offset >> 8);
		} else
		if (core->base.user.oclass < GF110_DISP_BASE_CHANNEL_DMA) {
			evo_mthd(push, 0x0880 + head->base.index * 0x400, 2);
			evo_data(push, 0x80000000 | (asyh->curs.layout << 26) |
						    (asyh->curs.format << 24));
			evo_data(push, asyh->curs.offset >> 8);
			evo_mthd(push, 0x089c + head->base.index * 0x400, 1);
			evo_data(push, asyh->curs.handle);
		} else {
			evo_mthd(push, 0x0480 + head->base.index * 0x300, 2);
			evo_data(push, 0x80000000 | (asyh->curs.layout << 26) |
						    (asyh->curs.format << 24));
			evo_data(push, asyh->curs.offset >> 8);
			evo_mthd(push, 0x048c + head->base.index * 0x300, 1);
			evo_data(push, asyh->curs.handle);
		}
		evo_kick(push, core);
	}
}

static void
head507d_core_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA)
			evo_mthd(push, 0x0874 + head->base.index * 0x400, 1);
		else
			evo_mthd(push, 0x0474 + head->base.index * 0x300, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, core);
	}
}

static void
head507d_core_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 9))) {
		if (core->base.user.oclass < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0860 + head->base.index * 0x400, 1);
			evo_data(push, asyh->core.offset >> 8);
			evo_mthd(push, 0x0868 + head->base.index * 0x400, 4);
			evo_data(push, (asyh->core.h << 16) | asyh->core.w);
			evo_data(push, asyh->core.layout << 20 |
				       (asyh->core.pitch >> 8) << 8 |
				       asyh->core.block);
			evo_data(push, asyh->core.kind << 16 |
				       asyh->core.format << 8);
			evo_data(push, asyh->core.handle);
			evo_mthd(push, 0x08c0 + head->base.index * 0x400, 1);
			evo_data(push, (asyh->core.y << 16) | asyh->core.x);
			/* EVO will complain with INVALID_STATE if we have an
			 * active cursor and (re)specify HeadSetContextDmaIso
			 * without also updating HeadSetOffsetCursor.
			 */
			asyh->set.curs = asyh->curs.visible;
		} else
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0860 + head->base.index * 0x400, 1);
			evo_data(push, asyh->core.offset >> 8);
			evo_mthd(push, 0x0868 + head->base.index * 0x400, 4);
			evo_data(push, (asyh->core.h << 16) | asyh->core.w);
			evo_data(push, asyh->core.layout << 20 |
				       (asyh->core.pitch >> 8) << 8 |
				       asyh->core.block);
			evo_data(push, asyh->core.format << 8);
			evo_data(push, asyh->core.handle);
			evo_mthd(push, 0x08c0 + head->base.index * 0x400, 1);
			evo_data(push, (asyh->core.y << 16) | asyh->core.x);
		} else {
			evo_mthd(push, 0x0460 + head->base.index * 0x300, 1);
			evo_data(push, asyh->core.offset >> 8);
			evo_mthd(push, 0x0468 + head->base.index * 0x300, 4);
			evo_data(push, (asyh->core.h << 16) | asyh->core.w);
			evo_data(push, asyh->core.layout << 24 |
				       (asyh->core.pitch >> 8) << 8 |
				       asyh->core.block);
			evo_data(push, asyh->core.format << 8);
			evo_data(push, asyh->core.handle);
			evo_mthd(push, 0x04b0 + head->base.index * 0x300, 1);
			evo_data(push, (asyh->core.y << 16) | asyh->core.x);
		}
		evo_kick(push, core);
	}
}

static void
head507d_ilut_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		if (core->base.user.oclass < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0840 + (head->base.index * 0x400), 1);
			evo_data(push, 0x40000000);
		} else
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0840 + (head->base.index * 0x400), 1);
			evo_data(push, 0x40000000);
			evo_mthd(push, 0x085c + (head->base.index * 0x400), 1);
			evo_data(push, 0x00000000);
		} else {
			evo_mthd(push, 0x0440 + (head->base.index * 0x300), 1);
			evo_data(push, 0x03000000);
			evo_mthd(push, 0x045c + (head->base.index * 0x300), 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, core);
	}
}

static void
head507d_ilut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 7))) {
		if (core->base.user.oclass < G82_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0840 + (head->base.index * 0x400), 2);
			evo_data(push, 0x80000000 | asyh->ilut.mode << 30);
			evo_data(push, asyh->ilut.offset >> 8);
		} else
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0840 + (head->base.index * 0x400), 2);
			evo_data(push, 0x80000000 | asyh->ilut.mode << 30);
			evo_data(push, asyh->ilut.offset >> 8);
			evo_mthd(push, 0x085c + (head->base.index * 0x400), 1);
			evo_data(push, asyh->ilut.handle);
		} else {
			evo_mthd(push, 0x0440 + (head->base.index * 0x300), 4);
			evo_data(push, 0x80000000 | asyh->ilut.mode << 24);
			evo_data(push, asyh->ilut.offset >> 8);
			evo_data(push, 0x00000000);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x045c + (head->base.index * 0x300), 1);
			evo_data(push, asyh->ilut.handle);
		}
		evo_kick(push, core);
	}
}

static void
head507d_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	struct nv50_head_mode *m = &asyh->mode;
	u32 *push;
	if ((push = evo_wait(core, 14))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x0804 + (head->base.index * 0x400), 2);
			evo_data(push, 0x00800000 | m->clock);
			evo_data(push, m->interlace ? 0x00000002 : 0x00000000);
			evo_mthd(push, 0x0810 + (head->base.index * 0x400), 7);
			evo_data(push, 0x00000000);
			evo_data(push, (m->v.active  << 16) | m->h.active );
			evo_data(push, (m->v.synce   << 16) | m->h.synce  );
			evo_data(push, (m->v.blanke  << 16) | m->h.blanke );
			evo_data(push, (m->v.blanks  << 16) | m->h.blanks );
			evo_data(push, (m->v.blank2e << 16) | m->v.blank2s);
			evo_data(push, asyh->mode.v.blankus);
			evo_mthd(push, 0x082c + (head->base.index * 0x400), 1);
			evo_data(push, 0x00000000);
		} else {
			evo_mthd(push, 0x0410 + (head->base.index * 0x300), 6);
			evo_data(push, 0x00000000);
			evo_data(push, (m->v.active  << 16) | m->h.active );
			evo_data(push, (m->v.synce   << 16) | m->h.synce  );
			evo_data(push, (m->v.blanke  << 16) | m->h.blanke );
			evo_data(push, (m->v.blanks  << 16) | m->h.blanks );
			evo_data(push, (m->v.blank2e << 16) | m->v.blank2s);
			evo_mthd(push, 0x042c + (head->base.index * 0x300), 2);
			evo_data(push, 0x00000000); /* ??? */
			evo_data(push, 0xffffff00);
			evo_mthd(push, 0x0450 + (head->base.index * 0x300), 3);
			evo_data(push, m->clock * 1000);
			evo_data(push, 0x00200000); /* ??? */
			evo_data(push, m->clock * 1000);
		}
		evo_kick(push, core);
	}
}

static void
head507d_view(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 10))) {
		if (core->base.user.oclass < GF110_DISP_CORE_CHANNEL_DMA) {
			evo_mthd(push, 0x08a4 + (head->base.index * 0x400), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x08c8 + (head->base.index * 0x400), 1);
			evo_data(push, (asyh->view.iH << 16) | asyh->view.iW);
			evo_mthd(push, 0x08d8 + (head->base.index * 0x400), 2);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
		} else {
			evo_mthd(push, 0x0494 + (head->base.index * 0x300), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x04b8 + (head->base.index * 0x300), 1);
			evo_data(push, (asyh->view.iH << 16) | asyh->view.iW);
			evo_mthd(push, 0x04c0 + (head->base.index * 0x300), 3);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
			evo_data(push, (asyh->view.oH << 16) | asyh->view.oW);
		}
		evo_kick(push, core);
	}
}

const struct nv50_head_func
head507d = {
	.view = head507d_view,
	.mode = head507d_mode,
	.ilut_set = head507d_ilut_set,
	.ilut_clr = head507d_ilut_clr,
	.core_set = head507d_core_set,
	.core_clr = head507d_core_clr,
	.curs_set = head507d_curs_set,
	.curs_clr = head507d_curs_clr,
	.base = head507d_base,
	.ovly = head507d_ovly,
	.dither = head507d_dither,
	.procamp = head507d_procamp,
	.or = head907d_or,
};
