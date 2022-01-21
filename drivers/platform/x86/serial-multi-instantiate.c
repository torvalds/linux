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
#include <linux/types.h>

#define IRQ_RESOURCE_TYPE	GENMASK(1, 0)
#define IRQ_RESOURCE_NONE	0
#define IRQ_RESOURCE_GPIO	1
#define IRQ_RESOURCE_APIC	2

struct smi_instance {
	const char *type;
	unsigned int flags;
	int irq_idx;
};

struct smi {
	int i2c_num;
	struct i2c_client **i2c_devs;
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
	const struct smi_instance *inst_array;
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	struct smi *smi;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	inst_array = device_get_match_data(dev);
	if (!inst_array) {
		dev_dbg(dev, "Error ACPI match data is missing\n");
		return -ENODEV;
	}

	smi = devm_kzalloc(dev, sizeof(*smi), GFP_KERNEL);
	if (!smi)
		return -ENOMEM;

	platform_set_drvdata(pdev, smi);

	return smi_i2c_probe(pdev, adev, smi, inst_array);
}

static int smi_remove(struct platform_device *pdev)
{
	struct smi *smi = platform_get_drvdata(pdev);

	smi_devs_unregister(smi);

	return 0;
}

static const struct smi_instance bsg1160_data[]  = {
	{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
	{ "bmc150_magn" },
	{ "bmg160" },
	{}
};

static const struct smi_instance bsg2150_data[]  = {
	{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
	{ "bmc150_magn" },
	/* The resources describe a 3th client, but it is not really there. */
	{ "bsg2150_dummy_dev" },
	{}
};

static const struct smi_instance int3515_data[]  = {
	{ "tps6598x", IRQ_RESOURCE_APIC, 0 },
	{ "tps6598x", IRQ_RESOURCE_APIC, 1 },
	{ "tps6598x", IRQ_RESOURCE_APIC, 2 },
	{ "tps6598x", IRQ_RESOURCE_APIC, 3 },
	{}
};

/*
 * Note new device-ids must also be added to ignore_serial_bus_ids in
 * drivers/acpi/scan.c: acpi_device_enumeration_by_parent().
 */
static const struct acpi_device_id smi_acpi_ids[] = {
	{ "BSG1160", (unsigned long)bsg1160_data },
	{ "BSG2150", (unsigned long)bsg2150_data },
	{ "INT3515", (unsigned long)int3515_data },
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
