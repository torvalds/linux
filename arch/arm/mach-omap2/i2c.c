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

#include <plat/cpu.h>
#include <plat/i2c.h>
#include <plat/mux.h>

#include "mux.h"

void __init omap2_i2c_mux_pins(int bus_id)
{
	if (cpu_is_omap24xx()) {
		const int omap24xx_pins[][2] = {
			{ M19_24XX_I2C1_SCL, L15_24XX_I2C1_SDA },
			{ J15_24XX_I2C2_SCL, H19_24XX_I2C2_SDA },
		};
		int scl, sda;

		scl = omap24xx_pins[bus_id - 1][0];
		sda = omap24xx_pins[bus_id - 1][1];
		omap_cfg_reg(sda);
		omap_cfg_reg(scl);
	}

	/* First I2C bus is not muxable */
	if (cpu_is_omap34xx() && bus_id > 1) {
		char mux_name[sizeof("i2c2_scl.i2c2_scl")];

		sprintf(mux_name, "i2c%i_scl.i2c%i_scl", bus_id, bus_id);
		omap_mux_init_signal(mux_name, OMAP_PIN_INPUT);
		sprintf(mux_name, "i2c%i_sda.i2c%i_sda", bus_id, bus_id);
		omap_mux_init_signal(mux_name, OMAP_PIN_INPUT);
	}
}
