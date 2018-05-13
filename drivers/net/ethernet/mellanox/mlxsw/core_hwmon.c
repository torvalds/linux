/*
 * drivers/net/ethernet/mellanox/mlxsw/core_hwmon.c
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/hwmon.h>
#include <linux/err.h>

#include "core.h"

#define MLXSW_HWMON_TEMP_SENSOR_MAX_COUNT 127
#define MLXSW_HWMON_ATTR_COUNT (MLXSW_HWMON_TEMP_SENSOR_MAX_COUNT * 4 + \
				MLXSW_MFCR_TACHOS_MAX + MLXSW_MFCR_PWMS_MAX)

struct mlxsw_hwmon_attr {
	struct device_attribute dev_attr;
	struct mlxsw_hwmon *hwmon;
	unsigned int type_index;
	char name[32];
};

struct mlxsw_hwmon {
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	struct device *hwmon_dev;
	struct attribute_group group;
	const struct attribute_group *groups[2];
	struct attribute *attrs[MLXSW_HWMON_ATTR_COUNT + 1];
	struct mlxsw_hwmon_attr hwmon_attrs[MLXSW_HWMON_ATTR_COUNT];
	unsigned int attrs_count;
};

static ssize_t mlxsw_hwmon_temp_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	unsigned int temp;
	int err;

	mlxsw_reg_mtmp_pack(mtmp_pl, mlwsw_hwmon_attr->type_index,
			    false, false);
	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to query temp sensor\n");
		return err;
	}
	mlxsw_reg_mtmp_unpack(mtmp_pl, &temp, NULL, NULL);
	return sprintf(buf, "%u\n", temp);
}

static ssize_t mlxsw_hwmon_temp_max_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	unsigned int temp_max;
	int err;

	mlxsw_reg_mtmp_pack(mtmp_pl, mlwsw_hwmon_attr->type_index,
			    false, false);
	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to query temp sensor\n");
		return err;
	}
	mlxsw_reg_mtmp_unpack(mtmp_pl, NULL, &temp_max, NULL);
	return sprintf(buf, "%u\n", temp_max);
}

static ssize_t mlxsw_hwmon_temp_rst_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t len)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	if (val != 1)
		return -EINVAL;

	mlxsw_reg_mtmp_pack(mtmp_pl, mlwsw_hwmon_attr->type_index, true, true);
	err = mlxsw_reg_write(mlxsw_hwmon->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to reset temp sensor history\n");
		return err;
	}
	return len;
}

static ssize_t mlxsw_hwmon_fan_rpm_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	char mfsm_pl[MLXSW_REG_MFSM_LEN];
	int err;

	mlxsw_reg_mfsm_pack(mfsm_pl, mlwsw_hwmon_attr->type_index);
	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(mfsm), mfsm_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to query fan\n");
		return err;
	}
	return sprintf(buf, "%u\n", mlxsw_reg_mfsm_rpm_get(mfsm_pl));
}

static ssize_t mlxsw_hwmon_pwm_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	char mfsc_pl[MLXSW_REG_MFSC_LEN];
	int err;

	mlxsw_reg_mfsc_pack(mfsc_pl, mlwsw_hwmon_attr->type_index, 0);
	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(mfsc), mfsc_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to query PWM\n");
		return err;
	}
	return sprintf(buf, "%u\n",
		       mlxsw_reg_mfsc_pwm_duty_cycle_get(mfsc_pl));
}

static ssize_t mlxsw_hwmon_pwm_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	char mfsc_pl[MLXSW_REG_MFSC_LEN];
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	if (val > 255)
		return -EINVAL;

	mlxsw_reg_mfsc_pack(mfsc_pl, mlwsw_hwmon_attr->type_index, val);
	err = mlxsw_reg_write(mlxsw_hwmon->core, MLXSW_REG(mfsc), mfsc_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to write PWM\n");
		return err;
	}
	return len;
}

enum mlxsw_hwmon_attr_type {
	MLXSW_HWMON_ATTR_TYPE_TEMP,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MAX,
	MLXSW_HWMON_ATTR_TYPE_TEMP_RST,
	MLXSW_HWMON_ATTR_TYPE_FAN_RPM,
	MLXSW_HWMON_ATTR_TYPE_PWM,
};

static void mlxsw_hwmon_attr_add(struct mlxsw_hwmon *mlxsw_hwmon,
				 enum mlxsw_hwmon_attr_type attr_type,
				 unsigned int type_index, unsigned int num) {
	struct mlxsw_hwmon_attr *mlxsw_hwmon_attr;
	unsigned int attr_index;

	attr_index = mlxsw_hwmon->attrs_count;
	mlxsw_hwmon_attr = &mlxsw_hwmon->hwmon_attrs[attr_index];

	switch (attr_type) {
	case MLXSW_HWMON_ATTR_TYPE_TEMP:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_temp_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_input", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MAX:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_temp_max_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_highest", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_RST:
		mlxsw_hwmon_attr->dev_attr.store = mlxsw_hwmon_temp_rst_store;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0200;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_reset_history", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_FAN_RPM:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_fan_rpm_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "fan%u_input", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_PWM:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_pwm_show;
		mlxsw_hwmon_attr->dev_attr.store = mlxsw_hwmon_pwm_store;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0644;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "pwm%u", num + 1);
		break;
	default:
		WARN_ON(1);
	}

	mlxsw_hwmon_attr->type_index = type_index;
	mlxsw_hwmon_attr->hwmon = mlxsw_hwmon;
	mlxsw_hwmon_attr->dev_attr.attr.name = mlxsw_hwmon_attr->name;
	sysfs_attr_init(&mlxsw_hwmon_attr->dev_attr.attr);

	mlxsw_hwmon->attrs[attr_index] = &mlxsw_hwmon_attr->dev_attr.attr;
	mlxsw_hwmon->attrs_count++;
}

static int mlxsw_hwmon_temp_init(struct mlxsw_hwmon *mlxsw_hwmon)
{
	char mtcap_pl[MLXSW_REG_MTCAP_LEN] = {0};
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	u8 sensor_count;
	int i;
	int err;

	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(mtcap), mtcap_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to get number of temp sensors\n");
		return err;
	}
	sensor_count = mlxsw_reg_mtcap_sensor_count_get(mtcap_pl);
	for (i = 0; i < sensor_count; i++) {
		mlxsw_reg_mtmp_pack(mtmp_pl, i, true, true);
		err = mlxsw_reg_write(mlxsw_hwmon->core,
				      MLXSW_REG(mtmp), mtmp_pl);
		if (err) {
			dev_err(mlxsw_hwmon->bus_info->dev, "Failed to setup temp sensor number %d\n",
				i);
			return err;
		}
		mlxsw_hwmon_attr_add(mlxsw_hwmon,
				     MLXSW_HWMON_ATTR_TYPE_TEMP, i, i);
		mlxsw_hwmon_attr_add(mlxsw_hwmon,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MAX, i, i);
		mlxsw_hwmon_attr_add(mlxsw_hwmon,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_RST, i, i);
	}
	return 0;
}

static int mlxsw_hwmon_fans_init(struct mlxsw_hwmon *mlxsw_hwmon)
{
	char mfcr_pl[MLXSW_REG_MFCR_LEN] = {0};
	enum mlxsw_reg_mfcr_pwm_frequency freq;
	unsigned int type_index;
	unsigned int num;
	u16 tacho_active;
	u8 pwm_active;
	int err;

	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(mfcr), mfcr_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to get to probe PWMs and Tachometers\n");
		return err;
	}
	mlxsw_reg_mfcr_unpack(mfcr_pl, &freq, &tacho_active, &pwm_active);
	num = 0;
	for (type_index = 0; type_index < MLXSW_MFCR_TACHOS_MAX; type_index++) {
		if (tacho_active & BIT(type_index))
			mlxsw_hwmon_attr_add(mlxsw_hwmon,
					     MLXSW_HWMON_ATTR_TYPE_FAN_RPM,
					     type_index, num++);
	}
	num = 0;
	for (type_index = 0; type_index < MLXSW_MFCR_PWMS_MAX; type_index++) {
		if (pwm_active & BIT(type_index))
			mlxsw_hwmon_attr_add(mlxsw_hwmon,
					     MLXSW_HWMON_ATTR_TYPE_PWM,
					     type_index, num++);
	}
	return 0;
}

int mlxsw_hwmon_init(struct mlxsw_core *mlxsw_core,
		     const struct mlxsw_bus_info *mlxsw_bus_info,
		     struct mlxsw_hwmon **p_hwmon)
{
	struct mlxsw_hwmon *mlxsw_hwmon;
	struct device *hwmon_dev;
	int err;

	mlxsw_hwmon = devm_kzalloc(mlxsw_bus_info->dev, sizeof(*mlxsw_hwmon),
				   GFP_KERNEL);
	if (!mlxsw_hwmon)
		return -ENOMEM;
	mlxsw_hwmon->core = mlxsw_core;
	mlxsw_hwmon->bus_info = mlxsw_bus_info;

	err = mlxsw_hwmon_temp_init(mlxsw_hwmon);
	if (err)
		goto err_temp_init;

	err = mlxsw_hwmon_fans_init(mlxsw_hwmon);
	if (err)
		goto err_fans_init;

	mlxsw_hwmon->groups[0] = &mlxsw_hwmon->group;
	mlxsw_hwmon->group.attrs = mlxsw_hwmon->attrs;

	hwmon_dev = devm_hwmon_device_register_with_groups(mlxsw_bus_info->dev,
							   "mlxsw",
							   mlxsw_hwmon,
							   mlxsw_hwmon->groups);
	if (IS_ERR(hwmon_dev)) {
		err = PTR_ERR(hwmon_dev);
		goto err_hwmon_register;
	}

	mlxsw_hwmon->hwmon_dev = hwmon_dev;
	*p_hwmon = mlxsw_hwmon;
	return 0;

err_hwmon_register:
err_fans_init:
err_temp_init:
	return err;
}
