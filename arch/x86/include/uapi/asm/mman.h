/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_MMAN_H
#define _ASM_X86_MMAN_H

#define MAP_32BIT	0x40		/* only give out 32bit addresses */
#define MAP_ABOVE4G	0x80		/* only map above 4GB */

/* Flags for map_shadow_stack(2) */
#define SHADOW_STACK_SET_TOKEN	(1ULL << 0)	/* Set up a restore token in the shadow stack */

#include <asm-generic/mman.h>

#endif /* _ASM_X86_MMAN_H */
