#ifndef __ASM_SH_FUTEX_CAS_H
#define __ASM_SH_FUTEX_CAS_H

static inline int atomic_futex_op_cmpxchg_inatomic(u32 *uval,
						   u32 __user *uaddr,
						   u32 oldval, u32 newval)
{
	int err = 0;
	__asm__ __volatile__(
		"1:\n\t"
		"cas.l	%2, %1, @r0\n"
		"2:\n\t"
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
		:"+r" (err), "+r" (newval)
		:"r" (oldval), "i" (-EFAULT), "z" (uaddr)
		:"t", "memory");
	if (err) return err;
	*uval = newval;
	return 0;
}

#endif /* __ASM_SH_FUTEX_CAS_H */
