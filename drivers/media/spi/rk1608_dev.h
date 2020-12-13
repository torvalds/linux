/* SPDX-License-Identifier: GPL-2.0 */
/**
 * Rockchip rk1608 driver
 *
 * Copyright (C) 2017-2018 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __RK1608_DEV_H__
#define __RK1608_DEV_H__

#include "rk1608_core.h"

int rk1608_dev_register(struct rk1608_state *pdata);
void rk1608_dev_unregister(struct rk1608_state *pdata);
void rk1608_dev_receive_msg(struct rk1608_state *pdata, struct msg *msg);
#endif
