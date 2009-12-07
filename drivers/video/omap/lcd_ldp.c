/*
 * LCD panel support for the TI LDP board
 *
 * Copyright (C) 2007 WindRiver
 * Author: Stanley Miao <stanley.miao@windriver.com>
 *
 * Derived from drivers/video/omap/lcd-2430sdp.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/i2c/twl4030.h>

#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/omapfb.h>
#include <asm/mach-types.h>

#define LCD_PANEL_BACKLIGHT_GPIO	(15 + OMAP_MAX_GPIO_LINES)
#define LCD_PANEL_ENABLE_GPIO		(7 + OMAP_MAX_GPIO_LINES)

#define LCD_PANEL_RESET_GPIO		55
#define LCD_PANEL_QVGA_GPIO		56

#ifdef CONFIG_FB_OMAP_LCD_VGA
#define LCD_XRES		480
#define LCD_YRES		640
#define LCD_PIXCLOCK_MAX	41700
#else
#define LCD_XRES		240
#define LCD_YRES		320
#define LCD_PIXCLOCK_MAX	185186
#endif

#define PM_RECEIVER             TWL4030_MODULE_PM_RECEIVER
#define ENABLE_VAUX2_DEDICATED  0x09
#define ENABLE_VAUX2_DEV_GRP    0x20
#define ENABLE_VAUX3_DEDICATED	0x03
#define ENABLE_VAUX3_DEV_GRP	0x20

#define ENABLE_VPLL2_DEDICATED          0x05
#define ENABLE_VPLL2_DEV_GRP            0xE0
#define TWL4030_VPLL2_DEV_GRP           0x33
#define TWL4030_VPLL2_DEDICATED         0x36

#define t2_out(c, r, v) twl4030_i2c_write_u8(c, r, v)


static int ldp_panel_init(struct lcd_panel *panel,
				struct omapfb_device *fbdev)
{
	gpio_request(LCD_PANEL_RESET_GPIO, "lcd reset");
	gpio_request(LCD_PANEL_QVGA_GPIO, "lcd qvga");
	gpio_request(LCD_PANEL_ENABLE_GPIO, "lcd panel");
	gpio_request(LCD_PANEL_BACKLIGHT_GPIO, "lcd backlight");

	gpio_direction_output(LCD_PANEL_QVGA_GPIO, 0);
	gpio_direction_output(LCD_PANEL_RESET_GPIO, 0);
	gpio_direction_output(LCD_PANEL_ENABLE_GPIO, 0);
	gpio_direction_output(LCD_PANEL_BACKLIGHT_GPIO, 0);

#ifdef CONFIG_FB_OMAP_LCD_VGA
	gpio_set_value(LCD_PANEL_QVGA_GPIO, 0);
#else
	gpio_set_value(LCD_PANEL_QVGA_GPIO, 1);
#endif
	gpio_set_value(LCD_PANEL_RESET_GPIO, 1);

	return 0;
}

static void ldp_panel_cleanup(struct lcd_panel *panel)
{
	gpio_free(LCD_PANEL_BACKLIGHT_GPIO);
	gpio_free(LCD_PANEL_ENABLE_GPIO);
	gpio_free(LCD_PANEL_QVGA_GPIO);
	gpio_free(LCD_PANEL_RESET_GPIO);
}

static int ldp_panel_enable(struct lcd_panel *panel)
{
	if (0 != t2_out(PM_RECEIVER, ENABLE_VPLL2_DEDICATED,
			TWL4030_VPLL2_DEDICATED))
		return -EIO;
	if (0 != t2_out(PM_RECEIVER, ENABLE_VPLL2_DEV_GRP,
			TWL4030_VPLL2_DEV_GRP))
		return -EIO;

	gpio_direction_output(LCD_PANEL_ENABLE_GPIO, 1);
	gpio_direction_output(LCD_PANEL_BACKLIGHT_GPIO, 1);

	if (0 != t2_out(PM_RECEIVER, ENABLE_VAUX3_DEDICATED,
				TWL4030_VAUX3_DEDICATED))
		return -EIO;
	if (0 != t2_out(PM_RECEIVER, ENABLE_VAUX3_DEV_GRP,
				TWL4030_VAUX3_DEV_GRP))
		return -EIO;

	return 0;
}

static void ldp_panel_disable(struct lcd_panel *panel)
{
	gpio_direction_output(LCD_PANEL_ENABLE_GPIO, 0);
	gpio_direction_output(LCD_PANEL_BACKLIGHT_GPIO, 0);

	t2_out(PM_RECEIVER, 0x0, TWL4030_VPLL2_DEDICATED);
	t2_out(PM_RECEIVER, 0x0, TWL4030_VPLL2_DEV_GRP);
	msleep(4);
}

static unsigned long ldp_panel_get_caps(struct lcd_panel *panel)
{
	return 0;
}

struct lcd_panel ldp_panel = {
	.name		= "ldp",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_HSYNC,

	.bpp		= 16,
	.data_lines	= 18,
	.x_res		= LCD_XRES,
	.y_res		= LCD_YRES,
	.hsw		= 3,		/* hsync_len (4) - 1 */
	.hfp		= 3,		/* right_margin (4) - 1 */
	.hbp		= 39,		/* left_margin (40) - 1 */
	.vsw		= 1,		/* vsync_len (2) - 1 */
	.vfp		= 2,		/* lower_margin */
	.vbp		= 7,		/* upper_margin (8) - 1 */

	.pixel_clock	= LCD_PIXCLOCK_MAX,

	.init		= ldp_panel_init,
	.cleanup	= ldp_panel_cleanup,
	.enable		= ldp_panel_enable,
	.disable	= ldp_panel_disable,
	.get_caps	= ldp_panel_get_caps,
};

static int ldp_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&ldp_panel);
	return 0;
}

static int ldp_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int ldp_panel_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	return 0;
}

static int ldp_panel_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver ldp_panel_driver = {
	.probe		= ldp_panel_probe,
	.remove		= ldp_panel_remove,
	.suspend	= ldp_panel_suspend,
	.resume		= ldp_panel_resume,
	.driver		= {
		.name	= "ldp_lcd",
		.owner	= THIS_MODULE,
	},
};

static int __init ldp_panel_drv_init(void)
{
	return platform_driver_register(&ldp_panel_driver);
}

static void __exit ldp_panel_drv_exit(void)
{
	platform_driver_unregister(&ldp_panel_driver);
}

module_init(ldp_panel_drv_init);
module_exit(ldp_panel_drv_exit);
