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
#include <subdev/bios/volt.h>

u16
nvbios_volt_table(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct bit_entry bit_P;
	u16 volt = 0x0000;

	if (!bit_entry(bios, 'P', &bit_P)) {
		if (bit_P.version == 2)
			volt = nv_ro16(bios, bit_P.offset + 0x0c);
		else
		if (bit_P.version == 1)
			volt = nv_ro16(bios, bit_P.offset + 0x10);

		if (volt) {
			*ver = nv_ro08(bios, volt + 0);
			switch (*ver) {
			case 0x12:
				*hdr = 5;
				*cnt = nv_ro08(bios, volt + 2);
				*len = nv_ro08(bios, volt + 1);
				return volt;
			case 0x20:
				*hdr = nv_ro08(bios, volt + 1);
				*cnt = nv_ro08(bios, volt + 2);
				*len = nv_ro08(bios, volt + 3);
				return volt;
			case 0x30:
			case 0x40:
			case 0x50:
				*hdr = nv_ro08(bios, volt + 1);
				*cnt = nv_ro08(bios, volt + 3);
				*len = nv_ro08(bios, volt + 2);
				return volt;
			}
		}
	}

	return 0x0000;
}

u16
nvbios_volt_parse(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		  struct nvbios_volt *info)
{
	u16 volt = nvbios_volt_table(bios, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	switch (!!volt * *ver) {
	case 0x12:
		info->vidmask = nv_ro08(bios, volt + 0x04);
		break;
	case 0x20:
		info->vidmask = nv_ro08(bios, volt + 0x05);
		break;
	case 0x30:
		info->vidmask = nv_ro08(bios, volt + 0x04);
		break;
	case 0x40:
		info->base    = nv_ro32(bios, volt + 0x04);
		info->step    = nv_ro16(bios, volt + 0x08);
		info->vidmask = nv_ro08(bios, volt + 0x0b);
		/*XXX*/
		info->min     = 0;
		info->max     = info->base;
		break;
	case 0x50:
		info->vidmask = nv_ro08(bios, volt + 0x06);
		info->min     = nv_ro32(bios, volt + 0x0a);
		info->max     = nv_ro32(bios, volt + 0x0e);
		info->base    = nv_ro32(bios, volt + 0x12) & 0x00ffffff;
		info->step    = nv_ro16(bios, volt + 0x16);
		break;
	}
	return volt;
}

u16
nvbios_volt_entry(struct nvkm_bios *bios, int idx, u8 *ver, u8 *len)
{
	u8  hdr, cnt;
	u16 volt = nvbios_volt_table(bios, ver, &hdr, &cnt, len);
	if (volt && idx < cnt) {
		volt = volt + hdr + (idx * *len);
		return volt;
	}
	return 0x0000;
}

u16
nvbios_volt_entry_parse(struct nvkm_bios *bios, int idx, u8 *ver, u8 *len,
			struct nvbios_volt_entry *info)
{
	u16 volt = nvbios_volt_entry(bios, idx, ver, len);
	memset(info, 0x00, sizeof(*info));
	switch (!!volt * *ver) {
	case 0x12:
	case 0x20:
		info->voltage = nv_ro08(bios, volt + 0x00) * 10000;
		info->vid     = nv_ro08(bios, volt + 0x01);
		break;
	case 0x30:
		info->voltage = nv_ro08(bios, volt + 0x00) * 10000;
		info->vid     = nv_ro08(bios, volt + 0x01) >> 2;
		break;
	case 0x40:
	case 0x50:
		break;
	}
	return volt;
}
