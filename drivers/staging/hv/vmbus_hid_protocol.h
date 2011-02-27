/*
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
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#ifndef _VMBUS_HID_PROTOCOL_
#define _VMBUS_HID_PROTOCOL_

/* The maximum size of a synthetic input message. */
#define SYNTHHID_MAX_INPUT_REPORT_SIZE 16

/*
 * Current version
 *
 * History:
 * Beta, RC < 2008/1/22        1,0
 * RC > 2008/1/22              2,0
 */
#define SYNTHHID_INPUT_VERSION_MAJOR 2
#define SYNTHHID_INPUT_VERSION_MINOR 0
#define SYNTHHID_INPUT_VERSION_DWORD (SYNTHHID_INPUT_VERSION_MINOR | \
    (SYNTHHID_INPUT_VERSION_MAJOR << 16))


#pragma pack(push,1)

/*
 * Message types in the synthetic input protocol
 */
enum synthhid_msg_type
{
	SynthHidProtocolRequest,
	SynthHidProtocolResponse,
	SynthHidInitialDeviceInfo,
	SynthHidInitialDeviceInfoAck,
	SynthHidInputReport,
	SynthHidMax
};


/*
 * Basic message structures.
 */
typedef struct
{
	enum synthhid_msg_type  Type;    /* Type of the enclosed message */
	u32                     Size;    /* Size of the enclosed message
					  *  (size of the data payload)
					  */
} SYNTHHID_MESSAGE_HEADER, *PSYNTHHID_MESSAGE_HEADER;

typedef struct
{
	SYNTHHID_MESSAGE_HEADER Header;
	char                    Data[1]; /* Enclosed message */
} SYNTHHID_MESSAGE, *PSYNTHHID_MESSAGE;

typedef union
{
	struct {
		u16  Minor;
		u16  Major;
	};

	u32 AsDWord;
} SYNTHHID_VERSION, *PSYNTHHID_VERSION;

/*
 * Protocol messages
 */
typedef struct
{
	SYNTHHID_MESSAGE_HEADER Header;
	SYNTHHID_VERSION        VersionRequested;
} SYNTHHID_PROTOCOL_REQUEST, *PSYNTHHID_PROTOCOL_REQUEST;

typedef struct
{
	SYNTHHID_MESSAGE_HEADER Header;
	SYNTHHID_VERSION        VersionRequested;
	unsigned char           Approved;
} SYNTHHID_PROTOCOL_RESPONSE, *PSYNTHHID_PROTOCOL_RESPONSE;

typedef struct
{
	SYNTHHID_MESSAGE_HEADER     Header;
	struct input_dev_info       HidDeviceAttributes;
	unsigned char               HidDescriptorInformation[1];
} SYNTHHID_DEVICE_INFO, *PSYNTHHID_DEVICE_INFO;

typedef struct
{
	SYNTHHID_MESSAGE_HEADER Header;
	unsigned char           Reserved;
} SYNTHHID_DEVICE_INFO_ACK, *PSYNTHHID_DEVICE_INFO_ACK;

typedef struct
{
	SYNTHHID_MESSAGE_HEADER Header;
	char                    ReportBuffer[1];
} SYNTHHID_INPUT_REPORT, *PSYNTHHID_INPUT_REPORT;

#pragma pack(pop)

#endif /* _VMBUS_HID_PROTOCOL_ */

