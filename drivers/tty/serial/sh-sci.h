/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/bitops.h>
#include <linux/serial_core.h>
#include <linux/io.h>

#define SCI_MAJOR		204
#define SCI_MINOR_START		8


/*
 * SCI register subset common for all port types.
 * Not all registers will exist on all parts.
 */
enum {
	SCSMR,				/* Serial Mode Register */
	SCBRR,				/* Bit Rate Register */
	SCSCR,				/* Serial Control Register */
	SCxSR,				/* Serial Status Register */
	SCFCR,				/* FIFO Control Register */
	SCFDR,				/* FIFO Data Count Register */
	SCxTDR,				/* Transmit (FIFO) Data Register */
	SCxRDR,				/* Receive (FIFO) Data Register */
	SCLSR,				/* Line Status Register */
	SCTFDR,				/* Transmit FIFO Data Count Register */
	SCRFDR,				/* Receive FIFO Data Count Register */
	SCSPTR,				/* Serial Port Register */
	HSSRR,				/* Sampling Rate Register */
	SCPCR,				/* Serial Port Control Register */
	SCPDR,				/* Serial Port Data Register */
	SCDL,				/* BRG Frequency Division Register */
	SCCKS,				/* BRG Clock Select Register */
	HSRTRGR,			/* Rx FIFO Data Count Trigger Register */
	HSTTRGR,			/* Tx FIFO Data Count Trigger Register */
	SEMR,				/* Serial extended mode register */

	SCIx_NR_REGS,
};


/* SCSMR (Serial Mode Register) */
#define SCSMR_C_A	BIT(7)	/* Communication Mode */
#define SCSMR_CSYNC	BIT(7)	/*   - Clocked synchronous mode */
#define SCSMR_ASYNC	0	/*   - Asynchronous mode */
#define SCSMR_CHR	BIT(6)	/* 7-bit Character Length */
#define SCSMR_PE	BIT(5)	/* Parity Enable */
#define SCSMR_ODD	BIT(4)	/* Odd Parity */
#define SCSMR_STOP	BIT(3)	/* Stop Bit Length */
#define SCSMR_CKS	0x0003	/* Clock Select */

/* Serial Mode Register, SCIFA/SCIFB only bits */
#define SCSMR_CKEDG	BIT(12)	/* Transmit/Receive Clock Edge Select */
#define SCSMR_SRC_MASK	0x0700	/* Sampling Control */
#define SCSMR_SRC_16	0x0000	/* Sampling rate 1/16 */
#define SCSMR_SRC_5	0x0100	/* Sampling rate 1/5 */
#define SCSMR_SRC_7	0x0200	/* Sampling rate 1/7 */
#define SCSMR_SRC_11	0x0300	/* Sampling rate 1/11 */
#define SCSMR_SRC_13	0x0400	/* Sampling rate 1/13 */
#define SCSMR_SRC_17	0x0500	/* Sampling rate 1/17 */
#define SCSMR_SRC_19	0x0600	/* Sampling rate 1/19 */
#define SCSMR_SRC_27	0x0700	/* Sampling rate 1/27 */

/* Serial Control Register, SCI only bits */
#define SCSCR_TEIE	BIT(2)  /* Transmit End Interrupt Enable */

/* Serial Control Register, SCIFA/SCIFB only bits */
#define SCSCR_TDRQE	BIT(15)	/* Tx Data Transfer Request Enable */
#define SCSCR_RDRQE	BIT(14)	/* Rx Data Transfer Request Enable */

/* Serial Control Register, HSCIF-only bits */
#define HSSCR_TOT_SHIFT	14

/* SCxSR (Serial Status Register) on SCI */
#define SCI_TDRE	BIT(7)	/* Transmit Data Register Empty */
#define SCI_RDRF	BIT(6)	/* Receive Data Register Full */
#define SCI_ORER	BIT(5)	/* Overrun Error */
#define SCI_FER		BIT(4)	/* Framing Error */
#define SCI_PER		BIT(3)	/* Parity Error */
#define SCI_TEND	BIT(2)	/* Transmit End */
#define SCI_RESERVED	0x03	/* All reserved bits */

#define SCI_DEFAULT_ERROR_MASK (SCI_PER | SCI_FER)

#define SCI_RDxF_CLEAR	(u32)(~(SCI_RESERVED | SCI_RDRF))
#define SCI_ERROR_CLEAR	(u32)(~(SCI_RESERVED | SCI_PER | SCI_FER | SCI_ORER))
#define SCI_TDxE_CLEAR	(u32)(~(SCI_RESERVED | SCI_TEND | SCI_TDRE))
#define SCI_BREAK_CLEAR	(u32)(~(SCI_RESERVED | SCI_PER | SCI_FER | SCI_ORER))

/* SCxSR (Serial Status Register) on SCIF, SCIFA, SCIFB, HSCIF */
#define SCIF_ER		BIT(7)	/* Receive Error */
#define SCIF_TEND	BIT(6)	/* Transmission End */
#define SCIF_TDFE	BIT(5)	/* Transmit FIFO Data Empty */
#define SCIF_BRK	BIT(4)	/* Break Detect */
#define SCIF_FER	BIT(3)	/* Framing Error */
#define SCIF_PER	BIT(2)	/* Parity Error */
#define SCIF_RDF	BIT(1)	/* Receive FIFO Data Full */
#define SCIF_DR		BIT(0)	/* Receive Data Ready */
/* SCIF only (optional) */
#define SCIF_PERC	0xf000	/* Number of Parity Errors */
#define SCIF_FERC	0x0f00	/* Number of Framing Errors */
/*SCIFA/SCIFB and SCIF on SH7705/SH7720/SH7721 only */
#define SCIFA_ORER	BIT(9)	/* Overrun Error */

#define SCIF_DEFAULT_ERROR_MASK (SCIF_PER | SCIF_FER | SCIF_BRK | SCIF_ER)

#define SCIF_RDxF_CLEAR		(u32)(~(SCIF_DR | SCIF_RDF))
#define SCIF_ERROR_CLEAR	(u32)(~(SCIF_PER | SCIF_FER | SCIF_ER))
#define SCIF_TDxE_CLEAR		(u32)(~(SCIF_TDFE))
#define SCIF_BREAK_CLEAR	(u32)(~(SCIF_PER | SCIF_FER | SCIF_BRK))

/* SCFCR (FIFO Control Register) */
#define SCFCR_RTRG1	BIT(7)	/* Receive FIFO Data Count Trigger */
#define SCFCR_RTRG0	BIT(6)
#define SCFCR_TTRG1	BIT(5)	/* Transmit FIFO Data Count Trigger */
#define SCFCR_TTRG0	BIT(4)
#define SCFCR_MCE	BIT(3)	/* Modem Control Enable */
#define SCFCR_TFRST	BIT(2)	/* Transmit FIFO Data Register Reset */
#define SCFCR_RFRST	BIT(1)	/* Receive FIFO Data Register Reset */
#define SCFCR_LOOP	BIT(0)	/* Loopback Test */

/* SCLSR (Line Status Register) on (H)SCIF */
#define SCLSR_TO	BIT(2)	/* Timeout */
#define SCLSR_ORER	BIT(0)	/* Overrun Error */

/* SCSPTR (Serial Port Register), optional */
#define SCSPTR_RTSIO	BIT(7)	/* Serial Port RTS# Pin Input/Output */
#define SCSPTR_RTSDT	BIT(6)	/* Serial Port RTS# Pin Data */
#define SCSPTR_CTSIO	BIT(5)	/* Serial Port CTS# Pin Input/Output */
#define SCSPTR_CTSDT	BIT(4)	/* Serial Port CTS# Pin Data */
#define SCSPTR_SCKIO	BIT(3)	/* Serial Port Clock Pin Input/Output */
#define SCSPTR_SCKDT	BIT(2)	/* Serial Port Clock Pin Data */
#define SCSPTR_SPB2IO	BIT(1)	/* Serial Port Break Input/Output */
#define SCSPTR_SPB2DT	BIT(0)	/* Serial Port Break Data */

/* HSSRR HSCIF */
#define HSCIF_SRE	BIT(15)	/* Sampling Rate Register Enable */
#define HSCIF_SRDE	BIT(14) /* Sampling Point Register Enable */

#define HSCIF_SRHP_SHIFT	8
#define HSCIF_SRHP_MASK		0x0f00

/* SCPCR (Serial Port Control Register), SCIFA/SCIFB only */
#define SCPCR_RTSC	BIT(4)	/* Serial Port RTS# Pin / Output Pin */
#define SCPCR_CTSC	BIT(3)	/* Serial Port CTS# Pin / Input Pin */
#define SCPCR_SCKC	BIT(2)	/* Serial Port SCK Pin / Output Pin */
#define SCPCR_RXDC	BIT(1)	/* Serial Port RXD Pin / Input Pin */
#define SCPCR_TXDC	BIT(0)	/* Serial Port TXD Pin / Output Pin */

/* SCPDR (Serial Port Data Register), SCIFA/SCIFB only */
#define SCPDR_RTSD	BIT(4)	/* Serial Port RTS# Output Pin Data */
#define SCPDR_CTSD	BIT(3)	/* Serial Port CTS# Input Pin Data */
#define SCPDR_SCKD	BIT(2)	/* Serial Port SCK Output Pin Data */
#define SCPDR_RXDD	BIT(1)	/* Serial Port RXD Input Pin Data */
#define SCPDR_TXDD	BIT(0)	/* Serial Port TXD Output Pin Data */

/*
 * BRG Clock Select Register (Some SCIF and HSCIF)
 * The Baud Rate Generator for external clock can provide a clock source for
 * the sampling clock. It outputs either its frequency divided clock, or the
 * (undivided) (H)SCK external clock.
 */
#define SCCKS_CKS	BIT(15)	/* Select (H)SCK (1) or divided SC_CLK (0) */
#define SCCKS_XIN	BIT(14)	/* SC_CLK uses bus clock (1) or SCIF_CLK (0) */

#define SCxSR_TEND(port)	(((port)->type == PORT_SCI) ? SCI_TEND   : SCIF_TEND)
#define SCxSR_RDxF(port)	(((port)->type == PORT_SCI) ? SCI_RDRF   : SCIF_DR | SCIF_RDF)
#define SCxSR_TDxE(port)	(((port)->type == PORT_SCI) ? SCI_TDRE   : SCIF_TDFE)
#define SCxSR_FER(port)		(((port)->type == PORT_SCI) ? SCI_FER    : SCIF_FER)
#define SCxSR_PER(port)		(((port)->type == PORT_SCI) ? SCI_PER    : SCIF_PER)
#define SCxSR_BRK(port)		(((port)->type == PORT_SCI) ? 0x00       : SCIF_BRK)

#define SCxSR_ERRORS(port)	(to_sci_port(port)->params->error_mask)

#define SCxSR_RDxF_CLEAR(port) \
	(((port)->type == PORT_SCI) ? SCI_RDxF_CLEAR : SCIF_RDxF_CLEAR)
#define SCxSR_ERROR_CLEAR(port) \
	(to_sci_port(port)->params->error_clear)
#define SCxSR_TDxE_CLEAR(port) \
	(((port)->type == PORT_SCI) ? SCI_TDxE_CLEAR : SCIF_TDxE_CLEAR)
#define SCxSR_BREAK_CLEAR(port) \
	(((port)->type == PORT_SCI) ? SCI_BREAK_CLEAR : SCIF_BREAK_CLEAR)
