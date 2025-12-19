/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_ARCH_STACKPROTECTOR_H
#define _ASM_S390_ARCH_STACKPROTECTOR_H

extern unsigned long __stack_chk_guard;
extern int stack_protector_debug;

void __stack_protector_apply_early(unsigned long kernel_start);
int __stack_protector_apply(unsigned long *start, unsigned long *end, unsigned long kernel_start);

static inline void stack_protector_apply_early(unsigned long kernel_start)
{
	if (IS_ENABLED(CONFIG_STACKPROTECTOR))
		__stack_protector_apply_early(kernel_start);
}

static inline int stack_protector_apply(unsigned long *start, unsigned long *end)
{
	if (IS_ENABLED(CONFIG_STACKPROTECTOR))
		return __stack_protector_apply(start, end, 0);
	return 0;
}

#endif /* _ASM_S390_ARCH_STACKPROTECTOR_H */
