/*
 * arch/sh/kernel/sh_ksyms_64.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/rwsem.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/screen_info.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/irq.h>

extern int dump_fpu(struct pt_regs *, elf_fpregset_t *);

/* platform dependent support */
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(kernel_thread);

#if !defined(CONFIG_CACHE_OFF) && defined(CONFIG_MMU)
EXPORT_SYMBOL(clear_user_page);
#endif

#ifndef CONFIG_CACHE_OFF
EXPORT_SYMBOL(flush_dcache_page);
#endif

/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_nocheck);
#ifdef CONFIG_IPV6
EXPORT_SYMBOL(csum_ipv6_magic);
#endif

#ifdef CONFIG_VT
EXPORT_SYMBOL(screen_info);
#endif

EXPORT_SYMBOL(__put_user_asm_b);
EXPORT_SYMBOL(__put_user_asm_w);
EXPORT_SYMBOL(__put_user_asm_l);
EXPORT_SYMBOL(__put_user_asm_q);
EXPORT_SYMBOL(__get_user_asm_b);
EXPORT_SYMBOL(__get_user_asm_w);
EXPORT_SYMBOL(__get_user_asm_l);
EXPORT_SYMBOL(__get_user_asm_q);
EXPORT_SYMBOL(__strnlen_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(clear_page);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(copy_page);
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__ndelay);
EXPORT_SYMBOL(__const_udelay);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strcpy);

/* Ugh.  These come in from libgcc.a at link time. */
#define DECLARE_EXPORT(name) extern void name(void);EXPORT_SYMBOL(name)

DECLARE_EXPORT(__sdivsi3);
DECLARE_EXPORT(__udivsi3);
DECLARE_EXPORT(__div_table);
