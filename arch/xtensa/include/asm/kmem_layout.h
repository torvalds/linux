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

#include <asm/core.h>
#include <asm/types.h>

#ifdef CONFIG_MMU

/*
 * Fixed TLB translations in the processor.
 */

#define XCHAL_PAGE_TABLE_VADDR	__XTENSA_UL_CONST(0x80000000)
#define XCHAL_PAGE_TABLE_SIZE	__XTENSA_UL_CONST(0x00400000)

#if defined(CONFIG_XTENSA_KSEG_MMU_V2)

#define XCHAL_KSEG_CACHED_VADDR	__XTENSA_UL_CONST(0xd0000000)
#define XCHAL_KSEG_BYPASS_VADDR	__XTENSA_UL_CONST(0xd8000000)
#define XCHAL_KSEG_SIZE		__XTENSA_UL_CONST(0x08000000)
#define XCHAL_KSEG_ALIGNMENT	__XTENSA_UL_CONST(0x08000000)
#define XCHAL_KSEG_TLB_WAY	5
#define XCHAL_KIO_TLB_WAY	6

#elif defined(CONFIG_XTENSA_KSEG_256M)

#define XCHAL_KSEG_CACHED_VADDR	__XTENSA_UL_CONST(0xb0000000)
#define XCHAL_KSEG_BYPASS_VADDR	__XTENSA_UL_CONST(0xc0000000)
#define XCHAL_KSEG_SIZE		__XTENSA_UL_CONST(0x10000000)
#define XCHAL_KSEG_ALIGNMENT	__XTENSA_UL_CONST(0x10000000)
#define XCHAL_KSEG_TLB_WAY	6
#define XCHAL_KIO_TLB_WAY	6

#elif defined(CONFIG_XTENSA_KSEG_512M)

#define XCHAL_KSEG_CACHED_VADDR	__XTENSA_UL_CONST(0xa0000000)
#define XCHAL_KSEG_BYPASS_VADDR	__XTENSA_UL_CONST(0xc0000000)
#define XCHAL_KSEG_SIZE		__XTENSA_UL_CONST(0x20000000)
#define XCHAL_KSEG_ALIGNMENT	__XTENSA_UL_CONST(0x10000000)
#define XCHAL_KSEG_TLB_WAY	6
#define XCHAL_KIO_TLB_WAY	6

#else
#error Unsupported KSEG configuration
#endif

#ifdef CONFIG_KSEG_PADDR
#define XCHAL_KSEG_PADDR        __XTENSA_UL_CONST(CONFIG_KSEG_PADDR)
#else
#define XCHAL_KSEG_PADDR	__XTENSA_UL_CONST(0x00000000)
#endif

#if XCHAL_KSEG_PADDR & (XCHAL_KSEG_ALIGNMENT - 1)
#error XCHAL_KSEG_PADDR is not properly aligned to XCHAL_KSEG_ALIGNMENT
#endif

#endif

/* KIO definition */

#if XCHAL_HAVE_PTP_MMU
#define XCHAL_KIO_CACHED_VADDR		0xe0000000
#define XCHAL_KIO_BYPASS_VADDR		0xf0000000
#define XCHAL_KIO_DEFAULT_PADDR		0xf0000000
#else
#define XCHAL_KIO_BYPASS_VADDR		XCHAL_KIO_PADDR
#define XCHAL_KIO_DEFAULT_PADDR		0x90000000
#endif
#define XCHAL_KIO_SIZE			0x10000000

#if (!XCHAL_HAVE_PTP_MMU || XCHAL_HAVE_SPANNING_WAY) && defined(CONFIG_OF)
#define XCHAL_KIO_PADDR			xtensa_get_kio_paddr()
#ifndef __ASSEMBLY__
extern unsigned long xtensa_kio_paddr;

static inline unsigned long xtensa_get_kio_paddr(void)
{
	return xtensa_kio_paddr;
}
#endif
#else
#define XCHAL_KIO_PADDR			XCHAL_KIO_DEFAULT_PADDR
#endif

/* KERNEL_STACK definition */

#ifndef CONFIG_KASAN
#define KERNEL_STACK_SHIFT	13
#else
#define KERNEL_STACK_SHIFT	15
#endif
#define KERNEL_STACK_SIZE	(1 << KERNEL_STACK_SHIFT)

#endif
