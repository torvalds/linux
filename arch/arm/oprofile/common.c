/**
 * @file common.c
 *
 * @remark Copyright 2004 Oprofile Authors
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo
 */

#include <linux/init.h>
#include <linux/oprofile.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/sysdev.h>
#include <linux/mutex.h>

#include "op_counter.h"
#include "op_arm_model.h"

static struct op_arm_model_spec *op_arm_model;
static int op_arm_enabled;
static DEFINE_MUTEX(op_arm_mutex);

struct op_counter_config *counter_config;

static int op_arm_create_files(struct super_block *sb, struct dentry *root)
{
	unsigned int i;

	for (i = 0; i < op_arm_model->num_counters; i++) {
		struct dentry *dir;
		char buf[4];

		snprintf(buf, sizeof buf, "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);
		oprofilefs_create_ulong(sb, dir, "enabled", &counter_config[i].enabled);
		oprofilefs_create_ulong(sb, dir, "event", &counter_config[i].event);
		oprofilefs_create_ulong(sb, dir, "count", &counter_config[i].count);
		oprofilefs_create_ulong(sb, dir, "unit_mask", &counter_config[i].unit_mask);
		oprofilefs_create_ulong(sb, dir, "kernel", &counter_config[i].kernel);
		oprofilefs_create_ulong(sb, dir, "user", &counter_config[i].user);
	}

	return 0;
}

static int op_arm_setup(void)
{
	int ret;

	spin_lock(&oprofilefs_lock);
	ret = op_arm_model->setup_ctrs();
	spin_unlock(&oprofilefs_lock);
	return ret;
}

static int op_arm_start(void)
{
	int ret = -EBUSY;

	mutex_lock(&op_arm_mutex);
	if (!op_arm_enabled) {
		ret = op_arm_model->start();
		op_arm_enabled = !ret;
	}
	mutex_unlock(&op_arm_mutex);
	return ret;
}

static void op_arm_stop(void)
{
	mutex_lock(&op_arm_mutex);
	if (op_arm_enabled)
		op_arm_model->stop();
	op_arm_enabled = 0;
	mutex_unlock(&op_arm_mutex);
}

#ifdef CONFIG_PM
static int op_arm_suspend(struct sys_device *dev, pm_message_t state)
{
	mutex_lock(&op_arm_mutex);
	if (op_arm_enabled)
		op_arm_model->stop();
	mutex_unlock(&op_arm_mutex);
	return 0;
}

static int op_arm_resume(struct sys_device *dev)
{
	mutex_lock(&op_arm_mutex);
	if (op_arm_enabled && op_arm_model->start())
		op_arm_enabled = 0;
	mutex_unlock(&op_arm_mutex);
	return 0;
}

static struct sysdev_class oprofile_sysclass = {
	set_kset_name("oprofile"),
	.resume		= op_arm_resume,
	.suspend	= op_arm_suspend,
};

static struct sys_device device_oprofile = {
	.id		= 0,
	.cls		= &oprofile_sysclass,
};

static int __init init_driverfs(void)
{
	int ret;

	if (!(ret = sysdev_class_register(&oprofile_sysclass)))
		ret = sysdev_register(&device_oprofile);

	return ret;
}

static void  exit_driverfs(void)
{
	sysdev_unregister(&device_oprofile);
	sysdev_class_unregister(&oprofile_sysclass);
}
#else
#define init_driverfs()	do { } while (0)
#define exit_driverfs() do { } while (0)
#endif /* CONFIG_PM */

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	struct op_arm_model_spec *spec = NULL;
	int ret = -ENODEV;

#ifdef CONFIG_CPU_XSCALE
	spec = &op_xscale_spec;
#endif

	if (spec) {
		ret = spec->init();
		if (ret < 0)
			return ret;

		counter_config = kcalloc(spec->num_counters, sizeof(struct op_counter_config),
					 GFP_KERNEL);
		if (!counter_config)
			return -ENOMEM;

		op_arm_model = spec;
		init_driverfs();
		ops->create_files = op_arm_create_files;
		ops->setup = op_arm_setup;
		ops->shutdown = op_arm_stop;
		ops->start = op_arm_start;
		ops->stop = op_arm_stop;
		ops->cpu_type = op_arm_model->name;
		ops->backtrace = arm_backtrace;
		printk(KERN_INFO "oprofile: using %s\n", spec->name);
	}

	return ret;
}

void oprofile_arch_exit(void)
{
	if (op_arm_model) {
		exit_driverfs();
		op_arm_model = NULL;
	}
	kfree(counter_config);
}
