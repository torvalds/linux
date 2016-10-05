/*
 * Driver for the mt9m111 sensor
 *
 * Copyright (C) 2008 Erik Andrén
 * Copyright (C) 2007 Ilyes Gouta. Based on the m5603x Linux Driver Project.
 * Copyright (C) 2005 m5603x Linux Driver Project <m5602@x3ng.com.br>
 *
 * Portions of code to USB interface and ALi driver software,
 * Copyright (c) 2006 Willem Duinker
 * v4l2 interface modeled after the V4L2 driver
 * for SN9C10x PC Camera Controllers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "m5602_mt9m111.h"

static int mt9m111_s_ctrl(struct v4l2_ctrl *ctrl);
static void mt9m111_dump_registers(struct sd *sd);

static const unsigned char preinit_mt9m111[][4] = {
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0d, 0x00},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},

	{SENSOR, MT9M111_PAGE_MAP, 0x00, 0x00},
	{SENSOR, MT9M111_SC_RESET,
		MT9M111_RESET |
		MT9M111_RESTART |
		MT9M111_ANALOG_STANDBY |
		MT9M111_CHIP_DISABLE,
		MT9M111_SHOW_BAD_FRAMES |
		MT9M111_RESTART_BAD_FRAMES |
		MT9M111_SYNCHRONIZE_CHANGES},

	{BRIDGE, M5602_XB_GPIO_DIR, 0x05, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3e, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3e, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x02, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},

	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x07, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x0b, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},

	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x0a, 0x00}
};

static const unsigned char init_mt9m111[][4] = {
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},

	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3e, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x02, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x07, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x0b, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x0a, 0x00},

	{SENSOR, MT9M111_SC_RESET, 0x00, 0x29},
	{SENSOR, MT9M111_PAGE_MAP, 0x00, 0x00},
	{SENSOR, MT9M111_SC_RESET, 0x00, 0x08},
	{SENSOR, MT9M111_PAGE_MAP, 0x00, 0x01},
	{SENSOR, MT9M111_CP_OPERATING_MODE_CTL, 0x00,
			MT9M111_CP_OPERATING_MODE_CTL},
	{SENSOR, MT9M111_CP_LENS_CORRECTION_1, 0x04, 0x2a},
	{SENSOR, MT9M111_CP_DEFECT_CORR_CONTEXT_A, 0x00,
				MT9M111_2D_DEFECT_CORRECTION_ENABLE},
	{SENSOR, MT9M111_CP_DEFECT_CORR_CONTEXT_B, 0x00,
				MT9M111_2D_DEFECT_CORRECTION_ENABLE},
	{SENSOR, MT9M111_CP_LUMA_OFFSET, 0x00, 0x00},
	{SENSOR, MT9M111_CP_LUMA_CLIP, 0xff, 0x00},
	{SENSOR, MT9M111_CP_OUTPUT_FORMAT_CTL2_CONTEXT_A, 0x14, 0x00},
	{SENSOR, MT9M111_CP_OUTPUT_FORMAT_CTL2_CONTEXT_B, 0x14, 0x00},
	{SENSOR, 0xcd, 0x00, 0x0e},
	{SENSOR, 0xd0, 0x00, 0x40},

	{SENSOR, MT9M111_PAGE_MAP, 0x00, 0x02},
	{SENSOR, MT9M111_CC_AUTO_EXPOSURE_PARAMETER_18, 0x00, 0x00},
	{SENSOR, MT9M111_CC_AWB_PARAMETER_7, 0xef, 0x03},

	{SENSOR, MT9M111_PAGE_MAP, 0x00, 0x00},
	{SENSOR, 0x33, 0x03, 0x49},
	{SENSOR, 0x34, 0xc0, 0x19},
	{SENSOR, 0x3f, 0x20, 0x20},
	{SENSOR, 0x40, 0x20, 0x20},
	{SENSOR, 0x5a, 0xc0, 0x0a},
	{SENSOR, 0x70, 0x7b, 0x0a},
	{SENSOR, 0x71, 0xff, 0x00},
	{SENSOR, 0x72, 0x19, 0x0e},
	{SENSOR, 0x73, 0x18, 0x0f},
	{SENSOR, 0x74, 0x57, 0x32},
	{SENSOR, 0x75, 0x56, 0x34},
	{SENSOR, 0x76, 0x73, 0x35},
	{SENSOR, 0x77, 0x30, 0x12},
	{SENSOR, 0x78, 0x79, 0x02},
	{SENSOR, 0x79, 0x75, 0x06},
	{SENSOR, 0x7a, 0x77, 0x0a},
	{SENSOR, 0x7b, 0x78, 0x09},
	{SENSOR, 0x7c, 0x7d, 0x06},
	{SENSOR, 0x7d, 0x31, 0x10},
	{SENSOR, 0x7e, 0x00, 0x7e},
	{SENSOR, 0x80, 0x59, 0x04},
	{SENSOR, 0x81, 0x59, 0x04},
	{SENSOR, 0x82, 0x57, 0x0a},
	{SENSOR, 0x83, 0x58, 0x0b},
	{SENSOR, 0x84, 0x47, 0x0c},
	{SENSOR, 0x85, 0x48, 0x0e},
	{SENSOR, 0x86, 0x5b, 0x02},
	{SENSOR, 0x87, 0x00, 0x5c},
	{SENSOR, MT9M111_CONTEXT_CONTROL, 0x00, MT9M111_SEL_CONTEXT_B},
	{SENSOR, 0x60, 0x00, 0x80},
	{SENSOR, 0x61, 0x00, 0x00},
	{SENSOR, 0x62, 0x00, 0x00},
	{SENSOR, 0x63, 0x00, 0x00},
	{SENSOR, 0x64, 0x00, 0x00},

	{SENSOR, MT9M111_SC_ROWSTART, 0x00, 0x0d}, /* 13 */
	{SENSOR, MT9M111_SC_COLSTART, 0x00, 0x12}, /* 18 */
	{SENSOR, MT9M111_SC_WINDOW_HEIGHT, 0x04, 0x00}, /* 1024 */
	{SENSOR, MT9M111_SC_WINDOW_WIDTH, 0x05, 0x10}, /* 1296 */
	{SENSOR, MT9M111_SC_HBLANK_CONTEXT_B, 0x01, 0x60}, /* 352 */
	{SENSOR, MT9M111_SC_VBLANK_CONTEXT_B, 0x00, 0x11}, /* 17 */
	{SENSOR, MT9M111_SC_HBLANK_CONTEXT_A, 0x01, 0x60}, /* 352 */
	{SENSOR, MT9M111_SC_VBLANK_CONTEXT_A, 0x00, 0x11}, /* 17 */
	{SENSOR, MT9M111_SC_R_MODE_CONTEXT_A, 0x01, 0x0f}, /* 271 */
	{SENSOR, 0x30, 0x04, 0x00},
	/* Set number of blank rows chosen to 400 */
	{SENSOR, MT9M111_SC_SHUTTER_WIDTH, 0x01, 0x90},
};

static const unsigned char start_mt9m111[][4] = {
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
};

static struct v4l2_pix_format mt9m111_modes[] = {
	{
		640,
		480,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage = 640 * 480,
		.bytesperline = 640,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	}
};

static const struct v4l2_ctrl_ops mt9m111_ctrl_ops = {
	.s_ctrl = mt9m111_s_ctrl,
};

static const struct v4l2_ctrl_config mt9m111_greenbal_cfg = {
	.ops	= &mt9m111_ctrl_ops,
	.id	= M5602_V4L2_CID_GREEN_BALANCE,
	.name	= "Green Balance",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= 0x7ff,
	.step	= 1,
	.def	= MT9M111_GREEN_GAIN_DEFAULT,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

int mt9m111_probe(struct sd *sd)
{
	u8 data[2] = {0x00, 0x00};
	int i;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	if (force_sensor) {
		if (force_sensor == MT9M111_SENSOR) {
			pr_info("Forcing a %s sensor\n", mt9m111.name);
			goto sensor_found;
		}
		/* If we want to force another sensor, don't try to probe this
		 * one */
		return -ENODEV;
	}

	PDEBUG(D_PROBE, "Probing for a mt9m111 sensor");

	/* Do the preinit */
	for (i = 0; i < ARRAY_SIZE(preinit_mt9m111); i++) {
		if (preinit_mt9m111[i][0] == BRIDGE) {
			m5602_write_bridge(sd,
				preinit_mt9m111[i][1],
				preinit_mt9m111[i][2]);
		} else {
			data[0] = preinit_mt9m111[i][2];
			data[1] = preinit_mt9m111[i][3];
			m5602_write_sensor(sd,
				preinit_mt9m111[i][1], data, 2);
		}
	}

	if (m5602_read_sensor(sd, MT9M111_SC_CHIPVER, data, 2))
		return -ENODEV;

	if ((data[0] == 0x14) && (data[1] == 0x3a)) {
		pr_info("Detected a mt9m111 sensor\n");
		goto sensor_found;
	}

	return -ENODEV;

sensor_found:
	sd->gspca_dev.cam.cam_mode = mt9m111_modes;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(mt9m111_modes);

	return 0;
}

int mt9m111_init(struct sd *sd)
{
	int i, err = 0;

	/* Init the sensor */
	for (i = 0; i < ARRAY_SIZE(init_mt9m111) && !err; i++) {
		u8 data[2];

		if (init_mt9m111[i][0] == BRIDGE) {
			err = m5602_write_bridge(sd,
				init_mt9m111[i][1],
				init_mt9m111[i][2]);
		} else {
			data[0] = init_mt9m111[i][2];
			data[1] = init_mt9m111[i][3];
			err = m5602_write_sensor(sd,
				init_mt9m111[i][1], data, 2);
		}
	}

	if (dump_sensor)
		mt9m111_dump_registers(sd);

	return 0;
}

int mt9m111_init_controls(struct sd *sd)
{
	struct v4l2_ctrl_handler *hdl = &sd->gspca_dev.ctrl_handler;

	sd->gspca_dev.vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 7);

	sd->auto_white_bal = v4l2_ctrl_new_std(hdl, &mt9m111_ctrl_ops,
					       V4L2_CID_AUTO_WHITE_BALANCE,
					       0, 1, 1, 0);
	sd->green_bal = v4l2_ctrl_new_custom(hdl, &mt9m111_greenbal_cfg, NULL);
	sd->red_bal = v4l2_ctrl_new_std(hdl, &mt9m111_ctrl_ops,
					V4L2_CID_RED_BALANCE, 0, 0x7ff, 1,
					MT9M111_RED_GAIN_DEFAULT);
	sd->blue_bal = v4l2_ctrl_new_std(hdl, &mt9m111_ctrl_ops,
					V4L2_CID_BLUE_BALANCE, 0, 0x7ff, 1,
					MT9M111_BLUE_GAIN_DEFAULT);

	v4l2_ctrl_new_std(hdl, &mt9m111_ctrl_ops, V4L2_CID_GAIN, 0,
			  (INITIAL_MAX_GAIN - 1) * 2 * 2 * 2, 1,
			  MT9M111_DEFAULT_GAIN);

	sd->hflip = v4l2_ctrl_new_std(hdl, &mt9m111_ctrl_ops, V4L2_CID_HFLIP,
				      0, 1, 1, 0);
	sd->vflip = v4l2_ctrl_new_std(hdl, &mt9m111_ctrl_ops, V4L2_CID_VFLIP,
				      0, 1, 1, 0);

	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}

	v4l2_ctrl_auto_cluster(4, &sd->auto_white_bal, 0, false);
	v4l2_ctrl_cluster(2, &sd->hflip);

	return 0;
}

int mt9m111_start(struct sd *sd)
{
	int i, err = 0;
	u8 data[2];
	struct cam *cam = &sd->gspca_dev.cam;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	int width = cam->cam_mode[sd->gspca_dev.curr_mode].width - 1;
	int height = cam->cam_mode[sd->gspca_dev.curr_mode].height;

	for (i = 0; i < ARRAY_SIZE(start_mt9m111) && !err; i++) {
		if (start_mt9m111[i][0] == BRIDGE) {
			err = m5602_write_bridge(sd,
				start_mt9m111[i][1],
				start_mt9m111[i][2]);
		} else {
			data[0] = start_mt9m111[i][2];
			data[1] = start_mt9m111[i][3];
			err = m5602_write_sensor(sd,
				start_mt9m111[i][1], data, 2);
		}
	}
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, (height >> 8) & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, (height & 0xff));
	if (err < 0)
		return err;

	for (i = 0; i < 2 && !err; i++)
		err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, 0);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 0);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 2);
	if (err < 0)
		return err;

	for (i = 0; i < 2 && !err; i++)
		err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA, 0);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA,
				 (width >> 8) & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA, width & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 0);
	if (err < 0)
		return err;

	switch (width) {
	case 640:
		PDEBUG(D_CONF, "Configuring camera for VGA mode");
		break;

	case 320:
		PDEBUG(D_CONF, "Configuring camera for QVGA mode");
		break;
	}
	return err;
}

void mt9m111_disconnect(struct sd *sd)
{
	sd->sensor = NULL;
}

static int mt9m111_set_hvflip(struct gspca_dev *gspca_dev)
{
	int err;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;
	int hflip;
	int vflip;

	PDEBUG(D_CONF, "Set hvflip to %d %d", sd->hflip->val, sd->vflip->val);

	/* The mt9m111 is flipped by default */
	hflip = !sd->hflip->val;
	vflip = !sd->vflip->val;

	/* Set the correct page map */
	err = m5602_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		return err;

	data[0] = MT9M111_RMB_OVER_SIZED;
	if (gspca_dev->pixfmt.width == 640) {
		data[1] = MT9M111_RMB_ROW_SKIP_2X |
			  MT9M111_RMB_COLUMN_SKIP_2X |
			  (hflip << 1) | vflip;
	} else {
		data[1] = MT9M111_RMB_ROW_SKIP_4X |
			  MT9M111_RMB_COLUMN_SKIP_4X |
			  (hflip << 1) | vflip;
	}
	err = m5602_write_sensor(sd, MT9M111_SC_R_MODE_CONTEXT_B,
					data, 2);
	return err;
}

static int mt9m111_set_auto_white_balance(struct gspca_dev *gspca_dev,
					  __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int err;
	u8 data[2];

	err = m5602_read_sensor(sd, MT9M111_CP_OPERATING_MODE_CTL, data, 2);
	if (err < 0)
		return err;

	data[1] = ((data[1] & 0xfd) | ((val & 0x01) << 1));

	err = m5602_write_sensor(sd, MT9M111_CP_OPERATING_MODE_CTL, data, 2);

	PDEBUG(D_CONF, "Set auto white balance %d", val);
	return err;
}

static int mt9m111_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err, tmp;
	u8 data[2] = {0x00, 0x00};
	struct sd *sd = (struct sd *) gspca_dev;

	/* Set the correct page map */
	err = m5602_write_sensor(sd, MT9M111_PAGE_MAP, data, 2);
	if (err < 0)
		return err;

	if (val >= INITIAL_MAX_GAIN * 2 * 2 * 2)
		return -EINVAL;

	if ((val >= INITIAL_MAX_GAIN * 2 * 2) &&
	    (val < (INITIAL_MAX_GAIN - 1) * 2 * 2 * 2))
		tmp = (1 << 10) | (val << 9) |
				(val << 8) | (val / 8);
	else if ((val >= INITIAL_MAX_GAIN * 2) &&
		 (val <  INITIAL_MAX_GAIN * 2 * 2))
		tmp = (1 << 9) | (1 << 8) | (val / 4);
	else if ((val >= INITIAL_MAX_GAIN) &&
		 (val < INITIAL_MAX_GAIN * 2))
		tmp = (1 << 8) | (val / 2);
	else
		tmp = val;

	data[1] = (tmp & 0xff);
	data[0] = (tmp & 0xff00) >> 8;
	PDEBUG(D_CONF, "tmp=%d, data[1]=%d, data[0]=%d", tmp,
	       data[1], data[0]);

	err = m5602_write_sensor(sd, MT9M111_SC_GLOBAL_GAIN,
				   data, 2);

	return err;
}

static int mt9m111_set_green_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;

	data[1] = (val & 0xff);
	data[0] = (val & 0xff00) >> 8;

	PDEBUG(D_CONF, "Set green balance %d", val);
	err = m5602_write_sensor(sd, MT9M111_SC_GREEN_1_GAIN,
				 data, 2);
	if (err < 0)
		return err;

	return m5602_write_sensor(sd, MT9M111_SC_GREEN_2_GAIN,
				  data, 2);
}

static int mt9m111_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;

	data[1] = (val & 0xff);
	data[0] = (val & 0xff00) >> 8;

	PDEBUG(D_CONF, "Set blue balance %d", val);

	return m5602_write_sensor(sd, MT9M111_SC_BLUE_GAIN,
				  data, 2);
}

static int mt9m111_set_red_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	u8 data[2];
	struct sd *sd = (struct sd *) gspca_dev;

	data[1] = (val & 0xff);
	data[0] = (val & 0xff00) >> 8;

	PDEBUG(D_CONF, "Set red balance %d", val);

	return m5602_write_sensor(sd, MT9M111_SC_RED_GAIN,
				  data, 2);
}

static int mt9m111_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *) gspca_dev;
	int err;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		err = mt9m111_set_auto_white_balance(gspca_dev, ctrl->val);
		if (err || ctrl->val)
			return err;
		err = mt9m111_set_green_balance(gspca_dev, sd->green_bal->val);
		if (err)
			return err;
		err = mt9m111_set_red_balance(gspca_dev, sd->red_bal->val);
		if (err)
			return err;
		err = mt9m111_set_blue_balance(gspca_dev, sd->blue_bal->val);
		break;
	case V4L2_CID_GAIN:
		err = mt9m111_set_gain(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		err = mt9m111_set_hvflip(gspca_dev);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

static void mt9m111_dump_registers(struct sd *sd)
{
	u8 address, value[2] = {0x00, 0x00};

	pr_info("Dumping the mt9m111 register state\n");

	pr_info("Dumping the mt9m111 sensor core registers\n");
	value[1] = MT9M111_SENSOR_CORE;
	m5602_write_sensor(sd, MT9M111_PAGE_MAP, value, 2);
	for (address = 0; address < 0xff; address++) {
		m5602_read_sensor(sd, address, value, 2);
		pr_info("register 0x%x contains 0x%x%x\n",
			address, value[0], value[1]);
	}

	pr_info("Dumping the mt9m111 color pipeline registers\n");
	value[1] = MT9M111_COLORPIPE;
	m5602_write_sensor(sd, MT9M111_PAGE_MAP, value, 2);
	for (address = 0; address < 0xff; address++) {
		m5602_read_sensor(sd, address, value, 2);
		pr_info("register 0x%x contains 0x%x%x\n",
			address, value[0], value[1]);
	}

	pr_info("Dumping the mt9m111 camera control registers\n");
	value[1] = MT9M111_CAMERA_CONTROL;
	m5602_write_sensor(sd, MT9M111_PAGE_MAP, value, 2);
	for (address = 0; address < 0xff; address++) {
		m5602_read_sensor(sd, address, value, 2);
		pr_info("register 0x%x contains 0x%x%x\n",
			address, value[0], value[1]);
	}

	pr_info("mt9m111 register state dump complete\n");
}
