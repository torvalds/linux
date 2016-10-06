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
#include <subdev/bios/dcb.h>
#include <subdev/bios/extdev.h>

static u16
extdev_table(struct nvkm_bios *bios, u8 *ver, u8 *hdr, u8 *len, u8 *cnt)
{
	u8  dcb_ver, dcb_hdr, dcb_cnt, dcb_len;
	u16 dcb, extdev = 0;

	dcb = dcb_table(bios, &dcb_ver, &dcb_hdr, &dcb_cnt, &dcb_len);
	if (!dcb || (dcb_ver != 0x30 && dcb_ver != 0x40 && dcb_ver != 0x41))
		return 0x0000;

	extdev = nvbios_rd16(bios, dcb + 18);
	if (!extdev)
		return 0x0000;

	*ver = nvbios_rd08(bios, extdev + 0);
	*hdr = nvbios_rd08(bios, extdev + 1);
	*cnt = nvbios_rd08(bios, extdev + 2);
	*len = nvbios_rd08(bios, extdev + 3);
	return extdev + *hdr;
}

static u16
nvbios_extdev_entry(struct nvkm_bios *bios, int idx, u8 *ver, u8 *len)
{
	u8 hdr, cnt;
	u16 extdev = extdev_table(bios, ver, &hdr, len, &cnt);
	if (extdev && idx < cnt)
		return extdev + idx * *len;
	return 0x0000;
}

static void
extdev_parse_entry(struct nvkm_bios *bios, u16 offset,
		   struct nvbios_extdev_func *entry)
{
	entry->type = nvbios_rd08(bios, offset + 0);
	entry->addr = nvbios_rd08(bios, offset + 1);
	entry->bus = (nvbios_rd08(bios, offset + 2) >> 4) & 1;
}

int
nvbios_extdev_parse(struct nvkm_bios *bios, int idx,
		    struct nvbios_extdev_func *func)
{
	u8 ver, len;
	u16 entry;

	if (!(entry = nvbios_extdev_entry(bios, idx, &ver, &len)))
		return -EINVAL;

	extdev_parse_entry(bios, entry, func);
	return 0;
}

int
nvbios_extdev_find(struct nvkm_bios *bios, enum nvbios_extdev_type type,
		   struct nvbios_extdev_func *func)
{
	u8 ver, len, i;
	u16 entry;

	i = 0;
	while ((entry = nvbios_extdev_entry(bios, i++, &ver, &len))) {
		extdev_parse_entry(bios, entry, func);
		if (func->type == type)
			return 0;
	}

	return -EINVAL;
}
