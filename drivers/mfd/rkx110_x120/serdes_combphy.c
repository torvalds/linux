// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include <linux/kernel.h>
#include "rkx110_x120.h"
#include "serdes_combphy.h"

int serdes_combphy_write(struct rk_serdes *serdes, u8 remote_id, u32 reg, u32 val)
{
	struct i2c_client *client = serdes->chip[remote_id].client;

	return serdes->i2c_write_reg(client, reg, val);
}

int serdes_combphy_read(struct rk_serdes *serdes, u8 remote_id, u32 reg, u32 *val)
{
	struct i2c_client *client = serdes->chip[remote_id].client;

	return serdes->i2c_read_reg(client, reg, val);
}

int serdes_combphy_update_bits(struct rk_serdes *serdes, u8 remote_id,
			       u32 reg, u32 mask, u32 val)
{
	struct i2c_client *client = serdes->chip[remote_id].client;

	return serdes->i2c_update_bits(client, reg, mask, val);
}

void serdes_combphy_get_default_config(u64 hs_clk_rate,
				       struct configure_opts_combphy *cfg)
{
	unsigned long long ui;

	ui = ALIGN(NSEC_PER_SEC, hs_clk_rate);
	do_div(ui, hs_clk_rate);

	cfg->clk_miss = 0;
	cfg->clk_post = 60 + 52 * ui;
	cfg->clk_pre = 8;
	cfg->clk_prepare = 38;
	cfg->clk_settle = 95;
	cfg->clk_term_en = 0;
	cfg->clk_trail = 60;
	cfg->clk_zero = 262;
	cfg->d_term_en = 0;
	cfg->eot = 0;
	cfg->hs_exit = 100;
	cfg->hs_prepare = 40 + 4 * ui;
	cfg->hs_zero = 105 + 6 * ui;
	cfg->hs_settle = 85 + 6 * ui;
	cfg->hs_skip = 40;

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
	cfg->hs_trail = max(4 * 8 * ui, 60 + 4 * 4 * ui);

	/*
	 * Note that TINIT is considered a protocol-dependent parameter, and
	 * thus the exact requirements for TINIT,MASTER and TINIT,SLAVE (transmitter
	 * and receiver initialization Stop state lengths, respectively,) are defined
	 * by the protocol layer specification and are outside the scope of this document.
	 * However, the D-PHY specification does place a minimum bound on the lengths of
	 * TINIT,MASTER and TINIT,SLAVE, which each shall be no less than 100 µs. A protocol
	 * layer specification using the D-PHY specification may specify any values greater
	 * than this limit, for example, TINIT,MASTER ≥ 1 ms and TINIT,SLAVE = 500 to 800 µs
	 */
	cfg->init = NSEC_PER_SEC / MSEC_PER_SEC;
	cfg->lpx = 50;
	cfg->ta_get = 5 * cfg->lpx;
	cfg->ta_go = 4 * cfg->lpx;
	cfg->ta_sure = cfg->lpx;
	cfg->wakeup = NSEC_PER_SEC / MSEC_PER_SEC;
}
