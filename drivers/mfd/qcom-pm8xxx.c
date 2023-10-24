// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/ssbi.h>
#include <linux/regmap.h>
#include <linux/of_platform.h>
#include <linux/mfd/core.h>

#define	SSBI_REG_ADDR_IRQ_BASE		0x1BB

#define	SSBI_REG_ADDR_IRQ_ROOT		(SSBI_REG_ADDR_IRQ_BASE + 0)
#define	SSBI_REG_ADDR_IRQ_M_STATUS1	(SSBI_REG_ADDR_IRQ_BASE + 1)
#define	SSBI_REG_ADDR_IRQ_M_STATUS2	(SSBI_REG_ADDR_IRQ_BASE + 2)
#define	SSBI_REG_ADDR_IRQ_M_STATUS3	(SSBI_REG_ADDR_IRQ_BASE + 3)
#define	SSBI_REG_ADDR_IRQ_M_STATUS4	(SSBI_REG_ADDR_IRQ_BASE + 4)
#define	SSBI_REG_ADDR_IRQ_BLK_SEL	(SSBI_REG_ADDR_IRQ_BASE + 5)
#define	SSBI_REG_ADDR_IRQ_IT_STATUS	(SSBI_REG_ADDR_IRQ_BASE + 6)
#define	SSBI_REG_ADDR_IRQ_CONFIG	(SSBI_REG_ADDR_IRQ_BASE + 7)
#define	SSBI_REG_ADDR_IRQ_RT_STATUS	(SSBI_REG_ADDR_IRQ_BASE + 8)

#define	PM8821_SSBI_REG_ADDR_IRQ_BASE	0x100
#define	PM8821_SSBI_REG_ADDR_IRQ_MASTER0 (PM8821_SSBI_REG_ADDR_IRQ_BASE + 0x30)
#define	PM8821_SSBI_REG_ADDR_IRQ_MASTER1 (PM8821_SSBI_REG_ADDR_IRQ_BASE + 0xb0)
#define	PM8821_SSBI_REG(m, b, offset) \
			((m == 0) ? \
			(PM8821_SSBI_REG_ADDR_IRQ_MASTER0 + b + offset) : \
			(PM8821_SSBI_REG_ADDR_IRQ_MASTER1 + b + offset))
#define	PM8821_SSBI_ADDR_IRQ_ROOT(m, b)		PM8821_SSBI_REG(m, b, 0x0)
#define	PM8821_SSBI_ADDR_IRQ_CLEAR(m, b)	PM8821_SSBI_REG(m, b, 0x01)
#define	PM8821_SSBI_ADDR_IRQ_MASK(m, b)		PM8821_SSBI_REG(m, b, 0x08)
#define	PM8821_SSBI_ADDR_IRQ_RT_STATUS(m, b)	PM8821_SSBI_REG(m, b, 0x0f)

#define	PM8821_BLOCKS_PER_MASTER	7

#define	PM_IRQF_LVL_SEL			0x01	/* level select */
#define	PM_IRQF_MASK_FE			0x02	/* mask falling edge */
#define	PM_IRQF_MASK_RE			0x04	/* mask rising edge */
#define	PM_IRQF_CLR			0x08	/* clear interrupt */
#define	PM_IRQF_BITS_MASK		0x70
#define	PM_IRQF_BITS_SHIFT		4
#define	PM_IRQF_WRITE			0x80

#define	PM_IRQF_MASK_ALL		(PM_IRQF_MASK_FE | \
					PM_IRQF_MASK_RE)

#define REG_HWREV		0x002  /* PMIC4 revision */
#define REG_HWREV_2		0x0E8  /* PMIC4 revision 2 */

#define PM8XXX_NR_IRQS		256
#define PM8821_NR_IRQS		112

struct pm_irq_data {
	int num_irqs;
	struct irq_chip *irq_chip;
	irq_handler_t irq_handler;
};

struct pm_irq_chip {
	struct regmap		*regmap;
	spinlock_t		pm_irq_lock;
	struct irq_domain	*irqdomain;
	unsigned int		num_blocks;
	unsigned int		num_masters;
	const struct pm_irq_data *pm_irq_data;
	/* MUST BE AT THE END OF THIS STRUCT */
	u8			config[];
};

static int pm8xxx_read_block_irq(struct pm_irq_chip *chip, unsigned int bp,
				 unsigned int *ip)
{
	int	rc;

	spin_lock(&chip->pm_irq_lock);
	rc = regmap_write(chip->regmap, SSBI_REG_ADDR_IRQ_BLK_SEL, bp);
	if (rc) {
		pr_err("Failed Selecting Block %d rc=%d\n", bp, rc);
		goto bail;
	}

	rc = regmap_read(chip->regmap, SSBI_REG_ADDR_IRQ_IT_STATUS, ip);
	if (rc)
		pr_err("Failed Reading Status rc=%d\n", rc);
bail:
	spin_unlock(&chip->pm_irq_lock);
	return rc;
}

static int
pm8xxx_config_irq(struct pm_irq_chip *chip, unsigned int bp, unsigned int cp)
{
	int	rc;
	unsigned long flags;

	spin_lock_irqsave(&chip->pm_irq_lock, flags);
	rc = regmap_write(chip->regmap, SSBI_REG_ADDR_IRQ_BLK_SEL, bp);
	if (rc) {
		pr_err("Failed Selecting Block %d rc=%d\n", bp, rc);
		goto bail;
	}

	cp |= PM_IRQF_WRITE;
	rc = regmap_write(chip->regmap, SSBI_REG_ADDR_IRQ_CONFIG, cp);
	if (rc)
		pr_err("Failed Configuring IRQ rc=%d\n", rc);
bail:
	spin_unlock_irqrestore(&chip->pm_irq_lock, flags);
	return rc;
}

static int pm8xxx_irq_block_handler(struct pm_irq_chip *chip, int block)
{
	int pmirq, i, ret = 0;
	unsigned int bits;

	ret = pm8xxx_read_block_irq(chip, block, &bits);
	if (ret) {
		pr_err("Failed reading %d block ret=%d", block, ret);
		return ret;
	}
	if (!bits) {
		pr_err("block bit set in master but no irqs: %d", block);
		return 0;
	}

	/* Check IRQ bits */
	for (i = 0; i < 8; i++) {
		if (bits & (1 << i)) {
			pmirq = block * 8 + i;
			generic_handle_domain_irq(chip->irqdomain, pmirq);
		}
	}
	return 0;
}

static int pm8xxx_irq_master_handler(struct pm_irq_chip *chip, int master)
{
	unsigned int blockbits;
	int block_number, i, ret = 0;

	ret = regmap_read(chip->regmap, SSBI_REG_ADDR_IRQ_M_STATUS1 + master,
			  &blockbits);
	if (ret) {
		pr_err("Failed to read master %d ret=%d\n", master, ret);
		return ret;
	}
	if (!blockbits) {
		pr_err("master bit set in root but no blocks: %d", master);
		return 0;
	}

	for (i = 0; i < 8; i++)
		if (blockbits & (1 << i)) {
			block_number = master * 8 + i;	/* block # */
			ret |= pm8xxx_irq_block_handler(chip, block_number);
		}
	return ret;
}

static irqreturn_t pm8xxx_irq_handler(int irq, void *data)
{
	struct pm_irq_chip *chip = data;
	unsigned int root;
	int	i, ret, masters = 0;

	ret = regmap_read(chip->regmap, SSBI_REG_ADDR_IRQ_ROOT, &root);
	if (ret) {
		pr_err("Can't read root status ret=%d\n", ret);
		return IRQ_NONE;
	}

	/* on pm8xxx series masters start from bit 1 of the root */
	masters = root >> 1;

	/* Read allowed masters for blocks. */
	for (i = 0; i < chip->num_masters; i++)
		if (masters & (1 << i))
			pm8xxx_irq_master_handler(chip, i);

	return IRQ_HANDLED;
}

static void pm8821_irq_block_handler(struct pm_irq_chip *chip,
				     int master, int block)
{
	int pmirq, i, ret;
	unsigned int bits;

	ret = regmap_read(chip->regmap,
			  PM8821_SSBI_ADDR_IRQ_ROOT(master, block), &bits);
	if (ret) {
		pr_err("Reading block %d failed ret=%d", block, ret);
		return;
	}

	/* Convert block offset to global block number */
	block += (master * PM8821_BLOCKS_PER_MASTER) - 1;

	/* Check IRQ bits */
	for (i = 0; i < 8; i++) {
		if (bits & BIT(i)) {
			pmirq = block * 8 + i;
			generic_handle_domain_irq(chip->irqdomain, pmirq);
		}
	}
}

static inline void pm8821_irq_master_handler(struct pm_irq_chip *chip,
					     int master, u8 master_val)
{
	int block;

	for (block = 1; block < 8; block++)
		if (master_val & BIT(block))
			pm8821_irq_block_handler(chip, master, block);
}

static irqreturn_t pm8821_irq_handler(int irq, void *data)
{
	struct pm_irq_chip *chip = data;
	unsigned int master;
	int ret;

	ret = regmap_read(chip->regmap,
			  PM8821_SSBI_REG_ADDR_IRQ_MASTER0, &master);
	if (ret) {
		pr_err("Failed to read master 0 ret=%d\n", ret);
		return IRQ_NONE;
	}

	/* bits 1 through 7 marks the first 7 blocks in master 0 */
	if (master & GENMASK(7, 1))
		pm8821_irq_master_handler(chip, 0, master);

	/* bit 0 marks if master 1 contains any bits */
	if (!(master & BIT(0)))
		return IRQ_NONE;

	ret = regmap_read(chip->regmap,
			  PM8821_SSBI_REG_ADDR_IRQ_MASTER1, &master);
	if (ret) {
		pr_err("Failed to read master 1 ret=%d\n", ret);
		return IRQ_NONE;
	}

	pm8821_irq_master_handler(chip, 1, master);

	return IRQ_HANDLED;
}

static void pm8xxx_irq_mask_ack(struct irq_data *d)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int pmirq = irqd_to_hwirq(d);
	u8	block, config;

	block = pmirq / 8;

	config = chip->config[pmirq] | PM_IRQF_MASK_ALL | PM_IRQF_CLR;
	pm8xxx_config_irq(chip, block, config);
}

static void pm8xxx_irq_unmask(struct irq_data *d)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int pmirq = irqd_to_hwirq(d);
	u8	block, config;

	block = pmirq / 8;

	config = chip->config[pmirq];
	pm8xxx_config_irq(chip, block, config);
}

static int pm8xxx_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int pmirq = irqd_to_hwirq(d);
	int irq_bit;
	u8 block, config;

	block = pmirq / 8;
	irq_bit  = pmirq % 8;

	chip->config[pmirq] = (irq_bit << PM_IRQF_BITS_SHIFT)
							| PM_IRQF_MASK_ALL;
	if (flow_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		if (flow_type & IRQF_TRIGGER_RISING)
			chip->config[pmirq] &= ~PM_IRQF_MASK_RE;
		if (flow_type & IRQF_TRIGGER_FALLING)
			chip->config[pmirq] &= ~PM_IRQF_MASK_FE;
	} else {
		chip->config[pmirq] |= PM_IRQF_LVL_SEL;

		if (flow_type & IRQF_TRIGGER_HIGH)
			chip->config[pmirq] &= ~PM_IRQF_MASK_RE;
		else
			chip->config[pmirq] &= ~PM_IRQF_MASK_FE;
	}

	config = chip->config[pmirq] | PM_IRQF_CLR;
	return pm8xxx_config_irq(chip, block, config);
}

static int pm8xxx_irq_get_irqchip_state(struct irq_data *d,
					enum irqchip_irq_state which,
					bool *state)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int pmirq = irqd_to_hwirq(d);
	unsigned int bits;
	unsigned long flags;
	int irq_bit;
	u8 block;
	int rc;

	if (which != IRQCHIP_STATE_LINE_LEVEL)
		return -EINVAL;

	block = pmirq / 8;
	irq_bit = pmirq % 8;

	spin_lock_irqsave(&chip->pm_irq_lock, flags);
	rc = regmap_write(chip->regmap, SSBI_REG_ADDR_IRQ_BLK_SEL, block);
	if (rc) {
		pr_err("Failed Selecting Block %d rc=%d\n", block, rc);
		goto bail;
	}

	rc = regmap_read(chip->regmap, SSBI_REG_ADDR_IRQ_RT_STATUS, &bits);
	if (rc) {
		pr_err("Failed Reading Status rc=%d\n", rc);
		goto bail;
	}

	*state = !!(bits & BIT(irq_bit));
bail:
	spin_unlock_irqrestore(&chip->pm_irq_lock, flags);

	return rc;
}

static struct irq_chip pm8xxx_irq_chip = {
	.name		= "pm8xxx",
	.irq_mask_ack	= pm8xxx_irq_mask_ack,
	.irq_unmask	= pm8xxx_irq_unmask,
	.irq_set_type	= pm8xxx_irq_set_type,
	.irq_get_irqchip_state = pm8xxx_irq_get_irqchip_state,
	.flags		= IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE,
};

static void pm8xxx_irq_domain_map(struct pm_irq_chip *chip,
				  struct irq_domain *domain, unsigned int irq,
				  irq_hw_number_t hwirq, unsigned int type)
{
	irq_domain_set_info(domain, irq, hwirq, chip->pm_irq_data->irq_chip,
			    chip, handle_level_irq, NULL, NULL);
	irq_set_noprobe(irq);
}

static int pm8xxx_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *data)
{
	struct pm_irq_chip *chip = domain->host_data;
	struct irq_fwspec *fwspec = data;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret, i;

	ret = irq_domain_translate_twocell(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++)
		pm8xxx_irq_domain_map(chip, domain, virq + i, hwirq + i, type);

	return 0;
}

static const struct irq_domain_ops pm8xxx_irq_domain_ops = {
	.alloc = pm8xxx_irq_domain_alloc,
	.free = irq_domain_free_irqs_common,
	.translate = irq_domain_translate_twocell,
};

static void pm8821_irq_mask_ack(struct irq_data *d)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int pmirq = irqd_to_hwirq(d);
	u8 block, master;
	int irq_bit, rc;

	block = pmirq / 8;
	master = block / PM8821_BLOCKS_PER_MASTER;
	irq_bit = pmirq % 8;
	block %= PM8821_BLOCKS_PER_MASTER;

	rc = regmap_update_bits(chip->regmap,
				PM8821_SSBI_ADDR_IRQ_MASK(master, block),
				BIT(irq_bit), BIT(irq_bit));
	if (rc) {
		pr_err("Failed to mask IRQ:%d rc=%d\n", pmirq, rc);
		return;
	}

	rc = regmap_update_bits(chip->regmap,
				PM8821_SSBI_ADDR_IRQ_CLEAR(master, block),
				BIT(irq_bit), BIT(irq_bit));
	if (rc)
		pr_err("Failed to CLEAR IRQ:%d rc=%d\n", pmirq, rc);
}

static void pm8821_irq_unmask(struct irq_data *d)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int pmirq = irqd_to_hwirq(d);
	int irq_bit, rc;
	u8 block, master;

	block = pmirq / 8;
	master = block / PM8821_BLOCKS_PER_MASTER;
	irq_bit = pmirq % 8;
	block %= PM8821_BLOCKS_PER_MASTER;

	rc = regmap_update_bits(chip->regmap,
				PM8821_SSBI_ADDR_IRQ_MASK(master, block),
				BIT(irq_bit), ~BIT(irq_bit));
	if (rc)
		pr_err("Failed to read/write unmask IRQ:%d rc=%d\n", pmirq, rc);

}

static int pm8821_irq_get_irqchip_state(struct irq_data *d,
					enum irqchip_irq_state which,
					bool *state)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	int rc, pmirq = irqd_to_hwirq(d);
	u8 block, irq_bit, master;
	unsigned int bits;

	block = pmirq / 8;
	master = block / PM8821_BLOCKS_PER_MASTER;
	irq_bit = pmirq % 8;
	block %= PM8821_BLOCKS_PER_MASTER;

	rc = regmap_read(chip->regmap,
		PM8821_SSBI_ADDR_IRQ_RT_STATUS(master, block), &bits);
	if (rc) {
		pr_err("Reading Status of IRQ %d failed rc=%d\n", pmirq, rc);
		return rc;
	}

	*state = !!(bits & BIT(irq_bit));

	return rc;
}

static struct irq_chip pm8821_irq_chip = {
	.name		= "pm8821",
	.irq_mask_ack	= pm8821_irq_mask_ack,
	.irq_unmask	= pm8821_irq_unmask,
	.irq_get_irqchip_state = pm8821_irq_get_irqchip_state,
	.flags		= IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE,
};

static const struct regmap_config ssbi_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x3ff,
	.fast_io = true,
	.reg_read = ssbi_reg_read,
	.reg_write = ssbi_reg_write
};

static const struct pm_irq_data pm8xxx_data = {
	.num_irqs = PM8XXX_NR_IRQS,
	.irq_chip = &pm8xxx_irq_chip,
	.irq_handler = pm8xxx_irq_handler,
};

static const struct pm_irq_data pm8821_data = {
	.num_irqs = PM8821_NR_IRQS,
	.irq_chip = &pm8821_irq_chip,
	.irq_handler = pm8821_irq_handler,
};

static const struct of_device_id pm8xxx_id_table[] = {
	{ .compatible = "qcom,pm8058", .data = &pm8xxx_data},
	{ .compatible = "qcom,pm8821", .data = &pm8821_data},
	{ .compatible = "qcom,pm8921", .data = &pm8xxx_data},
	{ }
};
MODULE_DEVICE_TABLE(of, pm8xxx_id_table);

static int pm8xxx_probe(struct platform_device *pdev)
{
	const struct pm_irq_data *data;
	struct regmap *regmap;
	int irq, rc;
	unsigned int val;
	struct pm_irq_chip *chip;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "No matching driver data found\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	regmap = devm_regmap_init(&pdev->dev, NULL, pdev->dev.parent,
				  &ssbi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Read PMIC chip revision */
	rc = regmap_read(regmap, REG_HWREV, &val);
	if (rc) {
		pr_err("Failed to read hw rev reg %d:rc=%d\n", REG_HWREV, rc);
		return rc;
	}
	pr_info("PMIC revision 1: %02X\n", val);

	/* Read PMIC chip revision 2 */
	rc = regmap_read(regmap, REG_HWREV_2, &val);
	if (rc) {
		pr_err("Failed to read hw rev 2 reg %d:rc=%d\n",
			REG_HWREV_2, rc);
		return rc;
	}
	pr_info("PMIC revision 2: %02X\n", val);

	chip = devm_kzalloc(&pdev->dev,
			    struct_size(chip, config, data->num_irqs),
			    GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	platform_set_drvdata(pdev, chip);
	chip->regmap = regmap;
	chip->num_blocks = DIV_ROUND_UP(data->num_irqs, 8);
	chip->num_masters = DIV_ROUND_UP(chip->num_blocks, 8);
	chip->pm_irq_data = data;
	spin_lock_init(&chip->pm_irq_lock);

	chip->irqdomain = irq_domain_add_linear(pdev->dev.of_node,
						data->num_irqs,
						&pm8xxx_irq_domain_ops,
						chip);
	if (!chip->irqdomain)
		return -ENODEV;

	rc = devm_request_irq(&pdev->dev, irq, data->irq_handler, 0, dev_name(&pdev->dev), chip);
	if (rc)
		return rc;

	irq_set_irq_wake(irq, 1);

	rc = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (rc)
		irq_domain_remove(chip->irqdomain);

	return rc;
}

static int pm8xxx_remove_child(struct device *dev, void *unused)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int pm8xxx_remove(struct platform_device *pdev)
{
	struct pm_irq_chip *chip = platform_get_drvdata(pdev);

	device_for_each_child(&pdev->dev, NULL, pm8xxx_remove_child);
	irq_domain_remove(chip->irqdomain);

	return 0;
}

static struct platform_driver pm8xxx_driver = {
	.probe		= pm8xxx_probe,
	.remove		= pm8xxx_remove,
	.driver		= {
		.name	= "pm8xxx-core",
		.of_match_table = pm8xxx_id_table,
	},
};

static int __init pm8xxx_init(void)
{
	return platform_driver_register(&pm8xxx_driver);
}
subsys_initcall(pm8xxx_init);

static void __exit pm8xxx_exit(void)
{
	platform_driver_unregister(&pm8xxx_driver);
}
module_exit(pm8xxx_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PMIC 8xxx core driver");
MODULE_VERSION("1.0");
MODULE_ALIAS("platform:pm8xxx-core");
