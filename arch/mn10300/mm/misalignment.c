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
#include <linux/smp_lock.h>
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
#define kdebug(FMT, ...) printk(KERN_DEBUG FMT, ##__VA_ARGS__)
#else
#define kdebug(FMT, ...) do {} while (0)
#endif

static int misalignment_addr(unsigned long *registers, unsigned params,
			     unsigned opcode, unsigned disp,
			     void **_address, unsigned long **_postinc);

static int misalignment_reg(unsigned long *registers, unsigned params,
			    unsigned opcode, unsigned disp,
			    unsigned long **_register);

static inline unsigned int_log2(unsigned x)
{
	unsigned y;
	asm("bsch %1,%0" : "=r"(y) : "r"(x), "0"(0));
	return y;
}
#define log2(x) int_log2(x)

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
};

struct {
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
	IMM24,		/* 24-bit unsigned immediate */
	IMM32,		/* 32-bit unsigned immediate */
	IMM32_HIGH8,	/* 32-bit unsigned immediate, high 8-bits in opcode */

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
	const char	*name;
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
{ "mov",	0xf8f000,    0xfffc00,    0,    FMT_D1, AM33,	{MEM2(SD8, AM0), SP}},
{ "mov",	0xf8f400,    0xfffc00,    0,    FMT_D1, AM33,	{SP, MEM2(SD8, AN0)}},
{ "mov",	0xf90a00,    0xffff00,    0,    FMT_D6, AM33,	{MEM(RM0), RN2}},
{ "mov",	0xf91a00,    0xffff00,    0,    FMT_D6, AM33,	{RM2, MEM(RN0)}},
{ "mov",	0xf96a00,    0xffff00,    0x12, FMT_D6, AM33,	{MEMINC(RM0), RN2}},
{ "mov",	0xf97a00,    0xffff00,    0,	FMT_D6, AM33,	{RM2, MEMINC(RN0)}},
{ "mov",	0xfa000000,  0xfff00000,  0,    FMT_D2, 0,	{MEM2(SD16, AM0), DN1}},
{ "mov",	0xfa100000,  0xfff00000,  0,    FMT_D2, 0,	{DM1, MEM2(SD16, AN0)}},
{ "mov",	0xfa200000,  0xfff00000,  0,    FMT_D2, 0,	{MEM2(SD16, AM0), AN1}},
{ "mov",	0xfa300000,  0xfff00000,  0,    FMT_D2, 0,	{AM1, MEM2(SD16, AN0)}},
{ "mov",	0xfb0a0000,  0xffff0000,  0,    FMT_D7, AM33,	{MEM2(SD8, RM0), RN2}},
{ "mov",	0xfb1a0000,  0xffff0000,  0,    FMT_D7, AM33,	{RM2, MEM2(SD8, RN0)}},
{ "mov",	0xfb6a0000,  0xffff0000,  0x22, FMT_D7, AM33,	{MEMINC2 (RM0, SIMM8), RN2}},
{ "mov",	0xfb7a0000,  0xffff0000,  0,	FMT_D7, AM33,	{RM2, MEMINC2 (RN0, SIMM8)}},
{ "mov",	0xfb8e0000,  0xffff000f,  0,    FMT_D7, AM33,	{MEM2(RI, RM0), RD2}},
{ "mov",	0xfb9e0000,  0xffff000f,  0,    FMT_D7, AM33,	{RD2, MEM2(RI, RN0)}},
{ "mov",	0xfc000000,  0xfff00000,  0,    FMT_D4, 0,	{MEM2(IMM32,AM0), DN1}},
{ "mov",	0xfc100000,  0xfff00000,  0,    FMT_D4, 0,	{DM1, MEM2(IMM32,AN0)}},
{ "mov",	0xfc200000,  0xfff00000,  0,    FMT_D4, 0,	{MEM2(IMM32,AM0), AN1}},
{ "mov",	0xfc300000,  0xfff00000,  0,    FMT_D4, 0,	{AM1, MEM2(IMM32,AN0)}},
{ "mov",	0xfd0a0000,  0xffff0000,  0,    FMT_D8, AM33,	{MEM2(SD24, RM0), RN2}},
{ "mov",	0xfd1a0000,  0xffff0000,  0,    FMT_D8, AM33,	{RM2, MEM2(SD24, RN0)}},
{ "mov",	0xfd6a0000,  0xffff0000,  0x22, FMT_D8, AM33,	{MEMINC2 (RM0, IMM24), RN2}},
{ "mov",	0xfd7a0000,  0xffff0000,  0,	FMT_D8, AM33,	{RM2, MEMINC2 (RN0, IMM24)}},
{ "mov",	0xfe0a0000,  0xffff0000,  0,    FMT_D9, AM33,	{MEM2(IMM32_HIGH8,RM0), RN2}},
{ "mov",	0xfe1a0000,  0xffff0000,  0,    FMT_D9, AM33,	{RM2, MEM2(IMM32_HIGH8, RN0)}},
{ "mov",	0xfe6a0000,  0xffff0000,  0x22, FMT_D9, AM33,	{MEMINC2 (RM0, IMM32_HIGH8), RN2}},
{ "mov",	0xfe7a0000,  0xffff0000,  0,	FMT_D9, AM33,	{RN2, MEMINC2 (RM0, IMM32_HIGH8)}},

{ "movhu",	0xf060,	     0xfff0,	  0,    FMT_D0, 0,	{MEM(AM0), DN1}},
{ "movhu",	0xf070,	     0xfff0,	  0,    FMT_D0, 0,	{DM1, MEM(AN0)}},
{ "movhu",	0xf480,	     0xffc0,	  0,    FMT_D0, 0,	{MEM2(DI, AM0), DN2}},
{ "movhu",	0xf4c0,	     0xffc0,	  0,    FMT_D0, 0,	{DM2, MEM2(DI, AN0)}},
{ "movhu",	0xf86000,    0xfff000,    0,    FMT_D1, 0,	{MEM2(SD8, AM0), DN1}},
{ "movhu",	0xf87000,    0xfff000,    0,    FMT_D1, 0,	{DM1, MEM2(SD8, AN0)}},
{ "movhu",	0xf94a00,    0xffff00,    0,    FMT_D6, AM33,	{MEM(RM0), RN2}},
{ "movhu",	0xf95a00,    0xffff00,    0,    FMT_D6, AM33,	{RM2, MEM(RN0)}},
{ "movhu",	0xf9ea00,    0xffff00,    0x12, FMT_D6, AM33,	{MEMINC(RM0), RN2}},
{ "movhu",	0xf9fa00,    0xffff00,    0,	FMT_D6, AM33,	{RM2, MEMINC(RN0)}},
{ "movhu",	0xfa600000,  0xfff00000,  0,    FMT_D2, 0,	{MEM2(SD16, AM0), DN1}},
{ "movhu",	0xfa700000,  0xfff00000,  0,    FMT_D2, 0,	{DM1, MEM2(SD16, AN0)}},
{ "movhu",	0xfb4a0000,  0xffff0000,  0,    FMT_D7, AM33,	{MEM2(SD8, RM0), RN2}},
{ "movhu",	0xfb5a0000,  0xffff0000,  0,    FMT_D7, AM33,	{RM2, MEM2(SD8, RN0)}},
{ "movhu",	0xfbce0000,  0xffff000f,  0,    FMT_D7, AM33,	{MEM2(RI, RM0), RD2}},
{ "movhu",	0xfbde0000,  0xffff000f,  0,    FMT_D7, AM33,	{RD2, MEM2(RI, RN0)}},
{ "movhu",	0xfbea0000,  0xffff0000,  0x22, FMT_D7, AM33,	{MEMINC2 (RM0, SIMM8), RN2}},
{ "movhu",	0xfbfa0000,  0xffff0000,  0,	FMT_D7, AM33,	{RM2, MEMINC2 (RN0, SIMM8)}},
{ "movhu",	0xfc600000,  0xfff00000,  0,    FMT_D4, 0,	{MEM2(IMM32,AM0), DN1}},
{ "movhu",	0xfc700000,  0xfff00000,  0,    FMT_D4, 0,	{DM1, MEM2(IMM32,AN0)}},
{ "movhu",	0xfd4a0000,  0xffff0000,  0,    FMT_D8, AM33,	{MEM2(SD24, RM0), RN2}},
{ "movhu",	0xfd5a0000,  0xffff0000,  0,    FMT_D8, AM33,	{RM2, MEM2(SD24, RN0)}},
{ "movhu",	0xfdea0000,  0xffff0000,  0x22, FMT_D8, AM33,	{MEMINC2 (RM0, IMM24), RN2}},
{ "movhu",	0xfdfa0000,  0xffff0000,  0,	FMT_D8, AM33,	{RM2, MEMINC2 (RN0, IMM24)}},
{ "movhu",	0xfe4a0000,  0xffff0000,  0,    FMT_D9, AM33,	{MEM2(IMM32_HIGH8,RM0), RN2}},
{ "movhu",	0xfe5a0000,  0xffff0000,  0,    FMT_D9, AM33,	{RM2, MEM2(IMM32_HIGH8, RN0)}},
{ "movhu",	0xfeea0000,  0xffff0000,  0x22, FMT_D9, AM33,	{MEMINC2 (RM0, IMM32_HIGH8), RN2}},
{ "movhu",	0xfefa0000,  0xffff0000,  0,	FMT_D9, AM33,	{RN2, MEMINC2 (RM0, IMM32_HIGH8)}},
{ 0, 0, 0, 0, 0, 0, {0}},
};

/*
 * fix up misalignment problems where possible
 */
asmlinkage void misalignment(struct pt_regs *regs, enum exception_code code)
{
	const struct exception_table_entry *fixup;
	const struct mn10300_opcode *pop;
	unsigned long *registers = (unsigned long *) regs;
	unsigned long data, *store, *postinc;
	mm_segment_t seg;
	siginfo_t info;
	uint32_t opcode, disp, noc, xo, xm;
	uint8_t *pc, byte;
	void *address;
	unsigned tmp, npop;

	kdebug("MISALIGN at %lx\n", regs->pc);

	if (in_interrupt())
		die("Misalignment trap in interrupt context", regs, code);

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

	for (pop = mn10300_opcodes; pop->name; pop++) {
		npop = log2(pop->opcode | pop->opmask);
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
	if (!user_mode(regs))
		printk(KERN_CRIT "MISALIGN: %lx: unsupported instruction %x\n",
		       regs->pc, opcode);

failed:
	set_fs(seg);
	if (die_if_no_fixup("misalignment error", regs, code))
		return;

	info.si_signo	= SIGBUS;
	info.si_errno	= 0;
	info.si_code	= BUS_ADRALN;
	info.si_addr	= (void *) regs->pc;
	force_sig_info(SIGBUS, &info, current);
	return;

	/* error reading opcodes */
fetch_error:
	if (!user_mode(regs))
		printk(KERN_CRIT
		       "MISALIGN: %p: fault whilst reading instruction data\n",
		       pc);
	goto failed;

bad_addr_mode:
	if (!user_mode(regs))
		printk(KERN_CRIT
		       "MISALIGN: %lx: unsupported addressing mode %x\n",
		       regs->pc, opcode);
	goto failed;

bad_reg_mode:
	if (!user_mode(regs))
		printk(KERN_CRIT
		       "MISALIGN: %lx: unsupported register mode %x\n",
		       regs->pc, opcode);
	goto failed;

unsupported_instruction:
	if (!user_mode(regs))
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
	kdebug("MISALIGN: %lx: %x==%x { %x, %x }\n",
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
	tmp = format_tbl[pop->format].dispsz >> 3;
	while (tmp > 0) {
		tmp--;
		disp <<= 8;

		pc++;
		if (__get_user(byte, pc) != 0)
			goto fetch_error;
		disp |= byte;
	}

	set_fs(KERNEL_XDS);
	if (fixup || regs->epsw & EPSW_nSL)
		set_fs(seg);

	tmp = (pop->params[0] ^ pop->params[1]) & 0x80000000;
	if (!tmp) {
		if (!user_mode(regs))
			printk(KERN_CRIT
			       "MISALIGN: %lx:"
			       " insn not move to/from memory %x\n",
			       regs->pc, opcode);
		goto failed;
	}

	if (pop->params[0] & 0x80000000) {
		/* move memory to register */
		if (!misalignment_addr(registers, pop->params[0], opcode, disp,
				       &address, &postinc))
			goto bad_addr_mode;

		if (!misalignment_reg(registers, pop->params[1], opcode, disp,
				      &store))
			goto bad_reg_mode;

		if (strcmp(pop->name, "mov") == 0) {
			kdebug("FIXUP: mov (%p),DARn\n", address);
			if (copy_from_user(&data, (void *) address, 4) != 0)
				goto transfer_failed;
			if (pop->params[0] & 0x1000000)
				*postinc += 4;
		} else if (strcmp(pop->name, "movhu") == 0) {
			kdebug("FIXUP: movhu (%p),DARn\n", address);
			data = 0;
			if (copy_from_user(&data, (void *) address, 2) != 0)
				goto transfer_failed;
			if (pop->params[0] & 0x1000000)
				*postinc += 2;
		} else {
			goto unsupported_instruction;
		}

		*store = data;
	} else {
		/* move register to memory */
		if (!misalignment_reg(registers, pop->params[0], opcode, disp,
				      &store))
			goto bad_reg_mode;

		if (!misalignment_addr(registers, pop->params[1], opcode, disp,
				       &address, &postinc))
			goto bad_addr_mode;

		data = *store;

		if (strcmp(pop->name, "mov") == 0) {
			kdebug("FIXUP: mov %lx,(%p)\n", data, address);
			if (copy_to_user((void *) address, &data, 4) != 0)
				goto transfer_failed;
			if (pop->params[1] & 0x1000000)
				*postinc += 4;
		} else if (strcmp(pop->name, "movhu") == 0) {
			kdebug("FIXUP: movhu %hx,(%p)\n",
			       (uint16_t) data, address);
			if (copy_to_user((void *) address, &data, 2) != 0)
				goto transfer_failed;
			if (pop->params[1] & 0x1000000)
				*postinc += 2;
		} else {
			goto unsupported_instruction;
		}
	}

	tmp = format_tbl[pop->format].opsz + format_tbl[pop->format].dispsz;
	regs->pc += tmp >> 3;

	set_fs(seg);
	return;
}

/*
 * determine the address that was being accessed
 */
static int misalignment_addr(unsigned long *registers, unsigned params,
			     unsigned opcode, unsigned disp,
			     void **_address, unsigned long **_postinc)
{
	unsigned long *postinc = NULL, address = 0, tmp;

	params &= 0x7fffffff;

	do {
		switch (params & 0xff) {
		case DM0:
			postinc = &registers[Dreg_index[opcode & 0x03]];
			address += *postinc;
			break;
		case DM1:
			postinc = &registers[Dreg_index[opcode >> 2 & 0x0c]];
			address += *postinc;
			break;
		case DM2:
			postinc = &registers[Dreg_index[opcode >> 4 & 0x30]];
			address += *postinc;
			break;
		case AM0:
			postinc = &registers[Areg_index[opcode & 0x03]];
			address += *postinc;
			break;
		case AM1:
			postinc = &registers[Areg_index[opcode >> 2 & 0x0c]];
			address += *postinc;
			break;
		case AM2:
			postinc = &registers[Areg_index[opcode >> 4 & 0x30]];
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

		case SD8:
		case SIMM8:
			address += (int32_t) (int8_t) (disp & 0xff);
			break;
		case SD16:
			address += (int32_t) (int16_t) (disp & 0xffff);
			break;
		case SD24:
			tmp = disp << 8;
			asm("asr 8,%0" : "=r"(tmp) : "0"(tmp));
			address += tmp;
			break;
		case SIMM4_2:
			tmp = opcode >> 4 & 0x0f;
			tmp <<= 28;
			asm("asr 28,%0" : "=r"(tmp) : "0"(tmp));
			address += tmp;
			break;
		case IMM24:
			address += disp & 0x00ffffff;
			break;
		case IMM32:
		case IMM32_HIGH8:
			address += disp;
			break;
		default:
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
			    unsigned opcode, unsigned disp,
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
		return 0;
	}

	return 1;
}
