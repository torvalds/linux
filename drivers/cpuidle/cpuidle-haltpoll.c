// SPDX-License-Identifier: GPL-2.0
/*
 * cpuidle driver for haltpoll governor.
 *
 * Copyright 2019 Red Hat, Inc. and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Authors: Marcelo Tosatti <mtosatti@redhat.com>
 */

#include <linux/init.h>
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/sched/idle.h>
#include <linux/kvm_para.h>

static int default_enter_idle(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv, int index)
{
	if (current_clr_polling_and_test()) {
		local_irq_enable();
		return index;
	}
	default_idle();
	return index;
}

static struct cpuidle_driver haltpoll_driver = {
	.name = "haltpoll",
	.owner = THIS_MODULE,
	.states = {
		{ /* entry 0 is for polling */ },
		{
			.enter			= default_enter_idle,
			.exit_latency		= 1,
			.target_residency	= 1,
			.power_usage		= -1,
			.name			= "haltpoll idle",
			.desc			= "default architecture idle",
		},
	},
	.safe_state_index = 0,
	.state_count = 2,
};

static int __init haltpoll_init(void)
{
	struct cpuidle_driver *drv = &haltpoll_driver;

	cpuidle_poll_state_init(drv);

	if (!kvm_para_available())
		return 0;

	return cpuidle_register(&haltpoll_driver, NULL);
}

static void __exit haltpoll_exit(void)
{
	cpuidle_unregister(&haltpoll_driver);
}

module_init(haltpoll_init);
module_exit(haltpoll_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marcelo Tosatti <mtosatti@redhat.com>");
