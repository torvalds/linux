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
#include <subdev/bios/boost.h>

u32
nvbios_boostTe(struct nvkm_bios *bios,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *snr, u8 *ssz)
{
	struct bit_entry bit_P;
	u32 boost = 0;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version == 2 && bit_P.length >= 0x34)
			boost = nvbios_rd32(bios, bit_P.offset + 0x30);

		if (boost) {
			*ver = nvbios_rd08(bios, boost + 0);
			switch (*ver) {
			case 0x11:
				*hdr = nvbios_rd08(bios, boost + 1);
				*cnt = nvbios_rd08(bios, boost + 5);
				*len = nvbios_rd08(bios, boost + 2);
				*snr = nvbios_rd08(bios, boost + 4);
				*ssz = nvbios_rd08(bios, boost + 3);
				return boost;
			default:
				break;
			}
		}
	}

	return 0;
}

u32
nvbios_boostEe(struct nvkm_bios *bios, int idx,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u8  snr, ssz;
	u32 data = nvbios_boostTe(bios, ver, hdr, cnt, len, &snr, &ssz);
	if (data && idx < *cnt) {
		data = data + *hdr + (idx * (*len + (snr * ssz)));
		*hdr = *len;
		*cnt = snr;
		*len = ssz;
		return data;
	}
	return 0;
}

u32
nvbios_boostEp(struct nvkm_bios *bios, int idx,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_boostE *info)
{
	u32 data = nvbios_boostEe(bios, idx, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	if (data) {
		info->pstate = (nvbios_rd16(bios, data + 0x00) & 0x01e0) >> 5;
		info->min    =  nvbios_rd16(bios, data + 0x02) * 1000;
		info->max    =  nvbios_rd16(bios, data + 0x04) * 1000;
	}
	return data;
}

u32
nvbios_boostEm(struct nvkm_bios *bios, u8 pstate,
	       u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_boostE *info)
{
	u32 data, idx = 0;
	while ((data = nvbios_boostEp(bios, idx++, ver, hdr, cnt, len, info))) {
		if (info->pstate == pstate)
			break;
	}
	return data;
}

u32
nvbios_boostSe(struct nvkm_bios *bios, int idx,
	       u32 data, u8 *ver, u8 *hdr, u8 cnt, u8 len)
{
	if (data && idx < cnt) {
		data = data + *hdr + (idx * len);
		*hdr = len;
		return data;
	}
	return 0;
}

u32
nvbios_boostSp(struct nvkm_bios *bios, int idx,
	       u32 data, u8 *ver, u8 *hdr, u8 cnt, u8 len,
	       struct nvbios_boostS *info)
{
	data = nvbios_boostSe(bios, idx, data, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	if (data) {
		info->domain  = nvbios_rd08(bios, data + 0x00);
		info->percent = nvbios_rd08(bios, data + 0x01);
		info->min     = nvbios_rd16(bios, data + 0x02) * 1000;
		info->max     = nvbios_rd16(bios, data + 0x04) * 1000;
	}
	return data;
}
