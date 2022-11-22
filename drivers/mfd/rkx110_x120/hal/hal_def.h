/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Steven Liu <steven.liu@rock-chips.com>
 */

#ifndef _HAL_DEF_H_
#define _HAL_DEF_H_

#include "hal_os_def.h"

#define HAL_LOGLEVEL 3

#define __hal_print(level, fmt, ...)                                    \
({                                                                      \
    level < HAL_LOGLEVEL ? HAL_SYSLOG("[HAL] " fmt, ##__VA_ARGS__) : 0; \
})
#define HAL_ERR(fmt, ...)  __hal_print(0, "ERROR: " fmt, ##__VA_ARGS__)
#define HAL_WARN(fmt, ...) __hal_print(1, "WARN: " fmt, ##__VA_ARGS__)
#define HAL_MSG(fmt, ...)  __hal_print(2, fmt, ##__VA_ARGS__)
#define HAL_DBG(fmt, ...)  __hal_print(3, fmt, ##__VA_ARGS__)

#define HAL_NULL		((void *)0)
#define HAL_BIT(nr)		(1UL << (nr))

/***************************** Structure Definition **************************/
/** HAL boolean type definition */
typedef enum {
    HAL_FALSE = 0x00U,
    HAL_TRUE  = 0x01U
} HAL_Check;

/** HAL error code definition */
typedef enum {
    HAL_OK      = 0x00U,
    HAL_ERROR   = (-1),
    HAL_NOMEM   = (-12),
    HAL_BUSY    = (-16),
    HAL_NODEV   = (-19),
    HAL_INVAL   = (-22),
    HAL_NOSYS   = (-38),
    HAL_TIMEOUT = (-110)
} HAL_Status;

/** HAL functional status definition */
typedef enum {
    HAL_DISABLE = 0x00U,
    HAL_ENABLE  = 0x01U
} HAL_FuncStatus;

/** HAL lock structures definition */
typedef enum {
    HAL_UNLOCKED = 0x00U,
    HAL_LOCKED   = 0x01U
} HAL_LockStatus;

#endif /* _HAL_DEF_H_ */
