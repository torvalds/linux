/*
 * Copyright 2012 Red Hat Inc.
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
#include <subdev/bios/dp.h>

static u16
nvbios_dp_table(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct bit_entry d;

	if (!bit_entry(bios, 'd', &d)) {
		if (d.version == 1 && d.length >= 2) {
			u16 data = nvbios_rd16(bios, d.offset);
			if (data) {
				*ver = nvbios_rd08(bios, data + 0x00);
				switch (*ver) {
				case 0x20:
				case 0x21:
				case 0x30:
				case 0x40:
				case 0x41:
				case 0x42:
					*hdr = nvbios_rd08(bios, data + 0x01);
					*len = nvbios_rd08(bios, data + 0x02);
					*cnt = nvbios_rd08(bios, data + 0x03);
					return data;
				default:
					break;
				}
			}
		}
	}

	return 0x0000;
}

static u16
nvbios_dpout_entry(struct nvkm_bios *bios, u8 idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u16 data = nvbios_dp_table(bios, ver, hdr, cnt, len);
	if (data && idx < *cnt) {
		u16 outp = nvbios_rd16(bios, data + *hdr + idx * *len);
		switch (*ver * !!outp) {
		case 0x20:
		case 0x21:
		case 0x30:
			*hdr = nvbios_rd08(bios, data + 0x04);
			*len = nvbios_rd08(bios, data + 0x05);
			*cnt = nvbios_rd08(bios, outp + 0x04);
			break;
		case 0x40:
		case 0x41:
		case 0x42:
			*hdr = nvbios_rd08(bios, data + 0x04);
			*cnt = 0;
			*len = 0;
			break;
		default:
			break;
		}
		return outp;
	}
	*ver = 0x00;
	return 0x0000;
}

u16
nvbios_dpout_parse(struct nvkm_bios *bios, u8 idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		   struct nvbios_dpout *info)
{
	u16 data = nvbios_dpout_entry(bios, idx, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	if (data && *ver) {
		info->type = nvbios_rd16(bios, data + 0x00);
		info->mask = nvbios_rd16(bios, data + 0x02);
		switch (*ver) {
		case 0x20:
			info->mask |= 0x00c0; /* match any link */
			/* fall-through */
		case 0x21:
		case 0x30:
			info->flags     = nvbios_rd08(bios, data + 0x05);
			info->script[0] = nvbios_rd16(bios, data + 0x06);
			info->script[1] = nvbios_rd16(bios, data + 0x08);
			if (*len >= 0x0c)
				info->lnkcmp    = nvbios_rd16(bios, data + 0x0a);
			if (*len >= 0x0f) {
				info->script[2] = nvbios_rd16(bios, data + 0x0c);
				info->script[3] = nvbios_rd16(bios, data + 0x0e);
			}
			if (*len >= 0x11)
				info->script[4] = nvbios_rd16(bios, data + 0x10);
			break;
		case 0x40:
		case 0x41:
		case 0x42:
			info->flags     = nvbios_rd08(bios, data + 0x04);
			info->script[0] = nvbios_rd16(bios, data + 0x05);
			info->script[1] = nvbios_rd16(bios, data + 0x07);
			info->lnkcmp    = nvbios_rd16(bios, data + 0x09);
			info->script[2] = nvbios_rd16(bios, data + 0x0b);
			info->script[3] = nvbios_rd16(bios, data + 0x0d);
			info->script[4] = nvbios_rd16(bios, data + 0x0f);
			break;
		default:
			data = 0x0000;
			break;
		}
	}
	return data;
}

u16
nvbios_dpout_match(struct nvkm_bios *bios, u16 type, u16 mask,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		   struct nvbios_dpout *info)
{
	u16 data, idx = 0;
	while ((data = nvbios_dpout_parse(bios, idx++, ver, hdr, cnt, len, info)) || *ver) {
		if (data && info->type == type) {
			if ((info->mask & mask) == mask)
				break;
		}
	}
	return data;
}

static u16
nvbios_dpcfg_entry(struct nvkm_bios *bios, u16 outp, u8 idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	if (*ver >= 0x40) {
		outp = nvbios_dp_table(bios, ver, hdr, cnt, len);
		*hdr = *hdr + (*len * * cnt);
		*len = nvbios_rd08(bios, outp + 0x06);
		*cnt = nvbios_rd08(bios, outp + 0x07) *
		       nvbios_rd08(bios, outp + 0x05);
	}

	if (idx < *cnt)
		return outp + *hdr + (idx * *len);

	return 0x0000;
}

u16
nvbios_dpcfg_parse(struct nvkm_bios *bios, u16 outp, u8 idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		   struct nvbios_dpcfg *info)
{
	u16 data = nvbios_dpcfg_entry(bios, outp, idx, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	if (data) {
		switch (*ver) {
		case 0x20:
		case 0x21:
			info->dc    = nvbios_rd08(bios, data + 0x02);
			info->pe    = nvbios_rd08(bios, data + 0x03);
			info->tx_pu = nvbios_rd08(bios, data + 0x04);
			break;
		case 0x30:
		case 0x40:
		case 0x41:
			info->pc    = nvbios_rd08(bios, data + 0x00);
			info->dc    = nvbios_rd08(bios, data + 0x01);
			info->pe    = nvbios_rd08(bios, data + 0x02);
			info->tx_pu = nvbios_rd08(bios, data + 0x03);
			break;
		case 0x42:
			info->dc    = nvbios_rd08(bios, data + 0x00);
			info->pe    = nvbios_rd08(bios, data + 0x01);
			info->tx_pu = nvbios_rd08(bios, data + 0x02);
			break;
		default:
			data = 0x0000;
			break;
		}
	}
	return data;
}

u16
nvbios_dpcfg_match(struct nvkm_bios *bios, u16 outp, u8 pc, u8 vs, u8 pe,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		   struct nvbios_dpcfg *info)
{
	u8 idx = 0xff;
	u16 data;

	if (*ver >= 0x30) {
		const u8 vsoff[] = { 0, 4, 7, 9 };
		idx = (pc * 10) + vsoff[vs] + pe;
		if (*ver >= 0x40 && *ver <= 0x41 && *hdr >= 0x12)
			idx += nvbios_rd08(bios, outp + 0x11) * 40;
		else
		if (*ver >= 0x42)
			idx += nvbios_rd08(bios, outp + 0x11) * 10;
	} else {
		while ((data = nvbios_dpcfg_entry(bios, outp, ++idx,
						  ver, hdr, cnt, len))) {
			if (nvbios_rd08(bios, data + 0x00) == vs &&
			    nvbios_rd08(bios, data + 0x01) == pe)
				break;
		}
	}

	return nvbios_dpcfg_parse(bios, outp, idx, ver, hdr, cnt, len, info);
}
