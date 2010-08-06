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
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#ifndef __HV_UTILS_H_
#define __HV_UTILS_H_

/*
 * Common header for Hyper-V ICs
 */
#define ICMSGTYPE_NEGOTIATE		0
#define ICMSGTYPE_HEARTBEAT		1
#define ICMSGTYPE_KVPEXCHANGE		2
#define ICMSGTYPE_SHUTDOWN		3
#define ICMSGTYPE_TIMESYNC		4
#define ICMSGTYPE_VSS			5

#define ICMSGHDRFLAG_TRANSACTION	1
#define ICMSGHDRFLAG_REQUEST		2
#define ICMSGHDRFLAG_RESPONSE		4

#define HV_S_OK				0x00000000
#define HV_E_FAIL			0x80004005
#define HV_ERROR_NOT_SUPPORTED		0x80070032
#define HV_ERROR_MACHINE_LOCKED		0x800704F7

struct vmbuspipe_hdr {
	u32 flags;
	u32 msgsize;
} __attribute__((packed));

struct ic_version {
	u16 major;
	u16 minor;
} __attribute__((packed));

struct icmsg_hdr {
	struct ic_version icverframe;
	u16 icmsgtype;
	struct ic_version icvermsg;
	u16 icmsgsize;
	u32 status;
	u8 ictransaction_id;
	u8 icflags;
	u8 reserved[2];
} __attribute__((packed));

struct icmsg_negotiate {
	u16 icframe_vercnt;
	u16 icmsg_vercnt;
	u32 reserved;
	struct ic_version icversion_data[1]; /* any size array */
} __attribute__((packed));

struct shutdown_msg_data {
	u32 reason_code;
	u32 timeout_seconds;
	u32 flags;
	u8  display_message[2048];
} __attribute__((packed));

struct heartbeat_msg_data {
	u64 seq_num;
	u32 reserved[8];
} __attribute__((packed));

/* Time Sync IC defs */
#define ICTIMESYNCFLAG_PROBE	0
#define ICTIMESYNCFLAG_SYNC	1
#define ICTIMESYNCFLAG_SAMPLE	2

#ifdef __x86_64__
#define WLTIMEDELTA	116444736000000000L	/* in 100ns unit */
#else
#define WLTIMEDELTA	116444736000000000LL
#endif

struct ictimesync_data{
	u64 parenttime;
	u64 childtime;
	u64 roundtriptime;
	u8 flags;
} __attribute__((packed));

/* Index for each IC struct in array hv_cb_utils[] */
#define HV_SHUTDOWN_MSG		0
#define HV_TIMESYNC_MSG		1
#define HV_HEARTBEAT_MSG	2

struct hyperv_service_callback {
	u8 msg_type;
	char *log_msg;
	unsigned char data[16];
	struct vmbus_channel *channel;
	void (*callback) (void *context);
};

extern void prep_negotiate_resp(struct icmsg_hdr *,
				struct icmsg_negotiate *, u8 *);
extern void chn_cb_negotiate(void *);
extern struct hyperv_service_callback hv_cb_utils[];

#endif /* __HV_UTILS_H_ */
