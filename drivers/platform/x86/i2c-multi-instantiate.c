// SPDX-License-Identifier: GPL-2.0+
/*
 * I2C multi-instantiate driver, pseudo driver to instantiate multiple
 * i2c-clients from a single fwnode.
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
#include <linux/types.h>

#define IRQ_RESOURCE_TYPE	GENMASK(1, 0)
#define IRQ_RESOURCE_NONE	0
#define IRQ_RESOURCE_GPIO	1
#define IRQ_RESOURCE_APIC	2

struct i2c_inst_data {
	const char *type;
	unsigned int flags;
	int irq_idx;
};

struct i2c_multi_inst_data {
	int num_clients;
	struct i2c_client *clients[];
};

static int i2c_multi_inst_count(struct acpi_resource *ares, void *data)
{
	struct acpi_resource_i2c_serialbus *sb;
	int *count = data;

	if (i2c_acpi_get_i2c_resource(ares, &sb))
		*count = *count + 1;

	return 1;
}

static int i2c_multi_inst_count_resources(struct acpi_device *adev)
{
	LIST_HEAD(r);
	int count = 0;
	int ret;

	ret = acpi_dev_get_resources(adev, &r, i2c_multi_inst_count, &count);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&r);
	return count;
}

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
	ret = i2c_multi_inst_count_resources(adev);
	if (ret < 0)
		return ret;

	multi = devm_kmalloc(dev, struct_size(multi, clients, ret), GFP_KERNEL);
	if (!multi)
		return -ENOMEM;

	multi->num_clients = ret;

	for (i = 0; i < multi->num_clients && inst_data[i].type; i++) {
		memset(&board_info, 0, sizeof(board_info));
		strlcpy(board_info.type, inst_data[i].type, I2C_NAME_SIZE);
		snprintf(name, sizeof(name), "%s-%s.%d", dev_name(dev),
			 inst_data[i].type, i);
		board_info.dev_name = name;
		switch (inst_data[i].flags & IRQ_RESOURCE_TYPE) {
		case IRQ_RESOURCE_GPIO:
			ret = acpi_dev_gpio_irq_get(adev, inst_data[i].irq_idx);
			if (ret < 0) {
				dev_err(dev, "Error requesting irq at index %d: %d\n",
					inst_data[i].irq_idx, ret);
				goto error;
			}
			board_info.irq = ret;
			break;
		case IRQ_RESOURCE_APIC:
			ret = platform_get_irq(pdev, inst_data[i].irq_idx);
			if (ret < 0) {
				dev_dbg(dev, "Error requesting irq at index %d: %d\n",
					inst_data[i].irq_idx, ret);
				goto error;
			}
			board_info.irq = ret;
			break;
		default:
			board_info.irq = 0;
			break;
		}
		multi->clients[i] = i2c_acpi_new_device(dev, i, &board_info);
		if (IS_ERR(multi->clients[i])) {
			ret = PTR_ERR(multi->clients[i]);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Error creating i2c-client, idx %d\n", i);
			goto error;
		}
	}
	if (i < multi->num_clients) {
		dev_err(dev, "Error finding driver, idx %d\n", i);
		ret = -ENODEV;
		goto error;
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
	{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
	{ "bmc150_magn" },
	{ "bmg160" },
	{}
};

static const struct i2c_inst_data bsg2150_data[]  = {
	{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
	{ "bmc150_magn" },
	/* The resources describe a 3th client, but it is not really there. */
	{ "bsg2150_dummy_dev" },
	{}
};

/*
 * Device with _HID INT3515 (TI PD controllers) has some unresolved interrupt
 * issues. The most common problem seen is interrupt flood.
 *
 * There are at least two known causes. Firstly, on some boards, the
 * I2CSerialBus resource index does not match the Interrupt resource, i.e. they
 * are not one-to-one mapped like in the array below. Secondly, on some boards
 * the IRQ line from the PD controller is not actually connected at all. But the
 * interrupt flood is also seen on some boards where those are not a problem, so
 * there are some other problems as well.
 *
 * Because of the issues with the interrupt, the device is disabled for now. If
 * you wish to debug the issues, uncomment the below, and add an entry for the
 * INT3515 device to the i2c_multi_instance_ids table.
 *
 * static const struct i2c_inst_data int3515_data[]  = {
 *	{ "tps6598x", IRQ_RESOURCE_APIC, 0 },
 *	{ "tps6598x", IRQ_RESOURCE_APIC, 1 },
 *	{ "tps6598x", IRQ_RESOURCE_APIC, 2 },
 *	{ "tps6598x", IRQ_RESOURCE_APIC, 3 },
 *	{ }
 * };
 */

/*
 * Note new device-ids must also be added to i2c_multi_instantiate_ids in
 * drivers/acpi/scan.c: acpi_device_enumeration_by_parent().
 */
static const struct acpi_device_id i2c_multi_inst_acpi_ids[] = {
	{ "BSG1160", (unsigned long)bsg1160_data },
	{ "BSG2150", (unsigned long)bsg2150_data },
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
