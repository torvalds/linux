// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: nvp6158_i2c.c
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
#include "nvp6158_common.h"

extern struct i2c_client* nvp6158_client;
//origin
#if 0
void nvp6158_I2CWriteByte8(unsigned char chip_addr, unsigned char reg_addr, unsigned char value)
{
    int ret;
    unsigned char buf[2];
    struct i2c_client* client = nvp6158_client;

    nvp6158_client->addr = chip_addr;

    buf[0] = reg_addr;
    buf[1] = value;

    ret = i2c_master_send(client, buf, 2);
    udelay(300);
    //return ret;
}

unsigned char nvp6158_I2CReadByte8(unsigned char chip_addr, unsigned char reg_addr)
{
    int ret_data = 0xFF;
    int ret;
    struct i2c_client* client = nvp6158_client;
    unsigned char buf[2];

    nvp6158_client->addr = chip_addr;

    buf[0] = reg_addr;
    ret = i2c_master_recv(client, buf, 1);
    if (ret >= 0)
    {
        ret_data = buf[0];
    }
    return ret_data;
}
#endif


void nvp6158_I2CWriteByte8(unsigned char chip_addr, unsigned char reg_addr, unsigned char value)
{
    int ret;
    unsigned char buf[2];
	struct i2c_client* client = nvp6158_client;

	client->addr = chip_addr>>1;

	buf[0] = reg_addr;
	buf[1] = value;

	ret = i2c_master_send(client, buf, 2);
	udelay(300);
}

unsigned char nvp6158_I2CReadByte8(unsigned char chip_addr, unsigned char reg_addr)
{
	struct i2c_client* client = nvp6158_client;

	client->addr = chip_addr>>1;

	return i2c_smbus_read_byte_data(client, reg_addr);
}
