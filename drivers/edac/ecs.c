// SPDX-License-Identifier: GPL-2.0
/*
 * The generic ECS driver is designed to support control of on-die error
 * check scrub (e.g., DDR5 ECS). The common sysfs ECS interface abstracts
 * the control of various ECS functionalities into a unified set of functions.
 *
 * Copyright (c) 2024-2025 HiSilicon Limited.
 */

#include <linux/edac.h>

#define EDAC_ECS_FRU_NAME "ecs_fru"

enum edac_ecs_attributes {
	ECS_LOG_ENTRY_TYPE,
	ECS_MODE,
	ECS_RESET,
	ECS_THRESHOLD,
	ECS_MAX_ATTRS
};

struct edac_ecs_dev_attr {
	struct device_attribute dev_attr;
	int fru_id;
};

struct edac_ecs_fru_context {
	char name[EDAC_FEAT_NAME_LEN];
	struct edac_ecs_dev_attr dev_attr[ECS_MAX_ATTRS];
	struct attribute *ecs_attrs[ECS_MAX_ATTRS + 1];
	struct attribute_group group;
};

struct edac_ecs_context {
	u16 num_media_frus;
	struct edac_ecs_fru_context *fru_ctxs;
};

#define TO_ECS_DEV_ATTR(_dev_attr)	\
	container_of(_dev_attr, struct edac_ecs_dev_attr, dev_attr)

#define EDAC_ECS_ATTR_SHOW(attrib, cb, type, format)				\
static ssize_t attrib##_show(struct device *ras_feat_dev,			\
			     struct device_attribute *attr, char *buf)		\
{										\
	struct edac_ecs_dev_attr *dev_attr = TO_ECS_DEV_ATTR(attr);		\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);		\
	const struct edac_ecs_ops *ops = ctx->ecs.ecs_ops;			\
	type data;								\
	int ret;								\
										\
	ret = ops->cb(ras_feat_dev->parent, ctx->ecs.private,			\
		      dev_attr->fru_id, &data);					\
	if (ret)								\
		return ret;							\
										\
	return sysfs_emit(buf, format, data);					\
}

EDAC_ECS_ATTR_SHOW(log_entry_type, get_log_entry_type, u32, "%u\n")
EDAC_ECS_ATTR_SHOW(mode, get_mode, u32, "%u\n")
EDAC_ECS_ATTR_SHOW(threshold, get_threshold, u32, "%u\n")

#define EDAC_ECS_ATTR_STORE(attrib, cb, type, conv_func)			\
static ssize_t attrib##_store(struct device *ras_feat_dev,			\
			      struct device_attribute *attr,			\
			      const char *buf, size_t len)			\
{										\
	struct edac_ecs_dev_attr *dev_attr = TO_ECS_DEV_ATTR(attr);		\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);		\
	const struct edac_ecs_ops *ops = ctx->ecs.ecs_ops;			\
	type data;								\
	int ret;								\
										\
	ret = conv_func(buf, 0, &data);						\
	if (ret < 0)								\
		return ret;							\
										\
	ret = ops->cb(ras_feat_dev->parent, ctx->ecs.private,			\
		      dev_attr->fru_id, data);					\
	if (ret)								\
		return ret;							\
										\
	return len;								\
}

EDAC_ECS_ATTR_STORE(log_entry_type, set_log_entry_type, unsigned long, kstrtoul)
EDAC_ECS_ATTR_STORE(mode, set_mode, unsigned long, kstrtoul)
EDAC_ECS_ATTR_STORE(reset, reset, unsigned long, kstrtoul)
EDAC_ECS_ATTR_STORE(threshold, set_threshold, unsigned long, kstrtoul)

static umode_t ecs_attr_visible(struct kobject *kobj, struct attribute *a, int attr_id)
{
	struct device *ras_feat_dev = kobj_to_dev(kobj);
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	const struct edac_ecs_ops *ops = ctx->ecs.ecs_ops;

	switch (attr_id) {
	case ECS_LOG_ENTRY_TYPE:
		if (ops->get_log_entry_type)  {
			if (ops->set_log_entry_type)
				return a->mode;
			else
				return 0444;
		}
		break;
	case ECS_MODE:
		if (ops->get_mode) {
			if (ops->set_mode)
				return a->mode;
			else
				return 0444;
		}
		break;
	case ECS_RESET:
		if (ops->reset)
			return a->mode;
		break;
	case ECS_THRESHOLD:
		if (ops->get_threshold) {
			if (ops->set_threshold)
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

#define EDAC_ECS_ATTR_RO(_name, _fru_id)       \
	((struct edac_ecs_dev_attr) { .dev_attr = __ATTR_RO(_name), \
				     .fru_id = _fru_id })

#define EDAC_ECS_ATTR_WO(_name, _fru_id)       \
	((struct edac_ecs_dev_attr) { .dev_attr = __ATTR_WO(_name), \
				     .fru_id = _fru_id })

#define EDAC_ECS_ATTR_RW(_name, _fru_id)       \
	((struct edac_ecs_dev_attr) { .dev_attr = __ATTR_RW(_name), \
				     .fru_id = _fru_id })

static int ecs_create_desc(struct device *ecs_dev, const struct attribute_group **attr_groups,
			   u16 num_media_frus)
{
	struct edac_ecs_context *ecs_ctx;
	u32 fru;

	ecs_ctx = devm_kzalloc(ecs_dev, sizeof(*ecs_ctx), GFP_KERNEL);
	if (!ecs_ctx)
		return -ENOMEM;

	ecs_ctx->num_media_frus = num_media_frus;
	ecs_ctx->fru_ctxs = devm_kcalloc(ecs_dev, num_media_frus,
					 sizeof(*ecs_ctx->fru_ctxs),
					 GFP_KERNEL);
	if (!ecs_ctx->fru_ctxs)
		return -ENOMEM;

	for (fru = 0; fru < num_media_frus; fru++) {
		struct edac_ecs_fru_context *fru_ctx = &ecs_ctx->fru_ctxs[fru];
		struct attribute_group *group = &fru_ctx->group;
		int i;

		fru_ctx->dev_attr[ECS_LOG_ENTRY_TYPE]	= EDAC_ECS_ATTR_RW(log_entry_type, fru);
		fru_ctx->dev_attr[ECS_MODE]		= EDAC_ECS_ATTR_RW(mode, fru);
		fru_ctx->dev_attr[ECS_RESET]		= EDAC_ECS_ATTR_WO(reset, fru);
		fru_ctx->dev_attr[ECS_THRESHOLD]	= EDAC_ECS_ATTR_RW(threshold, fru);

		for (i = 0; i < ECS_MAX_ATTRS; i++)
			fru_ctx->ecs_attrs[i] = &fru_ctx->dev_attr[i].dev_attr.attr;

		sprintf(fru_ctx->name, "%s%d", EDAC_ECS_FRU_NAME, fru);
		group->name = fru_ctx->name;
		group->attrs = fru_ctx->ecs_attrs;
		group->is_visible  = ecs_attr_visible;

		attr_groups[fru] = group;
	}

	return 0;
}

/**
 * edac_ecs_get_desc - get EDAC ECS descriptors
 * @ecs_dev: client device, supports ECS feature
 * @attr_groups: pointer to attribute group container
 * @num_media_frus: number of media FRUs in the device
 *
 * Return:
 *  * %0	- Success.
 *  * %-EINVAL	- Invalid parameters passed.
 *  * %-ENOMEM	- Dynamic memory allocation failed.
 */
int edac_ecs_get_desc(struct device *ecs_dev,
		      const struct attribute_group **attr_groups, u16 num_media_frus)
{
	if (!ecs_dev || !attr_groups || !num_media_frus)
		return -EINVAL;

	return ecs_create_desc(ecs_dev, attr_groups, num_media_frus);
}
