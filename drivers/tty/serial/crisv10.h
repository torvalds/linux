/*
 * serial.h: Arch-dep definitions for the Etrax100 serial driver.
 *
 * Copyright (C) 1998-2007 Axis Communications AB
 */

#ifndef _ETRAX_SERIAL_H
#define _ETRAX_SERIAL_H

#include <linux/circ_buf.h>
#include <asm/termios.h>
#include <asm/dma.h>
#include <arch/io_interface_mux.h>

/* Software state per channel */

#ifdef __KERNEL__
/*
 * This is our internal structure for each serial port's state.
 *
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

#define SERIAL_RECV_DESCRIPTORS 8

struct etrax_recv_buffer {
	struct etrax_recv_buffer *next;
	unsigned short length;
	unsigned char error;
	unsigned char pad;

	unsigned char buffer[0];
};

struct e100_serial {
	struct tty_port port;
	int baud;
	volatile u8	*ioport;	/* R_SERIALx_CTRL */
	u32		irq;	/* bitnr in R_IRQ_MASK2 for dmaX_descr */

	/* Output registers */
	volatile u8 *oclrintradr;	/* adr to R_DMA_CHx_CLR_INTR */
	volatile u32 *ofirstadr;	/* adr to R_DMA_CHx_FIRST */
	volatile u8 *ocmdadr;		/* adr to R_DMA_CHx_CMD */
	const volatile u8 *ostatusadr;	/* adr to R_DMA_CHx_STATUS */

	/* Input registers */
	volatile u8 *iclrintradr;	/* adr to R_DMA_CHx_CLR_INTR */
	volatile u32 *ifirstadr;	/* adr to R_DMA_CHx_FIRST */
	volatile u8 *icmdadr;		/* adr to R_DMA_CHx_CMD */
	volatile u32 *idescradr;	/* adr to R_DMA_CHx_DESCR */

	u8 rx_ctrl;	/* shadow for R_SERIALx_REC_CTRL */
	u8 tx_ctrl;	/* shadow for R_SERIALx_TR_CTRL */
	u8 iseteop;	/* bit number for R_SET_EOP for the input dma */
	int enabled;	/* Set to 1 if the port is enabled in HW config */

	u8 dma_out_enabled;	/* Set to 1 if DMA should be used */
	u8 dma_in_enabled;	/* Set to 1 if DMA should be used */

	/* end of fields defined in rs_table[] in .c-file */
	int		dma_owner;
	unsigned int	dma_in_nbr;
	unsigned int	dma_out_nbr;
	unsigned int	dma_in_irq_nbr;
	unsigned int	dma_out_irq_nbr;
	unsigned long	dma_in_irq_flags;
	unsigned long	dma_out_irq_flags;
	char		*dma_in_irq_description;
	char		*dma_out_irq_description;

	enum cris_io_interface io_if;
	char            *io_if_description;

	u8		uses_dma_in;  /* Set to 1 if DMA is used */
	u8		uses_dma_out; /* Set to 1 if DMA is used */
	u8		forced_eop;   /* a fifo eop has been forced */
	int			baud_base;     /* For special baudrates */
	int			custom_divisor; /* For special baudrates */
	struct etrax_dma_descr	tr_descr;
	struct etrax_dma_descr	rec_descr[SERIAL_RECV_DESCRIPTORS];
	int			cur_rec_descr;

	volatile int		tr_running; /* 1 if output is running */

	int			x_char;	/* xon/xoff character */
	unsigned long		event;
	int			line;
	int			type;  /* PORT_ETRAX */
	struct circ_buf		xmit;
	struct etrax_recv_buffer *first_recv_buffer;
	struct etrax_recv_buffer *last_recv_buffer;
	unsigned int		recv_cnt;
	unsigned int		max_recv_cnt;

	struct work_struct	work;
	struct async_icount	icount;   /* error-statistics etc.*/
	struct ktermios		normal_termios;

	unsigned long char_time_usec;       /* The time for 1 char, in usecs */
	unsigned long flush_time_usec;      /* How often we should flush */
	unsigned long last_tx_active_usec;  /* Last tx usec in the jiffies */
	unsigned long last_tx_active;       /* Last tx time in jiffies */
	unsigned long last_rx_active_usec;  /* Last rx usec in the jiffies */
	unsigned long last_rx_active;       /* Last rx time in jiffies */

	int break_detected_cnt;
	int errorcode;

#ifdef CONFIG_ETRAX_RS485
	struct serial_rs485	rs485;  /* RS-485 support */
#endif
};

/* this PORT is not in the standard serial.h. it's not actually used for
 * anything since we only have one type of async serial-port anyway in this
 * system.
 */

#define PORT_ETRAX 1

/*
 * Events are used to schedule things to happen at timer-interrupt
 * time, instead of at rs interrupt time.
 */
#define RS_EVENT_WRITE_WAKEUP	0

#endif /* __KERNEL__ */

#endif /* !_ETRAX_SERIAL_H */
