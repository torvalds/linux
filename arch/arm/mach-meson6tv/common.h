/*
 * arch/arm/mach-meson6tv/common.h
 *
 * Copyright (C) 2013 Amlogic, Inc.
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

/* time.c */
extern void __init meson6tv_timer_init(void);

/* headsmp.S */
extern void meson_secondary_startup(void);

/* clock.c */
extern void meson_set_cpu_ctrl_reg(int value);

/* pinctrl.c */
extern unsigned p_pull_up_addr[];
extern unsigned p_pin_mux_reg_addr[];

