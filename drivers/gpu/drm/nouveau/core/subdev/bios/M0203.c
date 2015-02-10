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
 * Authors: Ben Skeggs
 */

#include <subdev/bios.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/M0203.h>

u32
nvbios_M0203Te(struct nouveau_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct bit_entry bit_M;
	u32 data = 0x00000000;

	if (!bit_entry(bios, 'M', &bit_M)) {
		if (bit_M.version == 2 && bit_M.length > 0x04)
			data = nv_ro16(bios, bit_M.offset + 0x03);
		if (data) {
			*ver = nv_ro08(bios, data + 0x00);
			switch (*ver) {
			case 0x10:
				*hdr = nv_ro08(bios, data + 0x01);
				*len = nv_ro08(bios, data + 0x02);
				*cnt = nv_ro08(bios, data + 0x03);
				return data;
			default:
				break;
			}
		}
	}

	return 0x00000000;
}

u32
nvbios_M0203Tp(struct nouveau_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
	       struct nvbios_M0203T *info)
{
	u32 data = nvbios_M0203Te(bios, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x10:
		info->type    = nv_ro08(bios, data + 0x04);
		info->pointer = nv_ro16(bios, data + 0x05);
		break;
	default:
		break;
	}
	return data;
}

u32
nvbios_M0203Ee(struct nouveau_bios *bios, int idx, u8 *ver, u8 *hdr)
{
	u8  cnt, len;
	u32 data = nvbios_M0203Te(bios, ver, hdr, &cnt, &len);
	if (data && idx < cnt) {
		data = data + *hdr + idx * len;
		*hdr = len;
		return data;
	}
	return 0x00000000;
}

u32
nvbios_M0203Ep(struct nouveau_bios *bios, int idx, u8 *ver, u8 *hdr,
	       struct nvbios_M0203E *info)
{
	u32 data = nvbios_M0203Ee(bios, idx, ver, hdr);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x10:
		info->type  = (nv_ro08(bios, data + 0x00) & 0x0f) >> 0;
		info->strap = (nv_ro08(bios, data + 0x00) & 0xf0) >> 4;
		info->group = (nv_ro08(bios, data + 0x01) & 0x0f) >> 0;
		return data;
	default:
		break;
	}
	return 0x00000000;
}

u32
nvbios_M0203Em(struct nouveau_bios *bios, u8 ramcfg, u8 *ver, u8 *hdr,
	       struct nvbios_M0203E *info)
{
	struct nvbios_M0203T M0203T;
	u8  cnt, len, idx = 0xff;
	u32 data;

	if (!nvbios_M0203Tp(bios, ver, hdr, &cnt, &len, &M0203T)) {
		nv_warn(bios, "M0203T not found\n");
		return 0x00000000;
	}

	while ((data = nvbios_M0203Ep(bios, ++idx, ver, hdr, info))) {
		switch (M0203T.type) {
		case M0203T_TYPE_RAMCFG:
			if (info->strap != ramcfg)
				continue;
			return data;
		default:
			nv_warn(bios, "M0203T type %02x\n", M0203T.type);
			return 0x00000000;
		}
	}

	return data;
}
