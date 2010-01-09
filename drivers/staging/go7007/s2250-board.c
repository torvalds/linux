/*
 * Copyright (C) 2008 Sensoray Company Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-i2c-drv.h>
#include <media/v4l2-subdev.h>
#include "go7007-priv.h"

MODULE_DESCRIPTION("Sensoray 2250/2251 i2c v4l2 subdev driver");
MODULE_LICENSE("GPL v2");

#define TLV320_ADDRESS      0x34
#define VPX322_ADDR_ANALOGCONTROL1	0x02
#define VPX322_ADDR_BRIGHTNESS0		0x0127
#define VPX322_ADDR_BRIGHTNESS1		0x0131
#define VPX322_ADDR_CONTRAST0		0x0128
#define VPX322_ADDR_CONTRAST1		0x0132
#define VPX322_ADDR_HUE			0x00dc
#define VPX322_ADDR_SAT			0x0030

struct go7007_usb_board {
	unsigned int flags;
	struct go7007_board_info main_info;
};

struct go7007_usb {
	struct go7007_usb_board *board;
	struct mutex i2c_lock;
	struct usb_device *usbdev;
	struct urb *video_urbs[8];
	struct urb *audio_urbs[8];
	struct urb *intr_urb;
};

static unsigned char aud_regs[] = {
	0x1e, 0x00,
	0x00, 0x17,
	0x02, 0x17,
	0x04, 0xf9,
	0x06, 0xf9,
	0x08, 0x02,
	0x0a, 0x00,
	0x0c, 0x00,
	0x0a, 0x00,
	0x0c, 0x00,
	0x0e, 0x02,
	0x10, 0x00,
	0x12, 0x01,
	0x00, 0x00,
};


static unsigned char vid_regs[] = {
	0xF2, 0x0f,
	0xAA, 0x00,
	0xF8, 0xff,
	0x00, 0x00,
};

static u16 vid_regs_fp[] = {
	0x028, 0x067,
	0x120, 0x016,
	0x121, 0xcF2,
	0x122, 0x0F2,
	0x123, 0x00c,
	0x124, 0x2d0,
	0x125, 0x2e0,
	0x126, 0x004,
	0x128, 0x1E0,
	0x12A, 0x016,
	0x12B, 0x0F2,
	0x12C, 0x0F2,
	0x12D, 0x00c,
	0x12E, 0x2d0,
	0x12F, 0x2e0,
	0x130, 0x004,
	0x132, 0x1E0,
	0x140, 0x060,
	0x153, 0x00C,
	0x154, 0x200,
	0x150, 0x801,
	0x000, 0x000
};

/* PAL specific values */
static u16 vid_regs_fp_pal[] =
{
	0x120, 0x017,
	0x121, 0xd22,
	0x122, 0x122,
	0x12A, 0x017,
	0x12B, 0x122,
	0x12C, 0x122,
	0x140, 0x060,
	0x000, 0x000,
};

struct s2250 {
	struct v4l2_subdev sd;
	v4l2_std_id std;
	int input;
	int brightness;
	int contrast;
	int saturation;
	int hue;
	int reg12b_val;
	int audio_input;
	struct i2c_client *audio;
};

static inline struct s2250 *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s2250, sd);
}

/* from go7007-usb.c which is Copyright (C) 2005-2006 Micronas USA Inc.*/
static int go7007_usb_vendor_request(struct go7007 *go, u16 request,
	u16 value, u16 index, void *transfer_buffer, int length, int in)
{
	struct go7007_usb *usb = go->hpi_context;
	int timeout = 5000;

	if (in) {
		return usb_control_msg(usb->usbdev,
				usb_rcvctrlpipe(usb->usbdev, 0), request,
				USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
				value, index, transfer_buffer, length, timeout);
	} else {
		return usb_control_msg(usb->usbdev,
				usb_sndctrlpipe(usb->usbdev, 0), request,
				USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				value, index, transfer_buffer, length, timeout);
	}
}
/* end from go7007-usb.c which is Copyright (C) 2005-2006 Micronas USA Inc.*/

static int write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	struct go7007 *go = i2c_get_adapdata(client->adapter);
	struct go7007_usb *usb;
	int rc;
	int dev_addr = client->addr;
	u8 *buf;

	if (go == NULL)
		return -ENODEV;

	if (go->status == STATUS_SHUTDOWN)
		return -EBUSY;

	buf = kzalloc(16, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	usb = go->hpi_context;
	if (mutex_lock_interruptible(&usb->i2c_lock) != 0) {
		printk(KERN_INFO "i2c lock failed\n");
		kfree(buf);
		return -EINTR;
	}
	rc = go7007_usb_vendor_request(go, 0x55, dev_addr,
				       (reg<<8 | value),
				       buf,
				       16, 1);

	mutex_unlock(&usb->i2c_lock);
	kfree(buf);
	return rc;
}

static int write_reg_fp(struct i2c_client *client, u16 addr, u16 val)
{
	struct go7007 *go = i2c_get_adapdata(client->adapter);
	struct go7007_usb *usb;
	u8 *buf;
	struct s2250 *dec = i2c_get_clientdata(client);

	if (go == NULL)
		return -ENODEV;

	if (go->status == STATUS_SHUTDOWN)
		return -EBUSY;

	buf = kzalloc(16, GFP_KERNEL);

	if (buf == NULL)
		return -ENOMEM;



	memset(buf, 0xcd, 6);

	usb = go->hpi_context;
	if (mutex_lock_interruptible(&usb->i2c_lock) != 0) {
		printk(KERN_INFO "i2c lock failed\n");
		kfree(buf);
		return -EINTR;
	}
	if (go7007_usb_vendor_request(go, 0x57, addr, val, buf, 16, 1) < 0) {
		kfree(buf);
		return -EFAULT;
	}

	mutex_unlock(&usb->i2c_lock);
	if (buf[0] == 0) {
		unsigned int subaddr, val_read;

		subaddr = (buf[4] << 8) + buf[5];
		val_read = (buf[2] << 8) + buf[3];
		kfree(buf);
		if (val_read != val) {
			printk(KERN_INFO "invalid fp write %x %x\n",
			       val_read, val);
			return -EFAULT;
		}
		if (subaddr != addr) {
			printk(KERN_INFO "invalid fp write addr %x %x\n",
			       subaddr, addr);
			return -EFAULT;
		}
	} else {
		kfree(buf);
		return -EFAULT;
	}

	/* save last 12b value */
	if (addr == 0x12b)
		dec->reg12b_val = val;

	return 0;
}

static int read_reg_fp(struct i2c_client *client, u16 addr, u16 *val)
{
	struct go7007 *go = i2c_get_adapdata(client->adapter);
	struct go7007_usb *usb;
	u8 *buf;

	if (go == NULL)
		return -ENODEV;

	if (go->status == STATUS_SHUTDOWN)
		return -EBUSY;

	buf = kzalloc(16, GFP_KERNEL);

	if (buf == NULL)
		return -ENOMEM;



	memset(buf, 0xcd, 6);
	usb = go->hpi_context;
	if (mutex_lock_interruptible(&usb->i2c_lock) != 0) {
		printk(KERN_INFO "i2c lock failed\n");
		kfree(buf);
		return -EINTR;
	}
	if (go7007_usb_vendor_request(go, 0x58, addr, 0, buf, 16, 1) < 0) {
		kfree(buf);
		return -EFAULT;
	}
	mutex_unlock(&usb->i2c_lock);

	*val = (buf[0] << 8) | buf[1];
	kfree(buf);

	return 0;
}


static int write_regs(struct i2c_client *client, u8 *regs)
{
	int i;

	for (i = 0; !((regs[i] == 0x00) && (regs[i+1] == 0x00)); i += 2) {
		if (write_reg(client, regs[i], regs[i+1]) < 0) {
			printk(KERN_INFO "s2250: failed\n");
			return -1;
		}
	}
	return 0;
}

static int write_regs_fp(struct i2c_client *client, u16 *regs)
{
	int i;

	for (i = 0; !((regs[i] == 0x00) && (regs[i+1] == 0x00)); i += 2) {
		if (write_reg_fp(client, regs[i], regs[i+1]) < 0) {
			printk(KERN_INFO "s2250: failed fp\n");
			return -1;
		}
	}
	return 0;
}


/* ------------------------------------------------------------------------- */

static int s2250_s_video_routing(struct v4l2_subdev *sd, u32 input, u32 output,
				 u32 config)
{
	struct s2250 *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int vidsys;

	vidsys = (state->std == V4L2_STD_NTSC) ? 0x01 : 0x00;
	if (input == 0) {
		/* composite */
		write_reg_fp(client, 0x20, 0x020 | vidsys);
		write_reg_fp(client, 0x21, 0x662);
		write_reg_fp(client, 0x140, 0x060);
	} else if (input == 1) {
		/* S-Video */
		write_reg_fp(client, 0x20, 0x040 | vidsys);
		write_reg_fp(client, 0x21, 0x666);
		write_reg_fp(client, 0x140, 0x060);
	} else {
		return -EINVAL;
	}
	state->input = input;
	return 0;
}

static int s2250_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct s2250 *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 vidsource;

	vidsource = (state->input == 1) ? 0x040 : 0x020;
	switch (norm) {
	case V4L2_STD_NTSC:
		write_regs_fp(client, vid_regs_fp);
		write_reg_fp(client, 0x20, vidsource | 1);
		break;
	case V4L2_STD_PAL:
		write_regs_fp(client, vid_regs_fp);
		write_regs_fp(client, vid_regs_fp_pal);
		write_reg_fp(client, 0x20, vidsource);
		break;
	default:
		return -EINVAL;
	}
	state->std = norm;
	return 0;
}

static int s2250_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *query)
{
	switch (query->id) {
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(query, 0, 100, 1, 50);
	case V4L2_CID_CONTRAST:
		return v4l2_ctrl_query_fill(query, 0, 100, 1, 50);
	case V4L2_CID_SATURATION:
		return v4l2_ctrl_query_fill(query, 0, 100, 1, 50);
	case V4L2_CID_HUE:
		return v4l2_ctrl_query_fill(query, -50, 50, 1, 0);
	default:
		return -EINVAL;
	}
	return 0;
}

static int s2250_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct s2250 *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int value1;
	u16 oldvalue;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (ctrl->value > 100)
			state->brightness = 100;
		else if (ctrl->value < 0)
			state->brightness = 0;
		else
			state->brightness = ctrl->value;
		value1 = (state->brightness - 50) * 255 / 100;
		read_reg_fp(client, VPX322_ADDR_BRIGHTNESS0, &oldvalue);
		write_reg_fp(client, VPX322_ADDR_BRIGHTNESS0,
			     value1 | (oldvalue & ~0xff));
		read_reg_fp(client, VPX322_ADDR_BRIGHTNESS1, &oldvalue);
		write_reg_fp(client, VPX322_ADDR_BRIGHTNESS1,
			     value1 | (oldvalue & ~0xff));
		write_reg_fp(client, 0x140, 0x60);
		break;
	case V4L2_CID_CONTRAST:
		if (ctrl->value > 100)
			state->contrast = 100;
		else if (ctrl->value < 0)
			state->contrast = 0;
		else
			state->contrast = ctrl->value;
		value1 = state->contrast * 0x40 / 100;
		if (value1 > 0x3f)
			value1 = 0x3f; /* max */
		read_reg_fp(client, VPX322_ADDR_CONTRAST0, &oldvalue);
		write_reg_fp(client, VPX322_ADDR_CONTRAST0,
			     value1 | (oldvalue & ~0x3f));
		read_reg_fp(client, VPX322_ADDR_CONTRAST1, &oldvalue);
		write_reg_fp(client, VPX322_ADDR_CONTRAST1,
			     value1 | (oldvalue & ~0x3f));
		write_reg_fp(client, 0x140, 0x60);
		break;
	case V4L2_CID_SATURATION:
		if (ctrl->value > 100)
			state->saturation = 100;
		else if (ctrl->value < 0)
			state->saturation = 0;
		else
			state->saturation = ctrl->value;
		value1 = state->saturation * 4140 / 100;
		if (value1 > 4094)
			value1 = 4094;
		write_reg_fp(client, VPX322_ADDR_SAT, value1);
		break;
	case V4L2_CID_HUE:
		if (ctrl->value > 50)
			state->hue = 50;
		else if (ctrl->value < -50)
			state->hue = -50;
		else
			state->hue = ctrl->value;
		/* clamp the hue range */
		value1 = state->hue * 280 / 50;
		write_reg_fp(client, VPX322_ADDR_HUE, value1);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int s2250_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct s2250 *state = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = state->brightness;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = state->contrast;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = state->saturation;
		break;
	case V4L2_CID_HUE:
		ctrl->value = state->hue;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int s2250_s_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
{
	struct s2250 *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (fmt->fmt.pix.height < 640) {
		write_reg_fp(client, 0x12b, state->reg12b_val | 0x400);
		write_reg_fp(client, 0x140, 0x060);
	} else {
		write_reg_fp(client, 0x12b, state->reg12b_val & ~0x400);
		write_reg_fp(client, 0x140, 0x060);
	}
	return 0;
}

static int s2250_s_audio_routing(struct v4l2_subdev *sd, u32 input, u32 output,
				 u32 config)
{
	struct s2250 *state = to_state(sd);

	switch (input) {
	case 0:
		write_reg(state->audio, 0x08, 0x02); /* Line In */
		break;
	case 1:
		write_reg(state->audio, 0x08, 0x04); /* Mic */
		break;
	case 2:
		write_reg(state->audio, 0x08, 0x05); /* Mic Boost */
		break;
	default:
		return -EINVAL;
	}
	state->audio_input = input;
	return 0;
}


static int s2250_log_status(struct v4l2_subdev *sd)
{
	struct s2250 *state = to_state(sd);

	v4l2_info(sd, "Standard: %s\n", state->std == V4L2_STD_NTSC ? "NTSC" :
					state->std == V4L2_STD_PAL ? "PAL" :
					state->std == V4L2_STD_SECAM ? "SECAM" :
					"unknown");
	v4l2_info(sd, "Input: %s\n", state->input == 0 ? "Composite" :
					state->input == 1 ? "S-video" :
					"error");
	v4l2_info(sd, "Brightness: %d\n", state->brightness);
	v4l2_info(sd, "Contrast: %d\n", state->contrast);
	v4l2_info(sd, "Saturation: %d\n", state->saturation);
	v4l2_info(sd, "Hue: %d\n", state->hue);	return 0;
	v4l2_info(sd, "Audio input: %s\n", state->audio_input == 0 ? "Line In" :
					state->audio_input == 1 ? "Mic" :
					state->audio_input == 2 ? "Mic Boost" :
					"error");
	return 0;
}

/* --------------------------------------------------------------------------*/

static const struct v4l2_subdev_core_ops s2250_core_ops = {
	.log_status = s2250_log_status,
	.g_ctrl = s2250_g_ctrl,
	.s_ctrl = s2250_s_ctrl,
	.queryctrl = s2250_queryctrl,
	.s_std = s2250_s_std,
};

static const struct v4l2_subdev_audio_ops s2250_audio_ops = {
	.s_routing = s2250_s_audio_routing,
};

static const struct v4l2_subdev_video_ops s2250_video_ops = {
	.s_routing = s2250_s_video_routing,
	.s_fmt = s2250_s_fmt,
};

static const struct v4l2_subdev_ops s2250_ops = {
	.core = &s2250_core_ops,
	.audio = &s2250_audio_ops,
	.video = &s2250_video_ops,
};

/* --------------------------------------------------------------------------*/

static int s2250_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct i2c_client *audio;
	struct i2c_adapter *adapter = client->adapter;
	struct s2250 *state;
	struct v4l2_subdev *sd;
	u8 *data;
	struct go7007 *go = i2c_get_adapdata(adapter);
	struct go7007_usb *usb = go->hpi_context;

	audio = i2c_new_dummy(adapter, TLV320_ADDRESS >> 1);
	if (audio == NULL)
		return -ENOMEM;

	state = kmalloc(sizeof(struct s2250), GFP_KERNEL);
	if (state == NULL) {
		i2c_unregister_device(audio);
		return -ENOMEM;
	}

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &s2250_ops);

	v4l2_info(sd, "initializing %s at address 0x%x on %s\n",
	       "Sensoray 2250/2251", client->addr, client->adapter->name);

	state->std = V4L2_STD_NTSC;
	state->brightness = 50;
	state->contrast = 50;
	state->saturation = 50;
	state->hue = 0;
	state->audio = audio;

	/* initialize the audio */
	if (write_regs(audio, aud_regs) < 0) {
		printk(KERN_ERR
		       "s2250: error initializing audio\n");
		i2c_unregister_device(audio);
		kfree(state);
		return 0;
	}

	if (write_regs(client, vid_regs) < 0) {
		printk(KERN_ERR
		       "s2250: error initializing decoder\n");
		i2c_unregister_device(audio);
		kfree(state);
		return 0;
	}
	if (write_regs_fp(client, vid_regs_fp) < 0) {
		printk(KERN_ERR
		       "s2250: error initializing decoder\n");
		i2c_unregister_device(audio);
		kfree(state);
		return 0;
	}
	/* set default channel */
	/* composite */
	write_reg_fp(client, 0x20, 0x020 | 1);
	write_reg_fp(client, 0x21, 0x662);
	write_reg_fp(client, 0x140, 0x060);

	/* set default audio input */
	state->audio_input = 0;
	write_reg(client, 0x08, 0x02); /* Line In */

	if (mutex_lock_interruptible(&usb->i2c_lock) == 0) {
		data = kzalloc(16, GFP_KERNEL);
		if (data != NULL) {
			int rc;
			rc = go7007_usb_vendor_request(go, 0x41, 0, 0,
						       data, 16, 1);
			if (rc > 0) {
				u8 mask;
				data[0] = 0;
				mask = 1<<5;
				data[0] &= ~mask;
				data[1] |= mask;
				go7007_usb_vendor_request(go, 0x40, 0,
							  (data[1]<<8)
							  + data[1],
							  data, 16, 0);
			}
			kfree(data);
		}
		mutex_unlock(&usb->i2c_lock);
	}

	v4l2_info(sd, "initialized successfully\n");
	return 0;
}

static int s2250_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id s2250_id[] = {
	{ "s2250", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s2250_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "s2250",
	.probe = s2250_probe,
	.remove = s2250_remove,
	.id_table = s2250_id,
};
