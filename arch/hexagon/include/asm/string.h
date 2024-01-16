/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_STRING_H_
#define _ASM_STRING_H_

#ifdef __KERNEL__
#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *__to, __const__ void *__from, size_t __n);

/*  ToDo:  use dczeroa, accelerate the compiler-constant zero case  */
#define __HAVE_ARCH_MEMSET
extern void *memset(void *__to, int c, size_t __n);
#endif


#endif /* _ASM_STRING_H_ */
