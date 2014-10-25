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

#define nvd0_pwr_code gk104_pwr_code
#define nvd0_pwr_data gk104_pwr_data
#include "fuc/nvd0.fuc.h"

static void
gk104_pwr_pgob(struct nouveau_pwr *ppwr, bool enable)
{
	nv_mask(ppwr, 0x000200, 0x00001000, 0x00000000);
	nv_rd32(ppwr, 0x000200);
	nv_mask(ppwr, 0x000200, 0x08000000, 0x08000000);
	msleep(50);

	nv_mask(ppwr, 0x10a78c, 0x00000002, 0x00000002);
	nv_mask(ppwr, 0x10a78c, 0x00000001, 0x00000001);
	nv_mask(ppwr, 0x10a78c, 0x00000001, 0x00000000);

	nv_mask(ppwr, 0x020004, 0xc0000000, enable ? 0xc0000000 : 0x40000000);
	msleep(50);

	nv_mask(ppwr, 0x10a78c, 0x00000002, 0x00000000);
	nv_mask(ppwr, 0x10a78c, 0x00000001, 0x00000001);
	nv_mask(ppwr, 0x10a78c, 0x00000001, 0x00000000);

	nv_mask(ppwr, 0x000200, 0x08000000, 0x00000000);
	nv_mask(ppwr, 0x000200, 0x00001000, 0x00001000);
	nv_rd32(ppwr, 0x000200);
}

struct nouveau_oclass *
gk104_pwr_oclass = &(struct nvkm_pwr_impl) {
	.base.handle = NV_SUBDEV(PWR, 0xe4),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_pwr_ctor,
		.dtor = _nouveau_pwr_dtor,
		.init = _nouveau_pwr_init,
		.fini = _nouveau_pwr_fini,
	},
	.code.data = gk104_pwr_code,
	.code.size = sizeof(gk104_pwr_code),
	.data.data = gk104_pwr_data,
	.data.size = sizeof(gk104_pwr_data),
	.pgob = gk104_pwr_pgob,
}.base;
