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
#include <asm/semaphore.h>
#include <linux/sysdev.h>

#include "op_counter.h"
#include "op_arm_model.h"

static struct op_arm_model_spec *pmu_model;
static int pmu_enabled;
static struct semaphore pmu_sem;

static int pmu_start(void);
static int pmu_setup(void);
static void pmu_stop(void);
static int pmu_create_files(struct super_block *, struct dentry *);

#ifdef CONFIG_PM
static int pmu_suspend(struct sys_device *dev, pm_message_t state)
{
	if (pmu_enabled)
		pmu_stop();
	return 0;
}

static int pmu_resume(struct sys_device *dev)
{
	if (pmu_enabled)
		pmu_start();
	return 0;
}

static struct sysdev_class oprofile_sysclass = {
	set_kset_name("oprofile"),
	.resume		= pmu_resume,
	.suspend	= pmu_suspend,
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

struct op_counter_config counter_config[OP_MAX_COUNTER];

static int pmu_create_files(struct super_block *sb, struct dentry *root)
{
	unsigned int i;

	for (i = 0; i < pmu_model->num_counters; i++) {
		struct dentry *dir;
		char buf[2];

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

static int pmu_setup(void)
{
	int ret;

	spin_lock(&oprofilefs_lock);
	ret = pmu_model->setup_ctrs();
	spin_unlock(&oprofilefs_lock);
	return ret;
}

static int pmu_start(void)
{
	int ret = -EBUSY;

	down(&pmu_sem);
	if (!pmu_enabled) {
		ret = pmu_model->start();
		pmu_enabled = !ret;
	}
	up(&pmu_sem);
	return ret;
}

static void pmu_stop(void)
{
	down(&pmu_sem);
	if (pmu_enabled)
		pmu_model->stop();
	pmu_enabled = 0;
	up(&pmu_sem);
}

int __init pmu_init(struct oprofile_operations *ops, struct op_arm_model_spec *spec)
{
	init_MUTEX(&pmu_sem);

	if (spec->init() < 0)
		return -ENODEV;

	pmu_model = spec;
	init_driverfs();
	ops->create_files = pmu_create_files;
	ops->setup = pmu_setup;
	ops->shutdown = pmu_stop;
	ops->start = pmu_start;
	ops->stop = pmu_stop;
	ops->cpu_type = pmu_model->name;
	printk(KERN_INFO "oprofile: using %s PMU\n", spec->name);

	return 0;
}

void pmu_exit(void)
{
	if (pmu_model) {
		exit_driverfs();
		pmu_model = NULL;
	}
}

