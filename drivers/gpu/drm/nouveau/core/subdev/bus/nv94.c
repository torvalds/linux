/*
 * Copyright 2012 Nouveau Community
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
 * Authors: Martin Peres <martin.peres@labri.fr>
 *          Ben Skeggs
 */

#include <subdev/timer.h>

#include "nv04.h"

static int
nv94_bus_hwsq_exec(struct nouveau_bus *pbus, u32 *data, u32 size)
{
	struct nv50_bus_priv *priv = (void *)pbus;
	int i;

	nv_mask(pbus, 0x001098, 0x00000008, 0x00000000);
	nv_wr32(pbus, 0x001304, 0x00000000);
	nv_wr32(pbus, 0x001318, 0x00000000);
	for (i = 0; i < size; i++)
		nv_wr32(priv, 0x080000 + (i * 4), data[i]);
	nv_mask(pbus, 0x001098, 0x00000018, 0x00000018);
	nv_wr32(pbus, 0x00130c, 0x00000001);

	return nv_wait(pbus, 0x001308, 0x00000100, 0x00000000) ? 0 : -ETIMEDOUT;
}

struct nouveau_oclass *
nv94_bus_oclass = &(struct nv04_bus_impl) {
	.base.handle = NV_SUBDEV(BUS, 0x94),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_bus_ctor,
		.dtor = _nouveau_bus_dtor,
		.init = nv50_bus_init,
		.fini = _nouveau_bus_fini,
	},
	.intr = nv50_bus_intr,
	.hwsq_exec = nv94_bus_hwsq_exec,
	.hwsq_size = 128,
}.base;
