// SPDX-License-Identifier: GPL-2.0
/*
 * nvp6188 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 version.
 *  1. add get virtual channel fmt ioctl
 *  2. add get virtual channel hotplug status ioctl
 *  3. add virtual channel hotplug status event report to vicap
 *  4. fixup variables are reused when multiple devices use the same driver
 * V0.0X01.0X02 add quick stream support
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include <linux/platform_device.h>
#include <linux/input.h>

#define DRIVER_VERSION				KERNEL_VERSION(0, 0x01, 0x2)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN			V4L2_CID_GAIN
#endif

#define NVP6188_XVCLK_FREQ			27000000
#define NVP6188_LINK_FREQ_1458M		(1458000000UL >> 1)
#define NVP6188_LINK_FREQ_756M		(756000000UL >> 1)

#define NVP6188_LANES			4
#define NVP6188_BITS_PER_SAMPLE		8

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define OF_CAMERA_PINCTRL_STATE_DEFAULT		"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP		"rockchip,camera_sleep"

#define NVP6188_NAME				"nvp6188"

#define _MIPI_PORT0_
//#define _MIPI_PORT1_

#define POWER_ALWAY_ON 1

#ifdef _MIPI_PORT0_
#define _MAR_BANK_ 0x20
#define _MTX_BANK_ 0x23
#else
#define _MAR_BANK_ 0x30
#define _MTX_BANK_ 0x33
#endif

#define NVP_RESO_960H_NSTC_VALUE	0x00
#define NVP_RESO_960H_PAL_VALUE	0x10
#define NVP_RESO_720P_NSTC_VALUE	0x20
#define NVP_RESO_720P_PAL_VALUE	0x21
#define NVP_RESO_1080P_NSTC_VALUE	0x30
#define NVP_RESO_1080P_PAL_VALUE	0x31
#define NVP_RESO_960P_NSTC_VALUE	0xa0
#define NVP_RESO_960P_PAL_VALUE	0xa1

enum nvp6188_support_reso {
	NVP_RESO_UNKOWN = 0,
	NVP_RESO_960H_PAL,
	NVP_RESO_720P_PAL,
	NVP_RESO_960P_PAL,
	NVP_RESO_1080P_PAL,
	NVP_RESO_960H_NSTC,
	NVP_RESO_720P_NSTC,
	NVP_RESO_960P_NSTC,
	NVP_RESO_1080P_NSTC,
};

/* Audio output port formats */
enum nvp6188_audfmts {
	AUDFMT_DISABLED = 0,
	AUDFMT_I2S,
	AUDFMT_DSP,
	AUDFMT_SSP,
};

struct regval {
	u8 addr;
	u8 val;
};

struct nvp6188_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 mipi_freq_idx;
	u32 bpp;
	const struct regval *global_reg_list;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
	u32 channel_reso[PAD_MAX];
};

struct nvp6188_audio {
	enum nvp6188_audfmts audfmt;
	int mclk_fs;
};

struct nvp6188 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*vi_gpio;

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	bool			power_on;
	struct nvp6188_mode cur_mode;

	u32			module_index;
	u32			cfg_num;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;

	struct nvp6188_audio *audio_in;
	struct nvp6188_audio *audio_out;

	int streaming;

	struct task_struct *detect_thread;
	struct input_dev* input_dev;
	unsigned char detect_status;
	unsigned char last_detect_status;
	u8 is_reset;
};

#define to_nvp6188(sd) container_of(sd, struct nvp6188, subdev)

static int nvp6188_audio_init(struct nvp6188 *nvp6188);

// detect_status: bit 0~3 means channels plugin status : 1 no exist 0: exist
static ssize_t show_hotplug_status(struct device *dev,
				   struct device_attribute *attr,
				   char *buf) //cat命令时,将会调用该函数
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	return sprintf(buf, "%d\n", nvp6188->detect_status);
}

static DEVICE_ATTR(hotplug_status, S_IRUSR, show_hotplug_status, NULL);
static struct attribute *dev_attrs[] = {
	&dev_attr_hotplug_status.attr,
	NULL,
};
static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

static __maybe_unused const struct regval common_setting_756M_regs[] = {
	{ 0xff, 0x00 },
	{ 0x80, 0x0f },
	{ 0x00, 0x10 },
	{ 0x01, 0x10 },
	{ 0x02, 0x10 },
	{ 0x03, 0x10 },
	{ 0x22, 0x0b },
	{ 0x23, 0x41 },
	{ 0x26, 0x0b },
	{ 0x27, 0x41 },
	{ 0x2a, 0x0b },
	{ 0x2b, 0x41 },
	{ 0x2e, 0x0b },
	{ 0x2f, 0x41 },

	{ 0xff, 0x01 },
	{ 0x98, 0x30 },
	{ 0xed, 0x00 },

	{ 0xff, 0x05+0 },
	{ 0x00, 0xd0 },
	{ 0x01, 0x22 },
	{ 0x47, 0xee },
	{ 0x50, 0xc6 },
	{ 0x57, 0x00 },
	{ 0x58, 0x77 },
	{ 0x5b, 0x41 },
	{ 0x5c, 0x78 },
	{ 0xB8, 0xB8 },

	{ 0xff, 0x05+1 },
	{ 0x00, 0xd0 },
	{ 0x01, 0x22 },
	{ 0x47, 0xee },
	{ 0x50, 0xc6 },
	{ 0x57, 0x00 },
	{ 0x58, 0x77 },
	{ 0x5b, 0x41 },
	{ 0x5c, 0x78 },
	{ 0xB8, 0xB8 },

	{ 0xff, 0x05+2 },
	{ 0x00, 0xd0 },
	{ 0x01, 0x22 },
	{ 0x47, 0xee },
	{ 0x50, 0xc6 },
	{ 0x57, 0x00 },
	{ 0x58, 0x77 },
	{ 0x5b, 0x41 },
	{ 0x5c, 0x78 },
	{ 0xB8, 0xB8 },

	{ 0xff, 0x05+3 },
	{ 0x00, 0xd0 },
	{ 0x01, 0x22 },
	{ 0x47, 0xee },
	{ 0x50, 0xc6 },
	{ 0x57, 0x00 },
	{ 0x58, 0x77 },
	{ 0x5b, 0x41 },
	{ 0x5c, 0x78 },
	{ 0xB8, 0xB8 },

	{ 0xff, 0x09 },
	{ 0x50, 0x30 },
	{ 0x51, 0x6f },
	{ 0x52, 0x67 },
	{ 0x53, 0x48 },
	{ 0x54, 0x30 },
	{ 0x55, 0x6f },
	{ 0x56, 0x67 },
	{ 0x57, 0x48 },
	{ 0x58, 0x30 },
	{ 0x59, 0x6f },
	{ 0x5a, 0x67 },
	{ 0x5b, 0x48 },
	{ 0x5c, 0x30 },
	{ 0x5d, 0x6f },
	{ 0x5e, 0x67 },
	{ 0x5f, 0x48 },

	{ 0xff, 0x0a },
	{ 0x25, 0x10 },
	{ 0x27, 0x1e },
	{ 0x30, 0xac },
	{ 0x31, 0x78 },
	{ 0x32, 0x17 },
	{ 0x33, 0xc1 },
	{ 0x34, 0x40 },
	{ 0x35, 0x00 },
	{ 0x36, 0xc3 },
	{ 0x37, 0x0a },
	{ 0x38, 0x00 },
	{ 0x39, 0x02 },
	{ 0x3a, 0x00 },
	{ 0x3b, 0xb2 },
	{ 0xa5, 0x10 },
	{ 0xa7, 0x1e },
	{ 0xb0, 0xac },
	{ 0xb1, 0x78 },
	{ 0xb2, 0x17 },
	{ 0xb3, 0xc1 },
	{ 0xb4, 0x40 },
	{ 0xb5, 0x00 },
	{ 0xb6, 0xc3 },
	{ 0xb7, 0x0a },
	{ 0xb8, 0x00 },
	{ 0xb9, 0x02 },
	{ 0xba, 0x00 },
	{ 0xbb, 0xb2 },
	{ 0xff, 0x0b },
	{ 0x25, 0x10 },
	{ 0x27, 0x1e },
	{ 0x30, 0xac },
	{ 0x31, 0x78 },
	{ 0x32, 0x17 },
	{ 0x33, 0xc1 },
	{ 0x34, 0x40 },
	{ 0x35, 0x00 },
	{ 0x36, 0xc3 },
	{ 0x37, 0x0a },
	{ 0x38, 0x00 },
	{ 0x39, 0x02 },
	{ 0x3a, 0x00 },
	{ 0x3b, 0xb2 },
	{ 0xa5, 0x10 },
	{ 0xa7, 0x1e },
	{ 0xb0, 0xac },
	{ 0xb1, 0x78 },
	{ 0xb2, 0x17 },
	{ 0xb3, 0xc1 },
	{ 0xb4, 0x40 },
	{ 0xb5, 0x00 },
	{ 0xb6, 0xc3 },
	{ 0xb7, 0x0a },
	{ 0xb8, 0x00 },
	{ 0xb9, 0x02 },
	{ 0xba, 0x00 },
	{ 0xbb, 0xb2 },

	{ 0xff, 0x13 },
	{ 0x05, 0xa0 },
	{ 0x31, 0xff },
	{ 0x07, 0x47 },
	{ 0x12, 0x04 },
	{ 0x1e, 0x1f },
	{ 0x1f, 0x27 },
	{ 0x2e, 0x10 },
	{ 0x2f, 0xc8 },
	{ 0x31, 0xff },
	{ 0x32, 0x00 },
	{ 0x33, 0x00 },
	{ 0x72, 0x05 },
	{ 0x7a, 0xf0 },
	{ 0xff, _MAR_BANK_ },
	{ 0x10, 0xff },
	{ 0x11, 0xff },

	{ 0x30, 0x0f },
	{ 0x32, 0x92 },
	{ 0x34, 0xcd },
	{ 0x36, 0x04 },
	{ 0x38, 0x58 },

	{ 0x3c, 0x01 },
	{ 0x3d, 0x11 },
	{ 0x3e, 0x11 },
	{ 0x45, 0x60 },
	{ 0x46, 0x49 },

	{ 0xff, _MTX_BANK_ },
	{ 0xe9, 0x03 },
	{ 0x03, 0x02 },
	{ 0x01, 0xe0 },
	{ 0x00, 0x7d },
	{ 0x01, 0xe0 },
	{ 0x02, 0xa0 },
	{ 0x20, 0x1e },
	{ 0x20, 0x1f },

	{ 0x04, 0x38 },
	{ 0x45, 0xc4 },
	{ 0x46, 0x01 },
	{ 0x47, 0x1b },
	{ 0x48, 0x08 },
	{ 0x65, 0xc4 },
	{ 0x66, 0x01 },
	{ 0x67, 0x1b },
	{ 0x68, 0x08 },
	{ 0x85, 0xc4 },
	{ 0x86, 0x01 },
	{ 0x87, 0x1b },
	{ 0x88, 0x08 },
	{ 0xa5, 0xc4 },
	{ 0xa6, 0x01 },
	{ 0xa7, 0x1b },
	{ 0xa8, 0x08 },
	{ 0xc5, 0xc4 },
	{ 0xc6, 0x01 },	
	{ 0xc7, 0x1b },	
	{ 0xc8, 0x08 },
	{ 0xeb, 0x8d },

	{ 0xff, _MAR_BANK_ },
	{ 0x00, 0xff },
	{ 0x40, 0x01 },
	{ 0x40, 0x00 },
	{ 0xff, 0x01 },
	{ 0x97, 0x00 },
	{ 0x97, 0x0f },

	{ 0xff, 0x00 },  //test pattern
	{ 0x78, 0xba },
	{ 0x79, 0xac },
	{ 0xff, 0x05 },
	{ 0x2c, 0x08 },
	{ 0x6a, 0x80 },
	{ 0xff, 0x06 },
	{ 0x2c, 0x08 },
	{ 0x6a, 0x80 },
	{ 0xff, 0x07 },
	{ 0x2c, 0x08 },
	{ 0x6a, 0x80 },
	{ 0xff, 0x08 },
	{ 0x2c, 0x08 },
	{ 0x6a, 0x80 },
};

static __maybe_unused const struct regval common_setting_1458M_regs[] = {
	{ 0xff, 0x00 },
	{ 0x80, 0x0f },
	{ 0x00, 0x10 },
	{ 0x01, 0x10 },
	{ 0x02, 0x10 },
	{ 0x03, 0x10 },
	{ 0x22, 0x0b },
	{ 0x23, 0x41 },
	{ 0x26, 0x0b },
	{ 0x27, 0x41 },
	{ 0x2a, 0x0b },
	{ 0x2b, 0x41 },
	{ 0x2e, 0x0b },
	{ 0x2f, 0x41 },

	{ 0xff, 0x01 },
	{ 0x98, 0x30 },
	{ 0xed, 0x00 },

	{ 0xff, 0x05+0 },
	{ 0x00, 0xd0 },
	{ 0x01, 0x22 },
	{ 0x47, 0xee },
	{ 0x50, 0xc6 },
	{ 0x57, 0x00 },
	{ 0x58, 0x77 },
	{ 0x5b, 0x41 },
	{ 0x5c, 0x78 },
	{ 0xB8, 0xB8 },

	{ 0xff, 0x05+1 },
	{ 0x00, 0xd0 },
	{ 0x01, 0x22 },
	{ 0x47, 0xee },
	{ 0x50, 0xc6 },
	{ 0x57, 0x00 },
	{ 0x58, 0x77 },
	{ 0x5b, 0x41 },
	{ 0x5c, 0x78 },
	{ 0xB8, 0xB8 },

	{ 0xff, 0x05+2 },
	{ 0x00, 0xd0 },
	{ 0x01, 0x22 },
	{ 0x47, 0xee },
	{ 0x50, 0xc6 },
	{ 0x57, 0x00 },
	{ 0x58, 0x77 },
	{ 0x5b, 0x41 },
	{ 0x5c, 0x78 },
	{ 0xB8, 0xB8 },

	{ 0xff, 0x05+3 },
	{ 0x00, 0xd0 },
	{ 0x01, 0x22 },
	{ 0x47, 0xee },
	{ 0x50, 0xc6 },
	{ 0x57, 0x00 },
	{ 0x58, 0x77 },
	{ 0x5b, 0x41 },
	{ 0x5c, 0x78 },
	{ 0xB8, 0xB8 },

	{ 0xff, 0x09 },
	{ 0x50, 0x30 },
	{ 0x51, 0x6f },
	{ 0x52, 0x67 },
	{ 0x53, 0x48 },
	{ 0x54, 0x30 },
	{ 0x55, 0x6f },
	{ 0x56, 0x67 },
	{ 0x57, 0x48 },
	{ 0x58, 0x30 },
	{ 0x59, 0x6f },
	{ 0x5a, 0x67 },
	{ 0x5b, 0x48 },
	{ 0x5c, 0x30 },
	{ 0x5d, 0x6f },
	{ 0x5e, 0x67 },
	{ 0x5f, 0x48 },

	{ 0xff, 0x0a },
	{ 0x25, 0x10 },
	{ 0x27, 0x1e },
	{ 0x30, 0xac },
	{ 0x31, 0x78 },
	{ 0x32, 0x17 },
	{ 0x33, 0xc1 },
	{ 0x34, 0x40 },
	{ 0x35, 0x00 },
	{ 0x36, 0xc3 },
	{ 0x37, 0x0a },
	{ 0x38, 0x00 },
	{ 0x39, 0x02 },
	{ 0x3a, 0x00 },
	{ 0x3b, 0xb2 },
	{ 0xa5, 0x10 },
	{ 0xa7, 0x1e },
	{ 0xb0, 0xac },
	{ 0xb1, 0x78 },
	{ 0xb2, 0x17 },
	{ 0xb3, 0xc1 },
	{ 0xb4, 0x40 },
	{ 0xb5, 0x00 },
	{ 0xb6, 0xc3 },
	{ 0xb7, 0x0a },
	{ 0xb8, 0x00 },
	{ 0xb9, 0x02 },
	{ 0xba, 0x00 },
	{ 0xbb, 0xb2 },
	{ 0xff, 0x0b },
	{ 0x25, 0x10 },
	{ 0x27, 0x1e },
	{ 0x30, 0xac },
	{ 0x31, 0x78 },
	{ 0x32, 0x17 },
	{ 0x33, 0xc1 },
	{ 0x34, 0x40 },
	{ 0x35, 0x00 },
	{ 0x36, 0xc3 },
	{ 0x37, 0x0a },
	{ 0x38, 0x00 },
	{ 0x39, 0x02 },
	{ 0x3a, 0x00 },
	{ 0x3b, 0xb2 },
	{ 0xa5, 0x10 },
	{ 0xa7, 0x1e },
	{ 0xb0, 0xac },
	{ 0xb1, 0x78 },
	{ 0xb2, 0x17 },
	{ 0xb3, 0xc1 },
	{ 0xb4, 0x40 },
	{ 0xb5, 0x00 },
	{ 0xb6, 0xc3 },
	{ 0xb7, 0x0a },
	{ 0xb8, 0x00 },
	{ 0xb9, 0x02 },
	{ 0xba, 0x00 },
	{ 0xbb, 0xb2 },

	{ 0xff, 0x13 },
	{ 0x05, 0xa0 },
	{ 0x31, 0xff },
	{ 0x07, 0x47 },
	{ 0x12, 0x04 },
	{ 0x1e, 0x1f },
	{ 0x1f, 0x27 },
	{ 0x2e, 0x10 },
	{ 0x2f, 0xc8 },
	{ 0x31, 0xff },
	{ 0x32, 0x00 },
	{ 0x33, 0x00 },
	{ 0x72, 0x05 },
	{ 0x7a, 0xf0 },
	{ 0xff, _MAR_BANK_ },
	{ 0x10, 0xff },
	{ 0x11, 0xff },

	{ 0x30, 0x0f },
	{ 0x32, 0xff },
	{ 0x34, 0xcd },
	{ 0x36, 0x04 },
	{ 0x38, 0xff },
	{ 0x3c, 0x01 },
	{ 0x3d, 0x11 },
	{ 0x3e, 0x11 },
	{ 0x45, 0x60 },
	{ 0x46, 0x49 },

	{ 0xff, _MTX_BANK_ },
	{ 0xe9, 0x03 },
	{ 0x03, 0x02 },
	{ 0x01, 0xe4 },
	{ 0x00, 0x7d },
	{ 0x01, 0xe0 },
	{ 0x02, 0xa0 },
	{ 0x20, 0x1e },
	{ 0x20, 0x1f },
	{ 0x04, 0x6c },
	{ 0x45, 0xcd },
	{ 0x46, 0x42 },
	{ 0x47, 0x36 },
	{ 0x48, 0x0f },
	{ 0x65, 0xcd },
	{ 0x66, 0x42 },
	{ 0x67, 0x0e },
	{ 0x68, 0x0f },
	{ 0x85, 0xcd },
	{ 0x86, 0x42 },
	{ 0x87, 0x0e },
	{ 0x88, 0x0f },
	{ 0xa5, 0xcd },
	{ 0xa6, 0x42 },
	{ 0xa7, 0x0e },
	{ 0xa8, 0x0f },
	{ 0xc5, 0xcd },
	{ 0xc6, 0x42 },
	{ 0xc7, 0x0e },
	{ 0xc8, 0x0f },
	{ 0xeb, 0x8d },

	{ 0xff, _MAR_BANK_ },
	{ 0x00, 0xff },
	{ 0x40, 0x01 },
	{ 0x40, 0x00 },
	{ 0xff, 0x01 },
	{ 0x97, 0x00 },
	{ 0x97, 0x0f },

	{ 0xff, 0x00 },  //test pattern
	{ 0x78, 0xba },
	{ 0x79, 0xac },
	{ 0xff, 0x05 },
	{ 0x2c, 0x08 },
	{ 0x6a, 0x80 },
	{ 0xff, 0x06 },
	{ 0x2c, 0x08 },
	{ 0x6a, 0x80 },
	{ 0xff, 0x07 },
	{ 0x2c, 0x08 },
	{ 0x6a, 0x80 },
	{ 0xff, 0x08 },
	{ 0x2c, 0x08 },
	{ 0x6a, 0x80 },
};

static __maybe_unused const struct regval auto_detect_regs[] = {
	{ 0xFF, 0x13 },
	{ 0x30, 0x7f },
	{ 0x70, 0xf0 },

	{ 0xFF, 0x00 },
	{ 0x00, 0x18 },
	{ 0x01, 0x18 },
	{ 0x02, 0x18 },
	{ 0x03, 0x18 },

	{ 0x00, 0x10 },
	{ 0x01, 0x10 },
	{ 0x02, 0x10 },
	{ 0x03, 0x10 },
};

static struct nvp6188_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.global_reg_list = common_setting_1458M_regs,
		.mipi_freq_idx = 0,
		.bpp = 8,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.global_reg_list = common_setting_1458M_regs,
		.mipi_freq_idx = 0,
		.bpp = 8,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
		.width = 960,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.global_reg_list = common_setting_1458M_regs,
		.mipi_freq_idx = 0,
		.bpp = 8,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	},
};

static const s64 link_freq_items[] = {
	NVP6188_LINK_FREQ_1458M,
	NVP6188_LINK_FREQ_756M,
};

/* sensor register write */
static int nvp6188_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0) {
		usleep_range(300, 400);
		return 0;
	}

	dev_err(&client->dev,
		"nvp6188 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int nvp6188_write_array(struct i2c_client *client,
			       const struct regval *regs, int size)
{
	int i, ret = 0;

	i = 0;
	while (i < size) {
		ret = nvp6188_write_reg(client, regs[i].addr, regs[i].val);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}
		i++;
	}

	return ret;
}

/* sensor register read */
static int nvp6188_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev, "nvp6188 read reg(0x%x) failed !\n", reg);

	return ret;
}

static unsigned char nv6188_read_vfc(struct nvp6188 *nvp6188, unsigned char ch)
{
	unsigned char ch_vfc = 0xff;
	struct i2c_client *client = nvp6188->client;
	nvp6188_write_reg(client, 0xff, 0x05 + ch);
	nvp6188_read_reg(client, 0xf0, &ch_vfc);
	return ch_vfc;
}

static __maybe_unused int nvp6188_read_all_vfc(struct nvp6188 *nvp6188,
					       u8 *ch_vfc)
{
	int ret = 0;
	int check_cnt = 0, ch = 0;
	struct i2c_client *client = nvp6188->client;

	ret = nvp6188_write_array(client,
		auto_detect_regs, ARRAY_SIZE(auto_detect_regs));
	if (ret) {
		dev_err(&client->dev, "write auto_detect_regs faild %d", ret);
	}

	ret = -1;
	while ((check_cnt++) < 50) {
		for (ch = 0; ch < 4; ch++) {
			ch_vfc[ch] = nv6188_read_vfc(nvp6188, ch);
		}
		if (ch_vfc[0] != 0xff || ch_vfc[1] != 0xff ||
		    ch_vfc[2] != 0xff || ch_vfc[3] != 0xff) {
			ret = 0;
			if (ch == 3) {
				dev_dbg(&client->dev, "try check cnt %d",check_cnt);
				break;
			}
		} else {
			usleep_range(20 * 1000, 40 * 1000);
		}
	}

	if (ret) {
		dev_err(&client->dev, "read vfc faild %d", ret);
	} else {
		dev_dbg(&client->dev, "read vfc 0x%2x 0x%2x 0x%2x 0x%2x",
				ch_vfc[0], ch_vfc[1], ch_vfc[2], ch_vfc[3]);
	}
	return ret;
}

static __maybe_unused int nvp6188_auto_detect_fmt(struct nvp6188 *nvp6188)
{
	int ret = 0;
	int ch = 0;
	unsigned char ch_vfc[4] = { 0xff, 0xff, 0xff, 0xff };
	unsigned char val_13x70 = 0, val_13x71 = 0;
	struct i2c_client *client = nvp6188->client;

	if (nvp6188_read_all_vfc(nvp6188, ch_vfc))
		return -1;

	for (ch = 0; ch < 4; ch++) {
		nvp6188_write_reg(client, 0xFF, 0x13);
		nvp6188_read_reg(client, 0x70, &val_13x70);
		val_13x70 |= (0x01 << ch);
		nvp6188_write_reg(client, 0x70, val_13x70);
		nvp6188_read_reg(client, 0x71, &val_13x71);
		val_13x71 |= (0x01 << ch);
		nvp6188_write_reg(client, 0x71, val_13x71);
		switch(ch_vfc[ch]) {
			case NVP_RESO_960H_NSTC_VALUE:
				dev_dbg(&client->dev, "channel %d det 960h nstc", ch);
				nvp6188->cur_mode.channel_reso[ch] = NVP_RESO_960H_NSTC;
			break;
			case NVP_RESO_960H_PAL_VALUE:
				dev_dbg(&client->dev, "channel %d det 960h pal", ch);
				nvp6188->cur_mode.channel_reso[ch] = NVP_RESO_960H_PAL;
			break;
			case NVP_RESO_720P_NSTC_VALUE:
				dev_dbg(&client->dev, "channel %d det 720p nstc", ch);
				nvp6188->cur_mode.channel_reso[ch] = NVP_RESO_720P_NSTC;
			break;
			case NVP_RESO_720P_PAL_VALUE:
				dev_dbg(&client->dev, "channel %d det 720p pal", ch);
				nvp6188->cur_mode.channel_reso[ch] = NVP_RESO_720P_PAL;
			break;
			case NVP_RESO_1080P_NSTC_VALUE:
				dev_dbg(&client->dev, "channel %d det 1080p nstc", ch);
				nvp6188->cur_mode.channel_reso[ch] = NVP_RESO_1080P_NSTC;
			break;
			case NVP_RESO_1080P_PAL_VALUE:
				dev_dbg(&client->dev, "channel %d det 1080p pal", ch);
				nvp6188->cur_mode.channel_reso[ch] = NVP_RESO_1080P_PAL;
			break;
			case NVP_RESO_960P_NSTC_VALUE:
				dev_dbg(&client->dev, "channel %d det 960p nstc", ch);
				nvp6188->cur_mode.channel_reso[ch] = NVP_RESO_960P_NSTC;
			break;
			case NVP_RESO_960P_PAL_VALUE:
				dev_dbg(&client->dev, "channel %d det 960p pal", ch);
				nvp6188->cur_mode.channel_reso[ch] = NVP_RESO_960P_PAL;
			break;
			default:
				dev_err(&client->dev, "channel %d not detect, def 1080p pal\n", ch);
				nvp6188->cur_mode.channel_reso[ch] = NVP_RESO_1080P_PAL;
			break;
		}
	}

	return ret;
}

static __maybe_unused int nvp6188_auto_detect_hotplug(struct nvp6188 *nvp6188)
{
	int ret = 0;
	struct i2c_client *client = nvp6188->client;
	nvp6188_write_reg(client, 0xff, 0x00);
	nvp6188_read_reg(client, 0xa8, &nvp6188->detect_status);
	nvp6188->detect_status = ~nvp6188->detect_status;
	return ret;
}

static int nvp6188_get_reso_dist(const struct nvp6188_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static struct nvp6188_mode *
nvp6188_find_best_fit(struct nvp6188 *nvp6188,
                      struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < nvp6188->cfg_num; i++) {
		dist = nvp6188_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
		    supported_modes[i].bus_fmt == framefmt->code) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int nvp6188_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct nvp6188_mode *mode;
	u64 pixel_rate = 0;

	mutex_lock(&nvp6188->mutex);

	mode = nvp6188_find_best_fit(nvp6188, fmt);
	memcpy(&nvp6188->cur_mode, mode, sizeof(struct nvp6188_mode));

	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_SRGB;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&nvp6188->mutex);
		return -ENOTTY;
#endif
	} else {
		__v4l2_ctrl_s_ctrl(nvp6188->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * NVP6188_LANES;
		__v4l2_ctrl_s_ctrl_int64(nvp6188->pixel_rate, pixel_rate);
		dev_err(&nvp6188->client->dev, "mipi_freq_idx %d\n", mode->mipi_freq_idx);
		dev_err(&nvp6188->client->dev, "pixel_rate %lld\n", pixel_rate);
	}

	mutex_unlock(&nvp6188->mutex);
	return 0;
}

static int nvp6188_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct i2c_client *client = nvp6188->client;

	const struct nvp6188_mode *mode = &nvp6188->cur_mode;

	mutex_lock(&nvp6188->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&nvp6188->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && fmt->pad >= PAD0)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&nvp6188->mutex);

	dev_dbg(&client->dev, "%s: %x %dx%d vc %x\n",
		__func__, fmt->format.code,
		fmt->format.width, fmt->format.height, fmt->pad);

	return 0;
}

static int nvp6188_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct nvp6188 *nvp6188 = to_nvp6188(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = nvp6188->cur_mode.bus_fmt;

	return 0;
}

static int nvp6188_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct i2c_client *client = nvp6188->client;

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (fse->index >= nvp6188->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;
	return 0;
}

static int nvp6188_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	const struct nvp6188_mode *mode = &nvp6188->cur_mode;

	mutex_lock(&nvp6188->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&nvp6188->mutex);

	return 0;
}

static int nvp6188_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;

	return 0;
}

static int nvp6188_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->flags = V4L2_MBUS_CSI2_4_LANE |
		     V4L2_MBUS_CSI2_CHANNELS;

	return 0;
}

static void nvp6188_get_module_inf(struct nvp6188 *nvp6188,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, NVP6188_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, nvp6188->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, nvp6188->len_name, sizeof(inf->base.lens));
}

static void nvp6188_get_vc_fmt_inf(struct nvp6188 *nvp6188,
				   struct rkmodule_vc_fmt_info *inf)
{
	int ch = 0;
	unsigned char ch_vfc[4] = { 0xff, 0xff, 0xff, 0xff };
	memset(inf, 0, sizeof(*inf));
	nvp6188_read_all_vfc(nvp6188, ch_vfc);

	dev_dbg(&nvp6188->client->dev, "nvp6188_get_vc_fmt_inf 0x%2x 0x%2x 0x%2x 0x%2x",
			ch_vfc[0], ch_vfc[1], ch_vfc[2], ch_vfc[3]);

	for (ch = 0; ch < 4; ch++) {
		switch(ch_vfc[ch]) {
			case NVP_RESO_960H_NSTC_VALUE:
				inf->width[ch] = 960;
				inf->height[ch] = 576;
				inf->fps[ch] = 30;
			break;
			case NVP_RESO_960H_PAL_VALUE:
				inf->width[ch] = 960;
				inf->height[ch] = 576;
				inf->fps[ch] = 25;
			break;
			case NVP_RESO_960P_PAL_VALUE:
				inf->width[ch] = 1280;
				inf->height[ch] = 960;
				inf->fps[ch] = 25;
			break;
			case NVP_RESO_960P_NSTC_VALUE:
				inf->width[ch] = 1280;
				inf->height[ch] = 960;
				inf->fps[ch] = 30;
			break;
			case NVP_RESO_720P_PAL_VALUE:
				inf->width[ch] = 1280;
				inf->height[ch] = 720;
				inf->fps[ch] = 25;
			break;
			case NVP_RESO_720P_NSTC_VALUE:
				inf->width[ch] = 1280;
				inf->height[ch] = 720;
				inf->fps[ch] = 30;
			break;
			case NVP_RESO_1080P_NSTC_VALUE:
				inf->width[ch] = 1920;
				inf->height[ch] = 1080;
				inf->fps[ch] = 30;
			break;
			case NVP_RESO_1080P_PAL_VALUE:
			default:
				inf->width[ch] = 1920;
				inf->height[ch] = 1080;
				inf->fps[ch] = 25;
			break;
		}
	}
}

static void nvp6188_get_vc_hotplug_inf(struct nvp6188 *nvp6188,
				       struct rkmodule_vc_hotplug_info *inf)
{
	memset(inf, 0, sizeof(*inf));
	nvp6188_auto_detect_hotplug(nvp6188);
	inf->detect_status = nvp6188->detect_status;
}

static void nvp6188_get_vicap_rst_inf(struct nvp6188 *nvp6188,
				   struct rkmodule_vicap_reset_info *rst_info)
{
	rst_info->is_reset = nvp6188->is_reset;
	rst_info->src = RKCIF_RESET_SRC_ERR_HOTPLUG;
}

static void nvp6188_set_vicap_rst_inf(struct nvp6188 *nvp6188,
				   struct rkmodule_vicap_reset_info rst_info)
{
	nvp6188->is_reset = rst_info.is_reset;
}

static void nvp6188_set_streaming(struct nvp6188 *nvp6188, int on)
{
	struct i2c_client *client = nvp6188->client;

	dev_info(&client->dev, "%s: on: %d\n", __func__, on);

	if (on) {
		nvp6188_write_reg(client, 0xff, 0x20);
		nvp6188_write_reg(client, 0xff, 0xff);
	} else {
		nvp6188_write_reg(client, 0xff, 0x20);
		nvp6188_write_reg(client, 0xff, 0x00);
	}
}

static long nvp6188_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		nvp6188_get_module_inf(nvp6188, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_VC_FMT_INFO:
		nvp6188_get_vc_fmt_inf(nvp6188, (struct rkmodule_vc_fmt_info *)arg);
		break;
	case RKMODULE_GET_VC_HOTPLUG_INFO:
		nvp6188_get_vc_hotplug_inf(nvp6188, (struct rkmodule_vc_hotplug_info *)arg);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		nvp6188_get_vicap_rst_inf(nvp6188, (struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		nvp6188_set_vicap_rst_inf(nvp6188, *(struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		*(int *)arg = RKMODULE_START_STREAM_FRONT;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		nvp6188_set_streaming(nvp6188, !!stream);
		break;

	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long nvp6188_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_vc_fmt_info *vc_fmt_inf;
	struct rkmodule_vc_hotplug_info *vc_hp_inf;
	struct rkmodule_vicap_reset_info *vicap_rst_inf;
	int *seq;
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = nvp6188_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = nvp6188_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_VC_FMT_INFO:
		vc_fmt_inf = kzalloc(sizeof(*vc_fmt_inf), GFP_KERNEL);
		if (!vc_fmt_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = nvp6188_ioctl(sd, cmd, vc_fmt_inf);
		if (!ret) {
			ret = copy_to_user(up, vc_fmt_inf, sizeof(*vc_fmt_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(vc_fmt_inf);
		break;
	case RKMODULE_GET_VC_HOTPLUG_INFO:
		vc_hp_inf = kzalloc(sizeof(*vc_hp_inf), GFP_KERNEL);
		if (!vc_hp_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = nvp6188_ioctl(sd, cmd, vc_hp_inf);
		if (!ret) {
			ret = copy_to_user(up, vc_hp_inf, sizeof(*vc_hp_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(vc_hp_inf);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = nvp6188_ioctl(sd, cmd, vicap_rst_inf);
		if (!ret) {
			ret = copy_to_user(up, vicap_rst_inf, sizeof(*vicap_rst_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(vicap_rst_inf, up, sizeof(*vicap_rst_inf));
		if (!ret)
			ret = nvp6188_ioctl(sd, cmd, vicap_rst_inf);
		else
			ret = -EFAULT;
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		seq = kzalloc(sizeof(*seq), GFP_KERNEL);
		if (!seq) {
			ret = -ENOMEM;
			return ret;
		}

		ret = nvp6188_ioctl(sd, cmd, seq);
		if (!ret) {
			ret = copy_to_user(up, seq, sizeof(*seq));
			if (ret)
				ret = -EFAULT;
		}
		kfree(seq);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

//each channel setting
/*
960x480i
ch : 0 ~ 3
ntpal: 1:25p, 0:30p
*/
static __maybe_unused void nv6188_set_chn_960h(struct nvp6188 *nvp6188, u8 ch,
					       u8 ntpal)
{
	unsigned char val_0x54 = 0, val_20x01 = 0;
	struct i2c_client *client = nvp6188->client;
	dev_err(&client->dev, "nv6188_set_chn_960h ch %d ntpal %d", ch, ntpal);
	nvp6188_write_reg(client, 0xff, 0x00);
	nvp6188_write_reg(client, 0x08 + ch, ntpal ? 0xdd : 0xa0);
	nvp6188_write_reg(client, 0x18 + ch, 0x08);
	nvp6188_write_reg(client, 0x22 + ch * 4, 0x0b);
	nvp6188_write_reg(client, 0x23 + ch * 4, 0x41);
	nvp6188_write_reg(client, 0x30 + ch, 0x12);
	nvp6188_write_reg(client, 0x34 + ch, 0x01);
	nvp6188_read_reg(client, 0x54, &val_0x54);
	if (ntpal)
		val_0x54 &= ~(0x10 << ch);
	else
		val_0x54 |= (0x10 << ch);
	nvp6188_write_reg(client, 0x54, val_0x54);
	nvp6188_write_reg(client, 0x58 + ch, ntpal ? 0x80 : 0x90);
	nvp6188_write_reg(client, 0x5c + ch, ntpal ? 0xbe : 0xbc);
	nvp6188_write_reg(client, 0x64 + ch, ntpal ? 0xa0 : 0x81);
	nvp6188_write_reg(client, 0x81 + ch, ntpal ? 0xf0 : 0xe0);
	nvp6188_write_reg(client, 0x85 + ch, 0x00);
	nvp6188_write_reg(client, 0x89 + ch, 0x00);
	nvp6188_write_reg(client, ch + 0x8e, 0x00);
	nvp6188_write_reg(client, 0xa0 + ch, 0x05);

	nvp6188_write_reg(client, 0xff, 0x01);
	nvp6188_write_reg(client, 0x84 + ch, 0x02);
	nvp6188_write_reg(client, 0x88 + ch, 0x00);
	nvp6188_write_reg(client, 0x8c + ch, 0x40);
	nvp6188_write_reg(client, 0xa0 + ch, 0x20);
	nvp6188_write_reg(client, 0xed, 0x00);

	nvp6188_write_reg(client, 0xff, 0x05 + ch);
	nvp6188_write_reg(client, 0x01, 0x22);
	nvp6188_write_reg(client, 0x05, 0x00);
	nvp6188_write_reg(client, 0x08, 0x55);
	nvp6188_write_reg(client, 0x25, 0xdc);
	nvp6188_write_reg(client, 0x28, 0x80);
	nvp6188_write_reg(client, 0x2f, 0x00);
	nvp6188_write_reg(client, 0x30, 0xe0);
	nvp6188_write_reg(client, 0x31, 0x43);
	nvp6188_write_reg(client, 0x32, 0xa2);
	nvp6188_write_reg(client, 0x47, 0x04);
	nvp6188_write_reg(client, 0x50, 0x84);
	nvp6188_write_reg(client, 0x57, 0x00);
	nvp6188_write_reg(client, 0x58, 0x77);
	nvp6188_write_reg(client, 0x5b, 0x43);
	nvp6188_write_reg(client, 0x5c, 0x78);
	nvp6188_write_reg(client, 0x5f, 0x00);
	nvp6188_write_reg(client, 0x62, 0x20);
	nvp6188_write_reg(client, 0x7b, 0x00);
	nvp6188_write_reg(client, 0x7c, 0x01);
	nvp6188_write_reg(client, 0x7d, 0x80);
	nvp6188_write_reg(client, 0x80, 0x00);
	nvp6188_write_reg(client, 0x90, 0x01);
	nvp6188_write_reg(client, 0xa9, 0x00);
	nvp6188_write_reg(client, 0xb5, 0x00);
	nvp6188_write_reg(client, 0xb8, 0xb9);
	nvp6188_write_reg(client, 0xb9, 0x72);
	nvp6188_write_reg(client, 0xd1, 0x00);
	nvp6188_write_reg(client, 0xd5, 0x80);

	nvp6188_write_reg(client, 0xff, 0x09);
	nvp6188_write_reg(client, 0x96 + ch * 0x20, 0x10);
	nvp6188_write_reg(client, 0x98 + ch * 0x20, ntpal ? 0xc0 : 0xe0);
	nvp6188_write_reg(client, ch * 0x20 + 0x9e, 0x00);

	nvp6188_write_reg(client, 0xff, _MAR_BANK_);
	nvp6188_read_reg(client, 0x01, &val_20x01);
	val_20x01 &= (~(0x03 << (ch * 2)));
	val_20x01 |= (0x02 << (ch * 2));
	nvp6188_write_reg(client, 0x01, val_20x01);
	nvp6188_write_reg(client, 0x12 + ch * 2, 0xe0);
	nvp6188_write_reg(client, 0x13 + ch * 2, 0x01);
}

//each channel setting
/*
1280x720p
ch : 0 ~ 3
ntpal: 1:25p, 0:30p
*/
static __maybe_unused void nv6188_set_chn_720p(struct nvp6188 *nvp6188, u8 ch,
					       u8 ntpal)
{
	unsigned char val_0x54 = 0, val_20x01 = 0;
	struct i2c_client *client = nvp6188->client;
	dev_err(&client->dev, "nv6188_set_chn_720p ch %d ntpal %d", ch, ntpal);
	nvp6188_write_reg(client, 0xff, 0x00);
	nvp6188_write_reg(client, 0x08 + ch, 0x00);
	nvp6188_write_reg(client, 0x18 + ch, 0x3f);
	nvp6188_write_reg(client, 0x30 + ch, 0x12);
	nvp6188_write_reg(client, 0x34 + ch, 0x00);
	nvp6188_read_reg(client, 0x54, &val_0x54);
	val_0x54 &= ~(0x10 << ch);
	nvp6188_write_reg(client, 0x54, val_0x54);
	nvp6188_write_reg(client, 0x58 + ch, ntpal ? 0x80 : 0x80);
	nvp6188_write_reg(client, 0x5c + ch, ntpal ? 0x00 : 0x00);
	nvp6188_write_reg(client, 0x64 + ch, ntpal ? 0x01 : 0x01);
	nvp6188_write_reg(client, 0x81 + ch, ntpal ? 0x0d : 0x0c);
	nvp6188_write_reg(client, 0x85 + ch, 0x00);
	nvp6188_write_reg(client, 0x89 + ch, 0x00);
	nvp6188_write_reg(client, ch + 0x8e, 0x00);
	nvp6188_write_reg(client, 0xa0 + ch, 0x05);

	nvp6188_write_reg(client, 0xff, 0x01);
	nvp6188_write_reg(client, 0x84 + ch, 0x02);
	nvp6188_write_reg(client, 0x88 + ch, 0x00);
	nvp6188_write_reg(client, 0x8c + ch, 0x40);
	nvp6188_write_reg(client, 0xa0 + ch, 0x20);

	nvp6188_write_reg(client, 0xff, 0x05 + ch);
	nvp6188_write_reg(client, 0x01, 0x22);
	nvp6188_write_reg(client, 0x05, 0x04);
	nvp6188_write_reg(client, 0x08, 0x55);
	nvp6188_write_reg(client, 0x25, 0xdc);
	nvp6188_write_reg(client, 0x28, 0x80);
	nvp6188_write_reg(client, 0x2f, 0x00);
	nvp6188_write_reg(client, 0x30, 0xe0);
	nvp6188_write_reg(client, 0x31, 0x43);
	nvp6188_write_reg(client, 0x32, 0xa2);
	nvp6188_write_reg(client, 0x47, 0xee);
	nvp6188_write_reg(client, 0x50, 0xc6);
	nvp6188_write_reg(client, 0x57, 0x00);
	nvp6188_write_reg(client, 0x58, 0x77);
	nvp6188_write_reg(client, 0x5b, 0x41);
	nvp6188_write_reg(client, 0x5c, 0x7C);
	nvp6188_write_reg(client, 0x5f, 0x00);
	nvp6188_write_reg(client, 0x62, 0x20);
	nvp6188_write_reg(client, 0x7b, 0x11);
	nvp6188_write_reg(client, 0x7c, 0x01);
	nvp6188_write_reg(client, 0x7d, 0x80);
	nvp6188_write_reg(client, 0x80, 0x00);
	nvp6188_write_reg(client, 0x90, 0x01);
	nvp6188_write_reg(client, 0xa9, 0x00);
	nvp6188_write_reg(client, 0xb5, 0x40);
	nvp6188_write_reg(client, 0xb8, 0x39);
	nvp6188_write_reg(client, 0xb9, 0x72);
	nvp6188_write_reg(client, 0xd1, 0x00);
	nvp6188_write_reg(client, 0xd5, 0x80);

	nvp6188_write_reg(client, 0xff, 0x09);
	nvp6188_write_reg(client, 0x96 + ch * 0x20, 0x00);
	nvp6188_write_reg(client, 0x98 + ch * 0x20, 0x00);
	nvp6188_write_reg(client, ch * 0x20 + 0x9e, 0x00);

	nvp6188_write_reg(client, 0xff, _MAR_BANK_);
	nvp6188_read_reg(client, 0x01, &val_20x01);
	val_20x01 &= (~(0x03 << (ch * 2)));
	val_20x01 |= (0x01 << (ch * 2));
	nvp6188_write_reg(client, 0x01, val_20x01);
	nvp6188_write_reg(client, 0x12 + ch * 2, 0x80);
	nvp6188_write_reg(client, 0x13 + ch * 2, 0x02);
}

//each channel setting
/*
1920x1080p
ch : 0 ~ 3
ntpal: 1:25p, 0:30p
*/
static __maybe_unused void nv6188_set_chn_1080p(struct nvp6188 *nvp6188, u8 ch,
						u8 ntpal)
{
	unsigned char val_0x54 = 0, val_20x01 = 0;
	struct i2c_client *client = nvp6188->client;
	dev_err(&client->dev, "nv6188_set_chn_1080p ch %d ntpal %d", ch, ntpal);
	nvp6188_write_reg(client, 0xff, 0x00);
	nvp6188_write_reg(client, 0x08 + ch, 0x00);
	nvp6188_write_reg(client, 0x18 + ch, 0x3f);
	nvp6188_write_reg(client, 0x30 + ch, 0x12);
	nvp6188_write_reg(client, 0x34 + ch, 0x00);
	nvp6188_read_reg(client, 0x54, &val_0x54);
	val_0x54 &= ~(0x10 << ch);
	nvp6188_write_reg(client, 0x54, val_0x54);
	nvp6188_write_reg(client, 0x58 + ch, ntpal ? 0x80 : 0x80);
	nvp6188_write_reg(client, 0x5c + ch, ntpal ? 0x00 : 0x00);
	nvp6188_write_reg(client, 0x64 + ch, ntpal ? 0x01 : 0x01);
	nvp6188_write_reg(client, 0x81 + ch, ntpal ? 0x03 : 0x02);
	nvp6188_write_reg(client, 0x85 + ch, 0x00);
	nvp6188_write_reg(client, 0x89 + ch, 0x10);
	nvp6188_write_reg(client, ch + 0x8e, 0x00);
	nvp6188_write_reg(client, 0xa0 + ch, 0x05);

	nvp6188_write_reg(client, 0xff, 0x01);
	nvp6188_write_reg(client, 0x84 + ch, 0x02);
	nvp6188_write_reg(client, 0x88 + ch, 0x00);
	nvp6188_write_reg(client, 0x8c + ch, 0x40);
	nvp6188_write_reg(client, 0xa0 + ch, 0x20);

	nvp6188_write_reg(client, 0xff, 0x05 + ch);
	nvp6188_write_reg(client, 0x01, 0x22);
	nvp6188_write_reg(client, 0x05, 0x04);
	nvp6188_write_reg(client, 0x08, 0x55);
	nvp6188_write_reg(client, 0x25, 0xdc);
	nvp6188_write_reg(client, 0x28, 0x80);
	nvp6188_write_reg(client, 0x2f, 0x00);
	nvp6188_write_reg(client, 0x30, 0xe0);
	nvp6188_write_reg(client, 0x31, 0x41);
	nvp6188_write_reg(client, 0x32, 0xa2);
	nvp6188_write_reg(client, 0x47, 0xee);
	nvp6188_write_reg(client, 0x50, 0xc6);
	nvp6188_write_reg(client, 0x57, 0x00);
	nvp6188_write_reg(client, 0x58, 0x77);
	nvp6188_write_reg(client, 0x5b, 0x41);
	nvp6188_write_reg(client, 0x5c, 0x7C);
	nvp6188_write_reg(client, 0x5f, 0x00);
	nvp6188_write_reg(client, 0x62, 0x20);
	nvp6188_write_reg(client, 0x7b, 0x11);
	nvp6188_write_reg(client, 0x7c, 0x01);
	nvp6188_write_reg(client, 0x7d, 0x80);
	nvp6188_write_reg(client, 0x80, 0x00);
	nvp6188_write_reg(client, 0x90, 0x01);
	nvp6188_write_reg(client, 0xa9, 0x00);
	nvp6188_write_reg(client, 0xb5, 0x40);
	nvp6188_write_reg(client, 0xb8, 0x39);
	nvp6188_write_reg(client, 0xb9, 0x72);
	nvp6188_write_reg(client, 0xd1, 0x00);
	nvp6188_write_reg(client, 0xd5, 0x80);

	nvp6188_write_reg(client, 0xff, 0x09);
	nvp6188_write_reg(client, 0x96 + ch * 0x20, 0x00);
	nvp6188_write_reg(client, 0x98 + ch * 0x20, 0x00);
	nvp6188_write_reg(client, ch * 0x20 + 0x9e, 0x00);

	nvp6188_write_reg(client, 0xff, _MAR_BANK_);
	nvp6188_read_reg(client, 0x01, &val_20x01);
	val_20x01 &= (~(0x03 << (ch * 2)));
	nvp6188_write_reg(client, 0x01, val_20x01);
	nvp6188_write_reg(client, 0x12 + ch * 2, 0xc0);
	nvp6188_write_reg(client, 0x13 + ch * 2, 0x03);
}

static __maybe_unused void nvp6188_manual_mode(struct nvp6188 *nvp6188)
{
	int i, reso;
	for (i = 3; i >= 0; i--) {
		reso = nvp6188->cur_mode.channel_reso[i];
		switch (reso) {
		case NVP_RESO_960H_PAL:
			nv6188_set_chn_960h(nvp6188, i, 1);
			break;
		case NVP_RESO_720P_PAL:
			nv6188_set_chn_720p(nvp6188, i, 1);
			break;
		case NVP_RESO_1080P_PAL:
			nv6188_set_chn_1080p(nvp6188, i, 1);
			break;
		case NVP_RESO_960H_NSTC:
			nv6188_set_chn_960h(nvp6188, i, 0);
			break;
		case NVP_RESO_720P_NSTC:
			nv6188_set_chn_720p(nvp6188, i, 0);
			break;
		case NVP_RESO_1080P_NSTC:
			nv6188_set_chn_1080p(nvp6188, i, 0);
			break;
		default:
			nv6188_set_chn_1080p(nvp6188, i, 1);
			break;
		}
	}
}

static int detect_thread_function(void *data)
{
	struct nvp6188 *nvp6188 = (struct nvp6188 *) data;
	struct i2c_client *client = nvp6188->client;
	unsigned char bits = 0, ch, val_13x70 = 0, val_13x71 = 0;
	int need_reset_wait = -1;
	if (nvp6188->power_on) {
		nvp6188_auto_detect_hotplug(nvp6188);
		nvp6188->last_detect_status = nvp6188->detect_status;
		nvp6188->is_reset = 0;
	}
	while (!kthread_should_stop()) {
		if (nvp6188->power_on) {
			nvp6188_auto_detect_hotplug(nvp6188);
			if (nvp6188->last_detect_status != nvp6188->detect_status) {
				bits = nvp6188->last_detect_status ^ nvp6188->detect_status;
				for (ch = 0; ch < 4; ch++) {
					if (bits & (1 << ch)) {
						dev_err(&client->dev, "nvp6188 detect ch %d change\n", ch);
						nvp6188_write_reg(client, 0xFF, 0x13);
						nvp6188_read_reg(client, 0x70, &val_13x70);
						val_13x70 |= (0x01 << ch);
						nvp6188_write_reg(client, 0x70, val_13x70);
						nvp6188_read_reg(client, 0x71, &val_13x71);
						val_13x71 |= (0x01 << ch);
						nvp6188_write_reg(client, 0x71, val_13x71);
					}
				}
				nvp6188->last_detect_status = nvp6188->detect_status;
				input_event(nvp6188->input_dev, EV_MSC, MSC_RAW, nvp6188->detect_status);
				input_sync(nvp6188->input_dev);
				need_reset_wait = 5;
			}
			if (need_reset_wait > 0) {
				need_reset_wait--;
			} else if (need_reset_wait == 0) {
				need_reset_wait = -1;
				nvp6188->is_reset = 1;
				dev_err(&client->dev, "trigger reset time up\n");
			}
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(200));
	}
	return 0;
}

static int __maybe_unused detect_thread_start(struct nvp6188 *nvp6188)
{
	int ret = 0;
	struct i2c_client *client = nvp6188->client;
	nvp6188->detect_thread = kthread_create(detect_thread_function,
                                   nvp6188, "nvp6188_kthread");
	if (IS_ERR(nvp6188->detect_thread)) {
		dev_err(&client->dev, "kthread_create nvp6188_kthread failed\n");
		ret = PTR_ERR(nvp6188->detect_thread);
		nvp6188->detect_thread = NULL;
		return ret;
	}
	wake_up_process(nvp6188->detect_thread);
	return ret;
}

static int __maybe_unused detect_thread_stop(struct nvp6188 *nvp6188)
{
	if (nvp6188->detect_thread)
		kthread_stop(nvp6188->detect_thread);
	nvp6188->detect_thread = NULL;
	return 0;
}

static int __nvp6188_start_stream(struct nvp6188 *nvp6188)
{
	int ret;
	int array_size = 0;
	struct i2c_client *client = nvp6188->client;

	if (nvp6188->cur_mode.global_reg_list == common_setting_1458M_regs) {
		array_size = ARRAY_SIZE(common_setting_1458M_regs);
	} else if (nvp6188->cur_mode.global_reg_list == common_setting_756M_regs) {
		array_size = ARRAY_SIZE(common_setting_756M_regs);
	} else {
		return -1;
	}

	ret = nvp6188_write_array(nvp6188->client,
		nvp6188->cur_mode.global_reg_list, array_size);
	if (ret) {
		dev_err(&client->dev, "__nvp6188_start_stream global_reg_list faild");
		return ret;
	}

	nvp6188_auto_detect_fmt(nvp6188);
	nvp6188_manual_mode(nvp6188);
	nvp6188_audio_init(nvp6188);
	detect_thread_start(nvp6188);
	return 0;
}

static int __nvp6188_stop_stream(struct nvp6188 *nvp6188)
{
	struct i2c_client *client = nvp6188->client;
	nvp6188_write_reg(client, 0xff, 0x20);
	nvp6188_write_reg(client, 0x00, 0x00);
	nvp6188_write_reg(client, 0x40, 0x01);
	nvp6188_write_reg(client, 0x40, 0x00);
	detect_thread_stop(nvp6188);
	return 0;
}

static int nvp6188_stream(struct v4l2_subdev *sd, int on)
{
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct i2c_client *client = nvp6188->client;

	dev_dbg(&client->dev, "s_stream: %d. %dx%d\n", on,
			nvp6188->cur_mode.width,
			nvp6188->cur_mode.height);

	mutex_lock(&nvp6188->mutex);
	on = !!on;
	if (nvp6188->streaming == on)
		goto unlock;

	if (on) {
		__nvp6188_start_stream(nvp6188);
	} else {
		__nvp6188_stop_stream(nvp6188);
	}

	nvp6188->streaming = on;

unlock:
	mutex_unlock(&nvp6188->mutex);

	return 0;
}

static int nvp6188_power(struct v4l2_subdev *sd, int on)
{
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct i2c_client *client = nvp6188->client;
	int ret = 0;

	mutex_lock(&nvp6188->mutex);

	/* If the power state is not modified - no work to do. */
	if (nvp6188->power_on == !!on)
		goto exit;

	dev_dbg(&client->dev, "%s: on %d\n", __func__, on);

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto exit;
		}
		nvp6188->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		nvp6188->power_on = false;
	}

exit:
	mutex_unlock(&nvp6188->mutex);

	return ret;
}

static int __nvp6188_power_on(struct nvp6188 *nvp6188)
{
	int ret;
	struct device *dev = &nvp6188->client->dev;

	dev_dbg(dev, "%s\n", __func__);

	if (!IS_ERR_OR_NULL(nvp6188->pins_default)) {
		ret = pinctrl_select_state(nvp6188->pinctrl,
					   nvp6188->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins. ret=%d\n", ret);
	}

#if POWER_ALWAY_ON
#else
	if (!IS_ERR(nvp6188->power_gpio)) {
		gpiod_set_value_cansleep(nvp6188->power_gpio, 1);
		usleep_range(25 * 1000, 30 * 1000);
	}
#endif

	usleep_range(1500, 2000);

	ret = clk_set_rate(nvp6188->xvclk, NVP6188_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");
	if (clk_get_rate(nvp6188->xvclk) != NVP6188_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched\n");
	ret = clk_prepare_enable(nvp6188->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		goto err_clk;
	}

	if (!IS_ERR(nvp6188->reset_gpio)) {
		gpiod_set_value_cansleep(nvp6188->reset_gpio, 0);
		usleep_range(10 * 1000, 20 * 1000);
		gpiod_set_value_cansleep(nvp6188->reset_gpio, 1);
		usleep_range(10 * 1000, 20 * 1000);
	}

	usleep_range(10 * 1000, 20 * 1000);

	return 0;

err_clk:
	if (!IS_ERR(nvp6188->reset_gpio))
		gpiod_set_value_cansleep(nvp6188->reset_gpio, 1);

	if (!IS_ERR_OR_NULL(nvp6188->pins_sleep))
		pinctrl_select_state(nvp6188->pinctrl, nvp6188->pins_sleep);

	return ret;
}

static void __nvp6188_power_off(struct nvp6188 *nvp6188)
{
	int ret;
	struct device *dev = &nvp6188->client->dev;

	dev_dbg(dev, "%s\n", __func__);

	if (!IS_ERR(nvp6188->reset_gpio))
		gpiod_set_value_cansleep(nvp6188->reset_gpio, 1);
	clk_disable_unprepare(nvp6188->xvclk);

	if (!IS_ERR_OR_NULL(nvp6188->pins_sleep)) {
		ret = pinctrl_select_state(nvp6188->pinctrl,
					   nvp6188->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}

#if POWER_ALWAY_ON
#else
	if (!IS_ERR(nvp6188->power_gpio))
		gpiod_set_value_cansleep(nvp6188->power_gpio, 0);
#endif
}

static int nvp6188_initialize_controls(struct nvp6188 *nvp6188)
{
	const struct nvp6188_mode *mode;
	struct v4l2_ctrl_handler *handler;
	u64 pixel_rate;
	int ret;

	handler = &nvp6188->ctrl_handler;
	mode = &nvp6188->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 2);
	if (ret)
		return ret;
	handler->lock = &nvp6188->mutex;

	nvp6188->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_items) - 1, 0,
				link_freq_items);
	__v4l2_ctrl_s_ctrl(nvp6188->link_freq, mode->mipi_freq_idx);

	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * NVP6188_LANES;
	nvp6188->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, pixel_rate, 1, pixel_rate);
	if (handler->error) {
		ret = handler->error;
		dev_err(&nvp6188->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	dev_err(&nvp6188->client->dev, "mipi_freq_idx %d\n", mode->mipi_freq_idx);
	dev_err(&nvp6188->client->dev, "pixel_rate %lld\n", pixel_rate);
	dev_err(&nvp6188->client->dev, "link_freq %lld\n", link_freq_items[mode->mipi_freq_idx]);

	nvp6188->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int nvp6188_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct nvp6188 *nvp6188 = to_nvp6188(sd);

	return __nvp6188_power_on(nvp6188);
}

static int nvp6188_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct nvp6188 *nvp6188 = to_nvp6188(sd);

	__nvp6188_power_off(nvp6188);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int nvp6188_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct nvp6188_mode *def_mode = &supported_modes[0];

	dev_dbg(&nvp6188->client->dev, "%s\n", __func__);

	mutex_lock(&nvp6188->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&nvp6188->mutex);
	/* No crop or compose */

	return 0;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops nvp6188_internal_ops = {
	.open = nvp6188_open,
};
#endif

static const struct v4l2_subdev_video_ops nvp6188_video_ops = {
	.s_stream = nvp6188_stream,
	.g_frame_interval = nvp6188_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops nvp6188_subdev_pad_ops = {
	.enum_mbus_code = nvp6188_enum_mbus_code,
	.enum_frame_size = nvp6188_enum_frame_sizes,
	.enum_frame_interval = nvp6188_enum_frame_interval,
	.get_fmt = nvp6188_get_fmt,
	.set_fmt = nvp6188_set_fmt,
	.get_mbus_config = nvp6188_g_mbus_config,
};

static const struct v4l2_subdev_core_ops nvp6188_core_ops = {
	.s_power = nvp6188_power,
	.ioctl = nvp6188_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = nvp6188_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_ops nvp6188_subdev_ops = {
	.core = &nvp6188_core_ops,
	.video = &nvp6188_video_ops,
	.pad   = &nvp6188_subdev_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Audio Codec
 */
static unsigned int nvp6188_codec_read(struct snd_soc_component *component,
				       unsigned int reg)
{
	struct v4l2_subdev *sd = snd_soc_component_get_drvdata(component);
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct i2c_client *client = nvp6188->client;
	int ret;
	u8 val;

	ret = nvp6188_read_reg(client, reg, &val);
	if (ret < 0) {
		dev_err(&client->dev, "%s failed: (%d)\n", __func__, ret);
		return ret;
	}

	return val;
}

static int nvp6188_codec_write(struct snd_soc_component *component,
			       unsigned int reg, unsigned int val)
{
	struct v4l2_subdev *sd = snd_soc_component_get_drvdata(component);
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct i2c_client *client = nvp6188->client;
	int ret;

	ret = nvp6188_write_reg(client, reg, val);
	if (ret < 0) {
		dev_err(&client->dev, "%s failed: (%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int nvp6188_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	return 0;
}

static void nvp6188_pcm_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
}

static int nvp6188_pcm_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct v4l2_subdev *sd = snd_soc_dai_get_drvdata(dai);
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct i2c_client *client = nvp6188->client;
	u8 val_rm = 0, val_pb = 0;

	nvp6188_write_reg(client, 0xff, 0x01); /* Switch to bank1 for audio */
	nvp6188_read_reg(client, 0x07, &val_rm);
	nvp6188_read_reg(client, 0x13, &val_pb);
	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM: /* MASTER MODE */
		val_rm |= 0x80;
		val_pb |= 0x80;
		break;
	case SND_SOC_DAIFMT_CBS_CFS: /* SLAVE MODE */
		val_rm &= (~0x80);
		val_pb &= (~0x80);
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:	/* I2S MODE */
		val_rm &= (~0x01);
		val_pb &= (~0x01);
		break;
	case SND_SOC_DAIFMT_DSP_A:	/* DSP MODE */
		val_rm |= 0x01;
		val_pb |= 0x01;
		break;
	case SND_SOC_DAIFMT_DSP_B:	/* SSP MODE */
		val_rm |= 0x03;
		val_pb |= 0x03;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:	/* Inverted Clock */
		val_rm &= (~0x40);
		val_pb &= (~0x40);
		break;
	case SND_SOC_DAIFMT_IB_NF:	/* Non-inverted Clock */
		val_rm |= 0x40;
		val_pb |= 0x40;
		break;
	default:
		return -EINVAL;
	}

	nvp6188_write_reg(client, 0x07, val_rm);
	nvp6188_write_reg(client, 0x13, val_pb);

	return 0;
}

static int nvp6188_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct v4l2_subdev *sd = snd_soc_dai_get_drvdata(dai);
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct i2c_client *client = nvp6188->client;
	u8 val = 0;

	nvp6188_write_reg(client, 0xff, 0x01); /* Switch to bank1 for audio */

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* Configure formats for Playback */
		nvp6188_read_reg(client, 0x13, &val);
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S8:
			val |= 0x04;
			break;
		case SNDRV_PCM_FORMAT_S16_LE:
			val &= (~0x04);
			break;
		default:
			return -EINVAL;
		}

		switch (params_rate(params)) {
		case 8000:
			val &= (~0x08);
			break;
		case 16000:
			val |= 0x08;
			break;
		case 32000:
			/* TODO */
			break;
		default:
			return -EINVAL;
		}

		if (nvp6188->audio_out) {
			switch (nvp6188->audio_out->mclk_fs) {
			case 256:
				val = ((val & (~0x30)) | 0x00);
				break;
			case 384:
				val = ((val & (~0x30)) | 0x10);
				break;
			case 320:
				val = ((val & (~0x30)) | 0x20);
				break;
			default:
				dev_err(&client->dev, "Invalid audio_out mclk_fs: %d\n",
					nvp6188->audio_out->mclk_fs);
				return -EINVAL;
			}
		}

		nvp6188_write_reg(client, 0x13, val);
	} else {
		/* Configure formats for Capture */
		nvp6188_read_reg(client, 0x07, &val);
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S8:
			val |= 0x04;
			break;
		case SNDRV_PCM_FORMAT_S16_LE:
			val &= (~0x04);
			break;
		default:
			return -EINVAL;
		}

		switch (params_rate(params)) {
		case 8000:
			val &= (~0x08);
			break;
		case 16000:
			val |= 0x08;
			break;
		case 32000:
			/* TODO */
			break;
		default:
			return -EINVAL;
		}

		if (nvp6188->audio_in) {
			switch (nvp6188->audio_in->mclk_fs) {
			case 256:
				val = ((val & (~0x30)) | 0x00);
				break;
			case 384:
				val = ((val & (~0x30)) | 0x10);
				break;
			case 320:
				val = ((val & (~0x30)) | 0x20);
				break;
			default:
				dev_err(&client->dev, "Invalid audio_in mclk_fs: %d\n",
					nvp6188->audio_in->mclk_fs);
				return -EINVAL;
			}
		}
		nvp6188_write_reg(client, 0x07, val);

		nvp6188_read_reg(client, 0x08, &val);
		switch (params_channels(params)) {
		case 2:
			val = (val & (~0x03));
			break;
		case 4:
			val = (val & (~0x03)) | 0x01;
			break;
		default:
			dev_err(&client->dev, "Not supported channels: %d\n",
				params_channels(params));
			return -EINVAL;
		}
		nvp6188_write_reg(client, 0x08, val);
	}

	return 0;
}

static int nvp6188_pcm_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	return 0;
}

static const struct snd_soc_dai_ops nvp6188_dai_ops = {
	.startup = nvp6188_pcm_startup,
	.shutdown = nvp6188_pcm_shutdown,
	.set_fmt = nvp6188_pcm_set_dai_fmt,
	.hw_params = nvp6188_pcm_hw_params,
	.mute_stream = nvp6188_pcm_mute,
};

static struct snd_soc_dai_driver nvp6188_audio_dai = {
	.name = "nvp6188",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 16,
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			 SNDRV_PCM_RATE_32000,
		.formats = (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE),
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 16,
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			 SNDRV_PCM_RATE_32000,
		.formats = (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE),
	},
	.ops = &nvp6188_dai_ops,
};

static int nvp6188_codec_probe(struct snd_soc_component *component)
{
	return 0;
}

static void nvp6188_codec_remove(struct snd_soc_component *component)
{
}

/*
 * Control Functions
 */

/* nvp6188 tlv kcontrol calls */
static int nvp6188_codec_tlv_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct v4l2_subdev *sd = snd_soc_component_get_drvdata(component);
	struct nvp6188 *nvp6188 = to_nvp6188(sd);
	struct i2c_client *client = nvp6188->client;

	nvp6188_write_reg(client, 0xff, 0x01); /* Switch to bank1 for audio */
	return snd_soc_get_volsw(kcontrol, ucontrol);
}

static int nvp6188_codec_tlv_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	return snd_soc_put_volsw(kcontrol, ucontrol);
}

static const DECLARE_TLV_DB_SCALE(nvp6188_codec_aiao_gains_tlv, 0, 125, 1875);

/*
 * KControls
 */
static const struct snd_kcontrol_new nvp6188_codec_controls[] = {
	/* AIGAINs and MIGAIN */
	SOC_SINGLE_EXT_TLV("AIGain_01", 0x01,
		       0,
		       0x0f,
		       0,
		       nvp6188_codec_tlv_get,
		       nvp6188_codec_tlv_put,
		       nvp6188_codec_aiao_gains_tlv),
	SOC_SINGLE_EXT_TLV("AIGain_02", 0x02,
		       0,
		       0x0f,
		       0,
		       nvp6188_codec_tlv_get,
		       nvp6188_codec_tlv_put,
		       nvp6188_codec_aiao_gains_tlv),
	SOC_SINGLE_EXT_TLV("AIGain_03", 0x03,
		       0,
		       0x0f,
		       0,
		       nvp6188_codec_tlv_get,
		       nvp6188_codec_tlv_put,
		       nvp6188_codec_aiao_gains_tlv),
	SOC_SINGLE_EXT_TLV("AIGain_04", 0x04,
		       0,
		       0x0f,
		       0,
		       nvp6188_codec_tlv_get,
		       nvp6188_codec_tlv_put,
		       nvp6188_codec_aiao_gains_tlv),
	SOC_SINGLE_EXT_TLV("MIGain", 0x05,
		       0,
		       0x0f,
		       0,
		       nvp6188_codec_tlv_get,
		       nvp6188_codec_tlv_put,
		       nvp6188_codec_aiao_gains_tlv),

	/* AOGAIN */
	SOC_SINGLE_EXT_TLV("AOGain", 0x22,
		       0,
		       0x0f,
		       0,
		       nvp6188_codec_tlv_get,
		       nvp6188_codec_tlv_put,
		       nvp6188_codec_aiao_gains_tlv),
};

static struct snd_soc_component_driver nvp6188_codec_driver = {
	.probe			= nvp6188_codec_probe,
	.remove			= nvp6188_codec_remove,
	.read			= nvp6188_codec_read,
	.write			= nvp6188_codec_write,
	.controls		= nvp6188_codec_controls,
	.num_controls		= ARRAY_SIZE(nvp6188_codec_controls),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int check_chip_id(struct i2c_client *client){
	struct device *dev = &client->dev;
	unsigned char chip_id = 0, chip_revid = 0;
	nvp6188_write_reg(client, 0xFF, 0x00);
	nvp6188_read_reg(client, 0xF4, &chip_id);

	nvp6188_write_reg(client, 0xFF, 0x00);
	nvp6188_read_reg(client, 0xF5, &chip_revid);
	dev_err(dev, "chip_id : 0x%2x chip_revid : 0x%2x \n", chip_id, chip_revid);
	if (chip_id != 0xd0 && chip_id != 0xd3) {
		return -1;
	}
	return 0;
}

static int nvp6188_audio_init(struct nvp6188 *nvp6188)
{
	struct i2c_client *client = nvp6188->client;

	if (!nvp6188->audio_in && !nvp6188->audio_out)
		return 0;

	/* Switch to bank1 for audio */
	nvp6188_write_reg(client, 0xff, 0x01);
	nvp6188_write_reg(client, 0x94, 0x00);

	/* Single chip operation */
	nvp6188_write_reg(client, 0x06, 0x03);

	/* MSB/u-law/linear PCM/Speaker data/4ch */
	nvp6188_write_reg(client, 0x08, 0x01);

	/* Channels mapping */
	nvp6188_write_reg(client, 0x0a, 0x20);
	nvp6188_write_reg(client, 0x0b, 0x98);
	nvp6188_write_reg(client, 0x0f, 0x31);

	/* AOGAIN 1.0 */
	nvp6188_write_reg(client, 0x22, 0x08);
	/* First stage playback audio*/
	nvp6188_write_reg(client, 0x23, 0x10);

	/* Slave */
	nvp6188_write_reg(client, 0x39, 0x82);

	nvp6188_write_reg(client, 0x01, 0x09);
	nvp6188_write_reg(client, 0x02, 0x09);
	nvp6188_write_reg(client, 0x03, 0x09);
	nvp6188_write_reg(client, 0x04, 0x09);
	nvp6188_write_reg(client, 0x05, 0x09);

	nvp6188_write_reg(client, 0x31, 0x0a);
	nvp6188_write_reg(client, 0x47, 0x01);
	nvp6188_write_reg(client, 0x49, 0x88);
	nvp6188_write_reg(client, 0x44, 0x00);

	nvp6188_write_reg(client, 0x32, 0x00);
	/* Filter on / 16K Mode */
	nvp6188_write_reg(client, 0x00, 0x02);
	nvp6188_write_reg(client, 0x46, 0x10);
	nvp6188_write_reg(client, 0x48, 0xD0);
	nvp6188_write_reg(client, 0x94, 0x40);

	nvp6188_write_reg(client, 0x38, 0x18);
	msleep(30);
	nvp6188_write_reg(client, 0x38, 0x08);

	return 0;
}

static int nvp6188_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct nvp6188 *nvp6188;
	struct v4l2_subdev *sd;
	const char *str;
	__maybe_unused char facing[2];
	u32 v;
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	nvp6188 = devm_kzalloc(dev, sizeof(*nvp6188), GFP_KERNEL);
	if (!nvp6188)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &nvp6188->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &nvp6188->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &nvp6188->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &nvp6188->len_name);
	if (ret) {
		dev_err(dev, "could not get %s!\n", RKMODULE_CAMERA_LENS_NAME);
		return -EINVAL;
	}

	nvp6188->client = client;
	nvp6188->cfg_num = ARRAY_SIZE(supported_modes);
	memcpy(&nvp6188->cur_mode, &supported_modes[0], sizeof(struct nvp6188_mode));

	nvp6188->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(nvp6188->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	nvp6188->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(nvp6188->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	nvp6188->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_HIGH);
	if (IS_ERR(nvp6188->power_gpio))
		dev_warn(dev, "Failed to get power-gpios\n");

	nvp6188->vi_gpio = devm_gpiod_get(dev, "vi", GPIOD_OUT_HIGH);
	if (IS_ERR(nvp6188->vi_gpio))
		dev_warn(dev, "Failed to get vi-gpios\n");

	nvp6188->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(nvp6188->pinctrl)) {
		nvp6188->pins_default =
			pinctrl_lookup_state(nvp6188->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(nvp6188->pins_default))
			dev_info(dev, "could not get default pinstate\n");

		nvp6188->pins_sleep =
			pinctrl_lookup_state(nvp6188->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(nvp6188->pins_sleep))
			dev_info(dev, "could not get sleep pinstate\n");
	} else {
		dev_info(dev, "no pinctrl\n");
	}

	mutex_init(&nvp6188->mutex);

	sd = &nvp6188->subdev;
	v4l2_i2c_subdev_init(sd, client, &nvp6188_subdev_ops);
	ret = nvp6188_initialize_controls(nvp6188);
	if (ret) {
		dev_err(dev, "Failed to initialize controls nvp6188\n");
		goto err_destroy_mutex;
	}

	ret = __nvp6188_power_on(nvp6188);
	if (ret) {
		dev_err(dev, "Failed to power on nvp6188\n");
		goto err_free_handler;
	}

	ret = check_chip_id(client);
	if (ret) {
		dev_err(dev, "Failed to check senosr id\n");
		goto err_free_handler;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &nvp6188_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	nvp6188->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &nvp6188->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(nvp6188->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 nvp6188->module_index, facing,
		 NVP6188_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	/* Parse audio parts */
	nvp6188->audio_in = NULL;
	if (!of_property_read_string(node, "rockchip,audio-in-format", &str)) {
		struct nvp6188_audio *audio_stream;

		nvp6188->audio_in = devm_kzalloc(dev, sizeof(struct nvp6188_audio), GFP_KERNEL);
		if (!nvp6188->audio_in) {
			dev_err(dev, "alloc audio_in failed\n");
			return -ENOMEM;
		}
		audio_stream = nvp6188->audio_in;

		if (strcmp(str, "i2s") == 0)
			audio_stream->audfmt = AUDFMT_I2S;
		else if (strcmp(str, "dsp") == 0)
			audio_stream->audfmt = AUDFMT_DSP;
		else if (strcmp(str, "ssp") == 0)
			audio_stream->audfmt = AUDFMT_SSP;
		else {
			dev_err(dev, "rockchip,audio-in-format invalid\n");
			return -EINVAL;
		}

		if (!of_property_read_u32(node, "rockchip,audio-in-mclk-fs", &v)) {
			switch (v) {
			case 256:
			case 384:
			case 320:
				break;
			default:
				dev_err(dev,
					"rockchip,audio-in-mclk-fs invalid\n");
				return -EINVAL;
			}
			audio_stream->mclk_fs = v;
		}
	}

	nvp6188->audio_out = NULL;
	if (!of_property_read_string(node, "rockchip,audio-out-format", &str)) {
		struct nvp6188_audio *audio_stream;

		nvp6188->audio_out = devm_kzalloc(dev, sizeof(struct nvp6188_audio), GFP_KERNEL);
		if (!nvp6188->audio_out) {
			dev_err(dev, "alloc audio_out failed\n");
			return -ENOMEM;
		}
		audio_stream = nvp6188->audio_out;

		if (strcmp(str, "i2s") == 0)
			audio_stream->audfmt = AUDFMT_I2S;
		else if (strcmp(str, "dsp") == 0)
			audio_stream->audfmt = AUDFMT_DSP;
		else if (strcmp(str, "ssp") == 0)
			audio_stream->audfmt = AUDFMT_SSP;
		else {
			dev_err(dev, "rockchip,audio-out-format invalid\n");
			return -EINVAL;
		}

		if (!of_property_read_u32(node, "rockchip,audio-out-mclk-fs", &v)) {
			switch (v) {
			case 256:
			case 384:
			case 320:
				break;
			default:
				dev_err(dev,
					"rockchip,audio-out-mclk-fs invalid\n");
				return -EINVAL;
			}
			audio_stream->mclk_fs = v;
		}
	}

	/* Register audio DAIs */
	if (nvp6188->audio_in || nvp6188->audio_out) {
		ret = devm_snd_soc_register_component(dev,
					     &nvp6188_codec_driver,
					     &nvp6188_audio_dai, 1);
		if (ret) {
			dev_err(dev, "register audio codec failed\n");
			return -EINVAL;
		}

		dev_info(dev, "registered audio codec\n");
		nvp6188_audio_init(nvp6188);
	}

	if (sysfs_create_group(&dev->kobj, &dev_attr_grp))
		return -ENODEV;

	nvp6188->input_dev = devm_input_allocate_device(dev);
	if (nvp6188->input_dev == NULL) {
		dev_err(dev, "failed to allocate nvp6188 input device\n");
		return -ENOMEM;
	}
	nvp6188->input_dev->name = "nvp6188_input_event";
	set_bit(EV_MSC,  nvp6188->input_dev->evbit);
	set_bit(MSC_RAW, nvp6188->input_dev->mscbit);

	ret = input_register_device(nvp6188->input_dev);
	if (ret) {
		pr_err("%s: failed to register nvp6188 input device\n", __func__);
		return ret;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__nvp6188_power_off(nvp6188);
err_free_handler:
	v4l2_ctrl_handler_free(&nvp6188->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&nvp6188->mutex);

	return ret;
}

static int nvp6188_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct nvp6188 *nvp6188 = to_nvp6188(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&nvp6188->ctrl_handler);
	mutex_destroy(&nvp6188->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__nvp6188_power_off(nvp6188);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct dev_pm_ops nvp6188_pm_ops = {
	SET_RUNTIME_PM_OPS(nvp6188_runtime_suspend,
			   nvp6188_runtime_resume, NULL)
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id nvp6188_of_match[] = {
	{ .compatible = "nvp6188" },
	{},
};
MODULE_DEVICE_TABLE(of, nvp6188_of_match);
#endif

static const struct i2c_device_id nvp6188_match_id[] = {
	{ "nvp6188", 0 },
	{ },
};

static struct i2c_driver nvp6188_i2c_driver = {
	.driver = {
		.name = NVP6188_NAME,
		.pm = &nvp6188_pm_ops,
		.of_match_table = of_match_ptr(nvp6188_of_match),
	},
	.probe		= &nvp6188_probe,
	.remove		= &nvp6188_remove,
	.id_table	= nvp6188_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&nvp6188_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&nvp6188_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_AUTHOR("Vicent Chi <vicent.chi@rock-chips.com>");
MODULE_AUTHOR("Xing Zheng <zhengxing@rock-chips.com>");
MODULE_DESCRIPTION("nvp6188 sensor driver");
MODULE_LICENSE("GPL v2");
