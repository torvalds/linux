/* controlvmcompletionstatus.c
 *
 * Copyright © 2010 - 2013 UNISYS CORPORATION
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*  Defines for all valid values returned in the response message header
 *  completionStatus field.  See controlvmchannel.h for description of
 *  the header: _CONTROLVM_MESSAGE_HEADER.
 */

#ifndef __CONTROLVMCOMPLETIONSTATUS_H__
#define __CONTROLVMCOMPLETIONSTATUS_H__

/* General Errors------------------------------------------------------[0-99] */
#define CONTROLVM_RESP_SUCCESS                                  0
#define CONTROLVM_RESP_ERROR_ALREADY_DONE                       1
#define CONTROLVM_RESP_ERROR_IOREMAP_FAILED                     2
#define CONTROLVM_RESP_ERROR_KMALLOC_FAILED                     3
#define CONTROLVM_RESP_ERROR_MESSAGE_ID_UNKNOWN                 4
#define CONTROLVM_RESP_ERROR_MESSAGE_ID_INVALID_FOR_CLIENT      5

/* CONTROLVM_INIT_CHIPSET-------------------------------------------[100-199] */
#define CONTROLVM_RESP_ERROR_CLIENT_SWITCHCOUNT_NONZERO         100
#define CONTROLVM_RESP_ERROR_EXPECTED_CHIPSET_INIT              101

/* Maximum Limit----------------------------------------------------[200-299] */
#define CONTROLVM_RESP_ERROR_MAX_BUSES		201	/* BUS_CREATE */
#define CONTROLVM_RESP_ERROR_MAX_DEVICES        202	/* DEVICE_CREATE */
/* Payload and Parameter Related------------------------------------[400-499] */
#define CONTROLVM_RESP_ERROR_PAYLOAD_INVALID	400	/* SWITCH_ATTACHEXTPORT,
							 * DEVICE_CONFIGURE */
#define CONTROLVM_RESP_ERROR_INITIATOR_PARAMETER_INVALID 401	/* Multiple */
#define CONTROLVM_RESP_ERROR_TARGET_PARAMETER_INVALID 402 /* DEVICE_CONFIGURE */
#define CONTROLVM_RESP_ERROR_CLIENT_PARAMETER_INVALID 403 /* DEVICE_CONFIGURE */
/* Specified[Packet Structure] Value-------------------------------[500-599] */
#define CONTROLVM_RESP_ERROR_BUS_INVALID	500	/* SWITCH_ATTACHINTPORT,
							 * BUS_CONFIGURE,
							 * DEVICE_CREATE,
							 * DEVICE_CONFIG
							 * DEVICE_DESTROY */
#define CONTROLVM_RESP_ERROR_DEVICE_INVALID	501 /* SWITCH_ATTACHINTPORT */
						    /* DEVICE_CREATE,
						     * DEVICE_CONFIGURE,
						     * DEVICE_DESTROY */
#define CONTROLVM_RESP_ERROR_CHANNEL_INVALID	502 /* DEVICE_CREATE,
						     * DEVICE_CONFIGURE */
/* Partition Driver Callback Interface----------------------[600-699] */
#define CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_FAILURE 604	/* BUS_CREATE,
							 * BUS_DESTROY,
							 * DEVICE_CREATE,
							 * DEVICE_DESTROY */
/* Unable to invoke VIRTPCI callback */
#define CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_CALLBACK_ERROR 605 /* BUS_CREATE,
								* BUS_DESTROY,
								* DEVICE_CREATE,
								* DEVICE_DESTROY */
/* VIRTPCI Callback returned error */
#define CONTROLVM_RESP_ERROR_GENERIC_DRIVER_CALLBACK_ERROR 606 /* SWITCH_ATTACHEXTPORT,
								* SWITCH_DETACHEXTPORT
								* DEVICE_CONFIGURE */

/* generic device callback returned error */
/* Bus Related------------------------------------------------------[700-799] */
#define CONTROLVM_RESP_ERROR_BUS_DEVICE_ATTACHED 700	/* BUS_DESTROY */
/* Channel Related--------------------------------------------------[800-899] */
#define CONTROLVM_RESP_ERROR_CHANNEL_TYPE_UNKNOWN 800	/* GET_CHANNELINFO,
							 * DEVICE_DESTROY */
#define CONTROLVM_RESP_ERROR_CHANNEL_SIZE_TOO_SMALL 801	/* DEVICE_CREATE */
/* Chipset Shutdown Related---------------------------------------[1000-1099] */
#define CONTROLVM_RESP_ERROR_CHIPSET_SHUTDOWN_FAILED            1000
#define CONTROLVM_RESP_ERROR_CHIPSET_SHUTDOWN_ALREADY_ACTIVE    1001

/* Chipset Stop Related-------------------------------------------[1100-1199] */
#define CONTROLVM_RESP_ERROR_CHIPSET_STOP_FAILED_BUS            1100
#define CONTROLVM_RESP_ERROR_CHIPSET_STOP_FAILED_SWITCH         1101

/* Device Related-------------------------------------------------[1400-1499] */
#define CONTROLVM_RESP_ERROR_DEVICE_UDEV_TIMEOUT                1400

#endif /* __CONTROLVMCOMPLETIONSTATUS_H__ not defined */
