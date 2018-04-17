/*
 * R-Car Generation 2 da9063/da9210 regulator quirk
 *
 * Certain Gen2 development boards have an da9063 and one or more da9210
 * regulators. All of these regulators have their interrupt request lines
 * tied to the same interrupt pin (IRQ2) on the SoC.
 *
 * After cold boot or da9063-induced restart, both the da9063 and da9210 seem
 * to assert their interrupt request lines.  Hence as soon as one driver
 * requests this irq, it gets stuck in an interrupt storm, as it only manages
 * to deassert its own interrupt request line, and the other driver hasn't
 * installed an interrupt handler yet.
 *
 * To handle this, install a quirk that masks the interrupts in both the
 * da9063 and da9210.  This quirk has to run after the i2c master driver has
 * been initialized, but before the i2c slave drivers are initialized.
 *
 * Copyright (C) 2015 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/mfd/da9063/registers.h>


#define IRQC_BASE		0xe61c0000
#define IRQC_MONITOR		0x104	/* IRQn Signal Level Monitor Register */

#define REGULATOR_IRQ_MASK	BIT(2)	/* IRQ2, active low */

/* start of DA9210 System Control and Event Registers */
#define DA9210_REG_MASK_A		0x54

static void __iomem *irqc;

/* first byte sets the memory pointer, following are consecutive reg values */
static u8 da9063_irq_clr[] = { DA9063_REG_IRQ_MASK_A, 0xff, 0xff, 0xff, 0xff };
static u8 da9210_irq_clr[] = { DA9210_REG_MASK_A, 0xff, 0xff };

static struct i2c_msg da9xxx_msgs[3] = {
	{
		.addr = 0x58,
		.len = ARRAY_SIZE(da9063_irq_clr),
		.buf = da9063_irq_clr,
	}, {
		.addr = 0x68,
		.len = ARRAY_SIZE(da9210_irq_clr),
		.buf = da9210_irq_clr,
	}, {
		.addr = 0x70,
		.len = ARRAY_SIZE(da9210_irq_clr),
		.buf = da9210_irq_clr,
	},
};

static int regulator_quirk_notify(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct device *dev = data;
	struct i2c_client *client;
	static bool done;
	u32 mon;

	if (done)
		return 0;

	mon = ioread32(irqc + IRQC_MONITOR);
	dev_dbg(dev, "%s: %ld, IRQC_MONITOR = 0x%x\n", __func__, action, mon);
	if (mon & REGULATOR_IRQ_MASK)
		goto remove;

	if (action != BUS_NOTIFY_ADD_DEVICE || dev->type == &i2c_adapter_type)
		return 0;

	client = to_i2c_client(dev);
	dev_dbg(dev, "Detected %s\n", client->name);

	if ((client->addr == 0x58 && !strcmp(client->name, "da9063")) ||
	    (client->addr == 0x68 && !strcmp(client->name, "da9210")) ||
	    (client->addr == 0x70 && !strcmp(client->name, "da9210"))) {
		int ret, len;

		/* There are two DA9210 on Stout, one on the other boards. */
		len = of_machine_is_compatible("renesas,stout") ? 3 : 2;

		dev_info(&client->dev, "clearing da9063/da9210 interrupts\n");
		ret = i2c_transfer(client->adapter, da9xxx_msgs, len);
		if (ret != len)
			dev_err(&client->dev, "i2c error %d\n", ret);
	}

	mon = ioread32(irqc + IRQC_MONITOR);
	if (mon & REGULATOR_IRQ_MASK)
		goto remove;

	return 0;

remove:
	dev_info(dev, "IRQ2 is not asserted, removing quirk\n");

	done = true;
	iounmap(irqc);
	return 0;
}

static struct notifier_block regulator_quirk_nb = {
	.notifier_call = regulator_quirk_notify
};

static int __init rcar_gen2_regulator_quirk(void)
{
	u32 mon;

	if (!of_machine_is_compatible("renesas,koelsch") &&
	    !of_machine_is_compatible("renesas,lager") &&
	    !of_machine_is_compatible("renesas,stout") &&
	    !of_machine_is_compatible("renesas,gose"))
		return -ENODEV;

	irqc = ioremap(IRQC_BASE, PAGE_SIZE);
	if (!irqc)
		return -ENOMEM;

	mon = ioread32(irqc + IRQC_MONITOR);
	if (mon & REGULATOR_IRQ_MASK) {
		pr_debug("%s: IRQ2 is not asserted, not installing quirk\n",
			 __func__);
		iounmap(irqc);
		return 0;
	}

	pr_info("IRQ2 is asserted, installing da9063/da9210 regulator quirk\n");

	bus_register_notifier(&i2c_bus_type, &regulator_quirk_nb);
	return 0;
}

arch_initcall(rcar_gen2_regulator_quirk);
