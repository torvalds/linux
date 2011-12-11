#ifndef __OMAP_SECURE_H__
#define __OMAP_SECURE_H__

#include <linux/types.h>

#ifdef CONFIG_ARCH_OMAP2PLUS
extern int omap_secure_ram_reserve_memblock(void);
#else
static inline void omap_secure_ram_reserve_memblock(void)
{ }
#endif

#endif /* __OMAP_SECURE_H__ */
