/*
 * Copyright 2012 Nouveau Community
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
 * Authors: Martin Peres
 */

#include <subdev/bios.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/perf.h>

static u16
perf_table(struct nouveau_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct bit_entry bit_P;
	u16 perf = 0x0000;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version <= 2) {
			perf = nv_ro16(bios, bit_P.offset + 0);
			if (perf) {
				*ver = nv_ro08(bios, perf + 0);
				*hdr = nv_ro08(bios, perf + 1);
			}
		} else
			nv_error(bios, "unknown offset for perf in BIT P %d\n",
				bit_P.version);
	}

	if (bios->bmp_offset) {
		if (nv_ro08(bios, bios->bmp_offset + 6) >= 0x25) {
			perf = nv_ro16(bios, bios->bmp_offset + 0x94);
			if (perf) {
				*hdr = nv_ro08(bios, perf + 0);
				*ver = nv_ro08(bios, perf + 1);
			}
		}
	}

	return perf;
}

int
nvbios_perf_fan_parse(struct nouveau_bios *bios,
		      struct nvbios_perf_fan *fan)
{
	u8 ver = 0, hdr = 0, cnt = 0, len = 0;
	u16 perf = perf_table(bios, &ver, &hdr, &cnt, &len);
	if (!perf)
		return -ENODEV;

	if (ver >= 0x20 && ver < 0x40 && hdr > 6)
		fan->pwm_divisor = nv_ro16(bios, perf + 6);
	else
		fan->pwm_divisor = 0;

	return 0;
}
