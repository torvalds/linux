// SPDX-License-Identifier: GPL-2.0
/*
 * MFD core driver for Intel Broxton Whiskey Cove PMIC
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/mfd/intel_soc_pmic_bxtwc.h>
#include <linux/module.h>

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
#define BXTWC_TMUIRQ		0x4FB6

/* Interrupt MASK Registers */
#define BXTWC_MIRQLVL1		0x4E0E
#define BXTWC_MIRQLVL1_MCHGR	BIT(5)

#define BXTWC_MPWRBTNIRQ	0x4E0F
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
#define BXTWC_MTMUIRQ		0x4FB7

/* Whiskey Cove PMIC share same ACPI ID between different platforms */
#define BROXTON_PMIC_WC_HRV	4

enum bxtwc_irqs {
	BXTWC_PWRBTN_LVL1_IRQ = 0,
	BXTWC_TMU_LVL1_IRQ,
	BXTWC_THRM_LVL1_IRQ,
	BXTWC_BCU_LVL1_IRQ,
	BXTWC_ADC_LVL1_IRQ,
	BXTWC_CHGR_LVL1_IRQ,
	BXTWC_GPIO_LVL1_IRQ,
	BXTWC_CRIT_LVL1_IRQ,
};

enum bxtwc_irqs_pwrbtn {
	BXTWC_PWRBTN_IRQ = 0,
	BXTWC_UIBTN_IRQ,
};

enum bxtwc_irqs_bcu {
	BXTWC_BCU_IRQ = 0,
};

enum bxtwc_irqs_adc {
	BXTWC_ADC_IRQ = 0,
};

enum bxtwc_irqs_chgr {
	BXTWC_USBC_IRQ = 0,
	BXTWC_CHGR0_IRQ,
	BXTWC_CHGR1_IRQ,
};

enum bxtwc_irqs_tmu {
	BXTWC_TMU_IRQ = 0,
};

enum bxtwc_irqs_crit {
	BXTWC_CRIT_IRQ = 0,
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
};

static const struct regmap_irq bxtwc_regmap_irqs_pwrbtn[] = {
	REGMAP_IRQ_REG(BXTWC_PWRBTN_IRQ, 0, 0x01),
};

static const struct regmap_irq bxtwc_regmap_irqs_bcu[] = {
	REGMAP_IRQ_REG(BXTWC_BCU_IRQ, 0, 0x1f),
};

static const struct regmap_irq bxtwc_regmap_irqs_adc[] = {
	REGMAP_IRQ_REG(BXTWC_ADC_IRQ, 0, 0xff),
};

static const struct regmap_irq bxtwc_regmap_irqs_chgr[] = {
	REGMAP_IRQ_REG(BXTWC_USBC_IRQ, 0, 0x20),
	REGMAP_IRQ_REG(BXTWC_CHGR0_IRQ, 0, 0x1f),
	REGMAP_IRQ_REG(BXTWC_CHGR1_IRQ, 1, 0x1f),
};

static const struct regmap_irq bxtwc_regmap_irqs_tmu[] = {
	REGMAP_IRQ_REG(BXTWC_TMU_IRQ, 0, 0x06),
};

static const struct regmap_irq bxtwc_regmap_irqs_crit[] = {
	REGMAP_IRQ_REG(BXTWC_CRIT_IRQ, 0, 0x03),
};

static struct regmap_irq_chip bxtwc_regmap_irq_chip = {
	.name = "bxtwc_irq_chip",
	.status_base = BXTWC_IRQLVL1,
	.mask_base = BXTWC_MIRQLVL1,
	.irqs = bxtwc_regmap_irqs,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs),
	.num_regs = 1,
};

static struct regmap_irq_chip bxtwc_regmap_irq_chip_pwrbtn = {
	.name = "bxtwc_irq_chip_pwrbtn",
	.status_base = BXTWC_PWRBTNIRQ,
	.mask_base = BXTWC_MPWRBTNIRQ,
	.irqs = bxtwc_regmap_irqs_pwrbtn,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_pwrbtn),
	.num_regs = 1,
};

static struct regmap_irq_chip bxtwc_regmap_irq_chip_tmu = {
	.name = "bxtwc_irq_chip_tmu",
	.status_base = BXTWC_TMUIRQ,
	.mask_base = BXTWC_MTMUIRQ,
	.irqs = bxtwc_regmap_irqs_tmu,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_tmu),
	.num_regs = 1,
};

static struct regmap_irq_chip bxtwc_regmap_irq_chip_bcu = {
	.name = "bxtwc_irq_chip_bcu",
	.status_base = BXTWC_BCUIRQ,
	.mask_base = BXTWC_MBCUIRQ,
	.irqs = bxtwc_regmap_irqs_bcu,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_bcu),
	.num_regs = 1,
};

static struct regmap_irq_chip bxtwc_regmap_irq_chip_adc = {
	.name = "bxtwc_irq_chip_adc",
	.status_base = BXTWC_ADCIRQ,
	.mask_base = BXTWC_MADCIRQ,
	.irqs = bxtwc_regmap_irqs_adc,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_adc),
	.num_regs = 1,
};

static struct regmap_irq_chip bxtwc_regmap_irq_chip_chgr = {
	.name = "bxtwc_irq_chip_chgr",
	.status_base = BXTWC_CHGR0IRQ,
	.mask_base = BXTWC_MCHGR0IRQ,
	.irqs = bxtwc_regmap_irqs_chgr,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_chgr),
	.num_regs = 2,
};

static struct regmap_irq_chip bxtwc_regmap_irq_chip_crit = {
	.name = "bxtwc_irq_chip_crit",
	.status_base = BXTWC_CRITIRQ,
	.mask_base = BXTWC_MCRITIRQ,
	.irqs = bxtwc_regmap_irqs_crit,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_crit),
	.num_regs = 1,
};

static struct resource gpio_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_GPIO_LVL1_IRQ, "GPIO"),
};

static struct resource adc_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_ADC_IRQ, "ADC"),
};

static struct resource usbc_resources[] = {
	DEFINE_RES_IRQ(BXTWC_USBC_IRQ),
};

static struct resource charger_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_CHGR0_IRQ, "CHARGER"),
	DEFINE_RES_IRQ_NAMED(BXTWC_CHGR1_IRQ, "CHARGER1"),
};

static struct resource thermal_resources[] = {
	DEFINE_RES_IRQ(BXTWC_THRM_LVL1_IRQ),
};

static struct resource bcu_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_BCU_IRQ, "BCU"),
};

static struct resource tmu_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_TMU_IRQ, "TMU"),
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
		.name = "bxt_wcove_tmu",
		.num_resources = ARRAY_SIZE(tmu_resources),
		.resources = tmu_resources,
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

	if (!pmic)
		return -EINVAL;

	if (reg & REG_ADDR_MASK)
		i2c_addr = (reg & REG_ADDR_MASK) >> REG_ADDR_SHIFT;
	else
		i2c_addr = BXTWC_DEVICE1_ADDR;

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

	if (!pmic)
		return -EINVAL;

	if (reg & REG_ADDR_MASK)
		i2c_addr = (reg & REG_ADDR_MASK) >> REG_ADDR_SHIFT;
	else
		i2c_addr = BXTWC_DEVICE1_ADDR;

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

static int bxtwc_add_chained_irq_chip(struct intel_soc_pmic *pmic,
				struct regmap_irq_chip_data *pdata,
				int pirq, int irq_flags,
				const struct regmap_irq_chip *chip,
				struct regmap_irq_chip_data **data)
{
	int irq;

	irq = regmap_irq_get_virq(pdata, pirq);
	if (irq < 0) {
		dev_err(pmic->dev,
			"Failed to get parent vIRQ(%d) for chip %s, ret:%d\n",
			pirq, chip->name, irq);
		return irq;
	}

	return devm_regmap_add_irq_chip(pmic->dev, pmic->regmap, irq, irq_flags,
					0, chip, data);
}

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

	ret = devm_regmap_add_irq_chip(&pdev->dev, pmic->regmap, pmic->irq,
				       IRQF_ONESHOT | IRQF_SHARED,
				       0, &bxtwc_regmap_irq_chip,
				       &pmic->irq_chip_data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add IRQ chip\n");
		return ret;
	}

	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_PWRBTN_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_pwrbtn,
					 &pmic->irq_chip_data_pwrbtn);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add PWRBTN IRQ chip\n");
		return ret;
	}

	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_TMU_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_tmu,
					 &pmic->irq_chip_data_tmu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add TMU IRQ chip\n");
		return ret;
	}

	/* Add chained IRQ handler for BCU IRQs */
	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_BCU_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_bcu,
					 &pmic->irq_chip_data_bcu);


	if (ret) {
		dev_err(&pdev->dev, "Failed to add BUC IRQ chip\n");
		return ret;
	}

	/* Add chained IRQ handler for ADC IRQs */
	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_ADC_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_adc,
					 &pmic->irq_chip_data_adc);


	if (ret) {
		dev_err(&pdev->dev, "Failed to add ADC IRQ chip\n");
		return ret;
	}

	/* Add chained IRQ handler for CHGR IRQs */
	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_CHGR_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_chgr,
					 &pmic->irq_chip_data_chgr);


	if (ret) {
		dev_err(&pdev->dev, "Failed to add CHGR IRQ chip\n");
		return ret;
	}

	/* Add chained IRQ handler for CRIT IRQs */
	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_CRIT_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_crit,
					 &pmic->irq_chip_data_crit);


	if (ret) {
		dev_err(&pdev->dev, "Failed to add CRIT IRQ chip\n");
		return ret;
	}

	ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE, bxt_wc_dev,
				   ARRAY_SIZE(bxt_wc_dev), NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add devices\n");
		return ret;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &bxtwc_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs group %d\n", ret);
		return ret;
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
}

static int bxtwc_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &bxtwc_group);

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
MODULE_DEVICE_TABLE(acpi, bxtwc_acpi_ids);

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
