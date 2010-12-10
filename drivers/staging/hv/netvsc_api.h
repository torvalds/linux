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


#ifndef _NETVSC_API_H_
#define _NETVSC_API_H_

#include "vmbus_api.h"

/* Fwd declaration */
struct hv_netvsc_packet;

/* Represent the xfer page packet which contains 1 or more netvsc packet */
struct xferpage_packet {
	struct list_head list_ent;

	/* # of netvsc packets this xfer packet contains */
	u32 count;
};

/* The number of pages which are enough to cover jumbo frame buffer. */
#define NETVSC_PACKET_MAXPAGE		4

/*
 * Represent netvsc packet which contains 1 RNDIS and 1 ethernet frame
 * within the RNDIS
 */
struct hv_netvsc_packet {
	/* Bookkeeping stuff */
	struct list_head list_ent;

	struct hv_device *device;
	bool is_data_pkt;

	/*
	 * Valid only for receives when we break a xfer page packet
	 * into multiple netvsc packets
	 */
	struct xferpage_packet *xfer_page_pkt;

	union {
		struct{
			u64 recv_completion_tid;
			void *recv_completion_ctx;
			void (*recv_completion)(void *context);
		} recv;
		struct{
			u64 send_completion_tid;
			void *send_completion_ctx;
			void (*send_completion)(void *context);
		} send;
	} completion;

	/* This points to the memory after page_buf */
	void *extension;

	u32 total_data_buflen;
	/* Points to the send/receive buffer where the ethernet frame is */
	u32 page_buf_cnt;
	struct hv_page_buffer page_buf[NETVSC_PACKET_MAXPAGE];
};

/* Represents the net vsc driver */
struct netvsc_driver {
	/* Must be the first field */
	/* Which is a bug FIXME! */
	struct hv_driver base;

	u32 ring_buf_size;
	u32 req_ext_size;

	/*
	 * This is set by the caller to allow us to callback when we
	 * receive a packet from the "wire"
	 */
	int (*recv_cb)(struct hv_device *dev,
				 struct hv_netvsc_packet *packet);
	void (*link_status_change)(struct hv_device *dev, u32 Status);

	/* Specific to this driver */
	int (*send)(struct hv_device *dev, struct hv_netvsc_packet *packet);

	void *ctx;
};

struct netvsc_device_info {
	unsigned char mac_adr[6];
	bool link_state;	/* 0 - link up, 1 - link down */
};

/* Interface */
int netvsc_initialize(struct hv_driver *drv);
int rndis_filter_open(struct hv_device *dev);
int rndis_filter_close(struct hv_device *dev);

#endif /* _NETVSC_API_H_ */
