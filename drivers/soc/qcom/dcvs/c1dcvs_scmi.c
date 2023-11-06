// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/scmi_protocol.h>
#include <linux/scmi_c1dcvs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/scmi_c1dcvs.h>
#include <linux/slab.h>

static struct kobject c1dcvs_kobj;
static struct scmi_protocol_handle *ph;
static const struct scmi_c1dcvs_vendor_ops *ops;
static unsigned int user_c1dcvs_en;
static unsigned int kernel_c1dcvs_en;
static DEFINE_MUTEX(c1dcvs_lock);

struct qcom_c1dcvs_attr {
	struct attribute		attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count);
};

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
									\
	if (!ops)							\
		return -ENODEV;						\
									\
	ret = kstrtouint(buf, 10, &var);				\
	if (ret < 0)							\
		return ret;						\
									\
	ret = ops->set_##name(ph, &var);				\
	return ((ret < 0) ? ret : count);				\
}									\

#define show_c1dcvs_attr(name)						\
static ssize_t show_##name(struct kobject *kobj,			\
				 struct attribute *attr, char *buf)	\
{									\
	unsigned int var;						\
	int ret;							\
									\
	if (!ops)							\
		return -ENODEV;						\
									\
	ret = ops->get_##name(ph, &var);				\
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

	if (!ops)
		return -ENODEV;

	return ops->set_enable_c1dcvs(ph, &enable);
}

static ssize_t store_enable_c1dcvs(struct kobject *kobj,
				  struct attribute *attr, const char *buf,
				  size_t count)
{
	unsigned int var;
	int ret;

	if (!ops)
		return -ENODEV;

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

	if (!ops)
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
	ret = ops->set_##name(ph, msg);					\
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
	if (!ops)							\
		return -ENODEV;						\
									\
	vars = kcalloc(num_possible_cpus(), sizeof(unsigned int), GFP_KERNEL);\
	if (!vars)							\
		return -ENOMEM;						\
									\
	ret = ops->get_##name(ph, vars);				\
	if (ret < 0) {							\
		kfree(vars);						\
		return ret;						\
	}								\
									\
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

static int scmi_c1dcvs_probe(struct scmi_device *sdev)
{
	int ret;

	if (!sdev)
		return -ENODEV;

	ops = sdev->handle->devm_protocol_get(sdev, SCMI_C1DCVS_PROTOCOL, &ph);
	if (IS_ERR(ops)) {
		ret = PTR_ERR(ops);
		ops = NULL;
		return ret;
	}

	ret = kobject_init_and_add(&c1dcvs_kobj, &c1dcvs_settings_ktype,
				   &cpu_subsys.dev_root->kobj, "c1dcvs");
	if (ret < 0) {
		pr_err("failed to init c1 dcvs kobj: %d\n", ret);
		kobject_put(&c1dcvs_kobj);
	}

	user_c1dcvs_en = kernel_c1dcvs_en = 1;

	return 0;
}

static const struct scmi_device_id scmi_id_table[] = {
	{ .protocol_id = SCMI_C1DCVS_PROTOCOL, .name = "scmi_c1dcvs_protocol" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_c1dcvs_drv = {
	.name		= "scmi-c1dcvs-driver",
	.probe		= scmi_c1dcvs_probe,
	.id_table	= scmi_id_table,
};
module_scmi_driver(scmi_c1dcvs_drv);

MODULE_SOFTDEP("pre: c1dcvs_vendor");
MODULE_DESCRIPTION("ARM SCMI C1DCVS driver");
MODULE_LICENSE("GPL");
