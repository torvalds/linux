/*
 * arch/arm/mach-at91/include/mach/at91cap9_matrix.h
 *
 *  Copyright (C) 2007 Stelian Pop <stelian.pop@leadtechdesign.com>
 *  Copyright (C) 2007 Lead Tech Design <www.leadtechdesign.com>
 *  Copyright (C) 2006 Atmel Corporation.
 *
 * Memory Controllers (MATRIX, EBI) - System peripherals registers.
 * Based on AT91CAP9 datasheet revision B (Preliminary).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91CAP9_MATRIX_H
#define AT91CAP9_MATRIX_H

#define AT91_MATRIX_MCFG0	(AT91_MATRIX + 0x00)	/* Master Configuration Register 0 */
#define AT91_MATRIX_MCFG1	(AT91_MATRIX + 0x04)	/* Master Configuration Register 1 */
#define AT91_MATRIX_MCFG2	(AT91_MATRIX + 0x08)	/* Master Configuration Register 2 */
#define AT91_MATRIX_MCFG3	(AT91_MATRIX + 0x0C)	/* Master Configuration Register 3 */
#define AT91_MATRIX_MCFG4	(AT91_MATRIX + 0x10)	/* Master Configuration Register 4 */
#define AT91_MATRIX_MCFG5	(AT91_MATRIX + 0x14)	/* Master Configuration Register 5 */
#define AT91_MATRIX_MCFG6	(AT91_MATRIX + 0x18)	/* Master Configuration Register 6 */
#define AT91_MATRIX_MCFG7	(AT91_MATRIX + 0x1C)	/* Master Configuration Register 7 */
#define AT91_MATRIX_MCFG8	(AT91_MATRIX + 0x20)	/* Master Configuration Register 8 */
#define AT91_MATRIX_MCFG9	(AT91_MATRIX + 0x24)	/* Master Configuration Register 9 */
#define AT91_MATRIX_MCFG10	(AT91_MATRIX + 0x28)	/* Master Configuration Register 10 */
#define AT91_MATRIX_MCFG11	(AT91_MATRIX + 0x2C)	/* Master Configuration Register 11 */
#define		AT91_MATRIX_ULBT	(7 << 0)	/* Undefined Length Burst Type */
#define			AT91_MATRIX_ULBT_INFINITE	(0 << 0)
#define			AT91_MATRIX_ULBT_SINGLE		(1 << 0)
#define			AT91_MATRIX_ULBT_FOUR		(2 << 0)
#define			AT91_MATRIX_ULBT_EIGHT		(3 << 0)
#define			AT91_MATRIX_ULBT_SIXTEEN	(4 << 0)

#define AT91_MATRIX_SCFG0	(AT91_MATRIX + 0x40)	/* Slave Configuration Register 0 */
#define AT91_MATRIX_SCFG1	(AT91_MATRIX + 0x44)	/* Slave Configuration Register 1 */
#define AT91_MATRIX_SCFG2	(AT91_MATRIX + 0x48)	/* Slave Configuration Register 2 */
#define AT91_MATRIX_SCFG3	(AT91_MATRIX + 0x4C)	/* Slave Configuration Register 3 */
#define AT91_MATRIX_SCFG4	(AT91_MATRIX + 0x50)	/* Slave Configuration Register 4 */
#define AT91_MATRIX_SCFG5	(AT91_MATRIX + 0x54)	/* Slave Configuration Register 5 */
#define AT91_MATRIX_SCFG6	(AT91_MATRIX + 0x58)	/* Slave Configuration Register 6 */
#define AT91_MATRIX_SCFG7	(AT91_MATRIX + 0x5C)	/* Slave Configuration Register 7 */
#define AT91_MATRIX_SCFG8	(AT91_MATRIX + 0x60)	/* Slave Configuration Register 8 */
#define AT91_MATRIX_SCFG9	(AT91_MATRIX + 0x64)	/* Slave Configuration Register 9 */
#define		AT91_MATRIX_SLOT_CYCLE		(0xff << 0)	/* Maximum Number of Allowed Cycles for a Burst */
#define		AT91_MATRIX_DEFMSTR_TYPE	(3    << 16)	/* Default Master Type */
#define			AT91_MATRIX_DEFMSTR_TYPE_NONE	(0 << 16)
#define			AT91_MATRIX_DEFMSTR_TYPE_LAST	(1 << 16)
#define			AT91_MATRIX_DEFMSTR_TYPE_FIXED	(2 << 16)
#define		AT91_MATRIX_FIXED_DEFMSTR	(0xf  << 18)	/* Fixed Index of Default Master */
#define		AT91_MATRIX_ARBT		(3    << 24)	/* Arbitration Type */
#define			AT91_MATRIX_ARBT_ROUND_ROBIN	(0 << 24)
#define			AT91_MATRIX_ARBT_FIXED_PRIORITY	(1 << 24)

#define AT91_MATRIX_PRAS0	(AT91_MATRIX + 0x80)	/* Priority Register A for Slave 0 */
#define AT91_MATRIX_PRBS0	(AT91_MATRIX + 0x84)	/* Priority Register B for Slave 0 */
#define AT91_MATRIX_PRAS1	(AT91_MATRIX + 0x88)	/* Priority Register A for Slave 1 */
#define AT91_MATRIX_PRBS1	(AT91_MATRIX + 0x8C)	/* Priority Register B for Slave 1 */
#define AT91_MATRIX_PRAS2	(AT91_MATRIX + 0x90)	/* Priority Register A for Slave 2 */
#define AT91_MATRIX_PRBS2	(AT91_MATRIX + 0x94)	/* Priority Register B for Slave 2 */
#define AT91_MATRIX_PRAS3	(AT91_MATRIX + 0x98)	/* Priority Register A for Slave 3 */
#define AT91_MATRIX_PRBS3	(AT91_MATRIX + 0x9C)	/* Priority Register B for Slave 3 */
#define AT91_MATRIX_PRAS4	(AT91_MATRIX + 0xA0)	/* Priority Register A for Slave 4 */
#define AT91_MATRIX_PRBS4	(AT91_MATRIX + 0xA4)	/* Priority Register B for Slave 4 */
#define AT91_MATRIX_PRAS5	(AT91_MATRIX + 0xA8)	/* Priority Register A for Slave 5 */
#define AT91_MATRIX_PRBS5	(AT91_MATRIX + 0xAC)	/* Priority Register B for Slave 5 */
#define AT91_MATRIX_PRAS6	(AT91_MATRIX + 0xB0)	/* Priority Register A for Slave 6 */
#define AT91_MATRIX_PRBS6	(AT91_MATRIX + 0xB4)	/* Priority Register B for Slave 6 */
#define AT91_MATRIX_PRAS7	(AT91_MATRIX + 0xB8)	/* Priority Register A for Slave 7 */
#define AT91_MATRIX_PRBS7	(AT91_MATRIX + 0xBC)	/* Priority Register B for Slave 7 */
#define AT91_MATRIX_PRAS8	(AT91_MATRIX + 0xC0)	/* Priority Register A for Slave 8 */
#define AT91_MATRIX_PRBS8	(AT91_MATRIX + 0xC4)	/* Priority Register B for Slave 8 */
#define AT91_MATRIX_PRAS9	(AT91_MATRIX + 0xC8)	/* Priority Register A for Slave 9 */
#define AT91_MATRIX_PRBS9	(AT91_MATRIX + 0xCC)	/* Priority Register B for Slave 9 */
#define		AT91_MATRIX_M0PR		(3 << 0)	/* Master 0 Priority */
#define		AT91_MATRIX_M1PR		(3 << 4)	/* Master 1 Priority */
#define		AT91_MATRIX_M2PR		(3 << 8)	/* Master 2 Priority */
#define		AT91_MATRIX_M3PR		(3 << 12)	/* Master 3 Priority */
#define		AT91_MATRIX_M4PR		(3 << 16)	/* Master 4 Priority */
#define		AT91_MATRIX_M5PR		(3 << 20)	/* Master 5 Priority */
#define		AT91_MATRIX_M6PR		(3 << 24)	/* Master 6 Priority */
#define		AT91_MATRIX_M7PR		(3 << 28)	/* Master 7 Priority */
#define		AT91_MATRIX_M8PR		(3 << 0)	/* Master 8 Priority (in Register B) */
#define		AT91_MATRIX_M9PR		(3 << 4)	/* Master 9 Priority (in Register B) */
#define		AT91_MATRIX_M10PR		(3 << 8)	/* Master 10 Priority (in Register B) */
#define		AT91_MATRIX_M11PR		(3 << 12)	/* Master 11 Priority (in Register B) */

#define AT91_MATRIX_MRCR	(AT91_MATRIX + 0x100)	/* Master Remap Control Register */
#define		AT91_MATRIX_RCB0		(1 << 0)	/* Remap Command for AHB Master 0 (ARM926EJ-S Instruction Master) */
#define		AT91_MATRIX_RCB1		(1 << 1)	/* Remap Command for AHB Master 1 (ARM926EJ-S Data Master) */
#define		AT91_MATRIX_RCB2		(1 << 2)
#define		AT91_MATRIX_RCB3		(1 << 3)
#define		AT91_MATRIX_RCB4		(1 << 4)
#define		AT91_MATRIX_RCB5		(1 << 5)
#define		AT91_MATRIX_RCB6		(1 << 6)
#define		AT91_MATRIX_RCB7		(1 << 7)
#define		AT91_MATRIX_RCB8		(1 << 8)
#define		AT91_MATRIX_RCB9		(1 << 9)
#define		AT91_MATRIX_RCB10		(1 << 10)
#define		AT91_MATRIX_RCB11		(1 << 11)

#define AT91_MPBS0_SFR		(AT91_MATRIX + 0x114)	/* MPBlock Slave 0 Special Function Register */
#define AT91_MPBS1_SFR		(AT91_MATRIX + 0x11C)	/* MPBlock Slave 1 Special Function Register */

#define AT91_MATRIX_UDPHS	(AT91_MATRIX + 0x118)	/* USBHS Special Function Register [AT91CAP9 only] */
#define		AT91_MATRIX_SELECT_UDPHS	(0 << 31)	/* select High Speed UDP */
#define		AT91_MATRIX_SELECT_UDP		(1 << 31)	/* select standard UDP */
#define		AT91_MATRIX_UDPHS_BYPASS_LOCK	(1 << 30)	/* bypass lock bit */

#define AT91_MATRIX_EBICSA	(AT91_MATRIX + 0x120)	/* EBI Chip Select Assignment Register */
#define		AT91_MATRIX_EBI_CS1A		(1 << 1)	/* Chip Select 1 Assignment */
#define			AT91_MATRIX_EBI_CS1A_SMC		(0 << 1)
#define			AT91_MATRIX_EBI_CS1A_BCRAMC		(1 << 1)
#define		AT91_MATRIX_EBI_CS3A		(1 << 3)	/* Chip Select 3 Assignment */
#define			AT91_MATRIX_EBI_CS3A_SMC		(0 << 3)
#define			AT91_MATRIX_EBI_CS3A_SMC_SMARTMEDIA	(1 << 3)
#define		AT91_MATRIX_EBI_CS4A		(1 << 4)	/* Chip Select 4 Assignment */
#define			AT91_MATRIX_EBI_CS4A_SMC		(0 << 4)
#define			AT91_MATRIX_EBI_CS4A_SMC_CF1		(1 << 4)
#define		AT91_MATRIX_EBI_CS5A		(1 << 5)	/* Chip Select 5 Assignment */
#define			AT91_MATRIX_EBI_CS5A_SMC		(0 << 5)
#define			AT91_MATRIX_EBI_CS5A_SMC_CF2		(1 << 5)
#define		AT91_MATRIX_EBI_DBPUC		(1 << 8)	/* Data Bus Pull-up Configuration */
#define		AT91_MATRIX_EBI_DQSPDC		(1 << 9)	/* Data Qualifier Strobe Pull-Down Configuration */
#define		AT91_MATRIX_EBI_VDDIOMSEL	(1 << 16)	/* Memory voltage selection */
#define			AT91_MATRIX_EBI_VDDIOMSEL_1_8V		(0 << 16)
#define			AT91_MATRIX_EBI_VDDIOMSEL_3_3V		(1 << 16)

#define AT91_MPBS2_SFR		(AT91_MATRIX + 0x12C)	/* MPBlock Slave 2 Special Function Register */
#define AT91_MPBS3_SFR		(AT91_MATRIX + 0x130)	/* MPBlock Slave 3 Special Function Register */
#define AT91_APB_SFR		(AT91_MATRIX + 0x134)	/* APB Bridge Special Function Register */

#endif
