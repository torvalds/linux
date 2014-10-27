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

#include <subdev/fb/regsnv04.h>

#include "priv.h"

static int
nv04_ram_create(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nouveau_fb *pfb = nouveau_fb(parent);
	struct nouveau_ram *ram;
	u32 boot0 = nv_rd32(pfb, NV04_PFB_BOOT_0);
	int ret;

	ret = nouveau_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	if (boot0 & 0x00000100) {
		ram->size  = ((boot0 >> 12) & 0xf) * 2 + 2;
		ram->size *= 1024 * 1024;
	} else {
		switch (boot0 & NV04_PFB_BOOT_0_RAM_AMOUNT) {
		case NV04_PFB_BOOT_0_RAM_AMOUNT_32MB:
			ram->size = 32 * 1024 * 1024;
			break;
		case NV04_PFB_BOOT_0_RAM_AMOUNT_16MB:
			ram->size = 16 * 1024 * 1024;
			break;
		case NV04_PFB_BOOT_0_RAM_AMOUNT_8MB:
			ram->size = 8 * 1024 * 1024;
			break;
		case NV04_PFB_BOOT_0_RAM_AMOUNT_4MB:
			ram->size = 4 * 1024 * 1024;
			break;
		}
	}

	if ((boot0 & 0x00000038) <= 0x10)
		ram->type = NV_MEM_TYPE_SGRAM;
	else
		ram->type = NV_MEM_TYPE_SDRAM;
	return 0;
}

struct nouveau_oclass
nv04_ram_oclass = {
	.handle = 0,
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_ram_create,
		.dtor = _nouveau_ram_dtor,
		.init = _nouveau_ram_init,
		.fini = _nouveau_ram_fini,
	}
};
