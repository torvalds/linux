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

/*
 * Slave sysfs
 */

/*
 * The sysfs for Slave reflects the MIPI description as given
 * in the MIPI DisCo spec
 *
 * Base file is device
 *	|---- modalias
 *	|---- dev-properties
 *		|---- mipi_revision
 *		|---- wake_capable
 *		|---- test_mode_capable
 *		|---- clk_stop_mode1
 *		|---- simple_clk_stop_capable
 *		|---- clk_stop_timeout
 *		|---- ch_prep_timeout
 *		|---- reset_behave
 *		|---- high_PHY_capable
 *		|---- paging_support
 *		|---- bank_delay_support
 *		|---- p15_behave
 *		|---- master_count
 *		|---- source_ports
 *		|---- sink_ports
 *	|---- dp0
 *		|---- max_word
 *		|---- min_word
 *		|---- words
 *		|---- BRA_flow_controlled
 *		|---- simple_ch_prep_sm
 *		|---- imp_def_interrupts
 *	|---- dpN_<sink/src>
 *		|---- max_word
 *		|---- min_word
 *		|---- words
 *		|---- type
 *		|---- max_grouping
 *		|---- simple_ch_prep_sm
 *		|---- ch_prep_timeout
 *		|---- imp_def_interrupts
 *		|---- min_ch
 *		|---- max_ch
 *		|---- channels
 *		|---- ch_combinations
 *		|---- max_async_buffer
 *		|---- block_pack_mode
 *		|---- port_encoding
 *
 */

#define sdw_slave_attr(field, format_string)			\
static ssize_t field##_show(struct device *dev,			\
			    struct device_attribute *attr,	\
			    char *buf)				\
{								\
	struct sdw_slave *slave = dev_to_sdw_dev(dev);		\
	return sprintf(buf, format_string, slave->prop.field);	\
}								\
static DEVICE_ATTR_RO(field)

sdw_slave_attr(mipi_revision, "0x%x\n");
sdw_slave_attr(wake_capable, "%d\n");
sdw_slave_attr(test_mode_capable, "%d\n");
sdw_slave_attr(clk_stop_mode1, "%d\n");
sdw_slave_attr(simple_clk_stop_capable, "%d\n");
sdw_slave_attr(clk_stop_timeout, "%d\n");
sdw_slave_attr(ch_prep_timeout, "%d\n");
sdw_slave_attr(reset_behave, "%d\n");
sdw_slave_attr(high_PHY_capable, "%d\n");
sdw_slave_attr(paging_support, "%d\n");
sdw_slave_attr(bank_delay_support, "%d\n");
sdw_slave_attr(p15_behave, "%d\n");
sdw_slave_attr(master_count, "%d\n");
sdw_slave_attr(source_ports, "0x%x\n");
sdw_slave_attr(sink_ports, "0x%x\n");

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);

	return sdw_slave_modalias(slave, buf, 256);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *slave_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(slave);

static struct attribute *slave_dev_attrs[] = {
	&dev_attr_mipi_revision.attr,
	&dev_attr_wake_capable.attr,
	&dev_attr_test_mode_capable.attr,
	&dev_attr_clk_stop_mode1.attr,
	&dev_attr_simple_clk_stop_capable.attr,
	&dev_attr_clk_stop_timeout.attr,
	&dev_attr_ch_prep_timeout.attr,
	&dev_attr_reset_behave.attr,
	&dev_attr_high_PHY_capable.attr,
	&dev_attr_paging_support.attr,
	&dev_attr_bank_delay_support.attr,
	&dev_attr_p15_behave.attr,
	&dev_attr_master_count.attr,
	&dev_attr_source_ports.attr,
	&dev_attr_sink_ports.attr,
	NULL,
};

/*
 * we don't use ATTRIBUTES_GROUP here since we want to add a subdirectory
 * for device-level properties
 */
static struct attribute_group sdw_slave_dev_attr_group = {
	.attrs	= slave_dev_attrs,
	.name = "dev-properties",
};

/*
 * DP0 sysfs
 */

#define sdw_dp0_attr(field, format_string)				\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct sdw_slave *slave = dev_to_sdw_dev(dev);			\
	return sprintf(buf, format_string, slave->prop.dp0_prop->field);\
}									\
static DEVICE_ATTR_RO(field)

sdw_dp0_attr(max_word, "%d\n");
sdw_dp0_attr(min_word, "%d\n");
sdw_dp0_attr(BRA_flow_controlled, "%d\n");
sdw_dp0_attr(simple_ch_prep_sm, "%d\n");
sdw_dp0_attr(imp_def_interrupts, "0x%x\n");

static ssize_t words_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	ssize_t size = 0;
	int i;

	for (i = 0; i < slave->prop.dp0_prop->num_words; i++)
		size += sprintf(buf + size, "%d ",
				slave->prop.dp0_prop->words[i]);
	size += sprintf(buf + size, "\n");

	return size;
}
static DEVICE_ATTR_RO(words);

static struct attribute *dp0_attrs[] = {
	&dev_attr_max_word.attr,
	&dev_attr_min_word.attr,
	&dev_attr_words.attr,
	&dev_attr_BRA_flow_controlled.attr,
	&dev_attr_simple_ch_prep_sm.attr,
	&dev_attr_imp_def_interrupts.attr,
	NULL,
};

/*
 * we don't use ATTRIBUTES_GROUP here since we want to add a subdirectory
 * for dp0-level properties
 */
static const struct attribute_group dp0_group = {
	.attrs = dp0_attrs,
	.name = "dp0",
};

int sdw_slave_sysfs_init(struct sdw_slave *slave)
{
	int ret;

	ret = devm_device_add_groups(&slave->dev, slave_groups);
	if (ret < 0)
		return ret;

	ret = devm_device_add_group(&slave->dev, &sdw_slave_dev_attr_group);
	if (ret < 0)
		return ret;

	if (slave->prop.dp0_prop) {
		ret = devm_device_add_group(&slave->dev, &dp0_group);
		if (ret < 0)
			return ret;
	}

	if (slave->prop.source_ports || slave->prop.sink_ports) {
		ret = sdw_slave_sysfs_dpn_init(slave);
		if (ret < 0)
			return ret;
	}

	return 0;
}
