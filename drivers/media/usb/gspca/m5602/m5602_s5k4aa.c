/*
 * Driver for the s5k4aa sensor
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

#include "m5602_s5k4aa.h"

static const unsigned char preinit_s5k4aa[][4] = {
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0d, 0x00},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00, 0x00},

	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0x80, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08, 0x00},

	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x14, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xf0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x1c, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x20, 0x00},

	{SENSOR, S5K4AA_PAGE_MAP, 0x00, 0x00}
};

static const unsigned char init_s5k4aa[][4] = {
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0d, 0x00},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00, 0x00},

	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0x80, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08, 0x00},

	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x14, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xf0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x1c, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x20, 0x00},

	{SENSOR, S5K4AA_PAGE_MAP, 0x07, 0x00},
	{SENSOR, 0x36, 0x01, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x00, 0x00},
	{SENSOR, 0x7b, 0xff, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, 0x0c, 0x05, 0x00},
	{SENSOR, 0x02, 0x0e, 0x00},
	{SENSOR, S5K4AA_READ_MODE, 0xa0, 0x00},
	{SENSOR, 0x37, 0x00, 0x00},
};

static const unsigned char VGA_s5k4aa[][4] = {
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	/* VSYNC_PARA, VSYNC_PARA : img height 480 = 0x01e0 */
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xe0, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	/* HSYNC_PARA, HSYNC_PARA : img width 640 = 0x0280 */
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x80, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xa0, 0x00}, /* 48 MHz */

	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, S5K4AA_READ_MODE, S5K4AA_RM_H_FLIP | S5K4AA_RM_ROW_SKIP_2X
		| S5K4AA_RM_COL_SKIP_2X, 0x00},
	/* 0x37 : Fix image stability when light is too bright and improves
	 * image quality in 640x480, but worsens it in 1280x1024 */
	{SENSOR, 0x37, 0x01, 0x00},
	/* ROWSTART_HI, ROWSTART_LO : 10 + (1024-960)/2 = 42 = 0x002a */
	{SENSOR, S5K4AA_ROWSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_ROWSTART_LO, 0x29, 0x00},
	{SENSOR, S5K4AA_COLSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_COLSTART_LO, 0x0c, 0x00},
	/* window_height_hi, window_height_lo : 960 = 0x03c0 */
	{SENSOR, S5K4AA_WINDOW_HEIGHT_HI, 0x03, 0x00},
	{SENSOR, S5K4AA_WINDOW_HEIGHT_LO, 0xc0, 0x00},
	/* window_width_hi, window_width_lo : 1280 = 0x0500 */
	{SENSOR, S5K4AA_WINDOW_WIDTH_HI, 0x05, 0x00},
	{SENSOR, S5K4AA_WINDOW_WIDTH_LO, 0x00, 0x00},
	{SENSOR, S5K4AA_H_BLANK_HI__, 0x00, 0x00},
	{SENSOR, S5K4AA_H_BLANK_LO__, 0xa8, 0x00}, /* helps to sync... */
	{SENSOR, S5K4AA_EXPOSURE_HI, 0x01, 0x00},
	{SENSOR, S5K4AA_EXPOSURE_LO, 0x00, 0x00},
	{SENSOR, 0x11, 0x04, 0x00},
	{SENSOR, 0x12, 0xc3, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, 0x02, 0x0e, 0x00},
};

static const unsigned char SXGA_s5k4aa[][4] = {
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	/* VSYNC_PARA, VSYNC_PARA : img height 1024 = 0x0400 */
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x04, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	/* HSYNC_PARA, HSYNC_PARA : img width 1280 = 0x0500 */
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x05, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xa0, 0x00}, /* 48 MHz */

	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, S5K4AA_READ_MODE, S5K4AA_RM_H_FLIP, 0x00},
	{SENSOR, 0x37, 0x01, 0x00},
	{SENSOR, S5K4AA_ROWSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_ROWSTART_LO, 0x09, 0x00},
	{SENSOR, S5K4AA_COLSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_COLSTART_LO, 0x0a, 0x00},
	{SENSOR, S5K4AA_WINDOW_HEIGHT_HI, 0x04, 0x00},
	{SENSOR, S5K4AA_WINDOW_HEIGHT_LO, 0x00, 0x00},
	{SENSOR, S5K4AA_WINDOW_WIDTH_HI, 0x05, 0x00},
	{SENSOR, S5K4AA_WINDOW_WIDTH_LO, 0x00, 0x00},
	{SENSOR, S5K4AA_H_BLANK_HI__, 0x01, 0x00},
	{SENSOR, S5K4AA_H_BLANK_LO__, 0xa8, 0x00},
	{SENSOR, S5K4AA_EXPOSURE_HI, 0x01, 0x00},
	{SENSOR, S5K4AA_EXPOSURE_LO, 0x00, 0x00},
	{SENSOR, 0x11, 0x04, 0x00},
	{SENSOR, 0x12, 0xc3, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, 0x02, 0x0e, 0x00},
};


static int s5k4aa_s_ctrl(struct v4l2_ctrl *ctrl);
static void s5k4aa_dump_registers(struct sd *sd);

static const struct v4l2_ctrl_ops s5k4aa_ctrl_ops = {
	.s_ctrl = s5k4aa_s_ctrl,
};

static
    const
	struct dmi_system_id s5k4aa_vflip_dmi_table[] = {
	{
		.ident = "BRUNEINIT",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "BRUNENIT"),
			DMI_MATCH(DMI_PRODUCT_NAME, "BRUNENIT"),
			DMI_MATCH(DMI_BOARD_VERSION, "00030D0000000001")
		}
	}, {
		.ident = "Fujitsu-Siemens Amilo Xa 2528",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Xa 2528")
		}
	}, {
		.ident = "Fujitsu-Siemens Amilo Xi 2428",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Xi 2428")
		}
	}, {
		.ident = "Fujitsu-Siemens Amilo Xi 2528",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Xi 2528")
		}
	}, {
		.ident = "Fujitsu-Siemens Amilo Xi 2550",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Xi 2550")
		}
	}, {
		.ident = "Fujitsu-Siemens Amilo Pa 2548",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pa 2548")
		}
	}, {
		.ident = "Fujitsu-Siemens Amilo Pi 2530",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pi 2530")
		}
	}, {
		.ident = "MSI GX700",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "GX700"),
			DMI_MATCH(DMI_BIOS_DATE, "12/02/2008")
		}
	}, {
		.ident = "MSI GX700",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "GX700"),
			DMI_MATCH(DMI_BIOS_DATE, "07/26/2007")
		}
	}, {
		.ident = "MSI GX700",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "GX700"),
			DMI_MATCH(DMI_BIOS_DATE, "07/19/2007")
		}
	}, {
		.ident = "MSI GX700/GX705/EX700",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "GX700/GX705/EX700")
		}
	}, {
		.ident = "MSI L735",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-1717X")
		}
	}, {
		.ident = "Lenovo Y300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "L3000 Y300"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Y300")
		}
	},
	{ }
};

static struct v4l2_pix_format s5k4aa_modes[] = {
	{
		640,
		480,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage =
			640 * 480,
		.bytesperline = 640,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	},
	{
		1280,
		1024,
		V4L2_PIX_FMT_SBGGR8,
		V4L2_FIELD_NONE,
		.sizeimage =
			1280 * 1024,
		.bytesperline = 1280,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0
	}
};

int s5k4aa_probe(struct sd *sd)
{
	u8 prod_id[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	const u8 expected_prod_id[6] = {0x00, 0x10, 0x00, 0x4b, 0x33, 0x75};
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;
	int i, err = 0;

	if (force_sensor) {
		if (force_sensor == S5K4AA_SENSOR) {
			pr_info("Forcing a %s sensor\n", s5k4aa.name);
			goto sensor_found;
		}
		/* If we want to force another sensor, don't try to probe this
		 * one */
		return -ENODEV;
	}

	gspca_dbg(gspca_dev, D_PROBE, "Probing for a s5k4aa sensor\n");

	/* Preinit the sensor */
	for (i = 0; i < ARRAY_SIZE(preinit_s5k4aa) && !err; i++) {
		u8 data[2] = {0x00, 0x00};

		switch (preinit_s5k4aa[i][0]) {
		case BRIDGE:
			err = m5602_write_bridge(sd,
						 preinit_s5k4aa[i][1],
						 preinit_s5k4aa[i][2]);
			break;

		case SENSOR:
			data[0] = preinit_s5k4aa[i][2];
			err = m5602_write_sensor(sd,
						  preinit_s5k4aa[i][1],
						  data, 1);
			break;

		case SENSOR_LONG:
			data[0] = preinit_s5k4aa[i][2];
			data[1] = preinit_s5k4aa[i][3];
			err = m5602_write_sensor(sd,
						  preinit_s5k4aa[i][1],
						  data, 2);
			break;
		default:
			pr_info("Invalid stream command, exiting init\n");
			return -EINVAL;
		}
	}

	/* Test some registers, but we don't know their exact meaning yet */
	if (m5602_read_sensor(sd, 0x00, prod_id, 2))
		return -ENODEV;
	if (m5602_read_sensor(sd, 0x02, prod_id+2, 2))
		return -ENODEV;
	if (m5602_read_sensor(sd, 0x04, prod_id+4, 2))
		return -ENODEV;

	if (memcmp(prod_id, expected_prod_id, sizeof(prod_id)))
		return -ENODEV;
	else
		pr_info("Detected a s5k4aa sensor\n");

sensor_found:
	sd->gspca_dev.cam.cam_mode = s5k4aa_modes;
	sd->gspca_dev.cam.nmodes = ARRAY_SIZE(s5k4aa_modes);

	return 0;
}

int s5k4aa_start(struct sd *sd)
{
	int i, err = 0;
	u8 data[2];
	struct cam *cam = &sd->gspca_dev.cam;
	struct gspca_dev *gspca_dev = (struct gspca_dev *)sd;

	switch (cam->cam_mode[sd->gspca_dev.curr_mode].width) {
	case 1280:
		gspca_dbg(gspca_dev, D_CONF, "Configuring camera for SXGA mode\n");

		for (i = 0; i < ARRAY_SIZE(SXGA_s5k4aa); i++) {
			switch (SXGA_s5k4aa[i][0]) {
			case BRIDGE:
				err = m5602_write_bridge(sd,
						 SXGA_s5k4aa[i][1],
						 SXGA_s5k4aa[i][2]);
			break;

			case SENSOR:
				data[0] = SXGA_s5k4aa[i][2];
				err = m5602_write_sensor(sd,
						 SXGA_s5k4aa[i][1],
						 data, 1);
			break;

			case SENSOR_LONG:
				data[0] = SXGA_s5k4aa[i][2];
				data[1] = SXGA_s5k4aa[i][3];
				err = m5602_write_sensor(sd,
						  SXGA_s5k4aa[i][1],
						  data, 2);
			break;

			default:
				pr_err("Invalid stream command, exiting init\n");
				return -EINVAL;
			}
		}
		break;

	case 640:
		gspca_dbg(gspca_dev, D_CONF, "Configuring camera for VGA mode\n");

		for (i = 0; i < ARRAY_SIZE(VGA_s5k4aa); i++) {
			switch (VGA_s5k4aa[i][0]) {
			case BRIDGE:
				err = m5602_write_bridge(sd,
						 VGA_s5k4aa[i][1],
						 VGA_s5k4aa[i][2]);
			break;

			case SENSOR:
				data[0] = VGA_s5k4aa[i][2];
				err = m5602_write_sensor(sd,
						 VGA_s5k4aa[i][1],
						 data, 1);
			break;

			case SENSOR_LONG:
				data[0] = VGA_s5k4aa[i][2];
				data[1] = VGA_s5k4aa[i][3];
				err = m5602_write_sensor(sd,
						  VGA_s5k4aa[i][1],
						  data, 2);
			break;

			default:
				pr_err("Invalid stream command, exiting init\n");
				return -EINVAL;
			}
		}
		break;
	}
	if (err < 0)
		return err;

	return 0;
}

int s5k4aa_init(struct sd *sd)
{
	int i, err = 0;

	for (i = 0; i < ARRAY_SIZE(init_s5k4aa) && !err; i++) {
		u8 data[2] = {0x00, 0x00};

		switch (init_s5k4aa[i][0]) {
		case BRIDGE:
			err = m5602_write_bridge(sd,
				init_s5k4aa[i][1],
				init_s5k4aa[i][2]);
			break;

		case SENSOR:
			data[0] = init_s5k4aa[i][2];
			err = m5602_write_sensor(sd,
				init_s5k4aa[i][1], data, 1);
			break;

		case SENSOR_LONG:
			data[0] = init_s5k4aa[i][2];
			data[1] = init_s5k4aa[i][3];
			err = m5602_write_sensor(sd,
				init_s5k4aa[i][1], data, 2);
			break;
		default:
			pr_info("Invalid stream command, exiting init\n");
			return -EINVAL;
		}
	}

	if (dump_sensor)
		s5k4aa_dump_registers(sd);

	return err;
}

int s5k4aa_init_controls(struct sd *sd)
{
	struct v4l2_ctrl_handler *hdl = &sd->gspca_dev.ctrl_handler;

	sd->gspca_dev.vdev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(hdl, 6);

	v4l2_ctrl_new_std(hdl, &s5k4aa_ctrl_ops, V4L2_CID_BRIGHTNESS,
			  0, 0x1f, 1, S5K4AA_DEFAULT_BRIGHTNESS);

	v4l2_ctrl_new_std(hdl, &s5k4aa_ctrl_ops, V4L2_CID_EXPOSURE,
			  13, 0xfff, 1, 0x100);

	v4l2_ctrl_new_std(hdl, &s5k4aa_ctrl_ops, V4L2_CID_GAIN,
			  0, 127, 1, S5K4AA_DEFAULT_GAIN);

	v4l2_ctrl_new_std(hdl, &s5k4aa_ctrl_ops, V4L2_CID_SHARPNESS,
			  0, 1, 1, 1);

	sd->hflip = v4l2_ctrl_new_std(hdl, &s5k4aa_ctrl_ops, V4L2_CID_HFLIP,
				      0, 1, 1, 0);
	sd->vflip = v4l2_ctrl_new_std(hdl, &s5k4aa_ctrl_ops, V4L2_CID_VFLIP,
				      0, 1, 1, 0);

	if (hdl->error) {
		pr_err("Could not initialize controls\n");
		return hdl->error;
	}

	v4l2_ctrl_cluster(2, &sd->hflip);

	return 0;
}

static int s5k4aa_set_exposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	gspca_dbg(gspca_dev, D_CONF, "Set exposure to %d\n", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;
	data = (val >> 8) & 0xff;
	err = m5602_write_sensor(sd, S5K4AA_EXPOSURE_HI, &data, 1);
	if (err < 0)
		return err;
	data = val & 0xff;
	err = m5602_write_sensor(sd, S5K4AA_EXPOSURE_LO, &data, 1);

	return err;
}

static int s5k4aa_set_hvflip(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;
	int hflip = sd->hflip->val;
	int vflip = sd->vflip->val;

	gspca_dbg(gspca_dev, D_CONF, "Set hvflip %d %d\n", hflip, vflip);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		return err;

	if (dmi_check_system(s5k4aa_vflip_dmi_table)) {
		hflip = !hflip;
		vflip = !vflip;
	}

	data = (data & 0x7f) | (vflip << 7) | (hflip << 6);
	err = m5602_write_sensor(sd, S5K4AA_READ_MODE, &data, 1);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);
	if (err < 0)
		return err;
	if (hflip)
		data &= 0xfe;
	else
		data |= 0x01;
	err = m5602_write_sensor(sd, S5K4AA_COLSTART_LO, &data, 1);
	if (err < 0)
		return err;

	err = m5602_read_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
	if (err < 0)
		return err;
	if (vflip)
		data &= 0xfe;
	else
		data |= 0x01;
	err = m5602_write_sensor(sd, S5K4AA_ROWSTART_LO, &data, 1);
	if (err < 0)
		return err;

	return 0;
}

static int s5k4aa_set_gain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	gspca_dbg(gspca_dev, D_CONF, "Set gain to %d\n", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;

	data = val & 0xff;
	err = m5602_write_sensor(sd, S5K4AA_GAIN, &data, 1);

	return err;
}

static int s5k4aa_set_brightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	gspca_dbg(gspca_dev, D_CONF, "Set brightness to %d\n", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;

	data = val & 0xff;
	return m5602_write_sensor(sd, S5K4AA_BRIGHTNESS, &data, 1);
}

static int s5k4aa_set_noise(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;
	u8 data = S5K4AA_PAGE_MAP_2;
	int err;

	gspca_dbg(gspca_dev, D_CONF, "Set noise to %d\n", val);
	err = m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &data, 1);
	if (err < 0)
		return err;

	data = val & 0x01;
	return m5602_write_sensor(sd, S5K4AA_NOISE_SUPP, &data, 1);
}

static int s5k4aa_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gspca_dev *gspca_dev =
		container_of(ctrl->handler, struct gspca_dev, ctrl_handler);
	int err;

	if (!gspca_dev->streaming)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		err = s5k4aa_set_brightness(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		err = s5k4aa_set_exposure(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_GAIN:
		err = s5k4aa_set_gain(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_SHARPNESS:
		err = s5k4aa_set_noise(gspca_dev, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		err = s5k4aa_set_hvflip(gspca_dev);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

void s5k4aa_disconnect(struct sd *sd)
{
	sd->sensor = NULL;
}

static void s5k4aa_dump_registers(struct sd *sd)
{
	int address;
	u8 page, old_page;
	m5602_read_sensor(sd, S5K4AA_PAGE_MAP, &old_page, 1);
	for (page = 0; page < 16; page++) {
		m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &page, 1);
		pr_info("Dumping the s5k4aa register state for page 0x%x\n",
			page);
		for (address = 0; address <= 0xff; address++) {
			u8 value = 0;
			m5602_read_sensor(sd, address, &value, 1);
			pr_info("register 0x%x contains 0x%x\n",
				address, value);
		}
	}
	pr_info("s5k4aa register state dump complete\n");

	for (page = 0; page < 16; page++) {
		m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &page, 1);
		pr_info("Probing for which registers that are read/write for page 0x%x\n",
			page);
		for (address = 0; address <= 0xff; address++) {
			u8 old_value, ctrl_value, test_value = 0xff;

			m5602_read_sensor(sd, address, &old_value, 1);
			m5602_write_sensor(sd, address, &test_value, 1);
			m5602_read_sensor(sd, address, &ctrl_value, 1);

			if (ctrl_value == test_value)
				pr_info("register 0x%x is writeable\n",
					address);
			else
				pr_info("register 0x%x is read only\n",
					address);

			/* Restore original value */
			m5602_write_sensor(sd, address, &old_value, 1);
		}
	}
	pr_info("Read/write register probing complete\n");
	m5602_write_sensor(sd, S5K4AA_PAGE_MAP, &old_page, 1);
}
