// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2016-2018 Mellanox Technologies. All rights reserved
 * Copyright (c) 2016 Ivan Vecera <cera@cera.cz>
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/sfp.h>

#include "core.h"
#include "core_env.h"

#define MLXSW_THERMAL_POLL_INT	1000	/* ms */
#define MLXSW_THERMAL_SLOW_POLL_INT	20000	/* ms */
#define MLXSW_THERMAL_ASIC_TEMP_NORM	75000	/* 75C */
#define MLXSW_THERMAL_ASIC_TEMP_HIGH	85000	/* 85C */
#define MLXSW_THERMAL_ASIC_TEMP_HOT	105000	/* 105C */
#define MLXSW_THERMAL_MODULE_TEMP_NORM	55000	/* 55C */
#define MLXSW_THERMAL_MODULE_TEMP_HIGH	65000	/* 65C */
#define MLXSW_THERMAL_MODULE_TEMP_HOT	80000	/* 80C */
#define MLXSW_THERMAL_HYSTERESIS_TEMP	5000	/* 5C */
#define MLXSW_THERMAL_MODULE_TEMP_SHIFT	(MLXSW_THERMAL_HYSTERESIS_TEMP * 2)
#define MLXSW_THERMAL_MAX_STATE	10
#define MLXSW_THERMAL_MIN_STATE	2
#define MLXSW_THERMAL_MAX_DUTY	255

/* External cooling devices, allowed for binding to mlxsw thermal zones. */
static char * const mlxsw_thermal_external_allowed_cdev[] = {
	"mlxreg_fan",
	"emc2305",
};

struct mlxsw_cooling_states {
	int	min_state;
	int	max_state;
};

static const struct thermal_trip default_thermal_trips[] = {
	{	/* In range - 0-40% PWM */
		.type		= THERMAL_TRIP_ACTIVE,
		.temperature	= MLXSW_THERMAL_ASIC_TEMP_NORM,
		.hysteresis	= MLXSW_THERMAL_HYSTERESIS_TEMP,
		.flags		= THERMAL_TRIP_FLAG_RW_TEMP,
	},
	{
		/* In range - 40-100% PWM */
		.type		= THERMAL_TRIP_ACTIVE,
		.temperature	= MLXSW_THERMAL_ASIC_TEMP_HIGH,
		.hysteresis	= MLXSW_THERMAL_HYSTERESIS_TEMP,
		.flags		= THERMAL_TRIP_FLAG_RW_TEMP,
	},
	{	/* Warning */
		.type		= THERMAL_TRIP_HOT,
		.temperature	= MLXSW_THERMAL_ASIC_TEMP_HOT,
		.flags		= THERMAL_TRIP_FLAG_RW_TEMP,
	},
};

static const struct thermal_trip default_thermal_module_trips[] = {
	{	/* In range - 0-40% PWM */
		.type		= THERMAL_TRIP_ACTIVE,
		.temperature	= MLXSW_THERMAL_MODULE_TEMP_NORM,
		.hysteresis	= MLXSW_THERMAL_HYSTERESIS_TEMP,
		.flags		= THERMAL_TRIP_FLAG_RW_TEMP,
	},
	{
		/* In range - 40-100% PWM */
		.type		= THERMAL_TRIP_ACTIVE,
		.temperature	= MLXSW_THERMAL_MODULE_TEMP_HIGH,
		.hysteresis	= MLXSW_THERMAL_HYSTERESIS_TEMP,
		.flags		= THERMAL_TRIP_FLAG_RW_TEMP,
	},
	{	/* Warning */
		.type		= THERMAL_TRIP_HOT,
		.temperature	= MLXSW_THERMAL_MODULE_TEMP_HOT,
		.flags		= THERMAL_TRIP_FLAG_RW_TEMP,
	},
};

static const struct mlxsw_cooling_states default_cooling_states[] = {
	{
		.min_state	= 0,
		.max_state	= (4 * MLXSW_THERMAL_MAX_STATE) / 10,
	},
	{
		.min_state	= (4 * MLXSW_THERMAL_MAX_STATE) / 10,
		.max_state	= MLXSW_THERMAL_MAX_STATE,
	},
	{
		.min_state	= MLXSW_THERMAL_MAX_STATE,
		.max_state	= MLXSW_THERMAL_MAX_STATE,
	},
};

#define MLXSW_THERMAL_NUM_TRIPS	ARRAY_SIZE(default_thermal_trips)

struct mlxsw_thermal;

struct mlxsw_thermal_cooling_device {
	struct mlxsw_thermal *thermal;
	struct thermal_cooling_device *cdev;
	unsigned int idx;
};

struct mlxsw_thermal_module {
	struct mlxsw_thermal *parent;
	struct thermal_zone_device *tzdev;
	struct thermal_trip trips[MLXSW_THERMAL_NUM_TRIPS];
	struct mlxsw_cooling_states cooling_states[MLXSW_THERMAL_NUM_TRIPS];
	int module; /* Module or gearbox number */
	u8 slot_index;
};

struct mlxsw_thermal_area {
	struct mlxsw_thermal_module *tz_module_arr;
	u8 tz_module_num;
	struct mlxsw_thermal_module *tz_gearbox_arr;
	u8 tz_gearbox_num;
	u8 slot_index;
	bool active;
};

struct mlxsw_thermal {
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	struct thermal_zone_device *tzdev;
	int polling_delay;
	struct mlxsw_thermal_cooling_device cdevs[MLXSW_MFCR_PWMS_MAX];
	struct thermal_trip trips[MLXSW_THERMAL_NUM_TRIPS];
	struct mlxsw_cooling_states cooling_states[MLXSW_THERMAL_NUM_TRIPS];
	struct mlxsw_thermal_area line_cards[];
};

static inline u8 mlxsw_state_to_duty(int state)
{
	return DIV_ROUND_CLOSEST(state * MLXSW_THERMAL_MAX_DUTY,
				 MLXSW_THERMAL_MAX_STATE);
}

static inline int mlxsw_duty_to_state(u8 duty)
{
	return DIV_ROUND_CLOSEST(duty * MLXSW_THERMAL_MAX_STATE,
				 MLXSW_THERMAL_MAX_DUTY);
}

static int mlxsw_get_cooling_device_idx(struct mlxsw_thermal *thermal,
					struct thermal_cooling_device *cdev)
{
	int i;

	for (i = 0; i < MLXSW_MFCR_PWMS_MAX; i++)
		if (thermal->cdevs[i].cdev == cdev)
			return i;

	/* Allow mlxsw thermal zone binding to an external cooling device */
	for (i = 0; i < ARRAY_SIZE(mlxsw_thermal_external_allowed_cdev); i++) {
		if (!strcmp(cdev->type, mlxsw_thermal_external_allowed_cdev[i]))
			return 0;
	}

	return -ENODEV;
}

static int mlxsw_thermal_bind(struct thermal_zone_device *tzdev,
			      struct thermal_cooling_device *cdev)
{
	struct mlxsw_thermal *thermal = thermal_zone_device_priv(tzdev);
	struct device *dev = thermal->bus_info->dev;
	int i, err;

	/* If the cooling device is one of ours bind it */
	if (mlxsw_get_cooling_device_idx(thermal, cdev) < 0)
		return 0;

	for (i = 0; i < MLXSW_THERMAL_NUM_TRIPS; i++) {
		const struct mlxsw_cooling_states *state = &thermal->cooling_states[i];

		err = thermal_zone_bind_cooling_device(tzdev, i, cdev,
						       state->max_state,
						       state->min_state,
						       THERMAL_WEIGHT_DEFAULT);
		if (err < 0) {
			dev_err(dev, "Failed to bind cooling device to trip %d\n", i);
			return err;
		}
	}
	return 0;
}

static int mlxsw_thermal_unbind(struct thermal_zone_device *tzdev,
				struct thermal_cooling_device *cdev)
{
	struct mlxsw_thermal *thermal = thermal_zone_device_priv(tzdev);
	struct device *dev = thermal->bus_info->dev;
	int i;
	int err;

	/* If the cooling device is our one unbind it */
	if (mlxsw_get_cooling_device_idx(thermal, cdev) < 0)
		return 0;

	for (i = 0; i < MLXSW_THERMAL_NUM_TRIPS; i++) {
		err = thermal_zone_unbind_cooling_device(tzdev, i, cdev);
		if (err < 0) {
			dev_err(dev, "Failed to unbind cooling device\n");
			return err;
		}
	}
	return 0;
}

static int mlxsw_thermal_get_temp(struct thermal_zone_device *tzdev,
				  int *p_temp)
{
	struct mlxsw_thermal *thermal = thermal_zone_device_priv(tzdev);
	struct device *dev = thermal->bus_info->dev;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	int temp;
	int err;

	mlxsw_reg_mtmp_pack(mtmp_pl, 0, 0, false, false);

	err = mlxsw_reg_query(thermal->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err) {
		dev_err(dev, "Failed to query temp sensor\n");
		return err;
	}
	mlxsw_reg_mtmp_unpack(mtmp_pl, &temp, NULL, NULL, NULL, NULL);

	*p_temp = temp;
	return 0;
}

static struct thermal_zone_params mlxsw_thermal_params = {
	.no_hwmon = true,
};

static struct thermal_zone_device_ops mlxsw_thermal_ops = {
	.bind = mlxsw_thermal_bind,
	.unbind = mlxsw_thermal_unbind,
	.get_temp = mlxsw_thermal_get_temp,
};

static int mlxsw_thermal_module_bind(struct thermal_zone_device *tzdev,
				     struct thermal_cooling_device *cdev)
{
	struct mlxsw_thermal_module *tz = thermal_zone_device_priv(tzdev);
	struct mlxsw_thermal *thermal = tz->parent;
	int i, j, err;

	/* If the cooling device is one of ours bind it */
	if (mlxsw_get_cooling_device_idx(thermal, cdev) < 0)
		return 0;

	for (i = 0; i < MLXSW_THERMAL_NUM_TRIPS; i++) {
		const struct mlxsw_cooling_states *state = &tz->cooling_states[i];

		err = thermal_zone_bind_cooling_device(tzdev, i, cdev,
						       state->max_state,
						       state->min_state,
						       THERMAL_WEIGHT_DEFAULT);
		if (err < 0)
			goto err_thermal_zone_bind_cooling_device;
	}
	return 0;

err_thermal_zone_bind_cooling_device:
	for (j = i - 1; j >= 0; j--)
		thermal_zone_unbind_cooling_device(tzdev, j, cdev);
	return err;
}

static int mlxsw_thermal_module_unbind(struct thermal_zone_device *tzdev,
				       struct thermal_cooling_device *cdev)
{
	struct mlxsw_thermal_module *tz = thermal_zone_device_priv(tzdev);
	struct mlxsw_thermal *thermal = tz->parent;
	int i;
	int err;

	/* If the cooling device is one of ours unbind it */
	if (mlxsw_get_cooling_device_idx(thermal, cdev) < 0)
		return 0;

	for (i = 0; i < MLXSW_THERMAL_NUM_TRIPS; i++) {
		err = thermal_zone_unbind_cooling_device(tzdev, i, cdev);
		WARN_ON(err);
	}
	return err;
}

static int mlxsw_thermal_module_temp_get(struct thermal_zone_device *tzdev,
					 int *p_temp)
{
	struct mlxsw_thermal_module *tz = thermal_zone_device_priv(tzdev);
	struct mlxsw_thermal *thermal = tz->parent;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	u16 sensor_index;
	int err;

	sensor_index = MLXSW_REG_MTMP_MODULE_INDEX_MIN + tz->module;
	mlxsw_reg_mtmp_pack(mtmp_pl, tz->slot_index, sensor_index,
			    false, false);
	err = mlxsw_reg_query(thermal->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err)
		return err;
	mlxsw_reg_mtmp_unpack(mtmp_pl, p_temp, NULL, NULL, NULL, NULL);
	return 0;
}

static struct thermal_zone_device_ops mlxsw_thermal_module_ops = {
	.bind		= mlxsw_thermal_module_bind,
	.unbind		= mlxsw_thermal_module_unbind,
	.get_temp	= mlxsw_thermal_module_temp_get,
};

static int mlxsw_thermal_gearbox_temp_get(struct thermal_zone_device *tzdev,
					  int *p_temp)
{
	struct mlxsw_thermal_module *tz = thermal_zone_device_priv(tzdev);
	struct mlxsw_thermal *thermal = tz->parent;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	u16 index;
	int temp;
	int err;

	index = MLXSW_REG_MTMP_GBOX_INDEX_MIN + tz->module;
	mlxsw_reg_mtmp_pack(mtmp_pl, tz->slot_index, index, false, false);

	err = mlxsw_reg_query(thermal->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err)
		return err;

	mlxsw_reg_mtmp_unpack(mtmp_pl, &temp, NULL, NULL, NULL, NULL);

	*p_temp = temp;
	return 0;
}

static struct thermal_zone_device_ops mlxsw_thermal_gearbox_ops = {
	.bind		= mlxsw_thermal_module_bind,
	.unbind		= mlxsw_thermal_module_unbind,
	.get_temp	= mlxsw_thermal_gearbox_temp_get,
};

static int mlxsw_thermal_get_max_state(struct thermal_cooling_device *cdev,
				       unsigned long *p_state)
{
	*p_state = MLXSW_THERMAL_MAX_STATE;
	return 0;
}

static int mlxsw_thermal_get_cur_state(struct thermal_cooling_device *cdev,
				       unsigned long *p_state)

{
	struct mlxsw_thermal_cooling_device *mlxsw_cdev = cdev->devdata;
	struct mlxsw_thermal *thermal = mlxsw_cdev->thermal;
	struct device *dev = thermal->bus_info->dev;
	char mfsc_pl[MLXSW_REG_MFSC_LEN];
	u8 duty;
	int err;

	mlxsw_reg_mfsc_pack(mfsc_pl, mlxsw_cdev->idx, 0);
	err = mlxsw_reg_query(thermal->core, MLXSW_REG(mfsc), mfsc_pl);
	if (err) {
		dev_err(dev, "Failed to query PWM duty\n");
		return err;
	}

	duty = mlxsw_reg_mfsc_pwm_duty_cycle_get(mfsc_pl);
	*p_state = mlxsw_duty_to_state(duty);
	return 0;
}

static int mlxsw_thermal_set_cur_state(struct thermal_cooling_device *cdev,
				       unsigned long state)

{
	struct mlxsw_thermal_cooling_device *mlxsw_cdev = cdev->devdata;
	struct mlxsw_thermal *thermal = mlxsw_cdev->thermal;
	struct device *dev = thermal->bus_info->dev;
	char mfsc_pl[MLXSW_REG_MFSC_LEN];
	int err;

	if (state > MLXSW_THERMAL_MAX_STATE)
		return -EINVAL;

	/* Normalize the state to the valid speed range. */
	state = max_t(unsigned long, MLXSW_THERMAL_MIN_STATE, state);
	mlxsw_reg_mfsc_pack(mfsc_pl, mlxsw_cdev->idx,
			    mlxsw_state_to_duty(state));
	err = mlxsw_reg_write(thermal->core, MLXSW_REG(mfsc), mfsc_pl);
	if (err) {
		dev_err(dev, "Failed to write PWM duty\n");
		return err;
	}
	return 0;
}

static const struct thermal_cooling_device_ops mlxsw_cooling_ops = {
	.get_max_state	= mlxsw_thermal_get_max_state,
	.get_cur_state	= mlxsw_thermal_get_cur_state,
	.set_cur_state	= mlxsw_thermal_set_cur_state,
};

static int
mlxsw_thermal_module_tz_init(struct mlxsw_thermal_module *module_tz)
{
	char tz_name[THERMAL_NAME_LENGTH];
	int err;

	if (module_tz->slot_index)
		snprintf(tz_name, sizeof(tz_name), "mlxsw-lc%d-module%d",
			 module_tz->slot_index, module_tz->module + 1);
	else
		snprintf(tz_name, sizeof(tz_name), "mlxsw-module%d",
			 module_tz->module + 1);
	module_tz->tzdev = thermal_zone_device_register_with_trips(tz_name,
							module_tz->trips,
							MLXSW_THERMAL_NUM_TRIPS,
							module_tz,
							&mlxsw_thermal_module_ops,
							&mlxsw_thermal_params,
							0,
							module_tz->parent->polling_delay);
	if (IS_ERR(module_tz->tzdev)) {
		err = PTR_ERR(module_tz->tzdev);
		return err;
	}

	err = thermal_zone_device_enable(module_tz->tzdev);
	if (err)
		thermal_zone_device_unregister(module_tz->tzdev);

	return err;
}

static void mlxsw_thermal_module_tz_fini(struct thermal_zone_device *tzdev)
{
	thermal_zone_device_unregister(tzdev);
}

static void
mlxsw_thermal_module_init(struct device *dev, struct mlxsw_core *core,
			  struct mlxsw_thermal *thermal,
			  struct mlxsw_thermal_area *area, u8 module)
{
	struct mlxsw_thermal_module *module_tz;

	module_tz = &area->tz_module_arr[module];
	/* Skip if parent is already set (case of port split). */
	if (module_tz->parent)
		return;
	module_tz->module = module;
	module_tz->slot_index = area->slot_index;
	module_tz->parent = thermal;
	BUILD_BUG_ON(ARRAY_SIZE(default_thermal_module_trips) !=
		     MLXSW_THERMAL_NUM_TRIPS);
	memcpy(module_tz->trips, default_thermal_module_trips,
	       sizeof(thermal->trips));
	memcpy(module_tz->cooling_states, default_cooling_states,
	       sizeof(thermal->cooling_states));
}

static void mlxsw_thermal_module_fini(struct mlxsw_thermal_module *module_tz)
{
	if (module_tz && module_tz->tzdev) {
		mlxsw_thermal_module_tz_fini(module_tz->tzdev);
		module_tz->tzdev = NULL;
		module_tz->parent = NULL;
	}
}

static int
mlxsw_thermal_modules_init(struct device *dev, struct mlxsw_core *core,
			   struct mlxsw_thermal *thermal,
			   struct mlxsw_thermal_area *area)
{
	struct mlxsw_thermal_module *module_tz;
	char mgpir_pl[MLXSW_REG_MGPIR_LEN];
	int i, err;

	mlxsw_reg_mgpir_pack(mgpir_pl, area->slot_index);
	err = mlxsw_reg_query(core, MLXSW_REG(mgpir), mgpir_pl);
	if (err)
		return err;

	mlxsw_reg_mgpir_unpack(mgpir_pl, NULL, NULL, NULL,
			       &area->tz_module_num, NULL);

	/* For modular system module counter could be zero. */
	if (!area->tz_module_num)
		return 0;

	area->tz_module_arr = kcalloc(area->tz_module_num,
				      sizeof(*area->tz_module_arr),
				      GFP_KERNEL);
	if (!area->tz_module_arr)
		return -ENOMEM;

	for (i = 0; i < area->tz_module_num; i++)
		mlxsw_thermal_module_init(dev, core, thermal, area, i);

	for (i = 0; i < area->tz_module_num; i++) {
		module_tz = &area->tz_module_arr[i];
		if (!module_tz->parent)
			continue;
		err = mlxsw_thermal_module_tz_init(module_tz);
		if (err)
			goto err_thermal_module_tz_init;
	}

	return 0;

err_thermal_module_tz_init:
	for (i = area->tz_module_num - 1; i >= 0; i--)
		mlxsw_thermal_module_fini(&area->tz_module_arr[i]);
	kfree(area->tz_module_arr);
	return err;
}

static void
mlxsw_thermal_modules_fini(struct mlxsw_thermal *thermal,
			   struct mlxsw_thermal_area *area)
{
	int i;

	for (i = area->tz_module_num - 1; i >= 0; i--)
		mlxsw_thermal_module_fini(&area->tz_module_arr[i]);
	kfree(area->tz_module_arr);
}

static int
mlxsw_thermal_gearbox_tz_init(struct mlxsw_thermal_module *gearbox_tz)
{
	char tz_name[40];
	int ret;

	if (gearbox_tz->slot_index)
		snprintf(tz_name, sizeof(tz_name), "mlxsw-lc%d-gearbox%d",
			 gearbox_tz->slot_index, gearbox_tz->module + 1);
	else
		snprintf(tz_name, sizeof(tz_name), "mlxsw-gearbox%d",
			 gearbox_tz->module + 1);
	gearbox_tz->tzdev = thermal_zone_device_register_with_trips(tz_name,
						gearbox_tz->trips,
						MLXSW_THERMAL_NUM_TRIPS,
						gearbox_tz,
						&mlxsw_thermal_gearbox_ops,
						&mlxsw_thermal_params, 0,
						gearbox_tz->parent->polling_delay);
	if (IS_ERR(gearbox_tz->tzdev))
		return PTR_ERR(gearbox_tz->tzdev);

	ret = thermal_zone_device_enable(gearbox_tz->tzdev);
	if (ret)
		thermal_zone_device_unregister(gearbox_tz->tzdev);

	return ret;
}

static void
mlxsw_thermal_gearbox_tz_fini(struct mlxsw_thermal_module *gearbox_tz)
{
	thermal_zone_device_unregister(gearbox_tz->tzdev);
}

static int
mlxsw_thermal_gearboxes_init(struct device *dev, struct mlxsw_core *core,
			     struct mlxsw_thermal *thermal,
			     struct mlxsw_thermal_area *area)
{
	enum mlxsw_reg_mgpir_device_type device_type;
	struct mlxsw_thermal_module *gearbox_tz;
	char mgpir_pl[MLXSW_REG_MGPIR_LEN];
	u8 gbox_num;
	int i;
	int err;

	mlxsw_reg_mgpir_pack(mgpir_pl, area->slot_index);
	err = mlxsw_reg_query(core, MLXSW_REG(mgpir), mgpir_pl);
	if (err)
		return err;

	mlxsw_reg_mgpir_unpack(mgpir_pl, &gbox_num, &device_type, NULL,
			       NULL, NULL);
	if (device_type != MLXSW_REG_MGPIR_DEVICE_TYPE_GEARBOX_DIE ||
	    !gbox_num)
		return 0;

	area->tz_gearbox_num = gbox_num;
	area->tz_gearbox_arr = kcalloc(area->tz_gearbox_num,
				       sizeof(*area->tz_gearbox_arr),
				       GFP_KERNEL);
	if (!area->tz_gearbox_arr)
		return -ENOMEM;

	for (i = 0; i < area->tz_gearbox_num; i++) {
		gearbox_tz = &area->tz_gearbox_arr[i];
		memcpy(gearbox_tz->trips, default_thermal_trips,
		       sizeof(thermal->trips));
		memcpy(gearbox_tz->cooling_states, default_cooling_states,
		       sizeof(thermal->cooling_states));
		gearbox_tz->module = i;
		gearbox_tz->parent = thermal;
		gearbox_tz->slot_index = area->slot_index;
		err = mlxsw_thermal_gearbox_tz_init(gearbox_tz);
		if (err)
			goto err_thermal_gearbox_tz_init;
	}

	return 0;

err_thermal_gearbox_tz_init:
	for (i--; i >= 0; i--)
		mlxsw_thermal_gearbox_tz_fini(&area->tz_gearbox_arr[i]);
	kfree(area->tz_gearbox_arr);
	return err;
}

static void
mlxsw_thermal_gearboxes_fini(struct mlxsw_thermal *thermal,
			     struct mlxsw_thermal_area *area)
{
	int i;

	for (i = area->tz_gearbox_num - 1; i >= 0; i--)
		mlxsw_thermal_gearbox_tz_fini(&area->tz_gearbox_arr[i]);
	kfree(area->tz_gearbox_arr);
}

static void
mlxsw_thermal_got_active(struct mlxsw_core *mlxsw_core, u8 slot_index,
			 void *priv)
{
	struct mlxsw_thermal *thermal = priv;
	struct mlxsw_thermal_area *linecard;
	int err;

	linecard = &thermal->line_cards[slot_index];

	if (linecard->active)
		return;

	linecard->slot_index = slot_index;
	err = mlxsw_thermal_modules_init(thermal->bus_info->dev, thermal->core,
					 thermal, linecard);
	if (err) {
		dev_err(thermal->bus_info->dev, "Failed to configure thermal objects for line card modules in slot %d\n",
			slot_index);
		return;
	}

	err = mlxsw_thermal_gearboxes_init(thermal->bus_info->dev,
					   thermal->core, thermal, linecard);
	if (err) {
		dev_err(thermal->bus_info->dev, "Failed to configure thermal objects for line card gearboxes in slot %d\n",
			slot_index);
		goto err_thermal_linecard_gearboxes_init;
	}

	linecard->active = true;

	return;

err_thermal_linecard_gearboxes_init:
	mlxsw_thermal_modules_fini(thermal, linecard);
}

static void
mlxsw_thermal_got_inactive(struct mlxsw_core *mlxsw_core, u8 slot_index,
			   void *priv)
{
	struct mlxsw_thermal *thermal = priv;
	struct mlxsw_thermal_area *linecard;

	linecard = &thermal->line_cards[slot_index];
	if (!linecard->active)
		return;
	linecard->active = false;
	mlxsw_thermal_gearboxes_fini(thermal, linecard);
	mlxsw_thermal_modules_fini(thermal, linecard);
}

static struct mlxsw_linecards_event_ops mlxsw_thermal_event_ops = {
	.got_active = mlxsw_thermal_got_active,
	.got_inactive = mlxsw_thermal_got_inactive,
};

int mlxsw_thermal_init(struct mlxsw_core *core,
		       const struct mlxsw_bus_info *bus_info,
		       struct mlxsw_thermal **p_thermal)
{
	char mfcr_pl[MLXSW_REG_MFCR_LEN] = { 0 };
	enum mlxsw_reg_mfcr_pwm_frequency freq;
	struct device *dev = bus_info->dev;
	char mgpir_pl[MLXSW_REG_MGPIR_LEN];
	struct mlxsw_thermal *thermal;
	u8 pwm_active, num_of_slots;
	u16 tacho_active;
	int err, i;

	mlxsw_reg_mgpir_pack(mgpir_pl, 0);
	err = mlxsw_reg_query(core, MLXSW_REG(mgpir), mgpir_pl);
	if (err)
		return err;

	mlxsw_reg_mgpir_unpack(mgpir_pl, NULL, NULL, NULL, NULL,
			       &num_of_slots);

	thermal = kzalloc(struct_size(thermal, line_cards, num_of_slots + 1),
			  GFP_KERNEL);
	if (!thermal)
		return -ENOMEM;

	thermal->core = core;
	thermal->bus_info = bus_info;
	memcpy(thermal->trips, default_thermal_trips, sizeof(thermal->trips));
	memcpy(thermal->cooling_states, default_cooling_states, sizeof(thermal->cooling_states));
	thermal->line_cards[0].slot_index = 0;

	err = mlxsw_reg_query(thermal->core, MLXSW_REG(mfcr), mfcr_pl);
	if (err) {
		dev_err(dev, "Failed to probe PWMs\n");
		goto err_reg_query;
	}
	mlxsw_reg_mfcr_unpack(mfcr_pl, &freq, &tacho_active, &pwm_active);

	for (i = 0; i < MLXSW_MFCR_TACHOS_MAX; i++) {
		if (tacho_active & BIT(i)) {
			char mfsl_pl[MLXSW_REG_MFSL_LEN];

			mlxsw_reg_mfsl_pack(mfsl_pl, i, 0, 0);

			/* We need to query the register to preserve maximum */
			err = mlxsw_reg_query(thermal->core, MLXSW_REG(mfsl),
					      mfsl_pl);
			if (err)
				goto err_reg_query;

			/* set the minimal RPMs to 0 */
			mlxsw_reg_mfsl_tach_min_set(mfsl_pl, 0);
			err = mlxsw_reg_write(thermal->core, MLXSW_REG(mfsl),
					      mfsl_pl);
			if (err)
				goto err_reg_write;
		}
	}
	for (i = 0; i < MLXSW_MFCR_PWMS_MAX; i++) {
		if (pwm_active & BIT(i)) {
			struct mlxsw_thermal_cooling_device *mlxsw_cdev;
			struct thermal_cooling_device *cdev;

			mlxsw_cdev = &thermal->cdevs[i];
			mlxsw_cdev->thermal = thermal;
			mlxsw_cdev->idx = i;
			cdev = thermal_cooling_device_register("mlxsw_fan",
							       mlxsw_cdev,
							       &mlxsw_cooling_ops);
			if (IS_ERR(cdev)) {
				err = PTR_ERR(cdev);
				dev_err(dev, "Failed to register cooling device\n");
				goto err_thermal_cooling_device_register;
			}
			mlxsw_cdev->cdev = cdev;
		}
	}

	thermal->polling_delay = bus_info->low_frequency ?
				 MLXSW_THERMAL_SLOW_POLL_INT :
				 MLXSW_THERMAL_POLL_INT;

	thermal->tzdev = thermal_zone_device_register_with_trips("mlxsw",
						      thermal->trips,
						      MLXSW_THERMAL_NUM_TRIPS,
						      thermal,
						      &mlxsw_thermal_ops,
						      &mlxsw_thermal_params, 0,
						      thermal->polling_delay);
	if (IS_ERR(thermal->tzdev)) {
		err = PTR_ERR(thermal->tzdev);
		dev_err(dev, "Failed to register thermal zone\n");
		goto err_thermal_zone_device_register;
	}

	err = mlxsw_thermal_modules_init(dev, core, thermal,
					 &thermal->line_cards[0]);
	if (err)
		goto err_thermal_modules_init;

	err = mlxsw_thermal_gearboxes_init(dev, core, thermal,
					   &thermal->line_cards[0]);
	if (err)
		goto err_thermal_gearboxes_init;

	err = mlxsw_linecards_event_ops_register(core,
						 &mlxsw_thermal_event_ops,
						 thermal);
	if (err)
		goto err_linecards_event_ops_register;

	err = thermal_zone_device_enable(thermal->tzdev);
	if (err)
		goto err_thermal_zone_device_enable;

	thermal->line_cards[0].active = true;
	*p_thermal = thermal;
	return 0;

err_thermal_zone_device_enable:
	mlxsw_linecards_event_ops_unregister(thermal->core,
					     &mlxsw_thermal_event_ops,
					     thermal);
err_linecards_event_ops_register:
	mlxsw_thermal_gearboxes_fini(thermal, &thermal->line_cards[0]);
err_thermal_gearboxes_init:
	mlxsw_thermal_modules_fini(thermal, &thermal->line_cards[0]);
err_thermal_modules_init:
	if (thermal->tzdev) {
		thermal_zone_device_unregister(thermal->tzdev);
		thermal->tzdev = NULL;
	}
err_thermal_zone_device_register:
err_thermal_cooling_device_register:
	for (i = 0; i < MLXSW_MFCR_PWMS_MAX; i++)
		thermal_cooling_device_unregister(thermal->cdevs[i].cdev);
err_reg_write:
err_reg_query:
	kfree(thermal);
	return err;
}

void mlxsw_thermal_fini(struct mlxsw_thermal *thermal)
{
	int i;

	thermal->line_cards[0].active = false;
	mlxsw_linecards_event_ops_unregister(thermal->core,
					     &mlxsw_thermal_event_ops,
					     thermal);
	mlxsw_thermal_gearboxes_fini(thermal, &thermal->line_cards[0]);
	mlxsw_thermal_modules_fini(thermal, &thermal->line_cards[0]);
	if (thermal->tzdev) {
		thermal_zone_device_unregister(thermal->tzdev);
		thermal->tzdev = NULL;
	}

	for (i = 0; i < MLXSW_MFCR_PWMS_MAX; i++)
		thermal_cooling_device_unregister(thermal->cdevs[i].cdev);

	kfree(thermal);
}
