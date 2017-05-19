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

#include <subdev/bios.h>
#include <subdev/bios/disp.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>
#include <subdev/devinit.h>

static struct nvkm_output *
exec_lookup(struct nv50_disp *disp, int head, int or, u32 ctrl,
	    u32 *data, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
	    struct nvbios_outp *info)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvkm_output *outp;
	u16 mask, type;

	if (or < 4) {
		type = DCB_OUTPUT_ANALOG;
		mask = 0;
	} else {
		or -= 4;
		switch (ctrl & 0x00000f00) {
		case 0x00000000: type = DCB_OUTPUT_LVDS; mask = 1; break;
		case 0x00000100: type = DCB_OUTPUT_TMDS; mask = 1; break;
		case 0x00000200: type = DCB_OUTPUT_TMDS; mask = 2; break;
		case 0x00000500: type = DCB_OUTPUT_TMDS; mask = 3; break;
		case 0x00000800: type = DCB_OUTPUT_DP; mask = 1; break;
		case 0x00000900: type = DCB_OUTPUT_DP; mask = 2; break;
		default:
			nvkm_error(subdev, "unknown SOR mc %08x\n", ctrl);
			return NULL;
		}
	}

	mask  = 0x00c0 & (mask << 6);
	mask |= 0x0001 << or;
	mask |= 0x0100 << head;

	list_for_each_entry(outp, &disp->base.outp, head) {
		if ((outp->info.hasht & 0xff) == type &&
		    (outp->info.hashm & mask) == mask) {
			*data = nvbios_outp_match(bios, outp->info.hasht, mask,
						  ver, hdr, cnt, len, info);
			if (!*data)
				return NULL;
			return outp;
		}
	}

	return NULL;
}

static struct nvkm_output *
exec_clkcmp(struct nv50_disp *disp, int head, int id, u32 pclk, u32 *conf)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_bios *bios = device->bios;
	struct nvkm_output *outp;
	struct nvbios_outp info1;
	struct nvbios_ocfg info2;
	u8  ver, hdr, cnt, len;
	u32 data, ctrl = 0;
	int or;

	for (or = 0; !(ctrl & (1 << head)) && or < 8; or++) {
		ctrl = nvkm_rd32(device, 0x660180 + (or * 0x20));
		if (ctrl & (1 << head))
			break;
	}

	if (or == 8)
		return NULL;

	outp = exec_lookup(disp, head, or, ctrl, &data, &ver, &hdr, &cnt, &len, &info1);
	if (!outp)
		return NULL;

	*conf = (ctrl & 0x00000f00) >> 8;
	switch (outp->info.type) {
	case DCB_OUTPUT_TMDS:
		if (*conf == 5)
			*conf |= 0x0100;
		break;
	case DCB_OUTPUT_LVDS:
		*conf |= disp->sor.lvdsconf;
		break;
	default:
		break;
	}

	data = nvbios_ocfg_match(bios, data, *conf & 0xff, *conf >> 8,
				 &ver, &hdr, &cnt, &len, &info2);
	if (data && id < 0xff) {
		data = nvbios_oclk_match(bios, info2.clkcmp[id], pclk);
		if (data) {
			struct nvbios_init init = {
				.subdev = subdev,
				.bios = bios,
				.offset = data,
				.outp = &outp->info,
				.crtc = head,
				.execute = 1,
			};

			nvbios_exec(&init);
		}
	}

	return outp;
}

static void
gf119_disp_intr_unk4_0(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	u32 pclk = nvkm_rd32(device, 0x660450 + (head * 0x300)) / 1000;
	u32 conf;

	exec_clkcmp(disp, head, 1, pclk, &conf);
}

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
			nvkm_debug(subdev, "supervisor 3.0 - head %d\n", head->id);
			gf119_disp_intr_unk4_0(disp, head->id);
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
