// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2026 Arm, Ltd. */
/* Based on v3d_perfmon.c, Copyright (C) 2021 Raspberry Pi */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>

#include <uapi/drm/ethosu_accel.h>

#include "ethosu_drv.h"
#include "ethosu_device.h"

void ethosu_perfmon_get(struct ethosu_perfmon *perfmon)
{
	if (perfmon)
		refcount_inc(&perfmon->refcnt);
}

void ethosu_perfmon_put(struct ethosu_perfmon *perfmon)
{
	if (perfmon && refcount_dec_and_test(&perfmon->refcnt))
		kfree(perfmon);
}

void ethosu_perfmon_start(struct ethosu_device *ethosu, struct ethosu_perfmon *perfmon)
{
	unsigned int i;
	u8 ncounters;
	u32 mask;

	lockdep_assert_held(&ethosu->perfmon_state.lock);

	if (WARN_ON_ONCE(!perfmon || ethosu->perfmon_state.active))
		return;

	writel_relaxed(PMCR_CNT_EN, ethosu->pmu_regs + NPU_REG_PMCR);
	writel_relaxed(PMU_EV_TYPE_CYCLES, ethosu->pmu_regs + NPU_REG_PMCCNTR_CFG);

	mask = 0x80000000;
	ncounters = perfmon->ncounters - 1;
	if (ncounters)
		mask |= GENMASK(ncounters - 1, 0);

	for (i = 0; i < ncounters; i++)
		writel_relaxed(perfmon->counters[i], ethosu->pmu_regs + NPU_REG_PMU_EVTYPER(i));

	writel_relaxed(mask, ethosu->pmu_regs + NPU_REG_PMCNTENSET);
	writel_relaxed(PMCR_CNT_EN | PMCR_EVENT_CNT_RST | PMCR_CYCLE_CNT_RST,
		ethosu->pmu_regs + NPU_REG_PMCR);
	ethosu->perfmon_state.active = perfmon;
}

void ethosu_perfmon_stop_locked(struct ethosu_device *ethosu, struct ethosu_perfmon *perfmon,
				bool capture)
{
	unsigned int i;
	u8 ncounters;
	u32 mask;

	lockdep_assert_held(&ethosu->perfmon_state.lock);

	if (!perfmon || perfmon != ethosu->perfmon_state.active)
		return;

	ncounters = perfmon->ncounters - 1;

	if (!pm_runtime_get_if_active(ethosu->base.dev)) {
		ethosu->perfmon_state.active = NULL;
		return;
	}

	if (capture) {
		for (i = 0; i < ncounters; i++)
			perfmon->values[i] += readl_relaxed(ethosu->pmu_regs + NPU_REG_PMU_EVCNTR(i));

		perfmon->values[ncounters] +=
			readl_relaxed(ethosu->pmu_regs + NPU_REG_PMCCNTR_LO) |
			(u64)readl_relaxed(ethosu->pmu_regs + NPU_REG_PMCCNTR_HI) << 32;
	}

	mask = 0x80000000;
	if (ncounters)
		mask |= GENMASK(ncounters - 1, 0);
	writel_relaxed(mask, ethosu->pmu_regs + NPU_REG_PMCNTENCLR);

	writel_relaxed(0, ethosu->pmu_regs + NPU_REG_PMCR);
	ethosu->perfmon_state.active = NULL;

	pm_runtime_put(ethosu->base.dev);
}

void ethosu_perfmon_stop(struct ethosu_device *ethosu, struct ethosu_perfmon *perfmon,
			 bool capture)
{
	if (!perfmon)
		return;

	guard(mutex)(&ethosu->perfmon_state.lock);
	ethosu_perfmon_stop_locked(ethosu, perfmon, capture);
}

struct ethosu_perfmon *ethosu_perfmon_find(struct ethosu_file_priv *ethosu_priv, int id)
{
	struct ethosu_perfmon *perfmon;

	xa_lock(&ethosu_priv->perfmons);
	perfmon = xa_load(&ethosu_priv->perfmons, id);
	ethosu_perfmon_get(perfmon);
	xa_unlock(&ethosu_priv->perfmons);

	return perfmon;
}

void ethosu_perfmon_open_file(struct ethosu_file_priv *ethosu_priv)
{
	xa_init_flags(&ethosu_priv->perfmons, XA_FLAGS_ALLOC1);
}

static void ethosu_perfmon_delete(struct ethosu_file_priv *ethosu_priv,
				 struct ethosu_perfmon *perfmon)
{
	struct ethosu_device *ethosu = ethosu_priv->edev;

	/* If the active perfmon is being destroyed, stop it first */
	scoped_guard(mutex, &ethosu->perfmon_state.lock) {
		/* If the global perfmon is being destroyed, set it to NULL */
		if (ethosu->global_perfmon == perfmon) {
			ethosu->global_perfmon = NULL;
			ethosu_perfmon_put(perfmon);
		}

		ethosu_perfmon_stop_locked(ethosu, perfmon, false);
	}

	ethosu_perfmon_put(perfmon);
}

void ethosu_perfmon_close_file(struct ethosu_file_priv *ethosu_priv)
{
	struct ethosu_perfmon *perfmon;
	unsigned long id;

	xa_for_each(&ethosu_priv->perfmons, id, perfmon)
		ethosu_perfmon_delete(ethosu_priv, perfmon);

	xa_destroy(&ethosu_priv->perfmons);
}

int ethosu_ioctl_perfmon_create(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct ethosu_file_priv *ethosu_priv = file_priv->driver_priv;
	struct drm_ethosu_perfmon_create *req = data;
	struct ethosu_device *ethosu = to_ethosu_device(dev);
	struct ethosu_perfmon *perfmon;
	unsigned int i, event_max;
	int ret;
	u32 id;

	/* Number of monitored counters cannot exceed HW limits. */
	if (req->ncounters > ethosu->npu_info.pmu_counters)
		return -EINVAL;

	/* Make sure all counters are valid. */
	event_max = ethosu_is_u65(ethosu) ? 433 : 671;
	for (i = 0; i < req->ncounters; i++) {
		if (req->counters[i] > event_max)
			return -EINVAL;
	}

	/* Add 1 more counter for cycle counter */
	req->ncounters++;

	perfmon = kzalloc_flex(*perfmon, values, req->ncounters);
	if (!perfmon)
		return -ENOMEM;

	for (i = 0; i < req->ncounters - 1; i++)
		perfmon->counters[i] = req->counters[i];

	perfmon->ncounters = req->ncounters;

	refcount_set(&perfmon->refcnt, 1);

	ret = xa_alloc(&ethosu_priv->perfmons, &id, perfmon, xa_limit_32b,
		       GFP_KERNEL);

	if (ret < 0) {
		kfree(perfmon);
		return ret;
	}

	req->id = id;

	return 0;
}

int ethosu_ioctl_perfmon_destroy(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct ethosu_file_priv *ethosu_priv = file_priv->driver_priv;
	struct drm_ethosu_perfmon_destroy *req = data;
	struct ethosu_perfmon *perfmon;

	perfmon = xa_erase(&ethosu_priv->perfmons, req->id);
	if (!perfmon)
		return -EINVAL;

	ethosu_perfmon_delete(ethosu_priv, perfmon);

	return 0;
}

int ethosu_ioctl_perfmon_get_values(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct ethosu_device *ethosu = to_ethosu_device(dev);
	struct ethosu_file_priv *ethosu_priv = file_priv->driver_priv;
	struct drm_ethosu_perfmon_get_values *req = data;
	struct ethosu_perfmon *perfmon;
	int ret = 0;

	if (req->pad != 0)
		return -EINVAL;

	perfmon = ethosu_perfmon_find(ethosu_priv, req->id);
	if (!perfmon)
		return -EINVAL;

	ret = pm_runtime_resume_and_get(dev->dev);
	if (ret) {
		ethosu_perfmon_put(perfmon);
		return ret;
	}
	ethosu_perfmon_stop(ethosu, perfmon, true);

	pm_runtime_put_autosuspend(dev->dev);

	if (copy_to_user(u64_to_user_ptr(req->values_ptr), perfmon->values,
			 perfmon->ncounters * sizeof(u64)))
		ret = -EFAULT;

	ethosu_perfmon_put(perfmon);

	return ret;
}

int ethosu_ioctl_perfmon_set_global(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct ethosu_file_priv *ethosu_priv = file_priv->driver_priv;
	struct drm_ethosu_perfmon_set_global *req = data;
	struct ethosu_device *ethosu = to_ethosu_device(dev);
	struct ethosu_perfmon *perfmon;

	if (req->flags & ~DRM_ETHOSU_PERFMON_CLEAR_GLOBAL)
		return -EINVAL;

	perfmon = ethosu_perfmon_find(ethosu_priv, req->id);
	if (!perfmon)
		return -EINVAL;

	/* If the request is to clear the global performance monitor */
	if (req->flags & DRM_ETHOSU_PERFMON_CLEAR_GLOBAL) {
		struct ethosu_perfmon *old;

		scoped_guard(mutex, &ethosu->perfmon_state.lock) {
			old = ethosu->global_perfmon;
			if (!old) {
				ethosu_perfmon_put(perfmon);
				return -EINVAL;
			}

			ethosu->global_perfmon = NULL;
			ethosu_perfmon_stop_locked(ethosu, old, true);
		}

		ethosu_perfmon_put(old);
		ethosu_perfmon_put(perfmon);

		return 0;
	}

	scoped_guard(mutex, &ethosu->perfmon_state.lock) {
		if (ethosu->perfmon_state.active || ethosu->global_perfmon) {
			ethosu_perfmon_put(perfmon);
			return -EBUSY;
		}

		ethosu->global_perfmon = perfmon;
	}

	return 0;
}
