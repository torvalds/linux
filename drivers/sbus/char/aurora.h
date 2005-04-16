/*	$Id: aurora.h,v 1.6 2001/06/05 12:23:38 davem Exp $
 *	linux/drivers/sbus/char/aurora.h -- Aurora multiport driver
 *
 *	Copyright (c) 1999 by Oliver Aldulea (oli@bv.ro)
 *
 *	This code is based on the RISCom/8 multiport serial driver written
 *	by Dmitry Gorodchanin (pgmdsg@ibi.com), based on the Linux serial
 *	driver, written by Linus Torvalds, Theodore T'so and others.
 *	The Aurora multiport programming info was obtained mainly from the
 *	Cirrus Logic CD180 documentation (available on the web), and by
 *	doing heavy tests on the board. Many thanks to Eddie C. Dost for the
 *	help on the sbus interface.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	Revision 1.0
 *
 *	This is the first public release.
 *
 *	This version needs a lot of feedback. This is the version that works
 *	with _my_ board. My board is model 1600se, revision '@(#)1600se.fth
 *	1.2 3/28/95 1'. The driver might work with your board, but I do not
 *	guarantee it. If you have _any_ type of board, I need to know if the
 *	driver works or not, I need to know exactly your board parameters
 *	(get them with 'cd /proc/openprom/iommu/sbus/sio16/; ls *; cat *')
 *	Also, I need your board revision code, which is written on the board.
 *	Send me the output of my driver too (it outputs through klogd).
 *
 *	If the driver does not work, you can try enabling the debug options
 *	to see what's wrong or what should be done.
 *
 *	I'm sorry about the alignment of the code. It was written in a
 *	128x48 environment.
 *
 *	I must say that I do not like Aurora Technologies' policy. I asked
 *	them to help me do this driver faster, but they ended by something
 *	like "don't call us, we'll call you", and I never heard anything
 *	from them. They told me "knowing the way the board works, I don't
 *	doubt you and others on the net will make the driver."
 *	The truth about this board is that it has nothing intelligent on it.
 *	If you want to say to somebody what kind of board you have, say that
 *	it uses Cirrus Logic processors (CD180). The power of the board is
 *	in those two chips. The rest of the board is the interface to the
 *	sbus and to the peripherals. Still, they did something smart: they
 *	reversed DTR and RTS to make on-board automatic hardware flow
 *	control usable.
 *	Thanks to Aurora Technologies for wasting my time, nerves and money.
 */

#ifndef __LINUX_AURORA_H
#define __LINUX_AURORA_H

#include <linux/serial.h>
#include <linux/serialP.h>

#ifdef __KERNEL__

/* This is the number of boards to support. I've only tested this driver with
 * one board, so it might not work.
 */
#define AURORA_NBOARD 1

/* Useful ? Yes. But you can safely comment the warnings if they annoy you
 * (let me say that again: the warnings in the code, not this define). 
 */
#define AURORA_PARANOIA_CHECK

/* Well, after many lost nights, I found that the IRQ for this board is
 * selected from four built-in values by writing some bits in the
 * configuration register. This causes a little problem to occur: which
 * IRQ to select ? Which one is the best for the user ? Well, I finally
 * decided for the following algorithm: if the "bintr" value is not acceptable
 * (not within type_1_irq[], then test the "intr" value, if that fails too,
 * try each value from type_1_irq until succeded. Hope it's ok.
 * You can safely reorder the irq's.
 */
#define TYPE_1_IRQS 4
unsigned char type_1_irq[TYPE_1_IRQS] = {
	3, 5, 9, 13
};
/* I know something about another method of interrupt setting, but not enough.
 * Also, this is for another type of board, so I first have to learn how to
 * detect it.
#define TYPE_2_IRQS 3
unsigned char type_2_irq[TYPE_2_IRQS] = {
	0, 0, 0 ** could anyone find these for me ? (see AURORA_ALLIRQ below) **
	};
unsigned char type_2_mask[TYPE_2_IRQS] = {
	32, 64, 128
	};
*/

/* The following section should only be modified by those who know what
 * they're doing (or don't, but want to help with some feedback). Modifying
 * anything raises a _big_ probability for your system to hang, but the
 * sacrifice worths. (I sacrificed my ext2fs many, many times...)
 */

/* This one tries to dump to console the name of almost every function called,
 * and many other debugging info.
 */
#undef AURORA_DEBUG

/* These are the most dangerous and useful defines. They do printk() during
 * the interrupt processing routine(s), so if you manage to get "flooded" by
 * irq's, start thinking about the "Power off/on" button...
 */
#undef AURORA_INTNORM	/* This one enables the "normal" messages, but some
			 * of them cause flood, so I preffered putting
			 * them under a define */
#undef AURORA_INT_DEBUG /* This one is really bad. */

/* Here's something helpful: after n irq's, the board will be disabled. This
 * prevents irq flooding during debug (no need to think about power
 * off/on anymore...)
 */
#define AURORA_FLOODPRO	10

/* This one helps finding which irq the board calls, in case of a strange/
 * unsupported board. AURORA_INT_DEBUG should be enabled, because I don't
 * think /proc/interrupts or any command will be available in case of an irq
 * flood... "allirq" is the list of all free irq's.
 */
/*
#define AURORA_ALLIRQ 6
int allirq[AURORA_ALLIRQ]={
	2,3,5,7,9,13
	};
*/

/* These must not be modified. These values are assumed during the code for
 * performance optimisations.
 */
#define AURORA_NCD180 2 /* two chips per board */
#define AURORA_NPORT 8  /* 8 ports per chip */

/* several utilities */
#define AURORA_BOARD(line)	(((line) >> 4) & 0x01)
#define AURORA_CD180(line)	(((line) >> 3) & 0x01)
#define AURORA_PORT(line)	((line) & 15)

#define AURORA_TNPORTS (AURORA_NBOARD*AURORA_NCD180*AURORA_NPORT)

/* Ticks per sec. Used for setting receiver timeout and break length */
#define AURORA_TPS		4000

#define AURORA_MAGIC	0x0A18

/* Yeah, after heavy testing I decided it must be 6.
 * Sure, You can change it if needed.
 */
#define AURORA_RXFIFO		6	/* Max. receiver FIFO size (1-8) */

#define AURORA_RXTH		7

struct aurora_reg1 {
	__volatile__ unsigned char r;
};

struct aurora_reg128 {
	__volatile__ unsigned char r[128];
};
	
struct aurora_reg4 {
	__volatile__ unsigned char r[4];
};

struct Aurora_board {
	unsigned long		flags;
	struct aurora_reg1	* r0;	/* This is the board configuration
					 * register (write-only). */
	struct aurora_reg128	* r[2];	/* These are the registers for the
					 * two chips. */
	struct aurora_reg4	* r3;	/* These are used for hardware-based
					 * acknowledge. Software-based ack is
					 * not supported by CD180. */
	unsigned int		oscfreq; /* The on-board oscillator
					  * frequency, in Hz. */
	unsigned char		irq;
#ifdef MODULE
	signed char		count;	/* counts the use of the board */
#endif
	/* Values for the dtr_rts swapped mode. */
	unsigned char		DTR;
	unsigned char		RTS;
	unsigned char		MSVDTR;
	unsigned char		MSVRTS;
	/* Values for hardware acknowledge. */
	unsigned char		ACK_MINT, ACK_TINT, ACK_RINT;
};

/* Board configuration register */
#define AURORA_CFG_ENABLE_IO	8
#define AURORA_CFG_ENABLE_IRQ	4

/* Board flags */
#define AURORA_BOARD_PRESENT		0x00000001
#define AURORA_BOARD_ACTIVE		0x00000002
#define AURORA_BOARD_TYPE_2		0x00000004	/* don't know how to
							 * detect this yet */
#define AURORA_BOARD_DTR_FLOW_OK	0x00000008

/* The story goes like this: Cirrus programmed the CD-180 chip to do automatic
 * hardware flow control, and do it using CTS and DTR. CTS is ok, but, if you
 * have a modem and the chip drops DTR, then the modem will drop the carrier
 * (ain't that cute...). Luckily, the guys at Aurora decided to swap DTR and
 * RTS, which makes the flow control usable. I hope that all the boards made
 * by Aurora have these two signals swapped. If your's doesn't but you have a
 * breakout box, you can try to reverse them yourself, then set the following
 * flag.
 */
#undef AURORA_FORCE_DTR_FLOW

/* In fact, a few more words have to be said about hardware flow control.
 * This driver handles "output" flow control through the on-board facility
 * CTS Auto Enable. For the "input" flow control there are two cases when
 * the flow should be controlled. The first case is when the kernel is so
 * busy that it cannot process IRQ's in time; this flow control can only be
 * activated by the on-board chip, and if the board has RTS and DTR swapped,
 * this facility is usable. The second case is when the application is so
 * busy that it cannot receive bytes from the kernel, and this flow must be
 * activated by software. This second case is not yet implemented in this
 * driver. Unfortunately, I estimate that the second case is the one that
 * occurs the most.
 */


struct Aurora_port {
	int			magic;
	int			baud_base;
	int			flags;
	struct tty_struct 	* tty;
	int			count;
	int			blocked_open;
	long			event;
	int			timeout;
	int			close_delay;
	unsigned char 		* xmit_buf;
	int			custom_divisor;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
	struct tq_struct	tqueue;
	struct tq_struct	tqueue_hangup;
	short			wakeup_chars;
	short			break_length;
	unsigned short		closing_wait;
	unsigned char		mark_mask;
	unsigned char		SRER;
	unsigned char		MSVR;
	unsigned char		COR2;
#ifdef AURORA_REPORT_OVERRUN
	unsigned long		overrun;
#endif	
#ifdef AURORA_REPORT_FIFO
	unsigned long		hits[10];
#endif
};

#endif
#endif /*__LINUX_AURORA_H*/

