// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/notifier.h>

#include <linux/gunyah/gh_rm_drv.h>

#define GH_GUEST_POPS_POFF_BUTTON_HOLD_SHUTDOWN_DELAY_MS	1000
#define GH_GUEST_POPS_POFF_BUTTON_HOLD_RESTART_DELAY_MS		500

static struct input_dev *gh_vm_poff_input;
static struct notifier_block rm_nb;

static struct kobj_type gh_guest_kobj_type = {
	.sysfs_ops = &kobj_sysfs_ops,
};
static struct kobject gh_guest_kobj;

static int gh_guest_pops_handle_stop_shutdown(u32 stop_reason)
{
	/* Emulate a KEY_POWER event to notify user-space of a shutdown */
	pr_info("Sending KEY_POWER event\n");

	input_report_key(gh_vm_poff_input, KEY_POWER, 1);
	input_sync(gh_vm_poff_input);

	switch (stop_reason) {
	case GH_VM_STOP_SHUTDOWN:
		msleep(GH_GUEST_POPS_POFF_BUTTON_HOLD_SHUTDOWN_DELAY_MS);
		break;
	case GH_VM_STOP_RESTART:
		msleep(GH_GUEST_POPS_POFF_BUTTON_HOLD_RESTART_DELAY_MS);
		break;
	}

	input_report_key(gh_vm_poff_input, KEY_POWER, 0);
	input_sync(gh_vm_poff_input);

	return 0;
}

static int gh_guest_pops_handle_stop_crash(void)
{
	panic("Panic requested by Primary-VM\n");
	return 0;
}

static int
gh_guest_pops_vm_shutdown(struct gh_rm_notif_vm_shutdown_payload *vm_shutdown)
{
	switch (vm_shutdown->stop_reason) {
	case GH_VM_STOP_SHUTDOWN:
		return gh_guest_pops_handle_stop_shutdown(vm_shutdown->stop_reason);
	case GH_VM_STOP_CRASH:
		return gh_guest_pops_handle_stop_crash();
	case GH_VM_STOP_RESTART:
		return gh_guest_pops_handle_stop_shutdown(vm_shutdown->stop_reason);
	}

	return 0;
}

static int gh_guest_pops_rm_notifer_fn(struct notifier_block *nb,
					unsigned long cmd, void *data)
{
	switch (cmd) {
	case GH_RM_NOTIF_VM_SHUTDOWN:
		return gh_guest_pops_vm_shutdown(data);
	}

	return NOTIFY_DONE;
}

static int __init gh_guest_pops_init_poff(void)
{
	int ret;

	gh_vm_poff_input = input_allocate_device();
	if (!gh_vm_poff_input)
		return -ENOMEM;

	input_set_capability(gh_vm_poff_input, EV_KEY, KEY_POWER);

	ret = input_register_device(gh_vm_poff_input);
	if (ret)
		goto fail_register;

	rm_nb.notifier_call = gh_guest_pops_rm_notifer_fn;
	ret = gh_rm_register_notifier(&rm_nb);
	if (ret)
		goto fail_init;

	return 0;

fail_init:
	input_unregister_device(gh_vm_poff_input);
fail_register:
	input_free_device(gh_vm_poff_input);
	return ret;
}

static ssize_t gh_guest_set_app_status(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret = 0;
	u16 app_status;

	ret = kstrtou16(buf, 0, &app_status);
	if (ret)
		return -EINVAL;

	if (app_status != GH_RM_APP_STATUS_TUI_SERVICE_BOOT)
		return -EINVAL;

	ret = gh_rm_vm_set_app_status(app_status);
	if (ret) {
		pr_err("Failed to set the APP status\n");
		return -EINVAL;
	}

	return count;
}

static struct kobj_attribute gh_guest_attribute =
__ATTR(app_status, 0220, NULL, gh_guest_set_app_status);

static struct attribute *attrs[] = {
	&gh_guest_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static int gh_guest_sysfs_init(void)
{
	int ret = 0;

	ret = kobject_init_and_add(&gh_guest_kobj, &gh_guest_kobj_type,
			kernel_kobj, "%s", "vm_set_status");
	if (ret) {
		pr_err("sysfs create and add failed\n");
		ret = -ENOMEM;
		goto error_return;
	}

	ret = sysfs_create_group(&gh_guest_kobj, &attr_group);
	if (ret) {
		pr_err("sysfs create group failed %d\n", ret);
		goto error_return;
	}

	return 0;

error_return:
	if (kobject_name(&gh_guest_kobj) != NULL) {
		kobject_del(&gh_guest_kobj);
		kobject_put(&gh_guest_kobj);
		memset(&gh_guest_kobj, 0, sizeof(gh_guest_kobj));
	}

	return ret;
}

static void gh_guest_sysfs_cleanup(void)
{
	sysfs_remove_group(&gh_guest_kobj, &attr_group);
	kobject_del(&gh_guest_kobj);
	kobject_put(&gh_guest_kobj);
	memset(&gh_guest_kobj, 0, sizeof(gh_guest_kobj));
}

static void gh_guest_pops_exit_poff(void)
{
	gh_rm_unregister_notifier(&rm_nb);

	input_unregister_device(gh_vm_poff_input);
	input_free_device(gh_vm_poff_input);
}

static int __init gh_guest_pops_init(void)
{
	int ret;

	ret = gh_guest_pops_init_poff();
	if (ret) {
		pr_err("Failed to initialize guest poweroff driver\n");
		return 0;
	}

	ret = gh_guest_sysfs_init();
	if (ret) {
		pr_err("Failed to init sysfs of guest_pops\n");
		gh_guest_pops_exit_poff();
		return 0;
	}

	ret = gh_rm_vm_set_os_status(GH_RM_OS_STATUS_BOOT);
	if (ret) {
		pr_err("Failed to set the OS status\n");
		gh_guest_pops_exit_poff();
		gh_guest_sysfs_cleanup();
	}

	return 0;
}
module_init(gh_guest_pops_init);

static void __exit gh_guest_pops_exit(void)
{
	gh_guest_pops_exit_poff();
	gh_guest_sysfs_cleanup();
}
module_exit(gh_guest_pops_exit);

MODULE_LICENSE("GPL");
