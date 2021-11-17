/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _H8300_STRING_H_
#define _H8300_STRING_H_

#ifdef __KERNEL__ /* only set these up for kernel code */

#include <asm/setup.h>
#include <asm/page.h>

#define __HAVE_ARCH_MEMSET
extern void *memset(void *s, int c, size_t count);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *d, const void *s, size_t count);

#endif /* KERNEL */

#endif
