// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Raspberry Pi
 */

#include "v3d_drv.h"
#include "v3d_regs.h"

#define V3D_PERFMONID_MIN	1
#define V3D_PERFMONID_MAX	U32_MAX

void v3d_perfmon_get(struct v3d_perfmon *perfmon)
{
	if (perfmon)
		refcount_inc(&perfmon->refcnt);
}

void v3d_perfmon_put(struct v3d_perfmon *perfmon)
{
	if (perfmon && refcount_dec_and_test(&perfmon->refcnt))
		kfree(perfmon);
}

void v3d_perfmon_start(struct v3d_dev *v3d, struct v3d_perfmon *perfmon)
{
	unsigned int i;
	u32 mask;
	u8 ncounters;

	if (WARN_ON_ONCE(!perfmon || v3d->active_perfmon))
		return;

	ncounters = perfmon->ncounters;
	mask = GENMASK(ncounters - 1, 0);

	for (i = 0; i < ncounters; i++) {
		u32 source = i / 4;
		u32 channel = V3D_SET_FIELD(perfmon->counters[i], V3D_PCTR_S0);

		i++;
		channel |= V3D_SET_FIELD(i < ncounters ? perfmon->counters[i] : 0,
					 V3D_PCTR_S1);
		i++;
		channel |= V3D_SET_FIELD(i < ncounters ? perfmon->counters[i] : 0,
					 V3D_PCTR_S2);
		i++;
		channel |= V3D_SET_FIELD(i < ncounters ? perfmon->counters[i] : 0,
					 V3D_PCTR_S3);
		V3D_CORE_WRITE(0, V3D_V4_PCTR_0_SRC_X(source), channel);
	}

	V3D_CORE_WRITE(0, V3D_V4_PCTR_0_CLR, mask);
	V3D_CORE_WRITE(0, V3D_PCTR_0_OVERFLOW, mask);
	V3D_CORE_WRITE(0, V3D_V4_PCTR_0_EN, mask);

	v3d->active_perfmon = perfmon;
}

void v3d_perfmon_stop(struct v3d_dev *v3d, struct v3d_perfmon *perfmon,
		      bool capture)
{
	unsigned int i;

	if (!perfmon || !v3d->active_perfmon)
		return;

	mutex_lock(&perfmon->lock);
	if (perfmon != v3d->active_perfmon) {
		mutex_unlock(&perfmon->lock);
		return;
	}

	if (capture)
		for (i = 0; i < perfmon->ncounters; i++)
			perfmon->values[i] += V3D_CORE_READ(0, V3D_PCTR_0_PCTRX(i));

	V3D_CORE_WRITE(0, V3D_V4_PCTR_0_EN, 0);

	v3d->active_perfmon = NULL;
	mutex_unlock(&perfmon->lock);
}

struct v3d_perfmon *v3d_perfmon_find(struct v3d_file_priv *v3d_priv, int id)
{
	struct v3d_perfmon *perfmon;

	mutex_lock(&v3d_priv->perfmon.lock);
	perfmon = idr_find(&v3d_priv->perfmon.idr, id);
	v3d_perfmon_get(perfmon);
	mutex_unlock(&v3d_priv->perfmon.lock);

	return perfmon;
}

void v3d_perfmon_open_file(struct v3d_file_priv *v3d_priv)
{
	mutex_init(&v3d_priv->perfmon.lock);
	idr_init_base(&v3d_priv->perfmon.idr, 1);
}

static int v3d_perfmon_idr_del(int id, void *elem, void *data)
{
	struct v3d_perfmon *perfmon = elem;

	v3d_perfmon_put(perfmon);

	return 0;
}

void v3d_perfmon_close_file(struct v3d_file_priv *v3d_priv)
{
	mutex_lock(&v3d_priv->perfmon.lock);
	idr_for_each(&v3d_priv->perfmon.idr, v3d_perfmon_idr_del, NULL);
	idr_destroy(&v3d_priv->perfmon.idr);
	mutex_unlock(&v3d_priv->perfmon.lock);
}

int v3d_perfmon_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_perfmon_create *req = data;
	struct v3d_perfmon *perfmon;
	unsigned int i;
	int ret;

	/* Number of monitored counters cannot exceed HW limits. */
	if (req->ncounters > DRM_V3D_MAX_PERF_COUNTERS ||
	    !req->ncounters)
		return -EINVAL;

	/* Make sure all counters are valid. */
	for (i = 0; i < req->ncounters; i++) {
		if (req->counters[i] >= V3D_PERFCNT_NUM)
			return -EINVAL;
	}

	perfmon = kzalloc(struct_size(perfmon, values, req->ncounters),
			  GFP_KERNEL);
	if (!perfmon)
		return -ENOMEM;

	for (i = 0; i < req->ncounters; i++)
		perfmon->counters[i] = req->counters[i];

	perfmon->ncounters = req->ncounters;

	refcount_set(&perfmon->refcnt, 1);
	mutex_init(&perfmon->lock);

	mutex_lock(&v3d_priv->perfmon.lock);
	ret = idr_alloc(&v3d_priv->perfmon.idr, perfmon, V3D_PERFMONID_MIN,
			V3D_PERFMONID_MAX, GFP_KERNEL);
	mutex_unlock(&v3d_priv->perfmon.lock);

	if (ret < 0) {
		kfree(perfmon);
		return ret;
	}

	req->id = ret;

	return 0;
}

int v3d_perfmon_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_perfmon_destroy *req = data;
	struct v3d_perfmon *perfmon;

	mutex_lock(&v3d_priv->perfmon.lock);
	perfmon = idr_remove(&v3d_priv->perfmon.idr, req->id);
	mutex_unlock(&v3d_priv->perfmon.lock);

	if (!perfmon)
		return -EINVAL;

	v3d_perfmon_put(perfmon);

	return 0;
}

int v3d_perfmon_get_values_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct v3d_dev *v3d = to_v3d_dev(dev);
	struct v3d_file_priv *v3d_priv = file_priv->driver_priv;
	struct drm_v3d_perfmon_get_values *req = data;
	struct v3d_perfmon *perfmon;
	int ret = 0;

	if (req->pad != 0)
		return -EINVAL;

	mutex_lock(&v3d_priv->perfmon.lock);
	perfmon = idr_find(&v3d_priv->perfmon.idr, req->id);
	v3d_perfmon_get(perfmon);
	mutex_unlock(&v3d_priv->perfmon.lock);

	if (!perfmon)
		return -EINVAL;

	v3d_perfmon_stop(v3d, perfmon, true);

	if (copy_to_user(u64_to_user_ptr(req->values_ptr), perfmon->values,
			 perfmon->ncounters * sizeof(u64)))
		ret = -EFAULT;

	v3d_perfmon_put(perfmon);

	return ret;
}
