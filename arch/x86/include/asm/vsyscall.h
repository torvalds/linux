/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_VSYSCALL_H
#define _ASM_X86_VSYSCALL_H

#include <linux/seqlock.h>
#include <uapi/asm/vsyscall.h>
#include <asm/page_types.h>

#ifdef CONFIG_X86_VSYSCALL_EMULATION
extern void map_vsyscall(void);
extern void set_vsyscall_pgtable_user_bits(pgd_t *root);

/*
 * Called on instruction fetch fault in vsyscall page.
 * Returns true if handled.
 */
extern bool emulate_vsyscall(unsigned long error_code,
			     struct pt_regs *regs, unsigned long address);
#else
static inline void map_vsyscall(void) {}
static inline bool emulate_vsyscall(unsigned long error_code,
				    struct pt_regs *regs, unsigned long address)
{
	return false;
}
#endif

/*
 * The (legacy) vsyscall page is the long page in the kernel portion
 * of the address space that has user-accessible permissions.
 */
static inline bool is_vsyscall_vaddr(unsigned long vaddr)
{
	return unlikely((vaddr & PAGE_MASK) == VSYSCALL_ADDR);
}

#endif /* _ASM_X86_VSYSCALL_H */
