/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CMPXCHG_32_H
#define _ASM_X86_CMPXCHG_32_H

/*
 * Note: if you use __cmpxchg64(), or their variants,
 *       you need to test for the feature in boot_cpu_data.
 */

union __u64_halves {
	u64 full;
	struct {
		u32 low, high;
	};
};

#define __arch_cmpxchg64(_ptr, _old, _new, _lock)			\
({									\
	union __u64_halves o = { .full = (_old), },			\
			   n = { .full = (_new), };			\
									\
	asm volatile(_lock "cmpxchg8b %[ptr]"				\
		     : [ptr] "+m" (*(_ptr)),				\
		       "+a" (o.low), "+d" (o.high)			\
		     : "b" (n.low), "c" (n.high)			\
		     : "memory");					\
									\
	o.full;								\
})


static __always_inline u64 __cmpxchg64(volatile u64 *ptr, u64 old, u64 new)
{
	return __arch_cmpxchg64(ptr, old, new, LOCK_PREFIX);
}

static __always_inline u64 __cmpxchg64_local(volatile u64 *ptr, u64 old, u64 new)
{
	return __arch_cmpxchg64(ptr, old, new,);
}

#define __arch_try_cmpxchg64(_ptr, _oldp, _new, _lock)			\
({									\
	union __u64_halves o = { .full = *(_oldp), },			\
			   n = { .full = (_new), };			\
	bool ret;							\
									\
	asm volatile(_lock "cmpxchg8b %[ptr]"				\
		     CC_SET(e)						\
		     : CC_OUT(e) (ret),					\
		       [ptr] "+m" (*(_ptr)),				\
		       "+a" (o.low), "+d" (o.high)			\
		     : "b" (n.low), "c" (n.high)			\
		     : "memory");					\
									\
	if (unlikely(!ret))						\
		*(_oldp) = o.full;					\
									\
	likely(ret);							\
})

static __always_inline bool __try_cmpxchg64(volatile u64 *ptr, u64 *oldp, u64 new)
{
	return __arch_try_cmpxchg64(ptr, oldp, new, LOCK_PREFIX);
}

static __always_inline bool __try_cmpxchg64_local(volatile u64 *ptr, u64 *oldp, u64 new)
{
	return __arch_try_cmpxchg64(ptr, oldp, new,);
}

#ifdef CONFIG_X86_CMPXCHG64

#define arch_cmpxchg64 __cmpxchg64

#define arch_cmpxchg64_local __cmpxchg64_local

#define arch_try_cmpxchg64 __try_cmpxchg64

#define arch_try_cmpxchg64_local __try_cmpxchg64_local

#else

/*
 * Building a kernel capable running on 80386 and 80486. It may be necessary
 * to simulate the cmpxchg8b on the 80386 and 80486 CPU.
 */

#define __arch_cmpxchg64_emu(_ptr, _old, _new, _lock_loc, _lock)	\
({									\
	union __u64_halves o = { .full = (_old), },			\
			   n = { .full = (_new), };			\
									\
	asm volatile(ALTERNATIVE(_lock_loc				\
				 "call cmpxchg8b_emu",			\
				 _lock "cmpxchg8b %[ptr]", X86_FEATURE_CX8) \
		     : [ptr] "+m" (*(_ptr)),				\
		       "+a" (o.low), "+d" (o.high)			\
		     : "b" (n.low), "c" (n.high), "S" (_ptr)		\
		     : "memory");					\
									\
	o.full;								\
})

static __always_inline u64 arch_cmpxchg64(volatile u64 *ptr, u64 old, u64 new)
{
	return __arch_cmpxchg64_emu(ptr, old, new, LOCK_PREFIX_HERE, "lock; ");
}
#define arch_cmpxchg64 arch_cmpxchg64

static __always_inline u64 arch_cmpxchg64_local(volatile u64 *ptr, u64 old, u64 new)
{
	return __arch_cmpxchg64_emu(ptr, old, new, ,);
}
#define arch_cmpxchg64_local arch_cmpxchg64_local

#define __arch_try_cmpxchg64_emu(_ptr, _oldp, _new, _lock_loc, _lock)	\
({									\
	union __u64_halves o = { .full = *(_oldp), },			\
			   n = { .full = (_new), };			\
	bool ret;							\
									\
	asm volatile(ALTERNATIVE(_lock_loc				\
				 "call cmpxchg8b_emu",			\
				 _lock "cmpxchg8b %[ptr]", X86_FEATURE_CX8) \
		     CC_SET(e)						\
		     : CC_OUT(e) (ret),					\
		       [ptr] "+m" (*(_ptr)),				\
		       "+a" (o.low), "+d" (o.high)			\
		     : "b" (n.low), "c" (n.high), "S" (_ptr)		\
		     : "memory");					\
									\
	if (unlikely(!ret))						\
		*(_oldp) = o.full;					\
									\
	likely(ret);							\
})

static __always_inline bool arch_try_cmpxchg64(volatile u64 *ptr, u64 *oldp, u64 new)
{
	return __arch_try_cmpxchg64_emu(ptr, oldp, new, LOCK_PREFIX_HERE, "lock; ");
}
#define arch_try_cmpxchg64 arch_try_cmpxchg64

static __always_inline bool arch_try_cmpxchg64_local(volatile u64 *ptr, u64 *oldp, u64 new)
{
	return __arch_try_cmpxchg64_emu(ptr, oldp, new, ,);
}
#define arch_try_cmpxchg64_local arch_try_cmpxchg64_local

#endif

#define system_has_cmpxchg64()		boot_cpu_has(X86_FEATURE_CX8)

#endif /* _ASM_X86_CMPXCHG_32_H */
