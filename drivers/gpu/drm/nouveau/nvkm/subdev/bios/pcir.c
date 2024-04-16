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
#include <subdev/bios/pcir.h>

u32
nvbios_pcirTe(struct nvkm_bios *bios, u32 base, u8 *ver, u16 *hdr)
{
	u32 data = nvbios_rd16(bios, base + 0x18);
	if (data) {
		data += base;
		switch (nvbios_rd32(bios, data + 0x00)) {
		case 0x52494350: /* PCIR */
		case 0x53494752: /* RGIS */
		case 0x5344504e: /* NPDS */
			*hdr = nvbios_rd16(bios, data + 0x0a);
			*ver = nvbios_rd08(bios, data + 0x0c);
			break;
		default:
			nvkm_debug(&bios->subdev,
				   "%08x: PCIR signature (%08x) unknown\n",
				   data, nvbios_rd32(bios, data + 0x00));
			data = 0;
			break;
		}
	}
	return data;
}

u32
nvbios_pcirTp(struct nvkm_bios *bios, u32 base, u8 *ver, u16 *hdr,
	      struct nvbios_pcirT *info)
{
	u32 data = nvbios_pcirTe(bios, base, ver, hdr);
	memset(info, 0x00, sizeof(*info));
	if (data) {
		info->vendor_id = nvbios_rd16(bios, data + 0x04);
		info->device_id = nvbios_rd16(bios, data + 0x06);
		info->class_code[0] = nvbios_rd08(bios, data + 0x0d);
		info->class_code[1] = nvbios_rd08(bios, data + 0x0e);
		info->class_code[2] = nvbios_rd08(bios, data + 0x0f);
		info->image_size = nvbios_rd16(bios, data + 0x10) * 512;
		info->image_rev = nvbios_rd16(bios, data + 0x12);
		info->image_type = nvbios_rd08(bios, data + 0x14);
		info->last = nvbios_rd08(bios, data + 0x15) & 0x80;
	}
	return data;
}
