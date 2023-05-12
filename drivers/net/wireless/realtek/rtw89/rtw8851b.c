// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2022-2023  Realtek Corporation
 */

#include "coex.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8851b.h"
#include "rtw8851b_rfk_table.h"
#include "rtw8851b_table.h"
#include "txrx.h"
#include "util.h"

#define RTW8851B_FW_FORMAT_MAX 0
#define RTW8851B_FW_BASENAME "rtw89/rtw8851b_fw"
#define RTW8851B_MODULE_FIRMWARE \
	RTW8851B_FW_BASENAME ".bin"

static const struct rtw89_hfc_ch_cfg rtw8851b_hfc_chcfg_pcie[] = {
	{5, 343, grp_0}, /* ACH 0 */
	{5, 343, grp_0}, /* ACH 1 */
	{5, 343, grp_0}, /* ACH 2 */
	{5, 343, grp_0}, /* ACH 3 */
	{0, 0, grp_0}, /* ACH 4 */
	{0, 0, grp_0}, /* ACH 5 */
	{0, 0, grp_0}, /* ACH 6 */
	{0, 0, grp_0}, /* ACH 7 */
	{4, 344, grp_0}, /* B0MGQ */
	{4, 344, grp_0}, /* B0HIQ */
	{0, 0, grp_0}, /* B1MGQ */
	{0, 0, grp_0}, /* B1HIQ */
	{40, 0, 0} /* FWCMDQ */
};

static const struct rtw89_hfc_pub_cfg rtw8851b_hfc_pubcfg_pcie = {
	448, /* Group 0 */
	0, /* Group 1 */
	448, /* Public Max */
	0 /* WP threshold */
};

static const struct rtw89_hfc_param_ini rtw8851b_hfc_param_ini_pcie[] = {
	[RTW89_QTA_SCC] = {rtw8851b_hfc_chcfg_pcie, &rtw8851b_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_preccfg_pcie, RTW89_HCIFC_POH},
	[RTW89_QTA_DLFW] = {NULL, NULL, &rtw89_mac_size.hfc_preccfg_pcie,
			    RTW89_HCIFC_POH},
	[RTW89_QTA_INVALID] = {NULL},
};

static const struct rtw89_dle_mem rtw8851b_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_mac_size.wde_size6,
			   &rtw89_mac_size.ple_size6, &rtw89_mac_size.wde_qt6,
			   &rtw89_mac_size.wde_qt6, &rtw89_mac_size.ple_qt18,
			   &rtw89_mac_size.ple_qt58},
	[RTW89_QTA_WOW] = {RTW89_QTA_WOW, &rtw89_mac_size.wde_size6,
			   &rtw89_mac_size.ple_size6, &rtw89_mac_size.wde_qt6,
			   &rtw89_mac_size.wde_qt6, &rtw89_mac_size.ple_qt18,
			   &rtw89_mac_size.ple_qt_51b_wow},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size9,
			    &rtw89_mac_size.ple_size8, &rtw89_mac_size.wde_qt4,
			    &rtw89_mac_size.wde_qt4, &rtw89_mac_size.ple_qt13,
			    &rtw89_mac_size.ple_qt13},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static const struct rtw89_xtal_info rtw8851b_xtal_info = {
	.xcap_reg		= R_AX_XTAL_ON_CTRL3,
	.sc_xo_mask		= B_AX_XTAL_SC_XO_A_BLOCK_MASK,
	.sc_xi_mask		= B_AX_XTAL_SC_XI_A_BLOCK_MASK,
};

static const struct rtw89_btc_rf_trx_para rtw89_btc_8851b_rf_ul[] = {
	{255, 0, 0, 7}, /* 0 -> original */
	{255, 2, 0, 7}, /* 1 -> for BT-connected ACI issue && BTG co-rx */
	{255, 0, 0, 7}, /* 2 ->reserved for shared-antenna */
	{255, 0, 0, 7}, /* 3- >reserved for shared-antenna */
	{255, 0, 0, 7}, /* 4 ->reserved for shared-antenna */
	{255, 1, 0, 7}, /* the below id is for non-shared-antenna free-run */
	{6, 1, 0, 7},
	{13, 1, 0, 7},
	{13, 1, 0, 7}
};

static const struct rtw89_btc_rf_trx_para rtw89_btc_8851b_rf_dl[] = {
	{255, 0, 0, 7}, /* 0 -> original */
	{255, 2, 0, 7}, /* 1 -> reserved for shared-antenna */
	{255, 0, 0, 7}, /* 2 ->reserved for shared-antenna */
	{255, 0, 0, 7}, /* 3- >reserved for shared-antenna */
	{255, 0, 0, 7}, /* 4 ->reserved for shared-antenna */
	{255, 1, 0, 7}, /* the below id is for non-shared-antenna free-run */
	{255, 1, 0, 7},
	{255, 1, 0, 7},
	{255, 1, 0, 7}
};

static const struct rtw89_btc_fbtc_mreg rtw89_btc_8851b_mon_reg[] = {
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda24),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda28),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda2c),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda30),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda4c),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda10),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda20),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda34),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xcef4),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0x8424),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xd200),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xd220),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x980),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4738),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4688),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4694),
};

static const u8 rtw89_btc_8851b_wl_rssi_thres[BTC_WL_RSSI_THMAX] = {70, 60, 50, 40};
static const u8 rtw89_btc_8851b_bt_rssi_thres[BTC_BT_RSSI_THMAX] = {50, 40, 30, 20};

static void rtw8851b_set_bb_gpio(struct rtw89_dev *rtwdev, u8 gpio_idx, bool inv,
				 u8 src_sel)
{
	u32 addr, mask;

	if (gpio_idx >= 32)
		return;

	/* 2 continual 32-bit registers for 32 GPIOs, and each GPIO occupies 2 bits */
	addr = R_RFE_SEL0_A2 + (gpio_idx / 16) * sizeof(u32);
	mask = B_RFE_SEL0_MASK << (gpio_idx % 16) * 2;

	rtw89_phy_write32_mask(rtwdev, addr, mask, RF_PATH_A);
	rtw89_phy_write32_mask(rtwdev, R_RFE_INV0, BIT(gpio_idx), inv);

	/* 4 continual 32-bit registers for 32 GPIOs, and each GPIO occupies 4 bits */
	addr = R_RFE_SEL0_BASE + (gpio_idx / 8) * sizeof(u32);
	mask = B_RFE_SEL0_SRC_MASK << (gpio_idx % 8) * 4;

	rtw89_phy_write32_mask(rtwdev, addr, mask, src_sel);
}

static void rtw8851b_set_mac_gpio(struct rtw89_dev *rtwdev, u8 func)
{
	static const struct rtw89_reg3_def func16 = {
		R_AX_GPIO16_23_FUNC_SEL, B_AX_PINMUX_GPIO16_FUNC_SEL_MASK, BIT(3)
	};
	static const struct rtw89_reg3_def func17 = {
		R_AX_GPIO16_23_FUNC_SEL, B_AX_PINMUX_GPIO17_FUNC_SEL_MASK, BIT(7) >> 4,
	};
	const struct rtw89_reg3_def *def;

	switch (func) {
	case 16:
		def = &func16;
		break;
	case 17:
		def = &func17;
		break;
	default:
		rtw89_warn(rtwdev, "undefined gpio func %d\n", func);
		return;
	}

	rtw89_write8_mask(rtwdev, def->addr, def->mask, def->data);
}

static void rtw8851b_rfe_gpio(struct rtw89_dev *rtwdev)
{
	u8 rfe_type = rtwdev->efuse.rfe_type;

	if (rfe_type > 50)
		return;

	if (rfe_type % 3 == 2) {
		rtw8851b_set_bb_gpio(rtwdev, 16, true, RFE_SEL0_SRC_ANTSEL_0);
		rtw8851b_set_bb_gpio(rtwdev, 17, false, RFE_SEL0_SRC_ANTSEL_0);

		rtw8851b_set_mac_gpio(rtwdev, 16);
		rtw8851b_set_mac_gpio(rtwdev, 17);
	}
}

static void rtw8851b_btc_set_rfe(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_module *module = &btc->mdinfo;

	module->rfe_type = rtwdev->efuse.rfe_type;
	module->cv = rtwdev->hal.cv;
	module->bt_solo = 0;
	module->switch_type = BTC_SWITCH_INTERNAL;
	module->ant.isolation = 10;
	module->kt_ver_adie = rtwdev->hal.acv;

	if (module->rfe_type == 0)
		return;

	/* rfe_type 3*n+1: 1-Ant(shared),
	 *	    3*n+2: 2-Ant+Div(non-shared),
	 *	    3*n+3: 2-Ant+no-Div(non-shared)
	 */
	module->ant.num = (module->rfe_type % 3 == 1) ? 1 : 2;
	/* WL-1ss at S0, btg at s0 (On 1 WL RF) */
	module->ant.single_pos = RF_PATH_A;
	module->ant.btg_pos = RF_PATH_A;
	module->ant.stream_cnt = 1;

	if (module->ant.num == 1) {
		module->ant.type = BTC_ANT_SHARED;
		module->bt_pos = BTC_BT_BTG;
		module->wa_type = 1;
		module->ant.diversity = 0;
	} else { /* ant.num == 2 */
		module->ant.type = BTC_ANT_DEDICATED;
		module->bt_pos = BTC_BT_ALONE;
		module->switch_type = BTC_SWITCH_EXTERNAL;
		module->wa_type = 0;
		if (module->rfe_type % 3 == 2)
			module->ant.diversity = 1;
	}
}

static
void rtw8851b_set_trx_mask(struct rtw89_dev *rtwdev, u8 path, u8 group, u32 val)
{
	if (group > BTC_BT_SS_GROUP)
		group--; /* Tx-group=1, Rx-group=2 */

	if (rtwdev->btc.mdinfo.ant.type == BTC_ANT_SHARED) /* 1-Ant */
		group += 3;

	rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, group);
	rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, val);
}

static void rtw8851b_btc_init_cfg(struct rtw89_dev *rtwdev)
{
	static const struct rtw89_mac_ax_coex coex_params = {
		.pta_mode = RTW89_MAC_AX_COEX_RTK_MODE,
		.direction = RTW89_MAC_AX_COEX_INNER,
	};
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_module *module = &btc->mdinfo;
	struct rtw89_btc_ant_info *ant = &module->ant;
	u8 path, path_min, path_max;

	/* PTA init  */
	rtw89_mac_coex_init(rtwdev, &coex_params);

	/* set WL Tx response = Hi-Pri */
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_TX_RESP, true);
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_BEACON, true);

	/* for 1-Ant && 1-ss case: only 1-path */
	if (ant->stream_cnt == 1) {
		path_min = ant->single_pos;
		path_max = path_min;
	} else {
		path_min = RF_PATH_A;
		path_max = RF_PATH_B;
	}

	for (path = path_min; path <= path_max; path++) {
		/* set rf gnt-debug off */
		rtw89_write_rf(rtwdev, path, RR_WLSEL, RFREG_MASK, 0x0);

		/* set DEBUG_LUT_RFMODE_MASK = 1 to start trx-mask-setup */
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, BIT(17));

		/* if GNT_WL=0 && BT=SS_group --> WL Tx/Rx = THRU  */
		rtw8851b_set_trx_mask(rtwdev, path, BTC_BT_SS_GROUP, 0x5ff);

		/* if GNT_WL=0 && BT=Rx_group --> WL-Rx = THRU + WL-Tx = MASK */
		rtw8851b_set_trx_mask(rtwdev, path, BTC_BT_RX_GROUP, 0x5df);

		/* if GNT_WL = 0 && BT = Tx_group -->
		 * Shared-Ant && BTG-path:WL mask(0x55f), others:WL THRU(0x5ff)
		 */
		if (ant->type == BTC_ANT_SHARED && ant->btg_pos == path)
			rtw8851b_set_trx_mask(rtwdev, path, BTC_BT_TX_GROUP, 0x55f);
		else
			rtw8851b_set_trx_mask(rtwdev, path, BTC_BT_TX_GROUP, 0x5ff);

		/* set DEBUG_LUT_RFMODE_MASK = 0 to stop trx-mask-setup */
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0);
	}

	/* set PTA break table */
	rtw89_write32(rtwdev, R_BTC_BREAK_TABLE, BTC_BREAK_PARAM);

	/* enable BT counter 0xda40[16,2] = 2b'11 */
	rtw89_write32_set(rtwdev, R_AX_CSR_MODE, B_AX_BT_CNT_RST | B_AX_STATIS_BT_EN);

	btc->cx.wl.status.map.init_ok = true;
}

static
void rtw8851b_btc_set_wl_pri(struct rtw89_dev *rtwdev, u8 map, bool state)
{
	u32 bitmap;
	u32 reg;

	switch (map) {
	case BTC_PRI_MASK_TX_RESP:
		reg = R_BTC_BT_COEX_MSK_TABLE;
		bitmap = B_BTC_PRI_MASK_TX_RESP_V1;
		break;
	case BTC_PRI_MASK_BEACON:
		reg = R_AX_WL_PRI_MSK;
		bitmap = B_AX_PTA_WL_PRI_MASK_BCNQ;
		break;
	case BTC_PRI_MASK_RX_CCK:
		reg = R_BTC_BT_COEX_MSK_TABLE;
		bitmap = B_BTC_PRI_MASK_RXCCK_V1;
		break;
	default:
		return;
	}

	if (state)
		rtw89_write32_set(rtwdev, reg, bitmap);
	else
		rtw89_write32_clr(rtwdev, reg, bitmap);
}

union rtw8851b_btc_wl_txpwr_ctrl {
	u32 txpwr_val;
	struct {
		union {
			u16 ctrl_all_time;
			struct {
				s16 data:9;
				u16 rsvd:6;
				u16 flag:1;
			} all_time;
		};
		union {
			u16 ctrl_gnt_bt;
			struct {
				s16 data:9;
				u16 rsvd:7;
			} gnt_bt;
		};
	};
} __packed;

static void
rtw8851b_btc_set_wl_txpwr_ctrl(struct rtw89_dev *rtwdev, u32 txpwr_val)
{
	union rtw8851b_btc_wl_txpwr_ctrl arg = { .txpwr_val = txpwr_val };
	s32 val;

#define __write_ctrl(_reg, _msk, _val, _en, _cond)		\
do {								\
	u32 _wrt = FIELD_PREP(_msk, _val);			\
	BUILD_BUG_ON(!!(_msk & _en));				\
	if (_cond)						\
		_wrt |= _en;					\
	else							\
		_wrt &= ~_en;					\
	rtw89_mac_txpwr_write32_mask(rtwdev, RTW89_PHY_0, _reg,	\
				     _msk | _en, _wrt);		\
} while (0)

	switch (arg.ctrl_all_time) {
	case 0xffff:
		val = 0;
		break;
	default:
		val = arg.all_time.data;
		break;
	}

	__write_ctrl(R_AX_PWR_RATE_CTRL, B_AX_FORCE_PWR_BY_RATE_VALUE_MASK,
		     val, B_AX_FORCE_PWR_BY_RATE_EN,
		     arg.ctrl_all_time != 0xffff);

	switch (arg.ctrl_gnt_bt) {
	case 0xffff:
		val = 0;
		break;
	default:
		val = arg.gnt_bt.data;
		break;
	}

	__write_ctrl(R_AX_PWR_COEXT_CTRL, B_AX_TXAGC_BT_MASK, val,
		     B_AX_TXAGC_BT_EN, arg.ctrl_gnt_bt != 0xffff);

#undef __write_ctrl
}

static
s8 rtw8851b_btc_get_bt_rssi(struct rtw89_dev *rtwdev, s8 val)
{
	val = clamp_t(s8, val, -100, 0) + 100;
	val = min(val + 6, 100); /* compensate offset */

	return val;
}

static
void rtw8851b_btc_update_bt_cnt(struct rtw89_dev *rtwdev)
{
	/* Feature move to firmware */
}

static void rtw8851b_btc_wl_s1_standby(struct rtw89_dev *rtwdev, bool state)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_ant_info *ant = &btc->mdinfo.ant;

	rtw89_write_rf(rtwdev, ant->btg_pos, RR_LUTWE, RFREG_MASK, 0x80000);
	rtw89_write_rf(rtwdev, ant->btg_pos, RR_LUTWA, RFREG_MASK, 0x1);
	rtw89_write_rf(rtwdev, ant->btg_pos, RR_LUTWD1, RFREG_MASK, 0x110);

	/* set WL standby = Rx for GNT_BT_Tx = 1->0 settle issue */
	if (state)
		rtw89_write_rf(rtwdev, ant->btg_pos, RR_LUTWD0, RFREG_MASK, 0x179c);
	else
		rtw89_write_rf(rtwdev, ant->btg_pos, RR_LUTWD0, RFREG_MASK, 0x208);

	rtw89_write_rf(rtwdev, ant->btg_pos, RR_LUTWE, RFREG_MASK, 0x0);
}

#define LNA2_51B_MA 0x700

static const struct rtw89_reg2_def btc_8851b_rf_0[] = {{0x2, 0x0}};
static const struct rtw89_reg2_def btc_8851b_rf_1[] = {{0x2, 0x1}};

static void rtw8851b_btc_set_wl_rx_gain(struct rtw89_dev *rtwdev, u32 level)
{
	/* To improve BT ACI in co-rx
	 * level=0 Default: TIA 1/0= (LNA2,TIAN6) = (7,1)/(5,1) = 21dB/12dB
	 * level=1 Fix LNA2=5: TIA 1/0= (LNA2,TIAN6) = (5,0)/(5,1) = 18dB/12dB
	 */
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_ant_info *ant = &btc->mdinfo.ant;
	const struct rtw89_reg2_def *rf;
	u32 n, i, val;

	switch (level) {
	case 0: /* original */
	default:
		btc->dm.wl_lna2 = 0;
		break;
	case 1: /* for FDD free-run */
		btc->dm.wl_lna2 = 0;
		break;
	case 2: /* for BTG Co-Rx*/
		btc->dm.wl_lna2 = 1;
		break;
	}

	if (btc->dm.wl_lna2 == 0) {
		rf = btc_8851b_rf_0;
		n = ARRAY_SIZE(btc_8851b_rf_0);
	} else {
		rf = btc_8851b_rf_1;
		n = ARRAY_SIZE(btc_8851b_rf_1);
	}

	for (i = 0; i < n; i++, rf++) {
		val = rf->data;
		/* bit[10] = 1 if non-shared-ant for 8851b */
		if (btc->mdinfo.ant.type == BTC_ANT_DEDICATED)
			val |= 0x4;

		rtw89_write_rf(rtwdev, ant->btg_pos, rf->addr, LNA2_51B_MA, val);
	}
}

static const struct rtw89_chip_ops rtw8851b_chip_ops = {
	.fem_setup		= NULL,
	.rfe_gpio		= rtw8851b_rfe_gpio,
	.fill_txdesc		= rtw89_core_fill_txdesc,
	.fill_txdesc_fwcmd	= rtw89_core_fill_txdesc,
	.h2c_dctl_sec_cam	= NULL,

	.btc_set_rfe		= rtw8851b_btc_set_rfe,
	.btc_init_cfg		= rtw8851b_btc_init_cfg,
	.btc_set_wl_pri		= rtw8851b_btc_set_wl_pri,
	.btc_set_wl_txpwr_ctrl	= rtw8851b_btc_set_wl_txpwr_ctrl,
	.btc_get_bt_rssi	= rtw8851b_btc_get_bt_rssi,
	.btc_update_bt_cnt	= rtw8851b_btc_update_bt_cnt,
	.btc_wl_s1_standby	= rtw8851b_btc_wl_s1_standby,
	.btc_set_wl_rx_gain	= rtw8851b_btc_set_wl_rx_gain,
	.btc_set_policy		= rtw89_btc_set_policy_v1,
};

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support rtw_wowlan_stub_8851b = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT,
	.n_patterns = RTW89_MAX_PATTERN_NUM,
	.pattern_max_len = RTW89_MAX_PATTERN_SIZE,
	.pattern_min_len = 1,
};
#endif

const struct rtw89_chip_info rtw8851b_chip_info = {
	.chip_id		= RTL8851B,
	.ops			= &rtw8851b_chip_ops,
	.fw_basename		= RTW8851B_FW_BASENAME,
	.fw_format_max		= RTW8851B_FW_FORMAT_MAX,
	.try_ce_fw		= true,
	.fifo_size		= 196608,
	.small_fifo_size	= true,
	.dle_scc_rsvd_size	= 98304,
	.max_amsdu_limit	= 3500,
	.dis_2g_40m_ul_ofdma	= true,
	.rsvd_ple_ofst		= 0x2f800,
	.hfc_param_ini		= rtw8851b_hfc_param_ini_pcie,
	.dle_mem		= rtw8851b_dle_mem_pcie,
	.wde_qempty_acq_num     = 4,
	.wde_qempty_mgq_sel     = 4,
	.rf_base_addr		= {0xe000},
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.bb_table		= &rtw89_8851b_phy_bb_table,
	.bb_gain_table		= &rtw89_8851b_phy_bb_gain_table,
	.rf_table		= {&rtw89_8851b_phy_radioa_table,},
	.nctl_table		= &rtw89_8851b_phy_nctl_table,
	.nctl_post_table	= &rtw8851b_nctl_post_defs_tbl,
	.byr_table		= &rtw89_8851b_byr_table,
	.dflt_parms		= &rtw89_8851b_dflt_parms,
	.rfe_parms_conf		= rtw89_8851b_rfe_parms_conf,
	.txpwr_factor_rf	= 2,
	.txpwr_factor_mac	= 1,
	.dig_table		= NULL,
	.tssi_dbw_table		= NULL,
	.support_chanctx_num	= 0,
	.support_bands		= BIT(NL80211_BAND_2GHZ) |
				  BIT(NL80211_BAND_5GHZ),
	.support_bw160		= false,
	.support_unii4		= true,
	.support_ul_tb_ctrl	= true,
	.hw_sec_hdr		= false,
	.rf_path_num		= 1,
	.tx_nss			= 1,
	.rx_nss			= 1,
	.acam_num		= 32,
	.bcam_num		= 20,
	.scam_num		= 128,
	.bacam_num		= 2,
	.bacam_dynamic_num	= 4,
	.bacam_ver		= RTW89_BACAM_V0,
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 1216,
	.logical_efuse_size	= 2048,
	.limit_efuse_size	= 1280,
	.dav_phy_efuse_size	= 0,
	.dav_log_efuse_size	= 0,
	.phycap_addr		= 0x580,
	.phycap_size		= 128,
	.para_ver		= 0,
	.wlcx_desired		= 0x06000000,
	.btcx_desired		= 0x7,
	.scbd			= 0x1,
	.mailbox		= 0x1,

	.afh_guard_ch		= 6,
	.wl_rssi_thres		= rtw89_btc_8851b_wl_rssi_thres,
	.bt_rssi_thres		= rtw89_btc_8851b_bt_rssi_thres,
	.rssi_tol		= 2,
	.mon_reg_num		= ARRAY_SIZE(rtw89_btc_8851b_mon_reg),
	.mon_reg		= rtw89_btc_8851b_mon_reg,
	.rf_para_ulink_num	= ARRAY_SIZE(rtw89_btc_8851b_rf_ul),
	.rf_para_ulink		= rtw89_btc_8851b_rf_ul,
	.rf_para_dlink_num	= ARRAY_SIZE(rtw89_btc_8851b_rf_dl),
	.rf_para_dlink		= rtw89_btc_8851b_rf_dl,
	.ps_mode_supported	= BIT(RTW89_PS_MODE_RFOFF) |
				  BIT(RTW89_PS_MODE_CLK_GATED),
	.low_power_hci_modes	= 0,
	.h2c_cctl_func_id	= H2C_FUNC_MAC_CCTLINFO_UD,
	.hci_func_en_addr	= R_AX_HCI_FUNC_EN,
	.h2c_desc_size		= sizeof(struct rtw89_txwd_body),
	.txwd_body_size		= sizeof(struct rtw89_txwd_body),
	.bss_clr_map_reg	= R_BSS_CLR_MAP_V1,
	.dma_ch_mask		= BIT(RTW89_DMA_ACH4) | BIT(RTW89_DMA_ACH5) |
				  BIT(RTW89_DMA_ACH6) | BIT(RTW89_DMA_ACH7) |
				  BIT(RTW89_DMA_B1MG) | BIT(RTW89_DMA_B1HI),
	.edcca_lvl_reg		= R_SEG0R_EDCCA_LVL_V1,
#ifdef CONFIG_PM
	.wowlan_stub		= &rtw_wowlan_stub_8851b,
#endif
	.xtal_info		= &rtw8851b_xtal_info,
};
EXPORT_SYMBOL(rtw8851b_chip_info);

MODULE_FIRMWARE(RTW8851B_MODULE_FIRMWARE);
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8851B driver");
MODULE_LICENSE("Dual BSD/GPL");
