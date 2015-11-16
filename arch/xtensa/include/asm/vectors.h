/*
 * arch/xtensa/include/asm/xchal_vaddr_remap.h
 *
 * Xtensa macros for MMU V3 Support. Deals with re-mapping the Virtual
 * Memory Addresses from "Virtual == Physical" to their prevvious V2 MMU
 * mappings (KSEG at 0xD0000000 and KIO at 0XF0000000).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 - 2012 Tensilica Inc.
 *
 * Pete Delaney <piet@tensilica.com>
 * Marc Gauthier <marc@tensilica.com
 */

#ifndef _XTENSA_VECTORS_H
#define _XTENSA_VECTORS_H

#include <variant/core.h>
#include <platform/hardware.h>

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

#if defined(CONFIG_MMU)

/* Will Become VECBASE */
#define VIRTUAL_MEMORY_ADDRESS		0xD0000000

/* Image Virtual Start Address */
#define KERNELOFFSET			0xD0003000

#if defined(XCHAL_HAVE_PTP_MMU) && XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY
  /* MMU v3  - XCHAL_HAVE_PTP_MMU  == 1 */
  #define LOAD_MEMORY_ADDRESS		0x00003000
#else
  /* MMU V2 -  XCHAL_HAVE_PTP_MMU  == 0 */
  #define LOAD_MEMORY_ADDRESS		0xD0003000
#endif

#define RESET_VECTOR1_VADDR		(VIRTUAL_MEMORY_ADDRESS + \
					 XCHAL_RESET_VECTOR1_PADDR)

#else /* !defined(CONFIG_MMU) */
  /* MMU Not being used - Virtual == Physical */

  /* VECBASE */
  #define VIRTUAL_MEMORY_ADDRESS	(PLATFORM_DEFAULT_MEM_START + 0x2000)

  /* Location of the start of the kernel text, _start */
  #define KERNELOFFSET			(PLATFORM_DEFAULT_MEM_START + 0x3000)

  /* Loaded just above possibly live vectors */
  #define LOAD_MEMORY_ADDRESS		(PLATFORM_DEFAULT_MEM_START + 0x3000)

#define RESET_VECTOR1_VADDR		(XCHAL_RESET_VECTOR1_VADDR)

#endif /* CONFIG_MMU */

#define XC_VADDR(offset)		(VIRTUAL_MEMORY_ADDRESS  + offset)

/* Used to set VECBASE register */
#define VECBASE_RESET_VADDR		VIRTUAL_MEMORY_ADDRESS

#if defined(XCHAL_HAVE_VECBASE) && XCHAL_HAVE_VECBASE

#define USER_VECTOR_VADDR		XC_VADDR(XCHAL_USER_VECOFS)
#define KERNEL_VECTOR_VADDR		XC_VADDR(XCHAL_KERNEL_VECOFS)
#define DOUBLEEXC_VECTOR_VADDR		XC_VADDR(XCHAL_DOUBLEEXC_VECOFS)
#define WINDOW_VECTORS_VADDR		XC_VADDR(XCHAL_WINDOW_OF4_VECOFS)
#define INTLEVEL2_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL2_VECOFS)
#define INTLEVEL3_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL3_VECOFS)
#define INTLEVEL4_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL4_VECOFS)
#define INTLEVEL5_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL5_VECOFS)
#define INTLEVEL6_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL6_VECOFS)

#define DEBUG_VECTOR_VADDR		XC_VADDR(XCHAL_DEBUG_VECOFS)

#define NMI_VECTOR_VADDR		XC_VADDR(XCHAL_NMI_VECOFS)

#define INTLEVEL7_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL7_VECOFS)

/*
 * These XCHAL_* #defines from varian/core.h
 * are not valid to use with V3 MMU. Non-XCHAL
 * constants are defined above and should be used.
 */
#undef  XCHAL_VECBASE_RESET_VADDR
#undef  XCHAL_RESET_VECTOR0_VADDR
#undef  XCHAL_USER_VECTOR_VADDR
#undef  XCHAL_KERNEL_VECTOR_VADDR
#undef  XCHAL_DOUBLEEXC_VECTOR_VADDR
#undef  XCHAL_WINDOW_VECTORS_VADDR
#undef  XCHAL_INTLEVEL2_VECTOR_VADDR
#undef  XCHAL_INTLEVEL3_VECTOR_VADDR
#undef  XCHAL_INTLEVEL4_VECTOR_VADDR
#undef  XCHAL_INTLEVEL5_VECTOR_VADDR
#undef  XCHAL_INTLEVEL6_VECTOR_VADDR
#undef  XCHAL_DEBUG_VECTOR_VADDR
#undef  XCHAL_NMI_VECTOR_VADDR
#undef  XCHAL_INTLEVEL7_VECTOR_VADDR

#else

#define USER_VECTOR_VADDR		XCHAL_USER_VECTOR_VADDR
#define KERNEL_VECTOR_VADDR		XCHAL_KERNEL_VECTOR_VADDR
#define DOUBLEEXC_VECTOR_VADDR		XCHAL_DOUBLEEXC_VECTOR_VADDR
#define WINDOW_VECTORS_VADDR		XCHAL_WINDOW_VECTORS_VADDR
#define INTLEVEL2_VECTOR_VADDR		XCHAL_INTLEVEL2_VECTOR_VADDR
#define INTLEVEL3_VECTOR_VADDR		XCHAL_INTLEVEL3_VECTOR_VADDR
#define INTLEVEL4_VECTOR_VADDR		XCHAL_INTLEVEL4_VECTOR_VADDR
#define INTLEVEL5_VECTOR_VADDR		XCHAL_INTLEVEL5_VECTOR_VADDR
#define INTLEVEL6_VECTOR_VADDR		XCHAL_INTLEVEL6_VECTOR_VADDR
#define DEBUG_VECTOR_VADDR		XCHAL_DEBUG_VECTOR_VADDR

#endif

#endif /* _XTENSA_VECTORS_H */
