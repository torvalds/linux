#ifndef _METAG_L2CACHE_H
#define _METAG_L2CACHE_H

#ifdef CONFIG_METAG_L2C

#include <asm/global_lock.h>
#include <asm/io.h>

/*
 * Store the last known value of pfenable (we don't want prefetch enabled while
 * L2 is off).
 */
extern int l2c_pfenable;

/* defined in arch/metag/drivers/core-sysfs.c */
extern struct sysdev_class cache_sysclass;

static inline void wr_fence(void);

/*
 * Functions for reading of L2 cache configuration.
 */

/* Get raw L2 config register (CORE_CONFIG3) */
static inline unsigned int meta_l2c_config(void)
{
	const unsigned int *corecfg3 = (const unsigned int *)METAC_CORE_CONFIG3;
	return *corecfg3;
}

/* Get whether the L2 is present */
static inline int meta_l2c_is_present(void)
{
	return meta_l2c_config() & METAC_CORECFG3_L2C_HAVE_L2C_BIT;
}

/* Get whether the L2 is configured for write-back instead of write-through */
static inline int meta_l2c_is_writeback(void)
{
	return meta_l2c_config() & METAC_CORECFG3_L2C_MODE_BIT;
}

/* Get whether the L2 is unified instead of separated code/data */
static inline int meta_l2c_is_unified(void)
{
	return meta_l2c_config() & METAC_CORECFG3_L2C_UNIFIED_BIT;
}

/* Get the L2 cache size in bytes */
static inline unsigned int meta_l2c_size(void)
{
	unsigned int size_s;
	if (!meta_l2c_is_present())
		return 0;
	size_s = (meta_l2c_config() & METAC_CORECFG3_L2C_SIZE_BITS)
			>> METAC_CORECFG3_L2C_SIZE_S;
	/* L2CSIZE is in KiB */
	return 1024 << size_s;
}

/* Get the number of ways in the L2 cache */
static inline unsigned int meta_l2c_ways(void)
{
	unsigned int ways_s;
	if (!meta_l2c_is_present())
		return 0;
	ways_s = (meta_l2c_config() & METAC_CORECFG3_L2C_NUM_WAYS_BITS)
			>> METAC_CORECFG3_L2C_NUM_WAYS_S;
	return 0x1 << ways_s;
}

/* Get the line size of the L2 cache */
static inline unsigned int meta_l2c_linesize(void)
{
	unsigned int line_size;
	if (!meta_l2c_is_present())
		return 0;
	line_size = (meta_l2c_config() & METAC_CORECFG3_L2C_LINE_SIZE_BITS)
			>> METAC_CORECFG3_L2C_LINE_SIZE_S;
	switch (line_size) {
	case METAC_CORECFG3_L2C_LINE_SIZE_64B:
		return 64;
	default:
		return 0;
	}
}

/* Get the revision ID of the L2 cache */
static inline unsigned int meta_l2c_revision(void)
{
	return (meta_l2c_config() & METAC_CORECFG3_L2C_REV_ID_BITS)
			>> METAC_CORECFG3_L2C_REV_ID_S;
}


/*
 * Start an initialisation of the L2 cachelines and wait for completion.
 * This should only be done in a LOCK1 or LOCK2 critical section while the L2
 * is disabled.
 */
static inline void _meta_l2c_init(void)
{
	metag_out32(SYSC_L2C_INIT_INIT, SYSC_L2C_INIT);
	while (metag_in32(SYSC_L2C_INIT) == SYSC_L2C_INIT_IN_PROGRESS)
		/* do nothing */;
}

/*
 * Start a writeback of dirty L2 cachelines and wait for completion.
 * This should only be done in a LOCK1 or LOCK2 critical section.
 */
static inline void _meta_l2c_purge(void)
{
	metag_out32(SYSC_L2C_PURGE_PURGE, SYSC_L2C_PURGE);
	while (metag_in32(SYSC_L2C_PURGE) == SYSC_L2C_PURGE_IN_PROGRESS)
		/* do nothing */;
}

/* Set whether the L2 cache is enabled. */
static inline void _meta_l2c_enable(int enabled)
{
	unsigned int enable;

	enable = metag_in32(SYSC_L2C_ENABLE);
	if (enabled)
		enable |= SYSC_L2C_ENABLE_ENABLE_BIT;
	else
		enable &= ~SYSC_L2C_ENABLE_ENABLE_BIT;
	metag_out32(enable, SYSC_L2C_ENABLE);
}

/* Set whether the L2 cache prefetch is enabled. */
static inline void _meta_l2c_pf_enable(int pfenabled)
{
	unsigned int enable;

	enable = metag_in32(SYSC_L2C_ENABLE);
	if (pfenabled)
		enable |= SYSC_L2C_ENABLE_PFENABLE_BIT;
	else
		enable &= ~SYSC_L2C_ENABLE_PFENABLE_BIT;
	metag_out32(enable, SYSC_L2C_ENABLE);
}

/* Return whether the L2 cache is enabled */
static inline int _meta_l2c_is_enabled(void)
{
	return metag_in32(SYSC_L2C_ENABLE) & SYSC_L2C_ENABLE_ENABLE_BIT;
}

/* Return whether the L2 cache prefetch is enabled */
static inline int _meta_l2c_pf_is_enabled(void)
{
	return metag_in32(SYSC_L2C_ENABLE) & SYSC_L2C_ENABLE_PFENABLE_BIT;
}


/* Return whether the L2 cache is enabled */
static inline int meta_l2c_is_enabled(void)
{
	int en;

	/*
	 * There is no need to lock at the moment, as the enable bit is never
	 * intermediately changed, so we will never see an intermediate result.
	 */
	en = _meta_l2c_is_enabled();

	return en;
}

/*
 * Ensure the L2 cache is disabled.
 * Return whether the L2 was previously disabled.
 */
int meta_l2c_disable(void);

/*
 * Ensure the L2 cache is enabled.
 * Return whether the L2 was previously enabled.
 */
int meta_l2c_enable(void);

/* Return whether the L2 cache prefetch is enabled */
static inline int meta_l2c_pf_is_enabled(void)
{
	return l2c_pfenable;
}

/*
 * Set whether the L2 cache prefetch is enabled.
 * Return whether the L2 prefetch was previously enabled.
 */
int meta_l2c_pf_enable(int pfenable);

/*
 * Flush the L2 cache.
 * Return 1 if the L2 is disabled.
 */
int meta_l2c_flush(void);

/*
 * Write back all dirty cache lines in the L2 cache.
 * Return 1 if the L2 is disabled or there isn't any writeback.
 */
static inline int meta_l2c_writeback(void)
{
	unsigned long flags;
	int en;

	/* no need to purge if it's not a writeback cache */
	if (!meta_l2c_is_writeback())
		return 1;

	/*
	 * Purge only works if the L2 is enabled, and involves reading back to
	 * detect completion, so keep this operation atomic with other threads.
	 */
	__global_lock1(flags);
	en = meta_l2c_is_enabled();
	if (likely(en)) {
		wr_fence();
		_meta_l2c_purge();
	}
	__global_unlock1(flags);

	return !en;
}

#else /* CONFIG_METAG_L2C */

#define meta_l2c_config()		0
#define meta_l2c_is_present()		0
#define meta_l2c_is_writeback()		0
#define meta_l2c_is_unified()		0
#define meta_l2c_size()			0
#define meta_l2c_ways()			0
#define meta_l2c_linesize()		0
#define meta_l2c_revision()		0

#define meta_l2c_is_enabled()		0
#define _meta_l2c_pf_is_enabled()	0
#define meta_l2c_pf_is_enabled()	0
#define meta_l2c_disable()		1
#define meta_l2c_enable()		0
#define meta_l2c_pf_enable(X)		0
static inline int meta_l2c_flush(void)
{
	return 1;
}
static inline int meta_l2c_writeback(void)
{
	return 1;
}

#endif /* CONFIG_METAG_L2C */

#endif /* _METAG_L2CACHE_H */
