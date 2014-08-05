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

#include <core/device.h>

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/conn.h>

u32
nvbios_connTe(struct nouveau_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len)
{
	u32 dcb = dcb_table(bios, ver, hdr, cnt, len);
	if (dcb && *ver >= 0x30 && *hdr >= 0x16) {
		u32 data = nv_ro16(bios, dcb + 0x14);
		if (data) {
			*ver = nv_ro08(bios, data + 0);
			*hdr = nv_ro08(bios, data + 1);
			*cnt = nv_ro08(bios, data + 2);
			*len = nv_ro08(bios, data + 3);
			return data;
		}
	}
	return 0x00000000;
}

u32
nvbios_connTp(struct nouveau_bios *bios, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
	      struct nvbios_connT *info)
{
	u32 data = nvbios_connTe(bios, ver, hdr, cnt, len);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x30:
	case 0x40:
		return data;
	default:
		break;
	}
	return 0x00000000;
}

u32
nvbios_connEe(struct nouveau_bios *bios, u8 idx, u8 *ver, u8 *len)
{
	u8  hdr, cnt;
	u32 data = nvbios_connTe(bios, ver, &hdr, &cnt, len);
	if (data && idx < cnt)
		return data + hdr + (idx * *len);
	return 0x00000000;
}

u32
nvbios_connEp(struct nouveau_bios *bios, u8 idx, u8 *ver, u8 *len,
	      struct nvbios_connE *info)
{
	u32 data = nvbios_connEe(bios, idx, ver, len);
	memset(info, 0x00, sizeof(*info));
	switch (!!data * *ver) {
	case 0x30:
	case 0x40:
		info->type     =  nv_ro08(bios, data + 0x00);
		info->location =  nv_ro08(bios, data + 0x01) & 0x0f;
		info->hpd      = (nv_ro08(bios, data + 0x01) & 0x30) >> 4;
		info->dp       = (nv_ro08(bios, data + 0x01) & 0xc0) >> 6;
		if (*len < 4)
			return data;
		info->hpd     |= (nv_ro08(bios, data + 0x02) & 0x03) << 2;
		info->dp      |=  nv_ro08(bios, data + 0x02) & 0x0c;
		info->di       = (nv_ro08(bios, data + 0x02) & 0xf0) >> 4;
		info->hpd     |= (nv_ro08(bios, data + 0x03) & 0x07) << 4;
		info->sr       = (nv_ro08(bios, data + 0x03) & 0x08) >> 3;
		info->lcdid    = (nv_ro08(bios, data + 0x03) & 0x70) >> 4;
		return data;
	default:
		break;
	}
	return 0x00000000;
}
