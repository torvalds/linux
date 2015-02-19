/*
 *  pvpanic.c - pvpanic Device Support
 *
 *  Copyright (C) 2013 Fujitsu.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/acpi.h>

MODULE_AUTHOR("Hu Tao <hutao@cn.fujitsu.com>");
MODULE_DESCRIPTION("pvpanic device driver");
MODULE_LICENSE("GPL");

static int pvpanic_add(struct acpi_device *device);
static int pvpanic_remove(struct acpi_device *device);

static const struct acpi_device_id pvpanic_device_ids[] = {
	{ "QEMU0001", 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, pvpanic_device_ids);

#define PVPANIC_PANICKED	(1 << 0)

static u16 port;

static struct acpi_driver pvpanic_driver = {
	.name =		"pvpanic",
	.class =	"QEMU",
	.ids =		pvpanic_device_ids,
	.ops =		{
				.add =		pvpanic_add,
				.remove =	pvpanic_remove,
			},
	.owner =	THIS_MODULE,
};

static void
pvpanic_send_event(unsigned int event)
{
	outb(event, port);
}

static int
pvpanic_panic_notify(struct notifier_block *nb, unsigned long code,
		     void *unused)
{
	pvpanic_send_event(PVPANIC_PANICKED);
	return NOTIFY_DONE;
}

static struct notifier_block pvpanic_panic_nb = {
	.notifier_call = pvpanic_panic_notify,
	.priority = 1, /* let this called before broken drm_fb_helper */
};


static acpi_status
pvpanic_walk_resources(struct acpi_resource *res, void *context)
{
	switch (res->type) {
	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;

	case ACPI_RESOURCE_TYPE_IO:
		port = res->data.io.minimum;
		return AE_OK;

	default:
		return AE_ERROR;
	}
}

static int pvpanic_add(struct acpi_device *device)
{
	acpi_status status;
	u64 ret;

	status = acpi_evaluate_integer(device->handle, "_STA", NULL,
				       &ret);

	if (ACPI_FAILURE(status) || (ret & 0x0B) != 0x0B)
		return -ENODEV;

	acpi_walk_resources(device->handle, METHOD_NAME__CRS,
			    pvpanic_walk_resources, NULL);

	if (!port)
		return -ENODEV;

	atomic_notifier_chain_register(&panic_notifier_list,
				       &pvpanic_panic_nb);

	return 0;
}

static int pvpanic_remove(struct acpi_device *device)
{

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &pvpanic_panic_nb);
	return 0;
}

module_acpi_driver(pvpanic_driver);
