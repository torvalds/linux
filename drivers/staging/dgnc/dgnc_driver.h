/*
 * Copyright 2003 Digi International (www.digi.com)
 *      Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 *************************************************************************
 *
 * Driver includes
 *
 *************************************************************************/

#ifndef __DGNC_DRIVER_H
#define __DGNC_DRIVER_H

#include <linux/types.h>
#include <linux/tty.h>
#include <linux/interrupt.h>

#include "digi.h"		/* Digi specific ioctl header */
#include "dgnc_sysfs.h"		/* Support for SYSFS */

/*************************************************************************
 *
 * Driver defines
 *
 *************************************************************************/

/* Driver identification and error statments */
#define	PROCSTR		"dgnc"			/* /proc entries	 */
#define	DEVSTR		"/dev/dg/dgnc"		/* /dev entries		 */
#define	DRVSTR		"dgnc"			/* Driver name string	 */
#define	DG_PART		"40002369_F"		/* RPM part number	 */

#define TRC_TO_CONSOLE 1

/* Number of boards we support at once. */
#define	MAXBOARDS	20
#define	MAXPORTS	8
#define MAXTTYNAMELEN	200

/* Our 3 magic numbers for our board, channel and unit structs */
#define DGNC_BOARD_MAGIC	0x5c6df104
#define DGNC_CHANNEL_MAGIC	0x6c6df104
#define DGNC_UNIT_MAGIC		0x7c6df104

/* Serial port types */
#define DGNC_SERIAL		0
#define DGNC_PRINT		1

#define	SERIAL_TYPE_NORMAL	1

#define PORT_NUM(dev)	((dev) & 0x7f)
#define IS_PRINT(dev)	(((dev) & 0xff) >= 0x80)

/* MAX number of stop characters we will send
 * when our read queue is getting full
 */
#define MAX_STOPS_SENT 5

/* 4 extra for alignment play space */
#define WRITEBUFLEN		((4096) + 4)

#define dgnc_jiffies_from_ms(a) (((a) * HZ) / 1000)

/*
 * Define a local default termios struct. All ports will be created
 * with this termios initially.  This is the same structure that is defined
 * as the default in tty_io.c with the same settings overridden as in serial.c
 *
 * In short, this should match the internal serial ports' defaults.
 */
#define	DEFAULT_IFLAGS	(ICRNL | IXON)
#define	DEFAULT_OFLAGS	(OPOST | ONLCR)
#define	DEFAULT_CFLAGS	(B9600 | CS8 | CREAD | HUPCL | CLOCAL)
#define	DEFAULT_LFLAGS	(ISIG | ICANON | ECHO | ECHOE | ECHOK | \
			ECHOCTL | ECHOKE | IEXTEN)

#ifndef _POSIX_VDISABLE
#define   _POSIX_VDISABLE '\0'
#endif

/*
 * All the possible states the driver can be while being loaded.
 */
enum {
	DRIVER_INITIALIZED = 0,
	DRIVER_READY
};

/*
 * All the possible states the board can be while booting up.
 */
enum {
	BOARD_FAILED = 0,
	BOARD_FOUND,
	BOARD_READY
};

/*************************************************************************
 *
 * Structures and closely related defines.
 *
 *************************************************************************/

struct dgnc_board;
struct channel_t;

/************************************************************************
 * Per board operations structure				       *
 ************************************************************************/
struct board_ops {
	void (*tasklet)(unsigned long data);
	irqreturn_t (*intr)(int irq, void *voidbrd);
	void (*uart_init)(struct channel_t *ch);
	void (*uart_off)(struct channel_t *ch);
	int  (*drain)(struct tty_struct *tty, uint seconds);
	void (*param)(struct tty_struct *tty);
	void (*vpd)(struct dgnc_board *brd);
	void (*assert_modem_signals)(struct channel_t *ch);
	void (*flush_uart_write)(struct channel_t *ch);
	void (*flush_uart_read)(struct channel_t *ch);
	void (*disable_receiver)(struct channel_t *ch);
	void (*enable_receiver)(struct channel_t *ch);
	void (*send_break)(struct channel_t *ch, int);
	void (*send_start_character)(struct channel_t *ch);
	void (*send_stop_character)(struct channel_t *ch);
	void (*copy_data_from_queue_to_uart)(struct channel_t *ch);
	uint (*get_uart_bytes_left)(struct channel_t *ch);
	void (*send_immediate_char)(struct channel_t *ch, unsigned char);
};

/************************************************************************
 * Device flag definitions for bd_flags.
 ************************************************************************/
#define BD_IS_PCI_EXPRESS     0x0001	  /* Is a PCI Express board */

/*
 *	Per-board information
 */
struct dgnc_board {
	int		magic;		/* Board Magic number.  */
	int		boardnum;	/* Board number: 0-32 */

	int		type;		/* Type of board */
	char		*name;		/* Product Name */
	struct pci_dev	*pdev;		/* Pointer to the pci_dev struct */
	unsigned long	bd_flags;	/* Board flags */
	u16		vendor;		/* PCI vendor ID */
	u16		device;		/* PCI device ID */
	u16		subvendor;	/* PCI subsystem vendor ID */
	u16		subdevice;	/* PCI subsystem device ID */
	unsigned char	rev;		/* PCI revision ID */
	uint		pci_bus;	/* PCI bus value */
	uint		pci_slot;	/* PCI slot value */
	uint		maxports;	/* MAX ports this board can handle */
	unsigned char	dvid;		/* Board specific device id */
	unsigned char	vpd[128];	/* VPD of board, if found */
	unsigned char	serial_num[20];	/* Serial number of board,
					 * if found in VPD
					 */

	spinlock_t	bd_lock;	/* Used to protect board */

	spinlock_t	bd_intr_lock;	/* Used to protect the poller tasklet
					 * and the interrupt routine from each
					 * other.
					 */

	uint		state;		/* State of card. */
	wait_queue_head_t state_wait;	/* Place to sleep on for state change */

	struct		tasklet_struct helper_tasklet; /* Poll helper tasklet */

	uint		nasync;		/* Number of ports on card */

	uint		irq;		/* Interrupt request number */
	ulong		intr_count;	/* Count of interrupts */
	ulong		intr_modem;	/* Count of interrupts */
	ulong		intr_tx;	/* Count of interrupts */
	ulong		intr_rx;	/* Count of interrupts */

	ulong		membase;	/* Start of base memory of the card */
	ulong		membase_end;	/* End of base memory of the card */

	u8 __iomem	*re_map_membase; /* Remapped memory of the card */

	ulong		iobase;		/* Start of io base of the card */
	ulong		iobase_end;	/* End of io base of the card */

	uint		bd_uart_offset;	/* Space between each UART */

	struct channel_t *channels[MAXPORTS];	/* array of pointers
						 * to our channels.
						 */

	struct tty_driver *serial_driver;
	char		serial_name[200];
	struct tty_driver *print_driver;
	char		print_name[200];

	bool		dgnc_major_serial_registered;
	bool		dgnc_major_transparent_print_registered;

	u16		dpatype;	/* The board "type",
					 * as defined by DPA
					 */
	u16		dpastatus;	/* The board "status",
					 * as defined by DPA
					 */

	/*
	 *	Mgmt data.
	 */
	char		*msgbuf_head;
	char		*msgbuf;

	uint		bd_dividend;	/* Board/UARTs specific dividend */

	struct board_ops *bd_ops;

	/* /proc/<board> entries */
	struct proc_dir_entry *proc_entry_pointer;
	struct dgnc_proc_entry *dgnc_board_table;

};

/************************************************************************
 * Unit flag definitions for un_flags.
 ************************************************************************/
#define UN_ISOPEN	0x0001		/* Device is open		*/
#define UN_CLOSING	0x0002		/* Line is being closed		*/
#define UN_IMM		0x0004		/* Service immediately		*/
#define UN_BUSY		0x0008		/* Some work this channel	*/
#define UN_BREAKI	0x0010		/* Input break received		*/
#define UN_PWAIT	0x0020		/* Printer waiting for terminal	*/
#define UN_TIME		0x0040		/* Waiting on time		*/
#define UN_EMPTY	0x0080		/* Waiting output queue empty	*/
#define UN_LOW		0x0100		/* Waiting output low water mark*/
#define UN_EXCL_OPEN	0x0200		/* Open for exclusive use	*/
#define UN_WOPEN	0x0400		/* Device waiting for open	*/
#define UN_WIOCTL	0x0800		/* Device waiting for open	*/
#define UN_HANGUP	0x8000		/* Carrier lost			*/

struct device;

/************************************************************************
 * Structure for terminal or printer unit.
 ************************************************************************/
struct un_t {
	int	magic;		/* Unit Magic Number.			*/
	struct	channel_t *un_ch;
	ulong	un_time;
	uint	un_type;
	uint	un_open_count;	/* Counter of opens to port		*/
	struct tty_struct *un_tty;/* Pointer to unit tty structure	*/
	uint	un_flags;	/* Unit flags				*/
	wait_queue_head_t un_flags_wait; /* Place to sleep to wait on unit */
	uint	un_dev;		/* Minor device number			*/
	struct device *un_sysfs;
};

/************************************************************************
 * Device flag definitions for ch_flags.
 ************************************************************************/
#define CH_PRON		0x0001		/* Printer on string		*/
#define CH_STOP		0x0002		/* Output is stopped		*/
#define CH_STOPI	0x0004		/* Input is stopped		*/
#define CH_CD		0x0008		/* Carrier is present		*/
#define CH_FCAR		0x0010		/* Carrier forced on		*/
#define CH_HANGUP       0x0020		/* Hangup received		*/

#define CH_RECEIVER_OFF	0x0040		/* Receiver is off		*/
#define CH_OPENING	0x0080		/* Port in fragile open state	*/
#define CH_CLOSING	0x0100		/* Port in fragile close state	*/
#define CH_FIFO_ENABLED 0x0200		/* Port has FIFOs enabled	*/
#define CH_TX_FIFO_EMPTY 0x0400		/* TX Fifo is completely empty	*/
#define CH_TX_FIFO_LWM  0x0800		/* TX Fifo is below Low Water	*/
#define CH_BREAK_SENDING 0x1000		/* Break is being sent		*/
#define CH_LOOPBACK 0x2000		/* Channel is in lookback mode	*/
#define CH_BAUD0	0x08000		/* Used for checking B0 transitions */
#define CH_FORCED_STOP  0x20000		/* Output is forcibly stopped	*/
#define CH_FORCED_STOPI 0x40000		/* Input is forcibly stopped	*/

/* Our Read/Error/Write queue sizes */
#define RQUEUEMASK	0x1FFF		/* 8 K - 1 */
#define EQUEUEMASK	0x1FFF		/* 8 K - 1 */
#define WQUEUEMASK	0x0FFF		/* 4 K - 1 */
#define RQUEUESIZE	(RQUEUEMASK + 1)
#define EQUEUESIZE	RQUEUESIZE
#define WQUEUESIZE	(WQUEUEMASK + 1)

/************************************************************************
 * Channel information structure.
 ************************************************************************/
struct channel_t {
	int magic;			/* Channel Magic Number		*/
	struct dgnc_board	*ch_bd;		/* Board structure pointer */
	struct digi_t	ch_digi;	/* Transparent Print structure  */
	struct un_t	ch_tun;		/* Terminal unit info	   */
	struct un_t	ch_pun;		/* Printer unit info	    */

	spinlock_t	ch_lock;	/* provide for serialization */
	wait_queue_head_t ch_flags_wait;

	uint		ch_portnum;	/* Port number, 0 offset.	*/
	uint		ch_open_count;	/* open count			*/
	uint		ch_flags;	/* Channel flags		*/

	ulong		ch_close_delay;	/* How long we should
					 * drop RTS/DTR for
					 */

	ulong		ch_cpstime;	/* Time for CPS calculations    */

	tcflag_t	ch_c_iflag;	/* channel iflags	       */
	tcflag_t	ch_c_cflag;	/* channel cflags	       */
	tcflag_t	ch_c_oflag;	/* channel oflags	       */
	tcflag_t	ch_c_lflag;	/* channel lflags	       */
	unsigned char	ch_stopc;	/* Stop character	       */
	unsigned char	ch_startc;	/* Start character	      */

	uint		ch_old_baud;	/* Cache of the current baud */
	uint		ch_custom_speed;/* Custom baud, if set */

	uint		ch_wopen;	/* Waiting for open process cnt */

	unsigned char		ch_mostat;	/* FEP output modem status */
	unsigned char		ch_mistat;	/* FEP input modem status */

	struct neo_uart_struct __iomem *ch_neo_uart;	/* Pointer to the
							 * "mapped" UART struct
							 */
	struct cls_uart_struct __iomem *ch_cls_uart;	/* Pointer to the
							 * "mapped" UART struct
							 */

	unsigned char	ch_cached_lsr;	/* Cached value of the LSR register */

	unsigned char	*ch_rqueue;	/* Our read queue buffer - malloc'ed */
	ushort		ch_r_head;	/* Head location of the read queue */
	ushort		ch_r_tail;	/* Tail location of the read queue */

	unsigned char	*ch_equeue;	/* Our error queue buffer - malloc'ed */
	ushort		ch_e_head;	/* Head location of the error queue */
	ushort		ch_e_tail;	/* Tail location of the error queue */

	unsigned char	*ch_wqueue;	/* Our write queue buffer - malloc'ed */
	ushort		ch_w_head;	/* Head location of the write queue */
	ushort		ch_w_tail;	/* Tail location of the write queue */

	ulong		ch_rxcount;	/* total of data received so far */
	ulong		ch_txcount;	/* total of data transmitted so far */

	unsigned char		ch_r_tlevel;	/* Receive Trigger level */
	unsigned char		ch_t_tlevel;	/* Transmit Trigger level */

	unsigned char		ch_r_watermark;	/* Receive Watermark */

	ulong		ch_stop_sending_break;	/* Time we should STOP
						 * sending a break
						 */

	uint		ch_stops_sent;	/* How many times I have sent a stop
					 * character to try to stop the other
					 * guy sending.
					 */
	ulong		ch_err_parity;	/* Count of parity errors on channel */
	ulong		ch_err_frame;	/* Count of framing errors on channel */
	ulong		ch_err_break;	/* Count of breaks on channel */
	ulong		ch_err_overrun; /* Count of overruns on channel */

	ulong		ch_xon_sends;	/* Count of xons transmitted */
	ulong		ch_xoff_sends;	/* Count of xoffs transmitted */

	ulong		ch_intr_modem;	/* Count of interrupts */
	ulong		ch_intr_tx;	/* Count of interrupts */
	ulong		ch_intr_rx;	/* Count of interrupts */

	/* /proc/<board>/<channel> entries */
	struct proc_dir_entry *proc_entry_pointer;
	struct dgnc_proc_entry *dgnc_channel_table;

};

/*
 * Our Global Variables.
 */
extern uint		dgnc_major;		/* Our driver/mgmt major */
extern int		dgnc_poll_tick;		/* Poll interval - 20 ms */
extern spinlock_t	dgnc_global_lock;	/* Driver global spinlock */
extern spinlock_t	dgnc_poll_lock;		/* Poll scheduling lock */
extern uint		dgnc_num_boards;		/* Total number of boards */
extern struct dgnc_board	*dgnc_board[MAXBOARDS];	/* Array of board
							 * structs
							 */

#endif
