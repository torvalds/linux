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

#include <plat/i2c.h>

#include "twl-common.h"

static struct i2c_board_info __initdata pmic_i2c_board_info = {
	.addr		= 0x48,
	.flags		= I2C_CLIENT_WAKE,
};

void __init omap_pmic_init(int bus, u32 clkrate,
			   const char *pmic_type, int pmic_irq,
			   struct twl4030_platform_data *pmic_data)
{
	strncpy(pmic_i2c_board_info.type, pmic_type,
		sizeof(pmic_i2c_board_info.type));
	pmic_i2c_board_info.irq = pmic_irq;
	pmic_i2c_board_info.platform_data = pmic_data;

	omap_register_i2c_bus(bus, clkrate, &pmic_i2c_board_info, 1);
}
