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

#include <linux/ptrace.h>
#include <linux/kgdb.h>
#include <linux/uaccess.h>
#include <unit/leds.h>
#include <unit/serial.h>
#include <asm/debugger.h>
#include <asm/serial-regs.h>
#include "internal.h"

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

/*
 * Handle unknown packets and [Ccs] packets
 */
int kgdb_arch_handle_exception(int vector, int signo, int err_code,
			       char *remcom_in_buffer, char *remcom_out_buffer,
			       struct pt_regs *regs)
{
	long addr;
	char *ptr;

	switch (remcom_in_buffer[0]) {
	case 'c':
		if (kgdb_contthread && kgdb_contthread != current) {
			strcpy(remcom_out_buffer, "E00");
			break;
		}

		kgdb_contthread = NULL;

		/* try to read optional parameter, pc unchanged if no parm */
		ptr = &remcom_in_buffer[1];
		if (kgdb_hex2long(&ptr, &addr))
			regs->pc = addr;
		return 0;

	case 's':
		break; /* we don't do hardware single stepping */
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
