/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1999, 2000 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef MIPS_SN_IOC3_H
#define MIPS_SN_IOC3_H

#include <linux/types.h>

/* serial port register map */
struct ioc3_serialregs {
	u32	sscr;
	u32	stpir;
	u32	stcir;
	u32	srpir;
	u32	srcir;
	u32	srtr;
	u32	shadow;
};

/* SUPERIO uart register map */
struct ioc3_uartregs {
	union {
		u8	iu_rbr;	/* read only, DLAB == 0 */
		u8	iu_thr;	/* write only, DLAB == 0 */
		u8	iu_dll;	/* DLAB == 1 */
	};
	union {
		u8	iu_ier;	/* DLAB == 0 */
		u8	iu_dlm;	/* DLAB == 1 */
	};
	union {
		u8	iu_iir;	/* read only */
		u8	iu_fcr;	/* write only */
	};
	u8	iu_lcr;
	u8	iu_mcr;
	u8	iu_lsr;
	u8	iu_msr;
	u8	iu_scr;
};

struct ioc3_sioregs {
	u8	fill[0x141];	/* starts at 0x141 */

	u8	uartc;
	u8	kbdcg;

	u8	fill0[0x150 - 0x142 - 1];

	u8	pp_data;
	u8	pp_dsr;
	u8	pp_dcr;

	u8	fill1[0x158 - 0x152 - 1];

	u8	pp_fifa;
	u8	pp_cfgb;
	u8	pp_ecr;

	u8	fill2[0x168 - 0x15a - 1];

	u8	rtcad;
	u8	rtcdat;

	u8	fill3[0x170 - 0x169 - 1];

	struct ioc3_uartregs	uartb;	/* 0x20170  */
	struct ioc3_uartregs	uarta;	/* 0x20178  */
};

struct ioc3_ethregs {
	u32	emcr;		/* 0x000f0  */
	u32	eisr;		/* 0x000f4  */
	u32	eier;		/* 0x000f8  */
	u32	ercsr;		/* 0x000fc  */
	u32	erbr_h;		/* 0x00100  */
	u32	erbr_l;		/* 0x00104  */
	u32	erbar;		/* 0x00108  */
	u32	ercir;		/* 0x0010c  */
	u32	erpir;		/* 0x00110  */
	u32	ertr;		/* 0x00114  */
	u32	etcsr;		/* 0x00118  */
	u32	ersr;		/* 0x0011c  */
	u32	etcdc;		/* 0x00120  */
	u32	ebir;		/* 0x00124  */
	u32	etbr_h;		/* 0x00128  */
	u32	etbr_l;		/* 0x0012c  */
	u32	etcir;		/* 0x00130  */
	u32	etpir;		/* 0x00134  */
	u32	emar_h;		/* 0x00138  */
	u32	emar_l;		/* 0x0013c  */
	u32	ehar_h;		/* 0x00140  */
	u32	ehar_l;		/* 0x00144  */
	u32	micr;		/* 0x00148  */
	u32	midr_r;		/* 0x0014c  */
	u32	midr_w;		/* 0x00150  */
};

struct ioc3_serioregs {
	u32	km_csr;		/* 0x0009c  */
	u32	k_rd;		/* 0x000a0  */
	u32	m_rd;		/* 0x000a4  */
	u32	k_wd;		/* 0x000a8  */
	u32	m_wd;		/* 0x000ac  */
};

/* Register layout of IOC3 in configuration space.  */
struct ioc3 {
	/* PCI Config Space registers  */
	u32	pci_id;		/* 0x00000  */
	u32	pci_scr;	/* 0x00004  */
	u32	pci_rev;	/* 0x00008  */
	u32	pci_lat;	/* 0x0000c  */
	u32	pci_addr;	/* 0x00010  */
	u32	pci_err_addr_l;	/* 0x00014  */
	u32	pci_err_addr_h;	/* 0x00018  */

	u32	sio_ir;		/* 0x0001c  */
	u32	sio_ies;	/* 0x00020  */
	u32	sio_iec;	/* 0x00024  */
	u32	sio_cr;		/* 0x00028  */
	u32	int_out;	/* 0x0002c  */
	u32	mcr;		/* 0x00030  */

	/* General Purpose I/O registers  */
	u32	gpcr_s;		/* 0x00034  */
	u32	gpcr_c;		/* 0x00038  */
	u32	gpdr;		/* 0x0003c  */
	u32	gppr[16];	/* 0x00040  */

	/* Parallel Port Registers  */
	u32	ppbr_h_a;	/* 0x00080  */
	u32	ppbr_l_a;	/* 0x00084  */
	u32	ppcr_a;		/* 0x00088  */
	u32	ppcr;		/* 0x0008c  */
	u32	ppbr_h_b;	/* 0x00090  */
	u32	ppbr_l_b;	/* 0x00094  */
	u32	ppcr_b;		/* 0x00098  */

	/* Keyboard and Mouse Registers	 */
	struct ioc3_serioregs	serio;

	/* Serial Port Registers  */
	u32	sbbr_h;		/* 0x000b0  */
	u32	sbbr_l;		/* 0x000b4  */
	struct ioc3_serialregs	port_a;
	struct ioc3_serialregs	port_b;

	/* Ethernet Registers */
	struct ioc3_ethregs	eth;
	u32	pad1[(0x20000 - 0x00154) / 4];

	/* SuperIO Registers  XXX */
	struct ioc3_sioregs	sregs;	/* 0x20000 */
	u32	pad2[(0x40000 - 0x20180) / 4];

	/* SSRAM Diagnostic Access */
	u32	ssram[(0x80000 - 0x40000) / 4];

	/* Bytebus device offsets
	   0x80000 -   Access to the generic devices selected with   DEV0
	   0x9FFFF     bytebus DEV_SEL_0
	   0xA0000 -   Access to the generic devices selected with   DEV1
	   0xBFFFF     bytebus DEV_SEL_1
	   0xC0000 -   Access to the generic devices selected with   DEV2
	   0xDFFFF     bytebus DEV_SEL_2
	   0xE0000 -   Access to the generic devices selected with   DEV3
	   0xFFFFF     bytebus DEV_SEL_3  */
};


#define PCI_LAT			0xc		/* Latency Timer */
#define PCI_SCR_DROP_MODE_EN	0x00008000	/* drop pios on parity err */
#define UARTA_BASE		0x178
#define UARTB_BASE		0x170

/*
 * Bytebus device space
 */
#define IOC3_BYTEBUS_DEV0	0x80000L
#define IOC3_BYTEBUS_DEV1	0xa0000L
#define IOC3_BYTEBUS_DEV2	0xc0000L
#define IOC3_BYTEBUS_DEV3	0xe0000L

/*
 * Ethernet RX Buffer
 */
struct ioc3_erxbuf {
	u32	w0;			/* first word (valid,bcnt,cksum) */
	u32	err;			/* second word various errors */
	/* next comes n bytes of padding */
	/* then the received ethernet frame itself */
};

#define ERXBUF_IPCKSUM_MASK	0x0000ffff
#define ERXBUF_BYTECNT_MASK	0x07ff0000
#define ERXBUF_BYTECNT_SHIFT	16
#define ERXBUF_V		0x80000000

#define ERXBUF_CRCERR		0x00000001	/* aka RSV15 */
#define ERXBUF_FRAMERR		0x00000002	/* aka RSV14 */
#define ERXBUF_CODERR		0x00000004	/* aka RSV13 */
#define ERXBUF_INVPREAMB	0x00000008	/* aka RSV18 */
#define ERXBUF_LOLEN		0x00007000	/* aka RSV2_0 */
#define ERXBUF_HILEN		0x03ff0000	/* aka RSV12_3 */
#define ERXBUF_MULTICAST	0x04000000	/* aka RSV16 */
#define ERXBUF_BROADCAST	0x08000000	/* aka RSV17 */
#define ERXBUF_LONGEVENT	0x10000000	/* aka RSV19 */
#define ERXBUF_BADPKT		0x20000000	/* aka RSV20 */
#define ERXBUF_GOODPKT		0x40000000	/* aka RSV21 */
#define ERXBUF_CARRIER		0x80000000	/* aka RSV22 */

/*
 * Ethernet TX Descriptor
 */
#define ETXD_DATALEN	104
struct ioc3_etxd {
	u32	cmd;				/* command field */
	u32	bufcnt;				/* buffer counts field */
	u64	p1;				/* buffer pointer 1 */
	u64	p2;				/* buffer pointer 2 */
	u8	data[ETXD_DATALEN];		/* opt. tx data */
};

#define ETXD_BYTECNT_MASK	0x000007ff	/* total byte count */
#define ETXD_INTWHENDONE	0x00001000	/* intr when done */
#define ETXD_D0V		0x00010000	/* data 0 valid */
#define ETXD_B1V		0x00020000	/* buf 1 valid */
#define ETXD_B2V		0x00040000	/* buf 2 valid */
#define ETXD_DOCHECKSUM		0x00080000	/* insert ip cksum */
#define ETXD_CHKOFF_MASK	0x07f00000	/* cksum byte offset */
#define ETXD_CHKOFF_SHIFT	20

#define ETXD_D0CNT_MASK		0x0000007f
#define ETXD_B1CNT_MASK		0x0007ff00
#define ETXD_B1CNT_SHIFT	8
#define ETXD_B2CNT_MASK		0x7ff00000
#define ETXD_B2CNT_SHIFT	20

/* ------------------------------------------------------------------------- */

/* Superio Registers (PIO Access) */
#define IOC3_SIO_BASE		0x20000
#define IOC3_SIO_UARTC		(IOC3_SIO_BASE+0x141)	/* UART Config */
#define IOC3_SIO_KBDCG		(IOC3_SIO_BASE+0x142)	/* KBD Config */
#define IOC3_SIO_PP_BASE	(IOC3_SIO_BASE+PP_BASE)	/* Parallel Port */
#define IOC3_SIO_RTC_BASE	(IOC3_SIO_BASE+0x168)	/* Real Time Clock */
#define IOC3_SIO_UB_BASE	(IOC3_SIO_BASE+UARTB_BASE)	/* UART B */
#define IOC3_SIO_UA_BASE	(IOC3_SIO_BASE+UARTA_BASE)	/* UART A */

/* SSRAM Diagnostic Access */
#define IOC3_SSRAM	IOC3_RAM_OFF	/* base of SSRAM diagnostic access */
#define IOC3_SSRAM_LEN	0x40000	/* 256kb (addrspc sz, may not be populated) */
#define IOC3_SSRAM_DM	0x0000ffff	/* data mask */
#define IOC3_SSRAM_PM	0x00010000	/* parity mask */

/* bitmasks for PCI_SCR */
#define PCI_SCR_PAR_RESP_EN	0x00000040	/* enb PCI parity checking */
#define PCI_SCR_SERR_EN		0x00000100	/* enable the SERR# driver */
#define PCI_SCR_DROP_MODE_EN	0x00008000	/* drop pios on parity err */
#define PCI_SCR_RX_SERR		(0x1 << 16)
#define PCI_SCR_DROP_MODE	(0x1 << 17)
#define PCI_SCR_SIG_PAR_ERR	(0x1 << 24)
#define PCI_SCR_SIG_TAR_ABRT	(0x1 << 27)
#define PCI_SCR_RX_TAR_ABRT	(0x1 << 28)
#define PCI_SCR_SIG_MST_ABRT	(0x1 << 29)
#define PCI_SCR_SIG_SERR	(0x1 << 30)
#define PCI_SCR_PAR_ERR		(0x1 << 31)

/* bitmasks for IOC3_KM_CSR */
#define KM_CSR_K_WRT_PEND 0x00000001	/* kbd port xmitting or resetting */
#define KM_CSR_M_WRT_PEND 0x00000002	/* mouse port xmitting or resetting */
#define KM_CSR_K_LCB	  0x00000004	/* Line Cntrl Bit for last KBD write */
#define KM_CSR_M_LCB	  0x00000008	/* same for mouse */
#define KM_CSR_K_DATA	  0x00000010	/* state of kbd data line */
#define KM_CSR_K_CLK	  0x00000020	/* state of kbd clock line */
#define KM_CSR_K_PULL_DATA 0x00000040	/* pull kbd data line low */
#define KM_CSR_K_PULL_CLK 0x00000080	/* pull kbd clock line low */
#define KM_CSR_M_DATA	  0x00000100	/* state of ms data line */
#define KM_CSR_M_CLK	  0x00000200	/* state of ms clock line */
#define KM_CSR_M_PULL_DATA 0x00000400	/* pull ms data line low */
#define KM_CSR_M_PULL_CLK 0x00000800	/* pull ms clock line low */
#define KM_CSR_EMM_MODE	  0x00001000	/* emulation mode */
#define KM_CSR_SIM_MODE	  0x00002000	/* clock X8 */
#define KM_CSR_K_SM_IDLE  0x00004000	/* Keyboard is idle */
#define KM_CSR_M_SM_IDLE  0x00008000	/* Mouse is idle */
#define KM_CSR_K_TO	  0x00010000	/* Keyboard trying to send/receive */
#define KM_CSR_M_TO	  0x00020000	/* Mouse trying to send/receive */
#define KM_CSR_K_TO_EN	  0x00040000	/* KM_CSR_K_TO + KM_CSR_K_TO_EN = cause
					   SIO_IR to assert */
#define KM_CSR_M_TO_EN	  0x00080000	/* KM_CSR_M_TO + KM_CSR_M_TO_EN = cause
					   SIO_IR to assert */
#define KM_CSR_K_CLAMP_1  0x00100000	/* Pull K_CLK low aft recv 1 char */
#define KM_CSR_M_CLAMP_1  0x00200000	/* Pull M_CLK low aft recv 1 char */
#define KM_CSR_K_CLAMP_3  0x00400000	/* Pull K_CLK low aft recv 3 chars */
#define KM_CSR_M_CLAMP_3  0x00800000	/* Pull M_CLK low aft recv 3 chars */

/* bitmasks for IOC3_K_RD and IOC3_M_RD */
#define KM_RD_DATA_2	0x000000ff	/* 3rd char recvd since last read */
#define KM_RD_DATA_2_SHIFT 0
#define KM_RD_DATA_1	0x0000ff00	/* 2nd char recvd since last read */
#define KM_RD_DATA_1_SHIFT 8
#define KM_RD_DATA_0	0x00ff0000	/* 1st char recvd since last read */
#define KM_RD_DATA_0_SHIFT 16
#define KM_RD_FRAME_ERR_2 0x01000000	/*  framing or parity error in byte 2 */
#define KM_RD_FRAME_ERR_1 0x02000000	/* same for byte 1 */
#define KM_RD_FRAME_ERR_0 0x04000000	/* same for byte 0 */

#define KM_RD_KBD_MSE	0x08000000	/* 0 if from kbd, 1 if from mouse */
#define KM_RD_OFLO	0x10000000	/* 4th char recvd before this read */
#define KM_RD_VALID_2	0x20000000	/* DATA_2 valid */
#define KM_RD_VALID_1	0x40000000	/* DATA_1 valid */
#define KM_RD_VALID_0	0x80000000	/* DATA_0 valid */
#define KM_RD_VALID_ALL (KM_RD_VALID_0|KM_RD_VALID_1|KM_RD_VALID_2)

/* bitmasks for IOC3_K_WD & IOC3_M_WD */
#define KM_WD_WRT_DATA	0x000000ff	/* write to keyboard/mouse port */
#define KM_WD_WRT_DATA_SHIFT 0

/* bitmasks for serial RX status byte */
#define RXSB_OVERRUN	0x01	/* char(s) lost */
#define RXSB_PAR_ERR	0x02	/* parity error */
#define RXSB_FRAME_ERR	0x04	/* framing error */
#define RXSB_BREAK	0x08	/* break character */
#define RXSB_CTS	0x10	/* state of CTS */
#define RXSB_DCD	0x20	/* state of DCD */
#define RXSB_MODEM_VALID 0x40	/* DCD, CTS and OVERRUN are valid */
#define RXSB_DATA_VALID 0x80	/* data byte, FRAME_ERR PAR_ERR & BREAK valid */

/* bitmasks for serial TX control byte */
#define TXCB_INT_WHEN_DONE 0x20 /* interrupt after this byte is sent */
#define TXCB_INVALID	0x00	/* byte is invalid */
#define TXCB_VALID	0x40	/* byte is valid */
#define TXCB_MCR	0x80	/* data<7:0> to modem control register */
#define TXCB_DELAY	0xc0	/* delay data<7:0> mSec */

/* bitmasks for IOC3_SBBR_L */
#define SBBR_L_SIZE	0x00000001	/* 0 == 1KB rings, 1 == 4KB rings */
#define SBBR_L_BASE	0xfffff000	/* lower serial ring base addr */

/* bitmasks for IOC3_SSCR_<A:B> */
#define SSCR_RX_THRESHOLD 0x000001ff	/* hiwater mark */
#define SSCR_TX_TIMER_BUSY 0x00010000	/* TX timer in progress */
#define SSCR_HFC_EN	0x00020000	/* hardware flow control enabled */
#define SSCR_RX_RING_DCD 0x00040000	/* post RX record on delta-DCD */
#define SSCR_RX_RING_CTS 0x00080000	/* post RX record on delta-CTS */
#define SSCR_HIGH_SPD	0x00100000	/* 4X speed */
#define SSCR_DIAG	0x00200000	/* bypass clock divider for sim */
#define SSCR_RX_DRAIN	0x08000000	/* drain RX buffer to memory */
#define SSCR_DMA_EN	0x10000000	/* enable ring buffer DMA */
#define SSCR_DMA_PAUSE	0x20000000	/* pause DMA */
#define SSCR_PAUSE_STATE 0x40000000	/* sets when PAUSE takes effect */
#define SSCR_RESET	0x80000000	/* reset DMA channels */

/* all producer/consumer pointers are the same bitfield */
#define PROD_CONS_PTR_4K 0x00000ff8	/* for 4K buffers */
#define PROD_CONS_PTR_1K 0x000003f8	/* for 1K buffers */
#define PROD_CONS_PTR_OFF 3

/* bitmasks for IOC3_SRCIR_<A:B> */
#define SRCIR_ARM	0x80000000	/* arm RX timer */

/* bitmasks for IOC3_SRPIR_<A:B> */
#define SRPIR_BYTE_CNT	0x07000000	/* bytes in packer */
#define SRPIR_BYTE_CNT_SHIFT 24

/* bitmasks for IOC3_STCIR_<A:B> */
#define STCIR_BYTE_CNT	0x0f000000	/* bytes in unpacker */
#define STCIR_BYTE_CNT_SHIFT 24

/* bitmasks for IOC3_SHADOW_<A:B> */
#define SHADOW_DR	0x00000001	/* data ready */
#define SHADOW_OE	0x00000002	/* overrun error */
#define SHADOW_PE	0x00000004	/* parity error */
#define SHADOW_FE	0x00000008	/* framing error */
#define SHADOW_BI	0x00000010	/* break interrupt */
#define SHADOW_THRE	0x00000020	/* transmit holding register empty */
#define SHADOW_TEMT	0x00000040	/* transmit shift register empty */
#define SHADOW_RFCE	0x00000080	/* char in RX fifo has an error */
#define SHADOW_DCTS	0x00010000	/* delta clear to send */
#define SHADOW_DDCD	0x00080000	/* delta data carrier detect */
#define SHADOW_CTS	0x00100000	/* clear to send */
#define SHADOW_DCD	0x00800000	/* data carrier detect */
#define SHADOW_DTR	0x01000000	/* data terminal ready */
#define SHADOW_RTS	0x02000000	/* request to send */
#define SHADOW_OUT1	0x04000000	/* 16550 OUT1 bit */
#define SHADOW_OUT2	0x08000000	/* 16550 OUT2 bit */
#define SHADOW_LOOP	0x10000000	/* loopback enabled */

/* bitmasks for IOC3_SRTR_<A:B> */
#define SRTR_CNT	0x00000fff	/* reload value for RX timer */
#define SRTR_CNT_VAL	0x0fff0000	/* current value of RX timer */
#define SRTR_CNT_VAL_SHIFT 16
#define SRTR_HZ		16000	/* SRTR clock frequency */

/* bitmasks for IOC3_SIO_IR, IOC3_SIO_IEC and IOC3_SIO_IES  */
#define SIO_IR_SA_TX_MT		0x00000001	/* Serial port A TX empty */
#define SIO_IR_SA_RX_FULL	0x00000002	/* port A RX buf full */
#define SIO_IR_SA_RX_HIGH	0x00000004	/* port A RX hiwat */
#define SIO_IR_SA_RX_TIMER	0x00000008	/* port A RX timeout */
#define SIO_IR_SA_DELTA_DCD	0x00000010	/* port A delta DCD */
#define SIO_IR_SA_DELTA_CTS	0x00000020	/* port A delta CTS */
#define SIO_IR_SA_INT		0x00000040	/* port A pass-thru intr */
#define SIO_IR_SA_TX_EXPLICIT	0x00000080	/* port A explicit TX thru */
#define SIO_IR_SA_MEMERR	0x00000100	/* port A PCI error */
#define SIO_IR_SB_TX_MT		0x00000200	/* */
#define SIO_IR_SB_RX_FULL	0x00000400	/* */
#define SIO_IR_SB_RX_HIGH	0x00000800	/* */
#define SIO_IR_SB_RX_TIMER	0x00001000	/* */
#define SIO_IR_SB_DELTA_DCD	0x00002000	/* */
#define SIO_IR_SB_DELTA_CTS	0x00004000	/* */
#define SIO_IR_SB_INT		0x00008000	/* */
#define SIO_IR_SB_TX_EXPLICIT	0x00010000	/* */
#define SIO_IR_SB_MEMERR	0x00020000	/* */
#define SIO_IR_PP_INT		0x00040000	/* P port pass-thru intr */
#define SIO_IR_PP_INTA		0x00080000	/* PP context A thru */
#define SIO_IR_PP_INTB		0x00100000	/* PP context B thru */
#define SIO_IR_PP_MEMERR	0x00200000	/* PP PCI error */
#define SIO_IR_KBD_INT		0x00400000	/* kbd/mouse intr */
#define SIO_IR_RT_INT		0x08000000	/* RT output pulse */
#define SIO_IR_GEN_INT1		0x10000000	/* RT input pulse */
#define SIO_IR_GEN_INT_SHIFT	28

/* per device interrupt masks */
#define SIO_IR_SA		(SIO_IR_SA_TX_MT | SIO_IR_SA_RX_FULL | \
				 SIO_IR_SA_RX_HIGH | SIO_IR_SA_RX_TIMER | \
				 SIO_IR_SA_DELTA_DCD | SIO_IR_SA_DELTA_CTS | \
				 SIO_IR_SA_INT | SIO_IR_SA_TX_EXPLICIT | \
				 SIO_IR_SA_MEMERR)
#define SIO_IR_SB		(SIO_IR_SB_TX_MT | SIO_IR_SB_RX_FULL | \
				 SIO_IR_SB_RX_HIGH | SIO_IR_SB_RX_TIMER | \
				 SIO_IR_SB_DELTA_DCD | SIO_IR_SB_DELTA_CTS | \
				 SIO_IR_SB_INT | SIO_IR_SB_TX_EXPLICIT | \
				 SIO_IR_SB_MEMERR)
#define SIO_IR_PP		(SIO_IR_PP_INT | SIO_IR_PP_INTA | \
				 SIO_IR_PP_INTB | SIO_IR_PP_MEMERR)
#define SIO_IR_RT		(SIO_IR_RT_INT | SIO_IR_GEN_INT1)

/* bitmasks for SIO_CR */
#define SIO_CR_SIO_RESET	0x00000001	/* reset the SIO */
#define SIO_CR_SER_A_BASE	0x000000fe	/* DMA poll addr port A */
#define SIO_CR_SER_A_BASE_SHIFT 1
#define SIO_CR_SER_B_BASE	0x00007f00	/* DMA poll addr port B */
#define SIO_CR_SER_B_BASE_SHIFT 8
#define SIO_SR_CMD_PULSE	0x00078000	/* byte bus strobe length */
#define SIO_CR_CMD_PULSE_SHIFT	15
#define SIO_CR_ARB_DIAG		0x00380000	/* cur !enet PCI requet (ro) */
#define SIO_CR_ARB_DIAG_TXA	0x00000000
#define SIO_CR_ARB_DIAG_RXA	0x00080000
#define SIO_CR_ARB_DIAG_TXB	0x00100000
#define SIO_CR_ARB_DIAG_RXB	0x00180000
#define SIO_CR_ARB_DIAG_PP	0x00200000
#define SIO_CR_ARB_DIAG_IDLE	0x00400000	/* 0 -> active request (ro) */

/* bitmasks for INT_OUT */
#define INT_OUT_COUNT	0x0000ffff	/* pulse interval timer */
#define INT_OUT_MODE	0x00070000	/* mode mask */
#define INT_OUT_MODE_0	0x00000000	/* set output to 0 */
#define INT_OUT_MODE_1	0x00040000	/* set output to 1 */
#define INT_OUT_MODE_1PULSE 0x00050000	/* send 1 pulse */
#define INT_OUT_MODE_PULSES 0x00060000	/* send 1 pulse every interval */
#define INT_OUT_MODE_SQW 0x00070000	/* toggle output every interval */
#define INT_OUT_DIAG	0x40000000	/* diag mode */
#define INT_OUT_INT_OUT 0x80000000	/* current state of INT_OUT */

/* time constants for INT_OUT */
#define INT_OUT_NS_PER_TICK (30 * 260)	/* 30 ns PCI clock, divisor=260 */
#define INT_OUT_TICKS_PER_PULSE 3	/* outgoing pulse lasts 3 ticks */
#define INT_OUT_US_TO_COUNT(x)		/* convert uS to a count value */ \
	(((x) * 10 + INT_OUT_NS_PER_TICK / 200) *	\
	 100 / INT_OUT_NS_PER_TICK - 1)
#define INT_OUT_COUNT_TO_US(x)		/* convert count value to uS */ \
	(((x) + 1) * INT_OUT_NS_PER_TICK / 1000)
#define INT_OUT_MIN_TICKS 3	/* min period is width of pulse in "ticks" */
#define INT_OUT_MAX_TICKS INT_OUT_COUNT		/* largest possible count */

/* bitmasks for GPCR */
#define GPCR_DIR	0x000000ff	/* tristate pin input or output */
#define GPCR_DIR_PIN(x) (1<<(x))	/* access one of the DIR bits */
#define GPCR_EDGE	0x000f0000	/* extint edge or level sensitive */
#define GPCR_EDGE_PIN(x) (1<<((x)+15))	/* access one of the EDGE bits */

/* values for GPCR */
#define GPCR_INT_OUT_EN 0x00100000	/* enable INT_OUT to pin 0 */
#define GPCR_MLAN_EN	0x00200000	/* enable MCR to pin 8 */
#define GPCR_DIR_SERA_XCVR 0x00000080	/* Port A Transceiver select enable */
#define GPCR_DIR_SERB_XCVR 0x00000040	/* Port B Transceiver select enable */
#define GPCR_DIR_PHY_RST   0x00000020	/* ethernet PHY reset enable */

/* defs for some of the generic I/O pins */
#define GPCR_PHY_RESET		0x20	/* pin is output to PHY reset */
#define GPCR_UARTB_MODESEL	0x40	/* pin is output to port B mode sel */
#define GPCR_UARTA_MODESEL	0x80	/* pin is output to port A mode sel */

#define GPPR_PHY_RESET_PIN	5	/* GIO pin cntrlling phy reset */
#define GPPR_UARTB_MODESEL_PIN	6	/* GIO pin cntrlling uart b mode sel */
#define GPPR_UARTA_MODESEL_PIN	7	/* GIO pin cntrlling uart a mode sel */

/* ethernet */
#define EMCR_DUPLEX		0x00000001
#define EMCR_PROMISC		0x00000002
#define EMCR_PADEN		0x00000004
#define EMCR_RXOFF_MASK		0x000001f8
#define EMCR_RXOFF_SHIFT	3
#define EMCR_RAMPAR		0x00000200
#define EMCR_BADPAR		0x00000800
#define EMCR_BUFSIZ		0x00001000
#define EMCR_TXDMAEN		0x00002000
#define EMCR_TXEN		0x00004000
#define EMCR_RXDMAEN		0x00008000
#define EMCR_RXEN		0x00010000
#define EMCR_LOOPBACK		0x00020000
#define EMCR_ARB_DIAG		0x001c0000
#define EMCR_ARB_DIAG_IDLE	0x00200000
#define EMCR_RST		0x80000000

#define EISR_RXTIMERINT		0x00000001
#define EISR_RXTHRESHINT	0x00000002
#define EISR_RXOFLO		0x00000004
#define EISR_RXBUFOFLO		0x00000008
#define EISR_RXMEMERR		0x00000010
#define EISR_RXPARERR		0x00000020
#define EISR_TXEMPTY		0x00010000
#define EISR_TXRTRY		0x00020000
#define EISR_TXEXDEF		0x00040000
#define EISR_TXLCOL		0x00080000
#define EISR_TXGIANT		0x00100000
#define EISR_TXBUFUFLO		0x00200000
#define EISR_TXEXPLICIT		0x00400000
#define EISR_TXCOLLWRAP		0x00800000
#define EISR_TXDEFERWRAP	0x01000000
#define EISR_TXMEMERR		0x02000000
#define EISR_TXPARERR		0x04000000

#define ERCSR_THRESH_MASK	0x000001ff	/* enet RX threshold */
#define ERCSR_RX_TMR		0x40000000	/* simulation only */
#define ERCSR_DIAG_OFLO		0x80000000	/* simulation only */

#define ERBR_ALIGNMENT		4096
#define ERBR_L_RXRINGBASE_MASK	0xfffff000

#define ERBAR_BARRIER_BIT	0x0100
#define ERBAR_RXBARR_MASK	0xffff0000
#define ERBAR_RXBARR_SHIFT	16

#define ERCIR_RXCONSUME_MASK	0x00000fff

#define ERPIR_RXPRODUCE_MASK	0x00000fff
#define ERPIR_ARM		0x80000000

#define ERTR_CNT_MASK		0x000007ff

#define ETCSR_IPGT_MASK		0x0000007f
#define ETCSR_IPGR1_MASK	0x00007f00
#define ETCSR_IPGR1_SHIFT	8
#define ETCSR_IPGR2_MASK	0x007f0000
#define ETCSR_IPGR2_SHIFT	16
#define ETCSR_NOTXCLK		0x80000000

#define ETCDC_COLLCNT_MASK	0x0000ffff
#define ETCDC_DEFERCNT_MASK	0xffff0000
#define ETCDC_DEFERCNT_SHIFT	16

#define ETBR_ALIGNMENT		(64*1024)
#define ETBR_L_RINGSZ_MASK	0x00000001
#define ETBR_L_RINGSZ128	0
#define ETBR_L_RINGSZ512	1
#define ETBR_L_TXRINGBASE_MASK	0xffffc000

#define ETCIR_TXCONSUME_MASK	0x0000ffff
#define ETCIR_IDLE		0x80000000

#define ETPIR_TXPRODUCE_MASK	0x0000ffff

#define EBIR_TXBUFPROD_MASK	0x0000001f
#define EBIR_TXBUFCONS_MASK	0x00001f00
#define EBIR_TXBUFCONS_SHIFT	8
#define EBIR_RXBUFPROD_MASK	0x007fc000
#define EBIR_RXBUFPROD_SHIFT	14
#define EBIR_RXBUFCONS_MASK	0xff800000
#define EBIR_RXBUFCONS_SHIFT	23

#define MICR_REGADDR_MASK	0x0000001f
#define MICR_PHYADDR_MASK	0x000003e0
#define MICR_PHYADDR_SHIFT	5
#define MICR_READTRIG		0x00000400
#define MICR_BUSY		0x00000800

#define MIDR_DATA_MASK		0x0000ffff

#endif /* MIPS_SN_IOC3_H */
