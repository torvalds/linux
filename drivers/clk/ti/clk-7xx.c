/*
 * DRA7 Clock init
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo (t-kristo@ti.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk/ti.h>
#include <dt-bindings/clock/dra7.h>

#include "clock.h"

#define DRA7_DPLL_GMAC_DEFFREQ				1000000000
#define DRA7_DPLL_USB_DEFFREQ				960000000

int __init dra7xx_dt_clk_init(void)
{
	int rc;
	struct clk *dpll_ck, *hdcp_ck;

	ti_dt_clocks_register(dra7xx_compat_clks);

	omap2_clk_disable_autoidle_all();

	ti_clk_add_aliases();

	dpll_ck = clk_get_sys(NULL, "dpll_gmac_ck");
	rc = clk_set_rate(dpll_ck, DRA7_DPLL_GMAC_DEFFREQ);
	if (rc)
		pr_err("%s: failed to configure GMAC DPLL!\n", __func__);

	dpll_ck = clk_get_sys(NULL, "dpll_usb_ck");
	rc = clk_set_rate(dpll_ck, DRA7_DPLL_USB_DEFFREQ);
	if (rc)
		pr_err("%s: failed to configure USB DPLL!\n", __func__);

	dpll_ck = clk_get_sys(NULL, "dpll_usb_m2_ck");
	rc = clk_set_rate(dpll_ck, DRA7_DPLL_USB_DEFFREQ/2);
	if (rc)
		pr_err("%s: failed to set USB_DPLL M2 OUT\n", __func__);

	hdcp_ck = clk_get_sys(NULL, "dss_deshdcp_clk");
	rc = clk_prepare_enable(hdcp_ck);
	if (rc)
		pr_err("%s: failed to set dss_deshdcp_clk\n", __func__);

	return rc;
}
