/*
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU General Public License for more details.
*/
/**
 * @file os_types.h
 *
 * Data type definitions for the OSAL layer (internal)
 *
 * Don't use source control directives!
 * Don't use source control directives!
 * Don't use source control directives!
 *
 *****************************************************************************/

#ifndef _OS_TYPES_H
#define _OS_TYPES_H

#if !defined(__KERNEL__)
#include <sys/time.h>
/*lint -save -e104 -e19*/
#include <sys/uio.h>
/*lint -restore*/

#include "osal/sii_common/sii_types.h"
#endif

#include "osal/sii_common/sii_ids.h"
#include "osal/sii_common/sii_inline.h"


#define OS_NO_WAIT          0
#define OS_INFINITE_WAIT    -1
#define OS_MAX_TIMEOUT      (1000*60*60*24) /* 24 hours expressed in milliseconds. This can be increased depending on OS support */

typedef struct timeval SiiOsTimeVal_t;
typedef struct iovec SiiOsIoVec_t;
typedef uint32_t SiiOsIpAddr_t;

/** @brief return status codes */
typedef enum
{
    /* Success */
    SII_OS_STATUS_SUCCESS                = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_SUCCESS),             /** success */

    /* Warnings */
    SII_OS_STATUS_WARN_PENDING           = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_WARN_PENDING),        /** operation pending */
    SII_OS_STATUS_WARN_BREAK             = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_WARN_BREAK),          /** operation has been interrupted */
    SII_OS_STATUS_WARN_INCOMPLETE        = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_WARN_INCOMPLETE),     /** operation only partially completed */

    /* Errors */
    SII_OS_STATUS_ERR_INVALID_HANDLE     = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_INVALID_HANDLE),  /** invalid handle */
    SII_OS_STATUS_ERR_INVALID_PARAM      = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_INVALID_PARAM),   /** invalid parameter */
    SII_OS_STATUS_ERR_INVALID_OP         = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_INVALID_OP),      /** invalid operation */
    SII_OS_STATUS_ERR_NOT_AVAIL          = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_NOT_AVAIL),       /** requested resource not available */
    SII_OS_STATUS_ERR_IN_USE             = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_IN_USE),          /** requested resource is in use */
    SII_OS_STATUS_ERR_TIMEOUT            = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_TIMEOUT),         /** timeout expired */
    SII_OS_STATUS_ERR_FAILED             = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_FAILED),          /** general failure */
    SII_OS_STATUS_ERR_NOT_IMPLEMENTED    = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_NOT_IMPLEMENTED), /** function not implemented (NOTE: for use in pre-production releases ONLY) */

    SII_OS_STATUS_ERR_SEM_COUNT_EXCEEDED = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_CUSTOM1),         /** semaphore-give was called more times than the specific max count of the semaphore */
    SII_OS_STATUS_ERR_QUEUE_EMPTY        = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_CUSTOM2),         /** message queue empty */
    SII_OS_STATUS_ERR_QUEUE_FULL         = SII_STATUS_SET_GROUP(SII_GROUP_OSAL, SII_STATUS_ERR_CUSTOM3),         /** message queue full */
    SII_OS_STATUS_LAST
} SiiOsStatus_t;

static SII_INLINE const char * SiiOsStatusString(const SiiOsStatus_t status)
{
    switch (status)
    {
        case SII_OS_STATUS_SUCCESS:
            return "Success";

        case SII_OS_STATUS_WARN_PENDING:
            return "Warning-Operation Pending";
        case SII_OS_STATUS_WARN_BREAK:
            return "Warning-Break";
        case SII_OS_STATUS_WARN_INCOMPLETE:
            return "Warning-Operation Incomplete";

        case SII_OS_STATUS_ERR_INVALID_HANDLE:
            return "Error-Invalid Handle";
        case SII_OS_STATUS_ERR_INVALID_PARAM:
            return "Error-Invalid Parameter";
        case SII_OS_STATUS_ERR_INVALID_OP:
            return "Error-Invalid Operation";
        case SII_OS_STATUS_ERR_NOT_AVAIL:
            return "Error-Resource Not Available";
        case SII_OS_STATUS_ERR_IN_USE:
            return "Error-Resource In Use";
        case SII_OS_STATUS_ERR_TIMEOUT:
            return "Error-Timeout Expired";
        case SII_OS_STATUS_ERR_FAILED:
            return "Error-General Failure";
        case SII_OS_STATUS_ERR_NOT_IMPLEMENTED:
            return "Error-Not Implemented";
        case SII_OS_STATUS_ERR_SEM_COUNT_EXCEEDED:
            return "Error-Semaphore Count Exceeded";
        case SII_OS_STATUS_ERR_QUEUE_EMPTY:
            return "Error-Queue Empty";
		case SII_OS_STATUS_ERR_QUEUE_FULL:
			return "Error-Queue Full";	

        default:
            return "UNKNOWN";
    }
}


#endif /* _OS_TYPES_H */

