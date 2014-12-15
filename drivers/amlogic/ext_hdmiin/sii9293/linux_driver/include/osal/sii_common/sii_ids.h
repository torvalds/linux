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
 * @file sii_ids.h
 *
 * This file defines global identifiers and macros
 *
 * Don't use source control directives!
 * Don't use source control directives!
 * Don't use source control directives!
 *
 *****************************************************************************/
#ifndef _SII_IDS_H
#define _SII_IDS_H

/* Globally unique identifiers for related groups (libraries, layers, etc.) */
#define SII_GROUP_GENERAL  0x00
#define SII_GROUP_OSAL     0x01
#define SII_GROUP_NCL      0x02
#define SII_GROUP_APP      0x03

/* Macros for defining and testing status values */
#define SII_STATUS_GROUP_MASK     0x00FF0000
#define SII_STATUS_GROUP_SHIFT    16
#define SII_STATUS_LEVEL_MASK     0x0000FF00
#define SII_STATUS_LEVEL_SHIFT    8
#define SII_STATUS_VALUE_MASK     0x000000FF
#define SII_STATUS_VALUE_SHIFT    0

#define SII_STATUS_LEVEL_SUCCESS  0x00
#define SII_STATUS_LEVEL_WARNING  0x01
#define SII_STATUS_LEVEL_ERROR    0x02

#define SII_STATUS_VALUE(group, level, value)                           \
    ((((group) << SII_STATUS_GROUP_SHIFT) & SII_STATUS_GROUP_MASK) |    \
     (((level) << SII_STATUS_LEVEL_SHIFT) & SII_STATUS_LEVEL_MASK) |    \
     (((value) << SII_STATUS_VALUE_SHIFT) & SII_STATUS_VALUE_MASK))

#define SII_STATUS_SET_GROUP(group, value)                          \
    (((value) & ~(SII_STATUS_GROUP_MASK)) |                         \
     (((group) << SII_STATUS_GROUP_SHIFT) & SII_STATUS_GROUP_MASK))

#define SII_STATUS_GET_GROUP(value)                                 \
    (((value) & SII_STATUS_GROUP_MASK) >> SII_STATUS_GROUP_SHIFT)

#define SII_STATUS_GET_LEVEL(value)                                 \
    (((value) & SII_STATUS_LEVEL_MASK) >> SII_STATUS_LEVEL_SHIFT)


#define SII_STATUS_ISERROR(status)                                      \
    (((SII_STATUS_GET_LEVEL(status)) == SII_STATUS_LEVEL_ERROR) ? true : false )
#define SII_STATUS_ISWARNING(status)                                    \
    (((SII_STATUS_GET_LEVEL(status)) == SII_STATUS_LEVEL_WARNING) ? true : false )


/** @brief general return status values */
typedef enum
{
    /* Success */
    SII_STATUS_SUCCESS                = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_SUCCESS, 0x00),    /** success */
    SII_STATUS_SUCCESS_LAST,

    /* Warnings */
    SII_STATUS_WARN_PENDING           = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_WARNING, 0x01),   /** operation pending */
    SII_STATUS_WARN_BREAK             = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_WARNING, 0x02),   /** operation has been interrupted */
    SII_STATUS_WARN_INCOMPLETE        = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_WARNING, 0x03),   /** operation only partially completed */

    /* Custom warning values (to be redefined with meaningful names in other groups) */
    SII_STATUS_WARN_CUSTOM1           = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_WARNING, 0x81),   /** custom1 */
    SII_STATUS_WARN_CUSTOM2           = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_WARNING, 0x82),   /** custom2 */
    SII_STATUS_WARN_CUSTOM3           = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_WARNING, 0x83),   /** custom3 */
    SII_STATUS_WARN_CUSTOM4           = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_WARNING, 0x84),   /** custom4 */
    SII_STATUS_WARN_CUSTOM5           = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_WARNING, 0x85),   /** custom5 */
    SII_STATUS_WARN_LAST,

    /* Errors */
    SII_STATUS_ERR_INVALID_HANDLE     = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x00),  /** invalid handle */
    SII_STATUS_ERR_INVALID_PARAM      = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x01),  /** invalid parameter */
    SII_STATUS_ERR_INVALID_OP         = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x02),  /** invalid operation */
    SII_STATUS_ERR_NOT_AVAIL          = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x03),  /** resource not available */
    SII_STATUS_ERR_IN_USE             = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x04),  /** resource is in use */
    SII_STATUS_ERR_TIMEOUT            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x05),  /** timeout expired */
    SII_STATUS_ERR_FAILED             = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x07),  /** general failure */

    SII_STATUS_ERR_NOT_IMPLEMENTED    = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x41),  /** function not implemented (NOTE: for use in pre-production releases ONLY) */

    /* Custom error values (to be redefined with meaningful names in other groups) */
    SII_STATUS_ERR_CUSTOM1            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x81),  /** custom1 */
    SII_STATUS_ERR_CUSTOM2            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x82),  /** custom2 */
    SII_STATUS_ERR_CUSTOM3            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x83),  /** custom3 */
    SII_STATUS_ERR_CUSTOM4            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x84),  /** custom4 */
    SII_STATUS_ERR_CUSTOM5            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x85),  /** custom5 */
    SII_STATUS_ERR_CUSTOM6            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x86),  /** custom6 */
    SII_STATUS_ERR_CUSTOM7            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x87),  /** custom7 */
    SII_STATUS_ERR_CUSTOM8            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x88),  /** custom8 */
    SII_STATUS_ERR_CUSTOM9            = SII_STATUS_VALUE(SII_GROUP_GENERAL, SII_STATUS_LEVEL_ERROR, 0x89),  /** custom9 */
    SII_STATUS_ERR_LAST
} SiiStatus_t;


#endif /* _SII_IDS_H */
