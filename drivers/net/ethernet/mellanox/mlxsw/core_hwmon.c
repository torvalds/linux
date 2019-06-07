// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/sfp.h>

#include "core.h"
#include "core_env.h"

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
	u8 sensor_count;
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

static ssize_t mlxsw_hwmon_fan_fault_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	char fore_pl[MLXSW_REG_FORE_LEN];
	bool fault;
	int err;

	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(fore), fore_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to query fan\n");
		return err;
	}
	mlxsw_reg_fore_unpack(fore_pl, mlwsw_hwmon_attr->type_index, &fault);

	return sprintf(buf, "%u\n", fault);
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

static ssize_t mlxsw_hwmon_module_temp_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	char mtbr_pl[MLXSW_REG_MTBR_LEN] = {0};
	u16 temp;
	u8 module;
	int err;

	module = mlwsw_hwmon_attr->type_index - mlxsw_hwmon->sensor_count;
	mlxsw_reg_mtbr_pack(mtbr_pl, MLXSW_REG_MTBR_BASE_MODULE_INDEX + module,
			    1);
	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(mtbr), mtbr_pl);
	if (err) {
		dev_err(dev, "Failed to query module temperature sensor\n");
		return err;
	}

	mlxsw_reg_mtbr_temp_unpack(mtbr_pl, 0, &temp, NULL);
	/* Update status and temperature cache. */
	switch (temp) {
	case MLXSW_REG_MTBR_NO_CONN: /* fall-through */
	case MLXSW_REG_MTBR_NO_TEMP_SENS: /* fall-through */
	case MLXSW_REG_MTBR_INDEX_NA:
		temp = 0;
		break;
	case MLXSW_REG_MTBR_BAD_SENS_INFO:
		/* Untrusted cable is connected. Reading temperature from its
		 * sensor is faulty.
		 */
		temp = 0;
		break;
	default:
		temp = MLXSW_REG_MTMP_TEMP_TO_MC(temp);
		break;
	}

	return sprintf(buf, "%u\n", temp);
}

static ssize_t mlxsw_hwmon_module_temp_fault_show(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	char mtbr_pl[MLXSW_REG_MTBR_LEN] = {0};
	u8 module, fault;
	u16 temp;
	int err;

	module = mlwsw_hwmon_attr->type_index - mlxsw_hwmon->sensor_count;
	mlxsw_reg_mtbr_pack(mtbr_pl, MLXSW_REG_MTBR_BASE_MODULE_INDEX + module,
			    1);
	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(mtbr), mtbr_pl);
	if (err) {
		dev_err(dev, "Failed to query module temperature sensor\n");
		return err;
	}

	mlxsw_reg_mtbr_temp_unpack(mtbr_pl, 0, &temp, NULL);

	/* Update status and temperature cache. */
	switch (temp) {
	case MLXSW_REG_MTBR_BAD_SENS_INFO:
		/* Untrusted cable is connected. Reading temperature from its
		 * sensor is faulty.
		 */
		fault = 1;
		break;
	case MLXSW_REG_MTBR_NO_CONN: /* fall-through */
	case MLXSW_REG_MTBR_NO_TEMP_SENS: /* fall-through */
	case MLXSW_REG_MTBR_INDEX_NA:
	default:
		fault = 0;
		break;
	}

	return sprintf(buf, "%u\n", fault);
}

static ssize_t
mlxsw_hwmon_module_temp_critical_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	int temp;
	u8 module;
	int err;

	module = mlwsw_hwmon_attr->type_index - mlxsw_hwmon->sensor_count;
	err = mlxsw_env_module_temp_thresholds_get(mlxsw_hwmon->core, module,
						   SFP_TEMP_HIGH_WARN, &temp);
	if (err) {
		dev_err(dev, "Failed to query module temperature thresholds\n");
		return err;
	}

	return sprintf(buf, "%u\n", temp);
}

static ssize_t
mlxsw_hwmon_module_temp_emergency_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon *mlxsw_hwmon = mlwsw_hwmon_attr->hwmon;
	u8 module;
	int temp;
	int err;

	module = mlwsw_hwmon_attr->type_index - mlxsw_hwmon->sensor_count;
	err = mlxsw_env_module_temp_thresholds_get(mlxsw_hwmon->core, module,
						   SFP_TEMP_HIGH_ALARM, &temp);
	if (err) {
		dev_err(dev, "Failed to query module temperature thresholds\n");
		return err;
	}

	return sprintf(buf, "%u\n", temp);
}

static ssize_t
mlxsw_hwmon_module_temp_label_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);

	return sprintf(buf, "front panel %03u\n",
		       mlwsw_hwmon_attr->type_index);
}

enum mlxsw_hwmon_attr_type {
	MLXSW_HWMON_ATTR_TYPE_TEMP,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MAX,
	MLXSW_HWMON_ATTR_TYPE_TEMP_RST,
	MLXSW_HWMON_ATTR_TYPE_FAN_RPM,
	MLXSW_HWMON_ATTR_TYPE_FAN_FAULT,
	MLXSW_HWMON_ATTR_TYPE_PWM,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_FAULT,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_CRIT,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_EMERG,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_LABEL,
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
	case MLXSW_HWMON_ATTR_TYPE_FAN_FAULT:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_fan_fault_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "fan%u_fault", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_PWM:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_pwm_show;
		mlxsw_hwmon_attr->dev_attr.store = mlxsw_hwmon_pwm_store;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0644;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "pwm%u", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_module_temp_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_input", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_FAULT:
		mlxsw_hwmon_attr->dev_attr.show =
					mlxsw_hwmon_module_temp_fault_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_fault", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_CRIT:
		mlxsw_hwmon_attr->dev_attr.show =
			mlxsw_hwmon_module_temp_critical_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_crit", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_EMERG:
		mlxsw_hwmon_attr->dev_attr.show =
			mlxsw_hwmon_module_temp_emergency_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_emergency", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_LABEL:
		mlxsw_hwmon_attr->dev_attr.show =
			mlxsw_hwmon_module_temp_label_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_label", num + 1);
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
	int i;
	int err;

	err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(mtcap), mtcap_pl);
	if (err) {
		dev_err(mlxsw_hwmon->bus_info->dev, "Failed to get number of temp sensors\n");
		return err;
	}
	mlxsw_hwmon->sensor_count = mlxsw_reg_mtcap_sensor_count_get(mtcap_pl);
	for (i = 0; i < mlxsw_hwmon->sensor_count; i++) {
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
		if (tacho_active & BIT(type_index)) {
			mlxsw_hwmon_attr_add(mlxsw_hwmon,
					     MLXSW_HWMON_ATTR_TYPE_FAN_RPM,
					     type_index, num);
			mlxsw_hwmon_attr_add(mlxsw_hwmon,
					     MLXSW_HWMON_ATTR_TYPE_FAN_FAULT,
					     type_index, num++);
		}
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

static int mlxsw_hwmon_module_init(struct mlxsw_hwmon *mlxsw_hwmon)
{
	unsigned int module_count = mlxsw_core_max_ports(mlxsw_hwmon->core);
	char pmlp_pl[MLXSW_REG_PMLP_LEN] = {0};
	int i, index;
	u8 width;
	int err;

	if (!mlxsw_core_res_query_enabled(mlxsw_hwmon->core))
		return 0;

	/* Add extra attributes for module temperature. Sensor index is
	 * assigned to sensor_count value, while all indexed before
	 * sensor_count are already utilized by the sensors connected through
	 * mtmp register by mlxsw_hwmon_temp_init().
	 */
	index = mlxsw_hwmon->sensor_count;
	for (i = 1; i < module_count; i++) {
		mlxsw_reg_pmlp_pack(pmlp_pl, i);
		err = mlxsw_reg_query(mlxsw_hwmon->core, MLXSW_REG(pmlp),
				      pmlp_pl);
		if (err) {
			dev_err(mlxsw_hwmon->bus_info->dev, "Failed to read module index %d\n",
				i);
			return err;
		}
		width = mlxsw_reg_pmlp_width_get(pmlp_pl);
		if (!width)
			continue;
		mlxsw_hwmon_attr_add(mlxsw_hwmon,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE, index,
				     index);
		mlxsw_hwmon_attr_add(mlxsw_hwmon,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_FAULT,
				     index, index);
		mlxsw_hwmon_attr_add(mlxsw_hwmon,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_CRIT,
				     index, index);
		mlxsw_hwmon_attr_add(mlxsw_hwmon,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_EMERG,
				     index, index);
		mlxsw_hwmon_attr_add(mlxsw_hwmon,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_LABEL,
				     index, index);
		index++;
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

	mlxsw_hwmon = kzalloc(sizeof(*mlxsw_hwmon), GFP_KERNEL);
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

	err = mlxsw_hwmon_module_init(mlxsw_hwmon);
	if (err)
		goto err_temp_module_init;

	mlxsw_hwmon->groups[0] = &mlxsw_hwmon->group;
	mlxsw_hwmon->group.attrs = mlxsw_hwmon->attrs;

	hwmon_dev = hwmon_device_register_with_groups(mlxsw_bus_info->dev,
						      "mlxsw", mlxsw_hwmon,
						      mlxsw_hwmon->groups);
	if (IS_ERR(hwmon_dev)) {
		err = PTR_ERR(hwmon_dev);
		goto err_hwmon_register;
	}

	mlxsw_hwmon->hwmon_dev = hwmon_dev;
	*p_hwmon = mlxsw_hwmon;
	return 0;

err_hwmon_register:
err_temp_module_init:
err_fans_init:
err_temp_init:
	kfree(mlxsw_hwmon);
	return err;
}

void mlxsw_hwmon_fini(struct mlxsw_hwmon *mlxsw_hwmon)
{
	hwmon_device_unregister(mlxsw_hwmon->hwmon_dev);
	kfree(mlxsw_hwmon);
}
