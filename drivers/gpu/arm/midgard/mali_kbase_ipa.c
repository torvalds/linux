/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */


#include <linux/of.h>
#include <linux/sysfs.h>

#include <mali_kbase.h>

#define NR_IPA_GROUPS 8

/**
 * struct ipa_group - represents a single IPA group
 * @name:               name of the IPA group
 * @capacitance:        capacitance constant for IPA group
 */
struct ipa_group {
	const char *name;
	u32 capacitance;
};

/**
 * struct kbase_ipa_context - IPA context per device
 * @kbdev:      pointer to kbase device
 * @groups:     array of IPA groups for this context
 * @ipa_lock:   protects the entire IPA context
 */
struct kbase_ipa_context {
	struct kbase_device *kbdev;
	struct ipa_group groups[NR_IPA_GROUPS];
	struct mutex ipa_lock;
};

static struct ipa_group ipa_groups_def_v4[] = {
	{ .name = "group0", .capacitance = 0 },
	{ .name = "group1", .capacitance = 0 },
	{ .name = "group2", .capacitance = 0 },
	{ .name = "group3", .capacitance = 0 },
	{ .name = "group4", .capacitance = 0 },
	{ .name = "group5", .capacitance = 0 },
	{ .name = "group6", .capacitance = 0 },
	{ .name = "group7", .capacitance = 0 },
};

static struct ipa_group ipa_groups_def_v5[] = {
	{ .name = "group0", .capacitance = 0 },
	{ .name = "group1", .capacitance = 0 },
	{ .name = "group2", .capacitance = 0 },
	{ .name = "group3", .capacitance = 0 },
	{ .name = "group4", .capacitance = 0 },
	{ .name = "group5", .capacitance = 0 },
	{ .name = "group6", .capacitance = 0 },
	{ .name = "group7", .capacitance = 0 },
};

static ssize_t show_ipa_group(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct kbase_ipa_context *ctx = kbdev->ipa_ctx;
	ssize_t count = -EINVAL;
	size_t i;

	mutex_lock(&ctx->ipa_lock);
	for (i = 0; i < ARRAY_SIZE(ctx->groups); i++) {
		if (!strcmp(ctx->groups[i].name, attr->attr.name)) {
			count = snprintf(buf, PAGE_SIZE, "%lu\n",
				(unsigned long)ctx->groups[i].capacitance);
			break;
		}
	}
	mutex_unlock(&ctx->ipa_lock);
	return count;
}

static ssize_t set_ipa_group(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct kbase_ipa_context *ctx = kbdev->ipa_ctx;
	unsigned long capacitance;
	size_t i;
	int err;

	err = kstrtoul(buf, 0, &capacitance);
	if (err < 0)
		return err;
	if (capacitance > U32_MAX)
		return -ERANGE;

	mutex_lock(&ctx->ipa_lock);
	for (i = 0; i < ARRAY_SIZE(ctx->groups); i++) {
		if (!strcmp(ctx->groups[i].name, attr->attr.name)) {
			ctx->groups[i].capacitance = capacitance;
			mutex_unlock(&ctx->ipa_lock);
			return count;
		}
	}
	mutex_unlock(&ctx->ipa_lock);
	return -EINVAL;
}

static DEVICE_ATTR(group0, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group1, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group2, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group3, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group4, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group5, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group6, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group7, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);

static struct attribute *kbase_ipa_attrs[] = {
	&dev_attr_group0.attr,
	&dev_attr_group1.attr,
	&dev_attr_group2.attr,
	&dev_attr_group3.attr,
	&dev_attr_group4.attr,
	&dev_attr_group5.attr,
	&dev_attr_group6.attr,
	&dev_attr_group7.attr,
	NULL,
};

static struct attribute_group kbase_ipa_attr_group = {
	.name = "ipa",
	.attrs = kbase_ipa_attrs,
};

static void init_ipa_groups(struct kbase_ipa_context *ctx)
{
	struct kbase_device *kbdev = ctx->kbdev;
	struct ipa_group *defs;
	size_t i, len;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_V4)) {
		defs = ipa_groups_def_v4;
		len = ARRAY_SIZE(ipa_groups_def_v4);
	} else {
		defs = ipa_groups_def_v5;
		len = ARRAY_SIZE(ipa_groups_def_v5);
	}

	for (i = 0; i < len; i++) {
		ctx->groups[i].name = defs[i].name;
		ctx->groups[i].capacitance = defs[i].capacitance;
	}
}

#if defined(CONFIG_OF) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
static int update_ipa_groups_from_dt(struct kbase_ipa_context *ctx)
{
	struct kbase_device *kbdev = ctx->kbdev;
	struct device_node *np, *child;
	struct ipa_group *group;
	size_t nr_groups;
	size_t i;
	int err;

	np = of_find_node_by_name(kbdev->dev->of_node, "ipa-groups");
	if (!np)
		return 0;

	nr_groups = 0;
	for_each_available_child_of_node(np, child)
		nr_groups++;
	if (!nr_groups || nr_groups > ARRAY_SIZE(ctx->groups)) {
		dev_err(kbdev->dev, "invalid number of IPA groups: %zu", nr_groups);
		err = -EINVAL;
		goto err0;
	}

	for_each_available_child_of_node(np, child) {
		const char *name;
		u32 capacitance;

		name = of_get_property(child, "label", NULL);
		if (!name) {
			dev_err(kbdev->dev, "label missing for IPA group");
			err = -EINVAL;
			goto err0;
		}
		err = of_property_read_u32(child, "capacitance",
				&capacitance);
		if (err < 0) {
			dev_err(kbdev->dev, "capacitance missing for IPA group");
			goto err0;
		}

		for (i = 0; i < ARRAY_SIZE(ctx->groups); i++) {
			group = &ctx->groups[i];
			if (!strcmp(group->name, name)) {
				group->capacitance = capacitance;
				break;
			}
		}
	}

	of_node_put(np);
	return 0;
err0:
	of_node_put(np);
	return err;
}
#else
static int update_ipa_groups_from_dt(struct kbase_ipa_context *ctx)
{
	return 0;
}
#endif

static int reset_ipa_groups(struct kbase_ipa_context *ctx)
{
	init_ipa_groups(ctx);
	return update_ipa_groups_from_dt(ctx);
}

struct kbase_ipa_context *kbase_ipa_init(struct kbase_device *kbdev)
{
	struct kbase_ipa_context *ctx;
	int err;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	mutex_init(&ctx->ipa_lock);
	ctx->kbdev = kbdev;

	err = reset_ipa_groups(ctx);
	if (err < 0)
		goto err0;

	err = sysfs_create_group(&kbdev->dev->kobj, &kbase_ipa_attr_group);
	if (err < 0)
		goto err0;

	return ctx;
err0:
	kfree(ctx);
	return NULL;
}

void kbase_ipa_term(struct kbase_ipa_context *ctx)
{
	struct kbase_device *kbdev = ctx->kbdev;

	sysfs_remove_group(&kbdev->dev->kobj, &kbase_ipa_attr_group);
	kfree(ctx);
}
