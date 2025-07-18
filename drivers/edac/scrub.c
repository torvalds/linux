// SPDX-License-Identifier: GPL-2.0
/*
 * The generic EDAC scrub driver controls the memory scrubbers in the
 * system. The common sysfs scrub interface abstracts the control of
 * various arbitrary scrubbing functionalities into a unified set of
 * functions.
 *
 * Copyright (c) 2024-2025 HiSilicon Limited.
 */

#include <linux/edac.h>

enum edac_scrub_attributes {
	SCRUB_ADDRESS,
	SCRUB_SIZE,
	SCRUB_ENABLE_BACKGROUND,
	SCRUB_MIN_CYCLE_DURATION,
	SCRUB_MAX_CYCLE_DURATION,
	SCRUB_CUR_CYCLE_DURATION,
	SCRUB_MAX_ATTRS
};

struct edac_scrub_dev_attr {
	struct device_attribute dev_attr;
	u8 instance;
};

struct edac_scrub_context {
	char name[EDAC_FEAT_NAME_LEN];
	struct edac_scrub_dev_attr scrub_dev_attr[SCRUB_MAX_ATTRS];
	struct attribute *scrub_attrs[SCRUB_MAX_ATTRS + 1];
	struct attribute_group group;
};

#define TO_SCRUB_DEV_ATTR(_dev_attr)      \
		container_of(_dev_attr, struct edac_scrub_dev_attr, dev_attr)

#define EDAC_SCRUB_ATTR_SHOW(attrib, cb, type, format)				\
static ssize_t attrib##_show(struct device *ras_feat_dev,			\
			     struct device_attribute *attr, char *buf)		\
{										\
	u8 inst = TO_SCRUB_DEV_ATTR(attr)->instance;				\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);		\
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;		\
	type data;								\
	int ret;								\
										\
	ret = ops->cb(ras_feat_dev->parent, ctx->scrub[inst].private, &data);	\
	if (ret)								\
		return ret;							\
										\
	return sysfs_emit(buf, format, data);					\
}

EDAC_SCRUB_ATTR_SHOW(addr, read_addr, u64, "0x%llx\n")
EDAC_SCRUB_ATTR_SHOW(size, read_size, u64, "0x%llx\n")
EDAC_SCRUB_ATTR_SHOW(enable_background, get_enabled_bg, bool, "%u\n")
EDAC_SCRUB_ATTR_SHOW(min_cycle_duration, get_min_cycle, u32, "%u\n")
EDAC_SCRUB_ATTR_SHOW(max_cycle_duration, get_max_cycle, u32, "%u\n")
EDAC_SCRUB_ATTR_SHOW(current_cycle_duration, get_cycle_duration, u32, "%u\n")

#define EDAC_SCRUB_ATTR_STORE(attrib, cb, type, conv_func)			\
static ssize_t attrib##_store(struct device *ras_feat_dev,			\
			      struct device_attribute *attr,			\
			      const char *buf, size_t len)			\
{										\
	u8 inst = TO_SCRUB_DEV_ATTR(attr)->instance;				\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);		\
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;		\
	type data;								\
	int ret;								\
										\
	ret = conv_func(buf, 0, &data);						\
	if (ret < 0)								\
		return ret;							\
										\
	ret = ops->cb(ras_feat_dev->parent, ctx->scrub[inst].private, data);	\
	if (ret)								\
		return ret;							\
										\
	return len;								\
}

EDAC_SCRUB_ATTR_STORE(addr, write_addr, u64, kstrtou64)
EDAC_SCRUB_ATTR_STORE(size, write_size, u64, kstrtou64)
EDAC_SCRUB_ATTR_STORE(enable_background, set_enabled_bg, unsigned long, kstrtoul)
EDAC_SCRUB_ATTR_STORE(current_cycle_duration, set_cycle_duration, unsigned long, kstrtoul)

static umode_t scrub_attr_visible(struct kobject *kobj, struct attribute *a, int attr_id)
{
	struct device *ras_feat_dev = kobj_to_dev(kobj);
	struct device_attribute *dev_attr = container_of(a, struct device_attribute, attr);
	u8 inst = TO_SCRUB_DEV_ATTR(dev_attr)->instance;
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_scrub_ops *ops = ctx->scrub[inst].scrub_ops;

	switch (attr_id) {
	case SCRUB_ADDRESS:
		if (ops->read_addr) {
			if (ops->write_addr)
				return a->mode;
			else
				return 0444;
		}
		break;
	case SCRUB_SIZE:
		if (ops->read_size) {
			if (ops->write_size)
				return a->mode;
			else
				return 0444;
		}
		break;
	case SCRUB_ENABLE_BACKGROUND:
		if (ops->get_enabled_bg) {
			if (ops->set_enabled_bg)
				return a->mode;
			else
				return 0444;
		}
		break;
	case SCRUB_MIN_CYCLE_DURATION:
		if (ops->get_min_cycle)
			return a->mode;
		break;
	case SCRUB_MAX_CYCLE_DURATION:
		if (ops->get_max_cycle)
			return a->mode;
		break;
	case SCRUB_CUR_CYCLE_DURATION:
		if (ops->get_cycle_duration) {
			if (ops->set_cycle_duration)
				return a->mode;
			else
				return 0444;
		}
		break;
	default:
		break;
	}

	return 0;
}

#define EDAC_SCRUB_ATTR_RO(_name, _instance)       \
	((struct edac_scrub_dev_attr) { .dev_attr = __ATTR_RO(_name), \
					.instance = _instance })

#define EDAC_SCRUB_ATTR_WO(_name, _instance)       \
	((struct edac_scrub_dev_attr) { .dev_attr = __ATTR_WO(_name), \
					.instance = _instance })

#define EDAC_SCRUB_ATTR_RW(_name, _instance)       \
	((struct edac_scrub_dev_attr) { .dev_attr = __ATTR_RW(_name), \
					.instance = _instance })

static int scrub_create_desc(struct device *scrub_dev,
			     const struct attribute_group **attr_groups, u8 instance)
{
	struct edac_scrub_context *scrub_ctx;
	struct attribute_group *group;
	int i;
	struct edac_scrub_dev_attr dev_attr[] = {
		[SCRUB_ADDRESS] = EDAC_SCRUB_ATTR_RW(addr, instance),
		[SCRUB_SIZE] = EDAC_SCRUB_ATTR_RW(size, instance),
		[SCRUB_ENABLE_BACKGROUND] = EDAC_SCRUB_ATTR_RW(enable_background, instance),
		[SCRUB_MIN_CYCLE_DURATION] = EDAC_SCRUB_ATTR_RO(min_cycle_duration, instance),
		[SCRUB_MAX_CYCLE_DURATION] = EDAC_SCRUB_ATTR_RO(max_cycle_duration, instance),
		[SCRUB_CUR_CYCLE_DURATION] = EDAC_SCRUB_ATTR_RW(current_cycle_duration, instance)
	};

	scrub_ctx = devm_kzalloc(scrub_dev, sizeof(*scrub_ctx), GFP_KERNEL);
	if (!scrub_ctx)
		return -ENOMEM;

	group = &scrub_ctx->group;
	for (i = 0; i < SCRUB_MAX_ATTRS; i++) {
		memcpy(&scrub_ctx->scrub_dev_attr[i], &dev_attr[i], sizeof(dev_attr[i]));
		sysfs_attr_init(&scrub_ctx->scrub_dev_attr[i].dev_attr.attr);
		scrub_ctx->scrub_attrs[i] = &scrub_ctx->scrub_dev_attr[i].dev_attr.attr;
	}
	sprintf(scrub_ctx->name, "%s%d", "scrub", instance);
	group->name = scrub_ctx->name;
	group->attrs = scrub_ctx->scrub_attrs;
	group->is_visible  = scrub_attr_visible;

	attr_groups[0] = group;

	return 0;
}

/**
 * edac_scrub_get_desc - get EDAC scrub descriptors
 * @scrub_dev: client device, with scrub support
 * @attr_groups: pointer to attribute group container
 * @instance: device's scrub instance number.
 *
 * Return:
 *  * %0	- Success.
 *  * %-EINVAL	- Invalid parameters passed.
 *  * %-ENOMEM	- Dynamic memory allocation failed.
 */
int edac_scrub_get_desc(struct device *scrub_dev,
			const struct attribute_group **attr_groups, u8 instance)
{
	if (!scrub_dev || !attr_groups)
		return -EINVAL;

	return scrub_create_desc(scrub_dev, attr_groups, instance);
}
