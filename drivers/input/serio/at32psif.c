/*
 * Copyright (C) 2007 Atmel Corporation
 *
 * Driver for the AT32AP700X PS/2 controller (PSIF).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

/* PSIF register offsets */
#define PSIF_CR				0x00
#define PSIF_RHR			0x04
#define PSIF_THR			0x08
#define PSIF_SR				0x10
#define PSIF_IER			0x14
#define PSIF_IDR			0x18
#define PSIF_IMR			0x1c
#define PSIF_PSR			0x24

/* Bitfields in control register. */
#define PSIF_CR_RXDIS_OFFSET		1
#define PSIF_CR_RXDIS_SIZE		1
#define PSIF_CR_RXEN_OFFSET		0
#define PSIF_CR_RXEN_SIZE		1
#define PSIF_CR_SWRST_OFFSET		15
#define PSIF_CR_SWRST_SIZE		1
#define PSIF_CR_TXDIS_OFFSET		9
#define PSIF_CR_TXDIS_SIZE		1
#define PSIF_CR_TXEN_OFFSET		8
#define PSIF_CR_TXEN_SIZE		1

/* Bitfields in interrupt disable, enable, mask and status register. */
#define PSIF_NACK_OFFSET		8
#define PSIF_NACK_SIZE			1
#define PSIF_OVRUN_OFFSET		5
#define PSIF_OVRUN_SIZE			1
#define PSIF_PARITY_OFFSET		9
#define PSIF_PARITY_SIZE		1
#define PSIF_RXRDY_OFFSET		4
#define PSIF_RXRDY_SIZE			1
#define PSIF_TXEMPTY_OFFSET		1
#define PSIF_TXEMPTY_SIZE		1
#define PSIF_TXRDY_OFFSET		0
#define PSIF_TXRDY_SIZE			1

/* Bitfields in prescale register. */
#define PSIF_PSR_PRSCV_OFFSET		0
#define PSIF_PSR_PRSCV_SIZE		12

/* Bitfields in receive hold register. */
#define PSIF_RHR_RXDATA_OFFSET		0
#define PSIF_RHR_RXDATA_SIZE		8

/* Bitfields in transmit hold register. */
#define PSIF_THR_TXDATA_OFFSET		0
#define PSIF_THR_TXDATA_SIZE		8

/* Bit manipulation macros */
#define PSIF_BIT(name)					\
	(1 << PSIF_##name##_OFFSET)

#define PSIF_BF(name, value)				\
	(((value) & ((1 << PSIF_##name##_SIZE) - 1))	\
	 << PSIF_##name##_OFFSET)

#define PSIF_BFEXT(name, value)				\
	(((value) >> PSIF_##name##_OFFSET)		\
	 & ((1 << PSIF_##name##_SIZE) - 1))

#define PSIF_BFINS(name, value, old)			\
	(((old) & ~(((1 << PSIF_##name##_SIZE) - 1)	\
		    << PSIF_##name##_OFFSET))		\
	 | PSIF_BF(name, value))

/* Register access macros */
#define psif_readl(port, reg)				\
	__raw_readl((port)->regs + PSIF_##reg)

#define psif_writel(port, reg, value)			\
	__raw_writel((value), (port)->regs + PSIF_##reg)

struct psif {
	struct platform_device	*pdev;
	struct clk		*pclk;
	struct serio		*io;
	void __iomem		*regs;
	unsigned int		irq;
	unsigned int		open;
	/* Prevent concurrent writes to PSIF THR. */
	spinlock_t		lock;
};

static irqreturn_t psif_interrupt(int irq, void *_ptr)
{
	struct psif *psif = _ptr;
	int retval = IRQ_NONE;
	unsigned int io_flags = 0;
	unsigned long status;

	status = psif_readl(psif, SR);

	if (status & PSIF_BIT(RXRDY)) {
		unsigned char val = (unsigned char) psif_readl(psif, RHR);

		if (status & PSIF_BIT(PARITY))
			io_flags |= SERIO_PARITY;
		if (status & PSIF_BIT(OVRUN))
			dev_err(&psif->pdev->dev, "overrun read error\n");

		serio_interrupt(psif->io, val, io_flags);

		retval = IRQ_HANDLED;
	}

	return retval;
}

static int psif_write(struct serio *io, unsigned char val)
{
	struct psif *psif = io->port_data;
	unsigned long flags;
	int timeout = 10;
	int retval = 0;

	spin_lock_irqsave(&psif->lock, flags);

	while (!(psif_readl(psif, SR) & PSIF_BIT(TXEMPTY)) && timeout--)
		msleep(10);

	if (timeout >= 0) {
		psif_writel(psif, THR, val);
	} else {
		dev_dbg(&psif->pdev->dev, "timeout writing to THR\n");
		retval = -EBUSY;
	}

	spin_unlock_irqrestore(&psif->lock, flags);

	return retval;
}

static int psif_open(struct serio *io)
{
	struct psif *psif = io->port_data;
	int retval;

	retval = clk_enable(psif->pclk);
	if (retval)
		goto out;

	psif_writel(psif, CR, PSIF_BIT(CR_TXEN) | PSIF_BIT(CR_RXEN));
	psif_writel(psif, IER, PSIF_BIT(RXRDY));

	psif->open = 1;
out:
	return retval;
}

static void psif_close(struct serio *io)
{
	struct psif *psif = io->port_data;

	psif->open = 0;

	psif_writel(psif, IDR, ~0UL);
	psif_writel(psif, CR, PSIF_BIT(CR_TXDIS) | PSIF_BIT(CR_RXDIS));

	clk_disable(psif->pclk);
}

static void psif_set_prescaler(struct psif *psif)
{
	unsigned long prscv;
	unsigned long rate = clk_get_rate(psif->pclk);

	/* PRSCV = Pulse length (100 us) * PSIF module frequency. */
	prscv = 100 * (rate / 1000000UL);

	if (prscv > ((1<<PSIF_PSR_PRSCV_SIZE) - 1)) {
		prscv = (1<<PSIF_PSR_PRSCV_SIZE) - 1;
		dev_dbg(&psif->pdev->dev, "pclk too fast, "
				"prescaler set to max\n");
	}

	clk_enable(psif->pclk);
	psif_writel(psif, PSR, prscv);
	clk_disable(psif->pclk);
}

static int __init psif_probe(struct platform_device *pdev)
{
	struct resource *regs;
	struct psif *psif;
	struct serio *io;
	struct clk *pclk;
	int irq;
	int ret;

	psif = kzalloc(sizeof(struct psif), GFP_KERNEL);
	if (!psif) {
		dev_dbg(&pdev->dev, "out of memory\n");
		ret = -ENOMEM;
		goto out;
	}
	psif->pdev = pdev;

	io = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!io) {
		dev_dbg(&pdev->dev, "out of memory\n");
		ret = -ENOMEM;
		goto out_free_psif;
	}
	psif->io = io;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_dbg(&pdev->dev, "no mmio resources defined\n");
		ret = -ENOMEM;
		goto out_free_io;
	}

	psif->regs = ioremap(regs->start, resource_size(regs));
	if (!psif->regs) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "could not map I/O memory\n");
		goto out_free_io;
	}

	pclk = clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pclk)) {
		dev_dbg(&pdev->dev, "could not get peripheral clock\n");
		ret = PTR_ERR(pclk);
		goto out_iounmap;
	}
	psif->pclk = pclk;

	/* Reset the PSIF to enter at a known state. */
	ret = clk_enable(pclk);
	if (ret) {
		dev_dbg(&pdev->dev, "could not enable pclk\n");
		goto out_put_clk;
	}
	psif_writel(psif, CR, PSIF_BIT(CR_SWRST));
	clk_disable(pclk);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_dbg(&pdev->dev, "could not get irq\n");
		ret = -ENXIO;
		goto out_put_clk;
	}
	ret = request_irq(irq, psif_interrupt, IRQF_SHARED, "at32psif", psif);
	if (ret) {
		dev_dbg(&pdev->dev, "could not request irq %d\n", irq);
		goto out_put_clk;
	}
	psif->irq = irq;

	io->id.type	= SERIO_8042;
	io->write	= psif_write;
	io->open	= psif_open;
	io->close	= psif_close;
	snprintf(io->name, sizeof(io->name), "AVR32 PS/2 port%d", pdev->id);
	snprintf(io->phys, sizeof(io->phys), "at32psif/serio%d", pdev->id);
	io->port_data	= psif;
	io->dev.parent	= &pdev->dev;

	psif_set_prescaler(psif);

	spin_lock_init(&psif->lock);
	serio_register_port(psif->io);
	platform_set_drvdata(pdev, psif);

	dev_info(&pdev->dev, "Atmel AVR32 PSIF PS/2 driver on 0x%08x irq %d\n",
			(int)psif->regs, psif->irq);

	return 0;

out_put_clk:
	clk_put(psif->pclk);
out_iounmap:
	iounmap(psif->regs);
out_free_io:
	kfree(io);
out_free_psif:
	kfree(psif);
out:
	return ret;
}

static int __exit psif_remove(struct platform_device *pdev)
{
	struct psif *psif = platform_get_drvdata(pdev);

	psif_writel(psif, IDR, ~0UL);
	psif_writel(psif, CR, PSIF_BIT(CR_TXDIS) | PSIF_BIT(CR_RXDIS));

	serio_unregister_port(psif->io);
	iounmap(psif->regs);
	free_irq(psif->irq, psif);
	clk_put(psif->pclk);
	kfree(psif);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int psif_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct psif *psif = platform_get_drvdata(pdev);

	if (psif->open) {
		psif_writel(psif, CR, PSIF_BIT(CR_RXDIS) | PSIF_BIT(CR_TXDIS));
		clk_disable(psif->pclk);
	}

	return 0;
}

static int psif_resume(struct platform_device *pdev)
{
	struct psif *psif = platform_get_drvdata(pdev);

	if (psif->open) {
		clk_enable(psif->pclk);
		psif_set_prescaler(psif);
		psif_writel(psif, CR, PSIF_BIT(CR_RXEN) | PSIF_BIT(CR_TXEN));
	}

	return 0;
}
#else
#define psif_suspend	NULL
#define psif_resume	NULL
#endif

static struct platform_driver psif_driver = {
	.remove		= __exit_p(psif_remove),
	.driver		= {
		.name	= "atmel_psif",
	},
	.suspend	= psif_suspend,
	.resume		= psif_resume,
};

static int __init psif_init(void)
{
	return platform_driver_probe(&psif_driver, psif_probe);
}

static void __exit psif_exit(void)
{
	platform_driver_unregister(&psif_driver);
}

module_init(psif_init);
module_exit(psif_exit);

MODULE_AUTHOR("Hans-Christian Egtvedt <hans-christian.egtvedt@atmel.com>");
MODULE_DESCRIPTION("Atmel AVR32 PSIF PS/2 driver");
MODULE_LICENSE("GPL");
