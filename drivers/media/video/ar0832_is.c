/*
 * driver for Fusitju AR0832 LS 8MP camera
 *
 * Copyright (c) 2010, Samsung Electronics. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <media/v4l2-device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/videodev2.h>
#include <linux/slab.h>

#ifdef CONFIG_VIDEO_SAMSUNG_V4L2
#include <linux/videodev2_samsung.h>
#endif

#include <linux/regulator/machine.h>

#include <media/ar0832_platform.h>
#include "ar0832.h"

#define AR0832_DRIVER_NAME	"AR0832_IS"
#define SDCARD_FW
#ifdef SDCARD_FW
#define AR0832_FW_PATH		"/sdcard/RS_M5LS.bin"
#endif /* SDCARD_FW */
#define AR0832T_FW_REQUEST_PATH	"ar0832/RS_M5LS_T.bin"	/* Techwin */
#define AR0832O_FW_REQUEST_PATH	"ar0832/RS_M5LS_O.bin"	/* Optical communication */
#define AR0832_FW_DUMP_PATH	"/data/RS_M5LS_dump.bin"
#define AR0832_FW_VER_LEN		22
#define AR0832_FW_VER_FILE_CUR	0x16FF00

#define AR0832_FLASH_BASE_ADDR	0x10000000
#define AR0832_INT_RAM_BASE_ADDR	0x68000000

#define AR0832_I2C_RETRY		5
#define AR0832_I2C_VERIFY		100
#define AR0832_ISP_TIMEOUT	3000
#define AR0832_ISP_AFB_TIMEOUT	15000 /* FIXME */
#define AR0832_ISP_ESD_TIMEOUT	1000

#define AR0832_JPEG_MAXSIZE	0x3A0000
#define AR0832_THUMB_MAXSIZE	0xFC00
#define AR0832_POST_MAXSIZE	0xBB800

#define AR0832_DEF_APEX_DEN	100

#define ar0832_readb(sd, g, b, v)		ar0832_read(sd, 1, g, b, v)
#define ar0832_readw(sd, g, b, v)		ar0832_read(sd, 2, g, b, v)
#define ar0832_readl(sd, g, b, v)		ar0832_read(sd, 4, g, b, v)

#define ar0832_writeb(sd, g, b, v)	ar0832_write(sd, 1, g, b, v)
#define ar0832_writew(sd, g, b, v)	ar0832_write(sd, 2, g, b, v)
#define ar0832_writel(sd, g, b, v)	ar0832_write(sd, 4, g, b, v)

#define CHECK_ERR(x)	if ((x) < 0) { \
				cam_err("i2c failed, err %d\n", x); \
				return x; \
			}


/*
* Because misc devices can not carry a pointer from driver register to
* open, we keep this global. This limits the driver to a single instance.
*/
struct ar0832_state *ar0832_is_data;

static const struct ar0832_frmsizeenum preview_frmsizes[] = {
	{ AR0832_PREVIEW_QCIF,	176,	144,	0x05 },	/* 176 x 144 */
	{ AR0832_PREVIEW_QCIF2,	528,	432,	0x2C },	/* 176 x 144 */
	{ AR0832_PREVIEW_QVGA,	320,	240,	0x09 },
	{ AR0832_PREVIEW_VGA,	640,	480,	0x17 },
	{ AR0832_PREVIEW_D1,	720,	480,	0x18 },
	{ AR0832_PREVIEW_WVGA,	800,	480,	0x1A },
	{ AR0832_PREVIEW_720P,	1280,	720,	0x21 },
	{ AR0832_PREVIEW_1080P,	1920,	1080,	0x28 },
	{ AR0832_PREVIEW_HDR,	3264,	2448,	0x27 },
};

static const struct ar0832_frmsizeenum capture_frmsizes[] = {
	{ AR0832_CAPTURE_VGA,	640,	480,	0x09 },
	{ AR0832_CAPTURE_WVGA,	800,	480,	0x0A },
	{ AR0832_CAPTURE_W2MP,	2048,	1232,	0x2C },
	{ AR0832_CAPTURE_3MP,	2048,	1536,	0x1B },
	{ AR0832_CAPTURE_W7MP,	3264,	1968,	0x2D },
	{ AR0832_CAPTURE_8MP,	3264,	2448,	0x25 },
};

static struct ar0832_control ar0832_ctrls[] = {
	{
		.id = V4L2_CID_CAMERA_ISO,
		.minimum = ISO_AUTO,
		.maximum = ISO_800,
		.step = 1,
		.value = ISO_AUTO,
		.default_value = ISO_AUTO,
	}, {
		.id = V4L2_CID_CAMERA_BRIGHTNESS,
		.minimum = EV_MINUS_4,
		.maximum = EV_MAX - 1,
		.step = 1,
		.value = EV_DEFAULT,
		.default_value = EV_DEFAULT,
	}, {
		.id = V4L2_CID_CAMERA_SATURATION,
		.minimum = SATURATION_MINUS_2,
		.maximum = SATURATION_MAX - 1,
		.step = 1,
		.value = SATURATION_DEFAULT,
		.default_value = SATURATION_DEFAULT,
	}, {
		.id = V4L2_CID_CAMERA_SHARPNESS,
		.minimum = SHARPNESS_MINUS_2,
		.maximum = SHARPNESS_MAX - 1,
		.step = 1,
		.value = SHARPNESS_DEFAULT,
		.default_value = SHARPNESS_DEFAULT,
	}, {
		.id = V4L2_CID_CAMERA_ZOOM,
		.minimum = ZOOM_LEVEL_0,
		.maximum = ZOOM_LEVEL_MAX - 1,
		.step = 1,
		.value = ZOOM_LEVEL_0,
		.default_value = ZOOM_LEVEL_0,
	}, {
		.id = V4L2_CID_CAM_JPEG_QUALITY,
		.minimum = 1,
		.maximum = 100,
		.step = 1,
		.value = 100,
		.default_value = 100,
	},
};

static inline struct ar0832_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ar0832_state, sd);
}

static int ar0832_read(struct v4l2_subdev *sd,
	u8 len, u8 category, u8 byte, int *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[5];
	unsigned char recv_data[len + 1];
	int i, err = 0;

	if (!client->adapter)
		return -ENODEV;

	if (len != 0x01 && len != 0x02 && len != 0x04)
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	/* high byte goes out first */
	data[0] = msg.len;
	data[1] = 0x01;			/* Read category parameters */
	data[2] = category;
	data[3] = byte;
	data[4] = len;

	for (i = AR0832_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1) {
		cam_err("category %#x, byte %#x\n", category, byte);
		return err;
	}

	msg.flags = I2C_M_RD;
	msg.len = sizeof(recv_data);
	msg.buf = recv_data;
	for (i = AR0832_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1) {
		cam_err("category %#x, byte %#x\n", category, byte);
		return err;
	}

	if (recv_data[0] != sizeof(recv_data))
		cam_i2c_dbg("expected length %d, but return length %d\n",
				 sizeof(recv_data), recv_data[0]);

	if (len == 0x01)
		*val = recv_data[1];
	else if (len == 0x02)
		*val = recv_data[1] << 8 | recv_data[2];
	else
		*val = recv_data[1] << 24 | recv_data[2] << 16 |
				recv_data[3] << 8 | recv_data[4];

	cam_i2c_dbg("category %#02x, byte %#x, value %#x\n", category, byte, *val);
	return err;
}

static int ar0832_write(struct v4l2_subdev *sd,
	u8 len, u8 category, u8 byte, int val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[len + 4];
	int i, err;
return 0;
	if (!client->adapter)
		return -ENODEV;

	if (len != 0x01 && len != 0x02 && len != 0x04)
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	data[0] = msg.len;
	data[1] = 0x02;			/* Write category parameters */
	data[2] = category;
	data[3] = byte;
	if (len == 0x01) {
		data[4] = val & 0xFF;
	} else if (len == 0x02) {
		data[4] = (val >> 8) & 0xFF;
		data[5] = val & 0xFF;
	} else {
		data[4] = (val >> 24) & 0xFF;
		data[5] = (val >> 16) & 0xFF;
		data[6] = (val >> 8) & 0xFF;
		data[7] = val & 0xFF;
	}

	cam_i2c_dbg("category %#x, byte %#x, value %#x\n", category, byte, val);

	for (i = AR0832_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	return err;
}
static unsigned short ar0832_i2c_read_reg(struct i2c_client *client, unsigned short reg,int readbyte /*1 or 2*/)
{
	int ret;
	unsigned char i2c_data[10];
	unsigned char i2c_rdata[10];
	memset(i2c_rdata,0,10);

	i2c_data[0]= (unsigned char)((reg>>8) & 0xff);
	i2c_data[1]= (unsigned char)(reg & 0xff);	
	i2c_master_send(client,i2c_data,2);

	ret = i2c_master_recv(client,i2c_rdata,readbyte);

#if 0
	int i;
	for(i=0;i<2;i++)
	printk("retdata %d => %x \n",i,i2c_rdata[i]);

	if (ret < 0)
	printk( "%s: err %d\n", __func__, ret);
#endif

	if (readbyte== 1) 
		return i2c_rdata[0];
	return i2c_rdata[0]<<8 | i2c_rdata[1];
}

static int ar0832_i2c_write(struct v4l2_subdev *sd, unsigned char i2c_data[],
				unsigned char length)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char buf[length], i;
	struct i2c_msg msg = {client->addr, 0, length, buf};

	for (i = 0; i < length; i++)
		buf[i] = i2c_data[i];

	return i2c_transfer(client->adapter, &msg, 1) == 1 ? 0 : -EIO;
}

static int ar0832_i2c_write_4byte(struct v4l2_subdev *sd, unsigned char d0,unsigned char d1,unsigned char d2,unsigned char d3)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char buf[4], i;
	struct i2c_msg msg = {client->addr, 0, 4, buf};

	buf[0] = d0;
	buf[1] = d1;
	buf[2] = d2;
	buf[3] = d3;

	return i2c_transfer(client->adapter, &msg, 1) == 1 ? 0 : -EIO;
}

static int ar0832_i2c_write_2short(struct v4l2_subdev *sd, unsigned short d0,unsigned short d1)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char buf[4], i;
	struct i2c_msg msg = {client->addr, 0, 4, buf};

	buf[0] = d0>>8;
	buf[1] = d0 & 0xff;
	buf[2] = d1>>8;
	buf[3] = d1 & 0xff;

	return i2c_transfer(client->adapter, &msg, 1) == 1 ? 0 : -EIO;
}


static int ar0832_write_array(struct v4l2_subdev *sd,unsigned  short * reg , int size)
{	
	unsigned char _tmp[4];
	int i,err=0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	for (i = 0; i < size ; i=i+2) {
		if(reg[i] == 0xffff){ //delay
			mdelay(reg[i+1]);
		}
		else {
//			printk("%x %x. ",reg[i],reg[i+1]);
//			printk(".");

			_tmp[0] =(unsigned char)( reg[i] >> 8) ;
			_tmp[1] =(unsigned char)( reg[i] & 0xff);

			_tmp[2] =(unsigned char)( reg[i+1] >> 8) ;
			_tmp[3] =(unsigned char)( reg[i+1] & 0xff);
			err = ar0832_i2c_write(sd,_tmp , 4);
			if (err < 0){
				v4l_info(client, "%s: register set failed\n", \
					__func__);
				v4l_info(client,"err i=%d %02x %02x %02x %02x \n",\
					i, _tmp[0],_tmp[1],_tmp[2],_tmp[3]); 
				return -1;
				
				}
		}
//	if(i%50 == 0) 	printk("\n");
	}
//	printk(" %d\n",i);
	
	return err;
}

/* ****************************************
	data is 3 bytes 
	{address , bit field, set or clear}
***************************************** */
static int ar0832_write_array3(struct v4l2_subdev *sd,unsigned  short * reg , int size)
{	
	unsigned char _tmp[4];
	int i,err=0;
	int rdata=0;
	int wdata=0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	for (i = 0; i < size ; i=i+3) {
		
		rdata = ar0832_i2c_read_reg(client,reg[i],2) & 0xffff;
		if(reg[i+2] == 1) wdata = rdata | reg[i+1];
		else wdata = rdata & (~reg[i+1]);
#if 0	
		printk("%x %x %x read reg: %x --> write %x \n",reg[i],reg[i+1],reg[i+2],rdata,wdata);
#endif
		//address
		_tmp[0] =(unsigned char)( reg[i] >> 8) ;
		_tmp[1] =(unsigned char)( reg[i] & 0xff);

		// modified data
		_tmp[2] =(unsigned char)( wdata >> 8) ;
		_tmp[3] =(unsigned char)( wdata & 0xff);
		err = ar0832_i2c_write(sd,_tmp , 4);
		if (err < 0){
			v4l_info(client, "%s: register set failed\n", \
				__func__);
			v4l_info(client,"err i=%d %02x %02x %02x %02x \n",\
				i, _tmp[0],_tmp[1],_tmp[2],_tmp[3]); 
			return -1;
			
			}
		
//	if(i%50 == 0) 	printk("\n");
	}
//	printk(" %d\n",i);
	
	return err;
}

static int ar0832_mem_read(struct v4l2_subdev *sd, u16 len, u32 addr, u8 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[8];
	unsigned char recv_data[len + 3];
	int i, err = 0;

	if (!client->adapter)
		return -ENODEV;

	if (len <= 0)
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	/* high byte goes out first */
	data[0] = 0x00;
	data[1] = 0x03;
	data[2] = (addr >> 24) & 0xFF;
	data[3] = (addr >> 16) & 0xFF;
	data[4] = (addr >> 8) & 0xFF;
	data[5] = addr & 0xFF;
	data[6] = (len >> 8) & 0xFF;
	data[7] = len & 0xFF;

	for (i = AR0832_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1)
		return err;

	msg.flags = I2C_M_RD;
	msg.len = sizeof(recv_data);
	msg.buf = recv_data;
	for (i = AR0832_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1)
		return err;

	if (len != (recv_data[1] << 8 | recv_data[2]))
		cam_i2c_dbg("expected length %d, but return length %d\n",
			len, recv_data[1] << 8 | recv_data[2]);

	memcpy(val, recv_data + 3, len);

	cam_i2c_dbg("address %#x, length %d\n", addr, len);
	return err;
}

static int ar0832_mem_write(struct v4l2_subdev *sd, u8 cmd, u16 len, u32 addr, u8 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[len + 8];
	int i, err = 0;

	if (!client->adapter)
		return -ENODEV;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	/* high byte goes out first */
	data[0] = 0x00;
	data[1] = cmd;
	data[2] = (addr >> 24) & 0xFF;
	data[3] = (addr >> 16) & 0xFF;
	data[4] = (addr >> 8) & 0xFF;
	data[5] = addr & 0xFF;
	data[6] = (len >> 8) & 0xFF;
	data[7] = len & 0xFF;
	memcpy(data + 2 + sizeof(addr) + sizeof(len), val, len);

	cam_i2c_dbg("address %#x, length %d\n", addr, len);

	for (i = AR0832_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	return err;
}

static irqreturn_t ar0832_isp_isr(int irq, void *dev_id)
{
	struct v4l2_subdev *sd = (struct v4l2_subdev *)dev_id;
	struct ar0832_state *state = to_state(sd);

	cam_dbg("**************** interrupt ****************\n");
	state->isp.issued = 1;
	wake_up_interruptible(&state->isp.wait);

	return IRQ_HANDLED;
}

static u32 ar0832_wait_interrupt(struct v4l2_subdev *sd,
	unsigned int timeout)
{
	struct ar0832_state *state = to_state(sd);
	cam_trace("E\n");
return 0;
	if (wait_event_interruptible_timeout(state->isp.wait,
		state->isp.issued == 1,
		msecs_to_jiffies(timeout)) == 0) {
		cam_err("timeout\n");
		return 0;
	}

	state->isp.issued = 0;

	ar0832_readb(sd, AR0832_CATEGORY_SYS,
		AR0832_SYS_INT_FACTOR, &state->isp.int_factor);

	cam_trace("X\n");
	return state->isp.int_factor;
}

static int ar0832_set_mode(struct v4l2_subdev *sd, u32 mode)
{
	int i, err;
	u32 old_mode, val;
	cam_trace("E\n");

	err = ar0832_readb(sd, AR0832_CATEGORY_SYS, AR0832_SYS_MODE, &old_mode);

	if (err < 0)
		return err;

	if (old_mode == mode) {
		cam_dbg("%#x -> %#x\n", old_mode, mode);
		return old_mode;
	}

	cam_dbg("%#x -> %#x\n", old_mode, mode);
	
	switch (old_mode) {
	case AR0832_SYSINIT_MODE:
		cam_warn("sensor is initializing\n");
		err = -EBUSY;
		break;

	case AR0832_PARMSET_MODE:
		if (mode == AR0832_STILLCAP_MODE) {
			err = ar0832_writeb(sd, AR0832_CATEGORY_SYS,
				AR0832_SYS_MODE, AR0832_MONITOR_MODE);
			if (err < 0)
				break;
			for (i = AR0832_I2C_VERIFY; i; i--) {
				err = ar0832_readb(sd, AR0832_CATEGORY_SYS,
					AR0832_SYS_MODE, &val);
				if (val == AR0832_MONITOR_MODE)
					break;
				msleep(10);
			}
		}
	case AR0832_MONITOR_MODE:
	case AR0832_STILLCAP_MODE:
		err = ar0832_writeb(sd, AR0832_CATEGORY_SYS,
			AR0832_SYS_MODE, mode);
		for (i = AR0832_I2C_VERIFY; i; i--) {
			err = ar0832_readb(sd, AR0832_CATEGORY_SYS,
				AR0832_SYS_MODE, &val);
			if (val == AR0832_MONITOR_MODE)
				break;
			msleep(10);
		}
		break;

	default:
		cam_warn("current mode is unknown, %d\n", old_mode);
		err = -EINVAL;
	}

	if (err < 0)
		return err;

	for (i = AR0832_I2C_VERIFY; i; i--) {
		err = ar0832_readb(sd, AR0832_CATEGORY_SYS,
			AR0832_SYS_MODE, &val);
		if (val == mode)
			break;
		msleep(10);
	}

	cam_trace("X\n");
	return old_mode;
}

/*
 * v4l2_subdev_core_ops
 */
static int ar0832_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ar0832_ctrls); i++) {
		if (qc->id == ar0832_ctrls[i].id) {
			qc->maximum = ar0832_ctrls[i].maximum;
			qc->minimum = ar0832_ctrls[i].minimum;
			qc->step = ar0832_ctrls[i].step;
			qc->default_value = ar0832_ctrls[i].default_value;
			return 0;
		}
	}

	return -EINVAL;
}

static int ar0832_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct ar0832_state *state = to_state(sd);
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_AUTO_FOCUS_RESULT:
		ctrl->value = state->focus.status;
		break;

	case V4L2_CID_CAM_JPEG_MEMSIZE:
		ctrl->value = AR0832_JPEG_MAXSIZE +
			AR0832_THUMB_MAXSIZE + AR0832_POST_MAXSIZE;
		break;

	case V4L2_CID_CAM_JPEG_MAIN_SIZE:
		ctrl->value = state->jpeg.main_size;
		break;

	case V4L2_CID_CAM_JPEG_MAIN_OFFSET:
		ctrl->value = state->jpeg.main_offset;
		break;

	case V4L2_CID_CAM_JPEG_THUMB_SIZE:
		ctrl->value = state->jpeg.thumb_size;
		break;

	case V4L2_CID_CAM_JPEG_THUMB_OFFSET:
		ctrl->value = state->jpeg.thumb_offset;
		break;

	case V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET:
		ctrl->value = state->jpeg.postview_offset;
		break;

	case V4L2_CID_CAMERA_EXIF_FLASH:
		ctrl->value = state->exif.flash;
		break;

	case V4L2_CID_CAMERA_EXIF_ISO:
		ctrl->value = state->exif.iso;
		break;

	case V4L2_CID_CAMERA_EXIF_TV:
		ctrl->value = state->exif.tv;
		break;

	case V4L2_CID_CAMERA_EXIF_BV:
		ctrl->value = state->exif.bv;
		break;

	case V4L2_CID_CAMERA_EXIF_EBV:
		ctrl->value = state->exif.ebv;
		break;

	default:
		cam_err("no such control id %d\n",
				ctrl->id - V4L2_CID_PRIVATE_BASE);
		/*err = -ENOIOCTLCMD*/
		err = 0;
		break;
	}

	if (err < 0 && err != -ENOIOCTLCMD)
		cam_err("failed, id %d\n", ctrl->id - V4L2_CID_PRIVATE_BASE);

	return err;
}

#ifdef CONFIG_TARGET_LOCALE_KOR
static int ar0832_set_antibanding(struct v4l2_subdev *sd, int val)
{
	int antibanding = 0x02;	/* Fix 60Hz for domastic */
	int err = 0;

	cam_dbg("E, value %d\n", val);

	antibanding = val;

	err = ar0832_writeb(sd, AR0832_CATEGORY_AE,	AR0832_AE_FLICKER, antibanding);
	CHECK_ERR(err);

	cam_trace("X\n");
	return err;
}
#endif

static int ar0832_set_af_softlanding(struct v4l2_subdev *sd)
{
	struct ar0832_state *state = to_state(sd);
	u32 status = 0;
	int i, err = 0;

	cam_trace("E\n");

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	err = ar0832_set_mode(sd, AR0832_MONITOR_MODE);
	if (err <= 0) {
		cam_err("failed to set mode\n");
		return err;
	}

	err = ar0832_writeb(sd, AR0832_CATEGORY_LENS, AR0832_LENS_AF_MODE, 0x07);
	CHECK_ERR(err);

	for (i = AR0832_I2C_VERIFY; i; i--) {
		msleep(10);
		err = ar0832_readb(sd, AR0832_CATEGORY_LENS,
			AR0832_LENS_AF_STATUS, &status);
		CHECK_ERR(err);

		if ((status & 0x01) == 0x00)
			break;
	}

	if ((status & 0x01) != 0x00) {
		cam_err("failed\n");
		return -ETIMEDOUT;
	}

	cam_trace("X\n");
	return err;
}

static int ar0832_dump_fw(struct v4l2_subdev *sd)
{
	struct file *fp;
	mm_segment_t old_fs;
	u8 *buf, val;
	u32 addr, unit, count, intram_unit = 0x1000;
	int i, j, err;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(AR0832_FW_DUMP_PATH,
		O_WRONLY|O_CREAT|O_TRUNC, S_IRUGO|S_IWUGO|S_IXUSR);
	if (IS_ERR(fp)) {
		cam_err("failed to open %s, err %ld\n",
			AR0832_FW_DUMP_PATH, PTR_ERR(fp));
		err = -ENOENT;
		goto out0;
	}

	buf = kmalloc(intram_unit, GFP_KERNEL);
	if (!buf) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out0;
	}

	cam_dbg("start, file path %s\n", AR0832_FW_DUMP_PATH);

	/* set pin */
	val = 0x7E;
	err = ar0832_mem_write(sd, 0x04, sizeof(val), 0x50000308, &val);
	if (err < 0) {
		cam_err("i2c falied, err %d\n", err);
		goto out1;
	}

	addr = AR0832_FLASH_BASE_ADDR;
	unit = SZ_64K;
	count = 31;
	for (i = 0; i < count; i++) {
		for (j = 0; j < unit; j += intram_unit) {
			err = ar0832_mem_read(sd,
				intram_unit, addr + (i * unit) + j, buf);
			if (err < 0) {
				cam_err("i2c falied, err %d\n", err);
				goto out1;
			}
			vfs_write(fp, buf, intram_unit, &fp->f_pos);
		}
	}

	addr = AR0832_FLASH_BASE_ADDR + SZ_64K * count;
	unit = SZ_8K;
	count = 4;
	for (i = 0; i < count; i++) {
		for (j = 0; j < unit; j += intram_unit) {
			err = ar0832_mem_read(sd,
				intram_unit, addr + (i * unit) + j, buf);
			if (err < 0) {
				cam_err("i2c falied, err %d\n", err);
				goto out1;
			}
			vfs_write(fp, buf, intram_unit, &fp->f_pos);
		}
	}

	cam_dbg("end\n");
out1:
	kfree(buf);
out0:
	if (!IS_ERR(fp))
		filp_close(fp, current->files);
	set_fs(old_fs);

	return err;
}

static int ar0832_get_sensor_fw_version(struct v4l2_subdev *sd,
	char *buf)
{
	u8 val;
	int err;

	/* set pin */
	val = 0x7E;
	err = ar0832_mem_write(sd, 0x04, sizeof(val), 0x50000308, &val);
	CHECK_ERR(err);

	err = ar0832_mem_read(sd, AR0832_FW_VER_LEN,
		AR0832_FLASH_BASE_ADDR + AR0832_FW_VER_FILE_CUR, buf);

	cam_dbg("%s\n", buf);
	return 0;
}

static int ar0832_get_phone_fw_version(struct v4l2_subdev *sd,
	char *buf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->adapter->dev;
	u8 sensor_ver[AR0832_FW_VER_LEN] = {0, };
	const struct firmware *fentry;
	int err;

#ifdef SDCARD_FW
	struct file *fp;
	mm_segment_t old_fs;
	long nread;
	int fw_requested = 1;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(AR0832_FW_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_trace("failed to open %s, err %ld\n", AR0832_FW_PATH, PTR_ERR(fp));
		goto request_fw;
	}

	fw_requested = 0;
	err = vfs_llseek(fp, AR0832_FW_VER_FILE_CUR, SEEK_SET);
	if (err < 0) {
		cam_warn("failed to fseek, %d\n", err);
		goto out;
	}

	nread = vfs_read(fp, (char __user *)buf, AR0832_FW_VER_LEN, &fp->f_pos);
	if (nread != AR0832_FW_VER_LEN) {
		cam_err("failed to read firmware file, %ld Bytes\n", nread);
		err = -EIO;
		goto out;
	}

request_fw:
	if (fw_requested) {
		set_fs(old_fs);
#endif /* SDCARD_FW */
	ar0832_get_sensor_fw_version(sd, sensor_ver);

	if (sensor_ver[0] == 'T')
		err = request_firmware(&fentry, AR0832T_FW_REQUEST_PATH, dev);
	else
		err = request_firmware(&fentry, AR0832O_FW_REQUEST_PATH, dev);

	if (err != 0) {
		cam_err("request_firmware falied\n");
		err = -EINVAL;
		goto out;
	}

	memcpy(buf, (u8 *)&fentry->data[AR0832_FW_VER_FILE_CUR], AR0832_FW_VER_LEN);
#ifdef SDCARD_FW
	}
#endif /* SDCARD_FW */

out:
#ifdef SDCARD_FW
	if (!fw_requested) {
		filp_close(fp, current->files);
		set_fs(old_fs);
	}
#endif  /* SDCARD_FW */

	cam_dbg("%s\n", buf);
	return 0;
}

static int ar0832_check_fw(struct v4l2_subdev *sd)
{
	struct ar0832_state *state = to_state(sd);
	u8 sensor_ver[AR0832_FW_VER_LEN] = "FAILED Fujitsu AR0832LS";
	u8 phone_ver[AR0832_FW_VER_LEN] = "FAILED Fujitsu AR0832LS";
	int af_cal_h = 0, af_cal_l = 0;
	int rg_cal_h = 0, rg_cal_l = 0;
	int bg_cal_h = 0, bg_cal_l = 0;
	int update_count = 0;
	u32 int_factor;
	int err;

	cam_trace("E\n");

	/* F/W version */
	ar0832_get_phone_fw_version(sd, phone_ver);

	if (state->isp.bad_fw)
		goto out;

	ar0832_get_sensor_fw_version(sd, sensor_ver);

	err = ar0832_writeb(sd, AR0832_CATEGORY_FLASH, AR0832_FLASH_CAM_START, 0x01);
	CHECK_ERR(err);

	int_factor = ar0832_wait_interrupt(sd, AR0832_ISP_TIMEOUT);
	if (!(int_factor & AR0832_INT_MODE)) {
		cam_err("firmware was erased?\n");
		return -ETIMEDOUT;
	}

	err = ar0832_readb(sd, AR0832_CATEGORY_LENS, AR0832_LENS_AF_CAL, &af_cal_l);
	CHECK_ERR(err);

	err = ar0832_readb(sd, AR0832_CATEGORY_ADJST, AR0832_ADJST_AWB_RG_H, &rg_cal_h);
	CHECK_ERR(err);
	err = ar0832_readb(sd, AR0832_CATEGORY_ADJST, AR0832_ADJST_AWB_RG_L, &rg_cal_l);
	CHECK_ERR(err);

	err = ar0832_readb(sd, AR0832_CATEGORY_ADJST, AR0832_ADJST_AWB_BG_H, &bg_cal_h);
	CHECK_ERR(err);
	err = ar0832_readb(sd, AR0832_CATEGORY_ADJST, AR0832_ADJST_AWB_BG_L, &bg_cal_l);
	CHECK_ERR(err);

out:
	if (!state->fw_version) {
		state->fw_version = kzalloc(50, GFP_KERNEL);
		if (!state->fw_version) {
			cam_err("no memory for F/W version\n");
			return -ENOMEM;
		}
	}

	sprintf(state->fw_version, "%s %s %d %x %x %x %x %x %x",
		sensor_ver, phone_ver, update_count,
		af_cal_h, af_cal_l, rg_cal_h, rg_cal_l, bg_cal_h, bg_cal_l);

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_sensor_mode(struct v4l2_subdev *sd, int val)
{
	struct ar0832_state *state = to_state(sd);
	int err;
	cam_dbg("E, value %d\n", val);

	err = ar0832_set_mode(sd, AR0832_PARMSET_MODE);
	CHECK_ERR(err);

	err = ar0832_writeb(sd, AR0832_CATEGORY_PARM,
		AR0832_PARM_HDMOVIE, val == SENSOR_MOVIE ? 0x01 : 0x00);
	CHECK_ERR(err);

	state->sensor_mode = val;

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_flash(struct v4l2_subdev *sd, int val, int recording)
{
	struct ar0832_state *state = to_state(sd);
	int light, flash;
	int err;
	cam_dbg("E, value %d\n", val);

	if (!recording)
		state->flash_mode = val;

	/* movie flash mode should be set when recording is started */
	if (state->sensor_mode == SENSOR_MOVIE && !recording)
		return 0;

retry:
	switch (val) {
	case FLASH_MODE_OFF:
		light = 0x00;
		flash = (state->sensor_mode == SENSOR_CAMERA) ? 0x00 : -1;
		break;

	case FLASH_MODE_AUTO:
		light = (state->sensor_mode == SENSOR_CAMERA) ? 0x02 : 0x04;
		flash = (state->sensor_mode == SENSOR_CAMERA) ? 0x02 : -1;
		break;

	case FLASH_MODE_ON:
		light = (state->sensor_mode == SENSOR_CAMERA) ? 0x01 : 0x03;
		flash = (state->sensor_mode == SENSOR_CAMERA) ? 0x01 : -1;
		break;

	case FLASH_MODE_TORCH:
		light = 0x03;
		flash = -1;
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = FLASH_MODE_OFF;
		goto retry;
	}

	if (light >= 0) {
		err = ar0832_writeb(sd, AR0832_CATEGORY_CAPPARM,
			AR0832_CAPPARM_LIGHT_CTRL, light);
		CHECK_ERR(err);
	}

	if (flash >= 0) {
		err = ar0832_writeb(sd, AR0832_CATEGORY_CAPPARM,
			AR0832_CAPPARM_FLASH_CTRL, flash);
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_iso(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	u32 iso[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	ar0832_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	val -= qc.minimum;

	err = ar0832_writeb(sd, AR0832_CATEGORY_AE, AR0832_AE_ISOSEL, iso[val]);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_metering(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case METERING_CENTER:
		err = ar0832_writeb(sd, AR0832_CATEGORY_AE, AR0832_AE_MODE, 0x03);
		CHECK_ERR(err);
		break;
	case METERING_SPOT:
		err = ar0832_writeb(sd, AR0832_CATEGORY_AE, AR0832_AE_MODE, 0x06);
		CHECK_ERR(err);
		break;
	case METERING_MATRIX:
		err = ar0832_writeb(sd, AR0832_CATEGORY_AE, AR0832_AE_MODE, 0x01);
		CHECK_ERR(err);
		break;
	default:
		cam_warn("invalid value, %d\n", val);
		val = METERING_CENTER;
		goto retry;
	}

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_exposure(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	u32 exposure[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	ar0832_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	val -= qc.minimum;

	err = ar0832_writeb(sd, AR0832_CATEGORY_AE,
		AR0832_AE_INDEX, exposure[val]);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_whitebalance(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case WHITE_BALANCE_AUTO:
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MODE, 0x01);
		CHECK_ERR(err);
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MANUAL, 0x01);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_SUNNY:
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MANUAL, 0x04);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_CLOUDY:
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MANUAL, 0x05);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_TUNGSTEN:
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MANUAL, 0x01);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_FLUORESCENT:
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = ar0832_writeb(sd, AR0832_CATEGORY_WB,
			AR0832_WB_AWB_MANUAL, 0x02);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = WHITE_BALANCE_AUTO;
		goto retry;
	}

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_sharpness(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	u32 sharpness[] = {0x03, 0x04, 0x05, 0x06, 0x07};
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	ar0832_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	val -= qc.minimum;

	err = ar0832_writeb(sd, AR0832_CATEGORY_MON,
		AR0832_MON_EDGE_LVL, sharpness[val]);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_saturation(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	u32 saturation[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	ar0832_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	val -= qc.minimum;

	err = ar0832_writeb(sd, AR0832_CATEGORY_MON,
		AR0832_MON_CHROMA_LVL, saturation[val]);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_scene_mode(struct v4l2_subdev *sd, int val)
{
	struct v4l2_control ctrl;
	int evp, iso, brightness, whitebalance, sharpness, saturation;
	int err;
	cam_dbg("E, value %d\n", val);

	iso = ISO_AUTO;
	brightness = EV_DEFAULT;
	whitebalance = WHITE_BALANCE_AUTO;
	sharpness = SHARPNESS_DEFAULT;
	saturation = CONTRAST_DEFAULT;

retry:
	switch (val) {
	case SCENE_MODE_NONE:
		evp = 0x00;
		break;

	case SCENE_MODE_PORTRAIT:
		evp = 0x01;
		sharpness = SHARPNESS_MINUS_1;
		break;

	case SCENE_MODE_LANDSCAPE:
		evp = 0x02;
		sharpness = SHARPNESS_PLUS_1;
		saturation = SATURATION_PLUS_1;
		break;

	case SCENE_MODE_SPORTS:
		evp = 0x03;
		break;

	case SCENE_MODE_PARTY_INDOOR:
		evp = 0x04;
		/*iso = ISO_200; sensor will set internally */
		saturation = SATURATION_PLUS_1;
		break;

	case SCENE_MODE_BEACH_SNOW:
		evp = 0x05;
		/*iso = ISO_50; sensor will set internally */
		brightness = EV_PLUS_2;
		saturation = SATURATION_PLUS_1;
		break;

	case SCENE_MODE_SUNSET:
		evp = 0x06;
		whitebalance = WHITE_BALANCE_SUNNY;
		break;

	case SCENE_MODE_DUSK_DAWN:
		evp = 0x07;
		whitebalance = WHITE_BALANCE_FLUORESCENT;
		break;

	case SCENE_MODE_FALL_COLOR:
		evp = 0x08;
		saturation = SATURATION_PLUS_2;
		break;

	case SCENE_MODE_NIGHTSHOT:
		evp = 0x09;
		break;

	case SCENE_MODE_BACK_LIGHT:
		evp = 0x0A;
		break;

	case SCENE_MODE_FIREWORKS:
		evp = 0x0B;
		/*iso = ISO_50; sensor will set internally */
		break;

	case SCENE_MODE_TEXT:
		evp = 0x0C;
		sharpness = SHARPNESS_PLUS_2;
		break;

	case SCENE_MODE_CANDLE_LIGHT:
		evp = 0x0D;
		whitebalance = WHITE_BALANCE_SUNNY;
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = SCENE_MODE_NONE;
		goto retry;
	}

	/* EV-P */
	err = ar0832_writeb(sd, AR0832_CATEGORY_AE, AR0832_AE_EP_MODE_MON, evp);
	CHECK_ERR(err);
	err = ar0832_writeb(sd, AR0832_CATEGORY_AE, AR0832_AE_EP_MODE_CAP, evp);
	CHECK_ERR(err);

	/* ISO */
	ctrl.id = V4L2_CID_CAMERA_ISO;
	ctrl.value = iso;
	ar0832_set_iso(sd, &ctrl);

	/* EV Bias */
	ctrl.id = V4L2_CID_CAMERA_BRIGHTNESS;
	ctrl.value = brightness;
	ar0832_set_exposure(sd, &ctrl);

	/* AWB */
	ar0832_set_whitebalance(sd, whitebalance);

	/* Chroma Saturation */
	ctrl.id = V4L2_CID_CAMERA_SATURATION;
	ctrl.value = saturation;
	ar0832_set_saturation(sd, &ctrl);

	/* Sharpness */
	ctrl.id = V4L2_CID_CAMERA_SHARPNESS;
	ctrl.value = sharpness;
	ar0832_set_sharpness(sd, &ctrl);

	/* Emotional Color */
	err = ar0832_writeb(sd, AR0832_CATEGORY_CAPPARM,
		AR0832_CAPPARM_MCC_MODE, val == SCENE_MODE_NONE ? 0x01 : 0x00);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_effect_color(struct v4l2_subdev *sd, int val)
{
	u32 int_factor;
	int on, old_mode, cb, cr;
	int err;

	err = ar0832_readb(sd, AR0832_CATEGORY_PARM, AR0832_PARM_EFFECT, &on);
	CHECK_ERR(err);
	if (on)	{
		old_mode = ar0832_set_mode(sd, AR0832_PARMSET_MODE);
		CHECK_ERR(old_mode);

		err = ar0832_writeb(sd, AR0832_CATEGORY_PARM, AR0832_PARM_EFFECT, 0);
		CHECK_ERR(err);

		if (old_mode == AR0832_MONITOR_MODE) {
			err = ar0832_set_mode(sd, old_mode);
			CHECK_ERR(err);

			int_factor = ar0832_wait_interrupt(sd, AR0832_ISP_TIMEOUT);
			if (!(int_factor & AR0832_INT_MODE)) {
				cam_err("AR0832_INT_MODE isn't issued, %#x\n",
					int_factor);
				return -ETIMEDOUT;
			}
			CHECK_ERR(err);
		}
	}

	switch (val) {
	case IMAGE_EFFECT_NONE:
		break;

	case IMAGE_EFFECT_SEPIA:
		cb = 0xD8;
		cr = 0x18;
		break;

	case IMAGE_EFFECT_BNW:
		cb = 0x00;
		cr = 0x00;
		break;
	}

	err = ar0832_writeb(sd, AR0832_CATEGORY_MON,
		AR0832_MON_COLOR_EFFECT, val == IMAGE_EFFECT_NONE ? 0x00 : 0x01);
		CHECK_ERR(err);

	if (val != IMAGE_EFFECT_NONE) {
		err = ar0832_writeb(sd, AR0832_CATEGORY_MON, AR0832_MON_CFIXB, cb);
		CHECK_ERR(err);
		err = ar0832_writeb(sd, AR0832_CATEGORY_MON, AR0832_MON_CFIXR, cr);
		CHECK_ERR(err);
	}

	return 0;
}

static int ar0832_set_effect_gamma(struct v4l2_subdev *sd, s32 val)
{
	u32 int_factor;
	int on, effect, old_mode;
	int err;

	err = ar0832_readb(sd, AR0832_CATEGORY_MON, AR0832_MON_COLOR_EFFECT, &on);
	CHECK_ERR(err);
	if (on) {
		err = ar0832_writeb(sd, AR0832_CATEGORY_MON,
			AR0832_MON_COLOR_EFFECT, 0);
		CHECK_ERR(err);
	}

	switch (val) {
	case IMAGE_EFFECT_NEGATIVE:
		effect = 0x01;
		break;

	case IMAGE_EFFECT_AQUA:
		effect = 0x08;
		break;
	}

	old_mode = ar0832_set_mode(sd, AR0832_PARMSET_MODE);
	CHECK_ERR(old_mode);

	err = ar0832_writeb(sd, AR0832_CATEGORY_PARM, AR0832_PARM_EFFECT, effect);
	CHECK_ERR(err);

	if (old_mode == AR0832_MONITOR_MODE) {
		err = ar0832_set_mode(sd, old_mode);
		CHECK_ERR(err);

		int_factor = ar0832_wait_interrupt(sd, AR0832_ISP_TIMEOUT);
		if (!(int_factor & AR0832_INT_MODE)) {
			cam_err("AR0832_INT_MODE isn't issued, %#x\n",
				int_factor);
			return -ETIMEDOUT;
		}
		CHECK_ERR(err);
	}

	return err;
}

static int ar0832_set_effect(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case IMAGE_EFFECT_NONE:
	case IMAGE_EFFECT_BNW:
	case IMAGE_EFFECT_SEPIA:
		err = ar0832_set_effect_color(sd, val);
		CHECK_ERR(err);
		break;

	case IMAGE_EFFECT_AQUA:
	case IMAGE_EFFECT_NEGATIVE:
		err = ar0832_set_effect_gamma(sd, val);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = IMAGE_EFFECT_NONE;
		goto retry;
	}

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_wdr(struct v4l2_subdev *sd, int val)
{
	int contrast, wdr, err;

	cam_dbg("%s\n", val ? "on" : "off");

	contrast = (val == 1 ? 0x09 : 0x05);
	wdr = (val == 1 ? 0x01 : 0x00);

	err = ar0832_writeb(sd, AR0832_CATEGORY_MON,
			AR0832_MON_TONE_CTRL, contrast);
		CHECK_ERR(err);
	err = ar0832_writeb(sd, AR0832_CATEGORY_CAPPARM,
			AR0832_CAPPARM_WDR_EN, wdr);
		CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_antishake(struct v4l2_subdev *sd, int val)
{
	int ahs, err;

	cam_dbg("%s\n", val ? "on" : "off");

	ahs = (val == 1 ? 0x0E : 0x00);

	err = ar0832_writeb(sd, AR0832_CATEGORY_AE, AR0832_AE_EP_MODE_MON, ahs);
		CHECK_ERR(err);
	err = ar0832_writeb(sd, AR0832_CATEGORY_AE, AR0832_AE_EP_MODE_CAP, ahs);
		CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_face_beauty(struct v4l2_subdev *sd, int val)
{
	struct ar0832_state *state = to_state(sd);
	int err;

	cam_dbg("%s\n", val ? "on" : "off");

	err = ar0832_writeb(sd, AR0832_CATEGORY_CAPPARM,
		AR0832_CAPPARM_AFB_CAP_EN, val ? 0x01 : 0x00);
	CHECK_ERR(err);

	state->beauty_mode = val;

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_lock(struct v4l2_subdev *sd, int val)
{
	struct ar0832_state *state = to_state(sd);

	cam_trace("%s\n", val ? "on" : "off");

	ar0832_writeb(sd, AR0832_CATEGORY_AE, AR0832_AE_LOCK, val);
	ar0832_writeb(sd, AR0832_CATEGORY_WB, AR0832_AWB_LOCK, val);
	state->focus.lock = val;

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_af(struct v4l2_subdev *sd, int val)
{
	struct ar0832_state *state = to_state(sd);
	int i, status, err;

	cam_info("%s, mode %#x\n", val ? "start" : "stop", state->focus.mode);

	state->focus.status = 0;

	if (state->focus.mode != FOCUS_MODE_CONTINOUS) {
		err = ar0832_writeb(sd, AR0832_CATEGORY_LENS,
			AR0832_LENS_AF_START, val);
		CHECK_ERR(err);

		if (!(state->focus.touch &&
			state->focus.mode == FOCUS_MODE_TOUCH)) {
			if (val && state->focus.lock) {
				ar0832_set_lock(sd, 0);
				msleep(100);
			}
			ar0832_set_lock(sd, val);
		}

		/* check AF status for 6 sec */
		for (i = 600; i && err; i--) {
			msleep(10);
			err = ar0832_readb(sd, AR0832_CATEGORY_LENS,
				AR0832_LENS_AF_STATUS, &status);
			CHECK_ERR(err);

			if (!(status & 0x01))
				err = 0;
		}

		state->focus.status = status;
	} else {
		err = ar0832_writeb(sd, AR0832_CATEGORY_LENS,
			AR0832_LENS_AF_START, val ? 0x02 : 0x00);
		CHECK_ERR(err);

		err = -EBUSY;
		for (i = AR0832_I2C_VERIFY; i && err; i--) {
			msleep(10);
			err = ar0832_readb(sd, AR0832_CATEGORY_LENS,
				AR0832_LENS_AF_STATUS, &status);
			CHECK_ERR(err);

			if ((val && status == 0x05) || (!val && status != 0x05))
				err = 0;
		}
	}

	cam_dbg("X\n");
	return err;
}

static int ar0832_set_af_mode(struct v4l2_subdev *sd, int val)
{
	struct ar0832_state *state = to_state(sd);
	struct regulator *movie = regulator_get(NULL, "led_movie");
	u32 cancel, mode, status = 0;
	int i, err;

	cancel = val & FOCUS_MODE_DEFAULT;
	val &= 0xFF;

retry:
	switch (val) {
	case FOCUS_MODE_AUTO:
		mode = 0x00;
		break;

	case FOCUS_MODE_MACRO:
		mode = 0x01;
		break;

	case FOCUS_MODE_CONTINOUS:
		mode = 0x02;
		cancel = 0;
		break;

	case FOCUS_MODE_FACEDETECT:
		mode = 0x03;
		break;

	case FOCUS_MODE_TOUCH:
		mode = 0x04;
		cancel = 0;
		break;

	case FOCUS_MODE_INFINITY:
		mode = 0x06;
		cancel = 0;
		break;

	default:
		cam_warn("invalid value, %d", val);
		val = FOCUS_MODE_AUTO;
		goto retry;
	}

	if (cancel) {
		ar0832_set_af(sd, 0);
		ar0832_set_lock(sd, 0);
	} else {
		if (state->focus.mode == val)
			return 0;
	}

	cam_dbg("E, value %d\n", val);

	if (val == FOCUS_MODE_FACEDETECT) {
		/* enable face detection */
		err = ar0832_writeb(sd, AR0832_CATEGORY_FD, AR0832_FD_CTL, 0x11);
		CHECK_ERR(err);
		msleep(10);
	} else if (state->focus.mode == FOCUS_MODE_FACEDETECT) {
		/* disable face detection */
		err = ar0832_writeb(sd, AR0832_CATEGORY_FD, AR0832_FD_CTL, 0x00);
		CHECK_ERR(err);
	}

	if (val == FOCUS_MODE_MACRO)
		regulator_set_current_limit(movie, 15000, 17000);
	else if (state->focus.mode == FOCUS_MODE_MACRO)
		regulator_set_current_limit(movie, 90000, 110000);

	state->focus.mode = val;

	err = ar0832_writeb(sd, AR0832_CATEGORY_LENS, AR0832_LENS_AF_MODE, mode);
	CHECK_ERR(err);

	for (i = AR0832_I2C_VERIFY; i; i--) {
		msleep(10);
		err = ar0832_readb(sd, AR0832_CATEGORY_LENS,
			AR0832_LENS_AF_STATUS, &status);
		CHECK_ERR(err);

		if (!(status & 0x01))
			break;
	}

	if ((status & 0x01) != 0x00) {
		cam_err("failed\n");
		return -ETIMEDOUT;
	}

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_touch_auto_focus(struct v4l2_subdev *sd, int val)
{
	struct ar0832_state *state = to_state(sd);
	int err;
	cam_info("%s\n", val ? "start" : "stop");

	state->focus.touch = val;

	if (val) {
		err = ar0832_set_af_mode(sd, FOCUS_MODE_TOUCH);
		if (err < 0) {
			cam_err("ar0832_set_af_mode failed\n");
			return err;
		}
		err = ar0832_writew(sd, AR0832_CATEGORY_LENS,
				AR0832_LENS_AF_TOUCH_POSX, state->focus.pos_x);
		CHECK_ERR(err);
		err = ar0832_writew(sd, AR0832_CATEGORY_LENS,
				AR0832_LENS_AF_TOUCH_POSY, state->focus.pos_y);
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return err;
}

static int ar0832_set_zoom(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct ar0832_state *state = to_state(sd);
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	int zoom[] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15, 17, 18, 19,
		20, 21, 22, 24, 25, 26, 28, 29, 30, 31, 32, 34, 35, 36, 38, 39};
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	ar0832_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	err = ar0832_writeb(sd, AR0832_CATEGORY_MON, AR0832_MON_ZOOM, zoom[val]);
	CHECK_ERR(err);

	state->zoom = val;

	cam_trace("X\n");
	return 0;
}

static int ar0832_set_jpeg_quality(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, ratio, err;
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	ar0832_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	err = ar0832_writeb(sd, AR0832_CATEGORY_CAPPARM,
		AR0832_CAPPARM_JPEG_RATIO, 0x62);
	CHECK_ERR(err);

	if (val <= 65)		/* Normal */
		ratio = 0x0A;
	else if (val <= 75)	/* Fine */
		ratio = 0x05;
	else			/* Superfine */
		ratio = 0x00;

	err = ar0832_writeb(sd, AR0832_CATEGORY_CAPPARM,
		AR0832_CAPPARM_JPEG_RATIO_OFS, ratio);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int ar0832_get_exif(struct v4l2_subdev *sd)
{
	struct ar0832_state *state = to_state(sd);
	/* standard values */
	u16 iso_std_values[] = { 10, 12, 16, 20, 25, 32, 40, 50, 64, 80,
		100, 125, 160, 200, 250, 320, 400, 500, 640, 800,
		1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000};
	/* quantization table */
	u16 iso_qtable[] = { 11, 14, 17, 22, 28, 35, 44, 56, 71, 89,
		112, 141, 178, 224, 282, 356, 449, 565, 712, 890,
		1122, 1414, 1782, 2245, 2828, 3564, 4490, 5657, 7127, 8909};
	int num, den, i, err;

	/* exposure time */
	err = ar0832_readl(sd, AR0832_CATEGORY_EXIF,
		AR0832_EXIF_EXPTIME_NUM, &num);
	CHECK_ERR(err);
	err = ar0832_readl(sd, AR0832_CATEGORY_EXIF,
		AR0832_EXIF_EXPTIME_DEN, &den);
	CHECK_ERR(err);
	state->exif.exptime = (u32)num*1000/den;

	/* flash */
	err = ar0832_readw(sd, AR0832_CATEGORY_EXIF, AR0832_EXIF_FLASH, &num);
	CHECK_ERR(err);
	state->exif.flash = (u16)num;

	/* iso */
	err = ar0832_readw(sd, AR0832_CATEGORY_EXIF, AR0832_EXIF_ISO, &num);
	CHECK_ERR(err);
	for (i = 0; i < ARRAY_SIZE(iso_qtable); i++) {
		if (num <= iso_qtable[i]) {
			state->exif.iso = iso_std_values[i];
			break;
		}
	}

	/* shutter speed */
	err = ar0832_readl(sd, AR0832_CATEGORY_EXIF, AR0832_EXIF_TV_NUM, &num);
	CHECK_ERR(err);
	err = ar0832_readl(sd, AR0832_CATEGORY_EXIF, AR0832_EXIF_TV_DEN, &den);
	CHECK_ERR(err);
	state->exif.tv = num*AR0832_DEF_APEX_DEN/den;

	/* brightness */
	err = ar0832_readl(sd, AR0832_CATEGORY_EXIF, AR0832_EXIF_BV_NUM, &num);
	CHECK_ERR(err);
	err = ar0832_readl(sd, AR0832_CATEGORY_EXIF, AR0832_EXIF_BV_DEN, &den);
	CHECK_ERR(err);
	state->exif.bv = num*AR0832_DEF_APEX_DEN/den;

	/* exposure */
	err = ar0832_readl(sd, AR0832_CATEGORY_EXIF, AR0832_EXIF_EBV_NUM, &num);
	CHECK_ERR(err);
	err = ar0832_readl(sd, AR0832_CATEGORY_EXIF, AR0832_EXIF_EBV_DEN, &den);
	CHECK_ERR(err);
	state->exif.ebv = num*AR0832_DEF_APEX_DEN/den;

	return err;
}

static int ar0832_start_capture(struct v4l2_subdev *sd, int val)
{
	struct ar0832_state *state = to_state(sd);
	int err, int_factor;
	cam_trace("E\n");

	if (!(state->isp.int_factor & AR0832_INT_CAPTURE)) {
		int_factor = ar0832_wait_interrupt(sd,
			state->beauty_mode ? AR0832_ISP_AFB_TIMEOUT : AR0832_ISP_TIMEOUT);
		if (!(int_factor & AR0832_INT_CAPTURE)) {
			cam_warn("AR0832_INT_CAPTURE isn't issued, %#x\n", int_factor);
			return -ETIMEDOUT;
		}
	}

	err = ar0832_writeb(sd, AR0832_CATEGORY_CAPCTRL, AR0832_CAPCTRL_FRM_SEL, 0x01);
	CHECK_ERR(err);

	err = ar0832_writeb(sd, AR0832_CATEGORY_CAPCTRL,
		AR0832_CAPCTRL_TRANSFER, 0x01);
	int_factor = ar0832_wait_interrupt(sd, AR0832_ISP_TIMEOUT);
	if (!(int_factor & AR0832_INT_CAPTURE)) {
		cam_warn("AR0832_INT_CAPTURE isn't issued on transfer, %#x\n", int_factor);
		return -ETIMEDOUT;
	}

	err = ar0832_readl(sd, AR0832_CATEGORY_CAPCTRL, AR0832_CAPCTRL_IMG_SIZE,
				&state->jpeg.main_size);
	CHECK_ERR(err);
	err = ar0832_readl(sd, AR0832_CATEGORY_CAPCTRL, AR0832_CAPCTRL_THUMB_SIZE,
				&state->jpeg.thumb_size);
	CHECK_ERR(err);

	state->jpeg.main_offset = 0;
	state->jpeg.thumb_offset = AR0832_JPEG_MAXSIZE;
	state->jpeg.postview_offset = AR0832_JPEG_MAXSIZE + AR0832_THUMB_MAXSIZE;

	ar0832_get_exif(sd);

	cam_trace("X\n");
	return err;
}

static int ar0832_set_hdr(struct v4l2_subdev *sd, int val)
{
	u32 int_factor;
	int err;
	cam_trace("E\n");

	switch (val) {
	case 0:
		err = ar0832_set_mode(sd, AR0832_MONITOR_MODE);
		CHECK_ERR(err);
		int_factor = ar0832_wait_interrupt(sd, AR0832_ISP_TIMEOUT);
		if (!(int_factor & AR0832_INT_MODE)) {
			cam_err("AR0832_INT_MODE isn't issued, %#x\n",
				int_factor);
			return -ETIMEDOUT;
		}
		break;
	case 1:
	case 2:
		err = ar0832_writeb(sd, AR0832_CATEGORY_SYS,
			AR0832_SYS_ROOT_EN, 0x01);
		int_factor = ar0832_wait_interrupt(sd, AR0832_ISP_TIMEOUT);
		break;
	default:
		cam_err("invalid HDR count\n");
	}

	cam_trace("X\n");
	return 0;
}

static int ar0832_check_dataline(struct v4l2_subdev *sd, int val)
{
	int err = 0;

	cam_dbg("E, value %d\n", val);

	err = ar0832_writeb(sd, AR0832_CATEGORY_TEST,
		AR0832_TEST_OUTPUT_YCO_TEST_DATA, val ? 0x01 : 0x00);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int ar0832_check_esd(struct v4l2_subdev *sd)
{
	s32 val = 0;
	int err = 0;

	/* check ISP */
	err = ar0832_readb(sd, AR0832_CATEGORY_TEST, AR0832_TEST_ISP_PROCESS, &val);
	CHECK_ERR(err);
	cam_dbg("progress %#x\n", val);

	if (val != 0x80) {
		goto esd_occur;
	} else {
		ar0832_wait_interrupt(sd, AR0832_ISP_ESD_TIMEOUT);

		err = ar0832_readb(sd, AR0832_CATEGORY_SYS, AR0832_SYS_ESD_INT, &val);
		CHECK_ERR(err);

		if (val & AR0832_INT_ESD)
			goto esd_occur;
	}

	cam_warn("ESD is not detected\n");
	return 0;

esd_occur:
	cam_warn("ESD shock is detected\n");
	return -EIO;
}

static int ar0832_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct ar0832_state *state = to_state(sd);
	int err = 0;

	printk(KERN_INFO "id %d, value %d\n",
		ctrl->id - V4L2_CID_PRIVATE_BASE, ctrl->value);

	if (unlikely(state->isp.bad_fw && ctrl->id != V4L2_CID_CAM_UPDATE_FW)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	switch (ctrl->id) {
	case V4L2_CID_CAM_UPDATE_FW:
		if (ctrl->value == FW_MODE_DUMP)
			err = ar0832_dump_fw(sd);
		else
			err = ar0832_check_fw(sd);
		break;

	case V4L2_CID_CAMERA_SENSOR_MODE:
		err = ar0832_set_sensor_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FLASH_MODE:
		err = ar0832_set_flash(sd, ctrl->value, 0);
		break;

	case V4L2_CID_CAMERA_ISO:
		err = ar0832_set_iso(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_METERING:
		if (state->sensor_mode == SENSOR_CAMERA)
			err = ar0832_set_metering(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_BRIGHTNESS:
		err = ar0832_set_exposure(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_WHITE_BALANCE:
		err = ar0832_set_whitebalance(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SCENE_MODE:
		err = ar0832_set_scene_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_EFFECT:
		err = ar0832_set_effect(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_WDR:
		err = ar0832_set_wdr(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_ANTI_SHAKE:
		err = ar0832_set_antishake(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_BEAUTY_SHOT:
		err = ar0832_set_face_beauty(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FOCUS_MODE:
		err = ar0832_set_af_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SET_AUTO_FOCUS:
		err = ar0832_set_af(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_OBJECT_POSITION_X:
		state->focus.pos_x = ctrl->value;
		break;

	case V4L2_CID_CAMERA_OBJECT_POSITION_Y:
		state->focus.pos_y = ctrl->value;
		break;

	case V4L2_CID_CAMERA_TOUCH_AF_START_STOP:
		err = ar0832_set_touch_auto_focus(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_ZOOM:
		err = ar0832_set_zoom(sd, ctrl);
		break;

	case V4L2_CID_CAM_JPEG_QUALITY:
		err = ar0832_set_jpeg_quality(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_CAPTURE:
		err = ar0832_start_capture(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_HDR:
		err = ar0832_set_hdr(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_VT_MODE:
		state->vt_mode = ctrl->value;
		break;

	case V4L2_CID_CAMERA_CHECK_DATALINE:
		state->check_dataline = ctrl->value;
		break;

	case V4L2_CID_CAMERA_CHECK_ESD:
		err = ar0832_check_esd(sd);
		break;

	default:
		cam_err("no such control id %d, value %d\n",
				ctrl->id - V4L2_CID_PRIVATE_BASE, ctrl->value);
		/*err = -ENOIOCTLCMD;*/
		err = 0;
		break;
	}

	if (err < 0 && err != -ENOIOCTLCMD)
		cam_err("failed, id %d, value %d\n",
				ctrl->id - V4L2_CID_PRIVATE_BASE, ctrl->value);
	return err;
}

static int ar0832_g_ext_ctrl(struct v4l2_subdev *sd, struct v4l2_ext_control *ctrl)
{
	struct ar0832_state *state = to_state(sd);
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_CAM_SENSOR_FW_VER:
		strcpy(ctrl->string, state->exif.unique_id);
		break;

	default:
		cam_err("no such control id %d\n", ctrl->id - V4L2_CID_CAMERA_CLASS_BASE);
		/*err = -ENOIOCTLCMD*/
		err = 0;
		break;
	}

	if (err < 0 && err != -ENOIOCTLCMD)
		cam_err("failed, id %d\n", ctrl->id - V4L2_CID_CAMERA_CLASS_BASE);

	return err;
}

static int ar0832_g_ext_ctrls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ctrls)
{
	struct v4l2_ext_control *ctrl = ctrls->controls;
	int i, err = 0;

	for (i = 0; i < ctrls->count; i++, ctrl++) {
		err = ar0832_g_ext_ctrl(sd, ctrl);
		if (err) {
			ctrls->error_idx = i;
			break;
		}
	}
	return err;
}

static int ar0832_check_manufacturer_id(struct v4l2_subdev *sd)
{
	int i, err;
	u8 id;
	u32 addr[] = {0x1000AAAA, 0x10005554, 0x1000AAAA};
	u8 val[3][2] = {
		[0] = {0x00, 0xAA},
		[1] = {0x00, 0x55},
		[2] = {0x00, 0x90},
	};
	u8 reset[] = {0x00, 0xF0};

	/* set manufacturer's ID read-mode */
	for (i = 0; i < 3; i++) {
		err = ar0832_mem_write(sd, 0x06, 2, addr[i], val[i]);
		CHECK_ERR(err);
	}

	/* read manufacturer's ID */
	err = ar0832_mem_read(sd, sizeof(id), 0x10000001, &id);
	CHECK_ERR(err);

	/* reset manufacturer's ID read-mode */
	err = ar0832_mem_write(sd, 0x06, sizeof(reset), 0x10000000, reset);
	CHECK_ERR(err);

	cam_dbg("%#x\n", id);

	return id;
}

static int ar0832_program_fw(struct v4l2_subdev *sd,
	u8 *buf, u32 addr, u32 unit, u32 count, u8 id)
{
	u32 val;
	u32 intram_unit = SZ_4K;
	int i, j, retries, err = 0;
	int erase = 0x01;
	if (unit == SZ_64K && id != 0x01)
		erase = 0x04;

	for (i = 0; i < unit*count; i += unit) {
		/* Set Flash ROM memory address */
		err = ar0832_writel(sd, AR0832_CATEGORY_FLASH,
			AR0832_FLASH_ADDR, addr + i);
		CHECK_ERR(err);

		/* Erase FLASH ROM entire memory */
		err = ar0832_writeb(sd, AR0832_CATEGORY_FLASH,
			AR0832_FLASH_ERASE, erase);
		CHECK_ERR(err);
		/* Response while sector-erase is operating */
		retries = 0;
		do {
			mdelay(50);
			err = ar0832_readb(sd, AR0832_CATEGORY_FLASH,
				AR0832_FLASH_ERASE, &val);
			CHECK_ERR(err);
		} while (val == erase && retries++ < AR0832_I2C_VERIFY);

		if (val != 0) {
			cam_err("failed to erase sector\n");
			return -1;
		}

		/* Set FLASH ROM programming size */
		err = ar0832_writew(sd, AR0832_CATEGORY_FLASH, AR0832_FLASH_BYTE,
			unit == SZ_64K ? 0 : unit);
		CHECK_ERR(err);

		/* Clear M-5MoLS internal RAM */
		err = ar0832_writeb(sd, AR0832_CATEGORY_FLASH,
			AR0832_FLASH_RAM_CLEAR, 0x01);
		CHECK_ERR(err);

		/* Set Flash ROM programming address */
		err = ar0832_writel(sd, AR0832_CATEGORY_FLASH,
			AR0832_FLASH_ADDR, addr + i);
		CHECK_ERR(err);

		/* Send programmed firmware */
		for (j = 0; j < unit; j += intram_unit) {
			err = ar0832_mem_write(sd, 0x04, intram_unit,
				AR0832_INT_RAM_BASE_ADDR + j, buf + i + j);
			CHECK_ERR(err);
			mdelay(10);
		}

		/* Start Programming */
		err = ar0832_writeb(sd, AR0832_CATEGORY_FLASH, AR0832_FLASH_WR, 0x01);
		CHECK_ERR(err);

		/* Confirm programming has been completed */
		retries = 0;
		do {
			mdelay(50);
			err = ar0832_readb(sd, AR0832_CATEGORY_FLASH,
				AR0832_FLASH_WR, &val);
			CHECK_ERR(err);
		} while (val && retries++ < AR0832_I2C_VERIFY);

		if (val != 0) {
			cam_err("failed to program\n");
			return -1;
		}
	}

	return 0;
}

static int ar0832_load_fw(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->adapter->dev;
	const struct firmware *fentry;
	u8 sensor_ver[AR0832_FW_VER_LEN] = {0, };
	u8 *buf = NULL, val, id;
	int offset, err;

#ifdef SDCARD_FW
	struct file *fp;
	mm_segment_t old_fs;
	long fsize, nread;
	int fw_requested = 1;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(AR0832_FW_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_trace("failed to open %s, err %ld\n",
			AR0832_FW_PATH, PTR_ERR(fp));
		goto request_fw;
	}

	fw_requested = 0;
	fsize = fp->f_path.dentry->d_inode->i_size;

	cam_dbg("start, file path %s, size %ld Bytes\n", AR0832_FW_PATH, fsize);

	buf = vmalloc(fsize);
	if (!buf) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	nread = vfs_read(fp, (char __user *)buf, fsize, &fp->f_pos);
	if (nread != fsize) {
		cam_err("failed to read firmware file, %ld Bytes\n", nread);
		err = -EIO;
		goto out;
	}

request_fw:
	if (fw_requested) {
		set_fs(old_fs);
#endif /* SDCARD_FW */
	ar0832_get_sensor_fw_version(sd, sensor_ver);

	if (sensor_ver[0] == 'T')
		err = request_firmware(&fentry, AR0832T_FW_REQUEST_PATH, dev);
	else
		err = request_firmware(&fentry, AR0832O_FW_REQUEST_PATH, dev);

	if (err != 0) {
		cam_err("request_firmware falied\n");
			err = -EINVAL;
			goto out;
	}

	cam_dbg("start, size %d Bytes\n", fentry->size);
	buf = (u8 *)fentry->data;

#ifdef SDCARD_FW
	}
#endif /* SDCARD_FW */

	/* set pin */
	val = 0x7E;
	err = ar0832_mem_write(sd, 0x04, sizeof(val), 0x50000308, &val);
	if (err < 0) {
		cam_err("i2c falied, err %d\n", err);
		goto out;
	}

	id = ar0832_check_manufacturer_id(sd);
	if (id < 0) {
		cam_err("i2c falied, err %d\n", id);
		goto out;
	}

	/* select flash memory */
	err = ar0832_writeb(sd, AR0832_CATEGORY_FLASH,
		AR0832_FLASH_SEL, id == 0x01 ? 0x00 : 0x01);
	if (err < 0) {
		cam_err("i2c falied, err %d\n", err);
		goto out;
	}

	/* program FLSH ROM */
	err = ar0832_program_fw(sd, buf, AR0832_FLASH_BASE_ADDR, SZ_64K, 31, id);
	if (err < 0)
		goto out;

	offset = SZ_64K * 31;
	if (id == 0x01) {
		err = ar0832_program_fw(sd,
			buf + offset, AR0832_FLASH_BASE_ADDR + offset, SZ_8K, 4, id);
	} else {
		err = ar0832_program_fw(sd,
			buf + offset, AR0832_FLASH_BASE_ADDR + offset, SZ_4K, 8, id);
	}

	cam_dbg("end\n");

out:
#ifdef SDCARD_FW
	if (!fw_requested) {
		vfree(buf);
		filp_close(fp, current->files);
		set_fs(old_fs);
	}
#endif  /* SDCARD_FW */
	return err;
}

/*
 * v4l2_subdev_video_ops
 */
static const struct ar0832_frmsizeenum *ar0832_get_frmsize
	(const struct ar0832_frmsizeenum *frmsizes, int num_entries, int index)
{
	int i;

	for (i = 0; i < num_entries; i++) {
		if (frmsizes[i].index == index)
			return &frmsizes[i];
	}

	return NULL;
}

static int ar0832_set_frmsize(struct v4l2_subdev *sd)
{
	struct ar0832_state *state = to_state(sd);
	struct v4l2_control ctrl;
	int err;
	u32 old_mode;
	cam_trace("E\n");

	if (state->format_mode != V4L2_PIX_FMT_MODE_CAPTURE) {
		err = ar0832_set_mode(sd, AR0832_PARMSET_MODE);
		err = ar0832_readb(sd, AR0832_CATEGORY_SYS, AR0832_SYS_MODE, &old_mode);

		CHECK_ERR(err);

		err = ar0832_writeb(sd, AR0832_CATEGORY_PARM,
			AR0832_PARM_MON_SIZE, state->preview->reg_val);
		CHECK_ERR(err);

		if (state->zoom) {
			/* Zoom position returns to 1 when the monitor size is changed. */
			ctrl.id = V4L2_CID_CAMERA_ZOOM;
			ctrl.value = state->zoom;
			ar0832_set_zoom(sd, &ctrl);
		}

		cam_info("preview frame size %dx%d\n",
			state->preview->width, state->preview->height);
	} else {
		err = ar0832_writeb(sd, AR0832_CATEGORY_CAPPARM,
			AR0832_CAPPARM_MAIN_IMG_SIZE, state->capture->reg_val);
		CHECK_ERR(err);
		cam_info("capture frame size %dx%d\n",
			state->capture->width, state->capture->height);
	}

	cam_trace("X\n");
	return 0;
}

static int ar0832_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *ffmt)
{
	struct ar0832_state *state = to_state(sd);
	const struct ar0832_frmsizeenum **frmsize;
	
	u32 width = ffmt->width;
	u32 height = ffmt->height;
	u32 tmp_width;
	u32 old_index;
	int i, num_entries;
	cam_trace("E\n");

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}
	if (ffmt->width < ffmt->height) {
		tmp_width = ffmt->height;
		height = ffmt->width;
		width = tmp_width;
	}

	if (ffmt->colorspace == V4L2_COLORSPACE_JPEG) {
		state->format_mode = V4L2_PIX_FMT_MODE_CAPTURE;
		frmsize = &state->capture;
	} else {
		state->format_mode = V4L2_PIX_FMT_MODE_PREVIEW;
		frmsize = &state->preview;
	}
	
	old_index = *frmsize ? (*frmsize)->index : -1;
	*frmsize = NULL;

	if (state->format_mode != V4L2_PIX_FMT_MODE_CAPTURE) {
		num_entries = ARRAY_SIZE(preview_frmsizes);
		for (i = 0; i < num_entries; i++) {
			if (width == preview_frmsizes[i].width &&
				height == preview_frmsizes[i].height) {
				*frmsize = &preview_frmsizes[i];
				break;
			}
		}
	} else {
		num_entries = ARRAY_SIZE(capture_frmsizes);
		for (i = 0; i < num_entries; i++) {
			if (width == capture_frmsizes[i].width &&
				height == capture_frmsizes[i].height) {
				*frmsize = &capture_frmsizes[i];
				break;
			}
		}
	}

	if (*frmsize == NULL) {
		cam_warn("invalid frame size %dx%d\n", width, height);
		*frmsize = state->format_mode != V4L2_PIX_FMT_MODE_CAPTURE ?
			ar0832_get_frmsize(preview_frmsizes, num_entries,
				AR0832_PREVIEW_VGA) :
			ar0832_get_frmsize(capture_frmsizes, num_entries,
				AR0832_CAPTURE_3MP);
	}

	cam_dbg("%dx%d\n", (*frmsize)->width, (*frmsize)->height);
	ar0832_set_frmsize(sd);

	cam_trace("X\n");
	return 0;
}

static int ar0832_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct ar0832_state *state = to_state(sd);

	a->parm.capture.timeperframe.numerator = 1;
	a->parm.capture.timeperframe.denominator = state->fps;

	return 0;
}

static int ar0832_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct ar0832_state *state = to_state(sd);
	int err;

	u32 fps = a->parm.capture.timeperframe.denominator /
					a->parm.capture.timeperframe.numerator;

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	if (fps != state->fps) {
		if (fps <= 0 || fps > 30) {
			cam_err("invalid frame rate %d\n", fps);
			fps = 30;
		}
		state->fps = fps;
	}

	err = ar0832_set_mode(sd, AR0832_PARMSET_MODE);
	CHECK_ERR(err);

	cam_dbg("fixed fps %d\n", state->fps);
	err = ar0832_writeb(sd, AR0832_CATEGORY_PARM,
		AR0832_PARM_FLEX_FPS, state->fps != 30 ? state->fps : 0);
	CHECK_ERR(err);

	return 0;
}

static int ar0832_enum_framesizes(struct v4l2_subdev *sd,
	struct v4l2_frmsizeenum *fsize)
{
	struct ar0832_state *state = to_state(sd);
	u32 err, old_mode;
	err = ar0832_readb(sd, AR0832_CATEGORY_SYS, AR0832_SYS_MODE, &old_mode);

	/*
	* The camera interface should read this value, this is the resolution
	* at which the sensor would provide framedata to the camera i/f
	* In case of image capture,
	* this returns the default camera resolution (VGA)
	*/
	if (state->format_mode != V4L2_PIX_FMT_MODE_CAPTURE) {
		if (state->preview == NULL || state->preview->index < 0)
			return -EINVAL;

		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = state->preview->width;
		fsize->discrete.height = state->preview->height;
//		fsize->discrete.width = 3272;
//		fsize->discrete.height =2456;
	} else {
		if (state->capture == NULL || state->capture->index < 0)
			return -EINVAL;

		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = state->capture->width;
		fsize->discrete.height = state->capture->height;
	}

	return 0;
}

static int ar0832_s_stream_preview(struct v4l2_subdev *sd, int enable)
{
	struct ar0832_state *state = to_state(sd);
	u32 old_mode, int_factor;
	int err;

	err = ar0832_readb(sd, AR0832_CATEGORY_SYS, AR0832_SYS_MODE, &old_mode);

	if (enable) {
		ar0832_set_lock(sd, 0);

		if (state->vt_mode) {
			printk("vt mode\n");
			err = ar0832_writeb(sd, AR0832_CATEGORY_AE,
				AR0832_AE_EP_MODE_MON, 0x11);
			CHECK_ERR(err);
		}

		old_mode = ar0832_set_mode(sd, AR0832_MONITOR_MODE);
		if (old_mode <= 0) {
			cam_err("failed to set mode\n");
			return old_mode;
		}

		if (old_mode != AR0832_MONITOR_MODE) {
			int_factor = ar0832_wait_interrupt(sd, AR0832_ISP_TIMEOUT);
			if (!(int_factor & AR0832_INT_MODE)) {
				cam_err("AR0832_INT_MODE isn't issued, %#x\n",
					int_factor);
				return -ETIMEDOUT;
			}
		}

		if (state->check_dataline) {
			err = ar0832_check_dataline(sd, state->check_dataline);
			CHECK_ERR(err);
		}
	} else {
		err = ar0832_readb(sd, AR0832_CATEGORY_SYS, AR0832_SYS_MODE, &old_mode);
	}

	return 0;
}

static int ar0832_s_stream_capture(struct v4l2_subdev *sd, int enable)
{
	u32 int_factor;
	int err;

	if (enable) {
		err = ar0832_set_mode(sd, AR0832_STILLCAP_MODE);
		if (err <= 0) {
			cam_err("failed to set mode\n");
			return err;
		}

		int_factor = ar0832_wait_interrupt(sd, AR0832_ISP_TIMEOUT);
		if (!(int_factor & AR0832_INT_SOUND)) {
			cam_err("AR0832_INT_SOUND isn't issued, %#x\n",
				int_factor);
			return -ETIMEDOUT;
		}
	} else {
	}
	return 0;
}

static int ar0832_s_stream_hdr(struct v4l2_subdev *sd, int enable)
{
	int err;

	err = ar0832_set_mode(sd, AR0832_PARMSET_MODE);
	CHECK_ERR(err);

	if (enable) {
		err = ar0832_writeb(sd, AR0832_CATEGORY_TEST, 0x50, 0x02);
		CHECK_ERR(err);

		err = ar0832_writeb(sd, AR0832_CATEGORY_TEST, 0x51, 0x80);
		CHECK_ERR(err);

		err = ar0832_writeb(sd, AR0832_CATEGORY_PARM,
			AR0832_PARM_HDR_MON, 0x01);
		CHECK_ERR(err);

		err = ar0832_writeb(sd, AR0832_CATEGORY_PARM,
			AR0832_PARM_HDR_MON_OFFSET_EV, 0x64);
		CHECK_ERR(err);
	} else {
		err = ar0832_writeb(sd, AR0832_CATEGORY_PARM,
			AR0832_PARM_HDR_MON, 0x00);
		CHECK_ERR(err);
	}
	return 0;
}

static int ar0832_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ar0832_state *state = to_state(sd);
	int err;

	cam_trace("E\n");

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	switch (enable) {
	case STREAM_MODE_CAM_ON:
	case STREAM_MODE_CAM_OFF:
		switch (state->format_mode) {
		case V4L2_PIX_FMT_MODE_CAPTURE:
			cam_info("capture %s",
				enable == STREAM_MODE_CAM_ON ? "on" : "off");
			err = ar0832_s_stream_capture(sd, enable == STREAM_MODE_CAM_ON);
			break;
		case V4L2_PIX_FMT_MODE_HDR:
			err = ar0832_s_stream_hdr(sd, enable == STREAM_MODE_CAM_ON);
			break;
		default:
			cam_info("preview %s",
				enable == STREAM_MODE_CAM_ON ? "on" : "off");
			//err = ar0832_s_stream_preview(sd, enable == STREAM_MODE_CAM_ON);

			if(enable){
				//preview start
				mdelay(500);
				
				err=ar0832_write_array3(sd,ar0832_reg_start_streaming1_3,AR0832_REG_START_STREAMING1_3);
					if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
				err=ar0832_write_array(sd,ar0832_reg_start_streaming2,AR0832_REG_START_STREAMING2);
					if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
				err=ar0832_write_array3(sd,ar0832_reg_start_streaming3_3,AR0832_REG_START_STREAMING3_3);
					if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
				
				//test		
				err=ar0832_write_array(sd,ar0832_reg_test,AR0832_INIT_REG_TEST);
						if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
				}
			else {
				//preview stop
				err=ar0832_write_array3(sd,ar0832_reg_stop_streaming1_3,AR0832_REG_STOP_STREAMING1_3);
					if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
				err=ar0832_write_array(sd,ar0832_reg_stop_streaming2,AR0832_REG_STOP_STREAMING2);
					if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
				}
			cam_info("\n");
			break;
		}
		break;

	case STREAM_MODE_MOVIE_ON:
		if (state->flash_mode != FLASH_MODE_OFF)
			err = ar0832_set_flash(sd, state->flash_mode, 1);

		if (state->preview->index == AR0832_PREVIEW_720P ||
				state->preview->index == AR0832_PREVIEW_1080P)
			err = ar0832_set_af(sd, 1);
		break;

	case STREAM_MODE_MOVIE_OFF:
		if (state->preview->index == AR0832_PREVIEW_720P ||
				state->preview->index == AR0832_PREVIEW_1080P)
			err = ar0832_set_af(sd, 0);

		ar0832_set_flash(sd, FLASH_MODE_OFF, 1);
		break;

	default:
		cam_err("invalid stream option, %d\n", enable);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int ar0832_check_version(struct v4l2_subdev *sd)
{
	struct ar0832_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i, val,err;

	cam_info("*************************************\n");
#if 0
	for (i = 0; i < 0xff; i++) {
		client->addr=i;
			cam_info("client->addr %x \n",client->addr);
			err=ar0832_i2c_write_2short(sd,0x1406,0x00);
			if(err<0) cam_info("error \n");	
		}

	err=ar0832_i2c_write_2short(sd,0x1406,0x00);
		if(err<0) cam_info("error \n"); 
		else  cam_info("address check passed \n"); 
#endif

	val=ar0832_i2c_read_reg(client,AR0832_CATEGORY_MODEL_ID,2);
	cam_info("model_id : %d\n",val);
	val=ar0832_i2c_read_reg(client,AR0832_CATEGORY_REVISION_NUM,1);
	cam_info("revision_number :%d\n",val);
	val=ar0832_i2c_read_reg(client,AR0832_CATEGORY_MANUFACTURE_ID,1);
	cam_info("manufacturer_id : %d\n",val);
	val=ar0832_i2c_read_reg(client,AR0832_CATEGORY_SMIA_VERSION,1);
	cam_info("smia_version : %d\n",val);

//	state->exif.unique_id[i] = (char)val;
//	state->exif.unique_id[i] = '\0';

	cam_info("*************************************\n");

	return val;
}
  int ar0832_init_param_extern()
{
	printk("%s ... \n",__func__);
	struct v4l2_subdev *sd;
	sd= &ar0832_is_data->sd;
	int err;
	printk("E\n");

	err=ar0832_write_array3(sd,ar0832_init_reg_MIPI_IF_CFG1_3,AR0832_INIT_REG_MIPI_IF_CFG1_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_init_reg_MIPI_IF_CFG2,AR0832_INIT_REG_MIPI_IF_CFG2);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array3(sd,ar0832_reg_stop_streaming1_3,AR0832_REG_STOP_STREAMING1_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_reg_stop_streaming2,AR0832_REG_STOP_STREAMING2);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_init_reg,AR0832_INIT_REG_REG);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_init_reg_raw10,AR0832_INIT_REG_RAW10);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_init_reg_pll,AR0832_INIT_REG_PLL);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
	
	err=ar0832_write_array(sd,ar0832_init_reg_output_size,AR0832_INIT_REG_OUTPUT_SIZE);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array3(sd,ar0832_init_reg_etc1_3,AR0832_INIT_REG_ETC1_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }		

	err=ar0832_write_array(sd,ar0832_init_reg_etc2,AR0832_INIT_REG_ETC2);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array3(sd,ar0832_init_reg_etc3_3,AR0832_INIT_REG_ETC3_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }		
		
	err=ar0832_write_array(sd,ar0832_init_reg_etc4,AR0832_INIT_REG_ETC4);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
#if 0
	//start
	err=ar0832_write_array3(sd,ar0832_reg_start_streaming1_3,AR0832_REG_START_STREAMING1_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
	err=ar0832_write_array(sd,ar0832_reg_start_streaming2,AR0832_REG_START_STREAMING2);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
	err=ar0832_write_array3(sd,ar0832_reg_start_streaming3_3,AR0832_REG_START_STREAMING3_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	//test		
	err=ar0832_write_array(sd,ar0832_reg_test,AR0832_INIT_REG_TEST);
			if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
#endif
	printk("X\n");
	return 0;
}
  int ar0832_stream_param_extern()
  {
	  printk("%s ... \n",__func__);
	  struct v4l2_subdev *sd;
	  sd= &ar0832_is_data->sd;
	  int err;
	  printk("E\n");
  
	 
#if 1
	  //start
	  err=ar0832_write_array3(sd,ar0832_reg_start_streaming1_3,AR0832_REG_START_STREAMING1_3);
		  if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
	  err=ar0832_write_array(sd,ar0832_reg_start_streaming2,AR0832_REG_START_STREAMING2);
		  if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
	  err=ar0832_write_array3(sd,ar0832_reg_start_streaming3_3,AR0832_REG_START_STREAMING3_3);
		  if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
  mdelay(100);
	  //test	  
	 // err=ar0832_write_array(sd,ar0832_reg_test,AR0832_INIT_REG_TEST);
		//	  if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
#endif
	  printk("X\n");
	  return 0;
  }

 int ar0832_init_param(struct v4l2_subdev *sd)
{
	int err;
	cam_trace("E\n");
	
	err=ar0832_write_array3(sd,ar0832_init_reg_MIPI_IF_CFG1_3,AR0832_INIT_REG_MIPI_IF_CFG1_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_init_reg_MIPI_IF_CFG2,AR0832_INIT_REG_MIPI_IF_CFG2);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array3(sd,ar0832_reg_stop_streaming1_3,AR0832_REG_STOP_STREAMING1_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_reg_stop_streaming2,AR0832_REG_STOP_STREAMING2);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_init_reg,AR0832_INIT_REG_REG);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_init_reg_raw10,AR0832_INIT_REG_RAW10);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array(sd,ar0832_init_reg_pll,AR0832_INIT_REG_PLL);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
	
	err=ar0832_write_array(sd,ar0832_init_reg_output_size,AR0832_INIT_REG_OUTPUT_SIZE);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array3(sd,ar0832_init_reg_etc1_3,AR0832_INIT_REG_ETC1_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }		

	err=ar0832_write_array(sd,ar0832_init_reg_etc2,AR0832_INIT_REG_ETC2);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	err=ar0832_write_array3(sd,ar0832_init_reg_etc3_3,AR0832_INIT_REG_ETC3_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }		
		
	err=ar0832_write_array(sd,ar0832_init_reg_etc4,AR0832_INIT_REG_ETC4);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
#if 1
	//start
	err=ar0832_write_array3(sd,ar0832_reg_start_streaming1_3,AR0832_REG_START_STREAMING1_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
	err=ar0832_write_array(sd,ar0832_reg_start_streaming2,AR0832_REG_START_STREAMING2);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
	err=ar0832_write_array3(sd,ar0832_reg_start_streaming3_3,AR0832_REG_START_STREAMING3_3);
		if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }

	//test		
	err=ar0832_write_array(sd,ar0832_reg_test,AR0832_INIT_REG_TEST);
			if(err <0) {cam_err("%s init error %d \n",__func__,err); return -1; }
#endif
	cam_trace("X\n");
	return 0;
}

static int ar0832_init(struct v4l2_subdev *sd, u32 val)
{
	struct ar0832_state *state = to_state(sd);
	u32 int_factor;
	int err;
	cam_trace("%s \n",__func__);
	printk("%s\n", __func__);

	/* Default state values */
	state->isp.bad_fw = 0;

	state->preview = NULL;
	state->capture = NULL;

	state->format_mode = V4L2_PIX_FMT_MODE_PREVIEW;
	state->sensor_mode = SENSOR_CAMERA;
	state->flash_mode = FLASH_MODE_OFF;
	state->beauty_mode = 0;

	state->fps = 0;			/* auto */

	memset(&state->focus, 0, sizeof(state->focus));
#if 0 //firmware no needed //rapheal
	/* start camera program(parallel FLASH ROM) */
	err = ar0832_writeb(sd, AR0832_CATEGORY_FLASH,
		AR0832_FLASH_CAM_START, 0x01);
	CHECK_ERR(err);

	int_factor = ar0832_wait_interrupt(sd, AR0832_ISP_TIMEOUT);
	if (!(int_factor & AR0832_INT_MODE)) {
		cam_err("firmware was erased?\n");
		state->isp.bad_fw = 1;
		return -ENOSYS;
	}
#endif

	/* check up F/W version */
	err = ar0832_check_version(sd);
	CHECK_ERR(err);

	ar0832_init_param(sd);

	return 0;
}

static const struct v4l2_subdev_core_ops ar0832_core_ops = {
	.init = ar0832_init,		/* initializing API */
	//.load_fw = ar0832_load_fw,
	.queryctrl = ar0832_queryctrl,
	.g_ctrl = ar0832_g_ctrl,
	.s_ctrl = ar0832_s_ctrl,
	.g_ext_ctrls = ar0832_g_ext_ctrls,
};

static const struct v4l2_subdev_video_ops ar0832_video_ops = {
	.s_mbus_fmt = ar0832_s_fmt,
	.g_parm = ar0832_g_parm,
	.s_parm = ar0832_s_parm,
	.enum_framesizes = ar0832_enum_framesizes,
	.s_stream = ar0832_s_stream,
};

static const struct v4l2_subdev_ops ar0832_ops = {
	.core = &ar0832_core_ops,
	.video = &ar0832_video_ops,
};

static ssize_t ar0832_camera_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char type[] = "APTINA_AR0832_NONE";

	return sprintf(buf, "%s\n", type);
}

static ssize_t ar0832_camera_fw_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0832_state *state = to_state(sd);

	return sprintf(buf, "%s\n", state->fw_version);
}

static DEVICE_ATTR(camera_type, S_IRUGO, ar0832_camera_type_show, NULL);
static DEVICE_ATTR(camera_fw, S_IRUGO, ar0832_camera_fw_show, NULL);

/*
 * ar0832_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int __devinit ar0832_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct ar0832_state *state;
	struct v4l2_subdev *sd;

	const struct ar0832_platform_data *pdata =
		client->dev.platform_data;
	int err = 0;

	state = kzalloc(sizeof(struct ar0832_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	sd = &state->sd;
	strcpy(sd->name, AR0832_DRIVER_NAME);
	
	/* Registering subdev */
	v4l2_i2c_subdev_init(sd, client, &ar0832_ops);

	state->this_client = client;
	ar0832_is_data = state;

#ifdef CAM_DEBUG
	state->dbg_level = CAM_DEBUG| CAM_TRACE | CAM_I2C;
#endif

#if 0 //rapheal
	if (device_create_file(&client->dev, &dev_attr_camera_type) < 0) {
		cam_warn("failed to create device file, %s\n",
			dev_attr_camera_type.attr.name);
	}

	if (device_create_file(&client->dev, &dev_attr_camera_fw) < 0) {
		cam_warn("failed to create device file, %s\n",
			dev_attr_camera_fw.attr.name);
	}

	/* wait queue initialize */
	init_waitqueue_head(&state->isp.wait);

	if (pdata->config_isp_irq)
		pdata->config_isp_irq();

	err = request_irq(pdata->irq,
		ar0832_isp_isr, IRQF_TRIGGER_RISING, "ar0832 isp", sd);
	if (err) {
		cam_err("failed to request irq\n");
		return err;
	}
	state->isp.irq = pdata->irq;
	state->isp.issued = 0;
#endif
	printk("%s\n", __func__);

	return 0;
}

static int __devexit ar0832_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0832_state *state = to_state(sd);

	if (ar0832_set_af_softlanding(sd) < 0)
		cam_err("failed to set soft landing\n");

	device_remove_file(&client->dev, &dev_attr_camera_type);
	device_remove_file(&client->dev, &dev_attr_camera_fw);

	if (state->isp.irq > 0)
		free_irq(state->isp.irq, sd);

	v4l2_device_unregister_subdev(sd);

	kfree(state->fw_version);
	kfree(state);

	return 0;
}

static const struct i2c_device_id ar0832_id[] = {
	{ AR0832_DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ar0832_id);

static struct i2c_driver ar0832_i2c_driver = {
	.driver = {
		.name	= AR0832_DRIVER_NAME,
	},
	.probe		= ar0832_probe,
	.remove		= __devexit_p(ar0832_remove),
	.id_table	= ar0832_id,
};

static int __init ar0832_mod_init(void)
{
	printk("%s\n", __func__);

	return i2c_add_driver(&ar0832_i2c_driver);
}

static void __exit ar0832_mod_exit(void)
{
	i2c_del_driver(&ar0832_i2c_driver);
}
module_init(ar0832_mod_init);
module_exit(ar0832_mod_exit);


MODULE_AUTHOR("Dongyol Yun <ydongyol@hardkenel.com>");
MODULE_DESCRIPTION("driver for Aptina AR0832 8MP camera");
MODULE_LICENSE("GPL");
