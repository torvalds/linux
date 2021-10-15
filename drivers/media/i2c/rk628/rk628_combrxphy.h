/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen csq@rock-chips.com>
 */

#ifndef _RK628_COMBRXPHY_H
#define _RK628_COMBRXPHY_H

#define COMBRX_REG(x)			((x) + 0x10000)

int rk628_rxphy_power_on(struct rk628 *rk628, int f);
int rk628_rxphy_power_off(struct rk628 *rk628);

#endif
