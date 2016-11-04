/*
 * twl-common.c
 *
 * Copyright (C) 2011 Texas Instruments, Inc..
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/i2c.h>
#include <linux/i2c/twl.h>
#include <linux/gpio.h>
#include <linux/string.h>
#include <linux/phy/phy.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>

#include "soc.h"
#include "twl-common.h"
#include "pm.h"
#include "voltage.h"
#include "mux.h"

static struct i2c_board_info __initdata pmic_i2c_board_info = {
	.addr		= 0x48,
	.flags		= I2C_CLIENT_WAKE,
};

void __init omap_pmic_late_init(void)
{
	/* Init the OMAP TWL parameters (if PMIC has been registerd) */
	if (!pmic_i2c_board_info.irq)
		return;

	omap3_twl_init();
	omap4_twl_init();
}

#if IS_ENABLED(CONFIG_SND_OMAP_SOC_OMAP_TWL4030)
#include <linux/platform_data/omap-twl4030.h>

/* Commonly used configuration */
static struct omap_tw4030_pdata omap_twl4030_audio_data;

static struct platform_device audio_device = {
	.name		= "omap-twl4030",
	.id		= -1,
};

void omap_twl4030_audio_init(char *card_name,
				    struct omap_tw4030_pdata *pdata)
{
	if (!pdata)
		pdata = &omap_twl4030_audio_data;

	pdata->card_name = card_name;

	audio_device.dev.platform_data = pdata;
	platform_device_register(&audio_device);
}

#else /* SOC_OMAP_TWL4030 */
void omap_twl4030_audio_init(char *card_name,
				    struct omap_tw4030_pdata *pdata)
{
	return;
}
#endif /* SOC_OMAP_TWL4030 */
