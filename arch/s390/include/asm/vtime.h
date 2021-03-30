/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _S390_VTIME_H
#define _S390_VTIME_H

#define __ARCH_HAS_VTIME_TASK_SWITCH

static inline void update_timer_sys(void)
{
	S390_lowcore.system_timer += S390_lowcore.last_update_timer - S390_lowcore.exit_timer;
	S390_lowcore.user_timer += S390_lowcore.exit_timer - S390_lowcore.sys_enter_timer;
	S390_lowcore.last_update_timer = S390_lowcore.sys_enter_timer;
}

static inline void update_timer_mcck(void)
{
	S390_lowcore.system_timer += S390_lowcore.last_update_timer - S390_lowcore.exit_timer;
	S390_lowcore.user_timer += S390_lowcore.exit_timer - S390_lowcore.mcck_enter_timer;
	S390_lowcore.last_update_timer = S390_lowcore.mcck_enter_timer;
}

#endif /* _S390_VTIME_H */
