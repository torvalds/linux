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

#include <nvif/pushc37b.h>

static int
headc37d_or(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u8 depth;
	int ret;

	/*XXX: This is a dirty hack until OR depth handling is
	 *     improved later for deep colour etc.
	 */
	switch (asyh->or.depth) {
	case 6: depth = 5; break;
	case 5: depth = 4; break;
	case 2: depth = 1; break;
	case 0:	depth = 4; break;
	default:
		depth = asyh->or.depth;
		WARN_ON(1);
		break;
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NVC37D, 0x2004 + (i * 0x400), depth << 4 |
						      asyh->or.nvsync << 3 |
						      asyh->or.nhsync << 2 |
						      asyh->or.crc_raster);
	return 0;
}

static int
headc37d_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NVC37D, 0x2000 + (i * 0x400), 0x80000000 |
						      asyh->procamp.sat.sin << 16 |
						      asyh->procamp.sat.cos << 4);
	return 0;
}

int
headc37d_dither(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NV907D, 0x2018 + (i * 0x400), asyh->dither.mode << 8 |
						      asyh->dither.bits << 4 |
						      asyh->dither.enable);
	return 0;
}

int
headc37d_curs_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	PUSH_NVSQ(push, NVC37D, 0x209c + (i * 0x400), 0x000000cf);
	PUSH_NVSQ(push, NVC37D, 0x2088 + (i * 0x400), 0x00000000);
	return 0;
}

int
headc37d_curs_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 7)))
		return ret;

	PUSH_NVSQ(push, NVC37D, 0x209c + (i * 0x400), 0x80000000 |
						      asyh->curs.layout << 8 |
						      asyh->curs.format << 0,
				0x20a0 + (i * 0x400), 0x000072ff);
	PUSH_NVSQ(push, NVC37D, 0x2088 + (i * 0x400), asyh->curs.handle);
	PUSH_NVSQ(push, NVC37D, 0x2090 + (i * 0x400), asyh->curs.offset >> 8);
	return 0;
}

int
headc37d_curs_format(struct nv50_head *head, struct nv50_wndw_atom *asyw,
		     struct nv50_head_atom *asyh)
{
	asyh->curs.format = asyw->image.format;
	return 0;
}

static int
headc37d_olut_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NVC37D, 0x20ac + (i * 0x400), 0x00000000);
	return 0;
}

static int
headc37d_olut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	PUSH_NVSQ(push, NVC37D, 0x20a4 + (i * 0x400), asyh->olut.output_mode << 8 |
						      asyh->olut.range << 4 |
						      asyh->olut.size,
				0x20a8 + (i * 0x400), asyh->olut.offset >> 8,
				0x20ac + (i * 0x400), asyh->olut.handle);
	return 0;
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

static int
headc37d_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	struct nv50_head_mode *m = &asyh->mode;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 13)))
		return ret;

	PUSH_NVSQ(push, NVC37D, 0x2064 + (i * 0x400), m->v.active  << 16 | m->h.active,
				0x2068 + (i * 0x400), m->v.synce   << 16 | m->h.synce,
				0x206c + (i * 0x400), m->v.blanke  << 16 | m->h.blanke,
				0x2070 + (i * 0x400), m->v.blanks  << 16 | m->h.blanks,
				0x2074 + (i * 0x400), m->v.blank2e << 16 | m->v.blank2s);
	PUSH_NVSQ(push, NVC37D, 0x2008 + (i * 0x400), m->interlace,
				0x200c + (i * 0x400), m->clock * 1000);
	PUSH_NVSQ(push, NVC37D, 0x2028 + (i * 0x400), m->clock * 1000);

	/*XXX: HEAD_USAGE_BOUNDS, doesn't belong here. */
	PUSH_NVSQ(push, NVC37D, 0x2030 + (i * 0x400), 0x00000124);
	return 0;
}

int
headc37d_view(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	PUSH_NVSQ(push, NVC37D, 0x204c + (i * 0x400), asyh->view.iH << 16 | asyh->view.iW);
	PUSH_NVSQ(push, NVC37D, 0x2058 + (i * 0x400), asyh->view.oH << 16 | asyh->view.oW);
	return 0;
}

void
headc37d_static_wndw_map(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	int i, end;

	for (i = head->base.index * 2, end = i + 2; i < end; i++)
		asyh->wndw.owned |= BIT(i);
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
	.static_wndw_map = headc37d_static_wndw_map,
};
