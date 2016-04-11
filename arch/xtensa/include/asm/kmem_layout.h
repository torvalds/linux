/*
 * Kernel virtual memory layout definitions.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2016 Cadence Design Systems Inc.
 */

#ifndef _XTENSA_KMEM_LAYOUT_H
#define _XTENSA_KMEM_LAYOUT_H

#include <asm/types.h>

/*
 * Fixed TLB translations in the processor.
 */

#define XCHAL_KSEG_CACHED_VADDR	__XTENSA_UL_CONST(0xd0000000)
#define XCHAL_KSEG_BYPASS_VADDR	__XTENSA_UL_CONST(0xd8000000)
#define XCHAL_KSEG_SIZE		__XTENSA_UL_CONST(0x08000000)
#define XCHAL_KSEG_PADDR	__XTENSA_UL_CONST(0x00000000)

#endif
