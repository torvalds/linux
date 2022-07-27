// SPDX-License-Identifier: GPL-2.0+
/*
 * Serial multi-instantiate driver, pseudo driver to instantiate multiple
 * client devices from a single fwnode.
 *
 * Copyright 2018 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#define IRQ_RESOURCE_TYPE	GENMASK(1, 0)
#define IRQ_RESOURCE_NONE	0
#define IRQ_RESOURCE_GPIO	1
#define IRQ_RESOURCE_APIC	2

enum smi_bus_type {
	SMI_I2C,
	SMI_SPI,
	SMI_AUTO_DETECT,
};

struct smi_instance {
	const char *type;
	unsigned int flags;
	int irq_idx;
};

struct smi_node {
	enum smi_bus_type bus_type;
	struct smi_instance instances[];
};

struct smi {
	int i2c_num;
	int spi_num;
	struct i2c_client **i2c_devs;
	struct spi_device **spi_devs;
};

static int smi_get_irq(struct platform_device *pdev, struct acpi_device *adev,
		       const struct smi_instance *inst)
{
	int ret;

	switch (inst->flags & IRQ_RESOURCE_TYPE) {
	case IRQ_RESOURCE_GPIO:
		ret = acpi_dev_gpio_irq_get(adev, inst->irq_idx);
		break;
	case IRQ_RESOURCE_APIC:
		ret = platform_get_irq(pdev, inst->irq_idx);
		break;
	default:
		return 0;
	}

	if (ret < 0)
		dev_err_probe(&pdev->dev, ret, "Error requesting irq at index %d: %d\n",
			      inst->irq_idx, ret);

	return ret;
}

static void smi_devs_unregister(struct smi *smi)
{
	while (smi->i2c_num > 0)
		i2c_unregister_device(smi->i2c_devs[--smi->i2c_num]);

	while (smi->spi_num > 0)
		spi_unregister_device(smi->spi_devs[--smi->spi_num]);
}

/**
 * smi_spi_probe - Instantiate multiple SPI devices from inst array
 * @pdev:	Platform device
 * @adev:	ACPI device
 * @smi:	Internal struct for Serial multi instantiate driver
 * @inst_array:	Array of instances to probe
 *
 * Returns the number of SPI devices instantiate, Zero if none is found or a negative error code.
 */
static int smi_spi_probe(struct platform_device *pdev, struct acpi_device *adev, struct smi *smi,
			 const struct smi_instance *inst_array)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *ctlr;
	struct spi_device *spi_dev;
	char name[50];
	int i, ret, count;

	ret = acpi_spi_count_resources(adev);
	if (ret < 0)
		return ret;
	else if (!ret)
		return -ENODEV;

	count = ret;

	smi->spi_devs = devm_kcalloc(dev, count, sizeof(*smi->spi_devs), GFP_KERNEL);
	if (!smi->spi_devs)
		return -ENOMEM;

	for (i = 0; i < count && inst_array[i].type; i++) {

		spi_dev = acpi_spi_device_alloc(NULL, adev, i);
		if (IS_ERR(spi_dev)) {
			ret = PTR_ERR(spi_dev);
			dev_err_probe(dev, ret, "failed to allocate SPI device %s from ACPI: %d\n",
				      dev_name(&adev->dev), ret);
			goto error;
		}

		ctlr = spi_dev->controller;

		strscpy(spi_dev->modalias, inst_array[i].type, sizeof(spi_dev->modalias));

		ret = smi_get_irq(pdev, adev, &inst_array[i]);
		if (ret < 0) {
			spi_dev_put(spi_dev);
			goto error;
		}
		spi_dev->irq = ret;

		snprintf(name, sizeof(name), "%s-%s-%s.%d", dev_name(&ctlr->dev), dev_name(dev),
			 inst_array[i].type, i);
		spi_dev->dev.init_name = name;

		ret = spi_add_device(spi_dev);
		if (ret) {
			dev_err_probe(&ctlr->dev, ret,
				      "failed to add SPI device %s from ACPI: %d\n",
				      dev_name(&adev->dev), ret);
			spi_dev_put(spi_dev);
			goto error;
		}

		dev_dbg(dev, "SPI device %s using chip select %u", name, spi_dev->chip_select);

		smi->spi_devs[i] = spi_dev;
		smi->spi_num++;
	}

	if (smi->spi_num < count) {
		dev_dbg(dev, "Error finding driver, idx %d\n", i);
		ret = -ENODEV;
		goto error;
	}

	dev_info(dev, "Instantiated %d SPI devices.\n", smi->spi_num);

	return 0;
error:
	smi_devs_unregister(smi);

	return ret;
}

/**
 * smi_i2c_probe - Instantiate multiple I2C devices from inst array
 * @pdev:	Platform device
 * @adev:	ACPI device
 * @smi:	Internal struct for Serial multi instantiate driver
 * @inst_array:	Array of instances to probe
 *
 * Returns the number of I2C devices instantiate, Zero if none is found or a negative error code.
 */
static int smi_i2c_probe(struct platform_device *pdev, struct acpi_device *adev, struct smi *smi,
			 const struct smi_instance *inst_array)
{
	struct i2c_board_info board_info = {};
	struct device *dev = &pdev->dev;
	char name[32];
	int i, ret, count;

	ret = i2c_acpi_client_count(adev);
	if (ret < 0)
		return ret;
	else if (!ret)
		return -ENODEV;

	count = ret;

	smi->i2c_devs = devm_kcalloc(dev, count, sizeof(*smi->i2c_devs), GFP_KERNEL);
	if (!smi->i2c_devs)
		return -ENOMEM;

	for (i = 0; i < count && inst_array[i].type; i++) {
		memset(&board_info, 0, sizeof(board_info));
		strscpy(board_info.type, inst_array[i].type, I2C_NAME_SIZE);
		snprintf(name, sizeof(name), "%s-%s.%d", dev_name(dev), inst_array[i].type, i);
		board_info.dev_name = name;

		ret = smi_get_irq(pdev, adev, &inst_array[i]);
		if (ret < 0)
			goto error;
		board_info.irq = ret;

		smi->i2c_devs[i] = i2c_acpi_new_device(dev, i, &board_info);
		if (IS_ERR(smi->i2c_devs[i])) {
			ret = dev_err_probe(dev, PTR_ERR(smi->i2c_devs[i]),
					    "Error creating i2c-client, idx %d\n", i);
			goto error;
		}
		smi->i2c_num++;
	}
	if (smi->i2c_num < count) {
		dev_dbg(dev, "Error finding driver, idx %d\n", i);
		ret = -ENODEV;
		goto error;
	}

	dev_info(dev, "Instantiated %d I2C devices.\n", smi->i2c_num);

	return 0;
error:
	smi_devs_unregister(smi);

	return ret;
}

static int smi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct smi_node *node;
	struct acpi_device *adev;
	struct smi *smi;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	node = device_get_match_data(dev);
	if (!node) {
		dev_dbg(dev, "Error ACPI match data is missing\n");
		return -ENODEV;
	}

	smi = devm_kzalloc(dev, sizeof(*smi), GFP_KERNEL);
	if (!smi)
		return -ENOMEM;

	platform_set_drvdata(pdev, smi);

	switch (node->bus_type) {
	case SMI_I2C:
		return smi_i2c_probe(pdev, adev, smi, node->instances);
	case SMI_SPI:
		return smi_spi_probe(pdev, adev, smi, node->instances);
	case SMI_AUTO_DETECT:
		if (i2c_acpi_client_count(adev) > 0)
			return smi_i2c_probe(pdev, adev, smi, node->instances);
		else
			return smi_spi_probe(pdev, adev, smi, node->instances);
	default:
		return -EINVAL;
	}

	return 0; /* never reached */
}

static int smi_remove(struct platform_device *pdev)
{
	struct smi *smi = platform_get_drvdata(pdev);

	smi_devs_unregister(smi);

	return 0;
}

static const struct smi_node bsg1160_data = {
	.instances = {
		{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
		{ "bmc150_magn" },
		{ "bmg160" },
		{}
	},
	.bus_type = SMI_I2C,
};

static const struct smi_node bsg2150_data = {
	.instances = {
		{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
		{ "bmc150_magn" },
		/* The resources describe a 3th client, but it is not really there. */
		{ "bsg2150_dummy_dev" },
		{}
	},
	.bus_type = SMI_I2C,
};

static const struct smi_node int3515_data = {
	.instances = {
		{ "tps6598x", IRQ_RESOURCE_APIC, 0 },
		{ "tps6598x", IRQ_RESOURCE_APIC, 1 },
		{ "tps6598x", IRQ_RESOURCE_APIC, 2 },
		{ "tps6598x", IRQ_RESOURCE_APIC, 3 },
		{}
	},
	.bus_type = SMI_I2C,
};

static const struct smi_node cs35l41_hda = {
	.instances = {
		{ "cs35l41-hda", IRQ_RESOURCE_GPIO, 0 },
		{ "cs35l41-hda", IRQ_RESOURCE_GPIO, 0 },
		{ "cs35l41-hda", IRQ_RESOURCE_GPIO, 0 },
		{ "cs35l41-hda", IRQ_RESOURCE_GPIO, 0 },
		{}
	},
	.bus_type = SMI_AUTO_DETECT,
};

/*
 * Note new device-ids must also be added to ignore_serial_bus_ids in
 * drivers/acpi/scan.c: acpi_device_enumeration_by_parent().
 */
static const struct acpi_device_id smi_acpi_ids[] = {
	{ "BSG1160", (unsigned long)&bsg1160_data },
	{ "BSG2150", (unsigned long)&bsg2150_data },
	{ "INT3515", (unsigned long)&int3515_data },
	{ "CSC3551", (unsigned long)&cs35l41_hda },
	/* Non-conforming _HID for Cirrus Logic already released */
	{ "CLSA0100", (unsigned long)&cs35l41_hda },
	{ "CLSA0101", (unsigned long)&cs35l41_hda },
	{ }
};
MODULE_DEVICE_TABLE(acpi, smi_acpi_ids);

static struct platform_driver smi_driver = {
	.driver	= {
		.name = "Serial bus multi instantiate pseudo device driver",
		.acpi_match_table = smi_acpi_ids,
	},
	.probe = smi_probe,
	.remove = smi_remove,
};
module_platform_driver(smi_driver);

MODULE_DESCRIPTION("Serial multi instantiate pseudo device driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
