/*
 *  linux/arch/arm/lib/io-acorn.S
 *
 *  Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 27/03/03 Ian Molton Clean up CONFIG_CPU
 *
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>

		.text
		.align

.Liosl_warning:
		.ascii	"<4>insl/outsl not implemented, called from %08lX\0"
		.align

/*
 * These make no sense on Acorn machines.
 * Print a warning message.
 */
ENTRY(insl)
ENTRY(outsl)
		adr	r0, .Liosl_warning
		mov	r1, lr
		b	printk
