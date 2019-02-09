/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PMAC_ZILOG_H__
#define __PMAC_ZILOG_H__

/*
 * At most 2 ESCCs with 2 ports each
 */
#define MAX_ZS_PORTS	4

/* 
 * We wrap our port structure around the generic uart_port.
 */
#define NUM_ZSREGS    17

struct uart_pmac_port {
	struct uart_port		port;
	struct uart_pmac_port		*mate;

#ifdef CONFIG_PPC_PMAC
	/* macio_dev for the escc holding this port (maybe be null on
	 * early inited port)
	 */
	struct macio_dev		*dev;
	/* device node to this port, this points to one of 2 childs
	 * of "escc" node (ie. ch-a or ch-b)
	 */
	struct device_node		*node;
#else
	struct platform_device		*pdev;
#endif

	/* Port type as obtained from device tree (IRDA, modem, ...) */
	int				port_type;
	u8				curregs[NUM_ZSREGS];

	unsigned int			flags;
#define PMACZILOG_FLAG_IS_CONS		0x00000001
#define PMACZILOG_FLAG_IS_KGDB		0x00000002
#define PMACZILOG_FLAG_MODEM_STATUS	0x00000004
#define PMACZILOG_FLAG_IS_CHANNEL_A	0x00000008
#define PMACZILOG_FLAG_REGS_HELD	0x00000010
#define PMACZILOG_FLAG_TX_STOPPED	0x00000020
#define PMACZILOG_FLAG_TX_ACTIVE	0x00000040
#define PMACZILOG_FLAG_IS_IRDA		0x00000100
#define PMACZILOG_FLAG_IS_INTMODEM	0x00000200
#define PMACZILOG_FLAG_HAS_DMA		0x00000400
#define PMACZILOG_FLAG_RSRC_REQUESTED	0x00000800
#define PMACZILOG_FLAG_IS_OPEN		0x00002000
#define PMACZILOG_FLAG_IS_EXTCLK	0x00008000
#define PMACZILOG_FLAG_BREAK		0x00010000

	unsigned char			parity_mask;
	unsigned char			prev_status;

	volatile u8			__iomem *control_reg;
	volatile u8			__iomem *data_reg;

#ifdef CONFIG_PPC_PMAC
	unsigned int			tx_dma_irq;
	unsigned int			rx_dma_irq;
	volatile struct dbdma_regs	__iomem *tx_dma_regs;
	volatile struct dbdma_regs	__iomem *rx_dma_regs;
#endif

	unsigned char			irq_name[8];

	struct ktermios			termios_cache;
};

#define to_pmz(p) ((struct uart_pmac_port *)(p))

static inline struct uart_pmac_port *pmz_get_port_A(struct uart_pmac_port *uap)
{
	if (uap->flags & PMACZILOG_FLAG_IS_CHANNEL_A)
		return uap;
	return uap->mate;
}

/*
 * Register accessors. Note that we don't need to enforce a recovery
 * delay on PCI PowerMac hardware, it's dealt in HW by the MacIO chip,
 * though if we try to use this driver on older machines, we might have
 * to add it back
 */
static inline u8 read_zsreg(struct uart_pmac_port *port, u8 reg)
{
	if (reg != 0)
		writeb(reg, port->control_reg);
	return readb(port->control_reg);
}

static inline void write_zsreg(struct uart_pmac_port *port, u8 reg, u8 value)
{
	if (reg != 0)
		writeb(reg, port->control_reg);
	writeb(value, port->control_reg);
}

static inline u8 read_zsdata(struct uart_pmac_port *port)
{
	return readb(port->data_reg);
}

static inline void write_zsdata(struct uart_pmac_port *port, u8 data)
{
	writeb(data, port->data_reg);
}

static inline void zssync(struct uart_pmac_port *port)
{
	(void)readb(port->control_reg);
}

/* Conversion routines to/from brg time constants from/to bits
 * per second.
 */
#define BRG_TO_BPS(brg, freq) ((freq) / 2 / ((brg) + 2))
#define BPS_TO_BRG(bps, freq) ((((freq) + (bps)) / (2 * (bps))) - 2)

#define ZS_CLOCK         3686400	/* Z8530 RTxC input clock rate */

/* The Zilog register set */

#define	FLAG	0x7e

/* Write Register 0 */
#define	R0	0		/* Register selects */
#define	R1	1
#define	R2	2
#define	R3	3
#define	R4	4
#define	R5	5
#define	R6	6
#define	R7	7
#define	R8	8
#define	R9	9
#define	R10	10
#define	R11	11
#define	R12	12
#define	R13	13
#define	R14	14
#define	R15	15
#define	R7P	16

#define	NULLCODE	0	/* Null Code */
#define	POINT_HIGH	0x8	/* Select upper half of registers */
#define	RES_EXT_INT	0x10	/* Reset Ext. Status Interrupts */
#define	SEND_ABORT	0x18	/* HDLC Abort */
#define	RES_RxINT_FC	0x20	/* Reset RxINT on First Character */
#define	RES_Tx_P	0x28	/* Reset TxINT Pending */
#define	ERR_RES		0x30	/* Error Reset */
#define	RES_H_IUS	0x38	/* Reset highest IUS */

#define	RES_Rx_CRC	0x40	/* Reset Rx CRC Checker */
#define	RES_Tx_CRC	0x80	/* Reset Tx CRC Checker */
#define	RES_EOM_L	0xC0	/* Reset EOM latch */

/* Write Register 1 */

#define	EXT_INT_ENAB	0x1	/* Ext Int Enable */
#define	TxINT_ENAB	0x2	/* Tx Int Enable */
#define	PAR_SPEC	0x4	/* Parity is special condition */

#define	RxINT_DISAB	0	/* Rx Int Disable */
#define	RxINT_FCERR	0x8	/* Rx Int on First Character Only or Error */
#define	INT_ALL_Rx	0x10	/* Int on all Rx Characters or error */
#define	INT_ERR_Rx	0x18	/* Int on error only */
#define RxINT_MASK	0x18

#define	WT_RDY_RT	0x20	/* W/Req reflects recv if 1, xmit if 0 */
#define	WT_FN_RDYFN	0x40	/* W/Req pin is DMA request if 1, wait if 0 */
#define	WT_RDY_ENAB	0x80	/* Enable W/Req pin */

/* Write Register #2 (Interrupt Vector) */

/* Write Register 3 */

#define	RxENABLE	0x1	/* Rx Enable */
#define	SYNC_L_INH	0x2	/* Sync Character Load Inhibit */
#define	ADD_SM		0x4	/* Address Search Mode (SDLC) */
#define	RxCRC_ENAB	0x8	/* Rx CRC Enable */
#define	ENT_HM		0x10	/* Enter Hunt Mode */
#define	AUTO_ENAB	0x20	/* Auto Enables */
#define	Rx5		0x0	/* Rx 5 Bits/Character */
#define	Rx7		0x40	/* Rx 7 Bits/Character */
#define	Rx6		0x80	/* Rx 6 Bits/Character */
#define	Rx8		0xc0	/* Rx 8 Bits/Character */
#define RxN_MASK	0xc0

/* Write Register 4 */

#define	PAR_ENAB	0x1	/* Parity Enable */
#define	PAR_EVEN	0x2	/* Parity Even/Odd* */

#define	SYNC_ENAB	0	/* Sync Modes Enable */
#define	SB1		0x4	/* 1 stop bit/char */
#define	SB15		0x8	/* 1.5 stop bits/char */
#define	SB2		0xc	/* 2 stop bits/char */
#define SB_MASK		0xc

#define	MONSYNC		0	/* 8 Bit Sync character */
#define	BISYNC		0x10	/* 16 bit sync character */
#define	SDLC		0x20	/* SDLC Mode (01111110 Sync Flag) */
#define	EXTSYNC		0x30	/* External Sync Mode */

#define	X1CLK		0x0	/* x1 clock mode */
#define	X16CLK		0x40	/* x16 clock mode */
#define	X32CLK		0x80	/* x32 clock mode */
#define	X64CLK		0xC0	/* x64 clock mode */
#define XCLK_MASK	0xC0

/* Write Register 5 */

#define	TxCRC_ENAB	0x1	/* Tx CRC Enable */
#define	RTS		0x2	/* RTS */
#define	SDLC_CRC	0x4	/* SDLC/CRC-16 */
#define	TxENABLE	0x8	/* Tx Enable */
#define	SND_BRK		0x10	/* Send Break */
#define	Tx5		0x0	/* Tx 5 bits (or less)/character */
#define	Tx7		0x20	/* Tx 7 bits/character */
#define	Tx6		0x40	/* Tx 6 bits/character */
#define	Tx8		0x60	/* Tx 8 bits/character */
#define TxN_MASK	0x60
#define	DTR		0x80	/* DTR */

/* Write Register 6 (Sync bits 0-7/SDLC Address Field) */

/* Write Register 7 (Sync bits 8-15/SDLC 01111110) */

/* Write Register 7' (Some enhanced feature control) */
#define	ENEXREAD	0x40	/* Enable read of some write registers */

/* Write Register 8 (transmit buffer) */

/* Write Register 9 (Master interrupt control) */
#define	VIS	1	/* Vector Includes Status */
#define	NV	2	/* No Vector */
#define	DLC	4	/* Disable Lower Chain */
#define	MIE	8	/* Master Interrupt Enable */
#define	STATHI	0x10	/* Status high */
#define	NORESET	0	/* No reset on write to R9 */
#define	CHRB	0x40	/* Reset channel B */
#define	CHRA	0x80	/* Reset channel A */
#define	FHWRES	0xc0	/* Force hardware reset */

/* Write Register 10 (misc control bits) */
#define	BIT6	1	/* 6 bit/8bit sync */
#define	LOOPMODE 2	/* SDLC Loop mode */
#define	ABUNDER	4	/* Abort/flag on SDLC xmit underrun */
#define	MARKIDLE 8	/* Mark/flag on idle */
#define	GAOP	0x10	/* Go active on poll */
#define	NRZ	0	/* NRZ mode */
#define	NRZI	0x20	/* NRZI mode */
#define	FM1	0x40	/* FM1 (transition = 1) */
#define	FM0	0x60	/* FM0 (transition = 0) */
#define	CRCPS	0x80	/* CRC Preset I/O */

/* Write Register 11 (Clock Mode control) */
#define	TRxCXT	0	/* TRxC = Xtal output */
#define	TRxCTC	1	/* TRxC = Transmit clock */
#define	TRxCBR	2	/* TRxC = BR Generator Output */
#define	TRxCDP	3	/* TRxC = DPLL output */
#define	TRxCOI	4	/* TRxC O/I */
#define	TCRTxCP	0	/* Transmit clock = RTxC pin */
#define	TCTRxCP	8	/* Transmit clock = TRxC pin */
#define	TCBR	0x10	/* Transmit clock = BR Generator output */
#define	TCDPLL	0x18	/* Transmit clock = DPLL output */
#define	RCRTxCP	0	/* Receive clock = RTxC pin */
#define	RCTRxCP	0x20	/* Receive clock = TRxC pin */
#define	RCBR	0x40	/* Receive clock = BR Generator output */
#define	RCDPLL	0x60	/* Receive clock = DPLL output */
#define	RTxCX	0x80	/* RTxC Xtal/No Xtal */

/* Write Register 12 (lower byte of baud rate generator time constant) */

/* Write Register 13 (upper byte of baud rate generator time constant) */

/* Write Register 14 (Misc control bits) */
#define	BRENAB	1	/* Baud rate generator enable */
#define	BRSRC	2	/* Baud rate generator source */
#define	DTRREQ	4	/* DTR/Request function */
#define	AUTOECHO 8	/* Auto Echo */
#define	LOOPBAK	0x10	/* Local loopback */
#define	SEARCH	0x20	/* Enter search mode */
#define	RMC	0x40	/* Reset missing clock */
#define	DISDPLL	0x60	/* Disable DPLL */
#define	SSBR	0x80	/* Set DPLL source = BR generator */
#define	SSRTxC	0xa0	/* Set DPLL source = RTxC */
#define	SFMM	0xc0	/* Set FM mode */
#define	SNRZI	0xe0	/* Set NRZI mode */

/* Write Register 15 (external/status interrupt control) */
#define	EN85C30	1	/* Enable some 85c30-enhanced registers */
#define	ZCIE	2	/* Zero count IE */
#define	ENSTFIFO 4	/* Enable status FIFO (SDLC) */
#define	DCDIE	8	/* DCD IE */
#define	SYNCIE	0x10	/* Sync/hunt IE */
#define	CTSIE	0x20	/* CTS IE */
#define	TxUIE	0x40	/* Tx Underrun/EOM IE */
#define	BRKIE	0x80	/* Break/Abort IE */


/* Read Register 0 */
#define	Rx_CH_AV	0x1	/* Rx Character Available */
#define	ZCOUNT		0x2	/* Zero count */
#define	Tx_BUF_EMP	0x4	/* Tx Buffer empty */
#define	DCD		0x8	/* DCD */
#define	SYNC_HUNT	0x10	/* Sync/hunt */
#define	CTS		0x20	/* CTS */
#define	TxEOM		0x40	/* Tx underrun */
#define	BRK_ABRT	0x80	/* Break/Abort */

/* Read Register 1 */
#define	ALL_SNT		0x1	/* All sent */
/* Residue Data for 8 Rx bits/char programmed */
#define	RES3		0x8	/* 0/3 */
#define	RES4		0x4	/* 0/4 */
#define	RES5		0xc	/* 0/5 */
#define	RES6		0x2	/* 0/6 */
#define	RES7		0xa	/* 0/7 */
#define	RES8		0x6	/* 0/8 */
#define	RES18		0xe	/* 1/8 */
#define	RES28		0x0	/* 2/8 */
/* Special Rx Condition Interrupts */
#define	PAR_ERR		0x10	/* Parity error */
#define	Rx_OVR		0x20	/* Rx Overrun Error */
#define	CRC_ERR		0x40	/* CRC/Framing Error */
#define	END_FR		0x80	/* End of Frame (SDLC) */

/* Read Register 2 (channel b only) - Interrupt vector */
#define	CHB_Tx_EMPTY	0x00
#define	CHB_EXT_STAT	0x02
#define	CHB_Rx_AVAIL	0x04
#define	CHB_SPECIAL	0x06
#define	CHA_Tx_EMPTY	0x08
#define	CHA_EXT_STAT	0x0a
#define	CHA_Rx_AVAIL	0x0c
#define	CHA_SPECIAL	0x0e
#define	STATUS_MASK	0x06

/* Read Register 3 (interrupt pending register) ch a only */
#define	CHBEXT	0x1		/* Channel B Ext/Stat IP */
#define	CHBTxIP	0x2		/* Channel B Tx IP */
#define	CHBRxIP	0x4		/* Channel B Rx IP */
#define	CHAEXT	0x8		/* Channel A Ext/Stat IP */
#define	CHATxIP	0x10		/* Channel A Tx IP */
#define	CHARxIP	0x20		/* Channel A Rx IP */

/* Read Register 8 (receive data register) */

/* Read Register 10  (misc status bits) */
#define	ONLOOP	2		/* On loop */
#define	LOOPSEND 0x10		/* Loop sending */
#define	CLK2MIS	0x40		/* Two clocks missing */
#define	CLK1MIS	0x80		/* One clock missing */

/* Read Register 12 (lower byte of baud rate generator constant) */

/* Read Register 13 (upper byte of baud rate generator constant) */

/* Read Register 15 (value of WR 15) */

/* Misc macros */
#define ZS_CLEARERR(port)    (write_zsreg(port, 0, ERR_RES))
#define ZS_CLEARFIFO(port)   do { volatile unsigned char garbage; \
				     garbage = read_zsdata(port); \
				     garbage = read_zsdata(port); \
				     garbage = read_zsdata(port); \
				} while(0)

#define ZS_IS_CONS(UP)			((UP)->flags & PMACZILOG_FLAG_IS_CONS)
#define ZS_IS_KGDB(UP)			((UP)->flags & PMACZILOG_FLAG_IS_KGDB)
#define ZS_IS_CHANNEL_A(UP)		((UP)->flags & PMACZILOG_FLAG_IS_CHANNEL_A)
#define ZS_REGS_HELD(UP)		((UP)->flags & PMACZILOG_FLAG_REGS_HELD)
#define ZS_TX_STOPPED(UP)		((UP)->flags & PMACZILOG_FLAG_TX_STOPPED)
#define ZS_TX_ACTIVE(UP)		((UP)->flags & PMACZILOG_FLAG_TX_ACTIVE)
#define ZS_WANTS_MODEM_STATUS(UP)	((UP)->flags & PMACZILOG_FLAG_MODEM_STATUS)
#define ZS_IS_IRDA(UP)			((UP)->flags & PMACZILOG_FLAG_IS_IRDA)
#define ZS_IS_INTMODEM(UP)		((UP)->flags & PMACZILOG_FLAG_IS_INTMODEM)
#define ZS_HAS_DMA(UP)			((UP)->flags & PMACZILOG_FLAG_HAS_DMA)
#define ZS_IS_OPEN(UP)			((UP)->flags & PMACZILOG_FLAG_IS_OPEN)
#define ZS_IS_EXTCLK(UP)		((UP)->flags & PMACZILOG_FLAG_IS_EXTCLK)

#endif /* __PMAC_ZILOG_H__ */
