/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Counter sysfs interface
 * Copyright (C) 2020 William Breathitt Gray
 */
#ifndef _COUNTER_SYSFS_H_
#define _COUNTER_SYSFS_H_

#include <linux/counter.h>

int counter_sysfs_add(struct counter_device *const counter);

#endif /* _COUNTER_SYSFS_H_ */
