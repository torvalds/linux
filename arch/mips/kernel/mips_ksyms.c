/*
 * Export MIPS-specific functions needed for loadable modules.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 98, 99, 2000, 01, 03, 04, 05 by Ralf Baechle
 * Copyright (C) 1999, 2000, 01 Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/checksum.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

extern void *__bzero(void *__s, size_t __count);
extern long __strncpy_from_user_nocheck_asm(char *__to,
                                            const char *__from, long __len);
extern long __strncpy_from_user_asm(char *__to, const char *__from,
                                    long __len);
extern long __strlen_user_nocheck_asm(const char *s);
extern long __strlen_user_asm(const char *s);
extern long __strnlen_user_nocheck_asm(const char *s);
extern long __strnlen_user_asm(const char *s);

/*
 * String functions
 */
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);

EXPORT_SYMBOL(kernel_thread);

/*
 * Userspace access stuff.
 */
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(__bzero);
EXPORT_SYMBOL(__strncpy_from_user_nocheck_asm);
EXPORT_SYMBOL(__strncpy_from_user_asm);
EXPORT_SYMBOL(__strlen_user_nocheck_asm);
EXPORT_SYMBOL(__strlen_user_asm);
EXPORT_SYMBOL(__strnlen_user_nocheck_asm);
EXPORT_SYMBOL(__strnlen_user_asm);

EXPORT_SYMBOL(csum_partial);

EXPORT_SYMBOL(invalid_pte_table);
