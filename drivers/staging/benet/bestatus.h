/*
 * Copyright (C) 2005 - 2008 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 */
#ifndef _BESTATUS_H_
#define _BESTATUS_H_

#define BE_SUCCESS                      (0x00000000L)
/*
 * MessageId: BE_PENDING
 *  The BladeEngine Driver call succeeded, and pended operation.
 */
#define BE_PENDING                       (0x20070001L)
#define BE_STATUS_PENDING                (BE_PENDING)
/*
 * MessageId: BE_NOT_OK
 *  An error occurred.
 */
#define BE_NOT_OK                        (0xE0070002L)
/*
 * MessageId: BE_STATUS_SYSTEM_RESOURCES
 *  Insufficient host system resources exist to complete the API.
 */
#define BE_STATUS_SYSTEM_RESOURCES       (0xE0070003L)
/*
 * MessageId: BE_STATUS_CHIP_RESOURCES
 *  Insufficient chip resources exist to complete the API.
 */
#define BE_STATUS_CHIP_RESOURCES         (0xE0070004L)
/*
 * MessageId: BE_STATUS_NO_RESOURCE
 *  Insufficient resources to complete request.
 */
#define BE_STATUS_NO_RESOURCE            (0xE0070005L)
/*
 * MessageId: BE_STATUS_BUSY
 *  Resource is currently busy.
 */
#define BE_STATUS_BUSY                   (0xE0070006L)
/*
 * MessageId: BE_STATUS_INVALID_PARAMETER
 *  Invalid Parameter in request.
 */
#define BE_STATUS_INVALID_PARAMETER      (0xE0000007L)
/*
 * MessageId: BE_STATUS_NOT_SUPPORTED
 *  Requested operation is not supported.
 */
#define BE_STATUS_NOT_SUPPORTED          (0xE000000DL)

/*
 * ***************************************************************************
 *                     E T H E R N E T   S T A T U S
 * ***************************************************************************
 */

/*
 * MessageId: BE_ETH_TX_ERROR
 *  The Ethernet device driver failed to transmit a packet.
 */
#define BE_ETH_TX_ERROR                  (0xE0070101L)

/*
 * ***************************************************************************
 *                     S H A R E D   S T A T U S
 * ***************************************************************************
 */

/*
 * MessageId: BE_STATUS_VBD_INVALID_VERSION
 *  The device driver is not compatible with this version of the VBD.
 */
#define BE_STATUS_INVALID_VERSION    (0xE0070402L)
/*
 * MessageId: BE_STATUS_DOMAIN_DENIED
 *  The operation failed to complete due to insufficient access
 *  rights for the requesting domain.
 */
#define BE_STATUS_DOMAIN_DENIED          (0xE0070403L)
/*
 * MessageId: BE_STATUS_TCP_NOT_STARTED
 *  The embedded TCP/IP stack has not been started.
 */
#define BE_STATUS_TCP_NOT_STARTED        (0xE0070409L)
/*
 * MessageId: BE_STATUS_NO_MCC_WRB
 *  No free MCC WRB are available for posting the request.
 */
#define BE_STATUS_NO_MCC_WRB                 (0xE0070414L)

#endif /* _BESTATUS_ */
