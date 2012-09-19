#ifndef __OMAP_SECURE_H__
#define __OMAP_SECURE_H__

#include <linux/types.h>

extern int omap_secure_ram_reserve_memblock(void);

#ifdef CONFIG_OMAP4_ERRATA_I688
extern int omap_barrier_reserve_memblock(void);
#else
static inline void omap_barrier_reserve_memblock(void)
{ }
#endif
#endif /* __OMAP_SECURE_H__ */
