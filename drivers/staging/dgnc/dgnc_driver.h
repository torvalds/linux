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
 */

#ifndef _DGNC_DRIVER_H
#define _DGNC_DRIVER_H

#include <linux/types.h>
#include <linux/tty.h>
#include <linux/interrupt.h>

#include "digi.h"		/* Digi specific ioctl header */

/* Driver identification and error statements */
#define	PROCSTR		"dgnc"			/* /proc entries */
#define	DEVSTR		"/dev/dg/dgnc"		/* /dev entries */
#define	DRVSTR		"dgnc"			/* Driver name string */
#define	DG_PART		"40002369_F"		/* RPM part number */

#define TRC_TO_CONSOLE 1

/* Number of boards we support at once. */
#define	MAXBOARDS	20
#define	MAXPORTS	8
#define MAXTTYNAMELEN	200

/* Serial port types */
#define DGNC_SERIAL		0
#define DGNC_PRINT		1

#define	SERIAL_TYPE_NORMAL	1

#define PORT_NUM(dev)	((dev) & 0x7f)
#define IS_PRINT(dev)	(((dev) & 0xff) >= 0x80)

/* MAX number of stop characters sent when our read queue is getting full */
#define MAX_STOPS_SENT 5

/* 4 extra for alignment play space */
#define WRITEBUFLEN		((4096) + 4)

#define dgnc_jiffies_from_ms(a) (((a) * HZ) / 1000)

#ifndef _POSIX_VDISABLE
#define   _POSIX_VDISABLE '\0'
#endif

/* All the possible states the driver can be while being loaded. */
enum {
	DRIVER_INITIALIZED = 0,
	DRIVER_READY
};

/* All the possible states the board can be while booting up. */
enum {
	BOARD_FAILED = 0,
	BOARD_FOUND,
	BOARD_READY
};

struct dgnc_board;
struct channel_t;

/**
 * struct board_ops - Per board operations.
 */
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

/* Device flag definitions for bd_flags. */

#define BD_IS_PCI_EXPRESS     0x0001	  /* Is a PCI Express board */

/**
 * struct dgnc_board - Per board information.
 * @boardnum: Board number (0 - 32).
 *
 * @name: Product name.
 * @pdev: Pointer to the pci_dev structure.
 * @bd_flags: Board flags.
 * @vendor: PCI vendor ID.
 * @device: PCI device ID.
 * @subvendor: PCI subsystem vendor ID.
 * @subdevice: PCI subsystem device ID.
 * @rev: PCI revision ID.
 * @pci_bus: PCI bus value.
 * @pci_slot: PCI slot value.
 * @maxports: Maximum ports this board can handle.
 * @dvid: Board specific device ID.
 * @vpd: VPD of this board, if found.
 * @serial_num: Serial number of this board, if found in VPD.
 * @bd_lock: Used to protect board.
 * @bd_intr_lock: Protect poller tasklet and interrupt routine from each other.
 * @state: State of the card.
 * @state_wait: Queue to sleep on for state change.
 * @helper_tasklet: Poll helper tasklet.
 * @nasync: Number of ports on card.
 * @irq: Interrupt request number.
 * @membase: Start of base memory of the card.
 * @membase_end: End of base memory of the card.
 * @iobase: Start of IO base of the card.
 * @iobase_end: End of IO base of the card.
 * @bd_uart_offset: Space between each UART.
 * @channels: array of pointers to our channels.
 * @serial_driver: Pointer to the serial driver.
 * @serial_name: Serial driver name.
 * @print_dirver: Pointer to the print driver.
 * @print_name: Print driver name.
 * @dpatype: Board type as defined by DPA.
 * @dpastatus: Board status as defined by DPA.
 * @bd_dividend: Board/UART's specific dividend.
 * @bd_ops: Pointer to board operations structure.
 */
struct dgnc_board {
	int		boardnum;
	char		*name;
	struct pci_dev	*pdev;
	unsigned long	bd_flags;
	u16		vendor;
	u16		device;
	u16		subvendor;
	u16		subdevice;
	unsigned char	rev;
	uint		pci_bus;
	uint		pci_slot;
	uint		maxports;
	unsigned char	dvid;
	unsigned char	vpd[128];
	unsigned char	serial_num[20];

	/* used to protect the board */
	spinlock_t	bd_lock;

	/*  Protect poller tasklet and interrupt routine from each other. */
	spinlock_t	bd_intr_lock;

	uint		state;
	wait_queue_head_t state_wait;

	struct tasklet_struct helper_tasklet;

	uint		nasync;

	uint		irq;

	ulong		membase;
	ulong		membase_end;

	u8 __iomem	*re_map_membase;

	ulong		iobase;
	ulong		iobase_end;

	uint		bd_uart_offset;

	struct channel_t *channels[MAXPORTS];

	struct tty_driver *serial_driver;
	char		serial_name[200];
	struct tty_driver *print_driver;
	char		print_name[200];

	u16		dpatype;
	u16		dpastatus;

	uint		bd_dividend;

	struct board_ops *bd_ops;
};

/* Unit flag definitions for un_flags. */
#define UN_ISOPEN	0x0001		/* Device is open */
#define UN_CLOSING	0x0002		/* Line is being closed	*/
#define UN_IMM		0x0004		/* Service immediately */
#define UN_BUSY		0x0008		/* Some work this channel */
#define UN_BREAKI	0x0010		/* Input break received	*/
#define UN_PWAIT	0x0020		/* Printer waiting for terminal	*/
#define UN_TIME		0x0040		/* Waiting on time */
#define UN_EMPTY	0x0080		/* Waiting output queue empty */
#define UN_LOW		0x0100		/* Waiting output low water mark*/
#define UN_EXCL_OPEN	0x0200		/* Open for exclusive use */
#define UN_WOPEN	0x0400		/* Device waiting for open */
#define UN_WIOCTL	0x0800		/* Device waiting for open */
#define UN_HANGUP	0x8000		/* Carrier lost	*/

struct device;

/**
 * struct un_t - terminal or printer unit
 * @un_open_count: Counter of opens to port.
 * @un_tty: Pointer to unit tty structure.
 * @un_flags: Unit flags.
 * @un_flags_wait: Place to sleep to wait on unit.
 * @un_dev: Minor device number.
 */
struct un_t {
	struct	channel_t *un_ch;
	uint	un_type;
	uint	un_open_count;
	struct tty_struct *un_tty;
	uint	un_flags;
	wait_queue_head_t un_flags_wait;
	uint	un_dev;
	struct device *un_sysfs;
};

/* Device flag definitions for ch_flags. */
#define CH_PRON		0x0001		/* Printer on string */
#define CH_STOP		0x0002		/* Output is stopped */
#define CH_STOPI	0x0004		/* Input is stopped */
#define CH_CD		0x0008		/* Carrier is present */
#define CH_FCAR		0x0010		/* Carrier forced on */
#define CH_HANGUP       0x0020		/* Hangup received */

#define CH_RECEIVER_OFF	0x0040		/* Receiver is off */
#define CH_OPENING	0x0080		/* Port in fragile open state */
#define CH_CLOSING	0x0100		/* Port in fragile close state */
#define CH_FIFO_ENABLED 0x0200		/* Port has FIFOs enabled */
#define CH_TX_FIFO_EMPTY 0x0400		/* TX Fifo is completely empty */
#define CH_TX_FIFO_LWM  0x0800		/* TX Fifo is below Low Water */
#define CH_BREAK_SENDING 0x1000		/* Break is being sent */
#define CH_LOOPBACK	0x2000		/* Channel is in lookback mode */
#define CH_BAUD0	0x08000		/* Used for checking B0 transitions */
#define CH_FORCED_STOP  0x20000		/* Output is forcibly stopped */
#define CH_FORCED_STOPI 0x40000		/* Input is forcibly stopped */

/* Our Read/Error/Write queue sizes */
#define RQUEUEMASK	0x1FFF		/* 8 K - 1 */
#define EQUEUEMASK	0x1FFF		/* 8 K - 1 */
#define WQUEUEMASK	0x0FFF		/* 4 K - 1 */
#define RQUEUESIZE	(RQUEUEMASK + 1)
#define EQUEUESIZE	RQUEUESIZE
#define WQUEUESIZE	(WQUEUEMASK + 1)

/**
 * struct channel_t - Channel information.
 * @dgnc_board: Pointer to board structure.
 * @ch_bd: Transparent print structure.
 * @ch_tun: Terminal unit information.
 * @ch_pun: Printer unit information.
 * @ch_lock: Provide for serialization.
 * @ch_flags_wait: Channel flags wait queue.
 * @ch_portnum: Port number, 0 offset.
 * @ch_open_count: Open count.
 * @ch_flags: Channel flags.
 * @ch_close_delay: How long we should drop RTS/DTR for.
 * @ch_cpstime: Time for CPS calculations.
 * @ch_c_iflag: Channel iflags.
 * @ch_c_cflag: Channel cflags.
 * @ch_c_oflag: Channel oflags.
 * @ch_c_lflag: Channel lflags.
 * @ch_stopc: Stop character.
 * @ch_startc: Start character.
 * @ch_old_baud: Cache of the current baud rate.
 * @ch_custom_speed: Custom baud rate, if set.
 * @ch_wopen: Waiting for open process count.
 * @ch_mostat: FEP output modem status.
 * @ch_mistat: FEP input modem status.
 * @chc_neo_uart: Pointer to the mapped neo UART struct
 * @ch_cls_uart:  Pointer to the mapped cls UART struct
 * @ch_cached_lsr: Cached value of the LSR register.
 * @ch_rqueue: Read queue buffer, malloc'ed.
 * @ch_r_head: Head location of the read queue.
 * @ch_r_tail: Tail location of the read queue.
 * @ch_equeue: Error queue buffer, malloc'ed.
 * @ch_e_head: Head location of the error queue.
 * @ch_e_tail: Tail location of the error queue.
 * @ch_wqueue: Write queue buffer, malloc'ed.
 * @ch_w_head: Head location of the write queue.
 * @ch_w_tail: Tail location of the write queue.
 * @ch_rxcount: Total of data received so far.
 * @ch_txcount: Total of data transmitted so far.
 * @ch_r_tlevel: Receive trigger level.
 * @ch_t_tlevel: Transmit trigger level.
 * @ch_r_watermark: Receive water mark.
 * @ch_stop_sending_break: Time we should STOP sending a break.
 * @ch_stops_sent: How many times I have send a stop character to try
 *                 to stop the other guy sending.
 * @ch_err_parity: Count of parity
 * @ch_err_frame: Count of framing errors on channel.
 * @ch_err_break: Count of breaks on channel.
 * @ch_err_overrun: Count of overruns on channel.
 * @ch_xon_sends: Count of xons transmitted.
 * @ch_xoff_sends: Count of xoffs transmitted.
 */
struct channel_t {
	struct dgnc_board *ch_bd;
	struct digi_t	ch_digi;
	struct un_t	ch_tun;
	struct un_t	ch_pun;

	spinlock_t	ch_lock; /* provide for serialization */
	wait_queue_head_t ch_flags_wait;

	uint		ch_portnum;
	uint		ch_open_count;
	uint		ch_flags;

	ulong		ch_close_delay;

	ulong		ch_cpstime;

	tcflag_t	ch_c_iflag;
	tcflag_t	ch_c_cflag;
	tcflag_t	ch_c_oflag;
	tcflag_t	ch_c_lflag;
	unsigned char	ch_stopc;
	unsigned char	ch_startc;

	uint		ch_old_baud;
	uint		ch_custom_speed;

	uint		ch_wopen;

	unsigned char	ch_mostat;
	unsigned char	ch_mistat;

	struct neo_uart_struct __iomem *ch_neo_uart;
	struct cls_uart_struct __iomem *ch_cls_uart;

	unsigned char	ch_cached_lsr;

	unsigned char	*ch_rqueue;
	ushort		ch_r_head;
	ushort		ch_r_tail;

	unsigned char	*ch_equeue;
	ushort		ch_e_head;
	ushort		ch_e_tail;

	unsigned char	*ch_wqueue;
	ushort		ch_w_head;
	ushort		ch_w_tail;

	ulong		ch_rxcount;
	ulong		ch_txcount;

	unsigned char	ch_r_tlevel;
	unsigned char	ch_t_tlevel;

	unsigned char	ch_r_watermark;

	ulong		ch_stop_sending_break;
	uint		ch_stops_sent;

	ulong		ch_err_parity;
	ulong		ch_err_frame;
	ulong		ch_err_break;
	ulong		ch_err_overrun;

	ulong		ch_xon_sends;
	ulong		ch_xoff_sends;
};

extern uint		dgnc_major;		/* Our driver/mgmt major */
extern int		dgnc_poll_tick;		/* Poll interval - 20 ms */
extern spinlock_t	dgnc_global_lock;	/* Driver global spinlock */
extern spinlock_t	dgnc_poll_lock;		/* Poll scheduling lock */
extern uint		dgnc_num_boards;	/* Total number of boards */
extern struct dgnc_board *dgnc_board[MAXBOARDS];/* Array of boards */

#endif	/* _DGNC_DRIVER_H */
