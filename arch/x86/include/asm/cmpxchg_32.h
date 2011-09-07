#ifndef _ASM_X86_CMPXCHG_32_H
#define _ASM_X86_CMPXCHG_32_H

#include <linux/bitops.h> /* for LOCK_PREFIX */

/*
 * Note: if you use set64_bit(), __cmpxchg64(), or their variants, you
 *       you need to test for the feature in boot_cpu_data.
 */

extern void __xchg_wrong_size(void);

/*
 * Note: no "lock" prefix even on SMP: xchg always implies lock anyway.
 * Since this is generally used to protect other memory information, we
 * use "asm volatile" and "memory" clobbers to prevent gcc from moving
 * information around.
 */
#define __xchg(x, ptr, size)						\
({									\
	__typeof(*(ptr)) __x = (x);					\
	switch (size) {							\
	case 1:								\
	{								\
		volatile u8 *__ptr = (volatile u8 *)(ptr);		\
		asm volatile("xchgb %0,%1"				\
			     : "=q" (__x), "+m" (*__ptr)		\
			     : "0" (__x)				\
			     : "memory");				\
		break;							\
	}								\
	case 2:								\
	{								\
		volatile u16 *__ptr = (volatile u16 *)(ptr);		\
		asm volatile("xchgw %0,%1"				\
			     : "=r" (__x), "+m" (*__ptr)		\
			     : "0" (__x)				\
			     : "memory");				\
		break;							\
	}								\
	case 4:								\
	{								\
		volatile u32 *__ptr = (volatile u32 *)(ptr);		\
		asm volatile("xchgl %0,%1"				\
			     : "=r" (__x), "+m" (*__ptr)		\
			     : "0" (__x)				\
			     : "memory");				\
		break;							\
	}								\
	default:							\
		__xchg_wrong_size();					\
	}								\
	__x;								\
})

#define xchg(ptr, v)							\
	__xchg((v), (ptr), sizeof(*ptr))

/*
 * CMPXCHG8B only writes to the target if we had the previous
 * value in registers, otherwise it acts as a read and gives us the
 * "new previous" value.  That is why there is a loop.  Preloading
 * EDX:EAX is a performance optimization: in the common case it means
 * we need only one locked operation.
 *
 * A SIMD/3DNOW!/MMX/FPU 64-bit store here would require at the very
 * least an FPU save and/or %cr0.ts manipulation.
 *
 * cmpxchg8b must be used with the lock prefix here to allow the
 * instruction to be executed atomically.  We need to have the reader
 * side to see the coherent 64bit value.
 */
static inline void set_64bit(volatile u64 *ptr, u64 value)
{
	u32 low  = value;
	u32 high = value >> 32;
	u64 prev = *ptr;

	asm volatile("\n1:\t"
		     LOCK_PREFIX "cmpxchg8b %0\n\t"
		     "jnz 1b"
		     : "=m" (*ptr), "+A" (prev)
		     : "b" (low), "c" (high)
		     : "memory");
}

extern void __cmpxchg_wrong_size(void);

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
#define __raw_cmpxchg(ptr, old, new, size, lock)			\
({									\
	__typeof__(*(ptr)) __ret;					\
	__typeof__(*(ptr)) __old = (old);				\
	__typeof__(*(ptr)) __new = (new);				\
	switch (size) {							\
	case 1:								\
	{								\
		volatile u8 *__ptr = (volatile u8 *)(ptr);		\
		asm volatile(lock "cmpxchgb %2,%1"			\
			     : "=a" (__ret), "+m" (*__ptr)		\
			     : "q" (__new), "0" (__old)			\
			     : "memory");				\
		break;							\
	}								\
	case 2:								\
	{								\
		volatile u16 *__ptr = (volatile u16 *)(ptr);		\
		asm volatile(lock "cmpxchgw %2,%1"			\
			     : "=a" (__ret), "+m" (*__ptr)		\
			     : "r" (__new), "0" (__old)			\
			     : "memory");				\
		break;							\
	}								\
	case 4:								\
	{								\
		volatile u32 *__ptr = (volatile u32 *)(ptr);		\
		asm volatile(lock "cmpxchgl %2,%1"			\
			     : "=a" (__ret), "+m" (*__ptr)		\
			     : "r" (__new), "0" (__old)			\
			     : "memory");				\
		break;							\
	}								\
	default:							\
		__cmpxchg_wrong_size();					\
	}								\
	__ret;								\
})

#define __cmpxchg(ptr, old, new, size)					\
	__raw_cmpxchg((ptr), (old), (new), (size), LOCK_PREFIX)

#define __sync_cmpxchg(ptr, old, new, size)				\
	__raw_cmpxchg((ptr), (old), (new), (size), "lock; ")

#define __cmpxchg_local(ptr, old, new, size)				\
	__raw_cmpxchg((ptr), (old), (new), (size), "")

#ifdef CONFIG_X86_CMPXCHG
#define __HAVE_ARCH_CMPXCHG 1

#define cmpxchg(ptr, old, new)						\
	__cmpxchg((ptr), (old), (new), sizeof(*ptr))

#define sync_cmpxchg(ptr, old, new)					\
	__sync_cmpxchg((ptr), (old), (new), sizeof(*ptr))

#define cmpxchg_local(ptr, old, new)					\
	__cmpxchg_local((ptr), (old), (new), sizeof(*ptr))
#endif

#ifdef CONFIG_X86_CMPXCHG64
#define cmpxchg64(ptr, o, n)						\
	((__typeof__(*(ptr)))__cmpxchg64((ptr), (unsigned long long)(o), \
					 (unsigned long long)(n)))
#define cmpxchg64_local(ptr, o, n)					\
	((__typeof__(*(ptr)))__cmpxchg64_local((ptr), (unsigned long long)(o), \
					       (unsigned long long)(n)))
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

#ifndef CONFIG_X86_CMPXCHG
/*
 * Building a kernel capable running on 80386. It may be necessary to
 * simulate the cmpxchg on the 80386 CPU. For that purpose we define
 * a function for each of the sizes we support.
 */

extern unsigned long cmpxchg_386_u8(volatile void *, u8, u8);
extern unsigned long cmpxchg_386_u16(volatile void *, u16, u16);
extern unsigned long cmpxchg_386_u32(volatile void *, u32, u32);

static inline unsigned long cmpxchg_386(volatile void *ptr, unsigned long old,
					unsigned long new, int size)
{
	switch (size) {
	case 1:
		return cmpxchg_386_u8(ptr, old, new);
	case 2:
		return cmpxchg_386_u16(ptr, old, new);
	case 4:
		return cmpxchg_386_u32(ptr, old, new);
	}
	return old;
}

#define cmpxchg(ptr, o, n)						\
({									\
	__typeof__(*(ptr)) __ret;					\
	if (likely(boot_cpu_data.x86 > 3))				\
		__ret = (__typeof__(*(ptr)))__cmpxchg((ptr),		\
				(unsigned long)(o), (unsigned long)(n),	\
				sizeof(*(ptr)));			\
	else								\
		__ret = (__typeof__(*(ptr)))cmpxchg_386((ptr),		\
				(unsigned long)(o), (unsigned long)(n),	\
				sizeof(*(ptr)));			\
	__ret;								\
})
#define cmpxchg_local(ptr, o, n)					\
({									\
	__typeof__(*(ptr)) __ret;					\
	if (likely(boot_cpu_data.x86 > 3))				\
		__ret = (__typeof__(*(ptr)))__cmpxchg_local((ptr),	\
				(unsigned long)(o), (unsigned long)(n),	\
				sizeof(*(ptr)));			\
	else								\
		__ret = (__typeof__(*(ptr)))cmpxchg_386((ptr),		\
				(unsigned long)(o), (unsigned long)(n),	\
				sizeof(*(ptr)));			\
	__ret;								\
})
#endif

#ifndef CONFIG_X86_CMPXCHG64
/*
 * Building a kernel capable running on 80386 and 80486. It may be necessary
 * to simulate the cmpxchg8b on the 80386 and 80486 CPU.
 */

#define cmpxchg64(ptr, o, n)					\
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


#define cmpxchg64_local(ptr, o, n)				\
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

#define cmpxchg8b(ptr, o1, o2, n1, n2)				\
({								\
	char __ret;						\
	__typeof__(o2) __dummy;					\
	__typeof__(*(ptr)) __old1 = (o1);			\
	__typeof__(o2) __old2 = (o2);				\
	__typeof__(*(ptr)) __new1 = (n1);			\
	__typeof__(o2) __new2 = (n2);				\
	asm volatile(LOCK_PREFIX "cmpxchg8b %2; setz %1"	\
		       : "=d"(__dummy), "=a" (__ret), "+m" (*ptr)\
		       : "a" (__old1), "d"(__old2),		\
		         "b" (__new1), "c" (__new2)		\
		       : "memory");				\
	__ret; })


#define cmpxchg8b_local(ptr, o1, o2, n1, n2)			\
({								\
	char __ret;						\
	__typeof__(o2) __dummy;					\
	__typeof__(*(ptr)) __old1 = (o1);			\
	__typeof__(o2) __old2 = (o2);				\
	__typeof__(*(ptr)) __new1 = (n1);			\
	__typeof__(o2) __new2 = (n2);				\
	asm volatile("cmpxchg8b %2; setz %1"			\
		       : "=d"(__dummy), "=a"(__ret), "+m" (*ptr)\
		       : "a" (__old), "d"(__old2),		\
		         "b" (__new1), "c" (__new2),		\
		       : "memory");				\
	__ret; })


#define cmpxchg_double(ptr, o1, o2, n1, n2)				\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 4);				\
	VM_BUG_ON((unsigned long)(ptr) % 8);				\
	cmpxchg8b((ptr), (o1), (o2), (n1), (n2));			\
})

#define cmpxchg_double_local(ptr, o1, o2, n1, n2)			\
({									\
       BUILD_BUG_ON(sizeof(*(ptr)) != 4);				\
       VM_BUG_ON((unsigned long)(ptr) % 8);				\
       cmpxchg16b_local((ptr), (o1), (o2), (n1), (n2));			\
})

#define system_has_cmpxchg_double() cpu_has_cx8

#endif /* _ASM_X86_CMPXCHG_32_H */
