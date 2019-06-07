/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_SECTIONS_H
#define _PARISC_SECTIONS_H

/* nothing to see, move along */
#include <asm-generic/sections.h>

extern char __alt_instructions[], __alt_instructions_end[];

#ifdef CONFIG_64BIT

#define HAVE_DEREFERENCE_FUNCTION_DESCRIPTOR 1

#undef dereference_function_descriptor
void *dereference_function_descriptor(void *);

#undef dereference_kernel_function_descriptor
void *dereference_kernel_function_descriptor(void *);
#endif

#endif
