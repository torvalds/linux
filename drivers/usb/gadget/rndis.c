/* 
 * RNDIS MSG parser
 * 
 * Version:     $Id: rndis.c,v 1.19 2004/03/25 21:33:46 robert Exp $
 * 
 * Authors:	Benedikt Spranger, Pengutronix
 * 		Robert Schwebel, Pengutronix
 * 
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              version 2, as published by the Free Software Foundation. 
 * 
 *		This software was originally developed in conformance with
 *		Microsoft's Remote NDIS Specification License Agreement.
 *              
 * 03/12/2004 Kai-Uwe Bloem <linux-development@auerswald.de>
 *		Fixed message length bug in init_response
 * 
 * 03/25/2004 Kai-Uwe Bloem <linux-development@auerswald.de>
 * 		Fixed rndis_rm_hdr length bug.
 *
 * Copyright (C) 2004 by David Brownell
 *		updates to merge with Linux 2.6, better match RNDIS spec
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>

#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/unaligned.h>


#undef	RNDIS_PM
#undef	RNDIS_WAKEUP
#undef	VERBOSE

#include "rndis.h"


/* The driver for your USB chip needs to support ep0 OUT to work with
 * RNDIS, plus all three CDC Ethernet endpoints (interrupt not optional).
 *
 * Windows hosts need an INF file like Documentation/usb/linux.inf
 * and will be happier if you provide the host_addr module parameter.
 */

#if 0
#define DEBUG(str,args...) do { \
	if (rndis_debug) \
		printk(KERN_DEBUG str , ## args ); \
	} while (0)
static int rndis_debug = 0;

module_param (rndis_debug, int, 0);
MODULE_PARM_DESC (rndis_debug, "enable debugging");

#else

#define rndis_debug		0
#define DEBUG(str,args...)	do{}while(0)
#endif

#define RNDIS_MAX_CONFIGS	1


static rndis_params rndis_per_dev_params [RNDIS_MAX_CONFIGS];

/* Driver Version */
static const __le32 rndis_driver_version = __constant_cpu_to_le32 (1);

/* Function Prototypes */
static rndis_resp_t *rndis_add_response (int configNr, u32 length);


/* supported OIDs */
static const u32 oid_supported_list [] = 
{
	/* the general stuff */
	OID_GEN_SUPPORTED_LIST,
	OID_GEN_HARDWARE_STATUS,
	OID_GEN_MEDIA_SUPPORTED,
	OID_GEN_MEDIA_IN_USE,
	OID_GEN_MAXIMUM_FRAME_SIZE,
	OID_GEN_LINK_SPEED,
	OID_GEN_TRANSMIT_BLOCK_SIZE,
	OID_GEN_RECEIVE_BLOCK_SIZE,
	OID_GEN_VENDOR_ID,
	OID_GEN_VENDOR_DESCRIPTION,
	OID_GEN_VENDOR_DRIVER_VERSION,
	OID_GEN_CURRENT_PACKET_FILTER,
	OID_GEN_MAXIMUM_TOTAL_SIZE,
	OID_GEN_MEDIA_CONNECT_STATUS,
	OID_GEN_PHYSICAL_MEDIUM,
#if 0
	OID_GEN_RNDIS_CONFIG_PARAMETER,
#endif
	
	/* the statistical stuff */
	OID_GEN_XMIT_OK,
	OID_GEN_RCV_OK,
	OID_GEN_XMIT_ERROR,
	OID_GEN_RCV_ERROR,
	OID_GEN_RCV_NO_BUFFER,
#ifdef	RNDIS_OPTIONAL_STATS
	OID_GEN_DIRECTED_BYTES_XMIT,
	OID_GEN_DIRECTED_FRAMES_XMIT,
	OID_GEN_MULTICAST_BYTES_XMIT,
	OID_GEN_MULTICAST_FRAMES_XMIT,
	OID_GEN_BROADCAST_BYTES_XMIT,
	OID_GEN_BROADCAST_FRAMES_XMIT,
	OID_GEN_DIRECTED_BYTES_RCV,
	OID_GEN_DIRECTED_FRAMES_RCV,
	OID_GEN_MULTICAST_BYTES_RCV,
	OID_GEN_MULTICAST_FRAMES_RCV,
	OID_GEN_BROADCAST_BYTES_RCV,
	OID_GEN_BROADCAST_FRAMES_RCV,
	OID_GEN_RCV_CRC_ERROR,
	OID_GEN_TRANSMIT_QUEUE_LENGTH,
#endif	/* RNDIS_OPTIONAL_STATS */

    	/* mandatory 802.3 */
	/* the general stuff */
	OID_802_3_PERMANENT_ADDRESS,
	OID_802_3_CURRENT_ADDRESS,
	OID_802_3_MULTICAST_LIST,
	OID_802_3_MAC_OPTIONS,
	OID_802_3_MAXIMUM_LIST_SIZE,
	
	/* the statistical stuff */
	OID_802_3_RCV_ERROR_ALIGNMENT,
	OID_802_3_XMIT_ONE_COLLISION,
	OID_802_3_XMIT_MORE_COLLISIONS,
#ifdef	RNDIS_OPTIONAL_STATS
	OID_802_3_XMIT_DEFERRED,
	OID_802_3_XMIT_MAX_COLLISIONS,
	OID_802_3_RCV_OVERRUN,
	OID_802_3_XMIT_UNDERRUN,
	OID_802_3_XMIT_HEARTBEAT_FAILURE,
	OID_802_3_XMIT_TIMES_CRS_LOST,
	OID_802_3_XMIT_LATE_COLLISIONS,
#endif	/* RNDIS_OPTIONAL_STATS */

#ifdef	RNDIS_PM
	/* PM and wakeup are mandatory for USB: */

	/* power management */
	OID_PNP_CAPABILITIES,
	OID_PNP_QUERY_POWER,
	OID_PNP_SET_POWER,

#ifdef	RNDIS_WAKEUP
	/* wake up host */
	OID_PNP_ENABLE_WAKE_UP,
	OID_PNP_ADD_WAKE_UP_PATTERN,
	OID_PNP_REMOVE_WAKE_UP_PATTERN,
#endif	/* RNDIS_WAKEUP */
#endif	/* RNDIS_PM */
};


/* NDIS Functions */
static int
gen_ndis_query_resp (int configNr, u32 OID, u8 *buf, unsigned buf_len,
		rndis_resp_t *r)
{
	int 			retval = -ENOTSUPP;
	u32 			length = 4;	/* usually */
	__le32			*outbuf;
	int			i, count;
	rndis_query_cmplt_type	*resp;

	if (!r) return -ENOMEM;
	resp = (rndis_query_cmplt_type *) r->buf;

	if (!resp) return -ENOMEM;

	if (buf_len && rndis_debug > 1) {
		DEBUG("query OID %08x value, len %d:\n", OID, buf_len);
		for (i = 0; i < buf_len; i += 16) {
			DEBUG ("%03d: %08x %08x %08x %08x\n", i,
				le32_to_cpup((__le32 *)&buf[i]),
				le32_to_cpup((__le32 *)&buf[i + 4]),
				le32_to_cpup((__le32 *)&buf[i + 8]),
				le32_to_cpup((__le32 *)&buf[i + 12]));
		}
	}

	/* response goes here, right after the header */
	outbuf = (__le32 *) &resp[1];
	resp->InformationBufferOffset = __constant_cpu_to_le32 (16);

	switch (OID) {

	/* general oids (table 4-1) */

	/* mandatory */
	case OID_GEN_SUPPORTED_LIST:
		DEBUG ("%s: OID_GEN_SUPPORTED_LIST\n", __FUNCTION__);
		length = sizeof (oid_supported_list);
		count  = length / sizeof (u32);
		for (i = 0; i < count; i++)
			outbuf[i] = cpu_to_le32 (oid_supported_list[i]);
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_HARDWARE_STATUS:
		DEBUG("%s: OID_GEN_HARDWARE_STATUS\n", __FUNCTION__);
		/* Bogus question! 
		 * Hardware must be ready to receive high level protocols.
		 * BTW: 
		 * reddite ergo quae sunt Caesaris Caesari
		 * et quae sunt Dei Deo!
		 */
		*outbuf = __constant_cpu_to_le32 (0);
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_MEDIA_SUPPORTED:
		DEBUG("%s: OID_GEN_MEDIA_SUPPORTED\n", __FUNCTION__);
		*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr].medium);
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_MEDIA_IN_USE:
		DEBUG("%s: OID_GEN_MEDIA_IN_USE\n", __FUNCTION__);
		/* one medium, one transport... (maybe you do it better) */
		*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr].medium);
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_MAXIMUM_FRAME_SIZE:
		DEBUG("%s: OID_GEN_MAXIMUM_FRAME_SIZE\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			*outbuf = cpu_to_le32 (
				rndis_per_dev_params [configNr].dev->mtu);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_LINK_SPEED:
		if (rndis_debug > 1)
			DEBUG("%s: OID_GEN_LINK_SPEED\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].media_state
				== NDIS_MEDIA_STATE_DISCONNECTED)
			*outbuf = __constant_cpu_to_le32 (0);
		else
			*outbuf = cpu_to_le32 (
				rndis_per_dev_params [configNr].speed);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_TRANSMIT_BLOCK_SIZE:
		DEBUG("%s: OID_GEN_TRANSMIT_BLOCK_SIZE\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			*outbuf = cpu_to_le32 (
				rndis_per_dev_params [configNr].dev->mtu);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_RECEIVE_BLOCK_SIZE:
		DEBUG("%s: OID_GEN_RECEIVE_BLOCK_SIZE\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			*outbuf = cpu_to_le32 (
				rndis_per_dev_params [configNr].dev->mtu);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_VENDOR_ID:
		DEBUG("%s: OID_GEN_VENDOR_ID\n", __FUNCTION__);
		*outbuf = cpu_to_le32 (
			rndis_per_dev_params [configNr].vendorID);
		retval = 0;
		break;
		
	/* mandatory */
	case OID_GEN_VENDOR_DESCRIPTION:
		DEBUG("%s: OID_GEN_VENDOR_DESCRIPTION\n", __FUNCTION__);
		length = strlen (rndis_per_dev_params [configNr].vendorDescr);
		memcpy (outbuf,
			rndis_per_dev_params [configNr].vendorDescr, length);
		retval = 0;
		break;

	case OID_GEN_VENDOR_DRIVER_VERSION:
		DEBUG("%s: OID_GEN_VENDOR_DRIVER_VERSION\n", __FUNCTION__);
		/* Created as LE */
		*outbuf = rndis_driver_version;
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_CURRENT_PACKET_FILTER:
		DEBUG("%s: OID_GEN_CURRENT_PACKET_FILTER\n", __FUNCTION__);
		*outbuf = cpu_to_le32 (*rndis_per_dev_params[configNr].filter);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_MAXIMUM_TOTAL_SIZE:
		DEBUG("%s: OID_GEN_MAXIMUM_TOTAL_SIZE\n", __FUNCTION__);
		*outbuf = __constant_cpu_to_le32(RNDIS_MAX_TOTAL_SIZE);
		retval = 0;
		break;

	/* mandatory */
	case OID_GEN_MEDIA_CONNECT_STATUS:
		if (rndis_debug > 1)
			DEBUG("%s: OID_GEN_MEDIA_CONNECT_STATUS\n", __FUNCTION__);
		*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
						.media_state);
		retval = 0;
		break;

	case OID_GEN_PHYSICAL_MEDIUM:
		DEBUG("%s: OID_GEN_PHYSICAL_MEDIUM\n", __FUNCTION__);
		*outbuf = __constant_cpu_to_le32 (0);
		retval = 0;
		break;

	/* The RNDIS specification is incomplete/wrong.   Some versions
	 * of MS-Windows expect OIDs that aren't specified there.  Other
	 * versions emit undefined RNDIS messages. DOCUMENT ALL THESE!
	 */
	case OID_GEN_MAC_OPTIONS:		/* from WinME */
		DEBUG("%s: OID_GEN_MAC_OPTIONS\n", __FUNCTION__);
		*outbuf = __constant_cpu_to_le32(
			  NDIS_MAC_OPTION_RECEIVE_SERIALIZED
			| NDIS_MAC_OPTION_FULL_DUPLEX);
		retval = 0;
		break;

	/* statistics OIDs (table 4-2) */

	/* mandatory */
	case OID_GEN_XMIT_OK:
		if (rndis_debug > 1)
			DEBUG("%s: OID_GEN_XMIT_OK\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (
			    rndis_per_dev_params [configNr].stats->tx_packets - 
			    rndis_per_dev_params [configNr].stats->tx_errors -
			    rndis_per_dev_params [configNr].stats->tx_dropped);
			retval = 0;
		}
		break;

	/* mandatory */
	case OID_GEN_RCV_OK:
		if (rndis_debug > 1)
			DEBUG("%s: OID_GEN_RCV_OK\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (
			    rndis_per_dev_params [configNr].stats->rx_packets - 
			    rndis_per_dev_params [configNr].stats->rx_errors -
			    rndis_per_dev_params [configNr].stats->rx_dropped);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_XMIT_ERROR:
		if (rndis_debug > 1)
			DEBUG("%s: OID_GEN_XMIT_ERROR\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->tx_errors);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_RCV_ERROR:
		if (rndis_debug > 1)
			DEBUG("%s: OID_GEN_RCV_ERROR\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->rx_errors);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_GEN_RCV_NO_BUFFER:
		DEBUG("%s: OID_GEN_RCV_NO_BUFFER\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->rx_dropped);
			retval = 0;
		}
		break;

#ifdef	RNDIS_OPTIONAL_STATS
	case OID_GEN_DIRECTED_BYTES_XMIT:
		DEBUG("%s: OID_GEN_DIRECTED_BYTES_XMIT\n", __FUNCTION__);
		/* 
		 * Aunt Tilly's size of shoes
		 * minus antarctica count of penguins
		 * divided by weight of Alpha Centauri
		 */
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (
				(rndis_per_dev_params [configNr]
					.stats->tx_packets - 
				 rndis_per_dev_params [configNr]
					 .stats->tx_errors -
				 rndis_per_dev_params [configNr]
					 .stats->tx_dropped)
				* 123);
			retval = 0;
		}
		break;
		
	case OID_GEN_DIRECTED_FRAMES_XMIT:
		DEBUG("%s: OID_GEN_DIRECTED_FRAMES_XMIT\n", __FUNCTION__);
		/* dito */
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (
				(rndis_per_dev_params [configNr]
					.stats->tx_packets - 
				 rndis_per_dev_params [configNr]
					 .stats->tx_errors -
				 rndis_per_dev_params [configNr]
					 .stats->tx_dropped)
				/ 123);
			retval = 0;
		}
		break;
		
	case OID_GEN_MULTICAST_BYTES_XMIT:
		DEBUG("%s: OID_GEN_MULTICAST_BYTES_XMIT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->multicast*1234);
			retval = 0;
		}
		break;
		
	case OID_GEN_MULTICAST_FRAMES_XMIT:
		DEBUG("%s: OID_GEN_MULTICAST_FRAMES_XMIT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->multicast);
			retval = 0;
		}
		break;
		
	case OID_GEN_BROADCAST_BYTES_XMIT:
		DEBUG("%s: OID_GEN_BROADCAST_BYTES_XMIT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->tx_packets/42*255);
			retval = 0;
		}
		break;
		
	case OID_GEN_BROADCAST_FRAMES_XMIT:
		DEBUG("%s: OID_GEN_BROADCAST_FRAMES_XMIT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->tx_packets/42);
			retval = 0;
		}
		break;
		
	case OID_GEN_DIRECTED_BYTES_RCV:
		DEBUG("%s: OID_GEN_DIRECTED_BYTES_RCV\n", __FUNCTION__);
		*outbuf = __constant_cpu_to_le32 (0);
		retval = 0;
		break;
		
	case OID_GEN_DIRECTED_FRAMES_RCV:
		DEBUG("%s: OID_GEN_DIRECTED_FRAMES_RCV\n", __FUNCTION__);
		*outbuf = __constant_cpu_to_le32 (0);
		retval = 0;
		break;
		
	case OID_GEN_MULTICAST_BYTES_RCV:
		DEBUG("%s: OID_GEN_MULTICAST_BYTES_RCV\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->multicast * 1111);
			retval = 0;
		}
		break;
		
	case OID_GEN_MULTICAST_FRAMES_RCV:
		DEBUG("%s: OID_GEN_MULTICAST_FRAMES_RCV\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->multicast);
			retval = 0;
		}
		break;
		
	case OID_GEN_BROADCAST_BYTES_RCV:
		DEBUG("%s: OID_GEN_BROADCAST_BYTES_RCV\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->rx_packets/42*255);
			retval = 0;
		}
		break;
		
	case OID_GEN_BROADCAST_FRAMES_RCV:
		DEBUG("%s: OID_GEN_BROADCAST_FRAMES_RCV\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->rx_packets/42);
			retval = 0;
		}
		break;
		
	case OID_GEN_RCV_CRC_ERROR:
		DEBUG("%s: OID_GEN_RCV_CRC_ERROR\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->rx_crc_errors);
			retval = 0;
		}
		break;
		
	case OID_GEN_TRANSMIT_QUEUE_LENGTH:
		DEBUG("%s: OID_GEN_TRANSMIT_QUEUE_LENGTH\n", __FUNCTION__);
		*outbuf = __constant_cpu_to_le32 (0);
		retval = 0;
		break;
#endif	/* RNDIS_OPTIONAL_STATS */

	/* ieee802.3 OIDs (table 4-3) */

	/* mandatory */
	case OID_802_3_PERMANENT_ADDRESS:
		DEBUG("%s: OID_802_3_PERMANENT_ADDRESS\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			length = ETH_ALEN;
			memcpy (outbuf,
				rndis_per_dev_params [configNr].host_mac,
				length);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_802_3_CURRENT_ADDRESS:
		DEBUG("%s: OID_802_3_CURRENT_ADDRESS\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].dev) {
			length = ETH_ALEN;
			memcpy (outbuf,
				rndis_per_dev_params [configNr].host_mac,
				length);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_802_3_MULTICAST_LIST:
		DEBUG("%s: OID_802_3_MULTICAST_LIST\n", __FUNCTION__);
		/* Multicast base address only */
		*outbuf = __constant_cpu_to_le32 (0xE0000000);
		retval = 0;
		break;
		
	/* mandatory */
	case OID_802_3_MAXIMUM_LIST_SIZE:
		DEBUG("%s: OID_802_3_MAXIMUM_LIST_SIZE\n", __FUNCTION__);
		/* Multicast base address only */
		*outbuf = __constant_cpu_to_le32 (1);
		retval = 0;
		break;
		
	case OID_802_3_MAC_OPTIONS:
		DEBUG("%s: OID_802_3_MAC_OPTIONS\n", __FUNCTION__);
		break;

	/* ieee802.3 statistics OIDs (table 4-4) */

	/* mandatory */
	case OID_802_3_RCV_ERROR_ALIGNMENT:
		DEBUG("%s: OID_802_3_RCV_ERROR_ALIGNMENT\n", __FUNCTION__);
		if (rndis_per_dev_params [configNr].stats) {
			*outbuf = cpu_to_le32 (rndis_per_dev_params [configNr]
					.stats->rx_frame_errors);
			retval = 0;
		}
		break;
		
	/* mandatory */
	case OID_802_3_XMIT_ONE_COLLISION:
		DEBUG("%s: OID_802_3_XMIT_ONE_COLLISION\n", __FUNCTION__);
		*outbuf = __constant_cpu_to_le32 (0);
		retval = 0;
		break;
		
	/* mandatory */
	case OID_802_3_XMIT_MORE_COLLISIONS:
		DEBUG("%s: OID_802_3_XMIT_MORE_COLLISIONS\n", __FUNCTION__);
		*outbuf = __constant_cpu_to_le32 (0);
		retval = 0;
		break;
		
#ifdef	RNDIS_OPTIONAL_STATS
	case OID_802_3_XMIT_DEFERRED:
		DEBUG("%s: OID_802_3_XMIT_DEFERRED\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_MAX_COLLISIONS:
		DEBUG("%s: OID_802_3_XMIT_MAX_COLLISIONS\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_RCV_OVERRUN:
		DEBUG("%s: OID_802_3_RCV_OVERRUN\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_UNDERRUN:
		DEBUG("%s: OID_802_3_XMIT_UNDERRUN\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_HEARTBEAT_FAILURE:
		DEBUG("%s: OID_802_3_XMIT_HEARTBEAT_FAILURE\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_TIMES_CRS_LOST:
		DEBUG("%s: OID_802_3_XMIT_TIMES_CRS_LOST\n", __FUNCTION__);
		/* TODO */
		break;
		
	case OID_802_3_XMIT_LATE_COLLISIONS:
		DEBUG("%s: OID_802_3_XMIT_LATE_COLLISIONS\n", __FUNCTION__);
		/* TODO */
		break;		
#endif	/* RNDIS_OPTIONAL_STATS */

#ifdef	RNDIS_PM
	/* power management OIDs (table 4-5) */
	case OID_PNP_CAPABILITIES:
		DEBUG("%s: OID_PNP_CAPABILITIES\n", __FUNCTION__);

		/* for now, no wakeup capabilities */
		length = sizeof (struct NDIS_PNP_CAPABILITIES);
		memset(outbuf, 0, length);
		retval = 0;
		break;
	case OID_PNP_QUERY_POWER:
		DEBUG("%s: OID_PNP_QUERY_POWER D%d\n", __FUNCTION__,
				le32_to_cpup((__le32 *) buf) - 1);
		/* only suspend is a real power state, and
		 * it can't be entered by OID_PNP_SET_POWER...
		 */
		length = 0;
		retval = 0;
		break;
#endif

	default:
		printk (KERN_WARNING "%s: query unknown OID 0x%08X\n", 
			 __FUNCTION__, OID);
	}
	if (retval < 0)
		length = 0;
	
	resp->InformationBufferLength = cpu_to_le32 (length);
	r->length = length + sizeof *resp;
	resp->MessageLength = cpu_to_le32 (r->length);
	return retval;
}

static int gen_ndis_set_resp (u8 configNr, u32 OID, u8 *buf, u32 buf_len, 
			      rndis_resp_t *r)
{
	rndis_set_cmplt_type		*resp;
	int 				i, retval = -ENOTSUPP;
	struct rndis_params		*params;

	if (!r)
		return -ENOMEM;
	resp = (rndis_set_cmplt_type *) r->buf;
	if (!resp)
		return -ENOMEM;

	if (buf_len && rndis_debug > 1) {
		DEBUG("set OID %08x value, len %d:\n", OID, buf_len);
		for (i = 0; i < buf_len; i += 16) {
			DEBUG ("%03d: %08x %08x %08x %08x\n", i,
				le32_to_cpup((__le32 *)&buf[i]),
				le32_to_cpup((__le32 *)&buf[i + 4]),
				le32_to_cpup((__le32 *)&buf[i + 8]),
				le32_to_cpup((__le32 *)&buf[i + 12]));
		}
	}

	params = &rndis_per_dev_params [configNr];
	switch (OID) {
	case OID_GEN_CURRENT_PACKET_FILTER:

		/* these NDIS_PACKET_TYPE_* bitflags are shared with
		 * cdc_filter; it's not RNDIS-specific
		 * NDIS_PACKET_TYPE_x == USB_CDC_PACKET_TYPE_x for x in:
		 *	PROMISCUOUS, DIRECTED,
		 *	MULTICAST, ALL_MULTICAST, BROADCAST
		 */
		*params->filter = (u16) le32_to_cpup((__le32 *)buf);
		DEBUG("%s: OID_GEN_CURRENT_PACKET_FILTER %08x\n",
			__FUNCTION__, *params->filter);

		/* this call has a significant side effect:  it's
		 * what makes the packet flow start and stop, like
		 * activating the CDC Ethernet altsetting.
		 */
#ifdef	RNDIS_PM
update_linkstate:
#endif
		retval = 0;
		if (*params->filter) {
			params->state = RNDIS_DATA_INITIALIZED;
			netif_carrier_on(params->dev);
			if (netif_running(params->dev))
				netif_wake_queue (params->dev);
		} else {
			params->state = RNDIS_INITIALIZED;
			netif_carrier_off (params->dev);
			netif_stop_queue (params->dev);
		}
		break;
		
	case OID_802_3_MULTICAST_LIST:
		/* I think we can ignore this */		
		DEBUG("%s: OID_802_3_MULTICAST_LIST\n", __FUNCTION__);
		retval = 0;
		break;
#if 0
	case OID_GEN_RNDIS_CONFIG_PARAMETER:
		{
		struct rndis_config_parameter	*param;
		param = (struct rndis_config_parameter *) buf;
		DEBUG("%s: OID_GEN_RNDIS_CONFIG_PARAMETER '%*s'\n",
			__FUNCTION__,
			min(cpu_to_le32(param->ParameterNameLength),80),
			buf + param->ParameterNameOffset);
		retval = 0;
		}
		break;
#endif

#ifdef	RNDIS_PM
	case OID_PNP_SET_POWER:
		/* The only real power state is USB suspend, and RNDIS requests
		 * can't enter it; this one isn't really about power.  After
		 * resuming, Windows forces a reset, and then SET_POWER D0.
		 * FIXME ... then things go batty; Windows wedges itself.
		 */
		i = le32_to_cpup((__force __le32 *)buf);
		DEBUG("%s: OID_PNP_SET_POWER D%d\n", __FUNCTION__, i - 1);
		switch (i) {
		case NdisDeviceStateD0:
			*params->filter = params->saved_filter;
			goto update_linkstate;
		case NdisDeviceStateD3:
		case NdisDeviceStateD2:
		case NdisDeviceStateD1:
			params->saved_filter = *params->filter;
			retval = 0;
			break;
		}
		break;

#ifdef	RNDIS_WAKEUP
	// no wakeup support advertised, so wakeup OIDs always fail:
	//  - OID_PNP_ENABLE_WAKE_UP
	//  - OID_PNP_{ADD,REMOVE}_WAKE_UP_PATTERN
#endif

#endif	/* RNDIS_PM */

	default:
		printk (KERN_WARNING "%s: set unknown OID 0x%08X, size %d\n", 
			 __FUNCTION__, OID, buf_len);
	}
	
	return retval;
}

/* 
 * Response Functions 
 */

static int rndis_init_response (int configNr, rndis_init_msg_type *buf)
{
	rndis_init_cmplt_type	*resp; 
	rndis_resp_t            *r;
	
	if (!rndis_per_dev_params [configNr].dev) return -ENOTSUPP;
	
	r = rndis_add_response (configNr, sizeof (rndis_init_cmplt_type));
	if (!r)
		return -ENOMEM;
	resp = (rndis_init_cmplt_type *) r->buf;
	
	resp->MessageType = __constant_cpu_to_le32 (
			REMOTE_NDIS_INITIALIZE_CMPLT);
	resp->MessageLength = __constant_cpu_to_le32 (52);
	resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
	resp->Status = __constant_cpu_to_le32 (RNDIS_STATUS_SUCCESS);
	resp->MajorVersion = __constant_cpu_to_le32 (RNDIS_MAJOR_VERSION);
	resp->MinorVersion = __constant_cpu_to_le32 (RNDIS_MINOR_VERSION);
	resp->DeviceFlags = __constant_cpu_to_le32 (RNDIS_DF_CONNECTIONLESS);
	resp->Medium = __constant_cpu_to_le32 (RNDIS_MEDIUM_802_3);
	resp->MaxPacketsPerTransfer = __constant_cpu_to_le32 (1);
	resp->MaxTransferSize = cpu_to_le32 (
		  rndis_per_dev_params [configNr].dev->mtu
		+ sizeof (struct ethhdr)
		+ sizeof (struct rndis_packet_msg_type)
		+ 22);
	resp->PacketAlignmentFactor = __constant_cpu_to_le32 (0);
	resp->AFListOffset = __constant_cpu_to_le32 (0);
	resp->AFListSize = __constant_cpu_to_le32 (0);
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);
	
	return 0;
}

static int rndis_query_response (int configNr, rndis_query_msg_type *buf)
{
	rndis_query_cmplt_type *resp;
	rndis_resp_t            *r;
	
	// DEBUG("%s: OID = %08X\n", __FUNCTION__, cpu_to_le32(buf->OID));
	if (!rndis_per_dev_params [configNr].dev) return -ENOTSUPP;
	
	/* 
	 * we need more memory: 
	 * oid_supported_list is the largest answer 
	 */
	r = rndis_add_response (configNr, sizeof (oid_supported_list));
	if (!r)
		return -ENOMEM;
	resp = (rndis_query_cmplt_type *) r->buf;
	
	resp->MessageType = __constant_cpu_to_le32 (REMOTE_NDIS_QUERY_CMPLT);
	resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
  	
	if (gen_ndis_query_resp (configNr, le32_to_cpu (buf->OID),
			le32_to_cpu(buf->InformationBufferOffset)
					+ 8 + (u8 *) buf,
			le32_to_cpu(buf->InformationBufferLength),
			r)) {
		/* OID not supported */
		resp->Status = __constant_cpu_to_le32 (
				RNDIS_STATUS_NOT_SUPPORTED);
		resp->MessageLength = __constant_cpu_to_le32 (sizeof *resp);
		resp->InformationBufferLength = __constant_cpu_to_le32 (0);
		resp->InformationBufferOffset = __constant_cpu_to_le32 (0);
	} else
		resp->Status = __constant_cpu_to_le32 (RNDIS_STATUS_SUCCESS);
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);
	return 0;
}

static int rndis_set_response (int configNr, rndis_set_msg_type *buf)
{
	u32			BufLength, BufOffset;
	rndis_set_cmplt_type	*resp;
	rndis_resp_t		*r;
	
	r = rndis_add_response (configNr, sizeof (rndis_set_cmplt_type));
	if (!r)
		return -ENOMEM;
	resp = (rndis_set_cmplt_type *) r->buf;

	BufLength = le32_to_cpu (buf->InformationBufferLength);
	BufOffset = le32_to_cpu (buf->InformationBufferOffset);

#ifdef	VERBOSE
	DEBUG("%s: Length: %d\n", __FUNCTION__, BufLength);
	DEBUG("%s: Offset: %d\n", __FUNCTION__, BufOffset);
	DEBUG("%s: InfoBuffer: ", __FUNCTION__);
	
	for (i = 0; i < BufLength; i++) {
		DEBUG ("%02x ", *(((u8 *) buf) + i + 8 + BufOffset));
	}
	
	DEBUG ("\n");
#endif
	
	resp->MessageType = __constant_cpu_to_le32 (REMOTE_NDIS_SET_CMPLT);
	resp->MessageLength = __constant_cpu_to_le32 (16);
	resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
	if (gen_ndis_set_resp (configNr, le32_to_cpu (buf->OID), 
			       ((u8 *) buf) + 8 + BufOffset, BufLength, r))
	    resp->Status = __constant_cpu_to_le32 (RNDIS_STATUS_NOT_SUPPORTED);
	else resp->Status = __constant_cpu_to_le32 (RNDIS_STATUS_SUCCESS);
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);
	
	return 0;
}

static int rndis_reset_response (int configNr, rndis_reset_msg_type *buf)
{
	rndis_reset_cmplt_type	*resp;
	rndis_resp_t		*r;
	
	r = rndis_add_response (configNr, sizeof (rndis_reset_cmplt_type));
	if (!r)
		return -ENOMEM;
	resp = (rndis_reset_cmplt_type *) r->buf;
	
	resp->MessageType = __constant_cpu_to_le32 (REMOTE_NDIS_RESET_CMPLT);
	resp->MessageLength = __constant_cpu_to_le32 (16);
	resp->Status = __constant_cpu_to_le32 (RNDIS_STATUS_SUCCESS);
	/* resent information */
	resp->AddressingReset = __constant_cpu_to_le32 (1);
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);

	return 0;
}

static int rndis_keepalive_response (int configNr,
				     rndis_keepalive_msg_type *buf)
{
	rndis_keepalive_cmplt_type	*resp;
	rndis_resp_t			*r;

	/* host "should" check only in RNDIS_DATA_INITIALIZED state */

	r = rndis_add_response (configNr, sizeof (rndis_keepalive_cmplt_type));
	if (!r)
		return -ENOMEM;
	resp = (rndis_keepalive_cmplt_type *) r->buf;
		
	resp->MessageType = __constant_cpu_to_le32 (
			REMOTE_NDIS_KEEPALIVE_CMPLT);
	resp->MessageLength = __constant_cpu_to_le32 (16);
	resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
	resp->Status = __constant_cpu_to_le32 (RNDIS_STATUS_SUCCESS);
	
	if (rndis_per_dev_params [configNr].ack)
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);
	
	return 0;
}


/* 
 * Device to Host Comunication 
 */
static int rndis_indicate_status_msg (int configNr, u32 status)
{
	rndis_indicate_status_msg_type	*resp;	
	rndis_resp_t			*r;
	
	if (rndis_per_dev_params [configNr].state == RNDIS_UNINITIALIZED)
	    return -ENOTSUPP;
	
	r = rndis_add_response (configNr, 
				sizeof (rndis_indicate_status_msg_type));
	if (!r)
		return -ENOMEM;
	resp = (rndis_indicate_status_msg_type *) r->buf;
	
	resp->MessageType = __constant_cpu_to_le32 (
			REMOTE_NDIS_INDICATE_STATUS_MSG);
	resp->MessageLength = __constant_cpu_to_le32 (20);
	resp->Status = cpu_to_le32 (status);
	resp->StatusBufferLength = __constant_cpu_to_le32 (0);
	resp->StatusBufferOffset = __constant_cpu_to_le32 (0);
	
	if (rndis_per_dev_params [configNr].ack) 
	    rndis_per_dev_params [configNr].ack (
	    		rndis_per_dev_params [configNr].dev);
	return 0;
}

int rndis_signal_connect (int configNr)
{
	rndis_per_dev_params [configNr].media_state
			= NDIS_MEDIA_STATE_CONNECTED;
	return rndis_indicate_status_msg (configNr, 
					  RNDIS_STATUS_MEDIA_CONNECT);
}

int rndis_signal_disconnect (int configNr)
{
	rndis_per_dev_params [configNr].media_state
			= NDIS_MEDIA_STATE_DISCONNECTED;
	return rndis_indicate_status_msg (configNr,
					  RNDIS_STATUS_MEDIA_DISCONNECT);
}

void rndis_uninit (int configNr)
{
	u8 *buf;
	u32 length;

	if (configNr >= RNDIS_MAX_CONFIGS)
		return;
	rndis_per_dev_params [configNr].used = 0;
	rndis_per_dev_params [configNr].state = RNDIS_UNINITIALIZED;

	/* drain the response queue */
	while ((buf = rndis_get_next_response(configNr, &length)))
		rndis_free_response(configNr, buf);
}

void rndis_set_host_mac (int configNr, const u8 *addr)
{
	rndis_per_dev_params [configNr].host_mac = addr;
}

/* 
 * Message Parser 
 */
int rndis_msg_parser (u8 configNr, u8 *buf)
{
	u32 MsgType, MsgLength;
	__le32 *tmp;
	struct rndis_params		*params;
	
	if (!buf)
		return -ENOMEM;
	
	tmp = (__le32 *) buf; 
	MsgType   = le32_to_cpup(tmp++);
	MsgLength = le32_to_cpup(tmp++);
	
	if (configNr >= RNDIS_MAX_CONFIGS)
		return -ENOTSUPP;
	params = &rndis_per_dev_params [configNr];
	
	/* NOTE: RNDIS is *EXTREMELY* chatty ... Windows constantly polls for
	 * rx/tx statistics and link status, in addition to KEEPALIVE traffic
	 * and normal HC level polling to see if there's any IN traffic.
	 */

	/* For USB: responses may take up to 10 seconds */
	switch (MsgType) {
	case REMOTE_NDIS_INITIALIZE_MSG:
		DEBUG("%s: REMOTE_NDIS_INITIALIZE_MSG\n", 
			__FUNCTION__ );
		params->state = RNDIS_INITIALIZED;
		return  rndis_init_response (configNr,
					     (rndis_init_msg_type *) buf);
		
	case REMOTE_NDIS_HALT_MSG:
		DEBUG("%s: REMOTE_NDIS_HALT_MSG\n",
			__FUNCTION__ );
		params->state = RNDIS_UNINITIALIZED;
		if (params->dev) {
			netif_carrier_off (params->dev);
			netif_stop_queue (params->dev);
		}
		return 0;
		
	case REMOTE_NDIS_QUERY_MSG:
		return rndis_query_response (configNr, 
					     (rndis_query_msg_type *) buf);
		
	case REMOTE_NDIS_SET_MSG:
		return rndis_set_response (configNr, 
					   (rndis_set_msg_type *) buf);
		
	case REMOTE_NDIS_RESET_MSG:
		DEBUG("%s: REMOTE_NDIS_RESET_MSG\n", 
			__FUNCTION__ );
		return rndis_reset_response (configNr,
					     (rndis_reset_msg_type *) buf);

	case REMOTE_NDIS_KEEPALIVE_MSG:
		/* For USB: host does this every 5 seconds */
		if (rndis_debug > 1)
			DEBUG("%s: REMOTE_NDIS_KEEPALIVE_MSG\n", 
				__FUNCTION__ );
		return rndis_keepalive_response (configNr,
						 (rndis_keepalive_msg_type *) 
						 buf);
		
	default: 
		/* At least Windows XP emits some undefined RNDIS messages.
		 * In one case those messages seemed to relate to the host
		 * suspending itself.
		 */
		printk (KERN_WARNING
			"%s: unknown RNDIS message 0x%08X len %d\n", 
			__FUNCTION__ , MsgType, MsgLength);
		{
			unsigned i;
			for (i = 0; i < MsgLength; i += 16) {
				DEBUG ("%03d: "
					" %02x %02x %02x %02x"
					" %02x %02x %02x %02x"
					" %02x %02x %02x %02x"
					" %02x %02x %02x %02x"
					"\n",
					i,
					buf[i], buf [i+1],
						buf[i+2], buf[i+3],
					buf[i+4], buf [i+5],
						buf[i+6], buf[i+7],
					buf[i+8], buf [i+9],
						buf[i+10], buf[i+11],
					buf[i+12], buf [i+13],
						buf[i+14], buf[i+15]);
			}
		}
		break;
	}
	
	return -ENOTSUPP;
}

int rndis_register (int (* rndis_control_ack) (struct net_device *))
{
	u8 i;
	
	for (i = 0; i < RNDIS_MAX_CONFIGS; i++) {
		if (!rndis_per_dev_params [i].used) {
			rndis_per_dev_params [i].used = 1;
			rndis_per_dev_params [i].ack = rndis_control_ack;
			DEBUG("%s: configNr = %d\n", __FUNCTION__, i);
			return i;
		}
	}
	DEBUG("failed\n");
	
	return -1;
}

void rndis_deregister (int configNr)
{
	DEBUG("%s: \n", __FUNCTION__ );
	
	if (configNr >= RNDIS_MAX_CONFIGS) return;
	rndis_per_dev_params [configNr].used = 0;
	
	return;
}

int rndis_set_param_dev (u8 configNr, struct net_device *dev, 
			 struct net_device_stats *stats,
			 u16 *cdc_filter)
{
	DEBUG("%s:\n", __FUNCTION__ );
	if (!dev || !stats) return -1;
	if (configNr >= RNDIS_MAX_CONFIGS) return -1;
	
	rndis_per_dev_params [configNr].dev = dev;
	rndis_per_dev_params [configNr].stats = stats;
	rndis_per_dev_params [configNr].filter = cdc_filter;
	
	return 0;
}

int rndis_set_param_vendor (u8 configNr, u32 vendorID, const char *vendorDescr)
{
	DEBUG("%s:\n", __FUNCTION__ );
	if (!vendorDescr) return -1;
	if (configNr >= RNDIS_MAX_CONFIGS) return -1;
	
	rndis_per_dev_params [configNr].vendorID = vendorID;
	rndis_per_dev_params [configNr].vendorDescr = vendorDescr;
	
	return 0;
}

int rndis_set_param_medium (u8 configNr, u32 medium, u32 speed)
{
	DEBUG("%s: %u %u\n", __FUNCTION__, medium, speed);
	if (configNr >= RNDIS_MAX_CONFIGS) return -1;
	
	rndis_per_dev_params [configNr].medium = medium;
	rndis_per_dev_params [configNr].speed = speed;
	
	return 0;
}

void rndis_add_hdr (struct sk_buff *skb)
{
	struct rndis_packet_msg_type	*header;

	if (!skb)
		return;
	header = (void *) skb_push (skb, sizeof *header);
	memset (header, 0, sizeof *header);
	header->MessageType = __constant_cpu_to_le32(REMOTE_NDIS_PACKET_MSG);
	header->MessageLength = cpu_to_le32(skb->len);
	header->DataOffset = __constant_cpu_to_le32 (36);
	header->DataLength = cpu_to_le32(skb->len - sizeof *header);
}

void rndis_free_response (int configNr, u8 *buf)
{
	rndis_resp_t		*r;
	struct list_head	*act, *tmp;
	
	list_for_each_safe (act, tmp, 
			    &(rndis_per_dev_params [configNr].resp_queue))
	{
		r = list_entry (act, rndis_resp_t, list);
		if (r && r->buf == buf) {
			list_del (&r->list);
			kfree (r);
		}
	}
}

u8 *rndis_get_next_response (int configNr, u32 *length)
{
	rndis_resp_t		*r;
	struct list_head 	*act, *tmp;
	
	if (!length) return NULL;
	
	list_for_each_safe (act, tmp, 
			    &(rndis_per_dev_params [configNr].resp_queue))
	{
		r = list_entry (act, rndis_resp_t, list);
		if (!r->send) {
			r->send = 1;
			*length = r->length;
			return r->buf;
		}
	}
	
	return NULL;
}

static rndis_resp_t *rndis_add_response (int configNr, u32 length)
{
	rndis_resp_t	*r;
	
	/* NOTE:  this gets copied into ether.c USB_BUFSIZ bytes ... */
	r = kmalloc (sizeof (rndis_resp_t) + length, GFP_ATOMIC);
	if (!r) return NULL;
	
	r->buf = (u8 *) (r + 1);
	r->length = length;
	r->send = 0;
	
	list_add_tail (&r->list, 
		       &(rndis_per_dev_params [configNr].resp_queue));
	return r;
}

int rndis_rm_hdr(struct sk_buff *skb)
{
	/* tmp points to a struct rndis_packet_msg_type */
	__le32		*tmp = (void *) skb->data;

	/* MessageType, MessageLength */
	if (__constant_cpu_to_le32(REMOTE_NDIS_PACKET_MSG)
			!= get_unaligned(tmp++))
		return -EINVAL;
	tmp++;

	/* DataOffset, DataLength */
	if (!skb_pull(skb, le32_to_cpu(get_unaligned(tmp++))
			+ 8 /* offset of DataOffset */))
		return -EOVERFLOW;
	skb_trim(skb, le32_to_cpu(get_unaligned(tmp++)));

	return 0;
}

#ifdef	CONFIG_USB_GADGET_DEBUG_FILES

static int rndis_proc_read (char *page, char **start, off_t off, int count, int *eof, 
		     void *data)
{
	char *out = page;
	int len;
	rndis_params *param = (rndis_params *) data;
	
	out += snprintf (out, count, 
			 "Config Nr. %d\n"
			 "used      : %s\n"
			 "state     : %s\n"
			 "medium    : 0x%08X\n"
			 "speed     : %d\n"
			 "cable     : %s\n"
			 "vendor ID : 0x%08X\n"
			 "vendor    : %s\n", 
			 param->confignr, (param->used) ? "y" : "n", 
			 ({ char *s = "?";
			 switch (param->state) {
			 case RNDIS_UNINITIALIZED:
				s = "RNDIS_UNINITIALIZED"; break;
			 case RNDIS_INITIALIZED:
				s = "RNDIS_INITIALIZED"; break;
			 case RNDIS_DATA_INITIALIZED:
				s = "RNDIS_DATA_INITIALIZED"; break;
			}; s; }),
			 param->medium, 
			 (param->media_state) ? 0 : param->speed*100, 
			 (param->media_state) ? "disconnected" : "connected",
			 param->vendorID, param->vendorDescr);      
	
	len = out - page;
	len -= off;
	
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;
	
	*start = page + off;
	return len;
}

static int rndis_proc_write (struct file *file, const char __user *buffer, 
		      unsigned long count, void *data)
{
	rndis_params *p = data;
	u32 speed = 0;
	int i, fl_speed = 0;
	
	for (i = 0; i < count; i++) {
		char c;
		if (get_user(c, buffer))
			return -EFAULT;
		switch (c) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			fl_speed = 1;
			speed = speed*10 + c - '0';
			break;
		case 'C':
		case 'c':
			rndis_signal_connect (p->confignr);
			break;
		case 'D':
		case 'd':
			rndis_signal_disconnect(p->confignr);
			break;
		default: 
			if (fl_speed) p->speed = speed;
			else DEBUG ("%c is not valid\n", c);
			break;
		}
		
		buffer++;
	}
	
	return count;
}

#define	NAME_TEMPLATE	"driver/rndis-%03d"

static struct proc_dir_entry *rndis_connect_state [RNDIS_MAX_CONFIGS];

#endif	/* CONFIG_USB_GADGET_DEBUG_FILES */


int __init rndis_init (void)
{
	u8 i;

	for (i = 0; i < RNDIS_MAX_CONFIGS; i++) {
#ifdef	CONFIG_USB_GADGET_DEBUG_FILES
		char name [20];

		sprintf (name, NAME_TEMPLATE, i);
		if (!(rndis_connect_state [i]
				= create_proc_entry (name, 0660, NULL))) 
		{
			DEBUG ("%s :remove entries", __FUNCTION__);
			while (i) {
				sprintf (name, NAME_TEMPLATE, --i);
				remove_proc_entry (name, NULL);
			}
			DEBUG ("\n");
			return -EIO;
		}

		rndis_connect_state [i]->nlink = 1;
		rndis_connect_state [i]->write_proc = rndis_proc_write;
		rndis_connect_state [i]->read_proc = rndis_proc_read;
		rndis_connect_state [i]->data = (void *)
				(rndis_per_dev_params + i);
#endif
		rndis_per_dev_params [i].confignr = i;
		rndis_per_dev_params [i].used = 0;
		rndis_per_dev_params [i].state = RNDIS_UNINITIALIZED;
		rndis_per_dev_params [i].media_state
				= NDIS_MEDIA_STATE_DISCONNECTED;
		INIT_LIST_HEAD (&(rndis_per_dev_params [i].resp_queue));
	}
	
	return 0;
}

void rndis_exit (void)
{
#ifdef	CONFIG_USB_GADGET_DEBUG_FILES
	u8 i;
	char name [20];
	
	for (i = 0; i < RNDIS_MAX_CONFIGS; i++) {
		sprintf (name, NAME_TEMPLATE, i);
		remove_proc_entry (name, NULL);
	}
#endif
}

