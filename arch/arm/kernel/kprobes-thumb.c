/*
 * arch/arm/kernel/kprobes-thumb.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>

#include "kprobes.h"


/*
 * True if current instruction is in an IT block.
 */
#define in_it_block(cpsr)	((cpsr & 0x06000c00) != 0x00000000)

/*
 * Return the condition code to check for the currently executing instruction.
 * This is in ITSTATE<7:4> which is in CPSR<15:12> but is only valid if
 * in_it_block returns true.
 */
#define current_cond(cpsr)	((cpsr >> 12) & 0xf)

/*
 * Return the PC value for a probe in thumb code.
 * This is the address of the probed instruction plus 4.
 * We subtract one because the address will have bit zero set to indicate
 * a pointer to thumb code.
 */
static inline unsigned long __kprobes thumb_probe_pc(struct kprobe *p)
{
	return (unsigned long)p->addr - 1 + 4;
}

static void __kprobes
t16_simulate_bxblx(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = thumb_probe_pc(p);
	int rm = (insn >> 3) & 0xf;
	unsigned long rmv = (rm == 15) ? pc : regs->uregs[rm];

	if (insn & (1 << 7)) /* BLX ? */
		regs->ARM_lr = (unsigned long)p->addr + 2;

	bx_write_pc(rmv, regs);
}

static void __kprobes
t16_simulate_ldr_literal(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long* base = (unsigned long *)(thumb_probe_pc(p) & ~3);
	long index = insn & 0xff;
	int rt = (insn >> 8) & 0x7;
	regs->uregs[rt] = base[index];
}

static void __kprobes
t16_simulate_ldrstr_sp_relative(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long* base = (unsigned long *)regs->ARM_sp;
	long index = insn & 0xff;
	int rt = (insn >> 8) & 0x7;
	if (insn & 0x800) /* LDR */
		regs->uregs[rt] = base[index];
	else /* STR */
		base[index] = regs->uregs[rt];
}

static void __kprobes
t16_simulate_reladr(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long base = (insn & 0x800) ? regs->ARM_sp
					    : (thumb_probe_pc(p) & ~3);
	long offset = insn & 0xff;
	int rt = (insn >> 8) & 0x7;
	regs->uregs[rt] = base + offset * 4;
}

static void __kprobes
t16_simulate_add_sp_imm(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	long imm = insn & 0x7f;
	if (insn & 0x80) /* SUB */
		regs->ARM_sp -= imm * 4;
	else /* ADD */
		regs->ARM_sp += imm * 4;
}

static void __kprobes
t16_simulate_cbz(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rn = insn & 0x7;
	kprobe_opcode_t nonzero = regs->uregs[rn] ? insn : ~insn;
	if (nonzero & 0x800) {
		long i = insn & 0x200;
		long imm5 = insn & 0xf8;
		unsigned long pc = thumb_probe_pc(p);
		regs->ARM_pc = pc + (i >> 3) + (imm5 >> 2);
	}
}

static unsigned long __kprobes
t16_emulate_loregs(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long oldcpsr = regs->ARM_cpsr;
	unsigned long newcpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[oldcpsr]	\n\t"
		"ldmia	%[regs], {r0-r7}	\n\t"
		"blx	%[fn]			\n\t"
		"stmia	%[regs], {r0-r7}	\n\t"
		"mrs	%[newcpsr], cpsr	\n\t"
		: [newcpsr] "=r" (newcpsr)
		: [oldcpsr] "r" (oldcpsr), [regs] "r" (regs),
		  [fn] "r" (p->ainsn.insn_fn)
		: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
		  "lr", "memory", "cc"
		);

	return (oldcpsr & ~APSR_MASK) | (newcpsr & APSR_MASK);
}

static void __kprobes
t16_emulate_loregs_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	regs->ARM_cpsr = t16_emulate_loregs(p, regs);
}

static void __kprobes
t16_emulate_loregs_noitrwflags(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long cpsr = t16_emulate_loregs(p, regs);
	if (!in_it_block(cpsr))
		regs->ARM_cpsr = cpsr;
}

static void __kprobes
t16_emulate_hiregs(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = thumb_probe_pc(p);
	int rdn = (insn & 0x7) | ((insn & 0x80) >> 4);
	int rm = (insn >> 3) & 0xf;

	register unsigned long rdnv asm("r1");
	register unsigned long rmv asm("r0");
	unsigned long cpsr = regs->ARM_cpsr;

	rdnv = (rdn == 15) ? pc : regs->uregs[rdn];
	rmv = (rm == 15) ? pc : regs->uregs[rm];

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		"blx    %[fn]			\n\t"
		"mrs	%[cpsr], cpsr		\n\t"
		: "=r" (rdnv), [cpsr] "=r" (cpsr)
		: "0" (rdnv), "r" (rmv), "1" (cpsr), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	if (rdn == 15)
		rdnv &= ~1;

	regs->uregs[rdn] = rdnv;
	regs->ARM_cpsr = (regs->ARM_cpsr & ~APSR_MASK) | (cpsr & APSR_MASK);
}

static enum kprobe_insn __kprobes
t16_decode_hiregs(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	insn &= ~0x00ff;
	insn |= 0x001; /* Set Rdn = R1 and Rm = R0 */
	((u16 *)asi->insn)[0] = insn;
	asi->insn_handler = t16_emulate_hiregs;
	return INSN_GOOD;
}

static void __kprobes
t16_emulate_push(struct kprobe *p, struct pt_regs *regs)
{
	__asm__ __volatile__ (
		"ldr	r9, [%[regs], #13*4]	\n\t"
		"ldr	r8, [%[regs], #14*4]	\n\t"
		"ldmia	%[regs], {r0-r7}	\n\t"
		"blx	%[fn]			\n\t"
		"str	r9, [%[regs], #13*4]	\n\t"
		:
		: [regs] "r" (regs), [fn] "r" (p->ainsn.insn_fn)
		: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9",
		  "lr", "memory", "cc"
		);
}

static enum kprobe_insn __kprobes
t16_decode_push(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/*
	 * To simulate a PUSH we use a Thumb-2 "STMDB R9!, {registers}"
	 * and call it with R9=SP and LR in the register list represented
	 * by R8.
	 */
	((u16 *)asi->insn)[0] = 0xe929;		/* 1st half STMDB R9!,{} */
	((u16 *)asi->insn)[1] = insn & 0x1ff;	/* 2nd half (register list) */
	asi->insn_handler = t16_emulate_push;
	return INSN_GOOD;
}

static void __kprobes
t16_emulate_pop_nopc(struct kprobe *p, struct pt_regs *regs)
{
	__asm__ __volatile__ (
		"ldr	r9, [%[regs], #13*4]	\n\t"
		"ldmia	%[regs], {r0-r7}	\n\t"
		"blx	%[fn]			\n\t"
		"stmia	%[regs], {r0-r7}	\n\t"
		"str	r9, [%[regs], #13*4]	\n\t"
		:
		: [regs] "r" (regs), [fn] "r" (p->ainsn.insn_fn)
		: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r9",
		  "lr", "memory", "cc"
		);
}

static void __kprobes
t16_emulate_pop_pc(struct kprobe *p, struct pt_regs *regs)
{
	register unsigned long pc asm("r8");

	__asm__ __volatile__ (
		"ldr	r9, [%[regs], #13*4]	\n\t"
		"ldmia	%[regs], {r0-r7}	\n\t"
		"blx	%[fn]			\n\t"
		"stmia	%[regs], {r0-r7}	\n\t"
		"str	r9, [%[regs], #13*4]	\n\t"
		: "=r" (pc)
		: [regs] "r" (regs), [fn] "r" (p->ainsn.insn_fn)
		: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r9",
		  "lr", "memory", "cc"
		);

	bx_write_pc(pc, regs);
}

static enum kprobe_insn __kprobes
t16_decode_pop(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/*
	 * To simulate a POP we use a Thumb-2 "LDMDB R9!, {registers}"
	 * and call it with R9=SP and PC in the register list represented
	 * by R8.
	 */
	((u16 *)asi->insn)[0] = 0xe8b9;		/* 1st half LDMIA R9!,{} */
	((u16 *)asi->insn)[1] = insn & 0x1ff;	/* 2nd half (register list) */
	asi->insn_handler = insn & 0x100 ? t16_emulate_pop_pc
					 : t16_emulate_pop_nopc;
	return INSN_GOOD;
}

static const union decode_item t16_table_1011[] = {
	/* Miscellaneous 16-bit instructions		    */

	/* ADD (SP plus immediate)	1011 0000 0xxx xxxx */
	/* SUB (SP minus immediate)	1011 0000 1xxx xxxx */
	DECODE_SIMULATE	(0xff00, 0xb000, t16_simulate_add_sp_imm),

	/* CBZ				1011 00x1 xxxx xxxx */
	/* CBNZ				1011 10x1 xxxx xxxx */
	DECODE_SIMULATE	(0xf500, 0xb100, t16_simulate_cbz),

	/* SXTH				1011 0010 00xx xxxx */
	/* SXTB				1011 0010 01xx xxxx */
	/* UXTH				1011 0010 10xx xxxx */
	/* UXTB				1011 0010 11xx xxxx */
	/* REV				1011 1010 00xx xxxx */
	/* REV16			1011 1010 01xx xxxx */
	/* ???				1011 1010 10xx xxxx */
	/* REVSH			1011 1010 11xx xxxx */
	DECODE_REJECT	(0xffc0, 0xba80),
	DECODE_EMULATE	(0xf500, 0xb000, t16_emulate_loregs_rwflags),

	/* PUSH				1011 010x xxxx xxxx */
	DECODE_CUSTOM	(0xfe00, 0xb400, t16_decode_push),
	/* POP				1011 110x xxxx xxxx */
	DECODE_CUSTOM	(0xfe00, 0xbc00, t16_decode_pop),

	/*
	 * If-Then, and hints
	 *				1011 1111 xxxx xxxx
	 */

	/* YIELD			1011 1111 0001 0000 */
	DECODE_OR	(0xffff, 0xbf10),
	/* SEV				1011 1111 0100 0000 */
	DECODE_EMULATE	(0xffff, 0xbf40, kprobe_emulate_none),
	/* NOP				1011 1111 0000 0000 */
	/* WFE				1011 1111 0010 0000 */
	/* WFI				1011 1111 0011 0000 */
	DECODE_SIMULATE	(0xffcf, 0xbf00, kprobe_simulate_nop),
	/* Unassigned hints		1011 1111 xxxx 0000 */
	DECODE_REJECT	(0xff0f, 0xbf00),

	DECODE_END
};

const union decode_item kprobe_decode_thumb16_table[] = {

	/*
	 * Shift (immediate), add, subtract, move, and compare
	 *				00xx xxxx xxxx xxxx
	 */

	/* CMP (immediate)		0010 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xf800, 0x2800, t16_emulate_loregs_rwflags),

	/* ADD (register)		0001 100x xxxx xxxx */
	/* SUB (register)		0001 101x xxxx xxxx */
	/* LSL (immediate)		0000 0xxx xxxx xxxx */
	/* LSR (immediate)		0000 1xxx xxxx xxxx */
	/* ASR (immediate)		0001 0xxx xxxx xxxx */
	/* ADD (immediate, Thumb)	0001 110x xxxx xxxx */
	/* SUB (immediate, Thumb)	0001 111x xxxx xxxx */
	/* MOV (immediate)		0010 0xxx xxxx xxxx */
	/* ADD (immediate, Thumb)	0011 0xxx xxxx xxxx */
	/* SUB (immediate, Thumb)	0011 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xc000, 0x0000, t16_emulate_loregs_noitrwflags),

	/*
	 * 16-bit Thumb data-processing instructions
	 *				0100 00xx xxxx xxxx
	 */

	/* TST (register)		0100 0010 00xx xxxx */
	DECODE_EMULATE	(0xffc0, 0x4200, t16_emulate_loregs_rwflags),
	/* CMP (register)		0100 0010 10xx xxxx */
	/* CMN (register)		0100 0010 11xx xxxx */
	DECODE_EMULATE	(0xff80, 0x4280, t16_emulate_loregs_rwflags),
	/* AND (register)		0100 0000 00xx xxxx */
	/* EOR (register)		0100 0000 01xx xxxx */
	/* LSL (register)		0100 0000 10xx xxxx */
	/* LSR (register)		0100 0000 11xx xxxx */
	/* ASR (register)		0100 0001 00xx xxxx */
	/* ADC (register)		0100 0001 01xx xxxx */
	/* SBC (register)		0100 0001 10xx xxxx */
	/* ROR (register)		0100 0001 11xx xxxx */
	/* RSB (immediate)		0100 0010 01xx xxxx */
	/* ORR (register)		0100 0011 00xx xxxx */
	/* MUL				0100 0011 00xx xxxx */
	/* BIC (register)		0100 0011 10xx xxxx */
	/* MVN (register)		0100 0011 10xx xxxx */
	DECODE_EMULATE	(0xfc00, 0x4000, t16_emulate_loregs_noitrwflags),

	/*
	 * Special data instructions and branch and exchange
	 *				0100 01xx xxxx xxxx
	 */

	/* BLX pc			0100 0111 1111 1xxx */
	DECODE_REJECT	(0xfff8, 0x47f8),

	/* BX (register)		0100 0111 0xxx xxxx */
	/* BLX (register)		0100 0111 1xxx xxxx */
	DECODE_SIMULATE (0xff00, 0x4700, t16_simulate_bxblx),

	/* ADD pc, pc			0100 0100 1111 1111 */
	DECODE_REJECT	(0xffff, 0x44ff),

	/* ADD (register)		0100 0100 xxxx xxxx */
	/* CMP (register)		0100 0101 xxxx xxxx */
	/* MOV (register)		0100 0110 xxxx xxxx */
	DECODE_CUSTOM	(0xfc00, 0x4400, t16_decode_hiregs),

	/*
	 * Load from Literal Pool
	 * LDR (literal)		0100 1xxx xxxx xxxx
	 */
	DECODE_SIMULATE	(0xf800, 0x4800, t16_simulate_ldr_literal),

	/*
	 * 16-bit Thumb Load/store instructions
	 *				0101 xxxx xxxx xxxx
	 *				011x xxxx xxxx xxxx
	 *				100x xxxx xxxx xxxx
	 */

	/* STR (register)		0101 000x xxxx xxxx */
	/* STRH (register)		0101 001x xxxx xxxx */
	/* STRB (register)		0101 010x xxxx xxxx */
	/* LDRSB (register)		0101 011x xxxx xxxx */
	/* LDR (register)		0101 100x xxxx xxxx */
	/* LDRH (register)		0101 101x xxxx xxxx */
	/* LDRB (register)		0101 110x xxxx xxxx */
	/* LDRSH (register)		0101 111x xxxx xxxx */
	/* STR (immediate, Thumb)	0110 0xxx xxxx xxxx */
	/* LDR (immediate, Thumb)	0110 1xxx xxxx xxxx */
	/* STRB (immediate, Thumb)	0111 0xxx xxxx xxxx */
	/* LDRB (immediate, Thumb)	0111 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xc000, 0x4000, t16_emulate_loregs_rwflags),
	/* STRH (immediate, Thumb)	1000 0xxx xxxx xxxx */
	/* LDRH (immediate, Thumb)	1000 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xf000, 0x8000, t16_emulate_loregs_rwflags),
	/* STR (immediate, Thumb)	1001 0xxx xxxx xxxx */
	/* LDR (immediate, Thumb)	1001 1xxx xxxx xxxx */
	DECODE_SIMULATE	(0xf000, 0x9000, t16_simulate_ldrstr_sp_relative),

	/*
	 * Generate PC-/SP-relative address
	 * ADR (literal)		1010 0xxx xxxx xxxx
	 * ADD (SP plus immediate)	1010 1xxx xxxx xxxx
	 */
	DECODE_SIMULATE	(0xf000, 0xa000, t16_simulate_reladr),

	/*
	 * Miscellaneous 16-bit instructions
	 *				1011 xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xf000, 0xb000, t16_table_1011),

	/* STM				1100 0xxx xxxx xxxx */
	/* LDM				1100 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xf000, 0xc000, t16_emulate_loregs_rwflags),

	DECODE_END
};

static unsigned long __kprobes thumb_check_cc(unsigned long cpsr)
{
	if (unlikely(in_it_block(cpsr)))
		return kprobe_condition_checks[current_cond(cpsr)](cpsr);
	return true;
}

static void __kprobes thumb16_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->ARM_pc += 2;
	p->ainsn.insn_handler(p, regs);
	regs->ARM_cpsr = it_advance(regs->ARM_cpsr);
}

static void __kprobes thumb32_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->ARM_pc += 4;
	p->ainsn.insn_handler(p, regs);
	regs->ARM_cpsr = it_advance(regs->ARM_cpsr);
}

enum kprobe_insn __kprobes
thumb16_kprobe_decode_insn(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	asi->insn_singlestep = thumb16_singlestep;
	asi->insn_check_cc = thumb_check_cc;
	return kprobe_decode_insn(insn, asi, kprobe_decode_thumb16_table, true);
}

enum kprobe_insn __kprobes
thumb32_kprobe_decode_insn(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	asi->insn_singlestep = thumb32_singlestep;
	asi->insn_check_cc = thumb_check_cc;
	return INSN_REJECTED;
}
