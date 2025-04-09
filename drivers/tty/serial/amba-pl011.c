// SPDX-License-Identifier: GPL-2.0+
/*
 *  Driver for AMBA serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *  Copyright (C) 2010 ST-Ericsson SA
 *
 * This is a generic driver for ARM AMBA-type serial ports.  They
 * have a lot of 16550-like features, but are not register compatible.
 * Note that although they do have CTS, DCD and DSR inputs, they do
 * not have an RI input, nor do they have DTR or RTS outputs.  If
 * required, these have to be supplied via some other means (eg, GPIO)
 * and hooked into this driver.
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/platform_device.h>
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
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sizes.h>
#include <linux/io.h>
#include <linux/acpi.h>

#define UART_NR			14

#define SERIAL_AMBA_MAJOR	204
#define SERIAL_AMBA_MINOR	64
#define SERIAL_AMBA_NR		UART_NR

#define AMBA_ISR_PASS_LIMIT	256

#define UART_DR_ERROR		(UART011_DR_OE | UART011_DR_BE | UART011_DR_PE | UART011_DR_FE)
#define UART_DUMMY_DR_RX	BIT(16)

enum {
	REG_DR,
	REG_ST_DMAWM,
	REG_ST_TIMEOUT,
	REG_FR,
	REG_LCRH_RX,
	REG_LCRH_TX,
	REG_IBRD,
	REG_FBRD,
	REG_CR,
	REG_IFLS,
	REG_IMSC,
	REG_RIS,
	REG_MIS,
	REG_ICR,
	REG_DMACR,
	REG_ST_XFCR,
	REG_ST_XON1,
	REG_ST_XON2,
	REG_ST_XOFF1,
	REG_ST_XOFF2,
	REG_ST_ITCR,
	REG_ST_ITIP,
	REG_ST_ABCR,
	REG_ST_ABIMSC,

	/* The size of the array - must be last */
	REG_ARRAY_SIZE,
};

static u16 pl011_std_offsets[REG_ARRAY_SIZE] = {
	[REG_DR] = UART01x_DR,
	[REG_FR] = UART01x_FR,
	[REG_LCRH_RX] = UART011_LCRH,
	[REG_LCRH_TX] = UART011_LCRH,
	[REG_IBRD] = UART011_IBRD,
	[REG_FBRD] = UART011_FBRD,
	[REG_CR] = UART011_CR,
	[REG_IFLS] = UART011_IFLS,
	[REG_IMSC] = UART011_IMSC,
	[REG_RIS] = UART011_RIS,
	[REG_MIS] = UART011_MIS,
	[REG_ICR] = UART011_ICR,
	[REG_DMACR] = UART011_DMACR,
};

/* There is by now at least one vendor with differing details, so handle it */
struct vendor_data {
	const u16		*reg_offset;
	unsigned int		ifls;
	unsigned int		fr_busy;
	unsigned int		fr_dsr;
	unsigned int		fr_cts;
	unsigned int		fr_ri;
	unsigned int		inv_fr;
	bool			access_32b;
	bool			oversampling;
	bool			dma_threshold;
	bool			cts_event_workaround;
	bool			always_enabled;
	bool			fixed_options;

	unsigned int (*get_fifosize)(struct amba_device *dev);
};

static unsigned int get_fifosize_arm(struct amba_device *dev)
{
	return amba_rev(dev) < 3 ? 16 : 32;
}

static struct vendor_data vendor_arm = {
	.reg_offset		= pl011_std_offsets,
	.ifls			= UART011_IFLS_RX4_8 | UART011_IFLS_TX4_8,
	.fr_busy		= UART01x_FR_BUSY,
	.fr_dsr			= UART01x_FR_DSR,
	.fr_cts			= UART01x_FR_CTS,
	.fr_ri			= UART011_FR_RI,
	.oversampling		= false,
	.dma_threshold		= false,
	.cts_event_workaround	= false,
	.always_enabled		= false,
	.fixed_options		= false,
	.get_fifosize		= get_fifosize_arm,
};

static const struct vendor_data vendor_sbsa = {
	.reg_offset		= pl011_std_offsets,
	.fr_busy		= UART01x_FR_BUSY,
	.fr_dsr			= UART01x_FR_DSR,
	.fr_cts			= UART01x_FR_CTS,
	.fr_ri			= UART011_FR_RI,
	.access_32b		= true,
	.oversampling		= false,
	.dma_threshold		= false,
	.cts_event_workaround	= false,
	.always_enabled		= true,
	.fixed_options		= true,
};

#ifdef CONFIG_ACPI_SPCR_TABLE
static const struct vendor_data vendor_qdt_qdf2400_e44 = {
	.reg_offset		= pl011_std_offsets,
	.fr_busy		= UART011_FR_TXFE,
	.fr_dsr			= UART01x_FR_DSR,
	.fr_cts			= UART01x_FR_CTS,
	.fr_ri			= UART011_FR_RI,
	.inv_fr			= UART011_FR_TXFE,
	.access_32b		= true,
	.oversampling		= false,
	.dma_threshold		= false,
	.cts_event_workaround	= false,
	.always_enabled		= true,
	.fixed_options		= true,
};
#endif

static u16 pl011_st_offsets[REG_ARRAY_SIZE] = {
	[REG_DR] = UART01x_DR,
	[REG_ST_DMAWM] = ST_UART011_DMAWM,
	[REG_ST_TIMEOUT] = ST_UART011_TIMEOUT,
	[REG_FR] = UART01x_FR,
	[REG_LCRH_RX] = ST_UART011_LCRH_RX,
	[REG_LCRH_TX] = ST_UART011_LCRH_TX,
	[REG_IBRD] = UART011_IBRD,
	[REG_FBRD] = UART011_FBRD,
	[REG_CR] = UART011_CR,
	[REG_IFLS] = UART011_IFLS,
	[REG_IMSC] = UART011_IMSC,
	[REG_RIS] = UART011_RIS,
	[REG_MIS] = UART011_MIS,
	[REG_ICR] = UART011_ICR,
	[REG_DMACR] = UART011_DMACR,
	[REG_ST_XFCR] = ST_UART011_XFCR,
	[REG_ST_XON1] = ST_UART011_XON1,
	[REG_ST_XON2] = ST_UART011_XON2,
	[REG_ST_XOFF1] = ST_UART011_XOFF1,
	[REG_ST_XOFF2] = ST_UART011_XOFF2,
	[REG_ST_ITCR] = ST_UART011_ITCR,
	[REG_ST_ITIP] = ST_UART011_ITIP,
	[REG_ST_ABCR] = ST_UART011_ABCR,
	[REG_ST_ABIMSC] = ST_UART011_ABIMSC,
};

static unsigned int get_fifosize_st(struct amba_device *dev)
{
	return 64;
}

static struct vendor_data vendor_st = {
	.reg_offset		= pl011_st_offsets,
	.ifls			= UART011_IFLS_RX_HALF | UART011_IFLS_TX_HALF,
	.fr_busy		= UART01x_FR_BUSY,
	.fr_dsr			= UART01x_FR_DSR,
	.fr_cts			= UART01x_FR_CTS,
	.fr_ri			= UART011_FR_RI,
	.oversampling		= true,
	.dma_threshold		= true,
	.cts_event_workaround	= true,
	.always_enabled		= false,
	.fixed_options		= false,
	.get_fifosize		= get_fifosize_st,
};

/* Deals with DMA transactions */

struct pl011_dmabuf {
	dma_addr_t		dma;
	size_t			len;
	char			*buf;
};

struct pl011_dmarx_data {
	struct dma_chan		*chan;
	struct completion	complete;
	bool			use_buf_b;
	struct pl011_dmabuf	dbuf_a;
	struct pl011_dmabuf	dbuf_b;
	dma_cookie_t		cookie;
	bool			running;
	struct timer_list	timer;
	unsigned int last_residue;
	unsigned long last_jiffies;
	bool auto_poll_rate;
	unsigned int poll_rate;
	unsigned int poll_timeout;
};

struct pl011_dmatx_data {
	struct dma_chan		*chan;
	dma_addr_t		dma;
	size_t			len;
	char			*buf;
	bool			queued;
};

enum pl011_rs485_tx_state {
	OFF,
	WAIT_AFTER_RTS,
	SEND,
	WAIT_AFTER_SEND,
};

/*
 * We wrap our port structure around the generic uart_port.
 */
struct uart_amba_port {
	struct uart_port	port;
	const u16		*reg_offset;
	struct clk		*clk;
	const struct vendor_data *vendor;
	unsigned int		im;		/* interrupt mask */
	unsigned int		old_status;
	unsigned int		fifosize;	/* vendor-specific */
	unsigned int		fixed_baud;	/* vendor-set fixed baud rate */
	char			type[12];
	ktime_t			rs485_tx_drain_interval; /* nano */
	enum pl011_rs485_tx_state	rs485_tx_state;
	struct hrtimer		trigger_start_tx;
	struct hrtimer		trigger_stop_tx;
	bool			console_line_ended;
#ifdef CONFIG_DMA_ENGINE
	/* DMA stuff */
	unsigned int		dmacr;		/* dma control reg */
	bool			using_tx_dma;
	bool			using_rx_dma;
	struct pl011_dmarx_data dmarx;
	struct pl011_dmatx_data	dmatx;
	bool			dma_probed;
#endif
};

static unsigned int pl011_tx_empty(struct uart_port *port);

static unsigned int pl011_reg_to_offset(const struct uart_amba_port *uap,
					unsigned int reg)
{
	return uap->reg_offset[reg];
}

static unsigned int pl011_read(const struct uart_amba_port *uap,
			       unsigned int reg)
{
	void __iomem *addr = uap->port.membase + pl011_reg_to_offset(uap, reg);

	return (uap->port.iotype == UPIO_MEM32) ?
		readl_relaxed(addr) : readw_relaxed(addr);
}

static void pl011_write(unsigned int val, const struct uart_amba_port *uap,
			unsigned int reg)
{
	void __iomem *addr = uap->port.membase + pl011_reg_to_offset(uap, reg);

	if (uap->port.iotype == UPIO_MEM32)
		writel_relaxed(val, addr);
	else
		writew_relaxed(val, addr);
}

/*
 * Reads up to 256 characters from the FIFO or until it's empty and
 * inserts them into the TTY layer. Returns the number of characters
 * read from the FIFO.
 */
static int pl011_fifo_to_tty(struct uart_amba_port *uap)
{
	unsigned int ch, fifotaken;
	int sysrq;
	u16 status;
	u8 flag;

	for (fifotaken = 0; fifotaken != 256; fifotaken++) {
		status = pl011_read(uap, REG_FR);
		if (status & UART01x_FR_RXFE)
			break;

		/* Take chars from the FIFO and update status */
		ch = pl011_read(uap, REG_DR) | UART_DUMMY_DR_RX;
		flag = TTY_NORMAL;
		uap->port.icount.rx++;

		if (unlikely(ch & UART_DR_ERROR)) {
			if (ch & UART011_DR_BE) {
				ch &= ~(UART011_DR_FE | UART011_DR_PE);
				uap->port.icount.brk++;
				if (uart_handle_break(&uap->port))
					continue;
			} else if (ch & UART011_DR_PE) {
				uap->port.icount.parity++;
			} else if (ch & UART011_DR_FE) {
				uap->port.icount.frame++;
			}
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

		sysrq = uart_prepare_sysrq_char(&uap->port, ch & 255);
		if (!sysrq)
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

static int pl011_dmabuf_init(struct dma_chan *chan, struct pl011_dmabuf *db,
			     enum dma_data_direction dir)
{
	db->buf = dma_alloc_coherent(chan->device->dev, PL011_DMA_BUFFER_SIZE,
				     &db->dma, GFP_KERNEL);
	if (!db->buf)
		return -ENOMEM;
	db->len = PL011_DMA_BUFFER_SIZE;

	return 0;
}

static void pl011_dmabuf_free(struct dma_chan *chan, struct pl011_dmabuf *db,
			      enum dma_data_direction dir)
{
	if (db->buf) {
		dma_free_coherent(chan->device->dev,
				  PL011_DMA_BUFFER_SIZE, db->buf, db->dma);
	}
}

static void pl011_dma_probe(struct uart_amba_port *uap)
{
	/* DMA is the sole user of the platform data right now */
	struct amba_pl011_data *plat = dev_get_platdata(uap->port.dev);
	struct device *dev = uap->port.dev;
	struct dma_slave_config tx_conf = {
		.dst_addr = uap->port.mapbase +
				 pl011_reg_to_offset(uap, REG_DR),
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.direction = DMA_MEM_TO_DEV,
		.dst_maxburst = uap->fifosize >> 1,
		.device_fc = false,
	};
	struct dma_chan *chan;
	dma_cap_mask_t mask;

	uap->dma_probed = true;
	chan = dma_request_chan(dev, "tx");
	if (IS_ERR(chan)) {
		if (PTR_ERR(chan) == -EPROBE_DEFER) {
			uap->dma_probed = false;
			return;
		}

		/* We need platform data */
		if (!plat || !plat->dma_filter) {
			dev_dbg(uap->port.dev, "no DMA platform data\n");
			return;
		}

		/* Try to acquire a generic DMA engine slave TX channel */
		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		chan = dma_request_channel(mask, plat->dma_filter,
					   plat->dma_tx_param);
		if (!chan) {
			dev_err(uap->port.dev, "no TX DMA channel!\n");
			return;
		}
	}

	dmaengine_slave_config(chan, &tx_conf);
	uap->dmatx.chan = chan;

	dev_info(uap->port.dev, "DMA channel TX %s\n",
		 dma_chan_name(uap->dmatx.chan));

	/* Optionally make use of an RX channel as well */
	chan = dma_request_chan(dev, "rx");

	if (IS_ERR(chan) && plat && plat->dma_rx_param) {
		chan = dma_request_channel(mask, plat->dma_filter, plat->dma_rx_param);

		if (!chan) {
			dev_err(uap->port.dev, "no RX DMA channel!\n");
			return;
		}
	}

	if (!IS_ERR(chan)) {
		struct dma_slave_config rx_conf = {
			.src_addr = uap->port.mapbase +
				pl011_reg_to_offset(uap, REG_DR),
			.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
			.direction = DMA_DEV_TO_MEM,
			.src_maxburst = uap->fifosize >> 2,
			.device_fc = false,
		};
		struct dma_slave_caps caps;

		/*
		 * Some DMA controllers provide information on their capabilities.
		 * If the controller does, check for suitable residue processing
		 * otherwise assime all is well.
		 */
		if (dma_get_slave_caps(chan, &caps) == 0) {
			if (caps.residue_granularity ==
					DMA_RESIDUE_GRANULARITY_DESCRIPTOR) {
				dma_release_channel(chan);
				dev_info(uap->port.dev,
					 "RX DMA disabled - no residue processing\n");
				return;
			}
		}
		dmaengine_slave_config(chan, &rx_conf);
		uap->dmarx.chan = chan;

		uap->dmarx.auto_poll_rate = false;
		if (plat && plat->dma_rx_poll_enable) {
			/* Set poll rate if specified. */
			if (plat->dma_rx_poll_rate) {
				uap->dmarx.auto_poll_rate = false;
				uap->dmarx.poll_rate = plat->dma_rx_poll_rate;
			} else {
				/*
				 * 100 ms defaults to poll rate if not
				 * specified. This will be adjusted with
				 * the baud rate at set_termios.
				 */
				uap->dmarx.auto_poll_rate = true;
				uap->dmarx.poll_rate =  100;
			}
			/* 3 secs defaults poll_timeout if not specified. */
			if (plat->dma_rx_poll_timeout)
				uap->dmarx.poll_timeout =
					plat->dma_rx_poll_timeout;
			else
				uap->dmarx.poll_timeout = 3000;
		} else if (!plat && dev->of_node) {
			uap->dmarx.auto_poll_rate =
					of_property_read_bool(dev->of_node, "auto-poll");
			if (uap->dmarx.auto_poll_rate) {
				u32 x;

				if (of_property_read_u32(dev->of_node, "poll-rate-ms", &x) == 0)
					uap->dmarx.poll_rate = x;
				else
					uap->dmarx.poll_rate = 100;
				if (of_property_read_u32(dev->of_node, "poll-timeout-ms", &x) == 0)
					uap->dmarx.poll_timeout = x;
				else
					uap->dmarx.poll_timeout = 3000;
			}
		}
		dev_info(uap->port.dev, "DMA channel RX %s\n",
			 dma_chan_name(uap->dmarx.chan));
	}
}

static void pl011_dma_remove(struct uart_amba_port *uap)
{
	if (uap->dmatx.chan)
		dma_release_channel(uap->dmatx.chan);
	if (uap->dmarx.chan)
		dma_release_channel(uap->dmarx.chan);
}

/* Forward declare these for the refill routine */
static int pl011_dma_tx_refill(struct uart_amba_port *uap);
static void pl011_start_tx_pio(struct uart_amba_port *uap);

/*
 * The current DMA TX buffer has been sent.
 * Try to queue up another DMA buffer.
 */
static void pl011_dma_tx_callback(void *data)
{
	struct uart_amba_port *uap = data;
	struct tty_port *tport = &uap->port.state->port;
	struct pl011_dmatx_data *dmatx = &uap->dmatx;
	unsigned long flags;
	u16 dmacr;

	uart_port_lock_irqsave(&uap->port, &flags);
	if (uap->dmatx.queued)
		dma_unmap_single(dmatx->chan->device->dev, dmatx->dma,
				 dmatx->len, DMA_TO_DEVICE);

	dmacr = uap->dmacr;
	uap->dmacr = dmacr & ~UART011_TXDMAE;
	pl011_write(uap->dmacr, uap, REG_DMACR);

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
	    kfifo_is_empty(&tport->xmit_fifo)) {
		uap->dmatx.queued = false;
		uart_port_unlock_irqrestore(&uap->port, flags);
		return;
	}

	if (pl011_dma_tx_refill(uap) <= 0)
		/*
		 * We didn't queue a DMA buffer for some reason, but we
		 * have data pending to be sent.  Re-enable the TX IRQ.
		 */
		pl011_start_tx_pio(uap);

	uart_port_unlock_irqrestore(&uap->port, flags);
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
	struct tty_port *tport = &uap->port.state->port;
	unsigned int count;

	/*
	 * Try to avoid the overhead involved in using DMA if the
	 * transaction fits in the first half of the FIFO, by using
	 * the standard interrupt handling.  This ensures that we
	 * issue a uart_write_wakeup() at the appropriate time.
	 */
	count = kfifo_len(&tport->xmit_fifo);
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

	count = kfifo_out_peek(&tport->xmit_fifo, dmatx->buf, count);
	dmatx->len = count;
	dmatx->dma = dma_map_single(dma_dev->dev, dmatx->buf, count,
				    DMA_TO_DEVICE);
	if (dmatx->dma == DMA_MAPPING_ERROR) {
		uap->dmatx.queued = false;
		dev_dbg(uap->port.dev, "unable to map TX DMA\n");
		return -EBUSY;
	}

	desc = dmaengine_prep_slave_single(chan, dmatx->dma, dmatx->len, DMA_MEM_TO_DEV,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dma_unmap_single(dma_dev->dev, dmatx->dma, dmatx->len, DMA_TO_DEVICE);
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
	pl011_write(uap->dmacr, uap, REG_DMACR);
	uap->dmatx.queued = true;

	/*
	 * Now we know that DMA will fire, so advance the ring buffer
	 * with the stuff we just dispatched.
	 */
	uart_xmit_advance(&uap->port, count);

	if (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
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
		pl011_write(uap->dmacr, uap, REG_DMACR);
		uap->im &= ~UART011_TXIM;
		pl011_write(uap->im, uap, REG_IMSC);
		return true;
	}

	/*
	 * We don't have a TX buffer queued, so try to queue one.
	 * If we successfully queued a buffer, mask the TX IRQ.
	 */
	if (pl011_dma_tx_refill(uap) > 0) {
		uap->im &= ~UART011_TXIM;
		pl011_write(uap->im, uap, REG_IMSC);
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
		pl011_write(uap->dmacr, uap, REG_DMACR);
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
				pl011_write(uap->im, uap, REG_IMSC);
			} else {
				ret = false;
			}
		} else if (!(uap->dmacr & UART011_TXDMAE)) {
			uap->dmacr |= UART011_TXDMAE;
			pl011_write(uap->dmacr, uap, REG_DMACR);
		}
		return ret;
	}

	/*
	 * We have an X-char to send.  Disable DMA to prevent it loading
	 * the TX fifo, and then see if we can stuff it into the FIFO.
	 */
	dmacr = uap->dmacr;
	uap->dmacr &= ~UART011_TXDMAE;
	pl011_write(uap->dmacr, uap, REG_DMACR);

	if (pl011_read(uap, REG_FR) & UART01x_FR_TXFF) {
		/*
		 * No space in the FIFO, so enable the transmit interrupt
		 * so we know when there is space.  Note that once we've
		 * loaded the character, we should just re-enable DMA.
		 */
		return false;
	}

	pl011_write(uap->port.x_char, uap, REG_DR);
	uap->port.icount.tx++;
	uap->port.x_char = 0;

	/* Success - restore the DMA state */
	uap->dmacr = dmacr;
	pl011_write(dmacr, uap, REG_DMACR);

	return true;
}

/*
 * Flush the transmit buffer.
 * Locking: called with port lock held and IRQs disabled.
 */
static void pl011_dma_flush_buffer(struct uart_port *port)
__releases(&uap->port.lock)
__acquires(&uap->port.lock)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);

	if (!uap->using_tx_dma)
		return;

	dmaengine_terminate_async(uap->dmatx.chan);

	if (uap->dmatx.queued) {
		dma_unmap_single(uap->dmatx.chan->device->dev, uap->dmatx.dma,
				 uap->dmatx.len, DMA_TO_DEVICE);
		uap->dmatx.queued = false;
		uap->dmacr &= ~UART011_TXDMAE;
		pl011_write(uap->dmacr, uap, REG_DMACR);
	}
}

static void pl011_dma_rx_callback(void *data);

static int pl011_dma_rx_trigger_dma(struct uart_amba_port *uap)
{
	struct dma_chan *rxchan = uap->dmarx.chan;
	struct pl011_dmarx_data *dmarx = &uap->dmarx;
	struct dma_async_tx_descriptor *desc;
	struct pl011_dmabuf *dbuf;

	if (!rxchan)
		return -EIO;

	/* Start the RX DMA job */
	dbuf = uap->dmarx.use_buf_b ?
		&uap->dmarx.dbuf_b : &uap->dmarx.dbuf_a;
	desc = dmaengine_prep_slave_single(rxchan, dbuf->dma, dbuf->len,
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
	pl011_write(uap->dmacr, uap, REG_DMACR);
	uap->dmarx.running = true;

	uap->im &= ~UART011_RXIM;
	pl011_write(uap->im, uap, REG_IMSC);

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
	struct tty_port *port = &uap->port.state->port;
	struct pl011_dmabuf *dbuf = use_buf_b ?
		&uap->dmarx.dbuf_b : &uap->dmarx.dbuf_a;
	int dma_count = 0;
	u32 fifotaken = 0; /* only used for vdbg() */

	struct pl011_dmarx_data *dmarx = &uap->dmarx;
	int dmataken = 0;

	if (uap->dmarx.poll_rate) {
		/* The data can be taken by polling */
		dmataken = dbuf->len - dmarx->last_residue;
		/* Recalculate the pending size */
		if (pending >= dmataken)
			pending -= dmataken;
	}

	/* Pick the remain data from the DMA */
	if (pending) {
		/*
		 * First take all chars in the DMA pipe, then look in the FIFO.
		 * Note that tty_insert_flip_buf() tries to take as many chars
		 * as it can.
		 */
		dma_count = tty_insert_flip_string(port, dbuf->buf + dmataken, pending);

		uap->port.icount.rx += dma_count;
		if (dma_count < pending)
			dev_warn(uap->port.dev,
				 "couldn't insert all characters (TTY is full?)\n");
	}

	/* Reset the last_residue for Rx DMA poll */
	if (uap->dmarx.poll_rate)
		dmarx->last_residue = dbuf->len;

	/*
	 * Only continue with trying to read the FIFO if all DMA chars have
	 * been taken first.
	 */
	if (dma_count == pending && readfifo) {
		/* Clear any error flags */
		pl011_write(UART011_OEIS | UART011_BEIS | UART011_PEIS |
			    UART011_FEIS, uap, REG_ICR);

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

	dev_vdbg(uap->port.dev,
		 "Took %d chars from DMA buffer and %d chars from the FIFO\n",
		 dma_count, fifotaken);
	tty_flip_buffer_push(port);
}

static void pl011_dma_rx_irq(struct uart_amba_port *uap)
{
	struct pl011_dmarx_data *dmarx = &uap->dmarx;
	struct dma_chan *rxchan = dmarx->chan;
	struct pl011_dmabuf *dbuf = dmarx->use_buf_b ?
		&dmarx->dbuf_b : &dmarx->dbuf_a;
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
	pl011_write(uap->dmacr, uap, REG_DMACR);
	uap->dmarx.running = false;

	pending = dbuf->len - state.residue;
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
		dev_dbg(uap->port.dev,
			"could not retrigger RX DMA job fall back to interrupt mode\n");
		uap->im |= UART011_RXIM;
		pl011_write(uap->im, uap, REG_IMSC);
	}
}

static void pl011_dma_rx_callback(void *data)
{
	struct uart_amba_port *uap = data;
	struct pl011_dmarx_data *dmarx = &uap->dmarx;
	struct dma_chan *rxchan = dmarx->chan;
	bool lastbuf = dmarx->use_buf_b;
	struct pl011_dmabuf *dbuf = dmarx->use_buf_b ?
		&dmarx->dbuf_b : &dmarx->dbuf_a;
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
	uart_port_lock_irq(&uap->port);
	/*
	 * Rx data can be taken by the UART interrupts during
	 * the DMA irq handler. So we check the residue here.
	 */
	rxchan->device->device_tx_status(rxchan, dmarx->cookie, &state);
	pending = dbuf->len - state.residue;
	BUG_ON(pending > PL011_DMA_BUFFER_SIZE);
	/* Then we terminate the transfer - we now know our residue */
	dmaengine_terminate_all(rxchan);

	uap->dmarx.running = false;
	dmarx->use_buf_b = !lastbuf;
	ret = pl011_dma_rx_trigger_dma(uap);

	pl011_dma_rx_chars(uap, pending, lastbuf, false);
	uart_unlock_and_check_sysrq(&uap->port);
	/*
	 * Do this check after we picked the DMA chars so we don't
	 * get some IRQ immediately from RX.
	 */
	if (ret) {
		dev_dbg(uap->port.dev,
			"could not retrigger RX DMA job fall back to interrupt mode\n");
		uap->im |= UART011_RXIM;
		pl011_write(uap->im, uap, REG_IMSC);
	}
}

/*
 * Stop accepting received characters, when we're shutting down or
 * suspending this port.
 * Locking: called with port lock held and IRQs disabled.
 */
static inline void pl011_dma_rx_stop(struct uart_amba_port *uap)
{
	if (!uap->using_rx_dma)
		return;

	/* FIXME.  Just disable the DMA enable */
	uap->dmacr &= ~UART011_RXDMAE;
	pl011_write(uap->dmacr, uap, REG_DMACR);
}

/*
 * Timer handler for Rx DMA polling.
 * Every polling, It checks the residue in the dma buffer and transfer
 * data to the tty. Also, last_residue is updated for the next polling.
 */
static void pl011_dma_rx_poll(struct timer_list *t)
{
	struct uart_amba_port *uap = from_timer(uap, t, dmarx.timer);
	struct tty_port *port = &uap->port.state->port;
	struct pl011_dmarx_data *dmarx = &uap->dmarx;
	struct dma_chan *rxchan = uap->dmarx.chan;
	unsigned long flags;
	unsigned int dmataken = 0;
	unsigned int size = 0;
	struct pl011_dmabuf *dbuf;
	int dma_count;
	struct dma_tx_state state;

	dbuf = dmarx->use_buf_b ? &uap->dmarx.dbuf_b : &uap->dmarx.dbuf_a;
	rxchan->device->device_tx_status(rxchan, dmarx->cookie, &state);
	if (likely(state.residue < dmarx->last_residue)) {
		dmataken = dbuf->len - dmarx->last_residue;
		size = dmarx->last_residue - state.residue;
		dma_count = tty_insert_flip_string(port, dbuf->buf + dmataken,
						   size);
		if (dma_count == size)
			dmarx->last_residue =  state.residue;
		dmarx->last_jiffies = jiffies;
	}
	tty_flip_buffer_push(port);

	/*
	 * If no data is received in poll_timeout, the driver will fall back
	 * to interrupt mode. We will retrigger DMA at the first interrupt.
	 */
	if (jiffies_to_msecs(jiffies - dmarx->last_jiffies)
			> uap->dmarx.poll_timeout) {
		uart_port_lock_irqsave(&uap->port, &flags);
		pl011_dma_rx_stop(uap);
		uap->im |= UART011_RXIM;
		pl011_write(uap->im, uap, REG_IMSC);
		uart_port_unlock_irqrestore(&uap->port, flags);

		uap->dmarx.running = false;
		dmaengine_terminate_all(rxchan);
		timer_delete(&uap->dmarx.timer);
	} else {
		mod_timer(&uap->dmarx.timer,
			  jiffies + msecs_to_jiffies(uap->dmarx.poll_rate));
	}
}

static void pl011_dma_startup(struct uart_amba_port *uap)
{
	int ret;

	if (!uap->dma_probed)
		pl011_dma_probe(uap);

	if (!uap->dmatx.chan)
		return;

	uap->dmatx.buf = kmalloc(PL011_DMA_BUFFER_SIZE, GFP_KERNEL | __GFP_DMA);
	if (!uap->dmatx.buf) {
		uap->port.fifosize = uap->fifosize;
		return;
	}

	uap->dmatx.len = PL011_DMA_BUFFER_SIZE;

	/* The DMA buffer is now the FIFO the TTY subsystem can use */
	uap->port.fifosize = PL011_DMA_BUFFER_SIZE;
	uap->using_tx_dma = true;

	if (!uap->dmarx.chan)
		goto skip_rx;

	/* Allocate and map DMA RX buffers */
	ret = pl011_dmabuf_init(uap->dmarx.chan, &uap->dmarx.dbuf_a,
				DMA_FROM_DEVICE);
	if (ret) {
		dev_err(uap->port.dev, "failed to init DMA %s: %d\n",
			"RX buffer A", ret);
		goto skip_rx;
	}

	ret = pl011_dmabuf_init(uap->dmarx.chan, &uap->dmarx.dbuf_b,
				DMA_FROM_DEVICE);
	if (ret) {
		dev_err(uap->port.dev, "failed to init DMA %s: %d\n",
			"RX buffer B", ret);
		pl011_dmabuf_free(uap->dmarx.chan, &uap->dmarx.dbuf_a,
				  DMA_FROM_DEVICE);
		goto skip_rx;
	}

	uap->using_rx_dma = true;

skip_rx:
	/* Turn on DMA error (RX/TX will be enabled on demand) */
	uap->dmacr |= UART011_DMAONERR;
	pl011_write(uap->dmacr, uap, REG_DMACR);

	/*
	 * ST Micro variants has some specific dma burst threshold
	 * compensation. Set this to 16 bytes, so burst will only
	 * be issued above/below 16 bytes.
	 */
	if (uap->vendor->dma_threshold)
		pl011_write(ST_UART011_DMAWM_RX_16 | ST_UART011_DMAWM_TX_16,
			    uap, REG_ST_DMAWM);

	if (uap->using_rx_dma) {
		if (pl011_dma_rx_trigger_dma(uap))
			dev_dbg(uap->port.dev,
				"could not trigger initial RX DMA job, fall back to interrupt mode\n");
		if (uap->dmarx.poll_rate) {
			timer_setup(&uap->dmarx.timer, pl011_dma_rx_poll, 0);
			mod_timer(&uap->dmarx.timer,
				  jiffies + msecs_to_jiffies(uap->dmarx.poll_rate));
			uap->dmarx.last_residue = PL011_DMA_BUFFER_SIZE;
			uap->dmarx.last_jiffies = jiffies;
		}
	}
}

static void pl011_dma_shutdown(struct uart_amba_port *uap)
{
	if (!(uap->using_tx_dma || uap->using_rx_dma))
		return;

	/* Disable RX and TX DMA */
	while (pl011_read(uap, REG_FR) & uap->vendor->fr_busy)
		cpu_relax();

	uart_port_lock_irq(&uap->port);
	uap->dmacr &= ~(UART011_DMAONERR | UART011_RXDMAE | UART011_TXDMAE);
	pl011_write(uap->dmacr, uap, REG_DMACR);
	uart_port_unlock_irq(&uap->port);

	if (uap->using_tx_dma) {
		/* In theory, this should already be done by pl011_dma_flush_buffer */
		dmaengine_terminate_all(uap->dmatx.chan);
		if (uap->dmatx.queued) {
			dma_unmap_single(uap->dmatx.chan->device->dev,
					 uap->dmatx.dma, uap->dmatx.len,
					 DMA_TO_DEVICE);
			uap->dmatx.queued = false;
		}

		kfree(uap->dmatx.buf);
		uap->using_tx_dma = false;
	}

	if (uap->using_rx_dma) {
		dmaengine_terminate_all(uap->dmarx.chan);
		/* Clean up the RX DMA */
		pl011_dmabuf_free(uap->dmarx.chan, &uap->dmarx.dbuf_a, DMA_FROM_DEVICE);
		pl011_dmabuf_free(uap->dmarx.chan, &uap->dmarx.dbuf_b, DMA_FROM_DEVICE);
		if (uap->dmarx.poll_rate)
			timer_delete_sync(&uap->dmarx.timer);
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

static void pl011_rs485_tx_stop(struct uart_amba_port *uap)
{
	struct uart_port *port = &uap->port;
	u32 cr;

	if (uap->rs485_tx_state == SEND)
		uap->rs485_tx_state = WAIT_AFTER_SEND;

	if (uap->rs485_tx_state == WAIT_AFTER_SEND) {
		/* Schedule hrtimer if tx queue not empty */
		if (!pl011_tx_empty(port)) {
			hrtimer_start(&uap->trigger_stop_tx,
				      uap->rs485_tx_drain_interval,
				      HRTIMER_MODE_REL);
			return;
		}
		if (port->rs485.delay_rts_after_send > 0) {
			hrtimer_start(&uap->trigger_stop_tx,
				      ms_to_ktime(port->rs485.delay_rts_after_send),
				      HRTIMER_MODE_REL);
			return;
		}
		/* Continue without any delay */
	} else if (uap->rs485_tx_state == WAIT_AFTER_RTS) {
		hrtimer_try_to_cancel(&uap->trigger_start_tx);
	}

	cr = pl011_read(uap, REG_CR);

	if (port->rs485.flags & SER_RS485_RTS_AFTER_SEND)
		cr &= ~UART011_CR_RTS;
	else
		cr |= UART011_CR_RTS;

	/* Disable the transmitter and reenable the transceiver */
	cr &= ~UART011_CR_TXE;
	cr |= UART011_CR_RXE;
	pl011_write(cr, uap, REG_CR);

	uap->rs485_tx_state = OFF;
}

static void pl011_stop_tx(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);

	if (port->rs485.flags & SER_RS485_ENABLED &&
	    uap->rs485_tx_state == WAIT_AFTER_RTS) {
		pl011_rs485_tx_stop(uap);
		return;
	}

	uap->im &= ~UART011_TXIM;
	pl011_write(uap->im, uap, REG_IMSC);
	pl011_dma_tx_stop(uap);

	if (port->rs485.flags & SER_RS485_ENABLED &&
	    uap->rs485_tx_state != OFF)
		pl011_rs485_tx_stop(uap);
}

static bool pl011_tx_chars(struct uart_amba_port *uap, bool from_irq);

/* Start TX with programmed I/O only (no DMA) */
static void pl011_start_tx_pio(struct uart_amba_port *uap)
{
	if (pl011_tx_chars(uap, false)) {
		uap->im |= UART011_TXIM;
		pl011_write(uap->im, uap, REG_IMSC);
	}
}

static void pl011_rs485_tx_start(struct uart_amba_port *uap)
{
	struct uart_port *port = &uap->port;
	u32 cr;

	if (uap->rs485_tx_state == WAIT_AFTER_RTS) {
		uap->rs485_tx_state = SEND;
		return;
	}
	if (uap->rs485_tx_state == WAIT_AFTER_SEND) {
		hrtimer_try_to_cancel(&uap->trigger_stop_tx);
		uap->rs485_tx_state = SEND;
		return;
	}
	/* uap->rs485_tx_state == OFF */
	/* Enable transmitter */
	cr = pl011_read(uap, REG_CR);
	cr |= UART011_CR_TXE;
	/* Disable receiver if half-duplex */
	if (!(port->rs485.flags & SER_RS485_RX_DURING_TX))
		cr &= ~UART011_CR_RXE;

	if (port->rs485.flags & SER_RS485_RTS_ON_SEND)
		cr &= ~UART011_CR_RTS;
	else
		cr |= UART011_CR_RTS;

	pl011_write(cr, uap, REG_CR);

	if (port->rs485.delay_rts_before_send > 0) {
		uap->rs485_tx_state = WAIT_AFTER_RTS;
		hrtimer_start(&uap->trigger_start_tx,
			      ms_to_ktime(port->rs485.delay_rts_before_send),
			      HRTIMER_MODE_REL);
	} else {
		uap->rs485_tx_state = SEND;
	}
}

static void pl011_start_tx(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);

	if ((uap->port.rs485.flags & SER_RS485_ENABLED) &&
	    uap->rs485_tx_state != SEND) {
		pl011_rs485_tx_start(uap);
		if (uap->rs485_tx_state == WAIT_AFTER_RTS)
			return;
	}

	if (!pl011_dma_tx_start(uap))
		pl011_start_tx_pio(uap);
}

static enum hrtimer_restart pl011_trigger_start_tx(struct hrtimer *t)
{
	struct uart_amba_port *uap =
	    container_of(t, struct uart_amba_port, trigger_start_tx);
	unsigned long flags;

	uart_port_lock_irqsave(&uap->port, &flags);
	if (uap->rs485_tx_state == WAIT_AFTER_RTS)
		pl011_start_tx(&uap->port);
	uart_port_unlock_irqrestore(&uap->port, flags);

	return HRTIMER_NORESTART;
}

static enum hrtimer_restart pl011_trigger_stop_tx(struct hrtimer *t)
{
	struct uart_amba_port *uap =
	    container_of(t, struct uart_amba_port, trigger_stop_tx);
	unsigned long flags;

	uart_port_lock_irqsave(&uap->port, &flags);
	if (uap->rs485_tx_state == WAIT_AFTER_SEND)
		pl011_rs485_tx_stop(uap);
	uart_port_unlock_irqrestore(&uap->port, flags);

	return HRTIMER_NORESTART;
}

static void pl011_stop_rx(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);

	uap->im &= ~(UART011_RXIM | UART011_RTIM | UART011_FEIM |
		     UART011_PEIM | UART011_BEIM | UART011_OEIM);
	pl011_write(uap->im, uap, REG_IMSC);

	pl011_dma_rx_stop(uap);
}

static void pl011_throttle_rx(struct uart_port *port)
{
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	pl011_stop_rx(port);
	uart_port_unlock_irqrestore(port, flags);
}

static void pl011_enable_ms(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);

	uap->im |= UART011_RIMIM | UART011_CTSMIM | UART011_DCDMIM | UART011_DSRMIM;
	pl011_write(uap->im, uap, REG_IMSC);
}

static void pl011_rx_chars(struct uart_amba_port *uap)
__releases(&uap->port.lock)
__acquires(&uap->port.lock)
{
	pl011_fifo_to_tty(uap);

	uart_port_unlock(&uap->port);
	tty_flip_buffer_push(&uap->port.state->port);
	/*
	 * If we were temporarily out of DMA mode for a while,
	 * attempt to switch back to DMA mode again.
	 */
	if (pl011_dma_rx_available(uap)) {
		if (pl011_dma_rx_trigger_dma(uap)) {
			dev_dbg(uap->port.dev,
				"could not trigger RX DMA job fall back to interrupt mode again\n");
			uap->im |= UART011_RXIM;
			pl011_write(uap->im, uap, REG_IMSC);
		} else {
#ifdef CONFIG_DMA_ENGINE
			/* Start Rx DMA poll */
			if (uap->dmarx.poll_rate) {
				uap->dmarx.last_jiffies = jiffies;
				uap->dmarx.last_residue	= PL011_DMA_BUFFER_SIZE;
				mod_timer(&uap->dmarx.timer,
					  jiffies + msecs_to_jiffies(uap->dmarx.poll_rate));
			}
#endif
		}
	}
	uart_port_lock(&uap->port);
}

static bool pl011_tx_char(struct uart_amba_port *uap, unsigned char c,
			  bool from_irq)
{
	if (unlikely(!from_irq) &&
	    pl011_read(uap, REG_FR) & UART01x_FR_TXFF)
		return false; /* unable to transmit character */

	pl011_write(c, uap, REG_DR);
	uap->port.icount.tx++;

	return true;
}

/* Returns true if tx interrupts have to be (kept) enabled  */
static bool pl011_tx_chars(struct uart_amba_port *uap, bool from_irq)
{
	struct tty_port *tport = &uap->port.state->port;
	int count = uap->fifosize >> 1;

	if (uap->port.x_char) {
		if (!pl011_tx_char(uap, uap->port.x_char, from_irq))
			return true;
		uap->port.x_char = 0;
		--count;
	}
	if (kfifo_is_empty(&tport->xmit_fifo) || uart_tx_stopped(&uap->port)) {
		pl011_stop_tx(&uap->port);
		return false;
	}

	/* If we are using DMA mode, try to send some characters. */
	if (pl011_dma_tx_irq(uap))
		return true;

	while (1) {
		unsigned char c;

		if (likely(from_irq) && count-- == 0)
			break;

		if (!kfifo_peek(&tport->xmit_fifo, &c))
			break;

		if (!pl011_tx_char(uap, c, from_irq))
			break;

		kfifo_skip(&tport->xmit_fifo);
	}

	if (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
		uart_write_wakeup(&uap->port);

	if (kfifo_is_empty(&tport->xmit_fifo)) {
		pl011_stop_tx(&uap->port);
		return false;
	}
	return true;
}

static void pl011_modem_status(struct uart_amba_port *uap)
{
	unsigned int status, delta;

	status = pl011_read(uap, REG_FR) & UART01x_FR_MODEM_ANY;

	delta = status ^ uap->old_status;
	uap->old_status = status;

	if (!delta)
		return;

	if (delta & UART01x_FR_DCD)
		uart_handle_dcd_change(&uap->port, status & UART01x_FR_DCD);

	if (delta & uap->vendor->fr_dsr)
		uap->port.icount.dsr++;

	if (delta & uap->vendor->fr_cts)
		uart_handle_cts_change(&uap->port,
				       status & uap->vendor->fr_cts);

	wake_up_interruptible(&uap->port.state->port.delta_msr_wait);
}

static void check_apply_cts_event_workaround(struct uart_amba_port *uap)
{
	if (!uap->vendor->cts_event_workaround)
		return;

	/* workaround to make sure that all bits are unlocked.. */
	pl011_write(0x00, uap, REG_ICR);

	/*
	 * WA: introduce 26ns(1 uart clk) delay before W1C;
	 * single apb access will incur 2 pclk(133.12Mhz) delay,
	 * so add 2 dummy reads
	 */
	pl011_read(uap, REG_ICR);
	pl011_read(uap, REG_ICR);
}

static irqreturn_t pl011_int(int irq, void *dev_id)
{
	struct uart_amba_port *uap = dev_id;
	unsigned int status, pass_counter = AMBA_ISR_PASS_LIMIT;
	int handled = 0;

	uart_port_lock(&uap->port);
	status = pl011_read(uap, REG_RIS) & uap->im;
	if (status) {
		do {
			check_apply_cts_event_workaround(uap);

			pl011_write(status & ~(UART011_TXIS | UART011_RTIS | UART011_RXIS),
				    uap, REG_ICR);

			if (status & (UART011_RTIS | UART011_RXIS)) {
				if (pl011_dma_rx_running(uap))
					pl011_dma_rx_irq(uap);
				else
					pl011_rx_chars(uap);
			}
			if (status & (UART011_DSRMIS | UART011_DCDMIS |
				      UART011_CTSMIS | UART011_RIMIS))
				pl011_modem_status(uap);
			if (status & UART011_TXIS)
				pl011_tx_chars(uap, true);

			if (pass_counter-- == 0)
				break;

			status = pl011_read(uap, REG_RIS) & uap->im;
		} while (status != 0);
		handled = 1;
	}

	uart_unlock_and_check_sysrq(&uap->port);

	return IRQ_RETVAL(handled);
}

static unsigned int pl011_tx_empty(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);

	/* Allow feature register bits to be inverted to work around errata */
	unsigned int status = pl011_read(uap, REG_FR) ^ uap->vendor->inv_fr;

	return status & (uap->vendor->fr_busy | UART01x_FR_TXFF) ?
							0 : TIOCSER_TEMT;
}

static void pl011_maybe_set_bit(bool cond, unsigned int *ptr, unsigned int mask)
{
	if (cond)
		*ptr |= mask;
}

static unsigned int pl011_get_mctrl(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);
	unsigned int result = 0;
	unsigned int status = pl011_read(uap, REG_FR);

	pl011_maybe_set_bit(status & UART01x_FR_DCD, &result, TIOCM_CAR);
	pl011_maybe_set_bit(status & uap->vendor->fr_dsr, &result, TIOCM_DSR);
	pl011_maybe_set_bit(status & uap->vendor->fr_cts, &result, TIOCM_CTS);
	pl011_maybe_set_bit(status & uap->vendor->fr_ri, &result, TIOCM_RNG);

	return result;
}

static void pl011_assign_bit(bool cond, unsigned int *ptr, unsigned int mask)
{
	if (cond)
		*ptr |= mask;
	else
		*ptr &= ~mask;
}

static void pl011_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);
	unsigned int cr;

	cr = pl011_read(uap, REG_CR);

	pl011_assign_bit(mctrl & TIOCM_RTS, &cr, UART011_CR_RTS);
	pl011_assign_bit(mctrl & TIOCM_DTR, &cr, UART011_CR_DTR);
	pl011_assign_bit(mctrl & TIOCM_OUT1, &cr, UART011_CR_OUT1);
	pl011_assign_bit(mctrl & TIOCM_OUT2, &cr, UART011_CR_OUT2);
	pl011_assign_bit(mctrl & TIOCM_LOOP, &cr, UART011_CR_LBE);

	if (port->status & UPSTAT_AUTORTS) {
		/* We need to disable auto-RTS if we want to turn RTS off */
		pl011_assign_bit(mctrl & TIOCM_RTS, &cr, UART011_CR_RTSEN);
	}

	pl011_write(cr, uap, REG_CR);
}

static void pl011_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);
	unsigned long flags;
	unsigned int lcr_h;

	uart_port_lock_irqsave(&uap->port, &flags);
	lcr_h = pl011_read(uap, REG_LCRH_TX);
	if (break_state == -1)
		lcr_h |= UART01x_LCRH_BRK;
	else
		lcr_h &= ~UART01x_LCRH_BRK;
	pl011_write(lcr_h, uap, REG_LCRH_TX);
	uart_port_unlock_irqrestore(&uap->port, flags);
}

#ifdef CONFIG_CONSOLE_POLL

static void pl011_quiesce_irqs(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);

	pl011_write(pl011_read(uap, REG_MIS), uap, REG_ICR);
	/*
	 * There is no way to clear TXIM as this is "ready to transmit IRQ", so
	 * we simply mask it. start_tx() will unmask it.
	 *
	 * Note we can race with start_tx(), and if the race happens, the
	 * polling user might get another interrupt just after we clear it.
	 * But it should be OK and can happen even w/o the race, e.g.
	 * controller immediately got some new data and raised the IRQ.
	 *
	 * And whoever uses polling routines assumes that it manages the device
	 * (including tx queue), so we're also fine with start_tx()'s caller
	 * side.
	 */
	pl011_write(pl011_read(uap, REG_IMSC) & ~UART011_TXIM, uap,
		    REG_IMSC);
}

static int pl011_get_poll_char(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);
	unsigned int status;

	/*
	 * The caller might need IRQs lowered, e.g. if used with KDB NMI
	 * debugger.
	 */
	pl011_quiesce_irqs(port);

	status = pl011_read(uap, REG_FR);
	if (status & UART01x_FR_RXFE)
		return NO_POLL_CHAR;

	return pl011_read(uap, REG_DR);
}

static void pl011_put_poll_char(struct uart_port *port, unsigned char ch)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);

	while (pl011_read(uap, REG_FR) & UART01x_FR_TXFF)
		cpu_relax();

	pl011_write(ch, uap, REG_DR);
}

#endif /* CONFIG_CONSOLE_POLL */

static int pl011_hwinit(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);
	int retval;

	/* Optionaly enable pins to be muxed in and configured */
	pinctrl_pm_select_default_state(port->dev);

	/*
	 * Try to enable the clock producer.
	 */
	retval = clk_prepare_enable(uap->clk);
	if (retval)
		return retval;

	uap->port.uartclk = clk_get_rate(uap->clk);

	/* Clear pending error and receive interrupts */
	pl011_write(UART011_OEIS | UART011_BEIS | UART011_PEIS |
		    UART011_FEIS | UART011_RTIS | UART011_RXIS,
		    uap, REG_ICR);

	/*
	 * Save interrupts enable mask, and enable RX interrupts in case if
	 * the interrupt is used for NMI entry.
	 */
	uap->im = pl011_read(uap, REG_IMSC);
	pl011_write(UART011_RTIM | UART011_RXIM, uap, REG_IMSC);

	if (dev_get_platdata(uap->port.dev)) {
		struct amba_pl011_data *plat;

		plat = dev_get_platdata(uap->port.dev);
		if (plat->init)
			plat->init();
	}
	return 0;
}

static bool pl011_split_lcrh(const struct uart_amba_port *uap)
{
	return pl011_reg_to_offset(uap, REG_LCRH_RX) !=
	       pl011_reg_to_offset(uap, REG_LCRH_TX);
}

static void pl011_write_lcr_h(struct uart_amba_port *uap, unsigned int lcr_h)
{
	pl011_write(lcr_h, uap, REG_LCRH_RX);
	if (pl011_split_lcrh(uap)) {
		int i;
		/*
		 * Wait 10 PCLKs before writing LCRH_TX register,
		 * to get this delay write read only register 10 times
		 */
		for (i = 0; i < 10; ++i)
			pl011_write(0xff, uap, REG_MIS);
		pl011_write(lcr_h, uap, REG_LCRH_TX);
	}
}

static int pl011_allocate_irq(struct uart_amba_port *uap)
{
	pl011_write(uap->im, uap, REG_IMSC);

	return request_irq(uap->port.irq, pl011_int, IRQF_SHARED, "uart-pl011", uap);
}

/*
 * Enable interrupts, only timeouts when using DMA
 * if initial RX DMA job failed, start in interrupt mode
 * as well.
 */
static void pl011_enable_interrupts(struct uart_amba_port *uap)
{
	unsigned long flags;
	unsigned int i;

	uart_port_lock_irqsave(&uap->port, &flags);

	/* Clear out any spuriously appearing RX interrupts */
	pl011_write(UART011_RTIS | UART011_RXIS, uap, REG_ICR);

	/*
	 * RXIS is asserted only when the RX FIFO transitions from below
	 * to above the trigger threshold.  If the RX FIFO is already
	 * full to the threshold this can't happen and RXIS will now be
	 * stuck off.  Drain the RX FIFO explicitly to fix this:
	 */
	for (i = 0; i < uap->fifosize * 2; ++i) {
		if (pl011_read(uap, REG_FR) & UART01x_FR_RXFE)
			break;

		pl011_read(uap, REG_DR);
	}

	uap->im = UART011_RTIM;
	if (!pl011_dma_rx_running(uap))
		uap->im |= UART011_RXIM;
	pl011_write(uap->im, uap, REG_IMSC);
	uart_port_unlock_irqrestore(&uap->port, flags);
}

static void pl011_unthrottle_rx(struct uart_port *port)
{
	struct uart_amba_port *uap = container_of(port, struct uart_amba_port, port);
	unsigned long flags;

	uart_port_lock_irqsave(&uap->port, &flags);

	uap->im = UART011_RTIM;
	if (!pl011_dma_rx_running(uap))
		uap->im |= UART011_RXIM;

	pl011_write(uap->im, uap, REG_IMSC);

#ifdef CONFIG_DMA_ENGINE
	if (uap->using_rx_dma) {
		uap->dmacr |= UART011_RXDMAE;
		pl011_write(uap->dmacr, uap, REG_DMACR);
	}
#endif

	uart_port_unlock_irqrestore(&uap->port, flags);
}

static int pl011_startup(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);
	unsigned int cr;
	int retval;

	retval = pl011_hwinit(port);
	if (retval)
		goto clk_dis;

	retval = pl011_allocate_irq(uap);
	if (retval)
		goto clk_dis;

	pl011_write(uap->vendor->ifls, uap, REG_IFLS);

	uart_port_lock_irq(&uap->port);

	cr = pl011_read(uap, REG_CR);
	cr &= UART011_CR_RTS | UART011_CR_DTR;
	cr |= UART01x_CR_UARTEN | UART011_CR_RXE;

	if (!(port->rs485.flags & SER_RS485_ENABLED))
		cr |= UART011_CR_TXE;

	pl011_write(cr, uap, REG_CR);

	uart_port_unlock_irq(&uap->port);

	/*
	 * initialise the old status of the modem signals
	 */
	uap->old_status = pl011_read(uap, REG_FR) & UART01x_FR_MODEM_ANY;

	/* Startup DMA */
	pl011_dma_startup(uap);

	pl011_enable_interrupts(uap);

	return 0;

 clk_dis:
	clk_disable_unprepare(uap->clk);
	return retval;
}

static int sbsa_uart_startup(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);
	int retval;

	retval = pl011_hwinit(port);
	if (retval)
		return retval;

	retval = pl011_allocate_irq(uap);
	if (retval)
		return retval;

	/* The SBSA UART does not support any modem status lines. */
	uap->old_status = 0;

	pl011_enable_interrupts(uap);

	return 0;
}

static void pl011_shutdown_channel(struct uart_amba_port *uap, unsigned int lcrh)
{
	unsigned long val;

	val = pl011_read(uap, lcrh);
	val &= ~(UART01x_LCRH_BRK | UART01x_LCRH_FEN);
	pl011_write(val, uap, lcrh);
}

/*
 * disable the port. It should not disable RTS and DTR.
 * Also RTS and DTR state should be preserved to restore
 * it during startup().
 */
static void pl011_disable_uart(struct uart_amba_port *uap)
{
	unsigned int cr;

	uap->port.status &= ~(UPSTAT_AUTOCTS | UPSTAT_AUTORTS);
	uart_port_lock_irq(&uap->port);
	cr = pl011_read(uap, REG_CR);
	cr &= UART011_CR_RTS | UART011_CR_DTR;
	cr |= UART01x_CR_UARTEN | UART011_CR_TXE;
	pl011_write(cr, uap, REG_CR);
	uart_port_unlock_irq(&uap->port);

	/*
	 * disable break condition and fifos
	 */
	pl011_shutdown_channel(uap, REG_LCRH_RX);
	if (pl011_split_lcrh(uap))
		pl011_shutdown_channel(uap, REG_LCRH_TX);
}

static void pl011_disable_interrupts(struct uart_amba_port *uap)
{
	uart_port_lock_irq(&uap->port);

	/* mask all interrupts and clear all pending ones */
	uap->im = 0;
	pl011_write(uap->im, uap, REG_IMSC);
	pl011_write(0xffff, uap, REG_ICR);

	uart_port_unlock_irq(&uap->port);
}

static void pl011_shutdown(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);

	pl011_disable_interrupts(uap);

	pl011_dma_shutdown(uap);

	if ((port->rs485.flags & SER_RS485_ENABLED && uap->rs485_tx_state != OFF))
		pl011_rs485_tx_stop(uap);

	free_irq(uap->port.irq, uap);

	pl011_disable_uart(uap);

	/*
	 * Shut down the clock producer
	 */
	clk_disable_unprepare(uap->clk);
	/* Optionally let pins go into sleep states */
	pinctrl_pm_select_sleep_state(port->dev);

	if (dev_get_platdata(uap->port.dev)) {
		struct amba_pl011_data *plat;

		plat = dev_get_platdata(uap->port.dev);
		if (plat->exit)
			plat->exit();
	}

	if (uap->port.ops->flush_buffer)
		uap->port.ops->flush_buffer(port);
}

static void sbsa_uart_shutdown(struct uart_port *port)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);

	pl011_disable_interrupts(uap);

	free_irq(uap->port.irq, uap);

	if (uap->port.ops->flush_buffer)
		uap->port.ops->flush_buffer(port);
}

static void
pl011_setup_status_masks(struct uart_port *port, struct ktermios *termios)
{
	port->read_status_mask = UART011_DR_OE | 255;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= UART011_DR_FE | UART011_DR_PE;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
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
}

static void
pl011_set_termios(struct uart_port *port, struct ktermios *termios,
		  const struct ktermios *old)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);
	unsigned int lcr_h, old_cr;
	unsigned long flags;
	unsigned int baud, quot, clkdiv;
	unsigned int bits;

	if (uap->vendor->oversampling)
		clkdiv = 8;
	else
		clkdiv = 16;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0,
				  port->uartclk / clkdiv);
#ifdef CONFIG_DMA_ENGINE
	/*
	 * Adjust RX DMA polling rate with baud rate if not specified.
	 */
	if (uap->dmarx.auto_poll_rate)
		uap->dmarx.poll_rate = DIV_ROUND_UP(10000000, baud);
#endif

	if (baud > port->uartclk / 16)
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
		if (termios->c_cflag & CMSPAR)
			lcr_h |= UART011_LCRH_SPS;
	}
	if (uap->fifosize > 1)
		lcr_h |= UART01x_LCRH_FEN;

	bits = tty_get_frame_size(termios->c_cflag);

	uart_port_lock_irqsave(port, &flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * Calculate the approximated time it takes to transmit one character
	 * with the given baud rate. We use this as the poll interval when we
	 * wait for the tx queue to empty.
	 */
	uap->rs485_tx_drain_interval = ns_to_ktime(DIV_ROUND_UP(bits * NSEC_PER_SEC, baud));

	pl011_setup_status_masks(port, termios);

	if (UART_ENABLE_MS(port, termios->c_cflag))
		pl011_enable_ms(port);

	if (port->rs485.flags & SER_RS485_ENABLED)
		termios->c_cflag &= ~CRTSCTS;

	old_cr = pl011_read(uap, REG_CR);

	if (termios->c_cflag & CRTSCTS) {
		if (old_cr & UART011_CR_RTS)
			old_cr |= UART011_CR_RTSEN;

		old_cr |= UART011_CR_CTSEN;
		port->status |= UPSTAT_AUTOCTS | UPSTAT_AUTORTS;
	} else {
		old_cr &= ~(UART011_CR_CTSEN | UART011_CR_RTSEN);
		port->status &= ~(UPSTAT_AUTOCTS | UPSTAT_AUTORTS);
	}

	if (uap->vendor->oversampling) {
		if (baud > port->uartclk / 16)
			old_cr |= ST_UART011_CR_OVSFACT;
		else
			old_cr &= ~ST_UART011_CR_OVSFACT;
	}

	/*
	 * Workaround for the ST Micro oversampling variants to
	 * increase the bitrate slightly, by lowering the divisor,
	 * to avoid delayed sampling of start bit at high speeds,
	 * else we see data corruption.
	 */
	if (uap->vendor->oversampling) {
		if (baud >= 3000000 && baud < 3250000 && quot > 1)
			quot -= 1;
		else if (baud > 3250000 && quot > 2)
			quot -= 2;
	}
	/* Set baud rate */
	pl011_write(quot & 0x3f, uap, REG_FBRD);
	pl011_write(quot >> 6, uap, REG_IBRD);

	/*
	 * ----------v----------v----------v----------v-----
	 * NOTE: REG_LCRH_TX and REG_LCRH_RX MUST BE WRITTEN AFTER
	 * REG_FBRD & REG_IBRD.
	 * ----------^----------^----------^----------^-----
	 */
	pl011_write_lcr_h(uap, lcr_h);

	/*
	 * Receive was disabled by pl011_disable_uart during shutdown.
	 * Need to reenable receive if you need to use a tty_driver
	 * returns from tty_find_polling_driver() after a port shutdown.
	 */
	old_cr |= UART011_CR_RXE;
	pl011_write(old_cr, uap, REG_CR);

	uart_port_unlock_irqrestore(port, flags);
}

static void
sbsa_uart_set_termios(struct uart_port *port, struct ktermios *termios,
		      const struct ktermios *old)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);
	unsigned long flags;

	tty_termios_encode_baud_rate(termios, uap->fixed_baud, uap->fixed_baud);

	/* The SBSA UART only supports 8n1 without hardware flow control. */
	termios->c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD);
	termios->c_cflag &= ~(CMSPAR | CRTSCTS);
	termios->c_cflag |= CS8 | CLOCAL;

	uart_port_lock_irqsave(port, &flags);
	uart_update_timeout(port, CS8, uap->fixed_baud);
	pl011_setup_status_masks(port, termios);
	uart_port_unlock_irqrestore(port, flags);
}

static const char *pl011_type(struct uart_port *port)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);
	return uap->port.type == PORT_AMBA ? uap->type : NULL;
}

/*
 * Configure/autoconfigure the port.
 */
static void pl011_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_AMBA;
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int pl011_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_AMBA)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= irq_get_nr_irqs())
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	if (port->mapbase != (unsigned long)ser->iomem_base)
		ret = -EINVAL;
	return ret;
}

static int pl011_rs485_config(struct uart_port *port, struct ktermios *termios,
			      struct serial_rs485 *rs485)
{
	struct uart_amba_port *uap =
		container_of(port, struct uart_amba_port, port);

	if (port->rs485.flags & SER_RS485_ENABLED)
		pl011_rs485_tx_stop(uap);

	/* Make sure auto RTS is disabled */
	if (rs485->flags & SER_RS485_ENABLED) {
		u32 cr = pl011_read(uap, REG_CR);

		cr &= ~UART011_CR_RTSEN;
		pl011_write(cr, uap, REG_CR);
		port->status &= ~UPSTAT_AUTORTS;
	}

	return 0;
}

static const struct uart_ops amba_pl011_pops = {
	.tx_empty	= pl011_tx_empty,
	.set_mctrl	= pl011_set_mctrl,
	.get_mctrl	= pl011_get_mctrl,
	.stop_tx	= pl011_stop_tx,
	.start_tx	= pl011_start_tx,
	.stop_rx	= pl011_stop_rx,
	.throttle	= pl011_throttle_rx,
	.unthrottle	= pl011_unthrottle_rx,
	.enable_ms	= pl011_enable_ms,
	.break_ctl	= pl011_break_ctl,
	.startup	= pl011_startup,
	.shutdown	= pl011_shutdown,
	.flush_buffer	= pl011_dma_flush_buffer,
	.set_termios	= pl011_set_termios,
	.type		= pl011_type,
	.config_port	= pl011_config_port,
	.verify_port	= pl011_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init     = pl011_hwinit,
	.poll_get_char = pl011_get_poll_char,
	.poll_put_char = pl011_put_poll_char,
#endif
};

static void sbsa_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int sbsa_uart_get_mctrl(struct uart_port *port)
{
	return 0;
}

static const struct uart_ops sbsa_uart_pops = {
	.tx_empty	= pl011_tx_empty,
	.set_mctrl	= sbsa_uart_set_mctrl,
	.get_mctrl	= sbsa_uart_get_mctrl,
	.stop_tx	= pl011_stop_tx,
	.start_tx	= pl011_start_tx,
	.stop_rx	= pl011_stop_rx,
	.startup	= sbsa_uart_startup,
	.shutdown	= sbsa_uart_shutdown,
	.set_termios	= sbsa_uart_set_termios,
	.type		= pl011_type,
	.config_port	= pl011_config_port,
	.verify_port	= pl011_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init     = pl011_hwinit,
	.poll_get_char = pl011_get_poll_char,
	.poll_put_char = pl011_put_poll_char,
#endif
};

static struct uart_amba_port *amba_ports[UART_NR];

#ifdef CONFIG_SERIAL_AMBA_PL011_CONSOLE

static void pl011_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct uart_amba_port *uap =
	    container_of(port, struct uart_amba_port, port);

	while (pl011_read(uap, REG_FR) & UART01x_FR_TXFF)
		cpu_relax();
	pl011_write(ch, uap, REG_DR);
	uap->console_line_ended = (ch == '\n');
}

static void pl011_console_get_options(struct uart_amba_port *uap, int *baud,
				      int *parity, int *bits)
{
	unsigned int lcr_h, ibrd, fbrd;

	if (!(pl011_read(uap, REG_CR) & UART01x_CR_UARTEN))
		return;

	lcr_h = pl011_read(uap, REG_LCRH_TX);

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

	ibrd = pl011_read(uap, REG_IBRD);
	fbrd = pl011_read(uap, REG_FBRD);

	*baud = uap->port.uartclk * 4 / (64 * ibrd + fbrd);

	if (uap->vendor->oversampling &&
	    (pl011_read(uap, REG_CR) & ST_UART011_CR_OVSFACT))
		*baud *= 2;
}

static int pl011_console_setup(struct console *co, char *options)
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
	pinctrl_pm_select_default_state(uap->port.dev);

	ret = clk_prepare(uap->clk);
	if (ret)
		return ret;

	uap->console_line_ended = true;

	if (dev_get_platdata(uap->port.dev)) {
		struct amba_pl011_data *plat;

		plat = dev_get_platdata(uap->port.dev);
		if (plat->init)
			plat->init();
	}

	uap->port.uartclk = clk_get_rate(uap->clk);

	if (uap->vendor->fixed_options) {
		baud = uap->fixed_baud;
	} else {
		if (options)
			uart_parse_options(options,
					   &baud, &parity, &bits, &flow);
		else
			pl011_console_get_options(uap, &baud, &parity, &bits);
	}

	return uart_set_options(&uap->port, co, baud, parity, bits, flow);
}

/**
 *	pl011_console_match - non-standard console matching
 *	@co:	  registering console
 *	@name:	  name from console command line
 *	@idx:	  index from console command line
 *	@options: ptr to option string from console command line
 *
 *	Only attempts to match console command lines of the form:
 *	    console=pl011,mmio|mmio32,<addr>[,<options>]
 *	    console=pl011,0x<addr>[,<options>]
 *	This form is used to register an initial earlycon boot console and
 *	replace it with the amba_console at pl011 driver init.
 *
 *	Performs console setup for a match (as required by interface)
 *	If no <options> are specified, then assume the h/w is already setup.
 *
 *	Returns 0 if console matches; otherwise non-zero to use default matching
 */
static int pl011_console_match(struct console *co, char *name, int idx,
			       char *options)
{
	unsigned char iotype;
	resource_size_t addr;
	int i;

	/*
	 * Systems affected by the Qualcomm Technologies QDF2400 E44 erratum
	 * have a distinct console name, so make sure we check for that.
	 * The actual implementation of the erratum occurs in the probe
	 * function.
	 */
	if ((strcmp(name, "qdf2400_e44") != 0) && (strcmp(name, "pl011") != 0))
		return -ENODEV;

	if (uart_parse_earlycon(options, &iotype, &addr, &options))
		return -ENODEV;

	if (iotype != UPIO_MEM && iotype != UPIO_MEM32)
		return -ENODEV;

	/* try to match the port specified on the command line */
	for (i = 0; i < ARRAY_SIZE(amba_ports); i++) {
		struct uart_port *port;

		if (!amba_ports[i])
			continue;

		port = &amba_ports[i]->port;

		if (port->mapbase != addr)
			continue;

		co->index = i;
		uart_port_set_cons(port, co);
		return pl011_console_setup(co, options);
	}

	return -ENODEV;
}

static void
pl011_console_write_atomic(struct console *co, struct nbcon_write_context *wctxt)
{
	struct uart_amba_port *uap = amba_ports[co->index];
	unsigned int old_cr = 0;

	if (!nbcon_enter_unsafe(wctxt))
		return;

	clk_enable(uap->clk);

	if (!uap->vendor->always_enabled) {
		old_cr = pl011_read(uap, REG_CR);
		pl011_write((old_cr & ~UART011_CR_CTSEN) | (UART01x_CR_UARTEN | UART011_CR_TXE),
				uap, REG_CR);
	}

	if (!uap->console_line_ended)
		uart_console_write(&uap->port, "\n", 1, pl011_console_putchar);
	uart_console_write(&uap->port, wctxt->outbuf, wctxt->len, pl011_console_putchar);

	while ((pl011_read(uap, REG_FR) ^ uap->vendor->inv_fr) & uap->vendor->fr_busy)
		cpu_relax();

	if (!uap->vendor->always_enabled)
		pl011_write(old_cr, uap, REG_CR);

	clk_disable(uap->clk);

	nbcon_exit_unsafe(wctxt);
}

static void
pl011_console_write_thread(struct console *co, struct nbcon_write_context *wctxt)
{
	struct uart_amba_port *uap = amba_ports[co->index];
	unsigned int old_cr = 0;

	if (!nbcon_enter_unsafe(wctxt))
		return;

	clk_enable(uap->clk);

	if (!uap->vendor->always_enabled) {
		old_cr = pl011_read(uap, REG_CR);
		pl011_write((old_cr & ~UART011_CR_CTSEN) | (UART01x_CR_UARTEN | UART011_CR_TXE),
				uap, REG_CR);
	}

	if (nbcon_exit_unsafe(wctxt)) {
		int i;
		unsigned int len = READ_ONCE(wctxt->len);

		for (i = 0; i < len; i++) {
			if (!nbcon_enter_unsafe(wctxt))
				break;
			uart_console_write(&uap->port, wctxt->outbuf + i, 1, pl011_console_putchar);
			if (!nbcon_exit_unsafe(wctxt))
				break;
		}
	}

	while (!nbcon_enter_unsafe(wctxt))
		nbcon_reacquire_nobuf(wctxt);

	while ((pl011_read(uap, REG_FR) ^ uap->vendor->inv_fr) & uap->vendor->fr_busy)
		cpu_relax();

	if (!uap->vendor->always_enabled)
		pl011_write(old_cr, uap, REG_CR);

	clk_disable(uap->clk);

	nbcon_exit_unsafe(wctxt);
}

static void
pl011_console_device_lock(struct console *co, unsigned long *flags)
{
	__uart_port_lock_irqsave(&amba_ports[co->index]->port, flags);
}

static void
pl011_console_device_unlock(struct console *co, unsigned long flags)
{
	__uart_port_unlock_irqrestore(&amba_ports[co->index]->port, flags);
}

static struct uart_driver amba_reg;
static struct console amba_console = {
	.name		= "ttyAMA",
	.device		= uart_console_device,
	.setup		= pl011_console_setup,
	.match		= pl011_console_match,
	.write_atomic	= pl011_console_write_atomic,
	.write_thread	= pl011_console_write_thread,
	.device_lock	= pl011_console_device_lock,
	.device_unlock	= pl011_console_device_unlock,
	.flags		= CON_PRINTBUFFER | CON_ANYTIME | CON_NBCON,
	.index		= -1,
	.data		= &amba_reg,
};

#define AMBA_CONSOLE	(&amba_console)

static void qdf2400_e44_putc(struct uart_port *port, unsigned char c)
{
	while (readl(port->membase + UART01x_FR) & UART01x_FR_TXFF)
		cpu_relax();
	writel(c, port->membase + UART01x_DR);
	while (!(readl(port->membase + UART01x_FR) & UART011_FR_TXFE))
		cpu_relax();
}

static void qdf2400_e44_early_write(struct console *con, const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, qdf2400_e44_putc);
}

static void pl011_putc(struct uart_port *port, unsigned char c)
{
	while (readl(port->membase + UART01x_FR) & UART01x_FR_TXFF)
		cpu_relax();
	if (port->iotype == UPIO_MEM32)
		writel(c, port->membase + UART01x_DR);
	else
		writeb(c, port->membase + UART01x_DR);
	while (readl(port->membase + UART01x_FR) & UART01x_FR_BUSY)
		cpu_relax();
}

static void pl011_early_write(struct console *con, const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, pl011_putc);
}

#ifdef CONFIG_CONSOLE_POLL
static int pl011_getc(struct uart_port *port)
{
	if (readl(port->membase + UART01x_FR) & UART01x_FR_RXFE)
		return NO_POLL_CHAR;

	if (port->iotype == UPIO_MEM32)
		return readl(port->membase + UART01x_DR);
	else
		return readb(port->membase + UART01x_DR);
}

static int pl011_early_read(struct console *con, char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;
	int ch, num_read = 0;

	while (num_read < n) {
		ch = pl011_getc(&dev->port);
		if (ch == NO_POLL_CHAR)
			break;

		s[num_read++] = ch;
	}

	return num_read;
}
#else
#define pl011_early_read NULL
#endif

/*
 * On non-ACPI systems, earlycon is enabled by specifying
 * "earlycon=pl011,<address>" on the kernel command line.
 *
 * On ACPI ARM64 systems, an "early" console is enabled via the SPCR table,
 * by specifying only "earlycon" on the command line.  Because it requires
 * SPCR, the console starts after ACPI is parsed, which is later than a
 * traditional early console.
 *
 * To get the traditional early console that starts before ACPI is parsed,
 * specify the full "earlycon=pl011,<address>" option.
 */
static int __init pl011_early_console_setup(struct earlycon_device *device,
					    const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = pl011_early_write;
	device->con->read = pl011_early_read;

	return 0;
}

OF_EARLYCON_DECLARE(pl011, "arm,pl011", pl011_early_console_setup);

OF_EARLYCON_DECLARE(pl011, "arm,sbsa-uart", pl011_early_console_setup);

/*
 * On Qualcomm Datacenter Technologies QDF2400 SOCs affected by
 * Erratum 44, traditional earlycon can be enabled by specifying
 * "earlycon=qdf2400_e44,<address>".  Any options are ignored.
 *
 * Alternatively, you can just specify "earlycon", and the early console
 * will be enabled with the information from the SPCR table.  In this
 * case, the SPCR code will detect the need for the E44 work-around,
 * and set the console name to "qdf2400_e44".
 */
static int __init
qdf2400_e44_early_console_setup(struct earlycon_device *device,
				const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = qdf2400_e44_early_write;
	return 0;
}

EARLYCON_DECLARE(qdf2400_e44, qdf2400_e44_early_console_setup);

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

static int pl011_probe_dt_alias(int index, struct device *dev)
{
	struct device_node *np;
	static bool seen_dev_with_alias;
	static bool seen_dev_without_alias;
	int ret = index;

	if (!IS_ENABLED(CONFIG_OF))
		return ret;

	np = dev->of_node;
	if (!np)
		return ret;

	ret = of_alias_get_id(np, "serial");
	if (ret < 0) {
		seen_dev_without_alias = true;
		ret = index;
	} else {
		seen_dev_with_alias = true;
		if (ret >= ARRAY_SIZE(amba_ports) || amba_ports[ret]) {
			dev_warn(dev, "requested serial port %d  not available.\n", ret);
			ret = index;
		}
	}

	if (seen_dev_with_alias && seen_dev_without_alias)
		dev_warn(dev, "aliased and non-aliased serial devices found in device tree. Serial port enumeration may be unpredictable.\n");

	return ret;
}

/* unregisters the driver also if no more ports are left */
static void pl011_unregister_port(struct uart_amba_port *uap)
{
	int i;
	bool busy = false;

	for (i = 0; i < ARRAY_SIZE(amba_ports); i++) {
		if (amba_ports[i] == uap)
			amba_ports[i] = NULL;
		else if (amba_ports[i])
			busy = true;
	}
	pl011_dma_remove(uap);
	if (!busy)
		uart_unregister_driver(&amba_reg);
}

static int pl011_find_free_port(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(amba_ports); i++)
		if (!amba_ports[i])
			return i;

	return -EBUSY;
}

static int pl011_setup_port(struct device *dev, struct uart_amba_port *uap,
			    struct resource *mmiobase, int index)
{
	void __iomem *base;
	int ret;

	base = devm_ioremap_resource(dev, mmiobase);
	if (IS_ERR(base))
		return PTR_ERR(base);

	index = pl011_probe_dt_alias(index, dev);

	uap->port.dev = dev;
	uap->port.mapbase = mmiobase->start;
	uap->port.membase = base;
	uap->port.fifosize = uap->fifosize;
	uap->port.has_sysrq = IS_ENABLED(CONFIG_SERIAL_AMBA_PL011_CONSOLE);
	uap->port.flags = UPF_BOOT_AUTOCONF;
	uap->port.line = index;

	ret = uart_get_rs485_mode(&uap->port);
	if (ret)
		return ret;

	amba_ports[index] = uap;

	return 0;
}

static int pl011_register_port(struct uart_amba_port *uap)
{
	int ret, i;

	/* Ensure interrupts from this UART are masked and cleared */
	pl011_write(0, uap, REG_IMSC);
	pl011_write(0xffff, uap, REG_ICR);

	if (!amba_reg.state) {
		ret = uart_register_driver(&amba_reg);
		if (ret < 0) {
			dev_err(uap->port.dev,
				"Failed to register AMBA-PL011 driver\n");
			for (i = 0; i < ARRAY_SIZE(amba_ports); i++)
				if (amba_ports[i] == uap)
					amba_ports[i] = NULL;
			return ret;
		}
	}

	ret = uart_add_one_port(&amba_reg, &uap->port);
	if (ret)
		pl011_unregister_port(uap);

	return ret;
}

static const struct serial_rs485 pl011_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND |
		 SER_RS485_RX_DURING_TX,
	.delay_rts_before_send = 1,
	.delay_rts_after_send = 1,
};

static int pl011_probe(struct amba_device *dev, const struct amba_id *id)
{
	struct uart_amba_port *uap;
	struct vendor_data *vendor = id->data;
	int portnr, ret;
	u32 val;

	portnr = pl011_find_free_port();
	if (portnr < 0)
		return portnr;

	uap = devm_kzalloc(&dev->dev, sizeof(struct uart_amba_port),
			   GFP_KERNEL);
	if (!uap)
		return -ENOMEM;

	uap->clk = devm_clk_get(&dev->dev, NULL);
	if (IS_ERR(uap->clk))
		return PTR_ERR(uap->clk);

	uap->reg_offset = vendor->reg_offset;
	uap->vendor = vendor;
	uap->fifosize = vendor->get_fifosize(dev);
	uap->port.iotype = vendor->access_32b ? UPIO_MEM32 : UPIO_MEM;
	uap->port.irq = dev->irq[0];
	uap->port.ops = &amba_pl011_pops;
	uap->port.rs485_config = pl011_rs485_config;
	uap->port.rs485_supported = pl011_rs485_supported;
	snprintf(uap->type, sizeof(uap->type), "PL011 rev%u", amba_rev(dev));

	if (device_property_read_u32(&dev->dev, "reg-io-width", &val) == 0) {
		switch (val) {
		case 1:
			uap->port.iotype = UPIO_MEM;
			break;
		case 4:
			uap->port.iotype = UPIO_MEM32;
			break;
		default:
			dev_warn(&dev->dev, "unsupported reg-io-width (%d)\n",
				 val);
			return -EINVAL;
		}
	}
	hrtimer_setup(&uap->trigger_start_tx, pl011_trigger_start_tx, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);
	hrtimer_setup(&uap->trigger_stop_tx, pl011_trigger_stop_tx, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);

	ret = pl011_setup_port(&dev->dev, uap, &dev->res, portnr);
	if (ret)
		return ret;

	amba_set_drvdata(dev, uap);

	return pl011_register_port(uap);
}

static void pl011_remove(struct amba_device *dev)
{
	struct uart_amba_port *uap = amba_get_drvdata(dev);

	uart_remove_one_port(&amba_reg, &uap->port);
	pl011_unregister_port(uap);
}

#ifdef CONFIG_PM_SLEEP
static int pl011_suspend(struct device *dev)
{
	struct uart_amba_port *uap = dev_get_drvdata(dev);

	if (!uap)
		return -EINVAL;

	return uart_suspend_port(&amba_reg, &uap->port);
}

static int pl011_resume(struct device *dev)
{
	struct uart_amba_port *uap = dev_get_drvdata(dev);

	if (!uap)
		return -EINVAL;

	return uart_resume_port(&amba_reg, &uap->port);
}
#endif

static SIMPLE_DEV_PM_OPS(pl011_dev_pm_ops, pl011_suspend, pl011_resume);

#ifdef CONFIG_ACPI_SPCR_TABLE
static void qpdf2400_erratum44_workaround(struct device *dev,
					  struct uart_amba_port *uap)
{
	if (!qdf2400_e44_present)
		return;

	dev_info(dev, "working around QDF2400 SoC erratum 44\n");
	uap->vendor = &vendor_qdt_qdf2400_e44;
}
#else
static void qpdf2400_erratum44_workaround(struct device *dev,
					  struct uart_amba_port *uap)
{ /* empty */ }
#endif

static int sbsa_uart_probe(struct platform_device *pdev)
{
	struct uart_amba_port *uap;
	struct resource *r;
	int portnr, ret;
	int baudrate;

	/*
	 * Check the mandatory baud rate parameter in the DT node early
	 * so that we can easily exit with the error.
	 */
	if (pdev->dev.of_node) {
		struct device_node *np = pdev->dev.of_node;

		ret = of_property_read_u32(np, "current-speed", &baudrate);
		if (ret)
			return ret;
	} else {
		baudrate = 115200;
	}

	portnr = pl011_find_free_port();
	if (portnr < 0)
		return portnr;

	uap = devm_kzalloc(&pdev->dev, sizeof(struct uart_amba_port),
			   GFP_KERNEL);
	if (!uap)
		return -ENOMEM;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	uap->port.irq	= ret;

	uap->vendor = &vendor_sbsa;
	qpdf2400_erratum44_workaround(&pdev->dev, uap);

	uap->reg_offset	= uap->vendor->reg_offset;
	uap->fifosize	= 32;
	uap->port.iotype = uap->vendor->access_32b ? UPIO_MEM32 : UPIO_MEM;
	uap->port.ops	= &sbsa_uart_pops;
	uap->fixed_baud = baudrate;

	snprintf(uap->type, sizeof(uap->type), "SBSA");

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ret = pl011_setup_port(&pdev->dev, uap, r, portnr);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, uap);

	return pl011_register_port(uap);
}

static void sbsa_uart_remove(struct platform_device *pdev)
{
	struct uart_amba_port *uap = platform_get_drvdata(pdev);

	uart_remove_one_port(&amba_reg, &uap->port);
	pl011_unregister_port(uap);
}

static const struct of_device_id sbsa_uart_of_match[] = {
	{ .compatible = "arm,sbsa-uart", },
	{},
};
MODULE_DEVICE_TABLE(of, sbsa_uart_of_match);

static const struct acpi_device_id sbsa_uart_acpi_match[] = {
	{ "ARMH0011", 0 },
	{ "ARMHB000", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, sbsa_uart_acpi_match);

static struct platform_driver arm_sbsa_uart_platform_driver = {
	.probe		= sbsa_uart_probe,
	.remove		= sbsa_uart_remove,
	.driver	= {
		.name	= "sbsa-uart",
		.pm	= &pl011_dev_pm_ops,
		.of_match_table = sbsa_uart_of_match,
		.acpi_match_table = sbsa_uart_acpi_match,
		.suppress_bind_attrs = IS_BUILTIN(CONFIG_SERIAL_AMBA_PL011),
	},
};

static const struct amba_id pl011_ids[] = {
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
		.pm	= &pl011_dev_pm_ops,
		.suppress_bind_attrs = IS_BUILTIN(CONFIG_SERIAL_AMBA_PL011),
	},
	.id_table	= pl011_ids,
	.probe		= pl011_probe,
	.remove		= pl011_remove,
};

static int __init pl011_init(void)
{
	pr_info("Serial: AMBA PL011 UART driver\n");

	if (platform_driver_register(&arm_sbsa_uart_platform_driver))
		pr_warn("could not register SBSA UART platform driver\n");
	return amba_driver_register(&pl011_driver);
}

static void __exit pl011_exit(void)
{
	platform_driver_unregister(&arm_sbsa_uart_platform_driver);
	amba_driver_unregister(&pl011_driver);
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
