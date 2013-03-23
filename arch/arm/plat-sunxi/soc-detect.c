/*
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/io.h>

#include <asm/mach-types.h>

#include <plat/system.h>

enum sw_ic_ver sw_get_ic_ver(void)
{
	volatile u32 val = readl(SW_VA_TIMERC_IO_BASE + 0x13c);

	val = (val >> 6) & 0x3;

	if (machine_is_sun4i()) {
		switch (val) {
		case 0x00:
			return SUNXI_VER_A10A;
		case 0x03:
			return SUNXI_VER_A10B;
		default:
			return SUNXI_VER_A10C;
		}
	} else {
		switch (val) {
		case 0x03:
			return SUNXI_VER_A13B;
		default:
			return SUNXI_VER_A13A;
		}
	}
}
EXPORT_SYMBOL(sw_get_ic_ver);
