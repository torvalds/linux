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

#include <subdev/timer.h>

static inline u32
g94_sor_soff(struct nvkm_output_dp *outp)
{
	return (ffs(outp->base.info.or) - 1) * 0x800;
}

static inline u32
g94_sor_loff(struct nvkm_output_dp *outp)
{
	return g94_sor_soff(outp) + !(outp->base.info.sorconf.link & 1) * 0x80;
}

/*******************************************************************************
 * DisplayPort
 ******************************************************************************/
u32
g94_sor_dp_lane_map(struct nvkm_device *device, u8 lane)
{
	static const u8 gm100[] = { 0, 8, 16, 24 };
	static const u8 mcp89[] = { 24, 16, 8, 0 }; /* thanks, apple.. */
	static const u8   g94[] = { 16, 8, 0, 24 };
	if (device->chipset >= 0x110)
		return gm100[lane];
	if (device->chipset == 0xaf)
		return mcp89[lane];
	return g94[lane];
}

static int
g94_sor_dp_pattern(struct nvkm_output_dp *outp, int pattern)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	const u32 loff = g94_sor_loff(outp);
	nvkm_mask(device, 0x61c10c + loff, 0x0f000000, pattern << 24);
	return 0;
}

int
g94_sor_dp_lnk_pwr(struct nvkm_output_dp *outp, int nr)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	const u32 soff = g94_sor_soff(outp);
	const u32 loff = g94_sor_loff(outp);
	u32 mask = 0, i;

	for (i = 0; i < nr; i++)
		mask |= 1 << (g94_sor_dp_lane_map(device, i) >> 3);

	nvkm_mask(device, 0x61c130 + loff, 0x0000000f, mask);
	nvkm_mask(device, 0x61c034 + soff, 0x80000000, 0x80000000);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61c034 + soff) & 0x80000000))
			break;
	);
	return 0;
}

static int
g94_sor_dp_lnk_ctl(struct nvkm_output_dp *outp, int nr, int bw, bool ef)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	const u32 soff = g94_sor_soff(outp);
	const u32 loff = g94_sor_loff(outp);
	u32 dpctrl = 0x00000000;
	u32 clksor = 0x00000000;

	dpctrl |= ((1 << nr) - 1) << 16;
	if (ef)
		dpctrl |= 0x00004000;
	if (bw > 0x06)
		clksor |= 0x00040000;

	nvkm_mask(device, 0x614300 + soff, 0x000c0000, clksor);
	nvkm_mask(device, 0x61c10c + loff, 0x001f4000, dpctrl);
	return 0;
}

static int
g94_sor_dp_drv_ctl(struct nvkm_output_dp *outp, int ln, int vs, int pe, int pc)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	struct nvkm_bios *bios = device->bios;
	const u32 shift = g94_sor_dp_lane_map(device, ln);
	const u32 loff = g94_sor_loff(outp);
	u32 addr, data[3];
	u8  ver, hdr, cnt, len;
	struct nvbios_dpout info;
	struct nvbios_dpcfg ocfg;

	addr = nvbios_dpout_match(bios, outp->base.info.hasht,
					outp->base.info.hashm,
				  &ver, &hdr, &cnt, &len, &info);
	if (!addr)
		return -ENODEV;

	addr = nvbios_dpcfg_match(bios, addr, 0, vs, pe,
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
	return 0;
}

static const struct nvkm_output_dp_func
g94_sor_dp_func = {
	.pattern = g94_sor_dp_pattern,
	.lnk_pwr = g94_sor_dp_lnk_pwr,
	.lnk_ctl = g94_sor_dp_lnk_ctl,
	.drv_ctl = g94_sor_dp_drv_ctl,
};

int
g94_sor_dp_new(struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
	       struct nvkm_output **poutp)
{
	return nvkm_output_dp_new_(&g94_sor_dp_func, disp, index, dcbE, poutp);
}
