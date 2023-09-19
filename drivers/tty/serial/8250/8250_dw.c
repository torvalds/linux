// SPDX-License-Identifier: GPL-2.0+
/*
 * Synopsys DesignWare 8250 driver.
 *
 * Copyright 2011 Picochip, Jamie Iles.
 * Copyright 2013 Intel Corporation
 *
 * The Synopsys DesignWare 8250 has an extra feature whereby it detects if the
 * LCR is written whilst busy.  If it is, then a busy detect interrupt is
 * raised, the LCR needs to be rewritten and the uart status register read.
 */
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <asm/byteorder.h>

#include <linux/serial_8250.h>
#include <linux/serial_reg.h>

#include "8250_dwlib.h"

/* Offsets for the DesignWare specific registers */
#define DW_UART_USR	0x1f /* UART Status Register */
#define DW_UART_DMASA	0xa8 /* DMA Software Ack */

#define OCTEON_UART_USR	0x27 /* UART Status Register */

#define RZN1_UART_TDMACR 0x10c /* DMA Control Register Transmit Mode */
#define RZN1_UART_RDMACR 0x110 /* DMA Control Register Receive Mode */

/* DesignWare specific register fields */
#define DW_UART_MCR_SIRE		BIT(6)

/* Renesas specific register fields */
#define RZN1_UART_xDMACR_DMA_EN		BIT(0)
#define RZN1_UART_xDMACR_1_WORD_BURST	(0 << 1)
#define RZN1_UART_xDMACR_4_WORD_BURST	(1 << 1)
#define RZN1_UART_xDMACR_8_WORD_BURST	(2 << 1)
#define RZN1_UART_xDMACR_BLK_SZ(x)	((x) << 3)

/* Quirks */
#define DW_UART_QUIRK_OCTEON		BIT(0)
#define DW_UART_QUIRK_ARMADA_38X	BIT(1)
#define DW_UART_QUIRK_SKIP_SET_RATE	BIT(2)
#define DW_UART_QUIRK_IS_DMA_FC		BIT(3)

static inline struct dw8250_data *clk_to_dw8250_data(struct notifier_block *nb)
{
	return container_of(nb, struct dw8250_data, clk_notifier);
}

static inline struct dw8250_data *work_to_dw8250_data(struct work_struct *work)
{
	return container_of(work, struct dw8250_data, clk_work);
}

static inline int dw8250_modify_msr(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = to_dw8250_data(p->private_data);

	/* Override any modem control signals if needed */
	if (offset == UART_MSR) {
		value |= d->msr_mask_on;
		value &= ~d->msr_mask_off;
	}

	return value;
}

static void dw8250_force_idle(struct uart_port *p)
{
	struct uart_8250_port *up = up_to_u8250p(p);
	unsigned int lsr;

	serial8250_clear_and_reinit_fifos(up);

	/*
	 * With PSLVERR_RESP_EN parameter set to 1, the device generates an
	 * error response when an attempt to read an empty RBR with FIFO
	 * enabled.
	 */
	if (up->fcr & UART_FCR_ENABLE_FIFO) {
		lsr = p->serial_in(p, UART_LSR);
		if (!(lsr & UART_LSR_DR))
			return;
	}

	(void)p->serial_in(p, UART_RX);
}

static void dw8250_check_lcr(struct uart_port *p, int value)
{
	void __iomem *offset = p->membase + (UART_LCR << p->regshift);
	int tries = 1000;

	/* Make sure LCR write wasn't ignored */
	while (tries--) {
		unsigned int lcr = p->serial_in(p, UART_LCR);

		if ((value & ~UART_LCR_SPAR) == (lcr & ~UART_LCR_SPAR))
			return;

		dw8250_force_idle(p);

#ifdef CONFIG_64BIT
		if (p->type == PORT_OCTEON)
			__raw_writeq(value & 0xff, offset);
		else
#endif
		if (p->iotype == UPIO_MEM32)
			writel(value, offset);
		else if (p->iotype == UPIO_MEM32BE)
			iowrite32be(value, offset);
		else
			writeb(value, offset);
	}
	/*
	 * FIXME: this deadlocks if port->lock is already held
	 * dev_err(p->dev, "Couldn't set LCR to %d\n", value);
	 */
}

/* Returns once the transmitter is empty or we run out of retries */
static void dw8250_tx_wait_empty(struct uart_port *p)
{
	struct uart_8250_port *up = up_to_u8250p(p);
	unsigned int tries = 20000;
	unsigned int delay_threshold = tries - 1000;
	unsigned int lsr;

	while (tries--) {
		lsr = readb (p->membase + (UART_LSR << p->regshift));
		up->lsr_saved_flags |= lsr & up->lsr_save_mask;

		if (lsr & UART_LSR_TEMT)
			break;

		/* The device is first given a chance to empty without delay,
		 * to avoid slowdowns at high bitrates. If after 1000 tries
		 * the buffer has still not emptied, allow more time for low-
		 * speed links. */
		if (tries < delay_threshold)
			udelay (1);
	}
}

static void dw8250_serial_out(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = to_dw8250_data(p->private_data);

	writeb(value, p->membase + (offset << p->regshift));

	if (offset == UART_LCR && !d->uart_16550_compatible)
		dw8250_check_lcr(p, value);
}

static void dw8250_serial_out38x(struct uart_port *p, int offset, int value)
{
	/* Allow the TX to drain before we reconfigure */
	if (offset == UART_LCR)
		dw8250_tx_wait_empty(p);

	dw8250_serial_out(p, offset, value);
}

static unsigned int dw8250_serial_in(struct uart_port *p, int offset)
{
	unsigned int value = readb(p->membase + (offset << p->regshift));

	return dw8250_modify_msr(p, offset, value);
}

#ifdef CONFIG_64BIT
static unsigned int dw8250_serial_inq(struct uart_port *p, int offset)
{
	unsigned int value;

	value = (u8)__raw_readq(p->membase + (offset << p->regshift));

	return dw8250_modify_msr(p, offset, value);
}

static void dw8250_serial_outq(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = to_dw8250_data(p->private_data);

	value &= 0xff;
	__raw_writeq(value, p->membase + (offset << p->regshift));
	/* Read back to ensure register write ordering. */
	__raw_readq(p->membase + (UART_LCR << p->regshift));

	if (offset == UART_LCR && !d->uart_16550_compatible)
		dw8250_check_lcr(p, value);
}
#endif /* CONFIG_64BIT */

static void dw8250_serial_out32(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = to_dw8250_data(p->private_data);

	writel(value, p->membase + (offset << p->regshift));

	if (offset == UART_LCR && !d->uart_16550_compatible)
		dw8250_check_lcr(p, value);
}

static unsigned int dw8250_serial_in32(struct uart_port *p, int offset)
{
	unsigned int value = readl(p->membase + (offset << p->regshift));

	return dw8250_modify_msr(p, offset, value);
}

static void dw8250_serial_out32be(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = to_dw8250_data(p->private_data);

	iowrite32be(value, p->membase + (offset << p->regshift));

	if (offset == UART_LCR && !d->uart_16550_compatible)
		dw8250_check_lcr(p, value);
}

static unsigned int dw8250_serial_in32be(struct uart_port *p, int offset)
{
       unsigned int value = ioread32be(p->membase + (offset << p->regshift));

       return dw8250_modify_msr(p, offset, value);
}


static int dw8250_handle_irq(struct uart_port *p)
{
	struct uart_8250_port *up = up_to_u8250p(p);
	struct dw8250_data *d = to_dw8250_data(p->private_data);
	unsigned int iir = p->serial_in(p, UART_IIR);
	bool rx_timeout = (iir & 0x3f) == UART_IIR_RX_TIMEOUT;
	unsigned int quirks = d->pdata->quirks;
	unsigned int status;
	unsigned long flags;

	/*
	 * There are ways to get Designware-based UARTs into a state where
	 * they are asserting UART_IIR_RX_TIMEOUT but there is no actual
	 * data available.  If we see such a case then we'll do a bogus
	 * read.  If we don't do this then the "RX TIMEOUT" interrupt will
	 * fire forever.
	 *
	 * This problem has only been observed so far when not in DMA mode
	 * so we limit the workaround only to non-DMA mode.
	 */
	if (!up->dma && rx_timeout) {
		uart_port_lock_irqsave(p, &flags);
		status = serial_lsr_in(up);

		if (!(status & (UART_LSR_DR | UART_LSR_BI)))
			(void) p->serial_in(p, UART_RX);

		uart_port_unlock_irqrestore(p, flags);
	}

	/* Manually stop the Rx DMA transfer when acting as flow controller */
	if (quirks & DW_UART_QUIRK_IS_DMA_FC && up->dma && up->dma->rx_running && rx_timeout) {
		uart_port_lock_irqsave(p, &flags);
		status = serial_lsr_in(up);
		uart_port_unlock_irqrestore(p, flags);

		if (status & (UART_LSR_DR | UART_LSR_BI)) {
			dw8250_writel_ext(p, RZN1_UART_RDMACR, 0);
			dw8250_writel_ext(p, DW_UART_DMASA, 1);
		}
	}

	if (serial8250_handle_irq(p, iir))
		return 1;

	if ((iir & UART_IIR_BUSY) == UART_IIR_BUSY) {
		/* Clear the USR */
		(void)p->serial_in(p, d->pdata->usr_reg);

		return 1;
	}

	return 0;
}

static void dw8250_clk_work_cb(struct work_struct *work)
{
	struct dw8250_data *d = work_to_dw8250_data(work);
	struct uart_8250_port *up;
	unsigned long rate;

	rate = clk_get_rate(d->clk);
	if (rate <= 0)
		return;

	up = serial8250_get_port(d->data.line);

	serial8250_update_uartclk(&up->port, rate);
}

static int dw8250_clk_notifier_cb(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct dw8250_data *d = clk_to_dw8250_data(nb);

	/*
	 * We have no choice but to defer the uartclk update due to two
	 * deadlocks. First one is caused by a recursive mutex lock which
	 * happens when clk_set_rate() is called from dw8250_set_termios().
	 * Second deadlock is more tricky and is caused by an inverted order of
	 * the clk and tty-port mutexes lock. It happens if clock rate change
	 * is requested asynchronously while set_termios() is executed between
	 * tty-port mutex lock and clk_set_rate() function invocation and
	 * vise-versa. Anyway if we didn't have the reference clock alteration
	 * in the dw8250_set_termios() method we wouldn't have needed this
	 * deferred event handling complication.
	 */
	if (event == POST_RATE_CHANGE) {
		queue_work(system_unbound_wq, &d->clk_work);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static void
dw8250_do_pm(struct uart_port *port, unsigned int state, unsigned int old)
{
	if (!state)
		pm_runtime_get_sync(port->dev);

	serial8250_do_pm(port, state, old);

	if (state)
		pm_runtime_put_sync_suspend(port->dev);
}

static void dw8250_set_termios(struct uart_port *p, struct ktermios *termios,
			       const struct ktermios *old)
{
	unsigned long newrate = tty_termios_baud_rate(termios) * 16;
	struct dw8250_data *d = to_dw8250_data(p->private_data);
	long rate;
	int ret;

	clk_disable_unprepare(d->clk);
	rate = clk_round_rate(d->clk, newrate);
	if (rate > 0) {
		/*
		 * Note that any clock-notifer worker will block in
		 * serial8250_update_uartclk() until we are done.
		 */
		ret = clk_set_rate(d->clk, newrate);
		if (!ret)
			p->uartclk = rate;
	}
	clk_prepare_enable(d->clk);

	dw8250_do_set_termios(p, termios, old);
}

static void dw8250_set_ldisc(struct uart_port *p, struct ktermios *termios)
{
	struct uart_8250_port *up = up_to_u8250p(p);
	unsigned int mcr = p->serial_in(p, UART_MCR);

	if (up->capabilities & UART_CAP_IRDA) {
		if (termios->c_line == N_IRDA)
			mcr |= DW_UART_MCR_SIRE;
		else
			mcr &= ~DW_UART_MCR_SIRE;

		p->serial_out(p, UART_MCR, mcr);
	}
	serial8250_do_set_ldisc(p, termios);
}

/*
 * dw8250_fallback_dma_filter will prevent the UART from getting just any free
 * channel on platforms that have DMA engines, but don't have any channels
 * assigned to the UART.
 *
 * REVISIT: This is a work around for limitation in the DMA Engine API. Once the
 * core problem is fixed, this function is no longer needed.
 */
static bool dw8250_fallback_dma_filter(struct dma_chan *chan, void *param)
{
	return false;
}

static bool dw8250_idma_filter(struct dma_chan *chan, void *param)
{
	return param == chan->device->dev;
}

static u32 dw8250_rzn1_get_dmacr_burst(int max_burst)
{
	if (max_burst >= 8)
		return RZN1_UART_xDMACR_8_WORD_BURST;
	else if (max_burst >= 4)
		return RZN1_UART_xDMACR_4_WORD_BURST;
	else
		return RZN1_UART_xDMACR_1_WORD_BURST;
}

static void dw8250_prepare_tx_dma(struct uart_8250_port *p)
{
	struct uart_port *up = &p->port;
	struct uart_8250_dma *dma = p->dma;
	u32 val;

	dw8250_writel_ext(up, RZN1_UART_TDMACR, 0);
	val = dw8250_rzn1_get_dmacr_burst(dma->txconf.dst_maxburst) |
	      RZN1_UART_xDMACR_BLK_SZ(dma->tx_size) |
	      RZN1_UART_xDMACR_DMA_EN;
	dw8250_writel_ext(up, RZN1_UART_TDMACR, val);
}

static void dw8250_prepare_rx_dma(struct uart_8250_port *p)
{
	struct uart_port *up = &p->port;
	struct uart_8250_dma *dma = p->dma;
	u32 val;

	dw8250_writel_ext(up, RZN1_UART_RDMACR, 0);
	val = dw8250_rzn1_get_dmacr_burst(dma->rxconf.src_maxburst) |
	      RZN1_UART_xDMACR_BLK_SZ(dma->rx_size) |
	      RZN1_UART_xDMACR_DMA_EN;
	dw8250_writel_ext(up, RZN1_UART_RDMACR, val);
}

static void dw8250_quirks(struct uart_port *p, struct dw8250_data *data)
{
	struct device_node *np = p->dev->of_node;

	if (np) {
		unsigned int quirks = data->pdata->quirks;
		int id;

		/* get index of serial line, if found in DT aliases */
		id = of_alias_get_id(np, "serial");
		if (id >= 0)
			p->line = id;
#ifdef CONFIG_64BIT
		if (quirks & DW_UART_QUIRK_OCTEON) {
			p->serial_in = dw8250_serial_inq;
			p->serial_out = dw8250_serial_outq;
			p->flags = UPF_SKIP_TEST | UPF_SHARE_IRQ | UPF_FIXED_TYPE;
			p->type = PORT_OCTEON;
			data->skip_autocfg = true;
		}
#endif

		if (of_device_is_big_endian(np)) {
			p->iotype = UPIO_MEM32BE;
			p->serial_in = dw8250_serial_in32be;
			p->serial_out = dw8250_serial_out32be;
		}

		if (quirks & DW_UART_QUIRK_ARMADA_38X)
			p->serial_out = dw8250_serial_out38x;
		if (quirks & DW_UART_QUIRK_SKIP_SET_RATE)
			p->set_termios = dw8250_do_set_termios;
		if (quirks & DW_UART_QUIRK_IS_DMA_FC) {
			data->data.dma.txconf.device_fc = 1;
			data->data.dma.rxconf.device_fc = 1;
			data->data.dma.prepare_tx_dma = dw8250_prepare_tx_dma;
			data->data.dma.prepare_rx_dma = dw8250_prepare_rx_dma;
		}

	} else if (acpi_dev_present("APMC0D08", NULL, -1)) {
		p->iotype = UPIO_MEM32;
		p->regshift = 2;
		p->serial_in = dw8250_serial_in32;
		data->uart_16550_compatible = true;
	}

	/* Platforms with iDMA 64-bit */
	if (platform_get_resource_byname(to_platform_device(p->dev),
					 IORESOURCE_MEM, "lpss_priv")) {
		data->data.dma.rx_param = p->dev->parent;
		data->data.dma.tx_param = p->dev->parent;
		data->data.dma.fn = dw8250_idma_filter;
	}
}

static void dw8250_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

static int dw8250_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = {}, *up = &uart;
	struct uart_port *p = &up->port;
	struct device *dev = &pdev->dev;
	struct dw8250_data *data;
	struct resource *regs;
	int irq;
	int err;
	u32 val;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return dev_err_probe(dev, -EINVAL, "no registers defined\n");

	irq = platform_get_irq_optional(pdev, 0);
	/* no interrupt -> fall back to polling */
	if (irq == -ENXIO)
		irq = 0;
	if (irq < 0)
		return irq;

	spin_lock_init(&p->lock);
	p->mapbase	= regs->start;
	p->irq		= irq;
	p->handle_irq	= dw8250_handle_irq;
	p->pm		= dw8250_do_pm;
	p->type		= PORT_8250;
	p->flags	= UPF_SHARE_IRQ | UPF_FIXED_PORT;
	p->dev		= dev;
	p->iotype	= UPIO_MEM;
	p->serial_in	= dw8250_serial_in;
	p->serial_out	= dw8250_serial_out;
	p->set_ldisc	= dw8250_set_ldisc;
	p->set_termios	= dw8250_set_termios;

	p->membase = devm_ioremap(dev, regs->start, resource_size(regs));
	if (!p->membase)
		return -ENOMEM;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->data.dma.fn = dw8250_fallback_dma_filter;
	data->pdata = device_get_match_data(p->dev);
	p->private_data = &data->data;

	data->uart_16550_compatible = device_property_read_bool(dev,
						"snps,uart-16550-compatible");

	err = device_property_read_u32(dev, "reg-shift", &val);
	if (!err)
		p->regshift = val;

	err = device_property_read_u32(dev, "reg-io-width", &val);
	if (!err && val == 4) {
		p->iotype = UPIO_MEM32;
		p->serial_in = dw8250_serial_in32;
		p->serial_out = dw8250_serial_out32;
	}

	if (device_property_read_bool(dev, "dcd-override")) {
		/* Always report DCD as active */
		data->msr_mask_on |= UART_MSR_DCD;
		data->msr_mask_off |= UART_MSR_DDCD;
	}

	if (device_property_read_bool(dev, "dsr-override")) {
		/* Always report DSR as active */
		data->msr_mask_on |= UART_MSR_DSR;
		data->msr_mask_off |= UART_MSR_DDSR;
	}

	if (device_property_read_bool(dev, "cts-override")) {
		/* Always report CTS as active */
		data->msr_mask_on |= UART_MSR_CTS;
		data->msr_mask_off |= UART_MSR_DCTS;
	}

	if (device_property_read_bool(dev, "ri-override")) {
		/* Always report Ring indicator as inactive */
		data->msr_mask_off |= UART_MSR_RI;
		data->msr_mask_off |= UART_MSR_TERI;
	}

	/* Always ask for fixed clock rate from a property. */
	device_property_read_u32(dev, "clock-frequency", &p->uartclk);

	/* If there is separate baudclk, get the rate from it. */
	data->clk = devm_clk_get_optional_enabled(dev, "baudclk");
	if (data->clk == NULL)
		data->clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(data->clk))
		return PTR_ERR(data->clk);

	INIT_WORK(&data->clk_work, dw8250_clk_work_cb);
	data->clk_notifier.notifier_call = dw8250_clk_notifier_cb;

	if (data->clk)
		p->uartclk = clk_get_rate(data->clk);

	/* If no clock rate is defined, fail. */
	if (!p->uartclk)
		return dev_err_probe(dev, -EINVAL, "clock rate not defined\n");

	data->pclk = devm_clk_get_optional_enabled(dev, "apb_pclk");
	if (IS_ERR(data->pclk))
		return PTR_ERR(data->pclk);

	data->rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(data->rst))
		return PTR_ERR(data->rst);

	reset_control_deassert(data->rst);

	err = devm_add_action_or_reset(dev, dw8250_reset_control_assert, data->rst);
	if (err)
		return err;

	dw8250_quirks(p, data);

	/* If the Busy Functionality is not implemented, don't handle it */
	if (data->uart_16550_compatible)
		p->handle_irq = NULL;

	if (!data->skip_autocfg)
		dw8250_setup_port(p);

	/* If we have a valid fifosize, try hooking up DMA */
	if (p->fifosize) {
		data->data.dma.rxconf.src_maxburst = p->fifosize / 4;
		data->data.dma.txconf.dst_maxburst = p->fifosize / 4;
		up->dma = &data->data.dma;
	}

	data->data.line = serial8250_register_8250_port(up);
	if (data->data.line < 0)
		return data->data.line;

	/*
	 * Some platforms may provide a reference clock shared between several
	 * devices. In this case any clock state change must be known to the
	 * UART port at least post factum.
	 */
	if (data->clk) {
		err = clk_notifier_register(data->clk, &data->clk_notifier);
		if (err)
			return dev_err_probe(dev, err, "Failed to set the clock notifier\n");
		queue_work(system_unbound_wq, &data->clk_work);
	}

	platform_set_drvdata(pdev, data);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static int dw8250_remove(struct platform_device *pdev)
{
	struct dw8250_data *data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	pm_runtime_get_sync(dev);

	if (data->clk) {
		clk_notifier_unregister(data->clk, &data->clk_notifier);

		flush_work(&data->clk_work);
	}

	serial8250_unregister_port(data->data.line);

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);

	return 0;
}

static int dw8250_suspend(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	serial8250_suspend_port(data->data.line);

	return 0;
}

static int dw8250_resume(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	serial8250_resume_port(data->data.line);

	return 0;
}

static int dw8250_runtime_suspend(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	clk_disable_unprepare(data->clk);

	clk_disable_unprepare(data->pclk);

	return 0;
}

static int dw8250_runtime_resume(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	clk_prepare_enable(data->pclk);

	clk_prepare_enable(data->clk);

	return 0;
}

static const struct dev_pm_ops dw8250_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(dw8250_suspend, dw8250_resume)
	RUNTIME_PM_OPS(dw8250_runtime_suspend, dw8250_runtime_resume, NULL)
};

static const struct dw8250_platform_data dw8250_dw_apb = {
	.usr_reg = DW_UART_USR,
};

static const struct dw8250_platform_data dw8250_octeon_3860_data = {
	.usr_reg = OCTEON_UART_USR,
	.quirks = DW_UART_QUIRK_OCTEON,
};

static const struct dw8250_platform_data dw8250_armada_38x_data = {
	.usr_reg = DW_UART_USR,
	.quirks = DW_UART_QUIRK_ARMADA_38X,
};

static const struct dw8250_platform_data dw8250_renesas_rzn1_data = {
	.usr_reg = DW_UART_USR,
	.cpr_val = 0x00012f32,
	.quirks = DW_UART_QUIRK_IS_DMA_FC,
};

static const struct dw8250_platform_data dw8250_starfive_jh7100_data = {
	.usr_reg = DW_UART_USR,
	.quirks = DW_UART_QUIRK_SKIP_SET_RATE,
};

static const struct of_device_id dw8250_of_match[] = {
	{ .compatible = "snps,dw-apb-uart", .data = &dw8250_dw_apb },
	{ .compatible = "cavium,octeon-3860-uart", .data = &dw8250_octeon_3860_data },
	{ .compatible = "marvell,armada-38x-uart", .data = &dw8250_armada_38x_data },
	{ .compatible = "renesas,rzn1-uart", .data = &dw8250_renesas_rzn1_data },
	{ .compatible = "starfive,jh7100-uart", .data = &dw8250_starfive_jh7100_data },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, dw8250_of_match);

static const struct acpi_device_id dw8250_acpi_match[] = {
	{ "80860F0A", (kernel_ulong_t)&dw8250_dw_apb },
	{ "8086228A", (kernel_ulong_t)&dw8250_dw_apb },
	{ "AMD0020", (kernel_ulong_t)&dw8250_dw_apb },
	{ "AMDI0020", (kernel_ulong_t)&dw8250_dw_apb },
	{ "AMDI0022", (kernel_ulong_t)&dw8250_dw_apb },
	{ "APMC0D08", (kernel_ulong_t)&dw8250_dw_apb},
	{ "BRCM2032", (kernel_ulong_t)&dw8250_dw_apb },
	{ "HISI0031", (kernel_ulong_t)&dw8250_dw_apb },
	{ "INT33C4", (kernel_ulong_t)&dw8250_dw_apb },
	{ "INT33C5", (kernel_ulong_t)&dw8250_dw_apb },
	{ "INT3434", (kernel_ulong_t)&dw8250_dw_apb },
	{ "INT3435", (kernel_ulong_t)&dw8250_dw_apb },
	{ },
};
MODULE_DEVICE_TABLE(acpi, dw8250_acpi_match);

static struct platform_driver dw8250_platform_driver = {
	.driver = {
		.name		= "dw-apb-uart",
		.pm		= pm_ptr(&dw8250_pm_ops),
		.of_match_table	= dw8250_of_match,
		.acpi_match_table = dw8250_acpi_match,
	},
	.probe			= dw8250_probe,
	.remove			= dw8250_remove,
};

module_platform_driver(dw8250_platform_driver);

MODULE_AUTHOR("Jamie Iles");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synopsys DesignWare 8250 serial port driver");
MODULE_ALIAS("platform:dw-apb-uart");
