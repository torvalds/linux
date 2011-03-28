/* Kernel debugger for MN10300
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_KGDB_H
#define _ASM_KGDB_H

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound
 * buffers at least NUMREGBYTES*2 are needed for register packets
 * Longer buffer is needed to list all threads
 */
#define BUFMAX			1024

/*
 * Note that this register image is in a different order than the register
 * image that Linux produces at interrupt time.
 */
enum regnames {
	GDB_FR_D0		= 0,
	GDB_FR_D1		= 1,
	GDB_FR_D2		= 2,
	GDB_FR_D3		= 3,
	GDB_FR_A0		= 4,
	GDB_FR_A1		= 5,
	GDB_FR_A2		= 6,
	GDB_FR_A3		= 7,

	GDB_FR_SP		= 8,
	GDB_FR_PC		= 9,
	GDB_FR_MDR		= 10,
	GDB_FR_EPSW		= 11,
	GDB_FR_LIR		= 12,
	GDB_FR_LAR		= 13,
	GDB_FR_MDRQ		= 14,

	GDB_FR_E0		= 15,
	GDB_FR_E1		= 16,
	GDB_FR_E2		= 17,
	GDB_FR_E3		= 18,
	GDB_FR_E4		= 19,
	GDB_FR_E5		= 20,
	GDB_FR_E6		= 21,
	GDB_FR_E7		= 22,

	GDB_FR_SSP		= 23,
	GDB_FR_MSP		= 24,
	GDB_FR_USP		= 25,
	GDB_FR_MCRH		= 26,
	GDB_FR_MCRL		= 27,
	GDB_FR_MCVF		= 28,

	GDB_FR_FPCR		= 29,
	GDB_FR_DUMMY0		= 30,
	GDB_FR_DUMMY1		= 31,

	GDB_FR_FS0		= 32,

	GDB_FR_SIZE		= 64,
};

#define GDB_ORIG_D0		41
#define NUMREGBYTES		(GDB_FR_SIZE*4)

static inline void arch_kgdb_breakpoint(void)
{
	asm(".globl __arch_kgdb_breakpoint; __arch_kgdb_breakpoint: break");
}
extern u8 __arch_kgdb_breakpoint;

#define BREAK_INSTR_SIZE	1
#define CACHE_FLUSH_IS_SAFE	1

#endif /* _ASM_KGDB_H */
