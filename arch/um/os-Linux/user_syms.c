// SPDX-License-Identifier: GPL-2.0
#define __NO_FORTIFY
#include <linux/types.h>
#include <linux/module.h>

/* Some of this are builtin function (some are not but could in the future),
 * so I *must* declare good prototypes for them and then EXPORT them.
 * The kernel code uses the macro defined by include/linux/string.h,
 * so I undef macros; the userspace code does not include that and I
 * add an EXPORT for the glibc one.
 */

#undef strlen
#undef strstr
#undef memcpy
#undef memset

extern size_t strlen(const char *);
extern void *memmove(void *, const void *, size_t);
extern void *memset(void *, int, size_t);

/* If it's not defined, the export is included in lib/string.c.*/
#ifdef __HAVE_ARCH_STRSTR
EXPORT_SYMBOL(strstr);
#endif

#ifndef __x86_64__
extern void *memcpy(void *, const void *, size_t);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memset);
#endif

#ifdef CONFIG_ARCH_REUSE_HOST_VSYSCALL_AREA
EXPORT_SYMBOL(vsyscall_ehdr);
EXPORT_SYMBOL(vsyscall_end);
#endif

/* Export symbols used by GCC for the stack protector. */
extern void __stack_smash_handler(void *) __attribute__((weak));
EXPORT_SYMBOL(__stack_smash_handler);

extern long __guard __attribute__((weak));
EXPORT_SYMBOL(__guard);

#ifdef _FORTIFY_SOURCE
extern int __sprintf_chk(char *str, int flag, size_t strlen, const char *format);
EXPORT_SYMBOL(__sprintf_chk);
#endif
