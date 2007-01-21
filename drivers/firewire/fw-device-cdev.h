/*						-*- c-basic-offset: 8 -*-
 *
 * fw-device-cdev.h -- Char device interface.
 *
 * Copyright (C) 2005-2006  Kristian Hoegsberg <krh@bitplanet.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __fw_cdev_h
#define __fw_cdev_h

#include <asm/ioctl.h>
#include <asm/types.h>

#define TCODE_WRITE_QUADLET_REQUEST	0
#define TCODE_WRITE_BLOCK_REQUEST	1
#define TCODE_WRITE_RESPONSE		2
#define TCODE_READ_QUADLET_REQUEST	4
#define TCODE_READ_BLOCK_REQUEST	5
#define TCODE_READ_QUADLET_RESPONSE	6
#define TCODE_READ_BLOCK_RESPONSE	7
#define TCODE_CYCLE_START		8
#define TCODE_LOCK_REQUEST		9
#define TCODE_STREAM_DATA		10
#define TCODE_LOCK_RESPONSE		11

#define RCODE_COMPLETE			0x0
#define RCODE_CONFLICT_ERROR		0x4
#define RCODE_DATA_ERROR		0x5
#define RCODE_TYPE_ERROR		0x6
#define RCODE_ADDRESS_ERROR		0x7

#define SCODE_100			0x0
#define SCODE_200			0x1
#define SCODE_400			0x2
#define SCODE_800			0x3
#define SCODE_1600			0x4
#define SCODE_3200			0x5

#define FW_CDEV_EVENT_RESPONSE		0x00
#define FW_CDEV_EVENT_REQUEST		0x01
#define FW_CDEV_EVENT_ISO_INTERRUPT	0x02

/* The 'closure' fields are for user space to use.  Data passed in the
 * 'closure' field for a request will be returned in the corresponding
 * event.  It's a 64-bit type so that it's a fixed size type big
 * enough to hold a pointer on all platforms. */

struct fw_cdev_event_response {
	__u32 type;
	__u32 rcode;
	__u64 closure;
	__u32 length;
	__u32 data[0];
};

struct fw_cdev_event_request {
	__u32 type;
	__u32 tcode;
	__u64 offset;
	__u64 closure;
	__u32 serial;
	__u32 length;
	__u32 data[0];
};

struct fw_cdev_event_iso_interrupt {
	__u32 type;
	__u32 cycle;
	__u64 closure;
};

#define FW_CDEV_IOC_GET_CONFIG_ROM	_IOR('#', 0x00, struct fw_cdev_get_config_rom)
#define FW_CDEV_IOC_SEND_REQUEST	_IO('#', 0x01)
#define FW_CDEV_IOC_ALLOCATE		_IO('#', 0x02)
#define FW_CDEV_IOC_SEND_RESPONSE	_IO('#', 0x03)
#define FW_CDEV_IOC_CREATE_ISO_CONTEXT	_IO('#', 0x04)
#define FW_CDEV_IOC_QUEUE_ISO		_IO('#', 0x05)
#define FW_CDEV_IOC_SEND_ISO		_IO('#', 0x06)

struct fw_cdev_get_config_rom {
	__u32 length;
	__u32 data[256];
};

struct fw_cdev_send_request {
	__u32 tcode;
	__u32 length;
	__u64 offset;
	__u64 closure;
	__u64 data;
};

struct fw_cdev_send_response {
	__u32 rcode;
	__u32 length;
	__u64 data;
	__u32 serial;
};

struct fw_cdev_allocate {
	__u64 offset;
	__u64 closure;
	__u32 length;
};

struct fw_cdev_create_iso_context {
	__u32 buffer_size;
};

struct fw_cdev_iso_packet {
	__u16 payload_length;	/* Length of indirect payload. */
	__u32 interrupt : 1;	/* Generate interrupt on this packet */
	__u32 skip : 1;		/* Set to not send packet at all. */
	__u32 tag : 2;
	__u32 sy : 4;
	__u32 header_length : 8;	/* Length of immediate header. */
	__u32 header[0];
};

struct fw_cdev_queue_iso {
	__u32 size;
	__u64 packets;
	__u64 data;
};

struct fw_cdev_send_iso {
	__u32 channel;
	__u32 speed;
	__s32 cycle;
};

#endif /* __fw_cdev_h */
