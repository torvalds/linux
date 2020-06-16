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
headc57d_or(struct nv50_head *head, struct nv50_head_atom *asyh)
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
		evo_data(push, 0xfc000001 |
			       asyh->or.depth << 4 |
			       asyh->or.nvsync << 3 |
			       asyh->or.nhsync << 2);
		evo_kick(push, core);
	}
}

static void
headc57d_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x2000 + (head->base.index * 0x400), 1);
#if 0
		evo_data(push, 0x80000000 |
			       asyh->procamp.sat.sin << 16 |
			       asyh->procamp.sat.cos << 4);
#else
		evo_data(push, 0);
#endif
		evo_kick(push, core);
	}
}

void
headc57d_olut_clr(struct nv50_head *head)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 2))) {
		evo_mthd(push, 0x2288 + (head->base.index * 0x400), 1);
		evo_data(push, 0x00000000);
		evo_kick(push, core);
	}
}

void
headc57d_olut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push;
	if ((push = evo_wait(core, 4))) {
		evo_mthd(push, 0x2280 + (head->base.index * 0x400), 4);
		evo_data(push, asyh->olut.size << 8 |
			       asyh->olut.mode << 2 |
			       asyh->olut.output_mode);
		evo_data(push, 0xffffffff); /* FP_NORM_SCALE. */
		evo_data(push, asyh->olut.handle);
		evo_data(push, asyh->olut.offset >> 8);
		evo_kick(push, core);
	}
}

static void
headc57d_olut_load_8(struct drm_color_lut *in, int size, void __iomem *mem)
{
	memset_io(mem, 0x00, 0x20); /* VSS header. */
	mem += 0x20;

	while (size--) {
		u16 r = drm_color_lut_extract(in->  red + 0, 16);
		u16 g = drm_color_lut_extract(in->green + 0, 16);
		u16 b = drm_color_lut_extract(in-> blue + 0, 16);
		u16 ri = 0, gi = 0, bi = 0, i;

		if (in++, size) {
			ri = (drm_color_lut_extract(in->  red, 16) - r) / 4;
			gi = (drm_color_lut_extract(in->green, 16) - g) / 4;
			bi = (drm_color_lut_extract(in-> blue, 16) - b) / 4;
		}

		for (i = 0; i < 4; i++, mem += 8) {
			writew(r + ri * i, mem + 0);
			writew(g + gi * i, mem + 2);
			writew(b + bi * i, mem + 4);
		}
	}

	/* INTERPOLATE modes require a "next" entry to interpolate with,
	 * so we replicate the last entry to deal with this for now.
	 */
	writew(readw(mem - 8), mem + 0);
	writew(readw(mem - 6), mem + 2);
	writew(readw(mem - 4), mem + 4);
}

static void
headc57d_olut_load(struct drm_color_lut *in, int size, void __iomem *mem)
{
	memset_io(mem, 0x00, 0x20); /* VSS header. */
	mem += 0x20;

	for (; size--; in++, mem += 0x08) {
		writew(drm_color_lut_extract(in->  red, 16), mem + 0);
		writew(drm_color_lut_extract(in->green, 16), mem + 2);
		writew(drm_color_lut_extract(in-> blue, 16), mem + 4);
	}

	/* INTERPOLATE modes require a "next" entry to interpolate with,
	 * so we replicate the last entry to deal with this for now.
	 */
	writew(readw(mem - 8), mem + 0);
	writew(readw(mem - 6), mem + 2);
	writew(readw(mem - 4), mem + 4);
}

bool
headc57d_olut(struct nv50_head *head, struct nv50_head_atom *asyh, int size)
{
	if (size != 0 && size != 256 && size != 1024)
		return false;

	asyh->olut.mode = 2; /* DIRECT10 */
	asyh->olut.size = 4 /* VSS header. */ + 1024 + 1 /* Entries. */;
	asyh->olut.output_mode = 1; /* INTERPOLATE_ENABLE. */
	if (size == 256)
		asyh->olut.load = headc57d_olut_load_8;
	else
		asyh->olut.load = headc57d_olut_load;
	return true;
}

static void
headc57d_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
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
		evo_data(push, 0x00001014);
		evo_kick(push, core);
	}
}

const struct nv50_head_func
headc57d = {
	.view = headc37d_view,
	.mode = headc57d_mode,
	.olut = headc57d_olut,
	.olut_identity = true,
	.olut_size = 1024,
	.olut_set = headc57d_olut_set,
	.olut_clr = headc57d_olut_clr,
	.curs_layout = head917d_curs_layout,
	.curs_format = headc37d_curs_format,
	.curs_set = headc37d_curs_set,
	.curs_clr = headc37d_curs_clr,
	.dither = headc37d_dither,
	.procamp = headc57d_procamp,
	.or = headc57d_or,
};
