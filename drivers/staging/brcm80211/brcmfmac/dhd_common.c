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
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <asm/unaligned.h>
#include <defs.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "dngl_stats.h"
#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_proto.h"
#include "dhd_dbg.h"

#define BRCM_OUI			"\x00\x10\x18"
#define DOT11_OUI_LEN			3
#define BCMILCP_BCM_SUBTYPE_EVENT	1
#define PKTFILTER_BUF_SIZE		2048

int brcmf_msg_level;
char brcmf_fw_path[MOD_PARAM_PATHLEN];
char brcmf_nv_path[MOD_PARAM_PATHLEN];

#define MSGTRACE_VERSION	1

#ifdef BCMDBG
const char brcmf_version[] =
"Dongle Host Driver, version " BRCMF_VERSION_STR "\nCompiled on " __DATE__
" at " __TIME__;
#else
const char brcmf_version[] = "Dongle Host Driver, version " BRCMF_VERSION_STR;
#endif

/* IOVar table */
enum {
	IOV_VERSION = 1,
	IOV_MSGLEVEL,
	IOV_BCMERRORSTR,
	IOV_BCMERROR,
	IOV_DUMP,
#ifdef BCMDBG
	IOV_CONS,
	IOV_DCONSOLE_POLL,
#endif
	IOV_CLEARCOUNTS,
	IOV_LOGDUMP,
	IOV_LOGCAL,
	IOV_LOGSTAMP,
	IOV_GPIOOB,
	IOV_IOCTLTIMEOUT,
	IOV_LAST
};

const struct brcmu_iovar brcmf_iovars[] = {
	{"version", IOV_VERSION, 0, IOVT_BUFFER, sizeof(brcmf_version)}
	,
#ifdef BCMDBG
	{"msglevel", IOV_MSGLEVEL, 0, IOVT_UINT32, 0}
	,
#endif				/* BCMDBG */
	{"bcmerrorstr", IOV_BCMERRORSTR, 0, IOVT_BUFFER, BCME_STRLEN}
	,
	{"bcmerror", IOV_BCMERROR, 0, IOVT_INT8, 0}
	,
	{"dump", IOV_DUMP, 0, IOVT_BUFFER, DHD_IOCTL_MAXLEN}
	,
#ifdef BCMDBG
	{"dconpoll", IOV_DCONSOLE_POLL, 0, IOVT_UINT32, 0}
	,
	{"cons", IOV_CONS, 0, IOVT_BUFFER, 0}
	,
#endif
	{"clearcounts", IOV_CLEARCOUNTS, 0, IOVT_VOID, 0}
	,
	{"gpioob", IOV_GPIOOB, 0, IOVT_UINT32, 0}
	,
	{"ioctl_timeout", IOV_IOCTLTIMEOUT, 0, IOVT_UINT32, 0}
	,
	{NULL, 0, 0, 0, 0}
};

/* Message trace header */
struct msgtrace_hdr {
	u8 version;
	u8 spare;
	u16 len;		/* Len of the trace */
	u32 seqnum;		/* Sequence number of message. Useful
				 * if the messsage has been lost
				 * because of DMA error or a bus reset
				 * (ex: SDIO Func2)
				 */
	u32 discarded_bytes;	/* Number of discarded bytes because of
				 trace overflow  */
	u32 discarded_printf;	/* Number of discarded printf
				 because of trace overflow */
} __packed;

void brcmf_c_init(void)
{
	/* Init global variables at run-time, not as part of the declaration.
	 * This is required to support init/de-init of the driver.
	 * Initialization
	 * of globals as part of the declaration results in non-deterministic
	 * behaviour since the value of the globals may be different on the
	 * first time that the driver is initialized vs subsequent
	 * initializations.
	 */
	brcmf_msg_level = DHD_ERROR_VAL;
#ifdef CONFIG_BCM4329_FW_PATH
	strncpy(brcmf_fw_path, CONFIG_BCM4329_FW_PATH, MOD_PARAM_PATHLEN - 1);
#else
	brcmf_fw_path[0] = '\0';
#endif
#ifdef CONFIG_BCM4329_NVRAM_PATH
	strncpy(brcmf_nv_path,
		CONFIG_BCM4329_NVRAM_PATH, MOD_PARAM_PATHLEN - 1);
#else
	brcmf_nv_path[0] = '\0';
#endif
}

static int brcmf_c_dump(dhd_pub_t *dhdp, char *buf, int buflen)
{
	struct brcmu_strbuf b;
	struct brcmu_strbuf *strbuf = &b;

	brcmu_binit(strbuf, buf, buflen);

	/* Base DHD info */
	brcmu_bprintf(strbuf, "%s\n", brcmf_version);
	brcmu_bprintf(strbuf, "\n");
	brcmu_bprintf(strbuf, "pub.up %d pub.txoff %d pub.busstate %d\n",
		    dhdp->up, dhdp->txoff, dhdp->busstate);
	brcmu_bprintf(strbuf, "pub.hdrlen %d pub.maxctl %d pub.rxsz %d\n",
		    dhdp->hdrlen, dhdp->maxctl, dhdp->rxsz);
	brcmu_bprintf(strbuf, "pub.iswl %d pub.drv_version %ld pub.mac %pM\n",
		    dhdp->iswl, dhdp->drv_version, &dhdp->mac);
	brcmu_bprintf(strbuf, "pub.bcmerror %d tickcnt %d\n", dhdp->bcmerror,
		    dhdp->tickcnt);

	brcmu_bprintf(strbuf, "dongle stats:\n");
	brcmu_bprintf(strbuf,
		    "tx_packets %ld tx_bytes %ld tx_errors %ld tx_dropped %ld\n",
		    dhdp->dstats.tx_packets, dhdp->dstats.tx_bytes,
		    dhdp->dstats.tx_errors, dhdp->dstats.tx_dropped);
	brcmu_bprintf(strbuf,
		    "rx_packets %ld rx_bytes %ld rx_errors %ld rx_dropped %ld\n",
		    dhdp->dstats.rx_packets, dhdp->dstats.rx_bytes,
		    dhdp->dstats.rx_errors, dhdp->dstats.rx_dropped);
	brcmu_bprintf(strbuf, "multicast %ld\n", dhdp->dstats.multicast);

	brcmu_bprintf(strbuf, "bus stats:\n");
	brcmu_bprintf(strbuf, "tx_packets %ld tx_multicast %ld tx_errors %ld\n",
		    dhdp->tx_packets, dhdp->tx_multicast, dhdp->tx_errors);
	brcmu_bprintf(strbuf, "tx_ctlpkts %ld tx_ctlerrs %ld\n",
		    dhdp->tx_ctlpkts, dhdp->tx_ctlerrs);
	brcmu_bprintf(strbuf, "rx_packets %ld rx_multicast %ld rx_errors %ld\n",
		    dhdp->rx_packets, dhdp->rx_multicast, dhdp->rx_errors);
	brcmu_bprintf(strbuf,
		    "rx_ctlpkts %ld rx_ctlerrs %ld rx_dropped %ld rx_flushed %ld\n",
		    dhdp->rx_ctlpkts, dhdp->rx_ctlerrs, dhdp->rx_dropped,
		    dhdp->rx_flushed);
	brcmu_bprintf(strbuf,
		    "rx_readahead_cnt %ld tx_realloc %ld fc_packets %ld\n",
		    dhdp->rx_readahead_cnt, dhdp->tx_realloc, dhdp->fc_packets);
	brcmu_bprintf(strbuf, "wd_dpc_sched %ld\n", dhdp->wd_dpc_sched);
	brcmu_bprintf(strbuf, "\n");

	/* Add any prot info */
	brcmf_proto_dump(dhdp, strbuf);
	brcmu_bprintf(strbuf, "\n");

	/* Add any bus info */
	brcmf_sdbrcm_bus_dump(dhdp, strbuf);

	return !strbuf->size ? -EOVERFLOW : 0;
}

static int
brcmf_c_doiovar(dhd_pub_t *dhd_pub, const struct brcmu_iovar *vi, u32 actionid,
	    const char *name, void *params, int plen, void *arg, int len,
	    int val_size)
{
	int bcmerror = 0;
	s32 int_val = 0;

	DHD_TRACE(("%s: Enter\n", __func__));

	bcmerror = brcmu_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid));
	if (bcmerror != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		memcpy(&int_val, params, sizeof(int_val));

	switch (actionid) {
	case IOV_GVAL(IOV_VERSION):
		/* Need to have checked buffer length */
		strncpy((char *)arg, brcmf_version, len);
		break;

	case IOV_GVAL(IOV_MSGLEVEL):
		int_val = (s32) brcmf_msg_level;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_MSGLEVEL):
		brcmf_msg_level = int_val;
		break;

	case IOV_GVAL(IOV_BCMERRORSTR):
		strncpy((char *)arg, "bcm_error",
			BCME_STRLEN);
		((char *)arg)[BCME_STRLEN - 1] = 0x00;
		break;

	case IOV_GVAL(IOV_BCMERROR):
		int_val = (s32) dhd_pub->bcmerror;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_GVAL(IOV_DUMP):
		bcmerror = brcmf_c_dump(dhd_pub, arg, len);
		break;

#ifdef BCMDBG
	case IOV_GVAL(IOV_DCONSOLE_POLL):
		int_val = (s32) brcmf_console_ms;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_DCONSOLE_POLL):
		brcmf_console_ms = (uint) int_val;
		break;

	case IOV_SVAL(IOV_CONS):
		if (len > 0)
			bcmerror = brcmf_sdbrcm_bus_console_in(dhd_pub, arg,
							       len - 1);
		break;
#endif

	case IOV_SVAL(IOV_CLEARCOUNTS):
		dhd_pub->tx_packets = dhd_pub->rx_packets = 0;
		dhd_pub->tx_errors = dhd_pub->rx_errors = 0;
		dhd_pub->tx_ctlpkts = dhd_pub->rx_ctlpkts = 0;
		dhd_pub->tx_ctlerrs = dhd_pub->rx_ctlerrs = 0;
		dhd_pub->rx_dropped = 0;
		dhd_pub->rx_readahead_cnt = 0;
		dhd_pub->tx_realloc = 0;
		dhd_pub->wd_dpc_sched = 0;
		memset(&dhd_pub->dstats, 0, sizeof(dhd_pub->dstats));
		dhd_bus_clearcounts(dhd_pub);
		break;

	case IOV_GVAL(IOV_IOCTLTIMEOUT):{
			int_val = (s32) brcmf_os_get_ioctl_resp_timeout();
			memcpy(arg, &int_val, sizeof(int_val));
			break;
		}

	case IOV_SVAL(IOV_IOCTLTIMEOUT):{
			if (int_val <= 0)
				bcmerror = -EINVAL;
			else
				brcmf_os_set_ioctl_resp_timeout((unsigned int)
							      int_val);
			break;
		}

	default:
		bcmerror = -ENOTSUPP;
		break;
	}

exit:
	return bcmerror;
}

bool brcmf_c_prec_enq(dhd_pub_t *dhdp, struct pktq *q, struct sk_buff *pkt,
		  int prec)
{
	struct sk_buff *p;
	int eprec = -1;		/* precedence to evict from */
	bool discard_oldest;

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
		ASSERT(p);
		if (eprec > prec)
			return false;
	}

	/* Evict if needed */
	if (eprec >= 0) {
		/* Detect queueing to unconfigured precedence */
		ASSERT(!pktq_pempty(q, eprec));
		discard_oldest = AC_BITMAP_TST(dhdp->wme_dp, eprec);
		if (eprec == prec && !discard_oldest)
			return false;	/* refuse newer (incoming) packet */
		/* Evict packet according to discard policy */
		p = discard_oldest ? brcmu_pktq_pdeq(q, eprec) :
			brcmu_pktq_pdeq_tail(q, eprec);
		if (p == NULL) {
			DHD_ERROR(("%s: brcmu_pktq_penq() failed, oldest %d.",
				   __func__, discard_oldest));
			ASSERT(p);
		}

		brcmu_pkt_buf_free_skb(p);
	}

	/* Enqueue */
	p = brcmu_pktq_penq(q, prec, pkt);
	if (p == NULL) {
		DHD_ERROR(("%s: brcmu_pktq_penq() failed.", __func__));
		ASSERT(p);
	}

	return true;
}

static int
brcmf_c_iovar_op(dhd_pub_t *dhd_pub, const char *name,
	     void *params, int plen, void *arg, int len, bool set)
{
	int bcmerror = 0;
	int val_size;
	const struct brcmu_iovar *vi = NULL;
	u32 actionid;

	DHD_TRACE(("%s: Enter\n", __func__));

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get MUST have return space */
	ASSERT(set || (arg && len));

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !plen));

	vi = brcmu_iovar_lookup(brcmf_iovars, name);
	if (vi == NULL) {
		bcmerror = -ENOTSUPP;
		goto exit;
	}

	DHD_CTL(("%s: %s %s, len %d plen %d\n", __func__,
		 name, (set ? "set" : "get"), len, plen));

	/* set up 'params' pointer in case this is a set command so that
	 * the convenience int and bool code can be common to set and get
	 */
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		/* all other types are integer sized */
		val_size = sizeof(int);

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);
	bcmerror =
	    brcmf_c_doiovar(dhd_pub, vi, actionid, name, params, plen, arg, len,
			val_size);

exit:
	return bcmerror;
}

int brcmf_c_ioctl(dhd_pub_t *dhd_pub, dhd_ioctl_t *ioc, void *buf, uint buflen)
{
	int bcmerror = 0;

	DHD_TRACE(("%s: Enter\n", __func__));

	if (!buf)
		return -EINVAL;

	switch (ioc->cmd) {
	case DHD_GET_MAGIC:
		if (buflen < sizeof(int))
			bcmerror = -EOVERFLOW;
		else
			*(int *)buf = DHD_IOCTL_MAGIC;
		break;

	case DHD_GET_VERSION:
		if (buflen < sizeof(int))
			bcmerror = -EOVERFLOW;
		else
			*(int *)buf = DHD_IOCTL_VERSION;
		break;

	case DHD_GET_VAR:
	case DHD_SET_VAR:{
			char *arg;
			uint arglen;

			/* scan past the name to any arguments */
			for (arg = buf, arglen = buflen; *arg && arglen;
			     arg++, arglen--)
				;

			if (*arg) {
				bcmerror = -EOVERFLOW;
				break;
			}

			/* account for the NUL terminator */
			arg++, arglen--;

			/* call with the appropriate arguments */
			if (ioc->cmd == DHD_GET_VAR)
				bcmerror = brcmf_c_iovar_op(dhd_pub, buf, arg,
						arglen, buf, buflen, IOV_GET);
			else
				bcmerror =
				    brcmf_c_iovar_op(dhd_pub, buf, NULL, 0, arg,
						     arglen, IOV_SET);
			if (bcmerror != -ENOTSUPP)
				break;

			/* not in generic table, try protocol module */
			if (ioc->cmd == DHD_GET_VAR)
				bcmerror = brcmf_proto_iovar_op(dhd_pub, buf,
								arg, arglen,
								buf, buflen,
								IOV_GET);
			else
				bcmerror = brcmf_proto_iovar_op(dhd_pub, buf,
								NULL, 0, arg,
								arglen,
								IOV_SET);
			if (bcmerror != -ENOTSUPP)
				break;

			/* if still not found, try bus module */
			if (ioc->cmd == DHD_GET_VAR)
				bcmerror = brcmf_sdbrcm_bus_iovar_op(dhd_pub,
						buf, arg, arglen, buf, buflen,
						IOV_GET);
			else
				bcmerror = brcmf_sdbrcm_bus_iovar_op(dhd_pub,
						buf, NULL, 0, arg, arglen,
						IOV_SET);

			break;
		}

	default:
		bcmerror = -ENOTSUPP;
	}

	return bcmerror;
}

#ifdef SHOW_EVENTS
static void brcmf_c_show_host_event(brcmf_event_msg_t *event, void *event_data)
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
		BRCMF_E_PFN_SCAN_COMPLETE, "SCAN_COMPLETE"}
	};
	uint event_type, flags, auth_type, datalen;
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

	DHD_EVENT(("EVENT: %s, event ID = %d\n", event_name, event_type));
	DHD_EVENT(("flags 0x%04x, status %d, reason %d, auth_type %d MAC %s\n",
				flags, status, reason, auth_type, eabuf));

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
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

	case BRCMF_E_ASSOC_IND:
	case BRCMF_E_REASSOC_IND:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

	case BRCMF_E_ASSOC:
	case BRCMF_E_REASSOC:
		if (status == BRCMF_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, SUCCESS\n",
				   event_name, eabuf));
		} else if (status == BRCMF_E_STATUS_TIMEOUT) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, TIMEOUT\n",
				   event_name, eabuf));
		} else if (status == BRCMF_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, FAILURE, reason %d\n",
				   event_name, eabuf, (int)reason));
		} else {
			DHD_EVENT(("MACEVENT: %s, MAC %s, unexpected status "
				"%d\n", event_name, eabuf, (int)status));
		}
		break;

	case BRCMF_E_DEAUTH_IND:
	case BRCMF_E_DISASSOC_IND:
		DHD_EVENT(("MACEVENT: %s, MAC %s, reason %d\n", event_name,
			   eabuf, (int)reason));
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
		if (event_type == BRCMF_E_AUTH_IND) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s\n", event_name,
				   eabuf, auth_str));
		} else if (status == BRCMF_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, SUCCESS\n",
				   event_name, eabuf, auth_str));
		} else if (status == BRCMF_E_STATUS_TIMEOUT) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, TIMEOUT\n",
				   event_name, eabuf, auth_str));
		} else if (status == BRCMF_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, FAILURE, "
				"reason %d\n",
				event_name, eabuf, auth_str, (int)reason));
		}

		break;

	case BRCMF_E_JOIN:
	case BRCMF_E_ROAM:
	case BRCMF_E_SET_SSID:
		if (status == BRCMF_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name,
				   eabuf));
		} else if (status == BRCMF_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, failed\n", event_name));
		} else if (status == BRCMF_E_STATUS_NO_NETWORKS) {
			DHD_EVENT(("MACEVENT: %s, no networks found\n",
				   event_name));
		} else {
			DHD_EVENT(("MACEVENT: %s, unexpected status %d\n",
				   event_name, (int)status));
		}
		break;

	case BRCMF_E_BEACON_RX:
		if (status == BRCMF_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, SUCCESS\n", event_name));
		} else if (status == BRCMF_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, FAIL\n", event_name));
		} else {
			DHD_EVENT(("MACEVENT: %s, status %d\n", event_name,
				   status));
		}
		break;

	case BRCMF_E_LINK:
		DHD_EVENT(("MACEVENT: %s %s\n", event_name,
			   link ? "UP" : "DOWN"));
		break;

	case BRCMF_E_MIC_ERROR:
		DHD_EVENT(("MACEVENT: %s, MAC %s, Group %d, Flush %d\n",
			   event_name, eabuf, group, flush_txq));
		break;

	case BRCMF_E_ICV_ERROR:
	case BRCMF_E_UNICAST_DECODE_ERROR:
	case BRCMF_E_MULTICAST_DECODE_ERROR:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

	case BRCMF_E_TXFAIL:
		DHD_EVENT(("MACEVENT: %s, RA %s\n", event_name, eabuf));
		break;

	case BRCMF_E_SCAN_COMPLETE:
	case BRCMF_E_PMKID_CACHE:
		DHD_EVENT(("MACEVENT: %s\n", event_name));
		break;

	case BRCMF_E_PFN_NET_FOUND:
	case BRCMF_E_PFN_NET_LOST:
	case BRCMF_E_PFN_SCAN_COMPLETE:
		DHD_EVENT(("PNOEVENT: %s\n", event_name));
		break;

	case BRCMF_E_PSK_SUP:
	case BRCMF_E_PRUNE:
		DHD_EVENT(("MACEVENT: %s, status %d, reason %d\n",
			   event_name, (int)status, (int)reason));
		break;

	case BRCMF_E_TRACE:
		{
			static u32 seqnum_prev;
			struct msgtrace_hdr hdr;
			u32 nblost;
			char *s, *p;

			buf = (unsigned char *) event_data;
			memcpy(&hdr, buf, sizeof(struct msgtrace_hdr));

			if (hdr.version != MSGTRACE_VERSION) {
				DHD_ERROR(
				    ("\nMACEVENT: %s [unsupported version --> "
				     "dhd version:%d dongle version:%d]\n",
				     event_name, MSGTRACE_VERSION, hdr.version)
				);
				/* Reset datalen to avoid display below */
				datalen = 0;
				break;
			}

			/* There are 2 bytes available at the end of data */
			*(buf + sizeof(struct msgtrace_hdr)
				 + be16_to_cpu(hdr.len)) = '\0';

			if (be32_to_cpu(hdr.discarded_bytes)
			    || be32_to_cpu(hdr.discarded_printf)) {
				DHD_ERROR(
				    ("\nWLC_E_TRACE: [Discarded traces in dongle -->"
				     "discarded_bytes %d discarded_printf %d]\n",
				     be32_to_cpu(hdr.discarded_bytes),
				     be32_to_cpu(hdr.discarded_printf)));
			}

			nblost = be32_to_cpu(hdr.seqnum) - seqnum_prev - 1;
			if (nblost > 0) {
				DHD_ERROR(
				    ("\nWLC_E_TRACE: [Event lost --> seqnum %d nblost %d\n",
				     be32_to_cpu(hdr.seqnum), nblost));
			}
			seqnum_prev = be32_to_cpu(hdr.seqnum);

			/* Display the trace buffer. Advance from \n to \n to
			 * avoid display big
			 * printf (issue with Linux printk )
			 */
			p = (char *)&buf[sizeof(struct msgtrace_hdr)];
			while ((s = strstr(p, "\n")) != NULL) {
				*s = '\0';
				printk(KERN_DEBUG"%s\n", p);
				p = s + 1;
			}
			printk(KERN_DEBUG "%s\n", p);

			/* Reset datalen to avoid display below */
			datalen = 0;
		}
		break;

	case BRCMF_E_RSSI:
		DHD_EVENT(("MACEVENT: %s %d\n", event_name,
			   be32_to_cpu(*((int *)event_data))));
		break;

	default:
		DHD_EVENT(("MACEVENT: %s %d, MAC %s, status %d, reason %d, "
			"auth %d\n", event_name, event_type, eabuf,
			(int)status, (int)reason, (int)auth_type));
		break;
	}

	/* show any appended data */
	if (datalen) {
		buf = (unsigned char *) event_data;
		DHD_EVENT((" data (%d) : ", datalen));
		for (i = 0; i < datalen; i++)
			DHD_EVENT((" 0x%02x ", *buf++));
		DHD_EVENT(("\n"));
	}
}
#endif				/* SHOW_EVENTS */

int
brcmf_c_host_event(struct dhd_info *dhd, int *ifidx, void *pktdata,
	      brcmf_event_msg_t *event, void **data_ptr)
{
	/* check whether packet is a BRCM event pkt */
	brcmf_event_t *pvt_data = (brcmf_event_t *) pktdata;
	char *event_data;
	u32 type, status;
	u16 flags;
	int evlen;

	if (memcmp(BRCM_OUI, &pvt_data->hdr.oui[0], DOT11_OUI_LEN)) {
		DHD_ERROR(("%s: mismatched OUI, bailing\n", __func__));
		return -EBADE;
	}

	/* BRCM event pkt may be unaligned - use xxx_ua to load user_subtype. */
	if (get_unaligned_be16(&pvt_data->hdr.usr_subtype) !=
	    BCMILCP_BCM_SUBTYPE_EVENT) {
		DHD_ERROR(("%s: mismatched subtype, bailing\n", __func__));
		return -EBADE;
	}

	*data_ptr = &pvt_data[1];
	event_data = *data_ptr;

	/* memcpy since BRCM event pkt may be unaligned. */
	memcpy(event, &pvt_data->msg, sizeof(brcmf_event_msg_t));

	type = get_unaligned_be32(&event->event_type);
	flags = get_unaligned_be16(&event->flags);
	status = get_unaligned_be32(&event->status);
	evlen = get_unaligned_be32(&event->datalen) + sizeof(brcmf_event_t);

	switch (type) {
	case BRCMF_E_IF:
		{
			dhd_if_event_t *ifevent = (dhd_if_event_t *) event_data;
			DHD_TRACE(("%s: if event\n", __func__));

			if (ifevent->ifidx > 0 &&
				 ifevent->ifidx < DHD_MAX_IFS) {
				if (ifevent->action == BRCMF_E_IF_ADD)
					brcmf_add_if(dhd, ifevent->ifidx,
						   NULL, event->ifname,
						   pvt_data->eth.h_dest,
						   ifevent->flags,
						   ifevent->bssidx);
				else
					brcmf_del_if(dhd, ifevent->ifidx);
			} else {
				DHD_ERROR(("%s: Invalid ifidx %d for %s\n",
					   __func__, ifevent->ifidx,
					   event->ifname));
			}
		}
		/* send up the if event: btamp user needs it */
		*ifidx = brcmf_ifname2idx(dhd, event->ifname);
		/* push up to external supp/auth */
		brcmf_event(dhd, (char *)pvt_data, evlen, *ifidx);
		break;

		/* These are what external supplicant/authenticator wants */
	case BRCMF_E_LINK:
	case BRCMF_E_ASSOC_IND:
	case BRCMF_E_REASSOC_IND:
	case BRCMF_E_DISASSOC_IND:
	case BRCMF_E_MIC_ERROR:
	default:
		/* Fall through: this should get _everything_  */

		*ifidx = brcmf_ifname2idx(dhd, event->ifname);
		/* push up to external supp/auth */
		brcmf_event(dhd, (char *)pvt_data, evlen, *ifidx);
		DHD_TRACE(("%s: MAC event %d, flags %x, status %x\n",
			   __func__, type, flags, status));

		/* put it back to BRCMF_E_NDIS_LINK */
		if (type == BRCMF_E_NDIS_LINK) {
			u32 temp;

			temp = get_unaligned_be32(&event->event_type);
			DHD_TRACE(("Converted to WLC_E_LINK type %d\n", temp));

			temp = be32_to_cpu(BRCMF_E_NDIS_LINK);
			memcpy((void *)(&pvt_data->msg.event_type), &temp,
			       sizeof(pvt_data->msg.event_type));
		}
		break;
	}

#ifdef SHOW_EVENTS
	brcmf_c_show_host_event(event, event_data);
#endif				/* SHOW_EVENTS */

	return 0;
}

/* Convert user's input in hex pattern to byte-size mask */
static int brcmf_c_pattern_atoh(char *src, char *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 && strncmp(src, "0X", 2) != 0) {
		DHD_ERROR(("Mask invalid format. Needs to start with 0x\n"));
		return -1;
	}
	src = src + 2;		/* Skip past 0x */
	if (strlen(src) % 2 != 0) {
		DHD_ERROR(("Mask invalid format. Length must be even.\n"));
		return -1;
	}
	for (i = 0; *src != '\0'; i++) {
		char num[3];
		strncpy(num, src, 2);
		num[2] = '\0';
		dst[i] = (u8) simple_strtoul(num, NULL, 16);
		src += 2;
	}
	return i;
}

void
brcmf_c_pktfilter_offload_enable(dhd_pub_t *dhd, char *arg, int enable,
			     int master_mode)
{
	char *argv[8];
	int i = 0;
	const char *str;
	int buf_len;
	int str_len;
	char *arg_save = 0, *arg_org = 0;
	int rc;
	char buf[128];
	wl_pkt_filter_enable_t enable_parm;
	wl_pkt_filter_enable_t *pkt_filterp;

	arg_save = kmalloc(strlen(arg) + 1, GFP_ATOMIC);
	if (!arg_save) {
		DHD_ERROR(("%s: kmalloc failed\n", __func__));
		goto fail;
	}
	arg_org = arg_save;
	memcpy(arg_save, arg, strlen(arg) + 1);

	argv[i] = strsep(&arg_save, " ");

	i = 0;
	if (NULL == argv[i]) {
		DHD_ERROR(("No args provided\n"));
		goto fail;
	}

	str = "pkt_filter_enable";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[str_len] = '\0';
	buf_len = str_len + 1;

	pkt_filterp = (wl_pkt_filter_enable_t *) (buf + str_len + 1);

	/* Parse packet filter id. */
	enable_parm.id = simple_strtoul(argv[i], NULL, 0);

	/* Parse enable/disable value. */
	enable_parm.enable = enable;

	buf_len += sizeof(enable_parm);
	memcpy((char *)pkt_filterp, &enable_parm, sizeof(enable_parm));

	/* Enable/disable the specified filter. */
	rc = brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR, buf, buf_len);
	rc = rc >= 0 ? 0 : rc;
	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
			   __func__, arg, rc));
	else
		DHD_TRACE(("%s: successfully added pktfilter %s\n",
			   __func__, arg));

	/* Contorl the master mode */
	brcmu_mkiovar("pkt_filter_mode", (char *)&master_mode, 4, buf,
		    sizeof(buf));
	rc = brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR, buf,
				       sizeof(buf));
	rc = rc >= 0 ? 0 : rc;
	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
			   __func__, arg, rc));

fail:
	kfree(arg_org);
}

void brcmf_c_pktfilter_offload_set(dhd_pub_t *dhd, char *arg)
{
	const char *str;
	wl_pkt_filter_t pkt_filter;
	wl_pkt_filter_t *pkt_filterp;
	int buf_len;
	int str_len;
	int rc;
	u32 mask_size;
	u32 pattern_size;
	char *argv[8], *buf = 0;
	int i = 0;
	char *arg_save = 0, *arg_org = 0;

	arg_save = kmalloc(strlen(arg) + 1, GFP_ATOMIC);
	if (!arg_save) {
		DHD_ERROR(("%s: kmalloc failed\n", __func__));
		goto fail;
	}

	arg_org = arg_save;

	buf = kmalloc(PKTFILTER_BUF_SIZE, GFP_ATOMIC);
	if (!buf) {
		DHD_ERROR(("%s: kmalloc failed\n", __func__));
		goto fail;
	}

	memcpy(arg_save, arg, strlen(arg) + 1);

	if (strlen(arg) > PKTFILTER_BUF_SIZE) {
		DHD_ERROR(("Not enough buffer %d < %d\n", (int)strlen(arg),
			   (int)sizeof(buf)));
		goto fail;
	}

	argv[i] = strsep(&arg_save, " ");
	while (argv[i++])
		argv[i] = strsep(&arg_save, " ");

	i = 0;
	if (NULL == argv[i]) {
		DHD_ERROR(("No args provided\n"));
		goto fail;
	}

	str = "pkt_filter_add";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[str_len] = '\0';
	buf_len = str_len + 1;

	pkt_filterp = (wl_pkt_filter_t *) (buf + str_len + 1);

	/* Parse packet filter id. */
	pkt_filter.id = simple_strtoul(argv[i], NULL, 0);

	if (NULL == argv[++i]) {
		DHD_ERROR(("Polarity not provided\n"));
		goto fail;
	}

	/* Parse filter polarity. */
	pkt_filter.negate_match = simple_strtoul(argv[i], NULL, 0);

	if (NULL == argv[++i]) {
		DHD_ERROR(("Filter type not provided\n"));
		goto fail;
	}

	/* Parse filter type. */
	pkt_filter.type = simple_strtoul(argv[i], NULL, 0);

	if (NULL == argv[++i]) {
		DHD_ERROR(("Offset not provided\n"));
		goto fail;
	}

	/* Parse pattern filter offset. */
	pkt_filter.u.pattern.offset = simple_strtoul(argv[i], NULL, 0);

	if (NULL == argv[++i]) {
		DHD_ERROR(("Bitmask not provided\n"));
		goto fail;
	}

	/* Parse pattern filter mask. */
	mask_size =
	    brcmf_c_pattern_atoh
		   (argv[i], (char *)pkt_filterp->u.pattern.mask_and_pattern);

	if (NULL == argv[++i]) {
		DHD_ERROR(("Pattern not provided\n"));
		goto fail;
	}

	/* Parse pattern filter pattern. */
	pattern_size =
	    brcmf_c_pattern_atoh(argv[i],
				   (char *)&pkt_filterp->u.pattern.
				   mask_and_pattern[mask_size]);

	if (mask_size != pattern_size) {
		DHD_ERROR(("Mask and pattern not the same size\n"));
		goto fail;
	}

	pkt_filter.u.pattern.size_bytes = mask_size;
	buf_len += WL_PKT_FILTER_FIXED_LEN;
	buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + 2 * mask_size);

	/* Keep-alive attributes are set in local
	 * variable (keep_alive_pkt), and
	 ** then memcpy'ed into buffer (keep_alive_pktp) since there is no
	 ** guarantee that the buffer is properly aligned.
	 */
	memcpy((char *)pkt_filterp,
	       &pkt_filter,
	       WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_FIXED_LEN);

	rc = brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR, buf, buf_len);
	rc = rc >= 0 ? 0 : rc;

	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
			   __func__, arg, rc));
	else
		DHD_TRACE(("%s: successfully added pktfilter %s\n",
			   __func__, arg));

fail:
	kfree(arg_org);

	kfree(buf);
}

void brcmf_c_arp_offload_set(dhd_pub_t *dhd, int arp_mode)
{
	char iovbuf[32];
	int retcode;

	brcmu_mkiovar("arp_ol", (char *)&arp_mode, 4, iovbuf, sizeof(iovbuf));
	retcode = brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR,
				   iovbuf, sizeof(iovbuf));
	retcode = retcode >= 0 ? 0 : retcode;
	if (retcode)
		DHD_TRACE(("%s: failed to set ARP offload mode to 0x%x, "
			"retcode = %d\n", __func__, arp_mode, retcode));
	else
		DHD_TRACE(("%s: successfully set ARP offload mode to 0x%x\n",
			   __func__, arp_mode));
}

void brcmf_c_arp_offload_enable(dhd_pub_t *dhd, int arp_enable)
{
	char iovbuf[32];
	int retcode;

	brcmu_mkiovar("arpoe", (char *)&arp_enable, 4, iovbuf, sizeof(iovbuf));
	retcode = brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR,
				   iovbuf, sizeof(iovbuf));
	retcode = retcode >= 0 ? 0 : retcode;
	if (retcode)
		DHD_TRACE(("%s: failed to enabe ARP offload to %d, "
			"retcode = %d\n", __func__, arp_enable, retcode));
	else
		DHD_TRACE(("%s: successfully enabed ARP offload to %d\n",
			   __func__, arp_enable));
}

int brcmf_c_preinit_ioctls(dhd_pub_t *dhd)
{
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for
				 "event_msgs" + '\0' + bitvec  */
	uint up = 0;
	char buf[128], *ptr;
	uint power_mode = PM_FAST;
	u32 dongle_align = BRCMF_SDALIGN;
	u32 glom = 0;
	uint bcn_timeout = 3;
	int scan_assoc_time = 40;
	int scan_unassoc_time = 40;
	int i;

	brcmf_os_proto_block(dhd);

	/* Set Country code */
	if (dhd->country_code[0] != 0) {
		if (brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_COUNTRY,
				     dhd->country_code,
				     sizeof(dhd->country_code)) < 0) {
			DHD_ERROR(("%s: country code setting failed\n",
				   __func__));
		}
	}

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	ptr = buf;
	brcmu_mkiovar("ver", 0, 0, buf, sizeof(buf));
	brcmf_proto_cdc_query_ioctl(dhd, 0, BRCMF_C_GET_VAR, buf, sizeof(buf));
	strsep(&ptr, "\n");
	/* Print fw version info */
	DHD_ERROR(("Firmware version = %s\n", buf));

	/* Set PowerSave mode */
	brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_PM, (char *)&power_mode,
			 sizeof(power_mode));

	/* Match Host and Dongle rx alignment */
	brcmu_mkiovar("bus:txglomalign", (char *)&dongle_align, 4, iovbuf,
		    sizeof(iovbuf));
	brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR, iovbuf,
				  sizeof(iovbuf));

	/* disable glom option per default */
	brcmu_mkiovar("bus:txglom", (char *)&glom, 4, iovbuf, sizeof(iovbuf));
	brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR, iovbuf,
				  sizeof(iovbuf));

	/* Setup timeout if Beacons are lost and roam is off to report
		 link down */
	brcmu_mkiovar("bcn_timeout", (char *)&bcn_timeout, 4, iovbuf,
		    sizeof(iovbuf));
	brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR, iovbuf,
				  sizeof(iovbuf));

	/* Enable/Disable build-in roaming to allowed ext supplicant to take
		 of romaing */
	brcmu_mkiovar("roam_off", (char *)&brcmf_roam, 4,
		      iovbuf, sizeof(iovbuf));
	brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR, iovbuf,
				  sizeof(iovbuf));

	/* Force STA UP */
	if (brcmf_radio_up)
		brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_UP, (char *)&up,
					  sizeof(up));

	/* Setup event_msgs */
	brcmu_mkiovar("event_msgs", dhd->eventmask, WL_EVENTING_MASK_LEN,
		      iovbuf, sizeof(iovbuf));
	brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_VAR, iovbuf,
				  sizeof(iovbuf));

	brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_SCAN_CHANNEL_TIME,
			 (char *)&scan_assoc_time, sizeof(scan_assoc_time));
	brcmf_proto_cdc_set_ioctl(dhd, 0, BRCMF_C_SET_SCAN_UNASSOC_TIME,
			 (char *)&scan_unassoc_time, sizeof(scan_unassoc_time));

	/* Set and enable ARP offload feature */
	if (brcmf_arp_enable)
		brcmf_c_arp_offload_set(dhd, brcmf_arp_mode);
	brcmf_c_arp_offload_enable(dhd, brcmf_arp_enable);

	/* Set up pkt filter */
	if (brcmf_pkt_filter_enable) {
		for (i = 0; i < dhd->pktfilter_count; i++) {
			brcmf_c_pktfilter_offload_set(dhd,
						  dhd->pktfilter[i]);
			brcmf_c_pktfilter_offload_enable(dhd,
			     dhd->pktfilter[i],
			     brcmf_pkt_filter_init,
			     brcmf_master_mode);
		}
	}

	brcmf_os_proto_unblock(dhd);

	return 0;
}
