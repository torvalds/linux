/*
 * Purna Chandra Mandal,<purna.mandal@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#ifndef __MICROCHIP_CLK_PIC32_H_
#define __MICROCHIP_CLK_PIC32_H_

#include <linux/clk-provider.h>

/* PIC32 clock data */
struct pic32_clk_common {
	struct device *dev;
	void __iomem *iobase;
	spinlock_t reg_lock; /* clock lock */
};

/* System PLL clock */
struct pic32_sys_pll_data {
	struct clk_init_data init_data;
	const u32 ctrl_reg;
	const u32 status_reg;
	const u32 lock_mask;
};

/* System clock */
struct pic32_sys_clk_data {
	struct clk_init_data init_data;
	const u32 mux_reg;
	const u32 slew_reg;
	const u32 *parent_map;
	const u32 slew_div;
};

/* Reference Oscillator clock */
struct pic32_ref_osc_data {
	struct clk_init_data init_data;
	const u32 ctrl_reg;
	const u32 *parent_map;
};

/* Peripheral Bus clock */
struct pic32_periph_clk_data {
	struct clk_init_data init_data;
	const u32 ctrl_reg;
};

/* External Secondary Oscillator clock  */
struct pic32_sec_osc_data {
	struct clk_init_data init_data;
	const u32 enable_reg;
	const u32 status_reg;
	const u32 enable_mask;
	const u32 status_mask;
	const unsigned long fixed_rate;
};

extern const struct clk_ops pic32_pbclk_ops;
extern const struct clk_ops pic32_sclk_ops;
extern const struct clk_ops pic32_sclk_no_div_ops;
extern const struct clk_ops pic32_spll_ops;
extern const struct clk_ops pic32_roclk_ops;
extern const struct clk_ops pic32_sosc_ops;

struct clk *pic32_periph_clk_register(const struct pic32_periph_clk_data *data,
				      struct pic32_clk_common *core);
struct clk *pic32_refo_clk_register(const struct pic32_ref_osc_data *data,
				    struct pic32_clk_common *core);
struct clk *pic32_sys_clk_register(const struct pic32_sys_clk_data *data,
				   struct pic32_clk_common *core);
struct clk *pic32_spll_clk_register(const struct pic32_sys_pll_data *data,
				    struct pic32_clk_common *core);
struct clk *pic32_sosc_clk_register(const struct pic32_sec_osc_data *data,
				    struct pic32_clk_common *core);

#endif /* __MICROCHIP_CLK_PIC32_H_*/
