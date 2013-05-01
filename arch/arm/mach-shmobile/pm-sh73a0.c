/*
 * sh73a0 Power management support
 *
 *  Copyright (C) 2012 Bastian Hecht <hechtb+renesas@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/suspend.h>
#include <mach/common.h>

#ifdef CONFIG_SUSPEND
static int sh73a0_enter_suspend(suspend_state_t suspend_state)
{
	cpu_do_idle();
	return 0;
}

static void sh73a0_suspend_init(void)
{
	shmobile_suspend_ops.enter = sh73a0_enter_suspend;
}
#else
static void sh73a0_suspend_init(void) {}
#endif

void __init sh73a0_pm_init(void)
{
	sh73a0_suspend_init();
}
