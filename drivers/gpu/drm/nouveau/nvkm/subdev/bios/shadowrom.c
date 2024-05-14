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
 */
#include "priv.h"

#include <subdev/pci.h>

static u32
nvbios_prom_read(void *data, u32 offset, u32 length, struct nvkm_bios *bios)
{
	struct nvkm_device *device = data;
	u32 i;
	if (offset + length <= 0x00100000) {
		for (i = offset; i < offset + length; i += 4)
			*(u32 *)&bios->data[i] = nvkm_rd32(device, 0x300000 + i);
		return length;
	}
	return 0;
}

static void
nvbios_prom_fini(void *data)
{
	struct nvkm_device *device = data;
	nvkm_pci_rom_shadow(device->pci, true);
}

static void *
nvbios_prom_init(struct nvkm_bios *bios, const char *name)
{
	struct nvkm_device *device = bios->subdev.device;
	if (device->card_type == NV_40 && device->chipset >= 0x4c)
		return ERR_PTR(-ENODEV);
	nvkm_pci_rom_shadow(device->pci, false);
	return device;
}

const struct nvbios_source
nvbios_prom = {
	.name = "PROM",
	.init = nvbios_prom_init,
	.fini = nvbios_prom_fini,
	.read = nvbios_prom_read,
	.rw = false,
};
