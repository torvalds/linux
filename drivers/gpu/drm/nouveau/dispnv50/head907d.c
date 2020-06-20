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

void
head907d_or(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 3))) {
		evo_mthd(push, 0x0404 + (head->base.index * 0x300), 2);
		evo_data(push, asyh->or.depth  << 6 |
			       asyh->or.nvsync << 4 |
			       asyh->or.nhsync << 3 |
			       asyh->or.crc_raster);
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

	PUSH_NVSQ(push, NV907D, 0x0410 + (i * 0x300), 0x00000000,
				0x0414 + (i * 0x300), m->v.active  << 16 | m->h.active,
				0x0418 + (i * 0x300), m->v.synce   << 16 | m->h.synce,
				0x041c + (i * 0x300), m->v.blanke  << 16 | m->h.blanke,
				0x0420 + (i * 0x300), m->v.blanks  << 16 | m->h.blanks,
				0x0424 + (i * 0x300), m->v.blank2e << 16 | m->v.blank2s);
	PUSH_NVSQ(push, NV907D, 0x042c + (i * 0x300), 0x00000000,
				0x0430 + (i * 0x300), 0xffffff00);
	PUSH_NVSQ(push, NV907D, 0x0450 + (i * 0x300), m->clock * 1000,
				0x0454 + (i * 0x300), 0x00200000,
				0x0458 + (i * 0x300), m->clock * 1000);
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

	PUSH_NVSQ(push, NV907D, 0x0494 + (i * 0x300), 0x00000000);
	PUSH_NVSQ(push, NV907D, 0x04b8 + (i * 0x300), asyh->view.iH << 16 | asyh->view.iW);
	PUSH_NVSQ(push, NV907D, 0x04c0 + (i * 0x300), asyh->view.oH << 16 | asyh->view.oW,
				0x04c4 + (i * 0x300), asyh->view.oH << 16 | asyh->view.oW,
				0x04c8 + (i * 0x300), asyh->view.oH << 16 | asyh->view.oW);
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
