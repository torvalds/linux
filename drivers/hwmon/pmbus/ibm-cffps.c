/*
 * Copyright 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include "pmbus.h"

/* STATUS_MFR_SPECIFIC bits */
#define CFFPS_MFR_FAN_FAULT			BIT(0)
#define CFFPS_MFR_THERMAL_FAULT			BIT(1)
#define CFFPS_MFR_OV_FAULT			BIT(2)
#define CFFPS_MFR_UV_FAULT			BIT(3)
#define CFFPS_MFR_PS_KILL			BIT(4)
#define CFFPS_MFR_OC_FAULT			BIT(5)
#define CFFPS_MFR_VAUX_FAULT			BIT(6)
#define CFFPS_MFR_CURRENT_SHARE_WARNING		BIT(7)

static int ibm_cffps_read_byte_data(struct i2c_client *client, int page,
				    int reg)
{
	int rc, mfr;

	switch (reg) {
	case PMBUS_STATUS_VOUT:
	case PMBUS_STATUS_IOUT:
	case PMBUS_STATUS_TEMPERATURE:
	case PMBUS_STATUS_FAN_12:
		rc = pmbus_read_byte_data(client, page, reg);
		if (rc < 0)
			return rc;

		mfr = pmbus_read_byte_data(client, page,
					   PMBUS_STATUS_MFR_SPECIFIC);
		if (mfr < 0)
			/*
			 * Return the status register instead of an error,
			 * since we successfully read status.
			 */
			return rc;

		/* Add MFR_SPECIFIC bits to the standard pmbus status regs. */
		if (reg == PMBUS_STATUS_FAN_12) {
			if (mfr & CFFPS_MFR_FAN_FAULT)
				rc |= PB_FAN_FAN1_FAULT;
		} else if (reg == PMBUS_STATUS_TEMPERATURE) {
			if (mfr & CFFPS_MFR_THERMAL_FAULT)
				rc |= PB_TEMP_OT_FAULT;
		} else if (reg == PMBUS_STATUS_VOUT) {
			if (mfr & (CFFPS_MFR_OV_FAULT | CFFPS_MFR_VAUX_FAULT))
				rc |= PB_VOLTAGE_OV_FAULT;
			if (mfr & CFFPS_MFR_UV_FAULT)
				rc |= PB_VOLTAGE_UV_FAULT;
		} else if (reg == PMBUS_STATUS_IOUT) {
			if (mfr & CFFPS_MFR_OC_FAULT)
				rc |= PB_IOUT_OC_FAULT;
			if (mfr & CFFPS_MFR_CURRENT_SHARE_WARNING)
				rc |= PB_CURRENT_SHARE_FAULT;
		}
		break;
	default:
		rc = -ENODATA;
		break;
	}

	return rc;
}

static int ibm_cffps_read_word_data(struct i2c_client *client, int page,
				    int reg)
{
	int rc, mfr;

	switch (reg) {
	case PMBUS_STATUS_WORD:
		rc = pmbus_read_word_data(client, page, reg);
		if (rc < 0)
			return rc;

		mfr = pmbus_read_byte_data(client, page,
					   PMBUS_STATUS_MFR_SPECIFIC);
		if (mfr < 0)
			/*
			 * Return the status register instead of an error,
			 * since we successfully read status.
			 */
			return rc;

		if (mfr & CFFPS_MFR_PS_KILL)
			rc |= PB_STATUS_OFF;
		break;
	default:
		rc = -ENODATA;
		break;
	}

	return rc;
}

static struct pmbus_driver_info ibm_cffps_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_PIN | PMBUS_HAVE_FAN12 | PMBUS_HAVE_TEMP |
		PMBUS_HAVE_TEMP2 | PMBUS_HAVE_TEMP3 | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_INPUT |
		PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_STATUS_FAN12,
	.read_byte_data = ibm_cffps_read_byte_data,
	.read_word_data = ibm_cffps_read_word_data,
};

static int ibm_cffps_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	return pmbus_do_probe(client, id, &ibm_cffps_info);
}

static const struct i2c_device_id ibm_cffps_id[] = {
	{ "ibm_cffps1", 1 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ibm_cffps_id);

static const struct of_device_id ibm_cffps_of_match[] = {
	{ .compatible = "ibm,cffps1" },
	{}
};
MODULE_DEVICE_TABLE(of, ibm_cffps_of_match);

static struct i2c_driver ibm_cffps_driver = {
	.driver = {
		.name = "ibm-cffps",
		.of_match_table = ibm_cffps_of_match,
	},
	.probe = ibm_cffps_probe,
	.remove = pmbus_do_remove,
	.id_table = ibm_cffps_id,
};

module_i2c_driver(ibm_cffps_driver);

MODULE_AUTHOR("Eddie James");
MODULE_DESCRIPTION("PMBus driver for IBM Common Form Factor power supplies");
MODULE_LICENSE("GPL");
