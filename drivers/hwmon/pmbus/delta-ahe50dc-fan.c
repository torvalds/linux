// SPDX-License-Identifier: GPL-2.0
/*
 * Delta AHE-50DC power shelf fan control module driver
 *
 * Copyright 2021 Zev Weiss <zev@bewilderbeest.net>
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>

#include "pmbus.h"

#define AHE50DC_PMBUS_READ_TEMP4 0xd0

static int ahe50dc_fan_write_byte(struct i2c_client *client, int page, u8 value)
{
	/*
	 * The CLEAR_FAULTS operation seems to sometimes (unpredictably, perhaps
	 * 5% of the time or so) trigger a problematic phenomenon in which the
	 * fan speeds surge momentarily and at least some (perhaps all?) of the
	 * system's power outputs experience a glitch.
	 *
	 * However, according to Delta it should be OK to simply not send any
	 * CLEAR_FAULTS commands (the device doesn't seem to be capable of
	 * reporting any faults anyway), so just blackhole them unconditionally.
	 */
	return value == PMBUS_CLEAR_FAULTS ? -EOPNOTSUPP : -ENODATA;
}

static int ahe50dc_fan_read_word_data(struct i2c_client *client, int page, int phase, int reg)
{
	/* temp1 in (virtual) page 1 is remapped to mfr-specific temp4 */
	if (page == 1) {
		if (reg == PMBUS_READ_TEMPERATURE_1)
			return i2c_smbus_read_word_data(client, AHE50DC_PMBUS_READ_TEMP4);
		return -EOPNOTSUPP;
	}

	/*
	 * There's a fairly limited set of commands this device actually
	 * supports, so here we block attempts to read anything else (which
	 * return 0xffff and would cause confusion elsewhere).
	 */
	switch (reg) {
	case PMBUS_STATUS_WORD:
	case PMBUS_FAN_COMMAND_1:
	case PMBUS_FAN_COMMAND_2:
	case PMBUS_FAN_COMMAND_3:
	case PMBUS_FAN_COMMAND_4:
	case PMBUS_STATUS_FAN_12:
	case PMBUS_STATUS_FAN_34:
	case PMBUS_READ_VIN:
	case PMBUS_READ_TEMPERATURE_1:
	case PMBUS_READ_TEMPERATURE_2:
	case PMBUS_READ_TEMPERATURE_3:
	case PMBUS_READ_FAN_SPEED_1:
	case PMBUS_READ_FAN_SPEED_2:
	case PMBUS_READ_FAN_SPEED_3:
	case PMBUS_READ_FAN_SPEED_4:
		return -ENODATA;
	default:
		return -EOPNOTSUPP;
	}
}

static struct pmbus_driver_info ahe50dc_fan_info = {
	.pages = 2,
	.format[PSC_FAN] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.format[PSC_VOLTAGE_IN] = direct,
	.m[PSC_FAN] = 1,
	.b[PSC_FAN] = 0,
	.R[PSC_FAN] = 0,
	.m[PSC_TEMPERATURE] = 1,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 1,
	.m[PSC_VOLTAGE_IN] = 1,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 3,
	.func[0] = PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_TEMP3 |
		PMBUS_HAVE_VIN | PMBUS_HAVE_FAN12 | PMBUS_HAVE_FAN34 |
		PMBUS_HAVE_STATUS_FAN12 | PMBUS_HAVE_STATUS_FAN34 | PMBUS_PAGE_VIRTUAL,
	.func[1] = PMBUS_HAVE_TEMP | PMBUS_PAGE_VIRTUAL,
	.write_byte = ahe50dc_fan_write_byte,
	.read_word_data = ahe50dc_fan_read_word_data,
};

/*
 * CAPABILITY returns 0xff, which appears to be this device's way indicating
 * it doesn't support something (and if we enable I2C_CLIENT_PEC on seeing bit
 * 7 being set it generates bad PECs, so let's not go there).
 */
static struct pmbus_platform_data ahe50dc_fan_data = {
	.flags = PMBUS_NO_CAPABILITY,
};

static int ahe50dc_fan_probe(struct i2c_client *client)
{
	client->dev.platform_data = &ahe50dc_fan_data;
	return pmbus_do_probe(client, &ahe50dc_fan_info);
}

static const struct i2c_device_id ahe50dc_fan_id[] = {
	{ "ahe50dc_fan" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ahe50dc_fan_id);

static const struct of_device_id __maybe_unused ahe50dc_fan_of_match[] = {
	{ .compatible = "delta,ahe50dc-fan" },
	{ }
};
MODULE_DEVICE_TABLE(of, ahe50dc_fan_of_match);

static struct i2c_driver ahe50dc_fan_driver = {
	.driver = {
		   .name = "ahe50dc_fan",
		   .of_match_table = of_match_ptr(ahe50dc_fan_of_match),
	},
	.probe_new = ahe50dc_fan_probe,
	.id_table = ahe50dc_fan_id,
};
module_i2c_driver(ahe50dc_fan_driver);

MODULE_AUTHOR("Zev Weiss <zev@bewilderbeest.net>");
MODULE_DESCRIPTION("Driver for Delta AHE-50DC power shelf fan control module");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
