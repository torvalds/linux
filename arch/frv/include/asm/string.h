/* string.h: FRV string handling
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_STRING_H_
#define _ASM_STRING_H_

#ifdef __KERNEL__ /* only set these up for kernel code */

#define __HAVE_ARCH_MEMSET 1
#define __HAVE_ARCH_MEMCPY 1

extern void *memset(void *, int, __kernel_size_t);
extern void *memcpy(void *, const void *, __kernel_size_t);

#else /* KERNEL */

/*
 *	let user libraries deal with these,
 *	IMHO the kernel has no place defining these functions for user apps
 */

#define __HAVE_ARCH_STRCPY 1
#define __HAVE_ARCH_STRNCPY 1
#define __HAVE_ARCH_STRCAT 1
#define __HAVE_ARCH_STRNCAT 1
#define __HAVE_ARCH_STRCMP 1
#define __HAVE_ARCH_STRNCMP 1
#define __HAVE_ARCH_STRCHR 1
#define __HAVE_ARCH_STRRCHR 1
#define __HAVE_ARCH_STRSTR 1
#define __HAVE_ARCH_STRLEN 1
#define __HAVE_ARCH_STRNLEN 1
#define __HAVE_ARCH_MEMSET 1
#define __HAVE_ARCH_MEMCPY 1
#define __HAVE_ARCH_MEMMOVE 1
#define __HAVE_ARCH_MEMSCAN 1
#define __HAVE_ARCH_MEMCMP 1
#define __HAVE_ARCH_MEMCHR 1
#define __HAVE_ARCH_STRTOK 1

#endif /* KERNEL */
#endif /* _ASM_STRING_H_ */
