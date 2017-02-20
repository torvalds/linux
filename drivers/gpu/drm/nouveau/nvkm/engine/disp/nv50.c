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

#include <core/client.h>
#include <core/enum.h>
#include <core/gpuobj.h>
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

static int
nv50_disp_outp_internal_crt_(struct nvkm_disp *base, int index,
			     struct dcb_output *dcb, struct nvkm_output **poutp)
{
	struct nv50_disp *disp = nv50_disp(base);
	return disp->func->outp.internal.crt(base, index, dcb, poutp);
}

static int
nv50_disp_outp_internal_tmds_(struct nvkm_disp *base, int index,
			      struct dcb_output *dcb,
			      struct nvkm_output **poutp)
{
	struct nv50_disp *disp = nv50_disp(base);
	return disp->func->outp.internal.tmds(base, index, dcb, poutp);
}

static int
nv50_disp_outp_internal_lvds_(struct nvkm_disp *base, int index,
			      struct dcb_output *dcb,
			      struct nvkm_output **poutp)
{
	struct nv50_disp *disp = nv50_disp(base);
	return disp->func->outp.internal.lvds(base, index, dcb, poutp);
}

static int
nv50_disp_outp_internal_dp_(struct nvkm_disp *base, int index,
			    struct dcb_output *dcb, struct nvkm_output **poutp)
{
	struct nv50_disp *disp = nv50_disp(base);
	if (disp->func->outp.internal.dp)
		return disp->func->outp.internal.dp(base, index, dcb, poutp);
	return -ENODEV;
}

static int
nv50_disp_outp_external_tmds_(struct nvkm_disp *base, int index,
			      struct dcb_output *dcb,
			      struct nvkm_output **poutp)
{
	struct nv50_disp *disp = nv50_disp(base);
	if (disp->func->outp.external.tmds)
		return disp->func->outp.external.tmds(base, index, dcb, poutp);
	return -ENODEV;
}

static int
nv50_disp_outp_external_dp_(struct nvkm_disp *base, int index,
			    struct dcb_output *dcb, struct nvkm_output **poutp)
{
	struct nv50_disp *disp = nv50_disp(base);
	if (disp->func->outp.external.dp)
		return disp->func->outp.external.dp(base, index, dcb, poutp);
	return -ENODEV;
}

static void
nv50_disp_vblank_fini_(struct nvkm_disp *base, int head)
{
	struct nv50_disp *disp = nv50_disp(base);
	disp->func->head.vblank_fini(disp, head);
}

static void
nv50_disp_vblank_init_(struct nvkm_disp *base, int head)
{
	struct nv50_disp *disp = nv50_disp(base);
	disp->func->head.vblank_init(disp, head);
}

static void
nv50_disp_intr_(struct nvkm_disp *base)
{
	struct nv50_disp *disp = nv50_disp(base);
	disp->func->intr(disp);
}

static void *
nv50_disp_dtor_(struct nvkm_disp *base)
{
	struct nv50_disp *disp = nv50_disp(base);
	nvkm_event_fini(&disp->uevent);
	return disp;
}

static const struct nvkm_disp_func
nv50_disp_ = {
	.dtor = nv50_disp_dtor_,
	.intr = nv50_disp_intr_,
	.root = nv50_disp_root_,
	.outp.internal.crt = nv50_disp_outp_internal_crt_,
	.outp.internal.tmds = nv50_disp_outp_internal_tmds_,
	.outp.internal.lvds = nv50_disp_outp_internal_lvds_,
	.outp.internal.dp = nv50_disp_outp_internal_dp_,
	.outp.external.tmds = nv50_disp_outp_external_tmds_,
	.outp.external.dp = nv50_disp_outp_external_dp_,
	.head.vblank_init = nv50_disp_vblank_init_,
	.head.vblank_fini = nv50_disp_vblank_fini_,
};

int
nv50_disp_new_(const struct nv50_disp_func *func, struct nvkm_device *device,
	       int index, int heads, struct nvkm_disp **pdisp)
{
	struct nv50_disp *disp;
	int ret;

	if (!(disp = kzalloc(sizeof(*disp), GFP_KERNEL)))
		return -ENOMEM;
	INIT_WORK(&disp->supervisor, func->super);
	disp->func = func;
	*pdisp = &disp->base;

	ret = nvkm_disp_ctor(&nv50_disp_, device, index, heads, &disp->base);
	if (ret)
		return ret;

	return nvkm_event_init(func->uevent, 1, 1 + (heads * 4), &disp->uevent);
}

void
nv50_disp_vblank_fini(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_mask(device, 0x61002c, (4 << head), 0);
}

void
nv50_disp_vblank_init(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_mask(device, 0x61002c, (4 << head), (4 << head));
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
	} else
	if (or < 8) {
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
		or  -= 4;
	} else {
		or   = or - 8;
		type = 0x0010;
		mask = 0;
		switch (ctrl & 0x00000f00) {
		case 0x00000000: type |= disp->pior.type[or]; break;
		default:
			nvkm_error(subdev, "unknown PIOR mc %08x\n", ctrl);
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
	u32 reg;
	int i;

	/* DAC */
	for (i = 0; !(ctrl & (1 << head)) && i < disp->func->dac.nr; i++)
		ctrl = nvkm_rd32(device, 0x610b5c + (i * 8));

	/* SOR */
	if (!(ctrl & (1 << head))) {
		if (device->chipset  < 0x90 ||
		    device->chipset == 0x92 ||
		    device->chipset == 0xa0) {
			reg = 0x610b74;
		} else {
			reg = 0x610798;
		}
		for (i = 0; !(ctrl & (1 << head)) && i < disp->func->sor.nr; i++)
			ctrl = nvkm_rd32(device, reg + (i * 8));
		i += 4;
	}

	/* PIOR */
	if (!(ctrl & (1 << head))) {
		for (i = 0; !(ctrl & (1 << head)) && i < disp->func->pior.nr; i++)
			ctrl = nvkm_rd32(device, 0x610b84 + (i * 8));
		i += 8;
	}

	if (!(ctrl & (1 << head)))
		return NULL;
	i--;

	outp = exec_lookup(disp, head, i, ctrl, &data, &ver, &hdr, &cnt, &len, &info);
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
	u32 reg;
	int i;

	/* DAC */
	for (i = 0; !(ctrl & (1 << head)) && i < disp->func->dac.nr; i++)
		ctrl = nvkm_rd32(device, 0x610b58 + (i * 8));

	/* SOR */
	if (!(ctrl & (1 << head))) {
		if (device->chipset  < 0x90 ||
		    device->chipset == 0x92 ||
		    device->chipset == 0xa0) {
			reg = 0x610b70;
		} else {
			reg = 0x610794;
		}
		for (i = 0; !(ctrl & (1 << head)) && i < disp->func->sor.nr; i++)
			ctrl = nvkm_rd32(device, reg + (i * 8));
		i += 4;
	}

	/* PIOR */
	if (!(ctrl & (1 << head))) {
		for (i = 0; !(ctrl & (1 << head)) && i < disp->func->pior.nr; i++)
			ctrl = nvkm_rd32(device, 0x610b80 + (i * 8));
		i += 8;
	}

	if (!(ctrl & (1 << head)))
		return NULL;
	i--;

	outp = exec_lookup(disp, head, i, ctrl, &data, &ver, &hdr, &cnt, &len, &info1);
	if (!outp)
		return NULL;

	*conf = (ctrl & 0x00000f00) >> 8;
	if (outp->info.location == 0) {
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
	} else {
		*conf = (ctrl & 0x00000f00) >> 8;
		pclk = pclk / 2;
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

static bool
nv50_disp_dptmds_war(struct nvkm_device *device)
{
	switch (device->chipset) {
	case 0x94:
	case 0x96:
	case 0x98:
		return true;
	default:
		break;
	}
	return false;
}

static bool
nv50_disp_dptmds_war_needed(struct nv50_disp *disp, struct dcb_output *outp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const u32 soff = __ffs(outp->or) * 0x800;
	if (nv50_disp_dptmds_war(device) && outp->type == DCB_OUTPUT_TMDS) {
		switch (nvkm_rd32(device, 0x614300 + soff) & 0x00030000) {
		case 0x00000000:
		case 0x00030000:
			return true;
		default:
			break;
		}
	}
	return false;

}

static void
nv50_disp_dptmds_war_2(struct nv50_disp *disp, struct dcb_output *outp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const u32 soff = __ffs(outp->or) * 0x800;

	if (!nv50_disp_dptmds_war_needed(disp, outp))
		return;

	nvkm_mask(device, 0x00e840, 0x80000000, 0x80000000);
	nvkm_mask(device, 0x614300 + soff, 0x03000000, 0x03000000);
	nvkm_mask(device, 0x61c10c + soff, 0x00000001, 0x00000001);

	nvkm_mask(device, 0x61c00c + soff, 0x0f000000, 0x00000000);
	nvkm_mask(device, 0x61c008 + soff, 0xff000000, 0x14000000);
	nvkm_usec(device, 400, NVKM_DELAY);
	nvkm_mask(device, 0x61c008 + soff, 0xff000000, 0x00000000);
	nvkm_mask(device, 0x61c00c + soff, 0x0f000000, 0x01000000);

	if (nvkm_rd32(device, 0x61c004 + soff) & 0x00000001) {
		u32 seqctl = nvkm_rd32(device, 0x61c030 + soff);
		u32  pu_pc = seqctl & 0x0000000f;
		nvkm_wr32(device, 0x61c040 + soff + pu_pc * 4, 0x1f008000);
	}
}

static void
nv50_disp_dptmds_war_3(struct nv50_disp *disp, struct dcb_output *outp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const u32 soff = __ffs(outp->or) * 0x800;
	u32 sorpwr;

	if (!nv50_disp_dptmds_war_needed(disp, outp))
		return;

	sorpwr = nvkm_rd32(device, 0x61c004 + soff);
	if (sorpwr & 0x00000001) {
		u32 seqctl = nvkm_rd32(device, 0x61c030 + soff);
		u32  pd_pc = (seqctl & 0x00000f00) >> 8;
		u32  pu_pc =  seqctl & 0x0000000f;

		nvkm_wr32(device, 0x61c040 + soff + pd_pc * 4, 0x1f008000);

		nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x61c030 + soff) & 0x10000000))
				break;
		);
		nvkm_mask(device, 0x61c004 + soff, 0x80000001, 0x80000000);
		nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x61c030 + soff) & 0x10000000))
				break;
		);

		nvkm_wr32(device, 0x61c040 + soff + pd_pc * 4, 0x00002000);
		nvkm_wr32(device, 0x61c040 + soff + pu_pc * 4, 0x1f000000);
	}

	nvkm_mask(device, 0x61c10c + soff, 0x00000001, 0x00000000);
	nvkm_mask(device, 0x614300 + soff, 0x03000000, 0x00000000);

	if (sorpwr & 0x00000001) {
		nvkm_mask(device, 0x61c004 + soff, 0x80000001, 0x80000001);
	}
}

static void
nv50_disp_update_sppll1(struct nv50_disp *disp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	bool used = false;
	int sor;

	if (!nv50_disp_dptmds_war(device))
		return;

	for (sor = 0; sor < disp->func->sor.nr; sor++) {
		u32 clksor = nvkm_rd32(device, 0x614300 + (sor * 0x800));
		switch (clksor & 0x03000000) {
		case 0x02000000:
		case 0x03000000:
			used = true;
			break;
		default:
			break;
		}
	}

	if (used)
		return;

	nvkm_mask(device, 0x00e840, 0x80000000, 0x00000000);
}

static void
nv50_disp_intr_unk10_0(struct nv50_disp *disp, int head)
{
	exec_script(disp, head, 1);
}

static void
nv50_disp_intr_unk20_0(struct nv50_disp *disp, int head)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_output *outp = exec_script(disp, head, 2);

	/* the binary driver does this outside of the supervisor handling
	 * (after the third supervisor from a detach).  we (currently?)
	 * allow both detach/attach to happen in the same set of
	 * supervisor interrupts, so it would make sense to execute this
	 * (full power down?) script after all the detach phases of the
	 * supervisor handling.  like with training if needed from the
	 * second supervisor, nvidia doesn't do this, so who knows if it's
	 * entirely safe, but it does appear to work..
	 *
	 * without this script being run, on some configurations i've
	 * seen, switching from DP to TMDS on a DP connector may result
	 * in a blank screen (SOR_PWR off/on can restore it)
	 */
	if (outp && outp->info.type == DCB_OUTPUT_DP) {
		struct nvkm_output_dp *outpdp = nvkm_output_dp(outp);
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

static void
nv50_disp_intr_unk20_1(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_devinit *devinit = device->devinit;
	u32 pclk = nvkm_rd32(device, 0x610ad0 + (head * 0x540)) & 0x3fffff;
	if (pclk)
		nvkm_devinit_pll_set(devinit, PLL_VPLL0 + head, pclk);
}

static void
nv50_disp_intr_unk20_2_dp(struct nv50_disp *disp, int head,
			  struct dcb_output *outp, u32 pclk)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	const int link = !(outp->sorconf.link & 1);
	const int   or = ffs(outp->or) - 1;
	const u32 soff = (  or * 0x800);
	const u32 loff = (link * 0x080) + soff;
	const u32 ctrl = nvkm_rd32(device, 0x610794 + (or * 8));
	const u32 symbol = 100000;
	const s32 vactive = nvkm_rd32(device, 0x610af8 + (head * 0x540)) & 0xffff;
	const s32 vblanke = nvkm_rd32(device, 0x610ae8 + (head * 0x540)) & 0xffff;
	const s32 vblanks = nvkm_rd32(device, 0x610af0 + (head * 0x540)) & 0xffff;
	u32 dpctrl = nvkm_rd32(device, 0x61c10c + loff);
	u32 clksor = nvkm_rd32(device, 0x614300 + soff);
	int bestTU = 0, bestVTUi = 0, bestVTUf = 0, bestVTUa = 0;
	int TU, VTUi, VTUf, VTUa;
	u64 link_data_rate, link_ratio, unk;
	u32 best_diff = 64 * symbol;
	u32 link_nr, link_bw, bits;
	u64 value;

	link_bw = (clksor & 0x000c0000) ? 270000 : 162000;
	link_nr = hweight32(dpctrl & 0x000f0000);

	/* symbols/hblank - algorithm taken from comments in tegra driver */
	value = vblanke + vactive - vblanks - 7;
	value = value * link_bw;
	do_div(value, pclk);
	value = value - (3 * !!(dpctrl & 0x00004000)) - (12 / link_nr);
	nvkm_mask(device, 0x61c1e8 + soff, 0x0000ffff, value);

	/* symbols/vblank - algorithm taken from comments in tegra driver */
	value = vblanks - vblanke - 25;
	value = value * link_bw;
	do_div(value, pclk);
	value = value - ((36 / link_nr) + 3) - 1;
	nvkm_mask(device, 0x61c1ec + soff, 0x00ffffff, value);

	/* watermark / activesym */
	if      ((ctrl & 0xf0000) == 0x60000) bits = 30;
	else if ((ctrl & 0xf0000) == 0x50000) bits = 24;
	else                                  bits = 18;

	link_data_rate = (pclk * bits / 8) / link_nr;

	/* calculate ratio of packed data rate to link symbol rate */
	link_ratio = link_data_rate * symbol;
	do_div(link_ratio, link_bw);

	for (TU = 64; TU >= 32; TU--) {
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

	if (!bestTU) {
		nvkm_error(subdev, "unable to find suitable dp config\n");
		return;
	}

	/* XXX close to vbios numbers, but not right */
	unk  = (symbol - link_ratio) * bestTU;
	unk *= link_ratio;
	do_div(unk, symbol);
	do_div(unk, symbol);
	unk += 6;

	nvkm_mask(device, 0x61c10c + loff, 0x000001fc, bestTU << 2);
	nvkm_mask(device, 0x61c128 + loff, 0x010f7f3f, bestVTUa << 24 |
						   bestVTUf << 16 |
						   bestVTUi << 8 | unk);
}

static void
nv50_disp_intr_unk20_2(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_output *outp;
	u32 pclk = nvkm_rd32(device, 0x610ad0 + (head * 0x540)) & 0x3fffff;
	u32 hval, hreg = 0x614200 + (head * 0x800);
	u32 oval, oreg;
	u32 mask, conf;

	outp = exec_clkcmp(disp, head, 0xff, pclk, &conf);
	if (!outp)
		return;

	/* we allow both encoder attach and detach operations to occur
	 * within a single supervisor (ie. modeset) sequence.  the
	 * encoder detach scripts quite often switch off power to the
	 * lanes, which requires the link to be re-trained.
	 *
	 * this is not generally an issue as the sink "must" (heh)
	 * signal an irq when it's lost sync so the driver can
	 * re-train.
	 *
	 * however, on some boards, if one does not configure at least
	 * the gpu side of the link *before* attaching, then various
	 * things can go horribly wrong (PDISP disappearing from mmio,
	 * third supervisor never happens, etc).
	 *
	 * the solution is simply to retrain here, if necessary.  last
	 * i checked, the binary driver userspace does not appear to
	 * trigger this situation (it forces an UPDATE between steps).
	 */
	if (outp->info.type == DCB_OUTPUT_DP) {
		u32 soff = (ffs(outp->info.or) - 1) * 0x08;
		u32 ctrl, datarate;

		if (outp->info.location == 0) {
			ctrl = nvkm_rd32(device, 0x610794 + soff);
			soff = 1;
		} else {
			ctrl = nvkm_rd32(device, 0x610b80 + soff);
			soff = 2;
		}

		switch ((ctrl & 0x000f0000) >> 16) {
		case 6: datarate = pclk * 30; break;
		case 5: datarate = pclk * 24; break;
		case 2:
		default:
			datarate = pclk * 18;
			break;
		}

		if (nvkm_output_dp_train(outp, datarate / soff))
			OUTP_ERR(outp, "link not trained before attach");
	}

	exec_clkcmp(disp, head, 0, pclk, &conf);

	if (!outp->info.location && outp->info.type == DCB_OUTPUT_ANALOG) {
		oreg = 0x614280 + (ffs(outp->info.or) - 1) * 0x800;
		oval = 0x00000000;
		hval = 0x00000000;
		mask = 0xffffffff;
	} else
	if (!outp->info.location) {
		if (outp->info.type == DCB_OUTPUT_DP)
			nv50_disp_intr_unk20_2_dp(disp, head, &outp->info, pclk);
		oreg = 0x614300 + (ffs(outp->info.or) - 1) * 0x800;
		oval = (conf & 0x0100) ? 0x00000101 : 0x00000000;
		hval = 0x00000000;
		mask = 0x00000707;
	} else {
		oreg = 0x614380 + (ffs(outp->info.or) - 1) * 0x800;
		oval = 0x00000001;
		hval = 0x00000001;
		mask = 0x00000707;
	}

	nvkm_mask(device, hreg, 0x0000000f, hval);
	nvkm_mask(device, oreg, mask, oval);

	nv50_disp_dptmds_war_2(disp, &outp->info);
}

/* If programming a TMDS output on a SOR that can also be configured for
 * DisplayPort, make sure NV50_SOR_DP_CTRL_ENABLE is forced off.
 *
 * It looks like the VBIOS TMDS scripts make an attempt at this, however,
 * the VBIOS scripts on at least one board I have only switch it off on
 * link 0, causing a blank display if the output has previously been
 * programmed for DisplayPort.
 */
static void
nv50_disp_intr_unk40_0_tmds(struct nv50_disp *disp,
			    struct dcb_output *outp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_bios *bios = device->bios;
	const int link = !(outp->sorconf.link & 1);
	const int   or = ffs(outp->or) - 1;
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u16 mask = (outp->sorconf.link << 6) | outp->or;
	struct dcb_output match;
	u8  ver, hdr;

	if (dcb_outp_match(bios, DCB_OUTPUT_DP, mask, &ver, &hdr, &match))
		nvkm_mask(device, 0x61c10c + loff, 0x00000001, 0x00000000);
}

static void
nv50_disp_intr_unk40_0(struct nv50_disp *disp, int head)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_output *outp;
	u32 pclk = nvkm_rd32(device, 0x610ad0 + (head * 0x540)) & 0x3fffff;
	u32 conf;

	outp = exec_clkcmp(disp, head, 1, pclk, &conf);
	if (!outp)
		return;

	if (outp->info.location == 0 && outp->info.type == DCB_OUTPUT_TMDS)
		nv50_disp_intr_unk40_0_tmds(disp, &outp->info);
	nv50_disp_dptmds_war_3(disp, &outp->info);
}

void
nv50_disp_intr_supervisor(struct work_struct *work)
{
	struct nv50_disp *disp =
		container_of(work, struct nv50_disp, supervisor);
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 super = nvkm_rd32(device, 0x610030);
	int head;

	nvkm_debug(subdev, "supervisor %08x %08x\n", disp->super, super);

	if (disp->super & 0x00000010) {
		nv50_disp_chan_mthd(disp->chan[0], NV_DBG_DEBUG);
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(super & (0x00000020 << head)))
				continue;
			if (!(super & (0x00000080 << head)))
				continue;
			nv50_disp_intr_unk10_0(disp, head);
		}
	} else
	if (disp->super & 0x00000020) {
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(super & (0x00000080 << head)))
				continue;
			nv50_disp_intr_unk20_0(disp, head);
		}
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(super & (0x00000200 << head)))
				continue;
			nv50_disp_intr_unk20_1(disp, head);
		}
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(super & (0x00000080 << head)))
				continue;
			nv50_disp_intr_unk20_2(disp, head);
		}
	} else
	if (disp->super & 0x00000040) {
		for (head = 0; head < disp->base.head.nr; head++) {
			if (!(super & (0x00000080 << head)))
				continue;
			nv50_disp_intr_unk40_0(disp, head);
		}
		nv50_disp_update_sppll1(disp);
	}

	nvkm_wr32(device, 0x610030, 0x80000000);
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
		schedule_work(&disp->supervisor);
		nvkm_wr32(device, 0x610024, disp->super);
	}
}

static const struct nv50_disp_func
nv50_disp = {
	.intr = nv50_disp_intr,
	.uevent = &nv50_disp_chan_uevent,
	.super = nv50_disp_intr_supervisor,
	.root = &nv50_disp_root_oclass,
	.head.vblank_init = nv50_disp_vblank_init,
	.head.vblank_fini = nv50_disp_vblank_fini,
	.head.scanoutpos = nv50_disp_root_scanoutpos,
	.outp.internal.crt = nv50_dac_output_new,
	.outp.internal.tmds = nv50_sor_output_new,
	.outp.internal.lvds = nv50_sor_output_new,
	.outp.external.tmds = nv50_pior_output_new,
	.outp.external.dp = nv50_pior_dp_new,
	.dac.nr = 3,
	.dac.power = nv50_dac_power,
	.dac.sense = nv50_dac_sense,
	.sor.nr = 2,
	.sor.power = nv50_sor_power,
	.pior.nr = 3,
	.pior.power = nv50_pior_power,
};

int
nv50_disp_new(struct nvkm_device *device, int index, struct nvkm_disp **pdisp)
{
	return nv50_disp_new_(&nv50_disp, device, index, 2, pdisp);
}
