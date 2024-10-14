/*
 * Copyright 2013 Red Hat Inc.
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
#include "ram.h"

#include <subdev/bios.h>
#include <subdev/bios/init.h>
#include <subdev/bios/rammap.h>

int
gp100_ram_init(struct nvkm_ram *ram)
{
	struct nvkm_subdev *subdev = &ram->fb->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_bios *bios = device->bios;
	u8  ver, hdr, cnt, len, snr, ssz;
	u32 data;
	int i;

	/* run a bunch of tables from rammap table.  there's actually
	 * individual pointers for each rammap entry too, but, nvidia
	 * seem to just run the last two entries' scripts early on in
	 * their init, and never again.. we'll just run 'em all once
	 * for now.
	 *
	 * i strongly suspect that each script is for a separate mode
	 * (likely selected by 0x9a065c's lower bits?), and the
	 * binary driver skips the one that's already been setup by
	 * the init tables.
	 */
	data = nvbios_rammapTe(bios, &ver, &hdr, &cnt, &len, &snr, &ssz);
	if (!data || hdr < 0x15)
		return -EINVAL;

	cnt  = nvbios_rd08(bios, data + 0x14); /* guess at count */
	data = nvbios_rd32(bios, data + 0x10); /* guess u32... */
	if (cnt) {
		u32 save = nvkm_rd32(device, 0x9a065c) & 0x000000f0;
		for (i = 0; i < cnt; i++, data += 4) {
			if (i != save >> 4) {
				nvkm_mask(device, 0x9a065c, 0x000000f0, i << 4);
				nvbios_init(subdev, nvbios_rd32(bios, data));
			}
		}
		nvkm_mask(device, 0x9a065c, 0x000000f0, save);
	}

	nvkm_mask(device, 0x9a0584, 0x11000000, 0x00000000);
	nvkm_wr32(device, 0x10ecc0, 0xffffffff);
	nvkm_mask(device, 0x9a0160, 0x00000010, 0x00000010);
	return 0;
}

static u32
gp100_ram_probe_fbpa(struct nvkm_device *device, int fbpa)
{
	return nvkm_rd32(device, 0x90020c + (fbpa * 0x4000));
}

static const struct nvkm_ram_func
gp100_ram = {
	.upper = 0x1000000000ULL,
	.probe_fbp = gm107_ram_probe_fbp,
	.probe_fbp_amount = gm200_ram_probe_fbp_amount,
	.probe_fbpa_amount = gp100_ram_probe_fbpa,
	.init = gp100_ram_init,
};

int
gp100_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	struct nvkm_ram *ram;

	if (!(ram = *pram = kzalloc(sizeof(*ram), GFP_KERNEL)))
		return -ENOMEM;

	return gf100_ram_ctor(&gp100_ram, fb, ram);

}
