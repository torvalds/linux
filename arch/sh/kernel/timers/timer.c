/*
 * arch/sh/kernel/timers/timer.c - Common timer code
 *
 *  Copyright (C) 2005  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <asm/timer.h>

static struct sys_timer *sys_timers[] = {
#ifdef CONFIG_SH_TMU
	&tmu_timer,
#endif
#ifdef CONFIG_SH_MTU2
	&mtu2_timer,
#endif
#ifdef CONFIG_SH_CMT
	&cmt_timer,
#endif
	NULL,
};

static char timer_override[10];
static int __init timer_setup(char *str)
{
	if (str)
		strlcpy(timer_override, str, sizeof(timer_override));
	return 1;
}
__setup("timer=", timer_setup);

struct sys_timer *get_sys_timer(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sys_timers); i++) {
		struct sys_timer *t = sys_timers[i];

		if (unlikely(!t))
			break;
		if (unlikely(timer_override[0]))
			if ((strcmp(timer_override, t->name) != 0))
				continue;
		if (likely(t->ops->init() == 0))
			return t;
	}

	return NULL;
}
