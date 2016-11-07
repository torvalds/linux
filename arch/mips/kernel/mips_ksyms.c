/*
 * Export MIPS-specific functions needed for loadable modules.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 98, 99, 2000, 01, 03, 04, 05, 12 by Ralf Baechle
 * Copyright (C) 1999, 2000, 01 Silicon Graphics, Inc.
 */
#include <linux/interrupt.h>
#include <linux/export.h>
#include <asm/checksum.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/ftrace.h>
#include <asm/fpu.h>
#include <asm/msa.h>

extern void *__bzero_kernel(void *__s, size_t __count);
extern void *__bzero(void *__s, size_t __count);
extern long __strncpy_from_kernel_nocheck_asm(char *__to,
					      const char *__from, long __len);
extern long __strncpy_from_kernel_asm(char *__to, const char *__from,
				      long __len);
extern long __strncpy_from_user_nocheck_asm(char *__to,
					    const char *__from, long __len);
extern long __strncpy_from_user_asm(char *__to, const char *__from,
				    long __len);
extern long __strlen_kernel_asm(const char *s);
extern long __strlen_user_asm(const char *s);
extern long __strnlen_kernel_nocheck_asm(const char *s);
extern long __strnlen_kernel_asm(const char *s);
extern long __strnlen_user_nocheck_asm(const char *s);
extern long __strnlen_user_asm(const char *s);

/*
 * String functions
 */
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);

/*
 * Functions that operate on entire pages.  Mostly used by memory management.
 */
EXPORT_SYMBOL(clear_page);
EXPORT_SYMBOL(copy_page);

/*
 * Userspace access stuff.
 */
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(__copy_user_inatomic);
#ifdef CONFIG_EVA
EXPORT_SYMBOL(__copy_from_user_eva);
EXPORT_SYMBOL(__copy_in_user_eva);
EXPORT_SYMBOL(__copy_to_user_eva);
EXPORT_SYMBOL(__copy_user_inatomic_eva);
EXPORT_SYMBOL(__bzero_kernel);
#endif
EXPORT_SYMBOL(__bzero);
EXPORT_SYMBOL(__strncpy_from_kernel_nocheck_asm);
EXPORT_SYMBOL(__strncpy_from_kernel_asm);
EXPORT_SYMBOL(__strncpy_from_user_nocheck_asm);
EXPORT_SYMBOL(__strncpy_from_user_asm);
EXPORT_SYMBOL(__strlen_kernel_asm);
EXPORT_SYMBOL(__strlen_user_asm);
EXPORT_SYMBOL(__strnlen_kernel_nocheck_asm);
EXPORT_SYMBOL(__strnlen_kernel_asm);
EXPORT_SYMBOL(__strnlen_user_nocheck_asm);
EXPORT_SYMBOL(__strnlen_user_asm);
