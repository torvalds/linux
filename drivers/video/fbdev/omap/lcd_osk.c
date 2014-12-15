/*
 * LCD panel support for the TI OMAP OSK board
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 * Adapted for OSK by <dirk.behme@de.bosch.com>
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

#include <asm/gpio.h>

#include <mach/hardware.h>
#include <mach/mux.h>

#include "omapfb.h"

static int osk_panel_init(struct lcd_panel *panel, struct omapfb_device *fbdev)
{
	/* gpio2 was allocated in board init */
	return 0;
}

static void osk_panel_cleanup(struct lcd_panel *panel)
{
}

static int osk_panel_enable(struct lcd_panel *panel)
{
	/* configure PWL pin */
	omap_cfg_reg(PWL);

	/* Enable PWL unit */
	omap_writeb(0x01, OMAP_PWL_CLK_ENABLE);

	/* Set PWL level */
	omap_writeb(0xFF, OMAP_PWL_ENABLE);

	/* set GPIO2 high (lcd power enabled) */
	gpio_set_value(2, 1);

	return 0;
}

static void osk_panel_disable(struct lcd_panel *panel)
{
	/* Set PWL level to zero */
	omap_writeb(0x00, OMAP_PWL_ENABLE);

	/* Disable PWL unit */
	omap_writeb(0x00, OMAP_PWL_CLK_ENABLE);

	/* set GPIO2 low */
	gpio_set_value(2, 0);
}

static unsigned long osk_panel_get_caps(struct lcd_panel *panel)
{
	return 0;
}

struct lcd_panel osk_panel = {
	.name		= "osk",
	.config		= OMAP_LCDC_PANEL_TFT,

	.bpp		= 16,
	.data_lines	= 16,
	.x_res		= 240,
	.y_res		= 320,
	.pixel_clock	= 12500,
	.hsw		= 40,
	.hfp		= 40,
	.hbp		= 72,
	.vsw		= 1,
	.vfp		= 1,
	.vbp		= 0,
	.pcd		= 12,

	.init		= osk_panel_init,
	.cleanup	= osk_panel_cleanup,
	.enable		= osk_panel_enable,
	.disable	= osk_panel_disable,
	.get_caps	= osk_panel_get_caps,
};

static int osk_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&osk_panel);
	return 0;
}

static int osk_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int osk_panel_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	return 0;
}

static int osk_panel_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver osk_panel_driver = {
	.probe		= osk_panel_probe,
	.remove		= osk_panel_remove,
	.suspend	= osk_panel_suspend,
	.resume		= osk_panel_resume,
	.driver		= {
		.name	= "lcd_osk",
	},
};

module_platform_driver(osk_panel_driver);
