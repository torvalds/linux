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

#include <core/os.h>
#include <core/class.h>

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/dp.h>
#include <subdev/bios/init.h>

#include "nv50.h"

static inline u32
nv94_sor_dp_lane_map(struct nv50_disp_priv *priv, u8 lane)
{
	static const u8 nvaf[] = { 24, 16, 8, 0 }; /* thanks, apple.. */
	static const u8 nv94[] = { 16, 8, 0, 24 };
	if (nv_device(priv)->chipset == 0xaf)
		return nvaf[lane];
	return nv94[lane];
}

int
nv94_sor_dp_train_init(struct nv50_disp_priv *priv, int or, int link, int head,
		       u16 type, u16 mask, u32 data, struct dcb_output *dcbo)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_dpout info;
	u8  ver, hdr, cnt, len;
	u16 outp;

	outp = nvbios_dpout_match(bios, type, mask, &ver, &hdr, &cnt, &len, &info);
	if (outp) {
		struct nvbios_init init = {
			.subdev = nv_subdev(priv),
			.bios = bios,
			.outp = dcbo,
			.crtc = head,
			.execute = 1,
		};

		if (data & NV94_DISP_SOR_DP_TRAIN_INIT_SPREAD_ON)
			init.offset = info.script[2];
		else
			init.offset = info.script[3];
		nvbios_exec(&init);

		init.offset = info.script[0];
		nvbios_exec(&init);
	}

	return 0;
}

int
nv94_sor_dp_train_fini(struct nv50_disp_priv *priv, int or, int link, int head,
		       u16 type, u16 mask, u32 data, struct dcb_output *dcbo)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_dpout info;
	u8  ver, hdr, cnt, len;
	u16 outp;

	outp = nvbios_dpout_match(bios, type, mask, &ver, &hdr, &cnt, &len, &info);
	if (outp) {
		struct nvbios_init init = {
			.subdev = nv_subdev(priv),
			.bios = bios,
			.offset = info.script[1],
			.outp = dcbo,
			.crtc = head,
			.execute = 1,
		};

		nvbios_exec(&init);
	}

	return 0;
}

int
nv94_sor_dp_train(struct nv50_disp_priv *priv, int or, int link,
		  u16 type, u16 mask, u32 data, struct dcb_output *info)
{
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u32 patt = (data & NV94_DISP_SOR_DP_TRAIN_PATTERN);
	nv_mask(priv, 0x61c10c + loff, 0x0f000000, patt << 24);
	return 0;
}

int
nv94_sor_dp_lnkctl(struct nv50_disp_priv *priv, int or, int link, int head,
		   u16 type, u16 mask, u32 data, struct dcb_output *dcbo)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u32 soff = (or * 0x800);
	u16 link_bw = (data & NV94_DISP_SOR_DP_LNKCTL_WIDTH) >> 8;
	u8  link_nr = (data & NV94_DISP_SOR_DP_LNKCTL_COUNT);
	u32 dpctrl = 0x00000000;
	u32 clksor = 0x00000000;
	u32 outp, lane = 0;
	u8  ver, hdr, cnt, len;
	struct nvbios_dpout info;
	int i;

	/* -> 10Khz units */
	link_bw *= 2700;

	outp = nvbios_dpout_match(bios, type, mask, &ver, &hdr, &cnt, &len, &info);
	if (outp && info.lnkcmp) {
		struct nvbios_init init = {
			.subdev = nv_subdev(priv),
			.bios = bios,
			.offset = 0x0000,
			.outp = dcbo,
			.crtc = head,
			.execute = 1,
		};

		while (link_bw < nv_ro16(bios, info.lnkcmp))
			info.lnkcmp += 4;
		init.offset = nv_ro16(bios, info.lnkcmp + 2);

		nvbios_exec(&init);
	}

	dpctrl |= ((1 << link_nr) - 1) << 16;
	if (data & NV94_DISP_SOR_DP_LNKCTL_FRAME_ENH)
		dpctrl |= 0x00004000;
	if (link_bw > 16200)
		clksor |= 0x00040000;

	for (i = 0; i < link_nr; i++)
		lane |= 1 << (nv94_sor_dp_lane_map(priv, i) >> 3);

	nv_mask(priv, 0x614300 + soff, 0x000c0000, clksor);
	nv_mask(priv, 0x61c10c + loff, 0x001f4000, dpctrl);
	nv_mask(priv, 0x61c130 + loff, 0x0000000f, lane);
	return 0;
}

int
nv94_sor_dp_drvctl(struct nv50_disp_priv *priv, int or, int link, int lane,
		   u16 type, u16 mask, u32 data, struct dcb_output *dcbo)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u8 swing = (data & NV94_DISP_SOR_DP_DRVCTL_VS) >> 8;
	const u8 preem = (data & NV94_DISP_SOR_DP_DRVCTL_PE);
	u32 addr, shift = nv94_sor_dp_lane_map(priv, lane);
	u8  ver, hdr, cnt, len;
	struct nvbios_dpout outp;
	struct nvbios_dpcfg ocfg;

	addr = nvbios_dpout_match(bios, type, mask, &ver, &hdr, &cnt, &len, &outp);
	if (!addr)
		return -ENODEV;

	addr = nvbios_dpcfg_match(bios, addr, 0, swing, preem, &ver, &hdr, &cnt, &len, &ocfg);
	if (!addr)
		return -EINVAL;

	nv_mask(priv, 0x61c118 + loff, 0x000000ff << shift, ocfg.drv << shift);
	nv_mask(priv, 0x61c120 + loff, 0x000000ff << shift, ocfg.pre << shift);
	nv_mask(priv, 0x61c130 + loff, 0x0000ff00, ocfg.unk << 8);
	return 0;
}
