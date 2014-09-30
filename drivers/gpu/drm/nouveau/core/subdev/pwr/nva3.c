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
#include "fuc/nva3.fuc.h"

static int
nva3_pwr_init(struct nouveau_object *object)
{
	struct nouveau_pwr *ppwr = (void *)object;
	nv_mask(ppwr, 0x022210, 0x00000001, 0x00000000);
	nv_mask(ppwr, 0x022210, 0x00000001, 0x00000001);
	return nouveau_pwr_init(ppwr);
}

struct nouveau_oclass *
nva3_pwr_oclass = &(struct nvkm_pwr_impl) {
	.base.handle = NV_SUBDEV(PWR, 0xa3),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_pwr_ctor,
		.dtor = _nouveau_pwr_dtor,
		.init = nva3_pwr_init,
		.fini = _nouveau_pwr_fini,
	},
	.code.data = nva3_pwr_code,
	.code.size = sizeof(nva3_pwr_code),
	.data.data = nva3_pwr_data,
	.data.size = sizeof(nva3_pwr_data),
}.base;
