/*
 * intel_pmic.c - Intel PMIC operation region driver
 *
 * Copyright (C) 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/regmap.h>
#include "intel_pmic.h"

#define PMIC_POWER_OPREGION_ID		0x8d
#define PMIC_THERMAL_OPREGION_ID	0x8c

struct acpi_lpat {
	int temp;
	int raw;
};

struct intel_pmic_opregion {
	struct mutex lock;
	struct acpi_lpat *lpat;
	int lpat_count;
	struct regmap *regmap;
	struct intel_pmic_opregion_data *data;
};

static int pmic_get_reg_bit(int address, struct pmic_table *table,
			    int count, int *reg, int *bit)
{
	int i;

	for (i = 0; i < count; i++) {
		if (table[i].address == address) {
			*reg = table[i].reg;
			if (bit)
				*bit = table[i].bit;
			return 0;
		}
	}
	return -ENOENT;
}

/**
 * raw_to_temp(): Return temperature from raw value through LPAT table
 *
 * @lpat: the temperature_raw mapping table
 * @count: the count of the above mapping table
 * @raw: the raw value, used as a key to get the temerature from the
 *       above mapping table
 *
 * A positive value will be returned on success, a negative errno will
 * be returned in error cases.
 */
static int raw_to_temp(struct acpi_lpat *lpat, int count, int raw)
{
	int i, delta_temp, delta_raw, temp;

	for (i = 0; i < count - 1; i++) {
		if ((raw >= lpat[i].raw && raw <= lpat[i+1].raw) ||
		    (raw <= lpat[i].raw && raw >= lpat[i+1].raw))
			break;
	}

	if (i == count - 1)
		return -ENOENT;

	delta_temp = lpat[i+1].temp - lpat[i].temp;
	delta_raw = lpat[i+1].raw - lpat[i].raw;
	temp = lpat[i].temp + (raw - lpat[i].raw) * delta_temp / delta_raw;

	return temp;
}

/**
 * temp_to_raw(): Return raw value from temperature through LPAT table
 *
 * @lpat: the temperature_raw mapping table
 * @count: the count of the above mapping table
 * @temp: the temperature, used as a key to get the raw value from the
 *        above mapping table
 *
 * A positive value will be returned on success, a negative errno will
 * be returned in error cases.
 */
static int temp_to_raw(struct acpi_lpat *lpat, int count, int temp)
{
	int i, delta_temp, delta_raw, raw;

	for (i = 0; i < count - 1; i++) {
		if (temp >= lpat[i].temp && temp <= lpat[i+1].temp)
			break;
	}

	if (i == count - 1)
		return -ENOENT;

	delta_temp = lpat[i+1].temp - lpat[i].temp;
	delta_raw = lpat[i+1].raw - lpat[i].raw;
	raw = lpat[i].raw + (temp - lpat[i].temp) * delta_raw / delta_temp;

	return raw;
}

static void pmic_thermal_lpat(struct intel_pmic_opregion *opregion,
			      acpi_handle handle, struct device *dev)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj_p, *obj_e;
	int *lpat, i;
	acpi_status status;

	status = acpi_evaluate_object(handle, "LPAT", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return;

	obj_p = (union acpi_object *)buffer.pointer;
	if (!obj_p || (obj_p->type != ACPI_TYPE_PACKAGE) ||
	    (obj_p->package.count % 2) || (obj_p->package.count < 4))
		goto out;

	lpat = devm_kmalloc(dev, sizeof(int) * obj_p->package.count,
			    GFP_KERNEL);
	if (!lpat)
		goto out;

	for (i = 0; i < obj_p->package.count; i++) {
		obj_e = &obj_p->package.elements[i];
		if (obj_e->type != ACPI_TYPE_INTEGER) {
			devm_kfree(dev, lpat);
			goto out;
		}
		lpat[i] = (s64)obj_e->integer.value;
	}

	opregion->lpat = (struct acpi_lpat *)lpat;
	opregion->lpat_count = obj_p->package.count / 2;

out:
	kfree(buffer.pointer);
}

static acpi_status intel_pmic_power_handler(u32 function,
		acpi_physical_address address, u32 bits, u64 *value64,
		void *handler_context, void *region_context)
{
	struct intel_pmic_opregion *opregion = region_context;
	struct regmap *regmap = opregion->regmap;
	struct intel_pmic_opregion_data *d = opregion->data;
	int reg, bit, result;

	if (bits != 32 || !value64)
		return AE_BAD_PARAMETER;

	if (function == ACPI_WRITE && !(*value64 == 0 || *value64 == 1))
		return AE_BAD_PARAMETER;

	result = pmic_get_reg_bit(address, d->power_table,
				  d->power_table_count, &reg, &bit);
	if (result == -ENOENT)
		return AE_BAD_PARAMETER;

	mutex_lock(&opregion->lock);

	result = function == ACPI_READ ?
		d->get_power(regmap, reg, bit, value64) :
		d->update_power(regmap, reg, bit, *value64 == 1);

	mutex_unlock(&opregion->lock);

	return result ? AE_ERROR : AE_OK;
}

static int pmic_read_temp(struct intel_pmic_opregion *opregion,
			  int reg, u64 *value)
{
	int raw_temp, temp;

	if (!opregion->data->get_raw_temp)
		return -ENXIO;

	raw_temp = opregion->data->get_raw_temp(opregion->regmap, reg);
	if (raw_temp < 0)
		return raw_temp;

	if (!opregion->lpat) {
		*value = raw_temp;
		return 0;
	}

	temp = raw_to_temp(opregion->lpat, opregion->lpat_count, raw_temp);
	if (temp < 0)
		return temp;

	*value = temp;
	return 0;
}

static int pmic_thermal_temp(struct intel_pmic_opregion *opregion, int reg,
			     u32 function, u64 *value)
{
	return function == ACPI_READ ?
		pmic_read_temp(opregion, reg, value) : -EINVAL;
}

static int pmic_thermal_aux(struct intel_pmic_opregion *opregion, int reg,
			    u32 function, u64 *value)
{
	int raw_temp;

	if (function == ACPI_READ)
		return pmic_read_temp(opregion, reg, value);

	if (!opregion->data->update_aux)
		return -ENXIO;

	if (opregion->lpat) {
		raw_temp = temp_to_raw(opregion->lpat, opregion->lpat_count,
				       *value);
		if (raw_temp < 0)
			return raw_temp;
	} else {
		raw_temp = *value;
	}

	return opregion->data->update_aux(opregion->regmap, reg, raw_temp);
}

static int pmic_thermal_pen(struct intel_pmic_opregion *opregion, int reg,
			    u32 function, u64 *value)
{
	struct intel_pmic_opregion_data *d = opregion->data;
	struct regmap *regmap = opregion->regmap;

	if (!d->get_policy || !d->update_policy)
		return -ENXIO;

	if (function == ACPI_READ)
		return d->get_policy(regmap, reg, value);

	if (*value != 0 && *value != 1)
		return -EINVAL;

	return d->update_policy(regmap, reg, *value);
}

static bool pmic_thermal_is_temp(int address)
{
	return (address <= 0x3c) && !(address % 12);
}

static bool pmic_thermal_is_aux(int address)
{
	return (address >= 4 && address <= 0x40 && !((address - 4) % 12)) ||
	       (address >= 8 && address <= 0x44 && !((address - 8) % 12));
}

static bool pmic_thermal_is_pen(int address)
{
	return address >= 0x48 && address <= 0x5c;
}

static acpi_status intel_pmic_thermal_handler(u32 function,
		acpi_physical_address address, u32 bits, u64 *value64,
		void *handler_context, void *region_context)
{
	struct intel_pmic_opregion *opregion = region_context;
	struct intel_pmic_opregion_data *d = opregion->data;
	int reg, result;

	if (bits != 32 || !value64)
		return AE_BAD_PARAMETER;

	result = pmic_get_reg_bit(address, d->thermal_table,
				  d->thermal_table_count, &reg, NULL);
	if (result == -ENOENT)
		return AE_BAD_PARAMETER;

	mutex_lock(&opregion->lock);

	if (pmic_thermal_is_temp(address))
		result = pmic_thermal_temp(opregion, reg, function, value64);
	else if (pmic_thermal_is_aux(address))
		result = pmic_thermal_aux(opregion, reg, function, value64);
	else if (pmic_thermal_is_pen(address))
		result = pmic_thermal_pen(opregion, reg, function, value64);
	else
		result = -EINVAL;

	mutex_unlock(&opregion->lock);

	if (result < 0) {
		if (result == -EINVAL)
			return AE_BAD_PARAMETER;
		else
			return AE_ERROR;
	}

	return AE_OK;
}

int intel_pmic_install_opregion_handler(struct device *dev, acpi_handle handle,
					struct regmap *regmap,
					struct intel_pmic_opregion_data *d)
{
	acpi_status status;
	struct intel_pmic_opregion *opregion;

	if (!dev || !regmap || !d)
		return -EINVAL;

	if (!handle)
		return -ENODEV;

	opregion = devm_kzalloc(dev, sizeof(*opregion), GFP_KERNEL);
	if (!opregion)
		return -ENOMEM;

	mutex_init(&opregion->lock);
	opregion->regmap = regmap;
	pmic_thermal_lpat(opregion, handle, dev);

	status = acpi_install_address_space_handler(handle,
						    PMIC_POWER_OPREGION_ID,
						    intel_pmic_power_handler,
						    NULL, opregion);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	status = acpi_install_address_space_handler(handle,
						    PMIC_THERMAL_OPREGION_ID,
						    intel_pmic_thermal_handler,
						    NULL, opregion);
	if (ACPI_FAILURE(status)) {
		acpi_remove_address_space_handler(handle, PMIC_POWER_OPREGION_ID,
						  intel_pmic_power_handler);
		return -ENODEV;
	}

	opregion->data = d;
	return 0;
}
EXPORT_SYMBOL_GPL(intel_pmic_install_opregion_handler);

MODULE_LICENSE("GPL");
