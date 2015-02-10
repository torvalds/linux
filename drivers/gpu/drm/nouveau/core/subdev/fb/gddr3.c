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
 * 	    Roy Spliet <rspliet@eclipso.eu>
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
ramgddr3_cl_lo[] = {
	{ 7, 7 }, { 8, 0 }, { 9, 1 }, { 10, 2 }, { 11, 3 },
	/* the below are mentioned in some, but not all, gddr3 docs */
	{ 12, 4 }, { 13, 5 }, { 14, 6 },
	/* XXX: Per Samsung docs, are these used? They overlap with Qimonda */
	/* { 4, 4 }, { 5, 5 }, { 6, 6 }, { 12, 8 }, { 13, 9 }, { 14, 10 },
	 * { 15, 11 }, */
	{ -1 }
};

static const struct ramxlat
ramgddr3_cl_hi[] = {
	{ 10, 2 }, { 11, 3 }, { 12, 4 }, { 13, 5 }, { 14, 6 }, { 15, 7 },
	{ 16, 0 }, { 17, 1 },
	{ -1 }
};

static const struct ramxlat
ramgddr3_wr_lo[] = {
	{ 5, 2 }, { 7, 4 }, { 8, 5 }, { 9, 6 }, { 10, 7 },
	{ 11, 0 },
	/* the below are mentioned in some, but not all, gddr3 docs */
	{ 4, 1 }, { 6, 3 }, { 12, 1 }, { 13 , 2 },
	{ -1 }
};

int
nouveau_gddr3_calc(struct nouveau_ram *ram)
{
	int CL, WR, CWL, DLL = 0, ODT = 0, hi;

	switch (ram->next->bios.timing_ver) {
	case 0x10:
		CWL = ram->next->bios.timing_10_CWL;
		CL  = ram->next->bios.timing_10_CL;
		WR  = ram->next->bios.timing_10_WR;
		DLL = !ram->next->bios.ramcfg_10_DLLoff;
		ODT = ram->next->bios.timing_10_ODT;
		break;
	case 0x20:
		CWL = (ram->next->bios.timing[1] & 0x00000f80) >> 7;
		CL  = (ram->next->bios.timing[1] & 0x0000001f) >> 0;
		WR  = (ram->next->bios.timing[2] & 0x007f0000) >> 16;
		/* XXX: Get these values from the VBIOS instead */
		DLL = !(ram->mr[1] & 0x1);
		ODT =  (ram->mr[1] & 0x004) >> 2 |
		       (ram->mr[1] & 0x040) >> 5 |
		       (ram->mr[1] & 0x200) >> 7;
		break;
	default:
		return -ENOSYS;
	}

	hi = ram->mr[2] & 0x1;
	CL  = ramxlat(hi ? ramgddr3_cl_hi : ramgddr3_cl_lo, CL);
	WR  = ramxlat(ramgddr3_wr_lo, WR);
	if (CL < 0 || CWL < 1 || CWL > 7 || WR < 0)
		return -EINVAL;

	ram->mr[0] &= ~0xf74;
	ram->mr[0] |= (CWL & 0x07) << 9;
	ram->mr[0] |= (CL & 0x07) << 4;
	ram->mr[0] |= (CL & 0x08) >> 1;

	ram->mr[1] &= ~0x3fc;
	ram->mr[1] |= (ODT & 0x03) << 2;
	ram->mr[1] |= (ODT & 0x03) << 8;
	ram->mr[1] |= (WR  & 0x03) << 4;
	ram->mr[1] |= (WR  & 0x04) << 5;
	ram->mr[1] |= !DLL << 6;
	return 0;
}
