#ifndef __ARCH_BLACKFIN_PERCPU__
#define __ARCH_BLACKFIN_PERCPU__

#include <asm-generic/percpu.h>

#ifdef CONFIG_MODULES
#define PERCPU_MODULE_RESERVE 8192
#else
#define PERCPU_MODULE_RESERVE 0
#endif

#define PERCPU_ENOUGH_ROOM \
	(ALIGN(__per_cpu_end - __per_cpu_start, SMP_CACHE_BYTES) + \
	 PERCPU_MODULE_RESERVE)

#endif	/* __ARCH_BLACKFIN_PERCPU__ */
