/*
 * Band Steering logic
 *
 * Feature by which dualband capable PEERs will be
 * forced move on 5GHz interface
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
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $ Copyright Cypress Semiconductor $
 *
 * $Id: dhd_bandsteer.c 724689 2020-03-04 10:04:03Z $
 */

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <bcmutils.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <wl_cfg80211.h>
#include <wl_android.h>
#include <wldev_common.h>
#include <dhd_linux_wq.h>
#include <dhd_cfg80211.h>
#include <dhd_bandsteer.h>
#include <dhd_dbg.h>

/* defines */
/* BANDSTEER STATE MACHINE STATES */
#define DHD_BANDSTEER_START			0x0001
#define DHD_BANDSTEER_WNM_FRAME_SEND		0x0002
#define DHD_BANDSTEER_WNM_FRAME_RETRY		0x0004
#define DHD_BANDSTEER_WNM_FRAME_SENT		0x0008
#define DHD_BANDSTEER_TRIAL_DONE		0x0080

#define DHD_BANDSTEER_ON_PROCESS_MASK (DHD_BANDSTEER_START | DHD_BANDSTEER_WNM_FRAME_SEND \
	| DHD_BANDSTEER_WNM_FRAME_RETRY | DHD_BANDSTEER_TRIAL_DONE)

#define DHD_BANDSTEER_WNM_FRAME_MAXRETRY 3
#define DHD_BANDSTEER_WNM_FRAME_DELAY 1000
#define DHD_BANDSTEER_WNM_FRAME_START_DELAY 10
#define DHD_BANDSTEER_WNM_FRAME_RESPONSE_DWELL 40
#define MAX_NUM_OF_ASSOCLIST    64
#define DHD_BANDSTEER_MAXIFACES 2

#define CHANNEL_IS_5G(channel)  (((channel >= 36) && (channel <= 165)) ? \
true : false)

/* ********************** Function declaration *********************** */
static s32
dhd_bandsteer_addmac_to_monitorlist(dhd_bandsteer_context_t *dhd_bandsteer_cntx,  uint8 *mac_addr);
static s32
dhd_bandsteer_remove_mac_from_list(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac, int all);
static dhd_bandsteer_mac_entry_t*
dhd_bandsteer_look_for_match(dhd_bandsteer_context_t *dhd_bandsteer_cntx, uint8 *mac_addr);
static s32
dhd_bandsteer_tx_wnm_actframe(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac);
static void
dhd_bandsteer_add_to_black_list(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac);

extern int
wl_android_set_ap_mac_list(struct net_device *dev, int macmode, struct maclist *maclist);

/* ********************** Function declartion ends ****************** */

static void
dhd_bandsteer_add_timer(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac, unsigned long msec)
{
	timer_expires(&dhd_bandsteer_mac->dhd_bandsteer_timer) =
		jiffies + msecs_to_jiffies(msec);
	add_timer(&dhd_bandsteer_mac->dhd_bandsteer_timer);
}

static void
dhd_bandsteer_delete_timer(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac)
{
	del_timer(&dhd_bandsteer_mac->dhd_bandsteer_timer);
}

/*
 * Idea used to call same callback everytime you conifigure timer
 * based on the status of the mac entry next step will be taken
 */
static void
dhd_bandsteer_state_machine(ulong arg)
{
	dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac = (dhd_bandsteer_mac_entry_t *)arg;

	if (dhd_bandsteer_mac == NULL) {
		DHD_ERROR(("%s: dhd_bandsteer_mac is null\n", __FUNCTION__));
		return;
	}

	DHD_TRACE(("%s: Peer STA BandSteer status 0x%x", __FUNCTION__,
			dhd_bandsteer_mac->dhd_bandsteer_status));

	switch (dhd_bandsteer_mac->dhd_bandsteer_status) {
	case DHD_BANDSTEER_START:
	case DHD_BANDSTEER_WNM_FRAME_RETRY:
		dhd_bandsteer_mac->dhd_bandsteer_status = DHD_BANDSTEER_WNM_FRAME_SEND;
		dhd_bandsteer_schedule_work_on_timeout(dhd_bandsteer_mac);
		break;

	case  DHD_BANDSTEER_WNM_FRAME_SEND:
		dhd_bandsteer_tx_wnm_actframe(dhd_bandsteer_mac);
		if (dhd_bandsteer_mac->wnm_frame_counter < DHD_BANDSTEER_WNM_FRAME_MAXRETRY) {
			/* Sending out WNM action frame as soon as assoc indication recieved */
			dhd_bandsteer_mac->dhd_bandsteer_status = DHD_BANDSTEER_WNM_FRAME_RETRY;
		}
		else {
			dhd_bandsteer_mac->dhd_bandsteer_status = DHD_BANDSTEER_TRIAL_DONE;
		}
		break;
	case DHD_BANDSTEER_TRIAL_DONE:
		dhd_bandsteer_remove_mac_from_list(dhd_bandsteer_mac, 0);
		break;
	}
	return;
}

void
dhd_bandsteer_workqueue_wrapper(void *handle, void *event_info, u8 event)
{
	dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac = (dhd_bandsteer_mac_entry_t *)event_info;

	if (event != DHD_WQ_WORK_BANDSTEER_STEP_MOVE) {
		DHD_ERROR(("%s: unexpected event \n", __FUNCTION__));
		return;
	}

	dhd_bandsteer_state_machine((ulong)dhd_bandsteer_mac);
}

/*
 *	This API create and initilize an entry into list which need to be processed later
 */
static s32
dhd_bandsteer_addmac_to_monitorlist(dhd_bandsteer_context_t *dhd_bandsteer_cntx,  uint8 *mac_addr)
{
	dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac;

	dhd_bandsteer_mac = kzalloc(sizeof(dhd_bandsteer_mac_entry_t), GFP_KERNEL);
	if (unlikely(!dhd_bandsteer_mac)) {
		DHD_ERROR(("%s: alloc failed\n", __FUNCTION__));
		return BCME_NOMEM;
	}
	INIT_LIST_HEAD(&dhd_bandsteer_mac->list);
	/* pointer dhd_bandsteer_cntx for future use */
	dhd_bandsteer_mac->dhd_bandsteer_cntx = dhd_bandsteer_cntx;

	dhd_bandsteer_mac->dhd_bandsteer_status = DHD_BANDSTEER_START;

	memcpy(&dhd_bandsteer_mac->mac_addr.octet, mac_addr, ETHER_ADDR_LEN);

	/* Configure timer for 20 Sec */
	init_timer_compat(&dhd_bandsteer_mac->dhd_bandsteer_timer, dhd_bandsteer_state_machine,
			dhd_bandsteer_mac);
	dhd_bandsteer_mac->wnm_frame_counter = 0;

	/* Add new entry into the list */
	list_add_tail(&dhd_bandsteer_mac->list,
			&dhd_bandsteer_cntx->dhd_bandsteer_monitor_list);

	DHD_TRACE(("%s: " MACDBG " added into list \n", __FUNCTION__,
			MAC2STRDBG(dhd_bandsteer_mac->mac_addr.octet)));
	/* This can be tweaked more */
	dhd_bandsteer_add_timer(dhd_bandsteer_mac, DHD_BANDSTEER_WNM_FRAME_START_DELAY);

	return BCME_OK;
}

/*
 * This function removes one or all mac entry from the list
 */
static s32
dhd_bandsteer_remove_mac_from_list(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac, int all)
{
	dhd_bandsteer_context_t *dhd_bandsteer_cntx;
	dhd_bandsteer_mac_entry_t *curr, *next;

	DHD_INFO(("%s: entered \n", __FUNCTION__));
	/* TODO:probably these sanity lines can be removed */
	if (dhd_bandsteer_mac == NULL) {
		DHD_ERROR(("%s: dhd_bandsteer_mac is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dhd_bandsteer_cntx = dhd_bandsteer_mac->dhd_bandsteer_cntx;

	list_for_each_entry_safe(curr, next,
			&dhd_bandsteer_cntx->dhd_bandsteer_monitor_list, list) {

		if (curr == NULL) {
			DHD_ERROR(("%s: Invalid MAC\n", __FUNCTION__));
			break;
		}

		if ((curr == dhd_bandsteer_mac) || all) {
			DHD_ERROR(("%s: " MACDBG " deleted from list \n", __FUNCTION__,
					MAC2STRDBG(dhd_bandsteer_mac->mac_addr.octet)));
			list_del(&curr->list);
			dhd_bandsteer_delete_timer(curr);
			kfree(curr);
			if (!all)
				break;
		}
	}
	return BCME_OK;
}

/*
 * Logic to find corresponding node in list based given mac address
 * Returns null if entry not seen
 */
static dhd_bandsteer_mac_entry_t*
dhd_bandsteer_look_for_match(dhd_bandsteer_context_t *dhd_bandsteer_cntx, uint8 *mac_addr)
{
	dhd_bandsteer_mac_entry_t *curr = NULL, *next = NULL;

	list_for_each_entry_safe(curr, next,
			&dhd_bandsteer_cntx->dhd_bandsteer_monitor_list, list) {
		if (memcmp(&curr->mac_addr.octet, mac_addr, ETHER_ADDR_LEN) == 0) {
			return curr;
		}
	}
	return NULL;
}

/*
 * This API will send wnm action frame also configure timeout timer
 */
static s32
dhd_bandsteer_tx_wnm_actframe(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac)
{
	dhd_bandsteer_context_t *dhd_bandsteer_cntx = dhd_bandsteer_mac->dhd_bandsteer_cntx;
	wl_action_frame_t *action_frame = NULL;
	wl_af_params_t *af_params = NULL;
	char *smbuf = NULL;
	int error = BCME_ERROR;
	uint8 *bp;
	dhd_bandsteer_iface_info_t *if_info_5g, *if_info_2g;

	if_info_5g = &dhd_bandsteer_cntx->bsd_ifaces[dhd_bandsteer_cntx->ifidx_5g];
	if_info_2g = &dhd_bandsteer_cntx->bsd_ifaces[!dhd_bandsteer_cntx->ifidx_5g];

	smbuf = kzalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (smbuf == NULL) {
		DHD_ERROR(("%s: failed to allocated memory %d bytes\n", __FUNCTION__,
				WLC_IOCTL_MAXLEN));
		goto send_action_frame_out;
	}

	af_params = (wl_af_params_t *) kzalloc(WL_WIFI_AF_PARAMS_SIZE, GFP_KERNEL);
	if (af_params == NULL) {
		DHD_ERROR(("%s: unable to allocate frame\n", __FUNCTION__));
		goto send_action_frame_out;
	}

	af_params->channel = if_info_2g->channel;
	af_params->dwell_time = DHD_BANDSTEER_WNM_FRAME_RESPONSE_DWELL;
	memcpy(&af_params->BSSID, &dhd_bandsteer_mac->mac_addr.octet, ETHER_ADDR_LEN);
	action_frame = &af_params->action_frame;

	action_frame->packetId = 0;
	memcpy(&action_frame->da, &dhd_bandsteer_mac->mac_addr.octet, ETHER_ADDR_LEN);

	dhd_bandsteer_mac->wnm_frame_counter++;
	bp = (uint8 *)action_frame->data;
	*bp++ = 0xa; /* Category */
	*bp++ = 0x7; /* Action ID */
	*bp++ = dhd_bandsteer_mac->wnm_frame_counter; /* Dialog Token */
	*bp++ = 0x1; /* Request mode */
	*bp++ = 0x0; /* disassociation timer has two bytes */
	*bp++ = 0x0;
	*bp++ = 0x0; /* Validity interval */
	*bp++ = 0x34; /* Element ID */
	*bp++ = 0xd; /* Len */
	memcpy(bp, if_info_5g->macaddr.octet, ETHER_ADDR_LEN);
	bp += ETHER_ADDR_LEN;
	bp +=4; /* Skip BSSID info 4 bytes in size */
	*bp++ = 0x7d; /* Operating class */
	*bp++ = if_info_5g->channel; /* Channel number */
	*bp = 0x0; /* Phy Type */

	action_frame->len = (bp - (uint8 *)&action_frame->data) + 1;

	error = wldev_iovar_setbuf(if_info_2g->ndev, "actframe", af_params,
			sizeof(wl_af_params_t), smbuf, WLC_IOCTL_MAXLEN, NULL);
	if (error) {
		DHD_ERROR(("Failed to set action frame, error=%d\n", error));
		goto send_action_frame_out;
	}
	DHD_TRACE(("%s: BSS Trans Req frame sent to " MACDBG " try %d\n", __FUNCTION__,
			MAC2STRDBG(dhd_bandsteer_mac->mac_addr.octet),
			dhd_bandsteer_mac->wnm_frame_counter));

send_action_frame_out:
	/* Re-schedule the timer */
	dhd_bandsteer_add_timer(dhd_bandsteer_mac, DHD_BANDSTEER_WNM_FRAME_DELAY);
	if (af_params)
		kfree(af_params);

	if (smbuf)
		kfree(smbuf);

	if (error)
		return BCME_ERROR;

	return BCME_OK;
}

/*
 * Call dhd_bandsteer_remove_mac_from_list()
 * Add into black list of corresponding interace at present 2.4
 * Uses existing IOVAR calls
 */
static void
dhd_bandsteer_add_to_black_list(dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac)
{
	dhd_bandsteer_context_t *dhd_bandsteer_cntx = dhd_bandsteer_mac->dhd_bandsteer_cntx;
	dhd_bandsteer_iface_info_t *if_info_2g;
	int err;
	int macmode = MACLIST_MODE_DENY;
	struct maclist *maclist;
	uint8 *pmaclist_ea;
	uint8 mac_buf[MAX_NUM_OF_ASSOCLIST *
		sizeof(struct ether_addr) + sizeof(uint)] = {0};

	if_info_2g = &dhd_bandsteer_cntx->bsd_ifaces[!dhd_bandsteer_cntx->ifidx_5g];

	/* Black listing */
	DHD_INFO(("%s: Black listing " MACDBG " on 2GHz IF\n", __FUNCTION__,
			MAC2STRDBG(dhd_bandsteer_mac->mac_addr.octet)));

	/* Get current black list */
	if ((err = wldev_ioctl_get(if_info_2g->ndev, WLC_GET_MACLIST, mac_buf,
			sizeof(mac_buf))) != 0) {
		DHD_ERROR(("%s: WLC_GET_MACLIST error=%d\n", __FUNCTION__, err));
	}

	maclist = (struct maclist *)mac_buf;
	pmaclist_ea = (uint8*) mac_buf +
			(sizeof(struct ether_addr) * maclist->count) + sizeof(uint);
	maclist->count++;

	memcpy(pmaclist_ea, &dhd_bandsteer_mac->mac_addr.octet, ETHER_ADDR_LEN);

	if ((err = wldev_ioctl_set(if_info_2g->ndev, WLC_SET_MACMODE, &macmode,
			sizeof(macmode))) != 0) {
		DHD_ERROR(("%s: WLC_SET_MACMODE error=%d\n", __FUNCTION__, err));
	}

	/* set the MAC filter list */
	if ((err = wldev_ioctl_set(if_info_2g->ndev, WLC_SET_MACLIST, maclist,
			sizeof(int) + sizeof(struct ether_addr) * maclist->count)) != 0) {
		DHD_ERROR(("%s: WLC_SET_MACLIST error=%d\n", __FUNCTION__, err));
	}
}

/*
 * Check if mac association on 2.4 G
 * If on 2.4
 *      * Ignore if we have already run Bandsteer cycle for this
 *	* Add PEER STA mac monitor_list
 *	* Send BSS transition request frame
 * Else
 *	* We recieved assoc on 5g from Mac we forced to move onto 5G
 */
s32
dhd_bandsteer_trigger_bandsteer(struct net_device *ndev, uint8 *mac_addr)
{
	struct wireless_dev *__wdev = (struct wireless_dev *)(ndev)->ieee80211_ptr;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(__wdev->wiphy);
	struct net_device *netdev_5g = NULL;
	dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac = NULL;
	dhd_bandsteer_context_t *dhd_bandsteer_cntx = NULL;

	DHD_ERROR(("%s: Start band-steer procedure for " MACDBG "\n", __FUNCTION__,
			MAC2STRDBG(mac_addr)));

	if (cfg == NULL) {
		DHD_ERROR(("%s: bcmcfg is null\n", __FUNCTION__));
		return BCME_ERROR;
	}
	if (cfg->dhd_bandsteer_cntx == NULL) {
		DHD_ERROR(("%s: Band Steering not enabled\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dhd_bandsteer_cntx = cfg->dhd_bandsteer_cntx;

	netdev_5g = dhd_bandsteer_cntx->bsd_ifaces[dhd_bandsteer_cntx->ifidx_5g].ndev;
	dhd_bandsteer_mac = dhd_bandsteer_look_for_match(dhd_bandsteer_cntx, mac_addr);
	if (dhd_bandsteer_mac == NULL) {
		/*
		 * This STA entry not found in list check if  bandsteer is already done for this
		 * Check if this on 2.4/5Ghz
		 */
		if (ndev == netdev_5g) {
			/* Ignore as device aleady connectd to 5G */
			DHD_ERROR(("%s: " MACDBG " is on 5GHz interface\n", __FUNCTION__,
			 MAC2STRDBG(mac_addr)));
			dhd_bandsteer_mac = kzalloc(sizeof(dhd_bandsteer_mac_entry_t), GFP_KERNEL);
			if (unlikely(!dhd_bandsteer_mac)) {
				DHD_ERROR(("%s: alloc failed\n", __FUNCTION__));
				return BCME_NOMEM;
			}
			dhd_bandsteer_mac->dhd_bandsteer_cntx = dhd_bandsteer_cntx;
			memcpy(&dhd_bandsteer_mac->mac_addr.octet, mac_addr, ETHER_ADDR_LEN);
			dhd_bandsteer_add_to_black_list(dhd_bandsteer_mac);
			kfree(dhd_bandsteer_mac);
			dhd_bandsteer_mac = NULL;
			return BCME_OK;
		} else {
			DHD_INFO(("%s: dhd_bandsteer_addmac_to_monitorlist\n", __FUNCTION__));
			dhd_bandsteer_addmac_to_monitorlist(dhd_bandsteer_cntx, mac_addr);
			/*
			 * TODO: Time for us to enable PROB_REQ MSG
			 */
		}
	} else {
		/*
		 * Start post connect process as bandsteer is successful  for this entry
		 */
		if (ndev == netdev_5g) {
			DHD_ERROR(("%s: Band Steer for " MACDBG " successful\n", __FUNCTION__,
					MAC2STRDBG(mac_addr)));
			dhd_bandsteer_add_to_black_list(dhd_bandsteer_mac);
			dhd_bandsteer_remove_mac_from_list(dhd_bandsteer_mac, 0);
			/* Probabaly add this mac into black list */
		}
	}
	return BCME_OK;
}

s32
dhd_bandsteer_module_init(struct net_device *ndev, bool ap, bool p2p)
{
	/* Initialize */
	dhd_bandsteer_context_t *dhd_bandsteer_cntx = NULL;
	struct channel_info ci;
	uint8 ifidx;
	struct wireless_dev *__wdev = (struct wireless_dev *)(ndev)->ieee80211_ptr;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(__wdev->wiphy);
	int err;

	DHD_INFO(("%s: entered\n", __FUNCTION__));

	if (cfg == NULL) {
		DHD_ERROR(("%s: bcmcfg is  null\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (cfg->dhd_bandsteer_cntx != NULL) {
		DHD_ERROR(("%s: Band Steering already enabled\n", __FUNCTION__));
		goto init_done;
	}

	dhd_bandsteer_cntx = (dhd_bandsteer_context_t *)kzalloc(sizeof(dhd_bandsteer_context_t),
			GFP_KERNEL);
	if (unlikely(!dhd_bandsteer_cntx)) {
		DHD_ERROR(("%s: dhd_bandsteer_cntx alloc failed\n", __FUNCTION__));
		return BCME_NOMEM;
	}

	if (dhd_bandsteer_get_ifaces(cfg->pub, &dhd_bandsteer_cntx->bsd_ifaces)) {
		DHD_ERROR(("%s: AP interfaces count != 2", __FUNCTION__));
		err = BCME_ERROR;
		goto failed;
	}

	for (ifidx = 0; ifidx < DHD_BANDSTEER_MAXIFACES; ifidx++) {
		err = wldev_iovar_getbuf_bsscfg(dhd_bandsteer_cntx->bsd_ifaces[ifidx].ndev,
			 "cur_etheraddr", NULL, 0, cfg->ioctl_buf,
				WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);
		if (err) {
			DHD_ERROR(("%s: Failed to get mac address\n", __FUNCTION__));
			goto failed;
		}

		memcpy(dhd_bandsteer_cntx->bsd_ifaces[ifidx].macaddr.octet,
				cfg->ioctl_buf, ETHER_ADDR_LEN);

		memset(&ci, 0, sizeof(struct channel_info));
		err = wldev_ioctl_get(dhd_bandsteer_cntx->bsd_ifaces[ifidx].ndev, WLC_GET_CHANNEL,
				&ci, sizeof(ci));
		if (err) {
			DHD_ERROR(("%s: Failed to get channel\n", __FUNCTION__));
			goto failed;
		}
		if (CHANNEL_IS_5G(ci.hw_channel))
			dhd_bandsteer_cntx->ifidx_5g = ifidx;

		dhd_bandsteer_cntx->bsd_ifaces[ifidx].channel = ci.hw_channel;
	}

	if (ap) {
		INIT_LIST_HEAD(&dhd_bandsteer_cntx->dhd_bandsteer_monitor_list);
		dhd_bandsteer_cntx->dhd_pub = cfg->pub;
		cfg->dhd_bandsteer_cntx = (void *) dhd_bandsteer_cntx;
	}

	/*
	 * Enabling iovar "probresp_sw" suppresses probe request as a result of
	 * which p2p discovery for only 2G capable STA fails. Hence commenting for now.
	 *
	 */

init_done:
	/* Enable p2p bandsteer on 2GHz interface */
	if (p2p) {
		if (dhd_bandsteer_cntx == NULL)
			dhd_bandsteer_cntx = cfg->dhd_bandsteer_cntx;

		if ((err = wldev_iovar_setint(
			dhd_bandsteer_cntx->bsd_ifaces[!dhd_bandsteer_cntx->ifidx_5g].ndev,
			"bandsteer", 1)) != BCME_OK) {
			DHD_ERROR(("%s: Failed to enable bandsteer in FW err = %d\n",
				__FUNCTION__, err));
		}
	}

	DHD_INFO(("%s: exited\n", __FUNCTION__));
	return BCME_OK;

failed:
	kfree(dhd_bandsteer_cntx);
	return err;
}

s32
dhd_bandsteer_module_deinit(struct net_device *ndev, bool ap, bool p2p)
{
	dhd_bandsteer_mac_entry_t *dhd_bandsteer_mac = NULL;
	dhd_bandsteer_context_t *dhd_bandsteer_cntx = NULL;
	struct wireless_dev *__wdev = (struct wireless_dev *)(ndev)->ieee80211_ptr;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(__wdev->wiphy);
	int macmode = MACLIST_MODE_DISABLED;
	int err;
	struct maclist maclist;

	DHD_INFO(("%s: entered\n", __FUNCTION__));

	if (cfg == NULL) {
		DHD_ERROR(("%s: bcmcfg is null\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (cfg->dhd_bandsteer_cntx == NULL) {
		DHD_ERROR(("%s: Band Steering not enabled\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dhd_bandsteer_cntx = cfg->dhd_bandsteer_cntx;

	if (ap) {
		/* Disable mac filter */
		if ((err = wldev_ioctl_set(
				dhd_bandsteer_cntx->bsd_ifaces[!dhd_bandsteer_cntx->ifidx_5g].ndev,
				WLC_SET_MACMODE, &macmode, sizeof(macmode))) != 0) {
			DHD_ERROR(("%s: WLC_SET_MACMODE error=%d\n", __FUNCTION__, err));
		}

		/* Set the MAC filter list */
		memset(&maclist, 0, sizeof(struct maclist));
		if ((err = wldev_ioctl_set(
				dhd_bandsteer_cntx->bsd_ifaces[!dhd_bandsteer_cntx->ifidx_5g].ndev,
				WLC_SET_MACLIST, &maclist, sizeof(struct maclist))) != 0) {
			DHD_ERROR(("%s: WLC_SET_MACLIST error=%d\n", __FUNCTION__, err));
		}
	}

	/* Disable p2p bandsteer on 2GHz interface */
	if (p2p) {
		if ((err = wldev_iovar_setint(
				dhd_bandsteer_cntx->bsd_ifaces[!dhd_bandsteer_cntx->ifidx_5g].ndev,
				"bandsteer", 0)) != BCME_OK) {
			DHD_ERROR(("%s: Failed to enable bandsteer in FW err = %d\n",
				__FUNCTION__, err));
		}
	}

	if (ap) {
		/* Get the first element of the list & pass it to remove */
		if (dhd_bandsteer_cntx->dhd_bandsteer_monitor_list.next  !=
				&dhd_bandsteer_cntx->dhd_bandsteer_monitor_list) {
			dhd_bandsteer_mac = (dhd_bandsteer_mac_entry_t *)list_entry(
					dhd_bandsteer_cntx->dhd_bandsteer_monitor_list.next,
					dhd_bandsteer_mac_entry_t, list);
		}

		if (dhd_bandsteer_mac) {
			dhd_bandsteer_remove_mac_from_list(dhd_bandsteer_mac, 1);
		}
		kfree(dhd_bandsteer_cntx);
		cfg->dhd_bandsteer_cntx = NULL;
	}

	DHD_INFO(("%s: exited\n", __FUNCTION__));
	return BCME_OK;
}
