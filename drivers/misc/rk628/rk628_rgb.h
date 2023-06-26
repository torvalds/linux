/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#ifndef RK628_RGB_H
#define RK628_RGB_H
#include "rk628.h"

void rk628_rgb_rx_enable(struct rk628 *rk628);
void rk628_rgb_tx_enable(struct rk628 *rk628);
void rk628_rgb_tx_disable(struct rk628 *rk628);
void rk628_bt1120_rx_enable(struct rk628 *rk628);
void rk628_bt1120_tx_enable(struct rk628 *rk628);
#endif
