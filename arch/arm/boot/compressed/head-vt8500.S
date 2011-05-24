/*
 * linux/arch/arm/boot/compressed/head-vt8500.S
 *
 * Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * VIA VT8500 specific tweaks. This is merged into head.S by the linker.
 *
 */

#include <linux/linkage.h>
#include <asm/mach-types.h>

		.section        ".start", "ax"

__VT8500_start:
	@ Compare the SCC ID register against a list of known values
	ldr	r1, .SCCID
	ldr	r3, [r1]

	@ VT8500 override
	ldr	r4, .VT8500SCC
	cmp	r3, r4
	ldreq	r7, .ID_BV07
	beq	.Lendvt8500

	@ WM8505 override
	ldr	r4, .WM8505SCC
	cmp	r3, r4
	ldreq	r7, .ID_8505
	beq	.Lendvt8500

	@ Otherwise, leave the bootloader's machine id untouched

.SCCID:
	.word	0xd8120000
.VT8500SCC:
	.word	0x34000102
.WM8505SCC:
	.word	0x34260103

.ID_BV07:
	.word	MACH_TYPE_BV07
.ID_8505:
	.word	MACH_TYPE_WM8505_7IN_NETBOOK

.Lendvt8500:
