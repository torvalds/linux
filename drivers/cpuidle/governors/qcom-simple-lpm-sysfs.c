// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cpu.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "qcom-simple-lpm.h"

#define MAX_LATENCY_DIV 1000

static struct kobject *qcom_lpm_simple_kobj;

static ssize_t cluster_idle_set(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t len)
{
	struct qcom_simple_cluster_node *d = container_of(attr,
				struct qcom_simple_cluster_node, disable_attr);
	bool disable;
	int ret;

	ret = strtobool(buf, &disable);
	if (ret)
		return -EINVAL;

	d->cluster->state_allowed[d->state_idx] = !disable;

	return len;
}

static ssize_t cluster_idle_get(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	struct qcom_simple_cluster_node *d = container_of(attr,
				struct qcom_simple_cluster_node, disable_attr);

	return scnprintf(buf, PAGE_SIZE, "%d\n", !d->cluster->state_allowed[d->state_idx]);
}

static int create_simple_cluster_state_node(struct device *dev, struct qcom_simple_cluster_node *d)
{
	struct kobj_attribute *attr = &d->disable_attr;
	int ret;

	d->attr_group = devm_kzalloc(dev, sizeof(struct attribute_group), GFP_KERNEL);
	if (!d->attr_group)
		return -ENOMEM;

	d->attrs = devm_kcalloc(dev, 2, sizeof(struct attribute *), GFP_KERNEL);
	if (!d->attrs)
		return -ENOMEM;

	sysfs_attr_init(&attr->attr);
	attr->attr.name = "disable";
	attr->attr.mode = 0644;
	attr->show = cluster_idle_get;
	attr->store = cluster_idle_set;

	d->attrs[0] = &attr->attr;
	d->attrs[1] = NULL;
	d->attr_group->attrs = d->attrs;

	ret = sysfs_create_group(d->kobj, d->attr_group);
	if (ret)
		return -ENOMEM;

	return ret;
}

void remove_simple_cluster_sysfs_nodes(struct simple_lpm_cluster *cluster)
{
	struct generic_pm_domain *genpd = cluster->genpd;
	struct kobject *kobj = cluster->dev_kobj;
	int i;

	if (!qcom_lpm_simple_kobj || !kobj)
		return;

	for (i = 0; i < genpd->state_count; i++) {
		struct qcom_simple_cluster_node *d = cluster->dev_node[i];

		if (d->kobj) {
			sysfs_remove_group(d->kobj, d->attr_group);
			kobject_put(d->kobj);
		}
	}

	kobject_put(kobj);
}

int create_simple_cluster_sysfs_nodes(struct simple_lpm_cluster *cluster)
{
	char name[10];
	int i, ret;
	struct generic_pm_domain *genpd = cluster->genpd;

	if (!qcom_lpm_simple_kobj)
		return -EPROBE_DEFER;

	cluster->dev_kobj = kobject_create_and_add(genpd->name, qcom_lpm_simple_kobj);
	if (!cluster->dev_kobj)
		return -ENOMEM;

	for (i = 0; i < genpd->state_count; i++) {
		struct qcom_simple_cluster_node *d;

		d = devm_kzalloc(cluster->dev, sizeof(*d), GFP_KERNEL);
		if (!d) {
			kobject_put(cluster->dev_kobj);
			return -ENOMEM;
		}

		d->state_idx = i;
		d->cluster = cluster;
		scnprintf(name, PAGE_SIZE, "D%u", i);
		d->kobj = kobject_create_and_add(name, cluster->dev_kobj);
		if (!d->kobj) {
			kobject_put(cluster->dev_kobj);
			return -ENOMEM;
		}

		ret = create_simple_cluster_state_node(cluster->dev, d);
		if (ret) {
			kobject_put(d->kobj);
			kobject_put(cluster->dev_kobj);
			return ret;
		}

		cluster->dev_node[i] = d;
	}

	return 0;
}

static ssize_t simple_sleep_disabled_show(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", simple_sleep_disabled);
}

static ssize_t simple_sleep_disabled_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret) {
		pr_err("Invalid argument passed\n");
		return count;
	}

	simple_sleep_disabled = val;

	return count;
}

static ssize_t cpu_latency_factor_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret || !val || val > MAX_LATENCY_DIV) {
		pr_err("Invalid argument passed\n");
		return count;
	}

	cur_div = val;

	return count;
}

static ssize_t cpu_latency_factor_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", cur_div);
}

static ssize_t cluster_latency_factor_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	int val, ret, i;
	struct simple_lpm_cluster *cluster_simple_gov;

	ret = kstrtoint(buf, 0, &val);
	if (ret || !val || val > MAX_LATENCY_DIV) {
		pr_err("Invalid argument passed\n");
		return count;
	}

	list_for_each_entry(cluster_simple_gov, &cluster_dev_list, list) {
		for (i = 0; i < cluster_simple_gov->genpd->state_count; i++) {
			struct generic_pm_domain *genpd = cluster_simple_gov->genpd;
			struct genpd_power_state *state = &genpd->states[i];

			state->residency_ns = state->residency_ns * cluster_cur_div;
			do_div(state->residency_ns, val);
			state->power_on_latency_ns = state->power_on_latency_ns * cluster_cur_div;
			do_div(state->power_on_latency_ns, val);
			state->power_off_latency_ns = state->power_on_latency_ns * cluster_cur_div;
			do_div(state->power_off_latency_ns, val);
		}
	}
	cluster_cur_div = val;

	return count;
}

static ssize_t cluster_latency_factor_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", cluster_cur_div);
}

static struct kobj_attribute attr_simple_sleep_disabled = __ATTR_RW(simple_sleep_disabled);
static struct kobj_attribute attr_cpu_latency_factor = __ATTR_RW(cpu_latency_factor);
static struct kobj_attribute attr_cluster_latency_factor = __ATTR_RW(cluster_latency_factor);

static struct attribute *lpm_simple_gov_attrs[] = {
	&attr_simple_sleep_disabled.attr,
	&attr_cpu_latency_factor.attr,
	&attr_cluster_latency_factor.attr,
	NULL
};

static struct attribute_group lpm_gov_attr_group = {
	.attrs = lpm_simple_gov_attrs,
	.name = "parameters",
};

void remove_simple_gov_global_sysfs_nodes(void)
{
	sysfs_remove_group(qcom_lpm_simple_kobj, &lpm_gov_attr_group);
	kobject_put(qcom_lpm_simple_kobj);
}

int create_simple_gov_global_sysfs_nodes(void)
{
	struct kobject *cpuidle_kobj = &cpu_subsys.dev_root->kobj;

	qcom_lpm_simple_kobj = kobject_create_and_add("qcom_simple_lpm", cpuidle_kobj);
	if (!qcom_lpm_simple_kobj)
		return -ENOMEM;

	return sysfs_create_group(qcom_lpm_simple_kobj, &lpm_gov_attr_group);
}
