/* drivers/misc/memory_state_time.c
 *
 * Copyright (C) 2016 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hashtable.h>
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/memory-state-time.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/workqueue.h>

#define KERNEL_ATTR_RO(_name) \
static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define KERNEL_ATTR_RW(_name) \
static struct kobj_attribute _name##_attr = \
	__ATTR(_name, 0644, _name##_show, _name##_store)

#define FREQ_HASH_BITS 4
DECLARE_HASHTABLE(freq_hash_table, FREQ_HASH_BITS);

static DEFINE_MUTEX(mem_lock);

#define TAG "memory_state_time"
#define BW_NODE "/soc/memory-state-time"
#define FREQ_TBL "freq-tbl"
#define BW_TBL "bw-buckets"
#define NUM_SOURCES "num-sources"

#define LOWEST_FREQ 2

static int curr_bw;
static int curr_freq;
static u32 *bw_buckets;
static u32 *freq_buckets;
static int num_freqs;
static int num_buckets;
static int registered_bw_sources;
static u64 last_update;
static bool init_success;
static struct workqueue_struct *memory_wq;
static u32 num_sources = 10;
static int *bandwidths;

struct freq_entry {
	int freq;
	u64 *buckets; /* Bandwidth buckets. */
	struct hlist_node hash;
};

struct queue_container {
	struct work_struct update_state;
	int value;
	u64 time_now;
	int id;
	struct mutex *lock;
};

static int find_bucket(int bw)
{
	int i;

	if (bw_buckets != NULL) {
		for (i = 0; i < num_buckets; i++) {
			if (bw_buckets[i] > bw) {
				pr_debug("Found bucket %d for bandwidth %d\n",
					i, bw);
				return i;
			}
		}
		return num_buckets - 1;
	}
	return 0;
}

static u64 get_time_diff(u64 time_now)
{
	u64 ms;

	ms = time_now - last_update;
	last_update = time_now;
	return ms;
}

static ssize_t show_stat_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int i, j;
	int len = 0;
	struct freq_entry *freq_entry;

	for (i = 0; i < num_freqs; i++) {
		hash_for_each_possible(freq_hash_table, freq_entry, hash,
				freq_buckets[i]) {
			if (freq_entry->freq == freq_buckets[i]) {
				len += scnprintf(buf + len, PAGE_SIZE - len,
						"%d ", freq_buckets[i]);
				if (len >= PAGE_SIZE)
					break;
				for (j = 0; j < num_buckets; j++) {
					len += scnprintf(buf + len,
							PAGE_SIZE - len,
							"%llu ",
							freq_entry->buckets[j]);
				}
				len += scnprintf(buf + len, PAGE_SIZE - len,
						"\n");
			}
		}
	}
	pr_debug("Current Time: %llu\n", ktime_get_boot_ns());
	return len;
}
KERNEL_ATTR_RO(show_stat);

static void update_table(u64 time_now)
{
	struct freq_entry *freq_entry;

	pr_debug("Last known bw %d freq %d\n", curr_bw, curr_freq);
	hash_for_each_possible(freq_hash_table, freq_entry, hash, curr_freq) {
		if (curr_freq == freq_entry->freq) {
			freq_entry->buckets[find_bucket(curr_bw)]
					+= get_time_diff(time_now);
			break;
		}
	}
}

static bool freq_exists(int freq)
{
	int i;

	for (i = 0; i < num_freqs; i++) {
		if (freq == freq_buckets[i])
			return true;
	}
	return false;
}

static int calculate_total_bw(int bw, int index)
{
	int i;
	int total_bw = 0;

	pr_debug("memory_state_time New bw %d for id %d\n", bw, index);
	bandwidths[index] = bw;
	for (i = 0; i < registered_bw_sources; i++)
		total_bw += bandwidths[i];
	return total_bw;
}

static void freq_update_do_work(struct work_struct *work)
{
	struct queue_container *freq_state_update
			= container_of(work, struct queue_container,
			update_state);
	if (freq_state_update) {
		mutex_lock(&mem_lock);
		update_table(freq_state_update->time_now);
		curr_freq = freq_state_update->value;
		mutex_unlock(&mem_lock);
		kfree(freq_state_update);
	}
}

static void bw_update_do_work(struct work_struct *work)
{
	struct queue_container *bw_state_update
			= container_of(work, struct queue_container,
			update_state);
	if (bw_state_update) {
		mutex_lock(&mem_lock);
		update_table(bw_state_update->time_now);
		curr_bw = calculate_total_bw(bw_state_update->value,
				bw_state_update->id);
		mutex_unlock(&mem_lock);
		kfree(bw_state_update);
	}
}

static void memory_state_freq_update(struct memory_state_update_block *ub,
		int value)
{
	if (IS_ENABLED(CONFIG_MEMORY_STATE_TIME)) {
		if (freq_exists(value) && init_success) {
			struct queue_container *freq_container
				= kmalloc(sizeof(struct queue_container),
				GFP_KERNEL);
			if (!freq_container)
				return;
			INIT_WORK(&freq_container->update_state,
					freq_update_do_work);
			freq_container->time_now = ktime_get_boot_ns();
			freq_container->value = value;
			pr_debug("Scheduling freq update in work queue\n");
			queue_work(memory_wq, &freq_container->update_state);
		} else {
			pr_debug("Freq does not exist.\n");
		}
	}
}

static void memory_state_bw_update(struct memory_state_update_block *ub,
		int value)
{
	if (IS_ENABLED(CONFIG_MEMORY_STATE_TIME)) {
		if (init_success) {
			struct queue_container *bw_container
				= kmalloc(sizeof(struct queue_container),
				GFP_KERNEL);
			if (!bw_container)
				return;
			INIT_WORK(&bw_container->update_state,
					bw_update_do_work);
			bw_container->time_now = ktime_get_boot_ns();
			bw_container->value = value;
			bw_container->id = ub->id;
			pr_debug("Scheduling bandwidth update in work queue\n");
			queue_work(memory_wq, &bw_container->update_state);
		}
	}
}

struct memory_state_update_block *memory_state_register_frequency_source(void)
{
	struct memory_state_update_block *block;

	if (IS_ENABLED(CONFIG_MEMORY_STATE_TIME)) {
		pr_debug("Allocating frequency source\n");
		block = kmalloc(sizeof(struct memory_state_update_block),
					GFP_KERNEL);
		if (!block)
			return NULL;
		block->update_call = memory_state_freq_update;
		return block;
	}
	pr_err("Config option disabled.\n");
	return NULL;
}
EXPORT_SYMBOL_GPL(memory_state_register_frequency_source);

struct memory_state_update_block *memory_state_register_bandwidth_source(void)
{
	struct memory_state_update_block *block;

	if (IS_ENABLED(CONFIG_MEMORY_STATE_TIME)) {
		pr_debug("Allocating bandwidth source %d\n",
				registered_bw_sources);
		block = kmalloc(sizeof(struct memory_state_update_block),
					GFP_KERNEL);
		if (!block)
			return NULL;
		block->update_call = memory_state_bw_update;
		if (registered_bw_sources < num_sources) {
			block->id = registered_bw_sources++;
		} else {
			pr_err("Unable to allocate source; max number reached\n");
			kfree(block);
			return NULL;
		}
		return block;
	}
	pr_err("Config option disabled.\n");
	return NULL;
}
EXPORT_SYMBOL_GPL(memory_state_register_bandwidth_source);

/* Buckets are designated by their maximum.
 * Returns the buckets decided by the capability of the device.
 */
static int get_bw_buckets(struct device *dev)
{
	int ret, lenb;
	struct device_node *node = dev->of_node;

	of_property_read_u32(node, NUM_SOURCES, &num_sources);
	if (of_find_property(node, BW_TBL, &lenb)) {
		bandwidths = devm_kzalloc(dev,
				sizeof(*bandwidths) * num_sources, GFP_KERNEL);
		if (!bandwidths)
			return -ENOMEM;
		lenb /= sizeof(*bw_buckets);
		bw_buckets = devm_kzalloc(dev, lenb * sizeof(*bw_buckets),
				GFP_KERNEL);
		if (!bw_buckets) {
			devm_kfree(dev, bandwidths);
			return -ENOMEM;
		}
		ret = of_property_read_u32_array(node, BW_TBL, bw_buckets,
				lenb);
		if (ret < 0) {
			devm_kfree(dev, bandwidths);
			devm_kfree(dev, bw_buckets);
			pr_err("Unable to read bandwidth table from device tree.\n");
			return ret;
		}
	}
	curr_bw = 0;
	num_buckets = lenb;
	return 0;
}

/* Adds struct freq_entry nodes to the hashtable for each compatible frequency.
 * Returns the supported number of frequencies.
 */
static int freq_buckets_init(struct device *dev)
{
	struct freq_entry *freq_entry;
	int i;
	int ret, lenf;
	struct device_node *node = dev->of_node;

	if (of_find_property(node, FREQ_TBL, &lenf)) {
		lenf /= sizeof(*freq_buckets);
		freq_buckets = devm_kzalloc(dev, lenf * sizeof(*freq_buckets),
				GFP_KERNEL);
		if (!freq_buckets)
			return -ENOMEM;
		pr_debug("freqs found len %d\n", lenf);
		ret = of_property_read_u32_array(node, FREQ_TBL, freq_buckets,
				lenf);
		if (ret < 0) {
			devm_kfree(dev, freq_buckets);
			pr_err("Unable to read frequency table from device tree.\n");
			return ret;
		}
		pr_debug("ret freq %d\n", ret);
	}
	num_freqs = lenf;
	curr_freq = freq_buckets[LOWEST_FREQ];

	for (i = 0; i < num_freqs; i++) {
		freq_entry = devm_kzalloc(dev, sizeof(struct freq_entry),
				GFP_KERNEL);
		if (!freq_entry)
			return -ENOMEM;
		freq_entry->buckets = devm_kzalloc(dev, sizeof(u64)*num_buckets,
				GFP_KERNEL);
		if (!freq_entry->buckets) {
			devm_kfree(dev, freq_entry);
			return -ENOMEM;
		}
		pr_debug("memory_state_time Adding freq to ht %d\n",
				freq_buckets[i]);
		freq_entry->freq = freq_buckets[i];
		hash_add(freq_hash_table, &freq_entry->hash, freq_buckets[i]);
	}
	return 0;
}

struct kobject *memory_kobj;
EXPORT_SYMBOL_GPL(memory_kobj);

static struct attribute *memory_attrs[] = {
	&show_stat_attr.attr,
	NULL
};

static struct attribute_group memory_attr_group = {
	.attrs = memory_attrs,
};

static int memory_state_time_probe(struct platform_device *pdev)
{
	int error;

	error = get_bw_buckets(&pdev->dev);
	if (error)
		return error;
	error = freq_buckets_init(&pdev->dev);
	if (error)
		return error;
	last_update = ktime_get_boot_ns();
	init_success = true;

	pr_debug("memory_state_time initialized with num_freqs %d\n",
			num_freqs);
	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "memory-state-time" },
	{}
};

static struct platform_driver memory_state_time_driver = {
	.probe = memory_state_time_probe,
	.driver = {
		.name = "memory-state-time",
		.of_match_table = match_table,
		.owner = THIS_MODULE,
	},
};

static int __init memory_state_time_init(void)
{
	int error;

	hash_init(freq_hash_table);
	memory_wq = create_singlethread_workqueue("memory_wq");
	if (!memory_wq) {
		pr_err("Unable to create workqueue.\n");
		return -EINVAL;
	}
	/*
	 * Create sys/kernel directory for memory_state_time.
	 */
	memory_kobj = kobject_create_and_add(TAG, kernel_kobj);
	if (!memory_kobj) {
		pr_err("Unable to allocate memory_kobj for sysfs directory.\n");
		error = -ENOMEM;
		goto wq;
	}
	error = sysfs_create_group(memory_kobj, &memory_attr_group);
	if (error) {
		pr_err("Unable to create sysfs folder.\n");
		goto kobj;
	}

	error = platform_driver_register(&memory_state_time_driver);
	if (error) {
		pr_err("Unable to register memory_state_time platform driver.\n");
		goto group;
	}
	return 0;

group:	sysfs_remove_group(memory_kobj, &memory_attr_group);
kobj:	kobject_put(memory_kobj);
wq:	destroy_workqueue(memory_wq);
	return error;
}
module_init(memory_state_time_init);
