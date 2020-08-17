// SPDX-License-Identifier: GPL-2.0
/*
 * Device driver for regulators in HISI PMIC IC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 Hisilicon.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/mfd/hi6421-spmi-pmic.h>
#include <linux/irq.h>
#include <linux/spmi.h>
#ifndef NO_IRQ
#define NO_IRQ       0
#endif

/* 8-bit register offset in PMIC */
#define HISI_MASK_STATE			0xff

#define HISI_IRQ_KEY_NUM		0
#define HISI_IRQ_KEY_VALUE		0xc0
#define HISI_IRQ_KEY_DOWN		7
#define HISI_IRQ_KEY_UP			6

/*#define HISI_NR_IRQ			25*/
#define HISI_MASK_FIELD		0xFF
#define HISI_BITS			8

/*define the first group interrupt register number*/
#define HISI_PMIC_FIRST_GROUP_INT_NUM        2

static const struct mfd_cell hi6421v600_devs[] = {
	{ .name = "hi6421v600-regulator", },
};

/*
 * The PMIC register is only 8-bit.
 * Hisilicon SoC use hardware to map PMIC register into SoC mapping.
 * At here, we are accessing SoC register with 32-bit.
 */
u32 hi6421_spmi_pmic_read(struct hi6421_spmi_pmic *pmic, int reg)
{
	u32 ret;
	u8 read_value = 0;
	struct spmi_device *pdev;

	pdev = to_spmi_device(pmic->dev);
	if (!pdev) {
		pr_err("%s: pdev get failed!\n", __func__);
		return 0;
	}

	ret = spmi_ext_register_readl(pdev, reg,
				      (unsigned char *)&read_value, 1);
	if (ret) {
		pr_err("%s: spmi_ext_register_readl failed!\n", __func__);
		return 0;
	}
	return (u32)read_value;
}
EXPORT_SYMBOL(hi6421_spmi_pmic_read);

void hi6421_spmi_pmic_write(struct hi6421_spmi_pmic *pmic, int reg, u32 val)
{
	u32 ret;
	struct spmi_device *pdev;

	pdev = to_spmi_device(pmic->dev);
	if (!pdev) {
		pr_err("%s: pdev get failed!\n", __func__);
		return;
	}

	ret = spmi_ext_register_writel(pdev, reg, (unsigned char *)&val, 1);
	if (ret) {
		pr_err("%s: spmi_ext_register_writel failed!\n", __func__);
		return;
	}
}
EXPORT_SYMBOL(hi6421_spmi_pmic_write);

void hi6421_spmi_pmic_rmw(struct hi6421_spmi_pmic *pmic, int reg,
			  u32 mask, u32 bits)
{
	u32 data;
	unsigned long flags;

	spin_lock_irqsave(&pmic->lock, flags);
	data = hi6421_spmi_pmic_read(pmic, reg) & ~mask;
	data |= mask & bits;
	hi6421_spmi_pmic_write(pmic, reg, data);
	spin_unlock_irqrestore(&pmic->lock, flags);
}
EXPORT_SYMBOL(hi6421_spmi_pmic_rmw);

static irqreturn_t hi6421_spmi_irq_handler(int irq, void *data)
{
	struct hi6421_spmi_pmic *pmic = (struct hi6421_spmi_pmic *)data;
	unsigned long pending;
	int i, offset;

	for (i = 0; i < pmic->irqarray; i++) {
		pending = hi6421_spmi_pmic_read(pmic, (i + pmic->irq_addr.start_addr));
		pending &= HISI_MASK_FIELD;
		if (pending != 0)
			pr_debug("pending[%d]=0x%lx\n\r", i, pending);

		hi6421_spmi_pmic_write(pmic, (i + pmic->irq_addr.start_addr),
				       pending);

		/* solve powerkey order */
		if ((i == HISI_IRQ_KEY_NUM) && ((pending & HISI_IRQ_KEY_VALUE) == HISI_IRQ_KEY_VALUE)) {
			generic_handle_irq(pmic->irqs[HISI_IRQ_KEY_DOWN]);
			generic_handle_irq(pmic->irqs[HISI_IRQ_KEY_UP]);
			pending &= (~HISI_IRQ_KEY_VALUE);
		}

		if (pending) {
			for_each_set_bit(offset, &pending, HISI_BITS)
				generic_handle_irq(pmic->irqs[offset + i * HISI_BITS]);
		}
	}

	return IRQ_HANDLED;
}

static void hi6421_spmi_irq_mask(struct irq_data *d)
{
	struct hi6421_spmi_pmic *pmic = irq_data_get_irq_chip_data(d);
	u32 data, offset;
	unsigned long flags;

	offset = (irqd_to_hwirq(d) >> 3);
	offset += pmic->irq_mask_addr.start_addr;

	spin_lock_irqsave(&pmic->lock, flags);
	data = hi6421_spmi_pmic_read(pmic, offset);
	data |= (1 << (irqd_to_hwirq(d) & 0x07));
	hi6421_spmi_pmic_write(pmic, offset, data);
	spin_unlock_irqrestore(&pmic->lock, flags);
}

static void hi6421_spmi_irq_unmask(struct irq_data *d)
{
	struct hi6421_spmi_pmic *pmic = irq_data_get_irq_chip_data(d);
	u32 data, offset;
	unsigned long flags;

	offset = (irqd_to_hwirq(d) >> 3);
	offset += pmic->irq_mask_addr.start_addr;

	spin_lock_irqsave(&pmic->lock, flags);
	data = hi6421_spmi_pmic_read(pmic, offset);
	data &= ~(1 << (irqd_to_hwirq(d) & 0x07));
	hi6421_spmi_pmic_write(pmic, offset, data);
	spin_unlock_irqrestore(&pmic->lock, flags);
}

static struct irq_chip hi6421_spmi_pmu_irqchip = {
	.name		= "hisi-irq",
	.irq_mask	= hi6421_spmi_irq_mask,
	.irq_unmask	= hi6421_spmi_irq_unmask,
	.irq_disable	= hi6421_spmi_irq_mask,
	.irq_enable	= hi6421_spmi_irq_unmask,
};

static int hi6421_spmi_irq_map(struct irq_domain *d, unsigned int virq,
			irq_hw_number_t hw)
{
	struct hi6421_spmi_pmic *pmic = d->host_data;

	irq_set_chip_and_handler_name(virq, &hi6421_spmi_pmu_irqchip,
				      handle_simple_irq, "hisi");
	irq_set_chip_data(virq, pmic);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static const struct irq_domain_ops hi6421_spmi_domain_ops = {
	.map	= hi6421_spmi_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static int get_pmic_device_tree_data(struct device_node *np,
				     struct hi6421_spmi_pmic *pmic)
{
	int ret = 0;

	/*get pmic irq num*/
	ret = of_property_read_u32_array(np, "irq-num",
					 &pmic->irqnum, 1);
	if (ret) {
		pr_err("no irq-num property set\n");
		ret = -ENODEV;
		return ret;
	}

	/*get pmic irq array number*/
	ret = of_property_read_u32_array(np, "irq-array",
					 &pmic->irqarray, 1);
	if (ret) {
		pr_err("no irq-array property set\n");
		ret = -ENODEV;
		return ret;
	}

	/*SOC_PMIC_IRQ_MASK_0_ADDR*/
	ret = of_property_read_u32_array(np, "irq-mask-addr",
					 (int *)&pmic->irq_mask_addr, 2);
	if (ret) {
		pr_err("no irq-mask-addr property set\n");
		ret = -ENODEV;
		return ret;
	}

	/*SOC_PMIC_IRQ0_ADDR*/
	ret = of_property_read_u32_array(np, "irq-addr",
					 (int *)&pmic->irq_addr, 2);
	if (ret) {
		pr_err("no irq-addr property set\n");
		ret = -ENODEV;
		return ret;
	}

	return ret;
}

static void hi6421_spmi_pmic_irq_prc(struct hi6421_spmi_pmic *pmic)
{
	int i;

	for (i = 0 ; i < pmic->irq_mask_addr.array; i++)
		hi6421_spmi_pmic_write(pmic, pmic->irq_mask_addr.start_addr + i,
				       HISI_MASK_STATE);

	for (i = 0 ; i < pmic->irq_addr.array; i++) {
		unsigned int pending = hi6421_spmi_pmic_read(pmic, pmic->irq_addr.start_addr + i);

		pr_debug("PMU IRQ address value:irq[0x%x] = 0x%x\n",
			 pmic->irq_addr.start_addr + i, pending);
		hi6421_spmi_pmic_write(pmic, pmic->irq_addr.start_addr + i,
				       HISI_MASK_STATE);
	}
}

static int hi6421_spmi_pmic_probe(struct spmi_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hi6421_spmi_pmic *pmic = NULL;
	enum of_gpio_flags flags;
	int ret = 0;
	int i;
	unsigned int virq;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	/*TODO: get pmic dts info*/
	ret = get_pmic_device_tree_data(np, pmic);
	if (ret) {
		dev_err(&pdev->dev, "Error reading hisi pmic dts\n");
		return ret;
	}

	/* TODO: get and enable clk request */
	spin_lock_init(&pmic->lock);

	pmic->dev = dev;

	pmic->gpio = of_get_gpio_flags(np, 0, &flags);
	if (pmic->gpio < 0)
		return pmic->gpio;

	if (!gpio_is_valid(pmic->gpio))
		return -EINVAL;

	ret = gpio_request_one(pmic->gpio, GPIOF_IN, "pmic");
	if (ret < 0) {
		dev_err(dev, "failed to request gpio%d\n", pmic->gpio);
		return ret;
	}

	pmic->irq = gpio_to_irq(pmic->gpio);

	/* mask && clear IRQ status */
	hi6421_spmi_pmic_irq_prc(pmic);

	pmic->irqs = devm_kzalloc(dev, pmic->irqnum * sizeof(int), GFP_KERNEL);
	if (!pmic->irqs)
		goto irq_malloc;

	pmic->domain = irq_domain_add_simple(np, pmic->irqnum, 0,
					     &hi6421_spmi_domain_ops, pmic);
	if (!pmic->domain) {
		dev_err(dev, "failed irq domain add simple!\n");
		ret = -ENODEV;
		goto irq_domain;
	}

	for (i = 0; i < pmic->irqnum; i++) {
		virq = irq_create_mapping(pmic->domain, i);
		if (virq == NO_IRQ) {
			pr_debug("Failed mapping hwirq\n");
			ret = -ENOSPC;
			goto irq_create_mapping;
		}
		pmic->irqs[i] = virq;
		pr_info("[%s]. pmic->irqs[%d] = %d\n", __func__, i, pmic->irqs[i]);
	}

	ret = request_threaded_irq(pmic->irq, hi6421_spmi_irq_handler, NULL,
				   IRQF_TRIGGER_LOW | IRQF_SHARED | IRQF_NO_SUSPEND,
				   "pmic", pmic);
	if (ret < 0) {
		dev_err(dev, "could not claim pmic %d\n", ret);
		ret = -ENODEV;
		goto request_theaded_irq;
	}

	dev_set_drvdata(&pdev->dev, pmic);

	/*
	 * The logic below will rely that the pmic is already stored at
	 * drvdata.
	 */
	dev_dbg(&pdev->dev, "SPMI-PMIC: adding childs for %pOF\n",
		pdev->dev.of_node);
	ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
				   hi6421v600_devs, ARRAY_SIZE(hi6421v600_devs),
				   NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add child devices: %d\n", ret);
		return ret;
	}

	return 0;

request_theaded_irq:
irq_create_mapping:
irq_domain:
irq_malloc:
	gpio_free(pmic->gpio);
	return ret;
}

static void hi6421_spmi_pmic_remove(struct spmi_device *pdev)
{
	struct hi6421_spmi_pmic *pmic = dev_get_drvdata(&pdev->dev);

	free_irq(pmic->irq, pmic);
	gpio_free(pmic->gpio);
	devm_kfree(&pdev->dev, pmic);
}

static const struct of_device_id pmic_spmi_id_table[] = {
	{ .compatible = "hisilicon,hi6421-spmi-pmic" },
	{ }
};
MODULE_DEVICE_TABLE(of, pmic_spmi_id_table);

static struct spmi_driver hi6421_spmi_pmic_driver = {
	.driver = {
		.name	= "hi6421-spmi-pmic",
		.of_match_table = pmic_spmi_id_table,
	},
	.probe	= hi6421_spmi_pmic_probe,
	.remove	= hi6421_spmi_pmic_remove,
};
module_spmi_driver(hi6421_spmi_pmic_driver);

MODULE_DESCRIPTION("HiSilicon Hi6421v600 SPMI PMIC driver");
MODULE_LICENSE("GPL v2");
