/*
 * DHD debugability: Header file for the Status Information Logging
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef __DHD_STATLOG_H_
#define __DHD_STATLOG_H_

#ifdef DHD_STATUS_LOGGING

/* status element */
typedef struct stat_elem {
	uint16 stat;		/* store status */
	uint64 ts;		/* local timestamp(ns) */
	uint64 ts_tz;		/* timestamp applied timezone(us) */
	uint8 ifidx;		/* ifidx */
	uint8 dir;		/* direction (TX/RX) */
	uint8 reason;		/* reason code from dongle */
	uint8 status;		/* status code from dongle */
	uint8 resv[2];		/* reserved for future use */
} stat_elem_t;

/* status logging info */
#define DHD_STAT_BDMASK_SIZE	16
typedef struct dhd_statlog {
	uint8 *logbuf;		/* log buffer */
	uint32 logbuf_len;	/* length of the log buffer */
	void *ringbuf;		/* fixed ring buffer */
	uint32 bufsize;		/* size of ring buffer */
	void *bdlog_ringbuf;	/* fixed ring buffer for bigdata logging */
	uint32 bdlog_bufsize;	/* size of ring buffer for bigdata logging */
	uint8 bdmask[DHD_STAT_BDMASK_SIZE];	/* bitmask for bigdata */
} dhd_statlog_t;

/* status query format */
typedef struct stat_query {
	uint8 *req_buf;		/* request buffer to interested status */
	uint32 req_buf_len;	/* length of the request buffer */
	uint8 *resp_buf;	/* response buffer */
	uint32 resp_buf_len;	/* length of the response buffer */
	uint32 req_num;		/* total number of items to query */
} stat_query_t;

/* bitmask generation request format */
typedef struct stat_bdmask_req {
	uint8 *req_buf;		/* request buffer to gernerate bitmask */
	uint32 req_buf_len;	/* length of the request buffer */
} stat_bdmask_req_t;

typedef void * dhd_statlog_handle_t; /* opaque handle to status log */

/* enums */
#define ST(x)			STATE_## x
#define STDIR(x)		STATE_DIR_## x

/* status direction */
typedef enum stat_log_dir {
	STDIR(TX)		= 1,
	STDIR(RX)		= 2,
	STDIR(MAX)		= 3
} stat_dir_t;

/* status definition */
typedef enum stat_log_stat {
	ST(INVALID)		= 0,	/* invalid status */
	ST(WLAN_POWER_ON)	= 1,	/* Wi-Fi Power on */
	ST(WLAN_POWER_OFF)	= 2,	/* Wi-Fi Power off */
	ST(ASSOC_START)		= 3,	/* connect to the AP triggered by upper layer */
	ST(AUTH_DONE)		= 4,	/* complete to authenticate with the AP */
	ST(ASSOC_REQ)		= 5,	/* send or receive Assoc Req */
	ST(ASSOC_RESP)		= 6,	/* send or receive Assoc Resp */
	ST(ASSOC_DONE)		= 7,	/* complete to disconnect to the associated AP */
	ST(DISASSOC_START)	= 8,	/* disconnect to the associated AP by upper layer */
	ST(DISASSOC_INT_START)	= 9,	/* initiate the disassoc by DHD */
	ST(DISASSOC_DONE)	= 10,	/* complete to disconnect to the associated AP */
	ST(DISASSOC)		= 11,	/* send or receive Disassoc */
	ST(DEAUTH)		= 12,	/* send or receive Deauth */
	ST(LINKDOWN)		= 13,	/* receive the link down event */
	ST(REASSOC_START)	= 14,	/* reassoc the candidate AP */
	ST(REASSOC_INFORM)	= 15,	/* inform reassoc completion to upper layer */
	ST(REASSOC_DONE)	= 16,	/* complete to reassoc */
	ST(EAPOL_M1)		= 17,	/* send or receive the EAPOL M1 */
	ST(EAPOL_M2)		= 18,	/* send or receive the EAPOL M2 */
	ST(EAPOL_M3)		= 19,	/* send or receive the EAPOL M3 */
	ST(EAPOL_M4)		= 20,	/* send or receive the EAPOL M4 */
	ST(EAPOL_GROUPKEY_M1)	= 21,	/* send or receive the EAPOL Group key handshake M1 */
	ST(EAPOL_GROUPKEY_M2)	= 22,	/* send or receive the EAPOL Group key handshake M2 */
	ST(EAP_REQ_IDENTITY)	= 23,	/* send or receive the EAP REQ IDENTITY */
	ST(EAP_RESP_IDENTITY)	= 24,	/* send or receive the EAP RESP IDENTITY */
	ST(EAP_REQ_TLS)		= 25,	/* send or receive the EAP REQ TLS */
	ST(EAP_RESP_TLS)	= 26,	/* send or receive the EAP RESP TLS */
	ST(EAP_REQ_LEAP)	= 27,	/* send or receive the EAP REQ LEAP */
	ST(EAP_RESP_LEAP)	= 28,	/* send or receive the EAP RESP LEAP */
	ST(EAP_REQ_TTLS)	= 29,	/* send or receive the EAP REQ TTLS */
	ST(EAP_RESP_TTLS)	= 30,	/* send or receive the EAP RESP TTLS */
	ST(EAP_REQ_AKA)		= 31,	/* send or receive the EAP REQ AKA */
	ST(EAP_RESP_AKA)	= 32,	/* send or receive the EAP RESP AKA */
	ST(EAP_REQ_PEAP)	= 33,	/* send or receive the EAP REQ PEAP */
	ST(EAP_RESP_PEAP)	= 34,	/* send or receive the EAP RESP PEAP */
	ST(EAP_REQ_FAST)	= 35,	/* send or receive the EAP REQ FAST */
	ST(EAP_RESP_FAST)	= 36,	/* send or receive the EAP RESP FAST */
	ST(EAP_REQ_PSK)		= 37,	/* send or receive the EAP REQ PSK */
	ST(EAP_RESP_PSK)	= 38,	/* send or receive the EAP RESP PSK */
	ST(EAP_REQ_AKAP)	= 39,	/* send or receive the EAP REQ AKAP */
	ST(EAP_RESP_AKAP)	= 40,	/* send or receive the EAP RESP AKAP */
	ST(EAP_SUCCESS)		= 41,	/* send or receive the EAP SUCCESS */
	ST(EAP_FAILURE)		= 42,	/* send or receive the EAP FAILURE */
	ST(EAPOL_START)		= 43,	/* send or receive the EAPOL-START */
	ST(WSC_START)		= 44,	/* send or receive the WSC START */
	ST(WSC_DONE)		= 45,	/* send or receive the WSC DONE */
	ST(WPS_M1)		= 46,	/* send or receive the WPS M1 */
	ST(WPS_M2)		= 47,	/* send or receive the WPS M2 */
	ST(WPS_M3)		= 48,	/* send or receive the WPS M3 */
	ST(WPS_M4)		= 49,	/* send or receive the WPS M4 */
	ST(WPS_M5)		= 50,	/* send or receive the WPS M5 */
	ST(WPS_M6)		= 51,	/* send or receive the WPS M6 */
	ST(WPS_M7)		= 52,	/* send or receive the WPS M7 */
	ST(WPS_M8)		= 53,	/* send or receive the WPS M8 */
	ST(8021X_OTHER)		= 54,	/* send or receive the other 8021X frames */
	ST(INSTALL_KEY)		= 55,	/* install the key */
	ST(DELETE_KEY)		= 56,	/* remove the key */
	ST(INSTALL_PMKSA)	= 57,	/* install PMKID information */
	ST(INSTALL_OKC_PMK)	= 58,	/* install PMKID information for OKC */
	ST(DHCP_DISCOVER)	= 59,	/* send or recv DHCP Discover */
	ST(DHCP_OFFER)		= 60,	/* send or recv DHCP Offer */
	ST(DHCP_REQUEST)	= 61,	/* send or recv DHCP Request */
	ST(DHCP_DECLINE)	= 62,	/* send or recv DHCP Decline */
	ST(DHCP_ACK)		= 63,	/* send or recv DHCP ACK */
	ST(DHCP_NAK)		= 64,	/* send or recv DHCP NACK */
	ST(DHCP_RELEASE)	= 65,	/* send or recv DHCP Release */
	ST(DHCP_INFORM)		= 66,	/* send or recv DHCP Inform */
	ST(ICMP_PING_REQ)	= 67,	/* send or recv ICMP PING Req */
	ST(ICMP_PING_RESP)	= 68,	/* send or recv ICMP PING Resp */
	ST(ICMP_DEST_UNREACH)	= 69,	/* send or recv ICMP DEST UNREACH message */
	ST(ICMP_OTHER)		= 70,	/* send or recv other ICMP */
	ST(ARP_REQ)		= 71,	/* send or recv ARP Req */
	ST(ARP_RESP)		= 72,	/* send or recv ARP Resp */
	ST(DNS_QUERY)		= 73,	/* send or recv DNS Query */
	ST(DNS_RESP)		= 74,	/* send or recv DNS Resp */
	ST(REASSOC_SUCCESS)	= 75,	/* reassociation success */
	ST(REASSOC_FAILURE)	= 76,	/* reassociation failure */
	ST(AUTH_TIMEOUT)	= 77,	/* authentication timeout */
	ST(AUTH_FAIL)		= 78,	/* authentication failure */
	ST(AUTH_NO_ACK)		= 79,	/* authentication failure due to no ACK */
	ST(AUTH_OTHERS)		= 80,	/* authentication failure with other status */
	ST(ASSOC_TIMEOUT)	= 81,	/* association timeout */
	ST(ASSOC_FAIL)		= 82,	/* association failure */
	ST(ASSOC_NO_ACK)	= 83,	/* association failure due to no ACK */
	ST(ASSOC_ABORT)		= 84,	/* association abort */
	ST(ASSOC_UNSOLICITED)	= 85,	/* association unsolicited */
	ST(ASSOC_NO_NETWORKS)	= 86,	/* association failure due to no networks */
	ST(ASSOC_OTHERS)	= 87,	/* association failure due to no networks */
	ST(REASSOC_DONE_OTHERS)	= 88,	/* complete to reassoc with other reason */
	ST(MAX)			= 89	/* Max Status */
} stat_log_stat_t;

/* functions */
extern dhd_statlog_handle_t *dhd_attach_statlog(dhd_pub_t *dhdp, uint32 num_items,
	uint32 bdlog_num_items, uint32 logbuf_len);
extern void dhd_detach_statlog(dhd_pub_t *dhdp);
extern int dhd_statlog_ring_log_data(dhd_pub_t *dhdp, uint16 stat, uint8 ifidx,
	uint8 dir, bool cond);
extern int dhd_statlog_ring_log_data_reason(dhd_pub_t *dhdp, uint16 stat,
	uint8 ifidx, uint8 dir, uint16 reason);
extern int dhd_statlog_ring_log_ctrl(dhd_pub_t *dhdp, uint16 stat, uint8 ifidx,
	uint16 reason);
extern int dhd_statlog_process_event(dhd_pub_t *dhdp, int type, uint8 ifidx,
	uint16 status, uint16 reason, uint16 flags);
extern int dhd_statlog_get_latest_info(dhd_pub_t *dhdp, void *reqbuf);
extern void dhd_statlog_dump_scr(dhd_pub_t *dhdp);
extern int dhd_statlog_query(dhd_pub_t *dhdp, char *cmd, int total_len);
extern uint32 dhd_statlog_get_logbuf_len(dhd_pub_t *dhdp);
extern void *dhd_statlog_get_logbuf(dhd_pub_t *dhdp);
extern int dhd_statlog_generate_bdmask(dhd_pub_t *dhdp, void *reqbuf);
#ifdef DHD_LOG_DUMP
extern int dhd_statlog_write_logdump(dhd_pub_t *dhdp, const void *user_buf,
	void *fp, uint32 len, unsigned long *pos);
#endif /* DHD_LOG_DUMP */

/* macros */
#define MAX_STATLOG_ITEM		512
#define MAX_STATLOG_REQ_ITEM		32
#define STATLOG_LOGBUF_LEN		(64 * 1024)
#define DHD_STATLOG_VERSION_V1		0x1
#define DHD_STATLOG_VERSION		DHD_STATLOG_VERSION_V1
#define	DHD_STATLOG_ITEM_SIZE		(sizeof(stat_elem_t))
#define DHD_STATLOG_RING_SIZE(items)	((items) * (DHD_STATLOG_ITEM_SIZE))
#define DHD_STATLOG_STATSTR_BUF_LEN	32
#define DHD_STATLOG_TZFMT_BUF_LEN	20
#define DHD_STATLOG_TZFMT_YYMMDDHHMMSSMS	"%02d%02d%02d%02d%02d%02d%04d"

#define DHD_STATLOG_CTRL(dhdp, stat, ifidx, reason)	\
	dhd_statlog_ring_log_ctrl((dhdp), (stat), (ifidx), (reason))
#define DHD_STATLOG_DATA(dhdp, stat, ifidx, dir, cond) \
	dhd_statlog_ring_log_data((dhdp), (stat), (ifidx), (dir), (cond))
#define DHD_STATLOG_DATA_RSN(dhdp, stat, ifidx, dir, reason) \
	dhd_statlog_ring_log_data_reason((dhdp), (stat), (ifidx), \
		(dir), (reason))

#endif /* DHD_STATUS_LOGGING */
#endif /* __DHD_STATLOG_H_ */
