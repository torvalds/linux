/*
 * Copyright (c) 2008 Nuovation System Designs, LLC
 *   Grant Erickson <gerickson@nuovations.com>
 *
 * This file defines processor mnemonics for accessing and managing
 * the IBM DDR1/DDR2 ECC controller found in the 405EX[r], 440SP,
 * 440SPe, 460EX, 460GT and 460SX.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#ifndef __PPC4XX_EDAC_H
#define __PPC4XX_EDAC_H

#include <linux/types.h>

/*
 * Macro for generating register field mnemonics
 */
#define PPC_REG_BITS			32
#define PPC_REG_VAL(bit, val)		((val) << ((PPC_REG_BITS - 1) - (bit)))
#define PPC_REG_DECODE(bit, val)	((val) >> ((PPC_REG_BITS - 1) - (bit)))

/*
 * IBM 4xx DDR1/DDR2 SDRAM memory controller registers (at least those
 * relevant to ECC)
 */
#define SDRAM_BESR			0x00	/* Error status (read/clear) */
#define SDRAM_BESRT			0x01	/* Error statuss (test/set)  */
#define SDRAM_BEARL			0x02	/* Error address low	     */
#define SDRAM_BEARH			0x03	/* Error address high	     */
#define SDRAM_WMIRQ			0x06	/* Write master (read/clear) */
#define SDRAM_WMIRQT			0x07	/* Write master (test/set)   */
#define SDRAM_MCOPT1			0x20	/* Controller options 1	     */
#define SDRAM_MBXCF_BASE		0x40	/* Bank n configuration base */
#define	SDRAM_MBXCF(n)			(SDRAM_MBXCF_BASE + (4 * (n)))
#define SDRAM_MB0CF			SDRAM_MBXCF(0)
#define SDRAM_MB1CF			SDRAM_MBXCF(1)
#define SDRAM_MB2CF			SDRAM_MBXCF(2)
#define SDRAM_MB3CF			SDRAM_MBXCF(3)
#define SDRAM_ECCCR			0x98	/* ECC error status	     */
#define SDRAM_ECCES			SDRAM_ECCCR

/*
 * PLB Master IDs
 */
#define	SDRAM_PLB_M0ID_FIRST		0
#define	SDRAM_PLB_M0ID_ICU		SDRAM_PLB_M0ID_FIRST
#define	SDRAM_PLB_M0ID_PCIE0		1
#define	SDRAM_PLB_M0ID_PCIE1		2
#define	SDRAM_PLB_M0ID_DMA		3
#define	SDRAM_PLB_M0ID_DCU		4
#define	SDRAM_PLB_M0ID_OPB		5
#define	SDRAM_PLB_M0ID_MAL		6
#define	SDRAM_PLB_M0ID_SEC		7
#define	SDRAM_PLB_M0ID_AHB		8
#define SDRAM_PLB_M0ID_LAST		SDRAM_PLB_M0ID_AHB
#define SDRAM_PLB_M0ID_COUNT		(SDRAM_PLB_M0ID_LAST - \
					 SDRAM_PLB_M0ID_FIRST + 1)

/*
 * Memory Controller Bus Error Status Register
 */
#define SDRAM_BESR_MASK			PPC_REG_VAL(7, 0xFF)
#define SDRAM_BESR_M0ID_MASK		PPC_REG_VAL(3, 0xF)
#define	SDRAM_BESR_M0ID_DECODE(n)	PPC_REG_DECODE(3, n)
#define SDRAM_BESR_M0ID_ICU		PPC_REG_VAL(3, SDRAM_PLB_M0ID_ICU)
#define SDRAM_BESR_M0ID_PCIE0		PPC_REG_VAL(3, SDRAM_PLB_M0ID_PCIE0)
#define SDRAM_BESR_M0ID_PCIE1		PPC_REG_VAL(3, SDRAM_PLB_M0ID_PCIE1)
#define SDRAM_BESR_M0ID_DMA		PPC_REG_VAL(3, SDRAM_PLB_M0ID_DMA)
#define SDRAM_BESR_M0ID_DCU		PPC_REG_VAL(3, SDRAM_PLB_M0ID_DCU)
#define SDRAM_BESR_M0ID_OPB		PPC_REG_VAL(3, SDRAM_PLB_M0ID_OPB)
#define SDRAM_BESR_M0ID_MAL		PPC_REG_VAL(3, SDRAM_PLB_M0ID_MAL)
#define SDRAM_BESR_M0ID_SEC		PPC_REG_VAL(3, SDRAM_PLB_M0ID_SEC)
#define SDRAM_BESR_M0ID_AHB		PPC_REG_VAL(3, SDRAM_PLB_M0ID_AHB)
#define SDRAM_BESR_M0ET_MASK		PPC_REG_VAL(6, 0x7)
#define SDRAM_BESR_M0ET_NONE		PPC_REG_VAL(6, 0)
#define SDRAM_BESR_M0ET_ECC		PPC_REG_VAL(6, 1)
#define SDRAM_BESR_M0RW_MASK		PPC_REG_VAL(7, 1)
#define SDRAM_BESR_M0RW_WRITE		PPC_REG_VAL(7, 0)
#define SDRAM_BESR_M0RW_READ		PPC_REG_VAL(7, 1)

/*
 * Memory Controller PLB Write Master Interrupt Register
 */
#define SDRAM_WMIRQ_MASK		PPC_REG_VAL(8, 0x1FF)
#define	SDRAM_WMIRQ_ENCODE(id)		PPC_REG_VAL((id % \
						     SDRAM_PLB_M0ID_COUNT), 1)
#define SDRAM_WMIRQ_ICU			PPC_REG_VAL(SDRAM_PLB_M0ID_ICU, 1)
#define SDRAM_WMIRQ_PCIE0		PPC_REG_VAL(SDRAM_PLB_M0ID_PCIE0, 1)
#define SDRAM_WMIRQ_PCIE1		PPC_REG_VAL(SDRAM_PLB_M0ID_PCIE1, 1)
#define SDRAM_WMIRQ_DMA			PPC_REG_VAL(SDRAM_PLB_M0ID_DMA, 1)
#define SDRAM_WMIRQ_DCU			PPC_REG_VAL(SDRAM_PLB_M0ID_DCU, 1)
#define SDRAM_WMIRQ_OPB			PPC_REG_VAL(SDRAM_PLB_M0ID_OPB, 1)
#define SDRAM_WMIRQ_MAL			PPC_REG_VAL(SDRAM_PLB_M0ID_MAL, 1)
#define SDRAM_WMIRQ_SEC			PPC_REG_VAL(SDRAM_PLB_M0ID_SEC, 1)
#define SDRAM_WMIRQ_AHB			PPC_REG_VAL(SDRAM_PLB_M0ID_AHB, 1)

/*
 * Memory Controller Options 1 Register
 */
#define SDRAM_MCOPT1_MCHK_MASK	    PPC_REG_VAL(3, 0x3)	 /* ECC mask	     */
#define SDRAM_MCOPT1_MCHK_NON	    PPC_REG_VAL(3, 0x0)	 /* No ECC gen	     */
#define SDRAM_MCOPT1_MCHK_GEN	    PPC_REG_VAL(3, 0x2)	 /* ECC gen	     */
#define SDRAM_MCOPT1_MCHK_CHK	    PPC_REG_VAL(3, 0x1)	 /* ECC gen and chk  */
#define SDRAM_MCOPT1_MCHK_CHK_REP   PPC_REG_VAL(3, 0x3)	 /* ECC gen/chk/rpt  */
#define SDRAM_MCOPT1_MCHK_DECODE(n) ((((u32)(n)) >> 28) & 0x3)
#define SDRAM_MCOPT1_RDEN_MASK	    PPC_REG_VAL(4, 0x1)	 /* Rgstrd DIMM mask */
#define SDRAM_MCOPT1_RDEN	    PPC_REG_VAL(4, 0x1)	 /* Rgstrd DIMM enbl */
#define SDRAM_MCOPT1_WDTH_MASK	    PPC_REG_VAL(7, 0x1)	 /* Width mask	     */
#define SDRAM_MCOPT1_WDTH_32	    PPC_REG_VAL(7, 0x0)	 /* 32 bits	     */
#define SDRAM_MCOPT1_WDTH_16	    PPC_REG_VAL(7, 0x1)	 /* 16 bits	     */
#define SDRAM_MCOPT1_DDR_TYPE_MASK  PPC_REG_VAL(11, 0x1) /* DDR type mask    */
#define SDRAM_MCOPT1_DDR1_TYPE	    PPC_REG_VAL(11, 0x0) /* DDR1 type	     */
#define SDRAM_MCOPT1_DDR2_TYPE	    PPC_REG_VAL(11, 0x1) /* DDR2 type	     */

/*
 * Memory Bank 0 - n Configuration Register
 */
#define SDRAM_MBCF_BA_MASK		PPC_REG_VAL(12, 0x1FFF)
#define SDRAM_MBCF_SZ_MASK		PPC_REG_VAL(19, 0xF)
#define SDRAM_MBCF_SZ_DECODE(mbxcf)	PPC_REG_DECODE(19, mbxcf)
#define SDRAM_MBCF_SZ_4MB		PPC_REG_VAL(19, 0x0)
#define SDRAM_MBCF_SZ_8MB		PPC_REG_VAL(19, 0x1)
#define SDRAM_MBCF_SZ_16MB		PPC_REG_VAL(19, 0x2)
#define SDRAM_MBCF_SZ_32MB		PPC_REG_VAL(19, 0x3)
#define SDRAM_MBCF_SZ_64MB		PPC_REG_VAL(19, 0x4)
#define SDRAM_MBCF_SZ_128MB		PPC_REG_VAL(19, 0x5)
#define SDRAM_MBCF_SZ_256MB		PPC_REG_VAL(19, 0x6)
#define SDRAM_MBCF_SZ_512MB		PPC_REG_VAL(19, 0x7)
#define SDRAM_MBCF_SZ_1GB		PPC_REG_VAL(19, 0x8)
#define SDRAM_MBCF_SZ_2GB		PPC_REG_VAL(19, 0x9)
#define SDRAM_MBCF_SZ_4GB		PPC_REG_VAL(19, 0xA)
#define SDRAM_MBCF_SZ_8GB		PPC_REG_VAL(19, 0xB)
#define SDRAM_MBCF_AM_MASK		PPC_REG_VAL(23, 0xF)
#define SDRAM_MBCF_AM_MODE0		PPC_REG_VAL(23, 0x0)
#define SDRAM_MBCF_AM_MODE1		PPC_REG_VAL(23, 0x1)
#define SDRAM_MBCF_AM_MODE2		PPC_REG_VAL(23, 0x2)
#define SDRAM_MBCF_AM_MODE3		PPC_REG_VAL(23, 0x3)
#define SDRAM_MBCF_AM_MODE4		PPC_REG_VAL(23, 0x4)
#define SDRAM_MBCF_AM_MODE5		PPC_REG_VAL(23, 0x5)
#define SDRAM_MBCF_AM_MODE6		PPC_REG_VAL(23, 0x6)
#define SDRAM_MBCF_AM_MODE7		PPC_REG_VAL(23, 0x7)
#define SDRAM_MBCF_AM_MODE8		PPC_REG_VAL(23, 0x8)
#define SDRAM_MBCF_AM_MODE9		PPC_REG_VAL(23, 0x9)
#define SDRAM_MBCF_BE_MASK		PPC_REG_VAL(31, 0x1)
#define SDRAM_MBCF_BE_DISABLE		PPC_REG_VAL(31, 0x0)
#define SDRAM_MBCF_BE_ENABLE		PPC_REG_VAL(31, 0x1)

/*
 * ECC Error Status
 */
#define SDRAM_ECCES_MASK		PPC_REG_VAL(21, 0x3FFFFF)
#define SDRAM_ECCES_BNCE_MASK		PPC_REG_VAL(15, 0xFFFF)
#define SDRAM_ECCES_BNCE_ENCODE(lane)	PPC_REG_VAL(((lane) & 0xF), 1)
#define SDRAM_ECCES_CKBER_MASK		PPC_REG_VAL(17, 0x3)
#define SDRAM_ECCES_CKBER_NONE		PPC_REG_VAL(17, 0)
#define SDRAM_ECCES_CKBER_16_ECC_0_3	PPC_REG_VAL(17, 2)
#define SDRAM_ECCES_CKBER_32_ECC_0_3	PPC_REG_VAL(17, 1)
#define SDRAM_ECCES_CKBER_32_ECC_4_8	PPC_REG_VAL(17, 2)
#define SDRAM_ECCES_CKBER_32_ECC_0_8	PPC_REG_VAL(17, 3)
#define SDRAM_ECCES_CE			PPC_REG_VAL(18, 1)
#define SDRAM_ECCES_UE			PPC_REG_VAL(19, 1)
#define SDRAM_ECCES_BKNER_MASK		PPC_REG_VAL(21, 0x3)
#define SDRAM_ECCES_BK0ER		PPC_REG_VAL(20, 1)
#define SDRAM_ECCES_BK1ER		PPC_REG_VAL(21, 1)

#endif /* __PPC4XX_EDAC_H */
