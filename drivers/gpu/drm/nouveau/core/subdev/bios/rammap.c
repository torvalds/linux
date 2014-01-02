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
#include <subdev/bios/rammap.h>

u16
nvbios_rammap_table(struct nouveau_bios *bios, u8 *ver, u8 *hdr,
		    u8 *cnt, u8 *len, u8 *snr, u8 *ssz)
{
	struct bit_entry bit_P;
	u16 rammap = 0x0000;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version == 2)
			rammap = nv_ro16(bios, bit_P.offset + 4);

		if (rammap) {
			*ver = nv_ro08(bios, rammap + 0);
			switch (*ver) {
			case 0x10:
			case 0x11:
				*hdr = nv_ro08(bios, rammap + 1);
				*cnt = nv_ro08(bios, rammap + 5);
				*len = nv_ro08(bios, rammap + 2);
				*snr = nv_ro08(bios, rammap + 4);
				*ssz = nv_ro08(bios, rammap + 3);
				return rammap;
			default:
				break;
			}
		}
	}

	return 0x0000;
}

u16
nvbios_rammap_entry(struct nouveau_bios *bios, int idx,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u8  snr, ssz;
	u16 rammap = nvbios_rammap_table(bios, ver, hdr, cnt, len, &snr, &ssz);
	if (rammap && idx < *cnt) {
		rammap = rammap + *hdr + (idx * (*len + (snr * ssz)));
		*hdr = *len;
		*cnt = snr;
		*len = ssz;
		return rammap;
	}
	return 0x0000;
}

u16
nvbios_rammap_match(struct nouveau_bios *bios, u16 khz,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	int idx = 0;
	u32 data;
	while ((data = nvbios_rammap_entry(bios, idx++, ver, hdr, cnt, len))) {
		if (khz >= nv_ro16(bios, data + 0x00) &&
		    khz <= nv_ro16(bios, data + 0x02))
			break;
	}
	return data;
}
