/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005 Silicon Graphics, Inc.
 */
#ifndef IA64_SN_IOC3_H
#define IA64_SN_IOC3_H

/* serial port register map */
struct ioc3_serialregs {
	uint32_t sscr;
	uint32_t stpir;
	uint32_t stcir;
	uint32_t srpir;
	uint32_t srcir;
	uint32_t srtr;
	uint32_t shadow;
};

/* SUPERIO uart register map */
struct ioc3_uartregs {
	char iu_lcr;
	union {
		char iir;	/* read only */
		char fcr;	/* write only */
	} u3;
	union {
		char ier;	/* DLAB == 0 */
		char dlm;	/* DLAB == 1 */
	} u2;
	union {
		char rbr;	/* read only, DLAB == 0 */
		char thr;	/* write only, DLAB == 0 */
		char dll;	/* DLAB == 1 */
	} u1;
	char iu_scr;
	char iu_msr;
	char iu_lsr;
	char iu_mcr;
};

#define iu_rbr u1.rbr
#define iu_thr u1.thr
#define iu_dll u1.dll
#define iu_ier u2.ier
#define iu_dlm u2.dlm
#define iu_iir u3.iir
#define iu_fcr u3.fcr

struct ioc3_sioregs {
	char fill[0x170];
	struct ioc3_uartregs uartb;
	struct ioc3_uartregs uarta;
};

/* PCI IO/mem space register map */
struct ioc3 {
	uint32_t pci_id;
	uint32_t pci_scr;
	uint32_t pci_rev;
	uint32_t pci_lat;
	uint32_t pci_addr;
	uint32_t pci_err_addr_l;
	uint32_t pci_err_addr_h;

	uint32_t sio_ir;
	/* these registers are read-only for general kernel code. To
	 * modify them use the functions in ioc3.c
	 */
	uint32_t sio_ies;
	uint32_t sio_iec;
	uint32_t sio_cr;
	uint32_t int_out;
	uint32_t mcr;
	uint32_t gpcr_s;
	uint32_t gpcr_c;
	uint32_t gpdr;
	uint32_t gppr[9];
	char fill[0x4c];

	/* serial port registers */
	uint32_t sbbr_h;
	uint32_t sbbr_l;

	struct ioc3_serialregs port_a;
	struct ioc3_serialregs port_b;
	char fill1[0x1ff10];
	/* superio registers */
	struct ioc3_sioregs sregs;
};

/* These don't exist on the ioc3 serial card... */
#define eier	fill1[8]
#define eisr	fill1[4]

#define PCI_LAT			0xc	/* Latency Timer */
#define PCI_SCR_DROP_MODE_EN	0x00008000 /* drop pios on parity err */
#define UARTA_BASE		0x178
#define UARTB_BASE		0x170


/* bitmasks for serial RX status byte */
#define RXSB_OVERRUN		0x01	/* char(s) lost */
#define RXSB_PAR_ERR		0x02	/* parity error */
#define RXSB_FRAME_ERR		0x04	/* framing error */
#define RXSB_BREAK		0x08	/* break character */
#define RXSB_CTS		0x10	/* state of CTS */
#define RXSB_DCD		0x20	/* state of DCD */
#define RXSB_MODEM_VALID	0x40	/* DCD, CTS and OVERRUN are valid */
#define RXSB_DATA_VALID		0x80	/* FRAME_ERR PAR_ERR & BREAK valid */

/* bitmasks for serial TX control byte */
#define TXCB_INT_WHEN_DONE	0x20	/* interrupt after this byte is sent */
#define TXCB_INVALID		0x00	/* byte is invalid */
#define TXCB_VALID		0x40	/* byte is valid */
#define TXCB_MCR		0x80	/* data<7:0> to modem cntrl register */
#define TXCB_DELAY		0xc0	/* delay data<7:0> mSec */

/* bitmasks for SBBR_L */
#define SBBR_L_SIZE		0x00000001	/* 0 1KB rings, 1 4KB rings */

/* bitmasks for SSCR_<A:B> */
#define SSCR_RX_THRESHOLD	0x000001ff	/* hiwater mark */
#define SSCR_TX_TIMER_BUSY	0x00010000	/* TX timer in progress */
#define SSCR_HFC_EN		0x00020000	/* h/w flow cntrl enabled */
#define SSCR_RX_RING_DCD	0x00040000	/* postRX record on delta-DCD */
#define SSCR_RX_RING_CTS	0x00080000	/* postRX record on delta-CTS */
#define SSCR_HIGH_SPD		0x00100000	/* 4X speed */
#define SSCR_DIAG		0x00200000	/* bypass clock divider */
#define SSCR_RX_DRAIN		0x08000000	/* drain RX buffer to memory */
#define SSCR_DMA_EN		0x10000000	/* enable ring buffer DMA */
#define SSCR_DMA_PAUSE		0x20000000	/* pause DMA */
#define SSCR_PAUSE_STATE	0x40000000	/* set when PAUSE takes effect*/
#define SSCR_RESET		0x80000000	/* reset DMA channels */

/* all producer/consumer pointers are the same bitfield */
#define PROD_CONS_PTR_4K	0x00000ff8	/* for 4K buffers */
#define PROD_CONS_PTR_1K	0x000003f8	/* for 1K buffers */
#define PROD_CONS_PTR_OFF	3

/* bitmasks for SRCIR_<A:B> */
#define SRCIR_ARM		0x80000000	/* arm RX timer */

/* bitmasks for SHADOW_<A:B> */
#define SHADOW_DR		0x00000001	/* data ready */
#define SHADOW_OE		0x00000002	/* overrun error */
#define SHADOW_PE		0x00000004	/* parity error */
#define SHADOW_FE		0x00000008	/* framing error */
#define SHADOW_BI		0x00000010	/* break interrupt */
#define SHADOW_THRE		0x00000020	/* transmit holding reg empty */
#define SHADOW_TEMT		0x00000040	/* transmit shift reg empty */
#define SHADOW_RFCE		0x00000080	/* char in RX fifo has error */
#define SHADOW_DCTS		0x00010000	/* delta clear to send */
#define SHADOW_DDCD		0x00080000	/* delta data carrier detect */
#define SHADOW_CTS		0x00100000	/* clear to send */
#define SHADOW_DCD		0x00800000	/* data carrier detect */
#define SHADOW_DTR		0x01000000	/* data terminal ready */
#define SHADOW_RTS		0x02000000	/* request to send */
#define SHADOW_OUT1		0x04000000	/* 16550 OUT1 bit */
#define SHADOW_OUT2		0x08000000	/* 16550 OUT2 bit */
#define SHADOW_LOOP		0x10000000	/* loopback enabled */

/* bitmasks for SRTR_<A:B> */
#define SRTR_CNT		0x00000fff	/* reload value for RX timer */
#define SRTR_CNT_VAL		0x0fff0000	/* current value of RX timer */
#define SRTR_CNT_VAL_SHIFT	16
#define SRTR_HZ			16000		/* SRTR clock frequency */

/* bitmasks for SIO_IR, SIO_IEC and SIO_IES  */
#define SIO_IR_SA_TX_MT		0x00000001	/* Serial port A TX empty */
#define SIO_IR_SA_RX_FULL	0x00000002	/* port A RX buf full */
#define SIO_IR_SA_RX_HIGH	0x00000004	/* port A RX hiwat */
#define SIO_IR_SA_RX_TIMER	0x00000008	/* port A RX timeout */
#define SIO_IR_SA_DELTA_DCD	0x00000010	/* port A delta DCD */
#define SIO_IR_SA_DELTA_CTS	0x00000020	/* port A delta CTS */
#define SIO_IR_SA_INT		0x00000040	/* port A pass-thru intr */
#define SIO_IR_SA_TX_EXPLICIT	0x00000080	/* port A explicit TX thru */
#define SIO_IR_SA_MEMERR	0x00000100	/* port A PCI error */
#define SIO_IR_SB_TX_MT		0x00000200
#define SIO_IR_SB_RX_FULL	0x00000400
#define SIO_IR_SB_RX_HIGH	0x00000800
#define SIO_IR_SB_RX_TIMER	0x00001000
#define SIO_IR_SB_DELTA_DCD	0x00002000
#define SIO_IR_SB_DELTA_CTS	0x00004000
#define SIO_IR_SB_INT		0x00008000
#define SIO_IR_SB_TX_EXPLICIT	0x00010000
#define SIO_IR_SB_MEMERR	0x00020000
#define SIO_IR_PP_INT		0x00040000	/* P port pass-thru intr */
#define SIO_IR_PP_INTA		0x00080000	/* PP context A thru */
#define SIO_IR_PP_INTB		0x00100000	/* PP context B thru */
#define SIO_IR_PP_MEMERR	0x00200000	/* PP PCI error */
#define SIO_IR_KBD_INT		0x00400000	/* kbd/mouse intr */
#define SIO_IR_RT_INT		0x08000000	/* RT output pulse */
#define SIO_IR_GEN_INT1		0x10000000	/* RT input pulse */
#define SIO_IR_GEN_INT_SHIFT	28

/* per device interrupt masks */
#define SIO_IR_SA		(SIO_IR_SA_TX_MT | \
				 SIO_IR_SA_RX_FULL | \
				 SIO_IR_SA_RX_HIGH | \
				 SIO_IR_SA_RX_TIMER | \
				 SIO_IR_SA_DELTA_DCD | \
				 SIO_IR_SA_DELTA_CTS | \
				 SIO_IR_SA_INT | \
				 SIO_IR_SA_TX_EXPLICIT | \
				 SIO_IR_SA_MEMERR)

#define SIO_IR_SB		(SIO_IR_SB_TX_MT | \
				 SIO_IR_SB_RX_FULL | \
				 SIO_IR_SB_RX_HIGH | \
				 SIO_IR_SB_RX_TIMER | \
				 SIO_IR_SB_DELTA_DCD | \
				 SIO_IR_SB_DELTA_CTS | \
				 SIO_IR_SB_INT | \
				 SIO_IR_SB_TX_EXPLICIT | \
				 SIO_IR_SB_MEMERR)

#define SIO_IR_PP		(SIO_IR_PP_INT | SIO_IR_PP_INTA | \
				 SIO_IR_PP_INTB | SIO_IR_PP_MEMERR)
#define SIO_IR_RT		(SIO_IR_RT_INT | SIO_IR_GEN_INT1)

/* bitmasks for SIO_CR */
#define SIO_CR_CMD_PULSE_SHIFT 15
#define SIO_CR_SER_A_BASE_SHIFT 1
#define SIO_CR_SER_B_BASE_SHIFT 8
#define SIO_CR_ARB_DIAG		0x00380000	/* cur !enet PCI requet (ro) */
#define SIO_CR_ARB_DIAG_TXA	0x00000000
#define SIO_CR_ARB_DIAG_RXA	0x00080000
#define SIO_CR_ARB_DIAG_TXB	0x00100000
#define SIO_CR_ARB_DIAG_RXB	0x00180000
#define SIO_CR_ARB_DIAG_PP	0x00200000
#define SIO_CR_ARB_DIAG_IDLE	0x00400000	/* 0 -> active request (ro) */

/* defs for some of the generic I/O pins */
#define GPCR_PHY_RESET		0x20	/* pin is output to PHY reset */
#define GPCR_UARTB_MODESEL	0x40	/* pin is output to port B mode sel */
#define GPCR_UARTA_MODESEL	0x80	/* pin is output to port A mode sel */

#define GPPR_PHY_RESET_PIN	5	/* GIO pin controlling phy reset */
#define GPPR_UARTB_MODESEL_PIN	6	/* GIO pin cntrling uartb modeselect */
#define GPPR_UARTA_MODESEL_PIN	7	/* GIO pin cntrling uarta modeselect */

#endif /* IA64_SN_IOC3_H */
