/*
 * Synopsys DesignWare 8250 driver.
 *
 * Copyright 2011 Picochip, Jamie Iles.
 * Copyright 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Synopsys DesignWare 8250 has an extra feature whereby it detects if the
 * LCR is written whilst busy.  If it is, then a busy detect interrupt is
 * raised, the LCR needs to be rewritten and the uart status register read.
 */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

#include <asm/byteorder.h>

#include "8250.h"

/* Offsets for the DesignWare specific registers */
#define DW_UART_USR	0x1f /* UART Status Register */
#define DW_UART_CPR	0xf4 /* Component Parameter Register */
#define DW_UART_UCV	0xf8 /* UART Component Version */

/* Component Parameter Register bits */
#define DW_UART_CPR_ABP_DATA_WIDTH	(3 << 0)
#define DW_UART_CPR_AFCE_MODE		(1 << 4)
#define DW_UART_CPR_THRE_MODE		(1 << 5)
#define DW_UART_CPR_SIR_MODE		(1 << 6)
#define DW_UART_CPR_SIR_LP_MODE		(1 << 7)
#define DW_UART_CPR_ADDITIONAL_FEATURES	(1 << 8)
#define DW_UART_CPR_FIFO_ACCESS		(1 << 9)
#define DW_UART_CPR_FIFO_STAT		(1 << 10)
#define DW_UART_CPR_SHADOW		(1 << 11)
#define DW_UART_CPR_ENCODED_PARMS	(1 << 12)
#define DW_UART_CPR_DMA_EXTRA		(1 << 13)
#define DW_UART_CPR_FIFO_MODE		(0xff << 16)
/* Helper for fifo size calculation */
#define DW_UART_CPR_FIFO_SIZE(a)	(((a >> 16) & 0xff) * 16)


struct dw8250_data {
	u8			usr_reg;
	int			line;
	int			msr_mask_on;
	int			msr_mask_off;
	struct clk		*clk;
	struct clk		*pclk;
	struct reset_control	*rst;
	struct uart_8250_dma	dma;

	unsigned int		skip_autocfg:1;
	unsigned int		uart_16550_compatible:1;
};

#define BYT_PRV_CLK			0x800
#define BYT_PRV_CLK_EN			(1 << 0)
#define BYT_PRV_CLK_M_VAL_SHIFT		1
#define BYT_PRV_CLK_N_VAL_SHIFT		16
#define BYT_PRV_CLK_UPDATE		(1 << 31)

static inline int dw8250_modify_msr(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = p->private_data;

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

	serial8250_clear_and_reinit_fifos(up);
	(void)p->serial_in(p, UART_RX);
}

static void dw8250_serial_out(struct uart_port *p, int offset, int value)
{
	writeb(value, p->membase + (offset << p->regshift));

	/* Make sure LCR write wasn't ignored */
	if (offset == UART_LCR) {
		int tries = 1000;
		while (tries--) {
			unsigned int lcr = p->serial_in(p, UART_LCR);
			if ((value & ~UART_LCR_SPAR) == (lcr & ~UART_LCR_SPAR))
				return;
			dw8250_force_idle(p);
			writeb(value, p->membase + (UART_LCR << p->regshift));
		}
		/*
		 * FIXME: this deadlocks if port->lock is already held
		 * dev_err(p->dev, "Couldn't set LCR to %d\n", value);
		 */
	}
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
	value &= 0xff;
	__raw_writeq(value, p->membase + (offset << p->regshift));
	/* Read back to ensure register write ordering. */
	__raw_readq(p->membase + (UART_LCR << p->regshift));

	/* Make sure LCR write wasn't ignored */
	if (offset == UART_LCR) {
		int tries = 1000;
		while (tries--) {
			unsigned int lcr = p->serial_in(p, UART_LCR);
			if ((value & ~UART_LCR_SPAR) == (lcr & ~UART_LCR_SPAR))
				return;
			dw8250_force_idle(p);
			__raw_writeq(value & 0xff,
				     p->membase + (UART_LCR << p->regshift));
		}
		/*
		 * FIXME: this deadlocks if port->lock is already held
		 * dev_err(p->dev, "Couldn't set LCR to %d\n", value);
		 */
	}
}
#endif /* CONFIG_64BIT */

static void dw8250_serial_out32(struct uart_port *p, int offset, int value)
{
	writel(value, p->membase + (offset << p->regshift));

	/* Make sure LCR write wasn't ignored */
	if (offset == UART_LCR) {
		int tries = 1000;
		while (tries--) {
			unsigned int lcr = p->serial_in(p, UART_LCR);
			if ((value & ~UART_LCR_SPAR) == (lcr & ~UART_LCR_SPAR))
				return;
			dw8250_force_idle(p);
			writel(value, p->membase + (UART_LCR << p->regshift));
		}
		/*
		 * FIXME: this deadlocks if port->lock is already held
		 * dev_err(p->dev, "Couldn't set LCR to %d\n", value);
		 */
	}
}

static unsigned int dw8250_serial_in32(struct uart_port *p, int offset)
{
	unsigned int value = readl(p->membase + (offset << p->regshift));

	return dw8250_modify_msr(p, offset, value);
}

static int dw8250_handle_irq(struct uart_port *p)
{
	struct dw8250_data *d = p->private_data;
	unsigned int iir = p->serial_in(p, UART_IIR);
	unsigned int status;
	unsigned long flags;

	/*
	 * There are ways to get Designware-based UARTs into a state where
	 * they are asserting UART_IIR_RX_TIMEOUT but there is no actual
	 * data available.  If we see such a case then we'll do a bogus
	 * read.  If we don't do this then the "RX TIMEOUT" interrupt will
	 * fire forever.
	 */
	if ((iir & 0x3f) == UART_IIR_RX_TIMEOUT) {
		spin_lock_irqsave(&p->lock, flags);
		status = p->serial_in(p, UART_LSR);

		if (!(status & (UART_LSR_DR | UART_LSR_BI)))
			(void) p->serial_in(p, UART_RX);

		spin_unlock_irqrestore(&p->lock, flags);
	}

	if (serial8250_handle_irq(p, iir)) {
		return 1;
	} else if ((iir & UART_IIR_BUSY) == UART_IIR_BUSY) {
		/* Clear the USR */
		(void)p->serial_in(p, d->usr_reg);

		return 1;
	}

	return 0;
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
			       struct ktermios *old)
{
	unsigned int baud = tty_termios_baud_rate(termios);
	struct dw8250_data *d = p->private_data;
	unsigned int rate;
#ifdef CONFIG_ARCH_ROCKCHIP
	unsigned int div, rate_temp, diff;
#endif
	int ret;

	if (IS_ERR(d->clk) || !old)
		goto out;

	clk_disable_unprepare(d->clk);
#ifdef CONFIG_ARCH_ROCKCHIP
	if ((baud * 16) <= 4000000) {
		/*
		 * Make sure uart sclk is high enough
		 */
		div = 4000000 / baud / 16;
		rate = baud * 16 * div;
	} else {
		rate = baud * 16;
	}

	ret = clk_set_rate(d->clk, rate);
	rate_temp = clk_get_rate(d->clk);
	diff = rate * 20 / 1000;
	/*
	 * If rate_temp is not equal to rate, is means fractional frequency
	 * division is failed. Then use Integer frequency division, and
	 * the buad rate error must be under -+2%
	 */
	if ((rate_temp < rate) && ((rate - rate_temp) > diff)) {
		ret = clk_set_rate(d->clk, rate + diff);
		rate_temp = clk_get_rate(d->clk);
		if ((rate_temp < rate) && ((rate - rate_temp) > diff))
			dev_info(p->dev, "set rate:%d, but get rate:%d\n",
				 rate, rate_temp);
		else if ((rate < rate_temp) && ((rate_temp - rate) > diff))
			dev_info(p->dev, "set rate:%d, but get rate:%d\n",
				 rate, rate_temp);
	}
#else
	rate = clk_round_rate(d->clk, baud * 16);
	ret = clk_set_rate(d->clk, rate);
#endif
	clk_prepare_enable(d->clk);

	if (!ret)
		p->uartclk = rate;
out:
	p->status &= ~UPSTAT_AUTOCTS;
	if (termios->c_cflag & CRTSCTS)
		p->status |= UPSTAT_AUTOCTS;

	serial8250_do_set_termios(p, termios, old);
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
	return param == chan->device->dev->parent;
}

static void dw8250_quirks(struct uart_port *p, struct dw8250_data *data)
{
	if (p->dev->of_node) {
		struct device_node *np = p->dev->of_node;
		int id;

		/* get index of serial line, if found in DT aliases */
		id = of_alias_get_id(np, "serial");
		if (id >= 0)
			p->line = id;
#ifdef CONFIG_64BIT
		if (of_device_is_compatible(np, "cavium,octeon-3860-uart")) {
			p->serial_in = dw8250_serial_inq;
			p->serial_out = dw8250_serial_outq;
			p->flags = UPF_SKIP_TEST | UPF_SHARE_IRQ | UPF_FIXED_TYPE;
			p->type = PORT_OCTEON;
			data->usr_reg = 0x27;
			data->skip_autocfg = true;
		}
#endif
	} else if (has_acpi_companion(p->dev)) {
		p->iotype = UPIO_MEM32;
		p->regshift = 2;
		p->serial_in = dw8250_serial_in32;
		/* So far none of there implement the Busy Functionality */
		data->uart_16550_compatible = true;
	}

	/* Platforms with iDMA */
	if (platform_get_resource_byname(to_platform_device(p->dev),
					 IORESOURCE_MEM, "lpss_priv")) {
		data->dma.rx_param = p->dev->parent;
		data->dma.tx_param = p->dev->parent;
		data->dma.fn = dw8250_idma_filter;
	}
}

static void dw8250_setup_port(struct uart_port *p)
{
	struct uart_8250_port *up = up_to_u8250p(p);
	u32 reg;

	/*
	 * If the Component Version Register returns zero, we know that
	 * ADDITIONAL_FEATURES are not enabled. No need to go any further.
	 */
	reg = readl(p->membase + DW_UART_UCV);
	if (!reg)
		return;

	dev_dbg(p->dev, "Designware UART version %c.%c%c\n",
		(reg >> 24) & 0xff, (reg >> 16) & 0xff, (reg >> 8) & 0xff);

	reg = readl(p->membase + DW_UART_CPR);

#ifdef CONFIG_ARCH_ROCKCHIP
	/*
	 * The UART CPR may be 0 of some rockchip soc,
	 * but it supports fifo and AFC, fifo entry is 32 default.
	 */
	if (reg == 0)
		reg = 0x00023ff2;
#endif
	if (!reg)
		return;

	/* Select the type based on fifo */
	if (reg & DW_UART_CPR_FIFO_MODE) {
		p->type = PORT_16550A;
		p->flags |= UPF_FIXED_TYPE;
		p->fifosize = DW_UART_CPR_FIFO_SIZE(reg);
#ifdef CONFIG_ARCH_ROCKCHIP
		up->tx_loadsz = p->fifosize * 3 / 4;
#endif
		up->capabilities = UART_CAP_FIFO;
	}

	if (reg & DW_UART_CPR_AFCE_MODE)
		up->capabilities |= UART_CAP_AFE;
}

static int dw8250_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = {};
	struct resource *regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int irq = platform_get_irq(pdev, 0);
	struct uart_port *p = &uart.port;
	struct dw8250_data *data;
	int err;
	u32 val;

	if (!regs) {
		dev_err(&pdev->dev, "no registers defined\n");
		return -EINVAL;
	}

	if (irq < 0) {
		if (irq != -EPROBE_DEFER)
			dev_err(&pdev->dev, "cannot get irq\n");
		return irq;
	}

	spin_lock_init(&p->lock);
	p->mapbase	= regs->start;
	p->irq		= irq;
	p->handle_irq	= dw8250_handle_irq;
	p->pm		= dw8250_do_pm;
	p->type		= PORT_8250;
	p->flags	= UPF_SHARE_IRQ | UPF_FIXED_PORT;
	p->dev		= &pdev->dev;
	p->iotype	= UPIO_MEM;
	p->serial_in	= dw8250_serial_in;
	p->serial_out	= dw8250_serial_out;
	p->set_termios = dw8250_set_termios;

	p->membase = devm_ioremap(&pdev->dev, regs->start, resource_size(regs));
	if (!p->membase)
		return -ENOMEM;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dma.fn = dw8250_fallback_dma_filter;
	data->usr_reg = DW_UART_USR;
	p->private_data = data;

	data->uart_16550_compatible = device_property_read_bool(p->dev,
						"snps,uart-16550-compatible");

	err = device_property_read_u32(p->dev, "reg-shift", &val);
	if (!err)
		p->regshift = val;

	err = device_property_read_u32(p->dev, "reg-io-width", &val);
	if (!err && val == 4) {
		p->iotype = UPIO_MEM32;
		p->serial_in = dw8250_serial_in32;
		p->serial_out = dw8250_serial_out32;
	}

	if (device_property_read_bool(p->dev, "dcd-override")) {
		/* Always report DCD as active */
		data->msr_mask_on |= UART_MSR_DCD;
		data->msr_mask_off |= UART_MSR_DDCD;
	}

	if (device_property_read_bool(p->dev, "dsr-override")) {
		/* Always report DSR as active */
		data->msr_mask_on |= UART_MSR_DSR;
		data->msr_mask_off |= UART_MSR_DDSR;
	}

	if (device_property_read_bool(p->dev, "cts-override")) {
		/* Always report CTS as active */
		data->msr_mask_on |= UART_MSR_CTS;
		data->msr_mask_off |= UART_MSR_DCTS;
	}

	if (device_property_read_bool(p->dev, "ri-override")) {
		/* Always report Ring indicator as inactive */
		data->msr_mask_off |= UART_MSR_RI;
		data->msr_mask_off |= UART_MSR_TERI;
	}

	/* Always ask for fixed clock rate from a property. */
	device_property_read_u32(p->dev, "clock-frequency", &p->uartclk);

	/* If there is separate baudclk, get the rate from it. */
	data->clk = devm_clk_get(&pdev->dev, "baudclk");
	if (IS_ERR(data->clk) && PTR_ERR(data->clk) != -EPROBE_DEFER)
		data->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(data->clk) && PTR_ERR(data->clk) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (!IS_ERR_OR_NULL(data->clk)) {
		err = clk_prepare_enable(data->clk);
		if (err)
			dev_warn(&pdev->dev, "could not enable optional baudclk: %d\n",
				 err);
		else
			p->uartclk = clk_get_rate(data->clk);
	}

	/* If no clock rate is defined, fail. */
	if (!p->uartclk) {
		dev_err(&pdev->dev, "clock rate not defined\n");
		return -EINVAL;
	}

	data->pclk = devm_clk_get(&pdev->dev, "apb_pclk");
	if (IS_ERR(data->pclk) && PTR_ERR(data->pclk) == -EPROBE_DEFER) {
		err = -EPROBE_DEFER;
		goto err_clk;
	}
	if (!IS_ERR(data->pclk)) {
		err = clk_prepare_enable(data->pclk);
		if (err) {
			dev_err(&pdev->dev, "could not enable apb_pclk\n");
			goto err_clk;
		}
	}

	data->rst = devm_reset_control_get_optional(&pdev->dev, NULL);
	if (IS_ERR(data->rst) && PTR_ERR(data->rst) == -EPROBE_DEFER) {
		err = -EPROBE_DEFER;
		goto err_pclk;
	}
	if (!IS_ERR(data->rst))
		reset_control_deassert(data->rst);

	dw8250_quirks(p, data);

	/* If the Busy Functionality is not implemented, don't handle it */
	if (data->uart_16550_compatible) {
		p->serial_out = NULL;
		p->handle_irq = NULL;
	}

	if (!data->skip_autocfg)
		dw8250_setup_port(p);

	/* If we have a valid fifosize, try hooking up DMA */
	if (p->fifosize) {
		data->dma.rxconf.src_maxburst = p->fifosize / 4;
		data->dma.txconf.dst_maxburst = p->fifosize / 4;
		uart.dma = &data->dma;
	}

	data->line = serial8250_register_8250_port(&uart);
	if (data->line < 0) {
		err = data->line;
		goto err_reset;
	}

	platform_set_drvdata(pdev, data);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

err_reset:
	if (!IS_ERR(data->rst))
		reset_control_assert(data->rst);

err_pclk:
	if (!IS_ERR(data->pclk))
		clk_disable_unprepare(data->pclk);

err_clk:
	if (!IS_ERR(data->clk))
		clk_disable_unprepare(data->clk);

	return err;
}

static int dw8250_remove(struct platform_device *pdev)
{
	struct dw8250_data *data = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);

	serial8250_unregister_port(data->line);

	if (!IS_ERR(data->rst))
		reset_control_assert(data->rst);

	if (!IS_ERR(data->pclk))
		clk_disable_unprepare(data->pclk);

	if (!IS_ERR(data->clk))
		clk_disable_unprepare(data->clk);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dw8250_suspend(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	serial8250_suspend_port(data->line);

	return 0;
}

static int dw8250_resume(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	serial8250_resume_port(data->line);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int dw8250_runtime_suspend(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	if (!IS_ERR(data->clk))
		clk_disable_unprepare(data->clk);

	if (!IS_ERR(data->pclk))
		clk_disable_unprepare(data->pclk);

	return 0;
}

static int dw8250_runtime_resume(struct device *dev)
{
	struct dw8250_data *data = dev_get_drvdata(dev);

	if (!IS_ERR(data->pclk))
		clk_prepare_enable(data->pclk);

	if (!IS_ERR(data->clk))
		clk_prepare_enable(data->clk);

	return 0;
}
#endif

static const struct dev_pm_ops dw8250_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw8250_suspend, dw8250_resume)
	SET_RUNTIME_PM_OPS(dw8250_runtime_suspend, dw8250_runtime_resume, NULL)
};

static const struct of_device_id dw8250_of_match[] = {
	{ .compatible = "snps,dw-apb-uart" },
	{ .compatible = "cavium,octeon-3860-uart" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, dw8250_of_match);

static const struct acpi_device_id dw8250_acpi_match[] = {
	{ "INT33C4", 0 },
	{ "INT33C5", 0 },
	{ "INT3434", 0 },
	{ "INT3435", 0 },
	{ "80860F0A", 0 },
	{ "8086228A", 0 },
	{ "APMC0D08", 0},
	{ "AMD0020", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, dw8250_acpi_match);

static struct platform_driver dw8250_platform_driver = {
	.driver = {
		.name		= "dw-apb-uart",
		.pm		= &dw8250_pm_ops,
		.of_match_table	= dw8250_of_match,
		.acpi_match_table = ACPI_PTR(dw8250_acpi_match),
	},
	.probe			= dw8250_probe,
	.remove			= dw8250_remove,
};

module_platform_driver(dw8250_platform_driver);

MODULE_AUTHOR("Jamie Iles");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synopsys DesignWare 8250 serial port driver");
MODULE_ALIAS("platform:dw-apb-uart");
