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
#include <subdev/bios/npde.h>
#include <subdev/bios/pcir.h>

u32
nvbios_npdeTe(struct nvkm_bios *bios, u32 base)
{
	struct nvbios_pcirT pcir;
	u8  ver; u16 hdr;
	u32 data = nvbios_pcirTp(bios, base, &ver, &hdr, &pcir);
	if (data = (data + hdr + 0x0f) & ~0x0f, data) {
		switch (nv_ro32(bios, data + 0x00)) {
		case 0x4544504e: /* NPDE */
			break;
		default:
			nv_debug(bios, "%08x: NPDE signature (%08x) unknown\n",
				 data, nv_ro32(bios, data + 0x00));
			data = 0;
			break;
		}
	}
	return data;
}

u32
nvbios_npdeTp(struct nvkm_bios *bios, u32 base, struct nvbios_npdeT *info)
{
	u32 data = nvbios_npdeTe(bios, base);
	memset(info, 0x00, sizeof(*info));
	if (data) {
		info->image_size = nv_ro16(bios, data + 0x08) * 512;
		info->last = nv_ro08(bios, data + 0x0a) & 0x80;
	}
	return data;
}
