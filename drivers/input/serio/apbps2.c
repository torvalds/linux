// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013 Aeroflex Gaisler
 *
 * This driver supports the APBPS2 PS/2 core available in the GRLIB
 * VHDL IP core library.
 *
 * Full documentation of the APBPS2 core can be found here:
 * http://www.gaisler.com/products/grlib/grip.pdf
 *
 * See "Documentation/devicetree/bindings/input/ps2keyb-mouse-apbps2.txt" for
 * information on open firmware properties.
 *
 * Contributors: Daniel Hellstrom <daniel@gaisler.com>
 */
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/io.h>

struct apbps2_regs {
	u32 __iomem data;	/* 0x00 */
	u32 __iomem status;	/* 0x04 */
	u32 __iomem ctrl;	/* 0x08 */
	u32 __iomem reload;	/* 0x0c */
};

#define APBPS2_STATUS_DR	(1<<0)
#define APBPS2_STATUS_PE	(1<<1)
#define APBPS2_STATUS_FE	(1<<2)
#define APBPS2_STATUS_KI	(1<<3)
#define APBPS2_STATUS_RF	(1<<4)
#define APBPS2_STATUS_TF	(1<<5)
#define APBPS2_STATUS_TCNT	(0x1f<<22)
#define APBPS2_STATUS_RCNT	(0x1f<<27)

#define APBPS2_CTRL_RE		(1<<0)
#define APBPS2_CTRL_TE		(1<<1)
#define APBPS2_CTRL_RI		(1<<2)
#define APBPS2_CTRL_TI		(1<<3)

struct apbps2_priv {
	struct serio		*io;
	struct apbps2_regs	*regs;
};

static int apbps2_idx;

static irqreturn_t apbps2_isr(int irq, void *dev_id)
{
	struct apbps2_priv *priv = dev_id;
	unsigned long status, data, rxflags;
	irqreturn_t ret = IRQ_NONE;

	while ((status = ioread32be(&priv->regs->status)) & APBPS2_STATUS_DR) {
		data = ioread32be(&priv->regs->data);
		rxflags = (status & APBPS2_STATUS_PE) ? SERIO_PARITY : 0;
		rxflags |= (status & APBPS2_STATUS_FE) ? SERIO_FRAME : 0;

		/* clear error bits? */
		if (rxflags)
			iowrite32be(0, &priv->regs->status);

		serio_interrupt(priv->io, data, rxflags);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static int apbps2_write(struct serio *io, unsigned char val)
{
	struct apbps2_priv *priv = io->port_data;
	unsigned int tleft = 10000; /* timeout in 100ms */

	/* delay until PS/2 controller has room for more chars */
	while ((ioread32be(&priv->regs->status) & APBPS2_STATUS_TF) && tleft--)
		udelay(10);

	if ((ioread32be(&priv->regs->status) & APBPS2_STATUS_TF) == 0) {
		iowrite32be(val, &priv->regs->data);

		iowrite32be(APBPS2_CTRL_RE | APBPS2_CTRL_RI | APBPS2_CTRL_TE,
				&priv->regs->ctrl);
		return 0;
	}

	return -ETIMEDOUT;
}

static int apbps2_open(struct serio *io)
{
	struct apbps2_priv *priv = io->port_data;
	int limit;
	unsigned long tmp;

	/* clear error flags */
	iowrite32be(0, &priv->regs->status);

	/* Clear old data if available (unlikely) */
	limit = 1024;
	while ((ioread32be(&priv->regs->status) & APBPS2_STATUS_DR) && --limit)
		tmp = ioread32be(&priv->regs->data);

	/* Enable reciever and it's interrupt */
	iowrite32be(APBPS2_CTRL_RE | APBPS2_CTRL_RI, &priv->regs->ctrl);

	return 0;
}

static void apbps2_close(struct serio *io)
{
	struct apbps2_priv *priv = io->port_data;

	/* stop interrupts at PS/2 HW level */
	iowrite32be(0, &priv->regs->ctrl);
}

/* Initialize one APBPS2 PS/2 core */
static int apbps2_of_probe(struct platform_device *ofdev)
{
	struct apbps2_priv *priv;
	int irq, err;
	u32 freq_hz;
	struct resource *res;

	priv = devm_kzalloc(&ofdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&ofdev->dev, "memory allocation failed\n");
		return -ENOMEM;
	}

	/* Find Device Address */
	res = platform_get_resource(ofdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(&ofdev->dev, res);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	/* Reset hardware, disable interrupt */
	iowrite32be(0, &priv->regs->ctrl);

	/* IRQ */
	irq = irq_of_parse_and_map(ofdev->dev.of_node, 0);
	err = devm_request_irq(&ofdev->dev, irq, apbps2_isr,
				IRQF_SHARED, "apbps2", priv);
	if (err) {
		dev_err(&ofdev->dev, "request IRQ%d failed\n", irq);
		return err;
	}

	/* Get core frequency */
	if (of_property_read_u32(ofdev->dev.of_node, "freq", &freq_hz)) {
		dev_err(&ofdev->dev, "unable to get core frequency\n");
		return -EINVAL;
	}

	/* Set reload register to core freq in kHz/10 */
	iowrite32be(freq_hz / 10000, &priv->regs->reload);

	priv->io = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!priv->io)
		return -ENOMEM;

	priv->io->id.type = SERIO_8042;
	priv->io->open = apbps2_open;
	priv->io->close = apbps2_close;
	priv->io->write = apbps2_write;
	priv->io->port_data = priv;
	strlcpy(priv->io->name, "APBPS2 PS/2", sizeof(priv->io->name));
	snprintf(priv->io->phys, sizeof(priv->io->phys),
		 "apbps2_%d", apbps2_idx++);

	dev_info(&ofdev->dev, "irq = %d, base = 0x%p\n", irq, priv->regs);

	serio_register_port(priv->io);

	platform_set_drvdata(ofdev, priv);

	return 0;
}

static int apbps2_of_remove(struct platform_device *of_dev)
{
	struct apbps2_priv *priv = platform_get_drvdata(of_dev);

	serio_unregister_port(priv->io);

	return 0;
}

static const struct of_device_id apbps2_of_match[] = {
	{ .name = "GAISLER_APBPS2", },
	{ .name = "01_060", },
	{}
};

MODULE_DEVICE_TABLE(of, apbps2_of_match);

static struct platform_driver apbps2_of_driver = {
	.driver = {
		.name = "grlib-apbps2",
		.of_match_table = apbps2_of_match,
	},
	.probe = apbps2_of_probe,
	.remove = apbps2_of_remove,
};

module_platform_driver(apbps2_of_driver);

MODULE_AUTHOR("Aeroflex Gaisler AB.");
MODULE_DESCRIPTION("GRLIB APBPS2 PS/2 serial I/O");
MODULE_LICENSE("GPL");
