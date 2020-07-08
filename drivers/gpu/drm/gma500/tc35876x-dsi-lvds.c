/*
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>

#include <asm/intel_scu_ipc.h>

#include "mdfld_dsi_dpi.h"
#include "mdfld_dsi_pkg_sender.h"
#include "mdfld_output.h"
#include "tc35876x-dsi-lvds.h"

static struct i2c_client *tc35876x_client;
static struct i2c_client *cmi_lcd_i2c_client;
/* Panel GPIOs */
static struct gpio_desc *bridge_reset;
static struct gpio_desc *bridge_bl_enable;
static struct gpio_desc *backlight_voltage;


#define FLD_MASK(start, end)	(((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))

/* DSI D-PHY Layer Registers */
#define D0W_DPHYCONTTX		0x0004
#define CLW_DPHYCONTRX		0x0020
#define D0W_DPHYCONTRX		0x0024
#define D1W_DPHYCONTRX		0x0028
#define D2W_DPHYCONTRX		0x002C
#define D3W_DPHYCONTRX		0x0030
#define COM_DPHYCONTRX		0x0038
#define CLW_CNTRL		0x0040
#define D0W_CNTRL		0x0044
#define D1W_CNTRL		0x0048
#define D2W_CNTRL		0x004C
#define D3W_CNTRL		0x0050
#define DFTMODE_CNTRL		0x0054

/* DSI PPI Layer Registers */
#define PPI_STARTPPI		0x0104
#define PPI_BUSYPPI		0x0108
#define PPI_LINEINITCNT		0x0110
#define PPI_LPTXTIMECNT		0x0114
#define PPI_LANEENABLE		0x0134
#define PPI_TX_RX_TA		0x013C
#define PPI_CLS_ATMR		0x0140
#define PPI_D0S_ATMR		0x0144
#define PPI_D1S_ATMR		0x0148
#define PPI_D2S_ATMR		0x014C
#define PPI_D3S_ATMR		0x0150
#define PPI_D0S_CLRSIPOCOUNT	0x0164
#define PPI_D1S_CLRSIPOCOUNT	0x0168
#define PPI_D2S_CLRSIPOCOUNT	0x016C
#define PPI_D3S_CLRSIPOCOUNT	0x0170
#define CLS_PRE			0x0180
#define D0S_PRE			0x0184
#define D1S_PRE			0x0188
#define D2S_PRE			0x018C
#define D3S_PRE			0x0190
#define CLS_PREP		0x01A0
#define D0S_PREP		0x01A4
#define D1S_PREP		0x01A8
#define D2S_PREP		0x01AC
#define D3S_PREP		0x01B0
#define CLS_ZERO		0x01C0
#define D0S_ZERO		0x01C4
#define D1S_ZERO		0x01C8
#define D2S_ZERO		0x01CC
#define D3S_ZERO		0x01D0
#define PPI_CLRFLG		0x01E0
#define PPI_CLRSIPO		0x01E4
#define HSTIMEOUT		0x01F0
#define HSTIMEOUTENABLE		0x01F4

/* DSI Protocol Layer Registers */
#define DSI_STARTDSI		0x0204
#define DSI_BUSYDSI		0x0208
#define DSI_LANEENABLE		0x0210
#define DSI_LANESTATUS0		0x0214
#define DSI_LANESTATUS1		0x0218
#define DSI_INTSTATUS		0x0220
#define DSI_INTMASK		0x0224
#define DSI_INTCLR		0x0228
#define DSI_LPTXTO		0x0230

/* DSI General Registers */
#define DSIERRCNT		0x0300

/* DSI Application Layer Registers */
#define APLCTRL			0x0400
#define RDPKTLN			0x0404

/* Video Path Registers */
#define VPCTRL			0x0450
#define HTIM1			0x0454
#define HTIM2			0x0458
#define VTIM1			0x045C
#define VTIM2			0x0460
#define VFUEN			0x0464

/* LVDS Registers */
#define LVMX0003		0x0480
#define LVMX0407		0x0484
#define LVMX0811		0x0488
#define LVMX1215		0x048C
#define LVMX1619		0x0490
#define LVMX2023		0x0494
#define LVMX2427		0x0498
#define LVCFG			0x049C
#define LVPHY0			0x04A0
#define LVPHY1			0x04A4

/* System Registers */
#define SYSSTAT			0x0500
#define SYSRST			0x0504

/* GPIO Registers */
/*#define GPIOC			0x0520*/
#define GPIOO			0x0524
#define GPIOI			0x0528

/* I2C Registers */
#define I2CTIMCTRL		0x0540
#define I2CMADDR		0x0544
#define WDATAQ			0x0548
#define RDATAQ			0x054C

/* Chip/Rev Registers */
#define IDREG			0x0580

/* Debug Registers */
#define DEBUG00			0x05A0
#define DEBUG01			0x05A4

/* Panel CABC registers */
#define PANEL_PWM_CONTROL	0x90
#define PANEL_FREQ_DIVIDER_HI	0x91
#define PANEL_FREQ_DIVIDER_LO	0x92
#define PANEL_DUTY_CONTROL	0x93
#define PANEL_MODIFY_RGB	0x94
#define PANEL_FRAMERATE_CONTROL	0x96
#define PANEL_PWM_MIN		0x97
#define PANEL_PWM_REF		0x98
#define PANEL_PWM_MAX		0x99
#define PANEL_ALLOW_DISTORT	0x9A
#define PANEL_BYPASS_PWMI	0x9B

/* Panel color management registers */
#define PANEL_CM_ENABLE		0x700
#define PANEL_CM_HUE		0x701
#define PANEL_CM_SATURATION	0x702
#define PANEL_CM_INTENSITY	0x703
#define PANEL_CM_BRIGHTNESS	0x704
#define PANEL_CM_CE_ENABLE	0x705
#define PANEL_CM_PEAK_EN	0x710
#define PANEL_CM_GAIN		0x711
#define PANEL_CM_HUETABLE_START	0x730
#define PANEL_CM_HUETABLE_END	0x747 /* inclusive */

/* Input muxing for registers LVMX0003...LVMX2427 */
enum {
	INPUT_R0,	/* 0 */
	INPUT_R1,
	INPUT_R2,
	INPUT_R3,
	INPUT_R4,
	INPUT_R5,
	INPUT_R6,
	INPUT_R7,
	INPUT_G0,	/* 8 */
	INPUT_G1,
	INPUT_G2,
	INPUT_G3,
	INPUT_G4,
	INPUT_G5,
	INPUT_G6,
	INPUT_G7,
	INPUT_B0,	/* 16 */
	INPUT_B1,
	INPUT_B2,
	INPUT_B3,
	INPUT_B4,
	INPUT_B5,
	INPUT_B6,
	INPUT_B7,
	INPUT_HSYNC,	/* 24 */
	INPUT_VSYNC,
	INPUT_DE,
	LOGIC_0,
	/* 28...31 undefined */
};

#define INPUT_MUX(lvmx03, lvmx02, lvmx01, lvmx00)		\
	(FLD_VAL(lvmx03, 29, 24) | FLD_VAL(lvmx02, 20, 16) |	\
	FLD_VAL(lvmx01, 12, 8) | FLD_VAL(lvmx00, 4, 0))

/**
 * tc35876x_regw - Write DSI-LVDS bridge register using I2C
 * @client: struct i2c_client to use
 * @reg: register address
 * @value: value to write
 *
 * Returns 0 on success, or a negative error value.
 */
static int tc35876x_regw(struct i2c_client *client, u16 reg, u32 value)
{
	int r;
	u8 tx_data[] = {
		/* NOTE: Register address big-endian, data little-endian. */
		(reg >> 8) & 0xff,
		reg & 0xff,
		value & 0xff,
		(value >> 8) & 0xff,
		(value >> 16) & 0xff,
		(value >> 24) & 0xff,
	};
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = tx_data,
			.len = ARRAY_SIZE(tx_data),
		},
	};

	r = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (r < 0) {
		dev_err(&client->dev, "%s: reg 0x%04x val 0x%08x error %d\n",
			__func__, reg, value, r);
		return r;
	}

	if (r < ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "%s: reg 0x%04x val 0x%08x msgs %d\n",
			__func__, reg, value, r);
		return -EAGAIN;
	}

	dev_dbg(&client->dev, "%s: reg 0x%04x val 0x%08x\n",
			__func__, reg, value);

	return 0;
}

/**
 * tc35876x_regr - Read DSI-LVDS bridge register using I2C
 * @client: struct i2c_client to use
 * @reg: register address
 * @value: pointer for storing the value
 *
 * Returns 0 on success, or a negative error value.
 */
static int tc35876x_regr(struct i2c_client *client, u16 reg, u32 *value)
{
	int r;
	u8 tx_data[] = {
		(reg >> 8) & 0xff,
		reg & 0xff,
	};
	u8 rx_data[4];
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = tx_data,
			.len = ARRAY_SIZE(tx_data),
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = rx_data,
			.len = ARRAY_SIZE(rx_data),
		 },
	};

	r = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (r < 0) {
		dev_err(&client->dev, "%s: reg 0x%04x error %d\n", __func__,
			reg, r);
		return r;
	}

	if (r < ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "%s: reg 0x%04x msgs %d\n", __func__,
			reg, r);
		return -EAGAIN;
	}

	*value = rx_data[0] << 24 | rx_data[1] << 16 |
		rx_data[2] << 8 | rx_data[3];

	dev_dbg(&client->dev, "%s: reg 0x%04x value 0x%08x\n", __func__,
		reg, *value);

	return 0;
}

void tc35876x_set_bridge_reset_state(struct drm_device *dev, int state)
{
	if (WARN(!tc35876x_client, "%s called before probe", __func__))
		return;

	dev_dbg(&tc35876x_client->dev, "%s: state %d\n", __func__, state);

	if (!bridge_reset)
		return;

	if (state) {
		gpiod_set_value_cansleep(bridge_reset, 0);
		mdelay(10);
	} else {
		/* Pull MIPI Bridge reset pin to Low */
		gpiod_set_value_cansleep(bridge_reset, 0);
		mdelay(20);
		/* Pull MIPI Bridge reset pin to High */
		gpiod_set_value_cansleep(bridge_reset, 1);
		mdelay(40);
	}
}

void tc35876x_configure_lvds_bridge(struct drm_device *dev)
{
	struct i2c_client *i2c = tc35876x_client;
	u32 ppi_lptxtimecnt;
	u32 txtagocnt;
	u32 txtasurecnt;
	u32 id;

	if (WARN(!tc35876x_client, "%s called before probe", __func__))
		return;

	dev_dbg(&tc35876x_client->dev, "%s\n", __func__);

	if (!tc35876x_regr(i2c, IDREG, &id))
		dev_info(&tc35876x_client->dev, "tc35876x ID 0x%08x\n", id);
	else
		dev_err(&tc35876x_client->dev, "Cannot read ID\n");

	ppi_lptxtimecnt = 4;
	txtagocnt = (5 * ppi_lptxtimecnt - 3) / 4;
	txtasurecnt = 3 * ppi_lptxtimecnt / 2;
	tc35876x_regw(i2c, PPI_TX_RX_TA, FLD_VAL(txtagocnt, 26, 16) |
		FLD_VAL(txtasurecnt, 10, 0));
	tc35876x_regw(i2c, PPI_LPTXTIMECNT, FLD_VAL(ppi_lptxtimecnt, 10, 0));

	tc35876x_regw(i2c, PPI_D0S_CLRSIPOCOUNT, FLD_VAL(1, 5, 0));
	tc35876x_regw(i2c, PPI_D1S_CLRSIPOCOUNT, FLD_VAL(1, 5, 0));
	tc35876x_regw(i2c, PPI_D2S_CLRSIPOCOUNT, FLD_VAL(1, 5, 0));
	tc35876x_regw(i2c, PPI_D3S_CLRSIPOCOUNT, FLD_VAL(1, 5, 0));

	/* Enabling MIPI & PPI lanes, Enable 4 lanes */
	tc35876x_regw(i2c, PPI_LANEENABLE,
		BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0));
	tc35876x_regw(i2c, DSI_LANEENABLE,
		BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0));
	tc35876x_regw(i2c, PPI_STARTPPI, BIT(0));
	tc35876x_regw(i2c, DSI_STARTDSI, BIT(0));

	/* Setting LVDS output frequency */
	tc35876x_regw(i2c, LVPHY0, FLD_VAL(1, 20, 16) |
		FLD_VAL(2, 15, 14) | FLD_VAL(6, 4, 0)); /* 0x00048006 */

	/* Setting video panel control register,0x00000120 VTGen=ON ?!?!? */
	tc35876x_regw(i2c, VPCTRL, BIT(8) | BIT(5));

	/* Horizontal back porch and horizontal pulse width. 0x00280028 */
	tc35876x_regw(i2c, HTIM1, FLD_VAL(40, 24, 16) | FLD_VAL(40, 8, 0));

	/* Horizontal front porch and horizontal active video size. 0x00500500*/
	tc35876x_regw(i2c, HTIM2, FLD_VAL(80, 24, 16) | FLD_VAL(1280, 10, 0));

	/* Vertical back porch and vertical sync pulse width. 0x000e000a */
	tc35876x_regw(i2c, VTIM1, FLD_VAL(14, 23, 16) | FLD_VAL(10, 7, 0));

	/* Vertical front porch and vertical display size. 0x000e0320 */
	tc35876x_regw(i2c, VTIM2, FLD_VAL(14, 23, 16) | FLD_VAL(800, 10, 0));

	/* Set above HTIM1, HTIM2, VTIM1, and VTIM2 at next VSYNC. */
	tc35876x_regw(i2c, VFUEN, BIT(0));

	/* Soft reset LCD controller. */
	tc35876x_regw(i2c, SYSRST, BIT(2));

	/* LVDS-TX input muxing */
	tc35876x_regw(i2c, LVMX0003,
		INPUT_MUX(INPUT_R5, INPUT_R4, INPUT_R3, INPUT_R2));
	tc35876x_regw(i2c, LVMX0407,
		INPUT_MUX(INPUT_G2, INPUT_R7, INPUT_R1, INPUT_R6));
	tc35876x_regw(i2c, LVMX0811,
		INPUT_MUX(INPUT_G1, INPUT_G0, INPUT_G4, INPUT_G3));
	tc35876x_regw(i2c, LVMX1215,
		INPUT_MUX(INPUT_B2, INPUT_G7, INPUT_G6, INPUT_G5));
	tc35876x_regw(i2c, LVMX1619,
		INPUT_MUX(INPUT_B4, INPUT_B3, INPUT_B1, INPUT_B0));
	tc35876x_regw(i2c, LVMX2023,
		INPUT_MUX(LOGIC_0,  INPUT_B7, INPUT_B6, INPUT_B5));
	tc35876x_regw(i2c, LVMX2427,
		INPUT_MUX(INPUT_R0, INPUT_DE, INPUT_VSYNC, INPUT_HSYNC));

	/* Enable LVDS transmitter. */
	tc35876x_regw(i2c, LVCFG, BIT(0));

	/* Clear notifications. Don't write reserved bits. Was write 0xffffffff
	 * to 0x0288, must be in error?! */
	tc35876x_regw(i2c, DSI_INTCLR, FLD_MASK(31, 30) | FLD_MASK(22, 0));
}

#define GPIOPWMCTRL	0x38F
#define PWM0CLKDIV0	0x62 /* low byte */
#define PWM0CLKDIV1	0x61 /* high byte */

#define SYSTEMCLK	19200000UL /* 19.2 MHz */
#define PWM_FREQUENCY	9600 /* Hz */

/* f = baseclk / (clkdiv + 1) => clkdiv = (baseclk - f) / f */
static inline u16 calc_clkdiv(unsigned long baseclk, unsigned int f)
{
	return (baseclk - f) / f;
}

static void tc35876x_brightness_init(struct drm_device *dev)
{
	int ret;
	u8 pwmctrl;
	u16 clkdiv;

	/* Make sure the PWM reference is the 19.2 MHz system clock. Read first
	 * instead of setting directly to catch potential conflicts between PWM
	 * users. */
	ret = intel_scu_ipc_ioread8(GPIOPWMCTRL, &pwmctrl);
	if (ret || pwmctrl != 0x01) {
		if (ret)
			dev_err(&dev->pdev->dev, "GPIOPWMCTRL read failed\n");
		else
			dev_warn(&dev->pdev->dev, "GPIOPWMCTRL was not set to system clock (pwmctrl = 0x%02x)\n", pwmctrl);

		ret = intel_scu_ipc_iowrite8(GPIOPWMCTRL, 0x01);
		if (ret)
			dev_err(&dev->pdev->dev, "GPIOPWMCTRL set failed\n");
	}

	clkdiv = calc_clkdiv(SYSTEMCLK, PWM_FREQUENCY);

	ret = intel_scu_ipc_iowrite8(PWM0CLKDIV1, (clkdiv >> 8) & 0xff);
	if (!ret)
		ret = intel_scu_ipc_iowrite8(PWM0CLKDIV0, clkdiv & 0xff);

	if (ret)
		dev_err(&dev->pdev->dev, "PWM0CLKDIV set failed\n");
	else
		dev_dbg(&dev->pdev->dev, "PWM0CLKDIV set to 0x%04x (%d Hz)\n",
			clkdiv, PWM_FREQUENCY);
}

#define PWM0DUTYCYCLE			0x67

void tc35876x_brightness_control(struct drm_device *dev, int level)
{
	int ret;
	u8 duty_val;
	u8 panel_duty_val;

	level = clamp(level, 0, MDFLD_DSI_BRIGHTNESS_MAX_LEVEL);

	/* PWM duty cycle 0x00...0x63 corresponds to 0...99% */
	duty_val = level * 0x63 / MDFLD_DSI_BRIGHTNESS_MAX_LEVEL;

	/* I won't pretend to understand this formula. The panel spec is quite
	 * bad engrish.
	 */
	panel_duty_val = (2 * level - 100) * 0xA9 /
			 MDFLD_DSI_BRIGHTNESS_MAX_LEVEL + 0x56;

	ret = intel_scu_ipc_iowrite8(PWM0DUTYCYCLE, duty_val);
	if (ret)
		dev_err(&tc35876x_client->dev, "%s: ipc write fail\n",
			__func__);

	if (cmi_lcd_i2c_client) {
		ret = i2c_smbus_write_byte_data(cmi_lcd_i2c_client,
						PANEL_PWM_MAX, panel_duty_val);
		if (ret < 0)
			dev_err(&cmi_lcd_i2c_client->dev, "%s: i2c write failed\n",
				__func__);
	}
}

void tc35876x_toshiba_bridge_panel_off(struct drm_device *dev)
{
	if (WARN(!tc35876x_client, "%s called before probe", __func__))
		return;

	dev_dbg(&tc35876x_client->dev, "%s\n", __func__);

	if (bridge_bl_enable)
		gpiod_set_value_cansleep(bridge_bl_enable, 0);

	if (backlight_voltage)
		gpiod_set_value_cansleep(backlight_voltage, 0);
}

void tc35876x_toshiba_bridge_panel_on(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (WARN(!tc35876x_client, "%s called before probe", __func__))
		return;

	dev_dbg(&tc35876x_client->dev, "%s\n", __func__);

	if (backlight_voltage) {
		gpiod_set_value_cansleep(backlight_voltage, 1);
		msleep(260);
	}

	if (cmi_lcd_i2c_client) {
		int ret;
		dev_dbg(&cmi_lcd_i2c_client->dev, "setting TCON\n");
		/* Bit 4 is average_saving. Setting it to 1, the brightness is
		 * referenced to the average of the frame content. 0 means
		 * reference to the maximum of frame contents. Bits 3:0 are
		 * allow_distort. When set to a nonzero value, all color values
		 * between 255-allow_distort*2 and 255 are mapped to the
		 * 255-allow_distort*2 value.
		 */
		ret = i2c_smbus_write_byte_data(cmi_lcd_i2c_client,
						PANEL_ALLOW_DISTORT, 0x10);
		if (ret < 0)
			dev_err(&cmi_lcd_i2c_client->dev,
				"i2c write failed (%d)\n", ret);
		ret = i2c_smbus_write_byte_data(cmi_lcd_i2c_client,
						PANEL_BYPASS_PWMI, 0);
		if (ret < 0)
			dev_err(&cmi_lcd_i2c_client->dev,
				"i2c write failed (%d)\n", ret);
		/* Set minimum brightness value - this is tunable */
		ret = i2c_smbus_write_byte_data(cmi_lcd_i2c_client,
						PANEL_PWM_MIN, 0x35);
		if (ret < 0)
			dev_err(&cmi_lcd_i2c_client->dev,
				"i2c write failed (%d)\n", ret);
	}

	if (bridge_bl_enable)
		gpiod_set_value_cansleep(bridge_bl_enable, 1);

	tc35876x_brightness_control(dev, dev_priv->brightness_adjusted);
}

static struct drm_display_mode *tc35876x_get_config_mode(struct drm_device *dev)
{
	struct drm_display_mode *mode;

	dev_dbg(&dev->pdev->dev, "%s\n", __func__);

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return NULL;

	/* FIXME: do this properly. */
	mode->hdisplay = 1280;
	mode->vdisplay = 800;
	mode->hsync_start = 1360;
	mode->hsync_end = 1400;
	mode->htotal = 1440;
	mode->vsync_start = 814;
	mode->vsync_end = 824;
	mode->vtotal = 838;
	mode->clock = 33324 << 1;

	dev_info(&dev->pdev->dev, "hdisplay(w) = %d\n", mode->hdisplay);
	dev_info(&dev->pdev->dev, "vdisplay(h) = %d\n", mode->vdisplay);
	dev_info(&dev->pdev->dev, "HSS = %d\n", mode->hsync_start);
	dev_info(&dev->pdev->dev, "HSE = %d\n", mode->hsync_end);
	dev_info(&dev->pdev->dev, "htotal = %d\n", mode->htotal);
	dev_info(&dev->pdev->dev, "VSS = %d\n", mode->vsync_start);
	dev_info(&dev->pdev->dev, "VSE = %d\n", mode->vsync_end);
	dev_info(&dev->pdev->dev, "vtotal = %d\n", mode->vtotal);
	dev_info(&dev->pdev->dev, "clock = %d\n", mode->clock);

	drm_mode_set_name(mode);
	drm_mode_set_crtcinfo(mode, 0);

	mode->type |= DRM_MODE_TYPE_PREFERRED;

	return mode;
}

/* DV1 Active area 216.96 x 135.6 mm */
#define DV1_PANEL_WIDTH 217
#define DV1_PANEL_HEIGHT 136

static int tc35876x_get_panel_info(struct drm_device *dev, int pipe,
				struct panel_info *pi)
{
	if (!dev || !pi)
		return -EINVAL;

	pi->width_mm = DV1_PANEL_WIDTH;
	pi->height_mm = DV1_PANEL_HEIGHT;

	return 0;
}

static int tc35876x_bridge_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	dev_info(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "%s: i2c_check_functionality() failed\n",
			__func__);
		return -ENODEV;
	}

	bridge_reset = devm_gpiod_get_optional(&client->dev, "bridge-reset", GPIOD_OUT_LOW);
	if (IS_ERR(bridge_reset))
		return PTR_ERR(bridge_reset);
	if (bridge_reset)
		gpiod_set_consumer_name(bridge_reset, "tc35876x bridge reset");

	bridge_bl_enable = devm_gpiod_get_optional(&client->dev, "bl-en", GPIOD_OUT_LOW);
	if (IS_ERR(bridge_bl_enable))
		return PTR_ERR(bridge_bl_enable);
	if (bridge_bl_enable)
		gpiod_set_consumer_name(bridge_bl_enable, "tc35876x panel bl en");

	backlight_voltage = devm_gpiod_get_optional(&client->dev, "vadd", GPIOD_OUT_LOW);
	if (IS_ERR(backlight_voltage))
		return PTR_ERR(backlight_voltage);
	if (backlight_voltage)
		gpiod_set_consumer_name(backlight_voltage, "tc35876x panel vadd");

	tc35876x_client = client;

	return 0;
}

static int tc35876x_bridge_remove(struct i2c_client *client)
{
	dev_dbg(&client->dev, "%s\n", __func__);

	tc35876x_client = NULL;

	return 0;
}

static const struct i2c_device_id tc35876x_bridge_id[] = {
	{ "i2c_disp_brig", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tc35876x_bridge_id);

static struct i2c_driver tc35876x_bridge_i2c_driver = {
	.driver = {
		.name = "i2c_disp_brig",
	},
	.id_table = tc35876x_bridge_id,
	.probe = tc35876x_bridge_probe,
	.remove = tc35876x_bridge_remove,
};

/* LCD panel I2C */
static int cmi_lcd_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	dev_info(&client->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "%s: i2c_check_functionality() failed\n",
			__func__);
		return -ENODEV;
	}

	cmi_lcd_i2c_client = client;

	return 0;
}

static int cmi_lcd_i2c_remove(struct i2c_client *client)
{
	dev_dbg(&client->dev, "%s\n", __func__);

	cmi_lcd_i2c_client = NULL;

	return 0;
}

static const struct i2c_device_id cmi_lcd_i2c_id[] = {
	{ "cmi-lcd", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cmi_lcd_i2c_id);

static struct i2c_driver cmi_lcd_i2c_driver = {
	.driver = {
		.name = "cmi-lcd",
	},
	.id_table = cmi_lcd_i2c_id,
	.probe = cmi_lcd_i2c_probe,
	.remove = cmi_lcd_i2c_remove,
};

/* HACK to create I2C device while it's not created by platform code */
#define CMI_LCD_I2C_ADAPTER	2
#define CMI_LCD_I2C_ADDR	0x60

static int cmi_lcd_hack_create_device(void)
{
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct i2c_board_info info = {
		.type = "cmi-lcd",
		.addr = CMI_LCD_I2C_ADDR,
	};

	pr_debug("%s\n", __func__);

	adapter = i2c_get_adapter(CMI_LCD_I2C_ADAPTER);
	if (!adapter) {
		pr_err("%s: i2c_get_adapter(%d) failed\n", __func__,
			CMI_LCD_I2C_ADAPTER);
		return -EINVAL;
	}

	client = i2c_new_client_device(adapter, &info);
	if (IS_ERR(client)) {
		pr_err("%s: creating I2C device failed\n", __func__);
		i2c_put_adapter(adapter);
		return PTR_ERR(client);
	}

	return 0;
}

static const struct drm_encoder_helper_funcs tc35876x_encoder_helper_funcs = {
	.dpms = mdfld_dsi_dpi_dpms,
	.mode_fixup = mdfld_dsi_dpi_mode_fixup,
	.prepare = mdfld_dsi_dpi_prepare,
	.mode_set = mdfld_dsi_dpi_mode_set,
	.commit = mdfld_dsi_dpi_commit,
};

const struct panel_funcs mdfld_tc35876x_funcs = {
	.encoder_helper_funcs = &tc35876x_encoder_helper_funcs,
	.get_config_mode = tc35876x_get_config_mode,
	.get_panel_info = tc35876x_get_panel_info,
};

void tc35876x_init(struct drm_device *dev)
{
	int r;

	dev_dbg(&dev->pdev->dev, "%s\n", __func__);

	cmi_lcd_hack_create_device();

	r = i2c_add_driver(&cmi_lcd_i2c_driver);
	if (r < 0)
		dev_err(&dev->pdev->dev,
			"%s: i2c_add_driver() for %s failed (%d)\n",
			__func__, cmi_lcd_i2c_driver.driver.name, r);

	r = i2c_add_driver(&tc35876x_bridge_i2c_driver);
	if (r < 0)
		dev_err(&dev->pdev->dev,
			"%s: i2c_add_driver() for %s failed (%d)\n",
			__func__, tc35876x_bridge_i2c_driver.driver.name, r);

	tc35876x_brightness_init(dev);
}

void tc35876x_exit(void)
{
	pr_debug("%s\n", __func__);

	i2c_del_driver(&tc35876x_bridge_i2c_driver);

	if (cmi_lcd_i2c_client)
		i2c_del_driver(&cmi_lcd_i2c_driver);
}
