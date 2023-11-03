/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_DEVFREQ_H
#define __LINUX_RKNPU_DEVFREQ_H

#ifdef CONFIG_PM_DEVFREQ
void rknpu_devfreq_lock(struct rknpu_device *rknpu_dev);
void rknpu_devfreq_unlock(struct rknpu_device *rknpu_dev);
int rknpu_devfreq_init(struct rknpu_device *rknpu_dev);
void rknpu_devfreq_remove(struct rknpu_device *rknpu_dev);
int rknpu_devfreq_runtime_suspend(struct device *dev);
int rknpu_devfreq_runtime_resume(struct device *dev);
#else
static inline int rknpu_devfreq_init(struct rknpu_device *rknpu_dev)
{
	return -EOPNOTSUPP;
}

static inline void rknpu_devfreq_remove(struct rknpu_device *rknpu_dev)
{
}

static inline void rknpu_devfreq_lock(struct rknpu_device *rknpu_dev)
{
}

static inline void rknpu_devfreq_unlock(struct rknpu_device *rknpu_dev)
{
}

static inline int rknpu_devfreq_runtime_suspend(struct device *dev)
{
	return -EOPNOTSUPP;
}

static inline int rknpu_devfreq_runtime_resume(struct device *dev)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_PM_DEVFREQ */

#endif /* __LINUX_RKNPU_DEVFREQ_H_ */
