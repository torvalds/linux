/*
 * Linux cfg80211 driver - Dongle Host Driver (DHD) related
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
 * $Id: wl_cfg80211.c,v 1.1.4.1.2.14 2011/02/09 01:40:07 Exp $
 */

#include <net/rtnetlink.h>

#include <bcmutils.h>
#include <wldev_common.h>
#include <wl_cfg80211.h>
#include <dhd_cfg80211.h>
extern struct wl_priv *wlcfg_drv_priv;
static int dhd_dongle_up = FALSE;

#include <dngl_stats.h>
#include <dhd.h>
#include <dhdioctl.h>
#include <wlioctl.h>
#include <dhd_cfg80211.h>

static s32 wl_dongle_up(struct net_device *ndev, u32 up);

/**
 * Function implementations
 */

s32 dhd_cfg80211_init(struct wl_priv *wl)
{
	dhd_dongle_up = FALSE;
	return 0;
}

s32 dhd_cfg80211_deinit(struct wl_priv *wl)
{
	dhd_dongle_up = FALSE;
	return 0;
}

s32 dhd_cfg80211_down(struct wl_priv *wl)
{
	dhd_dongle_up = FALSE;
	return 0;
}

s32 dhd_cfg80211_set_p2p_info(struct wl_priv *wl, int val)
{
	dhd_pub_t *dhd =  (dhd_pub_t *)(wl->pub);
	dhd->op_mode |= val;
	WL_ERR(("Set : op_mode=%d\n", dhd->op_mode));
	return 0;
}

s32 dhd_cfg80211_clean_p2p_info(struct wl_priv *wl)
{
	dhd_pub_t *dhd =  (dhd_pub_t *)(wl->pub);
	dhd->op_mode &= ~CONCURENT_MASK;
	WL_ERR(("Clean : op_mode=%d\n", dhd->op_mode));
	return 0;
}

static s32 wl_dongle_up(struct net_device *ndev, u32 up)
{
	s32 err = 0;

	err = wldev_ioctl(ndev, WLC_UP, &up, sizeof(up), true);
	if (unlikely(err)) {
		WL_ERR(("WLC_UP error (%d)\n", err));
	}
	return err;
}

s32 dhd_config_dongle(struct wl_priv *wl, bool need_lock)
{
#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
	struct net_device *ndev;
	s32 err = 0;

	WL_TRACE(("In\n"));
	if (dhd_dongle_up) {
		WL_ERR(("Dongle is already up\n"));
		return err;
	}

	ndev = wl_to_prmry_ndev(wl);

	if (need_lock)
		rtnl_lock();

	err = wl_dongle_up(ndev, 0);
	if (unlikely(err)) {
		WL_ERR(("wl_dongle_up failed\n"));
		goto default_conf_out;
	}
	dhd_dongle_up = true;

default_conf_out:
	if (need_lock)
		rtnl_unlock();
	return err;

}


/* TODO: clean up the BT-Coex code, it still have some legacy ioctl/iovar functions */
#define COEX_DHCP

#if defined(COEX_DHCP)

/* use New SCO/eSCO smart YG suppression */
#define BT_DHCP_eSCO_FIX
/* this flag boost wifi pkt priority to max, caution: -not fair to sco */
#define BT_DHCP_USE_FLAGS
/* T1 start SCO/ESCo priority suppression */
#define BT_DHCP_OPPR_WIN_TIME	2500
/* T2 turn off SCO/SCO supperesion is (timeout) */
#define BT_DHCP_FLAG_FORCE_TIME 5500

enum wl_cfg80211_btcoex_status {
	BT_DHCP_IDLE,
	BT_DHCP_START,
	BT_DHCP_OPPR_WIN,
	BT_DHCP_FLAG_FORCE_TIMEOUT
};

/*
 * get named driver variable to uint register value and return error indication
 * calling example: dev_wlc_intvar_get_reg(dev, "btc_params",66, &reg_value)
 */
static int
dev_wlc_intvar_get_reg(struct net_device *dev, char *name,
	uint reg, int *retval)
{
	union {
		char buf[WLC_IOCTL_SMLEN];
		int val;
	} var;
	int error;

	bcm_mkiovar(name, (char *)(&reg), sizeof(reg),
		(char *)(&var), sizeof(var.buf));
	error = wldev_ioctl(dev, WLC_GET_VAR, (char *)(&var), sizeof(var.buf), false);

	*retval = dtoh32(var.val);
	return (error);
}

static int
dev_wlc_bufvar_set(struct net_device *dev, char *name, char *buf, int len)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
	char ioctlbuf_local[1024];
#else
	static char ioctlbuf_local[1024];
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31) */

	bcm_mkiovar(name, buf, len, ioctlbuf_local, sizeof(ioctlbuf_local));

	return (wldev_ioctl(dev, WLC_SET_VAR, ioctlbuf_local, sizeof(ioctlbuf_local), true));
}
/*
get named driver variable to uint register value and return error indication
calling example: dev_wlc_intvar_set_reg(dev, "btc_params",66, value)
*/
static int
dev_wlc_intvar_set_reg(struct net_device *dev, char *name, char *addr, char * val)
{
	char reg_addr[8];

	memset(reg_addr, 0, sizeof(reg_addr));
	memcpy((char *)&reg_addr[0], (char *)addr, 4);
	memcpy((char *)&reg_addr[4], (char *)val, 4);

	return (dev_wlc_bufvar_set(dev, name, (char *)&reg_addr[0], sizeof(reg_addr)));
}

static bool btcoex_is_sco_active(struct net_device *dev)
{
	int ioc_res = 0;
	bool res = FALSE;
	int sco_id_cnt = 0;
	int param27;
	int i;

	for (i = 0; i < 12; i++) {

		ioc_res = dev_wlc_intvar_get_reg(dev, "btc_params", 27, &param27);

		WL_TRACE(("%s, sample[%d], btc params: 27:%x\n",
			__FUNCTION__, i, param27));

		if (ioc_res < 0) {
			WL_ERR(("%s ioc read btc params error\n", __FUNCTION__));
			break;
		}

		if ((param27 & 0x6) == 2) { /* count both sco & esco  */
			sco_id_cnt++;
		}

		if (sco_id_cnt > 2) {
			WL_TRACE(("%s, sco/esco detected, pkt id_cnt:%d  samples:%d\n",
				__FUNCTION__, sco_id_cnt, i));
			res = TRUE;
			break;
		}

		msleep(5);
	}

	return res;
}

#if defined(BT_DHCP_eSCO_FIX)
/* Enhanced BT COEX settings for eSCO compatibility during DHCP window */
static int set_btc_esco_params(struct net_device *dev, bool trump_sco)
{
	static bool saved_status = FALSE;

	char buf_reg50va_dhcp_on[8] =
		{ 50, 00, 00, 00, 0x22, 0x80, 0x00, 0x00 };
	char buf_reg51va_dhcp_on[8] =
		{ 51, 00, 00, 00, 0x00, 0x00, 0x00, 0x00 };
	char buf_reg64va_dhcp_on[8] =
		{ 64, 00, 00, 00, 0x00, 0x00, 0x00, 0x00 };
	char buf_reg65va_dhcp_on[8] =
		{ 65, 00, 00, 00, 0x00, 0x00, 0x00, 0x00 };
	char buf_reg71va_dhcp_on[8] =
		{ 71, 00, 00, 00, 0x00, 0x00, 0x00, 0x00 };
	uint32 regaddr;
	static uint32 saved_reg50;
	static uint32 saved_reg51;
	static uint32 saved_reg64;
	static uint32 saved_reg65;
	static uint32 saved_reg71;

	if (trump_sco) {
		/* this should reduce eSCO agressive retransmit
		 * w/o breaking it
		 */

		/* 1st save current */
		WL_TRACE(("Do new SCO/eSCO coex algo {save &"
			  "override}\n"));
		if ((!dev_wlc_intvar_get_reg(dev, "btc_params", 50, &saved_reg50)) &&
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 51, &saved_reg51)) &&
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 64, &saved_reg64)) &&
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 65, &saved_reg65)) &&
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 71, &saved_reg71))) {
			saved_status = TRUE;
			WL_TRACE(("%s saved bt_params[50,51,64,65,71]:"
				  "0x%x 0x%x 0x%x 0x%x 0x%x\n",
				  __FUNCTION__, saved_reg50, saved_reg51,
				  saved_reg64, saved_reg65, saved_reg71));
		} else {
			WL_ERR((":%s: save btc_params failed\n",
				__FUNCTION__));
			saved_status = FALSE;
			return -1;
		}

		WL_TRACE(("override with [50,51,64,65,71]:"
			  "0x%x 0x%x 0x%x 0x%x 0x%x\n",
			  *(u32 *)(buf_reg50va_dhcp_on+4),
			  *(u32 *)(buf_reg51va_dhcp_on+4),
			  *(u32 *)(buf_reg64va_dhcp_on+4),
			  *(u32 *)(buf_reg65va_dhcp_on+4),
			  *(u32 *)(buf_reg71va_dhcp_on+4)));

		dev_wlc_bufvar_set(dev, "btc_params",
			(char *)&buf_reg50va_dhcp_on[0], 8);
		dev_wlc_bufvar_set(dev, "btc_params",
			(char *)&buf_reg51va_dhcp_on[0], 8);
		dev_wlc_bufvar_set(dev, "btc_params",
			(char *)&buf_reg64va_dhcp_on[0], 8);
		dev_wlc_bufvar_set(dev, "btc_params",
			(char *)&buf_reg65va_dhcp_on[0], 8);
		dev_wlc_bufvar_set(dev, "btc_params",
			(char *)&buf_reg71va_dhcp_on[0], 8);

		saved_status = TRUE;
	} else if (saved_status) {
		/* restore previously saved bt params */
		WL_TRACE(("Do new SCO/eSCO coex algo {save &"
			  "override}\n"));

		regaddr = 50;
		dev_wlc_intvar_set_reg(dev, "btc_params",
			(char *)&regaddr, (char *)&saved_reg50);
		regaddr = 51;
		dev_wlc_intvar_set_reg(dev, "btc_params",
			(char *)&regaddr, (char *)&saved_reg51);
		regaddr = 64;
		dev_wlc_intvar_set_reg(dev, "btc_params",
			(char *)&regaddr, (char *)&saved_reg64);
		regaddr = 65;
		dev_wlc_intvar_set_reg(dev, "btc_params",
			(char *)&regaddr, (char *)&saved_reg65);
		regaddr = 71;
		dev_wlc_intvar_set_reg(dev, "btc_params",
			(char *)&regaddr, (char *)&saved_reg71);

		WL_TRACE(("restore bt_params[50,51,64,65,71]:"
			"0x%x 0x%x 0x%x 0x%x 0x%x\n",
			saved_reg50, saved_reg51, saved_reg64,
			saved_reg65, saved_reg71));

		saved_status = FALSE;
	} else {
		WL_ERR((":%s att to restore not saved BTCOEX params\n",
			__FUNCTION__));
		return -1;
	}
	return 0;
}
#endif /* BT_DHCP_eSCO_FIX */

static void
wl_cfg80211_bt_setflag(struct net_device *dev, bool set)
{
#if defined(BT_DHCP_USE_FLAGS)
	char buf_flag7_dhcp_on[8] = { 7, 00, 00, 00, 0x1, 0x0, 0x00, 0x00 };
	char buf_flag7_default[8]   = { 7, 00, 00, 00, 0x0, 0x00, 0x00, 0x00};
#endif


#if defined(BT_DHCP_eSCO_FIX)
	/* set = 1, save & turn on  0 - off & restore prev settings */
	set_btc_esco_params(dev, set);
#endif

#if defined(BT_DHCP_USE_FLAGS)
	WL_TRACE(("WI-FI priority boost via bt flags, set:%d\n", set));
	if (set == TRUE)
		/* Forcing bt_flag7  */
		dev_wlc_bufvar_set(dev, "btc_flags",
			(char *)&buf_flag7_dhcp_on[0],
			sizeof(buf_flag7_dhcp_on));
	else
		/* Restoring default bt flag7 */
		dev_wlc_bufvar_set(dev, "btc_flags",
			(char *)&buf_flag7_default[0],
			sizeof(buf_flag7_default));
#endif
}

static void wl_cfg80211_bt_timerfunc(ulong data)
{
	struct btcoex_info *bt_local = (struct btcoex_info *)data;
	WL_TRACE(("%s\n", __FUNCTION__));
	bt_local->timer_on = 0;
	schedule_work(&bt_local->work);
}

static void wl_cfg80211_bt_handler(struct work_struct *work)
{
	struct btcoex_info *btcx_inf;

	btcx_inf = container_of(work, struct btcoex_info, work);

	if (btcx_inf->timer_on) {
		btcx_inf->timer_on = 0;
		del_timer_sync(&btcx_inf->timer);
	}

	switch (btcx_inf->bt_state) {
		case BT_DHCP_START:
			/* DHCP started
			 * provide OPPORTUNITY window to get DHCP address
			 */
			WL_TRACE(("%s bt_dhcp stm: started \n",
				__FUNCTION__));
			btcx_inf->bt_state = BT_DHCP_OPPR_WIN;
			mod_timer(&btcx_inf->timer,
				jiffies + BT_DHCP_OPPR_WIN_TIME*HZ/1000);
			btcx_inf->timer_on = 1;
			break;

		case BT_DHCP_OPPR_WIN:
			if (btcx_inf->dhcp_done) {
				WL_TRACE(("%s DHCP Done before T1 expiration\n",
					__FUNCTION__));
				goto btc_coex_idle;
			}

			/* DHCP is not over yet, start lowering BT priority
			 * enforce btc_params + flags if necessary
			 */
			WL_TRACE(("%s DHCP T1:%d expired\n", __FUNCTION__,
				BT_DHCP_OPPR_WIN_TIME));
			if (btcx_inf->dev)
				wl_cfg80211_bt_setflag(btcx_inf->dev, TRUE);
			btcx_inf->bt_state = BT_DHCP_FLAG_FORCE_TIMEOUT;
			mod_timer(&btcx_inf->timer,
				jiffies + BT_DHCP_FLAG_FORCE_TIME*HZ/1000);
			btcx_inf->timer_on = 1;
			break;

		case BT_DHCP_FLAG_FORCE_TIMEOUT:
			if (btcx_inf->dhcp_done) {
				WL_TRACE(("%s DHCP Done before T2 expiration\n",
					__FUNCTION__));
			} else {
				/* Noo dhcp during T1+T2, restore BT priority */
				WL_TRACE(("%s DHCP wait interval T2:%d"
					  "msec expired\n", __FUNCTION__,
					  BT_DHCP_FLAG_FORCE_TIME));
			}

			/* Restoring default bt priority */
			if (btcx_inf->dev)
				wl_cfg80211_bt_setflag(btcx_inf->dev, FALSE);
btc_coex_idle:
			btcx_inf->bt_state = BT_DHCP_IDLE;
			btcx_inf->timer_on = 0;
			break;

		default:
			WL_ERR(("%s error g_status=%d !!!\n", __FUNCTION__,
				btcx_inf->bt_state));
			if (btcx_inf->dev)
				wl_cfg80211_bt_setflag(btcx_inf->dev, FALSE);
			btcx_inf->bt_state = BT_DHCP_IDLE;
			btcx_inf->timer_on = 0;
			break;
	}

	net_os_wake_unlock(btcx_inf->dev);
}

int wl_cfg80211_btcoex_init(struct wl_priv *wl)
{
	struct btcoex_info *btco_inf = NULL;

	btco_inf = kmalloc(sizeof(struct btcoex_info), GFP_KERNEL);
	if (!btco_inf)
		return -ENOMEM;

	btco_inf->bt_state = BT_DHCP_IDLE;
	btco_inf->ts_dhcp_start = 0;
	btco_inf->ts_dhcp_ok = 0;
	/* Set up timer for BT  */
	btco_inf->timer_ms = 10;
	init_timer(&btco_inf->timer);
	btco_inf->timer.data = (ulong)btco_inf;
	btco_inf->timer.function = wl_cfg80211_bt_timerfunc;

	btco_inf->dev = wl->wdev->netdev;

	INIT_WORK(&btco_inf->work, wl_cfg80211_bt_handler);

	wl->btcoex_info = btco_inf;
	return 0;
}

void wl_cfg80211_btcoex_deinit(struct wl_priv *wl)
{
	if (!wl->btcoex_info)
		return;

	if (!wl->btcoex_info->timer_on) {
		wl->btcoex_info->timer_on = 0;
		del_timer_sync(&wl->btcoex_info->timer);
	}

	cancel_work_sync(&wl->btcoex_info->work);

	kfree(wl->btcoex_info);
	wl->btcoex_info = NULL;
}

int wl_cfg80211_set_btcoex_dhcp(struct net_device *dev, char *command)
{

	struct wl_priv *wl = wlcfg_drv_priv;
	char powermode_val = 0;
	char buf_reg66va_dhcp_on[8] = { 66, 00, 00, 00, 0x10, 0x27, 0x00, 0x00 };
	char buf_reg41va_dhcp_on[8] = { 41, 00, 00, 00, 0x33, 0x00, 0x00, 0x00 };
	char buf_reg68va_dhcp_on[8] = { 68, 00, 00, 00, 0x90, 0x01, 0x00, 0x00 };

	uint32 regaddr;
	static uint32 saved_reg66;
	static uint32 saved_reg41;
	static uint32 saved_reg68;
	static bool saved_status = FALSE;

#ifdef COEX_DHCP
	char buf_flag7_default[8] =   { 7, 00, 00, 00, 0x0, 0x00, 0x00, 0x00};
	struct btcoex_info *btco_inf = wl->btcoex_info;
#endif /* COEX_DHCP */

	/* Figure out powermode 1 or o command */
	strncpy((char *)&powermode_val, command + strlen("BTCOEXMODE") +1, 1);

	if (strnicmp((char *)&powermode_val, "1", strlen("1")) == 0) {

		WL_TRACE(("%s: DHCP session starts\n", __FUNCTION__));

		/* Retrieve and saved orig regs value */
		if ((saved_status == FALSE) &&
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 66,  &saved_reg66)) &&
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 41,  &saved_reg41)) &&
			(!dev_wlc_intvar_get_reg(dev, "btc_params", 68,  &saved_reg68)))   {
				saved_status = TRUE;
				WL_TRACE(("Saved 0x%x 0x%x 0x%x\n",
					saved_reg66, saved_reg41, saved_reg68));

				/* Disable PM mode during dhpc session */

				/* Disable PM mode during dhpc session */
#ifdef COEX_DHCP
				/* Start  BT timer only for SCO connection */
				if (btcoex_is_sco_active(dev)) {
					/* btc_params 66 */
					dev_wlc_bufvar_set(dev, "btc_params",
						(char *)&buf_reg66va_dhcp_on[0],
						sizeof(buf_reg66va_dhcp_on));
					/* btc_params 41 0x33 */
					dev_wlc_bufvar_set(dev, "btc_params",
						(char *)&buf_reg41va_dhcp_on[0],
						sizeof(buf_reg41va_dhcp_on));
					/* btc_params 68 0x190 */
					dev_wlc_bufvar_set(dev, "btc_params",
						(char *)&buf_reg68va_dhcp_on[0],
						sizeof(buf_reg68va_dhcp_on));
					saved_status = TRUE;

					btco_inf->bt_state = BT_DHCP_START;
					btco_inf->timer_on = 1;
					mod_timer(&btco_inf->timer, btco_inf->timer.expires);
					WL_TRACE(("%s enable BT DHCP Timer\n",
					__FUNCTION__));
				}
#endif /* COEX_DHCP */
		}
		else if (saved_status == TRUE) {
			WL_ERR(("%s was called w/o DHCP OFF. Continue\n", __FUNCTION__));
		}
	}
	else if (strnicmp((char *)&powermode_val, "2", strlen("2")) == 0) {


		/* Restoring PM mode */

#ifdef COEX_DHCP
		/* Stop any bt timer because DHCP session is done */
		WL_TRACE(("%s disable BT DHCP Timer\n", __FUNCTION__));
		if (btco_inf->timer_on) {
			btco_inf->timer_on = 0;
			del_timer_sync(&btco_inf->timer);

			if (btco_inf->bt_state != BT_DHCP_IDLE) {
			/* need to restore original btc flags & extra btc params */
				WL_TRACE(("%s bt->bt_state:%d\n",
					__FUNCTION__, btco_inf->bt_state));
				/* wake up btcoex thread to restore btlags+params  */
				schedule_work(&btco_inf->work);
			}
		}

		/* Restoring btc_flag paramter anyway */
		if (saved_status == TRUE)
			dev_wlc_bufvar_set(dev, "btc_flags",
				(char *)&buf_flag7_default[0], sizeof(buf_flag7_default));
#endif /* COEX_DHCP */

		/* Restore original values */
		if (saved_status == TRUE) {
			regaddr = 66;
			dev_wlc_intvar_set_reg(dev, "btc_params",
				(char *)&regaddr, (char *)&saved_reg66);
			regaddr = 41;
			dev_wlc_intvar_set_reg(dev, "btc_params",
				(char *)&regaddr, (char *)&saved_reg41);
			regaddr = 68;
			dev_wlc_intvar_set_reg(dev, "btc_params",
				(char *)&regaddr, (char *)&saved_reg68);

			WL_TRACE(("restore regs {66,41,68} <- 0x%x 0x%x 0x%x\n",
				saved_reg66, saved_reg41, saved_reg68));
		}
		saved_status = FALSE;

	}
	else {
		WL_ERR(("%s Unkwown yet power setting, ignored\n",
			__FUNCTION__));
	}

	snprintf(command, 3, "OK");

	return (strlen("OK"));
}
#endif 
