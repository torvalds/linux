/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2025 ARM Limited. All rights reserved. */

#ifndef __PANTHOR_PWR_H__
#define __PANTHOR_PWR_H__

struct panthor_device;

void panthor_pwr_unplug(struct panthor_device *ptdev);

int panthor_pwr_init(struct panthor_device *ptdev);

void panthor_pwr_suspend(struct panthor_device *ptdev);

void panthor_pwr_resume(struct panthor_device *ptdev);

#endif /* __PANTHOR_PWR_H__ */
