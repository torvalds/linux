/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CMPXCHG_32_H
#define _ASM_X86_CMPXCHG_32_H

/*
 * Note: if you use set64_bit(), __cmpxchg64(), or their variants,
 *       you need to test for the feature in boot_cpu_data.
 */

#ifdef CONFIG_X86_CMPXCHG64
#define arch_cmpxchg64(ptr, o, n)					\
	((__typeof__(*(ptr)))__cmpxchg64((ptr), (unsigned long long)(o), \
					 (unsigned long long)(n)))
#define arch_cmpxchg64_local(ptr, o, n)					\
	((__typeof__(*(ptr)))__cmpxchg64_local((ptr), (unsigned long long)(o), \
					       (unsigned long long)(n)))
#define arch_try_cmpxchg64(ptr, po, n)					\
	__try_cmpxchg64((ptr), (unsigned long long *)(po), \
			(unsigned long long)(n))
#endif

static inline u64 __cmpxchg64(volatile u64 *ptr, u64 old, u64 new)
{
	u64 prev;
	asm volatile(LOCK_PREFIX "cmpxchg8b %1"
		     : "=A" (prev),
		       "+m" (*ptr)
		     : "b" ((u32)new),
		       "c" ((u32)(new >> 32)),
		       "0" (old)
		     : "memory");
	return prev;
}

static inline u64 __cmpxchg64_local(volatile u64 *ptr, u64 old, u64 new)
{
	u64 prev;
	asm volatile("cmpxchg8b %1"
		     : "=A" (prev),
		       "+m" (*ptr)
		     : "b" ((u32)new),
		       "c" ((u32)(new >> 32)),
		       "0" (old)
		     : "memory");
	return prev;
}

static inline bool __try_cmpxchg64(volatile u64 *ptr, u64 *pold, u64 new)
{
	bool success;
	u64 old = *pold;
	asm volatile(LOCK_PREFIX "cmpxchg8b %[ptr]"
		     CC_SET(z)
		     : CC_OUT(z) (success),
		       [ptr] "+m" (*ptr),
		       "+A" (old)
		     : "b" ((u32)new),
		       "c" ((u32)(new >> 32))
		     : "memory");

	if (unlikely(!success))
		*pold = old;
	return success;
}

#ifndef CONFIG_X86_CMPXCHG64
/*
 * Building a kernel capable running on 80386 and 80486. It may be necessary
 * to simulate the cmpxchg8b on the 80386 and 80486 CPU.
 */

#define arch_cmpxchg64(ptr, o, n)				\
({								\
	__typeof__(*(ptr)) __ret;				\
	__typeof__(*(ptr)) __old = (o);				\
	__typeof__(*(ptr)) __new = (n);				\
	alternative_io(LOCK_PREFIX_HERE				\
			"call cmpxchg8b_emu",			\
			"lock; cmpxchg8b (%%esi)" ,		\
		       X86_FEATURE_CX8,				\
		       "=A" (__ret),				\
		       "S" ((ptr)), "0" (__old),		\
		       "b" ((unsigned int)__new),		\
		       "c" ((unsigned int)(__new>>32))		\
		       : "memory");				\
	__ret; })


#define arch_cmpxchg64_local(ptr, o, n)				\
({								\
	__typeof__(*(ptr)) __ret;				\
	__typeof__(*(ptr)) __old = (o);				\
	__typeof__(*(ptr)) __new = (n);				\
	alternative_io("call cmpxchg8b_emu",			\
		       "cmpxchg8b (%%esi)" ,			\
		       X86_FEATURE_CX8,				\
		       "=A" (__ret),				\
		       "S" ((ptr)), "0" (__old),		\
		       "b" ((unsigned int)__new),		\
		       "c" ((unsigned int)(__new>>32))		\
		       : "memory");				\
	__ret; })

#endif

#define system_has_cmpxchg64()		boot_cpu_has(X86_FEATURE_CX8)

#endif /* _ASM_X86_CMPXCHG_32_H */
