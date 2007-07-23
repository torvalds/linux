/*
 *  linux/arch/arm26/kernel/armksyms.c
 *
 *  Copyright (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/module.h>
#include <linux/user.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/delay.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/syscalls.h>

#include <asm/byteorder.h>
#include <asm/elf.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include <asm/semaphore.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/mach-types.h>

extern int dump_fpu(struct pt_regs *, struct user_fp_struct *);
extern void inswb(unsigned int port, void *to, int len);
extern void outswb(unsigned int port, const void *to, int len);

extern void __bad_xchg(volatile void *ptr, int size);

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
extern void __udivdi3(void);
extern void __umoddi3(void);
extern void __udivmoddi4(void);
extern void __udivsi3(void);
extern void __umodsi3(void);
extern void abort(void);

extern void ret_from_exception(void);
extern void fpundefinstr(void);
extern void fp_enter(void);

/*
 * This has a special calling convention; it doesn't
 * modify any of the usual registers, except for LR.
 * FIXME - we used to use our own local version - looks to be in kernel/softirq now
 */
//extern void __do_softirq(void);

#define EXPORT_SYMBOL_ALIAS(sym,orig)		\
 const char __kstrtab_##sym[]			\
  __attribute__((section(".kstrtab"))) =	\
    __MODULE_STRING(sym);			\
 const struct module_symbol __ksymtab_##sym	\
  __attribute__((section("__ksymtab"))) =	\
    { (unsigned long)&orig, __kstrtab_##sym };

/*
 * floating point math emulator support.
 * These symbols will never change their calling convention...
 */
EXPORT_SYMBOL_ALIAS(kern_fp_enter,fp_enter);
EXPORT_SYMBOL_ALIAS(fp_printk,printk);
EXPORT_SYMBOL_ALIAS(fp_send_sig,send_sig);

EXPORT_SYMBOL(fpundefinstr);
EXPORT_SYMBOL(ret_from_exception);

#ifdef CONFIG_VT
EXPORT_SYMBOL(kd_mksound);
#endif

//EXPORT_SYMBOL(__do_softirq);

	/* platform dependent support */
EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(udelay);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(system_rev);
EXPORT_SYMBOL(system_serial_low);
EXPORT_SYMBOL(system_serial_high);
#ifdef CONFIG_DEBUG_BUGVERBOSE
EXPORT_SYMBOL(__bug);
#endif
EXPORT_SYMBOL(__bad_xchg);
EXPORT_SYMBOL(__readwrite_bug);
EXPORT_SYMBOL(set_irq_type);
EXPORT_SYMBOL(pm_idle);
EXPORT_SYMBOL(pm_power_off);

	/* processor dependencies */
EXPORT_SYMBOL(__machine_arch_type);

	/* networking */
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
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(__memzero);

	/* user mem (segment) */
EXPORT_SYMBOL(uaccess_kernel);
EXPORT_SYMBOL(uaccess_user);

EXPORT_SYMBOL(__get_user_1);
EXPORT_SYMBOL(__get_user_2);
EXPORT_SYMBOL(__get_user_4);
EXPORT_SYMBOL(__get_user_8);

EXPORT_SYMBOL(__put_user_1);
EXPORT_SYMBOL(__put_user_2);
EXPORT_SYMBOL(__put_user_4);
EXPORT_SYMBOL(__put_user_8);

	/* gcc lib functions */
EXPORT_SYMBOL(__ashldi3);
EXPORT_SYMBOL(__ashrdi3);
EXPORT_SYMBOL(__divsi3);
EXPORT_SYMBOL(__lshrdi3);
EXPORT_SYMBOL(__modsi3);
EXPORT_SYMBOL(__muldi3);
EXPORT_SYMBOL(__ucmpdi2);
EXPORT_SYMBOL(__udivdi3);
EXPORT_SYMBOL(__umoddi3);
EXPORT_SYMBOL(__udivmoddi4);
EXPORT_SYMBOL(__udivsi3);
EXPORT_SYMBOL(__umodsi3);

	/* bitops */
EXPORT_SYMBOL(_set_bit_le);
EXPORT_SYMBOL(_test_and_set_bit_le);
EXPORT_SYMBOL(_clear_bit_le);
EXPORT_SYMBOL(_test_and_clear_bit_le);
EXPORT_SYMBOL(_change_bit_le);
EXPORT_SYMBOL(_test_and_change_bit_le);
EXPORT_SYMBOL(_find_first_zero_bit_le);
EXPORT_SYMBOL(_find_next_zero_bit_le);

	/* elf */
EXPORT_SYMBOL(elf_platform);
EXPORT_SYMBOL(elf_hwcap);

#ifdef CONFIG_PREEMPT
EXPORT_SYMBOL(kernel_flag);
#endif
