/*
 * Error Corrected Code Controller (ECC) - System peripherals regsters.
 * Based on AT91SAM9260 datasheet revision B.
 *
 * Copyright (C) 2007 Andrew Victor
 * Copyright (C) 2007 - 2012 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef ATMEL_NAND_ECC_H
#define ATMEL_NAND_ECC_H

#define ATMEL_ECC_CR		0x00			/* Control register */
#define		ATMEL_ECC_RST		(1 << 0)		/* Reset parity */

#define ATMEL_ECC_MR		0x04			/* Mode register */
#define		ATMEL_ECC_PAGESIZE	(3 << 0)		/* Page Size */
#define			ATMEL_ECC_PAGESIZE_528		(0)
#define			ATMEL_ECC_PAGESIZE_1056		(1)
#define			ATMEL_ECC_PAGESIZE_2112		(2)
#define			ATMEL_ECC_PAGESIZE_4224		(3)

#define ATMEL_ECC_SR		0x08			/* Status register */
#define		ATMEL_ECC_RECERR		(1 << 0)		/* Recoverable Error */
#define		ATMEL_ECC_ECCERR		(1 << 1)		/* ECC Single Bit Error */
#define		ATMEL_ECC_MULERR		(1 << 2)		/* Multiple Errors */

#define ATMEL_ECC_PR		0x0c			/* Parity register */
#define		ATMEL_ECC_BITADDR	(0xf << 0)		/* Bit Error Address */
#define		ATMEL_ECC_WORDADDR	(0xfff << 4)		/* Word Error Address */

#define ATMEL_ECC_NPR		0x10			/* NParity register */
#define		ATMEL_ECC_NPARITY	(0xffff << 0)		/* NParity */

/* PMECC Register Definitions */
#define ATMEL_PMECC_CFG			0x000	/* Configuration Register */
#define		PMECC_CFG_BCH_ERR2		(0 << 0)
#define		PMECC_CFG_BCH_ERR4		(1 << 0)
#define		PMECC_CFG_BCH_ERR8		(2 << 0)
#define		PMECC_CFG_BCH_ERR12		(3 << 0)
#define		PMECC_CFG_BCH_ERR24		(4 << 0)

#define		PMECC_CFG_SECTOR512		(0 << 4)
#define		PMECC_CFG_SECTOR1024		(1 << 4)

#define		PMECC_CFG_PAGE_1SECTOR		(0 << 8)
#define		PMECC_CFG_PAGE_2SECTORS		(1 << 8)
#define		PMECC_CFG_PAGE_4SECTORS		(2 << 8)
#define		PMECC_CFG_PAGE_8SECTORS		(3 << 8)

#define		PMECC_CFG_READ_OP		(0 << 12)
#define		PMECC_CFG_WRITE_OP		(1 << 12)

#define		PMECC_CFG_SPARE_ENABLE		(1 << 16)
#define		PMECC_CFG_SPARE_DISABLE		(0 << 16)

#define		PMECC_CFG_AUTO_ENABLE		(1 << 20)
#define		PMECC_CFG_AUTO_DISABLE		(0 << 20)

#define ATMEL_PMECC_SAREA		0x004	/* Spare area size */
#define ATMEL_PMECC_SADDR		0x008	/* PMECC starting address */
#define ATMEL_PMECC_EADDR		0x00c	/* PMECC ending address */
#define ATMEL_PMECC_CLK			0x010	/* PMECC clock control */
#define		PMECC_CLK_133MHZ		(2 << 0)

#define ATMEL_PMECC_CTRL		0x014	/* PMECC control register */
#define		PMECC_CTRL_RST			(1 << 0)
#define		PMECC_CTRL_DATA			(1 << 1)
#define		PMECC_CTRL_USER			(1 << 2)
#define		PMECC_CTRL_ENABLE		(1 << 4)
#define		PMECC_CTRL_DISABLE		(1 << 5)

#define ATMEL_PMECC_SR			0x018	/* PMECC status register */
#define		PMECC_SR_BUSY			(1 << 0)
#define		PMECC_SR_ENABLE			(1 << 4)

#define ATMEL_PMECC_IER			0x01c	/* PMECC interrupt enable */
#define		PMECC_IER_ENABLE		(1 << 0)
#define ATMEL_PMECC_IDR			0x020	/* PMECC interrupt disable */
#define		PMECC_IER_DISABLE		(1 << 0)
#define ATMEL_PMECC_IMR			0x024	/* PMECC interrupt mask */
#define		PMECC_IER_MASK			(1 << 0)
#define ATMEL_PMECC_ISR			0x028	/* PMECC interrupt status */
#define ATMEL_PMECC_ECCx		0x040	/* PMECC ECC x */
#define ATMEL_PMECC_REMx		0x240	/* PMECC REM x */

/* PMERRLOC Register Definitions */
#define ATMEL_PMERRLOC_ELCFG		0x000	/* Error location config */
#define		PMERRLOC_ELCFG_SECTOR_512	(0 << 0)
#define		PMERRLOC_ELCFG_SECTOR_1024	(1 << 0)
#define		PMERRLOC_ELCFG_NUM_ERRORS(n)	((n) << 16)

#define ATMEL_PMERRLOC_ELPRIM		0x004	/* Error location primitive */
#define ATMEL_PMERRLOC_ELEN		0x008	/* Error location enable */
#define ATMEL_PMERRLOC_ELDIS		0x00c	/* Error location disable */
#define		PMERRLOC_DISABLE		(1 << 0)

#define ATMEL_PMERRLOC_ELSR		0x010	/* Error location status */
#define		PMERRLOC_ELSR_BUSY		(1 << 0)
#define ATMEL_PMERRLOC_ELIER		0x014	/* Error location int enable */
#define ATMEL_PMERRLOC_ELIDR		0x018	/* Error location int disable */
#define ATMEL_PMERRLOC_ELIMR		0x01c	/* Error location int mask */
#define ATMEL_PMERRLOC_ELISR		0x020	/* Error location int status */
#define		PMERRLOC_ERR_NUM_MASK		(0x1f << 8)
#define		PMERRLOC_CALC_DONE		(1 << 0)
#define ATMEL_PMERRLOC_SIGMAx		0x028	/* Error location SIGMA x */
#define ATMEL_PMERRLOC_ELx		0x08c	/* Error location x */

/* Register access macros for PMECC */
#define pmecc_readl_relaxed(addr, reg) \
	readl_relaxed((addr) + ATMEL_PMECC_##reg)

#define pmecc_writel(addr, reg, value) \
	writel((value), (addr) + ATMEL_PMECC_##reg)

#define pmecc_readb_ecc_relaxed(addr, sector, n) \
	readb_relaxed((addr) + ATMEL_PMECC_ECCx + ((sector) * 0x40) + (n))

#define pmecc_readl_rem_relaxed(addr, sector, n) \
	readl_relaxed((addr) + ATMEL_PMECC_REMx + ((sector) * 0x40) + ((n) * 4))

#define pmerrloc_readl_relaxed(addr, reg) \
	readl_relaxed((addr) + ATMEL_PMERRLOC_##reg)

#define pmerrloc_writel(addr, reg, value) \
	writel((value), (addr) + ATMEL_PMERRLOC_##reg)

#define pmerrloc_writel_sigma_relaxed(addr, n, value) \
	writel_relaxed((value), (addr) + ATMEL_PMERRLOC_SIGMAx + ((n) * 4))

#define pmerrloc_readl_sigma_relaxed(addr, n) \
	readl_relaxed((addr) + ATMEL_PMERRLOC_SIGMAx + ((n) * 4))

#define pmerrloc_readl_el_relaxed(addr, n) \
	readl_relaxed((addr) + ATMEL_PMERRLOC_ELx + ((n) * 4))

/* Galois field dimension */
#define PMECC_GF_DIMENSION_13			13
#define PMECC_GF_DIMENSION_14			14

/* Primitive Polynomial used by PMECC */
#define PMECC_GF_13_PRIMITIVE_POLY		0x201b
#define PMECC_GF_14_PRIMITIVE_POLY		0x4443

#define PMECC_LOOKUP_TABLE_SIZE_512		0x2000
#define PMECC_LOOKUP_TABLE_SIZE_1024		0x4000

/* Time out value for reading PMECC status register */
#define PMECC_MAX_TIMEOUT_MS			100

/* Reserved bytes in oob area */
#define PMECC_OOB_RESERVED_BYTES		2

#endif
