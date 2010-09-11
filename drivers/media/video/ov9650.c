/*
o* Driver for MT9M001 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/circ_buf.h>
#include <linux/miscdevice.h>

#include <mach/spi_fpga.h>

#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>


struct reginfo
{
    u8 reg;
    u8 val;
};

/* init 352X288 SVGA */
static struct reginfo ov9650_init_data[] =
{
    {0x12, 0x80},
    {0x11, 0x00},//0x82:为20貞；//0x02：为10貞；
    {0x6b, 0x0a},
    {0x6a, 0x64},
    {0x3b, 0x09}, // night mode
    {0x13, 0xe0},
    {0x01, 0x80},
    {0x02, 0x80},
    {0x00, 0x00},
    {0x10, 0x00},
    {0x13, 0xe5},
    {0x39, 0x50},
    {0x38, 0x92},
    {0x37, 0x00},
    {0x35, 0x81},
    {0x0e, 0x20},
    {0x1e, 0x04},//34：第一个水平线有异常 -> ;//04: <- ;//14: ;//旋转度
    {0xA8, 0x80},
    {0x12, 0x10},
    {0x04, 0x00},
    {0x0c, 0x04},
    {0x0d, 0x80},
    {0x18, 0xc7},
    {0x17, 0x27},
    {0x32, 0xbd},
    {0x03, 0x36},
    {0x1a, 0x1e},
    {0x19, 0x00},
    {0x3f, 0xa6},
    {0x14, 0x1e},//降低增益，减小躁声
    {0x15, 0x00},//0x02
    {0x41, 0x02},
    {0x42, 0x08},
    {0x1b, 0x00},
    {0x16, 0x06},
    {0x33, 0xe2},
    {0x34, 0xbf},
    {0x96, 0x04},
    {0x3a, 0x00},
    {0x8e, 0x00},
    {0x3c, 0x77},
    {0x8B, 0x06},
    {0x94, 0x88},
    {0x95, 0x88},
    {0x40, 0xc1},
    {0x29, 0x3f},
    {0x0f, 0x42},
    {0x3d, 0x92},
    {0x69, 0x40},
    {0x5C, 0xb9},
    {0x5D, 0x96},
    {0x5E, 0x10},
    {0x59, 0xc0},
    {0x5A, 0xaf},
    {0x5B, 0x55},
    {0x43, 0xf0},
    {0x44, 0x10},
    {0x45, 0x68},
    {0x46, 0x96},
    {0x47, 0x60},
    {0x48, 0x80},
    {0x5F, 0xe0},
    {0x60, 0x8c},
    {0x61, 0x20},
    {0xa5, 0xd9},
    {0xa4, 0x74},
    {0x8d, 0x02},
    {0x13, 0xe7},
    {0x4f, 0x46},
    {0x50, 0x49},
    {0x51, 0x04},
    {0x52, 0x16},
    {0x53, 0x2e},
    {0x54, 0x43},
    {0x55, 0x40},
    {0x56, 0x40},
    {0x57, 0x40},
    {0x58, 0x0d},
    {0x8C, 0x23},
    {0x3E, 0x02},
    {0xa9, 0xb8},
    {0xaa, 0x92},
    {0xab, 0x0a},
    {0x8f, 0xdf},
    {0x90, 0x00},
    {0x91, 0x00},
    {0x9f, 0x00},
    {0xa0, 0x00},
    {0x3A, 0x0D},
    {0x24, 0x70},
    {0x25, 0x64},
    {0x26, 0xc3},
    {0x0f, 0x4a},
    {0x27, 0x20},
    {0x28, 0x20},
    {0x2c, 0x20},
    {0x2a, 0x10},
    {0x2b, 0x40},
    {0x6c, 0x40},
    {0x6d, 0x30},
    {0x6e, 0x4b},
    {0x6f, 0x60},
    {0x70, 0x70},
    {0x71, 0x70},
    {0x72, 0x70},
    {0x73, 0x70},
    {0x74, 0x60},
    {0x75, 0x60},
    {0x76, 0x50},
    {0x77, 0x48},
    {0x78, 0x3a},
    {0x79, 0x2e},
    {0x7a, 0x28},
    {0x7b, 0x22},
    {0x7c, 0x04},
    {0x7d, 0x07},
    {0x7e, 0x10},
    {0x7f, 0x28},
    {0x80, 0x36},
    {0x81, 0x44},
    {0x82, 0x52},
    {0x83, 0x60},
    {0x84, 0x6c},
    {0x85, 0x78},
    {0x86, 0x8c},
    {0x87, 0x9e},
    {0x88, 0xbb},
    {0x89, 0xd2},
    {0x8a, 0xe6},

	{0x00,0x00}
};

/* 1280X1024 SXGA */
static struct reginfo ov9650_sxga[] =
{
	{0x04, 0x00},
    {0xa8, 0x80},
    {0x0c, 0x00},
    {0x0d, 0x00},
    {0x11, 0x80},
    {0x6b, 0x0a},
    {0x6a, 0x41},
    {0x12, 0x00},
    {0x18, 0xbd},
    {0x17, 0x1d},
    {0x32, 0xbd},
    {0x03, 0x12},
    {0x1a, 0x81},
    {0x19, 0x01},
    {0x39, 0x43},
    {0x38, 0x12},
    {0x35, 0x91},
    {0x92, 0x00},
    {0x93, 0x00},
    {0x2a, 0x10},
    {0x2b, 0x34},
	{0x00,0x00}
};

/* 800X600 SVGA*/
static struct reginfo ov9650_svga[] =
{
	{0x00,0x00}
};

/* 640X480 VGA */
static struct reginfo ov9650_vga[] =
{
	{0xa8, 0x80},
    {0x0c, 0x04},
    {0x0d, 0x80},
    {0x11, 0x00},
    {0x6b, 0x0a},
    {0x6a, 0x3e},
    {0x12, 0x40},
    {0x18, 0xc7},
    {0x17, 0x27},
    {0x32, 0xbd},
    {0x03, 0x00},
    {0x1a, 0x3d},
    {0x19, 0x01},
    {0x39, 0x50},
    {0x38, 0x92},
    {0x35, 0x81},
    {0x92, 0x00},
    {0x93, 0x00},
    {0x2a, 0x10},
    {0x2b, 0x40},
	{0x00,0x00}
};

/* 352X288 CIF */
static struct reginfo ov9650_cif[] =
{
	{0x0c ,0x04},
    {0x0d ,0x80},
    {0x11 ,0x80},
    {0x12 ,0x20},
    {0x13 ,0xe5},
    {0x18 ,0xc7},
    {0x17 ,0x27},
    {0x03 ,0x00},
    {0x1a ,0x3d},
    {0x19 ,0x01},
    {0x39 ,0x50},
    {0x38 ,0x92},
    {0x35 ,0x81},
    {0x00,0x00}
};

/* 320*240 QVGA */
static  struct reginfo ov9650_qvga[] =
{
	{0x12, 0x10},
    {0xa8, 0x80},
    {0x04, 0x00},
    {0x0c, 0x04},
    {0x0d, 0x80},
    {0x18, 0xc7},
    {0x17, 0x27},
    {0x32, 0xbd},
    {0x03, 0x36},
    {0x1a, 0x1e},
    {0x19, 0x00},
    {0x11, 0x00},
    {0x6b, 0x0a},
    {0x92, 0x00},
    {0x93, 0x00},
    {0x2a, 0x10},
    {0x2b, 0x40},
    {0x6a, 0x3e},
    {0x3b, 0x09},
    {0x00,0x00}
};

/* 176X144 QCIF*/
static struct reginfo ov9650_qcif[] =
{
	{0x0c ,0x04},
    {0x0d ,0x80},
    {0x11 ,0x80},
    {0x12 ,0x08},
    {0x13 ,0xe5},
    {0x18 ,0xc7},
    {0x17 ,0x27},
    {0x03 ,0x00},
    {0x1a ,0x3d},
    {0x19 ,0x01},
    {0x39 ,0x50},
    {0x38 ,0x92},
    {0x35 ,0x81},
    {0x00,0x00}
};

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)

#define OV9650_MIN_WIDTH    176
#define OV9650_MIN_HEIGHT   144
#define OV9650_MAX_WIDTH    1280
#define OV9650_MAX_HEIGHT   1024

#define CONFIG_OV9650_TR      1
#define CONFIG_OV9650_DEBUG	  0
#if (CONFIG_OV9650_TR)
	#define OV9650_TR(format, ...)      printk(format, ## __VA_ARGS__)
	#if (CONFIG_OV9650_DEBUG)
	#define OV9650_DG(format, ...)      printk(format, ## __VA_ARGS__)
	#else
	#define OV9650_DG(format, ...)
	#endif
#else
	#define OV9650_TR(format, ...)
#endif

#define COL_FMT(_name, _depth, _fourcc, _colorspace) \
	{ .name = _name, .depth = _depth, .fourcc = _fourcc, \
	.colorspace = _colorspace }

#define JPG_FMT(_name, _depth, _fourcc) \
	COL_FMT(_name, _depth, _fourcc, V4L2_COLORSPACE_JPEG)

static const struct soc_camera_data_format ov9650_colour_formats[] = {
	JPG_FMT("ov9650 UYVY", 16, V4L2_PIX_FMT_UYVY),
	JPG_FMT("ov9650 YUYV", 16, V4L2_PIX_FMT_YUYV),
};

typedef struct ov9650_info_priv_s
{
    int whiteBalance;
    int brightness;
    int contrast;
    int saturation;
    int effect;
    int scene;
    int digitalzoom;
    int focus;
    int flash;
    int exposure;
    unsigned char mirror;                                        /* HFLIP */
    unsigned char flip;                                               /* VFLIP */
    unsigned int winseqe_cur_addr;

    unsigned int powerdown_pin;

} ov9650_info_priv_t;

struct ov9650
{
    struct v4l2_subdev subdev;
    struct i2c_client *client;
    ov9650_info_priv_t info_priv;
    unsigned int pixfmt;
    int model;	/* V4L2_IDENT_OV* codes from v4l2-chip-ident.h */
};

static const struct v4l2_queryctrl ov9650_controls[] =
{
    {
        .id		= V4L2_CID_DO_WHITE_BALANCE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "White Balance Control",
        .minimum	= 0,
        .maximum	= 4,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_BRIGHTNESS,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Brightness Control",
        .minimum	= -3,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_EFFECT,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Effect Control",
        .minimum	= 0,
        .maximum	= 5,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_EXPOSURE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Exposure Control",
        .minimum	= 0,
        .maximum	= 6,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_SATURATION,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Saturation Control",
        .minimum	= 0,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_CONTRAST,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Contrast Control",
        .minimum	= -3,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_HFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Mirror Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    }, {
        .id		= V4L2_CID_VFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Flip Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    }, {
        .id		= V4L2_CID_SCENE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Scene Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_ZOOM_RELATIVE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_ZOOM_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= 0,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_FOCUS_RELATIVE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_FOCUS_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 255,
        .step		= 1,
        .default_value = 125,
    }, {
        .id		= V4L2_CID_FLASH,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Flash Control",
        .minimum	= 0,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    }
};

static int ov9650_probe(struct i2c_client *client, const struct i2c_device_id *did);
static int ov9650_video_probe(struct soc_camera_device *icd, struct i2c_client *client);
static int ov9650_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int ov9650_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int ov9650_g_ext_controls(struct v4l2_subdev *sd,  struct v4l2_ext_controls *ext_ctrl);
static int ov9650_s_ext_controls(struct v4l2_subdev *sd,  struct v4l2_ext_controls *ext_ctrl);


static struct ov9650* to_ov9650(const struct i2c_client *client)
{
    return container_of(i2c_get_clientdata(client), struct ov9650, subdev);
}

/* ov9650 register write */
static int ov9650_write(struct i2c_client *client, u8 reg, u8 val)
{
    int ret; 
	ret = i2c_master_reg8_send(client, reg, &val, 1, 400*1000);
	return ret;   
}

/* ov9650 register read */
static int ov9650_read(struct i2c_client *client, u8 reg, u8 *val)
{
    int err,cnt;
    u8 buf[1];
    struct i2c_msg msg[1];

    buf[0] = reg & 0xFF;

    msg->addr = client->addr;
    msg->flags = client->flags;
    msg->buf = buf;
    msg->len = sizeof(buf);
    msg->scl_rate = 400*1000;                                        /* ddl@rock-chips.com : 100kHz */
    i2c_transfer(client->adapter, msg, 1);
    msg->addr = client->addr;
    msg->flags = client->flags|I2C_M_RD;
    msg->buf = buf;
    msg->len = 1;                                     /* ddl@rock-chips.com : 100kHz */

    cnt = 3;
    err = -EAGAIN;
    while ((cnt--) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 1);

        if (err >= 0) {
            *val = buf[0];
            return 0;
        } else {
            udelay(10);
         }
    }

    return err;
}

/* write a array of registers  */
static int ov9650_write_array(struct i2c_client *client, struct reginfo *regarray)
{
    int err;
    int i = 0;

    while (regarray[i].reg != 0)
    {
        err = ov9650_write(client, regarray[i].reg, regarray[i].val);
        if (err < 0)
        {
            OV9650_TR("write failed current i = %d\n", i);
            return err;
        }
        i++;
    }
    return 0;
}

static int ov9650_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = sd->priv;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct ov9650 *ov9650 = to_ov9650(client);
    int ret;

    OV9650_DG("\n%s..%d..  *** ddl ***\n",__FUNCTION__,__LINE__);

    /* soft reset */
    ret = ov9650_write(client, 0x12, 0x80);
    if (ret < 0)
    {
        OV9650_TR("soft reset ov9650 failed\n");
        return -ENODEV;
    }

    mdelay(5);  //delay 5 microseconds

    ret = ov9650_write_array(client, ov9650_init_data);
    if (ret != 0)
    {
        OV9650_TR("error: ov9650 initial failed\n");
        return ret;
    }

    icd->user_width = 320;
    icd->user_height = 240;

    /* sensor ov9650 information for initialization  */
    ov9650->info_priv.whiteBalance = ov9650_controls[0].default_value;
    ov9650->info_priv.brightness = ov9650_controls[1].default_value;
    ov9650->info_priv.effect = ov9650_controls[2].default_value;
    ov9650->info_priv.exposure = ov9650_controls[3].default_value;
    ov9650->info_priv.saturation = ov9650_controls[4].default_value;
    ov9650->info_priv.contrast = ov9650_controls[5].default_value;
    ov9650->info_priv.mirror = ov9650_controls[6].default_value;
    ov9650->info_priv.flip = ov9650_controls[7].default_value;
    ov9650->info_priv.scene = ov9650_controls[8].default_value;
    ov9650->info_priv.digitalzoom = ov9650_controls[10].default_value;
    ov9650->info_priv.winseqe_cur_addr  = (int)ov9650_svga;

    /* ddl@rock-chips.com : if sensor support auto focus and flash, programer must run focus and flash code  */
    //ov9650_set_focus();
    //ov9650_set_flash();
    ov9650->info_priv.focus = ov9650_controls[12].default_value;
    ov9650->info_priv.flash = ov9650_controls[13].default_value;


    OV9650_DG("\n%s..%d..  *** ddl *** icd->width = %d.. icd->height %d\n",__FUNCTION__,__LINE__,icd->user_width,icd->user_height);

    return 0;
}

static  struct reginfo ov9650_power_down_sequence[]=
{
    {0x00,0x00}
};
static int ov9650_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
    int ret;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct soc_camera_link *icl;


    if (pm_msg.event == PM_EVENT_SUSPEND)
    {
        OV9650_DG("\n ov9650 Enter Suspend. %x   ******** ddl *********\n", __LINE__);
        ret = ov9650_write_array(client, ov9650_power_down_sequence) ;
        if (ret != 0)
        {
            OV9650_TR("\n OV9650 WriteReg Fail.. %x   ******** ddl *********\n", __LINE__);
            return ret;
        }
        else
        {
            icl = to_soc_camera_link(icd);
            if (icl->power) {
                ret = icl->power(icd->pdev, 0);
                if (ret < 0)
                     return -EINVAL;
            }
        }
    }
    else
    {
        OV9650_TR("\n Sov9650 cann't suppout Suspend. %x   ******** ddl *********\n", __LINE__);
        return -EINVAL;
    }
    return 0;
}

static int ov9650_resume(struct soc_camera_device *icd)
{
    struct soc_camera_link *icl;
    int ret;

    icl = to_soc_camera_link(icd);
    if (icl->power) {
        ret = icl->power(icd->pdev, 0);
        if (ret < 0)
             return -EINVAL;
    }

    return 0;

}

static int ov9650_set_bus_param(struct soc_camera_device *icd,
                                unsigned long flags)
{

    return 0;
}

static unsigned long ov9650_query_bus_param(struct soc_camera_device *icd)
{
    struct soc_camera_link *icl = to_soc_camera_link(icd);
    unsigned long flags = SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING |
    SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_LOW |
    SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8  |SOCAM_MCLK_24MHZ;

    return soc_camera_apply_sensor_flags(icl, flags);
}

static int ov9650_g_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
    struct i2c_client *client = sd->priv;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct ov9650 *ov9650 = to_ov9650(client);
    struct v4l2_pix_format *pix = &f->fmt.pix;

    pix->width		= icd->user_width;
    pix->height		= icd->user_height;
    pix->pixelformat	= ov9650->pixfmt;
    pix->field		= V4L2_FIELD_NONE;
    pix->colorspace		= V4L2_COLORSPACE_JPEG;

    return 0;
}
static int ov9650_s_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
    struct i2c_client *client = sd->priv;
    struct ov9650 *ov9650 = to_ov9650(client);
    struct v4l2_pix_format *pix = &f->fmt.pix;
    struct reginfo *winseqe_set_addr;
    int ret, set_w,set_h;

    set_w = pix->width;
    set_h = pix->height;

	if ((set_w <= 176) && (set_h <= 144))
	{
		winseqe_set_addr = ov9650_qcif;
        set_w = 176;
        set_h = 144;
	}
	else if ((set_w <= 320) && (set_h <= 240))
    {
        winseqe_set_addr = ov9650_qvga;
        set_w = 320;
        set_h = 240;
    }
    else if ((set_w <= 352) && (set_h<= 288))
    {
        winseqe_set_addr = ov9650_cif;
        set_w = 352;
        set_h = 288;
    }
    else if ((set_w <= 640) && (set_h <= 480))
    {
        winseqe_set_addr = ov9650_vga;
        set_w = 640;
        set_h = 480;
    }
    else if ((set_w <= 1280) && (set_h <= 1024))
    {
        winseqe_set_addr = ov9650_sxga;
        set_w = 1280;
        set_h = 1024;
    }
    else
    {
        winseqe_set_addr = ov9650_qvga;               /* ddl@rock-chips.com : Sensor output smallest size if  isn't support app  */
        set_w = 320;
        set_h = 240;
    }

    if ((int)winseqe_set_addr  != ov9650->info_priv.winseqe_cur_addr)
    {
        ret = ov9650_write_array(client, winseqe_set_addr);
        if (ret != 0)
        {
            OV9650_TR("ov9650 set format capability failed\n");
            return ret;
        }

        ov9650->info_priv.winseqe_cur_addr  = (int)winseqe_set_addr;
        mdelay(250);

        OV9650_DG("\n%s..%d *** ddl *** icd->width = %d.. icd->height %d\n",__FUNCTION__,__LINE__,set_w,set_h);
    }
    else
    {
        OV9650_TR("\n .. Current Format is validate *** ddl *** icd->width = %d.. icd->height %d\n",set_w,set_h);
    }

    return 0;
}

static int ov9650_try_fmt(struct v4l2_subdev *sd, struct v4l2_format *f)
{
    struct v4l2_pix_format *pix = &f->fmt.pix;
    bool bayer = pix->pixelformat == V4L2_PIX_FMT_UYVY ||
        pix->pixelformat == V4L2_PIX_FMT_YUYV;

    /*
    * With Bayer format enforce even side lengths, but let the user play
    * with the starting pixel
    */

    if (pix->height > OV9650_MAX_HEIGHT)
        pix->height = OV9650_MAX_HEIGHT;
    else if (pix->height < OV9650_MIN_HEIGHT)
        pix->height = OV9650_MIN_HEIGHT;
    else if (bayer)
        pix->height = ALIGN(pix->height, 2);

    if (pix->width > OV9650_MAX_WIDTH)
        pix->width = OV9650_MAX_WIDTH;
    else if (pix->width < OV9650_MIN_WIDTH)
        pix->width = OV9650_MIN_WIDTH;
    else if (bayer)
        pix->width = ALIGN(pix->width, 2);

    return 0;
}

 static int ov9650_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *id)
{
    struct i2c_client *client = sd->priv;

    if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
        return -EINVAL;

    if (id->match.addr != client->addr)
        return -ENODEV;

    id->ident = V4L2_IDENT_OV9650;      /* ddl@rock-chips.com :  Return OV9650  identifier */
    id->revision = 0;

    return 0;
}

#define COLOR_TEMPERATURE_CLOUDY_DN  6500
#define COLOR_TEMPERATURE_CLOUDY_UP    8000
#define COLOR_TEMPERATURE_CLEARDAY_DN  5000
#define COLOR_TEMPERATURE_CLEARDAY_UP    6500
#define COLOR_TEMPERATURE_OFFICE_DN     3500
#define COLOR_TEMPERATURE_OFFICE_UP     5000
#define COLOR_TEMPERATURE_HOME_DN       2500
#define COLOR_TEMPERATURE_HOME_UP       3500

static  struct reginfo ov9650_WhiteB_Auto[]=
{
	{0x84, 0x6C},		//Contrast 4
	{0x85, 0x78},
	{0x86, 0x8C},
	{0x87, 0x9E},
	{0x88, 0xBB},
	{0x89, 0xD2},
	{0x8A, 0xE6},
	{0x6C, 0x40},
	{0x6D, 0x30},
	{0x6E, 0x48},
	{0x6F, 0x60},
	{0x70, 0x70},
	{0x71, 0x70},
	{0x72, 0x70},
	{0x73, 0x70},
	{0x74, 0x60},
	{0x75, 0x60},
	{0x76, 0x50},
	{0x77, 0x48},
	{0x78, 0x3A},
	{0x79, 0x2E},
	{0x7A, 0x28},
	{0x7B, 0x22},

	{0x0f, 0x4a},		//Saturation 3
	{0x27, 0x80},
	{0x28, 0x80},
	{0x2c, 0x80},
	{0x62, 0x60},
	{0x63, 0xe0},
	{0x64, 0x04},
	{0x65, 0x00},
	{0x66, 0x01},
	{0x24, 0x70},
	{0x25, 0x64},

	{0x4f, 0x2e},		//Brightness 3
	{0x50, 0x31},
	{0x51, 0x02},
	{0x52, 0x0e},
	{0x53, 0x1e},
	{0x54, 0x2d},

	{0x11, 0x80},
	{0x14, 0x2a},
	{0x13, 0xe7},
	{0x66, 0x05},

    {0x00, 0x00}
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct reginfo ov9650_WhiteB_Cloudy[]=
{
	{0x7C,0x04},		//Contrast 5
	{0x7D,0x09},
	{0x7E,0x13},
	{0x7F,0x29},
	{0x80,0x35},
	{0x81,0x41},
	{0x82,0x4D},
	{0x83,0x59},
	{0x84,0x64},
	{0x85,0x6F},
	{0x86,0x85},
	{0x87,0x97},
	{0x88,0xB7},
	{0x89,0xCF},
	{0x8A,0xE3},
	{0x6C,0x40},
	{0x6D,0x50},
	{0x6E,0x50},
	{0x6F,0x58},
	{0x70,0x60},
	{0x71,0x60},
	{0x72,0x60},
	{0x73,0x60},
	{0x74,0x58},
	{0x75,0x58},
	{0x76,0x58},
	{0x77,0x48},
	{0x78,0x40},
	{0x79,0x30},
	{0x7A,0x28},
	{0x7B,0x26},



	{0x4f,0x3a},		//Saturation 4
	{0x50,0x3d},
	{0x51,0x03},
	{0x52,0x12},
	{0x53,0x26},
	{0x54,0x38},
	{0x4f, 0x2e},		//Brightness 3
	{0x50, 0x31},
	{0x51, 0x02},
	{0x52, 0x0e},
	{0x53, 0x1e},
	{0x54, 0x2d},

	{0x11,0x80},
	{0x14,0x0a},
	{0x13,0xc7},
	{0x66,0x05},
    {0x00, 0x00}
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct reginfo ov9650_WhiteB_ClearDay[]=
{
    //Sunny
	{0x7C,0x04},		//Contrast 5
	{0x7D,0x09},
	{0x7E,0x13},
	{0x7F,0x29},
	{0x80,0x35},
	{0x81,0x41},
	{0x82,0x4D},
	{0x83,0x59},
	{0x84,0x64},
	{0x85,0x6F},
	{0x86,0x85},
	{0x87,0x97},
	{0x88,0xB7},
	{0x89,0xCF},
	{0x8A,0xE3},
	{0x6C,0x40},
	{0x6D,0x50},
	{0x6E,0x50},
	{0x6F,0x58},
	{0x70,0x60},
	{0x71,0x60},
	{0x72,0x60},
	{0x73,0x60},
	{0x74,0x58},
	{0x75,0x58},
	{0x76,0x58},
	{0x77,0x48},
	{0x78,0x40},
	{0x79,0x30},
	{0x7A,0x28},
	{0x7B,0x26},



	{0x4f,0x3a},		//Saturation 4
	{0x50,0x3d},
	{0x51,0x03},
	{0x52,0x12},
	{0x53,0x26},
	{0x54,0x38},
	{0x4f, 0x2e},		//Brightness 3
	{0x50, 0x31},
	{0x51, 0x02},
	{0x52, 0x0e},
	{0x53, 0x1e},
	{0x54, 0x2d},

	{0x11,0x80},
	{0x14,0x0a},
	{0x13,0xc7},
	{0x66,0x05},
    {0x00, 0x00}
};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct reginfo ov9650_WhiteB_TungstenLamp1[]=
{
    //Office
	{0x84, 0x6C},		//Contrast 4
	{0x85, 0x78},
	{0x86, 0x8C},
	{0x87, 0x9E},
	{0x88, 0xBB},
	{0x89, 0xD2},
	{0x8A, 0xE6},
	{0x6C, 0x40},
	{0x6D, 0x30},
	{0x6E, 0x48},
	{0x6F, 0x60},
	{0x70, 0x70},
	{0x71, 0x70},
	{0x72, 0x70},
	{0x73, 0x70},
	{0x74, 0x60},
	{0x75, 0x60},
	{0x76, 0x50},
	{0x77, 0x48},
	{0x78, 0x3A},
	{0x79, 0x2E},
	{0x7A, 0x28},
	{0x7B, 0x22},

	{0x0f, 0x4a},		//Saturation 3
	{0x27, 0x80},
	{0x28, 0x80},
	{0x2c, 0x80},
	{0x62, 0x60},
	{0x63, 0xe0},
	{0x64, 0x04},
	{0x65, 0x00},
	{0x66, 0x01},
	{0x24, 0x70},
	{0x25, 0x64},

	{0x4f, 0x2e},		//Brightness 3
	{0x50, 0x31},
	{0x51, 0x02},
	{0x52, 0x0e},
	{0x53, 0x1e},
	{0x54, 0x2d},

	{0x11,0x80},
	{0x14,0x2a},
	{0x13,0xe7},
	{0x66,0x05},

    {0x00, 0x00}

};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct reginfo ov9650_WhiteB_TungstenLamp2[]=
{
    //Home
	{0x84, 0x6C},		//Contrast 4
	{0x85, 0x78},
	{0x86, 0x8C},
	{0x87, 0x9E},
	{0x88, 0xBB},
	{0x89, 0xD2},
	{0x8A, 0xE6},
	{0x6C, 0x40},
	{0x6D, 0x30},
	{0x6E, 0x48},
	{0x6F, 0x60},
	{0x70, 0x70},
	{0x71, 0x70},
	{0x72, 0x70},
	{0x73, 0x70},
	{0x74, 0x60},
	{0x75, 0x60},
	{0x76, 0x50},
	{0x77, 0x48},
	{0x78, 0x3A},
	{0x79, 0x2E},
	{0x7A, 0x28},
	{0x7B, 0x22},

	{0x0f, 0x4a},		//Saturation 3
	{0x27, 0x80},
	{0x28, 0x80},
	{0x2c, 0x80},
	{0x62, 0x60},
	{0x63, 0xe0},
	{0x64, 0x04},
	{0x65, 0x00},
	{0x66, 0x01},
	{0x24, 0x70},
	{0x25, 0x64},

	{0x4f, 0x2e},		//Brightness 3
	{0x50, 0x31},
	{0x51, 0x02},
	{0x52, 0x0e},
	{0x53, 0x1e},
	{0x54, 0x2d},

	{0x11, 0x80},
	{0x14, 0x2a},
	{0x13, 0xe7},
	{0x66, 0x05},
    {0x00, 0x00}
};

static  struct reginfo ov9650_Brightness0[]=
{
    // Brightness -2

    {0x00, 0x00}
};

static  struct reginfo ov9650_Brightness1[]=
{
    // Brightness -1

    {0x00, 0x00}
};

static  struct reginfo ov9650_Brightness2[]=
{
    //  Brightness 0

    {0x00, 0x00}
};

static  struct reginfo ov9650_Brightness3[]=
{
    // Brightness +1

    {0x00, 0x00}
};

static  struct reginfo ov9650_Brightness4[]=
{
    //  Brightness +2

    {0x00, 0x00}
};

static  struct reginfo ov9650_Brightness5[]=
{
    //  Brightness +3

    {0x00, 0x00}
};

static  struct reginfo ov9650_Effect_Normal[] =
{
	{0x3a,0x0d},
	{0x67,0x80},
	{0x68,0x80},
    {0x00, 0x00}
};

static  struct reginfo ov9650_Effect_WandB[] =
{
	{0x3a,0x1d},
	{0x67,0x80},
	{0x68,0x80},
    {0x00, 0x00}
};

static  struct reginfo ov9650_Effect_Sepia[] =
{
	{0x3a,0x1d},
	{0x67,0x40},
	{0x68,0xa0},
    {0x00, 0x00}
};

static  struct reginfo ov9650_Effect_Negative[] =
{
    //Negative
	{0x3a,0x2d},
	{0x67,0x80},
	{0x68,0x80},
    {0x00, 0x00}
};
static  struct reginfo ov9650_Effect_Bluish[] =
{
    // Bluish
	{0x3a,0x1d},
	{0x67,0xc0},
	{0x68,0x80},
    {0x00, 0x00}
};

static  struct reginfo ov9650_Effect_Green[] =
{
    //  Greenish
	{0x3a,0x1d},
	{0x67,0x40},
	{0x68,0x40},
    {0x00, 0x00}
};
static  struct reginfo ov9650_Exposure0[]=
{
    //-3

    {0x00, 0x00}
};

static  struct reginfo ov9650_Exposure1[]=
{
    //-2

    {0x00, 0x00}
};

static  struct reginfo ov9650_Exposure2[]=
{
    //-0.3EV

    {0x00, 0x00}
};

static  struct reginfo ov9650_Exposure3[]=
{
    //default

    {0x00, 0x00}
};

static  struct reginfo ov9650_Exposure4[]=
{
    // 1

    {0x00, 0x00}
};

static  struct reginfo ov9650_Exposure5[]=
{
    // 2

    {0x00, 0x00}
};

static  struct reginfo ov9650_Exposure6[]=
{
    // 3

    {0x00, 0x00}
};

static  struct reginfo ov9650_Saturation0[]=
{

    {0x00, 0x00}
};

static  struct reginfo ov9650_Saturation1[]=
{

    {0x00, 0x00}
};

static  struct reginfo ov9650_Saturation2[]=
{

    {0x00, 0x00}
};


static  struct reginfo ov9650_Contrast0[]=
{
    //Contrast -3

    {0x00, 0x00}
};

static  struct reginfo ov9650_Contrast1[]=
{
    //Contrast -2

    {0x00, 0x00}
};

static  struct reginfo ov9650_Contrast2[]=
{
    // Contrast -1

    {0x00, 0x00}
};

static  struct reginfo ov9650_Contrast3[]=
{
    //Contrast 0

    {0x00, 0x00}
};

static  struct reginfo ov9650_Contrast4[]=
{
    //Contrast +1

    {0x00, 0x00}
};


static  struct reginfo ov9650_Contrast5[]=
{
    //Contrast +2

    {0x00, 0x00}
};

static  struct reginfo ov9650_Contrast6[]=
{
    //Contrast +3

    {0x00, 0x00}
};

static  struct reginfo ov9650_MirrorOn[]=
{

    {0x00, 0x00}
};

static  struct reginfo ov9650_MirrorOff[]=
{

    {0x00, 0x00}
};

static  struct reginfo ov9650_FlipOn[]=
{

    {0x00, 0x00}
};

static  struct reginfo ov9650_FlipOff[]=
{

    {0x00, 0x00}
};

static  struct reginfo ov9650_SceneAuto[] =
{

    {0x00, 0x00}
};

static  struct reginfo ov9650_SceneNight[] =
{
	{0x00, 0x00}
};


static struct reginfo ov9650_Zoom0[] =
{
    {0x0, 0x0},
};

static struct reginfo ov9650_Zoom1[] =
{
     {0x0, 0x0},
};

static struct reginfo ov9650_Zoom2[] =
{
    {0x0, 0x0},
};


static struct reginfo ov9650_Zoom3[] =
{
    {0x0, 0x0},
};

static struct reginfo *ov9650_ExposureSeqe[] = {ov9650_Exposure0, ov9650_Exposure1, ov9650_Exposure2, ov9650_Exposure3,
    ov9650_Exposure4, ov9650_Exposure5,ov9650_Exposure6,NULL,
};

static struct reginfo *ov9650_EffectSeqe[] = {ov9650_Effect_Normal, ov9650_Effect_WandB, ov9650_Effect_Negative,ov9650_Effect_Sepia,
    ov9650_Effect_Bluish, ov9650_Effect_Green,NULL,
};

static struct reginfo *ov9650_WhiteBalanceSeqe[] = {ov9650_WhiteB_Auto, ov9650_WhiteB_TungstenLamp1,ov9650_WhiteB_TungstenLamp2,
    ov9650_WhiteB_ClearDay, ov9650_WhiteB_Cloudy,NULL,
};

static struct reginfo *ov9650_BrightnessSeqe[] = {ov9650_Brightness0, ov9650_Brightness1, ov9650_Brightness2, ov9650_Brightness3,
    ov9650_Brightness4, ov9650_Brightness5,NULL,
};

static struct reginfo *ov9650_ContrastSeqe[] = {ov9650_Contrast0, ov9650_Contrast1, ov9650_Contrast2, ov9650_Contrast3,
    ov9650_Contrast4, ov9650_Contrast5, ov9650_Contrast6, NULL,
};

static struct reginfo *ov9650_SaturationSeqe[] = {ov9650_Saturation0, ov9650_Saturation1, ov9650_Saturation2, NULL,};

static struct reginfo *ov9650_MirrorSeqe[] = {ov9650_MirrorOff, ov9650_MirrorOn,NULL,};

static struct reginfo *ov9650_FlipSeqe[] = {ov9650_FlipOff, ov9650_FlipOn,NULL,};

static struct reginfo *ov9650_SceneSeqe[] = {ov9650_SceneAuto, ov9650_SceneNight,NULL,};

static struct reginfo *ov9650_ZoomSeqe[] = {ov9650_Zoom0, ov9650_Zoom1, ov9650_Zoom2, ov9650_Zoom3, NULL,};


static const struct v4l2_querymenu ov9650_menus[] =
{
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 0,  .name = "auto",  .reserved = 0, }, {  .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 1, .name = "incandescent",  .reserved = 0,},
            { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 2,  .name = "fluorescent", .reserved = 0,}, {  .id = V4L2_CID_DO_WHITE_BALANCE, .index = 3,  .name = "daylight", .reserved = 0,},
            { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 4,  .name = "cloudy-daylight", .reserved = 0,},
            { .id = V4L2_CID_EFFECT,  .index = 0,  .name = "none",  .reserved = 0, }, {  .id = V4L2_CID_EFFECT,  .index = 1, .name = "mono",  .reserved = 0,},
            { .id = V4L2_CID_EFFECT,  .index = 2,  .name = "negative", .reserved = 0,}, {  .id = V4L2_CID_EFFECT, .index = 3,  .name = "sepia", .reserved = 0,},
            { .id = V4L2_CID_EFFECT,  .index = 4, .name = "posterize", .reserved = 0,} ,{ .id = V4L2_CID_EFFECT,  .index = 5,  .name = "aqua", .reserved = 0,},
            { .id = V4L2_CID_SCENE,  .index = 0, .name = "auto", .reserved = 0,} ,{ .id = V4L2_CID_SCENE,  .index = 1,  .name = "night", .reserved = 0,},
            { .id = V4L2_CID_FLASH,  .index = 0,  .name = "off",  .reserved = 0, }, {  .id = V4L2_CID_FLASH,  .index = 1, .name = "auto",  .reserved = 0,},
            { .id = V4L2_CID_FLASH,  .index = 2,  .name = "on", .reserved = 0,}, {  .id = V4L2_CID_FLASH, .index = 3,  .name = "torch", .reserved = 0,},
};

static struct soc_camera_ops ov9650_ops =
{
    .suspend                     = ov9650_suspend,
    .resume                       = ov9650_resume,
    .set_bus_param		= ov9650_set_bus_param,
    .query_bus_param	= ov9650_query_bus_param,
    .controls		= ov9650_controls,
    .menus                         = ov9650_menus,
    .num_controls		= ARRAY_SIZE(ov9650_controls),
    .num_menus		= ARRAY_SIZE(ov9650_menus),
};
static int ov9650_set_brightness(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (ov9650_BrightnessSeqe[value - qctrl->minimum] != NULL)
        {
            if (ov9650_write_array(client, ov9650_BrightnessSeqe[value - qctrl->minimum]) != 0)
            {
                OV9650_TR("\n OV9650 WriteReg Fail.. %x   ******** ddl *********\n", __LINE__);
                return -EINVAL;
            }
            OV9650_DG("\n OV9650 Set Brightness - %x   ******** ddl *********\n", value);
            return 0;
        }
    }
    return -EINVAL;
}
static int ov9650_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (ov9650_EffectSeqe[value - qctrl->minimum] != NULL)
        {
            if (ov9650_write_array(client, ov9650_EffectSeqe[value - qctrl->minimum]) != 0)
            {
                OV9650_TR("\n OV9650 WriteReg Fail.. %x   ******** ddl *********\n", __LINE__);
                return -EINVAL;
            }
            OV9650_DG("\n OV9650 Set effect - %x   ******** ddl *********\n", value);
            return 0;
        }
    }
    return -EINVAL;
}
static int ov9650_set_exposure(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (ov9650_ExposureSeqe[value - qctrl->minimum] != NULL)
        {
            if (ov9650_write_array(client, ov9650_ExposureSeqe[value - qctrl->minimum]) != 0)
            {
                OV9650_TR("\n OV9650 WriteReg Fail.. %x   ******** ddl *********\n", __LINE__);
                return -EINVAL;
            }
            OV9650_DG("\n OV9650 Set Exposurce - %x   ******** ddl *********\n", value);
            return 0;
        }
    }
    return -EINVAL;
}

static int ov9650_set_saturation(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (ov9650_SaturationSeqe[value - qctrl->minimum] != NULL)
        {
            if (ov9650_write_array(client, ov9650_SaturationSeqe[value - qctrl->minimum]) != 0)
            {
                OV9650_TR("\n OV9650 WriteReg Fail.. %x   ******** ddl *********\n", __LINE__);
                return -EINVAL;
            }
            OV9650_DG("\n OV9650 Set Saturation - %x   ******** ddl *********\n", value);
            return 0;
        }
    }
    OV9650_TR("\n Saturation valure = %d is invalidate..    ******** ddl *********\n",value);
    return -EINVAL;
}

static int ov9650_set_contrast(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (ov9650_ContrastSeqe[value - qctrl->minimum] != NULL)
        {
            if (ov9650_write_array(client, ov9650_ContrastSeqe[value - qctrl->minimum]) != 0)
            {
                OV9650_TR("\n OV9650 WriteReg Fail.. %x   ******** ddl *********\n", __LINE__);
                return -EINVAL;
            }
            OV9650_DG("\n OV9650 Set Contrast - %x   ******** ddl *********\n", value);
            return 0;
        }
    }
    OV9650_TR("\n Contrast valure = %d is invalidate..    ******** ddl *********\n", value);
    return -EINVAL;
}

static int ov9650_set_mirror(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (ov9650_MirrorSeqe[value - qctrl->minimum] != NULL)
        {
            if (ov9650_write_array(client, ov9650_MirrorSeqe[value - qctrl->minimum]) != 0)
            {
                OV9650_TR("\n OV9650 WriteReg Fail.. %x   ******** ddl *********\n", __LINE__);
                return -EINVAL;
            }
            OV9650_DG("\n OV9650 Set Mirror - %x   ******** ddl *********\n", value);
            return 0;
        }
    }
    OV9650_TR("\n Mirror valure = %d is invalidate..    ******** ddl *********\n", value);
    return -EINVAL;
}


static int ov9650_set_flip(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (ov9650_FlipSeqe[value - qctrl->minimum] != NULL)
        {
            if (ov9650_write_array(client, ov9650_FlipSeqe[value - qctrl->minimum]) != 0)
            {
                OV9650_TR("\n OV9650 WriteReg Fail.. %x   ******** ddl *********\n", __LINE__);
                return -EINVAL;
            }
            OV9650_DG("\n OV9650 Set Flip - %x   ******** ddl *********\n", value);
            return 0;
        }
    }
    OV9650_TR("\n Flip valure = %d is invalidate..    ******** ddl *********\n", value);
    return -EINVAL;
}

static int ov9650_set_scene(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (ov9650_SceneSeqe[value - qctrl->minimum] != NULL)
        {
            if (ov9650_write_array(client, ov9650_SceneSeqe[value - qctrl->minimum]) != 0)
            {
                OV9650_TR("\n OV9650 WriteReg Fail.. %x   ******** ddl *********\n", __LINE__);
                return -EINVAL;
            }
            OV9650_DG("\n OV9650 Set Scene - %x   ******** ddl *********\n", value);
            return 0;
        }
    }
    OV9650_TR("\n Scene valure = %d is invalidate..    ******** ddl *********\n", value);
    return -EINVAL;
}

static int ov9650_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (ov9650_WhiteBalanceSeqe[value - qctrl->minimum] != NULL)
        {
            if (ov9650_write_array(client, ov9650_WhiteBalanceSeqe[value - qctrl->minimum]) != 0)
            {
                OV9650_TR("OV9650 WriteReg Fail.. %x\n", __LINE__);
                return -EINVAL;
            }
            OV9650_DG("ov9650_set_whiteBalance - %x\n", value);
            return 0;
        }
    }
    return -EINVAL;
}

static int ov9650_set_digitalzoom(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct ov9650 *ov9650 = to_ov9650(client);
    int digitalzoom_cur, digitalzoom_total;

    digitalzoom_cur = ov9650->info_priv.digitalzoom;
    digitalzoom_total = ov9650_controls[10].maximum;

    if ((*value > 0) && (digitalzoom_cur >= digitalzoom_total))
    {
        OV9650_TR("ov9650 digitalzoom is maximum - %x\n", digitalzoom_cur);
        return -EINVAL;
    }

    if  ((*value < 0) && (digitalzoom_cur <= ov9650_controls[10].minimum))
    {
        OV9650_TR("ov9650 digitalzoom is minimum - %x\n", digitalzoom_cur);
        return -EINVAL;
    }

    if ((*value > 0) && ((digitalzoom_cur + *value) > digitalzoom_total))
    {
        *value = digitalzoom_total - digitalzoom_cur;
    }

    if ((*value < 0) && ((digitalzoom_cur + *value) < 0))
    {
        *value = 0 - digitalzoom_cur;
    }

    digitalzoom_cur += *value;

    if (ov9650_ZoomSeqe[digitalzoom_cur] != NULL)
    {
        if (ov9650_write_array(client, ov9650_ZoomSeqe[digitalzoom_cur]) != 0)
        {
            OV9650_TR("OV9650 WriteReg Fail.. %x\n", __LINE__);
            return -EINVAL;
        }
        OV9650_DG("ov9650_set_digitalzoom - %x\n", *value);
        return 0;
    }

    return -EINVAL;
}

static int ov9650_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = sd->priv;
    struct ov9650 *ov9650 = to_ov9650(client);
    const struct v4l2_queryctrl *qctrl;

    qctrl = soc_camera_find_qctrl(&ov9650_ops, ctrl->id);

    if (!qctrl)
    {
        OV9650_TR("\n%s..%s..%d.. ioctrl is faild    ******** ddl *********\n",__FUNCTION__,__FILE__,__LINE__);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
        case V4L2_CID_BRIGHTNESS:
            {
                ctrl->value = ov9650->info_priv.brightness;
                break;
            }
        case V4L2_CID_SATURATION:
            {
                ctrl->value = ov9650->info_priv.saturation;
                break;
            }
        case V4L2_CID_CONTRAST:
            {
                ctrl->value = ov9650->info_priv.contrast;
                break;
            }
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                ctrl->value = ov9650->info_priv.whiteBalance;
                break;
            }
        case V4L2_CID_EXPOSURE:
            {
                ctrl->value = ov9650->info_priv.exposure;
                break;
            }
        case V4L2_CID_HFLIP:
            {
                ctrl->value = ov9650->info_priv.mirror;
                break;
            }
        case V4L2_CID_VFLIP:
            {
                ctrl->value = ov9650->info_priv.flip;
                break;
            }
        default :
                break;
    }
    return 0;
}



static int ov9650_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = sd->priv;
    struct ov9650 *ov9650 = to_ov9650(client);
    struct soc_camera_device *icd = client->dev.platform_data;
    const struct v4l2_queryctrl *qctrl;


    qctrl = soc_camera_find_qctrl(&ov9650_ops, ctrl->id);

    if (!qctrl)
    {
        OV9650_TR("\n OV9650 ioctrl id = %x  is invalidate   ******** ddl *********\n", ctrl->id);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
        case V4L2_CID_BRIGHTNESS:
            {
                if (ctrl->value != ov9650->info_priv.brightness)
                {
                    if (ov9650_set_brightness(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    ov9650->info_priv.brightness = ctrl->value;
                }
                break;
            }
        case V4L2_CID_EXPOSURE:
            {
                if (ctrl->value != ov9650->info_priv.exposure)
                {
                    if (ov9650_set_exposure(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    ov9650->info_priv.exposure = ctrl->value;
                }
                break;
            }
        case V4L2_CID_SATURATION:
            {
                if (ctrl->value != ov9650->info_priv.saturation)
                {
                    if (ov9650_set_saturation(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    ov9650->info_priv.saturation = ctrl->value;
                }
                break;
            }
        case V4L2_CID_CONTRAST:
            {
                if (ctrl->value != ov9650->info_priv.contrast)
                {
                    if (ov9650_set_contrast(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    ov9650->info_priv.contrast = ctrl->value;
                }
                break;
            }
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                if (ctrl->value != ov9650->info_priv.whiteBalance)
                {
                    if (ov9650_set_whiteBalance(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    ov9650->info_priv.whiteBalance = ctrl->value;
                }
                break;
            }
        case V4L2_CID_HFLIP:
            {
                if (ctrl->value != ov9650->info_priv.mirror)
                {
                    if (ov9650_set_mirror(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    ov9650->info_priv.mirror = ctrl->value;
                }
                break;
            }
        case V4L2_CID_VFLIP:
            {
                if (ctrl->value != ov9650->info_priv.flip)
                {
                    if (ov9650_set_flip(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    ov9650->info_priv.flip = ctrl->value;
                }
                break;
            }
        default :
            break;
    }

    return 0;
}
static int ov9650_g_ext_control(struct soc_camera_device *icd , struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct ov9650 *ov9650 = to_ov9650(client);

    qctrl = soc_camera_find_qctrl(&ov9650_ops, ext_ctrl->id);

    if (!qctrl)
    {
        OV9650_TR("\n%s..%s..%d.. ioctrl is faild    ******** ddl *********\n",__FUNCTION__,__FILE__,__LINE__);
        return -EINVAL;
    }

    switch (ext_ctrl->id)
    {
        case V4L2_CID_SCENE:
            {
                ext_ctrl->value = ov9650->info_priv.scene;
                break;
            }
        case V4L2_CID_EFFECT:
            {
                ext_ctrl->value = ov9650->info_priv.effect;
                break;
            }
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                ext_ctrl->value = ov9650->info_priv.digitalzoom;
                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FOCUS_ABSOLUTE:
            {
                ext_ctrl->value = ov9650->info_priv.focus;
                break;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FLASH:
            {
                ext_ctrl->value = ov9650->info_priv.flash;
                break;
            }
        default :
            break;
    }
    return 0;
}
static int ov9650_s_ext_control(struct soc_camera_device *icd, struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct ov9650 *ov9650 = to_ov9650(client);
    int val_offset;

    qctrl = soc_camera_find_qctrl(&ov9650_ops, ext_ctrl->id);

    if (!qctrl)
    {
        OV9650_TR("\n OV9650 ioctrl id = %d  is invalidate   ******** ddl *********\n", ext_ctrl->id);
        return -EINVAL;
    }

    switch (ext_ctrl->id)
    {
        case V4L2_CID_SCENE:
            {
                if (ext_ctrl->value != ov9650->info_priv.scene)
                {
                    if (ov9650_set_scene(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    ov9650->info_priv.scene = ext_ctrl->value;
                }
                break;
            }
        case V4L2_CID_EFFECT:
            {
                if (ext_ctrl->value != ov9650->info_priv.effect)
                {
                    if (ov9650_set_effect(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    ov9650->info_priv.effect= ext_ctrl->value;
                }
                break;
            }
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                if (ext_ctrl->value != ov9650->info_priv.digitalzoom)
                {
                    val_offset = ext_ctrl->value -ov9650->info_priv.digitalzoom;

                    if (ov9650_set_digitalzoom(icd, qctrl,&val_offset) != 0)
                        return -EINVAL;
                    ov9650->info_priv.digitalzoom += val_offset;

                    OV9650_DG("ov9650 digitalzoom is %x\n", ov9650->info_priv.digitalzoom);
                }

                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                if (ext_ctrl->value)
                {
                    if (ov9650_set_digitalzoom(icd, qctrl,&ext_ctrl->value) != 0)
                        return -EINVAL;
                    ov9650->info_priv.digitalzoom += ext_ctrl->value;

                    OV9650_DG("ov9650 digitalzoom is %x\n", ov9650->info_priv.digitalzoom);
                }
                break;
            }

        case V4L2_CID_FOCUS_ABSOLUTE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                if (ext_ctrl->value != ov9650->info_priv.focus)
                {
                    val_offset = ext_ctrl->value -ov9650->info_priv.focus;

                    ov9650->info_priv.focus += val_offset;
                }

                break;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
                if (ext_ctrl->value)
                {
                    ov9650->info_priv.focus += ext_ctrl->value;

                    OV9650_DG("ov9650 focus is %x\n", ov9650->info_priv.focus);
                }
                break;
            }

        case V4L2_CID_FLASH:
            {
                ov9650->info_priv.flash = ext_ctrl->value;

                OV9650_DG("ov9650 flash is %x\n", ov9650->info_priv.flash);
                break;
            }
        default:
            break;
    }

    return 0;
}

static int ov9650_g_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = sd->priv;
    struct soc_camera_device *icd = client->dev.platform_data;
    int i, error_cnt=0, error_idx=-1;


    for (i=0; i<ext_ctrl->count; i++) {
        if (ov9650_g_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}

static int ov9650_s_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = sd->priv;
    struct soc_camera_device *icd = client->dev.platform_data;
    int i, error_cnt=0, error_idx=-1;


    for (i=0; i<ext_ctrl->count; i++) {
        if (ov9650_s_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}

/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int ov9650_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
    char pid = 0;
    int ret;
    struct ov9650 *ov9650 = to_ov9650(client);

    /* We must have a parent by now. And it cannot be a wrong one.
     * So this entire test is completely redundant. */
    if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

    /* soft reset */
    ret = ov9650_write(client, 0x12, 0x80);
    if (ret < 0)
    {
        OV9650_TR("soft reset ov9650 failed\n");
        return -ENODEV;
    }
    mdelay(5);         //delay 5 microseconds
    /* check if it is an ov9650 sensor */
    ret = ov9650_read(client, 0x0a, &pid);
    if (ret != 0) {
        OV9650_TR("OV9650 read chip id failed\n");
        ret = -ENODEV;
       goto ov9650_video_probe_err;
    }

    OV9650_DG("\n OV9650   pid = 0x%x\n", pid);
    if (pid == 0x96) {
        ov9650->model = V4L2_IDENT_OV9650;
    } else {
        OV9650_TR("error: devicr mismatched   pid = 0x%x\n", pid);
        ret = -ENODEV;
        goto ov9650_video_probe_err;
    }

    icd->formats = ov9650_colour_formats;
    icd->num_formats = ARRAY_SIZE(ov9650_colour_formats);

    return 0;

ov9650_video_probe_err:

    return ret;
}

static struct v4l2_subdev_core_ops ov9650_subdev_core_ops = {
	.init		= ov9650_init,
	.g_ctrl		= ov9650_g_control,
	.s_ctrl		= ov9650_s_control,
	.g_ext_ctrls          = ov9650_g_ext_controls,
	.s_ext_ctrls          = ov9650_s_ext_controls,
	.g_chip_ident	= ov9650_g_chip_ident,
};

static struct v4l2_subdev_video_ops ov9650_subdev_video_ops = {
	.s_fmt		= ov9650_s_fmt,
	.g_fmt		= ov9650_g_fmt,
	.try_fmt	= ov9650_try_fmt,
};

static struct v4l2_subdev_ops ov9650_subdev_ops = {
	.core	= &ov9650_subdev_core_ops,
	.video = &ov9650_subdev_video_ops,
};

static int ov9650_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
    struct ov9650 *ov9650;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    struct soc_camera_link *icl;
    int ret;

    OV9650_DG("\n%s..%s..%d    ******** ddl *********\n",__FUNCTION__,__FILE__,__LINE__);
    if (!icd) {
        dev_err(&client->dev, "ov9650: missing soc-camera data!\n");
        return -EINVAL;
    }

    icl = to_soc_camera_link(icd);
    if (!icl) {
        dev_err(&client->dev, "ov9650 driver needs platform data\n");
        return -EINVAL;
    }

    if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
        dev_warn(&adapter->dev,
        	 "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
        return -EIO;
    }

    ov9650 = kzalloc(sizeof(struct ov9650), GFP_KERNEL);
    if (!ov9650)
        return -ENOMEM;

    v4l2_i2c_subdev_init(&ov9650->subdev, client, &ov9650_subdev_ops);

    /* Second stage probe - when a capture adapter is there */
    icd->ops		= &ov9650_ops;
    icd->y_skip_top		= 0;


		ret = ov9650_video_probe(icd, client);

    if (ret) {
        icd->ops = NULL;
        i2c_set_clientdata(client, NULL);
        kfree(ov9650);
    }
    OV9650_DG("\n%s..%s..%d  ret = %x  ^^^^^^^^ ddl^^^^^^^^\n",__FUNCTION__,__FILE__,__LINE__,ret);
    return ret;
}

static int ov9650_remove(struct i2c_client *client)
{
    struct ov9650 *ov9650 = to_ov9650(client);
    struct soc_camera_device *icd = client->dev.platform_data;

    icd->ops = NULL;
    i2c_set_clientdata(client, NULL);
    client->driver = NULL;
    kfree(ov9650);

    return 0;
}

static const struct i2c_device_id ov9650_id[] = {
	{ "ov9650", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov9650_id);

static struct i2c_driver ov9650_i2c_driver = {
	.driver = {
		.name = "ov9650",
	},
	.probe		= ov9650_probe,
	.remove		= ov9650_remove,
	.id_table	= ov9650_id,
};

static int __init ov9650_mod_init(void)
{
    OV9650_DG("\n%s..%s..%d    ******** ddl *********\n",__FUNCTION__,__FILE__,__LINE__);
    return i2c_add_driver(&ov9650_i2c_driver);
}

static void __exit ov9650_mod_exit(void)
{
    i2c_del_driver(&ov9650_i2c_driver);
}

//module_init(ov9650_mod_init);
device_initcall_sync(ov9650_mod_init);
module_exit(ov9650_mod_exit);

MODULE_DESCRIPTION("OV9650 Camera sensor driver");
MODULE_AUTHOR("lbt <kernel@rock-chips>");
MODULE_LICENSE("GPL");

