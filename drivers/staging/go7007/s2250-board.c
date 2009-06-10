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
#include <media/v4l2-common.h>
#include "go7007-priv.h"
#include "wis-i2c.h"

extern int s2250loader_init(void);
extern void s2250loader_cleanup(void);

#define TLV320_ADDRESS      0x34
#define VPX322_ADDR_ANALOGCONTROL1	0x02
#define VPX322_ADDR_BRIGHTNESS0		0x0127
#define VPX322_ADDR_BRIGHTNESS1		0x0131
#define VPX322_ADDR_CONTRAST0		0x0128
#define VPX322_ADDR_CONTRAST1		0x0132
#define VPX322_ADDR_HUE			0x00dc
#define VPX322_ADDR_SAT	 	        0x0030

struct go7007_usb_board {
	unsigned int flags;
	struct go7007_board_info main_info;
};

struct go7007_usb {
	struct go7007_usb_board *board;
	struct semaphore i2c_lock;
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
	int std;
	int input;
	int brightness;
	int contrast;
	int saturation;
	int hue;
	int reg12b_val;
	int audio_input;
	struct i2c_client *audio;
};

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
	if (down_interruptible(&usb->i2c_lock) != 0) {
		printk(KERN_INFO "i2c lock failed\n");
		kfree(buf);
		return -EINTR;
	}
	rc = go7007_usb_vendor_request(go, 0x55, dev_addr,
				       (reg<<8 | value),
				       buf,
				       16, 1);

	up(&usb->i2c_lock);
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
	if (down_interruptible(&usb->i2c_lock) != 0) {
		printk(KERN_INFO "i2c lock failed\n");
		return -EINTR;
	}
	if (go7007_usb_vendor_request(go, 0x57, addr, val, buf, 16, 1) < 0)
		return -EFAULT;

	up(&usb->i2c_lock);
	if (buf[0] == 0) {
		unsigned int subaddr, val_read;

		subaddr = (buf[4] << 8) + buf[5];
		val_read = (buf[2] << 8) + buf[3];
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
	} else
		return -EFAULT;

	/* save last 12b value */
	if (addr == 0x12b)
		dec->reg12b_val = val;

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


static int s2250_command(struct i2c_client *client,
			 unsigned int cmd, void *arg)
{
	struct s2250 *dec = i2c_get_clientdata(client);

	switch (cmd) {
	case VIDIOC_S_INPUT:
	{
		int vidsys;
		int *input = arg;

		vidsys = (dec->std == V4L2_STD_NTSC) ? 0x01 : 0x00;
		if (*input == 0) {
			/* composite */
			write_reg_fp(client, 0x20, 0x020 | vidsys);
			write_reg_fp(client, 0x21, 0x662);
			write_reg_fp(client, 0x140, 0x060);
		} else {
			/* S-Video */
			write_reg_fp(client, 0x20, 0x040 | vidsys);
			write_reg_fp(client, 0x21, 0x666);
			write_reg_fp(client, 0x140, 0x060);
		}
		dec->input = *input;
		break;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *std = arg;
		u16 vidsource;

		vidsource = (dec->input == 1) ? 0x040 : 0x020;
		dec->std = *std;
		switch (dec->std) {
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
		break;
	}
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *ctrl = arg;
		static const u32 user_ctrls[] = {
			V4L2_CID_BRIGHTNESS,
			V4L2_CID_CONTRAST,
			V4L2_CID_SATURATION,
			V4L2_CID_HUE,
			0
		};
		static const u32 *ctrl_classes[] = {
			user_ctrls,
			NULL
		};

		ctrl->id = v4l2_ctrl_next(ctrl_classes, ctrl->id);
		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			v4l2_ctrl_query_fill(ctrl, 0, 100, 1, 50);
			break;
		case V4L2_CID_CONTRAST:
			v4l2_ctrl_query_fill(ctrl, 0, 100, 1, 50);
			break;
		case V4L2_CID_SATURATION:
			v4l2_ctrl_query_fill(ctrl, 0, 100, 1, 50);
			break;
		case V4L2_CID_HUE:
			v4l2_ctrl_query_fill(ctrl, -50, 50, 1, 0);
			break;
		default:
			ctrl->name[0] = '\0';
			return -EINVAL;
		}
		break;
	}
	case VIDIOC_S_CTRL:
	{
		struct v4l2_control *ctrl = arg;
		int value1;

		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			printk(KERN_INFO "s2250: future setting\n");
			return -EINVAL;
		case V4L2_CID_CONTRAST:
			printk(KERN_INFO "s2250: future setting\n");
			return -EINVAL;
			break;
		case V4L2_CID_SATURATION:
			if (ctrl->value > 127)
				dec->saturation = 127;
			else if (ctrl->value < 0)
				dec->saturation = 0;
			else
				dec->saturation = ctrl->value;

			value1 = dec->saturation * 4140 / 100;
			if (value1 > 4094)
				value1 = 4094;
			write_reg_fp(client, VPX322_ADDR_SAT, value1);
			break;
		case V4L2_CID_HUE:
			if (ctrl->value > 50)
				dec->hue = 50;
			else if (ctrl->value < -50)
				dec->hue = -50;
			else
				dec->hue = ctrl->value;
			/* clamp the hue range */
			value1 = dec->hue * 280 / 50;
			write_reg_fp(client, VPX322_ADDR_HUE, value1);
			break;
		}
		break;
	}
	case VIDIOC_G_CTRL:
	{
		struct v4l2_control *ctrl = arg;

		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			ctrl->value = dec->brightness;
			break;
		case V4L2_CID_CONTRAST:
			ctrl->value = dec->contrast;
			break;
		case V4L2_CID_SATURATION:
			ctrl->value = dec->saturation;
			break;
		case V4L2_CID_HUE:
			ctrl->value = dec->hue;
			break;
		}
		break;
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *fmt = arg;
		if (fmt->fmt.pix.height < 640) {
			write_reg_fp(client, 0x12b, dec->reg12b_val | 0x400);
			write_reg_fp(client, 0x140, 0x060);
		} else {
			write_reg_fp(client, 0x12b, dec->reg12b_val & ~0x400);
			write_reg_fp(client, 0x140, 0x060);
		}
		return 0;
	}
	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *audio = arg;

		memset(audio, 0, sizeof(*audio));
		audio->index = dec->audio_input;
		/* fall through */
	}
	case VIDIOC_ENUMAUDIO:
	{
		struct v4l2_audio *audio = arg;

		switch (audio->index) {
		case 0:
			strcpy(audio->name, "Line In");
			break;
		case 1:
			strcpy(audio->name, "Mic");
			break;
		case 2:
			strcpy(audio->name, "Mic Boost");
			break;
		default:
			audio->name[0] = '\0';
			return 0;
		}
		audio->capability = V4L2_AUDCAP_STEREO;
		audio->mode = 0;
		return 0;
	}
	case VIDIOC_S_AUDIO:
	{
		struct v4l2_audio *audio = arg;

		switch (audio->index) {
		case 0:
			write_reg(dec->audio, 0x08, 0x02); /* Line In */
			break;
		case 1:
			write_reg(dec->audio, 0x08, 0x04); /* Mic */
			break;
		case 2:
			write_reg(dec->audio, 0x08, 0x05); /* Mic Boost */
			break;
		default:
			return -EINVAL;
		}
		dec->audio_input = audio->index;
		return 0;
	}

	default:
		printk(KERN_INFO "s2250: unknown command 0x%x\n", cmd);
		break;
	}
	return 0;
}

static int s2250_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct i2c_client *audio;
	struct i2c_adapter *adapter = client->adapter;
	struct s2250 *dec;
	u8 *data;
	struct go7007 *go = i2c_get_adapdata(adapter);
	struct go7007_usb *usb = go->hpi_context;

	audio = i2c_new_dummy(adapter, TLV320_ADDRESS >> 1);
	if (audio == NULL)
		return -ENOMEM;

	dec = kmalloc(sizeof(struct s2250), GFP_KERNEL);
	if (dec == NULL) {
		i2c_unregister_device(audio);
		return -ENOMEM;
	}

	dec->std = V4L2_STD_NTSC;
	dec->brightness = 50;
	dec->contrast = 50;
	dec->saturation = 50;
	dec->hue = 0;
	dec->audio = audio;
	i2c_set_clientdata(client, dec);

	printk(KERN_DEBUG
	       "s2250: initializing video decoder on %s\n",
	       adapter->name);

	/* initialize the audio */
	if (write_regs(audio, aud_regs) < 0) {
		printk(KERN_ERR
		       "s2250: error initializing audio\n");
		i2c_unregister_device(audio);
		kfree(dec);
		return 0;
	}

	if (write_regs(client, vid_regs) < 0) {
		printk(KERN_ERR
		       "s2250: error initializing decoder\n");
		i2c_unregister_device(audio);
		kfree(dec);
		return 0;
	}
	if (write_regs_fp(client, vid_regs_fp) < 0) {
		printk(KERN_ERR
		       "s2250: error initializing decoder\n");
		i2c_unregister_device(audio);
		kfree(dec);
		return 0;
	}
	/* set default channel */
	/* composite */
	write_reg_fp(client, 0x20, 0x020 | 1);
	write_reg_fp(client, 0x21, 0x662);
	write_reg_fp(client, 0x140, 0x060);

	/* set default audio input */
	dec->audio_input = 0;
	write_reg(client, 0x08, 0x02); /* Line In */

	if (down_interruptible(&usb->i2c_lock) == 0) {
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
		up(&usb->i2c_lock);
	}

	printk("s2250: initialized successfully\n");
	return 0;
}

static int s2250_remove(struct i2c_client *client)
{
	struct s2250 *dec = i2c_get_clientdata(client);

	i2c_set_clientdata(client, NULL);
	i2c_unregister_device(dec->audio);
	kfree(dec);
	return 0;
}

static struct i2c_device_id s2250_id[] = {
	{ "s2250_board", 0 },
	{ }
};

static struct i2c_driver s2250_driver = {
	.driver = {
		.name	= "Sensoray 2250 board driver",
	},
	.probe		= s2250_probe,
	.remove		= s2250_remove,
	.command	= s2250_command,
	.id_table	= s2250_id,
};

static int __init s2250_init(void)
{
	int r;

	r = s2250loader_init();
	if (r < 0)
		return r;

	r = i2c_add_driver(&s2250_driver);
	if (r < 0)
		s2250loader_cleanup();

	return r;
}

static void __exit s2250_cleanup(void)
{
	i2c_del_driver(&s2250_driver);

	s2250loader_cleanup();
}

module_init(s2250_init);
module_exit(s2250_cleanup);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("Board driver for Sensoryray 2250");
MODULE_LICENSE("GPL v2");
