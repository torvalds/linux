/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#ifndef _RKX110_DSI_RX_H
#define _RKX110_DSI_RX_H

void rkx110_dsi_rx_enable(struct rk_serdes *ser, struct rk_serdes_route *route, int id);
void rkx110_dsi_rx_disable(struct rk_serdes *ser, struct rk_serdes_route *route, int id);

#endif
