/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#ifndef POST_PROCESS_H
#define POST_PROCESS_H

void rk628_post_process_init(struct rk628 *rk628);
void rk628_post_process_enable(struct rk628 *rk628);
void rk628_post_process_disable(struct rk628 *rk628);

#endif
