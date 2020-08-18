/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_COEX_H__
#define __RTW_COEX_H__

/* BT profile map bit definition */
#define BPM_HFP		BIT(0)
#define BPM_HID		BIT(1)
#define BPM_A2DP		BIT(2)
#define BPM_PAN		BIT(3)

#define COEX_RESP_ACK_BY_WL_FW	0x1
#define COEX_REQUEST_TIMEOUT	msecs_to_jiffies(10)

#define COEX_MIN_DELAY		10 /* delay unit in ms */
#define COEX_RFK_TIMEOUT	600 /* RFK timeout in ms */

#define COEX_RF_OFF	0x0
#define COEX_RF_ON	0x1

#define COEX_H2C69_WL_LEAKAP	0xc
#define PARA1_H2C69_DIS_5MS	0x1
#define PARA1_H2C69_EN_5MS	0x0

#define COEX_H2C69_TDMA_SLOT	0xb
#define PARA1_H2C69_TDMA_4SLOT	0xc1
#define PARA1_H2C69_TDMA_2SLOT	0x1

#define TDMA_4SLOT	BIT(8)

#define COEX_RSSI_STEP		4
#define COEX_RSSI_HIGH(rssi) \
	({ typeof(rssi) __rssi__ = rssi; \
	   (__rssi__ == COEX_RSSI_STATE_HIGH || \
	    __rssi__ == COEX_RSSI_STATE_STAY_HIGH ? true : false); })

#define COEX_RSSI_MEDIUM(rssi) \
	({ typeof(rssi) __rssi__ = rssi; \
	   (__rssi__ == COEX_RSSI_STATE_MEDIUM || \
	    __rssi__ == COEX_RSSI_STATE_STAY_MEDIUM ? true : false); })

#define COEX_RSSI_LOW(rssi) \
	({ typeof(rssi) __rssi__ = rssi; \
	   (__rssi__ == COEX_RSSI_STATE_LOW || \
	    __rssi__ == COEX_RSSI_STATE_STAY_LOW ? true : false); })

#define GET_COEX_RESP_BT_SUPP_VER(payload)				\
	le64_get_bits(*((__le64 *)(payload)), GENMASK_ULL(39, 32))
#define GET_COEX_RESP_BT_SUPP_FEAT(payload)				\
	le64_get_bits(*((__le64 *)(payload)), GENMASK_ULL(39, 24))
#define GET_COEX_RESP_BT_PATCH_VER(payload)				\
	le64_get_bits(*((__le64 *)(payload)), GENMASK_ULL(55, 24))
#define GET_COEX_RESP_BT_REG_VAL(payload)				\
	le64_get_bits(*((__le64 *)(payload)), GENMASK_ULL(39, 24))
#define GET_COEX_RESP_BT_SCAN_TYPE(payload)				\
	le64_get_bits(*((__le64 *)(payload)), GENMASK(31, 24))

enum coex_mp_info_op {
	BT_MP_INFO_OP_PATCH_VER	= 0x00,
	BT_MP_INFO_OP_READ_REG	= 0x11,
	BT_MP_INFO_OP_SUPP_FEAT	= 0x2a,
	BT_MP_INFO_OP_SUPP_VER	= 0x2b,
	BT_MP_INFO_OP_SCAN_TYPE	= 0x2d,
	BT_MP_INFO_OP_LNA_CONSTRAINT	= 0x32,
};

enum coex_set_ant_phase {
	COEX_SET_ANT_INIT,
	COEX_SET_ANT_WONLY,
	COEX_SET_ANT_WOFF,
	COEX_SET_ANT_2G,
	COEX_SET_ANT_5G,
	COEX_SET_ANT_POWERON,
	COEX_SET_ANT_2G_WLBT,
	COEX_SET_ANT_2G_FREERUN,

	COEX_SET_ANT_MAX
};

enum coex_runreason {
	COEX_RSN_2GSCANSTART	= 0,
	COEX_RSN_5GSCANSTART	= 1,
	COEX_RSN_SCANFINISH	= 2,
	COEX_RSN_2GSWITCHBAND	= 3,
	COEX_RSN_5GSWITCHBAND	= 4,
	COEX_RSN_2GCONSTART	= 5,
	COEX_RSN_5GCONSTART	= 6,
	COEX_RSN_2GCONFINISH	= 7,
	COEX_RSN_5GCONFINISH	= 8,
	COEX_RSN_2GMEDIA	= 9,
	COEX_RSN_5GMEDIA	= 10,
	COEX_RSN_MEDIADISCON	= 11,
	COEX_RSN_BTINFO		= 12,
	COEX_RSN_LPS		= 13,
	COEX_RSN_WLSTATUS	= 14,
	COEX_RSN_BTSTATUS	= 15,

	COEX_RSN_MAX
};

enum coex_lte_coex_table_type {
	COEX_CTT_WL_VS_LTE,
	COEX_CTT_BT_VS_LTE,
};

enum coex_gnt_setup_state {
	COEX_GNT_SET_HW_PTA	= 0x0,
	COEX_GNT_SET_SW_LOW	= 0x1,
	COEX_GNT_SET_SW_HIGH	= 0x3,
};

enum coex_ext_ant_switch_pos_type {
	COEX_SWITCH_TO_BT,
	COEX_SWITCH_TO_WLG,
	COEX_SWITCH_TO_WLA,
	COEX_SWITCH_TO_NOCARE,
	COEX_SWITCH_TO_WLG_BT,

	COEX_SWITCH_TO_MAX
};

enum coex_ext_ant_switch_ctrl_type {
	COEX_SWITCH_CTRL_BY_BBSW,
	COEX_SWITCH_CTRL_BY_PTA,
	COEX_SWITCH_CTRL_BY_ANTDIV,
	COEX_SWITCH_CTRL_BY_MAC,
	COEX_SWITCH_CTRL_BY_BT,
	COEX_SWITCH_CTRL_BY_FW,

	COEX_SWITCH_CTRL_MAX
};

enum coex_algorithm {
	COEX_ALGO_NOPROFILE	= 0,
	COEX_ALGO_HFP		= 1,
	COEX_ALGO_HID		= 2,
	COEX_ALGO_A2DP		= 3,
	COEX_ALGO_PAN		= 4,
	COEX_ALGO_A2DP_HID	= 5,
	COEX_ALGO_A2DP_PAN	= 6,
	COEX_ALGO_PAN_HID	= 7,
	COEX_ALGO_A2DP_PAN_HID	= 8,

	COEX_ALGO_MAX
};

enum coex_wl_link_mode {
	COEX_WLINK_2G1PORT	= 0x0,
	COEX_WLINK_5G		= 0x3,
	COEX_WLINK_MAX
};

enum coex_wl2bt_scoreboard {
	COEX_SCBD_ACTIVE	= BIT(0),
	COEX_SCBD_ONOFF		= BIT(1),
	COEX_SCBD_SCAN		= BIT(2),
	COEX_SCBD_UNDERTEST	= BIT(3),
	COEX_SCBD_RXGAIN	= BIT(4),
	COEX_SCBD_BT_RFK	= BIT(5),
	COEX_SCBD_WLBUSY	= BIT(6),
	COEX_SCBD_EXTFEM	= BIT(8),
	COEX_SCBD_TDMA		= BIT(9),
	COEX_SCBD_FIX2M		= BIT(10),
	COEX_SCBD_ALL		= GENMASK(15, 0),
};

enum coex_power_save_type {
	COEX_PS_WIFI_NATIVE	= 0,
	COEX_PS_LPS_ON		= 1,
	COEX_PS_LPS_OFF		= 2,
};

enum coex_rssi_state {
	COEX_RSSI_STATE_HIGH,
	COEX_RSSI_STATE_MEDIUM,
	COEX_RSSI_STATE_LOW,
	COEX_RSSI_STATE_STAY_HIGH,
	COEX_RSSI_STATE_STAY_MEDIUM,
	COEX_RSSI_STATE_STAY_LOW,
};

enum coex_notify_type_ips {
	COEX_IPS_LEAVE		= 0x0,
	COEX_IPS_ENTER		= 0x1,
};

enum coex_notify_type_lps {
	COEX_LPS_DISABLE	= 0x0,
	COEX_LPS_ENABLE		= 0x1,
};

enum coex_notify_type_scan {
	COEX_SCAN_FINISH,
	COEX_SCAN_START,
	COEX_SCAN_START_2G,
	COEX_SCAN_START_5G,
};

enum coex_notify_type_switchband {
	COEX_NOT_SWITCH,
	COEX_SWITCH_TO_24G,
	COEX_SWITCH_TO_5G,
	COEX_SWITCH_TO_24G_NOFORSCAN,
};

enum coex_notify_type_associate {
	COEX_ASSOCIATE_FINISH,
	COEX_ASSOCIATE_START,
	COEX_ASSOCIATE_5G_FINISH,
	COEX_ASSOCIATE_5G_START,
};

enum coex_notify_type_media_status {
	COEX_MEDIA_DISCONNECT,
	COEX_MEDIA_CONNECT,
	COEX_MEDIA_CONNECT_5G,
};

enum coex_bt_status {
	COEX_BTSTATUS_NCON_IDLE		= 0,
	COEX_BTSTATUS_CON_IDLE		= 1,
	COEX_BTSTATUS_INQ_PAGE		= 2,
	COEX_BTSTATUS_ACL_BUSY		= 3,
	COEX_BTSTATUS_SCO_BUSY		= 4,
	COEX_BTSTATUS_ACL_SCO_BUSY	= 5,

	COEX_BTSTATUS_MAX
};

enum coex_wl_tput_dir {
	COEX_WL_TPUT_TX			= 0x0,
	COEX_WL_TPUT_RX			= 0x1,
	COEX_WL_TPUT_MAX
};

enum coex_wl_priority_mask {
	COEX_WLPRI_RX_RSP	= 2,
	COEX_WLPRI_TX_RSP	= 3,
	COEX_WLPRI_TX_BEACON	= 4,
	COEX_WLPRI_TX_OFDM	= 11,
	COEX_WLPRI_TX_CCK	= 12,
	COEX_WLPRI_TX_BEACONQ	= 27,
	COEX_WLPRI_RX_CCK	= 28,
	COEX_WLPRI_RX_OFDM	= 29,
	COEX_WLPRI_MAX
};

enum coex_commom_chip_setup {
	COEX_CSETUP_INIT_HW		= 0x0,
	COEX_CSETUP_ANT_SWITCH		= 0x1,
	COEX_CSETUP_GNT_FIX		= 0x2,
	COEX_CSETUP_GNT_DEBUG		= 0x3,
	COEX_CSETUP_RFE_TYPE		= 0x4,
	COEX_CSETUP_COEXINFO_HW		= 0x5,
	COEX_CSETUP_WL_TX_POWER		= 0x6,
	COEX_CSETUP_WL_RX_GAIN		= 0x7,
	COEX_CSETUP_WLAN_ACT_IPS	= 0x8,
	COEX_CSETUP_MAX
};

enum coex_indirect_reg_type {
	COEX_INDIRECT_1700		= 0x0,
	COEX_INDIRECT_7C0		= 0x1,
	COEX_INDIRECT_MAX
};

enum coex_pstdma_type {
	COEX_PSTDMA_FORCE_LPSOFF	= 0x0,
	COEX_PSTDMA_FORCE_LPSON		= 0x1,
	COEX_PSTDMA_MAX
};

enum coex_btrssi_type {
	COEX_BTRSSI_RATIO		= 0x0,
	COEX_BTRSSI_DBM			= 0x1,
	COEX_BTRSSI_MAX
};

struct coex_table_para {
	u32 bt;
	u32 wl;
};

struct coex_tdma_para {
	u8 para[5];
};

struct coex_5g_afh_map {
	u32 wl_5g_ch;
	u8 bt_skip_ch;
	u8 bt_skip_span;
};

struct coex_rf_para {
	u8 wl_pwr_dec_lvl;
	u8 bt_pwr_dec_lvl;
	bool wl_low_gain_en;
	u8 bt_lna_lvl;
};

static inline void rtw_coex_set_init(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	chip->ops->coex_set_init(rtwdev);
}

static inline
void rtw_coex_set_ant_switch(struct rtw_dev *rtwdev, u8 ctrl_type, u8 pos_type)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	if (!chip->ops->coex_set_ant_switch)
		return;

	chip->ops->coex_set_ant_switch(rtwdev, ctrl_type, pos_type);
}

static inline void rtw_coex_set_gnt_fix(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	chip->ops->coex_set_gnt_fix(rtwdev);
}

static inline void rtw_coex_set_gnt_debug(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	chip->ops->coex_set_gnt_debug(rtwdev);
}

static inline  void rtw_coex_set_rfe_type(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	chip->ops->coex_set_rfe_type(rtwdev);
}

static inline void rtw_coex_set_wl_tx_power(struct rtw_dev *rtwdev, u8 wl_pwr)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	chip->ops->coex_set_wl_tx_power(rtwdev, wl_pwr);
}

static inline
void rtw_coex_set_wl_rx_gain(struct rtw_dev *rtwdev, bool low_gain)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	chip->ops->coex_set_wl_rx_gain(rtwdev, low_gain);
}

void rtw_coex_info_response(struct rtw_dev *rtwdev, struct sk_buff *skb);
u32 rtw_coex_read_indirect_reg(struct rtw_dev *rtwdev, u16 addr);
void rtw_coex_write_indirect_reg(struct rtw_dev *rtwdev, u16 addr,
				 u32 mask, u32 val);
void rtw_coex_write_scbd(struct rtw_dev *rtwdev, u16 bitpos, bool set);

void rtw_coex_bt_relink_work(struct work_struct *work);
void rtw_coex_bt_reenable_work(struct work_struct *work);
void rtw_coex_defreeze_work(struct work_struct *work);
void rtw_coex_wl_remain_work(struct work_struct *work);
void rtw_coex_bt_remain_work(struct work_struct *work);

void rtw_coex_power_on_setting(struct rtw_dev *rtwdev);
void rtw_coex_init_hw_config(struct rtw_dev *rtwdev, bool wifi_only);
void rtw_coex_ips_notify(struct rtw_dev *rtwdev, u8 type);
void rtw_coex_lps_notify(struct rtw_dev *rtwdev, u8 type);
void rtw_coex_scan_notify(struct rtw_dev *rtwdev, u8 type);
void rtw_coex_connect_notify(struct rtw_dev *rtwdev, u8 action);
void rtw_coex_media_status_notify(struct rtw_dev *rtwdev, u8 status);
void rtw_coex_bt_info_notify(struct rtw_dev *rtwdev, u8 *buf, u8 len);
void rtw_coex_wl_fwdbginfo_notify(struct rtw_dev *rtwdev, u8 *buf, u8 length);
void rtw_coex_switchband_notify(struct rtw_dev *rtwdev, u8 type);
void rtw_coex_wl_status_change_notify(struct rtw_dev *rtwdev);

void rtw_coex_display_coex_info(struct rtw_dev *rtwdev, struct seq_file *m);

#endif
