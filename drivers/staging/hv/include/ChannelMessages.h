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

#include "VmbusPacketFormat.h"

#define C_ASSERT(x)
typedef u32 NTSTATUS;

#pragma pack(push,1)

//
// Version 1 messages
//

typedef enum _VMBUS_CHANNEL_MESSAGE_TYPE
{
    ChannelMessageInvalid                   =  0,
    ChannelMessageOfferChannel              =  1,
    ChannelMessageRescindChannelOffer       =  2,
    ChannelMessageRequestOffers             =  3,
    ChannelMessageAllOffersDelivered        =  4,
    ChannelMessageOpenChannel               =  5,
    ChannelMessageOpenChannelResult         =  6,
    ChannelMessageCloseChannel              =  7,
    ChannelMessageGpadlHeader               =  8,
    ChannelMessageGpadlBody                 =  9,
    ChannelMessageGpadlCreated              = 10,
    ChannelMessageGpadlTeardown             = 11,
    ChannelMessageGpadlTorndown             = 12,
    ChannelMessageRelIdReleased             = 13,
    ChannelMessageInitiateContact           = 14,
    ChannelMessageVersionResponse           = 15,
    ChannelMessageUnload                    = 16,
#ifdef VMBUS_FEATURE_PARENT_OR_PEER_MEMORY_MAPPED_INTO_A_CHILD
    ChannelMessageViewRangeAdd              = 17,
    ChannelMessageViewRangeRemove           = 18,
#endif
    ChannelMessageCount
} VMBUS_CHANNEL_MESSAGE_TYPE, *PVMBUS_CHANNEL_MESSAGE_TYPE;

// begin_wpp config
// CUSTOM_TYPE(ChannelMessageType, ItemEnum(_VMBUS_CHANNEL_MESSAGE_TYPE));
// end_wpp

typedef struct _VMBUS_CHANNEL_MESSAGE_HEADER
{
    VMBUS_CHANNEL_MESSAGE_TYPE  MessageType;
    u32                      Padding;
} VMBUS_CHANNEL_MESSAGE_HEADER, *PVMBUS_CHANNEL_MESSAGE_HEADER;

// Query VMBus Version parameters
typedef struct _VMBUS_CHANNEL_QUERY_VMBUS_VERSION
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32 Version;
} VMBUS_CHANNEL_QUERY_VMBUS_VERSION, *PVMBUS_CHANNEL_QUERY_VMBUS_VERSION;

// VMBus Version Supported parameters
typedef struct _VMBUS_CHANNEL_VERSION_SUPPORTED
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    BOOLEAN VersionSupported;
} VMBUS_CHANNEL_VERSION_SUPPORTED, *PVMBUS_CHANNEL_VERSION_SUPPORTED;

// Offer Channel parameters
typedef struct _VMBUS_CHANNEL_OFFER_CHANNEL
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    VMBUS_CHANNEL_OFFER Offer;
    u32  ChildRelId;
    u8   MonitorId;
    BOOLEAN MonitorAllocated;
} VMBUS_CHANNEL_OFFER_CHANNEL, *PVMBUS_CHANNEL_OFFER_CHANNEL;

//
// Make sure VMBUS_CHANNEL_OFFER_CHANNEL fits into Synic message.
//
C_ASSERT(sizeof(VMBUS_CHANNEL_OFFER_CHANNEL) <= MAXIMUM_SYNIC_MESSAGE_BYTES);

// Rescind Offer parameters
typedef struct _VMBUS_CHANNEL_RESCIND_OFFER
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32          ChildRelId;
} VMBUS_CHANNEL_RESCIND_OFFER, *PVMBUS_CHANNEL_RESCIND_OFFER;

// Request Offer -- no parameters, SynIC message contains the partition ID
// Set Snoop -- no parameters, SynIC message contains the partition ID
// Clear Snoop -- no parameters, SynIC message contains the partition ID
// All Offers Delivered -- no parameters, SynIC message contains the partition ID
// Flush Client -- no parameters, SynIC message contains the partition ID

// Open Channel parameters
typedef struct _VMBUS_CHANNEL_OPEN_CHANNEL
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;

    //
    // Identifies the specific VMBus channel that is being opened.
    //
    u32          ChildRelId;

    //
    // ID making a particular open request at a channel offer unique.
    //
    u32          OpenId;

    //
    // GPADL for the channel's ring buffer.
    //
    GPADL_HANDLE    RingBufferGpadlHandle;

    //
    // GPADL for the channel's server context save area.
    //
    GPADL_HANDLE    ServerContextAreaGpadlHandle;

    //
    // The upstream ring buffer begins at offset zero in the memory described
    // by RingBufferGpadlHandle. The downstream ring buffer follows it at this
    // offset (in pages).
    //
    u32          DownstreamRingBufferPageOffset;

    //
    // User-specific data to be passed along to the server endpoint.
    //
    UCHAR           UserData[MAX_USER_DEFINED_BYTES];

} VMBUS_CHANNEL_OPEN_CHANNEL, *PVMBUS_CHANNEL_OPEN_CHANNEL;

// Reopen Channel parameters;
typedef VMBUS_CHANNEL_OPEN_CHANNEL VMBUS_CHANNEL_REOPEN_CHANNEL, *PVMBUS_CHANNEL_REOPEN_CHANNEL;

// Open Channel Result parameters
typedef struct _VMBUS_CHANNEL_OPEN_RESULT
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32      ChildRelId;
    u32      OpenId;
    NTSTATUS    Status;
} VMBUS_CHANNEL_OPEN_RESULT, *PVMBUS_CHANNEL_OPEN_RESULT;

// Close channel parameters;
typedef struct _VMBUS_CHANNEL_CLOSE_CHANNEL
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32      ChildRelId;
} VMBUS_CHANNEL_CLOSE_CHANNEL, *PVMBUS_CHANNEL_CLOSE_CHANNEL;

// Channel Message GPADL
#define GPADL_TYPE_RING_BUFFER          1
#define GPADL_TYPE_SERVER_SAVE_AREA     2
#define GPADL_TYPE_TRANSACTION          8

//
// The number of PFNs in a GPADL message is defined by the number of pages
// that would be spanned by ByteCount and ByteOffset.  If the implied number
// of PFNs won't fit in this packet, there will be a follow-up packet that
// contains more.
//

typedef struct _VMBUS_CHANNEL_GPADL_HEADER
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32      ChildRelId;
    u32      Gpadl;
    u16      RangeBufLen;
    u16      RangeCount;
    GPA_RANGE   Range[0];
} VMBUS_CHANNEL_GPADL_HEADER, *PVMBUS_CHANNEL_GPADL_HEADER;


//
// This is the followup packet that contains more PFNs.
//

typedef struct _VMBUS_CHANNEL_GPADL_BODY
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32              MessageNumber;
    u32              Gpadl;
    UINT64              Pfn[0];
} VMBUS_CHANNEL_GPADL_BODY, *PVMBUS_CHANNEL_GPADL_BODY;


typedef struct _VMBUS_CHANNEL_GPADL_CREATED
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32              ChildRelId;
    u32              Gpadl;
    u32              CreationStatus;
} VMBUS_CHANNEL_GPADL_CREATED, *PVMBUS_CHANNEL_GPADL_CREATED;

typedef struct _VMBUS_CHANNEL_GPADL_TEARDOWN
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32              ChildRelId;
    u32              Gpadl;
} VMBUS_CHANNEL_GPADL_TEARDOWN, *PVMBUS_CHANNEL_GPADL_TEARDOWN;

typedef struct _VMBUS_CHANNEL_GPADL_TORNDOWN
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32              Gpadl;
} VMBUS_CHANNEL_GPADL_TORNDOWN, *PVMBUS_CHANNEL_GPADL_TORNDOWN;

#ifdef VMBUS_FEATURE_PARENT_OR_PEER_MEMORY_MAPPED_INTO_A_CHILD
typedef struct _VMBUS_CHANNEL_VIEW_RANGE_ADD
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    PHYSICAL_ADDRESS    ViewRangeBase;
    UINT64              ViewRangeLength;
    u32              ChildRelId;
} VMBUS_CHANNEL_VIEW_RANGE_ADD, *PVMBUS_CHANNEL_VIEW_RANGE_ADD;

typedef struct _VMBUS_CHANNEL_VIEW_RANGE_REMOVE
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    PHYSICAL_ADDRESS    ViewRangeBase;
    u32              ChildRelId;
} VMBUS_CHANNEL_VIEW_RANGE_REMOVE, *PVMBUS_CHANNEL_VIEW_RANGE_REMOVE;
#endif

typedef struct _VMBUS_CHANNEL_RELID_RELEASED
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32              ChildRelId;
} VMBUS_CHANNEL_RELID_RELEASED, *PVMBUS_CHANNEL_RELID_RELEASED;

typedef struct _VMBUS_CHANNEL_INITIATE_CONTACT
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    u32              VMBusVersionRequested;
    u32              Padding2;
    UINT64              InterruptPage;
    UINT64              MonitorPage1;
    UINT64              MonitorPage2;
} VMBUS_CHANNEL_INITIATE_CONTACT, *PVMBUS_CHANNEL_INITIATE_CONTACT;

typedef struct _VMBUS_CHANNEL_VERSION_RESPONSE
{
    VMBUS_CHANNEL_MESSAGE_HEADER Header;
    BOOLEAN     VersionSupported;
} VMBUS_CHANNEL_VERSION_RESPONSE, *PVMBUS_CHANNEL_VERSION_RESPONSE;

typedef VMBUS_CHANNEL_MESSAGE_HEADER VMBUS_CHANNEL_UNLOAD, *PVMBUS_CHANNEL_UNLOAD;

//
// Kind of a table to use the preprocessor to get us the right type for a
// specified message ID. Used with ChAllocateSendMessage()
//
#define ChannelMessageQueryVmbusVersion_TYPE    VMBUS_CHANNEL_MESSAGE_HEADER
#define ChannelMessageVmbusVersionSupported_TYPE VMBUS_CHANNEL_VERSION_SUPPORTED
#define ChannelMessageOfferChannel_TYPE         VMBUS_CHANNEL_OFFER_CHANNEL
#define ChannelMessageRescindChannelOffer_TYPE  VMBUS_CHANNEL_RESCIND_OFFER
#define ChannelMessageRequestOffers_TYPE        VMBUS_CHANNEL_MESSAGE_HEADER
#define ChannelMessageAllOffersDelivered_TYPE   VMBUS_CHANNEL_MESSAGE_HEADER
#define ChannelMessageOpenChannel_TYPE          VMBUS_CHANNEL_OPEN_CHANNEL
#define ChannelMessageOpenChannelResult_TYPE    VMBUS_CHANNEL_OPEN_RESULT
#define ChannelMessageCloseChannel_TYPE         VMBUS_CHANNEL_CLOSE_CHANNEL
#define ChannelMessageAllGpadlsUnmapped_TYPE    VMBUS_CHANNEL_CLOSE_CHANNEL
#define ChannelMessageGpadlHeader_TYPE          VMBUS_CHANNEL_GPADL_HEADER
#define ChannelMessageGpadlBody_TYPE            VMBUS_CHANNEL_GPADL_BODY
#define ChannelMessageGpadlCreated_TYPE         VMBUS_CHANNEL_GPADL_CREATED
#define ChannelMessageGpadlTeardown_TYPE        VMBUS_CHANNEL_GPADL_TEARDOWN
#define ChannelMessageGpadlTorndown_TYPE        VMBUS_CHANNEL_GPADL_TORNDOWN
#define ChannelMessageViewRangeAdd_TYPE         VMBUS_CHANNEL_VIEW_RANGE_ADD
#define ChannelMessageViewRangeRemove_TYPE      VMBUS_CHANNEL_VIEW_RANGE_REMOVE
#define ChannelMessageRelIdReleased_TYPE        VMBUS_CHANNEL_RELID_RELEASED
#define ChannelMessageInitiateContact_TYPE      VMBUS_CHANNEL_INITIATE_CONTACT
#define ChannelMessageVersionResponse_TYPE      VMBUS_CHANNEL_VERSION_RESPONSE
#define ChannelMessageUnload_TYPE               VMBUS_CHANNEL_UNLOAD

//
// Preprocessor wrapper to ChAllocateSendMessageSize() converting the return
// value to the correct pointer and calculate the needed size.
//
// Argument:
//
//  Id - the numberic ID (type VMBUS_CHANNEL_MESSAGE_TYPE) of the message to
//       send.
//
#define ChAllocateSendMessage(Id, Fn, Context)   \
    (Id##_TYPE*)ChAllocateSendMessageSized(sizeof(Id##_TYPE), Id, Fn, Context)


#pragma pack(pop)

