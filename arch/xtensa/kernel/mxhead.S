/*
 * Xtensa Secondary Processors startup code.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2013 Tensilica Inc.
 *
 * Joe Taylor <joe@tensilica.com>
 * Chris Zankel <chris@zankel.net>
 * Marc Gauthier <marc@tensilica.com, marc@alumni.uwaterloo.ca>
 * Pete Delaney <piet@tensilica.com>
 */

#include <linux/linkage.h>

#include <asm/cacheasm.h>
#include <asm/initialize_mmu.h>
#include <asm/mxregs.h>
#include <asm/regs.h>


	.section .SecondaryResetVector.text, "ax"


ENTRY(_SecondaryResetVector)
	_j _SetupOCD

	.begin  no-absolute-literals
	.literal_position

_SetupOCD:
	/*
	 * Initialize WB, WS, and clear PS.EXCM (to allow loop instructions).
	 * Set Interrupt Level just below XCHAL_DEBUGLEVEL to allow
	 * xt-gdb to single step via DEBUG exceptions received directly
	 * by ocd.
	 */
	movi	a1, 1
	movi	a0, 0
	wsr	a1, windowstart
	wsr	a0, windowbase
	rsync

	movi	a1, LOCKLEVEL
	wsr	a1, ps
	rsync

_SetupMMU:
	Offset = _SetupMMU - _SecondaryResetVector

#ifdef CONFIG_INITIALIZE_XTENSA_MMU_INSIDE_VMLINUX
	initialize_mmu
#endif

	/*
	 * Start Secondary Processors with NULL pointer to boot params.
	 */
	movi	a2, 0				#  a2 == NULL
	movi	a3, _startup
	jx	a3

	.end    no-absolute-literals


	.section 	.SecondaryResetVector.remapped_text, "ax"
	.global         _RemappedSecondaryResetVector

	.org 0                                  # Need to do org before literals

_RemappedSecondaryResetVector:
	.begin  no-absolute-literals
	.literal_position

	_j      _RemappedSetupMMU
	. = _RemappedSecondaryResetVector + Offset

_RemappedSetupMMU:

#ifdef CONFIG_INITIALIZE_XTENSA_MMU_INSIDE_VMLINUX
	initialize_mmu
#endif

	.end    no-absolute-literals
