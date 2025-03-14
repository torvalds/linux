/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#include <asm/alternative.h>

#ifndef __ASSEMBLER__

/* The synchronize caches instruction executes as a nop on systems in
   which all memory references are performed in order. */
#define synchronize_caches() asm volatile("sync" \
	ALTERNATIVE(ALT_COND_NO_SMP, INSN_NOP) \
	: : : "memory")

#if defined(CONFIG_SMP)
#define mb()		do { synchronize_caches(); } while (0)
#define rmb()		mb()
#define wmb()		mb()
#define dma_rmb()	mb()
#define dma_wmb()	mb()
#else
#define mb()		barrier()
#define rmb()		barrier()
#define wmb()		barrier()
#define dma_rmb()	barrier()
#define dma_wmb()	barrier()
#endif

#define __smp_mb()	mb()
#define __smp_rmb()	mb()
#define __smp_wmb()	mb()

#define __smp_store_release(p, v)					\
do {									\
	typeof(p) __p = (p);						\
        union { typeof(*p) __val; char __c[1]; } __u =			\
                { .__val = (__force typeof(*p)) (v) };			\
	compiletime_assert_atomic_type(*p);				\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile("stb,ma %0,0(%1)"				\
				: : "r"(*(__u8 *)__u.__c), "r"(__p)	\
				: "memory");				\
		break;							\
	case 2:								\
		asm volatile("sth,ma %0,0(%1)"				\
				: : "r"(*(__u16 *)__u.__c), "r"(__p)	\
				: "memory");				\
		break;							\
	case 4:								\
		asm volatile("stw,ma %0,0(%1)"				\
				: : "r"(*(__u32 *)__u.__c), "r"(__p)	\
				: "memory");				\
		break;							\
	case 8:								\
		if (IS_ENABLED(CONFIG_64BIT))				\
			asm volatile("std,ma %0,0(%1)"			\
				: : "r"(*(__u64 *)__u.__c), "r"(__p)	\
				: "memory");				\
		break;							\
	}								\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	union { typeof(*p) __val; char __c[1]; } __u;			\
	typeof(p) __p = (p);						\
	compiletime_assert_atomic_type(*p);				\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile("ldb,ma 0(%1),%0"				\
				: "=r"(*(__u8 *)__u.__c) : "r"(__p)	\
				: "memory");				\
		break;							\
	case 2:								\
		asm volatile("ldh,ma 0(%1),%0"				\
				: "=r"(*(__u16 *)__u.__c) : "r"(__p)	\
				: "memory");				\
		break;							\
	case 4:								\
		asm volatile("ldw,ma 0(%1),%0"				\
				: "=r"(*(__u32 *)__u.__c) : "r"(__p)	\
				: "memory");				\
		break;							\
	case 8:								\
		if (IS_ENABLED(CONFIG_64BIT))				\
			asm volatile("ldd,ma 0(%1),%0"			\
				: "=r"(*(__u64 *)__u.__c) : "r"(__p)	\
				: "memory");				\
		break;							\
	}								\
	__u.__val;							\
})
#include <asm-generic/barrier.h>

#endif /* !__ASSEMBLER__ */
#endif /* __ASM_BARRIER_H */
