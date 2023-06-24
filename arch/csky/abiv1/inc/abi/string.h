/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ABI_CSKY_STRING_H
#define __ABI_CSKY_STRING_H

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMSET
extern void *memset(void *, int,  __kernel_size_t);

#endif /* __ABI_CSKY_STRING_H */
