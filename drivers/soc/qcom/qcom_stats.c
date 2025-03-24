// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#include <linux/soc/qcom/smem.h>
#include <clocksource/arm_arch_timer.h>

#define RPM_DYNAMIC_ADDR	0x14
#define RPM_DYNAMIC_ADDR_MASK	0xFFFF

#define STAT_TYPE_OFFSET	0x0
#define COUNT_OFFSET		0x4
#define LAST_ENTERED_AT_OFFSET	0x8
#define LAST_EXITED_AT_OFFSET	0x10
#define ACCUMULATED_OFFSET	0x18
#define CLIENT_VOTES_OFFSET	0x20

struct subsystem_data {
	const char *name;
	u32 smem_item;
	u32 pid;
};

static const struct subsystem_data subsystems[] = {
	{ "modem", 605, 1 },
	{ "wpss", 605, 13 },
	{ "adsp", 606, 2 },
	{ "cdsp", 607, 5 },
	{ "cdsp1", 607, 12 },
	{ "gpdsp0", 607, 17 },
	{ "gpdsp1", 607, 18 },
	{ "slpi", 608, 3 },
	{ "gpu", 609, 0 },
	{ "display", 610, 0 },
	{ "adsp_island", 613, 2 },
	{ "slpi_island", 613, 3 },
	{ "apss", 631, QCOM_SMEM_HOST_ANY },
};

struct stats_config {
	size_t stats_offset;
	size_t num_records;
	bool appended_stats_avail;
	bool dynamic_offset;
	bool subsystem_stats_in_smem;
};

struct stats_data {
	bool appended_stats_avail;
	void __iomem *base;
};

struct sleep_stats {
	u32 stat_type;
	u32 count;
	u64 last_entered_at;
	u64 last_exited_at;
	u64 accumulated;
};

struct appended_stats {
	u32 client_votes;
	u32 reserved[3];
};

static void qcom_print_stats(struct seq_file *s, const struct sleep_stats *stat)
{
	u64 accumulated = stat->accumulated;
	/*
	 * If a subsystem is in sleep when reading the sleep stats adjust
	 * the accumulated sleep duration to show actual sleep time.
	 */
	if (stat->last_entered_at > stat->last_exited_at)
		accumulated += arch_timer_read_counter() - stat->last_entered_at;

	seq_printf(s, "Count: %u\n", stat->count);
	seq_printf(s, "Last Entered At: %llu\n", stat->last_entered_at);
	seq_printf(s, "Last Exited At: %llu\n", stat->last_exited_at);
	seq_printf(s, "Accumulated Duration: %llu\n", accumulated);
}

static int qcom_subsystem_sleep_stats_show(struct seq_file *s, void *unused)
{
	struct subsystem_data *subsystem = s->private;
	struct sleep_stats *stat;

	/* Items are allocated lazily, so lookup pointer each time */
	stat = qcom_smem_get(subsystem->pid, subsystem->smem_item, NULL);
	if (IS_ERR(stat))
		return 0;

	qcom_print_stats(s, stat);

	return 0;
}

static int qcom_soc_sleep_stats_show(struct seq_file *s, void *unused)
{
	struct stats_data *d = s->private;
	void __iomem *reg = d->base;
	struct sleep_stats stat;

	memcpy_fromio(&stat, reg, sizeof(stat));
	qcom_print_stats(s, &stat);

	if (d->appended_stats_avail) {
		struct appended_stats votes;

		memcpy_fromio(&votes, reg + CLIENT_VOTES_OFFSET, sizeof(votes));
		seq_printf(s, "Client Votes: %#x\n", votes.client_votes);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qcom_soc_sleep_stats);
DEFINE_SHOW_ATTRIBUTE(qcom_subsystem_sleep_stats);

static void qcom_create_soc_sleep_stat_files(struct dentry *root, void __iomem *reg,
					     struct stats_data *d,
					     const struct stats_config *config)
{
	char stat_type[sizeof(u32) + 1] = {0};
	size_t stats_offset = config->stats_offset;
	u32 offset = 0, type;
	int i, j;

	/*
	 * On RPM targets, stats offset location is dynamic and changes from target
	 * to target and sometimes from build to build for same target.
	 *
	 * In such cases the dynamic address is present at 0x14 offset from base
	 * address in devicetree. The last 16bits indicates the stats_offset.
	 */
	if (config->dynamic_offset) {
		stats_offset = readl(reg + RPM_DYNAMIC_ADDR);
		stats_offset &= RPM_DYNAMIC_ADDR_MASK;
	}

	for (i = 0; i < config->num_records; i++) {
		d[i].base = reg + offset + stats_offset;

		/*
		 * Read the low power mode name and create debugfs file for it.
		 * The names read could be of below,
		 * (may change depending on low power mode supported).
		 * For rpmh-sleep-stats: "aosd", "cxsd" and "ddr".
		 * For rpm-sleep-stats: "vmin" and "vlow".
		 */
		type = readl(d[i].base);
		for (j = 0; j < sizeof(u32); j++) {
			stat_type[j] = type & 0xff;
			type = type >> 8;
		}
		strim(stat_type);
		debugfs_create_file(stat_type, 0400, root, &d[i],
				    &qcom_soc_sleep_stats_fops);

		offset += sizeof(struct sleep_stats);
		if (d[i].appended_stats_avail)
			offset += sizeof(struct appended_stats);
	}
}

static void qcom_create_subsystem_stat_files(struct dentry *root,
					     const struct stats_config *config)
{
	int i;

	if (!config->subsystem_stats_in_smem)
		return;

	for (i = 0; i < ARRAY_SIZE(subsystems); i++)
		debugfs_create_file(subsystems[i].name, 0400, root, (void *)&subsystems[i],
				    &qcom_subsystem_sleep_stats_fops);
}

static int qcom_stats_probe(struct platform_device *pdev)
{
	void __iomem *reg;
	struct dentry *root;
	const struct stats_config *config;
	struct stats_data *d;
	int i;

	config = device_get_match_data(&pdev->dev);
	if (!config)
		return -ENODEV;

	reg = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(reg))
		return -ENOMEM;

	d = devm_kcalloc(&pdev->dev, config->num_records,
			 sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	for (i = 0; i < config->num_records; i++)
		d[i].appended_stats_avail = config->appended_stats_avail;

	root = debugfs_create_dir("qcom_stats", NULL);

	qcom_create_subsystem_stat_files(root, config);
	qcom_create_soc_sleep_stat_files(root, reg, d, config);

	platform_set_drvdata(pdev, root);

	device_set_pm_not_required(&pdev->dev);

	return 0;
}

static void qcom_stats_remove(struct platform_device *pdev)
{
	struct dentry *root = platform_get_drvdata(pdev);

	debugfs_remove_recursive(root);
}

static const struct stats_config rpm_data = {
	.stats_offset = 0,
	.num_records = 2,
	.appended_stats_avail = true,
	.dynamic_offset = true,
	.subsystem_stats_in_smem = false,
};

/* Older RPM firmwares have the stats at a fixed offset instead */
static const struct stats_config rpm_data_dba0 = {
	.stats_offset = 0xdba0,
	.num_records = 2,
	.appended_stats_avail = true,
	.dynamic_offset = false,
	.subsystem_stats_in_smem = false,
};

static const struct stats_config rpmh_data_sdm845 = {
	.stats_offset = 0x48,
	.num_records = 2,
	.appended_stats_avail = false,
	.dynamic_offset = false,
	.subsystem_stats_in_smem = true,
};

static const struct stats_config rpmh_data = {
	.stats_offset = 0x48,
	.num_records = 3,
	.appended_stats_avail = false,
	.dynamic_offset = false,
	.subsystem_stats_in_smem = true,
};

static const struct of_device_id qcom_stats_table[] = {
	{ .compatible = "qcom,apq8084-rpm-stats", .data = &rpm_data_dba0 },
	{ .compatible = "qcom,msm8226-rpm-stats", .data = &rpm_data_dba0 },
	{ .compatible = "qcom,msm8916-rpm-stats", .data = &rpm_data_dba0 },
	{ .compatible = "qcom,msm8974-rpm-stats", .data = &rpm_data_dba0 },
	{ .compatible = "qcom,rpm-stats", .data = &rpm_data },
	{ .compatible = "qcom,rpmh-stats", .data = &rpmh_data },
	{ .compatible = "qcom,sdm845-rpmh-stats", .data = &rpmh_data_sdm845 },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_stats_table);

static struct platform_driver qcom_stats = {
	.probe = qcom_stats_probe,
	.remove = qcom_stats_remove,
	.driver = {
		.name = "qcom_stats",
		.of_match_table = qcom_stats_table,
	},
};

static int __init qcom_stats_init(void)
{
	return platform_driver_register(&qcom_stats);
}
late_initcall(qcom_stats_init);

static void __exit qcom_stats_exit(void)
{
	platform_driver_unregister(&qcom_stats);
}
module_exit(qcom_stats_exit)

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) Stats driver");
MODULE_LICENSE("GPL v2");
