/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics IIO custom types
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#ifndef __STM_IIO_CUSTOM_TYPE__
#define __STM_IIO_CUSTOM_TYPE__

/* Linux IIO driver custom types */
enum {
	STM_IIO_LAST = 0x3f,
	STM_IIO_SIGN_MOTION = STM_IIO_LAST - 6,
	STM_IIO_STEP_COUNTER = STM_IIO_LAST - 5,
	STM_IIO_TILT = STM_IIO_LAST - 4,
	STM_IIO_TAP = STM_IIO_LAST - 3,
	STM_IIO_TAP_TAP = STM_IIO_LAST - 2,
	STM_IIO_WRIST_TILT_GESTURE = STM_IIO_LAST - 1,
	STM_IIO_GESTURE = STM_IIO_LAST,
};

enum {
	STM_IIO_EV_DIR_LAST = 0x1f,
	STM_IIO_EV_DIR_FIFO_EMPTY = STM_IIO_EV_DIR_LAST - 1,
	STM_IIO_EV_DIR_FIFO_DATA = STM_IIO_EV_DIR_LAST,
};

enum {
	STM_IIO_EV_TYPE_LAST = 0x1f,
	STM_IIO_EV_TYPE_FIFO_FLUSH = STM_IIO_EV_TYPE_LAST - 1,
	STM_IIO_EV_TYPE_TIME_SYNC = STM_IIO_EV_TYPE_LAST,
};

#endif /* __STM_IIO_CUSTOM_TYPE__ */

