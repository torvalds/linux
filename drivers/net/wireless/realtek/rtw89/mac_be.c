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
	.ptcl_dbg = R_BE_PTCL_DBG,
	.ptcl_dbg_info = R_BE_PTCL_DBG_INFO,
	.bcn_drop_all = R_BE_BCN_DROP_ALL0,
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

static bool is_qta_poh(struct rtw89_dev *rtwdev)
{
	return rtwdev->hci.type == RTW89_HCI_TYPE_PCIE;
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

static int dmac_func_en_be(struct rtw89_dev *rtwdev)
{
	return 0;
}

static int cmac_func_en_be(struct rtw89_dev *rtwdev, u8 mac_idx, bool en)
{
	u32 reg;

	if (mac_idx > RTW89_MAC_1)
		return -EINVAL;

	if (mac_idx == RTW89_MAC_0)
		return 0;

	if (en) {
		rtw89_write32_set(rtwdev, R_BE_AFE_CTRL1, B_BE_AFE_CTRL1_SET);
		rtw89_write32_clr(rtwdev, R_BE_SYS_ISO_CTRL_EXTEND, B_BE_R_SYM_ISO_CMAC12PP);
		rtw89_write32_set(rtwdev, R_BE_FEN_RST_ENABLE, B_BE_CMAC1_FEN);

		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_CK_EN, mac_idx);
		rtw89_write32_set(rtwdev, reg, B_BE_CK_EN_SET);

		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_CMAC_FUNC_EN, mac_idx);
		rtw89_write32_set(rtwdev, reg, B_BE_CMAC_FUNC_EN_SET);

		set_bit(RTW89_FLAG_CMAC1_FUNC, rtwdev->flags);
	} else {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_CMAC_FUNC_EN, mac_idx);
		rtw89_write32_clr(rtwdev, reg, B_BE_CMAC_FUNC_EN_SET);

		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_CK_EN, mac_idx);
		rtw89_write32_clr(rtwdev, reg, B_BE_CK_EN_SET);

		rtw89_write32_clr(rtwdev, R_BE_FEN_RST_ENABLE, B_BE_CMAC1_FEN);
		rtw89_write32_set(rtwdev, R_BE_SYS_ISO_CTRL_EXTEND, B_BE_R_SYM_ISO_CMAC12PP);
		rtw89_write32_clr(rtwdev, R_BE_AFE_CTRL1, B_BE_AFE_CTRL1_SET);

		clear_bit(RTW89_FLAG_CMAC1_FUNC, rtwdev->flags);
	}

	return 0;
}

static int chip_func_en_be(struct rtw89_dev *rtwdev)
{
	return 0;
}

static int sys_init_be(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = dmac_func_en_be(rtwdev);
	if (ret)
		return ret;

	ret = cmac_func_en_be(rtwdev, RTW89_MAC_0, true);
	if (ret)
		return ret;

	ret = chip_func_en_be(rtwdev);
	if (ret)
		return ret;

	return ret;
}

static int sta_sch_init_be(struct rtw89_dev *rtwdev)
{
	u32 p_val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	rtw89_write8_set(rtwdev, R_BE_SS_CTRL, B_BE_SS_EN);

	ret = read_poll_timeout(rtw89_read32, p_val, p_val & B_BE_SS_INIT_DONE,
				1, TRXCFG_WAIT_CNT, false, rtwdev, R_BE_SS_CTRL);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]STA scheduler init\n");
		return ret;
	}

	rtw89_write32_set(rtwdev, R_BE_SS_CTRL, B_BE_WARM_INIT);
	rtw89_write32_clr(rtwdev, R_BE_SS_CTRL, B_BE_BAND_TRIG_EN | B_BE_BAND1_TRIG_EN);

	return 0;
}

static int mpdu_proc_init_be(struct rtw89_dev *rtwdev)
{
	u32 val32;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_BE_MPDU_PROC, B_BE_APPEND_FCS);
	rtw89_write32(rtwdev, R_BE_CUT_AMSDU_CTRL, TRXCFG_MPDU_PROC_CUT_CTRL);

	val32 = rtw89_read32(rtwdev, R_BE_HDR_SHCUT_SETTING);
	val32 |= (B_BE_TX_HW_SEQ_EN | B_BE_TX_HW_ACK_POLICY_EN | B_BE_TX_MAC_MPDU_PROC_EN);
	val32 &= ~B_BE_TX_ADDR_MLD_TO_LIK;
	rtw89_write32_set(rtwdev, R_BE_HDR_SHCUT_SETTING, val32);

	rtw89_write32(rtwdev, R_BE_RX_HDRTRNS, TRXCFG_MPDU_PROC_RX_HDR_CONV);

	val32 = rtw89_read32(rtwdev, R_BE_DISP_FWD_WLAN_0);
	val32 = u32_replace_bits(val32, 1, B_BE_FWD_WLAN_CPU_TYPE_0_DATA_MASK);
	val32 = u32_replace_bits(val32, 1, B_BE_FWD_WLAN_CPU_TYPE_0_MNG_MASK);
	val32 = u32_replace_bits(val32, 1, B_BE_FWD_WLAN_CPU_TYPE_0_CTL_MASK);
	val32 = u32_replace_bits(val32, 1, B_BE_FWD_WLAN_CPU_TYPE_1_MASK);
	rtw89_write32(rtwdev, R_BE_DISP_FWD_WLAN_0, val32);

	return 0;
}

static int sec_eng_init_be(struct rtw89_dev *rtwdev)
{
	u32 val32;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	val32 = rtw89_read32(rtwdev, R_BE_SEC_ENG_CTRL);
	val32 |= B_BE_CLK_EN_CGCMP | B_BE_CLK_EN_WAPI | B_BE_CLK_EN_WEP_TKIP |
		 B_BE_SEC_TX_ENC | B_BE_SEC_RX_DEC |
		 B_BE_MC_DEC | B_BE_BC_DEC |
		 B_BE_BMC_MGNT_DEC | B_BE_UC_MGNT_DEC;
	val32 &= ~B_BE_SEC_PRE_ENQUE_TX;
	rtw89_write32(rtwdev, R_BE_SEC_ENG_CTRL, val32);

	rtw89_write32_set(rtwdev, R_BE_SEC_MPDU_PROC, B_BE_APPEND_ICV | B_BE_APPEND_MIC);

	return 0;
}

static int txpktctrl_init_be(struct rtw89_dev *rtwdev)
{
	struct rtw89_mac_dle_rsvd_qt_cfg qt_cfg;
	u32 val32;
	int ret;

	ret = rtw89_mac_get_dle_rsvd_qt_cfg(rtwdev, DLE_RSVD_QT_MPDU_INFO, &qt_cfg);
	if (ret) {
		rtw89_err(rtwdev, "get dle rsvd qt %d cfg fail %d\n",
			  DLE_RSVD_QT_MPDU_INFO, ret);
		return ret;
	}

	val32 = rtw89_read32(rtwdev, R_BE_TXPKTCTL_MPDUINFO_CFG);
	val32 = u32_replace_bits(val32, qt_cfg.pktid, B_BE_MPDUINFO_PKTID_MASK);
	val32 = u32_replace_bits(val32, MPDU_INFO_B1_OFST, B_BE_MPDUINFO_B1_BADDR_MASK);
	val32 |= B_BE_MPDUINFO_FEN;
	rtw89_write32(rtwdev, R_BE_TXPKTCTL_MPDUINFO_CFG, val32);

	return 0;
}

static int mlo_init_be(struct rtw89_dev *rtwdev)
{
	u32 val32;
	int ret;

	val32 = rtw89_read32(rtwdev, R_BE_MLO_INIT_CTL);

	val32 |= B_BE_MLO_TABLE_REINIT;
	rtw89_write32(rtwdev, R_BE_MLO_INIT_CTL, val32);
	val32 &= ~B_BE_MLO_TABLE_REINIT;
	rtw89_write32(rtwdev, R_BE_MLO_INIT_CTL, val32);

	ret = read_poll_timeout_atomic(rtw89_read32, val32,
				       val32 & B_BE_MLO_TABLE_INIT_DONE,
				       1, 1000, false, rtwdev, R_BE_MLO_INIT_CTL);
	if (ret)
		rtw89_err(rtwdev, "[MLO]%s: MLO init polling timeout\n", __func__);

	rtw89_write32_set(rtwdev, R_BE_SS_CTRL, B_BE_MLO_HW_CHGLINK_EN);
	rtw89_write32_set(rtwdev, R_BE_CMAC_SHARE_ACQCHK_CFG_0, B_BE_R_MACID_ACQ_CHK_EN);

	return ret;
}

static int dmac_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	int ret;

	ret = rtw89_mac_dle_init(rtwdev, rtwdev->mac.qta_mode, RTW89_QTA_INVALID);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]DLE init %d\n", ret);
		return ret;
	}

	ret = rtw89_mac_preload_init(rtwdev, mac_idx, rtwdev->mac.qta_mode);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]preload init %d\n", ret);
		return ret;
	}

	ret = rtw89_mac_hfc_init(rtwdev, true, true, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]HCI FC init %d\n", ret);
		return ret;
	}

	ret = sta_sch_init_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]STA SCH init %d\n", ret);
		return ret;
	}

	ret = mpdu_proc_init_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]MPDU Proc init %d\n", ret);
		return ret;
	}

	ret = sec_eng_init_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]Security Engine init %d\n", ret);
		return ret;
	}

	ret = txpktctrl_init_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]TX pkt ctrl init %d\n", ret);
		return ret;
	}

	ret = mlo_init_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]MLO init %d\n", ret);
		return ret;
	}

	return ret;
}

static int scheduler_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 val32;
	u32 reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_HE_CTN_CHK_CCA_NAV, mac_idx);
	val32 = B_BE_HE_CTN_CHK_CCA_P20 | B_BE_HE_CTN_CHK_EDCCA_P20 |
		B_BE_HE_CTN_CHK_CCA_BITMAP | B_BE_HE_CTN_CHK_EDCCA_BITMAP |
		B_BE_HE_CTN_CHK_NO_GNT_WL | B_BE_HE_CTN_CHK_BASIC_NAV |
		B_BE_HE_CTN_CHK_INTRA_NAV | B_BE_HE_CTN_CHK_TX_NAV;
	rtw89_write32(rtwdev, reg, val32);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_HE_SIFS_CHK_CCA_NAV, mac_idx);
	val32 = B_BE_HE_SIFS_CHK_EDCCA_P20 | B_BE_HE_SIFS_CHK_EDCCA_BITMAP |
		B_BE_HE_SIFS_CHK_NO_GNT_WL;
	rtw89_write32(rtwdev, reg, val32);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TB_CHK_CCA_NAV, mac_idx);
	val32 = B_BE_TB_CHK_EDCCA_BITMAP | B_BE_TB_CHK_NO_GNT_WL | B_BE_TB_CHK_BASIC_NAV;
	rtw89_write32(rtwdev, reg, val32);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_CCA_CFG_0, mac_idx);
	rtw89_write32_clr(rtwdev, reg, B_BE_NO_GNT_WL_EN);

	if (is_qta_poh(rtwdev)) {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PREBKF_CFG_0, mac_idx);
		rtw89_write32_mask(rtwdev, reg, B_BE_PREBKF_TIME_MASK,
				   SCH_PREBKF_24US);

		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_CTN_CFG_0, mac_idx);
		rtw89_write32_mask(rtwdev, reg, B_BE_PREBKF_TIME_NONAC_MASK,
				   SCH_PREBKF_24US);
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_EDCA_BCNQ_PARAM, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_BE_BCNQ_CW_MASK, 0x32);
	rtw89_write32_mask(rtwdev, reg, B_BE_BCNQ_AIFS_MASK, BCN_IFS_25US);

	return 0;
}

static int addr_cam_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 val32;
	u16 val16;
	u32 reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_ADDR_CAM_CTRL, mac_idx);
	val32 = rtw89_read32(rtwdev, reg);
	val32 = u32_replace_bits(val32, ADDR_CAM_SERCH_RANGE, B_BE_ADDR_CAM_RANGE_MASK);
	val32 |= B_BE_ADDR_CAM_EN;
	if (mac_idx == RTW89_MAC_0)
		val32 |= B_BE_ADDR_CAM_CLR;
	rtw89_write32(rtwdev, reg, val32);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_ADDR_CAM_CTRL, mac_idx);
	ret = read_poll_timeout_atomic(rtw89_read16, val16, !(val16 & B_BE_ADDR_CAM_CLR),
				       1, TRXCFG_WAIT_CNT, false, rtwdev, reg);
	if (ret)
		rtw89_err(rtwdev, "[ERR]ADDR_CAM reset\n");

	return ret;
}

static int rtw89_mac_typ_fltr_opt_be(struct rtw89_dev *rtwdev,
				     enum rtw89_machdr_frame_type type,
				     enum rtw89_mac_fwd_target fwd_target,
				     u8 mac_idx)
{
	u32 reg;
	u32 val;

	switch (fwd_target) {
	case RTW89_FWD_DONT_CARE:
		val = RX_FLTR_FRAME_DROP_BE;
		break;
	case RTW89_FWD_TO_HOST:
	case RTW89_FWD_TO_WLAN_CPU:
		val = RX_FLTR_FRAME_ACCEPT_BE;
		break;
	default:
		rtw89_err(rtwdev, "[ERR]set rx filter fwd target err\n");
		return -EINVAL;
	}

	switch (type) {
	case RTW89_MGNT:
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_MGNT_FLTR, mac_idx);
		break;
	case RTW89_CTRL:
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_CTRL_FLTR, mac_idx);
		break;
	case RTW89_DATA:
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_DATA_FLTR, mac_idx);
		break;
	default:
		rtw89_err(rtwdev, "[ERR]set rx filter type err\n");
		return -EINVAL;
	}
	rtw89_write32(rtwdev, reg, val);

	return 0;
}

static int rx_fltr_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;
	u32 val;

	rtw89_mac_typ_fltr_opt_be(rtwdev, RTW89_MGNT, RTW89_FWD_TO_HOST, mac_idx);
	rtw89_mac_typ_fltr_opt_be(rtwdev, RTW89_CTRL, RTW89_FWD_TO_HOST, mac_idx);
	rtw89_mac_typ_fltr_opt_be(rtwdev, RTW89_DATA, RTW89_FWD_TO_HOST, mac_idx);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_RX_FLTR_OPT, mac_idx);
	val = B_BE_A_BC_CAM_MATCH | B_BE_A_UC_CAM_MATCH | B_BE_A_MC |
	      B_BE_A_BC | B_BE_A_A1_MATCH | B_BE_SNIFFER_MODE |
	      u32_encode_bits(15, B_BE_UID_FILTER_MASK);
	rtw89_write32(rtwdev, reg, val);
	u32p_replace_bits(&rtwdev->hal.rx_fltr, 15, B_BE_UID_FILTER_MASK);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PLCP_HDR_FLTR, mac_idx);
	val = B_BE_HE_SIGB_CRC_CHK | B_BE_VHT_MU_SIGB_CRC_CHK |
	      B_BE_VHT_SU_SIGB_CRC_CHK | B_BE_SIGA_CRC_CHK |
	      B_BE_LSIG_PARITY_CHK_EN | B_BE_CCK_SIG_CHK | B_BE_CCK_CRC_CHK;
	rtw89_write16(rtwdev, reg, val);

	return 0;
}

static int cca_ctrl_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	return 0;
}

static int nav_ctrl_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 val32;
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_WMAC_NAV_CTL, mac_idx);

	val32 = rtw89_read32(rtwdev, reg);
	val32 &= ~B_BE_WMAC_PLCP_UP_NAV_EN;
	val32 |= B_BE_WMAC_TF_UP_NAV_EN | B_BE_WMAC_NAV_UPPER_EN;
	val32 = u32_replace_bits(val32, NAV_25MS, B_BE_WMAC_NAV_UPPER_MASK);

	rtw89_write32(rtwdev, reg, val32);

	return 0;
}

static int spatial_reuse_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_RX_SR_CTRL, mac_idx);
	rtw89_write8_clr(rtwdev, reg, B_BE_SR_EN | B_BE_SR_CTRL_PLCP_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_BSSID_SRC_CTRL, mac_idx);
	rtw89_write8_set(rtwdev, reg, B_BE_PLCP_SRC_EN);

	return 0;
}

static int tmac_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TB_PPDU_CTRL, mac_idx);
	rtw89_write32_clr(rtwdev, reg, B_BE_QOSNULL_UPD_MUEDCA_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_WMTX_TCR_BE_4, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_BE_EHT_HE_PPDU_4XLTF_ZLD_USTIMER_MASK, 0x12);
	rtw89_write32_mask(rtwdev, reg, B_BE_EHT_HE_PPDU_2XLTF_ZLD_USTIMER_MASK, 0xe);

	return 0;
}

static int trxptcl_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_rrsr_cfgs *rrsr = chip->rrsr_cfgs;
	struct rtw89_hal *hal = &rtwdev->hal;
	u32 val32;
	u32 reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_MAC_LOOPBACK, mac_idx);
	val32 = rtw89_read32(rtwdev, reg);
	val32 = u32_replace_bits(val32, S_BE_MACLBK_PLCP_DLY_DEF,
				 B_BE_MACLBK_PLCP_DLY_MASK);
	val32 &= ~B_BE_MACLBK_EN;
	rtw89_write32(rtwdev, reg, val32);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_0, mac_idx);
	val32 = rtw89_read32(rtwdev, reg);
	val32 = u32_replace_bits(val32, WMAC_SPEC_SIFS_CCK,
				 B_BE_WMAC_SPEC_SIFS_CCK_MASK);
	val32 = u32_replace_bits(val32, WMAC_SPEC_SIFS_OFDM_1115E,
				 B_BE_WMAC_SPEC_SIFS_OFDM_MASK);
	rtw89_write32(rtwdev, reg, val32);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_WMAC_ACK_BA_RESP_LEGACY, mac_idx);
	rtw89_write32_clr(rtwdev, reg, B_BE_ACK_BA_RESP_LEGACY_CHK_EDCCA);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_WMAC_ACK_BA_RESP_HE, mac_idx);
	rtw89_write32_clr(rtwdev, reg, B_BE_ACK_BA_RESP_HE_CHK_EDCCA);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_WMAC_ACK_BA_RESP_EHT_LEG_PUNC, mac_idx);
	rtw89_write32_clr(rtwdev, reg, B_BE_ACK_BA_EHT_LEG_PUNC_CHK_EDCCA);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_RXTRIG_TEST_USER_2, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_BE_RXTRIG_FCSCHK_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_1, mac_idx);
	val32 = rtw89_read32(rtwdev, reg);
	val32 &= B_BE_FTM_RRSR_RATE_EN_MASK | B_BE_WMAC_RESP_DOPPLEB_BE_EN |
		 B_BE_WMAC_RESP_DCM_EN | B_BE_WMAC_RESP_REF_RATE_MASK;
	rtw89_write32(rtwdev, reg, val32);
	rtw89_write32_mask(rtwdev, reg, rrsr->ref_rate.mask, rrsr->ref_rate.data);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PTCL_RRSR1, mac_idx);
	val32 = rtw89_read32(rtwdev, reg);
	val32 &= B_BE_RRSR_RATE_EN_MASK | B_BE_RRSR_CCK_MASK | B_BE_RSC_MASK;
	rtw89_write32(rtwdev, reg, val32);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PTCL_RRSR0, mac_idx);
	val32 = rtw89_read32(rtwdev, reg);
	val32 &= B_BE_RRSR_OFDM_MASK | B_BE_RRSR_HT_MASK | B_BE_RRSR_VHT_MASK |
		 B_BE_RRSR_HE_MASK;
	rtw89_write32(rtwdev, reg, val32);

	if (chip->chip_id == RTL8922A && hal->cv == CHIP_CAV) {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PTCL_RRSR1, mac_idx);
		rtw89_write32_mask(rtwdev, reg, B_BE_RSC_MASK, 1);
	}

	return 0;
}

static int rst_bacam_be(struct rtw89_dev *rtwdev)
{
	u32 val;
	int ret;

	rtw89_write32_mask(rtwdev, R_BE_RESPBA_CAM_CTRL, B_BE_BACAM_RST_MASK,
			   S_BE_BACAM_RST_ALL);

	ret = read_poll_timeout_atomic(rtw89_read32_mask, val, val == S_BE_BACAM_RST_DONE,
				       1, 1000, false,
				       rtwdev, R_BE_RESPBA_CAM_CTRL, B_BE_BACAM_RST_MASK);
	if (ret)
		rtw89_err(rtwdev, "[ERR]bacam rst timeout\n");

	return ret;
}

#define PLD_RLS_MAX_PG 127
#define RX_MAX_LEN_UNIT 512
#define RX_SPEC_MAX_LEN (11454 + RX_MAX_LEN_UNIT)

static int rmac_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 rx_min_qta, rx_max_len, rx_max_pg;
	u16 val16;
	u32 reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (mac_idx == RTW89_MAC_0) {
		ret = rst_bacam_be(rtwdev);
		if (ret)
			return ret;
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_DLK_PROTECT_CTL, mac_idx);
	val16 = rtw89_read16(rtwdev, reg);
	val16 = u16_replace_bits(val16, TRXCFG_RMAC_DATA_TO, B_BE_RX_DLK_DATA_TIME_MASK);
	val16 = u16_replace_bits(val16, TRXCFG_RMAC_CCA_TO, B_BE_RX_DLK_CCA_TIME_MASK);
	val16 |= B_BE_RX_DLK_RST_EN;
	rtw89_write16(rtwdev, reg, val16);

	if (mac_idx == RTW89_MAC_0)
		rx_min_qta = rtwdev->mac.dle_info.c0_rx_qta;
	else
		rx_min_qta = rtwdev->mac.dle_info.c1_rx_qta;
	rx_max_pg = min_t(u32, rx_min_qta, PLD_RLS_MAX_PG);
	rx_max_len = rx_max_pg * rtwdev->mac.dle_info.ple_pg_size;
	rx_max_len = min_t(u32, rx_max_len, RX_SPEC_MAX_LEN);
	rx_max_len /= RX_MAX_LEN_UNIT;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_RX_FLTR_OPT, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_BE_RX_MPDU_MAX_LEN_MASK, rx_max_len);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PLCP_HDR_FLTR, mac_idx);
	rtw89_write8_clr(rtwdev, reg, B_BE_VHT_SU_SIGB_CRC_CHK);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_RCR, mac_idx);
	rtw89_write16_set(rtwdev, reg, B_BE_BUSY_CHKSN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_RX_PLCP_EXT_OPTION_1, mac_idx);
	rtw89_write16_set(rtwdev, reg, B_BE_PLCP_SU_PSDU_LEN_SRC);

	return 0;
}

static int resp_pktctl_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	struct rtw89_mac_dle_rsvd_qt_cfg qt_cfg;
	enum rtw89_mac_dle_rsvd_qt_type type;
	u32 reg;
	int ret;

	if (mac_idx == RTW89_MAC_1)
		type = DLE_RSVD_QT_B1_CSI;
	else
		type = DLE_RSVD_QT_B0_CSI;

	ret = rtw89_mac_get_dle_rsvd_qt_cfg(rtwdev, type, &qt_cfg);
	if (ret) {
		rtw89_err(rtwdev, "get dle rsvd qt %d cfg fail %d\n", type, ret);
		return ret;
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_RESP_CSI_RESERVED_PAGE, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_BE_CSI_RESERVED_START_PAGE_MASK, qt_cfg.pktid);
	rtw89_write32_mask(rtwdev, reg, B_BE_CSI_RESERVED_PAGE_NUM_MASK, qt_cfg.pg_num);

	return 0;
}

static int cmac_com_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 val32;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (mac_idx == RTW89_MAC_0) {
		val32 = rtw89_read32(rtwdev, R_BE_TX_SUB_BAND_VALUE);
		val32 = u32_replace_bits(val32, S_BE_TXSB_20M_8, B_BE_TXSB_20M_MASK);
		val32 = u32_replace_bits(val32, S_BE_TXSB_40M_4, B_BE_TXSB_40M_MASK);
		val32 = u32_replace_bits(val32, S_BE_TXSB_80M_2, B_BE_TXSB_80M_MASK);
		val32 = u32_replace_bits(val32, S_BE_TXSB_160M_1, B_BE_TXSB_160M_MASK);
		rtw89_write32(rtwdev, R_BE_TX_SUB_BAND_VALUE, val32);
	} else {
		val32 = rtw89_read32(rtwdev, R_BE_TX_SUB_BAND_VALUE_C1);
		val32 = u32_replace_bits(val32, S_BE_TXSB_20M_2, B_BE_TXSB_20M_MASK);
		val32 = u32_replace_bits(val32, S_BE_TXSB_40M_1, B_BE_TXSB_40M_MASK);
		val32 = u32_replace_bits(val32, S_BE_TXSB_80M_0, B_BE_TXSB_80M_MASK);
		val32 = u32_replace_bits(val32, S_BE_TXSB_160M_0, B_BE_TXSB_160M_MASK);
		rtw89_write32(rtwdev, R_BE_TX_SUB_BAND_VALUE_C1, val32);
	}

	return 0;
}

static int ptcl_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 val32;
	u8 val8;
	u32 reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (is_qta_poh(rtwdev)) {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_SIFS_SETTING, mac_idx);
		val32 = rtw89_read32(rtwdev, reg);
		val32 = u32_replace_bits(val32, S_AX_CTS2S_TH_1K,
					 B_BE_HW_CTS2SELF_PKT_LEN_TH_MASK);
		val32 = u32_replace_bits(val32, S_AX_CTS2S_TH_SEC_256B,
					 B_BE_HW_CTS2SELF_PKT_LEN_TH_TWW_MASK);
		val32 |= B_BE_HW_CTS2SELF_EN;
		rtw89_write32(rtwdev, reg, val32);

		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PTCL_FSM_MON, mac_idx);
		val32 = rtw89_read32(rtwdev, reg);
		val32 = u32_replace_bits(val32, S_AX_PTCL_TO_2MS,
					 B_BE_PTCL_TX_ARB_TO_THR_MASK);
		val32 &= ~B_BE_PTCL_TX_ARB_TO_MODE;
		rtw89_write32(rtwdev, reg, val32);
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PTCL_COMMON_SETTING_0, mac_idx);
	val8 = rtw89_read8(rtwdev, reg);
	val8 |= B_BE_CMAC_TX_MODE_0 | B_BE_CMAC_TX_MODE_1;
	val8 &= ~(B_BE_PTCL_TRIGGER_SS_EN_0 |
		  B_BE_PTCL_TRIGGER_SS_EN_1 |
		  B_BE_PTCL_TRIGGER_SS_EN_UL);
	rtw89_write8(rtwdev, reg, val8);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_AMPDU_AGG_LIMIT, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_BE_AMPDU_MAX_TIME_MASK, AMPDU_MAX_TIME);

	return 0;
}

static int cmac_dma_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 val32;
	u32 reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_RX_CTRL_1, mac_idx);

	val32 = rtw89_read32(rtwdev, reg);
	val32 = u32_replace_bits(val32, WLCPU_RXCH2_QID,
				 B_BE_RXDMA_TXRPT_QUEUE_ID_SW_MASK);
	val32 = u32_replace_bits(val32, WLCPU_RXCH2_QID,
				 B_BE_RXDMA_F2PCMDRPT_QUEUE_ID_SW_MASK);
	rtw89_write32(rtwdev, reg, val32);

	return 0;
}

static int cmac_init_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	int ret;

	ret = scheduler_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d SCH init %d\n", mac_idx, ret);
		return ret;
	}

	ret = addr_cam_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d ADDR_CAM reset %d\n", mac_idx,
			  ret);
		return ret;
	}

	ret = rx_fltr_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d RX filter init %d\n", mac_idx,
			  ret);
		return ret;
	}

	ret = cca_ctrl_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d CCA CTRL init %d\n", mac_idx,
			  ret);
		return ret;
	}

	ret = nav_ctrl_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d NAV CTRL init %d\n", mac_idx,
			  ret);
		return ret;
	}

	ret = spatial_reuse_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d Spatial Reuse init %d\n",
			  mac_idx, ret);
		return ret;
	}

	ret = tmac_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d TMAC init %d\n", mac_idx, ret);
		return ret;
	}

	ret = trxptcl_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d TRXPTCL init %d\n", mac_idx, ret);
		return ret;
	}

	ret = rmac_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d RMAC init %d\n", mac_idx, ret);
		return ret;
	}

	ret = resp_pktctl_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d resp pktctl init %d\n", mac_idx, ret);
		return ret;
	}

	ret = cmac_com_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d Com init %d\n", mac_idx, ret);
		return ret;
	}

	ret = ptcl_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d PTCL init %d\n", mac_idx, ret);
		return ret;
	}

	ret = cmac_dma_init_be(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d DMA init %d\n", mac_idx, ret);
		return ret;
	}

	return ret;
}

static int tx_idle_poll_band_be(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;
	u8 val8;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PTCL_TX_CTN_SEL, mac_idx);

	ret = read_poll_timeout_atomic(rtw89_read8, val8, !(val8 & B_BE_PTCL_BUSY),
				       30, 66000, false, rtwdev, reg);

	return ret;
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

static int dle_upd_qta_aval_page_be(struct rtw89_dev *rtwdev,
				    enum rtw89_mac_dle_ctrl_type type,
				    enum rtw89_mac_dle_ple_quota_id quota_id)
{
	u32 val;

	if (type == DLE_CTRL_TYPE_WDE) {
		rtw89_write32_mask(rtwdev, R_BE_WDE_BUFMGN_CTL,
				   B_BE_WDE_AVAL_UPD_QTAID_MASK, quota_id);
		rtw89_write32_set(rtwdev, R_BE_WDE_BUFMGN_CTL, B_BE_WDE_AVAL_UPD_REQ);

		return read_poll_timeout(rtw89_read32, val,
					 !(val & B_BE_WDE_AVAL_UPD_REQ),
					 1, 2000, false, rtwdev, R_BE_WDE_BUFMGN_CTL);
	} else if (type == DLE_CTRL_TYPE_PLE) {
		rtw89_write32_mask(rtwdev, R_BE_PLE_BUFMGN_CTL,
				   B_BE_PLE_AVAL_UPD_QTAID_MASK, quota_id);
		rtw89_write32_set(rtwdev, R_BE_PLE_BUFMGN_CTL, B_BE_PLE_AVAL_UPD_REQ);

		return read_poll_timeout(rtw89_read32, val,
					 !(val & B_BE_PLE_AVAL_UPD_REQ),
					 1, 2000, false, rtwdev, R_BE_PLE_BUFMGN_CTL);
	}

	rtw89_warn(rtwdev, "%s wrong type %d\n", __func__, type);
	return -EINVAL;
}

static int dle_quota_change_be(struct rtw89_dev *rtwdev, bool band1_en)
{
	int ret;

	if (band1_en) {
		ret = dle_upd_qta_aval_page_be(rtwdev, DLE_CTRL_TYPE_PLE,
					       PLE_QTAID_B0_TXPL);
		if (ret) {
			rtw89_err(rtwdev, "update PLE B0 TX avail page fail %d\n", ret);
			return ret;
		}

		ret = dle_upd_qta_aval_page_be(rtwdev, DLE_CTRL_TYPE_PLE,
					       PLE_QTAID_CMAC0_RX);
		if (ret) {
			rtw89_err(rtwdev, "update PLE CMAC0 RX avail page fail %d\n", ret);
			return ret;
		}
	} else {
		ret = dle_upd_qta_aval_page_be(rtwdev, DLE_CTRL_TYPE_PLE,
					       PLE_QTAID_B1_TXPL);
		if (ret) {
			rtw89_err(rtwdev, "update PLE B1 TX avail page fail %d\n", ret);
			return ret;
		}

		ret = dle_upd_qta_aval_page_be(rtwdev, DLE_CTRL_TYPE_PLE,
					       PLE_QTAID_CMAC1_RX);
		if (ret) {
			rtw89_err(rtwdev, "update PLE CMAC1 RX avail page fail %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int preload_init_be(struct rtw89_dev *rtwdev, u8 mac_idx,
			   enum rtw89_qta_mode mode)
{
	u32 max_preld_size, min_rsvd_size;
	u32 val32;
	u32 reg;

	max_preld_size = mac_idx == RTW89_MAC_0 ?
			 PRELD_B0_ENT_NUM : PRELD_B1_ENT_NUM;
	max_preld_size *= PRELD_AMSDU_SIZE;

	reg = mac_idx == RTW89_MAC_0 ? R_BE_TXPKTCTL_B0_PRELD_CFG0 :
				       R_BE_TXPKTCTL_B1_PRELD_CFG0;
	val32 = rtw89_read32(rtwdev, reg);
	val32 = u32_replace_bits(val32, max_preld_size, B_BE_B0_PRELD_USEMAXSZ_MASK);
	val32 |= B_BE_B0_PRELD_FEN;
	rtw89_write32(rtwdev, reg, val32);

	min_rsvd_size = PRELD_AMSDU_SIZE;
	reg = mac_idx == RTW89_MAC_0 ? R_BE_TXPKTCTL_B0_PRELD_CFG1 :
				       R_BE_TXPKTCTL_B1_PRELD_CFG1;
	val32 = rtw89_read32(rtwdev, reg);
	val32 = u32_replace_bits(val32, PRELD_NEXT_WND, B_BE_B0_PRELD_NXT_TXENDWIN_MASK);
	val32 = u32_replace_bits(val32, min_rsvd_size, B_BE_B0_PRELD_NXT_RSVMINSZ_MASK);
	rtw89_write32(rtwdev, reg, val32);

	return 0;
}

static int dbcc_bb_ctrl_be(struct rtw89_dev *rtwdev, bool bb1_en)
{
	u32 set = B_BE_FEN_BB1PLAT_RSTB | B_BE_FEN_BB1_IP_RSTN;

	if (bb1_en)
		rtw89_write32_set(rtwdev, R_BE_FEN_RST_ENABLE, set);
	else
		rtw89_write32_clr(rtwdev, R_BE_FEN_RST_ENABLE, set);

	return 0;
}

static int enable_imr_be(struct rtw89_dev *rtwdev, u8 mac_idx,
			 enum rtw89_mac_hwmod_sel sel)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_imr_table *table;
	const struct rtw89_reg_imr *reg;
	u32 addr;
	u32 val;
	int i;

	if (sel == RTW89_DMAC_SEL)
		table = chip->imr_dmac_table;
	else if (sel == RTW89_CMAC_SEL)
		table = chip->imr_cmac_table;
	else
		return -EINVAL;

	for (i = 0; i < table->n_regs; i++) {
		reg = &table->regs[i];
		addr = rtw89_mac_reg_by_idx(rtwdev, reg->addr, mac_idx);

		val = rtw89_read32(rtwdev, addr);
		val &= ~reg->clr;
		val |= reg->set;
		rtw89_write32(rtwdev, addr, val);
	}

	return 0;
}

static void err_imr_ctrl_be(struct rtw89_dev *rtwdev, bool en)
{
	u32 v32_dmac = en ? DMAC_ERR_IMR_EN : DMAC_ERR_IMR_DIS;
	u32 v32_cmac0 = en ? CMAC0_ERR_IMR_EN : CMAC0_ERR_IMR_DIS;
	u32 v32_cmac1 = en ? CMAC1_ERR_IMR_EN : CMAC1_ERR_IMR_DIS;

	v32_dmac &= ~B_BE_DMAC_NOTX_ERR_INT_EN;

	rtw89_write32(rtwdev, R_BE_DMAC_ERR_IMR, v32_dmac);
	rtw89_write32(rtwdev, R_BE_CMAC_ERR_IMR, v32_cmac0);

	if (rtwdev->dbcc_en)
		rtw89_write32(rtwdev, R_BE_CMAC_ERR_IMR_C1, v32_cmac1);
}

static int band1_enable_be(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = tx_idle_poll_band_be(rtwdev, RTW89_MAC_0);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]tx idle poll %d\n", ret);
		return ret;
	}

	ret = rtw89_mac_dle_quota_change(rtwdev, rtwdev->mac.qta_mode, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]DLE quota change %d\n", ret);
		return ret;
	}

	ret = preload_init_be(rtwdev, RTW89_MAC_1, rtwdev->mac.qta_mode);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]preload init B1 %d\n", ret);
		return ret;
	}

	ret = cmac_func_en_be(rtwdev, RTW89_MAC_1, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d func en %d\n", RTW89_MAC_1, ret);
		return ret;
	}

	ret = cmac_init_be(rtwdev, RTW89_MAC_1);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d init %d\n", RTW89_MAC_1, ret);
		return ret;
	}

	ret = dbcc_bb_ctrl_be(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]enable bb 1 %d\n", ret);
		return ret;
	}

	ret = enable_imr_be(rtwdev, RTW89_MAC_1, RTW89_CMAC_SEL);
	if (ret) {
		rtw89_err(rtwdev, "[ERR] enable CMAC1 IMR %d\n", ret);
		return ret;
	}

	return 0;
}

static int band1_disable_be(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = dbcc_bb_ctrl_be(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]disable bb 1 %d\n", ret);
		return ret;
	}

	ret = cmac_func_en_be(rtwdev, RTW89_MAC_1, false);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d func dis %d\n", RTW89_MAC_1, ret);
		return ret;
	}

	ret = rtw89_mac_dle_quota_change(rtwdev, rtwdev->mac.qta_mode, false);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]DLE quota change %d\n", ret);
		return ret;
	}

	return 0;
}

static int dbcc_enable_be(struct rtw89_dev *rtwdev, bool enable)
{
	int ret;

	if (enable) {
		ret = band1_enable_be(rtwdev);
		if (ret) {
			rtw89_err(rtwdev, "[ERR] band1_enable %d\n", ret);
			return ret;
		}

		if (test_bit(RTW89_FLAG_FW_RDY, rtwdev->flags)) {
			ret = rtw89_fw_h2c_notify_dbcc(rtwdev, true);
			if (ret) {
				rtw89_err(rtwdev, "%s:[ERR] notify dbcc1 fail %d\n",
					  __func__, ret);
				return ret;
			}
		}
	} else {
		if (test_bit(RTW89_FLAG_FW_RDY, rtwdev->flags)) {
			ret = rtw89_fw_h2c_notify_dbcc(rtwdev, false);
			if (ret) {
				rtw89_err(rtwdev, "%s:[ERR] notify dbcc1 fail %d\n",
					  __func__, ret);
				return ret;
			}
		}

		ret = band1_disable_be(rtwdev);
		if (ret) {
			rtw89_err(rtwdev, "[ERR] band1_disable %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int set_host_rpr_be(struct rtw89_dev *rtwdev)
{
	u32 val32;
	u32 mode;
	u32 fltr;
	bool poh;

	poh = is_qta_poh(rtwdev);

	if (poh) {
		mode = RTW89_RPR_MODE_POH;
		fltr = S_BE_WDRLS_FLTR_TXOK | S_BE_WDRLS_FLTR_RTYLMT |
		       S_BE_WDRLS_FLTR_LIFTIM | S_BE_WDRLS_FLTR_MACID;
	} else {
		mode = RTW89_RPR_MODE_STF;
		fltr = 0;
	}

	rtw89_write32_mask(rtwdev, R_BE_WDRLS_CFG, B_BE_WDRLS_MODE_MASK, mode);

	val32 = rtw89_read32(rtwdev, R_BE_RLSRPT0_CFG1);
	val32 = u32_replace_bits(val32, fltr, B_BE_RLSRPT0_FLTR_MAP_MASK);
	val32 = u32_replace_bits(val32, 30, B_BE_RLSRPT0_AGGNUM_MASK);
	val32 = u32_replace_bits(val32, 255, B_BE_RLSRPT0_TO_MASK);
	rtw89_write32(rtwdev, R_BE_RLSRPT0_CFG1, val32);

	return 0;
}

static int trx_init_be(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	enum rtw89_qta_mode qta_mode = rtwdev->mac.qta_mode;
	int ret;

	ret = dmac_init_be(rtwdev, 0);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]DMAC init %d\n", ret);
		return ret;
	}

	ret = cmac_init_be(rtwdev, 0);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d init %d\n", 0, ret);
		return ret;
	}

	if (rtw89_mac_is_qta_dbcc(rtwdev, qta_mode)) {
		ret = dbcc_enable_be(rtwdev, true);
		if (ret) {
			rtw89_err(rtwdev, "[ERR]dbcc_enable init %d\n", ret);
			return ret;
		}
	}

	ret = enable_imr_be(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret) {
		rtw89_err(rtwdev, "[ERR] enable DMAC IMR %d\n", ret);
		return ret;
	}

	ret = enable_imr_be(rtwdev, RTW89_MAC_0, RTW89_CMAC_SEL);
	if (ret) {
		rtw89_err(rtwdev, "[ERR] to enable CMAC0 IMR %d\n", ret);
		return ret;
	}

	err_imr_ctrl_be(rtwdev, true);

	ret = set_host_rpr_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR] set host rpr %d\n", ret);
		return ret;
	}

	if (chip_id == RTL8922A)
		rtw89_write32_clr(rtwdev, R_BE_RSP_CHK_SIG,
				  B_BE_RSP_STATIC_RTS_CHK_SERV_BW_EN);

	return 0;
}

int rtw89_mac_cfg_gnt_v2(struct rtw89_dev *rtwdev,
			 const struct rtw89_mac_ax_coex_gnt *gnt_cfg)
{
	u32 val = 0;

	if (gnt_cfg->band[0].gnt_bt)
		val |= B_BE_GNT_BT_BB0_VAL | B_BE_GNT_BT_RX_BB0_VAL |
		       B_BE_GNT_BT_TX_BB0_VAL;

	if (gnt_cfg->band[0].gnt_bt_sw_en)
		val |= B_BE_GNT_BT_BB0_SWCTRL | B_BE_GNT_BT_RX_BB0_SWCTRL |
		       B_BE_GNT_BT_TX_BB0_SWCTRL;

	if (gnt_cfg->band[0].gnt_wl)
		val |= B_BE_GNT_WL_BB0_VAL | B_BE_GNT_WL_RX_VAL |
		       B_BE_GNT_WL_TX_VAL | B_BE_GNT_WL_BB_PWR_VAL;

	if (gnt_cfg->band[0].gnt_wl_sw_en)
		val |= B_BE_GNT_WL_BB0_SWCTRL | B_BE_GNT_WL_RX_SWCTRL |
		       B_BE_GNT_WL_TX_SWCTRL | B_BE_GNT_WL_BB_PWR_SWCTRL;

	if (gnt_cfg->band[1].gnt_bt)
		val |= B_BE_GNT_BT_BB1_VAL | B_BE_GNT_BT_RX_BB1_VAL |
		       B_BE_GNT_BT_TX_BB1_VAL;

	if (gnt_cfg->band[1].gnt_bt_sw_en)
		val |= B_BE_GNT_BT_BB1_SWCTRL | B_BE_GNT_BT_RX_BB1_SWCTRL |
		       B_BE_GNT_BT_TX_BB1_SWCTRL;

	if (gnt_cfg->band[1].gnt_wl)
		val |= B_BE_GNT_WL_BB1_VAL | B_BE_GNT_WL_RX_VAL |
		       B_BE_GNT_WL_TX_VAL | B_BE_GNT_WL_BB_PWR_VAL;

	if (gnt_cfg->band[1].gnt_wl_sw_en)
		val |= B_BE_GNT_WL_BB1_SWCTRL | B_BE_GNT_WL_RX_SWCTRL |
		       B_BE_GNT_WL_TX_SWCTRL | B_BE_GNT_WL_BB_PWR_SWCTRL;

	if (gnt_cfg->bt[0].wlan_act_en)
		val |= B_BE_WL_ACT_SWCTRL;
	if (gnt_cfg->bt[0].wlan_act)
		val |= B_BE_WL_ACT_VAL;
	if (gnt_cfg->bt[1].wlan_act_en)
		val |= B_BE_WL_ACT2_SWCTRL;
	if (gnt_cfg->bt[1].wlan_act)
		val |= B_BE_WL_ACT2_VAL;

	rtw89_write32(rtwdev, R_BE_GNT_SW_CTRL, val);

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_cfg_gnt_v2);

int rtw89_mac_cfg_ctrl_path_v2(struct rtw89_dev *rtwdev, bool wl)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_mac_ax_gnt *g = dm->gnt.band;
	struct rtw89_mac_ax_wl_act *gbt = dm->gnt.bt;
	int i;

	if (wl)
		return 0;

	for (i = 0; i < RTW89_PHY_MAX; i++) {
		g[i].gnt_bt_sw_en = 1;
		g[i].gnt_bt = 1;
		g[i].gnt_wl_sw_en = 1;
		g[i].gnt_wl = 0;
		gbt[i].wlan_act = 1;
		gbt[i].wlan_act_en = 0;
	}

	return rtw89_mac_cfg_gnt_v2(rtwdev, &dm->gnt);
}
EXPORT_SYMBOL(rtw89_mac_cfg_ctrl_path_v2);

static
int rtw89_mac_cfg_plt_be(struct rtw89_dev *rtwdev, struct rtw89_mac_ax_plt *plt)
{
	u32 reg;
	u16 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, plt->band, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_BT_PLT, plt->band);
	val = (plt->tx & RTW89_MAC_AX_PLT_LTE_RX ? B_BE_TX_PLT_GNT_LTE_RX : 0) |
	      (plt->tx & RTW89_MAC_AX_PLT_GNT_BT_TX ? B_BE_TX_PLT_GNT_BT_TX : 0) |
	      (plt->tx & RTW89_MAC_AX_PLT_GNT_BT_RX ? B_BE_TX_PLT_GNT_BT_RX : 0) |
	      (plt->tx & RTW89_MAC_AX_PLT_GNT_WL ? B_BE_TX_PLT_GNT_WL : 0) |
	      (plt->rx & RTW89_MAC_AX_PLT_LTE_RX ? B_BE_RX_PLT_GNT_LTE_RX : 0) |
	      (plt->rx & RTW89_MAC_AX_PLT_GNT_BT_TX ? B_BE_RX_PLT_GNT_BT_TX : 0) |
	      (plt->rx & RTW89_MAC_AX_PLT_GNT_BT_RX ? B_BE_RX_PLT_GNT_BT_RX : 0) |
	      (plt->rx & RTW89_MAC_AX_PLT_GNT_WL ? B_BE_RX_PLT_GNT_WL : 0) |
	      B_BE_PLT_EN;
	rtw89_write16(rtwdev, reg, val);

	return 0;
}

static u16 rtw89_mac_get_plt_cnt_be(struct rtw89_dev *rtwdev, u8 band)
{
	u32 reg;
	u16 cnt;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_BT_PLT, band);
	cnt = rtw89_read32_mask(rtwdev, reg, B_BE_BT_PLT_PKT_CNT_MASK);
	rtw89_write16_set(rtwdev, reg, B_BE_BT_PLT_RST);

	return cnt;
}

static int rtw89_set_hw_sch_tx_en_v2(struct rtw89_dev *rtwdev, u8 mac_idx,
				     u32 tx_en, u32 tx_en_mask)
{
	u32 reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_CTN_DRV_TXEN, mac_idx);
	u32 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	val = rtw89_read32(rtwdev, reg);
	val = (val & ~tx_en_mask) | (tx_en & tx_en_mask);
	rtw89_write32(rtwdev, reg, val);

	return 0;
}

int rtw89_mac_stop_sch_tx_v2(struct rtw89_dev *rtwdev, u8 mac_idx,
			     u32 *tx_en, enum rtw89_sch_tx_sel sel)
{
	int ret;

	*tx_en = rtw89_read32(rtwdev,
			      rtw89_mac_reg_by_idx(rtwdev, R_BE_CTN_DRV_TXEN, mac_idx));

	switch (sel) {
	case RTW89_SCH_TX_SEL_ALL:
		ret = rtw89_set_hw_sch_tx_en_v2(rtwdev, mac_idx, 0,
						B_BE_CTN_TXEN_ALL_MASK);
		if (ret)
			return ret;
		break;
	case RTW89_SCH_TX_SEL_HIQ:
		ret = rtw89_set_hw_sch_tx_en_v2(rtwdev, mac_idx,
						0, B_BE_CTN_TXEN_HGQ);
		if (ret)
			return ret;
		break;
	case RTW89_SCH_TX_SEL_MG0:
		ret = rtw89_set_hw_sch_tx_en_v2(rtwdev, mac_idx,
						0, B_BE_CTN_TXEN_MGQ);
		if (ret)
			return ret;
		break;
	case RTW89_SCH_TX_SEL_MACID:
		ret = rtw89_set_hw_sch_tx_en_v2(rtwdev, mac_idx, 0,
						B_BE_CTN_TXEN_ALL_MASK);
		if (ret)
			return ret;
		break;
	default:
		return 0;
	}

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_stop_sch_tx_v2);

int rtw89_mac_resume_sch_tx_v2(struct rtw89_dev *rtwdev, u8 mac_idx, u32 tx_en)
{
	int ret;

	ret = rtw89_set_hw_sch_tx_en_v2(rtwdev, mac_idx, tx_en,
					B_BE_CTN_TXEN_ALL_MASK);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_resume_sch_tx_v2);

static
int rtw89_mac_cfg_ppdu_status_be(struct rtw89_dev *rtwdev, u8 mac_idx, bool enable)
{
	u32 reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PPDU_STAT, mac_idx);
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (!enable) {
		rtw89_write32_clr(rtwdev, reg, B_BE_PPDU_STAT_RPT_EN);
		return 0;
	}

	rtw89_write32_mask(rtwdev, R_BE_HW_PPDU_STATUS, B_BE_FWD_PPDU_STAT_MASK, 3);
	rtw89_write32(rtwdev, reg, B_BE_PPDU_STAT_RPT_EN | B_BE_PPDU_MAC_INFO |
				   B_BE_APP_RX_CNT_RPT | B_BE_APP_PLCP_HDR_RPT |
				   B_BE_PPDU_STAT_RPT_CRC32 | B_BE_PPDU_STAT_RPT_DMA);

	return 0;
}

static bool rtw89_mac_get_txpwr_cr_be(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx,
				      u32 reg_base, u32 *cr)
{
	enum rtw89_qta_mode mode = rtwdev->mac.qta_mode;
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
					 struct rtw89_vif_link *rtwvif_link,
					 struct rtw89_sta_link *rtwsta_link)
{
	u8 nc = 1, nr = 3, ng = 0, cb = 1, cs = 1, ldpc_en = 1, stbc_en = 1;
	struct ieee80211_link_sta *link_sta;
	u8 mac_idx = rtwvif_link->mac_idx;
	u8 port_sel = rtwvif_link->port;
	u8 sound_dim = 3, t;
	u8 *phy_cap;
	u32 reg;
	u16 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);
	phy_cap = link_sta->he_cap.he_cap_elem.phy_cap_info;

	if ((phy_cap[3] & IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER) ||
	    (phy_cap[4] & IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER)) {
		ldpc_en &= !!(phy_cap[1] & IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD);
		stbc_en &= !!(phy_cap[2] & IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ);
		t = u8_get_bits(phy_cap[5],
				IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK);
		sound_dim = min(sound_dim, t);
	}

	if ((link_sta->vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE) ||
	    (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE)) {
		ldpc_en &= !!(link_sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC);
		stbc_en &= !!(link_sta->vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_MASK);
		t = u32_get_bits(link_sta->vht_cap.cap,
				 IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK);
		sound_dim = min(sound_dim, t);
	}

	nc = min(nc, sound_dim);
	nr = min(nr, sound_dim);

	rcu_read_unlock();

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
				 struct rtw89_vif_link *rtwvif_link,
				 struct rtw89_sta_link *rtwsta_link)
{
	u32 rrsc = BIT(RTW89_MAC_BF_RRSC_6M) | BIT(RTW89_MAC_BF_RRSC_24M);
	struct ieee80211_link_sta *link_sta;
	u8 mac_idx = rtwvif_link->mac_idx;
	int ret;
	u32 reg;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);

	if (link_sta->he_cap.has_he) {
		rrsc |= (BIT(RTW89_MAC_BF_RRSC_HE_MSC0) |
			 BIT(RTW89_MAC_BF_RRSC_HE_MSC3) |
			 BIT(RTW89_MAC_BF_RRSC_HE_MSC5));
	}
	if (link_sta->vht_cap.vht_supported) {
		rrsc |= (BIT(RTW89_MAC_BF_RRSC_VHT_MSC0) |
			 BIT(RTW89_MAC_BF_RRSC_VHT_MSC3) |
			 BIT(RTW89_MAC_BF_RRSC_VHT_MSC5));
	}
	if (link_sta->ht_cap.ht_supported) {
		rrsc |= (BIT(RTW89_MAC_BF_RRSC_HT_MSC0) |
			 BIT(RTW89_MAC_BF_RRSC_HT_MSC3) |
			 BIT(RTW89_MAC_BF_RRSC_HT_MSC5));
	}

	rcu_read_unlock();

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_CTRL_0, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_BE_BFMEE_BFPARAM_SEL);
	rtw89_write32_clr(rtwdev, reg, B_BE_BFMEE_CSI_FORCE_RETE_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_TRXPTCL_RESP_CSI_RRSC, mac_idx);
	rtw89_write32(rtwdev, reg, rrsc);

	return 0;
}

static void rtw89_mac_bf_assoc_be(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link,
				  struct rtw89_sta_link *rtwsta_link)
{
	struct ieee80211_link_sta *link_sta;
	bool has_beamformer_cap;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);
	has_beamformer_cap = rtw89_sta_has_beamformer_cap(link_sta);

	rcu_read_unlock();

	if (has_beamformer_cap) {
		rtw89_debug(rtwdev, RTW89_DBG_BF,
			    "initialize bfee for new association\n");
		rtw89_mac_init_bfee_be(rtwdev, rtwvif_link->mac_idx);
		rtw89_mac_set_csi_para_reg_be(rtwdev, rtwvif_link, rtwsta_link);
		rtw89_mac_csi_rrsc_be(rtwdev, rtwvif_link, rtwsta_link);
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

static int rtw89_wow_config_mac_be(struct rtw89_dev *rtwdev, bool enable_wow)
{
	if (enable_wow) {
		rtw89_write32_set(rtwdev, R_BE_RX_STOP, B_BE_HOST_RX_STOP);
		rtw89_write32_clr(rtwdev, R_BE_RX_FLTR_OPT, B_BE_SNIFFER_MODE);
		rtw89_mac_cpu_io_rx(rtwdev, enable_wow);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
		rtw89_write32(rtwdev, R_BE_FWD_ERR, 0);
		rtw89_write32(rtwdev, R_BE_FWD_ACTN0, 0);
		rtw89_write32(rtwdev, R_BE_FWD_ACTN1, 0);
		rtw89_write32(rtwdev, R_BE_FWD_ACTN2, 0);
		rtw89_write32(rtwdev, R_BE_FWD_TF0, 0);
		rtw89_write32(rtwdev, R_BE_FWD_TF1, 0);
		rtw89_write32(rtwdev, R_BE_FWD_ERR, 0);
		rtw89_write32(rtwdev, R_BE_HW_PPDU_STATUS, 0);
		rtw89_write8(rtwdev, R_BE_DBG_WOW_READY, WOWLAN_NOT_READY);
	} else {
		rtw89_mac_cpu_io_rx(rtwdev, enable_wow);
		rtw89_write32_clr(rtwdev, R_BE_RX_STOP, B_BE_HOST_RX_STOP);
		rtw89_write32_set(rtwdev, R_BE_RX_FLTR_OPT, R_BE_RX_FLTR_OPT);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
	}

	return 0;
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
	.ps_status = R_BE_WMTX_POWER_BE_BIT_CTL,

	.muedca_ctrl = {
		.addr = R_BE_MUEDCA_EN,
		.mask = B_BE_MUEDCA_EN_0 | B_BE_SET_MUEDCATIMER_TF_0,
	},
	.bfee_ctrl = {
		.addr = R_BE_BFMEE_RESP_OPTION,
		.mask = B_BE_BFMEE_HT_NDPA_EN | B_BE_BFMEE_VHT_NDPA_EN |
			B_BE_BFMEE_HE_NDPA_EN | B_BE_BFMEE_EHT_NDPA_EN,
	},
	.narrow_bw_ru_dis = {
		.addr = R_BE_RXTRIG_TEST_USER_2,
		.mask = B_BE_RXTRIG_RU26_DIS,
	},
	.wow_ctrl = {.addr = R_BE_WOW_CTRL, .mask = B_BE_WOW_WOWEN,},

	.check_mac_en = rtw89_mac_check_mac_en_be,
	.sys_init = sys_init_be,
	.trx_init = trx_init_be,
	.hci_func_en = rtw89_mac_hci_func_en_be,
	.dmac_func_pre_en = rtw89_mac_dmac_func_pre_en_be,
	.dle_func_en = dle_func_en_be,
	.dle_clk_en = dle_clk_en_be,
	.bf_assoc = rtw89_mac_bf_assoc_be,

	.typ_fltr_opt = rtw89_mac_typ_fltr_opt_be,
	.cfg_ppdu_status = rtw89_mac_cfg_ppdu_status_be,

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
	.dle_quota_change = dle_quota_change_be,

	.disable_cpu = rtw89_mac_disable_cpu_be,
	.fwdl_enable_wcpu = rtw89_mac_fwdl_enable_wcpu_be,
	.fwdl_get_status = fwdl_get_status_be,
	.fwdl_check_path_ready = rtw89_fwdl_check_path_ready_be,
	.fwdl_secure_idmem_share_mode = NULL,
	.parse_efuse_map = rtw89_parse_efuse_map_be,
	.parse_phycap_map = rtw89_parse_phycap_map_be,
	.cnv_efuse_state = rtw89_cnv_efuse_state_be,
	.efuse_read_fw_secure = rtw89_efuse_read_fw_secure_be,

	.cfg_plt = rtw89_mac_cfg_plt_be,
	.get_plt_cnt = rtw89_mac_get_plt_cnt_be,

	.get_txpwr_cr = rtw89_mac_get_txpwr_cr_be,

	.write_xtal_si = rtw89_mac_write_xtal_si_be,
	.read_xtal_si = rtw89_mac_read_xtal_si_be,

	.dump_qta_lost = rtw89_mac_dump_qta_lost_be,
	.dump_err_status = rtw89_mac_dump_err_status_be,

	.is_txq_empty = mac_is_txq_empty_be,

	.add_chan_list = rtw89_hw_scan_add_chan_list_be,
	.add_chan_list_pno = rtw89_pno_scan_add_chan_list_be,
	.scan_offload = rtw89_fw_h2c_scan_offload_be,

	.wow_config_mac = rtw89_wow_config_mac_be,
};
EXPORT_SYMBOL(rtw89_mac_gen_be);
