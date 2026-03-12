// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Broadcom
 */

/**
 * DOC: VC4 V3D performance monitor module
 *
 * The V3D block provides 16 hardware counters which can count various events.
 */

#include <drm/drm_print.h>

#include "vc4_drv.h"
#include "vc4_regs.h"

void vc4_perfmon_get(struct vc4_perfmon *perfmon)
{
	struct vc4_dev *vc4;

	if (!perfmon)
		return;

	vc4 = perfmon->dev;
	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return;

	refcount_inc(&perfmon->refcnt);
}

void vc4_perfmon_put(struct vc4_perfmon *perfmon)
{
	struct vc4_dev *vc4;

	if (!perfmon)
		return;

	vc4 = perfmon->dev;
	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return;

	if (refcount_dec_and_test(&perfmon->refcnt))
		kfree(perfmon);
}

void vc4_perfmon_start(struct vc4_dev *vc4, struct vc4_perfmon *perfmon)
{
	unsigned int i;
	u32 mask;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return;

	if (WARN_ON_ONCE(!perfmon || vc4->active_perfmon))
		return;

	for (i = 0; i < perfmon->ncounters; i++)
		V3D_WRITE(V3D_PCTRS(i), perfmon->events[i]);

	mask = GENMASK(perfmon->ncounters - 1, 0);
	V3D_WRITE(V3D_PCTRC, mask);
	V3D_WRITE(V3D_PCTRE, V3D_PCTRE_EN | mask);
	vc4->active_perfmon = perfmon;
}

void vc4_perfmon_stop(struct vc4_dev *vc4, struct vc4_perfmon *perfmon,
		      bool capture)
{
	unsigned int i;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return;

	if (WARN_ON_ONCE(!vc4->active_perfmon ||
			 perfmon != vc4->active_perfmon))
		return;

	if (capture) {
		for (i = 0; i < perfmon->ncounters; i++)
			perfmon->counters[i] += V3D_READ(V3D_PCTR(i));
	}

	V3D_WRITE(V3D_PCTRE, 0);
	vc4->active_perfmon = NULL;
}

struct vc4_perfmon *vc4_perfmon_find(struct vc4_file *vc4file, int id)
{
	struct vc4_dev *vc4 = vc4file->dev;
	struct vc4_perfmon *perfmon;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return NULL;

	xa_lock(&vc4file->perfmons);
	perfmon = xa_load(&vc4file->perfmons, id);
	vc4_perfmon_get(perfmon);
	xa_unlock(&vc4file->perfmons);

	return perfmon;
}

void vc4_perfmon_open_file(struct vc4_file *vc4file)
{
	struct vc4_dev *vc4 = vc4file->dev;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return;

	xa_init_flags(&vc4file->perfmons, XA_FLAGS_ALLOC1);
}

static void vc4_perfmon_delete(struct vc4_file *vc4file,
			       struct vc4_perfmon *perfmon)
{
	struct vc4_dev *vc4 = vc4file->dev;

	/* If the active perfmon is being destroyed, stop it first */
	if (perfmon == vc4->active_perfmon)
		vc4_perfmon_stop(vc4, perfmon, false);

	vc4_perfmon_put(perfmon);
}

void vc4_perfmon_close_file(struct vc4_file *vc4file)
{
	struct vc4_dev *vc4 = vc4file->dev;
	struct vc4_perfmon *perfmon;
	unsigned long id;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return;

	xa_for_each(&vc4file->perfmons, id, perfmon)
		vc4_perfmon_delete(vc4file, perfmon);

	xa_destroy(&vc4file->perfmons);
}

int vc4_perfmon_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_file *vc4file = file_priv->driver_priv;
	struct drm_vc4_perfmon_create *req = data;
	struct vc4_perfmon *perfmon;
	unsigned int i;
	int ret;
	u32 id;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	if (!vc4->v3d) {
		DRM_DEBUG("Creating perfmon no VC4 V3D probed\n");
		return -ENODEV;
	}

	/* Number of monitored counters cannot exceed HW limits. */
	if (req->ncounters > DRM_VC4_MAX_PERF_COUNTERS ||
	    !req->ncounters)
		return -EINVAL;

	/* Make sure all events are valid. */
	for (i = 0; i < req->ncounters; i++) {
		if (req->events[i] >= VC4_PERFCNT_NUM_EVENTS)
			return -EINVAL;
	}

	perfmon = kzalloc_flex(*perfmon, counters, req->ncounters);
	if (!perfmon)
		return -ENOMEM;
	perfmon->dev = vc4;

	for (i = 0; i < req->ncounters; i++)
		perfmon->events[i] = req->events[i];

	perfmon->ncounters = req->ncounters;

	refcount_set(&perfmon->refcnt, 1);

	ret = xa_alloc(&vc4file->perfmons, &id, perfmon, xa_limit_32b,
		       GFP_KERNEL);
	if (ret < 0) {
		kfree(perfmon);
		return ret;
	}

	req->id = id;

	return 0;
}

int vc4_perfmon_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_file *vc4file = file_priv->driver_priv;
	struct drm_vc4_perfmon_destroy *req = data;
	struct vc4_perfmon *perfmon;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	if (!vc4->v3d) {
		DRM_DEBUG("Destroying perfmon no VC4 V3D probed\n");
		return -ENODEV;
	}

	perfmon = xa_erase(&vc4file->perfmons, req->id);
	if (!perfmon)
		return -EINVAL;

	vc4_perfmon_delete(vc4file, perfmon);

	return 0;
}

int vc4_perfmon_get_values_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_file *vc4file = file_priv->driver_priv;
	struct drm_vc4_perfmon_get_values *req = data;
	struct vc4_perfmon *perfmon;
	int ret;

	if (WARN_ON_ONCE(vc4->gen > VC4_GEN_4))
		return -ENODEV;

	if (!vc4->v3d) {
		DRM_DEBUG("Getting perfmon no VC4 V3D probed\n");
		return -ENODEV;
	}

	perfmon = vc4_perfmon_find(vc4file, req->id);
	if (!perfmon)
		return -EINVAL;

	if (copy_to_user(u64_to_user_ptr(req->values_ptr), perfmon->counters,
			 perfmon->ncounters * sizeof(u64)))
		ret = -EFAULT;
	else
		ret = 0;

	vc4_perfmon_put(perfmon);
	return ret;
}
