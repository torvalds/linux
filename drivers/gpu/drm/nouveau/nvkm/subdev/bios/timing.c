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

u32
nvbios_timingTe(struct nvkm_bios *bios,
		u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *snr, u8 *ssz)
{
	struct bit_entry bit_P;
	u32 timing = 0;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version == 1)
			timing = nvbios_rd32(bios, bit_P.offset + 4);
		else
		if (bit_P.version == 2)
			timing = nvbios_rd32(bios, bit_P.offset + 8);

		if (timing) {
			*ver = nvbios_rd08(bios, timing + 0);
			switch (*ver) {
			case 0x10:
				*hdr = nvbios_rd08(bios, timing + 1);
				*cnt = nvbios_rd08(bios, timing + 2);
				*len = nvbios_rd08(bios, timing + 3);
				*snr = 0;
				*ssz = 0;
				return timing;
			case 0x20:
				*hdr = nvbios_rd08(bios, timing + 1);
				*cnt = nvbios_rd08(bios, timing + 5);
				*len = nvbios_rd08(bios, timing + 2);
				*snr = nvbios_rd08(bios, timing + 4);
				*ssz = nvbios_rd08(bios, timing + 3);
				return timing;
			default:
				break;
			}
		}
	}

	return 0;
}

u32
nvbios_timingEe(struct nvkm_bios *bios, int idx,
		u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u8  snr, ssz;
	u32 timing = nvbios_timingTe(bios, ver, hdr, cnt, len, &snr, &ssz);
	if (timing && idx < *cnt) {
		timing += *hdr + idx * (*len + (snr * ssz));
		*hdr = *len;
		*cnt = snr;
		*len = ssz;
		return timing;
	}
	return 0;
}

u32
nvbios_timingEp(struct nvkm_bios *bios, int idx,
		u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_ramcfg *p)
{
	u32 data = nvbios_timingEe(bios, idx, ver, hdr, cnt, len), temp;
	p->timing_ver = *ver;
	p->timing_hdr = *hdr;
	switch (!!data * *ver) {
	case 0x10:
		p->timing_10_WR    = nvbios_rd08(bios, data + 0x00);
		p->timing_10_WTR   = nvbios_rd08(bios, data + 0x01);
		p->timing_10_CL    = nvbios_rd08(bios, data + 0x02);
		p->timing_10_RC    = nvbios_rd08(bios, data + 0x03);
		p->timing_10_RFC   = nvbios_rd08(bios, data + 0x05);
		p->timing_10_RAS   = nvbios_rd08(bios, data + 0x07);
		p->timing_10_RP    = nvbios_rd08(bios, data + 0x09);
		p->timing_10_RCDRD = nvbios_rd08(bios, data + 0x0a);
		p->timing_10_RCDWR = nvbios_rd08(bios, data + 0x0b);
		p->timing_10_RRD   = nvbios_rd08(bios, data + 0x0c);
		p->timing_10_13    = nvbios_rd08(bios, data + 0x0d);
		p->timing_10_ODT   = nvbios_rd08(bios, data + 0x0e) & 0x07;
		if (p->ramcfg_ver >= 0x10)
			p->ramcfg_RON = nvbios_rd08(bios, data + 0x0e) & 0x07;

		p->timing_10_24  = 0xff;
		p->timing_10_21  = 0;
		p->timing_10_20  = 0;
		p->timing_10_CWL = 0;
		p->timing_10_18  = 0;
		p->timing_10_16  = 0;

		switch (min_t(u8, *hdr, 25)) {
		case 25:
			p->timing_10_24  = nvbios_rd08(bios, data + 0x18);
		case 24:
		case 23:
		case 22:
			p->timing_10_21  = nvbios_rd08(bios, data + 0x15);
		case 21:
			p->timing_10_20  = nvbios_rd08(bios, data + 0x14);
		case 20:
			p->timing_10_CWL = nvbios_rd08(bios, data + 0x13);
		case 19:
			p->timing_10_18  = nvbios_rd08(bios, data + 0x12);
		case 18:
		case 17:
			p->timing_10_16  = nvbios_rd08(bios, data + 0x10);
		}

		break;
	case 0x20:
		p->timing[0] = nvbios_rd32(bios, data + 0x00);
		p->timing[1] = nvbios_rd32(bios, data + 0x04);
		p->timing[2] = nvbios_rd32(bios, data + 0x08);
		p->timing[3] = nvbios_rd32(bios, data + 0x0c);
		p->timing[4] = nvbios_rd32(bios, data + 0x10);
		p->timing[5] = nvbios_rd32(bios, data + 0x14);
		p->timing[6] = nvbios_rd32(bios, data + 0x18);
		p->timing[7] = nvbios_rd32(bios, data + 0x1c);
		p->timing[8] = nvbios_rd32(bios, data + 0x20);
		p->timing[9] = nvbios_rd32(bios, data + 0x24);
		p->timing[10] = nvbios_rd32(bios, data + 0x28);
		p->timing_20_2e_03 = (nvbios_rd08(bios, data + 0x2e) & 0x03) >> 0;
		p->timing_20_2e_30 = (nvbios_rd08(bios, data + 0x2e) & 0x30) >> 4;
		p->timing_20_2e_c0 = (nvbios_rd08(bios, data + 0x2e) & 0xc0) >> 6;
		p->timing_20_2f_03 = (nvbios_rd08(bios, data + 0x2f) & 0x03) >> 0;
		temp = nvbios_rd16(bios, data + 0x2c);
		p->timing_20_2c_003f = (temp & 0x003f) >> 0;
		p->timing_20_2c_1fc0 = (temp & 0x1fc0) >> 6;
		p->timing_20_30_07 = (nvbios_rd08(bios, data + 0x30) & 0x07) >> 0;
		p->timing_20_30_f8 = (nvbios_rd08(bios, data + 0x30) & 0xf8) >> 3;
		temp = nvbios_rd16(bios, data + 0x31);
		p->timing_20_31_0007 = (temp & 0x0007) >> 0;
		p->timing_20_31_0078 = (temp & 0x0078) >> 3;
		p->timing_20_31_0780 = (temp & 0x0780) >> 7;
		p->timing_20_31_0800 = (temp & 0x0800) >> 11;
		p->timing_20_31_7000 = (temp & 0x7000) >> 12;
		p->timing_20_31_8000 = (temp & 0x8000) >> 15;
		break;
	default:
		data = 0;
		break;
	}
	return data;
}
