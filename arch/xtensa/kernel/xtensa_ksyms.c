/*
 * arch/xtensa/kernel/xtensa_ksyms.c
 *
 * Export Xtensa-specific functions for loadable modules.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005  Tensilica Inc.
 *
 * Joe Taylor <joe@tensilica.com>
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/in6.h>

#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/checksum.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/ftrace.h>
#ifdef CONFIG_BLK_DEV_FD
#include <asm/floppy.h>
#endif
#ifdef CONFIG_NET
#include <net/checksum.h>
#endif /* CONFIG_NET */


/*
 * String functions
 */
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(__memset);
EXPORT_SYMBOL(__memcpy);
EXPORT_SYMBOL(__memmove);
#ifdef CONFIG_ARCH_HAS_STRNCPY_FROM_USER
EXPORT_SYMBOL(__strncpy_user);
#endif
EXPORT_SYMBOL(clear_page);
EXPORT_SYMBOL(copy_page);

EXPORT_SYMBOL(empty_zero_page);

/*
 * gcc internal math functions
 */
extern long long __ashrdi3(long long, int);
extern long long __ashldi3(long long, int);
extern long long __bswapdi2(long long);
extern int __bswapsi2(int);
extern long long __lshrdi3(long long, int);
extern int __divsi3(int, int);
extern int __modsi3(int, int);
extern int __mulsi3(int, int);
extern unsigned int __udivsi3(unsigned int, unsigned int);
extern unsigned int __umodsi3(unsigned int, unsigned int);
extern unsigned long long __umulsidi3(unsigned int, unsigned int);

EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__bswapdi2);
EXPORT_SYMBOL(__bswapsi2);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__divsi3);
EXPORT_SYMBOL(__modsi3);
EXPORT_SYMBOL(__mulsi3);
EXPORT_SYMBOL(__udivsi3);
EXPORT_SYMBOL(__umodsi3);
EXPORT_SYMBOL(__umulsidi3);

unsigned int __sync_fetch_and_and_4(volatile void *p, unsigned int v)
{
	BUG();
}
EXPORT_SYMBOL(__sync_fetch_and_and_4);

unsigned int __sync_fetch_and_or_4(volatile void *p, unsigned int v)
{
	BUG();
}
EXPORT_SYMBOL(__sync_fetch_and_or_4);

/*
 * Networking support
 */
EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_generic);

/*
 * Architecture-specific symbols
 */
EXPORT_SYMBOL(__xtensa_copy_user);
EXPORT_SYMBOL(__invalidate_icache_range);

/*
 * Kernel hacking ...
 */

#if defined(CONFIG_VGA_CONSOLE) || defined(CONFIG_DUMMY_CONSOLE)
// FIXME EXPORT_SYMBOL(screen_info);
#endif

extern long common_exception_return;
EXPORT_SYMBOL(common_exception_return);

#ifdef CONFIG_FUNCTION_TRACER
EXPORT_SYMBOL(_mcount);
#endif

EXPORT_SYMBOL(__invalidate_dcache_range);
#if XCHAL_DCACHE_IS_WRITEBACK
EXPORT_SYMBOL(__flush_dcache_range);
#endif
