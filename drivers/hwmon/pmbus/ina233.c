// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for ina233
 *
 * Copyright (c) 2025 Leo Yang
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

#define MFR_READ_VSHUNT 0xd1
#define MFR_CALIBRATION 0xd4

#define INA233_MAX_CURRENT_DEFAULT	32768000 /* uA */
#define INA233_RSHUNT_DEFAULT		2000 /* uOhm */

#define MAX_M_VAL 32767

static void calculate_coef(int *m, int *R, u32 current_lsb, int power_coef)
{
	u64 scaled_m;
	int scale_factor = 0;
	int scale_coef = 1;

	/*
	 * 1000000 from Current_LSB A->uA .
	 * scale_coef is for scaling up to minimize rounding errors,
	 * If there is no decimal information, no need to scale.
	 */
	if (1000000 % current_lsb) {
		/* Scaling to keep integer precision */
		scale_factor = -3;
		scale_coef = 1000;
	}

	/*
	 * Unit Conversion (Current_LSB A->uA) and use scaling(scale_factor)
	 * to keep integer precision.
	 * Formulae referenced from spec.
	 */
	scaled_m = div64_u64(1000000 * scale_coef, (u64)current_lsb * power_coef);

	/* Maximize while keeping it bounded.*/
	while (scaled_m > MAX_M_VAL) {
		scaled_m = div_u64(scaled_m, 10);
		scale_factor++;
	}
	/* Scale up only if fractional part exists. */
	while (scaled_m * 10 < MAX_M_VAL && scale_coef != 1) {
		scaled_m *= 10;
		scale_factor--;
	}

	*m = scaled_m;
	*R = scale_factor;
}

static int ina233_read_word_data(struct i2c_client *client, int page,
				 int phase, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VMON:
		ret = pmbus_read_word_data(client, 0, 0xff, MFR_READ_VSHUNT);

		/* Adjust returned value to match VIN coefficients */
		/* VIN: 1.25 mV VSHUNT: 2.5 uV LSB */
		ret = DIV_ROUND_CLOSEST(ret * 25, 12500);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int ina233_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int ret, m, R;
	u32 rshunt;
	u32 max_current;
	u32 current_lsb;
	u16 calibration;
	struct pmbus_driver_info *info;

	info = devm_kzalloc(dev, sizeof(struct pmbus_driver_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->pages = 1;
	info->format[PSC_VOLTAGE_IN] = direct;
	info->format[PSC_VOLTAGE_OUT] = direct;
	info->format[PSC_CURRENT_OUT] = direct;
	info->format[PSC_POWER] = direct;
	info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_INPUT
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
		| PMBUS_HAVE_POUT
		| PMBUS_HAVE_VMON | PMBUS_HAVE_STATUS_VMON;
	info->m[PSC_VOLTAGE_IN] = 8;
	info->R[PSC_VOLTAGE_IN] = 2;
	info->m[PSC_VOLTAGE_OUT] = 8;
	info->R[PSC_VOLTAGE_OUT] = 2;
	info->read_word_data = ina233_read_word_data;

	/* If INA233 skips current/power, shunt-resistor and current-lsb aren't needed.	*/
	/* read rshunt value (uOhm) */
	ret = device_property_read_u32(dev, "shunt-resistor", &rshunt);
	if (ret) {
		if (ret != -EINVAL)
			return dev_err_probe(dev, ret, "Shunt resistor property read fail.\n");
		rshunt = INA233_RSHUNT_DEFAULT;
	}
	if (!rshunt)
		return dev_err_probe(dev, -EINVAL,
				     "Shunt resistor cannot be zero.\n");

	/* read Maximum expected current value (uA) */
	ret = device_property_read_u32(dev, "ti,maximum-expected-current-microamp", &max_current);
	if (ret) {
		if (ret != -EINVAL)
			return dev_err_probe(dev, ret,
					     "Maximum expected current property read fail.\n");
		max_current = INA233_MAX_CURRENT_DEFAULT;
	}
	if (max_current < 32768)
		return dev_err_probe(dev, -EINVAL,
				     "Maximum expected current cannot less then 32768.\n");

	/* Calculate Current_LSB according to the spec formula */
	current_lsb = max_current / 32768;

	/* calculate current coefficient */
	calculate_coef(&m, &R, current_lsb, 1);
	info->m[PSC_CURRENT_OUT] = m;
	info->R[PSC_CURRENT_OUT] = R;

	/* calculate power coefficient */
	calculate_coef(&m, &R, current_lsb, 25);
	info->m[PSC_POWER] = m;
	info->R[PSC_POWER] = R;

	/* write MFR_CALIBRATION register, Apply formula from spec with unit scaling. */
	calibration = div64_u64(5120000000ULL, (u64)rshunt * current_lsb);
	if (calibration > 0x7FFF)
		return dev_err_probe(dev, -EINVAL,
				     "Product of Current_LSB %u and shunt resistor %u too small, MFR_CALIBRATION reg exceeds 0x7FFF.\n",
				     current_lsb, rshunt);
	ret = i2c_smbus_write_word_data(client, MFR_CALIBRATION, calibration);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Unable to write calibration.\n");

	dev_dbg(dev, "power monitor %s (Rshunt = %u uOhm, Current_LSB = %u uA/bit)\n",
		client->name, rshunt, current_lsb);

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id ina233_id[] = {
	{"ina233", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ina233_id);

static const struct of_device_id __maybe_unused ina233_of_match[] = {
	{ .compatible = "ti,ina233" },
	{}
};
MODULE_DEVICE_TABLE(of, ina233_of_match);

static struct i2c_driver ina233_driver = {
	.driver = {
		   .name = "ina233",
		   .of_match_table = of_match_ptr(ina233_of_match),
	},
	.probe = ina233_probe,
	.id_table = ina233_id,
};

module_i2c_driver(ina233_driver);

MODULE_AUTHOR("Leo Yang <leo.yang.sy0@gmail.com>");
MODULE_DESCRIPTION("PMBus driver for INA233 and compatible chips");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
