// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kontron PLD MFD core driver
 *
 * Copyright (c) 2010-2013 Kontron Europe GmbH
 * Author: Michael Brunner <michael.brunner@kontron.com>
 */

#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/kempld.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/acpi.h>

#define MAX_ID_LEN 4
static char force_device_id[MAX_ID_LEN + 1] = "";
module_param_string(force_device_id, force_device_id,
		    sizeof(force_device_id), 0);
MODULE_PARM_DESC(force_device_id, "Override detected product");

/*
 * Get hardware mutex to block firmware from accessing the pld.
 * It is possible for the firmware may hold the mutex for an extended length of
 * time. This function will block until access has been granted.
 */
static void kempld_get_hardware_mutex(struct kempld_device_data *pld)
{
	/* The mutex bit will read 1 until access has been granted */
	while (ioread8(pld->io_index) & KEMPLD_MUTEX_KEY)
		usleep_range(1000, 3000);
}

static void kempld_release_hardware_mutex(struct kempld_device_data *pld)
{
	/* The harware mutex is released when 1 is written to the mutex bit. */
	iowrite8(KEMPLD_MUTEX_KEY, pld->io_index);
}

static int kempld_get_info_generic(struct kempld_device_data *pld)
{
	u16 version;
	u8 spec;

	kempld_get_mutex(pld);

	version = kempld_read16(pld, KEMPLD_VERSION);
	spec = kempld_read8(pld, KEMPLD_SPEC);
	pld->info.buildnr = kempld_read16(pld, KEMPLD_BUILDNR);

	pld->info.minor = KEMPLD_VERSION_GET_MINOR(version);
	pld->info.major = KEMPLD_VERSION_GET_MAJOR(version);
	pld->info.number = KEMPLD_VERSION_GET_NUMBER(version);
	pld->info.type = KEMPLD_VERSION_GET_TYPE(version);

	if (spec == 0xff) {
		pld->info.spec_minor = 0;
		pld->info.spec_major = 1;
	} else {
		pld->info.spec_minor = KEMPLD_SPEC_GET_MINOR(spec);
		pld->info.spec_major = KEMPLD_SPEC_GET_MAJOR(spec);
	}

	if (pld->info.spec_major > 0)
		pld->feature_mask = kempld_read16(pld, KEMPLD_FEATURE);
	else
		pld->feature_mask = 0;

	kempld_release_mutex(pld);

	return 0;
}

enum kempld_cells {
	KEMPLD_I2C = 0,
	KEMPLD_WDT,
	KEMPLD_GPIO,
	KEMPLD_UART,
};

static const char *kempld_dev_names[] = {
	[KEMPLD_I2C] = "kempld-i2c",
	[KEMPLD_WDT] = "kempld-wdt",
	[KEMPLD_GPIO] = "kempld-gpio",
	[KEMPLD_UART] = "kempld-uart",
};

#define KEMPLD_MAX_DEVS	ARRAY_SIZE(kempld_dev_names)

static int kempld_register_cells_generic(struct kempld_device_data *pld)
{
	struct mfd_cell devs[KEMPLD_MAX_DEVS] = {};
	int i = 0;

	if (pld->feature_mask & KEMPLD_FEATURE_BIT_I2C)
		devs[i++].name = kempld_dev_names[KEMPLD_I2C];

	if (pld->feature_mask & KEMPLD_FEATURE_BIT_WATCHDOG)
		devs[i++].name = kempld_dev_names[KEMPLD_WDT];

	if (pld->feature_mask & KEMPLD_FEATURE_BIT_GPIO)
		devs[i++].name = kempld_dev_names[KEMPLD_GPIO];

	if (pld->feature_mask & KEMPLD_FEATURE_MASK_UART)
		devs[i++].name = kempld_dev_names[KEMPLD_UART];

	return mfd_add_devices(pld->dev, -1, devs, i, NULL, 0, NULL);
}

static struct resource kempld_ioresource = {
	.start	= KEMPLD_IOINDEX,
	.end	= KEMPLD_IODATA,
	.flags	= IORESOURCE_IO,
};

static const struct kempld_platform_data kempld_platform_data_generic = {
	.pld_clock		= KEMPLD_CLK,
	.ioresource		= &kempld_ioresource,
	.get_hardware_mutex	= kempld_get_hardware_mutex,
	.release_hardware_mutex	= kempld_release_hardware_mutex,
	.get_info		= kempld_get_info_generic,
	.register_cells		= kempld_register_cells_generic,
};

static struct platform_device *kempld_pdev;
static bool kempld_acpi_mode;

static int kempld_create_platform_device(const struct dmi_system_id *id)
{
	const struct kempld_platform_data *pdata = id->driver_data;
	int ret;

	kempld_pdev = platform_device_alloc("kempld", -1);
	if (!kempld_pdev)
		return -ENOMEM;

	ret = platform_device_add_data(kempld_pdev, pdata, sizeof(*pdata));
	if (ret)
		goto err;

	ret = platform_device_add_resources(kempld_pdev, pdata->ioresource, 1);
	if (ret)
		goto err;

	ret = platform_device_add(kempld_pdev);
	if (ret)
		goto err;

	return 0;
err:
	platform_device_put(kempld_pdev);
	return ret;
}

/**
 * kempld_read8 - read 8 bit register
 * @pld: kempld_device_data structure describing the PLD
 * @index: register index on the chip
 *
 * kempld_get_mutex must be called prior to calling this function.
 */
u8 kempld_read8(struct kempld_device_data *pld, u8 index)
{
	iowrite8(index, pld->io_index);
	return ioread8(pld->io_data);
}
EXPORT_SYMBOL_GPL(kempld_read8);

/**
 * kempld_write8 - write 8 bit register
 * @pld: kempld_device_data structure describing the PLD
 * @index: register index on the chip
 * @data: new register value
 *
 * kempld_get_mutex must be called prior to calling this function.
 */
void kempld_write8(struct kempld_device_data *pld, u8 index, u8 data)
{
	iowrite8(index, pld->io_index);
	iowrite8(data, pld->io_data);
}
EXPORT_SYMBOL_GPL(kempld_write8);

/**
 * kempld_read16 - read 16 bit register
 * @pld: kempld_device_data structure describing the PLD
 * @index: register index on the chip
 *
 * kempld_get_mutex must be called prior to calling this function.
 */
u16 kempld_read16(struct kempld_device_data *pld, u8 index)
{
	return kempld_read8(pld, index) | kempld_read8(pld, index + 1) << 8;
}
EXPORT_SYMBOL_GPL(kempld_read16);

/**
 * kempld_write16 - write 16 bit register
 * @pld: kempld_device_data structure describing the PLD
 * @index: register index on the chip
 * @data: new register value
 *
 * kempld_get_mutex must be called prior to calling this function.
 */
void kempld_write16(struct kempld_device_data *pld, u8 index, u16 data)
{
	kempld_write8(pld, index, (u8)data);
	kempld_write8(pld, index + 1, (u8)(data >> 8));
}
EXPORT_SYMBOL_GPL(kempld_write16);

/**
 * kempld_read32 - read 32 bit register
 * @pld: kempld_device_data structure describing the PLD
 * @index: register index on the chip
 *
 * kempld_get_mutex must be called prior to calling this function.
 */
u32 kempld_read32(struct kempld_device_data *pld, u8 index)
{
	return kempld_read16(pld, index) | kempld_read16(pld, index + 2) << 16;
}
EXPORT_SYMBOL_GPL(kempld_read32);

/**
 * kempld_write32 - write 32 bit register
 * @pld: kempld_device_data structure describing the PLD
 * @index: register index on the chip
 * @data: new register value
 *
 * kempld_get_mutex must be called prior to calling this function.
 */
void kempld_write32(struct kempld_device_data *pld, u8 index, u32 data)
{
	kempld_write16(pld, index, (u16)data);
	kempld_write16(pld, index + 2, (u16)(data >> 16));
}
EXPORT_SYMBOL_GPL(kempld_write32);

/**
 * kempld_get_mutex - acquire PLD mutex
 * @pld: kempld_device_data structure describing the PLD
 */
void kempld_get_mutex(struct kempld_device_data *pld)
{
	const struct kempld_platform_data *pdata = dev_get_platdata(pld->dev);

	mutex_lock(&pld->lock);
	pdata->get_hardware_mutex(pld);
}
EXPORT_SYMBOL_GPL(kempld_get_mutex);

/**
 * kempld_release_mutex - release PLD mutex
 * @pld: kempld_device_data structure describing the PLD
 */
void kempld_release_mutex(struct kempld_device_data *pld)
{
	const struct kempld_platform_data *pdata = dev_get_platdata(pld->dev);

	pdata->release_hardware_mutex(pld);
	mutex_unlock(&pld->lock);
}
EXPORT_SYMBOL_GPL(kempld_release_mutex);

/**
 * kempld_get_info - update device specific information
 * @pld: kempld_device_data structure describing the PLD
 *
 * This function calls the configured board specific kempld_get_info_XXXX
 * function which is responsible for gathering information about the specific
 * hardware. The information is then stored within the pld structure.
 */
static int kempld_get_info(struct kempld_device_data *pld)
{
	int ret;
	const struct kempld_platform_data *pdata = dev_get_platdata(pld->dev);
	char major, minor;

	ret = pdata->get_info(pld);
	if (ret)
		return ret;

	/* The Kontron PLD firmware version string has the following format:
	 * Pwxy.zzzz
	 *   P:    Fixed
	 *   w:    PLD number    - 1 hex digit
	 *   x:    Major version - 1 alphanumerical digit (0-9A-V)
	 *   y:    Minor version - 1 alphanumerical digit (0-9A-V)
	 *   zzzz: Build number  - 4 zero padded hex digits */

	if (pld->info.major < 10)
		major = pld->info.major + '0';
	else
		major = (pld->info.major - 10) + 'A';
	if (pld->info.minor < 10)
		minor = pld->info.minor + '0';
	else
		minor = (pld->info.minor - 10) + 'A';

	ret = scnprintf(pld->info.version, sizeof(pld->info.version),
			"P%X%c%c.%04X", pld->info.number, major, minor,
			pld->info.buildnr);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * kempld_register_cells - register cell drivers
 *
 * This function registers cell drivers for the detected hardware by calling
 * the configured kempld_register_cells_XXXX function which is responsible
 * to detect and register the needed cell drivers.
 */
static int kempld_register_cells(struct kempld_device_data *pld)
{
	const struct kempld_platform_data *pdata = dev_get_platdata(pld->dev);

	return pdata->register_cells(pld);
}

static const char *kempld_get_type_string(struct kempld_device_data *pld)
{
	const char *version_type;

	switch (pld->info.type) {
	case 0:
		version_type = "release";
		break;
	case 1:
		version_type = "debug";
		break;
	case 2:
		version_type = "custom";
		break;
	default:
		version_type = "unspecified";
		break;
	}

	return version_type;
}

static ssize_t kempld_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kempld_device_data *pld = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", pld->info.version);
}

static ssize_t kempld_specification_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kempld_device_data *pld = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d.%d\n", pld->info.spec_major,
		       pld->info.spec_minor);
}

static ssize_t kempld_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct kempld_device_data *pld = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", kempld_get_type_string(pld));
}

static DEVICE_ATTR(pld_version, S_IRUGO, kempld_version_show, NULL);
static DEVICE_ATTR(pld_specification, S_IRUGO, kempld_specification_show,
		   NULL);
static DEVICE_ATTR(pld_type, S_IRUGO, kempld_type_show, NULL);

static struct attribute *pld_attributes[] = {
	&dev_attr_pld_version.attr,
	&dev_attr_pld_specification.attr,
	&dev_attr_pld_type.attr,
	NULL
};

static const struct attribute_group pld_attr_group = {
	.attrs = pld_attributes,
};

static int kempld_detect_device(struct kempld_device_data *pld)
{
	u8 index_reg;
	int ret;

	mutex_lock(&pld->lock);

	/* Check for empty IO space */
	index_reg = ioread8(pld->io_index);
	if (index_reg == 0xff && ioread8(pld->io_data) == 0xff) {
		mutex_unlock(&pld->lock);
		return -ENODEV;
	}

	/* Release hardware mutex if acquired */
	if (!(index_reg & KEMPLD_MUTEX_KEY)) {
		iowrite8(KEMPLD_MUTEX_KEY, pld->io_index);
		/* PXT and COMe-cPC2 boards may require a second release */
		iowrite8(KEMPLD_MUTEX_KEY, pld->io_index);
	}

	mutex_unlock(&pld->lock);

	ret = kempld_get_info(pld);
	if (ret)
		return ret;

	dev_info(pld->dev, "Found Kontron PLD - %s (%s), spec %d.%d\n",
		 pld->info.version, kempld_get_type_string(pld),
		 pld->info.spec_major, pld->info.spec_minor);

	ret = sysfs_create_group(&pld->dev->kobj, &pld_attr_group);
	if (ret)
		return ret;

	ret = kempld_register_cells(pld);
	if (ret)
		sysfs_remove_group(&pld->dev->kobj, &pld_attr_group);

	return ret;
}

#ifdef CONFIG_ACPI
static int kempld_get_acpi_data(struct platform_device *pdev)
{
	struct list_head resource_list;
	struct resource *resources;
	struct resource_entry *rentry;
	struct device *dev = &pdev->dev;
	struct acpi_device *acpi_dev = ACPI_COMPANION(dev);
	const struct kempld_platform_data *pdata;
	int ret;
	int count;

	pdata = acpi_device_get_match_data(dev);
	ret = platform_device_add_data(pdev, pdata,
				       sizeof(struct kempld_platform_data));
	if (ret)
		return ret;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(acpi_dev, &resource_list, NULL, NULL);
	if (ret < 0)
		goto out;

	count = ret;

	if (count == 0) {
		ret = platform_device_add_resources(pdev, pdata->ioresource, 1);
		goto out;
	}

	resources = devm_kcalloc(&acpi_dev->dev, count, sizeof(*resources),
				 GFP_KERNEL);
	if (!resources) {
		ret = -ENOMEM;
		goto out;
	}

	count = 0;
	list_for_each_entry(rentry, &resource_list, node) {
		memcpy(&resources[count], rentry->res,
		       sizeof(*resources));
		count++;
	}
	ret = platform_device_add_resources(pdev, resources, count);

out:
	acpi_dev_free_resource_list(&resource_list);

	return ret;
}
#else
static int kempld_get_acpi_data(struct platform_device *pdev)
{
	return -ENODEV;
}
#endif /* CONFIG_ACPI */

static int kempld_probe(struct platform_device *pdev)
{
	const struct kempld_platform_data *pdata;
	struct device *dev = &pdev->dev;
	struct kempld_device_data *pld;
	struct resource *ioport;
	int ret;

	if (kempld_pdev == NULL) {
		/*
		 * No kempld_pdev device has been registered in kempld_init,
		 * so we seem to be probing an ACPI platform device.
		 */
		ret = kempld_get_acpi_data(pdev);
		if (ret)
			return ret;

		kempld_acpi_mode = true;
	} else if (kempld_pdev != pdev) {
		/*
		 * The platform device we are probing is not the one we
		 * registered in kempld_init using the DMI table, so this one
		 * comes from ACPI.
		 * As we can only probe one - abort here and use the DMI
		 * based one instead.
		 */
		dev_notice(dev, "platform device exists - not using ACPI\n");
		return -ENODEV;
	}
	pdata = dev_get_platdata(dev);

	pld = devm_kzalloc(dev, sizeof(*pld), GFP_KERNEL);
	if (!pld)
		return -ENOMEM;

	ioport = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!ioport)
		return -EINVAL;

	pld->io_base = devm_ioport_map(dev, ioport->start,
					resource_size(ioport));
	if (!pld->io_base)
		return -ENOMEM;

	pld->io_index = pld->io_base;
	pld->io_data = pld->io_base + 1;
	pld->pld_clock = pdata->pld_clock;
	pld->dev = dev;

	mutex_init(&pld->lock);
	platform_set_drvdata(pdev, pld);

	return kempld_detect_device(pld);
}

static int kempld_remove(struct platform_device *pdev)
{
	struct kempld_device_data *pld = platform_get_drvdata(pdev);
	const struct kempld_platform_data *pdata = dev_get_platdata(pld->dev);

	sysfs_remove_group(&pld->dev->kobj, &pld_attr_group);

	mfd_remove_devices(&pdev->dev);
	pdata->release_hardware_mutex(pld);

	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id kempld_acpi_table[] = {
	{ "KEM0001", (kernel_ulong_t)&kempld_platform_data_generic },
	{}
};
MODULE_DEVICE_TABLE(acpi, kempld_acpi_table);
#endif

static struct platform_driver kempld_driver = {
	.driver		= {
		.name	= "kempld",
		.acpi_match_table = ACPI_PTR(kempld_acpi_table),
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
	},
	.probe		= kempld_probe,
	.remove		= kempld_remove,
};

static const struct dmi_system_id kempld_dmi_table[] __initconst = {
	{
		.ident = "BBD6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bBD"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "BBL6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bBL6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "BHL6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bHL6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "BKL6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bKL6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "BSL6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bSL6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CAL6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cAL"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CBL6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cBL6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CBW6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cBW6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CCR2",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bIP2"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CCR6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bIP6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CHL6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cHL6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CHR2",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "ETXexpress-SC T2"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CHR2",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "ETXe-SC T2"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CHR2",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bSC2"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CHR6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "ETXexpress-SC T6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CHR6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "ETXe-SC T6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CHR6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bSC6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CKL6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cKL6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CNTG",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "ETXexpress-PC"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CNTG",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-bPC2"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CNTX",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "PXT"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CSL6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cSL6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "CVV6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cBT"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "FRI2",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BIOS_VERSION, "FRI2"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "FRI2",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Fish River Island II"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "MAL1",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-mAL10"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "MBR1",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "ETX-OH"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "MVV1",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-mBT"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "NTC1",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "nanoETXexpress-TT"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "NTC1",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "nETXe-TT"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "NTC1",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-mTT"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "NUP1",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-mCT"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "UNP1",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "microETXexpress-DC"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "UNP1",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cDC2"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "UNTG",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "microETXexpress-PC"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "UNTG",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cPC2"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	}, {
		.ident = "UUP6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cCT6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	},
	{
		.ident = "UTH6",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Kontron"),
			DMI_MATCH(DMI_BOARD_NAME, "COMe-cTH6"),
		},
		.driver_data = (void *)&kempld_platform_data_generic,
		.callback = kempld_create_platform_device,
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, kempld_dmi_table);

static int __init kempld_init(void)
{
	const struct dmi_system_id *id;
	int ret;

	if (force_device_id[0]) {
		for (id = kempld_dmi_table;
		     id->matches[0].slot != DMI_NONE; id++)
			if (strstr(id->ident, force_device_id))
				if (id->callback && !id->callback(id))
					break;
		if (id->matches[0].slot == DMI_NONE)
			return -ENODEV;
	}

	ret = platform_driver_register(&kempld_driver);
	if (ret)
		return ret;

	/*
	 * With synchronous probing the device should already be probed now.
	 * If no device id is forced and also no ACPI definition for the
	 * device was found, scan DMI table as fallback.
	 *
	 * If drivers_autoprobing is disabled and the device is found here,
	 * only that device can be bound manually later.
	 */
	if (!kempld_pdev && !kempld_acpi_mode)
		dmi_check_system(kempld_dmi_table);

	return 0;
}

static void __exit kempld_exit(void)
{
	if (kempld_pdev)
		platform_device_unregister(kempld_pdev);

	platform_driver_unregister(&kempld_driver);
}

module_init(kempld_init);
module_exit(kempld_exit);

MODULE_DESCRIPTION("KEM PLD Core Driver");
MODULE_AUTHOR("Michael Brunner <michael.brunner@kontron.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:kempld-core");
