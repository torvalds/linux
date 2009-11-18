/* 
 * linux/arch/arm/boot/compressed/head-sa1100.S
 * 
 * Copyright (C) 1999 Nicolas Pitre <nico@fluxnic.net>
 * 
 * SA1100 specific tweaks.  This is merged into head.S by the linker.
 *
 */

#include <linux/linkage.h>
#include <asm/mach-types.h>

		.section        ".start", "ax"

__SA1100_start:

		@ Preserve r8/r7 i.e. kernel entry values
#ifdef CONFIG_SA1100_COLLIE
		mov	r7, #MACH_TYPE_COLLIE
#endif
#ifdef CONFIG_SA1100_SIMPAD
		@ UNTIL we've something like an open bootldr
		mov	r7, #MACH_TYPE_SIMPAD	@should be 87
#endif
		mrc	p15, 0, r0, c1, c0, 0	@ read control reg
		ands	r0, r0, #0x0d
		beq	99f

		@ Data cache might be active.
		@ Be sure to flush kernel binary out of the cache,
		@ whatever state it is, before it is turned off.
		@ This is done by fetching through currently executed
		@ memory to be sure we hit the same cache.
		bic	r2, pc, #0x1f
		add	r3, r2, #0x4000		@ 16 kb is quite enough...
1:		ldr	r0, [r2], #32
		teq	r2, r3
		bne	1b
		mcr	p15, 0, r0, c7, c10, 4	@ drain WB
		mcr	p15, 0, r0, c7, c7, 0	@ flush I & D caches

		@ disabling MMU and caches
		mrc	p15, 0, r0, c1, c0, 0	@ read control reg
		bic	r0, r0, #0x0d		@ clear WB, DC, MMU
		bic	r0, r0, #0x1000		@ clear Icache
		mcr	p15, 0, r0, c1, c0, 0
99:
