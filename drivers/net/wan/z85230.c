/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	(c) Copyright 1998 Alan Cox <alan@lxorguk.ukuu.org.uk>
 *	(c) Copyright 2000, 2001 Red Hat Inc
 *
 *	Development of this driver was funded by Equiinet Ltd
 *			http://www.equiinet.com
 *
 *	ChangeLog:
 *
 *	Asynchronous mode dropped for 2.2. For 2.5 we will attempt the
 *	unification of all the Z85x30 asynchronous drivers for real.
 *
 *	DMA now uses get_free_page as kmalloc buffers may span a 64K 
 *	boundary.
 *
 *	Modified for SMP safety and SMP locking by Alan Cox <alan@redhat.com>
 *
 *	Performance
 *
 *	Z85230:
 *	Non DMA you want a 486DX50 or better to do 64Kbits. 9600 baud
 *	X.25 is not unrealistic on all machines. DMA mode can in theory
 *	handle T1/E1 quite nicely. In practice the limit seems to be about
 *	512Kbit->1Mbit depending on motherboard.
 *
 *	Z85C30:
 *	64K will take DMA, 9600 baud X.25 should be ok.
 *
 *	Z8530:
 *	Synchronous mode without DMA is unlikely to pass about 2400 baud.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <asm/dma.h>
#include <asm/io.h>
#define RT_LOCK
#define RT_UNLOCK
#include <linux/spinlock.h>

#include <net/syncppp.h>
#include "z85230.h"


/**
 *	z8530_read_port - Architecture specific interface function
 *	@p: port to read
 *
 *	Provided port access methods. The Comtrol SV11 requires no delays
 *	between accesses and uses PC I/O. Some drivers may need a 5uS delay
 *	
 *	In the longer term this should become an architecture specific
 *	section so that this can become a generic driver interface for all
 *	platforms. For now we only handle PC I/O ports with or without the
 *	dread 5uS sanity delay.
 *
 *	The caller must hold sufficient locks to avoid violating the horrible
 *	5uS delay rule.
 */

static inline int z8530_read_port(unsigned long p)
{
	u8 r=inb(Z8530_PORT_OF(p));
	if(p&Z8530_PORT_SLEEP)	/* gcc should figure this out efficiently ! */
		udelay(5);
	return r;
}

/**
 *	z8530_write_port - Architecture specific interface function
 *	@p: port to write
 *	@d: value to write
 *
 *	Write a value to a port with delays if need be. Note that the
 *	caller must hold locks to avoid read/writes from other contexts
 *	violating the 5uS rule
 *
 *	In the longer term this should become an architecture specific
 *	section so that this can become a generic driver interface for all
 *	platforms. For now we only handle PC I/O ports with or without the
 *	dread 5uS sanity delay.
 */


static inline void z8530_write_port(unsigned long p, u8 d)
{
	outb(d,Z8530_PORT_OF(p));
	if(p&Z8530_PORT_SLEEP)
		udelay(5);
}



static void z8530_rx_done(struct z8530_channel *c);
static void z8530_tx_done(struct z8530_channel *c);


/**
 *	read_zsreg - Read a register from a Z85230 
 *	@c: Z8530 channel to read from (2 per chip)
 *	@reg: Register to read
 *	FIXME: Use a spinlock.
 *	
 *	Most of the Z8530 registers are indexed off the control registers.
 *	A read is done by writing to the control register and reading the
 *	register back.  The caller must hold the lock
 */
 
static inline u8 read_zsreg(struct z8530_channel *c, u8 reg)
{
	if(reg)
		z8530_write_port(c->ctrlio, reg);
	return z8530_read_port(c->ctrlio);
}

/**
 *	read_zsdata - Read the data port of a Z8530 channel
 *	@c: The Z8530 channel to read the data port from
 *
 *	The data port provides fast access to some things. We still
 *	have all the 5uS delays to worry about.
 */

static inline u8 read_zsdata(struct z8530_channel *c)
{
	u8 r;
	r=z8530_read_port(c->dataio);
	return r;
}

/**
 *	write_zsreg - Write to a Z8530 channel register
 *	@c: The Z8530 channel
 *	@reg: Register number
 *	@val: Value to write
 *
 *	Write a value to an indexed register. The caller must hold the lock
 *	to honour the irritating delay rules. We know about register 0
 *	being fast to access.
 *
 *      Assumes c->lock is held.
 */
static inline void write_zsreg(struct z8530_channel *c, u8 reg, u8 val)
{
	if(reg)
		z8530_write_port(c->ctrlio, reg);
	z8530_write_port(c->ctrlio, val);

}

/**
 *	write_zsctrl - Write to a Z8530 control register
 *	@c: The Z8530 channel
 *	@val: Value to write
 *
 *	Write directly to the control register on the Z8530
 */

static inline void write_zsctrl(struct z8530_channel *c, u8 val)
{
	z8530_write_port(c->ctrlio, val);
}

/**
 *	write_zsdata - Write to a Z8530 control register
 *	@c: The Z8530 channel
 *	@val: Value to write
 *
 *	Write directly to the data register on the Z8530
 */


static inline void write_zsdata(struct z8530_channel *c, u8 val)
{
	z8530_write_port(c->dataio, val);
}

/*
 *	Register loading parameters for a dead port
 */
 
u8 z8530_dead_port[]=
{
	255
};

EXPORT_SYMBOL(z8530_dead_port);

/*
 *	Register loading parameters for currently supported circuit types
 */


/*
 *	Data clocked by telco end. This is the correct data for the UK
 *	"kilostream" service, and most other similar services.
 */
 
u8 z8530_hdlc_kilostream[]=
{
	4,	SYNC_ENAB|SDLC|X1CLK,
	2,	0,	/* No vector */
	1,	0,
	3,	ENT_HM|RxCRC_ENAB|Rx8,
	5,	TxCRC_ENAB|RTS|TxENAB|Tx8|DTR,
	9,	0,		/* Disable interrupts */
	6,	0xFF,
	7,	FLAG,
	10,	ABUNDER|NRZ|CRCPS,/*MARKIDLE ??*/
	11,	TCTRxCP,
	14,	DISDPLL,
	15,	DCDIE|SYNCIE|CTSIE|TxUIE|BRKIE,
	1,	EXT_INT_ENAB|TxINT_ENAB|INT_ALL_Rx,
	9,	NV|MIE|NORESET,
	255
};

EXPORT_SYMBOL(z8530_hdlc_kilostream);

/*
 *	As above but for enhanced chips.
 */
 
u8 z8530_hdlc_kilostream_85230[]=
{
	4,	SYNC_ENAB|SDLC|X1CLK,
	2,	0,	/* No vector */
	1,	0,
	3,	ENT_HM|RxCRC_ENAB|Rx8,
	5,	TxCRC_ENAB|RTS|TxENAB|Tx8|DTR,
	9,	0,		/* Disable interrupts */
	6,	0xFF,
	7,	FLAG,
	10,	ABUNDER|NRZ|CRCPS,	/* MARKIDLE?? */
	11,	TCTRxCP,
	14,	DISDPLL,
	15,	DCDIE|SYNCIE|CTSIE|TxUIE|BRKIE,
	1,	EXT_INT_ENAB|TxINT_ENAB|INT_ALL_Rx,
	9,	NV|MIE|NORESET,
	23,	3,		/* Extended mode AUTO TX and EOM*/
	
	255
};

EXPORT_SYMBOL(z8530_hdlc_kilostream_85230);

/**
 *	z8530_flush_fifo - Flush on chip RX FIFO
 *	@c: Channel to flush
 *
 *	Flush the receive FIFO. There is no specific option for this, we 
 *	blindly read bytes and discard them. Reading when there is no data
 *	is harmless. The 8530 has a 4 byte FIFO, the 85230 has 8 bytes.
 *	
 *	All locking is handled for the caller. On return data may still be
 *	present if it arrived during the flush.
 */
 
static void z8530_flush_fifo(struct z8530_channel *c)
{
	read_zsreg(c, R1);
	read_zsreg(c, R1);
	read_zsreg(c, R1);
	read_zsreg(c, R1);
	if(c->dev->type==Z85230)
	{
		read_zsreg(c, R1);
		read_zsreg(c, R1);
		read_zsreg(c, R1);
		read_zsreg(c, R1);
	}
}	

/**
 *	z8530_rtsdtr - Control the outgoing DTS/RTS line
 *	@c: The Z8530 channel to control;
 *	@set: 1 to set, 0 to clear
 *
 *	Sets or clears DTR/RTS on the requested line. All locking is handled
 *	by the caller. For now we assume all boards use the actual RTS/DTR
 *	on the chip. Apparently one or two don't. We'll scream about them
 *	later.
 */

static void z8530_rtsdtr(struct z8530_channel *c, int set)
{
	if (set)
		c->regs[5] |= (RTS | DTR);
	else
		c->regs[5] &= ~(RTS | DTR);
	write_zsreg(c, R5, c->regs[5]);
}

/**
 *	z8530_rx - Handle a PIO receive event
 *	@c: Z8530 channel to process
 *
 *	Receive handler for receiving in PIO mode. This is much like the 
 *	async one but not quite the same or as complex
 *
 *	Note: Its intended that this handler can easily be separated from
 *	the main code to run realtime. That'll be needed for some machines
 *	(eg to ever clock 64kbits on a sparc ;)).
 *
 *	The RT_LOCK macros don't do anything now. Keep the code covered
 *	by them as short as possible in all circumstances - clocks cost
 *	baud. The interrupt handler is assumed to be atomic w.r.t. to
 *	other code - this is true in the RT case too.
 *
 *	We only cover the sync cases for this. If you want 2Mbit async
 *	do it yourself but consider medical assistance first. This non DMA 
 *	synchronous mode is portable code. The DMA mode assumes PCI like 
 *	ISA DMA
 *
 *	Called with the device lock held
 */
 
static void z8530_rx(struct z8530_channel *c)
{
	u8 ch,stat;
	spin_lock(c->lock);
 
	while(1)
	{
		/* FIFO empty ? */
		if(!(read_zsreg(c, R0)&1))
			break;
		ch=read_zsdata(c);
		stat=read_zsreg(c, R1);
	
		/*
		 *	Overrun ?
		 */
		if(c->count < c->max)
		{
			*c->dptr++=ch;
			c->count++;
		}

		if(stat&END_FR)
		{
		
			/*
			 *	Error ?
			 */
			if(stat&(Rx_OVR|CRC_ERR))
			{
				/* Rewind the buffer and return */
				if(c->skb)
					c->dptr=c->skb->data;
				c->count=0;
				if(stat&Rx_OVR)
				{
					printk(KERN_WARNING "%s: overrun\n", c->dev->name);
					c->rx_overrun++;
				}
				if(stat&CRC_ERR)
				{
					c->rx_crc_err++;
					/* printk("crc error\n"); */
				}
				/* Shove the frame upstream */
			}
			else
			{
				/*
				 *	Drop the lock for RX processing, or
		 		 *	there are deadlocks
		 		 */
				z8530_rx_done(c);
				write_zsctrl(c, RES_Rx_CRC);
			}
		}
	}
	/*
	 *	Clear irq
	 */
	write_zsctrl(c, ERR_RES);
	write_zsctrl(c, RES_H_IUS);
	spin_unlock(c->lock);
}


/**
 *	z8530_tx - Handle a PIO transmit event
 *	@c: Z8530 channel to process
 *
 *	Z8530 transmit interrupt handler for the PIO mode. The basic
 *	idea is to attempt to keep the FIFO fed. We fill as many bytes
 *	in as possible, its quite possible that we won't keep up with the
 *	data rate otherwise.
 */
 
static void z8530_tx(struct z8530_channel *c)
{
	spin_lock(c->lock);
	while(c->txcount) {
		/* FIFO full ? */
		if(!(read_zsreg(c, R0)&4))
			break;
		c->txcount--;
		/*
		 *	Shovel out the byte
		 */
		write_zsreg(c, R8, *c->tx_ptr++);
		write_zsctrl(c, RES_H_IUS);
		/* We are about to underflow */
		if(c->txcount==0)
		{
			write_zsctrl(c, RES_EOM_L);
			write_zsreg(c, R10, c->regs[10]&~ABUNDER);
		}
	}

	
	/*
	 *	End of frame TX - fire another one
	 */
	 
	write_zsctrl(c, RES_Tx_P);

	z8530_tx_done(c);	 
	write_zsctrl(c, RES_H_IUS);
	spin_unlock(c->lock);
}

/**
 *	z8530_status - Handle a PIO status exception
 *	@chan: Z8530 channel to process
 *
 *	A status event occurred in PIO synchronous mode. There are several
 *	reasons the chip will bother us here. A transmit underrun means we
 *	failed to feed the chip fast enough and just broke a packet. A DCD
 *	change is a line up or down. We communicate that back to the protocol
 *	layer for synchronous PPP to renegotiate.
 */

static void z8530_status(struct z8530_channel *chan)
{
	u8 status, altered;

	spin_lock(chan->lock);
	status=read_zsreg(chan, R0);
	altered=chan->status^status;
	
	chan->status=status;
	
	if(status&TxEOM)
	{
/*		printk("%s: Tx underrun.\n", chan->dev->name); */
		chan->stats.tx_fifo_errors++;
		write_zsctrl(chan, ERR_RES);
		z8530_tx_done(chan);
	}
		
	if(altered&chan->dcdcheck)
	{
		if(status&chan->dcdcheck)
		{
			printk(KERN_INFO "%s: DCD raised\n", chan->dev->name);
			write_zsreg(chan, R3, chan->regs[3]|RxENABLE);
			if(chan->netdevice &&
			    ((chan->netdevice->type == ARPHRD_HDLC) ||
			    (chan->netdevice->type == ARPHRD_PPP)))
				sppp_reopen(chan->netdevice);
		}
		else
		{
			printk(KERN_INFO "%s: DCD lost\n", chan->dev->name);
			write_zsreg(chan, R3, chan->regs[3]&~RxENABLE);
			z8530_flush_fifo(chan);
		}
		
	}	
	write_zsctrl(chan, RES_EXT_INT);
	write_zsctrl(chan, RES_H_IUS);
	spin_unlock(chan->lock);
}

struct z8530_irqhandler z8530_sync=
{
	z8530_rx,
	z8530_tx,
	z8530_status
};

EXPORT_SYMBOL(z8530_sync);

/**
 *	z8530_dma_rx - Handle a DMA RX event
 *	@chan: Channel to handle
 *
 *	Non bus mastering DMA interfaces for the Z8x30 devices. This
 *	is really pretty PC specific. The DMA mode means that most receive
 *	events are handled by the DMA hardware. We get a kick here only if
 *	a frame ended.
 */
 
static void z8530_dma_rx(struct z8530_channel *chan)
{
	spin_lock(chan->lock);
	if(chan->rxdma_on)
	{
		/* Special condition check only */
		u8 status;
	
		read_zsreg(chan, R7);
		read_zsreg(chan, R6);
		
		status=read_zsreg(chan, R1);
	
		if(status&END_FR)
		{
			z8530_rx_done(chan);	/* Fire up the next one */
		}		
		write_zsctrl(chan, ERR_RES);
		write_zsctrl(chan, RES_H_IUS);
	}
	else
	{
		/* DMA is off right now, drain the slow way */
		z8530_rx(chan);
	}	
	spin_unlock(chan->lock);
}

/**
 *	z8530_dma_tx - Handle a DMA TX event
 *	@chan:	The Z8530 channel to handle
 *
 *	We have received an interrupt while doing DMA transmissions. It
 *	shouldn't happen. Scream loudly if it does.
 */
 
static void z8530_dma_tx(struct z8530_channel *chan)
{
	spin_lock(chan->lock);
	if(!chan->dma_tx)
	{
		printk(KERN_WARNING "Hey who turned the DMA off?\n");
		z8530_tx(chan);
		return;
	}
	/* This shouldnt occur in DMA mode */
	printk(KERN_ERR "DMA tx - bogus event!\n");
	z8530_tx(chan);
	spin_unlock(chan->lock);
}

/**
 *	z8530_dma_status - Handle a DMA status exception
 *	@chan: Z8530 channel to process
 *	
 *	A status event occurred on the Z8530. We receive these for two reasons
 *	when in DMA mode. Firstly if we finished a packet transfer we get one
 *	and kick the next packet out. Secondly we may see a DCD change and
 *	have to poke the protocol layer.
 *
 */
 
static void z8530_dma_status(struct z8530_channel *chan)
{
	u8 status, altered;

	status=read_zsreg(chan, R0);
	altered=chan->status^status;
	
	chan->status=status;


	if(chan->dma_tx)
	{
		if(status&TxEOM)
		{
			unsigned long flags;
	
			flags=claim_dma_lock();
			disable_dma(chan->txdma);
			clear_dma_ff(chan->txdma);	
			chan->txdma_on=0;
			release_dma_lock(flags);
			z8530_tx_done(chan);
		}
	}

	spin_lock(chan->lock);
	if(altered&chan->dcdcheck)
	{
		if(status&chan->dcdcheck)
		{
			printk(KERN_INFO "%s: DCD raised\n", chan->dev->name);
			write_zsreg(chan, R3, chan->regs[3]|RxENABLE);
			if(chan->netdevice &&
			    ((chan->netdevice->type == ARPHRD_HDLC) ||
			    (chan->netdevice->type == ARPHRD_PPP)))
				sppp_reopen(chan->netdevice);
		}
		else
		{
			printk(KERN_INFO "%s:DCD lost\n", chan->dev->name);
			write_zsreg(chan, R3, chan->regs[3]&~RxENABLE);
			z8530_flush_fifo(chan);
		}
	}	

	write_zsctrl(chan, RES_EXT_INT);
	write_zsctrl(chan, RES_H_IUS);
	spin_unlock(chan->lock);
}

struct z8530_irqhandler z8530_dma_sync=
{
	z8530_dma_rx,
	z8530_dma_tx,
	z8530_dma_status
};

EXPORT_SYMBOL(z8530_dma_sync);

struct z8530_irqhandler z8530_txdma_sync=
{
	z8530_rx,
	z8530_dma_tx,
	z8530_dma_status
};

EXPORT_SYMBOL(z8530_txdma_sync);

/**
 *	z8530_rx_clear - Handle RX events from a stopped chip
 *	@c: Z8530 channel to shut up
 *
 *	Receive interrupt vectors for a Z8530 that is in 'parked' mode.
 *	For machines with PCI Z85x30 cards, or level triggered interrupts
 *	(eg the MacII) we must clear the interrupt cause or die.
 */


static void z8530_rx_clear(struct z8530_channel *c)
{
	/*
	 *	Data and status bytes
	 */
	u8 stat;

	read_zsdata(c);
	stat=read_zsreg(c, R1);
	
	if(stat&END_FR)
		write_zsctrl(c, RES_Rx_CRC);
	/*
	 *	Clear irq
	 */
	write_zsctrl(c, ERR_RES);
	write_zsctrl(c, RES_H_IUS);
}

/**
 *	z8530_tx_clear - Handle TX events from a stopped chip
 *	@c: Z8530 channel to shut up
 *
 *	Transmit interrupt vectors for a Z8530 that is in 'parked' mode.
 *	For machines with PCI Z85x30 cards, or level triggered interrupts
 *	(eg the MacII) we must clear the interrupt cause or die.
 */

static void z8530_tx_clear(struct z8530_channel *c)
{
	write_zsctrl(c, RES_Tx_P);
	write_zsctrl(c, RES_H_IUS);
}

/**
 *	z8530_status_clear - Handle status events from a stopped chip
 *	@chan: Z8530 channel to shut up
 *
 *	Status interrupt vectors for a Z8530 that is in 'parked' mode.
 *	For machines with PCI Z85x30 cards, or level triggered interrupts
 *	(eg the MacII) we must clear the interrupt cause or die.
 */

static void z8530_status_clear(struct z8530_channel *chan)
{
	u8 status=read_zsreg(chan, R0);
	if(status&TxEOM)
		write_zsctrl(chan, ERR_RES);
	write_zsctrl(chan, RES_EXT_INT);
	write_zsctrl(chan, RES_H_IUS);
}

struct z8530_irqhandler z8530_nop=
{
	z8530_rx_clear,
	z8530_tx_clear,
	z8530_status_clear
};


EXPORT_SYMBOL(z8530_nop);

/**
 *	z8530_interrupt - Handle an interrupt from a Z8530
 *	@irq: 	Interrupt number
 *	@dev_id: The Z8530 device that is interrupting.
 *	@regs: unused
 *
 *	A Z85[2]30 device has stuck its hand in the air for attention.
 *	We scan both the channels on the chip for events and then call
 *	the channel specific call backs for each channel that has events.
 *	We have to use callback functions because the two channels can be
 *	in different modes.
 *
 *	Locking is done for the handlers. Note that locking is done
 *	at the chip level (the 5uS delay issue is per chip not per
 *	channel). c->lock for both channels points to dev->lock
 */

irqreturn_t z8530_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct z8530_dev *dev=dev_id;
	u8 intr;
	static volatile int locker=0;
	int work=0;
	struct z8530_irqhandler *irqs;
	
	if(locker)
	{
		printk(KERN_ERR "IRQ re-enter\n");
		return IRQ_NONE;
	}
	locker=1;

	spin_lock(&dev->lock);

	while(++work<5000)
	{

		intr = read_zsreg(&dev->chanA, R3);
		if(!(intr & (CHARxIP|CHATxIP|CHAEXT|CHBRxIP|CHBTxIP|CHBEXT)))
			break;
	
		/* This holds the IRQ status. On the 8530 you must read it from chan 
		   A even though it applies to the whole chip */
		
		/* Now walk the chip and see what it is wanting - it may be
		   an IRQ for someone else remember */
		   
		irqs=dev->chanA.irqs;

		if(intr & (CHARxIP|CHATxIP|CHAEXT))
		{
			if(intr&CHARxIP)
				irqs->rx(&dev->chanA);
			if(intr&CHATxIP)
				irqs->tx(&dev->chanA);
			if(intr&CHAEXT)
				irqs->status(&dev->chanA);
		}

		irqs=dev->chanB.irqs;

		if(intr & (CHBRxIP|CHBTxIP|CHBEXT))
		{
			if(intr&CHBRxIP)
				irqs->rx(&dev->chanB);
			if(intr&CHBTxIP)
				irqs->tx(&dev->chanB);
			if(intr&CHBEXT)
				irqs->status(&dev->chanB);
		}
	}
	spin_unlock(&dev->lock);
	if(work==5000)
		printk(KERN_ERR "%s: interrupt jammed - abort(0x%X)!\n", dev->name, intr);
	/* Ok all done */
	locker=0;
	return IRQ_HANDLED;
}

EXPORT_SYMBOL(z8530_interrupt);

static char reg_init[16]=
{
	0,0,0,0,
	0,0,0,0,
	0,0,0,0,
	0x55,0,0,0
};


/**
 *	z8530_sync_open - Open a Z8530 channel for PIO
 *	@dev:	The network interface we are using
 *	@c:	The Z8530 channel to open in synchronous PIO mode
 *
 *	Switch a Z8530 into synchronous mode without DMA assist. We
 *	raise the RTS/DTR and commence network operation.
 */
 
int z8530_sync_open(struct net_device *dev, struct z8530_channel *c)
{
	unsigned long flags;

	spin_lock_irqsave(c->lock, flags);

	c->sync = 1;
	c->mtu = dev->mtu+64;
	c->count = 0;
	c->skb = NULL;
	c->skb2 = NULL;
	c->irqs = &z8530_sync;

	/* This loads the double buffer up */
	z8530_rx_done(c);	/* Load the frame ring */
	z8530_rx_done(c);	/* Load the backup frame */
	z8530_rtsdtr(c,1);
	c->dma_tx = 0;
	c->regs[R1]|=TxINT_ENAB;
	write_zsreg(c, R1, c->regs[R1]);
	write_zsreg(c, R3, c->regs[R3]|RxENABLE);

	spin_unlock_irqrestore(c->lock, flags);
	return 0;
}


EXPORT_SYMBOL(z8530_sync_open);

/**
 *	z8530_sync_close - Close a PIO Z8530 channel
 *	@dev: Network device to close
 *	@c: Z8530 channel to disassociate and move to idle
 *
 *	Close down a Z8530 interface and switch its interrupt handlers
 *	to discard future events.
 */
 
int z8530_sync_close(struct net_device *dev, struct z8530_channel *c)
{
	u8 chk;
	unsigned long flags;
	
	spin_lock_irqsave(c->lock, flags);
	c->irqs = &z8530_nop;
	c->max = 0;
	c->sync = 0;
	
	chk=read_zsreg(c,R0);
	write_zsreg(c, R3, c->regs[R3]);
	z8530_rtsdtr(c,0);

	spin_unlock_irqrestore(c->lock, flags);
	return 0;
}

EXPORT_SYMBOL(z8530_sync_close);

/**
 *	z8530_sync_dma_open - Open a Z8530 for DMA I/O
 *	@dev: The network device to attach
 *	@c: The Z8530 channel to configure in sync DMA mode.
 *
 *	Set up a Z85x30 device for synchronous DMA in both directions. Two
 *	ISA DMA channels must be available for this to work. We assume ISA
 *	DMA driven I/O and PC limits on access.
 */
 
int z8530_sync_dma_open(struct net_device *dev, struct z8530_channel *c)
{
	unsigned long cflags, dflags;
	
	c->sync = 1;
	c->mtu = dev->mtu+64;
	c->count = 0;
	c->skb = NULL;
	c->skb2 = NULL;
	/*
	 *	Load the DMA interfaces up
	 */
	c->rxdma_on = 0;
	c->txdma_on = 0;
	
	/*
	 *	Allocate the DMA flip buffers. Limit by page size.
	 *	Everyone runs 1500 mtu or less on wan links so this
	 *	should be fine.
	 */
	 
	if(c->mtu  > PAGE_SIZE/2)
		return -EMSGSIZE;
	 
	c->rx_buf[0]=(void *)get_zeroed_page(GFP_KERNEL|GFP_DMA);
	if(c->rx_buf[0]==NULL)
		return -ENOBUFS;
	c->rx_buf[1]=c->rx_buf[0]+PAGE_SIZE/2;
	
	c->tx_dma_buf[0]=(void *)get_zeroed_page(GFP_KERNEL|GFP_DMA);
	if(c->tx_dma_buf[0]==NULL)
	{
		free_page((unsigned long)c->rx_buf[0]);
		c->rx_buf[0]=NULL;
		return -ENOBUFS;
	}
	c->tx_dma_buf[1]=c->tx_dma_buf[0]+PAGE_SIZE/2;

	c->tx_dma_used=0;
	c->dma_tx = 1;
	c->dma_num=0;
	c->dma_ready=1;
	
	/*
	 *	Enable DMA control mode
	 */

	spin_lock_irqsave(c->lock, cflags);
	 
	/*
	 *	TX DMA via DIR/REQ
	 */
	 
	c->regs[R14]|= DTRREQ;
	write_zsreg(c, R14, c->regs[R14]);     

	c->regs[R1]&= ~TxINT_ENAB;
	write_zsreg(c, R1, c->regs[R1]);
	
	/*
	 *	RX DMA via W/Req
	 */	 

	c->regs[R1]|= WT_FN_RDYFN;
	c->regs[R1]|= WT_RDY_RT;
	c->regs[R1]|= INT_ERR_Rx;
	c->regs[R1]&= ~TxINT_ENAB;
	write_zsreg(c, R1, c->regs[R1]);
	c->regs[R1]|= WT_RDY_ENAB;
	write_zsreg(c, R1, c->regs[R1]);            
	
	/*
	 *	DMA interrupts
	 */
	 
	/*
	 *	Set up the DMA configuration
	 */	
	 
	dflags=claim_dma_lock();
	 
	disable_dma(c->rxdma);
	clear_dma_ff(c->rxdma);
	set_dma_mode(c->rxdma, DMA_MODE_READ|0x10);
	set_dma_addr(c->rxdma, virt_to_bus(c->rx_buf[0]));
	set_dma_count(c->rxdma, c->mtu);
	enable_dma(c->rxdma);

	disable_dma(c->txdma);
	clear_dma_ff(c->txdma);
	set_dma_mode(c->txdma, DMA_MODE_WRITE);
	disable_dma(c->txdma);
	
	release_dma_lock(dflags);
	
	/*
	 *	Select the DMA interrupt handlers
	 */

	c->rxdma_on = 1;
	c->txdma_on = 1;
	c->tx_dma_used = 1;
	 
	c->irqs = &z8530_dma_sync;
	z8530_rtsdtr(c,1);
	write_zsreg(c, R3, c->regs[R3]|RxENABLE);

	spin_unlock_irqrestore(c->lock, cflags);
	
	return 0;
}

EXPORT_SYMBOL(z8530_sync_dma_open);

/**
 *	z8530_sync_dma_close - Close down DMA I/O
 *	@dev: Network device to detach
 *	@c: Z8530 channel to move into discard mode
 *
 *	Shut down a DMA mode synchronous interface. Halt the DMA, and
 *	free the buffers.
 */
 
int z8530_sync_dma_close(struct net_device *dev, struct z8530_channel *c)
{
	u8 chk;
	unsigned long flags;
	
	c->irqs = &z8530_nop;
	c->max = 0;
	c->sync = 0;
	
	/*
	 *	Disable the PC DMA channels
	 */
	
	flags=claim_dma_lock(); 
	disable_dma(c->rxdma);
	clear_dma_ff(c->rxdma);
	
	c->rxdma_on = 0;
	
	disable_dma(c->txdma);
	clear_dma_ff(c->txdma);
	release_dma_lock(flags);
	
	c->txdma_on = 0;
	c->tx_dma_used = 0;

	spin_lock_irqsave(c->lock, flags);

	/*
	 *	Disable DMA control mode
	 */
	 
	c->regs[R1]&= ~WT_RDY_ENAB;
	write_zsreg(c, R1, c->regs[R1]);            
	c->regs[R1]&= ~(WT_RDY_RT|WT_FN_RDYFN|INT_ERR_Rx);
	c->regs[R1]|= INT_ALL_Rx;
	write_zsreg(c, R1, c->regs[R1]);
	c->regs[R14]&= ~DTRREQ;
	write_zsreg(c, R14, c->regs[R14]);   
	
	if(c->rx_buf[0])
	{
		free_page((unsigned long)c->rx_buf[0]);
		c->rx_buf[0]=NULL;
	}
	if(c->tx_dma_buf[0])
	{
		free_page((unsigned  long)c->tx_dma_buf[0]);
		c->tx_dma_buf[0]=NULL;
	}
	chk=read_zsreg(c,R0);
	write_zsreg(c, R3, c->regs[R3]);
	z8530_rtsdtr(c,0);

	spin_unlock_irqrestore(c->lock, flags);

	return 0;
}

EXPORT_SYMBOL(z8530_sync_dma_close);

/**
 *	z8530_sync_txdma_open - Open a Z8530 for TX driven DMA
 *	@dev: The network device to attach
 *	@c: The Z8530 channel to configure in sync DMA mode.
 *
 *	Set up a Z85x30 device for synchronous DMA tranmission. One
 *	ISA DMA channel must be available for this to work. The receive
 *	side is run in PIO mode, but then it has the bigger FIFO.
 */

int z8530_sync_txdma_open(struct net_device *dev, struct z8530_channel *c)
{
	unsigned long cflags, dflags;

	printk("Opening sync interface for TX-DMA\n");
	c->sync = 1;
	c->mtu = dev->mtu+64;
	c->count = 0;
	c->skb = NULL;
	c->skb2 = NULL;
	
	/*
	 *	Allocate the DMA flip buffers. Limit by page size.
	 *	Everyone runs 1500 mtu or less on wan links so this
	 *	should be fine.
	 */
	 
	if(c->mtu  > PAGE_SIZE/2)
		return -EMSGSIZE;
	 
	c->tx_dma_buf[0]=(void *)get_zeroed_page(GFP_KERNEL|GFP_DMA);
	if(c->tx_dma_buf[0]==NULL)
		return -ENOBUFS;

	c->tx_dma_buf[1] = c->tx_dma_buf[0] + PAGE_SIZE/2;


	spin_lock_irqsave(c->lock, cflags);

	/*
	 *	Load the PIO receive ring
	 */

	z8530_rx_done(c);
	z8530_rx_done(c);

 	/*
	 *	Load the DMA interfaces up
	 */

	c->rxdma_on = 0;
	c->txdma_on = 0;
	
	c->tx_dma_used=0;
	c->dma_num=0;
	c->dma_ready=1;
	c->dma_tx = 1;

 	/*
	 *	Enable DMA control mode
	 */

 	/*
	 *	TX DMA via DIR/REQ
 	 */
	c->regs[R14]|= DTRREQ;
	write_zsreg(c, R14, c->regs[R14]);     
	
	c->regs[R1]&= ~TxINT_ENAB;
	write_zsreg(c, R1, c->regs[R1]);
	
	/*
	 *	Set up the DMA configuration
	 */	
	 
	dflags = claim_dma_lock();

	disable_dma(c->txdma);
	clear_dma_ff(c->txdma);
	set_dma_mode(c->txdma, DMA_MODE_WRITE);
	disable_dma(c->txdma);

	release_dma_lock(dflags);
	
	/*
	 *	Select the DMA interrupt handlers
	 */

	c->rxdma_on = 0;
	c->txdma_on = 1;
	c->tx_dma_used = 1;
	 
	c->irqs = &z8530_txdma_sync;
	z8530_rtsdtr(c,1);
	write_zsreg(c, R3, c->regs[R3]|RxENABLE);
	spin_unlock_irqrestore(c->lock, cflags);
	
	return 0;
}

EXPORT_SYMBOL(z8530_sync_txdma_open);

/**
 *	z8530_sync_txdma_close - Close down a TX driven DMA channel
 *	@dev: Network device to detach
 *	@c: Z8530 channel to move into discard mode
 *
 *	Shut down a DMA/PIO split mode synchronous interface. Halt the DMA, 
 *	and  free the buffers.
 */

int z8530_sync_txdma_close(struct net_device *dev, struct z8530_channel *c)
{
	unsigned long dflags, cflags;
	u8 chk;

	
	spin_lock_irqsave(c->lock, cflags);
	
	c->irqs = &z8530_nop;
	c->max = 0;
	c->sync = 0;
	
	/*
	 *	Disable the PC DMA channels
	 */
	 
	dflags = claim_dma_lock();

	disable_dma(c->txdma);
	clear_dma_ff(c->txdma);
	c->txdma_on = 0;
	c->tx_dma_used = 0;

	release_dma_lock(dflags);

	/*
	 *	Disable DMA control mode
	 */
	 
	c->regs[R1]&= ~WT_RDY_ENAB;
	write_zsreg(c, R1, c->regs[R1]);            
	c->regs[R1]&= ~(WT_RDY_RT|WT_FN_RDYFN|INT_ERR_Rx);
	c->regs[R1]|= INT_ALL_Rx;
	write_zsreg(c, R1, c->regs[R1]);
	c->regs[R14]&= ~DTRREQ;
	write_zsreg(c, R14, c->regs[R14]);   
	
	if(c->tx_dma_buf[0])
	{
		free_page((unsigned long)c->tx_dma_buf[0]);
		c->tx_dma_buf[0]=NULL;
	}
	chk=read_zsreg(c,R0);
	write_zsreg(c, R3, c->regs[R3]);
	z8530_rtsdtr(c,0);

	spin_unlock_irqrestore(c->lock, cflags);
	return 0;
}


EXPORT_SYMBOL(z8530_sync_txdma_close);


/*
 *	Name strings for Z8530 chips. SGI claim to have a 130, Zilog deny
 *	it exists...
 */
 
static char *z8530_type_name[]={
	"Z8530",
	"Z85C30",
	"Z85230"
};

/**
 *	z8530_describe - Uniformly describe a Z8530 port
 *	@dev: Z8530 device to describe
 *	@mapping: string holding mapping type (eg "I/O" or "Mem")
 *	@io: the port value in question
 *
 *	Describe a Z8530 in a standard format. We must pass the I/O as
 *	the port offset isnt predictable. The main reason for this function
 *	is to try and get a common format of report.
 */

void z8530_describe(struct z8530_dev *dev, char *mapping, unsigned long io)
{
	printk(KERN_INFO "%s: %s found at %s 0x%lX, IRQ %d.\n",
		dev->name, 
		z8530_type_name[dev->type],
		mapping,
		Z8530_PORT_OF(io),
		dev->irq);
}

EXPORT_SYMBOL(z8530_describe);

/*
 *	Locked operation part of the z8530 init code
 */
 
static inline int do_z8530_init(struct z8530_dev *dev)
{
	/* NOP the interrupt handlers first - we might get a
	   floating IRQ transition when we reset the chip */
	dev->chanA.irqs=&z8530_nop;
	dev->chanB.irqs=&z8530_nop;
	dev->chanA.dcdcheck=DCD;
	dev->chanB.dcdcheck=DCD;

	/* Reset the chip */
	write_zsreg(&dev->chanA, R9, 0xC0);
	udelay(200);
	/* Now check its valid */
	write_zsreg(&dev->chanA, R12, 0xAA);
	if(read_zsreg(&dev->chanA, R12)!=0xAA)
		return -ENODEV;
	write_zsreg(&dev->chanA, R12, 0x55);
	if(read_zsreg(&dev->chanA, R12)!=0x55)
		return -ENODEV;
		
	dev->type=Z8530;
	
	/*
	 *	See the application note.
	 */
	 
	write_zsreg(&dev->chanA, R15, 0x01);
	
	/*
	 *	If we can set the low bit of R15 then
	 *	the chip is enhanced.
	 */
	 
	if(read_zsreg(&dev->chanA, R15)==0x01)
	{
		/* This C30 versus 230 detect is from Klaus Kudielka's dmascc */
		/* Put a char in the fifo */
		write_zsreg(&dev->chanA, R8, 0);
		if(read_zsreg(&dev->chanA, R0)&Tx_BUF_EMP)
			dev->type = Z85230;	/* Has a FIFO */
		else
			dev->type = Z85C30;	/* Z85C30, 1 byte FIFO */
	}
		
	/*
	 *	The code assumes R7' and friends are
	 *	off. Use write_zsext() for these and keep
	 *	this bit clear.
	 */
	 
	write_zsreg(&dev->chanA, R15, 0);
		
	/*
	 *	At this point it looks like the chip is behaving
	 */
	 
	memcpy(dev->chanA.regs, reg_init, 16);
	memcpy(dev->chanB.regs, reg_init ,16);
	
	return 0;
}

/**
 *	z8530_init - Initialise a Z8530 device
 *	@dev: Z8530 device to initialise.
 *
 *	Configure up a Z8530/Z85C30 or Z85230 chip. We check the device
 *	is present, identify the type and then program it to hopefully
 *	keep quite and behave. This matters a lot, a Z8530 in the wrong
 *	state will sometimes get into stupid modes generating 10Khz
 *	interrupt streams and the like.
 *
 *	We set the interrupt handler up to discard any events, in case
 *	we get them during reset or setp.
 *
 *	Return 0 for success, or a negative value indicating the problem
 *	in errno form.
 */

int z8530_init(struct z8530_dev *dev)
{
	unsigned long flags;
	int ret;

	/* Set up the chip level lock */
	spin_lock_init(&dev->lock);
	dev->chanA.lock = &dev->lock;
	dev->chanB.lock = &dev->lock;

	spin_lock_irqsave(&dev->lock, flags);
	ret = do_z8530_init(dev);
	spin_unlock_irqrestore(&dev->lock, flags);

	return ret;
}


EXPORT_SYMBOL(z8530_init);

/**
 *	z8530_shutdown - Shutdown a Z8530 device
 *	@dev: The Z8530 chip to shutdown
 *
 *	We set the interrupt handlers to silence any interrupts. We then 
 *	reset the chip and wait 100uS to be sure the reset completed. Just
 *	in case the caller then tries to do stuff.
 *
 *	This is called without the lock held
 */
 
int z8530_shutdown(struct z8530_dev *dev)
{
	unsigned long flags;
	/* Reset the chip */

	spin_lock_irqsave(&dev->lock, flags);
	dev->chanA.irqs=&z8530_nop;
	dev->chanB.irqs=&z8530_nop;
	write_zsreg(&dev->chanA, R9, 0xC0);
	/* We must lock the udelay, the chip is offlimits here */
	udelay(100);
	spin_unlock_irqrestore(&dev->lock, flags);
	return 0;
}

EXPORT_SYMBOL(z8530_shutdown);

/**
 *	z8530_channel_load - Load channel data
 *	@c: Z8530 channel to configure
 *	@rtable: table of register, value pairs
 *	FIXME: ioctl to allow user uploaded tables
 *
 *	Load a Z8530 channel up from the system data. We use +16 to 
 *	indicate the "prime" registers. The value 255 terminates the
 *	table.
 */

int z8530_channel_load(struct z8530_channel *c, u8 *rtable)
{
	unsigned long flags;

	spin_lock_irqsave(c->lock, flags);

	while(*rtable!=255)
	{
		int reg=*rtable++;
		if(reg>0x0F)
			write_zsreg(c, R15, c->regs[15]|1);
		write_zsreg(c, reg&0x0F, *rtable);
		if(reg>0x0F)
			write_zsreg(c, R15, c->regs[15]&~1);
		c->regs[reg]=*rtable++;
	}
	c->rx_function=z8530_null_rx;
	c->skb=NULL;
	c->tx_skb=NULL;
	c->tx_next_skb=NULL;
	c->mtu=1500;
	c->max=0;
	c->count=0;
	c->status=read_zsreg(c, R0);
	c->sync=1;
	write_zsreg(c, R3, c->regs[R3]|RxENABLE);

	spin_unlock_irqrestore(c->lock, flags);
	return 0;
}

EXPORT_SYMBOL(z8530_channel_load);


/**
 *	z8530_tx_begin - Begin packet transmission
 *	@c: The Z8530 channel to kick
 *
 *	This is the speed sensitive side of transmission. If we are called
 *	and no buffer is being transmitted we commence the next buffer. If
 *	nothing is queued we idle the sync. 
 *
 *	Note: We are handling this code path in the interrupt path, keep it
 *	fast or bad things will happen.
 *
 *	Called with the lock held.
 */

static void z8530_tx_begin(struct z8530_channel *c)
{
	unsigned long flags;
	if(c->tx_skb)
		return;
		
	c->tx_skb=c->tx_next_skb;
	c->tx_next_skb=NULL;
	c->tx_ptr=c->tx_next_ptr;
	
	if(c->tx_skb==NULL)
	{
		/* Idle on */
		if(c->dma_tx)
		{
			flags=claim_dma_lock();
			disable_dma(c->txdma);
			/*
			 *	Check if we crapped out.
			 */
			if(get_dma_residue(c->txdma))
			{
				c->stats.tx_dropped++;
				c->stats.tx_fifo_errors++;
			}
			release_dma_lock(flags);
		}
		c->txcount=0;
	}
	else
	{
		c->txcount=c->tx_skb->len;
		
		
		if(c->dma_tx)
		{
			/*
			 *	FIXME. DMA is broken for the original 8530,
			 *	on the older parts we need to set a flag and
			 *	wait for a further TX interrupt to fire this
			 *	stage off	
			 */
			 
			flags=claim_dma_lock();
			disable_dma(c->txdma);

			/*
			 *	These two are needed by the 8530/85C30
			 *	and must be issued when idling.
			 */
			 
			if(c->dev->type!=Z85230)
			{
				write_zsctrl(c, RES_Tx_CRC);
				write_zsctrl(c, RES_EOM_L);
			}	
			write_zsreg(c, R10, c->regs[10]&~ABUNDER);
			clear_dma_ff(c->txdma);
			set_dma_addr(c->txdma, virt_to_bus(c->tx_ptr));
			set_dma_count(c->txdma, c->txcount);
			enable_dma(c->txdma);
			release_dma_lock(flags);
			write_zsctrl(c, RES_EOM_L);
			write_zsreg(c, R5, c->regs[R5]|TxENAB);
		}
		else
		{

			/* ABUNDER off */
			write_zsreg(c, R10, c->regs[10]);
			write_zsctrl(c, RES_Tx_CRC);
	
			while(c->txcount && (read_zsreg(c,R0)&Tx_BUF_EMP))
			{		
				write_zsreg(c, R8, *c->tx_ptr++);
				c->txcount--;
			}

		}
	}
	/*
	 *	Since we emptied tx_skb we can ask for more
	 */
	netif_wake_queue(c->netdevice);
}

/**
 *	z8530_tx_done - TX complete callback
 *	@c: The channel that completed a transmit.
 *
 *	This is called when we complete a packet send. We wake the queue,
 *	start the next packet going and then free the buffer of the existing
 *	packet. This code is fairly timing sensitive.
 *
 *	Called with the register lock held.
 */ 
 
static void z8530_tx_done(struct z8530_channel *c)
{
	struct sk_buff *skb;

	/* Actually this can happen.*/
	if(c->tx_skb==NULL)
		return;

	skb=c->tx_skb;
	c->tx_skb=NULL;
	z8530_tx_begin(c);
	c->stats.tx_packets++;
	c->stats.tx_bytes+=skb->len;
	dev_kfree_skb_irq(skb);
}

/**
 *	z8530_null_rx - Discard a packet
 *	@c: The channel the packet arrived on
 *	@skb: The buffer
 *
 *	We point the receive handler at this function when idle. Instead
 *	of syncppp processing the frames we get to throw them away.
 */
 
void z8530_null_rx(struct z8530_channel *c, struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
}

EXPORT_SYMBOL(z8530_null_rx);

/**
 *	z8530_rx_done - Receive completion callback
 *	@c: The channel that completed a receive
 *
 *	A new packet is complete. Our goal here is to get back into receive
 *	mode as fast as possible. On the Z85230 we could change to using
 *	ESCC mode, but on the older chips we have no choice. We flip to the
 *	new buffer immediately in DMA mode so that the DMA of the next
 *	frame can occur while we are copying the previous buffer to an sk_buff
 *
 *	Called with the lock held
 */
 
static void z8530_rx_done(struct z8530_channel *c)
{
	struct sk_buff *skb;
	int ct;
	
	/*
	 *	Is our receive engine in DMA mode
	 */
	 
	if(c->rxdma_on)
	{
		/*
		 *	Save the ready state and the buffer currently
		 *	being used as the DMA target
		 */
		 
		int ready=c->dma_ready;
		unsigned char *rxb=c->rx_buf[c->dma_num];
		unsigned long flags;
		
		/*
		 *	Complete this DMA. Neccessary to find the length
		 */		
		 
		flags=claim_dma_lock();
		
		disable_dma(c->rxdma);
		clear_dma_ff(c->rxdma);
		c->rxdma_on=0;
		ct=c->mtu-get_dma_residue(c->rxdma);
		if(ct<0)
			ct=2;	/* Shit happens.. */
		c->dma_ready=0;
		
		/*
		 *	Normal case: the other slot is free, start the next DMA
		 *	into it immediately.
		 */
		 
		if(ready)
		{
			c->dma_num^=1;
			set_dma_mode(c->rxdma, DMA_MODE_READ|0x10);
			set_dma_addr(c->rxdma, virt_to_bus(c->rx_buf[c->dma_num]));
			set_dma_count(c->rxdma, c->mtu);
			c->rxdma_on = 1;
			enable_dma(c->rxdma);
			/* Stop any frames that we missed the head of 
			   from passing */
			write_zsreg(c, R0, RES_Rx_CRC);
		}
		else
			/* Can't occur as we dont reenable the DMA irq until
			   after the flip is done */
			printk(KERN_WARNING "%s: DMA flip overrun!\n", c->netdevice->name);
			
		release_dma_lock(flags);
		
		/*
		 *	Shove the old buffer into an sk_buff. We can't DMA
		 *	directly into one on a PC - it might be above the 16Mb
		 *	boundary. Optimisation - we could check to see if we
		 *	can avoid the copy. Optimisation 2 - make the memcpy
		 *	a copychecksum.
		 */
		 
		skb=dev_alloc_skb(ct);
		if(skb==NULL)
		{
			c->stats.rx_dropped++;
			printk(KERN_WARNING "%s: Memory squeeze.\n", c->netdevice->name);
		}
		else
		{
			skb_put(skb, ct);
			memcpy(skb->data, rxb, ct);
			c->stats.rx_packets++;
			c->stats.rx_bytes+=ct;
		}
		c->dma_ready=1;
	}
	else
	{
		RT_LOCK;	
		skb=c->skb;
		
		/*
		 *	The game we play for non DMA is similar. We want to
		 *	get the controller set up for the next packet as fast
		 *	as possible. We potentially only have one byte + the
		 *	fifo length for this. Thus we want to flip to the new
		 *	buffer and then mess around copying and allocating
		 *	things. For the current case it doesn't matter but
		 *	if you build a system where the sync irq isnt blocked
		 *	by the kernel IRQ disable then you need only block the
		 *	sync IRQ for the RT_LOCK area.
		 *	
		 */
		ct=c->count;
		
		c->skb = c->skb2;
		c->count = 0;
		c->max = c->mtu;
		if(c->skb)
		{
			c->dptr = c->skb->data;
			c->max = c->mtu;
		}
		else
		{
			c->count= 0;
			c->max = 0;
		}
		RT_UNLOCK;

		c->skb2 = dev_alloc_skb(c->mtu);
		if(c->skb2==NULL)
			printk(KERN_WARNING "%s: memory squeeze.\n",
				c->netdevice->name);
		else
		{
			skb_put(c->skb2,c->mtu);
		}
		c->stats.rx_packets++;
		c->stats.rx_bytes+=ct;
		
	}
	/*
	 *	If we received a frame we must now process it.
	 */
	if(skb)
	{
		skb_trim(skb, ct);
		c->rx_function(c,skb);
	}
	else
	{
		c->stats.rx_dropped++;
		printk(KERN_ERR "%s: Lost a frame\n", c->netdevice->name);
	}
}

/**
 *	spans_boundary - Check a packet can be ISA DMA'd
 *	@skb: The buffer to check
 *
 *	Returns true if the buffer cross a DMA boundary on a PC. The poor
 *	thing can only DMA within a 64K block not across the edges of it.
 */
 
static inline int spans_boundary(struct sk_buff *skb)
{
	unsigned long a=(unsigned long)skb->data;
	a^=(a+skb->len);
	if(a&0x00010000)	/* If the 64K bit is different.. */
		return 1;
	return 0;
}

/**
 *	z8530_queue_xmit - Queue a packet
 *	@c: The channel to use
 *	@skb: The packet to kick down the channel
 *
 *	Queue a packet for transmission. Because we have rather
 *	hard to hit interrupt latencies for the Z85230 per packet 
 *	even in DMA mode we do the flip to DMA buffer if needed here
 *	not in the IRQ.
 *
 *	Called from the network code. The lock is not held at this 
 *	point.
 */

int z8530_queue_xmit(struct z8530_channel *c, struct sk_buff *skb)
{
	unsigned long flags;
	
	netif_stop_queue(c->netdevice);
	if(c->tx_next_skb)
	{
		return 1;
	}
	
	/* PC SPECIFIC - DMA limits */
	
	/*
	 *	If we will DMA the transmit and its gone over the ISA bus
	 *	limit, then copy to the flip buffer
	 */
	 
	if(c->dma_tx && ((unsigned long)(virt_to_bus(skb->data+skb->len))>=16*1024*1024 || spans_boundary(skb)))
	{
		/* 
		 *	Send the flip buffer, and flip the flippy bit.
		 *	We don't care which is used when just so long as
		 *	we never use the same buffer twice in a row. Since
		 *	only one buffer can be going out at a time the other
		 *	has to be safe.
		 */
		c->tx_next_ptr=c->tx_dma_buf[c->tx_dma_used];
		c->tx_dma_used^=1;	/* Flip temp buffer */
		memcpy(c->tx_next_ptr, skb->data, skb->len);
	}
	else
		c->tx_next_ptr=skb->data;	
	RT_LOCK;
	c->tx_next_skb=skb;
	RT_UNLOCK;
	
	spin_lock_irqsave(c->lock, flags);
	z8530_tx_begin(c);
	spin_unlock_irqrestore(c->lock, flags);
	
	return 0;
}

EXPORT_SYMBOL(z8530_queue_xmit);

/**
 *	z8530_get_stats - Get network statistics
 *	@c: The channel to use
 *
 *	Get the statistics block. We keep the statistics in software as
 *	the chip doesn't do it for us.
 *
 *	Locking is ignored here - we could lock for a copy but its
 *	not likely to be that big an issue
 */
 
struct net_device_stats *z8530_get_stats(struct z8530_channel *c)
{
	return &c->stats;
}

EXPORT_SYMBOL(z8530_get_stats);

/*
 *	Module support
 */
static char banner[] __initdata = KERN_INFO "Generic Z85C30/Z85230 interface driver v0.02\n";

static int __init z85230_init_driver(void)
{
	printk(banner);
	return 0;
}
module_init(z85230_init_driver);

static void __exit z85230_cleanup_driver(void)
{
}
module_exit(z85230_cleanup_driver);

MODULE_AUTHOR("Red Hat Inc.");
MODULE_DESCRIPTION("Z85x30 synchronous driver core");
MODULE_LICENSE("GPL");
