// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s: %s: " fmt, KBUILD_MODNAME, __func__

#include <linux/arm-smccc.h>
#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <soc/qcom/rpm-smd.h>
#include <linux/soc/qcom/qcom_aoss.h>

#include "linux/power_state.h"

#if IS_ENABLED(CONFIG_ARCH_MONACO)
#define DS_ENTRY_SMC_ID		0xC3000924
#else
#define DS_ENTRY_SMC_ID		0xC3000923
#endif

#if IS_ENABLED(CONFIG_MSM_RPM_SMD)
#define RPM_XO_DS_REQ		0x73646f78
#define RPM_XO_DS_ID		0x0
#define RPM_XO_DS_KEY		0x62616e45
#define RPM_XO_DS_ENTER_VALUE		0x0
#define RPM_XO_DS_EXIT_VALUE		0x1
#endif

#define DS_NUM_PARAMETERS	1
#define DS_ENTRY		1
#define DS_EXIT			0

#define POWER_STATS_BASEMINOR		0
#define POWER_STATS_MAX_MINOR		1
#define POWER_STATE_DEVICE_NAME		"power_state"
#define STRING_LEN			32
#define PS_PM_NOTIFIER_PRIORITY		100
#define PS_SSR_NOTIFIER_PRIORITY	0

#define MAX_QMP_MSG_SIZE	96

enum power_states {
	ACTIVE,
	DEEPSLEEP,
	HIBERNATE,
};

static char * const power_state[] = {
	[ACTIVE] = "active",
	[DEEPSLEEP] = "deepsleep",
	[HIBERNATE] = "hibernate",
};

struct subsystem_event_data {
	const char *name;
	enum ps_event_type enter;
	enum ps_event_type exit;
};

static struct subsystem_event_data event_data[] = {
	{ "mpss", MDSP_BEFORE_POWERDOWN, MDSP_AFTER_POWERUP },
	{ "lpass", ADSP_BEFORE_POWERDOWN, ADSP_AFTER_POWERUP },
	{ "cdsp", CDSP_BEFORE_POWERDOWN, CDSP_AFTER_POWERUP },
	{ "cdsp1", CDSP1_BEFORE_POWERDOWN, CDSP1_AFTER_POWERUP },
	{ "gpdsp0", GPDSP0_BEFORE_POWERDOWN, GPDSP0_AFTER_POWERUP },
	{ "gpdsp1", GPDSP1_BEFORE_POWERDOWN, GPDSP1_AFTER_POWERUP },
};

struct subsystem_data {
	struct list_head list;
	const char *name;
	bool ignore_ssr;
	enum ps_event_type enter;
	enum ps_event_type exit;
	phandle rproc_handle;
	void *ssr_handle;
	struct notifier_block ps_ssr_nb;
};

struct power_state_drvdata {
	struct class *ps_class;
	struct device *ps_dev;
	struct cdev ps_cdev;
	dev_t ps_dev_no;
	struct kobject *ps_kobj;
	struct kobj_attribute ps_ka;
	struct kobj_attribute ds_ka;
	struct kobj_attribute sd_ka;
	struct wakeup_source *ps_ws;
	struct notifier_block ps_pm_nb;
	struct qmp *qmp;
	struct msm_rpm_kvp kvp_req;
	struct syscore_ops ps_ops;
	enum power_states current_state;
	int subsys_count;
	struct list_head sub_sys_list;
	bool deep_sleep_allowed;
	u32 suspend_delay;
};

static struct power_state_drvdata *drv;

static int subsys_suspend(struct subsystem_data *ss_data, struct rproc *rproc, uint32_t state)
{
	int ret = 0;

	switch (state) {
	case SUBSYS_DEEPSLEEP:
	case SUBSYS_HIBERNATE:
		ss_data->ignore_ssr = true;
		rproc_shutdown(rproc);
		ss_data->ignore_ssr = false;
		break;
	default:
		pr_err("Invalid %s suspend state\n", ss_data->name);
		ret = -1;
		break;
	}

	return ret;
}

static int subsys_resume(struct subsystem_data *ss_data, struct rproc *rproc, u32 state)
{
	int ret = 0;

	switch (state) {
	case SUBSYS_DEEPSLEEP:
	case SUBSYS_HIBERNATE:
		ss_data->ignore_ssr = true;
		ret = rproc_boot(rproc);
		ss_data->ignore_ssr = false;
		break;
	default:
		pr_err("Invalid %s resume state\n", ss_data->name);
		ret = -1;
		break;
	}

	return ret;
}

static int subsystem_resume(struct power_state_drvdata *drv, u32 state)
{
	struct subsystem_data *ss_data;
	int ret = 0;
	struct rproc *rproc = NULL;

	list_for_each_entry(ss_data, &drv->sub_sys_list, list) {
		pr_info("%s subsystem resume start\n", ss_data->name);
		rproc = rproc_get_by_phandle(ss_data->rproc_handle);
		if (!rproc)
			return -ENODEV;

		ret = subsys_resume(ss_data, rproc, state);
		if (ret) {
			pr_err("%s subsystem resume failed\n", ss_data->name);
			BUG();
		}
		rproc_put(rproc);
		pr_info("%s subsystem resume complete\n", ss_data->name);
	}

	return ret;
}

static int subsystem_suspend(struct power_state_drvdata *drv, u32 state)
{
	struct subsystem_data *ss_data;
	int ret = 0;
	struct rproc *rproc = NULL;

	list_for_each_entry(ss_data, &drv->sub_sys_list, list) {
		pr_info("%s subsystem suspend start\n", ss_data->name);
		rproc = rproc_get_by_phandle(ss_data->rproc_handle);
		if (!rproc)
			return -ENODEV;

		ret = subsys_suspend(ss_data, rproc, state);
		if (ret) {
			pr_err("%s subsystem suspend failed\n", ss_data->name);
			BUG();
		}
		rproc_put(rproc);
		pr_info("%s subsystem suspend complete\n", ss_data->name);
	}

	return ret;
}

static int ps_open(struct inode *inode, struct file *file)
{
	struct power_state_drvdata *drv = NULL;

	if (!inode || !inode->i_cdev || !file)
		return -EINVAL;

	drv = container_of(inode->i_cdev, struct power_state_drvdata, ps_cdev);
	file->private_data = drv;

	return 0;
}

#if IS_ENABLED(CONFIG_MSM_RPM_SMD)
static int send_deep_sleep_vote(int state, struct power_state_drvdata *drv)
{
	u32 val;

	if (state == DS_ENTRY)
		val = RPM_XO_DS_ENTER_VALUE;
	else
		val = RPM_XO_DS_EXIT_VALUE;

	drv->kvp_req.key = RPM_XO_DS_KEY;
	drv->kvp_req.data = (void *)&val;
	drv->kvp_req.length = sizeof(val);

	return msm_rpm_send_message(MSM_RPM_CTX_SLEEP_SET, RPM_XO_DS_REQ,
				    RPM_XO_DS_ID, &drv->kvp_req, 1);
}
#elif IS_ENABLED(CONFIG_NOTIFY_AOP)
static int send_deep_sleep_vote(int state, struct power_state_drvdata *drv)
{
	char buf[MAX_QMP_MSG_SIZE] = {};

	if (state == DS_ENTRY)
		scnprintf(buf, sizeof(buf), "{class: deep_sleep, res: 1}");
	else
		scnprintf(buf, sizeof(buf), "{class: deep_sleep, res: 0}");

	return qmp_send(drv->qmp, buf, sizeof(buf));
}
#else
static int send_deep_sleep_vote(int state, struct power_state_drvdata *drv)
{
	return 0;
}
#endif

static long ps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct power_state_drvdata *drv = file->private_data;
	int ret = 0;

	switch (cmd) {
	case LPM_ACTIVE:
	case POWER_STATE_LPM_ACTIVE:
		pr_debug("State changed to Active\n");
		if (pm_suspend_via_firmware()) {
			pm_suspend_clear_flags();
			__pm_relax(drv->ps_ws);
		}
		drv->current_state = ACTIVE;
		pr_info("low power mode exit complete\n");
		break;

	case ENTER_DEEPSLEEP:
	case POWER_STATE_ENTER_DEEPSLEEP:
		pr_debug("Enter Deep Sleep\n");
		ret = subsystem_suspend(drv, SUBSYS_DEEPSLEEP);
		drv->current_state = DEEPSLEEP;
		break;

	case ENTER_HIBERNATE:
	case POWER_STATE_ENTER_HIBERNATE:
		pr_debug("Enter Hibernate\n");
		ret = subsystem_suspend(drv, SUBSYS_HIBERNATE);
		drv->current_state = HIBERNATE;
		break;

	case EXIT_DEEPSLEEP_STATE:
	case POWER_STATE_EXIT_DEEPSLEEP_STATE:
		pr_debug("Exit Deep Sleep\n");
		ret = subsystem_resume(drv, SUBSYS_DEEPSLEEP);
		break;

	case EXIT_HIBERNATE_STATE:
	case POWER_STATE_EXIT_HIBERNATE_STATE:
		pr_debug("Exit Hibernate\n");
		ret = subsystem_resume(drv, SUBSYS_HIBERNATE);
		break;

	case MODEM_SUSPEND:
	case MODEM_EXIT:
	case POWER_STATE_MODEM_SUSPEND:
	case POWER_STATE_MODEM_EXIT:
	case ADSP_SUSPEND:
	case ADSP_EXIT:
	case CDSP_EXIT:
	case CDSP_SUSPEND:
	case POWER_STATE_ADSP_SUSPEND:
	case POWER_STATE_ADSP_EXIT:
		pr_debug("Deprecated ioctl\n");
		break;

	default:
		pr_err("Inside default in power_state.c due to %d\n", cmd);
		ret = -ENOIOCTLCMD;
		pr_err("%s: Default\n", __func__);
		break;
	}

	return ret;
}

static long compat_ps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = _IOC_NR(cmd);

	return (long)ps_ioctl(file, nr, arg);
}

static const struct file_operations ps_fops = {
	.owner = THIS_MODULE,
	.open = ps_open,
	.unlocked_ioctl = ps_ioctl,
	.compat_ioctl = compat_ps_ioctl,
};

static int send_uevent(struct power_state_drvdata *drv, enum ps_event_type event)
{
	char event_string[STRING_LEN];
	char *envp[2] = { event_string, NULL };

	scnprintf(event_string, ARRAY_SIZE(event_string), "POWER_STATE_EVENT = %d", event);
	return kobject_uevent_env(&drv->ps_dev->kobj, KOBJ_CHANGE, envp);
}

static int ps_ssr_cb(struct notifier_block *nb, unsigned long opcode, void *data)
{
	struct qcom_ssr_notify_data *notify_data = data;
	struct subsystem_data *ss_data;
	bool ss_present = false;

	list_for_each_entry(ss_data, &drv->sub_sys_list, list) {
		if (!strcmp(ss_data->name, notify_data->name)) {
			ss_present = true;
			break;
		}
	}

	if (!ss_present || ss_data->ignore_ssr)
		return NOTIFY_DONE;

	switch (opcode) {
	case QCOM_SSR_BEFORE_SHUTDOWN:
		pr_debug("%s is shutdown\n", notify_data->name);
		send_uevent(drv, ss_data->enter);
		break;
	case QCOM_SSR_AFTER_POWERUP:
		pr_debug("%s: is powered up\n", notify_data->name);
		send_uevent(drv, ss_data->exit);
		break;
	default:
		pr_debug("%s: ignore ssr event\n", notify_data->name);
		break;
	}

	return NOTIFY_DONE;
}

static int ps_pm_cb(struct notifier_block *nb, unsigned long event, void *unused)
{
	struct power_state_drvdata *drv = container_of(nb, struct power_state_drvdata, ps_pm_nb);
	int ret;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		if (drv->current_state == DEEPSLEEP) {
			pr_info("Deep Sleep entry\n");
			ret = send_deep_sleep_vote(DS_ENTRY, drv);
			if (ret)
				return NOTIFY_BAD;
			pm_set_suspend_via_firmware();
		} else {
			pr_debug("RBSC Suspend\n");
		}
		break;

	case PM_POST_SUSPEND:
		if (pm_suspend_via_firmware()) {
			pr_info("Deep Sleep exit\n");

			ret = send_deep_sleep_vote(DS_EXIT, drv);
			if (ret)
				BUG_ON(1);
			__pm_stay_awake(drv->ps_ws);
			send_uevent(drv, EXIT_DEEP_SLEEP);
		} else {
			pr_debug("RBSC Resume\n");
		}
		break;

	case PM_HIBERNATION_PREPARE:
		pr_info("Hibernate entry\n");

		send_uevent(drv, PREPARE_FOR_HIBERNATION);
		drv->current_state = HIBERNATE;
		break;

	case PM_RESTORE_PREPARE:
		pr_debug("Hibernate prepare\n");
		break;

	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		pr_info("Hibernate exit\n");
		send_uevent(drv, EXIT_HIBERNATE);
		break;

	default:
		WARN_ONCE(1, "Default case: PM Notifier\n");
		break;
	}

	return NOTIFY_DONE;
}

static void power_state_resume(void)
{
	struct arm_smccc_res res;

	if (pm_suspend_via_firmware())
		arm_smccc_smc(DS_ENTRY_SMC_ID, DS_NUM_PARAMETERS, DS_EXIT, 0, 0, 0, 0, 0, &res);
}

static int power_state_suspend(void)
{
	struct arm_smccc_res res;

	if (pm_suspend_via_firmware()) {
		arm_smccc_smc(DS_ENTRY_SMC_ID, DS_NUM_PARAMETERS, DS_ENTRY, 0, 0, 0, 0, 0, &res);
		if (res.a0)
			return res.a0;
	}

	return 0;
}

static ssize_t suspend_delay_show(struct kobject *kobj, struct kobj_attribute *attr,
				       char *buf)
{
	struct power_state_drvdata *drv = container_of(attr, struct power_state_drvdata, sd_ka);

	return scnprintf(buf, PAGE_SIZE, "%u\n", drv->suspend_delay);
}

static ssize_t suspend_delay_store(struct kobject *kobj, struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	struct power_state_drvdata *drv = container_of(attr, struct power_state_drvdata, sd_ka);
	u32 val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret < 0) {
		pr_err("Invalid argument passed\n");
		return ret;
	}

	drv->deep_sleep_allowed = val;

	return count;
}

static ssize_t deep_sleep_allowed_show(struct kobject *kobj, struct kobj_attribute *attr,
				       char *buf)
{
	struct power_state_drvdata *drv = container_of(attr, struct power_state_drvdata, ds_ka);

	return scnprintf(buf, PAGE_SIZE, "%u\n", drv->deep_sleep_allowed);
}

static ssize_t deep_sleep_allowed_store(struct kobject *kobj, struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	struct power_state_drvdata *drv = container_of(attr, struct power_state_drvdata, ds_ka);
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret) {
		pr_err("Invalid argument passed\n");
		return ret;
	}

	drv->deep_sleep_allowed = val;

	return count;
}

static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct power_state_drvdata *drv = container_of(attr, struct power_state_drvdata, ps_ka);
	int len = strlen(power_state[drv->current_state]);

	return scnprintf(buf, len + 2, "%s\n", power_state[drv->current_state]);
}

static int power_state_dev_init(struct power_state_drvdata *drv)
{
	int ret;

	ret = alloc_chrdev_region(&drv->ps_dev_no, POWER_STATS_BASEMINOR,
				  POWER_STATS_MAX_MINOR, POWER_STATE_DEVICE_NAME);
	if (ret)
		return ret;

	cdev_init(&drv->ps_cdev, &ps_fops);
	ret = cdev_add(&drv->ps_cdev, drv->ps_dev_no, 1);
	if (ret) {
		unregister_chrdev_region(drv->ps_dev_no, 1);
		return ret;
	}

	drv->ps_class = class_create(THIS_MODULE, POWER_STATE_DEVICE_NAME);
	if (IS_ERR_OR_NULL(drv->ps_class)) {
		cdev_del(&drv->ps_cdev);
		unregister_chrdev_region(drv->ps_dev_no, 1);
		return PTR_ERR(drv->ps_class);
	}

	drv->ps_dev = device_create(drv->ps_class, NULL,
				    drv->ps_dev_no, NULL, POWER_STATE_DEVICE_NAME);
	if (IS_ERR_OR_NULL(drv->ps_dev)) {
		class_destroy(drv->ps_class);
		cdev_del(&drv->ps_cdev);
		unregister_chrdev_region(drv->ps_dev_no, 1);
		return PTR_ERR(drv->ps_dev);
	}

	drv->ps_kobj = kobject_create_and_add(POWER_STATE_DEVICE_NAME, kernel_kobj);
	if (!drv->ps_kobj) {
		ret = -ENOMEM;
		device_destroy(drv->ps_class, drv->ps_dev_no);
		class_destroy(drv->ps_class);
		cdev_del(&drv->ps_cdev);
		unregister_chrdev_region(drv->ps_dev_no, 1);
		return ret;
	}

	sysfs_attr_init(&drv->ps_ka.attr);
	drv->ps_ka.attr.mode = 0444;
	drv->ps_ka.attr.name = "state";
	drv->ps_ka.show = state_show;

	ret = sysfs_create_file(drv->ps_kobj, &drv->ps_ka.attr);
	if (ret)
		goto exit;

	sysfs_attr_init(&drv->ds_ka.attr);
	drv->ds_ka.attr.mode = 0644;
	drv->ds_ka.attr.name = "deep_sleep_allowed";
	drv->ds_ka.show = deep_sleep_allowed_show;
	drv->ds_ka.store = deep_sleep_allowed_store;

	ret = sysfs_create_file(drv->ps_kobj, &drv->ds_ka.attr);
	if (ret) {
		sysfs_remove_file(drv->ps_kobj, &drv->ps_ka.attr);
		goto exit;
	}

	sysfs_attr_init(&drv->sd_ka.attr);
	drv->sd_ka.attr.mode = 0644;
	drv->sd_ka.attr.name = "suspend_delay";
	drv->sd_ka.show = suspend_delay_show;
	drv->sd_ka.store = suspend_delay_store;

	ret = sysfs_create_file(drv->ps_kobj, &drv->sd_ka.attr);
	if (ret) {
		sysfs_remove_file(drv->ps_kobj, &drv->ds_ka.attr);
		sysfs_remove_file(drv->ps_kobj, &drv->ps_ka.attr);
		goto exit;
	}

	/* Default delay of 1 second */
	drv->suspend_delay = 1;
	return 0;

exit:
	kobject_put(drv->ps_kobj);
	device_destroy(drv->ps_class, drv->ps_dev_no);
	class_destroy(drv->ps_class);
	cdev_del(&drv->ps_cdev);
	unregister_chrdev_region(drv->ps_dev_no, 1);

	return ret;
}

static int power_state_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct subsystem_data *ss_data;
	int ret, i, j;
	const char *name;
	phandle rproc_handle;

	drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->ps_pm_nb.notifier_call = ps_pm_cb;
	drv->ps_pm_nb.priority = PS_PM_NOTIFIER_PRIORITY;
	ret = register_pm_notifier(&drv->ps_pm_nb);
	if (ret)
		return ret;

	drv->ps_ws = wakeup_source_register(&pdev->dev, POWER_STATE_DEVICE_NAME);
	if (!drv->ps_ws)
		goto remove_pm_notifier;

	INIT_LIST_HEAD(&drv->sub_sys_list);

	drv->subsys_count = of_property_count_strings(dn, "qcom,subsys-name");
	if (drv->subsys_count < 0)
		drv->subsys_count = 0;
	for (i = 0; i < drv->subsys_count; i++) {
		of_property_read_string_index(dn, "qcom,subsys-name", i, &name);

		ret = of_property_read_u32_index(dn, "qcom,rproc-handle", i, &rproc_handle);
		if (ret)
			goto remove_ss;

		ss_data = devm_kzalloc(&pdev->dev, sizeof(struct subsystem_data), GFP_KERNEL);
		if (!ss_data) {
			ret = -ENOMEM;
			goto remove_ss;
		}

		ss_data->name = name;
		ss_data->rproc_handle = rproc_handle;

		ss_data->ps_ssr_nb.notifier_call = ps_ssr_cb;
		ss_data->ps_ssr_nb.priority = PS_SSR_NOTIFIER_PRIORITY;
		ss_data->ssr_handle = qcom_register_ssr_notifier(name, &ss_data->ps_ssr_nb);
		ss_data->ignore_ssr = false;
		if (IS_ERR(ss_data->ssr_handle)) {
			ret = PTR_ERR(ss_data->ssr_handle);
			goto remove_ss;
		}

		for (j = 0; j < ARRAY_SIZE(event_data); j++) {
			if (!strcmp(event_data[j].name, name)) {
				ss_data->enter = event_data[j].enter;
				ss_data->exit = event_data[j].exit;
			}
		}
		if (!ss_data->enter) {
			ret = -ENODEV;
			goto remove_ss;
		}

		list_add_tail(&ss_data->list, &drv->sub_sys_list);
	}

	if (IS_ENABLED(CONFIG_NOTIFY_AOP)) {
		drv->qmp = qmp_get(&pdev->dev);
		if (IS_ERR(drv->qmp)) {
			ret = PTR_ERR(drv->qmp);
			goto remove_ss;
		}
	}

	ret = power_state_dev_init(drv);
	if (ret)
		goto remove_ss;

	drv->ps_ops.suspend = power_state_suspend;
	drv->ps_ops.resume = power_state_resume;
	register_syscore_ops(&drv->ps_ops);

	dev_set_drvdata(&pdev->dev, drv);
	return ret;

remove_ss:
	list_for_each_entry(ss_data, &drv->sub_sys_list, list) {
		qcom_unregister_ssr_notifier(ss_data->ssr_handle, &ss_data->ps_ssr_nb);
		list_del(&ss_data->list);
	}
	INIT_LIST_HEAD(&drv->sub_sys_list);
	wakeup_source_unregister(drv->ps_ws);
remove_pm_notifier:
	unregister_pm_notifier(&drv->ps_pm_nb);
	return ret;
}

static int power_state_remove(struct platform_device *pdev)
{
	struct power_state_drvdata *drv = dev_get_drvdata(&pdev->dev);
	struct subsystem_data *ss_data;

	unregister_syscore_ops(&drv->ps_ops);

	list_for_each_entry(ss_data, &drv->sub_sys_list, list) {
		qcom_unregister_ssr_notifier(ss_data->ssr_handle, &ss_data->ps_ssr_nb);
		list_del(&ss_data->list);
	}

	INIT_LIST_HEAD(&drv->sub_sys_list);
	wakeup_source_unregister(drv->ps_ws);
	sysfs_remove_file(drv->ps_kobj, &drv->ds_ka.attr);
	sysfs_remove_file(drv->ps_kobj, &drv->ps_ka.attr);
	kobject_put(drv->ps_kobj);
	device_destroy(drv->ps_class, drv->ps_dev_no);
	class_destroy(drv->ps_class);
	cdev_del(&drv->ps_cdev);
	unregister_chrdev_region(drv->ps_dev_no, 1);
	unregister_pm_notifier(&drv->ps_pm_nb);

	return 0;
}

static const struct of_device_id power_state_of_match[] = {
	{ .compatible = "qcom,power-state", },
	{ }
};
MODULE_DEVICE_TABLE(of, power_state_of_match);

static struct platform_driver power_state_driver = {
	.probe = power_state_probe,
	.remove = power_state_remove,
	.driver = {
		.name = "power-state",
		.of_match_table = power_state_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(power_state_driver);
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) Power State Driver");
MODULE_LICENSE("GPL");
