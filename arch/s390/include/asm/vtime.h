/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _S390_VTIME_H
#define _S390_VTIME_H

static inline void update_timer_sys(void)
{
	get_lowcore()->system_timer += get_lowcore()->last_update_timer - get_lowcore()->exit_timer;
	get_lowcore()->user_timer += get_lowcore()->exit_timer - get_lowcore()->sys_enter_timer;
	get_lowcore()->last_update_timer = get_lowcore()->sys_enter_timer;
}

static inline void update_timer_mcck(void)
{
	get_lowcore()->system_timer += get_lowcore()->last_update_timer - get_lowcore()->exit_timer;
	get_lowcore()->user_timer += get_lowcore()->exit_timer - get_lowcore()->mcck_enter_timer;
	get_lowcore()->last_update_timer = get_lowcore()->mcck_enter_timer;
}

#endif /* _S390_VTIME_H */
