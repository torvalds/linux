/*
 * Matrix-centric header file for the AT91SAM9G45 family
 *
 *  Copyright (C) 2008-2009 Atmel Corporation.
 *
 * Memory Controllers (MATRIX, EBI) - System peripherals registers.
 * Based on AT91SAM9G45 preliminary datasheet.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91SAM9G45_MATRIX_H
#define AT91SAM9G45_MATRIX_H

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
#define			AT91_MATRIX_ULBT_THIRTYTWO	(5 << 0)
#define			AT91_MATRIX_ULBT_SIXTYFOUR	(6 << 0)
#define			AT91_MATRIX_ULBT_128		(7 << 0)

#define AT91_MATRIX_SCFG0	(AT91_MATRIX + 0x40)	/* Slave Configuration Register 0 */
#define AT91_MATRIX_SCFG1	(AT91_MATRIX + 0x44)	/* Slave Configuration Register 1 */
#define AT91_MATRIX_SCFG2	(AT91_MATRIX + 0x48)	/* Slave Configuration Register 2 */
#define AT91_MATRIX_SCFG3	(AT91_MATRIX + 0x4C)	/* Slave Configuration Register 3 */
#define AT91_MATRIX_SCFG4	(AT91_MATRIX + 0x50)	/* Slave Configuration Register 4 */
#define AT91_MATRIX_SCFG5	(AT91_MATRIX + 0x54)	/* Slave Configuration Register 5 */
#define AT91_MATRIX_SCFG6	(AT91_MATRIX + 0x58)	/* Slave Configuration Register 6 */
#define AT91_MATRIX_SCFG7	(AT91_MATRIX + 0x5C)	/* Slave Configuration Register 7 */
#define		AT91_MATRIX_SLOT_CYCLE		(0x1ff << 0)	/* Maximum Number of Allowed Cycles for a Burst */
#define		AT91_MATRIX_DEFMSTR_TYPE	(3    << 16)	/* Default Master Type */
#define			AT91_MATRIX_DEFMSTR_TYPE_NONE	(0 << 16)
#define			AT91_MATRIX_DEFMSTR_TYPE_LAST	(1 << 16)
#define			AT91_MATRIX_DEFMSTR_TYPE_FIXED	(2 << 16)
#define		AT91_MATRIX_FIXED_DEFMSTR	(0xf  << 18)	/* Fixed Index of Default Master */

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

#define AT91_MATRIX_TCMR	(AT91_MATRIX + 0x110)	/* TCM Configuration Register */
#define		AT91_MATRIX_ITCM_SIZE		(0xf << 0)	/* Size of ITCM enabled memory block */
#define			AT91_MATRIX_ITCM_0		(0 << 0)
#define			AT91_MATRIX_ITCM_32		(6 << 0)
#define		AT91_MATRIX_DTCM_SIZE		(0xf << 4)	/* Size of DTCM enabled memory block */
#define			AT91_MATRIX_DTCM_0		(0 << 4)
#define			AT91_MATRIX_DTCM_32		(6 << 4)
#define			AT91_MATRIX_DTCM_64		(7 << 4)
#define		AT91_MATRIX_TCM_NWS		(0x1 << 11)	/* Wait state TCM register */
#define			AT91_MATRIX_TCM_NO_WS		(0x0 << 11)
#define			AT91_MATRIX_TCM_ONE_WS		(0x1 << 11)

#define AT91_MATRIX_VIDEO	(AT91_MATRIX + 0x118)	/* Video Mode Configuration Register */
#define		AT91C_VDEC_SEL			(0x1 <<  0) /* Video Mode Selection */
#define			AT91C_VDEC_SEL_OFF		(0 << 0)
#define			AT91C_VDEC_SEL_ON		(1 << 0)

#define AT91_MATRIX_EBICSA	(AT91_MATRIX + 0x128)	/* EBI Chip Select Assignment Register */
#define		AT91_MATRIX_EBI_CS1A		(1 << 1)	/* Chip Select 1 Assignment */
#define			AT91_MATRIX_EBI_CS1A_SMC		(0 << 1)
#define			AT91_MATRIX_EBI_CS1A_SDRAMC		(1 << 1)
#define		AT91_MATRIX_EBI_CS3A		(1 << 3)	/* Chip Select 3 Assignment */
#define			AT91_MATRIX_EBI_CS3A_SMC		(0 << 3)
#define			AT91_MATRIX_EBI_CS3A_SMC_SMARTMEDIA	(1 << 3)
#define		AT91_MATRIX_EBI_CS4A		(1 << 4)	/* Chip Select 4 Assignment */
#define			AT91_MATRIX_EBI_CS4A_SMC		(0 << 4)
#define			AT91_MATRIX_EBI_CS4A_SMC_CF0		(1 << 4)
#define		AT91_MATRIX_EBI_CS5A		(1 << 5)	/* Chip Select 5 Assignment */
#define			AT91_MATRIX_EBI_CS5A_SMC		(0 << 5)
#define			AT91_MATRIX_EBI_CS5A_SMC_CF1		(1 << 5)
#define		AT91_MATRIX_EBI_DBPUC		(1 << 8)	/* Data Bus Pull-up Configuration */
#define			AT91_MATRIX_EBI_DBPU_ON			(0 << 8)
#define			AT91_MATRIX_EBI_DBPU_OFF		(1 << 8)
#define		AT91_MATRIX_EBI_VDDIOMSEL	(1 << 16)	/* Memory voltage selection */
#define			AT91_MATRIX_EBI_VDDIOMSEL_1_8V		(0 << 16)
#define			AT91_MATRIX_EBI_VDDIOMSEL_3_3V		(1 << 16)
#define		AT91_MATRIX_EBI_EBI_IOSR	(1 << 17)	/* EBI I/O slew rate selection */
#define			AT91_MATRIX_EBI_EBI_IOSR_REDUCED	(0 << 17)
#define			AT91_MATRIX_EBI_EBI_IOSR_NORMAL		(1 << 17)
#define		AT91_MATRIX_EBI_DDR_IOSR	(1 << 18)	/* DDR2 dedicated port I/O slew rate selection */
#define			AT91_MATRIX_EBI_DDR_IOSR_REDUCED	(0 << 18)
#define			AT91_MATRIX_EBI_DDR_IOSR_NORMAL		(1 << 18)

#define AT91_MATRIX_WPMR	(AT91_MATRIX + 0x1E4)	/* Write Protect Mode Register */
#define		AT91_MATRIX_WPMR_WPEN		(1 << 0)	/* Write Protect ENable */
#define			AT91_MATRIX_WPMR_WP_WPDIS		(0 << 0)
#define			AT91_MATRIX_WPMR_WP_WPEN		(1 << 0)
#define		AT91_MATRIX_WPMR_WPKEY		(0xFFFFFF << 8)	/* Write Protect KEY */

#define AT91_MATRIX_WPSR	(AT91_MATRIX + 0x1E8)	/* Write Protect Status Register */
#define		AT91_MATRIX_WPSR_WPVS		(1 << 0)	/* Write Protect Violation Status */
#define			AT91_MATRIX_WPSR_NO_WPV		(0 << 0)
#define			AT91_MATRIX_WPSR_WPV		(1 << 0)
#define		AT91_MATRIX_WPSR_WPVSRC		(0xFFFF << 8)	/* Write Protect Violation Source */

#endif
