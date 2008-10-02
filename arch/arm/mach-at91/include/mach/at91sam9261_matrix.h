/*
 * arch/arm/mach-at91/include/mach/at91sam9261_matrix.h
 *
 * Memory Controllers (MATRIX, EBI) - System peripherals registers.
 * Based on AT91SAM9261 datasheet revision D.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91SAM9261_MATRIX_H
#define AT91SAM9261_MATRIX_H

#define AT91_MATRIX_MCFG	(AT91_MATRIX + 0x00)	/* Master Configuration Register */
#define		AT91_MATRIX_RCB0	(1 << 0)		/* Remap Command for AHB Master 0 (ARM926EJ-S Instruction Master) */
#define		AT91_MATRIX_RCB1	(1 << 1)		/* Remap Command for AHB Master 1 (ARM926EJ-S Data Master) */

#define AT91_MATRIX_SCFG0	(AT91_MATRIX + 0x04)	/* Slave Configuration Register 0 */
#define AT91_MATRIX_SCFG1	(AT91_MATRIX + 0x08)	/* Slave Configuration Register 1 */
#define AT91_MATRIX_SCFG2	(AT91_MATRIX + 0x0C)	/* Slave Configuration Register 2 */
#define AT91_MATRIX_SCFG3	(AT91_MATRIX + 0x10)	/* Slave Configuration Register 3 */
#define AT91_MATRIX_SCFG4	(AT91_MATRIX + 0x14)	/* Slave Configuration Register 4 */
#define		AT91_MATRIX_SLOT_CYCLE		(0xff << 0)	/* Maximum Number of Allowed Cycles for a Burst */
#define		AT91_MATRIX_DEFMSTR_TYPE	(3    << 16)	/* Default Master Type */
#define			AT91_MATRIX_DEFMSTR_TYPE_NONE	(0 << 16)
#define			AT91_MATRIX_DEFMSTR_TYPE_LAST	(1 << 16)
#define			AT91_MATRIX_DEFMSTR_TYPE_FIXED	(2 << 16)
#define		AT91_MATRIX_FIXED_DEFMSTR	(7    << 18)	/* Fixed Index of Default Master */

#define AT91_MATRIX_TCR		(AT91_MATRIX + 0x24)	/* TCM Configuration Register */
#define		AT91_MATRIX_ITCM_SIZE		(0xf << 0)	/* Size of ITCM enabled memory block */
#define			AT91_MATRIX_ITCM_0		(0 << 0)
#define			AT91_MATRIX_ITCM_16		(5 << 0)
#define			AT91_MATRIX_ITCM_32		(6 << 0)
#define			AT91_MATRIX_ITCM_64		(7 << 0)
#define		AT91_MATRIX_DTCM_SIZE		(0xf << 4)	/* Size of DTCM enabled memory block */
#define			AT91_MATRIX_DTCM_0		(0 << 4)
#define			AT91_MATRIX_DTCM_16		(5 << 4)
#define			AT91_MATRIX_DTCM_32		(6 << 4)
#define			AT91_MATRIX_DTCM_64		(7 << 4)

#define AT91_MATRIX_EBICSA	(AT91_MATRIX + 0x30)	/* EBI Chip Select Assignment Register */
#define		AT91_MATRIX_CS1A		(1 << 1)	/* Chip Select 1 Assignment */
#define			AT91_MATRIX_CS1A_SMC		(0 << 1)
#define			AT91_MATRIX_CS1A_SDRAMC		(1 << 1)
#define		AT91_MATRIX_CS3A		(1 << 3)	/* Chip Select 3 Assignment */
#define			AT91_MATRIX_CS3A_SMC		(0 << 3)
#define			AT91_MATRIX_CS3A_SMC_SMARTMEDIA	(1 << 3)
#define		AT91_MATRIX_CS4A		(1 << 4)	/* Chip Select 4 Assignment */
#define			AT91_MATRIX_CS4A_SMC		(0 << 4)
#define			AT91_MATRIX_CS4A_SMC_CF1	(1 << 4)
#define		AT91_MATRIX_CS5A		(1 << 5)	/* Chip Select 5 Assignment */
#define			AT91_MATRIX_CS5A_SMC		(0 << 5)
#define			AT91_MATRIX_CS5A_SMC_CF2	(1 << 5)
#define		AT91_MATRIX_DBPUC		(1 << 8)	/* Data Bus Pull-up Configuration */

#define AT91_MATRIX_USBPUCR	(AT91_MATRIX + 0x34)	/* USB Pad Pull-Up Control Register */
#define		AT91_MATRIX_USBPUCR_PUON	(1 << 30)	/* USB Device PAD Pull-up Enable */

#endif
