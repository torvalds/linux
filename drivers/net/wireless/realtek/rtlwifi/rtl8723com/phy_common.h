/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2014  Realtek Corporation.*/

#ifndef __PHY_COMMON__
#define __PHY_COMMON__

#define RT_CANNOT_IO(hw)			false

enum swchnlcmd_id {
	CMDID_END,
	CMDID_SET_TXPOWEROWER_LEVEL,
	CMDID_BBREGWRITE10,
	CMDID_WRITEPORT_ULONG,
	CMDID_WRITEPORT_USHORT,
	CMDID_WRITEPORT_UCHAR,
	CMDID_RF_WRITEREG,
};

struct swchnlcmd {
	enum swchnlcmd_id cmdid;
	u32 para1;
	u32 para2;
	u32 msdelay;
};

u32 rtl8723_phy_query_bb_reg(struct ieee80211_hw *hw,
			     u32 regaddr, u32 bitmask);
void rtl8723_phy_set_bb_reg(struct ieee80211_hw *hw, u32 regaddr,
			      u32 bitmask, u32 data);
u32 rtl8723_phy_calculate_bit_shift(u32 bitmask);
u32 rtl8723_phy_rf_serial_read(struct ieee80211_hw *hw,
			       enum radio_path rfpath, u32 offset);
void rtl8723_phy_rf_serial_write(struct ieee80211_hw *hw,
				 enum radio_path rfpath,
				 u32 offset, u32 data);
long rtl8723_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
				  enum wireless_mode wirelessmode,
				  u8 txpwridx);
void rtl8723_phy_init_bb_rf_reg_def(struct ieee80211_hw *hw);
bool rtl8723_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
				      u32 cmdtableidx,
				      u32 cmdtablesz,
				      enum swchnlcmd_id cmdid,
				      u32 para1, u32 para2,
				      u32 msdelay);
void rtl8723_phy_path_a_fill_iqk_matrix(struct ieee80211_hw *hw,
					bool iqk_ok,
					long result[][8],
					u8 final_candidate,
					bool btxonly);
void rtl8723_save_adda_registers(struct ieee80211_hw *hw, u32 *addareg,
				 u32 *addabackup, u32 registernum);
void rtl8723_phy_save_mac_registers(struct ieee80211_hw *hw,
				    u32 *macreg, u32 *macbackup);
void rtl8723_phy_reload_adda_registers(struct ieee80211_hw *hw,
				       u32 *addareg, u32 *addabackup,
				       u32 regiesternum);
void rtl8723_phy_reload_mac_registers(struct ieee80211_hw *hw,
				      u32 *macreg, u32 *macbackup);
void rtl8723_phy_path_adda_on(struct ieee80211_hw *hw, u32 *addareg,
			      bool is_patha_on, bool is2t);
void rtl8723_phy_mac_setting_calibration(struct ieee80211_hw *hw,
					 u32 *macreg, u32 *macbackup);
void rtl8723_phy_path_a_standby(struct ieee80211_hw *hw);
void rtl8723_phy_pi_mode_switch(struct ieee80211_hw *hw, bool pi_mode);

#endif
