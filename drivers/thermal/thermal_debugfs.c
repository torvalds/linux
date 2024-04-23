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

#include "thermal_core.h"

static struct dentry *d_root;
static struct dentry *d_cdev;
static struct dentry *d_tz;

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
 * struct cdev_record - Common structure for cooling device entry
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
 * struct trip_stats - Thermal trip statistics
 *
 * The trip_stats structure has the relevant information to show the
 * statistics related to temperature going above a trip point.
 *
 * @timestamp: the trip crossing timestamp
 * @duration: total time when the zone temperature was above the trip point
 * @count: the number of times the zone temperature was above the trip point
 * @max: maximum recorded temperature above the trip point
 * @min: minimum recorded temperature above the trip point
 * @avg: average temperature above the trip point
 */
struct trip_stats {
	ktime_t timestamp;
	ktime_t duration;
	int count;
	int max;
	int min;
	int avg;
};

/**
 * struct tz_episode - A mitigation episode information
 *
 * The tz_episode structure describes a mitigation episode. A
 * mitigation episode begins the trip point with the lower temperature
 * is crossed the way up and ends when it is crossed the way
 * down. During this episode we can have multiple trip points crossed
 * the way up and down if there are multiple trip described in the
 * firmware after the lowest temperature trip point.
 *
 * @timestamp: first trip point crossed the way up
 * @duration: total duration of the mitigation episode
 * @node: a list element to be added to the list of tz events
 * @trip_stats: per trip point statistics, flexible array
 */
struct tz_episode {
	ktime_t timestamp;
	ktime_t duration;
	struct list_head node;
	struct trip_stats trip_stats[];
};

/**
 * struct tz_debugfs - Store all mitigation episodes for a thermal zone
 *
 * The tz_debugfs structure contains the list of the mitigation
 * episodes and has to track which trip point has been crossed in
 * order to handle correctly nested trip point mitigation episodes.
 *
 * We keep the history of the trip point crossed in an array and as we
 * can go back and forth inside this history, eg. trip 0,1,2,1,2,1,0,
 * we keep track of the current position in the history array.
 *
 * @tz_episodes: a list of thermal mitigation episodes
 * @trips_crossed: an array of trip points crossed by id
 * @nr_trips: the number of trip points currently being crossed
 */
struct tz_debugfs {
	struct list_head tz_episodes;
	int *trips_crossed;
	int nr_trips;
};

/**
 * struct thermal_debugfs - High level structure for a thermal object in debugfs
 *
 * The thermal_debugfs structure is the common structure used by the
 * cooling device or the thermal zone to store the statistics.
 *
 * @d_top: top directory of the thermal object directory
 * @lock: per object lock to protect the internals
 *
 * @cdev_dbg: a cooling device debug structure
 * @tz_dbg: a thermal zone debug structure
 */
struct thermal_debugfs {
	struct dentry *d_top;
	struct mutex lock;
	union {
		struct cdev_debugfs cdev_dbg;
		struct tz_debugfs tz_dbg;
	};
};

void thermal_debug_init(void)
{
	d_root = debugfs_create_dir("thermal", NULL);
	if (!d_root)
		return;

	d_cdev = debugfs_create_dir("cooling_devices", d_root);
	if (!d_cdev)
		return;

	d_tz = debugfs_create_dir("thermal_zones", d_root);
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

static struct tz_episode *thermal_debugfs_tz_event_alloc(struct thermal_zone_device *tz,
							ktime_t now)
{
	struct tz_episode *tze;
	int i;

	tze = kzalloc(struct_size(tze, trip_stats, tz->num_trips), GFP_KERNEL);
	if (!tze)
		return NULL;

	INIT_LIST_HEAD(&tze->node);
	tze->timestamp = now;

	for (i = 0; i < tz->num_trips; i++) {
		tze->trip_stats[i].min = INT_MAX;
		tze->trip_stats[i].max = INT_MIN;
	}

	return tze;
}

void thermal_debug_tz_trip_up(struct thermal_zone_device *tz,
			      const struct thermal_trip *trip)
{
	struct tz_episode *tze;
	struct tz_debugfs *tz_dbg;
	struct thermal_debugfs *thermal_dbg = tz->debugfs;
	int temperature = tz->temperature;
	int trip_id = thermal_zone_trip_id(tz, trip);
	ktime_t now = ktime_get();

	if (!thermal_dbg)
		return;

	mutex_lock(&thermal_dbg->lock);

	tz_dbg = &thermal_dbg->tz_dbg;

	/*
	 * The mitigation is starting. A mitigation can contain
	 * several episodes where each of them is related to a
	 * temperature crossing a trip point. The episodes are
	 * nested. That means when the temperature is crossing the
	 * first trip point, the duration begins to be measured. If
	 * the temperature continues to increase and reaches the
	 * second trip point, the duration of the first trip must be
	 * also accumulated.
	 *
	 * eg.
	 *
	 * temp
	 *   ^
	 *   |             --------
	 * trip 2         /        \         ------
	 *   |           /|        |\      /|      |\
	 * trip 1       / |        | `----  |      | \
	 *   |         /| |        |        |      | |\
	 * trip 0     / | |        |        |      | | \
	 *   |       /| | |        |        |      | | |\
	 *   |      / | | |        |        |      | | | `--
	 *   |     /  | | |        |        |      | | |
	 *   |-----   | | |        |        |      | | |
	 *   |        | | |        |        |      | | |
	 *    --------|-|-|--------|--------|------|-|-|------------------> time
	 *            | | |<--t2-->|        |<-t2'>| | |
	 *            | |                            | |
	 *            | |<------------t1------------>| |
	 *            |                                |
	 *            |<-------------t0--------------->|
	 *
	 */
	if (!tz_dbg->nr_trips) {
		tze = thermal_debugfs_tz_event_alloc(tz, now);
		if (!tze)
			goto unlock;

		list_add(&tze->node, &tz_dbg->tz_episodes);
	}

	/*
	 * Each time a trip point is crossed the way up, the trip_id
	 * is stored in the trip_crossed array and the nr_trips is
	 * incremented. A nr_trips equal to zero means we are entering
	 * a mitigation episode.
	 *
	 * The trip ids may not be in the ascending order but the
	 * result in the array trips_crossed will be in the ascending
	 * temperature order. The function detecting when a trip point
	 * is crossed the way down will handle the very rare case when
	 * the trip points may have been reordered during this
	 * mitigation episode.
	 */
	tz_dbg->trips_crossed[tz_dbg->nr_trips++] = trip_id;

	tze = list_first_entry(&tz_dbg->tz_episodes, struct tz_episode, node);
	tze->trip_stats[trip_id].timestamp = now;
	tze->trip_stats[trip_id].max = max(tze->trip_stats[trip_id].max, temperature);
	tze->trip_stats[trip_id].min = min(tze->trip_stats[trip_id].min, temperature);
	tze->trip_stats[trip_id].count++;
	tze->trip_stats[trip_id].avg = tze->trip_stats[trip_id].avg +
		(temperature - tze->trip_stats[trip_id].avg) /
		tze->trip_stats[trip_id].count;

unlock:
	mutex_unlock(&thermal_dbg->lock);
}

void thermal_debug_tz_trip_down(struct thermal_zone_device *tz,
				const struct thermal_trip *trip)
{
	struct thermal_debugfs *thermal_dbg = tz->debugfs;
	struct tz_episode *tze;
	struct tz_debugfs *tz_dbg;
	ktime_t delta, now = ktime_get();
	int trip_id = thermal_zone_trip_id(tz, trip);
	int i;

	if (!thermal_dbg)
		return;

	mutex_lock(&thermal_dbg->lock);

	tz_dbg = &thermal_dbg->tz_dbg;

	/*
	 * The temperature crosses the way down but there was not
	 * mitigation detected before. That may happen when the
	 * temperature is greater than a trip point when registering a
	 * thermal zone, which is a common use case as the kernel has
	 * no mitigation mechanism yet at boot time.
	 */
	if (!tz_dbg->nr_trips)
		goto out;

	for (i = tz_dbg->nr_trips - 1; i >= 0; i--) {
		if (tz_dbg->trips_crossed[i] == trip_id)
			break;
	}

	if (i < 0)
		goto out;

	tz_dbg->nr_trips--;

	if (i < tz_dbg->nr_trips)
		tz_dbg->trips_crossed[i] = tz_dbg->trips_crossed[tz_dbg->nr_trips];

	tze = list_first_entry(&tz_dbg->tz_episodes, struct tz_episode, node);

	delta = ktime_sub(now, tze->trip_stats[trip_id].timestamp);

	tze->trip_stats[trip_id].duration =
		ktime_add(delta, tze->trip_stats[trip_id].duration);

	/*
	 * This event closes the mitigation as we are crossing the
	 * last trip point the way down.
	 */
	if (!tz_dbg->nr_trips)
		tze->duration = ktime_sub(now, tze->timestamp);

out:
	mutex_unlock(&thermal_dbg->lock);
}

void thermal_debug_update_temp(struct thermal_zone_device *tz)
{
	struct thermal_debugfs *thermal_dbg = tz->debugfs;
	struct tz_episode *tze;
	struct tz_debugfs *tz_dbg;
	int trip_id, i;

	if (!thermal_dbg)
		return;

	mutex_lock(&thermal_dbg->lock);

	tz_dbg = &thermal_dbg->tz_dbg;

	if (!tz_dbg->nr_trips)
		goto out;

	for (i = 0; i < tz_dbg->nr_trips; i++) {
		trip_id = tz_dbg->trips_crossed[i];
		tze = list_first_entry(&tz_dbg->tz_episodes, struct tz_episode, node);
		tze->trip_stats[trip_id].count++;
		tze->trip_stats[trip_id].max = max(tze->trip_stats[trip_id].max, tz->temperature);
		tze->trip_stats[trip_id].min = min(tze->trip_stats[trip_id].min, tz->temperature);
		tze->trip_stats[trip_id].avg = tze->trip_stats[trip_id].avg +
			(tz->temperature - tze->trip_stats[trip_id].avg) /
			tze->trip_stats[trip_id].count;
	}
out:
	mutex_unlock(&thermal_dbg->lock);
}

static void *tze_seq_start(struct seq_file *s, loff_t *pos)
{
	struct thermal_zone_device *tz = s->private;
	struct thermal_debugfs *thermal_dbg = tz->debugfs;
	struct tz_debugfs *tz_dbg = &thermal_dbg->tz_dbg;

	mutex_lock(&thermal_dbg->lock);

	return seq_list_start(&tz_dbg->tz_episodes, *pos);
}

static void *tze_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct thermal_zone_device *tz = s->private;
	struct thermal_debugfs *thermal_dbg = tz->debugfs;
	struct tz_debugfs *tz_dbg = &thermal_dbg->tz_dbg;

	return seq_list_next(v, &tz_dbg->tz_episodes, pos);
}

static void tze_seq_stop(struct seq_file *s, void *v)
{
	struct thermal_zone_device *tz = s->private;
	struct thermal_debugfs *thermal_dbg = tz->debugfs;

	mutex_unlock(&thermal_dbg->lock);
}

static int tze_seq_show(struct seq_file *s, void *v)
{
	struct thermal_zone_device *tz = s->private;
	struct thermal_trip *trip;
	struct tz_episode *tze;
	const char *type;
	int trip_id;

	tze = list_entry((struct list_head *)v, struct tz_episode, node);

	seq_printf(s, ",-Mitigation at %lluus, duration=%llums\n",
		   ktime_to_us(tze->timestamp),
		   ktime_to_ms(tze->duration));

	seq_printf(s, "| trip |     type | temp(°mC) | hyst(°mC) |  duration  |  avg(°mC) |  min(°mC) |  max(°mC) |\n");

	for_each_trip(tz, trip) {
		/*
		 * There is no possible mitigation happening at the
		 * critical trip point, so the stats will be always
		 * zero, skip this trip point
		 */
		if (trip->type == THERMAL_TRIP_CRITICAL)
			continue;

		if (trip->type == THERMAL_TRIP_PASSIVE)
			type = "passive";
		else if (trip->type == THERMAL_TRIP_ACTIVE)
			type = "active";
		else
			type = "hot";

		trip_id = thermal_zone_trip_id(tz, trip);

		seq_printf(s, "| %*d | %*s | %*d | %*d | %*lld | %*d | %*d | %*d |\n",
			   4 , trip_id,
			   8, type,
			   9, trip->temperature,
			   9, trip->hysteresis,
			   10, ktime_to_ms(tze->trip_stats[trip_id].duration),
			   9, tze->trip_stats[trip_id].avg,
			   9, tze->trip_stats[trip_id].min,
			   9, tze->trip_stats[trip_id].max);
	}

	return 0;
}

static const struct seq_operations tze_sops = {
	.start = tze_seq_start,
	.next = tze_seq_next,
	.stop = tze_seq_stop,
	.show = tze_seq_show,
};

DEFINE_SEQ_ATTRIBUTE(tze);

void thermal_debug_tz_add(struct thermal_zone_device *tz)
{
	struct thermal_debugfs *thermal_dbg;
	struct tz_debugfs *tz_dbg;

	thermal_dbg = thermal_debugfs_add_id(d_tz, tz->id);
	if (!thermal_dbg)
		return;

	tz_dbg = &thermal_dbg->tz_dbg;

	tz_dbg->trips_crossed = kzalloc(sizeof(int) * tz->num_trips, GFP_KERNEL);
	if (!tz_dbg->trips_crossed) {
		thermal_debugfs_remove_id(thermal_dbg);
		return;
	}

	INIT_LIST_HEAD(&tz_dbg->tz_episodes);

	debugfs_create_file("mitigations", 0400, thermal_dbg->d_top, tz, &tze_fops);

	tz->debugfs = thermal_dbg;
}

void thermal_debug_tz_remove(struct thermal_zone_device *tz)
{
	struct thermal_debugfs *thermal_dbg = tz->debugfs;

	if (!thermal_dbg)
		return;

	mutex_lock(&thermal_dbg->lock);

	tz->debugfs = NULL;

	mutex_unlock(&thermal_dbg->lock);

	thermal_debugfs_remove_id(thermal_dbg);
}
