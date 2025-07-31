// SPDX-License-Identifier: GPL-2.0
/*
 * The generic EDAC memory repair driver is designed to control the memory
 * devices with memory repair features, such as Post Package Repair (PPR),
 * memory sparing etc. The common sysfs memory repair interface abstracts
 * the control of various arbitrary memory repair functionalities into a
 * unified set of functions.
 *
 * Copyright (c) 2024-2025 HiSilicon Limited.
 */

#include <linux/edac.h>

enum edac_mem_repair_attributes {
	MR_TYPE,
	MR_PERSIST_MODE,
	MR_SAFE_IN_USE,
	MR_HPA,
	MR_MIN_HPA,
	MR_MAX_HPA,
	MR_DPA,
	MR_MIN_DPA,
	MR_MAX_DPA,
	MR_NIBBLE_MASK,
	MR_BANK_GROUP,
	MR_BANK,
	MR_RANK,
	MR_ROW,
	MR_COLUMN,
	MR_CHANNEL,
	MR_SUB_CHANNEL,
	MEM_DO_REPAIR,
	MR_MAX_ATTRS
};

struct edac_mem_repair_dev_attr {
	struct device_attribute dev_attr;
	u8 instance;
};

struct edac_mem_repair_context {
	char name[EDAC_FEAT_NAME_LEN];
	struct edac_mem_repair_dev_attr mem_repair_dev_attr[MR_MAX_ATTRS];
	struct attribute *mem_repair_attrs[MR_MAX_ATTRS + 1];
	struct attribute_group group;
};

const char * const edac_repair_type[] = {
	[EDAC_REPAIR_PPR] = "ppr",
	[EDAC_REPAIR_CACHELINE_SPARING] = "cacheline-sparing",
	[EDAC_REPAIR_ROW_SPARING] = "row-sparing",
	[EDAC_REPAIR_BANK_SPARING] = "bank-sparing",
	[EDAC_REPAIR_RANK_SPARING] = "rank-sparing",
};
EXPORT_SYMBOL_GPL(edac_repair_type);

#define TO_MR_DEV_ATTR(_dev_attr)      \
	container_of(_dev_attr, struct edac_mem_repair_dev_attr, dev_attr)

#define MR_ATTR_SHOW(attrib, cb, type, format)			\
static ssize_t attrib##_show(struct device *ras_feat_dev,			\
			     struct device_attribute *attr, char *buf)		\
{										\
	u8 inst = TO_MR_DEV_ATTR(attr)->instance;			\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);		\
	const struct edac_mem_repair_ops *ops =					\
		ctx->mem_repair[inst].mem_repair_ops;				\
	type data;								\
	int ret;								\
										\
	ret = ops->cb(ras_feat_dev->parent, ctx->mem_repair[inst].private,	\
		      &data);							\
	if (ret)								\
		return ret;							\
										\
	return sysfs_emit(buf, format, data);					\
}

MR_ATTR_SHOW(repair_type, get_repair_type, const char *, "%s\n")
MR_ATTR_SHOW(persist_mode, get_persist_mode, bool, "%u\n")
MR_ATTR_SHOW(repair_safe_when_in_use, get_repair_safe_when_in_use, bool, "%u\n")
MR_ATTR_SHOW(hpa, get_hpa, u64, "0x%llx\n")
MR_ATTR_SHOW(min_hpa, get_min_hpa, u64, "0x%llx\n")
MR_ATTR_SHOW(max_hpa, get_max_hpa, u64, "0x%llx\n")
MR_ATTR_SHOW(dpa, get_dpa, u64, "0x%llx\n")
MR_ATTR_SHOW(min_dpa, get_min_dpa, u64, "0x%llx\n")
MR_ATTR_SHOW(max_dpa, get_max_dpa, u64, "0x%llx\n")
MR_ATTR_SHOW(nibble_mask, get_nibble_mask, u32, "0x%x\n")
MR_ATTR_SHOW(bank_group, get_bank_group, u32, "%u\n")
MR_ATTR_SHOW(bank, get_bank, u32, "%u\n")
MR_ATTR_SHOW(rank, get_rank, u32, "%u\n")
MR_ATTR_SHOW(row, get_row, u32, "0x%x\n")
MR_ATTR_SHOW(column, get_column, u32, "%u\n")
MR_ATTR_SHOW(channel, get_channel, u32, "%u\n")
MR_ATTR_SHOW(sub_channel, get_sub_channel, u32, "%u\n")

#define MR_ATTR_STORE(attrib, cb, type, conv_func)			\
static ssize_t attrib##_store(struct device *ras_feat_dev,			\
			      struct device_attribute *attr,			\
			      const char *buf, size_t len)			\
{										\
	u8 inst = TO_MR_DEV_ATTR(attr)->instance;			\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);		\
	const struct edac_mem_repair_ops *ops =					\
		ctx->mem_repair[inst].mem_repair_ops;				\
	type data;								\
	int ret;								\
										\
	ret = conv_func(buf, 0, &data);						\
	if (ret < 0)								\
		return ret;							\
										\
	ret = ops->cb(ras_feat_dev->parent, ctx->mem_repair[inst].private,	\
		      data);							\
	if (ret)								\
		return ret;							\
										\
	return len;								\
}

MR_ATTR_STORE(persist_mode, set_persist_mode, unsigned long, kstrtoul)
MR_ATTR_STORE(hpa, set_hpa, u64, kstrtou64)
MR_ATTR_STORE(dpa, set_dpa, u64, kstrtou64)
MR_ATTR_STORE(nibble_mask, set_nibble_mask, unsigned long, kstrtoul)
MR_ATTR_STORE(bank_group, set_bank_group, unsigned long, kstrtoul)
MR_ATTR_STORE(bank, set_bank, unsigned long, kstrtoul)
MR_ATTR_STORE(rank, set_rank, unsigned long, kstrtoul)
MR_ATTR_STORE(row, set_row, unsigned long, kstrtoul)
MR_ATTR_STORE(column, set_column, unsigned long, kstrtoul)
MR_ATTR_STORE(channel, set_channel, unsigned long, kstrtoul)
MR_ATTR_STORE(sub_channel, set_sub_channel, unsigned long, kstrtoul)

#define MR_DO_OP(attrib, cb)						\
static ssize_t attrib##_store(struct device *ras_feat_dev,				\
			      struct device_attribute *attr,				\
			      const char *buf, size_t len)				\
{											\
	u8 inst = TO_MR_DEV_ATTR(attr)->instance;				\
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);			\
	const struct edac_mem_repair_ops *ops = ctx->mem_repair[inst].mem_repair_ops;	\
	unsigned long data;								\
	int ret;									\
											\
	ret = kstrtoul(buf, 0, &data);							\
	if (ret < 0)									\
		return ret;								\
											\
	ret = ops->cb(ras_feat_dev->parent, ctx->mem_repair[inst].private, data);	\
	if (ret)									\
		return ret;								\
											\
	return len;									\
}

MR_DO_OP(repair, do_repair)

static umode_t mem_repair_attr_visible(struct kobject *kobj, struct attribute *a, int attr_id)
{
	struct device *ras_feat_dev = kobj_to_dev(kobj);
	struct device_attribute *dev_attr = container_of(a, struct device_attribute, attr);
	struct edac_dev_feat_ctx *ctx = dev_get_drvdata(ras_feat_dev);
	u8 inst = TO_MR_DEV_ATTR(dev_attr)->instance;
	const struct edac_mem_repair_ops *ops = ctx->mem_repair[inst].mem_repair_ops;

	switch (attr_id) {
	case MR_TYPE:
		if (ops->get_repair_type)
			return a->mode;
		break;
	case MR_PERSIST_MODE:
		if (ops->get_persist_mode) {
			if (ops->set_persist_mode)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_SAFE_IN_USE:
		if (ops->get_repair_safe_when_in_use)
			return a->mode;
		break;
	case MR_HPA:
		if (ops->get_hpa) {
			if (ops->set_hpa)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_MIN_HPA:
		if (ops->get_min_hpa)
			return a->mode;
		break;
	case MR_MAX_HPA:
		if (ops->get_max_hpa)
			return a->mode;
		break;
	case MR_DPA:
		if (ops->get_dpa) {
			if (ops->set_dpa)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_MIN_DPA:
		if (ops->get_min_dpa)
			return a->mode;
		break;
	case MR_MAX_DPA:
		if (ops->get_max_dpa)
			return a->mode;
		break;
	case MR_NIBBLE_MASK:
		if (ops->get_nibble_mask) {
			if (ops->set_nibble_mask)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_BANK_GROUP:
		if (ops->get_bank_group) {
			if (ops->set_bank_group)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_BANK:
		if (ops->get_bank) {
			if (ops->set_bank)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_RANK:
		if (ops->get_rank) {
			if (ops->set_rank)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_ROW:
		if (ops->get_row) {
			if (ops->set_row)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_COLUMN:
		if (ops->get_column) {
			if (ops->set_column)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_CHANNEL:
		if (ops->get_channel) {
			if (ops->set_channel)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MR_SUB_CHANNEL:
		if (ops->get_sub_channel) {
			if (ops->set_sub_channel)
				return a->mode;
			else
				return 0444;
		}
		break;
	case MEM_DO_REPAIR:
		if (ops->do_repair)
			return a->mode;
		break;
	default:
		break;
	}

	return 0;
}

#define MR_ATTR_RO(_name, _instance)       \
	((struct edac_mem_repair_dev_attr) { .dev_attr = __ATTR_RO(_name), \
					     .instance = _instance })

#define MR_ATTR_WO(_name, _instance)       \
	((struct edac_mem_repair_dev_attr) { .dev_attr = __ATTR_WO(_name), \
					     .instance = _instance })

#define MR_ATTR_RW(_name, _instance)       \
	((struct edac_mem_repair_dev_attr) { .dev_attr = __ATTR_RW(_name), \
					     .instance = _instance })

static int mem_repair_create_desc(struct device *dev,
				  const struct attribute_group **attr_groups,
				  u8 instance)
{
	struct edac_mem_repair_context *ctx;
	struct attribute_group *group;
	int i;
	struct edac_mem_repair_dev_attr dev_attr[] = {
		[MR_TYPE]	  = MR_ATTR_RO(repair_type, instance),
		[MR_PERSIST_MODE] = MR_ATTR_RW(persist_mode, instance),
		[MR_SAFE_IN_USE]  = MR_ATTR_RO(repair_safe_when_in_use, instance),
		[MR_HPA]	  = MR_ATTR_RW(hpa, instance),
		[MR_MIN_HPA]	  = MR_ATTR_RO(min_hpa, instance),
		[MR_MAX_HPA]	  = MR_ATTR_RO(max_hpa, instance),
		[MR_DPA]	  = MR_ATTR_RW(dpa, instance),
		[MR_MIN_DPA]	  = MR_ATTR_RO(min_dpa, instance),
		[MR_MAX_DPA]	  = MR_ATTR_RO(max_dpa, instance),
		[MR_NIBBLE_MASK]  = MR_ATTR_RW(nibble_mask, instance),
		[MR_BANK_GROUP]   = MR_ATTR_RW(bank_group, instance),
		[MR_BANK]	  = MR_ATTR_RW(bank, instance),
		[MR_RANK]	  = MR_ATTR_RW(rank, instance),
		[MR_ROW]	  = MR_ATTR_RW(row, instance),
		[MR_COLUMN]	  = MR_ATTR_RW(column, instance),
		[MR_CHANNEL]	  = MR_ATTR_RW(channel, instance),
		[MR_SUB_CHANNEL]  = MR_ATTR_RW(sub_channel, instance),
		[MEM_DO_REPAIR]	  = MR_ATTR_WO(repair, instance)
	};

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	for (i = 0; i < MR_MAX_ATTRS; i++) {
		memcpy(&ctx->mem_repair_dev_attr[i],
		       &dev_attr[i], sizeof(dev_attr[i]));
		ctx->mem_repair_attrs[i] =
			&ctx->mem_repair_dev_attr[i].dev_attr.attr;
	}

	sprintf(ctx->name, "%s%d", "mem_repair", instance);
	group = &ctx->group;
	group->name = ctx->name;
	group->attrs = ctx->mem_repair_attrs;
	group->is_visible  = mem_repair_attr_visible;
	attr_groups[0] = group;

	return 0;
}

/**
 * edac_mem_repair_get_desc - get EDAC memory repair descriptors
 * @dev: client device with memory repair feature
 * @attr_groups: pointer to attribute group container
 * @instance: device's memory repair instance number.
 *
 * Return:
 *  * %0	- Success.
 *  * %-EINVAL	- Invalid parameters passed.
 *  * %-ENOMEM	- Dynamic memory allocation failed.
 */
int edac_mem_repair_get_desc(struct device *dev,
			     const struct attribute_group **attr_groups, u8 instance)
{
	if (!dev || !attr_groups)
		return -EINVAL;

	return mem_repair_create_desc(dev, attr_groups, instance);
}
