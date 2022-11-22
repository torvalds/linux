/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#ifndef RKX120_DSI_TX_H
#define RKX120_DSI_TX_H

#include "rkx110_x120.h"

int rkx120_dsi_tx_cmd_seq_xfer(struct rk_serdes *des, u8 remote_id,
			       struct panel_cmds *cmds);
void rkx120_dsi_tx_pre_enable(struct rk_serdes *serdes,
			      struct rk_serdes_route *route, u8 remote_id);
void rkx120_dsi_tx_enable(struct rk_serdes *serdes,
			  struct rk_serdes_route *route, u8 remote_id);
void rkx120_dsi_tx_post_disable(struct rk_serdes *serdes,
				struct rk_serdes_route *route, u8 remote_id);
void rkx120_dsi_tx_disable(struct rk_serdes *serdes,
			   struct rk_serdes_route *route, u8 remote_id);
#endif
