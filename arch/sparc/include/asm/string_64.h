/*
 * string.h: External definitions for optimized assembly string
 *           routines for the Linux Kernel.
 *
 * Copyright (C) 1995,1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997,1999 Jakub Jelinek (jakub@redhat.com)
 */

#ifndef __SPARC64_STRING_H__
#define __SPARC64_STRING_H__

#include <asm/asi.h>

/* Now the str*() stuff... */
#define __HAVE_ARCH_STRLEN
__kernel_size_t strlen(const char *);

#endif /* !(__SPARC64_STRING_H__) */
