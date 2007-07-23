/*********************************************************************
 * 
 * Filename:	  irport.c
 * Version:	  1.0
 * Description:   Half duplex serial port SIR driver for IrDA. 
 * Status:	  Experimental.
 * Author:	  Dag Brattli <dagb@cs.uit.no>
 * Created at:	  Sun Aug  3 13:49:59 1997
 * Modified at:   Fri Jan 28 20:22:38 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:	  serial.c by Linus Torvalds 
 * 
 *     Copyright (c) 1997, 1998, 1999-2000 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2003 Jean Tourrilhes, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *
 *     This driver is ment to be a small half duplex serial driver to be
 *     used for IR-chipsets that has a UART (16550) compatibility mode. 
 *     Eventually it will replace irtty, because of irtty has some 
 *     problems that is hard to get around when we don't have control
 *     over the serial driver. This driver may also be used by FIR 
 *     drivers to handle SIR mode for them.
 *
 ********************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/serial_reg.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/rtnetlink.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>

#include <net/irda/irda.h>
#include <net/irda/wrapper.h>
#include "irport.h"

#define IO_EXTENT 8

/* 
 * Currently you'll need to set these values using insmod like this:
 * insmod irport io=0x3e8 irq=11
 */
static unsigned int io[]  = { ~0, ~0, ~0, ~0 };
static unsigned int irq[] = { 0, 0, 0, 0 };

static unsigned int qos_mtt_bits = 0x03;

static struct irport_cb *dev_self[] = { NULL, NULL, NULL, NULL};
static char *driver_name = "irport";

static inline void irport_write_wakeup(struct irport_cb *self);
static inline int  irport_write(int iobase, int fifo_size, __u8 *buf, int len);
static inline void irport_receive(struct irport_cb *self);

static int  irport_net_ioctl(struct net_device *dev, struct ifreq *rq, 
			     int cmd);
static inline int  irport_is_receiving(struct irport_cb *self);
static int  irport_set_dtr_rts(struct net_device *dev, int dtr, int rts);
static int  irport_raw_write(struct net_device *dev, __u8 *buf, int len);
static struct net_device_stats *irport_net_get_stats(struct net_device *dev);
static int irport_change_speed_complete(struct irda_task *task);
static void irport_timeout(struct net_device *dev);

static irqreturn_t irport_interrupt(int irq, void *dev_id);
static int irport_hard_xmit(struct sk_buff *skb, struct net_device *dev);
static void irport_change_speed(void *priv, __u32 speed);
static int irport_net_open(struct net_device *dev);
static int irport_net_close(struct net_device *dev);

static struct irport_cb *
irport_open(int i, unsigned int iobase, unsigned int irq)
{
	struct net_device *dev;
	struct irport_cb *self;

	IRDA_DEBUG(1, "%s()\n", __FUNCTION__);

	/* Lock the port that we need */
	if (!request_region(iobase, IO_EXTENT, driver_name)) {
		IRDA_DEBUG(0, "%s(), can't get iobase of 0x%03x\n",
			   __FUNCTION__, iobase);
		goto err_out1;
	}

	/*
	 *  Allocate new instance of the driver
	 */
	dev = alloc_irdadev(sizeof(struct irport_cb));
	if (!dev) {
		IRDA_ERROR("%s(), can't allocate memory for "
			   "irda device!\n", __FUNCTION__);
		goto err_out2;
	}

	self = dev->priv;
	spin_lock_init(&self->lock);

	/* Need to store self somewhere */
	dev_self[i] = self;
	self->priv = self;
	self->index = i;

	/* Initialize IO */
	self->io.sir_base  = iobase;
        self->io.sir_ext   = IO_EXTENT;
        self->io.irq       = irq;
        self->io.fifo_size = 16;		/* 16550A and compatible */

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);
	
	self->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200;

	self->qos.min_turn_time.bits = qos_mtt_bits;
	irda_qos_bits_to_value(&self->qos);
	
	/* Bootstrap ZeroCopy Rx */
	self->rx_buff.truesize = IRDA_SKB_MAX_MTU;
	self->rx_buff.skb = __dev_alloc_skb(self->rx_buff.truesize,
					    GFP_KERNEL);
	if (self->rx_buff.skb == NULL) {
		IRDA_ERROR("%s(), can't allocate memory for "
			   "receive buffer!\n", __FUNCTION__);
		goto err_out3;
	}
	skb_reserve(self->rx_buff.skb, 1);
	self->rx_buff.head = self->rx_buff.skb->data;
	/* No need to memset the buffer, unless you are really pedantic */

	/* Finish setup the Rx buffer descriptor */
	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;
	self->rx_buff.data = self->rx_buff.head;

	/* Specify how much memory we want */
	self->tx_buff.truesize = 4000;
	
	/* Allocate memory if needed */
	if (self->tx_buff.truesize > 0) {
		self->tx_buff.head = kzalloc(self->tx_buff.truesize,
						      GFP_KERNEL);
		if (self->tx_buff.head == NULL) {
			IRDA_ERROR("%s(), can't allocate memory for "
				   "transmit buffer!\n", __FUNCTION__);
			goto err_out4;
		}
	}	
	self->tx_buff.data = self->tx_buff.head;

	self->netdev = dev;
	/* Keep track of module usage */
	SET_MODULE_OWNER(dev);

	/* May be overridden by piggyback drivers */
	self->interrupt    = irport_interrupt;
	self->change_speed = irport_change_speed;

	/* Override the network functions we need to use */
	dev->hard_start_xmit = irport_hard_xmit;
	dev->tx_timeout	     = irport_timeout;
	dev->watchdog_timeo  = HZ;  /* Allow time enough for speed change */
	dev->open            = irport_net_open;
	dev->stop            = irport_net_close;
	dev->get_stats	     = irport_net_get_stats;
	dev->do_ioctl        = irport_net_ioctl;

	/* Make ifconfig display some details */
	dev->base_addr = iobase;
	dev->irq = irq;

	if (register_netdev(dev)) {
		IRDA_ERROR("%s(), register_netdev() failed!\n", __FUNCTION__);
		goto err_out5;
	}
	IRDA_MESSAGE("IrDA: Registered device %s (irport io=0x%X irq=%d)\n",
		dev->name, iobase, irq);

	return self;
 err_out5:
	kfree(self->tx_buff.head);
 err_out4:
	kfree_skb(self->rx_buff.skb);
 err_out3:
	free_netdev(dev);
	dev_self[i] = NULL;
 err_out2:
	release_region(iobase, IO_EXTENT);
 err_out1:
	return NULL;
}

static int irport_close(struct irport_cb *self)
{
	IRDA_ASSERT(self != NULL, return -1;);

	/* We are not using any dongle anymore! */
	if (self->dongle)
		irda_device_dongle_cleanup(self->dongle);
	self->dongle = NULL;
	
	/* Remove netdevice */
	unregister_netdev(self->netdev);

	/* Release the IO-port that this driver is using */
	IRDA_DEBUG(0 , "%s(), Releasing Region %03x\n", 
		   __FUNCTION__, self->io.sir_base);
	release_region(self->io.sir_base, self->io.sir_ext);

	kfree(self->tx_buff.head);
	
	if (self->rx_buff.skb)
		kfree_skb(self->rx_buff.skb);
	self->rx_buff.skb = NULL;
	
	/* Remove ourselves */
	dev_self[self->index] = NULL;
	free_netdev(self->netdev);
	
	return 0;
}

static void irport_stop(struct irport_cb *self)
{
	int iobase;

	iobase = self->io.sir_base;

	/* We can't lock, we may be called from a FIR driver - Jean II */

	/* We are not transmitting any more */
	self->transmitting = 0;

	/* Reset UART */
	outb(0, iobase+UART_MCR);
	
	/* Turn off interrupts */
	outb(0, iobase+UART_IER);
}

static void irport_start(struct irport_cb *self)
{
	int iobase;

	iobase = self->io.sir_base;

	irport_stop(self);
	
	/* We can't lock, we may be called from a FIR driver - Jean II */

	/* Initialize UART */
	outb(UART_LCR_WLEN8, iobase+UART_LCR);  /* Reset DLAB */
	outb((UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2), iobase+UART_MCR);
	
	/* Turn on interrups */
	outb(UART_IER_RLSI | UART_IER_RDI |UART_IER_THRI, iobase+UART_IER);
}

/*
 * Function irport_get_fcr (speed)
 *
 *    Compute value of fcr
 *
 */
static inline unsigned int irport_get_fcr(__u32 speed)
{
	unsigned int fcr;    /* FIFO control reg */

	/* Enable fifos */
	fcr = UART_FCR_ENABLE_FIFO;

	/* 
	 * Use trigger level 1 to avoid 3 ms. timeout delay at 9600 bps, and
	 * almost 1,7 ms at 19200 bps. At speeds above that we can just forget
	 * about this timeout since it will always be fast enough. 
	 */
	if (speed < 38400)
		fcr |= UART_FCR_TRIGGER_1;
	else 
		//fcr |= UART_FCR_TRIGGER_14;
		fcr |= UART_FCR_TRIGGER_8;

	return(fcr);
}
 
/*
 * Function irport_change_speed (self, speed)
 *
 *    Set speed of IrDA port to specified baudrate
 *
 * This function should be called with irq off and spin-lock.
 */
static void irport_change_speed(void *priv, __u32 speed)
{
	struct irport_cb *self = (struct irport_cb *) priv;
	int iobase; 
	unsigned int fcr;    /* FIFO control reg */
	unsigned int lcr;    /* Line control reg */
	int divisor;

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(speed != 0, return;);

	IRDA_DEBUG(1, "%s(), Setting speed to: %d - iobase=%#x\n",
		    __FUNCTION__, speed, self->io.sir_base);

	/* We can't lock, we may be called from a FIR driver - Jean II */

	iobase = self->io.sir_base;
	
	/* Update accounting for new speed */
	self->io.speed = speed;

	/* Turn off interrupts */
	outb(0, iobase+UART_IER); 

	divisor = SPEED_MAX/speed;
	
	/* Get proper fifo configuration */
	fcr = irport_get_fcr(speed);

	/* IrDA ports use 8N1 */
	lcr = UART_LCR_WLEN8;
	
	outb(UART_LCR_DLAB | lcr, iobase+UART_LCR); /* Set DLAB */
	outb(divisor & 0xff,      iobase+UART_DLL); /* Set speed */
	outb(divisor >> 8,	  iobase+UART_DLM);
	outb(lcr,		  iobase+UART_LCR); /* Set 8N1	*/
	outb(fcr,		  iobase+UART_FCR); /* Enable FIFO's */

	/* Turn on interrups */
	/* This will generate a fatal interrupt storm.
	 * People calling us will do that properly - Jean II */
	//outb(/*UART_IER_RLSI|*/UART_IER_RDI/*|UART_IER_THRI*/, iobase+UART_IER);
}

/*
 * Function __irport_change_speed (instance, state, param)
 *
 *    State machine for changing speed of the device. We do it this way since
 *    we cannot use schedule_timeout() when we are in interrupt context
 *
 */
static int __irport_change_speed(struct irda_task *task)
{
	struct irport_cb *self;
	__u32 speed = (__u32) task->param;
	unsigned long flags = 0;
	int wasunlocked = 0;
	int ret = 0;

	IRDA_DEBUG(2, "%s(), <%ld>\n", __FUNCTION__, jiffies); 

	self = (struct irport_cb *) task->instance;

	IRDA_ASSERT(self != NULL, return -1;);

	/* Locking notes : this function may be called from irq context with
	 * spinlock, via irport_write_wakeup(), or from non-interrupt without
	 * spinlock (from the task timer). Yuck !
	 * This is ugly, and unsafe is the spinlock is not already acquired.
	 * This will be fixed when irda-task get rewritten.
	 * Jean II */
	if (!spin_is_locked(&self->lock)) {
		spin_lock_irqsave(&self->lock, flags);
		wasunlocked = 1;
	}

	switch (task->state) {
	case IRDA_TASK_INIT:
	case IRDA_TASK_WAIT:
		/* Are we ready to change speed yet? */
		if (self->tx_buff.len > 0) {
			task->state = IRDA_TASK_WAIT;

			/* Try again later */
			ret = msecs_to_jiffies(20);
			break;
		}

		if (self->dongle)
			irda_task_next_state(task, IRDA_TASK_CHILD_INIT);
		else
			irda_task_next_state(task, IRDA_TASK_CHILD_DONE);
		break;
	case IRDA_TASK_CHILD_INIT:
		/* Go to default speed */
		self->change_speed(self->priv, 9600);

		/* Change speed of dongle */
		if (irda_task_execute(self->dongle,
				      self->dongle->issue->change_speed, 
				      NULL, task, (void *) speed))
		{
			/* Dongle need more time to change its speed */
			irda_task_next_state(task, IRDA_TASK_CHILD_WAIT);

			/* Give dongle 1 sec to finish */
			ret = msecs_to_jiffies(1000);
		} else
			/* Child finished immediately */
			irda_task_next_state(task, IRDA_TASK_CHILD_DONE);
		break;
	case IRDA_TASK_CHILD_WAIT:
		IRDA_WARNING("%s(), changing speed of dongle timed out!\n", __FUNCTION__);
		ret = -1;		
		break;
	case IRDA_TASK_CHILD_DONE:
		/* Finally we are ready to change the speed */
		self->change_speed(self->priv, speed);
		
		irda_task_next_state(task, IRDA_TASK_DONE);
		break;
	default:
		IRDA_ERROR("%s(), unknown state %d\n",
			   __FUNCTION__, task->state);
		irda_task_next_state(task, IRDA_TASK_DONE);
		ret = -1;
		break;
	}
	/* Put stuff in the state we found them - Jean II */
	if(wasunlocked) {
		spin_unlock_irqrestore(&self->lock, flags);
	}

	return ret;
}

/*
 * Function irport_change_speed_complete (task)
 *
 *    Called when the change speed operation completes
 *
 */
static int irport_change_speed_complete(struct irda_task *task)
{
	struct irport_cb *self;

	IRDA_DEBUG(1, "%s()\n", __FUNCTION__);

	self = (struct irport_cb *) task->instance;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->netdev != NULL, return -1;);

	/* Finished changing speed, so we are not busy any longer */
	/* Signal network layer so it can try to send the frame */

	netif_wake_queue(self->netdev);
	
	return 0;
}

/*
 * Function irport_timeout (struct net_device *dev)
 *
 *    The networking layer thinks we timed out.
 *
 */

static void irport_timeout(struct net_device *dev)
{
	struct irport_cb *self;
	int iobase;
	int iir, lsr;
	unsigned long flags;

	self = (struct irport_cb *) dev->priv;
	IRDA_ASSERT(self != NULL, return;);
	iobase = self->io.sir_base;
	
	IRDA_WARNING("%s: transmit timed out, jiffies = %ld, trans_start = %ld\n",
		dev->name, jiffies, dev->trans_start);
	spin_lock_irqsave(&self->lock, flags);

	/* Debug what's happening... */

	/* Get interrupt status */
	lsr = inb(iobase+UART_LSR);
	/* Read interrupt register */
	iir = inb(iobase+UART_IIR);
	IRDA_DEBUG(0, "%s(), iir=%02x, lsr=%02x, iobase=%#x\n", 
		   __FUNCTION__, iir, lsr, iobase);

	IRDA_DEBUG(0, "%s(), transmitting=%d, remain=%d, done=%td\n",
		   __FUNCTION__, self->transmitting, self->tx_buff.len,
		   self->tx_buff.data - self->tx_buff.head);

	/* Now, restart the port */
	irport_start(self);
	self->change_speed(self->priv, self->io.speed);
	/* This will re-enable irqs */
	outb(/*UART_IER_RLSI|*/UART_IER_RDI/*|UART_IER_THRI*/, iobase+UART_IER);
	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&self->lock, flags);

	netif_wake_queue(dev);
}
 
/*
 * Function irport_wait_hw_transmitter_finish ()
 *
 *    Wait for the real end of HW transmission
 *
 * The UART is a strict FIFO, and we get called only when we have finished
 * pushing data to the FIFO, so the maximum amount of time we must wait
 * is only for the FIFO to drain out.
 *
 * We use a simple calibrated loop. We may need to adjust the loop
 * delay (udelay) to balance I/O traffic and latency. And we also need to
 * adjust the maximum timeout.
 * It would probably be better to wait for the proper interrupt,
 * but it doesn't seem to be available.
 *
 * We can't use jiffies or kernel timers because :
 * 1) We are called from the interrupt handler, which disable softirqs,
 * so jiffies won't be increased
 * 2) Jiffies granularity is usually very coarse (10ms), and we don't
 * want to wait that long to detect stuck hardware.
 * Jean II
 */

static void irport_wait_hw_transmitter_finish(struct irport_cb *self)
{
	int iobase;
	int count = 1000;	/* 1 ms */
	
	iobase = self->io.sir_base;

	/* Calibrated busy loop */
	while((count-- > 0) && !(inb(iobase+UART_LSR) & UART_LSR_TEMT))
		udelay(1);

	if(count == 0)
		IRDA_DEBUG(0, "%s(): stuck transmitter\n", __FUNCTION__);
}

/*
 * Function irport_hard_start_xmit (struct sk_buff *skb, struct net_device *dev)
 *
 *    Transmits the current frame until FIFO is full, then
 *    waits until the next transmitt interrupt, and continues until the
 *    frame is transmitted.
 */
static int irport_hard_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct irport_cb *self;
	unsigned long flags;
	int iobase;
	s32 speed;

	IRDA_DEBUG(1, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(dev != NULL, return 0;);
	
	self = (struct irport_cb *) dev->priv;
	IRDA_ASSERT(self != NULL, return 0;);

	iobase = self->io.sir_base;

	netif_stop_queue(dev);

	/* Make sure tests & speed change are atomic */
	spin_lock_irqsave(&self->lock, flags);

	/* Check if we need to change the speed */
	speed = irda_get_next_speed(skb);
	if ((speed != self->io.speed) && (speed != -1)) {
		/* Check for empty frame */
		if (!skb->len) {
			/*
			 * We send frames one by one in SIR mode (no
			 * pipelining), so at this point, if we were sending
			 * a previous frame, we just received the interrupt
			 * telling us it is finished (UART_IIR_THRI).
			 * Therefore, waiting for the transmitter to really
			 * finish draining the fifo won't take too long.
			 * And the interrupt handler is not expected to run.
			 * - Jean II */
			irport_wait_hw_transmitter_finish(self);
			/* Better go there already locked - Jean II */
			irda_task_execute(self, __irport_change_speed, 
					  irport_change_speed_complete, 
					  NULL, (void *) speed);
			dev->trans_start = jiffies;
			spin_unlock_irqrestore(&self->lock, flags);
			dev_kfree_skb(skb);
			return 0;
		} else
			self->new_speed = speed;
	}

	/* Init tx buffer */
	self->tx_buff.data = self->tx_buff.head;

        /* Copy skb to tx_buff while wrapping, stuffing and making CRC */
	self->tx_buff.len = async_wrap_skb(skb, self->tx_buff.data, 
					   self->tx_buff.truesize);
	
	self->stats.tx_bytes += self->tx_buff.len;

	/* We are transmitting */
	self->transmitting = 1;

	/* Turn on transmit finished interrupt. Will fire immediately!  */
	outb(UART_IER_THRI, iobase+UART_IER); 

	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&self->lock, flags);

	dev_kfree_skb(skb);
	
	return 0;
}
        
/*
 * Function irport_write (driver)
 *
 *    Fill Tx FIFO with transmit data
 *
 * Called only from irport_write_wakeup()
 */
static inline int irport_write(int iobase, int fifo_size, __u8 *buf, int len)
{
	int actual = 0;

	/* Fill FIFO with current frame */
	while ((actual < fifo_size) && (actual < len)) {
		/* Transmit next byte */
		outb(buf[actual], iobase+UART_TX);

		actual++;
	}
        
	return actual;
}

/*
 * Function irport_write_wakeup (tty)
 *
 *    Called by the driver when there's room for more data.  If we have
 *    more packets to send, we send them here.
 *
 * Called only from irport_interrupt()
 * Make sure this function is *not* called while we are receiving,
 * otherwise we will reset fifo and loose data :-(
 */
static inline void irport_write_wakeup(struct irport_cb *self)
{
	int actual = 0;
	int iobase;
	unsigned int fcr;

	IRDA_ASSERT(self != NULL, return;);

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	iobase = self->io.sir_base;

	/* Finished with frame?  */
	if (self->tx_buff.len > 0)  {
		/* Write data left in transmit buffer */
		actual = irport_write(iobase, self->io.fifo_size, 
				      self->tx_buff.data, self->tx_buff.len);
		self->tx_buff.data += actual;
		self->tx_buff.len  -= actual;
	} else {
		/* 
		 *  Now serial buffer is almost free & we can start 
		 *  transmission of another packet. But first we must check
		 *  if we need to change the speed of the hardware
		 */
		if (self->new_speed) {
			irport_wait_hw_transmitter_finish(self);
			irda_task_execute(self, __irport_change_speed, 
					  irport_change_speed_complete, 
					  NULL, (void *) self->new_speed);
			self->new_speed = 0;
		} else {
			/* Tell network layer that we want more frames */
			netif_wake_queue(self->netdev);
		}
		self->stats.tx_packets++;

		/* 
		 * Reset Rx FIFO to make sure that all reflected transmit data
		 * is discarded. This is needed for half duplex operation
		 */
		fcr = irport_get_fcr(self->io.speed);
		fcr |= UART_FCR_CLEAR_RCVR;
		outb(fcr, iobase+UART_FCR);

		/* Finished transmitting */
		self->transmitting = 0;

		/* Turn on receive interrupts */
		outb(UART_IER_RDI, iobase+UART_IER);

		IRDA_DEBUG(1, "%s() : finished Tx\n", __FUNCTION__);
	}
}

/*
 * Function irport_receive (self)
 *
 *    Receive one frame from the infrared port
 *
 * Called only from irport_interrupt()
 */
static inline void irport_receive(struct irport_cb *self) 
{
	int boguscount = 0;
	int iobase;

	IRDA_ASSERT(self != NULL, return;);

	iobase = self->io.sir_base;

	/*  
	 * Receive all characters in Rx FIFO, unwrap and unstuff them. 
         * async_unwrap_char will deliver all found frames  
	 */
	do {
		async_unwrap_char(self->netdev, &self->stats, &self->rx_buff, 
				  inb(iobase+UART_RX));

		/* Make sure we don't stay here too long */
		if (boguscount++ > 32) {
			IRDA_DEBUG(2,"%s(), breaking!\n", __FUNCTION__);
			break;
		}
	} while (inb(iobase+UART_LSR) & UART_LSR_DR);	
}

/*
 * Function irport_interrupt (irq, dev_id)
 *
 *    Interrupt handler
 */
static irqreturn_t irport_interrupt(int irq, void *dev_id) 
{
	struct net_device *dev = dev_id;
	struct irport_cb *self;
	int boguscount = 0;
	int iobase;
	int iir, lsr;
	int handled = 0;

	self = dev->priv;

	spin_lock(&self->lock);

	iobase = self->io.sir_base;

	/* Cut'n'paste interrupt routine from serial.c
	 * This version try to minimise latency and I/O operations.
	 * Simplified and modified to enforce half duplex operation.
	 * - Jean II */

	/* Check status even is iir reg is cleared, more robust and
	 * eliminate a read on the I/O bus - Jean II */
	do {
		/* Get interrupt status ; Clear interrupt */
		lsr = inb(iobase+UART_LSR);
		
		/* Are we receiving or transmitting ? */
		if(!self->transmitting) {
			/* Received something ? */
			if (lsr & UART_LSR_DR)
				irport_receive(self);
		} else {
			/* Room in Tx fifo ? */
			if (lsr & (UART_LSR_THRE | UART_LSR_TEMT))
				irport_write_wakeup(self);
		}

		/* A bit hackish, but working as expected... Jean II */
		if(lsr & (UART_LSR_THRE | UART_LSR_TEMT | UART_LSR_DR))
			handled = 1;

		/* Make sure we don't stay here to long */
		if (boguscount++ > 10) {
			IRDA_WARNING("%s() irq handler looping : lsr=%02x\n",
				     __FUNCTION__, lsr);
			break;
		}

		/* Read interrupt register */
 	        iir = inb(iobase+UART_IIR);

		/* Enable this debug only when no other options and at low
		 * bit rates, otherwise it may cause Rx overruns (lsr=63).
		 * - Jean II */
		IRDA_DEBUG(6, "%s(), iir=%02x, lsr=%02x, iobase=%#x\n", 
			    __FUNCTION__, iir, lsr, iobase);

		/* As long as interrupt pending... */
	} while ((iir & UART_IIR_NO_INT) == 0);

	spin_unlock(&self->lock);
	return IRQ_RETVAL(handled);
}

/*
 * Function irport_net_open (dev)
 *
 *    Network device is taken up. Usually this is done by "ifconfig irda0 up" 
 *   
 */
static int irport_net_open(struct net_device *dev)
{
	struct irport_cb *self;
	int iobase;
	char hwname[16];
	unsigned long flags;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(dev != NULL, return -1;);
	self = (struct irport_cb *) dev->priv;

	iobase = self->io.sir_base;

	if (request_irq(self->io.irq, self->interrupt, 0, dev->name, 
			(void *) dev)) {
		IRDA_DEBUG(0, "%s(), unable to allocate irq=%d\n",
			   __FUNCTION__, self->io.irq);
		return -EAGAIN;
	}

	spin_lock_irqsave(&self->lock, flags);
	/* Init uart */
	irport_start(self);
	/* Set 9600 bauds per default, including at the dongle */
	irda_task_execute(self, __irport_change_speed, 
			  irport_change_speed_complete, 
			  NULL, (void *) 9600);
	spin_unlock_irqrestore(&self->lock, flags);


	/* Give self a hardware name */
	sprintf(hwname, "SIR @ 0x%03x", self->io.sir_base);

	/* 
	 * Open new IrLAP layer instance, now that everything should be
	 * initialized properly 
	 */
	self->irlap = irlap_open(dev, &self->qos, hwname);

	/* Ready to play! */

	netif_start_queue(dev);

	return 0;
}

/*
 * Function irport_net_close (self)
 *
 *    Network device is taken down. Usually this is done by 
 *    "ifconfig irda0 down" 
 */
static int irport_net_close(struct net_device *dev)
{
	struct irport_cb *self;
	int iobase;
	unsigned long flags;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	IRDA_ASSERT(dev != NULL, return -1;);
	self = (struct irport_cb *) dev->priv;

	IRDA_ASSERT(self != NULL, return -1;);

	iobase = self->io.sir_base;

	/* Stop device */
	netif_stop_queue(dev);
	
	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;

	spin_lock_irqsave(&self->lock, flags);
	irport_stop(self);
	spin_unlock_irqrestore(&self->lock, flags);

	free_irq(self->io.irq, dev);

	return 0;
}

/*
 * Function irport_is_receiving (self)
 *
 *    Returns true is we are currently receiving data
 *
 */
static inline int irport_is_receiving(struct irport_cb *self)
{
	return (self->rx_buff.state != OUTSIDE_FRAME);
}

/*
 * Function irport_set_dtr_rts (tty, dtr, rts)
 *
 *    This function can be used by dongles etc. to set or reset the status
 *    of the dtr and rts lines
 */
static int irport_set_dtr_rts(struct net_device *dev, int dtr, int rts)
{
	struct irport_cb *self = dev->priv;
	int iobase;

	IRDA_ASSERT(self != NULL, return -1;);

	iobase = self->io.sir_base;

	if (dtr)
		dtr = UART_MCR_DTR;
	if (rts)
		rts = UART_MCR_RTS;

	outb(dtr|rts|UART_MCR_OUT2, iobase+UART_MCR);

	return 0;
}

static int irport_raw_write(struct net_device *dev, __u8 *buf, int len)
{
	struct irport_cb *self = (struct irport_cb *) dev->priv;
	int actual = 0;
	int iobase;

	IRDA_ASSERT(self != NULL, return -1;);

	iobase = self->io.sir_base;

	/* Tx FIFO should be empty! */
	if (!(inb(iobase+UART_LSR) & UART_LSR_THRE)) {
		IRDA_DEBUG( 0, "%s(), failed, fifo not empty!\n", __FUNCTION__);
		return -1;
	}
        
	/* Fill FIFO with current frame */
	while (actual < len) {
		/* Transmit next byte */
		outb(buf[actual], iobase+UART_TX);
		actual++;
	}

	return actual;
}

/*
 * Function irport_net_ioctl (dev, rq, cmd)
 *
 *    Process IOCTL commands for this device
 *
 */
static int irport_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct irport_cb *self;
	dongle_t *dongle;
	unsigned long flags;
	int ret = 0;

	IRDA_ASSERT(dev != NULL, return -1;);

	self = dev->priv;

	IRDA_ASSERT(self != NULL, return -1;);

	IRDA_DEBUG(2, "%s(), %s, (cmd=0x%X)\n", __FUNCTION__, dev->name, cmd);
	
	switch (cmd) {
	case SIOCSBANDWIDTH: /* Set bandwidth */
		if (!capable(CAP_NET_ADMIN))
			ret = -EPERM;
                else
			irda_task_execute(self, __irport_change_speed, NULL, 
					  NULL, (void *) irq->ifr_baudrate);
		break;
	case SIOCSDONGLE: /* Set dongle */
		if (!capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}

		/* Locking :
		 * irda_device_dongle_init() can't be locked.
		 * irda_task_execute() doesn't need to be locked.
		 * Jean II
		 */

		/* Initialize dongle */
		dongle = irda_device_dongle_init(dev, irq->ifr_dongle);
		if (!dongle)
			break;
		
		dongle->set_mode    = NULL;
		dongle->read        = NULL;
		dongle->write       = irport_raw_write;
		dongle->set_dtr_rts = irport_set_dtr_rts;
		
		/* Now initialize the dongle!  */
		dongle->issue->open(dongle, &self->qos);
		
		/* Reset dongle */
		irda_task_execute(dongle, dongle->issue->reset, NULL, NULL, 
				  NULL);	

		/* Make dongle available to driver only now to avoid
		 * race conditions - Jean II */
		self->dongle = dongle;
		break;
	case SIOCSMEDIABUSY: /* Set media busy */
		if (!capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}

		irda_device_set_media_busy(self->netdev, TRUE);
		break;
	case SIOCGRECEIVING: /* Check if we are receiving right now */
		irq->ifr_receiving = irport_is_receiving(self);
		break;
	case SIOCSDTRRTS:
		if (!capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}

		/* No real need to lock... */
		spin_lock_irqsave(&self->lock, flags);
		irport_set_dtr_rts(dev, irq->ifr_dtr, irq->ifr_rts);
		spin_unlock_irqrestore(&self->lock, flags);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	
	return ret;
}

static struct net_device_stats *irport_net_get_stats(struct net_device *dev)
{
	struct irport_cb *self = (struct irport_cb *) dev->priv;
	
	return &self->stats;
}

static int __init irport_init(void)
{
 	int i;

 	for (i=0; (io[i] < 2000) && (i < ARRAY_SIZE(dev_self)); i++) {
 		if (irport_open(i, io[i], irq[i]) != NULL)
 			return 0;
 	}
	/* 
	 * Maybe something failed, but we can still be usable for FIR drivers 
	 */
 	return 0;
}

/*
 * Function irport_cleanup ()
 *
 *    Close all configured ports
 *
 */
static void __exit irport_cleanup(void)
{
 	int i;

        IRDA_DEBUG( 4, "%s()\n", __FUNCTION__);

	for (i=0; i < ARRAY_SIZE(dev_self); i++) {
 		if (dev_self[i])
 			irport_close(dev_self[i]);
 	}
}

module_param_array(io, int, NULL, 0);
MODULE_PARM_DESC(io, "Base I/O addresses");
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(irq, "IRQ lines");

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Half duplex serial driver for IrDA SIR mode");
MODULE_LICENSE("GPL");

module_init(irport_init);
module_exit(irport_cleanup);

