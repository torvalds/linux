// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *
 * UX500 common part of Power domain regulators
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "dbx500-prcmu.h"

/*
 * power state reference count
 */
static int power_state_active_cnt; /* will initialize to zero */
static DEFINE_SPINLOCK(power_state_active_lock);

void power_state_active_enable(void)
{
	unsigned long flags;

	spin_lock_irqsave(&power_state_active_lock, flags);
	power_state_active_cnt++;
	spin_unlock_irqrestore(&power_state_active_lock, flags);
}

int power_state_active_disable(void)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&power_state_active_lock, flags);
	if (power_state_active_cnt <= 0) {
		pr_err("power state: unbalanced enable/disable calls\n");
		ret = -EINVAL;
		goto out;
	}

	power_state_active_cnt--;
out:
	spin_unlock_irqrestore(&power_state_active_lock, flags);
	return ret;
}

#ifdef CONFIG_REGULATOR_DEBUG

static int power_state_active_get(void)
{
	unsigned long flags;
	int cnt;

	spin_lock_irqsave(&power_state_active_lock, flags);
	cnt = power_state_active_cnt;
	spin_unlock_irqrestore(&power_state_active_lock, flags);

	return cnt;
}

static struct ux500_regulator_debug {
	struct dentry *dir;
	struct dbx500_regulator_info *regulator_array;
	int num_regulators;
	u8 *state_before_suspend;
	u8 *state_after_suspend;
} rdebug;

static int ux500_regulator_power_state_cnt_show(struct seq_file *s, void *p)
{
	/* print power state count */
	seq_printf(s, "ux500-regulator power state count: %i\n",
		   power_state_active_get());

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ux500_regulator_power_state_cnt);

static int ux500_regulator_status_show(struct seq_file *s, void *p)
{
	int i;

	/* print dump header */
	seq_puts(s, "ux500-regulator status:\n");
	seq_printf(s, "%31s : %8s : %8s\n", "current", "before", "after");

	for (i = 0; i < rdebug.num_regulators; i++) {
		struct dbx500_regulator_info *info;
		/* Access per-regulator data */
		info = &rdebug.regulator_array[i];

		/* print status */
		seq_printf(s, "%20s : %8s : %8s : %8s\n",
			   info->desc.name,
			   info->is_enabled ? "enabled" : "disabled",
			   rdebug.state_before_suspend[i] ? "enabled" : "disabled",
			   rdebug.state_after_suspend[i] ? "enabled" : "disabled");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ux500_regulator_status);

int
ux500_regulator_debug_init(struct platform_device *pdev,
	struct dbx500_regulator_info *regulator_info,
	int num_regulators)
{
	/* create directory */
	rdebug.dir = debugfs_create_dir("ux500-regulator", NULL);

	/* create "status" file */
	debugfs_create_file("status", S_IRUGO, rdebug.dir, &pdev->dev,
			    &ux500_regulator_status_fops);

	/* create "power-state-count" file */
	debugfs_create_file("power-state-count", S_IRUGO, rdebug.dir,
			    &pdev->dev, &ux500_regulator_power_state_cnt_fops);

	rdebug.regulator_array = regulator_info;
	rdebug.num_regulators = num_regulators;

	rdebug.state_before_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rdebug.state_before_suspend)
		goto exit_destroy_power_state;

	rdebug.state_after_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rdebug.state_after_suspend)
		goto exit_free;

	return 0;

exit_free:
	kfree(rdebug.state_before_suspend);
exit_destroy_power_state:
	debugfs_remove_recursive(rdebug.dir);
	return -ENOMEM;
}

int ux500_regulator_debug_exit(void)
{
	debugfs_remove_recursive(rdebug.dir);
	kfree(rdebug.state_after_suspend);
	kfree(rdebug.state_before_suspend);

	return 0;
}
#endif
