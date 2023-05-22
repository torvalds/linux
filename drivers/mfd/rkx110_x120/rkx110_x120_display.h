/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co. Ltd.
 *
 * Author: Zhang Yubing <yubing.zhang@rock-chips.com>
 */

#ifndef _RKX110_RKX120_DISPLAY_H
#define _RKX110_RKX120_DISPLAY_H

int rk_serdes_display_route_prepare(struct rk_serdes *serdes, struct rk_serdes_route *route);
int rk_serdes_display_route_enable(struct rk_serdes *serdes, struct rk_serdes_route *route);
int rk_serdes_display_route_disable(struct rk_serdes *serdes, struct rk_serdes_route *route);
int rk_serdes_display_route_unprepare(struct rk_serdes *serdes, struct rk_serdes_route *route);
int rk_serdes_display_route_init(struct  rk_serdes *serdes);
int rk_serdes_display_video_start(struct rk_serdes *serdes,
					 struct rk_serdes_route *route, bool enable);

#endif
