/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_FUTEX_LLSC_H
#define __ASM_SH_FUTEX_LLSC_H

static inline int atomic_futex_op_cmpxchg_inatomic(u32 *uval,
						   u32 __user *uaddr,
						   u32 oldval, u32 newval)
{
	int err = 0;
	__asm__ __volatile__(
		"synco\n"
		"1:\n\t"
		"movli.l	@%2, r0\n\t"
		"mov	r0, %1\n\t"
		"cmp/eq	%1, %4\n\t"
		"bf	2f\n\t"
		"mov	%5, r0\n\t"
		"movco.l	r0, @%2\n\t"
		"bf	1b\n"
		"2:\n\t"
		"synco\n\t"
#ifdef CONFIG_MMU
		".section	.fixup,\"ax\"\n"
		"3:\n\t"
		"mov.l	4f, %0\n\t"
		"jmp	@%0\n\t"
		" mov	%3, %0\n\t"
		".balign	4\n"
		"4:	.long	2b\n\t"
		".previous\n"
		".section	__ex_table,\"a\"\n\t"
		".long	1b, 3b\n\t"
		".previous"
#endif
		:"+r" (err), "=&r" (*uval)
		:"r" (uaddr), "i" (-EFAULT), "r" (oldval), "r" (newval)
		:"t", "memory", "r0");
	if (err) return err;
	return 0;
}

#endif /* __ASM_SH_FUTEX_LLSC_H */
