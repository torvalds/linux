// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD ACPI support for ACPI2platform device.
 *
 * Copyright (c) 2014,2015 AMD Corporation.
 * Authors: Ken Xue <Ken.Xue@amd.com>
 *	Wu, Jeff <Jeff.Wu@amd.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_data/clk-fch.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/clkdev.h>
#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pm.h>

#include "internal.h"

ACPI_MODULE_NAME("acpi_apd");
struct apd_private_data;

/**
 * ACPI_APD_SYSFS : add device attributes in sysfs
 * ACPI_APD_PM : attach power domain to device
 */
#define ACPI_APD_SYSFS	BIT(0)
#define ACPI_APD_PM	BIT(1)

/**
 * struct apd_device_desc - a descriptor for apd device
 * @flags: device flags like %ACPI_APD_SYSFS, %ACPI_APD_PM
 * @fixed_clk_rate: fixed rate input clock source for acpi device;
 *			0 means no fixed rate input clock source
 * @setup: a hook routine to set device resource during create platform device
 *
 * Device description defined as acpi_device_id.driver_data
 */
struct apd_device_desc {
	unsigned int flags;
	unsigned int fixed_clk_rate;
	struct property_entry *properties;
	int (*setup)(struct apd_private_data *pdata);
};

struct apd_private_data {
	struct clk *clk;
	struct acpi_device *adev;
	const struct apd_device_desc *dev_desc;
};

#if defined(CONFIG_X86_AMD_PLATFORM_DEVICE) || defined(CONFIG_ARM64)
#define APD_ADDR(desc)	((unsigned long)&desc)

static int acpi_apd_setup(struct apd_private_data *pdata)
{
	const struct apd_device_desc *dev_desc = pdata->dev_desc;
	struct clk *clk;

	if (dev_desc->fixed_clk_rate) {
		clk = clk_register_fixed_rate(&pdata->adev->dev,
					dev_name(&pdata->adev->dev),
					NULL, 0, dev_desc->fixed_clk_rate);
		clk_register_clkdev(clk, NULL, dev_name(&pdata->adev->dev));
		pdata->clk = clk;
	}

	return 0;
}

#ifdef CONFIG_X86_AMD_PLATFORM_DEVICE

static int misc_check_res(struct acpi_resource *ares, void *data)
{
	struct resource res;

	return !acpi_dev_resource_memory(ares, &res);
}

static int fch_misc_setup(struct apd_private_data *pdata)
{
	struct acpi_device *adev = pdata->adev;
	const union acpi_object *obj;
	struct platform_device *clkdev;
	struct fch_clk_data *clk_data;
	struct resource_entry *rentry;
	struct list_head resource_list;
	int ret;

	clk_data = devm_kzalloc(&adev->dev, sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, misc_check_res,
				     NULL);
	if (ret < 0)
		return -ENOENT;

	if (!acpi_dev_get_property(adev, "is-rv", ACPI_TYPE_INTEGER, &obj))
		clk_data->is_rv = obj->integer.value;

	list_for_each_entry(rentry, &resource_list, node) {
		clk_data->base = devm_ioremap(&adev->dev, rentry->res->start,
					      resource_size(rentry->res));
		break;
	}

	acpi_dev_free_resource_list(&resource_list);

	clkdev = platform_device_register_data(&adev->dev, "clk-fch",
					       PLATFORM_DEVID_NONE, clk_data,
					       sizeof(*clk_data));
	return PTR_ERR_OR_ZERO(clkdev);
}

static const struct apd_device_desc cz_i2c_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 133000000,
};

static const struct apd_device_desc wt_i2c_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 150000000,
};

static struct property_entry uart_properties[] = {
	PROPERTY_ENTRY_U32("reg-io-width", 4),
	PROPERTY_ENTRY_U32("reg-shift", 2),
	PROPERTY_ENTRY_BOOL("snps,uart-16550-compatible"),
	{ },
};

static const struct apd_device_desc cz_uart_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 48000000,
	.properties = uart_properties,
};

static const struct apd_device_desc fch_misc_desc = {
	.setup = fch_misc_setup,
};
#endif

#ifdef CONFIG_ARM64
static const struct apd_device_desc xgene_i2c_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 100000000,
};

static const struct apd_device_desc vulcan_spi_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 133000000,
};

static const struct apd_device_desc hip07_i2c_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 200000000,
};

static const struct apd_device_desc hip08_i2c_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 250000000,
};

static const struct apd_device_desc hip08_lite_i2c_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 125000000,
};

static const struct apd_device_desc thunderx2_i2c_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 125000000,
};

static const struct apd_device_desc nxp_i2c_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 350000000,
};

static const struct apd_device_desc hip08_spi_desc = {
	.setup = acpi_apd_setup,
	.fixed_clk_rate = 250000000,
};
#endif

#else

#define APD_ADDR(desc) (0UL)

#endif /* CONFIG_X86_AMD_PLATFORM_DEVICE */

/**
* Create platform device during acpi scan attach handle.
* Return value > 0 on success of creating device.
*/
static int acpi_apd_create_device(struct acpi_device *adev,
				   const struct acpi_device_id *id)
{
	const struct apd_device_desc *dev_desc = (void *)id->driver_data;
	struct apd_private_data *pdata;
	struct platform_device *pdev;
	int ret;

	if (!dev_desc) {
		pdev = acpi_create_platform_device(adev, NULL);
		return IS_ERR_OR_NULL(pdev) ? PTR_ERR(pdev) : 1;
	}

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->adev = adev;
	pdata->dev_desc = dev_desc;

	if (dev_desc->setup) {
		ret = dev_desc->setup(pdata);
		if (ret)
			goto err_out;
	}

	adev->driver_data = pdata;
	pdev = acpi_create_platform_device(adev, dev_desc->properties);
	if (!IS_ERR_OR_NULL(pdev))
		return 1;

	ret = PTR_ERR(pdev);
	adev->driver_data = NULL;

 err_out:
	kfree(pdata);
	return ret;
}

static const struct acpi_device_id acpi_apd_device_ids[] = {
	/* Generic apd devices */
#ifdef CONFIG_X86_AMD_PLATFORM_DEVICE
	{ "AMD0010", APD_ADDR(cz_i2c_desc) },
	{ "AMDI0010", APD_ADDR(wt_i2c_desc) },
	{ "AMD0020", APD_ADDR(cz_uart_desc) },
	{ "AMDI0020", APD_ADDR(cz_uart_desc) },
	{ "AMD0030", },
	{ "AMD0040", APD_ADDR(fch_misc_desc)},
	{ "HYGO0010", APD_ADDR(wt_i2c_desc) },
#endif
#ifdef CONFIG_ARM64
	{ "APMC0D0F", APD_ADDR(xgene_i2c_desc) },
	{ "BRCM900D", APD_ADDR(vulcan_spi_desc) },
	{ "CAV900D",  APD_ADDR(vulcan_spi_desc) },
	{ "CAV9007",  APD_ADDR(thunderx2_i2c_desc) },
	{ "HISI02A1", APD_ADDR(hip07_i2c_desc) },
	{ "HISI02A2", APD_ADDR(hip08_i2c_desc) },
	{ "HISI02A3", APD_ADDR(hip08_lite_i2c_desc) },
	{ "HISI0173", APD_ADDR(hip08_spi_desc) },
	{ "NXP0001", APD_ADDR(nxp_i2c_desc) },
#endif
	{ }
};

static struct acpi_scan_handler apd_handler = {
	.ids = acpi_apd_device_ids,
	.attach = acpi_apd_create_device,
};

void __init acpi_apd_init(void)
{
	acpi_scan_add_handler(&apd_handler);
}
