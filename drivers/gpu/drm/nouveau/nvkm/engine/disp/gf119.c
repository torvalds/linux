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
#include "head.h"
#include "ior.h"
#include "rootnv50.h"

void
gf119_disp_super(struct work_struct *work)
{
	struct nv50_disp *disp =
		container_of(work, struct nv50_disp, supervisor);
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_head *head;
	u32 mask[4];

	nvkm_debug(subdev, "supervisor %d\n", ffs(disp->super));
	list_for_each_entry(head, &disp->base.head, head) {
		mask[head->id] = nvkm_rd32(device, 0x6101d4 + (head->id * 0x800));
		HEAD_DBG(head, "%08x", mask[head->id]);
	}

	if (disp->super & 0x00000001) {
		nv50_disp_chan_mthd(disp->chan[0], NV_DBG_DEBUG);
		nv50_disp_super_1(disp);
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_1_0(disp, head);
		}
	} else
	if (disp->super & 0x00000002) {
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_2_0(disp, head);
		}
		nvkm_outp_route(&disp->base);
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00010000))
				continue;
			nv50_disp_super_2_1(disp, head);
		}
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_2_2(disp, head);
		}
	} else
	if (disp->super & 0x00000004) {
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_3_0(disp, head);
		}
	}

	list_for_each_entry(head, &disp->base.head, head)
		nvkm_wr32(device, 0x6101d4 + (head->id * 0x800), 0x00000000);
	nvkm_wr32(device, 0x6101d0, 0x80000000);
}

void
gf119_disp_intr_error(struct nv50_disp *disp, int chid)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mthd = nvkm_rd32(device, 0x6101f0 + (chid * 12));
	u32 data = nvkm_rd32(device, 0x6101f4 + (chid * 12));
	u32 unkn = nvkm_rd32(device, 0x6101f8 + (chid * 12));

	nvkm_error(subdev, "chid %d mthd %04x data %08x %08x %08x\n",
		   chid, (mthd & 0x0000ffc), data, mthd, unkn);

	if (chid < ARRAY_SIZE(disp->chan)) {
		switch (mthd & 0xffc) {
		case 0x0080:
			nv50_disp_chan_mthd(disp->chan[chid], NV_DBG_ERROR);
			break;
		default:
			break;
		}
	}

	nvkm_wr32(device, 0x61009c, (1 << chid));
	nvkm_wr32(device, 0x6101f0 + (chid * 12), 0x90000000);
}

void
gf119_disp_intr(struct nv50_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_head *head;
	u32 intr = nvkm_rd32(device, 0x610088);

	if (intr & 0x00000001) {
		u32 stat = nvkm_rd32(device, 0x61008c);
		while (stat) {
			int chid = __ffs(stat); stat &= ~(1 << chid);
			nv50_disp_chan_uevent_send(disp, chid);
			nvkm_wr32(device, 0x61008c, 1 << chid);
		}
		intr &= ~0x00000001;
	}

	if (intr & 0x00000002) {
		u32 stat = nvkm_rd32(device, 0x61009c);
		int chid = ffs(stat) - 1;
		if (chid >= 0)
			disp->func->intr_error(disp, chid);
		intr &= ~0x00000002;
	}

	if (intr & 0x00100000) {
		u32 stat = nvkm_rd32(device, 0x6100ac);
		if (stat & 0x00000007) {
			disp->super = (stat & 0x00000007);
			queue_work(disp->wq, &disp->supervisor);
			nvkm_wr32(device, 0x6100ac, disp->super);
			stat &= ~0x00000007;
		}

		if (stat) {
			nvkm_warn(subdev, "intr24 %08x\n", stat);
			nvkm_wr32(device, 0x6100ac, stat);
		}

		intr &= ~0x00100000;
	}

	list_for_each_entry(head, &disp->base.head, head) {
		const u32 hoff = head->id * 0x800;
		u32 mask = 0x01000000 << head->id;
		if (mask & intr) {
			u32 stat = nvkm_rd32(device, 0x6100bc + hoff);
			if (stat & 0x00000001)
				nvkm_disp_vblank(&disp->base, head->id);
			nvkm_mask(device, 0x6100bc + hoff, 0, 0);
			nvkm_rd32(device, 0x6100c0 + hoff);
		}
	}
}

int
gf119_disp_new_(const struct nv50_disp_func *func, struct nvkm_device *device,
		int index, struct nvkm_disp **pdisp)
{
	u32 heads = nvkm_rd32(device, 0x022448);
	return nv50_disp_new_(func, device, index, heads, pdisp);
}

static const struct nv50_disp_func
gf119_disp = {
	.intr = gf119_disp_intr,
	.intr_error = gf119_disp_intr_error,
	.uevent = &gf119_disp_chan_uevent,
	.super = gf119_disp_super,
	.root = &gf119_disp_root_oclass,
	.head.new = gf119_head_new,
	.dac = { .nr = 3, .new = gf119_dac_new },
	.sor = { .nr = 4, .new = gf119_sor_new },
};

int
gf119_disp_new(struct nvkm_device *device, int index, struct nvkm_disp **pdisp)
{
	return gf119_disp_new_(&gf119_disp, device, index, pdisp);
}
