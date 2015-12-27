/*
 * Copyright 2012 Red Hat Inc.
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
#include "nv40.h"
#include "regs.h"

#include <subdev/fb.h>
#include <engine/fifo.h>

static void
nv44_gr_tile(struct nvkm_gr *base, int i, struct nvkm_fb_tile *tile)
{
	struct nv40_gr *gr = nv40_gr(base);
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fifo *fifo = device->fifo;
	unsigned long flags;

	nvkm_fifo_pause(fifo, &flags);
	nv04_gr_idle(&gr->base);

	switch (device->chipset) {
	case 0x44:
	case 0x4a:
		nvkm_wr32(device, NV20_PGRAPH_TSIZE(i), tile->pitch);
		nvkm_wr32(device, NV20_PGRAPH_TLIMIT(i), tile->limit);
		nvkm_wr32(device, NV20_PGRAPH_TILE(i), tile->addr);
		break;
	case 0x46:
	case 0x4c:
	case 0x63:
	case 0x67:
	case 0x68:
		nvkm_wr32(device, NV47_PGRAPH_TSIZE(i), tile->pitch);
		nvkm_wr32(device, NV47_PGRAPH_TLIMIT(i), tile->limit);
		nvkm_wr32(device, NV47_PGRAPH_TILE(i), tile->addr);
		nvkm_wr32(device, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nvkm_wr32(device, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nvkm_wr32(device, NV40_PGRAPH_TILE1(i), tile->addr);
		break;
	case 0x4e:
		nvkm_wr32(device, NV20_PGRAPH_TSIZE(i), tile->pitch);
		nvkm_wr32(device, NV20_PGRAPH_TLIMIT(i), tile->limit);
		nvkm_wr32(device, NV20_PGRAPH_TILE(i), tile->addr);
		nvkm_wr32(device, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nvkm_wr32(device, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nvkm_wr32(device, NV40_PGRAPH_TILE1(i), tile->addr);
		break;
	default:
		WARN_ON(1);
		break;
	}

	nvkm_fifo_start(fifo, &flags);
}

static const struct nvkm_gr_func
nv44_gr = {
	.init = nv40_gr_init,
	.intr = nv40_gr_intr,
	.tile = nv44_gr_tile,
	.units = nv40_gr_units,
	.chan_new = nv40_gr_chan_new,
	.sclass = {
		{ -1, -1, 0x0012, &nv40_gr_object }, /* beta1 */
		{ -1, -1, 0x0019, &nv40_gr_object }, /* clip */
		{ -1, -1, 0x0030, &nv40_gr_object }, /* null */
		{ -1, -1, 0x0039, &nv40_gr_object }, /* m2mf */
		{ -1, -1, 0x0043, &nv40_gr_object }, /* rop */
		{ -1, -1, 0x0044, &nv40_gr_object }, /* patt */
		{ -1, -1, 0x004a, &nv40_gr_object }, /* gdi */
		{ -1, -1, 0x0062, &nv40_gr_object }, /* surf2d */
		{ -1, -1, 0x0072, &nv40_gr_object }, /* beta4 */
		{ -1, -1, 0x0089, &nv40_gr_object }, /* sifm */
		{ -1, -1, 0x008a, &nv40_gr_object }, /* ifc */
		{ -1, -1, 0x009f, &nv40_gr_object }, /* imageblit */
		{ -1, -1, 0x3062, &nv40_gr_object }, /* surf2d (nv40) */
		{ -1, -1, 0x3089, &nv40_gr_object }, /* sifm (nv40) */
		{ -1, -1, 0x309e, &nv40_gr_object }, /* swzsurf (nv40) */
		{ -1, -1, 0x4497, &nv40_gr_object }, /* curie */
		{}
	}
};

int
nv44_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return nv40_gr_new_(&nv44_gr, device, index, pgr);
}
