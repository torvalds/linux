// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2013 Broadcom Corporation
 */
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>

#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include <defs.h>
#include "core.h"
#include "debug.h"
#include "fwil.h"
#include "fwil_types.h"
#include "btcoex.h"
#include "p2p.h"
#include "cfg80211.h"

/* T1 start SCO/eSCO priority suppression */
#define BRCMF_BTCOEX_OPPR_WIN_TIME   msecs_to_jiffies(2000)

/* BT registers values during DHCP */
#define BRCMF_BT_DHCP_REG50 0x8022
#define BRCMF_BT_DHCP_REG51 0
#define BRCMF_BT_DHCP_REG64 0
#define BRCMF_BT_DHCP_REG65 0
#define BRCMF_BT_DHCP_REG71 0
#define BRCMF_BT_DHCP_REG66 0x2710
#define BRCMF_BT_DHCP_REG41 0x33
#define BRCMF_BT_DHCP_REG68 0x190

/* number of samples for SCO detection */
#define BRCMF_BT_SCO_SAMPLES 12

/**
* enum brcmf_btcoex_state - BT coex DHCP state machine states
* @BRCMF_BT_DHCP_IDLE: DCHP is idle
* @BRCMF_BT_DHCP_START: DHCP started, wait before
*	boosting wifi priority
* @BRCMF_BT_DHCP_OPPR_WIN: graceful DHCP opportunity ended,
*	boost wifi priority
* @BRCMF_BT_DHCP_FLAG_FORCE_TIMEOUT: wifi priority boost end,
*	restore defaults
*/
enum brcmf_btcoex_state {
	BRCMF_BT_DHCP_IDLE,
	BRCMF_BT_DHCP_START,
	BRCMF_BT_DHCP_OPPR_WIN,
	BRCMF_BT_DHCP_FLAG_FORCE_TIMEOUT
};

/**
 * struct brcmf_btcoex_info - BT coex related information
 * @vif: interface for which request was done.
 * @timer: timer for DHCP state machine
 * @timeout: configured timeout.
 * @timer_on:  DHCP timer active
 * @dhcp_done: DHCP finished before T1/T2 timer expiration
 * @bt_state: DHCP state machine state
 * @work: DHCP state machine work
 * @cfg: driver private data for cfg80211 interface
 * @reg66: saved value of btc_params 66
 * @reg41: saved value of btc_params 41
 * @reg68: saved value of btc_params 68
 * @saved_regs_part1: flag indicating regs 66,41,68
 *	have been saved
 * @reg50: saved value of btc_params 50
 * @reg51: saved value of btc_params 51
 * @reg64: saved value of btc_params 64
 * @reg65: saved value of btc_params 65
 * @reg71: saved value of btc_params 71
 * @saved_regs_part2: flag indicating regs 50,51,64,65,71
 *	have been saved
 */
struct brcmf_btcoex_info {
	struct brcmf_cfg80211_vif *vif;
	struct timer_list timer;
	u16 timeout;
	bool timer_on;
	bool dhcp_done;
	enum brcmf_btcoex_state bt_state;
	struct work_struct work;
	struct brcmf_cfg80211_info *cfg;
	u32 reg66;
	u32 reg41;
	u32 reg68;
	bool saved_regs_part1;
	u32 reg50;
	u32 reg51;
	u32 reg64;
	u32 reg65;
	u32 reg71;
	bool saved_regs_part2;
};

/**
 * brcmf_btcoex_params_write() - write btc_params firmware variable
 * @ifp: interface
 * @addr: btc_params register number
 * @data: data to write
 */
static s32 brcmf_btcoex_params_write(struct brcmf_if *ifp, u32 addr, u32 data)
{
	struct {
		__le32 addr;
		__le32 data;
	} reg_write;

	reg_write.addr = cpu_to_le32(addr);
	reg_write.data = cpu_to_le32(data);
	return brcmf_fil_iovar_data_set(ifp, "btc_params",
					&reg_write, sizeof(reg_write));
}

/**
 * brcmf_btcoex_params_read() - read btc_params firmware variable
 * @ifp: interface
 * @addr: btc_params register number
 * @data: read data
 */
static s32 brcmf_btcoex_params_read(struct brcmf_if *ifp, u32 addr, u32 *data)
{
	*data = addr;

	return brcmf_fil_iovar_int_get(ifp, "btc_params", data);
}

/**
 * brcmf_btcoex_boost_wifi() - control BT SCO/eSCO parameters
 * @btci: BT coex info
 * @trump_sco:
 *	true - set SCO/eSCO parameters for compatibility
 *		during DHCP window
 *	false - restore saved parameter values
 *
 * Enhanced BT COEX settings for eSCO compatibility during DHCP window
 */
static void brcmf_btcoex_boost_wifi(struct brcmf_btcoex_info *btci,
				    bool trump_sco)
{
	struct brcmf_if *ifp = brcmf_get_ifp(btci->cfg->pub, 0);

	if (trump_sco && !btci->saved_regs_part2) {
		/* this should reduce eSCO agressive
		 * retransmit w/o breaking it
		 */

		/* save current */
		brcmf_dbg(INFO, "new SCO/eSCO coex algo {save & override}\n");
		brcmf_btcoex_params_read(ifp, 50, &btci->reg50);
		brcmf_btcoex_params_read(ifp, 51, &btci->reg51);
		brcmf_btcoex_params_read(ifp, 64, &btci->reg64);
		brcmf_btcoex_params_read(ifp, 65, &btci->reg65);
		brcmf_btcoex_params_read(ifp, 71, &btci->reg71);

		btci->saved_regs_part2 = true;
		brcmf_dbg(INFO,
			  "saved bt_params[50,51,64,65,71]: 0x%x 0x%x 0x%x 0x%x 0x%x\n",
			  btci->reg50, btci->reg51, btci->reg64,
			  btci->reg65, btci->reg71);

		/* pacify the eSco   */
		brcmf_btcoex_params_write(ifp, 50, BRCMF_BT_DHCP_REG50);
		brcmf_btcoex_params_write(ifp, 51, BRCMF_BT_DHCP_REG51);
		brcmf_btcoex_params_write(ifp, 64, BRCMF_BT_DHCP_REG64);
		brcmf_btcoex_params_write(ifp, 65, BRCMF_BT_DHCP_REG65);
		brcmf_btcoex_params_write(ifp, 71, BRCMF_BT_DHCP_REG71);

	} else if (btci->saved_regs_part2) {
		/* restore previously saved bt params */
		brcmf_dbg(INFO, "Do new SCO/eSCO coex algo {restore}\n");
		brcmf_btcoex_params_write(ifp, 50, btci->reg50);
		brcmf_btcoex_params_write(ifp, 51, btci->reg51);
		brcmf_btcoex_params_write(ifp, 64, btci->reg64);
		brcmf_btcoex_params_write(ifp, 65, btci->reg65);
		brcmf_btcoex_params_write(ifp, 71, btci->reg71);

		brcmf_dbg(INFO,
			  "restored bt_params[50,51,64,65,71]: 0x%x 0x%x 0x%x 0x%x 0x%x\n",
			  btci->reg50, btci->reg51, btci->reg64,
			  btci->reg65, btci->reg71);

		btci->saved_regs_part2 = false;
	} else {
		brcmf_dbg(INFO, "attempted to restore not saved BTCOEX params\n");
	}
}

/**
 * brcmf_btcoex_is_sco_active() - check if SCO/eSCO is active
 * @ifp: interface
 *
 * return: true if SCO/eSCO session is active
 */
static bool brcmf_btcoex_is_sco_active(struct brcmf_if *ifp)
{
	int ioc_res = 0;
	bool res = false;
	int sco_id_cnt = 0;
	u32 param27;
	int i;

	for (i = 0; i < BRCMF_BT_SCO_SAMPLES; i++) {
		ioc_res = brcmf_btcoex_params_read(ifp, 27, &param27);

		if (ioc_res < 0) {
			brcmf_err("ioc read btc params error\n");
			break;
		}

		brcmf_dbg(INFO, "sample[%d], btc_params 27:%x\n", i, param27);

		if ((param27 & 0x6) == 2) { /* count both sco & esco  */
			sco_id_cnt++;
		}

		if (sco_id_cnt > 2) {
			brcmf_dbg(INFO,
				  "sco/esco detected, pkt id_cnt:%d samples:%d\n",
				  sco_id_cnt, i);
			res = true;
			break;
		}
	}
	brcmf_dbg(TRACE, "exit: result=%d\n", res);
	return res;
}

/*
 * btcmf_btcoex_save_part1() - save first step parameters.
 */
static void btcmf_btcoex_save_part1(struct brcmf_btcoex_info *btci)
{
	struct brcmf_if *ifp = btci->vif->ifp;

	if (!btci->saved_regs_part1) {
		/* Retrieve and save original reg value */
		brcmf_btcoex_params_read(ifp, 66, &btci->reg66);
		brcmf_btcoex_params_read(ifp, 41, &btci->reg41);
		brcmf_btcoex_params_read(ifp, 68, &btci->reg68);
		btci->saved_regs_part1 = true;
		brcmf_dbg(INFO,
			  "saved btc_params regs (66,41,68) 0x%x 0x%x 0x%x\n",
			  btci->reg66, btci->reg41,
			  btci->reg68);
	}
}

/*
 * brcmf_btcoex_restore_part1() - restore first step parameters.
 */
static void brcmf_btcoex_restore_part1(struct brcmf_btcoex_info *btci)
{
	struct brcmf_if *ifp;

	if (btci->saved_regs_part1) {
		btci->saved_regs_part1 = false;
		ifp = btci->vif->ifp;
		brcmf_btcoex_params_write(ifp, 66, btci->reg66);
		brcmf_btcoex_params_write(ifp, 41, btci->reg41);
		brcmf_btcoex_params_write(ifp, 68, btci->reg68);
		brcmf_dbg(INFO,
			  "restored btc_params regs {66,41,68} 0x%x 0x%x 0x%x\n",
			  btci->reg66, btci->reg41,
			  btci->reg68);
	}
}

/*
 * brcmf_btcoex_timerfunc() - BT coex timer callback
 */
static void brcmf_btcoex_timerfunc(struct timer_list *t)
{
	struct brcmf_btcoex_info *bt_local = from_timer(bt_local, t, timer);
	brcmf_dbg(TRACE, "enter\n");

	bt_local->timer_on = false;
	schedule_work(&bt_local->work);
}

/**
 * brcmf_btcoex_handler() - BT coex state machine work handler
 * @work: work
 */
static void brcmf_btcoex_handler(struct work_struct *work)
{
	struct brcmf_btcoex_info *btci;
	btci = container_of(work, struct brcmf_btcoex_info, work);
	if (btci->timer_on) {
		btci->timer_on = false;
		del_timer_sync(&btci->timer);
	}

	switch (btci->bt_state) {
	case BRCMF_BT_DHCP_START:
		/* DHCP started provide OPPORTUNITY window
		   to get DHCP address
		*/
		brcmf_dbg(INFO, "DHCP started\n");
		btci->bt_state = BRCMF_BT_DHCP_OPPR_WIN;
		if (btci->timeout < BRCMF_BTCOEX_OPPR_WIN_TIME) {
			mod_timer(&btci->timer, btci->timer.expires);
		} else {
			btci->timeout -= BRCMF_BTCOEX_OPPR_WIN_TIME;
			mod_timer(&btci->timer,
				  jiffies + BRCMF_BTCOEX_OPPR_WIN_TIME);
		}
		btci->timer_on = true;
		break;

	case BRCMF_BT_DHCP_OPPR_WIN:
		if (btci->dhcp_done) {
			brcmf_dbg(INFO, "DHCP done before T1 expiration\n");
			goto idle;
		}

		/* DHCP is not over yet, start lowering BT priority */
		brcmf_dbg(INFO, "DHCP T1:%d expired\n",
			  jiffies_to_msecs(BRCMF_BTCOEX_OPPR_WIN_TIME));
		brcmf_btcoex_boost_wifi(btci, true);

		btci->bt_state = BRCMF_BT_DHCP_FLAG_FORCE_TIMEOUT;
		mod_timer(&btci->timer, jiffies + btci->timeout);
		btci->timer_on = true;
		break;

	case BRCMF_BT_DHCP_FLAG_FORCE_TIMEOUT:
		if (btci->dhcp_done)
			brcmf_dbg(INFO, "DHCP done before T2 expiration\n");
		else
			brcmf_dbg(INFO, "DHCP T2:%d expired\n",
				  BRCMF_BT_DHCP_FLAG_FORCE_TIMEOUT);

		goto idle;

	default:
		brcmf_err("invalid state=%d !!!\n", btci->bt_state);
		goto idle;
	}

	return;

idle:
	btci->bt_state = BRCMF_BT_DHCP_IDLE;
	btci->timer_on = false;
	brcmf_btcoex_boost_wifi(btci, false);
	cfg80211_crit_proto_stopped(&btci->vif->wdev, GFP_KERNEL);
	brcmf_btcoex_restore_part1(btci);
	btci->vif = NULL;
}

/**
 * brcmf_btcoex_attach() - initialize BT coex data
 * @cfg: driver private cfg80211 data
 *
 * return: 0 on success
 */
int brcmf_btcoex_attach(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_btcoex_info *btci;
	brcmf_dbg(TRACE, "enter\n");

	btci = kmalloc(sizeof(*btci), GFP_KERNEL);
	if (!btci)
		return -ENOMEM;

	btci->bt_state = BRCMF_BT_DHCP_IDLE;

	/* Set up timer for BT  */
	btci->timer_on = false;
	btci->timeout = BRCMF_BTCOEX_OPPR_WIN_TIME;
	timer_setup(&btci->timer, brcmf_btcoex_timerfunc, 0);
	btci->cfg = cfg;
	btci->saved_regs_part1 = false;
	btci->saved_regs_part2 = false;

	INIT_WORK(&btci->work, brcmf_btcoex_handler);

	cfg->btcoex = btci;
	return 0;
}

/**
 * brcmf_btcoex_detach - clean BT coex data
 * @cfg: driver private cfg80211 data
 */
void brcmf_btcoex_detach(struct brcmf_cfg80211_info *cfg)
{
	brcmf_dbg(TRACE, "enter\n");

	if (!cfg->btcoex)
		return;

	if (cfg->btcoex->timer_on) {
		cfg->btcoex->timer_on = false;
		timer_shutdown_sync(&cfg->btcoex->timer);
	}

	cancel_work_sync(&cfg->btcoex->work);

	brcmf_btcoex_boost_wifi(cfg->btcoex, false);
	brcmf_btcoex_restore_part1(cfg->btcoex);

	kfree(cfg->btcoex);
	cfg->btcoex = NULL;
}

static void brcmf_btcoex_dhcp_start(struct brcmf_btcoex_info *btci)
{
	struct brcmf_if *ifp = btci->vif->ifp;

	btcmf_btcoex_save_part1(btci);
	/* set new regs values */
	brcmf_btcoex_params_write(ifp, 66, BRCMF_BT_DHCP_REG66);
	brcmf_btcoex_params_write(ifp, 41, BRCMF_BT_DHCP_REG41);
	brcmf_btcoex_params_write(ifp, 68, BRCMF_BT_DHCP_REG68);
	btci->dhcp_done = false;
	btci->bt_state = BRCMF_BT_DHCP_START;
	schedule_work(&btci->work);
	brcmf_dbg(TRACE, "enable BT DHCP Timer\n");
}

static void brcmf_btcoex_dhcp_end(struct brcmf_btcoex_info *btci)
{
	/* Stop any bt timer because DHCP session is done */
	btci->dhcp_done = true;
	if (btci->timer_on) {
		brcmf_dbg(INFO, "disable BT DHCP Timer\n");
		btci->timer_on = false;
		del_timer_sync(&btci->timer);

		/* schedule worker if transition to IDLE is needed */
		if (btci->bt_state != BRCMF_BT_DHCP_IDLE) {
			brcmf_dbg(INFO, "bt_state:%d\n",
				  btci->bt_state);
			schedule_work(&btci->work);
		}
	} else {
		/* Restore original values */
		brcmf_btcoex_restore_part1(btci);
	}
}

/*
 * brcmf_btcoex_set_mode - set BT coex mode
 * @mode: Wifi-Bluetooth coexistence mode
 *
 * return: 0 on success
 */
int brcmf_btcoex_set_mode(struct brcmf_cfg80211_vif *vif,
			  enum brcmf_btcoex_mode mode, u16 duration)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(vif->wdev.wiphy);
	struct brcmf_btcoex_info *btci = cfg->btcoex;
	struct brcmf_if *ifp = brcmf_get_ifp(cfg->pub, 0);

	switch (mode) {
	case BRCMF_BTCOEX_DISABLED:
		brcmf_dbg(INFO, "DHCP session starts\n");
		if (btci->bt_state != BRCMF_BT_DHCP_IDLE)
			return -EBUSY;
		/* Start BT timer only for SCO connection */
		if (brcmf_btcoex_is_sco_active(ifp)) {
			btci->timeout = msecs_to_jiffies(duration);
			btci->vif = vif;
			brcmf_btcoex_dhcp_start(btci);
		}
		break;

	case BRCMF_BTCOEX_ENABLED:
		brcmf_dbg(INFO, "DHCP session ends\n");
		if (btci->bt_state != BRCMF_BT_DHCP_IDLE &&
		    vif == btci->vif) {
			brcmf_btcoex_dhcp_end(btci);
		}
		break;
	default:
		brcmf_dbg(INFO, "Unknown mode, ignored\n");
	}
	return 0;
}
