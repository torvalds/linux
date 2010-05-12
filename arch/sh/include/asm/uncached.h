#ifndef __ASM_SH_UNCACHED_H
#define __ASM_SH_UNCACHED_H

#include <linux/bug.h>

#ifdef CONFIG_UNCACHED_MAPPING
extern unsigned long uncached_start, uncached_end;

extern int virt_addr_uncached(unsigned long kaddr);
extern void uncached_init(void);
extern void uncached_resize(unsigned long size);
#else
#define virt_addr_uncached(kaddr)	(0)
#define uncached_init()			do { } while (0)
#define uncached_resize(size)		BUG()
#endif

#endif /* __ASM_SH_UNCACHED_H */
