#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#ifndef __ASSEMBLY__

#define nop() __asm__ __volatile__("mov\tr0,r0\t@ nop\n\t");

#if __LINUX_ARM_ARCH__ >= 7 ||		\
	(__LINUX_ARM_ARCH__ == 6 && defined(CONFIG_CPU_32v6K))
#define sev()	__asm__ __volatile__ ("sev" : : : "memory")
#define wfe()	__asm__ __volatile__ ("wfe" : : : "memory")
#define wfi()	__asm__ __volatile__ ("wfi" : : : "memory")
#endif

#if __LINUX_ARM_ARCH__ >= 7
#define isb(option) __asm__ __volatile__ ("isb " #option : : : "memory")
#define dsb(option) __asm__ __volatile__ ("dsb " #option : : : "memory")
#define dmb(option) __asm__ __volatile__ ("dmb " #option : : : "memory")
#elif defined(CONFIG_CPU_XSC3) || __LINUX_ARM_ARCH__ == 6
#define isb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 5" \
				    : : "r" (0) : "memory")
#elif defined(CONFIG_CPU_FA526)
#define isb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb(x) __asm__ __volatile__ ("" : : : "memory")
#else
#define isb(x) __asm__ __volatile__ ("" : : : "memory")
#define dsb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb(x) __asm__ __volatile__ ("" : : : "memory")
#endif

#ifdef CONFIG_ARM_HEAVY_MB
extern void (*soc_mb)(void);
extern void arm_heavy_mb(void);
#define __arm_heavy_mb(x...) do { dsb(x); arm_heavy_mb(); } while (0)
#else
#define __arm_heavy_mb(x...) dsb(x)
#endif

#ifdef CONFIG_ARCH_HAS_BARRIERS
#include <mach/barriers.h>
#elif defined(CONFIG_ARM_DMA_MEM_BUFFERABLE) || defined(CONFIG_SMP)
#define mb()		__arm_heavy_mb()
#define rmb()		dsb()
#define wmb()		__arm_heavy_mb(st)
#define dma_rmb()	dmb(osh)
#define dma_wmb()	dmb(oshst)
#else
#define mb()		barrier()
#define rmb()		barrier()
#define wmb()		barrier()
#define dma_rmb()	barrier()
#define dma_wmb()	barrier()
#endif

#ifndef CONFIG_SMP
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#else
#define smp_mb()	dmb(ish)
#define smp_rmb()	smp_mb()
#define smp_wmb()	dmb(ishst)
#endif

#define smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	smp_mb();							\
	WRITE_ONCE(*p, v);						\
} while (0)

#define smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	smp_mb();							\
	___p1;								\
})

#define read_barrier_depends()		do { } while(0)
#define smp_read_barrier_depends()	do { } while(0)

#define smp_store_mb(var, value)	do { WRITE_ONCE(var, value); smp_mb(); } while (0)

#define smp_mb__before_atomic()	smp_mb()
#define smp_mb__after_atomic()	smp_mb()

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_BARRIER_H */
