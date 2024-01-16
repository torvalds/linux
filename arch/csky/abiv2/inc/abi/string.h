/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ABI_CSKY_STRING_H
#define __ABI_CSKY_STRING_H

#define __HAVE_ARCH_MEMCMP
extern int memcmp(const void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMSET
extern void *memset(void *, int,  __kernel_size_t);

#define __HAVE_ARCH_STRCMP
extern int strcmp(const char *, const char *);

#define __HAVE_ARCH_STRCPY
extern char *strcpy(char *, const char *);

#define __HAVE_ARCH_STRLEN
extern __kernel_size_t strlen(const char *);

#endif /* __ABI_CSKY_STRING_H */
