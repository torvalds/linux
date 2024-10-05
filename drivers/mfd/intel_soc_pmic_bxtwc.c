// SPDX-License-Identifier: GPL-2.0
/*
 * MFD core driver for Intel Broxton Whiskey Cove PMIC
 *
 * Copyright (C) 2015-2017, 2022 Intel Corporation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/mfd/intel_soc_pmic_bxtwc.h>
#include <linux/module.h>
#include <linux/platform_data/x86/intel_scu_ipc.h>

/* PMIC device registers */
#define REG_ADDR_MASK		GENMASK(15, 8)
#define REG_ADDR_SHIFT		8
#define REG_OFFSET_MASK		GENMASK(7, 0)

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

#define PMC_PMIC_ACCESS		0xFF
#define PMC_PMIC_READ		0x0
#define PMC_PMIC_WRITE		0x1

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
	REGMAP_IRQ_REG(BXTWC_PWRBTN_IRQ, 0, BIT(0)),
};

static const struct regmap_irq bxtwc_regmap_irqs_bcu[] = {
	REGMAP_IRQ_REG(BXTWC_BCU_IRQ, 0, GENMASK(4, 0)),
};

static const struct regmap_irq bxtwc_regmap_irqs_adc[] = {
	REGMAP_IRQ_REG(BXTWC_ADC_IRQ, 0, GENMASK(7, 0)),
};

static const struct regmap_irq bxtwc_regmap_irqs_chgr[] = {
	REGMAP_IRQ_REG(BXTWC_USBC_IRQ, 0, BIT(5)),
	REGMAP_IRQ_REG(BXTWC_CHGR0_IRQ, 0, GENMASK(4, 0)),
	REGMAP_IRQ_REG(BXTWC_CHGR1_IRQ, 1, GENMASK(4, 0)),
};

static const struct regmap_irq bxtwc_regmap_irqs_tmu[] = {
	REGMAP_IRQ_REG(BXTWC_TMU_IRQ, 0, GENMASK(2, 1)),
};

static const struct regmap_irq bxtwc_regmap_irqs_crit[] = {
	REGMAP_IRQ_REG(BXTWC_CRIT_IRQ, 0, GENMASK(1, 0)),
};

static const struct regmap_irq_chip bxtwc_regmap_irq_chip = {
	.name = "bxtwc_irq_chip",
	.status_base = BXTWC_IRQLVL1,
	.mask_base = BXTWC_MIRQLVL1,
	.irqs = bxtwc_regmap_irqs,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs),
	.num_regs = 1,
};

static const struct regmap_irq_chip bxtwc_regmap_irq_chip_pwrbtn = {
	.name = "bxtwc_irq_chip_pwrbtn",
	.status_base = BXTWC_PWRBTNIRQ,
	.mask_base = BXTWC_MPWRBTNIRQ,
	.irqs = bxtwc_regmap_irqs_pwrbtn,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_pwrbtn),
	.num_regs = 1,
};

static const struct regmap_irq_chip bxtwc_regmap_irq_chip_tmu = {
	.name = "bxtwc_irq_chip_tmu",
	.status_base = BXTWC_TMUIRQ,
	.mask_base = BXTWC_MTMUIRQ,
	.irqs = bxtwc_regmap_irqs_tmu,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_tmu),
	.num_regs = 1,
};

static const struct regmap_irq_chip bxtwc_regmap_irq_chip_bcu = {
	.name = "bxtwc_irq_chip_bcu",
	.status_base = BXTWC_BCUIRQ,
	.mask_base = BXTWC_MBCUIRQ,
	.irqs = bxtwc_regmap_irqs_bcu,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_bcu),
	.num_regs = 1,
};

static const struct regmap_irq_chip bxtwc_regmap_irq_chip_adc = {
	.name = "bxtwc_irq_chip_adc",
	.status_base = BXTWC_ADCIRQ,
	.mask_base = BXTWC_MADCIRQ,
	.irqs = bxtwc_regmap_irqs_adc,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_adc),
	.num_regs = 1,
};

static const struct regmap_irq_chip bxtwc_regmap_irq_chip_chgr = {
	.name = "bxtwc_irq_chip_chgr",
	.status_base = BXTWC_CHGR0IRQ,
	.mask_base = BXTWC_MCHGR0IRQ,
	.irqs = bxtwc_regmap_irqs_chgr,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_chgr),
	.num_regs = 2,
};

static const struct regmap_irq_chip bxtwc_regmap_irq_chip_crit = {
	.name = "bxtwc_irq_chip_crit",
	.status_base = BXTWC_CRITIRQ,
	.mask_base = BXTWC_MCRITIRQ,
	.irqs = bxtwc_regmap_irqs_crit,
	.num_irqs = ARRAY_SIZE(bxtwc_regmap_irqs_crit),
	.num_regs = 1,
};

static const struct resource gpio_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_GPIO_LVL1_IRQ, "GPIO"),
};

static const struct resource adc_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_ADC_IRQ, "ADC"),
};

static const struct resource usbc_resources[] = {
	DEFINE_RES_IRQ(BXTWC_USBC_IRQ),
};

static const struct resource charger_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_CHGR0_IRQ, "CHARGER"),
	DEFINE_RES_IRQ_NAMED(BXTWC_CHGR1_IRQ, "CHARGER1"),
};

static const struct resource thermal_resources[] = {
	DEFINE_RES_IRQ(BXTWC_THRM_LVL1_IRQ),
};

static const struct resource bcu_resources[] = {
	DEFINE_RES_IRQ_NAMED(BXTWC_BCU_IRQ, "BCU"),
};

static const struct resource tmu_resources[] = {
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

static const struct mfd_cell bxt_wc_tmu_dev[] = {
	{
		.name = "bxt_wcove_tmu",
		.num_resources = ARRAY_SIZE(tmu_resources),
		.resources = tmu_resources,
	},
};

static struct mfd_cell bxt_wc_chgr_dev[] = {
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
	ret = intel_scu_ipc_dev_command(pmic->scu, PMC_PMIC_ACCESS,
					PMC_PMIC_READ, ipc_in, sizeof(ipc_in),
					ipc_out, sizeof(ipc_out));
	if (ret)
		return ret;

	*val = ipc_out[0];

	return 0;
}

static int regmap_ipc_byte_reg_write(void *context, unsigned int reg,
				       unsigned int val)
{
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
	return intel_scu_ipc_dev_command(pmic->scu, PMC_PMIC_ACCESS,
					 PMC_PMIC_WRITE, ipc_in, sizeof(ipc_in),
					 NULL, 0);
}

/* sysfs interfaces to r/w PMIC registers, required by initial script */
static unsigned long bxtwc_reg_addr;
static ssize_t addr_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0x%lx\n", bxtwc_reg_addr);
}

static ssize_t addr_store(struct device *dev,
			  struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;

	ret = kstrtoul(buf, 0, &bxtwc_reg_addr);
	if (ret)
		return ret;

	return count;
}

static ssize_t val_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned int val;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	ret = regmap_read(pmic->regmap, bxtwc_reg_addr, &val);
	if (ret) {
		dev_err(dev, "Failed to read 0x%lx\n", bxtwc_reg_addr);
		return ret;
	}

	return sysfs_emit(buf, "0x%02x\n", val);
}

static ssize_t val_store(struct device *dev,
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
		return ret;
	}
	return count;
}

static DEVICE_ATTR_ADMIN_RW(addr);
static DEVICE_ATTR_ADMIN_RW(val);
static struct attribute *bxtwc_attrs[] = {
	&dev_attr_addr.attr,
	&dev_attr_val.attr,
	NULL
};

static const struct attribute_group bxtwc_group = {
	.attrs = bxtwc_attrs,
};

static const struct attribute_group *bxtwc_groups[] = {
	&bxtwc_group,
	NULL
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
	if (irq < 0)
		return dev_err_probe(pmic->dev, irq, "Failed to get parent vIRQ(%d) for chip %s\n",
				     pirq, chip->name);

	return devm_regmap_add_irq_chip(pmic->dev, pmic->regmap, irq, irq_flags,
					0, chip, data);
}

static int bxtwc_add_chained_devices(struct intel_soc_pmic *pmic,
				     const struct mfd_cell *cells, int n_devs,
				     struct regmap_irq_chip_data *pdata,
				     int pirq, int irq_flags,
				     const struct regmap_irq_chip *chip,
				     struct regmap_irq_chip_data **data)
{
	struct device *dev = pmic->dev;
	struct irq_domain *domain;
	int ret;

	ret = bxtwc_add_chained_irq_chip(pmic, pdata, pirq, irq_flags, chip, data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add %s IRQ chip\n", chip->name);

	domain = regmap_irq_get_domain(*data);

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, cells, n_devs, NULL, 0, domain);
}

static int bxtwc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	acpi_status status;
	unsigned long long hrv;
	struct intel_soc_pmic *pmic;

	status = acpi_evaluate_integer(ACPI_HANDLE(dev), "_HRV", NULL, &hrv);
	if (ACPI_FAILURE(status))
		return dev_err_probe(dev, -ENODEV, "Failed to get PMIC hardware revision\n");
	if (hrv != BROXTON_PMIC_WC_HRV)
		return dev_err_probe(dev, -ENODEV, "Invalid PMIC hardware revision: %llu\n", hrv);

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	pmic->irq = ret;

	platform_set_drvdata(pdev, pmic);
	pmic->dev = dev;

	pmic->scu = devm_intel_scu_ipc_dev_get(dev);
	if (!pmic->scu)
		return -EPROBE_DEFER;

	pmic->regmap = devm_regmap_init(dev, NULL, pmic, &bxtwc_regmap_config);
	if (IS_ERR(pmic->regmap))
		return dev_err_probe(dev, PTR_ERR(pmic->regmap), "Failed to initialise regmap\n");

	ret = devm_regmap_add_irq_chip(dev, pmic->regmap, pmic->irq,
				       IRQF_ONESHOT | IRQF_SHARED,
				       0, &bxtwc_regmap_irq_chip,
				       &pmic->irq_chip_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add IRQ chip\n");

	ret = bxtwc_add_chained_devices(pmic, bxt_wc_tmu_dev, ARRAY_SIZE(bxt_wc_tmu_dev),
					pmic->irq_chip_data,
					BXTWC_TMU_LVL1_IRQ,
					IRQF_ONESHOT,
					&bxtwc_regmap_irq_chip_tmu,
					&pmic->irq_chip_data_tmu);
	if (ret)
		return ret;

	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_PWRBTN_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_pwrbtn,
					 &pmic->irq_chip_data_pwrbtn);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add PWRBTN IRQ chip\n");

	/* Add chained IRQ handler for BCU IRQs */
	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_BCU_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_bcu,
					 &pmic->irq_chip_data_bcu);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add BUC IRQ chip\n");

	/* Add chained IRQ handler for ADC IRQs */
	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_ADC_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_adc,
					 &pmic->irq_chip_data_adc);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add ADC IRQ chip\n");

	ret = bxtwc_add_chained_devices(pmic, bxt_wc_chgr_dev, ARRAY_SIZE(bxt_wc_chgr_dev),
					pmic->irq_chip_data,
					BXTWC_CHGR_LVL1_IRQ,
					IRQF_ONESHOT,
					&bxtwc_regmap_irq_chip_chgr,
					&pmic->irq_chip_data_chgr);
	if (ret)
		return ret;

	/* Add chained IRQ handler for CRIT IRQs */
	ret = bxtwc_add_chained_irq_chip(pmic, pmic->irq_chip_data,
					 BXTWC_CRIT_LVL1_IRQ,
					 IRQF_ONESHOT,
					 &bxtwc_regmap_irq_chip_crit,
					 &pmic->irq_chip_data_crit);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add CRIT IRQ chip\n");

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, bxt_wc_dev, ARRAY_SIZE(bxt_wc_dev),
				   NULL, 0, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add devices\n");

	/*
	 * There is a known H/W bug. Upon reset, BIT 5 of register
	 * BXTWC_CHGR_LVL1_IRQ is 0 which is the expected value. However,
	 * later it's set to 1(masked) automatically by hardware. So we
	 * place the software workaround here to unmask it again in order
	 * to re-enable the charger interrupt.
	 */
	regmap_update_bits(pmic->regmap, BXTWC_MIRQLVL1, BXTWC_MIRQLVL1_MCHGR, 0);

	return 0;
}

static void bxtwc_shutdown(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = platform_get_drvdata(pdev);

	disable_irq(pmic->irq);
}

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

static DEFINE_SIMPLE_DEV_PM_OPS(bxtwc_pm_ops, bxtwc_suspend, bxtwc_resume);

static const struct acpi_device_id bxtwc_acpi_ids[] = {
	{ "INT34D3", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, bxtwc_acpi_ids);

static struct platform_driver bxtwc_driver = {
	.probe = bxtwc_probe,
	.shutdown = bxtwc_shutdown,
	.driver	= {
		.name	= "BXTWC PMIC",
		.pm     = pm_sleep_ptr(&bxtwc_pm_ops),
		.acpi_match_table = bxtwc_acpi_ids,
		.dev_groups = bxtwc_groups,
	},
};

module_platform_driver(bxtwc_driver);

MODULE_DESCRIPTION("Intel Broxton Whiskey Cove PMIC MFD core driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Qipeng Zha <qipeng.zha@intel.com>");
