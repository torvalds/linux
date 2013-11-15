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
 * Authors: Ben Skeggs
 */

#include <subdev/gpio.h>

#include <subdev/bios.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/pll.h>
#include <subdev/bios/init.h>
#include <subdev/bios/rammap.h>
#include <subdev/bios/timing.h>

#include <subdev/clock.h>
#include <subdev/clock/pll.h>

#include <subdev/timer.h>

#include <core/option.h>

#include "nvc0.h"

#include "ramfuc.h"

struct nve0_ramfuc {
	struct ramfuc base;

	struct nvbios_pll refpll;
	struct nvbios_pll mempll;

	struct ramfuc_reg r_gpioMV;
	u32 r_funcMV[2];
	struct ramfuc_reg r_gpio2E;
	u32 r_func2E[2];
	struct ramfuc_reg r_gpiotrig;

	struct ramfuc_reg r_0x132020;
	struct ramfuc_reg r_0x132028;
	struct ramfuc_reg r_0x132024;
	struct ramfuc_reg r_0x132030;
	struct ramfuc_reg r_0x132034;
	struct ramfuc_reg r_0x132000;
	struct ramfuc_reg r_0x132004;
	struct ramfuc_reg r_0x132040;

	struct ramfuc_reg r_0x10f248;
	struct ramfuc_reg r_0x10f290;
	struct ramfuc_reg r_0x10f294;
	struct ramfuc_reg r_0x10f298;
	struct ramfuc_reg r_0x10f29c;
	struct ramfuc_reg r_0x10f2a0;
	struct ramfuc_reg r_0x10f2a4;
	struct ramfuc_reg r_0x10f2a8;
	struct ramfuc_reg r_0x10f2ac;
	struct ramfuc_reg r_0x10f2cc;
	struct ramfuc_reg r_0x10f2e8;
	struct ramfuc_reg r_0x10f250;
	struct ramfuc_reg r_0x10f24c;
	struct ramfuc_reg r_0x10fec4;
	struct ramfuc_reg r_0x10fec8;
	struct ramfuc_reg r_0x10f604;
	struct ramfuc_reg r_0x10f614;
	struct ramfuc_reg r_0x10f610;
	struct ramfuc_reg r_0x100770;
	struct ramfuc_reg r_0x100778;
	struct ramfuc_reg r_0x10f224;

	struct ramfuc_reg r_0x10f870;
	struct ramfuc_reg r_0x10f698;
	struct ramfuc_reg r_0x10f694;
	struct ramfuc_reg r_0x10f6b8;
	struct ramfuc_reg r_0x10f808;
	struct ramfuc_reg r_0x10f670;
	struct ramfuc_reg r_0x10f60c;
	struct ramfuc_reg r_0x10f830;
	struct ramfuc_reg r_0x1373ec;
	struct ramfuc_reg r_0x10f800;
	struct ramfuc_reg r_0x10f82c;

	struct ramfuc_reg r_0x10f978;
	struct ramfuc_reg r_0x10f910;
	struct ramfuc_reg r_0x10f914;

	struct ramfuc_reg r_mr[16]; /* MR0 - MR8, MR15 */

	struct ramfuc_reg r_0x62c000;
	struct ramfuc_reg r_0x10f200;
	struct ramfuc_reg r_0x10f210;
	struct ramfuc_reg r_0x10f310;
	struct ramfuc_reg r_0x10f314;
	struct ramfuc_reg r_0x10f318;
	struct ramfuc_reg r_0x10f090;
	struct ramfuc_reg r_0x10f69c;
	struct ramfuc_reg r_0x10f824;
	struct ramfuc_reg r_0x1373f0;
	struct ramfuc_reg r_0x1373f4;
	struct ramfuc_reg r_0x137320;
	struct ramfuc_reg r_0x10f65c;
	struct ramfuc_reg r_0x10f6bc;
	struct ramfuc_reg r_0x100710;
	struct ramfuc_reg r_0x10f750;
};

struct nve0_ram {
	struct nouveau_ram base;
	struct nve0_ramfuc fuc;
	int from;
	int mode;
	int N1, fN1, M1, P1;
	int N2, M2, P2;
};

/*******************************************************************************
 * GDDR5
 ******************************************************************************/
static void
train(struct nve0_ramfuc *fuc, u32 magic)
{
	struct nve0_ram *ram = container_of(fuc, typeof(*ram), fuc);
	struct nouveau_fb *pfb = nouveau_fb(ram);
	const int mc = nv_rd32(pfb, 0x02243c);
	int i;

	ram_mask(fuc, 0x10f910, 0xbc0e0000, magic);
	ram_mask(fuc, 0x10f914, 0xbc0e0000, magic);
	for (i = 0; i < mc; i++) {
		const u32 addr = 0x110974 + (i * 0x1000);
		ram_wait(fuc, addr, 0x0000000f, 0x00000000, 500000);
	}
}

static void
r1373f4_init(struct nve0_ramfuc *fuc)
{
	struct nve0_ram *ram = container_of(fuc, typeof(*ram), fuc);
	const u32 mcoef = ((--ram->P2 << 28) | (ram->N2 << 8) | ram->M2);
	const u32 rcoef = ((  ram->P1 << 16) | (ram->N1 << 8) | ram->M1);
	const u32 runk0 = ram->fN1 << 16;
	const u32 runk1 = ram->fN1;

	if (ram->from == 2) {
		ram_mask(fuc, 0x1373f4, 0x00000000, 0x00001100);
		ram_mask(fuc, 0x1373f4, 0x00000000, 0x00000010);
	} else {
		ram_mask(fuc, 0x1373f4, 0x00000000, 0x00010010);
	}

	ram_mask(fuc, 0x1373f4, 0x00000003, 0x00000000);
	ram_mask(fuc, 0x1373f4, 0x00000010, 0x00000000);

	/* (re)program refpll, if required */
	if ((ram_rd32(fuc, 0x132024) & 0xffffffff) != rcoef ||
	    (ram_rd32(fuc, 0x132034) & 0x0000ffff) != runk1) {
		ram_mask(fuc, 0x132000, 0x00000001, 0x00000000);
		ram_mask(fuc, 0x132020, 0x00000001, 0x00000000);
		ram_wr32(fuc, 0x137320, 0x00000000);
		ram_mask(fuc, 0x132030, 0xffff0000, runk0);
		ram_mask(fuc, 0x132034, 0x0000ffff, runk1);
		ram_wr32(fuc, 0x132024, rcoef);
		ram_mask(fuc, 0x132028, 0x00080000, 0x00080000);
		ram_mask(fuc, 0x132020, 0x00000001, 0x00000001);
		ram_wait(fuc, 0x137390, 0x00020000, 0x00020000, 64000);
		ram_mask(fuc, 0x132028, 0x00080000, 0x00000000);
	}

	/* (re)program mempll, if required */
	if (ram->mode == 2) {
		ram_mask(fuc, 0x1373f4, 0x00010000, 0x00000000);
		ram_mask(fuc, 0x132000, 0x00000001, 0x00000000);
		ram_mask(fuc, 0x132004, 0x103fffff, mcoef);
		ram_mask(fuc, 0x132000, 0x00000001, 0x00000001);
		ram_wait(fuc, 0x137390, 0x00000002, 0x00000002, 64000);
		ram_mask(fuc, 0x1373f4, 0x00000000, 0x00001100);
	} else {
		ram_mask(fuc, 0x1373f4, 0x00000000, 0x00010100);
	}

	ram_mask(fuc, 0x1373f4, 0x00000000, 0x00000010);
}

static void
r1373f4_fini(struct nve0_ramfuc *fuc, u32 ramcfg)
{
	struct nve0_ram *ram = container_of(fuc, typeof(*ram), fuc);
	struct nouveau_bios *bios = nouveau_bios(ram);
	u8 v0 = (nv_ro08(bios, ramcfg + 0x03) & 0xc0) >> 6;
	u8 v1 = (nv_ro08(bios, ramcfg + 0x03) & 0x30) >> 4;
	u32 tmp;

	tmp = ram_rd32(fuc, 0x1373ec) & ~0x00030000;
	ram_wr32(fuc, 0x1373ec, tmp | (v1 << 16));
	ram_mask(fuc, 0x1373f0, (~ram->mode & 3), 0x00000000);
	if (ram->mode == 2) {
		ram_mask(fuc, 0x1373f4, 0x00000003, 0x000000002);
		ram_mask(fuc, 0x1373f4, 0x00001100, 0x000000000);
	} else {
		ram_mask(fuc, 0x1373f4, 0x00000003, 0x000000001);
		ram_mask(fuc, 0x1373f4, 0x00010000, 0x000000000);
	}
	ram_mask(fuc, 0x10f800, 0x00000030, (v0 ^ v1) << 4);
}

static int
nve0_ram_calc_gddr5(struct nouveau_fb *pfb, u32 freq)
{
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nve0_ram *ram = (void *)pfb->ram;
	struct nve0_ramfuc *fuc = &ram->fuc;
	const u32 rammap = ram->base.rammap.data;
	const u32 ramcfg = ram->base.ramcfg.data;
	const u32 timing = ram->base.timing.data;
	int vc = !(nv_ro08(bios, ramcfg + 0x02) & 0x08);
	int mv = 1; /*XXX*/
	u32 mask, data;

	ram_mask(fuc, 0x10f808, 0x40000000, 0x40000000);
	ram_wr32(fuc, 0x62c000, 0x0f0f0000);

	/* MR1: turn termination on early, for some reason.. */
	if ((ram->base.mr[1] & 0x03c) != 0x030)
		ram_mask(fuc, mr[1], 0x03c, ram->base.mr[1] & 0x03c);

	if (vc == 1 && ram_have(fuc, gpio2E)) {
		u32 temp  = ram_mask(fuc, gpio2E, 0x3000, fuc->r_func2E[1]);
		if (temp != ram_rd32(fuc, gpio2E)) {
			ram_wr32(fuc, gpiotrig, 1);
			ram_nsec(fuc, 20000);
		}
	}

	ram_mask(fuc, 0x10f200, 0x00000800, 0x00000000);

	ram_mask(fuc, 0x10f914, 0x01020000, 0x000c0000);
	ram_mask(fuc, 0x10f910, 0x01020000, 0x000c0000);

	ram_wr32(fuc, 0x10f210, 0x00000000); /* REFRESH_AUTO = 0 */
	ram_nsec(fuc, 1000);
	ram_wr32(fuc, 0x10f310, 0x00000001); /* REFRESH */
	ram_nsec(fuc, 1000);

	ram_mask(fuc, 0x10f200, 0x80000000, 0x80000000);
	ram_wr32(fuc, 0x10f314, 0x00000001); /* PRECHARGE */
	ram_mask(fuc, 0x10f200, 0x80000000, 0x00000000);
	ram_wr32(fuc, 0x10f090, 0x00000061);
	ram_wr32(fuc, 0x10f090, 0xc000007f);
	ram_nsec(fuc, 1000);

	ram_wr32(fuc, 0x10f698, 0x00000000);
	ram_wr32(fuc, 0x10f69c, 0x00000000);

	/*XXX: there does appear to be some kind of condition here, simply
	 *     modifying these bits in the vbios from the default pl0
	 *     entries shows no change.  however, the data does appear to
	 *     be correct and may be required for the transition back
	 */
	mask = 0x800f07e0;
	data = 0x00030000;
	if (ram_rd32(fuc, 0x10f978) & 0x00800000)
		data |= 0x00040000;

	if (1) {
		data |= 0x800807e0;
		switch (nv_ro08(bios, ramcfg + 0x03) & 0xc0) {
		case 0xc0: data &= ~0x00000040; break;
		case 0x80: data &= ~0x00000100; break;
		case 0x40: data &= ~0x80000000; break;
		case 0x00: data &= ~0x00000400; break;
		}

		switch (nv_ro08(bios, ramcfg + 0x03) & 0x30) {
		case 0x30: data &= ~0x00000020; break;
		case 0x20: data &= ~0x00000080; break;
		case 0x10: data &= ~0x00080000; break;
		case 0x00: data &= ~0x00000200; break;
		}
	}

	if (nv_ro08(bios, ramcfg + 0x02) & 0x80)
		mask |= 0x03000000;
	if (nv_ro08(bios, ramcfg + 0x02) & 0x40)
		mask |= 0x00002000;
	if (nv_ro08(bios, ramcfg + 0x07) & 0x10)
		mask |= 0x00004000;
	if (nv_ro08(bios, ramcfg + 0x07) & 0x08)
		mask |= 0x00000003;
	else {
		mask |= 0x34000000;
		if (ram_rd32(fuc, 0x10f978) & 0x00800000)
			mask |= 0x40000000;
	}
	ram_mask(fuc, 0x10f824, mask, data);

	ram_mask(fuc, 0x132040, 0x00010000, 0x00000000);

	if (ram->from == 2 && ram->mode != 2) {
		ram_mask(fuc, 0x10f808, 0x00080000, 0x00000000);
		ram_mask(fuc, 0x10f200, 0x00008000, 0x00008000);
		ram_mask(fuc, 0x10f800, 0x00000000, 0x00000004);
		ram_mask(fuc, 0x10f830, 0x00008000, 0x01040010);
		ram_mask(fuc, 0x10f830, 0x01000000, 0x00000000);
		r1373f4_init(fuc);
		ram_mask(fuc, 0x1373f0, 0x00000002, 0x00000001);
		r1373f4_fini(fuc, ramcfg);
		ram_mask(fuc, 0x10f830, 0x00c00000, 0x00240001);
	} else
	if (ram->from != 2 && ram->mode != 2) {
		r1373f4_init(fuc);
		r1373f4_fini(fuc, ramcfg);
	}

	if (ram_have(fuc, gpioMV)) {
		u32 temp  = ram_mask(fuc, gpioMV, 0x3000, fuc->r_funcMV[mv]);
		if (temp != ram_rd32(fuc, gpioMV)) {
			ram_wr32(fuc, gpiotrig, 1);
			ram_nsec(fuc, 64000);
		}
	}

	if ( (nv_ro08(bios, ramcfg + 0x02) & 0x40) ||
	     (nv_ro08(bios, ramcfg + 0x07) & 0x10)) {
		ram_mask(fuc, 0x132040, 0x00010000, 0x00010000);
		ram_nsec(fuc, 20000);
	}

	if (ram->from != 2 && ram->mode == 2) {
		ram_mask(fuc, 0x10f800, 0x00000004, 0x00000000);
		ram_mask(fuc, 0x1373f0, 0x00000000, 0x00000002);
		ram_mask(fuc, 0x10f830, 0x00800001, 0x00408010);
		r1373f4_init(fuc);
		r1373f4_fini(fuc, ramcfg);
		ram_mask(fuc, 0x10f808, 0x00000000, 0x00080000);
		ram_mask(fuc, 0x10f200, 0x00808000, 0x00800000);
	} else
	if (ram->from == 2 && ram->mode == 2) {
		ram_mask(fuc, 0x10f800, 0x00000004, 0x00000000);
		r1373f4_init(fuc);
		r1373f4_fini(fuc, ramcfg);
	}

	if (ram->mode != 2) /*XXX*/ {
		if (nv_ro08(bios, ramcfg + 0x07) & 0x40)
			ram_mask(fuc, 0x10f670, 0x80000000, 0x80000000);
	}

	data = (nv_ro08(bios, rammap + 0x11) & 0x0c) >> 2;
	ram_wr32(fuc, 0x10f65c, 0x00000011 * data);
	ram_wr32(fuc, 0x10f6b8, 0x01010101 * nv_ro08(bios, ramcfg + 0x09));
	ram_wr32(fuc, 0x10f6bc, 0x01010101 * nv_ro08(bios, ramcfg + 0x09));

	data = nv_ro08(bios, ramcfg + 0x04);
	if (!(nv_ro08(bios, ramcfg + 0x07) & 0x08)) {
		ram_wr32(fuc, 0x10f698, 0x01010101 * data);
		ram_wr32(fuc, 0x10f69c, 0x01010101 * data);
	}

	if (ram->mode != 2) {
		u32 temp = ram_rd32(fuc, 0x10f694) & ~0xff00ff00;
		ram_wr32(fuc, 0x10f694, temp | (0x01000100 * data));
	}

	if (ram->mode == 2 && (nv_ro08(bios, ramcfg + 0x08) & 0x10))
		data = 0x00000080;
	else
		data = 0x00000000;
	ram_mask(fuc, 0x10f60c, 0x00000080, data);

	mask = 0x00070000;
	data = 0x00000000;
	if (!(nv_ro08(bios, ramcfg + 0x02) & 0x80))
		data |= 0x03000000;
	if (!(nv_ro08(bios, ramcfg + 0x02) & 0x40))
		data |= 0x00002000;
	if (!(nv_ro08(bios, ramcfg + 0x07) & 0x10))
		data |= 0x00004000;
	if (!(nv_ro08(bios, ramcfg + 0x07) & 0x08))
		data |= 0x00000003;
	else
		data |= 0x74000000;
	ram_mask(fuc, 0x10f824, mask, data);

	if (nv_ro08(bios, ramcfg + 0x01) & 0x08)
		data = 0x00000000;
	else
		data = 0x00001000;
	ram_mask(fuc, 0x10f200, 0x00001000, data);

	if (ram_rd32(fuc, 0x10f670) & 0x80000000) {
		ram_nsec(fuc, 10000);
		ram_mask(fuc, 0x10f670, 0x80000000, 0x00000000);
	}

	if (nv_ro08(bios, ramcfg + 0x08) & 0x01)
		data = 0x00100000;
	else
		data = 0x00000000;
	ram_mask(fuc, 0x10f82c, 0x00100000, data);

	data = 0x00000000;
	if (nv_ro08(bios, ramcfg + 0x08) & 0x08)
		data |= 0x00002000;
	if (nv_ro08(bios, ramcfg + 0x08) & 0x04)
		data |= 0x00001000;
	if (nv_ro08(bios, ramcfg + 0x08) & 0x02)
		data |= 0x00004000;
	ram_mask(fuc, 0x10f830, 0x00007000, data);

	/* PFB timing */
	ram_mask(fuc, 0x10f248, 0xffffffff, nv_ro32(bios, timing + 0x28));
	ram_mask(fuc, 0x10f290, 0xffffffff, nv_ro32(bios, timing + 0x00));
	ram_mask(fuc, 0x10f294, 0xffffffff, nv_ro32(bios, timing + 0x04));
	ram_mask(fuc, 0x10f298, 0xffffffff, nv_ro32(bios, timing + 0x08));
	ram_mask(fuc, 0x10f29c, 0xffffffff, nv_ro32(bios, timing + 0x0c));
	ram_mask(fuc, 0x10f2a0, 0xffffffff, nv_ro32(bios, timing + 0x10));
	ram_mask(fuc, 0x10f2a4, 0xffffffff, nv_ro32(bios, timing + 0x14));
	ram_mask(fuc, 0x10f2a8, 0xffffffff, nv_ro32(bios, timing + 0x18));
	ram_mask(fuc, 0x10f2ac, 0xffffffff, nv_ro32(bios, timing + 0x1c));
	ram_mask(fuc, 0x10f2cc, 0xffffffff, nv_ro32(bios, timing + 0x20));
	ram_mask(fuc, 0x10f2e8, 0xffffffff, nv_ro32(bios, timing + 0x24));

	data = (nv_ro08(bios, ramcfg + 0x02) & 0x03) << 8;
	if (nv_ro08(bios, ramcfg + 0x01) & 0x10)
		data |= 0x70000000;
	ram_mask(fuc, 0x10f604, 0x70000300, data);

	data = (nv_ro08(bios, timing + 0x30) & 0x07) << 28;
	if (nv_ro08(bios, ramcfg + 0x01) & 0x01)
		data |= 0x00000100;
	ram_mask(fuc, 0x10f614, 0x70000000, data);

	data = (nv_ro08(bios, timing + 0x30) & 0x07) << 28;
	if (nv_ro08(bios, ramcfg + 0x01) & 0x02)
		data |= 0x00000100;
	ram_mask(fuc, 0x10f610, 0x70000000, data);

	mask = 0x33f00000;
	data = 0x00000000;
	if (!(nv_ro08(bios, ramcfg + 0x01) & 0x04))
		data |= 0x20200000;
	if (!(nv_ro08(bios, ramcfg + 0x07) & 0x80))
		data |= 0x12800000;
	/*XXX: see note above about there probably being some condition
	 *     for the 10f824 stuff that uses ramcfg 3...
	 */
	if ( (nv_ro08(bios, ramcfg + 0x03) & 0xf0)) {
		if (nv_ro08(bios, rammap + 0x08) & 0x0c) {
			if (!(nv_ro08(bios, ramcfg + 0x07) & 0x80))
				mask |= 0x00000020;
			else
				data |= 0x00000020;
			mask |= 0x00000004;
		}
	} else {
		mask |= 0x40000020;
		data |= 0x00000004;
	}

	ram_mask(fuc, 0x10f808, mask, data);

	data = nv_ro08(bios, ramcfg + 0x03) & 0x0f;
	ram_wr32(fuc, 0x10f870, 0x11111111 * data);

	data = nv_ro08(bios, ramcfg + 0x02) & 0x03;
	if (nv_ro08(bios, ramcfg + 0x01) & 0x10)
		data |= 0x00000004;
	if ((nv_rd32(bios, 0x100770) & 0x00000004) != (data & 0x00000004)) {
		ram_wr32(fuc, 0x10f750, 0x04000009);
		ram_wr32(fuc, 0x100710, 0x00000000);
		ram_wait(fuc, 0x100710, 0x80000000, 0x80000000, 200000);
	}
	ram_mask(fuc, 0x100770, 0x00000007, data);

	data = (nv_ro08(bios, timing + 0x30) & 0x07) << 8;
	if (nv_ro08(bios, ramcfg + 0x01) & 0x01)
		data |= 0x80000000;
	ram_mask(fuc, 0x100778, 0x00000700, data);

	data = nv_ro16(bios, timing + 0x2c);
	ram_mask(fuc, 0x10f250, 0x000003f0, (data & 0x003f) <<  4);
	ram_mask(fuc, 0x10f24c, 0x7f000000, (data & 0x1fc0) << 18);

	data = nv_ro08(bios, timing + 0x30);
	ram_mask(fuc, 0x10f224, 0x001f0000, (data & 0xf8) << 13);

	data = nv_ro16(bios, timing + 0x31);
	ram_mask(fuc, 0x10fec4, 0x041e0f07, (data & 0x0800) << 15 |
					    (data & 0x0780) << 10 |
					    (data & 0x0078) <<  5 |
					    (data & 0x0007));
	ram_mask(fuc, 0x10fec8, 0x00000027, (data & 0x8000) >> 10 |
					    (data & 0x7000) >> 12);

	ram_wr32(fuc, 0x10f090, 0x4000007e);
	ram_nsec(fuc, 1000);
	ram_wr32(fuc, 0x10f314, 0x00000001); /* PRECHARGE */
	ram_wr32(fuc, 0x10f310, 0x00000001); /* REFRESH */
	ram_nsec(fuc, 2000);
	ram_wr32(fuc, 0x10f210, 0x80000000); /* REFRESH_AUTO = 1 */

	if ((nv_ro08(bios, ramcfg + 0x08) & 0x10) && (ram->mode == 2) /*XXX*/) {
		u32 temp = ram_mask(fuc, 0x10f294, 0xff000000, 0x24000000);
		train(fuc, 0xa4010000); /*XXX*/
		ram_nsec(fuc, 1000);
		ram_wr32(fuc, 0x10f294, temp);
	}

	ram_mask(fuc, mr[3], 0xfff, ram->base.mr[3]);
	ram_wr32(fuc, mr[0], ram->base.mr[0]);
	ram_mask(fuc, mr[8], 0xfff, ram->base.mr[8]);
	ram_nsec(fuc, 1000);
	ram_mask(fuc, mr[1], 0xfff, ram->base.mr[1]);
	ram_mask(fuc, mr[5], 0xfff, ram->base.mr[5]);
	ram_mask(fuc, mr[6], 0xfff, ram->base.mr[6]);
	ram_mask(fuc, mr[7], 0xfff, ram->base.mr[7]);

	if (vc == 0 && ram_have(fuc, gpio2E)) {
		u32 temp  = ram_mask(fuc, gpio2E, 0x3000, fuc->r_func2E[0]);
		if (temp != ram_rd32(fuc, gpio2E)) {
			ram_wr32(fuc, gpiotrig, 1);
			ram_nsec(fuc, 20000);
		}
	}

	ram_mask(fuc, 0x10f200, 0x80000000, 0x80000000);
	ram_wr32(fuc, 0x10f318, 0x00000001); /* NOP? */
	ram_mask(fuc, 0x10f200, 0x80000000, 0x00000000);
	ram_nsec(fuc, 1000);

	data  = ram_rd32(fuc, 0x10f978);
	data &= ~0x00046144;
	data |=  0x0000000b;
	if (!(nv_ro08(bios, ramcfg + 0x07) & 0x08)) {
		if (!(nv_ro08(bios, ramcfg + 0x07) & 0x04))
			data |= 0x0000200c;
		else
			data |= 0x00000000;
	} else {
		data |= 0x00040044;
	}
	ram_wr32(fuc, 0x10f978, data);

	if (ram->mode == 1) {
		data = ram_rd32(fuc, 0x10f830) | 0x00000001;
		ram_wr32(fuc, 0x10f830, data);
	}

	if (!(nv_ro08(bios, ramcfg + 0x07) & 0x08)) {
		data = 0x88020000;
		if ( (nv_ro08(bios, ramcfg + 0x07) & 0x04))
			data |= 0x10000000;
		if (!(nv_ro08(bios, rammap + 0x08) & 0x10))
			data |= 0x00080000;
	} else {
		data = 0xa40e0000;
	}
	train(fuc, data);
	ram_nsec(fuc, 1000);

	if (ram->mode == 2) { /*XXX*/
		ram_mask(fuc, 0x10f800, 0x00000004, 0x00000004);
	}

	/* MR5: (re)enable LP3 if necessary
	 * XXX: need to find the switch, keeping off for now
	 */
	ram_mask(fuc, mr[5], 0x00000004, 0x00000000);

	if (ram->mode != 2) {
		ram_mask(fuc, 0x10f830, 0x01000000, 0x01000000);
		ram_mask(fuc, 0x10f830, 0x01000000, 0x00000000);
	}

	if (nv_ro08(bios, ramcfg + 0x07) & 0x02) {
		ram_mask(fuc, 0x10f910, 0x80020000, 0x01000000);
		ram_mask(fuc, 0x10f914, 0x80020000, 0x01000000);
	}

	ram_wr32(fuc, 0x62c000, 0x0f0f0f00);

	if (nv_ro08(bios, rammap + 0x08) & 0x01)
		data = 0x00000800;
	else
		data = 0x00000000;
	ram_mask(fuc, 0x10f200, 0x00000800, data);
	return 0;
}

/*******************************************************************************
 * DDR3
 ******************************************************************************/

static int
nve0_ram_calc_sddr3(struct nouveau_fb *pfb, u32 freq)
{
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nve0_ram *ram = (void *)pfb->ram;
	struct nve0_ramfuc *fuc = &ram->fuc;
	const u32 rcoef = ((  ram->P1 << 16) | (ram->N1 << 8) | ram->M1);
	const u32 runk0 = ram->fN1 << 16;
	const u32 runk1 = ram->fN1;
	const u32 rammap = ram->base.rammap.data;
	const u32 ramcfg = ram->base.ramcfg.data;
	const u32 timing = ram->base.timing.data;
	int vc = !(nv_ro08(bios, ramcfg + 0x02) & 0x08);
	int mv = 1; /*XXX*/
	u32 mask, data;

	ram_mask(fuc, 0x10f808, 0x40000000, 0x40000000);
	ram_wr32(fuc, 0x62c000, 0x0f0f0000);

	if (vc == 1 && ram_have(fuc, gpio2E)) {
		u32 temp  = ram_mask(fuc, gpio2E, 0x3000, fuc->r_func2E[1]);
		if (temp != ram_rd32(fuc, gpio2E)) {
			ram_wr32(fuc, gpiotrig, 1);
			ram_nsec(fuc, 20000);
		}
	}

	ram_mask(fuc, 0x10f200, 0x00000800, 0x00000000);
	if ((nv_ro08(bios, ramcfg + 0x03) & 0xf0))
		ram_mask(fuc, 0x10f808, 0x04000000, 0x04000000);

	ram_wr32(fuc, 0x10f314, 0x00000001); /* PRECHARGE */
	ram_wr32(fuc, 0x10f210, 0x00000000); /* REFRESH_AUTO = 0 */
	ram_wr32(fuc, 0x10f310, 0x00000001); /* REFRESH */
	ram_mask(fuc, 0x10f200, 0x80000000, 0x80000000);
	ram_wr32(fuc, 0x10f310, 0x00000001); /* REFRESH */
	ram_mask(fuc, 0x10f200, 0x80000000, 0x00000000);
	ram_nsec(fuc, 1000);

	ram_wr32(fuc, 0x10f090, 0x00000060);
	ram_wr32(fuc, 0x10f090, 0xc000007e);

	/*XXX: there does appear to be some kind of condition here, simply
	 *     modifying these bits in the vbios from the default pl0
	 *     entries shows no change.  however, the data does appear to
	 *     be correct and may be required for the transition back
	 */
	mask = 0x00010000;
	data = 0x00010000;

	if (1) {
		mask |= 0x800807e0;
		data |= 0x800807e0;
		switch (nv_ro08(bios, ramcfg + 0x03) & 0xc0) {
		case 0xc0: data &= ~0x00000040; break;
		case 0x80: data &= ~0x00000100; break;
		case 0x40: data &= ~0x80000000; break;
		case 0x00: data &= ~0x00000400; break;
		}

		switch (nv_ro08(bios, ramcfg + 0x03) & 0x30) {
		case 0x30: data &= ~0x00000020; break;
		case 0x20: data &= ~0x00000080; break;
		case 0x10: data &= ~0x00080000; break;
		case 0x00: data &= ~0x00000200; break;
		}
	}

	if (nv_ro08(bios, ramcfg + 0x02) & 0x80)
		mask |= 0x03000000;
	if (nv_ro08(bios, ramcfg + 0x02) & 0x40)
		mask |= 0x00002000;
	if (nv_ro08(bios, ramcfg + 0x07) & 0x10)
		mask |= 0x00004000;
	if (nv_ro08(bios, ramcfg + 0x07) & 0x08)
		mask |= 0x00000003;
	else
		mask |= 0x14000000;
	ram_mask(fuc, 0x10f824, mask, data);

	ram_mask(fuc, 0x132040, 0x00010000, 0x00000000);

	ram_mask(fuc, 0x1373f4, 0x00000000, 0x00010010);
	data  = ram_rd32(fuc, 0x1373ec) & ~0x00030000;
	data |= (nv_ro08(bios, ramcfg + 0x03) & 0x30) << 12;
	ram_wr32(fuc, 0x1373ec, data);
	ram_mask(fuc, 0x1373f4, 0x00000003, 0x00000000);
	ram_mask(fuc, 0x1373f4, 0x00000010, 0x00000000);

	/* (re)program refpll, if required */
	if ((ram_rd32(fuc, 0x132024) & 0xffffffff) != rcoef ||
	    (ram_rd32(fuc, 0x132034) & 0x0000ffff) != runk1) {
		ram_mask(fuc, 0x132000, 0x00000001, 0x00000000);
		ram_mask(fuc, 0x132020, 0x00000001, 0x00000000);
		ram_wr32(fuc, 0x137320, 0x00000000);
		ram_mask(fuc, 0x132030, 0xffff0000, runk0);
		ram_mask(fuc, 0x132034, 0x0000ffff, runk1);
		ram_wr32(fuc, 0x132024, rcoef);
		ram_mask(fuc, 0x132028, 0x00080000, 0x00080000);
		ram_mask(fuc, 0x132020, 0x00000001, 0x00000001);
		ram_wait(fuc, 0x137390, 0x00020000, 0x00020000, 64000);
		ram_mask(fuc, 0x132028, 0x00080000, 0x00000000);
	}

	ram_mask(fuc, 0x1373f4, 0x00000010, 0x00000010);
	ram_mask(fuc, 0x1373f4, 0x00000003, 0x00000001);
	ram_mask(fuc, 0x1373f4, 0x00010000, 0x00000000);

	if (ram_have(fuc, gpioMV)) {
		u32 temp  = ram_mask(fuc, gpioMV, 0x3000, fuc->r_funcMV[mv]);
		if (temp != ram_rd32(fuc, gpioMV)) {
			ram_wr32(fuc, gpiotrig, 1);
			ram_nsec(fuc, 64000);
		}
	}

	if ( (nv_ro08(bios, ramcfg + 0x02) & 0x40) ||
	     (nv_ro08(bios, ramcfg + 0x07) & 0x10)) {
		ram_mask(fuc, 0x132040, 0x00010000, 0x00010000);
		ram_nsec(fuc, 20000);
	}

	if (ram->mode != 2) /*XXX*/ {
		if (nv_ro08(bios, ramcfg + 0x07) & 0x40)
			ram_mask(fuc, 0x10f670, 0x80000000, 0x80000000);
	}

	data = (nv_ro08(bios, rammap + 0x11) & 0x0c) >> 2;
	ram_wr32(fuc, 0x10f65c, 0x00000011 * data);
	ram_wr32(fuc, 0x10f6b8, 0x01010101 * nv_ro08(bios, ramcfg + 0x09));
	ram_wr32(fuc, 0x10f6bc, 0x01010101 * nv_ro08(bios, ramcfg + 0x09));

	mask = 0x00010000;
	data = 0x00000000;
	if (!(nv_ro08(bios, ramcfg + 0x02) & 0x80))
		data |= 0x03000000;
	if (!(nv_ro08(bios, ramcfg + 0x02) & 0x40))
		data |= 0x00002000;
	if (!(nv_ro08(bios, ramcfg + 0x07) & 0x10))
		data |= 0x00004000;
	if (!(nv_ro08(bios, ramcfg + 0x07) & 0x08))
		data |= 0x00000003;
	else
		data |= 0x14000000;
	ram_mask(fuc, 0x10f824, mask, data);
	ram_nsec(fuc, 1000);

	if (nv_ro08(bios, ramcfg + 0x08) & 0x01)
		data = 0x00100000;
	else
		data = 0x00000000;
	ram_mask(fuc, 0x10f82c, 0x00100000, data);

	/* PFB timing */
	ram_mask(fuc, 0x10f248, 0xffffffff, nv_ro32(bios, timing + 0x28));
	ram_mask(fuc, 0x10f290, 0xffffffff, nv_ro32(bios, timing + 0x00));
	ram_mask(fuc, 0x10f294, 0xffffffff, nv_ro32(bios, timing + 0x04));
	ram_mask(fuc, 0x10f298, 0xffffffff, nv_ro32(bios, timing + 0x08));
	ram_mask(fuc, 0x10f29c, 0xffffffff, nv_ro32(bios, timing + 0x0c));
	ram_mask(fuc, 0x10f2a0, 0xffffffff, nv_ro32(bios, timing + 0x10));
	ram_mask(fuc, 0x10f2a4, 0xffffffff, nv_ro32(bios, timing + 0x14));
	ram_mask(fuc, 0x10f2a8, 0xffffffff, nv_ro32(bios, timing + 0x18));
	ram_mask(fuc, 0x10f2ac, 0xffffffff, nv_ro32(bios, timing + 0x1c));
	ram_mask(fuc, 0x10f2cc, 0xffffffff, nv_ro32(bios, timing + 0x20));
	ram_mask(fuc, 0x10f2e8, 0xffffffff, nv_ro32(bios, timing + 0x24));

	mask = 0x33f00000;
	data = 0x00000000;
	if (!(nv_ro08(bios, ramcfg + 0x01) & 0x04))
		data |= 0x20200000;
	if (!(nv_ro08(bios, ramcfg + 0x07) & 0x80))
		data |= 0x12800000;
	/*XXX: see note above about there probably being some condition
	 *     for the 10f824 stuff that uses ramcfg 3...
	 */
	if ( (nv_ro08(bios, ramcfg + 0x03) & 0xf0)) {
		if (nv_ro08(bios, rammap + 0x08) & 0x0c) {
			if (!(nv_ro08(bios, ramcfg + 0x07) & 0x80))
				mask |= 0x00000020;
			else
				data |= 0x00000020;
			mask |= 0x08000004;
		}
		data |= 0x04000000;
	} else {
		mask |= 0x44000020;
		data |= 0x08000004;
	}

	ram_mask(fuc, 0x10f808, mask, data);

	data = nv_ro08(bios, ramcfg + 0x03) & 0x0f;
	ram_wr32(fuc, 0x10f870, 0x11111111 * data);

	data = nv_ro16(bios, timing + 0x2c);
	ram_mask(fuc, 0x10f250, 0x000003f0, (data & 0x003f) <<  4);

	if (((nv_ro32(bios, timing + 0x2c) & 0x00001fc0) >>  6) >
	    ((nv_ro32(bios, timing + 0x28) & 0x7f000000) >> 24))
		data = (nv_ro32(bios, timing + 0x2c) & 0x00001fc0) >>  6;
	else
		data = (nv_ro32(bios, timing + 0x28) & 0x1f000000) >> 24;
	ram_mask(fuc, 0x10f24c, 0x7f000000, data << 24);

	data = nv_ro08(bios, timing + 0x30);
	ram_mask(fuc, 0x10f224, 0x001f0000, (data & 0xf8) << 13);

	ram_wr32(fuc, 0x10f090, 0x4000007f);
	ram_nsec(fuc, 1000);

	ram_wr32(fuc, 0x10f314, 0x00000001); /* PRECHARGE */
	ram_wr32(fuc, 0x10f310, 0x00000001); /* REFRESH */
	ram_wr32(fuc, 0x10f210, 0x80000000); /* REFRESH_AUTO = 1 */
	ram_nsec(fuc, 1000);

	ram_nuke(fuc, mr[0]);
	ram_mask(fuc, mr[0], 0x100, 0x100);
	ram_mask(fuc, mr[0], 0x100, 0x000);

	ram_mask(fuc, mr[2], 0xfff, ram->base.mr[2]);
	ram_wr32(fuc, mr[0], ram->base.mr[0]);
	ram_nsec(fuc, 1000);

	ram_nuke(fuc, mr[0]);
	ram_mask(fuc, mr[0], 0x100, 0x100);
	ram_mask(fuc, mr[0], 0x100, 0x000);

	if (vc == 0 && ram_have(fuc, gpio2E)) {
		u32 temp  = ram_mask(fuc, gpio2E, 0x3000, fuc->r_func2E[0]);
		if (temp != ram_rd32(fuc, gpio2E)) {
			ram_wr32(fuc, gpiotrig, 1);
			ram_nsec(fuc, 20000);
		}
	}

	if (ram->mode != 2) {
		ram_mask(fuc, 0x10f830, 0x01000000, 0x01000000);
		ram_mask(fuc, 0x10f830, 0x01000000, 0x00000000);
	}

	ram_mask(fuc, 0x10f200, 0x80000000, 0x80000000);
	ram_wr32(fuc, 0x10f318, 0x00000001); /* NOP? */
	ram_mask(fuc, 0x10f200, 0x80000000, 0x00000000);
	ram_nsec(fuc, 1000);

	ram_wr32(fuc, 0x62c000, 0x0f0f0f00);

	if (nv_ro08(bios, rammap + 0x08) & 0x01)
		data = 0x00000800;
	else
		data = 0x00000000;
	ram_mask(fuc, 0x10f200, 0x00000800, data);
	return 0;
}

/*******************************************************************************
 * main hooks
 ******************************************************************************/

static int
nve0_ram_calc(struct nouveau_fb *pfb, u32 freq)
{
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nve0_ram *ram = (void *)pfb->ram;
	struct nve0_ramfuc *fuc = &ram->fuc;
	struct bit_entry M;
	int ret, refclk, strap, i;
	u32 data;
	u8  cnt;

	/* lookup memory config data relevant to the target frequency */
	ram->base.rammap.data = nvbios_rammap_match(bios, freq / 1000,
						   &ram->base.rammap.version,
						   &ram->base.rammap.size, &cnt,
						   &ram->base.ramcfg.size);
	if (!ram->base.rammap.data || ram->base.rammap.version != 0x11 ||
	     ram->base.rammap.size < 0x09) {
		nv_error(pfb, "invalid/missing rammap entry\n");
		return -EINVAL;
	}

	/* locate specific data set for the attached memory */
	if (bit_entry(bios, 'M', &M) || M.version != 2 || M.length < 3) {
		nv_error(pfb, "invalid/missing memory table\n");
		return -EINVAL;
	}

	strap = (nv_rd32(pfb, 0x101000) & 0x0000003c) >> 2;
	data = nv_ro16(bios, M.offset + 1);
	if (data)
		strap = nv_ro08(bios, data + strap);

	if (strap >= cnt) {
		nv_error(pfb, "invalid ramcfg strap\n");
		return -EINVAL;
	}

	ram->base.ramcfg.version = ram->base.rammap.version;
	ram->base.ramcfg.data = ram->base.rammap.data + ram->base.rammap.size +
			       (ram->base.ramcfg.size * strap);
	if (!ram->base.ramcfg.data || ram->base.ramcfg.version != 0x11 ||
	     ram->base.ramcfg.size < 0x08) {
		nv_error(pfb, "invalid/missing ramcfg entry\n");
		return -EINVAL;
	}

	/* lookup memory timings, if bios says they're present */
	strap = nv_ro08(bios, ram->base.ramcfg.data + 0x00);
	if (strap != 0xff) {
		ram->base.timing.data =
			nvbios_timing_entry(bios, strap,
					   &ram->base.timing.version,
					   &ram->base.timing.size);
		if (!ram->base.timing.data ||
		     ram->base.timing.version != 0x20 ||
		     ram->base.timing.size < 0x33) {
			nv_error(pfb, "invalid/missing timing entry\n");
			return -EINVAL;
		}
	} else {
		ram->base.timing.data = 0;
	}

	ret = ram_init(fuc, pfb);
	if (ret)
		return ret;

	ram->mode = (freq > fuc->refpll.vco1.max_freq) ? 2 : 1;
	ram->from = ram_rd32(fuc, 0x1373f4) & 0x0000000f;

	/* XXX: this is *not* what nvidia do.  on fermi nvidia generally
	 * select, based on some unknown condition, one of the two possible
	 * reference frequencies listed in the vbios table for mempll and
	 * program refpll to that frequency.
	 *
	 * so far, i've seen very weird values being chosen by nvidia on
	 * kepler boards, no idea how/why they're chosen.
	 */
	refclk = freq;
	if (ram->mode == 2)
		refclk = fuc->mempll.refclk;

	/* calculate refpll coefficients */
	ret = nva3_pll_calc(nv_subdev(pfb), &fuc->refpll, refclk, &ram->N1,
			   &ram->fN1, &ram->M1, &ram->P1);
	fuc->mempll.refclk = ret;
	if (ret <= 0) {
		nv_error(pfb, "unable to calc refpll\n");
		return -EINVAL;
	}

	/* calculate mempll coefficients, if we're using it */
	if (ram->mode == 2) {
		/* post-divider doesn't work... the reg takes the values but
		 * appears to completely ignore it.  there *is* a bit at
		 * bit 28 that appears to divide the clock by 2 if set.
		 */
		fuc->mempll.min_p = 1;
		fuc->mempll.max_p = 2;

		ret = nva3_pll_calc(nv_subdev(pfb), &fuc->mempll, freq,
				   &ram->N2, NULL, &ram->M2, &ram->P2);
		if (ret <= 0) {
			nv_error(pfb, "unable to calc mempll\n");
			return -EINVAL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(fuc->r_mr); i++) {
		if (ram_have(fuc, mr[i]))
			ram->base.mr[i] = ram_rd32(fuc, mr[i]);
	}

	switch (ram->base.type) {
	case NV_MEM_TYPE_DDR3:
		ret = nouveau_sddr3_calc(&ram->base);
		if (ret == 0)
			ret = nve0_ram_calc_sddr3(pfb, freq);
		break;
	case NV_MEM_TYPE_GDDR5:
		ret = nouveau_gddr5_calc(&ram->base);
		if (ret == 0)
			ret = nve0_ram_calc_gddr5(pfb, freq);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	return ret;
}

static int
nve0_ram_prog(struct nouveau_fb *pfb)
{
	struct nouveau_device *device = nv_device(pfb);
	struct nve0_ram *ram = (void *)pfb->ram;
	struct nve0_ramfuc *fuc = &ram->fuc;
	ram_exec(fuc, nouveau_boolopt(device->cfgopt, "NvMemExec", false));
	return 0;
}

static void
nve0_ram_tidy(struct nouveau_fb *pfb)
{
	struct nve0_ram *ram = (void *)pfb->ram;
	struct nve0_ramfuc *fuc = &ram->fuc;
	ram_exec(fuc, false);
}

static int
nve0_ram_init(struct nouveau_object *object)
{
	struct nouveau_fb *pfb = (void *)object->parent;
	struct nve0_ram *ram   = (void *)object;
	struct nouveau_bios *bios = nouveau_bios(pfb);
	static const u8  train0[] = {
		0x00, 0xff, 0xff, 0x00, 0xff, 0x00,
		0x00, 0xff, 0xff, 0x00, 0xff, 0x00,
	};
	static const u32 train1[] = {
		0x00000000, 0xffffffff,
		0x55555555, 0xaaaaaaaa,
		0x33333333, 0xcccccccc,
		0xf0f0f0f0, 0x0f0f0f0f,
		0x00ff00ff, 0xff00ff00,
		0x0000ffff, 0xffff0000,
	};
	u8  ver, hdr, cnt, len, snr, ssz;
	u32 data, save;
	int ret, i;

	ret = nouveau_ram_init(&ram->base);
	if (ret)
		return ret;

	/* run a bunch of tables from rammap table.  there's actually
	 * individual pointers for each rammap entry too, but, nvidia
	 * seem to just run the last two entries' scripts early on in
	 * their init, and never again.. we'll just run 'em all once
	 * for now.
	 *
	 * i strongly suspect that each script is for a separate mode
	 * (likely selected by 0x10f65c's lower bits?), and the
	 * binary driver skips the one that's already been setup by
	 * the init tables.
	 */
	data = nvbios_rammap_table(bios, &ver, &hdr, &cnt, &len, &snr, &ssz);
	if (!data || hdr < 0x15)
		return -EINVAL;

	cnt  = nv_ro08(bios, data + 0x14); /* guess at count */
	data = nv_ro32(bios, data + 0x10); /* guess u32... */
	save = nv_rd32(pfb, 0x10f65c);
	for (i = 0; i < cnt; i++) {
		nv_mask(pfb, 0x10f65c, 0x000000f0, i << 4);
		nvbios_exec(&(struct nvbios_init) {
				.subdev = nv_subdev(pfb),
				.bios = bios,
				.offset = nv_ro32(bios, data), /* guess u32 */
				.execute = 1,
			    });
		data += 4;
	}
	nv_wr32(pfb, 0x10f65c, save);

	switch (ram->base.type) {
	case NV_MEM_TYPE_GDDR5:
		for (i = 0; i < 0x30; i++) {
			nv_wr32(pfb, 0x10f968, 0x00000000 | (i << 8));
			nv_wr32(pfb, 0x10f920, 0x00000000 | train0[i % 12]);
			nv_wr32(pfb, 0x10f918,              train1[i % 12]);
			nv_wr32(pfb, 0x10f920, 0x00000100 | train0[i % 12]);
			nv_wr32(pfb, 0x10f918,              train1[i % 12]);

			nv_wr32(pfb, 0x10f96c, 0x00000000 | (i << 8));
			nv_wr32(pfb, 0x10f924, 0x00000000 | train0[i % 12]);
			nv_wr32(pfb, 0x10f91c,              train1[i % 12]);
			nv_wr32(pfb, 0x10f924, 0x00000100 | train0[i % 12]);
			nv_wr32(pfb, 0x10f91c,              train1[i % 12]);
		}

		for (i = 0; i < 0x100; i++) {
			nv_wr32(pfb, 0x10f968, i);
			nv_wr32(pfb, 0x10f900, train1[2 + (i & 1)]);
		}

		for (i = 0; i < 0x100; i++) {
			nv_wr32(pfb, 0x10f96c, i);
			nv_wr32(pfb, 0x10f900, train1[2 + (i & 1)]);
		}
		break;
	default:
		break;
	}

	return 0;
}

static int
nve0_ram_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 size,
	      struct nouveau_object **pobject)
{
	struct nouveau_fb *pfb = nouveau_fb(parent);
	struct nouveau_bios *bios = nouveau_bios(pfb);
	struct nouveau_gpio *gpio = nouveau_gpio(pfb);
	struct dcb_gpio_func func;
	struct nve0_ram *ram;
	int ret;

	ret = nvc0_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	switch (ram->base.type) {
	case NV_MEM_TYPE_DDR3:
	case NV_MEM_TYPE_GDDR5:
		ram->base.calc = nve0_ram_calc;
		ram->base.prog = nve0_ram_prog;
		ram->base.tidy = nve0_ram_tidy;
		break;
	default:
		nv_warn(pfb, "reclocking of this RAM type is unsupported\n");
		break;
	}

	// parse bios data for both pll's
	ret = nvbios_pll_parse(bios, 0x0c, &ram->fuc.refpll);
	if (ret) {
		nv_error(pfb, "mclk refpll data not found\n");
		return ret;
	}

	ret = nvbios_pll_parse(bios, 0x04, &ram->fuc.mempll);
	if (ret) {
		nv_error(pfb, "mclk pll data not found\n");
		return ret;
	}

	ret = gpio->find(gpio, 0, 0x18, DCB_GPIO_UNUSED, &func);
	if (ret == 0) {
		ram->fuc.r_gpioMV = ramfuc_reg(0x00d610 + (func.line * 0x04));
		ram->fuc.r_funcMV[0] = (func.log[0] ^ 2) << 12;
		ram->fuc.r_funcMV[1] = (func.log[1] ^ 2) << 12;
	}

	ret = gpio->find(gpio, 0, 0x2e, DCB_GPIO_UNUSED, &func);
	if (ret == 0) {
		ram->fuc.r_gpio2E = ramfuc_reg(0x00d610 + (func.line * 0x04));
		ram->fuc.r_func2E[0] = (func.log[0] ^ 2) << 12;
		ram->fuc.r_func2E[1] = (func.log[1] ^ 2) << 12;
	}

	ram->fuc.r_gpiotrig = ramfuc_reg(0x00d604);

	ram->fuc.r_0x132020 = ramfuc_reg(0x132020);
	ram->fuc.r_0x132028 = ramfuc_reg(0x132028);
	ram->fuc.r_0x132024 = ramfuc_reg(0x132024);
	ram->fuc.r_0x132030 = ramfuc_reg(0x132030);
	ram->fuc.r_0x132034 = ramfuc_reg(0x132034);
	ram->fuc.r_0x132000 = ramfuc_reg(0x132000);
	ram->fuc.r_0x132004 = ramfuc_reg(0x132004);
	ram->fuc.r_0x132040 = ramfuc_reg(0x132040);

	ram->fuc.r_0x10f248 = ramfuc_reg(0x10f248);
	ram->fuc.r_0x10f290 = ramfuc_reg(0x10f290);
	ram->fuc.r_0x10f294 = ramfuc_reg(0x10f294);
	ram->fuc.r_0x10f298 = ramfuc_reg(0x10f298);
	ram->fuc.r_0x10f29c = ramfuc_reg(0x10f29c);
	ram->fuc.r_0x10f2a0 = ramfuc_reg(0x10f2a0);
	ram->fuc.r_0x10f2a4 = ramfuc_reg(0x10f2a4);
	ram->fuc.r_0x10f2a8 = ramfuc_reg(0x10f2a8);
	ram->fuc.r_0x10f2ac = ramfuc_reg(0x10f2ac);
	ram->fuc.r_0x10f2cc = ramfuc_reg(0x10f2cc);
	ram->fuc.r_0x10f2e8 = ramfuc_reg(0x10f2e8);
	ram->fuc.r_0x10f250 = ramfuc_reg(0x10f250);
	ram->fuc.r_0x10f24c = ramfuc_reg(0x10f24c);
	ram->fuc.r_0x10fec4 = ramfuc_reg(0x10fec4);
	ram->fuc.r_0x10fec8 = ramfuc_reg(0x10fec8);
	ram->fuc.r_0x10f604 = ramfuc_reg(0x10f604);
	ram->fuc.r_0x10f614 = ramfuc_reg(0x10f614);
	ram->fuc.r_0x10f610 = ramfuc_reg(0x10f610);
	ram->fuc.r_0x100770 = ramfuc_reg(0x100770);
	ram->fuc.r_0x100778 = ramfuc_reg(0x100778);
	ram->fuc.r_0x10f224 = ramfuc_reg(0x10f224);

	ram->fuc.r_0x10f870 = ramfuc_reg(0x10f870);
	ram->fuc.r_0x10f698 = ramfuc_reg(0x10f698);
	ram->fuc.r_0x10f694 = ramfuc_reg(0x10f694);
	ram->fuc.r_0x10f6b8 = ramfuc_reg(0x10f6b8);
	ram->fuc.r_0x10f808 = ramfuc_reg(0x10f808);
	ram->fuc.r_0x10f670 = ramfuc_reg(0x10f670);
	ram->fuc.r_0x10f60c = ramfuc_reg(0x10f60c);
	ram->fuc.r_0x10f830 = ramfuc_reg(0x10f830);
	ram->fuc.r_0x1373ec = ramfuc_reg(0x1373ec);
	ram->fuc.r_0x10f800 = ramfuc_reg(0x10f800);
	ram->fuc.r_0x10f82c = ramfuc_reg(0x10f82c);

	ram->fuc.r_0x10f978 = ramfuc_reg(0x10f978);
	ram->fuc.r_0x10f910 = ramfuc_reg(0x10f910);
	ram->fuc.r_0x10f914 = ramfuc_reg(0x10f914);

	switch (ram->base.type) {
	case NV_MEM_TYPE_GDDR5:
		ram->fuc.r_mr[0] = ramfuc_reg(0x10f300);
		ram->fuc.r_mr[1] = ramfuc_reg(0x10f330);
		ram->fuc.r_mr[2] = ramfuc_reg(0x10f334);
		ram->fuc.r_mr[3] = ramfuc_reg(0x10f338);
		ram->fuc.r_mr[4] = ramfuc_reg(0x10f33c);
		ram->fuc.r_mr[5] = ramfuc_reg(0x10f340);
		ram->fuc.r_mr[6] = ramfuc_reg(0x10f344);
		ram->fuc.r_mr[7] = ramfuc_reg(0x10f348);
		ram->fuc.r_mr[8] = ramfuc_reg(0x10f354);
		ram->fuc.r_mr[15] = ramfuc_reg(0x10f34c);
		break;
	case NV_MEM_TYPE_DDR3:
		ram->fuc.r_mr[0] = ramfuc_reg(0x10f300);
		ram->fuc.r_mr[2] = ramfuc_reg(0x10f320);
		break;
	default:
		break;
	}

	ram->fuc.r_0x62c000 = ramfuc_reg(0x62c000);
	ram->fuc.r_0x10f200 = ramfuc_reg(0x10f200);
	ram->fuc.r_0x10f210 = ramfuc_reg(0x10f210);
	ram->fuc.r_0x10f310 = ramfuc_reg(0x10f310);
	ram->fuc.r_0x10f314 = ramfuc_reg(0x10f314);
	ram->fuc.r_0x10f318 = ramfuc_reg(0x10f318);
	ram->fuc.r_0x10f090 = ramfuc_reg(0x10f090);
	ram->fuc.r_0x10f69c = ramfuc_reg(0x10f69c);
	ram->fuc.r_0x10f824 = ramfuc_reg(0x10f824);
	ram->fuc.r_0x1373f0 = ramfuc_reg(0x1373f0);
	ram->fuc.r_0x1373f4 = ramfuc_reg(0x1373f4);
	ram->fuc.r_0x137320 = ramfuc_reg(0x137320);
	ram->fuc.r_0x10f65c = ramfuc_reg(0x10f65c);
	ram->fuc.r_0x10f6bc = ramfuc_reg(0x10f6bc);
	ram->fuc.r_0x100710 = ramfuc_reg(0x100710);
	ram->fuc.r_0x10f750 = ramfuc_reg(0x10f750);
	return 0;
}

struct nouveau_oclass
nve0_ram_oclass = {
	.handle = 0,
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nve0_ram_ctor,
		.dtor = _nouveau_ram_dtor,
		.init = nve0_ram_init,
		.fini = _nouveau_ram_fini,
	}
};
