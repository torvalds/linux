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

int
nouveau_gddr5_calc(struct nouveau_ram *ram)
{
	struct nouveau_bios *bios = nouveau_bios(ram);
	int pd, lf, xd, vh, vr, vo;
	int WL, CL, WR, at, dt, ds;
	int rq = ram->freq < 1000000; /* XXX */

	switch (!!ram->ramcfg.data * ram->ramcfg.version) {
	case 0x11:
		pd =  (nv_ro08(bios, ram->ramcfg.data + 0x01) & 0x80) >> 7;
		lf =  (nv_ro08(bios, ram->ramcfg.data + 0x01) & 0x40) >> 6;
		xd = !(nv_ro08(bios, ram->ramcfg.data + 0x01) & 0x20);
		vh =  (nv_ro08(bios, ram->ramcfg.data + 0x02) & 0x10) >> 4;
		vr =  (nv_ro08(bios, ram->ramcfg.data + 0x02) & 0x04) >> 2;
		vo =   nv_ro08(bios, ram->ramcfg.data + 0x06) & 0xff;
		break;
	default:
		return -ENOSYS;
	}

	switch (!!ram->timing.data * ram->timing.version) {
	case 0x20:
		WL = (nv_ro16(bios, ram->timing.data + 0x04) & 0x0f80) >> 7;
		CL =  nv_ro08(bios, ram->timing.data + 0x04) & 0x1f;
		WR =  nv_ro08(bios, ram->timing.data + 0x0a) & 0x7f;
		at = (nv_ro08(bios, ram->timing.data + 0x2e) & 0xc0) >> 6;
		dt =  nv_ro08(bios, ram->timing.data + 0x2e) & 0x03;
		ds =  nv_ro08(bios, ram->timing.data + 0x2f) & 0x03;
		break;
	default:
		return -ENOSYS;
	}

	if (WL < 1 || WL > 7 || CL < 5 || CL > 36 || WR < 4 || WR > 35)
		return -EINVAL;
	CL -= 5;
	WR -= 4;

	ram->mr[0] &= ~0xf7f;
	ram->mr[0] |= (WR & 0x0f) << 8;
	ram->mr[0] |= (CL & 0x0f) << 3;
	ram->mr[0] |= (WL & 0x07) << 0;

	ram->mr[1] &= ~0x0bf;
	ram->mr[1] |= (xd & 0x01) << 7;
	ram->mr[1] |= (at & 0x03) << 4;
	ram->mr[1] |= (dt & 0x03) << 2;
	ram->mr[1] |= (ds & 0x03) << 0;

	ram->mr[3] &= ~0x020;
	ram->mr[3] |= (rq & 0x01) << 5;

	if (!vo)
		vo = (ram->mr[6] & 0xff0) >> 4;
	if (ram->mr[6] & 0x001)
		pd = 1; /* binary driver does this.. bug? */
	ram->mr[6] &= ~0xff1;
	ram->mr[6] |= (vo & 0xff) << 4;
	ram->mr[6] |= (pd & 0x01) << 0;

	if (!(ram->mr[7] & 0x100))
		vr = 0; /* binary driver does this.. bug? */
	ram->mr[7] &= ~0x188;
	ram->mr[7] |= (vr & 0x01) << 8;
	ram->mr[7] |= (vh & 0x01) << 7;
	ram->mr[7] |= (lf & 0x01) << 3;
	return 0;
}
