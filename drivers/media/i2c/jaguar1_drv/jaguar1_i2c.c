// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: i2c.c
 *  Description	:
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include "jaguar1_common.h"

extern struct i2c_client* jaguar1_client;

void jaguar1_I2CWriteByte8(unsigned char chip_addr, unsigned char reg_addr, unsigned char value)
{
	int ret;
	unsigned char buf[2];
	struct i2c_client* client = jaguar1_client;

	client->addr = chip_addr>>1;

	buf[0] = reg_addr;
	buf[1] = value;

	ret = i2c_master_send(client, buf, 2);
	udelay(300);
}

unsigned char jaguar1_I2CReadByte8(unsigned char chip_addr, unsigned char reg_addr)
{
	struct i2c_client* client = jaguar1_client;

	client->addr = chip_addr>>1;

	return i2c_smbus_read_byte_data(client, reg_addr);
}
