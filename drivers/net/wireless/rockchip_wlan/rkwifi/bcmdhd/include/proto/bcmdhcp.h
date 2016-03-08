/*
 * Fundamental constants relating to DHCP Protocol
 *
 * Copyright (C) 2016, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: bcmdhcp.h 518342 2014-12-01 23:21:41Z $
 */

#ifndef _bcmdhcp_h_
#define _bcmdhcp_h_

/* DHCP params */
#define DHCP_TYPE_OFFSET	0	/* DHCP type (request|reply) offset */
#define DHCP_TID_OFFSET		4	/* DHCP transition id offset */
#define DHCP_FLAGS_OFFSET	10	/* DHCP flags offset */
#define DHCP_CIADDR_OFFSET	12	/* DHCP client IP address offset */
#define DHCP_YIADDR_OFFSET	16	/* DHCP your IP address offset */
#define DHCP_GIADDR_OFFSET	24	/* DHCP relay agent IP address offset */
#define DHCP_CHADDR_OFFSET	28	/* DHCP client h/w address offset */
#define DHCP_OPT_OFFSET		236	/* DHCP options offset */

#define DHCP_OPT_MSGTYPE	53	/* DHCP message type */
#define DHCP_OPT_MSGTYPE_REQ	3
#define DHCP_OPT_MSGTYPE_ACK	5	/* DHCP message type - ACK */

#define DHCP_OPT_CODE_OFFSET	0	/* Option identifier */
#define DHCP_OPT_LEN_OFFSET	1	/* Option data length */
#define DHCP_OPT_DATA_OFFSET	2	/* Option data */

#define DHCP_OPT_CODE_CLIENTID	61	/* Option identifier */

#define DHCP_TYPE_REQUEST	1	/* DHCP request (discover|request) */
#define DHCP_TYPE_REPLY		2	/* DHCP reply (offset|ack) */

#define DHCP_PORT_SERVER	67	/* DHCP server UDP port */
#define DHCP_PORT_CLIENT	68	/* DHCP client UDP port */

#define DHCP_FLAG_BCAST	0x8000	/* DHCP broadcast flag */

#define DHCP_FLAGS_LEN	2	/* DHCP flags field length */

#define DHCP6_TYPE_SOLICIT	1	/* DHCP6 solicit */
#define DHCP6_TYPE_ADVERTISE	2	/* DHCP6 advertise */
#define DHCP6_TYPE_REQUEST	3	/* DHCP6 request */
#define DHCP6_TYPE_CONFIRM	4	/* DHCP6 confirm */
#define DHCP6_TYPE_RENEW	5	/* DHCP6 renew */
#define DHCP6_TYPE_REBIND	6	/* DHCP6 rebind */
#define DHCP6_TYPE_REPLY	7	/* DHCP6 reply */
#define DHCP6_TYPE_RELEASE	8	/* DHCP6 release */
#define DHCP6_TYPE_DECLINE	9	/* DHCP6 decline */
#define DHCP6_TYPE_RECONFIGURE	10	/* DHCP6 reconfigure */
#define DHCP6_TYPE_INFOREQ	11	/* DHCP6 information request */
#define DHCP6_TYPE_RELAYFWD	12	/* DHCP6 relay forward */
#define DHCP6_TYPE_RELAYREPLY	13	/* DHCP6 relay reply */

#define DHCP6_TYPE_OFFSET	0	/* DHCP6 type offset */

#define	DHCP6_MSG_OPT_OFFSET	4	/* Offset of options in client server messages */
#define	DHCP6_RELAY_OPT_OFFSET	34	/* Offset of options in relay messages */

#define	DHCP6_OPT_CODE_OFFSET	0	/* Option identifier */
#define	DHCP6_OPT_LEN_OFFSET	2	/* Option data length */
#define	DHCP6_OPT_DATA_OFFSET	4	/* Option data */

#define	DHCP6_OPT_CODE_CLIENTID	1	/* DHCP6 CLIENTID option */
#define	DHCP6_OPT_CODE_SERVERID	2	/* DHCP6 SERVERID option */

#define DHCP6_PORT_SERVER	547	/* DHCP6 server UDP port */
#define DHCP6_PORT_CLIENT	546	/* DHCP6 client UDP port */

#endif	/* #ifndef _bcmdhcp_h_ */
