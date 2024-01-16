/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_STRING_H
#define _ASM_MICROBLAZE_STRING_H

#ifdef __KERNEL__

#ifdef CONFIG_OPT_LIB_FUNCTION
#define __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMMOVE

extern void *memset(void *, int, __kernel_size_t);
extern void *memcpy(void *, const void *, __kernel_size_t);
extern void *memmove(void *, const void *, __kernel_size_t);
#endif

#endif /* __KERNEL__ */

#endif /* _ASM_MICROBLAZE_STRING_H */
