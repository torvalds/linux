/*
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_STRING_H
#define _ASM_NIOS2_STRING_H

#ifdef __KERNEL__

#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMMOVE

extern void *memset(void *s, int c, size_t count);
extern void *memcpy(void *d, const void *s, size_t count);
extern void *memmove(void *d, const void *s, size_t count);

#endif /* __KERNEL__ */

#endif /* _ASM_NIOS2_STRING_H */
