/*
 * RocketPort device driver for Linux
 *
 * Written by Theodore Ts'o, 1995, 1996, 1997, 1998, 1999, 2000.
 * 
 * Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2003 by Comtrol, Inc.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Kernel Synchronization:
 *
 * This driver has 2 kernel control paths - exception handlers (calls into the driver
 * from user mode) and the timer bottom half (tasklet).  This is a polled driver, interrupts
 * are not used.
 *
 * Critical data: 
 * -  rp_table[], accessed through passed "info" pointers, is a global (static) array of 
 *    serial port state information and the xmit_buf circular buffer.  Protected by 
 *    a per port spinlock.
 * -  xmit_flags[], an array of ints indexed by line (port) number, indicating that there
 *    is data to be transmitted.  Protected by atomic bit operations.
 * -  rp_num_ports, int indicating number of open ports, protected by atomic operations.
 * 
 * rp_write() and rp_write_char() functions use a per port semaphore to protect against
 * simultaneous access to the same port by more than one process.
 */

/****** Defines ******/
#ifdef PCI_NUM_RESOURCES
#define PCI_BASE_ADDRESS(dev, r) ((dev)->resource[r].start)
#else
#define PCI_BASE_ADDRESS(dev, r) ((dev)->base_address[r])
#endif

#define ROCKET_PARANOIA_CHECK
#define ROCKET_DISABLE_SIMUSAGE

#undef ROCKET_SOFT_FLOW
#undef ROCKET_DEBUG_OPEN
#undef ROCKET_DEBUG_INTR
#undef ROCKET_DEBUG_WRITE
#undef ROCKET_DEBUG_FLOW
#undef ROCKET_DEBUG_THROTTLE
#undef ROCKET_DEBUG_WAIT_UNTIL_SENT
#undef ROCKET_DEBUG_RECEIVE
#undef ROCKET_DEBUG_HANGUP
#undef REV_PCI_ORDER
#undef ROCKET_DEBUG_IO

#define POLL_PERIOD HZ/100	/*  Polling period .01 seconds (10ms) */

/****** Kernel includes ******/

#ifdef MODVERSIONS
#include <config/modversions.h>
#endif				

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <linux/init.h>

/****** RocketPort includes ******/

#include "rocket_int.h"
#include "rocket.h"

#define ROCKET_VERSION "2.09"
#define ROCKET_DATE "12-June-2003"

/****** RocketPort Local Variables ******/

static struct tty_driver *rocket_driver;

static struct rocket_version driver_version = {	
	ROCKET_VERSION, ROCKET_DATE
};

static struct r_port *rp_table[MAX_RP_PORTS];	       /*  The main repository of serial port state information. */
static unsigned int xmit_flags[NUM_BOARDS];	       /*  Bit significant, indicates port had data to transmit. */
						       /*  eg.  Bit 0 indicates port 0 has xmit data, ...        */
static atomic_t rp_num_ports_open;	               /*  Number of serial ports open                           */
static struct timer_list rocket_timer;

static unsigned long board1;	                       /* ISA addresses, retrieved from rocketport.conf          */
static unsigned long board2;
static unsigned long board3;
static unsigned long board4;
static unsigned long controller;
static int support_low_speed;
static unsigned long modem1;
static unsigned long modem2;
static unsigned long modem3;
static unsigned long modem4;
static unsigned long pc104_1[8];
static unsigned long pc104_2[8];
static unsigned long pc104_3[8];
static unsigned long pc104_4[8];
static unsigned long *pc104[4] = { pc104_1, pc104_2, pc104_3, pc104_4 };

static int rp_baud_base[NUM_BOARDS];	               /*  Board config info (Someday make a per-board structure)  */
static unsigned long rcktpt_io_addr[NUM_BOARDS];
static int rcktpt_type[NUM_BOARDS];
static int is_PCI[NUM_BOARDS];
static rocketModel_t rocketModel[NUM_BOARDS];
static int max_board;

/*
 * The following arrays define the interrupt bits corresponding to each AIOP.
 * These bits are different between the ISA and regular PCI boards and the
 * Universal PCI boards.
 */

static Word_t aiop_intr_bits[AIOP_CTL_SIZE] = {
	AIOP_INTR_BIT_0,
	AIOP_INTR_BIT_1,
	AIOP_INTR_BIT_2,
	AIOP_INTR_BIT_3
};

static Word_t upci_aiop_intr_bits[AIOP_CTL_SIZE] = {
	UPCI_AIOP_INTR_BIT_0,
	UPCI_AIOP_INTR_BIT_1,
	UPCI_AIOP_INTR_BIT_2,
	UPCI_AIOP_INTR_BIT_3
};

static Byte_t RData[RDATASIZE] = {
	0x00, 0x09, 0xf6, 0x82,
	0x02, 0x09, 0x86, 0xfb,
	0x04, 0x09, 0x00, 0x0a,
	0x06, 0x09, 0x01, 0x0a,
	0x08, 0x09, 0x8a, 0x13,
	0x0a, 0x09, 0xc5, 0x11,
	0x0c, 0x09, 0x86, 0x85,
	0x0e, 0x09, 0x20, 0x0a,
	0x10, 0x09, 0x21, 0x0a,
	0x12, 0x09, 0x41, 0xff,
	0x14, 0x09, 0x82, 0x00,
	0x16, 0x09, 0x82, 0x7b,
	0x18, 0x09, 0x8a, 0x7d,
	0x1a, 0x09, 0x88, 0x81,
	0x1c, 0x09, 0x86, 0x7a,
	0x1e, 0x09, 0x84, 0x81,
	0x20, 0x09, 0x82, 0x7c,
	0x22, 0x09, 0x0a, 0x0a
};

static Byte_t RRegData[RREGDATASIZE] = {
	0x00, 0x09, 0xf6, 0x82,	/* 00: Stop Rx processor */
	0x08, 0x09, 0x8a, 0x13,	/* 04: Tx software flow control */
	0x0a, 0x09, 0xc5, 0x11,	/* 08: XON char */
	0x0c, 0x09, 0x86, 0x85,	/* 0c: XANY */
	0x12, 0x09, 0x41, 0xff,	/* 10: Rx mask char */
	0x14, 0x09, 0x82, 0x00,	/* 14: Compare/Ignore #0 */
	0x16, 0x09, 0x82, 0x7b,	/* 18: Compare #1 */
	0x18, 0x09, 0x8a, 0x7d,	/* 1c: Compare #2 */
	0x1a, 0x09, 0x88, 0x81,	/* 20: Interrupt #1 */
	0x1c, 0x09, 0x86, 0x7a,	/* 24: Ignore/Replace #1 */
	0x1e, 0x09, 0x84, 0x81,	/* 28: Interrupt #2 */
	0x20, 0x09, 0x82, 0x7c,	/* 2c: Ignore/Replace #2 */
	0x22, 0x09, 0x0a, 0x0a	/* 30: Rx FIFO Enable */
};

static CONTROLLER_T sController[CTL_SIZE] = {
	{-1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0},
	 {0, 0, 0, 0}, {-1, -1, -1, -1}, {0, 0, 0, 0}},
	{-1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0},
	 {0, 0, 0, 0}, {-1, -1, -1, -1}, {0, 0, 0, 0}},
	{-1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0},
	 {0, 0, 0, 0}, {-1, -1, -1, -1}, {0, 0, 0, 0}},
	{-1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0, 0, 0},
	 {0, 0, 0, 0}, {-1, -1, -1, -1}, {0, 0, 0, 0}}
};

static Byte_t sBitMapClrTbl[8] = {
	0xfe, 0xfd, 0xfb, 0xf7, 0xef, 0xdf, 0xbf, 0x7f
};

static Byte_t sBitMapSetTbl[8] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
};

static int sClockPrescale = 0x14;

/*
 *  Line number is the ttySIx number (x), the Minor number.  We 
 *  assign them sequentially, starting at zero.  The following 
 *  array keeps track of the line number assigned to a given board/aiop/channel.
 */
static unsigned char lineNumbers[MAX_RP_PORTS];
static unsigned long nextLineNumber;

/*****  RocketPort Static Prototypes   *********/
static int __init init_ISA(int i);
static void rp_wait_until_sent(struct tty_struct *tty, int timeout);
static void rp_flush_buffer(struct tty_struct *tty);
static void rmSpeakerReset(CONTROLLER_T * CtlP, unsigned long model);
static unsigned char GetLineNumber(int ctrl, int aiop, int ch);
static unsigned char SetLineNumber(int ctrl, int aiop, int ch);
static void rp_start(struct tty_struct *tty);
static int sInitChan(CONTROLLER_T * CtlP, CHANNEL_T * ChP, int AiopNum,
		     int ChanNum);
static void sSetInterfaceMode(CHANNEL_T * ChP, Byte_t mode);
static void sFlushRxFIFO(CHANNEL_T * ChP);
static void sFlushTxFIFO(CHANNEL_T * ChP);
static void sEnInterrupts(CHANNEL_T * ChP, Word_t Flags);
static void sDisInterrupts(CHANNEL_T * ChP, Word_t Flags);
static void sModemReset(CONTROLLER_T * CtlP, int chan, int on);
static void sPCIModemReset(CONTROLLER_T * CtlP, int chan, int on);
static int sWriteTxPrioByte(CHANNEL_T * ChP, Byte_t Data);
static int sPCIInitController(CONTROLLER_T * CtlP, int CtlNum,
			      ByteIO_t * AiopIOList, int AiopIOListSize,
			      WordIO_t ConfigIO, int IRQNum, Byte_t Frequency,
			      int PeriodicOnly, int altChanRingIndicator,
			      int UPCIRingInd);
static int sInitController(CONTROLLER_T * CtlP, int CtlNum, ByteIO_t MudbacIO,
			   ByteIO_t * AiopIOList, int AiopIOListSize,
			   int IRQNum, Byte_t Frequency, int PeriodicOnly);
static int sReadAiopID(ByteIO_t io);
static int sReadAiopNumChan(WordIO_t io);

MODULE_AUTHOR("Theodore Ts'o");
MODULE_DESCRIPTION("Comtrol RocketPort driver");
module_param(board1, ulong, 0);
MODULE_PARM_DESC(board1, "I/O port for (ISA) board #1");
module_param(board2, ulong, 0);
MODULE_PARM_DESC(board2, "I/O port for (ISA) board #2");
module_param(board3, ulong, 0);
MODULE_PARM_DESC(board3, "I/O port for (ISA) board #3");
module_param(board4, ulong, 0);
MODULE_PARM_DESC(board4, "I/O port for (ISA) board #4");
module_param(controller, ulong, 0);
MODULE_PARM_DESC(controller, "I/O port for (ISA) rocketport controller");
module_param(support_low_speed, bool, 0);
MODULE_PARM_DESC(support_low_speed, "1 means support 50 baud, 0 means support 460400 baud");
module_param(modem1, ulong, 0);
MODULE_PARM_DESC(modem1, "1 means (ISA) board #1 is a RocketModem");
module_param(modem2, ulong, 0);
MODULE_PARM_DESC(modem2, "1 means (ISA) board #2 is a RocketModem");
module_param(modem3, ulong, 0);
MODULE_PARM_DESC(modem3, "1 means (ISA) board #3 is a RocketModem");
module_param(modem4, ulong, 0);
MODULE_PARM_DESC(modem4, "1 means (ISA) board #4 is a RocketModem");
module_param_array(pc104_1, ulong, NULL, 0);
MODULE_PARM_DESC(pc104_1, "set interface types for ISA(PC104) board #1 (e.g. pc104_1=232,232,485,485,...");
module_param_array(pc104_2, ulong, NULL, 0);
MODULE_PARM_DESC(pc104_2, "set interface types for ISA(PC104) board #2 (e.g. pc104_2=232,232,485,485,...");
module_param_array(pc104_3, ulong, NULL, 0);
MODULE_PARM_DESC(pc104_3, "set interface types for ISA(PC104) board #3 (e.g. pc104_3=232,232,485,485,...");
module_param_array(pc104_4, ulong, NULL, 0);
MODULE_PARM_DESC(pc104_4, "set interface types for ISA(PC104) board #4 (e.g. pc104_4=232,232,485,485,...");

static int rp_init(void);
static void rp_cleanup_module(void);

module_init(rp_init);
module_exit(rp_cleanup_module);


MODULE_LICENSE("Dual BSD/GPL");

/*************************************************************************/
/*                     Module code starts here                           */

static inline int rocket_paranoia_check(struct r_port *info,
					const char *routine)
{
#ifdef ROCKET_PARANOIA_CHECK
	if (!info)
		return 1;
	if (info->magic != RPORT_MAGIC) {
		printk(KERN_INFO "Warning: bad magic number for rocketport struct in %s\n",
		     routine);
		return 1;
	}
#endif
	return 0;
}


/*  Serial port receive data function.  Called (from timer poll) when an AIOPIC signals 
 *  that receive data is present on a serial port.  Pulls data from FIFO, moves it into the 
 *  tty layer.  
 */
static void rp_do_receive(struct r_port *info,
			  struct tty_struct *tty,
			  CHANNEL_t * cp, unsigned int ChanStatus)
{
	unsigned int CharNStat;
	int ToRecv, wRecv, space = 0, count;
	unsigned char *cbuf;
	char *fbuf;
	struct tty_ldisc *ld;

	ld = tty_ldisc_ref(tty);

	ToRecv = sGetRxCnt(cp);
	if (ld)
		space = ld->receive_room(tty);
	if (space > 2 * TTY_FLIPBUF_SIZE)
		space = 2 * TTY_FLIPBUF_SIZE;
	cbuf = tty->flip.char_buf;
	fbuf = tty->flip.flag_buf;
	count = 0;
#ifdef ROCKET_DEBUG_INTR
	printk(KERN_INFO "rp_do_receive(%d, %d)...", ToRecv, space);
#endif

	/*
	 * determine how many we can actually read in.  If we can't
	 * read any in then we have a software overrun condition.
	 */
	if (ToRecv > space)
		ToRecv = space;

	if (ToRecv <= 0)
		goto done;

	/*
	 * if status indicates there are errored characters in the
	 * FIFO, then enter status mode (a word in FIFO holds
	 * character and status).
	 */
	if (ChanStatus & (RXFOVERFL | RXBREAK | RXFRAME | RXPARITY)) {
		if (!(ChanStatus & STATMODE)) {
#ifdef ROCKET_DEBUG_RECEIVE
			printk(KERN_INFO "Entering STATMODE...");
#endif
			ChanStatus |= STATMODE;
			sEnRxStatusMode(cp);
		}
	}

	/* 
	 * if we previously entered status mode, then read down the
	 * FIFO one word at a time, pulling apart the character and
	 * the status.  Update error counters depending on status
	 */
	if (ChanStatus & STATMODE) {
#ifdef ROCKET_DEBUG_RECEIVE
		printk(KERN_INFO "Ignore %x, read %x...", info->ignore_status_mask,
		       info->read_status_mask);
#endif
		while (ToRecv) {
			CharNStat = sInW(sGetTxRxDataIO(cp));
#ifdef ROCKET_DEBUG_RECEIVE
			printk(KERN_INFO "%x...", CharNStat);
#endif
			if (CharNStat & STMBREAKH)
				CharNStat &= ~(STMFRAMEH | STMPARITYH);
			if (CharNStat & info->ignore_status_mask) {
				ToRecv--;
				continue;
			}
			CharNStat &= info->read_status_mask;
			if (CharNStat & STMBREAKH)
				*fbuf++ = TTY_BREAK;
			else if (CharNStat & STMPARITYH)
				*fbuf++ = TTY_PARITY;
			else if (CharNStat & STMFRAMEH)
				*fbuf++ = TTY_FRAME;
			else if (CharNStat & STMRCVROVRH)
				*fbuf++ = TTY_OVERRUN;
			else
				*fbuf++ = 0;
			*cbuf++ = CharNStat & 0xff;
			count++;
			ToRecv--;
		}

		/*
		 * after we've emptied the FIFO in status mode, turn
		 * status mode back off
		 */
		if (sGetRxCnt(cp) == 0) {
#ifdef ROCKET_DEBUG_RECEIVE
			printk(KERN_INFO "Status mode off.\n");
#endif
			sDisRxStatusMode(cp);
		}
	} else {
		/*
		 * we aren't in status mode, so read down the FIFO two
		 * characters at time by doing repeated word IO
		 * transfer.
		 */
		wRecv = ToRecv >> 1;
		if (wRecv)
			sInStrW(sGetTxRxDataIO(cp), (unsigned short *) cbuf, wRecv);
		if (ToRecv & 1)
			cbuf[ToRecv - 1] = sInB(sGetTxRxDataIO(cp));
		memset(fbuf, 0, ToRecv);
		cbuf += ToRecv;
		fbuf += ToRecv;
		count += ToRecv;
	}
	/*  Push the data up to the tty layer */
	ld->receive_buf(tty, tty->flip.char_buf, tty->flip.flag_buf, count);
done:
	tty_ldisc_deref(ld);
}

/*
 *  Serial port transmit data function.  Called from the timer polling loop as a 
 *  result of a bit set in xmit_flags[], indicating data (from the tty layer) is ready
 *  to be sent out the serial port.  Data is buffered in rp_table[line].xmit_buf, it is 
 *  moved to the port's xmit FIFO.  *info is critical data, protected by spinlocks.
 */
static void rp_do_transmit(struct r_port *info)
{
	int c;
	CHANNEL_t *cp = &info->channel;
	struct tty_struct *tty;
	unsigned long flags;

#ifdef ROCKET_DEBUG_INTR
	printk(KERN_INFO "rp_do_transmit ");
#endif
	if (!info)
		return;
	if (!info->tty) {
		printk(KERN_INFO  "rp: WARNING rp_do_transmit called with info->tty==NULL\n");
		clear_bit((info->aiop * 8) + info->chan, (void *) &xmit_flags[info->board]);
		return;
	}

	spin_lock_irqsave(&info->slock, flags);
	tty = info->tty;
	info->xmit_fifo_room = TXFIFO_SIZE - sGetTxCnt(cp);

	/*  Loop sending data to FIFO until done or FIFO full */
	while (1) {
		if (tty->stopped || tty->hw_stopped)
			break;
		c = min(info->xmit_fifo_room, min(info->xmit_cnt, XMIT_BUF_SIZE - info->xmit_tail));
		if (c <= 0 || info->xmit_fifo_room <= 0)
			break;
		sOutStrW(sGetTxRxDataIO(cp), (unsigned short *) (info->xmit_buf + info->xmit_tail), c / 2);
		if (c & 1)
			sOutB(sGetTxRxDataIO(cp), info->xmit_buf[info->xmit_tail + c - 1]);
		info->xmit_tail += c;
		info->xmit_tail &= XMIT_BUF_SIZE - 1;
		info->xmit_cnt -= c;
		info->xmit_fifo_room -= c;
#ifdef ROCKET_DEBUG_INTR
		printk(KERN_INFO "tx %d chars...", c);
#endif
	}

	if (info->xmit_cnt == 0)
		clear_bit((info->aiop * 8) + info->chan, (void *) &xmit_flags[info->board]);

	if (info->xmit_cnt < WAKEUP_CHARS) {
		tty_wakeup(tty);
		wake_up_interruptible(&tty->write_wait);
#ifdef ROCKETPORT_HAVE_POLL_WAIT
		wake_up_interruptible(&tty->poll_wait);
#endif
	}

	spin_unlock_irqrestore(&info->slock, flags);

#ifdef ROCKET_DEBUG_INTR
	printk(KERN_INFO "(%d,%d,%d,%d)...", info->xmit_cnt, info->xmit_head,
	       info->xmit_tail, info->xmit_fifo_room);
#endif
}

/*
 *  Called when a serial port signals it has read data in it's RX FIFO.
 *  It checks what interrupts are pending and services them, including
 *  receiving serial data.  
 */
static void rp_handle_port(struct r_port *info)
{
	CHANNEL_t *cp;
	struct tty_struct *tty;
	unsigned int IntMask, ChanStatus;

	if (!info)
		return;

	if ((info->flags & ROCKET_INITIALIZED) == 0) {
		printk(KERN_INFO "rp: WARNING: rp_handle_port called with info->flags & NOT_INIT\n");
		return;
	}
	if (!info->tty) {
		printk(KERN_INFO "rp: WARNING: rp_handle_port called with info->tty==NULL\n");
		return;
	}
	cp = &info->channel;
	tty = info->tty;

	IntMask = sGetChanIntID(cp) & info->intmask;
#ifdef ROCKET_DEBUG_INTR
	printk(KERN_INFO "rp_interrupt %02x...", IntMask);
#endif
	ChanStatus = sGetChanStatus(cp);
	if (IntMask & RXF_TRIG) {	/* Rx FIFO trigger level */
		rp_do_receive(info, tty, cp, ChanStatus);
	}
	if (IntMask & DELTA_CD) {	/* CD change  */
#if (defined(ROCKET_DEBUG_OPEN) || defined(ROCKET_DEBUG_INTR) || defined(ROCKET_DEBUG_HANGUP))
		printk(KERN_INFO "ttyR%d CD now %s...", info->line,
		       (ChanStatus & CD_ACT) ? "on" : "off");
#endif
		if (!(ChanStatus & CD_ACT) && info->cd_status) {
#ifdef ROCKET_DEBUG_HANGUP
			printk(KERN_INFO "CD drop, calling hangup.\n");
#endif
			tty_hangup(tty);
		}
		info->cd_status = (ChanStatus & CD_ACT) ? 1 : 0;
		wake_up_interruptible(&info->open_wait);
	}
#ifdef ROCKET_DEBUG_INTR
	if (IntMask & DELTA_CTS) {	/* CTS change */
		printk(KERN_INFO "CTS change...\n");
	}
	if (IntMask & DELTA_DSR) {	/* DSR change */
		printk(KERN_INFO "DSR change...\n");
	}
#endif
}

/*
 *  The top level polling routine.  Repeats every 1/100 HZ (10ms).
 */
static void rp_do_poll(unsigned long dummy)
{
	CONTROLLER_t *ctlp;
	int ctrl, aiop, ch, line, i;
	unsigned int xmitmask;
	unsigned int CtlMask;
	unsigned char AiopMask;
	Word_t bit;

	/*  Walk through all the boards (ctrl's) */
	for (ctrl = 0; ctrl < max_board; ctrl++) {
		if (rcktpt_io_addr[ctrl] <= 0)
			continue;

		/*  Get a ptr to the board's control struct */
		ctlp = sCtlNumToCtlPtr(ctrl);

		/*  Get the interupt status from the board */
#ifdef CONFIG_PCI
		if (ctlp->BusType == isPCI)
			CtlMask = sPCIGetControllerIntStatus(ctlp);
		else
#endif
			CtlMask = sGetControllerIntStatus(ctlp);

		/*  Check if any AIOP read bits are set */
		for (aiop = 0; CtlMask; aiop++) {
			bit = ctlp->AiopIntrBits[aiop];
			if (CtlMask & bit) {
				CtlMask &= ~bit;
				AiopMask = sGetAiopIntStatus(ctlp, aiop);

				/*  Check if any port read bits are set */
				for (ch = 0; AiopMask;  AiopMask >>= 1, ch++) {
					if (AiopMask & 1) {

						/*  Get the line number (/dev/ttyRx number). */
						/*  Read the data from the port. */
						line = GetLineNumber(ctrl, aiop, ch);
						rp_handle_port(rp_table[line]);
					}
				}
			}
		}

		xmitmask = xmit_flags[ctrl];

		/*
		 *  xmit_flags contains bit-significant flags, indicating there is data
		 *  to xmit on the port. Bit 0 is port 0 on this board, bit 1 is port 
		 *  1, ... (32 total possible).  The variable i has the aiop and ch 
		 *  numbers encoded in it (port 0-7 are aiop0, 8-15 are aiop1, etc).
		 */
		if (xmitmask) {
			for (i = 0; i < rocketModel[ctrl].numPorts; i++) {
				if (xmitmask & (1 << i)) {
					aiop = (i & 0x18) >> 3;
					ch = i & 0x07;
					line = GetLineNumber(ctrl, aiop, ch);
					rp_do_transmit(rp_table[line]);
				}
			}
		}
	}

	/*
	 * Reset the timer so we get called at the next clock tick (10ms).
	 */
	if (atomic_read(&rp_num_ports_open))
		mod_timer(&rocket_timer, jiffies + POLL_PERIOD);
}

/*
 *  Initializes the r_port structure for a port, as well as enabling the port on 
 *  the board.  
 *  Inputs:  board, aiop, chan numbers
 */
static void init_r_port(int board, int aiop, int chan, struct pci_dev *pci_dev)
{
	unsigned rocketMode;
	struct r_port *info;
	int line;
	CONTROLLER_T *ctlp;

	/*  Get the next available line number */
	line = SetLineNumber(board, aiop, chan);

	ctlp = sCtlNumToCtlPtr(board);

	/*  Get a r_port struct for the port, fill it in and save it globally, indexed by line number */
	info = kmalloc(sizeof (struct r_port), GFP_KERNEL);
	if (!info) {
		printk(KERN_INFO "Couldn't allocate info struct for line #%d\n", line);
		return;
	}
	memset(info, 0, sizeof (struct r_port));

	info->magic = RPORT_MAGIC;
	info->line = line;
	info->ctlp = ctlp;
	info->board = board;
	info->aiop = aiop;
	info->chan = chan;
	info->closing_wait = 3000;
	info->close_delay = 50;
	init_waitqueue_head(&info->open_wait);
	init_waitqueue_head(&info->close_wait);
	info->flags &= ~ROCKET_MODE_MASK;
	switch (pc104[board][line]) {
	case 422:
		info->flags |= ROCKET_MODE_RS422;
		break;
	case 485:
		info->flags |= ROCKET_MODE_RS485;
		break;
	case 232:
	default:
		info->flags |= ROCKET_MODE_RS232;
		break;
	}

	info->intmask = RXF_TRIG | TXFIFO_MT | SRC_INT | DELTA_CD | DELTA_CTS | DELTA_DSR;
	if (sInitChan(ctlp, &info->channel, aiop, chan) == 0) {
		printk(KERN_INFO "RocketPort sInitChan(%d, %d, %d) failed!\n", board, aiop, chan);
		kfree(info);
		return;
	}

	rocketMode = info->flags & ROCKET_MODE_MASK;

	if ((info->flags & ROCKET_RTS_TOGGLE) || (rocketMode == ROCKET_MODE_RS485))
		sEnRTSToggle(&info->channel);
	else
		sDisRTSToggle(&info->channel);

	if (ctlp->boardType == ROCKET_TYPE_PC104) {
		switch (rocketMode) {
		case ROCKET_MODE_RS485:
			sSetInterfaceMode(&info->channel, InterfaceModeRS485);
			break;
		case ROCKET_MODE_RS422:
			sSetInterfaceMode(&info->channel, InterfaceModeRS422);
			break;
		case ROCKET_MODE_RS232:
		default:
			if (info->flags & ROCKET_RTS_TOGGLE)
				sSetInterfaceMode(&info->channel, InterfaceModeRS232T);
			else
				sSetInterfaceMode(&info->channel, InterfaceModeRS232);
			break;
		}
	}
	spin_lock_init(&info->slock);
	sema_init(&info->write_sem, 1);
	rp_table[line] = info;
	if (pci_dev)
		tty_register_device(rocket_driver, line, &pci_dev->dev);
}

/*
 *  Configures a rocketport port according to its termio settings.  Called from 
 *  user mode into the driver (exception handler).  *info CD manipulation is spinlock protected.
 */
static void configure_r_port(struct r_port *info,
			     struct termios *old_termios)
{
	unsigned cflag;
	unsigned long flags;
	unsigned rocketMode;
	int bits, baud, divisor;
	CHANNEL_t *cp;

	if (!info->tty || !info->tty->termios)
		return;
	cp = &info->channel;
	cflag = info->tty->termios->c_cflag;

	/* Byte size and parity */
	if ((cflag & CSIZE) == CS8) {
		sSetData8(cp);
		bits = 10;
	} else {
		sSetData7(cp);
		bits = 9;
	}
	if (cflag & CSTOPB) {
		sSetStop2(cp);
		bits++;
	} else {
		sSetStop1(cp);
	}

	if (cflag & PARENB) {
		sEnParity(cp);
		bits++;
		if (cflag & PARODD) {
			sSetOddParity(cp);
		} else {
			sSetEvenParity(cp);
		}
	} else {
		sDisParity(cp);
	}

	/* baud rate */
	baud = tty_get_baud_rate(info->tty);
	if (!baud)
		baud = 9600;
	divisor = ((rp_baud_base[info->board] + (baud >> 1)) / baud) - 1;
	if ((divisor >= 8192 || divisor < 0) && old_termios) {
		info->tty->termios->c_cflag &= ~CBAUD;
		info->tty->termios->c_cflag |=
		    (old_termios->c_cflag & CBAUD);
		baud = tty_get_baud_rate(info->tty);
		if (!baud)
			baud = 9600;
		divisor = (rp_baud_base[info->board] / baud) - 1;
	}
	if (divisor >= 8192 || divisor < 0) {
		baud = 9600;
		divisor = (rp_baud_base[info->board] / baud) - 1;
	}
	info->cps = baud / bits;
	sSetBaud(cp, divisor);

	if (cflag & CRTSCTS) {
		info->intmask |= DELTA_CTS;
		sEnCTSFlowCtl(cp);
	} else {
		info->intmask &= ~DELTA_CTS;
		sDisCTSFlowCtl(cp);
	}
	if (cflag & CLOCAL) {
		info->intmask &= ~DELTA_CD;
	} else {
		spin_lock_irqsave(&info->slock, flags);
		if (sGetChanStatus(cp) & CD_ACT)
			info->cd_status = 1;
		else
			info->cd_status = 0;
		info->intmask |= DELTA_CD;
		spin_unlock_irqrestore(&info->slock, flags);
	}

	/*
	 * Handle software flow control in the board
	 */
#ifdef ROCKET_SOFT_FLOW
	if (I_IXON(info->tty)) {
		sEnTxSoftFlowCtl(cp);
		if (I_IXANY(info->tty)) {
			sEnIXANY(cp);
		} else {
			sDisIXANY(cp);
		}
		sSetTxXONChar(cp, START_CHAR(info->tty));
		sSetTxXOFFChar(cp, STOP_CHAR(info->tty));
	} else {
		sDisTxSoftFlowCtl(cp);
		sDisIXANY(cp);
		sClrTxXOFF(cp);
	}
#endif

	/*
	 * Set up ignore/read mask words
	 */
	info->read_status_mask = STMRCVROVRH | 0xFF;
	if (I_INPCK(info->tty))
		info->read_status_mask |= STMFRAMEH | STMPARITYH;
	if (I_BRKINT(info->tty) || I_PARMRK(info->tty))
		info->read_status_mask |= STMBREAKH;

	/*
	 * Characters to ignore
	 */
	info->ignore_status_mask = 0;
	if (I_IGNPAR(info->tty))
		info->ignore_status_mask |= STMFRAMEH | STMPARITYH;
	if (I_IGNBRK(info->tty)) {
		info->ignore_status_mask |= STMBREAKH;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(info->tty))
			info->ignore_status_mask |= STMRCVROVRH;
	}

	rocketMode = info->flags & ROCKET_MODE_MASK;

	if ((info->flags & ROCKET_RTS_TOGGLE)
	    || (rocketMode == ROCKET_MODE_RS485))
		sEnRTSToggle(cp);
	else
		sDisRTSToggle(cp);

	sSetRTS(&info->channel);

	if (cp->CtlP->boardType == ROCKET_TYPE_PC104) {
		switch (rocketMode) {
		case ROCKET_MODE_RS485:
			sSetInterfaceMode(cp, InterfaceModeRS485);
			break;
		case ROCKET_MODE_RS422:
			sSetInterfaceMode(cp, InterfaceModeRS422);
			break;
		case ROCKET_MODE_RS232:
		default:
			if (info->flags & ROCKET_RTS_TOGGLE)
				sSetInterfaceMode(cp, InterfaceModeRS232T);
			else
				sSetInterfaceMode(cp, InterfaceModeRS232);
			break;
		}
	}
}

/*  info->count is considered critical, protected by spinlocks.  */
static int block_til_ready(struct tty_struct *tty, struct file *filp,
			   struct r_port *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int retval;
	int do_clocal = 0, extra_count = 0;
	unsigned long flags;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp))
		return ((info->flags & ROCKET_HUP_NOTIFY) ? -EAGAIN : -ERESTARTSYS);
	if (info->flags & ROCKET_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
		return ((info->flags & ROCKET_HUP_NOTIFY) ? -EAGAIN : -ERESTARTSYS);
	}

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) || (tty->flags & (1 << TTY_IO_ERROR))) {
		info->flags |= ROCKET_NORMAL_ACTIVE;
		return 0;
	}
	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;

	/*
	 * Block waiting for the carrier detect and the line to become free.  While we are in
	 * this loop, info->count is dropped by one, so that rp_close() knows when to free things.  
         * We restore it upon exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef ROCKET_DEBUG_OPEN
	printk(KERN_INFO "block_til_ready before block: ttyR%d, count = %d\n", info->line, info->count);
#endif
	spin_lock_irqsave(&info->slock, flags);

#ifdef ROCKET_DISABLE_SIMUSAGE
	info->flags |= ROCKET_NORMAL_ACTIVE;
#else
	if (!tty_hung_up_p(filp)) {
		extra_count = 1;
		info->count--;
	}
#endif
	info->blocked_open++;

	spin_unlock_irqrestore(&info->slock, flags);

	while (1) {
		if (tty->termios->c_cflag & CBAUD) {
			sSetDTR(&info->channel);
			sSetRTS(&info->channel);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) || !(info->flags & ROCKET_INITIALIZED)) {
			if (info->flags & ROCKET_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
			break;
		}
		if (!(info->flags & ROCKET_CLOSING) && (do_clocal || (sGetChanStatusLo(&info->channel) & CD_ACT)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef ROCKET_DEBUG_OPEN
		printk(KERN_INFO "block_til_ready blocking: ttyR%d, count = %d, flags=0x%0x\n",
		     info->line, info->count, info->flags);
#endif
		schedule();	/*  Don't hold spinlock here, will hang PC */
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);

	spin_lock_irqsave(&info->slock, flags);

	if (extra_count)
		info->count++;
	info->blocked_open--;

	spin_unlock_irqrestore(&info->slock, flags);

#ifdef ROCKET_DEBUG_OPEN
	printk(KERN_INFO "block_til_ready after blocking: ttyR%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ROCKET_NORMAL_ACTIVE;
	return 0;
}

/*
 *  Exception handler that opens a serial port.  Creates xmit_buf storage, fills in 
 *  port's r_port struct.  Initializes the port hardware.  
 */
static int rp_open(struct tty_struct *tty, struct file *filp)
{
	struct r_port *info;
	int line = 0, retval;
	CHANNEL_t *cp;
	unsigned long page;

	line = TTY_GET_LINE(tty);
	if ((line < 0) || (line >= MAX_RP_PORTS) || ((info = rp_table[line]) == NULL))
		return -ENXIO;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	if (info->flags & ROCKET_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
		free_page(page);
		return ((info->flags & ROCKET_HUP_NOTIFY) ? -EAGAIN : -ERESTARTSYS);
	}

	/*
	 * We must not sleep from here until the port is marked fully in use.
	 */
	if (info->xmit_buf)
		free_page(page);
	else
		info->xmit_buf = (unsigned char *) page;

	tty->driver_data = info;
	info->tty = tty;

	if (info->count++ == 0) {
		atomic_inc(&rp_num_ports_open);

#ifdef ROCKET_DEBUG_OPEN
		printk(KERN_INFO "rocket mod++ = %d...", atomic_read(&rp_num_ports_open));
#endif
	}
#ifdef ROCKET_DEBUG_OPEN
	printk(KERN_INFO "rp_open ttyR%d, count=%d\n", info->line, info->count);
#endif

	/*
	 * Info->count is now 1; so it's safe to sleep now.
	 */
	info->session = current->signal->session;
	info->pgrp = process_group(current);

	if ((info->flags & ROCKET_INITIALIZED) == 0) {
		cp = &info->channel;
		sSetRxTrigger(cp, TRIG_1);
		if (sGetChanStatus(cp) & CD_ACT)
			info->cd_status = 1;
		else
			info->cd_status = 0;
		sDisRxStatusMode(cp);
		sFlushRxFIFO(cp);
		sFlushTxFIFO(cp);

		sEnInterrupts(cp, (TXINT_EN | MCINT_EN | RXINT_EN | SRCINT_EN | CHANINT_EN));
		sSetRxTrigger(cp, TRIG_1);

		sGetChanStatus(cp);
		sDisRxStatusMode(cp);
		sClrTxXOFF(cp);

		sDisCTSFlowCtl(cp);
		sDisTxSoftFlowCtl(cp);

		sEnRxFIFO(cp);
		sEnTransmit(cp);

		info->flags |= ROCKET_INITIALIZED;

		/*
		 * Set up the tty->alt_speed kludge
		 */
		if ((info->flags & ROCKET_SPD_MASK) == ROCKET_SPD_HI)
			info->tty->alt_speed = 57600;
		if ((info->flags & ROCKET_SPD_MASK) == ROCKET_SPD_VHI)
			info->tty->alt_speed = 115200;
		if ((info->flags & ROCKET_SPD_MASK) == ROCKET_SPD_SHI)
			info->tty->alt_speed = 230400;
		if ((info->flags & ROCKET_SPD_MASK) == ROCKET_SPD_WARP)
			info->tty->alt_speed = 460800;

		configure_r_port(info, NULL);
		if (tty->termios->c_cflag & CBAUD) {
			sSetDTR(cp);
			sSetRTS(cp);
		}
	}
	/*  Starts (or resets) the maint polling loop */
	mod_timer(&rocket_timer, jiffies + POLL_PERIOD);

	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef ROCKET_DEBUG_OPEN
		printk(KERN_INFO "rp_open returning after block_til_ready with %d\n", retval);
#endif
		return retval;
	}
	return 0;
}

/*
 *  Exception handler that closes a serial port. info->count is considered critical. 
 */
static void rp_close(struct tty_struct *tty, struct file *filp)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	unsigned long flags;
	int timeout;
	CHANNEL_t *cp;
	
	if (rocket_paranoia_check(info, "rp_close"))
		return;

#ifdef ROCKET_DEBUG_OPEN
	printk(KERN_INFO "rp_close ttyR%d, count = %d\n", info->line, info->count);
#endif

	if (tty_hung_up_p(filp))
		return;
	spin_lock_irqsave(&info->slock, flags);

	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk(KERN_INFO "rp_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk(KERN_INFO "rp_close: bad serial port count for ttyR%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		spin_unlock_irqrestore(&info->slock, flags);
		return;
	}
	info->flags |= ROCKET_CLOSING;
	spin_unlock_irqrestore(&info->slock, flags);

	cp = &info->channel;

	/*
	 * Notify the line discpline to only process XON/XOFF characters
	 */
	tty->closing = 1;

	/*
	 * If transmission was throttled by the application request,
	 * just flush the xmit buffer.
	 */
	if (tty->flow_stopped)
		rp_flush_buffer(tty);

	/*
	 * Wait for the transmit buffer to clear
	 */
	if (info->closing_wait != ROCKET_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * Before we drop DTR, make sure the UART transmitter
	 * has completely drained; this is especially
	 * important if there is a transmit FIFO!
	 */
	timeout = (sGetTxCnt(cp) + 1) * HZ / info->cps;
	if (timeout == 0)
		timeout = 1;
	rp_wait_until_sent(tty, timeout);
	clear_bit((info->aiop * 8) + info->chan, (void *) &xmit_flags[info->board]);

	sDisTransmit(cp);
	sDisInterrupts(cp, (TXINT_EN | MCINT_EN | RXINT_EN | SRCINT_EN | CHANINT_EN));
	sDisCTSFlowCtl(cp);
	sDisTxSoftFlowCtl(cp);
	sClrTxXOFF(cp);
	sFlushRxFIFO(cp);
	sFlushTxFIFO(cp);
	sClrRTS(cp);
	if (C_HUPCL(tty))
		sClrDTR(cp);

	if (TTY_DRIVER_FLUSH_BUFFER_EXISTS(tty))
		TTY_DRIVER_FLUSH_BUFFER(tty);
		
	tty_ldisc_flush(tty);

	clear_bit((info->aiop * 8) + info->chan, (void *) &xmit_flags[info->board]);

	if (info->blocked_open) {
		if (info->close_delay) {
			msleep_interruptible(jiffies_to_msecs(info->close_delay));
		}
		wake_up_interruptible(&info->open_wait);
	} else {
		if (info->xmit_buf) {
			free_page((unsigned long) info->xmit_buf);
			info->xmit_buf = NULL;
		}
	}
	info->flags &= ~(ROCKET_INITIALIZED | ROCKET_CLOSING | ROCKET_NORMAL_ACTIVE);
	tty->closing = 0;
	wake_up_interruptible(&info->close_wait);
	atomic_dec(&rp_num_ports_open);

#ifdef ROCKET_DEBUG_OPEN
	printk(KERN_INFO "rocket mod-- = %d...", atomic_read(&rp_num_ports_open));
	printk(KERN_INFO "rp_close ttyR%d complete shutdown\n", info->line);
#endif

}

static void rp_set_termios(struct tty_struct *tty,
			   struct termios *old_termios)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	CHANNEL_t *cp;
	unsigned cflag;

	if (rocket_paranoia_check(info, "rp_set_termios"))
		return;

	cflag = tty->termios->c_cflag;

	if (cflag == old_termios->c_cflag)
		return;

	/*
	 * This driver doesn't support CS5 or CS6
	 */
	if (((cflag & CSIZE) == CS5) || ((cflag & CSIZE) == CS6))
		tty->termios->c_cflag =
		    ((cflag & ~CSIZE) | (old_termios->c_cflag & CSIZE));

	configure_r_port(info, old_termios);

	cp = &info->channel;

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) && !(tty->termios->c_cflag & CBAUD)) {
		sClrDTR(cp);
		sClrRTS(cp);
	}

	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) && (tty->termios->c_cflag & CBAUD)) {
		if (!tty->hw_stopped || !(tty->termios->c_cflag & CRTSCTS))
			sSetRTS(cp);
		sSetDTR(cp);
	}

	if ((old_termios->c_cflag & CRTSCTS) && !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rp_start(tty);
	}
}

static void rp_break(struct tty_struct *tty, int break_state)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	unsigned long flags;

	if (rocket_paranoia_check(info, "rp_break"))
		return;

	spin_lock_irqsave(&info->slock, flags);
	if (break_state == -1)
		sSendBreak(&info->channel);
	else
		sClrBreak(&info->channel);
	spin_unlock_irqrestore(&info->slock, flags);
}

/*
 * sGetChanRI used to be a macro in rocket_int.h. When the functionality for
 * the UPCI boards was added, it was decided to make this a function because
 * the macro was getting too complicated. All cases except the first one
 * (UPCIRingInd) are taken directly from the original macro.
 */
static int sGetChanRI(CHANNEL_T * ChP)
{
	CONTROLLER_t *CtlP = ChP->CtlP;
	int ChanNum = ChP->ChanNum;
	int RingInd = 0;

	if (CtlP->UPCIRingInd)
		RingInd = !(sInB(CtlP->UPCIRingInd) & sBitMapSetTbl[ChanNum]);
	else if (CtlP->AltChanRingIndicator)
		RingInd = sInB((ByteIO_t) (ChP->ChanStat + 8)) & DSR_ACT;
	else if (CtlP->boardType == ROCKET_TYPE_PC104)
		RingInd = !(sInB(CtlP->AiopIO[3]) & sBitMapSetTbl[ChanNum]);

	return RingInd;
}

/********************************************************************************************/
/*  Here are the routines used by rp_ioctl.  These are all called from exception handlers.  */

/*
 *  Returns the state of the serial modem control lines.  These next 2 functions 
 *  are the way kernel versions > 2.5 handle modem control lines rather than IOCTLs.
 */
static int rp_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct r_port *info = (struct r_port *)tty->driver_data;
	unsigned int control, result, ChanStatus;

	ChanStatus = sGetChanStatusLo(&info->channel);
	control = info->channel.TxControl[3];
	result = ((control & SET_RTS) ? TIOCM_RTS : 0) | 
		((control & SET_DTR) ?  TIOCM_DTR : 0) |
		((ChanStatus & CD_ACT) ? TIOCM_CAR : 0) |
		(sGetChanRI(&info->channel) ? TIOCM_RNG : 0) |
		((ChanStatus & DSR_ACT) ? TIOCM_DSR : 0) |
		((ChanStatus & CTS_ACT) ? TIOCM_CTS : 0);

	return result;
}

/* 
 *  Sets the modem control lines
 */
static int rp_tiocmset(struct tty_struct *tty, struct file *file,
		    unsigned int set, unsigned int clear)
{
	struct r_port *info = (struct r_port *)tty->driver_data;

	if (set & TIOCM_RTS)
		info->channel.TxControl[3] |= SET_RTS;
	if (set & TIOCM_DTR)
		info->channel.TxControl[3] |= SET_DTR;
	if (clear & TIOCM_RTS)
		info->channel.TxControl[3] &= ~SET_RTS;
	if (clear & TIOCM_DTR)
		info->channel.TxControl[3] &= ~SET_DTR;

	sOutDW(info->channel.IndexAddr, *(DWord_t *) & (info->channel.TxControl[0]));
	return 0;
}

static int get_config(struct r_port *info, struct rocket_config __user *retinfo)
{
	struct rocket_config tmp;

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof (tmp));
	tmp.line = info->line;
	tmp.flags = info->flags;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.port = rcktpt_io_addr[(info->line >> 5) & 3];

	if (copy_to_user(retinfo, &tmp, sizeof (*retinfo)))
		return -EFAULT;
	return 0;
}

static int set_config(struct r_port *info, struct rocket_config __user *new_info)
{
	struct rocket_config new_serial;

	if (copy_from_user(&new_serial, new_info, sizeof (new_serial)))
		return -EFAULT;

	if (!capable(CAP_SYS_ADMIN))
	{
		if ((new_serial.flags & ~ROCKET_USR_MASK) != (info->flags & ~ROCKET_USR_MASK))
			return -EPERM;
		info->flags = ((info->flags & ~ROCKET_USR_MASK) | (new_serial.flags & ROCKET_USR_MASK));
		configure_r_port(info, NULL);
		return 0;
	}

	info->flags = ((info->flags & ~ROCKET_FLAGS) | (new_serial.flags & ROCKET_FLAGS));
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;

	if ((info->flags & ROCKET_SPD_MASK) == ROCKET_SPD_HI)
		info->tty->alt_speed = 57600;
	if ((info->flags & ROCKET_SPD_MASK) == ROCKET_SPD_VHI)
		info->tty->alt_speed = 115200;
	if ((info->flags & ROCKET_SPD_MASK) == ROCKET_SPD_SHI)
		info->tty->alt_speed = 230400;
	if ((info->flags & ROCKET_SPD_MASK) == ROCKET_SPD_WARP)
		info->tty->alt_speed = 460800;

	configure_r_port(info, NULL);
	return 0;
}

/*
 *  This function fills in a rocket_ports struct with information
 *  about what boards/ports are in the system.  This info is passed
 *  to user space.  See setrocket.c where the info is used to create
 *  the /dev/ttyRx ports.
 */
static int get_ports(struct r_port *info, struct rocket_ports __user *retports)
{
	struct rocket_ports tmp;
	int board;

	if (!retports)
		return -EFAULT;
	memset(&tmp, 0, sizeof (tmp));
	tmp.tty_major = rocket_driver->major;

	for (board = 0; board < 4; board++) {
		tmp.rocketModel[board].model = rocketModel[board].model;
		strcpy(tmp.rocketModel[board].modelString, rocketModel[board].modelString);
		tmp.rocketModel[board].numPorts = rocketModel[board].numPorts;
		tmp.rocketModel[board].loadrm2 = rocketModel[board].loadrm2;
		tmp.rocketModel[board].startingPortNumber = rocketModel[board].startingPortNumber;
	}
	if (copy_to_user(retports, &tmp, sizeof (*retports)))
		return -EFAULT;
	return 0;
}

static int reset_rm2(struct r_port *info, void __user *arg)
{
	int reset;

	if (copy_from_user(&reset, arg, sizeof (int)))
		return -EFAULT;
	if (reset)
		reset = 1;

	if (rcktpt_type[info->board] != ROCKET_TYPE_MODEMII &&
            rcktpt_type[info->board] != ROCKET_TYPE_MODEMIII)
		return -EINVAL;

	if (info->ctlp->BusType == isISA)
		sModemReset(info->ctlp, info->chan, reset);
	else
		sPCIModemReset(info->ctlp, info->chan, reset);

	return 0;
}

static int get_version(struct r_port *info, struct rocket_version __user *retvers)
{
	if (copy_to_user(retvers, &driver_version, sizeof (*retvers)))
		return -EFAULT;
	return 0;
}

/*  IOCTL call handler into the driver */
static int rp_ioctl(struct tty_struct *tty, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	void __user *argp = (void __user *)arg;

	if (cmd != RCKP_GET_PORTS && rocket_paranoia_check(info, "rp_ioctl"))
		return -ENXIO;

	switch (cmd) {
	case RCKP_GET_STRUCT:
		if (copy_to_user(argp, info, sizeof (struct r_port)))
			return -EFAULT;
		return 0;
	case RCKP_GET_CONFIG:
		return get_config(info, argp);
	case RCKP_SET_CONFIG:
		return set_config(info, argp);
	case RCKP_GET_PORTS:
		return get_ports(info, argp);
	case RCKP_RESET_RM2:
		return reset_rm2(info, argp);
	case RCKP_GET_VERSION:
		return get_version(info, argp);
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static void rp_send_xchar(struct tty_struct *tty, char ch)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	CHANNEL_t *cp;

	if (rocket_paranoia_check(info, "rp_send_xchar"))
		return;

	cp = &info->channel;
	if (sGetTxCnt(cp))
		sWriteTxPrioByte(cp, ch);
	else
		sWriteTxByte(sGetTxRxDataIO(cp), ch);
}

static void rp_throttle(struct tty_struct *tty)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	CHANNEL_t *cp;

#ifdef ROCKET_DEBUG_THROTTLE
	printk(KERN_INFO "throttle %s: %d....\n", tty->name,
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (rocket_paranoia_check(info, "rp_throttle"))
		return;

	cp = &info->channel;
	if (I_IXOFF(tty))
		rp_send_xchar(tty, STOP_CHAR(tty));

	sClrRTS(&info->channel);
}

static void rp_unthrottle(struct tty_struct *tty)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	CHANNEL_t *cp;
#ifdef ROCKET_DEBUG_THROTTLE
	printk(KERN_INFO "unthrottle %s: %d....\n", tty->name,
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (rocket_paranoia_check(info, "rp_throttle"))
		return;

	cp = &info->channel;
	if (I_IXOFF(tty))
		rp_send_xchar(tty, START_CHAR(tty));

	sSetRTS(&info->channel);
}

/*
 * ------------------------------------------------------------
 * rp_stop() and rp_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void rp_stop(struct tty_struct *tty)
{
	struct r_port *info = (struct r_port *) tty->driver_data;

#ifdef ROCKET_DEBUG_FLOW
	printk(KERN_INFO "stop %s: %d %d....\n", tty->name,
	       info->xmit_cnt, info->xmit_fifo_room);
#endif

	if (rocket_paranoia_check(info, "rp_stop"))
		return;

	if (sGetTxCnt(&info->channel))
		sDisTransmit(&info->channel);
}

static void rp_start(struct tty_struct *tty)
{
	struct r_port *info = (struct r_port *) tty->driver_data;

#ifdef ROCKET_DEBUG_FLOW
	printk(KERN_INFO "start %s: %d %d....\n", tty->name,
	       info->xmit_cnt, info->xmit_fifo_room);
#endif

	if (rocket_paranoia_check(info, "rp_stop"))
		return;

	sEnTransmit(&info->channel);
	set_bit((info->aiop * 8) + info->chan,
		(void *) &xmit_flags[info->board]);
}

/*
 * rp_wait_until_sent() --- wait until the transmitter is empty
 */
static void rp_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	CHANNEL_t *cp;
	unsigned long orig_jiffies;
	int check_time, exit_time;
	int txcnt;

	if (rocket_paranoia_check(info, "rp_wait_until_sent"))
		return;

	cp = &info->channel;

	orig_jiffies = jiffies;
#ifdef ROCKET_DEBUG_WAIT_UNTIL_SENT
	printk(KERN_INFO "In RP_wait_until_sent(%d) (jiff=%lu)...", timeout,
	       jiffies);
	printk(KERN_INFO "cps=%d...", info->cps);
#endif
	while (1) {
		txcnt = sGetTxCnt(cp);
		if (!txcnt) {
			if (sGetChanStatusLo(cp) & TXSHRMT)
				break;
			check_time = (HZ / info->cps) / 5;
		} else {
			check_time = HZ * txcnt / info->cps;
		}
		if (timeout) {
			exit_time = orig_jiffies + timeout - jiffies;
			if (exit_time <= 0)
				break;
			if (exit_time < check_time)
				check_time = exit_time;
		}
		if (check_time == 0)
			check_time = 1;
#ifdef ROCKET_DEBUG_WAIT_UNTIL_SENT
		printk(KERN_INFO "txcnt = %d (jiff=%lu,check=%d)...", txcnt, jiffies, check_time);
#endif
		msleep_interruptible(jiffies_to_msecs(check_time));
		if (signal_pending(current))
			break;
	}
	current->state = TASK_RUNNING;
#ifdef ROCKET_DEBUG_WAIT_UNTIL_SENT
	printk(KERN_INFO "txcnt = %d (jiff=%lu)...done\n", txcnt, jiffies);
#endif
}

/*
 * rp_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void rp_hangup(struct tty_struct *tty)
{
	CHANNEL_t *cp;
	struct r_port *info = (struct r_port *) tty->driver_data;

	if (rocket_paranoia_check(info, "rp_hangup"))
		return;

#if (defined(ROCKET_DEBUG_OPEN) || defined(ROCKET_DEBUG_HANGUP))
	printk(KERN_INFO "rp_hangup of ttyR%d...", info->line);
#endif
	rp_flush_buffer(tty);
	if (info->flags & ROCKET_CLOSING)
		return;
	if (info->count) 
		atomic_dec(&rp_num_ports_open);
	clear_bit((info->aiop * 8) + info->chan, (void *) &xmit_flags[info->board]);

	info->count = 0;
	info->flags &= ~ROCKET_NORMAL_ACTIVE;
	info->tty = NULL;

	cp = &info->channel;
	sDisRxFIFO(cp);
	sDisTransmit(cp);
	sDisInterrupts(cp, (TXINT_EN | MCINT_EN | RXINT_EN | SRCINT_EN | CHANINT_EN));
	sDisCTSFlowCtl(cp);
	sDisTxSoftFlowCtl(cp);
	sClrTxXOFF(cp);
	info->flags &= ~ROCKET_INITIALIZED;

	wake_up_interruptible(&info->open_wait);
}

/*
 *  Exception handler - write char routine.  The RocketPort driver uses a
 *  double-buffering strategy, with the twist that if the in-memory CPU
 *  buffer is empty, and there's space in the transmit FIFO, the
 *  writing routines will write directly to transmit FIFO.
 *  Write buffer and counters protected by spinlocks
 */
static void rp_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	CHANNEL_t *cp;
	unsigned long flags;

	if (rocket_paranoia_check(info, "rp_put_char"))
		return;

	/*  Grab the port write semaphore, locking out other processes that try to write to this port */
	down(&info->write_sem);

#ifdef ROCKET_DEBUG_WRITE
	printk(KERN_INFO "rp_put_char %c...", ch);
#endif

	spin_lock_irqsave(&info->slock, flags);
	cp = &info->channel;

	if (!tty->stopped && !tty->hw_stopped && info->xmit_fifo_room == 0)
		info->xmit_fifo_room = TXFIFO_SIZE - sGetTxCnt(cp);

	if (tty->stopped || tty->hw_stopped || info->xmit_fifo_room == 0 || info->xmit_cnt != 0) {
		info->xmit_buf[info->xmit_head++] = ch;
		info->xmit_head &= XMIT_BUF_SIZE - 1;
		info->xmit_cnt++;
		set_bit((info->aiop * 8) + info->chan, (void *) &xmit_flags[info->board]);
	} else {
		sOutB(sGetTxRxDataIO(cp), ch);
		info->xmit_fifo_room--;
	}
	spin_unlock_irqrestore(&info->slock, flags);
	up(&info->write_sem);
}

/*
 *  Exception handler - write routine, called when user app writes to the device.
 *  A per port write semaphore is used to protect from another process writing to
 *  this port at the same time.  This other process could be running on the other CPU
 *  or get control of the CPU if the copy_from_user() blocks due to a page fault (swapped out). 
 *  Spinlocks protect the info xmit members.
 */
static int rp_write(struct tty_struct *tty,
		    const unsigned char *buf, int count)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	CHANNEL_t *cp;
	const unsigned char *b;
	int c, retval = 0;
	unsigned long flags;

	if (count <= 0 || rocket_paranoia_check(info, "rp_write"))
		return 0;

	down_interruptible(&info->write_sem);

#ifdef ROCKET_DEBUG_WRITE
	printk(KERN_INFO "rp_write %d chars...", count);
#endif
	cp = &info->channel;

	if (!tty->stopped && !tty->hw_stopped && info->xmit_fifo_room < count)
		info->xmit_fifo_room = TXFIFO_SIZE - sGetTxCnt(cp);

        /*
	 *  If the write queue for the port is empty, and there is FIFO space, stuff bytes 
	 *  into FIFO.  Use the write queue for temp storage.
         */
	if (!tty->stopped && !tty->hw_stopped && info->xmit_cnt == 0 && info->xmit_fifo_room > 0) {
		c = min(count, info->xmit_fifo_room);
		b = buf;

		/*  Push data into FIFO, 2 bytes at a time */
		sOutStrW(sGetTxRxDataIO(cp), (unsigned short *) b, c / 2);

		/*  If there is a byte remaining, write it */
		if (c & 1)
			sOutB(sGetTxRxDataIO(cp), b[c - 1]);

		retval += c;
		buf += c;
		count -= c;

		spin_lock_irqsave(&info->slock, flags);
		info->xmit_fifo_room -= c;
		spin_unlock_irqrestore(&info->slock, flags);
	}

	/* If count is zero, we wrote it all and are done */
	if (!count)
		goto end;

	/*  Write remaining data into the port's xmit_buf */
	while (1) {
		if (info->tty == 0)	/*   Seemingly obligatory check... */
			goto end;

		c = min(count, min(XMIT_BUF_SIZE - info->xmit_cnt - 1, XMIT_BUF_SIZE - info->xmit_head));
		if (c <= 0)
			break;

		b = buf;
		memcpy(info->xmit_buf + info->xmit_head, b, c);

		spin_lock_irqsave(&info->slock, flags);
		info->xmit_head =
		    (info->xmit_head + c) & (XMIT_BUF_SIZE - 1);
		info->xmit_cnt += c;
		spin_unlock_irqrestore(&info->slock, flags);

		buf += c;
		count -= c;
		retval += c;
	}

	if ((retval > 0) && !tty->stopped && !tty->hw_stopped)
		set_bit((info->aiop * 8) + info->chan, (void *) &xmit_flags[info->board]);
	
end:
 	if (info->xmit_cnt < WAKEUP_CHARS) {
 		tty_wakeup(tty);
		wake_up_interruptible(&tty->write_wait);
#ifdef ROCKETPORT_HAVE_POLL_WAIT
		wake_up_interruptible(&tty->poll_wait);
#endif
	}
	up(&info->write_sem);
	return retval;
}

/*
 * Return the number of characters that can be sent.  We estimate
 * only using the in-memory transmit buffer only, and ignore the
 * potential space in the transmit FIFO.
 */
static int rp_write_room(struct tty_struct *tty)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	int ret;

	if (rocket_paranoia_check(info, "rp_write_room"))
		return 0;

	ret = XMIT_BUF_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
#ifdef ROCKET_DEBUG_WRITE
	printk(KERN_INFO "rp_write_room returns %d...", ret);
#endif
	return ret;
}

/*
 * Return the number of characters in the buffer.  Again, this only
 * counts those characters in the in-memory transmit buffer.
 */
static int rp_chars_in_buffer(struct tty_struct *tty)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	CHANNEL_t *cp;

	if (rocket_paranoia_check(info, "rp_chars_in_buffer"))
		return 0;

	cp = &info->channel;

#ifdef ROCKET_DEBUG_WRITE
	printk(KERN_INFO "rp_chars_in_buffer returns %d...", info->xmit_cnt);
#endif
	return info->xmit_cnt;
}

/*
 *  Flushes the TX fifo for a port, deletes data in the xmit_buf stored in the
 *  r_port struct for the port.  Note that spinlock are used to protect info members,
 *  do not call this function if the spinlock is already held.
 */
static void rp_flush_buffer(struct tty_struct *tty)
{
	struct r_port *info = (struct r_port *) tty->driver_data;
	CHANNEL_t *cp;
	unsigned long flags;

	if (rocket_paranoia_check(info, "rp_flush_buffer"))
		return;

	spin_lock_irqsave(&info->slock, flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	spin_unlock_irqrestore(&info->slock, flags);

	wake_up_interruptible(&tty->write_wait);
#ifdef ROCKETPORT_HAVE_POLL_WAIT
	wake_up_interruptible(&tty->poll_wait);
#endif
	tty_wakeup(tty);

	cp = &info->channel;
	sFlushTxFIFO(cp);
}

#ifdef CONFIG_PCI

/*
 *  Called when a PCI card is found.  Retrieves and stores model information,
 *  init's aiopic and serial port hardware.
 *  Inputs:  i is the board number (0-n)
 */
static __init int register_PCI(int i, struct pci_dev *dev)
{
	int num_aiops, aiop, max_num_aiops, num_chan, chan;
	unsigned int aiopio[MAX_AIOPS_PER_BOARD];
	char *str, *board_type;
	CONTROLLER_t *ctlp;

	int fast_clock = 0;
	int altChanRingIndicator = 0;
	int ports_per_aiop = 8;
	int ret;
	unsigned int class_rev;
	WordIO_t ConfigIO = 0;
	ByteIO_t UPCIRingInd = 0;

	if (!dev || pci_enable_device(dev))
		return 0;

	rcktpt_io_addr[i] = pci_resource_start(dev, 0);
	ret = pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);

	if (ret) {
		printk(KERN_INFO "  Error during register_PCI(), unable to read config dword \n");
		return 0;
	}

	rcktpt_type[i] = ROCKET_TYPE_NORMAL;
	rocketModel[i].loadrm2 = 0;
	rocketModel[i].startingPortNumber = nextLineNumber;

	/*  Depending on the model, set up some config variables */
	switch (dev->device) {
	case PCI_DEVICE_ID_RP4QUAD:
		str = "Quadcable";
		max_num_aiops = 1;
		ports_per_aiop = 4;
		rocketModel[i].model = MODEL_RP4QUAD;
		strcpy(rocketModel[i].modelString, "RocketPort 4 port w/quad cable");
		rocketModel[i].numPorts = 4;
		break;
	case PCI_DEVICE_ID_RP8OCTA:
		str = "Octacable";
		max_num_aiops = 1;
		rocketModel[i].model = MODEL_RP8OCTA;
		strcpy(rocketModel[i].modelString, "RocketPort 8 port w/octa cable");
		rocketModel[i].numPorts = 8;
		break;
	case PCI_DEVICE_ID_URP8OCTA:
		str = "Octacable";
		max_num_aiops = 1;
		rocketModel[i].model = MODEL_UPCI_RP8OCTA;
		strcpy(rocketModel[i].modelString, "RocketPort UPCI 8 port w/octa cable");
		rocketModel[i].numPorts = 8;
		break;
	case PCI_DEVICE_ID_RP8INTF:
		str = "8";
		max_num_aiops = 1;
		rocketModel[i].model = MODEL_RP8INTF;
		strcpy(rocketModel[i].modelString, "RocketPort 8 port w/external I/F");
		rocketModel[i].numPorts = 8;
		break;
	case PCI_DEVICE_ID_URP8INTF:
		str = "8";
		max_num_aiops = 1;
		rocketModel[i].model = MODEL_UPCI_RP8INTF;
		strcpy(rocketModel[i].modelString, "RocketPort UPCI 8 port w/external I/F");
		rocketModel[i].numPorts = 8;
		break;
	case PCI_DEVICE_ID_RP8J:
		str = "8J";
		max_num_aiops = 1;
		rocketModel[i].model = MODEL_RP8J;
		strcpy(rocketModel[i].modelString, "RocketPort 8 port w/RJ11 connectors");
		rocketModel[i].numPorts = 8;
		break;
	case PCI_DEVICE_ID_RP4J:
		str = "4J";
		max_num_aiops = 1;
		ports_per_aiop = 4;
		rocketModel[i].model = MODEL_RP4J;
		strcpy(rocketModel[i].modelString, "RocketPort 4 port w/RJ45 connectors");
		rocketModel[i].numPorts = 4;
		break;
	case PCI_DEVICE_ID_RP8SNI:
		str = "8 (DB78 Custom)";
		max_num_aiops = 1;
		rocketModel[i].model = MODEL_RP8SNI;
		strcpy(rocketModel[i].modelString, "RocketPort 8 port w/ custom DB78");
		rocketModel[i].numPorts = 8;
		break;
	case PCI_DEVICE_ID_RP16SNI:
		str = "16 (DB78 Custom)";
		max_num_aiops = 2;
		rocketModel[i].model = MODEL_RP16SNI;
		strcpy(rocketModel[i].modelString, "RocketPort 16 port w/ custom DB78");
		rocketModel[i].numPorts = 16;
		break;
	case PCI_DEVICE_ID_RP16INTF:
		str = "16";
		max_num_aiops = 2;
		rocketModel[i].model = MODEL_RP16INTF;
		strcpy(rocketModel[i].modelString, "RocketPort 16 port w/external I/F");
		rocketModel[i].numPorts = 16;
		break;
	case PCI_DEVICE_ID_URP16INTF:
		str = "16";
		max_num_aiops = 2;
		rocketModel[i].model = MODEL_UPCI_RP16INTF;
		strcpy(rocketModel[i].modelString, "RocketPort UPCI 16 port w/external I/F");
		rocketModel[i].numPorts = 16;
		break;
	case PCI_DEVICE_ID_CRP16INTF:
		str = "16";
		max_num_aiops = 2;
		rocketModel[i].model = MODEL_CPCI_RP16INTF;
		strcpy(rocketModel[i].modelString, "RocketPort Compact PCI 16 port w/external I/F");
		rocketModel[i].numPorts = 16;
		break;
	case PCI_DEVICE_ID_RP32INTF:
		str = "32";
		max_num_aiops = 4;
		rocketModel[i].model = MODEL_RP32INTF;
		strcpy(rocketModel[i].modelString, "RocketPort 32 port w/external I/F");
		rocketModel[i].numPorts = 32;
		break;
	case PCI_DEVICE_ID_URP32INTF:
		str = "32";
		max_num_aiops = 4;
		rocketModel[i].model = MODEL_UPCI_RP32INTF;
		strcpy(rocketModel[i].modelString, "RocketPort UPCI 32 port w/external I/F");
		rocketModel[i].numPorts = 32;
		break;
	case PCI_DEVICE_ID_RPP4:
		str = "Plus Quadcable";
		max_num_aiops = 1;
		ports_per_aiop = 4;
		altChanRingIndicator++;
		fast_clock++;
		rocketModel[i].model = MODEL_RPP4;
		strcpy(rocketModel[i].modelString, "RocketPort Plus 4 port");
		rocketModel[i].numPorts = 4;
		break;
	case PCI_DEVICE_ID_RPP8:
		str = "Plus Octacable";
		max_num_aiops = 2;
		ports_per_aiop = 4;
		altChanRingIndicator++;
		fast_clock++;
		rocketModel[i].model = MODEL_RPP8;
		strcpy(rocketModel[i].modelString, "RocketPort Plus 8 port");
		rocketModel[i].numPorts = 8;
		break;
	case PCI_DEVICE_ID_RP2_232:
		str = "Plus 2 (RS-232)";
		max_num_aiops = 1;
		ports_per_aiop = 2;
		altChanRingIndicator++;
		fast_clock++;
		rocketModel[i].model = MODEL_RP2_232;
		strcpy(rocketModel[i].modelString, "RocketPort Plus 2 port RS232");
		rocketModel[i].numPorts = 2;
		break;
	case PCI_DEVICE_ID_RP2_422:
		str = "Plus 2 (RS-422)";
		max_num_aiops = 1;
		ports_per_aiop = 2;
		altChanRingIndicator++;
		fast_clock++;
		rocketModel[i].model = MODEL_RP2_422;
		strcpy(rocketModel[i].modelString, "RocketPort Plus 2 port RS422");
		rocketModel[i].numPorts = 2;
		break;
	case PCI_DEVICE_ID_RP6M:

		max_num_aiops = 1;
		ports_per_aiop = 6;
		str = "6-port";

		/*  If class_rev is 1, the rocketmodem flash must be loaded.  If it is 2 it is a "socketed" version. */
		if ((class_rev & 0xFF) == 1) {
			rcktpt_type[i] = ROCKET_TYPE_MODEMII;
			rocketModel[i].loadrm2 = 1;
		} else {
			rcktpt_type[i] = ROCKET_TYPE_MODEM;
		}

		rocketModel[i].model = MODEL_RP6M;
		strcpy(rocketModel[i].modelString, "RocketModem 6 port");
		rocketModel[i].numPorts = 6;
		break;
	case PCI_DEVICE_ID_RP4M:
		max_num_aiops = 1;
		ports_per_aiop = 4;
		str = "4-port";
		if ((class_rev & 0xFF) == 1) {
			rcktpt_type[i] = ROCKET_TYPE_MODEMII;
			rocketModel[i].loadrm2 = 1;
		} else {
			rcktpt_type[i] = ROCKET_TYPE_MODEM;
		}

		rocketModel[i].model = MODEL_RP4M;
		strcpy(rocketModel[i].modelString, "RocketModem 4 port");
		rocketModel[i].numPorts = 4;
		break;
	default:
		str = "(unknown/unsupported)";
		max_num_aiops = 0;
		break;
	}

	/*
	 * Check for UPCI boards.
	 */

	switch (dev->device) {
	case PCI_DEVICE_ID_URP32INTF:
	case PCI_DEVICE_ID_URP8INTF:
	case PCI_DEVICE_ID_URP16INTF:
	case PCI_DEVICE_ID_CRP16INTF:
	case PCI_DEVICE_ID_URP8OCTA:
		rcktpt_io_addr[i] = pci_resource_start(dev, 2);
		ConfigIO = pci_resource_start(dev, 1);
		if (dev->device == PCI_DEVICE_ID_URP8OCTA) {
			UPCIRingInd = rcktpt_io_addr[i] + _PCI_9030_RING_IND;

			/*
			 * Check for octa or quad cable.
			 */
			if (!
			    (sInW(ConfigIO + _PCI_9030_GPIO_CTRL) &
			     PCI_GPIO_CTRL_8PORT)) {
				str = "Quadcable";
				ports_per_aiop = 4;
				rocketModel[i].numPorts = 4;
			}
		}
		break;
	case PCI_DEVICE_ID_UPCI_RM3_8PORT:
		str = "8 ports";
		max_num_aiops = 1;
		rocketModel[i].model = MODEL_UPCI_RM3_8PORT;
		strcpy(rocketModel[i].modelString, "RocketModem III 8 port");
		rocketModel[i].numPorts = 8;
		rcktpt_io_addr[i] = pci_resource_start(dev, 2);
		UPCIRingInd = rcktpt_io_addr[i] + _PCI_9030_RING_IND;
		ConfigIO = pci_resource_start(dev, 1);
		rcktpt_type[i] = ROCKET_TYPE_MODEMIII;
		break;
	case PCI_DEVICE_ID_UPCI_RM3_4PORT:
		str = "4 ports";
		max_num_aiops = 1;
		rocketModel[i].model = MODEL_UPCI_RM3_4PORT;
		strcpy(rocketModel[i].modelString, "RocketModem III 4 port");
		rocketModel[i].numPorts = 4;
		rcktpt_io_addr[i] = pci_resource_start(dev, 2);
		UPCIRingInd = rcktpt_io_addr[i] + _PCI_9030_RING_IND;
		ConfigIO = pci_resource_start(dev, 1);
		rcktpt_type[i] = ROCKET_TYPE_MODEMIII;
		break;
	default:
		break;
	}

	switch (rcktpt_type[i]) {
	case ROCKET_TYPE_MODEM:
		board_type = "RocketModem";
		break;
	case ROCKET_TYPE_MODEMII:
		board_type = "RocketModem II";
		break;
	case ROCKET_TYPE_MODEMIII:
		board_type = "RocketModem III";
		break;
	default:
		board_type = "RocketPort";
		break;
	}

	if (fast_clock) {
		sClockPrescale = 0x12;	/* mod 2 (divide by 3) */
		rp_baud_base[i] = 921600;
	} else {
		/*
		 * If support_low_speed is set, use the slow clock
		 * prescale, which supports 50 bps
		 */
		if (support_low_speed) {
			/* mod 9 (divide by 10) prescale */
			sClockPrescale = 0x19;
			rp_baud_base[i] = 230400;
		} else {
			/* mod 4 (devide by 5) prescale */
			sClockPrescale = 0x14;
			rp_baud_base[i] = 460800;
		}
	}

	for (aiop = 0; aiop < max_num_aiops; aiop++)
		aiopio[aiop] = rcktpt_io_addr[i] + (aiop * 0x40);
	ctlp = sCtlNumToCtlPtr(i);
	num_aiops = sPCIInitController(ctlp, i, aiopio, max_num_aiops, ConfigIO, 0, FREQ_DIS, 0, altChanRingIndicator, UPCIRingInd);
	for (aiop = 0; aiop < max_num_aiops; aiop++)
		ctlp->AiopNumChan[aiop] = ports_per_aiop;

	printk("Comtrol PCI controller #%d ID 0x%x found in bus:slot:fn %s at address %04lx, "
	     "%d AIOP(s) (%s)\n", i, dev->device, pci_name(dev),
	     rcktpt_io_addr[i], num_aiops, rocketModel[i].modelString);
	printk(KERN_INFO "Installing %s, creating /dev/ttyR%d - %ld\n",
	       rocketModel[i].modelString,
	       rocketModel[i].startingPortNumber,
	       rocketModel[i].startingPortNumber +
	       rocketModel[i].numPorts - 1);

	if (num_aiops <= 0) {
		rcktpt_io_addr[i] = 0;
		return (0);
	}
	is_PCI[i] = 1;

	/*  Reset the AIOPIC, init the serial ports */
	for (aiop = 0; aiop < num_aiops; aiop++) {
		sResetAiopByNum(ctlp, aiop);
		num_chan = ports_per_aiop;
		for (chan = 0; chan < num_chan; chan++)
			init_r_port(i, aiop, chan, dev);
	}

	/*  Rocket modems must be reset */
	if ((rcktpt_type[i] == ROCKET_TYPE_MODEM) ||
	    (rcktpt_type[i] == ROCKET_TYPE_MODEMII) ||
	    (rcktpt_type[i] == ROCKET_TYPE_MODEMIII)) {
		num_chan = ports_per_aiop;
		for (chan = 0; chan < num_chan; chan++)
			sPCIModemReset(ctlp, chan, 1);
		mdelay(500);
		for (chan = 0; chan < num_chan; chan++)
			sPCIModemReset(ctlp, chan, 0);
		mdelay(500);
		rmSpeakerReset(ctlp, rocketModel[i].model);
	}
	return (1);
}

/*
 *  Probes for PCI cards, inits them if found
 *  Input:   board_found = number of ISA boards already found, or the
 *           starting board number
 *  Returns: Number of PCI boards found
 */
static int __init init_PCI(int boards_found)
{
	struct pci_dev *dev = NULL;
	int count = 0;

	/*  Work through the PCI device list, pulling out ours */
	while ((dev = pci_find_device(PCI_VENDOR_ID_RP, PCI_ANY_ID, dev))) {
		if (register_PCI(count + boards_found, dev))
			count++;
	}
	return (count);
}

#endif				/* CONFIG_PCI */

/*
 *  Probes for ISA cards
 *  Input:   i = the board number to look for
 *  Returns: 1 if board found, 0 else
 */
static int __init init_ISA(int i)
{
	int num_aiops, num_chan = 0, total_num_chan = 0;
	int aiop, chan;
	unsigned int aiopio[MAX_AIOPS_PER_BOARD];
	CONTROLLER_t *ctlp;
	char *type_string;

	/*  If io_addr is zero, no board configured */
	if (rcktpt_io_addr[i] == 0)
		return (0);

	/*  Reserve the IO region */
	if (!request_region(rcktpt_io_addr[i], 64, "Comtrol RocketPort")) {
		printk(KERN_INFO "Unable to reserve IO region for configured ISA RocketPort at address 0x%lx, board not installed...\n", rcktpt_io_addr[i]);
		rcktpt_io_addr[i] = 0;
		return (0);
	}

	ctlp = sCtlNumToCtlPtr(i);

	ctlp->boardType = rcktpt_type[i];

	switch (rcktpt_type[i]) {
	case ROCKET_TYPE_PC104:
		type_string = "(PC104)";
		break;
	case ROCKET_TYPE_MODEM:
		type_string = "(RocketModem)";
		break;
	case ROCKET_TYPE_MODEMII:
		type_string = "(RocketModem II)";
		break;
	default:
		type_string = "";
		break;
	}

	/*
	 * If support_low_speed is set, use the slow clock prescale,
	 * which supports 50 bps
	 */
	if (support_low_speed) {
		sClockPrescale = 0x19;	/* mod 9 (divide by 10) prescale */
		rp_baud_base[i] = 230400;
	} else {
		sClockPrescale = 0x14;	/* mod 4 (devide by 5) prescale */
		rp_baud_base[i] = 460800;
	}

	for (aiop = 0; aiop < MAX_AIOPS_PER_BOARD; aiop++)
		aiopio[aiop] = rcktpt_io_addr[i] + (aiop * 0x400);

	num_aiops = sInitController(ctlp, i, controller + (i * 0x400), aiopio,  MAX_AIOPS_PER_BOARD, 0, FREQ_DIS, 0);

	if (ctlp->boardType == ROCKET_TYPE_PC104) {
		sEnAiop(ctlp, 2);	/* only one AIOPIC, but these */
		sEnAiop(ctlp, 3);	/* CSels used for other stuff */
	}

	/*  If something went wrong initing the AIOP's release the ISA IO memory */
	if (num_aiops <= 0) {
		release_region(rcktpt_io_addr[i], 64);
		rcktpt_io_addr[i] = 0;
		return (0);
	}
  
	rocketModel[i].startingPortNumber = nextLineNumber;

	for (aiop = 0; aiop < num_aiops; aiop++) {
		sResetAiopByNum(ctlp, aiop);
		sEnAiop(ctlp, aiop);
		num_chan = sGetAiopNumChan(ctlp, aiop);
		total_num_chan += num_chan;
		for (chan = 0; chan < num_chan; chan++)
			init_r_port(i, aiop, chan, NULL);
	}
	is_PCI[i] = 0;
	if ((rcktpt_type[i] == ROCKET_TYPE_MODEM) || (rcktpt_type[i] == ROCKET_TYPE_MODEMII)) {
		num_chan = sGetAiopNumChan(ctlp, 0);
		total_num_chan = num_chan;
		for (chan = 0; chan < num_chan; chan++)
			sModemReset(ctlp, chan, 1);
		mdelay(500);
		for (chan = 0; chan < num_chan; chan++)
			sModemReset(ctlp, chan, 0);
		mdelay(500);
		strcpy(rocketModel[i].modelString, "RocketModem ISA");
	} else {
		strcpy(rocketModel[i].modelString, "RocketPort ISA");
	}
	rocketModel[i].numPorts = total_num_chan;
	rocketModel[i].model = MODEL_ISA;

	printk(KERN_INFO "RocketPort ISA card #%d found at 0x%lx - %d AIOPs %s\n", 
	       i, rcktpt_io_addr[i], num_aiops, type_string);

	printk(KERN_INFO "Installing %s, creating /dev/ttyR%d - %ld\n",
	       rocketModel[i].modelString,
	       rocketModel[i].startingPortNumber,
	       rocketModel[i].startingPortNumber +
	       rocketModel[i].numPorts - 1);

	return (1);
}

static struct tty_operations rocket_ops = {
	.open = rp_open,
	.close = rp_close,
	.write = rp_write,
	.put_char = rp_put_char,
	.write_room = rp_write_room,
	.chars_in_buffer = rp_chars_in_buffer,
	.flush_buffer = rp_flush_buffer,
	.ioctl = rp_ioctl,
	.throttle = rp_throttle,
	.unthrottle = rp_unthrottle,
	.set_termios = rp_set_termios,
	.stop = rp_stop,
	.start = rp_start,
	.hangup = rp_hangup,
	.break_ctl = rp_break,
	.send_xchar = rp_send_xchar,
	.wait_until_sent = rp_wait_until_sent,
	.tiocmget = rp_tiocmget,
	.tiocmset = rp_tiocmset,
};

/*
 * The module "startup" routine; it's run when the module is loaded.
 */
static int __init rp_init(void)
{
	int retval, pci_boards_found, isa_boards_found, i;

	printk(KERN_INFO "RocketPort device driver module, version %s, %s\n",
	       ROCKET_VERSION, ROCKET_DATE);

	rocket_driver = alloc_tty_driver(MAX_RP_PORTS);
	if (!rocket_driver)
		return -ENOMEM;

	/*
	 * Set up the timer channel.
	 */
	init_timer(&rocket_timer);
	rocket_timer.function = rp_do_poll;

	/*
	 * Initialize the array of pointers to our own internal state
	 * structures.
	 */
	memset(rp_table, 0, sizeof (rp_table));
	memset(xmit_flags, 0, sizeof (xmit_flags));

	for (i = 0; i < MAX_RP_PORTS; i++)
		lineNumbers[i] = 0;
	nextLineNumber = 0;
	memset(rocketModel, 0, sizeof (rocketModel));

	/*
	 *  If board 1 is non-zero, there is at least one ISA configured.  If controller is 
	 *  zero, use the default controller IO address of board1 + 0x40.
	 */
	if (board1) {
		if (controller == 0)
			controller = board1 + 0x40;
	} else {
		controller = 0;  /*  Used as a flag, meaning no ISA boards */
	}

	/*  If an ISA card is configured, reserve the 4 byte IO space for the Mudbac controller */
	if (controller && (!request_region(controller, 4, "Comtrol RocketPort"))) {
		printk(KERN_INFO "Unable to reserve IO region for first configured ISA RocketPort controller 0x%lx.  Driver exiting \n", controller);
		return -EBUSY;
	}

	/*  Store ISA variable retrieved from command line or .conf file. */
	rcktpt_io_addr[0] = board1;
	rcktpt_io_addr[1] = board2;
	rcktpt_io_addr[2] = board3;
	rcktpt_io_addr[3] = board4;

	rcktpt_type[0] = modem1 ? ROCKET_TYPE_MODEM : ROCKET_TYPE_NORMAL;
	rcktpt_type[0] = pc104_1[0] ? ROCKET_TYPE_PC104 : rcktpt_type[0];
	rcktpt_type[1] = modem2 ? ROCKET_TYPE_MODEM : ROCKET_TYPE_NORMAL;
	rcktpt_type[1] = pc104_2[0] ? ROCKET_TYPE_PC104 : rcktpt_type[1];
	rcktpt_type[2] = modem3 ? ROCKET_TYPE_MODEM : ROCKET_TYPE_NORMAL;
	rcktpt_type[2] = pc104_3[0] ? ROCKET_TYPE_PC104 : rcktpt_type[2];
	rcktpt_type[3] = modem4 ? ROCKET_TYPE_MODEM : ROCKET_TYPE_NORMAL;
	rcktpt_type[3] = pc104_4[0] ? ROCKET_TYPE_PC104 : rcktpt_type[3];

	/*
	 * Set up the tty driver structure and then register this
	 * driver with the tty layer.
	 */

	rocket_driver->owner = THIS_MODULE;
	rocket_driver->flags = TTY_DRIVER_NO_DEVFS;
	rocket_driver->devfs_name = "tts/R";
	rocket_driver->name = "ttyR";
	rocket_driver->driver_name = "Comtrol RocketPort";
	rocket_driver->major = TTY_ROCKET_MAJOR;
	rocket_driver->minor_start = 0;
	rocket_driver->type = TTY_DRIVER_TYPE_SERIAL;
	rocket_driver->subtype = SERIAL_TYPE_NORMAL;
	rocket_driver->init_termios = tty_std_termios;
	rocket_driver->init_termios.c_cflag =
	    B9600 | CS8 | CREAD | HUPCL | CLOCAL;
#ifdef ROCKET_SOFT_FLOW
	rocket_driver->flags |= TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
#endif
	tty_set_operations(rocket_driver, &rocket_ops);

	retval = tty_register_driver(rocket_driver);
	if (retval < 0) {
		printk(KERN_INFO "Couldn't install tty RocketPort driver (error %d)\n", -retval);
		put_tty_driver(rocket_driver);
		return -1;
	}

#ifdef ROCKET_DEBUG_OPEN
	printk(KERN_INFO "RocketPort driver is major %d\n", rocket_driver.major);
#endif

	/*
	 *  OK, let's probe each of the controllers looking for boards.  Any boards found
         *  will be initialized here.
	 */
	isa_boards_found = 0;
	pci_boards_found = 0;

	for (i = 0; i < NUM_BOARDS; i++) {
		if (init_ISA(i))
			isa_boards_found++;
	}

#ifdef CONFIG_PCI
	if (isa_boards_found < NUM_BOARDS)
		pci_boards_found = init_PCI(isa_boards_found);
#endif

	max_board = pci_boards_found + isa_boards_found;

	if (max_board == 0) {
		printk(KERN_INFO "No rocketport ports found; unloading driver.\n");
		del_timer_sync(&rocket_timer);
		tty_unregister_driver(rocket_driver);
		put_tty_driver(rocket_driver);
		return -ENXIO;
	}

	return 0;
}


static void rp_cleanup_module(void)
{
	int retval;
	int i;

	del_timer_sync(&rocket_timer);

	retval = tty_unregister_driver(rocket_driver);
	if (retval)
		printk(KERN_INFO "Error %d while trying to unregister "
		       "rocketport driver\n", -retval);
	put_tty_driver(rocket_driver);

	for (i = 0; i < MAX_RP_PORTS; i++)
		kfree(rp_table[i]);

	for (i = 0; i < NUM_BOARDS; i++) {
		if (rcktpt_io_addr[i] <= 0 || is_PCI[i])
			continue;
		release_region(rcktpt_io_addr[i], 64);
	}
	if (controller)
		release_region(controller, 4);
}

/***************************************************************************
Function: sInitController
Purpose:  Initialization of controller global registers and controller
          structure.
Call:     sInitController(CtlP,CtlNum,MudbacIO,AiopIOList,AiopIOListSize,
                          IRQNum,Frequency,PeriodicOnly)
          CONTROLLER_T *CtlP; Ptr to controller structure
          int CtlNum; Controller number
          ByteIO_t MudbacIO; Mudbac base I/O address.
          ByteIO_t *AiopIOList; List of I/O addresses for each AIOP.
             This list must be in the order the AIOPs will be found on the
             controller.  Once an AIOP in the list is not found, it is
             assumed that there are no more AIOPs on the controller.
          int AiopIOListSize; Number of addresses in AiopIOList
          int IRQNum; Interrupt Request number.  Can be any of the following:
                         0: Disable global interrupts
                         3: IRQ 3
                         4: IRQ 4
                         5: IRQ 5
                         9: IRQ 9
                         10: IRQ 10
                         11: IRQ 11
                         12: IRQ 12
                         15: IRQ 15
          Byte_t Frequency: A flag identifying the frequency
                   of the periodic interrupt, can be any one of the following:
                      FREQ_DIS - periodic interrupt disabled
                      FREQ_137HZ - 137 Hertz
                      FREQ_69HZ - 69 Hertz
                      FREQ_34HZ - 34 Hertz
                      FREQ_17HZ - 17 Hertz
                      FREQ_9HZ - 9 Hertz
                      FREQ_4HZ - 4 Hertz
                   If IRQNum is set to 0 the Frequency parameter is
                   overidden, it is forced to a value of FREQ_DIS.
          int PeriodicOnly: 1 if all interrupts except the periodic
                               interrupt are to be blocked.
                            0 is both the periodic interrupt and
                               other channel interrupts are allowed.
                            If IRQNum is set to 0 the PeriodicOnly parameter is
                               overidden, it is forced to a value of 0.
Return:   int: Number of AIOPs on the controller, or CTLID_NULL if controller
               initialization failed.

Comments:
          If periodic interrupts are to be disabled but AIOP interrupts
          are allowed, set Frequency to FREQ_DIS and PeriodicOnly to 0.

          If interrupts are to be completely disabled set IRQNum to 0.

          Setting Frequency to FREQ_DIS and PeriodicOnly to 1 is an
          invalid combination.

          This function performs initialization of global interrupt modes,
          but it does not actually enable global interrupts.  To enable
          and disable global interrupts use functions sEnGlobalInt() and
          sDisGlobalInt().  Enabling of global interrupts is normally not
          done until all other initializations are complete.

          Even if interrupts are globally enabled, they must also be
          individually enabled for each channel that is to generate
          interrupts.

Warnings: No range checking on any of the parameters is done.

          No context switches are allowed while executing this function.

          After this function all AIOPs on the controller are disabled,
          they can be enabled with sEnAiop().
*/
static int sInitController(CONTROLLER_T * CtlP, int CtlNum, ByteIO_t MudbacIO,
			   ByteIO_t * AiopIOList, int AiopIOListSize,
			   int IRQNum, Byte_t Frequency, int PeriodicOnly)
{
	int i;
	ByteIO_t io;
	int done;

	CtlP->AiopIntrBits = aiop_intr_bits;
	CtlP->AltChanRingIndicator = 0;
	CtlP->CtlNum = CtlNum;
	CtlP->CtlID = CTLID_0001;	/* controller release 1 */
	CtlP->BusType = isISA;
	CtlP->MBaseIO = MudbacIO;
	CtlP->MReg1IO = MudbacIO + 1;
	CtlP->MReg2IO = MudbacIO + 2;
	CtlP->MReg3IO = MudbacIO + 3;
#if 1
	CtlP->MReg2 = 0;	/* interrupt disable */
	CtlP->MReg3 = 0;	/* no periodic interrupts */
#else
	if (sIRQMap[IRQNum] == 0) {	/* interrupts globally disabled */
		CtlP->MReg2 = 0;	/* interrupt disable */
		CtlP->MReg3 = 0;	/* no periodic interrupts */
	} else {
		CtlP->MReg2 = sIRQMap[IRQNum];	/* set IRQ number */
		CtlP->MReg3 = Frequency;	/* set frequency */
		if (PeriodicOnly) {	/* periodic interrupt only */
			CtlP->MReg3 |= PERIODIC_ONLY;
		}
	}
#endif
	sOutB(CtlP->MReg2IO, CtlP->MReg2);
	sOutB(CtlP->MReg3IO, CtlP->MReg3);
	sControllerEOI(CtlP);	/* clear EOI if warm init */
	/* Init AIOPs */
	CtlP->NumAiop = 0;
	for (i = done = 0; i < AiopIOListSize; i++) {
		io = AiopIOList[i];
		CtlP->AiopIO[i] = (WordIO_t) io;
		CtlP->AiopIntChanIO[i] = io + _INT_CHAN;
		sOutB(CtlP->MReg2IO, CtlP->MReg2 | (i & 0x03));	/* AIOP index */
		sOutB(MudbacIO, (Byte_t) (io >> 6));	/* set up AIOP I/O in MUDBAC */
		if (done)
			continue;
		sEnAiop(CtlP, i);	/* enable the AIOP */
		CtlP->AiopID[i] = sReadAiopID(io);	/* read AIOP ID */
		if (CtlP->AiopID[i] == AIOPID_NULL)	/* if AIOP does not exist */
			done = 1;	/* done looking for AIOPs */
		else {
			CtlP->AiopNumChan[i] = sReadAiopNumChan((WordIO_t) io);	/* num channels in AIOP */
			sOutW((WordIO_t) io + _INDX_ADDR, _CLK_PRE);	/* clock prescaler */
			sOutB(io + _INDX_DATA, sClockPrescale);
			CtlP->NumAiop++;	/* bump count of AIOPs */
		}
		sDisAiop(CtlP, i);	/* disable AIOP */
	}

	if (CtlP->NumAiop == 0)
		return (-1);
	else
		return (CtlP->NumAiop);
}

/***************************************************************************
Function: sPCIInitController
Purpose:  Initialization of controller global registers and controller
          structure.
Call:     sPCIInitController(CtlP,CtlNum,AiopIOList,AiopIOListSize,
                          IRQNum,Frequency,PeriodicOnly)
          CONTROLLER_T *CtlP; Ptr to controller structure
          int CtlNum; Controller number
          ByteIO_t *AiopIOList; List of I/O addresses for each AIOP.
             This list must be in the order the AIOPs will be found on the
             controller.  Once an AIOP in the list is not found, it is
             assumed that there are no more AIOPs on the controller.
          int AiopIOListSize; Number of addresses in AiopIOList
          int IRQNum; Interrupt Request number.  Can be any of the following:
                         0: Disable global interrupts
                         3: IRQ 3
                         4: IRQ 4
                         5: IRQ 5
                         9: IRQ 9
                         10: IRQ 10
                         11: IRQ 11
                         12: IRQ 12
                         15: IRQ 15
          Byte_t Frequency: A flag identifying the frequency
                   of the periodic interrupt, can be any one of the following:
                      FREQ_DIS - periodic interrupt disabled
                      FREQ_137HZ - 137 Hertz
                      FREQ_69HZ - 69 Hertz
                      FREQ_34HZ - 34 Hertz
                      FREQ_17HZ - 17 Hertz
                      FREQ_9HZ - 9 Hertz
                      FREQ_4HZ - 4 Hertz
                   If IRQNum is set to 0 the Frequency parameter is
                   overidden, it is forced to a value of FREQ_DIS.
          int PeriodicOnly: 1 if all interrupts except the periodic
                               interrupt are to be blocked.
                            0 is both the periodic interrupt and
                               other channel interrupts are allowed.
                            If IRQNum is set to 0 the PeriodicOnly parameter is
                               overidden, it is forced to a value of 0.
Return:   int: Number of AIOPs on the controller, or CTLID_NULL if controller
               initialization failed.

Comments:
          If periodic interrupts are to be disabled but AIOP interrupts
          are allowed, set Frequency to FREQ_DIS and PeriodicOnly to 0.

          If interrupts are to be completely disabled set IRQNum to 0.

          Setting Frequency to FREQ_DIS and PeriodicOnly to 1 is an
          invalid combination.

          This function performs initialization of global interrupt modes,
          but it does not actually enable global interrupts.  To enable
          and disable global interrupts use functions sEnGlobalInt() and
          sDisGlobalInt().  Enabling of global interrupts is normally not
          done until all other initializations are complete.

          Even if interrupts are globally enabled, they must also be
          individually enabled for each channel that is to generate
          interrupts.

Warnings: No range checking on any of the parameters is done.

          No context switches are allowed while executing this function.

          After this function all AIOPs on the controller are disabled,
          they can be enabled with sEnAiop().
*/
static int sPCIInitController(CONTROLLER_T * CtlP, int CtlNum,
			      ByteIO_t * AiopIOList, int AiopIOListSize,
			      WordIO_t ConfigIO, int IRQNum, Byte_t Frequency,
			      int PeriodicOnly, int altChanRingIndicator,
			      int UPCIRingInd)
{
	int i;
	ByteIO_t io;

	CtlP->AltChanRingIndicator = altChanRingIndicator;
	CtlP->UPCIRingInd = UPCIRingInd;
	CtlP->CtlNum = CtlNum;
	CtlP->CtlID = CTLID_0001;	/* controller release 1 */
	CtlP->BusType = isPCI;	/* controller release 1 */

	if (ConfigIO) {
		CtlP->isUPCI = 1;
		CtlP->PCIIO = ConfigIO + _PCI_9030_INT_CTRL;
		CtlP->PCIIO2 = ConfigIO + _PCI_9030_GPIO_CTRL;
		CtlP->AiopIntrBits = upci_aiop_intr_bits;
	} else {
		CtlP->isUPCI = 0;
		CtlP->PCIIO =
		    (WordIO_t) ((ByteIO_t) AiopIOList[0] + _PCI_INT_FUNC);
		CtlP->AiopIntrBits = aiop_intr_bits;
	}

	sPCIControllerEOI(CtlP);	/* clear EOI if warm init */
	/* Init AIOPs */
	CtlP->NumAiop = 0;
	for (i = 0; i < AiopIOListSize; i++) {
		io = AiopIOList[i];
		CtlP->AiopIO[i] = (WordIO_t) io;
		CtlP->AiopIntChanIO[i] = io + _INT_CHAN;

		CtlP->AiopID[i] = sReadAiopID(io);	/* read AIOP ID */
		if (CtlP->AiopID[i] == AIOPID_NULL)	/* if AIOP does not exist */
			break;	/* done looking for AIOPs */

		CtlP->AiopNumChan[i] = sReadAiopNumChan((WordIO_t) io);	/* num channels in AIOP */
		sOutW((WordIO_t) io + _INDX_ADDR, _CLK_PRE);	/* clock prescaler */
		sOutB(io + _INDX_DATA, sClockPrescale);
		CtlP->NumAiop++;	/* bump count of AIOPs */
	}

	if (CtlP->NumAiop == 0)
		return (-1);
	else
		return (CtlP->NumAiop);
}

/***************************************************************************
Function: sReadAiopID
Purpose:  Read the AIOP idenfication number directly from an AIOP.
Call:     sReadAiopID(io)
          ByteIO_t io: AIOP base I/O address
Return:   int: Flag AIOPID_XXXX if a valid AIOP is found, where X
                 is replace by an identifying number.
          Flag AIOPID_NULL if no valid AIOP is found
Warnings: No context switches are allowed while executing this function.

*/
static int sReadAiopID(ByteIO_t io)
{
	Byte_t AiopID;		/* ID byte from AIOP */

	sOutB(io + _CMD_REG, RESET_ALL);	/* reset AIOP */
	sOutB(io + _CMD_REG, 0x0);
	AiopID = sInW(io + _CHN_STAT0) & 0x07;
	if (AiopID == 0x06)
		return (1);
	else			/* AIOP does not exist */
		return (-1);
}

/***************************************************************************
Function: sReadAiopNumChan
Purpose:  Read the number of channels available in an AIOP directly from
          an AIOP.
Call:     sReadAiopNumChan(io)
          WordIO_t io: AIOP base I/O address
Return:   int: The number of channels available
Comments: The number of channels is determined by write/reads from identical
          offsets within the SRAM address spaces for channels 0 and 4.
          If the channel 4 space is mirrored to channel 0 it is a 4 channel
          AIOP, otherwise it is an 8 channel.
Warnings: No context switches are allowed while executing this function.
*/
static int sReadAiopNumChan(WordIO_t io)
{
	Word_t x;
	static Byte_t R[4] = { 0x00, 0x00, 0x34, 0x12 };

	/* write to chan 0 SRAM */
	sOutDW((DWordIO_t) io + _INDX_ADDR, *((DWord_t *) & R[0]));
	sOutW(io + _INDX_ADDR, 0);	/* read from SRAM, chan 0 */
	x = sInW(io + _INDX_DATA);
	sOutW(io + _INDX_ADDR, 0x4000);	/* read from SRAM, chan 4 */
	if (x != sInW(io + _INDX_DATA))	/* if different must be 8 chan */
		return (8);
	else
		return (4);
}

/***************************************************************************
Function: sInitChan
Purpose:  Initialization of a channel and channel structure
Call:     sInitChan(CtlP,ChP,AiopNum,ChanNum)
          CONTROLLER_T *CtlP; Ptr to controller structure
          CHANNEL_T *ChP; Ptr to channel structure
          int AiopNum; AIOP number within controller
          int ChanNum; Channel number within AIOP
Return:   int: 1 if initialization succeeded, 0 if it fails because channel
               number exceeds number of channels available in AIOP.
Comments: This function must be called before a channel can be used.
Warnings: No range checking on any of the parameters is done.

          No context switches are allowed while executing this function.
*/
static int sInitChan(CONTROLLER_T * CtlP, CHANNEL_T * ChP, int AiopNum,
		     int ChanNum)
{
	int i;
	WordIO_t AiopIO;
	WordIO_t ChIOOff;
	Byte_t *ChR;
	Word_t ChOff;
	static Byte_t R[4];
	int brd9600;

	if (ChanNum >= CtlP->AiopNumChan[AiopNum])
		return 0;	/* exceeds num chans in AIOP */

	/* Channel, AIOP, and controller identifiers */
	ChP->CtlP = CtlP;
	ChP->ChanID = CtlP->AiopID[AiopNum];
	ChP->AiopNum = AiopNum;
	ChP->ChanNum = ChanNum;

	/* Global direct addresses */
	AiopIO = CtlP->AiopIO[AiopNum];
	ChP->Cmd = (ByteIO_t) AiopIO + _CMD_REG;
	ChP->IntChan = (ByteIO_t) AiopIO + _INT_CHAN;
	ChP->IntMask = (ByteIO_t) AiopIO + _INT_MASK;
	ChP->IndexAddr = (DWordIO_t) AiopIO + _INDX_ADDR;
	ChP->IndexData = AiopIO + _INDX_DATA;

	/* Channel direct addresses */
	ChIOOff = AiopIO + ChP->ChanNum * 2;
	ChP->TxRxData = ChIOOff + _TD0;
	ChP->ChanStat = ChIOOff + _CHN_STAT0;
	ChP->TxRxCount = ChIOOff + _FIFO_CNT0;
	ChP->IntID = (ByteIO_t) AiopIO + ChP->ChanNum + _INT_ID0;

	/* Initialize the channel from the RData array */
	for (i = 0; i < RDATASIZE; i += 4) {
		R[0] = RData[i];
		R[1] = RData[i + 1] + 0x10 * ChanNum;
		R[2] = RData[i + 2];
		R[3] = RData[i + 3];
		sOutDW(ChP->IndexAddr, *((DWord_t *) & R[0]));
	}

	ChR = ChP->R;
	for (i = 0; i < RREGDATASIZE; i += 4) {
		ChR[i] = RRegData[i];
		ChR[i + 1] = RRegData[i + 1] + 0x10 * ChanNum;
		ChR[i + 2] = RRegData[i + 2];
		ChR[i + 3] = RRegData[i + 3];
	}

	/* Indexed registers */
	ChOff = (Word_t) ChanNum *0x1000;

	if (sClockPrescale == 0x14)
		brd9600 = 47;
	else
		brd9600 = 23;

	ChP->BaudDiv[0] = (Byte_t) (ChOff + _BAUD);
	ChP->BaudDiv[1] = (Byte_t) ((ChOff + _BAUD) >> 8);
	ChP->BaudDiv[2] = (Byte_t) brd9600;
	ChP->BaudDiv[3] = (Byte_t) (brd9600 >> 8);
	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->BaudDiv[0]);

	ChP->TxControl[0] = (Byte_t) (ChOff + _TX_CTRL);
	ChP->TxControl[1] = (Byte_t) ((ChOff + _TX_CTRL) >> 8);
	ChP->TxControl[2] = 0;
	ChP->TxControl[3] = 0;
	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->TxControl[0]);

	ChP->RxControl[0] = (Byte_t) (ChOff + _RX_CTRL);
	ChP->RxControl[1] = (Byte_t) ((ChOff + _RX_CTRL) >> 8);
	ChP->RxControl[2] = 0;
	ChP->RxControl[3] = 0;
	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->RxControl[0]);

	ChP->TxEnables[0] = (Byte_t) (ChOff + _TX_ENBLS);
	ChP->TxEnables[1] = (Byte_t) ((ChOff + _TX_ENBLS) >> 8);
	ChP->TxEnables[2] = 0;
	ChP->TxEnables[3] = 0;
	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->TxEnables[0]);

	ChP->TxCompare[0] = (Byte_t) (ChOff + _TXCMP1);
	ChP->TxCompare[1] = (Byte_t) ((ChOff + _TXCMP1) >> 8);
	ChP->TxCompare[2] = 0;
	ChP->TxCompare[3] = 0;
	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->TxCompare[0]);

	ChP->TxReplace1[0] = (Byte_t) (ChOff + _TXREP1B1);
	ChP->TxReplace1[1] = (Byte_t) ((ChOff + _TXREP1B1) >> 8);
	ChP->TxReplace1[2] = 0;
	ChP->TxReplace1[3] = 0;
	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->TxReplace1[0]);

	ChP->TxReplace2[0] = (Byte_t) (ChOff + _TXREP2);
	ChP->TxReplace2[1] = (Byte_t) ((ChOff + _TXREP2) >> 8);
	ChP->TxReplace2[2] = 0;
	ChP->TxReplace2[3] = 0;
	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->TxReplace2[0]);

	ChP->TxFIFOPtrs = ChOff + _TXF_OUTP;
	ChP->TxFIFO = ChOff + _TX_FIFO;

	sOutB(ChP->Cmd, (Byte_t) ChanNum | RESTXFCNT);	/* apply reset Tx FIFO count */
	sOutB(ChP->Cmd, (Byte_t) ChanNum);	/* remove reset Tx FIFO count */
	sOutW((WordIO_t) ChP->IndexAddr, ChP->TxFIFOPtrs);	/* clear Tx in/out ptrs */
	sOutW(ChP->IndexData, 0);
	ChP->RxFIFOPtrs = ChOff + _RXF_OUTP;
	ChP->RxFIFO = ChOff + _RX_FIFO;

	sOutB(ChP->Cmd, (Byte_t) ChanNum | RESRXFCNT);	/* apply reset Rx FIFO count */
	sOutB(ChP->Cmd, (Byte_t) ChanNum);	/* remove reset Rx FIFO count */
	sOutW((WordIO_t) ChP->IndexAddr, ChP->RxFIFOPtrs);	/* clear Rx out ptr */
	sOutW(ChP->IndexData, 0);
	sOutW((WordIO_t) ChP->IndexAddr, ChP->RxFIFOPtrs + 2);	/* clear Rx in ptr */
	sOutW(ChP->IndexData, 0);
	ChP->TxPrioCnt = ChOff + _TXP_CNT;
	sOutW((WordIO_t) ChP->IndexAddr, ChP->TxPrioCnt);
	sOutB(ChP->IndexData, 0);
	ChP->TxPrioPtr = ChOff + _TXP_PNTR;
	sOutW((WordIO_t) ChP->IndexAddr, ChP->TxPrioPtr);
	sOutB(ChP->IndexData, 0);
	ChP->TxPrioBuf = ChOff + _TXP_BUF;
	sEnRxProcessor(ChP);	/* start the Rx processor */

	return 1;
}

/***************************************************************************
Function: sStopRxProcessor
Purpose:  Stop the receive processor from processing a channel.
Call:     sStopRxProcessor(ChP)
          CHANNEL_T *ChP; Ptr to channel structure

Comments: The receive processor can be started again with sStartRxProcessor().
          This function causes the receive processor to skip over the
          stopped channel.  It does not stop it from processing other channels.

Warnings: No context switches are allowed while executing this function.

          Do not leave the receive processor stopped for more than one
          character time.

          After calling this function a delay of 4 uS is required to ensure
          that the receive processor is no longer processing this channel.
*/
static void sStopRxProcessor(CHANNEL_T * ChP)
{
	Byte_t R[4];

	R[0] = ChP->R[0];
	R[1] = ChP->R[1];
	R[2] = 0x0a;
	R[3] = ChP->R[3];
	sOutDW(ChP->IndexAddr, *(DWord_t *) & R[0]);
}

/***************************************************************************
Function: sFlushRxFIFO
Purpose:  Flush the Rx FIFO
Call:     sFlushRxFIFO(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Return:   void
Comments: To prevent data from being enqueued or dequeued in the Tx FIFO
          while it is being flushed the receive processor is stopped
          and the transmitter is disabled.  After these operations a
          4 uS delay is done before clearing the pointers to allow
          the receive processor to stop.  These items are handled inside
          this function.
Warnings: No context switches are allowed while executing this function.
*/
static void sFlushRxFIFO(CHANNEL_T * ChP)
{
	int i;
	Byte_t Ch;		/* channel number within AIOP */
	int RxFIFOEnabled;	/* 1 if Rx FIFO enabled */

	if (sGetRxCnt(ChP) == 0)	/* Rx FIFO empty */
		return;		/* don't need to flush */

	RxFIFOEnabled = 0;
	if (ChP->R[0x32] == 0x08) {	/* Rx FIFO is enabled */
		RxFIFOEnabled = 1;
		sDisRxFIFO(ChP);	/* disable it */
		for (i = 0; i < 2000 / 200; i++)	/* delay 2 uS to allow proc to disable FIFO */
			sInB(ChP->IntChan);	/* depends on bus i/o timing */
	}
	sGetChanStatus(ChP);	/* clear any pending Rx errors in chan stat */
	Ch = (Byte_t) sGetChanNum(ChP);
	sOutB(ChP->Cmd, Ch | RESRXFCNT);	/* apply reset Rx FIFO count */
	sOutB(ChP->Cmd, Ch);	/* remove reset Rx FIFO count */
	sOutW((WordIO_t) ChP->IndexAddr, ChP->RxFIFOPtrs);	/* clear Rx out ptr */
	sOutW(ChP->IndexData, 0);
	sOutW((WordIO_t) ChP->IndexAddr, ChP->RxFIFOPtrs + 2);	/* clear Rx in ptr */
	sOutW(ChP->IndexData, 0);
	if (RxFIFOEnabled)
		sEnRxFIFO(ChP);	/* enable Rx FIFO */
}

/***************************************************************************
Function: sFlushTxFIFO
Purpose:  Flush the Tx FIFO
Call:     sFlushTxFIFO(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Return:   void
Comments: To prevent data from being enqueued or dequeued in the Tx FIFO
          while it is being flushed the receive processor is stopped
          and the transmitter is disabled.  After these operations a
          4 uS delay is done before clearing the pointers to allow
          the receive processor to stop.  These items are handled inside
          this function.
Warnings: No context switches are allowed while executing this function.
*/
static void sFlushTxFIFO(CHANNEL_T * ChP)
{
	int i;
	Byte_t Ch;		/* channel number within AIOP */
	int TxEnabled;		/* 1 if transmitter enabled */

	if (sGetTxCnt(ChP) == 0)	/* Tx FIFO empty */
		return;		/* don't need to flush */

	TxEnabled = 0;
	if (ChP->TxControl[3] & TX_ENABLE) {
		TxEnabled = 1;
		sDisTransmit(ChP);	/* disable transmitter */
	}
	sStopRxProcessor(ChP);	/* stop Rx processor */
	for (i = 0; i < 4000 / 200; i++)	/* delay 4 uS to allow proc to stop */
		sInB(ChP->IntChan);	/* depends on bus i/o timing */
	Ch = (Byte_t) sGetChanNum(ChP);
	sOutB(ChP->Cmd, Ch | RESTXFCNT);	/* apply reset Tx FIFO count */
	sOutB(ChP->Cmd, Ch);	/* remove reset Tx FIFO count */
	sOutW((WordIO_t) ChP->IndexAddr, ChP->TxFIFOPtrs);	/* clear Tx in/out ptrs */
	sOutW(ChP->IndexData, 0);
	if (TxEnabled)
		sEnTransmit(ChP);	/* enable transmitter */
	sStartRxProcessor(ChP);	/* restart Rx processor */
}

/***************************************************************************
Function: sWriteTxPrioByte
Purpose:  Write a byte of priority transmit data to a channel
Call:     sWriteTxPrioByte(ChP,Data)
          CHANNEL_T *ChP; Ptr to channel structure
          Byte_t Data; The transmit data byte

Return:   int: 1 if the bytes is successfully written, otherwise 0.

Comments: The priority byte is transmitted before any data in the Tx FIFO.

Warnings: No context switches are allowed while executing this function.
*/
static int sWriteTxPrioByte(CHANNEL_T * ChP, Byte_t Data)
{
	Byte_t DWBuf[4];	/* buffer for double word writes */
	Word_t *WordPtr;	/* must be far because Win SS != DS */
	register DWordIO_t IndexAddr;

	if (sGetTxCnt(ChP) > 1) {	/* write it to Tx priority buffer */
		IndexAddr = ChP->IndexAddr;
		sOutW((WordIO_t) IndexAddr, ChP->TxPrioCnt);	/* get priority buffer status */
		if (sInB((ByteIO_t) ChP->IndexData) & PRI_PEND)	/* priority buffer busy */
			return (0);	/* nothing sent */

		WordPtr = (Word_t *) (&DWBuf[0]);
		*WordPtr = ChP->TxPrioBuf;	/* data byte address */

		DWBuf[2] = Data;	/* data byte value */
		sOutDW(IndexAddr, *((DWord_t *) (&DWBuf[0])));	/* write it out */

		*WordPtr = ChP->TxPrioCnt;	/* Tx priority count address */

		DWBuf[2] = PRI_PEND + 1;	/* indicate 1 byte pending */
		DWBuf[3] = 0;	/* priority buffer pointer */
		sOutDW(IndexAddr, *((DWord_t *) (&DWBuf[0])));	/* write it out */
	} else {		/* write it to Tx FIFO */

		sWriteTxByte(sGetTxRxDataIO(ChP), Data);
	}
	return (1);		/* 1 byte sent */
}

/***************************************************************************
Function: sEnInterrupts
Purpose:  Enable one or more interrupts for a channel
Call:     sEnInterrupts(ChP,Flags)
          CHANNEL_T *ChP; Ptr to channel structure
          Word_t Flags: Interrupt enable flags, can be any combination
             of the following flags:
                TXINT_EN:   Interrupt on Tx FIFO empty
                RXINT_EN:   Interrupt on Rx FIFO at trigger level (see
                            sSetRxTrigger())
                SRCINT_EN:  Interrupt on SRC (Special Rx Condition)
                MCINT_EN:   Interrupt on modem input change
                CHANINT_EN: Allow channel interrupt signal to the AIOP's
                            Interrupt Channel Register.
Return:   void
Comments: If an interrupt enable flag is set in Flags, that interrupt will be
          enabled.  If an interrupt enable flag is not set in Flags, that
          interrupt will not be changed.  Interrupts can be disabled with
          function sDisInterrupts().

          This function sets the appropriate bit for the channel in the AIOP's
          Interrupt Mask Register if the CHANINT_EN flag is set.  This allows
          this channel's bit to be set in the AIOP's Interrupt Channel Register.

          Interrupts must also be globally enabled before channel interrupts
          will be passed on to the host.  This is done with function
          sEnGlobalInt().

          In some cases it may be desirable to disable interrupts globally but
          enable channel interrupts.  This would allow the global interrupt
          status register to be used to determine which AIOPs need service.
*/
static void sEnInterrupts(CHANNEL_T * ChP, Word_t Flags)
{
	Byte_t Mask;		/* Interrupt Mask Register */

	ChP->RxControl[2] |=
	    ((Byte_t) Flags & (RXINT_EN | SRCINT_EN | MCINT_EN));

	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->RxControl[0]);

	ChP->TxControl[2] |= ((Byte_t) Flags & TXINT_EN);

	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->TxControl[0]);

	if (Flags & CHANINT_EN) {
		Mask = sInB(ChP->IntMask) | sBitMapSetTbl[ChP->ChanNum];
		sOutB(ChP->IntMask, Mask);
	}
}

/***************************************************************************
Function: sDisInterrupts
Purpose:  Disable one or more interrupts for a channel
Call:     sDisInterrupts(ChP,Flags)
          CHANNEL_T *ChP; Ptr to channel structure
          Word_t Flags: Interrupt flags, can be any combination
             of the following flags:
                TXINT_EN:   Interrupt on Tx FIFO empty
                RXINT_EN:   Interrupt on Rx FIFO at trigger level (see
                            sSetRxTrigger())
                SRCINT_EN:  Interrupt on SRC (Special Rx Condition)
                MCINT_EN:   Interrupt on modem input change
                CHANINT_EN: Disable channel interrupt signal to the
                            AIOP's Interrupt Channel Register.
Return:   void
Comments: If an interrupt flag is set in Flags, that interrupt will be
          disabled.  If an interrupt flag is not set in Flags, that
          interrupt will not be changed.  Interrupts can be enabled with
          function sEnInterrupts().

          This function clears the appropriate bit for the channel in the AIOP's
          Interrupt Mask Register if the CHANINT_EN flag is set.  This blocks
          this channel's bit from being set in the AIOP's Interrupt Channel
          Register.
*/
static void sDisInterrupts(CHANNEL_T * ChP, Word_t Flags)
{
	Byte_t Mask;		/* Interrupt Mask Register */

	ChP->RxControl[2] &=
	    ~((Byte_t) Flags & (RXINT_EN | SRCINT_EN | MCINT_EN));
	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->RxControl[0]);
	ChP->TxControl[2] &= ~((Byte_t) Flags & TXINT_EN);
	sOutDW(ChP->IndexAddr, *(DWord_t *) & ChP->TxControl[0]);

	if (Flags & CHANINT_EN) {
		Mask = sInB(ChP->IntMask) & sBitMapClrTbl[ChP->ChanNum];
		sOutB(ChP->IntMask, Mask);
	}
}

static void sSetInterfaceMode(CHANNEL_T * ChP, Byte_t mode)
{
	sOutB(ChP->CtlP->AiopIO[2], (mode & 0x18) | ChP->ChanNum);
}

/*
 *  Not an official SSCI function, but how to reset RocketModems.
 *  ISA bus version
 */
static void sModemReset(CONTROLLER_T * CtlP, int chan, int on)
{
	ByteIO_t addr;
	Byte_t val;

	addr = CtlP->AiopIO[0] + 0x400;
	val = sInB(CtlP->MReg3IO);
	/* if AIOP[1] is not enabled, enable it */
	if ((val & 2) == 0) {
		val = sInB(CtlP->MReg2IO);
		sOutB(CtlP->MReg2IO, (val & 0xfc) | (1 & 0x03));
		sOutB(CtlP->MBaseIO, (unsigned char) (addr >> 6));
	}

	sEnAiop(CtlP, 1);
	if (!on)
		addr += 8;
	sOutB(addr + chan, 0);	/* apply or remove reset */
	sDisAiop(CtlP, 1);
}

/*
 *  Not an official SSCI function, but how to reset RocketModems.
 *  PCI bus version
 */
static void sPCIModemReset(CONTROLLER_T * CtlP, int chan, int on)
{
	ByteIO_t addr;

	addr = CtlP->AiopIO[0] + 0x40;	/* 2nd AIOP */
	if (!on)
		addr += 8;
	sOutB(addr + chan, 0);	/* apply or remove reset */
}

/*  Resets the speaker controller on RocketModem II and III devices */
static void rmSpeakerReset(CONTROLLER_T * CtlP, unsigned long model)
{
	ByteIO_t addr;

	/* RocketModem II speaker control is at the 8th port location of offset 0x40 */
	if ((model == MODEL_RP4M) || (model == MODEL_RP6M)) {
		addr = CtlP->AiopIO[0] + 0x4F;
		sOutB(addr, 0);
	}

	/* RocketModem III speaker control is at the 1st port location of offset 0x80 */
	if ((model == MODEL_UPCI_RM3_8PORT)
	    || (model == MODEL_UPCI_RM3_4PORT)) {
		addr = CtlP->AiopIO[0] + 0x88;
		sOutB(addr, 0);
	}
}

/*  Returns the line number given the controller (board), aiop and channel number */
static unsigned char GetLineNumber(int ctrl, int aiop, int ch)
{
	return lineNumbers[(ctrl << 5) | (aiop << 3) | ch];
}

/*
 *  Stores the line number associated with a given controller (board), aiop
 *  and channel number.  
 *  Returns:  The line number assigned 
 */
static unsigned char SetLineNumber(int ctrl, int aiop, int ch)
{
	lineNumbers[(ctrl << 5) | (aiop << 3) | ch] = nextLineNumber++;
	return (nextLineNumber - 1);
}
