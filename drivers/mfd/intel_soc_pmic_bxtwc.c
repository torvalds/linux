/*
 * MFD core driver for Intel Broxton Whiskey Cove PMIC
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel_bxtwc.h>
#include <asm/intel_pmc_ipc.h>

/* PMIC device registers */
#define REG_ADDR_MASK		0xFF00
#define REG_ADDR_SHIFT		8
#define REG_OFFSET_MASK		0xFF

/* Interrupt Status Registers */
#define BXTWC_IRQLVL1		0x4E02
#define BXTWC_PWRBTNIRQ		0x4E03

#define BXTWC_THRM0IRQ		0x4E04
#define BXTWC_THRM1IRQ		0x4E05
#define BXTWC_THRM2IRQ		0x4E06
#define BXTWC_BCUIRQ		0x4E07
#define BXTWC_ADCIRQ		0x4E08
#define BXTWC_CHGR0IRQ		0x4E09
#define BXTWC_CHGR1IRQ		0x4E0A
#define BXTWC_GPIOIRQ0		0x4E0B
#define BXTWC_GPIOIRQ1		0x4E0C
#define BXTWC_CRITIRQ		0x4E0D

/* Interrupt MASK Registers */
#define BXTWC_MIRQLVL1		0x4E0E
#define BXTWC_MPWRTNIRQ		0x4E0F

#define BXTWC_MIRQLVL1_MCHGR	BIT(5)

#define BXTWC_MTHRM0IRQ		0x4E12
#define BXTWC_MTHRM1IRQ		0x4E13
#define BXTWC_MTHRM2IRQ		0x4E14
#define BXTWC_MBCUIRQ		0x4E15
#define BXTWC_MADCIRQ		0x4E16
#define BXTWC_MCHGR0IRQ		0x4E17
#define BXTWC_MCHGR1IRQ		0x4E18
#define BXTWC_MGPIO0IRQ		0x4E19
#define BXTWC_MGPIO1IRQ		0x4E1A
#define BXTWC_MCRITIRQ		0x4E1B

/* Whiskey Cove PMIC share same ACPI ID between different platforms */
#define BROXTON_PMIC_WC_HRV	4

/* Manage in two IRQ chips since mask registers are not consecutive */
enum bxtwc_irqs {
	/* Level 1 */
	BXTWC_PWRBTN_LVL1_IRQ = 0,
	BXTWC_TMU_LVL1_IRQ,
	BXTWC_THRM_LVL1_IRQ,
	BXTWC_BCU_LVL1_IRQ,
	BXTWC_ADC_LVL1_IRQ,
	BXTWC_CHGR_LVL1_IRQ,
	BXTWC_GPIO_LVL1_IRQ,
	BXTWC_CRIT_LVL1_IRQ,

	/* Level 2 */
	BXTWC_PWRBTN_IRQ,
};

enum bxtwc_irqs_level2 {
	/* Level 2 */
	BXTWC_THRM0_IRQ = 0,
	BXTWC_THRM1_IRQ,
	BXTWC_THRM2_IRQ,
	BXTWC_BCU_IRQ,
	BXTWC_ADC_IRQ,
	BXTWC_CHGR0_IRQ,
	BXTWC_CHGR1_IRQ,
	BXTWC_GPIO0_IRQ,
	BXTWC_GPIO1_IRQ,
	BXTWC_CRIT_IRQ,
};

static const struct regmap_irq bxtwc_regmap_irqs[] = {
	REGMAP_IRQ_REG(BXTWC_PWRBTN_LVL1_IRQ, 0, BIT(0)),
	REGMAP_IRQ_REG(BXTWC_TMU_LVL1_IRQ, 0, BIT(1)),
	REGMAP_IRQ_REG(BXTWC_THRM_LVL1_IRQ, 0, BIT(2)),
	REGMAP_IRQ_REG(BXTWC_BCU_LVL1_IRQ, 0, BIT(3)),
	REGMAP_IRQ_REG(BXTWC_ADC_LVL1_IRQ, 0, BIT(4)),
	REGMAP_IRQ_REG(BXTWC_CHGR_LVL1_IRQ, 0, BIT(5)),
	REGMAP_IRQ_REG(BXTWC_GPIO_LVL1_IRQ, 0, BIT(6)),
	REGMAP_IRQ_REG(BXTWC_CRIT_LVL1_IRQ, 0, BIT(7)),
	REGMAP_IRQ_REG(BXTWC_PWRBTN_IRQ, 1, 0x03),
};

static const struct regmap_irq bxtwc_regmap_irqs_level2[] = {
	REGMAP_IRQ_REG(BXTWC_THRM0_IRQ, 0, 0xff),
	REGMAP_IRQ_REG(BXTWC_THRM1_IRQ, 1, 0xbf),
	REGMAP_IRQ_REG(BXTWC_THRM2_IRQ, 2, 0xff),
	REGMAP_IRQ_REG(BXTWC_BCU_IRQ, 3, 0x1f),
	REGMAP_IRQ_REG(BXTWC_ADC_IRQ, 4, 0xff),
	REGMAP_IRQ_REG(BXTWC_CHGR0_IRQ, 5, 0x3f),
	REGMAP_IRQ_REG(BXTWC_CHGR1_IRQ, 6, 0x1f),
	REGMAP_IRQ_REG(BXTWC_GPIO0_IRQ, 7, 0xff),
	REGMAP_IRQ_REG(BXTWC_GPIO1_IRQ, 8, 0x3f),
	REGMAP_IRQ_REG(BXTWC_CRIT_IRQ, 9, 0x03),
};

static struct regmap_irq_chip bxtwc_regmap_irq_chip = {
	.name = "bxtwc_irq_chip",
	.status_base = BXTWC_IRQLVL1,
	.mask_base = BXTWC_MIRQLVL1,
	.irqs = bxtwc_regmap_irqs,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs),
	.num_regs = 2,
};

static struct regmap_irq_chip bxtwc_regmap_irq_chip_level2 = {
	.name = "bxtwc_irq_chip_level2",
	.status_base = BXTWC_THRM0IRQ,
	.mask_base = BXTWC_MTHRM0IRQ,
	.irqs = bxtwc_regmap_irqs_level2,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_level2),
	.num_regs = 10,
};

static struct resource gpio_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_GPIO0_IRQ, "GPIO0"),
	DEFINE_RES_IRQ_NAMED(BXTWC_GPIO1_IRQ, "GPIO1"),
};

static struct resource adc_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_ADC_IRQ, "ADC"),
};

static struct resource usbc_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_CHGR0_IRQ, "USBC"),
};

static struct resource charger_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_CHGR0_IRQ, "CHARGER"),
	DEFINE_RES_IRQ_NAMED(BXTWC_CHGR1_IRQ, "CHARGER1"),
};

static struct resource thermal_resources[] = {
	DEFINE_RES_IRQ(BXTWC_THRM0_IRQ),
	DEFINE_RES_IRQ(BXTWC_THRM1_IRQ),
	DEFINE_RES_IRQ(BXTWC_THRM2_IRQ),
};

static struct resource bcu_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_BCU_IRQ, "BCU"),
};

static struct mfd_cell bxt_wc_dev[] = {
	{
		.name = "bxt_wcove_gpadc",
		.num_resources = ARRAY_SIZE(adc_resources),
		.resources = adc_resources,
	},
	{
		.name = "bxt_wcove_thermal",
		.num_resources = ARRAY_SIZE(thermal_resources),
		.resources = thermal_resources,
	},
	{
		.name = "bxt_wcove_usbc",
		.num_resources = ARRAY_SIZE(usbc_resources),
		.resources = usbc_resources,
	},
	{
		.name = "bxt_wcove_ext_charger",
		.num_resources = ARRAY_SIZE(charger_resources),
		.resources = charger_resources,
	},
	{
		.name = "bxt_wcove_bcu",
		.num_resources = ARRAY_SIZE(bcu_resources),
		.resources = bcu_resources,
	},
	{
		.name = "bxt_wcove_gpio",
		.num_resources = ARRAY_SIZE(gpio_resources),
		.resources = gpio_resources,
	},
	{
		.name = "bxt_wcove_region",
	},
};

static int regmap_ipc_byte_reg_read(void *context, unsigned int reg,
				    unsigned int *val)
{
	int ret;
	int i2c_addr;
	u8 ipc_in[2];
	u8 ipc_out[4];
	struct intel_soc_pmic *pmic = context;

	if (reg & REG_ADDR_MASK)
		i2c_addr = (reg & REG_ADDR_MASK) >> REG_ADDR_SHIFT;
	else {
		i2c_addr = BXTWC_DEVICE1_ADDR;
		if (!i2c_addr) {
			dev_err(pmic->dev, "I2C address not set\n");
			return -EINVAL;
		}
	}
	reg &= REG_OFFSET_MASK;

	ipc_in[0] = reg;
	ipc_in[1] = i2c_addr;
	ret = intel_pmc_ipc_command(PMC_IPC_PMIC_ACCESS,
			PMC_IPC_PMIC_ACCESS_READ,
			ipc_in, sizeof(ipc_in), (u32 *)ipc_out, 1);
	if (ret) {
		dev_err(pmic->dev, "Failed to read from PMIC\n");
		return ret;
	}
	*val = ipc_out[0];

	return 0;
}

static int regmap_ipc_byte_reg_write(void *context, unsigned int reg,
				       unsigned int val)
{
	int ret;
	int i2c_addr;
	u8 ipc_in[3];
	struct intel_soc_pmic *pmic = context;

	if (reg & REG_ADDR_MASK)
		i2c_addr = (reg & REG_ADDR_MASK) >> REG_ADDR_SHIFT;
	else {
		i2c_addr = BXTWC_DEVICE1_ADDR;
		if (!i2c_addr) {
			dev_err(pmic->dev, "I2C address not set\n");
			return -EINVAL;
		}
	}
	reg &= REG_OFFSET_MASK;

	ipc_in[0] = reg;
	ipc_in[1] = i2c_addr;
	ipc_in[2] = val;
	ret = intel_pmc_ipc_command(PMC_IPC_PMIC_ACCESS,
			PMC_IPC_PMIC_ACCESS_WRITE,
			ipc_in, sizeof(ipc_in), NULL, 0);
	if (ret) {
		dev_err(pmic->dev, "Failed to write to PMIC\n");
		return ret;
	}

	return 0;
}

/* sysfs interfaces to r/w PMIC registers, required by initial script */
static unsigned long bxtwc_reg_addr;
static ssize_t bxtwc_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%lx\n", bxtwc_reg_addr);
}

static ssize_t bxtwc_reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	if (kstrtoul(buf, 0, &bxtwc_reg_addr)) {
		dev_err(dev, "Invalid register address\n");
		return -EINVAL;
	}
	return (ssize_t)count;
}

static ssize_t bxtwc_val_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned int val;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	ret = regmap_read(pmic->regmap, bxtwc_reg_addr, &val);
	if (ret < 0) {
		dev_err(dev, "Failed to read 0x%lx\n", bxtwc_reg_addr);
		return -EIO;
	}

	return sprintf(buf, "0x%02x\n", val);
}

static ssize_t bxtwc_val_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int val;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	ret = regmap_write(pmic->regmap, bxtwc_reg_addr, val);
	if (ret) {
		dev_err(dev, "Failed to write value 0x%02x to address 0x%lx",
			val, bxtwc_reg_addr);
		return -EIO;
	}
	return count;
}

static DEVICE_ATTR(addr, S_IWUSR | S_IRUSR, bxtwc_reg_show, bxtwc_reg_store);
static DEVICE_ATTR(val, S_IWUSR | S_IRUSR, bxtwc_val_show, bxtwc_val_store);
static struct attribute *bxtwc_attrs[] = {
	&dev_attr_addr.attr,
	&dev_attr_val.attr,
	NULL
};

static const struct attribute_group bxtwc_group = {
	.attrs = bxtwc_attrs,
};

static const struct regmap_config bxtwc_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_write = regmap_ipc_byte_reg_write,
	.reg_read = regmap_ipc_byte_reg_read,
};

static int bxtwc_probe(struct platform_device *pdev)
{
	int ret;
	acpi_handle handle;
	acpi_status status;
	unsigned long long hrv;
	struct intel_soc_pmic *pmic;

	handle = ACPI_HANDLE(&pdev->dev);
	status = acpi_evaluate_integer(handle, "_HRV", NULL, &hrv);
	if (ACPI_FAILURE(status)) {
		dev_err(&pdev->dev, "Failed to get PMIC hardware revision\n");
		return -ENODEV;
	}
	if (hrv != BROXTON_PMIC_WC_HRV) {
		dev_err(&pdev->dev, "Invalid PMIC hardware revision: %llu\n",
			hrv);
		return -ENODEV;
	}

	pmic = devm_kzalloc(&pdev->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Invalid IRQ\n");
		return ret;
	}
	pmic->irq = ret;

	dev_set_drvdata(&pdev->dev, pmic);
	pmic->dev = &pdev->dev;

	pmic->regmap = devm_regmap_init(&pdev->dev, NULL, pmic,
					&bxtwc_regmap_config);
	if (IS_ERR(pmic->regmap)) {
		ret = PTR_ERR(pmic->regmap);
		dev_err(&pdev->dev, "Failed to initialise regmap: %d\n", ret);
		return ret;
	}

	ret = regmap_add_irq_chip(pmic->regmap, pmic->irq,
				  IRQF_ONESHOT | IRQF_SHARED,
				  0, &bxtwc_regmap_irq_chip,
				  &pmic->irq_chip_data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add IRQ chip\n");
		return ret;
	}

	ret = regmap_add_irq_chip(pmic->regmap, pmic->irq,
				  IRQF_ONESHOT | IRQF_SHARED,
				  0, &bxtwc_regmap_irq_chip_level2,
				  &pmic->irq_chip_data_level2);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add secondary IRQ chip\n");
		goto err_irq_chip_level2;
	}

	ret = mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE, bxt_wc_dev,
			      ARRAY_SIZE(bxt_wc_dev), NULL, 0,
			      NULL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add devices\n");
		goto err_mfd;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &bxtwc_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs group %d\n", ret);
		goto err_sysfs;
	}

	/*
	 * There is known hw bug. Upon reset BIT 5 of register
	 * BXTWC_CHGR_LVL1_IRQ is 0 which is the expected value. However,
	 * later it's set to 1(masked) automatically by hardware. So we
	 * have the software workaround here to unmaksed it in order to let
	 * charger interrutp work.
	 */
	regmap_update_bits(pmic->regmap, BXTWC_MIRQLVL1,
				BXTWC_MIRQLVL1_MCHGR, 0);

	return 0;

err_sysfs:
	mfd_remove_devices(&pdev->dev);
err_mfd:
	regmap_del_irq_chip(pmic->irq, pmic->irq_chip_data_level2);
err_irq_chip_level2:
	regmap_del_irq_chip(pmic->irq, pmic->irq_chip_data);

	return ret;
}

static int bxtwc_remove(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(&pdev->dev);

	sysfs_remove_group(&pdev->dev.kobj, &bxtwc_group);
	mfd_remove_devices(&pdev->dev);
	regmap_del_irq_chip(pmic->irq, pmic->irq_chip_data);
	regmap_del_irq_chip(pmic->irq, pmic->irq_chip_data_level2);

	return 0;
}

static void bxtwc_shutdown(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(&pdev->dev);

	disable_irq(pmic->irq);
}

#ifdef CONFIG_PM_SLEEP
static int bxtwc_suspend(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	disable_irq(pmic->irq);

	return 0;
}

static int bxtwc_resume(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	enable_irq(pmic->irq);
	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(bxtwc_pm_ops, bxtwc_suspend, bxtwc_resume);

static const struct acpi_device_id bxtwc_acpi_ids[] = {
	{ "INT34D3", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, pmic_acpi_ids);

static struct platform_driver bxtwc_driver = {
	.probe = bxtwc_probe,
	.remove	= bxtwc_remove,
	.shutdown = bxtwc_shutdown,
	.driver	= {
		.name	= "BXTWC PMIC",
		.pm     = &bxtwc_pm_ops,
		.acpi_match_table = ACPI_PTR(bxtwc_acpi_ids),
	},
};

module_platform_driver(bxtwc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Qipeng Zha<qipeng.zha@intel.com>");
