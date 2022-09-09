/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HiSilicon Clock and Reset Driver Header
 *
 * Copyright (c) 2016 HiSilicon Limited.
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
