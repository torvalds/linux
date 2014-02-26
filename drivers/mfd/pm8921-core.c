/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/ssbi.h>
#include <linux/mfd/core.h>
#include <linux/mfd/pm8xxx/pm8921.h>
#include <linux/mfd/pm8xxx/core.h>
#include <linux/mfd/pm8xxx/irq.h>

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

struct pm_irq_chip {
	struct device		*dev;
	spinlock_t		pm_irq_lock;
	unsigned int		devirq;
	unsigned int		irq_base;
	unsigned int		num_irqs;
	unsigned int		num_blocks;
	unsigned int		num_masters;
	u8			config[0];
};

struct pm8921 {
	struct device			*dev;
	struct pm_irq_chip		*irq_chip;
};

static int pm8xxx_read_root_irq(const struct pm_irq_chip *chip, u8 *rp)
{
	return pm8xxx_readb(chip->dev, SSBI_REG_ADDR_IRQ_ROOT, rp);
}

static int pm8xxx_read_master_irq(const struct pm_irq_chip *chip, u8 m, u8 *bp)
{
	return pm8xxx_readb(chip->dev,
			SSBI_REG_ADDR_IRQ_M_STATUS1 + m, bp);
}

static int pm8xxx_read_block_irq(struct pm_irq_chip *chip, u8 bp, u8 *ip)
{
	int	rc;

	spin_lock(&chip->pm_irq_lock);
	rc = pm8xxx_writeb(chip->dev, SSBI_REG_ADDR_IRQ_BLK_SEL, bp);
	if (rc) {
		pr_err("Failed Selecting Block %d rc=%d\n", bp, rc);
		goto bail;
	}

	rc = pm8xxx_readb(chip->dev, SSBI_REG_ADDR_IRQ_IT_STATUS, ip);
	if (rc)
		pr_err("Failed Reading Status rc=%d\n", rc);
bail:
	spin_unlock(&chip->pm_irq_lock);
	return rc;
}

static int pm8xxx_config_irq(struct pm_irq_chip *chip, u8 bp, u8 cp)
{
	int	rc;

	spin_lock(&chip->pm_irq_lock);
	rc = pm8xxx_writeb(chip->dev, SSBI_REG_ADDR_IRQ_BLK_SEL, bp);
	if (rc) {
		pr_err("Failed Selecting Block %d rc=%d\n", bp, rc);
		goto bail;
	}

	cp |= PM_IRQF_WRITE;
	rc = pm8xxx_writeb(chip->dev, SSBI_REG_ADDR_IRQ_CONFIG, cp);
	if (rc)
		pr_err("Failed Configuring IRQ rc=%d\n", rc);
bail:
	spin_unlock(&chip->pm_irq_lock);
	return rc;
}

static int pm8xxx_irq_block_handler(struct pm_irq_chip *chip, int block)
{
	int pmirq, irq, i, ret = 0;
	u8 bits;

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
			irq = pmirq + chip->irq_base;
			generic_handle_irq(irq);
		}
	}
	return 0;
}

static int pm8xxx_irq_master_handler(struct pm_irq_chip *chip, int master)
{
	u8 blockbits;
	int block_number, i, ret = 0;

	ret = pm8xxx_read_master_irq(chip, master, &blockbits);
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

static void pm8xxx_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct pm_irq_chip *chip = irq_desc_get_handler_data(desc);
	struct irq_chip *irq_chip = irq_desc_get_chip(desc);
	u8	root;
	int	i, ret, masters = 0;

	chained_irq_enter(irq_chip, desc);

	ret = pm8xxx_read_root_irq(chip, &root);
	if (ret) {
		pr_err("Can't read root status ret=%d\n", ret);
		return;
	}

	/* on pm8xxx series masters start from bit 1 of the root */
	masters = root >> 1;

	/* Read allowed masters for blocks. */
	for (i = 0; i < chip->num_masters; i++)
		if (masters & (1 << i))
			pm8xxx_irq_master_handler(chip, i);

	chained_irq_exit(irq_chip, desc);
}

static void pm8xxx_irq_mask_ack(struct irq_data *d)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int pmirq = d->irq - chip->irq_base;
	int	master, irq_bit;
	u8	block, config;

	block = pmirq / 8;
	master = block / 8;
	irq_bit = pmirq % 8;

	config = chip->config[pmirq] | PM_IRQF_MASK_ALL | PM_IRQF_CLR;
	pm8xxx_config_irq(chip, block, config);
}

static void pm8xxx_irq_unmask(struct irq_data *d)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int pmirq = d->irq - chip->irq_base;
	int	master, irq_bit;
	u8	block, config;

	block = pmirq / 8;
	master = block / 8;
	irq_bit = pmirq % 8;

	config = chip->config[pmirq];
	pm8xxx_config_irq(chip, block, config);
}

static int pm8xxx_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct pm_irq_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int pmirq = d->irq - chip->irq_base;
	int master, irq_bit;
	u8 block, config;

	block = pmirq / 8;
	master = block / 8;
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

static int pm8xxx_irq_set_wake(struct irq_data *d, unsigned int on)
{
	return 0;
}

static struct irq_chip pm8xxx_irq_chip = {
	.name		= "pm8xxx",
	.irq_mask_ack	= pm8xxx_irq_mask_ack,
	.irq_unmask	= pm8xxx_irq_unmask,
	.irq_set_type	= pm8xxx_irq_set_type,
	.irq_set_wake	= pm8xxx_irq_set_wake,
	.flags		= IRQCHIP_MASK_ON_SUSPEND,
};

/**
 * pm8xxx_get_irq_stat - get the status of the irq line
 * @chip: pointer to identify a pmic irq controller
 * @irq: the irq number
 *
 * The pm8xxx gpio and mpp rely on the interrupt block to read
 * the values on their pins. This function is to facilitate reading
 * the status of a gpio or an mpp line. The caller has to convert the
 * gpio number to irq number.
 *
 * RETURNS:
 * an int indicating the value read on that line
 */
static int pm8xxx_get_irq_stat(struct pm_irq_chip *chip, int irq)
{
	int pmirq, rc;
	u8  block, bits, bit;
	unsigned long flags;

	if (chip == NULL || irq < chip->irq_base ||
			irq >= chip->irq_base + chip->num_irqs)
		return -EINVAL;

	pmirq = irq - chip->irq_base;

	block = pmirq / 8;
	bit = pmirq % 8;

	spin_lock_irqsave(&chip->pm_irq_lock, flags);

	rc = pm8xxx_writeb(chip->dev, SSBI_REG_ADDR_IRQ_BLK_SEL, block);
	if (rc) {
		pr_err("Failed Selecting block irq=%d pmirq=%d blk=%d rc=%d\n",
			irq, pmirq, block, rc);
		goto bail_out;
	}

	rc = pm8xxx_readb(chip->dev, SSBI_REG_ADDR_IRQ_RT_STATUS, &bits);
	if (rc) {
		pr_err("Failed Configuring irq=%d pmirq=%d blk=%d rc=%d\n",
			irq, pmirq, block, rc);
		goto bail_out;
	}

	rc = (bits & (1 << bit)) ? 1 : 0;

bail_out:
	spin_unlock_irqrestore(&chip->pm_irq_lock, flags);

	return rc;
}

static struct pm_irq_chip *pm8xxx_irq_init(struct device *dev,
				const struct pm8xxx_irq_platform_data *pdata)
{
	struct pm_irq_chip  *chip;
	int devirq, rc;
	unsigned int pmirq;

	if (!pdata) {
		pr_err("No platform data\n");
		return ERR_PTR(-EINVAL);
	}

	devirq = pdata->devirq;
	if (devirq < 0) {
		pr_err("missing devirq\n");
		rc = devirq;
		return ERR_PTR(-EINVAL);
	}

	chip = kzalloc(sizeof(struct pm_irq_chip)
			+ sizeof(u8) * pdata->irq_cdata.nirqs, GFP_KERNEL);
	if (!chip) {
		pr_err("Cannot alloc pm_irq_chip struct\n");
		return ERR_PTR(-EINVAL);
	}

	chip->dev = dev;
	chip->devirq = devirq;
	chip->irq_base = pdata->irq_base;
	chip->num_irqs = pdata->irq_cdata.nirqs;
	chip->num_blocks = DIV_ROUND_UP(chip->num_irqs, 8);
	chip->num_masters = DIV_ROUND_UP(chip->num_blocks, 8);
	spin_lock_init(&chip->pm_irq_lock);

	for (pmirq = 0; pmirq < chip->num_irqs; pmirq++) {
		irq_set_chip_and_handler(chip->irq_base + pmirq,
				&pm8xxx_irq_chip,
				handle_level_irq);
		irq_set_chip_data(chip->irq_base + pmirq, chip);
#ifdef CONFIG_ARM
		set_irq_flags(chip->irq_base + pmirq, IRQF_VALID);
#else
		irq_set_noprobe(chip->irq_base + pmirq);
#endif
	}

	irq_set_irq_type(devirq, pdata->irq_trigger_flag);
	irq_set_handler_data(devirq, chip);
	irq_set_chained_handler(devirq, pm8xxx_irq_handler);
	irq_set_irq_wake(devirq, 1);

	return chip;
}

static int pm8xxx_irq_exit(struct pm_irq_chip *chip)
{
	irq_set_chained_handler(chip->devirq, NULL);
	kfree(chip);
	return 0;
}

static int pm8921_readb(const struct device *dev, u16 addr, u8 *val)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return ssbi_read(pmic->dev->parent, addr, val, 1);
}

static int pm8921_writeb(const struct device *dev, u16 addr, u8 val)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return ssbi_write(pmic->dev->parent, addr, &val, 1);
}

static int pm8921_read_buf(const struct device *dev, u16 addr, u8 *buf,
									int cnt)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return ssbi_read(pmic->dev->parent, addr, buf, cnt);
}

static int pm8921_write_buf(const struct device *dev, u16 addr, u8 *buf,
									int cnt)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return ssbi_write(pmic->dev->parent, addr, buf, cnt);
}

static int pm8921_read_irq_stat(const struct device *dev, int irq)
{
	const struct pm8xxx_drvdata *pm8921_drvdata = dev_get_drvdata(dev);
	const struct pm8921 *pmic = pm8921_drvdata->pm_chip_data;

	return pm8xxx_get_irq_stat(pmic->irq_chip, irq);
}

static struct pm8xxx_drvdata pm8921_drvdata = {
	.pmic_readb		= pm8921_readb,
	.pmic_writeb		= pm8921_writeb,
	.pmic_read_buf		= pm8921_read_buf,
	.pmic_write_buf		= pm8921_write_buf,
	.pmic_read_irq_stat	= pm8921_read_irq_stat,
};

static int pm8921_add_subdevices(const struct pm8921_platform_data
					   *pdata,
					   struct pm8921 *pmic,
					   u32 rev)
{
	int ret = 0, irq_base = 0;
	struct pm_irq_chip *irq_chip;

	if (pdata->irq_pdata) {
		pdata->irq_pdata->irq_cdata.nirqs = PM8921_NR_IRQS;
		pdata->irq_pdata->irq_cdata.rev = rev;
		irq_base = pdata->irq_pdata->irq_base;
		irq_chip = pm8xxx_irq_init(pmic->dev, pdata->irq_pdata);

		if (IS_ERR(irq_chip)) {
			pr_err("Failed to init interrupts ret=%ld\n",
					PTR_ERR(irq_chip));
			return PTR_ERR(irq_chip);
		}
		pmic->irq_chip = irq_chip;
	}
	return ret;
}

static int pm8921_probe(struct platform_device *pdev)
{
	const struct pm8921_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct pm8921 *pmic;
	int rc;
	u8 val;
	u32 rev;

	if (!pdata) {
		pr_err("missing platform data\n");
		return -EINVAL;
	}

	pmic = devm_kzalloc(&pdev->dev, sizeof(struct pm8921), GFP_KERNEL);
	if (!pmic) {
		pr_err("Cannot alloc pm8921 struct\n");
		return -ENOMEM;
	}

	/* Read PMIC chip revision */
	rc = ssbi_read(pdev->dev.parent, REG_HWREV, &val, sizeof(val));
	if (rc) {
		pr_err("Failed to read hw rev reg %d:rc=%d\n", REG_HWREV, rc);
		return rc;
	}
	pr_info("PMIC revision 1: %02X\n", val);
	rev = val;

	/* Read PMIC chip revision 2 */
	rc = ssbi_read(pdev->dev.parent, REG_HWREV_2, &val, sizeof(val));
	if (rc) {
		pr_err("Failed to read hw rev 2 reg %d:rc=%d\n",
			REG_HWREV_2, rc);
		return rc;
	}
	pr_info("PMIC revision 2: %02X\n", val);
	rev |= val << BITS_PER_BYTE;

	pmic->dev = &pdev->dev;
	pm8921_drvdata.pm_chip_data = pmic;
	platform_set_drvdata(pdev, &pm8921_drvdata);

	rc = pm8921_add_subdevices(pdata, pmic, rev);
	if (rc) {
		pr_err("Cannot add subdevices rc=%d\n", rc);
		goto err;
	}

	/* gpio might not work if no irq device is found */
	WARN_ON(pmic->irq_chip == NULL);

	return 0;

err:
	mfd_remove_devices(pmic->dev);
	return rc;
}

static int pm8921_remove(struct platform_device *pdev)
{
	struct pm8xxx_drvdata *drvdata;
	struct pm8921 *pmic = NULL;

	drvdata = platform_get_drvdata(pdev);
	if (drvdata)
		pmic = drvdata->pm_chip_data;
	if (pmic) {
		mfd_remove_devices(pmic->dev);
		if (pmic->irq_chip) {
			pm8xxx_irq_exit(pmic->irq_chip);
			pmic->irq_chip = NULL;
		}
	}

	return 0;
}

static struct platform_driver pm8921_driver = {
	.probe		= pm8921_probe,
	.remove		= pm8921_remove,
	.driver		= {
		.name	= "pm8921-core",
		.owner	= THIS_MODULE,
	},
};

static int __init pm8921_init(void)
{
	return platform_driver_register(&pm8921_driver);
}
subsys_initcall(pm8921_init);

static void __exit pm8921_exit(void)
{
	platform_driver_unregister(&pm8921_driver);
}
module_exit(pm8921_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PMIC 8921 core driver");
MODULE_VERSION("1.0");
MODULE_ALIAS("platform:pm8921-core");
