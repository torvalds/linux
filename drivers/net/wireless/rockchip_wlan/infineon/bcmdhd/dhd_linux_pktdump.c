/*
 * Packet dump helper functions
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <typedefs.h>
#include <ethernet.h>
#include <bcmutils.h>
#include <bcmevent.h>
#include <bcmendian.h>
#include <bcmtlv.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <bcmip.h>
#include <bcmudp.h>
#include <bcmdhcp.h>
#include <bcmarp.h>
#include <bcmicmp.h>
#include <dhd_linux_pktdump.h>

#define DHD_PKTDUMP(arg)	DHD_ERROR(arg)
#define DHD_PKTDUMP_MEM(arg)	DHD_ERROR_MEM(arg)
#define PACKED_STRUCT __attribute__ ((packed))

#define EAPOL_HDR_LEN		4

/* EAPOL types */
#define EAP_PACKET		0
#define EAPOL_START		1
#define EAPOL_LOGOFF		2
#define EAPOL_KEY		3
#define EAPOL_ASF		4

/* EAPOL-Key types */
#define EAPOL_RC4_KEY		1
#define EAPOL_WPA2_KEY		2	/* 802.11i/WPA2 */
#define EAPOL_WPA_KEY		254	/* WPA */

/* EAPOL-Key header field size */
#define AKW_BLOCK_LEN		8
#define WPA_KEY_REPLAY_LEN	8
#define WPA_KEY_NONCE_LEN	32
#define WPA_KEY_IV_LEN		16
#define WPA_KEY_RSC_LEN		8
#define WPA_KEY_ID_LEN		8
#define WPA_KEY_MIC_LEN		16
#define WPA_MAX_KEY_SIZE	32
#define WPA_KEY_DATA_LEN	(WPA_MAX_KEY_SIZE + AKW_BLOCK_LEN)

/* Key information bit */
#define KEYINFO_TYPE_MASK	(1 << 3)
#define KEYINFO_INSTALL_MASK	(1 << 6)
#define KEYINFO_KEYACK_MASK	(1 << 7)
#define KEYINFO_KEYMIC_MASK	(1 << 8)
#define KEYINFO_SECURE_MASK	(1 << 9)
#define KEYINFO_ERROR_MASK	(1 << 10)
#define KEYINFO_REQ_MASK	(1 << 11)

/* EAP Code */
#define EAP_CODE_REQUEST	1	/* Request */
#define EAP_CODE_RESPONSE	2	/* Response */
#define EAP_CODE_SUCCESS	3	/* Success */
#define EAP_CODE_FAILURE	4	/* Failure */

/* EAP Type */
#define EAP_TYPE_RSVD		0	/* Reserved */
#define EAP_TYPE_IDENT		1	/* Identify */
#define EAP_TYPE_NOTI		2	/* Notification */
#define EAP_TYPE_TLS		13	/* EAP-TLS */
#define EAP_TYPE_LEAP		17	/* Cisco-LEAP */
#define EAP_TYPE_TTLS		21	/* EAP-TTLS */
#define EAP_TYPE_AKA		23	/* EAP-AKA */
#define EAP_TYPE_PEAP		25	/* EAP-PEAP */
#define EAP_TYPE_FAST		43	/* EAP-FAST */
#define EAP_TYPE_PSK		47	/* EAP-PSK */
#define EAP_TYPE_AKAP		50	/* EAP-AKA' */
#define EAP_TYPE_EXP		254	/* Reserved for Expended Type */

/* WSC */
#define EAP_HDR_LEN		5
#define EAP_WSC_NONCE_OFFSET	10
#define EAP_WSC_DATA_OFFSET	(OFFSETOF(eap_wsc_fmt_t, data))
#define EAP_WSC_MIN_DATA_LEN	((EAP_HDR_LEN) + (EAP_WSC_DATA_OFFSET))
#define WFA_VID			"\x00\x37\x2A"	/* WFA SMI code */
#define WFA_VID_LEN		3		/* WFA VID length */
#define WFA_VTYPE		1u		/* WFA Vendor type */

/* WSC opcode */
#define WSC_OPCODE_UPNP		0
#define WSC_OPCODE_START	1
#define WSC_OPCODE_ACK		2
#define WSC_OPCODE_NACK		3
#define WSC_OPCODE_MSG		4
#define WSC_OPCODE_DONE		5
#define WSC_OPCODE_FRAG_ACK	6

/* WSC flag */
#define WSC_FLAG_MF		1	/* more fragements */
#define WSC_FLAG_LF		2	/* length field */

/* WSC message code */
#define WSC_ATTR_MSG		0x1022
#define WSC_MSG_M1		0x04
#define WSC_MSG_M2		0x05
#define WSC_MSG_M3		0x07
#define WSC_MSG_M4		0x08
#define WSC_MSG_M5		0x09
#define WSC_MSG_M6		0x0A
#define WSC_MSG_M7		0x0B
#define WSC_MSG_M8		0x0C

/* Debug prints */
typedef enum pkt_cnt_type {
	PKT_CNT_TYPE_INVALID	= 0,
	PKT_CNT_TYPE_ARP	= 1,
	PKT_CNT_TYPE_DNS	= 2,
	PKT_CNT_TYPE_MAX	= 3
} pkt_cnt_type_t;

typedef struct pkt_cnt {
	uint32 tx_cnt;
	uint32 tx_err_cnt;
	uint32 rx_cnt;
} pkt_cnt_t;

typedef struct pkt_cnt_log {
	bool enabled;
	uint16 reason;
	timer_list_compat_t pktcnt_timer;
	pkt_cnt_t arp_cnt;
	pkt_cnt_t dns_cnt;
} pkt_cnts_log_t;

#define PKT_CNT_TIMER_INTERNVAL_MS		5000	/* packet count timeout(ms) */
#define PKT_CNT_RSN_VALID(rsn)	\
	(((rsn) > (PKT_CNT_RSN_INVALID)) && ((rsn) < (PKT_CNT_RSN_MAX)))

#ifdef DHD_PKTDUMP_ROAM
static const char pkt_cnt_msg[][20] = {
	"INVALID",
	"ROAM_SUCCESS",
	"GROUP_KEY_UPDATE",
	"INVALID"
};
#endif /* DHD_PKTDUMP_ROAM */

static const char tx_pktfate[][30] = {
	"TX_PKT_FATE_ACKED",		/* 0: WLFC_CTL_PKTFLAG_DISCARD */
	"TX_PKT_FATE_FW_QUEUED",	/* 1: WLFC_CTL_PKTFLAG_D11SUPPRESS */
	"TX_PKT_FATE_FW_QUEUED",	/* 2: WLFC_CTL_PKTFLAG_WLSUPPRESS */
	"TX_PKT_FATE_FW_DROP_INVALID",	/* 3: WLFC_CTL_PKTFLAG_TOSSED_BYWLC */
	"TX_PKT_FATE_SENT",		/* 4: WLFC_CTL_PKTFLAG_DISCARD_NOACK */
	"TX_PKT_FATE_FW_DROP_OTHER",	/* 5: WLFC_CTL_PKTFLAG_SUPPRESS_ACKED */
	"TX_PKT_FATE_FW_DROP_EXPTIME",	/* 6: WLFC_CTL_PKTFLAG_EXPIRED */
	"TX_PKT_FATE_FW_DROP_OTHER",	/* 7: WLFC_CTL_PKTFLAG_DROPPED */
	"TX_PKT_FATE_FW_PKT_FREE",	/* 8: WLFC_CTL_PKTFLAG_MKTFREE */
};

#define DBGREPLAY		" Replay Counter: %02x%02x%02x%02x%02x%02x%02x%02x"
#define REPLAY_FMT(key)		((const eapol_key_hdr_t *)(key))->replay[0], \
				((const eapol_key_hdr_t *)(key))->replay[1], \
				((const eapol_key_hdr_t *)(key))->replay[2], \
				((const eapol_key_hdr_t *)(key))->replay[3], \
				((const eapol_key_hdr_t *)(key))->replay[4], \
				((const eapol_key_hdr_t *)(key))->replay[5], \
				((const eapol_key_hdr_t *)(key))->replay[6], \
				((const eapol_key_hdr_t *)(key))->replay[7]
#define TXFATE_FMT		" TX_PKTHASH:0x%X TX_PKT_FATE:%s"
#define TX_PKTHASH(pkthash)		((pkthash) ? (*pkthash) : (0))
#define TX_FATE_STR(fate)	(((*fate) <= (WLFC_CTL_PKTFLAG_MKTFREE)) ? \
				(tx_pktfate[(*fate)]) : "TX_PKT_FATE_FW_DROP_OTHER")
#define TX_FATE(fate)		((fate) ? (TX_FATE_STR(fate)) : "N/A")
#define TX_FATE_ACKED(fate)	((fate) ? ((*fate) == (WLFC_CTL_PKTFLAG_DISCARD)) : (0))

#define EAP_PRINT(str) \
	do { \
		if (tx) { \
			DHD_PKTDUMP(("ETHER_TYPE_802_1X[%s] [TX]: " \
				str TXFATE_FMT "\n", ifname, \
				TX_PKTHASH(pkthash), TX_FATE(pktfate))); \
		} else { \
			DHD_PKTDUMP(("ETHER_TYPE_802_1X[%s] [RX]: " \
				str "\n", ifname)); \
		} \
	} while (0)

#define EAP_PRINT_REPLAY(str) \
	do { \
		if (tx) { \
			DHD_PKTDUMP(("ETHER_TYPE_802_1X[%s] [TX]: " \
				str DBGREPLAY TXFATE_FMT "\n", ifname, \
				REPLAY_FMT(eap_key), TX_PKTHASH(pkthash), \
				TX_FATE(pktfate))); \
		} else { \
			DHD_PKTDUMP(("ETHER_TYPE_802_1X[%s] [RX]: " \
				str DBGREPLAY "\n", ifname, \
				REPLAY_FMT(eap_key))); \
		} \
	} while (0)

#define EAP_PRINT_OTHER(str) \
	do { \
		if (tx) { \
			DHD_PKTDUMP(("ETHER_TYPE_802_1X[%s] [TX]: " \
				str "ver %d, type %d" TXFATE_FMT "\n", ifname, \
				eapol_hdr->version, eapol_hdr->type, \
				TX_PKTHASH(pkthash), TX_FATE(pktfate))); \
		} else { \
			DHD_PKTDUMP(("ETHER_TYPE_802_1X[%s] [RX]: " \
				str "ver %d, type %d\n", ifname, \
				eapol_hdr->version, eapol_hdr->type)); \
		} \
	} while (0)

#define EAP_PRINT_OTHER_4WAY(str) \
	do { \
		if (tx) { \
			DHD_PKTDUMP(("ETHER_TYPE_802_1X[%s] [TX]: " str \
				"ver %d type %d keytype %d keyinfo 0x%02X" \
				TXFATE_FMT "\n", ifname, eapol_hdr->version, \
				eapol_hdr->type, eap_key->type, \
				(uint32)hton16(eap_key->key_info), \
				TX_PKTHASH(pkthash), TX_FATE(pktfate))); \
		} else { \
			DHD_PKTDUMP(("ETHER_TYPE_802_1X[%s] [RX]: " str \
				"ver %d type %d keytype %d keyinfo 0x%02X\n", \
				ifname, eapol_hdr->version, eapol_hdr->type, \
				eap_key->type, (uint32)hton16(eap_key->key_info))); \
		} \
	} while (0)

/* EAPOL header */
typedef struct eapol_header {
	struct ether_header eth;	/* 802.3/Ethernet header */
	uint8 version;			/* EAPOL protocol version */
	uint8 type;			/* EAPOL type */
	uint16 length;			/* Length of body */
	uint8 body[1];			/* Body (optional) */
} PACKED_STRUCT eapol_header_t;

/* EAP header */
typedef struct eap_header_fmt {
	uint8 code;
	uint8 id;
	uint16 len;
	uint8 type;
	uint8 data[1];
} PACKED_STRUCT eap_header_fmt_t;

/* WSC EAP format */
typedef struct eap_wsc_fmt {
	uint8 oui[3];
	uint32 ouitype;
	uint8 opcode;
	uint8 flags;
	uint8 data[1];
} PACKED_STRUCT eap_wsc_fmt_t;

/* EAPOL-Key */
typedef struct eapol_key_hdr {
	uint8 type;				/* Key Descriptor Type */
	uint16 key_info;			/* Key Information (unaligned) */
	uint16 key_len;				/* Key Length (unaligned) */
	uint8 replay[WPA_KEY_REPLAY_LEN];	/* Replay Counter */
	uint8 nonce[WPA_KEY_NONCE_LEN];		/* Nonce */
	uint8 iv[WPA_KEY_IV_LEN];		/* Key IV */
	uint8 rsc[WPA_KEY_RSC_LEN];		/* Key RSC */
	uint8 id[WPA_KEY_ID_LEN];		/* WPA:Key ID, 802.11i/WPA2: Reserved */
	uint8 mic[WPA_KEY_MIC_LEN];		/* Key MIC */
	uint16 data_len;			/* Key Data Length */
	uint8 data[WPA_KEY_DATA_LEN];		/* Key data */
} PACKED_STRUCT eapol_key_hdr_t;

msg_eapol_t
dhd_is_4way_msg(uint8 *pktdata)
{
	eapol_header_t *eapol_hdr;
	eapol_key_hdr_t *eap_key;
	msg_eapol_t type = EAPOL_OTHER;
	bool pair, ack, mic, kerr, req, sec, install;
	uint16 key_info;

	if (!pktdata) {
		DHD_PKTDUMP(("%s: pktdata is NULL\n", __FUNCTION__));
		return type;
	}

	eapol_hdr = (eapol_header_t *)pktdata;
	eap_key = (eapol_key_hdr_t *)(eapol_hdr->body);
	if (eap_key->type != EAPOL_WPA2_KEY) {
		return type;
	}

	key_info = hton16(eap_key->key_info);
	pair = !!(key_info & KEYINFO_TYPE_MASK);
	ack = !!(key_info & KEYINFO_KEYACK_MASK);
	mic = !!(key_info & KEYINFO_KEYMIC_MASK);
	kerr = !!(key_info & KEYINFO_ERROR_MASK);
	req = !!(key_info & KEYINFO_REQ_MASK);
	sec = !!(key_info & KEYINFO_SECURE_MASK);
	install = !!(key_info & KEYINFO_INSTALL_MASK);

	if (pair && !install && ack && !mic && !sec && !kerr && !req) {
		type = EAPOL_4WAY_M1;
	} else if (pair && !install && !ack && mic && !sec && !kerr && !req) {
		type = EAPOL_4WAY_M2;
	} else if (pair && ack && mic && sec && !kerr && !req) {
		type = EAPOL_4WAY_M3;
	} else if (pair && !install && !ack && mic && sec && !req && !kerr) {
		type = EAPOL_4WAY_M4;
	} else if (!pair && !install && ack && mic && sec && !req && !kerr) {
		type = EAPOL_GROUPKEY_M1;
	} else if (!pair && !install && !ack && mic && sec && !req && !kerr) {
		type = EAPOL_GROUPKEY_M2;
	} else {
		type = EAPOL_OTHER;
	}

	return type;
}

void
dhd_dump_pkt(dhd_pub_t *dhdp, int ifidx, uint8 *pktdata, uint32 pktlen,
	bool tx, uint32 *pkthash, uint16 *pktfate)
{
	struct ether_header *eh;
	uint16 ether_type;

	if (!pktdata || pktlen < ETHER_HDR_LEN) {
		return;
	}

#if defined(BCMPCIE) && defined(DHD_PKT_LOGGING)
	if (tx && !pkthash && !pktfate) {
		return;
	}
#endif /* BCMPCIE && DHD_PKT_LOGGING */

	eh = (struct ether_header *)pktdata;
	ether_type = ntoh16(eh->ether_type);
	if (ether_type == ETHER_TYPE_802_1X) {
		dhd_dump_eapol_message(dhdp, ifidx, pktdata, pktlen,
			tx, pkthash, pktfate);
	}
	if (ntoh16(eh->ether_type) == ETHER_TYPE_IP) {
		dhd_dhcp_dump(dhdp, ifidx, pktdata, tx, pkthash, pktfate);
		dhd_icmp_dump(dhdp, ifidx, pktdata, tx, pkthash, pktfate);
		dhd_dns_dump(dhdp, ifidx, pktdata, tx, pkthash, pktfate);
	}
	if (ntoh16(eh->ether_type) == ETHER_TYPE_ARP) {
		dhd_arp_dump(dhdp, ifidx, pktdata, tx, pkthash, pktfate);
	}
}

#ifdef DHD_PKTDUMP_ROAM
static void
dhd_dump_pkt_cnts_inc(dhd_pub_t *dhdp, bool tx, uint16 *pktfate, uint16 pkttype)
{
	pkt_cnts_log_t *pktcnts;
	pkt_cnt_t *cnt;

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return;
	}

	pktcnts = (pkt_cnts_log_t *)(dhdp->pktcnts);
	if (!pktcnts) {
		DHD_ERROR(("%s: pktcnts is NULL\n", __FUNCTION__));
		return;
	}

	if (!pktcnts->enabled || (tx && !pktfate)) {
		return;
	}

	if (pkttype == PKT_CNT_TYPE_ARP) {
		cnt = (pkt_cnt_t *)&pktcnts->arp_cnt;
	} else if (pkttype == PKT_CNT_TYPE_DNS) {
		cnt = (pkt_cnt_t *)&pktcnts->dns_cnt;
	} else {
		/* invalid packet type */
		return;
	}

	if (tx) {
		TX_FATE_ACKED(pktfate) ? cnt->tx_cnt++ : cnt->tx_err_cnt++;
	} else {
		cnt->rx_cnt++;
	}
}

static void
dhd_dump_pkt_timer(unsigned long data)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)data;
	pkt_cnts_log_t *pktcnts = (pkt_cnts_log_t *)(dhdp->pktcnts);

	pktcnts->enabled = FALSE;

	/* print out the packet counter value */
	DHD_PKTDUMP(("============= PACKET COUNT SUMMARY ============\n"));
	DHD_PKTDUMP(("- Reason: %s\n", pkt_cnt_msg[pktcnts->reason]));
	DHD_PKTDUMP(("- Duration: %d msec(s)\n", PKT_CNT_TIMER_INTERNVAL_MS));
	DHD_PKTDUMP(("- ARP PACKETS: tx_success:%d tx_fail:%d rx_cnt:%d\n",
		pktcnts->arp_cnt.tx_cnt, pktcnts->arp_cnt.tx_err_cnt,
		pktcnts->arp_cnt.rx_cnt));
	DHD_PKTDUMP(("- DNS PACKETS: tx_success:%d tx_fail:%d rx_cnt:%d\n",
		pktcnts->dns_cnt.tx_cnt, pktcnts->dns_cnt.tx_err_cnt,
		pktcnts->dns_cnt.rx_cnt));
	DHD_PKTDUMP(("============= END OF COUNT SUMMARY ============\n"));
}

void
dhd_dump_mod_pkt_timer(dhd_pub_t *dhdp, uint16 rsn)
{
	pkt_cnts_log_t *pktcnts;

	if (!dhdp || !dhdp->pktcnts) {
		DHD_ERROR(("%s: dhdp or dhdp->pktcnts is NULL\n",
			__FUNCTION__));
		return;
	}

	if (!PKT_CNT_RSN_VALID(rsn)) {
		DHD_ERROR(("%s: invalid reason code %d\n",
			__FUNCTION__, rsn));
		return;
	}

	pktcnts = (pkt_cnts_log_t *)(dhdp->pktcnts);
	if (timer_pending(&pktcnts->pktcnt_timer)) {
		del_timer_sync(&pktcnts->pktcnt_timer);
	}

	bzero(&pktcnts->arp_cnt, sizeof(pkt_cnt_t));
	bzero(&pktcnts->dns_cnt, sizeof(pkt_cnt_t));
	pktcnts->reason = rsn;
	pktcnts->enabled = TRUE;
	mod_timer(&pktcnts->pktcnt_timer,
		jiffies + msecs_to_jiffies(PKT_CNT_TIMER_INTERNVAL_MS));
	DHD_PKTDUMP(("%s: Arm the pktcnt timer. reason=%d\n",
		__FUNCTION__, rsn));
}

void
dhd_dump_pkt_init(dhd_pub_t *dhdp)
{
	pkt_cnts_log_t *pktcnts;

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return;
	}

	pktcnts = (pkt_cnts_log_t *)MALLOCZ(dhdp->osh, sizeof(pkt_cnts_log_t));
	if (!pktcnts) {
		DHD_ERROR(("%s: failed to allocate memory for pktcnts\n",
			__FUNCTION__));
		return;
	}

	/* init timers */
	init_timer_compat(&pktcnts->pktcnt_timer, dhd_dump_pkt_timer, dhdp);
	dhdp->pktcnts = pktcnts;
}

void
dhd_dump_pkt_deinit(dhd_pub_t *dhdp)
{
	pkt_cnts_log_t *pktcnts;

	if (!dhdp || !dhdp->pktcnts) {
		DHD_ERROR(("%s: dhdp or pktcnts is NULL\n", __FUNCTION__));
		return;
	}

	pktcnts = (pkt_cnts_log_t *)(dhdp->pktcnts);
	pktcnts->enabled = FALSE;
	del_timer_sync(&pktcnts->pktcnt_timer);
	MFREE(dhdp->osh, dhdp->pktcnts, sizeof(pkt_cnts_log_t));
	dhdp->pktcnts = NULL;
}

void
dhd_dump_pkt_clear(dhd_pub_t *dhdp)
{
	pkt_cnts_log_t *pktcnts;

	if (!dhdp || !dhdp->pktcnts) {
		DHD_ERROR(("%s: dhdp or pktcnts is NULL\n", __FUNCTION__));
		return;
	}

	pktcnts = (pkt_cnts_log_t *)(dhdp->pktcnts);
	pktcnts->enabled = FALSE;
	del_timer_sync(&pktcnts->pktcnt_timer);
	pktcnts->reason = 0;
	bzero(&pktcnts->arp_cnt, sizeof(pkt_cnt_t));
	bzero(&pktcnts->dns_cnt, sizeof(pkt_cnt_t));
}

bool
dhd_dump_pkt_enabled(dhd_pub_t *dhdp)
{
	pkt_cnts_log_t *pktcnts;

	if (!dhdp || !dhdp->pktcnts) {
		return FALSE;
	}

	pktcnts = (pkt_cnts_log_t *)(dhdp->pktcnts);

	return pktcnts->enabled;
}
#else
static INLINE void
dhd_dump_pkt_cnts_inc(dhd_pub_t *dhdp, bool tx, uint16 *pktfate, uint16 pkttype) { }
static INLINE bool
dhd_dump_pkt_enabled(dhd_pub_t *dhdp) { return FALSE; }
#endif /* DHD_PKTDUMP_ROAM */

#ifdef DHD_8021X_DUMP
static void
dhd_dump_wsc_message(dhd_pub_t *dhd, int ifidx, uint8 *pktdata,
	uint32 pktlen, bool tx, uint32 *pkthash, uint16 *pktfate)
{
	eapol_header_t *eapol_hdr;
	eap_header_fmt_t *eap_hdr;
	eap_wsc_fmt_t *eap_wsc;
	char *ifname;
	uint16 eap_len;
	bool cond;

	if (!pktdata) {
		DHD_ERROR(("%s: pktdata is NULL\n", __FUNCTION__));
		return;
	}

	if (pktlen < (ETHER_HDR_LEN + EAPOL_HDR_LEN)) {
		DHD_ERROR(("%s: invalid pkt length\n", __FUNCTION__));
		return;
	}

	eapol_hdr = (eapol_header_t *)pktdata;
	eap_hdr = (eap_header_fmt_t *)(eapol_hdr->body);
	if (eap_hdr->type != EAP_TYPE_EXP) {
		return;
	}

	eap_len = ntoh16(eap_hdr->len);
	if (eap_len < EAP_WSC_MIN_DATA_LEN) {
		return;
	}

	eap_wsc = (eap_wsc_fmt_t *)(eap_hdr->data);
	if (bcmp(eap_wsc->oui, (const uint8 *)WFA_VID, WFA_VID_LEN) ||
		(ntoh32(eap_wsc->ouitype) != WFA_VTYPE)) {
		return;
	}

	if (eap_wsc->flags) {
		return;
	}

	ifname = dhd_ifname(dhd, ifidx);
	cond = (tx && pktfate) ? FALSE : TRUE;

	if (eap_wsc->opcode == WSC_OPCODE_MSG) {
		const uint8 *tlv_buf = (const uint8 *)(eap_wsc->data);
		const uint8 *msg;
		uint16 msglen;
		uint16 wsc_data_len = (uint16)(eap_len - EAP_HDR_LEN - EAP_WSC_DATA_OFFSET);
		bcm_xtlv_opts_t opt = BCM_XTLV_OPTION_IDBE | BCM_XTLV_OPTION_LENBE;

		msg = bcm_get_data_from_xtlv_buf(tlv_buf, wsc_data_len,
			WSC_ATTR_MSG, &msglen, opt);
		if (msg && msglen) {
			switch (*msg) {
			case WSC_MSG_M1:
				DHD_STATLOG_DATA(dhd, ST(WPS_M1), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, WPS M1");
				break;
			case WSC_MSG_M2:
				DHD_STATLOG_DATA(dhd, ST(WPS_M2), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, WPS M2");
				break;
			case WSC_MSG_M3:
				DHD_STATLOG_DATA(dhd, ST(WPS_M3), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, WPS M3");
				break;
			case WSC_MSG_M4:
				DHD_STATLOG_DATA(dhd, ST(WPS_M4), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, WPS M4");
				break;
			case WSC_MSG_M5:
				DHD_STATLOG_DATA(dhd, ST(WPS_M5), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, WPS M5");
				break;
			case WSC_MSG_M6:
				DHD_STATLOG_DATA(dhd, ST(WPS_M6), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, WPS M6");
				break;
			case WSC_MSG_M7:
				DHD_STATLOG_DATA(dhd, ST(WPS_M7), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, WPS M7");
				break;
			case WSC_MSG_M8:
				DHD_STATLOG_DATA(dhd, ST(WPS_M8), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, WPS M8");
				break;
			default:
				break;
			}
		}
	} else if (eap_wsc->opcode == WSC_OPCODE_START) {
		DHD_STATLOG_DATA(dhd, ST(WSC_START), ifidx, tx, cond);
		EAP_PRINT("EAP Packet, WSC Start");
	} else if (eap_wsc->opcode == WSC_OPCODE_DONE) {
		DHD_STATLOG_DATA(dhd, ST(WSC_DONE), ifidx, tx, cond);
		EAP_PRINT("EAP Packet, WSC Done");
	}
}

static void
dhd_dump_eap_packet(dhd_pub_t *dhd, int ifidx, uint8 *pktdata,
	uint32 pktlen, bool tx, uint32 *pkthash, uint16 *pktfate)
{
	eapol_header_t *eapol_hdr;
	eap_header_fmt_t *eap_hdr;
	char *ifname;
	bool cond;

	if (!pktdata) {
		DHD_PKTDUMP(("%s: pktdata is NULL\n", __FUNCTION__));
		return;
	}

	eapol_hdr = (eapol_header_t *)pktdata;
	eap_hdr = (eap_header_fmt_t *)(eapol_hdr->body);
	ifname = dhd_ifname(dhd, ifidx);
	cond = (tx && pktfate) ? FALSE : TRUE;

	if (eap_hdr->code == EAP_CODE_REQUEST ||
		eap_hdr->code == EAP_CODE_RESPONSE) {
		bool isreq = (eap_hdr->code == EAP_CODE_REQUEST);
		switch (eap_hdr->type) {
		case EAP_TYPE_IDENT:
			if (isreq) {
				DHD_STATLOG_DATA(dhd, ST(EAP_REQ_IDENTITY), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Request, Identity");
			} else {
				DHD_STATLOG_DATA(dhd, ST(EAP_RESP_IDENTITY), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Response, Identity");
			}
			break;
		case EAP_TYPE_TLS:
			if (isreq) {
				DHD_STATLOG_DATA(dhd, ST(EAP_REQ_TLS), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Request, TLS");
			} else {
				DHD_STATLOG_DATA(dhd, ST(EAP_RESP_TLS), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Response, TLS");
			}
			break;
		case EAP_TYPE_LEAP:
			if (isreq) {
				DHD_STATLOG_DATA(dhd, ST(EAP_REQ_LEAP), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Request, LEAP");
			} else {
				DHD_STATLOG_DATA(dhd, ST(EAP_RESP_LEAP), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Response, LEAP");
			}
			break;
		case EAP_TYPE_TTLS:
			if (isreq) {
				DHD_STATLOG_DATA(dhd, ST(EAP_REQ_TTLS), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Request, TTLS");
			} else {
				DHD_STATLOG_DATA(dhd, ST(EAP_RESP_TTLS), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Response, TTLS");
			}
			break;
		case EAP_TYPE_AKA:
			if (isreq) {
				DHD_STATLOG_DATA(dhd, ST(EAP_REQ_AKA), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Request, AKA");
			} else {
				DHD_STATLOG_DATA(dhd, ST(EAP_RESP_AKA), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Response, AKA");
			}
			break;
		case EAP_TYPE_PEAP:
			if (isreq) {
				DHD_STATLOG_DATA(dhd, ST(EAP_REQ_PEAP), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Request, PEAP");
			} else {
				DHD_STATLOG_DATA(dhd, ST(EAP_RESP_PEAP), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Response, PEAP");
			}
			break;
		case EAP_TYPE_FAST:
			if (isreq) {
				DHD_STATLOG_DATA(dhd, ST(EAP_REQ_FAST), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Request, FAST");
			} else {
				DHD_STATLOG_DATA(dhd, ST(EAP_RESP_FAST), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Response, FAST");
			}
			break;
		case EAP_TYPE_PSK:
			if (isreq) {
				DHD_STATLOG_DATA(dhd, ST(EAP_REQ_PSK), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Request, PSK");
			} else {
				DHD_STATLOG_DATA(dhd, ST(EAP_RESP_PSK), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Response, PSK");
			}
			break;
		case EAP_TYPE_AKAP:
			if (isreq) {
				DHD_STATLOG_DATA(dhd, ST(EAP_REQ_AKAP), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Request, AKAP");
			} else {
				DHD_STATLOG_DATA(dhd, ST(EAP_RESP_AKAP), ifidx, tx, cond);
				EAP_PRINT("EAP Packet, Response, AKAP");
			}
			break;
		case EAP_TYPE_EXP:
			dhd_dump_wsc_message(dhd, ifidx, pktdata, pktlen, tx,
				pkthash, pktfate);
			break;
		default:
			break;
		}
	} else if (eap_hdr->code == EAP_CODE_SUCCESS) {
		DHD_STATLOG_DATA(dhd, ST(EAP_SUCCESS), ifidx, tx, cond);
		EAP_PRINT("EAP Packet, Success");
	} else if (eap_hdr->code == EAP_CODE_FAILURE) {
		DHD_STATLOG_DATA(dhd, ST(EAP_FAILURE), ifidx, tx, cond);
		EAP_PRINT("EAP Packet, Failure");
	}
}

static void
dhd_dump_eapol_4way_message(dhd_pub_t *dhd, int ifidx, uint8 *pktdata, bool tx,
	uint32 *pkthash, uint16 *pktfate)
{
	eapol_header_t *eapol_hdr;
	eapol_key_hdr_t *eap_key;
	msg_eapol_t type;
	char *ifname;
	bool cond;

	if (!pktdata) {
		DHD_PKTDUMP(("%s: pktdata is NULL\n", __FUNCTION__));
		return;
	}

	type = dhd_is_4way_msg(pktdata);
	ifname = dhd_ifname(dhd, ifidx);
	eapol_hdr = (eapol_header_t *)pktdata;
	eap_key = (eapol_key_hdr_t *)(eapol_hdr->body);
	cond = (tx && pktfate) ? FALSE : TRUE;

	if (eap_key->type != EAPOL_WPA2_KEY) {
		EAP_PRINT_OTHER("NON EAPOL_WPA2_KEY");
		return;
	}

	switch (type) {
	case EAPOL_4WAY_M1:
		DHD_STATLOG_DATA(dhd, ST(EAPOL_M1), ifidx, tx, cond);
		EAP_PRINT("EAPOL Packet, 4-way handshake, M1");
		break;
	case EAPOL_4WAY_M2:
		DHD_STATLOG_DATA(dhd, ST(EAPOL_M2), ifidx, tx, cond);
		EAP_PRINT("EAPOL Packet, 4-way handshake, M2");
		break;
	case EAPOL_4WAY_M3:
		DHD_STATLOG_DATA(dhd, ST(EAPOL_M3), ifidx, tx, cond);
		EAP_PRINT("EAPOL Packet, 4-way handshake, M3");
		break;
	case EAPOL_4WAY_M4:
		DHD_STATLOG_DATA(dhd, ST(EAPOL_M4), ifidx, tx, cond);
		EAP_PRINT("EAPOL Packet, 4-way handshake, M4");
		break;
	case EAPOL_GROUPKEY_M1:
		DHD_STATLOG_DATA(dhd, ST(EAPOL_GROUPKEY_M1), ifidx, tx, cond);
		EAP_PRINT_REPLAY("EAPOL Packet, GROUP Key handshake, M1");
		break;
	case EAPOL_GROUPKEY_M2:
		DHD_STATLOG_DATA(dhd, ST(EAPOL_GROUPKEY_M2), ifidx, tx, cond);
		EAP_PRINT_REPLAY("EAPOL Packet, GROUP Key handshake, M2");
		if (ifidx == 0 && tx && pktfate) {
			dhd_dump_mod_pkt_timer(dhd, PKT_CNT_RSN_GRPKEY_UP);
		}
		break;
	default:
		DHD_STATLOG_DATA(dhd, ST(8021X_OTHER), ifidx, tx, cond);
		EAP_PRINT_OTHER("OTHER 4WAY");
		break;
	}
}

void
dhd_dump_eapol_message(dhd_pub_t *dhd, int ifidx, uint8 *pktdata,
	uint32 pktlen, bool tx, uint32 *pkthash, uint16 *pktfate)
{
	char *ifname;
	eapol_header_t *eapol_hdr = (eapol_header_t *)pktdata;
	bool cond;

	if (!pktdata) {
		DHD_ERROR(("%s: pktdata is NULL\n", __FUNCTION__));
		return;
	}

	eapol_hdr = (eapol_header_t *)pktdata;
	ifname = dhd_ifname(dhd, ifidx);
	cond = (tx && pktfate) ? FALSE : TRUE;

	if (eapol_hdr->type == EAP_PACKET) {
		dhd_dump_eap_packet(dhd, ifidx, pktdata, pktlen, tx,
			pkthash, pktfate);
	} else if (eapol_hdr->type == EAPOL_START) {
		DHD_STATLOG_DATA(dhd, ST(EAPOL_START), ifidx, tx, cond);
		EAP_PRINT("EAP Packet, EAPOL-Start");
	} else if (eapol_hdr->type == EAPOL_KEY) {
		dhd_dump_eapol_4way_message(dhd, ifidx, pktdata, tx,
			pkthash, pktfate);
	} else {
		DHD_STATLOG_DATA(dhd, ST(8021X_OTHER), ifidx, tx, cond);
		EAP_PRINT_OTHER("OTHER 8021X");
	}
}
#endif /* DHD_8021X_DUMP */

#ifdef DHD_DHCP_DUMP
#define BOOTP_CHADDR_LEN		16
#define BOOTP_SNAME_LEN			64
#define BOOTP_FILE_LEN			128
#define BOOTP_MIN_DHCP_OPT_LEN		312
#define BOOTP_MAGIC_COOKIE_LEN		4

#define DHCP_MSGTYPE_DISCOVER		1
#define DHCP_MSGTYPE_OFFER		2
#define DHCP_MSGTYPE_REQUEST		3
#define DHCP_MSGTYPE_DECLINE		4
#define DHCP_MSGTYPE_ACK		5
#define DHCP_MSGTYPE_NAK		6
#define DHCP_MSGTYPE_RELEASE		7
#define DHCP_MSGTYPE_INFORM		8

#define DHCP_PRINT(str) \
	do { \
		if (tx) { \
			DHD_PKTDUMP((str " %s[%s][%s] [TX] -" TXFATE_FMT "\n", \
				typestr, opstr, ifname, \
				TX_PKTHASH(pkthash), TX_FATE(pktfate))); \
		} else { \
			DHD_PKTDUMP((str " %s[%s][%s] [RX]\n", \
				typestr, opstr, ifname)); \
		} \
	} while (0)

typedef struct bootp_fmt {
	struct ipv4_hdr iph;
	struct bcmudp_hdr udph;
	uint8 op;
	uint8 htype;
	uint8 hlen;
	uint8 hops;
	uint32 transaction_id;
	uint16 secs;
	uint16 flags;
	uint32 client_ip;
	uint32 assigned_ip;
	uint32 server_ip;
	uint32 relay_ip;
	uint8 hw_address[BOOTP_CHADDR_LEN];
	uint8 server_name[BOOTP_SNAME_LEN];
	uint8 file_name[BOOTP_FILE_LEN];
	uint8 options[BOOTP_MIN_DHCP_OPT_LEN];
} PACKED_STRUCT bootp_fmt_t;

static const uint8 bootp_magic_cookie[4] = { 99, 130, 83, 99 };
static char dhcp_ops[][10] = {
	"NA", "REQUEST", "REPLY"
};
static char dhcp_types[][10] = {
	"NA", "DISCOVER", "OFFER", "REQUEST", "DECLINE", "ACK", "NAK", "RELEASE", "INFORM"
};

static const int dhcp_types_stat[9] = {
	ST(INVALID), ST(DHCP_DISCOVER), ST(DHCP_OFFER), ST(DHCP_REQUEST),
	ST(DHCP_DECLINE), ST(DHCP_ACK), ST(DHCP_NAK), ST(DHCP_RELEASE),
	ST(DHCP_INFORM)
};

void
dhd_dhcp_dump(dhd_pub_t *dhdp, int ifidx, uint8 *pktdata, bool tx,
	uint32 *pkthash, uint16 *pktfate)
{
	bootp_fmt_t *b = (bootp_fmt_t *)&pktdata[ETHER_HDR_LEN];
	struct ipv4_hdr *iph = &b->iph;
	uint8 *ptr, *opt, *end = (uint8 *) b + ntohs(b->iph.tot_len);
	int dhcp_type = 0, len, opt_len;
	char *ifname = NULL, *typestr = NULL, *opstr = NULL;
	bool cond;

	/* check IP header */
	if ((IPV4_HLEN(iph) < IPV4_HLEN_MIN) ||
		IP_VER(iph) != IP_VER_4 ||
		IPV4_PROT(iph) != IP_PROT_UDP) {
		return;
	}

	/* check UDP port for bootp (67, 68) */
	if (b->udph.src_port != htons(DHCP_PORT_SERVER) &&
		b->udph.src_port != htons(DHCP_PORT_CLIENT) &&
		b->udph.dst_port != htons(DHCP_PORT_SERVER) &&
		b->udph.dst_port != htons(DHCP_PORT_CLIENT)) {
		return;
	}

	/* check header length */
	if (ntohs(iph->tot_len) < ntohs(b->udph.len) + sizeof(struct bcmudp_hdr)) {
		return;
	}

	ifname = dhd_ifname(dhdp, ifidx);
	cond = (tx && pktfate) ? FALSE : TRUE;
	len = ntohs(b->udph.len) - sizeof(struct bcmudp_hdr);
	opt_len = len - (sizeof(*b) - sizeof(struct ipv4_hdr) -
		sizeof(struct bcmudp_hdr) - sizeof(b->options));

	/* parse bootp options */
	if (opt_len >= BOOTP_MAGIC_COOKIE_LEN &&
		!memcmp(b->options, bootp_magic_cookie, BOOTP_MAGIC_COOKIE_LEN)) {
		ptr = &b->options[BOOTP_MAGIC_COOKIE_LEN];
		while (ptr < end && *ptr != 0xff) {
			opt = ptr++;
			if (*opt == 0) {
				continue;
			}
			ptr += *ptr + 1;
			if (ptr >= end) {
				break;
			}
			if (*opt == DHCP_OPT_MSGTYPE) {
				if (opt[1]) {
					dhcp_type = opt[2];
					typestr = dhcp_types[dhcp_type];
					opstr = dhcp_ops[b->op];
					DHD_STATLOG_DATA(dhdp, dhcp_types_stat[dhcp_type],
						ifidx, tx, cond);
					DHCP_PRINT("DHCP");
					break;
				}
			}
		}
	}
}
#endif /* DHD_DHCP_DUMP */

#ifdef DHD_ICMP_DUMP
#define ICMP_TYPE_DEST_UNREACH		3
#define ICMP_ECHO_SEQ_OFFSET		6
#define ICMP_ECHO_SEQ(h) (*(uint16 *)((uint8 *)(h) + (ICMP_ECHO_SEQ_OFFSET)))
#define ICMP_PING_PRINT(str) \
	do { \
		if (tx) { \
			DHD_PKTDUMP_MEM((str "[%s][TX] : SEQNUM=%d" \
				TXFATE_FMT "\n", ifname, seqnum, \
				TX_PKTHASH(pkthash), TX_FATE(pktfate))); \
		} else { \
			DHD_PKTDUMP_MEM((str "[%s][RX] : SEQNUM=%d\n", \
				ifname, seqnum)); \
		} \
	} while (0)

#define ICMP_PRINT(str) \
	do { \
		if (tx) { \
			DHD_PKTDUMP_MEM((str "[%s][TX] : TYPE=%d, CODE=%d" \
				TXFATE_FMT "\n", ifname, type, code, \
				TX_PKTHASH(pkthash), TX_FATE(pktfate))); \
		} else { \
			DHD_PKTDUMP_MEM((str "[%s][RX] : TYPE=%d," \
				" CODE=%d\n", ifname, type, code)); \
		} \
	} while (0)

void
dhd_icmp_dump(dhd_pub_t *dhdp, int ifidx, uint8 *pktdata, bool tx,
	uint32 *pkthash, uint16 *pktfate)
{
	uint8 *pkt = (uint8 *)&pktdata[ETHER_HDR_LEN];
	struct ipv4_hdr *iph = (struct ipv4_hdr *)pkt;
	struct bcmicmp_hdr *icmph;
	char *ifname;
	bool cond;
	uint16 seqnum, type, code;

	/* check IP header */
	if ((IPV4_HLEN(iph) < IPV4_HLEN_MIN) ||
		IP_VER(iph) != IP_VER_4 ||
		IPV4_PROT(iph) != IP_PROT_ICMP) {
		return;
	}

	/* check header length */
	if (ntohs(iph->tot_len) - IPV4_HLEN(iph) < sizeof(struct bcmicmp_hdr)) {
		return;
	}

	ifname = dhd_ifname(dhdp, ifidx);
	cond = (tx && pktfate) ? FALSE : TRUE;
	icmph = (struct bcmicmp_hdr *)((uint8 *)pkt + sizeof(struct ipv4_hdr));
	seqnum = 0;
	type = icmph->type;
	code = icmph->code;
	if (type == ICMP_TYPE_ECHO_REQUEST) {
		seqnum = ntoh16(ICMP_ECHO_SEQ(icmph));
		DHD_STATLOG_DATA(dhdp, ST(ICMP_PING_REQ), ifidx, tx, cond);
		ICMP_PING_PRINT("PING REQUEST");
	} else if (type == ICMP_TYPE_ECHO_REPLY) {
		seqnum = ntoh16(ICMP_ECHO_SEQ(icmph));
		DHD_STATLOG_DATA(dhdp, ST(ICMP_PING_RESP), ifidx, tx, cond);
		ICMP_PING_PRINT("PING REPLY");
	} else if (type == ICMP_TYPE_DEST_UNREACH) {
		DHD_STATLOG_DATA(dhdp, ST(ICMP_DEST_UNREACH), ifidx, tx, cond);
		ICMP_PRINT("ICMP DEST UNREACH");
	} else {
		DHD_STATLOG_DATA(dhdp, ST(ICMP_OTHER), ifidx, tx, cond);
		ICMP_PRINT("ICMP OTHER");
	}
}
#endif /* DHD_ICMP_DUMP */

#ifdef DHD_ARP_DUMP
#define ARP_PRINT(str) \
	do { \
		if (tx) { \
			if (dump_enabled && pktfate && !TX_FATE_ACKED(pktfate)) { \
				DHD_PKTDUMP((str "[%s] [TX]" TXFATE_FMT "\n", \
					ifname, TX_PKTHASH(pkthash), \
					TX_FATE(pktfate))); \
			} else { \
				DHD_PKTDUMP_MEM((str "[%s] [TX]" TXFATE_FMT "\n", \
					ifname, TX_PKTHASH(pkthash), \
					TX_FATE(pktfate))); \
			} \
		} else { \
			DHD_PKTDUMP_MEM((str "[%s] [RX]\n", ifname)); \
		} \
	} while (0)

#define ARP_PRINT_OTHER(str) \
	do { \
		if (tx) { \
			if (dump_enabled && pktfate && !TX_FATE_ACKED(pktfate)) { \
				DHD_PKTDUMP((str "[%s] [TX] op_code=%d" \
					TXFATE_FMT "\n", ifname, opcode, \
					TX_PKTHASH(pkthash), \
					TX_FATE(pktfate))); \
			} else { \
				DHD_PKTDUMP_MEM((str "[%s] [TX] op_code=%d" \
				TXFATE_FMT "\n", ifname, opcode, \
				TX_PKTHASH(pkthash), TX_FATE(pktfate))); \
			} \
		} else { \
			DHD_PKTDUMP_MEM((str "[%s] [RX] op_code=%d\n", \
				ifname, opcode)); \
		} \
	} while (0)

void
dhd_arp_dump(dhd_pub_t *dhdp, int ifidx, uint8 *pktdata, bool tx,
	uint32 *pkthash, uint16 *pktfate)
{
	uint8 *pkt = (uint8 *)&pktdata[ETHER_HDR_LEN];
	struct bcmarp *arph = (struct bcmarp *)pkt;
	char *ifname;
	uint16 opcode;
	bool cond, dump_enabled;

	/* validation check */
	if (arph->htype != hton16(HTYPE_ETHERNET) ||
		arph->hlen != ETHER_ADDR_LEN ||
		arph->plen != 4) {
		return;
	}

	ifname = dhd_ifname(dhdp, ifidx);
	opcode = ntoh16(arph->oper);
	cond = (tx && pktfate) ? FALSE : TRUE;
	dump_enabled = dhd_dump_pkt_enabled(dhdp);
	if (opcode == ARP_OPC_REQUEST) {
		DHD_STATLOG_DATA(dhdp, ST(ARP_REQ), ifidx, tx, cond);
		ARP_PRINT("ARP REQUEST");
	} else if (opcode == ARP_OPC_REPLY) {
		DHD_STATLOG_DATA(dhdp, ST(ARP_RESP), ifidx, tx, cond);
		ARP_PRINT("ARP RESPONSE");
	} else {
		ARP_PRINT_OTHER("ARP OTHER");
	}

	if (ifidx == 0) {
		dhd_dump_pkt_cnts_inc(dhdp, tx, pktfate, PKT_CNT_TYPE_ARP);
	}
}
#endif /* DHD_ARP_DUMP */

#ifdef DHD_DNS_DUMP
typedef struct dns_fmt {
	struct ipv4_hdr iph;
	struct bcmudp_hdr udph;
	uint16 id;
	uint16 flags;
	uint16 qdcount;
	uint16 ancount;
	uint16 nscount;
	uint16 arcount;
} PACKED_STRUCT dns_fmt_t;

#define UDP_PORT_DNS		53
#define DNS_QR_LOC		15
#define DNS_OPCODE_LOC		11
#define DNS_RCODE_LOC		0
#define DNS_QR_MASK		((0x1) << (DNS_QR_LOC))
#define DNS_OPCODE_MASK		((0xF) << (DNS_OPCODE_LOC))
#define DNS_RCODE_MASK		((0xF) << (DNS_RCODE_LOC))
#define GET_DNS_QR(flags)	(((flags) & (DNS_QR_MASK)) >> (DNS_QR_LOC))
#define GET_DNS_OPCODE(flags)	(((flags) & (DNS_OPCODE_MASK)) >> (DNS_OPCODE_LOC))
#define GET_DNS_RCODE(flags)	(((flags) & (DNS_RCODE_MASK)) >> (DNS_RCODE_LOC))
#define DNS_UNASSIGNED_OPCODE(flags) ((GET_DNS_OPCODE(flags) >= (6)))

static const char dns_opcode_types[][11] = {
	"QUERY", "IQUERY", "STATUS", "UNASSIGNED", "NOTIFY", "UPDATE"
};

#define DNSOPCODE(op)	\
	(DNS_UNASSIGNED_OPCODE(flags) ? "UNASSIGNED" : dns_opcode_types[op])

#define DNS_REQ_PRINT(str) \
	do { \
		if (tx) { \
			if (dump_enabled && pktfate && !TX_FATE_ACKED(pktfate)) { \
				DHD_PKTDUMP((str "[%s] [TX] ID:0x%04X OPCODE:%s" \
					TXFATE_FMT "\n", ifname, id, DNSOPCODE(opcode), \
					TX_PKTHASH(pkthash), TX_FATE(pktfate))); \
			} else { \
				DHD_PKTDUMP_MEM((str "[%s] [TX] ID:0x%04X OPCODE:%s" \
					TXFATE_FMT "\n", ifname, id, DNSOPCODE(opcode), \
					TX_PKTHASH(pkthash), TX_FATE(pktfate))); \
			} \
		} else { \
			DHD_PKTDUMP_MEM((str "[%s] [RX] ID:0x%04X OPCODE:%s\n", \
				ifname, id, DNSOPCODE(opcode))); \
		} \
	} while (0)

#define DNS_RESP_PRINT(str) \
	do { \
		if (tx) { \
			if (dump_enabled && pktfate && !TX_FATE_ACKED(pktfate)) { \
				DHD_PKTDUMP((str "[%s] [TX] ID:0x%04X OPCODE:%s RCODE:%d" \
					TXFATE_FMT "\n", ifname, id, DNSOPCODE(opcode), \
					GET_DNS_RCODE(flags), TX_PKTHASH(pkthash), \
					TX_FATE(pktfate))); \
			} else { \
				DHD_PKTDUMP_MEM((str "[%s] [TX] ID:0x%04X OPCODE:%s RCODE:%d" \
					TXFATE_FMT "\n", ifname, id, DNSOPCODE(opcode), \
					GET_DNS_RCODE(flags), TX_PKTHASH(pkthash), \
					TX_FATE(pktfate))); \
			} \
		} else { \
			DHD_PKTDUMP_MEM((str "[%s] [RX] ID:0x%04X OPCODE:%s RCODE:%d\n", \
				ifname, id, DNSOPCODE(opcode), GET_DNS_RCODE(flags))); \
		} \
	} while (0)

void
dhd_dns_dump(dhd_pub_t *dhdp, int ifidx, uint8 *pktdata, bool tx,
	uint32 *pkthash, uint16 *pktfate)
{
	dns_fmt_t *dnsh = (dns_fmt_t *)&pktdata[ETHER_HDR_LEN];
	struct ipv4_hdr *iph = &dnsh->iph;
	uint16 flags, opcode, id;
	char *ifname;
	bool cond, dump_enabled;

	/* check IP header */
	if ((IPV4_HLEN(iph) < IPV4_HLEN_MIN) ||
		IP_VER(iph) != IP_VER_4 ||
		IPV4_PROT(iph) != IP_PROT_UDP) {
		return;
	}

	/* check UDP port for DNS */
	if (dnsh->udph.src_port != hton16(UDP_PORT_DNS) &&
		dnsh->udph.dst_port != hton16(UDP_PORT_DNS)) {
		return;
	}

	/* check header length */
	if (ntoh16(iph->tot_len) < (ntoh16(dnsh->udph.len) +
		sizeof(struct bcmudp_hdr))) {
		return;
	}

	ifname = dhd_ifname(dhdp, ifidx);
	cond = (tx && pktfate) ? FALSE : TRUE;
	dump_enabled = dhd_dump_pkt_enabled(dhdp);
	flags = hton16(dnsh->flags);
	opcode = GET_DNS_OPCODE(flags);
	id = hton16(dnsh->id);
	if (GET_DNS_QR(flags)) {
		/* Response */
		DHD_STATLOG_DATA(dhdp, ST(DNS_RESP), ifidx, tx, cond);
		DNS_RESP_PRINT("DNS RESPONSE");
	} else {
		/* Request */
		DHD_STATLOG_DATA(dhdp, ST(DNS_QUERY), ifidx, tx, cond);
		DNS_REQ_PRINT("DNS REQUEST");
	}

	if (ifidx == 0) {
		dhd_dump_pkt_cnts_inc(dhdp, tx, pktfate, PKT_CNT_TYPE_DNS);
	}
}
#endif /* DHD_DNS_DUMP */

#ifdef DHD_RX_DUMP
void
dhd_rx_pkt_dump(dhd_pub_t *dhdp, int ifidx, uint8 *pktdata, uint32 pktlen)
{
	struct ether_header *eh;
	uint16 protocol;
	char *pkttype = "UNKNOWN";

	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return;
	}

	if (!pktdata) {
		DHD_ERROR(("%s: pktdata is NULL\n", __FUNCTION__));
		return;
	}

	eh = (struct ether_header *)pktdata;
	protocol = hton16(eh->ether_type);
	BCM_REFERENCE(pktlen);

	switch (protocol) {
	case ETHER_TYPE_IP:
		pkttype = "IP";
		break;
	case ETHER_TYPE_ARP:
		pkttype = "ARP";
		break;
	case ETHER_TYPE_BRCM:
		pkttype = "BRCM";
		break;
	case ETHER_TYPE_802_1X:
		pkttype = "802.1X";
		break;
	case ETHER_TYPE_WAI:
		pkttype = "WAPI";
		break;
	default:
		break;
	}

	DHD_PKTDUMP(("RX DUMP[%s] - %s\n", dhd_ifname(dhdp, ifidx), pkttype));
	if (protocol != ETHER_TYPE_BRCM) {
		if (pktdata[0] == 0xFF) {
			DHD_PKTDUMP(("%s: BROADCAST\n", __FUNCTION__));
		} else if (pktdata[0] & 1) {
			DHD_PKTDUMP(("%s: MULTICAST: " MACDBG "\n",
				__FUNCTION__, MAC2STRDBG(pktdata)));
		}
#ifdef DHD_RX_FULL_DUMP
		{
			int k;
			for (k = 0; k < pktlen; k++) {
				DHD_PKTDUMP(("%02X ", pktdata[k]));
				if ((k & 15) == 15)
					DHD_PKTDUMP(("\n"));
			}
			DHD_PKTDUMP(("\n"));
		}
#endif /* DHD_RX_FULL_DUMP */
	}
}
#endif /* DHD_RX_DUMP */
