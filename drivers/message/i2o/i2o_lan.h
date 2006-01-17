/*
 *   	i2o_lan.h			I2O LAN Class definitions
 *
 *      I2O LAN CLASS OSM       	May 26th 2000
 *
 *      (C) Copyright 1999, 2000	University of Helsinki,
 *					Department of Computer Science
 *
 *      This code is still under development / test.
 *
 *	Author:		Auvo Häkkinen <Auvo.Hakkinen@cs.Helsinki.FI>
 *			Juha Sievänen <Juha.Sievanen@cs.Helsinki.FI>
 *			Taneli Vähäkangas <Taneli.Vahakangas@cs.Helsinki.FI>
 */

#ifndef _I2O_LAN_H
#define _I2O_LAN_H

/* Default values for tunable parameters first */

#define I2O_LAN_MAX_BUCKETS_OUT 96
#define I2O_LAN_BUCKET_THRESH	18	/* 9 buckets in one message */
#define I2O_LAN_RX_COPYBREAK	200
#define I2O_LAN_TX_TIMEOUT 	(1*HZ)
#define I2O_LAN_TX_BATCH_MODE	2	/* 2=automatic, 1=on, 0=off */
#define I2O_LAN_EVENT_MASK	0	/* 0=None, 0xFFC00002=All */

/* LAN types */
#define I2O_LAN_ETHERNET	0x0030
#define I2O_LAN_100VG		0x0040
#define I2O_LAN_TR		0x0050
#define I2O_LAN_FDDI		0x0060
#define I2O_LAN_FIBRE_CHANNEL	0x0070
#define I2O_LAN_UNKNOWN		0x00000000

/* Connector types */

/* Ethernet */
#define I2O_LAN_AUI		(I2O_LAN_ETHERNET << 4) + 0x00000001
#define I2O_LAN_10BASE5		(I2O_LAN_ETHERNET << 4) + 0x00000002
#define I2O_LAN_FIORL		(I2O_LAN_ETHERNET << 4) + 0x00000003
#define I2O_LAN_10BASE2		(I2O_LAN_ETHERNET << 4) + 0x00000004
#define I2O_LAN_10BROAD36	(I2O_LAN_ETHERNET << 4) + 0x00000005
#define I2O_LAN_10BASE_T	(I2O_LAN_ETHERNET << 4) + 0x00000006
#define I2O_LAN_10BASE_FP	(I2O_LAN_ETHERNET << 4) + 0x00000007
#define I2O_LAN_10BASE_FB	(I2O_LAN_ETHERNET << 4) + 0x00000008
#define I2O_LAN_10BASE_FL	(I2O_LAN_ETHERNET << 4) + 0x00000009
#define I2O_LAN_100BASE_TX	(I2O_LAN_ETHERNET << 4) + 0x0000000A
#define I2O_LAN_100BASE_FX	(I2O_LAN_ETHERNET << 4) + 0x0000000B
#define I2O_LAN_100BASE_T4	(I2O_LAN_ETHERNET << 4) + 0x0000000C
#define I2O_LAN_1000BASE_SX	(I2O_LAN_ETHERNET << 4) + 0x0000000D
#define I2O_LAN_1000BASE_LX	(I2O_LAN_ETHERNET << 4) + 0x0000000E
#define I2O_LAN_1000BASE_CX	(I2O_LAN_ETHERNET << 4) + 0x0000000F
#define I2O_LAN_1000BASE_T	(I2O_LAN_ETHERNET << 4) + 0x00000010

/* AnyLAN */
#define I2O_LAN_100VG_ETHERNET	(I2O_LAN_100VG << 4) + 0x00000001
#define I2O_LAN_100VG_TR	(I2O_LAN_100VG << 4) + 0x00000002

/* Token Ring */
#define I2O_LAN_4MBIT		(I2O_LAN_TR << 4) + 0x00000001
#define I2O_LAN_16MBIT		(I2O_LAN_TR << 4) + 0x00000002

/* FDDI */
#define I2O_LAN_125MBAUD	(I2O_LAN_FDDI << 4) + 0x00000001

/* Fibre Channel */
#define I2O_LAN_POINT_POINT	(I2O_LAN_FIBRE_CHANNEL << 4) + 0x00000001
#define I2O_LAN_ARB_LOOP	(I2O_LAN_FIBRE_CHANNEL << 4) + 0x00000002
#define I2O_LAN_PUBLIC_LOOP	(I2O_LAN_FIBRE_CHANNEL << 4) + 0x00000003
#define I2O_LAN_FABRIC		(I2O_LAN_FIBRE_CHANNEL << 4) + 0x00000004

#define I2O_LAN_EMULATION	0x00000F00
#define I2O_LAN_OTHER		0x00000F01
#define I2O_LAN_DEFAULT		0xFFFFFFFF

/* LAN class functions */

#define LAN_PACKET_SEND		0x3B
#define LAN_SDU_SEND		0x3D
#define LAN_RECEIVE_POST	0x3E
#define LAN_RESET		0x35
#define LAN_SUSPEND		0x37

/* LAN DetailedStatusCode defines */
#define I2O_LAN_DSC_SUCCESS			0x00
#define I2O_LAN_DSC_DEVICE_FAILURE		0x01
#define I2O_LAN_DSC_DESTINATION_NOT_FOUND	0x02
#define	I2O_LAN_DSC_TRANSMIT_ERROR		0x03
#define I2O_LAN_DSC_TRANSMIT_ABORTED		0x04
#define I2O_LAN_DSC_RECEIVE_ERROR		0x05
#define I2O_LAN_DSC_RECEIVE_ABORTED		0x06
#define I2O_LAN_DSC_DMA_ERROR			0x07
#define I2O_LAN_DSC_BAD_PACKET_DETECTED		0x08
#define I2O_LAN_DSC_OUT_OF_MEMORY		0x09
#define I2O_LAN_DSC_BUCKET_OVERRUN		0x0A
#define I2O_LAN_DSC_IOP_INTERNAL_ERROR		0x0B
#define I2O_LAN_DSC_CANCELED			0x0C
#define I2O_LAN_DSC_INVALID_TRANSACTION_CONTEXT	0x0D
#define I2O_LAN_DSC_DEST_ADDRESS_DETECTED	0x0E
#define I2O_LAN_DSC_DEST_ADDRESS_OMITTED	0x0F
#define I2O_LAN_DSC_PARTIAL_PACKET_RETURNED	0x10
#define I2O_LAN_DSC_SUSPENDED			0x11

struct i2o_packet_info {
	u32 offset:24;
	u32 flags:8;
	u32 len:24;
	u32 status:8;
};

struct i2o_bucket_descriptor {
	u32 context;		/* FIXME: 64bit support */
	struct i2o_packet_info packet_info[1];
};

/* Event Indicator Mask Flags for LAN OSM */

#define I2O_LAN_EVT_LINK_DOWN		0x01
#define I2O_LAN_EVT_LINK_UP		0x02
#define I2O_LAN_EVT_MEDIA_CHANGE 	0x04

#include <linux/netdevice.h>
#include <linux/fddidevice.h>

struct i2o_lan_local {
	u8 unit;
	struct i2o_device *i2o_dev;

	struct fddi_statistics stats;	/* see also struct net_device_stats */
	unsigned short (*type_trans) (struct sk_buff *, struct net_device *);
	atomic_t buckets_out;	/* nbr of unused buckets on DDM */
	atomic_t tx_out;	/* outstanding TXes */
	u8 tx_count;		/* packets in one TX message frame */
	u16 tx_max_out;		/* DDM's Tx queue len */
	u8 sgl_max;		/* max SGLs in one message frame */
	u32 m;			/* IOP address of the batch msg frame */

	struct work_struct i2o_batch_send_task;
	int send_active;
	struct sk_buff **i2o_fbl;	/* Free bucket list (to reuse skbs) */
	int i2o_fbl_tail;
	spinlock_t fbl_lock;

	spinlock_t tx_lock;

	u32 max_size_mc_table;	/* max number of multicast addresses */

	/* LAN OSM configurable parameters are here: */

	u16 max_buckets_out;	/* max nbr of buckets to send to DDM */
	u16 bucket_thresh;	/* send more when this many used */
	u16 rx_copybreak;

	u8 tx_batch_mode;	/* Set when using batch mode sends */
	u32 i2o_event_mask;	/* To turn on interesting event flags */
};

#endif				/* _I2O_LAN_H */
