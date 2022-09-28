// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "coex.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852b.h"
#include "rtw8852b_table.h"
#include "txrx.h"

static const struct rtw89_dle_mem rtw8852b_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_mac_size.wde_size6,
			   &rtw89_mac_size.ple_size6, &rtw89_mac_size.wde_qt6,
			   &rtw89_mac_size.wde_qt6, &rtw89_mac_size.ple_qt18,
			   &rtw89_mac_size.ple_qt58},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size9,
			    &rtw89_mac_size.ple_size8, &rtw89_mac_size.wde_qt4,
			    &rtw89_mac_size.wde_qt4, &rtw89_mac_size.ple_qt13,
			    &rtw89_mac_size.ple_qt13},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static u32 rtw8852b_bb_cal_txpwr_ref(struct rtw89_dev *rtwdev,
				     enum rtw89_phy_idx phy_idx, s16 ref)
{
	const u16 tssi_16dbm_cw = 0x12c;
	const u8 base_cw_0db = 0x27;
	const s8 ofst_int = 0;
	s16 pwr_s10_3;
	s16 rf_pwr_cw;
	u16 bb_pwr_cw;
	u32 pwr_cw;
	u32 tssi_ofst_cw;

	pwr_s10_3 = (ref << 1) + (s16)(ofst_int) + (s16)(base_cw_0db << 3);
	bb_pwr_cw = FIELD_GET(GENMASK(2, 0), pwr_s10_3);
	rf_pwr_cw = FIELD_GET(GENMASK(8, 3), pwr_s10_3);
	rf_pwr_cw = clamp_t(s16, rf_pwr_cw, 15, 63);
	pwr_cw = (rf_pwr_cw << 3) | bb_pwr_cw;

	tssi_ofst_cw = (u32)((s16)tssi_16dbm_cw + (ref << 1) - (16 << 3));
	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] tssi_ofst_cw=%d rf_cw=0x%x bb_cw=0x%x\n",
		    tssi_ofst_cw, rf_pwr_cw, bb_pwr_cw);

	return FIELD_PREP(B_DPD_TSSI_CW, tssi_ofst_cw) |
	       FIELD_PREP(B_DPD_PWR_CW, pwr_cw) |
	       FIELD_PREP(B_DPD_REF, ref);
}

static void rtw8852b_set_txpwr_ref(struct rtw89_dev *rtwdev,
				   enum rtw89_phy_idx phy_idx)
{
	static const u32 addr[RF_PATH_NUM_8852B] = {0x5800, 0x7800};
	const u32 mask = B_DPD_TSSI_CW | B_DPD_PWR_CW | B_DPD_REF;
	const u8 ofst_ofdm = 0x4;
	const u8 ofst_cck = 0x8;
	const s16 ref_ofdm = 0;
	const s16 ref_cck = 0;
	u32 val;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set txpwr reference\n");

	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_AX_PWR_RATE_CTRL,
				     B_AX_PWR_REF, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb ofdm txpwr ref\n");
	val = rtw8852b_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_ofdm);

	for (i = 0; i < RF_PATH_NUM_8852B; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_ofdm, mask, val,
				      phy_idx);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb cck txpwr ref\n");
	val = rtw8852b_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_cck);

	for (i = 0; i < RF_PATH_NUM_8852B; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_cck, mask, val,
				      phy_idx);
}

static void rtw8852b_bb_set_tx_shape_dfir(struct rtw89_dev *rtwdev,
					  u8 tx_shape_idx,
					  enum rtw89_phy_idx phy_idx)
{
#define __DFIR_CFG_ADDR(i) (R_TXFIR0 + ((i) << 2))
#define __DFIR_CFG_MASK 0xffffffff
#define __DFIR_CFG_NR 8
#define __DECL_DFIR_PARAM(_name, _val...) \
	static const u32 param_ ## _name[] = {_val}; \
	static_assert(ARRAY_SIZE(param_ ## _name) == __DFIR_CFG_NR)

	__DECL_DFIR_PARAM(flat,
			  0x023D23FF, 0x0029B354, 0x000FC1C8, 0x00FDB053,
			  0x00F86F9A, 0x06FAEF92, 0x00FE5FCC, 0x00FFDFF5);
	__DECL_DFIR_PARAM(sharp,
			  0x023D83FF, 0x002C636A, 0x0013F204, 0x00008090,
			  0x00F87FB0, 0x06F99F83, 0x00FDBFBA, 0x00003FF5);
	__DECL_DFIR_PARAM(sharp_14,
			  0x023B13FF, 0x001C42DE, 0x00FDB0AD, 0x00F60F6E,
			  0x00FD8F92, 0x0602D011, 0x0001C02C, 0x00FFF00A);
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	const u32 *param;
	u32 addr;
	int i;

	if (ch > 14) {
		rtw89_warn(rtwdev,
			   "set tx shape dfir by unknown ch: %d on 2G\n", ch);
		return;
	}

	if (ch == 14)
		param = param_sharp_14;
	else
		param = tx_shape_idx == 0 ? param_flat : param_sharp;

	for (i = 0; i < __DFIR_CFG_NR; i++) {
		addr = __DFIR_CFG_ADDR(i);
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
			    "set tx shape dfir: 0x%x: 0x%x\n", addr, param[i]);
		rtw89_phy_write32_idx(rtwdev, addr, __DFIR_CFG_MASK, param[i],
				      phy_idx);
	}

#undef __DECL_DFIR_PARAM
#undef __DFIR_CFG_NR
#undef __DFIR_CFG_MASK
#undef __DECL_CFG_ADDR
}

static void rtw8852b_set_tx_shape(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan,
				  enum rtw89_phy_idx phy_idx)
{
	u8 band = chan->band_type;
	u8 regd = rtw89_regd_get(rtwdev, band);
	u8 tx_shape_cck = rtw89_8852b_tx_shape[band][RTW89_RS_CCK][regd];
	u8 tx_shape_ofdm = rtw89_8852b_tx_shape[band][RTW89_RS_OFDM][regd];

	if (band == RTW89_BAND_2G)
		rtw8852b_bb_set_tx_shape_dfir(rtwdev, tx_shape_cck, phy_idx);

	rtw89_phy_write32_mask(rtwdev, R_DCFO_OPT, B_TXSHAPE_TRIANGULAR_CFG,
			       tx_shape_ofdm);
}

static void rtw8852b_set_txpwr(struct rtw89_dev *rtwdev,
			       const struct rtw89_chan *chan,
			       enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_set_txpwr_byrate(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_offset(rtwdev, chan, phy_idx);
	rtw8852b_set_tx_shape(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit_ru(rtwdev, chan, phy_idx);
}

static void rtw8852b_set_txpwr_ctrl(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy_idx)
{
	rtw8852b_set_txpwr_ref(rtwdev, phy_idx);
}

static
void rtw8852b_set_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
				     s8 pw_ofst, enum rtw89_mac_idx mac_idx)
{
	u32 reg;

	if (pw_ofst < -16 || pw_ofst > 15) {
		rtw89_warn(rtwdev, "[ULTB] Err pwr_offset=%d\n", pw_ofst);
		return;
	}

	reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_CTRL, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_PWR_UL_TB_CTRL_EN);

	reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_1T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_1T_MASK, pw_ofst);

	pw_ofst = max_t(s8, pw_ofst - 3, -16);
	reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_2T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_1T_MASK, pw_ofst);
}

static int
rtw8852b_init_txpwr_unit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	int ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL2, 0x07763333);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_COEXT_CTRL, 0x01ebf000);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL0, 0x0002f8ff);
	if (ret)
		return ret;

	rtw8852b_set_txpwr_ul_tb_offset(rtwdev, 0, phy_idx == RTW89_PHY_1 ?
						   RTW89_MAC_1 : RTW89_MAC_0);

	return 0;
}

static int rtw8852b_mac_enable_bb_rf(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_write8_set(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x1);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0xC7,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0xC7,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	rtw89_write8(rtwdev, R_AX_PHYREG_SET, PHYREG_SET_XYN_CYCLE);

	return 0;
}

static int rtw8852b_mac_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	u8 wl_rfc_s0;
	u8 wl_rfc_s1;
	int ret;

	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, &wl_rfc_s0);
	if (ret)
		return ret;
	wl_rfc_s0 &= ~XTAL_SI_RF00S_EN;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, wl_rfc_s0,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, &wl_rfc_s1);
	if (ret)
		return ret;
	wl_rfc_s1 &= ~XTAL_SI_RF10S_EN;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, wl_rfc_s1,
				      FULL_BIT_MASK);
	return ret;
}

static const struct rtw89_chip_ops rtw8852b_chip_ops = {
	.enable_bb_rf		= rtw8852b_mac_enable_bb_rf,
	.disable_bb_rf		= rtw8852b_mac_disable_bb_rf,
	.set_txpwr		= rtw8852b_set_txpwr,
	.set_txpwr_ctrl		= rtw8852b_set_txpwr_ctrl,
	.init_txpwr_unit	= rtw8852b_init_txpwr_unit,
};

const struct rtw89_chip_info rtw8852b_chip_info = {
	.chip_id		= RTL8852B,
	.ops			= &rtw8852b_chip_ops,
	.fifo_size		= 196608,
	.dle_scc_rsvd_size	= 98304,
	.dle_mem		= rtw8852b_dle_mem_pcie,
	.dma_ch_mask		= BIT(RTW89_DMA_ACH4) | BIT(RTW89_DMA_ACH5) |
				  BIT(RTW89_DMA_ACH6) | BIT(RTW89_DMA_ACH7) |
				  BIT(RTW89_DMA_B1MG) | BIT(RTW89_DMA_B1HI),
};
EXPORT_SYMBOL(rtw8852b_chip_info);

MODULE_FIRMWARE("rtw89/rtw8852b_fw.bin");
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852B driver");
MODULE_LICENSE("Dual BSD/GPL");
