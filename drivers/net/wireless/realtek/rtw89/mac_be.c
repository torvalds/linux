// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "reg.h"

static const u32 rtw89_mac_mem_base_addrs_be[RTW89_MAC_MEM_NUM] = {
	[RTW89_MAC_MEM_AXIDMA]	        = AXIDMA_BASE_ADDR_BE,
	[RTW89_MAC_MEM_SHARED_BUF]	= SHARED_BUF_BASE_ADDR_BE,
	[RTW89_MAC_MEM_DMAC_TBL]	= DMAC_TBL_BASE_ADDR_BE,
	[RTW89_MAC_MEM_SHCUT_MACHDR]	= SHCUT_MACHDR_BASE_ADDR_BE,
	[RTW89_MAC_MEM_STA_SCHED]	= STA_SCHED_BASE_ADDR_BE,
	[RTW89_MAC_MEM_RXPLD_FLTR_CAM]	= RXPLD_FLTR_CAM_BASE_ADDR_BE,
	[RTW89_MAC_MEM_SECURITY_CAM]	= SEC_CAM_BASE_ADDR_BE,
	[RTW89_MAC_MEM_WOW_CAM]		= WOW_CAM_BASE_ADDR_BE,
	[RTW89_MAC_MEM_CMAC_TBL]	= CMAC_TBL_BASE_ADDR_BE,
	[RTW89_MAC_MEM_ADDR_CAM]	= ADDR_CAM_BASE_ADDR_BE,
	[RTW89_MAC_MEM_BA_CAM]		= BA_CAM_BASE_ADDR_BE,
	[RTW89_MAC_MEM_BCN_IE_CAM0]	= BCN_IE_CAM0_BASE_ADDR_BE,
	[RTW89_MAC_MEM_BCN_IE_CAM1]	= BCN_IE_CAM1_BASE_ADDR_BE,
	[RTW89_MAC_MEM_TXD_FIFO_0]	= TXD_FIFO_0_BASE_ADDR_BE,
	[RTW89_MAC_MEM_TXD_FIFO_1]	= TXD_FIFO_1_BASE_ADDR_BE,
	[RTW89_MAC_MEM_TXDATA_FIFO_0]	= TXDATA_FIFO_0_BASE_ADDR_BE,
	[RTW89_MAC_MEM_TXDATA_FIFO_1]	= TXDATA_FIFO_1_BASE_ADDR_BE,
	[RTW89_MAC_MEM_CPU_LOCAL]	= CPU_LOCAL_BASE_ADDR_BE,
	[RTW89_MAC_MEM_BSSID_CAM]	= BSSID_CAM_BASE_ADDR_BE,
	[RTW89_MAC_MEM_WD_PAGE]		= WD_PAGE_BASE_ADDR_BE,
};

static const struct rtw89_port_reg rtw89_port_base_be = {
	.port_cfg = R_BE_PORT_CFG_P0,
	.tbtt_prohib = R_BE_TBTT_PROHIB_P0,
	.bcn_area = R_BE_BCN_AREA_P0,
	.bcn_early = R_BE_BCNERLYINT_CFG_P0,
	.tbtt_early = R_BE_TBTTERLYINT_CFG_P0,
	.tbtt_agg = R_BE_TBTT_AGG_P0,
	.bcn_space = R_BE_BCN_SPACE_CFG_P0,
	.bcn_forcetx = R_BE_BCN_FORCETX_P0,
	.bcn_err_cnt = R_BE_BCN_ERR_CNT_P0,
	.bcn_err_flag = R_BE_BCN_ERR_FLAG_P0,
	.dtim_ctrl = R_BE_DTIM_CTRL_P0,
	.tbtt_shift = R_BE_TBTT_SHIFT_P0,
	.bcn_cnt_tmr = R_BE_BCN_CNT_TMR_P0,
	.tsftr_l = R_BE_TSFTR_LOW_P0,
	.tsftr_h = R_BE_TSFTR_HIGH_P0,
	.md_tsft = R_BE_WMTX_MOREDATA_TSFT_STMP_CTL,
	.bss_color = R_BE_PTCL_BSS_COLOR_0,
	.mbssid = R_BE_MBSSID_CTRL,
	.mbssid_drop = R_BE_MBSSID_DROP_0,
	.tsf_sync = R_BE_PORT_0_TSF_SYNC,
	.hiq_win = {R_BE_P0MB_HGQ_WINDOW_CFG_0, R_BE_PORT_HGQ_WINDOW_CFG,
		    R_BE_PORT_HGQ_WINDOW_CFG + 1, R_BE_PORT_HGQ_WINDOW_CFG + 2,
		    R_BE_PORT_HGQ_WINDOW_CFG + 3},
};

static void rtw89_mac_disable_cpu_be(struct rtw89_dev *rtwdev)
{
	u32 val32;

	clear_bit(RTW89_FLAG_FW_RDY, rtwdev->flags);

	rtw89_write32_clr(rtwdev, R_BE_PLATFORM_ENABLE, B_BE_WCPU_EN);
	rtw89_write32_set(rtwdev, R_BE_PLATFORM_ENABLE, B_BE_HOLD_AFTER_RESET);
	rtw89_write32_set(rtwdev, R_BE_PLATFORM_ENABLE, B_BE_WCPU_EN);

	val32 = rtw89_read32(rtwdev, R_BE_WCPU_FW_CTRL);
	val32 &= B_BE_RUN_ENV_MASK;
	rtw89_write32(rtwdev, R_BE_WCPU_FW_CTRL, val32);

	rtw89_write32_set(rtwdev, R_BE_DCPU_PLATFORM_ENABLE, B_BE_DCPU_PLATFORM_EN);

	rtw89_write32(rtwdev, R_BE_UDM0, 0);
	rtw89_write32(rtwdev, R_BE_HALT_C2H, 0);
	rtw89_write32(rtwdev, R_BE_UDM2, 0);
}

static void set_cpu_en(struct rtw89_dev *rtwdev, bool include_bb)
{
	u32 set = B_BE_WLANCPU_FWDL_EN;

	if (include_bb)
		set |= B_BE_BBMCU0_FWDL_EN;

	rtw89_write32_set(rtwdev, R_BE_WCPU_FW_CTRL, set);
}

static int wcpu_on(struct rtw89_dev *rtwdev, u8 boot_reason, bool dlfw)
{
	u32 val32;
	int ret;

	rtw89_write32_set(rtwdev, R_BE_UDM0, B_BE_UDM0_DBG_MODE_CTRL);

	val32 = rtw89_read32(rtwdev, R_BE_HALT_C2H);
	if (val32) {
		rtw89_warn(rtwdev, "[SER] AON L2 Debug register not empty before Boot.\n");
		rtw89_warn(rtwdev, "[SER] %s: R_BE_HALT_C2H = 0x%x\n", __func__, val32);
	}
	val32 = rtw89_read32(rtwdev, R_BE_UDM1);
	if (val32) {
		rtw89_warn(rtwdev, "[SER] AON L2 Debug register not empty before Boot.\n");
		rtw89_warn(rtwdev, "[SER] %s: R_BE_UDM1 = 0x%x\n", __func__, val32);
	}
	val32 = rtw89_read32(rtwdev, R_BE_UDM2);
	if (val32) {
		rtw89_warn(rtwdev, "[SER] AON L2 Debug register not empty before Boot.\n");
		rtw89_warn(rtwdev, "[SER] %s: R_BE_UDM2 = 0x%x\n", __func__, val32);
	}

	rtw89_write32(rtwdev, R_BE_UDM1, 0);
	rtw89_write32(rtwdev, R_BE_UDM2, 0);
	rtw89_write32(rtwdev, R_BE_HALT_H2C, 0);
	rtw89_write32(rtwdev, R_BE_HALT_C2H, 0);
	rtw89_write32(rtwdev, R_BE_HALT_H2C_CTRL, 0);
	rtw89_write32(rtwdev, R_BE_HALT_C2H_CTRL, 0);

	rtw89_write32_set(rtwdev, R_BE_SYS_CLK_CTRL, B_BE_CPU_CLK_EN);
	rtw89_write32_clr(rtwdev, R_BE_SYS_CFG5,
			  B_BE_WDT_WAKE_PCIE_EN | B_BE_WDT_WAKE_USB_EN);
	rtw89_write32_clr(rtwdev, R_BE_WCPU_FW_CTRL,
			  B_BE_WDT_PLT_RST_EN | B_BE_WCPU_ROM_CUT_GET);

	rtw89_write16_mask(rtwdev, R_BE_BOOT_REASON, B_BE_BOOT_REASON_MASK, boot_reason);
	rtw89_write32_clr(rtwdev, R_BE_PLATFORM_ENABLE, B_BE_WCPU_EN);
	rtw89_write32_clr(rtwdev, R_BE_PLATFORM_ENABLE, B_BE_HOLD_AFTER_RESET);
	rtw89_write32_set(rtwdev, R_BE_PLATFORM_ENABLE, B_BE_WCPU_EN);

	if (!dlfw) {
		ret = rtw89_fw_check_rdy(rtwdev, RTW89_FWDL_CHECK_FREERTOS_DONE);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtw89_mac_fwdl_enable_wcpu_be(struct rtw89_dev *rtwdev,
					 u8 boot_reason, bool dlfw,
					 bool include_bb)
{
	set_cpu_en(rtwdev, include_bb);

	return wcpu_on(rtwdev, boot_reason, dlfw);
}

static const u8 fwdl_status_map[] = {
	[0] = RTW89_FWDL_INITIAL_STATE,
	[1] = RTW89_FWDL_FWDL_ONGOING,
	[4] = RTW89_FWDL_CHECKSUM_FAIL,
	[5] = RTW89_FWDL_SECURITY_FAIL,
	[6] = RTW89_FWDL_SECURITY_FAIL,
	[7] = RTW89_FWDL_CV_NOT_MATCH,
	[8] = RTW89_FWDL_RSVD0,
	[2] = RTW89_FWDL_WCPU_FWDL_RDY,
	[3] = RTW89_FWDL_WCPU_FW_INIT_RDY,
	[9] = RTW89_FWDL_RSVD0,
};

static u8 fwdl_get_status_be(struct rtw89_dev *rtwdev, enum rtw89_fwdl_check_type type)
{
	bool check_pass = false;
	u32 val32;
	u8 st;

	val32 = rtw89_read32(rtwdev, R_BE_WCPU_FW_CTRL);

	switch (type) {
	case RTW89_FWDL_CHECK_WCPU_FWDL_DONE:
		check_pass = !(val32 & B_BE_WLANCPU_FWDL_EN);
		break;
	case RTW89_FWDL_CHECK_DCPU_FWDL_DONE:
		check_pass = !(val32 & B_BE_DATACPU_FWDL_EN);
		break;
	case RTW89_FWDL_CHECK_BB0_FWDL_DONE:
		check_pass = !(val32 & B_BE_BBMCU0_FWDL_EN);
		break;
	case RTW89_FWDL_CHECK_BB1_FWDL_DONE:
		check_pass = !(val32 & B_BE_BBMCU1_FWDL_EN);
		break;
	default:
		break;
	}

	if (check_pass)
		return RTW89_FWDL_WCPU_FW_INIT_RDY;

	st = u32_get_bits(val32, B_BE_WCPU_FWDL_STATUS_MASK);
	if (st < ARRAY_SIZE(fwdl_status_map))
		return fwdl_status_map[st];

	return st;
}

static int rtw89_fwdl_check_path_ready_be(struct rtw89_dev *rtwdev,
					  bool h2c_or_fwdl)
{
	u32 check = h2c_or_fwdl ? B_BE_H2C_PATH_RDY : B_BE_DLFW_PATH_RDY;
	u32 val;

	return read_poll_timeout_atomic(rtw89_read32, val, val & check,
					1, 1000000, false,
					rtwdev, R_BE_WCPU_FW_CTRL);
}

static bool rtw89_mac_get_txpwr_cr_be(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx,
				      u32 reg_base, u32 *cr)
{
	const struct rtw89_dle_mem *dle_mem = rtwdev->chip->dle_mem;
	enum rtw89_qta_mode mode = dle_mem->mode;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, (enum rtw89_mac_idx)phy_idx,
				     RTW89_CMAC_SEL);
	if (ret) {
		if (test_bit(RTW89_FLAG_SER_HANDLING, rtwdev->flags))
			return false;

		rtw89_err(rtwdev, "[TXPWR] check mac enable failed\n");
		return false;
	}

	if (reg_base < R_BE_PWR_MODULE || reg_base > R_BE_CMAC_FUNC_EN_C1) {
		rtw89_err(rtwdev, "[TXPWR] reg_base=0x%x exceed txpwr cr\n",
			  reg_base);
		return false;
	}

	*cr = rtw89_mac_reg_by_idx(rtwdev, reg_base, phy_idx);

	if (*cr >= CMAC1_START_ADDR_BE && *cr <= CMAC1_END_ADDR_BE) {
		if (mode == RTW89_QTA_SCC) {
			rtw89_err(rtwdev,
				  "[TXPWR] addr=0x%x but hw not enable\n",
				  *cr);
			return false;
		}
	}

	return true;
}

static int rtw89_mac_init_bfee_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;
	u32 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	rtw89_mac_bfee_ctrl(rtwdev, mac_idx, true);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_CTRL_0, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_BE_BFMEE_BFPARAM_SEL |
				       B_BE_BFMEE_USE_NSTS |
				       B_BE_BFMEE_CSI_GID_SEL |
				       B_BE_BFMEE_CSI_FORCE_RETE_EN);
	rtw89_write32_mask(rtwdev, reg, B_BE_BFMEE_CSI_RSC_MASK, CSI_RX_BW_CFG);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_CSIRPT_OPTION, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_BE_CSIPRT_VHTSU_AID_EN |
				       B_BE_CSIPRT_HESU_AID_EN |
				       B_BE_CSIPRT_EHTSU_AID_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_RRSC, mac_idx);
	rtw89_write32(rtwdev, reg, CSI_RRSC_BMAP_BE);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_CTRL_1, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_BE_BFMEE_BE_CSI_RRSC_BITMAP_MASK,
			   CSI_RRSC_BITMAP_CFG);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_RATE, mac_idx);
	val = u32_encode_bits(CSI_INIT_RATE_HT, B_BE_BFMEE_HT_CSI_RATE_MASK) |
	      u32_encode_bits(CSI_INIT_RATE_VHT, B_BE_BFMEE_VHT_CSI_RATE_MASK) |
	      u32_encode_bits(CSI_INIT_RATE_HE, B_BE_BFMEE_HE_CSI_RATE_MASK) |
	      u32_encode_bits(CSI_INIT_RATE_EHT, B_BE_BFMEE_EHT_CSI_RATE_MASK);

	rtw89_write32(rtwdev, reg, val);

	return 0;
}

static int rtw89_mac_set_csi_para_reg_be(struct rtw89_dev *rtwdev,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	u8 nc = 1, nr = 3, ng = 0, cb = 1, cs = 1, ldpc_en = 1, stbc_en = 1;
	u8 mac_idx = rtwvif->mac_idx;
	u8 port_sel = rtwvif->port;
	u8 sound_dim = 3, t;
	u8 *phy_cap;
	u32 reg;
	u16 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	phy_cap = sta->deflink.he_cap.he_cap_elem.phy_cap_info;

	if ((phy_cap[3] & IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER) ||
	    (phy_cap[4] & IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER)) {
		ldpc_en &= !!(phy_cap[1] & IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD);
		stbc_en &= !!(phy_cap[2] & IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ);
		t = u8_get_bits(phy_cap[5],
				IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK);
		sound_dim = min(sound_dim, t);
	}

	if ((sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE) ||
	    (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE)) {
		ldpc_en &= !!(sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC);
		stbc_en &= !!(sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_MASK);
		t = u32_get_bits(sta->deflink.vht_cap.cap,
				 IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK);
		sound_dim = min(sound_dim, t);
	}

	nc = min(nc, sound_dim);
	nr = min(nr, sound_dim);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_CTRL_0, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_BE_BFMEE_BFPARAM_SEL);

	val = u16_encode_bits(nc, B_BE_BFMEE_CSIINFO0_NC_MASK) |
	      u16_encode_bits(nr, B_BE_BFMEE_CSIINFO0_NR_MASK) |
	      u16_encode_bits(ng, B_BE_BFMEE_CSIINFO0_NG_MASK) |
	      u16_encode_bits(cb, B_BE_BFMEE_CSIINFO0_CB_MASK) |
	      u16_encode_bits(cs, B_BE_BFMEE_CSIINFO0_CS_MASK) |
	      u16_encode_bits(ldpc_en, B_BE_BFMEE_CSIINFO0_LDPC_EN) |
	      u16_encode_bits(stbc_en, B_BE_BFMEE_CSIINFO0_STBC_EN);

	if (port_sel == 0)
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_CTRL_0,
					   mac_idx);
	else
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_CTRL_1,
					   mac_idx);

	rtw89_write16(rtwdev, reg, val);

	return 0;
}

static int rtw89_mac_csi_rrsc_be(struct rtw89_dev *rtwdev,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	u32 rrsc = BIT(RTW89_MAC_BF_RRSC_6M) | BIT(RTW89_MAC_BF_RRSC_24M);
	u8 mac_idx = rtwvif->mac_idx;
	int ret;
	u32 reg;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (sta->deflink.he_cap.has_he) {
		rrsc |= (BIT(RTW89_MAC_BF_RRSC_HE_MSC0) |
			 BIT(RTW89_MAC_BF_RRSC_HE_MSC3) |
			 BIT(RTW89_MAC_BF_RRSC_HE_MSC5));
	}
	if (sta->deflink.vht_cap.vht_supported) {
		rrsc |= (BIT(RTW89_MAC_BF_RRSC_VHT_MSC0) |
			 BIT(RTW89_MAC_BF_RRSC_VHT_MSC3) |
			 BIT(RTW89_MAC_BF_RRSC_VHT_MSC5));
	}
	if (sta->deflink.ht_cap.ht_supported) {
		rrsc |= (BIT(RTW89_MAC_BF_RRSC_HT_MSC0) |
			 BIT(RTW89_MAC_BF_RRSC_HT_MSC3) |
			 BIT(RTW89_MAC_BF_RRSC_HT_MSC5));
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_CTRL_0, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_BE_BFMEE_BFPARAM_SEL);
	rtw89_write32_clr(rtwdev, reg, B_BE_BFMEE_CSI_FORCE_RETE_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_RRSC, mac_idx);
	rtw89_write32(rtwdev, reg, rrsc);

	return 0;
}

static void rtw89_mac_bf_assoc_be(struct rtw89_dev *rtwdev,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	if (rtw89_sta_has_beamformer_cap(sta)) {
		rtw89_debug(rtwdev, RTW89_DBG_BF,
			    "initialize bfee for new association\n");
		rtw89_mac_init_bfee_be(rtwdev, rtwvif->mac_idx);
		rtw89_mac_set_csi_para_reg_be(rtwdev, vif, sta);
		rtw89_mac_csi_rrsc_be(rtwdev, vif, sta);
	}
}

const struct rtw89_mac_gen_def rtw89_mac_gen_be = {
	.band1_offset = RTW89_MAC_BE_BAND_REG_OFFSET,
	.filter_model_addr = R_BE_FILTER_MODEL_ADDR,
	.indir_access_addr = R_BE_INDIR_ACCESS_ENTRY,
	.mem_base_addrs = rtw89_mac_mem_base_addrs_be,
	.rx_fltr = R_BE_RX_FLTR_OPT,
	.port_base = &rtw89_port_base_be,
	.agg_len_ht = R_BE_AGG_LEN_HT_0,

	.muedca_ctrl = {
		.addr = R_BE_MUEDCA_EN,
		.mask = B_BE_MUEDCA_EN_0 | B_BE_SET_MUEDCATIMER_TF_0,
	},
	.bfee_ctrl = {
		.addr = R_BE_BFMEE_RESP_OPTION,
		.mask = B_BE_BFMEE_HT_NDPA_EN | B_BE_BFMEE_VHT_NDPA_EN |
			B_BE_BFMEE_HE_NDPA_EN | B_BE_BFMEE_EHT_NDPA_EN,
	},

	.bf_assoc = rtw89_mac_bf_assoc_be,

	.disable_cpu = rtw89_mac_disable_cpu_be,
	.fwdl_enable_wcpu = rtw89_mac_fwdl_enable_wcpu_be,
	.fwdl_get_status = fwdl_get_status_be,
	.fwdl_check_path_ready = rtw89_fwdl_check_path_ready_be,

	.get_txpwr_cr = rtw89_mac_get_txpwr_cr_be,
};
EXPORT_SYMBOL(rtw89_mac_gen_be);
