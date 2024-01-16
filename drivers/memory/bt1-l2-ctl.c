// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Authors:
 *   Serge Semin <Sergey.Semin@baikalelectronics.ru>
 *
 * Baikal-T1 CM2 L2-cache Control Block driver.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/sysfs.h>
#include <linux/of.h>

#define L2_CTL_REG			0x028
#define L2_CTL_DATA_STALL_FLD		0
#define L2_CTL_DATA_STALL_MASK		GENMASK(1, L2_CTL_DATA_STALL_FLD)
#define L2_CTL_TAG_STALL_FLD		2
#define L2_CTL_TAG_STALL_MASK		GENMASK(3, L2_CTL_TAG_STALL_FLD)
#define L2_CTL_WS_STALL_FLD		4
#define L2_CTL_WS_STALL_MASK		GENMASK(5, L2_CTL_WS_STALL_FLD)
#define L2_CTL_SET_CLKRATIO		BIT(13)
#define L2_CTL_CLKRATIO_LOCK		BIT(31)

#define L2_CTL_STALL_MIN		0
#define L2_CTL_STALL_MAX		3
#define L2_CTL_STALL_SET_DELAY_US	1
#define L2_CTL_STALL_SET_TOUT_US	1000

/*
 * struct l2_ctl - Baikal-T1 L2 Control block private data.
 * @dev: Pointer to the device structure.
 * @sys_regs: Baikal-T1 System Controller registers map.
 */
struct l2_ctl {
	struct device *dev;

	struct regmap *sys_regs;
};

/*
 * enum l2_ctl_stall - Baikal-T1 L2-cache-RAM stall identifier.
 * @L2_WSSTALL: Way-select latency.
 * @L2_TAGSTALL: Tag latency.
 * @L2_DATASTALL: Data latency.
 */
enum l2_ctl_stall {
	L2_WS_STALL,
	L2_TAG_STALL,
	L2_DATA_STALL
};

/*
 * struct l2_ctl_device_attribute - Baikal-T1 L2-cache device attribute.
 * @dev_attr: Actual sysfs device attribute.
 * @id: L2-cache stall field identifier.
 */
struct l2_ctl_device_attribute {
	struct device_attribute dev_attr;
	enum l2_ctl_stall id;
};

#define to_l2_ctl_dev_attr(_dev_attr) \
	container_of(_dev_attr, struct l2_ctl_device_attribute, dev_attr)

#define L2_CTL_ATTR_RW(_name, _prefix, _id) \
	struct l2_ctl_device_attribute l2_ctl_attr_##_name = \
		{ __ATTR(_name, 0644, _prefix##_show, _prefix##_store),	_id }

static int l2_ctl_get_latency(struct l2_ctl *l2, enum l2_ctl_stall id, u32 *val)
{
	u32 data = 0;
	int ret;

	ret = regmap_read(l2->sys_regs, L2_CTL_REG, &data);
	if (ret)
		return ret;

	switch (id) {
	case L2_WS_STALL:
		*val = FIELD_GET(L2_CTL_WS_STALL_MASK, data);
		break;
	case L2_TAG_STALL:
		*val = FIELD_GET(L2_CTL_TAG_STALL_MASK, data);
		break;
	case L2_DATA_STALL:
		*val = FIELD_GET(L2_CTL_DATA_STALL_MASK, data);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int l2_ctl_set_latency(struct l2_ctl *l2, enum l2_ctl_stall id, u32 val)
{
	u32 mask = 0, data = 0;
	int ret;

	val = clamp_val(val, L2_CTL_STALL_MIN, L2_CTL_STALL_MAX);

	switch (id) {
	case L2_WS_STALL:
		data = FIELD_PREP(L2_CTL_WS_STALL_MASK, val);
		mask = L2_CTL_WS_STALL_MASK;
		break;
	case L2_TAG_STALL:
		data = FIELD_PREP(L2_CTL_TAG_STALL_MASK, val);
		mask = L2_CTL_TAG_STALL_MASK;
		break;
	case L2_DATA_STALL:
		data = FIELD_PREP(L2_CTL_DATA_STALL_MASK, val);
		mask = L2_CTL_DATA_STALL_MASK;
		break;
	default:
		return -EINVAL;
	}

	data |= L2_CTL_SET_CLKRATIO;
	mask |= L2_CTL_SET_CLKRATIO;

	ret = regmap_update_bits(l2->sys_regs, L2_CTL_REG, mask, data);
	if (ret)
		return ret;

	return regmap_read_poll_timeout(l2->sys_regs, L2_CTL_REG, data,
					data & L2_CTL_CLKRATIO_LOCK,
					L2_CTL_STALL_SET_DELAY_US,
					L2_CTL_STALL_SET_TOUT_US);
}

static void l2_ctl_clear_data(void *data)
{
	struct l2_ctl *l2 = data;
	struct platform_device *pdev = to_platform_device(l2->dev);

	platform_set_drvdata(pdev, NULL);
}

static struct l2_ctl *l2_ctl_create_data(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct l2_ctl *l2;
	int ret;

	l2 = devm_kzalloc(dev, sizeof(*l2), GFP_KERNEL);
	if (!l2)
		return ERR_PTR(-ENOMEM);

	ret = devm_add_action(dev, l2_ctl_clear_data, l2);
	if (ret) {
		dev_err(dev, "Can't add L2 CTL data clear action\n");
		return ERR_PTR(ret);
	}

	l2->dev = dev;
	platform_set_drvdata(pdev, l2);

	return l2;
}

static int l2_ctl_find_sys_regs(struct l2_ctl *l2)
{
	l2->sys_regs = syscon_node_to_regmap(l2->dev->of_node->parent);
	if (IS_ERR(l2->sys_regs)) {
		dev_err(l2->dev, "Couldn't get L2 CTL register map\n");
		return PTR_ERR(l2->sys_regs);
	}

	return 0;
}

static int l2_ctl_of_parse_property(struct l2_ctl *l2, enum l2_ctl_stall id,
				    const char *propname)
{
	int ret = 0;
	u32 data;

	if (!of_property_read_u32(l2->dev->of_node, propname, &data)) {
		ret = l2_ctl_set_latency(l2, id, data);
		if (ret)
			dev_err(l2->dev, "Invalid value of '%s'\n", propname);
	}

	return ret;
}

static int l2_ctl_of_parse(struct l2_ctl *l2)
{
	int ret;

	ret = l2_ctl_of_parse_property(l2, L2_WS_STALL, "baikal,l2-ws-latency");
	if (ret)
		return ret;

	ret = l2_ctl_of_parse_property(l2, L2_TAG_STALL, "baikal,l2-tag-latency");
	if (ret)
		return ret;

	return l2_ctl_of_parse_property(l2, L2_DATA_STALL,
					"baikal,l2-data-latency");
}

static ssize_t l2_ctl_latency_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct l2_ctl_device_attribute *devattr = to_l2_ctl_dev_attr(attr);
	struct l2_ctl *l2 = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = l2_ctl_get_latency(l2, devattr->id, &data);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%u\n", data);
}

static ssize_t l2_ctl_latency_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct l2_ctl_device_attribute *devattr = to_l2_ctl_dev_attr(attr);
	struct l2_ctl *l2 = dev_get_drvdata(dev);
	u32 data;
	int ret;

	if (kstrtouint(buf, 0, &data) < 0)
		return -EINVAL;

	ret = l2_ctl_set_latency(l2, devattr->id, data);
	if (ret)
		return ret;

	return count;
}

static L2_CTL_ATTR_RW(l2_ws_latency, l2_ctl_latency, L2_WS_STALL);
static L2_CTL_ATTR_RW(l2_tag_latency, l2_ctl_latency, L2_TAG_STALL);
static L2_CTL_ATTR_RW(l2_data_latency, l2_ctl_latency, L2_DATA_STALL);

static struct attribute *l2_ctl_sysfs_attrs[] = {
	&l2_ctl_attr_l2_ws_latency.dev_attr.attr,
	&l2_ctl_attr_l2_tag_latency.dev_attr.attr,
	&l2_ctl_attr_l2_data_latency.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(l2_ctl_sysfs);

static void l2_ctl_remove_sysfs(void *data)
{
	struct l2_ctl *l2 = data;

	device_remove_groups(l2->dev, l2_ctl_sysfs_groups);
}

static int l2_ctl_init_sysfs(struct l2_ctl *l2)
{
	int ret;

	ret = device_add_groups(l2->dev, l2_ctl_sysfs_groups);
	if (ret) {
		dev_err(l2->dev, "Failed to create L2 CTL sysfs nodes\n");
		return ret;
	}

	ret = devm_add_action_or_reset(l2->dev, l2_ctl_remove_sysfs, l2);
	if (ret)
		dev_err(l2->dev, "Can't add L2 CTL sysfs remove action\n");

	return ret;
}

static int l2_ctl_probe(struct platform_device *pdev)
{
	struct l2_ctl *l2;
	int ret;

	l2 = l2_ctl_create_data(pdev);
	if (IS_ERR(l2))
		return PTR_ERR(l2);

	ret = l2_ctl_find_sys_regs(l2);
	if (ret)
		return ret;

	ret = l2_ctl_of_parse(l2);
	if (ret)
		return ret;

	ret = l2_ctl_init_sysfs(l2);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id l2_ctl_of_match[] = {
	{ .compatible = "baikal,bt1-l2-ctl" },
	{ }
};
MODULE_DEVICE_TABLE(of, l2_ctl_of_match);

static struct platform_driver l2_ctl_driver = {
	.probe = l2_ctl_probe,
	.driver = {
		.name = "bt1-l2-ctl",
		.of_match_table = l2_ctl_of_match
	}
};
module_platform_driver(l2_ctl_driver);

MODULE_AUTHOR("Serge Semin <Sergey.Semin@baikalelectronics.ru>");
MODULE_DESCRIPTION("Baikal-T1 L2-cache driver");
MODULE_LICENSE("GPL v2");
