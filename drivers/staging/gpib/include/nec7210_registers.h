/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright            : (C) 2002 by Frank Mori Hess
 ***************************************************************************/

#ifndef _NEC7210_REGISTERS_H
#define _NEC7210_REGISTERS_H

enum nec7210_chipset {
	NEC7210,	// The original
	TNT4882,	// NI
	NAT4882,	// NI
	CB7210,		// measurement computing
	IOT7210,	// iotech
	IGPIB7210,	// Ines
	TNT5004,	// NI (minor differences to TNT4882)
};

/*
 * nec7210 register numbers (might need to be multiplied by
 * a board-dependent offset to get actually io address offset)
 */
// write registers
enum nec7210_write_regs {
	CDOR,	// command/data out
	IMR1,	// interrupt mask 1
	IMR2,	// interrupt mask 2
	SPMR,	// serial poll mode
	ADMR,	// address mode
	AUXMR,	// auxiliary mode
	ADR,	// address
	EOSR,	// end-of-string

	// nec7210 has 8 registers
	nec7210_num_registers = 8,
};

// read registers
enum nec7210_read_regs {
	DIR,	// data in
	ISR1,	// interrupt status 1
	ISR2,	// interrupt status 2
	SPSR,	// serial poll status
	ADSR,	// address status
	CPTR,	// command pass though
	ADR0,	// address 1
	ADR1,	// address 2
};

// bit definitions common to nec-7210 compatible registers

// ISR1: interrupt status register 1
enum isr1_bits {
	HR_DI = (1 << 0),
	HR_DO = (1 << 1),
	HR_ERR = (1 << 2),
	HR_DEC = (1 << 3),
	HR_END = (1 << 4),
	HR_DET = (1 << 5),
	HR_APT = (1 << 6),
	HR_CPT = (1 << 7),
};

// IMR1: interrupt mask register 1
enum imr1_bits {
	HR_DIIE = (1 << 0),
	HR_DOIE = (1 << 1),
	HR_ERRIE = (1 << 2),
	HR_DECIE = (1 << 3),
	HR_ENDIE = (1 << 4),
	HR_DETIE = (1 << 5),
	HR_APTIE = (1 << 6),
	HR_CPTIE = (1 << 7),
};

// ISR2, interrupt status register 2
enum isr2_bits {
	HR_ADSC = (1 << 0),
	HR_REMC = (1 << 1),
	HR_LOKC = (1 << 2),
	HR_CO = (1 << 3),
	HR_REM = (1 << 4),
	HR_LOK = (1 << 5),
	HR_SRQI = (1 << 6),
	HR_INT = (1 << 7),
};

// IMR2, interrupt mask register 2
enum imr2_bits {
	// all the bits in this register that enable interrupts
	IMR2_ENABLE_INTR_MASK = 0x4f,
	HR_ACIE = (1 << 0),
	HR_REMIE = (1 << 1),
	HR_LOKIE = (1 << 2),
	HR_COIE = (1 << 3),
	HR_DMAI = (1 << 4),
	HR_DMAO = (1 << 5),
	HR_SRQIE = (1 << 6),
};

// SPSR, serial poll status register
enum spsr_bits {
	HR_PEND = (1 << 6),
};

// SPMR, serial poll mode register
enum spmr_bits {
	HR_RSV = (1 << 6),
};

// ADSR, address status register
enum adsr_bits {
	HR_MJMN = (1 << 0),
	HR_TA = (1 << 1),
	HR_LA = (1 << 2),
	HR_TPAS = (1 << 3),
	HR_LPAS = (1 << 4),
	HR_SPMS = (1 << 5),
	HR_NATN = (1 << 6),
	HR_CIC = (1 << 7),
};

// ADMR, address mode register
enum admr_bits {
	HR_ADM0 = (1 << 0),
	HR_ADM1 = (1 << 1),
	HR_TRM0 = (1 << 4),
	HR_TRM1 = (1 << 5),
	HR_TRM_EOIOE_TRIG = 0,
	HR_TRM_CIC_TRIG = HR_TRM0,
	HR_TRM_CIC_EOIOE = HR_TRM1,
	HR_TRM_CIC_PE = HR_TRM0 | HR_TRM1,
	HR_LON = (1 << 6),
	HR_TON = (1 << 7),
};

// ADR, bits used in address0, address1 and address0/1 registers
enum adr_bits {
	ADDRESS_MASK = 0x1f,	/* mask to specify lower 5 bits */
	HR_DL = (1 << 5),
	HR_DT = (1 << 6),
	HR_ARS = (1 << 7),
};

// ADR1, address1 register
enum adr1_bits {
	HR_EOI = (1 << 7),
};

// AUXMR, auxiliary mode register
enum auxmr_bits {
	ICR = 0x20,
	PPR = 0x60,
	AUXRA = 0x80,
	AUXRB = 0xa0,
	AUXRE = 0xc0,
};

// auxra, auxiliary register A
enum auxra_bits {
	HR_HANDSHAKE_MASK = 0x3,
	HR_HLDA = 0x1,
	HR_HLDE = 0x2,
	HR_LCM = 0x3,	/* auxra listen continuous */
	HR_REOS = 0x4,
	HR_XEOS = 0x8,
	HR_BIN = 0x10,
};

// auxrb, auxiliary register B
enum auxrb_bits {
	HR_CPTE = (1 << 0),
	HR_SPEOI = (1 << 1),
	HR_TRI = (1 << 2),
	HR_INV = (1 << 3),
	HR_ISS = (1 << 4),
};

enum auxre_bits {
	HR_DAC_HLD_DCAS = 0x1,	/* perform DAC holdoff on receiving clear */
	HR_DAC_HLD_DTAS = 0x2,	/* perform DAC holdoff on receiving trigger */
};

// parallel poll register
enum ppr_bits {
	HR_PPS = (1 << 3),
	HR_PPU = (1 << 4),
};

/* 7210 Auxiliary Commands */
enum aux_cmds {
	AUX_PON = 0x0,	/* Immediate Execute pon                  */
	AUX_CPPF = 0x1,	/* Clear Parallel Poll Flag               */
	AUX_CR = 0x2,	/* Chip Reset                             */
	AUX_FH = 0x3,	/* Finish Handshake                       */
	AUX_TRIG = 0x4,	/* Trigger                                */
	AUX_RTL = 0x5,	/* Return to local                        */
	AUX_SEOI = 0x6,	/* Send EOI                               */
	AUX_NVAL = 0x7,	/* Non-Valid Secondary Command or Address */
	AUX_SPPF = 0x9,	/* Set Parallel Poll Flag                 */
	AUX_VAL = 0xf,	/* Valid Secondary Command or Address     */
	AUX_GTS = 0x10,	/* Go To Standby                          */
	AUX_TCA = 0x11,	/* Take Control Asynchronously            */
	AUX_TCS = 0x12,	/* Take Control Synchronously             */
	AUX_LTN = 0x13,	/* Listen                                 */
	AUX_DSC = 0x14,	/* Disable System Control                 */
	AUX_CIFC = 0x16,	/* Clear IFC                              */
	AUX_CREN = 0x17,	/* Clear REN                              */
	AUX_TCSE = 0x1a,	/* Take Control Synchronously on End      */
	AUX_LTNC = 0x1b,	/* Listen in Continuous Mode              */
	AUX_LUN = 0x1c,	/* Local Unlisten                         */
	AUX_EPP = 0x1d,	/* Execute Parallel Poll                  */
	AUX_SIFC = 0x1e,	/* Set IFC                                */
	AUX_SREN = 0x1f,	/* Set REN                                */
};

#endif	//_NEC7210_REGISTERS_H
