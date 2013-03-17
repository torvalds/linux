/*
 * CEC example driver for demonstrating the use of this HDMI CEC stack
 * proposal.
 *
 * Copyright (C) 2012, Florian Fainelli <f.fainelli@gmail.com>
 *
 * Licensed under ther terms of the GPLv2
 *
 * This driver assumes we would be writing to some memory-mapped CEC
 * hardware with the the register mapping described below. Such hardware
 * would provide some kind of hardware FIFO to transmit the message,
 * and interrupt us upon reception of a complete CEC frame. It does not
 * support hardware counters nor detaching from host.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include "hdmi-cec.h"

#define CEC_EXAMPLE_ADDR_REG	0x00
#define CEC_EXAMPLE_RST_REG	0x01
#define CEC_EXAMPLE_RX_MODE_REG	0x02
#define CEC_EXAMPLE_ATTACH_REG	0x03
#define CEC_EXAMPLE_ISR_REG	0x04
#define CEC_EXAMPLE_ISR_TX	(1 << 0) /* interrupt on TX completion */
#define CEC_EXAMPLE_ISR_RX	(1 << 1) /* interrupt on RX completion */
#define CEC_EXAMPLE_ISR_MSG_CNT	(1 << 2) /* size of the incoming message */
#define CEC_EXAMPLE_IM_REG	0x05
#define CEC_EXAMPLE_FIFO_REG	0x06

struct cec_example_priv {
	void __iomem		*regs;
	struct platform_device	*pdev;
	int			irq;
	struct cec_driver	cec_drv;
	struct completion	tx_complete;
	spinlock_t		lock;
};

static irqreturn_t cec_example_isr(int irq, void *dev_id)
{
	struct cec_example_priv *priv = dev_id;
	u8 cause;
	u8 msg[CEC_MAX_MSG_LEN], count;

	/* read cause and mask all other causes */
	cause = ioread8(priv->regs + CEC_EXAMPLE_IM_REG);
	iowrite8(0, priv->regs + CEC_EXAMPLE_IM_REG);

	if (cause & CEC_EXAMPLE_ISR_TX)
		complete(&priv->tx_complete);

	/* we should probably use a bottom-half here, but there is not
	 * so much work to do */
	if (cause & CEC_EXAMPLE_ISR_RX) {
		count = cause >> CEC_EXAMPLE_ISR_MSG_CNT;
		ioread8_rep(priv->regs + CEC_EXAMPLE_FIFO_REG,
				msg, count);
		cec_receive_message(&priv->cec_drv, msg, count);
	}

	return IRQ_HANDLED;
}

static int cec_example_set_addr(struct cec_driver *drv, const u8 addr)
{
	struct cec_example_priv *priv =
		container_of(drv, struct cec_example_priv, cec_drv);

	iowrite8(addr, priv->regs + CEC_EXAMPLE_ADDR_REG);

	return 0;
}

static int
cec_example_send(struct cec_driver *drv, const u8 *data, const u8 len)
{
	struct cec_example_priv *priv =
		container_of(drv, struct cec_example_priv, cec_drv);
	unsigned int timeout;
	u8 reg;
	unsigned long flags;

	init_completion(&priv->tx_complete);

	spin_lock_irqsave(&priv->lock, flags);
	/* enable TX completion interrupt */
	reg = ioread8(priv->regs + CEC_EXAMPLE_ISR_REG);
	reg |=  CEC_EXAMPLE_ISR_TX;
	iowrite8(reg, priv->regs + CEC_EXAMPLE_ISR_REG);

	/* write to the hardware fifo */
	iowrite8_rep(priv->regs + CEC_EXAMPLE_FIFO_REG,
			data, len);

	spin_unlock_irqrestore(&priv->lock, flags);

	/* ISR will complete our tx completion */
	timeout = wait_for_completion_interruptible_timeout(
				&priv->tx_complete, HZ / 2);

	return !timeout ? -ETIMEDOUT : 0;
}

static int cec_example_reset(struct cec_driver *drv)
{
	struct cec_example_priv *priv =
		container_of(drv, struct cec_example_priv, cec_drv);

	/* clear all registers */
	iowrite8_rep(priv->regs, 0, 6 + CEC_MAX_MSG_LEN);

	return 0;
}

static struct cec_driver_ops cec_example_ops = {
	.set_logical_address	= cec_example_set_addr,
	.send			= cec_example_send,
	.reset			= cec_example_reset,
};

static int cec_example_probe(struct platform_device *pdev)
{
	struct cec_example_priv *priv;
	struct resource *r;
	int irq;
	int ret;

	r = platform_get_resource(pdev, 0, IORESOURCE_MEM);
	if (!r)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -ENODEV;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = ioremap(r->start, resource_size(r));
	if (!priv->regs) {
		ret = -ENOMEM;
		goto out;
	}

	spin_lock_init(&priv->lock);

	platform_set_drvdata(pdev, priv);
	priv->irq = irq;
	priv->pdev = pdev;
	priv->cec_drv.ops = &cec_example_ops;

	ret = register_cec_driver(&priv->cec_drv);
	if (ret)
		goto out_iomem;

	ret = request_irq(irq, cec_example_isr, 0, pdev->name, priv);
	if (ret)
		goto out_drv;

	/* enable CEC RX completion (TX enabled later) */
	iowrite8(CEC_EXAMPLE_ISR_RX, priv->regs + CEC_EXAMPLE_ISR_REG);

	dev_info(&pdev->dev, "CEC example driver registered");

	return 0;

out_drv:
	unregister_cec_driver(&priv->cec_drv);
out_iomem:
	iounmap(priv->regs);
out:
	kfree(priv);
	return ret;
}

static int cec_example_remove(struct platform_device *pdev)
{
	struct cec_example_priv *priv = platform_get_drvdata(pdev);

	/* disable all interrupts */
	iowrite8(0, priv->regs + CEC_EXAMPLE_ISR_REG);
	free_irq(priv->irq, priv);
	unregister_cec_driver(&priv->cec_drv);
	iounmap(priv->regs);
	kfree(priv);

	return 0;
}

static struct platform_driver cec_example_driver = {
	.driver	= {
		.name	= "cec-example",
		.owner	= THIS_MODULE,
	},
	.probe	= cec_example_probe,
	.remove	= __devexit_p(cec_example_remove),
};

static int __init cec_example_init(void)
{
	return platform_driver_register(&cec_example_driver);
}

static void __exit cec_example_exit(void)
{
	platform_driver_unregister(&cec_example_driver);
}

module_init(cec_example_init);
module_exit(cec_example_exit);

MODULE_AUTHOR("Florian Fainelli <f.fainelli@gmail.com>");
MODULE_DESCRIPTION("HDMI-CEC example driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cec-example");
