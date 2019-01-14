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
#include "channv50.h"
#include "rootnv50.h"

#include <core/client.h>
#include <core/enum.h>
#include <core/ramht.h>
#include <subdev/bios.h>
#include <subdev/bios/disp.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>
#include <subdev/devinit.h>
#include <subdev/timer.h>

static const struct nvkm_disp_oclass *
nv50_disp_root_(struct nvkm_disp *base)
{
	return nv50_disp(base)->func->root;
}

static void
nv50_disp_intr_(struct nvkm_disp *base)
{
	struct nv50_disp *disp = nv50_disp(base);
	disp->func->intr(disp);
}

static void
nv50_disp_fini_(struct nvkm_disp *base)
{
	struct nv50_disp *disp = nv50_disp(base);
	disp->func->fini(disp);
}

static int
nv50_disp_init_(struct nvkm_disp *base)
{
	struct nv50_disp *disp = nv50_disp(base);
	return disp->func->init(disp);
}

static void *
nv50_disp_dtor_(struct nvkm_disp *base)
{
	struct nv50_disp *disp = nv50_disp(base);

	nvkm_ramht_del(&disp->ramht);
	nvkm_gpuobj_del(&disp->inst);

	nvkm_event_fini(&disp->uevent);
	if (disp->wq)
		destroy_workqueue(disp->wq);

	return disp;
}

static int
nv50_disp_oneinit_(struct nvkm_disp *base)
{
	struct nv50_disp *disp = nv50_disp(base);
	const struct nv50_disp_func *func = disp->func;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ret, i;

	if (func->wndw.cnt) {
		disp->wndw.nr = func->wndw.cnt(&disp->base, &disp->wndw.mask);
		nvkm_debug(subdev, "Window(s): %d (%08lx)\n",
			   disp->wndw.nr, disp->wndw.mask);
	}

	disp->head.nr = func->head.cnt(&disp->base, &disp->head.mask);
	nvkm_debug(subdev, "  Head(s): %d (%02lx)\n",
		   disp->head.nr, disp->head.mask);
	for_each_set_bit(i, &disp->head.mask, disp->head.nr) {
		ret = func->head.new(&disp->base, i);
		if (ret)
			return ret;
	}

	if (func->dac.cnt) {
		disp->dac.nr = func->dac.cnt(&disp->base, &disp->dac.mask);
		nvkm_debug(subdev, "   DAC(s): %d (%02lx)\n",
			   disp->dac.nr, disp->dac.mask);
		for_each_set_bit(i, &disp->dac.mask, disp->dac.nr) {
			ret = func->dac.new(&disp->base, i);
			if (ret)
				return ret;
		}
	}

	if (func->pior.cnt) {
		disp->pior.nr = func->pior.cnt(&disp->base, &disp->pior.mask);
		nvkm_debug(subdev, "  PIOR(s): %d (%02lx)\n",
			   disp->pior.nr, disp->pior.mask);
		for_each_set_bit(i, &disp->pior.mask, disp->pior.nr) {
			ret = func->pior.new(&disp->base, i);
			if (ret)
				return ret;
		}
	}

	disp->sor.nr = func->sor.cnt(&disp->base, &disp->sor.mask);
	nvkm_debug(subdev, "   SOR(s): %d (%02lx)\n",
		   disp->sor.nr, disp->sor.mask);
	for_each_set_bit(i, &disp->sor.mask, disp->sor.nr) {
		ret = func->sor.new(&disp->base, i);
		if (ret)
			return ret;
	}

	ret = nvkm_gpuobj_new(device, 0x10000, 0x10000, false, NULL,
			      &disp->inst);
	if (ret)
		return ret;

	return nvkm_ramht_new(device, func->ramht_size ? func->ramht_size :
			      0x1000, 0, disp->inst, &disp->ramht);
}

static const struct nvkm_disp_func
nv50_disp_ = {
	.dtor = nv50_disp_dtor_,
	.oneinit = nv50_disp_oneinit_,
	.init = nv50_disp_init_,
	.fini = nv50_disp_fini_,
	.intr = nv50_disp_intr_,
	.root = nv50_disp_root_,
};

int
nv50_disp_new_(const struct nv50_disp_func *func, struct nvkm_device *device,
	       int index, struct nvkm_disp **pdisp)
{
	struct nv50_disp *disp;
	int ret;

	if (!(disp = kzalloc(sizeof(*disp), GFP_KERNEL)))
		return -ENOMEM;
	disp->func = func;
	*pdisp = &disp->base;

	ret = nvkm_disp_ctor(&nv50_disp_, device, index, &disp->base);
	if (ret)
		return ret;

	disp->wq = create_singlethread_workqueue("nvkm-disp");
	if (!disp->wq)
		return -ENOMEM;

	INIT_WORK(&disp->supervisor, func->super);

	return nvkm_event_init(func->uevent, 1, ARRAY_SIZE(disp->chan),
			       &disp->uevent);
}

static u32
nv50_disp_super_iedt(struct nvkm_head *head, struct nvkm_outp *outp,
		     u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		     struct nvbios_outp *iedt)
{
	struct nvkm_bios *bios = head->disp->engine.subdev.device->bios;
	const u8  l = ffs(outp->info.link);
	const u16 t = outp->info.hasht;
	const u16 m = (0x0100 << head->id) | (l << 6) | outp->info.or;
	u32 data = nvbios_outp_match(bios, t, m, ver, hdr, cnt, len, iedt);
	if (!data)
		OUTP_DBG(outp, "missing IEDT for %04x:%04x", t, m);
	return data;
}

static void
nv50_disp_super_ied_on(struct nvkm_head *head,
		       struct nvkm_ior *ior, int id, u32 khz)
{
	struct nvkm_subdev *subdev = &head->disp->engine.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvkm_outp *outp = ior->asy.outp;
	struct nvbios_ocfg iedtrs;
	struct nvbios_outp iedt;
	u8  ver, hdr, cnt, len, flags = 0x00;
	u32 data;

	if (!outp) {
		IOR_DBG(ior, "nothing to attach");
		return;
	}

	/* Lookup IED table for the device. */
	data = nv50_disp_super_iedt(head, outp, &ver, &hdr, &cnt, &len, &iedt);
	if (!data)
		return;

	/* Lookup IEDT runtime settings for the current configuration. */
	if (ior->type == SOR) {
		if (ior->asy.proto == LVDS) {
			if (head->asy.or.depth == 24)
				flags |= 0x02;
		}
		if (ior->asy.link == 3)
			flags |= 0x01;
	}

	data = nvbios_ocfg_match(bios, data, ior->asy.proto_evo, flags,
				 &ver, &hdr, &cnt, &len, &iedtrs);
	if (!data) {
		OUTP_DBG(outp, "missing IEDT RS for %02x:%02x",
			 ior->asy.proto_evo, flags);
		return;
	}

	/* Execute the OnInt[23] script for the current frequency. */
	data = nvbios_oclk_match(bios, iedtrs.clkcmp[id], khz);
	if (!data) {
		OUTP_DBG(outp, "missing IEDT RSS %d for %02x:%02x %d khz",
			 id, ior->asy.proto_evo, flags, khz);
		return;
	}

	nvbios_init(subdev, data,
		init.outp = &outp->info;
		init.or   = ior->id;
		init.link = ior->asy.link;
		init.head = head->id;
	);
}

static void
nv50_disp_super_ied_off(struct nvkm_head *head, struct nvkm_ior *ior, int id)
{
	struct nvkm_outp *outp = ior->arm.outp;
	struct nvbios_outp iedt;
	u8  ver, hdr, cnt, len;
	u32 data;

	if (!outp) {
		IOR_DBG(ior, "nothing attached");
		return;
	}

	data = nv50_disp_super_iedt(head, outp, &ver, &hdr, &cnt, &len, &iedt);
	if (!data)
		return;

	nvbios_init(&head->disp->engine.subdev, iedt.script[id],
		init.outp = &outp->info;
		init.or   = ior->id;
		init.link = ior->arm.link;
		init.head = head->id;
	);
}

static struct nvkm_ior *
nv50_disp_super_ior_asy(struct nvkm_head *head)
{
	struct nvkm_ior *ior;
	list_for_each_entry(ior, &head->disp->ior, head) {
		if (ior->asy.head & (1 << head->id)) {
			HEAD_DBG(head, "to %s", ior->name);
			return ior;
		}
	}
	HEAD_DBG(head, "nothing to attach");
	return NULL;
}

static struct nvkm_ior *
nv50_disp_super_ior_arm(struct nvkm_head *head)
{
	struct nvkm_ior *ior;
	list_for_each_entry(ior, &head->disp->ior, head) {
		if (ior->arm.head & (1 << head->id)) {
			HEAD_DBG(head, "on %s", ior->name);
			return ior;
		}
	}
	HEAD_DBG(head, "nothing attached");
	return NULL;
}

void
nv50_disp_super_3_0(struct nv50_disp *disp, struct nvkm_head *head)
{
	struct nvkm_ior *ior;

	/* Determine which OR, if any, we're attaching to the head. */
	HEAD_DBG(head, "supervisor 3.0");
	ior = nv50_disp_super_ior_asy(head);
	if (!ior)
		return;

	/* Execute OnInt3 IED script. */
	nv50_disp_super_ied_on(head, ior, 1, head->asy.hz / 1000);

	/* OR-specific handling. */
	if (ior->func->war_3)
		ior->func->war_3(ior);
}

static void
nv50_disp_super_2_2_dp(struct nvkm_head *head, struct nvkm_ior *ior)
{
	struct nvkm_subdev *subdev = &head->disp->engine.subdev;
	const u32      khz = head->asy.hz / 1000;
	const u32 linkKBps = ior->dp.bw * 27000;
	const u32   symbol = 100000;
	int bestTU = 0, bestVTUi = 0, bestVTUf = 0, bestVTUa = 0;
	int TU, VTUi, VTUf, VTUa;
	u64 link_data_rate, link_ratio, unk;
	u32 best_diff = 64 * symbol;
	u64 h, v;

	/* symbols/hblank - algorithm taken from comments in tegra driver */
	h = head->asy.hblanke + head->asy.htotal - head->asy.hblanks - 7;
	h = h * linkKBps;
	do_div(h, khz);
	h = h - (3 * ior->dp.ef) - (12 / ior->dp.nr);

	/* symbols/vblank - algorithm taken from comments in tegra driver */
	v = head->asy.vblanks - head->asy.vblanke - 25;
	v = v * linkKBps;
	do_div(v, khz);
	v = v - ((36 / ior->dp.nr) + 3) - 1;

	ior->func->dp.audio_sym(ior, head->id, h, v);

	/* watermark / activesym */
	link_data_rate = (khz * head->asy.or.depth / 8) / ior->dp.nr;

	/* calculate ratio of packed data rate to link symbol rate */
	link_ratio = link_data_rate * symbol;
	do_div(link_ratio, linkKBps);

	for (TU = 64; ior->func->dp.activesym && TU >= 32; TU--) {
		/* calculate average number of valid symbols in each TU */
		u32 tu_valid = link_ratio * TU;
		u32 calc, diff;

		/* find a hw representation for the fraction.. */
		VTUi = tu_valid / symbol;
		calc = VTUi * symbol;
		diff = tu_valid - calc;
		if (diff) {
			if (diff >= (symbol / 2)) {
				VTUf = symbol / (symbol - diff);
				if (symbol - (VTUf * diff))
					VTUf++;

				if (VTUf <= 15) {
					VTUa  = 1;
					calc += symbol - (symbol / VTUf);
				} else {
					VTUa  = 0;
					VTUf  = 1;
					calc += symbol;
				}
			} else {
				VTUa  = 0;
				VTUf  = min((int)(symbol / diff), 15);
				calc += symbol / VTUf;
			}

			diff = calc - tu_valid;
		} else {
			/* no remainder, but the hw doesn't like the fractional
			 * part to be zero.  decrement the integer part and
			 * have the fraction add a whole symbol back
			 */
			VTUa = 0;
			VTUf = 1;
			VTUi--;
		}

		if (diff < best_diff) {
			best_diff = diff;
			bestTU = TU;
			bestVTUa = VTUa;
			bestVTUf = VTUf;
			bestVTUi = VTUi;
			if (diff == 0)
				break;
		}
	}

	if (ior->func->dp.activesym) {
		if (!bestTU) {
			nvkm_error(subdev, "unable to determine dp config\n");
			return;
		}
		ior->func->dp.activesym(ior, head->id, bestTU,
					bestVTUa, bestVTUf, bestVTUi);
	} else {
		bestTU = 64;
	}

	/* XXX close to vbios numbers, but not right */
	unk  = (symbol - link_ratio) * bestTU;
	unk *= link_ratio;
	do_div(unk, symbol);
	do_div(unk, symbol);
	unk += 6;

	ior->func->dp.watermark(ior, head->id, unk);
}

void
nv50_disp_super_2_2(struct nv50_disp *disp, struct nvkm_head *head)
{
	const u32 khz = head->asy.hz / 1000;
	struct nvkm_outp *outp;
	struct nvkm_ior *ior;

	/* Determine which OR, if any, we're attaching from the head. */
	HEAD_DBG(head, "supervisor 2.2");
	ior = nv50_disp_super_ior_asy(head);
	if (!ior)
		return;

	/* For some reason, NVIDIA decided not to:
	 *
	 * A) Give dual-link LVDS a separate EVO protocol, like for TMDS.
	 *  and
	 * B) Use SetControlOutputResource.PixelDepth on LVDS.
	 *
	 * Override the values we usually read from HW with the same
	 * data we pass though an ioctl instead.
	 */
	if (ior->type == SOR && ior->asy.proto == LVDS) {
		head->asy.or.depth = (disp->sor.lvdsconf & 0x0200) ? 24 : 18;
		ior->asy.link      = (disp->sor.lvdsconf & 0x0100) ? 3  : 1;
	}

	/* Handle any link training, etc. */
	if ((outp = ior->asy.outp) && outp->func->acquire)
		outp->func->acquire(outp);

	/* Execute OnInt2 IED script. */
	nv50_disp_super_ied_on(head, ior, 0, khz);

	/* Program RG clock divider. */
	head->func->rgclk(head, ior->asy.rgdiv);

	/* Mode-specific internal DP configuration. */
	if (ior->type == SOR && ior->asy.proto == DP)
		nv50_disp_super_2_2_dp(head, ior);

	/* OR-specific handling. */
	ior->func->clock(ior);
	if (ior->func->war_2)
		ior->func->war_2(ior);
}

void
nv50_disp_super_2_1(struct nv50_disp *disp, struct nvkm_head *head)
{
	struct nvkm_devinit *devinit = disp->base.engine.subdev.device->devinit;
	const u32 khz = head->asy.hz / 1000;
	HEAD_DBG(head, "supervisor 2.1 - %d khz", khz);
	if (khz)
		nvkm_devinit_pll_set(devinit, PLL_VPLL0 + head->id, khz);
}

void
nv50_disp_super_2_0(struct nv50_disp *disp, struct nvkm_head *head)
{
	struct nvkm_outp *outp;
	struct nvkm_ior *ior;

	/* Determine which OR, if any, we're detaching from the head. */
	HEAD_DBG(head, "supervisor 2.0");
	ior = nv50_disp_super_ior_arm(head);
	if (!ior)
		return;

	/* Execute OffInt2 IED script. */
	nv50_disp_super_ied_off(head, ior, 2);

	/* If we're shutting down the OR's only active head, execute
	 * the output path's disable function.
	 */
	if (ior->arm.head == (1 << head->id)) {
		if ((outp = ior->arm.outp) && outp->func->disable)
			outp->func->disable(outp, ior);
	}
}

void
nv50_disp_super_1_0(struct nv50_disp *disp, struct nvkm_head *head)
{
	struct nvkm_ior *ior;

	/* Determine which OR, if any, we're detaching from the head. */
	HEAD_DBG(head, "supervisor 1.0");
	ior = nv50_disp_super_ior_arm(head);
	if (!ior)
		return;

	/* Execute OffInt1 IED script. */
	nv50_disp_super_ied_off(head, ior, 1);
}

void
nv50_disp_super_1(struct nv50_disp *disp)
{
	struct nvkm_head *head;
	struct nvkm_ior *ior;

	list_for_each_entry(head, &disp->base.head, head) {
		head->func->state(head, &head->arm);
		head->func->state(head, &head->asy);
	}

	list_for_each_entry(ior, &disp->base.ior, head) {
		ior->func->state(ior, &ior->arm);
		ior->func->state(ior, &ior->asy);
	}
}

void
nv50_disp_super(struct work_struct *work)
{
	struct nv50_disp *disp =
		container_of(work, struct nv50_disp, supervisor);
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_head *head;
	u32 super = nvkm_rd32(device, 0x610030);

	nvkm_debug(subdev, "supervisor %08x %08x\n", disp->super, super);

	if (disp->super & 0x00000010) {
		nv50_disp_chan_mthd(disp->chan[0], NV_DBG_DEBUG);
		nv50_disp_super_1(disp);
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(super & (0x00000020 << head->id)))
				continue;
			if (!(super & (0x00000080 << head->id)))
				continue;
			nv50_disp_super_1_0(disp, head);
		}
	} else
	if (disp->super & 0x00000020) {
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(super & (0x00000080 << head->id)))
				continue;
			nv50_disp_super_2_0(disp, head);
		}
		nvkm_outp_route(&disp->base);
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(super & (0x00000200 << head->id)))
				continue;
			nv50_disp_super_2_1(disp, head);
		}
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(super & (0x00000080 << head->id)))
				continue;
			nv50_disp_super_2_2(disp, head);
		}
	} else
	if (disp->super & 0x00000040) {
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(super & (0x00000080 << head->id)))
				continue;
			nv50_disp_super_3_0(disp, head);
		}
	}

	nvkm_wr32(device, 0x610030, 0x80000000);
}

static const struct nvkm_enum
nv50_disp_intr_error_type[] = {
	{ 3, "ILLEGAL_MTHD" },
	{ 4, "INVALID_VALUE" },
	{ 5, "INVALID_STATE" },
	{ 7, "INVALID_HANDLE" },
	{}
};

static const struct nvkm_enum
nv50_disp_intr_error_code[] = {
	{ 0x00, "" },
	{}
};

static void
nv50_disp_intr_error(struct nv50_disp *disp, int chid)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 data = nvkm_rd32(device, 0x610084 + (chid * 0x08));
	u32 addr = nvkm_rd32(device, 0x610080 + (chid * 0x08));
	u32 code = (addr & 0x00ff0000) >> 16;
	u32 type = (addr & 0x00007000) >> 12;
	u32 mthd = (addr & 0x00000ffc);
	const struct nvkm_enum *ec, *et;

	et = nvkm_enum_find(nv50_disp_intr_error_type, type);
	ec = nvkm_enum_find(nv50_disp_intr_error_code, code);

	nvkm_error(subdev,
		   "ERROR %d [%s] %02x [%s] chid %d mthd %04x data %08x\n",
		   type, et ? et->name : "", code, ec ? ec->name : "",
		   chid, mthd, data);

	if (chid < ARRAY_SIZE(disp->chan)) {
		switch (mthd) {
		case 0x0080:
			nv50_disp_chan_mthd(disp->chan[chid], NV_DBG_ERROR);
			break;
		default:
			break;
		}
	}

	nvkm_wr32(device, 0x610020, 0x00010000 << chid);
	nvkm_wr32(device, 0x610080 + (chid * 0x08), 0x90000000);
}

void
nv50_disp_intr(struct nv50_disp *disp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	u32 intr0 = nvkm_rd32(device, 0x610020);
	u32 intr1 = nvkm_rd32(device, 0x610024);

	while (intr0 & 0x001f0000) {
		u32 chid = __ffs(intr0 & 0x001f0000) - 16;
		nv50_disp_intr_error(disp, chid);
		intr0 &= ~(0x00010000 << chid);
	}

	while (intr0 & 0x0000001f) {
		u32 chid = __ffs(intr0 & 0x0000001f);
		nv50_disp_chan_uevent_send(disp, chid);
		intr0 &= ~(0x00000001 << chid);
	}

	if (intr1 & 0x00000004) {
		nvkm_disp_vblank(&disp->base, 0);
		nvkm_wr32(device, 0x610024, 0x00000004);
	}

	if (intr1 & 0x00000008) {
		nvkm_disp_vblank(&disp->base, 1);
		nvkm_wr32(device, 0x610024, 0x00000008);
	}

	if (intr1 & 0x00000070) {
		disp->super = (intr1 & 0x00000070);
		queue_work(disp->wq, &disp->supervisor);
		nvkm_wr32(device, 0x610024, disp->super);
	}
}

void
nv50_disp_fini(struct nv50_disp *disp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	/* disable all interrupts */
	nvkm_wr32(device, 0x610024, 0x00000000);
	nvkm_wr32(device, 0x610020, 0x00000000);
}

int
nv50_disp_init(struct nv50_disp *disp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_head *head;
	u32 tmp;
	int i;

	/* The below segments of code copying values from one register to
	 * another appear to inform EVO of the display capabilities or
	 * something similar.  NFI what the 0x614004 caps are for..
	 */
	tmp = nvkm_rd32(device, 0x614004);
	nvkm_wr32(device, 0x610184, tmp);

	/* ... CRTC caps */
	list_for_each_entry(head, &disp->base.head, head) {
		tmp = nvkm_rd32(device, 0x616100 + (head->id * 0x800));
		nvkm_wr32(device, 0x610190 + (head->id * 0x10), tmp);
		tmp = nvkm_rd32(device, 0x616104 + (head->id * 0x800));
		nvkm_wr32(device, 0x610194 + (head->id * 0x10), tmp);
		tmp = nvkm_rd32(device, 0x616108 + (head->id * 0x800));
		nvkm_wr32(device, 0x610198 + (head->id * 0x10), tmp);
		tmp = nvkm_rd32(device, 0x61610c + (head->id * 0x800));
		nvkm_wr32(device, 0x61019c + (head->id * 0x10), tmp);
	}

	/* ... DAC caps */
	for (i = 0; i < disp->dac.nr; i++) {
		tmp = nvkm_rd32(device, 0x61a000 + (i * 0x800));
		nvkm_wr32(device, 0x6101d0 + (i * 0x04), tmp);
	}

	/* ... SOR caps */
	for (i = 0; i < disp->sor.nr; i++) {
		tmp = nvkm_rd32(device, 0x61c000 + (i * 0x800));
		nvkm_wr32(device, 0x6101e0 + (i * 0x04), tmp);
	}

	/* ... PIOR caps */
	for (i = 0; i < disp->pior.nr; i++) {
		tmp = nvkm_rd32(device, 0x61e000 + (i * 0x800));
		nvkm_wr32(device, 0x6101f0 + (i * 0x04), tmp);
	}

	/* steal display away from vbios, or something like that */
	if (nvkm_rd32(device, 0x610024) & 0x00000100) {
		nvkm_wr32(device, 0x610024, 0x00000100);
		nvkm_mask(device, 0x6194e8, 0x00000001, 0x00000000);
		if (nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x6194e8) & 0x00000002))
				break;
		) < 0)
			return -EBUSY;
	}

	/* point at display engine memory area (hash table, objects) */
	nvkm_wr32(device, 0x610010, (disp->inst->addr >> 8) | 9);

	/* enable supervisor interrupts, disable everything else */
	nvkm_wr32(device, 0x61002c, 0x00000370);
	nvkm_wr32(device, 0x610028, 0x00000000);
	return 0;
}

static const struct nv50_disp_func
nv50_disp = {
	.init = nv50_disp_init,
	.fini = nv50_disp_fini,
	.intr = nv50_disp_intr,
	.uevent = &nv50_disp_chan_uevent,
	.super = nv50_disp_super,
	.root = &nv50_disp_root_oclass,
	.head = { .cnt = nv50_head_cnt, .new = nv50_head_new },
	.dac = { .cnt = nv50_dac_cnt, .new = nv50_dac_new },
	.sor = { .cnt = nv50_sor_cnt, .new = nv50_sor_new },
	.pior = { .cnt = nv50_pior_cnt, .new = nv50_pior_new },
};

int
nv50_disp_new(struct nvkm_device *device, int index, struct nvkm_disp **pdisp)
{
	return nv50_disp_new_(&nv50_disp, device, index, pdisp);
}
