/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Chen Shunqing <csq@rock-chips.com>
 */

#ifndef COMBRXPHY_H
#define COMBRXPHY_H

#define COMBRX_REG(x)			((x) + 0x10000)

int rk628_combrxphy_power_on(struct rk628 *rk628, int f);
int rk628_combrxphy_power_off(struct rk628 *rk628);

#endif
