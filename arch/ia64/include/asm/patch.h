/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_PATCH_H
#define _ASM_IA64_PATCH_H

/*
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * There are a number of reasons for patching instructions.  Rather than duplicating code
 * all over the place, we put the common stuff here.  Reasons for patching: in-kernel
 * module-loader, virtual-to-physical patch-list, McKinley Errata 9 workaround, and gate
 * shared library.  Undoubtedly, some of these reasons will disappear and others will
 * be added over time.
 */
#include <linux/elf.h>
#include <linux/types.h>

extern void ia64_patch (u64 insn_addr, u64 mask, u64 val);	/* patch any insn slot */
extern void ia64_patch_imm64 (u64 insn_addr, u64 val);		/* patch "movl" w/abs. value*/
extern void ia64_patch_imm60 (u64 insn_addr, u64 val);		/* patch "brl" w/ip-rel value */

extern void ia64_patch_mckinley_e9 (unsigned long start, unsigned long end);
extern void ia64_patch_vtop (unsigned long start, unsigned long end);
extern void ia64_patch_phys_stack_reg(unsigned long val);
extern void ia64_patch_rse (unsigned long start, unsigned long end);
extern void ia64_patch_gate (void);

#endif /* _ASM_IA64_PATCH_H */
