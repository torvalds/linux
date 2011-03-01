/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.
 $
 */
/******************************************************************************
 *
 * $Id: mltypes.h 4598 2011-01-25 19:33:13Z prao $
 *
 *****************************************************************************/

/**
 *  @defgroup MLERROR
 *  @brief  Motion Library - Error definitions.
 *          Definition of the error codes used within the MPL and returned
 *          to the user.
 *          Every function tries to return a meaningful error code basing
 *          on the occuring error condition. The error code is numeric.
 *
 *          The available error codes and their associated values are:
 *          - (0)       ML_SUCCESS
 *          - (1)       ML_ERROR
 *          - (2)       ML_ERROR_INVALID_PARAMETER
 *          - (3)       ML_ERROR_FEATURE_NOT_ENABLED
 *          - (4)       ML_ERROR_FEATURE_NOT_IMPLEMENTED
 *          - (6)       ML_ERROR_DMP_NOT_STARTED
 *          - (7)       ML_ERROR_DMP_STARTED
 *          - (8)       ML_ERROR_NOT_OPENED
 *          - (9)       ML_ERROR_OPENED
 *          - (10)      ML_ERROR_INVALID_MODULE
 *          - (11)      ML_ERROR_MEMORY_EXAUSTED
 *          - (12)      ML_ERROR_DIVIDE_BY_ZERO
 *          - (13)      ML_ERROR_ASSERTION_FAILURE
 *          - (14)      ML_ERROR_FILE_OPEN
 *          - (15)      ML_ERROR_FILE_READ
 *          - (16)      ML_ERROR_FILE_WRITE
 *          - (20)      ML_ERROR_SERIAL_CLOSED
 *          - (21)      ML_ERROR_SERIAL_OPEN_ERROR
 *          - (22)      ML_ERROR_SERIAL_READ
 *          - (23)      ML_ERROR_SERIAL_WRITE
 *          - (24)      ML_ERROR_SERIAL_DEVICE_NOT_RECOGNIZED
 *          - (25)      ML_ERROR_SM_TRANSITION
 *          - (26)      ML_ERROR_SM_IMPROPER_STATE
 *          - (30)      ML_ERROR_FIFO_OVERFLOW
 *          - (31)      ML_ERROR_FIFO_FOOTER
 *          - (32)      ML_ERROR_FIFO_READ_COUNT
 *          - (33)      ML_ERROR_FIFO_READ_DATA
 *          - (40)      ML_ERROR_MEMORY_SET
 *          - (50)      ML_ERROR_LOG_MEMORY_ERROR
 *          - (51)      ML_ERROR_LOG_OUTPUT_ERROR
 *          - (60)      ML_ERROR_OS_BAD_PTR
 *          - (61)      ML_ERROR_OS_BAD_HANDLE
 *          - (62)      ML_ERROR_OS_CREATE_FAILED
 *          - (63)      ML_ERROR_OS_LOCK_FAILED
 *          - (70)      ML_ERROR_COMPASS_DATA_OVERFLOW
 *          - (71)      ML_ERROR_COMPASS_DATA_UNDERFLOW
 *          - (72)      ML_ERROR_COMPASS_DATA_NOT_READY
 *          - (73)      ML_ERROR_COMPASS_DATA_ERROR
 *          - (75)      ML_ERROR_CALIBRATION_LOAD
 *          - (76)      ML_ERROR_CALIBRATION_STORE
 *          - (77)      ML_ERROR_CALIBRATION_LEN
 *          - (78)      ML_ERROR_CALIBRATION_CHECKSUM
 *
 *  @{
 *      @file mltypes.h
 *  @}
 */

#ifndef MLTYPES_H
#define MLTYPES_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include "stdint_invensense.h"
#endif
#include "log.h"

/*---------------------------
    ML Types
---------------------------*/

/**
 * @struct tMLError The MPL Error Code return type.
 *
 * @code
 *      typedef unsigned char tMLError;
 * @endcode
 */
typedef unsigned char tMLError;

#if defined(LINUX) || defined(__KERNEL__)
typedef unsigned int HANDLE;
#endif

#ifdef __KERNEL__
typedef HANDLE FILE;
#endif

#ifndef __cplusplus
#ifndef __KERNEL__
typedef int_fast8_t bool;
#endif
#endif

/*---------------------------
    ML Defines
---------------------------*/

#ifndef NULL
#define NULL 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* Dimension of an array */
#ifndef DIM
#define DIM(array) (sizeof(array)/sizeof((array)[0]))
#endif

/* - ML Errors. - */
#define ERROR_NAME(x)   (#x)
#define ERROR_CHECK(x)                                                  \
	{								\
		if (ML_SUCCESS != x) {					\
			MPL_LOGE("%s|%s|%d returning %d\n",		\
				__FILE__, __func__, __LINE__, x);	\
			return x;					\
		}							\
	}

#define ERROR_CHECK_FIRST(first, x)                                     \
	{ if (ML_SUCCESS == first) first = x; }

#define ML_SUCCESS                       (0)
/* Generic Error code.  Proprietary Error Codes only */
#define ML_ERROR                         (1)

/* Compatibility and other generic error codes */
#define ML_ERROR_INVALID_PARAMETER       (2)
#define ML_ERROR_FEATURE_NOT_ENABLED     (3)
#define ML_ERROR_FEATURE_NOT_IMPLEMENTED (4)
#define ML_ERROR_DMP_NOT_STARTED         (6)
#define ML_ERROR_DMP_STARTED             (7)
#define ML_ERROR_NOT_OPENED              (8)
#define ML_ERROR_OPENED                  (9)
#define ML_ERROR_INVALID_MODULE         (10)
#define ML_ERROR_MEMORY_EXAUSTED        (11)
#define ML_ERROR_DIVIDE_BY_ZERO         (12)
#define ML_ERROR_ASSERTION_FAILURE      (13)
#define ML_ERROR_FILE_OPEN              (14)
#define ML_ERROR_FILE_READ              (15)
#define ML_ERROR_FILE_WRITE             (16)
#define ML_ERROR_INVALID_CONFIGURATION  (17)

/* Serial Communication */
#define ML_ERROR_SERIAL_CLOSED          (20)
#define ML_ERROR_SERIAL_OPEN_ERROR      (21)
#define ML_ERROR_SERIAL_READ            (22)
#define ML_ERROR_SERIAL_WRITE           (23)
#define ML_ERROR_SERIAL_DEVICE_NOT_RECOGNIZED  (24)

/* SM = State Machine */
#define ML_ERROR_SM_TRANSITION          (25)
#define ML_ERROR_SM_IMPROPER_STATE      (26)

/* Fifo */
#define ML_ERROR_FIFO_OVERFLOW          (30)
#define ML_ERROR_FIFO_FOOTER            (31)
#define ML_ERROR_FIFO_READ_COUNT        (32)
#define ML_ERROR_FIFO_READ_DATA         (33)

/* Memory & Registers, Set & Get */
#define ML_ERROR_MEMORY_SET             (40)

#define ML_ERROR_LOG_MEMORY_ERROR       (50)
#define ML_ERROR_LOG_OUTPUT_ERROR       (51)

/* OS interface errors */
#define ML_ERROR_OS_BAD_PTR             (60)
#define ML_ERROR_OS_BAD_HANDLE          (61)
#define ML_ERROR_OS_CREATE_FAILED       (62)
#define ML_ERROR_OS_LOCK_FAILED         (63)

/* Compass errors */
#define ML_ERROR_COMPASS_DATA_OVERFLOW  (70)
#define ML_ERROR_COMPASS_DATA_UNDERFLOW (71)
#define ML_ERROR_COMPASS_DATA_NOT_READY (72)
#define ML_ERROR_COMPASS_DATA_ERROR     (73)

/* Load/Store calibration */
#define ML_ERROR_CALIBRATION_LOAD       (75)
#define ML_ERROR_CALIBRATION_STORE      (76)
#define ML_ERROR_CALIBRATION_LEN        (77)
#define ML_ERROR_CALIBRATION_CHECKSUM   (78)

/* For Linux coding compliance */
#ifndef __KERNEL__
#define EXPORT_SYMBOL(x)
#endif

/*---------------------------
    p-Types
---------------------------*/

#endif				/* MLTYPES_H */
