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

#include <subdev/bios.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/ramcfg.h>
#include <subdev/bios/P0260.h>

u32
nvbios_P0260Te(struct nouveau_bios *bios,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *xnr, u8 *xsz)
{
	struct bit_entry bit_P;
	u32 data = 0x00000000;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version == 2 && bit_P.length > 0x63)
			data = nv_ro32(bios, bit_P.offset + 0x60);
		if (data) {
			*ver = nv_ro08(bios, data + 0);
			switch (*ver) {
			case 0x10:
				*hdr = nv_ro08(bios, data + 1);
				*cnt = nv_ro08(bios, data + 2);
				*len = 4;
				*xnr = nv_ro08(bios, data + 3);
				*xsz = 4;
				return data;
			default:
				break;
			}
		}
	}

	return 0x00000000;
}

u32
nvbios_P0260Ee(struct nouveau_bios *bios, int idx, u8 *ver, u8 *len)
{
	u8  hdr, cnt, xnr, xsz;
	u32 data = nvbios_P0260Te(bios, ver, &hdr, &cnt, len, &xnr, &xsz);
	if (data && idx < cnt)
		return data + hdr + (idx * *len);
	return 0x00000000;
}

u32
nvbios_P0260Ep(struct nouveau_bios *bios, int idx, u8 *ver, u8 *len,
	       struct nvbios_P0260E *info)
{
	u32 data = nvbios_P0260Ee(bios, idx, ver, len);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x10:
		info->data = nv_ro32(bios, data);
		return data;
	default:
		break;
	}
	return 0x00000000;
}

u32
nvbios_P0260Xe(struct nouveau_bios *bios, int idx, u8 *ver, u8 *xsz)
{
	u8  hdr, cnt, len, xnr;
	u32 data = nvbios_P0260Te(bios, ver, &hdr, &cnt, &len, &xnr, xsz);
	if (data && idx < xnr)
		return data + hdr + (cnt * len) + (idx * *xsz);
	return 0x00000000;
}

u32
nvbios_P0260Xp(struct nouveau_bios *bios, int idx, u8 *ver, u8 *hdr,
	       struct nvbios_P0260X *info)
{
	u32 data = nvbios_P0260Xe(bios, idx, ver, hdr);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x10:
		info->data = nv_ro32(bios, data);
		return data;
	default:
		break;
	}
	return 0x00000000;
}
