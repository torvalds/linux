// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PPS dummy generator
 *
 * Copyright (C) 2024 Rodolfo Giometti <giometti@enneenne.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/random.h>
#include <linux/pps_gen_kernel.h>

static struct pps_gen_device *pps_gen;
static struct timer_list ktimer;

static unsigned int get_random_delay(void)
{
	unsigned int delay = get_random_u8() & 0x0f;

	return (delay + 1) * HZ;
}

/*
 * The kernel timer
 */

static void pps_gen_ktimer_event(struct timer_list *unused)
{
	pps_gen_event(pps_gen, PPS_GEN_EVENT_MISSEDPULSE, NULL);
}

/*
 * PPS Generator methods
 */

static int pps_gen_dummy_get_time(struct pps_gen_device *pps_gen,
					struct timespec64 *time)
{
	struct system_time_snapshot snap;

	ktime_get_snapshot(&snap);
	*time = ktime_to_timespec64(snap.real);

	return 0;
}

static int pps_gen_dummy_enable(struct pps_gen_device *pps_gen, bool enable)
{
	if (enable)
		mod_timer(&ktimer, jiffies + get_random_delay());
	else
		del_timer_sync(&ktimer);

	return 0;
}

/*
 * The PPS info struct
 */

static struct pps_gen_source_info pps_gen_dummy_info = {
	.use_system_clock	= true,
	.get_time		= pps_gen_dummy_get_time,
	.enable			= pps_gen_dummy_enable,
};

/*
 * Module staff
 */

static void __exit pps_gen_dummy_exit(void)
{
	del_timer_sync(&ktimer);
	pps_gen_unregister_source(pps_gen);
}

static int __init pps_gen_dummy_init(void)
{
	pps_gen = pps_gen_register_source(&pps_gen_dummy_info);
	if (IS_ERR(pps_gen))
		return PTR_ERR(pps_gen);

	timer_setup(&ktimer, pps_gen_ktimer_event, 0);

	return 0;
}

module_init(pps_gen_dummy_init);
module_exit(pps_gen_dummy_exit);

MODULE_AUTHOR("Rodolfo Giometti <giometti@enneenne.com>");
MODULE_DESCRIPTION("LinuxPPS dummy generator");
MODULE_LICENSE("GPL");
