// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_pmic.c - Intel PMIC operation region driver
 *
 * Copyright (C) 2014 Intel Corporation. All rights reserved.
 */

#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/regmap.h>
#include <acpi/acpi_lpat.h>
#include "intel_pmic.h"

#define PMIC_POWER_OPREGION_ID		0x8d
#define PMIC_THERMAL_OPREGION_ID	0x8c
#define PMIC_REGS_OPREGION_ID		0x8f

struct intel_pmic_regs_handler_ctx {
	unsigned int val;
	u16 addr;
};

struct intel_pmic_opregion {
	struct mutex lock;
	struct acpi_lpat_conversion_table *lpat_table;
	struct regmap *regmap;
	struct intel_pmic_opregion_data *data;
	struct intel_pmic_regs_handler_ctx ctx;
};

static struct intel_pmic_opregion *intel_pmic_opregion;

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

	if (!opregion->lpat_table) {
		*value = raw_temp;
		return 0;
	}

	temp = acpi_lpat_raw_to_temp(opregion->lpat_table, raw_temp);
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

	if (opregion->lpat_table) {
		raw_temp = acpi_lpat_temp_to_raw(opregion->lpat_table, *value);
		if (raw_temp < 0)
			return raw_temp;
	} else {
		raw_temp = *value;
	}

	return opregion->data->update_aux(opregion->regmap, reg, raw_temp);
}

static int pmic_thermal_pen(struct intel_pmic_opregion *opregion, int reg,
			    int bit, u32 function, u64 *value)
{
	struct intel_pmic_opregion_data *d = opregion->data;
	struct regmap *regmap = opregion->regmap;

	if (!d->get_policy || !d->update_policy)
		return -ENXIO;

	if (function == ACPI_READ)
		return d->get_policy(regmap, reg, bit, value);

	if (*value != 0 && *value != 1)
		return -EINVAL;

	return d->update_policy(regmap, reg, bit, *value);
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
	int reg, bit, result;

	if (bits != 32 || !value64)
		return AE_BAD_PARAMETER;

	result = pmic_get_reg_bit(address, d->thermal_table,
				  d->thermal_table_count, &reg, &bit);
	if (result == -ENOENT)
		return AE_BAD_PARAMETER;

	mutex_lock(&opregion->lock);

	if (pmic_thermal_is_temp(address))
		result = pmic_thermal_temp(opregion, reg, function, value64);
	else if (pmic_thermal_is_aux(address))
		result = pmic_thermal_aux(opregion, reg, function, value64);
	else if (pmic_thermal_is_pen(address))
		result = pmic_thermal_pen(opregion, reg, bit,
						function, value64);
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

static acpi_status intel_pmic_regs_handler(u32 function,
		acpi_physical_address address, u32 bits, u64 *value64,
		void *handler_context, void *region_context)
{
	struct intel_pmic_opregion *opregion = region_context;
	int result = 0;

	switch (address) {
	case 0:
		return AE_OK;
	case 1:
		opregion->ctx.addr |= (*value64 & 0xff) << 8;
		return AE_OK;
	case 2:
		opregion->ctx.addr |= *value64 & 0xff;
		return AE_OK;
	case 3:
		opregion->ctx.val = *value64 & 0xff;
		return AE_OK;
	case 4:
		if (*value64) {
			result = regmap_write(opregion->regmap, opregion->ctx.addr,
					      opregion->ctx.val);
		} else {
			result = regmap_read(opregion->regmap, opregion->ctx.addr,
					     &opregion->ctx.val);
			if (result == 0)
				*value64 = opregion->ctx.val;
		}
		memset(&opregion->ctx, 0x00, sizeof(opregion->ctx));
	}

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
	int ret;

	if (!dev || !regmap || !d)
		return -EINVAL;

	if (!handle)
		return -ENODEV;

	opregion = devm_kzalloc(dev, sizeof(*opregion), GFP_KERNEL);
	if (!opregion)
		return -ENOMEM;

	mutex_init(&opregion->lock);
	opregion->regmap = regmap;
	opregion->lpat_table = acpi_lpat_get_conversion_table(handle);

	status = acpi_install_address_space_handler(handle,
						    PMIC_POWER_OPREGION_ID,
						    intel_pmic_power_handler,
						    NULL, opregion);
	if (ACPI_FAILURE(status)) {
		ret = -ENODEV;
		goto out_error;
	}

	status = acpi_install_address_space_handler(handle,
						    PMIC_THERMAL_OPREGION_ID,
						    intel_pmic_thermal_handler,
						    NULL, opregion);
	if (ACPI_FAILURE(status)) {
		ret = -ENODEV;
		goto out_remove_power_handler;
	}

	status = acpi_install_address_space_handler(handle,
			PMIC_REGS_OPREGION_ID, intel_pmic_regs_handler, NULL,
			opregion);
	if (ACPI_FAILURE(status)) {
		ret = -ENODEV;
		goto out_remove_thermal_handler;
	}

	opregion->data = d;
	intel_pmic_opregion = opregion;
	return 0;

out_remove_thermal_handler:
	acpi_remove_address_space_handler(handle, PMIC_THERMAL_OPREGION_ID,
					  intel_pmic_thermal_handler);

out_remove_power_handler:
	acpi_remove_address_space_handler(handle, PMIC_POWER_OPREGION_ID,
					  intel_pmic_power_handler);

out_error:
	acpi_lpat_free_conversion_table(opregion->lpat_table);
	return ret;
}
EXPORT_SYMBOL_GPL(intel_pmic_install_opregion_handler);

/**
 * intel_soc_pmic_exec_mipi_pmic_seq_element - Execute PMIC MIPI sequence
 * @i2c_address:  I2C client address for the PMIC
 * @reg_address:  PMIC register address
 * @value:        New value for the register bits to change
 * @mask:         Mask indicating which register bits to change
 *
 * DSI LCD panels describe an initialization sequence in the i915 VBT (Video
 * BIOS Tables) using so called MIPI sequences. One possible element in these
 * sequences is a PMIC specific element of 15 bytes.
 *
 * This function executes these PMIC specific elements sending the embedded
 * commands to the PMIC.
 *
 * Return 0 on success, < 0 on failure.
 */
int intel_soc_pmic_exec_mipi_pmic_seq_element(u16 i2c_address, u32 reg_address,
					      u32 value, u32 mask)
{
	struct intel_pmic_opregion_data *d;
	int ret;

	if (!intel_pmic_opregion) {
		pr_warn("%s: No PMIC registered\n", __func__);
		return -ENXIO;
	}

	d = intel_pmic_opregion->data;

	mutex_lock(&intel_pmic_opregion->lock);

	if (d->exec_mipi_pmic_seq_element) {
		ret = d->exec_mipi_pmic_seq_element(intel_pmic_opregion->regmap,
						    i2c_address, reg_address,
						    value, mask);
	} else if (d->pmic_i2c_address) {
		if (i2c_address == d->pmic_i2c_address) {
			ret = regmap_update_bits(intel_pmic_opregion->regmap,
						 reg_address, mask, value);
		} else {
			pr_err("%s: Unexpected i2c-addr: 0x%02x (reg-addr 0x%x value 0x%x mask 0x%x)\n",
			       __func__, i2c_address, reg_address, value, mask);
			ret = -ENXIO;
		}
	} else {
		pr_warn("%s: Not implemented\n", __func__);
		pr_warn("%s: i2c-addr: 0x%x reg-addr 0x%x value 0x%x mask 0x%x\n",
			__func__, i2c_address, reg_address, value, mask);
		ret = -EOPNOTSUPP;
	}

	mutex_unlock(&intel_pmic_opregion->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(intel_soc_pmic_exec_mipi_pmic_seq_element);
