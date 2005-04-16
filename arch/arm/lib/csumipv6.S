/*
 *  linux/arch/arm/lib/csumipv6.S
 *
 *  Copyright (C) 1995-1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text

ENTRY(__csum_ipv6_magic)
		str	lr, [sp, #-4]!
		adds	ip, r2, r3
		ldmia	r1, {r1 - r3, lr}
		adcs	ip, ip, r1
		adcs	ip, ip, r2
		adcs	ip, ip, r3
		adcs	ip, ip, lr
		ldmia	r0, {r0 - r3}
		adcs	r0, ip, r0
		adcs	r0, r0, r1
		adcs	r0, r0, r2
		ldr	r2, [sp, #4]
		adcs	r0, r0, r3
		adcs	r0, r0, r2
		adcs	r0, r0, #0
		LOADREGS(fd, sp!, {pc})

