/*
 * Copyright (C) 2004 Paul Mackerras <paulus@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct pt_regs;

/*
 * We don't allow single-stepping an mtmsrd that would clear
 * MSR_RI, since that would make the exception unrecoverable.
 * Since we need to single-step to proceed from a breakpoint,
 * we don't allow putting a breakpoint on an mtmsrd instruction.
 * Similarly we don't allow breakpoints on rfid instructions.
 * These macros tell us if an instruction is a mtmsrd or rfid.
 * Note that IS_MTMSRD returns true for both an mtmsr (32-bit)
 * and an mtmsrd (64-bit).
 */
#define IS_MTMSRD(instr)	(((instr) & 0xfc0007be) == 0x7c000124)
#define IS_RFID(instr)		(((instr) & 0xfc0007fe) == 0x4c000024)
#define IS_RFI(instr)		(((instr) & 0xfc0007fe) == 0x4c000064)

/* Emulate instructions that cause a transfer of control. */
extern int emulate_step(struct pt_regs *regs, unsigned int instr);

enum instruction_type {
	COMPUTE,		/* arith/logical/CR op, etc. */
	LOAD,
	LOAD_MULTI,
	LOAD_FP,
	LOAD_VMX,
	LOAD_VSX,
	STORE,
	STORE_MULTI,
	STORE_FP,
	STORE_VMX,
	STORE_VSX,
	LARX,
	STCX,
	BRANCH,
	MFSPR,
	MTSPR,
	CACHEOP,
	BARRIER,
	SYSCALL,
	MFMSR,
	MTMSR,
	RFI,
	INTERRUPT,
	UNKNOWN
};

#define INSTR_TYPE_MASK	0x1f

/* Load/store flags, ORed in with type */
#define SIGNEXT		0x20
#define UPDATE		0x40	/* matches bit in opcode 31 instructions */
#define BYTEREV		0x80

/* Cacheop values, ORed in with type */
#define CACHEOP_MASK	0x700
#define DCBST		0
#define DCBF		0x100
#define DCBTST		0x200
#define DCBT		0x300
#define ICBI		0x400

/* Size field in type word */
#define SIZE(n)		((n) << 8)
#define GETSIZE(w)	((w) >> 8)

#define MKOP(t, f, s)	((t) | (f) | SIZE(s))

struct instruction_op {
	int type;
	int reg;
	unsigned long val;
	/* For LOAD/STORE/LARX/STCX */
	unsigned long ea;
	int update_reg;
	/* For MFSPR */
	int spr;
};

extern int analyse_instr(struct instruction_op *op, struct pt_regs *regs,
			 unsigned int instr);
