/*
 * Copyright 2014 Roy Spliet
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
 * Authors: Roy Spliet <rspliet@eclipso.eu>
 *          Ben Skeggs
 */
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
ramddr2_cl[] = {
	{ 2, 2 }, { 3, 3 }, { 4, 4 }, { 5, 5 }, { 6, 6 },
	/* The following are available in some, but not all DDR2 docs */
	{ 7, 7 },
	{ -1 }
};

static const struct ramxlat
ramddr2_wr[] = {
	{ 2, 1 }, { 3, 2 }, { 4, 3 }, { 5, 4 }, { 6, 5 },
	/* The following are available in some, but not all DDR2 docs */
	{ 7, 6 },
	{ -1 }
};

int
nvkm_sddr2_calc(struct nvkm_ram *ram)
{
	int CL, WR, DLL = 0, ODT = 0;

	switch (ram->next->bios.timing_ver) {
	case 0x10:
		CL  = ram->next->bios.timing_10_CL;
		WR  = ram->next->bios.timing_10_WR;
		DLL = !ram->next->bios.ramcfg_DLLoff;
		ODT = ram->next->bios.timing_10_ODT & 3;
		break;
	case 0x20:
		CL  = (ram->next->bios.timing[1] & 0x0000001f);
		WR  = (ram->next->bios.timing[2] & 0x007f0000) >> 16;
		break;
	default:
		return -ENOSYS;
	}

	CL  = ramxlat(ramddr2_cl, CL);
	WR  = ramxlat(ramddr2_wr, WR);
	if (CL < 0 || WR < 0)
		return -EINVAL;

	ram->mr[0] &= ~0xf70;
	ram->mr[0] |= (WR & 0x07) << 9;
	ram->mr[0] |= (CL & 0x07) << 4;

	ram->mr[1] &= ~0x045;
	ram->mr[1] |= (ODT & 0x1) << 2;
	ram->mr[1] |= (ODT & 0x2) << 5;
	ram->mr[1] |= !DLL;
	return 0;
}
