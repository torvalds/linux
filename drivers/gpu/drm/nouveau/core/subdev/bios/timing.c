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
#include <subdev/bios/timing.h>

u16
nvbios_timing_table(struct nouveau_bios *bios,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct bit_entry bit_P;
	u16 timing = 0x0000;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version == 1)
			timing = nv_ro16(bios, bit_P.offset + 4);
		else
		if (bit_P.version == 2)
			timing = nv_ro16(bios, bit_P.offset + 8);

		if (timing) {
			*ver = nv_ro08(bios, timing + 0);
			switch (*ver) {
			case 0x10:
				*hdr = nv_ro08(bios, timing + 1);
				*cnt = nv_ro08(bios, timing + 2);
				*len = nv_ro08(bios, timing + 3);
				return timing;
			case 0x20:
				*hdr = nv_ro08(bios, timing + 1);
				*cnt = nv_ro08(bios, timing + 3);
				*len = nv_ro08(bios, timing + 2);
				return timing;
			default:
				break;
			}
		}
	}

	return 0x0000;
}

u16
nvbios_timing_entry(struct nouveau_bios *bios, int idx, u8 *ver, u8 *len)
{
	u8  hdr, cnt;
	u16 timing = nvbios_timing_table(bios, ver, &hdr, &cnt, len);
	if (timing && idx < cnt)
		return timing + hdr + (idx * *len);
	return 0x0000;
}
