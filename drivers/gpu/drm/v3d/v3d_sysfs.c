// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Igalia S.L.
 */

#include <linux/sched/clock.h>
#include <linux/sysfs.h>

#include "v3d_drv.h"

static ssize_t
gpu_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct v3d_dev *v3d = to_v3d_dev(drm);
	enum v3d_queue queue;
	u64 timestamp = local_clock();
	u64 active_runtime;
	ssize_t len = 0;

	len += sysfs_emit(buf, "queue\ttimestamp\tjobs\truntime\n");

	for (queue = 0; queue < V3D_MAX_QUEUES; queue++) {
		if (v3d->queue[queue].start_ns)
			active_runtime = timestamp - v3d->queue[queue].start_ns;
		else
			active_runtime = 0;

		/* Each line will display the queue name, timestamp, the number
		 * of jobs sent to that queue and the runtime, as can be seem here:
		 *
		 * queue	timestamp	jobs	runtime
		 * bin		239043069420	22620	17438164056
		 * render	239043069420	22619	27284814161
		 * tfu		239043069420	8763	394592566
		 * csd		239043069420	3168	10787905530
		 * cache_clean	239043069420	6127	237375940
		 */
		len += sysfs_emit_at(buf, len, "%s\t%llu\t%llu\t%llu\n",
				     v3d_queue_to_string(queue),
				     timestamp,
				     v3d->queue[queue].jobs_sent,
				     v3d->queue[queue].enabled_ns + active_runtime);
	}

	return len;
}
static DEVICE_ATTR_RO(gpu_stats);

static struct attribute *v3d_sysfs_entries[] = {
	&dev_attr_gpu_stats.attr,
	NULL,
};

static struct attribute_group v3d_sysfs_attr_group = {
	.attrs = v3d_sysfs_entries,
};

int
v3d_sysfs_init(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &v3d_sysfs_attr_group);
}

void
v3d_sysfs_destroy(struct device *dev)
{
	return sysfs_remove_group(&dev->kobj, &v3d_sysfs_attr_group);
}
