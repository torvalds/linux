/*	$Id: aurora.c,v 1.19 2002/01/08 16:00:16 davem Exp $
 *	linux/drivers/sbus/char/aurora.c -- Aurora multiport driver
 *
 *	Copyright (c) 1999 by Oliver Aldulea (oli at bv dot ro)
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
 *	Most of the information you need is in the aurora.h file. Please
 *	read that file before reading this one.
 *
 *	Several parts of the code do not have comments yet.
 * 
 * n.b.  The board can support 115.2 bit rates, but only on a few
 * ports. The total badwidth of one chip (ports 0-7 or 8-15) is equal
 * to OSC_FREQ div 16. In case of my board, each chip can take 6
 * channels of 115.2 kbaud.  This information is not well-tested.
 * 
 * Fixed to use tty_get_baud_rate().
 *   Theodore Ts'o <tytso@mit.edu>, 2001-Oct-12
 */

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/sched.h>
#ifdef AURORA_INT_DEBUG
#include <linux/timer.h>
#endif
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/kdebug.h>
#include <asm/sbus.h>
#include <asm/uaccess.h>

#include "aurora.h"
#include "cd180.h"

unsigned char irqs[4] = {
	0, 0, 0, 0
};

#ifdef AURORA_INT_DEBUG
int irqhit=0;
#endif

static struct tty_driver *aurora_driver;
static struct Aurora_board aurora_board[AURORA_NBOARD] = {
	{0,},
};

static struct Aurora_port aurora_port[AURORA_TNPORTS] =  {
	{ 0, },
};

/* no longer used. static struct Aurora_board * IRQ_to_board[16] = { NULL, } ;*/
static unsigned char * tmp_buf = NULL;
static DECLARE_MUTEX(tmp_buf_sem);

DECLARE_TASK_QUEUE(tq_aurora);

static inline int aurora_paranoia_check(struct Aurora_port const * port,
				    char *name, const char *routine)
{
#ifdef AURORA_PARANOIA_CHECK
	static const char *badmagic =
		KERN_DEBUG "aurora: Warning: bad aurora port magic number for device %s in %s\n";
	static const char *badinfo =
		KERN_DEBUG "aurora: Warning: null aurora port for device %s in %s\n";

	if (!port) {
		printk(badinfo, name, routine);
		return 1;
	}
	if (port->magic != AURORA_MAGIC) {
		printk(badmagic, name, routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * 
 *  Service functions for aurora driver.
 * 
 */

/* Get board number from pointer */
static inline int board_No (struct Aurora_board const * bp)
{
	return bp - aurora_board;
}

/* Get port number from pointer */
static inline int port_No (struct Aurora_port const * port)
{
	return AURORA_PORT(port - aurora_port); 
}

/* Get pointer to board from pointer to port */
static inline struct Aurora_board * port_Board(struct Aurora_port const * port)
{
	return &aurora_board[AURORA_BOARD(port - aurora_port)];
}

/* Wait for Channel Command Register ready */
static inline void aurora_wait_CCR(struct aurora_reg128 * r)
{
	unsigned long delay;

#ifdef AURORA_DEBUG
printk("aurora_wait_CCR\n");
#endif
	/* FIXME: need something more descriptive than 100000 :) */
	for (delay = 100000; delay; delay--) 
		if (!sbus_readb(&r->r[CD180_CCR]))
			return;
	printk(KERN_DEBUG "aurora: Timeout waiting for CCR.\n");
}

/*
 *  aurora probe functions.
 */

/* Must be called with enabled interrupts */
static inline void aurora_long_delay(unsigned long delay)
{
	unsigned long i;

#ifdef AURORA_DEBUG
	printk("aurora_long_delay: start\n");
#endif
	for (i = jiffies + delay; time_before(jiffies, i); ) ;
#ifdef AURORA_DEBUG
	printk("aurora_long_delay: end\n");
#endif
}

/* Reset and setup CD180 chip */
static int aurora_init_CD180(struct Aurora_board * bp, int chip)
{
	unsigned long flags;
	int id;
	
#ifdef AURORA_DEBUG
	printk("aurora_init_CD180: start %d:%d\n",
	       board_No(bp), chip);
#endif
	save_flags(flags); cli();
	sbus_writeb(0, &bp->r[chip]->r[CD180_CAR]);
	sbus_writeb(0, &bp->r[chip]->r[CD180_GSVR]);

	/* Wait for CCR ready        */
	aurora_wait_CCR(bp->r[chip]);

	/* Reset CD180 chip          */
	sbus_writeb(CCR_HARDRESET, &bp->r[chip]->r[CD180_CCR]);
	udelay(1);
	sti();
	id=1000;
	while((--id) &&
	      (sbus_readb(&bp->r[chip]->r[CD180_GSVR])!=0xff))udelay(100);
	if(!id) {
		printk(KERN_ERR "aurora%d: Chip %d failed init.\n",
		       board_No(bp), chip);
		restore_flags(flags);
		return(-1);
	}
	cli();
	sbus_writeb((board_No(bp)<<5)|((chip+1)<<3),
		    &bp->r[chip]->r[CD180_GSVR]); /* Set ID for this chip      */
	sbus_writeb(0x80|bp->ACK_MINT,
		    &bp->r[chip]->r[CD180_MSMR]); /* Prio for modem intr       */
	sbus_writeb(0x80|bp->ACK_TINT,
		    &bp->r[chip]->r[CD180_TSMR]); /* Prio for transmitter intr */
	sbus_writeb(0x80|bp->ACK_RINT,
		    &bp->r[chip]->r[CD180_RSMR]); /* Prio for receiver intr    */
	/* Setting up prescaler. We need 4 tick per 1 ms */
	sbus_writeb((bp->oscfreq/(1000000/AURORA_TPS)) >> 8,
		    &bp->r[chip]->r[CD180_PPRH]);
	sbus_writeb((bp->oscfreq/(1000000/AURORA_TPS)) & 0xff,
		    &bp->r[chip]->r[CD180_PPRL]);

	sbus_writeb(SRCR_AUTOPRI|SRCR_GLOBPRI,
		    &bp->r[chip]->r[CD180_SRCR]);

	id = sbus_readb(&bp->r[chip]->r[CD180_GFRCR]);
	printk(KERN_INFO "aurora%d: Chip %d id %02x: ",
	       board_No(bp), chip,id);
	if(sbus_readb(&bp->r[chip]->r[CD180_SRCR]) & 128) {
		switch (id) {
			case 0x82:printk("CL-CD1864 rev A\n");break;
			case 0x83:printk("CL-CD1865 rev A\n");break;
			case 0x84:printk("CL-CD1865 rev B\n");break;
			case 0x85:printk("CL-CD1865 rev C\n");break;
			default:printk("Unknown.\n");
		};
	} else {
		switch (id) {
			case 0x81:printk("CL-CD180 rev B\n");break;
			case 0x82:printk("CL-CD180 rev C\n");break;
			default:printk("Unknown.\n");
		};
	}
	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_init_CD180: end\n");
#endif
	return 0;
}

static int valid_irq(unsigned char irq)
{
int i;
for(i=0;i<TYPE_1_IRQS;i++)
	if (type_1_irq[i]==irq) return 1;
return 0;
}

static irqreturn_t aurora_interrupt(int irq, void * dev_id, struct pt_regs * regs);

/* Main probing routine, also sets irq. */
static int aurora_probe(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev;
	int grrr;
	char buf[30];
	int bn = 0;
	struct Aurora_board *bp;

	for_each_sbus(sbus) {
		for_each_sbusdev(sdev, sbus) {
/*			printk("Try: %x %s\n",sdev,sdev->prom_name);*/
			if (!strcmp(sdev->prom_name, "sio16")) {
#ifdef AURORA_DEBUG
				printk(KERN_INFO "aurora: sio16 at %p\n",sdev);
#endif
				if((sdev->reg_addrs[0].reg_size!=1) &&
				   (sdev->reg_addrs[1].reg_size!=128) &&
				   (sdev->reg_addrs[2].reg_size!=128) &&
				   (sdev->reg_addrs[3].reg_size!=4)) {
				   	printk(KERN_ERR "aurora%d: registers' sizes "
					       "do not match.\n", bn);
				   	break;
				}
				bp = &aurora_board[bn];
				bp->r0 = (struct aurora_reg1 *)
					sbus_ioremap(&sdev->resource[0], 0,
						     sdev->reg_addrs[0].reg_size,
						     "sio16");
				if (bp->r0 == NULL) {
					printk(KERN_ERR "aurora%d: can't map "
					       "reg_addrs[0]\n", bn);
					break;
				}
#ifdef AURORA_DEBUG
				printk("Map reg 0: %p\n", bp->r0);
#endif
				bp->r[0] = (struct aurora_reg128 *)
					sbus_ioremap(&sdev->resource[1], 0,
						     sdev->reg_addrs[1].reg_size,
						     "sio16");
				if (bp->r[0] == NULL) {
					printk(KERN_ERR "aurora%d: can't map "
					       "reg_addrs[1]\n", bn);
					break;
				}
#ifdef AURORA_DEBUG
				printk("Map reg 1: %p\n", bp->r[0]);
#endif
				bp->r[1] = (struct aurora_reg128 *)
					sbus_ioremap(&sdev->resource[2], 0,
						     sdev->reg_addrs[2].reg_size,
						     "sio16");
				if (bp->r[1] == NULL) {
					printk(KERN_ERR "aurora%d: can't map "
					       "reg_addrs[2]\n", bn);
					break;
				}
#ifdef AURORA_DEBUG
				printk("Map reg 2: %p\n", bp->r[1]);
#endif
				bp->r3 = (struct aurora_reg4 *)
					sbus_ioremap(&sdev->resource[3], 0,
						     sdev->reg_addrs[3].reg_size,
						     "sio16");
				if (bp->r3 == NULL) {
					printk(KERN_ERR "aurora%d: can't map "
					       "reg_addrs[3]\n", bn);
					break;
				}
#ifdef AURORA_DEBUG
				printk("Map reg 3: %p\n", bp->r3);
#endif
				/* Variables setup */
				bp->flags = 0;
#ifdef AURORA_DEBUG
				grrr=prom_getint(sdev->prom_node,"intr");
				printk("intr pri %d\n", grrr);
#endif
				if ((bp->irq=irqs[bn]) && valid_irq(bp->irq) &&
				    !request_irq(bp->irq|0x30, aurora_interrupt, SA_SHIRQ, "sio16", bp)) {
					free_irq(bp->irq|0x30, bp);
				} else
				if ((bp->irq=prom_getint(sdev->prom_node, "bintr")) && valid_irq(bp->irq) &&
				    !request_irq(bp->irq|0x30, aurora_interrupt, SA_SHIRQ, "sio16", bp)) {
					free_irq(bp->irq|0x30, bp);
				} else
				if ((bp->irq=prom_getint(sdev->prom_node, "intr")) && valid_irq(bp->irq) &&
				    !request_irq(bp->irq|0x30, aurora_interrupt, SA_SHIRQ, "sio16", bp)) {
					free_irq(bp->irq|0x30, bp);
				} else
				for(grrr=0;grrr<TYPE_1_IRQS;grrr++) {
					if ((bp->irq=type_1_irq[grrr])&&!request_irq(bp->irq|0x30, aurora_interrupt, SA_SHIRQ, "sio16", bp)) {
						free_irq(bp->irq|0x30, bp);
						break;
					} else {
					printk(KERN_ERR "aurora%d: Could not get an irq for this board !!!\n",bn);
					bp->flags=0xff;
					}
				}
				if(bp->flags==0xff)break;
				printk(KERN_INFO "aurora%d: irq %d\n",bn,bp->irq&0x0f);
				buf[0]=0;
				grrr=prom_getproperty(sdev->prom_node,"dtr_rts",buf,sizeof(buf));
				if(!strcmp(buf,"swapped")){
					printk(KERN_INFO "aurora%d: Swapped DTR and RTS\n",bn);
					bp->DTR=MSVR_RTS;
					bp->RTS=MSVR_DTR;
					bp->MSVDTR=CD180_MSVRTS;
					bp->MSVRTS=CD180_MSVDTR;
					bp->flags|=AURORA_BOARD_DTR_FLOW_OK;
					}else{
					#ifdef AURORA_FORCE_DTR_FLOW
					printk(KERN_INFO "aurora%d: Forcing swapped DTR-RTS\n",bn);
					bp->DTR=MSVR_RTS;
					bp->RTS=MSVR_DTR;
					bp->MSVDTR=CD180_MSVRTS;
					bp->MSVRTS=CD180_MSVDTR;
					bp->flags|=AURORA_BOARD_DTR_FLOW_OK;
					#else
					printk(KERN_INFO "aurora%d: Normal DTR and RTS\n",bn);
					bp->DTR=MSVR_DTR;
					bp->RTS=MSVR_RTS;
					bp->MSVDTR=CD180_MSVDTR;
					bp->MSVRTS=CD180_MSVRTS;
					#endif
				}
				bp->oscfreq=prom_getint(sdev->prom_node,"clk")*100;
				printk(KERN_INFO "aurora%d: Oscillator: %d Hz\n",bn,bp->oscfreq);
				grrr=prom_getproperty(sdev->prom_node,"chip",buf,sizeof(buf));
				printk(KERN_INFO "aurora%d: Chips: %s\n",bn,buf);
				grrr=prom_getproperty(sdev->prom_node,"manu",buf,sizeof(buf));
				printk(KERN_INFO "aurora%d: Manufacturer: %s\n",bn,buf);
				grrr=prom_getproperty(sdev->prom_node,"model",buf,sizeof(buf));
				printk(KERN_INFO "aurora%d: Model: %s\n",bn,buf);
				grrr=prom_getproperty(sdev->prom_node,"rev",buf,sizeof(buf));
				printk(KERN_INFO "aurora%d: Revision: %s\n",bn,buf);
				grrr=prom_getproperty(sdev->prom_node,"mode",buf,sizeof(buf));
				printk(KERN_INFO "aurora%d: Mode: %s\n",bn,buf);
				#ifdef MODULE
				bp->count=0;
				#endif
				bp->flags = AURORA_BOARD_PRESENT;
				/* hardware ack */
				bp->ACK_MINT=1;
				bp->ACK_TINT=2;
				bp->ACK_RINT=3;
				bn++;
			}
		}
	}
	return bn;
}

static void aurora_release_io_range(struct Aurora_board *bp)
{
	sbus_iounmap((unsigned long)bp->r0, 1);
	sbus_iounmap((unsigned long)bp->r[0], 128);
	sbus_iounmap((unsigned long)bp->r[1], 128);
	sbus_iounmap((unsigned long)bp->r3, 4);
}

static inline void aurora_mark_event(struct Aurora_port * port, int event)
{
#ifdef AURORA_DEBUG
	printk("aurora_mark_event: start\n");
#endif
	set_bit(event, &port->event);
	queue_task(&port->tqueue, &tq_aurora);
	mark_bh(AURORA_BH);
#ifdef AURORA_DEBUG
	printk("aurora_mark_event: end\n");
#endif
}

static __inline__ struct Aurora_port * aurora_get_port(struct Aurora_board const * bp,
						       int chip,
						       unsigned char const *what)
{
	unsigned char channel;
	struct Aurora_port * port;

	channel = ((chip << 3) |
		   ((sbus_readb(&bp->r[chip]->r[CD180_GSCR]) & GSCR_CHAN) >> GSCR_CHAN_OFF));
	port = &aurora_port[board_No(bp) * AURORA_NPORT * AURORA_NCD180 + channel];
	if (port->flags & ASYNC_INITIALIZED)
		return port;

	printk(KERN_DEBUG "aurora%d: %s interrupt from invalid port %d\n",
	       board_No(bp), what, channel);
	return NULL;
}

static void aurora_receive_exc(struct Aurora_board const * bp, int chip)
{
	struct Aurora_port *port;
	struct tty_struct *tty;
	unsigned char status;
	unsigned char ch;
	
	if (!(port = aurora_get_port(bp, chip, "Receive_x")))
		return;

	tty = port->tty;
	if (tty->flip.count >= TTY_FLIPBUF_SIZE)  {
#ifdef AURORA_INTNORM
		printk("aurora%d: port %d: Working around flip buffer overflow.\n",
		       board_No(bp), port_No(port));
#endif
		return;
	}
	
#ifdef AURORA_REPORT_OVERRUN	
	status = sbus_readb(&bp->r[chip]->r[CD180_RCSR]);
	if (status & RCSR_OE)  {
		port->overrun++;
#if 1
		printk("aurora%d: port %d: Overrun. Total %ld overruns.\n",
		       board_No(bp), port_No(port), port->overrun);
#endif		
	}
	status &= port->mark_mask;
#else	
	status = sbus_readb(&bp->r[chip]->r[CD180_RCSR]) & port->mark_mask;
#endif	
	ch = sbus_readb(&bp->r[chip]->r[CD180_RDR]);
	if (!status)
		return;

	if (status & RCSR_TOUT)  {
/*		printk("aurora%d: port %d: Receiver timeout. Hardware problems ?\n",
		       board_No(bp), port_No(port));*/
		return;
		
	} else if (status & RCSR_BREAK)  {
		printk(KERN_DEBUG "aurora%d: port %d: Handling break...\n",
		       board_No(bp), port_No(port));
		*tty->flip.flag_buf_ptr++ = TTY_BREAK;
		if (port->flags & ASYNC_SAK)
			do_SAK(tty);
		
	} else if (status & RCSR_PE) 
		*tty->flip.flag_buf_ptr++ = TTY_PARITY;
	
	else if (status & RCSR_FE) 
		*tty->flip.flag_buf_ptr++ = TTY_FRAME;
	
        else if (status & RCSR_OE)
		*tty->flip.flag_buf_ptr++ = TTY_OVERRUN;
	
	else
		*tty->flip.flag_buf_ptr++ = 0;
	
	*tty->flip.char_buf_ptr++ = ch;
	tty->flip.count++;
	queue_task(&tty->flip.tqueue, &tq_timer);
}

static void aurora_receive(struct Aurora_board const * bp, int chip)
{
	struct Aurora_port *port;
	struct tty_struct *tty;
	unsigned char count,cnt;

	if (!(port = aurora_get_port(bp, chip, "Receive")))
		return;
	
	tty = port->tty;
	
	count = sbus_readb(&bp->r[chip]->r[CD180_RDCR]);

#ifdef AURORA_REPORT_FIFO
	port->hits[count > 8 ? 9 : count]++;
#endif

	while (count--)  {
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)  {
#ifdef AURORA_INTNORM
			printk("aurora%d: port %d: Working around flip buffer overflow.\n",
			       board_No(bp), port_No(port));
#endif
			break;
		}
		cnt = sbus_readb(&bp->r[chip]->r[CD180_RDR]);
		*tty->flip.char_buf_ptr++ = cnt;
		*tty->flip.flag_buf_ptr++ = 0;
		tty->flip.count++;
	}
	queue_task(&tty->flip.tqueue, &tq_timer);
}

static void aurora_transmit(struct Aurora_board const * bp, int chip)
{
	struct Aurora_port *port;
	struct tty_struct *tty;
	unsigned char count;
	
	if (!(port = aurora_get_port(bp, chip, "Transmit")))
		return;
		
	tty = port->tty;
	
	if (port->SRER & SRER_TXEMPTY)  {
		/* FIFO drained */
		sbus_writeb(port_No(port) & 7,
			    &bp->r[chip]->r[CD180_CAR]);
		udelay(1);
		port->SRER &= ~SRER_TXEMPTY;
		sbus_writeb(port->SRER, &bp->r[chip]->r[CD180_SRER]);
		return;
	}
	
	if ((port->xmit_cnt <= 0 && !port->break_length)
	    || tty->stopped || tty->hw_stopped)  {
		sbus_writeb(port_No(port) & 7,
			    &bp->r[chip]->r[CD180_CAR]);
		udelay(1);
		port->SRER &= ~SRER_TXRDY;
		sbus_writeb(port->SRER,
			    &bp->r[chip]->r[CD180_SRER]);
		return;
	}
	
	if (port->break_length)  {
		if (port->break_length > 0)  {
			if (port->COR2 & COR2_ETC)  {
				sbus_writeb(CD180_C_ESC,
					    &bp->r[chip]->r[CD180_TDR]);
				sbus_writeb(CD180_C_SBRK,
					    &bp->r[chip]->r[CD180_TDR]);
				port->COR2 &= ~COR2_ETC;
			}
			count = min(port->break_length, 0xff);
			sbus_writeb(CD180_C_ESC,
				    &bp->r[chip]->r[CD180_TDR]);
			sbus_writeb(CD180_C_DELAY,
				    &bp->r[chip]->r[CD180_TDR]);
			sbus_writeb(count,
				    &bp->r[chip]->r[CD180_TDR]);
			if (!(port->break_length -= count))
				port->break_length--;
		} else  {
			sbus_writeb(CD180_C_ESC,
				    &bp->r[chip]->r[CD180_TDR]);
			sbus_writeb(CD180_C_EBRK,
				    &bp->r[chip]->r[CD180_TDR]);
			sbus_writeb(port->COR2,
				    &bp->r[chip]->r[CD180_COR2]);
			aurora_wait_CCR(bp->r[chip]);
			sbus_writeb(CCR_CORCHG2,
				    &bp->r[chip]->r[CD180_CCR]);
			port->break_length = 0;
		}
		return;
	}
	
	count = CD180_NFIFO;
	do {
		u8 byte = port->xmit_buf[port->xmit_tail++];

		sbus_writeb(byte, &bp->r[chip]->r[CD180_TDR]);
		port->xmit_tail = port->xmit_tail & (SERIAL_XMIT_SIZE-1);
		if (--port->xmit_cnt <= 0)
			break;
	} while (--count > 0);
	
	if (port->xmit_cnt <= 0)  {
		sbus_writeb(port_No(port) & 7,
			    &bp->r[chip]->r[CD180_CAR]);
		udelay(1);
		port->SRER &= ~SRER_TXRDY;
		sbus_writeb(port->SRER,
			    &bp->r[chip]->r[CD180_SRER]);
	}
	if (port->xmit_cnt <= port->wakeup_chars)
		aurora_mark_event(port, RS_EVENT_WRITE_WAKEUP);
}

static void aurora_check_modem(struct Aurora_board const * bp, int chip)
{
	struct Aurora_port *port;
	struct tty_struct *tty;
	unsigned char mcr;
	
	if (!(port = aurora_get_port(bp, chip, "Modem")))
		return;
		
	tty = port->tty;
	
	mcr = sbus_readb(&bp->r[chip]->r[CD180_MCR]);
	if (mcr & MCR_CDCHG)  {
		if (sbus_readb(&bp->r[chip]->r[CD180_MSVR]) & MSVR_CD) 
			wake_up_interruptible(&port->open_wait);
		else
			schedule_task(&port->tqueue_hangup);
	}
	
/* We don't have such things yet. My aurora board has DTR and RTS swapped, but that doesn't count in this driver. Let's hope
 * Aurora didn't made any boards with CTS or DSR broken...
 */
/* #ifdef AURORA_BRAIN_DAMAGED_CTS
	if (mcr & MCR_CTSCHG)  {
		if (aurora_in(bp, CD180_MSVR) & MSVR_CTS)  {
			tty->hw_stopped = 0;
			port->SRER |= SRER_TXRDY;
			if (port->xmit_cnt <= port->wakeup_chars)
				aurora_mark_event(port, RS_EVENT_WRITE_WAKEUP);
		} else  {
			tty->hw_stopped = 1;
			port->SRER &= ~SRER_TXRDY;
		}
		sbus_writeb(port->SRER, &bp->r[chip]->r[CD180_SRER]);
	}
	if (mcr & MCR_DSRCHG)  {
		if (aurora_in(bp, CD180_MSVR) & MSVR_DSR)  {
			tty->hw_stopped = 0;
			port->SRER |= SRER_TXRDY;
			if (port->xmit_cnt <= port->wakeup_chars)
				aurora_mark_event(port, RS_EVENT_WRITE_WAKEUP);
		} else  {
			tty->hw_stopped = 1;
			port->SRER &= ~SRER_TXRDY;
		}
		sbus_writeb(port->SRER, &bp->r[chip]->r[CD180_SRER]);
	}
#endif AURORA_BRAIN_DAMAGED_CTS */
	
	/* Clear change bits */
	sbus_writeb(0, &bp->r[chip]->r[CD180_MCR]);
}

/* The main interrupt processing routine */
static irqreturn_t aurora_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
	unsigned char status;
	unsigned char ack,chip/*,chip_id*/;
	struct Aurora_board * bp = (struct Aurora_board *) dev_id;
	unsigned long loop = 0;

#ifdef AURORA_INT_DEBUG
	printk("IRQ%d %d\n",irq,++irqhit);
#ifdef AURORA_FLOODPRO
	if (irqhit>=AURORA_FLOODPRO)
		sbus_writeb(8, &bp->r0->r);
#endif
#endif
	
/* old	bp = IRQ_to_board[irq&0x0f];*/
	
	if (!bp || !(bp->flags & AURORA_BOARD_ACTIVE))
		return IRQ_NONE;

/*	The while() below takes care of this.
	status = sbus_readb(&bp->r[0]->r[CD180_SRSR]);
#ifdef AURORA_INT_DEBUG
	printk("mumu: %02x\n", status);
#endif
	if (!(status&SRSR_ANYINT))
		return IRQ_NONE; * Nobody has anything to say, so exit *
*/
	while ((loop++ < 48) &&
	       (status = sbus_readb(&bp->r[0]->r[CD180_SRSR]) & SRSR_ANYINT)){
#ifdef AURORA_INT_DEBUG
		printk("SRSR: %02x\n", status);
#endif
		if (status & SRSR_REXT) {
			ack = sbus_readb(&bp->r3->r[bp->ACK_RINT]);
#ifdef AURORA_INT_DEBUG
			printk("R-ACK %02x\n", ack);
#endif
			if ((ack >> 5) == board_No(bp)) {
				if ((chip=((ack>>3)&3)-1) < AURORA_NCD180) {
					if ((ack&GSVR_ITMASK)==GSVR_IT_RGD) {
						aurora_receive(bp,chip);
						sbus_writeb(0,
							 &bp->r[chip]->r[CD180_EOSRR]);
					} else if ((ack & GSVR_ITMASK) == GSVR_IT_REXC) {
						aurora_receive_exc(bp,chip);
						sbus_writeb(0,
							 &bp->r[chip]->r[CD180_EOSRR]);
					}
				}
			}
		} else if (status & SRSR_TEXT) {
			ack = sbus_readb(&bp->r3->r[bp->ACK_TINT]);
#ifdef AURORA_INT_DEBUG
			printk("T-ACK %02x\n", ack);
#endif
			if ((ack >> 5) == board_No(bp)) {
				if ((chip=((ack>>3)&3)-1) < AURORA_NCD180) {
					if ((ack&GSVR_ITMASK)==GSVR_IT_TX) {
						aurora_transmit(bp,chip);
						sbus_writeb(0,
							 &bp->r[chip]->r[CD180_EOSRR]);
					}
				}
			}
		} else if (status & SRSR_MEXT) {
			ack = sbus_readb(&bp->r3->r[bp->ACK_MINT]);
#ifdef AURORA_INT_DEBUG
			printk("M-ACK %02x\n", ack);
#endif
			if ((ack >> 5) == board_No(bp)) {
				if ((chip = ((ack>>3)&3)-1) < AURORA_NCD180) {
					if ((ack&GSVR_ITMASK)==GSVR_IT_MDM) {
						aurora_check_modem(bp,chip);
						sbus_writeb(0,
							 &bp->r[chip]->r[CD180_EOSRR]);
					}
				}
			}
		}
	}
/* I guess this faster code can be used with CD1865, using AUROPRI and GLOBPRI. */
#if 0
	while ((loop++ < 48)&&(status=bp->r[0]->r[CD180_SRSR]&SRSR_ANYINT)){
#ifdef AURORA_INT_DEBUG
		printk("SRSR: %02x\n",status);
#endif
		ack = sbus_readb(&bp->r3->r[0]);
#ifdef AURORA_INT_DEBUG
		printk("ACK: %02x\n",ack);
#endif
		if ((ack>>5)==board_No(bp)) {
			if ((chip=((ack>>3)&3)-1) < AURORA_NCD180) {
				ack&=GSVR_ITMASK;
				if (ack==GSVR_IT_RGD) {
					aurora_receive(bp,chip);
					sbus_writeb(0,
						    &bp->r[chip]->r[CD180_EOSRR]);
				} else if (ack==GSVR_IT_REXC) {
					aurora_receive_exc(bp,chip);
					sbus_writeb(0,
						    &bp->r[chip]->r[CD180_EOSRR]);
				} else if (ack==GSVR_IT_TX) {
					aurora_transmit(bp,chip);
					sbus_writeb(0,
						    &bp->r[chip]->r[CD180_EOSRR]);
				} else if (ack==GSVR_IT_MDM) {
					aurora_check_modem(bp,chip);
					sbus_writeb(0,
						    &bp->r[chip]->r[CD180_EOSRR]);
				}
			}
		}
	}
#endif

/* This is the old handling routine, used in riscom8 for only one CD180. I keep it here for reference. */
#if 0
	for(chip=0;chip<AURORA_NCD180;chip++){
		chip_id=(board_No(bp)<<5)|((chip+1)<<3);
		loop=0;
		while ((loop++ < 1) &&
		       ((status = sbus_readb(&bp->r[chip]->r[CD180_SRSR])) &
			(SRSR_TEXT | SRSR_MEXT | SRSR_REXT))) {

			if (status & SRSR_REXT) {
				ack = sbus_readb(&bp->r3->r[bp->ACK_RINT]);
				if (ack == (chip_id | GSVR_IT_RGD)) {
#ifdef AURORA_INTMSG
					printk("RX ACK\n");
#endif
					aurora_receive(bp,chip);
				} else if (ack == (chip_id | GSVR_IT_REXC)) {
#ifdef AURORA_INTMSG
					printk("RXC ACK\n");
#endif
					aurora_receive_exc(bp,chip);
				} else {
#ifdef AURORA_INTNORM
					printk("aurora%d-%d: Bad receive ack 0x%02x.\n",
					       board_No(bp), chip, ack);
#endif
				}
			} else if (status & SRSR_TEXT) {
				ack = sbus_readb(&bp->r3->r[bp->ACK_TINT]);
				if (ack == (chip_id | GSVR_IT_TX)){
#ifdef AURORA_INTMSG
					printk("TX ACK\n");
#endif
					aurora_transmit(bp,chip);
				} else {
#ifdef AURORA_INTNORM
					printk("aurora%d-%d: Bad transmit ack 0x%02x.\n",
					       board_No(bp), chip, ack);
#endif
				}
			} else  if (status & SRSR_MEXT)  {
				ack = sbus_readb(&bp->r3->r[bp->ACK_MINT]);
				if (ack == (chip_id | GSVR_IT_MDM)){
#ifdef AURORA_INTMSG
					printk("MDM ACK\n");
#endif
					aurora_check_modem(bp,chip);
				} else {
#ifdef AURORA_INTNORM
					printk("aurora%d-%d: Bad modem ack 0x%02x.\n",
					       board_No(bp), chip, ack);
#endif
				}
			}
			sbus_writeb(0, &bp->r[chip]->r[CD180_EOSRR]);
		}
	}
#endif

	return IRQ_HANDLED;
}

#ifdef AURORA_INT_DEBUG
static void aurora_timer (unsigned long ignored);

static DEFINE_TIMER(aurora_poll_timer, aurora_timer, 0, 0);

static void
aurora_timer (unsigned long ignored)
{
	unsigned long flags;
	int i;

	save_flags(flags); cli();

	printk("SRSR: %02x,%02x - ",
	       sbus_readb(&aurora_board[0].r[0]->r[CD180_SRSR]),
	       sbus_readb(&aurora_board[0].r[1]->r[CD180_SRSR]));
	for (i = 0; i < 4; i++) {
		udelay(1);
		printk("%02x ",
		       sbus_readb(&aurora_board[0].r3->r[i]));
	}
	printk("\n");

	aurora_poll_timer.expires = jiffies + 300;
	add_timer (&aurora_poll_timer);

	restore_flags(flags);
}
#endif

/*
 *  Routines for open & close processing.
 */

/* Called with disabled interrupts */
static int aurora_setup_board(struct Aurora_board * bp)
{
	int error;
	
#ifdef AURORA_ALLIRQ
	int i;
	for (i = 0; i < AURORA_ALLIRQ; i++) {
		error = request_irq(allirq[i]|0x30, aurora_interrupt, SA_SHIRQ,
				    "sio16", bp);
		if (error)
			printk(KERN_ERR "IRQ%d request error %d\n",
			       allirq[i], error);
	}
#else
	error = request_irq(bp->irq|0x30, aurora_interrupt, SA_SHIRQ,
			    "sio16", bp);
	if (error) {
		printk(KERN_ERR "IRQ request error %d\n", error);
		return error;
	}
#endif
	/* Board reset */
	sbus_writeb(0, &bp->r0->r);
	udelay(1);
	if (bp->flags & AURORA_BOARD_TYPE_2) {
		/* unknown yet */
	} else {
		sbus_writeb((AURORA_CFG_ENABLE_IO | AURORA_CFG_ENABLE_IRQ |
			     (((bp->irq)&0x0f)>>2)),
			    &bp->r0->r);
	}
	udelay(10000);

	if (aurora_init_CD180(bp,0))error=1;error=0;
	if (aurora_init_CD180(bp,1))error++;
	if (error == AURORA_NCD180) {
		printk(KERN_ERR "Both chips failed initialisation.\n");
		return -EIO;
	}

#ifdef AURORA_INT_DEBUG
	aurora_poll_timer.expires= jiffies + 1;
	add_timer(&aurora_poll_timer);
#endif
#ifdef AURORA_DEBUG
	printk("aurora_setup_board: end\n");
#endif
	return 0;
}

/* Called with disabled interrupts */
static void aurora_shutdown_board(struct Aurora_board *bp)
{
	int i;

#ifdef AURORA_DEBUG
	printk("aurora_shutdown_board: start\n");
#endif

#ifdef AURORA_INT_DEBUG
	del_timer(&aurora_poll_timer);
#endif

#ifdef AURORA_ALLIRQ
	for(i=0;i<AURORA_ALLIRQ;i++){
		free_irq(allirq[i]|0x30, bp);
/*		IRQ_to_board[allirq[i]&0xf] = NULL;*/
	}
#else
	free_irq(bp->irq|0x30, bp);
/*	IRQ_to_board[bp->irq&0xf] = NULL;*/
#endif	
	/* Drop all DTR's */
	for(i=0;i<16;i++){
		sbus_writeb(i & 7, &bp->r[i>>3]->r[CD180_CAR]);
		udelay(1);
		sbus_writeb(0, &bp->r[i>>3]->r[CD180_MSVR]);
		udelay(1);
	}
	/* Board shutdown */
	sbus_writeb(0, &bp->r0->r);

#ifdef AURORA_DEBUG
	printk("aurora_shutdown_board: end\n");
#endif
}

/* Setting up port characteristics. 
 * Must be called with disabled interrupts
 */
static void aurora_change_speed(struct Aurora_board *bp, struct Aurora_port *port)
{
	struct tty_struct *tty;
	unsigned long baud;
	long tmp;
	unsigned char cor1 = 0, cor3 = 0;
	unsigned char mcor1 = 0, mcor2 = 0,chip;
	
#ifdef AURORA_DEBUG
	printk("aurora_change_speed: start\n");
#endif
	if (!(tty = port->tty) || !tty->termios)
		return;
		
	chip = AURORA_CD180(port_No(port));

	port->SRER  = 0;
	port->COR2 = 0;
	port->MSVR = MSVR_RTS|MSVR_DTR;
	
	baud = tty_get_baud_rate(tty);
	
	/* Select port on the board */
	sbus_writeb(port_No(port) & 7,
		    &bp->r[chip]->r[CD180_CAR]);
	udelay(1);
	
	if (!baud)  {
		/* Drop DTR & exit */
		port->MSVR &= ~(bp->DTR|bp->RTS);
		sbus_writeb(port->MSVR,
			    &bp->r[chip]->r[CD180_MSVR]);
		return;
	} else  {
		/* Set DTR on */
		port->MSVR |= bp->DTR;
		sbus_writeb(port->MSVR,
			    &bp->r[chip]->r[CD180_MSVR]);
	}
	
	/* Now we must calculate some speed dependent things. */
	
	/* Set baud rate for port. */
	tmp = (((bp->oscfreq + baud/2) / baud +
		CD180_TPC/2) / CD180_TPC);

/*	tmp = (bp->oscfreq/7)/baud;
	if((tmp%10)>4)tmp=tmp/10+1;else tmp=tmp/10;*/
/*	printk("Prescaler period: %d\n",tmp);*/

	sbus_writeb((tmp >> 8) & 0xff,
		    &bp->r[chip]->r[CD180_RBPRH]);
	sbus_writeb((tmp >> 8) & 0xff,
		    &bp->r[chip]->r[CD180_TBPRH]);
	sbus_writeb(tmp & 0xff, &bp->r[chip]->r[CD180_RBPRL]);
	sbus_writeb(tmp & 0xff, &bp->r[chip]->r[CD180_TBPRL]);
	
	baud = (baud + 5) / 10;   /* Estimated CPS */
	
	/* Two timer ticks seems enough to wakeup something like SLIP driver */
	tmp = ((baud + HZ/2) / HZ) * 2 - CD180_NFIFO;		
	port->wakeup_chars = (tmp < 0) ? 0 : ((tmp >= SERIAL_XMIT_SIZE) ?
					      SERIAL_XMIT_SIZE - 1 : tmp);
	
	/* Receiver timeout will be transmission time for 1.5 chars */
	tmp = (AURORA_TPS + AURORA_TPS/2 + baud/2) / baud;
	tmp = (tmp > 0xff) ? 0xff : tmp;
	sbus_writeb(tmp, &bp->r[chip]->r[CD180_RTPR]);
	
	switch (C_CSIZE(tty))  {
	 case CS5:
		cor1 |= COR1_5BITS;
		break;
	 case CS6:
		cor1 |= COR1_6BITS;
		break;
	 case CS7:
		cor1 |= COR1_7BITS;
		break;
	 case CS8:
		cor1 |= COR1_8BITS;
		break;
	}
	
	if (C_CSTOPB(tty)) 
		cor1 |= COR1_2SB;
	
	cor1 |= COR1_IGNORE;
	if (C_PARENB(tty))  {
		cor1 |= COR1_NORMPAR;
		if (C_PARODD(tty)) 
			cor1 |= COR1_ODDP;
		if (I_INPCK(tty)) 
			cor1 &= ~COR1_IGNORE;
	}
	/* Set marking of some errors */
	port->mark_mask = RCSR_OE | RCSR_TOUT;
	if (I_INPCK(tty)) 
		port->mark_mask |= RCSR_FE | RCSR_PE;
	if (I_BRKINT(tty) || I_PARMRK(tty)) 
		port->mark_mask |= RCSR_BREAK;
	if (I_IGNPAR(tty)) 
		port->mark_mask &= ~(RCSR_FE | RCSR_PE);
	if (I_IGNBRK(tty))  {
		port->mark_mask &= ~RCSR_BREAK;
		if (I_IGNPAR(tty)) 
			/* Real raw mode. Ignore all */
			port->mark_mask &= ~RCSR_OE;
	}
	/* Enable Hardware Flow Control */
	if (C_CRTSCTS(tty))  {
/*#ifdef AURORA_BRAIN_DAMAGED_CTS
		port->SRER |= SRER_DSR | SRER_CTS;
		mcor1 |= MCOR1_DSRZD | MCOR1_CTSZD;
		mcor2 |= MCOR2_DSROD | MCOR2_CTSOD;
		tty->hw_stopped = !(aurora_in(bp, CD180_MSVR) & (MSVR_CTS|MSVR_DSR));
#else*/
		port->COR2 |= COR2_CTSAE;
/*#endif*/
		if (bp->flags&AURORA_BOARD_DTR_FLOW_OK) {
			mcor1 |= AURORA_RXTH;
		}
	}
	/* Enable Software Flow Control. FIXME: I'm not sure about this */
	/* Some people reported that it works, but I still doubt */
	if (I_IXON(tty))  {
		port->COR2 |= COR2_TXIBE;
		cor3 |= (COR3_FCT | COR3_SCDE);
		if (I_IXANY(tty))
			port->COR2 |= COR2_IXM;
		sbus_writeb(START_CHAR(tty),
			    &bp->r[chip]->r[CD180_SCHR1]);
		sbus_writeb(STOP_CHAR(tty),
			    &bp->r[chip]->r[CD180_SCHR2]);
		sbus_writeb(START_CHAR(tty),
			    &bp->r[chip]->r[CD180_SCHR3]);
		sbus_writeb(STOP_CHAR(tty),
			    &bp->r[chip]->r[CD180_SCHR4]);
	}
	if (!C_CLOCAL(tty))  {
		/* Enable CD check */
		port->SRER |= SRER_CD;
		mcor1 |= MCOR1_CDZD;
		mcor2 |= MCOR2_CDOD;
	}
	
	if (C_CREAD(tty)) 
		/* Enable receiver */
		port->SRER |= SRER_RXD;
	
	/* Set input FIFO size (1-8 bytes) */
	cor3 |= AURORA_RXFIFO; 
	/* Setting up CD180 channel registers */
	sbus_writeb(cor1, &bp->r[chip]->r[CD180_COR1]);
	sbus_writeb(port->COR2, &bp->r[chip]->r[CD180_COR2]);
	sbus_writeb(cor3, &bp->r[chip]->r[CD180_COR3]);
	/* Make CD180 know about registers change */
	aurora_wait_CCR(bp->r[chip]);
	sbus_writeb(CCR_CORCHG1 | CCR_CORCHG2 | CCR_CORCHG3,
		    &bp->r[chip]->r[CD180_CCR]);
	/* Setting up modem option registers */
	sbus_writeb(mcor1, &bp->r[chip]->r[CD180_MCOR1]);
	sbus_writeb(mcor2, &bp->r[chip]->r[CD180_MCOR2]);
	/* Enable CD180 transmitter & receiver */
	aurora_wait_CCR(bp->r[chip]);
	sbus_writeb(CCR_TXEN | CCR_RXEN, &bp->r[chip]->r[CD180_CCR]);
	/* Enable interrupts */
	sbus_writeb(port->SRER, &bp->r[chip]->r[CD180_SRER]);
	/* And finally set RTS on */
	sbus_writeb(port->MSVR, &bp->r[chip]->r[CD180_MSVR]);
#ifdef AURORA_DEBUG
	printk("aurora_change_speed: end\n");
#endif
}

/* Must be called with interrupts enabled */
static int aurora_setup_port(struct Aurora_board *bp, struct Aurora_port *port)
{
	unsigned long flags;
	
#ifdef AURORA_DEBUG
	printk("aurora_setup_port: start %d\n",port_No(port));
#endif
	if (port->flags & ASYNC_INITIALIZED)
		return 0;
		
	if (!port->xmit_buf) {
		/* We may sleep in get_zeroed_page() */
		unsigned long tmp;
		
		if (!(tmp = get_zeroed_page(GFP_KERNEL)))
			return -ENOMEM;
		    
		if (port->xmit_buf) {
			free_page(tmp);
			return -ERESTARTSYS;
		}
		port->xmit_buf = (unsigned char *) tmp;
	}
		
	save_flags(flags); cli();
		
	if (port->tty) 
		clear_bit(TTY_IO_ERROR, &port->tty->flags);
		
#ifdef MODULE
	if ((port->count == 1) && ((++bp->count) == 1))
			bp->flags |= AURORA_BOARD_ACTIVE;
#endif

	port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;
	aurora_change_speed(bp, port);
	port->flags |= ASYNC_INITIALIZED;
		
	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_setup_port: end\n");
#endif
	return 0;
}

/* Must be called with interrupts disabled */
static void aurora_shutdown_port(struct Aurora_board *bp, struct Aurora_port *port)
{
	struct tty_struct *tty;
	unsigned char chip;

#ifdef AURORA_DEBUG
	printk("aurora_shutdown_port: start\n");
#endif
	if (!(port->flags & ASYNC_INITIALIZED)) 
		return;
	
	chip = AURORA_CD180(port_No(port));
	
#ifdef AURORA_REPORT_OVERRUN
	printk("aurora%d: port %d: Total %ld overruns were detected.\n",
	       board_No(bp), port_No(port), port->overrun);
#endif	
#ifdef AURORA_REPORT_FIFO
	{
		int i;
		
		printk("aurora%d: port %d: FIFO hits [ ",
		       board_No(bp), port_No(port));
		for (i = 0; i < 10; i++)  {
			printk("%ld ", port->hits[i]);
		}
		printk("].\n");
	}
#endif	
	if (port->xmit_buf)  {
		free_page((unsigned long) port->xmit_buf);
		port->xmit_buf = NULL;
	}

	if (!(tty = port->tty) || C_HUPCL(tty))  {
		/* Drop DTR */
		port->MSVR &= ~(bp->DTR|bp->RTS);
		sbus_writeb(port->MSVR,
			    &bp->r[chip]->r[CD180_MSVR]);
	}
	
        /* Select port */
	sbus_writeb(port_No(port) & 7,
		    &bp->r[chip]->r[CD180_CAR]);
	udelay(1);

	/* Reset port */
	aurora_wait_CCR(bp->r[chip]);
	sbus_writeb(CCR_SOFTRESET, &bp->r[chip]->r[CD180_CCR]);

	/* Disable all interrupts from this port */
	port->SRER = 0;
	sbus_writeb(port->SRER, &bp->r[chip]->r[CD180_SRER]);
	
	if (tty)  
		set_bit(TTY_IO_ERROR, &tty->flags);
	port->flags &= ~ASYNC_INITIALIZED;

#ifdef MODULE
	if (--bp->count < 0)  {
		printk(KERN_DEBUG "aurora%d: aurora_shutdown_port: "
		       "bad board count: %d\n",
		       board_No(bp), bp->count);
		bp->count = 0;
	}
	
	if (!bp->count)
		bp->flags &= ~AURORA_BOARD_ACTIVE;
#endif

#ifdef AURORA_DEBUG
	printk("aurora_shutdown_port: end\n");
#endif
}

	
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct Aurora_port *port)
{
	DECLARE_WAITQUEUE(wait, current);
	struct Aurora_board *bp = port_Board(port);
	int    retval;
	int    do_clocal = 0;
	int    CD;
	unsigned char chip;
	
#ifdef AURORA_DEBUG
	printk("block_til_ready: start\n");
#endif
	chip = AURORA_CD180(port_No(port));

	/* If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) || port->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&port->close_wait);
		if (port->flags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
	}

	/* If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		port->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (C_CLOCAL(tty))  
		do_clocal = 1;

	/* Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&port->open_wait, &wait);
	cli();
	if (!tty_hung_up_p(filp))
		port->count--;
	sti();
	port->blocked_open++;
	while (1) {
		cli();
		sbus_writeb(port_No(port) & 7,
			    &bp->r[chip]->r[CD180_CAR]);
		udelay(1);
		CD = sbus_readb(&bp->r[chip]->r[CD180_MSVR]) & MSVR_CD;
		port->MSVR=bp->RTS;

		/* auto drops DTR */
		sbus_writeb(port->MSVR, &bp->r[chip]->r[CD180_MSVR]);
		sti();
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(port->flags & ASYNC_INITIALIZED)) {
			if (port->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
			break;
		}
		if (!(port->flags & ASYNC_CLOSING) &&
		    (do_clocal || CD))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&port->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		port->count++;
	port->blocked_open--;
	if (retval)
		return retval;
	
	port->flags |= ASYNC_NORMAL_ACTIVE;
#ifdef AURORA_DEBUG
	printk("block_til_ready: end\n");
#endif
	return 0;
}	

static int aurora_open(struct tty_struct * tty, struct file * filp)
{
	int board;
	int error;
	struct Aurora_port * port;
	struct Aurora_board * bp;
	unsigned long flags;
	
#ifdef AURORA_DEBUG
	printk("aurora_open: start\n");
#endif
	
	board = AURORA_BOARD(tty->index);
	if (board > AURORA_NBOARD ||
	    !(aurora_board[board].flags & AURORA_BOARD_PRESENT)) {
#ifdef AURORA_DEBUG
		printk("aurora_open: error board %d present %d\n",
		       board, aurora_board[board].flags & AURORA_BOARD_PRESENT);
#endif
		return -ENODEV;
	}
	
	bp = &aurora_board[board];
	port = aurora_port + board * AURORA_NPORT * AURORA_NCD180 + AURORA_PORT(tty->index);
	if ((aurora_paranoia_check(port, tty->name, "aurora_open")) {
#ifdef AURORA_DEBUG
		printk("aurora_open: error paranoia check\n");
#endif
		return -ENODEV;
	}
	
	port->count++;
	tty->driver_data = port;
	port->tty = tty;
	
	if ((error = aurora_setup_port(bp, port))) {
#ifdef AURORA_DEBUG
		printk("aurora_open: error aurora_setup_port ret %d\n",error);
#endif
		return error;
	}

	if ((error = block_til_ready(tty, filp, port))) {
#ifdef AURORA_DEBUG
		printk("aurora_open: error block_til_ready ret %d\n",error);
#endif
		return error;
	}
	
#ifdef AURORA_DEBUG
	printk("aurora_open: end\n");
#endif
	return 0;
}

static void aurora_close(struct tty_struct * tty, struct file * filp)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	struct Aurora_board *bp;
	unsigned long flags;
	unsigned long timeout;
	unsigned char chip;
	
#ifdef AURORA_DEBUG
	printk("aurora_close: start\n");
#endif
	
	if (!port || (aurora_paranoia_check(port, tty->name, "close"))
		return;
	
	chip = AURORA_CD180(port_No(port));

	save_flags(flags); cli();
	if (tty_hung_up_p(filp))  {
		restore_flags(flags);
		return;
	}
	
	bp = port_Board(port);
	if ((tty->count == 1) && (port->count != 1))  {
		printk(KERN_DEBUG "aurora%d: aurora_close: bad port count; "
		       "tty->count is 1, port count is %d\n",
		       board_No(bp), port->count);
		port->count = 1;
	}
	if (--port->count < 0)  {
		printk(KERN_DEBUG "aurora%d: aurora_close: bad port "
		       "count for tty%d: %d\n",
		       board_No(bp), port_No(port), port->count);
		port->count = 0;
	}
	if (port->count)  {
		restore_flags(flags);
		return;
	}
	port->flags |= ASYNC_CLOSING;

	/* Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (port->closing_wait != ASYNC_CLOSING_WAIT_NONE){
#ifdef AURORA_DEBUG
		printk("aurora_close: waiting to flush...\n");
#endif
		tty_wait_until_sent(tty, port->closing_wait);
	}

	/* At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	port->SRER &= ~SRER_RXD;
	if (port->flags & ASYNC_INITIALIZED) {
		port->SRER &= ~SRER_TXRDY;
		port->SRER |= SRER_TXEMPTY;
		sbus_writeb(port_No(port) & 7,
			    &bp->r[chip]->r[CD180_CAR]);
		udelay(1);
		sbus_writeb(port->SRER, &bp->r[chip]->r[CD180_SRER]);
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		timeout = jiffies+HZ;
		while(port->SRER & SRER_TXEMPTY)  {
			msleep_interruptible(jiffies_to_msecs(port->timeout));
			if (time_after(jiffies, timeout))
				break;
		}
	}
#ifdef AURORA_DEBUG
	printk("aurora_close: shutdown_port\n");
#endif
	aurora_shutdown_port(bp, port);
	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	tty_ldisc_flush(tty);
	tty->closing = 0;
	port->event = 0;
	port->tty = 0;
	if (port->blocked_open) {
		if (port->close_delay) {
			msleep_interruptible(jiffies_to_msecs(port->close_delay));
		}
		wake_up_interruptible(&port->open_wait);
	}
	port->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CLOSING);
	wake_up_interruptible(&port->close_wait);
	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_close: end\n");
#endif
}

static int aurora_write(struct tty_struct * tty, 
			const unsigned char *buf, int count)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	struct Aurora_board *bp;
	int c, total = 0;
	unsigned long flags;
	unsigned char chip;

#ifdef AURORA_DEBUG
	printk("aurora_write: start %d\n",count);
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_write"))
		return 0;
		
	chip = AURORA_CD180(port_No(port));
	
	bp = port_Board(port);

	if (!tty || !port->xmit_buf || !tmp_buf)
		return 0;

	save_flags(flags);
	while (1) {
		cli();
		c = min(count, min(SERIAL_XMIT_SIZE - port->xmit_cnt - 1,
				   SERIAL_XMIT_SIZE - port->xmit_head));
		if (c <= 0) {
			restore_flags(flags);
			break;
		}
		memcpy(port->xmit_buf + port->xmit_head, buf, c);
		port->xmit_head = (port->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
		port->xmit_cnt += c;
		restore_flags(flags);

		buf += c;
		count -= c;
		total += c;
	}

	cli();
	if (port->xmit_cnt && !tty->stopped && !tty->hw_stopped &&
	    !(port->SRER & SRER_TXRDY)) {
		port->SRER |= SRER_TXRDY;
		sbus_writeb(port_No(port) & 7,
			    &bp->r[chip]->r[CD180_CAR]);
		udelay(1);
		sbus_writeb(port->SRER, &bp->r[chip]->r[CD180_SRER]);
	}
	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_write: end %d\n",total);
#endif
	return total;
}

static void aurora_put_char(struct tty_struct * tty, unsigned char ch)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	unsigned long flags;

#ifdef AURORA_DEBUG
	printk("aurora_put_char: start %c\n",ch);
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_put_char"))
		return;

	if (!tty || !port->xmit_buf)
		return;

	save_flags(flags); cli();
	
	if (port->xmit_cnt >= SERIAL_XMIT_SIZE - 1) {
		restore_flags(flags);
		return;
	}

	port->xmit_buf[port->xmit_head++] = ch;
	port->xmit_head &= SERIAL_XMIT_SIZE - 1;
	port->xmit_cnt++;
	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_put_char: end\n");
#endif
}

static void aurora_flush_chars(struct tty_struct * tty)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	unsigned long flags;
	unsigned char chip;

/*#ifdef AURORA_DEBUG
	printk("aurora_flush_chars: start\n");
#endif*/
	if ((aurora_paranoia_check(port, tty->name, "aurora_flush_chars"))
		return;
		
	chip = AURORA_CD180(port_No(port));
	
	if (port->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !port->xmit_buf)
		return;

	save_flags(flags); cli();
	port->SRER |= SRER_TXRDY;
	sbus_writeb(port_No(port) & 7,
		    &port_Board(port)->r[chip]->r[CD180_CAR]);
	udelay(1);
	sbus_writeb(port->SRER,
		    &port_Board(port)->r[chip]->r[CD180_SRER]);
	restore_flags(flags);
/*#ifdef AURORA_DEBUG
	printk("aurora_flush_chars: end\n");
#endif*/
}

static int aurora_write_room(struct tty_struct * tty)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	int	ret;

#ifdef AURORA_DEBUG
	printk("aurora_write_room: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_write_room"))
		return 0;

	ret = SERIAL_XMIT_SIZE - port->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
#ifdef AURORA_DEBUG
	printk("aurora_write_room: end\n");
#endif
	return ret;
}

static int aurora_chars_in_buffer(struct tty_struct *tty)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
				
	if ((aurora_paranoia_check(port, tty->name, "aurora_chars_in_buffer"))
		return 0;
	
	return port->xmit_cnt;
}

static void aurora_flush_buffer(struct tty_struct *tty)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	unsigned long flags;

#ifdef AURORA_DEBUG
	printk("aurora_flush_buffer: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_flush_buffer"))
		return;

	save_flags(flags); cli();
	port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;
	restore_flags(flags);
	
	tty_wakeup(tty);
#ifdef AURORA_DEBUG
	printk("aurora_flush_buffer: end\n");
#endif
}

static int aurora_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	struct Aurora_board * bp;
	unsigned char status,chip;
	unsigned int result;
	unsigned long flags;

#ifdef AURORA_DEBUG
	printk("aurora_get_modem_info: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, __FUNCTION__))
		return -ENODEV;

	chip = AURORA_CD180(port_No(port));

	bp = port_Board(port);

	save_flags(flags); cli();

	sbus_writeb(port_No(port) & 7, &bp->r[chip]->r[CD180_CAR]);
	udelay(1);

	status = sbus_readb(&bp->r[chip]->r[CD180_MSVR]);
	result = 0/*bp->r[chip]->r[AURORA_RI] & (1u << port_No(port)) ? 0 : TIOCM_RNG*/;

	restore_flags(flags);

	result |= ((status & bp->RTS) ? TIOCM_RTS : 0)
		| ((status & bp->DTR) ? TIOCM_DTR : 0)
		| ((status & MSVR_CD)  ? TIOCM_CAR : 0)
		| ((status & MSVR_DSR) ? TIOCM_DSR : 0)
		| ((status & MSVR_CTS) ? TIOCM_CTS : 0);

#ifdef AURORA_DEBUG
	printk("aurora_get_modem_info: end\n");
#endif
	return result;
}

static int aurora_tiocmset(struct tty_struct *tty, struct file *file,
			   unsigned int set, unsigned int clear)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	unsigned int arg;
	unsigned long flags;
	struct Aurora_board *bp = port_Board(port);
	unsigned char chip;

#ifdef AURORA_DEBUG
	printk("aurora_set_modem_info: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, __FUNCTION__))
		return -ENODEV;

	chip = AURORA_CD180(port_No(port));

	save_flags(flags); cli();
	if (set & TIOCM_RTS)
		port->MSVR |= bp->RTS;
	if (set & TIOCM_DTR)
		port->MSVR |= bp->DTR;
	if (clear & TIOCM_RTS)
		port->MSVR &= ~bp->RTS;
	if (clear & TIOCM_DTR)
		port->MSVR &= ~bp->DTR;

	sbus_writeb(port_No(port) & 7, &bp->r[chip]->r[CD180_CAR]);
	udelay(1);

	sbus_writeb(port->MSVR, &bp->r[chip]->r[CD180_MSVR]);

	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_set_modem_info: end\n");
#endif
	return 0;
}

static void aurora_send_break(struct Aurora_port * port, unsigned long length)
{
	struct Aurora_board *bp = port_Board(port);
	unsigned long flags;
	unsigned char chip;
	
#ifdef AURORA_DEBUG
	printk("aurora_send_break: start\n");
#endif
	chip = AURORA_CD180(port_No(port));
	
	save_flags(flags); cli();

	port->break_length = AURORA_TPS / HZ * length;
	port->COR2 |= COR2_ETC;
	port->SRER  |= SRER_TXRDY;
	sbus_writeb(port_No(port) & 7, &bp->r[chip]->r[CD180_CAR]);
	udelay(1);

	sbus_writeb(port->COR2, &bp->r[chip]->r[CD180_COR2]);
	sbus_writeb(port->SRER, &bp->r[chip]->r[CD180_SRER]);
	aurora_wait_CCR(bp->r[chip]);

	sbus_writeb(CCR_CORCHG2, &bp->r[chip]->r[CD180_CCR]);
	aurora_wait_CCR(bp->r[chip]);

	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_send_break: end\n");
#endif
}

static int aurora_set_serial_info(struct Aurora_port * port,
				  struct serial_struct * newinfo)
{
	struct serial_struct tmp;
	struct Aurora_board *bp = port_Board(port);
	int change_speed;
	unsigned long flags;

#ifdef AURORA_DEBUG
	printk("aurora_set_serial_info: start\n");
#endif
	if (copy_from_user(&tmp, newinfo, sizeof(tmp)))
		return -EFAULT;
#if 0	
	if ((tmp.irq != bp->irq) ||
	    (tmp.port != bp->base) ||
	    (tmp.type != PORT_CIRRUS) ||
	    (tmp.baud_base != (bp->oscfreq + CD180_TPC/2) / CD180_TPC) ||
	    (tmp.custom_divisor != 0) ||
	    (tmp.xmit_fifo_size != CD180_NFIFO) ||
	    (tmp.flags & ~AURORA_LEGAL_FLAGS))
		return -EINVAL;
#endif	
	
	change_speed = ((port->flags & ASYNC_SPD_MASK) !=
			(tmp.flags & ASYNC_SPD_MASK));
	
	if (!capable(CAP_SYS_ADMIN)) {
		if ((tmp.close_delay != port->close_delay) ||
		    (tmp.closing_wait != port->closing_wait) ||
		    ((tmp.flags & ~ASYNC_USR_MASK) !=
		     (port->flags & ~ASYNC_USR_MASK)))  
			return -EPERM;
		port->flags = ((port->flags & ~ASYNC_USR_MASK) |
			       (tmp.flags & ASYNC_USR_MASK));
	} else  {
		port->flags = ((port->flags & ~ASYNC_FLAGS) |
			       (tmp.flags & ASYNC_FLAGS));
		port->close_delay = tmp.close_delay;
		port->closing_wait = tmp.closing_wait;
	}
	if (change_speed)  {
		save_flags(flags); cli();
		aurora_change_speed(bp, port);
		restore_flags(flags);
	}
#ifdef AURORA_DEBUG
	printk("aurora_set_serial_info: end\n");
#endif
	return 0;
}

extern int aurora_get_serial_info(struct Aurora_port * port,
				  struct serial_struct * retinfo)
{
	struct serial_struct tmp;
	struct Aurora_board *bp = port_Board(port);
	
#ifdef AURORA_DEBUG
	printk("aurora_get_serial_info: start\n");
#endif
	if (!access_ok(VERIFY_WRITE, (void *) retinfo, sizeof(tmp)))
		return -EFAULT;
	
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = PORT_CIRRUS;
	tmp.line = port - aurora_port;
	tmp.port = 0;
	tmp.irq  = bp->irq;
	tmp.flags = port->flags;
	tmp.baud_base = (bp->oscfreq + CD180_TPC/2) / CD180_TPC;
	tmp.close_delay = port->close_delay * HZ/100;
	tmp.closing_wait = port->closing_wait * HZ/100;
	tmp.xmit_fifo_size = CD180_NFIFO;
	copy_to_user(retinfo, &tmp, sizeof(tmp));
#ifdef AURORA_DEBUG
printk("aurora_get_serial_info: end\n");
#endif
	return 0;
}

static int aurora_ioctl(struct tty_struct * tty, struct file * filp, 
		    unsigned int cmd, unsigned long arg)
		    
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	int retval;

#ifdef AURORA_DEBUG
	printk("aurora_ioctl: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_ioctl"))
		return -ENODEV;
	
	switch (cmd) {
	case TCSBRK:	/* SVID version: non-zero arg --> no break */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		if (!arg)
			aurora_send_break(port, HZ/4);	/* 1/4 second */
		return 0;
	case TCSBRKP:	/* support for POSIX tcsendbreak() */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		aurora_send_break(port, arg ? arg*(HZ/10) : HZ/4);
		return 0;
	case TIOCGSOFTCAR:
		return put_user(C_CLOCAL(tty) ? 1 : 0, (unsigned long *)arg);
	case TIOCSSOFTCAR:
		if (get_user(arg,(unsigned long *)arg))
			return -EFAULT;
		tty->termios->c_cflag =
			((tty->termios->c_cflag & ~CLOCAL) |
			 (arg ? CLOCAL : 0));
		return 0;
	case TIOCGSERIAL:	
		return aurora_get_serial_info(port, (struct serial_struct *) arg);
	case TIOCSSERIAL:	
		return aurora_set_serial_info(port, (struct serial_struct *) arg);
	default:
		return -ENOIOCTLCMD;
	};
#ifdef AURORA_DEBUG
	printk("aurora_ioctl: end\n");
#endif
	return 0;
}

static void aurora_throttle(struct tty_struct * tty)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	struct Aurora_board *bp;
	unsigned long flags;
	unsigned char chip;

#ifdef AURORA_DEBUG
	printk("aurora_throttle: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_throttle"))
		return;
	
	bp = port_Board(port);
	chip = AURORA_CD180(port_No(port));
	
	save_flags(flags); cli();
	port->MSVR &= ~bp->RTS;
	sbus_writeb(port_No(port) & 7, &bp->r[chip]->r[CD180_CAR]);
	udelay(1);
	if (I_IXOFF(tty))  {
		aurora_wait_CCR(bp->r[chip]);
		sbus_writeb(CCR_SSCH2, &bp->r[chip]->r[CD180_CCR]);
		aurora_wait_CCR(bp->r[chip]);
	}
	sbus_writeb(port->MSVR, &bp->r[chip]->r[CD180_MSVR]);
	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_throttle: end\n");
#endif
}

static void aurora_unthrottle(struct tty_struct * tty)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	struct Aurora_board *bp;
	unsigned long flags;
	unsigned char chip;

#ifdef AURORA_DEBUG
	printk("aurora_unthrottle: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_unthrottle"))
		return;
	
	bp = port_Board(port);
	
	chip = AURORA_CD180(port_No(port));
	
	save_flags(flags); cli();
	port->MSVR |= bp->RTS;
	sbus_writeb(port_No(port) & 7,
		    &bp->r[chip]->r[CD180_CAR]);
	udelay(1);
	if (I_IXOFF(tty))  {
		aurora_wait_CCR(bp->r[chip]);
		sbus_writeb(CCR_SSCH1,
			    &bp->r[chip]->r[CD180_CCR]);
		aurora_wait_CCR(bp->r[chip]);
	}
	sbus_writeb(port->MSVR, &bp->r[chip]->r[CD180_MSVR]);
	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_unthrottle: end\n");
#endif
}

static void aurora_stop(struct tty_struct * tty)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	struct Aurora_board *bp;
	unsigned long flags;
	unsigned char chip;

#ifdef AURORA_DEBUG
	printk("aurora_stop: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_stop"))
		return;
	
	bp = port_Board(port);
	
	chip = AURORA_CD180(port_No(port));
	
	save_flags(flags); cli();
	port->SRER &= ~SRER_TXRDY;
	sbus_writeb(port_No(port) & 7,
		    &bp->r[chip]->r[CD180_CAR]);
	udelay(1);
	sbus_writeb(port->SRER,
		    &bp->r[chip]->r[CD180_SRER]);
	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_stop: end\n");
#endif
}

static void aurora_start(struct tty_struct * tty)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	struct Aurora_board *bp;
	unsigned long flags;
	unsigned char chip;

#ifdef AURORA_DEBUG
	printk("aurora_start: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_start"))
		return;
	
	bp = port_Board(port);
	
	chip = AURORA_CD180(port_No(port));
	
	save_flags(flags); cli();
	if (port->xmit_cnt && port->xmit_buf && !(port->SRER & SRER_TXRDY))  {
		port->SRER |= SRER_TXRDY;
		sbus_writeb(port_No(port) & 7,
			    &bp->r[chip]->r[CD180_CAR]);
		udelay(1);
		sbus_writeb(port->SRER,
			    &bp->r[chip]->r[CD180_SRER]);
	}
	restore_flags(flags);
#ifdef AURORA_DEBUG
	printk("aurora_start: end\n");
#endif
}

/*
 * This routine is called from the scheduler tqueue when the interrupt
 * routine has signalled that a hangup has occurred.  The path of
 * hangup processing is:
 *
 * 	serial interrupt routine -> (scheduler tqueue) ->
 * 	do_aurora_hangup() -> tty->hangup() -> aurora_hangup()
 * 
 */
static void do_aurora_hangup(void *private_)
{
	struct Aurora_port	*port = (struct Aurora_port *) private_;
	struct tty_struct	*tty;

#ifdef AURORA_DEBUG
	printk("do_aurora_hangup: start\n");
#endif
	tty = port->tty;
	if (tty != NULL) {
		tty_hangup(tty);	/* FIXME: module removal race - AKPM */
#ifdef AURORA_DEBUG
		printk("do_aurora_hangup: end\n");
#endif
	}
}

static void aurora_hangup(struct tty_struct * tty)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	struct Aurora_board *bp;
				
#ifdef AURORA_DEBUG
	printk("aurora_hangup: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_hangup"))
		return;
	
	bp = port_Board(port);
	
	aurora_shutdown_port(bp, port);
	port->event = 0;
	port->count = 0;
	port->flags &= ~ASYNC_NORMAL_ACTIVE;
	port->tty = 0;
	wake_up_interruptible(&port->open_wait);
#ifdef AURORA_DEBUG
	printk("aurora_hangup: end\n");
#endif
}

static void aurora_set_termios(struct tty_struct * tty, struct termios * old_termios)
{
	struct Aurora_port *port = (struct Aurora_port *) tty->driver_data;
	unsigned long flags;

#ifdef AURORA_DEBUG
	printk("aurora_set_termios: start\n");
#endif
	if ((aurora_paranoia_check(port, tty->name, "aurora_set_termios"))
		return;
	
	if (tty->termios->c_cflag == old_termios->c_cflag &&
	    tty->termios->c_iflag == old_termios->c_iflag)
		return;

	save_flags(flags); cli();
	aurora_change_speed(port_Board(port), port);
	restore_flags(flags);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		aurora_start(tty);
	}
#ifdef AURORA_DEBUG
	printk("aurora_set_termios: end\n");
#endif
}

static void do_aurora_bh(void)
{
	 run_task_queue(&tq_aurora);
}

static void do_softint(void *private_)
{
	struct Aurora_port	*port = (struct Aurora_port *) private_;
	struct tty_struct	*tty;

#ifdef AURORA_DEBUG
	printk("do_softint: start\n");
#endif
	tty = port->tty;
	if (tty == NULL)
		return;

	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &port->event)) {
		tty_wakeup(tty);
	}
#ifdef AURORA_DEBUG
	printk("do_softint: end\n");
#endif
}

static struct tty_operations aurora_ops = {
	.open  = aurora_open,
	.close = aurora_close,
	.write = aurora_write,
	.put_char = aurora_put_char,
	.flush_chars = aurora_flush_chars,
	.write_room = aurora_write_room,
	.chars_in_buffer = aurora_chars_in_buffer,
	.flush_buffer = aurora_flush_buffer,
	.ioctl = aurora_ioctl,
	.throttle = aurora_throttle,
	.unthrottle = aurora_unthrottle,
	.set_termios = aurora_set_termios,
	.stop = aurora_stop,
	.start = aurora_start,
	.hangup = aurora_hangup,
	.tiocmget = aurora_tiocmget,
	.tiocmset = aurora_tiocmset,
};

static int aurora_init_drivers(void)
{
	int error;
	int i;

#ifdef AURORA_DEBUG
	printk("aurora_init_drivers: start\n");
#endif
	tmp_buf = (unsigned char *) get_zeroed_page(GFP_KERNEL);
	if (tmp_buf == NULL) {
		printk(KERN_ERR "aurora: Couldn't get free page.\n");
		return 1;
	}
	init_bh(AURORA_BH, do_aurora_bh);
	aurora_driver = alloc_tty_driver(AURORA_INPORTS);
	if (!aurora_driver) {
		printk(KERN_ERR "aurora: Couldn't allocate tty driver.\n");
		free_page((unsigned long) tmp_buf);
		return 1;
	}
	aurora_driver->owner = THIS_MODULE;
	aurora_driver->name = "ttyA";
	aurora_driver->major = AURORA_MAJOR;
	aurora_driver->type = TTY_DRIVER_TYPE_SERIAL;
	aurora_driver->subtype = SERIAL_TYPE_NORMAL;
	aurora_driver->init_termios = tty_std_termios;
	aurora_driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	aurora_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(aurora_driver, &aurora_ops);
	error = tty_register_driver(aurora_driver);
	if (error) {
		put_tty_driver(aurora_driver);
		free_page((unsigned long) tmp_buf);
		printk(KERN_ERR "aurora: Couldn't register aurora driver, error = %d\n",
		       error);
		return 1;
	}
	
	memset(aurora_port, 0, sizeof(aurora_port));
	for (i = 0; i < AURORA_TNPORTS; i++)  {
		aurora_port[i].magic = AURORA_MAGIC;
		aurora_port[i].tqueue.routine = do_softint;
		aurora_port[i].tqueue.data = &aurora_port[i];
		aurora_port[i].tqueue_hangup.routine = do_aurora_hangup;
		aurora_port[i].tqueue_hangup.data = &aurora_port[i];
		aurora_port[i].close_delay = 50 * HZ/100;
		aurora_port[i].closing_wait = 3000 * HZ/100;
		init_waitqueue_head(&aurora_port[i].open_wait);
		init_waitqueue_head(&aurora_port[i].close_wait);
	}
#ifdef AURORA_DEBUG
	printk("aurora_init_drivers: end\n");
#endif
	return 0;
}

static void aurora_release_drivers(void)
{
#ifdef AURORA_DEBUG
	printk("aurora_release_drivers: start\n");
#endif
	free_page((unsigned long)tmp_buf);
	tty_unregister_driver(aurora_driver);
	put_tty_driver(aurora_driver);
#ifdef AURORA_DEBUG
	printk("aurora_release_drivers: end\n");
#endif
}

/*
 * Called at boot time.
 *
 * You can specify IO base for up to RC_NBOARD cards,
 * using line "riscom8=0xiobase1,0xiobase2,.." at LILO prompt.
 * Note that there will be no probing at default
 * addresses in this case.
 *
 */
void __init aurora_setup(char *str, int *ints)
{
	int i;

	for(i=0;(i<ints[0])&&(i<4);i++) {
		if (ints[i+1]) irqs[i]=ints[i+1];
		}
}

static int __init aurora_real_init(void)
{
	int found;
	int i;

	printk(KERN_INFO "aurora: Driver starting.\n");
	if(aurora_init_drivers())
		return -EIO;
	found = aurora_probe();
	if(!found) {
		aurora_release_drivers();
		printk(KERN_INFO "aurora: No Aurora Multiport boards detected.\n");
		return -EIO;
	} else {
		printk(KERN_INFO "aurora: %d boards found.\n", found);
	}
	for (i = 0; i < found; i++) {
		int ret = aurora_setup_board(&aurora_board[i]);

		if (ret) {
#ifdef AURORA_DEBUG
			printk(KERN_ERR "aurora_init: error aurora_setup_board ret %d\n",
			       ret);
#endif
			return ret;
		}
	}
	return 0;
}

int irq  = 0;
int irq1 = 0;
int irq2 = 0;
int irq3 = 0;
module_param(irq , int, 0);
module_param(irq1, int, 0);
module_param(irq2, int, 0);
module_param(irq3, int, 0);

static int __init aurora_init(void) 
{
	if (irq ) irqs[0]=irq ;
	if (irq1) irqs[1]=irq1;
	if (irq2) irqs[2]=irq2;
	if (irq3) irqs[3]=irq3;
	return aurora_real_init();
}
	
static void __exit aurora_cleanup(void)
{
	int i;
	
#ifdef AURORA_DEBUG
printk("cleanup_module: aurora_release_drivers\n");
#endif

	aurora_release_drivers();
	for (i = 0; i < AURORA_NBOARD; i++)
		if (aurora_board[i].flags & AURORA_BOARD_PRESENT) {
			aurora_shutdown_board(&aurora_board[i]);
			aurora_release_io_range(&aurora_board[i]);
		}
}

module_init(aurora_init);
module_exit(aurora_cleanup);
MODULE_LICENSE("GPL");
