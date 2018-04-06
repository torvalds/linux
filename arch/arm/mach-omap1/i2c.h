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

#ifndef __ARCH_ARM_MACH_OMAP1_I2C_H
#define __ARCH_ARM_MACH_OMAP1_I2C_H

struct i2c_board_info;
struct omap_i2c_bus_platform_data;

int omap_i2c_add_bus(struct omap_i2c_bus_platform_data *i2c_pdata,
			int bus_id);

#if defined(CONFIG_I2C_OMAP) || defined(CONFIG_I2C_OMAP_MODULE)
extern int omap_register_i2c_bus(int bus_id, u32 clkrate,
				 struct i2c_board_info const *info,
				 unsigned len);
extern int omap_register_i2c_bus_cmdline(void);
#else
static inline int omap_register_i2c_bus(int bus_id, u32 clkrate,
				 struct i2c_board_info const *info,
				 unsigned len)
{
	return 0;
}

static inline int omap_register_i2c_bus_cmdline(void)
{
	return 0;
}
#endif

#endif /* __ARCH_ARM_MACH_OMAP1_I2C_H */
