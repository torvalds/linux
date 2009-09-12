/* MN10300 Misalignment fixup handler
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/smp.h>
#include <asm/pgalloc.h>
#include <asm/cpu-regs.h>
#include <asm/busctl-regs.h>
#include <asm/fpu.h>
#include <asm/gdb-stub.h>
#include <asm/asm-offsets.h>

#if 0
#define kdebug(FMT, ...) printk(KERN_DEBUG "MISALIGN: "FMT"\n", ##__VA_ARGS__)
#else
#define kdebug(FMT, ...) do {} while (0)
#endif

static int misalignment_addr(unsigned long *registers, unsigned long sp,
			     unsigned params, unsigned opcode,
			     unsigned long disp,
			     void **_address, unsigned long **_postinc,
			     unsigned long *_inc);

static int misalignment_reg(unsigned long *registers, unsigned params,
			    unsigned opcode, unsigned long disp,
			    unsigned long **_register);

static void misalignment_MOV_Lcc(struct pt_regs *regs, uint32_t opcode);

static const unsigned Dreg_index[] = {
	REG_D0 >> 2, REG_D1 >> 2, REG_D2 >> 2, REG_D3 >> 2
};

static const unsigned Areg_index[] = {
	REG_A0 >> 2, REG_A1 >> 2, REG_A2 >> 2, REG_A3 >> 2
};

static const unsigned Rreg_index[] = {
	REG_E0 >> 2, REG_E1 >> 2, REG_E2 >> 2, REG_E3 >> 2,
	REG_E4 >> 2, REG_E5 >> 2, REG_E6 >> 2, REG_E7 >> 2,
	REG_A0 >> 2, REG_A1 >> 2, REG_A2 >> 2, REG_A3 >> 2,
	REG_D0 >> 2, REG_D1 >> 2, REG_D2 >> 2, REG_D3 >> 2
};

enum format_id {
	FMT_S0,
	FMT_S1,
	FMT_S2,
	FMT_S4,
	FMT_D0,
	FMT_D1,
	FMT_D2,
	FMT_D4,
	FMT_D6,
	FMT_D7,
	FMT_D8,
	FMT_D9,
	FMT_D10,
};

static const struct {
	u_int8_t opsz, dispsz;
} format_tbl[16] = {
	[FMT_S0]	= { 8,	0	},
	[FMT_S1]	= { 8,	8	},
	[FMT_S2]	= { 8,	16	},
	[FMT_S4]	= { 8,	32	},
	[FMT_D0]	= { 16,	0	},
	[FMT_D1]	= { 16,	8	},
	[FMT_D2]	= { 16,	16	},
	[FMT_D4]	= { 16,	32	},
	[FMT_D6]	= { 24,	0	},
	[FMT_D7]	= { 24,	8	},
	[FMT_D8]	= { 24,	24	},
	[FMT_D9]	= { 24,	32	},
	[FMT_D10]	= { 32,	0	},
};

enum value_id {
	DM0,		/* data reg in opcode in bits 0-1 */
	DM1,		/* data reg in opcode in bits 2-3 */
	DM2,		/* data reg in opcode in bits 4-5 */
	AM0,		/* addr reg in opcode in bits 0-1 */
	AM1,		/* addr reg in opcode in bits 2-3 */
	AM2,		/* addr reg in opcode in bits 4-5 */
	RM0,		/* reg in opcode in bits 0-3 */
	RM1,		/* reg in opcode in bits 2-5 */
	RM2,		/* reg in opcode in bits 4-7 */
	RM4,		/* reg in opcode in bits 8-11 */
	RM6,		/* reg in opcode in bits 12-15 */

	RD0,		/* reg in displacement in bits 0-3 */
	RD2,		/* reg in displacement in bits 4-7 */

	SP,		/* stack pointer */

	SD8,		/* 8-bit signed displacement */
	SD16,		/* 16-bit signed displacement */
	SD24,		/* 24-bit signed displacement */
	SIMM4_2,	/* 4-bit signed displacement in opcode bits 4-7 */
	SIMM8,		/* 8-bit signed immediate */
	IMM8,		/* 8-bit unsigned immediate */
	IMM16,		/* 16-bit unsigned immediate */
	IMM24,		/* 24-bit unsigned immediate */
	IMM32,		/* 32-bit unsigned immediate */
	IMM32_HIGH8,	/* 32-bit unsigned immediate, LSB in opcode */

	IMM32_MEM,	/* 32-bit unsigned displacement */
	IMM32_HIGH8_MEM, /* 32-bit unsigned displacement, LSB in opcode */

	DN0	= DM0,
	DN1	= DM1,
	DN2	= DM2,
	AN0	= AM0,
	AN1	= AM1,
	AN2	= AM2,
	RN0	= RM0,
	RN1	= RM1,
	RN2	= RM2,
	RN4	= RM4,
	RN6	= RM6,
	DI	= DM1,
	RI	= RM2,

};

struct mn10300_opcode {
	const char	name[8];
	u_int32_t	opcode;
	u_int32_t	opmask;
	unsigned	exclusion;

	enum format_id	format;

	unsigned	cpu_mask;
#define AM33	330

	unsigned	params[2];
#define MEM(ADDR)		(0x80000000 | (ADDR))
#define MEM2(ADDR1, ADDR2)	(0x80000000 | (ADDR1) << 8 | (ADDR2))
#define MEMINC(ADDR)		(0x81000000 | (ADDR))
#define MEMINC2(ADDR, INC)	(0x81000000 | (ADDR) << 8 | (INC))
};

/* LIBOPCODES EXCERPT
   Assemble Matsushita MN10300 instructions.
   Copyright 1996, 1997, 1998, 1999, 2000 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public Licence as published by
   the Free Software Foundation; either version 2 of the Licence, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public Licence for more details.

   You should have received a copy of the GNU General Public Licence
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/
static const struct mn10300_opcode mn10300_opcodes[] = {
{ "mov",	0x4200,	     0xf300,	  0,    FMT_S1, 0,	{DM1, MEM2(IMM8, SP)}},
{ "mov",	0x4300,	     0xf300,	  0,    FMT_S1, 0,	{AM1, MEM2(IMM8, SP)}},
{ "mov",	0x5800,	     0xfc00,	  0,    FMT_S1, 0,	{MEM2(IMM8, SP), DN0}},
{ "mov",	0x5c00,	     0xfc00,	  0,    FMT_S1, 0,	{MEM2(IMM8, SP), AN0}},
{ "mov",	0x60,	     0xf0,	  0,    FMT_S0, 0,	{DM1, MEM(AN0)}},
{ "mov",	0x70,	     0xf0,	  0,    FMT_S0, 0,	{MEM(AM0), DN1}},
{ "mov",	0xf000,	     0xfff0,	  0,    FMT_D0, 0,	{MEM(AM0), AN1}},
{ "mov",	0xf010,	     0xfff0,	  0,    FMT_D0, 0,	{AM1, MEM(AN0)}},
{ "mov",	0xf300,	     0xffc0,	  0,    FMT_D0, 0,	{MEM2(DI, AM0), DN2}},
{ "mov",	0xf340,	     0xffc0,	  0,    FMT_D0, 0,	{DM2, MEM2(DI, AN0)}},
{ "mov",	0xf380,	     0xffc0,	  0,    FMT_D0, 0,	{MEM2(DI, AM0), AN2}},
{ "mov",	0xf3c0,	     0xffc0,	  0,    FMT_D0, 0,	{AM2, MEM2(DI, AN0)}},
{ "mov",	0xf80000,    0xfff000,    0,    FMT_D1, 0,	{MEM2(SD8, AM0), DN1}},
{ "mov",	0xf81000,    0xfff000,    0,    FMT_D1, 0,	{DM1, MEM2(SD8, AN0)}},
{ "mov",	0xf82000,    0xfff000,    0,    FMT_D1, 0,	{MEM2(SD8,AM0), AN1}},
{ "mov",	0xf83000,    0xfff000,    0,    FMT_D1, 0,	{AM1, MEM2(SD8, AN0)}},
{ "mov",	0xf90a00,    0xffff00,    0,    FMT_D6, AM33,	{MEM(RM0), RN2}},
{ "mov",	0xf91a00,    0xffff00,    0,    FMT_D6, AM33,	{RM2, MEM(RN0)}},
{ "mov",	0xf96a00,    0xffff00,    0x12, FMT_D6, AM33,	{MEMINC(RM0), RN2}},
{ "mov",	0xf97a00,    0xffff00,    0,	FMT_D6, AM33,	{RM2, MEMINC(RN0)}},
{ "mov",	0xfa000000,  0xfff00000,  0,    FMT_D2, 0,	{MEM2(SD16, AM0), DN1}},
{ "mov",	0xfa100000,  0xfff00000,  0,    FMT_D2, 0,	{DM1, MEM2(SD16, AN0)}},
{ "mov",	0xfa200000,  0xfff00000,  0,    FMT_D2, 0,	{MEM2(SD16, AM0), AN1}},
{ "mov",	0xfa300000,  0xfff00000,  0,    FMT_D2, 0,	{AM1, MEM2(SD16, AN0)}},
{ "mov",	0xfa900000,  0xfff30000,  0,    FMT_D2, 0,	{AM1, MEM2(IMM16, SP)}},
{ "mov",	0xfa910000,  0xfff30000,  0,    FMT_D2, 0,	{DM1, MEM2(IMM16, SP)}},
{ "mov",	0xfab00000,  0xfffc0000,  0,    FMT_D2, 0,	{MEM2(IMM16, SP), AN0}},
{ "mov",	0xfab40000,  0xfffc0000,  0,    FMT_D2, 0,	{MEM2(IMM16, SP), DN0}},
{ "mov",	0xfb0a0000,  0xffff0000,  0,    FMT_D7, AM33,	{MEM2(SD8, RM0), RN2}},
{ "mov",	0xfb1a0000,  0xffff0000,  0,    FMT_D7, AM33,	{RM2, MEM2(SD8, RN0)}},
{ "mov",	0xfb6a0000,  0xffff0000,  0x22, FMT_D7, AM33,	{MEMINC2 (RM0, SIMM8), RN2}},
{ "mov",	0xfb7a0000,  0xffff0000,  0,	FMT_D7, AM33,	{RM2, MEMINC2 (RN0, SIMM8)}},
{ "mov",	0xfb8a0000,  0xffff0f00,  0,    FMT_D7, AM33,	{MEM2(IMM8, SP), RN2}},
{ "mov",	0xfb8e0000,  0xffff000f,  0,    FMT_D7, AM33,	{MEM2(RI, RM0), RD2}},
{ "mov",	0xfb9a0000,  0xffff0f00,  0,    FMT_D7, AM33,	{RM2, MEM2(IMM8, SP)}},
{ "mov",	0xfb9e0000,  0xffff000f,  0,    FMT_D7, AM33,	{RD2, MEM2(RI, RN0)}},
{ "mov",	0xfc000000,  0xfff00000,  0,    FMT_D4, 0,	{MEM2(IMM32,AM0), DN1}},
{ "mov",	0xfc100000,  0xfff00000,  0,    FMT_D4, 0,	{DM1, MEM2(IMM32,AN0)}},
{ "mov",	0xfc200000,  0xfff00000,  0,    FMT_D4, 0,	{MEM2(IMM32,AM0), AN1}},
{ "mov",	0xfc300000,  0xfff00000,  0,    FMT_D4, 0,	{AM1, MEM2(IMM32,AN0)}},
{ "mov",	0xfc800000,  0xfff30000,  0,    FMT_D4, 0,	{AM1, MEM(IMM32_MEM)}},
{ "mov",	0xfc810000,  0xfff30000,  0,    FMT_D4, 0,	{DM1, MEM(IMM32_MEM)}},
{ "mov",	0xfc900000,  0xfff30000,  0,    FMT_D4, 0,	{AM1, MEM2(IMM32, SP)}},
{ "mov",	0xfc910000,  0xfff30000,  0,    FMT_D4, 0,	{DM1, MEM2(IMM32, SP)}},
{ "mov",	0xfca00000,  0xfffc0000,  0,    FMT_D4, 0,	{MEM(IMM32_MEM), AN0}},
{ "mov",	0xfca40000,  0xfffc0000,  0,    FMT_D4, 0,	{MEM(IMM32_MEM), DN0}},
{ "mov",	0xfcb00000,  0xfffc0000,  0,    FMT_D4, 0,	{MEM2(IMM32, SP), AN0}},
{ "mov",	0xfcb40000,  0xfffc0000,  0,    FMT_D4, 0,	{MEM2(IMM32, SP), DN0}},
{ "mov",	0xfd0a0000,  0xffff0000,  0,    FMT_D8, AM33,	{MEM2(SD24, RM0), RN2}},
{ "mov",	0xfd1a0000,  0xffff0000,  0,    FMT_D8, AM33,	{RM2, MEM2(SD24, RN0)}},
{ "mov",	0xfd6a0000,  0xffff0000,  0x22, FMT_D8, AM33,	{MEMINC2 (RM0, IMM24), RN2}},
{ "mov",	0xfd7a0000,  0xffff0000,  0,	FMT_D8, AM33,	{RM2, MEMINC2 (RN0, IMM24)}},
{ "mov",	0xfd8a0000,  0xffff0f00,  0,    FMT_D8, AM33,	{MEM2(IMM24, SP), RN2}},
{ "mov",	0xfd9a0000,  0xffff0f00,  0,    FMT_D8, AM33,	{RM2, MEM2(IMM24, SP)}},
{ "mov",	0xfe0a0000,  0xffff0000,  0,    FMT_D9, AM33,	{MEM2(IMM32_HIGH8,RM0), RN2}},
{ "mov",	0xfe0a0000,  0xffff0000,  0,    FMT_D9, AM33,	{MEM2(IMM32_HIGH8,RM0), RN2}},
{ "mov",	0xfe0e0000,  0xffff0f00,  0,    FMT_D9, AM33,	{MEM(IMM32_HIGH8_MEM), RN2}},
{ "mov",	0xfe1a0000,  0xffff0000,  0,    FMT_D9, AM33,	{RM2, MEM2(IMM32_HIGH8, RN0)}},
{ "mov",	0xfe1a0000,  0xffff0000,  0,    FMT_D9, AM33,	{RM2, MEM2(IMM32_HIGH8, RN0)}},
{ "mov",	0xfe1e0000,  0xffff0f00,  0,    FMT_D9, AM33,	{RM2, MEM(IMM32_HIGH8_MEM)}},
{ "mov",	0xfe6a0000,  0xffff0000,  0x22, FMT_D9, AM33,	{MEMINC2 (RM0, IMM32_HIGH8), RN2}},
{ "mov",	0xfe7a0000,  0xffff0000,  0,	FMT_D9, AM33,	{RN2, MEMINC2 (RM0, IMM32_HIGH8)}},
{ "mov",	0xfe8a0000,  0xffff0f00,  0,    FMT_D9, AM33,	{MEM2(IMM32_HIGH8, SP), RN2}},
{ "mov",	0xfe9a0000,  0xffff0f00,  0,    FMT_D9, AM33,	{RM2, MEM2(IMM32_HIGH8, SP)}},

{ "movhu",	0xf060,	     0xfff0,	  0,    FMT_D0, 0,	{MEM(AM0), DN1}},
{ "movhu",	0xf070,	     0xfff0,	  0,    FMT_D0, 0,	{DM1, MEM(AN0)}},
{ "movhu",	0xf480,	     0xffc0,	  0,    FMT_D0, 0,	{MEM2(DI, AM0), DN2}},
{ "movhu",	0xf4c0,	     0xffc0,	  0,    FMT_D0, 0,	{DM2, MEM2(DI, AN0)}},
{ "movhu",	0xf86000,    0xfff000,    0,    FMT_D1, 0,	{MEM2(SD8, AM0), DN1}},
{ "movhu",	0xf87000,    0xfff000,    0,    FMT_D1, 0,	{DM1, MEM2(SD8, AN0)}},
{ "movhu",	0xf89300,    0xfff300,    0,    FMT_D1, 0,	{DM1, MEM2(IMM8, SP)}},
{ "movhu",	0xf8bc00,    0xfffc00,    0,    FMT_D1, 0,	{MEM2(IMM8, SP), DN0}},
{ "movhu",	0xf94a00,    0xffff00,    0,    FMT_D6, AM33,	{MEM(RM0), RN2}},
{ "movhu",	0xf95a00,    0xffff00,    0,    FMT_D6, AM33,	{RM2, MEM(RN0)}},
{ "movhu",	0xf9ea00,    0xffff00,    0x12, FMT_D6, AM33,	{MEMINC(RM0), RN2}},
{ "movhu",	0xf9fa00,    0xffff00,    0,	FMT_D6, AM33,	{RM2, MEMINC(RN0)}},
{ "movhu",	0xfa600000,  0xfff00000,  0,    FMT_D2, 0,	{MEM2(SD16, AM0), DN1}},
{ "movhu",	0xfa700000,  0xfff00000,  0,    FMT_D2, 0,	{DM1, MEM2(SD16, AN0)}},
{ "movhu",	0xfa930000,  0xfff30000,  0,    FMT_D2, 0,	{DM1, MEM2(IMM16, SP)}},
{ "movhu",	0xfabc0000,  0xfffc0000,  0,    FMT_D2, 0,	{MEM2(IMM16, SP), DN0}},
{ "movhu",	0xfb4a0000,  0xffff0000,  0,    FMT_D7, AM33,	{MEM2(SD8, RM0), RN2}},
{ "movhu",	0xfb5a0000,  0xffff0000,  0,    FMT_D7, AM33,	{RM2, MEM2(SD8, RN0)}},
{ "movhu",	0xfbca0000,  0xffff0f00,  0,    FMT_D7, AM33,	{MEM2(IMM8, SP), RN2}},
{ "movhu",	0xfbce0000,  0xffff000f,  0,    FMT_D7, AM33,	{MEM2(RI, RM0), RD2}},
{ "movhu",	0xfbda0000,  0xffff0f00,  0,    FMT_D7, AM33,	{RM2, MEM2(IMM8, SP)}},
{ "movhu",	0xfbde0000,  0xffff000f,  0,    FMT_D7, AM33,	{RD2, MEM2(RI, RN0)}},
{ "movhu",	0xfbea0000,  0xffff0000,  0x22, FMT_D7, AM33,	{MEMINC2 (RM0, SIMM8), RN2}},
{ "movhu",	0xfbfa0000,  0xffff0000,  0,	FMT_D7, AM33,	{RM2, MEMINC2 (RN0, SIMM8)}},
{ "movhu",	0xfc600000,  0xfff00000,  0,    FMT_D4, 0,	{MEM2(IMM32,AM0), DN1}},
{ "movhu",	0xfc700000,  0xfff00000,  0,    FMT_D4, 0,	{DM1, MEM2(IMM32,AN0)}},
{ "movhu",	0xfc830000,  0xfff30000,  0,    FMT_D4, 0,	{DM1, MEM(IMM32_MEM)}},
{ "movhu",	0xfc930000,  0xfff30000,  0,    FMT_D4, 0,	{DM1, MEM2(IMM32, SP)}},
{ "movhu",	0xfcac0000,  0xfffc0000,  0,    FMT_D4, 0,	{MEM(IMM32_MEM), DN0}},
{ "movhu",	0xfcbc0000,  0xfffc0000,  0,    FMT_D4, 0,	{MEM2(IMM32, SP), DN0}},
{ "movhu",	0xfd4a0000,  0xffff0000,  0,    FMT_D8, AM33,	{MEM2(SD24, RM0), RN2}},
{ "movhu",	0xfd5a0000,  0xffff0000,  0,    FMT_D8, AM33,	{RM2, MEM2(SD24, RN0)}},
{ "movhu",	0xfdca0000,  0xffff0f00,  0,    FMT_D8, AM33,	{MEM2(IMM24, SP), RN2}},
{ "movhu",	0xfdda0000,  0xffff0f00,  0,    FMT_D8, AM33,	{RM2, MEM2(IMM24, SP)}},
{ "movhu",	0xfdea0000,  0xffff0000,  0x22, FMT_D8, AM33,	{MEMINC2 (RM0, IMM24), RN2}},
{ "movhu",	0xfdfa0000,  0xffff0000,  0,	FMT_D8, AM33,	{RM2, MEMINC2 (RN0, IMM24)}},
{ "movhu",	0xfe4a0000,  0xffff0000,  0,    FMT_D9, AM33,	{MEM2(IMM32_HIGH8,RM0), RN2}},
{ "movhu",	0xfe4e0000,  0xffff0f00,  0,    FMT_D9, AM33,	{MEM(IMM32_HIGH8_MEM), RN2}},
{ "movhu",	0xfe5a0000,  0xffff0000,  0,    FMT_D9, AM33,	{RM2, MEM2(IMM32_HIGH8, RN0)}},
{ "movhu",	0xfe5e0000,  0xffff0f00,  0,    FMT_D9, AM33,	{RM2, MEM(IMM32_HIGH8_MEM)}},
{ "movhu",	0xfeca0000,  0xffff0f00,  0,    FMT_D9, AM33,	{MEM2(IMM32_HIGH8, SP), RN2}},
{ "movhu",	0xfeda0000,  0xffff0f00,  0,    FMT_D9, AM33,	{RM2, MEM2(IMM32_HIGH8, SP)}},
{ "movhu",	0xfeea0000,  0xffff0000,  0x22, FMT_D9, AM33,	{MEMINC2 (RM0, IMM32_HIGH8), RN2}},
{ "movhu",	0xfefa0000,  0xffff0000,  0,	FMT_D9, AM33,	{RN2, MEMINC2 (RM0, IMM32_HIGH8)}},

{ "mov_llt",	0xf7e00000,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_lgt",	0xf7e00001,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_lge",	0xf7e00002,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_lle",	0xf7e00003,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_lcs",	0xf7e00004,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_lhi",	0xf7e00005,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_lcc",	0xf7e00006,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_lls",	0xf7e00007,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_leq",	0xf7e00008,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_lne",	0xf7e00009,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},
{ "mov_lra",	0xf7e0000a,  0xffff000f,  0x22, FMT_D10, AM33,	 {MEMINC2 (RN4,SIMM4_2), RM6}},

{ "", 0, 0, 0, 0, 0, {0}},
};

/*
 * fix up misalignment problems where possible
 */
asmlinkage void misalignment(struct pt_regs *regs, enum exception_code code)
{
	const struct exception_table_entry *fixup;
	const struct mn10300_opcode *pop;
	unsigned long *registers = (unsigned long *) regs;
	unsigned long data, *store, *postinc, disp, inc, sp;
	mm_segment_t seg;
	siginfo_t info;
	uint32_t opcode, noc, xo, xm;
	uint8_t *pc, byte, datasz;
	void *address;
	unsigned tmp, npop, dispsz, loop;

	/* we don't fix up userspace misalignment faults */
	if (user_mode(regs))
		goto bus_error;

	sp = (unsigned long) regs + sizeof(*regs);

	kdebug("==>misalignment({pc=%lx,sp=%lx})", regs->pc, sp);

	if (regs->epsw & EPSW_IE)
		asm volatile("or %0,epsw" : : "i"(EPSW_IE));

	seg = get_fs();
	set_fs(KERNEL_DS);

	fixup = search_exception_tables(regs->pc);

	/* first thing to do is to match the opcode */
	pc = (u_int8_t *) regs->pc;

	if (__get_user(byte, pc) != 0)
		goto fetch_error;
	opcode = byte;
	noc = 8;

	for (pop = mn10300_opcodes; pop->name[0]; pop++) {
		npop = ilog2(pop->opcode | pop->opmask);
		if (npop <= 0 || npop > 31)
			continue;
		npop = (npop + 8) & ~7;

	got_more_bits:
		if (npop == noc) {
			if ((opcode & pop->opmask) == pop->opcode)
				goto found_opcode;
		} else if (npop > noc) {
			xo = pop->opcode >> (npop - noc);
			xm = pop->opmask >> (npop - noc);

			if ((opcode & xm) != xo)
				continue;

			/* we've got a partial match (an exact match on the
			 * first N bytes), so we need to get some more data */
			pc++;
			if (__get_user(byte, pc) != 0)
				goto fetch_error;
			opcode = opcode << 8 | byte;
			noc += 8;
			goto got_more_bits;
		} else {
			/* there's already been a partial match as long as the
			 * complete match we're now considering, so this one
			 * should't match */
			continue;
		}
	}

	/* didn't manage to find a fixup */
	printk(KERN_CRIT "MISALIGN: %lx: unsupported instruction %x\n",
	       regs->pc, opcode);

failed:
	set_fs(seg);
	if (die_if_no_fixup("misalignment error", regs, code))
		return;

bus_error:
	info.si_signo	= SIGBUS;
	info.si_errno	= 0;
	info.si_code	= BUS_ADRALN;
	info.si_addr	= (void *) regs->pc;
	force_sig_info(SIGBUS, &info, current);
	return;

	/* error reading opcodes */
fetch_error:
	printk(KERN_CRIT
	       "MISALIGN: %p: fault whilst reading instruction data\n",
	       pc);
	goto failed;

bad_addr_mode:
	printk(KERN_CRIT
	       "MISALIGN: %lx: unsupported addressing mode %x\n",
	       regs->pc, opcode);
	goto failed;

bad_reg_mode:
	printk(KERN_CRIT
	       "MISALIGN: %lx: unsupported register mode %x\n",
	       regs->pc, opcode);
	goto failed;

unsupported_instruction:
	printk(KERN_CRIT
	       "MISALIGN: %lx: unsupported instruction %x (%s)\n",
	       regs->pc, opcode, pop->name);
	goto failed;

transfer_failed:
	set_fs(seg);
	if (fixup) {
		regs->pc = fixup->fixup;
		return;
	}
	if (die_if_no_fixup("misalignment fixup", regs, code))
		return;

	info.si_signo	= SIGSEGV;
	info.si_errno	= 0;
	info.si_code	= 0;
	info.si_addr	= (void *) regs->pc;
	force_sig_info(SIGSEGV, &info, current);
	return;

	/* we matched the opcode */
found_opcode:
	kdebug("%lx: %x==%x { %x, %x }",
	       regs->pc, opcode, pop->opcode, pop->params[0], pop->params[1]);

	tmp = format_tbl[pop->format].opsz;
	if (tmp > noc)
		BUG(); /* match was less complete than it ought to have been */

	if (tmp < noc) {
		tmp = noc - tmp;
		opcode >>= tmp;
		pc -= tmp >> 3;
	}

	/* grab the extra displacement (note it's LSB first) */
	disp = 0;
	dispsz = format_tbl[pop->format].dispsz;
	for (loop = 0; loop < dispsz; loop += 8) {
		pc++;
		if (__get_user(byte, pc) != 0)
			goto fetch_error;
		disp |= byte << loop;
		kdebug("{%p} disp[%02x]=%02x", pc, loop, byte);
	}

	kdebug("disp=%lx", disp);

	set_fs(KERNEL_XDS);
	if (fixup)
		set_fs(seg);

	tmp = (pop->params[0] ^ pop->params[1]) & 0x80000000;
	if (!tmp) {
		printk(KERN_CRIT
		       "MISALIGN: %lx: insn not move to/from memory %x\n",
		       regs->pc, opcode);
		goto failed;
	}

	/* determine the data transfer size of the move */
	if (pop->name[3] == 0 || /* "mov" */
	    pop->name[4] == 'l') /* mov_lcc */
		inc = datasz = 4;
	else if (pop->name[3] == 'h') /* movhu */
		inc = datasz = 2;
	else
		goto unsupported_instruction;

	if (pop->params[0] & 0x80000000) {
		/* move memory to register */
		if (!misalignment_addr(registers, sp,
				       pop->params[0], opcode, disp,
				       &address, &postinc, &inc))
			goto bad_addr_mode;

		if (!misalignment_reg(registers, pop->params[1], opcode, disp,
				      &store))
			goto bad_reg_mode;

		kdebug("mov%u (%p),DARn", datasz, address);
		if (copy_from_user(&data, (void *) address, datasz) != 0)
			goto transfer_failed;
		if (pop->params[0] & 0x1000000) {
			kdebug("inc=%lx", inc);
			*postinc += inc;
		}

		*store = data;
		kdebug("loaded %lx", data);
	} else {
		/* move register to memory */
		if (!misalignment_reg(registers, pop->params[0], opcode, disp,
				      &store))
			goto bad_reg_mode;

		if (!misalignment_addr(registers, sp,
				       pop->params[1], opcode, disp,
				       &address, &postinc, &inc))
			goto bad_addr_mode;

		data = *store;

		kdebug("mov%u %lx,(%p)", datasz, data, address);
		if (copy_to_user((void *) address, &data, datasz) != 0)
			goto transfer_failed;
		if (pop->params[1] & 0x1000000)
			*postinc += inc;
	}

	tmp = format_tbl[pop->format].opsz + format_tbl[pop->format].dispsz;
	regs->pc += tmp >> 3;

	/* handle MOV_Lcc, which are currently the only FMT_D10 insns that
	 * access memory */
	if (pop->format == FMT_D10)
		misalignment_MOV_Lcc(regs, opcode);

	set_fs(seg);
}

/*
 * determine the address that was being accessed
 */
static int misalignment_addr(unsigned long *registers, unsigned long sp,
			     unsigned params, unsigned opcode,
			     unsigned long disp,
			     void **_address, unsigned long **_postinc,
			     unsigned long *_inc)
{
	unsigned long *postinc = NULL, address = 0, tmp;

	if (!(params & 0x1000000)) {
		kdebug("noinc");
		*_inc = 0;
		_inc = NULL;
	}

	params &= 0x00ffffff;

	do {
		switch (params & 0xff) {
		case DM0:
			postinc = &registers[Dreg_index[opcode & 0x03]];
			address += *postinc;
			break;
		case DM1:
			postinc = &registers[Dreg_index[opcode >> 2 & 0x03]];
			address += *postinc;
			break;
		case DM2:
			postinc = &registers[Dreg_index[opcode >> 4 & 0x03]];
			address += *postinc;
			break;
		case AM0:
			postinc = &registers[Areg_index[opcode & 0x03]];
			address += *postinc;
			break;
		case AM1:
			postinc = &registers[Areg_index[opcode >> 2 & 0x03]];
			address += *postinc;
			break;
		case AM2:
			postinc = &registers[Areg_index[opcode >> 4 & 0x03]];
			address += *postinc;
			break;
		case RM0:
			postinc = &registers[Rreg_index[opcode & 0x0f]];
			address += *postinc;
			break;
		case RM1:
			postinc = &registers[Rreg_index[opcode >> 2 & 0x0f]];
			address += *postinc;
			break;
		case RM2:
			postinc = &registers[Rreg_index[opcode >> 4 & 0x0f]];
			address += *postinc;
			break;
		case RM4:
			postinc = &registers[Rreg_index[opcode >> 8 & 0x0f]];
			address += *postinc;
			break;
		case RM6:
			postinc = &registers[Rreg_index[opcode >> 12 & 0x0f]];
			address += *postinc;
			break;
		case RD0:
			postinc = &registers[Rreg_index[disp & 0x0f]];
			address += *postinc;
			break;
		case RD2:
			postinc = &registers[Rreg_index[disp >> 4 & 0x0f]];
			address += *postinc;
			break;
		case SP:
			address += sp;
			break;

			/* displacements are either to be added to the address
			 * before use, or, in the case of post-inc addressing,
			 * to be added into the base register after use */
		case SD8:
		case SIMM8:
			disp = (long) (int8_t) (disp & 0xff);
			goto displace_or_inc;
		case SD16:
			disp = (long) (int16_t) (disp & 0xffff);
			goto displace_or_inc;
		case SD24:
			tmp = disp << 8;
			asm("asr 8,%0" : "=r"(tmp) : "0"(tmp));
			disp = (long) tmp;
			goto displace_or_inc;
		case SIMM4_2:
			tmp = opcode >> 4 & 0x0f;
			tmp <<= 28;
			asm("asr 28,%0" : "=r"(tmp) : "0"(tmp));
			disp = (long) tmp;
			goto displace_or_inc;
		case IMM8:
			disp &= 0x000000ff;
			goto displace_or_inc;
		case IMM16:
			disp &= 0x0000ffff;
			goto displace_or_inc;
		case IMM24:
			disp &= 0x00ffffff;
			goto displace_or_inc;
		case IMM32:
		case IMM32_MEM:
		case IMM32_HIGH8:
		case IMM32_HIGH8_MEM:
		displace_or_inc:
			kdebug("%s %lx", _inc ? "incr" : "disp", disp);
			if (!_inc)
				address += disp;
			else
				*_inc = disp;
			break;
		default:
			BUG();
			return 0;
		}
	} while ((params >>= 8));

	*_address = (void *) address;
	*_postinc = postinc;
	return 1;
}

/*
 * determine the register that is acting as source/dest
 */
static int misalignment_reg(unsigned long *registers, unsigned params,
			    unsigned opcode, unsigned long disp,
			    unsigned long **_register)
{
	params &= 0x7fffffff;

	if (params & 0xffffff00)
		return 0;

	switch (params & 0xff) {
	case DM0:
		*_register = &registers[Dreg_index[opcode & 0x03]];
		break;
	case DM1:
		*_register = &registers[Dreg_index[opcode >> 2 & 0x03]];
		break;
	case DM2:
		*_register = &registers[Dreg_index[opcode >> 4 & 0x03]];
		break;
	case AM0:
		*_register = &registers[Areg_index[opcode & 0x03]];
		break;
	case AM1:
		*_register = &registers[Areg_index[opcode >> 2 & 0x03]];
		break;
	case AM2:
		*_register = &registers[Areg_index[opcode >> 4 & 0x03]];
		break;
	case RM0:
		*_register = &registers[Rreg_index[opcode & 0x0f]];
		break;
	case RM1:
		*_register = &registers[Rreg_index[opcode >> 2 & 0x0f]];
		break;
	case RM2:
		*_register = &registers[Rreg_index[opcode >> 4 & 0x0f]];
		break;
	case RM4:
		*_register = &registers[Rreg_index[opcode >> 8 & 0x0f]];
		break;
	case RM6:
		*_register = &registers[Rreg_index[opcode >> 12 & 0x0f]];
		break;
	case RD0:
		*_register = &registers[Rreg_index[disp & 0x0f]];
		break;
	case RD2:
		*_register = &registers[Rreg_index[disp >> 4 & 0x0f]];
		break;
	case SP:
		*_register = &registers[REG_SP >> 2];
		break;

	default:
		BUG();
		return 0;
	}

	return 1;
}

/*
 * handle the conditional loop part of the move-and-loop instructions
 */
static void misalignment_MOV_Lcc(struct pt_regs *regs, uint32_t opcode)
{
	unsigned long epsw = regs->epsw;
	unsigned long NxorV;

	kdebug("MOV_Lcc %x [flags=%lx]", opcode, epsw & 0xf);

	/* calculate N^V and shift onto the same bit position as Z */
	NxorV = ((epsw >> 3) ^ epsw >> 1) & 1;

	switch (opcode & 0xf) {
	case 0x0: /* MOV_LLT: N^V */
		if (NxorV)
			goto take_the_loop;
		return;
	case 0x1: /* MOV_LGT: ~(Z or (N^V))*/
		if (!((epsw & EPSW_FLAG_Z) | NxorV))
			goto take_the_loop;
		return;
	case 0x2: /* MOV_LGE: ~(N^V) */
		if (!NxorV)
			goto take_the_loop;
		return;
	case 0x3: /* MOV_LLE: Z or (N^V) */
		if ((epsw & EPSW_FLAG_Z) | NxorV)
			goto take_the_loop;
		return;

	case 0x4: /* MOV_LCS: C */
		if (epsw & EPSW_FLAG_C)
			goto take_the_loop;
		return;
	case 0x5: /* MOV_LHI: ~(C or Z) */
		if (!(epsw & (EPSW_FLAG_C | EPSW_FLAG_Z)))
			goto take_the_loop;
		return;
	case 0x6: /* MOV_LCC: ~C */
		if (!(epsw & EPSW_FLAG_C))
			goto take_the_loop;
		return;
	case 0x7: /* MOV_LLS: C or Z */
		if (epsw & (EPSW_FLAG_C | EPSW_FLAG_Z))
			goto take_the_loop;
		return;

	case 0x8: /* MOV_LEQ: Z */
		if (epsw & EPSW_FLAG_Z)
			goto take_the_loop;
		return;
	case 0x9: /* MOV_LNE: ~Z */
		if (!(epsw & EPSW_FLAG_Z))
			goto take_the_loop;
		return;
	case 0xa: /* MOV_LRA: always */
		goto take_the_loop;

	default:
		BUG();
	}

take_the_loop:
	/* wind the PC back to just after the SETLB insn */
	kdebug("loop LAR=%lx", regs->lar);
	regs->pc = regs->lar - 4;
}

/*
 * misalignment handler tests
 */
#ifdef CONFIG_TEST_MISALIGNMENT_HANDLER
static u8 __initdata testbuf[512] __attribute__((aligned(16))) = {
	[257] = 0x11,
	[258] = 0x22,
	[259] = 0x33,
	[260] = 0x44,
};

#define ASSERTCMP(X, OP, Y)						\
do {									\
	if (unlikely(!((X) OP (Y)))) {					\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "MISALIGN: Assertion failed at line %u\n", \
		       __LINE__);					\
		printk(KERN_ERR "0x%lx " #OP " 0x%lx is false\n",	\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while(0)

static int __init test_misalignment(void)
{
	register void *r asm("e0");
	register u32 y asm("e1");
	void *p = testbuf, *q;
	u32 tmp, tmp2, x;

	printk(KERN_NOTICE "==>test_misalignment() [testbuf=%p]\n", p);
	p++;

	printk(KERN_NOTICE "___ MOV (Am),Dn ___\n");
	q = p + 256;
	asm volatile("mov	(%0),%1" : "+a"(q), "=d"(x));
	ASSERTCMP(q, ==, p + 256);
	ASSERTCMP(x, ==, 0x44332211);

	printk(KERN_NOTICE "___ MOV (256,Am),Dn ___\n");
	q = p;
	asm volatile("mov	(256,%0),%1" : "+a"(q), "=d"(x));
	ASSERTCMP(q, ==, p);
	ASSERTCMP(x, ==, 0x44332211);

	printk(KERN_NOTICE "___ MOV (Di,Am),Dn ___\n");
	tmp = 256;
	q = p;
	asm volatile("mov	(%2,%0),%1" : "+a"(q), "=d"(x), "+d"(tmp));
	ASSERTCMP(q, ==, p);
	ASSERTCMP(x, ==, 0x44332211);
	ASSERTCMP(tmp, ==, 256);

	printk(KERN_NOTICE "___ MOV (256,Rm),Rn ___\n");
	r = p;
	asm volatile("mov	(256,%0),%1" : "+r"(r), "=r"(y));
	ASSERTCMP(r, ==, p);
	ASSERTCMP(y, ==, 0x44332211);

	printk(KERN_NOTICE "___ MOV (Rm+),Rn ___\n");
	r = p + 256;
	asm volatile("mov	(%0+),%1" : "+r"(r), "=r"(y));
	ASSERTCMP(r, ==, p + 256 + 4);
	ASSERTCMP(y, ==, 0x44332211);

	printk(KERN_NOTICE "___ MOV (Rm+,8),Rn ___\n");
	r = p + 256;
	asm volatile("mov	(%0+,8),%1" : "+r"(r), "=r"(y));
	ASSERTCMP(r, ==, p + 256 + 8);
	ASSERTCMP(y, ==, 0x44332211);

	printk(KERN_NOTICE "___ MOV (7,SP),Rn ___\n");
	asm volatile(
		"add	-16,sp		\n"
		"mov	+0x11,%0	\n"
		"movbu	%0,(7,sp)	\n"
		"mov	+0x22,%0	\n"
		"movbu	%0,(8,sp)	\n"
		"mov	+0x33,%0	\n"
		"movbu	%0,(9,sp)	\n"
		"mov	+0x44,%0	\n"
		"movbu	%0,(10,sp)	\n"
		"mov	(7,sp),%1	\n"
		"add	+16,sp		\n"
		: "+a"(q), "=d"(x));
	ASSERTCMP(x, ==, 0x44332211);

	printk(KERN_NOTICE "___ MOV (259,SP),Rn ___\n");
	asm volatile(
		"add	-264,sp		\n"
		"mov	+0x11,%0	\n"
		"movbu	%0,(259,sp)	\n"
		"mov	+0x22,%0	\n"
		"movbu	%0,(260,sp)	\n"
		"mov	+0x33,%0	\n"
		"movbu	%0,(261,sp)	\n"
		"mov	+0x55,%0	\n"
		"movbu	%0,(262,sp)	\n"
		"mov	(259,sp),%1	\n"
		"add	+264,sp		\n"
		: "+d"(tmp), "=d"(x));
	ASSERTCMP(x, ==, 0x55332211);

	printk(KERN_NOTICE "___ MOV (260,SP),Rn ___\n");
	asm volatile(
		"add	-264,sp		\n"
		"mov	+0x11,%0	\n"
		"movbu	%0,(260,sp)	\n"
		"mov	+0x22,%0	\n"
		"movbu	%0,(261,sp)	\n"
		"mov	+0x33,%0	\n"
		"movbu	%0,(262,sp)	\n"
		"mov	+0x55,%0	\n"
		"movbu	%0,(263,sp)	\n"
		"mov	(260,sp),%1	\n"
		"add	+264,sp		\n"
		: "+d"(tmp), "=d"(x));
	ASSERTCMP(x, ==, 0x55332211);


	printk(KERN_NOTICE "___ MOV_LNE ___\n");
	tmp = 1;
	tmp2 = 2;
	q = p + 256;
	asm volatile(
		"setlb			\n"
		"mov	%2,%3		\n"
		"mov	%1,%2		\n"
		"cmp	+0,%1		\n"
		"mov_lne	(%0+,4),%1"
		: "+r"(q), "+d"(tmp), "+d"(tmp2), "=d"(x)
		:
		: "cc");
	ASSERTCMP(q, ==, p + 256 + 12);
	ASSERTCMP(x, ==, 0x44332211);

	printk(KERN_NOTICE "___ MOV in SETLB ___\n");
	tmp = 1;
	tmp2 = 2;
	q = p + 256;
	asm volatile(
		"setlb			\n"
		"mov	%1,%3		\n"
		"mov	(%0+),%1	\n"
		"cmp	+0,%1		\n"
		"lne			"
		: "+a"(q), "+d"(tmp), "+d"(tmp2), "=d"(x)
		:
		: "cc");

	ASSERTCMP(q, ==, p + 256 + 8);
	ASSERTCMP(x, ==, 0x44332211);

	printk(KERN_NOTICE "<==test_misalignment()\n");
	return 0;
}

arch_initcall(test_misalignment);

#endif /* CONFIG_TEST_MISALIGNMENT_HANDLER */
