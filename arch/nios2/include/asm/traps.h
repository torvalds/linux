/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_TRAPS_H
#define _ASM_NIOS2_TRAPS_H

#define TRAP_ID_SYSCALL		0

#ifndef __ASSEMBLY__
void _exception(int signo, struct pt_regs *regs, int code, unsigned long addr);
void do_page_fault(struct pt_regs *regs, unsigned long cause,
		   unsigned long address);
#endif

#endif /* _ASM_NIOS2_TRAPS_H */
