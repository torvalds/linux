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

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/hi6421-spmi-pmic.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spmi.h>

/* 8-bit register offset in PMIC */
#define HISI_MASK_STATE			0xff

#define HISI_IRQ_ARRAY			2
#define HISI_IRQ_NUM			(HISI_IRQ_ARRAY * 8)

#define SOC_PMIC_IRQ_MASK_0_ADDR	0x0202
#define SOC_PMIC_IRQ0_ADDR		0x0212

#define HISI_IRQ_KEY_NUM		0
#define HISI_IRQ_KEY_VALUE		0xc0
#define HISI_IRQ_KEY_DOWN		7
#define HISI_IRQ_KEY_UP			6

#define HISI_MASK_FIELD			0xFF
#define HISI_BITS			8

/*define the first group interrupt register number*/
#define HISI_PMIC_FIRST_GROUP_INT_NUM	2

static const struct mfd_cell hi6421v600_devs[] = {
	{ .name = "hi6421v600-regulator", },
};

/*
 * The PMIC register is only 8-bit.
 * Hisilicon SoC use hardware to map PMIC register into SoC mapping.
 * At here, we are accessing SoC register with 32-bit.
 */
int hi6421_spmi_pmic_read(struct hi6421_spmi_pmic *pmic, int reg)
{
	struct spmi_device *pdev;
	u8 read_value = 0;
	u32 ret;

	pdev = to_spmi_device(pmic->dev);
	if (!pdev) {
		pr_err("%s: pdev get failed!\n", __func__);
		return -ENODEV;
	}

	ret = spmi_ext_register_readl(pdev, reg, &read_value, 1);
	if (ret) {
		pr_err("%s: spmi_ext_register_readl failed!\n", __func__);
		return ret;
	}
	return read_value;
}
EXPORT_SYMBOL(hi6421_spmi_pmic_read);

int hi6421_spmi_pmic_write(struct hi6421_spmi_pmic *pmic, int reg, u32 val)
{
	struct spmi_device *pdev;
	u32 ret;

	pdev = to_spmi_device(pmic->dev);
	if (!pdev) {
		pr_err("%s: pdev get failed!\n", __func__);
		return -ENODEV;
	}

	ret = spmi_ext_register_writel(pdev, reg, (unsigned char *)&val, 1);
	if (ret)
		pr_err("%s: spmi_ext_register_writel failed!\n", __func__);

	return ret;
}
EXPORT_SYMBOL(hi6421_spmi_pmic_write);

int hi6421_spmi_pmic_rmw(struct hi6421_spmi_pmic *pmic, int reg,
			 u32 mask, u32 bits)
{
	unsigned long flags;
	u32 data;
	int ret;

	spin_lock_irqsave(&pmic->lock, flags);
	data = hi6421_spmi_pmic_read(pmic, reg) & ~mask;
	data |= mask & bits;
	ret = hi6421_spmi_pmic_write(pmic, reg, data);
	spin_unlock_irqrestore(&pmic->lock, flags);

	return ret;
}
EXPORT_SYMBOL(hi6421_spmi_pmic_rmw);

static irqreturn_t hi6421_spmi_irq_handler(int irq, void *data)
{
	struct hi6421_spmi_pmic *pmic = (struct hi6421_spmi_pmic *)data;
	unsigned long pending;
	int i, offset;

	for (i = 0; i < HISI_IRQ_ARRAY; i++) {
		pending = hi6421_spmi_pmic_read(pmic, (i + SOC_PMIC_IRQ0_ADDR));
		pending &= HISI_MASK_FIELD;
		if (pending != 0)
			pr_debug("pending[%d]=0x%lx\n\r", i, pending);

		hi6421_spmi_pmic_write(pmic, (i + SOC_PMIC_IRQ0_ADDR), pending);

		/* solve powerkey order */
		if ((i == HISI_IRQ_KEY_NUM) &&
		    ((pending & HISI_IRQ_KEY_VALUE) == HISI_IRQ_KEY_VALUE)) {
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
	offset += SOC_PMIC_IRQ_MASK_0_ADDR;

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
	offset += SOC_PMIC_IRQ_MASK_0_ADDR;

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

static void hi6421_spmi_pmic_irq_prc(struct hi6421_spmi_pmic *pmic)
{
	int i, pending;

	for (i = 0 ; i < HISI_IRQ_ARRAY; i++)
		hi6421_spmi_pmic_write(pmic, SOC_PMIC_IRQ_MASK_0_ADDR + i,
				       HISI_MASK_STATE);

	for (i = 0 ; i < HISI_IRQ_ARRAY; i++) {
		pending = hi6421_spmi_pmic_read(pmic, SOC_PMIC_IRQ0_ADDR + i);

		pr_debug("PMU IRQ address value:irq[0x%x] = 0x%x\n",
			 SOC_PMIC_IRQ0_ADDR + i, pending);
		hi6421_spmi_pmic_write(pmic, SOC_PMIC_IRQ0_ADDR + i,
				       HISI_MASK_STATE);
	}
}

static int hi6421_spmi_pmic_probe(struct spmi_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hi6421_spmi_pmic *pmic;
	unsigned int virq;
	int ret, i;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	spin_lock_init(&pmic->lock);

	pmic->dev = dev;

	pmic->gpio = of_get_gpio(np, 0);
	if (pmic->gpio < 0)
		return pmic->gpio;

	if (!gpio_is_valid(pmic->gpio))
		return -EINVAL;

	ret = devm_gpio_request_one(dev, pmic->gpio, GPIOF_IN, "pmic");
	if (ret < 0) {
		dev_err(dev, "failed to request gpio%d\n", pmic->gpio);
		return ret;
	}

	pmic->irq = gpio_to_irq(pmic->gpio);

	hi6421_spmi_pmic_irq_prc(pmic);

	pmic->irqs = devm_kzalloc(dev, HISI_IRQ_NUM * sizeof(int), GFP_KERNEL);
	if (!pmic->irqs) {
		ret = -ENOMEM;
		goto irq_malloc;
	}

	pmic->domain = irq_domain_add_simple(np, HISI_IRQ_NUM, 0,
					     &hi6421_spmi_domain_ops, pmic);
	if (!pmic->domain) {
		dev_err(dev, "failed irq domain add simple!\n");
		ret = -ENODEV;
		goto irq_malloc;
	}

	for (i = 0; i < HISI_IRQ_NUM; i++) {
		virq = irq_create_mapping(pmic->domain, i);
		if (!virq) {
			dev_err(dev, "Failed mapping hwirq\n");
			ret = -ENOSPC;
			goto irq_malloc;
		}
		pmic->irqs[i] = virq;
		dev_dbg(dev, "%s: pmic->irqs[%d] = %d\n",
			__func__, i, pmic->irqs[i]);
	}

	ret = request_threaded_irq(pmic->irq, hi6421_spmi_irq_handler, NULL,
				   IRQF_TRIGGER_LOW | IRQF_SHARED | IRQF_NO_SUSPEND,
				   "pmic", pmic);
	if (ret < 0) {
		dev_err(dev, "could not claim pmic IRQ: error %d\n", ret);
		goto irq_malloc;
	}

	dev_set_drvdata(&pdev->dev, pmic);

	/*
	 * The logic below will rely that the pmic is already stored at
	 * drvdata.
	 */
	dev_dbg(&pdev->dev, "SPMI-PMIC: adding children for %pOF\n",
		pdev->dev.of_node);
	ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
				   hi6421v600_devs, ARRAY_SIZE(hi6421v600_devs),
				   NULL, 0, NULL);
	if (!ret)
		return 0;

	dev_err(dev, "Failed to add child devices: %d\n", ret);

irq_malloc:
	free_irq(pmic->irq, pmic);

	return ret;
}

static void hi6421_spmi_pmic_remove(struct spmi_device *pdev)
{
	struct hi6421_spmi_pmic *pmic = dev_get_drvdata(&pdev->dev);

	free_irq(pmic->irq, pmic);
}

static const struct of_device_id pmic_spmi_id_table[] = {
	{ .compatible = "hisilicon,hi6421-spmi" },
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
