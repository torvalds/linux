// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/kernel/armksyms.c
 *
 *  Copyright (C) 2000 Russell King
 */
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/cryptohash.h>
#include <linux/delay.h>
#include <linux/in6.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>

#include <asm/checksum.h>
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
extern void __bswapsi2(void);
extern void __bswapdi2(void);

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

void mmioset(void *, unsigned int, size_t);
void mmiocpy(void *, const void *, size_t);

	/* platform dependent support */
EXPORT_SYMBOL(arm_delay_ops);

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
EXPORT_SYMBOL(__memset32);
EXPORT_SYMBOL(__memset64);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memchr);

EXPORT_SYMBOL(mmioset);
EXPORT_SYMBOL(mmiocpy);

#ifdef CONFIG_MMU
EXPORT_SYMBOL(copy_page);

EXPORT_SYMBOL(arm_copy_from_user);
EXPORT_SYMBOL(arm_copy_to_user);
EXPORT_SYMBOL(arm_clear_user);

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);
EXPORT_SYMBOL(__get_user_8);

#ifdef __ARMEB__
EXPORT_SYMBOL(__get_user_64t_1);
EXPORT_SYMBOL(__get_user_64t_2);
EXPORT_SYMBOL(__get_user_64t_4);
EXPORT_SYMBOL(__get_user_32t_8);
#endif

EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);
#endif

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
EXPORT_SYMBOL(__bswapsi2);
EXPORT_SYMBOL(__bswapdi2);

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
EXPORT_SYMBOL(_set_bit);
EXPORT_SYMBOL(_test_and_set_bit);
EXPORT_SYMBOL(_clear_bit);
EXPORT_SYMBOL(_test_and_clear_bit);
EXPORT_SYMBOL(_change_bit);
EXPORT_SYMBOL(_test_and_change_bit);
EXPORT_SYMBOL(_find_first_zero_bit_le);
EXPORT_SYMBOL(_find_next_zero_bit_le);
EXPORT_SYMBOL(_find_first_bit_le);
EXPORT_SYMBOL(_find_next_bit_le);

#ifdef __ARMEB__
EXPORT_SYMBOL(_find_first_zero_bit_be);
EXPORT_SYMBOL(_find_next_zero_bit_be);
EXPORT_SYMBOL(_find_first_bit_be);
EXPORT_SYMBOL(_find_next_bit_be);
#endif

#ifdef CONFIG_FUNCTION_TRACER
EXPORT_SYMBOL(__gnu_mcount_nc);
#endif

#ifdef CONFIG_ARM_PATCH_PHYS_VIRT
EXPORT_SYMBOL(__pv_phys_pfn_offset);
EXPORT_SYMBOL(__pv_offset);
#endif

#ifdef CONFIG_HAVE_ARM_SMCCC
EXPORT_SYMBOL(__arm_smccc_smc);
EXPORT_SYMBOL(__arm_smccc_hvc);
#endif
