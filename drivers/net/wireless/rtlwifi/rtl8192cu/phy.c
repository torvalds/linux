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
#include "../core.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "table.h"

u32 rtl92cu_phy_query_rf_reg(struct ieee80211_hw *hw,
			     enum radio_path rfpath, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		 regaddr, rfpath, bitmask);
	if (rtlphy->rf_mode != RF_OP_BY_FW) {
		original_value = _rtl92c_phy_rf_serial_read(hw,
							    rfpath, regaddr);
	} else {
		original_value = _rtl92c_phy_fw_rf_serial_read(hw,
							       rfpath, regaddr);
	}
	bitshift = _rtl92c_phy_calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x), original_value(%#x)\n",
		 regaddr, rfpath, bitmask, original_value);
	return readback_value;
}

void rtl92cu_phy_set_rf_reg(struct ieee80211_hw *hw,
			    enum radio_path rfpath,
			    u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 original_value, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);
	if (rtlphy->rf_mode != RF_OP_BY_FW) {
		if (bitmask != RFREG_OFFSET_MASK) {
			original_value = _rtl92c_phy_rf_serial_read(hw,
								    rfpath,
								    regaddr);
			bitshift = _rtl92c_phy_calculate_bit_shift(bitmask);
			data =
			    ((original_value & (~bitmask)) |
			     (data << bitshift));
		}
		_rtl92c_phy_rf_serial_write(hw, rfpath, regaddr, data);
	} else {
		if (bitmask != RFREG_OFFSET_MASK) {
			original_value = _rtl92c_phy_fw_rf_serial_read(hw,
								       rfpath,
								       regaddr);
			bitshift = _rtl92c_phy_calculate_bit_shift(bitmask);
			data =
			    ((original_value & (~bitmask)) |
			     (data << bitshift));
		}
		_rtl92c_phy_fw_rf_serial_write(hw, rfpath, regaddr, data);
	}
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);
}

bool rtl92cu_phy_mac_config(struct ieee80211_hw *hw)
{
	bool rtstatus;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool is92c = IS_92C_SERIAL(rtlhal->version);

	rtstatus = _rtl92cu_phy_config_mac_with_headerfile(hw);
	if (is92c && IS_HARDWARE_TYPE_8192CE(rtlhal))
		rtl_write_byte(rtlpriv, 0x14, 0x71);
	return rtstatus;
}

bool rtl92cu_phy_bb_config(struct ieee80211_hw *hw)
{
	bool rtstatus = true;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u16 regval;
	u32 regval32;
	u8 b_reg_hwparafile = 1;

	_rtl92c_phy_init_bb_rf_register_definition(hw);
	regval = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, regval | BIT(13) |
		       BIT(0) | BIT(1));
	rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL, 0x83);
	rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL + 1, 0xdb);
	rtl_write_byte(rtlpriv, REG_RF_CTRL, RF_EN | RF_RSTB | RF_SDMRSTB);
	if (IS_HARDWARE_TYPE_8192CE(rtlhal)) {
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, FEN_PPLL | FEN_PCIEA |
			       FEN_DIO_PCIE |	FEN_BB_GLB_RSTn | FEN_BBRSTB);
	} else if (IS_HARDWARE_TYPE_8192CU(rtlhal)) {
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, FEN_USBA | FEN_USBD |
			       FEN_BB_GLB_RSTn | FEN_BBRSTB);
	}
	regval32 = rtl_read_dword(rtlpriv, 0x87c);
	rtl_write_dword(rtlpriv, 0x87c, regval32 & (~BIT(31)));
	if (IS_HARDWARE_TYPE_8192CU(rtlhal))
		rtl_write_byte(rtlpriv, REG_LDOHCI12_CTRL, 0x0f);
	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL + 1, 0x80);
	if (b_reg_hwparafile == 1)
		rtstatus = _rtl92c_phy_bb8192c_config_parafile(hw);
	return rtstatus;
}

bool _rtl92cu_phy_config_mac_with_headerfile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 i;
	u32 arraylength;
	u32 *ptrarray;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Read Rtl819XMACPHY_Array\n");
	arraylength =  rtlphy->hwparam_tables[MAC_REG].length ;
	ptrarray = rtlphy->hwparam_tables[MAC_REG].pdata;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Img:RTL8192CEMAC_2T_ARRAY\n");
	for (i = 0; i < arraylength; i = i + 2)
		rtl_write_byte(rtlpriv, ptrarray[i], (u8) ptrarray[i + 1]);
	return true;
}

bool _rtl92cu_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
					    u8 configtype)
{
	int i;
	u32 *phy_regarray_table;
	u32 *agctab_array_table;
	u16 phy_reg_arraylen, agctab_arraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	if (IS_92C_SERIAL(rtlhal->version)) {
		agctab_arraylen = rtlphy->hwparam_tables[AGCTAB_2T].length;
		agctab_array_table =  rtlphy->hwparam_tables[AGCTAB_2T].pdata;
		phy_reg_arraylen = rtlphy->hwparam_tables[PHY_REG_2T].length;
		phy_regarray_table = rtlphy->hwparam_tables[PHY_REG_2T].pdata;
	} else {
		agctab_arraylen = rtlphy->hwparam_tables[AGCTAB_1T].length;
		agctab_array_table =  rtlphy->hwparam_tables[AGCTAB_1T].pdata;
		phy_reg_arraylen = rtlphy->hwparam_tables[PHY_REG_1T].length;
		phy_regarray_table = rtlphy->hwparam_tables[PHY_REG_1T].pdata;
	}
	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_reg_arraylen; i = i + 2) {
			rtl_addr_delay(phy_regarray_table[i]);
			rtl_set_bbreg(hw, phy_regarray_table[i], MASKDWORD,
				      phy_regarray_table[i + 1]);
			udelay(1);
			RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
				 "The phy_regarray_table[0] is %x Rtl819XPHY_REGArray[1] is %x\n",
				 phy_regarray_table[i],
				 phy_regarray_table[i + 1]);
		}
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		for (i = 0; i < agctab_arraylen; i = i + 2) {
			rtl_set_bbreg(hw, agctab_array_table[i], MASKDWORD,
				      agctab_array_table[i + 1]);
			udelay(1);
			RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
				 "The agctab_array_table[0] is %x Rtl819XPHY_REGArray[1] is %x\n",
				 agctab_array_table[i],
				 agctab_array_table[i + 1]);
		}
	}
	return true;
}

bool _rtl92cu_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
					      u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	int i;
	u32 *phy_regarray_table_pg;
	u16 phy_regarray_pg_len;

	rtlphy->pwrgroup_cnt = 0;
	phy_regarray_pg_len = rtlphy->hwparam_tables[PHY_REG_PG].length;
	phy_regarray_table_pg = rtlphy->hwparam_tables[PHY_REG_PG].pdata;
	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_regarray_pg_len; i = i + 3) {
			rtl_addr_delay(phy_regarray_table_pg[i]);
			_rtl92c_store_pwrIndex_diffrate_offset(hw,
						  phy_regarray_table_pg[i],
						  phy_regarray_table_pg[i + 1],
						  phy_regarray_table_pg[i + 2]);
		}
	} else {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
			 "configtype != BaseBand_Config_PHY_REG\n");
	}
	return true;
}

bool rtl92cu_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					  enum radio_path rfpath)
{
	int i;
	u32 *radioa_array_table;
	u32 *radiob_array_table;
	u16 radioa_arraylen, radiob_arraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	if (IS_92C_SERIAL(rtlhal->version)) {
		radioa_arraylen = rtlphy->hwparam_tables[RADIOA_2T].length;
		radioa_array_table = rtlphy->hwparam_tables[RADIOA_2T].pdata;
		radiob_arraylen = rtlphy->hwparam_tables[RADIOB_2T].length;
		radiob_array_table = rtlphy->hwparam_tables[RADIOB_2T].pdata;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Radio_A:RTL8192CERADIOA_2TARRAY\n");
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Radio_B:RTL8192CE_RADIOB_2TARRAY\n");
	} else {
		radioa_arraylen = rtlphy->hwparam_tables[RADIOA_1T].length;
		radioa_array_table = rtlphy->hwparam_tables[RADIOA_1T].pdata;
		radiob_arraylen = rtlphy->hwparam_tables[RADIOB_1T].length;
		radiob_array_table = rtlphy->hwparam_tables[RADIOB_1T].pdata;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Radio_A:RTL8192CE_RADIOA_1TARRAY\n");
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Radio_B:RTL8192CE_RADIOB_1TARRAY\n");
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Radio No %x\n", rfpath);
	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radioa_arraylen; i = i + 2) {
			rtl_rfreg_delay(hw, rfpath, radioa_array_table[i],
					RFREG_OFFSET_MASK,
					radioa_array_table[i + 1]);
		}
		break;
	case RF90_PATH_B:
		for (i = 0; i < radiob_arraylen; i = i + 2) {
			rtl_rfreg_delay(hw, rfpath, radiob_array_table[i],
					RFREG_OFFSET_MASK,
					radiob_array_table[i + 1]);
		}
		break;
	case RF90_PATH_C:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	case RF90_PATH_D:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	default:
		break;
	}
	return true;
}

void rtl92cu_phy_set_bw_mode_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 reg_bw_opmode;
	u8 reg_prsr_rsc;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "Switch to %s bandwidth\n",
		 rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20 ?
		 "20MHz" : "40MHz");
	if (is_hal_stop(rtlhal)) {
		rtlphy->set_bwmode_inprogress = false;
		return;
	}
	reg_bw_opmode = rtl_read_byte(rtlpriv, REG_BWOPMODE);
	reg_prsr_rsc = rtl_read_byte(rtlpriv, REG_RRSR + 2);
	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		reg_bw_opmode |= BW_OPMODE_20MHZ;
		rtl_write_byte(rtlpriv, REG_BWOPMODE, reg_bw_opmode);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		reg_bw_opmode &= ~BW_OPMODE_20MHZ;
		rtl_write_byte(rtlpriv, REG_BWOPMODE, reg_bw_opmode);
		reg_prsr_rsc =
		    (reg_prsr_rsc & 0x90) | (mac->cur_40_prime_sc << 5);
		rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_prsr_rsc);
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
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10), 1);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RCCK0_SYSTEM, BCCK_SIDEBAND,
			      (mac->cur_40_prime_sc >> 1));
		rtl_set_bbreg(hw, ROFDM1_LSTF, 0xC00, mac->cur_40_prime_sc);
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10), 0);
		rtl_set_bbreg(hw, 0x818, (BIT(26) | BIT(27)),
			      (mac->cur_40_prime_sc ==
			       HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;
	}
	rtl92cu_phy_rf6052_set_bandwidth(hw, rtlphy->current_chan_bw);
	rtlphy->set_bwmode_inprogress = false;
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "<==\n");
}

void rtl92cu_bb_block_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	mutex_lock(&rtlpriv->io.bb_mutex);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);
	mutex_unlock(&rtlpriv->io.bb_mutex);
}

void _rtl92cu_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t)
{
	u8 tmpreg;
	u32 rf_a_mode = 0, rf_b_mode = 0, lc_cal;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	tmpreg = rtl_read_byte(rtlpriv, 0xd03);

	if ((tmpreg & 0x70) != 0)
		rtl_write_byte(rtlpriv, 0xd03, tmpreg & 0x8F);
	else
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);

	if ((tmpreg & 0x70) != 0) {
		rf_a_mode = rtl_get_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS);
		if (is2t)
			rf_b_mode = rtl_get_rfreg(hw, RF90_PATH_B, 0x00,
						  MASK12BITS);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS,
			      (rf_a_mode & 0x8FFFF) | 0x10000);
		if (is2t)
			rtl_set_rfreg(hw, RF90_PATH_B, 0x00, MASK12BITS,
				      (rf_b_mode & 0x8FFFF) | 0x10000);
	}
	lc_cal = rtl_get_rfreg(hw, RF90_PATH_A, 0x18, MASK12BITS);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x18, MASK12BITS, lc_cal | 0x08000);
	mdelay(100);
	if ((tmpreg & 0x70) != 0) {
		rtl_write_byte(rtlpriv, 0xd03, tmpreg);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS, rf_a_mode);
		if (is2t)
			rtl_set_rfreg(hw, RF90_PATH_B, 0x00, MASK12BITS,
				      rf_b_mode);
	} else {
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
	}
}

static bool _rtl92cu_phy_set_rf_power_state(struct ieee80211_hw *hw,
					    enum rf_pwrstate rfpwr_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool bresult = true;
	u8 i, queue_id;
	struct rtl8192_tx_ring *ring = NULL;

	switch (rfpwr_state) {
	case ERFON:
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
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "Set ERFON sleeped:%d ms\n",
				 jiffies_to_msecs(jiffies -
						  ppsc->last_sleep_jiffies));
			ppsc->last_awake_jiffies = jiffies;
			rtl92ce_phy_set_rf_on(hw);
		}
		if (mac->link_state == MAC80211_LINKED) {
			rtlpriv->cfg->ops->led_control(hw,
						       LED_CTL_LINK);
		} else {
			rtlpriv->cfg->ops->led_control(hw,
						       LED_CTL_NO_LINK);
		}
		break;
	case ERFOFF:
		for (queue_id = 0, i = 0;
		     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
			ring = &pcipriv->dev.tx_ring[queue_id];
			if (skb_queue_len(&ring->queue) == 0 ||
				queue_id == BEACON_QUEUE) {
				queue_id++;
				continue;
			} else {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
					 i + 1,
					 queue_id,
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
		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC) {
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "IPS Set eRf nic disable\n");
			rtl_ps_disable_nic(hw);
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS) {
				rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_NO_LINK);
			} else {
				rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_POWER_OFF);
			}
		}
		break;
	case ERFSLEEP:
		if (ppsc->rfpwr_state == ERFOFF)
			return false;
		for (queue_id = 0, i = 0;
		     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
			ring = &pcipriv->dev.tx_ring[queue_id];
			if (skb_queue_len(&ring->queue) == 0) {
				queue_id++;
				continue;
			} else {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
					 i + 1, queue_id,
					 skb_queue_len(&ring->queue));
				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "ERFSLEEP: %d times TcbBusyQueue[%d] = %d !\n",
					 MAX_DOZE_WAITING_TIMES_9x,
					 queue_id,
					 skb_queue_len(&ring->queue));
				break;
			}
		}
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "Set ERFSLEEP awaked:%d ms\n",
			 jiffies_to_msecs(jiffies - ppsc->last_awake_jiffies));
		ppsc->last_sleep_jiffies = jiffies;
		_rtl92c_phy_set_rf_sleep(hw);
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

bool rtl92cu_phy_set_rf_power_state(struct ieee80211_hw *hw,
				    enum rf_pwrstate rfpwr_state)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool bresult = false;

	if (rfpwr_state == ppsc->rfpwr_state)
		return bresult;
	bresult = _rtl92cu_phy_set_rf_power_state(hw, rfpwr_state);
	return bresult;
}
