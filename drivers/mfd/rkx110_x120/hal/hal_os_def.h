// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Joseph Chen <chenjh@rock-chips.com>
 */

#ifndef _HAL_OS_DEF_H_
#define _HAL_OS_DEF_H_

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/i2c.h>

#define HAL_BITS_PER_LONG BITS_PER_LONG
#define HAL_SYSLOG        pr_info
#define HAL_DelayUs       udelay
#define HAL_SleepMs       msleep
#define HAL_ARRAY_SIZE    ARRAY_SIZE

#define HAL_DEFINE_MUTEX DEFINE_MUTEX
#define HAL_Mutex        struct mutex
#define HAL_MutexInit    mutex_init
#define HAL_MutexLock    mutex_lock
#define HAL_MutexUnlock  mutex_unlock

#define HAL_KCALLOC(n, size) kcalloc(n, size, GFP_KERNEL)

typedef int (HAL_RegRead_t)(struct i2c_client *client, uint32_t addr, uint32_t *value);
typedef int (HAL_RegWrite_t)(struct i2c_client *client, uint32_t addr, uint32_t value);

#endif /* _HAL_OS_DEF_H_ */
