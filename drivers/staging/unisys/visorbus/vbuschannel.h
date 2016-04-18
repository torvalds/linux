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
#include "vbusdeviceinfo.h"
#include "channel.h"

/* {193b331b-c58f-11da-95a9-00e08161165f} */
#define SPAR_VBUS_CHANNEL_PROTOCOL_UUID \
		UUID_LE(0x193b331b, 0xc58f, 0x11da, \
				0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)
static const uuid_le spar_vbus_channel_protocol_uuid =
	SPAR_VBUS_CHANNEL_PROTOCOL_UUID;

#define SPAR_VBUS_CHANNEL_PROTOCOL_SIGNATURE ULTRA_CHANNEL_PROTOCOL_SIGNATURE

/* Must increment this whenever you insert or delete fields within this channel
 * struct.  Also increment whenever you change the meaning of fields within this
 * channel struct so as to break pre-existing software.  Note that you can
 * usually add fields to the END of the channel struct withOUT needing to
 * increment this.
 */
#define SPAR_VBUS_CHANNEL_PROTOCOL_VERSIONID 1

#define SPAR_VBUS_CHANNEL_OK_CLIENT(ch)       \
	spar_check_channel_client(ch,				\
				   spar_vbus_channel_protocol_uuid,	\
				   "vbus",				\
				   sizeof(struct spar_vbus_channel_protocol),\
				   SPAR_VBUS_CHANNEL_PROTOCOL_VERSIONID, \
				   SPAR_VBUS_CHANNEL_PROTOCOL_SIGNATURE)

#define SPAR_VBUS_CHANNEL_OK_SERVER(actual_bytes)    \
	(spar_check_channel_server(spar_vbus_channel_protocol_uuid,	\
				   "vbus",				\
				   sizeof(struct spar_vbus_channel_protocol),\
				   actual_bytes))

#pragma pack(push, 1)		/* both GCC and VC now allow this pragma */
struct spar_vbus_headerinfo {
	u32 struct_bytes;	/* size of this struct in bytes */
	u32 device_info_struct_bytes;	/* sizeof(ULTRA_VBUS_DEVICEINFO) */
	u32 dev_info_count;	/* num of items in DevInfo member */
	/* (this is the allocated size) */
	u32 chp_info_offset;	/* byte offset from beginning of this struct */
	/* to the ChpInfo struct (below) */
	u32 bus_info_offset;	/* byte offset from beginning of this struct */
	/* to the BusInfo struct (below) */
	u32 dev_info_offset;	/* byte offset from beginning of this struct */
	/* to the DevInfo array (below) */
	u8 reserved[104];
};

struct spar_vbus_channel_protocol {
	struct channel_header channel_header;	/* initialized by server */
	struct spar_vbus_headerinfo hdr_info;	/* initialized by server */
	/* the remainder of this channel is filled in by the client */
	struct ultra_vbus_deviceinfo chp_info;
	/* describes client chipset device and driver */
	struct ultra_vbus_deviceinfo bus_info;
	/* describes client bus device and driver */
	struct ultra_vbus_deviceinfo dev_info[0];
	/* describes client device and driver for each device on the bus */
};

#define VBUS_CH_SIZE_EXACT(MAXDEVICES) \
	(sizeof(ULTRA_VBUS_CHANNEL_PROTOCOL) + ((MAXDEVICES) * \
						sizeof(ULTRA_VBUS_DEVICEINFO)))
#define VBUS_CH_SIZE(MAXDEVICES) COVER(VBUS_CH_SIZE_EXACT(MAXDEVICES), 4096)

#pragma pack(pop)

#endif
