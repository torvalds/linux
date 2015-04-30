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

	SCIx_NR_REGS,
};


/* SCSMR (Serial Mode Register) */
#define SCSMR_CHR	(1 << 6)	/* 7-bit Character Length */
#define SCSMR_PE	(1 << 5)	/* Parity Enable */
#define SCSMR_ODD	(1 << 4)	/* Odd Parity */
#define SCSMR_STOP	(1 << 3)	/* Stop Bit Length */
#define SCSMR_CKS	0x0003		/* Clock Select */

/* Serial Control Register, SCIFA/SCIFB only bits */
#define SCSCR_TDRQE	(1 << 15)	/* Tx Data Transfer Request Enable */
#define SCSCR_RDRQE	(1 << 14)	/* Rx Data Transfer Request Enable */

/* SCxSR (Serial Status Register) on SCI */
#define SCI_TDRE  0x80			/* Transmit Data Register Empty */
#define SCI_RDRF  0x40			/* Receive Data Register Full */
#define SCI_ORER  0x20			/* Overrun Error */
#define SCI_FER   0x10			/* Framing Error */
#define SCI_PER   0x08			/* Parity Error */
#define SCI_TEND  0x04			/* Transmit End */

#define SCI_DEFAULT_ERROR_MASK (SCI_PER | SCI_FER)

/* SCxSR (Serial Status Register) on SCIF, HSCIF */
#define SCIF_ER    0x0080		/* Receive Error */
#define SCIF_TEND  0x0040		/* Transmission End */
#define SCIF_TDFE  0x0020		/* Transmit FIFO Data Empty */
#define SCIF_BRK   0x0010		/* Break Detect */
#define SCIF_FER   0x0008		/* Framing Error */
#define SCIF_PER   0x0004		/* Parity Error */
#define SCIF_RDF   0x0002		/* Receive FIFO Data Full */
#define SCIF_DR    0x0001		/* Receive Data Ready */

#define SCIF_DEFAULT_ERROR_MASK (SCIF_PER | SCIF_FER | SCIF_ER | SCIF_BRK)

/* SCFCR (FIFO Control Register) */
#define SCFCR_MCE	0x0008
#define SCFCR_TFRST	0x0004
#define SCFCR_RFRST	0x0002
#define SCFCR_LOOP	(1 << 0)	/* Loopback Test */

/* SCSPTR (Serial Port Register), optional */
#define SCSPTR_RTSIO	(1 << 7)	/* Serial Port RTS Pin Input/Output */
#define SCSPTR_CTSIO	(1 << 5)	/* Serial Port CTS Pin Input/Output */
#define SCSPTR_SPB2IO	(1 << 1)	/* Serial Port Break Input/Output */
#define SCSPTR_SPB2DT	(1 << 0)	/* Serial Port Break Data */

/* HSSRR HSCIF */
#define HSCIF_SRE	0x8000		/* Sampling Rate Register Enable */


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

# define SCxSR_RDxF_CLEAR(port)	 (serial_port_in(port, SCxSR) & 0xfffc)
# define SCxSR_ERROR_CLEAR(port) (serial_port_in(port, SCxSR) & 0xfd73)
# define SCxSR_TDxE_CLEAR(port)	 (serial_port_in(port, SCxSR) & 0xffdf)
# define SCxSR_BREAK_CLEAR(port) (serial_port_in(port, SCxSR) & 0xffe3)
#else
# define SCxSR_RDxF_CLEAR(port)	 (((port)->type == PORT_SCI) ? 0xbc : 0x00fc)
# define SCxSR_ERROR_CLEAR(port) (((port)->type == PORT_SCI) ? 0xc4 : 0x0073)
# define SCxSR_TDxE_CLEAR(port)  (((port)->type == PORT_SCI) ? 0x78 : 0x00df)
# define SCxSR_BREAK_CLEAR(port) (((port)->type == PORT_SCI) ? 0xc4 : 0x00e3)
#endif

