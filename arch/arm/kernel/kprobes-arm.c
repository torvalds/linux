/*
 * arch/arm/kernel/kprobes-decode.c
 *
 * Copyright (C) 2006, 2007 Motorola Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/*
 * We do not have hardware single-stepping on ARM, This
 * effort is further complicated by the ARM not having a
 * "next PC" register.  Instructions that change the PC
 * can't be safely single-stepped in a MP environment, so
 * we have a lot of work to do:
 *
 * In the prepare phase:
 *   *) If it is an instruction that does anything
 *      with the CPU mode, we reject it for a kprobe.
 *      (This is out of laziness rather than need.  The
 *      instructions could be simulated.)
 *
 *   *) Otherwise, decode the instruction rewriting its
 *      registers to take fixed, ordered registers and
 *      setting a handler for it to run the instruction.
 *
 * In the execution phase by an instruction's handler:
 *
 *   *) If the PC is written to by the instruction, the
 *      instruction must be fully simulated in software.
 *
 *   *) Otherwise, a modified form of the instruction is
 *      directly executed.  Its handler calls the
 *      instruction in insn[0].  In insn[1] is a
 *      "mov pc, lr" to return.
 *
 *      Before calling, load up the reordered registers
 *      from the original instruction's registers.  If one
 *      of the original input registers is the PC, compute
 *      and adjust the appropriate input register.
 *
 *	After call completes, copy the output registers to
 *      the original instruction's original registers.
 *
 * We don't use a real breakpoint instruction since that
 * would have us in the kernel go from SVC mode to SVC
 * mode losing the link register.  Instead we use an
 * undefined instruction.  To simplify processing, the
 * undefined instruction used for kprobes must be reserved
 * exclusively for kprobes use.
 *
 * TODO: ifdef out some instruction decoding based on architecture.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>

#include "kprobes.h"

#define sign_extend(x, signbit) ((x) | (0 - ((x) & (1 << (signbit)))))

#define branch_displacement(insn) sign_extend(((insn) & 0xffffff) << 2, 25)

#if  __LINUX_ARM_ARCH__ >= 6
#define BLX(reg)	"blx	"reg"		\n\t"
#else
#define BLX(reg)	"mov	lr, pc		\n\t"	\
			"mov	pc, "reg"	\n\t"
#endif

#define is_r15(insn, bitpos) (((insn) & (0xf << bitpos)) == (0xf << bitpos))

#define PSR_fs	(PSR_f|PSR_s)

#define KPROBE_RETURN_INSTRUCTION	0xe1a0f00e	/* mov pc, lr */

typedef long (insn_0arg_fn_t)(void);
typedef long (insn_1arg_fn_t)(long);
typedef long (insn_2arg_fn_t)(long, long);
typedef long (insn_3arg_fn_t)(long, long, long);
typedef long (insn_4arg_fn_t)(long, long, long, long);
typedef long long (insn_llret_0arg_fn_t)(void);
typedef long long (insn_llret_3arg_fn_t)(long, long, long);
typedef long long (insn_llret_4arg_fn_t)(long, long, long, long);

union reg_pair {
	long long	dr;
#ifdef __LITTLE_ENDIAN
	struct { long	r0, r1; };
#else
	struct { long	r1, r0; };
#endif
};

/*
 * The insnslot_?arg_r[w]flags() functions below are to keep the
 * msr -> *fn -> mrs instruction sequences indivisible so that
 * the state of the CPSR flags aren't inadvertently modified
 * just before or just after the call.
 */

static inline long __kprobes
insnslot_0arg_rflags(long cpsr, insn_0arg_fn_t *fn)
{
	register long ret asm("r0");

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		: "=r" (ret)
		: [cpsr] "r" (cpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	return ret;
}

static inline long long __kprobes
insnslot_llret_0arg_rflags(long cpsr, insn_llret_0arg_fn_t *fn)
{
	register long ret0 asm("r0");
	register long ret1 asm("r1");
	union reg_pair fnr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		: "=r" (ret0), "=r" (ret1)
		: [cpsr] "r" (cpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	fnr.r0 = ret0;
	fnr.r1 = ret1;
	return fnr.dr;
}

static inline long __kprobes
insnslot_1arg_rflags(long r0, long cpsr, insn_1arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long ret asm("r0");

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		: "=r" (ret)
		: "0" (rr0), [cpsr] "r" (cpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	return ret;
}

static inline long __kprobes
insnslot_2arg_rflags(long r0, long r1, long cpsr, insn_2arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long rr1 asm("r1") = r1;
	register long ret asm("r0");

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		: "=r" (ret)
		: "0" (rr0), "r" (rr1),
		  [cpsr] "r" (cpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	return ret;
}

static inline long __kprobes
insnslot_3arg_rflags(long r0, long r1, long r2, long cpsr, insn_3arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long rr1 asm("r1") = r1;
	register long rr2 asm("r2") = r2;
	register long ret asm("r0");

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		: "=r" (ret)
		: "0" (rr0), "r" (rr1), "r" (rr2),
		  [cpsr] "r" (cpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	return ret;
}

static inline long long __kprobes
insnslot_llret_3arg_rflags(long r0, long r1, long r2, long cpsr,
			   insn_llret_3arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long rr1 asm("r1") = r1;
	register long rr2 asm("r2") = r2;
	register long ret0 asm("r0");
	register long ret1 asm("r1");
	union reg_pair fnr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		: "=r" (ret0), "=r" (ret1)
		: "0" (rr0), "r" (rr1), "r" (rr2),
		  [cpsr] "r" (cpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	fnr.r0 = ret0;
	fnr.r1 = ret1;
	return fnr.dr;
}

static inline long __kprobes
insnslot_4arg_rflags(long r0, long r1, long r2, long r3, long cpsr,
		     insn_4arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long rr1 asm("r1") = r1;
	register long rr2 asm("r2") = r2;
	register long rr3 asm("r3") = r3;
	register long ret asm("r0");

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		: "=r" (ret)
		: "0" (rr0), "r" (rr1), "r" (rr2), "r" (rr3),
		  [cpsr] "r" (cpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	return ret;
}

static inline long __kprobes
insnslot_1arg_rwflags(long r0, long *cpsr, insn_1arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long ret asm("r0");
	long oldcpsr = *cpsr;
	long newcpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[oldcpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		"mrs	%[newcpsr], cpsr	\n\t"
		: "=r" (ret), [newcpsr] "=r" (newcpsr)
		: "0" (rr0), [oldcpsr] "r" (oldcpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	*cpsr = (oldcpsr & ~PSR_fs) | (newcpsr & PSR_fs);
	return ret;
}

static inline long __kprobes
insnslot_2arg_rwflags(long r0, long r1, long *cpsr, insn_2arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long rr1 asm("r1") = r1;
	register long ret asm("r0");
	long oldcpsr = *cpsr;
	long newcpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[oldcpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		"mrs	%[newcpsr], cpsr	\n\t"
		: "=r" (ret), [newcpsr] "=r" (newcpsr)
		: "0" (rr0), "r" (rr1), [oldcpsr] "r" (oldcpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	*cpsr = (oldcpsr & ~PSR_fs) | (newcpsr & PSR_fs);
	return ret;
}

static inline long __kprobes
insnslot_3arg_rwflags(long r0, long r1, long r2, long *cpsr,
		      insn_3arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long rr1 asm("r1") = r1;
	register long rr2 asm("r2") = r2;
	register long ret asm("r0");
	long oldcpsr = *cpsr;
	long newcpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[oldcpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		"mrs	%[newcpsr], cpsr	\n\t"
		: "=r" (ret), [newcpsr] "=r" (newcpsr)
		: "0" (rr0), "r" (rr1), "r" (rr2),
		  [oldcpsr] "r" (oldcpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	*cpsr = (oldcpsr & ~PSR_fs) | (newcpsr & PSR_fs);
	return ret;
}

static inline long __kprobes
insnslot_4arg_rwflags(long r0, long r1, long r2, long r3, long *cpsr,
		      insn_4arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long rr1 asm("r1") = r1;
	register long rr2 asm("r2") = r2;
	register long rr3 asm("r3") = r3;
	register long ret asm("r0");
	long oldcpsr = *cpsr;
	long newcpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[oldcpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		"mrs	%[newcpsr], cpsr	\n\t"
		: "=r" (ret), [newcpsr] "=r" (newcpsr)
		: "0" (rr0), "r" (rr1), "r" (rr2), "r" (rr3),
		  [oldcpsr] "r" (oldcpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	*cpsr = (oldcpsr & ~PSR_fs) | (newcpsr & PSR_fs);
	return ret;
}

static inline long long __kprobes
insnslot_llret_4arg_rwflags(long r0, long r1, long r2, long r3, long *cpsr,
			    insn_llret_4arg_fn_t *fn)
{
	register long rr0 asm("r0") = r0;
	register long rr1 asm("r1") = r1;
	register long rr2 asm("r2") = r2;
	register long rr3 asm("r3") = r3;
	register long ret0 asm("r0");
	register long ret1 asm("r1");
	long oldcpsr = *cpsr;
	long newcpsr;
	union reg_pair fnr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[oldcpsr]	\n\t"
		"mov	lr, pc			\n\t"
		"mov	pc, %[fn]		\n\t"
		"mrs	%[newcpsr], cpsr	\n\t"
		: "=r" (ret0), "=r" (ret1), [newcpsr] "=r" (newcpsr)
		: "0" (rr0), "r" (rr1), "r" (rr2), "r" (rr3),
		  [oldcpsr] "r" (oldcpsr), [fn] "r" (fn)
		: "lr", "cc"
	);
	*cpsr = (oldcpsr & ~PSR_fs) | (newcpsr & PSR_fs);
	fnr.r0 = ret0;
	fnr.r1 = ret1;
	return fnr.dr;
}

/*
 * To avoid the complications of mimicing single-stepping on a
 * processor without a Next-PC or a single-step mode, and to
 * avoid having to deal with the side-effects of boosting, we
 * simulate or emulate (almost) all ARM instructions.
 *
 * "Simulation" is where the instruction's behavior is duplicated in
 * C code.  "Emulation" is where the original instruction is rewritten
 * and executed, often by altering its registers.
 *
 * By having all behavior of the kprobe'd instruction completed before
 * returning from the kprobe_handler(), all locks (scheduler and
 * interrupt) can safely be released.  There is no need for secondary
 * breakpoints, no race with MP or preemptable kernels, nor having to
 * clean up resources counts at a later time impacting overall system
 * performance.  By rewriting the instruction, only the minimum registers
 * need to be loaded and saved back optimizing performance.
 *
 * Calling the insnslot_*_rwflags version of a function doesn't hurt
 * anything even when the CPSR flags aren't updated by the
 * instruction.  It's just a little slower in return for saving
 * a little space by not having a duplicate function that doesn't
 * update the flags.  (The same optimization can be said for
 * instructions that do or don't perform register writeback)
 * Also, instructions can either read the flags, only write the
 * flags, or read and write the flags.  To save combinations
 * rather than for sheer performance, flag functions just assume
 * read and write of flags.
 */

static void __kprobes simulate_bbl(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	long iaddr = (long)p->addr;
	int disp  = branch_displacement(insn);

	if (insn & (1 << 24))
		regs->ARM_lr = iaddr + 4;

	regs->ARM_pc = iaddr + 8 + disp;
}

static void __kprobes simulate_blx1(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	long iaddr = (long)p->addr;
	int disp = branch_displacement(insn);

	regs->ARM_lr = iaddr + 4;
	regs->ARM_pc = iaddr + 8 + disp + ((insn >> 23) & 0x2);
	regs->ARM_cpsr |= PSR_T_BIT;
}

static void __kprobes simulate_blx2bx(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rm = insn & 0xf;
	long rmv = regs->uregs[rm];

	if (insn & (1 << 5))
		regs->ARM_lr = (long)p->addr + 4;

	regs->ARM_pc = rmv & ~0x1;
	regs->ARM_cpsr &= ~PSR_T_BIT;
	if (rmv & 0x1)
		regs->ARM_cpsr |= PSR_T_BIT;
}

static void __kprobes simulate_mrs(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	unsigned long mask = 0xf8ff03df; /* Mask out execution state */
	regs->uregs[rd] = regs->ARM_cpsr & mask;
}

static void __kprobes simulate_mov_ipsp(struct kprobe *p, struct pt_regs *regs)
{
	regs->uregs[12] = regs->uregs[13];
}

static void __kprobes emulate_ldrd(struct kprobe *p, struct pt_regs *regs)
{
	insn_2arg_fn_t *i_fn = (insn_2arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	long ppc = (long)p->addr + 8;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;  /* rm may be invalid, don't care. */
	long rmv = (rm == 15) ? ppc : regs->uregs[rm];
	long rnv = (rn == 15) ? ppc : regs->uregs[rn];

	/* Not following the C calling convention here, so need asm(). */
	__asm__ __volatile__ (
		"ldr	r0, %[rn]	\n\t"
		"ldr	r1, %[rm]	\n\t"
		"msr	cpsr_fs, %[cpsr]\n\t"
		"mov	lr, pc		\n\t"
		"mov	pc, %[i_fn]	\n\t"
		"str	r0, %[rn]	\n\t"	/* in case of writeback */
		"str	r2, %[rd0]	\n\t"
		"str	r3, %[rd1]	\n\t"
		: [rn]  "+m" (rnv),
		  [rd0] "=m" (regs->uregs[rd]),
		  [rd1] "=m" (regs->uregs[rd+1])
		: [rm]   "m" (rmv),
		  [cpsr] "r" (regs->ARM_cpsr),
		  [i_fn] "r" (i_fn)
		: "r0", "r1", "r2", "r3", "lr", "cc"
	);
	if (is_writeback(insn))
		regs->uregs[rn] = rnv;
}

static void __kprobes emulate_strd(struct kprobe *p, struct pt_regs *regs)
{
	insn_4arg_fn_t *i_fn = (insn_4arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	long ppc = (long)p->addr + 8;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm  = insn & 0xf;
	long rnv = (rn == 15) ? ppc : regs->uregs[rn];
	/* rm/rmv may be invalid, don't care. */
	long rmv = (rm == 15) ? ppc : regs->uregs[rm];
	long rnv_wb;

	rnv_wb = insnslot_4arg_rflags(rnv, rmv, regs->uregs[rd],
					       regs->uregs[rd+1],
					       regs->ARM_cpsr, i_fn);
	if (is_writeback(insn))
		regs->uregs[rn] = rnv_wb;
}

static void __kprobes emulate_ldr_old(struct kprobe *p, struct pt_regs *regs)
{
	insn_llret_3arg_fn_t *i_fn = (insn_llret_3arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	long ppc = (long)p->addr + 8;
	union reg_pair fnr;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;
	long rdv;
	long rnv = (rn == 15) ? ppc : regs->uregs[rn];
	long rmv = (rm == 15) ? ppc : regs->uregs[rm];
	long cpsr = regs->ARM_cpsr;

	fnr.dr = insnslot_llret_3arg_rflags(rnv, 0, rmv, cpsr, i_fn);
	if (rn != 15)
		regs->uregs[rn] = fnr.r0;  /* Save Rn in case of writeback. */
	rdv = fnr.r1;

	if (rd == 15) {
#if __LINUX_ARM_ARCH__ >= 5
		cpsr &= ~PSR_T_BIT;
		if (rdv & 0x1)
			cpsr |= PSR_T_BIT;
		regs->ARM_cpsr = cpsr;
		rdv &= ~0x1;
#else
		rdv &= ~0x2;
#endif
	}
	regs->uregs[rd] = rdv;
}

static void __kprobes emulate_str_old(struct kprobe *p, struct pt_regs *regs)
{
	insn_3arg_fn_t *i_fn = (insn_3arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	long iaddr = (long)p->addr;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;
	long rdv = (rd == 15) ? iaddr + str_pc_offset : regs->uregs[rd];
	long rnv = (rn == 15) ? iaddr +  8 : regs->uregs[rn];
	long rmv = regs->uregs[rm];  /* rm/rmv may be invalid, don't care. */
	long rnv_wb;

	rnv_wb = insnslot_3arg_rflags(rnv, rdv, rmv, regs->ARM_cpsr, i_fn);
	if (rn != 15)
		regs->uregs[rn] = rnv_wb;  /* Save Rn in case of writeback. */
}

static void __kprobes emulate_sat(struct kprobe *p, struct pt_regs *regs)
{
	insn_1arg_fn_t *i_fn = (insn_1arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	int rm = insn & 0xf;
	long rmv = regs->uregs[rm];

	/* Writes Q flag */
	regs->uregs[rd] = insnslot_1arg_rwflags(rmv, &regs->ARM_cpsr, i_fn);
}

static void __kprobes emulate_sel(struct kprobe *p, struct pt_regs *regs)
{
	insn_2arg_fn_t *i_fn = (insn_2arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;
	long rnv = regs->uregs[rn];
	long rmv = regs->uregs[rm];

	/* Reads GE bits */
	regs->uregs[rd] = insnslot_2arg_rflags(rnv, rmv, regs->ARM_cpsr, i_fn);
}

static void __kprobes emulate_none(struct kprobe *p, struct pt_regs *regs)
{
	insn_0arg_fn_t *i_fn = (insn_0arg_fn_t *)&p->ainsn.insn[0];

	insnslot_0arg_rflags(regs->ARM_cpsr, i_fn);
}

static void __kprobes emulate_nop(struct kprobe *p, struct pt_regs *regs)
{
}

static void __kprobes
emulate_rd12_modify(struct kprobe *p, struct pt_regs *regs)
{
	insn_1arg_fn_t *i_fn = (insn_1arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	long rdv = regs->uregs[rd];

	regs->uregs[rd] = insnslot_1arg_rflags(rdv, regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_rd12rn0_modify(struct kprobe *p, struct pt_regs *regs)
{
	insn_2arg_fn_t *i_fn = (insn_2arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	int rn = insn & 0xf;
	long rdv = regs->uregs[rd];
	long rnv = regs->uregs[rn];

	regs->uregs[rd] = insnslot_2arg_rflags(rdv, rnv, regs->ARM_cpsr, i_fn);
}

static void __kprobes emulate_rd12rm0(struct kprobe *p, struct pt_regs *regs)
{
	insn_1arg_fn_t *i_fn = (insn_1arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	int rm = insn & 0xf;
	long rmv = regs->uregs[rm];

	regs->uregs[rd] = insnslot_1arg_rflags(rmv, regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_rd12rn16rm0_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	insn_2arg_fn_t *i_fn = (insn_2arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;
	long rnv = regs->uregs[rn];
	long rmv = regs->uregs[rm];

	regs->uregs[rd] =
		insnslot_2arg_rwflags(rnv, rmv, &regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_rd16rn12rs8rm0_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	insn_3arg_fn_t *i_fn = (insn_3arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 16) & 0xf;
	int rn = (insn >> 12) & 0xf;
	int rs = (insn >> 8) & 0xf;
	int rm = insn & 0xf;
	long rnv = regs->uregs[rn];
	long rsv = regs->uregs[rs];
	long rmv = regs->uregs[rm];

	regs->uregs[rd] =
		insnslot_3arg_rwflags(rnv, rsv, rmv, &regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_rd16rs8rm0_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	insn_2arg_fn_t *i_fn = (insn_2arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 16) & 0xf;
	int rs = (insn >> 8) & 0xf;
	int rm = insn & 0xf;
	long rsv = regs->uregs[rs];
	long rmv = regs->uregs[rm];

	regs->uregs[rd] =
		insnslot_2arg_rwflags(rsv, rmv, &regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_rdhi16rdlo12rs8rm0_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	insn_llret_4arg_fn_t *i_fn = (insn_llret_4arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	union reg_pair fnr;
	int rdhi = (insn >> 16) & 0xf;
	int rdlo = (insn >> 12) & 0xf;
	int rs   = (insn >> 8) & 0xf;
	int rm   = insn & 0xf;
	long rsv = regs->uregs[rs];
	long rmv = regs->uregs[rm];

	fnr.dr = insnslot_llret_4arg_rwflags(regs->uregs[rdhi],
					     regs->uregs[rdlo], rsv, rmv,
					     &regs->ARM_cpsr, i_fn);
	regs->uregs[rdhi] = fnr.r0;
	regs->uregs[rdlo] = fnr.r1;
}

static void __kprobes
emulate_alu_imm_rflags(struct kprobe *p, struct pt_regs *regs)
{
	insn_1arg_fn_t *i_fn = (insn_1arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	long rnv = (rn == 15) ? (long)p->addr + 8 : regs->uregs[rn];

	regs->uregs[rd] = insnslot_1arg_rflags(rnv, regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_alu_imm_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	insn_1arg_fn_t *i_fn = (insn_1arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	long rnv = (rn == 15) ? (long)p->addr + 8 : regs->uregs[rn];

	regs->uregs[rd] = insnslot_1arg_rwflags(rnv, &regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_alu_tests_imm(struct kprobe *p, struct pt_regs *regs)
{
	insn_1arg_fn_t *i_fn = (insn_1arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	int rn = (insn >> 16) & 0xf;
	long rnv = (rn == 15) ? (long)p->addr + 8 : regs->uregs[rn];

	insnslot_1arg_rwflags(rnv, &regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_alu_rflags(struct kprobe *p, struct pt_regs *regs)
{
	insn_3arg_fn_t *i_fn = (insn_3arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	long ppc = (long)p->addr + 8;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;	/* rn/rnv/rs/rsv may be */
	int rs = (insn >> 8) & 0xf;	/* invalid, don't care. */
	int rm = insn & 0xf;
	long rnv = (rn == 15) ? ppc : regs->uregs[rn];
	long rmv = (rm == 15) ? ppc : regs->uregs[rm];
	long rsv = regs->uregs[rs];

	regs->uregs[rd] =
		insnslot_3arg_rflags(rnv, rmv, rsv, regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_alu_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	insn_3arg_fn_t *i_fn = (insn_3arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	long ppc = (long)p->addr + 8;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;	/* rn/rnv/rs/rsv may be */
	int rs = (insn >> 8) & 0xf;	/* invalid, don't care. */
	int rm = insn & 0xf;
	long rnv = (rn == 15) ? ppc : regs->uregs[rn];
	long rmv = (rm == 15) ? ppc : regs->uregs[rm];
	long rsv = regs->uregs[rs];

	regs->uregs[rd] =
		insnslot_3arg_rwflags(rnv, rmv, rsv, &regs->ARM_cpsr, i_fn);
}

static void __kprobes
emulate_alu_tests(struct kprobe *p, struct pt_regs *regs)
{
	insn_3arg_fn_t *i_fn = (insn_3arg_fn_t *)&p->ainsn.insn[0];
	kprobe_opcode_t insn = p->opcode;
	long ppc = (long)p->addr + 8;
	int rn = (insn >> 16) & 0xf;
	int rs = (insn >> 8) & 0xf;	/* rs/rsv may be invalid, don't care. */
	int rm = insn & 0xf;
	long rnv = (rn == 15) ? ppc : regs->uregs[rn];
	long rmv = (rm == 15) ? ppc : regs->uregs[rm];
	long rsv = regs->uregs[rs];

	insnslot_3arg_rwflags(rnv, rmv, rsv, &regs->ARM_cpsr, i_fn);
}

static enum kprobe_insn __kprobes
prep_emulate_ldr_str(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	int not_imm = (insn & (1 << 26)) ? (insn & (1 << 25))
					 : (~insn & (1 << 22));

	if (is_writeback(insn) && is_r15(insn, 16))
		return INSN_REJECTED;	/* Writeback to PC */

	insn &= 0xfff00fff;
	insn |= 0x00001000;	/* Rn = r0, Rd = r1 */
	if (not_imm) {
		insn &= ~0xf;
		insn |= 2;	/* Rm = r2 */
	}
	asi->insn[0] = insn;
	asi->insn_handler = (insn & (1 << 20)) ? emulate_ldr_old : emulate_str_old;
	return INSN_GOOD;
}

static enum kprobe_insn __kprobes
prep_emulate_rd12_modify(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	if (is_r15(insn, 12))
		return INSN_REJECTED;	/* Rd is PC */

	insn &= 0xffff0fff;	/* Rd = r0 */
	asi->insn[0] = insn;
	asi->insn_handler = emulate_rd12_modify;
	return INSN_GOOD;
}

static enum kprobe_insn __kprobes
prep_emulate_rd12rn0_modify(kprobe_opcode_t insn,
			    struct arch_specific_insn *asi)
{
	if (is_r15(insn, 12))
		return INSN_REJECTED;	/* Rd is PC */

	insn &= 0xffff0ff0;	/* Rd = r0 */
	insn |= 0x00000001;	/* Rn = r1 */
	asi->insn[0] = insn;
	asi->insn_handler = emulate_rd12rn0_modify;
	return INSN_GOOD;
}

static enum kprobe_insn __kprobes
prep_emulate_rd12rm0(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	if (is_r15(insn, 12))
		return INSN_REJECTED;	/* Rd is PC */

	insn &= 0xffff0ff0;	/* Rd = r0, Rm = r0 */
	asi->insn[0] = insn;
	asi->insn_handler = emulate_rd12rm0;
	return INSN_GOOD;
}

static enum kprobe_insn __kprobes
prep_emulate_rd12rn16rm0_wflags(kprobe_opcode_t insn,
				struct arch_specific_insn *asi)
{
	if (is_r15(insn, 12))
		return INSN_REJECTED;	/* Rd is PC */

	insn &= 0xfff00ff0;	/* Rd = r0, Rn = r0 */
	insn |= 0x00000001;	/* Rm = r1 */
	asi->insn[0] = insn;
	asi->insn_handler = emulate_rd12rn16rm0_rwflags;
	return INSN_GOOD;
}

static enum kprobe_insn __kprobes
prep_emulate_rd16rs8rm0_wflags(kprobe_opcode_t insn,
			       struct arch_specific_insn *asi)
{
	if (is_r15(insn, 16))
		return INSN_REJECTED;	/* Rd is PC */

	insn &= 0xfff0f0f0;	/* Rd = r0, Rs = r0 */
	insn |= 0x00000001;	/* Rm = r1          */
	asi->insn[0] = insn;
	asi->insn_handler = emulate_rd16rs8rm0_rwflags;
	return INSN_GOOD;
}

static enum kprobe_insn __kprobes
prep_emulate_rd16rn12rs8rm0_wflags(kprobe_opcode_t insn,
				   struct arch_specific_insn *asi)
{
	if (is_r15(insn, 16))
		return INSN_REJECTED;	/* Rd is PC */

	insn &= 0xfff000f0;	/* Rd = r0, Rn = r0 */
	insn |= 0x00000102;	/* Rs = r1, Rm = r2 */
	asi->insn[0] = insn;
	asi->insn_handler = emulate_rd16rn12rs8rm0_rwflags;
	return INSN_GOOD;
}

static enum kprobe_insn __kprobes
prep_emulate_rdhi16rdlo12rs8rm0_wflags(kprobe_opcode_t insn,
				       struct arch_specific_insn *asi)
{
	if (is_r15(insn, 16) || is_r15(insn, 12))
		return INSN_REJECTED;	/* RdHi or RdLo is PC */

	insn &= 0xfff000f0;	/* RdHi = r0, RdLo = r1 */
	insn |= 0x00001203;	/* Rs = r2, Rm = r3 */
	asi->insn[0] = insn;
	asi->insn_handler = emulate_rdhi16rdlo12rs8rm0_rwflags;
	return INSN_GOOD;
}

static void __kprobes
emulate_ldrdstrd(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = (unsigned long)p->addr + 8;
	int rt = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rtv asm("r0") = regs->uregs[rt];
	register unsigned long rt2v asm("r1") = regs->uregs[rt+1];
	register unsigned long rnv asm("r2") = (rn == 15) ? pc
							  : regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		BLX("%[fn]")
		: "=r" (rtv), "=r" (rt2v), "=r" (rnv)
		: "0" (rtv), "1" (rt2v), "2" (rnv), "r" (rmv),
		  [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rt] = rtv;
	regs->uregs[rt+1] = rt2v;
	if (is_writeback(insn))
		regs->uregs[rn] = rnv;
}

static void __kprobes
emulate_ldr(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = (unsigned long)p->addr + 8;
	int rt = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rtv asm("r0");
	register unsigned long rnv asm("r2") = (rn == 15) ? pc
							  : regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		BLX("%[fn]")
		: "=r" (rtv), "=r" (rnv)
		: "1" (rnv), "r" (rmv), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	if (rt == 15)
		load_write_pc(rtv, regs);
	else
		regs->uregs[rt] = rtv;

	if (is_writeback(insn))
		regs->uregs[rn] = rnv;
}

static void __kprobes
emulate_str(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long rtpc = (unsigned long)p->addr + str_pc_offset;
	unsigned long rnpc = (unsigned long)p->addr + 8;
	int rt = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rtv asm("r0") = (rt == 15) ? rtpc
							  : regs->uregs[rt];
	register unsigned long rnv asm("r2") = (rn == 15) ? rnpc
							  : regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		BLX("%[fn]")
		: "=r" (rnv)
		: "r" (rtv), "0" (rnv), "r" (rmv), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	if (is_writeback(insn))
		regs->uregs[rn] = rnv;
}

static void __kprobes
emulate_rd12rn16rm0rs8_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	unsigned long pc = (unsigned long)p->addr + 8;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;
	int rs = (insn >> 8) & 0xf;

	register unsigned long rdv asm("r0") = regs->uregs[rd];
	register unsigned long rnv asm("r2") = (rn == 15) ? pc
							  : regs->uregs[rn];
	register unsigned long rmv asm("r3") = (rm == 15) ? pc
							  : regs->uregs[rm];
	register unsigned long rsv asm("r1") = regs->uregs[rs];
	unsigned long cpsr = regs->ARM_cpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		BLX("%[fn]")
		"mrs	%[cpsr], cpsr		\n\t"
		: "=r" (rdv), [cpsr] "=r" (cpsr)
		: "0" (rdv), "r" (rnv), "r" (rmv), "r" (rsv),
		  "1" (cpsr), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	if (rd == 15)
		alu_write_pc(rdv, regs);
	else
		regs->uregs[rd] = rdv;
	regs->ARM_cpsr = (regs->ARM_cpsr & ~APSR_MASK) | (cpsr & APSR_MASK);
}

static void __kprobes
emulate_rd12rn16rm0_rwflags_nopc(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rdv asm("r0") = regs->uregs[rd];
	register unsigned long rnv asm("r2") = regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];
	unsigned long cpsr = regs->ARM_cpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		BLX("%[fn]")
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
emulate_rd16rn12rm0rs8_rwflags_nopc(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 16) & 0xf;
	int rn = (insn >> 12) & 0xf;
	int rm = insn & 0xf;
	int rs = (insn >> 8) & 0xf;

	register unsigned long rdv asm("r2") = regs->uregs[rd];
	register unsigned long rnv asm("r0") = regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];
	register unsigned long rsv asm("r1") = regs->uregs[rs];
	unsigned long cpsr = regs->ARM_cpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		BLX("%[fn]")
		"mrs	%[cpsr], cpsr		\n\t"
		: "=r" (rdv), [cpsr] "=r" (cpsr)
		: "0" (rdv), "r" (rnv), "r" (rmv), "r" (rsv),
		  "1" (cpsr), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rd] = rdv;
	regs->ARM_cpsr = (regs->ARM_cpsr & ~APSR_MASK) | (cpsr & APSR_MASK);
}

static void __kprobes
emulate_rd12rm0_noflags_nopc(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rd = (insn >> 12) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rdv asm("r0") = regs->uregs[rd];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		BLX("%[fn]")
		: "=r" (rdv)
		: "0" (rdv), "r" (rmv), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rd] = rdv;
}

static void __kprobes
emulate_rdlo12rdhi16rn0rm8_rwflags_nopc(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rdlo = (insn >> 12) & 0xf;
	int rdhi = (insn >> 16) & 0xf;
	int rn = insn & 0xf;
	int rm = (insn >> 8) & 0xf;

	register unsigned long rdlov asm("r0") = regs->uregs[rdlo];
	register unsigned long rdhiv asm("r2") = regs->uregs[rdhi];
	register unsigned long rnv asm("r3") = regs->uregs[rn];
	register unsigned long rmv asm("r1") = regs->uregs[rm];
	unsigned long cpsr = regs->ARM_cpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		BLX("%[fn]")
		"mrs	%[cpsr], cpsr		\n\t"
		: "=r" (rdlov), "=r" (rdhiv), [cpsr] "=r" (cpsr)
		: "0" (rdlov), "1" (rdhiv), "r" (rnv), "r" (rmv),
		  "2" (cpsr), [fn] "r" (p->ainsn.insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rdlo] = rdlov;
	regs->uregs[rdhi] = rdhiv;
	regs->ARM_cpsr = (regs->ARM_cpsr & ~APSR_MASK) | (cpsr & APSR_MASK);
}

/*
 * For the instruction masking and comparisons in all the "space_*"
 * functions below, Do _not_ rearrange the order of tests unless
 * you're very, very sure of what you are doing.  For the sake of
 * efficiency, the masks for some tests sometimes assume other test
 * have been done prior to them so the number of patterns to test
 * for an instruction set can be as broad as possible to reduce the
 * number of tests needed.
 */

static const union decode_item arm_1111_table[] = {
	/* Unconditional instructions					*/

	/* memory hint		1111 0100 x001 xxxx xxxx xxxx xxxx xxxx */
	/* PLDI (immediate)	1111 0100 x101 xxxx xxxx xxxx xxxx xxxx */
	/* PLDW (immediate)	1111 0101 x001 xxxx xxxx xxxx xxxx xxxx */
	/* PLD (immediate)	1111 0101 x101 xxxx xxxx xxxx xxxx xxxx */
	DECODE_SIMULATE	(0xfe300000, 0xf4100000, kprobe_simulate_nop),

	/* BLX (immediate)	1111 101x xxxx xxxx xxxx xxxx xxxx xxxx */
	DECODE_SIMULATE	(0xfe000000, 0xfa000000, simulate_blx1),

	/* CPS			1111 0001 0000 xxx0 xxxx xxxx xx0x xxxx */
	/* SETEND		1111 0001 0000 0001 xxxx xxxx 0000 xxxx */
	/* SRS			1111 100x x1x0 xxxx xxxx xxxx xxxx xxxx */
	/* RFE			1111 100x x0x1 xxxx xxxx xxxx xxxx xxxx */

	/* Coprocessor instructions... */
	/* MCRR2		1111 1100 0100 xxxx xxxx xxxx xxxx xxxx */
	/* MRRC2		1111 1100 0101 xxxx xxxx xxxx xxxx xxxx */
	/* LDC2			1111 110x xxx1 xxxx xxxx xxxx xxxx xxxx */
	/* STC2			1111 110x xxx0 xxxx xxxx xxxx xxxx xxxx */
	/* CDP2			1111 1110 xxxx xxxx xxxx xxxx xxx0 xxxx */
	/* MCR2			1111 1110 xxx0 xxxx xxxx xxxx xxx1 xxxx */
	/* MRC2			1111 1110 xxx1 xxxx xxxx xxxx xxx1 xxxx */

	/* Other unallocated instructions...				*/
	DECODE_END
};

static const union decode_item arm_cccc_0001_0xx0____0xxx_table[] = {
	/* Miscellaneous instructions					*/

	/* MRS cpsr		cccc 0001 0000 xxxx xxxx xxxx 0000 xxxx */
	DECODE_SIMULATEX(0x0ff000f0, 0x01000000, simulate_mrs,
						 REGS(0, NOPC, 0, 0, 0)),

	/* BX			cccc 0001 0010 xxxx xxxx xxxx 0001 xxxx */
	DECODE_SIMULATE	(0x0ff000f0, 0x01200010, simulate_blx2bx),

	/* BLX (register)	cccc 0001 0010 xxxx xxxx xxxx 0011 xxxx */
	DECODE_SIMULATEX(0x0ff000f0, 0x01200030, simulate_blx2bx,
						 REGS(0, 0, 0, 0, NOPC)),

	/* CLZ			cccc 0001 0110 xxxx xxxx xxxx 0001 xxxx */
	DECODE_EMULATEX	(0x0ff000f0, 0x01600010, emulate_rd12rm0_noflags_nopc,
						 REGS(0, NOPC, 0, 0, NOPC)),

	/* QADD			cccc 0001 0000 xxxx xxxx xxxx 0101 xxxx */
	/* QSUB			cccc 0001 0010 xxxx xxxx xxxx 0101 xxxx */
	/* QDADD		cccc 0001 0100 xxxx xxxx xxxx 0101 xxxx */
	/* QDSUB		cccc 0001 0110 xxxx xxxx xxxx 0101 xxxx */
	DECODE_EMULATEX	(0x0f9000f0, 0x01000050, emulate_rd12rn16rm0_rwflags_nopc,
						 REGS(NOPC, NOPC, 0, 0, NOPC)),

	/* BXJ			cccc 0001 0010 xxxx xxxx xxxx 0010 xxxx */
	/* MSR			cccc 0001 0x10 xxxx xxxx xxxx 0000 xxxx */
	/* MRS spsr		cccc 0001 0100 xxxx xxxx xxxx 0000 xxxx */
	/* BKPT			1110 0001 0010 xxxx xxxx xxxx 0111 xxxx */
	/* SMC			cccc 0001 0110 xxxx xxxx xxxx 0111 xxxx */
	/* And unallocated instructions...				*/
	DECODE_END
};

static const union decode_item arm_cccc_0001_0xx0____1xx0_table[] = {
	/* Halfword multiply and multiply-accumulate			*/

	/* SMLALxy		cccc 0001 0100 xxxx xxxx xxxx 1xx0 xxxx */
	DECODE_EMULATEX	(0x0ff00090, 0x01400080, emulate_rdlo12rdhi16rn0rm8_rwflags_nopc,
						 REGS(NOPC, NOPC, NOPC, 0, NOPC)),

	/* SMULWy		cccc 0001 0010 xxxx xxxx xxxx 1x10 xxxx */
	DECODE_OR	(0x0ff000b0, 0x012000a0),
	/* SMULxy		cccc 0001 0110 xxxx xxxx xxxx 1xx0 xxxx */
	DECODE_EMULATEX	(0x0ff00090, 0x01600080, emulate_rd16rn12rm0rs8_rwflags_nopc,
						 REGS(NOPC, 0, NOPC, 0, NOPC)),

	/* SMLAxy		cccc 0001 0000 xxxx xxxx xxxx 1xx0 xxxx */
	DECODE_OR	(0x0ff00090, 0x01000080),
	/* SMLAWy		cccc 0001 0010 xxxx xxxx xxxx 1x00 xxxx */
	DECODE_EMULATEX	(0x0ff000b0, 0x01200080, emulate_rd16rn12rm0rs8_rwflags_nopc,
						 REGS(NOPC, NOPC, NOPC, 0, NOPC)),

	DECODE_END
};

static const union decode_item arm_cccc_0000_____1001_table[] = {
	/* Multiply and multiply-accumulate				*/

	/* MUL			cccc 0000 0000 xxxx xxxx xxxx 1001 xxxx */
	/* MULS			cccc 0000 0001 xxxx xxxx xxxx 1001 xxxx */
	DECODE_EMULATEX	(0x0fe000f0, 0x00000090, emulate_rd16rn12rm0rs8_rwflags_nopc,
						 REGS(NOPC, 0, NOPC, 0, NOPC)),

	/* MLA			cccc 0000 0010 xxxx xxxx xxxx 1001 xxxx */
	/* MLAS			cccc 0000 0011 xxxx xxxx xxxx 1001 xxxx */
	DECODE_OR	(0x0fe000f0, 0x00200090),
	/* MLS			cccc 0000 0110 xxxx xxxx xxxx 1001 xxxx */
	DECODE_EMULATEX	(0x0ff000f0, 0x00600090, emulate_rd16rn12rm0rs8_rwflags_nopc,
						 REGS(NOPC, NOPC, NOPC, 0, NOPC)),

	/* UMAAL		cccc 0000 0100 xxxx xxxx xxxx 1001 xxxx */
	DECODE_OR	(0x0ff000f0, 0x00400090),
	/* UMULL		cccc 0000 1000 xxxx xxxx xxxx 1001 xxxx */
	/* UMULLS		cccc 0000 1001 xxxx xxxx xxxx 1001 xxxx */
	/* UMLAL		cccc 0000 1010 xxxx xxxx xxxx 1001 xxxx */
	/* UMLALS		cccc 0000 1011 xxxx xxxx xxxx 1001 xxxx */
	/* SMULL		cccc 0000 1100 xxxx xxxx xxxx 1001 xxxx */
	/* SMULLS		cccc 0000 1101 xxxx xxxx xxxx 1001 xxxx */
	/* SMLAL		cccc 0000 1110 xxxx xxxx xxxx 1001 xxxx */
	/* SMLALS		cccc 0000 1111 xxxx xxxx xxxx 1001 xxxx */
	DECODE_EMULATEX	(0x0f8000f0, 0x00800090, emulate_rdlo12rdhi16rn0rm8_rwflags_nopc,
						 REGS(NOPC, NOPC, NOPC, 0, NOPC)),

	DECODE_END
};

static const union decode_item arm_cccc_0001_____1001_table[] = {
	/* Synchronization primitives					*/

	/* SMP/SWPB		cccc 0001 0x00 xxxx xxxx xxxx 1001 xxxx */
	DECODE_EMULATEX	(0x0fb000f0, 0x01000090, emulate_rd12rn16rm0_rwflags_nopc,
						 REGS(NOPC, NOPC, 0, 0, NOPC)),

	/* LDREX/STREX{,D,B,H}	cccc 0001 1xxx xxxx xxxx xxxx 1001 xxxx */
	/* And unallocated instructions...				*/
	DECODE_END
};

static const union decode_item arm_cccc_000x_____1xx1_table[] = {
	/* Extra load/store instructions				*/

	/* LDRD/STRD lr,pc,{...	cccc 000x x0x0 xxxx 111x xxxx 1101 xxxx */
	DECODE_REJECT	(0x0e10e0d0, 0x0000e0d0),

	/* LDRD (register)	cccc 000x x0x0 xxxx xxxx xxxx 1101 xxxx */
	/* STRD (register)	cccc 000x x0x0 xxxx xxxx xxxx 1111 xxxx */
	DECODE_EMULATEX	(0x0e5000d0, 0x000000d0, emulate_ldrdstrd,
						 REGS(NOPCWB, NOPCX, 0, 0, NOPC)),

	/* LDRD (immediate)	cccc 000x x1x0 xxxx xxxx xxxx 1101 xxxx */
	/* STRD (immediate)	cccc 000x x1x0 xxxx xxxx xxxx 1111 xxxx */
	DECODE_EMULATEX	(0x0e5000d0, 0x004000d0, emulate_ldrdstrd,
						 REGS(NOPCWB, NOPCX, 0, 0, 0)),

	/* STRH (register)	cccc 000x x0x0 xxxx xxxx xxxx 1011 xxxx */
	DECODE_EMULATEX	(0x0e5000f0, 0x000000b0, emulate_str,
						 REGS(NOPCWB, NOPC, 0, 0, NOPC)),

	/* LDRH (register)	cccc 000x x0x1 xxxx xxxx xxxx 1011 xxxx */
	/* LDRSB (register)	cccc 000x x0x1 xxxx xxxx xxxx 1101 xxxx */
	/* LDRSH (register)	cccc 000x x0x1 xxxx xxxx xxxx 1111 xxxx */
	DECODE_EMULATEX	(0x0e500090, 0x00100090, emulate_ldr,
						 REGS(NOPCWB, NOPC, 0, 0, NOPC)),

	/* STRH (immediate)	cccc 000x x1x0 xxxx xxxx xxxx 1011 xxxx */
	DECODE_EMULATEX	(0x0e5000f0, 0x004000b0, emulate_str,
						 REGS(NOPCWB, NOPC, 0, 0, 0)),

	/* LDRH (immediate)	cccc 000x x1x1 xxxx xxxx xxxx 1011 xxxx */
	/* LDRSB (immediate)	cccc 000x x1x1 xxxx xxxx xxxx 1101 xxxx */
	/* LDRSH (immediate)	cccc 000x x1x1 xxxx xxxx xxxx 1111 xxxx */
	DECODE_EMULATEX	(0x0e500090, 0x00500090, emulate_ldr,
						 REGS(NOPCWB, NOPC, 0, 0, 0)),

	DECODE_END
};

static const union decode_item arm_cccc_000x_table[] = {
	/* Data-processing (register)					*/

	/* <op>S PC, ...	cccc 000x xxx1 xxxx 1111 xxxx xxxx xxxx */
	DECODE_REJECT	(0x0e10f000, 0x0010f000),

	/* MOV IP, SP		1110 0001 1010 0000 1100 0000 0000 1101 */
	DECODE_SIMULATE	(0xffffffff, 0xe1a0c00d, simulate_mov_ipsp),

	/* TST (register)	cccc 0001 0001 xxxx xxxx xxxx xxx0 xxxx */
	/* TEQ (register)	cccc 0001 0011 xxxx xxxx xxxx xxx0 xxxx */
	/* CMP (register)	cccc 0001 0101 xxxx xxxx xxxx xxx0 xxxx */
	/* CMN (register)	cccc 0001 0111 xxxx xxxx xxxx xxx0 xxxx */
	DECODE_EMULATEX	(0x0f900010, 0x01100000, emulate_rd12rn16rm0rs8_rwflags,
						 REGS(ANY, 0, 0, 0, ANY)),

	/* MOV (register)	cccc 0001 101x xxxx xxxx xxxx xxx0 xxxx */
	/* MVN (register)	cccc 0001 111x xxxx xxxx xxxx xxx0 xxxx */
	DECODE_EMULATEX	(0x0fa00010, 0x01a00000, emulate_rd12rn16rm0rs8_rwflags,
						 REGS(0, ANY, 0, 0, ANY)),

	/* AND (register)	cccc 0000 000x xxxx xxxx xxxx xxx0 xxxx */
	/* EOR (register)	cccc 0000 001x xxxx xxxx xxxx xxx0 xxxx */
	/* SUB (register)	cccc 0000 010x xxxx xxxx xxxx xxx0 xxxx */
	/* RSB (register)	cccc 0000 011x xxxx xxxx xxxx xxx0 xxxx */
	/* ADD (register)	cccc 0000 100x xxxx xxxx xxxx xxx0 xxxx */
	/* ADC (register)	cccc 0000 101x xxxx xxxx xxxx xxx0 xxxx */
	/* SBC (register)	cccc 0000 110x xxxx xxxx xxxx xxx0 xxxx */
	/* RSC (register)	cccc 0000 111x xxxx xxxx xxxx xxx0 xxxx */
	/* ORR (register)	cccc 0001 100x xxxx xxxx xxxx xxx0 xxxx */
	/* BIC (register)	cccc 0001 110x xxxx xxxx xxxx xxx0 xxxx */
	DECODE_EMULATEX	(0x0e000010, 0x00000000, emulate_rd12rn16rm0rs8_rwflags,
						 REGS(ANY, ANY, 0, 0, ANY)),

	/* TST (reg-shift reg)	cccc 0001 0001 xxxx xxxx xxxx 0xx1 xxxx */
	/* TEQ (reg-shift reg)	cccc 0001 0011 xxxx xxxx xxxx 0xx1 xxxx */
	/* CMP (reg-shift reg)	cccc 0001 0101 xxxx xxxx xxxx 0xx1 xxxx */
	/* CMN (reg-shift reg)	cccc 0001 0111 xxxx xxxx xxxx 0xx1 xxxx */
	DECODE_EMULATEX	(0x0f900090, 0x01100010, emulate_rd12rn16rm0rs8_rwflags,
						 REGS(ANY, 0, NOPC, 0, ANY)),

	/* MOV (reg-shift reg)	cccc 0001 101x xxxx xxxx xxxx 0xx1 xxxx */
	/* MVN (reg-shift reg)	cccc 0001 111x xxxx xxxx xxxx 0xx1 xxxx */
	DECODE_EMULATEX	(0x0fa00090, 0x01a00010, emulate_rd12rn16rm0rs8_rwflags,
						 REGS(0, ANY, NOPC, 0, ANY)),

	/* AND (reg-shift reg)	cccc 0000 000x xxxx xxxx xxxx 0xx1 xxxx */
	/* EOR (reg-shift reg)	cccc 0000 001x xxxx xxxx xxxx 0xx1 xxxx */
	/* SUB (reg-shift reg)	cccc 0000 010x xxxx xxxx xxxx 0xx1 xxxx */
	/* RSB (reg-shift reg)	cccc 0000 011x xxxx xxxx xxxx 0xx1 xxxx */
	/* ADD (reg-shift reg)	cccc 0000 100x xxxx xxxx xxxx 0xx1 xxxx */
	/* ADC (reg-shift reg)	cccc 0000 101x xxxx xxxx xxxx 0xx1 xxxx */
	/* SBC (reg-shift reg)	cccc 0000 110x xxxx xxxx xxxx 0xx1 xxxx */
	/* RSC (reg-shift reg)	cccc 0000 111x xxxx xxxx xxxx 0xx1 xxxx */
	/* ORR (reg-shift reg)	cccc 0001 100x xxxx xxxx xxxx 0xx1 xxxx */
	/* BIC (reg-shift reg)	cccc 0001 110x xxxx xxxx xxxx 0xx1 xxxx */
	DECODE_EMULATEX	(0x0e000090, 0x00000010, emulate_rd12rn16rm0rs8_rwflags,
						 REGS(ANY, ANY, NOPC, 0, ANY)),

	DECODE_END
};

static const union decode_item arm_cccc_001x_table[] = {
	/* Data-processing (immediate)					*/

	/* MOVW			cccc 0011 0000 xxxx xxxx xxxx xxxx xxxx */
	/* MOVT			cccc 0011 0100 xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0x0fb00000, 0x03000000, emulate_rd12rm0_noflags_nopc,
						 REGS(0, NOPC, 0, 0, 0)),

	/* YIELD		cccc 0011 0010 0000 xxxx xxxx 0000 0001 */
	DECODE_OR	(0x0fff00ff, 0x03200001),
	/* SEV			cccc 0011 0010 0000 xxxx xxxx 0000 0100 */
	DECODE_EMULATE	(0x0fff00ff, 0x03200004, kprobe_emulate_none),
	/* NOP			cccc 0011 0010 0000 xxxx xxxx 0000 0000 */
	/* WFE			cccc 0011 0010 0000 xxxx xxxx 0000 0010 */
	/* WFI			cccc 0011 0010 0000 xxxx xxxx 0000 0011 */
	DECODE_SIMULATE	(0x0fff00fc, 0x03200000, kprobe_simulate_nop),
	/* DBG			cccc 0011 0010 0000 xxxx xxxx ffff xxxx */
	/* unallocated hints	cccc 0011 0010 0000 xxxx xxxx xxxx xxxx */
	/* MSR (immediate)	cccc 0011 0x10 xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0x0fb00000, 0x03200000),

	/* <op>S PC, ...	cccc 001x xxx1 xxxx 1111 xxxx xxxx xxxx */
	DECODE_REJECT	(0x0e10f000, 0x0210f000),

	/* TST (immediate)	cccc 0011 0001 xxxx xxxx xxxx xxxx xxxx */
	/* TEQ (immediate)	cccc 0011 0011 xxxx xxxx xxxx xxxx xxxx */
	/* CMP (immediate)	cccc 0011 0101 xxxx xxxx xxxx xxxx xxxx */
	/* CMN (immediate)	cccc 0011 0111 xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0x0f900000, 0x03100000, emulate_rd12rn16rm0rs8_rwflags,
						 REGS(ANY, 0, 0, 0, 0)),

	/* MOV (immediate)	cccc 0011 101x xxxx xxxx xxxx xxxx xxxx */
	/* MVN (immediate)	cccc 0011 111x xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0x0fa00000, 0x03a00000, emulate_rd12rn16rm0rs8_rwflags,
						 REGS(0, ANY, 0, 0, 0)),

	/* AND (immediate)	cccc 0010 000x xxxx xxxx xxxx xxxx xxxx */
	/* EOR (immediate)	cccc 0010 001x xxxx xxxx xxxx xxxx xxxx */
	/* SUB (immediate)	cccc 0010 010x xxxx xxxx xxxx xxxx xxxx */
	/* RSB (immediate)	cccc 0010 011x xxxx xxxx xxxx xxxx xxxx */
	/* ADD (immediate)	cccc 0010 100x xxxx xxxx xxxx xxxx xxxx */
	/* ADC (immediate)	cccc 0010 101x xxxx xxxx xxxx xxxx xxxx */
	/* SBC (immediate)	cccc 0010 110x xxxx xxxx xxxx xxxx xxxx */
	/* RSC (immediate)	cccc 0010 111x xxxx xxxx xxxx xxxx xxxx */
	/* ORR (immediate)	cccc 0011 100x xxxx xxxx xxxx xxxx xxxx */
	/* BIC (immediate)	cccc 0011 110x xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0x0e000000, 0x02000000, emulate_rd12rn16rm0rs8_rwflags,
						 REGS(ANY, ANY, 0, 0, 0)),

	DECODE_END
};

static const union decode_item arm_cccc_0110_____xxx1_table[] = {
	/* Media instructions						*/

	/* SEL			cccc 0110 1000 xxxx xxxx xxxx 1011 xxxx */
	DECODE_EMULATEX	(0x0ff000f0, 0x068000b0, emulate_rd12rn16rm0_rwflags_nopc,
						 REGS(NOPC, NOPC, 0, 0, NOPC)),

	/* SSAT			cccc 0110 101x xxxx xxxx xxxx xx01 xxxx */
	/* USAT			cccc 0110 111x xxxx xxxx xxxx xx01 xxxx */
	DECODE_OR(0x0fa00030, 0x06a00010),
	/* SSAT16		cccc 0110 1010 xxxx xxxx xxxx 0011 xxxx */
	/* USAT16		cccc 0110 1110 xxxx xxxx xxxx 0011 xxxx */
	DECODE_EMULATEX	(0x0fb000f0, 0x06a00030, emulate_rd12rn16rm0_rwflags_nopc,
						 REGS(0, NOPC, 0, 0, NOPC)),

	/* REV			cccc 0110 1011 xxxx xxxx xxxx 0011 xxxx */
	/* REV16		cccc 0110 1011 xxxx xxxx xxxx 1011 xxxx */
	/* RBIT			cccc 0110 1111 xxxx xxxx xxxx 0011 xxxx */
	/* REVSH		cccc 0110 1111 xxxx xxxx xxxx 1011 xxxx */
	DECODE_EMULATEX	(0x0fb00070, 0x06b00030, emulate_rd12rm0_noflags_nopc,
						 REGS(0, NOPC, 0, 0, NOPC)),

	/* ???			cccc 0110 0x00 xxxx xxxx xxxx xxx1 xxxx */
	DECODE_REJECT	(0x0fb00010, 0x06000010),
	/* ???			cccc 0110 0xxx xxxx xxxx xxxx 1011 xxxx */
	DECODE_REJECT	(0x0f8000f0, 0x060000b0),
	/* ???			cccc 0110 0xxx xxxx xxxx xxxx 1101 xxxx */
	DECODE_REJECT	(0x0f8000f0, 0x060000d0),
	/* SADD16		cccc 0110 0001 xxxx xxxx xxxx 0001 xxxx */
	/* SADDSUBX		cccc 0110 0001 xxxx xxxx xxxx 0011 xxxx */
	/* SSUBADDX		cccc 0110 0001 xxxx xxxx xxxx 0101 xxxx */
	/* SSUB16		cccc 0110 0001 xxxx xxxx xxxx 0111 xxxx */
	/* SADD8		cccc 0110 0001 xxxx xxxx xxxx 1001 xxxx */
	/* SSUB8		cccc 0110 0001 xxxx xxxx xxxx 1111 xxxx */
	/* QADD16		cccc 0110 0010 xxxx xxxx xxxx 0001 xxxx */
	/* QADDSUBX		cccc 0110 0010 xxxx xxxx xxxx 0011 xxxx */
	/* QSUBADDX		cccc 0110 0010 xxxx xxxx xxxx 0101 xxxx */
	/* QSUB16		cccc 0110 0010 xxxx xxxx xxxx 0111 xxxx */
	/* QADD8		cccc 0110 0010 xxxx xxxx xxxx 1001 xxxx */
	/* QSUB8		cccc 0110 0010 xxxx xxxx xxxx 1111 xxxx */
	/* SHADD16		cccc 0110 0011 xxxx xxxx xxxx 0001 xxxx */
	/* SHADDSUBX		cccc 0110 0011 xxxx xxxx xxxx 0011 xxxx */
	/* SHSUBADDX		cccc 0110 0011 xxxx xxxx xxxx 0101 xxxx */
	/* SHSUB16		cccc 0110 0011 xxxx xxxx xxxx 0111 xxxx */
	/* SHADD8		cccc 0110 0011 xxxx xxxx xxxx 1001 xxxx */
	/* SHSUB8		cccc 0110 0011 xxxx xxxx xxxx 1111 xxxx */
	/* UADD16		cccc 0110 0101 xxxx xxxx xxxx 0001 xxxx */
	/* UADDSUBX		cccc 0110 0101 xxxx xxxx xxxx 0011 xxxx */
	/* USUBADDX		cccc 0110 0101 xxxx xxxx xxxx 0101 xxxx */
	/* USUB16		cccc 0110 0101 xxxx xxxx xxxx 0111 xxxx */
	/* UADD8		cccc 0110 0101 xxxx xxxx xxxx 1001 xxxx */
	/* USUB8		cccc 0110 0101 xxxx xxxx xxxx 1111 xxxx */
	/* UQADD16		cccc 0110 0110 xxxx xxxx xxxx 0001 xxxx */
	/* UQADDSUBX		cccc 0110 0110 xxxx xxxx xxxx 0011 xxxx */
	/* UQSUBADDX		cccc 0110 0110 xxxx xxxx xxxx 0101 xxxx */
	/* UQSUB16		cccc 0110 0110 xxxx xxxx xxxx 0111 xxxx */
	/* UQADD8		cccc 0110 0110 xxxx xxxx xxxx 1001 xxxx */
	/* UQSUB8		cccc 0110 0110 xxxx xxxx xxxx 1111 xxxx */
	/* UHADD16		cccc 0110 0111 xxxx xxxx xxxx 0001 xxxx */
	/* UHADDSUBX		cccc 0110 0111 xxxx xxxx xxxx 0011 xxxx */
	/* UHSUBADDX		cccc 0110 0111 xxxx xxxx xxxx 0101 xxxx */
	/* UHSUB16		cccc 0110 0111 xxxx xxxx xxxx 0111 xxxx */
	/* UHADD8		cccc 0110 0111 xxxx xxxx xxxx 1001 xxxx */
	/* UHSUB8		cccc 0110 0111 xxxx xxxx xxxx 1111 xxxx */
	DECODE_EMULATEX	(0x0f800010, 0x06000010, emulate_rd12rn16rm0_rwflags_nopc,
						 REGS(NOPC, NOPC, 0, 0, NOPC)),

	/* PKHBT		cccc 0110 1000 xxxx xxxx xxxx x001 xxxx */
	/* PKHTB		cccc 0110 1000 xxxx xxxx xxxx x101 xxxx */
	DECODE_EMULATEX	(0x0ff00030, 0x06800010, emulate_rd12rn16rm0_rwflags_nopc,
						 REGS(NOPC, NOPC, 0, 0, NOPC)),

	/* ???			cccc 0110 1001 xxxx xxxx xxxx 0111 xxxx */
	/* ???			cccc 0110 1101 xxxx xxxx xxxx 0111 xxxx */
	DECODE_REJECT	(0x0fb000f0, 0x06900070),

	/* SXTB16		cccc 0110 1000 1111 xxxx xxxx 0111 xxxx */
	/* SXTB			cccc 0110 1010 1111 xxxx xxxx 0111 xxxx */
	/* SXTH			cccc 0110 1011 1111 xxxx xxxx 0111 xxxx */
	/* UXTB16		cccc 0110 1100 1111 xxxx xxxx 0111 xxxx */
	/* UXTB			cccc 0110 1110 1111 xxxx xxxx 0111 xxxx */
	/* UXTH			cccc 0110 1111 1111 xxxx xxxx 0111 xxxx */
	DECODE_EMULATEX	(0x0f8f00f0, 0x068f0070, emulate_rd12rm0_noflags_nopc,
						 REGS(0, NOPC, 0, 0, NOPC)),

	/* SXTAB16		cccc 0110 1000 xxxx xxxx xxxx 0111 xxxx */
	/* SXTAB		cccc 0110 1010 xxxx xxxx xxxx 0111 xxxx */
	/* SXTAH		cccc 0110 1011 xxxx xxxx xxxx 0111 xxxx */
	/* UXTAB16		cccc 0110 1100 xxxx xxxx xxxx 0111 xxxx */
	/* UXTAB		cccc 0110 1110 xxxx xxxx xxxx 0111 xxxx */
	/* UXTAH		cccc 0110 1111 xxxx xxxx xxxx 0111 xxxx */
	DECODE_EMULATEX	(0x0f8000f0, 0x06800070, emulate_rd12rn16rm0_rwflags_nopc,
						 REGS(NOPCX, NOPC, 0, 0, NOPC)),

	DECODE_END
};

static const union decode_item arm_cccc_0111_____xxx1_table[] = {
	/* Media instructions						*/

	/* UNDEFINED		cccc 0111 1111 xxxx xxxx xxxx 1111 xxxx */
	DECODE_REJECT	(0x0ff000f0, 0x07f000f0),

	/* SMLALD		cccc 0111 0100 xxxx xxxx xxxx 00x1 xxxx */
	/* SMLSLD		cccc 0111 0100 xxxx xxxx xxxx 01x1 xxxx */
	DECODE_EMULATEX	(0x0ff00090, 0x07400010, emulate_rdlo12rdhi16rn0rm8_rwflags_nopc,
						 REGS(NOPC, NOPC, NOPC, 0, NOPC)),

	/* SMUAD		cccc 0111 0000 xxxx 1111 xxxx 00x1 xxxx */
	/* SMUSD		cccc 0111 0000 xxxx 1111 xxxx 01x1 xxxx */
	DECODE_OR	(0x0ff0f090, 0x0700f010),
	/* SMMUL		cccc 0111 0101 xxxx 1111 xxxx 00x1 xxxx */
	DECODE_OR	(0x0ff0f0d0, 0x0750f010),
	/* USAD8		cccc 0111 1000 xxxx 1111 xxxx 0001 xxxx */
	DECODE_EMULATEX	(0x0ff0f0f0, 0x0780f010, emulate_rd16rn12rm0rs8_rwflags_nopc,
						 REGS(NOPC, 0, NOPC, 0, NOPC)),

	/* SMLAD		cccc 0111 0000 xxxx xxxx xxxx 00x1 xxxx */
	/* SMLSD		cccc 0111 0000 xxxx xxxx xxxx 01x1 xxxx */
	DECODE_OR	(0x0ff00090, 0x07000010),
	/* SMMLA		cccc 0111 0101 xxxx xxxx xxxx 00x1 xxxx */
	DECODE_OR	(0x0ff000d0, 0x07500010),
	/* USADA8		cccc 0111 1000 xxxx xxxx xxxx 0001 xxxx */
	DECODE_EMULATEX	(0x0ff000f0, 0x07800010, emulate_rd16rn12rm0rs8_rwflags_nopc,
						 REGS(NOPC, NOPCX, NOPC, 0, NOPC)),

	/* SMMLS		cccc 0111 0101 xxxx xxxx xxxx 11x1 xxxx */
	DECODE_EMULATEX	(0x0ff000d0, 0x075000d0, emulate_rd16rn12rm0rs8_rwflags_nopc,
						 REGS(NOPC, NOPC, NOPC, 0, NOPC)),

	/* SBFX			cccc 0111 101x xxxx xxxx xxxx x101 xxxx */
	/* UBFX			cccc 0111 111x xxxx xxxx xxxx x101 xxxx */
	DECODE_EMULATEX	(0x0fa00070, 0x07a00050, emulate_rd12rm0_noflags_nopc,
						 REGS(0, NOPC, 0, 0, NOPC)),

	/* BFC			cccc 0111 110x xxxx xxxx xxxx x001 1111 */
	DECODE_EMULATEX	(0x0fe0007f, 0x07c0001f, emulate_rd12rm0_noflags_nopc,
						 REGS(0, NOPC, 0, 0, 0)),

	/* BFI			cccc 0111 110x xxxx xxxx xxxx x001 xxxx */
	DECODE_EMULATEX	(0x0fe00070, 0x07c00010, emulate_rd12rm0_noflags_nopc,
						 REGS(0, NOPC, 0, 0, NOPCX)),

	DECODE_END
};

static const union decode_item arm_cccc_01xx_table[] = {
	/* Load/store word and unsigned byte				*/

	/* LDRB/STRB pc,[...]	cccc 01xx x0xx xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0x0c40f000, 0x0440f000),

	/* STR (immediate)	cccc 010x x0x0 xxxx xxxx xxxx xxxx xxxx */
	/* STRB (immediate)	cccc 010x x1x0 xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0x0e100000, 0x04000000, emulate_str,
						 REGS(NOPCWB, ANY, 0, 0, 0)),

	/* LDR (immediate)	cccc 010x x0x1 xxxx xxxx xxxx xxxx xxxx */
	/* LDRB (immediate)	cccc 010x x1x1 xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0x0e100000, 0x04100000, emulate_ldr,
						 REGS(NOPCWB, ANY, 0, 0, 0)),

	/* STR (register)	cccc 011x x0x0 xxxx xxxx xxxx xxxx xxxx */
	/* STRB (register)	cccc 011x x1x0 xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0x0e100000, 0x06000000, emulate_str,
						 REGS(NOPCWB, ANY, 0, 0, NOPC)),

	/* LDR (register)	cccc 011x x0x1 xxxx xxxx xxxx xxxx xxxx */
	/* LDRB (register)	cccc 011x x1x1 xxxx xxxx xxxx xxxx xxxx */
	DECODE_EMULATEX	(0x0e100000, 0x06100000, emulate_ldr,
						 REGS(NOPCWB, ANY, 0, 0, NOPC)),

	DECODE_END
};

static const union decode_item arm_cccc_100x_table[] = {
	/* Block data transfer instructions				*/

	/* LDM			cccc 100x x0x1 xxxx xxxx xxxx xxxx xxxx */
	/* STM			cccc 100x x0x0 xxxx xxxx xxxx xxxx xxxx */
	DECODE_CUSTOM	(0x0e400000, 0x08000000, kprobe_decode_ldmstm),

	/* STM (user registers)	cccc 100x x1x0 xxxx xxxx xxxx xxxx xxxx */
	/* LDM (user registers)	cccc 100x x1x1 xxxx 0xxx xxxx xxxx xxxx */
	/* LDM (exception ret)	cccc 100x x1x1 xxxx 1xxx xxxx xxxx xxxx */
	DECODE_END
};

const union decode_item kprobe_decode_arm_table[] = {
	/*
	 * Unconditional instructions
	 *			1111 xxxx xxxx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xf0000000, 0xf0000000, arm_1111_table),

	/*
	 * Miscellaneous instructions
	 *			cccc 0001 0xx0 xxxx xxxx xxxx 0xxx xxxx
	 */
	DECODE_TABLE	(0x0f900080, 0x01000000, arm_cccc_0001_0xx0____0xxx_table),

	/*
	 * Halfword multiply and multiply-accumulate
	 *			cccc 0001 0xx0 xxxx xxxx xxxx 1xx0 xxxx
	 */
	DECODE_TABLE	(0x0f900090, 0x01000080, arm_cccc_0001_0xx0____1xx0_table),

	/*
	 * Multiply and multiply-accumulate
	 *			cccc 0000 xxxx xxxx xxxx xxxx 1001 xxxx
	 */
	DECODE_TABLE	(0x0f0000f0, 0x00000090, arm_cccc_0000_____1001_table),

	/*
	 * Synchronization primitives
	 *			cccc 0001 xxxx xxxx xxxx xxxx 1001 xxxx
	 */
	DECODE_TABLE	(0x0f0000f0, 0x01000090, arm_cccc_0001_____1001_table),

	/*
	 * Extra load/store instructions
	 *			cccc 000x xxxx xxxx xxxx xxxx 1xx1 xxxx
	 */
	DECODE_TABLE	(0x0e000090, 0x00000090, arm_cccc_000x_____1xx1_table),

	/*
	 * Data-processing (register)
	 *			cccc 000x xxxx xxxx xxxx xxxx xxx0 xxxx
	 * Data-processing (register-shifted register)
	 *			cccc 000x xxxx xxxx xxxx xxxx 0xx1 xxxx
	 */
	DECODE_TABLE	(0x0e000000, 0x00000000, arm_cccc_000x_table),

	/*
	 * Data-processing (immediate)
	 *			cccc 001x xxxx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0x0e000000, 0x02000000, arm_cccc_001x_table),

	/*
	 * Media instructions
	 *			cccc 011x xxxx xxxx xxxx xxxx xxx1 xxxx
	 */
	DECODE_TABLE	(0x0f000010, 0x06000010, arm_cccc_0110_____xxx1_table),
	DECODE_TABLE	(0x0f000010, 0x07000010, arm_cccc_0111_____xxx1_table),

	/*
	 * Load/store word and unsigned byte
	 *			cccc 01xx xxxx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0x0c000000, 0x04000000, arm_cccc_01xx_table),

	/*
	 * Block data transfer instructions
	 *			cccc 100x xxxx xxxx xxxx xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0x0e000000, 0x08000000, arm_cccc_100x_table),

	/* B			cccc 1010 xxxx xxxx xxxx xxxx xxxx xxxx */
	/* BL			cccc 1011 xxxx xxxx xxxx xxxx xxxx xxxx */
	DECODE_SIMULATE	(0x0e000000, 0x0a000000, simulate_bbl),

	/*
	 * Supervisor Call, and coprocessor instructions
	 */

	/* MCRR			cccc 1100 0100 xxxx xxxx xxxx xxxx xxxx */
	/* MRRC			cccc 1100 0101 xxxx xxxx xxxx xxxx xxxx */
	/* LDC			cccc 110x xxx1 xxxx xxxx xxxx xxxx xxxx */
	/* STC			cccc 110x xxx0 xxxx xxxx xxxx xxxx xxxx */
	/* CDP			cccc 1110 xxxx xxxx xxxx xxxx xxx0 xxxx */
	/* MCR			cccc 1110 xxx0 xxxx xxxx xxxx xxx1 xxxx */
	/* MRC			cccc 1110 xxx1 xxxx xxxx xxxx xxx1 xxxx */
	/* SVC			cccc 1111 xxxx xxxx xxxx xxxx xxxx xxxx */
	DECODE_REJECT	(0x0c000000, 0x0c000000),

	DECODE_END
};

static void __kprobes arm_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->ARM_pc += 4;
	p->ainsn.insn_handler(p, regs);
}

/* Return:
 *   INSN_REJECTED     If instruction is one not allowed to kprobe,
 *   INSN_GOOD         If instruction is supported and uses instruction slot,
 *   INSN_GOOD_NO_SLOT If instruction is supported but doesn't use its slot.
 *
 * For instructions we don't want to kprobe (INSN_REJECTED return result):
 *   These are generally ones that modify the processor state making
 *   them "hard" to simulate such as switches processor modes or
 *   make accesses in alternate modes.  Any of these could be simulated
 *   if the work was put into it, but low return considering they
 *   should also be very rare.
 */
enum kprobe_insn __kprobes
arm_kprobe_decode_insn(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	asi->insn_singlestep = arm_singlestep;
	asi->insn_check_cc = kprobe_condition_checks[insn>>28];
	return kprobe_decode_insn(insn, asi, kprobe_decode_arm_table, false);
}
