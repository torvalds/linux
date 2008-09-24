/*
 * LCD panel support for the Palm Zire71
 *
 * Original version : Romain Goyet
 * Current version : Laurent Gonzalez
 * Modified for zire71 : Marek Vasut
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
#include <linux/io.h>

#include <mach/omapfb.h>

static int palmz71_panel_init(struct lcd_panel *panel,
			      struct omapfb_device *fbdev)
{
	return 0;
}

static void palmz71_panel_cleanup(struct lcd_panel *panel)
{

}

static int palmz71_panel_enable(struct lcd_panel *panel)
{
	return 0;
}

static void palmz71_panel_disable(struct lcd_panel *panel)
{
}

static unsigned long palmz71_panel_get_caps(struct lcd_panel *panel)
{
	return OMAPFB_CAPS_SET_BACKLIGHT;
}

struct lcd_panel palmz71_panel = {
	.name		= "palmz71",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_HSYNC | OMAP_LCDC_HSVS_RISING_EDGE |
			  OMAP_LCDC_HSVS_OPPOSITE,
	.data_lines	= 16,
	.bpp		= 16,
	.pixel_clock	= 24000,
	.x_res		= 320,
	.y_res		= 320,
	.hsw		= 4,
	.hfp		= 8,
	.hbp		= 28,
	.vsw		= 1,
	.vfp		= 8,
	.vbp		= 7,
	.pcd		= 0,

	.init		= palmz71_panel_init,
	.cleanup	= palmz71_panel_cleanup,
	.enable		= palmz71_panel_enable,
	.disable	= palmz71_panel_disable,
	.get_caps	= palmz71_panel_get_caps,
};

static int palmz71_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&palmz71_panel);
	return 0;
}

static int palmz71_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int palmz71_panel_suspend(struct platform_device *pdev,
				 pm_message_t mesg)
{
	return 0;
}

static int palmz71_panel_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver palmz71_panel_driver = {
	.probe		= palmz71_panel_probe,
	.remove		= palmz71_panel_remove,
	.suspend	= palmz71_panel_suspend,
	.resume		= palmz71_panel_resume,
	.driver		= {
		.name	= "lcd_palmz71",
		.owner	= THIS_MODULE,
	},
};

static int palmz71_panel_drv_init(void)
{
	return platform_driver_register(&palmz71_panel_driver);
}

static void palmz71_panel_drv_cleanup(void)
{
	platform_driver_unregister(&palmz71_panel_driver);
}

module_init(palmz71_panel_drv_init);
module_exit(palmz71_panel_drv_cleanup);
