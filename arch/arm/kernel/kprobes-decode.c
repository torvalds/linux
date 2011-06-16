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

#define sign_extend(x, signbit) ((x) | (0 - ((x) & (1 << (signbit)))))

#define branch_displacement(insn) sign_extend(((insn) & 0xffffff) << 2, 25)

#define is_r15(insn, bitpos) (((insn) & (0xf << bitpos)) == (0xf << bitpos))

/*
 * Test if load/store instructions writeback the address register.
 * if P (bit 24) == 0 or W (bit 21) == 1
 */
#define is_writeback(insn) ((insn ^ 0x01000000) & 0x01200000)

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
 * For STR and STM instructions, an ARM core may choose to use either
 * a +8 or a +12 displacement from the current instruction's address.
 * Whichever value is chosen for a given core, it must be the same for
 * both instructions and may not change.  This function measures it.
 */

static int str_pc_offset;

static void __init find_str_pc_offset(void)
{
	int addr, scratch, ret;

	__asm__ (
		"sub	%[ret], pc, #4		\n\t"
		"str	pc, %[addr]		\n\t"
		"ldr	%[scr], %[addr]		\n\t"
		"sub	%[ret], %[scr], %[ret]	\n\t"
		: [ret] "=r" (ret), [scr] "=r" (scratch), [addr] "+m" (addr));

	str_pc_offset = ret;
}

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

static void __kprobes simulate_ldm1stm1(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = p->opcode;
	int rn = (insn >> 16) & 0xf;
	int lbit = insn & (1 << 20);
	int wbit = insn & (1 << 21);
	int ubit = insn & (1 << 23);
	int pbit = insn & (1 << 24);
	long *addr = (long *)regs->uregs[rn];
	int reg_bit_vector;
	int reg_count;

	reg_count = 0;
	reg_bit_vector = insn & 0xffff;
	while (reg_bit_vector) {
		reg_bit_vector &= (reg_bit_vector - 1);
		++reg_count;
	}

	if (!ubit)
		addr -= reg_count;
	addr += (!pbit == !ubit);

	reg_bit_vector = insn & 0xffff;
	while (reg_bit_vector) {
		int reg = __ffs(reg_bit_vector);
		reg_bit_vector &= (reg_bit_vector - 1);
		if (lbit)
			regs->uregs[reg] = *addr++;
		else
			*addr++ = regs->uregs[reg];
	}

	if (wbit) {
		if (!ubit)
			addr -= reg_count;
		addr -= (!pbit == !ubit);
		regs->uregs[rn] = (long)addr;
	}
}

static void __kprobes simulate_stm1_pc(struct kprobe *p, struct pt_regs *regs)
{
	regs->ARM_pc = (long)p->addr + str_pc_offset;
	simulate_ldm1stm1(p, regs);
	regs->ARM_pc = (long)p->addr + 4;
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

static void __kprobes emulate_ldr(struct kprobe *p, struct pt_regs *regs)
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

static void __kprobes emulate_str(struct kprobe *p, struct pt_regs *regs)
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
	asi->insn_handler = (insn & (1 << 20)) ? emulate_ldr : emulate_str;
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

/*
 * For the instruction masking and comparisons in all the "space_*"
 * functions below, Do _not_ rearrange the order of tests unless
 * you're very, very sure of what you are doing.  For the sake of
 * efficiency, the masks for some tests sometimes assume other test
 * have been done prior to them so the number of patterns to test
 * for an instruction set can be as broad as possible to reduce the
 * number of tests needed.
 */

static enum kprobe_insn __kprobes
space_1111(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/* memory hint : 1111 0100 x001 xxxx xxxx xxxx xxxx xxxx : */
	/* PLDI        : 1111 0100 x101 xxxx xxxx xxxx xxxx xxxx : */
	/* PLDW        : 1111 0101 x001 xxxx xxxx xxxx xxxx xxxx : */
	/* PLD         : 1111 0101 x101 xxxx xxxx xxxx xxxx xxxx : */
	if ((insn & 0xfe300000) == 0xf4100000) {
		asi->insn_handler = emulate_nop;
		return INSN_GOOD_NO_SLOT;
	}

	/* BLX(1) : 1111 101x xxxx xxxx xxxx xxxx xxxx xxxx : */
	if ((insn & 0xfe000000) == 0xfa000000) {
		asi->insn_handler = simulate_blx1;
		return INSN_GOOD_NO_SLOT;
	}

	/* CPS   : 1111 0001 0000 xxx0 xxxx xxxx xx0x xxxx */
	/* SETEND: 1111 0001 0000 0001 xxxx xxxx 0000 xxxx */

	/* SRS   : 1111 100x x1x0 xxxx xxxx xxxx xxxx xxxx */
	/* RFE   : 1111 100x x0x1 xxxx xxxx xxxx xxxx xxxx */

	/* Coprocessor instructions... */
	/* MCRR2 : 1111 1100 0100 xxxx xxxx xxxx xxxx xxxx : (Rd != Rn) */
	/* MRRC2 : 1111 1100 0101 xxxx xxxx xxxx xxxx xxxx : (Rd != Rn) */
	/* LDC2  : 1111 110x xxx1 xxxx xxxx xxxx xxxx xxxx */
	/* STC2  : 1111 110x xxx0 xxxx xxxx xxxx xxxx xxxx */
	/* CDP2  : 1111 1110 xxxx xxxx xxxx xxxx xxx0 xxxx */
	/* MCR2  : 1111 1110 xxx0 xxxx xxxx xxxx xxx1 xxxx */
	/* MRC2  : 1111 1110 xxx1 xxxx xxxx xxxx xxx1 xxxx */

	return INSN_REJECTED;
}

static enum kprobe_insn __kprobes
space_cccc_000x(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/* cccc 0001 0xx0 xxxx xxxx xxxx xxxx xxx0 xxxx */
	if ((insn & 0x0f900010) == 0x01000000) {

		/* MRS cpsr : cccc 0001 0000 xxxx xxxx xxxx 0000 xxxx */
		if ((insn & 0x0ff000f0) == 0x01000000) {
			if (is_r15(insn, 12))
				return INSN_REJECTED;	/* Rd is PC */
			asi->insn_handler = simulate_mrs;
			return INSN_GOOD_NO_SLOT;
		}

		/* SMLALxy : cccc 0001 0100 xxxx xxxx xxxx 1xx0 xxxx */
		if ((insn & 0x0ff00090) == 0x01400080)
			return prep_emulate_rdhi16rdlo12rs8rm0_wflags(insn,
									asi);

		/* SMULWy : cccc 0001 0010 xxxx xxxx xxxx 1x10 xxxx */
		/* SMULxy : cccc 0001 0110 xxxx xxxx xxxx 1xx0 xxxx */
		if ((insn & 0x0ff000b0) == 0x012000a0 ||
		    (insn & 0x0ff00090) == 0x01600080)
			return prep_emulate_rd16rs8rm0_wflags(insn, asi);

		/* SMLAxy : cccc 0001 0000 xxxx xxxx xxxx 1xx0 xxxx : Q */
		/* SMLAWy : cccc 0001 0010 xxxx xxxx xxxx 1x00 xxxx : Q */
		if ((insn & 0x0ff00090) == 0x01000080 ||
		    (insn & 0x0ff000b0) == 0x01200080)
			return prep_emulate_rd16rn12rs8rm0_wflags(insn, asi);

		/* BXJ      : cccc 0001 0010 xxxx xxxx xxxx 0010 xxxx */
		/* MSR      : cccc 0001 0x10 xxxx xxxx xxxx 0000 xxxx */
		/* MRS spsr : cccc 0001 0100 xxxx xxxx xxxx 0000 xxxx */

		/* Other instruction encodings aren't yet defined */
		return INSN_REJECTED;
	}

	/* cccc 0001 0xx0 xxxx xxxx xxxx xxxx 0xx1 xxxx */
	else if ((insn & 0x0f900090) == 0x01000010) {

		/* BLX(2) : cccc 0001 0010 xxxx xxxx xxxx 0011 xxxx */
		/* BX     : cccc 0001 0010 xxxx xxxx xxxx 0001 xxxx */
		if ((insn & 0x0ff000d0) == 0x01200010) {
			if ((insn & 0x0ff000ff) == 0x0120003f)
				return INSN_REJECTED; /* BLX pc */
			asi->insn_handler = simulate_blx2bx;
			return INSN_GOOD_NO_SLOT;
		}

		/* CLZ : cccc 0001 0110 xxxx xxxx xxxx 0001 xxxx */
		if ((insn & 0x0ff000f0) == 0x01600010)
			return prep_emulate_rd12rm0(insn, asi);

		/* QADD    : cccc 0001 0000 xxxx xxxx xxxx 0101 xxxx :Q */
		/* QSUB    : cccc 0001 0010 xxxx xxxx xxxx 0101 xxxx :Q */
		/* QDADD   : cccc 0001 0100 xxxx xxxx xxxx 0101 xxxx :Q */
		/* QDSUB   : cccc 0001 0110 xxxx xxxx xxxx 0101 xxxx :Q */
		if ((insn & 0x0f9000f0) == 0x01000050)
			return prep_emulate_rd12rn16rm0_wflags(insn, asi);

		/* BKPT : 1110 0001 0010 xxxx xxxx xxxx 0111 xxxx */
		/* SMC  : cccc 0001 0110 xxxx xxxx xxxx 0111 xxxx */

		/* Other instruction encodings aren't yet defined */
		return INSN_REJECTED;
	}

	/* cccc 0000 xxxx xxxx xxxx xxxx xxxx 1001 xxxx */
	else if ((insn & 0x0f0000f0) == 0x00000090) {

		/* MUL    : cccc 0000 0000 xxxx xxxx xxxx 1001 xxxx :   */
		/* MULS   : cccc 0000 0001 xxxx xxxx xxxx 1001 xxxx :cc */
		/* MLA    : cccc 0000 0010 xxxx xxxx xxxx 1001 xxxx :   */
		/* MLAS   : cccc 0000 0011 xxxx xxxx xxxx 1001 xxxx :cc */
		/* UMAAL  : cccc 0000 0100 xxxx xxxx xxxx 1001 xxxx :   */
		/* undef  : cccc 0000 0101 xxxx xxxx xxxx 1001 xxxx :   */
		/* MLS    : cccc 0000 0110 xxxx xxxx xxxx 1001 xxxx :   */
		/* undef  : cccc 0000 0111 xxxx xxxx xxxx 1001 xxxx :   */
		/* UMULL  : cccc 0000 1000 xxxx xxxx xxxx 1001 xxxx :   */
		/* UMULLS : cccc 0000 1001 xxxx xxxx xxxx 1001 xxxx :cc */
		/* UMLAL  : cccc 0000 1010 xxxx xxxx xxxx 1001 xxxx :   */
		/* UMLALS : cccc 0000 1011 xxxx xxxx xxxx 1001 xxxx :cc */
		/* SMULL  : cccc 0000 1100 xxxx xxxx xxxx 1001 xxxx :   */
		/* SMULLS : cccc 0000 1101 xxxx xxxx xxxx 1001 xxxx :cc */
		/* SMLAL  : cccc 0000 1110 xxxx xxxx xxxx 1001 xxxx :   */
		/* SMLALS : cccc 0000 1111 xxxx xxxx xxxx 1001 xxxx :cc */
		if ((insn & 0x00d00000) == 0x00500000)
			return INSN_REJECTED;
		else if ((insn & 0x00e00000) == 0x00000000)
			return prep_emulate_rd16rs8rm0_wflags(insn, asi);
		else if ((insn & 0x00a00000) == 0x00200000)
			return prep_emulate_rd16rn12rs8rm0_wflags(insn, asi);
		else
			return prep_emulate_rdhi16rdlo12rs8rm0_wflags(insn,
									asi);
	}

	/* cccc 000x xxxx xxxx xxxx xxxx xxxx 1xx1 xxxx */
	else if ((insn & 0x0e000090) == 0x00000090) {

		/* SWP   : cccc 0001 0000 xxxx xxxx xxxx 1001 xxxx */
		/* SWPB  : cccc 0001 0100 xxxx xxxx xxxx 1001 xxxx */
		/* ???   : cccc 0001 0x01 xxxx xxxx xxxx 1001 xxxx */
		/* ???   : cccc 0001 0x10 xxxx xxxx xxxx 1001 xxxx */
		/* ???   : cccc 0001 0x11 xxxx xxxx xxxx 1001 xxxx */
		/* STREX : cccc 0001 1000 xxxx xxxx xxxx 1001 xxxx */
		/* LDREX : cccc 0001 1001 xxxx xxxx xxxx 1001 xxxx */
		/* STREXD: cccc 0001 1010 xxxx xxxx xxxx 1001 xxxx */
		/* LDREXD: cccc 0001 1011 xxxx xxxx xxxx 1001 xxxx */
		/* STREXB: cccc 0001 1100 xxxx xxxx xxxx 1001 xxxx */
		/* LDREXB: cccc 0001 1101 xxxx xxxx xxxx 1001 xxxx */
		/* STREXH: cccc 0001 1110 xxxx xxxx xxxx 1001 xxxx */
		/* LDREXH: cccc 0001 1111 xxxx xxxx xxxx 1001 xxxx */

		/* LDRD  : cccc 000x xxx0 xxxx xxxx xxxx 1101 xxxx */
		/* STRD  : cccc 000x xxx0 xxxx xxxx xxxx 1111 xxxx */
		/* LDRH  : cccc 000x xxx1 xxxx xxxx xxxx 1011 xxxx */
		/* STRH  : cccc 000x xxx0 xxxx xxxx xxxx 1011 xxxx */
		/* LDRSB : cccc 000x xxx1 xxxx xxxx xxxx 1101 xxxx */
		/* LDRSH : cccc 000x xxx1 xxxx xxxx xxxx 1111 xxxx */
		if ((insn & 0x0f0000f0) == 0x01000090) {
			if ((insn & 0x0fb000f0) == 0x01000090) {
				/* SWP/SWPB */
				return prep_emulate_rd12rn16rm0_wflags(insn,
									asi);
			} else {
				/* STREX/LDREX variants and unallocaed space */
				return INSN_REJECTED;
			}

		} else if ((insn & 0x0e1000d0) == 0x00000d0) {
			/* STRD/LDRD */
			if ((insn & 0x0000e000) == 0x0000e000)
				return INSN_REJECTED;	/* Rd is LR or PC */
			if (is_writeback(insn) && is_r15(insn, 16))
				return INSN_REJECTED;	/* Writeback to PC */

			insn &= 0xfff00fff;
			insn |= 0x00002000;	/* Rn = r0, Rd = r2 */
			if (!(insn & (1 << 22))) {
				/* Register index */
				insn &= ~0xf;
				insn |= 1;	/* Rm = r1 */
			}
			asi->insn[0] = insn;
			asi->insn_handler =
				(insn & (1 << 5)) ? emulate_strd : emulate_ldrd;
			return INSN_GOOD;
		}

		/* LDRH/STRH/LDRSB/LDRSH */
		if (is_r15(insn, 12))
			return INSN_REJECTED;	/* Rd is PC */
		return prep_emulate_ldr_str(insn, asi);
	}

	/* cccc 000x xxxx xxxx xxxx xxxx xxxx xxxx xxxx */

	/*
	 * ALU op with S bit and Rd == 15 :
	 *	cccc 000x xxx1 xxxx 1111 xxxx xxxx xxxx
	 */
	if ((insn & 0x0e10f000) == 0x0010f000)
		return INSN_REJECTED;

	/*
	 * "mov ip, sp" is the most common kprobe'd instruction by far.
	 * Check and optimize for it explicitly.
	 */
	if (insn == 0xe1a0c00d) {
		asi->insn_handler = simulate_mov_ipsp;
		return INSN_GOOD_NO_SLOT;
	}

	/*
	 * Data processing: Immediate-shift / Register-shift
	 * ALU op : cccc 000x xxxx xxxx xxxx xxxx xxxx xxxx
	 * CPY    : cccc 0001 1010 xxxx xxxx 0000 0000 xxxx
	 * MOV    : cccc 0001 101x xxxx xxxx xxxx xxxx xxxx
	 * *S (bit 20) updates condition codes
	 * ADC/SBC/RSC reads the C flag
	 */
	insn &= 0xfff00ff0;	/* Rn = r0, Rd = r0 */
	insn |= 0x00000001;	/* Rm = r1 */
	if (insn & 0x010) {
		insn &= 0xfffff0ff;     /* register shift */
		insn |= 0x00000200;     /* Rs = r2 */
	}
	asi->insn[0] = insn;

	if ((insn & 0x0f900000) == 0x01100000) {
		/*
		 * TST : cccc 0001 0001 xxxx xxxx xxxx xxxx xxxx
		 * TEQ : cccc 0001 0011 xxxx xxxx xxxx xxxx xxxx
		 * CMP : cccc 0001 0101 xxxx xxxx xxxx xxxx xxxx
		 * CMN : cccc 0001 0111 xxxx xxxx xxxx xxxx xxxx
		 */
		asi->insn_handler = emulate_alu_tests;
	} else {
		/* ALU ops which write to Rd */
		asi->insn_handler = (insn & (1 << 20)) ?  /* S-bit */
				emulate_alu_rwflags : emulate_alu_rflags;
	}
	return INSN_GOOD;
}

static enum kprobe_insn __kprobes
space_cccc_001x(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/* MOVW  : cccc 0011 0000 xxxx xxxx xxxx xxxx xxxx */
	/* MOVT  : cccc 0011 0100 xxxx xxxx xxxx xxxx xxxx */
	if ((insn & 0x0fb00000) == 0x03000000)
		return prep_emulate_rd12_modify(insn, asi);

	/* hints : cccc 0011 0010 0000 xxxx xxxx xxxx xxxx */
	if ((insn & 0x0fff0000) == 0x03200000) {
		unsigned op2 = insn & 0x000000ff;
		if (op2 == 0x01 || op2 == 0x04) {
			/* YIELD : cccc 0011 0010 0000 xxxx xxxx 0000 0001 */
			/* SEV   : cccc 0011 0010 0000 xxxx xxxx 0000 0100 */
			asi->insn[0] = insn;
			asi->insn_handler = emulate_none;
			return INSN_GOOD;
		} else if (op2 <= 0x03) {
			/* NOP   : cccc 0011 0010 0000 xxxx xxxx 0000 0000 */
			/* WFE   : cccc 0011 0010 0000 xxxx xxxx 0000 0010 */
			/* WFI   : cccc 0011 0010 0000 xxxx xxxx 0000 0011 */
			/*
			 * We make WFE and WFI true NOPs to avoid stalls due
			 * to missing events whilst processing the probe.
			 */
			asi->insn_handler = emulate_nop;
			return INSN_GOOD_NO_SLOT;
		}
		/* For DBG and unallocated hints it's safest to reject them */
		return INSN_REJECTED;
	}

	/*
	 * MSR   : cccc 0011 0x10 xxxx xxxx xxxx xxxx xxxx
	 * ALU op with S bit and Rd == 15 :
	 *	   cccc 001x xxx1 xxxx 1111 xxxx xxxx xxxx
	 */
	if ((insn & 0x0fb00000) == 0x03200000 ||	/* MSR */
	    (insn & 0x0e10f000) == 0x0210f000)		/* ALU s-bit, R15  */
		return INSN_REJECTED;

	/*
	 * Data processing: 32-bit Immediate
	 * ALU op : cccc 001x xxxx xxxx xxxx xxxx xxxx xxxx
	 * MOV    : cccc 0011 101x xxxx xxxx xxxx xxxx xxxx
	 * *S (bit 20) updates condition codes
	 * ADC/SBC/RSC reads the C flag
	 */
	insn &= 0xfff00fff;	/* Rn = r0 and Rd = r0 */
	asi->insn[0] = insn;

	if ((insn & 0x0f900000) == 0x03100000) {
		/*
		 * TST : cccc 0011 0001 xxxx xxxx xxxx xxxx xxxx
		 * TEQ : cccc 0011 0011 xxxx xxxx xxxx xxxx xxxx
		 * CMP : cccc 0011 0101 xxxx xxxx xxxx xxxx xxxx
		 * CMN : cccc 0011 0111 xxxx xxxx xxxx xxxx xxxx
		 */
		asi->insn_handler = emulate_alu_tests_imm;
	} else {
		/* ALU ops which write to Rd */
		asi->insn_handler = (insn & (1 << 20)) ?  /* S-bit */
			emulate_alu_imm_rwflags : emulate_alu_imm_rflags;
	}
	return INSN_GOOD;
}

static enum kprobe_insn __kprobes
space_cccc_0110__1(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/* SEL : cccc 0110 1000 xxxx xxxx xxxx 1011 xxxx GE: !!! */
	if ((insn & 0x0ff000f0) == 0x068000b0) {
		if (is_r15(insn, 12))
			return INSN_REJECTED;	/* Rd is PC */
		insn &= 0xfff00ff0;	/* Rd = r0, Rn = r0 */
		insn |= 0x00000001;	/* Rm = r1 */
		asi->insn[0] = insn;
		asi->insn_handler = emulate_sel;
		return INSN_GOOD;
	}

	/* SSAT   : cccc 0110 101x xxxx xxxx xxxx xx01 xxxx :Q */
	/* USAT   : cccc 0110 111x xxxx xxxx xxxx xx01 xxxx :Q */
	/* SSAT16 : cccc 0110 1010 xxxx xxxx xxxx 0011 xxxx :Q */
	/* USAT16 : cccc 0110 1110 xxxx xxxx xxxx 0011 xxxx :Q */
	if ((insn & 0x0fa00030) == 0x06a00010 ||
	    (insn & 0x0fb000f0) == 0x06a00030) {
		if (is_r15(insn, 12))
			return INSN_REJECTED;	/* Rd is PC */
		insn &= 0xffff0ff0;	/* Rd = r0, Rm = r0 */
		asi->insn[0] = insn;
		asi->insn_handler = emulate_sat;
		return INSN_GOOD;
	}

	/* REV    : cccc 0110 1011 xxxx xxxx xxxx 0011 xxxx */
	/* REV16  : cccc 0110 1011 xxxx xxxx xxxx 1011 xxxx */
	/* RBIT   : cccc 0110 1111 xxxx xxxx xxxx 0011 xxxx */
	/* REVSH  : cccc 0110 1111 xxxx xxxx xxxx 1011 xxxx */
	if ((insn & 0x0ff00070) == 0x06b00030 ||
	    (insn & 0x0ff00070) == 0x06f00030)
		return prep_emulate_rd12rm0(insn, asi);

	/* ???       : cccc 0110 0000 xxxx xxxx xxxx xxx1 xxxx :   */
	/* SADD16    : cccc 0110 0001 xxxx xxxx xxxx 0001 xxxx :GE */
	/* SADDSUBX  : cccc 0110 0001 xxxx xxxx xxxx 0011 xxxx :GE */
	/* SSUBADDX  : cccc 0110 0001 xxxx xxxx xxxx 0101 xxxx :GE */
	/* SSUB16    : cccc 0110 0001 xxxx xxxx xxxx 0111 xxxx :GE */
	/* SADD8     : cccc 0110 0001 xxxx xxxx xxxx 1001 xxxx :GE */
	/* ???       : cccc 0110 0001 xxxx xxxx xxxx 1011 xxxx :   */
	/* ???       : cccc 0110 0001 xxxx xxxx xxxx 1101 xxxx :   */
	/* SSUB8     : cccc 0110 0001 xxxx xxxx xxxx 1111 xxxx :GE */
	/* QADD16    : cccc 0110 0010 xxxx xxxx xxxx 0001 xxxx :   */
	/* QADDSUBX  : cccc 0110 0010 xxxx xxxx xxxx 0011 xxxx :   */
	/* QSUBADDX  : cccc 0110 0010 xxxx xxxx xxxx 0101 xxxx :   */
	/* QSUB16    : cccc 0110 0010 xxxx xxxx xxxx 0111 xxxx :   */
	/* QADD8     : cccc 0110 0010 xxxx xxxx xxxx 1001 xxxx :   */
	/* ???       : cccc 0110 0010 xxxx xxxx xxxx 1011 xxxx :   */
	/* ???       : cccc 0110 0010 xxxx xxxx xxxx 1101 xxxx :   */
	/* QSUB8     : cccc 0110 0010 xxxx xxxx xxxx 1111 xxxx :   */
	/* SHADD16   : cccc 0110 0011 xxxx xxxx xxxx 0001 xxxx :   */
	/* SHADDSUBX : cccc 0110 0011 xxxx xxxx xxxx 0011 xxxx :   */
	/* SHSUBADDX : cccc 0110 0011 xxxx xxxx xxxx 0101 xxxx :   */
	/* SHSUB16   : cccc 0110 0011 xxxx xxxx xxxx 0111 xxxx :   */
	/* SHADD8    : cccc 0110 0011 xxxx xxxx xxxx 1001 xxxx :   */
	/* ???       : cccc 0110 0011 xxxx xxxx xxxx 1011 xxxx :   */
	/* ???       : cccc 0110 0011 xxxx xxxx xxxx 1101 xxxx :   */
	/* SHSUB8    : cccc 0110 0011 xxxx xxxx xxxx 1111 xxxx :   */
	/* ???       : cccc 0110 0100 xxxx xxxx xxxx xxx1 xxxx :   */
	/* UADD16    : cccc 0110 0101 xxxx xxxx xxxx 0001 xxxx :GE */
	/* UADDSUBX  : cccc 0110 0101 xxxx xxxx xxxx 0011 xxxx :GE */
	/* USUBADDX  : cccc 0110 0101 xxxx xxxx xxxx 0101 xxxx :GE */
	/* USUB16    : cccc 0110 0101 xxxx xxxx xxxx 0111 xxxx :GE */
	/* UADD8     : cccc 0110 0101 xxxx xxxx xxxx 1001 xxxx :GE */
	/* ???       : cccc 0110 0101 xxxx xxxx xxxx 1011 xxxx :   */
	/* ???       : cccc 0110 0101 xxxx xxxx xxxx 1101 xxxx :   */
	/* USUB8     : cccc 0110 0101 xxxx xxxx xxxx 1111 xxxx :GE */
	/* UQADD16   : cccc 0110 0110 xxxx xxxx xxxx 0001 xxxx :   */
	/* UQADDSUBX : cccc 0110 0110 xxxx xxxx xxxx 0011 xxxx :   */
	/* UQSUBADDX : cccc 0110 0110 xxxx xxxx xxxx 0101 xxxx :   */
	/* UQSUB16   : cccc 0110 0110 xxxx xxxx xxxx 0111 xxxx :   */
	/* UQADD8    : cccc 0110 0110 xxxx xxxx xxxx 1001 xxxx :   */
	/* ???       : cccc 0110 0110 xxxx xxxx xxxx 1011 xxxx :   */
	/* ???       : cccc 0110 0110 xxxx xxxx xxxx 1101 xxxx :   */
	/* UQSUB8    : cccc 0110 0110 xxxx xxxx xxxx 1111 xxxx :   */
	/* UHADD16   : cccc 0110 0111 xxxx xxxx xxxx 0001 xxxx :   */
	/* UHADDSUBX : cccc 0110 0111 xxxx xxxx xxxx 0011 xxxx :   */
	/* UHSUBADDX : cccc 0110 0111 xxxx xxxx xxxx 0101 xxxx :   */
	/* UHSUB16   : cccc 0110 0111 xxxx xxxx xxxx 0111 xxxx :   */
	/* UHADD8    : cccc 0110 0111 xxxx xxxx xxxx 1001 xxxx :   */
	/* ???       : cccc 0110 0111 xxxx xxxx xxxx 1011 xxxx :   */
	/* ???       : cccc 0110 0111 xxxx xxxx xxxx 1101 xxxx :   */
	/* UHSUB8    : cccc 0110 0111 xxxx xxxx xxxx 1111 xxxx :   */
	if ((insn & 0x0f800010) == 0x06000010) {
		if ((insn & 0x00300000) == 0x00000000 ||
		    (insn & 0x000000e0) == 0x000000a0 ||
		    (insn & 0x000000e0) == 0x000000c0)
			return INSN_REJECTED;	/* Unallocated space */
		return prep_emulate_rd12rn16rm0_wflags(insn, asi);
	}

	/* PKHBT     : cccc 0110 1000 xxxx xxxx xxxx x001 xxxx :   */
	/* PKHTB     : cccc 0110 1000 xxxx xxxx xxxx x101 xxxx :   */
	if ((insn & 0x0ff00030) == 0x06800010)
		return prep_emulate_rd12rn16rm0_wflags(insn, asi);

	/* SXTAB16   : cccc 0110 1000 xxxx xxxx xxxx 0111 xxxx :   */
	/* SXTB16    : cccc 0110 1000 1111 xxxx xxxx 0111 xxxx :   */
	/* ???       : cccc 0110 1001 xxxx xxxx xxxx 0111 xxxx :   */
	/* SXTAB     : cccc 0110 1010 xxxx xxxx xxxx 0111 xxxx :   */
	/* SXTB      : cccc 0110 1010 1111 xxxx xxxx 0111 xxxx :   */
	/* SXTAH     : cccc 0110 1011 xxxx xxxx xxxx 0111 xxxx :   */
	/* SXTH      : cccc 0110 1011 1111 xxxx xxxx 0111 xxxx :   */
	/* UXTAB16   : cccc 0110 1100 xxxx xxxx xxxx 0111 xxxx :   */
	/* UXTB16    : cccc 0110 1100 1111 xxxx xxxx 0111 xxxx :   */
	/* ???       : cccc 0110 1101 xxxx xxxx xxxx 0111 xxxx :   */
	/* UXTAB     : cccc 0110 1110 xxxx xxxx xxxx 0111 xxxx :   */
	/* UXTB      : cccc 0110 1110 1111 xxxx xxxx 0111 xxxx :   */
	/* UXTAH     : cccc 0110 1111 xxxx xxxx xxxx 0111 xxxx :   */
	/* UXTH      : cccc 0110 1111 1111 xxxx xxxx 0111 xxxx :   */
	if ((insn & 0x0f8000f0) == 0x06800070) {
		if ((insn & 0x00300000) == 0x00100000)
			return INSN_REJECTED;	/* Unallocated space */

		if ((insn & 0x000f0000) == 0x000f0000)
			return prep_emulate_rd12rm0(insn, asi);
		else
			return prep_emulate_rd12rn16rm0_wflags(insn, asi);
	}

	/* Other instruction encodings aren't yet defined */
	return INSN_REJECTED;
}

static enum kprobe_insn __kprobes
space_cccc_0111__1(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/* Undef : cccc 0111 1111 xxxx xxxx xxxx 1111 xxxx */
	if ((insn & 0x0ff000f0) == 0x03f000f0)
		return INSN_REJECTED;

	/* SMLALD : cccc 0111 0100 xxxx xxxx xxxx 00x1 xxxx */
	/* SMLSLD : cccc 0111 0100 xxxx xxxx xxxx 01x1 xxxx */
	if ((insn & 0x0ff00090) == 0x07400010)
		return prep_emulate_rdhi16rdlo12rs8rm0_wflags(insn, asi);

	/* SMLAD  : cccc 0111 0000 xxxx xxxx xxxx 00x1 xxxx :Q */
	/* SMUAD  : cccc 0111 0000 xxxx 1111 xxxx 00x1 xxxx :Q */
	/* SMLSD  : cccc 0111 0000 xxxx xxxx xxxx 01x1 xxxx :Q */
	/* SMUSD  : cccc 0111 0000 xxxx 1111 xxxx 01x1 xxxx :  */
	/* SMMLA  : cccc 0111 0101 xxxx xxxx xxxx 00x1 xxxx :  */
	/* SMMUL  : cccc 0111 0101 xxxx 1111 xxxx 00x1 xxxx :  */
	/* USADA8 : cccc 0111 1000 xxxx xxxx xxxx 0001 xxxx :  */
	/* USAD8  : cccc 0111 1000 xxxx 1111 xxxx 0001 xxxx :  */
	if ((insn & 0x0ff00090) == 0x07000010 ||
	    (insn & 0x0ff000d0) == 0x07500010 ||
	    (insn & 0x0ff000f0) == 0x07800010) {

		if ((insn & 0x0000f000) == 0x0000f000)
			return prep_emulate_rd16rs8rm0_wflags(insn, asi);
		else
			return prep_emulate_rd16rn12rs8rm0_wflags(insn, asi);
	}

	/* SMMLS  : cccc 0111 0101 xxxx xxxx xxxx 11x1 xxxx :  */
	if ((insn & 0x0ff000d0) == 0x075000d0)
		return prep_emulate_rd16rn12rs8rm0_wflags(insn, asi);

	/* SBFX   : cccc 0111 101x xxxx xxxx xxxx x101 xxxx :  */
	/* UBFX   : cccc 0111 111x xxxx xxxx xxxx x101 xxxx :  */
	if ((insn & 0x0fa00070) == 0x07a00050)
		return prep_emulate_rd12rm0(insn, asi);

	/* BFI    : cccc 0111 110x xxxx xxxx xxxx x001 xxxx :  */
	/* BFC    : cccc 0111 110x xxxx xxxx xxxx x001 1111 :  */
	if ((insn & 0x0fe00070) == 0x07c00010) {

		if ((insn & 0x0000000f) == 0x0000000f)
			return prep_emulate_rd12_modify(insn, asi);
		else
			return prep_emulate_rd12rn0_modify(insn, asi);
	}

	return INSN_REJECTED;
}

static enum kprobe_insn __kprobes
space_cccc_01xx(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/* LDR   : cccc 01xx x0x1 xxxx xxxx xxxx xxxx xxxx */
	/* LDRB  : cccc 01xx x1x1 xxxx xxxx xxxx xxxx xxxx */
	/* LDRBT : cccc 01x0 x111 xxxx xxxx xxxx xxxx xxxx */
	/* LDRT  : cccc 01x0 x011 xxxx xxxx xxxx xxxx xxxx */
	/* STR   : cccc 01xx x0x0 xxxx xxxx xxxx xxxx xxxx */
	/* STRB  : cccc 01xx x1x0 xxxx xxxx xxxx xxxx xxxx */
	/* STRBT : cccc 01x0 x110 xxxx xxxx xxxx xxxx xxxx */
	/* STRT  : cccc 01x0 x010 xxxx xxxx xxxx xxxx xxxx */

	if ((insn & 0x00500000) == 0x00500000 && is_r15(insn, 12))
		return INSN_REJECTED;	/* LDRB into PC */

	return prep_emulate_ldr_str(insn, asi);
}

static enum kprobe_insn __kprobes
space_cccc_100x(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/* LDM(2) : cccc 100x x101 xxxx 0xxx xxxx xxxx xxxx */
	/* LDM(3) : cccc 100x x1x1 xxxx 1xxx xxxx xxxx xxxx */
	if ((insn & 0x0e708000) == 0x85000000 ||
	    (insn & 0x0e508000) == 0x85010000)
		return INSN_REJECTED;

	/* LDM(1) : cccc 100x x0x1 xxxx xxxx xxxx xxxx xxxx */
	/* STM(1) : cccc 100x x0x0 xxxx xxxx xxxx xxxx xxxx */
	asi->insn_handler = ((insn & 0x108000) == 0x008000) ? /* STM & R15 */
				simulate_stm1_pc : simulate_ldm1stm1;
	return INSN_GOOD_NO_SLOT;
}

static enum kprobe_insn __kprobes
space_cccc_101x(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/* B  : cccc 1010 xxxx xxxx xxxx xxxx xxxx xxxx */
	/* BL : cccc 1011 xxxx xxxx xxxx xxxx xxxx xxxx */
	asi->insn_handler = simulate_bbl;
	return INSN_GOOD_NO_SLOT;
}

static enum kprobe_insn __kprobes
space_cccc_11xx(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	/* Coprocessor instructions... */
	/* MCRR : cccc 1100 0100 xxxx xxxx xxxx xxxx xxxx : (Rd!=Rn) */
	/* MRRC : cccc 1100 0101 xxxx xxxx xxxx xxxx xxxx : (Rd!=Rn) */
	/* LDC  : cccc 110x xxx1 xxxx xxxx xxxx xxxx xxxx */
	/* STC  : cccc 110x xxx0 xxxx xxxx xxxx xxxx xxxx */
	/* CDP  : cccc 1110 xxxx xxxx xxxx xxxx xxx0 xxxx */
	/* MCR  : cccc 1110 xxx0 xxxx xxxx xxxx xxx1 xxxx */
	/* MRC  : cccc 1110 xxx1 xxxx xxxx xxxx xxx1 xxxx */

	/* SVC  : cccc 1111 xxxx xxxx xxxx xxxx xxxx xxxx */

	return INSN_REJECTED;
}

static unsigned long __kprobes __check_eq(unsigned long cpsr)
{
	return cpsr & PSR_Z_BIT;
}

static unsigned long __kprobes __check_ne(unsigned long cpsr)
{
	return (~cpsr) & PSR_Z_BIT;
}

static unsigned long __kprobes __check_cs(unsigned long cpsr)
{
	return cpsr & PSR_C_BIT;
}

static unsigned long __kprobes __check_cc(unsigned long cpsr)
{
	return (~cpsr) & PSR_C_BIT;
}

static unsigned long __kprobes __check_mi(unsigned long cpsr)
{
	return cpsr & PSR_N_BIT;
}

static unsigned long __kprobes __check_pl(unsigned long cpsr)
{
	return (~cpsr) & PSR_N_BIT;
}

static unsigned long __kprobes __check_vs(unsigned long cpsr)
{
	return cpsr & PSR_V_BIT;
}

static unsigned long __kprobes __check_vc(unsigned long cpsr)
{
	return (~cpsr) & PSR_V_BIT;
}

static unsigned long __kprobes __check_hi(unsigned long cpsr)
{
	cpsr &= ~(cpsr >> 1); /* PSR_C_BIT &= ~PSR_Z_BIT */
	return cpsr & PSR_C_BIT;
}

static unsigned long __kprobes __check_ls(unsigned long cpsr)
{
	cpsr &= ~(cpsr >> 1); /* PSR_C_BIT &= ~PSR_Z_BIT */
	return (~cpsr) & PSR_C_BIT;
}

static unsigned long __kprobes __check_ge(unsigned long cpsr)
{
	cpsr ^= (cpsr << 3); /* PSR_N_BIT ^= PSR_V_BIT */
	return (~cpsr) & PSR_N_BIT;
}

static unsigned long __kprobes __check_lt(unsigned long cpsr)
{
	cpsr ^= (cpsr << 3); /* PSR_N_BIT ^= PSR_V_BIT */
	return cpsr & PSR_N_BIT;
}

static unsigned long __kprobes __check_gt(unsigned long cpsr)
{
	unsigned long temp = cpsr ^ (cpsr << 3); /* PSR_N_BIT ^= PSR_V_BIT */
	temp |= (cpsr << 1);			 /* PSR_N_BIT |= PSR_Z_BIT */
	return (~temp) & PSR_N_BIT;
}

static unsigned long __kprobes __check_le(unsigned long cpsr)
{
	unsigned long temp = cpsr ^ (cpsr << 3); /* PSR_N_BIT ^= PSR_V_BIT */
	temp |= (cpsr << 1);			 /* PSR_N_BIT |= PSR_Z_BIT */
	return temp & PSR_N_BIT;
}

static unsigned long __kprobes __check_al(unsigned long cpsr)
{
	return true;
}

static kprobe_check_cc * const condition_checks[16] = {
	&__check_eq, &__check_ne, &__check_cs, &__check_cc,
	&__check_mi, &__check_pl, &__check_vs, &__check_vc,
	&__check_hi, &__check_ls, &__check_ge, &__check_lt,
	&__check_gt, &__check_le, &__check_al, &__check_al
};

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
	asi->insn_check_cc = condition_checks[insn>>28];
	asi->insn[1] = KPROBE_RETURN_INSTRUCTION;

	if ((insn & 0xf0000000) == 0xf0000000)

		return space_1111(insn, asi);

	else if ((insn & 0x0e000000) == 0x00000000)

		return space_cccc_000x(insn, asi);

	else if ((insn & 0x0e000000) == 0x02000000)

		return space_cccc_001x(insn, asi);

	else if ((insn & 0x0f000010) == 0x06000010)

		return space_cccc_0110__1(insn, asi);

	else if ((insn & 0x0f000010) == 0x07000010)

		return space_cccc_0111__1(insn, asi);

	else if ((insn & 0x0c000000) == 0x04000000)

		return space_cccc_01xx(insn, asi);

	else if ((insn & 0x0e000000) == 0x08000000)

		return space_cccc_100x(insn, asi);

	else if ((insn & 0x0e000000) == 0x0a000000)

		return space_cccc_101x(insn, asi);

	return space_cccc_11xx(insn, asi);
}

void __init arm_kprobe_decode_init(void)
{
	find_str_pc_offset();
}
