#ifndef __ASM_ARM_CACHETYPE_H
#define __ASM_ARM_CACHETYPE_H

#include <asm/cputype.h>

#define __cacheid_present(val)			(val != read_cpuid_id())
#define __cacheid_type_v7(val)			((val & (7 << 29)) == (4 << 29))

#define __cacheid_vivt_prev7(val)		((val & (15 << 25)) != (14 << 25))
#define __cacheid_vipt_prev7(val)		((val & (15 << 25)) == (14 << 25))
#define __cacheid_vipt_nonaliasing_prev7(val)	((val & (15 << 25 | 1 << 23)) == (14 << 25))
#define __cacheid_vipt_aliasing_prev7(val)	((val & (15 << 25 | 1 << 23)) == (14 << 25 | 1 << 23))

#define __cacheid_vivt(val)			(__cacheid_type_v7(val) ? 0 : __cacheid_vivt_prev7(val))
#define __cacheid_vipt(val)			(__cacheid_type_v7(val) ? 1 : __cacheid_vipt_prev7(val))
#define __cacheid_vipt_nonaliasing(val)		(__cacheid_type_v7(val) ? 1 : __cacheid_vipt_nonaliasing_prev7(val))
#define __cacheid_vipt_aliasing(val)		(__cacheid_type_v7(val) ? 0 : __cacheid_vipt_aliasing_prev7(val))
#define __cacheid_vivt_asid_tagged_instr(val)	(__cacheid_type_v7(val) ? ((val & (3 << 14)) == (1 << 14)) : 0)

#if defined(CONFIG_CPU_CACHE_VIVT) && !defined(CONFIG_CPU_CACHE_VIPT)
/*
 * VIVT caches only
 */
#define cache_is_vivt()			1
#define cache_is_vipt()			0
#define cache_is_vipt_nonaliasing()	0
#define cache_is_vipt_aliasing()	0
#define icache_is_vivt_asid_tagged()	0

#elif !defined(CONFIG_CPU_CACHE_VIVT) && defined(CONFIG_CPU_CACHE_VIPT)
/*
 * VIPT caches only
 */
#define cache_is_vivt()			0
#define cache_is_vipt()			1
#define cache_is_vipt_nonaliasing()					\
	({								\
		unsigned int __val = read_cpuid_cachetype();		\
		__cacheid_vipt_nonaliasing(__val);			\
	})

#define cache_is_vipt_aliasing()					\
	({								\
		unsigned int __val = read_cpuid_cachetype();		\
		__cacheid_vipt_aliasing(__val);				\
	})

#define icache_is_vivt_asid_tagged()					\
	({								\
		unsigned int __val = read_cpuid_cachetype();		\
		__cacheid_vivt_asid_tagged_instr(__val);		\
	})

#else
/*
 * VIVT or VIPT caches.  Note that this is unreliable since ARM926
 * and V6 CPUs satisfy the "(val & (15 << 25)) == (14 << 25)" test.
 * There's no way to tell from the CacheType register what type (!)
 * the cache is.
 */
#define cache_is_vivt()							\
	({								\
		unsigned int __val = read_cpuid_cachetype();		\
		(!__cacheid_present(__val)) || __cacheid_vivt(__val);	\
	})

#define cache_is_vipt()							\
	({								\
		unsigned int __val = read_cpuid_cachetype();		\
		__cacheid_present(__val) && __cacheid_vipt(__val);	\
	})

#define cache_is_vipt_nonaliasing()					\
	({								\
		unsigned int __val = read_cpuid_cachetype();		\
		__cacheid_present(__val) &&				\
		 __cacheid_vipt_nonaliasing(__val);			\
	})

#define cache_is_vipt_aliasing()					\
	({								\
		unsigned int __val = read_cpuid_cachetype();		\
		__cacheid_present(__val) &&				\
		 __cacheid_vipt_aliasing(__val);			\
	})

#define icache_is_vivt_asid_tagged()					\
	({								\
		unsigned int __val = read_cpuid_cachetype();		\
		__cacheid_present(__val) &&				\
		 __cacheid_vivt_asid_tagged_instr(__val);		\
	})

#endif

#endif
