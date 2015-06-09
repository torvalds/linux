/*
 * OLPC serio driver for multiplexed input from Marvell MMP security processor
 *
 * Copyright (C) 2011-2013 One Laptop Per Child
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
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/serio.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/delay.h>

/*
 * The OLPC XO-1.75 and XO-4 laptops do not have a hardware PS/2 controller.
 * Instead, the OLPC firmware runs a bit-banging PS/2 implementation on an
 * otherwise-unused slow processor which is included in the Marvell MMP2/MMP3
 * SoC, known as the "Security Processor" (SP) or "Wireless Trusted Module"
 * (WTM). This firmware then reports its results via the WTM registers,
 * which we read from the Application Processor (AP, i.e. main CPU) in this
 * driver.
 *
 * On the hardware side we have a PS/2 mouse and an AT keyboard, the data
 * is multiplexed through this system. We create a serio port for each one,
 * and demultiplex the data accordingly.
 */

/* WTM register offsets */
#define SECURE_PROCESSOR_COMMAND	0x40
#define COMMAND_RETURN_STATUS		0x80
#define COMMAND_FIFO_STATUS		0xc4
#define PJ_RST_INTERRUPT		0xc8
#define PJ_INTERRUPT_MASK		0xcc

/*
 * The upper byte of SECURE_PROCESSOR_COMMAND and COMMAND_RETURN_STATUS is
 * used to identify which port (device) is being talked to. The lower byte
 * is the data being sent/received.
 */
#define PORT_MASK	0xff00
#define DATA_MASK	0x00ff
#define PORT_SHIFT	8
#define KEYBOARD_PORT	0
#define TOUCHPAD_PORT	1

/* COMMAND_FIFO_STATUS */
#define CMD_CNTR_MASK		0x7 /* Number of pending/unprocessed commands */
#define MAX_PENDING_CMDS	4   /* from device specs */

/* PJ_RST_INTERRUPT */
#define SP_COMMAND_COMPLETE_RESET	0x1

/* PJ_INTERRUPT_MASK */
#define INT_0	(1 << 0)

/* COMMAND_FIFO_STATUS */
#define CMD_STS_MASK	0x100

struct olpc_apsp {
	struct device *dev;
	struct serio *kbio;
	struct serio *padio;
	void __iomem *base;
	int open_count;
	int irq;
};

static int olpc_apsp_write(struct serio *port, unsigned char val)
{
	struct olpc_apsp *priv = port->port_data;
	unsigned int i;
	u32 which = 0;

	if (port == priv->padio)
		which = TOUCHPAD_PORT << PORT_SHIFT;
	else
		which = KEYBOARD_PORT << PORT_SHIFT;

	dev_dbg(priv->dev, "olpc_apsp_write which=%x val=%x\n", which, val);
	for (i = 0; i < 50; i++) {
		u32 sts = readl(priv->base + COMMAND_FIFO_STATUS);
		if ((sts & CMD_CNTR_MASK) < MAX_PENDING_CMDS) {
			writel(which | val,
			       priv->base + SECURE_PROCESSOR_COMMAND);
			return 0;
		}
		/* SP busy. This has not been seen in practice. */
		mdelay(1);
	}

	dev_dbg(priv->dev, "olpc_apsp_write timeout, status=%x\n",
		readl(priv->base + COMMAND_FIFO_STATUS));

	return -ETIMEDOUT;
}

static irqreturn_t olpc_apsp_rx(int irq, void *dev_id)
{
	struct olpc_apsp *priv = dev_id;
	unsigned int w, tmp;
	struct serio *serio;

	/*
	 * Write 1 to PJ_RST_INTERRUPT to acknowledge and clear the interrupt
	 * Write 0xff00 to SECURE_PROCESSOR_COMMAND.
	 */
	tmp = readl(priv->base + PJ_RST_INTERRUPT);
	if (!(tmp & SP_COMMAND_COMPLETE_RESET)) {
		dev_warn(priv->dev, "spurious interrupt?\n");
		return IRQ_NONE;
	}

	w = readl(priv->base + COMMAND_RETURN_STATUS);
	dev_dbg(priv->dev, "olpc_apsp_rx %x\n", w);

	if (w >> PORT_SHIFT == KEYBOARD_PORT)
		serio = priv->kbio;
	else
		serio = priv->padio;

	serio_interrupt(serio, w & DATA_MASK, 0);

	/* Ack and clear interrupt */
	writel(tmp | SP_COMMAND_COMPLETE_RESET, priv->base + PJ_RST_INTERRUPT);
	writel(PORT_MASK, priv->base + SECURE_PROCESSOR_COMMAND);

	pm_wakeup_event(priv->dev, 1000);
	return IRQ_HANDLED;
}

static int olpc_apsp_open(struct serio *port)
{
	struct olpc_apsp *priv = port->port_data;
	unsigned int tmp;

	if (priv->open_count++ == 0) {
		/* Enable interrupt 0 by clearing its bit */
		tmp = readl(priv->base + PJ_INTERRUPT_MASK);
		writel(tmp & ~INT_0, priv->base + PJ_INTERRUPT_MASK);
	}

	return 0;
}

static void olpc_apsp_close(struct serio *port)
{
	struct olpc_apsp *priv = port->port_data;
	unsigned int tmp;

	if (--priv->open_count == 0) {
		/* Disable interrupt 0 */
		tmp = readl(priv->base + PJ_INTERRUPT_MASK);
		writel(tmp | INT_0, priv->base + PJ_INTERRUPT_MASK);
	}
}

static int olpc_apsp_probe(struct platform_device *pdev)
{
	struct serio *kb_serio, *pad_serio;
	struct olpc_apsp *priv;
	struct resource *res;
	struct device_node *np;
	unsigned long l;
	int error;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct olpc_apsp), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	np = pdev->dev.of_node;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base)) {
		dev_err(&pdev->dev, "Failed to map WTM registers\n");
		return PTR_ERR(priv->base);
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	l = readl(priv->base + COMMAND_FIFO_STATUS);
	if (!(l & CMD_STS_MASK)) {
		dev_err(&pdev->dev, "SP cannot accept commands.\n");
		return -EIO;
	}

	/* KEYBOARD */
	kb_serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!kb_serio)
		return -ENOMEM;
	kb_serio->id.type	= SERIO_8042_XL;
	kb_serio->write		= olpc_apsp_write;
	kb_serio->open		= olpc_apsp_open;
	kb_serio->close		= olpc_apsp_close;
	kb_serio->port_data	= priv;
	kb_serio->dev.parent	= &pdev->dev;
	strlcpy(kb_serio->name, "sp keyboard", sizeof(kb_serio->name));
	strlcpy(kb_serio->phys, "sp/serio0", sizeof(kb_serio->phys));
	priv->kbio		= kb_serio;
	serio_register_port(kb_serio);

	/* TOUCHPAD */
	pad_serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!pad_serio) {
		error = -ENOMEM;
		goto err_pad;
	}
	pad_serio->id.type	= SERIO_8042;
	pad_serio->write	= olpc_apsp_write;
	pad_serio->open		= olpc_apsp_open;
	pad_serio->close	= olpc_apsp_close;
	pad_serio->port_data	= priv;
	pad_serio->dev.parent	= &pdev->dev;
	strlcpy(pad_serio->name, "sp touchpad", sizeof(pad_serio->name));
	strlcpy(pad_serio->phys, "sp/serio1", sizeof(pad_serio->phys));
	priv->padio		= pad_serio;
	serio_register_port(pad_serio);

	error = request_irq(priv->irq, olpc_apsp_rx, 0, "olpc-apsp", priv);
	if (error) {
		dev_err(&pdev->dev, "Failed to request IRQ\n");
		goto err_irq;
	}

	priv->dev = &pdev->dev;
	device_init_wakeup(priv->dev, 1);
	platform_set_drvdata(pdev, priv);

	dev_dbg(&pdev->dev, "probed successfully.\n");
	return 0;

err_irq:
	serio_unregister_port(pad_serio);
err_pad:
	serio_unregister_port(kb_serio);
	return error;
}

static int olpc_apsp_remove(struct platform_device *pdev)
{
	struct olpc_apsp *priv = platform_get_drvdata(pdev);

	free_irq(priv->irq, priv);

	serio_unregister_port(priv->kbio);
	serio_unregister_port(priv->padio);

	return 0;
}

static const struct of_device_id olpc_apsp_dt_ids[] = {
	{ .compatible = "olpc,ap-sp", },
	{}
};
MODULE_DEVICE_TABLE(of, olpc_apsp_dt_ids);

static struct platform_driver olpc_apsp_driver = {
	.probe		= olpc_apsp_probe,
	.remove		= olpc_apsp_remove,
	.driver		= {
		.name	= "olpc-apsp",
		.of_match_table = olpc_apsp_dt_ids,
	},
};

MODULE_DESCRIPTION("OLPC AP-SP serio driver");
MODULE_LICENSE("GPL");
module_platform_driver(olpc_apsp_driver);
