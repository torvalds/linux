// SPDX-License-Identifier: GPL-2.0
/*
 * Device driver for irqs in HISI PMIC IC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 Hisilicon.
 * Copyright (c) 2020-2021 Huawei Technologies Co., Ltd.
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/irqdomain.h>
#include <linux/regmap.h>

struct hi6421v600_irq {
	struct device		*dev;
	struct irq_domain	*domain;
	int			irq;
	unsigned int		*irqs;
	struct regmap		*regmap;

	/* Protect IRQ mask changes */
	spinlock_t		lock;
};

enum hi6421v600_irq_list {
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
 * registers are used by the irqs.
 * See drivers/irq/hi6421-irq.c.
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

static irqreturn_t hi6421v600_irq_handler(int irq, void *__priv)
{
	struct hi6421v600_irq *priv = __priv;
	unsigned long pending;
	unsigned int in;
	int i, offset;

	for (i = 0; i < HISI_IRQ_BANK_SIZE; i++) {
		regmap_read(priv->regmap, SOC_PMIC_IRQ0_ADDR + i, &in);

		/* Mark pending IRQs as handled */
		regmap_write(priv->regmap, SOC_PMIC_IRQ0_ADDR + i, in);

		pending = in & HISI_8BITS_MASK;

		if (i == HISI_POWERKEY_IRQ_NUM &&
		    (pending & HISI_IRQ_POWERKEY_UP_DOWN) == HISI_IRQ_POWERKEY_UP_DOWN) {
			/*
			 * If both powerkey down and up IRQs are received,
			 * handle them at the right order
			 */
			generic_handle_irq(priv->irqs[POWERKEY_DOWN]);
			generic_handle_irq(priv->irqs[POWERKEY_UP]);
			pending &= ~HISI_IRQ_POWERKEY_UP_DOWN;
		}

		if (!pending)
			continue;

		for_each_set_bit(offset, &pending, BITS_PER_BYTE) {
			generic_handle_irq(priv->irqs[offset + i * BITS_PER_BYTE]);
		}
	}

	return IRQ_HANDLED;
}

static void hi6421v600_irq_mask(struct irq_data *d)
{
	struct hi6421v600_irq *priv = irq_data_get_irq_chip_data(d);
	unsigned long flags;
	unsigned int data;
	u32 offset;

	offset = HISI_IRQ_MASK_REG(d);

	spin_lock_irqsave(&priv->lock, flags);

	regmap_read(priv->regmap, offset, &data);
	data |= HISI_IRQ_MASK_BIT(d);
	regmap_write(priv->regmap, offset, data);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void hi6421v600_irq_unmask(struct irq_data *d)
{
	struct hi6421v600_irq *priv = irq_data_get_irq_chip_data(d);
	u32 data, offset;
	unsigned long flags;

	offset = HISI_IRQ_MASK_REG(d);

	spin_lock_irqsave(&priv->lock, flags);

	regmap_read(priv->regmap, offset, &data);
	data &= ~HISI_IRQ_MASK_BIT(d);
	regmap_write(priv->regmap, offset, data);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static struct irq_chip hi6421v600_pmu_irqchip = {
	.name		= "hi6421v600-irq",
	.irq_mask	= hi6421v600_irq_mask,
	.irq_unmask	= hi6421v600_irq_unmask,
	.irq_disable	= hi6421v600_irq_mask,
	.irq_enable	= hi6421v600_irq_unmask,
};

static int hi6421v600_irq_map(struct irq_domain *d, unsigned int virq,
			      irq_hw_number_t hw)
{
	struct hi6421v600_irq *priv = d->host_data;

	irq_set_chip_and_handler_name(virq, &hi6421v600_pmu_irqchip,
				      handle_simple_irq, "hi6421v600");
	irq_set_chip_data(virq, priv);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static const struct irq_domain_ops hi6421v600_domain_ops = {
	.map	= hi6421v600_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static void hi6421v600_irq_init(struct hi6421v600_irq *priv)
{
	int i;
	unsigned int pending;

	/* Mask all IRQs */
	for (i = 0; i < HISI_IRQ_BANK_SIZE; i++)
		regmap_write(priv->regmap, SOC_PMIC_IRQ_MASK_0_ADDR + i,
			     HISI_8BITS_MASK);

	/* Mark all IRQs as handled */
	for (i = 0; i < HISI_IRQ_BANK_SIZE; i++) {
		regmap_read(priv->regmap, SOC_PMIC_IRQ0_ADDR + i, &pending);
		regmap_write(priv->regmap, SOC_PMIC_IRQ0_ADDR + i,
			     HISI_8BITS_MASK);
	}
}

static int hi6421v600_irq_probe(struct platform_device *pdev)
{
	struct device *pmic_dev = pdev->dev.parent;
	struct device_node *np = pmic_dev->of_node;
	struct platform_device *pmic_pdev;
	struct device *dev = &pdev->dev;
	struct hi6421v600_irq *priv;
	struct regmap *regmap;
	unsigned int virq;
	int i, ret;

	/*
	 * This driver is meant to be called by hi6421-spmi-core,
	 * which should first set drvdata. If this doesn't happen, hit
	 * a warn on and return.
	 */
	regmap = dev_get_drvdata(pmic_dev);
	if (WARN_ON(!regmap))
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->regmap = regmap;

	spin_lock_init(&priv->lock);

	pmic_pdev = container_of(pmic_dev, struct platform_device, dev);

	priv->irq = platform_get_irq(pmic_pdev, 0);
	if (priv->irq < 0) {
		dev_err(dev, "Error %d when getting IRQs\n", priv->irq);
		return priv->irq;
	}

	platform_set_drvdata(pdev, priv);

	hi6421v600_irq_init(priv);

	priv->irqs = devm_kzalloc(dev, PMIC_IRQ_LIST_MAX * sizeof(int), GFP_KERNEL);
	if (!priv->irqs)
		return -ENOMEM;

	priv->domain = irq_domain_add_simple(np, PMIC_IRQ_LIST_MAX, 0,
					     &hi6421v600_domain_ops, priv);
	if (!priv->domain) {
		dev_err(dev, "Failed to create IRQ domain\n");
		return -ENODEV;
	}

	for (i = 0; i < PMIC_IRQ_LIST_MAX; i++) {
		virq = irq_create_mapping(priv->domain, i);
		if (!virq) {
			dev_err(dev, "Failed to map H/W IRQ\n");
			return -ENODEV;
		}
		priv->irqs[i] = virq;
	}

	ret = devm_request_threaded_irq(dev,
					priv->irq, hi6421v600_irq_handler,
					NULL,
					IRQF_TRIGGER_LOW | IRQF_SHARED | IRQF_NO_SUSPEND,
					"pmic", priv);
	if (ret < 0) {
		dev_err(dev, "Failed to start IRQ handling thread: error %d\n",
			ret);
		return ret;
	}

	return 0;
}

static const struct platform_device_id hi6421v600_irq_table[] = {
	{ .name = "hi6421v600-irq" },
	{},
};
MODULE_DEVICE_TABLE(platform, hi6421v600_irq_table);

static struct platform_driver hi6421v600_irq_driver = {
	.id_table = hi6421v600_irq_table,
	.driver = {
		.name = "hi6421v600-irq",
	},
	.probe	= hi6421v600_irq_probe,
};
module_platform_driver(hi6421v600_irq_driver);

MODULE_DESCRIPTION("HiSilicon Hi6421v600 IRQ driver");
MODULE_LICENSE("GPL v2");
