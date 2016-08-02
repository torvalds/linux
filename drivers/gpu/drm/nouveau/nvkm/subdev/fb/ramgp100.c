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

static int
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
				nvbios_exec(&(struct nvbios_init) {
						.subdev = subdev,
						.bios = bios,
						.offset = nvbios_rd32(bios, data),
						.execute = 1,
					    });
			}
		}
		nvkm_mask(device, 0x9a065c, 0x000000f0, save);
	}

	nvkm_mask(device, 0x9a0584, 0x11000000, 0x00000000);
	nvkm_wr32(device, 0x10ecc0, 0xffffffff);
	nvkm_mask(device, 0x9a0160, 0x00000010, 0x00000010);
	return 0;
}

static const struct nvkm_ram_func
gp100_ram_func = {
	.init = gp100_ram_init,
	.get = gf100_ram_get,
	.put = gf100_ram_put,
};

int
gp100_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	struct nvkm_ram *ram;
	struct nvkm_subdev *subdev = &fb->subdev;
	struct nvkm_device *device = subdev->device;
	enum nvkm_ram_type type = nvkm_fb_bios_memtype(device->bios);
	const u32 rsvd_head = ( 256 * 1024); /* vga memory */
	const u32 rsvd_tail = (1024 * 1024); /* vbios etc */
	u32 fbpa_num = nvkm_rd32(device, 0x022438), fbpa;
	u32 fbio_opt = nvkm_rd32(device, 0x021c14);
	u64 part, size = 0, comm = ~0ULL;
	bool mixed = false;
	int ret;

	nvkm_debug(subdev, "022438: %08x\n", fbpa_num);
	nvkm_debug(subdev, "021c14: %08x\n", fbio_opt);
	for (fbpa = 0; fbpa < fbpa_num; fbpa++) {
		if (!(fbio_opt & (1 << fbpa))) {
			part = nvkm_rd32(device, 0x90020c + (fbpa * 0x4000));
			nvkm_debug(subdev, "fbpa %02x: %lld MiB\n", fbpa, part);
			part = part << 20;
			if (part != comm) {
				if (comm != ~0ULL)
					mixed = true;
				comm = min(comm, part);
			}
			size = size + part;
		}
	}

	ret = nvkm_ram_new_(&gp100_ram_func, fb, type, size, 0, &ram);
	*pram = ram;
	if (ret)
		return ret;

	nvkm_mm_fini(&ram->vram);

	if (mixed) {
		ret = nvkm_mm_init(&ram->vram, rsvd_head >> NVKM_RAM_MM_SHIFT,
				   ((comm * fbpa_num) - rsvd_head) >>
				   NVKM_RAM_MM_SHIFT, 1);
		if (ret)
			return ret;

		ret = nvkm_mm_init(&ram->vram, (0x1000000000ULL + comm) >>
				   NVKM_RAM_MM_SHIFT,
				   (size - (comm * fbpa_num) - rsvd_tail) >>
				   NVKM_RAM_MM_SHIFT, 1);
		if (ret)
			return ret;
	} else {
		ret = nvkm_mm_init(&ram->vram, rsvd_head >> NVKM_RAM_MM_SHIFT,
				   (size - rsvd_head - rsvd_tail) >>
				   NVKM_RAM_MM_SHIFT, 1);
		if (ret)
			return ret;
	}

	return 0;
}
