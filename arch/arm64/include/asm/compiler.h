/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_COMPILER_H
#define __ASM_COMPILER_H

/*
 * The EL0/EL1 pointer bits used by a pointer authentication code.
 * This is dependent on TBI0/TBI1 being enabled, or bits 63:56 would also apply.
 */
#define ptrauth_user_pac_mask()		GENMASK_ULL(54, vabits_actual)
#define ptrauth_kernel_pac_mask()	GENMASK_ULL(63, vabits_actual)

/* Valid for EL0 TTBR0 and EL1 TTBR1 instruction pointers */
#define ptrauth_clear_pac(ptr)						\
	((ptr & BIT_ULL(55)) ? (ptr | ptrauth_kernel_pac_mask()) :	\
			       (ptr & ~ptrauth_user_pac_mask()))

#define __builtin_return_address(val)					\
	(void *)(ptrauth_clear_pac((unsigned long)__builtin_return_address(val)))

#endif /* __ASM_COMPILER_H */
