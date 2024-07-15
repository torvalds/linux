// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for Vicor PLI1209BC Digital Supervisor
 *
 * Copyright (c) 2022 9elements GmbH
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include <linux/regulator/driver.h>
#include "pmbus.h"

/*
 * The capability command is only supported at page 0. Probing the device while
 * the page register is set to 1 will falsely enable PEC support. Disable
 * capability probing accordingly, since the PLI1209BC does not have any
 * additional capabilities.
 */
static struct pmbus_platform_data pli1209bc_plat_data = {
	.flags = PMBUS_NO_CAPABILITY,
};

static int pli1209bc_read_word_data(struct i2c_client *client, int page,
				    int phase, int reg)
{
	int data;

	switch (reg) {
	/* PMBUS_READ_POUT uses a direct format with R=0 */
	case PMBUS_READ_POUT:
		data = pmbus_read_word_data(client, page, phase, reg);
		if (data < 0)
			return data;
		data = sign_extend32(data, 15) * 10;
		return clamp_val(data, -32768, 32767) & 0xffff;
	/*
	 * PMBUS_READ_VOUT and PMBUS_READ_TEMPERATURE_1 return invalid data
	 * when the BCM is turned off. Since it is not possible to return
	 * ENODATA error, return zero instead.
	 */
	case PMBUS_READ_VOUT:
	case PMBUS_READ_TEMPERATURE_1:
		data = pmbus_read_word_data(client, page, phase,
					    PMBUS_STATUS_WORD);
		if (data < 0)
			return data;
		if (data & PB_STATUS_POWER_GOOD_N)
			return 0;
		return pmbus_read_word_data(client, page, phase, reg);
	default:
		return -ENODATA;
	}
}

static int pli1209bc_write_byte(struct i2c_client *client, int page, u8 reg)
{
	int ret;

	switch (reg) {
	case PMBUS_CLEAR_FAULTS:
		ret = pmbus_write_byte(client, page, reg);
		/*
		 * PLI1209 takes 230 usec to execute the CLEAR_FAULTS command.
		 * During that time it's busy and NACKs all requests on the
		 * SMBUS interface. It also NACKs reads on PMBUS_STATUS_BYTE
		 * making it impossible to poll the BUSY flag.
		 *
		 * Just wait for not BUSY unconditionally.
		 */
		usleep_range(250, 300);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

#if IS_ENABLED(CONFIG_SENSORS_PLI1209BC_REGULATOR)
static const struct regulator_desc pli1209bc_reg_desc = {
	.name = "vout2",
	.id = 1,
	.of_match = of_match_ptr("vout2"),
	.regulators_node = of_match_ptr("regulators"),
	.ops = &pmbus_regulator_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
};
#endif

static struct pmbus_driver_info pli1209bc_info = {
	.pages = 2,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 1,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 1,
	.m[PSC_VOLTAGE_OUT] = 1,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 1,
	.m[PSC_CURRENT_IN] = 1,
	.b[PSC_CURRENT_IN] = 0,
	.R[PSC_CURRENT_IN] = 3,
	.m[PSC_CURRENT_OUT] = 1,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = 2,
	.m[PSC_POWER] = 1,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = 1,
	.m[PSC_TEMPERATURE] = 1,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 0,
	/*
	 * Page 0 sums up all attributes except voltage readings.
	 * The pli1209 digital supervisor only contains a single BCM, making
	 * page 0 redundant.
	 */
	.func[1] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT
	    | PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT
	    | PMBUS_HAVE_PIN | PMBUS_HAVE_POUT
	    | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP
	    | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_INPUT,
	.read_word_data = pli1209bc_read_word_data,
	.write_byte = pli1209bc_write_byte,
#if IS_ENABLED(CONFIG_SENSORS_PLI1209BC_REGULATOR)
	.num_regulators = 1,
	.reg_desc = &pli1209bc_reg_desc,
#endif
};

static int pli1209bc_probe(struct i2c_client *client)
{
	client->dev.platform_data = &pli1209bc_plat_data;
	return pmbus_do_probe(client, &pli1209bc_info);
}

static const struct i2c_device_id pli1209bc_id[] = {
	{"pli1209bc", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, pli1209bc_id);

#ifdef CONFIG_OF
static const struct of_device_id pli1209bc_of_match[] = {
	{ .compatible = "vicor,pli1209bc" },
	{ },
};
MODULE_DEVICE_TABLE(of, pli1209bc_of_match);
#endif

static struct i2c_driver pli1209bc_driver = {
	.driver = {
		   .name = "pli1209bc",
		   .of_match_table = of_match_ptr(pli1209bc_of_match),
		   },
	.probe = pli1209bc_probe,
	.id_table = pli1209bc_id,
};

module_i2c_driver(pli1209bc_driver);

MODULE_AUTHOR("Marcello Sylvester Bauer <sylv@sylv.io>");
MODULE_DESCRIPTION("PMBus driver for Vicor PLI1209BC");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
