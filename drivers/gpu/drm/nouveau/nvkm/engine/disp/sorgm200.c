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
gm200_sor_soff(struct nvkm_output_dp *outp)
{
	return (ffs(outp->base.info.or) - 1) * 0x800;
}

static inline u32
gm200_sor_loff(struct nvkm_output_dp *outp)
{
	return gm200_sor_soff(outp) + !(outp->base.info.sorconf.link & 1) * 0x80;
}

void
gm200_sor_magic(struct nvkm_output *outp)
{
	struct nvkm_device *device = outp->disp->engine.subdev.device;
	const u32 soff = outp->or * 0x100;
	const u32 data = outp->or + 1;
	if (outp->info.sorconf.link & 1)
		nvkm_mask(device, 0x612308 + soff, 0x0000001f, 0x00000000 | data);
	if (outp->info.sorconf.link & 2)
		nvkm_mask(device, 0x612388 + soff, 0x0000001f, 0x00000010 | data);
}

static inline u32
gm200_sor_dp_lane_map(struct nvkm_device *device, u8 lane)
{
	return lane * 0x08;
}

static int
gm200_sor_dp_lnk_pwr(struct nvkm_output_dp *outp, int nr)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	const u32 soff = gm200_sor_soff(outp);
	const u32 loff = gm200_sor_loff(outp);
	u32 mask = 0, i;

	for (i = 0; i < nr; i++)
		mask |= 1 << (gm200_sor_dp_lane_map(device, i) >> 3);

	nvkm_mask(device, 0x61c130 + loff, 0x0000000f, mask);
	nvkm_mask(device, 0x61c034 + soff, 0x80000000, 0x80000000);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61c034 + soff) & 0x80000000))
			break;
	);
	return 0;
}

static int
gm200_sor_dp_drv_ctl(struct nvkm_output_dp *outp,
		     int ln, int vs, int pe, int pc)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	struct nvkm_bios *bios = device->bios;
	const u32 shift = gm200_sor_dp_lane_map(device, ln);
	const u32 loff = gm200_sor_loff(outp);
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
	ocfg.tx_pu &= 0x0f;

	data[0] = nvkm_rd32(device, 0x61c118 + loff) & ~(0x000000ff << shift);
	data[1] = nvkm_rd32(device, 0x61c120 + loff) & ~(0x000000ff << shift);
	data[2] = nvkm_rd32(device, 0x61c130 + loff);
	if ((data[2] & 0x00000f00) < (ocfg.tx_pu << 8) || ln == 0)
		data[2] = (data[2] & ~0x00000f00) | (ocfg.tx_pu << 8);
	nvkm_wr32(device, 0x61c118 + loff, data[0] | (ocfg.dc << shift));
	nvkm_wr32(device, 0x61c120 + loff, data[1] | (ocfg.pe << shift));
	nvkm_wr32(device, 0x61c130 + loff, data[2]);
	data[3] = nvkm_rd32(device, 0x61c13c + loff) & ~(0x000000ff << shift);
	nvkm_wr32(device, 0x61c13c + loff, data[3] | (ocfg.pc << shift));
	return 0;
}

static const struct nvkm_output_dp_func
gm200_sor_dp_func = {
	.pattern = gm107_sor_dp_pattern,
	.lnk_pwr = gm200_sor_dp_lnk_pwr,
	.lnk_ctl = gf119_sor_dp_lnk_ctl,
	.drv_ctl = gm200_sor_dp_drv_ctl,
};

int
gm200_sor_dp_new(struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
		 struct nvkm_output **poutp)
{
	return nvkm_output_dp_new_(&gm200_sor_dp_func, disp, index, dcbE, poutp);
}
