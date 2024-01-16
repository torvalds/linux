// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PowerNV OPAL power control for graceful shutdown handling
 *
 * Copyright 2015 IBM Corp.
 */

#define pr_fmt(fmt)	"opal-power: "	fmt

#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/of.h>

#include <asm/opal.h>
#include <asm/machdep.h>

#define SOFT_OFF 0x00
#define SOFT_REBOOT 0x01

/* Detect EPOW event */
static bool detect_epow(void)
{
	u16 epow;
	int i, rc;
	__be16 epow_classes;
	__be16 opal_epow_status[OPAL_SYSEPOW_MAX] = {0};

	/*
	* Check for EPOW event. Kernel sends supported EPOW classes info
	* to OPAL. OPAL returns EPOW info along with classes present.
	*/
	epow_classes = cpu_to_be16(OPAL_SYSEPOW_MAX);
	rc = opal_get_epow_status(opal_epow_status, &epow_classes);
	if (rc != OPAL_SUCCESS) {
		pr_err("Failed to get EPOW event information\n");
		return false;
	}

	/* Look for EPOW events present */
	for (i = 0; i < be16_to_cpu(epow_classes); i++) {
		epow = be16_to_cpu(opal_epow_status[i]);

		/* Filter events which do not need shutdown. */
		if (i == OPAL_SYSEPOW_POWER)
			epow &= ~(OPAL_SYSPOWER_CHNG | OPAL_SYSPOWER_FAIL |
					OPAL_SYSPOWER_INCL);
		if (epow)
			return true;
	}

	return false;
}

/* Check for existing EPOW, DPO events */
static bool __init poweroff_pending(void)
{
	int rc;
	__be64 opal_dpo_timeout;

	/* Check for DPO event */
	rc = opal_get_dpo_status(&opal_dpo_timeout);
	if (rc == OPAL_SUCCESS) {
		pr_info("Existing DPO event detected.\n");
		return true;
	}

	/* Check for EPOW event */
	if (detect_epow()) {
		pr_info("Existing EPOW event detected.\n");
		return true;
	}

	return false;
}

/* OPAL power-control events notifier */
static int opal_power_control_event(struct notifier_block *nb,
					unsigned long msg_type, void *msg)
{
	uint64_t type;

	switch (msg_type) {
	case OPAL_MSG_EPOW:
		if (detect_epow()) {
			pr_info("EPOW msg received. Powering off system\n");
			orderly_poweroff(true);
		}
		break;
	case OPAL_MSG_DPO:
		pr_info("DPO msg received. Powering off system\n");
		orderly_poweroff(true);
		break;
	case OPAL_MSG_SHUTDOWN:
		type = be64_to_cpu(((struct opal_msg *)msg)->params[0]);
		switch (type) {
		case SOFT_REBOOT:
			pr_info("Reboot requested\n");
			orderly_reboot();
			break;
		case SOFT_OFF:
			pr_info("Poweroff requested\n");
			orderly_poweroff(true);
			break;
		default:
			pr_err("Unknown power-control type %llu\n", type);
		}
		break;
	default:
		pr_err("Unknown OPAL message type %lu\n", msg_type);
	}

	return 0;
}

/* OPAL EPOW event notifier block */
static struct notifier_block opal_epow_nb = {
	.notifier_call	= opal_power_control_event,
	.next		= NULL,
	.priority	= 0,
};

/* OPAL DPO event notifier block */
static struct notifier_block opal_dpo_nb = {
	.notifier_call	= opal_power_control_event,
	.next		= NULL,
	.priority	= 0,
};

/* OPAL power-control event notifier block */
static struct notifier_block opal_power_control_nb = {
	.notifier_call	= opal_power_control_event,
	.next		= NULL,
	.priority	= 0,
};

int __init opal_power_control_init(void)
{
	int ret, supported = 0;
	struct device_node *np;

	/* Register OPAL power-control events notifier */
	ret = opal_message_notifier_register(OPAL_MSG_SHUTDOWN,
						&opal_power_control_nb);
	if (ret)
		pr_err("Failed to register SHUTDOWN notifier, ret = %d\n", ret);

	/* Determine OPAL EPOW, DPO support */
	np = of_find_node_by_path("/ibm,opal/epow");
	if (np) {
		supported = of_device_is_compatible(np, "ibm,opal-v3-epow");
		of_node_put(np);
	}

	if (!supported)
		return 0;
	pr_info("OPAL EPOW, DPO support detected.\n");

	/* Register EPOW event notifier */
	ret = opal_message_notifier_register(OPAL_MSG_EPOW, &opal_epow_nb);
	if (ret)
		pr_err("Failed to register EPOW notifier, ret = %d\n", ret);

	/* Register DPO event notifier */
	ret = opal_message_notifier_register(OPAL_MSG_DPO, &opal_dpo_nb);
	if (ret)
		pr_err("Failed to register DPO notifier, ret = %d\n", ret);

	/* Check for any pending EPOW or DPO events. */
	if (poweroff_pending())
		orderly_poweroff(true);

	return 0;
}
