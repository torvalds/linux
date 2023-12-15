// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2012-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/suspend.h>
#include <linux/time64.h>

#include <soc/qcom/lpm-stats.h>

#define MAX_STR_LEN 256
#define MAX_TIME_LEN 20
static char *lpm_stats_reset = "reset";
static char *lpm_stats_suspend = "suspend";

struct lpm_sleep_time {
	struct kobj_attribute ts_attr;
	unsigned int cpu;
};

struct level_stats {
	const char *name;
	struct lpm_stats *owner;
	int64_t first_bucket_time;
	int bucket[CONFIG_MSM_IDLE_STATS_BUCKET_COUNT];
	int64_t min_time[CONFIG_MSM_IDLE_STATS_BUCKET_COUNT];
	int64_t max_time[CONFIG_MSM_IDLE_STATS_BUCKET_COUNT];
	int success_count;
	int failed_count;
	uint64_t total_time;
	uint64_t enter_time;
};

static struct level_stats suspend_time_stats;

static DEFINE_PER_CPU_SHARED_ALIGNED(struct lpm_stats, cpu_stats);

static bool str_is_reset(const char __user *in, size_t count)
{
	loff_t ppos = 0;
	char buffer[64] = { 0 };
	int ret = simple_write_to_buffer(buffer, sizeof(buffer) - 1,
					 &ppos, in, count - 1);

	if (ret > 0)
		return strcmp(buffer, lpm_stats_reset) ? false : true;

	return false;
}

static uint64_t get_total_sleep_time(unsigned int cpu_id)
{
	struct lpm_stats *stats = &per_cpu(cpu_stats, cpu_id);
	int i;
	uint64_t ret = 0;

	for (i = 0; i < stats->num_levels; i++)
		ret += stats->time_stats[i].total_time;

	return ret;
}

static void update_level_stats(struct level_stats *stats, uint64_t t,
				bool success)
{
	uint64_t bt;
	int i;

	if (!success) {
		stats->failed_count++;
		return;
	}

	stats->success_count++;
	stats->total_time += t;
	bt = t;
	do_div(bt, stats->first_bucket_time);

	if (bt < 1ULL << (CONFIG_MSM_IDLE_STATS_BUCKET_SHIFT *
			(CONFIG_MSM_IDLE_STATS_BUCKET_COUNT - 1)))
		i = DIV_ROUND_UP(fls((uint32_t)bt),
			CONFIG_MSM_IDLE_STATS_BUCKET_SHIFT);
	else
		i = CONFIG_MSM_IDLE_STATS_BUCKET_COUNT - 1;

	if (i >= CONFIG_MSM_IDLE_STATS_BUCKET_COUNT)
		i = CONFIG_MSM_IDLE_STATS_BUCKET_COUNT - 1;

	stats->bucket[i]++;

	if (t < stats->min_time[i] || !stats->max_time[i])
		stats->min_time[i] = t;
	if (t > stats->max_time[i])
		stats->max_time[i] = t;
}

static void level_stats_print(struct seq_file *m, struct level_stats *stats)
{
	int i = 0;
	int64_t bucket_time = 0;
	uint64_t s = stats->total_time;
	uint32_t ns = do_div(s, NSEC_PER_SEC);

	seq_printf(m, "[%s] %s:\n success count: %7d\n"
		"total success time: %lld.%09u\n",
		stats->owner->name, stats->name, stats->success_count, s, ns);

	if (stats->failed_count)
		seq_printf(m, "  failed count: %7d\n", stats->failed_count);

	bucket_time = stats->first_bucket_time;
	for (i = 0;
		i < CONFIG_MSM_IDLE_STATS_BUCKET_COUNT - 1;
		i++) {
		s = bucket_time;
		ns = do_div(s, NSEC_PER_SEC);
		seq_printf(m, "\t<%6lld.%09u: %7d (%lld-%lld)\n",
			s, ns, stats->bucket[i],
			stats->min_time[i],
			stats->max_time[i]);
		bucket_time <<= CONFIG_MSM_IDLE_STATS_BUCKET_SHIFT;
	}
	seq_printf(m, "\t>=%5lld.%09u:%8d (%lld-%lld)\n",
		s, ns, stats->bucket[i],
		stats->min_time[i],
		stats->max_time[i]);
}

static int level_stats_file_show(struct seq_file *m, void *v)
{
	struct level_stats *stats = (struct level_stats *) m->private;

	level_stats_print(m, stats);

	return 0;
}

static int level_stats_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, level_stats_file_show, inode->i_private);
}

static void level_stats_print_all(struct seq_file *m, struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;
	int i = 0;

	for (i = 0; i < stats->num_levels; i++)
		level_stats_print(m, &stats->time_stats[i]);

	if (list_empty(&stats->child))
		return;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		level_stats_print_all(m, pos);
	}
}

static void level_stats_reset(struct level_stats *stats)
{
	memset(stats->bucket, 0, sizeof(stats->bucket));
	memset(stats->min_time, 0, sizeof(stats->min_time));
	memset(stats->max_time, 0, sizeof(stats->max_time));
	stats->success_count = 0;
	stats->failed_count = 0;
	stats->total_time = 0;
}

static void level_stats_reset_all(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;
	int i = 0;

	for (i = 0; i < stats->num_levels; i++)
		level_stats_reset(&stats->time_stats[i]);

	if (list_empty(&stats->child))
		return;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		level_stats_reset_all(pos);
	}
}

static int lpm_stats_file_show(struct seq_file *m, void *v)
{
	struct lpm_stats *stats = (struct lpm_stats *)m->private;

	level_stats_print_all(m, stats);
	level_stats_print(m, &suspend_time_stats);

	return 0;
}

static int lpm_stats_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, lpm_stats_file_show, inode->i_private);
}

static ssize_t level_stats_file_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	struct inode *in = file->f_inode;
	struct level_stats *stats = (struct level_stats *)in->i_private;

	if (!str_is_reset(buffer, count))
		return -EINVAL;

	level_stats_reset(stats);
	return count;
}

static void reset_cpu_stats(void *info)
{
	struct lpm_stats *stats = &(*this_cpu_ptr(&(cpu_stats)));
	int i;

	for (i = 0; i < stats->num_levels; i++)
		level_stats_reset(&stats->time_stats[i]);
}

static ssize_t lpm_stats_file_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	struct inode *in = file->f_inode;
	struct lpm_stats *stats = (struct lpm_stats *)in->i_private;

	if (!str_is_reset(buffer, count))
		return -EINVAL;

	level_stats_reset_all(stats);
	/*
	 * Wake up each CPU and reset the stats from that CPU,
	 * for that CPU, so we could have better timestamp for
	 * accounting.
	 */
	on_each_cpu(reset_cpu_stats, NULL, 1);

	return count;
}

static int lifo_stats_file_show(struct seq_file *m, void *v)
{
	struct lpm_stats *stats = (struct lpm_stats *)m->private;
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;

	if (list_empty(&stats->child)) {
		pr_err("%s: ERROR: Lifo level with no children\n",
			__func__);
		return -EINVAL;
	}

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		seq_printf(m, "%s:\n\tLast-In:%u\n\tFirst-Out:%u\n",
			pos->name,
			pos->lifo.last_in,
			pos->lifo.first_out);
	}
	return 0;
}

static int lifo_stats_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, lifo_stats_file_show, inode->i_private);
}

static void lifo_stats_reset_all(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		pos->lifo.last_in = 0;
		pos->lifo.first_out = 0;
		if (!list_empty(&pos->child))
			lifo_stats_reset_all(pos);
	}
}

static ssize_t lifo_stats_file_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	struct inode *in = file->f_inode;
	struct lpm_stats *stats = (struct lpm_stats *)in->i_private;

	if (!str_is_reset(buffer, count))
		return -EINVAL;

	lifo_stats_reset_all(stats);

	return count;
}

static const struct file_operations level_stats_fops = {
	.owner	  = THIS_MODULE,
	.open	  = level_stats_file_open,
	.read	  = seq_read,
	.release  = single_release,
	.llseek   = no_llseek,
	.write	  = level_stats_file_write,
};

static const struct file_operations lpm_stats_fops = {
	.owner	  = THIS_MODULE,
	.open	  = lpm_stats_file_open,
	.read	  = seq_read,
	.release  = single_release,
	.llseek   = no_llseek,
	.write	  = lpm_stats_file_write,
};

static const struct file_operations lifo_stats_fops = {
	.owner	  = THIS_MODULE,
	.open	  = lifo_stats_file_open,
	.read	  = seq_read,
	.release  = single_release,
	.llseek   = no_llseek,
	.write	  = lifo_stats_file_write,
};

static void update_last_in_stats(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;

	if (list_empty(&stats->child))
		return;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		if (cpumask_test_cpu(smp_processor_id(), &pos->mask)) {
			pos->lifo.last_in++;
			return;
		}
	}
}

static void update_first_out_stats(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;

	if (list_empty(&stats->child))
		return;

	centry = &stats->child;
	list_for_each_entry(pos, centry, sibling) {
		if (cpumask_test_cpu(smp_processor_id(), &pos->mask)) {
			pos->lifo.first_out++;
			return;
		}
	}
}

static inline void update_exit_stats(struct lpm_stats *stats, uint32_t index,
					bool success)
{
	uint64_t exit_time = 0;

	/* Update time stats only when exit is preceded by enter */
	if (stats->sleep_time < 0)
		success = false;
	else
		exit_time = stats->sleep_time;
	update_level_stats(&stats->time_stats[index], exit_time,
					success);
}

static int config_level(const char *name, const char **levels,
	int num_levels, struct lpm_stats *parent, struct lpm_stats *stats)
{
	int i = 0;
	struct dentry *directory = NULL;
	const char *rootname = "lpm_stats";
	const char *dirname = rootname;

	strscpy(stats->name, name, MAX_STR_LEN);
	stats->num_levels = num_levels;
	stats->parent = parent;
	INIT_LIST_HEAD(&stats->sibling);
	INIT_LIST_HEAD(&stats->child);

	stats->time_stats = kcalloc(num_levels, sizeof(*stats->time_stats),
					GFP_KERNEL);
	if (!stats->time_stats)
		return -ENOMEM;

	if (parent) {
		list_add_tail(&stats->sibling, &parent->child);
		directory = parent->directory;
		dirname = name;
	}

	stats->directory = debugfs_create_dir(dirname, directory);
	if (!stats->directory) {
		pr_err("%s: Unable to create %s debugfs directory\n",
			__func__, dirname);
		kfree(stats->time_stats);
		return -EPERM;
	}

	for (i = 0; i < num_levels; i++) {
		stats->time_stats[i].name = levels[i];
		stats->time_stats[i].owner = stats;
		stats->time_stats[i].first_bucket_time =
			CONFIG_MSM_IDLE_STATS_FIRST_BUCKET;
		stats->time_stats[i].enter_time = 0;

		if (!debugfs_create_file(stats->time_stats[i].name, 0444,
			stats->directory, (void *)&stats->time_stats[i],
			&level_stats_fops)) {
			pr_err("%s: Unable to create %s %s level-stats file\n",
				__func__, stats->name,
				stats->time_stats[i].name);
			kfree(stats->time_stats);
			return -EPERM;
		}
	}

	if (!debugfs_create_file("stats", 0444, stats->directory,
		(void *)stats, &lpm_stats_fops)) {
		pr_err("%s: Unable to create %s's overall 'stats' file\n",
			__func__, stats->name);
		kfree(stats->time_stats);
		return -EPERM;
	}

	return 0;
}

static ssize_t total_sleep_time_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct lpm_sleep_time *cpu_sleep_time = container_of(attr,
			struct lpm_sleep_time, ts_attr);
	unsigned int cpu = cpu_sleep_time->cpu;
	uint64_t total_time = get_total_sleep_time(cpu);

	return scnprintf(buf, MAX_TIME_LEN, "%llu.%09u\n", total_time,
			do_div(total_time, NSEC_PER_SEC));
}

static struct kobject *local_module_kobject(void)
{
	struct kobject *kobj;

	kobj = kset_find_obj(module_kset, KBUILD_MODNAME);

	if (!kobj) {
		int err;
		struct module_kobject *mk;

		mk = kcalloc(1, sizeof(*mk), GFP_KERNEL);
		if (!mk)
			return ERR_PTR(-ENOMEM);

		mk->mod = THIS_MODULE;
		mk->kobj.kset = module_kset;

		err = kobject_init_and_add(&mk->kobj, &module_ktype, NULL,
				"%s", KBUILD_MODNAME);

		if (err) {
			kobject_put(&mk->kobj);
			kfree(mk);
			pr_err("%s: cannot create kobject for %s\n",
					__func__, KBUILD_MODNAME);
			return ERR_PTR(err);
		}

		kobject_get(&mk->kobj);
		kobj = &mk->kobj;
	}

	return kobj;
}

static int create_sysfs_node(unsigned int cpu, struct lpm_stats *stats)
{
	struct kobject *cpu_kobj = NULL;
	struct lpm_sleep_time *ts = NULL;
	struct kobject *stats_kobj;
	char cpu_name[10] = { 0 };
	int ret = -ENOMEM;

	stats_kobj = local_module_kobject();

	if (IS_ERR_OR_NULL(stats_kobj))
		return PTR_ERR(stats_kobj);

	snprintf(cpu_name, sizeof(cpu_name), "cpu%u", cpu);
	cpu_kobj = kobject_create_and_add(cpu_name, stats_kobj);
	if (!cpu_kobj)
		return -ENOMEM;

	ts = kcalloc(1, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		goto failed;

	sysfs_attr_init(&ts->ts_attr.attr);
	ts->ts_attr.attr.name = "total_sleep_time_secs";
	ts->ts_attr.attr.mode = 0444;
	ts->ts_attr.show = total_sleep_time_show;
	ts->ts_attr.store = NULL;
	ts->cpu = cpu;

	ret = sysfs_create_file(cpu_kobj, &ts->ts_attr.attr);
	if (ret)
		goto failed;

	return 0;

failed:
	kfree(ts);
	kobject_put(cpu_kobj);
	return ret;
}

static struct lpm_stats *config_cpu_level(const char *name,
	const char **levels, int num_levels, struct lpm_stats *parent,
	struct cpumask *mask)
{
	int cpu = 0;
	struct lpm_stats *pstats = NULL;
	struct lpm_stats *stats = NULL;

	for (pstats = parent; pstats; pstats = pstats->parent)
		cpumask_or(&pstats->mask, &pstats->mask, mask);

	for_each_cpu(cpu, mask) {
		int ret = 0;
		char cpu_name[16] = { 0 };

		stats = &per_cpu(cpu_stats, cpu);
		snprintf(cpu_name, sizeof(cpu_name), "%s%d", name, cpu);
		cpumask_set_cpu(cpu, &stats->mask);

		stats->is_cpu = true;

		ret = config_level(cpu_name, levels, num_levels, parent,
					stats);
		if (ret) {
			pr_err("%s: Unable to create %s stats\n",
				__func__, cpu_name);
			return ERR_PTR(ret);
		}

		ret = create_sysfs_node(cpu, stats);

		if (ret) {
			pr_err("Could not create the sysfs node\n");
			return ERR_PTR(ret);
		}
	}

	return stats;
}

static void config_suspend_level(struct lpm_stats *stats)
{
	suspend_time_stats.name = lpm_stats_suspend;
	suspend_time_stats.owner = stats;
	suspend_time_stats.first_bucket_time =
			CONFIG_MSM_SUSPEND_STATS_FIRST_BUCKET;
	suspend_time_stats.enter_time = 0;
	suspend_time_stats.success_count = 0;
	suspend_time_stats.failed_count = 0;

	if (!debugfs_create_file(suspend_time_stats.name, 0444,
		stats->directory, (void *)&suspend_time_stats,
		&level_stats_fops))
		pr_err("%s: Unable to create %s Suspend stats file\n",
			__func__, stats->name);
}

static struct lpm_stats *config_cluster_level(const char *name,
	const char **levels, int num_levels, struct lpm_stats *parent)
{
	struct lpm_stats *stats = NULL;
	int ret = 0;

	stats = kcalloc(1, sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return ERR_PTR(-ENOMEM);

	stats->is_cpu = false;

	ret = config_level(name, levels, num_levels, parent, stats);
	if (ret) {
		pr_err("%s: Unable to create %s stats\n", __func__,
			name);
		kfree(stats);
		return ERR_PTR(ret);
	}

	if (!debugfs_create_file("lifo", 0444, stats->directory,
		(void *)stats, &lifo_stats_fops)) {
		pr_err("%s: Unable to create %s lifo stats file\n",
			__func__, stats->name);
		kfree(stats);
		return ERR_PTR(-EPERM);
	}

	if (!parent)
		config_suspend_level(stats);

	return stats;
}

static void cleanup_stats(struct lpm_stats *stats)
{
	struct list_head *centry = NULL;
	struct lpm_stats *pos = NULL;
	struct lpm_stats *n = NULL;

	centry = &stats->child;
	list_for_each_entry_safe_reverse(pos, n, centry, sibling) {
		if (!list_empty(&pos->child)) {
			cleanup_stats(pos);
			continue;
		}

		list_del_init(&pos->child);

		kfree(pos->time_stats);
		if (!pos->is_cpu)
			kfree(pos);
	}
	kfree(stats->time_stats);
	kfree(stats);
}

static void lpm_stats_cleanup(struct lpm_stats *stats)
{
	struct lpm_stats *pstats = stats;

	if (!pstats)
		return;

	while (pstats->parent)
		pstats = pstats->parent;

	debugfs_remove_recursive(pstats->directory);

	cleanup_stats(pstats);
}

/**
 * lpm_stats_config_level() - API to configure levels stats.
 *
 * @name:	Name of the cluster/cpu.
 * @levels:	Low power mode level names.
 * @num_levels:	Number of leves supported.
 * @parent:	Pointer to the parent's lpm_stats object.
 * @mask:	cpumask, if configuring cpu stats, else NULL.
 *
 * Function to communicate the low power mode levels supported by
 * cpus or a cluster.
 *
 * Return: Pointer to the lpm_stats object or ERR_PTR(-ERRNO)
 */
struct lpm_stats *lpm_stats_config_level(const char *name,
	const char **levels, int num_levels, struct lpm_stats *parent,
	struct cpumask *mask)
{
	struct lpm_stats *stats = NULL;

	if (!levels || num_levels <= 0 || IS_ERR(parent)) {
		pr_err("%s: Invalid input\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	if (mask)
		stats = config_cpu_level(name, levels, num_levels, parent,
						mask);
	else
		stats = config_cluster_level(name, levels, num_levels,
						parent);

	if (IS_ERR(stats)) {
		lpm_stats_cleanup(parent);
		return stats;
	}

	return stats;
}
EXPORT_SYMBOL_GPL(lpm_stats_config_level);

/**
 * lpm_stats_cluster_enter() - API to communicate the lpm level a cluster
 * is prepared to enter.
 *
 * @stats:	Pointer to the cluster's lpm_stats object.
 * @index:	Index of the lpm level that the cluster is going to enter.
 *
 * Function to communicate the low power mode level that the cluster is
 * prepared to enter.
 */
void lpm_stats_cluster_enter(struct lpm_stats *stats, uint32_t index)
{
	if (IS_ERR_OR_NULL(stats))
		return;

	update_last_in_stats(stats);
}
EXPORT_SYMBOL_GPL(lpm_stats_cluster_enter);

/**
 * lpm_stats_cluster_exit() - API to communicate the lpm level a cluster
 * exited.
 *
 * @stats:	Pointer to the cluster's lpm_stats object.
 * @index:	Index of the cluster lpm level.
 * @success:	Success/Failure of the low power mode execution.
 *
 * Function to communicate the low power mode level that the cluster
 * exited.
 */
void lpm_stats_cluster_exit(struct lpm_stats *stats, uint32_t index,
				bool success)
{
	if (IS_ERR_OR_NULL(stats))
		return;

	update_exit_stats(stats, index, success);

	update_first_out_stats(stats);
}
EXPORT_SYMBOL_GPL(lpm_stats_cluster_exit);

/**
 * lpm_stats_cpu_enter() - API to communicate the lpm level a cpu
 * is prepared to enter.
 *
 * @index:	cpu's lpm level index.
 *
 * Function to communicate the low power mode level that the cpu is
 * prepared to enter.
 */
void lpm_stats_cpu_enter(uint32_t index, uint64_t time)
{
	struct lpm_stats *stats = &(*this_cpu_ptr(&(cpu_stats)));

	stats->sleep_time = time;

	if (!stats->time_stats)
		return;

}
EXPORT_SYMBOL_GPL(lpm_stats_cpu_enter);

/**
 * lpm_stats_cpu_exit() - API to communicate the lpm level that the cpu exited.
 *
 * @index:	cpu's lpm level index.
 * @success:	Success/Failure of the low power mode execution.
 *
 * Function to communicate the low power mode level that the cpu exited.
 */
void lpm_stats_cpu_exit(uint32_t index, uint64_t time, bool success)
{
	struct lpm_stats *stats = &(*this_cpu_ptr(&(cpu_stats)));

	if (!stats->time_stats)
		return;

	stats->sleep_time = time - stats->sleep_time;

	update_exit_stats(stats, index, success);
}
EXPORT_SYMBOL_GPL(lpm_stats_cpu_exit);

/**
 * lpm_stats_suspend_enter() - API to communicate system entering suspend.
 *
 * Function to communicate that the system is ready to enter suspend.
 */
void lpm_stats_suspend_enter(void)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	suspend_time_stats.enter_time = timespec64_to_ns(&ts);
}
EXPORT_SYMBOL_GPL(lpm_stats_suspend_enter);

/**
 * lpm_stats_suspend_exit() - API to communicate system exiting suspend.
 *
 * Function to communicate that the system exited suspend.
 */
void lpm_stats_suspend_exit(void)
{
	struct timespec64 ts;
	uint64_t exit_time = 0;

	ktime_get_real_ts64(&ts);
	exit_time = timespec64_to_ns(&ts) - suspend_time_stats.enter_time;
	update_level_stats(&suspend_time_stats, exit_time, true);
}
EXPORT_SYMBOL_GPL(lpm_stats_suspend_exit);
