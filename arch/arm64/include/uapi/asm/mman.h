/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__ASM_MMAN_H
#define _UAPI__ASM_MMAN_H

#include <asm-generic/mman.h>

#define PROT_BTI	0x10		/* BTI guarded page */
#define PROT_MTE	0x20		/* Normal Tagged mapping */

/* Override any generic PKEY permission defines */
#define PKEY_DISABLE_EXECUTE	0x4
#define PKEY_DISABLE_READ	0x8
#undef PKEY_ACCESS_MASK
#define PKEY_ACCESS_MASK       (PKEY_DISABLE_ACCESS |\
				PKEY_DISABLE_WRITE  |\
				PKEY_DISABLE_READ   |\
				PKEY_DISABLE_EXECUTE)

#endif /* ! _UAPI__ASM_MMAN_H */
