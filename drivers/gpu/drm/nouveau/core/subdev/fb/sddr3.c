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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include <subdev/bios.h>
#include "priv.h"

struct ramxlat {
	int id;
	u8 enc;
};

static inline int
ramxlat(const struct ramxlat *xlat, int id)
{
	while (xlat->id >= 0) {
		if (xlat->id == id)
			return xlat->enc;
		xlat++;
	}
	return -EINVAL;
}

static const struct ramxlat
ramddr3_cl[] = {
	{ 5, 2 }, { 6, 4 }, { 7, 6 }, { 8, 8 }, { 9, 10 }, { 10, 12 },
	{ 11, 14 },
	/* the below are mentioned in some, but not all, ddr3 docs */
	{ 12, 1 }, { 13, 3 }, { 14, 5 },
	{ -1 }
};

static const struct ramxlat
ramddr3_wr[] = {
	{ 5, 1 }, { 6, 2 }, { 7, 3 }, { 8, 4 }, { 10, 5 }, { 12, 6 },
	/* the below are mentioned in some, but not all, ddr3 docs */
	{ 14, 7 }, { 16, 0 },
	{ -1 }
};

static const struct ramxlat
ramddr3_cwl[] = {
	{ 5, 0 }, { 6, 1 }, { 7, 2 }, { 8, 3 },
	/* the below are mentioned in some, but not all, ddr3 docs */
	{ 9, 4 },
	{ -1 }
};

int
nouveau_sddr3_calc(struct nouveau_ram *ram)
{
	struct nouveau_bios *bios = nouveau_bios(ram);
	int WL, CL, WR;

	switch (!!ram->timing.data * ram->timing.version) {
	case 0x20:
		WL = (nv_ro16(bios, ram->timing.data + 0x04) & 0x0f80) >> 7;
		CL =  nv_ro08(bios, ram->timing.data + 0x04) & 0x1f;
		WR =  nv_ro08(bios, ram->timing.data + 0x0a) & 0x7f;
		break;
	default:
		return -ENOSYS;
	}

	WL = ramxlat(ramddr3_cwl, WL);
	CL = ramxlat(ramddr3_cl, CL);
	WR = ramxlat(ramddr3_wr, WR);
	if (WL < 0 || CL < 0 || WR < 0)
		return -EINVAL;

	ram->mr[0] &= ~0xe74;
	ram->mr[0] |= (WR & 0x07) << 9;
	ram->mr[0] |= (CL & 0x0e) << 3;
	ram->mr[0] |= (CL & 0x01) << 2;

	ram->mr[2] &= ~0x038;
	ram->mr[2] |= (WL & 0x07) << 3;
	return 0;
}
