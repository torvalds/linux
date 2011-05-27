/*
 * Linux cfgp2p driver
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
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
 * $Id: wl_cfg80211.c,v 1.1.4.1.2.14 2011-02-09 01:40:07 Exp $
 * $Id$
 */


#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/if_arp.h>
#include <asm/uaccess.h>

#include <bcmutils.h>
#include <bcmendian.h>
#include <proto/ethernet.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhdioctl.h>
#include <wlioctl.h>

#include <wl_cfg80211.h>
#include <wl_cfgp2p.h>
#include <wldev_common.h>


/* dword align allocation */
#define WLC_IOCTL_MAXLEN 8192
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

#define CFGP2P_DBG_NONE	0
#define CFGP2P_DBG_DBG 	(1 << 2)
#define CFGP2P_DBG_INFO	(1 << 1)
#define CFGP2P_DBG_ERR	(1 << 0)
#define CFGP2P_DBG_MASK ((WL_DBG_DBG | WL_DBG_INFO | WL_DBG_ERR) << 1)


#define CFGP2P_DBG_LEVEL 0xFF		/* 0 invalidates all debug messages */

u32 cfgp2p_dbg_level = CFGP2P_DBG_ERR | CFGP2P_DBG_INFO | CFGP2P_DBG_DBG;

#define CFGP2P_ERR(args)									\
	do {										\
		if (cfgp2p_dbg_level & CFGP2P_DBG_ERR) {				\
			printk(KERN_ERR "CFGP2P-ERROR) %s : ", __func__);	\
			printk args;						\
		}									\
	} while (0)
#define	CFGP2P_INFO(args)									\
	do {										\
		if (cfgp2p_dbg_level & CFGP2P_DBG_INFO) {				\
			printk(KERN_ERR "CFGP2P-INFO) %s : ", __func__);	\
			printk args;						\
		}									\
	} while (0)
#if (CFGP2P_DBG_LEVEL > 0)
#define	CFGP2P_DBG(args)								\
	do {									\
		if (cfgp2p_dbg_level & CFGP2P_DBG_DBG) {			\
			printk(KERN_ERR "CFGP2P-DEBUG) %s :", __func__);	\
			printk args;							\
		}									\
	} while (0)
#else				/* !(WL_DBG_LEVEL > 0) */
#define	CFGP2P_DBG(args)
#endif				/* (WL_DBG_LEVEL > 0) */


static s8 ioctlbuf[WLC_IOCTL_MAXLEN];
static s8 scanparambuf[WLC_IOCTL_SMLEN];
static s8 *smbuf = ioctlbuf;

static bool
wl_cfgp2p_has_ie(u8 *ie, u8 **tlvs, u32 *tlvs_len, const u8 *oui, u32 oui_len, u8 type);

static s32
wl_cfgp2p_vndr_ie(struct net_device *ndev, s32 bssidx, s32 pktflag,
            s8 *oui, s32 ie_id, s8 *data, s32 data_len, s32 delete);
/* 
 *  Initialize variables related to P2P
 *
 */
void
wl_cfgp2p_init_priv(struct wl_priv *wl)
{
	wl->p2p_on = 0;
	wl->p2p_scan = 0; /* by default , legacy scan */
	wl->p2p_status = 0;
	wl->listen_timer = NULL;

#define INIT_IE(IE_TYPE, BSS_TYPE)		\
	do {							\
		memset(wl_to_p2p_bss_saved_ie(wl, BSS_TYPE).p2p_ ## IE_TYPE ## _ie, 0, \
		   sizeof(wl_to_p2p_bss_saved_ie(wl, BSS_TYPE).p2p_ ## IE_TYPE ## _ie)); \
		wl_to_p2p_bss_saved_ie(wl, BSS_TYPE).p2p_ ## IE_TYPE ## _ie_len = 0; \
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
	wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_PRIMARY) = wl_to_prmry_ndev(wl);
	wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_PRIMARY) = 0;
	wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_DEVICE) = NULL;
	wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE) = 0;
	wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_CONNECTION) = NULL;
	wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_CONNECTION) = 0;

}

/*
 * Set P2P functions into firmware
 */
s32
wl_cfgp2p_set_firm_p2p(struct wl_priv *wl)
{
	s32 ret = BCME_OK;

	/* TODO : Do we have to check whether APSTA is enabled or not ? */
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
wl_cfgp2p_ifadd(struct wl_priv *wl, struct ether_addr *mac, u8 if_type,
            chanspec_t chspec)
{
	wl_p2p_if_t ifreq;
	s32 err;
	struct net_device *netdev = wl_to_prmry_ndev(wl);

	ifreq.type = if_type;
	ifreq.chspec = chspec;
	memcpy(ifreq.addr.octet, mac->octet, sizeof(ifreq.addr.octet));

	CFGP2P_INFO(("---wl p2p_ifadd %02x:%02x:%02x:%02x:%02x:%02x %s %u\n",
	    ifreq.addr.octet[0], ifreq.addr.octet[1], ifreq.addr.octet[2],
		ifreq.addr.octet[3], ifreq.addr.octet[4], ifreq.addr.octet[5],
		(if_type == WL_P2P_IF_GO) ? "go" : "client",
	        (chspec & WL_CHANSPEC_CHAN_MASK) >> WL_CHANSPEC_CHAN_SHIFT));

	err = wldev_iovar_setbuf(netdev, "p2p_ifadd", &ifreq, sizeof(ifreq),
		ioctlbuf, sizeof(ioctlbuf));
	return err;
}

/* Delete a P2P BSS.
 * Parameters:
 * @mac      : MAC address of the BSS to create
 * Returns 0 if success.
 */
s32
wl_cfgp2p_ifdel(struct wl_priv *wl, struct ether_addr *mac)
{
	s32 ret;
	struct net_device *netdev = wl_to_prmry_ndev(wl);

	CFGP2P_INFO(("---wl p2p_ifdel %02x:%02x:%02x:%02x:%02x:%02x\n",
	    mac->octet[0], mac->octet[1], mac->octet[2],
	    mac->octet[3], mac->octet[4], mac->octet[5]));

	ret = wldev_iovar_setbuf(netdev, "p2p_ifdel", mac, sizeof(*mac),
		ioctlbuf, sizeof(ioctlbuf));

	if (unlikely(ret < 0)) {
		printk("'wl p2p_ifdel' error %d\n", ret);
	}
	return ret;
}

/* Change a P2P Role.
 * Parameters:
 * @mac      : MAC address of the BSS to change a role
 * Returns 0 if success.
 */
s32
wl_cfgp2p_ifchange(struct wl_priv *wl, struct ether_addr *mac, u8 if_type,
            chanspec_t chspec)
{
	wl_p2p_if_t ifreq;
	s32 err;
	struct net_device *netdev = wl_to_prmry_ndev(wl);

	ifreq.type = if_type;
	ifreq.chspec = chspec;
	memcpy(ifreq.addr.octet, mac->octet, sizeof(ifreq.addr.octet));

	CFGP2P_INFO(("---wl p2p_ifchange %02x:%02x:%02x:%02x:%02x:%02x %s %u\n",
	    ifreq.addr.octet[0], ifreq.addr.octet[1], ifreq.addr.octet[2],
	    ifreq.addr.octet[3], ifreq.addr.octet[4], ifreq.addr.octet[5],
	    (if_type == WL_P2P_IF_GO) ? "go" : "client",
		(chspec & WL_CHANSPEC_CHAN_MASK) >> WL_CHANSPEC_CHAN_SHIFT));

	err = wldev_iovar_setbuf(netdev, "p2p_ifupd", &ifreq, sizeof(ifreq),
		ioctlbuf, sizeof(ioctlbuf));

	if (unlikely(err < 0)) {
		printk("'wl p2p_ifupd' error %d\n", err);
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
wl_cfgp2p_ifidx(struct wl_priv *wl, struct ether_addr *mac, s32 *index)
{
	s32 ret;
	u8 getbuf[64];
	struct net_device *dev = wl_to_prmry_ndev(wl);

	CFGP2P_INFO(("---wl p2p_if %02x:%02x:%02x:%02x:%02x:%02x\n",
	    mac->octet[0], mac->octet[1], mac->octet[2],
	    mac->octet[3], mac->octet[4], mac->octet[5]));

	ret = wldev_iovar_getbuf_bsscfg(dev, "p2p_if", mac, sizeof(*mac),
	            getbuf, sizeof(getbuf), wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_PRIMARY));

	if (ret == 0) {
		memcpy(index, getbuf, sizeof(index));
		CFGP2P_INFO(("---wl p2p_if   ==> %d\n", *index));
	}

	return ret;
}

s32
wl_cfgp2p_set_discovery(struct wl_priv *wl, s32 on)
{
	s32 ret = BCME_OK;
	struct net_device *ndev = wl_to_prmry_ndev(wl);
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
wl_cfgp2p_set_p2p_mode(struct wl_priv *wl, u8 mode, u32 channel, u16 listen_ms, int bssidx)
{
	wl_p2p_disc_st_t discovery_mode;
	s32 ret;
	struct net_device *dev;
	CFGP2P_DBG(("enter\n"));

	if (unlikely(bssidx >= P2PAPI_BSSCFG_MAX)) {
		CFGP2P_ERR((" %d index out of range\n", bssidx));
		return -1;
	}

	dev = wl_to_p2p_bss_ndev(wl, bssidx);
	if (unlikely(dev == NULL)) {
		CFGP2P_ERR(("bssidx %d is not assigned\n", bssidx));
		return BCME_NOTFOUND;
	}

	/* Put the WL driver into P2P Listen Mode to respond to P2P probe reqs */
	discovery_mode.state = mode;
	discovery_mode.chspec = CH20MHZ_CHSPEC(channel);
	discovery_mode.dwell = listen_ms;
	ret = wldev_iovar_setbuf_bsscfg(dev, "p2p_state", &discovery_mode,
	            sizeof(discovery_mode), ioctlbuf, sizeof(ioctlbuf), bssidx);

	return ret;
}

/* Get the index of the P2P Discovery BSS */
s32
wl_cfgp2p_get_disc_idx(struct wl_priv *wl, s32 *index)
{
	s32 ret;
	struct net_device *dev = wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_PRIMARY);

	ret = wldev_iovar_getint(dev, "p2p_dev", index);
	CFGP2P_INFO(("p2p_dev bsscfg_idx=%d ret=%d\n", *index, ret));

	if (unlikely(ret <  0)) {
	    CFGP2P_ERR(("'p2p_dev' error %d\n", ret));
		return ret;
	}
	return ret;
}

s32
wl_cfgp2p_init_discovery(struct wl_priv *wl)
{

	s32 index = 0;
	s32 ret = BCME_OK;

	CFGP2P_DBG(("enter\n"));

	if (wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE) != 0) {
		CFGP2P_ERR(("do nothing, already initialized\n"));
		return ret;
	}

	ret = wl_cfgp2p_set_discovery(wl, 1);
	if (ret < 0) {
		CFGP2P_ERR(("set discover error\n"));
		return ret;
	}
	/* Enable P2P Discovery in the WL Driver */
	ret = wl_cfgp2p_get_disc_idx(wl, &index);

	if (ret < 0) {
		return ret;
	}
	wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_DEVICE) =
	    wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_PRIMARY);
	wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE) = index;

	/* Set the initial discovery state to SCAN */
	ret = wl_cfgp2p_set_p2p_mode(wl, WL_P2P_DISC_ST_SCAN, 0, 0,
	        wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE));

	if (unlikely(ret != 0)) {
		CFGP2P_ERR(("unable to set WL_P2P_DISC_ST_SCAN\n"));
		wl_cfgp2p_set_discovery(wl, 0);
		wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE) = 0;
		wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_DEVICE) = NULL;
		return 0;
	}
	return ret;
}

/* Deinitialize P2P Discovery
 * Parameters :
 * @wl        : wl_private data
 * Returns 0 if succes
 */
s32
wl_cfgp2p_deinit_discovery(struct wl_priv *wl)
{
	s32 ret = BCME_OK;
	CFGP2P_DBG(("enter\n"));

	if (wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE) == 0) {
		CFGP2P_ERR(("do nothing, not initialized\n"));
		return -1;
	}
	/* Set the discovery state to SCAN */
	ret = wl_cfgp2p_set_p2p_mode(wl, WL_P2P_DISC_ST_SCAN, 0, 0,
	            wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE));
	/* Disable P2P discovery in the WL driver (deletes the discovery BSSCFG) */
	ret = wl_cfgp2p_set_discovery(wl, 0);

	/* Clear our saved WPS and P2P IEs for the discovery BSS.  The driver
	 * deleted these IEs when wl_cfgp2p_set_discovery() deleted the discovery
	 * BSS.
	 */

	/* Clear the saved bsscfg index of the discovery BSSCFG to indicate we
	 * have no discovery BSS.
	 */
	wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE) = 0;
	wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_DEVICE) = NULL;

	return ret;

}
/* Enable P2P Discovery
 * Parameters:
 * @wl	: wl_private data
 * @ie  : probe request ie (WPS IE + P2P IE)
 * @ie_len   : probe request ie length
 * Returns 0 if success.
 */
s32
wl_cfgp2p_enable_discovery(struct wl_priv *wl, struct net_device *dev, const u8 *ie, u32 ie_len)
{
	s32 ret = BCME_OK;
	if (test_bit(WLP2P_STATUS_DISCOVERY_ON, &wl->p2p_status)) {
		CFGP2P_INFO((" DISCOVERY is already initialized, we have nothing to do\n"));
		goto set_ie;
	}

	set_bit(WLP2P_STATUS_DISCOVERY_ON, &wl->p2p_status);

	CFGP2P_DBG(("enter\n"));

	ret = wl_cfgp2p_init_discovery(wl);
	if (unlikely(ret < 0)) {
		CFGP2P_ERR((" init discovery error %d\n", ret));
		goto exit;
	}
	/* Set wsec to any non-zero value in the discovery bsscfg to ensure our
	 * P2P probe responses have the privacy bit set in the 802.11 WPA IE.
	 * Some peer devices may not initiate WPS with us if this bit is not set.
	 */
	ret = wldev_iovar_setint_bsscfg(wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_DEVICE),
			"wsec", AES_ENABLED, wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE));
	if (unlikely(ret < 0)) {
		CFGP2P_ERR((" wsec error %d\n", ret));
	}
set_ie:
	ret = wl_cfgp2p_set_managment_ie(wl, dev,
	            wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE),
	            VNDR_IE_PRBREQ_FLAG, ie, ie_len);

	if (unlikely(ret < 0)) {
		CFGP2P_ERR(("set probreq ie occurs error %d\n", ret));
		goto exit;
	}
exit:
	return ret;
}

/* Disable P2P Discovery
 * Parameters:
 * @wl       : wl_private_data
 * Returns 0 if success.
 */
s32
wl_cfgp2p_disable_discovery(struct wl_priv *wl)
{
	s32 ret = BCME_OK;
	CFGP2P_DBG((" enter\n"));
	clear_bit(WLP2P_STATUS_DISCOVERY_ON, &wl->p2p_status);

	if (wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE) == 0) {
		CFGP2P_ERR((" do nothing, not initialized\n"));
		goto exit;
	}

	ret = wl_cfgp2p_set_p2p_mode(wl, WL_P2P_DISC_ST_SCAN, 0, 0,
	            wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE));

	if (unlikely(ret < 0)) {

		CFGP2P_ERR(("unable to set WL_P2P_DISC_ST_SCAN\n"));
	}
	/* Do a scan abort to stop the driver's scan engine in case it is still
	 * waiting out an action frame tx dwell time.
	 */
#ifdef NOT_YET
	if (test_bit(WLP2P_STATUS_SCANNING, &wl->p2p_status)) {
		p2pwlu_scan_abort(hdl, FALSE);
	}
#endif
	clear_bit(WLP2P_STATUS_DISCOVERY_ON, &wl->p2p_status);
	ret = wl_cfgp2p_deinit_discovery(wl);

exit:
	return ret;
}

s32
wl_cfgp2p_escan(struct wl_priv *wl, struct net_device *dev, u16 active,
	u32 num_chans, u16 *channels,
	s32 search_state, u16 action, u32 bssidx)
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
#define P2PAPI_SCAN_DWELL_TIME_MS 40
#define P2PAPI_SCAN_HOME_TIME_MS 10

	set_bit(WLP2P_STATUS_SCANNING, &wl->p2p_status);
	/* Allocate scan params which need space for 3 channels and 0 ssids */
	eparams_size = (WL_SCAN_PARAMS_FIXED_SIZE +
	    OFFSETOF(wl_escan_params_t, params)) +
		num_chans * sizeof(eparams->params.channel_list[0]);

	memsize = sizeof(wl_p2p_scan_t) + eparams_size;
	memblk = scanparambuf;
	if (memsize > sizeof(scanparambuf)) {
		CFGP2P_ERR((" scanpar buf too small (%u > %u)\n",
		    memsize, sizeof(scanparambuf)));
		return -1;
	}
	memset(memblk, 0, memsize);
	memset(ioctlbuf, 0, sizeof(ioctlbuf));
	if (search_state == WL_P2P_DISC_ST_SEARCH) {
		/*
		 * If we in SEARCH STATE, we don't need to set SSID explictly
		 * because dongle use P2P WILDCARD internally by default
		 */
		wl_cfgp2p_set_p2p_mode(wl, WL_P2P_DISC_ST_SEARCH, 0, 0, bssidx);
		ssid.SSID_len = htod32(0);

	} else if (search_state == WL_P2P_DISC_ST_SCAN) {
		/* SCAN STATE 802.11 SCAN
		 * WFD Supplicant has p2p_find command with (type=progressive, type= full)
		 * So if P2P_find command with type=progressive,
		 * we have to set ssid to P2P WILDCARD because
		 * we just do broadcast scan unless setting SSID
		 */
		strcpy(ssid.SSID, WL_P2P_WILDCARD_SSID);
		ssid.SSID_len = htod32(WL_P2P_WILDCARD_SSID_LEN);
		wl_cfgp2p_set_p2p_mode(wl, WL_P2P_DISC_ST_SCAN, 0, 0, bssidx);
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

	memcpy(&eparams->params.bssid, &ether_bcast, ETHER_ADDR_LEN);
	if (ssid.SSID_len)
		memcpy(&eparams->params.ssid, &ssid, sizeof(wlc_ssid_t));

	eparams->params.nprobes = htod32(P2PAPI_SCAN_NPROBES);
	eparams->params.home_time = htod32(P2PAPI_SCAN_HOME_TIME_MS);
	eparams->params.active_time = htod32(-1);
	eparams->params.passive_time = htod32(-1);
	eparams->params.channel_num = htod32((0 << WL_SCAN_PARAMS_NSSID_SHIFT) |
	    (num_chans & WL_SCAN_PARAMS_COUNT_MASK));

	for (i = 0; i < num_chans; i++) {
		eparams->params.channel_list[i] = htodchanspec(channels[i]);
	}
	eparams->version = htod32(ESCAN_REQ_VERSION);
	eparams->action =  htod16(action);
	eparams->sync_id = htod16(0x1234);
	CFGP2P_INFO(("SCAN CHANNELS : "));

	for (i = 0; i < num_chans; i++) {
		if (i == 0) CFGP2P_INFO(("%d", channels[i]));
		else CFGP2P_INFO((",%d", channels[i]));
	}

	CFGP2P_INFO(("\n"));

	ret = wldev_iovar_setbuf_bsscfg(dev, "p2p_scan",
	            memblk, memsize, smbuf, sizeof(ioctlbuf), bssidx);
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
/* Delete and Set a management ie to firmware
 * Parameters:
 * @wl       : wl_private data
 * @ndev     : net device for bssidx
 * @bssidx   : bssidx for BSS
 * @pktflag  : packet flag for IE (VNDR_IE_PRBREQ_FLAG,VNDR_IE_PRBRSP_FLAG, VNDR_IE_ASSOCRSP_FLAG,
 *                                 VNDR_IE_ASSOCREQ_FLAG)
 * @ie       : probe request ie (WPS IE + P2P IE)
 * @ie_len   : probe request ie length
 * Returns 0 if success.
 */

s32
wl_cfgp2p_set_managment_ie(struct wl_priv *wl, struct net_device *ndev, s32 bssidx,
    s32 pktflag, const u8 *p2p_ie, u32 p2p_ie_len)
{
	/* Vendor-specific Information Element ID */
#define VNDR_SPEC_ELEMENT_ID 0xdd
	s32 ret = BCME_OK;
	u32 pos;
	u8  *ie_buf;
	u8  *mgmt_ie_buf;
	u32 mgmt_ie_buf_len;
	u32 *mgmt_ie_len;
	u8 ie_id, ie_len;
	u8 delete = 0;
#define IE_TYPE(type, bsstype) (wl_to_p2p_bss_saved_ie(wl, bsstype).p2p_ ## type ## _ie)
#define IE_TYPE_LEN(type, bsstype) (wl_to_p2p_bss_saved_ie(wl, bsstype).p2p_ ## type ## _ie_len)
	if (bssidx == -1)
		return BCME_BADARG;
	if (bssidx == P2PAPI_BSSCFG_PRIMARY)
		bssidx =  wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE);
	switch (pktflag) {
		case VNDR_IE_PRBREQ_FLAG :
			mgmt_ie_buf = IE_TYPE(probe_req, bssidx);
			mgmt_ie_len = &IE_TYPE_LEN(probe_req, bssidx);
			mgmt_ie_buf_len = sizeof(IE_TYPE(probe_req, bssidx));
			break;
		case VNDR_IE_PRBRSP_FLAG :
			mgmt_ie_buf = IE_TYPE(probe_res, bssidx);
			mgmt_ie_len = &IE_TYPE_LEN(probe_res, bssidx);
			mgmt_ie_buf_len = sizeof(IE_TYPE(probe_res, bssidx));
			break;
		case VNDR_IE_ASSOCREQ_FLAG :
			mgmt_ie_buf = IE_TYPE(assoc_req, bssidx);
			mgmt_ie_len = &IE_TYPE_LEN(assoc_req, bssidx);
			mgmt_ie_buf_len = sizeof(IE_TYPE(assoc_req, bssidx));
			break;
		case VNDR_IE_ASSOCRSP_FLAG :
			mgmt_ie_buf = IE_TYPE(assoc_res, bssidx);
			mgmt_ie_len = &IE_TYPE_LEN(assoc_res, bssidx);
			mgmt_ie_buf_len = sizeof(IE_TYPE(assoc_res, bssidx));
			break;
		case VNDR_IE_BEACON_FLAG :
			mgmt_ie_buf = IE_TYPE(beacon, bssidx);
			mgmt_ie_len = &IE_TYPE_LEN(beacon, bssidx);
			mgmt_ie_buf_len = sizeof(IE_TYPE(beacon, bssidx));
			break;
		default:
			mgmt_ie_buf = NULL;
			mgmt_ie_len = NULL;
			CFGP2P_ERR(("not suitable type\n"));
			return -1;
	}
	/* Add if there is any extra IE */
	if (p2p_ie && p2p_ie_len) {
		CFGP2P_ERR(("Request has extra IE"));
		if (p2p_ie_len > mgmt_ie_buf_len) {
			CFGP2P_ERR(("extra IE size too big\n"));
			ret = -ENOMEM;
		} else {
			if (mgmt_ie_buf != NULL) {
				if ((p2p_ie_len == *mgmt_ie_len) &&
				     (memcmp(mgmt_ie_buf, p2p_ie, p2p_ie_len) == 0)) {
					CFGP2P_INFO(("Previous mgmt IE is equals to current IE"));
					goto exit;
				}
				pos = 0;
				delete = 1;
				ie_buf = (u8 *) mgmt_ie_buf;
				while (pos < *mgmt_ie_len) {
					ie_id = ie_buf[pos++];
					ie_len = ie_buf[pos++];
					CFGP2P_INFO(("DELELED ID(%d), Len(%d),"
						"OUI(%02x:%02x:%02x)\n",
						ie_id, ie_len, ie_buf[pos],
						ie_buf[pos+1], ie_buf[pos+2]));
					ret = wl_cfgp2p_vndr_ie(ndev, bssidx, pktflag,
					    ie_buf+pos, VNDR_SPEC_ELEMENT_ID,
						ie_buf+pos+3, ie_len-3, delete);
					pos += ie_len;
				}

			}
			/* save the current IE in wl struct */
			memcpy(mgmt_ie_buf, p2p_ie, p2p_ie_len);
			*mgmt_ie_len = p2p_ie_len;
			pos = 0;
			ie_buf = (u8 *) p2p_ie;
			delete = 0;
			while (pos < p2p_ie_len) {
				ie_id = ie_buf[pos++];
				ie_len = ie_buf[pos++];
				if ((ie_id == DOT11_MNG_VS_ID) &&
				   (wl_cfgp2p_is_wps_ie(&ie_buf[pos-2], NULL, 0) ||
					wl_cfgp2p_is_p2p_ie(&ie_buf[pos-2], NULL, 0))) {
					CFGP2P_INFO(("ADDED ID : %d, Len : %d , OUI :"
						"%02x:%02x:%02x\n", ie_id, ie_len, ie_buf[pos],
						ie_buf[pos+1], ie_buf[pos+2]));
					ret = wl_cfgp2p_vndr_ie(ndev, bssidx, pktflag, ie_buf+pos,
					    VNDR_SPEC_ELEMENT_ID, ie_buf+pos+3, ie_len-3, delete);
				}
				pos += ie_len;
			}
		}

	}
#undef IE_TYPE
#undef IE_TYPE_LEN
exit:
	return ret;
}

/* Clear the manament IE buffer of BSSCFG
 * Parameters:
 * @wl       : wl_private data
 * @bssidx   : bssidx for BSS
 *
 * Returns 0 if success.
 */
s32
wl_cfgp2p_clear_management_ie(struct wl_priv *wl, s32 bssidx)
{
#define INIT_IE(IE_TYPE, BSS_TYPE)		\
	do {							\
		memset(wl_to_p2p_bss_saved_ie(wl, BSS_TYPE).p2p_ ## IE_TYPE ## _ie, 0, \
		   sizeof(wl_to_p2p_bss_saved_ie(wl, BSS_TYPE).p2p_ ## IE_TYPE ## _ie)); \
		wl_to_p2p_bss_saved_ie(wl, BSS_TYPE).p2p_ ## IE_TYPE ## _ie_len = 0; \
	} while (0);
	if (bssidx < 0) {
		CFGP2P_ERR(("invalid bssidx\n"));
		return BCME_BADARG;
	}
	INIT_IE(probe_req, bssidx);
	INIT_IE(probe_res, bssidx);
	INIT_IE(assoc_req, bssidx);
	INIT_IE(assoc_res, bssidx);
	INIT_IE(beacon, bssidx);


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

static s32
wl_cfgp2p_vndr_ie(struct net_device *ndev, s32 bssidx, s32 pktflag,
            s8 *oui, s32 ie_id, s8 *data, s32 data_len, s32 delete)
{
	s32 err = BCME_OK;
	s32 buf_len;
	s32 iecount;

	vndr_ie_setbuf_t *ie_setbuf;

	/* Validate the pktflag parameter */
	if ((pktflag & ~(VNDR_IE_BEACON_FLAG | VNDR_IE_PRBRSP_FLAG |
	            VNDR_IE_ASSOCRSP_FLAG | VNDR_IE_AUTHRSP_FLAG |
	            VNDR_IE_PRBREQ_FLAG | VNDR_IE_ASSOCREQ_FLAG))) {
		CFGP2P_ERR(("p2pwl_vndr_ie: Invalid packet flag 0x%x\n", pktflag));
		return -1;
	}

	buf_len = sizeof(vndr_ie_setbuf_t) + data_len - 1;
	ie_setbuf = (vndr_ie_setbuf_t *) kzalloc(buf_len, GFP_KERNEL);

	CFGP2P_INFO((" ie_id : %02x, data length : %d\n", ie_id, data_len));
	if (!ie_setbuf) {

		CFGP2P_ERR(("Error allocating buffer for IE\n"));
		return -ENOMEM;
	}
	if (delete)
		strcpy(ie_setbuf->cmd, "del");
	else
		strcpy(ie_setbuf->cmd, "add");
	/* Buffer contains only 1 IE */
	iecount = htod32(1);
	memcpy((void *)&ie_setbuf->vndr_ie_buffer.iecount, &iecount, sizeof(int));
	pktflag = htod32(pktflag);
	memcpy((void *)&ie_setbuf->vndr_ie_buffer.vndr_ie_list[0].pktflag,
	    &pktflag, sizeof(uint32));
	ie_setbuf->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.id = ie_id;
	ie_setbuf->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.len
	        = (uchar)(data_len + VNDR_IE_MIN_LEN);
	memcpy(ie_setbuf->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui, oui, 3);
	memcpy(ie_setbuf->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.data, data, data_len);
	err = wldev_iovar_setbuf_bsscfg(ndev, "vndr_ie", ie_setbuf, buf_len,
		ioctlbuf, sizeof(ioctlbuf), bssidx);

	CFGP2P_INFO(("vndr_ie iovar returns %d\n", err));
	kfree(ie_setbuf);
	return err;
}

/*
 * Search the bssidx based on dev argument
 * Parameters:
 * @wl       : wl_private data
 * @ndev     : net device to search bssidx
 *  Returns bssidx for ndev
 */
s32
wl_cfgp2p_find_idx(struct wl_priv *wl, struct net_device *ndev)
{
	u32 i;
	s32 index = -1;

	if (ndev == NULL) {
		CFGP2P_ERR((" ndev is NULL\n"));
		goto exit;
	}
	for (i = 0; i < P2PAPI_BSSCFG_MAX; i++) {
	    if (ndev == wl_to_p2p_bss_ndev(wl, i)) {
			index = wl_to_p2p_bss_bssidx(wl, i);
			break;
		}
	}
exit:
	return index;
}
/*
 * Callback function for WLC_E_P2P_DISC_LISTEN_COMPLETE
 */
s32
wl_cfgp2p_listen_complete(struct wl_priv *wl, struct net_device *ndev,
            const wl_event_msg_t *e, void *data)
{
	s32 ret = BCME_OK;

	CFGP2P_DBG((" Enter\n"));
	/* TODO : have to acquire bottom half lock ? */
	if (test_bit(WLP2P_STATUS_LISTEN_EXPIRED, &wl->p2p_status) == 0) {
		set_bit(WLP2P_STATUS_LISTEN_EXPIRED, &wl->p2p_status);

		if (wl->listen_timer)
			del_timer_sync(wl->listen_timer);

		cfg80211_remain_on_channel_expired(ndev, wl->cache_cookie, &wl->remain_on_chan,
		    wl->remain_on_chan_type, GFP_KERNEL);
	}

	return ret;

}

/*
 *  Timer expire callback function for LISTEN
 *  We can't report cfg80211_remain_on_channel_expired from Timer ISR context, 
 *  so lets do it from thread context.
 */
static void
wl_cfgp2p_listen_expired(unsigned long data)
{
	wl_event_msg_t msg;
	struct wl_priv *wl = (struct wl_priv *) data;

	CFGP2P_DBG((" Enter\n"));
	msg.event_type =  hton32(WLC_E_P2P_DISC_LISTEN_COMPLETE);
	wl_cfg80211_event(wl_to_p2p_bss_ndev(wl, P2PAPI_BSSCFG_DEVICE), &msg, NULL, GFP_ATOMIC);
}

/* 
 * Do a P2P Listen on the given channel for the given duration.
 * A listen consists of sitting idle and responding to P2P probe requests
 * with a P2P probe response.
 *
 * This fn assumes dongle p2p device discovery is already enabled.
 * Parameters   :
 * @wl          : wl_private data
 * @channel     : channel to listen
 * @duration_ms : the time (milli seconds) to wait
 */
s32
wl_cfgp2p_discover_listen(struct wl_priv *wl, s32 channel, u32 duration_ms)
{
#define INIT_TIMER(timer, func, duration, extra_delay)	\
	do {                   \
		init_timer(timer); \
		timer->function = func; \
		timer->expires = jiffies + msecs_to_jiffies(duration + extra_delay); \
		timer->data = (unsigned long) wl; \
		add_timer(timer); \
	} while (0);

	s32 ret = BCME_OK;
	CFGP2P_DBG((" Enter\n"));
	CFGP2P_INFO(("Channel : %d, Duration : %d\n", channel, duration_ms));
	if (unlikely(test_bit(WLP2P_STATUS_DISCOVERY_ON, &wl->p2p_status) == 0)) {

		CFGP2P_ERR((" Discovery is not set, so we have noting to do\n"));

		ret = BCME_NOTREADY;
		goto exit;
	}

	clear_bit(WLP2P_STATUS_LISTEN_EXPIRED, &wl->p2p_status);

	wl_cfgp2p_set_p2p_mode(wl, WL_P2P_DISC_ST_LISTEN, channel, (u16) duration_ms,
	            wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE));

	if (wl->listen_timer)
		del_timer_sync(wl->listen_timer);

	wl->listen_timer = kmalloc(sizeof(struct timer_list), GFP_KERNEL);

	if (wl->listen_timer == NULL) {
		CFGP2P_ERR(("listen_timer allocation failed\n"));
		return -ENOMEM;
	}

	/*  We will wait to receive WLC_E_P2P_DISC_LISTEN_COMPLETE from dongle , 
	 *  otherwise we will wait up to duration_ms + 10ms
	 */
	INIT_TIMER(wl->listen_timer, wl_cfgp2p_listen_expired, duration_ms, 20);

#undef INIT_TIMER
exit:
	return ret;
}


s32
wl_cfgp2p_discover_enable_search(struct wl_priv *wl, u8 search_enable)
{
	s32 ret = BCME_OK;
	CFGP2P_DBG((" Enter\n"));
	if (!test_bit(WLP2P_STATUS_DISCOVERY_ON, &wl->p2p_status)) {

		CFGP2P_ERR((" do nothing, discovery is off\n"));
		return ret;
	}
	if (test_bit(WLP2P_STATUS_SEARCH_ENABLED, &wl->p2p_status) == search_enable) {
		CFGP2P_ERR(("already : %d\n", search_enable));
		return ret;
	}

	change_bit(WLP2P_STATUS_SEARCH_ENABLED, &wl->p2p_status);
	/* When disabling Search, reset the WL driver's p2p discovery state to
	 * WL_P2P_DISC_ST_SCAN.
	 */
	if (!search_enable) {
		clear_bit(WLP2P_STATUS_SCANNING, &wl->p2p_status);
		(void) wl_cfgp2p_set_p2p_mode(wl, WL_P2P_DISC_ST_SCAN, 0, 0,
		            wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE));
	}

	return ret;
}

/*
 * Callback function for WLC_E_ACTION_FRAME_COMPLETE, WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE
 */
s32
wl_cfgp2p_action_tx_complete(struct wl_priv *wl, struct net_device *ndev,
            const wl_event_msg_t *e, void *data)
{
	s32 ret = BCME_OK;
	u32 event_type = ntoh32(e->event_type);
	u32 status = ntoh32(e->status);
	CFGP2P_DBG((" Enter\n"));
	if (event_type == WLC_E_ACTION_FRAME_COMPLETE) {

		CFGP2P_INFO((" WLC_E_ACTION_FRAME_COMPLETE is received : %d\n", status));
		if (status == WLC_E_STATUS_SUCCESS)
			set_bit(WLP2P_STATUS_ACTION_TX_COMPLETED, &wl->p2p_status);
		else
			CFGP2P_ERR(("WLC_E_ACTION_FRAME_COMPLETE : NO ACK\n"));
		wake_up_interruptible(&wl->dongle_event_wait);
	} else {
		CFGP2P_INFO((" WLC_E_ACTION_FRAME_OFFCHAN_COMPLETE is received,"
					"status : %d\n", status));

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
wl_cfgp2p_tx_action_frame(struct wl_priv *wl, struct net_device *dev,
	wl_af_params_t *af_params, s32 bssidx)
{
	s32 ret = BCME_OK;
	s32 timeout = 0;


	CFGP2P_INFO(("\n"));
	CFGP2P_INFO(("channel : %u , dwell time : %u\n",
	    af_params->channel, af_params->dwell_time));

	clear_bit(WLP2P_STATUS_ACTION_TX_COMPLETED, &wl->p2p_status);
#define MAX_WAIT_TIME 2000
	if (bssidx == P2PAPI_BSSCFG_PRIMARY)
		bssidx =  wl_to_p2p_bss_bssidx(wl, P2PAPI_BSSCFG_DEVICE);

	ret = wldev_iovar_setbuf_bsscfg(dev, "actframe",
	           af_params, sizeof(*af_params), ioctlbuf, sizeof(ioctlbuf), bssidx);

	if (ret < 0) {

		CFGP2P_ERR((" sending action frame is failed\n"));
		goto exit;
	}
	timeout = wait_event_interruptible_timeout(wl->dongle_event_wait,
	                (test_bit(WLP2P_STATUS_ACTION_TX_COMPLETED, &wl->p2p_status) == TRUE),
	                    msecs_to_jiffies(MAX_WAIT_TIME));

	if (timeout > 0 && test_bit(WLP2P_STATUS_ACTION_TX_COMPLETED, &wl->p2p_status)) {
		CFGP2P_INFO(("tx action frame operation is completed\n"));
		ret = BCME_OK;
	} else {
		ret = BCME_ERROR;
		CFGP2P_INFO(("tx action frame operation is failed\n"));
	}
exit:
	CFGP2P_INFO((" via act frame iovar : status = %d\n", ret));
#undef MAX_WAIT_TIME
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
	out_int_addr->octet[4] ^= 0x80;

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
	                    sizeof(bsscfg_idx), getbuf, sizeof(getbuf), 0);
	if (result != 0) {
		CFGP2P_ERR(("'wl bss -C %d' failed: %d\n", bsscfg_idx, result));
		CFGP2P_ERR(("NOTE: this ioctl error is normal "
					"when the BSS has not been created yet.\n"));
	} else {
		val = *(int*)getbuf;
		val = dtoh32(val);
		CFGP2P_INFO(("---wl bss -C %d   ==> %d\n", bsscfg_idx, val));
		isup = (val ? TRUE : FALSE);
	}
	return isup;
}


/* Bring up or down a BSS */
s32
wl_cfgp2p_bss(struct net_device *ndev, s32 bsscfg_idx, s32 up)
{
	s32 ret = BCME_OK;
	s32 val = up ? 1 : 0;

	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;

	bss_setbuf.cfg = htod32(bsscfg_idx);
	bss_setbuf.val = htod32(val);
	CFGP2P_INFO(("---wl bss -C %d %s\n", bsscfg_idx, up ? "up" : "down"));
	ret = wldev_iovar_setbuf(ndev, "bss", &bss_setbuf, sizeof(bss_setbuf),
		ioctlbuf, sizeof(ioctlbuf));

	if (ret != 0) {
		CFGP2P_ERR(("'bss %d' failed with %d\n", up, ret));
	}

	return ret;
}

/* Check if 'p2p' is supported in the driver */
s32
wl_cfgp2p_is_p2p_supported(struct wl_priv *wl, struct net_device *ndev)
{
	s32 ret = BCME_OK;
	s32 is_p2p_supported = 0;
	ret = wldev_iovar_getint(ndev, "p2p",
	               &is_p2p_supported);
	if (ret < 0) {
	    CFGP2P_ERR(("wl p2p error %d\n", ret));
		return 0;
	}
	if (is_p2p_supported)
		CFGP2P_INFO(("p2p is supported\n"));

	return is_p2p_supported;
}

s32
wl_cfgp2p_down(struct wl_priv *wl)
{
	if (wl->listen_timer)
		del_timer_sync(wl->listen_timer);
	return 0;
}
