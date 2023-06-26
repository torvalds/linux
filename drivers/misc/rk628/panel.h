/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */
#ifndef _PANEL_H
#define _PANEL_H

#include "rk628.h"

int rk628_panel_info_get(struct rk628 *rk628, struct device_node *np);
void rk628_panel_prepare(struct rk628 *rk628);
void rk628_panel_enable(struct rk628 *rk628);
void rk628_panel_unprepare(struct rk628 *rk628);
void rk628_panel_disable(struct rk628 *rk628);
#endif

