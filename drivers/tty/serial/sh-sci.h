#include <linux/bitops.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/gpio.h>

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

	SCIx_NR_REGS,
};


/* SCSMR (Serial Mode Register) */
#define SCSMR_CHR	BIT(6)	/* 7-bit Character Length */
#define SCSMR_PE	BIT(5)	/* Parity Enable */
#define SCSMR_ODD	BIT(4)	/* Odd Parity */
#define SCSMR_STOP	BIT(3)	/* Stop Bit Length */
#define SCSMR_CKS	0x0003	/* Clock Select */

/* Serial Control Register, SCIFA/SCIFB only bits */
#define SCSCR_TDRQE	BIT(15)	/* Tx Data Transfer Request Enable */
#define SCSCR_RDRQE	BIT(14)	/* Rx Data Transfer Request Enable */

/* SCxSR (Serial Status Register) on SCI */
#define SCI_TDRE	BIT(7)	/* Transmit Data Register Empty */
#define SCI_RDRF	BIT(6)	/* Receive Data Register Full */
#define SCI_ORER	BIT(5)	/* Overrun Error */
#define SCI_FER		BIT(4)	/* Framing Error */
#define SCI_PER		BIT(3)	/* Parity Error */
#define SCI_TEND	BIT(2)	/* Transmit End */
#define SCI_RESERVED	0x03	/* All reserved bits */

#define SCI_DEFAULT_ERROR_MASK (SCI_PER | SCI_FER)

#define SCI_RDxF_CLEAR	~(SCI_RESERVED | SCI_RDRF)
#define SCI_ERROR_CLEAR	~(SCI_RESERVED | SCI_PER | SCI_FER | SCI_ORER)
#define SCI_TDxE_CLEAR	~(SCI_RESERVED | SCI_TEND | SCI_TDRE)
#define SCI_BREAK_CLEAR	~(SCI_RESERVED | SCI_PER | SCI_FER | SCI_ORER)

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

#define SCIF_RDxF_CLEAR		~(SCIF_DR | SCIF_RDF)
#define SCIF_ERROR_CLEAR	~(SCIFA_ORER | SCIF_PER | SCIF_FER | SCIF_ER)
#define SCIF_TDxE_CLEAR		~(SCIF_TDFE)
#define SCIF_BREAK_CLEAR	~(SCIF_PER | SCIF_FER | SCIF_BRK)

/* SCFCR (FIFO Control Register) */
#define SCFCR_MCE	BIT(3)	/* Modem Control Enable */
#define SCFCR_TFRST	BIT(2)	/* Transmit FIFO Data Register Reset */
#define SCFCR_RFRST	BIT(1)	/* Receive FIFO Data Register Reset */
#define SCFCR_LOOP	BIT(0)	/* Loopback Test */

/* SCLSR (Line Status Register) on (H)SCIF */
#define SCLSR_ORER	BIT(0)	/* Overrun Error */

/* SCSPTR (Serial Port Register), optional */
#define SCSPTR_RTSIO	BIT(7)	/* Serial Port RTS Pin Input/Output */
#define SCSPTR_RTSDT	BIT(6)	/* Serial Port RTS Pin Data */
#define SCSPTR_CTSIO	BIT(5)	/* Serial Port CTS Pin Input/Output */
#define SCSPTR_CTSDT	BIT(4)	/* Serial Port CTS Pin Data */
#define SCSPTR_SPB2IO	BIT(1)	/* Serial Port Break Input/Output */
#define SCSPTR_SPB2DT	BIT(0)	/* Serial Port Break Data */

/* HSSRR HSCIF */
#define HSCIF_SRE	BIT(15)	/* Sampling Rate Register Enable */

/* SCPCR (Serial Port Control Register), SCIFA/SCIFB only */
#define SCPCR_RTSC	BIT(4)	/* Serial Port RTS Pin / Output Pin */
#define SCPCR_CTSC	BIT(3)	/* Serial Port CTS Pin / Input Pin */

/* SCPDR (Serial Port Data Register), SCIFA/SCIFB only */
#define SCPDR_RTSD	BIT(4)	/* Serial Port RTS Output Pin Data */
#define SCPDR_CTSD	BIT(3)	/* Serial Port CTS Input Pin Data */


#define SCxSR_TEND(port)	(((port)->type == PORT_SCI) ? SCI_TEND   : SCIF_TEND)
#define SCxSR_RDxF(port)	(((port)->type == PORT_SCI) ? SCI_RDRF   : SCIF_RDF)
#define SCxSR_TDxE(port)	(((port)->type == PORT_SCI) ? SCI_TDRE   : SCIF_TDFE)
#define SCxSR_FER(port)		(((port)->type == PORT_SCI) ? SCI_FER    : SCIF_FER)
#define SCxSR_PER(port)		(((port)->type == PORT_SCI) ? SCI_PER    : SCIF_PER)
#define SCxSR_BRK(port)		(((port)->type == PORT_SCI) ? 0x00       : SCIF_BRK)

#define SCxSR_ERRORS(port)	(to_sci_port(port)->error_mask)

#if defined(CONFIG_CPU_SUBTYPE_SH7705) || \
    defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721) || \
    defined(CONFIG_ARCH_SH73A0) || \
    defined(CONFIG_ARCH_R8A7740)

# define SCxSR_RDxF_CLEAR(port) \
	(serial_port_in(port, SCxSR) & SCIF_RDxF_CLEAR)
# define SCxSR_ERROR_CLEAR(port) \
	(serial_port_in(port, SCxSR) & SCIF_ERROR_CLEAR)
# define SCxSR_TDxE_CLEAR(port) \
	(serial_port_in(port, SCxSR) & SCIF_TDxE_CLEAR)
# define SCxSR_BREAK_CLEAR(port) \
	(serial_port_in(port, SCxSR) & SCIF_BREAK_CLEAR)
#else
# define SCxSR_RDxF_CLEAR(port) \
	((((port)->type == PORT_SCI) ? SCI_RDxF_CLEAR : SCIF_RDxF_CLEAR) & 0xff)
# define SCxSR_ERROR_CLEAR(port) \
	((((port)->type == PORT_SCI) ? SCI_ERROR_CLEAR : SCIF_ERROR_CLEAR) & 0xff)
# define SCxSR_TDxE_CLEAR(port) \
	((((port)->type == PORT_SCI) ? SCI_TDxE_CLEAR : SCIF_TDxE_CLEAR) & 0xff)
# define SCxSR_BREAK_CLEAR(port) \
	((((port)->type == PORT_SCI) ? SCI_BREAK_CLEAR : SCIF_BREAK_CLEAR) & 0xff)
#endif

