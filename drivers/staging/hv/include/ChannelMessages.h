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

#ifndef __CHANNELMESSAGES_H
#define __CHANNELMESSAGES_H

#include "VmbusPacketFormat.h"

/* Version 1 messages */
enum vmbus_channel_message_type {
	ChannelMessageInvalid			=  0,
	ChannelMessageOfferChannel		=  1,
	ChannelMessageRescindChannelOffer	=  2,
	ChannelMessageRequestOffers		=  3,
	ChannelMessageAllOffersDelivered	=  4,
	ChannelMessageOpenChannel		=  5,
	ChannelMessageOpenChannelResult		=  6,
	ChannelMessageCloseChannel		=  7,
	ChannelMessageGpadlHeader		=  8,
	ChannelMessageGpadlBody			=  9,
	ChannelMessageGpadlCreated		= 10,
	ChannelMessageGpadlTeardown		= 11,
	ChannelMessageGpadlTorndown		= 12,
	ChannelMessageRelIdReleased		= 13,
	ChannelMessageInitiateContact		= 14,
	ChannelMessageVersionResponse		= 15,
	ChannelMessageUnload			= 16,
#ifdef VMBUS_FEATURE_PARENT_OR_PEER_MEMORY_MAPPED_INTO_A_CHILD
	ChannelMessageViewRangeAdd		= 17,
	ChannelMessageViewRangeRemove		= 18,
#endif
	ChannelMessageCount
} __attribute__((packed));

struct vmbus_channel_message_header {
	enum vmbus_channel_message_type MessageType;
	u32 Padding;
} __attribute__((packed));

/* Query VMBus Version parameters */
struct vmbus_channel_query_vmbus_version {
	struct vmbus_channel_message_header Header;
	u32 Version;
} __attribute__((packed));

/* VMBus Version Supported parameters */
struct vmbus_channel_version_supported {
	struct vmbus_channel_message_header Header;
	bool VersionSupported;
} __attribute__((packed));

/* Offer Channel parameters */
struct vmbus_channel_offer_channel {
	struct vmbus_channel_message_header Header;
	struct vmbus_channel_offer Offer;
	u32 ChildRelId;
	u8 MonitorId;
	bool MonitorAllocated;
} __attribute__((packed));

/* Rescind Offer parameters */
struct vmbus_channel_rescind_offer {
	struct vmbus_channel_message_header Header;
	u32 ChildRelId;
} __attribute__((packed));

/*
 * Request Offer -- no parameters, SynIC message contains the partition ID
 * Set Snoop -- no parameters, SynIC message contains the partition ID
 * Clear Snoop -- no parameters, SynIC message contains the partition ID
 * All Offers Delivered -- no parameters, SynIC message contains the partition
 *		           ID
 * Flush Client -- no parameters, SynIC message contains the partition ID
 */

/* Open Channel parameters */
struct vmbus_channel_open_channel {
	struct vmbus_channel_message_header Header;

	/* Identifies the specific VMBus channel that is being opened. */
	u32 ChildRelId;

	/* ID making a particular open request at a channel offer unique. */
	u32 OpenId;

	/* GPADL for the channel's ring buffer. */
	u32 RingBufferGpadlHandle;

	/* GPADL for the channel's server context save area. */
	u32 ServerContextAreaGpadlHandle;

	/*
	* The upstream ring buffer begins at offset zero in the memory
	* described by RingBufferGpadlHandle. The downstream ring buffer
	* follows it at this offset (in pages).
	*/
	u32 DownstreamRingBufferPageOffset;

	/* User-specific data to be passed along to the server endpoint. */
	unsigned char UserData[MAX_USER_DEFINED_BYTES];
} __attribute__((packed));

/* Open Channel Result parameters */
struct vmbus_channel_open_result {
	struct vmbus_channel_message_header Header;
	u32 ChildRelId;
	u32 OpenId;
	u32 Status;
} __attribute__((packed));

/* Close channel parameters; */
struct vmbus_channel_close_channel {
	struct vmbus_channel_message_header Header;
	u32 ChildRelId;
} __attribute__((packed));

/* Channel Message GPADL */
#define GPADL_TYPE_RING_BUFFER		1
#define GPADL_TYPE_SERVER_SAVE_AREA	2
#define GPADL_TYPE_TRANSACTION		8

/*
 * The number of PFNs in a GPADL message is defined by the number of
 * pages that would be spanned by ByteCount and ByteOffset.  If the
 * implied number of PFNs won't fit in this packet, there will be a
 * follow-up packet that contains more.
 */
struct vmbus_channel_gpadl_header {
	struct vmbus_channel_message_header Header;
	u32 ChildRelId;
	u32 Gpadl;
	u16 RangeBufLen;
	u16 RangeCount;
	GPA_RANGE Range[0];
} __attribute__((packed));

/* This is the followup packet that contains more PFNs. */
struct vmbus_channel_gpadl_body {
	struct vmbus_channel_message_header Header;
	u32 MessageNumber;
	u32 Gpadl;
	u64 Pfn[0];
} __attribute__((packed));

struct vmbus_channel_gpadl_created {
	struct vmbus_channel_message_header Header;
	u32 ChildRelId;
	u32 Gpadl;
	u32 CreationStatus;
} __attribute__((packed));

struct vmbus_channel_gpadl_teardown {
	struct vmbus_channel_message_header Header;
	u32 ChildRelId;
	u32 Gpadl;
} __attribute__((packed));

struct vmbus_channel_gpadl_torndown {
	struct vmbus_channel_message_header Header;
	u32 Gpadl;
} __attribute__((packed));

#ifdef VMBUS_FEATURE_PARENT_OR_PEER_MEMORY_MAPPED_INTO_A_CHILD
struct vmbus_channel_view_range_add {
	struct vmbus_channel_message_header Header;
	PHYSICAL_ADDRESS ViewRangeBase;
	u64 ViewRangeLength;
	u32 ChildRelId;
} __attribute__((packed));

struct vmbus_channel_view_range_remove {
	struct vmbus_channel_message_header Header;
	PHYSICAL_ADDRESS ViewRangeBase;
	u32 ChildRelId;
} __attribute__((packed));
#endif

struct vmbus_channel_relid_released {
	struct vmbus_channel_message_header Header;
	u32 ChildRelId;
} __attribute__((packed));

struct vmbus_channel_initiate_contact {
	struct vmbus_channel_message_header Header;
	u32 VMBusVersionRequested;
	u32 Padding2;
	u64 InterruptPage;
	u64 MonitorPage1;
	u64 MonitorPage2;
} __attribute__((packed));

struct vmbus_channel_version_response {
	struct vmbus_channel_message_header Header;
	bool VersionSupported;
} __attribute__((packed));

#endif
