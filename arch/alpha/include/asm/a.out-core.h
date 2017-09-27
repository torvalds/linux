/* a.out coredump register dumper
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_A_OUT_CORE_H
#define _ASM_A_OUT_CORE_H

#ifdef __KERNEL__

#include <linux/user.h>
#include <linux/mm_types.h>

/*
 * Fill in the user structure for an ECOFF core dump.
 */
static inline void aout_dump_thread(struct pt_regs *pt, struct user *dump)
{
	/* switch stack follows right below pt_regs: */
	struct switch_stack * sw = ((struct switch_stack *) pt) - 1;

	dump->magic = CMAGIC;
	dump->start_code  = current->mm->start_code;
	dump->start_data  = current->mm->start_data;
	dump->start_stack = rdusp() & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((current->mm->end_code - dump->start_code)
			 >> PAGE_SHIFT);
	dump->u_dsize = ((current->mm->brk + PAGE_SIZE-1 - dump->start_data)
			 >> PAGE_SHIFT);
	dump->u_ssize = (current->mm->start_stack - dump->start_stack
			 + PAGE_SIZE-1) >> PAGE_SHIFT;

	/*
	 * We store the registers in an order/format that is
	 * compatible with DEC Unix/OSF/1 as this makes life easier
	 * for gdb.
	 */
	dump->regs[EF_V0]  = pt->r0;
	dump->regs[EF_T0]  = pt->r1;
	dump->regs[EF_T1]  = pt->r2;
	dump->regs[EF_T2]  = pt->r3;
	dump->regs[EF_T3]  = pt->r4;
	dump->regs[EF_T4]  = pt->r5;
	dump->regs[EF_T5]  = pt->r6;
	dump->regs[EF_T6]  = pt->r7;
	dump->regs[EF_T7]  = pt->r8;
	dump->regs[EF_S0]  = sw->r9;
	dump->regs[EF_S1]  = sw->r10;
	dump->regs[EF_S2]  = sw->r11;
	dump->regs[EF_S3]  = sw->r12;
	dump->regs[EF_S4]  = sw->r13;
	dump->regs[EF_S5]  = sw->r14;
	dump->regs[EF_S6]  = sw->r15;
	dump->regs[EF_A3]  = pt->r19;
	dump->regs[EF_A4]  = pt->r20;
	dump->regs[EF_A5]  = pt->r21;
	dump->regs[EF_T8]  = pt->r22;
	dump->regs[EF_T9]  = pt->r23;
	dump->regs[EF_T10] = pt->r24;
	dump->regs[EF_T11] = pt->r25;
	dump->regs[EF_RA]  = pt->r26;
	dump->regs[EF_T12] = pt->r27;
	dump->regs[EF_AT]  = pt->r28;
	dump->regs[EF_SP]  = rdusp();
	dump->regs[EF_PS]  = pt->ps;
	dump->regs[EF_PC]  = pt->pc;
	dump->regs[EF_GP]  = pt->gp;
	dump->regs[EF_A0]  = pt->r16;
	dump->regs[EF_A1]  = pt->r17;
	dump->regs[EF_A2]  = pt->r18;
	memcpy((char *)dump->regs + EF_SIZE, sw->fp, 32 * 8);
}

#endif /* __KERNEL__ */
#endif /* _ASM_A_OUT_CORE_H */
