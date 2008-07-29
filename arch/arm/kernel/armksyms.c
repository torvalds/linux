/*
 *  linux/arch/arm/kernel/armksyms.c
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/cryptohash.h>
#include <linux/delay.h>
#include <linux/in6.h>
#include <linux/syscalls.h>

#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/ftrace.h>

/*
 * libgcc functions - functions that are used internally by the
 * compiler...  (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __divsi3(void);
extern void __lshrdi3(void);
extern void __modsi3(void);
extern void __muldi3(void);
extern void __ucmpdi2(void);
extern void __udivsi3(void);
extern void __umodsi3(void);
extern void __do_div64(void);

extern void __aeabi_idiv(void);
extern void __aeabi_idivmod(void);
extern void __aeabi_lasr(void);
extern void __aeabi_llsl(void);
extern void __aeabi_llsr(void);
extern void __aeabi_lmul(void);
extern void __aeabi_uidiv(void);
extern void __aeabi_uidivmod(void);
extern void __aeabi_ulcmp(void);

extern void fpundefinstr(void);
extern void fp_enter(void);

/*
 * This has a special calling convention; it doesn't
 * modify any of the usual registers, except for LR.
 */
#define EXPORT_CRC_ALIAS(sym) __CRC_SYMBOL(sym, "")

#define EXPORT_SYMBOL_ALIAS(sym,orig)		\
 EXPORT_CRC_ALIAS(sym)				\
 static const struct kernel_symbol __ksymtab_##sym	\
  __used __attribute__((section("__ksymtab"))) =	\
    { (unsigned long)&orig, #sym };

/*
 * floating point math emulator support.
 * These symbols will never change their calling convention...
 */
EXPORT_SYMBOL_ALIAS(kern_fp_enter,fp_enter);
EXPORT_SYMBOL_ALIAS(fp_printk,printk);
EXPORT_SYMBOL_ALIAS(fp_send_sig,send_sig);

EXPORT_SYMBOL(__backtrace);

	/* platform dependent support */
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__const_udelay);

	/* networking */
EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_from_user);
EXPORT_SYMBOL(csum_partial_copy_nocheck);
EXPORT_SYMBOL(__csum_ipv6_magic);

	/* io */
#ifndef __raw_readsb
EXPORT_SYMBOL(__raw_readsb);
#endif
#ifndef __raw_readsw
EXPORT_SYMBOL(__raw_readsw);
#endif
#ifndef __raw_readsl
EXPORT_SYMBOL(__raw_readsl);
#endif
#ifndef __raw_writesb
EXPORT_SYMBOL(__raw_writesb);
#endif
#ifndef __raw_writesw
EXPORT_SYMBOL(__raw_writesw);
#endif
#ifndef __raw_writesl
EXPORT_SYMBOL(__raw_writesl);
#endif

	/* string / mem functions */
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memchr);
EXPORT_SYMBOL(__memzero);

	/* user mem (segment) */
EXPORT_SYMBOL(__strnlen_user);
EXPORT_SYMBOL(__strncpy_from_user);

#ifdef CONFIG_MMU
EXPORT_SYMBOL(__copy_from_user);
EXPORT_SYMBOL(__copy_to_user);
EXPORT_SYMBOL(__clear_user);

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);

EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);
#endif

	/* crypto hash */
EXPORT_SYMBOL(sha_transform);

	/* gcc lib functions */
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__divsi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__modsi3);
EXPORT_SYMBOL(__muldi3);
EXPORT_SYMBOL(__ucmpdi2);
EXPORT_SYMBOL(__udivsi3);
EXPORT_SYMBOL(__umodsi3);
EXPORT_SYMBOL(__do_div64);

#ifdef CONFIG_AEABI
EXPORT_SYMBOL(__aeabi_idiv);
EXPORT_SYMBOL(__aeabi_idivmod);
EXPORT_SYMBOL(__aeabi_lasr);
EXPORT_SYMBOL(__aeabi_llsl);
EXPORT_SYMBOL(__aeabi_llsr);
EXPORT_SYMBOL(__aeabi_lmul);
EXPORT_SYMBOL(__aeabi_uidiv);
EXPORT_SYMBOL(__aeabi_uidivmod);
EXPORT_SYMBOL(__aeabi_ulcmp);
#endif

	/* bitops */
EXPORT_SYMBOL(_set_bit_le);
EXPORT_SYMBOL(_test_and_set_bit_le);
EXPORT_SYMBOL(_clear_bit_le);
EXPORT_SYMBOL(_test_and_clear_bit_le);
EXPORT_SYMBOL(_change_bit_le);
EXPORT_SYMBOL(_test_and_change_bit_le);
EXPORT_SYMBOL(_find_first_zero_bit_le);
EXPORT_SYMBOL(_find_next_zero_bit_le);
EXPORT_SYMBOL(_find_first_bit_le);
EXPORT_SYMBOL(_find_next_bit_le);

#ifdef __ARMEB__
EXPORT_SYMBOL(_set_bit_be);
EXPORT_SYMBOL(_test_and_set_bit_be);
EXPORT_SYMBOL(_clear_bit_be);
EXPORT_SYMBOL(_test_and_clear_bit_be);
EXPORT_SYMBOL(_change_bit_be);
EXPORT_SYMBOL(_test_and_change_bit_be);
EXPORT_SYMBOL(_find_first_zero_bit_be);
EXPORT_SYMBOL(_find_next_zero_bit_be);
EXPORT_SYMBOL(_find_first_bit_be);
EXPORT_SYMBOL(_find_next_bit_be);
#endif

EXPORT_SYMBOL(copy_page);

#ifdef CONFIG_FTRACE
EXPORT_SYMBOL(mcount);
#endif
