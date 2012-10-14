/*
 * arch/arm/plat-sunxi/include/plat/script.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#ifndef _SUNXI_PLAT_SCRIPT_H
#define _SUNXI_PLAT_SCRIPT_H

#define SYS_CONFIG_MEMBASE	(PLAT_PHYS_OFFSET + SZ_32M + SZ_16M)
#define SYS_CONFIG_MEMSIZE	(SZ_64K)

struct sunxi_script_section {
	char name[32];
	u32 count;
	u32 offset;
};

struct sunxi_script_property {
	char name[32];
	u32 offset;
	u32 pattern;
};

struct sunxi_script {
	u32 count;
	u32 version[3];
	struct sunxi_script_section section[];
};

struct sunxi_script_gpio_value {
	u32 port;
	u32 port_num;
	s32 mul_sel;
	s32 pull;
	s32 drv_level;
	s32 data;
};

enum sunxi_script_property_type {
	SUNXI_SCRIPT_PROP_TYPE_INVALID = 0,
	SUNXI_SCRIPT_PROP_TYPE_U32,
	SUNXI_SCRIPT_PROP_TYPE_STRING,
	SUNXI_SCRIPT_PROP_TYPE_U32_ARRAY,
	SUNXI_SCRIPT_PROP_TYPE_GPIO,
	SUNXI_SCRIPT_PROP_TYPE_NULL,
};

#endif
