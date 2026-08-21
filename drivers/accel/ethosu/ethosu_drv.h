/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright 2025-2026 Arm, Ltd. */
#ifndef __ETHOSU_DRV_H__
#define __ETHOSU_DRV_H__

#include <linux/mutex.h>
#include <linux/xarray.h>
#include <drm/gpu_scheduler.h>

struct ethosu_device;
struct drm_device;
struct drm_file;

struct ethosu_file_priv {
	struct ethosu_device *edev;
	struct drm_sched_entity sched_entity;
	struct xarray perfmons;
};

/* Performance monitor object. The perfmon lifetime is controlled by userspace
 * using perfmon related ioctls. A perfmon can be attached to a DRM_ETHOSU_SUBMIT
 * request, and when this is the case, HW perf counters will be activated just
 * before the job is submitted to the NPU and disabled when the job is
 * done. This way, only events related to a specific job will be counted.
 */
struct ethosu_perfmon {
	/* Tracks the number of users of the perfmon, when this counter reaches
	 * zero the perfmon is destroyed.
	 */
	refcount_t refcnt;

	/* Number of counters activated in this perfmon instance
	 * (should be less than or equal to DRM_ETHOSU_MAX_PERF_COUNTERS).
	 */
	u8 ncounters;

	/* Events counted by the HW perf counters. */
	u16 counters[DRM_ETHOSU_MAX_PERF_EVENT_COUNTERS];

	/*
	 * Storage for counter values. Counters are incremented by the HW
	 * perf counter values every time the perfmon is attached to an
	 * NPU job. This way, perfmon users don't have to retrieve the
	 * results after each job if they want to track events covering
	 * several submissions. Note that counter values can't be reset,
	 * but you can fake a reset by destroying the perfmon and
	 * creating a new one.
	 */
	u64 values[] __counted_by(ncounters);
};

/* ethosu_perfmon.c */
void ethosu_perfmon_get(struct ethosu_perfmon *perfmon);
void ethosu_perfmon_put(struct ethosu_perfmon *perfmon);
void ethosu_perfmon_start(struct ethosu_device *ethosu,
			  struct ethosu_perfmon *perfmon);
void ethosu_perfmon_stop(struct ethosu_device *ethosu,
			 struct ethosu_perfmon *perfmon, bool capture);
void ethosu_perfmon_stop_locked(struct ethosu_device *ethosu, struct ethosu_perfmon *perfmon,
				bool capture);
struct ethosu_perfmon *ethosu_perfmon_find(struct ethosu_file_priv *ethosu_priv,
					   int id);
void ethosu_perfmon_open_file(struct ethosu_file_priv *ethosu_priv);
void ethosu_perfmon_close_file(struct ethosu_file_priv *ethosu_priv);
int ethosu_ioctl_perfmon_create(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
int ethosu_ioctl_perfmon_destroy(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
int ethosu_ioctl_perfmon_get_values(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int ethosu_ioctl_perfmon_set_global(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);

#endif
