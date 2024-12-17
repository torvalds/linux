// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Intel Broxton PMC
 *
 * (C) Copyright 2014 - 2020 Intel Corporation
 *
 * This driver is based on Intel SCU IPC driver (intel_scu_ipc.c) by
 * Sreedhara DS <sreedhara.ds@intel.com>
 *
 * The PMC (Power Management Controller) running on the ARC processor
 * communicates with another entity running in the IA (Intel Architecture)
 * core through an IPC (Intel Processor Communications) mechanism which in
 * turn sends messages between the IA and the PMC.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel_pmc_bxt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/itco_wdt.h>
#include <linux/platform_data/x86/intel_scu_ipc.h>

/* Residency with clock rate at 19.2MHz to usecs */
#define S0IX_RESIDENCY_IN_USECS(d, s)		\
({						\
	u64 result = 10ull * ((d) + (s));	\
	do_div(result, 192);			\
	result;					\
})

/* Resources exported from IFWI */
#define PLAT_RESOURCE_IPC_INDEX		0
#define PLAT_RESOURCE_IPC_SIZE		0x1000
#define PLAT_RESOURCE_GCR_OFFSET	0x1000
#define PLAT_RESOURCE_GCR_SIZE		0x1000
#define PLAT_RESOURCE_BIOS_DATA_INDEX	1
#define PLAT_RESOURCE_BIOS_IFACE_INDEX	2
#define PLAT_RESOURCE_TELEM_SSRAM_INDEX	3
#define PLAT_RESOURCE_ISP_DATA_INDEX	4
#define PLAT_RESOURCE_ISP_IFACE_INDEX	5
#define PLAT_RESOURCE_GTD_DATA_INDEX	6
#define PLAT_RESOURCE_GTD_IFACE_INDEX	7
#define PLAT_RESOURCE_ACPI_IO_INDEX	0

/*
 * BIOS does not create an ACPI device for each PMC function, but
 * exports multiple resources from one ACPI device (IPC) for multiple
 * functions. This driver is responsible for creating a child device and
 * to export resources for those functions.
 */
#define SMI_EN_OFFSET			0x0040
#define SMI_EN_SIZE			4
#define TCO_BASE_OFFSET			0x0060
#define TCO_REGS_SIZE			16
#define TELEM_SSRAM_SIZE		240
#define TELEM_PMC_SSRAM_OFFSET		0x1b00
#define TELEM_PUNIT_SSRAM_OFFSET	0x1a00

/* Commands */
#define PMC_NORTHPEAK_CTRL		0xed

static inline bool is_gcr_valid(u32 offset)
{
	return offset < PLAT_RESOURCE_GCR_SIZE - 8;
}

/**
 * intel_pmc_gcr_read64() - Read a 64-bit PMC GCR register
 * @pmc: PMC device pointer
 * @offset: offset of GCR register from GCR address base
 * @data: data pointer for storing the register output
 *
 * Reads the 64-bit PMC GCR register at given offset.
 *
 * Return: Negative value on error or 0 on success.
 */
int intel_pmc_gcr_read64(struct intel_pmc_dev *pmc, u32 offset, u64 *data)
{
	if (!is_gcr_valid(offset))
		return -EINVAL;

	spin_lock(&pmc->gcr_lock);
	*data = readq(pmc->gcr_mem_base + offset);
	spin_unlock(&pmc->gcr_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_pmc_gcr_read64);

/**
 * intel_pmc_gcr_update() - Update PMC GCR register bits
 * @pmc: PMC device pointer
 * @offset: offset of GCR register from GCR address base
 * @mask: bit mask for update operation
 * @val: update value
 *
 * Updates the bits of given GCR register as specified by
 * @mask and @val.
 *
 * Return: Negative value on error or 0 on success.
 */
int intel_pmc_gcr_update(struct intel_pmc_dev *pmc, u32 offset, u32 mask, u32 val)
{
	u32 new_val;

	if (!is_gcr_valid(offset))
		return -EINVAL;

	spin_lock(&pmc->gcr_lock);
	new_val = readl(pmc->gcr_mem_base + offset);

	new_val = (new_val & ~mask) | (val & mask);
	writel(new_val, pmc->gcr_mem_base + offset);

	new_val = readl(pmc->gcr_mem_base + offset);
	spin_unlock(&pmc->gcr_lock);

	/* Check whether the bit update is successful */
	return (new_val & mask) != (val & mask) ? -EIO : 0;
}
EXPORT_SYMBOL_GPL(intel_pmc_gcr_update);

/**
 * intel_pmc_s0ix_counter_read() - Read S0ix residency
 * @pmc: PMC device pointer
 * @data: Out param that contains current S0ix residency count.
 *
 * Writes to @data how many usecs the system has been in low-power S0ix
 * state.
 *
 * Return: An error code or 0 on success.
 */
int intel_pmc_s0ix_counter_read(struct intel_pmc_dev *pmc, u64 *data)
{
	u64 deep, shlw;

	spin_lock(&pmc->gcr_lock);
	deep = readq(pmc->gcr_mem_base + PMC_GCR_TELEM_DEEP_S0IX_REG);
	shlw = readq(pmc->gcr_mem_base + PMC_GCR_TELEM_SHLW_S0IX_REG);
	spin_unlock(&pmc->gcr_lock);

	*data = S0IX_RESIDENCY_IN_USECS(deep, shlw);
	return 0;
}
EXPORT_SYMBOL_GPL(intel_pmc_s0ix_counter_read);

/**
 * simplecmd_store() - Send a simple IPC command
 * @dev: Device under the attribute is
 * @attr: Attribute in question
 * @buf: Buffer holding data to be stored to the attribute
 * @count: Number of bytes in @buf
 *
 * Expects a string with two integers separated with space. These two
 * values hold command and subcommand that is send to PMC.
 *
 * Return: Number number of bytes written (@count) or negative errno in
 *	   case of error.
 */
static ssize_t simplecmd_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct intel_pmc_dev *pmc = dev_get_drvdata(dev);
	struct intel_scu_ipc_dev *scu = pmc->scu;
	int subcmd;
	int cmd;
	int ret;

	ret = sscanf(buf, "%d %d", &cmd, &subcmd);
	if (ret != 2) {
		dev_err(dev, "Invalid values, expected: cmd subcmd\n");
		return -EINVAL;
	}

	ret = intel_scu_ipc_dev_simple_command(scu, cmd, subcmd);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(simplecmd);

/**
 * northpeak_store() - Enable or disable Northpeak
 * @dev: Device under the attribute is
 * @attr: Attribute in question
 * @buf: Buffer holding data to be stored to the attribute
 * @count: Number of bytes in @buf
 *
 * Expects an unsigned integer. Non-zero enables Northpeak and zero
 * disables it.
 *
 * Return: Number number of bytes written (@count) or negative errno in
 *	   case of error.
 */
static ssize_t northpeak_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct intel_pmc_dev *pmc = dev_get_drvdata(dev);
	struct intel_scu_ipc_dev *scu = pmc->scu;
	unsigned long val;
	int subcmd;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	/* Northpeak is enabled if subcmd == 1 and disabled if it is 0 */
	if (val)
		subcmd = 1;
	else
		subcmd = 0;

	ret = intel_scu_ipc_dev_simple_command(scu, PMC_NORTHPEAK_CTRL, subcmd);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(northpeak);

static struct attribute *intel_pmc_attrs[] = {
	&dev_attr_northpeak.attr,
	&dev_attr_simplecmd.attr,
	NULL
};

static const struct attribute_group intel_pmc_group = {
	.attrs = intel_pmc_attrs,
};

static const struct attribute_group *intel_pmc_groups[] = {
	&intel_pmc_group,
	NULL
};

static struct resource punit_res[6];

static struct mfd_cell punit = {
	.name = "intel_punit_ipc",
	.resources = punit_res,
};

static struct itco_wdt_platform_data tco_pdata = {
	.name = "Apollo Lake SoC",
	.version = 5,
	.no_reboot_use_pmc = true,
};

static struct resource tco_res[2];

static const struct mfd_cell tco = {
	.name = "iTCO_wdt",
	.ignore_resource_conflicts = true,
	.resources = tco_res,
	.num_resources = ARRAY_SIZE(tco_res),
	.platform_data = &tco_pdata,
	.pdata_size = sizeof(tco_pdata),
};

static const struct resource telem_res[] = {
	DEFINE_RES_MEM(TELEM_PUNIT_SSRAM_OFFSET, TELEM_SSRAM_SIZE),
	DEFINE_RES_MEM(TELEM_PMC_SSRAM_OFFSET, TELEM_SSRAM_SIZE),
};

static const struct mfd_cell telem = {
	.name = "intel_telemetry",
	.resources = telem_res,
	.num_resources = ARRAY_SIZE(telem_res),
};

static int intel_pmc_get_tco_resources(struct platform_device *pdev)
{
	struct resource *res;

	if (acpi_has_watchdog())
		return 0;

	res = platform_get_resource(pdev, IORESOURCE_IO,
				    PLAT_RESOURCE_ACPI_IO_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get IO resource\n");
		return -EINVAL;
	}

	tco_res[0].flags = IORESOURCE_IO;
	tco_res[0].start = res->start + TCO_BASE_OFFSET;
	tco_res[0].end = tco_res[0].start + TCO_REGS_SIZE - 1;
	tco_res[1].flags = IORESOURCE_IO;
	tco_res[1].start = res->start + SMI_EN_OFFSET;
	tco_res[1].end = tco_res[1].start + SMI_EN_SIZE - 1;

	return 0;
}

static int intel_pmc_get_resources(struct platform_device *pdev,
				   struct intel_pmc_dev *pmc,
				   struct intel_scu_ipc_data *scu_data)
{
	struct resource gcr_res;
	size_t npunit_res = 0;
	struct resource *res;
	int ret;

	scu_data->irq = platform_get_irq_optional(pdev, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_IPC_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get IPC resource\n");
		return -EINVAL;
	}

	/* IPC registers */
	scu_data->mem.flags = res->flags;
	scu_data->mem.start = res->start;
	scu_data->mem.end = res->start + PLAT_RESOURCE_IPC_SIZE - 1;

	/* GCR registers */
	gcr_res.flags = res->flags;
	gcr_res.start = res->start + PLAT_RESOURCE_GCR_OFFSET;
	gcr_res.end = gcr_res.start + PLAT_RESOURCE_GCR_SIZE - 1;

	pmc->gcr_mem_base = devm_ioremap_resource(&pdev->dev, &gcr_res);
	if (IS_ERR(pmc->gcr_mem_base))
		return PTR_ERR(pmc->gcr_mem_base);

	/* Only register iTCO watchdog if there is no WDAT ACPI table */
	ret = intel_pmc_get_tco_resources(pdev);
	if (ret)
		return ret;

	/* BIOS data register */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_BIOS_DATA_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get resource of P-unit BIOS data\n");
		return -EINVAL;
	}
	punit_res[npunit_res++] = *res;

	/* BIOS interface register */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_BIOS_IFACE_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get resource of P-unit BIOS interface\n");
		return -EINVAL;
	}
	punit_res[npunit_res++] = *res;

	/* ISP data register, optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_ISP_DATA_INDEX);
	if (res)
		punit_res[npunit_res++] = *res;

	/* ISP interface register, optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_ISP_IFACE_INDEX);
	if (res)
		punit_res[npunit_res++] = *res;

	/* GTD data register, optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_GTD_DATA_INDEX);
	if (res)
		punit_res[npunit_res++] = *res;

	/* GTD interface register, optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_GTD_IFACE_INDEX);
	if (res)
		punit_res[npunit_res++] = *res;

	punit.num_resources = npunit_res;

	/* Telemetry SSRAM is optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_TELEM_SSRAM_INDEX);
	if (res)
		pmc->telem_base = res;

	return 0;
}

static int intel_pmc_create_devices(struct intel_pmc_dev *pmc)
{
	int ret;

	if (!acpi_has_watchdog()) {
		ret = devm_mfd_add_devices(pmc->dev, PLATFORM_DEVID_AUTO, &tco,
					   1, NULL, 0, NULL);
		if (ret)
			return ret;
	}

	ret = devm_mfd_add_devices(pmc->dev, PLATFORM_DEVID_AUTO, &punit, 1,
				   NULL, 0, NULL);
	if (ret)
		return ret;

	if (pmc->telem_base) {
		ret = devm_mfd_add_devices(pmc->dev, PLATFORM_DEVID_AUTO,
					   &telem, 1, pmc->telem_base, 0, NULL);
	}

	return ret;
}

static const struct acpi_device_id intel_pmc_acpi_ids[] = {
	{ "INT34D2" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, intel_pmc_acpi_ids);

static int intel_pmc_probe(struct platform_device *pdev)
{
	struct intel_scu_ipc_data scu_data = {};
	struct intel_pmc_dev *pmc;
	int ret;

	pmc = devm_kzalloc(&pdev->dev, sizeof(*pmc), GFP_KERNEL);
	if (!pmc)
		return -ENOMEM;

	pmc->dev = &pdev->dev;
	spin_lock_init(&pmc->gcr_lock);

	ret = intel_pmc_get_resources(pdev, pmc, &scu_data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request resources\n");
		return ret;
	}

	pmc->scu = devm_intel_scu_ipc_register(&pdev->dev, &scu_data);
	if (IS_ERR(pmc->scu))
		return PTR_ERR(pmc->scu);

	platform_set_drvdata(pdev, pmc);

	ret = intel_pmc_create_devices(pmc);
	if (ret)
		dev_err(&pdev->dev, "Failed to create PMC devices\n");

	return ret;
}

static struct platform_driver intel_pmc_driver = {
	.probe = intel_pmc_probe,
	.driver = {
		.name = "intel_pmc_bxt",
		.acpi_match_table = intel_pmc_acpi_ids,
		.dev_groups = intel_pmc_groups,
	},
};
module_platform_driver(intel_pmc_driver);

MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_AUTHOR("Zha Qipeng <qipeng.zha@intel.com>");
MODULE_DESCRIPTION("Intel Broxton PMC driver");
MODULE_LICENSE("GPL v2");
