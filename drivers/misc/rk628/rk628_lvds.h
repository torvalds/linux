/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#ifndef RK628_LVDS_H
#define RK628_LVDS_H

int rk628_lvds_parse(struct rk628 *rk628, struct device_node *lvds_np);
void rk628_lvds_enable(struct rk628 *rk628);
void rk628_lvds_disable(struct rk628 *rk628);

#endif
