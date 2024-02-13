// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/scmi_protocol.h>
#include <linux/qcom_scmi_vendor.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/slab.h>

static struct kobject c1dcvs_kobj;
static struct scmi_protocol_handle *ph;
static const struct qcom_scmi_vendor_ops *ops;
static unsigned int user_c1dcvs_en;
static unsigned int kernel_c1dcvs_en;
static DEFINE_MUTEX(c1dcvs_lock);
#define C1DCVS_ALGO_STR   0x433144435653  /* "C1DCVS" */
struct scmi_device *sdev;

struct qcom_c1dcvs_attr {
	struct attribute		attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count);
};

enum cpucp_profiling_param_ids {
	PARAM_ENABLE_C1DCVS = 1,
	PARAM_ENABLE_TRACE,
	PARAM_IPC_THRESH,
	PARAM_EFREQ_THRESH,
	PARAM_HYSTERESIS,
	PARAM_C1DCVS_OPT_MODE,
};

static int set_enable_c1dcvs(void *buf)
{
	return ops->start_activity(ph, buf, C1DCVS_ALGO_STR, PARAM_ENABLE_C1DCVS, sizeof(u32));
}

static int set_c1dcvs_opt_mode(void *buf)
{
	return ops->set_param(ph, buf, C1DCVS_ALGO_STR, PARAM_C1DCVS_OPT_MODE, sizeof(u32));
}

static int set_enable_trace(void *buf)
{
	return ops->start_activity(ph, buf, C1DCVS_ALGO_STR, PARAM_ENABLE_TRACE, sizeof(u32));
}

static int set_ipc_thresh(void *buf, size_t rx_size)
{
	return ops->set_param(ph, buf, C1DCVS_ALGO_STR, PARAM_IPC_THRESH, rx_size);
}

static int set_efreq_thresh(void *buf, size_t rx_size)
{
	return ops->set_param(ph, buf, C1DCVS_ALGO_STR, PARAM_EFREQ_THRESH, rx_size);
}

static int set_hysteresis(void *buf)
{
	return ops->set_param(ph, buf, C1DCVS_ALGO_STR, PARAM_HYSTERESIS, sizeof(u32));
}

static int get_enable_c1dcvs(void *buf)
{
	return ops->get_param(ph, buf, C1DCVS_ALGO_STR, PARAM_ENABLE_C1DCVS, 0, sizeof(u32));
}

static int get_enable_trace(void *buf)
{
	return ops->get_param(ph, buf, C1DCVS_ALGO_STR, PARAM_ENABLE_TRACE, 0, sizeof(u32));
}

static int get_ipc_thresh(void *buf, size_t rx_size)
{
	return ops->get_param(ph, buf, C1DCVS_ALGO_STR, PARAM_IPC_THRESH, 0, rx_size);
}

static int get_efreq_thresh(void *buf, size_t rx_size)
{
	return ops->get_param(ph, buf, C1DCVS_ALGO_STR, PARAM_EFREQ_THRESH, 0, rx_size);
}

static int get_hysteresis(void *buf)
{
	return ops->get_param(ph, buf, C1DCVS_ALGO_STR, PARAM_HYSTERESIS, 0, sizeof(u32));
}
static int get_c1dcvs_opt_mode(void *buf)
{
	return ops->get_param(ph, buf, C1DCVS_ALGO_STR, PARAM_C1DCVS_OPT_MODE, 0, sizeof(u32));
}
#define to_c1dcvs_attr(_attr) \
	container_of(_attr, struct qcom_c1dcvs_attr, attr)
#define C1DCVS_ATTR_RW(_name)						\
static struct qcom_c1dcvs_attr _name =					\
__ATTR(_name, 0644, show_##_name, store_##_name)			\

#define store_c1dcvs_attr(name)						\
static ssize_t store_##name(struct kobject *kobj,			\
				  struct attribute *attr, const char *buf,\
				  size_t count)				\
{									\
	unsigned int var;						\
	int ret;							\
	ret = kstrtouint(buf, 10, &var);				\
	if (ret < 0)							\
		return ret;						\
	ret = set_##name(&var);				\
	return ((ret < 0) ? ret : count);				\
}									\

#define show_c1dcvs_attr(name)						\
static ssize_t show_##name(struct kobject *kobj,			\
				 struct attribute *attr, char *buf)	\
{									\
	unsigned int var;						\
	int ret;							\
									\
	ret = get_##name(&var);				\
	if (ret < 0)							\
		return ret;						\
									\
	return scnprintf(buf, PAGE_SIZE, "%lu\n", le32_to_cpu(var));	\
}									\

/*
 * Must hold c1dcvs_lock before calling this function
 */
static int update_enable_c1dcvs(void)
{
	unsigned int enable = min(user_c1dcvs_en, kernel_c1dcvs_en);

	return set_enable_c1dcvs(&enable);
}

static ssize_t store_enable_c1dcvs(struct kobject *kobj,
				  struct attribute *attr, const char *buf,
				  size_t count)
{
	unsigned int var;
	int ret;

	ret = kstrtouint(buf, 10, &var);
	if (ret < 0)
		return ret;

	mutex_lock(&c1dcvs_lock);
	user_c1dcvs_en = var;
	ret = update_enable_c1dcvs();
	mutex_unlock(&c1dcvs_lock);

	return ((ret < 0) ? ret : count);
}

int c1dcvs_enable(bool enable)
{
	unsigned int data = enable ? 1 : 0;
	int ret;

	if (IS_ERR(ops))
		return -EPROBE_DEFER;

	mutex_lock(&c1dcvs_lock);
	kernel_c1dcvs_en = data;
	ret = update_enable_c1dcvs();
	mutex_unlock(&c1dcvs_lock);

	return ret;
}
EXPORT_SYMBOL(c1dcvs_enable);

store_c1dcvs_attr(enable_trace);
show_c1dcvs_attr(enable_trace);
C1DCVS_ATTR_RW(enable_trace);
store_c1dcvs_attr(hysteresis);
show_c1dcvs_attr(hysteresis);
C1DCVS_ATTR_RW(hysteresis);
store_c1dcvs_attr(c1dcvs_opt_mode);
show_c1dcvs_attr(c1dcvs_opt_mode);
C1DCVS_ATTR_RW(c1dcvs_opt_mode);
show_c1dcvs_attr(enable_c1dcvs);
C1DCVS_ATTR_RW(enable_c1dcvs);

#define store_c1dcvs_thresh(name)					\
static ssize_t store_##name(struct kobject *kobj,			\
				   struct attribute *attr, const char *buf,\
				   size_t count)			\
{									\
	int ret, i = 0;							\
	char *s = kstrdup(buf, GFP_KERNEL);				\
	unsigned int msg[2] = {0};					\
	char *str, *s_orig = s;						\
									\
	while (((str = strsep(&s, " ")) != NULL) && i < 2) {		\
		ret = kstrtouint(str, 10, &msg[i]);			\
		if (ret < 0) {						\
			pr_err("Invalid value :%d\n", ret);		\
			goto out;					\
		}							\
		i++;							\
	}								\
									\
	pr_info("Input threshold :%lu for cluster :%lu\n", msg[1], msg[0]);\
	ret = set_##name(msg, sizeof(msg));				\
out:									\
	kfree(s_orig);							\
	return ((ret < 0) ? ret : count);				\
}									\

#define show_c1dcvs_thresh(name)					\
static ssize_t show_##name(struct kobject *kobj,			\
				  struct attribute *attr, char *buf)	\
{									\
	unsigned int *vars = NULL;					\
	int i, ret, tot = 0;						\
									\
	vars = kcalloc(num_possible_cpus(), sizeof(unsigned int), GFP_KERNEL);\
	if (!vars)							\
		return -ENOMEM;						\
	ret = get_##name(vars, num_possible_cpus() * \
			sizeof(unsigned int));				\
	if (ret < 0) {							\
		kfree(vars);						\
		return ret;						\
	}								\
	for (i = 0; i < num_possible_cpus(); i++) {			\
		vars[i] = le32_to_cpu(vars[i]);				\
		if (!vars[i])						\
			break;						\
		tot += scnprintf(buf + tot, PAGE_SIZE - tot, "%lu\t", vars[i]);\
	}								\
	tot += scnprintf(buf + tot, PAGE_SIZE - tot, "\n");		\
									\
	kfree(vars);							\
	return tot;							\
}									\

store_c1dcvs_thresh(ipc_thresh);
show_c1dcvs_thresh(ipc_thresh);
C1DCVS_ATTR_RW(ipc_thresh);
store_c1dcvs_thresh(efreq_thresh);
show_c1dcvs_thresh(efreq_thresh);
C1DCVS_ATTR_RW(efreq_thresh);

static struct attribute *c1dcvs_settings_attrs[] = {
	&enable_c1dcvs.attr,
	&enable_trace.attr,
	&ipc_thresh.attr,
	&efreq_thresh.attr,
	&hysteresis.attr,
	&c1dcvs_opt_mode.attr,
	NULL,
};
ATTRIBUTE_GROUPS(c1dcvs_settings);

static ssize_t attr_show(struct kobject *kobj, struct attribute *attr,
			 char *buf)
{
	struct qcom_c1dcvs_attr *c1dcvs_attr = to_c1dcvs_attr(attr);
	ssize_t ret = -EIO;

	if (c1dcvs_attr->show)
		ret = c1dcvs_attr->show(kobj, attr, buf);

	return ret;
}

static ssize_t attr_store(struct kobject *kobj, struct attribute *attr,
			  const char *buf, size_t count)
{
	struct qcom_c1dcvs_attr *c1dcvs_attr = to_c1dcvs_attr(attr);
	ssize_t ret = -EIO;

	if (c1dcvs_attr->store)
		ret = c1dcvs_attr->store(kobj, attr, buf, count);

	return ret;
}

static const struct sysfs_ops c1dcvs_sysfs_ops = {
	.show	= attr_show,
	.store	= attr_store,
};
static struct kobj_type c1dcvs_settings_ktype = {
	.sysfs_ops		= &c1dcvs_sysfs_ops,
	.default_groups		= c1dcvs_settings_groups,
};

static int scmi_c1dcvs_probe(struct platform_device *pdev)
{
	int ret;

	sdev = get_qcom_scmi_device();
	if (IS_ERR(sdev)) {
		ret = PTR_ERR(sdev);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Error getting scmi_dev ret = %d\n", ret);
		return ret;
	}
	ops = sdev->handle->devm_protocol_get(sdev, QCOM_SCMI_VENDOR_PROTOCOL, &ph);
	if (IS_ERR(ops)) {
		ret = PTR_ERR(ops);
		ops = NULL;
		return ret;
	}

	ret = kobject_init_and_add(&c1dcvs_kobj, &c1dcvs_settings_ktype,
				   &cpu_subsys.dev_root->kobj, "c1dcvs");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to init c1 dcvs kobj: %d\n", ret);
		kobject_put(&c1dcvs_kobj);
	}
	user_c1dcvs_en = kernel_c1dcvs_en = 1;

	return 0;
}

static const struct of_device_id c1dcvs_v2[] = {
	{.compatible = "qcom,c1dcvs-v2"},
	{},
};

static struct platform_driver c1dcvs_v2_driver = {
	.driver = {
		.name = "c1dcvs-v2",
		.of_match_table = c1dcvs_v2,
		.suppress_bind_attrs = true,
	},
	.probe = scmi_c1dcvs_probe,
};


module_platform_driver(c1dcvs_v2_driver);
MODULE_SOFTDEP("pre: qcom_scmi_client");
MODULE_DESCRIPTION("Qcom SCMI C1DCVS driver");
MODULE_LICENSE("GPL");
