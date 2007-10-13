/*****************************************************************************
*
* Filename:      stir4200.c
* Version:       0.4
* Description:   Irda SigmaTel USB Dongle
* Status:        Experimental
* Author:        Stephen Hemminger <shemminger@osdl.org>
*
*  	Based on earlier driver by Paul Stewart <stewart@parc.com>
*
*	Copyright (C) 2000, Roman Weissgaerber <weissg@vienna.at>
*	Copyright (C) 2001, Dag Brattli <dag@brattli.net>
*	Copyright (C) 2001, Jean Tourrilhes <jt@hpl.hp.com>
*	Copyright (C) 2004, Stephen Hemminger <shemminger@osdl.org>
*
*	This program is free software; you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation; either version 2 of the License.
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
*****************************************************************************/

/*
 * This dongle does no framing, and requires polling to receive the
 * data.  The STIr4200 has bulk in and out endpoints just like
 * usr-irda devices, but the data it sends and receives is raw; like
 * irtty, it needs to call the wrap and unwrap functions to add and
 * remove SOF/BOF and escape characters to/from the frame.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <net/irda/irda.h>
#include <net/irda/irda_device.h>
#include <net/irda/wrapper.h>
#include <net/irda/crc.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

MODULE_AUTHOR("Stephen Hemminger <shemminger@linux-foundation.org>");
MODULE_DESCRIPTION("IrDA-USB Dongle Driver for SigmaTel STIr4200");
MODULE_LICENSE("GPL");

static int qos_mtt_bits = 0x07;	/* 1 ms or more */
module_param(qos_mtt_bits, int, 0);
MODULE_PARM_DESC(qos_mtt_bits, "Minimum Turn Time");

static int rx_sensitivity = 1;	/* FIR 0..4, SIR 0..6 */
module_param(rx_sensitivity, int, 0);
MODULE_PARM_DESC(rx_sensitivity, "Set Receiver sensitivity (0-6, 0 is most sensitive)");

static int tx_power = 0;	/* 0 = highest ... 3 = lowest */
module_param(tx_power, int, 0);
MODULE_PARM_DESC(tx_power, "Set Transmitter power (0-3, 0 is highest power)");

#define STIR_IRDA_HEADER  	4
#define CTRL_TIMEOUT		100	   /* milliseconds */
#define TRANSMIT_TIMEOUT	200	   /* milliseconds */
#define STIR_FIFO_SIZE		4096
#define FIFO_REGS_SIZE		3

enum FirChars {
	FIR_CE   = 0x7d,
	FIR_XBOF = 0x7f,
	FIR_EOF  = 0x7e,
};

enum StirRequests {
	REQ_WRITE_REG =		0x00,
	REQ_READ_REG =		0x01,
	REQ_READ_ROM =		0x02,
	REQ_WRITE_SINGLE =	0x03,
};

/* Register offsets */
enum StirRegs {
	REG_RSVD=0,
	REG_MODE,
	REG_PDCLK,
	REG_CTRL1,
	REG_CTRL2,
	REG_FIFOCTL,
	REG_FIFOLSB,
	REG_FIFOMSB,
	REG_DPLL,
	REG_IRDIG,
	REG_TEST=15,
};

enum StirModeMask {
	MODE_FIR = 0x80,
	MODE_SIR = 0x20,
	MODE_ASK = 0x10,
	MODE_FASTRX = 0x08,
	MODE_FFRSTEN = 0x04,
	MODE_NRESET = 0x02,
	MODE_2400 = 0x01,
};

enum StirPdclkMask {
	PDCLK_4000000 = 0x02,
	PDCLK_115200 = 0x09,
	PDCLK_57600 = 0x13,
	PDCLK_38400 = 0x1D,
	PDCLK_19200 = 0x3B,
	PDCLK_9600 = 0x77,
	PDCLK_2400 = 0xDF,
};

enum StirCtrl1Mask {
	CTRL1_SDMODE = 0x80,
	CTRL1_RXSLOW = 0x40,
	CTRL1_TXPWD = 0x10,
	CTRL1_RXPWD = 0x08,
	CTRL1_SRESET = 0x01,
};

enum StirCtrl2Mask {
	CTRL2_SPWIDTH = 0x08,
	CTRL2_REVID = 0x03,
};

enum StirFifoCtlMask {
	FIFOCTL_EOF = 0x80,
	FIFOCTL_UNDER = 0x40,
	FIFOCTL_OVER = 0x20,
	FIFOCTL_DIR = 0x10,
	FIFOCTL_CLR = 0x08,
	FIFOCTL_EMPTY = 0x04,
};

enum StirDiagMask {
	IRDIG_RXHIGH = 0x80,
	IRDIG_RXLOW = 0x40,
};

enum StirTestMask {
	TEST_PLLDOWN = 0x80,
	TEST_LOOPIR = 0x40,
	TEST_LOOPUSB = 0x20,
	TEST_TSTENA = 0x10,
	TEST_TSTOSC = 0x0F,
};

struct stir_cb {
        struct usb_device *usbdev;      /* init: probe_irda */
        struct net_device *netdev;      /* network layer */
        struct irlap_cb   *irlap;       /* The link layer we are binded to */
        struct net_device_stats stats;	/* network statistics */
        struct qos_info   qos;
	unsigned 	  speed;	/* Current speed */

        struct task_struct *thread;     /* transmit thread */

	struct sk_buff	  *tx_pending;
	void		  *io_buf;	/* transmit/receive buffer */
	__u8		  *fifo_status;

	iobuff_t  	  rx_buff;	/* receive unwrap state machine */
	struct timeval	  rx_time;
	int		  receiving;
	struct urb	 *rx_urb;
};


/* These are the currently known USB ids */
static struct usb_device_id dongles[] = {
    /* SigmaTel, Inc,  STIr4200 IrDA/USB Bridge */
    { USB_DEVICE(0x066f, 0x4200) },
    { }
};

MODULE_DEVICE_TABLE(usb, dongles);

/* Send control message to set dongle register */
static int write_reg(struct stir_cb *stir, __u16 reg, __u8 value)
{
	struct usb_device *dev = stir->usbdev;

	pr_debug("%s: write reg %d = 0x%x\n",
		 stir->netdev->name, reg, value);
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			       REQ_WRITE_SINGLE,
			       USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE,
			       value, reg, NULL, 0,
			       CTRL_TIMEOUT);
}

/* Send control message to read multiple registers */
static inline int read_reg(struct stir_cb *stir, __u16 reg,
		    __u8 *data, __u16 count)
{
	struct usb_device *dev = stir->usbdev;

	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			       REQ_READ_REG,
			       USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			       0, reg, data, count,
			       CTRL_TIMEOUT);
}

static inline int isfir(u32 speed)
{
	return (speed == 4000000);
}

/*
 * Prepare a FIR IrDA frame for transmission to the USB dongle.  The
 * FIR transmit frame is documented in the datasheet.  It consists of
 * a two byte 0x55 0xAA sequence, two little-endian length bytes, a
 * sequence of exactly 16 XBOF bytes of 0x7E, two BOF bytes of 0x7E,
 * then the data escaped as follows:
 *
 *    0x7D -> 0x7D 0x5D
 *    0x7E -> 0x7D 0x5E
 *    0x7F -> 0x7D 0x5F
 *
 * Then, 4 bytes of little endian (stuffed) FCS follow, then two
 * trailing EOF bytes of 0x7E.
 */
static inline __u8 *stuff_fir(__u8 *p, __u8 c)
{
	switch(c) {
	case 0x7d:
	case 0x7e:
	case 0x7f:
		*p++ = 0x7d;
		c ^= IRDA_TRANS;
		/* fall through */
	default:
		*p++ = c;
	}
	return p;
}

/* Take raw data in skb and put it wrapped into buf */
static unsigned wrap_fir_skb(const struct sk_buff *skb, __u8 *buf)
{
	__u8 *ptr = buf;
	__u32 fcs = ~(crc32_le(~0, skb->data, skb->len));
	__u16 wraplen;
	int i;

	/* Header */
	buf[0] = 0x55;
	buf[1] = 0xAA;

	ptr = buf + STIR_IRDA_HEADER;
	memset(ptr, 0x7f, 16);
	ptr += 16;

	/* BOF */
	*ptr++  = 0x7e;
	*ptr++  = 0x7e;

	/* Address / Control / Information */
	for (i = 0; i < skb->len; i++)
		ptr = stuff_fir(ptr, skb->data[i]);

	/* FCS */
	ptr = stuff_fir(ptr, fcs & 0xff);
	ptr = stuff_fir(ptr, (fcs >> 8) & 0xff);
	ptr = stuff_fir(ptr, (fcs >> 16) & 0xff);
	ptr = stuff_fir(ptr, (fcs >> 24) & 0xff);

	/* EOFs */
	*ptr++ = 0x7e;
	*ptr++ = 0x7e;

	/* Total length, minus the header */
	wraplen = (ptr - buf) - STIR_IRDA_HEADER;
	buf[2] = wraplen & 0xff;
	buf[3] = (wraplen >> 8) & 0xff;

	return wraplen + STIR_IRDA_HEADER;
}

static unsigned wrap_sir_skb(struct sk_buff *skb, __u8 *buf)
{
	__u16 wraplen;

	wraplen = async_wrap_skb(skb, buf + STIR_IRDA_HEADER,
				 STIR_FIFO_SIZE - STIR_IRDA_HEADER);
	buf[0] = 0x55;
	buf[1] = 0xAA;
	buf[2] = wraplen & 0xff;
	buf[3] = (wraplen >> 8) & 0xff;

	return wraplen + STIR_IRDA_HEADER;
}

/*
 * Frame is fully formed in the rx_buff so check crc
 * and pass up to irlap
 * setup for next receive
 */
static void fir_eof(struct stir_cb *stir)
{
	iobuff_t *rx_buff = &stir->rx_buff;
	int len = rx_buff->len - 4;
	struct sk_buff *skb, *nskb;
	__u32 fcs;

	if (unlikely(len <= 0)) {
		pr_debug("%s: short frame len %d\n",
			 stir->netdev->name, len);

		++stir->stats.rx_errors;
		++stir->stats.rx_length_errors;
		return;
	}

	fcs = ~(crc32_le(~0, rx_buff->data, len));
	if (fcs != le32_to_cpu(get_unaligned((__le32 *)(rx_buff->data+len)))) {
		pr_debug("crc error calc 0x%x len %d\n", fcs, len);
		stir->stats.rx_errors++;
		stir->stats.rx_crc_errors++;
		return;
	}

	/* if frame is short then just copy it */
	if (len < IRDA_RX_COPY_THRESHOLD) {
		nskb = dev_alloc_skb(len + 1);
		if (unlikely(!nskb)) {
			++stir->stats.rx_dropped;
			return;
		}
		skb_reserve(nskb, 1);
		skb = nskb;
		skb_copy_to_linear_data(nskb, rx_buff->data, len);
	} else {
		nskb = dev_alloc_skb(rx_buff->truesize);
		if (unlikely(!nskb)) {
			++stir->stats.rx_dropped;
			return;
		}
		skb_reserve(nskb, 1);
		skb = rx_buff->skb;
		rx_buff->skb = nskb;
		rx_buff->head = nskb->data;
	}

	skb_put(skb, len);

	skb_reset_mac_header(skb);
	skb->protocol = htons(ETH_P_IRDA);
	skb->dev = stir->netdev;

	netif_rx(skb);

	stir->stats.rx_packets++;
	stir->stats.rx_bytes += len;

	rx_buff->data = rx_buff->head;
	rx_buff->len = 0;
}

/* Unwrap FIR stuffed data and bump it to IrLAP */
static void stir_fir_chars(struct stir_cb *stir,
			    const __u8 *bytes, int len)
{
	iobuff_t *rx_buff = &stir->rx_buff;
	int	i;

	for (i = 0; i < len; i++) {
		__u8	byte = bytes[i];

		switch(rx_buff->state) {
		case OUTSIDE_FRAME:
			/* ignore garbage till start of frame */
			if (unlikely(byte != FIR_EOF))
				continue;
			/* Now receiving frame */
			rx_buff->state = BEGIN_FRAME;

			/* Time to initialize receive buffer */
			rx_buff->data = rx_buff->head;
			rx_buff->len = 0;
			continue;

		case LINK_ESCAPE:
			if (byte == FIR_EOF) {
				pr_debug("%s: got EOF after escape\n",
					 stir->netdev->name);
				goto frame_error;
			}
			rx_buff->state = INSIDE_FRAME;
			byte ^= IRDA_TRANS;
			break;

		case BEGIN_FRAME:
			/* ignore multiple BOF/EOF */
			if (byte == FIR_EOF)
				continue;
			rx_buff->state = INSIDE_FRAME;
			rx_buff->in_frame = TRUE;

			/* fall through */
		case INSIDE_FRAME:
			switch(byte) {
			case FIR_CE:
				rx_buff->state = LINK_ESCAPE;
				continue;
			case FIR_XBOF:
				/* 0x7f is not used in this framing */
				pr_debug("%s: got XBOF without escape\n",
					 stir->netdev->name);
				goto frame_error;
			case FIR_EOF:
				rx_buff->state = OUTSIDE_FRAME;
				rx_buff->in_frame = FALSE;
				fir_eof(stir);
				continue;
			}
			break;
		}

		/* add byte to rx buffer */
		if (unlikely(rx_buff->len >= rx_buff->truesize)) {
			pr_debug("%s: fir frame exceeds %d\n",
				 stir->netdev->name, rx_buff->truesize);
			++stir->stats.rx_over_errors;
			goto error_recovery;
		}

		rx_buff->data[rx_buff->len++] = byte;
		continue;

	frame_error:
		++stir->stats.rx_frame_errors;

	error_recovery:
		++stir->stats.rx_errors;
		rx_buff->state = OUTSIDE_FRAME;
		rx_buff->in_frame = FALSE;
	}
}

/* Unwrap SIR stuffed data and bump it up to IrLAP */
static void stir_sir_chars(struct stir_cb *stir,
			    const __u8 *bytes, int len)
{
	int i;

	for (i = 0; i < len; i++)
		async_unwrap_char(stir->netdev, &stir->stats,
				  &stir->rx_buff, bytes[i]);
}

static inline void unwrap_chars(struct stir_cb *stir,
				const __u8 *bytes, int length)
{
	if (isfir(stir->speed))
		stir_fir_chars(stir, bytes, length);
	else
		stir_sir_chars(stir, bytes, length);
}

/* Mode parameters for each speed */
static const struct {
	unsigned speed;
	__u8 pdclk;
} stir_modes[] = {
        { 2400,    PDCLK_2400 },
        { 9600,    PDCLK_9600 },
        { 19200,   PDCLK_19200 },
        { 38400,   PDCLK_38400 },
        { 57600,   PDCLK_57600 },
        { 115200,  PDCLK_115200 },
        { 4000000, PDCLK_4000000 },
};


/*
 * Setup chip for speed.
 *  Called at startup to initialize the chip
 *  and on speed changes.
 *
 * Note: Write multiple registers doesn't appear to work
 */
static int change_speed(struct stir_cb *stir, unsigned speed)
{
	int i, err;
	__u8 mode;

	for (i = 0; i < ARRAY_SIZE(stir_modes); ++i) {
		if (speed == stir_modes[i].speed)
			goto found;
	}

	warn("%s: invalid speed %d", stir->netdev->name, speed);
	return -EINVAL;

 found:
	pr_debug("speed change from %d to %d\n", stir->speed, speed);

	/* Reset modulator */
	err = write_reg(stir, REG_CTRL1, CTRL1_SRESET);
	if (err)
		goto out;

	/* Undocumented magic to tweak the DPLL */
	err = write_reg(stir, REG_DPLL, 0x15);
	if (err)
		goto out;

	/* Set clock */
	err = write_reg(stir, REG_PDCLK, stir_modes[i].pdclk);
	if (err)
		goto out;

	mode = MODE_NRESET | MODE_FASTRX;
	if (isfir(speed))
		mode |= MODE_FIR | MODE_FFRSTEN;
	else
		mode |= MODE_SIR;

	if (speed == 2400)
		mode |= MODE_2400;

	err = write_reg(stir, REG_MODE, mode);
	if (err)
		goto out;

	/* This resets TEMIC style transceiver if any. */
	err = write_reg(stir, REG_CTRL1,
			CTRL1_SDMODE | (tx_power & 3) << 1);
	if (err)
		goto out;

	err = write_reg(stir, REG_CTRL1, (tx_power & 3) << 1);
	if (err)
		goto out;

	/* Reset sensitivity */
	err = write_reg(stir, REG_CTRL2, (rx_sensitivity & 7) << 5);
 out:
	stir->speed = speed;
	return err;
}

/*
 * Called from net/core when new frame is available.
 */
static int stir_hard_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct stir_cb *stir = netdev_priv(netdev);

	netif_stop_queue(netdev);

	/* the IRDA wrapping routines don't deal with non linear skb */
	SKB_LINEAR_ASSERT(skb);

	skb = xchg(&stir->tx_pending, skb);
        wake_up_process(stir->thread);
	
	/* this should never happen unless stop/wakeup problem */
	if (unlikely(skb)) {
		WARN_ON(1);
		dev_kfree_skb(skb);
	}

	return 0;
}

/*
 * Wait for the transmit FIFO to have space for next data
 *
 * If space < 0 then wait till FIFO completely drains.
 * FYI: can take up to 13 seconds at 2400baud.
 */
static int fifo_txwait(struct stir_cb *stir, int space)
{
	int err;
	unsigned long count, status;

	/* Read FIFO status and count */
	for(;;) {
		err = read_reg(stir, REG_FIFOCTL, stir->fifo_status, 
				   FIFO_REGS_SIZE);
		if (unlikely(err != FIFO_REGS_SIZE)) {
			warn("%s: FIFO register read error: %d", 
			     stir->netdev->name, err);

			return err;
		}

		status = stir->fifo_status[0];
		count = (unsigned)(stir->fifo_status[2] & 0x1f) << 8 
			| stir->fifo_status[1];

		pr_debug("fifo status 0x%lx count %lu\n", status, count);

		/* is fifo receiving already, or empty */
		if (!(status & FIFOCTL_DIR)
		    || (status & FIFOCTL_EMPTY))
			return 0;

		if (signal_pending(current))
			return -EINTR;

		/* shutting down? */
		if (!netif_running(stir->netdev)
		    || !netif_device_present(stir->netdev))
			return -ESHUTDOWN;

		/* only waiting for some space */
		if (space >= 0 && STIR_FIFO_SIZE - 4 > space + count)
			return 0;

		/* estimate transfer time for remaining chars */
		msleep((count * 8000) / stir->speed);
	}
			
	err = write_reg(stir, REG_FIFOCTL, FIFOCTL_CLR);
	if (err) 
		return err;
	err = write_reg(stir, REG_FIFOCTL, 0);
	if (err)
		return err;

	return 0;
}


/* Wait for turnaround delay before starting transmit.  */
static void turnaround_delay(const struct stir_cb *stir, long us)
{
	long ticks;
	struct timeval now;

	if (us <= 0)
		return;

	do_gettimeofday(&now);
	if (now.tv_sec - stir->rx_time.tv_sec > 0)
		us -= USEC_PER_SEC;
	us -= now.tv_usec - stir->rx_time.tv_usec;
	if (us < 10)
		return;

	ticks = us / (1000000 / HZ);
	if (ticks > 0)
		schedule_timeout_interruptible(1 + ticks);
	else
		udelay(us);
}

/*
 * Start receiver by submitting a request to the receive pipe.
 * If nothing is available it will return after rx_interval.
 */
static int receive_start(struct stir_cb *stir)
{
	/* reset state */
	stir->receiving = 1;

	stir->rx_buff.in_frame = FALSE;
	stir->rx_buff.state = OUTSIDE_FRAME;

	stir->rx_urb->status = 0;
	return usb_submit_urb(stir->rx_urb, GFP_KERNEL);
}

/* Stop all pending receive Urb's */
static void receive_stop(struct stir_cb *stir)
{
	stir->receiving = 0;
	usb_kill_urb(stir->rx_urb);

	if (stir->rx_buff.in_frame) 
		stir->stats.collisions++;
}
/*
 * Wrap data in socket buffer and send it.
 */
static void stir_send(struct stir_cb *stir, struct sk_buff *skb)
{
	unsigned wraplen;
	int first_frame = 0;

	/* if receiving, need to turnaround */
	if (stir->receiving) {
		receive_stop(stir);
		turnaround_delay(stir, irda_get_mtt(skb));
		first_frame = 1;
	}

	if (isfir(stir->speed))
		wraplen = wrap_fir_skb(skb, stir->io_buf);
	else
		wraplen = wrap_sir_skb(skb, stir->io_buf);
		
	/* check for space available in fifo */
	if (!first_frame)
		fifo_txwait(stir, wraplen);

	stir->stats.tx_packets++;
	stir->stats.tx_bytes += skb->len;
	stir->netdev->trans_start = jiffies;
	pr_debug("send %d (%d)\n", skb->len, wraplen);

	if (usb_bulk_msg(stir->usbdev, usb_sndbulkpipe(stir->usbdev, 1),
			 stir->io_buf, wraplen,
			 NULL, TRANSMIT_TIMEOUT))
		stir->stats.tx_errors++;
}

/*
 * Transmit state machine thread
 */
static int stir_transmit_thread(void *arg)
{
	struct stir_cb *stir = arg;
	struct net_device *dev = stir->netdev;
	struct sk_buff *skb;

        while (!kthread_should_stop()) {
#ifdef CONFIG_PM
		/* if suspending, then power off and wait */
		if (unlikely(freezing(current))) {
			if (stir->receiving)
				receive_stop(stir);
			else
				fifo_txwait(stir, -1);

			write_reg(stir, REG_CTRL1, CTRL1_TXPWD|CTRL1_RXPWD);

			refrigerator();

			if (change_speed(stir, stir->speed))
				break;
		}
#endif

		/* if something to send? */
		skb = xchg(&stir->tx_pending, NULL);
		if (skb) {
			unsigned new_speed = irda_get_next_speed(skb);
			netif_wake_queue(dev);

			if (skb->len > 0)
				stir_send(stir, skb);
			dev_kfree_skb(skb);

			if ((new_speed != -1) && (stir->speed != new_speed)) {
				if (fifo_txwait(stir, -1) ||
				    change_speed(stir, new_speed))
					break;
			}
			continue;
		}

		/* nothing to send? start receiving */
		if (!stir->receiving 
		    && irda_device_txqueue_empty(dev)) {
			/* Wait otherwise chip gets confused. */
			if (fifo_txwait(stir, -1))
				break;

			if (unlikely(receive_start(stir))) {
				if (net_ratelimit())
					info("%s: receive usb submit failed",
					     stir->netdev->name);
				stir->receiving = 0;
				msleep(10);
				continue;
			}
		}

		/* sleep if nothing to send */
                set_current_state(TASK_INTERRUPTIBLE);
                schedule();

	}
        return 0;
}


/*
 * USB bulk receive completion callback.
 * Wakes up every ms (usb round trip) with wrapped 
 * data.
 */
static void stir_rcv_irq(struct urb *urb)
{
	struct stir_cb *stir = urb->context;
	int err;

	/* in process of stopping, just drop data */
	if (!netif_running(stir->netdev))
		return;

	/* unlink, shutdown, unplug, other nasties */
	if (urb->status != 0) 
		return;

	if (urb->actual_length > 0) {
		pr_debug("receive %d\n", urb->actual_length);
		unwrap_chars(stir, urb->transfer_buffer,
			     urb->actual_length);
		
		stir->netdev->last_rx = jiffies;
		do_gettimeofday(&stir->rx_time);
	}

	/* kernel thread is stopping receiver don't resubmit */
	if (!stir->receiving)
		return;

	/* resubmit existing urb */
	err = usb_submit_urb(urb, GFP_ATOMIC);

	/* in case of error, the kernel thread will restart us */
	if (err) {
		warn("%s: usb receive submit error: %d",
			stir->netdev->name, err);
		stir->receiving = 0;
		wake_up_process(stir->thread);
	}
}

/*
 * Function stir_net_open (dev)
 *
 *    Network device is taken up. Usually this is done by "ifconfig irda0 up"
 */
static int stir_net_open(struct net_device *netdev)
{
	struct stir_cb *stir = netdev_priv(netdev);
	int err;
	char hwname[16];

	err = usb_clear_halt(stir->usbdev, usb_sndbulkpipe(stir->usbdev, 1));
	if (err)
		goto err_out1;
	err = usb_clear_halt(stir->usbdev, usb_rcvbulkpipe(stir->usbdev, 2));
	if (err)
		goto err_out1;

	err = change_speed(stir, 9600);
	if (err)
		goto err_out1;

	err = -ENOMEM;

	/* Initialize for SIR/FIR to copy data directly into skb.  */
	stir->receiving = 0;
	stir->rx_buff.truesize = IRDA_SKB_MAX_MTU;
	stir->rx_buff.skb = dev_alloc_skb(IRDA_SKB_MAX_MTU);
	if (!stir->rx_buff.skb) 
		goto err_out1;

	skb_reserve(stir->rx_buff.skb, 1);
	stir->rx_buff.head = stir->rx_buff.skb->data;
	do_gettimeofday(&stir->rx_time);

	stir->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!stir->rx_urb) 
		goto err_out2;

	stir->io_buf = kmalloc(STIR_FIFO_SIZE, GFP_KERNEL);
	if (!stir->io_buf)
		goto err_out3;

	usb_fill_bulk_urb(stir->rx_urb, stir->usbdev,
			  usb_rcvbulkpipe(stir->usbdev, 2),
			  stir->io_buf, STIR_FIFO_SIZE,
			  stir_rcv_irq, stir);

	stir->fifo_status = kmalloc(FIFO_REGS_SIZE, GFP_KERNEL);
	if (!stir->fifo_status) 
		goto err_out4;
		
	/*
	 * Now that everything should be initialized properly,
	 * Open new IrLAP layer instance to take care of us...
	 * Note : will send immediately a speed change...
	 */
	sprintf(hwname, "usb#%d", stir->usbdev->devnum);
	stir->irlap = irlap_open(netdev, &stir->qos, hwname);
	if (!stir->irlap) {
		err("stir4200: irlap_open failed");
		goto err_out5;
	}

	/** Start kernel thread for transmit.  */
	stir->thread = kthread_run(stir_transmit_thread, stir,
				   "%s", stir->netdev->name);
        if (IS_ERR(stir->thread)) {
                err = PTR_ERR(stir->thread);
		err("stir4200: unable to start kernel thread");
		goto err_out6;
	}

	netif_start_queue(netdev);

	return 0;

 err_out6:
	irlap_close(stir->irlap);
 err_out5:
	kfree(stir->fifo_status);
 err_out4:
	kfree(stir->io_buf);
 err_out3:
	usb_free_urb(stir->rx_urb);
 err_out2:
	kfree_skb(stir->rx_buff.skb);
 err_out1:
	return err;
}

/*
 * Function stir_net_close (stir)
 *
 *    Network device is taken down. Usually this is done by
 *    "ifconfig irda0 down"
 */
static int stir_net_close(struct net_device *netdev)
{
	struct stir_cb *stir = netdev_priv(netdev);

	/* Stop transmit processing */
	netif_stop_queue(netdev);

	/* Kill transmit thread */
	kthread_stop(stir->thread);
	kfree(stir->fifo_status);

	/* Mop up receive urb's */
	usb_kill_urb(stir->rx_urb);
	
	kfree(stir->io_buf);
	usb_free_urb(stir->rx_urb);
	kfree_skb(stir->rx_buff.skb);

	/* Stop and remove instance of IrLAP */
	if (stir->irlap)
		irlap_close(stir->irlap);

	stir->irlap = NULL;

	return 0;
}

/*
 * IOCTLs : Extra out-of-band network commands...
 */
static int stir_net_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct stir_cb *stir = netdev_priv(netdev);
	int ret = 0;

	switch (cmd) {
	case SIOCSBANDWIDTH: /* Set bandwidth */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		/* Check if the device is still there */
		if (netif_device_present(stir->netdev))
			ret = change_speed(stir, irq->ifr_baudrate);
		break;

	case SIOCSMEDIABUSY: /* Set media busy */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		/* Check if the IrDA stack is still there */
		if (netif_running(stir->netdev))
			irda_device_set_media_busy(stir->netdev, TRUE);
		break;

	case SIOCGRECEIVING:
		/* Only approximately true */
		irq->ifr_receiving = stir->receiving;
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/*
 * Get device stats (for /proc/net/dev and ifconfig)
 */
static struct net_device_stats *stir_net_get_stats(struct net_device *netdev)
{
	struct stir_cb *stir = netdev_priv(netdev);
	return &stir->stats;
}

/*
 * This routine is called by the USB subsystem for each new device
 * in the system. We need to check if the device is ours, and in
 * this case start handling it.
 * Note : it might be worth protecting this function by a global
 * spinlock... Or not, because maybe USB already deal with that...
 */
static int stir_probe(struct usb_interface *intf,
		      const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct stir_cb *stir = NULL;
	struct net_device *net;
	int ret = -ENOMEM;

	/* Allocate network device container. */
	net = alloc_irdadev(sizeof(*stir));
	if(!net)
		goto err_out1;

	SET_NETDEV_DEV(net, &intf->dev);
	stir = netdev_priv(net);
	stir->netdev = net;
	stir->usbdev = dev;

	ret = usb_reset_configuration(dev);
	if (ret != 0) {
		err("stir4200: usb reset configuration failed");
		goto err_out2;
	}

	printk(KERN_INFO "SigmaTel STIr4200 IRDA/USB found at address %d, "
		"Vendor: %x, Product: %x\n",
	       dev->devnum, le16_to_cpu(dev->descriptor.idVendor),
	       le16_to_cpu(dev->descriptor.idProduct));

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&stir->qos);

	/* That's the Rx capability. */
	stir->qos.baud_rate.bits       &= IR_2400 | IR_9600 | IR_19200 |
					 IR_38400 | IR_57600 | IR_115200 |
					 (IR_4000000 << 8);
	stir->qos.min_turn_time.bits   &= qos_mtt_bits;
	irda_qos_bits_to_value(&stir->qos);

	/* Override the network functions we need to use */
	net->hard_start_xmit = stir_hard_xmit;
	net->open            = stir_net_open;
	net->stop            = stir_net_close;
	net->get_stats	     = stir_net_get_stats;
	net->do_ioctl        = stir_net_ioctl;

	ret = register_netdev(net);
	if (ret != 0)
		goto err_out2;

	info("IrDA: Registered SigmaTel device %s", net->name);

	usb_set_intfdata(intf, stir);

	return 0;

err_out2:
	free_netdev(net);
err_out1:
	return ret;
}

/*
 * The current device is removed, the USB layer tell us to shut it down...
 */
static void stir_disconnect(struct usb_interface *intf)
{
	struct stir_cb *stir = usb_get_intfdata(intf);

	if (!stir)
		return;

	unregister_netdev(stir->netdev);
	free_netdev(stir->netdev);

	usb_set_intfdata(intf, NULL);
}

#ifdef CONFIG_PM
/* USB suspend, so power off the transmitter/receiver */
static int stir_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct stir_cb *stir = usb_get_intfdata(intf);

	netif_device_detach(stir->netdev);
	return 0;
}

/* Coming out of suspend, so reset hardware */
static int stir_resume(struct usb_interface *intf)
{
	struct stir_cb *stir = usb_get_intfdata(intf);

	netif_device_attach(stir->netdev);

	/* receiver restarted when send thread wakes up */
	return 0;
}
#endif

/*
 * USB device callbacks
 */
static struct usb_driver irda_driver = {
	.name		= "stir4200",
	.probe		= stir_probe,
	.disconnect	= stir_disconnect,
	.id_table	= dongles,
#ifdef CONFIG_PM
	.suspend	= stir_suspend,
	.resume		= stir_resume,
#endif
};

/*
 * Module insertion
 */
static int __init stir_init(void)
{
	return usb_register(&irda_driver);
}
module_init(stir_init);

/*
 * Module removal
 */
static void __exit stir_cleanup(void)
{
	/* Deregister the driver and remove all pending instances */
	usb_deregister(&irda_driver);
}
module_exit(stir_cleanup);
