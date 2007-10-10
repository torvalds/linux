/*
 * File:         arch/blackfin/kernel/bfin_ksyms.c
 * Based on:     none - original work
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/uaccess.h>

#include <asm/checksum.h>
#include <asm/cacheflush.h>

/* platform dependent support */

EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(dump_thread);

EXPORT_SYMBOL(ip_fast_csum);

EXPORT_SYMBOL(kernel_thread);

EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_trylock);
EXPORT_SYMBOL(__down_interruptible);

EXPORT_SYMBOL(is_in_rom);

/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy);

/* The following are special because they're not called
 * explicitly (the C compiler generates them).  Fortunately,
 * their interface isn't gonna change any time soon now, so
 * it's OK to leave it out of version control.
 */
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memchr);
EXPORT_SYMBOL(get_wchan);

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

/* gcc lib functions */
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

EXPORT_SYMBOL(outsb);
EXPORT_SYMBOL(insb);
EXPORT_SYMBOL(outsw);
EXPORT_SYMBOL(insw);
EXPORT_SYMBOL(outsl);
EXPORT_SYMBOL(insl);
EXPORT_SYMBOL(irq_flags);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(blackfin_dcache_invalidate_range);
EXPORT_SYMBOL(blackfin_icache_dcache_flush_range);
EXPORT_SYMBOL(blackfin_icache_flush_range);
EXPORT_SYMBOL(blackfin_dcache_flush_range);
EXPORT_SYMBOL(blackfin_dflush_page);

EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(__init_begin);
EXPORT_SYMBOL(__init_end);
EXPORT_SYMBOL(_ebss_l1);
EXPORT_SYMBOL(_stext_l1);
EXPORT_SYMBOL(_etext_l1);
EXPORT_SYMBOL(_sdata_l1);
EXPORT_SYMBOL(_ebss_b_l1);
EXPORT_SYMBOL(_sdata_b_l1);
