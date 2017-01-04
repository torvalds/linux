/*
 * HiSilicon Clock and Reset Driver Header
 *
 * Copyright (c) 2016 HiSilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __HISI_CRG_H
#define __HISI_CRG_H

struct hisi_clock_data;
struct hisi_reset_controller;

struct hisi_crg_funcs {
	struct hisi_clock_data*	(*register_clks)(struct platform_device *pdev);
	void (*unregister_clks)(struct platform_device *pdev);
};

struct hisi_crg_dev {
	struct hisi_clock_data *clk_data;
	struct hisi_reset_controller *rstc;
	const struct hisi_crg_funcs *funcs;
};

#endif	/* __HISI_CRG_H */
