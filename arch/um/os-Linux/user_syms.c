// SPDX-License-Identifier: GPL-2.0
#define __NO_FORTIFY
#include <linux/types.h>
#include <linux/module.h>

/*
 * This file exports some critical string functions and compiler
 * built-in functions (where calls are emitted by the compiler
 * itself that we cannot avoid even in kernel code) to modules.
 *
 * "_user.c" code that previously used exports here such as hostfs
 * really should be considered part of the 'hypervisor' and define
 * its own API boundary like hostfs does now; don't add exports to
 * this file for such cases.
 */

/* If it's not defined, the export is included in lib/string.c.*/
#ifdef __HAVE_ARCH_STRSTR
#undef strstr
EXPORT_SYMBOL(strstr);
#endif

#ifndef __x86_64__
#undef memcpy
extern void *memcpy(void *, const void *, size_t);
EXPORT_SYMBOL(memcpy);
extern void *memmove(void *, const void *, size_t);
EXPORT_SYMBOL(memmove);
#undef memset
extern void *memset(void *, int, size_t);
EXPORT_SYMBOL(memset);
#endif

#ifdef CONFIG_ARCH_REUSE_HOST_VSYSCALL_AREA
/* needed for __access_ok() */
EXPORT_SYMBOL(vsyscall_ehdr);
EXPORT_SYMBOL(vsyscall_end);
#endif

/* Export symbols used by GCC for the stack protector. */
extern void __stack_smash_handler(void *) __attribute__((weak));
EXPORT_SYMBOL(__stack_smash_handler);

extern long __guard __attribute__((weak));
EXPORT_SYMBOL(__guard);

#ifdef _FORTIFY_SOURCE
extern int __sprintf_chk(char *str, int flag, size_t len, const char *format);
EXPORT_SYMBOL(__sprintf_chk);
#endif
