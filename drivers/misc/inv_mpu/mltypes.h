/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */

/**
 *  @defgroup MLERROR
 *  @brief  Definition of the error codes used within the MPL and
 *          returned to the user.
 *          Every function tries to return a meaningful error code basing
 *          on the occuring error condition. The error code is numeric.
 *
 *          The available error codes and their associated values are:
 *          - (0)               INV_SUCCESS
 *          - (32)              INV_ERROR
 *          - (22 / EINVAL)     INV_ERROR_INVALID_PARAMETER
 *          - (1  / EPERM)      INV_ERROR_FEATURE_NOT_ENABLED
 *          - (36)              INV_ERROR_FEATURE_NOT_IMPLEMENTED
 *          - (38)              INV_ERROR_DMP_NOT_STARTED
 *          - (39)              INV_ERROR_DMP_STARTED
 *          - (40)              INV_ERROR_NOT_OPENED
 *          - (41)              INV_ERROR_OPENED
 *          - (19 / ENODEV)     INV_ERROR_INVALID_MODULE
 *          - (12 / ENOMEM)     INV_ERROR_MEMORY_EXAUSTED
 *          - (44)              INV_ERROR_DIVIDE_BY_ZERO
 *          - (45)              INV_ERROR_ASSERTION_FAILURE
 *          - (46)              INV_ERROR_FILE_OPEN
 *          - (47)              INV_ERROR_FILE_READ
 *          - (48)              INV_ERROR_FILE_WRITE
 *          - (49)              INV_ERROR_INVALID_CONFIGURATION
 *          - (52)              INV_ERROR_SERIAL_CLOSED
 *          - (53)              INV_ERROR_SERIAL_OPEN_ERROR
 *          - (54)              INV_ERROR_SERIAL_READ
 *          - (55)              INV_ERROR_SERIAL_WRITE
 *          - (56)              INV_ERROR_SERIAL_DEVICE_NOT_RECOGNIZED
 *          - (57)              INV_ERROR_SM_TRANSITION
 *          - (58)              INV_ERROR_SM_IMPROPER_STATE
 *          - (62)              INV_ERROR_FIFO_OVERFLOW
 *          - (63)              INV_ERROR_FIFO_FOOTER
 *          - (64)              INV_ERROR_FIFO_READ_COUNT
 *          - (65)              INV_ERROR_FIFO_READ_DATA
 *          - (72)              INV_ERROR_MEMORY_SET
 *          - (82)              INV_ERROR_LOG_MEMORY_ERROR
 *          - (83)              INV_ERROR_LOG_OUTPUT_ERROR
 *          - (92)              INV_ERROR_OS_BAD_PTR
 *          - (93)              INV_ERROR_OS_BAD_HANDLE
 *          - (94)              INV_ERROR_OS_CREATE_FAILED
 *          - (95)              INV_ERROR_OS_LOCK_FAILED
 *          - (102)             INV_ERROR_COMPASS_DATA_OVERFLOW
 *          - (103)             INV_ERROR_COMPASS_DATA_UNDERFLOW
 *          - (104)             INV_ERROR_COMPASS_DATA_NOT_READY
 *          - (105)             INV_ERROR_COMPASS_DATA_ERROR
 *          - (107)             INV_ERROR_CALIBRATION_LOAD
 *          - (108)             INV_ERROR_CALIBRATION_STORE
 *          - (109)             INV_ERROR_CALIBRATION_LEN
 *          - (110)             INV_ERROR_CALIBRATION_CHECKSUM
 *          - (111)             INV_ERROR_ACCEL_DATA_OVERFLOW
 *          - (112)             INV_ERROR_ACCEL_DATA_UNDERFLOW
 *          - (113)             INV_ERROR_ACCEL_DATA_NOT_READY
 *          - (114)             INV_ERROR_ACCEL_DATA_ERROR
 *
 *          The available warning codes and their associated values are:
 *          - (115)             INV_WARNING_MOTION_RACE
 *          - (116)             INV_WARNING_QUAT_TRASHED
 *
 *  @{
 *      @file mltypes.h
 *  @}
 */

#ifndef MLTYPES_H
#define MLTYPES_H

#include <linux/types.h>
#include <asm-generic/errno-base.h>




/*---------------------------
 *    ML Defines
 *--------------------------*/

#ifndef NULL
#define NULL 0
#endif

/* - ML Errors. - */
#define ERROR_NAME(x)   (#x)
#define ERROR_CHECK_FIRST(first, x) \
	{ if (INV_SUCCESS == first) first = x; }

#define INV_SUCCESS                       (0)
/* Generic Error code.  Proprietary Error Codes only */
#define INV_ERROR_BASE                    (0x20)
#define INV_ERROR                         (INV_ERROR_BASE)

/* Compatibility and other generic error codes */
#define INV_ERROR_INVALID_PARAMETER             (EINVAL)
#define INV_ERROR_FEATURE_NOT_ENABLED           (EPERM)
#define INV_ERROR_FEATURE_NOT_IMPLEMENTED       (INV_ERROR_BASE + 4)
#define INV_ERROR_DMP_NOT_STARTED               (INV_ERROR_BASE + 6)
#define INV_ERROR_DMP_STARTED                   (INV_ERROR_BASE + 7)
#define INV_ERROR_NOT_OPENED                    (INV_ERROR_BASE + 8)
#define INV_ERROR_OPENED                        (INV_ERROR_BASE + 9)
#define INV_ERROR_INVALID_MODULE                (ENODEV)
#define INV_ERROR_MEMORY_EXAUSTED               (ENOMEM)
#define INV_ERROR_DIVIDE_BY_ZERO                (INV_ERROR_BASE + 12)
#define INV_ERROR_ASSERTION_FAILURE             (INV_ERROR_BASE + 13)
#define INV_ERROR_FILE_OPEN                     (INV_ERROR_BASE + 14)
#define INV_ERROR_FILE_READ                     (INV_ERROR_BASE + 15)
#define INV_ERROR_FILE_WRITE                    (INV_ERROR_BASE + 16)
#define INV_ERROR_INVALID_CONFIGURATION         (INV_ERROR_BASE + 17)

/* Serial Communication */
#define INV_ERROR_SERIAL_CLOSED                 (INV_ERROR_BASE + 20)
#define INV_ERROR_SERIAL_OPEN_ERROR             (INV_ERROR_BASE + 21)
#define INV_ERROR_SERIAL_READ                   (INV_ERROR_BASE + 22)
#define INV_ERROR_SERIAL_WRITE                  (INV_ERROR_BASE + 23)
#define INV_ERROR_SERIAL_DEVICE_NOT_RECOGNIZED  (INV_ERROR_BASE + 24)

/* SM = State Machine */
#define INV_ERROR_SM_TRANSITION                 (INV_ERROR_BASE + 25)
#define INV_ERROR_SM_IMPROPER_STATE             (INV_ERROR_BASE + 26)

/* Fifo */
#define INV_ERROR_FIFO_OVERFLOW                 (INV_ERROR_BASE + 30)
#define INV_ERROR_FIFO_FOOTER                   (INV_ERROR_BASE + 31)
#define INV_ERROR_FIFO_READ_COUNT               (INV_ERROR_BASE + 32)
#define INV_ERROR_FIFO_READ_DATA                (INV_ERROR_BASE + 33)

/* Memory & Registers, Set & Get */
#define INV_ERROR_MEMORY_SET                    (INV_ERROR_BASE + 40)

#define INV_ERROR_LOG_MEMORY_ERROR              (INV_ERROR_BASE + 50)
#define INV_ERROR_LOG_OUTPUT_ERROR              (INV_ERROR_BASE + 51)

/* OS interface errors */
#define INV_ERROR_OS_BAD_PTR                    (INV_ERROR_BASE + 60)
#define INV_ERROR_OS_BAD_HANDLE                 (INV_ERROR_BASE + 61)
#define INV_ERROR_OS_CREATE_FAILED              (INV_ERROR_BASE + 62)
#define INV_ERROR_OS_LOCK_FAILED                (INV_ERROR_BASE + 63)

/* Compass errors */
#define INV_ERROR_COMPASS_DATA_OVERFLOW         (INV_ERROR_BASE + 70)
#define INV_ERROR_COMPASS_DATA_UNDERFLOW        (INV_ERROR_BASE + 71)
#define INV_ERROR_COMPASS_DATA_NOT_READY        (INV_ERROR_BASE + 72)
#define INV_ERROR_COMPASS_DATA_ERROR            (INV_ERROR_BASE + 73)

/* Load/Store calibration */
#define INV_ERROR_CALIBRATION_LOAD              (INV_ERROR_BASE + 75)
#define INV_ERROR_CALIBRATION_STORE             (INV_ERROR_BASE + 76)
#define INV_ERROR_CALIBRATION_LEN               (INV_ERROR_BASE + 77)
#define INV_ERROR_CALIBRATION_CHECKSUM          (INV_ERROR_BASE + 78)

/* Accel errors */
#define INV_ERROR_ACCEL_DATA_OVERFLOW           (INV_ERROR_BASE + 79)
#define INV_ERROR_ACCEL_DATA_UNDERFLOW          (INV_ERROR_BASE + 80)
#define INV_ERROR_ACCEL_DATA_NOT_READY          (INV_ERROR_BASE + 81)
#define INV_ERROR_ACCEL_DATA_ERROR              (INV_ERROR_BASE + 82)

/* No Motion Warning States */
#define INV_WARNING_MOTION_RACE                 (INV_ERROR_BASE + 83)
#define INV_WARNING_QUAT_TRASHED                (INV_ERROR_BASE + 84)
#define INV_WARNING_GYRO_MAG                    (INV_ERROR_BASE + 85)

#ifdef INV_USE_LEGACY_NAMES
#define ML_SUCCESS                        INV_SUCCESS
#define ML_ERROR                          INV_ERROR
#define ML_ERROR_INVALID_PARAMETER        INV_ERROR_INVALID_PARAMETER
#define ML_ERROR_FEATURE_NOT_ENABLED      INV_ERROR_FEATURE_NOT_ENABLED
#define ML_ERROR_FEATURE_NOT_IMPLEMENTED  INV_ERROR_FEATURE_NOT_IMPLEMENTED
#define ML_ERROR_DMP_NOT_STARTED          INV_ERROR_DMP_NOT_STARTED
#define ML_ERROR_DMP_STARTED              INV_ERROR_DMP_STARTED
#define ML_ERROR_NOT_OPENED               INV_ERROR_NOT_OPENED
#define ML_ERROR_OPENED                   INV_ERROR_OPENED
#define ML_ERROR_INVALID_MODULE           INV_ERROR_INVALID_MODULE
#define ML_ERROR_MEMORY_EXAUSTED          INV_ERROR_MEMORY_EXAUSTED
#define ML_ERROR_DIVIDE_BY_ZERO           INV_ERROR_DIVIDE_BY_ZERO
#define ML_ERROR_ASSERTION_FAILURE        INV_ERROR_ASSERTION_FAILURE
#define ML_ERROR_FILE_OPEN                INV_ERROR_FILE_OPEN
#define ML_ERROR_FILE_READ                INV_ERROR_FILE_READ
#define ML_ERROR_FILE_WRITE               INV_ERROR_FILE_WRITE
#define ML_ERROR_INVALID_CONFIGURATION    INV_ERROR_INVALID_CONFIGURATION
#define ML_ERROR_SERIAL_CLOSED            INV_ERROR_SERIAL_CLOSED
#define ML_ERROR_SERIAL_OPEN_ERROR        INV_ERROR_SERIAL_OPEN_ERROR
#define ML_ERROR_SERIAL_READ              INV_ERROR_SERIAL_READ
#define ML_ERROR_SERIAL_WRITE             INV_ERROR_SERIAL_WRITE
#define ML_ERROR_SERIAL_DEVICE_NOT_RECOGNIZED  \
	INV_ERROR_SERIAL_DEVICE_NOT_RECOGNIZED
#define ML_ERROR_SM_TRANSITION            INV_ERROR_SM_TRANSITION
#define ML_ERROR_SM_IMPROPER_STATE        INV_ERROR_SM_IMPROPER_STATE
#define ML_ERROR_FIFO_OVERFLOW            INV_ERROR_FIFO_OVERFLOW
#define ML_ERROR_FIFO_FOOTER              INV_ERROR_FIFO_FOOTER
#define ML_ERROR_FIFO_READ_COUNT          INV_ERROR_FIFO_READ_COUNT
#define ML_ERROR_FIFO_READ_DATA           INV_ERROR_FIFO_READ_DATA
#define ML_ERROR_MEMORY_SET               INV_ERROR_MEMORY_SET
#define ML_ERROR_LOG_MEMORY_ERROR         INV_ERROR_LOG_MEMORY_ERROR
#define ML_ERROR_LOG_OUTPUT_ERROR         INV_ERROR_LOG_OUTPUT_ERROR
#define ML_ERROR_OS_BAD_PTR               INV_ERROR_OS_BAD_PTR
#define ML_ERROR_OS_BAD_HANDLE            INV_ERROR_OS_BAD_HANDLE
#define ML_ERROR_OS_CREATE_FAILED         INV_ERROR_OS_CREATE_FAILED
#define ML_ERROR_OS_LOCK_FAILED           INV_ERROR_OS_LOCK_FAILED
#define ML_ERROR_COMPASS_DATA_OVERFLOW    INV_ERROR_COMPASS_DATA_OVERFLOW
#define ML_ERROR_COMPASS_DATA_UNDERFLOW   INV_ERROR_COMPASS_DATA_UNDERFLOW
#define ML_ERROR_COMPASS_DATA_NOT_READY   INV_ERROR_COMPASS_DATA_NOT_READY
#define ML_ERROR_COMPASS_DATA_ERROR       INV_ERROR_COMPASS_DATA_ERROR
#define ML_ERROR_CALIBRATION_LOAD         INV_ERROR_CALIBRATION_LOAD
#define ML_ERROR_CALIBRATION_STORE        INV_ERROR_CALIBRATION_STORE
#define ML_ERROR_CALIBRATION_LEN          INV_ERROR_CALIBRATION_LEN
#define ML_ERROR_CALIBRATION_CHECKSUM     INV_ERROR_CALIBRATION_CHECKSUM
#define ML_ERROR_ACCEL_DATA_OVERFLOW      INV_ERROR_ACCEL_DATA_OVERFLOW
#define ML_ERROR_ACCEL_DATA_UNDERFLOW     INV_ERROR_ACCEL_DATA_UNDERFLOW
#define ML_ERROR_ACCEL_DATA_NOT_READY     INV_ERROR_ACCEL_DATA_NOT_READY
#define ML_ERROR_ACCEL_DATA_ERROR         INV_ERROR_ACCEL_DATA_ERROR
#endif

/* For Linux coding compliance */

#endif				/* MLTYPES_H */
