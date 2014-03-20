/*
 *  Copyright 2013 Matthew Garrett <mjg59@srcf.ucam.org>
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/acpi.h>

MODULE_LICENSE("GPL");

static int smartconnect_acpi_init(struct acpi_device *acpi)
{
	unsigned long long value;
	acpi_status status;

	status = acpi_evaluate_integer(acpi->handle, "GAOS", NULL, &value);
	if (!ACPI_SUCCESS(status))
		return -EINVAL;

	if (value & 0x1) {
		dev_info(&acpi->dev, "Disabling Intel Smart Connect\n");
		status = acpi_execute_simple_method(acpi->handle, "SAOS", 0);
	}

	return 0;
}

static const struct acpi_device_id smartconnect_ids[] = {
	{"INT33A0", 0},
	{"", 0}
};

static struct acpi_driver smartconnect_driver = {
	.owner = THIS_MODULE,
	.name = "intel_smart_connect",
	.class = "intel_smart_connect",
	.ids = smartconnect_ids,
	.ops = {
		.add = smartconnect_acpi_init,
	},
};

module_acpi_driver(smartconnect_driver);

MODULE_DEVICE_TABLE(acpi, smartconnect_ids);
