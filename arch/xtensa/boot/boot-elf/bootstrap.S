/*
 * arch/xtensa/boot/boot-elf/bootstrap.S
 *
 * Low-level exception handling
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 - 2013 by Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 * Marc Gauthier <marc@tensilica.com>
 * Piet Delaney <piet@tensilica.com>
 */

#include <asm/bootparam.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/cacheasm.h>
#include <asm/initialize_mmu.h>
#include <asm/vectors.h>
#include <linux/linkage.h>

	.section	.ResetVector.text, "ax"
	.global         _ResetVector
	.global         reset

_ResetVector:
	_j _SetupMMU

	.begin  no-absolute-literals
	.literal_position

	.align 4
RomInitAddr:
#if defined(CONFIG_INITIALIZE_XTENSA_MMU_INSIDE_VMLINUX) && \
	XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY
	.word	CONFIG_KERNEL_LOAD_ADDRESS
#else
	.word	KERNELOFFSET
#endif
RomBootParam:
	.word _bootparam
_bootparam:
	.short	BP_TAG_FIRST
	.short	4
	.long	BP_VERSION
	.short	BP_TAG_LAST
	.short	0
	.long	0

	.align  4
_SetupMMU:
	movi	a0, 0
	wsr	a0, windowbase
	rsync
	movi	a0, 1
	wsr	a0, windowstart
	rsync
	movi	a0, 0x1F
	wsr	a0, ps
	rsync

#ifndef CONFIG_INITIALIZE_XTENSA_MMU_INSIDE_VMLINUX
	initialize_mmu
#endif

	.end    no-absolute-literals

	rsil    a0, XCHAL_DEBUGLEVEL-1
	rsync
reset:
	l32r    a0, RomInitAddr
	l32r	a2, RomBootParam
	movi	a3, 0
	movi	a4, 0
	jx      a0
