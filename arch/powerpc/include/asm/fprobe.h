/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PPC_FPROBE_H
#define _ASM_PPC_FPROBE_H

#include <asm-generic/fprobe.h>

#ifdef CONFIG_64BIT
#undef FPROBE_HEADER_MSB_PATTERN
#define FPROBE_HEADER_MSB_PATTERN	(PAGE_OFFSET & ~FPROBE_HEADER_MSB_MASK)
#endif

#endif /* _ASM_PPC_FPROBE_H */
