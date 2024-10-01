// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// Copyright (c) 2018 Mellanox Technologies. All rights reserved.
// Copyright (c) 2018 Vadim Pasternak <vadimp@mellanox.com>

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/platform_data/mlxreg.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

#define MLXREG_FAN_MAX_TACHO		24
#define MLXREG_FAN_MAX_PWM		4
#define MLXREG_FAN_PWM_NOT_CONNECTED	0xff
#define MLXREG_FAN_MAX_STATE		10
#define MLXREG_FAN_MIN_DUTY		51	/* 20% */
#define MLXREG_FAN_MAX_DUTY		255	/* 100% */
#define MLXREG_FAN_SPEED_MIN_LEVEL		2	/* 20 percent */
#define MLXREG_FAN_TACHO_SAMPLES_PER_PULSE_DEF	44
#define MLXREG_FAN_TACHO_DIV_MIN		283
#define MLXREG_FAN_TACHO_DIV_DEF		(MLXREG_FAN_TACHO_DIV_MIN * 4)
#define MLXREG_FAN_TACHO_DIV_SCALE_MAX	64
/*
 * FAN datasheet defines the formula for RPM calculations as RPM = 15/t-high.
 * The logic in a programmable device measures the time t-high by sampling the
 * tachometer every t-sample (with the default value 11.32 uS) and increment
 * a counter (N) as long as the pulse has not change:
 * RPM = 15 / (t-sample * (K + Regval)), where:
 * Regval: is the value read from the programmable device register;
 *  - 0xff - represents tachometer fault;
 *  - 0xfe - represents tachometer minimum value , which is 4444 RPM;
 *  - 0x00 - represents tachometer maximum value , which is 300000 RPM;
 * K: is 44 and it represents the minimum allowed samples per pulse;
 * N: is equal K + Regval;
 * In order to calculate RPM from the register value the following formula is
 * used: RPM = 15 / ((Regval + K) * 11.32) * 10^(-6)), which in  the
 * default case is modified to:
 * RPM = 15000000 * 100 / ((Regval + 44) * 1132);
 * - for Regval 0x00, RPM will be 15000000 * 100 / (44 * 1132) = 30115;
 * - for Regval 0xfe, RPM will be 15000000 * 100 / ((254 + 44) * 1132) = 4446;
 * In common case the formula is modified to:
 * RPM = 15000000 * 100 / ((Regval + samples) * divider).
 */
#define MLXREG_FAN_GET_RPM(rval, d, s)	(DIV_ROUND_CLOSEST(15000000 * 100, \
					 ((rval) + (s)) * (d)))
#define MLXREG_FAN_GET_FAULT(val, mask) ((val) == (mask))
#define MLXREG_FAN_PWM_DUTY2STATE(duty)	(DIV_ROUND_CLOSEST((duty) *	\
					 MLXREG_FAN_MAX_STATE,		\
					 MLXREG_FAN_MAX_DUTY))
#define MLXREG_FAN_PWM_STATE2DUTY(stat)	(DIV_ROUND_CLOSEST((stat) *	\
					 MLXREG_FAN_MAX_DUTY,		\
					 MLXREG_FAN_MAX_STATE))

struct mlxreg_fan;

/*
 * struct mlxreg_fan_tacho - tachometer data (internal use):
 *
 * @connected: indicates if tachometer is connected;
 * @reg: register offset;
 * @mask: fault mask;
 * @prsnt: present register offset;
 */
struct mlxreg_fan_tacho {
	bool connected;
	u32 reg;
	u32 mask;
	u32 prsnt;
};

/*
 * struct mlxreg_fan_pwm - PWM data (internal use):
 *
 * @fan: private data;
 * @connected: indicates if PWM is connected;
 * @reg: register offset;
 * @cooling: cooling device levels;
 * @last_hwmon_state: last cooling state set by hwmon subsystem;
 * @last_thermal_state: last cooling state set by thermal subsystem;
 * @cdev: cooling device;
 */
struct mlxreg_fan_pwm {
	struct mlxreg_fan *fan;
	bool connected;
	u32 reg;
	unsigned long last_hwmon_state;
	unsigned long last_thermal_state;
	struct thermal_cooling_device *cdev;
};

/*
 * struct mlxreg_fan - private data (internal use):
 *
 * @dev: basic device;
 * @regmap: register map of parent device;
 * @tacho: tachometer data;
 * @pwm: PWM data;
 * @tachos_per_drwr - number of tachometers per drawer;
 * @samples: minimum allowed samples per pulse;
 * @divider: divider value for tachometer RPM calculation;
 */
struct mlxreg_fan {
	struct device *dev;
	void *regmap;
	struct mlxreg_core_platform_data *pdata;
	struct mlxreg_fan_tacho tacho[MLXREG_FAN_MAX_TACHO];
	struct mlxreg_fan_pwm pwm[MLXREG_FAN_MAX_PWM];
	int tachos_per_drwr;
	int samples;
	int divider;
};

static int mlxreg_fan_set_cur_state(struct thermal_cooling_device *cdev,
				    unsigned long state);

static int
mlxreg_fan_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
		int channel, long *val)
{
	struct mlxreg_fan *fan = dev_get_drvdata(dev);
	struct mlxreg_fan_tacho *tacho;
	struct mlxreg_fan_pwm *pwm;
	u32 regval;
	int err;

	switch (type) {
	case hwmon_fan:
		tacho = &fan->tacho[channel];
		switch (attr) {
		case hwmon_fan_input:
			/*
			 * Check FAN presence: FAN related bit in presence register is one,
			 * if FAN is physically connected, zero - otherwise.
			 */
			if (tacho->prsnt && fan->tachos_per_drwr) {
				err = regmap_read(fan->regmap, tacho->prsnt, &regval);
				if (err)
					return err;

				/*
				 * Map channel to presence bit - drawer can be equipped with
				 * one or few FANs, while presence is indicated per drawer.
				 */
				if (BIT(channel / fan->tachos_per_drwr) & regval) {
					/* FAN is not connected - return zero for FAN speed. */
					*val = 0;
					return 0;
				}
			}

			err = regmap_read(fan->regmap, tacho->reg, &regval);
			if (err)
				return err;

			if (MLXREG_FAN_GET_FAULT(regval, tacho->mask)) {
				/* FAN is broken - return zero for FAN speed. */
				*val = 0;
				return 0;
			}

			*val = MLXREG_FAN_GET_RPM(regval, fan->divider,
						  fan->samples);
			break;

		case hwmon_fan_fault:
			err = regmap_read(fan->regmap, tacho->reg, &regval);
			if (err)
				return err;

			*val = MLXREG_FAN_GET_FAULT(regval, tacho->mask);
			break;

		default:
			return -EOPNOTSUPP;
		}
		break;

	case hwmon_pwm:
		pwm = &fan->pwm[channel];
		switch (attr) {
		case hwmon_pwm_input:
			err = regmap_read(fan->regmap, pwm->reg, &regval);
			if (err)
				return err;

			*val = regval;
			break;

		default:
			return -EOPNOTSUPP;
		}
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
mlxreg_fan_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
		 int channel, long val)
{
	struct mlxreg_fan *fan = dev_get_drvdata(dev);
	struct mlxreg_fan_pwm *pwm;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			if (val < MLXREG_FAN_MIN_DUTY ||
			    val > MLXREG_FAN_MAX_DUTY)
				return -EINVAL;
			pwm = &fan->pwm[channel];
			/* If thermal is configured - handle PWM limit setting. */
			if (IS_REACHABLE(CONFIG_THERMAL)) {
				pwm->last_hwmon_state = MLXREG_FAN_PWM_DUTY2STATE(val);
				/*
				 * Update PWM only in case requested state is not less than the
				 * last thermal state.
				 */
				if (pwm->last_hwmon_state >= pwm->last_thermal_state)
					return mlxreg_fan_set_cur_state(pwm->cdev,
									pwm->last_hwmon_state);
				return 0;
			}
			return regmap_write(fan->regmap, pwm->reg, val);
		default:
			return -EOPNOTSUPP;
		}
		break;

	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static umode_t
mlxreg_fan_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr,
		      int channel)
{
	switch (type) {
	case hwmon_fan:
		if (!(((struct mlxreg_fan *)data)->tacho[channel].connected))
			return 0;

		switch (attr) {
		case hwmon_fan_input:
		case hwmon_fan_fault:
			return 0444;
		default:
			break;
		}
		break;

	case hwmon_pwm:
		if (!(((struct mlxreg_fan *)data)->pwm[channel].connected))
			return 0;

		switch (attr) {
		case hwmon_pwm_input:
			return 0644;
		default:
			break;
		}
		break;

	default:
		break;
	}

	return 0;
}

static char *mlxreg_fan_name[] = {
	"mlxreg_fan",
	"mlxreg_fan1",
	"mlxreg_fan2",
	"mlxreg_fan3",
};

static const struct hwmon_channel_info * const mlxreg_fan_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_ops mlxreg_fan_hwmon_hwmon_ops = {
	.is_visible = mlxreg_fan_is_visible,
	.read = mlxreg_fan_read,
	.write = mlxreg_fan_write,
};

static const struct hwmon_chip_info mlxreg_fan_hwmon_chip_info = {
	.ops = &mlxreg_fan_hwmon_hwmon_ops,
	.info = mlxreg_fan_hwmon_info,
};

static int mlxreg_fan_get_max_state(struct thermal_cooling_device *cdev,
				    unsigned long *state)
{
	*state = MLXREG_FAN_MAX_STATE;
	return 0;
}

static int mlxreg_fan_get_cur_state(struct thermal_cooling_device *cdev,
				    unsigned long *state)

{
	struct mlxreg_fan_pwm *pwm = cdev->devdata;
	struct mlxreg_fan *fan = pwm->fan;
	u32 regval;
	int err;

	err = regmap_read(fan->regmap, pwm->reg, &regval);
	if (err) {
		dev_err(fan->dev, "Failed to query PWM duty\n");
		return err;
	}

	*state = MLXREG_FAN_PWM_DUTY2STATE(regval);

	return 0;
}

static int mlxreg_fan_set_cur_state(struct thermal_cooling_device *cdev,
				    unsigned long state)

{
	struct mlxreg_fan_pwm *pwm = cdev->devdata;
	struct mlxreg_fan *fan = pwm->fan;
	int err;

	if (state > MLXREG_FAN_MAX_STATE)
		return -EINVAL;

	/* Save thermal state. */
	pwm->last_thermal_state = state;

	state = max_t(unsigned long, state, pwm->last_hwmon_state);
	err = regmap_write(fan->regmap, pwm->reg,
			   MLXREG_FAN_PWM_STATE2DUTY(state));
	if (err) {
		dev_err(fan->dev, "Failed to write PWM duty\n");
		return err;
	}
	return 0;
}

static const struct thermal_cooling_device_ops mlxreg_fan_cooling_ops = {
	.get_max_state	= mlxreg_fan_get_max_state,
	.get_cur_state	= mlxreg_fan_get_cur_state,
	.set_cur_state	= mlxreg_fan_set_cur_state,
};

static int mlxreg_fan_connect_verify(struct mlxreg_fan *fan,
				     struct mlxreg_core_data *data)
{
	u32 regval;
	int err;

	err = regmap_read(fan->regmap, data->capability, &regval);
	if (err) {
		dev_err(fan->dev, "Failed to query capability register 0x%08x\n",
			data->capability);
		return err;
	}

	return !!(regval & data->bit);
}

static int mlxreg_pwm_connect_verify(struct mlxreg_fan *fan,
				     struct mlxreg_core_data *data)
{
	u32 regval;
	int err;

	err = regmap_read(fan->regmap, data->reg, &regval);
	if (err) {
		dev_err(fan->dev, "Failed to query pwm register 0x%08x\n",
			data->reg);
		return err;
	}

	return regval != MLXREG_FAN_PWM_NOT_CONNECTED;
}

static int mlxreg_fan_speed_divider_get(struct mlxreg_fan *fan,
					struct mlxreg_core_data *data)
{
	u32 regval;
	int err;

	err = regmap_read(fan->regmap, data->capability, &regval);
	if (err) {
		dev_err(fan->dev, "Failed to query capability register 0x%08x\n",
			data->capability);
		return err;
	}

	/*
	 * Set divider value according to the capability register, in case it
	 * contains valid value. Otherwise use default value. The purpose of
	 * this validation is to protect against the old hardware, in which
	 * this register can return zero.
	 */
	if (regval > 0 && regval <= MLXREG_FAN_TACHO_DIV_SCALE_MAX)
		fan->divider = regval * MLXREG_FAN_TACHO_DIV_MIN;

	return 0;
}

static int mlxreg_fan_config(struct mlxreg_fan *fan,
			     struct mlxreg_core_platform_data *pdata)
{
	int tacho_num = 0, tacho_avail = 0, pwm_num = 0, i;
	struct mlxreg_core_data *data = pdata->data;
	bool configured = false;
	int err;

	fan->samples = MLXREG_FAN_TACHO_SAMPLES_PER_PULSE_DEF;
	fan->divider = MLXREG_FAN_TACHO_DIV_DEF;
	for (i = 0; i < pdata->counter; i++, data++) {
		if (strnstr(data->label, "tacho", sizeof(data->label))) {
			if (tacho_num == MLXREG_FAN_MAX_TACHO) {
				dev_err(fan->dev, "too many tacho entries: %s\n",
					data->label);
				return -EINVAL;
			}

			if (data->capability) {
				err = mlxreg_fan_connect_verify(fan, data);
				if (err < 0)
					return err;
				else if (!err) {
					tacho_num++;
					continue;
				}
			}

			fan->tacho[tacho_num].reg = data->reg;
			fan->tacho[tacho_num].mask = data->mask;
			fan->tacho[tacho_num].prsnt = data->reg_prsnt;
			fan->tacho[tacho_num++].connected = true;
			tacho_avail++;
		} else if (strnstr(data->label, "pwm", sizeof(data->label))) {
			if (pwm_num == MLXREG_FAN_MAX_TACHO) {
				dev_err(fan->dev, "too many pwm entries: %s\n",
					data->label);
				return -EINVAL;
			}

			/* Validate if more then one PWM is connected. */
			if (pwm_num) {
				err = mlxreg_pwm_connect_verify(fan, data);
				if (err < 0)
					return err;
				else if (!err)
					continue;
			}

			fan->pwm[pwm_num].reg = data->reg;
			fan->pwm[pwm_num].connected = true;
			pwm_num++;
		} else if (strnstr(data->label, "conf", sizeof(data->label))) {
			if (configured) {
				dev_err(fan->dev, "duplicate conf entry: %s\n",
					data->label);
				return -EINVAL;
			}
			/* Validate that conf parameters are not zeros. */
			if (!data->mask && !data->bit && !data->capability) {
				dev_err(fan->dev, "invalid conf entry params: %s\n",
					data->label);
				return -EINVAL;
			}
			if (data->capability) {
				err = mlxreg_fan_speed_divider_get(fan, data);
				if (err)
					return err;
			} else {
				if (data->mask)
					fan->samples = data->mask;
				if (data->bit)
					fan->divider = data->bit;
			}
			configured = true;
		} else {
			dev_err(fan->dev, "invalid label: %s\n", data->label);
			return -EINVAL;
		}
	}

	if (pdata->capability) {
		int drwr_avail;
		u32 regval;

		/* Obtain the number of FAN drawers, supported by system. */
		err = regmap_read(fan->regmap, pdata->capability, &regval);
		if (err) {
			dev_err(fan->dev, "Failed to query capability register 0x%08x\n",
				pdata->capability);
			return err;
		}

		drwr_avail = hweight32(regval);
		if (!tacho_avail || !drwr_avail || tacho_avail < drwr_avail) {
			dev_err(fan->dev, "Configuration is invalid: drawers num %d tachos num %d\n",
				drwr_avail, tacho_avail);
			return -EINVAL;
		}

		/* Set the number of tachometers per one drawer. */
		fan->tachos_per_drwr = tacho_avail / drwr_avail;
	}

	return 0;
}

static int mlxreg_fan_cooling_config(struct device *dev, struct mlxreg_fan *fan)
{
	int i;

	for (i = 0; i < MLXREG_FAN_MAX_PWM; i++) {
		struct mlxreg_fan_pwm *pwm = &fan->pwm[i];

		if (!pwm->connected)
			continue;
		pwm->fan = fan;
		pwm->cdev = devm_thermal_of_cooling_device_register(dev, NULL, mlxreg_fan_name[i],
								    pwm, &mlxreg_fan_cooling_ops);
		if (IS_ERR(pwm->cdev)) {
			dev_err(dev, "Failed to register cooling device\n");
			return PTR_ERR(pwm->cdev);
		}

		/* Set minimal PWM speed. */
		pwm->last_hwmon_state = MLXREG_FAN_PWM_DUTY2STATE(MLXREG_FAN_MIN_DUTY);
	}

	return 0;
}

static int mlxreg_fan_probe(struct platform_device *pdev)
{
	struct mlxreg_core_platform_data *pdata;
	struct device *dev = &pdev->dev;
	struct mlxreg_fan *fan;
	struct device *hwm;
	int err;

	pdata = dev_get_platdata(dev);
	if (!pdata) {
		dev_err(dev, "Failed to get platform data.\n");
		return -EINVAL;
	}

	fan = devm_kzalloc(dev, sizeof(*fan), GFP_KERNEL);
	if (!fan)
		return -ENOMEM;

	fan->dev = dev;
	fan->regmap = pdata->regmap;

	err = mlxreg_fan_config(fan, pdata);
	if (err)
		return err;

	hwm = devm_hwmon_device_register_with_info(dev, "mlxreg_fan",
						   fan,
						   &mlxreg_fan_hwmon_chip_info,
						   NULL);
	if (IS_ERR(hwm)) {
		dev_err(dev, "Failed to register hwmon device\n");
		return PTR_ERR(hwm);
	}

	if (IS_REACHABLE(CONFIG_THERMAL))
		err = mlxreg_fan_cooling_config(dev, fan);

	return err;
}

static struct platform_driver mlxreg_fan_driver = {
	.driver = {
	    .name = "mlxreg-fan",
	},
	.probe = mlxreg_fan_probe,
};

module_platform_driver(mlxreg_fan_driver);

MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("Mellanox FAN driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mlxreg-fan");
