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
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "dbx500-prcmu.h"

/*
 * power state reference count
 */
static int power_state_active_cnt; /* will initialize to zero */
static DEFINE_SPINLOCK(power_state_active_lock);

int power_state_active_get(void)
{
	unsigned long flags;
	int cnt;

	spin_lock_irqsave(&power_state_active_lock, flags);
	cnt = power_state_active_cnt;
	spin_unlock_irqrestore(&power_state_active_lock, flags);

	return cnt;
}

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

static struct ux500_regulator_debug {
	struct dentry *dir;
	struct dentry *status_file;
	struct dentry *power_state_cnt_file;
	struct dbx500_regulator_info *regulator_array;
	int num_regulators;
	u8 *state_before_suspend;
	u8 *state_after_suspend;
} rdebug;

void ux500_regulator_suspend_debug(void)
{
	int i;
	for (i = 0; i < rdebug.num_regulators; i++)
		rdebug.state_before_suspend[i] =
			rdebug.regulator_array[i].is_enabled;
}

void ux500_regulator_resume_debug(void)
{
	int i;
	for (i = 0; i < rdebug.num_regulators; i++)
		rdebug.state_after_suspend[i] =
			rdebug.regulator_array[i].is_enabled;
}

static int ux500_regulator_power_state_cnt_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int err;

	/* print power state count */
	err = seq_printf(s, "ux500-regulator power state count: %i\n",
		power_state_active_get());
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	return 0;
}

static int ux500_regulator_power_state_cnt_open(struct inode *inode,
	struct file *file)
{
	return single_open(file, ux500_regulator_power_state_cnt_print,
		inode->i_private);
}

static const struct file_operations ux500_regulator_power_state_cnt_fops = {
	.open = ux500_regulator_power_state_cnt_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int ux500_regulator_status_print(struct seq_file *s, void *p)
{
	struct device *dev = s->private;
	int err;
	int i;

	/* print dump header */
	err = seq_printf(s, "ux500-regulator status:\n");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	err = seq_printf(s, "%31s : %8s : %8s\n", "current",
		"before", "after");
	if (err < 0)
		dev_err(dev, "seq_printf overflow\n");

	for (i = 0; i < rdebug.num_regulators; i++) {
		struct dbx500_regulator_info *info;
		/* Access per-regulator data */
		info = &rdebug.regulator_array[i];

		/* print status */
		err = seq_printf(s, "%20s : %8s : %8s : %8s\n", info->desc.name,
			info->is_enabled ? "enabled" : "disabled",
			rdebug.state_before_suspend[i] ? "enabled" : "disabled",
			rdebug.state_after_suspend[i] ? "enabled" : "disabled");
		if (err < 0)
			dev_err(dev, "seq_printf overflow\n");
	}

	return 0;
}

static int ux500_regulator_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, ux500_regulator_status_print,
		inode->i_private);
}

static const struct file_operations ux500_regulator_status_fops = {
	.open = ux500_regulator_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

int __attribute__((weak)) dbx500_regulator_testcase(
	struct dbx500_regulator_info *regulator_info,
	int num_regulators)
{
	return 0;
}

int
ux500_regulator_debug_init(struct platform_device *pdev,
	struct dbx500_regulator_info *regulator_info,
	int num_regulators)
{
	/* create directory */
	rdebug.dir = debugfs_create_dir("ux500-regulator", NULL);
	if (!rdebug.dir)
		goto exit_no_debugfs;

	/* create "status" file */
	rdebug.status_file = debugfs_create_file("status",
		S_IRUGO, rdebug.dir, &pdev->dev,
		&ux500_regulator_status_fops);
	if (!rdebug.status_file)
		goto exit_destroy_dir;

	/* create "power-state-count" file */
	rdebug.power_state_cnt_file = debugfs_create_file("power-state-count",
		S_IRUGO, rdebug.dir, &pdev->dev,
		&ux500_regulator_power_state_cnt_fops);
	if (!rdebug.power_state_cnt_file)
		goto exit_destroy_status;

	rdebug.regulator_array = regulator_info;
	rdebug.num_regulators = num_regulators;

	rdebug.state_before_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rdebug.state_before_suspend) {
		dev_err(&pdev->dev,
			"could not allocate memory for saving state\n");
		goto exit_destroy_power_state;
	}

	rdebug.state_after_suspend = kzalloc(num_regulators, GFP_KERNEL);
	if (!rdebug.state_after_suspend) {
		dev_err(&pdev->dev,
			"could not allocate memory for saving state\n");
		goto exit_free;
	}

	dbx500_regulator_testcase(regulator_info, num_regulators);
	return 0;

exit_free:
	kfree(rdebug.state_before_suspend);
exit_destroy_power_state:
	debugfs_remove(rdebug.power_state_cnt_file);
exit_destroy_status:
	debugfs_remove(rdebug.status_file);
exit_destroy_dir:
	debugfs_remove(rdebug.dir);
exit_no_debugfs:
	dev_err(&pdev->dev, "failed to create debugfs entries.\n");
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
