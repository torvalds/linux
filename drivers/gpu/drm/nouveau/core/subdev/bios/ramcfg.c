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
#include <subdev/bios/bit.h>
#include <subdev/bios/ramcfg.h>

static u8
nvbios_ramcfg_strap(struct nouveau_bios *bios)
{
	return (nv_rd32(bios, 0x101000) & 0x0000003c) >> 2;
}

u8
nvbios_ramcfg_count(struct nouveau_bios *bios)
{
	struct bit_entry bit_M;

	if (!bit_entry(bios, 'M', &bit_M)) {
		if (bit_M.version == 1 && bit_M.length >= 5)
			return nv_ro08(bios, bit_M.offset + 2);
		if (bit_M.version == 2 && bit_M.length >= 3)
			return nv_ro08(bios, bit_M.offset + 0);
	}

	return 0x00;
}

u8
nvbios_ramcfg_index(struct nouveau_bios *bios)
{
	u8 strap = nvbios_ramcfg_strap(bios);
	u32 xlat = 0x00000000;
	struct bit_entry bit_M;

	if (!bit_entry(bios, 'M', &bit_M)) {
		if (bit_M.version == 1 && bit_M.length >= 5)
			xlat = nv_ro16(bios, bit_M.offset + 3);
		if (bit_M.version == 2 && bit_M.length >= 3)
			xlat = nv_ro16(bios, bit_M.offset + 1);
	}

	if (xlat)
		strap = nv_ro08(bios, xlat + strap);
	return strap;
}
