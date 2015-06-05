/*
 * Copyright (C) 2005 Sascha Hauer, Pengutronix
 * Copyright (C) 2007 Wolfgang Grandegger <wg@grandegger.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/can/dev.h>
#include <linux/can/platform/sja1000.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include "sja1000.h"

#define DRV_NAME "sja1000_platform"
#define SP_CAN_CLOCK  (16000000 / 2)

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_AUTHOR("Wolfgang Grandegger <wg@grandegger.com>");
MODULE_DESCRIPTION("Socket-CAN driver for SJA1000 on the platform bus");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_LICENSE("GPL v2");

static u8 sp_read_reg8(const struct sja1000_priv *priv, int reg)
{
	return ioread8(priv->reg_base + reg);
}

static void sp_write_reg8(const struct sja1000_priv *priv, int reg, u8 val)
{
	iowrite8(val, priv->reg_base + reg);
}

static u8 sp_read_reg16(const struct sja1000_priv *priv, int reg)
{
	return ioread8(priv->reg_base + reg * 2);
}

static void sp_write_reg16(const struct sja1000_priv *priv, int reg, u8 val)
{
	iowrite8(val, priv->reg_base + reg * 2);
}

static u8 sp_read_reg32(const struct sja1000_priv *priv, int reg)
{
	return ioread8(priv->reg_base + reg * 4);
}

static void sp_write_reg32(const struct sja1000_priv *priv, int reg, u8 val)
{
	iowrite8(val, priv->reg_base + reg * 4);
}

static void sp_populate(struct sja1000_priv *priv,
			struct sja1000_platform_data *pdata,
			unsigned long resource_mem_flags)
{
	/* The CAN clock frequency is half the oscillator clock frequency */
	priv->can.clock.freq = pdata->osc_freq / 2;
	priv->ocr = pdata->ocr;
	priv->cdr = pdata->cdr;

	switch (resource_mem_flags & IORESOURCE_MEM_TYPE_MASK) {
	case IORESOURCE_MEM_32BIT:
		priv->read_reg = sp_read_reg32;
		priv->write_reg = sp_write_reg32;
		break;
	case IORESOURCE_MEM_16BIT:
		priv->read_reg = sp_read_reg16;
		priv->write_reg = sp_write_reg16;
		break;
	case IORESOURCE_MEM_8BIT:
	default:
		priv->read_reg = sp_read_reg8;
		priv->write_reg = sp_write_reg8;
		break;
	}
}

static void sp_populate_of(struct sja1000_priv *priv, struct device_node *of)
{
	int err;
	u32 prop;

	err = of_property_read_u32(of, "reg-io-width", &prop);
	if (err)
		prop = 1; /* 8 bit is default */

	switch (prop) {
	case 4:
		priv->read_reg = sp_read_reg32;
		priv->write_reg = sp_write_reg32;
		break;
	case 2:
		priv->read_reg = sp_read_reg16;
		priv->write_reg = sp_write_reg16;
		break;
	case 1:	/* fallthrough */
	default:
		priv->read_reg = sp_read_reg8;
		priv->write_reg = sp_write_reg8;
	}

	err = of_property_read_u32(of, "nxp,external-clock-frequency", &prop);
	if (!err)
		priv->can.clock.freq = prop / 2;
	else
		priv->can.clock.freq = SP_CAN_CLOCK; /* default */

	err = of_property_read_u32(of, "nxp,tx-output-mode", &prop);
	if (!err)
		priv->ocr |= prop & OCR_MODE_MASK;
	else
		priv->ocr |= OCR_MODE_NORMAL; /* default */

	err = of_property_read_u32(of, "nxp,tx-output-config", &prop);
	if (!err)
		priv->ocr |= (prop << OCR_TX_SHIFT) & OCR_TX_MASK;
	else
		priv->ocr |= OCR_TX0_PULLDOWN; /* default */

	err = of_property_read_u32(of, "nxp,clock-out-frequency", &prop);
	if (!err && prop) {
		u32 divider = priv->can.clock.freq * 2 / prop;

		if (divider > 1)
			priv->cdr |= divider / 2 - 1;
		else
			priv->cdr |= CDR_CLKOUT_MASK;
	} else {
		priv->cdr |= CDR_CLK_OFF; /* default */
	}

	if (!of_property_read_bool(of, "nxp,no-comparator-bypass"))
		priv->cdr |= CDR_CBP; /* default */
}

static int sp_probe(struct platform_device *pdev)
{
	int err, irq = 0;
	void __iomem *addr;
	struct net_device *dev;
	struct sja1000_priv *priv;
	struct resource *res_mem, *res_irq = NULL;
	struct sja1000_platform_data *pdata;
	struct device_node *of = pdev->dev.of_node;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata && !of) {
		dev_err(&pdev->dev, "No platform data provided!\n");
		return -ENODEV;
	}

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem)
		return -ENODEV;

	if (!devm_request_mem_region(&pdev->dev, res_mem->start,
				     resource_size(res_mem), DRV_NAME))
		return -EBUSY;

	addr = devm_ioremap_nocache(&pdev->dev, res_mem->start,
				    resource_size(res_mem));
	if (!addr)
		return -ENOMEM;

	if (of)
		irq = irq_of_parse_and_map(of, 0);
	else
		res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	if (!irq && !res_irq)
		return -ENODEV;

	dev = alloc_sja1000dev(0);
	if (!dev)
		return -ENOMEM;
	priv = netdev_priv(dev);

	if (res_irq) {
		irq = res_irq->start;
		priv->irq_flags = res_irq->flags & IRQF_TRIGGER_MASK;
		if (res_irq->flags & IORESOURCE_IRQ_SHAREABLE)
			priv->irq_flags |= IRQF_SHARED;
	} else {
		priv->irq_flags = IRQF_SHARED;
	}

	dev->irq = irq;
	priv->reg_base = addr;

	if (of)
		sp_populate_of(priv, of);
	else
		sp_populate(priv, pdata, res_mem->flags);

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = register_sja1000dev(dev);
	if (err) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			DRV_NAME, err);
		goto exit_free;
	}

	dev_info(&pdev->dev, "%s device registered (reg_base=%p, irq=%d)\n",
		 DRV_NAME, priv->reg_base, dev->irq);
	return 0;

 exit_free:
	free_sja1000dev(dev);
	return err;
}

static int sp_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	unregister_sja1000dev(dev);
	free_sja1000dev(dev);

	return 0;
}

static const struct of_device_id sp_of_table[] = {
	{.compatible = "nxp,sja1000"},
	{},
};
MODULE_DEVICE_TABLE(of, sp_of_table);

static struct platform_driver sp_driver = {
	.probe = sp_probe,
	.remove = sp_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = sp_of_table,
	},
};

module_platform_driver(sp_driver);
