// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include "rtl_core.h"
#include "r8192E_phy.h"
#include "r8192E_phyreg.h"
#include "r8190P_rtl8256.h" /* RTL8225 Radio frontend */
#include "r8192E_cmdpkt.h"

void rtl92e_cam_reset(struct net_device *dev)
{
	u32 ulcommand = 0;

	ulcommand |= BIT31|BIT30;
	rtl92e_writel(dev, RWCAM, ulcommand);
}

void rtl92e_enable_hw_security_config(struct net_device *dev)
{
	u8 SECR_value = 0x0;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;

	SECR_value = SCR_TxEncEnable | SCR_RxDecEnable;
	if (((ieee->pairwise_key_type == KEY_TYPE_WEP40) ||
	     (ieee->pairwise_key_type == KEY_TYPE_WEP104)) &&
	     (priv->rtllib->auth_mode != 2)) {
		SECR_value |= SCR_RxUseDK;
		SECR_value |= SCR_TxUseDK;
	} else if ((ieee->iw_mode == IW_MODE_ADHOC) &&
		   (ieee->pairwise_key_type & (KEY_TYPE_CCMP |
		   KEY_TYPE_TKIP))) {
		SECR_value |= SCR_RxUseDK;
		SECR_value |= SCR_TxUseDK;
	}


	ieee->hwsec_active = 1;
	if ((ieee->pHTInfo->IOTAction&HT_IOT_ACT_PURE_N_MODE) || !hwwep) {
		ieee->hwsec_active = 0;
		SECR_value &= ~SCR_RxDecEnable;
	}

	RT_TRACE(COMP_SEC, "%s:, hwsec:%d, pairwise_key:%d, SECR_value:%x\n",
		 __func__, ieee->hwsec_active, ieee->pairwise_key_type,
		 SECR_value);
	rtl92e_writeb(dev, SECR, SECR_value);
}

void rtl92e_set_swcam(struct net_device *dev, u8 EntryNo, u8 KeyIndex,
		      u16 KeyType, const u8 *MacAddr, u8 DefaultKey,
		      u32 *KeyContent, u8 is_mesh)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;

	RT_TRACE(COMP_DBG,
		 "===========>%s():EntryNo is %d,KeyIndex is %d,KeyType is %d,is_mesh is %d\n",
		 __func__, EntryNo, KeyIndex, KeyType, is_mesh);

	if (EntryNo >= TOTAL_CAM_ENTRY)
		return;

	if (!is_mesh) {
		ieee->swcamtable[EntryNo].bused = true;
		ieee->swcamtable[EntryNo].key_index = KeyIndex;
		ieee->swcamtable[EntryNo].key_type = KeyType;
		memcpy(ieee->swcamtable[EntryNo].macaddr, MacAddr, 6);
		ieee->swcamtable[EntryNo].useDK = DefaultKey;
		memcpy(ieee->swcamtable[EntryNo].key_buf, (u8 *)KeyContent, 16);
	}
}

void rtl92e_set_key(struct net_device *dev, u8 EntryNo, u8 KeyIndex,
		    u16 KeyType, const u8 *MacAddr, u8 DefaultKey,
		    u32 *KeyContent)
{
	u32 TargetCommand = 0;
	u32 TargetContent = 0;
	u16 usConfig = 0;
	u8 i;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	enum rt_rf_power_state rtState;

	rtState = priv->rtllib->eRFPowerState;
	if (priv->rtllib->PowerSaveControl.bInactivePs) {
		if (rtState == eRfOff) {
			if (priv->rtllib->RfOffReason > RF_CHANGE_BY_IPS) {
				netdev_warn(dev, "%s(): RF is OFF.\n",
					    __func__);
				return;
			}
			mutex_lock(&priv->rtllib->ips_mutex);
			rtl92e_ips_leave(dev);
			mutex_unlock(&priv->rtllib->ips_mutex);
		}
	}
	priv->rtllib->is_set_key = true;
	if (EntryNo >= TOTAL_CAM_ENTRY) {
		netdev_info(dev, "%s(): Invalid CAM entry\n", __func__);
		return;
	}

	RT_TRACE(COMP_SEC,
		 "====>to %s, dev:%p, EntryNo:%d, KeyIndex:%d,KeyType:%d, MacAddr %pM\n",
		 __func__, dev, EntryNo, KeyIndex, KeyType, MacAddr);

	if (DefaultKey)
		usConfig |= BIT15 | (KeyType<<2);
	else
		usConfig |= BIT15 | (KeyType<<2) | KeyIndex;


	for (i = 0; i < CAM_CONTENT_COUNT; i++) {
		TargetCommand  = i + CAM_CONTENT_COUNT * EntryNo;
		TargetCommand |= BIT31|BIT16;

		if (i == 0) {
			TargetContent = (u32)(*(MacAddr+0)) << 16 |
				(u32)(*(MacAddr+1)) << 24 |
				(u32)usConfig;

			rtl92e_writel(dev, WCAMI, TargetContent);
			rtl92e_writel(dev, RWCAM, TargetCommand);
		} else if (i == 1) {
			TargetContent = (u32)(*(MacAddr+2)) |
				(u32)(*(MacAddr+3)) <<  8 |
				(u32)(*(MacAddr+4)) << 16 |
				(u32)(*(MacAddr+5)) << 24;
			rtl92e_writel(dev, WCAMI, TargetContent);
			rtl92e_writel(dev, RWCAM, TargetCommand);
		} else {
			if (KeyContent != NULL) {
				rtl92e_writel(dev, WCAMI,
					      (u32)(*(KeyContent+i-2)));
				rtl92e_writel(dev, RWCAM, TargetCommand);
				udelay(100);
			}
		}
	}
	RT_TRACE(COMP_SEC, "=========>after set key, usconfig:%x\n", usConfig);
}

void rtl92e_cam_restore(struct net_device *dev)
{
	u8 EntryId = 0;
	struct r8192_priv *priv = rtllib_priv(dev);
	u8 *MacAddr = priv->rtllib->current_network.bssid;

	static u8	CAM_CONST_ADDR[4][6] = {
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x02},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x03}
	};
	static u8	CAM_CONST_BROAD[] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};

	RT_TRACE(COMP_SEC, "%s:\n", __func__);


	if ((priv->rtllib->pairwise_key_type == KEY_TYPE_WEP40) ||
	    (priv->rtllib->pairwise_key_type == KEY_TYPE_WEP104)) {

		for (EntryId = 0; EntryId < 4; EntryId++) {
			MacAddr = CAM_CONST_ADDR[EntryId];
			if (priv->rtllib->swcamtable[EntryId].bused) {
				rtl92e_set_key(dev, EntryId, EntryId,
					       priv->rtllib->pairwise_key_type,
					       MacAddr, 0,
					       (u32 *)(&priv->rtllib->swcamtable
						       [EntryId].key_buf[0]));
			}
		}

	} else if (priv->rtllib->pairwise_key_type == KEY_TYPE_TKIP) {
		if (priv->rtllib->iw_mode == IW_MODE_ADHOC) {
			rtl92e_set_key(dev, 4, 0,
				       priv->rtllib->pairwise_key_type,
				       (const u8 *)dev->dev_addr, 0,
				       (u32 *)(&priv->rtllib->swcamtable[4].key_buf[0]));
		} else {
			rtl92e_set_key(dev, 4, 0,
				       priv->rtllib->pairwise_key_type,
				       MacAddr, 0,
				       (u32 *)(&priv->rtllib->swcamtable[4].key_buf[0]));
		}

	} else if (priv->rtllib->pairwise_key_type == KEY_TYPE_CCMP) {
		if (priv->rtllib->iw_mode == IW_MODE_ADHOC) {
			rtl92e_set_key(dev, 4, 0,
				       priv->rtllib->pairwise_key_type,
				       (const u8 *)dev->dev_addr, 0,
				       (u32 *)(&priv->rtllib->swcamtable[4].key_buf[0]));
		} else {
			rtl92e_set_key(dev, 4, 0,
				       priv->rtllib->pairwise_key_type, MacAddr,
				       0, (u32 *)(&priv->rtllib->swcamtable[4].key_buf[0]));
			}
	}

	if (priv->rtllib->group_key_type == KEY_TYPE_TKIP) {
		MacAddr = CAM_CONST_BROAD;
		for (EntryId = 1; EntryId < 4; EntryId++) {
			if (priv->rtllib->swcamtable[EntryId].bused) {
				rtl92e_set_key(dev, EntryId, EntryId,
					       priv->rtllib->group_key_type,
					       MacAddr, 0,
					       (u32 *)(&priv->rtllib->swcamtable[EntryId].key_buf[0]));
			}
		}
		if (priv->rtllib->iw_mode == IW_MODE_ADHOC) {
			if (priv->rtllib->swcamtable[0].bused) {
				rtl92e_set_key(dev, 0, 0,
					       priv->rtllib->group_key_type,
					       CAM_CONST_ADDR[0], 0,
					       (u32 *)(&priv->rtllib->swcamtable[0].key_buf[0]));
			} else {
				netdev_warn(dev,
					    "%s(): ADHOC TKIP: missing key entry.\n",
					    __func__);
				return;
			}
		}
	} else if (priv->rtllib->group_key_type == KEY_TYPE_CCMP) {
		MacAddr = CAM_CONST_BROAD;
		for (EntryId = 1; EntryId < 4; EntryId++) {
			if (priv->rtllib->swcamtable[EntryId].bused) {
				rtl92e_set_key(dev, EntryId, EntryId,
					       priv->rtllib->group_key_type,
					       MacAddr, 0,
					       (u32 *)(&priv->rtllib->swcamtable[EntryId].key_buf[0]));
			}
		}

		if (priv->rtllib->iw_mode == IW_MODE_ADHOC) {
			if (priv->rtllib->swcamtable[0].bused) {
				rtl92e_set_key(dev, 0, 0,
					       priv->rtllib->group_key_type,
					       CAM_CONST_ADDR[0], 0,
					       (u32 *)(&priv->rtllib->swcamtable[0].key_buf[0]));
			} else {
				netdev_warn(dev,
					    "%s(): ADHOC CCMP: missing key entry.\n",
					    __func__);
				return;
			}
		}
	}
}
