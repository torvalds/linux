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
	int CWL, CL, WR, DLL = 0, ODT = 0;

	switch (ram->next->bios.timing_ver) {
	case 0x10:
		if (ram->next->bios.timing_hdr < 0x17) {
			/* XXX: NV50: Get CWL from the timing register */
			return -ENOSYS;
		}
		CWL = ram->next->bios.timing_10_CWL;
		CL  = ram->next->bios.timing_10_CL;
		WR  = ram->next->bios.timing_10_WR;
		DLL = !ram->next->bios.ramcfg_10_02_40;
		ODT = ram->next->bios.timing_10_ODT;
		break;
	case 0x20:
		CWL = (ram->next->bios.timing[1] & 0x00000f80) >> 7;
		CL  = (ram->next->bios.timing[1] & 0x0000001f) >> 0;
		WR  = (ram->next->bios.timing[2] & 0x007f0000) >> 16;
		/* XXX: Get these values from the VBIOS instead */
		DLL = !(ram->mr[1] & 0x1);
		ODT =   (ram->mr[1] & 0x004) >> 2 |
			(ram->mr[1] & 0x040) >> 5 |
		        (ram->mr[1] & 0x200) >> 7;
		break;
	default:
		return -ENOSYS;
	}

	CWL = ramxlat(ramddr3_cwl, CWL);
	CL  = ramxlat(ramddr3_cl, CL);
	WR  = ramxlat(ramddr3_wr, WR);
	if (CL < 0 || CWL < 0 || WR < 0)
		return -EINVAL;

	ram->mr[0] &= ~0xf74;
	ram->mr[0] |= (WR & 0x07) << 9;
	ram->mr[0] |= (CL & 0x0e) << 3;
	ram->mr[0] |= (CL & 0x01) << 2;

	ram->mr[1] &= ~0x245;
	ram->mr[1] |= (ODT & 0x1) << 2;
	ram->mr[1] |= (ODT & 0x2) << 5;
	ram->mr[1] |= (ODT & 0x4) << 7;
	ram->mr[1] |= !DLL;

	ram->mr[2] &= ~0x038;
	ram->mr[2] |= (CWL & 0x07) << 3;
	return 0;
}
