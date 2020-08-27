/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 */

#ifndef __VBUSCHANNEL_H__
#define __VBUSCHANNEL_H__

/*
 * The vbus channel is the channel area provided via the BUS_CREATE controlvm
 * message for each virtual bus.  This channel area is provided to both server
 * and client ends of the bus.  The channel header area is initialized by
 * the server, and the remaining information is filled in by the client.
 * We currently use this for the client to provide various information about
 * the client devices and client drivers for the server end to see.
 */

#include <linux/uuid.h>
#include <linux/visorbus.h>

/* {193b331b-c58f-11da-95a9-00e08161165f} */
#define VISOR_VBUS_CHANNEL_GUID						\
	GUID_INIT(0x193b331b, 0xc58f, 0x11da,				\
		  0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)

/*
 * Must increment this whenever you insert or delete fields within this channel
 * struct.  Also increment whenever you change the meaning of fields within this
 * channel struct so as to break pre-existing software.  Note that you can
 * usually add fields to the END of the channel struct withOUT needing to
 * increment this.
 */
#define VISOR_VBUS_CHANNEL_VERSIONID 1

/*
 * struct visor_vbus_deviceinfo
 * @devtype:  Short string identifying the device type.
 * @drvname:  Driver .sys file name.
 * @infostrs: Kernel vversion.
 * @reserved: Pad size to 256 bytes.
 *
 * An array of this struct is present in the channel area for each vbus. It is
 * filled in by the client side to provide info about the device and driver from
 * the client's perspective.
 */
struct visor_vbus_deviceinfo {
	u8 devtype[16];
	u8 drvname[16];
	u8 infostrs[96];
	u8 reserved[128];
} __packed;

/*
 * struct visor_vbus_headerinfo
 * @struct_bytes:	      Size of this struct in bytes.
 * @device_info_struct_bytes: Size of VISOR_VBUS_DEVICEINFO.
 * @dev_info_count:	      Num of items in DevInfo member. This is the
 *			      allocated size.
 * @chp_info_offset:	      Byte offset from beginning of this struct to the
 *			      ChpInfo struct.
 * @bus_info_offset:	      Byte offset from beginning of this struct to the
 *			      BusInfo struct.
 * @dev_info_offset:	      Byte offset from beginning of this struct to the
 *			      DevInfo array.
 * @reserved:		      Natural alignment.
 */
struct visor_vbus_headerinfo {
	u32 struct_bytes;
	u32 device_info_struct_bytes;
	u32 dev_info_count;
	u32 chp_info_offset;
	u32 bus_info_offset;
	u32 dev_info_offset;
	u8 reserved[104];
} __packed;

/*
 * struct visor_vbus_channel
 * @channel_header: Initialized by server.
 * @hdr_info:	    Initialized by server.
 * @chp_info:	    Describes client chipset device and driver.
 * @bus_info:	    Describes client bus device and driver.
 * @dev_info:	    Describes client device and driver for each device on the
 *		    bus.
 */
struct visor_vbus_channel {
	struct channel_header channel_header;
	struct visor_vbus_headerinfo hdr_info;
	struct visor_vbus_deviceinfo chp_info;
	struct visor_vbus_deviceinfo bus_info;
	struct visor_vbus_deviceinfo dev_info[0];
} __packed;

#endif
