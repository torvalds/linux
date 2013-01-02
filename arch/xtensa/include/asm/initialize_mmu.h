/*
 * arch/xtensa/include/asm/initialize_mmu.h
 *
 * Initializes MMU:
 *
 *      For the new V3 MMU we remap the TLB from virtual == physical
 *      to the standard Linux mapping used in earlier MMU's.
 *
 *      The the MMU we also support a new configuration register that
 *      specifies how the S32C1I instruction operates with the cache
 *      controller.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2008 - 2012 Tensilica, Inc.
 *
 *   Marc Gauthier <marc@tensilica.com>
 *   Pete Delaney <piet@tensilica.com>
 */

#ifndef _XTENSA_INITIALIZE_MMU_H
#define _XTENSA_INITIALIZE_MMU_H

#ifdef __ASSEMBLY__

#define XTENSA_HWVERSION_RC_2009_0 230000

	.macro	initialize_mmu

#if XCHAL_HAVE_S32C1I && (XCHAL_HW_MIN_VERSION >= XTENSA_HWVERSION_RC_2009_0)
/*
 * We Have Atomic Operation Control (ATOMCTL) Register; Initialize it.
 * For details see Documentation/xtensa/atomctl.txt
 */
#if XCHAL_DCACHE_IS_COHERENT
	movi	a3, 0x25	/* For SMP/MX -- internal for writeback,
				 * RCW otherwise
				 */
#else
	movi	a3, 0x29	/* non-MX -- Most cores use Std Memory
				 * Controlers which usually can't use RCW
				 */
#endif
	wsr	a3, atomctl
#endif  /* XCHAL_HAVE_S32C1I &&
	 * (XCHAL_HW_MIN_VERSION >= XTENSA_HWVERSION_RC_2009_0)
	 */

	.endm

#endif /*__ASSEMBLY__*/

#endif /* _XTENSA_INITIALIZE_MMU_H */
