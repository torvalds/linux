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
#include "nv50.h"

#include <subdev/timer.h>

static const struct nvkm_bitfield nv50_gr_status[] = {
	{ 0x00000001, "BUSY" }, /* set when any bit is set */
	{ 0x00000002, "DISPATCH" },
	{ 0x00000004, "UNK2" },
	{ 0x00000008, "UNK3" },
	{ 0x00000010, "UNK4" },
	{ 0x00000020, "UNK5" },
	{ 0x00000040, "M2MF" },
	{ 0x00000080, "UNK7" },
	{ 0x00000100, "CTXPROG" },
	{ 0x00000200, "VFETCH" },
	{ 0x00000400, "CCACHE_PREGEOM" },
	{ 0x00000800, "STRMOUT_VATTR_POSTGEOM" },
	{ 0x00001000, "VCLIP" },
	{ 0x00002000, "RATTR_APLANE" },
	{ 0x00004000, "TRAST" },
	{ 0x00008000, "CLIPID" },
	{ 0x00010000, "ZCULL" },
	{ 0x00020000, "ENG2D" },
	{ 0x00040000, "RMASK" },
	{ 0x00080000, "TPC_RAST" },
	{ 0x00100000, "TPC_PROP" },
	{ 0x00200000, "TPC_TEX" },
	{ 0x00400000, "TPC_GEOM" },
	{ 0x00800000, "TPC_MP" },
	{ 0x01000000, "ROP" },
	{}
};

static const struct nvkm_bitfield
nv50_gr_vstatus_0[] = {
	{ 0x01, "VFETCH" },
	{ 0x02, "CCACHE" },
	{ 0x04, "PREGEOM" },
	{ 0x08, "POSTGEOM" },
	{ 0x10, "VATTR" },
	{ 0x20, "STRMOUT" },
	{ 0x40, "VCLIP" },
	{}
};

static const struct nvkm_bitfield
nv50_gr_vstatus_1[] = {
	{ 0x01, "TPC_RAST" },
	{ 0x02, "TPC_PROP" },
	{ 0x04, "TPC_TEX" },
	{ 0x08, "TPC_GEOM" },
	{ 0x10, "TPC_MP" },
	{}
};

static const struct nvkm_bitfield
nv50_gr_vstatus_2[] = {
	{ 0x01, "RATTR" },
	{ 0x02, "APLANE" },
	{ 0x04, "TRAST" },
	{ 0x08, "CLIPID" },
	{ 0x10, "ZCULL" },
	{ 0x20, "ENG2D" },
	{ 0x40, "RMASK" },
	{ 0x80, "ROP" },
	{}
};

static void
nvkm_gr_vstatus_print(struct nv50_gr *gr, int r,
		      const struct nvkm_bitfield *units, u32 status)
{
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	u32 stat = status;
	u8  mask = 0x00;
	char msg[64];
	int i;

	for (i = 0; units[i].name && status; i++) {
		if ((status & 7) == 1)
			mask |= (1 << i);
		status >>= 3;
	}

	nvkm_snprintbf(msg, sizeof(msg), units, mask);
	nvkm_error(subdev, "PGRAPH_VSTATUS%d: %08x [%s]\n", r, stat, msg);
}

int
g84_gr_tlb_flush(struct nvkm_gr *base)
{
	struct nv50_gr *gr = nv50_gr(base);
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_timer *tmr = device->timer;
	bool idle, timeout = false;
	unsigned long flags;
	char status[128];
	u64 start;
	u32 tmp;

	spin_lock_irqsave(&gr->lock, flags);
	nvkm_mask(device, 0x400500, 0x00000001, 0x00000000);

	start = nvkm_timer_read(tmr);
	do {
		idle = true;

		for (tmp = nvkm_rd32(device, 0x400380); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nvkm_rd32(device, 0x400384); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nvkm_rd32(device, 0x400388); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}
	} while (!idle &&
		 !(timeout = nvkm_timer_read(tmr) - start > 2000000000));

	if (timeout) {
		nvkm_error(subdev, "PGRAPH TLB flush idle timeout fail\n");

		tmp = nvkm_rd32(device, 0x400700);
		nvkm_snprintbf(status, sizeof(status), nv50_gr_status, tmp);
		nvkm_error(subdev, "PGRAPH_STATUS %08x [%s]\n", tmp, status);

		nvkm_gr_vstatus_print(gr, 0, nv50_gr_vstatus_0,
				       nvkm_rd32(device, 0x400380));
		nvkm_gr_vstatus_print(gr, 1, nv50_gr_vstatus_1,
				       nvkm_rd32(device, 0x400384));
		nvkm_gr_vstatus_print(gr, 2, nv50_gr_vstatus_2,
				       nvkm_rd32(device, 0x400388));
	}


	nvkm_wr32(device, 0x100c80, 0x00000001);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x100c80) & 0x00000001))
			break;
	);
	nvkm_mask(device, 0x400500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&gr->lock, flags);
	return timeout ? -EBUSY : 0;
}

static const struct nvkm_gr_func
g84_gr = {
	.init = nv50_gr_init,
	.intr = nv50_gr_intr,
	.chan_new = nv50_gr_chan_new,
	.tlb_flush = g84_gr_tlb_flush,
	.units = nv50_gr_units,
	.sclass = {
		{ -1, -1, 0x0030, &nv50_gr_object },
		{ -1, -1, 0x502d, &nv50_gr_object },
		{ -1, -1, 0x5039, &nv50_gr_object },
		{ -1, -1, 0x50c0, &nv50_gr_object },
		{ -1, -1, 0x8297, &nv50_gr_object },
		{}
	}
};

int
g84_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return nv50_gr_new_(&g84_gr, device, index, pgr);
}
