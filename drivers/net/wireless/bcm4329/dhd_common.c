/*
 * Broadcom Dongle Host Driver (DHD), common DHD core.
 *
 * Copyright (C) 1999-2010, Broadcom Corporation
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
 * $Id: dhd_common.c,v 1.5.6.8.2.6.6.69.4.25 2011/02/11 21:16:02 Exp $
 */
#include <typedefs.h>
#include <osl.h>

#include <epivers.h>
#include <bcmutils.h>

#include <bcmendian.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <msgtrace.h>

#include <wlioctl.h>

#ifdef SET_RANDOM_MAC_SOFTAP
#include <linux/random.h>
#include <linux/jiffies.h>
#endif

#ifdef GET_CUSTOM_MAC_ENABLE
int wifi_get_mac_addr(unsigned char *buf);
#endif /* GET_CUSTOM_MAC_ENABLE */

int dhd_msg_level;

#include <wl_iw.h>

char fw_path[MOD_PARAM_PATHLEN];
char nv_path[MOD_PARAM_PATHLEN];

/* Last connection success/failure status */
uint32 dhd_conn_event;
uint32 dhd_conn_status;
uint32 dhd_conn_reason;

#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i

extern int dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len);
extern void dhd_ind_scan_confirm(void *h, bool status);
extern int dhd_wl_ioctl(dhd_pub_t *dhd, uint cmd, char *buf, uint buflen);
void dhd_iscan_lock(void);
void dhd_iscan_unlock(void);

#if defined(SOFTAP)
extern bool ap_fw_loaded;
#endif
#if defined(KEEP_ALIVE)
int dhd_keep_alive_onoff(dhd_pub_t *dhd, int ka_on);
#endif /* KEEP_ALIVE */

/* Packet alignment for most efficient SDIO (can change based on platform) */
#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
#if !ISPOWEROF2(DHD_SDALIGN)
#error DHD_SDALIGN is not a power of 2!
#endif

#ifdef DHD_DEBUG
const char dhd_version[] = "Dongle Host Driver, version " EPI_VERSION_STR "\nCompiled on "
	__DATE__ " at " __TIME__;
#else
const char dhd_version[] = "Dongle Host Driver, version " EPI_VERSION_STR;
#endif

void dhd_set_timer(void *bus, uint wdtick);

/* IOVar table */
enum {
	IOV_VERSION = 1,
	IOV_MSGLEVEL,
	IOV_BCMERRORSTR,
	IOV_BCMERROR,
	IOV_WDTICK,
	IOV_DUMP,
#ifdef DHD_DEBUG
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

const bcm_iovar_t dhd_iovars[] = {
	{"version", 	IOV_VERSION,	0,	IOVT_BUFFER,	sizeof(dhd_version) },
#ifdef DHD_DEBUG
	{"msglevel",	IOV_MSGLEVEL,	0,	IOVT_UINT32,	0 },
#endif /* DHD_DEBUG */
	{"bcmerrorstr", IOV_BCMERRORSTR, 0, IOVT_BUFFER,	BCME_STRLEN },
	{"bcmerror",	IOV_BCMERROR,	0,	IOVT_INT8,	0 },
	{"wdtick",	IOV_WDTICK, 0,	IOVT_UINT32,	0 },
	{"dump",	IOV_DUMP,	0,	IOVT_BUFFER,	DHD_IOCTL_MAXLEN },
#ifdef DHD_DEBUG
	{"dconpoll",	IOV_DCONSOLE_POLL, 0,	IOVT_UINT32,	0 },
	{"cons",	IOV_CONS,	0,	IOVT_BUFFER,	0 },
#endif
	{"clearcounts", IOV_CLEARCOUNTS, 0, IOVT_VOID,	0 },
	{"gpioob",	IOV_GPIOOB,	0,	IOVT_UINT32,	0 },
	{"ioctl_timeout",	IOV_IOCTLTIMEOUT,	0,	IOVT_UINT32,	0 },
	{NULL, 0, 0, 0, 0 }
};

void
dhd_common_init(void)
{
	/* Init global variables at run-time, not as part of the declaration.
	 * This is required to support init/de-init of the driver. Initialization
	 * of globals as part of the declaration results in non-deterministic
	 * behaviour since the value of the globals may be different on the
	 * first time that the driver is initialized vs subsequent initializations.
	 */
	dhd_msg_level = DHD_ERROR_VAL;
#ifdef CONFIG_BCM4329_FW_PATH
	strncpy(fw_path, CONFIG_BCM4329_FW_PATH, MOD_PARAM_PATHLEN-1);
#else
	fw_path[0] = '\0';
#endif
#ifdef CONFIG_BCM4329_NVRAM_PATH
	strncpy(nv_path, CONFIG_BCM4329_NVRAM_PATH, MOD_PARAM_PATHLEN-1);
#else
	nv_path[0] = '\0';
#endif
}

static int
dhd_dump(dhd_pub_t *dhdp, char *buf, int buflen)
{
	char eabuf[ETHER_ADDR_STR_LEN];

	struct bcmstrbuf b;
	struct bcmstrbuf *strbuf = &b;

	bcm_binit(strbuf, buf, buflen);

	/* Base DHD info */
	bcm_bprintf(strbuf, "%s\n", dhd_version);
	bcm_bprintf(strbuf, "\n");
	bcm_bprintf(strbuf, "pub.up %d pub.txoff %d pub.busstate %d\n",
	            dhdp->up, dhdp->txoff, dhdp->busstate);
	bcm_bprintf(strbuf, "pub.hdrlen %d pub.maxctl %d pub.rxsz %d\n",
	            dhdp->hdrlen, dhdp->maxctl, dhdp->rxsz);
	bcm_bprintf(strbuf, "pub.iswl %d pub.drv_version %ld pub.mac %s\n",
	            dhdp->iswl, dhdp->drv_version, bcm_ether_ntoa(&dhdp->mac, eabuf));
	bcm_bprintf(strbuf, "pub.bcmerror %d tickcnt %d\n", dhdp->bcmerror, dhdp->tickcnt);

	bcm_bprintf(strbuf, "dongle stats:\n");
	bcm_bprintf(strbuf, "tx_packets %ld tx_bytes %ld tx_errors %ld tx_dropped %ld\n",
	            dhdp->dstats.tx_packets, dhdp->dstats.tx_bytes,
	            dhdp->dstats.tx_errors, dhdp->dstats.tx_dropped);
	bcm_bprintf(strbuf, "rx_packets %ld rx_bytes %ld rx_errors %ld rx_dropped %ld\n",
	            dhdp->dstats.rx_packets, dhdp->dstats.rx_bytes,
	            dhdp->dstats.rx_errors, dhdp->dstats.rx_dropped);
	bcm_bprintf(strbuf, "multicast %ld\n", dhdp->dstats.multicast);

	bcm_bprintf(strbuf, "bus stats:\n");
	bcm_bprintf(strbuf, "tx_packets %ld tx_multicast %ld tx_errors %ld\n",
	            dhdp->tx_packets, dhdp->tx_multicast, dhdp->tx_errors);
	bcm_bprintf(strbuf, "tx_ctlpkts %ld tx_ctlerrs %ld\n",
	            dhdp->tx_ctlpkts, dhdp->tx_ctlerrs);
	bcm_bprintf(strbuf, "rx_packets %ld rx_multicast %ld rx_errors %ld \n",
	            dhdp->rx_packets, dhdp->rx_multicast, dhdp->rx_errors);
	bcm_bprintf(strbuf, "rx_ctlpkts %ld rx_ctlerrs %ld rx_dropped %ld rx_flushed %ld\n",
	            dhdp->rx_ctlpkts, dhdp->rx_ctlerrs, dhdp->rx_dropped, dhdp->rx_flushed);
	bcm_bprintf(strbuf, "rx_readahead_cnt %ld tx_realloc %ld fc_packets %ld\n",
	            dhdp->rx_readahead_cnt, dhdp->tx_realloc, dhdp->fc_packets);
	bcm_bprintf(strbuf, "wd_dpc_sched %ld\n", dhdp->wd_dpc_sched);
	bcm_bprintf(strbuf, "\n");

	/* Add any prot info */
	dhd_prot_dump(dhdp, strbuf);
	bcm_bprintf(strbuf, "\n");

	/* Add any bus info */
	dhd_bus_dump(dhdp, strbuf);

	return (!strbuf->size ? BCME_BUFTOOSHORT : 0);
}

static int
dhd_doiovar(dhd_pub_t *dhd_pub, const bcm_iovar_t *vi, uint32 actionid, const char *name,
            void *params, int plen, void *arg, int len, int val_size)
{
	int bcmerror = 0;
	int32 int_val = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid))) != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	switch (actionid) {
	case IOV_GVAL(IOV_VERSION):
		/* Need to have checked buffer length */
		strncpy((char*)arg, dhd_version, len);
		break;

	case IOV_GVAL(IOV_MSGLEVEL):
		int_val = (int32)dhd_msg_level;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_MSGLEVEL):
		dhd_msg_level = int_val;
		break;

	case IOV_GVAL(IOV_BCMERRORSTR):
		strncpy((char *)arg, bcmerrorstr(dhd_pub->bcmerror), BCME_STRLEN);
		((char *)arg)[BCME_STRLEN - 1] = 0x00;
		break;

	case IOV_GVAL(IOV_BCMERROR):
		int_val = (int32)dhd_pub->bcmerror;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_WDTICK):
		int_val = (int32)dhd_watchdog_ms;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_WDTICK):
		if (!dhd_pub->up) {
			bcmerror = BCME_NOTUP;
			break;
		}
		dhd_os_wd_timer(dhd_pub, (uint)int_val);
		break;

	case IOV_GVAL(IOV_DUMP):
		bcmerror = dhd_dump(dhd_pub, arg, len);
		break;

#ifdef DHD_DEBUG
	case IOV_GVAL(IOV_DCONSOLE_POLL):
		int_val = (int32)dhd_console_ms;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DCONSOLE_POLL):
		dhd_console_ms = (uint)int_val;
		break;

	case IOV_SVAL(IOV_CONS):
		if (len > 0)
			bcmerror = dhd_bus_console_in(dhd_pub, arg, len - 1);
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


	case IOV_GVAL(IOV_IOCTLTIMEOUT): {
		int_val = (int32)dhd_os_get_ioctl_resp_timeout();
		bcopy(&int_val, arg, sizeof(int_val));
		break;
	}

	case IOV_SVAL(IOV_IOCTLTIMEOUT): {
		if (int_val <= 0)
			bcmerror = BCME_BADARG;
		else
			dhd_os_set_ioctl_resp_timeout((unsigned int)int_val);
		break;
	}


	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:
	return bcmerror;
}

/* Store the status of a connection attempt for later retrieval by an iovar */
void
dhd_store_conn_status(uint32 event, uint32 status, uint32 reason)
{
	/* Do not overwrite a WLC_E_PRUNE with a WLC_E_SET_SSID
	 * because an encryption/rsn mismatch results in both events, and
	 * the important information is in the WLC_E_PRUNE.
	 */
	if (!(event == WLC_E_SET_SSID && status == WLC_E_STATUS_FAIL &&
	      dhd_conn_event == WLC_E_PRUNE)) {
		dhd_conn_event = event;
		dhd_conn_status = status;
		dhd_conn_reason = reason;
	}
}

bool
dhd_prec_enq(dhd_pub_t *dhdp, struct pktq *q, void *pkt, int prec)
{
	void *p;
	int eprec = -1;		/* precedence to evict from */
	bool discard_oldest;

	/* Fast case, precedence queue is not full and we are also not
	 * exceeding total queue length
	 */
	if (!pktq_pfull(q, prec) && !pktq_full(q)) {
		pktq_penq(q, prec, pkt);
		return TRUE;
	}

	/* Determine precedence from which to evict packet, if any */
	if (pktq_pfull(q, prec))
		eprec = prec;
	else if (pktq_full(q)) {
		p = pktq_peek_tail(q, &eprec);
		ASSERT(p);
		if (eprec > prec)
			return FALSE;
	}

	/* Evict if needed */
	if (eprec >= 0) {
		/* Detect queueing to unconfigured precedence */
		ASSERT(!pktq_pempty(q, eprec));
		discard_oldest = AC_BITMAP_TST(dhdp->wme_dp, eprec);
		if (eprec == prec && !discard_oldest)
			return FALSE;		/* refuse newer (incoming) packet */
		/* Evict packet according to discard policy */
		p = discard_oldest ? pktq_pdeq(q, eprec) : pktq_pdeq_tail(q, eprec);
		if (p == NULL) {
			DHD_ERROR(("%s: pktq_penq() failed, oldest %d.",
				__FUNCTION__, discard_oldest));
			ASSERT(p);
		}

		PKTFREE(dhdp->osh, p, TRUE);
	}

	/* Enqueue */
	p = pktq_penq(q, prec, pkt);
	if (p == NULL) {
		DHD_ERROR(("%s: pktq_penq() failed.", __FUNCTION__));
		ASSERT(p);
	}

	return TRUE;
}

static int
dhd_iovar_op(dhd_pub_t *dhd_pub, const char *name,
             void *params, int plen, void *arg, int len, bool set)
{
	int bcmerror = 0;
	int val_size;
	const bcm_iovar_t *vi = NULL;
	uint32 actionid;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get MUST have return space */
	ASSERT(set || (arg && len));

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !plen));

	if ((vi = bcm_iovar_lookup(dhd_iovars, name)) == NULL) {
		bcmerror = BCME_UNSUPPORTED;
		goto exit;
	}

	DHD_CTL(("%s: %s %s, len %d plen %d\n", __FUNCTION__,
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
	bcmerror = dhd_doiovar(dhd_pub, vi, actionid, name, params, plen, arg, len, val_size);

exit:
	return bcmerror;
}

int
dhd_ioctl(dhd_pub_t *dhd_pub, dhd_ioctl_t *ioc, void *buf, uint buflen)
{
	int bcmerror = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!buf) return BCME_BADARG;

	switch (ioc->cmd) {
	case DHD_GET_MAGIC:
		if (buflen < sizeof(int))
			bcmerror = BCME_BUFTOOSHORT;
		else
			*(int*)buf = DHD_IOCTL_MAGIC;
		break;

	case DHD_GET_VERSION:
		if (buflen < sizeof(int))
			bcmerror = -BCME_BUFTOOSHORT;
		else
			*(int*)buf = DHD_IOCTL_VERSION;
		break;

	case DHD_GET_VAR:
	case DHD_SET_VAR: {
		char *arg;
		uint arglen;

		/* scan past the name to any arguments */
		for (arg = buf, arglen = buflen; *arg && arglen; arg++, arglen--);

		if (*arg) {
			bcmerror = BCME_BUFTOOSHORT;
			break;
		}

		/* account for the NUL terminator */
		arg++, arglen--;

		/* call with the appropriate arguments */
		if (ioc->cmd == DHD_GET_VAR)
			bcmerror = dhd_iovar_op(dhd_pub, buf, arg, arglen,
			buf, buflen, IOV_GET);
		else
			bcmerror = dhd_iovar_op(dhd_pub, buf, NULL, 0, arg, arglen, IOV_SET);
		if (bcmerror != BCME_UNSUPPORTED)
			break;

		/* not in generic table, try protocol module */
		if (ioc->cmd == DHD_GET_VAR)
			bcmerror = dhd_prot_iovar_op(dhd_pub, buf, arg,
			                             arglen, buf, buflen, IOV_GET);
		else
			bcmerror = dhd_prot_iovar_op(dhd_pub, buf,
			                             NULL, 0, arg, arglen, IOV_SET);
		if (bcmerror != BCME_UNSUPPORTED)
			break;

		/* if still not found, try bus module */
		if (ioc->cmd == DHD_GET_VAR)
			bcmerror = dhd_bus_iovar_op(dhd_pub, buf,
			                            arg, arglen, buf, buflen, IOV_GET);
		else
			bcmerror = dhd_bus_iovar_op(dhd_pub, buf,
			                            NULL, 0, arg, arglen, IOV_SET);

		break;
	}

	default:
		bcmerror = BCME_UNSUPPORTED;
	}

	return bcmerror;
}


#ifdef SHOW_EVENTS
static void
wl_show_host_event(wl_event_msg_t *event, void *event_data)
{
	uint i, status, reason;
	bool group = FALSE, flush_txq = FALSE, link = FALSE;
	char *auth_str, *event_name;
	uchar *buf;
	char err_msg[256], eabuf[ETHER_ADDR_STR_LEN];
	static struct {uint event; char *event_name;} event_names[] = {
		{WLC_E_SET_SSID, "SET_SSID"},
		{WLC_E_JOIN, "JOIN"},
		{WLC_E_START, "START"},
		{WLC_E_AUTH, "AUTH"},
		{WLC_E_AUTH_IND, "AUTH_IND"},
		{WLC_E_DEAUTH, "DEAUTH"},
		{WLC_E_DEAUTH_IND, "DEAUTH_IND"},
		{WLC_E_ASSOC, "ASSOC"},
		{WLC_E_ASSOC_IND, "ASSOC_IND"},
		{WLC_E_REASSOC, "REASSOC"},
		{WLC_E_REASSOC_IND, "REASSOC_IND"},
		{WLC_E_DISASSOC, "DISASSOC"},
		{WLC_E_DISASSOC_IND, "DISASSOC_IND"},
		{WLC_E_QUIET_START, "START_QUIET"},
		{WLC_E_QUIET_END, "END_QUIET"},
		{WLC_E_BEACON_RX, "BEACON_RX"},
		{WLC_E_LINK, "LINK"},
		{WLC_E_MIC_ERROR, "MIC_ERROR"},
		{WLC_E_NDIS_LINK, "NDIS_LINK"},
		{WLC_E_ROAM, "ROAM"},
		{WLC_E_TXFAIL, "TXFAIL"},
		{WLC_E_PMKID_CACHE, "PMKID_CACHE"},
		{WLC_E_RETROGRADE_TSF, "RETROGRADE_TSF"},
		{WLC_E_PRUNE, "PRUNE"},
		{WLC_E_AUTOAUTH, "AUTOAUTH"},
		{WLC_E_EAPOL_MSG, "EAPOL_MSG"},
		{WLC_E_SCAN_COMPLETE, "SCAN_COMPLETE"},
		{WLC_E_ADDTS_IND, "ADDTS_IND"},
		{WLC_E_DELTS_IND, "DELTS_IND"},
		{WLC_E_BCNSENT_IND, "BCNSENT_IND"},
		{WLC_E_BCNRX_MSG, "BCNRX_MSG"},
		{WLC_E_BCNLOST_MSG, "BCNLOST_MSG"},
		{WLC_E_ROAM_PREP, "ROAM_PREP"},
		{WLC_E_PFN_NET_FOUND, "PNO_NET_FOUND"},
		{WLC_E_PFN_NET_LOST, "PNO_NET_LOST"},
		{WLC_E_RESET_COMPLETE, "RESET_COMPLETE"},
		{WLC_E_JOIN_START, "JOIN_START"},
		{WLC_E_ROAM_START, "ROAM_START"},
		{WLC_E_ASSOC_START, "ASSOC_START"},
		{WLC_E_IBSS_ASSOC, "IBSS_ASSOC"},
		{WLC_E_RADIO, "RADIO"},
		{WLC_E_PSM_WATCHDOG, "PSM_WATCHDOG"},
		{WLC_E_PROBREQ_MSG, "PROBREQ_MSG"},
		{WLC_E_SCAN_CONFIRM_IND, "SCAN_CONFIRM_IND"},
		{WLC_E_PSK_SUP, "PSK_SUP"},
		{WLC_E_COUNTRY_CODE_CHANGED, "COUNTRY_CODE_CHANGED"},
		{WLC_E_EXCEEDED_MEDIUM_TIME, "EXCEEDED_MEDIUM_TIME"},
		{WLC_E_ICV_ERROR, "ICV_ERROR"},
		{WLC_E_UNICAST_DECODE_ERROR, "UNICAST_DECODE_ERROR"},
		{WLC_E_MULTICAST_DECODE_ERROR, "MULTICAST_DECODE_ERROR"},
		{WLC_E_TRACE, "TRACE"},
		{WLC_E_ACTION_FRAME, "ACTION FRAME"},
		{WLC_E_ACTION_FRAME_COMPLETE, "ACTION FRAME TX COMPLETE"},
		{WLC_E_IF, "IF"},
		{WLC_E_RSSI, "RSSI"},
		{WLC_E_PFN_SCAN_COMPLETE, "SCAN_COMPLETE"}
	};
	uint event_type, flags, auth_type, datalen;
	event_type = ntoh32(event->event_type);
	flags = ntoh16(event->flags);
	status = ntoh32(event->status);
	reason = ntoh32(event->reason);
	auth_type = ntoh32(event->auth_type);
	datalen = ntoh32(event->datalen);
	/* debug dump of event messages */
	sprintf(eabuf, "%02x:%02x:%02x:%02x:%02x:%02x",
	        (uchar)event->addr.octet[0]&0xff,
	        (uchar)event->addr.octet[1]&0xff,
	        (uchar)event->addr.octet[2]&0xff,
	        (uchar)event->addr.octet[3]&0xff,
	        (uchar)event->addr.octet[4]&0xff,
	        (uchar)event->addr.octet[5]&0xff);

	event_name = "UNKNOWN";
	for (i = 0; i < ARRAYSIZE(event_names); i++) {
		if (event_names[i].event == event_type)
			event_name = event_names[i].event_name;
	}

	DHD_EVENT(("EVENT: %s, event ID = %d\n", event_name, event_type));

	if (flags & WLC_EVENT_MSG_LINK)
		link = TRUE;
	if (flags & WLC_EVENT_MSG_GROUP)
		group = TRUE;
	if (flags & WLC_EVENT_MSG_FLUSHTXQ)
		flush_txq = TRUE;

	switch (event_type) {
	case WLC_E_START:
	case WLC_E_DEAUTH:
	case WLC_E_DISASSOC:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

	case WLC_E_ASSOC_IND:
	case WLC_E_REASSOC_IND:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		break;

	case WLC_E_ASSOC:
	case WLC_E_REASSOC:
		if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, SUCCESS\n", event_name, eabuf));
		} else if (status == WLC_E_STATUS_TIMEOUT) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, TIMEOUT\n", event_name, eabuf));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, FAILURE, reason %d\n",
			       event_name, eabuf, (int)reason));
		} else {
			DHD_EVENT(("MACEVENT: %s, MAC %s, unexpected status %d\n",
			       event_name, eabuf, (int)status));
		}
		break;

	case WLC_E_DEAUTH_IND:
	case WLC_E_DISASSOC_IND:
		DHD_EVENT(("MACEVENT: %s, MAC %s, reason %d\n", event_name, eabuf, (int)reason));
		break;

	case WLC_E_AUTH:
	case WLC_E_AUTH_IND:
		if (auth_type == DOT11_OPEN_SYSTEM)
			auth_str = "Open System";
		else if (auth_type == DOT11_SHARED_KEY)
			auth_str = "Shared Key";
		else {
			sprintf(err_msg, "AUTH unknown: %d", (int)auth_type);
			auth_str = err_msg;
		}
		if (event_type == WLC_E_AUTH_IND) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s\n", event_name, eabuf, auth_str));
		} else if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, SUCCESS\n",
				event_name, eabuf, auth_str));
		} else if (status == WLC_E_STATUS_TIMEOUT) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, TIMEOUT\n",
				event_name, eabuf, auth_str));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, MAC %s, %s, FAILURE, reason %d\n",
			       event_name, eabuf, auth_str, (int)reason));
		}

		break;

	case WLC_E_JOIN:
	case WLC_E_ROAM:
	case WLC_E_SET_SSID:
		if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, MAC %s\n", event_name, eabuf));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, failed\n", event_name));
		} else if (status == WLC_E_STATUS_NO_NETWORKS) {
			DHD_EVENT(("MACEVENT: %s, no networks found\n", event_name));
		} else {
			DHD_EVENT(("MACEVENT: %s, unexpected status %d\n",
				event_name, (int)status));
		}
		break;

	case WLC_E_BEACON_RX:
		if (status == WLC_E_STATUS_SUCCESS) {
			DHD_EVENT(("MACEVENT: %s, SUCCESS\n", event_name));
		} else if (status == WLC_E_STATUS_FAIL) {
			DHD_EVENT(("MACEVENT: %s, FAIL\n", event_name));
		} else {
			DHD_EVENT(("MACEVENT: %s, status %d\n", event_name, status));
		}
		break;

	case WLC_E_LINK:
		DHD_EVENT(("MACEVENT: %s %s\n", event_name, link?"UP":"DOWN"));
		break;

	case WLC_E_MIC_ERROR:
		DHD_EVENT(("MACEVENT: %s, MAC %s, Group %d, Flush %d\n",
		       event_name, eabuf, group, flush_txq));
		break;

	case WLC_E_ICV_ERROR:
	case WLC_E_UNICAST_DECODE_ERROR:
	case WLC_E_MULTICAST_DECODE_ERROR:
		DHD_EVENT(("MACEVENT: %s, MAC %s\n",
		       event_name, eabuf));
		break;

	case WLC_E_TXFAIL:
		DHD_EVENT(("MACEVENT: %s, RA %s\n", event_name, eabuf));
		break;

	case WLC_E_SCAN_COMPLETE:
	case WLC_E_PMKID_CACHE:
		DHD_EVENT(("MACEVENT: %s\n", event_name));
		break;

	case WLC_E_PFN_NET_FOUND:
	case WLC_E_PFN_NET_LOST:
	case WLC_E_PFN_SCAN_COMPLETE:
		DHD_EVENT(("PNOEVENT: %s\n", event_name));
		break;

	case WLC_E_PSK_SUP:
	case WLC_E_PRUNE:
		DHD_EVENT(("MACEVENT: %s, status %d, reason %d\n",
		           event_name, (int)status, (int)reason));
		break;

	case WLC_E_TRACE:
		{
			static uint32 seqnum_prev = 0;
			msgtrace_hdr_t hdr;
			uint32 nblost;
			char *s, *p;

			buf = (uchar *) event_data;
			memcpy(&hdr, buf, MSGTRACE_HDRLEN);

			if (hdr.version != MSGTRACE_VERSION) {
				printf("\nMACEVENT: %s [unsupported version --> "
				       "dhd version:%d dongle version:%d]\n",
				       event_name, MSGTRACE_VERSION, hdr.version);
				/* Reset datalen to avoid display below */
				datalen = 0;
				break;
			}

			/* There are 2 bytes available at the end of data */
			buf[MSGTRACE_HDRLEN + ntoh16(hdr.len)] = '\0';

			if (ntoh32(hdr.discarded_bytes) || ntoh32(hdr.discarded_printf)) {
				printf("\nWLC_E_TRACE: [Discarded traces in dongle -->"
				       "discarded_bytes %d discarded_printf %d]\n",
				       ntoh32(hdr.discarded_bytes), ntoh32(hdr.discarded_printf));
			}

			nblost = ntoh32(hdr.seqnum) - seqnum_prev - 1;
			if (nblost > 0) {
				printf("\nWLC_E_TRACE: [Event lost --> seqnum %d nblost %d\n",
				        ntoh32(hdr.seqnum), nblost);
			}
			seqnum_prev = ntoh32(hdr.seqnum);

			/* Display the trace buffer. Advance from \n to \n to avoid display big
			 * printf (issue with Linux printk )
			 */
			p = (char *)&buf[MSGTRACE_HDRLEN];
			while ((s = strstr(p, "\n")) != NULL) {
				*s = '\0';
				printf("%s\n", p);
				p = s + 1;
			}
			printf("%s\n", p);

			/* Reset datalen to avoid display below */
			datalen = 0;
		}
		break;


	case WLC_E_RSSI:
		DHD_EVENT(("MACEVENT: %s %d\n", event_name, ntoh32(*((int *)event_data))));
		break;

	default:
		DHD_EVENT(("MACEVENT: %s %d, MAC %s, status %d, reason %d, auth %d\n",
		       event_name, event_type, eabuf, (int)status, (int)reason,
		       (int)auth_type));
		break;
	}

	/* show any appended data */
	if (datalen) {
		buf = (uchar *) event_data;
		DHD_EVENT((" data (%d) : ", datalen));
		for (i = 0; i < datalen; i++)
			DHD_EVENT((" 0x%02x ", *buf++));
		DHD_EVENT(("\n"));
	}
}
#endif /* SHOW_EVENTS */

int
wl_host_event(struct dhd_info *dhd, int *ifidx, void *pktdata,
              wl_event_msg_t *event, void **data_ptr)
{
	/* check whether packet is a BRCM event pkt */
	bcm_event_t *pvt_data = (bcm_event_t *)pktdata;
	char *event_data;
	uint32 type, status;
	uint16 flags;
	int evlen;

	if (bcmp(BRCM_OUI, &pvt_data->bcm_hdr.oui[0], DOT11_OUI_LEN)) {
		DHD_ERROR(("%s: mismatched OUI, bailing\n", __FUNCTION__));
		return (BCME_ERROR);
	}

	/* BRCM event pkt may be unaligned - use xxx_ua to load user_subtype. */
	if (ntoh16_ua((void *)&pvt_data->bcm_hdr.usr_subtype) != BCMILCP_BCM_SUBTYPE_EVENT) {
		DHD_ERROR(("%s: mismatched subtype, bailing\n", __FUNCTION__));
		return (BCME_ERROR);
	}

	*data_ptr = &pvt_data[1];
	event_data = *data_ptr;

	/* memcpy since BRCM event pkt may be unaligned. */
	memcpy(event, &pvt_data->event, sizeof(wl_event_msg_t));

	type = ntoh32_ua((void *)&event->event_type);
	flags = ntoh16_ua((void *)&event->flags);
	status = ntoh32_ua((void *)&event->status);
	evlen = ntoh32_ua((void *)&event->datalen) + sizeof(bcm_event_t);

	switch (type) {
		case WLC_E_IF:
			{
				dhd_if_event_t *ifevent = (dhd_if_event_t *)event_data;
				DHD_TRACE(("%s: if event\n", __FUNCTION__));

				if (ifevent->ifidx > 0 && ifevent->ifidx < DHD_MAX_IFS)
				{
					if (ifevent->action == WLC_E_IF_ADD)
						dhd_add_if(dhd, ifevent->ifidx,
							NULL, event->ifname,
							pvt_data->eth.ether_dhost,
							ifevent->flags, ifevent->bssidx);
					else
						dhd_del_if(dhd, ifevent->ifidx);
				} else {
					DHD_ERROR(("%s: Invalid ifidx %d for %s\n",
						__FUNCTION__, ifevent->ifidx, event->ifname));
				}
			}
			/* send up the if event: btamp user needs it */
			*ifidx = dhd_ifname2idx(dhd, event->ifname);
			/* push up to external supp/auth */
			dhd_event(dhd, (char *)pvt_data, evlen, *ifidx);
			break;


#ifdef P2P
		case WLC_E_NDIS_LINK:
			break;
#endif
		/* fall through */
		/* These are what external supplicant/authenticator wants */
		case WLC_E_LINK:
		case WLC_E_ASSOC_IND:
		case WLC_E_REASSOC_IND:
		case WLC_E_DISASSOC_IND:
		case WLC_E_MIC_ERROR:
		default:
		/* Fall through: this should get _everything_  */

			*ifidx = dhd_ifname2idx(dhd, event->ifname);
			/* push up to external supp/auth */
			dhd_event(dhd, (char *)pvt_data, evlen, *ifidx);
			DHD_TRACE(("%s: MAC event %d, flags %x, status %x\n",
			           __FUNCTION__, type, flags, status));

			/* put it back to WLC_E_NDIS_LINK */
			if (type == WLC_E_NDIS_LINK) {
				uint32 temp;

				temp = ntoh32_ua((void *)&event->event_type);
				DHD_TRACE(("Converted to WLC_E_LINK type %d\n", temp));

				temp = ntoh32(WLC_E_NDIS_LINK);
				memcpy((void *)(&pvt_data->event.event_type), &temp,
					sizeof(pvt_data->event.event_type));
			}
			break;
	}

#ifdef SHOW_EVENTS
	wl_show_host_event(event, event_data);
#endif /* SHOW_EVENTS */

	return (BCME_OK);
}


void
wl_event_to_host_order(wl_event_msg_t *evt)
{
	/* Event struct members passed from dongle to host are stored in network
	 * byte order. Convert all members to host-order.
	 */
	evt->event_type = ntoh32(evt->event_type);
	evt->flags = ntoh16(evt->flags);
	evt->status = ntoh32(evt->status);
	evt->reason = ntoh32(evt->reason);
	evt->auth_type = ntoh32(evt->auth_type);
	evt->datalen = ntoh32(evt->datalen);
	evt->version = ntoh16(evt->version);
}

void print_buf(void *pbuf, int len, int bytes_per_line)
{
	int i, j = 0;
	unsigned char *buf = pbuf;

	if (bytes_per_line == 0) {
		bytes_per_line = len;
	}

	for (i = 0; i < len; i++) {
		printf("%2.2x", *buf++);
		j++;
		if (j == bytes_per_line) {
			printf("\n");
			j = 0;
		} else {
			printf(":");
		}
	}
	printf("\n");
}

#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))

#ifdef PKT_FILTER_SUPPORT
/* Convert user's input in hex pattern to byte-size mask */
static int
wl_pattern_atoh(char *src, char *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 &&
	    strncmp(src, "0X", 2) != 0) {
		DHD_ERROR(("Mask invalid format. Needs to start with 0x\n"));
		return -1;
	}
	src = src + 2; /* Skip past 0x */
	if (strlen(src) % 2 != 0) {
		DHD_ERROR(("Mask invalid format. Needs to be of even length\n"));
		return -1;
	}
	for (i = 0; *src != '\0'; i++) {
		char num[3];
		strncpy(num, src, 2);
		num[2] = '\0';
		dst[i] = (uint8)strtoul(num, NULL, 16);
		src += 2;
	}
	return i;
}

void
dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode)
{
	char				*argv[8];
	int					i = 0;
	const char 			*str;
	int					buf_len;
	int					str_len;
	char				*arg_save = 0, *arg_org = 0;
	int					rc;
	char				buf[128];
	wl_pkt_filter_enable_t	enable_parm;
	wl_pkt_filter_enable_t	* pkt_filterp;

	if (!(arg_save = MALLOC(dhd->osh, strlen(arg) + 1))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}
	arg_org = arg_save;
	memcpy(arg_save, arg, strlen(arg) + 1);

	argv[i] = bcmstrtok(&arg_save, " ", 0);

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

	pkt_filterp = (wl_pkt_filter_enable_t *)(buf + str_len + 1);

	/* Parse packet filter id. */
	enable_parm.id = htod32(strtoul(argv[i], NULL, 0));

	/* Parse enable/disable value. */
	enable_parm.enable = htod32(enable);

	buf_len += sizeof(enable_parm);
	memcpy((char *)pkt_filterp,
	       &enable_parm,
	       sizeof(enable_parm));

	/* Enable/disable the specified filter. */
	rc = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, buf_len);
	rc = rc >= 0 ? 0 : rc;
	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
		__FUNCTION__, arg, rc));
	else
		DHD_TRACE(("%s: successfully added pktfilter %s\n",
		__FUNCTION__, arg));

	/* Contorl the master mode */
	bcm_mkiovar("pkt_filter_mode", (char *)&master_mode, 4, buf, sizeof(buf));
	rc = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, sizeof(buf));
	rc = rc >= 0 ? 0 : rc;
	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
		__FUNCTION__, arg, rc));

fail:
	if (arg_org)
		MFREE(dhd->osh, arg_org, strlen(arg) + 1);
}

void
dhd_pktfilter_offload_set(dhd_pub_t * dhd, char *arg)
{
	const char 			*str;
	wl_pkt_filter_t		pkt_filter;
	wl_pkt_filter_t		*pkt_filterp;
	int					buf_len;
	int					str_len;
	int 				rc;
	uint32				mask_size;
	uint32				pattern_size;
	char				*argv[8], * buf = 0;
	int					i = 0;
	char				*arg_save = 0, *arg_org = 0;
#define BUF_SIZE		2048

	if (!(arg_save = MALLOC(dhd->osh, strlen(arg) + 1))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}

	arg_org = arg_save;

	if (!(buf = MALLOC(dhd->osh, BUF_SIZE))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}

	memcpy(arg_save, arg, strlen(arg) + 1);

	if (strlen(arg) > BUF_SIZE) {
		DHD_ERROR(("Not enough buffer %d < %d\n", (int)strlen(arg), (int)sizeof(buf)));
		goto fail;
	}

	argv[i] = bcmstrtok(&arg_save, " ", 0);
	while (argv[i++])
		argv[i] = bcmstrtok(&arg_save, " ", 0);

	i = 0;
	if (NULL == argv[i]) {
		DHD_ERROR(("No args provided\n"));
		goto fail;
	}

	str = "pkt_filter_add";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[ str_len ] = '\0';
	buf_len = str_len + 1;

	pkt_filterp = (wl_pkt_filter_t *) (buf + str_len + 1);

	/* Parse packet filter id. */
	pkt_filter.id = htod32(strtoul(argv[i], NULL, 0));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Polarity not provided\n"));
		goto fail;
	}

	/* Parse filter polarity. */
	pkt_filter.negate_match = htod32(strtoul(argv[i], NULL, 0));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Filter type not provided\n"));
		goto fail;
	}

	/* Parse filter type. */
	pkt_filter.type = htod32(strtoul(argv[i], NULL, 0));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Offset not provided\n"));
		goto fail;
	}

	/* Parse pattern filter offset. */
	pkt_filter.u.pattern.offset = htod32(strtoul(argv[i], NULL, 0));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Bitmask not provided\n"));
		goto fail;
	}

	/* Parse pattern filter mask. */
	mask_size =
		htod32(wl_pattern_atoh(argv[i], (char *) pkt_filterp->u.pattern.mask_and_pattern));

	if (NULL == argv[++i]) {
		DHD_ERROR(("Pattern not provided\n"));
		goto fail;
	}

	/* Parse pattern filter pattern. */
	pattern_size =
		htod32(wl_pattern_atoh(argv[i],
	         (char *) &pkt_filterp->u.pattern.mask_and_pattern[mask_size]));

	if (mask_size != pattern_size) {
		DHD_ERROR(("Mask and pattern not the same size\n"));
		goto fail;
	}

	pkt_filter.u.pattern.size_bytes = mask_size;
	buf_len += WL_PKT_FILTER_FIXED_LEN;
	buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + 2 * mask_size);

	/* Keep-alive attributes are set in local	variable (keep_alive_pkt), and
	** then memcpy'ed into buffer (keep_alive_pktp) since there is no
	** guarantee that the buffer is properly aligned.
	*/
	memcpy((char *)pkt_filterp,
	       &pkt_filter,
	       WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_FIXED_LEN);

	rc = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, buf_len);
	rc = rc >= 0 ? 0 : rc;

	if (rc)
		DHD_TRACE(("%s: failed to add pktfilter %s, retcode = %d\n",
		__FUNCTION__, arg, rc));
	else
		DHD_TRACE(("%s: successfully added pktfilter %s\n",
		__FUNCTION__, arg));

fail:
	if (arg_org)
		MFREE(dhd->osh, arg_org, strlen(arg) + 1);

	if (buf)
		MFREE(dhd->osh, buf, BUF_SIZE);
}
#endif

#ifdef ARP_OFFLOAD_SUPPORT
void
dhd_arp_offload_set(dhd_pub_t * dhd, int arp_mode)
{
	char iovbuf[32];
	int retcode;

	bcm_mkiovar("arp_ol", (char *)&arp_mode, 4, iovbuf, sizeof(iovbuf));
	retcode = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	retcode = retcode >= 0 ? 0 : retcode;
	if (retcode)
		DHD_TRACE(("%s: failed to set ARP offload mode to 0x%x, retcode = %d\n",
		__FUNCTION__, arp_mode, retcode));
	else
		DHD_TRACE(("%s: successfully set ARP offload mode to 0x%x\n",
		__FUNCTION__, arp_mode));
}

void
dhd_arp_offload_enable(dhd_pub_t * dhd, int arp_enable)
{
	char iovbuf[32];
	int retcode;

	bcm_mkiovar("arpoe", (char *)&arp_enable, 4, iovbuf, sizeof(iovbuf));
	retcode = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	retcode = retcode >= 0 ? 0 : retcode;
	if (retcode)
		DHD_TRACE(("%s: failed to enabe ARP offload to %d, retcode = %d\n",
		__FUNCTION__, arp_enable, retcode));
	else
		DHD_TRACE(("%s: successfully enabed ARP offload to %d\n",
		__FUNCTION__, arp_enable));
}
#endif

int
dhd_preinit_ioctls(dhd_pub_t *dhd)
{
	char iovbuf[WL_EVENTING_MASK_LEN + 12];	/*  Room for "event_msgs" + '\0' + bitvec  */
	uint up = 0;
	char buf[128], *ptr;
	uint power_mode = PM_FAST;
	uint32 dongle_align = DHD_SDALIGN;
	uint32 glom = 0;
	uint bcn_timeout = 4;
	int scan_assoc_time = 40;
	int scan_unassoc_time = 40;
	uint32 listen_interval = LISTEN_INTERVAL; /* Default Listen Interval in Beacons */
#if defined(SOFTAP)
	uint dtim = 1;
#endif
	int ret = 0;
#ifdef GET_CUSTOM_MAC_ENABLE
	struct ether_addr ea_addr;
#endif /* GET_CUSTOM_MAC_ENABLE */

	dhd_os_proto_block(dhd);

#ifdef GET_CUSTOM_MAC_ENABLE
	/*
	** Read MAC address from external customer place
	** NOTE that default mac address has to be present in otp or nvram file
	** to bring up firmware but unique per board mac address maybe provided
	** by customer code
	*/
	ret = dhd_custom_get_mac_address(ea_addr.octet);
	if (!ret) {
		bcm_mkiovar("cur_etheraddr", (void *)&ea_addr, ETHER_ADDR_LEN, buf, sizeof(buf));
		ret = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, sizeof(buf));
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
		} else
			memcpy(dhd->mac.octet, (void *)&ea_addr, ETHER_ADDR_LEN);
	}
#endif /* GET_CUSTOM_MAC_ENABLE */

#ifdef SET_RANDOM_MAC_SOFTAP
	if (strstr(fw_path, "apsta") != NULL) {
		uint rand_mac;

		srandom32((uint)jiffies);
		rand_mac = random32();
		iovbuf[0] = 0x02;              /* locally administered bit */
		iovbuf[1] = 0x1A;
		iovbuf[2] = 0x11;
		iovbuf[3] = (unsigned char)(rand_mac & 0x0F) | 0xF0;
		iovbuf[4] = (unsigned char)(rand_mac >> 8);
		iovbuf[5] = (unsigned char)(rand_mac >> 16);

		printk("Broadcom Dongle Host Driver mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
			iovbuf[0], iovbuf[1], iovbuf[2], iovbuf[3], iovbuf[4], iovbuf[5]);

		bcm_mkiovar("cur_etheraddr", (void *)iovbuf, ETHER_ADDR_LEN, buf, sizeof(buf));
		ret = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, sizeof(buf));
		if (ret < 0) {
			DHD_ERROR(("%s: can't set MAC address , error=%d\n", __FUNCTION__, ret));
		} else
			memcpy(dhd->mac.octet, iovbuf, ETHER_ADDR_LEN);
	}
#endif /* SET_RANDOM_MAC_SOFTAP */

	/* Set Country code */
	if (dhd->dhd_cspec.ccode[0] != 0) {
		bcm_mkiovar("country", (char *)&dhd->dhd_cspec, \
			sizeof(wl_country_t), iovbuf, sizeof(iovbuf));
		if ((ret = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf))) < 0) {
			DHD_ERROR(("%s: country code setting failed\n", __FUNCTION__));
		}
	}

	/* Set Listen Interval */
	bcm_mkiovar("assoc_listen", (char *)&listen_interval, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf))) < 0)
		DHD_ERROR(("%s assoc_listen failed %d\n", __FUNCTION__, ret));

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	ptr = buf;
	bcm_mkiovar("ver", 0, 0, buf, sizeof(buf));
	dhdcdc_query_ioctl(dhd, 0, WLC_GET_VAR, buf, sizeof(buf));
	bcmstrtok(&ptr, "\n", 0);
	/* Print fw version info */
	DHD_ERROR(("Firmware version = %s\n", buf));

	/* Set PowerSave mode */
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_PM, (char *)&power_mode, sizeof(power_mode));

	/* Match Host and Dongle rx alignment */
	bcm_mkiovar("bus:txglomalign", (char *)&dongle_align, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	/* disable glom option per default */
	bcm_mkiovar("bus:txglom", (char *)&glom, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	/* Setup timeout if Beacons are lost and roam is off to report link down */
	bcm_mkiovar("bcn_timeout", (char *)&bcn_timeout, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	/* Enable/Disable build-in roaming to allowed ext supplicant to take of romaing */
	bcm_mkiovar("roam_off", (char *)&dhd_roam, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

#if defined(SOFTAP)
	if (ap_fw_loaded == TRUE) {
		dhdcdc_set_ioctl(dhd, 0, WLC_SET_DTIMPRD, (char *)&dtim, sizeof(dtim));
	}
#endif

	if (dhd_roam == 0)
	{
		/* set internal roaming roaming parameters */
		int roam_scan_period = 30; /* in sec */
		int roam_fullscan_period = 120; /* in sec */
		int roam_trigger = -85;
		int roam_delta = 15;
		int band;
		int band_temp_set = WLC_BAND_2G;

		if (dhdcdc_set_ioctl(dhd, 0, WLC_SET_ROAM_SCAN_PERIOD, \
			(char *)&roam_scan_period, sizeof(roam_scan_period)) < 0)
			DHD_ERROR(("%s: roam scan setup failed\n", __FUNCTION__));

		bcm_mkiovar("fullroamperiod", (char *)&roam_fullscan_period, \
					 4, iovbuf, sizeof(iovbuf));
		if (dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, \
			iovbuf, sizeof(iovbuf)) < 0)
			DHD_ERROR(("%s: roam fullscan setup failed\n", __FUNCTION__));

		if (dhdcdc_query_ioctl(dhd, 0, WLC_GET_BAND, \
				(char *)&band, sizeof(band)) < 0)
			DHD_ERROR(("%s: roam delta setting failed\n", __FUNCTION__));
		else {
			if ((band == WLC_BAND_AUTO) || (band == WLC_BAND_ALL))
			{
				/* temp set band to insert new roams values */
				if (dhdcdc_set_ioctl(dhd, 0, WLC_SET_BAND, \
					(char *)&band_temp_set, sizeof(band_temp_set)) < 0)
					DHD_ERROR(("%s: local band seting failed\n", __FUNCTION__));
			}
			if (dhdcdc_set_ioctl(dhd, 0, WLC_SET_ROAM_DELTA, \
				(char *)&roam_delta, sizeof(roam_delta)) < 0)
				DHD_ERROR(("%s: roam delta setting failed\n", __FUNCTION__));

			if (dhdcdc_set_ioctl(dhd, 0, WLC_SET_ROAM_TRIGGER, \
				(char *)&roam_trigger, sizeof(roam_trigger)) < 0)
				DHD_ERROR(("%s: roam trigger setting failed\n", __FUNCTION__));

			/* Restore original band settinngs */
			if (dhdcdc_set_ioctl(dhd, 0, WLC_SET_BAND, \
				(char *)&band, sizeof(band)) < 0)
				DHD_ERROR(("%s: Original band restore failed\n", __FUNCTION__));
		}
	}

	/* Force STA UP */
	if (dhd_radio_up)
		dhdcdc_set_ioctl(dhd, 0, WLC_UP, (char *)&up, sizeof(up));

	/* Setup event_msgs */
	bcm_mkiovar("event_msgs", dhd->eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	dhdcdc_set_ioctl(dhd, 0, WLC_SET_SCAN_CHANNEL_TIME, (char *)&scan_assoc_time,
		sizeof(scan_assoc_time));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_SCAN_UNASSOC_TIME, (char *)&scan_unassoc_time,
		sizeof(scan_unassoc_time));

#ifdef ARP_OFFLOAD_SUPPORT
	/* Set and enable ARP offload feature */
	if (dhd_arp_enable)
		dhd_arp_offload_set(dhd, dhd_arp_mode);
	dhd_arp_offload_enable(dhd, dhd_arp_enable);
#endif /* ARP_OFFLOAD_SUPPORT */

#ifdef PKT_FILTER_SUPPORT
	{
		int i;
		/* Set up pkt filter */
		if (dhd_pkt_filter_enable) {
			for (i = 0; i < dhd->pktfilter_count; i++) {
				dhd_pktfilter_offload_set(dhd, dhd->pktfilter[i]);
				dhd_pktfilter_offload_enable(dhd, dhd->pktfilter[i],
					dhd_pkt_filter_init, dhd_master_mode);
			}
		}
	}
#endif /* PKT_FILTER_SUPPORT */

#if defined(KEEP_ALIVE)
	{
	/* Set Keep Alive : be sure to use FW with -keepalive */
	int res;

	if (ap_fw_loaded == FALSE) {
		if ((res = dhd_keep_alive_onoff(dhd, 1)) < 0)
			DHD_ERROR(("%s set keeplive failed %d\n", \
		__FUNCTION__, res));
		}
	}
#endif

	dhd_os_proto_unblock(dhd);

	return 0;
}

#ifdef SIMPLE_ISCAN

uint iscan_thread_id;
iscan_buf_t * iscan_chain = 0;

iscan_buf_t *
dhd_iscan_allocate_buf(dhd_pub_t *dhd, iscan_buf_t **iscanbuf)
{
	iscan_buf_t *iscanbuf_alloc = 0;
	iscan_buf_t *iscanbuf_head;

	dhd_iscan_lock();

	iscanbuf_alloc = (iscan_buf_t*)MALLOC(dhd->osh, sizeof(iscan_buf_t));
	if (iscanbuf_alloc == NULL)
		goto fail;

	iscanbuf_alloc->next = NULL;
	iscanbuf_head = *iscanbuf;

	DHD_ISCAN(("%s: addr of allocated node = 0x%X"
		   "addr of iscanbuf_head = 0x%X dhd = 0x%X\n",
		   __FUNCTION__, iscanbuf_alloc, iscanbuf_head, dhd));

	if (iscanbuf_head == NULL) {
		*iscanbuf = iscanbuf_alloc;
		DHD_ISCAN(("%s: Head is allocated\n", __FUNCTION__));
		goto fail;
	}

	while (iscanbuf_head->next)
		iscanbuf_head = iscanbuf_head->next;

	iscanbuf_head->next = iscanbuf_alloc;

fail:
	dhd_iscan_unlock();
	return iscanbuf_alloc;
}

void
dhd_iscan_free_buf(void *dhdp, iscan_buf_t *iscan_delete)
{
	iscan_buf_t *iscanbuf_free = 0;
	iscan_buf_t *iscanbuf_prv = 0;
	iscan_buf_t *iscanbuf_cur = iscan_chain;
	dhd_pub_t *dhd = dhd_bus_pub(dhdp);

	dhd_iscan_lock();
	/* If iscan_delete is null then delete the entire
	 * chain or else delete specific one provided
	 */
	if (!iscan_delete) {
		while (iscanbuf_cur) {
			iscanbuf_free = iscanbuf_cur;
			iscanbuf_cur = iscanbuf_cur->next;
			iscanbuf_free->next = 0;
			MFREE(dhd->osh, iscanbuf_free, sizeof(iscan_buf_t));
		}
		iscan_chain = 0;
	} else {
		while (iscanbuf_cur) {
			if (iscanbuf_cur == iscan_delete)
				break;
			iscanbuf_prv = iscanbuf_cur;
			iscanbuf_cur = iscanbuf_cur->next;
		}
		if (iscanbuf_prv)
			iscanbuf_prv->next = iscan_delete->next;

		iscan_delete->next = 0;
		MFREE(dhd->osh, iscan_delete, sizeof(iscan_buf_t));

		if (!iscanbuf_prv)
			iscan_chain = 0;
	}
	dhd_iscan_unlock();
}

iscan_buf_t *
dhd_iscan_result_buf(void)
{
	return iscan_chain;
}



/*
* print scan cache
* print partial iscan_skip list differently
*/
int
dhd_iscan_print_cache(iscan_buf_t *iscan_skip)
{
	int i = 0, l = 0;
	iscan_buf_t *iscan_cur;
	wl_iscan_results_t *list;
	wl_scan_results_t *results;
	wl_bss_info_t UNALIGNED *bi;

	dhd_iscan_lock();

	iscan_cur = dhd_iscan_result_buf();

	while (iscan_cur) {
		list = (wl_iscan_results_t *)iscan_cur->iscan_buf;
		if (!list)
			break;

		results = (wl_scan_results_t *)&list->results;
		if (!results)
			break;

		if (results->version != WL_BSS_INFO_VERSION) {
			DHD_ISCAN(("%s: results->version %d != WL_BSS_INFO_VERSION\n",
				__FUNCTION__, results->version));
			goto done;
		}

		bi = results->bss_info;
		for (i = 0; i < results->count; i++) {
			if (!bi)
				break;

			DHD_ISCAN(("%s[%2.2d:%2.2d] %X:%X:%X:%X:%X:%X\n",
				iscan_cur != iscan_skip?"BSS":"bss", l, i,
				bi->BSSID.octet[0], bi->BSSID.octet[1], bi->BSSID.octet[2],
				bi->BSSID.octet[3], bi->BSSID.octet[4], bi->BSSID.octet[5]));

			bi = (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length));
		}
		iscan_cur = iscan_cur->next;
		l++;
	}

done:
	dhd_iscan_unlock();
	return 0;
}

/*
* delete disappeared AP from specific scan cache but skip partial list in iscan_skip
*/
int
dhd_iscan_delete_bss(void *dhdp, void *addr, iscan_buf_t *iscan_skip)
{
	int i = 0, j = 0, l = 0;
	iscan_buf_t *iscan_cur;
	wl_iscan_results_t *list;
	wl_scan_results_t *results;
	wl_bss_info_t UNALIGNED *bi, *bi_new, *bi_next;

	uchar *s_addr = addr;

	dhd_iscan_lock();
	DHD_ISCAN(("%s: BSS to remove %X:%X:%X:%X:%X:%X\n",
		__FUNCTION__, s_addr[0], s_addr[1], s_addr[2],
		s_addr[3], s_addr[4], s_addr[5]));

	iscan_cur = dhd_iscan_result_buf();

	while (iscan_cur) {
		if (iscan_cur != iscan_skip) {
			list = (wl_iscan_results_t *)iscan_cur->iscan_buf;
			if (!list)
				break;

			results = (wl_scan_results_t *)&list->results;
			if (!results)
				break;

			if (results->version != WL_BSS_INFO_VERSION) {
				DHD_ERROR(("%s: results->version %d != WL_BSS_INFO_VERSION\n",
				__FUNCTION__, results->version));
				goto done;
			}

			bi = results->bss_info;
			for (i = 0; i < results->count; i++) {
				if (!bi)
					break;

				if (!memcmp(bi->BSSID.octet, addr, ETHER_ADDR_LEN)) {
					DHD_ISCAN(("%s: Del BSS[%2.2d:%2.2d] %X:%X:%X:%X:%X:%X\n",
					__FUNCTION__, l, i, bi->BSSID.octet[0],
					bi->BSSID.octet[1], bi->BSSID.octet[2],
					bi->BSSID.octet[3], bi->BSSID.octet[4],
					bi->BSSID.octet[5]));

					bi_new = bi;
					bi = (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length));
/*
					if(bi && bi_new) {
						bcopy(bi, bi_new, results->buflen -
						dtoh32(bi_new->length));
						results->buflen -= dtoh32(bi_new->length);
					}
*/
					results->buflen -= dtoh32(bi_new->length);
					results->count--;

					for (j = i; j < results->count; j++) {
						if (bi && bi_new) {
							DHD_ISCAN(("%s: Moved up BSS[%2.2d:%2.2d]"
							"%X:%X:%X:%X:%X:%X\n",
							__FUNCTION__, l, j, bi->BSSID.octet[0],
							bi->BSSID.octet[1], bi->BSSID.octet[2],
							bi->BSSID.octet[3], bi->BSSID.octet[4],
							bi->BSSID.octet[5]));

							bi_next = (wl_bss_info_t *)((uintptr)bi +
								dtoh32(bi->length));
							bcopy(bi, bi_new, dtoh32(bi->length));
							bi_new = (wl_bss_info_t *)((uintptr)bi_new +
								dtoh32(bi_new->length));
							bi = bi_next;
						}
					}

					if (results->count == 0) {
						/* Prune now empty partial scan list */
						dhd_iscan_free_buf(dhdp, iscan_cur);
						goto done;
					}
					break;
				}
				bi = (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length));
			}
		}
		iscan_cur = iscan_cur->next;
		l++;
	}

done:
	dhd_iscan_unlock();
	return 0;
}

int
dhd_iscan_remove_duplicates(void * dhdp, iscan_buf_t *iscan_cur)
{
	int i = 0;
	wl_iscan_results_t *list;
	wl_scan_results_t *results;
	wl_bss_info_t UNALIGNED *bi, *bi_new, *bi_next;

	dhd_iscan_lock();

	DHD_ISCAN(("%s: Scan cache before delete\n",
		__FUNCTION__));
	dhd_iscan_print_cache(iscan_cur);

	if (!iscan_cur)
		goto done;

	list = (wl_iscan_results_t *)iscan_cur->iscan_buf;
	if (!list)
		goto done;

	results = (wl_scan_results_t *)&list->results;
	if (!results)
		goto done;

	if (results->version != WL_BSS_INFO_VERSION) {
		DHD_ERROR(("%s: results->version %d != WL_BSS_INFO_VERSION\n",
			__FUNCTION__, results->version));
		goto done;
	}

	bi = results->bss_info;
	for (i = 0; i < results->count; i++) {
		if (!bi)
			break;

		DHD_ISCAN(("%s: Find dups for BSS[%2.2d] %X:%X:%X:%X:%X:%X\n",
			__FUNCTION__, i, bi->BSSID.octet[0], bi->BSSID.octet[1], bi->BSSID.octet[2],
			bi->BSSID.octet[3], bi->BSSID.octet[4], bi->BSSID.octet[5]));

		dhd_iscan_delete_bss(dhdp, bi->BSSID.octet, iscan_cur);

		bi = (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length));
	}

done:
	DHD_ISCAN(("%s: Scan cache after delete\n", __FUNCTION__));
	dhd_iscan_print_cache(iscan_cur);
	dhd_iscan_unlock();
	return 0;
}

void
dhd_iscan_ind_scan_confirm(void *dhdp, bool status)
{

	dhd_ind_scan_confirm(dhdp, status);
}

int
dhd_iscan_request(void * dhdp, uint16 action)
{
	int rc;
	wl_iscan_params_t params;
	dhd_pub_t *dhd = dhd_bus_pub(dhdp);
	char buf[WLC_IOCTL_SMLEN];


	memset(&params, 0, sizeof(wl_iscan_params_t));
	memcpy(&params.params.bssid, &ether_bcast, ETHER_ADDR_LEN);

	params.params.bss_type = DOT11_BSSTYPE_ANY;
	params.params.scan_type = DOT11_SCANTYPE_ACTIVE;

	params.params.nprobes = htod32(-1);
	params.params.active_time = htod32(-1);
	params.params.passive_time = htod32(-1);
	params.params.home_time = htod32(-1);
	params.params.channel_num = htod32(0);

	params.version = htod32(ISCAN_REQ_VERSION);
	params.action = htod16(action);
	params.scan_duration = htod16(0);

	bcm_mkiovar("iscan", (char *)&params, sizeof(wl_iscan_params_t), buf, WLC_IOCTL_SMLEN);
	rc = dhd_wl_ioctl(dhdp, WLC_SET_VAR, buf, WLC_IOCTL_SMLEN);

	return rc;
}

static int
dhd_iscan_get_partial_result(void *dhdp, uint *scan_count)
{
	wl_iscan_results_t *list_buf;
	wl_iscan_results_t list;
	wl_scan_results_t *results;
	iscan_buf_t *iscan_cur;
	int status = -1;
	dhd_pub_t *dhd = dhd_bus_pub(dhdp);
	int rc;


	iscan_cur = dhd_iscan_allocate_buf(dhd, &iscan_chain);
	if (!iscan_cur) {
		DHD_ERROR(("%s: Failed to allocate node\n", __FUNCTION__));
		dhd_iscan_free_buf(dhdp, 0);
		dhd_iscan_request(dhdp, WL_SCAN_ACTION_ABORT);
		goto fail;
	}

	dhd_iscan_lock();

	memset(iscan_cur->iscan_buf, 0, WLC_IW_ISCAN_MAXLEN);
	list_buf = (wl_iscan_results_t*)iscan_cur->iscan_buf;
	results = &list_buf->results;
	results->buflen = WL_ISCAN_RESULTS_FIXED_SIZE;
	results->version = 0;
	results->count = 0;

	memset(&list, 0, sizeof(list));
	list.results.buflen = htod32(WLC_IW_ISCAN_MAXLEN);
	bcm_mkiovar("iscanresults", (char *)&list, WL_ISCAN_RESULTS_FIXED_SIZE,
		iscan_cur->iscan_buf, WLC_IW_ISCAN_MAXLEN);
	rc = dhd_wl_ioctl(dhdp, WLC_GET_VAR, iscan_cur->iscan_buf, WLC_IW_ISCAN_MAXLEN);

	results->buflen = dtoh32(results->buflen);
	results->version = dtoh32(results->version);
	*scan_count = results->count = dtoh32(results->count);
	status = dtoh32(list_buf->status);

	dhd_iscan_unlock();

	if (!(*scan_count))
		dhd_iscan_free_buf(dhdp, iscan_cur);
	else
		dhd_iscan_remove_duplicates(dhdp, iscan_cur);


fail:
	return status;
}

#endif

/* Function to estimate possible DTIM_SKIP value */
int dhd_get_dtim_skip(dhd_pub_t *dhd)
{
	int bcn_li_dtim;
	char buf[128];
	int ret;
	int dtim_assoc = 0;

	if ((dhd->dtim_skip == 0) || (dhd->dtim_skip == 1))
		bcn_li_dtim = 3;
	else
		bcn_li_dtim = dhd->dtim_skip;

	/* Read DTIM value if associated */
	memset(buf, 0, sizeof(buf));
	bcm_mkiovar("dtim_assoc", 0, 0, buf, sizeof(buf));
	if ((ret = dhdcdc_query_ioctl(dhd, 0, WLC_GET_VAR, buf, sizeof(buf))) < 0) {
		DHD_ERROR(("%s failed code %d\n", __FUNCTION__, ret));
		bcn_li_dtim = 1;
		goto exit;
	}
	else
		dtim_assoc = dtoh32(*(int *)buf);

	DHD_ERROR(("%s bcn_li_dtim=%d DTIM=%d Listen=%d\n", \
			 __FUNCTION__, bcn_li_dtim, dtim_assoc, LISTEN_INTERVAL));

	/* if not assocated just eixt */
	if (dtim_assoc == 0) {
		goto exit;
	}

	/* check if sta listen interval fits into AP dtim */
	if (dtim_assoc > LISTEN_INTERVAL) {
		/* AP DTIM to big for our Listen Interval : no dtim skiping */
		bcn_li_dtim = 1;
		DHD_ERROR(("%s DTIM=%d > Listen=%d : too big ...\n", \
				 __FUNCTION__, dtim_assoc, LISTEN_INTERVAL));
		goto exit;
	}

	if ((bcn_li_dtim * dtim_assoc) > LISTEN_INTERVAL) {
		/* Round up dtim_skip to fit into STAs Listen Interval */
		bcn_li_dtim = (int)(LISTEN_INTERVAL / dtim_assoc);
		DHD_TRACE(("%s agjust dtim_skip as %d\n", __FUNCTION__, bcn_li_dtim));
	}

exit:
	return bcn_li_dtim;
}

#ifdef PNO_SUPPORT
int dhd_pno_clean(dhd_pub_t *dhd)
{
	char iovbuf[128];
	int pfn_enabled = 0;
	int iov_len = 0;
	int ret;

	/* Disable pfn */
	iov_len = bcm_mkiovar("pfn", (char *)&pfn_enabled, 4, iovbuf, sizeof(iovbuf));
	if ((ret = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf))) >= 0) {
		/* clear pfn */
		iov_len = bcm_mkiovar("pfnclear", 0, 0, iovbuf, sizeof(iovbuf));
		if (iov_len) {
			if ((ret = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, iov_len)) < 0) {
				DHD_ERROR(("%s failed code %d\n", __FUNCTION__, ret));
			}
		}
		else {
			ret = -1;
			DHD_ERROR(("%s failed code %d\n", __FUNCTION__, iov_len));
		}
	}
	else
		DHD_ERROR(("%s failed code %d\n", __FUNCTION__, ret));

	return ret;
}

int dhd_pno_enable(dhd_pub_t *dhd, int pfn_enabled)
{
	char iovbuf[128];
	uint8 bssid[6];
	int ret = -1;

	if ((!dhd) && ((pfn_enabled != 0) || (pfn_enabled != 1))) {
		DHD_ERROR(("%s error exit\n", __FUNCTION__));
		return ret;
	}

	memset(iovbuf, 0, sizeof(iovbuf));

	/* Check if disassoc to enable pno */
	if ((pfn_enabled) && \
		((ret = dhdcdc_set_ioctl(dhd, 0, WLC_GET_BSSID, \
				 (char *)&bssid, ETHER_ADDR_LEN)) == BCME_NOTASSOCIATED)) {
			DHD_TRACE(("%s pno enable called in disassoc mode\n", __FUNCTION__));
	}
	else {
			DHD_ERROR(("%s pno enable called in assoc mode ret=%d\n", \
										__FUNCTION__, ret));
			return ret;
	}

	/* Enable/disable PNO */
	if ((ret = bcm_mkiovar("pfn", (char *)&pfn_enabled, 4, iovbuf, sizeof(iovbuf))) > 0) {
		if ((ret = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf))) < 0) {
			DHD_ERROR(("%s failed for error=%d\n", __FUNCTION__, ret));
			return ret;
		}
		else {
			dhd->pno_enable = pfn_enabled;
			DHD_TRACE(("%s set pno as %d\n", __FUNCTION__, dhd->pno_enable));
		}
	}
	else DHD_ERROR(("%s failed err=%d\n", __FUNCTION__, ret));

	return ret;
}

/* Function to execute combined scan */
int
dhd_pno_set(dhd_pub_t *dhd, wlc_ssid_t* ssids_local, int nssid, ushort scan_fr, \
			int pno_repeat, int pno_freq_expo_max)
{
	int err = -1;
	char iovbuf[128];
	int k, i;
	wl_pfn_param_t pfn_param;
	wl_pfn_t	pfn_element;

	DHD_TRACE(("%s nssid=%d nchan=%d\n", __FUNCTION__, nssid, scan_fr));

	if ((!dhd) && (!ssids_local)) {
		DHD_ERROR(("%s error exit\n", __FUNCTION__));
		err = -1;
	}

	/* Check for broadcast ssid */
	for (k = 0; k < nssid; k++) {
		if (!ssids_local[k].SSID_len) {
			DHD_ERROR(("%d: Broadcast SSID is ilegal for PNO setting\n", k));
			return err;
		}
	}
/* #define  PNO_DUMP 1 */
#ifdef PNO_DUMP
	{
		int j;
		for (j = 0; j < nssid; j++) {
			DHD_ERROR(("%d: scan  for  %s size =%d\n", j,
				ssids_local[j].SSID, ssids_local[j].SSID_len));
		}
	}
#endif /* PNO_DUMP */

	/* clean up everything */
	if  ((err = dhd_pno_clean(dhd)) < 0) {
		DHD_ERROR(("%s failed error=%d\n", __FUNCTION__, err));
		return err;
	}
	memset(&pfn_param, 0, sizeof(pfn_param));
	memset(&pfn_element, 0, sizeof(pfn_element));

	/* set pfn parameters */
	pfn_param.version = htod32(PFN_VERSION);
	pfn_param.flags = htod16((PFN_LIST_ORDER << SORT_CRITERIA_BIT));

	/* check and set extra pno params */
	if ((pno_repeat != 0) || (pno_freq_expo_max != 0)) {
		pfn_param.flags |= htod16(ENABLE << ENABLE_ADAPTSCAN_BIT);
		pfn_param.repeat_scan = htod32(pno_repeat);
		pfn_param.max_freq_adjust = htod32(pno_freq_expo_max);
	}

	/* set up pno scan fr */
	if (scan_fr  != 0)
		pfn_param.scan_freq = htod32(scan_fr);

	if (pfn_param.scan_freq > PNO_SCAN_MAX_FW_SEC) {
		DHD_ERROR(("%s pno freq above %d sec\n", __FUNCTION__, PNO_SCAN_MAX_FW_SEC));
		return err;
	}
	if (pfn_param.scan_freq < PNO_SCAN_MIN_FW_SEC) {
		DHD_ERROR(("%s pno freq less %d sec\n", __FUNCTION__, PNO_SCAN_MIN_FW_SEC));
		return err;
	}

	bcm_mkiovar("pfn_set", (char *)&pfn_param, sizeof(pfn_param), iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	/* set all pfn ssid */
	for (i = 0; i < nssid; i++) {

		pfn_element.bss_type = htod32(DOT11_BSSTYPE_INFRASTRUCTURE);
		pfn_element.auth = (DOT11_OPEN_SYSTEM);
		pfn_element.infra = htod32(1);

		memcpy((char *)pfn_element.ssid.SSID, ssids_local[i].SSID, ssids_local[i].SSID_len);
		pfn_element.ssid.SSID_len = ssids_local[i].SSID_len;

		if ((err =
		bcm_mkiovar("pfn_add", (char *)&pfn_element,
			sizeof(pfn_element), iovbuf, sizeof(iovbuf))) > 0) {
			if ((err =
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf))) < 0) {
				DHD_ERROR(("%s failed for i=%d error=%d\n",
					__FUNCTION__, i, err));
				return err;
			}
			else
				DHD_ERROR(("%s set OK with PNO time=%d repeat=%d max_adjust=%d\n", \
					__FUNCTION__, pfn_param.scan_freq, \
					pfn_param.repeat_scan, pfn_param.max_freq_adjust));
		}
		else DHD_ERROR(("%s failed err=%d\n", __FUNCTION__, err));
	}

	/* Enable PNO */
	/* dhd_pno_enable(dhd, 1); */
	return err;
}

int dhd_pno_get_status(dhd_pub_t *dhd)
{
	int ret = -1;

	if (!dhd)
		return ret;
	else
		return (dhd->pno_enable);
}

#endif /* PNO_SUPPORT */

#if defined(KEEP_ALIVE)
int dhd_keep_alive_onoff(dhd_pub_t *dhd, int ka_on)
{
	char buf[256];
	char *buf_ptr = buf;
	wl_keep_alive_pkt_t keep_alive_pkt;
	char * str;
	int str_len, buf_len;
	int res = 0;
	int keep_alive_period = KEEP_ALIVE_PERIOD; /* in ms */

	DHD_TRACE(("%s: ka:%d\n", __FUNCTION__, ka_on));

	if (ka_on) { /* on suspend */
		keep_alive_pkt.period_msec = keep_alive_period;

	} else {
		/* on resume, turn off keep_alive packets  */
		keep_alive_pkt.period_msec = 0;
	}

	/* IOC var name  */
	str = "keep_alive";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[str_len] = '\0';
	buf_len = str_len + 1;

	/* set ptr to IOCTL payload after the var name */
	buf_ptr += buf_len; /* include term Z */

	/* copy Keep-alive attributes from local var keep_alive_pkt */
	str = NULL_PKT_STR;
	keep_alive_pkt.len_bytes = strlen(str);

	memcpy(buf_ptr, &keep_alive_pkt, WL_KEEP_ALIVE_FIXED_LEN);
	buf_ptr += WL_KEEP_ALIVE_FIXED_LEN;

	/* copy packet data */
	memcpy(buf_ptr, str, keep_alive_pkt.len_bytes);
	buf_len += (WL_KEEP_ALIVE_FIXED_LEN + keep_alive_pkt.len_bytes);

	res = dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, buf_len);
	return res;
}
#endif /* defined(KEEP_ALIVE) */

#if defined(CSCAN)

/* Androd ComboSCAN support */
/*
 *  data parsing from ComboScan tlv list
*/
int
wl_iw_parse_data_tlv(char** list_str, void *dst, int dst_size, const char token,
                     int input_size, int *bytes_left)
{
	char* str = *list_str;
	uint16 short_temp;
	uint32 int_temp;

	if ((list_str == NULL) || (*list_str == NULL) ||(bytes_left == NULL) || (*bytes_left < 0)) {
		DHD_ERROR(("%s error paramters\n", __FUNCTION__));
		return -1;
	}

	/* Clean all dest bytes */
	memset(dst, 0, dst_size);
	while (*bytes_left > 0) {

		if (str[0] != token) {
			DHD_TRACE(("%s NOT Type=%d get=%d left_parse=%d \n",
				__FUNCTION__, token, str[0], *bytes_left));
			return -1;
		}

		*bytes_left -= 1;
		str += 1;

		if (input_size == 1) {
			memcpy(dst, str, input_size);
		}
		else if (input_size == 2) {
			memcpy(dst, (char *)htod16(memcpy(&short_temp, str, input_size)),
				input_size);
		}
		else if (input_size == 4) {
			memcpy(dst, (char *)htod32(memcpy(&int_temp, str, input_size)),
				input_size);
		}

		*bytes_left -= input_size;
		str += input_size;
		*list_str = str;
		return 1;
	}
	return 1;
}

/*
 *  channel list parsing from cscan tlv list
*/
int
wl_iw_parse_channel_list_tlv(char** list_str, uint16* channel_list,
                             int channel_num, int *bytes_left)
{
	char* str = *list_str;
	int idx = 0;

	if ((list_str == NULL) || (*list_str == NULL) ||(bytes_left == NULL) || (*bytes_left < 0)) {
		DHD_ERROR(("%s error paramters\n", __FUNCTION__));
		return -1;
	}

	while (*bytes_left > 0) {

		if (str[0] != CSCAN_TLV_TYPE_CHANNEL_IE) {
			*list_str = str;
			DHD_TRACE(("End channel=%d left_parse=%d %d\n", idx, *bytes_left, str[0]));
			return idx;
		}
		/* Get proper CSCAN_TLV_TYPE_CHANNEL_IE */
		*bytes_left -= 1;
		str += 1;

		if (str[0] == 0) {
			/* All channels */
			channel_list[idx] = 0x0;
		}
		else {
			channel_list[idx] = (uint16)str[0];
			DHD_TRACE(("%s channel=%d \n", __FUNCTION__,  channel_list[idx]));
		}
		*bytes_left -= 1;
		str += 1;

		if (idx++ > 255) {
			DHD_ERROR(("%s Too many channels \n", __FUNCTION__));
			return -1;
		}
	}

	*list_str = str;
	return idx;
}

/*
 *  SSIDs list parsing from cscan tlv list
 */
int
wl_iw_parse_ssid_list_tlv(char** list_str, wlc_ssid_t* ssid, int max, int *bytes_left)
{
	char* str =  *list_str;
	int idx = 0;

	if ((list_str == NULL) || (*list_str == NULL) || (*bytes_left < 0)) {
		DHD_ERROR(("%s error paramters\n", __FUNCTION__));
		return -1;
	}

	while (*bytes_left > 0) {

		if (str[0] != CSCAN_TLV_TYPE_SSID_IE) {
			*list_str = str;
			DHD_TRACE(("nssid=%d left_parse=%d %d\n", idx, *bytes_left, str[0]));
			return idx;
		}

		/* Get proper CSCAN_TLV_TYPE_SSID_IE */
		*bytes_left -= 1;
		str += 1;

		if (str[0] == 0) {
			/* Broadcast SSID */
			ssid[idx].SSID_len = 0;
			memset((char*)ssid[idx].SSID, 0x0, DOT11_MAX_SSID_LEN);
			*bytes_left -= 1;
			str += 1;

			DHD_TRACE(("BROADCAST SCAN  left=%d\n", *bytes_left));
		}
		else if (str[0] <= DOT11_MAX_SSID_LEN) {
			/* Get proper SSID size */
			ssid[idx].SSID_len = str[0];
			*bytes_left -= 1;
			str += 1;

			/* Get SSID */
			if (ssid[idx].SSID_len > *bytes_left) {
				DHD_ERROR(("%s out of memory range len=%d but left=%d\n",
				__FUNCTION__, ssid[idx].SSID_len, *bytes_left));
				return -1;
			}

			memcpy((char*)ssid[idx].SSID, str, ssid[idx].SSID_len);

			*bytes_left -= ssid[idx].SSID_len;
			str += ssid[idx].SSID_len;

			DHD_TRACE(("%s :size=%d left=%d\n",
				(char*)ssid[idx].SSID, ssid[idx].SSID_len, *bytes_left));
		}
		else {
			DHD_ERROR(("### SSID size more that %d\n", str[0]));
			return -1;
		}

		if (idx++ >  max) {
			DHD_ERROR(("%s number of SSIDs more that %d\n", __FUNCTION__, idx));
			return -1;
		}
	}

	*list_str = str;
	return idx;
}

/* Parse a comma-separated list from list_str into ssid array, starting
 * at index idx.  Max specifies size of the ssid array.  Parses ssids
 * and returns updated idx; if idx >= max not all fit, the excess have
 * not been copied.  Returns -1 on empty string, or on ssid too long.
 */
int
wl_iw_parse_ssid_list(char** list_str, wlc_ssid_t* ssid, int idx, int max)
{
	char* str, *ptr;

	if ((list_str == NULL) || (*list_str == NULL))
		return -1;

	for (str = *list_str; str != NULL; str = ptr) {

		/* check for next TAG */
		if (!strncmp(str, GET_CHANNEL, strlen(GET_CHANNEL))) {
			*list_str	 = str + strlen(GET_CHANNEL);
			return idx;
		}

		if ((ptr = strchr(str, ',')) != NULL) {
			*ptr++ = '\0';
		}

		if (strlen(str) > DOT11_MAX_SSID_LEN) {
			DHD_ERROR(("ssid <%s> exceeds %d\n", str, DOT11_MAX_SSID_LEN));
			return -1;
		}

		if (strlen(str) == 0)
			ssid[idx].SSID_len = 0;

		if (idx < max) {
			strcpy((char*)ssid[idx].SSID, str);
			ssid[idx].SSID_len = strlen(str);
		}
		idx++;
	}
	return idx;
}

/*
 * Parse channel list from iwpriv CSCAN
 */
int
wl_iw_parse_channel_list(char** list_str, uint16* channel_list, int channel_num)
{
	int num;
	int val;
	char* str;
	char* endptr = NULL;

	if ((list_str == NULL)||(*list_str == NULL))
		return -1;

	str = *list_str;
	num = 0;
	while (strncmp(str, GET_NPROBE, strlen(GET_NPROBE))) {
		val = (int)strtoul(str, &endptr, 0);
		if (endptr == str) {
			printf("could not parse channel number starting at"
				" substring \"%s\" in list:\n%s\n",
				str, *list_str);
			return -1;
		}
		str = endptr + strspn(endptr, " ,");

		if (num == channel_num) {
			DHD_ERROR(("too many channels (more than %d) in channel list:\n%s\n",
				channel_num, *list_str));
			return -1;
		}

		channel_list[num++] = (uint16)val;
	}
	*list_str = str;
	return num;
}

#endif
