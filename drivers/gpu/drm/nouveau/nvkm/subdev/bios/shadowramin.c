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

struct priv {
	struct nvkm_bios *bios;
	u32 bar0;
};

static u32
pramin_read(void *data, u32 offset, u32 length, struct nvkm_bios *bios)
{
	struct nvkm_device *device = bios->subdev.device;
	u32 i;
	if (offset + length <= 0x00100000) {
		for (i = offset; i < offset + length; i += 4)
			*(u32 *)&bios->data[i] = nvkm_rd32(device, 0x700000 + i);
		return length;
	}
	return 0;
}

static void
pramin_fini(void *data)
{
	struct priv *priv = data;
	if (priv) {
		struct nvkm_device *device = priv->bios->subdev.device;
		nvkm_wr32(device, 0x001700, priv->bar0);
		kfree(priv);
	}
}

static void *
pramin_init(struct nvkm_bios *bios, const char *name)
{
	struct nvkm_subdev *subdev = &bios->subdev;
	struct nvkm_device *device = subdev->device;
	struct priv *priv = NULL;
	u64 addr = 0;

	/* PRAMIN always potentially available prior to nv50 */
	if (device->card_type < NV_50)
		return NULL;

	/* we can't get the bios image pointer without PDISP */
	if (device->card_type >= GM100)
		addr = nvkm_rd32(device, 0x021c04);
	else
	if (device->card_type >= NV_C0)
		addr = nvkm_rd32(device, 0x022500);
	if (addr & 0x00000001) {
		nvkm_debug(subdev, "... display disabled\n");
		return ERR_PTR(-ENODEV);
	}

	/* check that the window is enabled and in vram, particularly
	 * important as we don't want to be touching vram on an
	 * uninitialised board
	 */
	addr = nvkm_rd32(device, 0x619f04);
	if (!(addr & 0x00000008)) {
		nvkm_debug(subdev, "... not enabled\n");
		return ERR_PTR(-ENODEV);
	}
	if ( (addr & 0x00000003) != 1) {
		nvkm_debug(subdev, "... not in vram\n");
		return ERR_PTR(-ENODEV);
	}

	/* some alternate method inherited from xf86-video-nv... */
	addr = (addr & 0xffffff00) << 8;
	if (!addr) {
		addr  = (u64)nvkm_rd32(device, 0x001700) << 16;
		addr += 0xf0000;
	}

	/* modify bar0 PRAMIN window to cover the bios image */
	if (!(priv = kmalloc(sizeof(*priv), GFP_KERNEL))) {
		nvkm_error(subdev, "... out of memory\n");
		return ERR_PTR(-ENOMEM);
	}

	priv->bios = bios;
	priv->bar0 = nvkm_rd32(device, 0x001700);
	nvkm_wr32(device, 0x001700, addr >> 16);
	return priv;
}

const struct nvbios_source
nvbios_ramin = {
	.name = "PRAMIN",
	.init = pramin_init,
	.fini = pramin_fini,
	.read = pramin_read,
	.rw = true,
};
