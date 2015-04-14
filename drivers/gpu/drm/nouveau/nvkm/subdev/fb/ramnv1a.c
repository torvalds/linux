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
#include "priv.h"

#include <core/device.h>

static int
nv1a_ram_create(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nvkm_fb *pfb = nvkm_fb(parent);
	struct nvkm_ram *ram;
	struct pci_dev *bridge;
	u32 mem, mib;
	int ret;

	bridge = pci_get_bus_and_slot(0, PCI_DEVFN(0, 1));
	if (!bridge) {
		nv_fatal(pfb, "no bridge device\n");
		return -ENODEV;
	}

	ret = nvkm_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	if (nv_device(pfb)->chipset == 0x1a) {
		pci_read_config_dword(bridge, 0x7c, &mem);
		mib = ((mem >> 6) & 31) + 1;
	} else {
		pci_read_config_dword(bridge, 0x84, &mem);
		mib = ((mem >> 4) & 127) + 1;
	}

	ram->type = NV_MEM_TYPE_STOLEN;
	ram->size = mib * 1024 * 1024;
	return 0;
}

struct nvkm_oclass
nv1a_ram_oclass = {
	.handle = 0,
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv1a_ram_create,
		.dtor = _nvkm_ram_dtor,
		.init = _nvkm_ram_init,
		.fini = _nvkm_ram_fini,
	}
};
