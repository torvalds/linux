/* Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __VBUSCHANNEL_H__
#define __VBUSCHANNEL_H__

/*  The vbus channel is the channel area provided via the BUS_CREATE controlvm
 *  message for each virtual bus.  This channel area is provided to both server
 *  and client ends of the bus.  The channel header area is initialized by
 *  the server, and the remaining information is filled in by the client.
 *  We currently use this for the client to provide various information about
 *  the client devices and client drivers for the server end to see.
 */
#include <linux/uuid.h>
#include <linux/ctype.h>
#include "channel.h"

/* {193b331b-c58f-11da-95a9-00e08161165f} */
#define VISOR_VBUS_CHANNEL_UUID \
	UUID_LE(0x193b331b, 0xc58f, 0x11da, \
		0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)
static const uuid_le visor_vbus_channel_uuid = VISOR_VBUS_CHANNEL_UUID;

#define VISOR_VBUS_CHANNEL_SIGNATURE VISOR_CHANNEL_SIGNATURE

/* Must increment this whenever you insert or delete fields within this channel
 * struct.  Also increment whenever you change the meaning of fields within this
 * channel struct so as to break pre-existing software.  Note that you can
 * usually add fields to the END of the channel struct withOUT needing to
 * increment this.
 */
#define VISOR_VBUS_CHANNEL_VERSIONID 1

/*
 * An array of this struct is present in the channel area for each vbus.
 * (See vbuschannel.h.)
 * It is filled in by the client side to provide info about the device
 * and driver from the client's perspective.
 */
struct visor_vbus_deviceinfo {
	u8 devtype[16];		/* short string identifying the device type */
	u8 drvname[16];		/* driver .sys file name */
	u8 infostrs[96];	/* kernel version */
	u8 reserved[128];	/* pad size to 256 bytes */
} __packed;

struct visor_vbus_headerinfo {
	u32 struct_bytes;	/* size of this struct in bytes */
	u32 device_info_struct_bytes;	/* sizeof(VISOR_VBUS_DEVICEINFO) */
	u32 dev_info_count;	/* num of items in DevInfo member */
	/* (this is the allocated size) */
	u32 chp_info_offset;	/* byte offset from beginning of this struct */
	/* to the ChpInfo struct (below) */
	u32 bus_info_offset;	/* byte offset from beginning of this struct */
	/* to the BusInfo struct (below) */
	u32 dev_info_offset;	/* byte offset from beginning of this struct */
	/* to the DevInfo array (below) */
	u8 reserved[104];
} __packed;

struct visor_vbus_channel {
	struct channel_header channel_header;	/* initialized by server */
	struct visor_vbus_headerinfo hdr_info;	/* initialized by server */
	/* the remainder of this channel is filled in by the client */
	struct visor_vbus_deviceinfo chp_info;
	/* describes client chipset device and driver */
	struct visor_vbus_deviceinfo bus_info;
	/* describes client bus device and driver */
	struct visor_vbus_deviceinfo dev_info[0];
	/* describes client device and driver for each device on the bus */
} __packed;

#endif
