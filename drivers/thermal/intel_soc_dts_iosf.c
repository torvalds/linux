/*
 * intel_soc_dts_iosf.c
 * Copyright (c) 2015, Intel Corporation.
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <asm/iosf_mbi.h>
#include "intel_soc_dts_iosf.h"

#define SOC_DTS_OFFSET_ENABLE		0xB0
#define SOC_DTS_OFFSET_TEMP		0xB1

#define SOC_DTS_OFFSET_PTPS		0xB2
#define SOC_DTS_OFFSET_PTTS		0xB3
#define SOC_DTS_OFFSET_PTTSS		0xB4
#define SOC_DTS_OFFSET_PTMC		0x80
#define SOC_DTS_TE_AUX0			0xB5
#define SOC_DTS_TE_AUX1			0xB6

#define SOC_DTS_AUX0_ENABLE_BIT		BIT(0)
#define SOC_DTS_AUX1_ENABLE_BIT		BIT(1)
#define SOC_DTS_CPU_MODULE0_ENABLE_BIT	BIT(16)
#define SOC_DTS_CPU_MODULE1_ENABLE_BIT	BIT(17)
#define SOC_DTS_TE_SCI_ENABLE		BIT(9)
#define SOC_DTS_TE_SMI_ENABLE		BIT(10)
#define SOC_DTS_TE_MSI_ENABLE		BIT(11)
#define SOC_DTS_TE_APICA_ENABLE		BIT(14)
#define SOC_DTS_PTMC_APIC_DEASSERT_BIT	BIT(4)

/* DTS encoding for TJ MAX temperature */
#define SOC_DTS_TJMAX_ENCODING		0x7F

/* Only 2 out of 4 is allowed for OSPM */
#define SOC_MAX_DTS_TRIPS		2

/* Mask for two trips in status bits */
#define SOC_DTS_TRIP_MASK		0x03

/* DTS0 and DTS 1 */
#define SOC_MAX_DTS_SENSORS		2

static int get_tj_max(u32 *tj_max)
{
	u32 eax, edx;
	u32 val;
	int err;

	err = rdmsr_safe(MSR_IA32_TEMPERATURE_TARGET, &eax, &edx);
	if (err)
		goto err_ret;
	else {
		val = (eax >> 16) & 0xff;
		if (val)
			*tj_max = val * 1000;
		else {
			err = -EINVAL;
			goto err_ret;
		}
	}

	return 0;
err_ret:
	*tj_max = 0;

	return err;
}

static int sys_get_trip_temp(struct thermal_zone_device *tzd, int trip,
			     int *temp)
{
	int status;
	u32 out;
	struct intel_soc_dts_sensor_entry *dts;
	struct intel_soc_dts_sensors *sensors;

	dts = tzd->devdata;
	sensors = dts->sensors;
	mutex_lock(&sensors->dts_update_lock);
	status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			       SOC_DTS_OFFSET_PTPS, &out);
	mutex_unlock(&sensors->dts_update_lock);
	if (status)
		return status;

	out = (out >> (trip * 8)) & SOC_DTS_TJMAX_ENCODING;
	if (!out)
		*temp = 0;
	else
		*temp = sensors->tj_max - out * 1000;

	return 0;
}

static int update_trip_temp(struct intel_soc_dts_sensor_entry *dts,
			    int thres_index, int temp,
			    enum thermal_trip_type trip_type)
{
	int status;
	u32 temp_out;
	u32 out;
	u32 store_ptps;
	u32 store_ptmc;
	u32 store_te_out;
	u32 te_out;
	u32 int_enable_bit = SOC_DTS_TE_APICA_ENABLE;
	struct intel_soc_dts_sensors *sensors = dts->sensors;

	if (sensors->intr_type == INTEL_SOC_DTS_INTERRUPT_MSI)
		int_enable_bit |= SOC_DTS_TE_MSI_ENABLE;

	temp_out = (sensors->tj_max - temp) / 1000;

	status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			       SOC_DTS_OFFSET_PTPS, &store_ptps);
	if (status)
		return status;

	out = (store_ptps & ~(0xFF << (thres_index * 8)));
	out |= (temp_out & 0xFF) << (thres_index * 8);
	status = iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
				SOC_DTS_OFFSET_PTPS, out);
	if (status)
		return status;

	pr_debug("update_trip_temp PTPS = %x\n", out);
	status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			       SOC_DTS_OFFSET_PTMC, &out);
	if (status)
		goto err_restore_ptps;

	store_ptmc = out;

	status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			       SOC_DTS_TE_AUX0 + thres_index,
			       &te_out);
	if (status)
		goto err_restore_ptmc;

	store_te_out = te_out;
	/* Enable for CPU module 0 and module 1 */
	out |= (SOC_DTS_CPU_MODULE0_ENABLE_BIT |
					SOC_DTS_CPU_MODULE1_ENABLE_BIT);
	if (temp) {
		if (thres_index)
			out |= SOC_DTS_AUX1_ENABLE_BIT;
		else
			out |= SOC_DTS_AUX0_ENABLE_BIT;
		te_out |= int_enable_bit;
	} else {
		if (thres_index)
			out &= ~SOC_DTS_AUX1_ENABLE_BIT;
		else
			out &= ~SOC_DTS_AUX0_ENABLE_BIT;
		te_out &= ~int_enable_bit;
	}
	status = iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
				SOC_DTS_OFFSET_PTMC, out);
	if (status)
		goto err_restore_te_out;

	status = iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
				SOC_DTS_TE_AUX0 + thres_index,
				te_out);
	if (status)
		goto err_restore_te_out;

	dts->trip_types[thres_index] = trip_type;

	return 0;
err_restore_te_out:
	iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
		       SOC_DTS_OFFSET_PTMC, store_te_out);
err_restore_ptmc:
	iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
		       SOC_DTS_OFFSET_PTMC, store_ptmc);
err_restore_ptps:
	iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
		       SOC_DTS_OFFSET_PTPS, store_ptps);
	/* Nothing we can do if restore fails */

	return status;
}

static int sys_set_trip_temp(struct thermal_zone_device *tzd, int trip,
			     int temp)
{
	struct intel_soc_dts_sensor_entry *dts = tzd->devdata;
	struct intel_soc_dts_sensors *sensors = dts->sensors;
	int status;

	if (temp > sensors->tj_max)
		return -EINVAL;

	mutex_lock(&sensors->dts_update_lock);
	status = update_trip_temp(tzd->devdata, trip, temp,
				  dts->trip_types[trip]);
	mutex_unlock(&sensors->dts_update_lock);

	return status;
}

static int sys_get_trip_type(struct thermal_zone_device *tzd,
			     int trip, enum thermal_trip_type *type)
{
	struct intel_soc_dts_sensor_entry *dts;

	dts = tzd->devdata;

	*type = dts->trip_types[trip];

	return 0;
}

static int sys_get_curr_temp(struct thermal_zone_device *tzd,
			     int *temp)
{
	int status;
	u32 out;
	struct intel_soc_dts_sensor_entry *dts;
	struct intel_soc_dts_sensors *sensors;

	dts = tzd->devdata;
	sensors = dts->sensors;
	status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			       SOC_DTS_OFFSET_TEMP, &out);
	if (status)
		return status;

	out = (out & dts->temp_mask) >> dts->temp_shift;
	out -= SOC_DTS_TJMAX_ENCODING;
	*temp = sensors->tj_max - out * 1000;

	return 0;
}

static struct thermal_zone_device_ops tzone_ops = {
	.get_temp = sys_get_curr_temp,
	.get_trip_temp = sys_get_trip_temp,
	.get_trip_type = sys_get_trip_type,
	.set_trip_temp = sys_set_trip_temp,
};

static int soc_dts_enable(int id)
{
	u32 out;
	int ret;

	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			    SOC_DTS_OFFSET_ENABLE, &out);
	if (ret)
		return ret;

	if (!(out & BIT(id))) {
		out |= BIT(id);
		ret = iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
				     SOC_DTS_OFFSET_ENABLE, out);
		if (ret)
			return ret;
	}

	return ret;
}

static void remove_dts_thermal_zone(struct intel_soc_dts_sensor_entry *dts)
{
	if (dts) {
		iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
			       SOC_DTS_OFFSET_ENABLE, dts->store_status);
		thermal_zone_device_unregister(dts->tzone);
	}
}

static int add_dts_thermal_zone(int id, struct intel_soc_dts_sensor_entry *dts,
				bool notification_support, int trip_cnt,
				int read_only_trip_cnt)
{
	char name[10];
	int trip_count = 0;
	int trip_mask = 0;
	u32 store_ptps;
	int ret;
	int i;

	/* Store status to restor on exit */
	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			    SOC_DTS_OFFSET_ENABLE, &dts->store_status);
	if (ret)
		goto err_ret;

	dts->id = id;
	dts->temp_mask = 0x00FF << (id * 8);
	dts->temp_shift = id * 8;
	if (notification_support) {
		trip_count = min(SOC_MAX_DTS_TRIPS, trip_cnt);
		trip_mask = BIT(trip_count - read_only_trip_cnt) - 1;
	}

	/* Check if the writable trip we provide is not used by BIOS */
	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			    SOC_DTS_OFFSET_PTPS, &store_ptps);
	if (ret)
		trip_mask = 0;
	else {
		for (i = 0; i < trip_count; ++i) {
			if (trip_mask & BIT(i))
				if (store_ptps & (0xff << (i * 8)))
					trip_mask &= ~BIT(i);
		}
	}
	dts->trip_mask = trip_mask;
	dts->trip_count = trip_count;
	snprintf(name, sizeof(name), "soc_dts%d", id);
	dts->tzone = thermal_zone_device_register(name,
						  trip_count,
						  trip_mask,
						  dts, &tzone_ops,
						  NULL, 0, 0);
	if (IS_ERR(dts->tzone)) {
		ret = PTR_ERR(dts->tzone);
		goto err_ret;
	}

	ret = soc_dts_enable(id);
	if (ret)
		goto err_enable;

	return 0;
err_enable:
	thermal_zone_device_unregister(dts->tzone);
err_ret:
	return ret;
}

int intel_soc_dts_iosf_add_read_only_critical_trip(
	struct intel_soc_dts_sensors *sensors, int critical_offset)
{
	int i, j;

	for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i) {
		for (j = 0; j < sensors->soc_dts[i].trip_count; ++j) {
			if (!(sensors->soc_dts[i].trip_mask & BIT(j))) {
				return update_trip_temp(&sensors->soc_dts[i], j,
					sensors->tj_max - critical_offset,
					THERMAL_TRIP_CRITICAL);
			}
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(intel_soc_dts_iosf_add_read_only_critical_trip);

void intel_soc_dts_iosf_interrupt_handler(struct intel_soc_dts_sensors *sensors)
{
	u32 sticky_out;
	int status;
	u32 ptmc_out;
	unsigned long flags;

	spin_lock_irqsave(&sensors->intr_notify_lock, flags);

	status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			       SOC_DTS_OFFSET_PTMC, &ptmc_out);
	ptmc_out |= SOC_DTS_PTMC_APIC_DEASSERT_BIT;
	status = iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
				SOC_DTS_OFFSET_PTMC, ptmc_out);

	status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			       SOC_DTS_OFFSET_PTTSS, &sticky_out);
	pr_debug("status %d PTTSS %x\n", status, sticky_out);
	if (sticky_out & SOC_DTS_TRIP_MASK) {
		int i;
		/* reset sticky bit */
		status = iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
					SOC_DTS_OFFSET_PTTSS, sticky_out);
		spin_unlock_irqrestore(&sensors->intr_notify_lock, flags);

		for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i) {
			pr_debug("TZD update for zone %d\n", i);
			thermal_zone_device_update(sensors->soc_dts[i].tzone);
		}
	} else
		spin_unlock_irqrestore(&sensors->intr_notify_lock, flags);
}
EXPORT_SYMBOL_GPL(intel_soc_dts_iosf_interrupt_handler);

struct intel_soc_dts_sensors *intel_soc_dts_iosf_init(
	enum intel_soc_dts_interrupt_type intr_type, int trip_count,
	int read_only_trip_count)
{
	struct intel_soc_dts_sensors *sensors;
	bool notification;
	u32 tj_max;
	int ret;
	int i;

	if (!iosf_mbi_available())
		return ERR_PTR(-ENODEV);

	if (!trip_count || read_only_trip_count > trip_count)
		return ERR_PTR(-EINVAL);

	if (get_tj_max(&tj_max))
		return ERR_PTR(-EINVAL);

	sensors = kzalloc(sizeof(*sensors), GFP_KERNEL);
	if (!sensors)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&sensors->intr_notify_lock);
	mutex_init(&sensors->dts_update_lock);
	sensors->intr_type = intr_type;
	sensors->tj_max = tj_max;
	if (intr_type == INTEL_SOC_DTS_INTERRUPT_NONE)
		notification = false;
	else
		notification = true;
	for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i) {
		sensors->soc_dts[i].sensors = sensors;
		ret = add_dts_thermal_zone(i, &sensors->soc_dts[i],
					   notification, trip_count,
					   read_only_trip_count);
		if (ret)
			goto err_free;
	}

	for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i) {
		ret = update_trip_temp(&sensors->soc_dts[i], 0, 0,
				       THERMAL_TRIP_PASSIVE);
		if (ret)
			goto err_remove_zone;

		ret = update_trip_temp(&sensors->soc_dts[i], 1, 0,
				       THERMAL_TRIP_PASSIVE);
		if (ret)
			goto err_remove_zone;
	}

	return sensors;
err_remove_zone:
	for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i)
		remove_dts_thermal_zone(&sensors->soc_dts[i]);

err_free:
	kfree(sensors);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(intel_soc_dts_iosf_init);

void intel_soc_dts_iosf_exit(struct intel_soc_dts_sensors *sensors)
{
	int i;

	for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i) {
		update_trip_temp(&sensors->soc_dts[i], 0, 0, 0);
		update_trip_temp(&sensors->soc_dts[i], 1, 0, 0);
		remove_dts_thermal_zone(&sensors->soc_dts[i]);
	}
	kfree(sensors);
}
EXPORT_SYMBOL_GPL(intel_soc_dts_iosf_exit);

MODULE_LICENSE("GPL v2");
