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

#include <core/device.h>

static u32
prom_read(void *data, u32 offset, u32 length, struct nvkm_bios *bios)
{
	u32 i;
	if (offset + length <= 0x00100000) {
		for (i = offset; i < offset + length; i += 4)
			*(u32 *)&bios->data[i] = nv_rd32(bios, 0x300000 + i);
		return length;
	}
	return 0;
}

static void
prom_fini(void *data)
{
	struct nvkm_bios *bios = data;
	if (nv_device(bios)->card_type < NV_50)
		nv_mask(bios, 0x001850, 0x00000001, 0x00000001);
	else
		nv_mask(bios, 0x088050, 0x00000001, 0x00000001);
}

static void *
prom_init(struct nvkm_bios *bios, const char *name)
{
	if (nv_device(bios)->card_type < NV_50) {
		if (nv_device(bios)->card_type == NV_40 &&
		    nv_device(bios)->chipset >= 0x4c)
			return ERR_PTR(-ENODEV);
		nv_mask(bios, 0x001850, 0x00000001, 0x00000000);
	} else {
		nv_mask(bios, 0x088050, 0x00000001, 0x00000000);
	}
	return bios;
}

const struct nvbios_source
nvbios_rom = {
	.name = "PROM",
	.init = prom_init,
	.fini = prom_fini,
	.read = prom_read,
	.rw = false,
};
