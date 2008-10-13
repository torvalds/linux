/*
 * LCD panel support for the TI OMAP1610 Innovator board
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
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

#include <mach/gpio.h>
#include <mach/omapfb.h>

#define MODULE_NAME	"omapfb-lcd_h3"

static int innovator1610_panel_init(struct lcd_panel *panel,
				    struct omapfb_device *fbdev)
{
	int r = 0;

	if (omap_request_gpio(14)) {
		pr_err(MODULE_NAME ": can't request GPIO 14\n");
		r = -1;
		goto exit;
	}
	if (omap_request_gpio(15)) {
		pr_err(MODULE_NAME ": can't request GPIO 15\n");
		omap_free_gpio(14);
		r = -1;
		goto exit;
	}
	/* configure GPIO(14, 15) as outputs */
	omap_set_gpio_direction(14, 0);
	omap_set_gpio_direction(15, 0);
exit:
	return r;
}

static void innovator1610_panel_cleanup(struct lcd_panel *panel)
{
	omap_free_gpio(15);
	omap_free_gpio(14);
}

static int innovator1610_panel_enable(struct lcd_panel *panel)
{
	/* set GPIO14 and GPIO15 high */
	omap_set_gpio_dataout(14, 1);
	omap_set_gpio_dataout(15, 1);
	return 0;
}

static void innovator1610_panel_disable(struct lcd_panel *panel)
{
	/* set GPIO13, GPIO14 and GPIO15 low */
	omap_set_gpio_dataout(14, 0);
	omap_set_gpio_dataout(15, 0);
}

static unsigned long innovator1610_panel_get_caps(struct lcd_panel *panel)
{
	return 0;
}

struct lcd_panel innovator1610_panel = {
	.name		= "inn1610",
	.config		= OMAP_LCDC_PANEL_TFT,

	.bpp		= 16,
	.data_lines	= 16,
	.x_res		= 320,
	.y_res		= 240,
	.pixel_clock	= 12500,
	.hsw		= 40,
	.hfp		= 40,
	.hbp		= 72,
	.vsw		= 1,
	.vfp		= 1,
	.vbp		= 0,
	.pcd		= 12,

	.init		= innovator1610_panel_init,
	.cleanup	= innovator1610_panel_cleanup,
	.enable		= innovator1610_panel_enable,
	.disable	= innovator1610_panel_disable,
	.get_caps	= innovator1610_panel_get_caps,
};

static int innovator1610_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&innovator1610_panel);
	return 0;
}

static int innovator1610_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int innovator1610_panel_suspend(struct platform_device *pdev,
				       pm_message_t mesg)
{
	return 0;
}

static int innovator1610_panel_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver innovator1610_panel_driver = {
	.probe		= innovator1610_panel_probe,
	.remove		= innovator1610_panel_remove,
	.suspend	= innovator1610_panel_suspend,
	.resume		= innovator1610_panel_resume,
	.driver		= {
		.name	= "lcd_inn1610",
		.owner	= THIS_MODULE,
	},
};

static int innovator1610_panel_drv_init(void)
{
	return platform_driver_register(&innovator1610_panel_driver);
}

static void innovator1610_panel_drv_cleanup(void)
{
	platform_driver_unregister(&innovator1610_panel_driver);
}

module_init(innovator1610_panel_drv_init);
module_exit(innovator1610_panel_drv_cleanup);

