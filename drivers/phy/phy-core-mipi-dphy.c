/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013 NVIDIA Corporation
 * Copyright (C) 2018 Cadence Design Systems Inc.
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/time64.h>

#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>

/*
 * Minimum D-PHY timings based on MIPI D-PHY specification. Derived
 * from the valid ranges specified in Section 6.9, Table 14, Page 41
 * of the D-PHY specification (v1.2).
 */
int phy_mipi_dphy_get_default_config(unsigned long pixel_clock,
				     unsigned int bpp,
				     unsigned int lanes,
				     struct phy_configure_opts_mipi_dphy *cfg)
{
	unsigned long long hs_clk_rate;
	unsigned long long ui;

	if (!cfg)
		return -EINVAL;

	hs_clk_rate = pixel_clock * bpp;
	do_div(hs_clk_rate, lanes);

	ui = ALIGN(PSEC_PER_SEC, hs_clk_rate);
	do_div(ui, hs_clk_rate);

	cfg->clk_miss = 0;
	cfg->clk_post = 60000 + 52 * ui;
	cfg->clk_pre = 8;
	cfg->clk_prepare = 38000;
	cfg->clk_settle = 95000;
	cfg->clk_term_en = 0;
	cfg->clk_trail = 60000;
	cfg->clk_zero = 262000;
	cfg->d_term_en = 0;
	cfg->eot = 0;
	cfg->hs_exit = 100000;
	cfg->hs_prepare = 40000 + 4 * ui;
	cfg->hs_zero = 105000 + 6 * ui;
	cfg->hs_settle = 85000 + 6 * ui;
	cfg->hs_skip = 40000;

	/*
	 * The MIPI D-PHY specification (Section 6.9, v1.2, Table 14, Page 40)
	 * contains this formula as:
	 *
	 *     T_HS-TRAIL = max(n * 8 * ui, 60 + n * 4 * ui)
	 *
	 * where n = 1 for forward-direction HS mode and n = 4 for reverse-
	 * direction HS mode. There's only one setting and this function does
	 * not parameterize on anything other that ui, so this code will
	 * assumes that reverse-direction HS mode is supported and uses n = 4.
	 */
	cfg->hs_trail = max(4 * 8 * ui, 60000 + 4 * 4 * ui);

	cfg->init = 100;
	cfg->lpx = 60000;
	cfg->ta_get = 5 * cfg->lpx;
	cfg->ta_go = 4 * cfg->lpx;
	cfg->ta_sure = 2 * cfg->lpx;
	cfg->wakeup = 1000;

	cfg->hs_clk_rate = hs_clk_rate;
	cfg->lanes = lanes;

	return 0;
}
EXPORT_SYMBOL(phy_mipi_dphy_get_default_config);

/*
 * Validate D-PHY configuration according to MIPI D-PHY specification
 * (v1.2, Section Section 6.9 "Global Operation Timing Parameters").
 */
int phy_mipi_dphy_config_validate(struct phy_configure_opts_mipi_dphy *cfg)
{
	unsigned long long ui;

	if (!cfg)
		return -EINVAL;

	ui = ALIGN(PSEC_PER_SEC, cfg->hs_clk_rate);
	do_div(ui, cfg->hs_clk_rate);

	if (cfg->clk_miss > 60000)
		return -EINVAL;

	if (cfg->clk_post < (60000 + 52 * ui))
		return -EINVAL;

	if (cfg->clk_pre < 8)
		return -EINVAL;

	if (cfg->clk_prepare < 38000 || cfg->clk_prepare > 95000)
		return -EINVAL;

	if (cfg->clk_settle < 95000 || cfg->clk_settle > 300000)
		return -EINVAL;

	if (cfg->clk_term_en > 38000)
		return -EINVAL;

	if (cfg->clk_trail < 60000)
		return -EINVAL;

	if ((cfg->clk_prepare + cfg->clk_zero) < 300000)
		return -EINVAL;

	if (cfg->d_term_en > (35000 + 4 * ui))
		return -EINVAL;

	if (cfg->eot > (105000 + 12 * ui))
		return -EINVAL;

	if (cfg->hs_exit < 100000)
		return -EINVAL;

	if (cfg->hs_prepare < (40000 + 4 * ui) ||
	    cfg->hs_prepare > (85000 + 6 * ui))
		return -EINVAL;

	if ((cfg->hs_prepare + cfg->hs_zero) < (145000 + 10 * ui))
		return -EINVAL;

	if ((cfg->hs_settle < (85000 + 6 * ui)) ||
	    (cfg->hs_settle > (145000 + 10 * ui)))
		return -EINVAL;

	if (cfg->hs_skip < 40000 || cfg->hs_skip > (55000 + 4 * ui))
		return -EINVAL;

	if (cfg->hs_trail < max(8 * ui, 60000 + 4 * ui))
		return -EINVAL;

	if (cfg->init < 100)
		return -EINVAL;

	if (cfg->lpx < 50000)
		return -EINVAL;

	if (cfg->ta_get != (5 * cfg->lpx))
		return -EINVAL;

	if (cfg->ta_go != (4 * cfg->lpx))
		return -EINVAL;

	if (cfg->ta_sure < cfg->lpx || cfg->ta_sure > (2 * cfg->lpx))
		return -EINVAL;

	if (cfg->wakeup < 1000)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(phy_mipi_dphy_config_validate);
