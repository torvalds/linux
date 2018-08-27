// SPDX-License-Identifier: GPL-2.0+
/*
 * I2C multi-instantiate driver, pseudo driver to instantiate multiple
 * i2c-clients from a single fwnode.
 *
 * Copyright 2018 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct i2c_inst_data {
	const char *type;
	int gpio_irq_idx;
};

struct i2c_multi_inst_data {
	int num_clients;
	struct i2c_client *clients[0];
};

static int i2c_multi_inst_probe(struct platform_device *pdev)
{
	struct i2c_multi_inst_data *multi;
	const struct acpi_device_id *match;
	const struct i2c_inst_data *inst_data;
	struct i2c_board_info board_info = {};
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	char name[32];
	int i, ret;

	match = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!match) {
		dev_err(dev, "Error ACPI match data is missing\n");
		return -ENODEV;
	}
	inst_data = (const struct i2c_inst_data *)match->driver_data;

	adev = ACPI_COMPANION(dev);

	/* Count number of clients to instantiate */
	for (i = 0; inst_data[i].type; i++) {}

	multi = devm_kmalloc(dev,
			offsetof(struct i2c_multi_inst_data, clients[i]),
			GFP_KERNEL);
	if (!multi)
		return -ENOMEM;

	multi->num_clients = i;

	for (i = 0; i < multi->num_clients; i++) {
		memset(&board_info, 0, sizeof(board_info));
		strlcpy(board_info.type, inst_data[i].type, I2C_NAME_SIZE);
		snprintf(name, sizeof(name), "%s-%s", match->id,
			 inst_data[i].type);
		board_info.dev_name = name;
		board_info.irq = 0;
		if (inst_data[i].gpio_irq_idx != -1) {
			ret = acpi_dev_gpio_irq_get(adev,
						    inst_data[i].gpio_irq_idx);
			if (ret < 0) {
				dev_err(dev, "Error requesting irq at index %d: %d\n",
					inst_data[i].gpio_irq_idx, ret);
				goto error;
			}
			board_info.irq = ret;
		}
		multi->clients[i] = i2c_acpi_new_device(dev, i, &board_info);
		if (!multi->clients[i]) {
			dev_err(dev, "Error creating i2c-client, idx %d\n", i);
			ret = -ENODEV;
			goto error;
		}
	}

	platform_set_drvdata(pdev, multi);
	return 0;

error:
	while (--i >= 0)
		i2c_unregister_device(multi->clients[i]);

	return ret;
}

static int i2c_multi_inst_remove(struct platform_device *pdev)
{
	struct i2c_multi_inst_data *multi = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < multi->num_clients; i++)
		i2c_unregister_device(multi->clients[i]);

	return 0;
}

static const struct i2c_inst_data bsg1160_data[]  = {
	{ "bmc150_accel", 0 },
	{ "bmc150_magn", -1 },
	{ "bmg160", -1 },
	{}
};

/*
 * Note new device-ids must also be added to i2c_multi_instantiate_ids in
 * drivers/acpi/scan.c: acpi_device_enumeration_by_parent().
 */
static const struct acpi_device_id i2c_multi_inst_acpi_ids[] = {
	{ "BSG1160", (unsigned long)bsg1160_data },
	{ }
};
MODULE_DEVICE_TABLE(acpi, i2c_multi_inst_acpi_ids);

static struct platform_driver i2c_multi_inst_driver = {
	.driver	= {
		.name = "I2C multi instantiate pseudo device driver",
		.acpi_match_table = ACPI_PTR(i2c_multi_inst_acpi_ids),
	},
	.probe = i2c_multi_inst_probe,
	.remove = i2c_multi_inst_remove,
};
module_platform_driver(i2c_multi_inst_driver);

MODULE_DESCRIPTION("I2C multi instantiate pseudo device driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
