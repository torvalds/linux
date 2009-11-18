#ifndef __ARCH_S390_PERCPU__
#define __ARCH_S390_PERCPU__

/*
 * s390 uses its own implementation for per cpu data, the offset of
 * the cpu local data area is cached in the cpu's lowcore memory.
 */
#define __my_cpu_offset S390_lowcore.percpu_offset

/*
 * For 64 bit module code, the module may be more than 4G above the
 * per cpu area, use weak definitions to force the compiler to
 * generate external references.
 */
#if defined(CONFIG_SMP) && defined(__s390x__) && defined(MODULE)
#define ARCH_NEEDS_WEAK_PER_CPU
#endif

#include <asm-generic/percpu.h>

#endif /* __ARCH_S390_PERCPU__ */
