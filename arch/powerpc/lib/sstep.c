/*
 * Single-step support.
 *
 * Copyright (C) 2004 Paul Mackerras <paulus@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/config.h>
#include <asm/sstep.h>
#include <asm/processor.h>

extern char system_call_common[];

#ifdef CONFIG_PPC64
/* Bits in SRR1 that are copied from MSR */
#define MSR_MASK	0xffffffff87c0ffffUL
#else
#define MSR_MASK	0x87c0ffff
#endif

/*
 * Determine whether a conditional branch instruction would branch.
 */
static int branch_taken(unsigned int instr, struct pt_regs *regs)
{
	unsigned int bo = (instr >> 21) & 0x1f;
	unsigned int bi;

	if ((bo & 4) == 0) {
		/* decrement counter */
		--regs->ctr;
		if (((bo >> 1) & 1) ^ (regs->ctr == 0))
			return 0;
	}
	if ((bo & 0x10) == 0) {
		/* check bit from CR */
		bi = (instr >> 16) & 0x1f;
		if (((regs->ccr >> (31 - bi)) & 1) != ((bo >> 3) & 1))
			return 0;
	}
	return 1;
}

/*
 * Emulate instructions that cause a transfer of control.
 * Returns 1 if the step was emulated, 0 if not,
 * or -1 if the instruction is one that should not be stepped,
 * such as an rfid, or a mtmsrd that would clear MSR_RI.
 */
int emulate_step(struct pt_regs *regs, unsigned int instr)
{
	unsigned int opcode, rd;
	unsigned long int imm;

	opcode = instr >> 26;
	switch (opcode) {
	case 16:	/* bc */
		imm = (signed short)(instr & 0xfffc);
		if ((instr & 2) == 0)
			imm += regs->nip;
		regs->nip += 4;
		if ((regs->msr & MSR_SF) == 0)
			regs->nip &= 0xffffffffUL;
		if (instr & 1)
			regs->link = regs->nip;
		if (branch_taken(instr, regs))
			regs->nip = imm;
		return 1;
#ifdef CONFIG_PPC64
	case 17:	/* sc */
		/*
		 * N.B. this uses knowledge about how the syscall
		 * entry code works.  If that is changed, this will
		 * need to be changed also.
		 */
		regs->gpr[9] = regs->gpr[13];
		regs->gpr[11] = regs->nip + 4;
		regs->gpr[12] = regs->msr & MSR_MASK;
		regs->gpr[13] = (unsigned long) get_paca();
		regs->nip = (unsigned long) &system_call_common;
		regs->msr = MSR_KERNEL;
		return 1;
#endif
	case 18:	/* b */
		imm = instr & 0x03fffffc;
		if (imm & 0x02000000)
			imm -= 0x04000000;
		if ((instr & 2) == 0)
			imm += regs->nip;
		if (instr & 1) {
			regs->link = regs->nip + 4;
			if ((regs->msr & MSR_SF) == 0)
				regs->link &= 0xffffffffUL;
		}
		if ((regs->msr & MSR_SF) == 0)
			imm &= 0xffffffffUL;
		regs->nip = imm;
		return 1;
	case 19:
		switch (instr & 0x7fe) {
		case 0x20:	/* bclr */
		case 0x420:	/* bcctr */
			imm = (instr & 0x400)? regs->ctr: regs->link;
			regs->nip += 4;
			if ((regs->msr & MSR_SF) == 0) {
				regs->nip &= 0xffffffffUL;
				imm &= 0xffffffffUL;
			}
			if (instr & 1)
				regs->link = regs->nip;
			if (branch_taken(instr, regs))
				regs->nip = imm;
			return 1;
		case 0x24:	/* rfid, scary */
			return -1;
		}
	case 31:
		rd = (instr >> 21) & 0x1f;
		switch (instr & 0x7fe) {
		case 0xa6:	/* mfmsr */
			regs->gpr[rd] = regs->msr & MSR_MASK;
			regs->nip += 4;
			if ((regs->msr & MSR_SF) == 0)
				regs->nip &= 0xffffffffUL;
			return 1;
		case 0x124:	/* mtmsr */
			imm = regs->gpr[rd];
			if ((imm & MSR_RI) == 0)
				/* can't step mtmsr that would clear MSR_RI */
				return -1;
			regs->msr = imm;
			regs->nip += 4;
			return 1;
#ifdef CONFIG_PPC64
		case 0x164:	/* mtmsrd */
			/* only MSR_EE and MSR_RI get changed if bit 15 set */
			/* mtmsrd doesn't change MSR_HV and MSR_ME */
			imm = (instr & 0x10000)? 0x8002: 0xefffffffffffefffUL;
			imm = (regs->msr & MSR_MASK & ~imm)
				| (regs->gpr[rd] & imm);
			if ((imm & MSR_RI) == 0)
				/* can't step mtmsrd that would clear MSR_RI */
				return -1;
			regs->msr = imm;
			regs->nip += 4;
			if ((imm & MSR_SF) == 0)
				regs->nip &= 0xffffffffUL;
			return 1;
#endif
		}
	}
	return 0;
}
