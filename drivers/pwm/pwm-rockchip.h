/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef _PWM_ROCKCHIP_H_
#define _PWM_ROCKCHIP_H_

#include <linux/pwm.h>

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

#endif
