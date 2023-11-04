// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include "rtl_core.h"
#include "rtl_dm.h"
#include "r8192E_hw.h"
#include "r8192E_phy.h"
#include "r8192E_phyreg.h"
#include "r8190P_rtl8256.h"
#include "r8192E_cmdpkt.h"

/*---------------------------Define Local Constant---------------------------*/
static u32 edca_setting_DL[HT_IOT_PEER_MAX] = {
	0x5e4322,
	0x5e4322,
	0x5ea44f,
	0x5e4322,
	0x604322,
	0xa44f,
	0x5e4322,
	0x5e4332
};

static u32 edca_setting_DL_GMode[HT_IOT_PEER_MAX] = {
	0x5e4322,
	0x5e4322,
	0x5e4322,
	0x5e4322,
	0x604322,
	0xa44f,
	0x5e4322,
	0x5e4322
};

static u32 edca_setting_UL[HT_IOT_PEER_MAX] = {
	0x5e4322,
	0xa44f,
	0x5ea44f,
	0x5e4322,
	0x604322,
	0x5e4322,
	0x5e4322,
	0x5e4332
};

const u32 dm_tx_bb_gain[TX_BB_GAIN_TABLE_LEN] = {
	0x7f8001fe, /* 12 dB */
	0x788001e2, /* 11 dB */
	0x71c001c7,
	0x6b8001ae,
	0x65400195,
	0x5fc0017f,
	0x5a400169,
	0x55400155,
	0x50800142,
	0x4c000130,
	0x47c0011f,
	0x43c0010f,
	0x40000100,
	0x3c8000f2,
	0x390000e4,
	0x35c000d7,
	0x32c000cb,
	0x300000c0,
	0x2d4000b5,
	0x2ac000ab,
	0x288000a2,
	0x26000098,
	0x24000090,
	0x22000088,
	0x20000080,
	0x1a00006c,
	0x1c800072,
	0x18000060,
	0x19800066,
	0x15800056,
	0x26c0005b,
	0x14400051,
	0x24400051,
	0x1300004c,
	0x12000048,
	0x11000044,
	0x10000040, /* -24 dB */
};

const u8 dm_cck_tx_bb_gain[CCK_TX_BB_GAIN_TABLE_LEN][8] = {
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04},
	{0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04},
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03},
	{0x2d, 0x2d, 0x27, 0x1f, 0x18, 0x0f, 0x08, 0x03},
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03},
	{0x28, 0x28, 0x22, 0x1c, 0x15, 0x0d, 0x07, 0x03},
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03},
	{0x24, 0x23, 0x1f, 0x19, 0x13, 0x0c, 0x06, 0x03},
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02},
	{0x20, 0x20, 0x1b, 0x16, 0x11, 0x08, 0x05, 0x02},
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02},
	{0x1d, 0x1c, 0x18, 0x14, 0x0f, 0x0a, 0x05, 0x02},
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02},
	{0x1a, 0x19, 0x16, 0x12, 0x0d, 0x09, 0x04, 0x02},
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02},
	{0x17, 0x16, 0x13, 0x10, 0x0c, 0x08, 0x04, 0x02},
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01},
	{0x14, 0x14, 0x11, 0x0e, 0x0b, 0x07, 0x03, 0x02},
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01},
	{0x12, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},
	{0x10, 0x10, 0x0e, 0x0b, 0x08, 0x05, 0x03, 0x01},
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01}
};

const u8 dm_cck_tx_bb_gain_ch14[CCK_TX_BB_GAIN_TABLE_LEN][8] = {
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00},
	{0x33, 0x32, 0x2b, 0x19, 0x00, 0x00, 0x00, 0x00},
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00},
	{0x2d, 0x2d, 0x27, 0x17, 0x00, 0x00, 0x00, 0x00},
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00},
	{0x28, 0x28, 0x22, 0x14, 0x00, 0x00, 0x00, 0x00},
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00},
	{0x24, 0x23, 0x1f, 0x12, 0x00, 0x00, 0x00, 0x00},
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00},
	{0x20, 0x20, 0x1b, 0x10, 0x00, 0x00, 0x00, 0x00},
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00},
	{0x1d, 0x1c, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00},
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00},
	{0x1a, 0x19, 0x16, 0x0d, 0x00, 0x00, 0x00, 0x00},
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00},
	{0x17, 0x16, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00},
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00},
	{0x14, 0x14, 0x11, 0x0a, 0x00, 0x00, 0x00, 0x00},
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00},
	{0x12, 0x12, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},
	{0x10, 0x10, 0x0e, 0x08, 0x00, 0x00, 0x00, 0x00},
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00}
};

/*---------------------------Define Local Constant---------------------------*/


/*------------------------Define global variable-----------------------------*/
struct dig_t dm_digtable;

struct drx_path_sel dm_rx_path_sel_table;
/*------------------------Define global variable-----------------------------*/


/*------------------------Define local variable------------------------------*/
/*------------------------Define local variable------------------------------*/



/*---------------------Define local function prototype-----------------------*/
static void _rtl92e_dm_check_rate_adaptive(struct net_device *dev);

static void _rtl92e_dm_init_bandwidth_autoswitch(struct net_device *dev);
static	void	_rtl92e_dm_bandwidth_autoswitch(struct net_device *dev);

static	void	_rtl92e_dm_check_tx_power_tracking(struct net_device *dev);

static void _rtl92e_dm_dig_init(struct net_device *dev);
static void _rtl92e_dm_ctrl_initgain_byrssi(struct net_device *dev);
static void _rtl92e_dm_ctrl_initgain_byrssi_driver(struct net_device *dev);
static void _rtl92e_dm_initial_gain(struct net_device *dev);
static void _rtl92e_dm_pd_th(struct net_device *dev);
static void _rtl92e_dm_cs_ratio(struct net_device *dev);

static	void _rtl92e_dm_init_cts_to_self(struct net_device *dev);

static void _rtl92e_dm_check_edca_turbo(struct net_device *dev);
static void _rtl92e_dm_check_rx_path_selection(struct net_device *dev);
static void _rtl92e_dm_init_rx_path_selection(struct net_device *dev);
static void _rtl92e_dm_rx_path_sel_byrssi(struct net_device *dev);

static void _rtl92e_dm_init_fsync(struct net_device *dev);
static void _rtl92e_dm_deinit_fsync(struct net_device *dev);

static	void _rtl92e_dm_check_txrateandretrycount(struct net_device *dev);
static void _rtl92e_dm_check_fsync(struct net_device *dev);
static void _rtl92e_dm_check_rf_ctrl_gpio(void *data);
static void _rtl92e_dm_fsync_timer_callback(struct timer_list *t);

/*---------------------Define local function prototype-----------------------*/

static	void	_rtl92e_dm_init_dynamic_tx_power(struct net_device *dev);
static void _rtl92e_dm_dynamic_tx_power(struct net_device *dev);

static void _rtl92e_dm_send_rssi_to_fw(struct net_device *dev);
static void _rtl92e_dm_cts_to_self(struct net_device *dev);
/*---------------------------Define function prototype------------------------*/

void rtl92e_dm_init(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->undecorated_smoothed_pwdb = -1;

	_rtl92e_dm_init_dynamic_tx_power(dev);

	rtl92e_init_adaptive_rate(dev);

	_rtl92e_dm_dig_init(dev);
	rtl92e_dm_init_edca_turbo(dev);
	_rtl92e_dm_init_bandwidth_autoswitch(dev);
	_rtl92e_dm_init_fsync(dev);
	_rtl92e_dm_init_rx_path_selection(dev);
	_rtl92e_dm_init_cts_to_self(dev);

	INIT_DELAYED_WORK(&priv->gpio_change_rf_wq, (void *)_rtl92e_dm_check_rf_ctrl_gpio);
}

void rtl92e_dm_deinit(struct net_device *dev)
{
	_rtl92e_dm_deinit_fsync(dev);
}

void rtl92e_dm_watchdog(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->being_init_adapter)
		return;

	_rtl92e_dm_check_txrateandretrycount(dev);
	_rtl92e_dm_check_edca_turbo(dev);

	_rtl92e_dm_check_rate_adaptive(dev);
	_rtl92e_dm_dynamic_tx_power(dev);
	_rtl92e_dm_check_tx_power_tracking(dev);

	_rtl92e_dm_ctrl_initgain_byrssi(dev);
	_rtl92e_dm_bandwidth_autoswitch(dev);

	_rtl92e_dm_check_rx_path_selection(dev);
	_rtl92e_dm_check_fsync(dev);

	_rtl92e_dm_send_rssi_to_fw(dev);
	_rtl92e_dm_cts_to_self(dev);
}

void rtl92e_init_adaptive_rate(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rate_adaptive *pra = &priv->rate_adaptive;

	pra->ratr_state = DM_RATR_STA_MAX;
	pra->high2low_rssi_thresh_for_ra = RATE_ADAPTIVE_TH_HIGH;
	pra->low2high_rssi_thresh_for_ra20M = RATE_ADAPTIVE_TH_LOW_20M + 5;
	pra->low2high_rssi_thresh_for_ra40M = RATE_ADAPTIVE_TH_LOW_40M + 5;

	pra->high_rssi_thresh_for_ra = RATE_ADAPTIVE_TH_HIGH + 5;
	pra->low_rssi_thresh_for_ra20M = RATE_ADAPTIVE_TH_LOW_20M;
	pra->low_rssi_thresh_for_ra40M = RATE_ADAPTIVE_TH_LOW_40M;

	if (priv->customer_id == RT_CID_819X_NETCORE)
		pra->ping_rssi_enable = 1;
	else
		pra->ping_rssi_enable = 0;
	pra->ping_rssi_thresh_for_ra = 15;

	pra->upper_rssi_threshold_ratr		=	0x000fc000;
	pra->middle_rssi_threshold_ratr		=	0x000ff000;
	pra->low_rssi_threshold_ratr		=	0x000ff001;
	pra->low_rssi_threshold_ratr_40M	=	0x000ff005;
	pra->low_rssi_threshold_ratr_20M	=	0x000ff001;
	pra->ping_rssi_ratr	=	0x0000000d;
}

static void _rtl92e_dm_check_rate_adaptive(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_hi_throughput *ht_info = priv->rtllib->ht_info;
	struct rate_adaptive *pra = &priv->rate_adaptive;
	u32 current_ratr, target_ratr = 0;
	u32 low_rssi_thresh_for_ra = 0, high_rssi_thresh_for_ra = 0;
	bool bshort_gi_enabled = false;
	static u8 ping_rssi_state;

	if (!priv->up)
		return;

	if (priv->rtllib->mode != WIRELESS_MODE_N_24G)
		return;

	if (priv->rtllib->link_state == MAC80211_LINKED) {
		bshort_gi_enabled = (ht_info->cur_tx_bw40mhz &&
				     ht_info->bCurShortGI40MHz) ||
				    (!ht_info->cur_tx_bw40mhz &&
				     ht_info->bCurShortGI20MHz);

		pra->upper_rssi_threshold_ratr =
				(pra->upper_rssi_threshold_ratr & (~BIT(31))) |
				((bshort_gi_enabled) ? BIT(31) : 0);

		pra->middle_rssi_threshold_ratr =
				(pra->middle_rssi_threshold_ratr & (~BIT(31))) |
				((bshort_gi_enabled) ? BIT(31) : 0);

		if (priv->current_chnl_bw != HT_CHANNEL_WIDTH_20) {
			pra->low_rssi_threshold_ratr =
				(pra->low_rssi_threshold_ratr_40M & (~BIT(31))) |
				((bshort_gi_enabled) ? BIT(31) : 0);
		} else {
			pra->low_rssi_threshold_ratr =
				(pra->low_rssi_threshold_ratr_20M & (~BIT(31))) |
				((bshort_gi_enabled) ? BIT(31) : 0);
		}
		pra->ping_rssi_ratr =
				(pra->ping_rssi_ratr & (~BIT(31))) |
				((bshort_gi_enabled) ? BIT(31) : 0);

		if (pra->ratr_state == DM_RATR_STA_HIGH) {
			high_rssi_thresh_for_ra = pra->high2low_rssi_thresh_for_ra;
			low_rssi_thresh_for_ra = (priv->current_chnl_bw != HT_CHANNEL_WIDTH_20) ?
					(pra->low_rssi_thresh_for_ra40M) : (pra->low_rssi_thresh_for_ra20M);
		} else if (pra->ratr_state == DM_RATR_STA_LOW) {
			high_rssi_thresh_for_ra = pra->high_rssi_thresh_for_ra;
			low_rssi_thresh_for_ra = (priv->current_chnl_bw != HT_CHANNEL_WIDTH_20) ?
					(pra->low2high_rssi_thresh_for_ra40M) : (pra->low2high_rssi_thresh_for_ra20M);
		} else {
			high_rssi_thresh_for_ra = pra->high_rssi_thresh_for_ra;
			low_rssi_thresh_for_ra = (priv->current_chnl_bw != HT_CHANNEL_WIDTH_20) ?
					(pra->low_rssi_thresh_for_ra40M) : (pra->low_rssi_thresh_for_ra20M);
		}

		if (priv->undecorated_smoothed_pwdb >=
		    (long)high_rssi_thresh_for_ra) {
			pra->ratr_state = DM_RATR_STA_HIGH;
			target_ratr = pra->upper_rssi_threshold_ratr;
		} else if (priv->undecorated_smoothed_pwdb >=
			   (long)low_rssi_thresh_for_ra) {
			pra->ratr_state = DM_RATR_STA_MIDDLE;
			target_ratr = pra->middle_rssi_threshold_ratr;
		} else {
			pra->ratr_state = DM_RATR_STA_LOW;
			target_ratr = pra->low_rssi_threshold_ratr;
		}

		if (pra->ping_rssi_enable) {
			if (priv->undecorated_smoothed_pwdb <
			    (long)(pra->ping_rssi_thresh_for_ra + 5)) {
				if ((priv->undecorated_smoothed_pwdb <
				     (long)pra->ping_rssi_thresh_for_ra) ||
				    ping_rssi_state) {
					pra->ratr_state = DM_RATR_STA_LOW;
					target_ratr = pra->ping_rssi_ratr;
					ping_rssi_state = 1;
				}
			} else {
				ping_rssi_state = 0;
			}
		}

		if (priv->rtllib->GetHalfNmodeSupportByAPsHandler(dev))
			target_ratr &=  0xf00fffff;

		current_ratr = rtl92e_readl(dev, RATR0);
		if (target_ratr !=  current_ratr) {
			u32 ratr_value;

			ratr_value = target_ratr;
			ratr_value &= ~(RATE_ALL_OFDM_2SS);
			rtl92e_writel(dev, RATR0, ratr_value);
			rtl92e_writeb(dev, UFWP, 1);
		}

	} else {
		pra->ratr_state = DM_RATR_STA_MAX;
	}
}

static void _rtl92e_dm_init_bandwidth_autoswitch(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->rtllib->bandwidth_auto_switch.threshold_20Mhzto40Mhz = BW_AUTO_SWITCH_LOW_HIGH;
	priv->rtllib->bandwidth_auto_switch.threshold_40Mhzto20Mhz = BW_AUTO_SWITCH_HIGH_LOW;
	priv->rtllib->bandwidth_auto_switch.bforced_tx20Mhz = false;
	priv->rtllib->bandwidth_auto_switch.bautoswitch_enable = false;
}

static void _rtl92e_dm_bandwidth_autoswitch(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->current_chnl_bw == HT_CHANNEL_WIDTH_20 ||
	    !priv->rtllib->bandwidth_auto_switch.bautoswitch_enable)
		return;
	if (!priv->rtllib->bandwidth_auto_switch.bforced_tx20Mhz) {
		if (priv->undecorated_smoothed_pwdb <=
		    priv->rtllib->bandwidth_auto_switch.threshold_40Mhzto20Mhz)
			priv->rtllib->bandwidth_auto_switch.bforced_tx20Mhz = true;
	} else {
		if (priv->undecorated_smoothed_pwdb >=
		    priv->rtllib->bandwidth_auto_switch.threshold_20Mhzto40Mhz)
			priv->rtllib->bandwidth_auto_switch.bforced_tx20Mhz = false;
	}
}

static u32 OFDMSwingTable[OFDM_TABLE_LEN] = {
	0x7f8001fe,
	0x71c001c7,
	0x65400195,
	0x5a400169,
	0x50800142,
	0x47c0011f,
	0x40000100,
	0x390000e4,
	0x32c000cb,
	0x2d4000b5,
	0x288000a2,
	0x24000090,
	0x20000080,
	0x1c800072,
	0x19800066,
	0x26c0005b,
	0x24400051,
	0x12000048,
	0x10000040
};

static u8	CCKSwingTable_Ch1_Ch13[CCK_TABLE_LEN][8] = {
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04},
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03},
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03},
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03},
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02},
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02},
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02},
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02},
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01},
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01},
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01}
};

static u8	CCKSwingTable_Ch14[CCK_TABLE_LEN][8] = {
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00},
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00},
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00},
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00},
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00},
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00},
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00},
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00},
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00},
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00},
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00}
};

#define		Pw_Track_Flag				0x11d
#define		Tssi_Mea_Value				0x13c
#define		Tssi_Report_Value1			0x134
#define		Tssi_Report_Value2			0x13e
#define		FW_Busy_Flag				0x13f

static void _rtl92e_dm_tx_update_tssi_weak_signal(struct net_device *dev)
{
	struct r8192_priv *p = rtllib_priv(dev);

	if (p->rfa_txpowertrackingindex > 0) {
		p->rfa_txpowertrackingindex--;
		if (p->rfa_txpowertrackingindex_real > 4) {
			p->rfa_txpowertrackingindex_real--;
			rtl92e_set_bb_reg(dev,
					  rOFDM0_XATxIQImbalance,
					  bMaskDWord,
					  dm_tx_bb_gain[p->rfa_txpowertrackingindex_real]);
		}
	} else {
		rtl92e_set_bb_reg(dev, rOFDM0_XATxIQImbalance,
				  bMaskDWord, dm_tx_bb_gain[4]);
	}
}

static void _rtl92e_dm_tx_update_tssi_strong_signal(struct net_device *dev)
{
	struct r8192_priv *p = rtllib_priv(dev);

	if (p->rfa_txpowertrackingindex < (TX_BB_GAIN_TABLE_LEN - 1)) {
		p->rfa_txpowertrackingindex++;
		p->rfa_txpowertrackingindex_real++;
		rtl92e_set_bb_reg(dev, rOFDM0_XATxIQImbalance,
				  bMaskDWord,
				  dm_tx_bb_gain[p->rfa_txpowertrackingindex_real]);
	} else {
		rtl92e_set_bb_reg(dev, rOFDM0_XATxIQImbalance,
				  bMaskDWord,
				  dm_tx_bb_gain[TX_BB_GAIN_TABLE_LEN - 1]);
	}
}

static void _rtl92e_dm_tx_power_tracking_callback_tssi(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	bool	viviflag = false;
	struct dcmd_txcmd tx_cmd;
	int	i = 0, j = 0, k = 0;
	u8	tmp_report[5] = {0, 0, 0, 0, 0};
	u8	Pwr_Flag;
	u16	Avg_TSSI_Meas, tssi_13dBm, Avg_TSSI_Meas_from_driver = 0;
	u32	delta = 0;

	rtl92e_writeb(dev, Pw_Track_Flag, 0);
	rtl92e_writeb(dev, FW_Busy_Flag, 0);
	priv->rtllib->bdynamic_txpower_enable = false;

	for (j = 0; j <= 30; j++) {
		tx_cmd.op	= TXCMD_SET_TX_PWR_TRACKING;
		tx_cmd.length	= 4;
		tx_cmd.value	= priv->pwr_track >> 24;
		rtl92e_send_cmd_pkt(dev, DESC_PACKET_TYPE_NORMAL, (u8 *)&tx_cmd,
				    sizeof(struct dcmd_txcmd));
		mdelay(1);
		for (i = 0; i <= 30; i++) {
			Pwr_Flag = rtl92e_readb(dev, Pw_Track_Flag);

			if (Pwr_Flag == 0) {
				mdelay(1);

				if (priv->rtllib->rf_power_state != rf_on) {
					rtl92e_writeb(dev, Pw_Track_Flag, 0);
					rtl92e_writeb(dev, FW_Busy_Flag, 0);
					return;
				}

				continue;
			}

			Avg_TSSI_Meas = rtl92e_readw(dev, Tssi_Mea_Value);

			if (Avg_TSSI_Meas == 0) {
				rtl92e_writeb(dev, Pw_Track_Flag, 0);
				rtl92e_writeb(dev, FW_Busy_Flag, 0);
				return;
			}

			for (k = 0; k < 5; k++) {
				if (k != 4)
					tmp_report[k] = rtl92e_readb(dev,
							 Tssi_Report_Value1 + k);
				else
					tmp_report[k] = rtl92e_readb(dev,
							 Tssi_Report_Value2);

				if (tmp_report[k] <= 20) {
					viviflag = true;
					break;
				}
			}

			if (viviflag) {
				rtl92e_writeb(dev, Pw_Track_Flag, 0);
				viviflag = false;
				for (k = 0; k < 5; k++)
					tmp_report[k] = 0;
				break;
			}

			for (k = 0; k < 5; k++)
				Avg_TSSI_Meas_from_driver += tmp_report[k];

			Avg_TSSI_Meas_from_driver *= 100 / 5;
			tssi_13dBm = priv->tssi_13dBm;

			if (Avg_TSSI_Meas_from_driver > tssi_13dBm)
				delta = Avg_TSSI_Meas_from_driver - tssi_13dBm;
			else
				delta = tssi_13dBm - Avg_TSSI_Meas_from_driver;

			if (delta <= E_FOR_TX_POWER_TRACK) {
				priv->rtllib->bdynamic_txpower_enable = true;
				rtl92e_writeb(dev, Pw_Track_Flag, 0);
				rtl92e_writeb(dev, FW_Busy_Flag, 0);
				return;
			}
			if (Avg_TSSI_Meas_from_driver < tssi_13dBm - E_FOR_TX_POWER_TRACK)
				_rtl92e_dm_tx_update_tssi_weak_signal(dev);
			else
				_rtl92e_dm_tx_update_tssi_strong_signal(dev);

			priv->cck_present_attn_diff
				= priv->rfa_txpowertrackingindex_real - priv->rfa_txpowertracking_default;

			if (priv->current_chnl_bw == HT_CHANNEL_WIDTH_20)
				priv->cck_present_attn =
					 priv->cck_present_attn_20m_def +
					 priv->cck_present_attn_diff;
			else
				priv->cck_present_attn =
					 priv->cck_present_attn_40m_def +
					 priv->cck_present_attn_diff;

			if (priv->cck_present_attn > (CCK_TX_BB_GAIN_TABLE_LEN - 1))
				priv->cck_present_attn = CCK_TX_BB_GAIN_TABLE_LEN - 1;
			if (priv->cck_present_attn < 0)
				priv->cck_present_attn = 0;

			if (priv->cck_present_attn > -1 &&
			    priv->cck_present_attn < CCK_TX_BB_GAIN_TABLE_LEN) {
				if (priv->rtllib->current_network.channel == 14 &&
				    !priv->bcck_in_ch14) {
					priv->bcck_in_ch14 = true;
					rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
				} else if (priv->rtllib->current_network.channel != 14 && priv->bcck_in_ch14) {
					priv->bcck_in_ch14 = false;
					rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
				} else {
					rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
				}
			}

			if (priv->cck_present_attn_diff <= -12 ||
			    priv->cck_present_attn_diff >= 24) {
				priv->rtllib->bdynamic_txpower_enable = true;
				rtl92e_writeb(dev, Pw_Track_Flag, 0);
				rtl92e_writeb(dev, FW_Busy_Flag, 0);
				return;
			}

			rtl92e_writeb(dev, Pw_Track_Flag, 0);
			Avg_TSSI_Meas_from_driver = 0;
			for (k = 0; k < 5; k++)
				tmp_report[k] = 0;
			break;
		}
		rtl92e_writeb(dev, FW_Busy_Flag, 0);
	}
	priv->rtllib->bdynamic_txpower_enable = true;
	rtl92e_writeb(dev, Pw_Track_Flag, 0);
}

static void _rtl92e_dm_tx_power_tracking_cb_thermal(struct net_device *dev)
{
#define ThermalMeterVal	9
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 tmp_reg, tmp_cck;
	u8 tmp_ofdm_index, tmp_cck_index, tmp_cck_20m_index, tmp_cck_40m_index, tmpval;
	int i = 0, CCKSwingNeedUpdate = 0;

	if (!priv->tx_pwr_tracking_init) {
		tmp_reg = rtl92e_get_bb_reg(dev, rOFDM0_XATxIQImbalance,
					    bMaskDWord);
		for (i = 0; i < OFDM_TABLE_LEN; i++) {
			if (tmp_reg == OFDMSwingTable[i])
				priv->ofdm_index[0] = i;
		}

		tmp_cck = rtl92e_get_bb_reg(dev, rCCK0_TxFilter1, bMaskByte2);
		for (i = 0; i < CCK_TABLE_LEN; i++) {
			if (tmp_cck == (u32)CCKSwingTable_Ch1_Ch13[i][0]) {
				priv->cck_index = i;
				break;
			}
		}
		priv->tx_pwr_tracking_init = true;
		return;
	}

	tmp_reg = rtl92e_get_rf_reg(dev, RF90_PATH_A, 0x12, 0x078);
	if (tmp_reg < 3 || tmp_reg > 13)
		return;
	if (tmp_reg >= 12)
		tmp_reg = 12;
	priv->thermal_meter[0] = ThermalMeterVal;
	priv->thermal_meter[1] = ThermalMeterVal;

	if (priv->thermal_meter[0] >= (u8)tmp_reg) {
		tmp_ofdm_index = 6 + (priv->thermal_meter[0] - (u8)tmp_reg);
		tmp_cck_20m_index = tmp_ofdm_index;
		tmp_cck_40m_index = tmp_cck_20m_index - 6;
		if (tmp_ofdm_index >= OFDM_TABLE_LEN)
			tmp_ofdm_index = OFDM_TABLE_LEN - 1;
		if (tmp_cck_20m_index >= CCK_TABLE_LEN)
			tmp_cck_20m_index = CCK_TABLE_LEN - 1;
		if (tmp_cck_40m_index >= CCK_TABLE_LEN)
			tmp_cck_40m_index = CCK_TABLE_LEN - 1;
	} else {
		tmpval = (u8)tmp_reg - priv->thermal_meter[0];
		if (tmpval >= 6) {
			tmp_ofdm_index = 0;
			tmp_cck_20m_index = 0;
		} else {
			tmp_ofdm_index = 6 - tmpval;
			tmp_cck_20m_index = 6 - tmpval;
		}
		tmp_cck_40m_index = 0;
	}
	if (priv->current_chnl_bw != HT_CHANNEL_WIDTH_20)
		tmp_cck_index = tmp_cck_40m_index;
	else
		tmp_cck_index = tmp_cck_20m_index;

	priv->rec_cck_20m_idx = tmp_cck_20m_index;
	priv->rec_cck_40m_idx = tmp_cck_40m_index;

	if (priv->rtllib->current_network.channel == 14 &&
	    !priv->bcck_in_ch14) {
		priv->bcck_in_ch14 = true;
		CCKSwingNeedUpdate = 1;
	} else if (priv->rtllib->current_network.channel != 14 &&
		   priv->bcck_in_ch14) {
		priv->bcck_in_ch14 = false;
		CCKSwingNeedUpdate = 1;
	}

	if (priv->cck_index != tmp_cck_index) {
		priv->cck_index = tmp_cck_index;
		CCKSwingNeedUpdate = 1;
	}

	if (CCKSwingNeedUpdate)
		rtl92e_dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
	if (priv->ofdm_index[0] != tmp_ofdm_index) {
		priv->ofdm_index[0] = tmp_ofdm_index;
		rtl92e_set_bb_reg(dev, rOFDM0_XATxIQImbalance, bMaskDWord,
				  OFDMSwingTable[priv->ofdm_index[0]]);
	}
	priv->txpower_count = 0;
}

void rtl92e_dm_txpower_tracking_wq(void *data)
{
	struct r8192_priv *priv = container_of_dwork_rsl(data,
				  struct r8192_priv, txpower_tracking_wq);
	struct net_device *dev = priv->rtllib->dev;

	if (priv->ic_cut >= IC_VersionCut_D)
		_rtl92e_dm_tx_power_tracking_callback_tssi(dev);
	else
		_rtl92e_dm_tx_power_tracking_cb_thermal(dev);
}

static void _rtl92e_dm_initialize_tx_power_tracking_tssi(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->btxpower_tracking = true;
	priv->txpower_count       = 0;
	priv->tx_pwr_tracking_init = false;
}

static void _rtl92e_dm_init_tx_power_tracking_thermal(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->rtllib->FwRWRF)
		priv->btxpower_tracking = true;
	else
		priv->btxpower_tracking = false;
	priv->txpower_count       = 0;
	priv->tx_pwr_tracking_init = false;
}

void rtl92e_dm_init_txpower_tracking(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->ic_cut >= IC_VersionCut_D)
		_rtl92e_dm_initialize_tx_power_tracking_tssi(dev);
	else
		_rtl92e_dm_init_tx_power_tracking_thermal(dev);
}

static void _rtl92e_dm_check_tx_power_tracking_tssi(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	static u32 tx_power_track_counter;

	if (rtl92e_readb(dev, 0x11e) == 1)
		return;
	if (!priv->btxpower_tracking)
		return;
	tx_power_track_counter++;

	if (tx_power_track_counter >= 180) {
		schedule_delayed_work(&priv->txpower_tracking_wq, 0);
		tx_power_track_counter = 0;
	}
}

static void _rtl92e_dm_check_tx_power_tracking_thermal(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	static u8	TM_Trigger;
	u8		TxPowerCheckCnt = 0;

	TxPowerCheckCnt = 2;
	if (!priv->btxpower_tracking)
		return;

	if (priv->txpower_count  <= TxPowerCheckCnt) {
		priv->txpower_count++;
		return;
	}

	if (!TM_Trigger) {
		rtl92e_set_rf_reg(dev, RF90_PATH_A, 0x02, bMask12Bits, 0x4d);
		rtl92e_set_rf_reg(dev, RF90_PATH_A, 0x02, bMask12Bits, 0x4f);
		rtl92e_set_rf_reg(dev, RF90_PATH_A, 0x02, bMask12Bits, 0x4d);
		rtl92e_set_rf_reg(dev, RF90_PATH_A, 0x02, bMask12Bits, 0x4f);
		TM_Trigger = 1;
		return;
	}
	netdev_info(dev, "===============>Schedule TxPowerTrackingWorkItem\n");
	schedule_delayed_work(&priv->txpower_tracking_wq, 0);
	TM_Trigger = 0;
}

static void _rtl92e_dm_check_tx_power_tracking(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->ic_cut >= IC_VersionCut_D)
		_rtl92e_dm_check_tx_power_tracking_tssi(dev);
	else
		_rtl92e_dm_check_tx_power_tracking_thermal(dev);
}

static void _rtl92e_dm_cck_tx_power_adjust_tssi(struct net_device *dev,
						bool bInCH14)
{
	u32 TempVal;
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 attenuation = priv->cck_present_attn;

	TempVal = 0;
	if (!bInCH14) {
		TempVal = (u32)(dm_cck_tx_bb_gain[attenuation][0] +
			  (dm_cck_tx_bb_gain[attenuation][1] << 8));

		rtl92e_set_bb_reg(dev, rCCK0_TxFilter1, bMaskHWord, TempVal);
		TempVal = (u32)((dm_cck_tx_bb_gain[attenuation][2]) +
			  (dm_cck_tx_bb_gain[attenuation][3] << 8) +
			  (dm_cck_tx_bb_gain[attenuation][4] << 16) +
			  (dm_cck_tx_bb_gain[attenuation][5] << 24));
		rtl92e_set_bb_reg(dev, rCCK0_TxFilter2, bMaskDWord, TempVal);
		TempVal = (u32)(dm_cck_tx_bb_gain[attenuation][6] +
			  (dm_cck_tx_bb_gain[attenuation][7] << 8));

		rtl92e_set_bb_reg(dev, rCCK0_DebugPort, bMaskLWord, TempVal);
	} else {
		TempVal = (u32)((dm_cck_tx_bb_gain_ch14[attenuation][0]) +
			  (dm_cck_tx_bb_gain_ch14[attenuation][1] << 8));

		rtl92e_set_bb_reg(dev, rCCK0_TxFilter1, bMaskHWord, TempVal);
		TempVal = (u32)((dm_cck_tx_bb_gain_ch14[attenuation][2]) +
			  (dm_cck_tx_bb_gain_ch14[attenuation][3] << 8) +
			  (dm_cck_tx_bb_gain_ch14[attenuation][4] << 16) +
			  (dm_cck_tx_bb_gain_ch14[attenuation][5] << 24));
		rtl92e_set_bb_reg(dev, rCCK0_TxFilter2, bMaskDWord, TempVal);
		TempVal = (u32)((dm_cck_tx_bb_gain_ch14[attenuation][6]) +
			  (dm_cck_tx_bb_gain_ch14[attenuation][7] << 8));

		rtl92e_set_bb_reg(dev, rCCK0_DebugPort, bMaskLWord, TempVal);
	}
}

static void _rtl92e_dm_cck_tx_power_adjust_thermal_meter(struct net_device *dev,
							 bool bInCH14)
{
	u32 TempVal;
	struct r8192_priv *priv = rtllib_priv(dev);

	TempVal = 0;
	if (!bInCH14) {
		TempVal = CCKSwingTable_Ch1_Ch13[priv->cck_index][0] +
			  (CCKSwingTable_Ch1_Ch13[priv->cck_index][1] << 8);
		rtl92e_set_bb_reg(dev, rCCK0_TxFilter1, bMaskHWord, TempVal);
		TempVal = CCKSwingTable_Ch1_Ch13[priv->cck_index][2] +
			  (CCKSwingTable_Ch1_Ch13[priv->cck_index][3] << 8) +
			  (CCKSwingTable_Ch1_Ch13[priv->cck_index][4] << 16) +
			  (CCKSwingTable_Ch1_Ch13[priv->cck_index][5] << 24);
		rtl92e_set_bb_reg(dev, rCCK0_TxFilter2, bMaskDWord, TempVal);
		TempVal = CCKSwingTable_Ch1_Ch13[priv->cck_index][6] +
			  (CCKSwingTable_Ch1_Ch13[priv->cck_index][7] << 8);

		rtl92e_set_bb_reg(dev, rCCK0_DebugPort, bMaskLWord, TempVal);
	} else {
		TempVal = CCKSwingTable_Ch14[priv->cck_index][0] +
			  (CCKSwingTable_Ch14[priv->cck_index][1] << 8);

		rtl92e_set_bb_reg(dev, rCCK0_TxFilter1, bMaskHWord, TempVal);
		TempVal = CCKSwingTable_Ch14[priv->cck_index][2] +
			  (CCKSwingTable_Ch14[priv->cck_index][3] << 8) +
			  (CCKSwingTable_Ch14[priv->cck_index][4] << 16) +
			  (CCKSwingTable_Ch14[priv->cck_index][5] << 24);
		rtl92e_set_bb_reg(dev, rCCK0_TxFilter2, bMaskDWord, TempVal);
		TempVal = CCKSwingTable_Ch14[priv->cck_index][6] +
			  (CCKSwingTable_Ch14[priv->cck_index][7] << 8);

		rtl92e_set_bb_reg(dev, rCCK0_DebugPort, bMaskLWord, TempVal);
	}
}

void rtl92e_dm_cck_txpower_adjust(struct net_device *dev, bool binch14)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->ic_cut >= IC_VersionCut_D)
		_rtl92e_dm_cck_tx_power_adjust_tssi(dev, binch14);
	else
		_rtl92e_dm_cck_tx_power_adjust_thermal_meter(dev, binch14);
}

static void _rtl92e_dm_dig_init(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	dm_digtable.cur_sta_connect_state = DIG_STA_DISCONNECT;
	dm_digtable.pre_sta_connect_state = DIG_STA_DISCONNECT;

	dm_digtable.rssi_low_thresh	= DM_DIG_THRESH_LOW;
	dm_digtable.rssi_high_thresh	= DM_DIG_THRESH_HIGH;

	dm_digtable.rssi_high_power_lowthresh = DM_DIG_HIGH_PWR_THRESH_LOW;
	dm_digtable.rssi_high_power_highthresh = DM_DIG_HIGH_PWR_THRESH_HIGH;

	dm_digtable.rssi_val = 50;
	dm_digtable.backoff_val = DM_DIG_BACKOFF;
	dm_digtable.rx_gain_range_max = DM_DIG_MAX;
	if (priv->customer_id == RT_CID_819X_NETCORE)
		dm_digtable.rx_gain_range_min = DM_DIG_MIN_Netcore;
	else
		dm_digtable.rx_gain_range_min = DM_DIG_MIN;
}

static void _rtl92e_dm_ctrl_initgain_byrssi(struct net_device *dev)
{
	_rtl92e_dm_ctrl_initgain_byrssi_driver(dev);
}

/*-----------------------------------------------------------------------------
 * Function:	dm_CtrlInitGainBeforeConnectByRssiAndFalseAlarm()
 *
 * Overview:	Driver monitor RSSI and False Alarm to change initial gain.
			Only change initial gain during link in progress.
 *
 * Input:		IN	PADAPTER	pAdapter
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	03/04/2009	hpfan	Create Version 0.
 *
 ******************************************************************************/

static void _rtl92e_dm_ctrl_initgain_byrssi_driver(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 i;
	static u8	fw_dig;

	if (fw_dig <= 3) {
		for (i = 0; i < 3; i++)
			rtl92e_set_bb_reg(dev, UFWP, bMaskByte1, 0x8);
		fw_dig++;
	}

	if (priv->rtllib->link_state == MAC80211_LINKED)
		dm_digtable.cur_sta_connect_state = DIG_STA_CONNECT;
	else
		dm_digtable.cur_sta_connect_state = DIG_STA_DISCONNECT;

	dm_digtable.rssi_val = priv->undecorated_smoothed_pwdb;
	_rtl92e_dm_initial_gain(dev);
	_rtl92e_dm_pd_th(dev);
	_rtl92e_dm_cs_ratio(dev);
	dm_digtable.pre_sta_connect_state = dm_digtable.cur_sta_connect_state;
}

static void _rtl92e_dm_initial_gain(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 initial_gain = 0;
	static u8 initialized, force_write;

	if (rtllib_act_scanning(priv->rtllib, true)) {
		force_write = 1;
		return;
	}

	if (dm_digtable.pre_sta_connect_state == dm_digtable.cur_sta_connect_state) {
		if (dm_digtable.cur_sta_connect_state == DIG_STA_CONNECT) {
			long gain_range = dm_digtable.rssi_val + 10 -
					  dm_digtable.backoff_val;
			gain_range = clamp_t(long, gain_range,
					     dm_digtable.rx_gain_range_min,
					     dm_digtable.rx_gain_range_max);
			dm_digtable.cur_ig_value = gain_range;
		} else {
			if (dm_digtable.cur_ig_value == 0)
				dm_digtable.cur_ig_value = priv->def_initial_gain[0];
			else
				dm_digtable.cur_ig_value = dm_digtable.pre_ig_value;
		}
	} else {
		dm_digtable.cur_ig_value = priv->def_initial_gain[0];
		dm_digtable.pre_ig_value = 0;
	}

	if (dm_digtable.pre_ig_value != rtl92e_readb(dev, rOFDM0_XAAGCCore1))
		force_write = 1;

	if ((dm_digtable.pre_ig_value != dm_digtable.cur_ig_value)
	    || !initialized || force_write) {
		initial_gain = dm_digtable.cur_ig_value;
		rtl92e_writeb(dev, rOFDM0_XAAGCCore1, initial_gain);
		rtl92e_writeb(dev, rOFDM0_XBAGCCore1, initial_gain);
		rtl92e_writeb(dev, rOFDM0_XCAGCCore1, initial_gain);
		rtl92e_writeb(dev, rOFDM0_XDAGCCore1, initial_gain);
		dm_digtable.pre_ig_value = dm_digtable.cur_ig_value;
		initialized = 1;
		force_write = 0;
	}
}

static void _rtl92e_dm_pd_th(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	static u8 initialized, force_write;

	if (dm_digtable.pre_sta_connect_state == dm_digtable.cur_sta_connect_state) {
		if (dm_digtable.cur_sta_connect_state == DIG_STA_CONNECT) {
			if (dm_digtable.rssi_val >=
			    dm_digtable.rssi_high_power_highthresh)
				dm_digtable.curpd_thstate =
							DIG_PD_AT_HIGH_POWER;
			else if (dm_digtable.rssi_val <=
				 dm_digtable.rssi_low_thresh)
				dm_digtable.curpd_thstate =
							DIG_PD_AT_LOW_POWER;
			else if ((dm_digtable.rssi_val >=
				  dm_digtable.rssi_high_thresh) &&
				 (dm_digtable.rssi_val <
				  dm_digtable.rssi_high_power_lowthresh))
				dm_digtable.curpd_thstate =
							DIG_PD_AT_NORMAL_POWER;
			else
				dm_digtable.curpd_thstate =
						dm_digtable.prepd_thstate;
		} else {
			dm_digtable.curpd_thstate = DIG_PD_AT_LOW_POWER;
		}
	} else {
		dm_digtable.curpd_thstate = DIG_PD_AT_LOW_POWER;
	}

	if ((dm_digtable.prepd_thstate != dm_digtable.curpd_thstate) ||
	    (initialized <= 3) || force_write) {
		if (dm_digtable.curpd_thstate == DIG_PD_AT_LOW_POWER) {
			if (priv->current_chnl_bw != HT_CHANNEL_WIDTH_20)
				rtl92e_writeb(dev, (rOFDM0_XATxAFE + 3), 0x00);
			else
				rtl92e_writeb(dev, rOFDM0_RxDetector1, 0x42);
		} else if (dm_digtable.curpd_thstate ==
			   DIG_PD_AT_NORMAL_POWER) {
			if (priv->current_chnl_bw != HT_CHANNEL_WIDTH_20)
				rtl92e_writeb(dev, (rOFDM0_XATxAFE + 3), 0x20);
			else
				rtl92e_writeb(dev, rOFDM0_RxDetector1, 0x44);
		} else if (dm_digtable.curpd_thstate == DIG_PD_AT_HIGH_POWER) {
			if (priv->current_chnl_bw != HT_CHANNEL_WIDTH_20)
				rtl92e_writeb(dev, (rOFDM0_XATxAFE + 3), 0x10);
			else
				rtl92e_writeb(dev, rOFDM0_RxDetector1, 0x43);
		}
		dm_digtable.prepd_thstate = dm_digtable.curpd_thstate;
		if (initialized <= 3)
			initialized++;
		force_write = 0;
	}
}

static void _rtl92e_dm_cs_ratio(struct net_device *dev)
{
	static u8 initialized, force_write;

	if (dm_digtable.pre_sta_connect_state == dm_digtable.cur_sta_connect_state) {
		if (dm_digtable.cur_sta_connect_state == DIG_STA_CONNECT) {
			if (dm_digtable.rssi_val <= dm_digtable.rssi_low_thresh)
				dm_digtable.curcs_ratio_state = DIG_CS_RATIO_LOWER;
			else if (dm_digtable.rssi_val >= dm_digtable.rssi_high_thresh)
				dm_digtable.curcs_ratio_state = DIG_CS_RATIO_HIGHER;
			else
				dm_digtable.curcs_ratio_state = dm_digtable.precs_ratio_state;
		} else {
			dm_digtable.curcs_ratio_state = DIG_CS_RATIO_LOWER;
		}
	} else {
		dm_digtable.curcs_ratio_state = DIG_CS_RATIO_LOWER;
	}

	if ((dm_digtable.precs_ratio_state != dm_digtable.curcs_ratio_state) ||
	    !initialized || force_write) {
		if (dm_digtable.curcs_ratio_state == DIG_CS_RATIO_LOWER)
			rtl92e_writeb(dev, 0xa0a, 0x08);
		else if (dm_digtable.curcs_ratio_state == DIG_CS_RATIO_HIGHER)
			rtl92e_writeb(dev, 0xa0a, 0xcd);
		dm_digtable.precs_ratio_state = dm_digtable.curcs_ratio_state;
		initialized = 1;
		force_write = 0;
	}
}

void rtl92e_dm_init_edca_turbo(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->bcurrent_turbo_EDCA = false;
	priv->rtllib->bis_any_nonbepkts = false;
	priv->bis_cur_rdlstate = false;
}

static void _rtl92e_dm_check_edca_turbo(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rt_hi_throughput *ht_info = priv->rtllib->ht_info;

	static unsigned long lastTxOkCnt;
	static unsigned long lastRxOkCnt;
	unsigned long curTxOkCnt = 0;
	unsigned long curRxOkCnt = 0;

	if (priv->rtllib->link_state != MAC80211_LINKED)
		goto dm_CheckEdcaTurbo_EXIT;
	if (priv->rtllib->ht_info->iot_action & HT_IOT_ACT_DISABLE_EDCA_TURBO)
		goto dm_CheckEdcaTurbo_EXIT;

	if (!priv->rtllib->bis_any_nonbepkts) {
		curTxOkCnt = priv->stats.txbytesunicast - lastTxOkCnt;
		curRxOkCnt = priv->stats.rxbytesunicast - lastRxOkCnt;
		if (ht_info->iot_action & HT_IOT_ACT_EDCA_BIAS_ON_RX) {
			if (curTxOkCnt > 4 * curRxOkCnt) {
				if (priv->bis_cur_rdlstate ||
				    !priv->bcurrent_turbo_EDCA) {
					rtl92e_writel(dev, EDCAPARA_BE,
						      edca_setting_UL[ht_info->IOTPeer]);
					priv->bis_cur_rdlstate = false;
				}
			} else {
				if (!priv->bis_cur_rdlstate ||
				    !priv->bcurrent_turbo_EDCA) {
					if (priv->rtllib->mode == WIRELESS_MODE_G)
						rtl92e_writel(dev, EDCAPARA_BE,
							      edca_setting_DL_GMode[ht_info->IOTPeer]);
					else
						rtl92e_writel(dev, EDCAPARA_BE,
							      edca_setting_DL[ht_info->IOTPeer]);
					priv->bis_cur_rdlstate = true;
				}
			}
			priv->bcurrent_turbo_EDCA = true;
		} else {
			if (curRxOkCnt > 4 * curTxOkCnt) {
				if (!priv->bis_cur_rdlstate ||
				    !priv->bcurrent_turbo_EDCA) {
					if (priv->rtllib->mode == WIRELESS_MODE_G)
						rtl92e_writel(dev, EDCAPARA_BE,
							      edca_setting_DL_GMode[ht_info->IOTPeer]);
					else
						rtl92e_writel(dev, EDCAPARA_BE,
							      edca_setting_DL[ht_info->IOTPeer]);
					priv->bis_cur_rdlstate = true;
				}
			} else {
				if (priv->bis_cur_rdlstate ||
				    !priv->bcurrent_turbo_EDCA) {
					rtl92e_writel(dev, EDCAPARA_BE,
						      edca_setting_UL[ht_info->IOTPeer]);
					priv->bis_cur_rdlstate = false;
				}
			}

			priv->bcurrent_turbo_EDCA = true;
		}
	} else {
		if (priv->bcurrent_turbo_EDCA) {
			u8 tmp = AC0_BE;

			priv->rtllib->SetHwRegHandler(dev, HW_VAR_AC_PARAM,
						      (u8 *)(&tmp));
			priv->bcurrent_turbo_EDCA = false;
		}
	}

dm_CheckEdcaTurbo_EXIT:
	priv->rtllib->bis_any_nonbepkts = false;
	lastTxOkCnt = priv->stats.txbytesunicast;
	lastRxOkCnt = priv->stats.rxbytesunicast;
}

static void _rtl92e_dm_init_cts_to_self(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv((struct net_device *)dev);

	priv->rtllib->bCTSToSelfEnable = true;
}

static void _rtl92e_dm_cts_to_self(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv((struct net_device *)dev);
	struct rt_hi_throughput *ht_info = priv->rtllib->ht_info;
	static unsigned long lastTxOkCnt;
	static unsigned long lastRxOkCnt;
	unsigned long curTxOkCnt = 0;
	unsigned long curRxOkCnt = 0;

	if (!priv->rtllib->bCTSToSelfEnable) {
		ht_info->iot_action &= ~HT_IOT_ACT_FORCED_CTS2SELF;
		return;
	}
	if (ht_info->IOTPeer == HT_IOT_PEER_BROADCOM) {
		curTxOkCnt = priv->stats.txbytesunicast - lastTxOkCnt;
		curRxOkCnt = priv->stats.rxbytesunicast - lastRxOkCnt;
		if (curRxOkCnt > 4 * curTxOkCnt)
			ht_info->iot_action &= ~HT_IOT_ACT_FORCED_CTS2SELF;
		else
			ht_info->iot_action |= HT_IOT_ACT_FORCED_CTS2SELF;

		lastTxOkCnt = priv->stats.txbytesunicast;
		lastRxOkCnt = priv->stats.rxbytesunicast;
	}
}

static void _rtl92e_dm_check_rf_ctrl_gpio(void *data)
{
	struct r8192_priv *priv = container_of_dwork_rsl(data,
				  struct r8192_priv, gpio_change_rf_wq);
	struct net_device *dev = priv->rtllib->dev;
	u8 tmp1byte;
	enum rt_rf_power_state rf_power_state_to_set;
	bool bActuallySet = false;

	if ((priv->up_first_time == 1) || (priv->being_init_adapter))
		return;

	if (priv->bfirst_after_down)
		return;

	tmp1byte = rtl92e_readb(dev, GPI);

	rf_power_state_to_set = (tmp1byte & BIT(1)) ?  rf_on : rf_off;

	if (priv->hw_radio_off && (rf_power_state_to_set == rf_on)) {
		netdev_info(dev, "gpiochangeRF  - HW Radio ON\n");
		priv->hw_radio_off = false;
		bActuallySet = true;
	} else if (!priv->hw_radio_off && (rf_power_state_to_set == rf_off)) {
		netdev_info(dev, "gpiochangeRF  - HW Radio OFF\n");
		priv->hw_radio_off = true;
		bActuallySet = true;
	}

	if (bActuallySet) {
		mdelay(1000);
		priv->hw_rf_off_action = 1;
		rtl92e_set_rf_state(dev, rf_power_state_to_set, RF_CHANGE_BY_HW);
	}
}

void rtl92e_dm_rf_pathcheck_wq(void *data)
{
	struct r8192_priv *priv = container_of_dwork_rsl(data,
				  struct r8192_priv,
				  rfpath_check_wq);
	struct net_device *dev = priv->rtllib->dev;
	u8 rfpath, i;

	rfpath = rtl92e_readb(dev, 0xc04);

	for (i = 0; i < RF90_PATH_MAX; i++) {
		if (rfpath & (0x01 << i))
			priv->brfpath_rxenable[i] = true;
		else
			priv->brfpath_rxenable[i] = false;
	}
	if (!dm_rx_path_sel_table.enable)
		return;

	_rtl92e_dm_rx_path_sel_byrssi(dev);
}

static void _rtl92e_dm_init_rx_path_selection(struct net_device *dev)
{
	u8 i;
	struct r8192_priv *priv = rtllib_priv(dev);

	dm_rx_path_sel_table.enable = 1;
	dm_rx_path_sel_table.ss_th_low = RX_PATH_SEL_SS_TH_LOW;
	dm_rx_path_sel_table.diff_th = RX_PATH_SEL_DIFF_TH;
	if (priv->customer_id == RT_CID_819X_NETCORE)
		dm_rx_path_sel_table.cck_method = CCK_Rx_Version_2;
	else
		dm_rx_path_sel_table.cck_method = CCK_Rx_Version_1;
	dm_rx_path_sel_table.disabled_rf = 0;
	for (i = 0; i < 4; i++) {
		dm_rx_path_sel_table.rf_rssi[i] = 50;
		dm_rx_path_sel_table.cck_pwdb_sta[i] = -64;
		dm_rx_path_sel_table.rf_enable_rssi_th[i] = 100;
	}
}

#define PWDB_IN_RANGE	((cur_cck_pwdb < tmp_cck_max_pwdb) &&	\
			(cur_cck_pwdb > tmp_cck_sec_pwdb))

static void _rtl92e_dm_rx_path_sel_byrssi(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 i, max_rssi_index = 0, min_rssi_index = 0;
	u8 sec_rssi_index = 0, rf_num = 0;
	u8 tmp_max_rssi = 0, tmp_min_rssi = 0, tmp_sec_rssi = 0;
	u8 cck_default_Rx = 0x2;
	u8 cck_optional_Rx = 0x3;
	long tmp_cck_max_pwdb = 0, tmp_cck_min_pwdb = 0, tmp_cck_sec_pwdb = 0;
	u8 cck_rx_ver2_max_index = 0;
	u8 cck_rx_ver2_sec_index = 0;
	u8 cur_rf_rssi;
	long cur_cck_pwdb;
	static u8 disabled_rf_cnt, cck_Rx_Path_initialized;
	u8 update_cck_rx_path;

	if (!cck_Rx_Path_initialized) {
		dm_rx_path_sel_table.cck_rx_path = (rtl92e_readb(dev, 0xa07) & 0xf);
		cck_Rx_Path_initialized = 1;
	}

	dm_rx_path_sel_table.disabled_rf = 0xf;
	dm_rx_path_sel_table.disabled_rf &= ~(rtl92e_readb(dev, 0xc04));

	if (priv->rtllib->mode == WIRELESS_MODE_B)
		dm_rx_path_sel_table.cck_method = CCK_Rx_Version_2;

	for (i = 0; i < RF90_PATH_MAX; i++) {
		dm_rx_path_sel_table.rf_rssi[i] = priv->stats.rx_rssi_percentage[i];

		if (priv->brfpath_rxenable[i]) {
			rf_num++;
			cur_rf_rssi = dm_rx_path_sel_table.rf_rssi[i];

			if (rf_num == 1) {
				max_rssi_index = min_rssi_index = sec_rssi_index = i;
				tmp_max_rssi = tmp_min_rssi = tmp_sec_rssi = cur_rf_rssi;
			} else if (rf_num == 2) {
				if (cur_rf_rssi >= tmp_max_rssi) {
					tmp_max_rssi = cur_rf_rssi;
					max_rssi_index = i;
				} else {
					tmp_sec_rssi = tmp_min_rssi = cur_rf_rssi;
					sec_rssi_index = min_rssi_index = i;
				}
			} else {
				if (cur_rf_rssi > tmp_max_rssi) {
					tmp_sec_rssi = tmp_max_rssi;
					sec_rssi_index = max_rssi_index;
					tmp_max_rssi = cur_rf_rssi;
					max_rssi_index = i;
				} else if (cur_rf_rssi == tmp_max_rssi) {
					tmp_sec_rssi = cur_rf_rssi;
					sec_rssi_index = i;
				} else if ((cur_rf_rssi < tmp_max_rssi) &&
					   (cur_rf_rssi > tmp_sec_rssi)) {
					tmp_sec_rssi = cur_rf_rssi;
					sec_rssi_index = i;
				} else if (cur_rf_rssi == tmp_sec_rssi) {
					if (tmp_sec_rssi == tmp_min_rssi) {
						tmp_sec_rssi = cur_rf_rssi;
						sec_rssi_index = i;
					}
				} else if ((cur_rf_rssi < tmp_sec_rssi) &&
					   (cur_rf_rssi > tmp_min_rssi)) {
					;
				} else if (cur_rf_rssi == tmp_min_rssi) {
					if (tmp_sec_rssi == tmp_min_rssi) {
						tmp_min_rssi = cur_rf_rssi;
						min_rssi_index = i;
					}
				} else if (cur_rf_rssi < tmp_min_rssi) {
					tmp_min_rssi = cur_rf_rssi;
					min_rssi_index = i;
				}
			}
		}
	}

	rf_num = 0;
	if (dm_rx_path_sel_table.cck_method == CCK_Rx_Version_2) {
		for (i = 0; i < RF90_PATH_MAX; i++) {
			if (priv->brfpath_rxenable[i]) {
				rf_num++;
				cur_cck_pwdb =
					 dm_rx_path_sel_table.cck_pwdb_sta[i];

				if (rf_num == 1) {
					cck_rx_ver2_max_index = i;
					cck_rx_ver2_sec_index = i;
					tmp_cck_max_pwdb = cur_cck_pwdb;
					tmp_cck_min_pwdb = cur_cck_pwdb;
					tmp_cck_sec_pwdb = cur_cck_pwdb;
				} else if (rf_num == 2) {
					if (cur_cck_pwdb >= tmp_cck_max_pwdb) {
						tmp_cck_max_pwdb = cur_cck_pwdb;
						cck_rx_ver2_max_index = i;
					} else {
						tmp_cck_sec_pwdb = cur_cck_pwdb;
						tmp_cck_min_pwdb = cur_cck_pwdb;
						cck_rx_ver2_sec_index = i;
					}
				} else {
					if (cur_cck_pwdb > tmp_cck_max_pwdb) {
						tmp_cck_sec_pwdb =
							 tmp_cck_max_pwdb;
						cck_rx_ver2_sec_index =
							 cck_rx_ver2_max_index;
						tmp_cck_max_pwdb = cur_cck_pwdb;
						cck_rx_ver2_max_index = i;
					} else if (cur_cck_pwdb ==
						   tmp_cck_max_pwdb) {
						tmp_cck_sec_pwdb = cur_cck_pwdb;
						cck_rx_ver2_sec_index = i;
					} else if (PWDB_IN_RANGE) {
						tmp_cck_sec_pwdb = cur_cck_pwdb;
						cck_rx_ver2_sec_index = i;
					} else if (cur_cck_pwdb ==
						   tmp_cck_sec_pwdb) {
						if (tmp_cck_sec_pwdb ==
						    tmp_cck_min_pwdb) {
							tmp_cck_sec_pwdb =
								 cur_cck_pwdb;
							cck_rx_ver2_sec_index =
								 i;
						}
					} else if ((cur_cck_pwdb < tmp_cck_sec_pwdb) &&
						   (cur_cck_pwdb > tmp_cck_min_pwdb)) {
						;
					} else if (cur_cck_pwdb == tmp_cck_min_pwdb) {
						if (tmp_cck_sec_pwdb == tmp_cck_min_pwdb)
							tmp_cck_min_pwdb = cur_cck_pwdb;
					} else if (cur_cck_pwdb < tmp_cck_min_pwdb) {
						tmp_cck_min_pwdb = cur_cck_pwdb;
					}
				}
			}
		}
	}

	update_cck_rx_path = 0;
	if (dm_rx_path_sel_table.cck_method == CCK_Rx_Version_2) {
		cck_default_Rx = cck_rx_ver2_max_index;
		cck_optional_Rx = cck_rx_ver2_sec_index;
		if (tmp_cck_max_pwdb != -64)
			update_cck_rx_path = 1;
	}

	if (tmp_min_rssi < dm_rx_path_sel_table.ss_th_low && disabled_rf_cnt < 2) {
		if ((tmp_max_rssi - tmp_min_rssi) >=
		     dm_rx_path_sel_table.diff_th) {
			dm_rx_path_sel_table.rf_enable_rssi_th[min_rssi_index] =
				 tmp_max_rssi + 5;
			rtl92e_set_bb_reg(dev, rOFDM0_TRxPathEnable,
					  0x1 << min_rssi_index, 0x0);
			rtl92e_set_bb_reg(dev, rOFDM1_TRxPathEnable,
					  0x1 << min_rssi_index, 0x0);
			disabled_rf_cnt++;
		}
		if (dm_rx_path_sel_table.cck_method == CCK_Rx_Version_1) {
			cck_default_Rx = max_rssi_index;
			cck_optional_Rx = sec_rssi_index;
			if (tmp_max_rssi)
				update_cck_rx_path = 1;
		}
	}

	if (update_cck_rx_path) {
		dm_rx_path_sel_table.cck_rx_path = (cck_default_Rx << 2) |
						(cck_optional_Rx);
		rtl92e_set_bb_reg(dev, rCCK0_AFESetting, 0x0f000000,
				  dm_rx_path_sel_table.cck_rx_path);
	}

	if (dm_rx_path_sel_table.disabled_rf) {
		for (i = 0; i < 4; i++) {
			if ((dm_rx_path_sel_table.disabled_rf >> i) & 0x1) {
				if (tmp_max_rssi >=
				    dm_rx_path_sel_table.rf_enable_rssi_th[i]) {
					rtl92e_set_bb_reg(dev,
							  rOFDM0_TRxPathEnable,
							  0x1 << i, 0x1);
					rtl92e_set_bb_reg(dev,
							  rOFDM1_TRxPathEnable,
							  0x1 << i, 0x1);
					dm_rx_path_sel_table.rf_enable_rssi_th[i]
						 = 100;
					disabled_rf_cnt--;
				}
			}
		}
	}
}

static void _rtl92e_dm_check_rx_path_selection(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	schedule_delayed_work(&priv->rfpath_check_wq, 0);
}

static void _rtl92e_dm_init_fsync(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->rtllib->fsync_time_interval = 500;
	priv->rtllib->fsync_rate_bitmap = 0x0f000800;
	priv->rtllib->fsync_rssi_threshold = 30;
	priv->rtllib->bfsync_enable = false;
	priv->rtllib->fsync_multiple_timeinterval = 3;
	priv->rtllib->fsync_firstdiff_ratethreshold = 100;
	priv->rtllib->fsync_seconddiff_ratethreshold = 200;
	priv->rtllib->fsync_state = Default_Fsync;

	timer_setup(&priv->fsync_timer, _rtl92e_dm_fsync_timer_callback, 0);
}

static void _rtl92e_dm_deinit_fsync(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	del_timer_sync(&priv->fsync_timer);
}

static void _rtl92e_dm_fsync_timer_callback(struct timer_list *t)
{
	struct r8192_priv *priv = from_timer(priv, t, fsync_timer);
	struct net_device *dev = priv->rtllib->dev;
	u32 rate_index, rate_count = 0, rate_count_diff = 0;
	bool		bSwitchFromCountDiff = false;
	bool		bDoubleTimeInterval = false;

	if (priv->rtllib->link_state == MAC80211_LINKED &&
	    priv->rtllib->bfsync_enable &&
	    (priv->rtllib->ht_info->iot_action & HT_IOT_ACT_CDD_FSYNC)) {
		u32 rate_bitmap;

		for (rate_index = 0; rate_index <= 27; rate_index++) {
			rate_bitmap  = 1 << rate_index;
			if (priv->rtllib->fsync_rate_bitmap &  rate_bitmap)
				rate_count +=
				   priv->stats.received_rate_histogram[1]
				   [rate_index];
		}

		if (rate_count < priv->rate_record)
			rate_count_diff = 0xffffffff - rate_count +
					  priv->rate_record;
		else
			rate_count_diff = rate_count - priv->rate_record;
		if (rate_count_diff < priv->rate_count_diff_rec) {
			u32 DiffNum = priv->rate_count_diff_rec -
				      rate_count_diff;
			if (DiffNum >=
			    priv->rtllib->fsync_seconddiff_ratethreshold)
				priv->continue_diff_count++;
			else
				priv->continue_diff_count = 0;

			if (priv->continue_diff_count >= 2) {
				bSwitchFromCountDiff = true;
				priv->continue_diff_count = 0;
			}
		} else {
			priv->continue_diff_count = 0;
		}

		if (rate_count_diff <=
		    priv->rtllib->fsync_firstdiff_ratethreshold) {
			bSwitchFromCountDiff = true;
			priv->continue_diff_count = 0;
		}
		priv->rate_record = rate_count;
		priv->rate_count_diff_rec = rate_count_diff;
		if (priv->undecorated_smoothed_pwdb >
		    priv->rtllib->fsync_rssi_threshold &&
		    bSwitchFromCountDiff) {
			bDoubleTimeInterval = true;
			priv->bswitch_fsync = !priv->bswitch_fsync;
			if (priv->bswitch_fsync) {
				rtl92e_writeb(dev, 0xC36, 0x1c);
				rtl92e_writeb(dev, 0xC3e, 0x90);
			} else {
				rtl92e_writeb(dev, 0xC36, 0x5c);
				rtl92e_writeb(dev, 0xC3e, 0x96);
			}
		} else if (priv->undecorated_smoothed_pwdb <=
			   priv->rtllib->fsync_rssi_threshold) {
			if (priv->bswitch_fsync) {
				priv->bswitch_fsync  = false;
				rtl92e_writeb(dev, 0xC36, 0x5c);
				rtl92e_writeb(dev, 0xC3e, 0x96);
			}
		}
		if (bDoubleTimeInterval) {
			if (timer_pending(&priv->fsync_timer))
				del_timer_sync(&priv->fsync_timer);
			priv->fsync_timer.expires = jiffies +
				 msecs_to_jiffies(priv->rtllib->fsync_time_interval *
				 priv->rtllib->fsync_multiple_timeinterval);
			add_timer(&priv->fsync_timer);
		} else {
			if (timer_pending(&priv->fsync_timer))
				del_timer_sync(&priv->fsync_timer);
			priv->fsync_timer.expires = jiffies +
				 msecs_to_jiffies(priv->rtllib->fsync_time_interval);
			add_timer(&priv->fsync_timer);
		}
	} else {
		if (priv->bswitch_fsync) {
			priv->bswitch_fsync  = false;
			rtl92e_writeb(dev, 0xC36, 0x5c);
			rtl92e_writeb(dev, 0xC3e, 0x96);
		}
		priv->continue_diff_count = 0;
		rtl92e_writel(dev, rOFDM0_RxDetector2, 0x465c52cd);
	}
}

static void _rtl92e_dm_start_hw_fsync(struct net_device *dev)
{
	u8 rf_timing = 0x77;
	struct r8192_priv *priv = rtllib_priv(dev);

	rtl92e_writel(dev, rOFDM0_RxDetector2, 0x465c12cf);
	priv->rtllib->SetHwRegHandler(dev, HW_VAR_RF_TIMING,
				      (u8 *)(&rf_timing));
	rtl92e_writeb(dev, 0xc3b, 0x41);
}

static void _rtl92e_dm_end_hw_fsync(struct net_device *dev)
{
	u8 rf_timing = 0xaa;
	struct r8192_priv *priv = rtllib_priv(dev);

	rtl92e_writel(dev, rOFDM0_RxDetector2, 0x465c52cd);
	priv->rtllib->SetHwRegHandler(dev, HW_VAR_RF_TIMING, (u8 *)
				     (&rf_timing));
	rtl92e_writeb(dev, 0xc3b, 0x49);
}

static void _rtl92e_dm_end_sw_fsync(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	del_timer_sync(&(priv->fsync_timer));

	if (priv->bswitch_fsync) {
		priv->bswitch_fsync  = false;

		rtl92e_writeb(dev, 0xC36, 0x5c);

		rtl92e_writeb(dev, 0xC3e, 0x96);
	}

	priv->continue_diff_count = 0;
	rtl92e_writel(dev, rOFDM0_RxDetector2, 0x465c52cd);
}

static void _rtl92e_dm_start_sw_fsync(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	u32 rate_index;
	u32 rate_bitmap;

	priv->rate_record = 0;
	priv->continue_diff_count = 0;
	priv->rate_count_diff_rec = 0;
	priv->bswitch_fsync  = false;

	if (priv->rtllib->mode == WIRELESS_MODE_N_24G) {
		priv->rtllib->fsync_firstdiff_ratethreshold = 600;
		priv->rtllib->fsync_seconddiff_ratethreshold = 0xffff;
	} else {
		priv->rtllib->fsync_firstdiff_ratethreshold = 200;
		priv->rtllib->fsync_seconddiff_ratethreshold = 200;
	}
	for (rate_index = 0; rate_index <= 27; rate_index++) {
		rate_bitmap  = 1 << rate_index;
		if (priv->rtllib->fsync_rate_bitmap & rate_bitmap)
			priv->rate_record +=
				 priv->stats.received_rate_histogram[1]
				[rate_index];
	}
	if (timer_pending(&priv->fsync_timer))
		del_timer_sync(&priv->fsync_timer);
	priv->fsync_timer.expires = jiffies +
				    msecs_to_jiffies(priv->rtllib->fsync_time_interval);
	add_timer(&priv->fsync_timer);

	rtl92e_writel(dev, rOFDM0_RxDetector2, 0x465c12cd);
}

static void _rtl92e_dm_check_fsync(struct net_device *dev)
{
#define	RegC38_Default			0
#define	RegC38_NonFsync_Other_AP	1
#define	RegC38_Fsync_AP_BCM		2
	struct r8192_priv *priv = rtllib_priv(dev);
	static u8 reg_c38_State = RegC38_Default;

	if (priv->rtllib->link_state == MAC80211_LINKED &&
	    priv->rtllib->ht_info->IOTPeer == HT_IOT_PEER_BROADCOM) {
		if (priv->rtllib->bfsync_enable == 0) {
			switch (priv->rtllib->fsync_state) {
			case Default_Fsync:
				_rtl92e_dm_start_hw_fsync(dev);
				priv->rtllib->fsync_state = HW_Fsync;
				break;
			case SW_Fsync:
				_rtl92e_dm_end_sw_fsync(dev);
				_rtl92e_dm_start_hw_fsync(dev);
				priv->rtllib->fsync_state = HW_Fsync;
				break;
			case HW_Fsync:
			default:
				break;
			}
		} else {
			switch (priv->rtllib->fsync_state) {
			case Default_Fsync:
				_rtl92e_dm_start_sw_fsync(dev);
				priv->rtllib->fsync_state = SW_Fsync;
				break;
			case HW_Fsync:
				_rtl92e_dm_end_hw_fsync(dev);
				_rtl92e_dm_start_sw_fsync(dev);
				priv->rtllib->fsync_state = SW_Fsync;
				break;
			case SW_Fsync:
			default:
				break;
			}
		}
		if (reg_c38_State != RegC38_Fsync_AP_BCM) {
			rtl92e_writeb(dev, rOFDM0_RxDetector3, 0x95);

			reg_c38_State = RegC38_Fsync_AP_BCM;
		}
	} else {
		switch (priv->rtllib->fsync_state) {
		case HW_Fsync:
			_rtl92e_dm_end_hw_fsync(dev);
			priv->rtllib->fsync_state = Default_Fsync;
			break;
		case SW_Fsync:
			_rtl92e_dm_end_sw_fsync(dev);
			priv->rtllib->fsync_state = Default_Fsync;
			break;
		case Default_Fsync:
		default:
			break;
		}

		if (priv->rtllib->link_state == MAC80211_LINKED) {
			if (priv->undecorated_smoothed_pwdb <=
			    RegC38_TH) {
				if (reg_c38_State !=
				    RegC38_NonFsync_Other_AP) {
					rtl92e_writeb(dev,
						      rOFDM0_RxDetector3,
						      0x90);

					reg_c38_State =
					     RegC38_NonFsync_Other_AP;
				}
			} else if (priv->undecorated_smoothed_pwdb >=
				   (RegC38_TH + 5)) {
				if (reg_c38_State) {
					rtl92e_writeb(dev,
						rOFDM0_RxDetector3,
						priv->framesync);
					reg_c38_State = RegC38_Default;
				}
			}
		} else {
			if (reg_c38_State) {
				rtl92e_writeb(dev, rOFDM0_RxDetector3,
					      priv->framesync);
				reg_c38_State = RegC38_Default;
			}
		}
	}
}

/*---------------------------Define function prototype------------------------*/
static void _rtl92e_dm_init_dynamic_tx_power(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	priv->rtllib->bdynamic_txpower_enable = true;
	priv->last_dtp_flag_high = false;
	priv->last_dtp_flag_low = false;
	priv->dynamic_tx_high_pwr = false;
	priv->dynamic_tx_low_pwr = false;
}

static void _rtl92e_dm_dynamic_tx_power(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	unsigned int txhipower_threshold = 0;
	unsigned int txlowpower_threshold = 0;

	if (!priv->rtllib->bdynamic_txpower_enable) {
		priv->dynamic_tx_high_pwr = false;
		priv->dynamic_tx_low_pwr = false;
		return;
	}
	if ((priv->rtllib->ht_info->IOTPeer == HT_IOT_PEER_ATHEROS) &&
	    (priv->rtllib->mode == WIRELESS_MODE_G)) {
		txhipower_threshold = TX_POWER_ATHEROAP_THRESH_HIGH;
		txlowpower_threshold = TX_POWER_ATHEROAP_THRESH_LOW;
	} else {
		txhipower_threshold = TX_POWER_NEAR_FIELD_THRESH_HIGH;
		txlowpower_threshold = TX_POWER_NEAR_FIELD_THRESH_LOW;
	}

	if (priv->rtllib->link_state == MAC80211_LINKED) {
		if (priv->undecorated_smoothed_pwdb >= txhipower_threshold) {
			priv->dynamic_tx_high_pwr = true;
			priv->dynamic_tx_low_pwr = false;
		} else {
			if (priv->undecorated_smoothed_pwdb <
			    txlowpower_threshold && priv->dynamic_tx_high_pwr)
				priv->dynamic_tx_high_pwr = false;
			if (priv->undecorated_smoothed_pwdb < 35)
				priv->dynamic_tx_low_pwr = true;
			else if (priv->undecorated_smoothed_pwdb >= 40)
				priv->dynamic_tx_low_pwr = false;
		}
	} else {
		priv->dynamic_tx_high_pwr = false;
		priv->dynamic_tx_low_pwr = false;
	}

	if ((priv->dynamic_tx_high_pwr != priv->last_dtp_flag_high) ||
	    (priv->dynamic_tx_low_pwr != priv->last_dtp_flag_low)) {
		rtl92e_set_tx_power(dev, priv->rtllib->current_network.channel);
	}
	priv->last_dtp_flag_high = priv->dynamic_tx_high_pwr;
	priv->last_dtp_flag_low = priv->dynamic_tx_low_pwr;
}

static void _rtl92e_dm_check_txrateandretrycount(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;

	ieee->softmac_stats.CurrentShowTxate = rtl92e_readb(dev, CURRENT_TX_RATE_REG);
	ieee->softmac_stats.last_packet_rate = rtl92e_readb(dev, INITIAL_TX_RATE_REG);
	ieee->softmac_stats.txretrycount = rtl92e_readl(dev, TX_RETRY_COUNT_REG);
}

static void _rtl92e_dm_send_rssi_to_fw(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	rtl92e_writeb(dev, DRIVER_RSSI, priv->undecorated_smoothed_pwdb);
}
