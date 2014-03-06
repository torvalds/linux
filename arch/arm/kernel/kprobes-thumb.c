/*
 * arch/arm/kernel/kprobes-thumb.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/kprobes.h>

#include "kprobes.h"
#include "probes-thumb.h"

/* These emulation encodings are functionally equivalent... */
#define t32_emulate_rd8rn16rm0ra12_noflags \
		t32_emulate_rdlo12rdhi8rn16rm0_noflags

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

/* t32 thumb actions */

static void __kprobes
t32_simulate_table_branch(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = thumb_probe_pc(p);
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	unsigned long rnv = (rn == 15) ? pc : regs->uregs[rn];
	unsigned long rmv = regs->uregs[rm];
	unsigned int halfwords;

	if (insn & 0x10) /* TBH */
		halfwords = ((u16 *)rnv)[rmv];
	else /* TBB */
		halfwords = ((u8 *)rnv)[rmv];

	regs->ARM_pc = pc + 2 * halfwords;
}

static void __kprobes
t32_simulate_mrs(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 8) & 0xf;
	unsigned long mask = 0xf8ff03df; /* Mask out execution state */
	regs->uregs[rd] = regs->ARM_cpsr & mask;
}

static void __kprobes
t32_simulate_cond_branch(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = thumb_probe_pc(p);

	long offset = insn & 0x7ff;		/* imm11 */
	offset += (insn & 0x003f0000) >> 5;	/* imm6 */
	offset += (insn & 0x00002000) << 4;	/* J1 */
	offset += (insn & 0x00000800) << 7;	/* J2 */
	offset -= (insn & 0x04000000) >> 7;	/* Apply sign bit */

	regs->ARM_pc = pc + (offset * 2);
}

static enum kprobe_insn __kprobes
t32_decode_cond_branch(kprobe_opcode_t insn, struct arch_specific_insn *asi,
		const struct decode_header *d)
{
	int cc = (insn >> 22) & 0xf;
	asi->insn_check_cc = kprobe_condition_checks[cc];
	asi->insn_handler = t32_simulate_cond_branch;
	return INSN_GOOD_NO_SLOT;
}

static void __kprobes
t32_simulate_branch(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = thumb_probe_pc(p);

	long offset = insn & 0x7ff;		/* imm11 */
	offset += (insn & 0x03ff0000) >> 5;	/* imm10 */
	offset += (insn & 0x00002000) << 9;	/* J1 */
	offset += (insn & 0x00000800) << 10;	/* J2 */
	if (insn & 0x04000000)
		offset -= 0x00800000; /* Apply sign bit */
	else
		offset ^= 0x00600000; /* Invert J1 and J2 */

	if (insn & (1 << 14)) {
		/* BL or BLX */
		regs->ARM_lr = (unsigned long)p->addr + 4;
		if (!(insn & (1 << 12))) {
			/* BLX so switch to ARM mode */
			regs->ARM_cpsr &= ~PSR_T_BIT;
			pc &= ~3;
		}
	}

	regs->ARM_pc = pc + (offset * 2);
}

static void __kprobes
t32_simulate_ldr_literal(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long addr = thumb_probe_pc(p) & ~3;
	int rt = (insn >> 12) & 0xf;
	unsigned long rtv;

	long offset = insn & 0xfff;
	if (insn & 0x00800000)
		addr += offset;
	else
		addr -= offset;

	if (insn & 0x00400000) {
		/* LDR */
		rtv = *(unsigned long *)addr;
		if (rt == 15) {
			bx_write_pc(rtv, regs);
			return;
		}
	} else if (insn & 0x00200000) {
		/* LDRH */
		if (insn & 0x01000000)
			rtv = *(s16 *)addr;
		else
			rtv = *(u16 *)addr;
	} else {
		/* LDRB */
		if (insn & 0x01000000)
			rtv = *(s8 *)addr;
		else
			rtv = *(u8 *)addr;
	}

	regs->uregs[rt] = rtv;
}

static enum kprobe_insn __kprobes
t32_decode_ldmstm(kprobe_opcode_t insn, struct arch_specific_insn *asi,
		const struct decode_header *d)
{
	enum kprobe_insn ret = kprobe_decode_ldmstm(insn, asi, d);

	/* Fixup modified instruction to have halfwords in correct order...*/
	insn = asi->insn[0];
	((u16 *)asi->insn)[0] = insn >> 16;
	((u16 *)asi->insn)[1] = insn & 0xffff;

	return ret;
}

static void __kprobes
t32_emulate_ldrdstrd(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = thumb_probe_pc(p) & ~3;
	int rt1 = (insn >> 12) & 0xf;
	int rt2 = (insn >> 8) & 0xf;
	int rn = (insn >> 16) & 0xf;

	register unsigned long rt1v asm("r0") = regs->uregs[rt1];
	register unsigned long rt2v asm("r1") = regs->uregs[rt2];
	register unsigned long rnv asm("r2") = (rn == 15) ? pc
							  : regs->uregs[rn];

	__asm__ __volatile__ (
		"blx    %[fn]"
		: "=r" (rt1v), "=r" (rt2v), "=r" (rnv)
		: "0" (rt1v), "1" (rt2v), "2" (rnv), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	if (rn != 15)
		regs->uregs[rn] = rnv; /* Writeback base register */
	regs->uregs[rt1] = rt1v;
	regs->uregs[rt2] = rt2v;
}

static void __kprobes
t32_emulate_ldrstr(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rt = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rtv asm("r0") = regs->uregs[rt];
	register unsigned long rnv asm("r2") = regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		"blx    %[fn]"
		: "=r" (rtv), "=r" (rnv)
		: "0" (rtv), "1" (rnv), "r" (rmv), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rn] = rnv; /* Writeback base register */
	if (rt == 15) /* Can't be true for a STR as they aren't allowed */
		bx_write_pc(rtv, regs);
	else
		regs->uregs[rt] = rtv;
}

static void __kprobes
t32_emulate_rd8rn16rm0_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 8) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rdv asm("r1") = regs->uregs[rd];
	register unsigned long rnv asm("r2") = regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];
	unsigned long cpsr = regs->ARM_cpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		"blx    %[fn]			\n\t"
		"mrs	%[cpsr], cpsr		\n\t"
		: "=r" (rdv), [cpsr] "=r" (cpsr)
		: "0" (rdv), "r" (rnv), "r" (rmv),
		  "1" (cpsr), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rd] = rdv;
	regs->ARM_cpsr = (regs->ARM_cpsr & ~APSR_MASK) | (cpsr & APSR_MASK);
}

static void __kprobes
t32_emulate_rd8pc16_noflags(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = thumb_probe_pc(p);
	int rd = (insn >> 8) & 0xf;

	register unsigned long rdv asm("r1") = regs->uregs[rd];
	register unsigned long rnv asm("r2") = pc & ~3;

	__asm__ __volatile__ (
		"blx    %[fn]"
		: "=r" (rdv)
		: "0" (rdv), "r" (rnv), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rd] = rdv;
}

static void __kprobes
t32_emulate_rd8rn16_noflags(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 8) & 0xf;
	int rn = (insn >> 16) & 0xf;

	register unsigned long rdv asm("r1") = regs->uregs[rd];
	register unsigned long rnv asm("r2") = regs->uregs[rn];

	__asm__ __volatile__ (
		"blx    %[fn]"
		: "=r" (rdv)
		: "0" (rdv), "r" (rnv), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rd] = rdv;
}

static void __kprobes
t32_emulate_rdlo12rdhi8rn16rm0_noflags(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rdlo = (insn >> 12) & 0xf;
	int rdhi = (insn >> 8) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rdlov asm("r0") = regs->uregs[rdlo];
	register unsigned long rdhiv asm("r1") = regs->uregs[rdhi];
	register unsigned long rnv asm("r2") = regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		"blx    %[fn]"
		: "=r" (rdlov), "=r" (rdhiv)
		: "0" (rdlov), "1" (rdhiv), "r" (rnv), "r" (rmv),
		  [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rdlo] = rdlov;
	regs->uregs[rdhi] = rdhiv;
}
/* t16 thumb actions */

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

static void __kprobes
t16_simulate_it(struct kprobe *p, struct pt_regs *regs)
{
	/*
	 * The 8 IT state bits are split into two parts in CPSR:
	 *	ITSTATE<1:0> are in CPSR<26:25>
	 *	ITSTATE<7:2> are in CPSR<15:10>
	 * The new IT state is in the lower byte of insn.
	 */
	kprobe_opcode_t insn = p->opcode;
	unsigned long cpsr = regs->ARM_cpsr;
	cpsr &= ~PSR_IT_MASK;
	cpsr |= (insn & 0xfc) << 8;
	cpsr |= (insn & 0x03) << 25;
	regs->ARM_cpsr = cpsr;
}

static void __kprobes
t16_singlestep_it(struct kprobe *p, struct pt_regs *regs)
{
	regs->ARM_pc += 2;
	t16_simulate_it(p, regs);
}

static enum kprobe_insn __kprobes
t16_decode_it(kprobe_opcode_t insn, struct arch_specific_insn *asi,
		const struct decode_header *d)
{
	asi->insn_singlestep = t16_singlestep_it;
	return INSN_GOOD_NO_SLOT;
}

static void __kprobes
t16_simulate_cond_branch(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = thumb_probe_pc(p);
	long offset = insn & 0x7f;
	offset -= insn & 0x80; /* Apply sign bit */
	regs->ARM_pc = pc + (offset * 2);
}

static enum kprobe_insn __kprobes
t16_decode_cond_branch(kprobe_opcode_t insn, struct arch_specific_insn *asi,
		const struct decode_header *d)
{
	int cc = (insn >> 8) & 0xf;
	asi->insn_check_cc = kprobe_condition_checks[cc];
	asi->insn_handler = t16_simulate_cond_branch;
	return INSN_GOOD_NO_SLOT;
}

static void __kprobes
t16_simulate_branch(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = thumb_probe_pc(p);
	long offset = insn & 0x3ff;
	offset -= insn & 0x400; /* Apply sign bit */
	regs->ARM_pc = pc + (offset * 2);
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
t16_decode_hiregs(kprobe_opcode_t insn, struct arch_specific_insn *asi,
		const struct decode_header *d)
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
t16_decode_push(kprobe_opcode_t insn, struct arch_specific_insn *asi,
		const struct decode_header *d)
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
t16_decode_pop(kprobe_opcode_t insn, struct arch_specific_insn *asi,
		const struct decode_header *d)
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

const union decode_action kprobes_t16_actions[NUM_PROBES_T16_ACTIONS] = {
	[PROBES_T16_ADD_SP] = {.handler = t16_simulate_add_sp_imm},
	[PROBES_T16_CBZ] = {.handler = t16_simulate_cbz},
	[PROBES_T16_SIGN_EXTEND] = {.handler = t16_emulate_loregs_rwflags},
	[PROBES_T16_PUSH] = {.decoder = t16_decode_push},
	[PROBES_T16_POP] = {.decoder = t16_decode_pop},
	[PROBES_T16_SEV] = {.handler = kprobe_emulate_none},
	[PROBES_T16_WFE] = {.handler = kprobe_simulate_nop},
	[PROBES_T16_IT] = {.decoder = t16_decode_it},
	[PROBES_T16_CMP] = {.handler = t16_emulate_loregs_rwflags},
	[PROBES_T16_ADDSUB] = {.handler = t16_emulate_loregs_noitrwflags},
	[PROBES_T16_LOGICAL] = {.handler = t16_emulate_loregs_noitrwflags},
	[PROBES_T16_LDR_LIT] = {.handler = t16_simulate_ldr_literal},
	[PROBES_T16_BLX] = {.handler = t16_simulate_bxblx},
	[PROBES_T16_HIREGOPS] = {.decoder = t16_decode_hiregs},
	[PROBES_T16_LDRHSTRH] = {.handler = t16_emulate_loregs_rwflags},
	[PROBES_T16_LDRSTR] = {.handler = t16_simulate_ldrstr_sp_relative},
	[PROBES_T16_ADR] = {.handler = t16_simulate_reladr},
	[PROBES_T16_LDMSTM] = {.handler = t16_emulate_loregs_rwflags},
	[PROBES_T16_BRANCH_COND] = {.decoder = t16_decode_cond_branch},
	[PROBES_T16_BRANCH] = {.handler = t16_simulate_branch},
};

const union decode_action kprobes_t32_actions[NUM_PROBES_T32_ACTIONS] = {
	[PROBES_T32_LDMSTM] = {.decoder = t32_decode_ldmstm},
	[PROBES_T32_LDRDSTRD] = {.handler = t32_emulate_ldrdstrd},
	[PROBES_T32_TABLE_BRANCH] = {.handler = t32_simulate_table_branch},
	[PROBES_T32_TST] = {.handler = t32_emulate_rd8rn16rm0_rwflags},
	[PROBES_T32_MOV] = {.handler = t32_emulate_rd8rn16rm0_rwflags},
	[PROBES_T32_ADDSUB] = {.handler = t32_emulate_rd8rn16rm0_rwflags},
	[PROBES_T32_LOGICAL] = {.handler = t32_emulate_rd8rn16rm0_rwflags},
	[PROBES_T32_CMP] = {.handler = t32_emulate_rd8rn16rm0_rwflags},
	[PROBES_T32_ADDWSUBW_PC] = {.handler = t32_emulate_rd8pc16_noflags,},
	[PROBES_T32_ADDWSUBW] = {.handler = t32_emulate_rd8rn16_noflags},
	[PROBES_T32_MOVW] = {.handler = t32_emulate_rd8rn16_noflags},
	[PROBES_T32_SAT] = {.handler = t32_emulate_rd8rn16rm0_rwflags},
	[PROBES_T32_BITFIELD] = {.handler = t32_emulate_rd8rn16_noflags},
	[PROBES_T32_SEV] = {.handler = kprobe_emulate_none},
	[PROBES_T32_WFE] = {.handler = kprobe_simulate_nop},
	[PROBES_T32_MRS] = {.handler = t32_simulate_mrs},
	[PROBES_T32_BRANCH_COND] = {.decoder = t32_decode_cond_branch},
	[PROBES_T32_BRANCH] = {.handler = t32_simulate_branch},
	[PROBES_T32_PLDI] = {.handler = kprobe_simulate_nop},
	[PROBES_T32_LDR_LIT] = {.handler = t32_simulate_ldr_literal},
	[PROBES_T32_LDRSTR] = {.handler = t32_emulate_ldrstr},
	[PROBES_T32_SIGN_EXTEND] = {.handler = t32_emulate_rd8rn16rm0_rwflags},
	[PROBES_T32_MEDIA] = {.handler = t32_emulate_rd8rn16rm0_rwflags},
	[PROBES_T32_REVERSE] = {.handler = t32_emulate_rd8rn16_noflags},
	[PROBES_T32_MUL_ADD] = {.handler = t32_emulate_rd8rn16rm0_rwflags},
	[PROBES_T32_MUL_ADD2] = {.handler = t32_emulate_rd8rn16rm0ra12_noflags},
	[PROBES_T32_MUL_ADD_LONG] = {
		.handler = t32_emulate_rdlo12rdhi8rn16rm0_noflags},
};
