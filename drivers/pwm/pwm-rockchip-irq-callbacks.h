/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef _PWM_ROCKCHIP_IRQ_CALLBACKS_H_
#define _PWM_ROCKCHIP_IRQ_CALLBACKS_H_

#include <linux/pwm.h>
#include <linux/pwm-rockchip.h>

static void rockchip_pwm_oneshot_callback(struct pwm_device *pwm, struct pwm_state *state)
{
	/*
	 * If you want to enable oneshot mode again, config and call
	 * pwm_apply_state().
	 *
	 * struct pwm_state new_state;
	 *
	 * pwm_get_state(pwm, &new_state);
	 * new_state.enabled = true;
	 * ......
	 * pwm_apply_state(pwm, &new_state);
	 *
	 */
}

static void rockchip_pwm_wave_middle_callback(struct pwm_device *pwm)
{
	/*
	 * If you want to update the configuration of wave table, set
	 * struct rockchip_pwm_wave_table and call rockchip_pwm_set_wave().
	 *
	 * struct rockchip_pwm_wave_config wave_config;
	 * struct rockchip_pwm_wave_table duty_table;
	 *
	 * //fill the duty table
	 * ......
	 * wave_config.duty_table = &duty_table;
	 * wave_config.enable = true;
	 * rockchip_pwm_set_wave(pwm, &wave_config);
	 *
	 */
}

static void rockchip_pwm_wave_max_callback(struct pwm_device *pwm)
{
	/*
	 * If you want to update the configuration of wave table, set
	 * struct rockchip_pwm_wave_table and call rockchip_pwm_set_wave().
	 *
	 * struct rockchip_pwm_wave_config wave_config;
	 * struct rockchip_pwm_wave_table duty_table;
	 *
	 * //fill the duty table
	 * ......
	 * wave_config.duty_table = &duty_table;
	 * wave_config.enable = true;
	 * rockchip_pwm_set_wave(pwm, &wave_config);
	 *
	 */
}

#endif
