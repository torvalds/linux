// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/uaccess.h>
#include <linux/pm.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <asm/arch_timer.h>
#include <asm/div64.h>
#include <linux/silent_mode.h>

static BLOCKING_NOTIFIER_HEAD(pm_silentmode_chain);

static atomic_t pm_silentmode_userspace_cntrl =
					ATOMIC_INIT(USERSPACE_CONTROL_DISABLED);

/* silent mode is dependent on BootArgs + HW state*/
static atomic_t pm_silent_mode_enable =
					ATOMIC_INIT(MODE_NON_SILENT);

static atomic_t pm_silentmode_hw_state =
					ATOMIC_INIT(MODE_GPIO_LOW);

static struct kobject *silent_boot_kobj;

/*
 * On the kernel command line specify silent_boot.mode=<mode>
 * to set mode in silent mode module.
 */
static char *mode = "nonsilent";
static int silent_mode;
static int silent_mode_param_set(const char *, const struct kernel_param *);
static int silent_mode_param_get(char *, const struct kernel_param *);
static const struct kernel_param_ops silent_mode_param_ops = {
	.set = silent_mode_param_set,
	.get = silent_mode_param_get,
};
module_param_cb(mode, &silent_mode_param_ops, &mode, 0644);

static int silent_mode_param_set(const char *val, const struct kernel_param *kp)
{
	if (!silent_mode) {
		int len = strlen("forcednonsilent");
		char *s = strstrip((char *)val);

		if (strnstr(s, "forcednonsilent", len)) {
			silent_mode = -MODE_NON_SILENT;
		} else if (strnstr(s, "forcedsilent", len)) {
			silent_mode = -MODE_SILENT;
		} else if (strnstr(s, "nonsilent", len)) {
			silent_mode = MODE_NON_SILENT;
		} else if (strnstr(s, "silent", len)) {
			silent_mode = MODE_SILENT;
		} else {
			silent_mode = MODE_NON_SILENT;
			pr_debug("silent_mode: No string found: NON_SILENT\n");
		}
	}

	pr_debug("Silent Mode set to %d\n", silent_mode);
	return 0;
}

static int silent_mode_param_get(char *buf, const struct kernel_param *kp)
{
	return scnprintf(buf, strlen(mode)+1, "%s\n", mode);
}

/*
 * pm_silentmode_kernel_set: Check if userspace is controlling this node
 * else go ahead and update the node.
 */
static int pm_silentmode_kernel_set(int val)
{
	if (!atomic_read(&pm_silentmode_userspace_cntrl)) {
		pr_debug("%s: Kernel Control sysfs\n", __func__);
		atomic_set(&pm_silent_mode_enable, val);
		return 0;
	}
	pr_debug("%s: Userspace Controls sysfs\n", __func__);
	return -USERSPACE_CONTROL_ENABLED;
}

static int pm_silentmode_kernel_get(void)
{
	return atomic_read(&pm_silent_mode_enable);
}

static void  pm_silentmode_hw_state_set(int val)
{
	atomic_set(&pm_silentmode_hw_state, val);
}

int pm_silentmode_hw_state_get(void)
{
	return atomic_read(&pm_silentmode_hw_state);
}
EXPORT_SYMBOL(pm_silentmode_hw_state_get);

/*
 * External Function to be called by drivers interested in checking
 * silent boot mode value and registering a callback if in silent mode
 * else return pm_silent_mode_enable value
 */
int register_pm_silentmode_notifier(struct notifier_block *nb)
{
	int lsilent = pm_silentmode_kernel_get();

	if (lsilent == MODE_SILENT) {
		blocking_notifier_chain_register(&pm_silentmode_chain, nb);
		pr_debug("%s: ....Registered\n", __func__);
	}
	return lsilent;
}
EXPORT_SYMBOL(register_pm_silentmode_notifier);

/*
 * External function used for unregistering the callback to the silent mode
 * driver for notification
 */
int unregister_pm_silentmode_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister
					(&pm_silentmode_chain, nb);
}
EXPORT_SYMBOL(unregister_pm_silentmode_notifier);

static void pm_silent_mode_cb_chain(void)
{
	pr_debug("%s: SilentMode notify\n", __func__);
	blocking_notifier_call_chain(&pm_silentmode_chain, 1, NULL);
}

void pm_silentmode_user_update(struct kobject *kobj, int val)
{
	if (kobj != NULL) {
		atomic_set(&pm_silentmode_userspace_cntrl,
						USERSPACE_CONTROL_ENABLED);
		atomic_set(&pm_silent_mode_enable, val);
		sysfs_notify(kobj, NULL, "pm_silentmode_kernel_state");
	}
}

int  pm_silentmode_update(int val, struct kobject *kobj, bool us)
{
	if (us) {
		pm_silentmode_user_update(kobj, val);
		return 0;
	}

	pr_debug("%s:Driver update to sysfs\n", __func__);
	pm_silentmode_kernel_set(val ^ 1);
	pm_silentmode_hw_state_set((val ^ 1));
	sysfs_notify(silent_boot_kobj, NULL, "pm_silentmode_kernel_state");
	sysfs_notify(silent_boot_kobj, NULL, "pm_silentmode_hw_state");
	pm_silent_mode_cb_chain();

	return 0;
}
EXPORT_SYMBOL(pm_silentmode_update);

int pm_silentmode_status(void)
{
	return pm_silentmode_kernel_get();
}
EXPORT_SYMBOL(pm_silentmode_status);

int pm_silentmode_get_mode(void)
{
	return silent_mode;
}
EXPORT_SYMBOL(pm_silentmode_get_mode);

#define silentmode_attr(_name) \
static struct kobj_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = 0664,			\
	},					\
	.show	= _name##_show,			\
	.store	= _name##_store,		\
}

static ssize_t pm_silentmode_kernel_state_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", pm_silentmode_status());
}

static ssize_t pm_silentmode_kernel_state_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t n)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	pm_silentmode_update(val, kobj, 1);

	return n;
}

silentmode_attr(pm_silentmode_kernel_state);

static ssize_t pm_silentmode_hw_state_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
					pm_silentmode_hw_state_get());
}

static ssize_t pm_silentmode_hw_state_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t n)
{
	return -EINVAL;
}
silentmode_attr(pm_silentmode_hw_state);

static struct attribute *g[] = {
	&pm_silentmode_kernel_state_attr.attr,
	&pm_silentmode_hw_state_attr.attr,
	NULL,
};


static const struct attribute_group attr_group = {
	.attrs = g,
};

static int __init pm_silentmode_init(void)
{
	int error;

	pr_debug("%s: Silent Boot Mode Entered\n", __func__);
	/* 1. Set based on Parse Command Line arguments */
	if (!silent_mode)
		silent_mode = MODE_NON_SILENT;

	if (silent_mode == MODE_SILENT || silent_mode == -MODE_SILENT) {
		pm_silentmode_hw_state_set(MODE_GPIO_HIGH);
		pm_silentmode_kernel_set(1);
	} else {
		pm_silentmode_hw_state_set(MODE_GPIO_LOW);
		pm_silentmode_kernel_set(0);
		pr_debug("%s: Silent Boot Mode disabled\n", __func__);
	}

	silent_boot_kobj = kobject_create_and_add("silent_boot", kernel_kobj);
	if (!silent_boot_kobj) {
		pr_err("%s: Failed to add silent_boot_kobj\n", __func__);
		return -ENOMEM;
	}

	/* 2. Append sysfs entries under /sys/kernel/ */
	error = sysfs_create_group(silent_boot_kobj, &attr_group);
	if (error) {
		pr_err("%s: failed to create sysfs %d\n", __func__, error);
		return error;
	}

	return 0;
}
postcore_initcall(pm_silentmode_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. PM Silent Mode driver");
MODULE_LICENSE("GPL");
