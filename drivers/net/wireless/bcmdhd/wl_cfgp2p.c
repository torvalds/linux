/*
 * Linux cfgp2p driver
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 * $Id: wl_cfgp2p.c 584240 2015-09-04 14:17:53Z $
 *
 */
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/if_arp.h>
#include <asm/uaccess.h>

#include <bcmutils.h>
#include <bcmendian.h>
#include <proto/ethernet.h>
#include <proto/802.11.h>
#include <net/rtnetlink.h>

#include <wl_cfg80211.h>
#include <wl_cfgp2p.h>
#include <wldev_common.h>
#include <wl_android.h>

#if defined(P2PONEINT)
#include <dngl_stats.h>
#include <dhd.h>
#endif

static s8 scanparambuf[WLC_IOCTL_SMLEN];
static s8 g_mgmt_ie_buf[2048];
static bool
wl_cfgp2p_has_ie(u8 *ie, u8 **tlvs, u32 *tlvs_len, const u8 *oui, u32 oui_len, u8 type);

static u32
wl_cfgp2p_vndr_ie(struct bcm_cfg80211 *cfg, u8 *iebuf, s32 pktflag,
            s8 *oui, s32 ie_id, s8 *data, s32 datalen, const s8* add_del_cmd);
static s32 wl_cfgp2p_cancel_listen(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	struct wireless_dev *wdev, bool notify);

#ifdef  P2PONEINT
void wl_cfg80211_scan_abort(struct bcm_cfg80211 *cfg);
chanspec_t wl_cfg80211_get_shared_freq(struct wiphy *wiphy);
s32 dhd_cfg80211_set_p2p_info(struct bcm_cfg80211 *cfg, int val);
int wl_cfgp2p_if_open(struct net_device *net);
int wl_cfgp2p_if_stop(struct net_device *net);
#endif

#if defined(WL_ENABLE_P2P_IF)
static int wl_cfgp2p_start_xmit(struct sk_buff *skb, struct net_device *ndev);
static int wl_cfgp2p_do_ioctl(struct net_device *net, struct ifreq *ifr, int cmd);
static int wl_cfgp2p_if_open(struct net_device *net);
static int wl_cfgp2p_if_stop(struct net_device *net);

static const struct net_device_ops wl_cfgp2p_if_ops = {
	.ndo_open       = wl_cfgp2p_if_open,
	.ndo_stop       = wl_cfgp2p_if_stop,
	.ndo_do_ioctl   = wl_cfgp2p_do_ioctl,
#ifndef  P2PONEINT
	.ndo_start_xmit = wl_cfgp2p_start_xmit,
#endif
};
#endif /* WL_ENABLE_P2P_IF */


bool wl_cfgp2p_is_pub_action(void *frame, u32 frame_len)
{
	wifi_p2p_pub_act_frame_t *pact_frm;

	if (frame == NULL)
		return false;
	pact_frm = (wifi_p2p_pub_act_frame_t *)frame;
	if (frame_len < sizeof(wifi_p2p_pub_act_frame_t) -1)
		return false;

	if (pact_frm->category == P2P_PUB_AF_CATEGORY &&
		pact_frm->action == P2P_PUB_AF_ACTION &&
		pact_frm->oui_type == P2P_VER &&
		memcmp(pact_frm->oui, P2P_OUI, sizeof(pact_frm->oui)) == 0) {
		return true;
	}

	return false;
}

bool wl_cfgp2p_is_p2p_action(void *frame, u32 frame_len)
{
	wifi_p2p_action_frame_t *act_frm;

	if (frame == NULL)
		return false;
	act_frm = (wifi_p2p_action_frame_t *)frame;
	if (frame_len < sizeof(wifi_p2p_action_frame_t) -1)
		return false;

	if (act_frm->category == P2P_AF_CATEGORY &&
		act_frm->type  == P2P_VER &&
		memcmp(act_frm->OUI, P2P_OUI, DOT11_OUI_LEN) == 0) {
		return true;
	}

	return false;
}

#define GAS_RESP_LEN		2
#define DOUBLE_TLV_BODY_OFF	4
#define GAS_RESP_OFFSET		4
#define GAS_CRESP_OFFSET	5

bool wl_cfgp2p_find_gas_subtype(u8 subtype, u8* data, u32 len)
{
	bcm_tlv_t *ie = (bcm_tlv_t *)data;
	u8 *frame = NULL;
	u16 id, flen;

	/* Skipped first ANQP Element, if frame has anqp elemnt */
	ie = bcm_parse_tlvs(ie, (int)len, DOT11_MNG_ADVERTISEMENT_ID);

	if (ie == NULL)
		return false;

	frame = (uint8 *)ie + ie->len + TLV_HDR_LEN + GAS_RESP_LEN;
	id = ((u16) (((frame)[1] << 8) | (frame)[0]));
	flen = ((u16) (((frame)[3] << 8) | (frame)[2]));

	/* If the contents match the OUI and the type */
	if (flen >= WFA_OUI_LEN + 1 &&
		id ==  P2PSD_GAS_NQP_INFOID &&
		!bcmp(&frame[DOUBLE_TLV_BODY_OFF], (const uint8*)WFA_OUI, WFA_OUI_LEN) &&
		subtype == frame[DOUBLE_TLV_BODY_OFF+WFA_OUI_LEN]) {
		return true;
	}

	return false;
}

bool wl_cfgp2p_is_gas_action(void *frame, u32 frame_len)
{

	wifi_p2psd_gas_pub_act_frame_t *sd_act_frm;

	if (frame == NULL)
		return false;

	sd_act_frm = (wifi_p2psd_gas_pub_act_frame_t *)frame;
	if (frame_len < (sizeof(wifi_p2psd_gas_pub_act_frame_t) - 1))
		return false;
	if (sd_act_frm->category != P2PSD_ACTION_CATEGORY)
		return false;

	if (sd_act_frm->action == P2PSD_ACTION_ID_GAS_IREQ ||
		sd_act_frm->action == P2PSD_ACTION_ID_GAS_IRESP ||
		sd_act_frm->action == P2PSD_ACTION_ID_GAS_CREQ ||
		sd_act_frm->action == P2PSD_ACTION_ID_GAS_CRESP)
		return true;
	else
		return false;
}

bool wl_cfgp2p_is_p2p_gas_action(void *frame, u32 frame_len)
{

	wifi_p2psd_gas_pub_act_frame_t *sd_act_frm;

	if (frame == NULL)
		return false;

	sd_act_frm = (wifi_p2psd_gas_pub_act_frame_t *)frame;
	if (frame_len < (sizeof(wifi_p2psd_gas_pub_act_frame_t) - 1))
		return false;
	if (sd_act_frm->category != P2PSD_ACTION_CATEGORY)
		return false;

	if (sd_act_frm->action == P2PSD_ACTION_ID_GAS_IREQ)
		return wl_cfgp2p_find_gas_subtype(P2PSD_GAS_OUI_SUBTYPE,
			(u8 *)sd_act_frm->query_data,
			frame_len);
	else
		return false;
}

void wl_cfgp2p_print_actframe(bool tx, void *frame, u32 frame_len, u32 channel)
{
	wifi_p2p_pub_act_frame_t *pact_frm;
	wifi_p2p_action_frame_t *act_frm;
	wifi_p2psd_gas_pub_act_frame_t *sd_act_frm;
	if (!frame || frame_len <= 2)
		return;

	if (wl_cfgp2p_is_pub_action(frame, frame_len)) {
		pact_frm = (wifi_p2p_pub_act_frame_t *)frame;
		switch (pact_frm->subtype) {
			case P2P_PAF_GON_REQ:
				CFGP2P_ACTION(("%s P2P Group Owner Negotiation Req Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_PAF_GON_RSP:
				CFGP2P_ACTION(("%s P2P Group Owner Negotiation Rsp Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_PAF_GON_CONF:
				CFGP2P_ACTION(("%s P2P Group Owner Negotiation Confirm Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_PAF_INVITE_REQ:
				CFGP2P_ACTION(("%s P2P Invitation Request  Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_PAF_INVITE_RSP:
				CFGP2P_ACTION(("%s P2P Invitation Response Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_PAF_DEVDIS_REQ:
				CFGP2P_ACTION(("%s P2P Device Discoverability Request Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_PAF_DEVDIS_RSP:
				CFGP2P_ACTION(("%s P2P Device Discoverability Response Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_PAF_PROVDIS_REQ:
				CFGP2P_ACTION(("%s P2P Provision Discovery Request Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_PAF_PROVDIS_RSP:
				CFGP2P_ACTION(("%s P2P Provision Discovery Response Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			default:
				CFGP2P_ACTION(("%s Unknown P2P Public Action Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));

		}

	} else if (wl_cfgp2p_is_p2p_action(frame, frame_len)) {
		act_frm = (wifi_p2p_action_frame_t *)frame;
		switch (act_frm->subtype) {
			case P2P_AF_NOTICE_OF_ABSENCE:
				CFGP2P_ACTION(("%s P2P Notice of Absence Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_AF_PRESENCE_REQ:
				CFGP2P_ACTION(("%s P2P Presence Request Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_AF_PRESENCE_RSP:
				CFGP2P_ACTION(("%s P2P Presence Response Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			case P2P_AF_GO_DISC_REQ:
				CFGP2P_ACTION(("%s P2P Discoverability Request Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
				break;
			default:
				CFGP2P_ACTION(("%s Unknown P2P Action Frame,"
					" channel=%d\n", (tx)? "TX": "RX", channel));
		}

	} else if (wl_cfgp2p_is_gas_action(frame, frame_len)) {
		sd_act_frm = (wifi_p2psd_gas_pub_act_frame_t *)frame;
		switch (sd_act_frm->action) {
			case P2PSD_ACTION_ID_GAS_IREQ:
				CFGP2P_ACTION(("%s P2P GAS Initial Request,"
					" channel=%d\n", (tx)? "TX" : "RX", channel));
				break;
			case P2PSD_ACTION_ID_GAS_IRESP:
				CFGP2P_ACTION(("%s P2P GAS Initial Response,"
					" channel=%d\n", (tx)? "TX" : "RX", channel));
				break;
			case P2PSD_ACTION_ID_GAS_CREQ:
				CFGP2P_ACTION(("%s P2P GAS Comback Request,"
					" channel=%d\n", (tx)? "TX" : "RX", channel));
				break;
			case P2PSD_ACTION_ID_GAS_CRESP:
				CFGP2P_ACTION(("%s P2P GAS Comback Response,"
					" channel=%d\n", (tx)? "TX" : "RX", channel));
				break;
			default:
				CFGP2P_ACTION(("%s Unknown P2P GAS Frame,"
					" channel=%d\n", (tx)? "TX" : "RX", channel));
		}


	}
}

/*
 *  Initialize variables related to P2P
 *
 */
s32
wl_cfgp2p_init_priv(struct bcm_cfg80211 *cfg)
{
	if (!(cfg->p2p = kzalloc(sizeof(struct p2p_info), GFP_KERNEL))) {
		CFGP2P_ERR(("struct p2p_info allocation failed\n"));
		return -ENOMEM;
	}
#define INIT_IE(IE_TYPE, BSS_TYPE)		\
	do {							\
		memset(wl_to_p2p_bss_saved_ie(cfg, BSS_TYPE).p2p_ ## IE_TYPE ## _ie, 0, \
		   sizeof(wl_to_p2p_bss_saved_ie(cfg, BSS_TYPE).p2p_ ## IE_TYPE ## _ie)); \
		wl_to_p2p_bss_saved_ie(cfg, BSS_TYPE).p2p_ ## IE_TYPE ## _ie_len = 0; \
	} while (0);

	INIT_IE(probe_req, P2PAPI_BSSCFG_PRIMARY);
	INIT_IE(probe_res, P2PAPI_BSSCFG_PRIMARY);
	INIT_IE(assoc_req, P2PAPI_BSSCFG_PRIMARY);
	INIT_IE(assoc_res, P2PAPI_BSSCFG_PRIMARY);
	INIT_IE(beacon,    P2PAPI_BSSCFG_PRIMARY);
	INIT_IE(probe_req, P2PAPI_BSSCFG_DEVICE);
	INIT_IE(probe_res, P2PAPI_BSSCFG_DEVICE);
	INIT_IE(assoc_req, P2PAPI_BSSCFG_DEVICE);
	INIT_IE(assoc_res, P2PAPI_BSSCFG_DEVICE);
	INIT_IE(beacon,    P2PAPI_BSSCFG_DEVICE);
	INIT_IE(probe_req, P2PAPI_BSSCFG_CONNECTION);
	INIT_IE(probe_res, P2PAPI_BSSCFG_CONNECTION);
	INIT_IE(assoc_req, P2PAPI_BSSCFG_CONNECTION);
	INIT_IE(assoc_res, P2PAPI_BSSCFG_CONNECTION);
	INIT_IE(beacon,    P2PAPI_BSSCFG_CONNECTION);
#undef INIT_IE
	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY) = bcmcfg_to_prmry_ndev(cfg);
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_PRIMARY) = 0;
	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE) = NULL;
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = 0;
	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION) = NULL;
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION) = 0;
	return BCME_OK;

}
/*
 *  Deinitialize variables related to P2P
 *
 */
void
wl_cfgp2p_deinit_priv(struct bcm_cfg80211 *cfg)
{
	CFGP2P_DBG(("In\n"));
	if (cfg->p2p) {
		kfree(cfg->p2p);
		cfg->p2p = NULL;
	}
	cfg->p2p_supported = 0;
}
/*
 * Set P2P functions into firmware
 */
s32
wl_cfgp2p_set_firm_p2p(struct bcm_cfg80211 *cfg)
{
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	struct ether_addr null_eth_addr = { { 0, 0, 0, 0, 0, 0 } };
	s32 ret = BCME_OK;
	s32 val = 0;
	/* Do we have to check whether APSTA is enabled or not ? */
	ret = wldev_iovar_getint(ndev, "apsta", &val);
	if (ret < 0) {
		CFGP2P_ERR(("get apsta error %d\n", ret));
		return ret;
	}
	if (val == 0) {
		val = 1;
		ret = wldev_ioctl(ndev, WLC_DOWN, &val, sizeof(s32), true);
		if (ret < 0) {
			CFGP2P_ERR(("WLC_DOWN error %d\n", ret));
			return ret;
		}
		wldev_iovar_setint(ndev, "apsta", val);
		ret = wldev_ioctl(ndev, WLC_UP, &val, sizeof(s32), true);
		if (ret < 0) {
			CFGP2P_ERR(("WLC_UP error %d\n", ret));
			return ret;
		}
	}

	/* In case of COB type, firmware has default mac address
	 * After Initializing firmware, we have to set current mac address to
	 * firmware for P2P device address
	 */
	ret = wldev_iovar_setbuf_bsscfg(ndev, "p2p_da_override", &null_eth_addr,
		sizeof(null_eth_addr), cfg->ioctl_buf, WLC_IOCTL_MAXLEN, 0, &cfg->ioctl_buf_sync);
	if (ret && ret != BCME_UNSUPPORTED) {
		CFGP2P_ERR(("failed to update device address ret %d\n", ret));
	}
	return ret;
}

/* Create a new P2P BSS.
 * Parameters:
 * @mac      : MAC address of the BSS to create
 * @if_type  : interface type: WL_P2P_IF_GO or WL_P2P_IF_CLIENT
 * @chspec   : chspec to use if creating a GO BSS.
 * Returns 0 if success.
 */
s32
wl_cfgp2p_ifadd(struct bcm_cfg80211 *cfg, struct ether_addr *mac, u8 if_type,
            chanspec_t chspec)
{
	wl_p2p_if_t ifreq;
	s32 err;
	u32 scb_timeout = WL_SCB_TIMEOUT;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);

	ifreq.type = if_type;
	ifreq.chspec = chspec;
	memcpy(ifreq.addr.octet, mac->octet, sizeof(ifreq.addr.octet));

	CFGP2P_DBG(("---cfg p2p_ifadd "MACDBG" %s %u\n",
		MAC2STRDBG(ifreq.addr.octet),
		(if_type == WL_P2P_IF_GO) ? "go" : "client",
	        (chspec & WL_CHANSPEC_CHAN_MASK) >> WL_CHANSPEC_CHAN_SHIFT));

	err = wldev_iovar_setbuf(ndev, "p2p_ifadd", &ifreq, sizeof(ifreq),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);

	if (unlikely(err < 0))
		printk("'cfg p2p_ifadd' error %d\n", err);
	else if (if_type == WL_P2P_IF_GO) {
		err = wldev_ioctl(ndev, WLC_SET_SCB_TIMEOUT, &scb_timeout, sizeof(u32), true);
		if (unlikely(err < 0))
			printk("'cfg scb_timeout' error %d\n", err);
	}
	return err;
}

/* Disable a P2P BSS.
 * Parameters:
 * @mac      : MAC address of the BSS to disable
 * Returns 0 if success.
 */
s32
wl_cfgp2p_ifdisable(struct bcm_cfg80211 *cfg, struct ether_addr *mac)
{
	s32 ret;
	struct net_device *netdev = bcmcfg_to_prmry_ndev(cfg);

	CFGP2P_INFO(("------primary idx %d : cfg p2p_ifdis "MACDBG"\n",
		netdev->ifindex, MAC2STRDBG(mac->octet)));
	ret = wldev_iovar_setbuf(netdev, "p2p_ifdis", mac, sizeof(*mac),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (unlikely(ret < 0)) {
		printk("'cfg p2p_ifdis' error %d\n", ret);
	}
	return ret;
}

/* Delete a P2P BSS.
 * Parameters:
 * @mac      : MAC address of the BSS to delete
 * Returns 0 if success.
 */
s32
wl_cfgp2p_ifdel(struct bcm_cfg80211 *cfg, struct ether_addr *mac)
{
	s32 ret;
	struct net_device *netdev = bcmcfg_to_prmry_ndev(cfg);

	CFGP2P_INFO(("------primary idx %d : cfg p2p_ifdel "MACDBG"\n",
	    netdev->ifindex, MAC2STRDBG(mac->octet)));
	ret = wldev_iovar_setbuf(netdev, "p2p_ifdel", mac, sizeof(*mac),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (unlikely(ret < 0)) {
		printk("'cfg p2p_ifdel' error %d\n", ret);
	}
	return ret;
}

/* Change a P2P Role.
 * Parameters:
 * @mac      : MAC address of the BSS to change a role
 * Returns 0 if success.
 */
s32
wl_cfgp2p_ifchange(struct bcm_cfg80211 *cfg, struct ether_addr *mac, u8 if_type,
            chanspec_t chspec)
{
	wl_p2p_if_t ifreq;
	s32 err;
	u32 scb_timeout = WL_SCB_TIMEOUT;

	struct net_device *netdev =  wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION);

	ifreq.type = if_type;
	ifreq.chspec = chspec;
	memcpy(ifreq.addr.octet, mac->octet, sizeof(ifreq.addr.octet));

	CFGP2P_INFO(("---cfg p2p_ifchange "MACDBG" %s %u"
		" chanspec 0x%04x\n", MAC2STRDBG(ifreq.addr.octet),
		(if_type == WL_P2P_IF_GO) ? "go" : "client",
		(chspec & WL_CHANSPEC_CHAN_MASK) >> WL_CHANSPEC_CHAN_SHIFT,
		ifreq.chspec));

	err = wldev_iovar_setbuf(netdev, "p2p_ifupd", &ifreq, sizeof(ifreq),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);

	if (unlikely(err < 0)) {
		printk("'cfg p2p_ifupd' error %d\n", err);
	} else if (if_type == WL_P2P_IF_GO) {
		err = wldev_ioctl(netdev, WLC_SET_SCB_TIMEOUT, &scb_timeout, sizeof(u32), true);
		if (unlikely(err < 0))
			printk("'cfg scb_timeout' error %d\n", err);
	}
	return err;
}


/* Get the index of a created P2P BSS.
 * Parameters:
 * @mac      : MAC address of the created BSS
 * @index    : output: index of created BSS
 * Returns 0 if success.
 */
s32
wl_cfgp2p_ifidx(struct bcm_cfg80211 *cfg, struct ether_addr *mac, s32 *index)
{
	s32 ret;
	u8 getbuf[64];
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);

	CFGP2P_INFO(("---cfg p2p_if "MACDBG"\n", MAC2STRDBG(mac->octet)));

	ret = wldev_iovar_getbuf_bsscfg(dev, "p2p_if", mac, sizeof(*mac), getbuf,
		sizeof(getbuf), wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_PRIMARY), NULL);

	if (ret == 0) {
		memcpy(index, getbuf, sizeof(s32));
		CFGP2P_INFO(("---cfg p2p_if   ==> %d\n", *index));
	}

	return ret;
}

static s32
wl_cfgp2p_set_discovery(struct bcm_cfg80211 *cfg, s32 on)
{
	s32 ret = BCME_OK;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	CFGP2P_DBG(("enter\n"));

	ret = wldev_iovar_setint(ndev, "p2p_disc", on);

	if (unlikely(ret < 0)) {
		CFGP2P_ERR(("p2p_disc %d error %d\n", on, ret));
	}

	return ret;
}

/* Set the WL driver's P2P mode.
 * Parameters :
 * @mode      : is one of WL_P2P_DISC_ST_{SCAN,LISTEN,SEARCH}.
 * @channel   : the channel to listen
 * @listen_ms : the time (milli seconds) to wait
 * @bssidx    : bss index for BSSCFG
 * Returns 0 if success
 */

s32
wl_cfgp2p_set_p2p_mode(struct bcm_cfg80211 *cfg, u8 mode, u32 channel, u16 listen_ms, int bssidx)
{
	wl_p2p_disc_st_t discovery_mode;
	s32 ret;
	struct net_device *dev;
	CFGP2P_DBG(("enter\n"));

	if (unlikely(bssidx == WL_INVALID)) {
		CFGP2P_ERR((" %d index out of range\n", bssidx));
		return -1;
	}

	dev = wl_cfgp2p_find_ndev(cfg, bssidx);
	if (unlikely(dev == NULL)) {
		CFGP2P_ERR(("bssidx %d is not assigned\n", bssidx));
		return BCME_NOTFOUND;
	}

	/* Put the WL driver into P2P Listen Mode to respond to P2P probe reqs */
	discovery_mode.state = mode;
	discovery_mode.chspec = wl_ch_host_to_driver(channel);
	discovery_mode.dwell = listen_ms;
	ret = wldev_iovar_setbuf_bsscfg(dev, "p2p_state", &discovery_mode,
		sizeof(discovery_mode), cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
		bssidx, &cfg->ioctl_buf_sync);

	return ret;
}

/* Get the index of the P2P Discovery BSS */
static s32
wl_cfgp2p_get_disc_idx(struct bcm_cfg80211 *cfg, s32 *index)
{
	s32 ret;
	struct net_device *dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY);

	ret = wldev_iovar_getint(dev, "p2p_dev", index);
	CFGP2P_INFO(("p2p_dev bsscfg_idx=%d ret=%d\n", *index, ret));

	if (unlikely(ret <  0)) {
	    CFGP2P_ERR(("'p2p_dev' error %d\n", ret));
		return ret;
	}
	return ret;
}

s32
wl_cfgp2p_init_discovery(struct bcm_cfg80211 *cfg)
{

	s32 index = 0;
	s32 ret = BCME_OK;

	CFGP2P_DBG(("enter\n"));

	if (wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) != 0) {
		CFGP2P_ERR(("do nothing, already initialized\n"));
		return ret;
	}

	ret = wl_cfgp2p_set_discovery(cfg, 1);
	if (ret < 0) {
		CFGP2P_ERR(("set discover error\n"));
		return ret;
	}
	/* Enable P2P Discovery in the WL Driver */
	ret = wl_cfgp2p_get_disc_idx(cfg, &index);

	if (ret < 0) {
		return ret;
	}
	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE) =
	    wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY);
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = index;

	/* Set the initial discovery state to SCAN */
	ret = wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
		wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));

	if (unlikely(ret != 0)) {
		CFGP2P_ERR(("unable to set WL_P2P_DISC_ST_SCAN\n"));
		wl_cfgp2p_set_discovery(cfg, 0);
		wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = 0;
		wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE) = NULL;
		return 0;
	}
	return ret;
}

/* Deinitialize P2P Discovery
 * Parameters :
 * @cfg        : wl_private data
 * Returns 0 if succes
 */
static s32
wl_cfgp2p_deinit_discovery(struct bcm_cfg80211 *cfg)
{
	s32 ret = BCME_OK;
	CFGP2P_DBG(("enter\n"));

	if (wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) == 0) {
		CFGP2P_ERR(("do nothing, not initialized\n"));
		return -1;
	}
	/* Set the discovery state to SCAN */
	ret = wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
	            wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));
	/* Disable P2P discovery in the WL driver (deletes the discovery BSSCFG) */
	ret = wl_cfgp2p_set_discovery(cfg, 0);

	/* Clear our saved WPS and P2P IEs for the discovery BSS.  The driver
	 * deleted these IEs when wl_cfgp2p_set_discovery() deleted the discovery
	 * BSS.
	 */

	/* Clear the saved bsscfg index of the discovery BSSCFG to indicate we
	 * have no discovery BSS.
	 */
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = WL_INVALID;
	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE) = NULL;

	return ret;

}
/* Enable P2P Discovery
 * Parameters:
 * @cfg	: wl_private data
 * @ie  : probe request ie (WPS IE + P2P IE)
 * @ie_len   : probe request ie length
 * Returns 0 if success.
 */
s32
wl_cfgp2p_enable_discovery(struct bcm_cfg80211 *cfg, struct net_device *dev,
	const u8 *ie, u32 ie_len)
{
	s32 ret = BCME_OK;
	s32 bssidx;

	if (wl_get_p2p_status(cfg, DISCOVERY_ON)) {
		CFGP2P_INFO((" DISCOVERY is already initialized, we have nothing to do\n"));
		goto set_ie;
	}

	wl_set_p2p_status(cfg, DISCOVERY_ON);

	CFGP2P_DBG(("enter\n"));

	ret = wl_cfgp2p_init_discovery(cfg);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR((" init discovery error %d\n", ret));
		goto exit;
	}
	/* Set wsec to any non-zero value in the discovery bsscfg to ensure our
	 * P2P probe responses have the privacy bit set in the 802.11 WPA IE.
	 * Some peer devices may not initiate WPS with us if this bit is not set.
	 */
	ret = wldev_iovar_setint_bsscfg(wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE),
			"wsec", AES_ENABLED, wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));
	if (unlikely(ret < 0)) {
		CFGP2P_ERR((" wsec error %d\n", ret));
	}
set_ie:
	if (ie_len) {
		if (bcmcfg_to_prmry_ndev(cfg) == dev) {
			bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
		} else if (wl_cfgp2p_find_idx(cfg, dev, &bssidx) != BCME_OK) {
			WL_ERR(("Find p2p index from dev(%p) failed\n", dev));
			return BCME_ERROR;
		}

		ret = wl_cfgp2p_set_management_ie(cfg, dev,
			bssidx,
			VNDR_IE_PRBREQ_FLAG, ie, ie_len);

		if (unlikely(ret < 0)) {
			CFGP2P_ERR(("set probreq ie occurs error %d\n", ret));
			goto exit;
		}
	}
exit:
	return ret;
}

/* Disable P2P Discovery
 * Parameters:
 * @cfg       : wl_private_data
 * Returns 0 if success.
 */
s32
wl_cfgp2p_disable_discovery(struct bcm_cfg80211 *cfg)
{
	s32 ret = BCME_OK;
	CFGP2P_DBG((" enter\n"));
	wl_clr_p2p_status(cfg, DISCOVERY_ON);

	if (wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) == 0) {
		CFGP2P_ERR((" do nothing, not initialized\n"));
		goto exit;
	}

	ret = wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
	            wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));

	if (unlikely(ret < 0)) {

		CFGP2P_ERR(("unable to set WL_P2P_DISC_ST_SCAN\n"));
	}
	/* Do a scan abort to stop the driver's scan engine in case it is still
	 * waiting out an action frame tx dwell time.
	 */
	wl_clr_p2p_status(cfg, DISCOVERY_ON);
	ret = wl_cfgp2p_deinit_discovery(cfg);

exit:
	return ret;
}

s32
wl_cfgp2p_escan(struct bcm_cfg80211 *cfg, struct net_device *dev, u16 active,
	u32 num_chans, u16 *channels,
	s32 search_state, u16 action, u32 bssidx, struct ether_addr *tx_dst_addr,
	p2p_scan_purpose_t p2p_scan_purpose)
{
	s32 ret = BCME_OK;
	s32 memsize;
	s32 eparams_size;
	u32 i;
	s8 *memblk;
	wl_p2p_scan_t *p2p_params;
	wl_escan_params_t *eparams;
	wlc_ssid_t ssid;
	/* Scan parameters */
#define P2PAPI_SCAN_NPROBES 1
#define P2PAPI_SCAN_DWELL_TIME_MS 80
#define P2PAPI_SCAN_SOCIAL_DWELL_TIME_MS 40
#define P2PAPI_SCAN_HOME_TIME_MS 60
#define P2PAPI_SCAN_NPROBS_TIME_MS 30
#define P2PAPI_SCAN_AF_SEARCH_DWELL_TIME_MS 100

	struct net_device *pri_dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY);
	/* Allocate scan params which need space for 3 channels and 0 ssids */
	eparams_size = (WL_SCAN_PARAMS_FIXED_SIZE +
	    OFFSETOF(wl_escan_params_t, params)) +
		num_chans * sizeof(eparams->params.channel_list[0]);

	memsize = sizeof(wl_p2p_scan_t) + eparams_size;
	memblk = scanparambuf;
	if (memsize > sizeof(scanparambuf)) {
		CFGP2P_ERR((" scanpar buf too small (%u > %zu)\n",
		    memsize, sizeof(scanparambuf)));
		return -1;
	}
	memset(memblk, 0, memsize);
	memset(cfg->ioctl_buf, 0, WLC_IOCTL_MAXLEN);
	if (search_state == WL_P2P_DISC_ST_SEARCH) {
		/*
		 * If we in SEARCH STATE, we don't need to set SSID explictly
		 * because dongle use P2P WILDCARD internally by default
		 */
		wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SEARCH, 0, 0, bssidx);
		/* use null ssid */
		ssid.SSID_len = 0;
		memset(&ssid.SSID, 0, sizeof(ssid.SSID));
	} else if (search_state == WL_P2P_DISC_ST_SCAN) {
		/* SCAN STATE 802.11 SCAN
		 * WFD Supplicant has p2p_find command with (type=progressive, type= full)
		 * So if P2P_find command with type=progressive,
		 * we have to set ssid to P2P WILDCARD because
		 * we just do broadcast scan unless setting SSID
		 */
		wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0, bssidx);
		/* use wild card ssid */
		ssid.SSID_len = WL_P2P_WILDCARD_SSID_LEN;
		memset(&ssid.SSID, 0, sizeof(ssid.SSID));
		memcpy(&ssid.SSID, WL_P2P_WILDCARD_SSID, WL_P2P_WILDCARD_SSID_LEN);
	} else {
		CFGP2P_ERR((" invalid search state %d\n", search_state));
		return -1;
	}


	/* Fill in the P2P scan structure at the start of the iovar param block */
	p2p_params = (wl_p2p_scan_t*) memblk;
	p2p_params->type = 'E';
	/* Fill in the Scan structure that follows the P2P scan structure */
	eparams = (wl_escan_params_t*) (p2p_params + 1);
	eparams->params.bss_type = DOT11_BSSTYPE_ANY;
	if (active)
		eparams->params.scan_type = DOT11_SCANTYPE_ACTIVE;
	else
		eparams->params.scan_type = DOT11_SCANTYPE_PASSIVE;

	if (tx_dst_addr == NULL)
		memcpy(&eparams->params.bssid, &ether_bcast, ETHER_ADDR_LEN);
	else
		memcpy(&eparams->params.bssid, tx_dst_addr, ETHER_ADDR_LEN);

	if (ssid.SSID_len)
		memcpy(&eparams->params.ssid, &ssid, sizeof(wlc_ssid_t));

	eparams->params.home_time = htod32(P2PAPI_SCAN_HOME_TIME_MS);

	switch (p2p_scan_purpose) {
		case P2P_SCAN_SOCIAL_CHANNEL:
		eparams->params.active_time = htod32(P2PAPI_SCAN_SOCIAL_DWELL_TIME_MS);
			break;
		case P2P_SCAN_AFX_PEER_NORMAL:
		case P2P_SCAN_AFX_PEER_REDUCED:
		eparams->params.active_time = htod32(P2PAPI_SCAN_AF_SEARCH_DWELL_TIME_MS);
			break;
		case P2P_SCAN_CONNECT_TRY:
			eparams->params.active_time = htod32(WL_SCAN_CONNECT_DWELL_TIME_MS);
			break;
		default :
			if (wl_get_drv_status_all(cfg, CONNECTED))
		eparams->params.active_time = -1;
	else
		eparams->params.active_time = htod32(P2PAPI_SCAN_DWELL_TIME_MS);
			break;
	}

	if (p2p_scan_purpose == P2P_SCAN_CONNECT_TRY)
		eparams->params.nprobes = htod32(eparams->params.active_time /
			WL_SCAN_JOIN_PROBE_INTERVAL_MS);
	else
	eparams->params.nprobes = htod32((eparams->params.active_time /
		P2PAPI_SCAN_NPROBS_TIME_MS));


	if (eparams->params.nprobes <= 0)
		eparams->params.nprobes = 1;
	CFGP2P_DBG(("nprobes # %d, active_time %d\n",
		eparams->params.nprobes, eparams->params.active_time));
	eparams->params.passive_time = htod32(-1);
	eparams->params.channel_num = htod32((0 << WL_SCAN_PARAMS_NSSID_SHIFT) |
	    (num_chans & WL_SCAN_PARAMS_COUNT_MASK));

	for (i = 0; i < num_chans; i++) {
		eparams->params.channel_list[i] = wl_ch_host_to_driver(channels[i]);
	}
	eparams->version = htod32(ESCAN_REQ_VERSION);
	eparams->action =  htod16(action);
	wl_escan_set_sync_id(eparams->sync_id, cfg);
	wl_escan_set_type(cfg, WL_SCANTYPE_P2P);
	CFGP2P_INFO(("SCAN CHANNELS : "));

	for (i = 0; i < num_chans; i++) {
		if (i == 0) CFGP2P_INFO(("%d", channels[i]));
		else CFGP2P_INFO((",%d", channels[i]));
	}

	CFGP2P_INFO(("\n"));

	ret = wldev_iovar_setbuf_bsscfg(pri_dev, "p2p_scan",
		memblk, memsize, cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	if (ret == BCME_OK)
		wl_set_p2p_status(cfg, SCANNING);
	return ret;
}

/* search function to reach at common channel to send action frame
 * Parameters:
 * @cfg       : wl_private data
 * @ndev     : net device for bssidx
 * @bssidx   : bssidx for BSS
 * Returns 0 if success.
 */
s32
wl_cfgp2p_act_frm_search(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	s32 bssidx, s32 channel, struct ether_addr *tx_dst_addr)
{
	s32 ret = 0;
	u32 chan_cnt = 0;
	u16 *default_chan_list = NULL;
	p2p_scan_purpose_t p2p_scan_purpose = P2P_SCAN_AFX_PEER_NORMAL;
	if (!p2p_is_on(cfg) || ndev == NULL || bssidx == WL_INVALID)
		return -BCME_ERROR;
	WL_TRACE_HW4((" Enter\n"));
	if (bssidx == wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_PRIMARY))
		bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	if (channel)
		chan_cnt = AF_PEER_SEARCH_CNT;
	else
		chan_cnt = SOCIAL_CHAN_CNT;
	default_chan_list = kzalloc(chan_cnt * sizeof(*default_chan_list), GFP_KERNEL);
	if (default_chan_list == NULL) {
		CFGP2P_ERR(("channel list allocation failed \n"));
		ret = -ENOMEM;
		goto exit;
	}
	if (channel) {
		u32 i;
		/* insert same channel to the chan_list */
		for (i = 0; i < chan_cnt; i++) {
			default_chan_list[i] = channel;
		}
	} else {
		default_chan_list[0] = SOCIAL_CHAN_1;
		default_chan_list[1] = SOCIAL_CHAN_2;
		default_chan_list[2] = SOCIAL_CHAN_3;
	}
	ret = wl_cfgp2p_escan(cfg, ndev, true, chan_cnt,
		default_chan_list, WL_P2P_DISC_ST_SEARCH,
		WL_SCAN_ACTION_START, bssidx, NULL, p2p_scan_purpose);
	kfree(default_chan_list);
exit:
	return ret;
}

/* Check whether pointed-to IE looks like WPA. */
#define wl_cfgp2p_is_wpa_ie(ie, tlvs, len)	wl_cfgp2p_has_ie(ie, tlvs, len, \
		(const uint8 *)WPS_OUI, WPS_OUI_LEN, WPA_OUI_TYPE)
/* Check whether pointed-to IE looks like WPS. */
#define wl_cfgp2p_is_wps_ie(ie, tlvs, len)	wl_cfgp2p_has_ie(ie, tlvs, len, \
		(const uint8 *)WPS_OUI, WPS_OUI_LEN, WPS_OUI_TYPE)
/* Check whether the given IE looks like WFA P2P IE. */
#define wl_cfgp2p_is_p2p_ie(ie, tlvs, len)	wl_cfgp2p_has_ie(ie, tlvs, len, \
		(const uint8 *)WFA_OUI, WFA_OUI_LEN, WFA_OUI_TYPE_P2P)
/* Check whether the given IE looks like WFA WFDisplay IE. */
#ifndef WFA_OUI_TYPE_WFD
#define WFA_OUI_TYPE_WFD	0x0a			/* WiFi Display OUI TYPE */
#endif
#define wl_cfgp2p_is_wfd_ie(ie, tlvs, len)	wl_cfgp2p_has_ie(ie, tlvs, len, \
		(const uint8 *)WFA_OUI, WFA_OUI_LEN, WFA_OUI_TYPE_WFD)

static s32
wl_cfgp2p_parse_vndr_ies(u8 *parse, u32 len,
	struct parsed_vndr_ies *vndr_ies)
{
	s32 err = BCME_OK;
	vndr_ie_t *vndrie;
	bcm_tlv_t *ie;
	struct parsed_vndr_ie_info *parsed_info;
	u32	count = 0;
	s32 remained_len;

	remained_len = (s32)len;
	memset(vndr_ies, 0, sizeof(*vndr_ies));

	WL_INFO(("---> len %d\n", len));
	ie = (bcm_tlv_t *) parse;
	if (!bcm_valid_tlv(ie, remained_len))
		ie = NULL;
	while (ie) {
		if (count >= MAX_VNDR_IE_NUMBER)
			break;
		if (ie->id == DOT11_MNG_VS_ID) {
			vndrie = (vndr_ie_t *) ie;
			/* len should be bigger than OUI length + one data length at least */
			if (vndrie->len < (VNDR_IE_MIN_LEN + 1)) {
				CFGP2P_ERR(("%s: invalid vndr ie. length is too small %d\n",
					__FUNCTION__, vndrie->len));
				goto end;
			}
			/* if wpa or wme ie, do not add ie */
			if (!bcmp(vndrie->oui, (u8*)WPA_OUI, WPA_OUI_LEN) &&
				((vndrie->data[0] == WPA_OUI_TYPE) ||
				(vndrie->data[0] == WME_OUI_TYPE))) {
				CFGP2P_DBG(("Found WPA/WME oui. Do not add it\n"));
				goto end;
			}

			parsed_info = &vndr_ies->ie_info[count++];

			/* save vndr ie information */
			parsed_info->ie_ptr = (char *)vndrie;
			parsed_info->ie_len = (vndrie->len + TLV_HDR_LEN);
			memcpy(&parsed_info->vndrie, vndrie, sizeof(vndr_ie_t));

			vndr_ies->count = count;

			CFGP2P_DBG(("\t ** OUI %02x %02x %02x, type 0x%02x \n",
				parsed_info->vndrie.oui[0], parsed_info->vndrie.oui[1],
				parsed_info->vndrie.oui[2], parsed_info->vndrie.data[0]));
		}
end:
		ie = bcm_next_tlv(ie, &remained_len);
	}
	return err;
}


/* Delete and Set a management vndr ie to firmware
 * Parameters:
 * @cfg       : wl_private data
 * @ndev     : net device for bssidx
 * @bssidx   : bssidx for BSS
 * @pktflag  : packet flag for IE (VNDR_IE_PRBREQ_FLAG,VNDR_IE_PRBRSP_FLAG, VNDR_IE_ASSOCRSP_FLAG,
 *                                 VNDR_IE_ASSOCREQ_FLAG)
 * @ie       :  VNDR IE (such as P2P IE , WPS IE)
 * @ie_len   : VNDR IE Length
 * Returns 0 if success.
 */

s32
wl_cfgp2p_set_management_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx,
    s32 pktflag, const u8 *vndr_ie, u32 vndr_ie_len)
{
	s32 ret = BCME_OK;
	u8  *curr_ie_buf = NULL;
	u8  *mgmt_ie_buf = NULL;
	u32 mgmt_ie_buf_len = 0;
	u32 *mgmt_ie_len = 0;
	u32 del_add_ie_buf_len = 0;
	u32 total_ie_buf_len = 0;
	u32 parsed_ie_buf_len = 0;
	struct parsed_vndr_ies old_vndr_ies;
	struct parsed_vndr_ies new_vndr_ies;
	s32 i;
	u8 *ptr;
	s32 type = -1;
	s32 remained_buf_len;
#define IE_TYPE(type, bsstype) (wl_to_p2p_bss_saved_ie(cfg, bsstype).p2p_ ## type ## _ie)
#define IE_TYPE_LEN(type, bsstype) (wl_to_p2p_bss_saved_ie(cfg, bsstype).p2p_ ## type ## _ie_len)
	memset(g_mgmt_ie_buf, 0, sizeof(g_mgmt_ie_buf));
	curr_ie_buf = g_mgmt_ie_buf;
	CFGP2P_DBG((" bssidx %d, pktflag : 0x%02X\n", bssidx, pktflag));
	if (cfg->p2p != NULL) {
		if (wl_cfgp2p_find_type(cfg, bssidx, &type)) {
			CFGP2P_ERR(("cannot find type from bssidx : %d\n", bssidx));
			return BCME_ERROR;
		}

		switch (pktflag) {
			case VNDR_IE_PRBREQ_FLAG :
				mgmt_ie_buf = IE_TYPE(probe_req, type);
				mgmt_ie_len = &IE_TYPE_LEN(probe_req, type);
				mgmt_ie_buf_len = sizeof(IE_TYPE(probe_req, type));
				break;
			case VNDR_IE_PRBRSP_FLAG :
				mgmt_ie_buf = IE_TYPE(probe_res, type);
				mgmt_ie_len = &IE_TYPE_LEN(probe_res, type);
				mgmt_ie_buf_len = sizeof(IE_TYPE(probe_res, type));
				break;
			case VNDR_IE_ASSOCREQ_FLAG :
				mgmt_ie_buf = IE_TYPE(assoc_req, type);
				mgmt_ie_len = &IE_TYPE_LEN(assoc_req, type);
				mgmt_ie_buf_len = sizeof(IE_TYPE(assoc_req, type));
				break;
			case VNDR_IE_ASSOCRSP_FLAG :
				mgmt_ie_buf = IE_TYPE(assoc_res, type);
				mgmt_ie_len = &IE_TYPE_LEN(assoc_res, type);
				mgmt_ie_buf_len = sizeof(IE_TYPE(assoc_res, type));
				break;
			case VNDR_IE_BEACON_FLAG :
				mgmt_ie_buf = IE_TYPE(beacon, type);
				mgmt_ie_len = &IE_TYPE_LEN(beacon, type);
				mgmt_ie_buf_len = sizeof(IE_TYPE(beacon, type));
				break;
			default:
				mgmt_ie_buf = NULL;
				mgmt_ie_len = NULL;
				CFGP2P_ERR(("not suitable type\n"));
				return BCME_ERROR;
		}
	} else if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_AP) {
		if (cfg->ap_info == NULL) {
			CFGP2P_ERR(("hostapd ap_info null ptr refrence while setting  IE\n"));
			return BCME_ERROR;

		}
		switch (pktflag) {
			case VNDR_IE_PRBRSP_FLAG :
				mgmt_ie_buf = cfg->ap_info->probe_res_ie;
				mgmt_ie_len = &cfg->ap_info->probe_res_ie_len;
				mgmt_ie_buf_len = sizeof(cfg->ap_info->probe_res_ie);
				break;
			case VNDR_IE_BEACON_FLAG :
				mgmt_ie_buf = cfg->ap_info->beacon_ie;
				mgmt_ie_len = &cfg->ap_info->beacon_ie_len;
				mgmt_ie_buf_len = sizeof(cfg->ap_info->beacon_ie);
				break;
			case VNDR_IE_ASSOCRSP_FLAG :
				/* WPS-AP WSC2.0 assoc res includes wps_ie */
				mgmt_ie_buf = cfg->ap_info->assoc_res_ie;
				mgmt_ie_len = &cfg->ap_info->assoc_res_ie_len;
				mgmt_ie_buf_len = sizeof(cfg->ap_info->assoc_res_ie);
				break;
			default:
				mgmt_ie_buf = NULL;
				mgmt_ie_len = NULL;
				CFGP2P_ERR(("not suitable type\n"));
				return BCME_ERROR;
		}
		bssidx = 0;
	} else if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_BSS) {
		switch (pktflag) {
			case VNDR_IE_PRBREQ_FLAG :
				mgmt_ie_buf = cfg->sta_info->probe_req_ie;
				mgmt_ie_len = &cfg->sta_info->probe_req_ie_len;
				mgmt_ie_buf_len = sizeof(cfg->sta_info->probe_req_ie);
				break;
			case VNDR_IE_ASSOCREQ_FLAG :
				mgmt_ie_buf = cfg->sta_info->assoc_req_ie;
				mgmt_ie_len = &cfg->sta_info->assoc_req_ie_len;
				mgmt_ie_buf_len = sizeof(cfg->sta_info->assoc_req_ie);
				break;
			default:
				mgmt_ie_buf = NULL;
				mgmt_ie_len = NULL;
				CFGP2P_ERR(("not suitable type\n"));
				return BCME_ERROR;
		}
		bssidx = 0;
	} else {
		CFGP2P_ERR(("not suitable type\n"));
		return BCME_ERROR;
	}

	if (vndr_ie_len > mgmt_ie_buf_len) {
		CFGP2P_ERR(("extra IE size too big\n"));
		ret = -ENOMEM;
	} else {
		/* parse and save new vndr_ie in curr_ie_buff before comparing it */
		if (vndr_ie && vndr_ie_len && curr_ie_buf) {
			ptr = curr_ie_buf;

			wl_cfgp2p_parse_vndr_ies((u8*)vndr_ie,
				vndr_ie_len, &new_vndr_ies);

			for (i = 0; i < new_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
					&new_vndr_ies.ie_info[i];

				memcpy(ptr + parsed_ie_buf_len, vndrie_info->ie_ptr,
					vndrie_info->ie_len);
				parsed_ie_buf_len += vndrie_info->ie_len;
			}
		}

		if (mgmt_ie_buf != NULL) {
			if (parsed_ie_buf_len && (parsed_ie_buf_len == *mgmt_ie_len) &&
			     (memcmp(mgmt_ie_buf, curr_ie_buf, parsed_ie_buf_len) == 0)) {
				CFGP2P_INFO(("Previous mgmt IE is equals to current IE"));
				goto exit;
			}

			/* parse old vndr_ie */
			wl_cfgp2p_parse_vndr_ies(mgmt_ie_buf, *mgmt_ie_len,
				&old_vndr_ies);

			/* make a command to delete old ie */
			for (i = 0; i < old_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
					&old_vndr_ies.ie_info[i];

				CFGP2P_INFO(("DELETED ID : %d, Len: %d , OUI:%02x:%02x:%02x\n",
					vndrie_info->vndrie.id, vndrie_info->vndrie.len,
					vndrie_info->vndrie.oui[0], vndrie_info->vndrie.oui[1],
					vndrie_info->vndrie.oui[2]));

				del_add_ie_buf_len = wl_cfgp2p_vndr_ie(cfg, curr_ie_buf,
					pktflag, vndrie_info->vndrie.oui,
					vndrie_info->vndrie.id,
					vndrie_info->ie_ptr + VNDR_IE_FIXED_LEN,
					vndrie_info->ie_len - VNDR_IE_FIXED_LEN,
					"del");

				curr_ie_buf += del_add_ie_buf_len;
				total_ie_buf_len += del_add_ie_buf_len;
			}
		}

		*mgmt_ie_len = 0;
		/* Add if there is any extra IE */
		if (mgmt_ie_buf && parsed_ie_buf_len) {
			ptr = mgmt_ie_buf;

			remained_buf_len = mgmt_ie_buf_len;

			/* make a command to add new ie */
			for (i = 0; i < new_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
					&new_vndr_ies.ie_info[i];

				CFGP2P_INFO(("ADDED ID : %d, Len: %d(%d), OUI:%02x:%02x:%02x\n",
					vndrie_info->vndrie.id, vndrie_info->vndrie.len,
					vndrie_info->ie_len - 2,
					vndrie_info->vndrie.oui[0], vndrie_info->vndrie.oui[1],
					vndrie_info->vndrie.oui[2]));

				del_add_ie_buf_len = wl_cfgp2p_vndr_ie(cfg, curr_ie_buf,
					pktflag, vndrie_info->vndrie.oui,
					vndrie_info->vndrie.id,
					vndrie_info->ie_ptr + VNDR_IE_FIXED_LEN,
					vndrie_info->ie_len - VNDR_IE_FIXED_LEN,
					"add");

				/* verify remained buf size before copy data */
				if (remained_buf_len >= vndrie_info->ie_len) {
					remained_buf_len -= vndrie_info->ie_len;
				} else {
					CFGP2P_ERR(("no space in mgmt_ie_buf: pktflag = %d, "
						"found vndr ies # = %d(cur %d), remained len %d, "
						"cur mgmt_ie_len %d, new ie len = %d\n",
						pktflag, new_vndr_ies.count, i, remained_buf_len,
						*mgmt_ie_len, vndrie_info->ie_len));
					break;
				}

				/* save the parsed IE in cfg struct */
				memcpy(ptr + (*mgmt_ie_len), vndrie_info->ie_ptr,
					vndrie_info->ie_len);
				*mgmt_ie_len += vndrie_info->ie_len;

				curr_ie_buf += del_add_ie_buf_len;
				total_ie_buf_len += del_add_ie_buf_len;
			}
		}
		if (total_ie_buf_len) {
			ret  = wldev_iovar_setbuf_bsscfg(ndev, "vndr_ie", g_mgmt_ie_buf,
				total_ie_buf_len, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
				bssidx, &cfg->ioctl_buf_sync);
			if (ret)
				CFGP2P_ERR(("vndr ie set error : %d\n", ret));
		}
	}
#undef IE_TYPE
#undef IE_TYPE_LEN
exit:
	return ret;
}

/* Clear the manament IE buffer of BSSCFG
 * Parameters:
 * @cfg       : wl_private data
 * @bssidx   : bssidx for BSS
 *
 * Returns 0 if success.
 */
s32
wl_cfgp2p_clear_management_ie(struct bcm_cfg80211 *cfg, s32 bssidx)
{

	s32 vndrie_flag[] = {VNDR_IE_BEACON_FLAG, VNDR_IE_PRBRSP_FLAG, VNDR_IE_ASSOCRSP_FLAG,
		VNDR_IE_PRBREQ_FLAG, VNDR_IE_ASSOCREQ_FLAG};
	s32 index = -1;
	s32 type = -1;
	struct net_device *ndev = wl_cfgp2p_find_ndev(cfg, bssidx);
#define INIT_IE(IE_TYPE, BSS_TYPE)		\
	do {							\
		memset(wl_to_p2p_bss_saved_ie(cfg, BSS_TYPE).p2p_ ## IE_TYPE ## _ie, 0, \
		   sizeof(wl_to_p2p_bss_saved_ie(cfg, BSS_TYPE).p2p_ ## IE_TYPE ## _ie)); \
		wl_to_p2p_bss_saved_ie(cfg, BSS_TYPE).p2p_ ## IE_TYPE ## _ie_len = 0; \
	} while (0);

	if (bssidx < 0 || ndev == NULL) {
		CFGP2P_ERR(("invalid %s\n", (bssidx < 0) ? "bssidx" : "ndev"));
		return BCME_BADARG;
	}

	if (wl_cfgp2p_find_type(cfg, bssidx, &type)) {
		CFGP2P_ERR(("invalid argument\n"));
		return BCME_BADARG;
	}
	for (index = 0; index < ARRAYSIZE(vndrie_flag); index++) {
		/* clean up vndr ies in dongle */
		wl_cfgp2p_set_management_ie(cfg, ndev, bssidx, vndrie_flag[index], NULL, 0);
	}
	INIT_IE(probe_req, type);
	INIT_IE(probe_res, type);
	INIT_IE(assoc_req, type);
	INIT_IE(assoc_res, type);
	INIT_IE(beacon, type);
	return BCME_OK;
}


/* Is any of the tlvs the expected entry? If
 * not update the tlvs buffer pointer/length.
 */
static bool
wl_cfgp2p_has_ie(u8 *ie, u8 **tlvs, u32 *tlvs_len, const u8 *oui, u32 oui_len, u8 type)
{
	/* If the contents match the OUI and the type */
	if (ie[TLV_LEN_OFF] >= oui_len + 1 &&
		!bcmp(&ie[TLV_BODY_OFF], oui, oui_len) &&
		type == ie[TLV_BODY_OFF + oui_len]) {
		return TRUE;
	}

	if (tlvs == NULL)
		return FALSE;
	/* point to the next ie */
	ie += ie[TLV_LEN_OFF] + TLV_HDR_LEN;
	/* calculate the length of the rest of the buffer */
	*tlvs_len -= (int)(ie - *tlvs);
	/* update the pointer to the start of the buffer */
	*tlvs = ie;

	return FALSE;
}

wpa_ie_fixed_t *
wl_cfgp2p_find_wpaie(u8 *parse, u32 len)
{
	bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, (u32)len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_wpa_ie((u8*)ie, &parse, &len)) {
			return (wpa_ie_fixed_t *)ie;
		}
	}
	return NULL;
}

wpa_ie_fixed_t *
wl_cfgp2p_find_wpsie(u8 *parse, u32 len)
{
	bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, (u32)len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_wps_ie((u8*)ie, &parse, &len)) {
			return (wpa_ie_fixed_t *)ie;
		}
	}
	return NULL;
}

wifi_p2p_ie_t *
wl_cfgp2p_find_p2pie(u8 *parse, u32 len)
{
	bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, (int)len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_p2p_ie((uint8*)ie, &parse, &len)) {
			return (wifi_p2p_ie_t *)ie;
		}
	}
	return NULL;
}

wifi_wfd_ie_t *
wl_cfgp2p_find_wfdie(u8 *parse, u32 len)
{
	bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, (int)len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_wfd_ie((uint8*)ie, &parse, &len)) {
			return (wifi_wfd_ie_t *)ie;
		}
	}
	return NULL;
}
static u32
wl_cfgp2p_vndr_ie(struct bcm_cfg80211 *cfg, u8 *iebuf, s32 pktflag,
            s8 *oui, s32 ie_id, s8 *data, s32 datalen, const s8* add_del_cmd)
{
	vndr_ie_setbuf_t hdr;	/* aligned temporary vndr_ie buffer header */
	s32 iecount;
	u32 data_offset;

	/* Validate the pktflag parameter */
	if ((pktflag & ~(VNDR_IE_BEACON_FLAG | VNDR_IE_PRBRSP_FLAG |
	            VNDR_IE_ASSOCRSP_FLAG | VNDR_IE_AUTHRSP_FLAG |
	            VNDR_IE_PRBREQ_FLAG | VNDR_IE_ASSOCREQ_FLAG))) {
		CFGP2P_ERR(("p2pwl_vndr_ie: Invalid packet flag 0x%x\n", pktflag));
		return -1;
	}

	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strncpy(hdr.cmd, add_del_cmd, VNDR_IE_CMD_LEN - 1);
	hdr.cmd[VNDR_IE_CMD_LEN - 1] = '\0';

	/* Set the IE count - the buffer contains only 1 IE */
	iecount = htod32(1);
	memcpy((void *)&hdr.vndr_ie_buffer.iecount, &iecount, sizeof(s32));

	/* Copy packet flags that indicate which packets will contain this IE */
	pktflag = htod32(pktflag);
	memcpy((void *)&hdr.vndr_ie_buffer.vndr_ie_list[0].pktflag, &pktflag,
		sizeof(u32));

	/* Add the IE ID to the buffer */
	hdr.vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.id = ie_id;

	/* Add the IE length to the buffer */
	hdr.vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.len =
		(uint8) VNDR_IE_MIN_LEN + datalen;

	/* Add the IE OUI to the buffer */
	hdr.vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui[0] = oui[0];
	hdr.vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui[1] = oui[1];
	hdr.vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui[2] = oui[2];

	/* Copy the aligned temporary vndr_ie buffer header to the IE buffer */
	memcpy(iebuf, &hdr, sizeof(hdr) - 1);

	/* Copy the IE data to the IE buffer */
	data_offset =
		(u8*)&hdr.vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.data[0] -
		(u8*)&hdr;
	memcpy(iebuf + data_offset, data, datalen);
	return data_offset + datalen;

}

/*
 * Search the bssidx based on dev argument
 * Parameters:
 * @cfg       : wl_private data
 * @ndev     : net device to search bssidx
 * @bssidx  : output arg to store bssidx of the bsscfg of firmware.
 * Returns error
 */
s32
wl_cfgp2p_find_idx(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 *bssidx)
{
	u32 i;
	if (ndev == NULL || bssidx == NULL) {
		CFGP2P_ERR((" argument is invalid\n"));
		return BCME_BADARG;
	}
	if (!cfg->p2p_supported) {
		*bssidx = P2PAPI_BSSCFG_PRIMARY;
		return BCME_OK;
	}
	/* we cannot find the bssidx of DISCOVERY BSS
	 *  because the ndev is same with ndev of PRIMARY BSS.
	 */
	for (i = 0; i < P2PAPI_BSSCFG_MAX; i++) {
		if (ndev == wl_to_p2p_bss_ndev(cfg, i)) {
			*bssidx = wl_to_p2p_bss_bssidx(cfg, i);
			return BCME_OK;
		}
	}
	return BCME_BADARG;
}
struct net_device *
wl_cfgp2p_find_ndev(struct bcm_cfg80211 *cfg, s32 bssidx)
{
	u32 i;
	struct net_device *ndev = NULL;
	if (bssidx < 0) {
		CFGP2P_ERR((" bsscfg idx is invalid\n"));
		goto exit;
	}

	for (i = 0; i < P2PAPI_BSSCFG_MAX; i++) {
		if (bssidx == wl_to_p2p_bss_bssidx(cfg, i)) {
			ndev = wl_to_p2p_bss_ndev(cfg, i);
			break;
		}
	}

exit:
	return ndev;
}
/*
 * Search the driver array idx based on bssidx argument
 * Parameters:
 * @cfg     : wl_private data
 * @bssidx : bssidx which indicate bsscfg->idx of firmware.
 * @type   : output arg to store array idx of p2p->bss.
 * Returns error
 */

s32
wl_cfgp2p_find_type(struct bcm_cfg80211 *cfg, s32 bssidx, s32 *type)
{
	u32 i;
	if (bssidx < 0 || type == NULL) {
		CFGP2P_ERR((" argument is invalid\n"));
		goto exit;
	}

	for (i = 0; i < P2PAPI_BSSCFG_MAX; i++) {
		if (bssidx == wl_to_p2p_bss_bssidx(cfg, i)) {
			*type = i;
			return BCME_OK;
		}
	}

exit:
	return BCME_BADARG;
}

/*
 * Callback function for WLC_E_P2P_DISC_LISTEN_COMPLETE
 */
s32
wl_cfgp2p_listen_complete(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 ret = BCME_OK;
	struct net_device *ndev = NULL;

	if (!cfg || !cfg->p2p || !cfgdev)
		return BCME_ERROR;

	CFGP2P_DBG((" Enter\n"));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (wl_get_p2p_status(cfg, LISTEN_EXPIRED) == 0) {
		wl_set_p2p_status(cfg, LISTEN_EXPIRED);
		if (timer_pending(&cfg->p2p->listen_timer)) {
			del_timer_sync(&cfg->p2p->listen_timer);
		}

		if (cfg->afx_hdl->is_listen == TRUE &&
			wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_DBG(("Listen DONE for action frame\n"));
			complete(&cfg->act_frm_scan);
		}
#ifdef WL_CFG80211_SYNC_GON
		else if (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM_LISTEN)) {
			wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM_LISTEN, ndev);
			WL_DBG(("Listen DONE and wake up wait_next_af !!(%d)\n",
				jiffies_to_msecs(jiffies - cfg->af_tx_sent_jiffies)));

			if (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM))
				wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, ndev);

			complete(&cfg->wait_next_af);
		}
#endif /* WL_CFG80211_SYNC_GON */

#ifndef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
		if (wl_get_drv_status_all(cfg, REMAINING_ON_CHANNEL)) {
#else
		if (wl_get_drv_status_all(cfg, REMAINING_ON_CHANNEL) ||
			wl_get_drv_status_all(cfg, FAKE_REMAINING_ON_CHANNEL)) {
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
			WL_DBG(("Listen DONE for ramain on channel expired\n"));
			wl_clr_drv_status(cfg, REMAINING_ON_CHANNEL, ndev);
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
			wl_clr_drv_status(cfg, FAKE_REMAINING_ON_CHANNEL, ndev);
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
			if (ndev && (ndev->ieee80211_ptr != NULL)) {
#if defined(WL_CFG80211_P2P_DEV_IF)
				cfg80211_remain_on_channel_expired(
					bcmcfg_to_p2p_wdev(cfg), cfg->last_roc_id,
					&cfg->remain_on_chan, GFP_KERNEL);
#else
				cfg80211_remain_on_channel_expired(cfg->p2p_net, cfg->last_roc_id,
					&cfg->remain_on_chan, cfg->remain_on_chan_type, GFP_KERNEL);
#endif /* WL_CFG80211_P2P_DEV_IF */
			}
		}
		if (wl_add_remove_eventmsg(bcmcfg_to_prmry_ndev(cfg),
			WLC_E_P2P_PROBREQ_MSG, false) != BCME_OK) {
			CFGP2P_ERR((" failed to unset WLC_E_P2P_PROPREQ_MSG\n"));
		}
	} else
		wl_clr_p2p_status(cfg, LISTEN_EXPIRED);

	return ret;

}

/*
 *  Timer expire callback function for LISTEN
 *  We can't report cfg80211_remain_on_channel_expired from Timer ISR context,
 *  so lets do it from thread context.
 */
void
wl_cfgp2p_listen_expired(unsigned long data)
{
	wl_event_msg_t msg;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *) data;
	CFGP2P_DBG((" Enter\n"));
	bzero(&msg, sizeof(wl_event_msg_t));
	msg.event_type =  hton32(WLC_E_P2P_DISC_LISTEN_COMPLETE);
#if defined(WL_ENABLE_P2P_IF)
	wl_cfg80211_event(cfg->p2p_net ? cfg->p2p_net :
		wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE), &msg, NULL);
#else
	wl_cfg80211_event(wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE), &msg,
		NULL);
#endif /* WL_ENABLE_P2P_IF */
}
/*
 *  Routine for cancelling the P2P LISTEN
 */
static s32
wl_cfgp2p_cancel_listen(struct bcm_cfg80211 *cfg, struct net_device *ndev,
                         struct wireless_dev *wdev, bool notify)
{
	WL_DBG(("Enter \n"));
	/* Irrespective of whether timer is running or not, reset
	 * the LISTEN state.
	 */
	if (timer_pending(&cfg->p2p->listen_timer)) {
		del_timer_sync(&cfg->p2p->listen_timer);
		if (notify) {
#if defined(WL_CFG80211_P2P_DEV_IF)
#ifdef P2PONEINT
			if (wdev == NULL)
				wdev = bcmcfg_to_p2p_wdev(cfg);
#endif
			if (wdev)
				cfg80211_remain_on_channel_expired(
					bcmcfg_to_p2p_wdev(cfg), cfg->last_roc_id,
					&cfg->remain_on_chan, GFP_KERNEL);
#else
			if (ndev && ndev->ieee80211_ptr)
				cfg80211_remain_on_channel_expired(cfg->p2p_net, cfg->last_roc_id,
					&cfg->remain_on_chan, cfg->remain_on_chan_type, GFP_KERNEL);
#endif /* WL_CFG80211_P2P_DEV_IF */
		}
	}
	return 0;
}
/*
 * Do a P2P Listen on the given channel for the given duration.
 * A listen consists of sitting idle and responding to P2P probe requests
 * with a P2P probe response.
 *
 * This fn assumes dongle p2p device discovery is already enabled.
 * Parameters   :
 * @cfg          : wl_private data
 * @channel     : channel to listen
 * @duration_ms : the time (milli seconds) to wait
 */
s32
wl_cfgp2p_discover_listen(struct bcm_cfg80211 *cfg, s32 channel, u32 duration_ms)
{
#define EXTRA_DELAY_TIME	100
	s32 ret = BCME_OK;
	struct timer_list *_timer;
	s32 extra_delay;
	struct net_device *netdev = bcmcfg_to_prmry_ndev(cfg);

	CFGP2P_DBG((" Enter Listen Channel : %d, Duration : %d\n", channel, duration_ms));
	if (unlikely(wl_get_p2p_status(cfg, DISCOVERY_ON) == 0)) {

		CFGP2P_ERR((" Discovery is not set, so we have noting to do\n"));

		ret = BCME_NOTREADY;
		goto exit;
	}
	if (timer_pending(&cfg->p2p->listen_timer)) {
		CFGP2P_DBG(("previous LISTEN is not completed yet\n"));
		goto exit;

	}
#ifndef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	else
		wl_clr_p2p_status(cfg, LISTEN_EXPIRED);
#endif /* not WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
	if (wl_add_remove_eventmsg(netdev, WLC_E_P2P_PROBREQ_MSG, true) != BCME_OK) {
			CFGP2P_ERR((" failed to set WLC_E_P2P_PROPREQ_MSG\n"));
	}

	ret = wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_LISTEN, channel, (u16) duration_ms,
	            wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));
	_timer = &cfg->p2p->listen_timer;

	/*  We will wait to receive WLC_E_P2P_DISC_LISTEN_COMPLETE from dongle ,
	 *  otherwise we will wait up to duration_ms + 100ms + duration / 10
	 */
	if (ret == BCME_OK) {
		extra_delay = EXTRA_DELAY_TIME + (duration_ms / 10);
	} else {
		/* if failed to set listen, it doesn't need to wait whole duration. */
		duration_ms = 100 + duration_ms / 20;
		extra_delay = 0;
	}

	INIT_TIMER(_timer, wl_cfgp2p_listen_expired, duration_ms, extra_delay);
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	wl_clr_p2p_status(cfg, LISTEN_EXPIRED);
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */

#undef EXTRA_DELAY_TIME
exit:
	return ret;
}


s32
wl_cfgp2p_discover_enable_search(struct bcm_cfg80211 *cfg, u8 enable)
{
	s32 ret = BCME_OK;
	CFGP2P_DBG((" Enter\n"));
	if (!wl_get_p2p_status(cfg, DISCOVERY_ON)) {

		CFGP2P_DBG((" do nothing, discovery is off\n"));
		return ret;
	}
	if (wl_get_p2p_status(cfg, SEARCH_ENABLED) == enable) {
		CFGP2P_DBG(("already : %d\n", enable));
		return ret;
	}

	wl_chg_p2p_status(cfg, SEARCH_ENABLED);
	/* When disabling Search, reset the WL driver's p2p discovery state to
	 * WL_P2P_DISC_ST_SCAN.
	 */
	if (!enable) {
		wl_clr_p2p_status(cfg, SCANNING);
		ret = wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
		            wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));
	}

	return ret;
}

/*
 * Callback function for WLC_E_ACTION_FRAME_COMPLETE, WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE
 */
s32
wl_cfgp2p_action_tx_complete(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
            const wl_event_msg_t *e, void *data)
{
	s32 ret = BCME_OK;
	u32 event_type = ntoh32(e->event_type);
	u32 status = ntoh32(e->status);
	struct net_device *ndev = NULL;
	CFGP2P_DBG((" Enter\n"));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (wl_get_drv_status_all(cfg, SENDING_ACT_FRM)) {
		if (event_type == WLC_E_ACTION_FRAME_COMPLETE) {

			CFGP2P_INFO((" WLC_E_ACTION_FRAME_COMPLETE is received : %d\n", status));
			if (status == WLC_E_STATUS_SUCCESS) {
				wl_set_p2p_status(cfg, ACTION_TX_COMPLETED);
				CFGP2P_DBG(("WLC_E_ACTION_FRAME_COMPLETE : ACK\n"));
				if (!cfg->need_wait_afrx && cfg->af_sent_channel) {
					CFGP2P_DBG(("no need to wait next AF.\n"));
					wl_stop_wait_next_action_frame(cfg, ndev);
				}
			}
			else if (!wl_get_p2p_status(cfg, ACTION_TX_COMPLETED)) {
				wl_set_p2p_status(cfg, ACTION_TX_NOACK);
				CFGP2P_INFO(("WLC_E_ACTION_FRAME_COMPLETE : NO ACK\n"));
				wl_stop_wait_next_action_frame(cfg, ndev);
			}
		} else {
			CFGP2P_INFO((" WLC_E_ACTION_FRAME_OFFCHAN_COMPLETE is received,"
						"status : %d\n", status));

			if (wl_get_drv_status_all(cfg, SENDING_ACT_FRM))
				complete(&cfg->send_af_done);
		}
	}
	return ret;
}
/* Send an action frame immediately without doing channel synchronization.
 *
 * This function does not wait for a completion event before returning.
 * The WLC_E_ACTION_FRAME_COMPLETE event will be received when the action
 * frame is transmitted.
 * The WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE event will be received when an
 * 802.11 ack has been received for the sent action frame.
 */
s32
wl_cfgp2p_tx_action_frame(struct bcm_cfg80211 *cfg, struct net_device *dev,
	wl_af_params_t *af_params, s32 bssidx)
{
	s32 ret = BCME_OK;
	s32 evt_ret = BCME_OK;
	s32 timeout = 0;
	wl_eventmsg_buf_t buf;


	CFGP2P_INFO(("\n"));
	CFGP2P_INFO(("channel : %u , dwell time : %u\n",
	    af_params->channel, af_params->dwell_time));

	wl_clr_p2p_status(cfg, ACTION_TX_COMPLETED);
	wl_clr_p2p_status(cfg, ACTION_TX_NOACK);

	bzero(&buf, sizeof(wl_eventmsg_buf_t));
	wl_cfg80211_add_to_eventbuffer(&buf, WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE, true);
	wl_cfg80211_add_to_eventbuffer(&buf, WLC_E_ACTION_FRAME_COMPLETE, true);
	if ((evt_ret = wl_cfg80211_apply_eventbuffer(bcmcfg_to_prmry_ndev(cfg), cfg, &buf)) < 0)
		return evt_ret;

	cfg->af_sent_channel  = af_params->channel;
#ifdef WL_CFG80211_SYNC_GON
	cfg->af_tx_sent_jiffies = jiffies;
#endif /* WL_CFG80211_SYNC_GON */

	ret = wldev_iovar_setbuf_bsscfg(dev, "actframe", af_params, sizeof(*af_params),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);

	if (ret < 0) {
		CFGP2P_ERR((" sending action frame is failed\n"));
		goto exit;
	}

	timeout = wait_for_completion_timeout(&cfg->send_af_done,
		msecs_to_jiffies(af_params->dwell_time + WL_AF_TX_EXTRA_TIME_MAX));

	if (timeout >= 0 && wl_get_p2p_status(cfg, ACTION_TX_COMPLETED)) {
		CFGP2P_INFO(("tx action frame operation is completed\n"));
		ret = BCME_OK;
	} else if (ETHER_ISBCAST(&cfg->afx_hdl->tx_dst_addr)) {
		CFGP2P_INFO(("bcast tx action frame operation is completed\n"));
		ret = BCME_OK;
	} else {
		ret = BCME_ERROR;
		CFGP2P_INFO(("tx action frame operation is failed\n"));
	}
	/* clear status bit for action tx */
	wl_clr_p2p_status(cfg, ACTION_TX_COMPLETED);
	wl_clr_p2p_status(cfg, ACTION_TX_NOACK);

exit:
	CFGP2P_INFO((" via act frame iovar : status = %d\n", ret));

	bzero(&buf, sizeof(wl_eventmsg_buf_t));
	wl_cfg80211_add_to_eventbuffer(&buf, WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE, false);
	wl_cfg80211_add_to_eventbuffer(&buf, WLC_E_ACTION_FRAME_COMPLETE, false);
	if ((evt_ret = wl_cfg80211_apply_eventbuffer(bcmcfg_to_prmry_ndev(cfg), cfg, &buf)) < 0) {
		WL_ERR(("TX frame events revert back failed \n"));
		return evt_ret;
	}

	return ret;
}

/* Generate our P2P Device Address and P2P Interface Address from our primary
 * MAC address.
 */
void
wl_cfgp2p_generate_bss_mac(struct ether_addr *primary_addr,
            struct ether_addr *out_dev_addr, struct ether_addr *out_int_addr)
{
	memset(out_dev_addr, 0, sizeof(*out_dev_addr));
	memset(out_int_addr, 0, sizeof(*out_int_addr));

	/* Generate the P2P Device Address.  This consists of the device's
	 * primary MAC address with the locally administered bit set.
	 */
	memcpy(out_dev_addr, primary_addr, sizeof(*out_dev_addr));
	out_dev_addr->octet[0] |= 0x02;

	/* Generate the P2P Interface Address.  If the discovery and connection
	 * BSSCFGs need to simultaneously co-exist, then this address must be
	 * different from the P2P Device Address.
	 */
	memcpy(out_int_addr, out_dev_addr, sizeof(*out_int_addr));
#ifndef  P2PONEINT
	out_int_addr->octet[4] ^= 0x80;
#endif

}

/* P2P IF Address change to Virtual Interface MAC Address */
void
wl_cfg80211_change_ifaddr(u8* buf, struct ether_addr *p2p_int_addr, u8 element_id)
{
	wifi_p2p_ie_t *ie = (wifi_p2p_ie_t*) buf;
	u16 len = ie->len;
	u8 *subel;
	u8 subelt_id;
	u16 subelt_len;
	CFGP2P_DBG((" Enter\n"));

	/* Point subel to the P2P IE's subelt field.
	 * Subtract the preceding fields (id, len, OUI, oui_type) from the length.
	 */
	subel = ie->subelts;
	len -= 4;	/* exclude OUI + OUI_TYPE */

	while (len >= 3) {
	/* attribute id */
		subelt_id = *subel;
		subel += 1;
		len -= 1;

		/* 2-byte little endian */
		subelt_len = *subel++;
		subelt_len |= *subel++ << 8;

		len -= 2;
		len -= subelt_len;	/* for the remaining subelt fields */

		if (subelt_id == element_id) {
			if (subelt_id == P2P_SEID_INTINTADDR) {
				memcpy(subel, p2p_int_addr->octet, ETHER_ADDR_LEN);
				CFGP2P_INFO(("Intended P2P Interface Address ATTR FOUND\n"));
			} else if (subelt_id == P2P_SEID_DEV_ID) {
				memcpy(subel, p2p_int_addr->octet, ETHER_ADDR_LEN);
				CFGP2P_INFO(("Device ID ATTR FOUND\n"));
			} else if (subelt_id == P2P_SEID_DEV_INFO) {
				memcpy(subel, p2p_int_addr->octet, ETHER_ADDR_LEN);
				CFGP2P_INFO(("Device INFO ATTR FOUND\n"));
			} else if (subelt_id == P2P_SEID_GROUP_ID) {
				memcpy(subel, p2p_int_addr->octet, ETHER_ADDR_LEN);
				CFGP2P_INFO(("GROUP ID ATTR FOUND\n"));
			}			return;
		} else {
			CFGP2P_DBG(("OTHER id : %d\n", subelt_id));
		}
		subel += subelt_len;
	}
}
/*
 * Check if a BSS is up.
 * This is a common implementation called by most OSL implementations of
 * p2posl_bss_isup().  DO NOT call this function directly from the
 * common code -- call p2posl_bss_isup() instead to allow the OSL to
 * override the common implementation if necessary.
 */
bool
wl_cfgp2p_bss_isup(struct net_device *ndev, int bsscfg_idx)
{
	s32 result, val;
	bool isup = false;
	s8 getbuf[64];

	/* Check if the BSS is up */
	*(int*)getbuf = -1;
	result = wldev_iovar_getbuf_bsscfg(ndev, "bss", &bsscfg_idx,
		sizeof(bsscfg_idx), getbuf, sizeof(getbuf), 0, NULL);
	if (result != 0) {
		CFGP2P_ERR(("'cfg bss -C %d' failed: %d\n", bsscfg_idx, result));
		CFGP2P_ERR(("NOTE: this ioctl error is normal "
					"when the BSS has not been created yet.\n"));
	} else {
		val = *(int*)getbuf;
		val = dtoh32(val);
		CFGP2P_INFO(("---cfg bss -C %d   ==> %d\n", bsscfg_idx, val));
		isup = (val ? TRUE : FALSE);
	}
	return isup;
}


/* Bring up or down a BSS */
s32
wl_cfgp2p_bss(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bsscfg_idx, s32 up)
{
	s32 ret = BCME_OK;
	s32 val = up ? 1 : 0;

	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;

	bss_setbuf.cfg = htod32(bsscfg_idx);
	bss_setbuf.val = htod32(val);
	CFGP2P_INFO(("---cfg bss -C %d %s\n", bsscfg_idx, up ? "up" : "down"));
	ret = wldev_iovar_setbuf(ndev, "bss", &bss_setbuf, sizeof(bss_setbuf),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);

	if (ret != 0) {
		CFGP2P_ERR(("'bss %d' failed with %d\n", up, ret));
	}

	return ret;
}

/* Check if 'p2p' is supported in the driver */
s32
wl_cfgp2p_supported(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	s32 ret = BCME_OK;
	s32 p2p_supported = 0;
	ret = wldev_iovar_getint(ndev, "p2p",
	               &p2p_supported);
	if (ret < 0) {
		if (ret == BCME_UNSUPPORTED) {
			CFGP2P_INFO(("p2p is unsupported\n"));
			return 0;
		} else {
			CFGP2P_ERR(("cfg p2p error %d\n", ret));
			return ret;
		}
	}
	if (p2p_supported == 1) {
		CFGP2P_INFO(("p2p is supported\n"));
	} else {
		CFGP2P_INFO(("p2p is unsupported\n"));
		p2p_supported = 0;
	}
	return p2p_supported;
}
/* Cleanup P2P resources */
s32
wl_cfgp2p_down(struct bcm_cfg80211 *cfg)
{
	struct net_device *ndev = NULL;
	struct wireless_dev *wdev = NULL;
	s32 i = 0, index = -1;

#if defined(WL_CFG80211_P2P_DEV_IF)
	wdev = bcmcfg_to_p2p_wdev(cfg);
#ifdef P2PONEINT
	ndev = wdev_to_ndev(wdev);
#else
	ndev = bcmcfg_to_prmry_ndev(cfg);
#endif
#elif defined(WL_ENABLE_P2P_IF)
	ndev = cfg->p2p_net ? cfg->p2p_net : bcmcfg_to_prmry_ndev(cfg);
	wdev = ndev_to_wdev(ndev);
#endif /* WL_CFG80211_P2P_DEV_IF */

	wl_cfgp2p_cancel_listen(cfg, ndev, wdev, TRUE);
	for (i = 0; i < P2PAPI_BSSCFG_MAX; i++) {
			index = wl_to_p2p_bss_bssidx(cfg, i);
			if (index != WL_INVALID)
				wl_cfgp2p_clear_management_ie(cfg, index);
	}
	wl_cfgp2p_deinit_priv(cfg);
	return 0;
}
s32
wl_cfgp2p_set_p2p_noa(struct bcm_cfg80211 *cfg, struct net_device *ndev, char* buf, int len)
{
	s32 ret = -1;
	int count, start, duration;
	wl_p2p_sched_t dongle_noa;

	CFGP2P_DBG((" Enter\n"));

	memset(&dongle_noa, 0, sizeof(dongle_noa));

	if (cfg->p2p && cfg->p2p->vif_created) {

		cfg->p2p->noa.desc[0].start = 0;

		sscanf(buf, "%10d %10d %10d", &count, &start, &duration);
		CFGP2P_DBG(("set_p2p_noa count %d start %d duration %d\n",
			count, start, duration));
		if (count != -1)
			cfg->p2p->noa.desc[0].count = count;

		/* supplicant gives interval as start */
		if (start != -1)
			cfg->p2p->noa.desc[0].interval = start;

		if (duration != -1)
			cfg->p2p->noa.desc[0].duration = duration;

		if (cfg->p2p->noa.desc[0].count < 255 && cfg->p2p->noa.desc[0].count > 1) {
			cfg->p2p->noa.desc[0].start = 0;
			dongle_noa.type = WL_P2P_SCHED_TYPE_ABS;
			dongle_noa.action = WL_P2P_SCHED_ACTION_NONE;
			dongle_noa.option = WL_P2P_SCHED_OPTION_TSFOFS;
		}
		else if (cfg->p2p->noa.desc[0].count == 1) {
			cfg->p2p->noa.desc[0].start = 200;
			dongle_noa.type = WL_P2P_SCHED_TYPE_REQ_ABS;
			dongle_noa.action = WL_P2P_SCHED_ACTION_GOOFF;
			dongle_noa.option = WL_P2P_SCHED_OPTION_TSFOFS;
		}
		else if (cfg->p2p->noa.desc[0].count == 0) {
			cfg->p2p->noa.desc[0].start = 0;
			dongle_noa.action = WL_P2P_SCHED_ACTION_RESET;
		}
		else {
			/* Continuous NoA interval. */
			dongle_noa.action = WL_P2P_SCHED_ACTION_NONE;
			dongle_noa.type = WL_P2P_SCHED_TYPE_ABS;
			if ((cfg->p2p->noa.desc[0].interval == 102) ||
				(cfg->p2p->noa.desc[0].interval == 100)) {
				cfg->p2p->noa.desc[0].start = 100 -
					cfg->p2p->noa.desc[0].duration;
				dongle_noa.option = WL_P2P_SCHED_OPTION_BCNPCT;
			}
			else {
				dongle_noa.option = WL_P2P_SCHED_OPTION_NORMAL;
			}
		}
		/* Put the noa descriptor in dongle format for dongle */
		dongle_noa.desc[0].count = htod32(cfg->p2p->noa.desc[0].count);
		if (dongle_noa.option == WL_P2P_SCHED_OPTION_BCNPCT) {
			dongle_noa.desc[0].start = htod32(cfg->p2p->noa.desc[0].start);
			dongle_noa.desc[0].duration = htod32(cfg->p2p->noa.desc[0].duration);
		}
		else {
			dongle_noa.desc[0].start = htod32(cfg->p2p->noa.desc[0].start*1000);
			dongle_noa.desc[0].duration = htod32(cfg->p2p->noa.desc[0].duration*1000);
		}
		dongle_noa.desc[0].interval = htod32(cfg->p2p->noa.desc[0].interval*1000);

		ret = wldev_iovar_setbuf(wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION),
			"p2p_noa", &dongle_noa, sizeof(dongle_noa), cfg->ioctl_buf,
			WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);

		if (ret < 0) {
			CFGP2P_ERR(("fw set p2p_noa failed %d\n", ret));
		}
	}
	else {
		CFGP2P_ERR(("ERROR: set_noa in non-p2p mode\n"));
	}
	return ret;
}
s32
wl_cfgp2p_get_p2p_noa(struct bcm_cfg80211 *cfg, struct net_device *ndev, char* buf, int buf_len)
{

	wifi_p2p_noa_desc_t *noa_desc;
	int len = 0, i;
	char _buf[200];

	CFGP2P_DBG((" Enter\n"));
	buf[0] = '\0';
	if (cfg->p2p && cfg->p2p->vif_created) {
		if (cfg->p2p->noa.desc[0].count || cfg->p2p->ops.ops) {
			_buf[0] = 1; /* noa index */
			_buf[1] = (cfg->p2p->ops.ops ? 0x80: 0) |
				(cfg->p2p->ops.ctw & 0x7f); /* ops + ctw */
			len += 2;
			if (cfg->p2p->noa.desc[0].count) {
				noa_desc = (wifi_p2p_noa_desc_t*)&_buf[len];
				noa_desc->cnt_type = cfg->p2p->noa.desc[0].count;
				noa_desc->duration = cfg->p2p->noa.desc[0].duration;
				noa_desc->interval = cfg->p2p->noa.desc[0].interval;
				noa_desc->start = cfg->p2p->noa.desc[0].start;
				len += sizeof(wifi_p2p_noa_desc_t);
			}
			if (buf_len <= len * 2) {
				CFGP2P_ERR(("ERROR: buf_len %d in not enough for"
					"returning noa in string format\n", buf_len));
				return -1;
			}
			/* We have to convert the buffer data into ASCII strings */
			for (i = 0; i < len; i++) {
				snprintf(buf, 3, "%02x", _buf[i]);
				buf += 2;
			}
			buf[i*2] = '\0';
		}
	}
	else {
		CFGP2P_ERR(("ERROR: get_noa in non-p2p mode\n"));
		return -1;
	}
	return len * 2;
}
s32
wl_cfgp2p_set_p2p_ps(struct bcm_cfg80211 *cfg, struct net_device *ndev, char* buf, int len)
{
	int ps, ctw;
	int ret = -1;
	s32 legacy_ps;
	struct net_device *dev;

	CFGP2P_DBG((" Enter\n"));
	if (cfg->p2p && cfg->p2p->vif_created) {
		sscanf(buf, "%10d %10d %10d", &legacy_ps, &ps, &ctw);
		CFGP2P_DBG((" Enter legacy_ps %d ps %d ctw %d\n", legacy_ps, ps, ctw));
		dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION);
		if (ctw != -1) {
			cfg->p2p->ops.ctw = ctw;
			ret = 0;
		}
		if (ps != -1) {
			cfg->p2p->ops.ops = ps;
			ret = wldev_iovar_setbuf(dev,
				"p2p_ops", &cfg->p2p->ops, sizeof(cfg->p2p->ops),
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
			if (ret < 0) {
				CFGP2P_ERR(("fw set p2p_ops failed %d\n", ret));
			}
		}

		if ((legacy_ps != -1) && ((legacy_ps == PM_MAX) || (legacy_ps == PM_OFF))) {
			ret = wldev_ioctl(dev,
				WLC_SET_PM, &legacy_ps, sizeof(legacy_ps), true);
			if (unlikely(ret))
				CFGP2P_ERR(("error (%d)\n", ret));
			wl_cfg80211_update_power_mode(dev);
		}
		else
			CFGP2P_ERR(("ilegal setting\n"));
	}
	else {
		CFGP2P_ERR(("ERROR: set_p2p_ps in non-p2p mode\n"));
		ret = -1;
	}
	return ret;
}

u8 *
wl_cfgp2p_retreive_p2pattrib(void *buf, u8 element_id)
{
	wifi_p2p_ie_t *ie = NULL;
	u16 len = 0;
	u8 *subel;
	u8 subelt_id;
	u16 subelt_len;

	if (!buf) {
		WL_ERR(("P2P IE not present"));
		return 0;
	}

	ie = (wifi_p2p_ie_t*) buf;
	len = ie->len;

	/* Point subel to the P2P IE's subelt field.
	 * Subtract the preceding fields (id, len, OUI, oui_type) from the length.
	 */
	subel = ie->subelts;
	len -= 4;	/* exclude OUI + OUI_TYPE */

	while (len >= 3) {
		/* attribute id */
		subelt_id = *subel;
		subel += 1;
		len -= 1;

		/* 2-byte little endian */
		subelt_len = *subel++;
		subelt_len |= *subel++ << 8;

		len -= 2;
		len -= subelt_len;	/* for the remaining subelt fields */

		if (subelt_id == element_id) {
			/* This will point to start of subelement attrib after
			 * attribute id & len
			 */
			return subel;
		}

		/* Go to next subelement */
		subel += subelt_len;
	}

	/* Not Found */
	return NULL;
}

#define P2P_GROUP_CAPAB_GO_BIT	0x01

u8*
wl_cfgp2p_find_attrib_in_all_p2p_Ies(u8 *parse, u32 len, u32 attrib)
{
	bcm_tlv_t *ie;
	u8* pAttrib;

	CFGP2P_INFO(("Starting parsing parse %p attrib %d remaining len %d ", parse, attrib, len));
	while ((ie = bcm_parse_tlvs(parse, (int)len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_p2p_ie((uint8*)ie, &parse, &len) == TRUE) {
			/* Have the P2p ie. Now check for attribute */
			if ((pAttrib = wl_cfgp2p_retreive_p2pattrib(parse, attrib)) != NULL) {
				CFGP2P_INFO(("P2P attribute %d was found at parse %p",
					attrib, parse));
				return pAttrib;
			}
			else {
				parse += (ie->len + TLV_HDR_LEN);
				len -= (ie->len + TLV_HDR_LEN);
				CFGP2P_INFO(("P2P Attribute %d not found Moving parse"
					" to %p len to %d", attrib, parse, len));
			}
		}
		else {
			/* It was not p2p IE. parse will get updated automatically to next TLV */
			CFGP2P_INFO(("IT was NOT P2P IE parse %p len %d", parse, len));
		}
	}
	CFGP2P_ERR(("P2P attribute %d was NOT found", attrib));
	return NULL;
}

u8 *
wl_cfgp2p_retreive_p2p_dev_addr(wl_bss_info_t *bi, u32 bi_length)
{
	u8 *capability = NULL;
	bool p2p_go	= 0;
	u8 *ptr = NULL;

	if ((capability = wl_cfgp2p_find_attrib_in_all_p2p_Ies(((u8 *) bi) + bi->ie_offset,
	bi->ie_length, P2P_SEID_P2P_INFO)) == NULL) {
		WL_ERR(("P2P Capability attribute not found"));
		return NULL;
	}

	/* Check Group capability for Group Owner bit */
	p2p_go = capability[1] & P2P_GROUP_CAPAB_GO_BIT;
	if (!p2p_go) {
		return bi->BSSID.octet;
	}

	/* In probe responses, DEVICE INFO attribute will be present */
	if (!(ptr = wl_cfgp2p_find_attrib_in_all_p2p_Ies(((u8 *) bi) + bi->ie_offset,
	bi->ie_length,  P2P_SEID_DEV_INFO))) {
		/* If DEVICE_INFO is not found, this might be a beacon frame.
		 * check for DEVICE_ID in the beacon frame.
		 */
		ptr = wl_cfgp2p_find_attrib_in_all_p2p_Ies(((u8 *) bi) + bi->ie_offset,
		bi->ie_length,  P2P_SEID_DEV_ID);
	}

	if (!ptr)
		WL_ERR((" Both DEVICE_ID & DEVICE_INFO attribute not present in P2P IE "));

	return ptr;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
static void
wl_cfgp2p_ethtool_get_drvinfo(struct net_device *net, struct ethtool_drvinfo *info)
{
	snprintf(info->driver, sizeof(info->driver), "p2p");
	snprintf(info->version, sizeof(info->version), "%lu", (unsigned long)(0));
}

struct ethtool_ops cfgp2p_ethtool_ops = {
	.get_drvinfo = wl_cfgp2p_ethtool_get_drvinfo
};
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) */

#if defined(WL_ENABLE_P2P_IF) || defined(P2PONEINT)
#ifdef  P2PONEINT
s32
wl_cfgp2p_register_ndev(struct bcm_cfg80211 *cfg)
{

	struct net_device *_ndev;
	struct ether_addr primary_mac;
	struct net_device *new_ndev;
	chanspec_t chspec;
	uint8 name[IFNAMSIZ];
	s32 mode = 0;
	s32 val = 0;


	s32 wlif_type = -1;
	s32 err, timeout = -1;

	memset(name, 0, IFNAMSIZ);
	strncpy(name, "p2p0", 4);
	name[IFNAMSIZ - 1] = '\0';

	if (cfg->p2p_net) {
		CFGP2P_ERR(("p2p_net defined already.\n"));
		return -EINVAL;
	}

	if (!cfg->p2p)
		return -EINVAL;

	if (cfg->p2p && !cfg->p2p->on && strstr(name, WL_P2P_INTERFACE_PREFIX)) {
		p2p_on(cfg) = true;
		wl_cfgp2p_set_firm_p2p(cfg);
		wl_cfgp2p_init_discovery(cfg);
		get_primary_mac(cfg, &primary_mac);
		wl_cfgp2p_generate_bss_mac(&primary_mac,
			&cfg->p2p->dev_addr, &cfg->p2p->int_addr);
	}

	_ndev = bcmcfg_to_prmry_ndev(cfg);
	memset(cfg->p2p->vir_ifname, 0, IFNAMSIZ);
	strncpy(cfg->p2p->vir_ifname, name, IFNAMSIZ - 1);

	wl_cfg80211_scan_abort(cfg);


	/* In concurrency case, STA may be already associated in a particular channel.
	 * so retrieve the current channel of primary interface and then start the virtual
	 * interface on that.
	 */
	chspec = wl_cfg80211_get_shared_freq(cfg->wdev->wiphy);

	/* For P2P mode, use P2P-specific driver features to create the
	 * bss: "cfg p2p_ifadd"
	 */
	wl_set_p2p_status(cfg, IF_ADDING);
	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));
	wlif_type = WL_P2P_IF_CLIENT;


	err = wl_cfgp2p_ifadd(cfg, &cfg->p2p->int_addr, htod32(wlif_type), chspec);
	if (unlikely(err)) {
		wl_clr_p2p_status(cfg, IF_ADDING);
		WL_ERR((" virtual iface add failed (%d) \n", err));
		return -ENOMEM;
	}

	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		(wl_get_p2p_status(cfg, IF_ADDING) == false),
		msecs_to_jiffies(MAX_WAIT_TIME));


	if (timeout > 0 && !wl_get_p2p_status(cfg, IF_ADDING) && cfg->if_event_info.valid) {
		struct wireless_dev *vwdev;
		int pm_mode = PM_ENABLE;
		wl_if_event_info *event = &cfg->if_event_info;

		/* IF_ADD event has come back, we can proceed to to register
		 * the new interface now, use the interface name provided by caller (thus
		 * ignore the one from wlc)
		 */
		strncpy(cfg->if_event_info.name, name, IFNAMSIZ - 1);
		new_ndev = wl_cfg80211_allocate_if(cfg, event->ifidx, cfg->p2p->vir_ifname,
			event->mac, event->bssidx);
		if (new_ndev == NULL)
			goto fail;

		wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION) = new_ndev;
		wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION) = event->bssidx;

		vwdev = kzalloc(sizeof(*vwdev), GFP_KERNEL);
		if (unlikely(!vwdev)) {
			WL_ERR(("Could not allocate wireless device\n"));
			goto fail;
		}
		vwdev->wiphy = cfg->wdev->wiphy;
		WL_TRACE(("virtual interface(%s) is created\n", cfg->p2p->vir_ifname));
		vwdev->iftype = NL80211_IFTYPE_P2P_DEVICE;
		vwdev->netdev = new_ndev;
		new_ndev->ieee80211_ptr = vwdev;
		SET_NETDEV_DEV(new_ndev, wiphy_dev(vwdev->wiphy));
		wl_set_drv_status(cfg, READY, new_ndev);
		cfg->p2p->vif_created = true;
		wl_set_mode_by_netdev(cfg, new_ndev, mode);

		if (wl_cfg80211_register_if(cfg, event->ifidx, new_ndev) != BCME_OK) {
			wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev);
			goto fail;
		}

		wl_alloc_netinfo(cfg, new_ndev, vwdev, mode, pm_mode);
		val = 1;
		/* Disable firmware roaming for P2P interface  */
		wldev_iovar_setint(new_ndev, "roam_off", val);

		if (mode != WL_MODE_AP)
			wldev_iovar_setint(new_ndev, "buf_key_b4_m4", 1);

		WL_ERR((" virtual interface(%s) is "
					"created net attach done\n", cfg->p2p->vir_ifname));

		/* reinitialize completion to clear previous count */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
		INIT_COMPLETION(cfg->iface_disable);
#else
		init_completion(&cfg->iface_disable);
#endif
		cfg->p2p_net = new_ndev;
		cfg->p2p_wdev = vwdev;

		return 0;
	} else {
		wl_clr_p2p_status(cfg, IF_ADDING);
		WL_ERR((" virtual interface(%s) is not created \n", cfg->p2p->vir_ifname));
		memset(cfg->p2p->vir_ifname, '\0', IFNAMSIZ);
		cfg->p2p->vif_created = false;
	}


fail:
	if (wlif_type == WL_P2P_IF_GO)
		wldev_iovar_setint(_ndev, "mpc", 1);
	return -ENODEV;

}
#else
s32
wl_cfgp2p_register_ndev(struct bcm_cfg80211 *cfg)
{
	int ret = 0;
	struct net_device* net = NULL;
	struct wireless_dev *wdev = NULL;
	uint8 temp_addr[ETHER_ADDR_LEN] = { 0x00, 0x90, 0x4c, 0x33, 0x22, 0x11 };

	if (cfg->p2p_net) {
		CFGP2P_ERR(("p2p_net defined already.\n"));
		return -EINVAL;
	}

	/* Allocate etherdev, including space for private structure */
	if (!(net = alloc_etherdev(sizeof(struct bcm_cfg80211 *)))) {
		CFGP2P_ERR(("%s: OOM - alloc_etherdev\n", __FUNCTION__));
		return -ENODEV;
	}

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (unlikely(!wdev)) {
		WL_ERR(("Could not allocate wireless device\n"));
		free_netdev(net);
		return -ENOMEM;
	}

	strncpy(net->name, "p2p%d", sizeof(net->name) - 1);
	net->name[IFNAMSIZ - 1] = '\0';

	/* Copy the reference to bcm_cfg80211 */
	memcpy((void *)netdev_priv(net), &cfg, sizeof(struct bcm_cfg80211 *));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31))
	ASSERT(!net->open);
	net->do_ioctl = wl_cfgp2p_do_ioctl;
	net->hard_start_xmit = wl_cfgp2p_start_xmit;
	net->open = wl_cfgp2p_if_open;
	net->stop = wl_cfgp2p_if_stop;
#else
	ASSERT(!net->netdev_ops);
	net->netdev_ops = &wl_cfgp2p_if_ops;
#endif

	/* Register with a dummy MAC addr */
	memcpy(net->dev_addr, temp_addr, ETHER_ADDR_LEN);

	wdev->wiphy = cfg->wdev->wiphy;

	wdev->iftype = wl_mode_to_nl80211_iftype(WL_MODE_BSS);

	net->ieee80211_ptr = wdev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	net->ethtool_ops = &cfgp2p_ethtool_ops;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) */

	SET_NETDEV_DEV(net, wiphy_dev(wdev->wiphy));

	/* Associate p2p0 network interface with new wdev */
	wdev->netdev = net;

	ret = register_netdev(net);
	if (ret) {
		CFGP2P_ERR((" register_netdevice failed (%d)\n", ret));
		free_netdev(net);
		kfree(wdev);
		return -ENODEV;
	}

	/* store p2p net ptr for further reference. Note that iflist won't have this
	 * entry as there corresponding firmware interface is a "Hidden" interface.
	 */
	cfg->p2p_wdev = wdev;
	cfg->p2p_net = net;

	printk("%s: P2P Interface Registered\n", net->name);

	return ret;
}
#endif /* P2PONEINT */

s32
wl_cfgp2p_unregister_ndev(struct bcm_cfg80211 *cfg)
{

	if (!cfg || !cfg->p2p_net) {
		CFGP2P_ERR(("Invalid Ptr\n"));
		return -EINVAL;
	}

	unregister_netdev(cfg->p2p_net);
	free_netdev(cfg->p2p_net);

	return 0;
}

#ifndef  P2PONEINT
static int wl_cfgp2p_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{

	if (skb)
	{
		CFGP2P_DBG(("(%s) is not used for data operations.Droping the packet.\n",
			ndev->name));
		dev_kfree_skb_any(skb);
	}

	return 0;
}

static int wl_cfgp2p_do_ioctl(struct net_device *net, struct ifreq *ifr, int cmd)
{
	int ret = 0;
	struct bcm_cfg80211 *cfg = *(struct bcm_cfg80211 **)netdev_priv(net);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);

	/* There is no ifidx corresponding to p2p0 in our firmware. So we should
	 * not Handle any IOCTL cmds on p2p0 other than ANDROID PRIVATE CMDs.
	 * For Android PRIV CMD handling map it to primary I/F
	 */
	if (cmd == SIOCDEVPRIVATE+1) {
		ret = wl_android_priv_cmd(ndev, ifr, cmd);

	} else {
		CFGP2P_ERR(("%s: IOCTL req 0x%x on p2p0 I/F. Ignoring. \n",
		__FUNCTION__, cmd));
		return -1;
	}

	return ret;
}
#endif /*  P2PONEINT */
#endif 


#if defined(WL_ENABLE_P2P_IF) || defined(P2PONEINT)
#ifdef  P2PONEINT
int wl_cfgp2p_if_open(struct net_device *net)
#else
static int wl_cfgp2p_if_open(struct net_device *net)
#endif /*  P2PONEINT */
{
	struct wireless_dev *wdev = net->ieee80211_ptr;

	if (!wdev || !wl_cfg80211_is_p2p_active())
		return -EINVAL;
	WL_TRACE(("Enter\n"));
#if !defined(WL_IFACE_COMB_NUM_CHANNELS)
	/* If suppose F/W download (ifconfig wlan0 up) hasn't been done by now,
	 * do it here. This will make sure that in concurrent mode, supplicant
	 * is not dependent on a particular order of interface initialization.
	 * i.e you may give wpa_supp -iwlan0 -N -ip2p0 or wpa_supp -ip2p0 -N
	 * -iwlan0.
	 */
	wdev->wiphy->interface_modes |= (BIT(NL80211_IFTYPE_P2P_CLIENT)
		| BIT(NL80211_IFTYPE_P2P_GO));
#endif /* !WL_IFACE_COMB_NUM_CHANNELS */
	wl_cfg80211_do_driver_init(net);

	return 0;
}

#ifdef  P2PONEINT
int wl_cfgp2p_if_stop(struct net_device *net)
#else
static int wl_cfgp2p_if_stop(struct net_device *net)
#endif
{
	struct wireless_dev *wdev = net->ieee80211_ptr;
#ifdef P2PONEINT
	bcm_struct_cfgdev *cfgdev;
#endif
	if (!wdev)
		return -EINVAL;

#ifdef P2PONEINT
	cfgdev = ndev_to_cfgdev(net);
	wl_cfg80211_scan_stop(cfgdev);
#else
	wl_cfg80211_scan_stop(net);
#endif

#if !defined(WL_IFACE_COMB_NUM_CHANNELS)
	wdev->wiphy->interface_modes = (wdev->wiphy->interface_modes)
					& (~(BIT(NL80211_IFTYPE_P2P_CLIENT)|
					BIT(NL80211_IFTYPE_P2P_GO)));
#endif /* !WL_IFACE_COMB_NUM_CHANNELS */

	return 0;
}
#endif /* defined(WL_ENABLE_P2P_IF) || defined(P2PONEINT) */

#if defined(WL_ENABLE_P2P_IF)
bool wl_cfgp2p_is_ifops(const struct net_device_ops *if_ops)
{
	return (if_ops == &wl_cfgp2p_if_ops);
}
#endif /* WL_ENABLE_P2P_IF */

#if defined(WL_CFG80211_P2P_DEV_IF)
struct wireless_dev *
wl_cfgp2p_add_p2p_disc_if(struct bcm_cfg80211 *cfg)
{
	struct wireless_dev *wdev = NULL;
	struct ether_addr primary_mac;

	if (!cfg)
		return ERR_PTR(-EINVAL);

	WL_TRACE(("Enter\n"));

	if (cfg->p2p_wdev) {
		CFGP2P_ERR(("p2p_wdev defined already.\n"));
		wl_cfgp2p_del_p2p_disc_if(cfg->p2p_wdev, cfg);
		CFGP2P_ERR(("p2p_wdev deleted.\n"));
	}

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (unlikely(!wdev)) {
		WL_ERR(("Could not allocate wireless device\n"));
		return ERR_PTR(-ENOMEM);
	}

	memset(&primary_mac, 0, sizeof(primary_mac));
	get_primary_mac(cfg, &primary_mac);
	wl_cfgp2p_generate_bss_mac(&primary_mac,
		&cfg->p2p->dev_addr, &cfg->p2p->int_addr);

	wdev->wiphy = cfg->wdev->wiphy;
	wdev->iftype = NL80211_IFTYPE_P2P_DEVICE;
	memcpy(wdev->address, &cfg->p2p->dev_addr, ETHER_ADDR_LEN);


	/* store p2p wdev ptr for further reference. */
	cfg->p2p_wdev = wdev;

	CFGP2P_ERR(("P2P interface registered\n"));


	return wdev;
}

int
wl_cfgp2p_start_p2p_device(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	int ret = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

	if (!cfg)
		return -EINVAL;

	WL_TRACE(("Enter\n"));

	ret = wl_cfgp2p_set_firm_p2p(cfg);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR(("Set P2P in firmware failed, ret=%d\n", ret));
		goto exit;
	}

	ret = wl_cfgp2p_enable_discovery(cfg, bcmcfg_to_prmry_ndev(cfg), NULL, 0);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR(("P2P enable discovery failed, ret=%d\n", ret));
		goto exit;
	}

	p2p_on(cfg) = true;

	CFGP2P_DBG(("P2P interface started\n"));

exit:
	return ret;
}

void
wl_cfgp2p_stop_p2p_device(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	int ret = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

	if (!cfg)
		return;

	WL_TRACE(("Enter\n"));

	ret = wl_cfg80211_scan_stop(wdev);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR(("P2P scan stop failed, ret=%d\n", ret));
	}

	if (!cfg->p2p)
		return;

	ret = wl_cfgp2p_disable_discovery(cfg);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR(("P2P disable discovery failed, ret=%d\n", ret));
	}

	p2p_on(cfg) = false;

	CFGP2P_DBG(("P2P interface stopped\n"));

	return;
}


int
wl_cfgp2p_del_p2p_disc_if(struct wireless_dev *wdev, struct bcm_cfg80211 *cfg)
{
	bool rollback_lock = false;

	if (!wdev)
		return -EINVAL;

#ifdef P2PONEINT
	return -EINVAL;
#endif

	WL_TRACE(("Enter\n"));

	if (!rtnl_is_locked()) {
		rtnl_lock();
		rollback_lock = true;
	}

	cfg80211_unregister_wdev(wdev);

	if (rollback_lock)
		rtnl_unlock();

	synchronize_rcu();

	kfree(wdev);

	if (cfg)
		cfg->p2p_wdev = NULL;

	CFGP2P_ERR(("P2P interface unregistered\n"));


	return 0;
}
#endif /* WL_CFG80211_P2P_DEV_IF */

void
wl_cfgp2p_need_wait_actfrmae(struct bcm_cfg80211 *cfg, void *frame, u32 frame_len, bool tx)
{
	wifi_p2p_pub_act_frame_t *pact_frm;
	int status = 0;

	if (!frame || (frame_len < (sizeof(*pact_frm) + WL_P2P_AF_STATUS_OFFSET - 1))) {
		return;
	}

	if (wl_cfgp2p_is_pub_action(frame, frame_len)) {
		pact_frm = (wifi_p2p_pub_act_frame_t *)frame;
		if (pact_frm->subtype == P2P_PAF_GON_RSP && tx) {
			CFGP2P_ACTION(("Check TX P2P Group Owner Negotiation Rsp Frame status\n"));
			status = pact_frm->elts[WL_P2P_AF_STATUS_OFFSET];
			if (status) {
				cfg->need_wait_afrx = false;
				return;
			}
		}
	}

	cfg->need_wait_afrx = true;
	return;
}

int
wl_cfgp2p_is_p2p_specific_scan(struct cfg80211_scan_request *request)
{
	if (request && (request->n_ssids == 1) &&
		(request->n_channels == 1) &&
		IS_P2P_SSID(request->ssids[0].ssid, WL_P2P_WILDCARD_SSID_LEN) &&
		(request->ssids[0].ssid_len > WL_P2P_WILDCARD_SSID_LEN)) {
		return true;
	}
	return false;
}
