/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2019 Collabora ltd. */

#ifndef __PANTHOR_DEVFREQ_H__
#define __PANTHOR_DEVFREQ_H__

struct devfreq;
struct thermal_cooling_device;

struct panthor_device;
struct panthor_devfreq;

int panthor_devfreq_init(struct panthor_device *ptdev);

int panthor_devfreq_resume(struct panthor_device *ptdev);
int panthor_devfreq_suspend(struct panthor_device *ptdev);

void panthor_devfreq_record_busy(struct panthor_device *ptdev);
void panthor_devfreq_record_idle(struct panthor_device *ptdev);

#endif /* __PANTHOR_DEVFREQ_H__ */
