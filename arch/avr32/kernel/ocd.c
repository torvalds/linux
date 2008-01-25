/*
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <asm/ocd.h>

static long ocd_count;
static spinlock_t ocd_lock;

/**
 * ocd_enable - enable on-chip debugging
 * @child: task to be debugged
 *
 * If @child is non-NULL, ocd_enable() first checks if debugging has
 * already been enabled for @child, and if it has, does nothing.
 *
 * If @child is NULL (e.g. when debugging the kernel), or debugging
 * has not already been enabled for it, ocd_enable() increments the
 * reference count and enables the debugging hardware.
 */
void ocd_enable(struct task_struct *child)
{
	u32 dc;

	if (child)
		pr_debug("ocd_enable: child=%s [%u]\n",
				child->comm, child->pid);
	else
		pr_debug("ocd_enable (no child)\n");

	if (!child || !test_and_set_tsk_thread_flag(child, TIF_DEBUG)) {
		spin_lock(&ocd_lock);
		ocd_count++;
		dc = ocd_read(DC);
		dc |= (1 << OCD_DC_MM_BIT) | (1 << OCD_DC_DBE_BIT);
		ocd_write(DC, dc);
		spin_unlock(&ocd_lock);
	}
}

/**
 * ocd_disable - disable on-chip debugging
 * @child: task that was being debugged, but isn't anymore
 *
 * If @child is non-NULL, ocd_disable() checks if debugging is enabled
 * for @child, and if it isn't, does nothing.
 *
 * If @child is NULL (e.g. when debugging the kernel), or debugging is
 * enabled, ocd_disable() decrements the reference count, and if it
 * reaches zero, disables the debugging hardware.
 */
void ocd_disable(struct task_struct *child)
{
	u32 dc;

	if (!child)
		pr_debug("ocd_disable (no child)\n");
	else if (test_tsk_thread_flag(child, TIF_DEBUG))
		pr_debug("ocd_disable: child=%s [%u]\n",
				child->comm, child->pid);

	if (!child || test_and_clear_tsk_thread_flag(child, TIF_DEBUG)) {
		spin_lock(&ocd_lock);
		ocd_count--;

		WARN_ON(ocd_count < 0);

		if (ocd_count <= 0) {
			dc = ocd_read(DC);
			dc &= ~((1 << OCD_DC_MM_BIT) | (1 << OCD_DC_DBE_BIT));
			ocd_write(DC, dc);
		}
		spin_unlock(&ocd_lock);
	}
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/module.h>

static struct dentry *ocd_debugfs_root;
static struct dentry *ocd_debugfs_DC;
static struct dentry *ocd_debugfs_DS;
static struct dentry *ocd_debugfs_count;

static u64 ocd_DC_get(void *data)
{
	return ocd_read(DC);
}
static void ocd_DC_set(void *data, u64 val)
{
	ocd_write(DC, val);
}
DEFINE_SIMPLE_ATTRIBUTE(fops_DC, ocd_DC_get, ocd_DC_set, "0x%08llx\n");

static u64 ocd_DS_get(void *data)
{
	return ocd_read(DS);
}
DEFINE_SIMPLE_ATTRIBUTE(fops_DS, ocd_DS_get, NULL, "0x%08llx\n");

static u64 ocd_count_get(void *data)
{
	return ocd_count;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_count, ocd_count_get, NULL, "%lld\n");

static void ocd_debugfs_init(void)
{
	struct dentry *root;

	root = debugfs_create_dir("ocd", NULL);
	if (IS_ERR(root) || !root)
		goto err_root;
	ocd_debugfs_root = root;

	ocd_debugfs_DC = debugfs_create_file("DC", S_IRUSR | S_IWUSR,
				root, NULL, &fops_DC);
	if (!ocd_debugfs_DC)
		goto err_DC;

	ocd_debugfs_DS = debugfs_create_file("DS", S_IRUSR, root,
				NULL, &fops_DS);
	if (!ocd_debugfs_DS)
		goto err_DS;

	ocd_debugfs_count = debugfs_create_file("count", S_IRUSR, root,
				NULL, &fops_count);
	if (!ocd_debugfs_count)
		goto err_count;

	return;

err_count:
	debugfs_remove(ocd_debugfs_DS);
err_DS:
	debugfs_remove(ocd_debugfs_DC);
err_DC:
	debugfs_remove(ocd_debugfs_root);
err_root:
	printk(KERN_WARNING "OCD: Failed to create debugfs entries\n");
}
#else
static inline void ocd_debugfs_init(void)
{

}
#endif

static int __init ocd_init(void)
{
	spin_lock_init(&ocd_lock);
	ocd_debugfs_init();
	return 0;
}
arch_initcall(ocd_init);
