/*
 * Copyright 2013 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs
 */

#include "priv.h"

static int
nv41_ram_create(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nouveau_fb *pfb = nouveau_fb(parent);
	struct nouveau_ram *ram;
	u32 pfb474 = nv_rd32(pfb, 0x100474);
	int ret;

	ret = nouveau_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	if (pfb474 & 0x00000004)
		ram->type = NV_MEM_TYPE_GDDR3;
	if (pfb474 & 0x00000002)
		ram->type = NV_MEM_TYPE_DDR2;
	if (pfb474 & 0x00000001)
		ram->type = NV_MEM_TYPE_DDR1;

	ram->size  =  nv_rd32(pfb, 0x10020c) & 0xff000000;
	ram->parts = (nv_rd32(pfb, 0x100200) & 0x00000003) + 1;
	ram->tags  =  nv_rd32(pfb, 0x100320);
	return 0;
}

struct nouveau_oclass
nv41_ram_oclass = {
	.handle = 0,
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv41_ram_create,
		.dtor = _nouveau_ram_dtor,
		.init = _nouveau_ram_init,
		.fini = _nouveau_ram_fini,
	}
};
