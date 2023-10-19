/*
 * Touchscreen driver for the tps6507x chip.
 *
 * Copyright (c) 2009 RidgeRun (todd.fischer@ridgerun.com)
 *
 * Credits:
 *
 *    Using code from tsc2007, MtekVision Co., Ltd.
 *
 * For licencing details see kernel-base/COPYING
 *
 * TPS65070, TPS65073, TPS650731, and TPS650732 support
 * 10 bit touch screen interface.
 */

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/mfd/tps6507x.h>
#include <linux/input/tps6507x-ts.h>
#include <linux/delay.h>

#define TSC_DEFAULT_POLL_PERIOD 30 /* ms */
#define TPS_DEFAULT_MIN_PRESSURE 0x30
#define MAX_10BIT ((1 << 10) - 1)

#define	TPS6507X_ADCONFIG_CONVERT_TS (TPS6507X_ADCONFIG_AD_ENABLE | \
					 TPS6507X_ADCONFIG_START_CONVERSION | \
					 TPS6507X_ADCONFIG_INPUT_REAL_TSC)
#define	TPS6507X_ADCONFIG_POWER_DOWN_TS (TPS6507X_ADCONFIG_INPUT_REAL_TSC)

struct ts_event {
	u16	x;
	u16	y;
	u16	pressure;
};

struct tps6507x_ts {
	struct device		*dev;
	struct input_dev	*input;
	struct tps6507x_dev	*mfd;
	char			phys[32];
	struct ts_event		tc;
	u16			min_pressure;
	bool			pendown;
};

static int tps6507x_read_u8(struct tps6507x_ts *tsc, u8 reg, u8 *data)
{
	return tsc->mfd->read_dev(tsc->mfd, reg, 1, data);
}

static int tps6507x_write_u8(struct tps6507x_ts *tsc, u8 reg, u8 data)
{
	return tsc->mfd->write_dev(tsc->mfd, reg, 1, &data);
}

static s32 tps6507x_adc_conversion(struct tps6507x_ts *tsc,
				   u8 tsc_mode, u16 *value)
{
	s32 ret;
	u8 adc_status;
	u8 result;

	/* Route input signal to A/D converter */

	ret = tps6507x_write_u8(tsc, TPS6507X_REG_TSCMODE, tsc_mode);
	if (ret) {
		dev_err(tsc->dev, "TSC mode read failed\n");
		goto err;
	}

	/* Start A/D conversion */

	ret = tps6507x_write_u8(tsc, TPS6507X_REG_ADCONFIG,
				TPS6507X_ADCONFIG_CONVERT_TS);
	if (ret) {
		dev_err(tsc->dev, "ADC config write failed\n");
		return ret;
	}

	do {
		ret = tps6507x_read_u8(tsc, TPS6507X_REG_ADCONFIG,
				       &adc_status);
		if (ret) {
			dev_err(tsc->dev, "ADC config read failed\n");
			goto err;
		}
	} while (adc_status & TPS6507X_ADCONFIG_START_CONVERSION);

	ret = tps6507x_read_u8(tsc, TPS6507X_REG_ADRESULT_2, &result);
	if (ret) {
		dev_err(tsc->dev, "ADC result 2 read failed\n");
		goto err;
	}

	*value = (result & TPS6507X_REG_ADRESULT_2_MASK) << 8;

	ret = tps6507x_read_u8(tsc, TPS6507X_REG_ADRESULT_1, &result);
	if (ret) {
		dev_err(tsc->dev, "ADC result 1 read failed\n");
		goto err;
	}

	*value |= result;

	dev_dbg(tsc->dev, "TSC channel %d = 0x%X\n", tsc_mode, *value);

err:
	return ret;
}

/* Need to call tps6507x_adc_standby() after using A/D converter for the
 * touch screen interrupt to work properly.
 */

static s32 tps6507x_adc_standby(struct tps6507x_ts *tsc)
{
	s32 ret;
	s32 loops = 0;
	u8 val;

	ret = tps6507x_write_u8(tsc,  TPS6507X_REG_ADCONFIG,
				TPS6507X_ADCONFIG_INPUT_TSC);
	if (ret)
		return ret;

	ret = tps6507x_write_u8(tsc, TPS6507X_REG_TSCMODE,
				TPS6507X_TSCMODE_STANDBY);
	if (ret)
		return ret;

	ret = tps6507x_read_u8(tsc, TPS6507X_REG_INT, &val);
	if (ret)
		return ret;

	while (val & TPS6507X_REG_TSC_INT) {
		mdelay(10);
		ret = tps6507x_read_u8(tsc, TPS6507X_REG_INT, &val);
		if (ret)
			return ret;
		loops++;
	}

	return ret;
}

static void tps6507x_ts_poll(struct input_dev *input_dev)
{
	struct tps6507x_ts *tsc = input_get_drvdata(input_dev);
	bool pendown;
	s32 ret;

	ret = tps6507x_adc_conversion(tsc, TPS6507X_TSCMODE_PRESSURE,
				      &tsc->tc.pressure);
	if (ret)
		goto done;

	pendown = tsc->tc.pressure > tsc->min_pressure;

	if (unlikely(!pendown && tsc->pendown)) {
		dev_dbg(tsc->dev, "UP\n");
		input_report_key(input_dev, BTN_TOUCH, 0);
		input_report_abs(input_dev, ABS_PRESSURE, 0);
		input_sync(input_dev);
		tsc->pendown = false;
	}

	if (pendown) {

		if (!tsc->pendown) {
			dev_dbg(tsc->dev, "DOWN\n");
			input_report_key(input_dev, BTN_TOUCH, 1);
		} else
			dev_dbg(tsc->dev, "still down\n");

		ret =  tps6507x_adc_conversion(tsc, TPS6507X_TSCMODE_X_POSITION,
					       &tsc->tc.x);
		if (ret)
			goto done;

		ret =  tps6507x_adc_conversion(tsc, TPS6507X_TSCMODE_Y_POSITION,
					       &tsc->tc.y);
		if (ret)
			goto done;

		input_report_abs(input_dev, ABS_X, tsc->tc.x);
		input_report_abs(input_dev, ABS_Y, tsc->tc.y);
		input_report_abs(input_dev, ABS_PRESSURE, tsc->tc.pressure);
		input_sync(input_dev);
		tsc->pendown = true;
	}

done:
	tps6507x_adc_standby(tsc);
}

static int tps6507x_ts_probe(struct platform_device *pdev)
{
	struct tps6507x_dev *tps6507x_dev = dev_get_drvdata(pdev->dev.parent);
	const struct tps6507x_board *tps_board;
	const struct touchscreen_init_data *init_data;
	struct tps6507x_ts *tsc;
	struct input_dev *input_dev;
	int error;

	/*
	 * tps_board points to pmic related constants
	 * coming from the board-evm file.
	 */
	tps_board = dev_get_platdata(tps6507x_dev->dev);
	if (!tps_board) {
		dev_err(tps6507x_dev->dev,
			"Could not find tps6507x platform data\n");
		return -ENODEV;
	}

	/*
	 * init_data points to array of regulator_init structures
	 * coming from the board-evm file.
	 */
	init_data = tps_board->tps6507x_ts_init_data;

	tsc = devm_kzalloc(&pdev->dev, sizeof(struct tps6507x_ts), GFP_KERNEL);
	if (!tsc) {
		dev_err(tps6507x_dev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	tsc->mfd = tps6507x_dev;
	tsc->dev = tps6507x_dev->dev;
	tsc->min_pressure = init_data ?
			init_data->min_pressure : TPS_DEFAULT_MIN_PRESSURE;

	snprintf(tsc->phys, sizeof(tsc->phys),
		 "%s/input0", dev_name(tsc->dev));

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev) {
		dev_err(tsc->dev, "Failed to allocate polled input device.\n");
		return -ENOMEM;
	}

	tsc->input = input_dev;
	input_set_drvdata(input_dev, tsc);

	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, 0, MAX_10BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_10BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_10BIT, 0, 0);

	input_dev->name = "TPS6507x Touchscreen";
	input_dev->phys = tsc->phys;
	input_dev->dev.parent = tsc->dev;
	input_dev->id.bustype = BUS_I2C;
	if (init_data) {
		input_dev->id.vendor = init_data->vendor;
		input_dev->id.product = init_data->product;
		input_dev->id.version = init_data->version;
	}

	error = tps6507x_adc_standby(tsc);
	if (error)
		return error;

	error = input_setup_polling(input_dev, tps6507x_ts_poll);
	if (error)
		return error;

	input_set_poll_interval(input_dev,
				init_data ? init_data->poll_period :
					    TSC_DEFAULT_POLL_PERIOD);

	error = input_register_device(input_dev);
	if (error)
		return error;

	return 0;
}

static struct platform_driver tps6507x_ts_driver = {
	.driver = {
		.name = "tps6507x-ts",
	},
	.probe = tps6507x_ts_probe,
};
module_platform_driver(tps6507x_ts_driver);

MODULE_AUTHOR("Todd Fischer <todd.fischer@ridgerun.com>");
MODULE_DESCRIPTION("TPS6507x - TouchScreen driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tps6507x-ts");
