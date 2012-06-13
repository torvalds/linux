/*
 * Matrix-centric header file for the AT91SAM9N12
 *
 * Copyright (C) 2012 Atmel Corporation.
 *
 * Only EBI related registers.
 * Write Protect register definitions may be useful.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _AT91SAM9N12_MATRIX_H_
#define _AT91SAM9N12_MATRIX_H_

#define AT91_MATRIX_EBICSA	(AT91_MATRIX + 0x118)	/* EBI Chip Select Assignment Register */
#define		AT91_MATRIX_EBI_CS1A		(1 << 1)	/* Chip Select 1 Assignment */
#define			AT91_MATRIX_EBI_CS1A_SMC		(0 << 1)
#define			AT91_MATRIX_EBI_CS1A_SDRAMC		(1 << 1)
#define		AT91_MATRIX_EBI_CS3A		(1 << 3)	/* Chip Select 3 Assignment */
#define			AT91_MATRIX_EBI_CS3A_SMC		(0 << 3)
#define			AT91_MATRIX_EBI_CS3A_SMC_NANDFLASH	(1 << 3)
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
#define		AT91_MATRIX_NFD0_SELECT		(1 << 24)	/* NAND Flash Data Bus Selection */
#define			AT91_MATRIX_NFD0_ON_D0			(0 << 24)
#define			AT91_MATRIX_NFD0_ON_D16			(1 << 24)
#define		AT91_MATRIX_DDR_MP_EN		(1 << 25)	/* DDR Multi-port Enable */
#define			AT91_MATRIX_MP_OFF			(0 << 25)
#define			AT91_MATRIX_MP_ON			(1 << 25)

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
