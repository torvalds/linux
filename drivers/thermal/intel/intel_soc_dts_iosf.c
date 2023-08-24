// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_soc_dts_iosf.c
 * Copyright (c) 2015, Intel Corporation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/intel_tcc.h>
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

/* Mask for two trips in status bits */
#define SOC_DTS_TRIP_MASK		0x03

static int update_trip_temp(struct intel_soc_dts_sensors *sensors,
			    int thres_index, int temp)
{
	int status;
	u32 temp_out;
	u32 out;
	unsigned long update_ptps;
	u32 store_ptps;
	u32 store_ptmc;
	u32 store_te_out;
	u32 te_out;
	u32 int_enable_bit = SOC_DTS_TE_APICA_ENABLE;

	if (sensors->intr_type == INTEL_SOC_DTS_INTERRUPT_MSI)
		int_enable_bit |= SOC_DTS_TE_MSI_ENABLE;

	temp_out = (sensors->tj_max - temp) / 1000;

	status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			       SOC_DTS_OFFSET_PTPS, &store_ptps);
	if (status)
		return status;

	update_ptps = store_ptps;
	bitmap_set_value8(&update_ptps, temp_out & 0xFF, thres_index * 8);
	out = update_ptps;

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

static int configure_trip(struct intel_soc_dts_sensor_entry *dts,
			  int thres_index, enum thermal_trip_type trip_type,
			  int temp)
{
	int ret;

	ret = update_trip_temp(dts->sensors, thres_index, temp);
	if (ret)
		return ret;

	dts->trips[thres_index].temperature = temp;
	dts->trips[thres_index].type = trip_type;

	return 0;
}

static int sys_set_trip_temp(struct thermal_zone_device *tzd, int trip,
			     int temp)
{
	struct intel_soc_dts_sensor_entry *dts = thermal_zone_device_priv(tzd);
	struct intel_soc_dts_sensors *sensors = dts->sensors;
	int status;

	if (temp > sensors->tj_max)
		return -EINVAL;

	mutex_lock(&sensors->dts_update_lock);
	status = update_trip_temp(sensors, trip, temp);
	mutex_unlock(&sensors->dts_update_lock);

	return status;
}

static int sys_get_curr_temp(struct thermal_zone_device *tzd,
			     int *temp)
{
	int status;
	u32 out;
	struct intel_soc_dts_sensor_entry *dts = thermal_zone_device_priv(tzd);
	struct intel_soc_dts_sensors *sensors;
	unsigned long raw;

	sensors = dts->sensors;
	status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			       SOC_DTS_OFFSET_TEMP, &out);
	if (status)
		return status;

	raw = out;
	out = bitmap_get_value8(&raw, dts->id * 8) - SOC_DTS_TJMAX_ENCODING;
	*temp = sensors->tj_max - out * 1000;

	return 0;
}

static struct thermal_zone_device_ops tzone_ops = {
	.get_temp = sys_get_curr_temp,
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
	iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
		       SOC_DTS_OFFSET_ENABLE, dts->store_status);
	thermal_zone_device_unregister(dts->tzone);
}

static int add_dts_thermal_zone(int id, struct intel_soc_dts_sensor_entry *dts,
				bool critical_trip)
{
	int writable_trip_cnt = SOC_MAX_DTS_TRIPS;
	char name[10];
	unsigned long trip;
	int trip_mask;
	unsigned long ptps;
	u32 store_ptps;
	unsigned long i;
	int ret;

	/* Store status to restor on exit */
	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			    SOC_DTS_OFFSET_ENABLE, &dts->store_status);
	if (ret)
		goto err_ret;

	dts->id = id;

	if (critical_trip)
		writable_trip_cnt--;

	trip_mask = GENMASK(writable_trip_cnt - 1, 0);

	/* Check if the writable trip we provide is not used by BIOS */
	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			    SOC_DTS_OFFSET_PTPS, &store_ptps);
	if (ret)
		trip_mask = 0;
	else {
		ptps = store_ptps;
		for_each_set_clump8(i, trip, &ptps, writable_trip_cnt * 8)
			trip_mask &= ~BIT(i / 8);
	}
	dts->trip_mask = trip_mask;
	snprintf(name, sizeof(name), "soc_dts%d", id);
	dts->tzone = thermal_zone_device_register_with_trips(name, dts->trips,
							     SOC_MAX_DTS_TRIPS,
							     trip_mask,
							     dts, &tzone_ops,
							     NULL, 0, 0);
	if (IS_ERR(dts->tzone)) {
		ret = PTR_ERR(dts->tzone);
		goto err_ret;
	}
	ret = thermal_zone_device_enable(dts->tzone);
	if (ret)
		goto err_enable;

	ret = soc_dts_enable(id);
	if (ret)
		goto err_enable;

	return 0;
err_enable:
	thermal_zone_device_unregister(dts->tzone);
err_ret:
	return ret;
}

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
			thermal_zone_device_update(sensors->soc_dts[i].tzone,
						   THERMAL_EVENT_UNSPECIFIED);
		}
	} else
		spin_unlock_irqrestore(&sensors->intr_notify_lock, flags);
}
EXPORT_SYMBOL_GPL(intel_soc_dts_iosf_interrupt_handler);

static void dts_trips_reset(struct intel_soc_dts_sensors *sensors, int dts_index)
{
	configure_trip(&sensors->soc_dts[dts_index], 0, 0, 0);
	configure_trip(&sensors->soc_dts[dts_index], 1, 0, 0);
}

struct intel_soc_dts_sensors *
intel_soc_dts_iosf_init(enum intel_soc_dts_interrupt_type intr_type,
			bool critical_trip, int crit_offset)
{
	struct intel_soc_dts_sensors *sensors;
	int tj_max;
	int ret;
	int i;

	if (!iosf_mbi_available())
		return ERR_PTR(-ENODEV);

	tj_max = intel_tcc_get_tjmax(-1);
	if (tj_max < 0)
		return ERR_PTR(tj_max);

	sensors = kzalloc(sizeof(*sensors), GFP_KERNEL);
	if (!sensors)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&sensors->intr_notify_lock);
	mutex_init(&sensors->dts_update_lock);
	sensors->intr_type = intr_type;
	sensors->tj_max = tj_max * 1000;

	for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i) {
		enum thermal_trip_type trip_type;
		int temp;

		sensors->soc_dts[i].sensors = sensors;

		ret = configure_trip(&sensors->soc_dts[i], 0,
				     THERMAL_TRIP_PASSIVE, 0);
		if (ret)
			goto err_reset_trips;

		if (critical_trip) {
			trip_type = THERMAL_TRIP_CRITICAL;
			temp = sensors->tj_max - crit_offset;
		} else {
			trip_type = THERMAL_TRIP_PASSIVE;
			temp = 0;
		}
		ret = configure_trip(&sensors->soc_dts[i], 1, trip_type, temp);
		if (ret)
			goto err_reset_trips;
	}

	for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i) {
		ret = add_dts_thermal_zone(i, &sensors->soc_dts[i], critical_trip);
		if (ret)
			goto err_remove_zone;
	}

	return sensors;

err_remove_zone:
	for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i)
		remove_dts_thermal_zone(&sensors->soc_dts[i]);

err_reset_trips:
	for (i = 0; i < SOC_MAX_DTS_SENSORS; i++)
		dts_trips_reset(sensors, i);

	kfree(sensors);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(intel_soc_dts_iosf_init);

void intel_soc_dts_iosf_exit(struct intel_soc_dts_sensors *sensors)
{
	int i;

	for (i = 0; i < SOC_MAX_DTS_SENSORS; ++i) {
		remove_dts_thermal_zone(&sensors->soc_dts[i]);
		dts_trips_reset(sensors, i);
	}
	kfree(sensors);
}
EXPORT_SYMBOL_GPL(intel_soc_dts_iosf_exit);

MODULE_IMPORT_NS(INTEL_TCC);
MODULE_LICENSE("GPL v2");
