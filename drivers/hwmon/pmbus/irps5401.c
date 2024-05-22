// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for the Infineon IRPS5401M PMIC.
 *
 * Copyright (c) 2019 SED Systems, a division of Calian Ltd.
 *
 * The device supports VOUT_PEAK, IOUT_PEAK, and TEMPERATURE_PEAK, however
 * this driver does not currently support them.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

#define IRPS5401_SW_FUNC (PMBUS_HAVE_VIN | PMBUS_HAVE_IIN | \
			  PMBUS_HAVE_STATUS_INPUT | \
			  PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT | \
			  PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | \
			  PMBUS_HAVE_PIN | PMBUS_HAVE_POUT | \
			  PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP)

#define IRPS5401_LDO_FUNC (PMBUS_HAVE_VIN | \
			   PMBUS_HAVE_STATUS_INPUT | \
			   PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT | \
			   PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | \
			   PMBUS_HAVE_PIN | PMBUS_HAVE_POUT | \
			   PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP)

static struct pmbus_driver_info irps5401_info = {
	.pages = 5,
	.func[0] = IRPS5401_SW_FUNC,
	.func[1] = IRPS5401_SW_FUNC,
	.func[2] = IRPS5401_SW_FUNC,
	.func[3] = IRPS5401_SW_FUNC,
	.func[4] = IRPS5401_LDO_FUNC,
};

static int irps5401_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &irps5401_info);
}

static const struct i2c_device_id irps5401_id[] = {
	{"irps5401"},
	{}
};

MODULE_DEVICE_TABLE(i2c, irps5401_id);

static struct i2c_driver irps5401_driver = {
	.driver = {
		   .name = "irps5401",
		   },
	.probe = irps5401_probe,
	.id_table = irps5401_id,
};

module_i2c_driver(irps5401_driver);

MODULE_AUTHOR("Robert Hancock");
MODULE_DESCRIPTION("PMBus driver for Infineon IRPS5401");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
