/* SMP caching definitions
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */


/*
 * Operation requests for smp_cache_call().
 *
 * One of smp_icache_ops and one of smp_dcache_ops can be OR'd together.
 */
enum smp_icache_ops {
	SMP_ICACHE_NOP			= 0x0000,
	SMP_ICACHE_INV			= 0x0001,
	SMP_ICACHE_INV_RANGE		= 0x0002,
};
#define SMP_ICACHE_OP_MASK		0x0003

enum smp_dcache_ops {
	SMP_DCACHE_NOP			= 0x0000,
	SMP_DCACHE_INV			= 0x0004,
	SMP_DCACHE_INV_RANGE		= 0x0008,
	SMP_DCACHE_FLUSH		= 0x000c,
	SMP_DCACHE_FLUSH_RANGE		= 0x0010,
	SMP_DCACHE_FLUSH_INV		= 0x0014,
	SMP_DCACHE_FLUSH_INV_RANGE	= 0x0018,
};
#define SMP_DCACHE_OP_MASK		0x001c

#define	SMP_IDCACHE_INV_FLUSH		(SMP_ICACHE_INV | SMP_DCACHE_FLUSH)
#define SMP_IDCACHE_INV_FLUSH_RANGE	(SMP_ICACHE_INV_RANGE | SMP_DCACHE_FLUSH_RANGE)

/*
 * cache-smp.c
 */
#ifdef CONFIG_SMP
extern spinlock_t smp_cache_lock;

extern void smp_cache_call(unsigned long opr_mask,
			   unsigned long addr, unsigned long end);

static inline unsigned long smp_lock_cache(void)
	__acquires(&smp_cache_lock)
{
	unsigned long flags;
	spin_lock_irqsave(&smp_cache_lock, flags);
	return flags;
}

static inline void smp_unlock_cache(unsigned long flags)
	__releases(&smp_cache_lock)
{
	spin_unlock_irqrestore(&smp_cache_lock, flags);
}

#else
static inline unsigned long smp_lock_cache(void) { return 0; }
static inline void smp_unlock_cache(unsigned long flags) {}
static inline void smp_cache_call(unsigned long opr_mask,
				  unsigned long addr, unsigned long end)
{
}
#endif /* CONFIG_SMP */
