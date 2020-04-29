/*
 * intel_quark_dts_thermal.c
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Contact Information:
 *  Ong Boon Leong <boon.leong.ong@intel.com>
 *  Intel Malaysia, Penang
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Quark DTS thermal driver is implemented by referencing
 * intel_soc_dts_thermal.c.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/thermal.h>
#include <asm/cpu_device_id.h>
#include <asm/iosf_mbi.h>

/* DTS reset is programmed via QRK_MBI_UNIT_SOC */
#define QRK_DTS_REG_OFFSET_RESET	0x34
#define QRK_DTS_RESET_BIT		BIT(0)

/* DTS enable is programmed via QRK_MBI_UNIT_RMU */
#define QRK_DTS_REG_OFFSET_ENABLE	0xB0
#define QRK_DTS_ENABLE_BIT		BIT(15)

/* Temperature Register is read via QRK_MBI_UNIT_RMU */
#define QRK_DTS_REG_OFFSET_TEMP		0xB1
#define QRK_DTS_MASK_TEMP		0xFF
#define QRK_DTS_OFFSET_TEMP		0
#define QRK_DTS_OFFSET_REL_TEMP		16
#define QRK_DTS_TEMP_BASE		50

/* Programmable Trip Point Register is configured via QRK_MBI_UNIT_RMU */
#define QRK_DTS_REG_OFFSET_PTPS		0xB2
#define QRK_DTS_MASK_TP_THRES		0xFF
#define QRK_DTS_SHIFT_TP		8
#define QRK_DTS_ID_TP_CRITICAL		0
#define QRK_DTS_SAFE_TP_THRES		105

/* Thermal Sensor Register Lock */
#define QRK_DTS_REG_OFFSET_LOCK		0x71
#define QRK_DTS_LOCK_BIT		BIT(5)

/* Quark DTS has 2 trip points: hot & catastrophic */
#define QRK_MAX_DTS_TRIPS	2
/* If DTS not locked, all trip points are configurable */
#define QRK_DTS_WR_MASK_SET	0x3
/* If DTS locked, all trip points are not configurable */
#define QRK_DTS_WR_MASK_CLR	0

#define DEFAULT_POLL_DELAY	2000

struct soc_sensor_entry {
	bool locked;
	u32 store_ptps;
	u32 store_dts_enable;
	enum thermal_device_mode mode;
	struct thermal_zone_device *tzone;
};

static struct soc_sensor_entry *soc_dts;

static int polling_delay = DEFAULT_POLL_DELAY;
module_param(polling_delay, int, 0644);
MODULE_PARM_DESC(polling_delay,
	"Polling interval for checking trip points (in milliseconds)");

static DEFINE_MUTEX(dts_update_mutex);

static int soc_dts_enable(struct thermal_zone_device *tzd)
{
	u32 out;
	struct soc_sensor_entry *aux_entry = tzd->devdata;
	int ret;

	ret = iosf_mbi_read(QRK_MBI_UNIT_RMU, MBI_REG_READ,
			    QRK_DTS_REG_OFFSET_ENABLE, &out);
	if (ret)
		return ret;

	if (out & QRK_DTS_ENABLE_BIT) {
		aux_entry->mode = THERMAL_DEVICE_ENABLED;
		return 0;
	}

	if (!aux_entry->locked) {
		out |= QRK_DTS_ENABLE_BIT;
		ret = iosf_mbi_write(QRK_MBI_UNIT_RMU, MBI_REG_WRITE,
				     QRK_DTS_REG_OFFSET_ENABLE, out);
		if (ret)
			return ret;

		aux_entry->mode = THERMAL_DEVICE_ENABLED;
	} else {
		aux_entry->mode = THERMAL_DEVICE_DISABLED;
		pr_info("DTS is locked. Cannot enable DTS\n");
		ret = -EPERM;
	}

	return ret;
}

static int soc_dts_disable(struct thermal_zone_device *tzd)
{
	u32 out;
	struct soc_sensor_entry *aux_entry = tzd->devdata;
	int ret;

	ret = iosf_mbi_read(QRK_MBI_UNIT_RMU, MBI_REG_READ,
			    QRK_DTS_REG_OFFSET_ENABLE, &out);
	if (ret)
		return ret;

	if (!(out & QRK_DTS_ENABLE_BIT)) {
		aux_entry->mode = THERMAL_DEVICE_DISABLED;
		return 0;
	}

	if (!aux_entry->locked) {
		out &= ~QRK_DTS_ENABLE_BIT;
		ret = iosf_mbi_write(QRK_MBI_UNIT_RMU, MBI_REG_WRITE,
				     QRK_DTS_REG_OFFSET_ENABLE, out);

		if (ret)
			return ret;

		aux_entry->mode = THERMAL_DEVICE_DISABLED;
	} else {
		aux_entry->mode = THERMAL_DEVICE_ENABLED;
		pr_info("DTS is locked. Cannot disable DTS\n");
		ret = -EPERM;
	}

	return ret;
}

static int _get_trip_temp(int trip, int *temp)
{
	int status;
	u32 out;

	mutex_lock(&dts_update_mutex);
	status = iosf_mbi_read(QRK_MBI_UNIT_RMU, MBI_REG_READ,
			       QRK_DTS_REG_OFFSET_PTPS, &out);
	mutex_unlock(&dts_update_mutex);

	if (status)
		return status;

	/*
	 * Thermal Sensor Programmable Trip Point Register has 8-bit
	 * fields for critical (catastrophic) and hot set trip point
	 * thresholds. The threshold value is always offset by its
	 * temperature base (50 degree Celsius).
	 */
	*temp = (out >> (trip * QRK_DTS_SHIFT_TP)) & QRK_DTS_MASK_TP_THRES;
	*temp -= QRK_DTS_TEMP_BASE;

	return 0;
}

static inline int sys_get_trip_temp(struct thermal_zone_device *tzd,
				int trip, int *temp)
{
	return _get_trip_temp(trip, temp);
}

static inline int sys_get_crit_temp(struct thermal_zone_device *tzd, int *temp)
{
	return _get_trip_temp(QRK_DTS_ID_TP_CRITICAL, temp);
}

static int update_trip_temp(struct soc_sensor_entry *aux_entry,
				int trip, int temp)
{
	u32 out;
	u32 temp_out;
	u32 store_ptps;
	int ret;

	mutex_lock(&dts_update_mutex);
	if (aux_entry->locked) {
		ret = -EPERM;
		goto failed;
	}

	ret = iosf_mbi_read(QRK_MBI_UNIT_RMU, MBI_REG_READ,
			    QRK_DTS_REG_OFFSET_PTPS, &store_ptps);
	if (ret)
		goto failed;

	/*
	 * Protection against unsafe trip point thresdhold value.
	 * As Quark X1000 data-sheet does not provide any recommendation
	 * regarding the safe trip point threshold value to use, we choose
	 * the safe value according to the threshold value set by UEFI BIOS.
	 */
	if (temp > QRK_DTS_SAFE_TP_THRES)
		temp = QRK_DTS_SAFE_TP_THRES;

	/*
	 * Thermal Sensor Programmable Trip Point Register has 8-bit
	 * fields for critical (catastrophic) and hot set trip point
	 * thresholds. The threshold value is always offset by its
	 * temperature base (50 degree Celsius).
	 */
	temp_out = temp + QRK_DTS_TEMP_BASE;
	out = (store_ptps & ~(QRK_DTS_MASK_TP_THRES <<
		(trip * QRK_DTS_SHIFT_TP)));
	out |= (temp_out & QRK_DTS_MASK_TP_THRES) <<
		(trip * QRK_DTS_SHIFT_TP);

	ret = iosf_mbi_write(QRK_MBI_UNIT_RMU, MBI_REG_WRITE,
			     QRK_DTS_REG_OFFSET_PTPS, out);

failed:
	mutex_unlock(&dts_update_mutex);
	return ret;
}

static inline int sys_set_trip_temp(struct thermal_zone_device *tzd, int trip,
				int temp)
{
	return update_trip_temp(tzd->devdata, trip, temp);
}

static int sys_get_trip_type(struct thermal_zone_device *thermal,
		int trip, enum thermal_trip_type *type)
{
	if (trip)
		*type = THERMAL_TRIP_HOT;
	else
		*type = THERMAL_TRIP_CRITICAL;

	return 0;
}

static int sys_get_curr_temp(struct thermal_zone_device *tzd,
				int *temp)
{
	u32 out;
	int ret;

	mutex_lock(&dts_update_mutex);
	ret = iosf_mbi_read(QRK_MBI_UNIT_RMU, MBI_REG_READ,
			    QRK_DTS_REG_OFFSET_TEMP, &out);
	mutex_unlock(&dts_update_mutex);

	if (ret)
		return ret;

	/*
	 * Thermal Sensor Temperature Register has 8-bit field
	 * for temperature value (offset by temperature base
	 * 50 degree Celsius).
	 */
	out = (out >> QRK_DTS_OFFSET_TEMP) & QRK_DTS_MASK_TEMP;
	*temp = out - QRK_DTS_TEMP_BASE;

	return 0;
}

static int sys_get_mode(struct thermal_zone_device *tzd,
				enum thermal_device_mode *mode)
{
	struct soc_sensor_entry *aux_entry = tzd->devdata;
	*mode = aux_entry->mode;
	return 0;
}

static int sys_set_mode(struct thermal_zone_device *tzd,
				enum thermal_device_mode mode)
{
	int ret;

	mutex_lock(&dts_update_mutex);
	if (mode == THERMAL_DEVICE_ENABLED)
		ret = soc_dts_enable(tzd);
	else
		ret = soc_dts_disable(tzd);
	mutex_unlock(&dts_update_mutex);

	return ret;
}

static struct thermal_zone_device_ops tzone_ops = {
	.get_temp = sys_get_curr_temp,
	.get_trip_temp = sys_get_trip_temp,
	.get_trip_type = sys_get_trip_type,
	.set_trip_temp = sys_set_trip_temp,
	.get_crit_temp = sys_get_crit_temp,
	.get_mode = sys_get_mode,
	.set_mode = sys_set_mode,
};

static void free_soc_dts(struct soc_sensor_entry *aux_entry)
{
	if (aux_entry) {
		if (!aux_entry->locked) {
			mutex_lock(&dts_update_mutex);
			iosf_mbi_write(QRK_MBI_UNIT_RMU, MBI_REG_WRITE,
				       QRK_DTS_REG_OFFSET_ENABLE,
				       aux_entry->store_dts_enable);

			iosf_mbi_write(QRK_MBI_UNIT_RMU, MBI_REG_WRITE,
				       QRK_DTS_REG_OFFSET_PTPS,
				       aux_entry->store_ptps);
			mutex_unlock(&dts_update_mutex);
		}
		thermal_zone_device_unregister(aux_entry->tzone);
		kfree(aux_entry);
	}
}

static struct soc_sensor_entry *alloc_soc_dts(void)
{
	struct soc_sensor_entry *aux_entry;
	int err;
	u32 out;
	int wr_mask;

	aux_entry = kzalloc(sizeof(*aux_entry), GFP_KERNEL);
	if (!aux_entry) {
		err = -ENOMEM;
		return ERR_PTR(-ENOMEM);
	}

	/* Check if DTS register is locked */
	err = iosf_mbi_read(QRK_MBI_UNIT_RMU, MBI_REG_READ,
			    QRK_DTS_REG_OFFSET_LOCK, &out);
	if (err)
		goto err_ret;

	if (out & QRK_DTS_LOCK_BIT) {
		aux_entry->locked = true;
		wr_mask = QRK_DTS_WR_MASK_CLR;
	} else {
		aux_entry->locked = false;
		wr_mask = QRK_DTS_WR_MASK_SET;
	}

	/* Store DTS default state if DTS registers are not locked */
	if (!aux_entry->locked) {
		/* Store DTS default enable for restore on exit */
		err = iosf_mbi_read(QRK_MBI_UNIT_RMU, MBI_REG_READ,
				    QRK_DTS_REG_OFFSET_ENABLE,
				    &aux_entry->store_dts_enable);
		if (err)
			goto err_ret;

		/* Store DTS default PTPS register for restore on exit */
		err = iosf_mbi_read(QRK_MBI_UNIT_RMU, MBI_REG_READ,
				    QRK_DTS_REG_OFFSET_PTPS,
				    &aux_entry->store_ptps);
		if (err)
			goto err_ret;
	}

	aux_entry->tzone = thermal_zone_device_register("quark_dts",
			QRK_MAX_DTS_TRIPS,
			wr_mask,
			aux_entry, &tzone_ops, NULL, 0, polling_delay);
	if (IS_ERR(aux_entry->tzone)) {
		err = PTR_ERR(aux_entry->tzone);
		goto err_ret;
	}

	mutex_lock(&dts_update_mutex);
	err = soc_dts_enable(aux_entry->tzone);
	mutex_unlock(&dts_update_mutex);
	if (err)
		goto err_aux_status;

	return aux_entry;

err_aux_status:
	thermal_zone_device_unregister(aux_entry->tzone);
err_ret:
	kfree(aux_entry);
	return ERR_PTR(err);
}

static const struct x86_cpu_id qrk_thermal_ids[] __initconst  = {
	X86_MATCH_VENDOR_FAM_MODEL(INTEL, 5, INTEL_FAM5_QUARK_X1000, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, qrk_thermal_ids);

static int __init intel_quark_thermal_init(void)
{
	int err = 0;

	if (!x86_match_cpu(qrk_thermal_ids) || !iosf_mbi_available())
		return -ENODEV;

	soc_dts = alloc_soc_dts();
	if (IS_ERR(soc_dts)) {
		err = PTR_ERR(soc_dts);
		goto err_free;
	}

	return 0;

err_free:
	free_soc_dts(soc_dts);
	return err;
}

static void __exit intel_quark_thermal_exit(void)
{
	free_soc_dts(soc_dts);
}

module_init(intel_quark_thermal_init)
module_exit(intel_quark_thermal_exit)

MODULE_DESCRIPTION("Intel Quark DTS Thermal Driver");
MODULE_AUTHOR("Ong Boon Leong <boon.leong.ong@intel.com>");
MODULE_LICENSE("Dual BSD/GPL");
