/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *
 * UX500 common part of Power domain regulators
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/defs.h>
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

#ifdef CONFIG_REGULATOR_DE

static int power_state_active_get(void)
{
	unsigned long flags;
	int cnt;

	spin_lock_irqsave(&power_state_active_lock, flags);
	cnt = power_state_active_cnt;
	spin_unlock_irqrestore(&power_state_active_lock, flags);

	return cnt;
}

static struct ux500_regulator_de {
	struct dentry *dir;
	struct dentry *status_file;
	struct dentry *power_state_cnt_file;
	struct dbx500_regulator_info *regulator_array;
	int num_regulators;
	u8 *state_before_suspend;
	u8 *state_after_suspend;
} rde;

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

	for (i = 0; i < rde.num_regulators; i++) {
		struct dbx500_regulator_info *info;
		/* Access per-regulator data */
		info = &rde.regulator_array[i];

		/* print status */
		seq_printf(s, "%20s : %8s : %8s : %8s\n",
			   info->desc.name,
			   info->is_enabled ? "enabled" : "disabled",
			   rde.state_before_suspend[i] ? "enabled" : "disabled",
			   rde.state_after_suspend[i] ? "enabled" : "disabled");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ux500_regulator_status);

int __attribute__((weak)) dbx500_regulator_testcase(
	struct dbx500_regulator_info *regulator_info,
	int num_regulators)
{
	return 0;
}

int
ux500_regulator_de_init(struct platform_device *pdev,
	struct dbx500_regulator_info *regulator_info,
	int num_regulators)
{
	/* create directory */
	rde.dir = defs_create_dir("ux500-regulator", NULL);
	if (!rde.dir)
		goto exit_no_defs;

	/* create "status" file */
	rde.status_file = defs_create_file("status",
		S_IRUGO, rde.dir, &pdev->dev,
		&ux500_regulator_status_fops);
	if (!rde.status_file)
		goto exit_destroy_dir;

	/* create "power-state-count" file */
	rde.power_state_cnt_file = defs_create_file("power-state-count",
		S_IRUGO, rde.dir, &pdev->dev,
		&ux500_regulator_power_state_cnt_fops);
	if (!rde.power_state_cnt_file)
		goto exit_destroy_status;

	rde.regulator_array = regulator_info;
	rde.num_regulators = num_regulators;

	rde.state_before_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rde.state_before_suspend)
		goto exit_destroy_power_state;

	rde.state_after_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rde.state_after_suspend)
		goto exit_free;

	dbx500_regulator_testcase(regulator_info, num_regulators);
	return 0;

exit_free:
	kfree(rde.state_before_suspend);
exit_destroy_power_state:
	defs_remove(rde.power_state_cnt_file);
exit_destroy_status:
	defs_remove(rde.status_file);
exit_destroy_dir:
	defs_remove(rde.dir);
exit_no_defs:
	dev_err(&pdev->dev, "failed to create defs entries.\n");
	return -ENOMEM;
}

int ux500_regulator_de_exit(void)
{
	defs_remove_recursive(rde.dir);
	kfree(rde.state_after_suspend);
	kfree(rde.state_before_suspend);

	return 0;
}
#endif
