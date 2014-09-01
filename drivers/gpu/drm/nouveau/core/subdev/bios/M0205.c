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
#include <subdev/bios/M0205.h>

u32
nvbios_M0205Te(struct nouveau_bios *bios,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *snr, u8 *ssz)
{
	struct bit_entry bit_M;
	u32 data = 0x00000000;

	if (!bit_entry(bios, 'M', &bit_M)) {
		if (bit_M.version == 2 && bit_M.length > 0x08)
			data = nv_ro32(bios, bit_M.offset + 0x05);
		if (data) {
			*ver = nv_ro08(bios, data + 0x00);
			switch (*ver) {
			case 0x10:
				*hdr = nv_ro08(bios, data + 0x01);
				*len = nv_ro08(bios, data + 0x02);
				*ssz = nv_ro08(bios, data + 0x03);
				*snr = nv_ro08(bios, data + 0x04);
				*cnt = nv_ro08(bios, data + 0x05);
				return data;
			default:
				break;
			}
		}
	}

	return 0x00000000;
}

u32
nvbios_M0205Tp(struct nouveau_bios *bios,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *snr, u8 *ssz,
	       struct nvbios_M0205T *info)
{
	u32 data = nvbios_M0205Te(bios, ver, hdr, cnt, len, snr, ssz);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x10:
		info->freq = nv_ro16(bios, data + 0x06);
		break;
	default:
		break;
	}
	return data;
}

u32
nvbios_M0205Ee(struct nouveau_bios *bios, int idx,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u8  snr, ssz;
	u32 data = nvbios_M0205Te(bios, ver, hdr, cnt, len, &snr, &ssz);
	if (data && idx < *cnt) {
		data = data + *hdr + idx * (*len + (snr * ssz));
		*hdr = *len;
		*cnt = snr;
		*len = ssz;
		return data;
	}
	return 0x00000000;
}

u32
nvbios_M0205Ep(struct nouveau_bios *bios, int idx,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
	       struct nvbios_M0205E *info)
{
	u32 data = nvbios_M0205Ee(bios, idx, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x10:
		info->type = nv_ro08(bios, data + 0x00) & 0x0f;
		return data;
	default:
		break;
	}
	return 0x00000000;
}

u32
nvbios_M0205Se(struct nouveau_bios *bios, int ent, int idx, u8 *ver, u8 *hdr)
{

	u8  cnt, len;
	u32 data = nvbios_M0205Ee(bios, ent, ver, hdr, &cnt, &len);
	if (data && idx < cnt) {
		data = data + *hdr + idx * len;
		*hdr = len;
		return data;
	}
	return 0x00000000;
}

u32
nvbios_M0205Sp(struct nouveau_bios *bios, int ent, int idx, u8 *ver, u8 *hdr,
	       struct nvbios_M0205S *info)
{
	u32 data = nvbios_M0205Se(bios, ent, idx, ver, hdr);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x10:
		info->data = nv_ro08(bios, data + 0x00);
		return data;
	default:
		break;
	}
	return 0x00000000;
}
