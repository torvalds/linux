/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#pragma once


/* Declare the various hypercall operations. */

typedef enum _HV_CALL_CODE
{

    HvCallPostMessage                   = 0x005c,
    HvCallSignalEvent                   = 0x005d,

} HV_CALL_CODE, *PHV_CALL_CODE;

/* Definition of the HvPostMessage hypercall input structure. */


typedef struct _HV_INPUT_POST_MESSAGE
{
    HV_CONNECTION_ID    ConnectionId;
    u32              Reserved;
    HV_MESSAGE_TYPE     MessageType;
    u32              PayloadSize;
    u64              Payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
} HV_INPUT_POST_MESSAGE, *PHV_INPUT_POST_MESSAGE;



/* Definition of the HvSignalEvent hypercall input structure. */


typedef struct _HV_INPUT_SIGNAL_EVENT
{
    HV_CONNECTION_ID ConnectionId;
    u16           FlagNumber;
    u16           RsvdZ;
} HV_INPUT_SIGNAL_EVENT, *PHV_INPUT_SIGNAL_EVENT;
