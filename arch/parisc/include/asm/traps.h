/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_TRAPS_H
#define __ASM_TRAPS_H

#define PARISC_ITLB_TRAP	6 /* defined by architecture. Do not change. */

#if !defined(__ASSEMBLER__)
struct pt_regs;

/* traps.c */
void parisc_terminate(char *msg, struct pt_regs *regs,
		int code, unsigned long offset) __noreturn __cold;

void die_if_kernel(char *str, struct pt_regs *regs, long err);

/* mm/fault.c */
unsigned long parisc_acctyp(unsigned long code, unsigned int inst);
const char *trap_name(unsigned long code);
void do_page_fault(struct pt_regs *regs, unsigned long code,
		unsigned long address);
int handle_nadtlb_fault(struct pt_regs *regs);
#endif

#endif
