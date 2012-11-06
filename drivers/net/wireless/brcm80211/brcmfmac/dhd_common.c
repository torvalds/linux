/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <asm/unaligned.h>
#include <defs.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_proto.h"
#include "dhd_dbg.h"
#include "fwil.h"

#define BRCM_OUI			"\x00\x10\x18"
#define DOT11_OUI_LEN			3
#define BCMILCP_BCM_SUBTYPE_EVENT	1
#define PKTFILTER_BUF_SIZE		128
#define BRCMF_ARPOL_MODE		0xb	/* agent|snoop|peer_autoreply */
#define BRCMF_DEFAULT_BCN_TIMEOUT	3
#define BRCMF_DEFAULT_SCAN_CHANNEL_TIME	40
#define BRCMF_DEFAULT_SCAN_UNASSOC_TIME	40
#define BRCMF_DEFAULT_PACKET_FILTER	"100 0 0 0 0x01 0x00"

#define MSGTRACE_VERSION	1

#ifdef DEBUG
static const char brcmf_version[] =
	"Dongle Host Driver, version " BRCMF_VERSION_STR "\nCompiled on "
	__DATE__ " at " __TIME__;
#else
static const char brcmf_version[] =
	"Dongle Host Driver, version " BRCMF_VERSION_STR;
#endif

/* Message trace header */
struct msgtrace_hdr {
	u8 version;
	u8 spare;
	__be16 len;		/* Len of the trace */
	__be32 seqnum;		/* Sequence number of message. Useful
				 * if the messsage has been lost
				 * because of DMA error or a bus reset
				 * (ex: SDIO Func2)
				 */
	__be32 discarded_bytes;	/* Number of discarded bytes because of
				 trace overflow  */
	__be32 discarded_printf;	/* Number of discarded printf
				 because of trace overflow */
} __packed;


bool brcmf_c_prec_enq(struct device *dev, struct pktq *q,
		      struct sk_buff *pkt, int prec)
{
	struct sk_buff *p;
	int eprec = -1;		/* precedence to evict from */
	bool discard_oldest;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	/* Fast case, precedence queue is not full and we are also not
	 * exceeding total queue length
	 */
	if (!pktq_pfull(q, prec) && !pktq_full(q)) {
		brcmu_pktq_penq(q, prec, pkt);
		return true;
	}

	/* Determine precedence from which to evict packet, if any */
	if (pktq_pfull(q, prec))
		eprec = prec;
	else if (pktq_full(q)) {
		p = brcmu_pktq_peek_tail(q, &eprec);
		if (eprec > prec)
			return false;
	}

	/* Evict if needed */
	if (eprec >= 0) {
		/* Detect queueing to unconfigured precedence */
		discard_oldest = ac_bitmap_tst(drvr->wme_dp, eprec);
		if (eprec == prec && !discard_oldest)
			return false;	/* refuse newer (incoming) packet */
		/* Evict packet according to discard policy */
		p = discard_oldest ? brcmu_pktq_pdeq(q, eprec) :
			brcmu_pktq_pdeq_tail(q, eprec);
		if (p == NULL)
			brcmf_dbg(ERROR, "brcmu_pktq_penq() failed, oldest %d\n",
				  discard_oldest);

		brcmu_pkt_buf_free_skb(p);
	}

	/* Enqueue */
	p = brcmu_pktq_penq(q, prec, pkt);
	if (p == NULL)
		brcmf_dbg(ERROR, "brcmu_pktq_penq() failed\n");

	return p != NULL;
}

#ifdef DEBUG
static void
brcmf_c_show_host_event(struct brcmf_event_msg *event, void *event_data)
{
	uint i, status, reason;
	bool group = false, flush_txq = false, link = false;
	char *auth_str, *event_name;
	unsigned char *buf;
	char err_msg[256], eabuf[ETHER_ADDR_STR_LEN];
	static struct {
		uint event;
		char *event_name;
	} event_names[] = {
		{
		BRCMF_E_SET_SSID, "SET_SSID"}, {
		BRCMF_E_JOIN, "JOIN"}, {
		BRCMF_E_START, "START"}, {
		BRCMF_E_AUTH, "AUTH"}, {
		BRCMF_E_AUTH_IND, "AUTH_IND"}, {
		BRCMF_E_DEAUTH, "DEAUTH"}, {
		BRCMF_E_DEAUTH_IND, "DEAUTH_IND"}, {
		BRCMF_E_ASSOC, "ASSOC"}, {
		BRCMF_E_ASSOC_IND, "ASSOC_IND"}, {
		BRCMF_E_REASSOC, "REASSOC"}, {
		BRCMF_E_REASSOC_IND, "REASSOC_IND"}, {
		BRCMF_E_DISASSOC, "DISASSOC"}, {
		BRCMF_E_DISASSOC_IND, "DISASSOC_IND"}, {
		BRCMF_E_QUIET_START, "START_QUIET"}, {
		BRCMF_E_QUIET_END, "END_QUIET"}, {
		BRCMF_E_BEACON_RX, "BEACON_RX"}, {
		BRCMF_E_LINK, "LINK"}, {
		BRCMF_E_MIC_ERROR, "MIC_ERROR"}, {
		BRCMF_E_NDIS_LINK, "NDIS_LINK"}, {
		BRCMF_E_ROAM, "ROAM"}, {
		BRCMF_E_TXFAIL, "TXFAIL"}, {
		BRCMF_E_PMKID_CACHE, "PMKID_CACHE"}, {
		BRCMF_E_RETROGRADE_TSF, "RETROGRADE_TSF"}, {
		BRCMF_E_PRUNE, "PRUNE"}, {
		BRCMF_E_AUTOAUTH, "AUTOAUTH"}, {
		BRCMF_E_EAPOL_MSG, "EAPOL_MSG"}, {
		BRCMF_E_SCAN_COMPLETE, "SCAN_COMPLETE"}, {
		BRCMF_E_ADDTS_IND, "ADDTS_IND"}, {
		BRCMF_E_DELTS_IND, "DELTS_IND"}, {
		BRCMF_E_BCNSENT_IND, "BCNSENT_IND"}, {
		BRCMF_E_BCNRX_MSG, "BCNRX_MSG"}, {
		BRCMF_E_BCNLOST_MSG, "BCNLOST_MSG"}, {
		BRCMF_E_ROAM_PREP, "ROAM_PREP"}, {
		BRCMF_E_PFN_NET_FOUND, "PNO_NET_FOUND"}, {
		BRCMF_E_PFN_NET_LOST, "PNO_NET_LOST"}, {
		BRCMF_E_RESET_COMPLETE, "RESET_COMPLETE"}, {
		BRCMF_E_JOIN_START, "JOIN_START"}, {
		BRCMF_E_ROAM_START, "ROAM_START"}, {
		BRCMF_E_ASSOC_START, "ASSOC_START"}, {
		BRCMF_E_IBSS_ASSOC, "IBSS_ASSOC"}, {
		BRCMF_E_RADIO, "RADIO"}, {
		BRCMF_E_PSM_WATCHDOG, "PSM_WATCHDOG"}, {
		BRCMF_E_PROBREQ_MSG, "PROBREQ_MSG"}, {
		BRCMF_E_SCAN_CONFIRM_IND, "SCAN_CONFIRM_IND"}, {
		BRCMF_E_PSK_SUP, "PSK_SUP"}, {
		BRCMF_E_COUNTRY_CODE_CHANGED, "COUNTRY_CODE_CHANGED"}, {
		BRCMF_E_EXCEEDED_MEDIUM_TIME, "EXCEEDED_MEDIUM_TIME"}, {
		BRCMF_E_ICV_ERROR, "ICV_ERROR"}, {
		BRCMF_E_UNICAST_DECODE_ERROR, "UNICAST_DECODE_ERROR"}, {
		BRCMF_E_MULTICAST_DECODE_ERROR, "MULTICAST_DECODE_ERROR"}, {
		BRCMF_E_TRACE, "TRACE"}, {
		BRCMF_E_ACTION_FRAME, "ACTION FRAME"}, {
		BRCMF_E_ACTION_FRAME_COMPLETE, "ACTION FRAME TX COMPLETE"}, {
		BRCMF_E_IF, "IF"}, {
		BRCMF_E_RSSI, "RSSI"}, {
		BRCMF_E_PFN_SCAN_COMPLETE, "SCAN_COMPLETE"}, {
		BRCMF_E_ESCAN_RESULT, "ESCAN_RESULT"}
	};
	uint event_type, flags, auth_type, datalen;
	static u32 seqnum_prev;
	struct msgtrace_hdr hdr;
	u32 nblost;
	char *s, *p;

	event_type = be32_to_cpu(event->event_type);
	flags = be16_to_cpu(event->flags);
	status = be32_to_cpu(event->status);
	reason = be32_to_cpu(event->reason);
	auth_type = be32_to_cpu(event->auth_type);
	datalen = be32_to_cpu(event->datalen);
	/* debug dump of event messages */
	sprintf(eabuf, "%pM", event->addr);

	event_name = "UNKNOWN";
	for (i = 0; i < ARRAY_SIZE(event_names); i++) {
		if (event_names[i].event == event_type)
			event_name = event_names[i].event_name;
	}

	brcmf_dbg(EVENT, "EVENT: %s, event ID = %d\n", event_name, event_type);
	brcmf_dbg(EVENT, "flags 0x%04x, status %d, reason %d, auth_type %d MAC %s\n",
		  flags, status, reason, auth_type, eabuf);

	if (flags & BRCMF_EVENT_MSG_LINK)
		link = true;
	if (flags & BRCMF_EVENT_MSG_GROUP)
		group = true;
	if (flags & BRCMF_EVENT_MSG_FLUSHTXQ)
		flush_txq = true;

	switch (event_type) {
	case BRCMF_E_START:
	case BRCMF_E_DEAUTH:
	case BRCMF_E_DISASSOC:
		brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s\n", event_name, eabuf);
		break;

	case BRCMF_E_ASSOC_IND:
	case BRCMF_E_REASSOC_IND:
		brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s\n", event_name, eabuf);
		break;

	case BRCMF_E_ASSOC:
	case BRCMF_E_REASSOC:
		if (status == BRCMF_E_STATUS_SUCCESS)
			brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, SUCCESS\n",
				  event_name, eabuf);
		else if (status == BRCMF_E_STATUS_TIMEOUT)
			brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, TIMEOUT\n",
				  event_name, eabuf);
		else if (status == BRCMF_E_STATUS_FAIL)
			brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, FAILURE, reason %d\n",
				  event_name, eabuf, (int)reason);
		else
			brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, unexpected status %d\n",
				  event_name, eabuf, (int)status);
		break;

	case BRCMF_E_DEAUTH_IND:
	case BRCMF_E_DISASSOC_IND:
		brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, reason %d\n",
			  event_name, eabuf, (int)reason);
		break;

	case BRCMF_E_AUTH:
	case BRCMF_E_AUTH_IND:
		if (auth_type == WLAN_AUTH_OPEN)
			auth_str = "Open System";
		else if (auth_type == WLAN_AUTH_SHARED_KEY)
			auth_str = "Shared Key";
		else {
			sprintf(err_msg, "AUTH unknown: %d", (int)auth_type);
			auth_str = err_msg;
		}
		if (event_type == BRCMF_E_AUTH_IND)
			brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, %s\n",
				  event_name, eabuf, auth_str);
		else if (status == BRCMF_E_STATUS_SUCCESS)
			brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, %s, SUCCESS\n",
				  event_name, eabuf, auth_str);
		else if (status == BRCMF_E_STATUS_TIMEOUT)
			brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, %s, TIMEOUT\n",
				  event_name, eabuf, auth_str);
		else if (status == BRCMF_E_STATUS_FAIL) {
			brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, %s, FAILURE, reason %d\n",
				  event_name, eabuf, auth_str, (int)reason);
		}

		break;

	case BRCMF_E_JOIN:
	case BRCMF_E_ROAM:
	case BRCMF_E_SET_SSID:
		if (status == BRCMF_E_STATUS_SUCCESS)
			brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s\n",
				  event_name, eabuf);
		else if (status == BRCMF_E_STATUS_FAIL)
			brcmf_dbg(EVENT, "MACEVENT: %s, failed\n", event_name);
		else if (status == BRCMF_E_STATUS_NO_NETWORKS)
			brcmf_dbg(EVENT, "MACEVENT: %s, no networks found\n",
				  event_name);
		else
			brcmf_dbg(EVENT, "MACEVENT: %s, unexpected status %d\n",
				  event_name, (int)status);
		break;

	case BRCMF_E_BEACON_RX:
		if (status == BRCMF_E_STATUS_SUCCESS)
			brcmf_dbg(EVENT, "MACEVENT: %s, SUCCESS\n", event_name);
		else if (status == BRCMF_E_STATUS_FAIL)
			brcmf_dbg(EVENT, "MACEVENT: %s, FAIL\n", event_name);
		else
			brcmf_dbg(EVENT, "MACEVENT: %s, status %d\n",
				  event_name, status);
		break;

	case BRCMF_E_LINK:
		brcmf_dbg(EVENT, "MACEVENT: %s %s\n",
			  event_name, link ? "UP" : "DOWN");
		break;

	case BRCMF_E_MIC_ERROR:
		brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s, Group %d, Flush %d\n",
			  event_name, eabuf, group, flush_txq);
		break;

	case BRCMF_E_ICV_ERROR:
	case BRCMF_E_UNICAST_DECODE_ERROR:
	case BRCMF_E_MULTICAST_DECODE_ERROR:
		brcmf_dbg(EVENT, "MACEVENT: %s, MAC %s\n", event_name, eabuf);
		break;

	case BRCMF_E_TXFAIL:
		brcmf_dbg(EVENT, "MACEVENT: %s, RA %s\n", event_name, eabuf);
		break;

	case BRCMF_E_SCAN_COMPLETE:
	case BRCMF_E_PMKID_CACHE:
		brcmf_dbg(EVENT, "MACEVENT: %s\n", event_name);
		break;

	case BRCMF_E_ESCAN_RESULT:
		brcmf_dbg(EVENT, "MACEVENT: %s\n", event_name);
		datalen = 0;
		break;

	case BRCMF_E_PFN_NET_FOUND:
	case BRCMF_E_PFN_NET_LOST:
	case BRCMF_E_PFN_SCAN_COMPLETE:
		brcmf_dbg(EVENT, "PNOEVENT: %s\n", event_name);
		break;

	case BRCMF_E_PSK_SUP:
	case BRCMF_E_PRUNE:
		brcmf_dbg(EVENT, "MACEVENT: %s, status %d, reason %d\n",
			  event_name, (int)status, (int)reason);
		break;

	case BRCMF_E_TRACE:
		buf = (unsigned char *) event_data;
		memcpy(&hdr, buf, sizeof(struct msgtrace_hdr));

		if (hdr.version != MSGTRACE_VERSION) {
			brcmf_dbg(ERROR,
				  "MACEVENT: %s [unsupported version --> brcmf"
				  " version:%d dongle version:%d]\n",
				  event_name, MSGTRACE_VERSION, hdr.version);
			/* Reset datalen to avoid display below */
			datalen = 0;
			break;
		}

		/* There are 2 bytes available at the end of data */
		*(buf + sizeof(struct msgtrace_hdr)
			 + be16_to_cpu(hdr.len)) = '\0';

		if (be32_to_cpu(hdr.discarded_bytes)
		    || be32_to_cpu(hdr.discarded_printf))
			brcmf_dbg(ERROR,
				  "WLC_E_TRACE: [Discarded traces in dongle -->"
				  " discarded_bytes %d discarded_printf %d]\n",
				  be32_to_cpu(hdr.discarded_bytes),
				  be32_to_cpu(hdr.discarded_printf));

		nblost = be32_to_cpu(hdr.seqnum) - seqnum_prev - 1;
		if (nblost > 0)
			brcmf_dbg(ERROR, "WLC_E_TRACE: [Event lost --> seqnum "
				  " %d nblost %d\n", be32_to_cpu(hdr.seqnum),
				  nblost);
		seqnum_prev = be32_to_cpu(hdr.seqnum);

		/* Display the trace buffer. Advance from \n to \n to
		 * avoid display big
		 * printf (issue with Linux printk )
		 */
		p = (char *)&buf[sizeof(struct msgtrace_hdr)];
		while ((s = strstr(p, "\n")) != NULL) {
			*s = '\0';
			pr_debug("%s\n", p);
			p = s + 1;
		}
		pr_debug("%s\n", p);

		/* Reset datalen to avoid display below */
		datalen = 0;
		break;

	case BRCMF_E_RSSI:
		brcmf_dbg(EVENT, "MACEVENT: %s %d\n",
			  event_name, be32_to_cpu(*((__be32 *)event_data)));
		break;

	default:
		brcmf_dbg(EVENT,
			  "MACEVENT: %s %d, MAC %s, status %d, reason %d, "
			  "auth %d\n", event_name, event_type, eabuf,
			  (int)status, (int)reason, (int)auth_type);
		break;
	}

	/* show any appended data */
	brcmf_dbg_hex_dump(datalen, event_data, datalen, "Received data");
}
#endif				/* DEBUG */

int
brcmf_c_host_event(struct brcmf_pub *drvr, int *ifidx, void *pktdata,
		   struct brcmf_event_msg *event, void **data_ptr)
{
	/* check whether packet is a BRCM event pkt */
	struct brcmf_event *pvt_data = (struct brcmf_event *) pktdata;
	struct brcmf_if_event *ifevent;
	struct brcmf_if *ifp;
	char *event_data;
	u32 type, status;
	u16 flags;
	int evlen;

	if (memcmp(BRCM_OUI, &pvt_data->hdr.oui[0], DOT11_OUI_LEN)) {
		brcmf_dbg(ERROR, "mismatched OUI, bailing\n");
		return -EBADE;
	}

	/* BRCM event pkt may be unaligned - use xxx_ua to load user_subtype. */
	if (get_unaligned_be16(&pvt_data->hdr.usr_subtype) !=
	    BCMILCP_BCM_SUBTYPE_EVENT) {
		brcmf_dbg(ERROR, "mismatched subtype, bailing\n");
		return -EBADE;
	}

	*data_ptr = &pvt_data[1];
	event_data = *data_ptr;

	/* memcpy since BRCM event pkt may be unaligned. */
	memcpy(event, &pvt_data->msg, sizeof(struct brcmf_event_msg));

	type = get_unaligned_be32(&event->event_type);
	flags = get_unaligned_be16(&event->flags);
	status = get_unaligned_be32(&event->status);
	evlen = get_unaligned_be32(&event->datalen) +
		sizeof(struct brcmf_event);

	switch (type) {
	case BRCMF_E_IF:
		ifevent = (struct brcmf_if_event *) event_data;
		brcmf_dbg(TRACE, "if event\n");

		if (ifevent->ifidx > 0 && ifevent->ifidx < BRCMF_MAX_IFS) {
			if (ifevent->action == BRCMF_E_IF_ADD) {
				ifp = brcmf_add_if(drvr->dev, ifevent->ifidx,
						   ifevent->bssidx,
						   event->ifname,
						   pvt_data->eth.h_dest);
				if (IS_ERR(ifp))
					return PTR_ERR(ifp);
				brcmf_net_attach(ifp);
			} else {
				brcmf_del_if(drvr, ifevent->ifidx);
			}
		} else {
			brcmf_dbg(ERROR, "Invalid ifidx %d for %s\n",
				  ifevent->ifidx, event->ifname);
		}

		/* send up the if event: btamp user needs it */
		*ifidx = brcmf_ifname2idx(drvr, event->ifname);
		break;

		/* These are what external supplicant/authenticator wants */
	case BRCMF_E_LINK:
	case BRCMF_E_ASSOC_IND:
	case BRCMF_E_REASSOC_IND:
	case BRCMF_E_DISASSOC_IND:
	case BRCMF_E_MIC_ERROR:
	default:
		/* Fall through: this should get _everything_  */

		*ifidx = brcmf_ifname2idx(drvr, event->ifname);
		brcmf_dbg(TRACE, "MAC event %d, flags %x, status %x\n",
			  type, flags, status);

		/* put it back to BRCMF_E_NDIS_LINK */
		if (type == BRCMF_E_NDIS_LINK) {
			u32 temp1;
			__be32 temp2;

			temp1 = get_unaligned_be32(&event->event_type);
			brcmf_dbg(TRACE, "Converted to WLC_E_LINK type %d\n",
				  temp1);

			temp2 = cpu_to_be32(BRCMF_E_NDIS_LINK);
			memcpy((void *)(&pvt_data->msg.event_type), &temp2,
			       sizeof(pvt_data->msg.event_type));
		}
		break;
	}

#ifdef DEBUG
	if (BRCMF_EVENT_ON())
		brcmf_c_show_host_event(event, event_data);
#endif /* DEBUG */

	return 0;
}

/* Convert user's input in hex pattern to byte-size mask */
static int brcmf_c_pattern_atoh(char *src, char *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 && strncmp(src, "0X", 2) != 0) {
		brcmf_dbg(ERROR, "Mask invalid format. Needs to start with 0x\n");
		return -EINVAL;
	}
	src = src + 2;		/* Skip past 0x */
	if (strlen(src) % 2 != 0) {
		brcmf_dbg(ERROR, "Mask invalid format. Length must be even.\n");
		return -EINVAL;
	}
	for (i = 0; *src != '\0'; i++) {
		unsigned long res;
		char num[3];
		strncpy(num, src, 2);
		num[2] = '\0';
		if (kstrtoul(num, 16, &res))
			return -EINVAL;
		dst[i] = (u8)res;
		src += 2;
	}
	return i;
}

static void
brcmf_c_pktfilter_offload_enable(struct brcmf_if *ifp, char *arg, int enable,
				 int master_mode)
{
	unsigned long res;
	char *argv;
	char *arg_save = NULL, *arg_org = NULL;
	s32 err;
	struct brcmf_pkt_filter_enable_le enable_parm;

	arg_save = kstrdup(arg, GFP_ATOMIC);
	if (!arg_save)
		goto fail;

	arg_org = arg_save;

	argv = strsep(&arg_save, " ");

	if (argv == NULL) {
		brcmf_dbg(ERROR, "No args provided\n");
		goto fail;
	}

	/* Parse packet filter id. */
	enable_parm.id = 0;
	if (!kstrtoul(argv, 0, &res))
		enable_parm.id = cpu_to_le32((u32)res);

	/* Enable/disable the specified filter. */
	enable_parm.enable = cpu_to_le32(enable);

	err = brcmf_fil_iovar_data_set(ifp, "pkt_filter_enable", &enable_parm,
				       sizeof(enable_parm));
	if (err)
		brcmf_dbg(ERROR, "Set pkt_filter_enable error (%d)\n", err);

	/* Control the master mode */
	err = brcmf_fil_iovar_int_set(ifp, "pkt_filter_mode", master_mode);
	if (err)
		brcmf_dbg(ERROR, "Set pkt_filter_mode error (%d)\n", err);

fail:
	kfree(arg_org);
}

static void brcmf_c_pktfilter_offload_set(struct brcmf_if *ifp, char *arg)
{
	struct brcmf_pkt_filter_le *pkt_filter;
	unsigned long res;
	int buf_len;
	s32 err;
	u32 mask_size;
	u32 pattern_size;
	char *argv[8], *buf = NULL;
	int i = 0;
	char *arg_save = NULL, *arg_org = NULL;

	arg_save = kstrdup(arg, GFP_ATOMIC);
	if (!arg_save)
		goto fail;

	arg_org = arg_save;

	buf = kmalloc(PKTFILTER_BUF_SIZE, GFP_ATOMIC);
	if (!buf)
		goto fail;

	argv[i] = strsep(&arg_save, " ");
	while (argv[i]) {
		i++;
		if (i >= 8) {
			brcmf_dbg(ERROR, "Too many parameters\n");
			goto fail;
		}
		argv[i] = strsep(&arg_save, " ");
	}

	if (i != 6) {
		brcmf_dbg(ERROR, "Not enough args provided %d\n", i);
		goto fail;
	}

	pkt_filter = (struct brcmf_pkt_filter_le *)buf;

	/* Parse packet filter id. */
	pkt_filter->id = 0;
	if (!kstrtoul(argv[0], 0, &res))
		pkt_filter->id = cpu_to_le32((u32)res);

	/* Parse filter polarity. */
	pkt_filter->negate_match = 0;
	if (!kstrtoul(argv[1], 0, &res))
		pkt_filter->negate_match = cpu_to_le32((u32)res);

	/* Parse filter type. */
	pkt_filter->type = 0;
	if (!kstrtoul(argv[2], 0, &res))
		pkt_filter->type = cpu_to_le32((u32)res);

	/* Parse pattern filter offset. */
	pkt_filter->u.pattern.offset = 0;
	if (!kstrtoul(argv[3], 0, &res))
		pkt_filter->u.pattern.offset = cpu_to_le32((u32)res);

	/* Parse pattern filter mask. */
	mask_size = brcmf_c_pattern_atoh(argv[4],
			(char *)pkt_filter->u.pattern.mask_and_pattern);

	/* Parse pattern filter pattern. */
	pattern_size = brcmf_c_pattern_atoh(argv[5],
		(char *)&pkt_filter->u.pattern.mask_and_pattern[mask_size]);

	if (mask_size != pattern_size) {
		brcmf_dbg(ERROR, "Mask and pattern not the same size\n");
		goto fail;
	}

	pkt_filter->u.pattern.size_bytes = cpu_to_le32(mask_size);
	buf_len = offsetof(struct brcmf_pkt_filter_le,
			   u.pattern.mask_and_pattern);
	buf_len += mask_size + pattern_size;

	err = brcmf_fil_iovar_data_set(ifp, "pkt_filter_add", pkt_filter,
				       buf_len);
	if (err)
		brcmf_dbg(ERROR, "Set pkt_filter_add error (%d)\n", err);

fail:
	kfree(arg_org);

	kfree(buf);
}

int brcmf_c_preinit_dcmds(struct brcmf_if *ifp)
{
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];
	u8 buf[BRCMF_DCMD_SMLEN];
	char *ptr;
	s32 err;
	struct brcmf_bus_dcmd *cmdlst;
	struct list_head *cur, *q;

	/* retreive mac address */
	err = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", ifp->mac_addr,
				       sizeof(ifp->mac_addr));
	if (err < 0) {
		brcmf_dbg(ERROR, "Retreiving cur_etheraddr failed, %d\n",
			  err);
		goto done;
	}
	memcpy(ifp->drvr->mac, ifp->mac_addr, sizeof(ifp->drvr->mac));

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	strcpy(buf, "ver");
	err = brcmf_fil_iovar_data_get(ifp, "ver", buf, sizeof(buf));
	if (err < 0) {
		brcmf_dbg(ERROR, "Retreiving version information failed, %d\n",
			  err);
		goto done;
	}
	ptr = (char *)buf;
	strsep(&ptr, "\n");
	/* Print fw version info */
	brcmf_dbg(ERROR, "Firmware version = %s\n", buf);

	/*
	 * Setup timeout if Beacons are lost and roam is off to report
	 * link down
	 */
	err = brcmf_fil_iovar_int_set(ifp, "bcn_timeout",
				      BRCMF_DEFAULT_BCN_TIMEOUT);
	if (err) {
		brcmf_dbg(ERROR, "bcn_timeout error (%d)\n", err);
		goto done;
	}

	/* Enable/Disable build-in roaming to allowed ext supplicant to take
	 * of romaing
	 */
	err = brcmf_fil_iovar_int_set(ifp, "roam_off", 1);
	if (err) {
		brcmf_dbg(ERROR, "roam_off error (%d)\n", err);
		goto done;
	}

	/* Setup event_msgs, enable E_IF */
	err = brcmf_fil_iovar_data_get(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		brcmf_dbg(ERROR, "Get event_msgs error (%d)\n", err);
		goto done;
	}
	setbit(eventmask, BRCMF_E_IF);
	err = brcmf_fil_iovar_data_set(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		brcmf_dbg(ERROR, "Set event_msgs error (%d)\n", err);
		goto done;
	}

	/* Setup default scan channel time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME,
				    BRCMF_DEFAULT_SCAN_CHANNEL_TIME);
	if (err) {
		brcmf_dbg(ERROR, "BRCMF_C_SET_SCAN_CHANNEL_TIME error (%d)\n",
			  err);
		goto done;
	}

	/* Setup default scan unassoc time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME,
				    BRCMF_DEFAULT_SCAN_UNASSOC_TIME);
	if (err) {
		brcmf_dbg(ERROR, "BRCMF_C_SET_SCAN_UNASSOC_TIME error (%d)\n",
			  err);
		goto done;
	}

	/* Try to set and enable ARP offload feature, this may fail */
	err = brcmf_fil_iovar_int_set(ifp, "arp_ol", BRCMF_ARPOL_MODE);
	if (err) {
		brcmf_dbg(TRACE, "failed to set ARP offload mode to 0x%x, err = %d\n",
			  BRCMF_ARPOL_MODE, err);
		err = 0;
	} else {
		err = brcmf_fil_iovar_int_set(ifp, "arpoe", 1);
		if (err) {
			brcmf_dbg(TRACE, "failed to enable ARP offload err = %d\n",
				  err);
			err = 0;
		} else
			brcmf_dbg(TRACE, "successfully enabled ARP offload to 0x%x\n",
				  BRCMF_ARPOL_MODE);
	}

	/* Setup packet filter */
	brcmf_c_pktfilter_offload_set(ifp, BRCMF_DEFAULT_PACKET_FILTER);
	brcmf_c_pktfilter_offload_enable(ifp, BRCMF_DEFAULT_PACKET_FILTER,
					 0, true);

	/* set bus specific command if there is any */
	list_for_each_safe(cur, q, &ifp->drvr->bus_if->dcmd_list) {
		cmdlst = list_entry(cur, struct brcmf_bus_dcmd, list);
		if (cmdlst->name && cmdlst->param && cmdlst->param_len) {
			brcmf_fil_iovar_data_set(ifp, cmdlst->name,
						 cmdlst->param,
						 cmdlst->param_len);
		}
		list_del(cur);
		kfree(cmdlst);
	}
done:
	return err;
}
