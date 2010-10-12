/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_TRAPS_H
#define _ASM_TILE_TRAPS_H

/* mm/fault.c */
void do_page_fault(struct pt_regs *, int fault_num,
		   unsigned long address, unsigned long write);
void do_async_page_fault(struct pt_regs *);

#ifndef __tilegx__
/*
 * We return this structure in registers to avoid having to write
 * additional save/restore code in the intvec.S caller.
 */
struct intvec_state {
	void *handler;
	unsigned long vecnum;
	unsigned long fault_num;
	unsigned long info;
	unsigned long retval;
};
struct intvec_state do_page_fault_ics(struct pt_regs *regs, int fault_num,
				      unsigned long address,
				      unsigned long info);
#endif

/* kernel/traps.c */
void do_trap(struct pt_regs *, int fault_num, unsigned long reason);
void kernel_double_fault(int dummy, ulong pc, ulong lr, ulong sp, ulong r52);

/* kernel/time.c */
void do_timer_interrupt(struct pt_regs *, int fault_num);

/* kernel/messaging.c */
void hv_message_intr(struct pt_regs *, int intnum);

/* kernel/irq.c */
void tile_dev_intr(struct pt_regs *, int intnum);

#ifdef CONFIG_HARDWALL
/* kernel/hardwall.c */
void do_hardwall_trap(struct pt_regs *, int fault_num);
#endif

/* kernel/ptrace.c */
void do_breakpoint(struct pt_regs *, int fault_num);


#endif /* _ASM_TILE_SYSCALLS_H */
