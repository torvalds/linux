/*
 * LCD panel support for the MISTRAL OMAP2EVM board
 *
 * Author: Arun C <arunedarath@mistralsolutions.com>
 *
 * Derived from drivers/video/omap/lcd_omap3evm.c
 * Derived from drivers/video/omap/lcd-apollon.c
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
#include <linux/gpio.h>
#include <linux/i2c/twl4030.h>

#include <mach/mux.h>
#include <mach/omapfb.h>
#include <asm/mach-types.h>

#define LCD_PANEL_ENABLE_GPIO	154
#define LCD_PANEL_LR		128
#define LCD_PANEL_UD		129
#define LCD_PANEL_INI		152
#define LCD_PANEL_QVGA		148
#define LCD_PANEL_RESB		153

#define TWL_LED_LEDEN		0x00
#define TWL_PWMA_PWMAON		0x00
#define TWL_PWMA_PWMAOFF	0x01

static unsigned int bklight_level;

static int omap2evm_panel_init(struct lcd_panel *panel,
				struct omapfb_device *fbdev)
{
	gpio_request(LCD_PANEL_ENABLE_GPIO, "LCD enable");
	gpio_request(LCD_PANEL_LR, "LCD lr");
	gpio_request(LCD_PANEL_UD, "LCD ud");
	gpio_request(LCD_PANEL_INI, "LCD ini");
	gpio_request(LCD_PANEL_QVGA, "LCD qvga");
	gpio_request(LCD_PANEL_RESB, "LCD resb");

	gpio_direction_output(LCD_PANEL_ENABLE_GPIO, 1);
	gpio_direction_output(LCD_PANEL_RESB, 1);
	gpio_direction_output(LCD_PANEL_INI, 1);
	gpio_direction_output(LCD_PANEL_QVGA, 0);
	gpio_direction_output(LCD_PANEL_LR, 1);
	gpio_direction_output(LCD_PANEL_UD, 1);

	twl4030_i2c_write_u8(TWL4030_MODULE_LED, 0x11, TWL_LED_LEDEN);
	twl4030_i2c_write_u8(TWL4030_MODULE_PWMA, 0x01, TWL_PWMA_PWMAON);
	twl4030_i2c_write_u8(TWL4030_MODULE_PWMA, 0x02, TWL_PWMA_PWMAOFF);
	bklight_level = 100;

	return 0;
}

static void omap2evm_panel_cleanup(struct lcd_panel *panel)
{
	gpio_free(LCD_PANEL_RESB);
	gpio_free(LCD_PANEL_QVGA);
	gpio_free(LCD_PANEL_INI);
	gpio_free(LCD_PANEL_UD);
	gpio_free(LCD_PANEL_LR);
	gpio_free(LCD_PANEL_ENABLE_GPIO);
}

static int omap2evm_panel_enable(struct lcd_panel *panel)
{
	gpio_set_value(LCD_PANEL_ENABLE_GPIO, 0);
	return 0;
}

static void omap2evm_panel_disable(struct lcd_panel *panel)
{
	gpio_set_value(LCD_PANEL_ENABLE_GPIO, 1);
}

static unsigned long omap2evm_panel_get_caps(struct lcd_panel *panel)
{
	return 0;
}

static int omap2evm_bklight_setlevel(struct lcd_panel *panel,
						unsigned int level)
{
	u8 c;
	if ((level >= 0) && (level <= 100)) {
		c = (125 * (100 - level)) / 100 + 2;
		twl4030_i2c_write_u8(TWL4030_MODULE_PWMA, c, TWL_PWMA_PWMAOFF);
		bklight_level = level;
	}
	return 0;
}

static unsigned int omap2evm_bklight_getlevel(struct lcd_panel *panel)
{
	return bklight_level;
}

static unsigned int omap2evm_bklight_getmaxlevel(struct lcd_panel *panel)
{
	return 100;
}

struct lcd_panel omap2evm_panel = {
	.name		= "omap2evm",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_HSYNC,

	.bpp		= 16,
	.data_lines	= 18,
	.x_res		= 480,
	.y_res		= 640,
	.hsw		= 3,
	.hfp		= 0,
	.hbp		= 28,
	.vsw		= 2,
	.vfp		= 1,
	.vbp		= 0,

	.pixel_clock	= 20000,

	.init		= omap2evm_panel_init,
	.cleanup	= omap2evm_panel_cleanup,
	.enable		= omap2evm_panel_enable,
	.disable	= omap2evm_panel_disable,
	.get_caps	= omap2evm_panel_get_caps,
	.set_bklight_level      = omap2evm_bklight_setlevel,
	.get_bklight_level      = omap2evm_bklight_getlevel,
	.get_bklight_max        = omap2evm_bklight_getmaxlevel,
};

static int omap2evm_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&omap2evm_panel);
	return 0;
}

static int omap2evm_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int omap2evm_panel_suspend(struct platform_device *pdev,
				   pm_message_t mesg)
{
	return 0;
}

static int omap2evm_panel_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver omap2evm_panel_driver = {
	.probe		= omap2evm_panel_probe,
	.remove		= omap2evm_panel_remove,
	.suspend	= omap2evm_panel_suspend,
	.resume		= omap2evm_panel_resume,
	.driver		= {
		.name	= "omap2evm_lcd",
		.owner	= THIS_MODULE,
	},
};

static int __init omap2evm_panel_drv_init(void)
{
	return platform_driver_register(&omap2evm_panel_driver);
}

static void __exit omap2evm_panel_drv_exit(void)
{
	platform_driver_unregister(&omap2evm_panel_driver);
}

module_init(omap2evm_panel_drv_init);
module_exit(omap2evm_panel_drv_exit);
