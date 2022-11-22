/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 */

#ifndef SERDES_COMBPHY_H
#define SERDES_COMBPHY_H

int serdes_combphy_write(struct rk_serdes *serdes, u8 remote_id, u32 reg, u32 val);
int serdes_combphy_read(struct rk_serdes *serdes, u8 remote_id, u32 reg, u32 *val);
int serdes_combphy_update_bits(struct rk_serdes *serdes, u8 remote_id,
			       u32 reg, u32 mask, u32 val);
void serdes_combphy_get_default_config(u64 hs_clk_rate,
				       struct configure_opts_combphy *cfg);

void rkx110_combrxphy_set_mode(struct rk_serdes *ser, enum combrx_phy_mode mode);
void rkx110_combrxphy_set_rate(struct rk_serdes *ser, u64 rate);
void rkx110_combrxphy_power_on(struct rk_serdes *ser, enum comb_phy_id id);
void rkx110_combrxphy_power_off(struct rk_serdes *ser, enum comb_phy_id id);

void rkx120_combtxphy_set_mode(struct rk_serdes *des, enum combtx_phy_mode mode);
void rkx120_combtxphy_set_rate(struct rk_serdes *des, u64 rate);
u64 rkx120_combtxphy_get_rate(struct rk_serdes *des);
void rkx120_combtxphy_power_on(struct rk_serdes *des, u8 remote_id, u8 phy_id);
void rkx120_combtxphy_power_off(struct rk_serdes *des, u8 remote_id);
#endif

