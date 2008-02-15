/*
 *      linux/drivers/char/specialix_io8.h  -- 
 *                                   Specialix IO8+ multiport serial driver.
 *
 *      Copyright (C) 1997 Roger Wolff (R.E.Wolff@BitWizard.nl)
 *      Copyright (C) 1994-1996  Dmitry Gorodchanin (pgmdsg@ibi.com)
 *
 *
 *      Specialix pays for the development and support of this driver.
 *      Please DO contact io8-linux@specialix.co.uk if you require
 *      support.
 *
 *      This driver was developped in the BitWizard linux device
 *      driver service. If you require a linux device driver for your
 *      product, please contact devices@BitWizard.nl for a quote.
 *
 *      This code is firmly based on the riscom/8 serial driver,
 *      written by Dmitry Gorodchanin. The specialix IO8+ card
 *      programming information was obtained from the CL-CD1865 Data
 *      Book, and Specialix document number 6200059: IO8+ Hardware
 *      Functional Specification.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation; either version 2 of
 *      the License, or (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be
 *      useful, but WITHOUT ANY WARRANTY; without even the implied
 *      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *      PURPOSE.  See the GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public
 *      License along with this program; if not, write to the Free
 *      Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *      USA.
 * */

#ifndef __LINUX_SPECIALIX_H
#define __LINUX_SPECIALIX_H

#include <linux/serial.h>

#ifdef __KERNEL__

/* You can have max 4 ISA cards in one PC, and I recommend not much 
more than a few  PCI versions of the card. */

#define SX_NBOARD		8

/* NOTE: Specialix decoder recognizes 4 addresses, but only two are used.... */
#define SX_IO_SPACE             4
/* The PCI version decodes 8 addresses, but still only 2 are used. */
#define SX_PCI_IO_SPACE         8

/* eight ports per board. */
#define SX_NPORT        	8
#define SX_BOARD(line)		((line) / SX_NPORT)
#define SX_PORT(line)		((line) & (SX_NPORT - 1))


#define SX_DATA_REG 0     /* Base+0 : Data register */
#define SX_ADDR_REG 1     /* base+1 : Address register. */

#define MHz *1000000	/* I'm ashamed of myself. */

/* On-board oscillator frequency */
#define SX_OSCFREQ      (25 MHz/2)
/* There is a 25MHz crystal on the board, but the chip is in /2 mode */


/* Ticks per sec. Used for setting receiver timeout and break length */
#define SPECIALIX_TPS		4000

/* Yeah, after heavy testing I decided it must be 6.
 * Sure, You can change it if needed.
 */
#define SPECIALIX_RXFIFO	6	/* Max. receiver FIFO size (1-8) */

#define SPECIALIX_MAGIC		0x0907

#define SX_CCR_TIMEOUT 10000   /* CCR timeout. You may need to wait upto
                                  10 milliseconds before the internal
                                  processor is available again after
                                  you give it a command */

#define SX_IOBASE1	0x100
#define SX_IOBASE2	0x180
#define SX_IOBASE3	0x250
#define SX_IOBASE4	0x260

struct specialix_board {
	unsigned long   flags;
	unsigned short	base;
	unsigned char 	irq;
	//signed   char	count;
	int count;
	unsigned char	DTR;
        int reg;
	spinlock_t lock;
};

#define SX_BOARD_PRESENT	0x00000001
#define SX_BOARD_ACTIVE		0x00000002
#define SX_BOARD_IS_PCI		0x00000004


struct specialix_port {
	int			magic;
	int			baud_base;
	int			flags;
	struct tty_struct 	* tty;
	int			count;
	int			blocked_open;
	int			timeout;
	int			close_delay;
	unsigned char 		* xmit_buf;
	int			custom_divisor;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
	short			wakeup_chars;
	short			break_length;
	unsigned short		closing_wait;
	unsigned char		mark_mask;
	unsigned char		IER;
	unsigned char		MSVR;
	unsigned char		COR2;
	unsigned long		overrun;
	unsigned long		hits[10];
	spinlock_t lock;
};

#endif /* __KERNEL__ */
#endif /* __LINUX_SPECIALIX_H */









