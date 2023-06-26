/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#ifndef RK628_PINCTRL_H
#define RK628_PINCTRL_H

int rk628_misc_pinctrl_set_mux(struct rk628 *rk628, int gpio, int mux);
int rk628_misc_gpio_get_value(struct rk628 *rk628, int gpio);
int rk628_misc_gpio_set_value(struct rk628 *rk628, int gpio, int value);
int rk628_misc_gpio_set_direction(struct rk628 *rk628, int gpio, int direction);
int rk628_misc_gpio_direction_input(struct rk628 *rk628, int gpio);
int rk628_misc_gpio_direction_output(struct rk628 *rk628, int gpio, int value);
int rk628_misc_gpio_set_pull_highz_up_down(struct rk628 *rk628, int gpio, int pull);

#endif // RK628_PINCTRL_H
