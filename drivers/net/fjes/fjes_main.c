/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015 FUJITSU LIMITED
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/nls.h>
#include <linux/platform_device.h>

#include "fjes.h"

#define MAJ 1
#define MIN 0
#define DRV_VERSION __stringify(MAJ) "." __stringify(MIN)
#define DRV_NAME	"fjes"
char fjes_driver_name[] = DRV_NAME;
char fjes_driver_version[] = DRV_VERSION;
static const char fjes_driver_string[] =
		"FUJITSU Extended Socket Network Device Driver";
static const char fjes_copyright[] =
		"Copyright (c) 2015 FUJITSU LIMITED";

MODULE_AUTHOR("Taku Izumi <izumi.taku@jp.fujitsu.com>");
MODULE_DESCRIPTION("FUJITSU Extended Socket Network Device Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static int fjes_acpi_add(struct acpi_device *);
static int fjes_acpi_remove(struct acpi_device *);
static acpi_status fjes_get_acpi_resource(struct acpi_resource *, void*);

static int fjes_probe(struct platform_device *);
static int fjes_remove(struct platform_device *);

static const struct acpi_device_id fjes_acpi_ids[] = {
	{"PNP0C02", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, fjes_acpi_ids);

static struct acpi_driver fjes_acpi_driver = {
	.name = DRV_NAME,
	.class = DRV_NAME,
	.owner = THIS_MODULE,
	.ids = fjes_acpi_ids,
	.ops = {
		.add = fjes_acpi_add,
		.remove = fjes_acpi_remove,
	},
};

static struct platform_driver fjes_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = fjes_probe,
	.remove = fjes_remove,
};

static struct resource fjes_resource[] = {
	{
		.flags = IORESOURCE_MEM,
		.start = 0,
		.end = 0,
	},
	{
		.flags = IORESOURCE_IRQ,
		.start = 0,
		.end = 0,
	},
};

static int fjes_acpi_add(struct acpi_device *device)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL};
	char str_buf[sizeof(FJES_ACPI_SYMBOL) + 1];
	struct platform_device *plat_dev;
	union acpi_object *str;
	acpi_status status;
	int result;

	status = acpi_evaluate_object(device->handle, "_STR", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	str = buffer.pointer;
	result = utf16s_to_utf8s((wchar_t *)str->string.pointer,
				 str->string.length, UTF16_LITTLE_ENDIAN,
				 str_buf, sizeof(str_buf) - 1);
	str_buf[result] = 0;

	if (strncmp(FJES_ACPI_SYMBOL, str_buf, strlen(FJES_ACPI_SYMBOL)) != 0) {
		kfree(buffer.pointer);
		return -ENODEV;
	}
	kfree(buffer.pointer);

	status = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
				     fjes_get_acpi_resource, fjes_resource);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	/* create platform_device */
	plat_dev = platform_device_register_simple(DRV_NAME, 0, fjes_resource,
						   ARRAY_SIZE(fjes_resource));
	device->driver_data = plat_dev;

	return 0;
}

static int fjes_acpi_remove(struct acpi_device *device)
{
	struct platform_device *plat_dev;

	plat_dev = (struct platform_device *)acpi_driver_data(device);
	platform_device_unregister(plat_dev);

	return 0;
}

static acpi_status
fjes_get_acpi_resource(struct acpi_resource *acpi_res, void *data)
{
	struct acpi_resource_address32 *addr;
	struct acpi_resource_irq *irq;
	struct resource *res = data;

	switch (acpi_res->type) {
	case ACPI_RESOURCE_TYPE_ADDRESS32:
		addr = &acpi_res->data.address32;
		res[0].start = addr->address.minimum;
		res[0].end = addr->address.minimum +
			addr->address.address_length - 1;
		break;

	case ACPI_RESOURCE_TYPE_IRQ:
		irq = &acpi_res->data.irq;
		if (irq->interrupt_count != 1)
			return AE_ERROR;
		res[1].start = irq->interrupts[0];
		res[1].end = irq->interrupts[0];
		break;

	default:
		break;
	}

	return AE_OK;
}

/* fjes_probe - Device Initialization Routine */
static int fjes_probe(struct platform_device *plat_dev)
{
	return 0;
}

/* fjes_remove - Device Removal Routine */
static int fjes_remove(struct platform_device *plat_dev)
{
	return 0;
}

/* fjes_init_module - Driver Registration Routine */
static int __init fjes_init_module(void)
{
	int result;

	pr_info("%s - version %s - %s\n",
		fjes_driver_string, fjes_driver_version, fjes_copyright);

	result = platform_driver_register(&fjes_driver);
	if (result < 0)
		return result;

	result = acpi_bus_register_driver(&fjes_acpi_driver);
	if (result < 0)
		goto fail_acpi_driver;

	return 0;

fail_acpi_driver:
	platform_driver_unregister(&fjes_driver);
	return result;
}

module_init(fjes_init_module);

/* fjes_exit_module - Driver Exit Cleanup Routine */
static void __exit fjes_exit_module(void)
{
	acpi_bus_unregister_driver(&fjes_acpi_driver);
	platform_driver_unregister(&fjes_driver);
}

module_exit(fjes_exit_module);
