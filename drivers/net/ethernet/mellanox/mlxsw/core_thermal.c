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

#include "core.h"

#define MLXSW_THERMAL_POLL_INT	1000	/* ms */
#define MLXSW_THERMAL_MAX_TEMP	110000	/* 110C */
#define MLXSW_THERMAL_MAX_STATE	10
#define MLXSW_THERMAL_MAX_DUTY	255
/* Minimum and maximum fan allowed speed in percent: from 20% to 100%. Values
 * MLXSW_THERMAL_MAX_STATE + x, where x is between 2 and 10 are used for
 * setting fan speed dynamic minimum. For example, if value is set to 14 (40%)
 * cooling levels vector will be set to 4, 4, 4, 4, 4, 5, 6, 7, 8, 9, 10 to
 * introduce PWM speed in percent: 40, 40, 40, 40, 40, 50, 60. 70, 80, 90, 100.
 */
#define MLXSW_THERMAL_SPEED_MIN		(MLXSW_THERMAL_MAX_STATE + 2)
#define MLXSW_THERMAL_SPEED_MAX		(MLXSW_THERMAL_MAX_STATE * 2)
#define MLXSW_THERMAL_SPEED_MIN_LEVEL	2		/* 20% */

struct mlxsw_thermal_trip {
	int	type;
	int	temp;
	int	min_state;
	int	max_state;
};

static const struct mlxsw_thermal_trip default_thermal_trips[] = {
	{	/* In range - 0-40% PWM */
		.type		= THERMAL_TRIP_ACTIVE,
		.temp		= 75000,
		.min_state	= 0,
		.max_state	= (4 * MLXSW_THERMAL_MAX_STATE) / 10,
	},
	{	/* High - 40-100% PWM */
		.type		= THERMAL_TRIP_ACTIVE,
		.temp		= 80000,
		.min_state	= (4 * MLXSW_THERMAL_MAX_STATE) / 10,
		.max_state	= MLXSW_THERMAL_MAX_STATE,
	},
	{
		/* Very high - 100% PWM */
		.type		= THERMAL_TRIP_ACTIVE,
		.temp		= 85000,
		.min_state	= MLXSW_THERMAL_MAX_STATE,
		.max_state	= MLXSW_THERMAL_MAX_STATE,
	},
	{	/* Warning */
		.type		= THERMAL_TRIP_HOT,
		.temp		= 105000,
		.min_state	= MLXSW_THERMAL_MAX_STATE,
		.max_state	= MLXSW_THERMAL_MAX_STATE,
	},
	{	/* Critical - soft poweroff */
		.type		= THERMAL_TRIP_CRITICAL,
		.temp		= MLXSW_THERMAL_MAX_TEMP,
		.min_state	= MLXSW_THERMAL_MAX_STATE,
		.max_state	= MLXSW_THERMAL_MAX_STATE,
	}
};

#define MLXSW_THERMAL_NUM_TRIPS	ARRAY_SIZE(default_thermal_trips)

/* Make sure all trips are writable */
#define MLXSW_THERMAL_TRIP_MASK	(BIT(MLXSW_THERMAL_NUM_TRIPS) - 1)

struct mlxsw_thermal {
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	struct thermal_zone_device *tzdev;
	struct thermal_cooling_device *cdevs[MLXSW_MFCR_PWMS_MAX];
	u8 cooling_levels[MLXSW_THERMAL_MAX_STATE + 1];
	struct mlxsw_thermal_trip trips[MLXSW_THERMAL_NUM_TRIPS];
	enum thermal_device_mode mode;
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
		if (thermal->cdevs[i] == cdev)
			return i;

	return -ENODEV;
}

static int mlxsw_thermal_bind(struct thermal_zone_device *tzdev,
			      struct thermal_cooling_device *cdev)
{
	struct mlxsw_thermal *thermal = tzdev->devdata;
	struct device *dev = thermal->bus_info->dev;
	int i, err;

	/* If the cooling device is one of ours bind it */
	if (mlxsw_get_cooling_device_idx(thermal, cdev) < 0)
		return 0;

	for (i = 0; i < MLXSW_THERMAL_NUM_TRIPS; i++) {
		const struct mlxsw_thermal_trip *trip = &thermal->trips[i];

		err = thermal_zone_bind_cooling_device(tzdev, i, cdev,
						       trip->max_state,
						       trip->min_state,
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
	struct mlxsw_thermal *thermal = tzdev->devdata;
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

static int mlxsw_thermal_get_mode(struct thermal_zone_device *tzdev,
				  enum thermal_device_mode *mode)
{
	struct mlxsw_thermal *thermal = tzdev->devdata;

	*mode = thermal->mode;

	return 0;
}

static int mlxsw_thermal_set_mode(struct thermal_zone_device *tzdev,
				  enum thermal_device_mode mode)
{
	struct mlxsw_thermal *thermal = tzdev->devdata;

	mutex_lock(&tzdev->lock);

	if (mode == THERMAL_DEVICE_ENABLED)
		tzdev->polling_delay = MLXSW_THERMAL_POLL_INT;
	else
		tzdev->polling_delay = 0;

	mutex_unlock(&tzdev->lock);

	thermal->mode = mode;
	thermal_zone_device_update(tzdev, THERMAL_EVENT_UNSPECIFIED);

	return 0;
}

static int mlxsw_thermal_get_temp(struct thermal_zone_device *tzdev,
				  int *p_temp)
{
	struct mlxsw_thermal *thermal = tzdev->devdata;
	struct device *dev = thermal->bus_info->dev;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	unsigned int temp;
	int err;

	mlxsw_reg_mtmp_pack(mtmp_pl, 0, false, false);

	err = mlxsw_reg_query(thermal->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err) {
		dev_err(dev, "Failed to query temp sensor\n");
		return err;
	}
	mlxsw_reg_mtmp_unpack(mtmp_pl, &temp, NULL, NULL);

	*p_temp = (int) temp;
	return 0;
}

static int mlxsw_thermal_get_trip_type(struct thermal_zone_device *tzdev,
				       int trip,
				       enum thermal_trip_type *p_type)
{
	struct mlxsw_thermal *thermal = tzdev->devdata;

	if (trip < 0 || trip >= MLXSW_THERMAL_NUM_TRIPS)
		return -EINVAL;

	*p_type = thermal->trips[trip].type;
	return 0;
}

static int mlxsw_thermal_get_trip_temp(struct thermal_zone_device *tzdev,
				       int trip, int *p_temp)
{
	struct mlxsw_thermal *thermal = tzdev->devdata;

	if (trip < 0 || trip >= MLXSW_THERMAL_NUM_TRIPS)
		return -EINVAL;

	*p_temp = thermal->trips[trip].temp;
	return 0;
}

static int mlxsw_thermal_set_trip_temp(struct thermal_zone_device *tzdev,
				       int trip, int temp)
{
	struct mlxsw_thermal *thermal = tzdev->devdata;

	if (trip < 0 || trip >= MLXSW_THERMAL_NUM_TRIPS ||
	    temp > MLXSW_THERMAL_MAX_TEMP)
		return -EINVAL;

	thermal->trips[trip].temp = temp;
	return 0;
}

static struct thermal_zone_device_ops mlxsw_thermal_ops = {
	.bind = mlxsw_thermal_bind,
	.unbind = mlxsw_thermal_unbind,
	.get_mode = mlxsw_thermal_get_mode,
	.set_mode = mlxsw_thermal_set_mode,
	.get_temp = mlxsw_thermal_get_temp,
	.get_trip_type	= mlxsw_thermal_get_trip_type,
	.get_trip_temp	= mlxsw_thermal_get_trip_temp,
	.set_trip_temp	= mlxsw_thermal_set_trip_temp,
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
	struct mlxsw_thermal *thermal = cdev->devdata;
	struct device *dev = thermal->bus_info->dev;
	char mfsc_pl[MLXSW_REG_MFSC_LEN];
	int err, idx;
	u8 duty;

	idx = mlxsw_get_cooling_device_idx(thermal, cdev);
	if (idx < 0)
		return idx;

	mlxsw_reg_mfsc_pack(mfsc_pl, idx, 0);
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
	struct mlxsw_thermal *thermal = cdev->devdata;
	struct device *dev = thermal->bus_info->dev;
	char mfsc_pl[MLXSW_REG_MFSC_LEN];
	unsigned long cur_state, i;
	int idx;
	u8 duty;
	int err;

	idx = mlxsw_get_cooling_device_idx(thermal, cdev);
	if (idx < 0)
		return idx;

	/* Verify if this request is for changing allowed fan dynamical
	 * minimum. If it is - update cooling levels accordingly and update
	 * state, if current state is below the newly requested minimum state.
	 * For example, if current state is 5, and minimal state is to be
	 * changed from 4 to 6, thermal->cooling_levels[0 to 5] will be changed
	 * all from 4 to 6. And state 5 (thermal->cooling_levels[4]) should be
	 * overwritten.
	 */
	if (state >= MLXSW_THERMAL_SPEED_MIN &&
	    state <= MLXSW_THERMAL_SPEED_MAX) {
		state -= MLXSW_THERMAL_MAX_STATE;
		for (i = 0; i <= MLXSW_THERMAL_MAX_STATE; i++)
			thermal->cooling_levels[i] = max(state, i);

		mlxsw_reg_mfsc_pack(mfsc_pl, idx, 0);
		err = mlxsw_reg_query(thermal->core, MLXSW_REG(mfsc), mfsc_pl);
		if (err)
			return err;

		duty = mlxsw_reg_mfsc_pwm_duty_cycle_get(mfsc_pl);
		cur_state = mlxsw_duty_to_state(duty);

		/* If current fan state is lower than requested dynamical
		 * minimum, increase fan speed up to dynamical minimum.
		 */
		if (state < cur_state)
			return 0;

		state = cur_state;
	}

	if (state > MLXSW_THERMAL_MAX_STATE)
		return -EINVAL;

	/* Normalize the state to the valid speed range. */
	state = thermal->cooling_levels[state];
	mlxsw_reg_mfsc_pack(mfsc_pl, idx, mlxsw_state_to_duty(state));
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

int mlxsw_thermal_init(struct mlxsw_core *core,
		       const struct mlxsw_bus_info *bus_info,
		       struct mlxsw_thermal **p_thermal)
{
	char mfcr_pl[MLXSW_REG_MFCR_LEN] = { 0 };
	enum mlxsw_reg_mfcr_pwm_frequency freq;
	struct device *dev = bus_info->dev;
	struct mlxsw_thermal *thermal;
	u16 tacho_active;
	u8 pwm_active;
	int err, i;

	thermal = devm_kzalloc(dev, sizeof(*thermal),
			       GFP_KERNEL);
	if (!thermal)
		return -ENOMEM;

	thermal->core = core;
	thermal->bus_info = bus_info;
	memcpy(thermal->trips, default_thermal_trips, sizeof(thermal->trips));

	err = mlxsw_reg_query(thermal->core, MLXSW_REG(mfcr), mfcr_pl);
	if (err) {
		dev_err(dev, "Failed to probe PWMs\n");
		goto err_free_thermal;
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
				goto err_free_thermal;

			/* set the minimal RPMs to 0 */
			mlxsw_reg_mfsl_tach_min_set(mfsl_pl, 0);
			err = mlxsw_reg_write(thermal->core, MLXSW_REG(mfsl),
					      mfsl_pl);
			if (err)
				goto err_free_thermal;
		}
	}
	for (i = 0; i < MLXSW_MFCR_PWMS_MAX; i++) {
		if (pwm_active & BIT(i)) {
			struct thermal_cooling_device *cdev;

			cdev = thermal_cooling_device_register("Fan", thermal,
							&mlxsw_cooling_ops);
			if (IS_ERR(cdev)) {
				err = PTR_ERR(cdev);
				dev_err(dev, "Failed to register cooling device\n");
				goto err_unreg_cdevs;
			}
			thermal->cdevs[i] = cdev;
		}
	}

	/* Initialize cooling levels per PWM state. */
	for (i = 0; i < MLXSW_THERMAL_MAX_STATE; i++)
		thermal->cooling_levels[i] = max(MLXSW_THERMAL_SPEED_MIN_LEVEL,
						 i);

	thermal->tzdev = thermal_zone_device_register("mlxsw",
						      MLXSW_THERMAL_NUM_TRIPS,
						      MLXSW_THERMAL_TRIP_MASK,
						      thermal,
						      &mlxsw_thermal_ops,
						      NULL, 0,
						      MLXSW_THERMAL_POLL_INT);
	if (IS_ERR(thermal->tzdev)) {
		err = PTR_ERR(thermal->tzdev);
		dev_err(dev, "Failed to register thermal zone\n");
		goto err_unreg_cdevs;
	}

	thermal->mode = THERMAL_DEVICE_ENABLED;
	*p_thermal = thermal;
	return 0;
err_unreg_cdevs:
	for (i = 0; i < MLXSW_MFCR_PWMS_MAX; i++)
		if (thermal->cdevs[i])
			thermal_cooling_device_unregister(thermal->cdevs[i]);
err_free_thermal:
	devm_kfree(dev, thermal);
	return err;
}

void mlxsw_thermal_fini(struct mlxsw_thermal *thermal)
{
	int i;

	if (thermal->tzdev) {
		thermal_zone_device_unregister(thermal->tzdev);
		thermal->tzdev = NULL;
	}

	for (i = 0; i < MLXSW_MFCR_PWMS_MAX; i++) {
		if (thermal->cdevs[i]) {
			thermal_cooling_device_unregister(thermal->cdevs[i]);
			thermal->cdevs[i] = NULL;
		}
	}

	devm_kfree(thermal->bus_info->dev, thermal);
}
