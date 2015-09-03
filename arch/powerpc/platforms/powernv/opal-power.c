/*
 * PowerNV OPAL power control for graceful shutdown handling
 *
 * Copyright 2015 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/notifier.h>

#include <asm/opal.h>
#include <asm/machdep.h>

#define SOFT_OFF 0x00
#define SOFT_REBOOT 0x01

static int opal_power_control_event(struct notifier_block *nb,
				    unsigned long msg_type, void *msg)
{
	struct opal_msg *power_msg = msg;
	uint64_t type;

	type = be64_to_cpu(power_msg->params[0]);

	switch (type) {
	case SOFT_REBOOT:
		pr_info("OPAL: reboot requested\n");
		orderly_reboot();
		break;
	case SOFT_OFF:
		pr_info("OPAL: poweroff requested\n");
		orderly_poweroff(true);
		break;
	default:
		pr_err("OPAL: power control type unexpected %016llx\n", type);
	}

	return 0;
}

static struct notifier_block opal_power_control_nb = {
	.notifier_call	= opal_power_control_event,
	.next		= NULL,
	.priority	= 0,
};

static int __init opal_power_control_init(void)
{
	int ret;

	ret = opal_message_notifier_register(OPAL_MSG_SHUTDOWN,
					     &opal_power_control_nb);
	if (ret) {
		pr_err("%s: Can't register OPAL event notifier (%d)\n",
				__func__, ret);
		return ret;
	}

	return 0;
}
machine_subsys_initcall(powernv, opal_power_control_init);
