// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car Generation 2 da9063(L)/da9210 regulator quirk
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
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/mfd/da9063/registers.h>

#define IRQC_BASE		0xe61c0000
#define IRQC_MONITOR		0x104	/* IRQn Signal Level Monitor Register */

#define REGULATOR_IRQ_MASK	BIT(2)	/* IRQ2, active low */

/* start of DA9210 System Control and Event Registers */
#define DA9210_REG_MASK_A		0x54

struct regulator_quirk {
	struct list_head		list;
	const struct of_device_id	*id;
	struct device_node		*np;
	struct of_phandle_args		irq_args;
	struct i2c_msg			i2c_msg;
	bool				shared;	/* IRQ line is shared */
};

static LIST_HEAD(quirk_list);
static void __iomem *irqc;

/* first byte sets the memory pointer, following are consecutive reg values */
static u8 da9063_irq_clr[] = { DA9063_REG_IRQ_MASK_A, 0xff, 0xff, 0xff, 0xff };
static u8 da9210_irq_clr[] = { DA9210_REG_MASK_A, 0xff, 0xff };

static struct i2c_msg da9063_msg = {
	.len = ARRAY_SIZE(da9063_irq_clr),
	.buf = da9063_irq_clr,
};

static struct i2c_msg da9210_msg = {
	.len = ARRAY_SIZE(da9210_irq_clr),
	.buf = da9210_irq_clr,
};

static const struct of_device_id rcar_gen2_quirk_match[] = {
	{ .compatible = "dlg,da9063", .data = &da9063_msg },
	{ .compatible = "dlg,da9063l", .data = &da9063_msg },
	{ .compatible = "dlg,da9210", .data = &da9210_msg },
	{},
};

static int regulator_quirk_notify(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct regulator_quirk *pos, *tmp;
	struct device *dev = data;
	struct i2c_client *client;
	static bool done;
	int ret;
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

	/*
	 * Send message to all PMICs that share an IRQ line to deassert it.
	 *
	 * WARNING: This works only if all the PMICs are on the same I2C bus.
	 */
	list_for_each_entry(pos, &quirk_list, list) {
		if (!pos->shared)
			continue;

		if (pos->np->parent != client->dev.parent->of_node)
			continue;

		dev_info(&client->dev, "clearing %s@0x%02x interrupts\n",
			 pos->id->compatible, pos->i2c_msg.addr);

		ret = i2c_transfer(client->adapter, &pos->i2c_msg, 1);
		if (ret != 1)
			dev_err(&client->dev, "i2c error %d\n", ret);
	}

	mon = ioread32(irqc + IRQC_MONITOR);
	if (mon & REGULATOR_IRQ_MASK)
		goto remove;

	return 0;

remove:
	dev_info(dev, "IRQ2 is not asserted, removing quirk\n");

	list_for_each_entry_safe(pos, tmp, &quirk_list, list) {
		list_del(&pos->list);
		of_node_put(pos->np);
		kfree(pos);
	}

	done = true;
	iounmap(irqc);
	return 0;
}

static struct notifier_block regulator_quirk_nb = {
	.notifier_call = regulator_quirk_notify
};

static int __init rcar_gen2_regulator_quirk(void)
{
	struct regulator_quirk *quirk, *pos, *tmp;
	struct of_phandle_args *argsa, *argsb;
	const struct of_device_id *id;
	struct device_node *np;
	u32 mon, addr;
	int ret;

	if (!of_machine_is_compatible("renesas,koelsch") &&
	    !of_machine_is_compatible("renesas,lager") &&
	    !of_machine_is_compatible("renesas,porter") &&
	    !of_machine_is_compatible("renesas,stout") &&
	    !of_machine_is_compatible("renesas,gose"))
		return -ENODEV;

	for_each_matching_node_and_match(np, rcar_gen2_quirk_match, &id) {
		if (!of_device_is_available(np)) {
			of_node_put(np);
			break;
		}

		ret = of_property_read_u32(np, "reg", &addr);
		if (ret)	/* Skip invalid entry and continue */
			continue;

		quirk = kzalloc(sizeof(*quirk), GFP_KERNEL);
		if (!quirk) {
			ret = -ENOMEM;
			of_node_put(np);
			goto err_mem;
		}

		argsa = &quirk->irq_args;
		memcpy(&quirk->i2c_msg, id->data, sizeof(quirk->i2c_msg));

		quirk->id = id;
		quirk->np = of_node_get(np);
		quirk->i2c_msg.addr = addr;

		ret = of_irq_parse_one(np, 0, argsa);
		if (ret) {	/* Skip invalid entry and continue */
			of_node_put(np);
			kfree(quirk);
			continue;
		}

		list_for_each_entry(pos, &quirk_list, list) {
			argsb = &pos->irq_args;

			if (argsa->args_count != argsb->args_count)
				continue;

			ret = memcmp(argsa->args, argsb->args,
				     argsa->args_count *
				     sizeof(argsa->args[0]));
			if (!ret) {
				pos->shared = true;
				quirk->shared = true;
			}
		}

		list_add_tail(&quirk->list, &quirk_list);
	}

	irqc = ioremap(IRQC_BASE, PAGE_SIZE);
	if (!irqc) {
		ret = -ENOMEM;
		goto err_mem;
	}

	mon = ioread32(irqc + IRQC_MONITOR);
	if (mon & REGULATOR_IRQ_MASK) {
		pr_debug("%s: IRQ2 is not asserted, not installing quirk\n",
			 __func__);
		ret = 0;
		goto err_free;
	}

	pr_info("IRQ2 is asserted, installing regulator quirk\n");

	bus_register_notifier(&i2c_bus_type, &regulator_quirk_nb);
	return 0;

err_free:
	iounmap(irqc);
err_mem:
	list_for_each_entry_safe(pos, tmp, &quirk_list, list) {
		list_del(&pos->list);
		of_node_put(pos->np);
		kfree(pos);
	}

	return ret;
}

arch_initcall(rcar_gen2_regulator_quirk);
