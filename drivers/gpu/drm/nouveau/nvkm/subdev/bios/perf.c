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

#include <core/device.h>

u16
nvbios_perf_table(struct nvkm_bios *bios, u8 *ver, u8 *hdr,
		  u8 *cnt, u8 *len, u8 *snr, u8 *ssz)
{
	struct bit_entry bit_P;
	u16 perf = 0x0000;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version <= 2) {
			perf = nv_ro16(bios, bit_P.offset + 0);
			if (perf) {
				*ver = nv_ro08(bios, perf + 0);
				*hdr = nv_ro08(bios, perf + 1);
				if (*ver >= 0x40 && *ver < 0x41) {
					*cnt = nv_ro08(bios, perf + 5);
					*len = nv_ro08(bios, perf + 2);
					*snr = nv_ro08(bios, perf + 4);
					*ssz = nv_ro08(bios, perf + 3);
					return perf;
				} else
				if (*ver >= 0x20 && *ver < 0x40) {
					*cnt = nv_ro08(bios, perf + 2);
					*len = nv_ro08(bios, perf + 3);
					*snr = nv_ro08(bios, perf + 4);
					*ssz = nv_ro08(bios, perf + 5);
					return perf;
				}
			}
		}
	}

	if (bios->bmp_offset) {
		if (nv_ro08(bios, bios->bmp_offset + 6) >= 0x25) {
			perf = nv_ro16(bios, bios->bmp_offset + 0x94);
			if (perf) {
				*hdr = nv_ro08(bios, perf + 0);
				*ver = nv_ro08(bios, perf + 1);
				*cnt = nv_ro08(bios, perf + 2);
				*len = nv_ro08(bios, perf + 3);
				*snr = 0;
				*ssz = 0;
				return perf;
			}
		}
	}

	return 0x0000;
}

u16
nvbios_perf_entry(struct nvkm_bios *bios, int idx,
		  u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u8  snr, ssz;
	u16 perf = nvbios_perf_table(bios, ver, hdr, cnt, len, &snr, &ssz);
	if (perf && idx < *cnt) {
		perf = perf + *hdr + (idx * (*len + (snr * ssz)));
		*hdr = *len;
		*cnt = snr;
		*len = ssz;
		return perf;
	}
	return 0x0000;
}

u16
nvbios_perfEp(struct nvkm_bios *bios, int idx,
	      u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_perfE *info)
{
	u16 perf = nvbios_perf_entry(bios, idx, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	info->pstate = nv_ro08(bios, perf + 0x00);
	switch (!!perf * *ver) {
	case 0x12:
	case 0x13:
	case 0x14:
		info->core     = nv_ro32(bios, perf + 0x01) * 10;
		info->memory   = nv_ro32(bios, perf + 0x05) * 20;
		info->fanspeed = nv_ro08(bios, perf + 0x37);
		if (*hdr > 0x38)
			info->voltage = nv_ro08(bios, perf + 0x38);
		break;
	case 0x21:
	case 0x23:
	case 0x24:
		info->fanspeed = nv_ro08(bios, perf + 0x04);
		info->voltage  = nv_ro08(bios, perf + 0x05);
		info->shader   = nv_ro16(bios, perf + 0x06) * 1000;
		info->core     = info->shader + (signed char)
				 nv_ro08(bios, perf + 0x08) * 1000;
		switch (nv_device(bios)->chipset) {
		case 0x49:
		case 0x4b:
			info->memory = nv_ro16(bios, perf + 0x0b) * 1000;
			break;
		default:
			info->memory = nv_ro16(bios, perf + 0x0b) * 2000;
			break;
		}
		break;
	case 0x25:
		info->fanspeed = nv_ro08(bios, perf + 0x04);
		info->voltage  = nv_ro08(bios, perf + 0x05);
		info->core     = nv_ro16(bios, perf + 0x06) * 1000;
		info->shader   = nv_ro16(bios, perf + 0x0a) * 1000;
		info->memory   = nv_ro16(bios, perf + 0x0c) * 1000;
		break;
	case 0x30:
		info->script   = nv_ro16(bios, perf + 0x02);
	case 0x35:
		info->fanspeed = nv_ro08(bios, perf + 0x06);
		info->voltage  = nv_ro08(bios, perf + 0x07);
		info->core     = nv_ro16(bios, perf + 0x08) * 1000;
		info->shader   = nv_ro16(bios, perf + 0x0a) * 1000;
		info->memory   = nv_ro16(bios, perf + 0x0c) * 1000;
		info->vdec     = nv_ro16(bios, perf + 0x10) * 1000;
		info->disp     = nv_ro16(bios, perf + 0x14) * 1000;
		break;
	case 0x40:
		info->voltage  = nv_ro08(bios, perf + 0x02);
		break;
	default:
		return 0x0000;
	}
	return perf;
}

u32
nvbios_perfSe(struct nvkm_bios *bios, u32 perfE, int idx,
	      u8 *ver, u8 *hdr, u8 cnt, u8 len)
{
	u32 data = 0x00000000;
	if (idx < cnt) {
		data = perfE + *hdr + (idx * len);
		*hdr = len;
	}
	return data;
}

u32
nvbios_perfSp(struct nvkm_bios *bios, u32 perfE, int idx,
	      u8 *ver, u8 *hdr, u8 cnt, u8 len,
	      struct nvbios_perfS *info)
{
	u32 data = nvbios_perfSe(bios, perfE, idx, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x40:
		info->v40.freq = (nv_ro16(bios, data + 0x00) & 0x3fff) * 1000;
		break;
	default:
		break;
	}
	return data;
}

int
nvbios_perf_fan_parse(struct nvkm_bios *bios,
		      struct nvbios_perf_fan *fan)
{
	u8  ver, hdr, cnt, len, snr, ssz;
	u16 perf = nvbios_perf_table(bios, &ver, &hdr, &cnt, &len, &snr, &ssz);
	if (!perf)
		return -ENODEV;

	if (ver >= 0x20 && ver < 0x40 && hdr > 6)
		fan->pwm_divisor = nv_ro16(bios, perf + 6);
	else
		fan->pwm_divisor = 0;

	return 0;
}
