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
#include "lut.h"
#include "disp.h"

#include <drm/drm_color_mgmt.h>
#include <drm/drm_mode.h>
#include <drm/drm_property.h>

#include <nvif/class.h>

u32
nv50_lut_load(struct nv50_lut *lut, bool legacy, int buffer,
	      struct drm_property_blob *blob)
{
	struct drm_color_lut *in = (struct drm_color_lut *)blob->data;
	void __iomem *mem = lut->mem[buffer].object.map.ptr;
	const int size = blob->length / sizeof(*in);
	int bits, shift, i;
	u16 zero, r, g, b;
	u32 addr = lut->mem[buffer].addr;

	/* This can't happen.. But it shuts the compiler up. */
	if (WARN_ON(size != 256))
		return 0;

	if (legacy) {
		bits = 11;
		shift = 3;
		zero = 0x0000;
	} else {
		bits = 14;
		shift = 0;
		zero = 0x6000;
	}

	for (i = 0; i < size; i++) {
		r = (drm_color_lut_extract(in[i].  red, bits) + zero) << shift;
		g = (drm_color_lut_extract(in[i].green, bits) + zero) << shift;
		b = (drm_color_lut_extract(in[i]. blue, bits) + zero) << shift;
		writew(r, mem + (i * 0x08) + 0);
		writew(g, mem + (i * 0x08) + 2);
		writew(b, mem + (i * 0x08) + 4);
	}

	/* INTERPOLATE modes require a "next" entry to interpolate with,
	 * so we replicate the last entry to deal with this for now.
	 */
	writew(r, mem + (i * 0x08) + 0);
	writew(g, mem + (i * 0x08) + 2);
	writew(b, mem + (i * 0x08) + 4);
	return addr;
}

void
nv50_lut_fini(struct nv50_lut *lut)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(lut->mem); i++)
		nvif_mem_fini(&lut->mem[i]);
}

int
nv50_lut_init(struct nv50_disp *disp, struct nvif_mmu *mmu,
	      struct nv50_lut *lut)
{
	const u32 size = disp->disp->object.oclass < GF110_DISP ? 257 : 1025;
	int i;
	for (i = 0; i < ARRAY_SIZE(lut->mem); i++) {
		int ret = nvif_mem_init_map(mmu, NVIF_MEM_VRAM, size * 8,
					    &lut->mem[i]);
		if (ret)
			return ret;
	}
	return 0;
}
