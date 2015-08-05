/*
 *
 * (C) COPYRIGHT 2012-2013, 2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#ifndef _KBASE_CPU_VEXPRESS_H_
#define _KBASE_CPU_VEXPRESS_H_

/**
 * Versatile Express implementation of @ref kbase_cpu_clk_speed_func.
 */
int kbase_get_vexpress_cpu_clock_speed(u32 *cpu_clock);

/**
 * kbase_get_platform_logic_tile_type -  determines which LogicTile type 
 * is used by Versatile Express
 *
 * When platform_config build parameter is specified as vexpress, i.e.,
 * platform_config=vexpress, GPU frequency may vary dependent on the
 * particular platform. The GPU frequency depends on the LogicTile type.
 *
 * This function is called by kbase_common_device_init to determine
 * which LogicTile type is used by the platform by reading the HBI value
 * of the daughterboard which holds the LogicTile:
 *
 * 0x192 HBI0192 Virtex-5
 * 0x217 HBI0217 Virtex-6
 * 0x247 HBI0247 Virtex-7
 *
 * Return: HBI value of the logic tile daughterboard, zero if not accessible
 */
u32 kbase_get_platform_logic_tile_type(void);

#endif				/* _KBASE_CPU_VEXPRESS_H_ */
