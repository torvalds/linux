/*
 * mcfserial.c -- serial driver for ColdFire internal UARTS.
 *
 * Copyright (c) 1999 Greg Ungerer <gerg@snapgear.com>
 * Copyright (c) 2000-2001 Lineo, Inc. <www.lineo.com>
 * Copyright (c) 2002 SnapGear Inc., <www.snapgear.com>
 *
 * Based on code from 68332serial.c which was:
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 TSHG
 * Copyright (c) 1999 Rt-Control Inc. <jeff@uclinux.org>
 */ 
#ifndef _MCF_SERIAL_H
#define _MCF_SERIAL_H

#include <linux/config.h>
#include <linux/serial.h>

#ifdef __KERNEL__

/*
 *	Define a local serial stats structure.
 */

struct mcf_stats {
	unsigned int	rx;
	unsigned int	tx;
	unsigned int	rxbreak;
	unsigned int	rxframing;
	unsigned int	rxparity;
	unsigned int	rxoverrun;
};


/*
 * This is our internal structure for each serial port's state.
 * Each serial port has one of these structures associated with it.
 */

struct mcf_serial {
	int			magic;
	volatile unsigned char	*addr;		/* UART memory address */
	int			irq;
	int			flags; 		/* defined in tty.h */
	int			type; 		/* UART type */
	struct tty_struct 	*tty;
	unsigned char		imr;		/* Software imr register */
	unsigned int		baud;
	int			sigs;
	int			custom_divisor;
	int			x_char;	/* xon/xoff character */
	int			baud_base;
	int			close_delay;
	unsigned short		closing_wait;
	unsigned short		closing_wait2;
	unsigned long		event;
	int			line;
	int			count;	    /* # of fd on device */
	int			blocked_open; /* # of blocked opens */
	unsigned char 		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	struct mcf_stats	stats;
	struct work_struct	tqueue;
	struct work_struct	tqueue_hangup;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;

};

#endif /* __KERNEL__ */

#endif /* _MCF_SERIAL_H */
