/* 68328serial.h: Definitions for the mc68328 serial driver.
 *
 * Copyright (C) 1995       David S. Miller    <davem@caip.rutgers.edu>
 * Copyright (C) 1998       Kenneth Albanowski <kjahds@kjahds.com>
 * Copyright (C) 1998, 1999 D. Jeff Dionne     <jeff@uclinux.org>
 * Copyright (C) 1999       Vladimir Gurevich  <vgurevic@cisco.com>
 *
 * VZ Support/Fixes             Evan Stawnyczy <e@lineo.ca>
 */

#ifndef _MC683XX_SERIAL_H
#define _MC683XX_SERIAL_H

/*
 * I believe this is the optimal setting that reduces the number of interrupts.
 * At high speeds the output might become a little "bursted" (use USTCNT_TXHE
 * if that bothers you), but in most cases it will not, since we try to 
 * transmit characters every time rs_interrupt is called. Thus, quite often
 * you'll see that a receive interrupt occures before the transmit one.
 *                                  -- Vladimir Gurevich
 */
#define USTCNT_TX_INTR_MASK (USTCNT_TXEE)

/*
 * 68328 and 68EZ328 UARTS are a little bit different. EZ328 has special
 * "Old data interrupt" which occures whenever the data stay in the FIFO
 * longer than 30 bits time. This allows us to use FIFO without compromising
 * latency. '328 does not have this feature and without the real  328-based
 * board I would assume that RXRE is the safest setting.
 *
 * For EZ328 I use RXHE (Half empty) interrupt to reduce the number of
 * interrupts. RXFE (receive queue full) causes the system to lose data
 * at least at 115200 baud
 *
 * If your board is busy doing other stuff, you might consider to use
 * RXRE (data ready intrrupt) instead.
 *
 * The other option is to make these INTR masks run-time configurable, so
 * that people can dynamically adapt them according to the current usage.
 *                                  -- Vladimir Gurevich
 */

/* (es) */
#if defined(CONFIG_M68EZ328) || defined(CONFIG_M68VZ328)
#define USTCNT_RX_INTR_MASK (USTCNT_RXHE | USTCNT_ODEN)
#elif defined(CONFIG_M68328)
#define USTCNT_RX_INTR_MASK (USTCNT_RXRE)
#else
#error Please, define the Rx interrupt events for your CPU
#endif
/* (/es) */

/*
 * This is our internal structure for each serial port's state.
 * 
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

struct m68k_serial {
	char is_cons;       /* Is this our console. */
	int			magic;
	int			baud_base;
	int			port;
	int			irq;
	int			flags; 		/* defined in tty.h */
	int			type; 		/* UART type */
	struct tty_struct 	*tty;
	int			custom_divisor;
	int			x_char;	/* xon/xoff character */
	int			close_delay;
	unsigned short		closing_wait;
	int			line;
	int			count;	    /* # of fd on device */
	int			blocked_open; /* # of blocked opens */
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
};

#define SERIAL_MAGIC 0x5301

/* 
 * Define the number of ports supported and their irqs.
 */
#define NR_PORTS 1
#define UART_IRQ_DEFNS {UART_IRQ_NUM}

#endif /* !(_MC683XX_SERIAL_H) */
