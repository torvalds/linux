/*
 *  linux/arch/arm/lib/ecard.S
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

#define CPSR2SPSR(rt) \
		mrs	rt, cpsr; \
		msr	spsr_cxsf, rt

@ Purpose: call an expansion card loader to read bytes.
@ Proto  : char read_loader(int offset, char *card_base, char *loader);
@ Returns: byte read

ENTRY(ecard_loader_read)
		stmfd	sp!, {r4 - r12, lr}
		mov	r11, r1
		mov	r1, r0
		CPSR2SPSR(r0)
		mov	lr, pc
		mov	pc, r2
		LOADREGS(fd, sp!, {r4 - r12, pc})

@ Purpose: call an expansion card loader to reset the card
@ Proto  : void read_loader(int card_base, char *loader);
@ Returns: byte read

ENTRY(ecard_loader_reset)
		stmfd	sp!, {r4 - r12, lr}
		mov	r11, r0
		CPSR2SPSR(r0)
		mov	lr, pc
		add	pc, r1, #8
		LOADREGS(fd, sp!, {r4 - r12, pc})

