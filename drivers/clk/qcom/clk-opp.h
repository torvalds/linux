/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#ifndef __QCOM_CLK_OPP_H__
#define __QCOM_CLK_OPP_H__

void clk_hw_populate_clock_opp_table(struct device_node *np,
							struct clk_hw *hw);

#define MAX_LEN_OPP_HANDLE	100
#define LEN_OPP_HANDLE		16

#endif
