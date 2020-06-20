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

static void
headc57d_or(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u8 depth;
	u32 *push;

	if ((push = evo_wait(core, 2))) {
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

		evo_mthd(push, 0x2004 + (head->base.index * 0x400), 1);
		evo_data(push, 0xfc000000 |
			       depth << 4 |
			       asyh->or.nvsync << 3 |
			       asyh->or.nhsync << 2 |
			       asyh->or.crc_raster);
		evo_kick(push, core);
	}
}

static int
headc57d_procamp(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	//TODO:
	PUSH_NVSQ(push, NVC57D, 0x2000 + (i * 0x400), 0x00000000);
	return 0;
}

static int
headc57d_olut_clr(struct nv50_head *head)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_NVSQ(push, NVC57D, 0x2288 + (i * 0x400), 0x00000000);
	return 0;
}

static int
headc57d_olut_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 5)))
		return ret;

	PUSH_NVSQ(push, NVC57D, 0x2280 + (i * 0x400), asyh->olut.size << 8 |
						      asyh->olut.mode << 2 |
						      asyh->olut.output_mode,
				0x2284 + (i * 0x400), 0xffffffff,
				0x2288 + (i * 0x400), asyh->olut.handle,
				0x228c + (i * 0x400), asyh->olut.offset >> 8);
	return 0;
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

static int
headc57d_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	struct nv50_head_mode *m = &asyh->mode;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 13)))
		return ret;

	PUSH_NVSQ(push, NVC57D, 0x2064 + (i * 0x400), m->v.active  << 16 | m->h.active,
				0x2068 + (i * 0x400), m->v.synce   << 16 | m->h.synce,
				0x206c + (i * 0x400), m->v.blanke  << 16 | m->h.blanke,
				0x2070 + (i * 0x400), m->v.blanks  << 16 | m->h.blanks,
				0x2074 + (i * 0x400), m->v.blank2e << 16 | m->v.blank2s);
	PUSH_NVSQ(push, NVC57D, 0x2008 + (i * 0x400), m->interlace,
				0x200c + (i * 0x400), m->clock * 1000);
	PUSH_NVSQ(push, NVC57D, 0x2028 + (i * 0x400), m->clock * 1000);

	/*XXX: HEAD_USAGE_BOUNDS, doesn't belong here. */
	PUSH_NVSQ(push, NVC57D, 0x2030 + (i * 0x400), 0x00001114);
	return 0;
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
	/* TODO: flexible window mappings */
	.static_wndw_map = headc37d_static_wndw_map,
};
