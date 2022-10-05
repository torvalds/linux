// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "core.h"
#include "mac.h"
#include "reg.h"

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
};

const struct rtw89_chip_info rtw8852b_chip_info = {
	.chip_id		= RTL8852B,
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
