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
#include <linux/module.h>
#include <mach/gpio.h>
#include <mach/pmic.h>
#include "msm_fb.h"

#ifdef CONFIG_FB_MSM_TRY_MDDI_CATCH_LCDC_PRISM
#include "mddihosti.h"
#endif

static int spi_cs;
static int spi_sclk;
static int spi_mosi;
static int spi_miso;

struct toshiba_state_type{
	boolean disp_initialized;
	boolean display_on;
	boolean disp_powered_up;
};

static struct toshiba_state_type toshiba_state = { 0 };
static struct msm_panel_common_pdata *lcdc_toshiba_pdata;

static void toshiba_spi_write_byte(char dc, uint8 data)
{
	uint32 bit;
	int bnum;

	gpio_set_value(spi_sclk, 0); /* clk low */
	/* dc: 0 for command, 1 for parameter */
	gpio_set_value(spi_mosi, dc);
	udelay(1);	/* at least 20 ns */
	gpio_set_value(spi_sclk, 1); /* clk high */
	udelay(1);	/* at least 20 ns */
	bnum = 8;	/* 8 data bits */
	bit = 0x80;
	while (bnum) {
		gpio_set_value(spi_sclk, 0); /* clk low */
		if (data & bit)
			gpio_set_value(spi_mosi, 1);
		else
			gpio_set_value(spi_mosi, 0);
		udelay(1);
		gpio_set_value(spi_sclk, 1); /* clk high */
		udelay(1);
		bit >>= 1;
		bnum--;
	}
}

static void toshiba_spi_write(char cmd, uint32 data, int num)
{
	char *bp;

	gpio_set_value(spi_cs, 1);	/* cs high */

	/* command byte first */
	toshiba_spi_write_byte(0, cmd);

	/* followed by parameter bytes */
	if (num) {
		bp = (char *)&data;
		bp += (num - 1);
		while (num) {
			toshiba_spi_write_byte(1, *bp);
			num--;
			bp--;
		}
	}

	gpio_set_value(spi_cs, 0);	/* cs low */
	udelay(1);
}

void toshiba_spi_read_bytes(char cmd, uint32 *data, int num)
{
	uint32 dbit, bits;
	int bnum;

	gpio_set_value(spi_cs, 1);	/* cs high */

	/* command byte first */
	toshiba_spi_write_byte(0, cmd);

	if (num > 1) {
		/* extra dc bit */
		gpio_set_value(spi_sclk, 0); /* clk low */
		udelay(1);
		dbit = gpio_get_value(spi_miso);/* dc bit */
		udelay(1);
		gpio_set_value(spi_sclk, 1); /* clk high */
	}

	/* followed by data bytes */
	bnum = num * 8;	/* number of bits */
	bits = 0;
	while (bnum) {
		bits <<= 1;
		gpio_set_value(spi_sclk, 0); /* clk low */
		udelay(1);
		dbit = gpio_get_value(spi_miso);
		udelay(1);
		gpio_set_value(spi_sclk, 1); /* clk high */
		bits |= dbit;
		bnum--;
	}

	*data = bits;

	udelay(1);
	gpio_set_value(spi_cs, 0);	/* cs low */
	udelay(1);
}

static void spi_pin_assign(void)
{
	/* Setting the Default GPIO's */
	spi_sclk = *(lcdc_toshiba_pdata->gpio_num);
	spi_cs   = *(lcdc_toshiba_pdata->gpio_num + 1);
	spi_mosi  = *(lcdc_toshiba_pdata->gpio_num + 2);
	spi_miso  = *(lcdc_toshiba_pdata->gpio_num + 3);
}

static void toshiba_disp_powerup(void)
{
	if (!toshiba_state.disp_powered_up && !toshiba_state.display_on) {
		/* Reset the hardware first */
		/* Include DAC power up implementation here */
	      toshiba_state.disp_powered_up = TRUE;
	}
}

static void toshiba_disp_on(void)
{
	uint32	data;

	gpio_set_value(spi_cs, 0);	/* low */
	gpio_set_value(spi_sclk, 1);	/* high */
	gpio_set_value(spi_mosi, 0);
	gpio_set_value(spi_miso, 0);

	if (toshiba_state.disp_powered_up && !toshiba_state.display_on) {
		toshiba_spi_write(0, 0, 0);
		mdelay(7);
		toshiba_spi_write(0, 0, 0);
		mdelay(7);
		toshiba_spi_write(0, 0, 0);
		mdelay(7);
		toshiba_spi_write(0xba, 0x11, 1);
		toshiba_spi_write(0x36, 0x00, 1);
		mdelay(1);
		toshiba_spi_write(0x3a, 0x60, 1);
		toshiba_spi_write(0xb1, 0x5d, 1);
		mdelay(1);
		toshiba_spi_write(0xb2, 0x33, 1);
		toshiba_spi_write(0xb3, 0x22, 1);
		mdelay(1);
		toshiba_spi_write(0xb4, 0x02, 1);
		toshiba_spi_write(0xb5, 0x1e, 1); /* vcs -- adjust brightness */
		mdelay(1);
		toshiba_spi_write(0xb6, 0x27, 1);
		toshiba_spi_write(0xb7, 0x03, 1);
		mdelay(1);
		toshiba_spi_write(0xb9, 0x24, 1);
		toshiba_spi_write(0xbd, 0xa1, 1);
		mdelay(1);
		toshiba_spi_write(0xbb, 0x00, 1);
		toshiba_spi_write(0xbf, 0x01, 1);
		mdelay(1);
		toshiba_spi_write(0xbe, 0x00, 1);
		toshiba_spi_write(0xc0, 0x11, 1);
		mdelay(1);
		toshiba_spi_write(0xc1, 0x11, 1);
		toshiba_spi_write(0xc2, 0x11, 1);
		mdelay(1);
		toshiba_spi_write(0xc3, 0x3232, 2);
		mdelay(1);
		toshiba_spi_write(0xc4, 0x3232, 2);
		mdelay(1);
		toshiba_spi_write(0xc5, 0x3232, 2);
		mdelay(1);
		toshiba_spi_write(0xc6, 0x3232, 2);
		mdelay(1);
		toshiba_spi_write(0xc7, 0x6445, 2);
		mdelay(1);
		toshiba_spi_write(0xc8, 0x44, 1);
		toshiba_spi_write(0xc9, 0x52, 1);
		mdelay(1);
		toshiba_spi_write(0xca, 0x00, 1);
		mdelay(1);
		toshiba_spi_write(0xec, 0x02a4, 2);	/* 0x02a4 */
		mdelay(1);
		toshiba_spi_write(0xcf, 0x01, 1);
		mdelay(1);
		toshiba_spi_write(0xd0, 0xc003, 2);	/* c003 */
		mdelay(1);
		toshiba_spi_write(0xd1, 0x01, 1);
		mdelay(1);
		toshiba_spi_write(0xd2, 0x0028, 2);
		mdelay(1);
		toshiba_spi_write(0xd3, 0x0028, 2);
		mdelay(1);
		toshiba_spi_write(0xd4, 0x26a4, 2);
		mdelay(1);
		toshiba_spi_write(0xd5, 0x20, 1);
		mdelay(1);
		toshiba_spi_write(0xef, 0x3200, 2);
		mdelay(32);
		toshiba_spi_write(0xbc, 0x80, 1);	/* wvga pass through */
		toshiba_spi_write(0x3b, 0x00, 1);
		mdelay(1);
		toshiba_spi_write(0xb0, 0x16, 1);
		mdelay(1);
		toshiba_spi_write(0xb8, 0xfff5, 2);
		mdelay(1);
		toshiba_spi_write(0x11, 0, 0);
		mdelay(5);
		toshiba_spi_write(0x29, 0, 0);
		mdelay(5);
		toshiba_state.display_on = TRUE;
	}

	data = 0;
	toshiba_spi_read_bytes(0x04, &data, 3);
	printk(KERN_INFO "toshiba_disp_on: id=%x\n", data);

}

static int lcdc_toshiba_panel_on(struct platform_device *pdev)
{
	if (!toshiba_state.disp_initialized) {
		/* Configure reset GPIO that drives DAC */
		if (lcdc_toshiba_pdata->panel_config_gpio)
			lcdc_toshiba_pdata->panel_config_gpio(1);
		toshiba_disp_powerup();
		toshiba_disp_on();
		toshiba_state.disp_initialized = TRUE;
	}
	return 0;
}

static int lcdc_toshiba_panel_off(struct platform_device *pdev)
{
	if (toshiba_state.disp_powered_up && toshiba_state.display_on) {
		/* Main panel power off (Deep standby in) */

		toshiba_spi_write(0x28, 0, 0);	/* display off */
		mdelay(1);
		toshiba_spi_write(0xb8, 0x8002, 2);	/* output control */
		mdelay(1);
		toshiba_spi_write(0x10, 0x00, 1);	/* sleep mode in */
		mdelay(85);		/* wait 85 msec */
		toshiba_spi_write(0xb0, 0x00, 1);	/* deep standby in */
		mdelay(1);
		if (lcdc_toshiba_pdata->panel_config_gpio)
			lcdc_toshiba_pdata->panel_config_gpio(0);
		toshiba_state.display_on = FALSE;
		toshiba_state.disp_initialized = FALSE;
	}
	return 0;
}

static void lcdc_toshiba_set_backlight(struct msm_fb_data_type *mfd)
{
	int bl_level;
	int ret = -EPERM;

	bl_level = mfd->bl_level;
	ret = pmic_set_led_intensity(LED_LCD, bl_level);

	if (ret)
		printk(KERN_WARNING "%s: can't set lcd backlight!\n",
				__func__);
}

static int __init toshiba_probe(struct platform_device *pdev)
{
	if (pdev->id == 0) {
		lcdc_toshiba_pdata = pdev->dev.platform_data;
		spi_pin_assign();
		return 0;
	}
	msm_fb_add_device(pdev);
	return 0;
}

static struct platform_driver this_driver = {
	.probe  = toshiba_probe,
	.driver = {
		.name   = "lcdc_toshiba_wvga",
	},
};

static struct msm_fb_panel_data toshiba_panel_data = {
	.on = lcdc_toshiba_panel_on,
	.off = lcdc_toshiba_panel_off,
	.set_backlight = lcdc_toshiba_set_backlight,
};

static struct platform_device this_device = {
	.name   = "lcdc_toshiba_wvga",
	.id	= 1,
	.dev	= {
		.platform_data = &toshiba_panel_data,
	}
};

static int __init lcdc_toshiba_panel_init(void)
{
	int ret;
	struct msm_panel_info *pinfo;
#ifdef CONFIG_FB_MSM_TRY_MDDI_CATCH_LCDC_PRISM
	if (mddi_get_client_id() != 0)
		return 0;

	ret = msm_fb_detect_client("lcdc_toshiba_wvga_pt");
	if (ret)
		return 0;

#endif

	ret = platform_driver_register(&this_driver);
	if (ret)
		return ret;

	pinfo = &toshiba_panel_data.panel_info;
	pinfo->xres = 480;
	pinfo->yres = 800;
	pinfo->type = LCDC_PANEL;
	pinfo->pdest = DISPLAY_1;
	pinfo->wait_cycle = 0;
	pinfo->bpp = 18;
	pinfo->fb_num = 2;
	/* 30Mhz mdp_lcdc_pclk and mdp_lcdc_pad_pcl */
	pinfo->clk_rate = 27648000;
	pinfo->bl_max = 15;
	pinfo->bl_min = 1;

	pinfo->lcdc.h_back_porch = 184;	/* hsw = 8 + hbp=184 */
	pinfo->lcdc.h_front_porch = 4;
	pinfo->lcdc.h_pulse_width = 8;
	pinfo->lcdc.v_back_porch = 2;	/* vsw=1 + vbp = 2 */
	pinfo->lcdc.v_front_porch = 3;
	pinfo->lcdc.v_pulse_width = 1;
	pinfo->lcdc.border_clr = 0;     /* blk */
	pinfo->lcdc.underflow_clr = 0xff;       /* blue */
	pinfo->lcdc.hsync_skew = 0;

	ret = platform_device_register(&this_device);
	if (ret)
		platform_driver_unregister(&this_driver);

	return ret;
}

device_initcall(lcdc_toshiba_panel_init);
