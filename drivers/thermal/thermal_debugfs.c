
// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * Thermal subsystem debug support
 */
#include <linux/debugfs.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/thermal.h>

static struct dentry *d_root;
static struct dentry *d_cdev;

/*
 * Length of the string containing the thermal zone id or the cooling
 * device id, including the ending nul character. We can reasonably
 * assume there won't be more than 256 thermal zones as the maximum
 * observed today is around 32.
 */
#define IDSLENGTH 4

/*
 * The cooling device transition list is stored in a hash table where
 * the size is CDEVSTATS_HASH_SIZE. The majority of cooling devices
 * have dozen of states but some can have much more, so a hash table
 * is more adequate in this case, because the cost of browsing the entire
 * list when storing the transitions may not be negligible.
 */
#define CDEVSTATS_HASH_SIZE 16

/**
 * struct cdev_debugfs - per cooling device statistics structure
 * A cooling device can have a high number of states. Showing the
 * transitions on a matrix based representation can be overkill given
 * most of the transitions won't happen and we end up with a matrix
 * filled with zero. Instead, we show the transitions which actually
 * happened.
 *
 * Every transition updates the current_state and the timestamp. The
 * transitions and the durations are stored in lists.
 *
 * @total: the number of transitions for this cooling device
 * @current_state: the current cooling device state
 * @timestamp: the state change timestamp
 * @transitions: an array of lists containing the state transitions
 * @durations: an array of lists containing the residencies of each state
 */
struct cdev_debugfs {
	u32 total;
	int current_state;
	ktime_t timestamp;
	struct list_head transitions[CDEVSTATS_HASH_SIZE];
	struct list_head durations[CDEVSTATS_HASH_SIZE];
};

/**
 * struct cdev_value - Common structure for cooling device entry
 *
 * The following common structure allows to store the information
 * related to the transitions and to the state residencies. They are
 * identified with a id which is associated to a value. It is used as
 * nodes for the "transitions" and "durations" above.
 *
 * @node: node to insert the structure in a list
 * @id: identifier of the value which can be a state or a transition
 * @residency: a ktime_t representing a state residency duration
 * @count: a number of occurrences
 */
struct cdev_record {
	struct list_head node;
	int id;
	union {
                ktime_t residency;
                u64 count;
        };
};

/**
 * struct thermal_debugfs - High level structure for a thermal object in debugfs
 *
 * The thermal_debugfs structure is the common structure used by the
 * cooling device to compute the statistics.
 *
 * @d_top: top directory of the thermal object directory
 * @lock: per object lock to protect the internals
 *
 * @cdev: a cooling device debug structure
 */
struct thermal_debugfs {
	struct dentry *d_top;
	struct mutex lock;
	union {
		struct cdev_debugfs cdev_dbg;
	};
};

void thermal_debug_init(void)
{
	d_root = debugfs_create_dir("thermal", NULL);
	if (!d_root)
		return;

	d_cdev = debugfs_create_dir("cooling_devices", d_root);
}

static struct thermal_debugfs *thermal_debugfs_add_id(struct dentry *d, int id)
{
	struct thermal_debugfs *thermal_dbg;
	char ids[IDSLENGTH];

	thermal_dbg = kzalloc(sizeof(*thermal_dbg), GFP_KERNEL);
	if (!thermal_dbg)
		return NULL;

	mutex_init(&thermal_dbg->lock);

	snprintf(ids, IDSLENGTH, "%d", id);

	thermal_dbg->d_top = debugfs_create_dir(ids, d);
	if (!thermal_dbg->d_top) {
		kfree(thermal_dbg);
		return NULL;
	}

	return thermal_dbg;
}

static void thermal_debugfs_remove_id(struct thermal_debugfs *thermal_dbg)
{
	if (!thermal_dbg)
		return;

	debugfs_remove(thermal_dbg->d_top);

	kfree(thermal_dbg);
}

static struct cdev_record *
thermal_debugfs_cdev_record_alloc(struct thermal_debugfs *thermal_dbg,
				  struct list_head *lists, int id)
{
	struct cdev_record *cdev_record;

	cdev_record = kzalloc(sizeof(*cdev_record), GFP_KERNEL);
	if (!cdev_record)
		return NULL;

	cdev_record->id = id;
	INIT_LIST_HEAD(&cdev_record->node);
	list_add_tail(&cdev_record->node,
		      &lists[cdev_record->id % CDEVSTATS_HASH_SIZE]);

	return cdev_record;
}

static struct cdev_record *
thermal_debugfs_cdev_record_find(struct thermal_debugfs *thermal_dbg,
				 struct list_head *lists, int id)
{
	struct cdev_record *entry;

	list_for_each_entry(entry, &lists[id % CDEVSTATS_HASH_SIZE], node)
		if (entry->id == id)
			return entry;

	return NULL;
}

static struct cdev_record *
thermal_debugfs_cdev_record_get(struct thermal_debugfs *thermal_dbg,
				struct list_head *lists, int id)
{
	struct cdev_record *cdev_record;

	cdev_record = thermal_debugfs_cdev_record_find(thermal_dbg, lists, id);
	if (cdev_record)
		return cdev_record;

	return thermal_debugfs_cdev_record_alloc(thermal_dbg, lists, id);
}

static void thermal_debugfs_cdev_clear(struct cdev_debugfs *cdev_dbg)
{
	int i;
	struct cdev_record *entry, *tmp;

	for (i = 0; i < CDEVSTATS_HASH_SIZE; i++) {

		list_for_each_entry_safe(entry, tmp,
					 &cdev_dbg->transitions[i], node) {
			list_del(&entry->node);
			kfree(entry);
		}

		list_for_each_entry_safe(entry, tmp,
					 &cdev_dbg->durations[i], node) {
			list_del(&entry->node);
			kfree(entry);
		}
	}

	cdev_dbg->total = 0;
}

static void *cdev_seq_start(struct seq_file *s, loff_t *pos)
{
	struct thermal_debugfs *thermal_dbg = s->private;

	mutex_lock(&thermal_dbg->lock);

	return (*pos < CDEVSTATS_HASH_SIZE) ? pos : NULL;
}

static void *cdev_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;

	return (*pos < CDEVSTATS_HASH_SIZE) ? pos : NULL;
}

static void cdev_seq_stop(struct seq_file *s, void *v)
{
	struct thermal_debugfs *thermal_dbg = s->private;

	mutex_unlock(&thermal_dbg->lock);
}

static int cdev_tt_seq_show(struct seq_file *s, void *v)
{
	struct thermal_debugfs *thermal_dbg = s->private;
	struct cdev_debugfs *cdev_dbg = &thermal_dbg->cdev_dbg;
	struct list_head *transitions = cdev_dbg->transitions;
	struct cdev_record *entry;
	int i = *(loff_t *)v;

	if (!i)
		seq_puts(s, "Transition\tOccurences\n");

	list_for_each_entry(entry, &transitions[i], node) {
		/*
		 * Assuming maximum cdev states is 1024, the longer
		 * string for a transition would be "1024->1024\0"
		 */
		char buffer[11];

		snprintf(buffer, ARRAY_SIZE(buffer), "%d->%d",
			 entry->id >> 16, entry->id & 0xFFFF);

		seq_printf(s, "%-10s\t%-10llu\n", buffer, entry->count);
	}

	return 0;
}

static const struct seq_operations tt_sops = {
	.start = cdev_seq_start,
	.next = cdev_seq_next,
	.stop = cdev_seq_stop,
	.show = cdev_tt_seq_show,
};

DEFINE_SEQ_ATTRIBUTE(tt);

static int cdev_dt_seq_show(struct seq_file *s, void *v)
{
	struct thermal_debugfs *thermal_dbg = s->private;
	struct cdev_debugfs *cdev_dbg = &thermal_dbg->cdev_dbg;
	struct list_head *durations = cdev_dbg->durations;
	struct cdev_record *entry;
	int i = *(loff_t *)v;

	if (!i)
		seq_puts(s, "State\tResidency\n");

	list_for_each_entry(entry, &durations[i], node) {
		s64 duration = ktime_to_ms(entry->residency);

		if (entry->id == cdev_dbg->current_state)
			duration += ktime_ms_delta(ktime_get(),
						   cdev_dbg->timestamp);

		seq_printf(s, "%-5d\t%-10llu\n", entry->id, duration);
	}

	return 0;
}

static const struct seq_operations dt_sops = {
	.start = cdev_seq_start,
	.next = cdev_seq_next,
	.stop = cdev_seq_stop,
	.show = cdev_dt_seq_show,
};

DEFINE_SEQ_ATTRIBUTE(dt);

static int cdev_clear_set(void *data, u64 val)
{
	struct thermal_debugfs *thermal_dbg = data;

	if (!val)
		return -EINVAL;

	mutex_lock(&thermal_dbg->lock);

	thermal_debugfs_cdev_clear(&thermal_dbg->cdev_dbg);

	mutex_unlock(&thermal_dbg->lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(cdev_clear_fops, NULL, cdev_clear_set, "%llu\n");

/**
 * thermal_debug_cdev_state_update - Update a cooling device state change
 *
 * Computes a transition and the duration of the previous state residency.
 *
 * @cdev : a pointer to a cooling device
 * @new_state: an integer corresponding to the new cooling device state
 */
void thermal_debug_cdev_state_update(const struct thermal_cooling_device *cdev,
				     int new_state)
{
	struct thermal_debugfs *thermal_dbg = cdev->debugfs;
	struct cdev_debugfs *cdev_dbg;
	struct cdev_record *cdev_record;
	int transition, old_state;

	if (!thermal_dbg || (thermal_dbg->cdev_dbg.current_state == new_state))
		return;

	mutex_lock(&thermal_dbg->lock);

	cdev_dbg = &thermal_dbg->cdev_dbg;

	old_state = cdev_dbg->current_state;

	/*
	 * Get the old state information in the durations list. If
	 * this one does not exist, a new allocated one will be
	 * returned. Recompute the total duration in the old state and
	 * get a new timestamp for the new state.
	 */
	cdev_record = thermal_debugfs_cdev_record_get(thermal_dbg,
						      cdev_dbg->durations,
						      old_state);
	if (cdev_record) {
		ktime_t now = ktime_get();
		ktime_t delta = ktime_sub(now, cdev_dbg->timestamp);
		cdev_record->residency = ktime_add(cdev_record->residency, delta);
		cdev_dbg->timestamp = now;
	}

	cdev_dbg->current_state = new_state;
	transition = (old_state << 16) | new_state;

	/*
	 * Get the transition in the transitions list. If this one
	 * does not exist, a new allocated one will be returned.
	 * Increment the occurrence of this transition which is stored
	 * in the value field.
	 */
	cdev_record = thermal_debugfs_cdev_record_get(thermal_dbg,
						      cdev_dbg->transitions,
						      transition);
	if (cdev_record)
		cdev_record->count++;

	cdev_dbg->total++;

	mutex_unlock(&thermal_dbg->lock);
}

/**
 * thermal_debug_cdev_add - Add a cooling device debugfs entry
 *
 * Allocates a cooling device object for debug, initializes the
 * statistics and create the entries in sysfs.
 * @cdev: a pointer to a cooling device
 */
void thermal_debug_cdev_add(struct thermal_cooling_device *cdev)
{
	struct thermal_debugfs *thermal_dbg;
	struct cdev_debugfs *cdev_dbg;
	int i;

	thermal_dbg = thermal_debugfs_add_id(d_cdev, cdev->id);
	if (!thermal_dbg)
		return;

	cdev_dbg = &thermal_dbg->cdev_dbg;

	for (i = 0; i < CDEVSTATS_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&cdev_dbg->transitions[i]);
		INIT_LIST_HEAD(&cdev_dbg->durations[i]);
	}

	cdev_dbg->current_state = 0;
	cdev_dbg->timestamp = ktime_get();

	debugfs_create_file("trans_table", 0400, thermal_dbg->d_top,
			    thermal_dbg, &tt_fops);

	debugfs_create_file("time_in_state_ms", 0400, thermal_dbg->d_top,
			    thermal_dbg, &dt_fops);

	debugfs_create_file("clear", 0200, thermal_dbg->d_top,
			    thermal_dbg, &cdev_clear_fops);

	debugfs_create_u32("total_trans", 0400, thermal_dbg->d_top,
			   &cdev_dbg->total);

	cdev->debugfs = thermal_dbg;
}

/**
 * thermal_debug_cdev_remove - Remove a cooling device debugfs entry
 *
 * Frees the statistics memory data and remove the debugfs entry
 *
 * @cdev: a pointer to a cooling device
 */
void thermal_debug_cdev_remove(struct thermal_cooling_device *cdev)
{
	struct thermal_debugfs *thermal_dbg = cdev->debugfs;

	if (!thermal_dbg)
		return;

	mutex_lock(&thermal_dbg->lock);

	thermal_debugfs_cdev_clear(&thermal_dbg->cdev_dbg);
	cdev->debugfs = NULL;

	mutex_unlock(&thermal_dbg->lock);

	thermal_debugfs_remove_id(thermal_dbg);
}
