/*
 * Register definitions for Atmel Serial Peripheral Interface (SPI)
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ATMEL_SPI_H__
#define __ATMEL_SPI_H__

/* SPI register offsets */
#define SPI_CR					0x0000
#define SPI_MR					0x0004
#define SPI_RDR					0x0008
#define SPI_TDR					0x000c
#define SPI_SR					0x0010
#define SPI_IER					0x0014
#define SPI_IDR					0x0018
#define SPI_IMR					0x001c
#define SPI_CSR0				0x0030
#define SPI_CSR1				0x0034
#define SPI_CSR2				0x0038
#define SPI_CSR3				0x003c
#define SPI_RPR					0x0100
#define SPI_RCR					0x0104
#define SPI_TPR					0x0108
#define SPI_TCR					0x010c
#define SPI_RNPR				0x0110
#define SPI_RNCR				0x0114
#define SPI_TNPR				0x0118
#define SPI_TNCR				0x011c
#define SPI_PTCR				0x0120
#define SPI_PTSR				0x0124

/* Bitfields in CR */
#define SPI_SPIEN_OFFSET			0
#define SPI_SPIEN_SIZE				1
#define SPI_SPIDIS_OFFSET			1
#define SPI_SPIDIS_SIZE				1
#define SPI_SWRST_OFFSET			7
#define SPI_SWRST_SIZE				1
#define SPI_LASTXFER_OFFSET			24
#define SPI_LASTXFER_SIZE			1

/* Bitfields in MR */
#define SPI_MSTR_OFFSET				0
#define SPI_MSTR_SIZE				1
#define SPI_PS_OFFSET				1
#define SPI_PS_SIZE				1
#define SPI_PCSDEC_OFFSET			2
#define SPI_PCSDEC_SIZE				1
#define SPI_FDIV_OFFSET				3
#define SPI_FDIV_SIZE				1
#define SPI_MODFDIS_OFFSET			4
#define SPI_MODFDIS_SIZE			1
#define SPI_LLB_OFFSET				7
#define SPI_LLB_SIZE				1
#define SPI_PCS_OFFSET				16
#define SPI_PCS_SIZE				4
#define SPI_DLYBCS_OFFSET			24
#define SPI_DLYBCS_SIZE				8

/* Bitfields in RDR */
#define SPI_RD_OFFSET				0
#define SPI_RD_SIZE				16

/* Bitfields in TDR */
#define SPI_TD_OFFSET				0
#define SPI_TD_SIZE				16

/* Bitfields in SR */
#define SPI_RDRF_OFFSET				0
#define SPI_RDRF_SIZE				1
#define SPI_TDRE_OFFSET				1
#define SPI_TDRE_SIZE				1
#define SPI_MODF_OFFSET				2
#define SPI_MODF_SIZE				1
#define SPI_OVRES_OFFSET			3
#define SPI_OVRES_SIZE				1
#define SPI_ENDRX_OFFSET			4
#define SPI_ENDRX_SIZE				1
#define SPI_ENDTX_OFFSET			5
#define SPI_ENDTX_SIZE				1
#define SPI_RXBUFF_OFFSET			6
#define SPI_RXBUFF_SIZE				1
#define SPI_TXBUFE_OFFSET			7
#define SPI_TXBUFE_SIZE				1
#define SPI_NSSR_OFFSET				8
#define SPI_NSSR_SIZE				1
#define SPI_TXEMPTY_OFFSET			9
#define SPI_TXEMPTY_SIZE			1
#define SPI_SPIENS_OFFSET			16
#define SPI_SPIENS_SIZE				1

/* Bitfields in CSR0 */
#define SPI_CPOL_OFFSET				0
#define SPI_CPOL_SIZE				1
#define SPI_NCPHA_OFFSET			1
#define SPI_NCPHA_SIZE				1
#define SPI_CSAAT_OFFSET			3
#define SPI_CSAAT_SIZE				1
#define SPI_BITS_OFFSET				4
#define SPI_BITS_SIZE				4
#define SPI_SCBR_OFFSET				8
#define SPI_SCBR_SIZE				8
#define SPI_DLYBS_OFFSET			16
#define SPI_DLYBS_SIZE				8
#define SPI_DLYBCT_OFFSET			24
#define SPI_DLYBCT_SIZE				8

/* Bitfields in RCR */
#define SPI_RXCTR_OFFSET			0
#define SPI_RXCTR_SIZE				16

/* Bitfields in TCR */
#define SPI_TXCTR_OFFSET			0
#define SPI_TXCTR_SIZE				16

/* Bitfields in RNCR */
#define SPI_RXNCR_OFFSET			0
#define SPI_RXNCR_SIZE				16

/* Bitfields in TNCR */
#define SPI_TXNCR_OFFSET			0
#define SPI_TXNCR_SIZE				16

/* Bitfields in PTCR */
#define SPI_RXTEN_OFFSET			0
#define SPI_RXTEN_SIZE				1
#define SPI_RXTDIS_OFFSET			1
#define SPI_RXTDIS_SIZE				1
#define SPI_TXTEN_OFFSET			8
#define SPI_TXTEN_SIZE				1
#define SPI_TXTDIS_OFFSET			9
#define SPI_TXTDIS_SIZE				1

/* Constants for BITS */
#define SPI_BITS_8_BPT				0
#define SPI_BITS_9_BPT				1
#define SPI_BITS_10_BPT				2
#define SPI_BITS_11_BPT				3
#define SPI_BITS_12_BPT				4
#define SPI_BITS_13_BPT				5
#define SPI_BITS_14_BPT				6
#define SPI_BITS_15_BPT				7
#define SPI_BITS_16_BPT				8

/* Bit manipulation macros */
#define SPI_BIT(name) \
	(1 << SPI_##name##_OFFSET)
#define SPI_BF(name,value) \
	(((value) & ((1 << SPI_##name##_SIZE) - 1)) << SPI_##name##_OFFSET)
#define SPI_BFEXT(name,value) \
	(((value) >> SPI_##name##_OFFSET) & ((1 << SPI_##name##_SIZE) - 1))
#define SPI_BFINS(name,value,old) \
	( ((old) & ~(((1 << SPI_##name##_SIZE) - 1) << SPI_##name##_OFFSET)) \
	  | SPI_BF(name,value))

/* Register access macros */
#define spi_readl(port,reg) \
	__raw_readl((port)->regs + SPI_##reg)
#define spi_writel(port,reg,value) \
	__raw_writel((value), (port)->regs + SPI_##reg)

#endif /* __ATMEL_SPI_H__ */
