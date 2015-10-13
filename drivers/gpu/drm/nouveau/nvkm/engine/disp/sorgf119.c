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
gf119_sor_soff(struct nvkm_output_dp *outp)
{
	return (ffs(outp->base.info.or) - 1) * 0x800;
}

static inline u32
gf119_sor_loff(struct nvkm_output_dp *outp)
{
	return gf119_sor_soff(outp) + !(outp->base.info.sorconf.link & 1) * 0x80;
}

static int
gf119_sor_dp_pattern(struct nvkm_output_dp *outp, int pattern)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	const u32 loff = gf119_sor_loff(outp);
	nvkm_mask(device, 0x61c110 + loff, 0x0f0f0f0f, 0x01010101 * pattern);
	return 0;
}

int
gf119_sor_dp_lnk_ctl(struct nvkm_output_dp *outp, int nr, int bw, bool ef)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	const u32 soff = gf119_sor_soff(outp);
	const u32 loff = gf119_sor_loff(outp);
	u32 dpctrl = 0x00000000;
	u32 clksor = 0x00000000;

	clksor |= bw << 18;
	dpctrl |= ((1 << nr) - 1) << 16;
	if (ef)
		dpctrl |= 0x00004000;

	nvkm_mask(device, 0x612300 + soff, 0x007c0000, clksor);
	nvkm_mask(device, 0x61c10c + loff, 0x001f4000, dpctrl);
	return 0;
}

static int
gf119_sor_dp_drv_ctl(struct nvkm_output_dp *outp,
		     int ln, int vs, int pe, int pc)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	struct nvkm_bios *bios = device->bios;
	const u32 shift = g94_sor_dp_lane_map(device, ln);
	const u32 loff = gf119_sor_loff(outp);
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

	data[0] = nvkm_rd32(device, 0x61c118 + loff) & ~(0x000000ff << shift);
	data[1] = nvkm_rd32(device, 0x61c120 + loff) & ~(0x000000ff << shift);
	data[2] = nvkm_rd32(device, 0x61c130 + loff);
	if ((data[2] & 0x0000ff00) < (ocfg.tx_pu << 8) || ln == 0)
		data[2] = (data[2] & ~0x0000ff00) | (ocfg.tx_pu << 8);
	nvkm_wr32(device, 0x61c118 + loff, data[0] | (ocfg.dc << shift));
	nvkm_wr32(device, 0x61c120 + loff, data[1] | (ocfg.pe << shift));
	nvkm_wr32(device, 0x61c130 + loff, data[2]);
	data[3] = nvkm_rd32(device, 0x61c13c + loff) & ~(0x000000ff << shift);
	nvkm_wr32(device, 0x61c13c + loff, data[3] | (ocfg.pc << shift));
	return 0;
}

static const struct nvkm_output_dp_func
gf119_sor_dp_func = {
	.pattern = gf119_sor_dp_pattern,
	.lnk_pwr = g94_sor_dp_lnk_pwr,
	.lnk_ctl = gf119_sor_dp_lnk_ctl,
	.drv_ctl = gf119_sor_dp_drv_ctl,
};

int
gf119_sor_dp_new(struct nvkm_disp *disp, int index,
		 struct dcb_output *dcbE, struct nvkm_output **poutp)
{
	return nvkm_output_dp_new_(&gf119_sor_dp_func, disp, index, dcbE, poutp);
}
