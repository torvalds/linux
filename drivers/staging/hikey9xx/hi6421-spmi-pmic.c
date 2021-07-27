// SPDX-License-Identifier: GPL-2.0
/*
 * Device driver for regulators in HISI PMIC IC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 Hisilicon.
 * Copyright (c) 2020-2021 Huawei Technologies Co., Ltd
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/hi6421-spmi-pmic.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spmi.h>

enum hi6421_spmi_pmic_irq_list {
	OTMP = 0,
	VBUS_CONNECT,
	VBUS_DISCONNECT,
	ALARMON_R,
	HOLD_6S,
	HOLD_1S,
	POWERKEY_UP,
	POWERKEY_DOWN,
	OCP_SCP_R,
	COUL_R,
	SIM0_HPD_R,
	SIM0_HPD_F,
	SIM1_HPD_R,
	SIM1_HPD_F,

	PMIC_IRQ_LIST_MAX
};

#define HISI_IRQ_BANK_SIZE		2

/*
 * IRQ number for the power key button and mask for both UP and DOWN IRQs
 */
#define HISI_POWERKEY_IRQ_NUM		0
#define HISI_IRQ_POWERKEY_UP_DOWN	(BIT(POWERKEY_DOWN) | BIT(POWERKEY_UP))

/*
 * Registers for IRQ address and IRQ mask bits
 *
 * Please notice that we need to regmap a larger region, as other
 * registers are used by the regulators.
 * See drivers/regulator/hi6421-regulator.c.
 */
#define SOC_PMIC_IRQ_MASK_0_ADDR	0x0202
#define SOC_PMIC_IRQ0_ADDR		0x0212

/*
 * The IRQs are mapped as:
 *
 *	======================  =============   ============	=====
 *	IRQ			MASK REGISTER	IRQ REGISTER	BIT
 *	======================  =============   ============	=====
 *	OTMP			0x0202		0x212		bit 0
 *	VBUS_CONNECT		0x0202		0x212		bit 1
 *	VBUS_DISCONNECT		0x0202		0x212		bit 2
 *	ALARMON_R		0x0202		0x212		bit 3
 *	HOLD_6S			0x0202		0x212		bit 4
 *	HOLD_1S			0x0202		0x212		bit 5
 *	POWERKEY_UP		0x0202		0x212		bit 6
 *	POWERKEY_DOWN		0x0202		0x212		bit 7
 *
 *	OCP_SCP_R		0x0203		0x213		bit 0
 *	COUL_R			0x0203		0x213		bit 1
 *	SIM0_HPD_R		0x0203		0x213		bit 2
 *	SIM0_HPD_F		0x0203		0x213		bit 3
 *	SIM1_HPD_R		0x0203		0x213		bit 4
 *	SIM1_HPD_F		0x0203		0x213		bit 5
 *	======================  =============   ============	=====
 *
 * Each mask register contains 8 bits. The ancillary macros below
 * convert a number from 0 to 14 into a register address and a bit mask
 */
#define HISI_IRQ_MASK_REG(irq_data)	(SOC_PMIC_IRQ_MASK_0_ADDR + \
					 (irqd_to_hwirq(irq_data) / BITS_PER_BYTE))
#define HISI_IRQ_MASK_BIT(irq_data)	BIT(irqd_to_hwirq(irq_data) & (BITS_PER_BYTE - 1))
#define HISI_8BITS_MASK			0xff

static const struct mfd_cell hi6421v600_devs[] = {
	{ .name = "hi6421v600-regulator", },
};

static irqreturn_t hi6421_spmi_irq_handler(int irq, void *priv)
{
	struct hi6421_spmi_pmic *ddata = (struct hi6421_spmi_pmic *)priv;
	unsigned long pending;
	unsigned int in;
	int i, offset;

	for (i = 0; i < HISI_IRQ_BANK_SIZE; i++) {
		regmap_read(ddata->regmap, SOC_PMIC_IRQ0_ADDR + i, &in);

		/* Mark pending IRQs as handled */
		regmap_write(ddata->regmap, SOC_PMIC_IRQ0_ADDR + i, in);

		pending = in & HISI_8BITS_MASK;

		if (i == HISI_POWERKEY_IRQ_NUM &&
		    (pending & HISI_IRQ_POWERKEY_UP_DOWN) == HISI_IRQ_POWERKEY_UP_DOWN) {
			/*
			 * If both powerkey down and up IRQs are received,
			 * handle them at the right order
			 */
			generic_handle_irq(ddata->irqs[POWERKEY_DOWN]);
			generic_handle_irq(ddata->irqs[POWERKEY_UP]);
			pending &= ~HISI_IRQ_POWERKEY_UP_DOWN;
		}

		if (!pending)
			continue;

		for_each_set_bit(offset, &pending, BITS_PER_BYTE) {
			generic_handle_irq(ddata->irqs[offset + i * BITS_PER_BYTE]);
		}
	}

	return IRQ_HANDLED;
}

static void hi6421_spmi_irq_mask(struct irq_data *d)
{
	struct hi6421_spmi_pmic *ddata = irq_data_get_irq_chip_data(d);
	unsigned long flags;
	unsigned int data;
	u32 offset;

	offset = HISI_IRQ_MASK_REG(d);

	spin_lock_irqsave(&ddata->lock, flags);

	regmap_read(ddata->regmap, offset, &data);
	data |= HISI_IRQ_MASK_BIT(d);
	regmap_write(ddata->regmap, offset, data);

	spin_unlock_irqrestore(&ddata->lock, flags);
}

static void hi6421_spmi_irq_unmask(struct irq_data *d)
{
	struct hi6421_spmi_pmic *ddata = irq_data_get_irq_chip_data(d);
	u32 data, offset;
	unsigned long flags;

	offset = HISI_IRQ_MASK_REG(d);

	spin_lock_irqsave(&ddata->lock, flags);

	regmap_read(ddata->regmap, offset, &data);
	data &= ~HISI_IRQ_MASK_BIT(d);
	regmap_write(ddata->regmap, offset, data);

	spin_unlock_irqrestore(&ddata->lock, flags);
}

static struct irq_chip hi6421_spmi_pmu_irqchip = {
	.name		= "hi6421v600-irq",
	.irq_mask	= hi6421_spmi_irq_mask,
	.irq_unmask	= hi6421_spmi_irq_unmask,
	.irq_disable	= hi6421_spmi_irq_mask,
	.irq_enable	= hi6421_spmi_irq_unmask,
};

static int hi6421_spmi_irq_map(struct irq_domain *d, unsigned int virq,
			       irq_hw_number_t hw)
{
	struct hi6421_spmi_pmic *ddata = d->host_data;

	irq_set_chip_and_handler_name(virq, &hi6421_spmi_pmu_irqchip,
				      handle_simple_irq, "hi6421v600");
	irq_set_chip_data(virq, ddata);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static const struct irq_domain_ops hi6421_spmi_domain_ops = {
	.map	= hi6421_spmi_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static void hi6421_spmi_pmic_irq_init(struct hi6421_spmi_pmic *ddata)
{
	int i;
	unsigned int pending;

	/* Mask all IRQs */
	for (i = 0; i < HISI_IRQ_BANK_SIZE; i++)
		regmap_write(ddata->regmap, SOC_PMIC_IRQ_MASK_0_ADDR + i,
			     HISI_8BITS_MASK);

	/* Mark all IRQs as handled */
	for (i = 0; i < HISI_IRQ_BANK_SIZE; i++) {
		regmap_read(ddata->regmap, SOC_PMIC_IRQ0_ADDR + i, &pending);
		regmap_write(ddata->regmap, SOC_PMIC_IRQ0_ADDR + i,
			     HISI_8BITS_MASK);
	}
}

static const struct regmap_config regmap_config = {
	.reg_bits	= 16,
	.val_bits	= BITS_PER_BYTE,
	.max_register	= 0xffff,
	.fast_io	= true
};

static int hi6421_spmi_pmic_probe(struct spmi_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hi6421_spmi_pmic *ddata;
	unsigned int virq;
	int ret, i;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->regmap = devm_regmap_init_spmi_ext(pdev, &regmap_config);
	if (IS_ERR(ddata->regmap))
		return PTR_ERR(ddata->regmap);

	spin_lock_init(&ddata->lock);

	ddata->dev = dev;

	ddata->gpio = of_get_gpio(np, 0);
	if (ddata->gpio < 0)
		return ddata->gpio;

	if (!gpio_is_valid(ddata->gpio))
		return -EINVAL;

	ret = devm_gpio_request_one(dev, ddata->gpio, GPIOF_IN, "pmic");
	if (ret < 0) {
		dev_err(dev, "Failed to request gpio%d\n", ddata->gpio);
		return ret;
	}

	ddata->irq = gpio_to_irq(ddata->gpio);

	hi6421_spmi_pmic_irq_init(ddata);

	ddata->irqs = devm_kzalloc(dev, PMIC_IRQ_LIST_MAX * sizeof(int), GFP_KERNEL);
	if (!ddata->irqs)
		return -ENOMEM;

	ddata->domain = irq_domain_add_simple(np, PMIC_IRQ_LIST_MAX, 0,
					      &hi6421_spmi_domain_ops, ddata);
	if (!ddata->domain) {
		dev_err(dev, "Failed to create IRQ domain\n");
		return -ENODEV;
	}

	for (i = 0; i < PMIC_IRQ_LIST_MAX; i++) {
		virq = irq_create_mapping(ddata->domain, i);
		if (!virq) {
			dev_err(dev, "Failed to map H/W IRQ\n");
			return -ENODEV;
		}
		ddata->irqs[i] = virq;
	}

	ret = devm_request_threaded_irq(dev,
					ddata->irq, hi6421_spmi_irq_handler,
					NULL,
				        IRQF_TRIGGER_LOW | IRQF_SHARED | IRQF_NO_SUSPEND,
				        "pmic", ddata);
	if (ret < 0) {
		dev_err(dev, "Failed to start IRQ handling thread: error %d\n",
			ret);
		return ret;
	}

	dev_set_drvdata(&pdev->dev, ddata);

	ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
				   hi6421v600_devs, ARRAY_SIZE(hi6421v600_devs),
				   NULL, 0, NULL);
	if (ret < 0)
		dev_err(dev, "Failed to add child devices: %d\n", ret);

	return ret;
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
};
module_spmi_driver(hi6421_spmi_pmic_driver);

MODULE_DESCRIPTION("HiSilicon Hi6421v600 SPMI PMIC driver");
MODULE_LICENSE("GPL v2");
