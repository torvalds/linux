/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh64/kernel/sh_ksyms.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
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

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/irq.h>

extern int dump_fpu(struct pt_regs *, elf_fpregset_t *);

/* platform dependent support */
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(kernel_thread);

/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy_nocheck);

EXPORT_SYMBOL(strstr);

#ifdef CONFIG_VT
EXPORT_SYMBOL(screen_info);
#endif

EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_trylock);
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__put_user_asm_l);
EXPORT_SYMBOL(__get_user_asm_l);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strlen);

EXPORT_SYMBOL(flush_dcache_page);

/* For ext3 */
EXPORT_SYMBOL(sh64_page_clear);

/* Ugh.  These come in from libgcc.a at link time. */

extern void __sdivsi3(void);
extern void __muldi3(void);
extern void __udivsi3(void);
extern char __div_table;
EXPORT_SYMBOL(__sdivsi3);
EXPORT_SYMBOL(__muldi3);
EXPORT_SYMBOL(__udivsi3);
EXPORT_SYMBOL(__div_table);


