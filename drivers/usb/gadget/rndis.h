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

#include <linux/rndis.h>
#include "ndis.h"

#define RNDIS_MAXIMUM_FRAME_SIZE	1518
#define RNDIS_MAX_TOTAL_SIZE		1558

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
