/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_OPENRISC_STRING_H
#define __ASM_OPENRISC_STRING_H

#define __HAVE_ARCH_MEMSET
extern void *memset(void *s, int c, __kernel_size_t n);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *dest, __const void *src, __kernel_size_t n);

#endif /* __ASM_OPENRISC_STRING_H */
