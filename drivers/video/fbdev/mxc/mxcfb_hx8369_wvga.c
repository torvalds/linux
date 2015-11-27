/*
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/mxcfb.h>
#include <linux/backlight.h>
#include <video/mipi_display.h>

#include "mipi_dsi.h"

#define MIPI_DSI_MAX_RET_PACK_SIZE				(0x4)

#define HX8369BL_MAX_BRIGHT		(255)
#define HX8369BL_DEF_BRIGHT		(255)

#define HX8369_MAX_DPHY_CLK					(800)
#define HX8369_ONE_DATA_LANE					(0x1)
#define HX8369_TWO_DATA_LANE					(0x2)

#define HX8369_CMD_SETEXTC					(0xB9)
#define HX8369_CMD_SETEXTC_LEN					(0x4)
#define HX8369_CMD_SETEXTC_PARAM_1				(0x6983ff)

#define HX8369_CMD_GETHXID					(0xF4)
#define HX8369_CMD_GETHXID_LEN					(0x4)
#define HX8369_ID						(0x69)
#define HX8369_ID_MASK						(0xFF)

#define HX8369_CMD_SETDISP					(0xB2)
#define HX8369_CMD_SETDISP_LEN					(16)
#define HX8369_CMD_SETDISP_1_HALT				(0x00)
#define HX8369_CMD_SETDISP_2_RES_MODE				(0x23)
#define HX8369_CMD_SETDISP_3_BP					(0x03)
#define HX8369_CMD_SETDISP_4_FP					(0x03)
#define HX8369_CMD_SETDISP_5_SAP				(0x70)
#define HX8369_CMD_SETDISP_6_GENON				(0x00)
#define HX8369_CMD_SETDISP_7_GENOFF				(0xff)
#define HX8369_CMD_SETDISP_8_RTN				(0x00)
#define HX8369_CMD_SETDISP_9_TEI				(0x00)
#define HX8369_CMD_SETDISP_10_TEP_UP				(0x00)
#define HX8369_CMD_SETDISP_11_TEP_LOW				(0x00)
#define HX8369_CMD_SETDISP_12_BP_PE				(0x03)
#define HX8369_CMD_SETDISP_13_FP_PE				(0x03)
#define HX8369_CMD_SETDISP_14_RTN_PE				(0x00)
#define HX8369_CMD_SETDISP_15_GON				(0x01)

#define HX8369_CMD_SETCYC					(0xB4)
#define HX8369_CMD_SETCYC_LEN					(6)
#define HX8369_CMD_SETCYC_PARAM_1				(0x5f1d00)
#define HX8369_CMD_SETCYC_PARAM_2				(0x060e)

#define HX8369_CMD_SETGIP					(0xD5)
#define HX8369_CMD_SETGIP_LEN					(27)
#define HX8369_CMD_SETGIP_PARAM_1				(0x030400)
#define HX8369_CMD_SETGIP_PARAM_2				(0x1c050100)
#define HX8369_CMD_SETGIP_PARAM_3				(0x00030170)
#define HX8369_CMD_SETGIP_PARAM_4				(0x51064000)
#define HX8369_CMD_SETGIP_PARAM_5				(0x41000007)
#define HX8369_CMD_SETGIP_PARAM_6				(0x07075006)
#define HX8369_CMD_SETGIP_PARAM_7				(0x040f)

#define HX8369_CMD_SETPOWER					(0xB1)
#define HX8369_CMD_SETPOWER_LEN					(20)
#define HX8369_CMD_SETPOWER_PARAM_1				(0x340001)
#define HX8369_CMD_SETPOWER_PARAM_2				(0x0f0f0006)
#define HX8369_CMD_SETPOWER_PARAM_3				(0x3f3f322a)
#define HX8369_CMD_SETPOWER_PARAM_4				(0xe6013a07)
#define HX8369_CMD_SETPOWER_PARAM_5				(0xe6e6e6e6)

#define HX8369_CMD_SETVCOM					(0xB6)
#define HX8369_CMD_SETVCOM_LEN					(3)
#define HX8369_CMD_SETVCOM_PARAM_1				(0x5656)

#define HX8369_CMD_SETPANEL					(0xCC)
#define HX8369_CMD_SETPANEL_PARAM_1				(0x02)

#define HX8369_CMD_SETGAMMA					(0xE0)
#define HX8369_CMD_SETGAMMA_LEN					(35)
#define HX8369_CMD_SETGAMMA_PARAM_1				(0x221d00)
#define HX8369_CMD_SETGAMMA_PARAM_2				(0x2e3f3d38)
#define HX8369_CMD_SETGAMMA_PARAM_3				(0x0f0d064a)
#define HX8369_CMD_SETGAMMA_PARAM_4				(0x16131513)
#define HX8369_CMD_SETGAMMA_PARAM_5				(0x1d001910)
#define HX8369_CMD_SETGAMMA_PARAM_6				(0x3f3d3822)
#define HX8369_CMD_SETGAMMA_PARAM_7				(0x0d064a2e)
#define HX8369_CMD_SETGAMMA_PARAM_8				(0x1315130f)
#define HX8369_CMD_SETGAMMA_PARAM_9				(0x191016)

#define HX8369_CMD_SETMIPI					(0xBA)
#define HX8369_CMD_SETMIPI_LEN					(14)
#define HX8369_CMD_SETMIPI_PARAM_1				(0xc6a000)
#define HX8369_CMD_SETMIPI_PARAM_2				(0x10000a00)
#define HX8369_CMD_SETMIPI_ONELANE				(0x10 << 24)
#define HX8369_CMD_SETMIPI_TWOLANE				(0x11 << 24)
#define HX8369_CMD_SETMIPI_PARAM_3				(0x00026f30)
#define HX8369_CMD_SETMIPI_PARAM_4				(0x4018)

#define HX8369_CMD_SETPIXEL_FMT					(0x3A)
#define HX8369_CMD_SETPIXEL_FMT_24BPP				(0x77)
#define HX8369_CMD_SETPIXEL_FMT_18BPP				(0x66)
#define HX8369_CMD_SETPIXEL_FMT_16BPP				(0x55)

#define HX8369_CMD_SETCLUMN_ADDR				(0x2A)
#define HX8369_CMD_SETCLUMN_ADDR_LEN				(5)
#define HX8369_CMD_SETCLUMN_ADDR_PARAM_1			(0xdf0000)
#define HX8369_CMD_SETCLUMN_ADDR_PARAM_2			(0x01)

#define HX8369_CMD_SETPAGE_ADDR					(0x2B)
#define HX8369_CMD_SETPAGE_ADDR_LEN				(5)
#define HX8369_CMD_SETPAGE_ADDR_PARAM_1				(0x1f0000)
#define HX8369_CMD_SETPAGE_ADDR_PARAM_2				(0x03)

#define HX8369_CMD_WRT_DISP_BRIGHT				(0x51)
#define HX8369_CMD_WRT_DISP_BRIGHT_PARAM_1			(0xFF)

#define HX8369_CMD_WRT_CABC_MIN_BRIGHT				(0x5E)
#define HX8369_CMD_WRT_CABC_MIN_BRIGHT_PARAM_1			(0x20)

#define HX8369_CMD_WRT_CABC_CTRL				(0x55)
#define HX8369_CMD_WRT_CABC_CTRL_PARAM_1			(0x1)

#define HX8369_CMD_WRT_CTRL_DISP				(0x53)
#define HX8369_CMD_WRT_CTRL_DISP_PARAM_1			(0x24)

#define CHECK_RETCODE(ret)					\
do {								\
	if (ret < 0) {						\
		dev_err(&mipi_dsi->pdev->dev,			\
			"%s ERR: ret:%d, line:%d.\n",		\
			__func__, ret, __LINE__);		\
		return ret;					\
	}							\
} while (0)

static int hx8369bl_brightness;
static int mipid_init_backlight(struct mipi_dsi_info *mipi_dsi);

static struct fb_videomode truly_lcd_modedb[] = {
	{
	 "TRULY-WVGA", 64, 480, 800, 37880,
	 8, 8,
	 6, 6,
	 8, 6,
	 FB_SYNC_OE_LOW_ACT,
	 FB_VMODE_NONINTERLACED,
	 0,
	},
};

static struct mipi_lcd_config lcd_config = {
	.virtual_ch		= 0x0,
	.data_lane_num  = HX8369_TWO_DATA_LANE,
	.max_phy_clk    = HX8369_MAX_DPHY_CLK,
	.dpi_fmt		= MIPI_RGB888,
};
void mipid_hx8369_get_lcd_videomode(struct fb_videomode **mode, int *size,
		struct mipi_lcd_config **data)
{
	*mode = &truly_lcd_modedb[0];
	*size = ARRAY_SIZE(truly_lcd_modedb);
	*data = &lcd_config;
}

int mipid_hx8369_lcd_setup(struct mipi_dsi_info *mipi_dsi)
{
	u32 buf[DSI_CMD_BUF_MAXSIZE];
	int err;

	dev_dbg(&mipi_dsi->pdev->dev, "MIPI DSI LCD setup.\n");
	buf[0] = HX8369_CMD_SETEXTC | (HX8369_CMD_SETEXTC_PARAM_1 << 8);
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE,
					buf, HX8369_CMD_SETEXTC_LEN);
	CHECK_RETCODE(err);
	buf[0] = MIPI_DSI_MAX_RET_PACK_SIZE;
	err = mipi_dsi_pkt_write(mipi_dsi,
				MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE,
				buf, 0);
	CHECK_RETCODE(err);
	buf[0] = HX8369_CMD_GETHXID;
	err =  mipi_dsi_pkt_read(mipi_dsi,
			MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM,
			buf, HX8369_CMD_GETHXID_LEN);
	if (!err && ((buf[0] & HX8369_ID_MASK) == HX8369_ID)) {
		dev_info(&mipi_dsi->pdev->dev,
				"MIPI DSI LCD ID:0x%x.\n", buf[0]);
	} else {
		dev_err(&mipi_dsi->pdev->dev,
			"mipi_dsi_pkt_read err:%d, data:0x%x.\n",
			err, buf[0]);
		dev_info(&mipi_dsi->pdev->dev,
				"MIPI DSI LCD not detected!\n");
		return err;
	}

	/* set LCD resolution as 480RGBx800, DPI interface,
	 * display operation mode: RGB data bypass GRAM mode.
	 */
	buf[0] = HX8369_CMD_SETDISP | (HX8369_CMD_SETDISP_1_HALT << 8) |
			(HX8369_CMD_SETDISP_2_RES_MODE << 16) |
			(HX8369_CMD_SETDISP_3_BP << 24);
	buf[1] = HX8369_CMD_SETDISP_4_FP | (HX8369_CMD_SETDISP_5_SAP << 8) |
			 (HX8369_CMD_SETDISP_6_GENON << 16) |
			 (HX8369_CMD_SETDISP_7_GENOFF << 24);
	buf[2] = HX8369_CMD_SETDISP_8_RTN | (HX8369_CMD_SETDISP_9_TEI << 8) |
			 (HX8369_CMD_SETDISP_10_TEP_UP << 16) |
			 (HX8369_CMD_SETDISP_11_TEP_LOW << 24);
	buf[3] = HX8369_CMD_SETDISP_12_BP_PE |
			(HX8369_CMD_SETDISP_13_FP_PE << 8) |
			 (HX8369_CMD_SETDISP_14_RTN_PE << 16) |
			 (HX8369_CMD_SETDISP_15_GON << 24);
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE,
						buf, HX8369_CMD_SETDISP_LEN);
	CHECK_RETCODE(err);

	/* Set display waveform cycle */
	buf[0] = HX8369_CMD_SETCYC | (HX8369_CMD_SETCYC_PARAM_1 << 8);
	buf[1] = HX8369_CMD_SETCYC_PARAM_2;
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE,
						buf, HX8369_CMD_SETCYC_LEN);
	CHECK_RETCODE(err);

	/* Set GIP timing output control */
	buf[0] = HX8369_CMD_SETGIP | (HX8369_CMD_SETGIP_PARAM_1 << 8);
	buf[1] = HX8369_CMD_SETGIP_PARAM_2;
	buf[2] = HX8369_CMD_SETGIP_PARAM_3;
	buf[3] = HX8369_CMD_SETGIP_PARAM_4;
	buf[4] = HX8369_CMD_SETGIP_PARAM_5;
	buf[5] = HX8369_CMD_SETGIP_PARAM_6;
	buf[6] = HX8369_CMD_SETGIP_PARAM_7;
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE, buf,
				HX8369_CMD_SETGIP_LEN);
	CHECK_RETCODE(err);

	/* Set power: standby, DC etc. */
	buf[0] = HX8369_CMD_SETPOWER | (HX8369_CMD_SETPOWER_PARAM_1 << 8);
	buf[1] = HX8369_CMD_SETPOWER_PARAM_2;
	buf[2] = HX8369_CMD_SETPOWER_PARAM_3;
	buf[3] = HX8369_CMD_SETPOWER_PARAM_4;
	buf[4] = HX8369_CMD_SETPOWER_PARAM_5;
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE, buf,
				HX8369_CMD_SETPOWER_LEN);
	CHECK_RETCODE(err);

	/* Set VCOM voltage. */
	buf[0] = HX8369_CMD_SETVCOM | (HX8369_CMD_SETVCOM_PARAM_1 << 8);
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE, buf,
				HX8369_CMD_SETVCOM_LEN);
	CHECK_RETCODE(err);

	/* Set Panel: BGR/RGB or Inversion. */
	buf[0] = HX8369_CMD_SETPANEL | (HX8369_CMD_SETPANEL_PARAM_1 << 8);
	err = mipi_dsi_pkt_write(mipi_dsi,
		MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM, buf, 0);
	CHECK_RETCODE(err);

	/* Set gamma curve related setting */
	buf[0] = HX8369_CMD_SETGAMMA | (HX8369_CMD_SETGAMMA_PARAM_1 << 8);
	buf[1] = HX8369_CMD_SETGAMMA_PARAM_2;
	buf[2] = HX8369_CMD_SETGAMMA_PARAM_3;
	buf[3] = HX8369_CMD_SETGAMMA_PARAM_4;
	buf[4] = HX8369_CMD_SETGAMMA_PARAM_5;
	buf[5] = HX8369_CMD_SETGAMMA_PARAM_6;
	buf[6] = HX8369_CMD_SETGAMMA_PARAM_7;
	buf[7] = HX8369_CMD_SETGAMMA_PARAM_8;
	buf[8] = HX8369_CMD_SETGAMMA_PARAM_9;
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE, buf,
				HX8369_CMD_SETGAMMA_LEN);
	CHECK_RETCODE(err);

	/* Set MIPI: DPHYCMD & DSICMD, data lane number */
	buf[0] = HX8369_CMD_SETMIPI | (HX8369_CMD_SETMIPI_PARAM_1 << 8);
	buf[1] = HX8369_CMD_SETMIPI_PARAM_2;
	buf[2] = HX8369_CMD_SETMIPI_PARAM_3;
	if (lcd_config.data_lane_num == HX8369_ONE_DATA_LANE)
		buf[2] |= HX8369_CMD_SETMIPI_ONELANE;
	else
		buf[2] |= HX8369_CMD_SETMIPI_TWOLANE;
	buf[3] = HX8369_CMD_SETMIPI_PARAM_4;
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE, buf,
				HX8369_CMD_SETMIPI_LEN);
	CHECK_RETCODE(err);

	/* Set pixel format:24bpp */
	buf[0] = HX8369_CMD_SETPIXEL_FMT;
	switch (lcd_config.dpi_fmt) {
	case MIPI_RGB565_PACKED:
	case MIPI_RGB565_LOOSELY:
	case MIPI_RGB565_CONFIG3:
		buf[0] |= (HX8369_CMD_SETPIXEL_FMT_16BPP << 8);
		break;

	case MIPI_RGB666_LOOSELY:
	case MIPI_RGB666_PACKED:
		buf[0] |= (HX8369_CMD_SETPIXEL_FMT_18BPP << 8);
		break;

	case MIPI_RGB888:
		buf[0] |= (HX8369_CMD_SETPIXEL_FMT_24BPP << 8);
		break;

	default:
		buf[0] |= (HX8369_CMD_SETPIXEL_FMT_24BPP << 8);
		break;
	}
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM,
			buf, 0);
	CHECK_RETCODE(err);

	/* Set column address: 0~479 */
	buf[0] = HX8369_CMD_SETCLUMN_ADDR |
		(HX8369_CMD_SETCLUMN_ADDR_PARAM_1 << 8);
	buf[1] = HX8369_CMD_SETCLUMN_ADDR_PARAM_2;
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE,
				buf, HX8369_CMD_SETCLUMN_ADDR_LEN);
	CHECK_RETCODE(err);

	/* Set page address: 0~799 */
	buf[0] = HX8369_CMD_SETPAGE_ADDR |
		(HX8369_CMD_SETPAGE_ADDR_PARAM_1 << 8);
	buf[1] = HX8369_CMD_SETPAGE_ADDR_PARAM_2;
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_LONG_WRITE,
					buf, HX8369_CMD_SETPAGE_ADDR_LEN);
	CHECK_RETCODE(err);

	/* Set display brightness related */
	buf[0] = HX8369_CMD_WRT_DISP_BRIGHT |
			(HX8369_CMD_WRT_DISP_BRIGHT_PARAM_1 << 8);
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM,
		buf, 0);
	CHECK_RETCODE(err);

	buf[0] = HX8369_CMD_WRT_CABC_CTRL |
		(HX8369_CMD_WRT_CABC_CTRL_PARAM_1 << 8);
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM,
		buf, 0);
	CHECK_RETCODE(err);

	buf[0] = HX8369_CMD_WRT_CTRL_DISP |
		(HX8369_CMD_WRT_CTRL_DISP_PARAM_1 << 8);
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM,
		buf, 0);
	CHECK_RETCODE(err);

	/* exit sleep mode and set display on */
	buf[0] = MIPI_DCS_EXIT_SLEEP_MODE;
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM,
		buf, 0);
	CHECK_RETCODE(err);
	/* To allow time for the supply voltages
	 * and clock circuits to stabilize.
	 */
	msleep(5);
	buf[0] = MIPI_DCS_SET_DISPLAY_ON;
	err = mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM,
		buf, 0);
	CHECK_RETCODE(err);

	err = mipid_init_backlight(mipi_dsi);
	return err;
}

static int mipid_bl_update_status(struct backlight_device *bl)
{
	u32 buf;
	int brightness = bl->props.brightness;
	struct mipi_dsi_info *mipi_dsi = bl_get_data(bl);

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;

	buf = HX8369_CMD_WRT_DISP_BRIGHT |
			((brightness & HX8369BL_MAX_BRIGHT) << 8);
	mipi_dsi_pkt_write(mipi_dsi, MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM,
		&buf, 0);

	hx8369bl_brightness = brightness & HX8369BL_MAX_BRIGHT;

	dev_dbg(&bl->dev, "mipid backlight bringtness:%d.\n", brightness);
	return 0;
}

static int mipid_bl_get_brightness(struct backlight_device *bl)
{
	return hx8369bl_brightness;
}

static int mipi_bl_check_fb(struct backlight_device *bl, struct fb_info *fbi)
{
	return 0;
}

static const struct backlight_ops mipid_lcd_bl_ops = {
	.update_status = mipid_bl_update_status,
	.get_brightness = mipid_bl_get_brightness,
	.check_fb = mipi_bl_check_fb,
};

static int mipid_init_backlight(struct mipi_dsi_info *mipi_dsi)
{
	struct backlight_properties props;
	struct backlight_device	*bl;

	if (mipi_dsi->bl) {
		pr_debug("mipid backlight already init!\n");
		return 0;
	}
	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = HX8369BL_MAX_BRIGHT;
	props.type = BACKLIGHT_RAW;
	bl = backlight_device_register("mipid-bl", &mipi_dsi->pdev->dev,
		mipi_dsi, &mipid_lcd_bl_ops, &props);
	if (IS_ERR(bl)) {
		pr_err("error %ld on backlight register\n", PTR_ERR(bl));
		return PTR_ERR(bl);
	}
	mipi_dsi->bl = bl;
	bl->props.power = FB_BLANK_UNBLANK;
	bl->props.fb_blank = FB_BLANK_UNBLANK;
	bl->props.brightness = HX8369BL_DEF_BRIGHT;

	mipid_bl_update_status(bl);
	return 0;
}
