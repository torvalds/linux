/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/delay.h>
#include <mach/gpio.h>
#include "msm_fb.h"

/* registers */
#define GORDON_REG_NOP          0x00
#define GORDON_REG_IMGCTL1      0x10
#define GORDON_REG_IMGCTL2      0x11
#define GORDON_REG_IMGSET1      0x12
#define GORDON_REG_IMGSET2      0x13
#define GORDON_REG_IVBP1        0x14
#define GORDON_REG_IHBP1        0x15
#define GORDON_REG_IVNUM1       0x16
#define GORDON_REG_IHNUM1       0x17
#define GORDON_REG_IVBP2        0x18
#define GORDON_REG_IHBP2        0x19
#define GORDON_REG_IVNUM2       0x1A
#define GORDON_REG_IHNUM2       0x1B
#define GORDON_REG_LCDIFCTL1    0x30
#define GORDON_REG_VALTRAN      0x31
#define GORDON_REG_AVCTL        0x33
#define GORDON_REG_LCDIFCTL2    0x34
#define GORDON_REG_LCDIFCTL3    0x35
#define GORDON_REG_LCDIFSET1    0x36
#define GORDON_REG_PCCTL        0x3C
#define GORDON_REG_TPARAM1      0x40
#define GORDON_REG_TLCDIF1      0x41
#define GORDON_REG_TSSPB_ST1    0x42
#define GORDON_REG_TSSPB_ED1    0x43
#define GORDON_REG_TSCK_ST1     0x44
#define GORDON_REG_TSCK_WD1     0x45
#define GORDON_REG_TGSPB_VST1   0x46
#define GORDON_REG_TGSPB_VED1   0x47
#define GORDON_REG_TGSPB_CH1    0x48
#define GORDON_REG_TGCK_ST1     0x49
#define GORDON_REG_TGCK_ED1     0x4A
#define GORDON_REG_TPCTL_ST1    0x4B
#define GORDON_REG_TPCTL_ED1    0x4C
#define GORDON_REG_TPCHG_ED1    0x4D
#define GORDON_REG_TCOM_CH1     0x4E
#define GORDON_REG_THBP1        0x4F
#define GORDON_REG_TPHCTL1      0x50
#define GORDON_REG_EVPH1        0x51
#define GORDON_REG_EVPL1        0x52
#define GORDON_REG_EVNH1        0x53
#define GORDON_REG_EVNL1        0x54
#define GORDON_REG_TBIAS1       0x55
#define GORDON_REG_TPARAM2      0x56
#define GORDON_REG_TLCDIF2      0x57
#define GORDON_REG_TSSPB_ST2    0x58
#define GORDON_REG_TSSPB_ED2    0x59
#define GORDON_REG_TSCK_ST2     0x5A
#define GORDON_REG_TSCK_WD2     0x5B
#define GORDON_REG_TGSPB_VST2   0x5C
#define GORDON_REG_TGSPB_VED2   0x5D
#define GORDON_REG_TGSPB_CH2    0x5E
#define GORDON_REG_TGCK_ST2     0x5F
#define GORDON_REG_TGCK_ED2     0x60
#define GORDON_REG_TPCTL_ST2    0x61
#define GORDON_REG_TPCTL_ED2    0x62
#define GORDON_REG_TPCHG_ED2    0x63
#define GORDON_REG_TCOM_CH2     0x64
#define GORDON_REG_THBP2        0x65
#define GORDON_REG_TPHCTL2      0x66
#define GORDON_REG_POWCTL       0x80

static int lcdc_gordon_panel_off(struct platform_device *pdev);

static int spi_cs;
static int spi_sclk;
static int spi_sdo;
static int spi_sdi;
static int spi_dac;
static unsigned char bit_shift[8] = { (1 << 7),	/* MSB */
	(1 << 6),
	(1 << 5),
	(1 << 4),
	(1 << 3),
	(1 << 2),
	(1 << 1),
	(1 << 0)		               /* LSB */
};

struct gordon_state_type{
	boolean disp_initialized;
	boolean display_on;
	boolean disp_powered_up;
};

static struct gordon_state_type gordon_state = { 0 };
static struct msm_panel_common_pdata *lcdc_gordon_pdata;

static void serigo(uint16 reg, uint8 data)
{
	unsigned int tx_val = ((0x00FF & reg) << 8) | data;
	unsigned char i, val = 0;

	/* Enable the Chip Select */
	gpio_set_value(spi_cs, 1);
	udelay(33);

	/* Transmit it in two parts, Higher Byte first, then Lower Byte */
	val = (unsigned char)((tx_val & 0xFF00) >> 8);

	/* Clock should be Low before entering ! */
	for (i = 0; i < 8; i++) {
		/* #1: Drive the Data (High or Low) */
		if (val & bit_shift[i])
			gpio_set_value(spi_sdi, 1);
		else
			gpio_set_value(spi_sdi, 0);

		/* #2: Drive the Clk High and then Low */
		udelay(33);
		gpio_set_value(spi_sclk, 1);
		udelay(33);
		gpio_set_value(spi_sclk, 0);
	}

	/* Idle state of SDO (MOSI) is Low */
	gpio_set_value(spi_sdi, 0);
	/* ..then Lower Byte */
	val = (uint8) (tx_val & 0x00FF);
	/* Before we enter here the Clock should be Low ! */

	for (i = 0; i < 8; i++) {
		/* #1: Drive the Data (High or Low) */
		if (val & bit_shift[i])
			gpio_set_value(spi_sdi, 1);
		else
			gpio_set_value(spi_sdi, 0);

		/* #2: Drive the Clk High and then Low */
		udelay(33);

		gpio_set_value(spi_sclk, 1);
		udelay(33);
		gpio_set_value(spi_sclk, 0);
	}

	/* Idle state of SDO (MOSI) is Low */
	gpio_set_value(spi_sdi, 0);

	/* Now Disable the Chip Select */
	udelay(33);
	gpio_set_value(spi_cs, 0);
}

static void spi_init(void)
{
	/* Setting the Default GPIO's */
	spi_sclk = *(lcdc_gordon_pdata->gpio_num);
	spi_cs   = *(lcdc_gordon_pdata->gpio_num + 1);
	spi_sdi  = *(lcdc_gordon_pdata->gpio_num + 2);
	spi_sdo  = *(lcdc_gordon_pdata->gpio_num + 3);

	/* Set the output so that we dont disturb the slave device */
	gpio_set_value(spi_sclk, 0);
	gpio_set_value(spi_sdi, 0);

	/* Set the Chip Select De-asserted */
	gpio_set_value(spi_cs, 0);

}

static void gordon_disp_powerup(void)
{
	if (!gordon_state.disp_powered_up && !gordon_state.display_on) {
		/* Reset the hardware first */
		/* Include DAC power up implementation here */
	      gordon_state.disp_powered_up = TRUE;
	}
}

static void gordon_init(void)
{
	/* Image interface settings */
	serigo(GORDON_REG_IMGCTL2, 0x00);
	serigo(GORDON_REG_IMGSET1, 0x00);

	/* Exchange the RGB signal for J510(Softbank mobile) */
	serigo(GORDON_REG_IMGSET2, 0x12);
	serigo(GORDON_REG_LCDIFSET1, 0x00);

	/* Pre-charge settings */
	serigo(GORDON_REG_PCCTL, 0x09);
	serigo(GORDON_REG_LCDIFCTL2, 0x7B);

	mdelay(1);
}

static void gordon_disp_on(void)
{
	if (gordon_state.disp_powered_up && !gordon_state.display_on) {
		gordon_init();
		mdelay(20);
		/* gordon_dispmode setting */
		serigo(GORDON_REG_TPARAM1, 0x30);
		serigo(GORDON_REG_TLCDIF1, 0x00);
		serigo(GORDON_REG_TSSPB_ST1, 0x8B);
		serigo(GORDON_REG_TSSPB_ED1, 0x93);
		serigo(GORDON_REG_TSCK_ST1, 0x88);
		serigo(GORDON_REG_TSCK_WD1, 0x00);
		serigo(GORDON_REG_TGSPB_VST1, 0x01);
		serigo(GORDON_REG_TGSPB_VED1, 0x02);
		serigo(GORDON_REG_TGSPB_CH1, 0x5E);
		serigo(GORDON_REG_TGCK_ST1, 0x80);
		serigo(GORDON_REG_TGCK_ED1, 0x3C);
		serigo(GORDON_REG_TPCTL_ST1, 0x50);
		serigo(GORDON_REG_TPCTL_ED1, 0x74);
		serigo(GORDON_REG_TPCHG_ED1, 0x78);
		serigo(GORDON_REG_TCOM_CH1, 0x50);
		serigo(GORDON_REG_THBP1, 0x84);
		serigo(GORDON_REG_TPHCTL1, 0x00);
		serigo(GORDON_REG_EVPH1, 0x70);
		serigo(GORDON_REG_EVPL1, 0x64);
		serigo(GORDON_REG_EVNH1, 0x56);
		serigo(GORDON_REG_EVNL1, 0x48);
		serigo(GORDON_REG_TBIAS1, 0x88);

		/* QVGA settings */
		serigo(GORDON_REG_TPARAM2, 0x28);
		serigo(GORDON_REG_TLCDIF2, 0x14);
		serigo(GORDON_REG_TSSPB_ST2, 0x49);
		serigo(GORDON_REG_TSSPB_ED2, 0x4B);
		serigo(GORDON_REG_TSCK_ST2, 0x4A);
		serigo(GORDON_REG_TSCK_WD2, 0x02);
		serigo(GORDON_REG_TGSPB_VST2, 0x02);
		serigo(GORDON_REG_TGSPB_VED2, 0x03);
		serigo(GORDON_REG_TGSPB_CH2, 0x2F);
		serigo(GORDON_REG_TGCK_ST2, 0x40);
		serigo(GORDON_REG_TGCK_ED2, 0x1E);
		serigo(GORDON_REG_TPCTL_ST2, 0x2C);
		serigo(GORDON_REG_TPCTL_ED2, 0x3A);
		serigo(GORDON_REG_TPCHG_ED2, 0x3C);
		serigo(GORDON_REG_TCOM_CH2, 0x28);
		serigo(GORDON_REG_THBP2, 0x4D);
		serigo(GORDON_REG_TPHCTL2, 0x1A);

		/* VGA settings */
		serigo(GORDON_REG_IVBP1, 0x02);
		serigo(GORDON_REG_IHBP1, 0x90);
		serigo(GORDON_REG_IVNUM1, 0xA0);
		serigo(GORDON_REG_IHNUM1, 0x78);

		/* QVGA settings */
		serigo(GORDON_REG_IVBP2, 0x02);
		serigo(GORDON_REG_IHBP2, 0x48);
		serigo(GORDON_REG_IVNUM2, 0x50);
		serigo(GORDON_REG_IHNUM2, 0x3C);

		/* Gordon Charge pump settings and ON */
		serigo(GORDON_REG_POWCTL, 0x03);
		mdelay(15);
		serigo(GORDON_REG_POWCTL, 0x07);
		mdelay(15);

		serigo(GORDON_REG_POWCTL, 0x0F);
		mdelay(15);

		serigo(GORDON_REG_AVCTL, 0x03);
		mdelay(15);

		serigo(GORDON_REG_POWCTL, 0x1F);
		mdelay(15);

		serigo(GORDON_REG_POWCTL, 0x5F);
		mdelay(15);

		serigo(GORDON_REG_POWCTL, 0x7F);
		mdelay(15);

		serigo(GORDON_REG_LCDIFCTL1, 0x02);
		mdelay(15);

		serigo(GORDON_REG_IMGCTL1, 0x00);
		mdelay(15);

		serigo(GORDON_REG_LCDIFCTL3, 0x00);
		mdelay(15);

		serigo(GORDON_REG_VALTRAN, 0x01);
		mdelay(15);

		serigo(GORDON_REG_LCDIFCTL1, 0x03);
		mdelay(1);
		gordon_state.display_on = TRUE;
	}
}

static int lcdc_gordon_panel_on(struct platform_device *pdev)
{
	if (!gordon_state.disp_initialized) {
		/* Configure reset GPIO that drives DAC */
		lcdc_gordon_pdata->panel_config_gpio(1);
		spi_dac = *(lcdc_gordon_pdata->gpio_num + 4);
		gpio_set_value(spi_dac, 0);
		udelay(15);
		gpio_set_value(spi_dac, 1);
		spi_init();	/* LCD needs SPI */
		gordon_disp_powerup();
		gordon_disp_on();
		gordon_state.disp_initialized = TRUE;
	}
	return 0;
}

static int lcdc_gordon_panel_off(struct platform_device *pdev)
{
	if (gordon_state.disp_powered_up && gordon_state.display_on) {
		serigo(GORDON_REG_LCDIFCTL2, 0x7B);
		serigo(GORDON_REG_VALTRAN, 0x01);
		serigo(GORDON_REG_LCDIFCTL1, 0x02);
		serigo(GORDON_REG_LCDIFCTL3, 0x01);
		mdelay(20);
		serigo(GORDON_REG_VALTRAN, 0x01);
		serigo(GORDON_REG_IMGCTL1, 0x01);
		serigo(GORDON_REG_LCDIFCTL1, 0x00);
		mdelay(20);

		serigo(GORDON_REG_POWCTL, 0x1F);
		mdelay(40);

		serigo(GORDON_REG_POWCTL, 0x07);
		mdelay(40);

		serigo(GORDON_REG_POWCTL, 0x03);
		mdelay(40);

		serigo(GORDON_REG_POWCTL, 0x00);
		mdelay(40);
		lcdc_gordon_pdata->panel_config_gpio(0);
		gordon_state.display_on = FALSE;
		gordon_state.disp_initialized = FALSE;
	}
	return 0;
}

static void lcdc_gordon_set_backlight(struct msm_fb_data_type *mfd)
{
		int bl_level = mfd->bl_level;

		if (bl_level <= 1) {
			/* keep back light OFF */
			serigo(GORDON_REG_LCDIFCTL2, 0x0B);
			udelay(15);
			serigo(GORDON_REG_VALTRAN, 0x01);
		} else {
			/* keep back light ON */
			serigo(GORDON_REG_LCDIFCTL2, 0x7B);
			udelay(15);
			serigo(GORDON_REG_VALTRAN, 0x01);
		}
}

static int __init gordon_probe(struct platform_device *pdev)
{
	if (pdev->id == 0) {
		lcdc_gordon_pdata = pdev->dev.platform_data;
		return 0;
	}
	msm_fb_add_device(pdev);
	return 0;
}

static struct platform_driver this_driver = {
	.probe  = gordon_probe,
	.driver = {
		.name   = "lcdc_gordon_vga",
	},
};

static struct msm_fb_panel_data gordon_panel_data = {
	.on = lcdc_gordon_panel_on,
	.off = lcdc_gordon_panel_off,
	.set_backlight = lcdc_gordon_set_backlight,
};

static struct platform_device this_device = {
	.name   = "lcdc_gordon_vga",
	.id	= 1,
	.dev	= {
		.platform_data = &gordon_panel_data,
	}
};

static int __init lcdc_gordon_panel_init(void)
{
	int ret;
	struct msm_panel_info *pinfo;

#ifdef CONFIG_FB_MSM_TRY_MDDI_CATCH_LCDC_PRISM
	if (msm_fb_detect_client("lcdc_gordon_vga"))
		return 0;
#endif
	ret = platform_driver_register(&this_driver);
	if (ret)
		return ret;

	pinfo = &gordon_panel_data.panel_info;
	pinfo->xres = 480;
	pinfo->yres = 640;
	pinfo->type = LCDC_PANEL;
	pinfo->pdest = DISPLAY_1;
	pinfo->wait_cycle = 0;
	pinfo->bpp = 24;
	pinfo->fb_num = 2;
	pinfo->clk_rate = 24500000;
	pinfo->bl_max = 4;
	pinfo->bl_min = 1;

	pinfo->lcdc.h_back_porch = 84;
	pinfo->lcdc.h_front_porch = 33;
	pinfo->lcdc.h_pulse_width = 60;
	pinfo->lcdc.v_back_porch = 0;
	pinfo->lcdc.v_front_porch = 2;
	pinfo->lcdc.v_pulse_width = 2;
	pinfo->lcdc.border_clr = 0;     /* blk */
	pinfo->lcdc.underflow_clr = 0xff;       /* blue */
	pinfo->lcdc.hsync_skew = 0;

	ret = platform_device_register(&this_device);
	if (ret)
		platform_driver_unregister(&this_driver);

	return ret;
}

module_init(lcdc_gordon_panel_init);
