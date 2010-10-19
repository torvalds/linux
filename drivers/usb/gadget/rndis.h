/*
 * RNDIS	Definitions for Remote NDIS
 *
 * Authors:	Benedikt Spranger, Pengutronix
 *		Robert Schwebel, Pengutronix
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		version 2, as published by the Free Software Foundation.
 *
 *		This software was originally developed in conformance with
 *		Microsoft's Remote NDIS Specification License Agreement.
 */

#ifndef _LINUX_RNDIS_H
#define _LINUX_RNDIS_H

#include "ndis.h"

#define RNDIS_MAXIMUM_FRAME_SIZE	1518
#define RNDIS_MAX_TOTAL_SIZE		1558

/* Remote NDIS Versions */
#define RNDIS_MAJOR_VERSION		1
#define RNDIS_MINOR_VERSION		0

/* Status Values */
#define RNDIS_STATUS_SUCCESS		0x00000000U	/* Success           */
#define RNDIS_STATUS_FAILURE		0xC0000001U	/* Unspecified error */
#define RNDIS_STATUS_INVALID_DATA	0xC0010015U	/* Invalid data      */
#define RNDIS_STATUS_NOT_SUPPORTED	0xC00000BBU	/* Unsupported request */
#define RNDIS_STATUS_MEDIA_CONNECT	0x4001000BU	/* Device connected  */
#define RNDIS_STATUS_MEDIA_DISCONNECT	0x4001000CU	/* Device disconnected */
/* For all not specified status messages:
 * RNDIS_STATUS_Xxx -> NDIS_STATUS_Xxx
 */

/* Message Set for Connectionless (802.3) Devices */
#define REMOTE_NDIS_PACKET_MSG		0x00000001U
#define REMOTE_NDIS_INITIALIZE_MSG	0x00000002U	/* Initialize device */
#define REMOTE_NDIS_HALT_MSG		0x00000003U
#define REMOTE_NDIS_QUERY_MSG		0x00000004U
#define REMOTE_NDIS_SET_MSG		0x00000005U
#define REMOTE_NDIS_RESET_MSG		0x00000006U
#define REMOTE_NDIS_INDICATE_STATUS_MSG	0x00000007U
#define REMOTE_NDIS_KEEPALIVE_MSG	0x00000008U

/* Message completion */
#define REMOTE_NDIS_INITIALIZE_CMPLT	0x80000002U
#define REMOTE_NDIS_QUERY_CMPLT		0x80000004U
#define REMOTE_NDIS_SET_CMPLT		0x80000005U
#define REMOTE_NDIS_RESET_CMPLT		0x80000006U
#define REMOTE_NDIS_KEEPALIVE_CMPLT	0x80000008U

/* Device Flags */
#define RNDIS_DF_CONNECTIONLESS		0x00000001U
#define RNDIS_DF_CONNECTION_ORIENTED	0x00000002U

#define RNDIS_MEDIUM_802_3		0x00000000U

/* from drivers/net/sk98lin/h/skgepnmi.h */
#define OID_PNP_CAPABILITIES			0xFD010100
#define OID_PNP_SET_POWER			0xFD010101
#define OID_PNP_QUERY_POWER			0xFD010102
#define OID_PNP_ADD_WAKE_UP_PATTERN		0xFD010103
#define OID_PNP_REMOVE_WAKE_UP_PATTERN		0xFD010104
#define OID_PNP_ENABLE_WAKE_UP			0xFD010106


typedef struct rndis_init_msg_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	RequestID;
	__le32	MajorVersion;
	__le32	MinorVersion;
	__le32	MaxTransferSize;
} rndis_init_msg_type;

typedef struct rndis_init_cmplt_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	RequestID;
	__le32	Status;
	__le32	MajorVersion;
	__le32	MinorVersion;
	__le32	DeviceFlags;
	__le32	Medium;
	__le32	MaxPacketsPerTransfer;
	__le32	MaxTransferSize;
	__le32	PacketAlignmentFactor;
	__le32	AFListOffset;
	__le32	AFListSize;
} rndis_init_cmplt_type;

typedef struct rndis_halt_msg_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	RequestID;
} rndis_halt_msg_type;

typedef struct rndis_query_msg_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	RequestID;
	__le32	OID;
	__le32	InformationBufferLength;
	__le32	InformationBufferOffset;
	__le32	DeviceVcHandle;
} rndis_query_msg_type;

typedef struct rndis_query_cmplt_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	RequestID;
	__le32	Status;
	__le32	InformationBufferLength;
	__le32	InformationBufferOffset;
} rndis_query_cmplt_type;

typedef struct rndis_set_msg_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	RequestID;
	__le32	OID;
	__le32	InformationBufferLength;
	__le32	InformationBufferOffset;
	__le32	DeviceVcHandle;
} rndis_set_msg_type;

typedef struct rndis_set_cmplt_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	RequestID;
	__le32	Status;
} rndis_set_cmplt_type;

typedef struct rndis_reset_msg_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	Reserved;
} rndis_reset_msg_type;

typedef struct rndis_reset_cmplt_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	Status;
	__le32	AddressingReset;
} rndis_reset_cmplt_type;

typedef struct rndis_indicate_status_msg_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	Status;
	__le32	StatusBufferLength;
	__le32	StatusBufferOffset;
} rndis_indicate_status_msg_type;

typedef struct rndis_keepalive_msg_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	RequestID;
} rndis_keepalive_msg_type;

typedef struct rndis_keepalive_cmplt_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	RequestID;
	__le32	Status;
} rndis_keepalive_cmplt_type;

struct rndis_packet_msg_type
{
	__le32	MessageType;
	__le32	MessageLength;
	__le32	DataOffset;
	__le32	DataLength;
	__le32	OOBDataOffset;
	__le32	OOBDataLength;
	__le32	NumOOBDataElements;
	__le32	PerPacketInfoOffset;
	__le32	PerPacketInfoLength;
	__le32	VcHandle;
	__le32	Reserved;
} __attribute__ ((packed));

struct rndis_config_parameter
{
	__le32	ParameterNameOffset;
	__le32	ParameterNameLength;
	__le32	ParameterType;
	__le32	ParameterValueOffset;
	__le32	ParameterValueLength;
};

/* implementation specific */
enum rndis_state
{
	RNDIS_UNINITIALIZED,
	RNDIS_INITIALIZED,
	RNDIS_DATA_INITIALIZED,
};

typedef struct rndis_resp_t
{
	struct list_head	list;
	u8			*buf;
	u32			length;
	int			send;
} rndis_resp_t;

typedef struct rndis_params
{
	u8			confignr;
	u8			used;
	u16			saved_filter;
	enum rndis_state	state;
	u32			medium;
	u32			speed;
	u32			media_state;

	const u8		*host_mac;
	u16			*filter;
	struct net_device	*dev;

	u32			vendorID;
	const char		*vendorDescr;
	void			(*resp_avail)(void *v);
	void			*v;
	struct list_head	resp_queue;
} rndis_params;

/* RNDIS Message parser and other useless functions */
int  rndis_msg_parser (u8 configNr, u8 *buf);
int  rndis_register(void (*resp_avail)(void *v), void *v);
void rndis_deregister (int configNr);
int  rndis_set_param_dev (u8 configNr, struct net_device *dev,
			 u16 *cdc_filter);
int  rndis_set_param_vendor (u8 configNr, u32 vendorID,
			    const char *vendorDescr);
int  rndis_set_param_medium (u8 configNr, u32 medium, u32 speed);
void rndis_add_hdr (struct sk_buff *skb);
int rndis_rm_hdr(struct gether *port, struct sk_buff *skb,
			struct sk_buff_head *list);
u8   *rndis_get_next_response (int configNr, u32 *length);
void rndis_free_response (int configNr, u8 *buf);

void rndis_uninit (int configNr);
int  rndis_signal_connect (int configNr);
int  rndis_signal_disconnect (int configNr);
int  rndis_state (int configNr);
extern void rndis_set_host_mac (int configNr, const u8 *addr);

int rndis_init(void);
void rndis_exit (void);

#endif  /* _LINUX_RNDIS_H */
