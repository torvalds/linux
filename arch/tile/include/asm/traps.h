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

/* kernel/traps.c */
void do_trap(struct pt_regs *, int fault_num, unsigned long reason);

/* kernel/time.c */
void do_timer_interrupt(struct pt_regs *, int fault_num);

/* kernel/messaging.c */
void hv_message_intr(struct pt_regs *, int intnum);

/* kernel/irq.c */
void tile_dev_intr(struct pt_regs *, int intnum);



#endif /* _ASM_TILE_SYSCALLS_H */
