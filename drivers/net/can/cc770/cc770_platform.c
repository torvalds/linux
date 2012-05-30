/*
 * Driver for CC770 and AN82527 CAN controllers on the platform bus
 *
 * Copyright (C) 2009, 2011 Wolfgang Grandegger <wg@grandegger.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * If platform data are used you should have similar definitions
 * in your board-specific code:
 *
 *   static struct cc770_platform_data myboard_cc770_pdata = {
 *           .osc_freq = 16000000,
 *           .cir = 0x41,
 *           .cor = 0x20,
 *           .bcr = 0x40,
 *   };
 *
 * Please see include/linux/can/platform/cc770.h for description of
 * above fields.
 *
 * If the device tree is used, you need a CAN node definition in your
 * DTS file similar to:
 *
 *   can@3,100 {
 *           compatible = "bosch,cc770";
 *           reg = <3 0x100 0x80>;
 *           interrupts = <2 0>;
 *           interrupt-parent = <&mpic>;
 *           bosch,external-clock-frequency = <16000000>;
 *   };
 *
 * See "Documentation/devicetree/bindings/net/can/cc770.txt" for further
 * information.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/platform/cc770.h>

#include "cc770.h"

#define DRV_NAME "cc770_platform"

MODULE_AUTHOR("Wolfgang Grandegger <wg@grandegger.com>");
MODULE_DESCRIPTION("Socket-CAN driver for CC770 on the platform bus");
MODULE_LICENSE("GPL v2");

#define CC770_PLATFORM_CAN_CLOCK  16000000

static u8 cc770_platform_read_reg(const struct cc770_priv *priv, int reg)
{
	return ioread8(priv->reg_base + reg);
}

static void cc770_platform_write_reg(const struct cc770_priv *priv, int reg,
				     u8 val)
{
	iowrite8(val, priv->reg_base + reg);
}

static int __devinit cc770_get_of_node_data(struct platform_device *pdev,
					    struct cc770_priv *priv)
{
	struct device_node *np = pdev->dev.of_node;
	const u32 *prop;
	int prop_size;
	u32 clkext;

	prop = of_get_property(np, "bosch,external-clock-frequency",
			       &prop_size);
	if (prop && (prop_size ==  sizeof(u32)))
		clkext = *prop;
	else
		clkext = CC770_PLATFORM_CAN_CLOCK; /* default */
	priv->can.clock.freq = clkext;

	/* The system clock may not exceed 10 MHz */
	if (priv->can.clock.freq > 10000000) {
		priv->cpu_interface |= CPUIF_DSC;
		priv->can.clock.freq /= 2;
	}

	/* The memory clock may not exceed 8 MHz */
	if (priv->can.clock.freq > 8000000)
		priv->cpu_interface |= CPUIF_DMC;

	if (of_get_property(np, "bosch,divide-memory-clock", NULL))
		priv->cpu_interface |= CPUIF_DMC;
	if (of_get_property(np, "bosch,iso-low-speed-mux", NULL))
		priv->cpu_interface |= CPUIF_MUX;

	if (!of_get_property(np, "bosch,no-comperator-bypass", NULL))
		priv->bus_config |= BUSCFG_CBY;
	if (of_get_property(np, "bosch,disconnect-rx0-input", NULL))
		priv->bus_config |= BUSCFG_DR0;
	if (of_get_property(np, "bosch,disconnect-rx1-input", NULL))
		priv->bus_config |= BUSCFG_DR1;
	if (of_get_property(np, "bosch,disconnect-tx1-output", NULL))
		priv->bus_config |= BUSCFG_DT1;
	if (of_get_property(np, "bosch,polarity-dominant", NULL))
		priv->bus_config |= BUSCFG_POL;

	prop = of_get_property(np, "bosch,clock-out-frequency", &prop_size);
	if (prop && (prop_size == sizeof(u32)) && *prop > 0) {
		u32 cdv = clkext / *prop;
		int slew;

		if (cdv > 0 && cdv < 16) {
			priv->cpu_interface |= CPUIF_CEN;
			priv->clkout |= (cdv - 1) & CLKOUT_CD_MASK;

			prop = of_get_property(np, "bosch,slew-rate",
					       &prop_size);
			if (prop && (prop_size == sizeof(u32))) {
				slew = *prop;
			} else {
				/* Determine default slew rate */
				slew = (CLKOUT_SL_MASK >>
					CLKOUT_SL_SHIFT) -
					((cdv * clkext - 1) / 8000000);
				if (slew < 0)
					slew = 0;
			}
			priv->clkout |= (slew << CLKOUT_SL_SHIFT) &
				CLKOUT_SL_MASK;
		} else {
			dev_dbg(&pdev->dev, "invalid clock-out-frequency\n");
		}
	}

	return 0;
}

static int __devinit cc770_get_platform_data(struct platform_device *pdev,
					     struct cc770_priv *priv)
{

	struct cc770_platform_data *pdata = pdev->dev.platform_data;

	priv->can.clock.freq = pdata->osc_freq;
	if (priv->cpu_interface & CPUIF_DSC)
		priv->can.clock.freq /= 2;
	priv->clkout = pdata->cor;
	priv->bus_config = pdata->bcr;
	priv->cpu_interface = pdata->cir;

	return 0;
}

static int __devinit cc770_platform_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct cc770_priv *priv;
	struct resource *mem;
	resource_size_t mem_size;
	void __iomem *base;
	int err, irq;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!mem || irq <= 0)
		return -ENODEV;

	mem_size = resource_size(mem);
	if (!request_mem_region(mem->start, mem_size, pdev->name))
		return -EBUSY;

	base = ioremap(mem->start, mem_size);
	if (!base) {
		err = -ENOMEM;
		goto exit_release_mem;
	}

	dev = alloc_cc770dev(0);
	if (!dev) {
		err = -ENOMEM;
		goto exit_unmap_mem;
	}

	dev->irq = irq;
	priv = netdev_priv(dev);
	priv->read_reg = cc770_platform_read_reg;
	priv->write_reg = cc770_platform_write_reg;
	priv->irq_flags = IRQF_SHARED;
	priv->reg_base = base;

	if (pdev->dev.of_node)
		err = cc770_get_of_node_data(pdev, priv);
	else if (pdev->dev.platform_data)
		err = cc770_get_platform_data(pdev, priv);
	else
		err = -ENODEV;
	if (err)
		goto exit_free_cc770;

	dev_dbg(&pdev->dev,
		 "reg_base=0x%p irq=%d clock=%d cpu_interface=0x%02x "
		 "bus_config=0x%02x clkout=0x%02x\n",
		 priv->reg_base, dev->irq, priv->can.clock.freq,
		 priv->cpu_interface, priv->bus_config, priv->clkout);

	dev_set_drvdata(&pdev->dev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = register_cc770dev(dev);
	if (err) {
		dev_err(&pdev->dev,
			"couldn't register CC700 device (err=%d)\n", err);
		goto exit_free_cc770;
	}

	return 0;

exit_free_cc770:
	free_cc770dev(dev);
exit_unmap_mem:
	iounmap(base);
exit_release_mem:
	release_mem_region(mem->start, mem_size);

	return err;
}

static int __devexit cc770_platform_remove(struct platform_device *pdev)
{
	struct net_device *dev = dev_get_drvdata(&pdev->dev);
	struct cc770_priv *priv = netdev_priv(dev);
	struct resource *mem;

	unregister_cc770dev(dev);
	iounmap(priv->reg_base);
	free_cc770dev(dev);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));

	return 0;
}

static struct of_device_id __devinitdata cc770_platform_table[] = {
	{.compatible = "bosch,cc770"}, /* CC770 from Bosch */
	{.compatible = "intc,82527"},  /* AN82527 from Intel CP */
	{},
};

static struct platform_driver cc770_platform_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cc770_platform_table,
	},
	.probe = cc770_platform_probe,
	.remove = __devexit_p(cc770_platform_remove),
};

module_platform_driver(cc770_platform_driver);
