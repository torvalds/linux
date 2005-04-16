/*
 *      linux/drivers/char/riscom8.h  -- RISCom/8 multiport serial driver.
 *
 *      Copyright (C) 1994-1996  Dmitry Gorodchanin (pgmdsg@ibi.com)
 *
 *      This code is loosely based on the Linux serial driver, written by
 *      Linus Torvalds, Theodore T'so and others. The RISCom/8 card 
 *      programming info was obtained from various drivers for other OSes 
 *	(FreeBSD, ISC, etc), but no source code from those drivers were 
 *	directly included in this driver.
 *
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_RISCOM8_H
#define __LINUX_RISCOM8_H

#include <linux/serial.h>

#ifdef __KERNEL__

#define RC_NBOARD		4
/* NOTE: RISCom decoder recognizes 16 addresses... */
#define RC_NPORT        	8  
#define RC_BOARD(line)		(((line) >> 3) & 0x07)
#define RC_PORT(line)		((line) & (RC_NPORT - 1))

/* Ticks per sec. Used for setting receiver timeout and break length */
#define RISCOM_TPS		4000

/* Yeah, after heavy testing I decided it must be 6.
 * Sure, You can change it if needed.
 */
#define RISCOM_RXFIFO		6	/* Max. receiver FIFO size (1-8) */

#define RISCOM8_MAGIC		0x0907

#define RC_IOBASE1	0x220
#define RC_IOBASE2	0x240
#define RC_IOBASE3	0x250
#define RC_IOBASE4	0x260

struct riscom_board {
	unsigned long   flags;
	unsigned short	base;
	unsigned char 	irq;
	signed   char	count;
	unsigned char	DTR;
};

#define RC_BOARD_PRESENT	0x00000001
#define RC_BOARD_ACTIVE		0x00000002
	
struct riscom_port {
	int			magic;
	int			baud_base;
	int			flags;
	struct tty_struct 	* tty;
	int			count;
	int			blocked_open;
	long			event; /* long req'd for set_bit --RR */
	int			timeout;
	int			close_delay;
	unsigned char 		* xmit_buf;
	int			custom_divisor;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
	struct work_struct	tqueue;
	struct work_struct	tqueue_hangup;
	short			wakeup_chars;
	short			break_length;
	unsigned short		closing_wait;
	unsigned char		mark_mask;
	unsigned char		IER;
	unsigned char		MSVR;
	unsigned char		COR2;
#ifdef RC_REPORT_OVERRUN
	unsigned long		overrun;
#endif	
#ifdef RC_REPORT_FIFO
	unsigned long		hits[10];
#endif
};

#endif /* __KERNEL__ */
#endif /* __LINUX_RISCOM8_H */
