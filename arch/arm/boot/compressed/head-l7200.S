/* 
 * linux/arch/arm/boot/compressed/head-l7200.S
 * 
 * Copyright (C) 2000 Steve Hill <sjhill@cotw.com>
 * 
 * Some code borrowed from Nicolas Pitre's 'head-sa1100.S' file. This
 * is merged with head.S by the linker.
 */

#include <asm/mach-types.h>

#ifndef CONFIG_ARCH_L7200
#error What am I doing here...
#endif

		.section        ".start", "ax"

__L7200_start:
		mov	r0, #0x00100000		@ FLASH address of initrd
		mov	r2, #0xf1000000		@ RAM address of initrd
		add	r3, r2, #0x00700000	@ Size of initrd 
1:
		ldmia   r0!, {r4, r5, r6, r7}
		stmia   r2!, {r4, r5, r6, r7}
		cmp	r2, r3
		ble	1b

		mov	r8, #0			@ Zero it out
		mov	r7, #MACH_TYPE_L7200	@ Set architecture ID
