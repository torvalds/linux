#ifndef _ASM_X86_KAISER_H
#define _ASM_X86_KAISER_H

/* This file includes the definitions for the KAISER feature.
 * KAISER is a counter measure against x86_64 side channel attacks on the kernel virtual memory.
 * It has a shodow-pgd for every process. the shadow-pgd has a minimalistic kernel-set mapped,
 * but includes the whole user memory. Within a kernel context switch, or when an interrupt is handled,
 * the pgd is switched to the normal one. When the system switches to user mode, the shadow pgd is enabled.
 * By this, the virtual memory chaches are freed, and the user may not attack the whole kernel memory.
 *
 * A minimalistic kernel mapping holds the parts needed to be mapped in user mode, as the entry/exit functions
 * of the user space, or the stacks.
 */
#ifdef __ASSEMBLY__
#ifdef CONFIG_KAISER

.macro _SWITCH_TO_KERNEL_CR3 reg
movq %cr3, \reg
#ifdef CONFIG_KAISER_REAL_SWITCH
andq $(~0x1000), \reg
#endif
movq \reg, %cr3
.endm

.macro _SWITCH_TO_USER_CR3 reg
movq %cr3, \reg
#ifdef CONFIG_KAISER_REAL_SWITCH
orq $(0x1000), \reg
#endif
movq \reg, %cr3
.endm

.macro SWITCH_KERNEL_CR3
pushq %rax
_SWITCH_TO_KERNEL_CR3 %rax
popq %rax
.endm

.macro SWITCH_USER_CR3
pushq %rax
_SWITCH_TO_USER_CR3 %rax
popq %rax
.endm

.macro SWITCH_KERNEL_CR3_NO_STACK
movq %rax, PER_CPU_VAR(unsafe_stack_register_backup)
_SWITCH_TO_KERNEL_CR3 %rax
movq PER_CPU_VAR(unsafe_stack_register_backup), %rax
.endm


.macro SWITCH_USER_CR3_NO_STACK

movq %rax, PER_CPU_VAR(unsafe_stack_register_backup)
_SWITCH_TO_USER_CR3 %rax
movq PER_CPU_VAR(unsafe_stack_register_backup), %rax

.endm

#else /* CONFIG_KAISER */

.macro SWITCH_KERNEL_CR3 reg
.endm
.macro SWITCH_USER_CR3 reg
.endm
.macro SWITCH_USER_CR3_NO_STACK
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
 *  kaiser_initialize_mapping - Initalize the shadow mapping
 *
 *  Most parts of the shadow mapping can be mapped upon boot
 *  time.  Only per-process things like the thread stacks
 *  or a new LDT have to be mapped at runtime.  These boot-
 *  time mappings are permanent and nevertunmapped.
 */
extern void kaiser_init(void);

#endif /* CONFIG_KAISER */

#endif /* __ASSEMBLY */



#endif /* _ASM_X86_KAISER_H */
