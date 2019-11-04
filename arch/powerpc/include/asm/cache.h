/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_CACHE_H
#define _ASM_POWERPC_CACHE_H

#ifdef __KERNEL__


/* bytes per L1 cache line */
#if defined(CONFIG_PPC_8xx) || defined(CONFIG_403GCX)
#define L1_CACHE_SHIFT		4
#define MAX_COPY_PREFETCH	1
#define IFETCH_ALIGN_SHIFT	2
#elif defined(CONFIG_PPC_E500MC)
#define L1_CACHE_SHIFT		6
#define MAX_COPY_PREFETCH	4
#define IFETCH_ALIGN_SHIFT	3
#elif defined(CONFIG_PPC32)
#define MAX_COPY_PREFETCH	4
#define IFETCH_ALIGN_SHIFT	3	/* 603 fetches 2 insn at a time */
#if defined(CONFIG_PPC_47x)
#define L1_CACHE_SHIFT		7
#else
#define L1_CACHE_SHIFT		5
#endif
#else /* CONFIG_PPC64 */
#define L1_CACHE_SHIFT		7
#define IFETCH_ALIGN_SHIFT	4 /* POWER8,9 */
#endif

#define	L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

#define	SMP_CACHE_BYTES		L1_CACHE_BYTES

#define IFETCH_ALIGN_BYTES	(1 << IFETCH_ALIGN_SHIFT)

#if !defined(__ASSEMBLY__)
#ifdef CONFIG_PPC64

struct ppc_cache_info {
	u32 size;
	u32 line_size;
	u32 block_size;	/* L1 only */
	u32 log_block_size;
	u32 blocks_per_page;
	u32 sets;
	u32 assoc;
};

struct ppc64_caches {
	struct ppc_cache_info l1d;
	struct ppc_cache_info l1i;
	struct ppc_cache_info l2;
	struct ppc_cache_info l3;
};

extern struct ppc64_caches ppc64_caches;

static inline u32 l1_dcache_shift(void)
{
	return ppc64_caches.l1d.log_block_size;
}

static inline u32 l1_dcache_bytes(void)
{
	return ppc64_caches.l1d.block_size;
}

static inline u32 l1_icache_shift(void)
{
	return ppc64_caches.l1i.log_block_size;
}

static inline u32 l1_icache_bytes(void)
{
	return ppc64_caches.l1i.block_size;
}
#else
static inline u32 l1_dcache_shift(void)
{
	return L1_CACHE_SHIFT;
}

static inline u32 l1_dcache_bytes(void)
{
	return L1_CACHE_BYTES;
}

static inline u32 l1_icache_shift(void)
{
	return L1_CACHE_SHIFT;
}

static inline u32 l1_icache_bytes(void)
{
	return L1_CACHE_BYTES;
}

#endif
#endif /* ! __ASSEMBLY__ */

#if defined(__ASSEMBLY__)
/*
 * For a snooping icache, we still need a dummy icbi to purge all the
 * prefetched instructions from the ifetch buffers. We also need a sync
 * before the icbi to order the the actual stores to memory that might
 * have modified instructions with the icbi.
 */
#define PURGE_PREFETCHED_INS	\
	sync;			\
	icbi	0,r3;		\
	sync;			\
	isync

#else
#define __read_mostly __attribute__((__section__(".data..read_mostly")))

#ifdef CONFIG_PPC_BOOK3S_32
extern long _get_L2CR(void);
extern long _get_L3CR(void);
extern void _set_L2CR(unsigned long);
extern void _set_L3CR(unsigned long);
#else
#define _get_L2CR()	0L
#define _get_L3CR()	0L
#define _set_L2CR(val)	do { } while(0)
#define _set_L3CR(val)	do { } while(0)
#endif

static inline void dcbz(void *addr)
{
	__asm__ __volatile__ ("dcbz 0, %0" : : "r"(addr) : "memory");
}

static inline void dcbi(void *addr)
{
	__asm__ __volatile__ ("dcbi 0, %0" : : "r"(addr) : "memory");
}

static inline void dcbf(void *addr)
{
	__asm__ __volatile__ ("dcbf 0, %0" : : "r"(addr) : "memory");
}

static inline void dcbst(void *addr)
{
	__asm__ __volatile__ ("dcbst 0, %0" : : "r"(addr) : "memory");
}
#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_CACHE_H */
