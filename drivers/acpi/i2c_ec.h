/*
 * SMBus driver for ACPI Embedded Controller ($Revision: 1.2 $)
 *
 * Copyright (c) 2002, 2005 Ducrot Bruno
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 */

struct acpi_ec_smbus {
	struct i2c_adapter adapter;
	union acpi_ec *ec;
	int base;
	int alert;
};

struct acpi_ec_hc {
	acpi_handle handle;
	struct acpi_ec_smbus *smbus;
};

struct acpi_ec_hc *acpi_get_ec_hc(struct acpi_device *device);
