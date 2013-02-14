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
nvd0_sor_dp_lane_map(struct nv50_disp_priv *priv, u8 lane)
{
	static const u8 nvd0[] = { 16, 8, 0, 24 };
	return nvd0[lane];
}

int
nvd0_sor_dp_train(struct nv50_disp_priv *priv, int or, int link,
		  u16 type, u16 mask, u32 data, struct dcb_output *info)
{
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u32 patt = (data & NV94_DISP_SOR_DP_TRAIN_PATTERN);
	nv_mask(priv, 0x61c110 + loff, 0x0f0f0f0f, 0x01010101 * patt);
	return 0;
}

int
nvd0_sor_dp_lnkctl(struct nv50_disp_priv *priv, int or, int link, int head,
		   u16 type, u16 mask, u32 data, struct dcb_output *dcbo)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u32 soff = (or * 0x800);
	const u8  link_bw = (data & NV94_DISP_SOR_DP_LNKCTL_WIDTH) >> 8;
	const u8  link_nr = (data & NV94_DISP_SOR_DP_LNKCTL_COUNT);
	u32 dpctrl = 0x00000000;
	u32 clksor = 0x00000000;
	u32 outp, lane = 0;
	u8  ver, hdr, cnt, len;
	struct nvbios_dpout info;
	int i;

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

		while (nv_ro08(bios, info.lnkcmp) < link_bw)
			info.lnkcmp += 3;
		init.offset = nv_ro16(bios, info.lnkcmp + 1);

		nvbios_exec(&init);
	}

	clksor |= link_bw << 18;
	dpctrl |= ((1 << link_nr) - 1) << 16;
	if (data & NV94_DISP_SOR_DP_LNKCTL_FRAME_ENH)
		dpctrl |= 0x00004000;

	for (i = 0; i < link_nr; i++)
		lane |= 1 << (nvd0_sor_dp_lane_map(priv, i) >> 3);

	nv_mask(priv, 0x612300 + soff, 0x007c0000, clksor);
	nv_mask(priv, 0x61c10c + loff, 0x001f4000, dpctrl);
	nv_mask(priv, 0x61c130 + loff, 0x0000000f, lane);
	return 0;
}

int
nvd0_sor_dp_drvctl(struct nv50_disp_priv *priv, int or, int link, int lane,
		   u16 type, u16 mask, u32 data, struct dcb_output *dcbo)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u8 swing = (data & NV94_DISP_SOR_DP_DRVCTL_VS) >> 8;
	const u8 preem = (data & NV94_DISP_SOR_DP_DRVCTL_PE);
	u32 addr, shift = nvd0_sor_dp_lane_map(priv, lane);
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
	nv_mask(priv, 0x61c13c + loff, 0x00000000, 0x00000000);
	return 0;
}
