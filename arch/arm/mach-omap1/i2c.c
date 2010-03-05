/*
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2009 Nokia Corporation.
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

#include <plat/i2c.h>
#include <plat/mux.h>
#include <plat/cpu.h>

void __init omap1_i2c_mux_pins(int bus_id)
{
	if (cpu_is_omap7xx()) {
		omap_cfg_reg(I2C_7XX_SDA);
		omap_cfg_reg(I2C_7XX_SCL);
	} else {
		omap_cfg_reg(I2C_SDA);
		omap_cfg_reg(I2C_SCL);
	}
}
