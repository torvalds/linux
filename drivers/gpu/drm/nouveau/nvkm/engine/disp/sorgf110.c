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
#include "outpdp.h"

static inline u32
gf110_sor_soff(struct nvkm_output_dp *outp)
{
	return (ffs(outp->base.info.or) - 1) * 0x800;
}

static inline u32
gf110_sor_loff(struct nvkm_output_dp *outp)
{
	return gf110_sor_soff(outp) + !(outp->base.info.sorconf.link & 1) * 0x80;
}

static inline u32
gf110_sor_dp_lane_map(struct nv50_disp_priv *priv, u8 lane)
{
	static const u8 gf110[] = { 16, 8, 0, 24 };
	return gf110[lane];
}

static int
gf110_sor_dp_pattern(struct nvkm_output_dp *outp, int pattern)
{
	struct nv50_disp_priv *priv = (void *)nvkm_disp(outp);
	const u32 loff = gf110_sor_loff(outp);
	nv_mask(priv, 0x61c110 + loff, 0x0f0f0f0f, 0x01010101 * pattern);
	return 0;
}

int
gf110_sor_dp_lnk_ctl(struct nvkm_output_dp *outp, int nr, int bw, bool ef)
{
	struct nv50_disp_priv *priv = (void *)nvkm_disp(outp);
	const u32 soff = gf110_sor_soff(outp);
	const u32 loff = gf110_sor_loff(outp);
	u32 dpctrl = 0x00000000;
	u32 clksor = 0x00000000;

	clksor |= bw << 18;
	dpctrl |= ((1 << nr) - 1) << 16;
	if (ef)
		dpctrl |= 0x00004000;

	nv_mask(priv, 0x612300 + soff, 0x007c0000, clksor);
	nv_mask(priv, 0x61c10c + loff, 0x001f4000, dpctrl);
	return 0;
}

static int
gf110_sor_dp_drv_ctl(struct nvkm_output_dp *outp,
		     int ln, int vs, int pe, int pc)
{
	struct nv50_disp_priv *priv = (void *)nvkm_disp(outp);
	struct nvkm_bios *bios = nvkm_bios(priv);
	const u32 shift = gf110_sor_dp_lane_map(priv, ln);
	const u32 loff = gf110_sor_loff(outp);
	u32 addr, data[4];
	u8  ver, hdr, cnt, len;
	struct nvbios_dpout info;
	struct nvbios_dpcfg ocfg;

	addr = nvbios_dpout_match(bios, outp->base.info.hasht,
					outp->base.info.hashm,
				  &ver, &hdr, &cnt, &len, &info);
	if (!addr)
		return -ENODEV;

	addr = nvbios_dpcfg_match(bios, addr, pc, vs, pe,
				  &ver, &hdr, &cnt, &len, &ocfg);
	if (!addr)
		return -EINVAL;

	data[0] = nv_rd32(priv, 0x61c118 + loff) & ~(0x000000ff << shift);
	data[1] = nv_rd32(priv, 0x61c120 + loff) & ~(0x000000ff << shift);
	data[2] = nv_rd32(priv, 0x61c130 + loff);
	if ((data[2] & 0x0000ff00) < (ocfg.tx_pu << 8) || ln == 0)
		data[2] = (data[2] & ~0x0000ff00) | (ocfg.tx_pu << 8);
	nv_wr32(priv, 0x61c118 + loff, data[0] | (ocfg.dc << shift));
	nv_wr32(priv, 0x61c120 + loff, data[1] | (ocfg.pe << shift));
	nv_wr32(priv, 0x61c130 + loff, data[2] | (ocfg.tx_pu << 8));
	data[3] = nv_rd32(priv, 0x61c13c + loff) & ~(0x000000ff << shift);
	nv_wr32(priv, 0x61c13c + loff, data[3] | (ocfg.pc << shift));
	return 0;
}

struct nvkm_output_dp_impl
gf110_sor_dp_impl = {
	.base.base.handle = DCB_OUTPUT_DP,
	.base.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_output_dp_ctor,
		.dtor = _nvkm_output_dp_dtor,
		.init = _nvkm_output_dp_init,
		.fini = _nvkm_output_dp_fini,
	},
	.pattern = gf110_sor_dp_pattern,
	.lnk_pwr = g94_sor_dp_lnk_pwr,
	.lnk_ctl = gf110_sor_dp_lnk_ctl,
	.drv_ctl = gf110_sor_dp_drv_ctl,
};
