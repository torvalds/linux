/*
 * Copyright 2014 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include <subdev/bios.h>
#include <subdev/bios/image.h>
#include <subdev/bios/pcir.h>
#include <subdev/bios/npde.h>

static bool
nvbios_imagen(struct nvkm_bios *bios, struct nvbios_image *image)
{
	struct nvkm_subdev *subdev = &bios->subdev;
	struct nvbios_pcirT pcir;
	struct nvbios_npdeT npde;
	u8  ver;
	u16 hdr;
	u32 data;

	switch ((data = nvbios_rd16(bios, image->base + 0x00))) {
	case 0xaa55:
	case 0xbb77:
	case 0x4e56: /* NV */
		break;
	default:
		nvkm_debug(subdev, "%08x: ROM signature (%04x) unknown\n",
			   image->base, data);
		return false;
	}

	if (!(data = nvbios_pcirTp(bios, image->base, &ver, &hdr, &pcir)))
		return false;
	image->size = pcir.image_size;
	image->type = pcir.image_type;
	image->last = pcir.last;

	if (image->type != 0x70) {
		if (!(data = nvbios_npdeTp(bios, image->base, &npde)))
			return true;
		image->size = npde.image_size;
		image->last = npde.last;
	} else {
		image->last = true;
	}

	return true;
}

bool
nvbios_image(struct nvkm_bios *bios, int idx, struct nvbios_image *image)
{
	memset(image, 0x00, sizeof(*image));
	do {
		image->base += image->size;
		if (image->last || !nvbios_imagen(bios, image))
			return false;
	} while(idx--);
	return true;
}
