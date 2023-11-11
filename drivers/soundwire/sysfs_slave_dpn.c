// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2015-2020 Intel Corporation.

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include "bus.h"
#include "sysfs_local.h"

struct dpn_attribute {
	struct device_attribute	dev_attr;
	int N;
	int dir;
	const char *format_string;
};

/*
 * Since we can't use ARRAY_SIZE, hard-code number of dpN attributes.
 * This needs to be updated when adding new attributes - an error will be
 * flagged on a mismatch.
 */
#define SDW_DPN_ATTRIBUTES 15

#define sdw_dpn_attribute_alloc(field)					\
static int field##_attribute_alloc(struct device *dev,			\
				struct attribute **res,			\
				int N, int dir,				\
				const char *format_string)		\
{									\
	struct dpn_attribute *dpn_attr;					\
									\
	dpn_attr = devm_kzalloc(dev, sizeof(*dpn_attr), GFP_KERNEL);	\
	if (!dpn_attr)							\
		return -ENOMEM;						\
	dpn_attr->N = N;						\
	dpn_attr->dir = dir;						\
	sysfs_attr_init(&dpn_attr->dev_attr.attr);			\
	dpn_attr->format_string = format_string;			\
	dpn_attr->dev_attr.attr.name = __stringify(field);		\
	dpn_attr->dev_attr.attr.mode = 0444;				\
	dpn_attr->dev_attr.show = field##_show;				\
									\
	*res = &dpn_attr->dev_attr.attr;				\
									\
	return 0;							\
}

#define sdw_dpn_attr(field)						\
									\
static ssize_t field##_dpn_show(struct sdw_slave *slave,		\
				int N,					\
				int dir,				\
				const char *format_string,		\
				char *buf)				\
{									\
	struct sdw_dpn_prop *dpn;					\
	unsigned long mask;						\
	int bit;							\
	int i;								\
									\
	if (dir) {							\
		dpn = slave->prop.src_dpn_prop;				\
		mask = slave->prop.source_ports;			\
	} else {							\
		dpn = slave->prop.sink_dpn_prop;			\
		mask = slave->prop.sink_ports;				\
	}								\
									\
	i = 0;								\
	for_each_set_bit(bit, &mask, 32) {				\
		if (bit == N) {						\
			return sprintf(buf, format_string,		\
				       dpn[i].field);			\
		}							\
		i++;							\
	}								\
	return -EINVAL;							\
}									\
									\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct sdw_slave *slave = dev_to_sdw_dev(dev);			\
	struct dpn_attribute *dpn_attr =				\
		container_of(attr, struct dpn_attribute, dev_attr);	\
									\
	return field##_dpn_show(slave,					\
				dpn_attr->N, dpn_attr->dir,		\
				dpn_attr->format_string,		\
				buf);					\
}									\
sdw_dpn_attribute_alloc(field)

sdw_dpn_attr(imp_def_interrupts);
sdw_dpn_attr(max_word);
sdw_dpn_attr(min_word);
sdw_dpn_attr(type);
sdw_dpn_attr(max_grouping);
sdw_dpn_attr(simple_ch_prep_sm);
sdw_dpn_attr(ch_prep_timeout);
sdw_dpn_attr(max_ch);
sdw_dpn_attr(min_ch);
sdw_dpn_attr(max_async_buffer);
sdw_dpn_attr(block_pack_mode);
sdw_dpn_attr(port_encoding);

#define sdw_dpn_array_attr(field)					\
									\
static ssize_t field##_dpn_show(struct sdw_slave *slave,		\
				int N,					\
				int dir,				\
				const char *format_string,		\
				char *buf)				\
{									\
	struct sdw_dpn_prop *dpn;					\
	unsigned long mask;						\
	ssize_t size = 0;						\
	int bit;							\
	int i;								\
	int j;								\
									\
	if (dir) {							\
		dpn = slave->prop.src_dpn_prop;				\
		mask = slave->prop.source_ports;			\
	} else {							\
		dpn = slave->prop.sink_dpn_prop;			\
		mask = slave->prop.sink_ports;				\
	}								\
									\
	i = 0;								\
	for_each_set_bit(bit, &mask, 32) {				\
		if (bit == N) {						\
			for (j = 0; j < dpn[i].num_##field; j++)	\
				size += sprintf(buf + size,		\
						format_string,		\
						dpn[i].field[j]);	\
			size += sprintf(buf + size, "\n");		\
			return size;					\
		}							\
		i++;							\
	}								\
	return -EINVAL;							\
}									\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct sdw_slave *slave = dev_to_sdw_dev(dev);			\
	struct dpn_attribute *dpn_attr =				\
		container_of(attr, struct dpn_attribute, dev_attr);	\
									\
	return field##_dpn_show(slave,					\
				dpn_attr->N, dpn_attr->dir,		\
				dpn_attr->format_string,		\
				buf);					\
}									\
sdw_dpn_attribute_alloc(field)

sdw_dpn_array_attr(words);
sdw_dpn_array_attr(ch_combinations);
sdw_dpn_array_attr(channels);

static int add_all_attributes(struct device *dev, int N, int dir)
{
	struct attribute **dpn_attrs;
	struct attribute_group *dpn_group;
	int i = 0;
	int ret;

	/* allocate attributes, last one is NULL */
	dpn_attrs = devm_kcalloc(dev, SDW_DPN_ATTRIBUTES + 1,
				 sizeof(struct attribute *),
				 GFP_KERNEL);
	if (!dpn_attrs)
		return -ENOMEM;

	ret = max_word_attribute_alloc(dev, &dpn_attrs[i++],
				       N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = min_word_attribute_alloc(dev, &dpn_attrs[i++],
				       N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = words_attribute_alloc(dev, &dpn_attrs[i++],
				    N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = type_attribute_alloc(dev, &dpn_attrs[i++],
				   N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = max_grouping_attribute_alloc(dev, &dpn_attrs[i++],
					   N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = simple_ch_prep_sm_attribute_alloc(dev, &dpn_attrs[i++],
						N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = ch_prep_timeout_attribute_alloc(dev, &dpn_attrs[i++],
					      N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = imp_def_interrupts_attribute_alloc(dev, &dpn_attrs[i++],
						 N, dir, "0x%x\n");
	if (ret < 0)
		return ret;

	ret = min_ch_attribute_alloc(dev, &dpn_attrs[i++],
				     N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = max_ch_attribute_alloc(dev, &dpn_attrs[i++],
				     N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = channels_attribute_alloc(dev, &dpn_attrs[i++],
				       N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = ch_combinations_attribute_alloc(dev, &dpn_attrs[i++],
					      N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = max_async_buffer_attribute_alloc(dev, &dpn_attrs[i++],
					       N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = block_pack_mode_attribute_alloc(dev, &dpn_attrs[i++],
					      N, dir, "%d\n");
	if (ret < 0)
		return ret;

	ret = port_encoding_attribute_alloc(dev, &dpn_attrs[i++],
					    N, dir, "%d\n");
	if (ret < 0)
		return ret;

	/* paranoia check for editing mistakes */
	if (i != SDW_DPN_ATTRIBUTES) {
		dev_err(dev, "mismatch in attributes, allocated %d got %d\n",
			SDW_DPN_ATTRIBUTES, i);
		return -EINVAL;
	}

	dpn_group = devm_kzalloc(dev, sizeof(*dpn_group), GFP_KERNEL);
	if (!dpn_group)
		return -ENOMEM;

	dpn_group->attrs = dpn_attrs;
	dpn_group->name = devm_kasprintf(dev, GFP_KERNEL, "dp%d_%s",
					 N, dir ? "src" : "sink");
	if (!dpn_group->name)
		return -ENOMEM;

	ret = devm_device_add_group(dev, dpn_group);
	if (ret < 0)
		return ret;

	return 0;
}

int sdw_slave_sysfs_dpn_init(struct sdw_slave *slave)
{
	unsigned long mask;
	int ret;
	int i;

	mask = slave->prop.source_ports;
	for_each_set_bit(i, &mask, 32) {
		ret = add_all_attributes(&slave->dev, i, 1);
		if (ret < 0)
			return ret;
	}

	mask = slave->prop.sink_ports;
	for_each_set_bit(i, &mask, 32) {
		ret = add_all_attributes(&slave->dev, i, 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}
