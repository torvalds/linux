/*
 * Linux cfgp2p driver
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
 * $Id: wl_cfgp2p.c 815562 2019-04-18 02:33:27Z $
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
#include <bcmstdlib_s.h>
#include <bcmendian.h>
#include <ethernet.h>
#include <802.11.h>
#include <net/rtnetlink.h>

#include <wl_cfg80211.h>
#include <wl_cfgp2p.h>
#include <wl_cfgscan.h>
#include <wldev_common.h>
#ifdef OEM_ANDROID
#include <wl_android.h>
#endif // endif
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_linux.h>
#include <dhdioctl.h>
#include <wlioctl.h>
#include <dhd_cfg80211.h>
#include <dhd_bus.h>

static s8 scanparambuf[WLC_IOCTL_SMLEN];
static bool wl_cfgp2p_has_ie(const bcm_tlv_t *ie, const u8 **tlvs, u32 *tlvs_len,
                             const u8 *oui, u32 oui_len, u8 type);

static s32 wl_cfgp2p_cancel_listen(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	struct wireless_dev *wdev, bool notify);

#if defined(WL_ENABLE_P2P_IF)
static int wl_cfgp2p_start_xmit(struct sk_buff *skb, struct net_device *ndev);
static int wl_cfgp2p_do_ioctl(struct net_device *net, struct ifreq *ifr, int cmd);
static int wl_cfgp2p_if_open(struct net_device *net);
static int wl_cfgp2p_if_stop(struct net_device *net);

static const struct net_device_ops wl_cfgp2p_if_ops = {
	.ndo_open       = wl_cfgp2p_if_open,
	.ndo_stop       = wl_cfgp2p_if_stop,
	.ndo_do_ioctl   = wl_cfgp2p_do_ioctl,
	.ndo_start_xmit = wl_cfgp2p_start_xmit,
};
#endif /* WL_ENABLE_P2P_IF */

#if defined(WL_NEWCFG_PRIVCMD_SUPPORT)
static int wl_cfgp2p_start_xmit(struct sk_buff *skb, struct net_device *ndev);
static int wl_cfgp2p_do_ioctl(struct net_device *net, struct ifreq *ifr, int cmd);

static int wl_cfgp2p_if_dummy(struct net_device *net)
{
	return 0;
}

static const struct net_device_ops wl_cfgp2p_if_ops = {
	.ndo_open       = wl_cfgp2p_if_dummy,
	.ndo_stop       = wl_cfgp2p_if_dummy,
	.ndo_do_ioctl   = wl_cfgp2p_do_ioctl,
	.ndo_start_xmit = wl_cfgp2p_start_xmit,
};
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */

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
	const bcm_tlv_t *ie = (bcm_tlv_t *)data;
	const u8 *frame = NULL;
	u16 id, flen;

	/* Skipped first ANQP Element, if frame has anqp elemnt */
	ie = bcm_parse_tlvs(ie, len, DOT11_MNG_ADVERTISEMENT_ID);

	if (ie == NULL)
		return false;

	frame = (const uint8 *)ie + ie->len + TLV_HDR_LEN + GAS_RESP_LEN;
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

#ifdef WL11U
	if (sd_act_frm->action == P2PSD_ACTION_ID_GAS_IRESP)
		return wl_cfgp2p_find_gas_subtype(P2PSD_GAS_OUI_SUBTYPE,
			(u8 *)sd_act_frm->query_data + GAS_RESP_OFFSET,
			frame_len);

	else if (sd_act_frm->action == P2PSD_ACTION_ID_GAS_CRESP)
		return wl_cfgp2p_find_gas_subtype(P2PSD_GAS_OUI_SUBTYPE,
			(u8 *)sd_act_frm->query_data + GAS_CRESP_OFFSET,
			frame_len);
	else if (sd_act_frm->action == P2PSD_ACTION_ID_GAS_IREQ ||
		sd_act_frm->action == P2PSD_ACTION_ID_GAS_CREQ)
		return true;
	else
		return false;
#else
	if (sd_act_frm->action == P2PSD_ACTION_ID_GAS_IREQ ||
		sd_act_frm->action == P2PSD_ACTION_ID_GAS_IRESP ||
		sd_act_frm->action == P2PSD_ACTION_ID_GAS_CREQ ||
		sd_act_frm->action == P2PSD_ACTION_ID_GAS_CRESP)
		return true;
	else
		return false;
#endif /* WL11U */
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
				CFGP2P_ACTION(("%s Unknown Public Action Frame,"
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
				CFGP2P_ACTION(("%s GAS Initial Request,"
					" channel=%d\n", (tx)? "TX" : "RX", channel));
				break;
			case P2PSD_ACTION_ID_GAS_IRESP:
				CFGP2P_ACTION(("%s GAS Initial Response,"
					" channel=%d\n", (tx)? "TX" : "RX", channel));
				break;
			case P2PSD_ACTION_ID_GAS_CREQ:
				CFGP2P_ACTION(("%s GAS Comback Request,"
					" channel=%d\n", (tx)? "TX" : "RX", channel));
				break;
			case P2PSD_ACTION_ID_GAS_CRESP:
				CFGP2P_ACTION(("%s GAS Comback Response,"
					" channel=%d\n", (tx)? "TX" : "RX", channel));
				break;
			default:
				CFGP2P_ACTION(("%s Unknown GAS Frame,"
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
	cfg->p2p = MALLOCZ(cfg->osh, sizeof(struct p2p_info));
	if (cfg->p2p == NULL) {
		CFGP2P_ERR(("struct p2p_info allocation failed\n"));
		return -ENOMEM;
	}

	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY) = bcmcfg_to_prmry_ndev(cfg);
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_PRIMARY) = 0;
	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE) = NULL;
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = 0;
	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION1) = NULL;
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION1) = -1;
	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION2) = NULL;
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION2) = -1;
	return BCME_OK;

}
/*
 *  Deinitialize variables related to P2P
 *
 */
void
wl_cfgp2p_deinit_priv(struct bcm_cfg80211 *cfg)
{
	CFGP2P_INFO(("In\n"));
	if (cfg->p2p) {
		MFREE(cfg->osh, cfg->p2p, sizeof(struct p2p_info));
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
		ret = wldev_ioctl_set(ndev, WLC_DOWN, &val, sizeof(s32));
		if (ret < 0) {
			CFGP2P_ERR(("WLC_DOWN error %d\n", ret));
			return ret;
		}

		ret = wldev_iovar_setint(ndev, "apsta", val);
		if (ret < 0) {
			/* return error and fail the initialization */
			CFGP2P_ERR(("wl apsta %d set error. ret: %d\n", val, ret));
			return ret;
		}

		ret = wldev_ioctl_set(ndev, WLC_UP, &val, sizeof(s32));
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

int wl_cfg_multip2p_operational(struct bcm_cfg80211 *cfg)
{
	if (!cfg->p2p) {
		CFGP2P_DBG(("p2p not enabled! \n"));
		return false;
	}

	if ((wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION1) != -1) &&
	    (wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION2) != -1))
		return true;
	else
		return false;
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
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);

	ifreq.type = if_type;
	ifreq.chspec = chspec;
	memcpy(ifreq.addr.octet, mac->octet, sizeof(ifreq.addr.octet));

	CFGP2P_ERR(("---cfg p2p_ifadd "MACDBG" %s %u\n",
		MAC2STRDBG(ifreq.addr.octet),
		(if_type == WL_P2P_IF_GO) ? "go" : "client",
	        (chspec & WL_CHANSPEC_CHAN_MASK) >> WL_CHANSPEC_CHAN_SHIFT));

	err = wldev_iovar_setbuf(ndev, "p2p_ifadd", &ifreq, sizeof(ifreq),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (unlikely(err < 0)) {
		printk("'cfg p2p_ifadd' error %d\n", err);
		return err;
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

	CFGP2P_INFO(("------ cfg p2p_ifdis "MACDBG" dev->ifindex:%d \n",
		MAC2STRDBG(mac->octet), netdev->ifindex));
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
#ifdef WL_DISABLE_HE_P2P
	s32 bssidx = 0;
#endif /* WL_DISABLE_HE_P2P */
	struct net_device *netdev = bcmcfg_to_prmry_ndev(cfg);

	CFGP2P_ERR(("------ cfg p2p_ifdel "MACDBG" dev->ifindex:%d\n",
	    MAC2STRDBG(mac->octet), netdev->ifindex));
	ret = wldev_iovar_setbuf(netdev, "p2p_ifdel", mac, sizeof(*mac),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (unlikely(ret < 0)) {
		printk("'cfg p2p_ifdel' error %d\n", ret);
	}
#ifdef WL_DISABLE_HE_P2P
	if ((bssidx = wl_get_bssidx_by_wdev(cfg, netdev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find index failed\n"));
		ret = BCME_ERROR;
		return ret;
	}
	WL_DBG(("Enabling back HE for P2P\n"));
	wl_cfg80211_set_he_mode(netdev, cfg, bssidx, WL_IF_TYPE_P2P_DISC, TRUE);
	if (ret < 0) {
		WL_ERR(("failed to set he features, error=%d\n", ret));
	}
#endif /* WL_DISABLE_HE_P2P */

	return ret;
}

/* Change a P2P Role.
 * Parameters:
 * @mac      : MAC address of the BSS to change a role
 * Returns 0 if success.
 */
s32
wl_cfgp2p_ifchange(struct bcm_cfg80211 *cfg, struct ether_addr *mac, u8 if_type,
            chanspec_t chspec, s32 conn_idx)
{
	wl_p2p_if_t ifreq;
	s32 err;

	struct net_device *netdev =  wl_to_p2p_bss_ndev(cfg, conn_idx);

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
		cfg->p2p->p2p_go_count++;
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
		CFGP2P_DBG(("---cfg p2p_if   ==> %d\n", *index));
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

#ifdef P2PLISTEN_AP_SAMECHN
	CFGP2P_DBG(("p2p0 listen channel %d  AP connection chan %d \n",
		channel, cfg->channel));
	if ((mode == WL_P2P_DISC_ST_LISTEN) && (cfg->channel == channel)) {
		struct net_device *primary_ndev = bcmcfg_to_prmry_ndev(cfg);

		if (cfg->p2p_resp_apchn_status) {
			CFGP2P_DBG(("p2p_resp_apchn_status already ON \n"));
			return BCME_OK;
		}

		if (wl_get_drv_status(cfg, CONNECTED, primary_ndev)) {
			ret = wl_cfg80211_set_p2p_resp_ap_chn(primary_ndev, 1);
			cfg->p2p_resp_apchn_status = true;
			CFGP2P_DBG(("p2p_resp_apchn_status ON \n"));
			return ret;
		}
	}
#endif /* P2PLISTEN_AP_SAMECHN */

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

int wl_cfgp2p_get_conn_idx(struct bcm_cfg80211 *cfg)
{
	int i;
	s32 connected_cnt;
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	if (!dhd)
		return (-ENODEV);
	for (i = P2PAPI_BSSCFG_CONNECTION1; i < P2PAPI_BSSCFG_MAX; i++) {
		if (wl_to_p2p_bss_bssidx(cfg, i) == -1) {
			if (i == P2PAPI_BSSCFG_CONNECTION2) {
				if (!(dhd->op_mode & DHD_FLAG_MP2P_MODE)) {
					CFGP2P_ERR(("Multi p2p not supported"));
					return BCME_ERROR;
				}
				if ((connected_cnt = wl_get_drv_status_all(cfg, CONNECTED)) > 2) {
					CFGP2P_ERR(("Failed to create second p2p interface"
						"Already one connection exists"));
					return BCME_ERROR;
				}
			}
		return i;
		}
	}
	return BCME_ERROR;
}

s32
wl_cfgp2p_init_discovery(struct bcm_cfg80211 *cfg)
{

	s32 bssidx = 0;
	s32 ret = BCME_OK;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);

	BCM_REFERENCE(ndev);
	CFGP2P_DBG(("enter\n"));

	if (wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) > 0) {
		CFGP2P_ERR(("do nothing, already initialized\n"));
		goto exit;
	}

	ret = wl_cfgp2p_set_discovery(cfg, 1);
	if (ret < 0) {
		CFGP2P_ERR(("set discover error\n"));
		goto exit;
	}
	/* Enable P2P Discovery in the WL Driver */
	ret = wl_cfgp2p_get_disc_idx(cfg, &bssidx);
	if (ret < 0) {
		goto exit;
	}

	/* In case of CFG80211 case, check if p2p_discovery interface has allocated p2p_wdev */
	if (!cfg->p2p_wdev) {
		CFGP2P_ERR(("p2p_wdev is NULL.\n"));
		ret = -ENODEV;
		goto exit;
	}

	/* Once p2p also starts using interface_create iovar, the ifidx may change.
	 * so that time, the ifidx returned in WLC_E_IF should be used for populating
	 * the netinfo
	 */
	ret = wl_alloc_netinfo(cfg, NULL, cfg->p2p_wdev, WL_IF_TYPE_STA, 0, bssidx, 0);
	if (unlikely(ret)) {
		goto exit;
	}
	wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE) =
	    wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY);
	wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = bssidx;

	/* Set the initial discovery state to SCAN */
	ret = wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
		wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));

	if (unlikely(ret != 0)) {
		CFGP2P_ERR(("unable to set WL_P2P_DISC_ST_SCAN\n"));
		wl_cfgp2p_set_discovery(cfg, 0);
		wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = 0;
		wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE) = NULL;
		ret = 0;
		goto exit;
	}

	/* Clear our saved WPS and P2P IEs for the discovery BSS */
	wl_cfg80211_clear_p2p_disc_ies(cfg);
exit:
	if (ret) {
		wl_flush_fw_log_buffer(ndev, FW_LOGSET_MASK_ALL);
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
	s32 bssidx;

	CFGP2P_DBG(("enter\n"));
	bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	if (bssidx <= 0) {
		CFGP2P_ERR(("do nothing, not initialized\n"));
		return -1;
	}

	/* Clear our saved WPS and P2P IEs for the discovery BSS */
	wl_cfg80211_clear_p2p_disc_ies(cfg);

	/* Set the discovery state to SCAN */
	wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
	            bssidx);
	/* Disable P2P discovery in the WL driver (deletes the discovery BSSCFG) */
	ret = wl_cfgp2p_set_discovery(cfg, 0);

	/* Remove the p2p disc entry in the netinfo */
	wl_dealloc_netinfo_by_wdev(cfg, cfg->p2p_wdev);

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
	bcm_struct_cfgdev *cfgdev;

	CFGP2P_DBG(("enter\n"));
	mutex_lock(&cfg->if_sync);
#ifdef WL_IFACE_MGMT
	if ((ret = wl_cfg80211_handle_if_role_conflict(cfg, WL_IF_TYPE_P2P_DISC)) != BCME_OK) {
		WL_ERR(("secondary iface is active, p2p enable discovery is not supported\n"));
		goto exit;
	}
#endif /* WL_IFACE_MGMT */

	if (wl_get_p2p_status(cfg, DISCOVERY_ON)) {
		CFGP2P_DBG((" DISCOVERY is already initialized, we have nothing to do\n"));
		goto set_ie;
	}

	ret = wl_cfgp2p_init_discovery(cfg);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR((" init discovery error %d\n", ret));
		goto exit;
	}

	wl_set_p2p_status(cfg, DISCOVERY_ON);
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
		} else if ((bssidx = wl_get_bssidx_by_wdev(cfg, cfg->p2p_wdev)) < 0) {
			WL_ERR(("Find p2p index from wdev(%p) failed\n", cfg->p2p_wdev));
			ret = BCME_ERROR;
			goto exit;
		}

#if defined(WL_CFG80211_P2P_DEV_IF)
		/* For 3.8+ kernels, pass p2p discovery wdev */
		cfgdev = cfg->p2p_wdev;
#else
		/* Prior to 3.8 kernel, there is no netless p2p, so pass p2p0 ndev */
		cfgdev = ndev_to_cfgdev(dev);
#endif /* WL_CFG80211_P2P_DEV_IF */
		ret = wl_cfg80211_set_mgmt_vndr_ies(cfg, cfgdev,
				bssidx, VNDR_IE_PRBREQ_FLAG, ie, ie_len);
		if (unlikely(ret < 0)) {
			CFGP2P_ERR(("set probreq ie occurs error %d\n", ret));
			goto exit;
		}
	}
exit:
	if (ret) {
		wl_flush_fw_log_buffer(dev, FW_LOGSET_MASK_ALL);
	}
	mutex_unlock(&cfg->if_sync);
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
	s32 bssidx;

	CFGP2P_DBG((" enter\n"));
	wl_clr_p2p_status(cfg, DISCOVERY_ON);
#ifdef DHD_IFDEBUG
	WL_ERR(("%s: bssidx: %d\n",
		__FUNCTION__, (cfg)->p2p->bss[P2PAPI_BSSCFG_DEVICE].bssidx));
#endif // endif
	bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	if (bssidx <= 0) {
		CFGP2P_ERR((" do nothing, not initialized\n"));
		return 0;
	}

	ret = wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0, bssidx);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR(("unable to set WL_P2P_DISC_ST_SCAN\n"));
	}
	/* Do a scan abort to stop the driver's scan engine in case it is still
	 * waiting out an action frame tx dwell time.
	 */
	wl_clr_p2p_status(cfg, DISCOVERY_ON);
	ret = wl_cfgp2p_deinit_discovery(cfg);
	return ret;
}

/* Scan parameters */
#define P2PAPI_SCAN_NPROBES 1
#define P2PAPI_SCAN_DWELL_TIME_MS 80
#define P2PAPI_SCAN_SOCIAL_DWELL_TIME_MS 40
#define P2PAPI_SCAN_HOME_TIME_MS 60
#define P2PAPI_SCAN_NPROBS_TIME_MS 30
#define P2PAPI_SCAN_AF_SEARCH_DWELL_TIME_MS 100
s32
wl_cfgp2p_escan(struct bcm_cfg80211 *cfg, struct net_device *dev, u16 active_scan,
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
	wl_escan_params_v2_t *eparams_v2;
	wlc_ssid_t ssid;
	u32 sync_id = 0;
	s32 nprobes = 0;
	s32 active_time = 0;
	const struct ether_addr *mac_addr = NULL;
	u32 scan_type = 0;
	struct net_device *pri_dev = NULL;

	pri_dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY);
	/* Allocate scan params which need space for 3 channels and 0 ssids */
	if (cfg->scan_params_v2) {
		eparams_size = (WL_SCAN_PARAMS_V2_FIXED_SIZE +
			OFFSETOF(wl_escan_params_v2_t, params)) +
			num_chans * sizeof(eparams->params.channel_list[0]);
	} else {
		eparams_size = (WL_SCAN_PARAMS_FIXED_SIZE +
			OFFSETOF(wl_escan_params_t, params)) +
			num_chans * sizeof(eparams->params.channel_list[0]);
	}

	memsize = sizeof(wl_p2p_scan_t) + eparams_size;
	memblk = scanparambuf;
	if (memsize > sizeof(scanparambuf)) {
		CFGP2P_ERR((" scanpar buf too small (%u > %zu)\n",
		    memsize, sizeof(scanparambuf)));
		return -1;
	}
	bzero(memblk, memsize);
	bzero(cfg->ioctl_buf, WLC_IOCTL_MAXLEN);
	if (search_state == WL_P2P_DISC_ST_SEARCH) {
		/*
		 * If we in SEARCH STATE, we don't need to set SSID explictly
		 * because dongle use P2P WILDCARD internally by default
		 */
		wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SEARCH, 0, 0, bssidx);
		/* use null ssid */
		ssid.SSID_len = 0;
		bzero(&ssid.SSID, sizeof(ssid.SSID));
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
		bzero(&ssid.SSID, sizeof(ssid.SSID));
		memcpy(&ssid.SSID, WL_P2P_WILDCARD_SSID, WL_P2P_WILDCARD_SSID_LEN);
	} else {
		CFGP2P_ERR((" invalid search state %d\n", search_state));
		return -1;
	}

	/* Fill in the P2P scan structure at the start of the iovar param block */
	p2p_params = (wl_p2p_scan_t*) memblk;
	p2p_params->type = 'E';

	if (!active_scan) {
		scan_type = WL_SCANFLAGS_PASSIVE;
	}

	if (tx_dst_addr == NULL) {
		mac_addr = &ether_bcast;
	} else {
		mac_addr = tx_dst_addr;
	}

	switch (p2p_scan_purpose) {
		case P2P_SCAN_SOCIAL_CHANNEL:
			active_time = P2PAPI_SCAN_SOCIAL_DWELL_TIME_MS;
			break;
		case P2P_SCAN_AFX_PEER_NORMAL:
		case P2P_SCAN_AFX_PEER_REDUCED:
			active_time = P2PAPI_SCAN_AF_SEARCH_DWELL_TIME_MS;
			break;
		case P2P_SCAN_CONNECT_TRY:
			active_time = WL_SCAN_CONNECT_DWELL_TIME_MS;
			break;
		default:
			active_time = wl_get_drv_status_all(cfg, CONNECTED) ?
				-1 : P2PAPI_SCAN_DWELL_TIME_MS;
			break;
	}

	if (p2p_scan_purpose == P2P_SCAN_CONNECT_TRY) {
		nprobes = active_time /
			WL_SCAN_JOIN_PROBE_INTERVAL_MS;
	} else {
		nprobes = active_time /
			P2PAPI_SCAN_NPROBS_TIME_MS;
	}

	if (nprobes <= 0) {
		nprobes = 1;
	}

	wl_escan_set_sync_id(sync_id, cfg);
	/* Fill in the Scan structure that follows the P2P scan structure */
	if (cfg->scan_params_v2) {
		eparams_v2 = (wl_escan_params_v2_t*) (p2p_params + 1);
		eparams_v2->version = htod16(ESCAN_REQ_VERSION_V2);
		eparams_v2->action =  htod16(action);
		eparams_v2->params.version = htod16(WL_SCAN_PARAMS_VERSION_V2);
		eparams_v2->params.length = htod16(sizeof(wl_scan_params_v2_t));
		eparams_v2->params.bss_type = DOT11_BSSTYPE_ANY;
		eparams_v2->params.scan_type = htod32(scan_type);
		(void)memcpy_s(&eparams_v2->params.bssid, ETHER_ADDR_LEN, mac_addr, ETHER_ADDR_LEN);
		eparams_v2->params.home_time = htod32(P2PAPI_SCAN_HOME_TIME_MS);
		eparams_v2->params.active_time = htod32(active_time);
		eparams_v2->params.nprobes = htod32(nprobes);
		eparams_v2->params.passive_time = htod32(-1);
		eparams_v2->sync_id = sync_id;
		for (i = 0; i < num_chans; i++) {
			eparams_v2->params.channel_list[i] =
				wl_ch_host_to_driver(channels[i]);
		}
		eparams_v2->params.channel_num = htod32((0 << WL_SCAN_PARAMS_NSSID_SHIFT) |
			(num_chans & WL_SCAN_PARAMS_COUNT_MASK));
		if (ssid.SSID_len)
			(void)memcpy_s(&eparams_v2->params.ssid,
					sizeof(wlc_ssid_t), &ssid, sizeof(wlc_ssid_t));
		sync_id = eparams_v2->sync_id;
	} else {
		eparams = (wl_escan_params_t*) (p2p_params + 1);
		eparams->version = htod32(ESCAN_REQ_VERSION);
		eparams->action =  htod16(action);
		eparams->params.bss_type = DOT11_BSSTYPE_ANY;
		eparams->params.scan_type = htod32(scan_type);
		(void)memcpy_s(&eparams->params.bssid, ETHER_ADDR_LEN, mac_addr, ETHER_ADDR_LEN);
		eparams->params.home_time = htod32(P2PAPI_SCAN_HOME_TIME_MS);
		eparams->params.active_time = htod32(active_time);
		eparams->params.nprobes = htod32(nprobes);
		eparams->params.passive_time = htod32(-1);
		eparams->sync_id = sync_id;
		for (i = 0; i < num_chans; i++) {
			eparams->params.channel_list[i] =
				wl_ch_host_to_driver(channels[i]);
		}
		eparams->params.channel_num = htod32((0 << WL_SCAN_PARAMS_NSSID_SHIFT) |
			(num_chans & WL_SCAN_PARAMS_COUNT_MASK));
		if (ssid.SSID_len)
			(void)memcpy_s(&eparams->params.ssid,
					sizeof(wlc_ssid_t), &ssid, sizeof(wlc_ssid_t));
		sync_id = eparams->sync_id;
	}

	wl_escan_set_type(cfg, WL_SCANTYPE_P2P);

	CFGP2P_DBG(("nprobes:%d active_time:%d\n", nprobes, active_time));
	CFGP2P_DBG(("SCAN CHANNELS : "));
	CFGP2P_DBG(("%d", channels[0]));
	for (i = 1; i < num_chans; i++) {
		CFGP2P_DBG((",%d", channels[i]));
	}
	CFGP2P_DBG(("\n"));

	ret = wldev_iovar_setbuf_bsscfg(pri_dev, "p2p_scan",
		memblk, memsize, cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	WL_INFORM(("P2P_SEARCH sync ID: %d, bssidx: %d\n", sync_id, bssidx));
	if (ret == BCME_OK) {
		wl_set_p2p_status(cfg, SCANNING);
	}
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
		return -EINVAL;
	WL_TRACE_HW4((" Enter\n"));
	if (bssidx == wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_PRIMARY))
		bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	if (channel)
		chan_cnt = AF_PEER_SEARCH_CNT;
	else
		chan_cnt = SOCIAL_CHAN_CNT;

	if (cfg->afx_hdl->pending_tx_act_frm && cfg->afx_hdl->is_active) {
		wl_action_frame_t *action_frame;
		action_frame = &(cfg->afx_hdl->pending_tx_act_frm->action_frame);
		if (wl_cfgp2p_is_p2p_gas_action(action_frame->data, action_frame->len))	{
			chan_cnt = 1;
			p2p_scan_purpose = P2P_SCAN_AFX_PEER_REDUCED;
		}
	}

	default_chan_list = (u16 *)MALLOCZ(cfg->osh, chan_cnt * sizeof(*default_chan_list));
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
	MFREE(cfg->osh, default_chan_list, chan_cnt * sizeof(*default_chan_list));
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
#endif // endif
#define wl_cfgp2p_is_wfd_ie(ie, tlvs, len)	wl_cfgp2p_has_ie(ie, tlvs, len, \
		(const uint8 *)WFA_OUI, WFA_OUI_LEN, WFA_OUI_TYPE_WFD)

/* Is any of the tlvs the expected entry? If
 * not update the tlvs buffer pointer/length.
 */
static bool
wl_cfgp2p_has_ie(const bcm_tlv_t *ie, const u8 **tlvs, u32 *tlvs_len,
                 const u8 *oui, u32 oui_len, u8 type)
{
	/* If the contents match the OUI and the type */
	if (ie->len >= oui_len + 1 &&
	    !bcmp(ie->data, oui, oui_len) &&
	    type == ie->data[oui_len]) {
		return TRUE;
	}

	/* point to the next ie */
	if (tlvs != NULL) {
		bcm_tlv_buffer_advance_past(ie, tlvs, tlvs_len);
	}

	return FALSE;
}

const wpa_ie_fixed_t *
wl_cfgp2p_find_wpaie(const u8 *parse, u32 len)
{
	const bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_wpa_ie(ie, &parse, &len)) {
			return (const wpa_ie_fixed_t *)ie;
		}
	}
	return NULL;
}

const wpa_ie_fixed_t *
wl_cfgp2p_find_wpsie(const u8 *parse, u32 len)
{
	const bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_wps_ie(ie, &parse, &len)) {
			return (const wpa_ie_fixed_t *)ie;
		}
	}
	return NULL;
}

wifi_p2p_ie_t *
wl_cfgp2p_find_p2pie(const u8 *parse, u32 len)
{
	bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_p2p_ie(ie, &parse, &len)) {
			return (wifi_p2p_ie_t *)ie;
		}
	}
	return NULL;
}

const wifi_wfd_ie_t *
wl_cfgp2p_find_wfdie(const u8 *parse, u32 len)
{
	const bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_wfd_ie(ie, &parse, &len)) {
			return (const wifi_wfd_ie_t *)ie;
		}
	}
	return NULL;
}

u32
wl_cfgp2p_vndr_ie(struct bcm_cfg80211 *cfg, u8 *iebuf, s32 pktflag,
            s8 *oui, s32 ie_id, const s8 *data, s32 datalen, const s8* add_del_cmd)
{
	vndr_ie_setbuf_t hdr;	/* aligned temporary vndr_ie buffer header */
	s32 iecount;
	u32 data_offset;

	/* Validate the pktflag parameter */
	if ((pktflag & ~(VNDR_IE_BEACON_FLAG | VNDR_IE_PRBRSP_FLAG |
	            VNDR_IE_ASSOCRSP_FLAG | VNDR_IE_AUTHRSP_FLAG |
	            VNDR_IE_PRBREQ_FLAG | VNDR_IE_ASSOCREQ_FLAG |
	            VNDR_IE_DISASSOC_FLAG))) {
		CFGP2P_ERR(("p2pwl_vndr_ie: Invalid packet flag 0x%x\n", pktflag));
		return -1;
	}

	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strlcpy(hdr.cmd, add_del_cmd, sizeof(hdr.cmd));

	/* Set the IE count - the buffer contains only 1 IE */
	iecount = htod32(1);
	memcpy((void *)&hdr.vndr_ie_buffer.iecount, &iecount, sizeof(s32));

	/* For vendor ID DOT11_MNG_ID_EXT_ID, need to set pkt flag to VNDR_IE_CUSTOM_FLAG */
	if (ie_id == DOT11_MNG_ID_EXT_ID) {
		pktflag = pktflag | VNDR_IE_CUSTOM_FLAG;
	}

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
 * Parameters: Note that this idx is applicable only
 * for primary and P2P interfaces. The virtual AP/STA is not
 * covered here.
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
	if (!cfg->p2p) {
		CFGP2P_ERR(("p2p if does not exist\n"));
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
#ifdef DHD_IFDEBUG
	PRINT_WDEV_INFO(cfgdev);
#endif /* DHD_IFDEBUG */

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

#ifdef P2P_LISTEN_OFFLOADING
	if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
		wl_clr_p2p_status(cfg, DISC_IN_PROGRESS);
		CFGP2P_ERR(("DISC_IN_PROGRESS cleared\n"));
		if (ndev && (ndev->ieee80211_ptr != NULL)) {
#if defined(WL_CFG80211_P2P_DEV_IF)
			if (cfgdev && ((struct wireless_dev *)cfgdev)->wiphy) {
				cfg80211_remain_on_channel_expired(cfgdev, cfg->last_roc_id,
						&cfg->remain_on_chan, GFP_KERNEL);
			} else {
				CFGP2P_ERR(("Invalid cfgdev. Dropping the"
						"remain_on_channel_expired event.\n"));
			}
#else
			cfg80211_remain_on_channel_expired(cfgdev, cfg->last_roc_id,
					&cfg->remain_on_chan, cfg->remain_on_chan_type, GFP_KERNEL);
#endif /* WL_CFG80211_P2P_DEV_IF */
		}
	}
#endif /* P2P_LISTEN_OFFLOADING */

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
			WL_DBG(("Listen DONE for remain on channel expired\n"));
			wl_clr_drv_status(cfg, REMAINING_ON_CHANNEL, ndev);
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
			wl_clr_drv_status(cfg, FAKE_REMAINING_ON_CHANNEL, ndev);
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
			if (ndev && (ndev->ieee80211_ptr != NULL)) {
#if defined(WL_CFG80211_P2P_DEV_IF)
				if (cfgdev && ((struct wireless_dev *)cfgdev)->wiphy &&
				    bcmcfg_to_p2p_wdev(cfg)) {
					/*
					 * To prevent kernel panic,
					 * if cfgdev->wiphy may be invalid, adding explicit check
					 */
					struct wireless_dev *wdev_dpp_listen = NULL;
					wdev_dpp_listen = wl_get_wdev_by_dpp_listen(cfg);
					/*
					 * check if dpp listen was trigerred
					 * if so, clear dpp disten flag and route the event for listen
					 * complete on the interface on which listen was reqeusted.
					 */
					if (wdev_dpp_listen) {
						wl_set_dpp_listen_by_netdev(cfg, wdev_dpp_listen->netdev, 0);
						cfg80211_remain_on_channel_expired(wdev_dpp_listen,
							cfg->last_roc_id, &cfg->remain_on_chan, GFP_KERNEL);
					} else {
						cfg80211_remain_on_channel_expired(bcmcfg_to_p2p_wdev(cfg),
						cfg->last_roc_id, &cfg->remain_on_chan, GFP_KERNEL);
					}

				} else
					CFGP2P_ERR(("Invalid cfgdev. Dropping the"
						"remain_on_channel_expired event.\n"));
#else
				if (cfgdev && ((struct wireless_dev *)cfgdev)->wiphy)
					cfg80211_remain_on_channel_expired(cfgdev,
						cfg->last_roc_id, &cfg->remain_on_chan,
						cfg->remain_on_chan_type, GFP_KERNEL);
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
	struct net_device *ndev;
	CFGP2P_DBG((" Enter\n"));

	if (!cfg) {
		CFGP2P_ERR((" No cfg\n"));
		return;
	}
	bzero(&msg, sizeof(wl_event_msg_t));
	msg.event_type =  hton32(WLC_E_P2P_DISC_LISTEN_COMPLETE);
	msg.bsscfgidx  =  wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
#if defined(WL_ENABLE_P2P_IF)
	ndev = cfg->p2p_net ? cfg->p2p_net :
		wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE);
#else
	ndev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_DEVICE);
#endif /* WL_ENABLE_P2P_IF */
	if (!ndev) {
		CFGP2P_ERR((" No ndev\n"));
		return;
	}
	wl_cfg80211_event(ndev, &msg, NULL);
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
			if (bcmcfg_to_p2p_wdev(cfg))
				cfg80211_remain_on_channel_expired(wdev, cfg->last_roc_id,
					&cfg->remain_on_chan, GFP_KERNEL);
#else
			if (ndev && ndev->ieee80211_ptr)
				cfg80211_remain_on_channel_expired(ndev, cfg->last_roc_id,
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
	timer_list_compat_t *_timer;
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
	u8 bsscfgidx = e->bsscfgidx;

	CFGP2P_DBG((" Enter\n"));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (wl_get_drv_status_all(cfg, SENDING_ACT_FRM)) {
		if (event_type == WLC_E_ACTION_FRAME_COMPLETE) {

			CFGP2P_DBG((" WLC_E_ACTION_FRAME_COMPLETE is received : %d\n", status));
			if (status == WLC_E_STATUS_SUCCESS) {
				wl_set_p2p_status(cfg, ACTION_TX_COMPLETED);
				CFGP2P_ACTION(("TX actfrm : ACK\n"));
				if (!cfg->need_wait_afrx && cfg->af_sent_channel) {
					CFGP2P_DBG(("no need to wait next AF.\n"));
					wl_stop_wait_next_action_frame(cfg, ndev, bsscfgidx);
				}
			}
			else if (!wl_get_p2p_status(cfg, ACTION_TX_COMPLETED)) {
				wl_set_p2p_status(cfg, ACTION_TX_NOACK);
				if (status == WLC_E_STATUS_SUPPRESS) {
					CFGP2P_ACTION(("TX actfrm : SUPPRES\n"));
				} else {
					CFGP2P_ACTION(("TX actfrm : NO ACK\n"));
				}
				wl_stop_wait_next_action_frame(cfg, ndev, bsscfgidx);
			}
		} else {
			CFGP2P_DBG((" WLC_E_ACTION_FRAME_OFFCHAN_COMPLETE is received,"
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

	CFGP2P_DBG(("\n"));
	CFGP2P_DBG(("channel : %u , dwell time : %u\n",
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
		CFGP2P_ACTION(("TX actfrm : ERROR\n"));
		goto exit;
	}

	timeout = wait_for_completion_timeout(&cfg->send_af_done,
		msecs_to_jiffies(af_params->dwell_time + WL_AF_TX_EXTRA_TIME_MAX));

	if (timeout >= 0 && wl_get_p2p_status(cfg, ACTION_TX_COMPLETED)) {
		CFGP2P_DBG(("tx action frame operation is completed\n"));
		ret = BCME_OK;
	} else if (ETHER_ISBCAST(&cfg->afx_hdl->tx_dst_addr)) {
		CFGP2P_DBG(("bcast tx action frame operation is completed\n"));
		ret = BCME_OK;
	} else {
		ret = BCME_ERROR;
		CFGP2P_DBG(("tx action frame operation is failed\n"));
	}
	/* clear status bit for action tx */
	wl_clr_p2p_status(cfg, ACTION_TX_COMPLETED);
	wl_clr_p2p_status(cfg, ACTION_TX_NOACK);

exit:
	CFGP2P_DBG((" via act frame iovar : status = %d\n", ret));

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
wl_cfgp2p_generate_bss_mac(struct bcm_cfg80211 *cfg, struct ether_addr *primary_addr)
{
	struct ether_addr *mac_addr = wl_to_p2p_bss_macaddr(cfg, P2PAPI_BSSCFG_DEVICE);
	struct ether_addr *int_addr;

	memcpy(mac_addr, primary_addr, sizeof(struct ether_addr));
	mac_addr->octet[0] |= 0x02;
	WL_DBG(("P2P Discovery address:"MACDBG "\n", MAC2STRDBG(mac_addr->octet)));

	int_addr = wl_to_p2p_bss_macaddr(cfg, P2PAPI_BSSCFG_CONNECTION1);
	memcpy(int_addr, mac_addr, sizeof(struct ether_addr));
	int_addr->octet[4] ^= 0x80;
	WL_DBG(("Primary P2P Interface address:"MACDBG "\n", MAC2STRDBG(int_addr->octet)));

	int_addr = wl_to_p2p_bss_macaddr(cfg, P2PAPI_BSSCFG_CONNECTION2);
	memcpy(int_addr, mac_addr, sizeof(struct ether_addr));
	int_addr->octet[4] ^= 0x90;
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

#if defined(WL_CFG80211_P2P_DEV_IF)
	ndev = bcmcfg_to_prmry_ndev(cfg);
	wdev = bcmcfg_to_p2p_wdev(cfg);
#elif defined(WL_ENABLE_P2P_IF)
	ndev = cfg->p2p_net ? cfg->p2p_net : bcmcfg_to_prmry_ndev(cfg);
	wdev = ndev_to_wdev(ndev);
#endif /* WL_CFG80211_P2P_DEV_IF */

	wl_cfgp2p_cancel_listen(cfg, ndev, wdev, TRUE);
	wl_cfgp2p_disable_discovery(cfg);

#if defined(WL_CFG80211_P2P_DEV_IF) && !defined(KEEP_WIFION_OPTION)
/*
 * In CUSTOMER_HW4 implementation "ifconfig wlan0 down" can get
 * called during phone suspend and customer requires the p2p
 * discovery interface to be left untouched so that the user
 * space can resume without any problem.
 */
	if (cfg->p2p_wdev) {
		/* If p2p wdev is left out, clean it up */
		WL_ERR(("Clean up the p2p discovery IF\n"));
		wl_cfgp2p_del_p2p_disc_if(cfg->p2p_wdev, cfg);
	}
#endif /* WL_CFG80211_P2P_DEV_IF !defined(KEEP_WIFION_OPTION) */

	wl_cfgp2p_deinit_priv(cfg);
	return 0;
}

int wl_cfgp2p_vif_created(struct bcm_cfg80211 *cfg)
{
	if (cfg->p2p && ((wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION1) != -1) ||
		(wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION2) != -1)))
		return true;
	else
		return false;

}

s32
wl_cfgp2p_set_p2p_noa(struct bcm_cfg80211 *cfg, struct net_device *ndev, char* buf, int len)
{
	s32 ret = -1;
	int count, start, duration;
	wl_p2p_sched_t dongle_noa;
	s32 bssidx, type;
	int iovar_len = sizeof(dongle_noa);
	CFGP2P_DBG((" Enter\n"));

	bzero(&dongle_noa, sizeof(dongle_noa));

	if (wl_cfgp2p_vif_created(cfg)) {
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

		if (cfg->p2p->noa.desc[0].count != 255 && cfg->p2p->noa.desc[0].count != 0) {
			cfg->p2p->noa.desc[0].start = 200;
			dongle_noa.type = WL_P2P_SCHED_TYPE_REQ_ABS;
			dongle_noa.action = WL_P2P_SCHED_ACTION_GOOFF;
			dongle_noa.option = WL_P2P_SCHED_OPTION_TSFOFS;
		}
		else if (cfg->p2p->noa.desc[0].count == 0) {
			cfg->p2p->noa.desc[0].start = 0;
			dongle_noa.type = WL_P2P_SCHED_TYPE_ABS;
			dongle_noa.option = WL_P2P_SCHED_OPTION_NORMAL;
			dongle_noa.action = WL_P2P_SCHED_ACTION_RESET;
		}
		else {
			/* Continuous NoA interval. */
			dongle_noa.action = WL_P2P_SCHED_ACTION_DOZE;
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
		bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
		if (wl_cfgp2p_find_type(cfg, bssidx, &type) != BCME_OK)
			return BCME_ERROR;

		if (dongle_noa.action == WL_P2P_SCHED_ACTION_RESET) {
			iovar_len -= sizeof(wl_p2p_sched_desc_t);
		}

		ret = wldev_iovar_setbuf(wl_to_p2p_bss_ndev(cfg, type),
			"p2p_noa", &dongle_noa, iovar_len, cfg->ioctl_buf,
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
	if (wl_cfgp2p_vif_created(cfg)) {
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
	s32 conn_idx;
	s32 bssidx;
	struct net_device *dev;

	CFGP2P_DBG((" Enter\n"));
	if (wl_cfgp2p_vif_created(cfg)) {
		sscanf(buf, "%10d %10d %10d", &legacy_ps, &ps, &ctw);
		CFGP2P_DBG((" Enter legacy_ps %d ps %d ctw %d\n", legacy_ps, ps, ctw));

		bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
		if (wl_cfgp2p_find_type(cfg, bssidx, &conn_idx) != BCME_OK)
			return BCME_ERROR;
		dev = wl_to_p2p_bss_ndev(cfg, conn_idx);
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
			ret = wldev_ioctl_set(dev,
				WLC_SET_PM, &legacy_ps, sizeof(legacy_ps));
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

s32
wl_cfgp2p_set_p2p_ecsa(struct bcm_cfg80211 *cfg, struct net_device *ndev, char* buf, int len)
{
	int ch, bw;
	s32 conn_idx;
	s32 bssidx;
	struct net_device *dev;
	char smbuf[WLC_IOCTL_SMLEN];
	wl_chan_switch_t csa_arg;
	u32 chnsp = 0;
	int err = 0;

	CFGP2P_DBG((" Enter\n"));
	if (wl_cfgp2p_vif_created(cfg)) {
		sscanf(buf, "%10d %10d", &ch, &bw);
		CFGP2P_DBG(("Enter ch %d bw %d\n", ch, bw));

		bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
		if (wl_cfgp2p_find_type(cfg, bssidx, &conn_idx) != BCME_OK) {
			return BCME_ERROR;
		}
		dev = wl_to_p2p_bss_ndev(cfg, conn_idx);
		if (ch <= 0 || bw <= 0) {
			CFGP2P_ERR(("Negative value not permitted!\n"));
			return BCME_ERROR;
		}

		memset_s(&csa_arg, sizeof(csa_arg), 0, sizeof(csa_arg));
		csa_arg.mode = DOT11_CSA_MODE_ADVISORY;
		csa_arg.count = P2P_ECSA_CNT;
		csa_arg.reg = 0;

		snprintf(buf, len, "%d/%d", ch, bw);
		chnsp = wf_chspec_aton(buf);
		if (chnsp == 0) {
			CFGP2P_ERR(("%s:chsp is not correct\n", __FUNCTION__));
			return BCME_ERROR;
		}
		chnsp = wl_chspec_host_to_driver(chnsp);
		csa_arg.chspec = chnsp;

		err = wldev_iovar_setbuf(dev, "csa", &csa_arg, sizeof(csa_arg),
			smbuf, sizeof(smbuf), NULL);
		if (err) {
			CFGP2P_ERR(("%s:set p2p_ecsa failed:%d\n", __FUNCTION__, err));
			return BCME_ERROR;
		}
	} else {
		CFGP2P_ERR(("ERROR: set_p2p_ecsa in non-p2p mode\n"));
		return BCME_ERROR;
	}
	return BCME_OK;
}

s32
wl_cfgp2p_increase_p2p_bw(struct bcm_cfg80211 *cfg, struct net_device *ndev, char* buf, int len)
{
	int algo;
	int bw;
	int ret = BCME_OK;

	sscanf(buf, "%3d", &bw);
	if (bw == 0) {
		algo = 0;
		ret = wldev_iovar_setbuf(ndev, "mchan_algo", &algo, sizeof(algo), cfg->ioctl_buf,
				WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (ret < 0) {
			CFGP2P_ERR(("fw set mchan_algo failed %d\n", ret));
			return BCME_ERROR;
		}
	} else {
		algo = 1;
		ret = wldev_iovar_setbuf(ndev, "mchan_algo", &algo, sizeof(algo), cfg->ioctl_buf,
				WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (ret < 0) {
			CFGP2P_ERR(("fw set mchan_algo failed %d\n", ret));
			return BCME_ERROR;
		}
		ret = wldev_iovar_setbuf(ndev, "mchan_bw", &bw, sizeof(algo), cfg->ioctl_buf,
				WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (ret < 0) {
			CFGP2P_ERR(("fw set mchan_bw failed %d\n", ret));
			return BCME_ERROR;
		}
	}
	return BCME_OK;
}

const u8 *
wl_cfgp2p_retreive_p2pattrib(const void *buf, u8 element_id)
{
	const wifi_p2p_ie_t *ie = NULL;
	u16 len = 0;
	const u8 *subel;
	u8 subelt_id;
	u16 subelt_len;

	if (!buf) {
		WL_ERR(("P2P IE not present"));
		return 0;
	}

	ie = (const wifi_p2p_ie_t*) buf;
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

const u8*
wl_cfgp2p_find_attrib_in_all_p2p_Ies(const u8 *parse, u32 len, u32 attrib)
{
	bcm_tlv_t *ie;
	const u8* pAttrib;
	uint ie_len;

	CFGP2P_DBG(("Starting parsing parse %p attrib %d remaining len %d ", parse, attrib, len));
	ie_len = len;
	while ((ie = bcm_parse_tlvs(parse, ie_len, DOT11_MNG_VS_ID))) {
		if (wl_cfgp2p_is_p2p_ie(ie, &parse, &ie_len) == TRUE) {
			/* Have the P2p ie. Now check for attribute */
			if ((pAttrib = wl_cfgp2p_retreive_p2pattrib(ie, attrib)) != NULL) {
				CFGP2P_DBG(("P2P attribute %d was found at parse %p",
					attrib, parse));
				return pAttrib;
			}
			else {
				/* move to next IE */
				bcm_tlv_buffer_advance_past(ie, &parse, &ie_len);

				CFGP2P_INFO(("P2P Attribute %d not found Moving parse"
					" to %p len to %d", attrib, parse, ie_len));
			}
		}
		else {
			/* It was not p2p IE. parse will get updated automatically to next TLV */
			CFGP2P_INFO(("IT was NOT P2P IE parse %p len %d", parse, ie_len));
		}
	}
	CFGP2P_ERR(("P2P attribute %d was NOT found", attrib));
	return NULL;
}

const u8 *
wl_cfgp2p_retreive_p2p_dev_addr(wl_bss_info_t *bi, u32 bi_length)
{
	const u8 *capability = NULL;
	bool p2p_go	= 0;
	const u8 *ptr = NULL;

	if (bi->length != bi->ie_offset + bi->ie_length) {
		return NULL;
	}

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

#if defined(WL_ENABLE_P2P_IF) || defined(WL_NEWCFG_PRIVCMD_SUPPORT)
s32
wl_cfgp2p_register_ndev(struct bcm_cfg80211 *cfg)
{
	int ret = 0;
	struct net_device* net = NULL;
#ifndef	WL_NEWCFG_PRIVCMD_SUPPORT
	struct wireless_dev *wdev = NULL;
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */
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

#ifndef	WL_NEWCFG_PRIVCMD_SUPPORT
	wdev = (struct wireless_dev *)MALLOCZ(cfg->osh, sizeof(*wdev));
	if (unlikely(!wdev)) {
		WL_ERR(("Could not allocate wireless device\n"));
		free_netdev(net);
		return -ENOMEM;
	}
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */

	strlcpy(net->name, "p2p%d", sizeof(net->name));

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
#endif // endif

	/* Register with a dummy MAC addr */
	memcpy(net->dev_addr, temp_addr, ETHER_ADDR_LEN);

#ifndef	WL_NEWCFG_PRIVCMD_SUPPORT
	wdev->wiphy = cfg->wdev->wiphy;

	wdev->iftype = wl_mode_to_nl80211_iftype(WL_MODE_BSS);

	net->ieee80211_ptr = wdev;
#else
	net->ieee80211_ptr = NULL;
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
	net->ethtool_ops = &cfgp2p_ethtool_ops;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) */

#ifndef	WL_NEWCFG_PRIVCMD_SUPPORT
	SET_NETDEV_DEV(net, wiphy_dev(wdev->wiphy));

	/* Associate p2p0 network interface with new wdev */
	wdev->netdev = net;
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */

	ret = register_netdev(net);
	if (ret) {
		CFGP2P_ERR((" register_netdevice failed (%d)\n", ret));
		free_netdev(net);
#ifndef	WL_NEWCFG_PRIVCMD_SUPPORT
		MFREE(cfg->osh, wdev, sizeof(*wdev));
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */
		return -ENODEV;
	}

	/* store p2p net ptr for further reference. Note that iflist won't have this
	 * entry as there corresponding firmware interface is a "Hidden" interface.
	 */
#ifndef	WL_NEWCFG_PRIVCMD_SUPPORT
	cfg->p2p_wdev = wdev;
#else
	cfg->p2p_wdev = NULL;
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */
	cfg->p2p_net = net;

	printk("%s: P2P Interface Registered\n", net->name);

	return ret;
}

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
#if defined(OEM_ANDROID)
		ret = wl_android_priv_cmd(ndev, ifr);
#else
	(void)ndev;
#endif // endif

	} else {
		CFGP2P_ERR(("%s: IOCTL req 0x%x on p2p0 I/F. Ignoring. \n",
		__FUNCTION__, cmd));
		return -1;
	}

	return ret;
}
#endif /* WL_ENABLE_P2P_IF || WL_NEWCFG_PRIVCMD_SUPPORT  */

#if defined(WL_ENABLE_P2P_IF)
static int wl_cfgp2p_if_open(struct net_device *net)
{
	struct wireless_dev *wdev = net->ieee80211_ptr;

	if (!wdev || !wl_cfg80211_is_p2p_active(net))
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

static int wl_cfgp2p_if_stop(struct net_device *net)
{
	struct wireless_dev *wdev = net->ieee80211_ptr;
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);

	if (!wdev)
		return -EINVAL;

	wl_cfg80211_scan_stop(cfg, net);

#if !defined(WL_IFACE_COMB_NUM_CHANNELS)
	wdev->wiphy->interface_modes = (wdev->wiphy->interface_modes)
					& (~(BIT(NL80211_IFTYPE_P2P_CLIENT)|
					BIT(NL80211_IFTYPE_P2P_GO)));
#endif /* !WL_IFACE_COMB_NUM_CHANNELS */
	return 0;
}

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

	if (!cfg || !cfg->p2p_supported)
		return ERR_PTR(-EINVAL);

	WL_TRACE(("Enter\n"));

	if (cfg->p2p_wdev) {
#ifndef EXPLICIT_DISCIF_CLEANUP
		dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
#endif /* EXPLICIT_DISCIF_CLEANUP */
		/*
		 * This is not expected. This can happen due to
		 * supplicant crash/unclean de-initialization which
		 * didn't free the p2p discovery interface. Indicate
		 * driver hang to user space so that the framework
		 * can rei-init the Wi-Fi.
		 */
		CFGP2P_ERR(("p2p_wdev defined already.\n"));
		wl_probe_wdev_all(cfg);
#ifdef EXPLICIT_DISCIF_CLEANUP
		/*
		 * CUSTOMER_HW4 design doesn't delete the p2p discovery
		 * interface on ifconfig wlan0 down context which comes
		 * without a preceeding NL80211_CMD_DEL_INTERFACE for p2p
		 * discovery. But during supplicant crash the DEL_IFACE
		 * command will not happen and will cause a left over iface
		 * even after ifconfig wlan0 down. So delete the iface
		 * first and then indicate the HANG event
		 */
		wl_cfgp2p_del_p2p_disc_if(cfg->p2p_wdev, cfg);
#else
		dhd->hang_reason = HANG_REASON_IFACE_DEL_FAILURE;
#ifdef OEM_ANDROID
#if defined(BCMPCIE) && defined(DHD_FW_COREDUMP)
		if (dhd->memdump_enabled) {
			/* Load the dongle side dump to host
			 * memory and then BUG_ON()
			 */
			dhd->memdump_type = DUMP_TYPE_IFACE_OP_FAILURE;
			dhd_bus_mem_dump(dhd);
		}
#endif /* BCMPCIE && DHD_FW_COREDUMP */
		net_os_send_hang_message(bcmcfg_to_prmry_ndev(cfg));
#endif /* OEM_ANDROID */
		return ERR_PTR(-ENODEV);
#endif /* EXPLICIT_DISCIF_CLEANUP */
	}

	wdev = (struct wireless_dev *)MALLOCZ(cfg->osh, sizeof(*wdev));
	if (unlikely(!wdev)) {
		WL_ERR(("Could not allocate wireless device\n"));
		return ERR_PTR(-ENOMEM);
	}

	bzero(&primary_mac, sizeof(primary_mac));
	get_primary_mac(cfg, &primary_mac);
	wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);

	wdev->wiphy = cfg->wdev->wiphy;
	wdev->iftype = NL80211_IFTYPE_P2P_DEVICE;
	memcpy(wdev->address, wl_to_p2p_bss_macaddr(cfg, P2PAPI_BSSCFG_DEVICE), ETHER_ADDR_LEN);

#if defined(WL_NEWCFG_PRIVCMD_SUPPORT)
	if (cfg->p2p_net)
		memcpy(cfg->p2p_net->dev_addr, wl_to_p2p_bss_macaddr(cfg, P2PAPI_BSSCFG_DEVICE),
			ETHER_ADDR_LEN);
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */

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
#if defined(P2P_IE_MISSING_FIX)
	cfg->p2p_prb_noti = false;
#endif // endif

	CFGP2P_DBG(("P2P interface started\n"));

exit:
	return ret;
}

void
wl_cfgp2p_stop_p2p_device(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	int ret = 0;
	struct net_device *ndev = NULL;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

	if (!cfg)
		return;

	CFGP2P_DBG(("Enter\n"));

	/* Check if cfg80211 interface is already down */
	ndev = bcmcfg_to_prmry_ndev(cfg);
	if (!wl_get_drv_status(cfg, READY, ndev)) {
		WL_DBG(("cfg80211 interface is already down\n"));
		return;	/* it is even not ready */
	}

	ret = wl_cfg80211_scan_stop(cfg, wdev);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR(("P2P scan stop failed, ret=%d\n", ret));
	}

	if (!p2p_is_on(cfg)) {
		return;
	}

#ifdef P2P_LISTEN_OFFLOADING
	wl_cfg80211_p2plo_deinit(cfg);
#endif /* P2P_LISTEN_OFFLOADING */

	/* Cancel any on-going listen */
	wl_cfgp2p_cancel_listen(cfg, bcmcfg_to_prmry_ndev(cfg), wdev, TRUE);

	ret = wl_cfgp2p_disable_discovery(cfg);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR(("P2P disable discovery failed, ret=%d\n", ret));
	}

	p2p_on(cfg) = false;

	CFGP2P_DBG(("Exit. P2P interface stopped\n"));

	return;
}

int
wl_cfgp2p_del_p2p_disc_if(struct wireless_dev *wdev, struct bcm_cfg80211 *cfg)
{
	bool rollback_lock = false;

	if (!wdev || !cfg) {
		WL_ERR(("null ptr. wdev:%p cfg:%p\n", wdev, cfg));
		return -EINVAL;
	}

	WL_INFORM(("Enter\n"));

	if (!rtnl_is_locked()) {
		rtnl_lock();
		rollback_lock = true;
	}

	cfg80211_unregister_wdev(wdev);

	if (rollback_lock)
		rtnl_unlock();

	synchronize_rcu();

	MFREE(cfg->osh, wdev, sizeof(*wdev));

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
