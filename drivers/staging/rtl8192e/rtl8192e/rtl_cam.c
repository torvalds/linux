/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#include "rtl_core.h"
#include "r8192E_phy.h"
#include "r8192E_phyreg.h"
#include "r8190P_rtl8256.h" /* RTL8225 Radio frontend */
#include "r8192E_cmdpkt.h"

extern int hwwep;
void CamResetAllEntry(struct net_device *dev)
{
	u32 ulcommand = 0;

	ulcommand |= BIT31|BIT30;
	write_nic_dword(dev, RWCAM, ulcommand);
}

void write_cam(struct net_device *dev, u8 addr, u32 data)
{
	write_nic_dword(dev, WCAMI, data);
	write_nic_dword(dev, RWCAM, BIT31|BIT16|(addr&0xff));
}

u32 read_cam(struct net_device *dev, u8 addr)
{
	write_nic_dword(dev, RWCAM, 0x80000000|(addr&0xff));
	return read_nic_dword(dev, 0xa8);
}

void EnableHWSecurityConfig8192(struct net_device *dev)
{
	u8 SECR_value = 0x0;
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;
	SECR_value = SCR_TxEncEnable | SCR_RxDecEnable;
	if (((KEY_TYPE_WEP40 == ieee->pairwise_key_type) ||
	     (KEY_TYPE_WEP104 == ieee->pairwise_key_type)) &&
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
	write_nic_byte(dev, SECR,  SECR_value);
}

void set_swcam(struct net_device *dev, u8 EntryNo, u8 KeyIndex, u16 KeyType,
	       u8 *MacAddr, u8 DefaultKey, u32 *KeyContent, u8 is_mesh)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;
	RT_TRACE(COMP_DBG, "===========>%s():EntryNo is %d,KeyIndex is "
		 "%d,KeyType is %d,is_mesh is %d\n", __func__, EntryNo,
		 KeyIndex, KeyType, is_mesh);
	if (!is_mesh) {
		ieee->swcamtable[EntryNo].bused = true;
		ieee->swcamtable[EntryNo].key_index = KeyIndex;
		ieee->swcamtable[EntryNo].key_type = KeyType;
		memcpy(ieee->swcamtable[EntryNo].macaddr, MacAddr, 6);
		ieee->swcamtable[EntryNo].useDK = DefaultKey;
		memcpy(ieee->swcamtable[EntryNo].key_buf, (u8 *)KeyContent, 16);
	}
}

void setKey(struct net_device *dev, u8 EntryNo, u8 KeyIndex, u16 KeyType,
	    u8 *MacAddr, u8 DefaultKey, u32 *KeyContent)
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
				RT_TRACE(COMP_ERR, "%s(): RF is OFF.\n",
					__func__);
				return ;
			} else {
				down(&priv->rtllib->ips_sem);
				IPSLeave(dev);
				up(&priv->rtllib->ips_sem);
			}
		}
	}
	priv->rtllib->is_set_key = true;
	if (EntryNo >= TOTAL_CAM_ENTRY)
		RT_TRACE(COMP_ERR, "cam entry exceeds in setKey()\n");

	RT_TRACE(COMP_SEC, "====>to setKey(), dev:%p, EntryNo:%d, KeyIndex:%d,"
		 "KeyType:%d, MacAddr %pM\n", dev, EntryNo, KeyIndex,
		 KeyType, MacAddr);

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

			write_nic_dword(dev, WCAMI, TargetContent);
			write_nic_dword(dev, RWCAM, TargetCommand);
		} else if (i == 1) {
			TargetContent = (u32)(*(MacAddr+2)) |
				(u32)(*(MacAddr+3)) <<  8 |
				(u32)(*(MacAddr+4)) << 16 |
				(u32)(*(MacAddr+5)) << 24;
			write_nic_dword(dev, WCAMI, TargetContent);
			write_nic_dword(dev, RWCAM, TargetCommand);
		} else {
			if (KeyContent != NULL) {
				write_nic_dword(dev, WCAMI,
						(u32)(*(KeyContent+i-2)));
				write_nic_dword(dev, RWCAM, TargetCommand);
				udelay(100);
			}
		}
	}
	RT_TRACE(COMP_SEC, "=========>after set key, usconfig:%x\n", usConfig);
}

void CAM_read_entry(struct net_device *dev, u32 iIndex)
{
	u32 target_command = 0;
	u32 target_content = 0;
	u8 entry_i = 0;
	u32 ulStatus;
	s32 i = 100;
	for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {
		target_command = entry_i+CAM_CONTENT_COUNT*iIndex;
		target_command = target_command | BIT31;

		while ((i--) >= 0) {
			ulStatus = read_nic_dword(dev, RWCAM);
			if (ulStatus & BIT31)
				continue;
			else
				break;
		}
		write_nic_dword(dev, RWCAM, target_command);
		RT_TRACE(COMP_SEC, "CAM_read_entry(): WRITE A0: %x\n",
			 target_command);
		target_content = read_nic_dword(dev, RCAMO);
		RT_TRACE(COMP_SEC, "CAM_read_entry(): WRITE A8: %x\n",
			 target_content);
	}
	printk(KERN_INFO "\n");
}

void CamRestoreAllEntry(struct net_device *dev)
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

	RT_TRACE(COMP_SEC, "CamRestoreAllEntry:\n");


	if ((priv->rtllib->pairwise_key_type == KEY_TYPE_WEP40) ||
	    (priv->rtllib->pairwise_key_type == KEY_TYPE_WEP104)) {

		for (EntryId = 0; EntryId < 4; EntryId++) {
			MacAddr = CAM_CONST_ADDR[EntryId];
			if (priv->rtllib->swcamtable[EntryId].bused) {
				setKey(dev, EntryId , EntryId,
				       priv->rtllib->pairwise_key_type, MacAddr,
				       0, (u32 *)(&priv->rtllib->swcamtable
				      [EntryId].key_buf[0]));
			}
		}

	} else if (priv->rtllib->pairwise_key_type == KEY_TYPE_TKIP) {
		if (priv->rtllib->iw_mode == IW_MODE_ADHOC) {
			setKey(dev, 4, 0, priv->rtllib->pairwise_key_type,
			       (u8 *)dev->dev_addr, 0,
			       (u32 *)(&priv->rtllib->swcamtable[4].key_buf[0]));
		} else {
			setKey(dev, 4, 0, priv->rtllib->pairwise_key_type,
			       MacAddr, 0,
			       (u32 *)(&priv->rtllib->swcamtable[4].key_buf[0]));
		}

	} else if (priv->rtllib->pairwise_key_type == KEY_TYPE_CCMP) {
		if (priv->rtllib->iw_mode == IW_MODE_ADHOC) {
			setKey(dev, 4, 0,
			       priv->rtllib->pairwise_key_type,
			       (u8 *)dev->dev_addr, 0,
			       (u32 *)(&priv->rtllib->swcamtable[4].
			       key_buf[0]));
		} else {
			setKey(dev, 4, 0,
			       priv->rtllib->pairwise_key_type, MacAddr,
			       0, (u32 *)(&priv->rtllib->swcamtable[4].
			       key_buf[0]));
			}
	}

	if (priv->rtllib->group_key_type == KEY_TYPE_TKIP) {
		MacAddr = CAM_CONST_BROAD;
		for (EntryId = 1; EntryId < 4; EntryId++) {
			if (priv->rtllib->swcamtable[EntryId].bused) {
				setKey(dev, EntryId, EntryId,
					priv->rtllib->group_key_type,
					MacAddr, 0,
					(u32 *)(&priv->rtllib->swcamtable[EntryId].key_buf[0])
				     );
			}
		}
		if (priv->rtllib->iw_mode == IW_MODE_ADHOC) {
			if (priv->rtllib->swcamtable[0].bused) {
				setKey(dev, 0, 0,
				       priv->rtllib->group_key_type,
				       CAM_CONST_ADDR[0], 0,
				       (u32 *)(&priv->rtllib->swcamtable[0].key_buf[0])
				     );
			} else {
				RT_TRACE(COMP_ERR, "===>%s():ERR!! ADHOC TKIP "
					 ",but 0 entry is have no data\n",
					 __func__);
				return;
			}
		}
	} else if (priv->rtllib->group_key_type == KEY_TYPE_CCMP) {
		MacAddr = CAM_CONST_BROAD;
		for (EntryId = 1; EntryId < 4; EntryId++) {
			if (priv->rtllib->swcamtable[EntryId].bused) {
				setKey(dev, EntryId , EntryId,
				       priv->rtllib->group_key_type,
				       MacAddr, 0,
				       (u32 *)(&priv->rtllib->swcamtable[EntryId].key_buf[0]));
			}
		}

		if (priv->rtllib->iw_mode == IW_MODE_ADHOC) {
			if (priv->rtllib->swcamtable[0].bused) {
				setKey(dev, 0 , 0,
					priv->rtllib->group_key_type,
					CAM_CONST_ADDR[0], 0,
					(u32 *)(&priv->rtllib->swcamtable[0].key_buf[0]));
			} else {
				RT_TRACE(COMP_ERR, "===>%s():ERR!! ADHOC CCMP ,"
					 "but 0 entry is have no data\n",
					 __func__);
				return;
			}
		}
	}
}
