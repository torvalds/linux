// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASM_NDS32_STRING_H
#define __ASM_NDS32_STRING_H

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMSET
extern void *memset(void *, int, __kernel_size_t);

extern void *memzero(void *ptr, __kernel_size_t n);
#endif
