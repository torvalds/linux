/*
 * RWL definitions  of
 * Broadcom 802.11bang Networking Device Driver
 *
 * Copyright (C) 2012, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: rwl_wifi.h 281527 2011-09-02 17:12:53Z $
 *
 */

#ifndef _rwl_wifi_h_
#define _rwl_wifi_h_

#if defined(RWL_WIFI) || defined(WIFI_REFLECTOR) || defined(RFAWARE)

#define RWL_ACTION_WIFI_CATEGORY	127  /* Vendor-specific category value for WiFi */
#define RWL_WIFI_OUI_BYTE0		0x00 /* BRCM-specific public OUI */
#define RWL_WIFI_OUI_BYTE1		0x90
#define RWL_WIFI_OUI_BYTE2		0x4c
#define RWL_WIFI_ACTION_FRAME_SIZE	sizeof(struct dot11_action_wifi_vendor_specific)
#define RWL_WIFI_FIND_MY_PEER		0x09 /* Used while finding server */
#define RWL_WIFI_FOUND_PEER		0x0A /* Server response to the client  */
#define RWL_WIFI_DEFAULT		0x00
#define RWL_ACTION_WIFI_FRAG_TYPE	0x55 /* Fragment indicator for receiver */

/*
 * Information about the action frame data fields in the dot11_action_wifi_vendor_specific
 * cdc structure (1 to 16). This does not include the status flag. Since this
 * is not directly visible to the driver code, we can't use sizeof(struct cdc_ioctl).
 * Hence Ref MAC address offset starts from byte 17.
 * REF MAC ADDR (6 bytes (MAC Address len) from byte 17 to 22)
 * DUT MAC ADDR (6 bytes after the REF MAC Address byte 23 to 28)
 * unused (byte 29 to 49)
 * REF/Client Channel offset (50)
 * DUT/Server channel offset (51)
 * ---------------------------------------------------------------------------------------
 * cdc struct|REF MAC ADDR|DUT_MAC_ADDR|un used|REF Channel|DUT channel|Action frame Data|
 * 1---------17-----------23-------------------50----------51----------52----------------1040
 * REF MAC addr after CDC struct without status flag (status flag not used by wifi)
 */

#define RWL_REF_MAC_ADDRESS_OFFSET	17
#define RWL_DUT_MAC_ADDRESS_OFFSET	23
#define RWL_WIFI_CLIENT_CHANNEL_OFFSET	50
#define RWL_WIFI_SERVER_CHANNEL_OFFSET	51

#ifdef WIFI_REFLECTOR
#include <bcmcdc.h>
#define REMOTE_FINDSERVER_CMD 	16
#define RWL_WIFI_ACTION_CMD		"wifiaction"
#define RWL_WIFI_ACTION_CMD_LEN		11	/* With the NULL terminator */
#define REMOTE_SET_CMD 		1
#define REMOTE_GET_CMD 		2
#define REMOTE_REPLY 			4
#define RWL_WIFI_DEFAULT_TYPE           0x00
#define RWL_WIFI_DEFAULT_SUBTYPE        0x00
#define RWL_ACTION_FRAME_DATA_SIZE      1024	/* fixed size for the wifi frame data */
#define RWL_WIFI_CDC_HEADER_OFFSET      0
#define RWL_WIFI_FRAG_DATA_SIZE         960	/* max size of the frag data */
#define RWL_DEFAULT_WIFI_FRAG_COUNT 	127 	/* maximum fragment count */
#define RWL_WIFI_RETRY			5       /* CMD retry count for wifi */
#define RWL_WIFI_SEND			5	/* WIFI frame sent count */
#define RWL_WIFI_SEND_DELAY		100	/* delay between two frames */
#define MICROSEC_CONVERTOR_VAL		1000
#ifndef IFNAMSIZ
#define IFNAMSIZ			16
#endif

typedef struct rem_packet {
	rem_ioctl_t rem_cdc;
	uchar message [RWL_ACTION_FRAME_DATA_SIZE];
} rem_packet_t;

struct send_packet {
	char command [RWL_WIFI_ACTION_CMD_LEN];
	dot11_action_wifi_vendor_specific_t response;
} PACKED;
typedef struct send_packet send_packet_t;

#define REMOTE_SIZE     sizeof(rem_ioctl_t)
#endif /* WIFI_REFLECTOR */

typedef struct rwl_request {
	struct rwl_request* next_request;
	struct dot11_action_wifi_vendor_specific action_frame;
} rwl_request_t;


#endif /* defined(RWL_WIFI) || defined(WIFI_REFLECTOR) */
#endif	/* _rwl_wifi_h_ */
