/*
 * Copyright 2018 Red Hat Inc.
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
 */
#include "channv50.h"

#include <subdev/timer.h>

const struct nv50_disp_mthd_list
gv100_disp_core_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0200, 0x680200 },
		{ 0x0208, 0x680208 },
		{ 0x020c, 0x68020c },
		{ 0x0210, 0x680210 },
		{ 0x0214, 0x680214 },
		{ 0x0218, 0x680218 },
		{ 0x021c, 0x68021c },
		{}
	}
};

const struct nv50_disp_mthd_list
gv100_disp_core_mthd_sor = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0300, 0x680300 },
		{ 0x0304, 0x680304 },
		{ 0x0308, 0x680308 },
		{ 0x030c, 0x68030c },
		{}
	}
};

static const struct nv50_disp_mthd_list
gv100_disp_core_mthd_wndw = {
	.mthd = 0x0080,
	.addr = 0x000080,
	.data = {
		{ 0x1000, 0x681000 },
		{ 0x1004, 0x681004 },
		{ 0x1008, 0x681008 },
		{ 0x100c, 0x68100c },
		{ 0x1010, 0x681010 },
		{}
	}
};

static const struct nv50_disp_mthd_list
gv100_disp_core_mthd_head = {
	.mthd = 0x0400,
	.addr = 0x000400,
	.data = {
		{ 0x2000, 0x682000 },
		{ 0x2004, 0x682004 },
		{ 0x2008, 0x682008 },
		{ 0x200c, 0x68200c },
		{ 0x2014, 0x682014 },
		{ 0x2018, 0x682018 },
		{ 0x201c, 0x68201c },
		{ 0x2020, 0x682020 },
		{ 0x2028, 0x682028 },
		{ 0x202c, 0x68202c },
		{ 0x2030, 0x682030 },
		{ 0x2038, 0x682038 },
		{ 0x203c, 0x68203c },
		{ 0x2048, 0x682048 },
		{ 0x204c, 0x68204c },
		{ 0x2050, 0x682050 },
		{ 0x2054, 0x682054 },
		{ 0x2058, 0x682058 },
		{ 0x205c, 0x68205c },
		{ 0x2060, 0x682060 },
		{ 0x2064, 0x682064 },
		{ 0x2068, 0x682068 },
		{ 0x206c, 0x68206c },
		{ 0x2070, 0x682070 },
		{ 0x2074, 0x682074 },
		{ 0x2078, 0x682078 },
		{ 0x207c, 0x68207c },
		{ 0x2080, 0x682080 },
		{ 0x2088, 0x682088 },
		{ 0x2090, 0x682090 },
		{ 0x209c, 0x68209c },
		{ 0x20a0, 0x6820a0 },
		{ 0x20a4, 0x6820a4 },
		{ 0x20a8, 0x6820a8 },
		{ 0x20ac, 0x6820ac },
		{ 0x218c, 0x68218c },
		{ 0x2194, 0x682194 },
		{ 0x2198, 0x682198 },
		{ 0x219c, 0x68219c },
		{ 0x21a0, 0x6821a0 },
		{ 0x21a4, 0x6821a4 },
		{ 0x2214, 0x682214 },
		{ 0x2218, 0x682218 },
		{}
	}
};

static const struct nv50_disp_chan_mthd
gv100_disp_core_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = 0x008000,
	.data = {
		{ "Global", 1, &gv100_disp_core_mthd_base },
		{    "SOR", 4, &gv100_disp_core_mthd_sor  },
		{ "WINDOW", 8, &gv100_disp_core_mthd_wndw },
		{   "HEAD", 4, &gv100_disp_core_mthd_head },
		{}
	}
};

static int
gv100_disp_core_idle(struct nv50_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	nvkm_msec(device, 2000,
		u32 stat = nvkm_rd32(device, 0x610630);
		if ((stat & 0x001f0000) == 0x000b0000)
			return 0;
	);
	return -EBUSY;
}

static u64
gv100_disp_core_user(struct nv50_disp_chan *chan, u64 *psize)
{
	*psize = 0x10000;
	return 0x680000;
}

static void
gv100_disp_core_intr(struct nv50_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	const u32 mask = 0x00000001;
	const u32 data = en ? mask : 0;
	nvkm_mask(device, 0x611dac, mask, data);
}

static void
gv100_disp_core_fini(struct nv50_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	nvkm_mask(device, 0x6104e0, 0x00000010, 0x00000000);
	gv100_disp_core_idle(chan);
	nvkm_mask(device, 0x6104e0, 0x00000002, 0x00000000);
	chan->suspend_put = nvkm_rd32(device, 0x680000);
}

static int
gv100_disp_core_init(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;

	nvkm_wr32(device, 0x610b24, lower_32_bits(chan->push));
	nvkm_wr32(device, 0x610b20, upper_32_bits(chan->push));
	nvkm_wr32(device, 0x610b28, 0x00000001);
	nvkm_wr32(device, 0x610b2c, 0x00000040);

	nvkm_mask(device, 0x6104e0, 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x680000, chan->suspend_put);
	nvkm_wr32(device, 0x6104e0, 0x00000013);
	return gv100_disp_core_idle(chan);
}

static const struct nv50_disp_chan_func
gv100_disp_core = {
	.init = gv100_disp_core_init,
	.fini = gv100_disp_core_fini,
	.intr = gv100_disp_core_intr,
	.user = gv100_disp_core_user,
	.bind = gv100_disp_dmac_bind,
};

int
gv100_disp_core_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nv50_disp *disp, struct nvkm_object **pobject)
{
	return nv50_disp_core_new_(&gv100_disp_core, &gv100_disp_core_mthd,
				   disp, 0, oclass, argv, argc, pobject);
}
