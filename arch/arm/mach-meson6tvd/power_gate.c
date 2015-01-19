/*
 * arch/arm/mach-meson6tv/power_gate.c
 *
 * Copyright (C) 2011-2013 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <mach/am_regs.h>
#include <mach/power_gate.h>
#include <mach/mod_gate.h>

unsigned char GCLK_ref[GCLK_IDX_MAX];
EXPORT_SYMBOL(GCLK_ref);


int video_dac_enable(unsigned char enable_mask)
{
	switch_mod_gate_by_name("venc", 1);
	CLEAR_CBUS_REG_MASK(VENC_VDAC_SETTING, enable_mask & 0x1f);
	return 0;
}
EXPORT_SYMBOL(video_dac_enable);

int video_dac_disable()
{
	SET_CBUS_REG_MASK(VENC_VDAC_SETTING, 0x1f);
	switch_mod_gate_by_name("venc", 0);
	return 0;
}
EXPORT_SYMBOL(video_dac_disable);

