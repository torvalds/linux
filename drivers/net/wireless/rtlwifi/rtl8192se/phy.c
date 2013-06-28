/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
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
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../pci.h"
#include "../ps.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "fw.h"
#include "hw.h"
#include "table.h"

static u32 _rtl92s_phy_calculate_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}

	return i;
}

u32 rtl92s_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 returnvalue = 0, originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, "regaddr(%#x), bitmask(%#x)\n",
		 regaddr, bitmask);

	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	bitshift = _rtl92s_phy_calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, "BBR MASK=0x%x Addr[0x%x]=0x%x\n",
		 bitmask, regaddr, originalvalue);

	return returnvalue;

}

void rtl92s_phy_set_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask,
			   u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		 regaddr, bitmask, data);

	if (bitmask != MASKDWORD) {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = _rtl92s_phy_calculate_bit_shift(bitmask);
		data = ((originalvalue & (~bitmask)) | (data << bitshift));
	}

	rtl_write_dword(rtlpriv, regaddr, data);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		 regaddr, bitmask, data);

}

static u32 _rtl92s_phy_rf_serial_read(struct ieee80211_hw *hw,
				      enum radio_path rfpath, u32 offset)
{

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 newoffset;
	u32 tmplong, tmplong2;
	u8 rfpi_enable = 0;
	u32 retvalue = 0;

	offset &= 0x3f;
	newoffset = offset;

	tmplong = rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD);

	if (rfpath == RF90_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = rtl_get_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD);

	tmplong2 = (tmplong2 & (~BLSSI_READADDRESS)) | (newoffset << 23) |
			BLSSI_READEDGE;

	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD,
		      tmplong & (~BLSSI_READEDGE));

	mdelay(1);

	rtl_set_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD, tmplong2);
	mdelay(1);

	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD, tmplong |
		      BLSSI_READEDGE);
	mdelay(1);

	if (rfpath == RF90_PATH_A)
		rfpi_enable = (u8)rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER1,
						BIT(8));
	else if (rfpath == RF90_PATH_B)
		rfpi_enable = (u8)rtl_get_bbreg(hw, RFPGA0_XB_HSSIPARAMETER1,
						BIT(8));

	if (rfpi_enable)
		retvalue = rtl_get_bbreg(hw, pphyreg->rf_rbpi,
					 BLSSI_READBACK_DATA);
	else
		retvalue = rtl_get_bbreg(hw, pphyreg->rf_rb,
					 BLSSI_READBACK_DATA);

	retvalue = rtl_get_bbreg(hw, pphyreg->rf_rb,
				 BLSSI_READBACK_DATA);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, "RFR-%d Addr[0x%x]=0x%x\n",
		 rfpath, pphyreg->rf_rb, retvalue);

	return retvalue;

}

static void _rtl92s_phy_rf_serial_write(struct ieee80211_hw *hw,
					enum radio_path rfpath, u32 offset,
					u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 data_and_addr = 0;
	u32 newoffset;

	offset &= 0x3f;
	newoffset = offset;

	data_and_addr = ((newoffset << 20) | (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, MASKDWORD, data_and_addr);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, "RFW-%d Addr[0x%x]=0x%x\n",
		 rfpath, pphyreg->rf3wire_offset, data_and_addr);
}


u32 rtl92s_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			    u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		 regaddr, rfpath, bitmask);

	spin_lock(&rtlpriv->locks.rf_lock);

	original_value = _rtl92s_phy_rf_serial_read(hw, rfpath, regaddr);

	bitshift = _rtl92s_phy_calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;

	spin_unlock(&rtlpriv->locks.rf_lock);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x), original_value(%#x)\n",
		 regaddr, rfpath, bitmask, original_value);

	return readback_value;
}

void rtl92s_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 original_value, bitshift;

	if (!((rtlphy->rf_pathmap >> rfpath) & 0x1))
		return;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);

	spin_lock(&rtlpriv->locks.rf_lock);

	if (bitmask != RFREG_OFFSET_MASK) {
		original_value = _rtl92s_phy_rf_serial_read(hw, rfpath,
							    regaddr);
		bitshift = _rtl92s_phy_calculate_bit_shift(bitmask);
		data = ((original_value & (~bitmask)) | (data << bitshift));
	}

	_rtl92s_phy_rf_serial_write(hw, rfpath, regaddr, data);

	spin_unlock(&rtlpriv->locks.rf_lock);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);

}

void rtl92s_phy_scan_operation_backup(struct ieee80211_hw *hw,
				      u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP:
			rtl92s_phy_set_fw_cmd(hw, FW_CMD_PAUSE_DM_BY_SCAN);
			break;
		case SCAN_OPT_RESTORE:
			rtl92s_phy_set_fw_cmd(hw, FW_CMD_RESUME_DM_BY_SCAN);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Unknown operation\n");
			break;
		}
	}
}

void rtl92s_phy_set_bw_mode(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 reg_bw_opmode;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "Switch to %s bandwidth\n",
		 rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20 ?
		 "20MHz" : "40MHz");

	if (rtlphy->set_bwmode_inprogress)
		return;
	if (is_hal_stop(rtlhal))
		return;

	rtlphy->set_bwmode_inprogress = true;

	reg_bw_opmode = rtl_read_byte(rtlpriv, BW_OPMODE);
	/* dummy read */
	rtl_read_byte(rtlpriv, RRSR + 2);

	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		reg_bw_opmode |= BW_OPMODE_20MHZ;
		rtl_write_byte(rtlpriv, BW_OPMODE, reg_bw_opmode);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		reg_bw_opmode &= ~BW_OPMODE_20MHZ;
		rtl_write_byte(rtlpriv, BW_OPMODE, reg_bw_opmode);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;
	}

	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x0);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x0);

		if (rtlhal->version >= VERSION_8192S_BCUT)
			rtl_write_byte(rtlpriv, RFPGA0_ANALOGPARAMETER2, 0x58);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x1);

		rtl_set_bbreg(hw, RCCK0_SYSTEM, BCCK_SIDEBAND,
				(mac->cur_40_prime_sc >> 1));
		rtl_set_bbreg(hw, ROFDM1_LSTF, 0xC00, mac->cur_40_prime_sc);

		if (rtlhal->version >= VERSION_8192S_BCUT)
			rtl_write_byte(rtlpriv, RFPGA0_ANALOGPARAMETER2, 0x18);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;
	}

	rtl92s_phy_rf6052_set_bandwidth(hw, rtlphy->current_chan_bw);
	rtlphy->set_bwmode_inprogress = false;
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "<==\n");
}

static bool _rtl92s_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
		u32 cmdtableidx, u32 cmdtablesz, enum swchnlcmd_id cmdid,
		u32 para1, u32 para2, u32 msdelay)
{
	struct swchnlcmd *pcmd;

	if (cmdtable == NULL) {
		RT_ASSERT(false, "cmdtable cannot be NULL\n");
		return false;
	}

	if (cmdtableidx >= cmdtablesz)
		return false;

	pcmd = cmdtable + cmdtableidx;
	pcmd->cmdid = cmdid;
	pcmd->para1 = para1;
	pcmd->para2 = para2;
	pcmd->msdelay = msdelay;

	return true;
}

static bool _rtl92s_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
	     u8 channel, u8 *stage, u8 *step, u32 *delay)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct swchnlcmd precommoncmd[MAX_PRECMD_CNT];
	u32 precommoncmdcnt;
	struct swchnlcmd postcommoncmd[MAX_POSTCMD_CNT];
	u32 postcommoncmdcnt;
	struct swchnlcmd rfdependcmd[MAX_RFDEPENDCMD_CNT];
	u32 rfdependcmdcnt;
	struct swchnlcmd *currentcmd = NULL;
	u8 rfpath;
	u8 num_total_rfpath = rtlphy->num_total_rfpath;

	precommoncmdcnt = 0;
	_rtl92s_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
			MAX_PRECMD_CNT, CMDID_SET_TXPOWEROWER_LEVEL, 0, 0, 0);
	_rtl92s_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
			MAX_PRECMD_CNT, CMDID_END, 0, 0, 0);

	postcommoncmdcnt = 0;

	_rtl92s_phy_set_sw_chnl_cmdarray(postcommoncmd, postcommoncmdcnt++,
			MAX_POSTCMD_CNT, CMDID_END, 0, 0, 0);

	rfdependcmdcnt = 0;

	RT_ASSERT((channel >= 1 && channel <= 14),
		  "invalid channel for Zebra: %d\n", channel);

	_rtl92s_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					 MAX_RFDEPENDCMD_CNT, CMDID_RF_WRITEREG,
					 RF_CHNLBW, channel, 10);

	_rtl92s_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
			MAX_RFDEPENDCMD_CNT, CMDID_END, 0, 0, 0);

	do {
		switch (*stage) {
		case 0:
			currentcmd = &precommoncmd[*step];
			break;
		case 1:
			currentcmd = &rfdependcmd[*step];
			break;
		case 2:
			currentcmd = &postcommoncmd[*step];
			break;
		}

		if (currentcmd->cmdid == CMDID_END) {
			if ((*stage) == 2) {
				return true;
			} else {
				(*stage)++;
				(*step) = 0;
				continue;
			}
		}

		switch (currentcmd->cmdid) {
		case CMDID_SET_TXPOWEROWER_LEVEL:
			rtl92s_phy_set_txpower(hw, channel);
			break;
		case CMDID_WRITEPORT_ULONG:
			rtl_write_dword(rtlpriv, currentcmd->para1,
					currentcmd->para2);
			break;
		case CMDID_WRITEPORT_USHORT:
			rtl_write_word(rtlpriv, currentcmd->para1,
				       (u16)currentcmd->para2);
			break;
		case CMDID_WRITEPORT_UCHAR:
			rtl_write_byte(rtlpriv, currentcmd->para1,
				       (u8)currentcmd->para2);
			break;
		case CMDID_RF_WRITEREG:
			for (rfpath = 0; rfpath < num_total_rfpath; rfpath++) {
				rtlphy->rfreg_chnlval[rfpath] =
					 ((rtlphy->rfreg_chnlval[rfpath] &
					 0xfffffc00) | currentcmd->para2);
				rtl_set_rfreg(hw, (enum radio_path)rfpath,
					      currentcmd->para1,
					      RFREG_OFFSET_MASK,
					      rtlphy->rfreg_chnlval[rfpath]);
			}
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "switch case not processed\n");
			break;
		}

		break;
	} while (true);

	(*delay) = currentcmd->msdelay;
	(*step)++;
	return false;
}

u8 rtl92s_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 delay;
	bool ret;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "switch to channel%d\n",
		 rtlphy->current_channel);

	if (rtlphy->sw_chnl_inprogress)
		return 0;

	if (rtlphy->set_bwmode_inprogress)
		return 0;

	if (is_hal_stop(rtlhal))
		return 0;

	rtlphy->sw_chnl_inprogress = true;
	rtlphy->sw_chnl_stage = 0;
	rtlphy->sw_chnl_step = 0;

	do {
		if (!rtlphy->sw_chnl_inprogress)
			break;

		ret = _rtl92s_phy_sw_chnl_step_by_step(hw,
				 rtlphy->current_channel,
				 &rtlphy->sw_chnl_stage,
				 &rtlphy->sw_chnl_step, &delay);
		if (!ret) {
			if (delay > 0)
				mdelay(delay);
			else
				continue;
		} else {
			rtlphy->sw_chnl_inprogress = false;
		}
		break;
	} while (true);

	rtlphy->sw_chnl_inprogress = false;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "<==\n");

	return 1;
}

static void _rtl92se_phy_set_rf_sleep(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1btmp;

	u1btmp = rtl_read_byte(rtlpriv, LDOV12D_CTRL);
	u1btmp |= BIT(0);

	rtl_write_byte(rtlpriv, LDOV12D_CTRL, u1btmp);
	rtl_write_byte(rtlpriv, SPS1_CTRL, 0x0);
	rtl_write_byte(rtlpriv, TXPAUSE, 0xFF);
	rtl_write_word(rtlpriv, CMDR, 0x57FC);
	udelay(100);

	rtl_write_word(rtlpriv, CMDR, 0x77FC);
	rtl_write_byte(rtlpriv, PHY_CCA, 0x0);
	udelay(10);

	rtl_write_word(rtlpriv, CMDR, 0x37FC);
	udelay(10);

	rtl_write_word(rtlpriv, CMDR, 0x77FC);
	udelay(10);

	rtl_write_word(rtlpriv, CMDR, 0x57FC);

	/* we should chnge GPIO to input mode
	 * this will drop away current about 25mA*/
	rtl8192se_gpiobit3_cfg_inputmode(hw);
}

bool rtl92s_phy_set_rf_power_state(struct ieee80211_hw *hw,
				   enum rf_pwrstate rfpwr_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool bresult = true;
	u8 i, queue_id;
	struct rtl8192_tx_ring *ring = NULL;

	if (rfpwr_state == ppsc->rfpwr_state)
		return false;

	switch (rfpwr_state) {
	case ERFON:{
			if ((ppsc->rfpwr_state == ERFOFF) &&
			    RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC)) {

				bool rtstatus;
				u32 InitializeCount = 0;
				do {
					InitializeCount++;
					RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
						 "IPS Set eRf nic enable\n");
					rtstatus = rtl_ps_enable_nic(hw);
				} while (!rtstatus && (InitializeCount < 10));

				RT_CLEAR_PS_LEVEL(ppsc,
						  RT_RF_OFF_LEVL_HALT_NIC);
			} else {
				RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
					 "awake, sleeped:%d ms state_inap:%x\n",
					 jiffies_to_msecs(jiffies -
							  ppsc->
							  last_sleep_jiffies),
					 rtlpriv->psc.state_inap);
				ppsc->last_awake_jiffies = jiffies;
				rtl_write_word(rtlpriv, CMDR, 0x37FC);
				rtl_write_byte(rtlpriv, TXPAUSE, 0x00);
				rtl_write_byte(rtlpriv, PHY_CCA, 0x3);
			}

			if (mac->link_state == MAC80211_LINKED)
				rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_LINK);
			else
				rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_NO_LINK);
			break;
		}
	case ERFOFF:{
			if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC) {
				RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
					 "IPS Set eRf nic disable\n");
				rtl_ps_disable_nic(hw);
				RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
			} else {
				if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
					rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_NO_LINK);
				else
					rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_POWER_OFF);
			}
			break;
		}
	case ERFSLEEP:
			if (ppsc->rfpwr_state == ERFOFF)
				return false;

			for (queue_id = 0, i = 0;
			     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
				ring = &pcipriv->dev.tx_ring[queue_id];
				if (skb_queue_len(&ring->queue) == 0 ||
					queue_id == BEACON_QUEUE) {
					queue_id++;
					continue;
				} else {
					RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
						 "eRf Off/Sleep: %d times TcbBusyQueue[%d] = %d before doze!\n",
						 i + 1, queue_id,
						 skb_queue_len(&ring->queue));

					udelay(10);
					i++;
				}

				if (i >= MAX_DOZE_WAITING_TIMES_9x) {
					RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
						 "ERFOFF: %d times TcbBusyQueue[%d] = %d !\n",
						 MAX_DOZE_WAITING_TIMES_9x,
						 queue_id,
						 skb_queue_len(&ring->queue));
					break;
				}
			}

			RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
				 "Set ERFSLEEP awaked:%d ms\n",
				 jiffies_to_msecs(jiffies -
						  ppsc->last_awake_jiffies));

			RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
				 "sleep awaked:%d ms state_inap:%x\n",
				 jiffies_to_msecs(jiffies -
						  ppsc->last_awake_jiffies),
				 rtlpriv->psc.state_inap);
			ppsc->last_sleep_jiffies = jiffies;
			_rtl92se_phy_set_rf_sleep(hw);
	    break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		bresult = false;
		break;
	}

	if (bresult)
		ppsc->rfpwr_state = rfpwr_state;

	return bresult;
}

static bool _rtl92s_phy_config_rfpa_bias_current(struct ieee80211_hw *hw,
						 enum radio_path rfpath)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool rtstatus = true;
	u32 tmpval = 0;

	/* If inferiority IC, we have to increase the PA bias current */
	if (rtlhal->ic_class != IC_INFERIORITY_A) {
		tmpval = rtl92s_phy_query_rf_reg(hw, rfpath, RF_IPA, 0xf);
		rtl92s_phy_set_rf_reg(hw, rfpath, RF_IPA, 0xf, tmpval + 1);
	}

	return rtstatus;
}

static void _rtl92s_store_pwrindex_diffrate_offset(struct ieee80211_hw *hw,
		u32 reg_addr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	int index;

	if (reg_addr == RTXAGC_RATE18_06)
		index = 0;
	else if (reg_addr == RTXAGC_RATE54_24)
		index = 1;
	else if (reg_addr == RTXAGC_CCK_MCS32)
		index = 6;
	else if (reg_addr == RTXAGC_MCS03_MCS00)
		index = 2;
	else if (reg_addr == RTXAGC_MCS07_MCS04)
		index = 3;
	else if (reg_addr == RTXAGC_MCS11_MCS08)
		index = 4;
	else if (reg_addr == RTXAGC_MCS15_MCS12)
		index = 5;
	else
		return;

	rtlphy->mcs_offset[rtlphy->pwrgroup_cnt][index] = data;
	if (index == 5)
		rtlphy->pwrgroup_cnt++;
}

static void _rtl92s_phy_init_register_definition(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	/*RF Interface Sowrtware Control */
	rtlphy->phyreg_def[RF90_PATH_A].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_C].rfintfs = RFPGA0_XCD_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_D].rfintfs = RFPGA0_XCD_RFINTERFACESW;

	/* RF Interface Readback Value */
	rtlphy->phyreg_def[RF90_PATH_A].rfintfi = RFPGA0_XAB_RFINTERFACERB;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfi = RFPGA0_XAB_RFINTERFACERB;
	rtlphy->phyreg_def[RF90_PATH_C].rfintfi = RFPGA0_XCD_RFINTERFACERB;
	rtlphy->phyreg_def[RF90_PATH_D].rfintfi = RFPGA0_XCD_RFINTERFACERB;

	/* RF Interface Output (and Enable) */
	rtlphy->phyreg_def[RF90_PATH_A].rfintfo = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfo = RFPGA0_XB_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_C].rfintfo = RFPGA0_XC_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_D].rfintfo = RFPGA0_XD_RFINTERFACEOE;

	/* RF Interface (Output and)  Enable */
	rtlphy->phyreg_def[RF90_PATH_A].rfintfe = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfe = RFPGA0_XB_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_C].rfintfe = RFPGA0_XC_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_D].rfintfe = RFPGA0_XD_RFINTERFACEOE;

	/* Addr of LSSI. Wirte RF register by driver */
	rtlphy->phyreg_def[RF90_PATH_A].rf3wire_offset =
						 RFPGA0_XA_LSSIPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rf3wire_offset =
						 RFPGA0_XB_LSSIPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_C].rf3wire_offset =
						 RFPGA0_XC_LSSIPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_D].rf3wire_offset =
						 RFPGA0_XD_LSSIPARAMETER;

	/* RF parameter */
	rtlphy->phyreg_def[RF90_PATH_A].rflssi_select = RFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_select = RFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_C].rflssi_select = RFPGA0_XCD_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_D].rflssi_select = RFPGA0_XCD_RFPARAMETER;

	/* Tx AGC Gain Stage (same for all path. Should we remove this?) */
	rtlphy->phyreg_def[RF90_PATH_A].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxgain_stage = RFPGA0_TXGAINSTAGE;

	/* Tranceiver A~D HSSI Parameter-1 */
	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para1 = RFPGA0_XA_HSSIPARAMETER1;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para1 = RFPGA0_XB_HSSIPARAMETER1;
	rtlphy->phyreg_def[RF90_PATH_C].rfhssi_para1 = RFPGA0_XC_HSSIPARAMETER1;
	rtlphy->phyreg_def[RF90_PATH_D].rfhssi_para1 = RFPGA0_XD_HSSIPARAMETER1;

	/* Tranceiver A~D HSSI Parameter-2 */
	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RFPGA0_XA_HSSIPARAMETER2;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RFPGA0_XB_HSSIPARAMETER2;
	rtlphy->phyreg_def[RF90_PATH_C].rfhssi_para2 = RFPGA0_XC_HSSIPARAMETER2;
	rtlphy->phyreg_def[RF90_PATH_D].rfhssi_para2 = RFPGA0_XD_HSSIPARAMETER2;

	/* RF switch Control */
	rtlphy->phyreg_def[RF90_PATH_A].rfsw_ctrl = RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_B].rfsw_ctrl = RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_C].rfsw_ctrl = RFPGA0_XCD_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_D].rfsw_ctrl = RFPGA0_XCD_SWITCHCONTROL;

	/* AGC control 1  */
	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control1 = ROFDM0_XAAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control1 = ROFDM0_XBAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control1 = ROFDM0_XCAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control1 = ROFDM0_XDAGCCORE1;

	/* AGC control 2  */
	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control2 = ROFDM0_XAAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control2 = ROFDM0_XBAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control2 = ROFDM0_XCAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control2 = ROFDM0_XDAGCCORE2;

	/* RX AFE control 1  */
	rtlphy->phyreg_def[RF90_PATH_A].rfrxiq_imbal = ROFDM0_XARXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrxiq_imbal = ROFDM0_XBRXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrxiq_imbal = ROFDM0_XCRXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrxiq_imbal = ROFDM0_XDRXIQIMBALANCE;

	/* RX AFE control 1   */
	rtlphy->phyreg_def[RF90_PATH_A].rfrx_afe = ROFDM0_XARXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrx_afe = ROFDM0_XBRXAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrx_afe = ROFDM0_XCRXAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrx_afe = ROFDM0_XDRXAFE;

	/* Tx AFE control 1  */
	rtlphy->phyreg_def[RF90_PATH_A].rftxiq_imbal = ROFDM0_XATXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxiq_imbal = ROFDM0_XBTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxiq_imbal = ROFDM0_XCTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxiq_imbal = ROFDM0_XDTXIQIMBALANCE;

	/* Tx AFE control 2  */
	rtlphy->phyreg_def[RF90_PATH_A].rftx_afe = ROFDM0_XATXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rftx_afe = ROFDM0_XBTXAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rftx_afe = ROFDM0_XCTXAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rftx_afe = ROFDM0_XDTXAFE;

	/* Tranceiver LSSI Readback */
	rtlphy->phyreg_def[RF90_PATH_A].rf_rb = RFPGA0_XA_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rb = RFPGA0_XB_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_C].rf_rb = RFPGA0_XC_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_D].rf_rb = RFPGA0_XD_LSSIREADBACK;

	/* Tranceiver LSSI Readback PI mode  */
	rtlphy->phyreg_def[RF90_PATH_A].rf_rbpi = TRANSCEIVERA_HSPI_READBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rbpi = TRANSCEIVERB_HSPI_READBACK;
}


static bool _rtl92s_phy_config_bb(struct ieee80211_hw *hw, u8 configtype)
{
	int i;
	u32 *phy_reg_table;
	u32 *agc_table;
	u16 phy_reg_len, agc_len;

	agc_len = AGCTAB_ARRAYLENGTH;
	agc_table = rtl8192seagctab_array;
	/* Default RF_type: 2T2R */
	phy_reg_len = PHY_REG_2T2RARRAYLENGTH;
	phy_reg_table = rtl8192sephy_reg_2t2rarray;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_reg_len; i = i + 2) {
			if (phy_reg_table[i] == 0xfe)
				mdelay(50);
			else if (phy_reg_table[i] == 0xfd)
				mdelay(5);
			else if (phy_reg_table[i] == 0xfc)
				mdelay(1);
			else if (phy_reg_table[i] == 0xfb)
				udelay(50);
			else if (phy_reg_table[i] == 0xfa)
				udelay(5);
			else if (phy_reg_table[i] == 0xf9)
				udelay(1);

			/* Add delay for ECS T20 & LG malow platform, */
			udelay(1);

			rtl92s_phy_set_bb_reg(hw, phy_reg_table[i], MASKDWORD,
					phy_reg_table[i + 1]);
		}
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		for (i = 0; i < agc_len; i = i + 2) {
			rtl92s_phy_set_bb_reg(hw, agc_table[i], MASKDWORD,
					agc_table[i + 1]);

			/* Add delay for ECS T20 & LG malow platform */
			udelay(1);
		}
	}

	return true;
}

static bool _rtl92s_phy_set_bb_to_diff_rf(struct ieee80211_hw *hw,
					  u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 *phy_regarray2xtxr_table;
	u16 phy_regarray2xtxr_len;
	int i;

	if (rtlphy->rf_type == RF_1T1R) {
		phy_regarray2xtxr_table = rtl8192sephy_changeto_1t1rarray;
		phy_regarray2xtxr_len = PHY_CHANGETO_1T1RARRAYLENGTH;
	} else if (rtlphy->rf_type == RF_1T2R) {
		phy_regarray2xtxr_table = rtl8192sephy_changeto_1t2rarray;
		phy_regarray2xtxr_len = PHY_CHANGETO_1T2RARRAYLENGTH;
	} else {
		return false;
	}

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_regarray2xtxr_len; i = i + 3) {
			if (phy_regarray2xtxr_table[i] == 0xfe)
				mdelay(50);
			else if (phy_regarray2xtxr_table[i] == 0xfd)
				mdelay(5);
			else if (phy_regarray2xtxr_table[i] == 0xfc)
				mdelay(1);
			else if (phy_regarray2xtxr_table[i] == 0xfb)
				udelay(50);
			else if (phy_regarray2xtxr_table[i] == 0xfa)
				udelay(5);
			else if (phy_regarray2xtxr_table[i] == 0xf9)
				udelay(1);

			rtl92s_phy_set_bb_reg(hw, phy_regarray2xtxr_table[i],
				phy_regarray2xtxr_table[i + 1],
				phy_regarray2xtxr_table[i + 2]);
		}
	}

	return true;
}

static bool _rtl92s_phy_config_bb_with_pg(struct ieee80211_hw *hw,
					  u8 configtype)
{
	int i;
	u32 *phy_table_pg;
	u16 phy_pg_len;

	phy_pg_len = PHY_REG_ARRAY_PGLENGTH;
	phy_table_pg = rtl8192sephy_reg_array_pg;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_pg_len; i = i + 3) {
			if (phy_table_pg[i] == 0xfe)
				mdelay(50);
			else if (phy_table_pg[i] == 0xfd)
				mdelay(5);
			else if (phy_table_pg[i] == 0xfc)
				mdelay(1);
			else if (phy_table_pg[i] == 0xfb)
				udelay(50);
			else if (phy_table_pg[i] == 0xfa)
				udelay(5);
			else if (phy_table_pg[i] == 0xf9)
				udelay(1);

			_rtl92s_store_pwrindex_diffrate_offset(hw,
					phy_table_pg[i],
					phy_table_pg[i + 1],
					phy_table_pg[i + 2]);
			rtl92s_phy_set_bb_reg(hw, phy_table_pg[i],
					phy_table_pg[i + 1],
					phy_table_pg[i + 2]);
		}
	}

	return true;
}

static bool _rtl92s_phy_bb_config_parafile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus = true;

	/* 1. Read PHY_REG.TXT BB INIT!! */
	/* We will separate as 1T1R/1T2R/1T2R_GREEN/2T2R */
	if (rtlphy->rf_type == RF_1T2R || rtlphy->rf_type == RF_2T2R ||
	    rtlphy->rf_type == RF_1T1R || rtlphy->rf_type == RF_2T2R_GREEN) {
		rtstatus = _rtl92s_phy_config_bb(hw, BASEBAND_CONFIG_PHY_REG);

		if (rtlphy->rf_type != RF_2T2R &&
		    rtlphy->rf_type != RF_2T2R_GREEN)
			/* so we should reconfig BB reg with the right
			 * PHY parameters. */
			rtstatus = _rtl92s_phy_set_bb_to_diff_rf(hw,
						BASEBAND_CONFIG_PHY_REG);
	} else {
		rtstatus = false;
	}

	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
			 "Write BB Reg Fail!!\n");
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	/* 2. If EEPROM or EFUSE autoload OK, We must config by
	 *    PHY_REG_PG.txt */
	if (rtlefuse->autoload_failflag == false) {
		rtlphy->pwrgroup_cnt = 0;

		rtstatus = _rtl92s_phy_config_bb_with_pg(hw,
						 BASEBAND_CONFIG_PHY_REG);
	}
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
			 "_rtl92s_phy_bb_config_parafile(): BB_PG Reg Fail!!\n");
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	/* 3. BB AGC table Initialization */
	rtstatus = _rtl92s_phy_config_bb(hw, BASEBAND_CONFIG_AGC_TAB);

	if (!rtstatus) {
		pr_err("%s(): AGC Table Fail\n", __func__);
		goto phy_BB8190_Config_ParaFile_Fail;
	}

	/* Check if the CCK HighPower is turned ON. */
	/* This is used to calculate PWDB. */
	rtlphy->cck_high_power = (bool)(rtl92s_phy_query_bb_reg(hw,
			RFPGA0_XA_HSSIPARAMETER2, 0x200));

phy_BB8190_Config_ParaFile_Fail:
	return rtstatus;
}

u8 rtl92s_phy_config_rf(struct ieee80211_hw *hw, enum radio_path rfpath)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	int i;
	bool rtstatus = true;
	u32 *radio_a_table;
	u32 *radio_b_table;
	u16 radio_a_tblen, radio_b_tblen;

	radio_a_tblen = RADIOA_1T_ARRAYLENGTH;
	radio_a_table = rtl8192seradioa_1t_array;

	/* Using Green mode array table for RF_2T2R_GREEN */
	if (rtlphy->rf_type == RF_2T2R_GREEN) {
		radio_b_table = rtl8192seradiob_gm_array;
		radio_b_tblen = RADIOB_GM_ARRAYLENGTH;
	} else {
		radio_b_table = rtl8192seradiob_array;
		radio_b_tblen = RADIOB_ARRAYLENGTH;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
	rtstatus = true;

	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radio_a_tblen; i = i + 2) {
			if (radio_a_table[i] == 0xfe)
				/* Delay specific ms. Only RF configuration
				 * requires delay. */
				mdelay(50);
			else if (radio_a_table[i] == 0xfd)
				mdelay(5);
			else if (radio_a_table[i] == 0xfc)
				mdelay(1);
			else if (radio_a_table[i] == 0xfb)
				udelay(50);
			else if (radio_a_table[i] == 0xfa)
				udelay(5);
			else if (radio_a_table[i] == 0xf9)
				udelay(1);
			else
				rtl92s_phy_set_rf_reg(hw, rfpath,
						      radio_a_table[i],
						      MASK20BITS,
						      radio_a_table[i + 1]);

			/* Add delay for ECS T20 & LG malow platform */
			udelay(1);
		}

		/* PA Bias current for inferiority IC */
		_rtl92s_phy_config_rfpa_bias_current(hw, rfpath);
		break;
	case RF90_PATH_B:
		for (i = 0; i < radio_b_tblen; i = i + 2) {
			if (radio_b_table[i] == 0xfe)
				/* Delay specific ms. Only RF configuration
				 * requires delay.*/
				mdelay(50);
			else if (radio_b_table[i] == 0xfd)
				mdelay(5);
			else if (radio_b_table[i] == 0xfc)
				mdelay(1);
			else if (radio_b_table[i] == 0xfb)
				udelay(50);
			else if (radio_b_table[i] == 0xfa)
				udelay(5);
			else if (radio_b_table[i] == 0xf9)
				udelay(1);
			else
				rtl92s_phy_set_rf_reg(hw, rfpath,
						      radio_b_table[i],
						      MASK20BITS,
						      radio_b_table[i + 1]);

			/* Add delay for ECS T20 & LG malow platform */
			udelay(1);
		}
		break;
	case RF90_PATH_C:
		;
		break;
	case RF90_PATH_D:
		;
		break;
	default:
		break;
	}

	return rtstatus;
}


bool rtl92s_phy_mac_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;
	u32 arraylength;
	u32 *ptraArray;

	arraylength = MAC_2T_ARRAYLENGTH;
	ptraArray = rtl8192semac_2t_array;

	for (i = 0; i < arraylength; i = i + 2)
		rtl_write_byte(rtlpriv, ptraArray[i], (u8)ptraArray[i + 1]);

	return true;
}


bool rtl92s_phy_bb_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	bool rtstatus = true;
	u8 pathmap, index, rf_num = 0;
	u8 path1, path2;

	_rtl92s_phy_init_register_definition(hw);

	/* Config BB and AGC */
	rtstatus = _rtl92s_phy_bb_config_parafile(hw);


	/* Check BB/RF confiuration setting. */
	/* We only need to configure RF which is turned on. */
	path1 = (u8)(rtl92s_phy_query_bb_reg(hw, RFPGA0_TXINFO, 0xf));
	mdelay(10);
	path2 = (u8)(rtl92s_phy_query_bb_reg(hw, ROFDM0_TRXPATHENABLE, 0xf));
	pathmap = path1 | path2;

	rtlphy->rf_pathmap = pathmap;
	for (index = 0; index < 4; index++) {
		if ((pathmap >> index) & 0x1)
			rf_num++;
	}

	if ((rtlphy->rf_type == RF_1T1R && rf_num != 1) ||
	    (rtlphy->rf_type == RF_1T2R && rf_num != 2) ||
	    (rtlphy->rf_type == RF_2T2R && rf_num != 2) ||
	    (rtlphy->rf_type == RF_2T2R_GREEN && rf_num != 2)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
			 "RF_Type(%x) does not match RF_Num(%x)!!\n",
			 rtlphy->rf_type, rf_num);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
			 "path1 0x%x, path2 0x%x, pathmap 0x%x\n",
			 path1, path2, pathmap);
	}

	return rtstatus;
}

bool rtl92s_phy_rf_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	/* Initialize general global value */
	if (rtlphy->rf_type == RF_1T1R)
		rtlphy->num_total_rfpath = 1;
	else
		rtlphy->num_total_rfpath = 2;

	/* Config BB and RF */
	return rtl92s_phy_rf6052_config(hw);
}

void rtl92s_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	/* read rx initial gain */
	rtlphy->default_initialgain[0] = rtl_get_bbreg(hw,
			ROFDM0_XAAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[1] = rtl_get_bbreg(hw,
			ROFDM0_XBAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[2] = rtl_get_bbreg(hw,
			ROFDM0_XCAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[3] = rtl_get_bbreg(hw,
			ROFDM0_XDAGCCORE1, MASKBYTE0);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x)\n",
		 rtlphy->default_initialgain[0],
		 rtlphy->default_initialgain[1],
		 rtlphy->default_initialgain[2],
		 rtlphy->default_initialgain[3]);

	/* read framesync */
	rtlphy->framesync = rtl_get_bbreg(hw, ROFDM0_RXDETECTOR3, MASKBYTE0);
	rtlphy->framesync_c34 = rtl_get_bbreg(hw, ROFDM0_RXDETECTOR2,
					      MASKDWORD);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Default framesync (0x%x) = 0x%x\n",
		 ROFDM0_RXDETECTOR3, rtlphy->framesync);

}

static void _rtl92s_phy_get_txpower_index(struct ieee80211_hw *hw, u8 channel,
					  u8 *cckpowerlevel, u8 *ofdmpowerLevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = (channel - 1);

	/* 1. CCK */
	/* RF-A */
	cckpowerlevel[0] = rtlefuse->txpwrlevel_cck[0][index];
	/* RF-B */
	cckpowerlevel[1] = rtlefuse->txpwrlevel_cck[1][index];

	/* 2. OFDM for 1T or 2T */
	if (rtlphy->rf_type == RF_1T2R || rtlphy->rf_type == RF_1T1R) {
		/* Read HT 40 OFDM TX power */
		ofdmpowerLevel[0] = rtlefuse->txpwrlevel_ht40_1s[0][index];
		ofdmpowerLevel[1] = rtlefuse->txpwrlevel_ht40_1s[1][index];
	} else if (rtlphy->rf_type == RF_2T2R) {
		/* Read HT 40 OFDM TX power */
		ofdmpowerLevel[0] = rtlefuse->txpwrlevel_ht40_2s[0][index];
		ofdmpowerLevel[1] = rtlefuse->txpwrlevel_ht40_2s[1][index];
	} else {
		ofdmpowerLevel[0] = 0;
		ofdmpowerLevel[1] = 0;
	}
}

static void _rtl92s_phy_ccxpower_indexcheck(struct ieee80211_hw *hw,
		u8 channel, u8 *cckpowerlevel, u8 *ofdmpowerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	rtlphy->cur_cck_txpwridx = cckpowerlevel[0];
	rtlphy->cur_ofdm24g_txpwridx = ofdmpowerlevel[0];
}

void rtl92s_phy_set_txpower(struct ieee80211_hw *hw, u8	channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	/* [0]:RF-A, [1]:RF-B */
	u8 cckpowerlevel[2], ofdmpowerLevel[2];

	if (!rtlefuse->txpwr_fromeprom)
		return;

	/* Mainly we use RF-A Tx Power to write the Tx Power registers,
	 * but the RF-B Tx Power must be calculated by the antenna diff.
	 * So we have to rewrite Antenna gain offset register here.
	 * Please refer to BB register 0x80c
	 * 1. For CCK.
	 * 2. For OFDM 1T or 2T */
	_rtl92s_phy_get_txpower_index(hw, channel, &cckpowerlevel[0],
			&ofdmpowerLevel[0]);

	RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
		 "Channel-%d, cckPowerLevel (A / B) = 0x%x / 0x%x, ofdmPowerLevel (A / B) = 0x%x / 0x%x\n",
		 channel, cckpowerlevel[0], cckpowerlevel[1],
		 ofdmpowerLevel[0], ofdmpowerLevel[1]);

	_rtl92s_phy_ccxpower_indexcheck(hw, channel, &cckpowerlevel[0],
			&ofdmpowerLevel[0]);

	rtl92s_phy_rf6052_set_ccktxpower(hw, cckpowerlevel[0]);
	rtl92s_phy_rf6052_set_ofdmtxpower(hw, &ofdmpowerLevel[0], channel);

}

void rtl92s_phy_chk_fwcmd_iodone(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 pollingcnt = 10000;
	u32 tmpvalue;

	/* Make sure that CMD IO has be accepted by FW. */
	do {
		udelay(10);

		tmpvalue = rtl_read_dword(rtlpriv, WFM5);
		if (tmpvalue == 0)
			break;
	} while (--pollingcnt);

	if (pollingcnt == 0)
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Set FW Cmd fail!!\n");
}


static void _rtl92s_phy_set_fwcmd_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 input, current_aid = 0;

	if (is_hal_stop(rtlhal))
		return;

	if (hal_get_firmwareversion(rtlpriv) < 0x34)
		goto skip;
	/* We re-map RA related CMD IO to combinational ones */
	/* if FW version is v.52 or later. */
	switch (rtlhal->current_fwcmd_io) {
	case FW_CMD_RA_REFRESH_N:
		rtlhal->current_fwcmd_io = FW_CMD_RA_REFRESH_N_COMB;
		break;
	case FW_CMD_RA_REFRESH_BG:
		rtlhal->current_fwcmd_io = FW_CMD_RA_REFRESH_BG_COMB;
		break;
	default:
		break;
	}

skip:
	switch (rtlhal->current_fwcmd_io) {
	case FW_CMD_RA_RESET:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG, "FW_CMD_RA_RESET\n");
		rtl_write_dword(rtlpriv, WFM5, FW_RA_RESET);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;
	case FW_CMD_RA_ACTIVE:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG, "FW_CMD_RA_ACTIVE\n");
		rtl_write_dword(rtlpriv, WFM5, FW_RA_ACTIVE);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;
	case FW_CMD_RA_REFRESH_N:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG, "FW_CMD_RA_REFRESH_N\n");
		input = FW_RA_REFRESH;
		rtl_write_dword(rtlpriv, WFM5, input);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		rtl_write_dword(rtlpriv, WFM5, FW_RA_ENABLE_RSSI_MASK);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;
	case FW_CMD_RA_REFRESH_BG:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG,
			 "FW_CMD_RA_REFRESH_BG\n");
		rtl_write_dword(rtlpriv, WFM5, FW_RA_REFRESH);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		rtl_write_dword(rtlpriv, WFM5, FW_RA_DISABLE_RSSI_MASK);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;
	case FW_CMD_RA_REFRESH_N_COMB:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG,
			 "FW_CMD_RA_REFRESH_N_COMB\n");
		input = FW_RA_IOT_N_COMB;
		rtl_write_dword(rtlpriv, WFM5, input);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;
	case FW_CMD_RA_REFRESH_BG_COMB:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG,
			 "FW_CMD_RA_REFRESH_BG_COMB\n");
		input = FW_RA_IOT_BG_COMB;
		rtl_write_dword(rtlpriv, WFM5, input);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;
	case FW_CMD_IQK_ENABLE:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG, "FW_CMD_IQK_ENABLE\n");
		rtl_write_dword(rtlpriv, WFM5, FW_IQK_ENABLE);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;
	case FW_CMD_PAUSE_DM_BY_SCAN:
		/* Lower initial gain */
		rtl_set_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0, 0x17);
		rtl_set_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0, 0x17);
		/* CCA threshold */
		rtl_set_bbreg(hw, RCCK0_CCA, MASKBYTE2, 0x40);
		break;
	case FW_CMD_RESUME_DM_BY_SCAN:
		/* CCA threshold */
		rtl_set_bbreg(hw, RCCK0_CCA, MASKBYTE2, 0xcd);
		rtl92s_phy_set_txpower(hw, rtlphy->current_channel);
		break;
	case FW_CMD_HIGH_PWR_DISABLE:
		if (rtlpriv->dm.dm_flag & HAL_DM_HIPWR_DISABLE)
			break;

		/* Lower initial gain */
		rtl_set_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0, 0x17);
		rtl_set_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0, 0x17);
		/* CCA threshold */
		rtl_set_bbreg(hw, RCCK0_CCA, MASKBYTE2, 0x40);
		break;
	case FW_CMD_HIGH_PWR_ENABLE:
		if ((rtlpriv->dm.dm_flag & HAL_DM_HIPWR_DISABLE) ||
			rtlpriv->dm.dynamic_txpower_enable)
			break;

		/* CCA threshold */
		rtl_set_bbreg(hw, RCCK0_CCA, MASKBYTE2, 0xcd);
		break;
	case FW_CMD_LPS_ENTER:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG, "FW_CMD_LPS_ENTER\n");
		current_aid = rtlpriv->mac80211.assoc_id;
		rtl_write_dword(rtlpriv, WFM5, (FW_LPS_ENTER |
				((current_aid | 0xc000) << 8)));
		rtl92s_phy_chk_fwcmd_iodone(hw);
		/* FW set TXOP disable here, so disable EDCA
		 * turbo mode until driver leave LPS */
		break;
	case FW_CMD_LPS_LEAVE:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG, "FW_CMD_LPS_LEAVE\n");
		rtl_write_dword(rtlpriv, WFM5, FW_LPS_LEAVE);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;
	case FW_CMD_ADD_A2_ENTRY:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG, "FW_CMD_ADD_A2_ENTRY\n");
		rtl_write_dword(rtlpriv, WFM5, FW_ADD_A2_ENTRY);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;
	case FW_CMD_CTRL_DM_BY_DRIVER:
		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
			 "FW_CMD_CTRL_DM_BY_DRIVER\n");
		rtl_write_dword(rtlpriv, WFM5, FW_CTRL_DM_BY_DRIVER);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		break;

	default:
		break;
	}

	rtl92s_phy_chk_fwcmd_iodone(hw);

	/* Clear FW CMD operation flag. */
	rtlhal->set_fwcmd_inprogress = false;
}

bool rtl92s_phy_set_fw_cmd(struct ieee80211_hw *hw, enum fwcmd_iotype fw_cmdio)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *digtable = &rtlpriv->dm_digtable;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u32	fw_param = FW_CMD_IO_PARA_QUERY(rtlpriv);
	u16	fw_cmdmap = FW_CMD_IO_QUERY(rtlpriv);
	bool postprocessing = false;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
		 "Set FW Cmd(%#x), set_fwcmd_inprogress(%d)\n",
		 fw_cmdio, rtlhal->set_fwcmd_inprogress);

	do {
		/* We re-map to combined FW CMD ones if firmware version */
		/* is v.53 or later. */
		if (hal_get_firmwareversion(rtlpriv) >= 0x35) {
			switch (fw_cmdio) {
			case FW_CMD_RA_REFRESH_N:
				fw_cmdio = FW_CMD_RA_REFRESH_N_COMB;
				break;
			case FW_CMD_RA_REFRESH_BG:
				fw_cmdio = FW_CMD_RA_REFRESH_BG_COMB;
				break;
			default:
				break;
			}
		} else {
			if ((fw_cmdio == FW_CMD_IQK_ENABLE) ||
			    (fw_cmdio == FW_CMD_RA_REFRESH_N) ||
			    (fw_cmdio == FW_CMD_RA_REFRESH_BG)) {
				postprocessing = true;
				break;
			}
		}

		/* If firmware version is v.62 or later,
		 * use FW_CMD_IO_SET for FW_CMD_CTRL_DM_BY_DRIVER */
		if (hal_get_firmwareversion(rtlpriv) >= 0x3E) {
			if (fw_cmdio == FW_CMD_CTRL_DM_BY_DRIVER)
				fw_cmdio = FW_CMD_CTRL_DM_BY_DRIVER_NEW;
		}


		/* We shall revise all FW Cmd IO into Reg0x364
		 * DM map table in the future. */
		switch (fw_cmdio) {
		case FW_CMD_RA_INIT:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "RA init!!\n");
			fw_cmdmap |= FW_RA_INIT_CTL;
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			/* Clear control flag to sync with FW. */
			FW_CMD_IO_CLR(rtlpriv, FW_RA_INIT_CTL);
			break;
		case FW_CMD_DIG_DISABLE:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 "Set DIG disable!!\n");
			fw_cmdmap &= ~FW_DIG_ENABLE_CTL;
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			break;
		case FW_CMD_DIG_ENABLE:
		case FW_CMD_DIG_RESUME:
			if (!(rtlpriv->dm.dm_flag & HAL_DM_DIG_DISABLE)) {
				RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
					 "Set DIG enable or resume!!\n");
				fw_cmdmap |= (FW_DIG_ENABLE_CTL | FW_SS_CTL);
				FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			}
			break;
		case FW_CMD_DIG_HALT:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 "Set DIG halt!!\n");
			fw_cmdmap &= ~(FW_DIG_ENABLE_CTL | FW_SS_CTL);
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			break;
		case FW_CMD_TXPWR_TRACK_THERMAL: {
			u8	thermalval = 0;
			fw_cmdmap |= FW_PWR_TRK_CTL;

			/* Clear FW parameter in terms of thermal parts. */
			fw_param &= FW_PWR_TRK_PARAM_CLR;

			thermalval = rtlpriv->dm.thermalvalue;
			fw_param |= ((thermalval << 24) |
				     (rtlefuse->thermalmeter[0] << 16));

			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 "Set TxPwr tracking!! FwCmdMap(%#x), FwParam(%#x)\n",
				 fw_cmdmap, fw_param);

			FW_CMD_PARA_SET(rtlpriv, fw_param);
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);

			/* Clear control flag to sync with FW. */
			FW_CMD_IO_CLR(rtlpriv, FW_PWR_TRK_CTL);
			}
			break;
		/* The following FW CMDs are only compatible to
		 * v.53 or later. */
		case FW_CMD_RA_REFRESH_N_COMB:
			fw_cmdmap |= FW_RA_N_CTL;

			/* Clear RA BG mode control. */
			fw_cmdmap &= ~(FW_RA_BG_CTL | FW_RA_INIT_CTL);

			/* Clear FW parameter in terms of RA parts. */
			fw_param &= FW_RA_PARAM_CLR;

			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 "[FW CMD] [New Version] Set RA/IOT Comb in n mode!! FwCmdMap(%#x), FwParam(%#x)\n",
				 fw_cmdmap, fw_param);

			FW_CMD_PARA_SET(rtlpriv, fw_param);
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);

			/* Clear control flag to sync with FW. */
			FW_CMD_IO_CLR(rtlpriv, FW_RA_N_CTL);
			break;
		case FW_CMD_RA_REFRESH_BG_COMB:
			fw_cmdmap |= FW_RA_BG_CTL;

			/* Clear RA n-mode control. */
			fw_cmdmap &= ~(FW_RA_N_CTL | FW_RA_INIT_CTL);
			/* Clear FW parameter in terms of RA parts. */
			fw_param &= FW_RA_PARAM_CLR;

			FW_CMD_PARA_SET(rtlpriv, fw_param);
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);

			/* Clear control flag to sync with FW. */
			FW_CMD_IO_CLR(rtlpriv, FW_RA_BG_CTL);
			break;
		case FW_CMD_IQK_ENABLE:
			fw_cmdmap |= FW_IQK_CTL;
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			/* Clear control flag to sync with FW. */
			FW_CMD_IO_CLR(rtlpriv, FW_IQK_CTL);
			break;
		/* The following FW CMD is compatible to v.62 or later.  */
		case FW_CMD_CTRL_DM_BY_DRIVER_NEW:
			fw_cmdmap |= FW_DRIVER_CTRL_DM_CTL;
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			break;
		/*  The followed FW Cmds needs post-processing later. */
		case FW_CMD_RESUME_DM_BY_SCAN:
			fw_cmdmap |= (FW_DIG_ENABLE_CTL |
				      FW_HIGH_PWR_ENABLE_CTL |
				      FW_SS_CTL);

			if (rtlpriv->dm.dm_flag & HAL_DM_DIG_DISABLE ||
				!digtable->dig_enable_flag)
				fw_cmdmap &= ~FW_DIG_ENABLE_CTL;

			if ((rtlpriv->dm.dm_flag & HAL_DM_HIPWR_DISABLE) ||
			    rtlpriv->dm.dynamic_txpower_enable)
				fw_cmdmap &= ~FW_HIGH_PWR_ENABLE_CTL;

			if ((digtable->dig_ext_port_stage ==
			    DIG_EXT_PORT_STAGE_0) ||
			    (digtable->dig_ext_port_stage ==
			    DIG_EXT_PORT_STAGE_1))
				fw_cmdmap &= ~FW_DIG_ENABLE_CTL;

			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			postprocessing = true;
			break;
		case FW_CMD_PAUSE_DM_BY_SCAN:
			fw_cmdmap &= ~(FW_DIG_ENABLE_CTL |
				       FW_HIGH_PWR_ENABLE_CTL |
				       FW_SS_CTL);
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			postprocessing = true;
			break;
		case FW_CMD_HIGH_PWR_DISABLE:
			fw_cmdmap &= ~FW_HIGH_PWR_ENABLE_CTL;
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			postprocessing = true;
			break;
		case FW_CMD_HIGH_PWR_ENABLE:
			if (!(rtlpriv->dm.dm_flag & HAL_DM_HIPWR_DISABLE) &&
			    !rtlpriv->dm.dynamic_txpower_enable) {
				fw_cmdmap |= (FW_HIGH_PWR_ENABLE_CTL |
					      FW_SS_CTL);
				FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
				postprocessing = true;
			}
			break;
		case FW_CMD_DIG_MODE_FA:
			fw_cmdmap |= FW_FA_CTL;
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			break;
		case FW_CMD_DIG_MODE_SS:
			fw_cmdmap &= ~FW_FA_CTL;
			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			break;
		case FW_CMD_PAPE_CONTROL:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 "[FW CMD] Set PAPE Control\n");
			fw_cmdmap &= ~FW_PAPE_CTL_BY_SW_HW;

			FW_CMD_IO_SET(rtlpriv, fw_cmdmap);
			break;
		default:
			/* Pass to original FW CMD processing callback
			 * routine. */
			postprocessing = true;
			break;
		}
	} while (false);

	/* We shall post processing these FW CMD if
	 * variable postprocessing is set.
	 */
	if (postprocessing && !rtlhal->set_fwcmd_inprogress) {
		rtlhal->set_fwcmd_inprogress = true;
		/* Update current FW Cmd for callback use. */
		rtlhal->current_fwcmd_io = fw_cmdio;
	} else {
		return false;
	}

	_rtl92s_phy_set_fwcmd_io(hw);
	return true;
}

static	void _rtl92s_phy_check_ephy_switchready(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32	delay = 100;
	u8	regu1;

	regu1 = rtl_read_byte(rtlpriv, 0x554);
	while ((regu1 & BIT(5)) && (delay > 0)) {
		regu1 = rtl_read_byte(rtlpriv, 0x554);
		delay--;
		/* We delay only 50us to prevent
		 * being scheduled out. */
		udelay(50);
	}
}

void rtl92s_phy_switch_ephy_parameter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	/* The way to be capable to switch clock request
	 * when the PG setting does not support clock request.
	 * This is the backdoor solution to switch clock
	 * request before ASPM or D3. */
	rtl_write_dword(rtlpriv, 0x540, 0x73c11);
	rtl_write_dword(rtlpriv, 0x548, 0x2407c);

	/* Switch EPHY parameter!!!! */
	rtl_write_word(rtlpriv, 0x550, 0x1000);
	rtl_write_byte(rtlpriv, 0x554, 0x20);
	_rtl92s_phy_check_ephy_switchready(hw);

	rtl_write_word(rtlpriv, 0x550, 0xa0eb);
	rtl_write_byte(rtlpriv, 0x554, 0x3e);
	_rtl92s_phy_check_ephy_switchready(hw);

	rtl_write_word(rtlpriv, 0x550, 0xff80);
	rtl_write_byte(rtlpriv, 0x554, 0x39);
	_rtl92s_phy_check_ephy_switchready(hw);

	/* Delay L1 enter time */
	if (ppsc->support_aspm && !ppsc->support_backdoor)
		rtl_write_byte(rtlpriv, 0x560, 0x40);
	else
		rtl_write_byte(rtlpriv, 0x560, 0x00);

}

void rtl92s_phy_set_beacon_hwreg(struct ieee80211_hw *hw, u16 beaconinterval)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 new_bcn_num = 0;

	if (hal_get_firmwareversion(rtlpriv) >= 0x33) {
		/* Fw v.51 and later. */
		rtl_write_dword(rtlpriv, WFM5, 0xF1000000 |
				(beaconinterval << 8));
	} else {
		new_bcn_num = beaconinterval * 32 - 64;
		rtl_write_dword(rtlpriv, WFM3 + 4, new_bcn_num);
		rtl_write_dword(rtlpriv, WFM3, 0xB026007C);
	}
}
