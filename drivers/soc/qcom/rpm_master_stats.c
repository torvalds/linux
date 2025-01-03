// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 *
 * This driver supports what is known as "Master Stats v2" in Qualcomm
 * downstream kernel terms, which seems to be the only version which has
 * ever shipped, all the way from 2013 to 2023.
 */

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

struct master_stats_data {
	void __iomem *base;
	const char *label;
};

struct rpm_master_stats {
	u32 active_cores;
	u32 num_shutdowns;
	u64 shutdown_req;
	u64 wakeup_idx;
	u64 bringup_req;
	u64 bringup_ack;
	u32 wakeup_reason; /* 0 = "rude wakeup", 1 = scheduled wakeup */
	u32 last_sleep_trans_dur;
	u32 last_wake_trans_dur;

	/* Per-subsystem (*not necessarily* SoC-wide) XO shutdown stats */
	u32 xo_count;
	u64 xo_last_enter;
	u64 last_exit;
	u64 xo_total_dur;
} __packed;

static int master_stats_show(struct seq_file *s, void *unused)
{
	struct master_stats_data *data = s->private;
	struct rpm_master_stats stat;

	memcpy_fromio(&stat, data->base, sizeof(stat));

	seq_printf(s, "%s:\n", data->label);

	seq_printf(s, "\tLast shutdown @ %llu\n", stat.shutdown_req);
	seq_printf(s, "\tLast bringup req @ %llu\n", stat.bringup_req);
	seq_printf(s, "\tLast bringup ack @ %llu\n", stat.bringup_ack);
	seq_printf(s, "\tLast wakeup idx: %llu\n", stat.wakeup_idx);
	seq_printf(s, "\tLast XO shutdown enter @ %llu\n", stat.xo_last_enter);
	seq_printf(s, "\tLast XO shutdown exit @ %llu\n", stat.last_exit);
	seq_printf(s, "\tXO total duration: %llu\n", stat.xo_total_dur);
	seq_printf(s, "\tLast sleep transition duration: %u\n", stat.last_sleep_trans_dur);
	seq_printf(s, "\tLast wake transition duration: %u\n", stat.last_wake_trans_dur);
	seq_printf(s, "\tXO shutdown count: %u\n", stat.xo_count);
	seq_printf(s, "\tWakeup reason: 0x%x\n", stat.wakeup_reason);
	seq_printf(s, "\tShutdown count: %u\n", stat.num_shutdowns);
	seq_printf(s, "\tActive cores bitmask: 0x%x\n", stat.active_cores);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(master_stats);

static int master_stats_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct master_stats_data *data;
	struct device_node *msgram_np;
	struct dentry *dent, *root;
	struct resource res;
	int count, i, ret;

	count = of_property_count_strings(dev->of_node, "qcom,master-names");
	if (count < 0)
		return count;

	data = devm_kzalloc(dev, count * sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	root = debugfs_create_dir("qcom_rpm_master_stats", NULL);
	platform_set_drvdata(pdev, root);

	for (i = 0; i < count; i++) {
		msgram_np = of_parse_phandle(dev->of_node, "qcom,rpm-msg-ram", i);
		if (!msgram_np) {
			debugfs_remove_recursive(root);
			return dev_err_probe(dev, -ENODEV,
					     "Couldn't parse MSG RAM phandle idx %d", i);
		}

		/*
		 * Purposefully skip devm_platform helpers as we're using a
		 * shared resource.
		 */
		ret = of_address_to_resource(msgram_np, 0, &res);
		of_node_put(msgram_np);
		if (ret < 0) {
			debugfs_remove_recursive(root);
			return ret;
		}

		data[i].base = devm_ioremap(dev, res.start, resource_size(&res));
		if (!data[i].base) {
			debugfs_remove_recursive(root);
			return dev_err_probe(dev, -EINVAL,
					     "Could not map the MSG RAM slice idx %d!\n", i);
		}

		ret = of_property_read_string_index(dev->of_node, "qcom,master-names", i,
						    &data[i].label);
		if (ret < 0) {
			debugfs_remove_recursive(root);
			return dev_err_probe(dev, ret,
					     "Could not read name idx %d!\n", i);
		}

		/*
		 * Generally it's not advised to fail on debugfs errors, but this
		 * driver's only job is exposing data therein.
		 */
		dent = debugfs_create_file(data[i].label, 0444, root,
					   &data[i], &master_stats_fops);
		if (IS_ERR(dent)) {
			debugfs_remove_recursive(root);
			return dev_err_probe(dev, PTR_ERR(dent),
					     "Failed to create debugfs file %s!\n", data[i].label);
		}
	}

	device_set_pm_not_required(dev);

	return 0;
}

static void master_stats_remove(struct platform_device *pdev)
{
	struct dentry *root = platform_get_drvdata(pdev);

	debugfs_remove_recursive(root);
}

static const struct of_device_id rpm_master_table[] = {
	{ .compatible = "qcom,rpm-master-stats" },
	{ },
};
/*
 * No MODULE_DEVICE_TABLE intentionally: that's a debugging module, to be
 * loaded manually only.
 */

static struct platform_driver master_stats_driver = {
	.probe = master_stats_probe,
	.remove = master_stats_remove,
	.driver = {
		.name = "qcom_rpm_master_stats",
		.of_match_table = rpm_master_table,
	},
};
module_platform_driver(master_stats_driver);

MODULE_DESCRIPTION("Qualcomm RPM Master Statistics driver");
MODULE_LICENSE("GPL");
