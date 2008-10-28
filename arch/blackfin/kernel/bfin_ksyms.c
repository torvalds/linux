/*
 * arch/blackfin/kernel/bfin_ksyms.c - exports for random symbols
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>

/* Allow people to have their own Blackfin exception handler in a module */
EXPORT_SYMBOL(bfin_return_from_exception);

/* All the Blackfin cache functions: mach-common/cache.S */
EXPORT_SYMBOL(blackfin_dcache_invalidate_range);
EXPORT_SYMBOL(blackfin_icache_dcache_flush_range);
EXPORT_SYMBOL(blackfin_icache_flush_range);
EXPORT_SYMBOL(blackfin_dcache_flush_range);
EXPORT_SYMBOL(blackfin_dflush_page);

/* The following are special because they're not called
 * explicitly (the C compiler generates them).  Fortunately,
 * their interface isn't gonna change any time soon now, so
 * it's OK to leave it out of version control.
 */
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memchr);

/*
 * libgcc functions - functions that are used internally by the
 * compiler...  (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __smulsi3_highpart(void);
extern void __umulsi3_highpart(void);
extern void __divsi3(void);
extern void __lshrdi3(void);
extern void __modsi3(void);
extern void __muldi3(void);
extern void __udivsi3(void);
extern void __umodsi3(void);
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__umulsi3_highpart);
EXPORT_SYMBOL(__smulsi3_highpart);
EXPORT_SYMBOL(__divsi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__modsi3);
EXPORT_SYMBOL(__muldi3);
EXPORT_SYMBOL(__udivsi3);
EXPORT_SYMBOL(__umodsi3);

/* Input/output symbols: lib/{in,out}s.S */
EXPORT_SYMBOL(outsb);
EXPORT_SYMBOL(insb);
EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(outsw_8);
EXPORT_SYMBOL(insw);
EXPORT_SYMBOL(insw_8);
EXPORT_SYMBOL(outsl);
EXPORT_SYMBOL(insl);
EXPORT_SYMBOL(insl_16);
