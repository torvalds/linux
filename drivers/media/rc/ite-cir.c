/*
 * Driver for ITE Tech Inc. IT8712F/IT8512 CIR
 *
 * Copyright (C) 2010 Juan Jesús García de Soria <skandalfo@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA.
 *
 * Inspired by the original lirc_it87 and lirc_ite8709 drivers, on top of the
 * skeleton provided by the nuvoton-cir driver.
 *
 * The lirc_it87 driver was originally written by Hans-Gunter Lutke Uphues
 * <hg_lu@web.de> in 2001, with enhancements by Christoph Bartelmus
 * <lirc@bartelmus.de>, Andrew Calkin <r_tay@hotmail.com> and James Edwards
 * <jimbo-lirc@edwardsclan.net>.
 *
 * The lirc_ite8709 driver was written by Grégory Lardière
 * <spmf2004-lirc@yahoo.fr> in 2008.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pnp.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/bitops.h>
#include <media/rc-core.h>
#include <linux/pci_ids.h>

#include "ite-cir.h"

/* module parameters */

/* debug level */
static int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging output");

/* low limit for RX carrier freq, Hz, 0 for no RX demodulation */
static int rx_low_carrier_freq;
module_param(rx_low_carrier_freq, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(rx_low_carrier_freq, "Override low RX carrier frequency, Hz, "
		 "0 for no RX demodulation");

/* high limit for RX carrier freq, Hz, 0 for no RX demodulation */
static int rx_high_carrier_freq;
module_param(rx_high_carrier_freq, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(rx_high_carrier_freq, "Override high RX carrier frequency, "
		 "Hz, 0 for no RX demodulation");

/* override tx carrier frequency */
static int tx_carrier_freq;
module_param(tx_carrier_freq, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(tx_carrier_freq, "Override TX carrier frequency, Hz");

/* override tx duty cycle */
static int tx_duty_cycle;
module_param(tx_duty_cycle, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(tx_duty_cycle, "Override TX duty cycle, 1-100");

/* override default sample period */
static long sample_period;
module_param(sample_period, long, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(sample_period, "Override carrier sample period, us");

/* override detected model id */
static int model_number = -1;
module_param(model_number, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(model_number, "Use this model number, don't autodetect");


/* HW-independent code functions */

/* check whether carrier frequency is high frequency */
static inline bool ite_is_high_carrier_freq(unsigned int freq)
{
	return freq >= ITE_HCF_MIN_CARRIER_FREQ;
}

/* get the bits required to program the carrier frequency in CFQ bits,
 * unshifted */
static u8 ite_get_carrier_freq_bits(unsigned int freq)
{
	if (ite_is_high_carrier_freq(freq)) {
		if (freq < 425000)
			return ITE_CFQ_400;

		else if (freq < 465000)
			return ITE_CFQ_450;

		else if (freq < 490000)
			return ITE_CFQ_480;

		else
			return ITE_CFQ_500;
	} else {
			/* trim to limits */
		if (freq < ITE_LCF_MIN_CARRIER_FREQ)
			freq = ITE_LCF_MIN_CARRIER_FREQ;
		if (freq > ITE_LCF_MAX_CARRIER_FREQ)
			freq = ITE_LCF_MAX_CARRIER_FREQ;

		/* convert to kHz and subtract the base freq */
		freq =
		    DIV_ROUND_CLOSEST(freq - ITE_LCF_MIN_CARRIER_FREQ,
				      1000);

		return (u8) freq;
	}
}

/* get the bits required to program the pulse with in TXMPW */
static u8 ite_get_pulse_width_bits(unsigned int freq, int duty_cycle)
{
	unsigned long period_ns, on_ns;

	/* sanitize freq into range */
	if (freq < ITE_LCF_MIN_CARRIER_FREQ)
		freq = ITE_LCF_MIN_CARRIER_FREQ;
	if (freq > ITE_HCF_MAX_CARRIER_FREQ)
		freq = ITE_HCF_MAX_CARRIER_FREQ;

	period_ns = 1000000000UL / freq;
	on_ns = period_ns * duty_cycle / 100;

	if (ite_is_high_carrier_freq(freq)) {
		if (on_ns < 750)
			return ITE_TXMPW_A;

		else if (on_ns < 850)
			return ITE_TXMPW_B;

		else if (on_ns < 950)
			return ITE_TXMPW_C;

		else if (on_ns < 1080)
			return ITE_TXMPW_D;

		else
			return ITE_TXMPW_E;
	} else {
		if (on_ns < 6500)
			return ITE_TXMPW_A;

		else if (on_ns < 7850)
			return ITE_TXMPW_B;

		else if (on_ns < 9650)
			return ITE_TXMPW_C;

		else if (on_ns < 11950)
			return ITE_TXMPW_D;

		else
			return ITE_TXMPW_E;
	}
}

/* decode raw bytes as received by the hardware, and push them to the ir-core
 * layer */
static void ite_decode_bytes(struct ite_dev *dev, const u8 * data, int
			     length)
{
	u32 sample_period;
	unsigned long *ldata;
	unsigned int next_one, next_zero, size;
	DEFINE_IR_RAW_EVENT(ev);

	if (length == 0)
		return;

	sample_period = dev->params.sample_period;
	ldata = (unsigned long *)data;
	size = length << 3;
	next_one = find_next_bit_le(ldata, size, 0);
	if (next_one > 0) {
		ev.pulse = true;
		ev.duration =
		    ITE_BITS_TO_NS(next_one, sample_period);
		ir_raw_event_store_with_filter(dev->rdev, &ev);
	}

	while (next_one < size) {
		next_zero = find_next_zero_bit_le(ldata, size, next_one + 1);
		ev.pulse = false;
		ev.duration = ITE_BITS_TO_NS(next_zero - next_one, sample_period);
		ir_raw_event_store_with_filter(dev->rdev, &ev);

		if (next_zero < size) {
			next_one =
			    find_next_bit_le(ldata,
						     size,
						     next_zero + 1);
			ev.pulse = true;
			ev.duration =
			    ITE_BITS_TO_NS(next_one - next_zero,
					   sample_period);
			ir_raw_event_store_with_filter
			    (dev->rdev, &ev);
		} else
			next_one = size;
	}

	ir_raw_event_handle(dev->rdev);

	ite_dbg_verbose("decoded %d bytes.", length);
}

/* set all the rx/tx carrier parameters; this must be called with the device
 * spinlock held */
static void ite_set_carrier_params(struct ite_dev *dev)
{
	unsigned int freq, low_freq, high_freq;
	int allowance;
	bool use_demodulator;
	bool for_tx = dev->transmitting;

	ite_dbg("%s called", __func__);

	if (for_tx) {
		/* we don't need no stinking calculations */
		freq = dev->params.tx_carrier_freq;
		allowance = ITE_RXDCR_DEFAULT;
		use_demodulator = false;
	} else {
		low_freq = dev->params.rx_low_carrier_freq;
		high_freq = dev->params.rx_high_carrier_freq;

		if (low_freq == 0) {
			/* don't demodulate */
			freq =
			ITE_DEFAULT_CARRIER_FREQ;
			allowance = ITE_RXDCR_DEFAULT;
			use_demodulator = false;
		} else {
			/* calculate the middle freq */
			freq = (low_freq + high_freq) / 2;

			/* calculate the allowance */
			allowance =
			    DIV_ROUND_CLOSEST(10000 * (high_freq - low_freq),
					      ITE_RXDCR_PER_10000_STEP
					      * (high_freq + low_freq));

			if (allowance < 1)
				allowance = 1;

			if (allowance > ITE_RXDCR_MAX)
				allowance = ITE_RXDCR_MAX;
		}
	}

	/* set the carrier parameters in a device-dependent way */
	dev->params.set_carrier_params(dev, ite_is_high_carrier_freq(freq),
		 use_demodulator, ite_get_carrier_freq_bits(freq), allowance,
		 ite_get_pulse_width_bits(freq, dev->params.tx_duty_cycle));
}

/* interrupt service routine for incoming and outgoing CIR data */
static irqreturn_t ite_cir_isr(int irq, void *data)
{
	struct ite_dev *dev = data;
	unsigned long flags;
	irqreturn_t ret = IRQ_RETVAL(IRQ_NONE);
	u8 rx_buf[ITE_RX_FIFO_LEN];
	int rx_bytes;
	int iflags;

	ite_dbg_verbose("%s firing", __func__);

	/* grab the spinlock */
	spin_lock_irqsave(&dev->lock, flags);

	/* read the interrupt flags */
	iflags = dev->params.get_irq_causes(dev);

	/* check for the receive interrupt */
	if (iflags & (ITE_IRQ_RX_FIFO | ITE_IRQ_RX_FIFO_OVERRUN)) {
		/* read the FIFO bytes */
		rx_bytes =
			dev->params.get_rx_bytes(dev, rx_buf,
					     ITE_RX_FIFO_LEN);

		if (rx_bytes > 0) {
			/* drop the spinlock, since the ir-core layer
			 * may call us back again through
			 * ite_s_idle() */
			spin_unlock_irqrestore(&dev->
									 lock,
									 flags);

			/* decode the data we've just received */
			ite_decode_bytes(dev, rx_buf,
								   rx_bytes);

			/* reacquire the spinlock */
			spin_lock_irqsave(&dev->lock,
								    flags);

			/* mark the interrupt as serviced */
			ret = IRQ_RETVAL(IRQ_HANDLED);
		}
	} else if (iflags & ITE_IRQ_TX_FIFO) {
		/* FIFO space available interrupt */
		ite_dbg_verbose("got interrupt for TX FIFO");

		/* wake any sleeping transmitter */
		wake_up_interruptible(&dev->tx_queue);

		/* mark the interrupt as serviced */
		ret = IRQ_RETVAL(IRQ_HANDLED);
	}

	/* drop the spinlock */
	spin_unlock_irqrestore(&dev->lock, flags);

	ite_dbg_verbose("%s done returning %d", __func__, (int)ret);

	return ret;
}

/* set the rx carrier freq range, guess it's in Hz... */
static int ite_set_rx_carrier_range(struct rc_dev *rcdev, u32 carrier_low, u32
				    carrier_high)
{
	unsigned long flags;
	struct ite_dev *dev = rcdev->priv;

	spin_lock_irqsave(&dev->lock, flags);
	dev->params.rx_low_carrier_freq = carrier_low;
	dev->params.rx_high_carrier_freq = carrier_high;
	ite_set_carrier_params(dev);
	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/* set the tx carrier freq, guess it's in Hz... */
static int ite_set_tx_carrier(struct rc_dev *rcdev, u32 carrier)
{
	unsigned long flags;
	struct ite_dev *dev = rcdev->priv;

	spin_lock_irqsave(&dev->lock, flags);
	dev->params.tx_carrier_freq = carrier;
	ite_set_carrier_params(dev);
	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/* set the tx duty cycle by controlling the pulse width */
static int ite_set_tx_duty_cycle(struct rc_dev *rcdev, u32 duty_cycle)
{
	unsigned long flags;
	struct ite_dev *dev = rcdev->priv;

	spin_lock_irqsave(&dev->lock, flags);
	dev->params.tx_duty_cycle = duty_cycle;
	ite_set_carrier_params(dev);
	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/* transmit out IR pulses; what you get here is a batch of alternating
 * pulse/space/pulse/space lengths that we should write out completely through
 * the FIFO, blocking on a full FIFO */
static int ite_tx_ir(struct rc_dev *rcdev, unsigned *txbuf, unsigned n)
{
	unsigned long flags;
	struct ite_dev *dev = rcdev->priv;
	bool is_pulse = false;
	int remaining_us, fifo_avail, fifo_remaining, last_idx = 0;
	int max_rle_us, next_rle_us;
	int ret = n;
	u8 last_sent[ITE_TX_FIFO_LEN];
	u8 val;

	ite_dbg("%s called", __func__);

	/* clear the array just in case */
	memset(last_sent, 0, ARRAY_SIZE(last_sent));

	spin_lock_irqsave(&dev->lock, flags);

	/* let everybody know we're now transmitting */
	dev->transmitting = true;

	/* and set the carrier values for transmission */
	ite_set_carrier_params(dev);

	/* calculate how much time we can send in one byte */
	max_rle_us =
	    (ITE_BAUDRATE_DIVISOR * dev->params.sample_period *
	     ITE_TX_MAX_RLE) / 1000;

	/* disable the receiver */
	dev->params.disable_rx(dev);

	/* this is where we'll begin filling in the FIFO, until it's full.
	 * then we'll just activate the interrupt, wait for it to wake us up
	 * again, disable it, continue filling the FIFO... until everything
	 * has been pushed out */
	fifo_avail =
	    ITE_TX_FIFO_LEN - dev->params.get_tx_used_slots(dev);

	while (n > 0 && dev->in_use) {
		/* transmit the next sample */
		is_pulse = !is_pulse;
		remaining_us = *(txbuf++);
		n--;

		ite_dbg("%s: %ld",
				      ((is_pulse) ? "pulse" : "space"),
				      (long int)
				      remaining_us);

		/* repeat while the pulse is non-zero length */
		while (remaining_us > 0 && dev->in_use) {
			if (remaining_us > max_rle_us)
				next_rle_us = max_rle_us;

			else
				next_rle_us = remaining_us;

			remaining_us -= next_rle_us;

			/* check what's the length we have to pump out */
			val = (ITE_TX_MAX_RLE * next_rle_us) / max_rle_us;

			/* put it into the sent buffer */
			last_sent[last_idx++] = val;
			last_idx &= (ITE_TX_FIFO_LEN);

			/* encode it for 7 bits */
			val = (val - 1) & ITE_TX_RLE_MASK;

			/* take into account pulse/space prefix */
			if (is_pulse)
				val |= ITE_TX_PULSE;

			else
				val |= ITE_TX_SPACE;

			/*
			 * if we get to 0 available, read again, just in case
			 * some other slot got freed
			 */
			if (fifo_avail <= 0)
				fifo_avail = ITE_TX_FIFO_LEN - dev->params.get_tx_used_slots(dev);

			/* if it's still full */
			if (fifo_avail <= 0) {
				/* enable the tx interrupt */
				dev->params.
				enable_tx_interrupt(dev);

				/* drop the spinlock */
				spin_unlock_irqrestore(&dev->lock, flags);

				/* wait for the FIFO to empty enough */
				wait_event_interruptible(dev->tx_queue, (fifo_avail = ITE_TX_FIFO_LEN - dev->params.get_tx_used_slots(dev)) >= 8);

				/* get the spinlock again */
				spin_lock_irqsave(&dev->lock, flags);

				/* disable the tx interrupt again. */
				dev->params.
				disable_tx_interrupt(dev);
			}

			/* now send the byte through the FIFO */
			dev->params.put_tx_byte(dev, val);
			fifo_avail--;
		}
	}

	/* wait and don't return until the whole FIFO has been sent out;
	 * otherwise we could configure the RX carrier params instead of the
	 * TX ones while the transmission is still being performed! */
	fifo_remaining = dev->params.get_tx_used_slots(dev);
	remaining_us = 0;
	while (fifo_remaining > 0) {
		fifo_remaining--;
		last_idx--;
		last_idx &= (ITE_TX_FIFO_LEN - 1);
		remaining_us += last_sent[last_idx];
	}
	remaining_us = (remaining_us * max_rle_us) / (ITE_TX_MAX_RLE);

	/* drop the spinlock while we sleep */
	spin_unlock_irqrestore(&dev->lock, flags);

	/* sleep remaining_us microseconds */
	mdelay(DIV_ROUND_UP(remaining_us, 1000));

	/* reacquire the spinlock */
	spin_lock_irqsave(&dev->lock, flags);

	/* now we're not transmitting anymore */
	dev->transmitting = false;

	/* and set the carrier values for reception */
	ite_set_carrier_params(dev);

	/* reenable the receiver */
	if (dev->in_use)
		dev->params.enable_rx(dev);

	/* notify transmission end */
	wake_up_interruptible(&dev->tx_ended);

	spin_unlock_irqrestore(&dev->lock, flags);

	return ret;
}

/* idle the receiver if needed */
static void ite_s_idle(struct rc_dev *rcdev, bool enable)
{
	unsigned long flags;
	struct ite_dev *dev = rcdev->priv;

	ite_dbg("%s called", __func__);

	if (enable) {
		spin_lock_irqsave(&dev->lock, flags);
		dev->params.idle_rx(dev);
		spin_unlock_irqrestore(&dev->lock, flags);
	}
}


/* IT8712F HW-specific functions */

/* retrieve a bitmask of the current causes for a pending interrupt; this may
 * be composed of ITE_IRQ_TX_FIFO, ITE_IRQ_RX_FIFO and ITE_IRQ_RX_FIFO_OVERRUN
 * */
static int it87_get_irq_causes(struct ite_dev *dev)
{
	u8 iflags;
	int ret = 0;

	ite_dbg("%s called", __func__);

	/* read the interrupt flags */
	iflags = inb(dev->cir_addr + IT87_IIR) & IT87_II;

	switch (iflags) {
	case IT87_II_RXDS:
		ret = ITE_IRQ_RX_FIFO;
		break;
	case IT87_II_RXFO:
		ret = ITE_IRQ_RX_FIFO_OVERRUN;
		break;
	case IT87_II_TXLDL:
		ret = ITE_IRQ_TX_FIFO;
		break;
	}

	return ret;
}

/* set the carrier parameters; to be called with the spinlock held */
static void it87_set_carrier_params(struct ite_dev *dev, bool high_freq,
				    bool use_demodulator,
				    u8 carrier_freq_bits, u8 allowance_bits,
				    u8 pulse_width_bits)
{
	u8 val;

	ite_dbg("%s called", __func__);

	/* program the RCR register */
	val = inb(dev->cir_addr + IT87_RCR)
		& ~(IT87_HCFS | IT87_RXEND | IT87_RXDCR);

	if (high_freq)
		val |= IT87_HCFS;

	if (use_demodulator)
		val |= IT87_RXEND;

	val |= allowance_bits;

	outb(val, dev->cir_addr + IT87_RCR);

	/* program the TCR2 register */
	outb((carrier_freq_bits << IT87_CFQ_SHIFT) | pulse_width_bits,
		dev->cir_addr + IT87_TCR2);
}

/* read up to buf_size bytes from the RX FIFO; to be called with the spinlock
 * held */
static int it87_get_rx_bytes(struct ite_dev *dev, u8 * buf, int buf_size)
{
	int fifo, read = 0;

	ite_dbg("%s called", __func__);

	/* read how many bytes are still in the FIFO */
	fifo = inb(dev->cir_addr + IT87_RSR) & IT87_RXFBC;

	while (fifo > 0 && buf_size > 0) {
		*(buf++) = inb(dev->cir_addr + IT87_DR);
		fifo--;
		read++;
		buf_size--;
	}

	return read;
}

/* return how many bytes are still in the FIFO; this will be called
 * with the device spinlock NOT HELD while waiting for the TX FIFO to get
 * empty; let's expect this won't be a problem */
static int it87_get_tx_used_slots(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	return inb(dev->cir_addr + IT87_TSR) & IT87_TXFBC;
}

/* put a byte to the TX fifo; this should be called with the spinlock held */
static void it87_put_tx_byte(struct ite_dev *dev, u8 value)
{
	outb(value, dev->cir_addr + IT87_DR);
}

/* idle the receiver so that we won't receive samples until another
  pulse is detected; this must be called with the device spinlock held */
static void it87_idle_rx(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable streaming by clearing RXACT writing it as 1 */
	outb(inb(dev->cir_addr + IT87_RCR) | IT87_RXACT,
		dev->cir_addr + IT87_RCR);

	/* clear the FIFO */
	outb(inb(dev->cir_addr + IT87_TCR1) | IT87_FIFOCLR,
		dev->cir_addr + IT87_TCR1);
}

/* disable the receiver; this must be called with the device spinlock held */
static void it87_disable_rx(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable the receiver interrupts */
	outb(inb(dev->cir_addr + IT87_IER) & ~(IT87_RDAIE | IT87_RFOIE),
		dev->cir_addr + IT87_IER);

	/* disable the receiver */
	outb(inb(dev->cir_addr + IT87_RCR) & ~IT87_RXEN,
		dev->cir_addr + IT87_RCR);

	/* clear the FIFO and RXACT (actually RXACT should have been cleared
	* in the previous outb() call) */
	it87_idle_rx(dev);
}

/* enable the receiver; this must be called with the device spinlock held */
static void it87_enable_rx(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* enable the receiver by setting RXEN */
	outb(inb(dev->cir_addr + IT87_RCR) | IT87_RXEN,
		dev->cir_addr + IT87_RCR);

	/* just prepare it to idle for the next reception */
	it87_idle_rx(dev);

	/* enable the receiver interrupts and master enable flag */
	outb(inb(dev->cir_addr + IT87_IER) | IT87_RDAIE | IT87_RFOIE | IT87_IEC,
		dev->cir_addr + IT87_IER);
}

/* disable the transmitter interrupt; this must be called with the device
 * spinlock held */
static void it87_disable_tx_interrupt(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable the transmitter interrupts */
	outb(inb(dev->cir_addr + IT87_IER) & ~IT87_TLDLIE,
		dev->cir_addr + IT87_IER);
}

/* enable the transmitter interrupt; this must be called with the device
 * spinlock held */
static void it87_enable_tx_interrupt(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* enable the transmitter interrupts and master enable flag */
	outb(inb(dev->cir_addr + IT87_IER) | IT87_TLDLIE | IT87_IEC,
		dev->cir_addr + IT87_IER);
}

/* disable the device; this must be called with the device spinlock held */
static void it87_disable(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* clear out all interrupt enable flags */
	outb(inb(dev->cir_addr + IT87_IER) &
		~(IT87_IEC | IT87_RFOIE | IT87_RDAIE | IT87_TLDLIE),
		dev->cir_addr + IT87_IER);

	/* disable the receiver */
	it87_disable_rx(dev);

	/* erase the FIFO */
	outb(IT87_FIFOCLR | inb(dev->cir_addr + IT87_TCR1),
		dev->cir_addr + IT87_TCR1);
}

/* initialize the hardware */
static void it87_init_hardware(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* enable just the baud rate divisor register,
	disabling all the interrupts at the same time */
	outb((inb(dev->cir_addr + IT87_IER) &
		~(IT87_IEC | IT87_RFOIE | IT87_RDAIE | IT87_TLDLIE)) | IT87_BR,
		dev->cir_addr + IT87_IER);

	/* write out the baud rate divisor */
	outb(ITE_BAUDRATE_DIVISOR & 0xff, dev->cir_addr + IT87_BDLR);
	outb((ITE_BAUDRATE_DIVISOR >> 8) & 0xff, dev->cir_addr + IT87_BDHR);

	/* disable the baud rate divisor register again */
	outb(inb(dev->cir_addr + IT87_IER) & ~IT87_BR,
		dev->cir_addr + IT87_IER);

	/* program the RCR register defaults */
	outb(ITE_RXDCR_DEFAULT, dev->cir_addr + IT87_RCR);

	/* program the TCR1 register */
	outb(IT87_TXMPM_DEFAULT | IT87_TXENDF | IT87_TXRLE
		| IT87_FIFOTL_DEFAULT | IT87_FIFOCLR,
		dev->cir_addr + IT87_TCR1);

	/* program the carrier parameters */
	ite_set_carrier_params(dev);
}

/* IT8512F on ITE8708 HW-specific functions */

/* retrieve a bitmask of the current causes for a pending interrupt; this may
 * be composed of ITE_IRQ_TX_FIFO, ITE_IRQ_RX_FIFO and ITE_IRQ_RX_FIFO_OVERRUN
 * */
static int it8708_get_irq_causes(struct ite_dev *dev)
{
	u8 iflags;
	int ret = 0;

	ite_dbg("%s called", __func__);

	/* read the interrupt flags */
	iflags = inb(dev->cir_addr + IT8708_C0IIR);

	if (iflags & IT85_TLDLI)
		ret |= ITE_IRQ_TX_FIFO;
	if (iflags & IT85_RDAI)
		ret |= ITE_IRQ_RX_FIFO;
	if (iflags & IT85_RFOI)
		ret |= ITE_IRQ_RX_FIFO_OVERRUN;

	return ret;
}

/* set the carrier parameters; to be called with the spinlock held */
static void it8708_set_carrier_params(struct ite_dev *dev, bool high_freq,
				      bool use_demodulator,
				      u8 carrier_freq_bits, u8 allowance_bits,
				      u8 pulse_width_bits)
{
	u8 val;

	ite_dbg("%s called", __func__);

	/* program the C0CFR register, with HRAE=1 */
	outb(inb(dev->cir_addr + IT8708_BANKSEL) | IT8708_HRAE,
		dev->cir_addr + IT8708_BANKSEL);

	val = (inb(dev->cir_addr + IT8708_C0CFR)
		& ~(IT85_HCFS | IT85_CFQ)) | carrier_freq_bits;

	if (high_freq)
		val |= IT85_HCFS;

	outb(val, dev->cir_addr + IT8708_C0CFR);

	outb(inb(dev->cir_addr + IT8708_BANKSEL) & ~IT8708_HRAE,
		   dev->cir_addr + IT8708_BANKSEL);

	/* program the C0RCR register */
	val = inb(dev->cir_addr + IT8708_C0RCR)
		& ~(IT85_RXEND | IT85_RXDCR);

	if (use_demodulator)
		val |= IT85_RXEND;

	val |= allowance_bits;

	outb(val, dev->cir_addr + IT8708_C0RCR);

	/* program the C0TCR register */
	val = inb(dev->cir_addr + IT8708_C0TCR) & ~IT85_TXMPW;
	val |= pulse_width_bits;
	outb(val, dev->cir_addr + IT8708_C0TCR);
}

/* read up to buf_size bytes from the RX FIFO; to be called with the spinlock
 * held */
static int it8708_get_rx_bytes(struct ite_dev *dev, u8 * buf, int buf_size)
{
	int fifo, read = 0;

	ite_dbg("%s called", __func__);

	/* read how many bytes are still in the FIFO */
	fifo = inb(dev->cir_addr + IT8708_C0RFSR) & IT85_RXFBC;

	while (fifo > 0 && buf_size > 0) {
		*(buf++) = inb(dev->cir_addr + IT8708_C0DR);
		fifo--;
		read++;
		buf_size--;
	}

	return read;
}

/* return how many bytes are still in the FIFO; this will be called
 * with the device spinlock NOT HELD while waiting for the TX FIFO to get
 * empty; let's expect this won't be a problem */
static int it8708_get_tx_used_slots(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	return inb(dev->cir_addr + IT8708_C0TFSR) & IT85_TXFBC;
}

/* put a byte to the TX fifo; this should be called with the spinlock held */
static void it8708_put_tx_byte(struct ite_dev *dev, u8 value)
{
	outb(value, dev->cir_addr + IT8708_C0DR);
}

/* idle the receiver so that we won't receive samples until another
  pulse is detected; this must be called with the device spinlock held */
static void it8708_idle_rx(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable streaming by clearing RXACT writing it as 1 */
	outb(inb(dev->cir_addr + IT8708_C0RCR) | IT85_RXACT,
		dev->cir_addr + IT8708_C0RCR);

	/* clear the FIFO */
	outb(inb(dev->cir_addr + IT8708_C0MSTCR) | IT85_FIFOCLR,
		dev->cir_addr + IT8708_C0MSTCR);
}

/* disable the receiver; this must be called with the device spinlock held */
static void it8708_disable_rx(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable the receiver interrupts */
	outb(inb(dev->cir_addr + IT8708_C0IER) &
		~(IT85_RDAIE | IT85_RFOIE),
		dev->cir_addr + IT8708_C0IER);

	/* disable the receiver */
	outb(inb(dev->cir_addr + IT8708_C0RCR) & ~IT85_RXEN,
		dev->cir_addr + IT8708_C0RCR);

	/* clear the FIFO and RXACT (actually RXACT should have been cleared
	 * in the previous outb() call) */
	it8708_idle_rx(dev);
}

/* enable the receiver; this must be called with the device spinlock held */
static void it8708_enable_rx(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* enable the receiver by setting RXEN */
	outb(inb(dev->cir_addr + IT8708_C0RCR) | IT85_RXEN,
		dev->cir_addr + IT8708_C0RCR);

	/* just prepare it to idle for the next reception */
	it8708_idle_rx(dev);

	/* enable the receiver interrupts and master enable flag */
	outb(inb(dev->cir_addr + IT8708_C0IER)
		|IT85_RDAIE | IT85_RFOIE | IT85_IEC,
		dev->cir_addr + IT8708_C0IER);
}

/* disable the transmitter interrupt; this must be called with the device
 * spinlock held */
static void it8708_disable_tx_interrupt(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable the transmitter interrupts */
	outb(inb(dev->cir_addr + IT8708_C0IER) & ~IT85_TLDLIE,
		dev->cir_addr + IT8708_C0IER);
}

/* enable the transmitter interrupt; this must be called with the device
 * spinlock held */
static void it8708_enable_tx_interrupt(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* enable the transmitter interrupts and master enable flag */
	outb(inb(dev->cir_addr + IT8708_C0IER)
		|IT85_TLDLIE | IT85_IEC,
		dev->cir_addr + IT8708_C0IER);
}

/* disable the device; this must be called with the device spinlock held */
static void it8708_disable(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* clear out all interrupt enable flags */
	outb(inb(dev->cir_addr + IT8708_C0IER) &
		~(IT85_IEC | IT85_RFOIE | IT85_RDAIE | IT85_TLDLIE),
		dev->cir_addr + IT8708_C0IER);

	/* disable the receiver */
	it8708_disable_rx(dev);

	/* erase the FIFO */
	outb(IT85_FIFOCLR | inb(dev->cir_addr + IT8708_C0MSTCR),
		dev->cir_addr + IT8708_C0MSTCR);
}

/* initialize the hardware */
static void it8708_init_hardware(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable all the interrupts */
	outb(inb(dev->cir_addr + IT8708_C0IER) &
		~(IT85_IEC | IT85_RFOIE | IT85_RDAIE | IT85_TLDLIE),
		dev->cir_addr + IT8708_C0IER);

	/* program the baud rate divisor */
	outb(inb(dev->cir_addr + IT8708_BANKSEL) | IT8708_HRAE,
		dev->cir_addr + IT8708_BANKSEL);

	outb(ITE_BAUDRATE_DIVISOR & 0xff, dev->cir_addr + IT8708_C0BDLR);
	outb((ITE_BAUDRATE_DIVISOR >> 8) & 0xff,
		   dev->cir_addr + IT8708_C0BDHR);

	outb(inb(dev->cir_addr + IT8708_BANKSEL) & ~IT8708_HRAE,
		   dev->cir_addr + IT8708_BANKSEL);

	/* program the C0MSTCR register defaults */
	outb((inb(dev->cir_addr + IT8708_C0MSTCR) &
			~(IT85_ILSEL | IT85_ILE | IT85_FIFOTL |
			  IT85_FIFOCLR | IT85_RESET)) |
		       IT85_FIFOTL_DEFAULT,
		       dev->cir_addr + IT8708_C0MSTCR);

	/* program the C0RCR register defaults */
	outb((inb(dev->cir_addr + IT8708_C0RCR) &
			~(IT85_RXEN | IT85_RDWOS | IT85_RXEND |
			  IT85_RXACT | IT85_RXDCR)) |
		       ITE_RXDCR_DEFAULT,
		       dev->cir_addr + IT8708_C0RCR);

	/* program the C0TCR register defaults */
	outb((inb(dev->cir_addr + IT8708_C0TCR) &
			~(IT85_TXMPM | IT85_TXMPW))
		       |IT85_TXRLE | IT85_TXENDF |
		       IT85_TXMPM_DEFAULT | IT85_TXMPW_DEFAULT,
		       dev->cir_addr + IT8708_C0TCR);

	/* program the carrier parameters */
	ite_set_carrier_params(dev);
}

/* IT8512F on ITE8709 HW-specific functions */

/* read a byte from the SRAM module */
static inline u8 it8709_rm(struct ite_dev *dev, int index)
{
	outb(index, dev->cir_addr + IT8709_RAM_IDX);
	return inb(dev->cir_addr + IT8709_RAM_VAL);
}

/* write a byte to the SRAM module */
static inline void it8709_wm(struct ite_dev *dev, u8 val, int index)
{
	outb(index, dev->cir_addr + IT8709_RAM_IDX);
	outb(val, dev->cir_addr + IT8709_RAM_VAL);
}

static void it8709_wait(struct ite_dev *dev)
{
	int i = 0;
	/*
	 * loop until device tells it's ready to continue
	 * iterations count is usually ~750 but can sometimes achieve 13000
	 */
	for (i = 0; i < 15000; i++) {
		udelay(2);
		if (it8709_rm(dev, IT8709_MODE) == IT8709_IDLE)
			break;
	}
}

/* read the value of a CIR register */
static u8 it8709_rr(struct ite_dev *dev, int index)
{
	/* just wait in case the previous access was a write */
	it8709_wait(dev);
	it8709_wm(dev, index, IT8709_REG_IDX);
	it8709_wm(dev, IT8709_READ, IT8709_MODE);

	/* wait for the read data to be available */
	it8709_wait(dev);

	/* return the read value */
	return it8709_rm(dev, IT8709_REG_VAL);
}

/* write the value of a CIR register */
static void it8709_wr(struct ite_dev *dev, u8 val, int index)
{
	/* we wait before writing, and not afterwards, since this allows us to
	 * pipeline the host CPU with the microcontroller */
	it8709_wait(dev);
	it8709_wm(dev, val, IT8709_REG_VAL);
	it8709_wm(dev, index, IT8709_REG_IDX);
	it8709_wm(dev, IT8709_WRITE, IT8709_MODE);
}

/* retrieve a bitmask of the current causes for a pending interrupt; this may
 * be composed of ITE_IRQ_TX_FIFO, ITE_IRQ_RX_FIFO and ITE_IRQ_RX_FIFO_OVERRUN
 * */
static int it8709_get_irq_causes(struct ite_dev *dev)
{
	u8 iflags;
	int ret = 0;

	ite_dbg("%s called", __func__);

	/* read the interrupt flags */
	iflags = it8709_rm(dev, IT8709_IIR);

	if (iflags & IT85_TLDLI)
		ret |= ITE_IRQ_TX_FIFO;
	if (iflags & IT85_RDAI)
		ret |= ITE_IRQ_RX_FIFO;
	if (iflags & IT85_RFOI)
		ret |= ITE_IRQ_RX_FIFO_OVERRUN;

	return ret;
}

/* set the carrier parameters; to be called with the spinlock held */
static void it8709_set_carrier_params(struct ite_dev *dev, bool high_freq,
				      bool use_demodulator,
				      u8 carrier_freq_bits, u8 allowance_bits,
				      u8 pulse_width_bits)
{
	u8 val;

	ite_dbg("%s called", __func__);

	val = (it8709_rr(dev, IT85_C0CFR)
		     &~(IT85_HCFS | IT85_CFQ)) |
	    carrier_freq_bits;

	if (high_freq)
		val |= IT85_HCFS;

	it8709_wr(dev, val, IT85_C0CFR);

	/* program the C0RCR register */
	val = it8709_rr(dev, IT85_C0RCR)
		& ~(IT85_RXEND | IT85_RXDCR);

	if (use_demodulator)
		val |= IT85_RXEND;

	val |= allowance_bits;

	it8709_wr(dev, val, IT85_C0RCR);

	/* program the C0TCR register */
	val = it8709_rr(dev, IT85_C0TCR) & ~IT85_TXMPW;
	val |= pulse_width_bits;
	it8709_wr(dev, val, IT85_C0TCR);
}

/* read up to buf_size bytes from the RX FIFO; to be called with the spinlock
 * held */
static int it8709_get_rx_bytes(struct ite_dev *dev, u8 * buf, int buf_size)
{
	int fifo, read = 0;

	ite_dbg("%s called", __func__);

	/* read how many bytes are still in the FIFO */
	fifo = it8709_rm(dev, IT8709_RFSR) & IT85_RXFBC;

	while (fifo > 0 && buf_size > 0) {
		*(buf++) = it8709_rm(dev, IT8709_FIFO + read);
		fifo--;
		read++;
		buf_size--;
	}

	/* 'clear' the FIFO by setting the writing index to 0; this is
	 * completely bound to be racy, but we can't help it, since it's a
	 * limitation of the protocol */
	it8709_wm(dev, 0, IT8709_RFSR);

	return read;
}

/* return how many bytes are still in the FIFO; this will be called
 * with the device spinlock NOT HELD while waiting for the TX FIFO to get
 * empty; let's expect this won't be a problem */
static int it8709_get_tx_used_slots(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	return it8709_rr(dev, IT85_C0TFSR) & IT85_TXFBC;
}

/* put a byte to the TX fifo; this should be called with the spinlock held */
static void it8709_put_tx_byte(struct ite_dev *dev, u8 value)
{
	it8709_wr(dev, value, IT85_C0DR);
}

/* idle the receiver so that we won't receive samples until another
  pulse is detected; this must be called with the device spinlock held */
static void it8709_idle_rx(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable streaming by clearing RXACT writing it as 1 */
	it8709_wr(dev, it8709_rr(dev, IT85_C0RCR) | IT85_RXACT,
			    IT85_C0RCR);

	/* clear the FIFO */
	it8709_wr(dev, it8709_rr(dev, IT85_C0MSTCR) | IT85_FIFOCLR,
			    IT85_C0MSTCR);
}

/* disable the receiver; this must be called with the device spinlock held */
static void it8709_disable_rx(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable the receiver interrupts */
	it8709_wr(dev, it8709_rr(dev, IT85_C0IER) &
			    ~(IT85_RDAIE | IT85_RFOIE),
			    IT85_C0IER);

	/* disable the receiver */
	it8709_wr(dev, it8709_rr(dev, IT85_C0RCR) & ~IT85_RXEN,
			    IT85_C0RCR);

	/* clear the FIFO and RXACT (actually RXACT should have been cleared
	 * in the previous it8709_wr(dev, ) call) */
	it8709_idle_rx(dev);
}

/* enable the receiver; this must be called with the device spinlock held */
static void it8709_enable_rx(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* enable the receiver by setting RXEN */
	it8709_wr(dev, it8709_rr(dev, IT85_C0RCR) | IT85_RXEN,
			    IT85_C0RCR);

	/* just prepare it to idle for the next reception */
	it8709_idle_rx(dev);

	/* enable the receiver interrupts and master enable flag */
	it8709_wr(dev, it8709_rr(dev, IT85_C0IER)
			    |IT85_RDAIE | IT85_RFOIE | IT85_IEC,
			    IT85_C0IER);
}

/* disable the transmitter interrupt; this must be called with the device
 * spinlock held */
static void it8709_disable_tx_interrupt(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable the transmitter interrupts */
	it8709_wr(dev, it8709_rr(dev, IT85_C0IER) & ~IT85_TLDLIE,
			    IT85_C0IER);
}

/* enable the transmitter interrupt; this must be called with the device
 * spinlock held */
static void it8709_enable_tx_interrupt(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* enable the transmitter interrupts and master enable flag */
	it8709_wr(dev, it8709_rr(dev, IT85_C0IER)
			    |IT85_TLDLIE | IT85_IEC,
			    IT85_C0IER);
}

/* disable the device; this must be called with the device spinlock held */
static void it8709_disable(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* clear out all interrupt enable flags */
	it8709_wr(dev, it8709_rr(dev, IT85_C0IER) &
			~(IT85_IEC | IT85_RFOIE | IT85_RDAIE | IT85_TLDLIE),
		  IT85_C0IER);

	/* disable the receiver */
	it8709_disable_rx(dev);

	/* erase the FIFO */
	it8709_wr(dev, IT85_FIFOCLR | it8709_rr(dev, IT85_C0MSTCR),
			    IT85_C0MSTCR);
}

/* initialize the hardware */
static void it8709_init_hardware(struct ite_dev *dev)
{
	ite_dbg("%s called", __func__);

	/* disable all the interrupts */
	it8709_wr(dev, it8709_rr(dev, IT85_C0IER) &
			~(IT85_IEC | IT85_RFOIE | IT85_RDAIE | IT85_TLDLIE),
		  IT85_C0IER);

	/* program the baud rate divisor */
	it8709_wr(dev, ITE_BAUDRATE_DIVISOR & 0xff, IT85_C0BDLR);
	it8709_wr(dev, (ITE_BAUDRATE_DIVISOR >> 8) & 0xff,
			IT85_C0BDHR);

	/* program the C0MSTCR register defaults */
	it8709_wr(dev, (it8709_rr(dev, IT85_C0MSTCR) &
			~(IT85_ILSEL | IT85_ILE | IT85_FIFOTL
			  | IT85_FIFOCLR | IT85_RESET)) | IT85_FIFOTL_DEFAULT,
		  IT85_C0MSTCR);

	/* program the C0RCR register defaults */
	it8709_wr(dev, (it8709_rr(dev, IT85_C0RCR) &
			~(IT85_RXEN | IT85_RDWOS | IT85_RXEND | IT85_RXACT
			  | IT85_RXDCR)) | ITE_RXDCR_DEFAULT,
		  IT85_C0RCR);

	/* program the C0TCR register defaults */
	it8709_wr(dev, (it8709_rr(dev, IT85_C0TCR) & ~(IT85_TXMPM | IT85_TXMPW))
			| IT85_TXRLE | IT85_TXENDF | IT85_TXMPM_DEFAULT
			| IT85_TXMPW_DEFAULT,
		  IT85_C0TCR);

	/* program the carrier parameters */
	ite_set_carrier_params(dev);
}


/* generic hardware setup/teardown code */

/* activate the device for use */
static int ite_open(struct rc_dev *rcdev)
{
	struct ite_dev *dev = rcdev->priv;
	unsigned long flags;

	ite_dbg("%s called", __func__);

	spin_lock_irqsave(&dev->lock, flags);
	dev->in_use = true;

	/* enable the receiver */
	dev->params.enable_rx(dev);

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/* deactivate the device for use */
static void ite_close(struct rc_dev *rcdev)
{
	struct ite_dev *dev = rcdev->priv;
	unsigned long flags;

	ite_dbg("%s called", __func__);

	spin_lock_irqsave(&dev->lock, flags);
	dev->in_use = false;

	/* wait for any transmission to end */
	spin_unlock_irqrestore(&dev->lock, flags);
	wait_event_interruptible(dev->tx_ended, !dev->transmitting);
	spin_lock_irqsave(&dev->lock, flags);

	dev->params.disable(dev);

	spin_unlock_irqrestore(&dev->lock, flags);
}

/* supported models and their parameters */
static const struct ite_dev_params ite_dev_descs[] = {
	{	/* 0: ITE8704 */
	       .model = "ITE8704 CIR transceiver",
	       .io_region_size = IT87_IOREG_LENGTH,
	       .io_rsrc_no = 0,
	       .hw_tx_capable = true,
	       .sample_period = (u32) (1000000000ULL / 115200),
	       .tx_carrier_freq = 38000,
	       .tx_duty_cycle = 33,
	       .rx_low_carrier_freq = 0,
	       .rx_high_carrier_freq = 0,

		/* operations */
	       .get_irq_causes = it87_get_irq_causes,
	       .enable_rx = it87_enable_rx,
	       .idle_rx = it87_idle_rx,
	       .disable_rx = it87_idle_rx,
	       .get_rx_bytes = it87_get_rx_bytes,
	       .enable_tx_interrupt = it87_enable_tx_interrupt,
	       .disable_tx_interrupt = it87_disable_tx_interrupt,
	       .get_tx_used_slots = it87_get_tx_used_slots,
	       .put_tx_byte = it87_put_tx_byte,
	       .disable = it87_disable,
	       .init_hardware = it87_init_hardware,
	       .set_carrier_params = it87_set_carrier_params,
	       },
	{	/* 1: ITE8713 */
	       .model = "ITE8713 CIR transceiver",
	       .io_region_size = IT87_IOREG_LENGTH,
	       .io_rsrc_no = 0,
	       .hw_tx_capable = true,
	       .sample_period = (u32) (1000000000ULL / 115200),
	       .tx_carrier_freq = 38000,
	       .tx_duty_cycle = 33,
	       .rx_low_carrier_freq = 0,
	       .rx_high_carrier_freq = 0,

		/* operations */
	       .get_irq_causes = it87_get_irq_causes,
	       .enable_rx = it87_enable_rx,
	       .idle_rx = it87_idle_rx,
	       .disable_rx = it87_idle_rx,
	       .get_rx_bytes = it87_get_rx_bytes,
	       .enable_tx_interrupt = it87_enable_tx_interrupt,
	       .disable_tx_interrupt = it87_disable_tx_interrupt,
	       .get_tx_used_slots = it87_get_tx_used_slots,
	       .put_tx_byte = it87_put_tx_byte,
	       .disable = it87_disable,
	       .init_hardware = it87_init_hardware,
	       .set_carrier_params = it87_set_carrier_params,
	       },
	{	/* 2: ITE8708 */
	       .model = "ITE8708 CIR transceiver",
	       .io_region_size = IT8708_IOREG_LENGTH,
	       .io_rsrc_no = 0,
	       .hw_tx_capable = true,
	       .sample_period = (u32) (1000000000ULL / 115200),
	       .tx_carrier_freq = 38000,
	       .tx_duty_cycle = 33,
	       .rx_low_carrier_freq = 0,
	       .rx_high_carrier_freq = 0,

		/* operations */
	       .get_irq_causes = it8708_get_irq_causes,
	       .enable_rx = it8708_enable_rx,
	       .idle_rx = it8708_idle_rx,
	       .disable_rx = it8708_idle_rx,
	       .get_rx_bytes = it8708_get_rx_bytes,
	       .enable_tx_interrupt = it8708_enable_tx_interrupt,
	       .disable_tx_interrupt =
	       it8708_disable_tx_interrupt,
	       .get_tx_used_slots = it8708_get_tx_used_slots,
	       .put_tx_byte = it8708_put_tx_byte,
	       .disable = it8708_disable,
	       .init_hardware = it8708_init_hardware,
	       .set_carrier_params = it8708_set_carrier_params,
	       },
	{	/* 3: ITE8709 */
	       .model = "ITE8709 CIR transceiver",
	       .io_region_size = IT8709_IOREG_LENGTH,
	       .io_rsrc_no = 2,
	       .hw_tx_capable = true,
	       .sample_period = (u32) (1000000000ULL / 115200),
	       .tx_carrier_freq = 38000,
	       .tx_duty_cycle = 33,
	       .rx_low_carrier_freq = 0,
	       .rx_high_carrier_freq = 0,

		/* operations */
	       .get_irq_causes = it8709_get_irq_causes,
	       .enable_rx = it8709_enable_rx,
	       .idle_rx = it8709_idle_rx,
	       .disable_rx = it8709_idle_rx,
	       .get_rx_bytes = it8709_get_rx_bytes,
	       .enable_tx_interrupt = it8709_enable_tx_interrupt,
	       .disable_tx_interrupt =
	       it8709_disable_tx_interrupt,
	       .get_tx_used_slots = it8709_get_tx_used_slots,
	       .put_tx_byte = it8709_put_tx_byte,
	       .disable = it8709_disable,
	       .init_hardware = it8709_init_hardware,
	       .set_carrier_params = it8709_set_carrier_params,
	       },
};

static const struct pnp_device_id ite_ids[] = {
	{"ITE8704", 0},		/* Default model */
	{"ITE8713", 1},		/* CIR found in EEEBox 1501U */
	{"ITE8708", 2},		/* Bridged IT8512 */
	{"ITE8709", 3},		/* SRAM-Bridged IT8512 */
	{"", 0},
};

/* allocate memory, probe hardware, and initialize everything */
static int ite_probe(struct pnp_dev *pdev, const struct pnp_device_id
		     *dev_id)
{
	const struct ite_dev_params *dev_desc = NULL;
	struct ite_dev *itdev = NULL;
	struct rc_dev *rdev = NULL;
	int ret = -ENOMEM;
	int model_no;
	int io_rsrc_no;

	ite_dbg("%s called", __func__);

	itdev = kzalloc(sizeof(struct ite_dev), GFP_KERNEL);
	if (!itdev)
		return ret;

	/* input device for IR remote (and tx) */
	rdev = rc_allocate_device();
	if (!rdev)
		goto exit_free_dev_rdev;
	itdev->rdev = rdev;

	ret = -ENODEV;

	/* get the model number */
	model_no = (int)dev_id->driver_data;
	ite_pr(KERN_NOTICE, "Auto-detected model: %s\n",
		ite_dev_descs[model_no].model);

	if (model_number >= 0 && model_number < ARRAY_SIZE(ite_dev_descs)) {
		model_no = model_number;
		ite_pr(KERN_NOTICE, "The model has been fixed by a module "
			"parameter.");
	}

	ite_pr(KERN_NOTICE, "Using model: %s\n", ite_dev_descs[model_no].model);

	/* get the description for the device */
	dev_desc = &ite_dev_descs[model_no];
	io_rsrc_no = dev_desc->io_rsrc_no;

	/* validate pnp resources */
	if (!pnp_port_valid(pdev, io_rsrc_no) ||
	    pnp_port_len(pdev, io_rsrc_no) != dev_desc->io_region_size) {
		dev_err(&pdev->dev, "IR PNP Port not valid!\n");
		goto exit_free_dev_rdev;
	}

	if (!pnp_irq_valid(pdev, 0)) {
		dev_err(&pdev->dev, "PNP IRQ not valid!\n");
		goto exit_free_dev_rdev;
	}

	/* store resource values */
	itdev->cir_addr = pnp_port_start(pdev, io_rsrc_no);
	itdev->cir_irq = pnp_irq(pdev, 0);

	/* initialize spinlocks */
	spin_lock_init(&itdev->lock);

	/* initialize raw event */
	init_ir_raw_event(&itdev->rawir);

	/* set driver data into the pnp device */
	pnp_set_drvdata(pdev, itdev);
	itdev->pdev = pdev;

	/* initialize waitqueues for transmission */
	init_waitqueue_head(&itdev->tx_queue);
	init_waitqueue_head(&itdev->tx_ended);

	/* copy model-specific parameters */
	itdev->params = *dev_desc;

	/* apply any overrides */
	if (sample_period > 0)
		itdev->params.sample_period = sample_period;

	if (tx_carrier_freq > 0)
		itdev->params.tx_carrier_freq = tx_carrier_freq;

	if (tx_duty_cycle > 0 && tx_duty_cycle <= 100)
		itdev->params.tx_duty_cycle = tx_duty_cycle;

	if (rx_low_carrier_freq > 0)
		itdev->params.rx_low_carrier_freq = rx_low_carrier_freq;

	if (rx_high_carrier_freq > 0)
		itdev->params.rx_high_carrier_freq = rx_high_carrier_freq;

	/* print out parameters */
	ite_pr(KERN_NOTICE, "TX-capable: %d\n", (int)
			 itdev->params.hw_tx_capable);
	ite_pr(KERN_NOTICE, "Sample period (ns): %ld\n", (long)
		     itdev->params.sample_period);
	ite_pr(KERN_NOTICE, "TX carrier frequency (Hz): %d\n", (int)
		     itdev->params.tx_carrier_freq);
	ite_pr(KERN_NOTICE, "TX duty cycle (%%): %d\n", (int)
		     itdev->params.tx_duty_cycle);
	ite_pr(KERN_NOTICE, "RX low carrier frequency (Hz): %d\n", (int)
		     itdev->params.rx_low_carrier_freq);
	ite_pr(KERN_NOTICE, "RX high carrier frequency (Hz): %d\n", (int)
		     itdev->params.rx_high_carrier_freq);

	/* set up hardware initial state */
	itdev->params.init_hardware(itdev);

	/* set up ir-core props */
	rdev->priv = itdev;
	rdev->driver_type = RC_DRIVER_IR_RAW;
	rdev->allowed_protocols = RC_BIT_ALL;
	rdev->open = ite_open;
	rdev->close = ite_close;
	rdev->s_idle = ite_s_idle;
	rdev->s_rx_carrier_range = ite_set_rx_carrier_range;
	rdev->min_timeout = ITE_MIN_IDLE_TIMEOUT;
	rdev->max_timeout = ITE_MAX_IDLE_TIMEOUT;
	rdev->timeout = ITE_IDLE_TIMEOUT;
	rdev->rx_resolution = ITE_BAUDRATE_DIVISOR *
				itdev->params.sample_period;
	rdev->tx_resolution = ITE_BAUDRATE_DIVISOR *
				itdev->params.sample_period;

	/* set up transmitter related values if needed */
	if (itdev->params.hw_tx_capable) {
		rdev->tx_ir = ite_tx_ir;
		rdev->s_tx_carrier = ite_set_tx_carrier;
		rdev->s_tx_duty_cycle = ite_set_tx_duty_cycle;
	}

	rdev->input_name = dev_desc->model;
	rdev->input_id.bustype = BUS_HOST;
	rdev->input_id.vendor = PCI_VENDOR_ID_ITE;
	rdev->input_id.product = 0;
	rdev->input_id.version = 0;
	rdev->driver_name = ITE_DRIVER_NAME;
	rdev->map_name = RC_MAP_RC6_MCE;

	ret = rc_register_device(rdev);
	if (ret)
		goto exit_free_dev_rdev;

	ret = -EBUSY;
	/* now claim resources */
	if (!request_region(itdev->cir_addr,
				dev_desc->io_region_size, ITE_DRIVER_NAME))
		goto exit_unregister_device;

	if (request_irq(itdev->cir_irq, ite_cir_isr, IRQF_SHARED,
			ITE_DRIVER_NAME, (void *)itdev))
		goto exit_release_cir_addr;

	ite_pr(KERN_NOTICE, "driver has been successfully loaded\n");

	return 0;

exit_release_cir_addr:
	release_region(itdev->cir_addr, itdev->params.io_region_size);
exit_unregister_device:
	rc_unregister_device(rdev);
	rdev = NULL;
exit_free_dev_rdev:
	rc_free_device(rdev);
	kfree(itdev);

	return ret;
}

static void ite_remove(struct pnp_dev *pdev)
{
	struct ite_dev *dev = pnp_get_drvdata(pdev);
	unsigned long flags;

	ite_dbg("%s called", __func__);

	spin_lock_irqsave(&dev->lock, flags);

	/* disable hardware */
	dev->params.disable(dev);

	spin_unlock_irqrestore(&dev->lock, flags);

	/* free resources */
	free_irq(dev->cir_irq, dev);
	release_region(dev->cir_addr, dev->params.io_region_size);

	rc_unregister_device(dev->rdev);

	kfree(dev);
}

static int ite_suspend(struct pnp_dev *pdev, pm_message_t state)
{
	struct ite_dev *dev = pnp_get_drvdata(pdev);
	unsigned long flags;

	ite_dbg("%s called", __func__);

	/* wait for any transmission to end */
	wait_event_interruptible(dev->tx_ended, !dev->transmitting);

	spin_lock_irqsave(&dev->lock, flags);

	/* disable all interrupts */
	dev->params.disable(dev);

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

static int ite_resume(struct pnp_dev *pdev)
{
	struct ite_dev *dev = pnp_get_drvdata(pdev);
	unsigned long flags;

	ite_dbg("%s called", __func__);

	spin_lock_irqsave(&dev->lock, flags);

	/* reinitialize hardware config registers */
	dev->params.init_hardware(dev);
	/* enable the receiver */
	dev->params.enable_rx(dev);

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

static void ite_shutdown(struct pnp_dev *pdev)
{
	struct ite_dev *dev = pnp_get_drvdata(pdev);
	unsigned long flags;

	ite_dbg("%s called", __func__);

	spin_lock_irqsave(&dev->lock, flags);

	/* disable all interrupts */
	dev->params.disable(dev);

	spin_unlock_irqrestore(&dev->lock, flags);
}

static struct pnp_driver ite_driver = {
	.name		= ITE_DRIVER_NAME,
	.id_table	= ite_ids,
	.probe		= ite_probe,
	.remove		= ite_remove,
	.suspend	= ite_suspend,
	.resume		= ite_resume,
	.shutdown	= ite_shutdown,
};

static int __init ite_init(void)
{
	return pnp_register_driver(&ite_driver);
}

static void __exit ite_exit(void)
{
	pnp_unregister_driver(&ite_driver);
}

MODULE_DEVICE_TABLE(pnp, ite_ids);
MODULE_DESCRIPTION("ITE Tech Inc. IT8712F/ITE8512F CIR driver");

MODULE_AUTHOR("Juan J. Garcia de Soria <skandalfo@gmail.com>");
MODULE_LICENSE("GPL");

module_init(ite_init);
module_exit(ite_exit);
