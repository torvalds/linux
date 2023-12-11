// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "debug.h"
#include "efuse.h"
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

static int rtw89_mac_check_mac_en_be(struct rtw89_dev *rtwdev, u8 mac_idx,
				     enum rtw89_mac_hwmod_sel sel)
{
	if (sel == RTW89_DMAC_SEL &&
	    test_bit(RTW89_FLAG_DMAC_FUNC, rtwdev->flags))
		return 0;
	if (sel == RTW89_CMAC_SEL && mac_idx == RTW89_MAC_0 &&
	    test_bit(RTW89_FLAG_CMAC0_FUNC, rtwdev->flags))
		return 0;
	if (sel == RTW89_CMAC_SEL && mac_idx == RTW89_MAC_1 &&
	    test_bit(RTW89_FLAG_CMAC1_FUNC, rtwdev->flags))
		return 0;

	return -EFAULT;
}

static void hfc_get_mix_info_be(struct rtw89_dev *rtwdev)
{
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	struct rtw89_hfc_prec_cfg *prec_cfg = &param->prec_cfg;
	struct rtw89_hfc_pub_cfg *pub_cfg = &param->pub_cfg;
	struct rtw89_hfc_pub_info *info = &param->pub_info;
	u32 val;

	val = rtw89_read32(rtwdev, R_BE_PUB_PAGE_INFO1);
	info->g0_used = u32_get_bits(val, B_BE_G0_USE_PG_MASK);
	info->g1_used = u32_get_bits(val, B_BE_G1_USE_PG_MASK);

	val = rtw89_read32(rtwdev, R_BE_PUB_PAGE_INFO3);
	info->g0_aval = u32_get_bits(val, B_BE_G0_AVAL_PG_MASK);
	info->g1_aval = u32_get_bits(val, B_BE_G1_AVAL_PG_MASK);
	info->pub_aval = u32_get_bits(rtw89_read32(rtwdev, R_BE_PUB_PAGE_INFO2),
				      B_BE_PUB_AVAL_PG_MASK);
	info->wp_aval = u32_get_bits(rtw89_read32(rtwdev, R_BE_WP_PAGE_INFO1),
				     B_BE_WP_AVAL_PG_MASK);

	val = rtw89_read32(rtwdev, R_BE_HCI_FC_CTRL);
	param->en = !!(val & B_BE_HCI_FC_EN);
	param->h2c_en = !!(val & B_BE_HCI_FC_CH12_EN);
	param->mode = u32_get_bits(val, B_BE_HCI_FC_MODE_MASK);
	prec_cfg->ch011_full_cond = u32_get_bits(val, B_BE_HCI_FC_WD_FULL_COND_MASK);
	prec_cfg->h2c_full_cond = u32_get_bits(val, B_BE_HCI_FC_CH12_FULL_COND_MASK);
	prec_cfg->wp_ch07_full_cond =
		u32_get_bits(val, B_BE_HCI_FC_WP_CH07_FULL_COND_MASK);
	prec_cfg->wp_ch811_full_cond =
		u32_get_bits(val, B_BE_HCI_FC_WP_CH811_FULL_COND_MASK);

	val = rtw89_read32(rtwdev, R_BE_CH_PAGE_CTRL);
	prec_cfg->ch011_prec = u32_get_bits(val, B_BE_PREC_PAGE_CH011_V1_MASK);
	prec_cfg->h2c_prec = u32_get_bits(val, B_BE_PREC_PAGE_CH12_V1_MASK);

	val = rtw89_read32(rtwdev, R_BE_PUB_PAGE_CTRL2);
	pub_cfg->pub_max = u32_get_bits(val, B_BE_PUBPG_ALL_MASK);

	val = rtw89_read32(rtwdev, R_BE_WP_PAGE_CTRL1);
	prec_cfg->wp_ch07_prec = u32_get_bits(val, B_BE_PREC_PAGE_WP_CH07_MASK);
	prec_cfg->wp_ch811_prec = u32_get_bits(val, B_BE_PREC_PAGE_WP_CH811_MASK);

	val = rtw89_read32(rtwdev, R_BE_WP_PAGE_CTRL2);
	pub_cfg->wp_thrd = u32_get_bits(val, B_BE_WP_THRD_MASK);

	val = rtw89_read32(rtwdev, R_BE_PUB_PAGE_CTRL1);
	pub_cfg->grp0 = u32_get_bits(val, B_BE_PUBPG_G0_MASK);
	pub_cfg->grp1 = u32_get_bits(val, B_BE_PUBPG_G1_MASK);
}

static void hfc_h2c_cfg_be(struct rtw89_dev *rtwdev)
{
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	const struct rtw89_hfc_prec_cfg *prec_cfg = &param->prec_cfg;
	u32 val;

	val = u32_encode_bits(prec_cfg->h2c_prec, B_BE_PREC_PAGE_CH12_V1_MASK);
	rtw89_write32(rtwdev, R_BE_CH_PAGE_CTRL, val);
}

static void hfc_mix_cfg_be(struct rtw89_dev *rtwdev)
{
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	const struct rtw89_hfc_prec_cfg *prec_cfg = &param->prec_cfg;
	const struct rtw89_hfc_pub_cfg *pub_cfg = &param->pub_cfg;
	u32 val;

	val = u32_encode_bits(prec_cfg->ch011_prec, B_BE_PREC_PAGE_CH011_V1_MASK) |
	      u32_encode_bits(prec_cfg->h2c_prec, B_BE_PREC_PAGE_CH12_V1_MASK);
	rtw89_write32(rtwdev, R_BE_CH_PAGE_CTRL, val);

	val = u32_encode_bits(pub_cfg->pub_max, B_BE_PUBPG_ALL_MASK);
	rtw89_write32(rtwdev, R_BE_PUB_PAGE_CTRL2, val);

	val = u32_encode_bits(prec_cfg->wp_ch07_prec, B_BE_PREC_PAGE_WP_CH07_MASK) |
	      u32_encode_bits(prec_cfg->wp_ch811_prec, B_BE_PREC_PAGE_WP_CH811_MASK);
	rtw89_write32(rtwdev, R_BE_WP_PAGE_CTRL1, val);

	val = u32_replace_bits(rtw89_read32(rtwdev, R_BE_HCI_FC_CTRL),
			       param->mode, B_BE_HCI_FC_MODE_MASK);
	val = u32_replace_bits(val, prec_cfg->ch011_full_cond,
			       B_BE_HCI_FC_WD_FULL_COND_MASK);
	val = u32_replace_bits(val, prec_cfg->h2c_full_cond,
			       B_BE_HCI_FC_CH12_FULL_COND_MASK);
	val = u32_replace_bits(val, prec_cfg->wp_ch07_full_cond,
			       B_BE_HCI_FC_WP_CH07_FULL_COND_MASK);
	val = u32_replace_bits(val, prec_cfg->wp_ch811_full_cond,
			       B_BE_HCI_FC_WP_CH811_FULL_COND_MASK);
	rtw89_write32(rtwdev, R_BE_HCI_FC_CTRL, val);
}

static void hfc_func_en_be(struct rtw89_dev *rtwdev, bool en, bool h2c_en)
{
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	u32 val;

	val = rtw89_read32(rtwdev, R_BE_HCI_FC_CTRL);
	param->en = en;
	param->h2c_en = h2c_en;
	val = en ? (val | B_BE_HCI_FC_EN) : (val & ~B_BE_HCI_FC_EN);
	val = h2c_en ? (val | B_BE_HCI_FC_CH12_EN) :
		       (val & ~B_BE_HCI_FC_CH12_EN);
	rtw89_write32(rtwdev, R_BE_HCI_FC_CTRL, val);
}

static void dle_func_en_be(struct rtw89_dev *rtwdev, bool enable)
{
	if (enable)
		rtw89_write32_set(rtwdev, R_BE_DMAC_FUNC_EN,
				  B_BE_DLE_WDE_EN | B_BE_DLE_PLE_EN);
	else
		rtw89_write32_clr(rtwdev, R_BE_DMAC_FUNC_EN,
				  B_BE_DLE_WDE_EN | B_BE_DLE_PLE_EN);
}

static void dle_clk_en_be(struct rtw89_dev *rtwdev, bool enable)
{
	if (enable)
		rtw89_write32_set(rtwdev, R_BE_DMAC_CLK_EN,
				  B_BE_DLE_WDE_CLK_EN | B_BE_DLE_PLE_CLK_EN);
	else
		rtw89_write32_clr(rtwdev, R_BE_DMAC_CLK_EN,
				  B_BE_DLE_WDE_CLK_EN | B_BE_DLE_PLE_CLK_EN);
}

static int dle_mix_cfg_be(struct rtw89_dev *rtwdev, const struct rtw89_dle_mem *cfg)
{
	const struct rtw89_dle_size *wde_size_cfg, *ple_size_cfg;
	u32 bound;
	u32 val;

	wde_size_cfg = cfg->wde_size;
	ple_size_cfg = cfg->ple_size;

	val = rtw89_read32(rtwdev, R_BE_WDE_PKTBUF_CFG);

	switch (wde_size_cfg->pge_size) {
	default:
	case RTW89_WDE_PG_64:
		val = u32_replace_bits(val, S_AX_WDE_PAGE_SEL_64,
				       B_BE_WDE_PAGE_SEL_MASK);
		break;
	case RTW89_WDE_PG_128:
		val = u32_replace_bits(val, S_AX_WDE_PAGE_SEL_128,
				       B_BE_WDE_PAGE_SEL_MASK);
		break;
	case RTW89_WDE_PG_256:
		rtw89_err(rtwdev, "[ERR]WDE DLE doesn't support 256 byte!\n");
		return -EINVAL;
	}

	bound = wde_size_cfg->srt_ofst / DLE_BOUND_UNIT;
	val = u32_replace_bits(val, bound, B_BE_WDE_START_BOUND_MASK);
	val = u32_replace_bits(val, wde_size_cfg->lnk_pge_num,
			       B_BE_WDE_FREE_PAGE_NUM_MASK);
	rtw89_write32(rtwdev, R_BE_WDE_PKTBUF_CFG, val);

	val = rtw89_read32(rtwdev, R_BE_PLE_PKTBUF_CFG);

	switch (ple_size_cfg->pge_size) {
	default:
	case RTW89_PLE_PG_64:
		rtw89_err(rtwdev, "[ERR]PLE DLE doesn't support 64 byte!\n");
		return -EINVAL;
	case RTW89_PLE_PG_128:
		val = u32_replace_bits(val, S_AX_PLE_PAGE_SEL_128,
				       B_BE_PLE_PAGE_SEL_MASK);
		break;
	case RTW89_PLE_PG_256:
		val = u32_replace_bits(val, S_AX_PLE_PAGE_SEL_256,
				       B_BE_PLE_PAGE_SEL_MASK);
		break;
	}

	bound = ple_size_cfg->srt_ofst / DLE_BOUND_UNIT;
	val = u32_replace_bits(val, bound, B_BE_PLE_START_BOUND_MASK);
	val = u32_replace_bits(val, ple_size_cfg->lnk_pge_num,
			       B_BE_PLE_FREE_PAGE_NUM_MASK);
	rtw89_write32(rtwdev, R_BE_PLE_PKTBUF_CFG, val);

	return 0;
}

static int chk_dle_rdy_be(struct rtw89_dev *rtwdev, bool wde_or_ple)
{
	u32 reg, mask;
	u32 ini;

	if (wde_or_ple) {
		reg = R_AX_WDE_INI_STATUS;
		mask = WDE_MGN_INI_RDY;
	} else {
		reg = R_AX_PLE_INI_STATUS;
		mask = PLE_MGN_INI_RDY;
	}

	return read_poll_timeout(rtw89_read32, ini, (ini & mask) == mask, 1,
				2000, false, rtwdev, reg);
}

#define INVALID_QT_WCPU U16_MAX
#define SET_QUOTA_VAL(_min_x, _max_x, _module, _idx)			\
	do {								\
		val = u32_encode_bits(_min_x, B_BE_ ## _module ## _Q ## _idx ## _MIN_SIZE_MASK) | \
		      u32_encode_bits(_max_x, B_BE_ ## _module ## _Q ## _idx ## _MAX_SIZE_MASK);  \
		rtw89_write32(rtwdev,					\
			      R_BE_ ## _module ## _QTA ## _idx ## _CFG,	\
			      val);					\
	} while (0)
#define SET_QUOTA(_x, _module, _idx)					\
	SET_QUOTA_VAL(min_cfg->_x, max_cfg->_x, _module, _idx)

static void wde_quota_cfg_be(struct rtw89_dev *rtwdev,
			     const struct rtw89_wde_quota *min_cfg,
			     const struct rtw89_wde_quota *max_cfg,
			     u16 ext_wde_min_qt_wcpu)
{
	u16 min_qt_wcpu = ext_wde_min_qt_wcpu != INVALID_QT_WCPU ?
			  ext_wde_min_qt_wcpu : min_cfg->wcpu;
	u16 max_qt_wcpu = max(max_cfg->wcpu, min_qt_wcpu);
	u32 val;

	SET_QUOTA(hif, WDE, 0);
	SET_QUOTA_VAL(min_qt_wcpu, max_qt_wcpu, WDE, 1);
	SET_QUOTA_VAL(0, 0, WDE, 2);
	SET_QUOTA(pkt_in, WDE, 3);
	SET_QUOTA(cpu_io, WDE, 4);
}

static void ple_quota_cfg_be(struct rtw89_dev *rtwdev,
			     const struct rtw89_ple_quota *min_cfg,
			     const struct rtw89_ple_quota *max_cfg)
{
	u32 val;

	SET_QUOTA(cma0_tx, PLE, 0);
	SET_QUOTA(cma1_tx, PLE, 1);
	SET_QUOTA(c2h, PLE, 2);
	SET_QUOTA(h2c, PLE, 3);
	SET_QUOTA(wcpu, PLE, 4);
	SET_QUOTA(mpdu_proc, PLE, 5);
	SET_QUOTA(cma0_dma, PLE, 6);
	SET_QUOTA(cma1_dma, PLE, 7);
	SET_QUOTA(bb_rpt, PLE, 8);
	SET_QUOTA(wd_rel, PLE, 9);
	SET_QUOTA(cpu_io, PLE, 10);
	SET_QUOTA(tx_rpt, PLE, 11);
	SET_QUOTA(h2d, PLE, 12);
}

static void rtw89_mac_hci_func_en_be(struct rtw89_dev *rtwdev)
{
	rtw89_write32_set(rtwdev, R_BE_HCI_FUNC_EN, B_BE_HCI_TXDMA_EN |
						    B_BE_HCI_RXDMA_EN);
}

static void rtw89_mac_dmac_func_pre_en_be(struct rtw89_dev *rtwdev)
{
	u32 val;

	val = rtw89_read32(rtwdev, R_BE_HAXI_INIT_CFG1);

	switch (rtwdev->hci.type) {
	case RTW89_HCI_TYPE_PCIE:
		val = u32_replace_bits(val, S_BE_DMA_MOD_PCIE_NO_DATA_CPU,
				       B_BE_DMA_MODE_MASK);
		break;
	case RTW89_HCI_TYPE_USB:
		val = u32_replace_bits(val, S_BE_DMA_MOD_USB, B_BE_DMA_MODE_MASK);
		val = (val & ~B_BE_STOP_AXI_MST) | B_BE_TXDMA_EN | B_BE_RXDMA_EN;
		break;
	case RTW89_HCI_TYPE_SDIO:
		val = u32_replace_bits(val, S_BE_DMA_MOD_SDIO, B_BE_DMA_MODE_MASK);
		val = (val & ~B_BE_STOP_AXI_MST) | B_BE_TXDMA_EN | B_BE_RXDMA_EN;
		break;
	default:
		return;
	}

	rtw89_write32(rtwdev, R_BE_HAXI_INIT_CFG1, val);

	rtw89_write32_clr(rtwdev, R_BE_HAXI_DMA_STOP1,
			  B_BE_STOP_CH0 | B_BE_STOP_CH1 | B_BE_STOP_CH2 |
			  B_BE_STOP_CH3 | B_BE_STOP_CH4 | B_BE_STOP_CH5 |
			  B_BE_STOP_CH6 | B_BE_STOP_CH7 | B_BE_STOP_CH8 |
			  B_BE_STOP_CH9 | B_BE_STOP_CH10 | B_BE_STOP_CH11 |
			  B_BE_STOP_CH12 | B_BE_STOP_CH13 | B_BE_STOP_CH14);

	rtw89_write32_set(rtwdev, R_BE_DMAC_TABLE_CTRL, B_BE_DMAC_ADDR_MODE);
}

static
int rtw89_mac_write_xtal_si_be(struct rtw89_dev *rtwdev, u8 offset, u8 val, u8 mask)
{
	u32 val32;
	int ret;

	val32 = u32_encode_bits(offset, B_BE_WL_XTAL_SI_ADDR_MASK) |
		u32_encode_bits(val, B_BE_WL_XTAL_SI_DATA_MASK) |
		u32_encode_bits(mask, B_BE_WL_XTAL_SI_BITMASK_MASK) |
		u32_encode_bits(XTAL_SI_NORMAL_WRITE, B_BE_WL_XTAL_SI_MODE_MASK) |
		u32_encode_bits(0, B_BE_WL_XTAL_SI_CHIPID_MASK) |
		B_BE_WL_XTAL_SI_CMD_POLL;
	rtw89_write32(rtwdev, R_BE_WLAN_XTAL_SI_CTRL, val32);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_BE_WL_XTAL_SI_CMD_POLL),
				50, 50000, false, rtwdev, R_BE_WLAN_XTAL_SI_CTRL);
	if (ret) {
		rtw89_warn(rtwdev, "xtal si not ready(W): offset=%x val=%x mask=%x\n",
			   offset, val, mask);
		return ret;
	}

	return 0;
}

static
int rtw89_mac_read_xtal_si_be(struct rtw89_dev *rtwdev, u8 offset, u8 *val)
{
	u32 val32;
	int ret;

	val32 = u32_encode_bits(offset, B_BE_WL_XTAL_SI_ADDR_MASK) |
		u32_encode_bits(0x0, B_BE_WL_XTAL_SI_DATA_MASK) |
		u32_encode_bits(0x0, B_BE_WL_XTAL_SI_BITMASK_MASK) |
		u32_encode_bits(XTAL_SI_NORMAL_READ, B_BE_WL_XTAL_SI_MODE_MASK) |
		u32_encode_bits(0, B_BE_WL_XTAL_SI_CHIPID_MASK) |
		B_BE_WL_XTAL_SI_CMD_POLL;
	rtw89_write32(rtwdev, R_BE_WLAN_XTAL_SI_CTRL, val32);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_BE_WL_XTAL_SI_CMD_POLL),
				50, 50000, false, rtwdev, R_BE_WLAN_XTAL_SI_CTRL);
	if (ret) {
		rtw89_warn(rtwdev, "xtal si not ready(R): offset=%x\n", offset);
		return ret;
	}

	*val = rtw89_read8(rtwdev, R_BE_WLAN_XTAL_SI_CTRL + 1);

	return 0;
}

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

	val32 = rtw89_read32(rtwdev, R_BE_HISR0);
	rtw89_write32(rtwdev, R_BE_HISR0, B_BE_HALT_C2H_INT);
	rtw89_debug(rtwdev, RTW89_DBG_SER, "HISR0=0x%x\n", val32);

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

static int dle_buf_req_be(struct rtw89_dev *rtwdev, u16 buf_len, bool wd, u16 *pkt_id)
{
	u32 val, reg;
	int ret;

	reg = wd ? R_BE_WD_BUF_REQ : R_BE_PL_BUF_REQ;
	val = buf_len;
	val |= B_BE_WD_BUF_REQ_EXEC;
	rtw89_write32(rtwdev, reg, val);

	reg = wd ? R_BE_WD_BUF_STATUS : R_BE_PL_BUF_STATUS;

	ret = read_poll_timeout(rtw89_read32, val, val & B_BE_WD_BUF_STAT_DONE,
				1, 2000, false, rtwdev, reg);
	if (ret)
		return ret;

	*pkt_id = u32_get_bits(val, B_BE_WD_BUF_STAT_PKTID_MASK);
	if (*pkt_id == S_WD_BUF_STAT_PKTID_INVALID)
		return -ENOENT;

	return 0;
}

static int set_cpuio_be(struct rtw89_dev *rtwdev,
			struct rtw89_cpuio_ctrl *ctrl_para, bool wd)
{
	u32 val_op0, val_op1, val_op2, val_op3;
	u32 val, cmd_type, reg;
	int ret;

	cmd_type = ctrl_para->cmd_type;

	reg = wd ? R_BE_WD_CPUQ_OP_3 : R_BE_PL_CPUQ_OP_3;
	val_op3 = u32_replace_bits(0, ctrl_para->start_pktid,
				   B_BE_WD_CPUQ_OP_STRT_PKTID_MASK);
	val_op3 = u32_replace_bits(val_op3, ctrl_para->end_pktid,
				   B_BE_WD_CPUQ_OP_END_PKTID_MASK);
	rtw89_write32(rtwdev, reg, val_op3);

	reg = wd ? R_BE_WD_CPUQ_OP_1 : R_BE_PL_CPUQ_OP_1;
	val_op1 = u32_replace_bits(0, ctrl_para->src_pid,
				   B_BE_WD_CPUQ_OP_SRC_PID_MASK);
	val_op1 = u32_replace_bits(val_op1, ctrl_para->src_qid,
				   B_BE_WD_CPUQ_OP_SRC_QID_MASK);
	val_op1 = u32_replace_bits(val_op1, ctrl_para->macid,
				   B_BE_WD_CPUQ_OP_SRC_MACID_MASK);
	rtw89_write32(rtwdev, reg, val_op1);

	reg = wd ? R_BE_WD_CPUQ_OP_2 : R_BE_PL_CPUQ_OP_2;
	val_op2 = u32_replace_bits(0, ctrl_para->dst_pid,
				   B_BE_WD_CPUQ_OP_DST_PID_MASK);
	val_op2 = u32_replace_bits(val_op2, ctrl_para->dst_qid,
				   B_BE_WD_CPUQ_OP_DST_QID_MASK);
	val_op2 = u32_replace_bits(val_op2, ctrl_para->macid,
				   B_BE_WD_CPUQ_OP_DST_MACID_MASK);
	rtw89_write32(rtwdev, reg, val_op2);

	reg = wd ? R_BE_WD_CPUQ_OP_0 : R_BE_PL_CPUQ_OP_0;
	val_op0 = u32_replace_bits(0, cmd_type,
				   B_BE_WD_CPUQ_OP_CMD_TYPE_MASK);
	val_op0 = u32_replace_bits(val_op0, ctrl_para->pkt_num,
				   B_BE_WD_CPUQ_OP_PKTNUM_MASK);
	val_op0 |= B_BE_WD_CPUQ_OP_EXEC;
	rtw89_write32(rtwdev, reg, val_op0);

	reg = wd ? R_BE_WD_CPUQ_OP_STATUS : R_BE_PL_CPUQ_OP_STATUS;

	ret = read_poll_timeout(rtw89_read32, val, val & B_BE_WD_CPUQ_OP_STAT_DONE,
				1, 2000, false, rtwdev, reg);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]set cpuio wd timeout\n");
		rtw89_err(rtwdev, "[ERR]op_0=0x%X, op_1=0x%X, op_2=0x%X\n",
			  val_op0, val_op1, val_op2);
		return ret;
	}

	if (cmd_type == CPUIO_OP_CMD_GET_NEXT_PID ||
	    cmd_type == CPUIO_OP_CMD_GET_1ST_PID)
		ctrl_para->pktid = u32_get_bits(val, B_BE_WD_CPUQ_OP_PKTID_MASK);

	return 0;
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

static void dump_err_status_dispatcher_be(struct rtw89_dev *rtwdev)
{
	rtw89_info(rtwdev, "R_BE_DISP_HOST_IMR=0x%08x ",
		   rtw89_read32(rtwdev, R_BE_DISP_HOST_IMR));
	rtw89_info(rtwdev, "R_BE_DISP_ERROR_ISR1=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_DISP_ERROR_ISR1));
	rtw89_info(rtwdev, "R_BE_DISP_CPU_IMR=0x%08x ",
		   rtw89_read32(rtwdev, R_BE_DISP_CPU_IMR));
	rtw89_info(rtwdev, "R_BE_DISP_ERROR_ISR2=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_DISP_ERROR_ISR2));
	rtw89_info(rtwdev, "R_BE_DISP_OTHER_IMR=0x%08x ",
		   rtw89_read32(rtwdev, R_BE_DISP_OTHER_IMR));
	rtw89_info(rtwdev, "R_BE_DISP_ERROR_ISR0=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_DISP_ERROR_ISR0));
}

static void rtw89_mac_dump_qta_lost_be(struct rtw89_dev *rtwdev)
{
	struct rtw89_mac_dle_dfi_qempty qempty;
	struct rtw89_mac_dle_dfi_quota quota;
	struct rtw89_mac_dle_dfi_ctrl ctrl;
	u32 val, not_empty, i;
	int ret;

	qempty.dle_type = DLE_CTRL_TYPE_PLE;
	qempty.grpsel = 0;
	qempty.qempty = ~(u32)0;
	ret = rtw89_mac_dle_dfi_qempty_cfg(rtwdev, &qempty);
	if (ret)
		rtw89_warn(rtwdev, "%s: query DLE fail\n", __func__);
	else
		rtw89_info(rtwdev, "DLE group0 empty: 0x%x\n", qempty.qempty);

	for (not_empty = ~qempty.qempty, i = 0; not_empty != 0; not_empty >>= 1, i++) {
		if (!(not_empty & BIT(0)))
			continue;
		ctrl.type = DLE_CTRL_TYPE_PLE;
		ctrl.target = DLE_DFI_TYPE_QLNKTBL;
		ctrl.addr = (QLNKTBL_ADDR_INFO_SEL_0 ? QLNKTBL_ADDR_INFO_SEL : 0) |
			    u32_encode_bits(i, QLNKTBL_ADDR_TBL_IDX_MASK);
		ret = rtw89_mac_dle_dfi_cfg(rtwdev, &ctrl);
		if (ret)
			rtw89_warn(rtwdev, "%s: query DLE fail\n", __func__);
		else
			rtw89_info(rtwdev, "qidx%d pktcnt = %d\n", i,
				   u32_get_bits(ctrl.out_data,
						QLNKTBL_DATA_SEL1_PKT_CNT_MASK));
	}

	quota.dle_type = DLE_CTRL_TYPE_PLE;
	quota.qtaid = 6;
	ret = rtw89_mac_dle_dfi_quota_cfg(rtwdev, &quota);
	if (ret)
		rtw89_warn(rtwdev, "%s: query DLE fail\n", __func__);
	else
		rtw89_info(rtwdev, "quota6 rsv/use: 0x%x/0x%x\n",
			   quota.rsv_pgnum, quota.use_pgnum);

	val = rtw89_read32(rtwdev, R_BE_PLE_QTA6_CFG);
	rtw89_info(rtwdev, "[PLE][CMAC0_RX]min_pgnum=0x%x\n",
		   u32_get_bits(val, B_BE_PLE_Q6_MIN_SIZE_MASK));
	rtw89_info(rtwdev, "[PLE][CMAC0_RX]max_pgnum=0x%x\n",
		   u32_get_bits(val, B_BE_PLE_Q6_MAX_SIZE_MASK));
	val = rtw89_read32(rtwdev, R_BE_RX_FLTR_OPT);
	rtw89_info(rtwdev, "[PLE][CMAC0_RX]B_BE_RX_MPDU_MAX_LEN=0x%x\n",
		   u32_get_bits(val, B_BE_RX_MPDU_MAX_LEN_MASK));
	rtw89_info(rtwdev, "R_BE_RSP_CHK_SIG=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_RSP_CHK_SIG));
	rtw89_info(rtwdev, "R_BE_TRXPTCL_RESP_0=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_TRXPTCL_RESP_0));

	if (!rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_1, RTW89_CMAC_SEL)) {
		quota.dle_type = DLE_CTRL_TYPE_PLE;
		quota.qtaid = 7;
		ret = rtw89_mac_dle_dfi_quota_cfg(rtwdev, &quota);
		if (ret)
			rtw89_warn(rtwdev, "%s: query DLE fail\n", __func__);
		else
			rtw89_info(rtwdev, "quota7 rsv/use: 0x%x/0x%x\n",
				   quota.rsv_pgnum, quota.use_pgnum);

		val = rtw89_read32(rtwdev, R_BE_PLE_QTA7_CFG);
		rtw89_info(rtwdev, "[PLE][CMAC1_RX]min_pgnum=0x%x\n",
			   u32_get_bits(val, B_BE_PLE_Q7_MIN_SIZE_MASK));
		rtw89_info(rtwdev, "[PLE][CMAC1_RX]max_pgnum=0x%x\n",
			   u32_get_bits(val, B_BE_PLE_Q7_MAX_SIZE_MASK));
		val = rtw89_read32(rtwdev, R_BE_RX_FLTR_OPT_C1);
		rtw89_info(rtwdev, "[PLE][CMAC1_RX]B_BE_RX_MPDU_MAX_LEN=0x%x\n",
			   u32_get_bits(val, B_BE_RX_MPDU_MAX_LEN_MASK));
		rtw89_info(rtwdev, "R_BE_RSP_CHK_SIG_C1=0x%08x\n",
			   rtw89_read32(rtwdev, R_BE_RSP_CHK_SIG_C1));
		rtw89_info(rtwdev, "R_BE_TRXPTCL_RESP_0_C1=0x%08x\n",
			   rtw89_read32(rtwdev, R_BE_TRXPTCL_RESP_0_C1));
	}

	rtw89_info(rtwdev, "R_BE_DLE_EMPTY0=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_DLE_EMPTY0));
	rtw89_info(rtwdev, "R_BE_DLE_EMPTY1=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_DLE_EMPTY1));

	dump_err_status_dispatcher_be(rtwdev);
}

static void rtw89_mac_dump_cmac_err_status_be(struct rtw89_dev *rtwdev,
					      u8 band)
{
	u32 offset = 0;
	u32 cmac_err;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, band, RTW89_CMAC_SEL);
	if (ret) {
		rtw89_info(rtwdev, "[CMAC] : CMAC%d not enabled\n", band);
		return;
	}

	if (band)
		offset = RTW89_MAC_BE_BAND_REG_OFFSET;

	cmac_err = rtw89_read32(rtwdev, R_BE_CMAC_ERR_ISR + offset);
	rtw89_info(rtwdev, "R_BE_CMAC_ERR_ISR [%d]=0x%08x\n", band,
		   rtw89_read32(rtwdev, R_BE_CMAC_ERR_ISR + offset));
	rtw89_info(rtwdev, "R_BE_CMAC_FUNC_EN [%d]=0x%08x\n", band,
		   rtw89_read32(rtwdev, R_BE_CMAC_FUNC_EN + offset));
	rtw89_info(rtwdev, "R_BE_CK_EN [%d]=0x%08x\n", band,
		   rtw89_read32(rtwdev, R_BE_CK_EN + offset));

	if (cmac_err & B_BE_SCHEDULE_TOP_ERR_IND) {
		rtw89_info(rtwdev, "R_BE_SCHEDULE_ERR_IMR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_SCHEDULE_ERR_IMR + offset));
		rtw89_info(rtwdev, "R_BE_SCHEDULE_ERR_ISR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_SCHEDULE_ERR_ISR + offset));
	}

	if (cmac_err & B_BE_PTCL_TOP_ERR_IND) {
		rtw89_info(rtwdev, "R_BE_PTCL_IMR0 [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_PTCL_IMR0 + offset));
		rtw89_info(rtwdev, "R_BE_PTCL_ISR0 [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_PTCL_ISR0 + offset));
		rtw89_info(rtwdev, "R_BE_PTCL_IMR1 [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_PTCL_IMR1 + offset));
		rtw89_info(rtwdev, "R_BE_PTCL_ISR1 [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_PTCL_ISR1 + offset));
	}

	if (cmac_err & B_BE_DMA_TOP_ERR_IND) {
		rtw89_info(rtwdev, "R_BE_RX_ERROR_FLAG_IMR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_RX_ERROR_FLAG_IMR + offset));
		rtw89_info(rtwdev, "R_BE_RX_ERROR_FLAG [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_RX_ERROR_FLAG + offset));
		rtw89_info(rtwdev, "R_BE_TX_ERROR_FLAG_IMR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_TX_ERROR_FLAG_IMR + offset));
		rtw89_info(rtwdev, "R_BE_TX_ERROR_FLAG [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_TX_ERROR_FLAG + offset));
		rtw89_info(rtwdev, "R_BE_RX_ERROR_FLAG_IMR_1 [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_RX_ERROR_FLAG_IMR_1 + offset));
		rtw89_info(rtwdev, "R_BE_RX_ERROR_FLAG_1 [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_RX_ERROR_FLAG_1 + offset));
	}

	if (cmac_err & B_BE_PHYINTF_ERR_IND) {
		rtw89_info(rtwdev, "R_BE_PHYINFO_ERR_IMR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_PHYINFO_ERR_IMR_V1 + offset));
		rtw89_info(rtwdev, "R_BE_PHYINFO_ERR_ISR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_PHYINFO_ERR_ISR + offset));
	}

	if (cmac_err & B_AX_TXPWR_CTRL_ERR_IND) {
		rtw89_info(rtwdev, "R_BE_TXPWR_ERR_FLAG [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_TXPWR_ERR_FLAG + offset));
		rtw89_info(rtwdev, "R_BE_TXPWR_ERR_IMR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_TXPWR_ERR_IMR + offset));
	}

	if (cmac_err & (B_BE_WMAC_RX_ERR_IND | B_BE_WMAC_TX_ERR_IND |
			B_BE_WMAC_RX_IDLETO_IDCT | B_BE_PTCL_TX_IDLETO_IDCT)) {
		rtw89_info(rtwdev, "R_BE_DBGSEL_TRXPTCL [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_DBGSEL_TRXPTCL + offset));
		rtw89_info(rtwdev, "R_BE_TRXPTCL_ERROR_INDICA_MASK [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_TRXPTCL_ERROR_INDICA_MASK + offset));
		rtw89_info(rtwdev, "R_BE_TRXPTCL_ERROR_INDICA [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_TRXPTCL_ERROR_INDICA + offset));
		rtw89_info(rtwdev, "R_BE_RX_ERR_IMR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_RX_ERR_IMR + offset));
		rtw89_info(rtwdev, "R_BE_RX_ERR_ISR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_BE_RX_ERR_ISR + offset));
	}

	rtw89_info(rtwdev, "R_BE_CMAC_ERR_IMR [%d]=0x%08x\n", band,
		   rtw89_read32(rtwdev, R_BE_CMAC_ERR_IMR + offset));
}

static void rtw89_mac_dump_err_status_be(struct rtw89_dev *rtwdev,
					 enum mac_ax_err_info err)
{
	if (err != MAC_AX_ERR_L1_ERR_DMAC &&
	    err != MAC_AX_ERR_L0_PROMOTE_TO_L1 &&
	    err != MAC_AX_ERR_L0_ERR_CMAC0 &&
	    err != MAC_AX_ERR_L0_ERR_CMAC1 &&
	    err != MAC_AX_ERR_RXI300)
		return;

	rtw89_info(rtwdev, "--->\nerr=0x%x\n", err);
	rtw89_info(rtwdev, "R_BE_SER_DBG_INFO=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_DBG_INFO));
	rtw89_info(rtwdev, "R_BE_SER_L0_DBG_CNT=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L0_DBG_CNT));
	rtw89_info(rtwdev, "R_BE_SER_L0_DBG_CNT1=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L0_DBG_CNT1));
	rtw89_info(rtwdev, "R_BE_SER_L0_DBG_CNT2=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L0_DBG_CNT2));
	rtw89_info(rtwdev, "R_BE_SER_L0_DBG_CNT3=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L0_DBG_CNT3));
	if (!rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_1, RTW89_CMAC_SEL)) {
		rtw89_info(rtwdev, "R_BE_SER_L0_DBG_CNT_C1=0x%08x\n",
			   rtw89_read32(rtwdev, R_BE_SER_L0_DBG_CNT_C1));
		rtw89_info(rtwdev, "R_BE_SER_L0_DBG_CNT1_C1=0x%08x\n",
			   rtw89_read32(rtwdev, R_BE_SER_L0_DBG_CNT1_C1));
	}
	rtw89_info(rtwdev, "R_BE_SER_L1_DBG_CNT_0=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L1_DBG_CNT_0));
	rtw89_info(rtwdev, "R_BE_SER_L1_DBG_CNT_1=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L1_DBG_CNT_1));
	rtw89_info(rtwdev, "R_BE_SER_L1_DBG_CNT_2=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L1_DBG_CNT_2));
	rtw89_info(rtwdev, "R_BE_SER_L1_DBG_CNT_3=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L1_DBG_CNT_3));
	rtw89_info(rtwdev, "R_BE_SER_L1_DBG_CNT_4=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L1_DBG_CNT_4));
	rtw89_info(rtwdev, "R_BE_SER_L1_DBG_CNT_5=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L1_DBG_CNT_5));
	rtw89_info(rtwdev, "R_BE_SER_L1_DBG_CNT_6=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L1_DBG_CNT_6));
	rtw89_info(rtwdev, "R_BE_SER_L1_DBG_CNT_7=0x%08x\n",
		   rtw89_read32(rtwdev, R_BE_SER_L1_DBG_CNT_7));

	rtw89_mac_dump_dmac_err_status(rtwdev);
	rtw89_mac_dump_cmac_err_status_be(rtwdev, RTW89_MAC_0);
	rtw89_mac_dump_cmac_err_status_be(rtwdev, RTW89_MAC_1);

	rtwdev->hci.ops->dump_err_status(rtwdev);

	if (err == MAC_AX_ERR_L0_PROMOTE_TO_L1)
		rtw89_mac_dump_l0_to_l1(rtwdev, err);

	rtw89_info(rtwdev, "<---\n");
}

static bool mac_is_txq_empty_be(struct rtw89_dev *rtwdev)
{
	struct rtw89_mac_dle_dfi_qempty qempty;
	u32 val32, msk32;
	u32 grpnum;
	int ret;
	int i;

	grpnum = rtwdev->chip->wde_qempty_acq_grpnum;
	qempty.dle_type = DLE_CTRL_TYPE_WDE;

	for (i = 0; i < grpnum; i++) {
		qempty.grpsel = i;
		ret = rtw89_mac_dle_dfi_qempty_cfg(rtwdev, &qempty);
		if (ret) {
			rtw89_warn(rtwdev,
				   "%s: failed to dle dfi acq empty: %d\n",
				   __func__, ret);
			return false;
		}

		/* Each acq group contains 32 queues (8 macid * 4 acq),
		 * but here, we can simply check if all bits are set.
		 */
		if (qempty.qempty != MASKDWORD)
			return false;
	}

	qempty.grpsel = rtwdev->chip->wde_qempty_mgq_grpsel;
	ret = rtw89_mac_dle_dfi_qempty_cfg(rtwdev, &qempty);
	if (ret) {
		rtw89_warn(rtwdev, "%s: failed to dle dfi mgq empty: %d\n",
			   __func__, ret);
		return false;
	}

	msk32 = B_CMAC0_MGQ_NORMAL_BE | B_CMAC1_MGQ_NORMAL_BE;
	if ((qempty.qempty & msk32) != msk32)
		return false;

	msk32 = B_BE_WDE_EMPTY_QUE_OTHERS;
	val32 = rtw89_read32(rtwdev, R_BE_DLE_EMPTY0);
	return (val32 & msk32) == msk32;
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

	.check_mac_en = rtw89_mac_check_mac_en_be,
	.hci_func_en = rtw89_mac_hci_func_en_be,
	.dmac_func_pre_en = rtw89_mac_dmac_func_pre_en_be,
	.dle_func_en = dle_func_en_be,
	.dle_clk_en = dle_clk_en_be,
	.bf_assoc = rtw89_mac_bf_assoc_be,

	.dle_mix_cfg = dle_mix_cfg_be,
	.chk_dle_rdy = chk_dle_rdy_be,
	.dle_buf_req = dle_buf_req_be,
	.hfc_func_en = hfc_func_en_be,
	.hfc_h2c_cfg = hfc_h2c_cfg_be,
	.hfc_mix_cfg = hfc_mix_cfg_be,
	.hfc_get_mix_info = hfc_get_mix_info_be,
	.wde_quota_cfg = wde_quota_cfg_be,
	.ple_quota_cfg = ple_quota_cfg_be,
	.set_cpuio = set_cpuio_be,

	.disable_cpu = rtw89_mac_disable_cpu_be,
	.fwdl_enable_wcpu = rtw89_mac_fwdl_enable_wcpu_be,
	.fwdl_get_status = fwdl_get_status_be,
	.fwdl_check_path_ready = rtw89_fwdl_check_path_ready_be,
	.parse_efuse_map = rtw89_parse_efuse_map_be,
	.parse_phycap_map = rtw89_parse_phycap_map_be,
	.cnv_efuse_state = rtw89_cnv_efuse_state_be,

	.get_txpwr_cr = rtw89_mac_get_txpwr_cr_be,

	.write_xtal_si = rtw89_mac_write_xtal_si_be,
	.read_xtal_si = rtw89_mac_read_xtal_si_be,

	.dump_qta_lost = rtw89_mac_dump_qta_lost_be,
	.dump_err_status = rtw89_mac_dump_err_status_be,

	.is_txq_empty = mac_is_txq_empty_be,
};
EXPORT_SYMBOL(rtw89_mac_gen_be);
