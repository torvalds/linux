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
#include "rootnv50.h"

#include <subdev/bios.h>
#include <subdev/bios/disp.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>
#include <subdev/devinit.h>

void
gf119_disp_vblank_init(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_mask(device, 0x6100c0 + (head * 0x800), 0x00000001, 0x00000001);
}

void
gf119_disp_vblank_fini(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_mask(device, 0x6100c0 + (head * 0x800), 0x00000001, 0x00000000);
}

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
exec_script(struct nv50_disp *disp, int head, int id)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_bios *bios = device->bios;
	struct nvkm_output *outp;
	struct nvbios_outp info;
	u8  ver, hdr, cnt, len;
	u32 data, ctrl = 0;
	int or;

	for (or = 0; !(ctrl & (1 << head)) && or < 8; or++) {
		ctrl = nvkm_rd32(device, 0x640180 + (or * 0x20));
		if (ctrl & (1 << head))
			break;
	}

	if (or == 8)
		return NULL;

	outp = exec_lookup(disp, head, or, ctrl, &data, &ver, &hdr, &cnt, &len, &info);
	if (outp) {
		struct nvbios_init init = {
			.subdev = subdev,
			.bios = bios,
			.offset = info.script[id],
			.outp = &outp->info,
			.crtc = head,
			.execute = 1,
		};

		nvbios_exec(&init);
	}

	return outp;
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
gf119_disp_intr_unk1_0(struct nv50_disp *disp, int head)
{
	exec_script(disp, head, 1);
}

static void
gf119_disp_intr_unk2_0(struct nv50_disp *disp, int head)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_output *outp = exec_script(disp, head, 2);

	/* see note in nv50_disp_intr_unk20_0() */
	if (outp && outp->info.type == DCB_OUTPUT_DP) {
		struct nvkm_output_dp *outpdp = nvkm_output_dp(outp);
		if (!outpdp->lt.mst) {
			struct nvbios_init init = {
				.subdev = subdev,
				.bios = subdev->device->bios,
				.outp = &outp->info,
				.crtc = head,
				.offset = outpdp->info.script[4],
				.execute = 1,
			};

			nvkm_notify_put(&outpdp->irq);
			nvbios_exec(&init);
			atomic_set(&outpdp->lt.done, 0);
		}
	}
}

static void
gf119_disp_intr_unk2_1(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_devinit *devinit = device->devinit;
	u32 pclk = nvkm_rd32(device, 0x660450 + (head * 0x300)) / 1000;
	if (pclk)
		nvkm_devinit_pll_set(devinit, PLL_VPLL0 + head, pclk);
	nvkm_wr32(device, 0x612200 + (head * 0x800), 0x00000000);
}

static void
gf119_disp_intr_unk2_2_tu(struct nv50_disp *disp, int head,
			  struct dcb_output *outp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const int or = ffs(outp->or) - 1;
	const u32 ctrl = nvkm_rd32(device, 0x660200 + (or   * 0x020));
	const u32 conf = nvkm_rd32(device, 0x660404 + (head * 0x300));
	const s32 vactive = nvkm_rd32(device, 0x660414 + (head * 0x300)) & 0xffff;
	const s32 vblanke = nvkm_rd32(device, 0x66041c + (head * 0x300)) & 0xffff;
	const s32 vblanks = nvkm_rd32(device, 0x660420 + (head * 0x300)) & 0xffff;
	const u32 pclk = nvkm_rd32(device, 0x660450 + (head * 0x300)) / 1000;
	const u32 link = ((ctrl & 0xf00) == 0x800) ? 0 : 1;
	const u32 hoff = (head * 0x800);
	const u32 soff = (  or * 0x800);
	const u32 loff = (link * 0x080) + soff;
	const u32 symbol = 100000;
	const u32 TU = 64;
	u32 dpctrl = nvkm_rd32(device, 0x61c10c + loff);
	u32 clksor = nvkm_rd32(device, 0x612300 + soff);
	u32 datarate, link_nr, link_bw, bits;
	u64 ratio, value;

	link_nr  = hweight32(dpctrl & 0x000f0000);
	link_bw  = (clksor & 0x007c0000) >> 18;
	link_bw *= 27000;

	/* symbols/hblank - algorithm taken from comments in tegra driver */
	value = vblanke + vactive - vblanks - 7;
	value = value * link_bw;
	do_div(value, pclk);
	value = value - (3 * !!(dpctrl & 0x00004000)) - (12 / link_nr);
	nvkm_mask(device, 0x616620 + hoff, 0x0000ffff, value);

	/* symbols/vblank - algorithm taken from comments in tegra driver */
	value = vblanks - vblanke - 25;
	value = value * link_bw;
	do_div(value, pclk);
	value = value - ((36 / link_nr) + 3) - 1;
	nvkm_mask(device, 0x616624 + hoff, 0x00ffffff, value);

	/* watermark */
	if      ((conf & 0x3c0) == 0x180) bits = 30;
	else if ((conf & 0x3c0) == 0x140) bits = 24;
	else                              bits = 18;
	datarate = (pclk * bits) / 8;

	ratio  = datarate;
	ratio *= symbol;
	do_div(ratio, link_nr * link_bw);

	value  = (symbol - ratio) * TU;
	value *= ratio;
	do_div(value, symbol);
	do_div(value, symbol);

	value += 5;
	value |= 0x08000000;

	nvkm_wr32(device, 0x616610 + hoff, value);
}

static void
gf119_disp_intr_unk2_2(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_output *outp;
	u32 pclk = nvkm_rd32(device, 0x660450 + (head * 0x300)) / 1000;
	u32 conf, addr, data;

	outp = exec_clkcmp(disp, head, 0xff, pclk, &conf);
	if (!outp)
		return;

	/* see note in nv50_disp_intr_unk20_2() */
	if (outp->info.type == DCB_OUTPUT_DP) {
		u32 sync = nvkm_rd32(device, 0x660404 + (head * 0x300));
		switch ((sync & 0x000003c0) >> 6) {
		case 6: pclk = pclk * 30; break;
		case 5: pclk = pclk * 24; break;
		case 2:
		default:
			pclk = pclk * 18;
			break;
		}

		if (nvkm_output_dp_train(outp, pclk))
			OUTP_ERR(outp, "link not trained before attach");
	} else {
		if (disp->func->sor.magic)
			disp->func->sor.magic(outp);
	}

	exec_clkcmp(disp, head, 0, pclk, &conf);

	if (outp->info.type == DCB_OUTPUT_ANALOG) {
		addr = 0x612280 + (ffs(outp->info.or) - 1) * 0x800;
		data = 0x00000000;
	} else {
		addr = 0x612300 + (ffs(outp->info.or) - 1) * 0x800;
		data = (conf & 0x0100) ? 0x00000101 : 0x00000000;
		switch (outp->info.type) {
		case DCB_OUTPUT_TMDS:
			nvkm_mask(device, addr, 0x007c0000, 0x00280000);
			break;
		case DCB_OUTPUT_DP:
			gf119_disp_intr_unk2_2_tu(disp, head, &outp->info);
			break;
		default:
			break;
		}
	}

	nvkm_mask(device, addr, 0x00000707, data);
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
gf119_disp_intr_supervisor(struct work_struct *work)
{
	struct nv50_disp *disp =
		container_of(work, struct nv50_disp, supervisor);
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask[4];
	int head;

	nvkm_debug(subdev, "supervisor %d\n", ffs(disp->super));
	for (head = 0; head < disp->base.head.nr; head++) {
		mask[head] = nvkm_rd32(device, 0x6101d4 + (head * 0x800));
		nvkm_debug(subdev, "head %d: %08x\n", head, mask[head]);
	}

	if (disp->super & 0x00000001) {
		nv50_disp_chan_mthd(disp->chan[0], NV_DBG_DEBUG);
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(mask[head] & 0x00001000))
				continue;
			nvkm_debug(subdev, "supervisor 1.0 - head %d\n", head);
			gf119_disp_intr_unk1_0(disp, head);
		}
	} else
	if (disp->super & 0x00000002) {
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(mask[head] & 0x00001000))
				continue;
			nvkm_debug(subdev, "supervisor 2.0 - head %d\n", head);
			gf119_disp_intr_unk2_0(disp, head);
		}
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(mask[head] & 0x00010000))
				continue;
			nvkm_debug(subdev, "supervisor 2.1 - head %d\n", head);
			gf119_disp_intr_unk2_1(disp, head);
		}
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(mask[head] & 0x00001000))
				continue;
			nvkm_debug(subdev, "supervisor 2.2 - head %d\n", head);
			gf119_disp_intr_unk2_2(disp, head);
		}
	} else
	if (disp->super & 0x00000004) {
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(mask[head] & 0x00001000))
				continue;
			nvkm_debug(subdev, "supervisor 3.0 - head %d\n", head);
			gf119_disp_intr_unk4_0(disp, head);
		}
	}

	for (head = 0; head < disp->base.head.nr; head++)
		nvkm_wr32(device, 0x6101d4 + (head * 0x800), 0x00000000);
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
	u32 intr = nvkm_rd32(device, 0x610088);
	int i;

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
			schedule_work(&disp->supervisor);
			nvkm_wr32(device, 0x6100ac, disp->super);
			stat &= ~0x00000007;
		}

		if (stat) {
			nvkm_warn(subdev, "intr24 %08x\n", stat);
			nvkm_wr32(device, 0x6100ac, stat);
		}

		intr &= ~0x00100000;
	}

	for (i = 0; i < disp->base.head.nr; i++) {
		u32 mask = 0x01000000 << i;
		if (mask & intr) {
			u32 stat = nvkm_rd32(device, 0x6100bc + (i * 0x800));
			if (stat & 0x00000001)
				nvkm_disp_vblank(&disp->base, i);
			nvkm_mask(device, 0x6100bc + (i * 0x800), 0, 0);
			nvkm_rd32(device, 0x6100c0 + (i * 0x800));
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
	.super = gf119_disp_intr_supervisor,
	.root = &gf119_disp_root_oclass,
	.head.vblank_init = gf119_disp_vblank_init,
	.head.vblank_fini = gf119_disp_vblank_fini,
	.head.scanoutpos = gf119_disp_root_scanoutpos,
	.outp.internal.crt = nv50_dac_output_new,
	.outp.internal.tmds = nv50_sor_output_new,
	.outp.internal.lvds = nv50_sor_output_new,
	.outp.internal.dp = gf119_sor_dp_new,
	.dac.nr = 3,
	.dac.power = nv50_dac_power,
	.dac.sense = nv50_dac_sense,
	.sor.nr = 4,
	.sor.power = nv50_sor_power,
	.sor.hda_eld = gf119_hda_eld,
	.sor.hdmi = gf119_hdmi_ctrl,
};

int
gf119_disp_new(struct nvkm_device *device, int index, struct nvkm_disp **pdisp)
{
	return gf119_disp_new_(&gf119_disp, device, index, pdisp);
}
