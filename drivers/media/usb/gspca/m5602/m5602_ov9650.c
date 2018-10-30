
/*
 * Driver for the ov9650 sensor
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

#include "m5602_ov9650.h"

static int ov9650_s_ctrl(struct v4l2_ctrl *ctrl);
static void ov9650_dump_registers(struct sd *sd);

static const unsigned char preinit_ov9650[][3] = {
	/* [INITCAM] */
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00},

	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x0a},
	/* Reset chip */
	{SENSOR, OV9650_COM7, OV9650_REGISTER_RESET},
	/* Enable double clock */
	{SENSOR, OV9650_CLKRC, 0x80},
	/* Do something out of spec with the power */
	{SENSOR, OV9650_OFON, 0x40}
};

static const unsigned char init_ov9650[][3] = {
	/* [INITCAM] */
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00},

	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x0a},

	/* Reset chip */
	{SENSOR, OV9650_COM7, OV9650_REGISTER_RESET},
	/* One extra reset is needed in order to make the sensor behave
	   properly when resuming from ram, could be a timing issue */
	{SENSOR, OV9650_COM7, OV9650_REGISTER_RESET},

	/* Enable double clock */
	{SENSOR, OV9650_CLKRC, 0x80},
	/* Do something out of spec with the power */
	{SENSOR, OV9650_OFON, 0x40},

	/* Set fast AGC/AEC algorithm with unlimited step size */
	{SENSOR, OV9650_COM8, OV9650_FAST_AGC_AEC |
			      OV9650_AEC_UNLIM_STEP_SIZE},

	{SENSOR, OV9650_CHLF, 0x10},
	{SENSOR, OV9650_ARBLM, 0xbf},
	{SENSOR, OV9650_ACOM38, 0x81},
	/* Turn off color matrix coefficient double option */
	{SENSOR, OV9650_COM16, 0x00},
	/* Enable color matrix for RGB/YUV, Delay Y channel,
	set output Y/UV delay to 1 */
	{SENSOR, OV9650_COM13, 0x19},
	/* Enable digital BLC, Set output mode to U Y V Y */
	{SENSOR, OV9650_TSLB, 0x0c},
	/* Limit the AGC/AEC stable upper region */
	{SENSOR, OV9650_COM24, 0x00},
	/* Enable HREF and some out of spec things */
	{SENSOR, OV9650_COM12, 0x73},
	/* Set all DBLC offset signs to positive and
	do some out of spec stuff */
	{SENSOR, OV9650_DBLC1, 0xdf},
	{SENSOR, OV9650_COM21, 0x06},
	{SENSOR, OV9650_RSVD35, 0x91},
	/* Necessary, no camera stream without it */
	{SENSOR, OV9650_RSVD16, 0x06},
	{SENSOR, OV9650_RSVD94, 0x99},
	{SENSOR, OV9650_RSVD95, 0x99},
	{SENSOR, OV9650_RSVD96, 0x04},
	/* Enable full range output */
	{SENSOR, OV9650_COM15, 0x0},
	/* Enable HREF at optical black, enable ADBLC bias,
	enable ADBLC, reset timings at format change */
	{SENSOR, OV9650_COM6, 0x4b},
	/* Subtract 32 from the B channel bias */
	{SENSOR, OV9650_BBIAS, 0xa0},
	/* Subtract 32 from the Gb channel bias */
	{SENSOR, OV9650_GbBIAS, 0xa0},
	/* Do not bypass the analog BLC and to some out of spec stuff */
	{SENSOR, OV9650_Gr_COM, 0x00},
	/* Subtract 32 from the R channel bias */
	{SENSOR, OV9650_RBIAS, 0xa0},
	/* Subtract 32 from the R channel bias */
	{SENSOR, OV9650_RBIAS, 0x0},
	{SENSOR, OV9650_COM26, 0x80},
	{SENSOR, OV9650_ACOMA9, 0x98},
	/* Set the AGC/AEC stable region upper limit */
	{SENSOR, OV9650_AEW, 0x68},
	/* Set the AGC/AEC stable region lower limit */
	{SENSOR, OV9650_AEB, 0x5c},
	/* Set the high and low limit nibbles to 3 */
	{SENSOR, OV9650_VPT, 0xc3},
	/* Set the Automatic Gain Ceiling (AGC) to 128x,
	drop VSYNC at frame drop,
	limit exposure timing,
	drop frame when the AEC step is larger than the exposure gap */
	{SENSOR, OV9650_COM9, 0x6e},
	/* Set VSYNC negative, Set RESET to SLHS (slave mode horizontal sync)
	and set PWDN to SLVS (slave mode vertical sync) */
	{SENSOR, OV9650_COM10, 0x42},
	/* Set horizontal column start high to default value */
	{SENSOR, OV9650_HSTART, 0x1a}, /* 210 */
	/* Set horizontal column end */
	{SENSOR, OV9650_HSTOP, 0xbf}, /* 1534 */
	/* Complementing register to the two writes above */
	{SENSOR, OV9650_HREF, 0xb2},
	/* Set vertical row start high bits */
	{SENSOR, OV9650_VSTRT, 0x02},
	/* Set vertical row end low bits */
	{SENSOR, OV9650_VSTOP, 0x7e},
	/* Set complementing vertical frame control */
	{SENSOR, OV9650_VREF, 0x10},
	{SENSOR, OV9650_ADC, 0x04},
	{SENSOR, OV9650_HV, 0x40},

	/* Enable denoise, and white-pixel erase */
	{SENSOR, OV9650_COM22, OV9650_DENOISE_ENABLE |
		 OV9650_WHITE_PIXEL_ENABLE |
		 OV9650_WHITE_PIXEL_OPTION},

	/* Enable VARIOPIXEL */
	{SENSOR, OV9650_COM3, OV9650_VARIOPIXEL},
	{SENSOR, OV9650_COM4, OV9650_QVGA_VARIOPIXEL},

	/* Put the sensor in soft sleep mode */
	{SENSOR, OV9650_COM2, OV9650_SOFT_SLEEP | OV9650_OUTPUT_DRIVE_2X},
};

static const unsigned char res_init_ov9650[][3] = {
	{SENSOR, OV9650_COM2, OV9650_OUTPUT_DRIVE_2X},

	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x82},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_L, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82},
	{BRIDGE, M5602_XB_PIX_OF_LINE_L, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01}
};

/* Vertically and horizontally flips the image if matched, needed for machines
   where the sensor is mounted upside down */
static
    const
	struct dmi_system_id ov9650_flip_dmi_table[] = {
	{
		.ident = "ASUS A6Ja",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "A6J")
		}
	},
	{
		.ident = "ASUS A6JC",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "A6JC")
		}
	},
	{
		.ident = "ASUS A6K",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "A6K")
		}
	},
	{
		.ident = "ASUS A6Kt",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "A6Kt")
		}
	},
	{
		.ident = "ASUS A6VA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "A6VA")
		}
	},
	{

		.ident = "ASUS A6VC",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "A6VC")
		}
	},
	{
		.ident = "ASUS A6VM",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "A6VM")
		}
	},
	{
		.ident = "ASUS A7V",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "A7V")
		}
	},
	{
		.ident = "Alienware Aurora m9700",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "alienware"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aurora m9700")
		}
	},
	{}
};

static struct v4l2_pix_format ov9650_modes[] = {
	{
		176,
		144,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage =
			176 * 144,
		.bytesperline = 176,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 9
	}, {
		320,
		240,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage =
			320 * 240,
		.bytesperline = 320,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 8
	}, {
		352,
		288,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage =
			352 * 288,
		.bytesperline = 352,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 9
	}, {
		640,
		480,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage =
			640 * 480,
		.bytesperline = 640,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 9
	}
};

static const struct v4l2_ctrl_ops ov9650_ctrl_ops = {
	.s_ctrl = ov9650_s_ctrl,
};

int ov9650_probe(struct sd *sd)
{
	int err = 0;
	u8 prod_id = 0, ver_id = 0, i;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	if (force_sensor) {
		if (force_sensor == OV9650_SENSOR) {
			pr_info("Forcing an %s sensor\n", ov9650.name);
			goto sensor_found;
		}
		/* If we want to force another sensor,
		   don't try to probe this one */
		return -ENODEV;
	}

	gspca_dbg(gspca_dev, D_PROBE, "Probing for an ov9650 sensor\n");

	/* Run the pre-init before probing the sensor */
	for (i = 0; i < ARRAY_SIZE(preinit_ov9650) && !err; i++) {
		u8 data = preinit_ov9650[i][2];
		if (preinit_ov9650[i][0] == SENSOR)
			err = m5602_write_sensor(sd,
				preinit_ov9650[i][1], &data, 1);
		else
			err = m5602_write_bridge(sd,
				preinit_ov9650[i][1], data);
	}

	if (err < 0)
		return err;

	if (m5602_read_sensor(sd, OV9650_PID, &prod_id, 1))
		return -ENODEV;

	if (m5602_read_sensor(sd, OV9650_VER, &ver_id, 1))
		return -ENODEV;

	if ((prod_id == 0x96) && (ver_id == 0x52)) {
		pr_info("Detected an ov9650 sensor\n");
		goto sensor_found;
	}
	return -ENODEV;

sensor_found:
	sd->gspca_dev.cam.cam_mode = ov9650_modes;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(ov9650_modes);

	return 0;
}

int ov9650_init(struct sd *sd)
{
	int i, err = 0;
	u8 data;

	if (dump_sensor)
		ov9650_dump_registers(sd);

	for (i = 0; i < ARRAY_SIZE(init_ov9650) && !err; i++) {
		data = init_ov9650[i][2];
		if (init_ov9650[i][0] == SENSOR)
			err = m5602_write_sensor(sd, init_ov9650[i][1],
						  &data, 1);
		else
			err = m5602_write_bridge(sd, init_ov9650[i][1], data);
	}

	return 0;
}

int ov9650_init_controls(struct sd *sd)
{
	struct v4l2_ctrl_handler *hdl = &sd->gspca_dev.ctrl_handler;

	sd->gspca_dev.vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 9);

	sd->auto_white_bal = v4l2_ctrl_new_std(hdl, &ov9650_ctrl_ops,
					       V4L2_CID_AUTO_WHITE_BALANCE,
					       0, 1, 1, 1);
	sd->red_bal = v4l2_ctrl_new_std(hdl, &ov9650_ctrl_ops,
					V4L2_CID_RED_BALANCE, 0, 255, 1,
					RED_GAIN_DEFAULT);
	sd->blue_bal = v4l2_ctrl_new_std(hdl, &ov9650_ctrl_ops,
					V4L2_CID_BLUE_BALANCE, 0, 255, 1,
					BLUE_GAIN_DEFAULT);

	sd->autoexpo = v4l2_ctrl_new_std_menu(hdl, &ov9650_ctrl_ops,
			  V4L2_CID_EXPOSURE_AUTO, 1, 0, V4L2_EXPOSURE_AUTO);
	sd->expo = v4l2_ctrl_new_std(hdl, &ov9650_ctrl_ops, V4L2_CID_EXPOSURE,
			  0, 0x1ff, 4, EXPOSURE_DEFAULT);

	sd->autogain = v4l2_ctrl_new_std(hdl, &ov9650_ctrl_ops,
					 V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	sd->gain = v4l2_ctrl_new_std(hdl, &ov9650_ctrl_ops, V4L2_CID_GAIN, 0,
				     0x3ff, 1, GAIN_DEFAULT);

	sd->hflip = v4l2_ctrl_new_std(hdl, &ov9650_ctrl_ops, V4L2_CID_HFLIP,
				      0, 1, 1, 0);
	sd->vflip = v4l2_ctrl_new_std(hdl, &ov9650_ctrl_ops, V4L2_CID_VFLIP,
				      0, 1, 1, 0);

	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}

	v4l2_ctrl_auto_cluster(3, &sd->auto_white_bal, 0, false);
	v4l2_ctrl_auto_cluster(2, &sd->autoexpo, 0, false);
	v4l2_ctrl_auto_cluster(2, &sd->autogain, 0, false);
	v4l2_ctrl_cluster(2, &sd->hflip);

	return 0;
}

int ov9650_start(struct sd *sd)
{
	u8 data;
	int i, err = 0;
	struct cam *cam = &sd->gspca_dev.cam;

	int width = cam->cam_mode[sd->gspca_dev.curr_mode].width;
	int height = cam->cam_mode[sd->gspca_dev.curr_mode].height;
	int ver_offs = cam->cam_mode[sd->gspca_dev.curr_mode].priv;
	int hor_offs = OV9650_LEFT_OFFSET;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	if ((!dmi_check_system(ov9650_flip_dmi_table) &&
		sd->vflip->val) ||
		(dmi_check_system(ov9650_flip_dmi_table) &&
		!sd->vflip->val))
		ver_offs--;

	if (width <= 320)
		hor_offs /= 2;

	/* Synthesize the vsync/hsync setup */
	for (i = 0; i < ARRAY_SIZE(res_init_ov9650) && !err; i++) {
		if (res_init_ov9650[i][0] == BRIDGE)
			err = m5602_write_bridge(sd, res_init_ov9650[i][1],
				res_init_ov9650[i][2]);
		else if (res_init_ov9650[i][0] == SENSOR) {
			data = res_init_ov9650[i][2];
			err = m5602_write_sensor(sd,
				res_init_ov9650[i][1], &data, 1);
		}
	}
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA,
				 ((ver_offs >> 8) & 0xff));
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, (ver_offs & 0xff));
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_VSYNC_PARA, 0);
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

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA,
				 (hor_offs >> 8) & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA, hor_offs & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA,
				 ((width + hor_offs) >> 8) & 0xff);
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_HSYNC_PARA,
				 ((width + hor_offs) & 0xff));
	if (err < 0)
		return err;

	err = m5602_write_bridge(sd, M5602_XB_SIG_INI, 0);
	if (err < 0)
		return err;

	switch (width) {
	case 640:
		gspca_dbg(gspca_dev, D_CONF, "Configuring camera for VGA mode\n");

		data = OV9650_VGA_SELECT | OV9650_RGB_SELECT |
		       OV9650_RAW_RGB_SELECT;
		err = m5602_write_sensor(sd, OV9650_COM7, &data, 1);
		break;

	case 352:
		gspca_dbg(gspca_dev, D_CONF, "Configuring camera for CIF mode\n");

		data = OV9650_CIF_SELECT | OV9650_RGB_SELECT |
				OV9650_RAW_RGB_SELECT;
		err = m5602_write_sensor(sd, OV9650_COM7, &data, 1);
		break;

	case 320:
		gspca_dbg(gspca_dev, D_CONF, "Configuring camera for QVGA mode\n");

		data = OV9650_QVGA_SELECT | OV9650_RGB_SELECT |
				OV9650_RAW_RGB_SELECT;
		err = m5602_write_sensor(sd, OV9650_COM7, &data, 1);
		break;

	case 176:
		gspca_dbg(gspca_dev, D_CONF, "Configuring camera for QCIF mode\n");

		data = OV9650_QCIF_SELECT | OV9650_RGB_SELECT |
			OV9650_RAW_RGB_SELECT;
		err = m5602_write_sensor(sd, OV9650_COM7, &data, 1);
		break;
	}
	return err;
}

int ov9650_stop(struct sd *sd)
{
	u8 data = OV9650_SOFT_SLEEP | OV9650_OUTPUT_DRIVE_2X;
	return m5602_write_sensor(sd, OV9650_COM2, &data, 1);
}

void ov9650_disconnect(struct sd *sd)
{
	ov9650_stop(sd);

	sd->sensor = NULL;
}

static int ov9650_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 i2c_data;
	int err;

	gspca_dbg(gspca_dev, D_CONF, "Set exposure to %d\n", val);

	/* The 6 MSBs */
	i2c_data = (val >> 10) & 0x3f;
	err = m5602_write_sensor(sd, OV9650_AECHM,
				  &i2c_data, 1);
	if (err < 0)
		return err;

	/* The 8 middle bits */
	i2c_data = (val >> 2) & 0xff;
	err = m5602_write_sensor(sd, OV9650_AECH,
				  &i2c_data, 1);
	if (err < 0)
		return err;

	/* The 2 LSBs */
	i2c_data = val & 0x03;
	err = m5602_write_sensor(sd, OV9650_COM1, &i2c_data, 1);
	return err;
}

static int ov9650_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_CONF, "Setting gain to %d\n", val);

	/* The 2 MSB */
	/* Read the OV9650_VREF register first to avoid
	   corrupting the VREF high and low bits */
	err = m5602_read_sensor(sd, OV9650_VREF, &i2c_data, 1);
	if (err < 0)
		return err;

	/* Mask away all uninteresting bits */
	i2c_data = ((val & 0x0300) >> 2) |
			(i2c_data & 0x3f);
	err = m5602_write_sensor(sd, OV9650_VREF, &i2c_data, 1);
	if (err < 0)
		return err;

	/* The 8 LSBs */
	i2c_data = val & 0xff;
	err = m5602_write_sensor(sd, OV9650_GAIN, &i2c_data, 1);
	return err;
}

static int ov9650_set_red_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_CONF, "Set red gain to %d\n", val);

	i2c_data = val & 0xff;
	err = m5602_write_sensor(sd, OV9650_RED, &i2c_data, 1);
	return err;
}

static int ov9650_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_CONF, "Set blue gain to %d\n", val);

	i2c_data = val & 0xff;
	err = m5602_write_sensor(sd, OV9650_BLUE, &i2c_data, 1);
	return err;
}

static int ov9650_set_hvflip(struct gspca_dev *gspca_dev)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;
	int hflip = sd->hflip->val;
	int vflip = sd->vflip->val;

	gspca_dbg(gspca_dev, D_CONF, "Set hvflip to %d %d\n", hflip, vflip);

	if (dmi_check_system(ov9650_flip_dmi_table))
		vflip = !vflip;

	i2c_data = (hflip << 5) | (vflip << 4);
	err = m5602_write_sensor(sd, OV9650_MVFP, &i2c_data, 1);
	if (err < 0)
		return err;

	/* When vflip is toggled we need to readjust the bridge hsync/vsync */
	if (gspca_dev->streaming)
		err = ov9650_start(sd);

	return err;
}

static int ov9650_set_auto_exposure(struct gspca_dev *gspca_dev,
				    __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_CONF, "Set auto exposure control to %d\n", val);

	err = m5602_read_sensor(sd, OV9650_COM8, &i2c_data, 1);
	if (err < 0)
		return err;

	val = (val == V4L2_EXPOSURE_AUTO);
	i2c_data = ((i2c_data & 0xfe) | ((val & 0x01) << 0));

	return m5602_write_sensor(sd, OV9650_COM8, &i2c_data, 1);
}

static int ov9650_set_auto_white_balance(struct gspca_dev *gspca_dev,
					 __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_CONF, "Set auto white balance to %d\n", val);

	err = m5602_read_sensor(sd, OV9650_COM8, &i2c_data, 1);
	if (err < 0)
		return err;

	i2c_data = ((i2c_data & 0xfd) | ((val & 0x01) << 1));
	err = m5602_write_sensor(sd, OV9650_COM8, &i2c_data, 1);

	return err;
}

static int ov9650_set_auto_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	int err;
	u8 i2c_data;
	struct sd *sd = (struct sd *) gspca_dev;

	gspca_dbg(gspca_dev, D_CONF, "Set auto gain control to %d\n", val);

	err = m5602_read_sensor(sd, OV9650_COM8, &i2c_data, 1);
	if (err < 0)
		return err;

	i2c_data = ((i2c_data & 0xfb) | ((val & 0x01) << 2));

	return m5602_write_sensor(sd, OV9650_COM8, &i2c_data, 1);
}

static int ov9650_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	struct sd *sd = (struct sd *) gspca_dev;
	int err;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		err = ov9650_set_auto_white_balance(gspca_dev, ctrl->val);
		if (err || ctrl->val)
			return err;
		err = ov9650_set_red_balance(gspca_dev, sd->red_bal->val);
		if (err)
			return err;
		err = ov9650_set_blue_balance(gspca_dev, sd->blue_bal->val);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		err = ov9650_set_auto_exposure(gspca_dev, ctrl->val);
		if (err || ctrl->val == V4L2_EXPOSURE_AUTO)
			return err;
		err = ov9650_set_exposure(gspca_dev, sd->expo->val);
		break;
	case V4L2_CID_AUTOGAIN:
		err = ov9650_set_auto_gain(gspca_dev, ctrl->val);
		if (err || ctrl->val)
			return err;
		err = ov9650_set_gain(gspca_dev, sd->gain->val);
		break;
	case V4L2_CID_HFLIP:
		err = ov9650_set_hvflip(gspca_dev);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

static void ov9650_dump_registers(struct sd *sd)
{
	int address;
	pr_info("Dumping the ov9650 register state\n");
	for (address = 0; address < 0xa9; address++) {
		u8 value;
		m5602_read_sensor(sd, address, &value, 1);
		pr_info("register 0x%x contains 0x%x\n", address, value);
	}

	pr_info("ov9650 register state dump complete\n");

	pr_info("Probing for which registers that are read/write\n");
	for (address = 0; address < 0xff; address++) {
		u8 old_value, ctrl_value;
		u8 test_value[2] = {0xff, 0xff};

		m5602_read_sensor(sd, address, &old_value, 1);
		m5602_write_sensor(sd, address, test_value, 1);
		m5602_read_sensor(sd, address, &ctrl_value, 1);

		if (ctrl_value == test_value[0])
			pr_info("register 0x%x is writeable\n", address);
		else
			pr_info("register 0x%x is read only\n", address);

		/* Restore original value */
		m5602_write_sensor(sd, address, &old_value, 1);
	}
}
