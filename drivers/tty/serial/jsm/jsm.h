/* SPDX-License-Identifier: GPL-2.0+ */
/************************************************************************
 * Copyright 2003 Digi International (www.digi.com)
 *
 * Copyright (C) 2004 IBM Corporation. All rights reserved.
 *
 * Contact Information:
 * Scott H Kilau <Scott_Kilau@digi.com>
 * Wendy Xiong   <wendyx@us.ibm.com>
 *
 ***********************************************************************/

#ifndef __JSM_DRIVER_H
#define __JSM_DRIVER_H

#include <linux/kernel.h>
#include <linux/types.h>	/* To pick up the varions Linux types */
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/device.h>

/*
 * Debugging levels can be set using debug insmod variable
 * They can also be compiled out completely.
 */
enum {
	DBG_INIT	= 0x01,
	DBG_BASIC	= 0x02,
	DBG_CORE	= 0x04,
	DBG_OPEN	= 0x08,
	DBG_CLOSE	= 0x10,
	DBG_READ	= 0x20,
	DBG_WRITE	= 0x40,
	DBG_IOCTL	= 0x80,
	DBG_PROC	= 0x100,
	DBG_PARAM	= 0x200,
	DBG_PSCAN	= 0x400,
	DBG_EVENT	= 0x800,
	DBG_DRAIN	= 0x1000,
	DBG_MSIGS	= 0x2000,
	DBG_MGMT	= 0x4000,
	DBG_INTR	= 0x8000,
	DBG_CARR	= 0x10000,
};

#define jsm_dbg(nlevel, pdev, fmt, ...)				\
do {								\
	if (DBG_##nlevel & jsm_debug)				\
		dev_dbg(pdev->dev, fmt, ##__VA_ARGS__);		\
} while (0)

#define	MAXLINES	256
#define MAXPORTS	8
#define MAX_STOPS_SENT	5

/* Board ids */
#define PCI_DEVICE_ID_CLASSIC_4		0x0028
#define PCI_DEVICE_ID_CLASSIC_8		0x0029
#define PCI_DEVICE_ID_CLASSIC_4_422	0x00D0
#define PCI_DEVICE_ID_CLASSIC_8_422	0x00D1
#define PCI_DEVICE_ID_NEO_4             0x00B0
#define PCI_DEVICE_ID_NEO_1_422         0x00CC
#define PCI_DEVICE_ID_NEO_1_422_485     0x00CD
#define PCI_DEVICE_ID_NEO_2_422_485     0x00CE
#define PCIE_DEVICE_ID_NEO_8            0x00F0
#define PCIE_DEVICE_ID_NEO_4            0x00F1
#define PCIE_DEVICE_ID_NEO_4RJ45        0x00F2
#define PCIE_DEVICE_ID_NEO_8RJ45        0x00F3

/* Board type definitions */

#define T_NEO		0000
#define T_CLASSIC	0001
#define T_PCIBUS	0400

/* Board State Definitions */

#define BD_RUNNING	0x0
#define BD_REASON	0x7f
#define BD_NOTFOUND	0x1
#define BD_NOIOPORT	0x2
#define BD_NOMEM	0x3
#define BD_NOBIOS	0x4
#define BD_NOFEP	0x5
#define BD_FAILED	0x6
#define BD_ALLOCATED	0x7
#define BD_TRIBOOT	0x8
#define BD_BADKME	0x80


/* 4 extra for alignment play space */
#define WRITEBUFLEN	((4096) + 4)

#define JSM_VERSION	"jsm: 1.2-1-INKERNEL"
#define JSM_PARTNUM	"40002438_A-INKERNEL"

struct jsm_board;
struct jsm_channel;

/************************************************************************
 * Per board operations structure					*
 ************************************************************************/
struct board_ops {
	irq_handler_t intr;
	void (*uart_init)(struct jsm_channel *ch);
	void (*uart_off)(struct jsm_channel *ch);
	void (*param)(struct jsm_channel *ch);
	void (*assert_modem_signals)(struct jsm_channel *ch);
	void (*flush_uart_write)(struct jsm_channel *ch);
	void (*flush_uart_read)(struct jsm_channel *ch);
	void (*disable_receiver)(struct jsm_channel *ch);
	void (*enable_receiver)(struct jsm_channel *ch);
	void (*send_break)(struct jsm_channel *ch);
	void (*clear_break)(struct jsm_channel *ch);
	void (*send_start_character)(struct jsm_channel *ch);
	void (*send_stop_character)(struct jsm_channel *ch);
	void (*copy_data_from_queue_to_uart)(struct jsm_channel *ch);
	u32 (*get_uart_bytes_left)(struct jsm_channel *ch);
	void (*send_immediate_char)(struct jsm_channel *ch, unsigned char);
};


/*
 *	Per-board information
 */
struct jsm_board
{
	int		boardnum;	/* Board number: 0-32 */

	int		type;		/* Type of board */
	u8		rev;		/* PCI revision ID */
	struct pci_dev	*pci_dev;
	u32		maxports;	/* MAX ports this board can handle */

	spinlock_t	bd_intr_lock;	/* Used to protect the poller tasklet and
					 * the interrupt routine from each other.
					 */

	u32		nasync;		/* Number of ports on card */

	u32		irq;		/* Interrupt request number */

	u64		membase;	/* Start of base memory of the card */
	u64		membase_end;	/* End of base memory of the card */

	u8	__iomem *re_map_membase;/* Remapped memory of the card */

	u64		iobase;		/* Start of io base of the card */
	u64		iobase_end;	/* End of io base of the card */

	u32		bd_uart_offset;	/* Space between each UART */

	struct jsm_channel *channels[MAXPORTS]; /* array of pointers to our channels. */

	u32		bd_dividend;	/* Board/UARTs specific dividend */

	struct board_ops *bd_ops;

	struct list_head jsm_board_entry;
};

/************************************************************************
 * Device flag definitions for ch_flags.
 ************************************************************************/
#define CH_PRON		0x0001		/* Printer on string		*/
#define CH_STOP		0x0002		/* Output is stopped		*/
#define CH_STOPI	0x0004		/* Input is stopped		*/
#define CH_CD		0x0008		/* Carrier is present		*/
#define CH_FCAR		0x0010		/* Carrier forced on		*/
#define CH_HANGUP	0x0020		/* Hangup received		*/

#define CH_RECEIVER_OFF	0x0040		/* Receiver is off		*/
#define CH_OPENING	0x0080		/* Port in fragile open state	*/
#define CH_CLOSING	0x0100		/* Port in fragile close state	*/
#define CH_FIFO_ENABLED 0x0200		/* Port has FIFOs enabled	*/
#define CH_TX_FIFO_EMPTY 0x0400		/* TX Fifo is completely empty	*/
#define CH_TX_FIFO_LWM	0x0800		/* TX Fifo is below Low Water	*/
#define CH_BREAK_SENDING 0x1000		/* Break is being sent		*/
#define CH_LOOPBACK 0x2000		/* Channel is in lookback mode	*/
#define CH_BAUD0	0x08000		/* Used for checking B0 transitions */

/* Our Read/Error queue sizes */
#define RQUEUEMASK	0x1FFF		/* 8 K - 1 */
#define EQUEUEMASK	0x1FFF		/* 8 K - 1 */
#define RQUEUESIZE	(RQUEUEMASK + 1)
#define EQUEUESIZE	RQUEUESIZE


/************************************************************************
 * Channel information structure.
 ************************************************************************/
struct jsm_channel {
	struct uart_port uart_port;
	struct jsm_board	*ch_bd;		/* Board structure pointer	*/

	spinlock_t	ch_lock;	/* provide for serialization */
	wait_queue_head_t ch_flags_wait;

	u32		ch_portnum;	/* Port number, 0 offset.	*/
	u32		ch_open_count;	/* open count			*/
	u32		ch_flags;	/* Channel flags		*/

	u64		ch_close_delay;	/* How long we should drop RTS/DTR for */

	tcflag_t	ch_c_iflag;	/* channel iflags		*/
	tcflag_t	ch_c_cflag;	/* channel cflags		*/
	tcflag_t	ch_c_oflag;	/* channel oflags		*/
	tcflag_t	ch_c_lflag;	/* channel lflags		*/
	u8		ch_stopc;	/* Stop character		*/
	u8		ch_startc;	/* Start character		*/

	u8		ch_mostat;	/* FEP output modem status	*/
	u8		ch_mistat;	/* FEP input modem status	*/

	/* Pointers to the "mapped" UART structs */
	struct neo_uart_struct __iomem *ch_neo_uart; /* NEO card */
	struct cls_uart_struct __iomem *ch_cls_uart; /* Classic card */

	u8		ch_cached_lsr;	/* Cached value of the LSR register */

	u8		*ch_rqueue;	/* Our read queue buffer - malloc'ed */
	u16		ch_r_head;	/* Head location of the read queue */
	u16		ch_r_tail;	/* Tail location of the read queue */

	u8		*ch_equeue;	/* Our error queue buffer - malloc'ed */
	u16		ch_e_head;	/* Head location of the error queue */
	u16		ch_e_tail;	/* Tail location of the error queue */

	u64		ch_rxcount;	/* total of data received so far */
	u64		ch_txcount;	/* total of data transmitted so far */

	u8		ch_r_tlevel;	/* Receive Trigger level */
	u8		ch_t_tlevel;	/* Transmit Trigger level */

	u8		ch_r_watermark;	/* Receive Watermark */


	u32		ch_stops_sent;	/* How many times I have sent a stop character
					 * to try to stop the other guy sending.
					 */
	u64		ch_err_parity;	/* Count of parity errors on channel */
	u64		ch_err_frame;	/* Count of framing errors on channel */
	u64		ch_err_break;	/* Count of breaks on channel */
	u64		ch_err_overrun; /* Count of overruns on channel */

	u64		ch_xon_sends;	/* Count of xons transmitted */
	u64		ch_xoff_sends;	/* Count of xoffs transmitted */
};

/************************************************************************
 * Per channel/port Classic UART structures				*
 ************************************************************************
 *		Base Structure Entries Usage Meanings to Host		*
 *									*
 *	W = read write		R = read only				*
 *			U = Unused.					*
 ************************************************************************/

struct cls_uart_struct {
	u8 txrx;	/* WR  RHR/THR - Holding Reg */
	u8 ier;		/* WR  IER - Interrupt Enable Reg */
	u8 isr_fcr;	/* WR  ISR/FCR - Interrupt Status Reg/Fifo Control Reg*/
	u8 lcr;		/* WR  LCR - Line Control Reg */
	u8 mcr;		/* WR  MCR - Modem Control Reg */
	u8 lsr;		/* WR  LSR - Line Status Reg */
	u8 msr;		/* WR  MSR - Modem Status Reg */
	u8 spr;		/* WR  SPR - Scratch Pad Reg */
};

/* Where to read the interrupt register (8bits) */
#define UART_CLASSIC_POLL_ADDR_OFFSET	0x40

#define UART_EXAR654_ENHANCED_REGISTER_SET 0xBF

#define UART_16654_FCR_TXTRIGGER_8	0x0
#define UART_16654_FCR_TXTRIGGER_16	0x10
#define UART_16654_FCR_TXTRIGGER_32	0x20
#define UART_16654_FCR_TXTRIGGER_56	0x30

#define UART_16654_FCR_RXTRIGGER_8	0x0
#define UART_16654_FCR_RXTRIGGER_16	0x40
#define UART_16654_FCR_RXTRIGGER_56	0x80
#define UART_16654_FCR_RXTRIGGER_60	0xC0

#define UART_IIR_CTSRTS			0x20	/* Received CTS/RTS change of state */
#define UART_IIR_RDI_TIMEOUT		0x0C    /* Receiver data TIMEOUT */

/*
 * These are the EXTENDED definitions for the Exar 654's Interrupt
 * Enable Register.
 */
#define UART_EXAR654_EFR_ECB      0x10    /* Enhanced control bit */
#define UART_EXAR654_EFR_IXON     0x2     /* Receiver compares Xon1/Xoff1 */
#define UART_EXAR654_EFR_IXOFF    0x8     /* Transmit Xon1/Xoff1 */
#define UART_EXAR654_EFR_RTSDTR   0x40    /* Auto RTS/DTR Flow Control Enable */
#define UART_EXAR654_EFR_CTSDSR   0x80    /* Auto CTS/DSR Flow COntrol Enable */

#define UART_EXAR654_XOFF_DETECT  0x1     /* Indicates whether chip saw an incoming XOFF char  */
#define UART_EXAR654_XON_DETECT   0x2     /* Indicates whether chip saw an incoming XON char */

#define UART_EXAR654_IER_XOFF     0x20    /* Xoff Interrupt Enable */
#define UART_EXAR654_IER_RTSDTR   0x40    /* Output Interrupt Enable */
#define UART_EXAR654_IER_CTSDSR   0x80    /* Input Interrupt Enable */

/************************************************************************
 * Per channel/port NEO UART structure					*
 ************************************************************************
 *		Base Structure Entries Usage Meanings to Host		*
 *									*
 *	W = read write		R = read only				*
 *			U = Unused.					*
 ************************************************************************/

struct neo_uart_struct {
	 u8 txrx;		/* WR	RHR/THR - Holding Reg */
	 u8 ier;		/* WR	IER - Interrupt Enable Reg */
	 u8 isr_fcr;		/* WR	ISR/FCR - Interrupt Status Reg/Fifo Control Reg */
	 u8 lcr;		/* WR	LCR - Line Control Reg */
	 u8 mcr;		/* WR	MCR - Modem Control Reg */
	 u8 lsr;		/* WR	LSR - Line Status Reg */
	 u8 msr;		/* WR	MSR - Modem Status Reg */
	 u8 spr;		/* WR	SPR - Scratch Pad Reg */
	 u8 fctr;		/* WR	FCTR - Feature Control Reg */
	 u8 efr;		/* WR	EFR - Enhanced Function Reg */
	 u8 tfifo;		/* WR	TXCNT/TXTRG - Transmit FIFO Reg */
	 u8 rfifo;		/* WR	RXCNT/RXTRG - Receive FIFO Reg */
	 u8 xoffchar1;	/* WR	XOFF 1 - XOff Character 1 Reg */
	 u8 xoffchar2;	/* WR	XOFF 2 - XOff Character 2 Reg */
	 u8 xonchar1;	/* WR	XON 1 - Xon Character 1 Reg */
	 u8 xonchar2;	/* WR	XON 2 - XOn Character 2 Reg */

	 u8 reserved1[0x2ff - 0x200]; /* U	Reserved by Exar */
	 u8 txrxburst[64];	/* RW	64 bytes of RX/TX FIFO Data */
	 u8 reserved2[0x37f - 0x340]; /* U	Reserved by Exar */
	 u8 rxburst_with_errors[64];	/* R	64 bytes of RX FIFO Data + LSR */
};

/* Where to read the extended interrupt register (32bits instead of 8bits) */
#define	UART_17158_POLL_ADDR_OFFSET	0x80

/*
 * These are the redefinitions for the FCTR on the XR17C158, since
 * Exar made them different than their earlier design. (XR16C854)
 */

/* These are only applicable when table D is selected */
#define UART_17158_FCTR_RTS_NODELAY	0x00
#define UART_17158_FCTR_RTS_4DELAY	0x01
#define UART_17158_FCTR_RTS_6DELAY	0x02
#define UART_17158_FCTR_RTS_8DELAY	0x03
#define UART_17158_FCTR_RTS_12DELAY	0x12
#define UART_17158_FCTR_RTS_16DELAY	0x05
#define UART_17158_FCTR_RTS_20DELAY	0x13
#define UART_17158_FCTR_RTS_24DELAY	0x06
#define UART_17158_FCTR_RTS_28DELAY	0x14
#define UART_17158_FCTR_RTS_32DELAY	0x07
#define UART_17158_FCTR_RTS_36DELAY	0x16
#define UART_17158_FCTR_RTS_40DELAY	0x08
#define UART_17158_FCTR_RTS_44DELAY	0x09
#define UART_17158_FCTR_RTS_48DELAY	0x10
#define UART_17158_FCTR_RTS_52DELAY	0x11

#define UART_17158_FCTR_RTS_IRDA	0x10
#define UART_17158_FCTR_RS485		0x20
#define UART_17158_FCTR_TRGA		0x00
#define UART_17158_FCTR_TRGB		0x40
#define UART_17158_FCTR_TRGC		0x80
#define UART_17158_FCTR_TRGD		0xC0

/* 17158 trigger table selects.. */
#define UART_17158_FCTR_BIT6		0x40
#define UART_17158_FCTR_BIT7		0x80

/* 17158 TX/RX memmapped buffer offsets */
#define UART_17158_RX_FIFOSIZE		64
#define UART_17158_TX_FIFOSIZE		64

/* 17158 Extended IIR's */
#define UART_17158_IIR_RDI_TIMEOUT	0x0C	/* Receiver data TIMEOUT */
#define UART_17158_IIR_XONXOFF		0x10	/* Received an XON/XOFF char */
#define UART_17158_IIR_HWFLOW_STATE_CHANGE 0x20	/* CTS/DSR or RTS/DTR state change */
#define UART_17158_IIR_FIFO_ENABLED	0xC0	/* 16550 FIFOs are Enabled */

/*
 * These are the extended interrupts that get sent
 * back to us from the UART's 32bit interrupt register
 */
#define UART_17158_RX_LINE_STATUS	0x1	/* RX Ready */
#define UART_17158_RXRDY_TIMEOUT	0x2	/* RX Ready Timeout */
#define UART_17158_TXRDY		0x3	/* TX Ready */
#define UART_17158_MSR			0x4	/* Modem State Change */
#define UART_17158_TX_AND_FIFO_CLR	0x40	/* Transmitter Holding Reg Empty */
#define UART_17158_RX_FIFO_DATA_ERROR	0x80	/* UART detected an RX FIFO Data error */

/*
 * These are the EXTENDED definitions for the 17C158's Interrupt
 * Enable Register.
 */
#define UART_17158_EFR_ECB	0x10	/* Enhanced control bit */
#define UART_17158_EFR_IXON	0x2	/* Receiver compares Xon1/Xoff1 */
#define UART_17158_EFR_IXOFF	0x8	/* Transmit Xon1/Xoff1 */
#define UART_17158_EFR_RTSDTR	0x40	/* Auto RTS/DTR Flow Control Enable */
#define UART_17158_EFR_CTSDSR	0x80	/* Auto CTS/DSR Flow COntrol Enable */

#define UART_17158_XOFF_DETECT	0x1	/* Indicates whether chip saw an incoming XOFF char */
#define UART_17158_XON_DETECT	0x2	/* Indicates whether chip saw an incoming XON char */

#define UART_17158_IER_RSVD1	0x10	/* Reserved by Exar */
#define UART_17158_IER_XOFF	0x20	/* Xoff Interrupt Enable */
#define UART_17158_IER_RTSDTR	0x40	/* Output Interrupt Enable */
#define UART_17158_IER_CTSDSR	0x80	/* Input Interrupt Enable */

#define PCI_DEVICE_NEO_2DB9_PCI_NAME		"Neo 2 - DB9 Universal PCI"
#define PCI_DEVICE_NEO_2DB9PRI_PCI_NAME		"Neo 2 - DB9 Universal PCI - Powered Ring Indicator"
#define PCI_DEVICE_NEO_2RJ45_PCI_NAME		"Neo 2 - RJ45 Universal PCI"
#define PCI_DEVICE_NEO_2RJ45PRI_PCI_NAME	"Neo 2 - RJ45 Universal PCI - Powered Ring Indicator"
#define PCIE_DEVICE_NEO_IBM_PCI_NAME		"Neo 4 - PCI Express - IBM"

/*
 * Our Global Variables.
 */
extern struct	uart_driver jsm_uart_driver;
extern struct	board_ops jsm_neo_ops;
extern struct	board_ops jsm_cls_ops;
extern int	jsm_debug;

/*************************************************************************
 *
 * Prototypes for non-static functions used in more than one module
 *
 *************************************************************************/
int jsm_tty_init(struct jsm_board *);
int jsm_uart_port_init(struct jsm_board *);
int jsm_remove_uart_port(struct jsm_board *);
void jsm_input(struct jsm_channel *ch);
void jsm_check_queue_flow_control(struct jsm_channel *ch);

#endif
