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
#include <subdev/bios/disp.h>

u16
nvbios_disp_table(struct nvkm_bios *bios,
		  u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *sub)
{
	struct bit_entry U;

	if (!bit_entry(bios, 'U', &U)) {
		if (U.version == 1) {
			u16 data = nv_ro16(bios, U.offset);
			if (data) {
				*ver = nv_ro08(bios, data + 0x00);
				switch (*ver) {
				case 0x20:
				case 0x21:
				case 0x22:
					*hdr = nv_ro08(bios, data + 0x01);
					*len = nv_ro08(bios, data + 0x02);
					*cnt = nv_ro08(bios, data + 0x03);
					*sub = nv_ro08(bios, data + 0x04);
					return data;
				default:
					break;
				}
			}
		}
	}

	return 0x0000;
}

u16
nvbios_disp_entry(struct nvkm_bios *bios, u8 idx, u8 *ver, u8 *len, u8 *sub)
{
	u8  hdr, cnt;
	u16 data = nvbios_disp_table(bios, ver, &hdr, &cnt, len, sub);
	if (data && idx < cnt)
		return data + hdr + (idx * *len);
	*ver = 0x00;
	return 0x0000;
}

u16
nvbios_disp_parse(struct nvkm_bios *bios, u8 idx, u8 *ver, u8 *len, u8 *sub,
		  struct nvbios_disp *info)
{
	u16 data = nvbios_disp_entry(bios, idx, ver, len, sub);
	if (data && *len >= 2) {
		info->data = nv_ro16(bios, data + 0);
		return data;
	}
	return 0x0000;
}

u16
nvbios_outp_entry(struct nvkm_bios *bios, u8 idx,
		  u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	struct nvbios_disp info;
	u16 data = nvbios_disp_parse(bios, idx, ver, len, hdr, &info);
	if (data) {
		*cnt = nv_ro08(bios, info.data + 0x05);
		*len = 0x06;
		data = info.data;
	}
	return data;
}

u16
nvbios_outp_parse(struct nvkm_bios *bios, u8 idx,
		  u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_outp *info)
{
	u16 data = nvbios_outp_entry(bios, idx, ver, hdr, cnt, len);
	if (data && *hdr >= 0x0a) {
		info->type      = nv_ro16(bios, data + 0x00);
		info->mask      = nv_ro32(bios, data + 0x02);
		if (*ver <= 0x20) /* match any link */
			info->mask |= 0x00c0;
		info->script[0] = nv_ro16(bios, data + 0x06);
		info->script[1] = nv_ro16(bios, data + 0x08);
		info->script[2] = 0x0000;
		if (*hdr >= 0x0c)
			info->script[2] = nv_ro16(bios, data + 0x0a);
		return data;
	}
	return 0x0000;
}

u16
nvbios_outp_match(struct nvkm_bios *bios, u16 type, u16 mask,
		  u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_outp *info)
{
	u16 data, idx = 0;
	while ((data = nvbios_outp_parse(bios, idx++, ver, hdr, cnt, len, info)) || *ver) {
		if (data && info->type == type) {
			if ((info->mask & mask) == mask)
				break;
		}
	}
	return data;
}

u16
nvbios_ocfg_entry(struct nvkm_bios *bios, u16 outp, u8 idx,
		  u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	if (idx < *cnt)
		return outp + *hdr + (idx * *len);
	return 0x0000;
}

u16
nvbios_ocfg_parse(struct nvkm_bios *bios, u16 outp, u8 idx,
		  u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_ocfg *info)
{
	u16 data = nvbios_ocfg_entry(bios, outp, idx, ver, hdr, cnt, len);
	if (data) {
		info->match     = nv_ro16(bios, data + 0x00);
		info->clkcmp[0] = nv_ro16(bios, data + 0x02);
		info->clkcmp[1] = nv_ro16(bios, data + 0x04);
	}
	return data;
}

u16
nvbios_ocfg_match(struct nvkm_bios *bios, u16 outp, u16 type,
		  u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_ocfg *info)
{
	u16 data, idx = 0;
	while ((data = nvbios_ocfg_parse(bios, outp, idx++, ver, hdr, cnt, len, info))) {
		if (info->match == type)
			break;
	}
	return data;
}

u16
nvbios_oclk_match(struct nvkm_bios *bios, u16 cmp, u32 khz)
{
	while (cmp) {
		if (khz / 10 >= nv_ro16(bios, cmp + 0x00))
			return  nv_ro16(bios, cmp + 0x02);
		cmp += 0x04;
	}
	return 0x0000;
}
