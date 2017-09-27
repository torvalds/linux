#ifndef _ASM_X86_KAISER_H
#define _ASM_X86_KAISER_H

#include <uapi/asm/processor-flags.h> /* For PCID constants */

/*
 * This file includes the definitions for the KAISER feature.
 * KAISER is a counter measure against x86_64 side channel attacks on
 * the kernel virtual memory.  It has a shadow pgd for every process: the
 * shadow pgd has a minimalistic kernel-set mapped, but includes the whole
 * user memory. Within a kernel context switch, or when an interrupt is handled,
 * the pgd is switched to the normal one. When the system switches to user mode,
 * the shadow pgd is enabled. By this, the virtual memory caches are freed,
 * and the user may not attack the whole kernel memory.
 *
 * A minimalistic kernel mapping holds the parts needed to be mapped in user
 * mode, such as the entry/exit functions of the user space, or the stacks.
 */

#define KAISER_SHADOW_PGD_OFFSET 0x1000

#ifdef __ASSEMBLY__
#ifdef CONFIG_KAISER

.macro _SWITCH_TO_KERNEL_CR3 reg
movq %cr3, \reg
andq $(~(X86_CR3_PCID_ASID_MASK | KAISER_SHADOW_PGD_OFFSET)), \reg
orq  x86_cr3_pcid_noflush, \reg
movq \reg, %cr3
.endm

.macro _SWITCH_TO_USER_CR3 reg regb
/*
 * regb must be the low byte portion of reg: because we have arranged
 * for the low byte of the user PCID to serve as the high byte of NOFLUSH
 * (0x80 for each when PCID is enabled, or 0x00 when PCID and NOFLUSH are
 * not enabled): so that the one register can update both memory and cr3.
 */
movq %cr3, \reg
orq  PER_CPU_VAR(x86_cr3_pcid_user), \reg
js   9f
/* FLUSH this time, reset to NOFLUSH for next time (if PCID enabled) */
movb \regb, PER_CPU_VAR(x86_cr3_pcid_user+7)
9:
movq \reg, %cr3
.endm

.macro SWITCH_KERNEL_CR3
pushq %rax
_SWITCH_TO_KERNEL_CR3 %rax
popq %rax
.endm

.macro SWITCH_USER_CR3
pushq %rax
_SWITCH_TO_USER_CR3 %rax %al
popq %rax
.endm

.macro SWITCH_KERNEL_CR3_NO_STACK
movq %rax, PER_CPU_VAR(unsafe_stack_register_backup)
_SWITCH_TO_KERNEL_CR3 %rax
movq PER_CPU_VAR(unsafe_stack_register_backup), %rax
.endm

#else /* CONFIG_KAISER */

.macro SWITCH_KERNEL_CR3 reg
.endm
.macro SWITCH_USER_CR3 reg regb
.endm
.macro SWITCH_KERNEL_CR3_NO_STACK
.endm

#endif /* CONFIG_KAISER */

#else /* __ASSEMBLY__ */

#ifdef CONFIG_KAISER
/*
 * Upon kernel/user mode switch, it may happen that the address
 * space has to be switched before the registers have been
 * stored.  To change the address space, another register is
 * needed.  A register therefore has to be stored/restored.
*/
DECLARE_PER_CPU_USER_MAPPED(unsigned long, unsafe_stack_register_backup);

extern unsigned long x86_cr3_pcid_noflush;
DECLARE_PER_CPU(unsigned long, x86_cr3_pcid_user);

extern char __per_cpu_user_mapped_start[], __per_cpu_user_mapped_end[];

/**
 *  kaiser_add_mapping - map a virtual memory part to the shadow (user) mapping
 *  @addr: the start address of the range
 *  @size: the size of the range
 *  @flags: The mapping flags of the pages
 *
 *  The mapping is done on a global scope, so no bigger
 *  synchronization has to be done.  the pages have to be
 *  manually unmapped again when they are not needed any longer.
 */
extern int kaiser_add_mapping(unsigned long addr, unsigned long size, unsigned long flags);

/**
 *  kaiser_remove_mapping - unmap a virtual memory part of the shadow mapping
 *  @addr: the start address of the range
 *  @size: the size of the range
 */
extern void kaiser_remove_mapping(unsigned long start, unsigned long size);

/**
 *  kaiser_init - Initialize the shadow mapping
 *
 *  Most parts of the shadow mapping can be mapped upon boot
 *  time.  Only per-process things like the thread stacks
 *  or a new LDT have to be mapped at runtime.  These boot-
 *  time mappings are permanent and never unmapped.
 */
extern void kaiser_init(void);

#endif /* CONFIG_KAISER */

#endif /* __ASSEMBLY */

#endif /* _ASM_X86_KAISER_H */
