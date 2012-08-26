/*
 *  Driver for AMBA serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *  Copyright (C) 2010 ST-Ericsson SA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This is a generic driver for ARM AMBA-type serial ports.  They
 * have a lot of 16550-like features, but are not register compatible.
 * Note that although they do have CTS, DCD and DSR inputs, they do
 * not have an RI input, nor do they have DTR or RTS outputs.  If
 * required, these have to be supplied via some other means (eg, GPIO)
 * and hooked into this driver.
 */

#if defined(CONFIG_SERIAL_AMBA_PL011_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sizes.h>

#include <asm/io.h>

#define UART_NR			14

#define SERIAL_AMBA_MAJOR	204
#define SERIAL_AMBA_MINOR	64
#define SERIAL_AMBA_NR		UART_NR

#define AMBA_ISR_PASS_LIMIT	256

#define UART_DR_ERROR		(UART011_DR_OE|UART011_DR_BE|UART011_DR_PE|UART011_DR_FE)
#define UART_DUMMY_DR_RX	(1 << 16)

/* There is by now at least one vendor with differing details, so handle it */
struct vendor_data {
	unsigned int		ifls;
	unsigned int		fifosize;
	unsigned int		lcrh_tx;
	unsigned int		lcrh_rx;
	bool			oversampling;
	bool			interrupt_may_hang;   /* vendor-specific */
	bool			dma_threshold;
	bool			cts_event_workaround;
};

static struct vendor_data vendor_arm = {
	.ifls			= UART011_IFLS_RX4_8|UART011_IFLS_TX4_8,
	.fifosize		= 16,
	.lcrh_tx		= UART011_LCRH,
	.lcrh_rx		= UART011_LCRH,
	.oversampling		= false,
	.dma_threshold		= false,
	.cts_event_workaround	= false,
};

static struct vendor_data vendor_st = {
	.ifls			= UART011_IFLS_RX_HALF|UART011_IFLS_TX_HALF,
	.fifosize		= 64,
	.lcrh_tx		= ST_UART011_LCRH_TX,
	.lcrh_rx		= ST_UART011_LCRH_RX,
	.oversampling		= true,
	.interrupt_may_hang	= true,
	.dma_threshold		= true,
	.cts_event_workaround	= true,
};

static struct uart_amba_port *amba_ports[UART_NR];

/* Deals with DMA transactions */

struct pl011_sgbuf {
	struct scatterlist sg;
	char *buf;
};

struct pl011_dmarx_data {
	struct dma_chan		*chan;
	struct completion	complete;
	bool			use_buf_b;
	struct pl011_sgbuf	sgbuf_a;
	struct pl011_sgbuf	sgbuf_b;
	dma_cookie_t		cookie;
	bool			running;
};

struct pl011_dmatx_data {
	struct dma_chan		*chan;
	struct scatterlist	sg;
	char			*buf;
	bool			queued;
};

/*
 * We wrap our port structure around the generic uart_port.
 */
struct uart_amba_port {
	struct uart_port	port;
	struct clk		*clk;
	/* Two optional pin states - default & sleep */
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;
	const struct vendor_data *vendor;
	unsigned int		dmacr;		/* dma control reg */
	unsigned int		im;		/* interrupt mask */
	unsigned int		old_status;
	unsigned int		fifosize;	/* vendor-specific */
	unsigned int		lcrh_tx;	/* vendor-specific */
	unsigned int		lcrh_rx;	/* vendor-specific */
	unsigned int		old_cr;		/* state during shutdown */
	bool			autorts;
	char			type[12];
	bool			interrupt_may_hang; /* vendor-specific */
#ifdef CONFIG_DMA_ENGINE
	/* DMA stuff */
	bool			using_tx_dma;
	bool			using_rx_dma;
	struct pl011_dmarx_data dmarx;
	struct pl011_dmatx_data	dmatx;
#endif
};

/*
 * Reads up to 256 characters from the FIFO or until it's empty and
 * inserts them into the TTY layer. Returns the number of characters
 * read from the FIFO.
 */
static int pl011_fifo_to_tty(struct uart_amba_port *uap)
{
	u16 status, ch;
	unsigned int flag, max_count = 256;
	int fifotaken = 0;

	while (max_count--) {
		status = readw(uap->port.membase + UART01x_FR);
		if (status & UART01x_FR_RXFE)
			break;

		/* Take chars from the FIFO and update status */
		ch = readw(uap->port.membase + UART01x_DR) |
			UART_DUMMY_DR_RX;
		flag = TTY_NORMAL;
		uap->port.icount.rx++;
		fifotaken++;

		if (unlikely(ch & UART_DR_ERROR)) {
			if (ch & UART011_DR_BE) {
				ch &= ~(UART011_DR_FE | UART011_DR_PE);
				uap->port.icount.brk++;
				if (uart_handle_break(&uap->port))
					continue;
			} else if (ch & UART011_DR_PE)
				uap->port.icount.parity++;
			else if (ch & UART011_DR_FE)
				uap->port.icount.frame++;
			if (ch & UART011_DR_OE)
				uap->port.icount.overrun++;

			ch &= uap->port.read_status_mask;

			if (ch & UART011_DR_BE)
				flag = TTY_BREAK;
			else if (ch & UART011_DR_PE)
				flag = TTY_PARITY;
			else if (ch & UART011_DR_FE)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(&uap->port, ch & 255))
			continue;

		uart_insert_char(&uap->port, ch, UART011_DR_OE, ch, flag);
	}

	return fifotaken;
}


/*
 * All the DMA operation mode stuff goes inside this ifdef.
 * This assumes that you have a generic DMA device interface,
 * no custom DMA interfaces are supported.
 */
#ifdef CONFIG_DMA_ENGINE

#define PL011_DMA_BUFFER_SIZE PAGE_SIZE

static int pl011_sgbuf_init(struct dma_chan *chan, struct pl011_sgbuf *sg,
	enum dma_data_direction dir)
{
	sg->buf = kmalloc(PL011_DMA_BUFFER_SIZE, GFP_KERNEL);
	if (!sg->buf)
		return -ENOMEM;

	sg_init_one(&sg->sg, sg->buf, PL011_DMA_BUFFER_SIZE);

	if (dma_map_sg(chan->device->dev, &sg->sg, 1, dir) != 1) {
		kfree(sg->buf);
		return -EINVAL;
	}
	return 0;
}

static void pl011_sgbuf_free(struct dma_chan *chan, struct pl011_sgbuf *sg,
	enum dma_data_direction dir)
{
	if (sg->buf) {
		dma_unmap_sg(chan->device->dev, &sg->sg, 1, dir);
		kfree(sg->buf);
	}
}

static void pl011_dma_probe_initcall(struct uart_amba_port *uap)
{
	/* DMA is the sole user of the platform data right now */
	struct amba_pl011_data *plat = uap->port.dev->platform_data;
	struct dma_slave_config tx_conf = {
		.dst_addr = uap->port.mapbase + UART01x_DR,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.direction = DMA_MEM_TO_DEV,
		.dst_maxburst = uap->fifosize >> 1,
		.device_fc = false,
	};
	struct dma_chan *chan;
	dma_cap_mask_t mask;

	/* We need platform data */
	if (!plat || !plat->dma_filter) {
		dev_info(uap->port.dev, "no DMA platform data\n");
		return;
	}

	/* Try to acquire a generic DMA engine slave TX channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	chan = dma_request_channel(mask, plat->dma_filter, plat->dma_tx_param);
	if (!chan) {
		dev_err(uap->port.dev, "no TX DMA channel!\n");
		return;
	}

	dmaengine_slave_config(chan, &tx_conf);
	uap->dmatx.chan = chan;

	dev_info(uap->port.dev, "DMA channel TX %s\n",
		 dma_chan_name(uap->dmatx.chan));

	/* Optionally make use of an RX channel as well */
	if (plat->dma_rx_param) {
		struct dma_slave_config rx_conf = {
			.src_addr = uap->port.mapbase + UART01x_DR,
			.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
			.direction = DMA_DEV_TO_MEM,
			.src_maxburst = uap->fifosize >> 1,
			.device_fc = false,
		};

		chan = dma_request_channel(mask, plat->dma_filter, plat->dma_rx_param);
		if (!chan) {
			dev_err(uap->port.dev, "no RX DMA channel!\n");
			return;
		}

		dmaengine_slave_config(chan, &rx_conf);
		uap->dmarx.chan = chan;

		dev_info(uap->port.dev, "DMA channel RX %s\n",
			 dma_chan_name(uap->dmarx.chan));
	}
}

#ifndef MODULE
/*
 * Stack up the UARTs and let the above initcall be done at device
 * initcall time, because the serial driver is called as an arch
 * initcall, and at this time the DMA subsystem is not yet registered.
 * At this point the driver will switch over to using DMA where desired.
 */
struct dma_uap {
	struct list_head node;
	struct uart_amba_port *uap;
};

static LIST_HEAD(pl011_dma_uarts);

static int __init pl011_dma_initcall(void)
{
	struct list_head *node, *tmp;

	list_for_each_safe(node, tmp, &pl011_dma_uarts) {
		struct dma_uap *dmau = list_entry(node, struct dma_uap, node);
		pl011_dma_probe_initcall(dmau->uap);
		list_del(node);
		kfree(dmau);
	}
	return 0;
}

device_initcall(pl011_dma_initcall);

static void pl011_dma_probe(struct uart_amba_port *uap)
{
	struct dma_uap *dmau = kzalloc(sizeof(struct dma_uap), GFP_KERNEL);
	if (dmau) {
		dmau->uap = uap;
		list_add_tail(&dmau->node, &pl011_dma_uarts);
	}
}
#else
static void pl011_dma_probe(struct uart_amba_port *uap)
{
	pl011_dma_probe_initcall(uap);
}
#endif

static void pl011_dma_remove(struct uart_amba_port *uap)
{
	/* TODO: remove the initcall if it has not yet executed */
	if (uap->dmatx.chan)
		dma_release_channel(uap->dmatx.chan);
	if (uap->dmarx.chan)
		dma_release_channel(uap->dmarx.chan);
}

/* Forward declare this for the refill routine */
static int pl011_dma_tx_refill(struct uart_amba_port *uap);

/*
 * The current DMA TX buffer has been sent.
 * Try to queue up another DMA buffer.
 */
static void pl011_dma_tx_callback(void *data)
{
	struct uart_amba_port *uap = data;
	struct pl011_dmatx_data *dmatx = &uap->dmatx;
	unsigned long flags;
	u16 dmacr;

	spin_lock_irqsave(&uap->port.lock, flags);
	if (uap->dmatx.queued)
		dma_unmap_sg(dmatx->chan->device->dev, &dmatx->sg, 1,
			     DMA_TO_DEVICE);

	dmacr = uap->dmacr;
	uap->dmacr = dmacr & ~UART011_TXDMAE;
	writew(uap->dmacr, uap->port.membase + UART011_DMACR);

	/*
	 * If TX DMA was disabled, it means that we've stopped the DMA for
	 * some reason (eg, XOFF received, or we want to send an X-char.)
	 *
	 * Note: we need to be careful here of a potential race between DMA
	 * and the rest of the driver - if the driver disables TX DMA while
	 * a TX buffer completing, we must update the tx queued status to
	 * get further refills (hence we check dmacr).
	 */
	if (!(dmacr & UART011_TXDMAE) || uart_tx_stopped(&uap->port) ||
	    uart_circ_empty(&uap->port.state->xmit)) {
		uap->dmatx.queued = false;
		spin_unlock_irqrestore(&uap->port.lock, flags);
		return;
	}

	if (pl011_dma_tx_refill(uap) <= 0) {
		/*
		 * We didn't queue a DMA buffer for some reason, but we
		 * have data pending to be sent.  Re-enable the TX IRQ.
		 */
		uap->im |= UART011_TXIM;
		writew(uap->im, uap->port.membase + UART011_IMSC);
	}
	spin_unlock_irqrestore(&uap->port.lock, flags);
}

/*
 * Try to refill the TX DMA buffer.
 * Locking: called with port lock held and IRQs disabled.
 * Returns:
 *   1 if we queued up a TX DMA buffer.
 *   0 if we didn't want to handle this by DMA
 *  <0 on error
 */
static int pl011_dma_tx_refill(struct uart_amba_port *uap)
{
	struct pl011_dmatx_data *dmatx = &uap->dmatx;
	struct dma_chan *chan = dmatx->chan;
	struct dma_device *dma_dev = chan->device;
	struct dma_async_tx_descriptor *desc;
	struct circ_buf *xmit = &uap->port.state->xmit;
	unsigned int count;

	/*
	 * Try to avoid the overhead involved in using DMA if the
	 * transaction fits in the first half of the FIFO, by using
	 * the standard interrupt handling.  This ensures that we
	 * issue a uart_write_wakeup() at the appropriate time.
	 */
	count = uart_circ_chars_pending(xmit);
	if (count < (uap->fifosize >> 1)) {
		uap->dmatx.queued = false;
		return 0;
	}

	/*
	 * Bodge: don't send the last character by DMA, as this
	 * will prevent XON from notifying us to restart DMA.
	 */
	count -= 1;

	/* Else proceed to copy the TX chars to the DMA buffer and fire DMA */
	if (count > PL011_DMA_BUFFER_SIZE)
		count = PL011_DMA_BUFFER_SIZE;

	if (xmit->tail < xmit->head)
		memcpy(&dmatx->buf[0], &xmit->buf[xmit->tail], count);
	else {
		size_t first = UART_XMIT_SIZE - xmit->tail;
		size_t second = xmit->head;

		memcpy(&dmatx->buf[0], &xmit->buf[xmit->tail], first);
		if (second)
			memcpy(&dmatx->buf[first], &xmit->buf[0], second);
	}

	dmatx->sg.length = count;

	if (dma_map_sg(dma_dev->dev, &dmatx->sg, 1, DMA_TO_DEVICE) != 1) {
		uap->dmatx.queued = false;
		dev_dbg(uap->port.dev, "unable to map TX DMA\n");
		return -EBUSY;
	}

	desc = dmaengine_prep_slave_sg(chan, &dmatx->sg, 1, DMA_MEM_TO_DEV,
					     DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dma_unmap_sg(dma_dev->dev, &dmatx->sg, 1, DMA_TO_DEVICE);
		uap->dmatx.queued = false;
		/*
		 * If DMA cannot be used right now, we complete this
		 * transaction via IRQ and let the TTY layer retry.
		 */
		dev_dbg(uap->port.dev, "TX DMA busy\n");
		return -EBUSY;
	}

	/* Some data to go along to the callback */
	desc->callback = pl011_dma_tx_callback;
	desc->callback_param = uap;

	/* All errors should happen at prepare time */
	dmaengine_submit(desc);

	/* Fire the DMA transaction */
	dma_dev->device_issue_pending(chan);

	uap->dmacr |= UART011_TXDMAE;
	writew(uap->dmacr, uap->port.membase + UART011_DMACR);
	uap->dmatx.queued = true;

	/*
	 * Now we know that DMA will fire, so advance the ring buffer
	 * with the stuff we just dispatched.
	 */
	xmit->tail = (xmit->tail + count) & (UART_XMIT_SIZE - 1);
	uap->port.icount.tx += count;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&uap->port);

	return 1;
}

/*
 * We received a transmit interrupt without a pending X-char but with
 * pending characters.
 * Locking: called with port lock held and IRQs disabled.
 * Returns:
 *   false if we want to use PIO to transmit
 *   true if we queued a DMA buffer
 */
static bool pl011_dma_tx_irq(struct uart_amba_port *uap)
{
	if (!uap->using_tx_dma)
		return false;

	/*
	 * If we already have a TX buffer queued, but received a
	 * TX interrupt, it will be because we've just sent an X-char.
	 * Ensure the TX DMA is enabled and the TX IRQ is disabled.
	 */
	if (uap->dmatx.queued) {
		uap->dmacr |= UART011_TXDMAE;
		writew(uap->dmacr, uap->port.membase + UART011_DMACR);
		uap->im &= ~UART011_TXIM;
		writew(uap->im, uap->port.membase + UART011_IMSC);
		return true;
	}

	/*
	 * We don't have a TX buffer queued, so try to queue one.
	 * If we successfully queued a buffer, mask the TX IRQ.
	 */
	if (pl011_dma_tx_refill(uap) > 0) {
		uap->im &= ~UART011_TXIM;
		writew(uap->im, uap->port.membase + UART011_IMSC);
		return true;
	}
	return false;
}

/*
 * Stop the DMA transmit (eg, due to received XOFF).
 * Locking: called with port lock held and IRQs disabled.
 */
static inline void pl011_dma_tx_stop(struct uart_amba_port *uap)
{
	if (uap->dmatx.queued) {
		uap->dmacr &= ~UART011_TXDMAE;
		writew(uap->dmacr, uap->port.membase + UART011_DMACR);
	}
}

/*
 * Try to start a DMA transmit, or in the case of an XON/OFF
 * character queued for send, try to get that character out ASAP.
 * Locking: called with port lock held and IRQs disabled.
 * Returns:
 *   false if we want the TX IRQ to be enabled
 *   true if we have a buffer queued
 */
static inline bool pl011_dma_tx_start(struct uart_amba_port *uap)
{
	u16 dmacr;

	if (!uap->using_tx_dma)
		return false;

	if (!uap->port.x_char) {
		/* no X-char, try to push chars out in DMA mode */
		bool ret = true;

		if (!uap->dmatx.queued) {
			if (pl011_dma_tx_refill(uap) > 0) {
				uap->im &= ~UART011_TXIM;
				ret = true;
			} else {
				uap->im |= UART011_TXIM;
				ret = false;
			}
			writew(uap->im, uap->port.membase + UART011_IMSC);
		} else if (!(uap->dmacr & UART011_TXDMAE)) {
			uap->dmacr |= UART011_TXDMAE;
			writew(uap->dmacr,
				       uap->port.membase + UART011_DMACR);
		}
		return ret;
	}

	/*
	 * We have an X-char to send.  Disable DMA to prevent it loading
	 * the TX fifo, and then see if we can stuff it into the FIFO.
	 */
	dmacr = uap->dmacr;
	uap->dmacr &= ~UART011_TXDMAE;
	writew(uap->dmacr, uap->port.membase + UART011_DMACR);

	if (readw(uap->port.membase + UART01x_FR) & UART01x_FR_TXFF) {
		/*
		 * No space in the FIFO, so enable the transmit interrupt
		 * so we know when there is space.  Note that once we've
		 * loaded the character, we should just re-enable DMA.
		 */
		return false;
	}

	writew(uap->port.x_char, uap->port.membase + UART01x_DR);
	uap->port.icount.tx++;
	uap->port.x_char = 0;

	/* Success - restore the DMA state */
	uap->dmacr = dmacr;
	writew(dmacr, uap->port.membase + UART011_DMACR);

	return true;
}

/*
 * Flush the transmit buffer.
 * Locking: called with port lock held and IRQs disabled.
 */
static void pl011_dma_flush_buffer(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;

	if (!uap->using_tx_dma)
		return;

	/* Avoid deadlock with the DMA engine callback */
	spin_unlock(&uap->port.lock);
	dmaengine_terminate_all(uap->dmatx.chan);
	spin_lock(&uap->port.lock);
	if (uap->dmatx.queued) {
		dma_unmap_sg(uap->dmatx.chan->device->dev, &uap->dmatx.sg, 1,
			     DMA_TO_DEVICE);
		uap->dmatx.queued = false;
		uap->dmacr &= ~UART011_TXDMAE;
		writew(uap->dmacr, uap->port.membase + UART011_DMACR);
	}
}

static void pl011_dma_rx_callback(void *data);

static int pl011_dma_rx_trigger_dma(struct uart_amba_port *uap)
{
	struct dma_chan *rxchan = uap->dmarx.chan;
	struct pl011_dmarx_data *dmarx = &uap->dmarx;
	struct dma_async_tx_descriptor *desc;
	struct pl011_sgbuf *sgbuf;

	if (!rxchan)
		return -EIO;

	/* Start the RX DMA job */
	sgbuf = uap->dmarx.use_buf_b ?
		&uap->dmarx.sgbuf_b : &uap->dmarx.sgbuf_a;
	desc = dmaengine_prep_slave_sg(rxchan, &sgbuf->sg, 1,
					DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	/*
	 * If the DMA engine is busy and cannot prepare a
	 * channel, no big deal, the driver will fall back
	 * to interrupt mode as a result of this error code.
	 */
	if (!desc) {
		uap->dmarx.running = false;
		dmaengine_terminate_all(rxchan);
		return -EBUSY;
	}

	/* Some data to go along to the callback */
	desc->callback = pl011_dma_rx_callback;
	desc->callback_param = uap;
	dmarx->cookie = dmaengine_submit(desc);
	dma_async_issue_pending(rxchan);

	uap->dmacr |= UART011_RXDMAE;
	writew(uap->dmacr, uap->port.membase + UART011_DMACR);
	uap->dmarx.running = true;

	uap->im &= ~UART011_RXIM;
	writew(uap->im, uap->port.membase + UART011_IMSC);

	return 0;
}

/*
 * This is called when either the DMA job is complete, or
 * the FIFO timeout interrupt occurred. This must be called
 * with the port spinlock uap->port.lock held.
 */
static void pl011_dma_rx_chars(struct uart_amba_port *uap,
			       u32 pending, bool use_buf_b,
			       bool readfifo)
{
	struct tty_struct *tty = uap->port.state->port.tty;
	struct pl011_sgbuf *sgbuf = use_buf_b ?
		&uap->dmarx.sgbuf_b : &uap->dmarx.sgbuf_a;
	struct device *dev = uap->dmarx.chan->device->dev;
	int dma_count = 0;
	u32 fifotaken = 0; /* only used for vdbg() */

	/* Pick everything from the DMA first */
	if (pending) {
		/* Sync in buffer */
		dma_sync_sg_for_cpu(dev, &sgbuf->sg, 1, DMA_FROM_DEVICE);

		/*
		 * First take all chars in the DMA pipe, then look in the FIFO.
		 * Note that tty_insert_flip_buf() tries to take as many chars
		 * as it can.
		 */
		dma_count = tty_insert_flip_string(uap->port.state->port.tty,
						   sgbuf->buf, pending);

		/* Return buffer to device */
		dma_sync_sg_for_device(dev, &sgbuf->sg, 1, DMA_FROM_DEVICE);

		uap->port.icount.rx += dma_count;
		if (dma_count < pending)
			dev_warn(uap->port.dev,
				 "couldn't insert all characters (TTY is full?)\n");
	}

	/*
	 * Only continue with trying to read the FIFO if all DMA chars have
	 * been taken first.
	 */
	if (dma_count == pending && readfifo) {
		/* Clear any error flags */
		writew(UART011_OEIS | UART011_BEIS | UART011_PEIS | UART011_FEIS,
		       uap->port.membase + UART011_ICR);

		/*
		 * If we read all the DMA'd characters, and we had an
		 * incomplete buffer, that could be due to an rx error, or
		 * maybe we just timed out. Read any pending chars and check
		 * the error status.
		 *
		 * Error conditions will only occur in the FIFO, these will
		 * trigger an immediate interrupt and stop the DMA job, so we
		 * will always find the error in the FIFO, never in the DMA
		 * buffer.
		 */
		fifotaken = pl011_fifo_to_tty(uap);
	}

	spin_unlock(&uap->port.lock);
	dev_vdbg(uap->port.dev,
		 "Took %d chars from DMA buffer and %d chars from the FIFO\n",
		 dma_count, fifotaken);
	tty_flip_buffer_push(tty);
	spin_lock(&uap->port.lock);
}

static void pl011_dma_rx_irq(struct uart_amba_port *uap)
{
	struct pl011_dmarx_data *dmarx = &uap->dmarx;
	struct dma_chan *rxchan = dmarx->chan;
	struct pl011_sgbuf *sgbuf = dmarx->use_buf_b ?
		&dmarx->sgbuf_b : &dmarx->sgbuf_a;
	size_t pending;
	struct dma_tx_state state;
	enum dma_status dmastat;

	/*
	 * Pause the transfer so we can trust the current counter,
	 * do this before we pause the PL011 block, else we may
	 * overflow the FIFO.
	 */
	if (dmaengine_pause(rxchan))
		dev_err(uap->port.dev, "unable to pause DMA transfer\n");
	dmastat = rxchan->device->device_tx_status(rxchan,
						   dmarx->cookie, &state);
	if (dmastat != DMA_PAUSED)
		dev_err(uap->port.dev, "unable to pause DMA transfer\n");

	/* Disable RX DMA - incoming data will wait in the FIFO */
	uap->dmacr &= ~UART011_RXDMAE;
	writew(uap->dmacr, uap->port.membase + UART011_DMACR);
	uap->dmarx.running = false;

	pending = sgbuf->sg.length - state.residue;
	BUG_ON(pending > PL011_DMA_BUFFER_SIZE);
	/* Then we terminate the transfer - we now know our residue */
	dmaengine_terminate_all(rxchan);

	/*
	 * This will take the chars we have so far and insert
	 * into the framework.
	 */
	pl011_dma_rx_chars(uap, pending, dmarx->use_buf_b, true);

	/* Switch buffer & re-trigger DMA job */
	dmarx->use_buf_b = !dmarx->use_buf_b;
	if (pl011_dma_rx_trigger_dma(uap)) {
		dev_dbg(uap->port.dev, "could not retrigger RX DMA job "
			"fall back to interrupt mode\n");
		uap->im |= UART011_RXIM;
		writew(uap->im, uap->port.membase + UART011_IMSC);
	}
}

static void pl011_dma_rx_callback(void *data)
{
	struct uart_amba_port *uap = data;
	struct pl011_dmarx_data *dmarx = &uap->dmarx;
	struct dma_chan *rxchan = dmarx->chan;
	bool lastbuf = dmarx->use_buf_b;
	struct pl011_sgbuf *sgbuf = dmarx->use_buf_b ?
		&dmarx->sgbuf_b : &dmarx->sgbuf_a;
	size_t pending;
	struct dma_tx_state state;
	int ret;

	/*
	 * This completion interrupt occurs typically when the
	 * RX buffer is totally stuffed but no timeout has yet
	 * occurred. When that happens, we just want the RX
	 * routine to flush out the secondary DMA buffer while
	 * we immediately trigger the next DMA job.
	 */
	spin_lock_irq(&uap->port.lock);
	/*
	 * Rx data can be taken by the UART interrupts during
	 * the DMA irq handler. So we check the residue here.
	 */
	rxchan->device->device_tx_status(rxchan, dmarx->cookie, &state);
	pending = sgbuf->sg.length - state.residue;
	BUG_ON(pending > PL011_DMA_BUFFER_SIZE);
	/* Then we terminate the transfer - we now know our residue */
	dmaengine_terminate_all(rxchan);

	uap->dmarx.running = false;
	dmarx->use_buf_b = !lastbuf;
	ret = pl011_dma_rx_trigger_dma(uap);

	pl011_dma_rx_chars(uap, pending, lastbuf, false);
	spin_unlock_irq(&uap->port.lock);
	/*
	 * Do this check after we picked the DMA chars so we don't
	 * get some IRQ immediately from RX.
	 */
	if (ret) {
		dev_dbg(uap->port.dev, "could not retrigger RX DMA job "
			"fall back to interrupt mode\n");
		uap->im |= UART011_RXIM;
		writew(uap->im, uap->port.membase + UART011_IMSC);
	}
}

/*
 * Stop accepting received characters, when we're shutting down or
 * suspending this port.
 * Locking: called with port lock held and IRQs disabled.
 */
static inline void pl011_dma_rx_stop(struct uart_amba_port *uap)
{
	/* FIXME.  Just disable the DMA enable */
	uap->dmacr &= ~UART011_RXDMAE;
	writew(uap->dmacr, uap->port.membase + UART011_DMACR);
}

static void pl011_dma_startup(struct uart_amba_port *uap)
{
	int ret;

	if (!uap->dmatx.chan)
		return;

	uap->dmatx.buf = kmalloc(PL011_DMA_BUFFER_SIZE, GFP_KERNEL);
	if (!uap->dmatx.buf) {
		dev_err(uap->port.dev, "no memory for DMA TX buffer\n");
		uap->port.fifosize = uap->fifosize;
		return;
	}

	sg_init_one(&uap->dmatx.sg, uap->dmatx.buf, PL011_DMA_BUFFER_SIZE);

	/* The DMA buffer is now the FIFO the TTY subsystem can use */
	uap->port.fifosize = PL011_DMA_BUFFER_SIZE;
	uap->using_tx_dma = true;

	if (!uap->dmarx.chan)
		goto skip_rx;

	/* Allocate and map DMA RX buffers */
	ret = pl011_sgbuf_init(uap->dmarx.chan, &uap->dmarx.sgbuf_a,
			       DMA_FROM_DEVICE);
	if (ret) {
		dev_err(uap->port.dev, "failed to init DMA %s: %d\n",
			"RX buffer A", ret);
		goto skip_rx;
	}

	ret = pl011_sgbuf_init(uap->dmarx.chan, &uap->dmarx.sgbuf_b,
			       DMA_FROM_DEVICE);
	if (ret) {
		dev_err(uap->port.dev, "failed to init DMA %s: %d\n",
			"RX buffer B", ret);
		pl011_sgbuf_free(uap->dmarx.chan, &uap->dmarx.sgbuf_a,
				 DMA_FROM_DEVICE);
		goto skip_rx;
	}

	uap->using_rx_dma = true;

skip_rx:
	/* Turn on DMA error (RX/TX will be enabled on demand) */
	uap->dmacr |= UART011_DMAONERR;
	writew(uap->dmacr, uap->port.membase + UART011_DMACR);

	/*
	 * ST Micro variants has some specific dma burst threshold
	 * compensation. Set this to 16 bytes, so burst will only
	 * be issued above/below 16 bytes.
	 */
	if (uap->vendor->dma_threshold)
		writew(ST_UART011_DMAWM_RX_16 | ST_UART011_DMAWM_TX_16,
			       uap->port.membase + ST_UART011_DMAWM);

	if (uap->using_rx_dma) {
		if (pl011_dma_rx_trigger_dma(uap))
			dev_dbg(uap->port.dev, "could not trigger initial "
				"RX DMA job, fall back to interrupt mode\n");
	}
}

static void pl011_dma_shutdown(struct uart_amba_port *uap)
{
	if (!(uap->using_tx_dma || uap->using_rx_dma))
		return;

	/* Disable RX and TX DMA */
	while (readw(uap->port.membase + UART01x_FR) & UART01x_FR_BUSY)
		barrier();

	spin_lock_irq(&uap->port.lock);
	uap->dmacr &= ~(UART011_DMAONERR | UART011_RXDMAE | UART011_TXDMAE);
	writew(uap->dmacr, uap->port.membase + UART011_DMACR);
	spin_unlock_irq(&uap->port.lock);

	if (uap->using_tx_dma) {
		/* In theory, this should already be done by pl011_dma_flush_buffer */
		dmaengine_terminate_all(uap->dmatx.chan);
		if (uap->dmatx.queued) {
			dma_unmap_sg(uap->dmatx.chan->device->dev, &uap->dmatx.sg, 1,
				     DMA_TO_DEVICE);
			uap->dmatx.queued = false;
		}

		kfree(uap->dmatx.buf);
		uap->using_tx_dma = false;
	}

	if (uap->using_rx_dma) {
		dmaengine_terminate_all(uap->dmarx.chan);
		/* Clean up the RX DMA */
		pl011_sgbuf_free(uap->dmarx.chan, &uap->dmarx.sgbuf_a, DMA_FROM_DEVICE);
		pl011_sgbuf_free(uap->dmarx.chan, &uap->dmarx.sgbuf_b, DMA_FROM_DEVICE);
		uap->using_rx_dma = false;
	}
}

static inline bool pl011_dma_rx_available(struct uart_amba_port *uap)
{
	return uap->using_rx_dma;
}

static inline bool pl011_dma_rx_running(struct uart_amba_port *uap)
{
	return uap->using_rx_dma && uap->dmarx.running;
}


#else
/* Blank functions if the DMA engine is not available */
static inline void pl011_dma_probe(struct uart_amba_port *uap)
{
}

static inline void pl011_dma_remove(struct uart_amba_port *uap)
{
}

static inline void pl011_dma_startup(struct uart_amba_port *uap)
{
}

static inline void pl011_dma_shutdown(struct uart_amba_port *uap)
{
}

static inline bool pl011_dma_tx_irq(struct uart_amba_port *uap)
{
	return false;
}

static inline void pl011_dma_tx_stop(struct uart_amba_port *uap)
{
}

static inline bool pl011_dma_tx_start(struct uart_amba_port *uap)
{
	return false;
}

static inline void pl011_dma_rx_irq(struct uart_amba_port *uap)
{
}

static inline void pl011_dma_rx_stop(struct uart_amba_port *uap)
{
}

static inline int pl011_dma_rx_trigger_dma(struct uart_amba_port *uap)
{
	return -EIO;
}

static inline bool pl011_dma_rx_available(struct uart_amba_port *uap)
{
	return false;
}

static inline bool pl011_dma_rx_running(struct uart_amba_port *uap)
{
	return false;
}

#define pl011_dma_flush_buffer	NULL
#endif

static void pl011_stop_tx(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;

	uap->im &= ~UART011_TXIM;
	writew(uap->im, uap->port.membase + UART011_IMSC);
	pl011_dma_tx_stop(uap);
}

static void pl011_start_tx(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;

	if (!pl011_dma_tx_start(uap)) {
		uap->im |= UART011_TXIM;
		writew(uap->im, uap->port.membase + UART011_IMSC);
	}
}

static void pl011_stop_rx(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;

	uap->im &= ~(UART011_RXIM|UART011_RTIM|UART011_FEIM|
		     UART011_PEIM|UART011_BEIM|UART011_OEIM);
	writew(uap->im, uap->port.membase + UART011_IMSC);

	pl011_dma_rx_stop(uap);
}

static void pl011_enable_ms(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;

	uap->im |= UART011_RIMIM|UART011_CTSMIM|UART011_DCDMIM|UART011_DSRMIM;
	writew(uap->im, uap->port.membase + UART011_IMSC);
}

static void pl011_rx_chars(struct uart_amba_port *uap)
{
	struct tty_struct *tty = uap->port.state->port.tty;

	pl011_fifo_to_tty(uap);

	spin_unlock(&uap->port.lock);
	tty_flip_buffer_push(tty);
	/*
	 * If we were temporarily out of DMA mode for a while,
	 * attempt to switch back to DMA mode again.
	 */
	if (pl011_dma_rx_available(uap)) {
		if (pl011_dma_rx_trigger_dma(uap)) {
			dev_dbg(uap->port.dev, "could not trigger RX DMA job "
				"fall back to interrupt mode again\n");
			uap->im |= UART011_RXIM;
		} else
			uap->im &= ~UART011_RXIM;
		writew(uap->im, uap->port.membase + UART011_IMSC);
	}
	spin_lock(&uap->port.lock);
}

static void pl011_tx_chars(struct uart_amba_port *uap)
{
	struct circ_buf *xmit = &uap->port.state->xmit;
	int count;

	if (uap->port.x_char) {
		writew(uap->port.x_char, uap->port.membase + UART01x_DR);
		uap->port.icount.tx++;
		uap->port.x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&uap->port)) {
		pl011_stop_tx(&uap->port);
		return;
	}

	/* If we are using DMA mode, try to send some characters. */
	if (pl011_dma_tx_irq(uap))
		return;

	count = uap->fifosize >> 1;
	do {
		writew(xmit->buf[xmit->tail], uap->port.membase + UART01x_DR);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		uap->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&uap->port);

	if (uart_circ_empty(xmit))
		pl011_stop_tx(&uap->port);
}

static void pl011_modem_status(struct uart_amba_port *uap)
{
	unsigned int status, delta;

	status = readw(uap->port.membase + UART01x_FR) & UART01x_FR_MODEM_ANY;

	delta = status ^ uap->old_status;
	uap->old_status = status;

	if (!delta)
		return;

	if (delta & UART01x_FR_DCD)
		uart_handle_dcd_change(&uap->port, status & UART01x_FR_DCD);

	if (delta & UART01x_FR_DSR)
		uap->port.icount.dsr++;

	if (delta & UART01x_FR_CTS)
		uart_handle_cts_change(&uap->port, status & UART01x_FR_CTS);

	wake_up_interruptible(&uap->port.state->port.delta_msr_wait);
}

static irqreturn_t pl011_int(int irq, void *dev_id)
{
	struct uart_amba_port *uap = dev_id;
	unsigned long flags;
	unsigned int status, pass_counter = AMBA_ISR_PASS_LIMIT;
	int handled = 0;
	unsigned int dummy_read;

	spin_lock_irqsave(&uap->port.lock, flags);

	status = readw(uap->port.membase + UART011_MIS);
	if (status) {
		do {
			if (uap->vendor->cts_event_workaround) {
				/* workaround to make sure that all bits are unlocked.. */
				writew(0x00, uap->port.membase + UART011_ICR);

				/*
				 * WA: introduce 26ns(1 uart clk) delay before W1C;
				 * single apb access will incur 2 pclk(133.12Mhz) delay,
				 * so add 2 dummy reads
				 */
				dummy_read = readw(uap->port.membase + UART011_ICR);
				dummy_read = readw(uap->port.membase + UART011_ICR);
			}

			writew(status & ~(UART011_TXIS|UART011_RTIS|
					  UART011_RXIS),
			       uap->port.membase + UART011_ICR);

			if (status & (UART011_RTIS|UART011_RXIS)) {
				if (pl011_dma_rx_running(uap))
					pl011_dma_rx_irq(uap);
				else
					pl011_rx_chars(uap);
			}
			if (status & (UART011_DSRMIS|UART011_DCDMIS|
				      UART011_CTSMIS|UART011_RIMIS))
				pl011_modem_status(uap);
			if (status & UART011_TXIS)
				pl011_tx_chars(uap);

			if (pass_counter-- == 0)
				break;

			status = readw(uap->port.membase + UART011_MIS);
		} while (status != 0);
		handled = 1;
	}

	spin_unlock_irqrestore(&uap->port.lock, flags);

	return IRQ_RETVAL(handled);
}

static unsigned int pl011_tx_empty(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int status = readw(uap->port.membase + UART01x_FR);
	return status & (UART01x_FR_BUSY|UART01x_FR_TXFF) ? 0 : TIOCSER_TEMT;
}

static unsigned int pl011_get_mctrl(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int result = 0;
	unsigned int status = readw(uap->port.membase + UART01x_FR);

#define TIOCMBIT(uartbit, tiocmbit)	\
	if (status & uartbit)		\
		result |= tiocmbit

	TIOCMBIT(UART01x_FR_DCD, TIOCM_CAR);
	TIOCMBIT(UART01x_FR_DSR, TIOCM_DSR);
	TIOCMBIT(UART01x_FR_CTS, TIOCM_CTS);
	TIOCMBIT(UART011_FR_RI, TIOCM_RNG);
#undef TIOCMBIT
	return result;
}

static void pl011_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int cr;

	cr = readw(uap->port.membase + UART011_CR);

#define	TIOCMBIT(tiocmbit, uartbit)		\
	if (mctrl & tiocmbit)		\
		cr |= uartbit;		\
	else				\
		cr &= ~uartbit

	TIOCMBIT(TIOCM_RTS, UART011_CR_RTS);
	TIOCMBIT(TIOCM_DTR, UART011_CR_DTR);
	TIOCMBIT(TIOCM_OUT1, UART011_CR_OUT1);
	TIOCMBIT(TIOCM_OUT2, UART011_CR_OUT2);
	TIOCMBIT(TIOCM_LOOP, UART011_CR_LBE);

	if (uap->autorts) {
		/* We need to disable auto-RTS if we want to turn RTS off */
		TIOCMBIT(TIOCM_RTS, UART011_CR_RTSEN);
	}
#undef TIOCMBIT

	writew(cr, uap->port.membase + UART011_CR);
}

static void pl011_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned long flags;
	unsigned int lcr_h;

	spin_lock_irqsave(&uap->port.lock, flags);
	lcr_h = readw(uap->port.membase + uap->lcrh_tx);
	if (break_state == -1)
		lcr_h |= UART01x_LCRH_BRK;
	else
		lcr_h &= ~UART01x_LCRH_BRK;
	writew(lcr_h, uap->port.membase + uap->lcrh_tx);
	spin_unlock_irqrestore(&uap->port.lock, flags);
}

#ifdef CONFIG_CONSOLE_POLL
static int pl011_get_poll_char(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int status;

	status = readw(uap->port.membase + UART01x_FR);
	if (status & UART01x_FR_RXFE)
		return NO_POLL_CHAR;

	return readw(uap->port.membase + UART01x_DR);
}

static void pl011_put_poll_char(struct uart_port *port,
			 unsigned char ch)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;

	while (readw(uap->port.membase + UART01x_FR) & UART01x_FR_TXFF)
		barrier();

	writew(ch, uap->port.membase + UART01x_DR);
}

#endif /* CONFIG_CONSOLE_POLL */

static int pl011_startup(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int cr;
	int retval;

	/* Optionaly enable pins to be muxed in and configured */
	if (!IS_ERR(uap->pins_default)) {
		retval = pinctrl_select_state(uap->pinctrl, uap->pins_default);
		if (retval)
			dev_err(port->dev,
				"could not set default pins\n");
	}

	/*
	 * Try to enable the clock producer.
	 */
	retval = clk_prepare_enable(uap->clk);
	if (retval)
		goto out;

	uap->port.uartclk = clk_get_rate(uap->clk);

	/* Clear pending error and receive interrupts */
	writew(UART011_OEIS | UART011_BEIS | UART011_PEIS | UART011_FEIS |
	       UART011_RTIS | UART011_RXIS, uap->port.membase + UART011_ICR);

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(uap->port.irq, pl011_int, 0, "uart-pl011", uap);
	if (retval)
		goto clk_dis;

	writew(uap->vendor->ifls, uap->port.membase + UART011_IFLS);

	/*
	 * Provoke TX FIFO interrupt into asserting.
	 */
	cr = UART01x_CR_UARTEN | UART011_CR_TXE | UART011_CR_LBE;
	writew(cr, uap->port.membase + UART011_CR);
	writew(0, uap->port.membase + UART011_FBRD);
	writew(1, uap->port.membase + UART011_IBRD);
	writew(0, uap->port.membase + uap->lcrh_rx);
	if (uap->lcrh_tx != uap->lcrh_rx) {
		int i;
		/*
		 * Wait 10 PCLKs before writing LCRH_TX register,
		 * to get this delay write read only register 10 times
		 */
		for (i = 0; i < 10; ++i)
			writew(0xff, uap->port.membase + UART011_MIS);
		writew(0, uap->port.membase + uap->lcrh_tx);
	}
	writew(0, uap->port.membase + UART01x_DR);
	while (readw(uap->port.membase + UART01x_FR) & UART01x_FR_BUSY)
		barrier();

	/* restore RTS and DTR */
	cr = uap->old_cr & (UART011_CR_RTS | UART011_CR_DTR);
	cr |= UART01x_CR_UARTEN | UART011_CR_RXE | UART011_CR_TXE;
	writew(cr, uap->port.membase + UART011_CR);

	/*
	 * initialise the old status of the modem signals
	 */
	uap->old_status = readw(uap->port.membase + UART01x_FR) & UART01x_FR_MODEM_ANY;

	/* Startup DMA */
	pl011_dma_startup(uap);

	/*
	 * Finally, enable interrupts, only timeouts when using DMA
	 * if initial RX DMA job failed, start in interrupt mode
	 * as well.
	 */
	spin_lock_irq(&uap->port.lock);
	/* Clear out any spuriously appearing RX interrupts */
	 writew(UART011_RTIS | UART011_RXIS,
		uap->port.membase + UART011_ICR);
	uap->im = UART011_RTIM;
	if (!pl011_dma_rx_running(uap))
		uap->im |= UART011_RXIM;
	writew(uap->im, uap->port.membase + UART011_IMSC);
	spin_unlock_irq(&uap->port.lock);

	if (uap->port.dev->platform_data) {
		struct amba_pl011_data *plat;

		plat = uap->port.dev->platform_data;
		if (plat->init)
			plat->init();
	}

	return 0;

 clk_dis:
	clk_disable_unprepare(uap->clk);
 out:
	return retval;
}

static void pl011_shutdown_channel(struct uart_amba_port *uap,
					unsigned int lcrh)
{
      unsigned long val;

      val = readw(uap->port.membase + lcrh);
      val &= ~(UART01x_LCRH_BRK | UART01x_LCRH_FEN);
      writew(val, uap->port.membase + lcrh);
}

static void pl011_shutdown(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int cr;
	int retval;

	/*
	 * disable all interrupts
	 */
	spin_lock_irq(&uap->port.lock);
	uap->im = 0;
	writew(uap->im, uap->port.membase + UART011_IMSC);
	writew(0xffff, uap->port.membase + UART011_ICR);
	spin_unlock_irq(&uap->port.lock);

	pl011_dma_shutdown(uap);

	/*
	 * Free the interrupt
	 */
	free_irq(uap->port.irq, uap);

	/*
	 * disable the port
	 * disable the port. It should not disable RTS and DTR.
	 * Also RTS and DTR state should be preserved to restore
	 * it during startup().
	 */
	uap->autorts = false;
	cr = readw(uap->port.membase + UART011_CR);
	uap->old_cr = cr;
	cr &= UART011_CR_RTS | UART011_CR_DTR;
	cr |= UART01x_CR_UARTEN | UART011_CR_TXE;
	writew(cr, uap->port.membase + UART011_CR);

	/*
	 * disable break condition and fifos
	 */
	pl011_shutdown_channel(uap, uap->lcrh_rx);
	if (uap->lcrh_rx != uap->lcrh_tx)
		pl011_shutdown_channel(uap, uap->lcrh_tx);

	/*
	 * Shut down the clock producer
	 */
	clk_disable_unprepare(uap->clk);
	/* Optionally let pins go into sleep states */
	if (!IS_ERR(uap->pins_sleep)) {
		retval = pinctrl_select_state(uap->pinctrl, uap->pins_sleep);
		if (retval)
			dev_err(port->dev,
				"could not set pins to sleep state\n");
	}


	if (uap->port.dev->platform_data) {
		struct amba_pl011_data *plat;

		plat = uap->port.dev->platform_data;
		if (plat->exit)
			plat->exit();
	}

}

static void
pl011_set_termios(struct uart_port *port, struct ktermios *termios,
		     struct ktermios *old)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	unsigned int lcr_h, old_cr;
	unsigned long flags;
	unsigned int baud, quot, clkdiv;

	if (uap->vendor->oversampling)
		clkdiv = 8;
	else
		clkdiv = 16;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0,
				  port->uartclk / clkdiv);

	if (baud > port->uartclk/16)
		quot = DIV_ROUND_CLOSEST(port->uartclk * 8, baud);
	else
		quot = DIV_ROUND_CLOSEST(port->uartclk * 4, baud);

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr_h = UART01x_LCRH_WLEN_5;
		break;
	case CS6:
		lcr_h = UART01x_LCRH_WLEN_6;
		break;
	case CS7:
		lcr_h = UART01x_LCRH_WLEN_7;
		break;
	default: // CS8
		lcr_h = UART01x_LCRH_WLEN_8;
		break;
	}
	if (termios->c_cflag & CSTOPB)
		lcr_h |= UART01x_LCRH_STP2;
	if (termios->c_cflag & PARENB) {
		lcr_h |= UART01x_LCRH_PEN;
		if (!(termios->c_cflag & PARODD))
			lcr_h |= UART01x_LCRH_EPS;
	}
	if (uap->fifosize > 1)
		lcr_h |= UART01x_LCRH_FEN;

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = UART011_DR_OE | 255;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= UART011_DR_FE | UART011_DR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= UART011_DR_BE;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= UART011_DR_FE | UART011_DR_PE;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= UART011_DR_BE;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= UART011_DR_OE;
	}

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= UART_DUMMY_DR_RX;

	if (UART_ENABLE_MS(port, termios->c_cflag))
		pl011_enable_ms(port);

	/* first, disable everything */
	old_cr = readw(port->membase + UART011_CR);
	writew(0, port->membase + UART011_CR);

	if (termios->c_cflag & CRTSCTS) {
		if (old_cr & UART011_CR_RTS)
			old_cr |= UART011_CR_RTSEN;

		old_cr |= UART011_CR_CTSEN;
		uap->autorts = true;
	} else {
		old_cr &= ~(UART011_CR_CTSEN | UART011_CR_RTSEN);
		uap->autorts = false;
	}

	if (uap->vendor->oversampling) {
		if (baud > port->uartclk / 16)
			old_cr |= ST_UART011_CR_OVSFACT;
		else
			old_cr &= ~ST_UART011_CR_OVSFACT;
	}

	/* Set baud rate */
	writew(quot & 0x3f, port->membase + UART011_FBRD);
	writew(quot >> 6, port->membase + UART011_IBRD);

	/*
	 * ----------v----------v----------v----------v-----
	 * NOTE: MUST BE WRITTEN AFTER UARTLCR_M & UARTLCR_L
	 * ----------^----------^----------^----------^-----
	 */
	writew(lcr_h, port->membase + uap->lcrh_rx);
	if (uap->lcrh_rx != uap->lcrh_tx) {
		int i;
		/*
		 * Wait 10 PCLKs before writing LCRH_TX register,
		 * to get this delay write read only register 10 times
		 */
		for (i = 0; i < 10; ++i)
			writew(0xff, uap->port.membase + UART011_MIS);
		writew(lcr_h, port->membase + uap->lcrh_tx);
	}
	writew(old_cr, port->membase + UART011_CR);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *pl011_type(struct uart_port *port)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;
	return uap->port.type == PORT_AMBA ? uap->type : NULL;
}

/*
 * Release the memory region(s) being used by 'port'
 */
static void pl011_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, SZ_4K);
}

/*
 * Request the memory region(s) being used by 'port'
 */
static int pl011_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, SZ_4K, "uart-pl011")
			!= NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void pl011_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_AMBA;
		pl011_request_port(port);
	}
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int pl011_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_AMBA)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= nr_irqs)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops amba_pl011_pops = {
	.tx_empty	= pl011_tx_empty,
	.set_mctrl	= pl011_set_mctrl,
	.get_mctrl	= pl011_get_mctrl,
	.stop_tx	= pl011_stop_tx,
	.start_tx	= pl011_start_tx,
	.stop_rx	= pl011_stop_rx,
	.enable_ms	= pl011_enable_ms,
	.break_ctl	= pl011_break_ctl,
	.startup	= pl011_startup,
	.shutdown	= pl011_shutdown,
	.flush_buffer	= pl011_dma_flush_buffer,
	.set_termios	= pl011_set_termios,
	.type		= pl011_type,
	.release_port	= pl011_release_port,
	.request_port	= pl011_request_port,
	.config_port	= pl011_config_port,
	.verify_port	= pl011_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char = pl011_get_poll_char,
	.poll_put_char = pl011_put_poll_char,
#endif
};

static struct uart_amba_port *amba_ports[UART_NR];

#ifdef CONFIG_SERIAL_AMBA_PL011_CONSOLE

static void pl011_console_putchar(struct uart_port *port, int ch)
{
	struct uart_amba_port *uap = (struct uart_amba_port *)port;

	while (readw(uap->port.membase + UART01x_FR) & UART01x_FR_TXFF)
		barrier();
	writew(ch, uap->port.membase + UART01x_DR);
}

static void
pl011_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_amba_port *uap = amba_ports[co->index];
	unsigned int status, old_cr, new_cr;
	unsigned long flags;
	int locked = 1;

	clk_enable(uap->clk);

	local_irq_save(flags);
	if (uap->port.sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&uap->port.lock);
	else
		spin_lock(&uap->port.lock);

	/*
	 *	First save the CR then disable the interrupts
	 */
	old_cr = readw(uap->port.membase + UART011_CR);
	new_cr = old_cr & ~UART011_CR_CTSEN;
	new_cr |= UART01x_CR_UARTEN | UART011_CR_TXE;
	writew(new_cr, uap->port.membase + UART011_CR);

	uart_console_write(&uap->port, s, count, pl011_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the TCR
	 */
	do {
		status = readw(uap->port.membase + UART01x_FR);
	} while (status & UART01x_FR_BUSY);
	writew(old_cr, uap->port.membase + UART011_CR);

	if (locked)
		spin_unlock(&uap->port.lock);
	local_irq_restore(flags);

	clk_disable(uap->clk);
}

static void __init
pl011_console_get_options(struct uart_amba_port *uap, int *baud,
			     int *parity, int *bits)
{
	if (readw(uap->port.membase + UART011_CR) & UART01x_CR_UARTEN) {
		unsigned int lcr_h, ibrd, fbrd;

		lcr_h = readw(uap->port.membase + uap->lcrh_tx);

		*parity = 'n';
		if (lcr_h & UART01x_LCRH_PEN) {
			if (lcr_h & UART01x_LCRH_EPS)
				*parity = 'e';
			else
				*parity = 'o';
		}

		if ((lcr_h & 0x60) == UART01x_LCRH_WLEN_7)
			*bits = 7;
		else
			*bits = 8;

		ibrd = readw(uap->port.membase + UART011_IBRD);
		fbrd = readw(uap->port.membase + UART011_FBRD);

		*baud = uap->port.uartclk * 4 / (64 * ibrd + fbrd);

		if (uap->vendor->oversampling) {
			if (readw(uap->port.membase + UART011_CR)
				  & ST_UART011_CR_OVSFACT)
				*baud *= 2;
		}
	}
}

static int __init pl011_console_setup(struct console *co, char *options)
{
	struct uart_amba_port *uap;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= UART_NR)
		co->index = 0;
	uap = amba_ports[co->index];
	if (!uap)
		return -ENODEV;

	/* Allow pins to be muxed in and configured */
	if (!IS_ERR(uap->pins_default)) {
		ret = pinctrl_select_state(uap->pinctrl, uap->pins_default);
		if (ret)
			dev_err(uap->port.dev,
				"could not set default pins\n");
	}

	ret = clk_prepare(uap->clk);
	if (ret)
		return ret;

	if (uap->port.dev->platform_data) {
		struct amba_pl011_data *plat;

		plat = uap->port.dev->platform_data;
		if (plat->init)
			plat->init();
	}

	uap->port.uartclk = clk_get_rate(uap->clk);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		pl011_console_get_options(uap, &baud, &parity, &bits);

	return uart_set_options(&uap->port, co, baud, parity, bits, flow);
}

static struct uart_driver amba_reg;
static struct console amba_console = {
	.name		= "ttyAMA",
	.write		= pl011_console_write,
	.device		= uart_console_device,
	.setup		= pl011_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &amba_reg,
};

#define AMBA_CONSOLE	(&amba_console)
#else
#define AMBA_CONSOLE	NULL
#endif

static struct uart_driver amba_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "ttyAMA",
	.dev_name		= "ttyAMA",
	.major			= SERIAL_AMBA_MAJOR,
	.minor			= SERIAL_AMBA_MINOR,
	.nr			= UART_NR,
	.cons			= AMBA_CONSOLE,
};

static int pl011_probe(struct amba_device *dev, const struct amba_id *id)
{
	struct uart_amba_port *uap;
	struct vendor_data *vendor = id->data;
	void __iomem *base;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(amba_ports); i++)
		if (amba_ports[i] == NULL)
			break;

	if (i == ARRAY_SIZE(amba_ports)) {
		ret = -EBUSY;
		goto out;
	}

	uap = kzalloc(sizeof(struct uart_amba_port), GFP_KERNEL);
	if (uap == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	base = ioremap(dev->res.start, resource_size(&dev->res));
	if (!base) {
		ret = -ENOMEM;
		goto free;
	}

	uap->pinctrl = devm_pinctrl_get(&dev->dev);
	if (IS_ERR(uap->pinctrl)) {
		ret = PTR_ERR(uap->pinctrl);
		goto unmap;
	}
	uap->pins_default = pinctrl_lookup_state(uap->pinctrl,
						 PINCTRL_STATE_DEFAULT);
	if (IS_ERR(uap->pins_default))
		dev_err(&dev->dev, "could not get default pinstate\n");

	uap->pins_sleep = pinctrl_lookup_state(uap->pinctrl,
					       PINCTRL_STATE_SLEEP);
	if (IS_ERR(uap->pins_sleep))
		dev_dbg(&dev->dev, "could not get sleep pinstate\n");

	uap->clk = clk_get(&dev->dev, NULL);
	if (IS_ERR(uap->clk)) {
		ret = PTR_ERR(uap->clk);
		goto unmap;
	}

	uap->vendor = vendor;
	uap->lcrh_rx = vendor->lcrh_rx;
	uap->lcrh_tx = vendor->lcrh_tx;
	uap->old_cr = 0;
	uap->fifosize = vendor->fifosize;
	uap->interrupt_may_hang = vendor->interrupt_may_hang;
	uap->port.dev = &dev->dev;
	uap->port.mapbase = dev->res.start;
	uap->port.membase = base;
	uap->port.iotype = UPIO_MEM;
	uap->port.irq = dev->irq[0];
	uap->port.fifosize = uap->fifosize;
	uap->port.ops = &amba_pl011_pops;
	uap->port.flags = UPF_BOOT_AUTOCONF;
	uap->port.line = i;
	pl011_dma_probe(uap);

	/* Ensure interrupts from this UART are masked and cleared */
	writew(0, uap->port.membase + UART011_IMSC);
	writew(0xffff, uap->port.membase + UART011_ICR);

	snprintf(uap->type, sizeof(uap->type), "PL011 rev%u", amba_rev(dev));

	amba_ports[i] = uap;

	amba_set_drvdata(dev, uap);
	ret = uart_add_one_port(&amba_reg, &uap->port);
	if (ret) {
		amba_set_drvdata(dev, NULL);
		amba_ports[i] = NULL;
		pl011_dma_remove(uap);
		clk_put(uap->clk);
 unmap:
		iounmap(base);
 free:
		kfree(uap);
	}
 out:
	return ret;
}

static int pl011_remove(struct amba_device *dev)
{
	struct uart_amba_port *uap = amba_get_drvdata(dev);
	int i;

	amba_set_drvdata(dev, NULL);

	uart_remove_one_port(&amba_reg, &uap->port);

	for (i = 0; i < ARRAY_SIZE(amba_ports); i++)
		if (amba_ports[i] == uap)
			amba_ports[i] = NULL;

	pl011_dma_remove(uap);
	iounmap(uap->port.membase);
	clk_put(uap->clk);
	kfree(uap);
	return 0;
}

#ifdef CONFIG_PM
static int pl011_suspend(struct amba_device *dev, pm_message_t state)
{
	struct uart_amba_port *uap = amba_get_drvdata(dev);

	if (!uap)
		return -EINVAL;

	return uart_suspend_port(&amba_reg, &uap->port);
}

static int pl011_resume(struct amba_device *dev)
{
	struct uart_amba_port *uap = amba_get_drvdata(dev);

	if (!uap)
		return -EINVAL;

	return uart_resume_port(&amba_reg, &uap->port);
}
#endif

static struct amba_id pl011_ids[] = {
	{
		.id	= 0x00041011,
		.mask	= 0x000fffff,
		.data	= &vendor_arm,
	},
	{
		.id	= 0x00380802,
		.mask	= 0x00ffffff,
		.data	= &vendor_st,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, pl011_ids);

static struct amba_driver pl011_driver = {
	.drv = {
		.name	= "uart-pl011",
	},
	.id_table	= pl011_ids,
	.probe		= pl011_probe,
	.remove		= pl011_remove,
#ifdef CONFIG_PM
	.suspend	= pl011_suspend,
	.resume		= pl011_resume,
#endif
};

static int __init pl011_init(void)
{
	int ret;
	printk(KERN_INFO "Serial: AMBA PL011 UART driver\n");

	ret = uart_register_driver(&amba_reg);
	if (ret == 0) {
		ret = amba_driver_register(&pl011_driver);
		if (ret)
			uart_unregister_driver(&amba_reg);
	}
	return ret;
}

static void __exit pl011_exit(void)
{
	amba_driver_unregister(&pl011_driver);
	uart_unregister_driver(&amba_reg);
}

/*
 * While this can be a module, if builtin it's most likely the console
 * So let's leave module_exit but move module_init to an earlier place
 */
arch_initcall(pl011_init);
module_exit(pl011_exit);

MODULE_AUTHOR("ARM Ltd/Deep Blue Solutions Ltd");
MODULE_DESCRIPTION("ARM AMBA serial port driver");
MODULE_LICENSE("GPL");
