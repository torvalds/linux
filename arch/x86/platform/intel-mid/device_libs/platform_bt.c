// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bluetooth platform data initialization file
 *
 * (C) Copyright 2017 Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/gpio/machine.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/intel-mid.h>

struct bt_sfi_data {
	struct device *dev;
	const char *name;
	int (*setup)(struct bt_sfi_data *ddata);
};

static struct gpiod_lookup_table tng_bt_sfi_gpio_table = {
	.dev_id	= "hci_bcm",
	.table	= {
		GPIO_LOOKUP("0000:00:0c.0", -1, "device-wakeup", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("0000:00:0c.0", -1, "shutdown",      GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("0000:00:0c.0", -1, "host-wakeup",   GPIO_ACTIVE_HIGH),
		{ },
	},
};

#define TNG_BT_SFI_GPIO_DEVICE_WAKEUP	"bt_wakeup"
#define TNG_BT_SFI_GPIO_SHUTDOWN	"BT-reset"
#define TNG_BT_SFI_GPIO_HOST_WAKEUP	"bt_uart_enable"

static int __init tng_bt_sfi_setup(struct bt_sfi_data *ddata)
{
	struct gpiod_lookup_table *table = &tng_bt_sfi_gpio_table;
	struct gpiod_lookup *lookup = table->table;
	struct pci_dev *pdev;

	/* Connected to /dev/ttyS0 */
	pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(4, 1));
	if (!pdev)
		return -ENODEV;

	ddata->dev = &pdev->dev;
	ddata->name = table->dev_id;

	lookup[0].chip_hwnum = get_gpio_by_name(TNG_BT_SFI_GPIO_DEVICE_WAKEUP);
	lookup[1].chip_hwnum = get_gpio_by_name(TNG_BT_SFI_GPIO_SHUTDOWN);
	lookup[2].chip_hwnum = get_gpio_by_name(TNG_BT_SFI_GPIO_HOST_WAKEUP);

	gpiod_add_lookup_table(table);
	return 0;
}

static struct bt_sfi_data tng_bt_sfi_data __initdata = {
	.setup	= tng_bt_sfi_setup,
};

static const struct x86_cpu_id bt_sfi_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_SILVERMONT_MID,	&tng_bt_sfi_data),
	{}
};

static int __init bt_sfi_init(void)
{
	struct platform_device_info info;
	struct platform_device *pdev;
	const struct x86_cpu_id *id;
	struct bt_sfi_data *ddata;
	int ret;

	id = x86_match_cpu(bt_sfi_cpu_ids);
	if (!id)
		return -ENODEV;

	ddata = (struct bt_sfi_data *)id->driver_data;
	if (!ddata)
		return -ENODEV;

	ret = ddata->setup(ddata);
	if (ret)
		return ret;

	memset(&info, 0, sizeof(info));
	info.fwnode	= ddata->dev->fwnode;
	info.parent	= ddata->dev;
	info.name	= ddata->name;
	info.id		= PLATFORM_DEVID_NONE;

	pdev = platform_device_register_full(&info);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	dev_info(ddata->dev, "Registered Bluetooth device: %s\n", ddata->name);
	return 0;
}
device_initcall(bt_sfi_init);
