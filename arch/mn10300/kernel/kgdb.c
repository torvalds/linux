/* kgdb support for MN10300
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/kgdb.h>
#include <linux/uaccess.h>
#include <unit/leds.h>
#include <unit/serial.h>
#include <asm/debugger.h>
#include <asm/serial-regs.h>
#include "internal.h"

/*
 * Software single-stepping breakpoint save (used by __switch_to())
 */
static struct thread_info *kgdb_sstep_thread;
u8 *kgdb_sstep_bp_addr[2];
u8 kgdb_sstep_bp[2];

/*
 * Copy kernel exception frame registers to the GDB register file
 */
void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	unsigned long ssp = (unsigned long) (regs + 1);

	gdb_regs[GDB_FR_D0]	= regs->d0;
	gdb_regs[GDB_FR_D1]	= regs->d1;
	gdb_regs[GDB_FR_D2]	= regs->d2;
	gdb_regs[GDB_FR_D3]	= regs->d3;
	gdb_regs[GDB_FR_A0]	= regs->a0;
	gdb_regs[GDB_FR_A1]	= regs->a1;
	gdb_regs[GDB_FR_A2]	= regs->a2;
	gdb_regs[GDB_FR_A3]	= regs->a3;
	gdb_regs[GDB_FR_SP]	= (regs->epsw & EPSW_nSL) ? regs->sp : ssp;
	gdb_regs[GDB_FR_PC]	= regs->pc;
	gdb_regs[GDB_FR_MDR]	= regs->mdr;
	gdb_regs[GDB_FR_EPSW]	= regs->epsw;
	gdb_regs[GDB_FR_LIR]	= regs->lir;
	gdb_regs[GDB_FR_LAR]	= regs->lar;
	gdb_regs[GDB_FR_MDRQ]	= regs->mdrq;
	gdb_regs[GDB_FR_E0]	= regs->e0;
	gdb_regs[GDB_FR_E1]	= regs->e1;
	gdb_regs[GDB_FR_E2]	= regs->e2;
	gdb_regs[GDB_FR_E3]	= regs->e3;
	gdb_regs[GDB_FR_E4]	= regs->e4;
	gdb_regs[GDB_FR_E5]	= regs->e5;
	gdb_regs[GDB_FR_E6]	= regs->e6;
	gdb_regs[GDB_FR_E7]	= regs->e7;
	gdb_regs[GDB_FR_SSP]	= ssp;
	gdb_regs[GDB_FR_MSP]	= 0;
	gdb_regs[GDB_FR_USP]	= regs->sp;
	gdb_regs[GDB_FR_MCRH]	= regs->mcrh;
	gdb_regs[GDB_FR_MCRL]	= regs->mcrl;
	gdb_regs[GDB_FR_MCVF]	= regs->mcvf;
	gdb_regs[GDB_FR_DUMMY0]	= 0;
	gdb_regs[GDB_FR_DUMMY1]	= 0;
	gdb_regs[GDB_FR_FS0]	= 0;
}

/*
 * Extracts kernel SP/PC values understandable by gdb from the values
 * saved by switch_to().
 */
void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	gdb_regs[GDB_FR_SSP]	= p->thread.sp;
	gdb_regs[GDB_FR_PC]	= p->thread.pc;
	gdb_regs[GDB_FR_A3]	= p->thread.a3;
	gdb_regs[GDB_FR_USP]	= p->thread.usp;
	gdb_regs[GDB_FR_FPCR]	= p->thread.fpu_state.fpcr;
}

/*
 * Fill kernel exception frame registers from the GDB register file
 */
void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	regs->d0	= gdb_regs[GDB_FR_D0];
	regs->d1	= gdb_regs[GDB_FR_D1];
	regs->d2	= gdb_regs[GDB_FR_D2];
	regs->d3	= gdb_regs[GDB_FR_D3];
	regs->a0	= gdb_regs[GDB_FR_A0];
	regs->a1	= gdb_regs[GDB_FR_A1];
	regs->a2	= gdb_regs[GDB_FR_A2];
	regs->a3	= gdb_regs[GDB_FR_A3];
	regs->sp	= gdb_regs[GDB_FR_SP];
	regs->pc	= gdb_regs[GDB_FR_PC];
	regs->mdr	= gdb_regs[GDB_FR_MDR];
	regs->epsw	= gdb_regs[GDB_FR_EPSW];
	regs->lir	= gdb_regs[GDB_FR_LIR];
	regs->lar	= gdb_regs[GDB_FR_LAR];
	regs->mdrq	= gdb_regs[GDB_FR_MDRQ];
	regs->e0	= gdb_regs[GDB_FR_E0];
	regs->e1	= gdb_regs[GDB_FR_E1];
	regs->e2	= gdb_regs[GDB_FR_E2];
	regs->e3	= gdb_regs[GDB_FR_E3];
	regs->e4	= gdb_regs[GDB_FR_E4];
	regs->e5	= gdb_regs[GDB_FR_E5];
	regs->e6	= gdb_regs[GDB_FR_E6];
	regs->e7	= gdb_regs[GDB_FR_E7];
	regs->sp	= gdb_regs[GDB_FR_SSP];
	/* gdb_regs[GDB_FR_MSP]; */
	// regs->usp	= gdb_regs[GDB_FR_USP];
	regs->mcrh	= gdb_regs[GDB_FR_MCRH];
	regs->mcrl	= gdb_regs[GDB_FR_MCRL];
	regs->mcvf	= gdb_regs[GDB_FR_MCVF];
	/* gdb_regs[GDB_FR_DUMMY0]; */
	/* gdb_regs[GDB_FR_DUMMY1]; */

	// regs->fpcr	= gdb_regs[GDB_FR_FPCR];
	// regs->fs0	= gdb_regs[GDB_FR_FS0];
}

struct kgdb_arch arch_kgdb_ops = {
	.gdb_bpt_instr	= { 0xff },
	.flags		= KGDB_HW_BREAKPOINT,
};

static const unsigned char mn10300_kgdb_insn_sizes[256] =
{
	/* 1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
	1, 3, 3, 3, 1, 3, 3, 3, 1, 3, 3, 3, 1, 3, 3, 3,	/* 0 */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 1 */
	2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, /* 2 */
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 1, 1, /* 3 */
	1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, /* 4 */
	1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, /* 5 */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 6 */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 7 */
	2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, /* 8 */
	2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, /* 9 */
	2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, /* a */
	2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, /* b */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 2, /* c */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* d */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* e */
	0, 2, 2, 2, 2, 2, 2, 4, 0, 3, 0, 4, 0, 6, 7, 1  /* f */
};

/*
 * Attempt to emulate single stepping by means of breakpoint instructions.
 * Although there is a single-step trace flag in EPSW, its use is not
 * sufficiently documented and is only intended for use with the JTAG debugger.
 */
static int kgdb_arch_do_singlestep(struct pt_regs *regs)
{
	unsigned long arg;
	unsigned size;
	u8 *pc = (u8 *)regs->pc, *sp = (u8 *)(regs + 1), cur;
	u8 *x = NULL, *y = NULL;
	int ret;

	ret = probe_kernel_read(&cur, pc, 1);
	if (ret < 0)
		return ret;

	size = mn10300_kgdb_insn_sizes[cur];
	if (size > 0) {
		x = pc + size;
		goto set_x;
	}

	switch (cur) {
		/* Bxx (d8,PC) */
	case 0xc0 ... 0xca:
		ret = probe_kernel_read(&arg, pc + 1, 1);
		if (ret < 0)
			return ret;
		x = pc + 2;
		if (arg >= 0 && arg <= 2)
			goto set_x;
		y = pc + (s8)arg;
		goto set_x_and_y;

		/* LXX (d8,PC) */
	case 0xd0 ... 0xda:
		x = pc + 1;
		if (regs->pc == regs->lar)
			goto set_x;
		y = (u8 *)regs->lar;
		goto set_x_and_y;

		/* SETLB - loads the next four bytes into the LIR register
		 * (which mustn't include a breakpoint instruction) */
	case 0xdb:
		x = pc + 5;
		goto set_x;

		/* JMP (d16,PC) or CALL (d16,PC) */
	case 0xcc:
	case 0xcd:
		ret = probe_kernel_read(&arg, pc + 1, 2);
		if (ret < 0)
			return ret;
		x = pc + (s16)arg;
		goto set_x;

		/* JMP (d32,PC) or CALL (d32,PC) */
	case 0xdc:
	case 0xdd:
		ret = probe_kernel_read(&arg, pc + 1, 4);
		if (ret < 0)
			return ret;
		x = pc + (s32)arg;
		goto set_x;

		/* RETF */
	case 0xde:
		x = (u8 *)regs->mdr;
		goto set_x;

		/* RET */
	case 0xdf:
		ret = probe_kernel_read(&arg, pc + 2, 1);
		if (ret < 0)
			return ret;
		ret = probe_kernel_read(&x, sp + (s8)arg, 4);
		if (ret < 0)
			return ret;
		goto set_x;

	case 0xf0:
		ret = probe_kernel_read(&cur, pc + 1, 1);
		if (ret < 0)
			return ret;

		if (cur >= 0xf0 && cur <= 0xf7) {
			/* JMP (An) / CALLS (An) */
			switch (cur & 3) {
			case 0: x = (u8 *)regs->a0; break;
			case 1: x = (u8 *)regs->a1; break;
			case 2: x = (u8 *)regs->a2; break;
			case 3: x = (u8 *)regs->a3; break;
			}
			goto set_x;
		} else if (cur == 0xfc) {
			/* RETS */
			ret = probe_kernel_read(&x, sp, 4);
			if (ret < 0)
				return ret;
			goto set_x;
		} else if (cur == 0xfd) {
			/* RTI */
			ret = probe_kernel_read(&x, sp + 4, 4);
			if (ret < 0)
				return ret;
			goto set_x;
		} else {
			x = pc + 2;
			goto set_x;
		}
		break;

		/* potential 3-byte conditional branches */
	case 0xf8:
		ret = probe_kernel_read(&cur, pc + 1, 1);
		if (ret < 0)
			return ret;
		x = pc + 3;

		if (cur >= 0xe8 && cur <= 0xeb) {
			ret = probe_kernel_read(&arg, pc + 2, 1);
			if (ret < 0)
				return ret;
			if (arg >= 0 && arg <= 3)
				goto set_x;
			y = pc + (s8)arg;
			goto set_x_and_y;
		}
		goto set_x;

	case 0xfa:
		ret = probe_kernel_read(&cur, pc + 1, 1);
		if (ret < 0)
			return ret;

		if (cur == 0xff) {
			/* CALLS (d16,PC) */
			ret = probe_kernel_read(&arg, pc + 2, 2);
			if (ret < 0)
				return ret;
			x = pc + (s16)arg;
			goto set_x;
		}

		x = pc + 4;
		goto set_x;

	case 0xfc:
		ret = probe_kernel_read(&cur, pc + 1, 1);
		if (ret < 0)
			return ret;

		if (cur == 0xff) {
			/* CALLS (d32,PC) */
			ret = probe_kernel_read(&arg, pc + 2, 4);
			if (ret < 0)
				return ret;
			x = pc + (s32)arg;
			goto set_x;
		}

		x = pc + 6;
		goto set_x;
	}

	return 0;

set_x:
	kgdb_sstep_bp_addr[0] = x;
	kgdb_sstep_bp_addr[1] = NULL;
	ret = probe_kernel_read(&kgdb_sstep_bp[0], x, 1);
	if (ret < 0)
		return ret;
	ret = probe_kernel_write(x, &arch_kgdb_ops.gdb_bpt_instr, 1);
	if (ret < 0)
		return ret;
	kgdb_sstep_thread = current_thread_info();
	debugger_local_cache_flushinv_one(x);
	return ret;

set_x_and_y:
	kgdb_sstep_bp_addr[0] = x;
	kgdb_sstep_bp_addr[1] = y;
	ret = probe_kernel_read(&kgdb_sstep_bp[0], x, 1);
	if (ret < 0)
		return ret;
	ret = probe_kernel_read(&kgdb_sstep_bp[1], y, 1);
	if (ret < 0)
		return ret;
	ret = probe_kernel_write(x, &arch_kgdb_ops.gdb_bpt_instr, 1);
	if (ret < 0)
		return ret;
	ret = probe_kernel_write(y, &arch_kgdb_ops.gdb_bpt_instr, 1);
	if (ret < 0) {
		probe_kernel_write(kgdb_sstep_bp_addr[0],
				   &kgdb_sstep_bp[0], 1);
	} else {
		kgdb_sstep_thread = current_thread_info();
	}
	debugger_local_cache_flushinv_one(x);
	debugger_local_cache_flushinv_one(y);
	return ret;
}

/*
 * Remove emplaced single-step breakpoints, returning true if we hit one of
 * them.
 */
static bool kgdb_arch_undo_singlestep(struct pt_regs *regs)
{
	bool hit = false;
	u8 *x = kgdb_sstep_bp_addr[0], *y = kgdb_sstep_bp_addr[1];
	u8 opcode;

	if (kgdb_sstep_thread == current_thread_info()) {
		if (x) {
			if (x == (u8 *)regs->pc)
				hit = true;
			if (probe_kernel_read(&opcode, x,
					      1) < 0 ||
			    opcode != 0xff)
				BUG();
			probe_kernel_write(x, &kgdb_sstep_bp[0], 1);
			debugger_local_cache_flushinv_one(x);
		}
		if (y) {
			if (y == (u8 *)regs->pc)
				hit = true;
			if (probe_kernel_read(&opcode, y,
					      1) < 0 ||
			    opcode != 0xff)
				BUG();
			probe_kernel_write(y, &kgdb_sstep_bp[1], 1);
			debugger_local_cache_flushinv_one(y);
		}
	}

	kgdb_sstep_bp_addr[0] = NULL;
	kgdb_sstep_bp_addr[1] = NULL;
	kgdb_sstep_thread = NULL;
	return hit;
}

/*
 * Catch a single-step-pending thread being deleted and make sure the global
 * single-step state is cleared.  At this point the breakpoints should have
 * been removed by __switch_to().
 */
void arch_release_thread_info(struct thread_info *ti)
{
	if (kgdb_sstep_thread == ti) {
		kgdb_sstep_thread = NULL;

		/* However, we may now be running in degraded mode, with most
		 * of the CPUs disabled until such a time as KGDB is reentered,
		 * so force immediate reentry */
		kgdb_breakpoint();
	}
}

/*
 * Handle unknown packets and [CcsDk] packets
 * - at this point breakpoints have been installed
 */
int kgdb_arch_handle_exception(int vector, int signo, int err_code,
			       char *remcom_in_buffer, char *remcom_out_buffer,
			       struct pt_regs *regs)
{
	long addr;
	char *ptr;

	switch (remcom_in_buffer[0]) {
	case 'c':
	case 's':
		/* try to read optional parameter, pc unchanged if no parm */
		ptr = &remcom_in_buffer[1];
		if (kgdb_hex2long(&ptr, &addr))
			regs->pc = addr;
	case 'D':
	case 'k':
		atomic_set(&kgdb_cpu_doing_single_step, -1);

		if (remcom_in_buffer[0] == 's') {
			kgdb_arch_do_singlestep(regs);
			kgdb_single_step = 1;
			atomic_set(&kgdb_cpu_doing_single_step,
				   raw_smp_processor_id());
		}
		return 0;
	}
	return -1; /* this means that we do not want to exit from the handler */
}

/*
 * Handle event interception
 * - returns 0 if the exception should be skipped, -ERROR otherwise.
 */
int debugger_intercept(enum exception_code excep, int signo, int si_code,
		       struct pt_regs *regs)
{
	int ret;

	if (kgdb_arch_undo_singlestep(regs)) {
		excep = EXCEP_TRAP;
		signo = SIGTRAP;
		si_code = TRAP_TRACE;
	}

	ret = kgdb_handle_exception(excep, signo, si_code, regs);

	debugger_local_cache_flushinv();

	return ret;
}

/*
 * Determine if we've hit a debugger special breakpoint
 */
int at_debugger_breakpoint(struct pt_regs *regs)
{
	return regs->pc == (unsigned long)&__arch_kgdb_breakpoint;
}

/*
 * Initialise kgdb
 */
int kgdb_arch_init(void)
{
	return 0;
}

/*
 * Do something, perhaps, but don't know what.
 */
void kgdb_arch_exit(void)
{
}

#ifdef CONFIG_SMP
void debugger_nmi_interrupt(struct pt_regs *regs, enum exception_code code)
{
	kgdb_nmicallback(arch_smp_processor_id(), regs);
	debugger_local_cache_flushinv();
}

void kgdb_roundup_cpus(unsigned long flags)
{
	smp_jump_to_debugger();
}
#endif
