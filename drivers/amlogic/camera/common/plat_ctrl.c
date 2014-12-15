/*
 *gt2005 - This code emulates a real video device with v4l2 api
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <media/videobuf-vmalloc.h>

#include <linux/i2c.h>
#include "plat_ctrl.h"

/****************************************************************
 *   i2c functions
 * **************************************************************/
static int camera_read_buff(struct i2c_client *client,char *buf, int addr_len, int data_len)
{
    int  i2c_flag = -1;
	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= addr_len,
			.buf	= buf,
		},{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= data_len,
			.buf	= buf,
		}
	};

		i2c_flag = i2c_transfer(client->adapter, msgs, 2);

	return i2c_flag;
}

static int  camera_write_buff(struct i2c_client *client,char *buf, int len)
{
	struct i2c_msg msg[] = {
	    {
			.addr	= client->addr,
			.flags	= 0,    //|I2C_M_TEN,
			.len	= len,
			.buf	= buf,
		}

	};

	if (i2c_transfer(client->adapter, msg, 1) < 0) {
		printk("i2c error\n");
		return -1;
	} else
		return 0;
}

int i2c_get_byte(struct i2c_client *client,unsigned short addr)
{
	unsigned char buff[4];
    buff[0] = (unsigned char)((addr >> 8) & 0xff);
    buff[1] = (unsigned char)(addr & 0xff);
       
	if (camera_read_buff(client, buff, 2, 1) <0)
		return -1;
	return buff[0];
}

int i2c_get_byte_add8(struct i2c_client *client,unsigned char addr)
{
	unsigned char buff[4];
    buff[0] = (unsigned char)(addr & 0xff);
       
	if (camera_read_buff(client, buff, 1, 1) <0)
		return -1;
	return buff[0];
}

int i2c_get_word(struct i2c_client *client,unsigned short addr)
{
	unsigned short data;
	unsigned char buff[4];
    buff[0] = (unsigned char)((addr >> 8) & 0xff);
    buff[1] = (unsigned char)(addr & 0xff);
    
	if (camera_read_buff(client, buff, 2, 2) <0)
		return -1;
    else
    {
		data = buff[0];
		data = (data << 8) | buff[1];
        return data;
    }
}

int i2c_put_byte(struct i2c_client *client, unsigned short addr, unsigned char data)
{
    unsigned char buff[4];
    buff[0] = (unsigned char)((addr >> 8) & 0xff);
    buff[1] = (unsigned char)(addr & 0xff);
    buff[2] = data;
	if (camera_write_buff(client, buff, 3) <0)
		return -1;
	return  0;
}

int i2c_put_word(struct i2c_client *client, unsigned short addr, unsigned short data)
{
    unsigned char buff[4];
    buff[0] = (unsigned char)((addr >> 8) & 0xff);
    buff[1] = (unsigned char)(addr & 0xff);
	buff[2] = (unsigned char)((data >> 8) & 0xff);
	buff[3] = (unsigned char)(data & 0xff);
	
	if (camera_write_buff(client, buff, 4) <0)
		return -1;
	return 0;
}

int i2c_put_byte_add8_new(struct i2c_client *client, unsigned char addr, unsigned char data)
{
	unsigned char buff[4];
	buff[0] = (unsigned char)(addr & 0xff);
	buff[1] = (unsigned char)(data & 0xff);
	if (camera_write_buff(client, buff, 2) <0)
		return -1;
	return  0;
}

int i2c_put_byte_add8(struct i2c_client *client,char *buf, int len)
{
	if (camera_write_buff(client, buf, len) <0)
		return -1;
	return  0;
}

int cam_i2c_send_msg(struct i2c_client *client, cam_i2c_msg_t i2c_msg)
{
	unsigned char buff[4];
	
	switch (i2c_msg.type) {
	case ADDR16_DATA16:
		buff[0] = (unsigned char)((i2c_msg.addr >> 8) & 0xff);
		buff[1] = (unsigned char)(i2c_msg.addr & 0xff);
		buff[2] = (unsigned char)((i2c_msg.data >> 8) & 0xff);
		buff[3] = (unsigned char)(i2c_msg.data & 0xff);
		if (camera_write_buff(client, buff, 4) <0)
			return -1;
		break;
	case ADDR16_DATA8:
		buff[0] = (unsigned char)((i2c_msg.addr >> 8) & 0xff);
		buff[1] = (unsigned char)(i2c_msg.addr & 0xff);
		buff[2] = (unsigned char)(i2c_msg.data & 0xff);
		if (camera_write_buff(client, buff, 3) <0)
			return -1;
		break;
	case ADDR8_DATA16:
		buff[0] = (unsigned char)(i2c_msg.addr & 0xff);
		buff[1] = (unsigned char)((i2c_msg.data >> 8) & 0xff);
		buff[2] = (unsigned char)(i2c_msg.data & 0xff);
		if (camera_write_buff(client, buff, 3) <0)
			return -1;
		break;
	case ADDR8_DATA8:
		buff[0] = (unsigned char)(i2c_msg.addr & 0xff);
		buff[1] = (unsigned char)(i2c_msg.data & 0xff);
		if (camera_write_buff(client, buff, 2) <0)
			return -1;
	case TIME_DELAY:
		msleep(i2c_msg.data);
		break;
	default:
		break;
	}
	return 0;
}


