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

#ifndef _RNDIS_H_
#define _RNDIS_H_

/*  Status codes */


#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS				(0x00000000L)
#endif

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL			(0xC0000001L)
#endif

#ifndef STATUS_PENDING
#define STATUS_PENDING				(0x00000103L)
#endif

#ifndef STATUS_INSUFFICIENT_RESOURCES
#define STATUS_INSUFFICIENT_RESOURCES		(0xC000009AL)
#endif

#ifndef STATUS_BUFFER_OVERFLOW
#define STATUS_BUFFER_OVERFLOW			(0x80000005L)
#endif

#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED			(0xC00000BBL)
#endif

#define RNDIS_STATUS_SUCCESS			(STATUS_SUCCESS)
#define RNDIS_STATUS_PENDING			(STATUS_PENDING)
#define RNDIS_STATUS_NOT_RECOGNIZED		(0x00010001L)
#define RNDIS_STATUS_NOT_COPIED			(0x00010002L)
#define RNDIS_STATUS_NOT_ACCEPTED		(0x00010003L)
#define RNDIS_STATUS_CALL_ACTIVE		(0x00010007L)

#define RNDIS_STATUS_ONLINE			(0x40010003L)
#define RNDIS_STATUS_RESET_START		(0x40010004L)
#define RNDIS_STATUS_RESET_END			(0x40010005L)
#define RNDIS_STATUS_RING_STATUS		(0x40010006L)
#define RNDIS_STATUS_CLOSED			(0x40010007L)
#define RNDIS_STATUS_WAN_LINE_UP		(0x40010008L)
#define RNDIS_STATUS_WAN_LINE_DOWN		(0x40010009L)
#define RNDIS_STATUS_WAN_FRAGMENT		(0x4001000AL)
#define RNDIS_STATUS_MEDIA_CONNECT		(0x4001000BL)
#define RNDIS_STATUS_MEDIA_DISCONNECT		(0x4001000CL)
#define RNDIS_STATUS_HARDWARE_LINE_UP		(0x4001000DL)
#define RNDIS_STATUS_HARDWARE_LINE_DOWN		(0x4001000EL)
#define RNDIS_STATUS_INTERFACE_UP		(0x4001000FL)
#define RNDIS_STATUS_INTERFACE_DOWN		(0x40010010L)
#define RNDIS_STATUS_MEDIA_BUSY			(0x40010011L)
#define RNDIS_STATUS_MEDIA_SPECIFIC_INDICATION	(0x40010012L)
#define RNDIS_STATUS_WW_INDICATION		RDIA_SPECIFIC_INDICATION
#define RNDIS_STATUS_LINK_SPEED_CHANGE		(0x40010013L)

#define RNDIS_STATUS_NOT_RESETTABLE		(0x80010001L)
#define RNDIS_STATUS_SOFT_ERRORS		(0x80010003L)
#define RNDIS_STATUS_HARD_ERRORS		(0x80010004L)
#define RNDIS_STATUS_BUFFER_OVERFLOW		(STATUS_BUFFER_OVERFLOW)

#define RNDIS_STATUS_FAILURE			(STATUS_UNSUCCESSFUL)
#define RNDIS_STATUS_RESOURCES			(STATUS_INSUFFICIENT_RESOURCES)
#define RNDIS_STATUS_CLOSING			(0xC0010002L)
#define RNDIS_STATUS_BAD_VERSION		(0xC0010004L)
#define RNDIS_STATUS_BAD_CHARACTERISTICS	(0xC0010005L)
#define RNDIS_STATUS_ADAPTER_NOT_FOUND		(0xC0010006L)
#define RNDIS_STATUS_OPEN_FAILED		(0xC0010007L)
#define RNDIS_STATUS_DEVICE_FAILED		(0xC0010008L)
#define RNDIS_STATUS_MULTICAST_FULL		(0xC0010009L)
#define RNDIS_STATUS_MULTICAST_EXISTS		(0xC001000AL)
#define RNDIS_STATUS_MULTICAST_NOT_FOUND	(0xC001000BL)
#define RNDIS_STATUS_REQUEST_ABORTED		(0xC001000CL)
#define RNDIS_STATUS_RESET_IN_PROGRESS		(0xC001000DL)
#define RNDIS_STATUS_CLOSING_INDICATING		(0xC001000EL)
#define RNDIS_STATUS_NOT_SUPPORTED		(STATUS_NOT_SUPPORTED)
#define RNDIS_STATUS_INVALID_PACKET		(0xC001000FL)
#define RNDIS_STATUS_OPEN_LIST_FULL		(0xC0010010L)
#define RNDIS_STATUS_ADAPTER_NOT_READY		(0xC0010011L)
#define RNDIS_STATUS_ADAPTER_NOT_OPEN		(0xC0010012L)
#define RNDIS_STATUS_NOT_INDICATING		(0xC0010013L)
#define RNDIS_STATUS_INVALID_LENGTH		(0xC0010014L)
#define RNDIS_STATUS_INVALID_DATA		(0xC0010015L)
#define RNDIS_STATUS_BUFFER_TOO_SHORT		(0xC0010016L)
#define RNDIS_STATUS_INVALID_OID		(0xC0010017L)
#define RNDIS_STATUS_ADAPTER_REMOVED		(0xC0010018L)
#define RNDIS_STATUS_UNSUPPORTED_MEDIA		(0xC0010019L)
#define RNDIS_STATUS_GROUP_ADDRESS_IN_USE	(0xC001001AL)
#define RNDIS_STATUS_FILE_NOT_FOUND		(0xC001001BL)
#define RNDIS_STATUS_ERROR_READING_FILE		(0xC001001CL)
#define RNDIS_STATUS_ALREADY_MAPPED		(0xC001001DL)
#define RNDIS_STATUS_RESOURCE_CONFLICT		(0xC001001EL)
#define RNDIS_STATUS_NO_CABLE			(0xC001001FL)

#define RNDIS_STATUS_INVALID_SAP		(0xC0010020L)
#define RNDIS_STATUS_SAP_IN_USE			(0xC0010021L)
#define RNDIS_STATUS_INVALID_ADDRESS		(0xC0010022L)
#define RNDIS_STATUS_VC_NOT_ACTIVATED		(0xC0010023L)
#define RNDIS_STATUS_DEST_OUT_OF_ORDER		(0xC0010024L)
#define RNDIS_STATUS_VC_NOT_AVAILABLE		(0xC0010025L)
#define RNDIS_STATUS_CELLRATE_NOT_AVAILABLE	(0xC0010026L)
#define RNDIS_STATUS_INCOMPATABLE_QOS		(0xC0010027L)
#define RNDIS_STATUS_AAL_PARAMS_UNSUPPORTED	(0xC0010028L)
#define RNDIS_STATUS_NO_ROUTE_TO_DESTINATION	(0xC0010029L)

#define RNDIS_STATUS_TOKEN_RING_OPEN_ERROR	(0xC0011000L)

/* Object Identifiers used by NdisRequest Query/Set Information */
/* General Objects */
#define RNDIS_OID_GEN_SUPPORTED_LIST		0x00010101
#define RNDIS_OID_GEN_HARDWARE_STATUS		0x00010102
#define RNDIS_OID_GEN_MEDIA_SUPPORTED		0x00010103
#define RNDIS_OID_GEN_MEDIA_IN_USE		0x00010104
#define RNDIS_OID_GEN_MAXIMUM_LOOKAHEAD		0x00010105
#define RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE	0x00010106
#define RNDIS_OID_GEN_LINK_SPEED		0x00010107
#define RNDIS_OID_GEN_TRANSMIT_BUFFER_SPACE	0x00010108
#define RNDIS_OID_GEN_RECEIVE_BUFFER_SPACE	0x00010109
#define RNDIS_OID_GEN_TRANSMIT_BLOCK_SIZE	0x0001010A
#define RNDIS_OID_GEN_RECEIVE_BLOCK_SIZE	0x0001010B
#define RNDIS_OID_GEN_VENDOR_ID			0x0001010C
#define RNDIS_OID_GEN_VENDOR_DESCRIPTION	0x0001010D
#define RNDIS_OID_GEN_CURRENT_PACKET_FILTER	0x0001010E
#define RNDIS_OID_GEN_CURRENT_LOOKAHEAD		0x0001010F
#define RNDIS_OID_GEN_DRIVER_VERSION		0x00010110
#define RNDIS_OID_GEN_MAXIMUM_TOTAL_SIZE	0x00010111
#define RNDIS_OID_GEN_PROTOCOL_OPTIONS		0x00010112
#define RNDIS_OID_GEN_MAC_OPTIONS		0x00010113
#define RNDIS_OID_GEN_MEDIA_CONNECT_STATUS	0x00010114
#define RNDIS_OID_GEN_MAXIMUM_SEND_PACKETS	0x00010115
#define RNDIS_OID_GEN_VENDOR_DRIVER_VERSION	0x00010116
#define RNDIS_OID_GEN_NETWORK_LAYER_ADDRESSES	0x00010118
#define RNDIS_OID_GEN_TRANSPORT_HEADER_OFFSET	0x00010119
#define RNDIS_OID_GEN_MACHINE_NAME		0x0001021A
#define RNDIS_OID_GEN_RNDIS_CONFIG_PARAMETER	0x0001021B

#define RNDIS_OID_GEN_XMIT_OK			0x00020101
#define RNDIS_OID_GEN_RCV_OK			0x00020102
#define RNDIS_OID_GEN_XMIT_ERROR		0x00020103
#define RNDIS_OID_GEN_RCV_ERROR			0x00020104
#define RNDIS_OID_GEN_RCV_NO_BUFFER		0x00020105

#define RNDIS_OID_GEN_DIRECTED_BYTES_XMIT	0x00020201
#define RNDIS_OID_GEN_DIRECTED_FRAMES_XMIT	0x00020202
#define RNDIS_OID_GEN_MULTICAST_BYTES_XMIT	0x00020203
#define RNDIS_OID_GEN_MULTICAST_FRAMES_XMIT	0x00020204
#define RNDIS_OID_GEN_BROADCAST_BYTES_XMIT	0x00020205
#define RNDIS_OID_GEN_BROADCAST_FRAMES_XMIT	0x00020206
#define RNDIS_OID_GEN_DIRECTED_BYTES_RCV	0x00020207
#define RNDIS_OID_GEN_DIRECTED_FRAMES_RCV	0x00020208
#define RNDIS_OID_GEN_MULTICAST_BYTES_RCV	0x00020209
#define RNDIS_OID_GEN_MULTICAST_FRAMES_RCV	0x0002020A
#define RNDIS_OID_GEN_BROADCAST_BYTES_RCV	0x0002020B
#define RNDIS_OID_GEN_BROADCAST_FRAMES_RCV	0x0002020C

#define RNDIS_OID_GEN_RCV_CRC_ERROR		0x0002020D
#define RNDIS_OID_GEN_TRANSMIT_QUEUE_LENGTH	0x0002020E

#define RNDIS_OID_GEN_GET_TIME_CAPS		0x0002020F
#define RNDIS_OID_GEN_GET_NETCARD_TIME		0x00020210

/* These are connection-oriented general OIDs. */
/* These replace the above OIDs for connection-oriented media. */
#define RNDIS_OID_GEN_CO_SUPPORTED_LIST		0x00010101
#define RNDIS_OID_GEN_CO_HARDWARE_STATUS	0x00010102
#define RNDIS_OID_GEN_CO_MEDIA_SUPPORTED	0x00010103
#define RNDIS_OID_GEN_CO_MEDIA_IN_USE		0x00010104
#define RNDIS_OID_GEN_CO_LINK_SPEED		0x00010105
#define RNDIS_OID_GEN_CO_VENDOR_ID		0x00010106
#define RNDIS_OID_GEN_CO_VENDOR_DESCRIPTION	0x00010107
#define RNDIS_OID_GEN_CO_DRIVER_VERSION		0x00010108
#define RNDIS_OID_GEN_CO_PROTOCOL_OPTIONS	0x00010109
#define RNDIS_OID_GEN_CO_MAC_OPTIONS		0x0001010A
#define RNDIS_OID_GEN_CO_MEDIA_CONNECT_STATUS	0x0001010B
#define RNDIS_OID_GEN_CO_VENDOR_DRIVER_VERSION	0x0001010C
#define RNDIS_OID_GEN_CO_MINIMUM_LINK_SPEED	0x0001010D

#define RNDIS_OID_GEN_CO_GET_TIME_CAPS		0x00010201
#define RNDIS_OID_GEN_CO_GET_NETCARD_TIME	0x00010202

/* These are connection-oriented statistics OIDs. */
#define RNDIS_OID_GEN_CO_XMIT_PDUS_OK		0x00020101
#define RNDIS_OID_GEN_CO_RCV_PDUS_OK		0x00020102
#define RNDIS_OID_GEN_CO_XMIT_PDUS_ERROR	0x00020103
#define RNDIS_OID_GEN_CO_RCV_PDUS_ERROR		0x00020104
#define RNDIS_OID_GEN_CO_RCV_PDUS_NO_BUFFER	0x00020105


#define RNDIS_OID_GEN_CO_RCV_CRC_ERROR		0x00020201
#define RNDIS_OID_GEN_CO_TRANSMIT_QUEUE_LENGTH	0x00020202
#define RNDIS_OID_GEN_CO_BYTES_XMIT		0x00020203
#define RNDIS_OID_GEN_CO_BYTES_RCV		0x00020204
#define RNDIS_OID_GEN_CO_BYTES_XMIT_OUTSTANDING	0x00020205
#define RNDIS_OID_GEN_CO_NETCARD_LOAD		0x00020206

/* These are objects for Connection-oriented media call-managers. */
#define RNDIS_OID_CO_ADD_PVC			0xFF000001
#define RNDIS_OID_CO_DELETE_PVC			0xFF000002
#define RNDIS_OID_CO_GET_CALL_INFORMATION	0xFF000003
#define RNDIS_OID_CO_ADD_ADDRESS		0xFF000004
#define RNDIS_OID_CO_DELETE_ADDRESS		0xFF000005
#define RNDIS_OID_CO_GET_ADDRESSES		0xFF000006
#define RNDIS_OID_CO_ADDRESS_CHANGE		0xFF000007
#define RNDIS_OID_CO_SIGNALING_ENABLED		0xFF000008
#define RNDIS_OID_CO_SIGNALING_DISABLED		0xFF000009

/* 802.3 Objects (Ethernet) */
#define RNDIS_OID_802_3_PERMANENT_ADDRESS	0x01010101
#define RNDIS_OID_802_3_CURRENT_ADDRESS		0x01010102
#define RNDIS_OID_802_3_MULTICAST_LIST		0x01010103
#define RNDIS_OID_802_3_MAXIMUM_LIST_SIZE	0x01010104
#define RNDIS_OID_802_3_MAC_OPTIONS		0x01010105

#define NDIS_802_3_MAC_OPTION_PRIORITY		0x00000001

#define RNDIS_OID_802_3_RCV_ERROR_ALIGNMENT	0x01020101
#define RNDIS_OID_802_3_XMIT_ONE_COLLISION	0x01020102
#define RNDIS_OID_802_3_XMIT_MORE_COLLISIONS	0x01020103

#define RNDIS_OID_802_3_XMIT_DEFERRED		0x01020201
#define RNDIS_OID_802_3_XMIT_MAX_COLLISIONS	0x01020202
#define RNDIS_OID_802_3_RCV_OVERRUN		0x01020203
#define RNDIS_OID_802_3_XMIT_UNDERRUN		0x01020204
#define RNDIS_OID_802_3_XMIT_HEARTBEAT_FAILURE	0x01020205
#define RNDIS_OID_802_3_XMIT_TIMES_CRS_LOST	0x01020206
#define RNDIS_OID_802_3_XMIT_LATE_COLLISIONS	0x01020207

/* Remote NDIS message types */
#define REMOTE_NDIS_PACKET_MSG			0x00000001
#define REMOTE_NDIS_INITIALIZE_MSG		0x00000002
#define REMOTE_NDIS_HALT_MSG			0x00000003
#define REMOTE_NDIS_QUERY_MSG			0x00000004
#define REMOTE_NDIS_SET_MSG			0x00000005
#define REMOTE_NDIS_RESET_MSG			0x00000006
#define REMOTE_NDIS_INDICATE_STATUS_MSG		0x00000007
#define REMOTE_NDIS_KEEPALIVE_MSG		0x00000008

#define REMOTE_CONDIS_MP_CREATE_VC_MSG		0x00008001
#define REMOTE_CONDIS_MP_DELETE_VC_MSG		0x00008002
#define REMOTE_CONDIS_MP_ACTIVATE_VC_MSG	0x00008005
#define REMOTE_CONDIS_MP_DEACTIVATE_VC_MSG	0x00008006
#define REMOTE_CONDIS_INDICATE_STATUS_MSG	0x00008007

/* Remote NDIS message completion types */
#define REMOTE_NDIS_INITIALIZE_CMPLT		0x80000002
#define REMOTE_NDIS_QUERY_CMPLT			0x80000004
#define REMOTE_NDIS_SET_CMPLT			0x80000005
#define REMOTE_NDIS_RESET_CMPLT			0x80000006
#define REMOTE_NDIS_KEEPALIVE_CMPLT		0x80000008

#define REMOTE_CONDIS_MP_CREATE_VC_CMPLT	0x80008001
#define REMOTE_CONDIS_MP_DELETE_VC_CMPLT	0x80008002
#define REMOTE_CONDIS_MP_ACTIVATE_VC_CMPLT	0x80008005
#define REMOTE_CONDIS_MP_DEACTIVATE_VC_CMPLT	0x80008006

/*
 * Reserved message type for private communication between lower-layer host
 * driver and remote device, if necessary.
 */
#define REMOTE_NDIS_BUS_MSG			0xff000001

/*  Defines for DeviceFlags in struct rndis_initialize_complete */
#define RNDIS_DF_CONNECTIONLESS			0x00000001
#define RNDIS_DF_CONNECTION_ORIENTED		0x00000002
#define RNDIS_DF_RAW_DATA			0x00000004

/*  Remote NDIS medium types. */
#define RNdisMedium802_3			0x00000000
#define RNdisMedium802_5			0x00000001
#define RNdisMediumFddi				0x00000002
#define RNdisMediumWan				0x00000003
#define RNdisMediumLocalTalk			0x00000004
#define RNdisMediumArcnetRaw			0x00000006
#define RNdisMediumArcnet878_2			0x00000007
#define RNdisMediumAtm				0x00000008
#define RNdisMediumWirelessWan			0x00000009
#define RNdisMediumIrda				0x0000000a
#define RNdisMediumCoWan			0x0000000b
/* Not a real medium, defined as an upper-bound */
#define RNdisMediumMax				0x0000000d


/* Remote NDIS medium connection states. */
#define RNdisMediaStateConnected		0x00000000
#define RNdisMediaStateDisconnected		0x00000001

/*  Remote NDIS version numbers */
#define RNDIS_MAJOR_VERSION			0x00000001
#define RNDIS_MINOR_VERSION			0x00000000


/* NdisInitialize message */
struct rndis_initialize_request {
	u32 RequestId;
	u32 MajorVersion;
	u32 MinorVersion;
	u32 MaxTransferSize;
};

/* Response to NdisInitialize */
struct rndis_initialize_complete {
	u32 RequestId;
	u32 Status;
	u32 MajorVersion;
	u32 MinorVersion;
	u32 DeviceFlags;
	u32 Medium;
	u32 MaxPacketsPerMessage;
	u32 MaxTransferSize;
	u32 PacketAlignmentFactor;
	u32 AFListOffset;
	u32 AFListSize;
};

/* Call manager devices only: Information about an address family */
/* supported by the device is appended to the response to NdisInitialize. */
struct rndis_co_address_family {
	u32 AddressFamily;
	u32 MajorVersion;
	u32 MinorVersion;
};

/* NdisHalt message */
struct rndis_halt_request {
	u32 RequestId;
};

/* NdisQueryRequest message */
struct rndis_query_request {
	u32 RequestId;
	u32 Oid;
	u32 InformationBufferLength;
	u32 InformationBufferOffset;
	u32 DeviceVcHandle;
};

/* Response to NdisQueryRequest */
struct rndis_query_complete {
	u32 RequestId;
	u32 Status;
	u32 InformationBufferLength;
	u32 InformationBufferOffset;
};

/* NdisSetRequest message */
struct rndis_set_request {
	u32 RequestId;
	u32 Oid;
	u32 InformationBufferLength;
	u32 InformationBufferOffset;
	u32 DeviceVcHandle;
};

/* Response to NdisSetRequest */
struct rndis_set_complete {
	u32 RequestId;
	u32 Status;
};

/* NdisReset message */
struct rndis_reset_request {
	u32 Reserved;
};

/* Response to NdisReset */
struct rndis_reset_complete {
	u32 Status;
	u32 AddressingReset;
};

/* NdisMIndicateStatus message */
struct rndis_indicate_status {
	u32 Status;
	u32 StatusBufferLength;
	u32 StatusBufferOffset;
};

/* Diagnostic information passed as the status buffer in */
/* struct rndis_indicate_status messages signifying error conditions. */
struct rndis_diagnostic_info {
	u32 DiagStatus;
	u32 ErrorOffset;
};

/* NdisKeepAlive message */
struct rndis_keepalive_request {
	u32 RequestId;
};

/* Response to NdisKeepAlive */
struct rndis_keepalive_complete {
	u32 RequestId;
	u32 Status;
};

/*
 * Data message. All Offset fields contain byte offsets from the beginning of
 * struct rndis_packet. All Length fields are in bytes.  VcHandle is set
 * to 0 for connectionless data, otherwise it contains the VC handle.
 */
struct rndis_packet {
	u32 DataOffset;
	u32 DataLength;
	u32 OOBDataOffset;
	u32 OOBDataLength;
	u32 NumOOBDataElements;
	u32 PerPacketInfoOffset;
	u32 PerPacketInfoLength;
	u32 VcHandle;
	u32 Reserved;
};

/* Optional Out of Band data associated with a Data message. */
struct rndis_oobd {
	u32 Size;
	u32 Type;
	u32 ClassInformationOffset;
};

/* Packet extension field contents associated with a Data message. */
struct rndis_per_packet_info {
	u32 Size;
	u32 Type;
	u32 PerPacketInformationOffset;
};

/* Format of Information buffer passed in a SetRequest for the OID */
/* OID_GEN_RNDIS_CONFIG_PARAMETER. */
struct rndis_config_parameter_info {
	u32 ParameterNameOffset;
	u32 ParameterNameLength;
	u32 ParameterType;
	u32 ParameterValueOffset;
	u32 ParameterValueLength;
};

/* Values for ParameterType in struct rndis_config_parameter_info */
#define RNDIS_CONFIG_PARAM_TYPE_INTEGER     0
#define RNDIS_CONFIG_PARAM_TYPE_STRING      2

/* CONDIS Miniport messages for connection oriented devices */
/* that do not implement a call manager. */

/* CoNdisMiniportCreateVc message */
struct rcondis_mp_create_vc {
	u32 RequestId;
	u32 NdisVcHandle;
};

/* Response to CoNdisMiniportCreateVc */
struct rcondis_mp_create_vc_complete {
	u32 RequestId;
	u32 DeviceVcHandle;
	u32 Status;
};

/* CoNdisMiniportDeleteVc message */
struct rcondis_mp_delete_vc {
	u32 RequestId;
	u32 DeviceVcHandle;
};

/* Response to CoNdisMiniportDeleteVc */
struct rcondis_mp_delete_vc_complete {
	u32 RequestId;
	u32 Status;
};

/* CoNdisMiniportQueryRequest message */
struct rcondis_mp_query_request {
	u32 RequestId;
	u32 RequestType;
	u32 Oid;
	u32 DeviceVcHandle;
	u32 InformationBufferLength;
	u32 InformationBufferOffset;
};

/* CoNdisMiniportSetRequest message */
struct rcondis_mp_set_request {
	u32 RequestId;
	u32 RequestType;
	u32 Oid;
	u32 DeviceVcHandle;
	u32 InformationBufferLength;
	u32 InformationBufferOffset;
};

/* CoNdisIndicateStatus message */
struct rcondis_indicate_status {
	u32 NdisVcHandle;
	u32 Status;
	u32 StatusBufferLength;
	u32 StatusBufferOffset;
};

/* CONDIS Call/VC parameters */
struct rcondis_specific_parameters {
	u32 ParameterType;
	u32 ParameterLength;
	u32 ParameterOffset;
};

struct rcondis_media_parameters {
	u32 Flags;
	u32 Reserved1;
	u32 Reserved2;
	struct rcondis_specific_parameters MediaSpecific;
};

struct rndis_flowspec {
	u32 TokenRate;
	u32 TokenBucketSize;
	u32 PeakBandwidth;
	u32 Latency;
	u32 DelayVariation;
	u32 ServiceType;
	u32 MaxSduSize;
	u32 MinimumPolicedSize;
};

struct rcondis_call_manager_parameters {
	struct rndis_flowspec Transmit;
	struct rndis_flowspec Receive;
	struct rcondis_specific_parameters CallMgrSpecific;
};

/* CoNdisMiniportActivateVc message */
struct rcondis_mp_activate_vc_request {
	u32 RequestId;
	u32 Flags;
	u32 DeviceVcHandle;
	u32 MediaParamsOffset;
	u32 MediaParamsLength;
	u32 CallMgrParamsOffset;
	u32 CallMgrParamsLength;
};

/* Response to CoNdisMiniportActivateVc */
struct rcondis_mp_activate_vc_complete {
	u32 RequestId;
	u32 Status;
};

/* CoNdisMiniportDeactivateVc message */
struct rcondis_mp_deactivate_vc_request {
	u32 RequestId;
	u32 Flags;
	u32 DeviceVcHandle;
};

/* Response to CoNdisMiniportDeactivateVc */
struct rcondis_mp_deactivate_vc_complete {
	u32 RequestId;
	u32 Status;
};


/* union with all of the RNDIS messages */
union rndis_message_container {
	struct rndis_packet Packet;
	struct rndis_initialize_request InitializeRequest;
	struct rndis_halt_request HaltRequest;
	struct rndis_query_request QueryRequest;
	struct rndis_set_request SetRequest;
	struct rndis_reset_request ResetRequest;
	struct rndis_keepalive_request KeepaliveRequest;
	struct rndis_indicate_status IndicateStatus;
	struct rndis_initialize_complete InitializeComplete;
	struct rndis_query_complete QueryComplete;
	struct rndis_set_complete SetComplete;
	struct rndis_reset_complete ResetComplete;
	struct rndis_keepalive_complete KeepaliveComplete;
	struct rcondis_mp_create_vc CoMiniportCreateVc;
	struct rcondis_mp_delete_vc CoMiniportDeleteVc;
	struct rcondis_indicate_status CoIndicateStatus;
	struct rcondis_mp_activate_vc_request CoMiniportActivateVc;
	struct rcondis_mp_deactivate_vc_request CoMiniportDeactivateVc;
	struct rcondis_mp_create_vc_complete CoMiniportCreateVcComplete;
	struct rcondis_mp_delete_vc_complete CoMiniportDeleteVcComplete;
	struct rcondis_mp_activate_vc_complete CoMiniportActivateVcComplete;
	struct rcondis_mp_deactivate_vc_complete CoMiniportDeactivateVcComplete;
};

/* Remote NDIS message format */
struct rndis_message {
	u32 NdisMessageType;

	/* Total length of this message, from the beginning */
	/* of the sruct rndis_message, in bytes. */
	u32 MessageLength;

	/* Actual message */
	union rndis_message_container Message;
};

/* Handy macros */

/* get the size of an RNDIS message. Pass in the message type, */
/* struct rndis_set_request, struct rndis_packet for example */
#define RNDIS_MESSAGE_SIZE(Message)				\
	(sizeof(Message) + (sizeof(struct rndis_message) -	\
	 sizeof(union rndis_message_container)))

/* get pointer to info buffer with message pointer */
#define MESSAGE_TO_INFO_BUFFER(Message)				\
	(((unsigned char *)(Message)) + Message->InformationBufferOffset)

/* get pointer to status buffer with message pointer */
#define MESSAGE_TO_STATUS_BUFFER(Message)			\
	(((unsigned char *)(Message)) + Message->StatusBufferOffset)

/* get pointer to OOBD buffer with message pointer */
#define MESSAGE_TO_OOBD_BUFFER(Message)				\
	(((unsigned char *)(Message)) + Message->OOBDataOffset)

/* get pointer to data buffer with message pointer */
#define MESSAGE_TO_DATA_BUFFER(Message)				\
	(((unsigned char *)(Message)) + Message->PerPacketInfoOffset)

/* get pointer to contained message from NDIS_MESSAGE pointer */
#define RNDIS_MESSAGE_PTR_TO_MESSAGE_PTR(RndisMessage)		\
	((void *) &RndisMessage->Message)

/* get pointer to contained message from NDIS_MESSAGE pointer */
#define RNDIS_MESSAGE_RAW_PTR_TO_MESSAGE_PTR(RndisMessage)	\
	((void *) RndisMessage)

#endif /* _RNDIS_H_ */
