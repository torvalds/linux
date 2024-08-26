// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "cam.h"
#include "chan.h"
#include "debug.h"
#include "efuse.h"
#include "fw.h"
#include "mac.h"
#include "pci.h"
#include "ps.h"
#include "reg.h"
#include "util.h"

static const u32 rtw89_mac_mem_base_addrs_ax[RTW89_MAC_MEM_NUM] = {
	[RTW89_MAC_MEM_AXIDMA]	        = AXIDMA_BASE_ADDR,
	[RTW89_MAC_MEM_SHARED_BUF]	= SHARED_BUF_BASE_ADDR,
	[RTW89_MAC_MEM_DMAC_TBL]	= DMAC_TBL_BASE_ADDR,
	[RTW89_MAC_MEM_SHCUT_MACHDR]	= SHCUT_MACHDR_BASE_ADDR,
	[RTW89_MAC_MEM_STA_SCHED]	= STA_SCHED_BASE_ADDR,
	[RTW89_MAC_MEM_RXPLD_FLTR_CAM]	= RXPLD_FLTR_CAM_BASE_ADDR,
	[RTW89_MAC_MEM_SECURITY_CAM]	= SECURITY_CAM_BASE_ADDR,
	[RTW89_MAC_MEM_WOW_CAM]		= WOW_CAM_BASE_ADDR,
	[RTW89_MAC_MEM_CMAC_TBL]	= CMAC_TBL_BASE_ADDR,
	[RTW89_MAC_MEM_ADDR_CAM]	= ADDR_CAM_BASE_ADDR,
	[RTW89_MAC_MEM_BA_CAM]		= BA_CAM_BASE_ADDR,
	[RTW89_MAC_MEM_BCN_IE_CAM0]	= BCN_IE_CAM0_BASE_ADDR,
	[RTW89_MAC_MEM_BCN_IE_CAM1]	= BCN_IE_CAM1_BASE_ADDR,
	[RTW89_MAC_MEM_TXD_FIFO_0]	= TXD_FIFO_0_BASE_ADDR,
	[RTW89_MAC_MEM_TXD_FIFO_1]	= TXD_FIFO_1_BASE_ADDR,
	[RTW89_MAC_MEM_TXDATA_FIFO_0]	= TXDATA_FIFO_0_BASE_ADDR,
	[RTW89_MAC_MEM_TXDATA_FIFO_1]	= TXDATA_FIFO_1_BASE_ADDR,
	[RTW89_MAC_MEM_CPU_LOCAL]	= CPU_LOCAL_BASE_ADDR,
	[RTW89_MAC_MEM_BSSID_CAM]	= BSSID_CAM_BASE_ADDR,
	[RTW89_MAC_MEM_TXD_FIFO_0_V1]	= TXD_FIFO_0_BASE_ADDR_V1,
	[RTW89_MAC_MEM_TXD_FIFO_1_V1]	= TXD_FIFO_1_BASE_ADDR_V1,
};

static void rtw89_mac_mem_write(struct rtw89_dev *rtwdev, u32 offset,
				u32 val, enum rtw89_mac_mem_sel sel)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u32 addr = mac->mem_base_addrs[sel] + offset;

	rtw89_write32(rtwdev, mac->filter_model_addr, addr);
	rtw89_write32(rtwdev, mac->indir_access_addr, val);
}

static u32 rtw89_mac_mem_read(struct rtw89_dev *rtwdev, u32 offset,
			      enum rtw89_mac_mem_sel sel)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u32 addr = mac->mem_base_addrs[sel] + offset;

	rtw89_write32(rtwdev, mac->filter_model_addr, addr);
	return rtw89_read32(rtwdev, mac->indir_access_addr);
}

static int rtw89_mac_check_mac_en_ax(struct rtw89_dev *rtwdev, u8 mac_idx,
				     enum rtw89_mac_hwmod_sel sel)
{
	u32 val, r_val;

	if (sel == RTW89_DMAC_SEL) {
		r_val = rtw89_read32(rtwdev, R_AX_DMAC_FUNC_EN);
		val = (B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN);
	} else if (sel == RTW89_CMAC_SEL && mac_idx == 0) {
		r_val = rtw89_read32(rtwdev, R_AX_CMAC_FUNC_EN);
		val = B_AX_CMAC_EN;
	} else if (sel == RTW89_CMAC_SEL && mac_idx == 1) {
		r_val = rtw89_read32(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND);
		val = B_AX_CMAC1_FEN;
	} else {
		return -EINVAL;
	}
	if (r_val == RTW89_R32_EA || r_val == RTW89_R32_DEAD ||
	    (val & r_val) != val)
		return -EFAULT;

	return 0;
}

int rtw89_mac_write_lte(struct rtw89_dev *rtwdev, const u32 offset, u32 val)
{
	u8 lte_ctrl;
	int ret;

	ret = read_poll_timeout(rtw89_read8, lte_ctrl, (lte_ctrl & BIT(5)) != 0,
				50, 50000, false, rtwdev, R_AX_LTE_CTRL + 3);
	if (ret)
		rtw89_err(rtwdev, "[ERR]lte not ready(W)\n");

	rtw89_write32(rtwdev, R_AX_LTE_WDATA, val);
	rtw89_write32(rtwdev, R_AX_LTE_CTRL, 0xC00F0000 | offset);

	return ret;
}

int rtw89_mac_read_lte(struct rtw89_dev *rtwdev, const u32 offset, u32 *val)
{
	u8 lte_ctrl;
	int ret;

	ret = read_poll_timeout(rtw89_read8, lte_ctrl, (lte_ctrl & BIT(5)) != 0,
				50, 50000, false, rtwdev, R_AX_LTE_CTRL + 3);
	if (ret)
		rtw89_err(rtwdev, "[ERR]lte not ready(W)\n");

	rtw89_write32(rtwdev, R_AX_LTE_CTRL, 0x800F0000 | offset);
	*val = rtw89_read32(rtwdev, R_AX_LTE_RDATA);

	return ret;
}

int rtw89_mac_dle_dfi_cfg(struct rtw89_dev *rtwdev, struct rtw89_mac_dle_dfi_ctrl *ctrl)
{
	u32 ctrl_reg, data_reg, ctrl_data;
	u32 val;
	int ret;

	switch (ctrl->type) {
	case DLE_CTRL_TYPE_WDE:
		ctrl_reg = R_AX_WDE_DBG_FUN_INTF_CTL;
		data_reg = R_AX_WDE_DBG_FUN_INTF_DATA;
		ctrl_data = FIELD_PREP(B_AX_WDE_DFI_TRGSEL_MASK, ctrl->target) |
			    FIELD_PREP(B_AX_WDE_DFI_ADDR_MASK, ctrl->addr) |
			    B_AX_WDE_DFI_ACTIVE;
		break;
	case DLE_CTRL_TYPE_PLE:
		ctrl_reg = R_AX_PLE_DBG_FUN_INTF_CTL;
		data_reg = R_AX_PLE_DBG_FUN_INTF_DATA;
		ctrl_data = FIELD_PREP(B_AX_PLE_DFI_TRGSEL_MASK, ctrl->target) |
			    FIELD_PREP(B_AX_PLE_DFI_ADDR_MASK, ctrl->addr) |
			    B_AX_PLE_DFI_ACTIVE;
		break;
	default:
		rtw89_warn(rtwdev, "[ERR] dfi ctrl type %d\n", ctrl->type);
		return -EINVAL;
	}

	rtw89_write32(rtwdev, ctrl_reg, ctrl_data);

	ret = read_poll_timeout_atomic(rtw89_read32, val, !(val & B_AX_WDE_DFI_ACTIVE),
				       1, 1000, false, rtwdev, ctrl_reg);
	if (ret) {
		rtw89_warn(rtwdev, "[ERR] dle dfi ctrl 0x%X set 0x%X timeout\n",
			   ctrl_reg, ctrl_data);
		return ret;
	}

	ctrl->out_data = rtw89_read32(rtwdev, data_reg);
	return 0;
}

int rtw89_mac_dle_dfi_quota_cfg(struct rtw89_dev *rtwdev,
				struct rtw89_mac_dle_dfi_quota *quota)
{
	struct rtw89_mac_dle_dfi_ctrl ctrl;
	int ret;

	ctrl.type = quota->dle_type;
	ctrl.target = DLE_DFI_TYPE_QUOTA;
	ctrl.addr = quota->qtaid;
	ret = rtw89_mac_dle_dfi_cfg(rtwdev, &ctrl);
	if (ret) {
		rtw89_warn(rtwdev, "[ERR] dle dfi quota %d\n", ret);
		return ret;
	}

	quota->rsv_pgnum = FIELD_GET(B_AX_DLE_RSV_PGNUM, ctrl.out_data);
	quota->use_pgnum = FIELD_GET(B_AX_DLE_USE_PGNUM, ctrl.out_data);
	return 0;
}

int rtw89_mac_dle_dfi_qempty_cfg(struct rtw89_dev *rtwdev,
				 struct rtw89_mac_dle_dfi_qempty *qempty)
{
	struct rtw89_mac_dle_dfi_ctrl ctrl;
	u32 ret;

	ctrl.type = qempty->dle_type;
	ctrl.target = DLE_DFI_TYPE_QEMPTY;
	ctrl.addr = qempty->grpsel;
	ret = rtw89_mac_dle_dfi_cfg(rtwdev, &ctrl);
	if (ret) {
		rtw89_warn(rtwdev, "[ERR] dle dfi qempty %d\n", ret);
		return ret;
	}

	qempty->qempty = FIELD_GET(B_AX_DLE_QEMPTY_GRP, ctrl.out_data);
	return 0;
}

static void dump_err_status_dispatcher_ax(struct rtw89_dev *rtwdev)
{
	rtw89_info(rtwdev, "R_AX_HOST_DISPATCHER_ALWAYS_IMR=0x%08x ",
		   rtw89_read32(rtwdev, R_AX_HOST_DISPATCHER_ERR_IMR));
	rtw89_info(rtwdev, "R_AX_HOST_DISPATCHER_ALWAYS_ISR=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_HOST_DISPATCHER_ERR_ISR));
	rtw89_info(rtwdev, "R_AX_CPU_DISPATCHER_ALWAYS_IMR=0x%08x ",
		   rtw89_read32(rtwdev, R_AX_CPU_DISPATCHER_ERR_IMR));
	rtw89_info(rtwdev, "R_AX_CPU_DISPATCHER_ALWAYS_ISR=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_CPU_DISPATCHER_ERR_ISR));
	rtw89_info(rtwdev, "R_AX_OTHER_DISPATCHER_ALWAYS_IMR=0x%08x ",
		   rtw89_read32(rtwdev, R_AX_OTHER_DISPATCHER_ERR_IMR));
	rtw89_info(rtwdev, "R_AX_OTHER_DISPATCHER_ALWAYS_ISR=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_OTHER_DISPATCHER_ERR_ISR));
}

static void rtw89_mac_dump_qta_lost_ax(struct rtw89_dev *rtwdev)
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

	val = rtw89_read32(rtwdev, R_AX_PLE_QTA6_CFG);
	rtw89_info(rtwdev, "[PLE][CMAC0_RX]min_pgnum=0x%x\n",
		   u32_get_bits(val, B_AX_PLE_Q6_MIN_SIZE_MASK));
	rtw89_info(rtwdev, "[PLE][CMAC0_RX]max_pgnum=0x%x\n",
		   u32_get_bits(val, B_AX_PLE_Q6_MAX_SIZE_MASK));
	val = rtw89_read32(rtwdev, R_AX_RX_FLTR_OPT);
	rtw89_info(rtwdev, "[PLE][CMAC0_RX]B_AX_RX_MPDU_MAX_LEN=0x%x\n",
		   u32_get_bits(val, B_AX_RX_MPDU_MAX_LEN_MASK));
	rtw89_info(rtwdev, "R_AX_RSP_CHK_SIG=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_RSP_CHK_SIG));
	rtw89_info(rtwdev, "R_AX_TRXPTCL_RESP_0=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_TRXPTCL_RESP_0));
	rtw89_info(rtwdev, "R_AX_CCA_CONTROL=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_CCA_CONTROL));

	if (!rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_1, RTW89_CMAC_SEL)) {
		quota.dle_type = DLE_CTRL_TYPE_PLE;
		quota.qtaid = 7;
		ret = rtw89_mac_dle_dfi_quota_cfg(rtwdev, &quota);
		if (ret)
			rtw89_warn(rtwdev, "%s: query DLE fail\n", __func__);
		else
			rtw89_info(rtwdev, "quota7 rsv/use: 0x%x/0x%x\n",
				   quota.rsv_pgnum, quota.use_pgnum);

		val = rtw89_read32(rtwdev, R_AX_PLE_QTA7_CFG);
		rtw89_info(rtwdev, "[PLE][CMAC1_RX]min_pgnum=0x%x\n",
			   u32_get_bits(val, B_AX_PLE_Q7_MIN_SIZE_MASK));
		rtw89_info(rtwdev, "[PLE][CMAC1_RX]max_pgnum=0x%x\n",
			   u32_get_bits(val, B_AX_PLE_Q7_MAX_SIZE_MASK));
		val = rtw89_read32(rtwdev, R_AX_RX_FLTR_OPT_C1);
		rtw89_info(rtwdev, "[PLE][CMAC1_RX]B_AX_RX_MPDU_MAX_LEN=0x%x\n",
			   u32_get_bits(val, B_AX_RX_MPDU_MAX_LEN_MASK));
		rtw89_info(rtwdev, "R_AX_RSP_CHK_SIG_C1=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_RSP_CHK_SIG_C1));
		rtw89_info(rtwdev, "R_AX_TRXPTCL_RESP_0_C1=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_TRXPTCL_RESP_0_C1));
		rtw89_info(rtwdev, "R_AX_CCA_CONTROL_C1=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_CCA_CONTROL_C1));
	}

	rtw89_info(rtwdev, "R_AX_DLE_EMPTY0=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_DLE_EMPTY0));
	rtw89_info(rtwdev, "R_AX_DLE_EMPTY1=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_DLE_EMPTY1));

	dump_err_status_dispatcher_ax(rtwdev);
}

void rtw89_mac_dump_l0_to_l1(struct rtw89_dev *rtwdev,
			     enum mac_ax_err_info err)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u32 dbg, event;

	dbg = rtw89_read32(rtwdev, R_AX_SER_DBG_INFO);
	event = u32_get_bits(dbg, B_AX_L0_TO_L1_EVENT_MASK);

	switch (event) {
	case MAC_AX_L0_TO_L1_RX_QTA_LOST:
		rtw89_info(rtwdev, "quota lost!\n");
		mac->dump_qta_lost(rtwdev);
		break;
	default:
		break;
	}
}

void rtw89_mac_dump_dmac_err_status(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u32 dmac_err;
	int i, ret;

	ret = rtw89_mac_check_mac_en(rtwdev, 0, RTW89_DMAC_SEL);
	if (ret) {
		rtw89_warn(rtwdev, "[DMAC] : DMAC not enabled\n");
		return;
	}

	dmac_err = rtw89_read32(rtwdev, R_AX_DMAC_ERR_ISR);
	rtw89_info(rtwdev, "R_AX_DMAC_ERR_ISR=0x%08x\n", dmac_err);
	rtw89_info(rtwdev, "R_AX_DMAC_ERR_IMR=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_DMAC_ERR_IMR));

	if (dmac_err) {
		rtw89_info(rtwdev, "R_AX_WDE_ERR_FLAG_CFG=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WDE_ERR_FLAG_CFG_NUM1));
		rtw89_info(rtwdev, "R_AX_PLE_ERR_FLAG_CFG=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PLE_ERR_FLAG_CFG_NUM1));
		if (chip->chip_id == RTL8852C) {
			rtw89_info(rtwdev, "R_AX_PLE_ERRFLAG_MSG=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_PLE_ERRFLAG_MSG));
			rtw89_info(rtwdev, "R_AX_WDE_ERRFLAG_MSG=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_WDE_ERRFLAG_MSG));
			rtw89_info(rtwdev, "R_AX_PLE_DBGERR_LOCKEN=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_PLE_DBGERR_LOCKEN));
			rtw89_info(rtwdev, "R_AX_PLE_DBGERR_STS=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_PLE_DBGERR_STS));
		}
	}

	if (dmac_err & B_AX_WDRLS_ERR_FLAG) {
		rtw89_info(rtwdev, "R_AX_WDRLS_ERR_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WDRLS_ERR_IMR));
		rtw89_info(rtwdev, "R_AX_WDRLS_ERR_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WDRLS_ERR_ISR));
		if (chip->chip_id == RTL8852C)
			rtw89_info(rtwdev, "R_AX_RPQ_RXBD_IDX=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_RPQ_RXBD_IDX_V1));
		else
			rtw89_info(rtwdev, "R_AX_RPQ_RXBD_IDX=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_RPQ_RXBD_IDX));
	}

	if (dmac_err & B_AX_WSEC_ERR_FLAG) {
		if (chip->chip_id == RTL8852C) {
			rtw89_info(rtwdev, "R_AX_SEC_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_ERROR_FLAG_IMR));
			rtw89_info(rtwdev, "R_AX_SEC_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_ERROR_FLAG));
			rtw89_info(rtwdev, "R_AX_SEC_ENG_CTRL=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_ENG_CTRL));
			rtw89_info(rtwdev, "R_AX_SEC_MPDU_PROC=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_MPDU_PROC));
			rtw89_info(rtwdev, "R_AX_SEC_CAM_ACCESS=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_CAM_ACCESS));
			rtw89_info(rtwdev, "R_AX_SEC_CAM_RDATA=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_CAM_RDATA));
			rtw89_info(rtwdev, "R_AX_SEC_DEBUG1=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_DEBUG1));
			rtw89_info(rtwdev, "R_AX_SEC_TX_DEBUG=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_TX_DEBUG));
			rtw89_info(rtwdev, "R_AX_SEC_RX_DEBUG=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_RX_DEBUG));

			rtw89_write32_mask(rtwdev, R_AX_DBG_CTRL,
					   B_AX_DBG_SEL0, 0x8B);
			rtw89_write32_mask(rtwdev, R_AX_DBG_CTRL,
					   B_AX_DBG_SEL1, 0x8B);
			rtw89_write32_mask(rtwdev, R_AX_SYS_STATUS1,
					   B_AX_SEL_0XC0_MASK, 1);
			for (i = 0; i < 0x10; i++) {
				rtw89_write32_mask(rtwdev, R_AX_SEC_ENG_CTRL,
						   B_AX_SEC_DBG_PORT_FIELD_MASK, i);
				rtw89_info(rtwdev, "sel=%x,R_AX_SEC_DEBUG2=0x%08x\n",
					   i, rtw89_read32(rtwdev, R_AX_SEC_DEBUG2));
			}
		} else if (chip->chip_id == RTL8922A) {
			rtw89_info(rtwdev, "R_BE_SEC_ERROR_FLAG=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_SEC_ERROR_FLAG));
			rtw89_info(rtwdev, "R_BE_SEC_ERROR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_SEC_ERROR_IMR));
			rtw89_info(rtwdev, "R_BE_SEC_ENG_CTRL=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_SEC_ENG_CTRL));
			rtw89_info(rtwdev, "R_BE_SEC_MPDU_PROC=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_SEC_MPDU_PROC));
			rtw89_info(rtwdev, "R_BE_SEC_CAM_ACCESS=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_SEC_CAM_ACCESS));
			rtw89_info(rtwdev, "R_BE_SEC_CAM_RDATA=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_SEC_CAM_RDATA));
			rtw89_info(rtwdev, "R_BE_SEC_DEBUG2=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_SEC_DEBUG2));
		} else {
			rtw89_info(rtwdev, "R_AX_SEC_ERR_IMR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_DEBUG));
			rtw89_info(rtwdev, "R_AX_SEC_ENG_CTRL=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_ENG_CTRL));
			rtw89_info(rtwdev, "R_AX_SEC_MPDU_PROC=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_MPDU_PROC));
			rtw89_info(rtwdev, "R_AX_SEC_CAM_ACCESS=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_CAM_ACCESS));
			rtw89_info(rtwdev, "R_AX_SEC_CAM_RDATA=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_CAM_RDATA));
			rtw89_info(rtwdev, "R_AX_SEC_CAM_WDATA=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_CAM_WDATA));
			rtw89_info(rtwdev, "R_AX_SEC_TX_DEBUG=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_TX_DEBUG));
			rtw89_info(rtwdev, "R_AX_SEC_RX_DEBUG=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_RX_DEBUG));
			rtw89_info(rtwdev, "R_AX_SEC_TRX_PKT_CNT=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_TRX_PKT_CNT));
			rtw89_info(rtwdev, "R_AX_SEC_TRX_BLK_CNT=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_SEC_TRX_BLK_CNT));
		}
	}

	if (dmac_err & B_AX_MPDU_ERR_FLAG) {
		rtw89_info(rtwdev, "R_AX_MPDU_TX_ERR_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_MPDU_TX_ERR_IMR));
		rtw89_info(rtwdev, "R_AX_MPDU_TX_ERR_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_MPDU_TX_ERR_ISR));
		rtw89_info(rtwdev, "R_AX_MPDU_RX_ERR_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_MPDU_RX_ERR_IMR));
		rtw89_info(rtwdev, "R_AX_MPDU_RX_ERR_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_MPDU_RX_ERR_ISR));
	}

	if (dmac_err & B_AX_STA_SCHEDULER_ERR_FLAG) {
		if (chip->chip_id == RTL8922A) {
			rtw89_info(rtwdev, "R_BE_INTERRUPT_MASK_REG=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_INTERRUPT_MASK_REG));
			rtw89_info(rtwdev, "R_BE_INTERRUPT_STS_REG=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_INTERRUPT_STS_REG));
		} else {
			rtw89_info(rtwdev, "R_AX_STA_SCHEDULER_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_STA_SCHEDULER_ERR_IMR));
			rtw89_info(rtwdev, "R_AX_STA_SCHEDULER_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_STA_SCHEDULER_ERR_ISR));
		}
	}

	if (dmac_err & B_AX_WDE_DLE_ERR_FLAG) {
		rtw89_info(rtwdev, "R_AX_WDE_ERR_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WDE_ERR_IMR));
		rtw89_info(rtwdev, "R_AX_WDE_ERR_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WDE_ERR_ISR));
		rtw89_info(rtwdev, "R_AX_PLE_ERR_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PLE_ERR_IMR));
		rtw89_info(rtwdev, "R_AX_PLE_ERR_FLAG_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PLE_ERR_FLAG_ISR));
	}

	if (dmac_err & B_AX_TXPKTCTRL_ERR_FLAG) {
		if (chip->chip_id == RTL8852C || chip->chip_id == RTL8922A) {
			rtw89_info(rtwdev, "R_AX_TXPKTCTL_B0_ERRFLAG_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_TXPKTCTL_B0_ERRFLAG_IMR));
			rtw89_info(rtwdev, "R_AX_TXPKTCTL_B0_ERRFLAG_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_TXPKTCTL_B0_ERRFLAG_ISR));
			rtw89_info(rtwdev, "R_AX_TXPKTCTL_B1_ERRFLAG_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_TXPKTCTL_B1_ERRFLAG_IMR));
			rtw89_info(rtwdev, "R_AX_TXPKTCTL_B1_ERRFLAG_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_TXPKTCTL_B1_ERRFLAG_ISR));
		} else {
			rtw89_info(rtwdev, "R_AX_TXPKTCTL_ERR_IMR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_TXPKTCTL_ERR_IMR_ISR));
			rtw89_info(rtwdev, "R_AX_TXPKTCTL_ERR_IMR_ISR_B1=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_TXPKTCTL_ERR_IMR_ISR_B1));
		}
	}

	if (dmac_err & B_AX_PLE_DLE_ERR_FLAG) {
		rtw89_info(rtwdev, "R_AX_WDE_ERR_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WDE_ERR_IMR));
		rtw89_info(rtwdev, "R_AX_WDE_ERR_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WDE_ERR_ISR));
		rtw89_info(rtwdev, "R_AX_PLE_ERR_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PLE_ERR_IMR));
		rtw89_info(rtwdev, "R_AX_PLE_ERR_FLAG_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PLE_ERR_FLAG_ISR));
		rtw89_info(rtwdev, "R_AX_WD_CPUQ_OP_0=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WD_CPUQ_OP_0));
		rtw89_info(rtwdev, "R_AX_WD_CPUQ_OP_1=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WD_CPUQ_OP_1));
		rtw89_info(rtwdev, "R_AX_WD_CPUQ_OP_2=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_WD_CPUQ_OP_2));
		rtw89_info(rtwdev, "R_AX_PL_CPUQ_OP_0=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PL_CPUQ_OP_0));
		rtw89_info(rtwdev, "R_AX_PL_CPUQ_OP_1=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PL_CPUQ_OP_1));
		rtw89_info(rtwdev, "R_AX_PL_CPUQ_OP_2=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PL_CPUQ_OP_2));
		if (chip->chip_id == RTL8922A) {
			rtw89_info(rtwdev, "R_BE_WD_CPUQ_OP_3=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_WD_CPUQ_OP_3));
			rtw89_info(rtwdev, "R_BE_WD_CPUQ_OP_STATUS=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_WD_CPUQ_OP_STATUS));
			rtw89_info(rtwdev, "R_BE_PLE_CPUQ_OP_3=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_PL_CPUQ_OP_3));
			rtw89_info(rtwdev, "R_BE_PL_CPUQ_OP_STATUS=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_PL_CPUQ_OP_STATUS));
		} else {
			rtw89_info(rtwdev, "R_AX_WD_CPUQ_OP_STATUS=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_WD_CPUQ_OP_STATUS));
			rtw89_info(rtwdev, "R_AX_PL_CPUQ_OP_STATUS=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_PL_CPUQ_OP_STATUS));
			if (chip->chip_id == RTL8852C) {
				rtw89_info(rtwdev, "R_AX_RX_CTRL0=0x%08x\n",
					   rtw89_read32(rtwdev, R_AX_RX_CTRL0));
				rtw89_info(rtwdev, "R_AX_RX_CTRL1=0x%08x\n",
					   rtw89_read32(rtwdev, R_AX_RX_CTRL1));
				rtw89_info(rtwdev, "R_AX_RX_CTRL2=0x%08x\n",
					   rtw89_read32(rtwdev, R_AX_RX_CTRL2));
			} else {
				rtw89_info(rtwdev, "R_AX_RXDMA_PKT_INFO_0=0x%08x\n",
					   rtw89_read32(rtwdev, R_AX_RXDMA_PKT_INFO_0));
				rtw89_info(rtwdev, "R_AX_RXDMA_PKT_INFO_1=0x%08x\n",
					   rtw89_read32(rtwdev, R_AX_RXDMA_PKT_INFO_1));
				rtw89_info(rtwdev, "R_AX_RXDMA_PKT_INFO_2=0x%08x\n",
					   rtw89_read32(rtwdev, R_AX_RXDMA_PKT_INFO_2));
			}
		}
	}

	if (dmac_err & B_AX_PKTIN_ERR_FLAG) {
		rtw89_info(rtwdev, "R_AX_PKTIN_ERR_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PKTIN_ERR_IMR));
		rtw89_info(rtwdev, "R_AX_PKTIN_ERR_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_AX_PKTIN_ERR_ISR));
	}

	if (dmac_err & B_AX_DISPATCH_ERR_FLAG) {
		if (chip->chip_id == RTL8922A) {
			rtw89_info(rtwdev, "R_BE_DISP_HOST_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_DISP_HOST_IMR));
			rtw89_info(rtwdev, "R_BE_DISP_ERROR_ISR1=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_DISP_ERROR_ISR1));
			rtw89_info(rtwdev, "R_BE_DISP_CPU_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_DISP_CPU_IMR));
			rtw89_info(rtwdev, "R_BE_DISP_ERROR_ISR2=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_DISP_ERROR_ISR2));
			rtw89_info(rtwdev, "R_BE_DISP_OTHER_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_DISP_OTHER_IMR));
			rtw89_info(rtwdev, "R_BE_DISP_ERROR_ISR0=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_DISP_ERROR_ISR0));
		} else {
			rtw89_info(rtwdev, "R_AX_HOST_DISPATCHER_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_HOST_DISPATCHER_ERR_IMR));
			rtw89_info(rtwdev, "R_AX_HOST_DISPATCHER_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_HOST_DISPATCHER_ERR_ISR));
			rtw89_info(rtwdev, "R_AX_CPU_DISPATCHER_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_CPU_DISPATCHER_ERR_IMR));
			rtw89_info(rtwdev, "R_AX_CPU_DISPATCHER_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_CPU_DISPATCHER_ERR_ISR));
			rtw89_info(rtwdev, "R_AX_OTHER_DISPATCHER_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_OTHER_DISPATCHER_ERR_IMR));
			rtw89_info(rtwdev, "R_AX_OTHER_DISPATCHER_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_OTHER_DISPATCHER_ERR_ISR));
		}
	}

	if (dmac_err & B_AX_BBRPT_ERR_FLAG) {
		if (chip->chip_id == RTL8852C || chip->chip_id == RTL8922A) {
			rtw89_info(rtwdev, "R_AX_BBRPT_COM_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_COM_ERR_IMR));
			rtw89_info(rtwdev, "R_AX_BBRPT_COM_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_COM_ERR_ISR));
			rtw89_info(rtwdev, "R_AX_BBRPT_CHINFO_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_CHINFO_ERR_ISR));
			rtw89_info(rtwdev, "R_AX_BBRPT_CHINFO_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_CHINFO_ERR_IMR));
			rtw89_info(rtwdev, "R_AX_BBRPT_DFS_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_DFS_ERR_IMR));
			rtw89_info(rtwdev, "R_AX_BBRPT_DFS_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_DFS_ERR_ISR));
		} else {
			rtw89_info(rtwdev, "R_AX_BBRPT_COM_ERR_IMR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_COM_ERR_IMR_ISR));
			rtw89_info(rtwdev, "R_AX_BBRPT_CHINFO_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_CHINFO_ERR_ISR));
			rtw89_info(rtwdev, "R_AX_BBRPT_CHINFO_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_CHINFO_ERR_IMR));
			rtw89_info(rtwdev, "R_AX_BBRPT_DFS_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_DFS_ERR_IMR));
			rtw89_info(rtwdev, "R_AX_BBRPT_DFS_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_BBRPT_DFS_ERR_ISR));
		}
		if (chip->chip_id == RTL8922A) {
			rtw89_info(rtwdev, "R_BE_LA_ERRFLAG_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_LA_ERRFLAG_IMR));
			rtw89_info(rtwdev, "R_BE_LA_ERRFLAG_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_LA_ERRFLAG_ISR));
		}
	}

	if (dmac_err & B_AX_HAXIDMA_ERR_FLAG) {
		if (chip->chip_id == RTL8922A) {
			rtw89_info(rtwdev, "R_BE_HAXI_IDCT_MSK=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_HAXI_IDCT_MSK));
			rtw89_info(rtwdev, "R_BE_HAXI_IDCT=0x%08x\n",
				   rtw89_read32(rtwdev, R_BE_HAXI_IDCT));
		} else if (chip->chip_id == RTL8852C) {
			rtw89_info(rtwdev, "R_AX_HAXIDMA_ERR_IMR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_HAXI_IDCT_MSK));
			rtw89_info(rtwdev, "R_AX_HAXIDMA_ERR_ISR=0x%08x\n",
				   rtw89_read32(rtwdev, R_AX_HAXI_IDCT));
		}
	}

	if (dmac_err & B_BE_P_AXIDMA_ERR_INT) {
		rtw89_info(rtwdev, "R_BE_PL_AXIDMA_IDCT_MSK=0x%08x\n",
			   rtw89_mac_mem_read(rtwdev, R_BE_PL_AXIDMA_IDCT_MSK,
					      RTW89_MAC_MEM_AXIDMA));
		rtw89_info(rtwdev, "R_BE_PL_AXIDMA_IDCT=0x%08x\n",
			   rtw89_mac_mem_read(rtwdev, R_BE_PL_AXIDMA_IDCT,
					      RTW89_MAC_MEM_AXIDMA));
	}

	if (dmac_err & B_BE_MLO_ERR_INT) {
		rtw89_info(rtwdev, "R_BE_MLO_ERR_IDCT_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_BE_MLO_ERR_IDCT_IMR));
		rtw89_info(rtwdev, "R_BE_PKTIN_ERR_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_BE_MLO_ERR_IDCT_ISR));
	}

	if (dmac_err & B_BE_PLRLS_ERR_INT) {
		rtw89_info(rtwdev, "R_BE_PLRLS_ERR_IMR=0x%08x\n",
			   rtw89_read32(rtwdev, R_BE_PLRLS_ERR_IMR));
		rtw89_info(rtwdev, "R_BE_PLRLS_ERR_ISR=0x%08x\n",
			   rtw89_read32(rtwdev, R_BE_PLRLS_ERR_ISR));
	}
}

static void rtw89_mac_dump_cmac_err_status_ax(struct rtw89_dev *rtwdev,
					      u8 band)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u32 offset = 0;
	u32 cmac_err;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, band, RTW89_CMAC_SEL);
	if (ret) {
		if (band)
			rtw89_warn(rtwdev, "[CMAC] : CMAC1 not enabled\n");
		else
			rtw89_warn(rtwdev, "[CMAC] : CMAC0 not enabled\n");
		return;
	}

	if (band)
		offset = RTW89_MAC_AX_BAND_REG_OFFSET;

	cmac_err = rtw89_read32(rtwdev, R_AX_CMAC_ERR_ISR + offset);
	rtw89_info(rtwdev, "R_AX_CMAC_ERR_ISR [%d]=0x%08x\n", band,
		   rtw89_read32(rtwdev, R_AX_CMAC_ERR_ISR + offset));
	rtw89_info(rtwdev, "R_AX_CMAC_FUNC_EN [%d]=0x%08x\n", band,
		   rtw89_read32(rtwdev, R_AX_CMAC_FUNC_EN + offset));
	rtw89_info(rtwdev, "R_AX_CK_EN [%d]=0x%08x\n", band,
		   rtw89_read32(rtwdev, R_AX_CK_EN + offset));

	if (cmac_err & B_AX_SCHEDULE_TOP_ERR_IND) {
		rtw89_info(rtwdev, "R_AX_SCHEDULE_ERR_IMR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_AX_SCHEDULE_ERR_IMR + offset));
		rtw89_info(rtwdev, "R_AX_SCHEDULE_ERR_ISR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_AX_SCHEDULE_ERR_ISR + offset));
	}

	if (cmac_err & B_AX_PTCL_TOP_ERR_IND) {
		rtw89_info(rtwdev, "R_AX_PTCL_IMR0 [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_AX_PTCL_IMR0 + offset));
		rtw89_info(rtwdev, "R_AX_PTCL_ISR0 [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_AX_PTCL_ISR0 + offset));
	}

	if (cmac_err & B_AX_DMA_TOP_ERR_IND) {
		if (chip->chip_id == RTL8852C) {
			rtw89_info(rtwdev, "R_AX_RX_ERR_FLAG [%d]=0x%08x\n", band,
				   rtw89_read32(rtwdev, R_AX_RX_ERR_FLAG + offset));
			rtw89_info(rtwdev, "R_AX_RX_ERR_FLAG_IMR [%d]=0x%08x\n", band,
				   rtw89_read32(rtwdev, R_AX_RX_ERR_FLAG_IMR + offset));
		} else {
			rtw89_info(rtwdev, "R_AX_DLE_CTRL [%d]=0x%08x\n", band,
				   rtw89_read32(rtwdev, R_AX_DLE_CTRL + offset));
		}
	}

	if (cmac_err & B_AX_DMA_TOP_ERR_IND || cmac_err & B_AX_WMAC_RX_ERR_IND) {
		if (chip->chip_id == RTL8852C) {
			rtw89_info(rtwdev, "R_AX_PHYINFO_ERR_ISR [%d]=0x%08x\n", band,
				   rtw89_read32(rtwdev, R_AX_PHYINFO_ERR_ISR + offset));
			rtw89_info(rtwdev, "R_AX_PHYINFO_ERR_IMR [%d]=0x%08x\n", band,
				   rtw89_read32(rtwdev, R_AX_PHYINFO_ERR_IMR + offset));
		} else {
			rtw89_info(rtwdev, "R_AX_PHYINFO_ERR_IMR [%d]=0x%08x\n", band,
				   rtw89_read32(rtwdev, R_AX_PHYINFO_ERR_IMR + offset));
		}
	}

	if (cmac_err & B_AX_TXPWR_CTRL_ERR_IND) {
		rtw89_info(rtwdev, "R_AX_TXPWR_IMR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_AX_TXPWR_IMR + offset));
		rtw89_info(rtwdev, "R_AX_TXPWR_ISR [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_AX_TXPWR_ISR + offset));
	}

	if (cmac_err & B_AX_WMAC_TX_ERR_IND) {
		if (chip->chip_id == RTL8852C) {
			rtw89_info(rtwdev, "R_AX_TRXPTCL_ERROR_INDICA [%d]=0x%08x\n", band,
				   rtw89_read32(rtwdev, R_AX_TRXPTCL_ERROR_INDICA + offset));
			rtw89_info(rtwdev, "R_AX_TRXPTCL_ERROR_INDICA_MASK [%d]=0x%08x\n", band,
				   rtw89_read32(rtwdev, R_AX_TRXPTCL_ERROR_INDICA_MASK + offset));
		} else {
			rtw89_info(rtwdev, "R_AX_TMAC_ERR_IMR_ISR [%d]=0x%08x\n", band,
				   rtw89_read32(rtwdev, R_AX_TMAC_ERR_IMR_ISR + offset));
		}
		rtw89_info(rtwdev, "R_AX_DBGSEL_TRXPTCL [%d]=0x%08x\n", band,
			   rtw89_read32(rtwdev, R_AX_DBGSEL_TRXPTCL + offset));
	}

	rtw89_info(rtwdev, "R_AX_CMAC_ERR_IMR [%d]=0x%08x\n", band,
		   rtw89_read32(rtwdev, R_AX_CMAC_ERR_IMR + offset));
}

static void rtw89_mac_dump_err_status_ax(struct rtw89_dev *rtwdev,
					 enum mac_ax_err_info err)
{
	if (err != MAC_AX_ERR_L1_ERR_DMAC &&
	    err != MAC_AX_ERR_L0_PROMOTE_TO_L1 &&
	    err != MAC_AX_ERR_L0_ERR_CMAC0 &&
	    err != MAC_AX_ERR_L0_ERR_CMAC1 &&
	    err != MAC_AX_ERR_RXI300)
		return;

	rtw89_info(rtwdev, "--->\nerr=0x%x\n", err);
	rtw89_info(rtwdev, "R_AX_SER_DBG_INFO =0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_SER_DBG_INFO));
	rtw89_info(rtwdev, "R_AX_SER_DBG_INFO =0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_SER_DBG_INFO));
	rtw89_info(rtwdev, "DBG Counter 1 (R_AX_DRV_FW_HSK_4)=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_DRV_FW_HSK_4));
	rtw89_info(rtwdev, "DBG Counter 2 (R_AX_DRV_FW_HSK_5)=0x%08x\n",
		   rtw89_read32(rtwdev, R_AX_DRV_FW_HSK_5));

	rtw89_mac_dump_dmac_err_status(rtwdev);
	rtw89_mac_dump_cmac_err_status_ax(rtwdev, RTW89_MAC_0);
	rtw89_mac_dump_cmac_err_status_ax(rtwdev, RTW89_MAC_1);

	rtwdev->hci.ops->dump_err_status(rtwdev);

	if (err == MAC_AX_ERR_L0_PROMOTE_TO_L1)
		rtw89_mac_dump_l0_to_l1(rtwdev, err);

	rtw89_info(rtwdev, "<---\n");
}

static bool rtw89_mac_suppress_log(struct rtw89_dev *rtwdev, u32 err)
{
	struct rtw89_ser *ser = &rtwdev->ser;
	u32 dmac_err, imr, isr;
	int ret;

	if (rtwdev->chip->chip_id == RTL8852C) {
		ret = rtw89_mac_check_mac_en(rtwdev, 0, RTW89_DMAC_SEL);
		if (ret)
			return true;

		if (err == MAC_AX_ERR_L1_ERR_DMAC) {
			dmac_err = rtw89_read32(rtwdev, R_AX_DMAC_ERR_ISR);
			imr = rtw89_read32(rtwdev, R_AX_TXPKTCTL_B0_ERRFLAG_IMR);
			isr = rtw89_read32(rtwdev, R_AX_TXPKTCTL_B0_ERRFLAG_ISR);

			if ((dmac_err & B_AX_TXPKTCTRL_ERR_FLAG) &&
			    ((isr & imr) & B_AX_B0_ISR_ERR_CMDPSR_FRZTO)) {
				set_bit(RTW89_SER_SUPPRESS_LOG, ser->flags);
				return true;
			}
		} else if (err == MAC_AX_ERR_L1_RESET_DISABLE_DMAC_DONE) {
			if (test_bit(RTW89_SER_SUPPRESS_LOG, ser->flags))
				return true;
		} else if (err == MAC_AX_ERR_L1_RESET_RECOVERY_DONE) {
			if (test_and_clear_bit(RTW89_SER_SUPPRESS_LOG, ser->flags))
				return true;
		}
	}

	return false;
}

u32 rtw89_mac_get_err_status(struct rtw89_dev *rtwdev)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u32 err, err_scnr;
	int ret;

	ret = read_poll_timeout(rtw89_read32, err, (err != 0), 1000, 100000,
				false, rtwdev, R_AX_HALT_C2H_CTRL);
	if (ret) {
		rtw89_warn(rtwdev, "Polling FW err status fail\n");
		return ret;
	}

	err = rtw89_read32(rtwdev, R_AX_HALT_C2H);
	rtw89_write32(rtwdev, R_AX_HALT_C2H_CTRL, 0);

	err_scnr = RTW89_ERROR_SCENARIO(err);
	if (err_scnr == RTW89_WCPU_CPU_EXCEPTION)
		err = MAC_AX_ERR_CPU_EXCEPTION;
	else if (err_scnr == RTW89_WCPU_ASSERTION)
		err = MAC_AX_ERR_ASSERTION;
	else if (err_scnr == RTW89_RXI300_ERROR)
		err = MAC_AX_ERR_RXI300;

	if (rtw89_mac_suppress_log(rtwdev, err))
		return err;

	rtw89_fw_st_dbg_dump(rtwdev);
	mac->dump_err_status(rtwdev, err);

	return err;
}
EXPORT_SYMBOL(rtw89_mac_get_err_status);

int rtw89_mac_set_err_status(struct rtw89_dev *rtwdev, u32 err)
{
	struct rtw89_ser *ser = &rtwdev->ser;
	u32 halt;
	int ret = 0;

	if (err > MAC_AX_SET_ERR_MAX) {
		rtw89_err(rtwdev, "Bad set-err-status value 0x%08x\n", err);
		return -EINVAL;
	}

	ret = read_poll_timeout(rtw89_read32, halt, (halt == 0x0), 1000,
				100000, false, rtwdev, R_AX_HALT_H2C_CTRL);
	if (ret) {
		rtw89_err(rtwdev, "FW doesn't receive previous msg\n");
		return -EFAULT;
	}

	rtw89_write32(rtwdev, R_AX_HALT_H2C, err);

	if (ser->prehandle_l1 &&
	    (err == MAC_AX_ERR_L1_DISABLE_EN || err == MAC_AX_ERR_L1_RCVY_EN))
		return 0;

	rtw89_write32(rtwdev, R_AX_HALT_H2C_CTRL, B_AX_HALT_H2C_TRIGGER);

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_set_err_status);

static int hfc_reset_param(struct rtw89_dev *rtwdev)
{
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	struct rtw89_hfc_param_ini param_ini = {NULL};
	u8 qta_mode = rtwdev->mac.dle_info.qta_mode;

	switch (rtwdev->hci.type) {
	case RTW89_HCI_TYPE_PCIE:
		param_ini = rtwdev->chip->hfc_param_ini[qta_mode];
		param->en = 0;
		break;
	default:
		return -EINVAL;
	}

	if (param_ini.pub_cfg)
		param->pub_cfg = *param_ini.pub_cfg;

	if (param_ini.prec_cfg)
		param->prec_cfg = *param_ini.prec_cfg;

	if (param_ini.ch_cfg)
		param->ch_cfg = param_ini.ch_cfg;

	memset(&param->ch_info, 0, sizeof(param->ch_info));
	memset(&param->pub_info, 0, sizeof(param->pub_info));
	param->mode = param_ini.mode;

	return 0;
}

static int hfc_ch_cfg_chk(struct rtw89_dev *rtwdev, u8 ch)
{
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	const struct rtw89_hfc_ch_cfg *ch_cfg = param->ch_cfg;
	const struct rtw89_hfc_pub_cfg *pub_cfg = &param->pub_cfg;
	const struct rtw89_hfc_prec_cfg *prec_cfg = &param->prec_cfg;

	if (ch >= RTW89_DMA_CH_NUM)
		return -EINVAL;

	if ((ch_cfg[ch].min && ch_cfg[ch].min < prec_cfg->ch011_prec) ||
	    ch_cfg[ch].max > pub_cfg->pub_max)
		return -EINVAL;
	if (ch_cfg[ch].grp >= grp_num)
		return -EINVAL;

	return 0;
}

static int hfc_pub_info_chk(struct rtw89_dev *rtwdev)
{
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	const struct rtw89_hfc_pub_cfg *cfg = &param->pub_cfg;
	struct rtw89_hfc_pub_info *info = &param->pub_info;

	if (info->g0_used + info->g1_used + info->pub_aval != cfg->pub_max) {
		if (rtwdev->chip->chip_id == RTL8852A)
			return 0;
		else
			return -EFAULT;
	}

	return 0;
}

static int hfc_pub_cfg_chk(struct rtw89_dev *rtwdev)
{
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	const struct rtw89_hfc_pub_cfg *pub_cfg = &param->pub_cfg;

	if (pub_cfg->grp0 + pub_cfg->grp1 != pub_cfg->pub_max)
		return -EFAULT;

	return 0;
}

static int hfc_ch_ctrl(struct rtw89_dev *rtwdev, u8 ch)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_page_regs *regs = chip->page_regs;
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	const struct rtw89_hfc_ch_cfg *cfg = param->ch_cfg;
	int ret = 0;
	u32 val = 0;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	ret = hfc_ch_cfg_chk(rtwdev, ch);
	if (ret)
		return ret;

	if (ch > RTW89_DMA_B1HI)
		return -EINVAL;

	val = u32_encode_bits(cfg[ch].min, B_AX_MIN_PG_MASK) |
	      u32_encode_bits(cfg[ch].max, B_AX_MAX_PG_MASK) |
	      (cfg[ch].grp ? B_AX_GRP : 0);
	rtw89_write32(rtwdev, regs->ach_page_ctrl + ch * 4, val);

	return 0;
}

static int hfc_upd_ch_info(struct rtw89_dev *rtwdev, u8 ch)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_page_regs *regs = chip->page_regs;
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	struct rtw89_hfc_ch_info *info = param->ch_info;
	const struct rtw89_hfc_ch_cfg *cfg = param->ch_cfg;
	u32 val;
	u32 ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	if (ch > RTW89_DMA_H2C)
		return -EINVAL;

	val = rtw89_read32(rtwdev, regs->ach_page_info + ch * 4);
	info[ch].aval = u32_get_bits(val, B_AX_AVAL_PG_MASK);
	if (ch < RTW89_DMA_H2C)
		info[ch].used = u32_get_bits(val, B_AX_USE_PG_MASK);
	else
		info[ch].used = cfg[ch].min - info[ch].aval;

	return 0;
}

static int hfc_pub_ctrl(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_page_regs *regs = chip->page_regs;
	const struct rtw89_hfc_pub_cfg *cfg = &rtwdev->mac.hfc_param.pub_cfg;
	u32 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	ret = hfc_pub_cfg_chk(rtwdev);
	if (ret)
		return ret;

	val = u32_encode_bits(cfg->grp0, B_AX_PUBPG_G0_MASK) |
	      u32_encode_bits(cfg->grp1, B_AX_PUBPG_G1_MASK);
	rtw89_write32(rtwdev, regs->pub_page_ctrl1, val);

	val = u32_encode_bits(cfg->wp_thrd, B_AX_WP_THRD_MASK);
	rtw89_write32(rtwdev, regs->wp_page_ctrl2, val);

	return 0;
}

static void hfc_get_mix_info_ax(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_page_regs *regs = chip->page_regs;
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	struct rtw89_hfc_pub_cfg *pub_cfg = &param->pub_cfg;
	struct rtw89_hfc_prec_cfg *prec_cfg = &param->prec_cfg;
	struct rtw89_hfc_pub_info *info = &param->pub_info;
	u32 val;

	val = rtw89_read32(rtwdev, regs->pub_page_info1);
	info->g0_used = u32_get_bits(val, B_AX_G0_USE_PG_MASK);
	info->g1_used = u32_get_bits(val, B_AX_G1_USE_PG_MASK);
	val = rtw89_read32(rtwdev, regs->pub_page_info3);
	info->g0_aval = u32_get_bits(val, B_AX_G0_AVAL_PG_MASK);
	info->g1_aval = u32_get_bits(val, B_AX_G1_AVAL_PG_MASK);
	info->pub_aval =
		u32_get_bits(rtw89_read32(rtwdev, regs->pub_page_info2),
			     B_AX_PUB_AVAL_PG_MASK);
	info->wp_aval =
		u32_get_bits(rtw89_read32(rtwdev, regs->wp_page_info1),
			     B_AX_WP_AVAL_PG_MASK);

	val = rtw89_read32(rtwdev, regs->hci_fc_ctrl);
	param->en = val & B_AX_HCI_FC_EN ? 1 : 0;
	param->h2c_en = val & B_AX_HCI_FC_CH12_EN ? 1 : 0;
	param->mode = u32_get_bits(val, B_AX_HCI_FC_MODE_MASK);
	prec_cfg->ch011_full_cond =
		u32_get_bits(val, B_AX_HCI_FC_WD_FULL_COND_MASK);
	prec_cfg->h2c_full_cond =
		u32_get_bits(val, B_AX_HCI_FC_CH12_FULL_COND_MASK);
	prec_cfg->wp_ch07_full_cond =
		u32_get_bits(val, B_AX_HCI_FC_WP_CH07_FULL_COND_MASK);
	prec_cfg->wp_ch811_full_cond =
		u32_get_bits(val, B_AX_HCI_FC_WP_CH811_FULL_COND_MASK);

	val = rtw89_read32(rtwdev, regs->ch_page_ctrl);
	prec_cfg->ch011_prec = u32_get_bits(val, B_AX_PREC_PAGE_CH011_MASK);
	prec_cfg->h2c_prec = u32_get_bits(val, B_AX_PREC_PAGE_CH12_MASK);

	val = rtw89_read32(rtwdev, regs->pub_page_ctrl2);
	pub_cfg->pub_max = u32_get_bits(val, B_AX_PUBPG_ALL_MASK);

	val = rtw89_read32(rtwdev, regs->wp_page_ctrl1);
	prec_cfg->wp_ch07_prec = u32_get_bits(val, B_AX_PREC_PAGE_WP_CH07_MASK);
	prec_cfg->wp_ch811_prec = u32_get_bits(val, B_AX_PREC_PAGE_WP_CH811_MASK);

	val = rtw89_read32(rtwdev, regs->wp_page_ctrl2);
	pub_cfg->wp_thrd = u32_get_bits(val, B_AX_WP_THRD_MASK);

	val = rtw89_read32(rtwdev, regs->pub_page_ctrl1);
	pub_cfg->grp0 = u32_get_bits(val, B_AX_PUBPG_G0_MASK);
	pub_cfg->grp1 = u32_get_bits(val, B_AX_PUBPG_G1_MASK);
}

static int hfc_upd_mix_info(struct rtw89_dev *rtwdev)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	mac->hfc_get_mix_info(rtwdev);

	ret = hfc_pub_info_chk(rtwdev);
	if (param->en && ret)
		return ret;

	return 0;
}

static void hfc_h2c_cfg_ax(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_page_regs *regs = chip->page_regs;
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	const struct rtw89_hfc_prec_cfg *prec_cfg = &param->prec_cfg;
	u32 val;

	val = u32_encode_bits(prec_cfg->h2c_prec, B_AX_PREC_PAGE_CH12_MASK);
	rtw89_write32(rtwdev, regs->ch_page_ctrl, val);

	rtw89_write32_mask(rtwdev, regs->hci_fc_ctrl,
			   B_AX_HCI_FC_CH12_FULL_COND_MASK,
			   prec_cfg->h2c_full_cond);
}

static void hfc_mix_cfg_ax(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_page_regs *regs = chip->page_regs;
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	const struct rtw89_hfc_pub_cfg *pub_cfg = &param->pub_cfg;
	const struct rtw89_hfc_prec_cfg *prec_cfg = &param->prec_cfg;
	u32 val;

	val = u32_encode_bits(prec_cfg->ch011_prec, B_AX_PREC_PAGE_CH011_MASK) |
	      u32_encode_bits(prec_cfg->h2c_prec, B_AX_PREC_PAGE_CH12_MASK);
	rtw89_write32(rtwdev, regs->ch_page_ctrl, val);

	val = u32_encode_bits(pub_cfg->pub_max, B_AX_PUBPG_ALL_MASK);
	rtw89_write32(rtwdev, regs->pub_page_ctrl2, val);

	val = u32_encode_bits(prec_cfg->wp_ch07_prec,
			      B_AX_PREC_PAGE_WP_CH07_MASK) |
	      u32_encode_bits(prec_cfg->wp_ch811_prec,
			      B_AX_PREC_PAGE_WP_CH811_MASK);
	rtw89_write32(rtwdev, regs->wp_page_ctrl1, val);

	val = u32_replace_bits(rtw89_read32(rtwdev, regs->hci_fc_ctrl),
			       param->mode, B_AX_HCI_FC_MODE_MASK);
	val = u32_replace_bits(val, prec_cfg->ch011_full_cond,
			       B_AX_HCI_FC_WD_FULL_COND_MASK);
	val = u32_replace_bits(val, prec_cfg->h2c_full_cond,
			       B_AX_HCI_FC_CH12_FULL_COND_MASK);
	val = u32_replace_bits(val, prec_cfg->wp_ch07_full_cond,
			       B_AX_HCI_FC_WP_CH07_FULL_COND_MASK);
	val = u32_replace_bits(val, prec_cfg->wp_ch811_full_cond,
			       B_AX_HCI_FC_WP_CH811_FULL_COND_MASK);
	rtw89_write32(rtwdev, regs->hci_fc_ctrl, val);
}

static void hfc_func_en_ax(struct rtw89_dev *rtwdev, bool en, bool h2c_en)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_page_regs *regs = chip->page_regs;
	struct rtw89_hfc_param *param = &rtwdev->mac.hfc_param;
	u32 val;

	val = rtw89_read32(rtwdev, regs->hci_fc_ctrl);
	param->en = en;
	param->h2c_en = h2c_en;
	val = en ? (val | B_AX_HCI_FC_EN) : (val & ~B_AX_HCI_FC_EN);
	val = h2c_en ? (val | B_AX_HCI_FC_CH12_EN) :
			 (val & ~B_AX_HCI_FC_CH12_EN);
	rtw89_write32(rtwdev, regs->hci_fc_ctrl, val);
}

int rtw89_mac_hfc_init(struct rtw89_dev *rtwdev, bool reset, bool en, bool h2c_en)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u32 dma_ch_mask = chip->dma_ch_mask;
	u8 ch;
	u32 ret = 0;

	if (reset)
		ret = hfc_reset_param(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	mac->hfc_func_en(rtwdev, false, false);

	if (!en && h2c_en) {
		mac->hfc_h2c_cfg(rtwdev);
		mac->hfc_func_en(rtwdev, en, h2c_en);
		return ret;
	}

	for (ch = RTW89_DMA_ACH0; ch < RTW89_DMA_H2C; ch++) {
		if (dma_ch_mask & BIT(ch))
			continue;
		ret = hfc_ch_ctrl(rtwdev, ch);
		if (ret)
			return ret;
	}

	ret = hfc_pub_ctrl(rtwdev);
	if (ret)
		return ret;

	mac->hfc_mix_cfg(rtwdev);
	if (en || h2c_en) {
		mac->hfc_func_en(rtwdev, en, h2c_en);
		udelay(10);
	}
	for (ch = RTW89_DMA_ACH0; ch < RTW89_DMA_H2C; ch++) {
		if (dma_ch_mask & BIT(ch))
			continue;
		ret = hfc_upd_ch_info(rtwdev, ch);
		if (ret)
			return ret;
	}
	ret = hfc_upd_mix_info(rtwdev);

	return ret;
}

#define PWR_POLL_CNT	2000
static int pwr_cmd_poll(struct rtw89_dev *rtwdev,
			const struct rtw89_pwr_cfg *cfg)
{
	u8 val = 0;
	int ret;
	u32 addr = cfg->base == PWR_INTF_MSK_SDIO ?
		   cfg->addr | SDIO_LOCAL_BASE_ADDR : cfg->addr;

	ret = read_poll_timeout(rtw89_read8, val, !((val ^ cfg->val) & cfg->msk),
				1000, 1000 * PWR_POLL_CNT, false, rtwdev, addr);

	if (!ret)
		return 0;

	rtw89_warn(rtwdev, "[ERR] Polling timeout\n");
	rtw89_warn(rtwdev, "[ERR] addr: %X, %X\n", addr, cfg->addr);
	rtw89_warn(rtwdev, "[ERR] val: %X, %X\n", val, cfg->val);

	return -EBUSY;
}

static int rtw89_mac_sub_pwr_seq(struct rtw89_dev *rtwdev, u8 cv_msk,
				 u8 intf_msk, const struct rtw89_pwr_cfg *cfg)
{
	const struct rtw89_pwr_cfg *cur_cfg;
	u32 addr;
	u8 val;

	for (cur_cfg = cfg; cur_cfg->cmd != PWR_CMD_END; cur_cfg++) {
		if (!(cur_cfg->intf_msk & intf_msk) ||
		    !(cur_cfg->cv_msk & cv_msk))
			continue;

		switch (cur_cfg->cmd) {
		case PWR_CMD_WRITE:
			addr = cur_cfg->addr;

			if (cur_cfg->base == PWR_BASE_SDIO)
				addr |= SDIO_LOCAL_BASE_ADDR;

			val = rtw89_read8(rtwdev, addr);
			val &= ~(cur_cfg->msk);
			val |= (cur_cfg->val & cur_cfg->msk);

			rtw89_write8(rtwdev, addr, val);
			break;
		case PWR_CMD_POLL:
			if (pwr_cmd_poll(rtwdev, cur_cfg))
				return -EBUSY;
			break;
		case PWR_CMD_DELAY:
			if (cur_cfg->val == PWR_DELAY_US)
				udelay(cur_cfg->addr);
			else
				fsleep(cur_cfg->addr * 1000);
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int rtw89_mac_pwr_seq(struct rtw89_dev *rtwdev,
			     const struct rtw89_pwr_cfg * const *cfg_seq)
{
	int ret;

	for (; *cfg_seq; cfg_seq++) {
		ret = rtw89_mac_sub_pwr_seq(rtwdev, BIT(rtwdev->hal.cv),
					    PWR_INTF_MSK_PCIE, *cfg_seq);
		if (ret)
			return -EBUSY;
	}

	return 0;
}

static enum rtw89_rpwm_req_pwr_state
rtw89_mac_get_req_pwr_state(struct rtw89_dev *rtwdev)
{
	enum rtw89_rpwm_req_pwr_state state;

	switch (rtwdev->ps_mode) {
	case RTW89_PS_MODE_RFOFF:
		state = RTW89_MAC_RPWM_REQ_PWR_STATE_BAND0_RFOFF;
		break;
	case RTW89_PS_MODE_CLK_GATED:
		state = RTW89_MAC_RPWM_REQ_PWR_STATE_CLK_GATED;
		break;
	case RTW89_PS_MODE_PWR_GATED:
		state = RTW89_MAC_RPWM_REQ_PWR_STATE_PWR_GATED;
		break;
	default:
		state = RTW89_MAC_RPWM_REQ_PWR_STATE_ACTIVE;
		break;
	}
	return state;
}

static void rtw89_mac_send_rpwm(struct rtw89_dev *rtwdev,
				enum rtw89_rpwm_req_pwr_state req_pwr_state,
				bool notify_wake)
{
	u16 request;

	spin_lock_bh(&rtwdev->rpwm_lock);

	request = rtw89_read16(rtwdev, R_AX_RPWM);
	request ^= request | PS_RPWM_TOGGLE;
	request |= req_pwr_state;

	if (notify_wake) {
		request |= PS_RPWM_NOTIFY_WAKE;
	} else {
		rtwdev->mac.rpwm_seq_num = (rtwdev->mac.rpwm_seq_num + 1) &
					    RPWM_SEQ_NUM_MAX;
		request |= FIELD_PREP(PS_RPWM_SEQ_NUM,
				      rtwdev->mac.rpwm_seq_num);

		if (req_pwr_state < RTW89_MAC_RPWM_REQ_PWR_STATE_CLK_GATED)
			request |= PS_RPWM_ACK;
	}
	rtw89_write16(rtwdev, rtwdev->hci.rpwm_addr, request);

	spin_unlock_bh(&rtwdev->rpwm_lock);
}

static int rtw89_mac_check_cpwm_state(struct rtw89_dev *rtwdev,
				      enum rtw89_rpwm_req_pwr_state req_pwr_state)
{
	bool request_deep_mode;
	bool in_deep_mode;
	u8 rpwm_req_num;
	u8 cpwm_rsp_seq;
	u8 cpwm_seq;
	u8 cpwm_status;

	if (req_pwr_state >= RTW89_MAC_RPWM_REQ_PWR_STATE_CLK_GATED)
		request_deep_mode = true;
	else
		request_deep_mode = false;

	if (rtw89_read32_mask(rtwdev, R_AX_LDM, B_AX_EN_32K))
		in_deep_mode = true;
	else
		in_deep_mode = false;

	if (request_deep_mode != in_deep_mode)
		return -EPERM;

	if (request_deep_mode)
		return 0;

	rpwm_req_num = rtwdev->mac.rpwm_seq_num;
	cpwm_rsp_seq = rtw89_read16_mask(rtwdev, rtwdev->hci.cpwm_addr,
					 PS_CPWM_RSP_SEQ_NUM);

	if (rpwm_req_num != cpwm_rsp_seq)
		return -EPERM;

	rtwdev->mac.cpwm_seq_num = (rtwdev->mac.cpwm_seq_num + 1) &
				    CPWM_SEQ_NUM_MAX;

	cpwm_seq = rtw89_read16_mask(rtwdev, rtwdev->hci.cpwm_addr, PS_CPWM_SEQ_NUM);
	if (cpwm_seq != rtwdev->mac.cpwm_seq_num)
		return -EPERM;

	cpwm_status = rtw89_read16_mask(rtwdev, rtwdev->hci.cpwm_addr, PS_CPWM_STATE);
	if (cpwm_status != req_pwr_state)
		return -EPERM;

	return 0;
}

void rtw89_mac_power_mode_change(struct rtw89_dev *rtwdev, bool enter)
{
	enum rtw89_rpwm_req_pwr_state state;
	unsigned long delay = enter ? 10 : 150;
	int ret;
	int i;

	if (enter)
		state = rtw89_mac_get_req_pwr_state(rtwdev);
	else
		state = RTW89_MAC_RPWM_REQ_PWR_STATE_ACTIVE;

	for (i = 0; i < RPWM_TRY_CNT; i++) {
		rtw89_mac_send_rpwm(rtwdev, state, false);
		ret = read_poll_timeout_atomic(rtw89_mac_check_cpwm_state, ret,
					       !ret, delay, 15000, false,
					       rtwdev, state);
		if (!ret)
			break;

		if (i == RPWM_TRY_CNT - 1)
			rtw89_err(rtwdev, "firmware failed to ack for %s ps mode\n",
				  enter ? "entering" : "leaving");
		else
			rtw89_debug(rtwdev, RTW89_DBG_UNEXP,
				    "%d time firmware failed to ack for %s ps mode\n",
				    i + 1, enter ? "entering" : "leaving");
	}
}

void rtw89_mac_notify_wake(struct rtw89_dev *rtwdev)
{
	enum rtw89_rpwm_req_pwr_state state;

	state = rtw89_mac_get_req_pwr_state(rtwdev);
	rtw89_mac_send_rpwm(rtwdev, state, true);
}

static int rtw89_mac_power_switch(struct rtw89_dev *rtwdev, bool on)
{
#define PWR_ACT 1
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_pwr_cfg * const *cfg_seq;
	int (*cfg_func)(struct rtw89_dev *rtwdev);
	int ret;
	u8 val;

	if (on) {
		cfg_seq = chip->pwr_on_seq;
		cfg_func = chip->ops->pwr_on_func;
	} else {
		cfg_seq = chip->pwr_off_seq;
		cfg_func = chip->ops->pwr_off_func;
	}

	if (test_bit(RTW89_FLAG_FW_RDY, rtwdev->flags))
		__rtw89_leave_ps_mode(rtwdev);

	val = rtw89_read32_mask(rtwdev, R_AX_IC_PWR_STATE, B_AX_WLMAC_PWR_STE_MASK);
	if (on && val == PWR_ACT) {
		rtw89_err(rtwdev, "MAC has already powered on\n");
		return -EBUSY;
	}

	ret = cfg_func ? cfg_func(rtwdev) : rtw89_mac_pwr_seq(rtwdev, cfg_seq);
	if (ret)
		return ret;

	if (on) {
		set_bit(RTW89_FLAG_POWERON, rtwdev->flags);
		set_bit(RTW89_FLAG_DMAC_FUNC, rtwdev->flags);
		set_bit(RTW89_FLAG_CMAC0_FUNC, rtwdev->flags);
		rtw89_write8(rtwdev, R_AX_SCOREBOARD + 3, MAC_AX_NOTIFY_TP_MAJOR);
	} else {
		clear_bit(RTW89_FLAG_POWERON, rtwdev->flags);
		clear_bit(RTW89_FLAG_DMAC_FUNC, rtwdev->flags);
		clear_bit(RTW89_FLAG_CMAC0_FUNC, rtwdev->flags);
		clear_bit(RTW89_FLAG_CMAC1_FUNC, rtwdev->flags);
		clear_bit(RTW89_FLAG_FW_RDY, rtwdev->flags);
		rtw89_write8(rtwdev, R_AX_SCOREBOARD + 3, MAC_AX_NOTIFY_PWR_MAJOR);
		rtw89_set_entity_state(rtwdev, false);
	}

	return 0;
#undef PWR_ACT
}

void rtw89_mac_pwr_off(struct rtw89_dev *rtwdev)
{
	rtw89_mac_power_switch(rtwdev, false);
}

static int cmac_func_en_ax(struct rtw89_dev *rtwdev, u8 mac_idx, bool en)
{
	u32 func_en = 0;
	u32 ck_en = 0;
	u32 c1pc_en = 0;
	u32 addrl_func_en[] = {R_AX_CMAC_FUNC_EN, R_AX_CMAC_FUNC_EN_C1};
	u32 addrl_ck_en[] = {R_AX_CK_EN, R_AX_CK_EN_C1};

	func_en = B_AX_CMAC_EN | B_AX_CMAC_TXEN | B_AX_CMAC_RXEN |
			B_AX_PHYINTF_EN | B_AX_CMAC_DMA_EN | B_AX_PTCLTOP_EN |
			B_AX_SCHEDULER_EN | B_AX_TMAC_EN | B_AX_RMAC_EN |
			B_AX_CMAC_CRPRT;
	ck_en = B_AX_CMAC_CKEN | B_AX_PHYINTF_CKEN | B_AX_CMAC_DMA_CKEN |
		      B_AX_PTCLTOP_CKEN | B_AX_SCHEDULER_CKEN | B_AX_TMAC_CKEN |
		      B_AX_RMAC_CKEN;
	c1pc_en = B_AX_R_SYM_WLCMAC1_PC_EN |
			B_AX_R_SYM_WLCMAC1_P1_PC_EN |
			B_AX_R_SYM_WLCMAC1_P2_PC_EN |
			B_AX_R_SYM_WLCMAC1_P3_PC_EN |
			B_AX_R_SYM_WLCMAC1_P4_PC_EN;

	if (en) {
		if (mac_idx == RTW89_MAC_1) {
			rtw89_write32_set(rtwdev, R_AX_AFE_CTRL1, c1pc_en);
			rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND,
					  B_AX_R_SYM_ISO_CMAC12PP);
			rtw89_write32_set(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND,
					  B_AX_CMAC1_FEN);
		}
		rtw89_write32_set(rtwdev, addrl_ck_en[mac_idx], ck_en);
		rtw89_write32_set(rtwdev, addrl_func_en[mac_idx], func_en);
	} else {
		rtw89_write32_clr(rtwdev, addrl_func_en[mac_idx], func_en);
		rtw89_write32_clr(rtwdev, addrl_ck_en[mac_idx], ck_en);
		if (mac_idx == RTW89_MAC_1) {
			rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND,
					  B_AX_CMAC1_FEN);
			rtw89_write32_set(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND,
					  B_AX_R_SYM_ISO_CMAC12PP);
			rtw89_write32_clr(rtwdev, R_AX_AFE_CTRL1, c1pc_en);
		}
	}

	return 0;
}

static int dmac_func_en_ax(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u32 val32;

	if (chip_id == RTL8852C)
		val32 = (B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN |
			 B_AX_MAC_SEC_EN | B_AX_DISPATCHER_EN |
			 B_AX_DLE_CPUIO_EN | B_AX_PKT_IN_EN |
			 B_AX_DMAC_TBL_EN | B_AX_PKT_BUF_EN |
			 B_AX_STA_SCH_EN | B_AX_TXPKT_CTRL_EN |
			 B_AX_WD_RLS_EN | B_AX_MPDU_PROC_EN |
			 B_AX_DMAC_CRPRT | B_AX_H_AXIDMA_EN);
	else
		val32 = (B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN |
			 B_AX_MAC_SEC_EN | B_AX_DISPATCHER_EN |
			 B_AX_DLE_CPUIO_EN | B_AX_PKT_IN_EN |
			 B_AX_DMAC_TBL_EN | B_AX_PKT_BUF_EN |
			 B_AX_STA_SCH_EN | B_AX_TXPKT_CTRL_EN |
			 B_AX_WD_RLS_EN | B_AX_MPDU_PROC_EN |
			 B_AX_DMAC_CRPRT);
	rtw89_write32(rtwdev, R_AX_DMAC_FUNC_EN, val32);

	val32 = (B_AX_MAC_SEC_CLK_EN | B_AX_DISPATCHER_CLK_EN |
		 B_AX_DLE_CPUIO_CLK_EN | B_AX_PKT_IN_CLK_EN |
		 B_AX_STA_SCH_CLK_EN | B_AX_TXPKT_CTRL_CLK_EN |
		 B_AX_WD_RLS_CLK_EN | B_AX_BBRPT_CLK_EN);
	if (chip_id == RTL8852BT)
		val32 |= B_AX_AXIDMA_CLK_EN;
	rtw89_write32(rtwdev, R_AX_DMAC_CLK_EN, val32);

	return 0;
}

static int chip_func_en_ax(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;

	if (chip_id == RTL8852A || rtw89_is_rtl885xb(rtwdev))
		rtw89_write32_set(rtwdev, R_AX_SPS_DIG_ON_CTRL0,
				  B_AX_OCP_L1_MASK);

	return 0;
}

static int sys_init_ax(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = dmac_func_en_ax(rtwdev);
	if (ret)
		return ret;

	ret = cmac_func_en_ax(rtwdev, 0, true);
	if (ret)
		return ret;

	ret = chip_func_en_ax(rtwdev);
	if (ret)
		return ret;

	return ret;
}

const struct rtw89_mac_size_set rtw89_mac_size = {
	.hfc_preccfg_pcie = {2, 40, 0, 0, 1, 0, 0, 0},
	.hfc_prec_cfg_c0 = {2, 32, 0, 0, 0, 0, 0, 0},
	.hfc_prec_cfg_c2 = {0, 256, 0, 0, 0, 0, 0, 0},
	/* PCIE 64 */
	.wde_size0 = {RTW89_WDE_PG_64, 4095, 1,},
	.wde_size0_v1 = {RTW89_WDE_PG_64, 3328, 0, 0,},
	/* DLFW */
	.wde_size4 = {RTW89_WDE_PG_64, 0, 4096,},
	.wde_size4_v1 = {RTW89_WDE_PG_64, 0, 3328, 0,},
	/* PCIE 64 */
	.wde_size6 = {RTW89_WDE_PG_64, 512, 0,},
	/* 8852B PCIE SCC */
	.wde_size7 = {RTW89_WDE_PG_64, 510, 2,},
	/* DLFW */
	.wde_size9 = {RTW89_WDE_PG_64, 0, 1024,},
	/* 8852C DLFW */
	.wde_size18 = {RTW89_WDE_PG_64, 0, 2048,},
	/* 8852C PCIE SCC */
	.wde_size19 = {RTW89_WDE_PG_64, 3328, 0,},
	/* PCIE */
	.ple_size0 = {RTW89_PLE_PG_128, 1520, 16,},
	.ple_size0_v1 = {RTW89_PLE_PG_128, 2688, 240, 212992,},
	.ple_size3_v1 = {RTW89_PLE_PG_128, 2928, 0, 212992,},
	/* DLFW */
	.ple_size4 = {RTW89_PLE_PG_128, 64, 1472,},
	/* PCIE 64 */
	.ple_size6 = {RTW89_PLE_PG_128, 496, 16,},
	/* DLFW */
	.ple_size8 = {RTW89_PLE_PG_128, 64, 960,},
	/* 8852C DLFW */
	.ple_size18 = {RTW89_PLE_PG_128, 2544, 16,},
	/* 8852C PCIE SCC */
	.ple_size19 = {RTW89_PLE_PG_128, 1904, 16,},
	/* PCIE 64 */
	.wde_qt0 = {3792, 196, 0, 107,},
	.wde_qt0_v1 = {3302, 6, 0, 20,},
	/* DLFW */
	.wde_qt4 = {0, 0, 0, 0,},
	/* PCIE 64 */
	.wde_qt6 = {448, 48, 0, 16,},
	/* 8852B PCIE SCC */
	.wde_qt7 = {446, 48, 0, 16,},
	/* 8852C DLFW */
	.wde_qt17 = {0, 0, 0,  0,},
	/* 8852C PCIE SCC */
	.wde_qt18 = {3228, 60, 0, 40,},
	.ple_qt0 = {320, 320, 32, 16, 13, 13, 292, 292, 64, 18, 1, 4, 0,},
	.ple_qt1 = {320, 320, 32, 16, 1316, 1316, 1595, 1595, 1367, 1321, 1, 1307, 0,},
	/* PCIE SCC */
	.ple_qt4 = {264, 0, 16, 20, 26, 13, 356, 0, 32, 40, 8,},
	/* PCIE SCC */
	.ple_qt5 = {264, 0, 32, 20, 64, 13, 1101, 0, 64, 128, 120,},
	.ple_qt9 = {0, 0, 32, 256, 0, 0, 0, 0, 0, 0, 1, 0, 0,},
	/* DLFW */
	.ple_qt13 = {0, 0, 16, 48, 0, 0, 0, 0, 0, 0, 0,},
	/* PCIE 64 */
	.ple_qt18 = {147, 0, 16, 20, 17, 13, 89, 0, 32, 14, 8, 0,},
	/* DLFW 52C */
	.ple_qt44 = {0, 0, 16, 256, 0, 0, 0, 0, 0, 0, 0, 0,},
	/* DLFW 52C */
	.ple_qt45 = {0, 0, 32, 256, 0, 0, 0, 0, 0, 0, 0, 0,},
	/* 8852C PCIE SCC */
	.ple_qt46 = {525, 0, 16, 20, 13, 13, 178, 0, 32, 62, 8, 16,},
	/* 8852C PCIE SCC */
	.ple_qt47 = {525, 0, 32, 20, 1034, 13, 1199, 0, 1053, 62, 160, 1037,},
	/* PCIE 64 */
	.ple_qt58 = {147, 0, 16, 20, 157, 13, 229, 0, 172, 14, 24, 0,},
	/* 8852A PCIE WOW */
	.ple_qt_52a_wow = {264, 0, 32, 20, 64, 13, 1005, 0, 64, 128, 120,},
	/* 8852B PCIE WOW */
	.ple_qt_52b_wow = {147, 0, 16, 20, 157, 13, 133, 0, 172, 14, 24, 0,},
	/* 8851B PCIE WOW */
	.ple_qt_51b_wow = {147, 0, 16, 20, 157, 13, 133, 0, 172, 14, 24, 0,},
	.ple_rsvd_qt0 = {2, 107, 107, 6, 6, 6, 6, 0, 0, 0,},
	.ple_rsvd_qt1 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,},
	.rsvd0_size0 = {212992, 0,},
	.rsvd1_size0 = {587776, 2048,},
};
EXPORT_SYMBOL(rtw89_mac_size);

static const struct rtw89_dle_mem *get_dle_mem_cfg(struct rtw89_dev *rtwdev,
						   enum rtw89_qta_mode mode)
{
	struct rtw89_mac_info *mac = &rtwdev->mac;
	const struct rtw89_dle_mem *cfg;

	cfg = &rtwdev->chip->dle_mem[mode];
	if (!cfg)
		return NULL;

	if (cfg->mode != mode) {
		rtw89_warn(rtwdev, "qta mode unmatch!\n");
		return NULL;
	}

	mac->dle_info.rsvd_qt = cfg->rsvd_qt;
	mac->dle_info.ple_pg_size = cfg->ple_size->pge_size;
	mac->dle_info.ple_free_pg = cfg->ple_size->lnk_pge_num;
	mac->dle_info.qta_mode = mode;
	mac->dle_info.c0_rx_qta = cfg->ple_min_qt->cma0_dma;
	mac->dle_info.c1_rx_qta = cfg->ple_min_qt->cma1_dma;

	return cfg;
}

int rtw89_mac_get_dle_rsvd_qt_cfg(struct rtw89_dev *rtwdev,
				  enum rtw89_mac_dle_rsvd_qt_type type,
				  struct rtw89_mac_dle_rsvd_qt_cfg *cfg)
{
	struct rtw89_dle_info *dle_info = &rtwdev->mac.dle_info;
	const struct rtw89_rsvd_quota *rsvd_qt = dle_info->rsvd_qt;

	switch (type) {
	case DLE_RSVD_QT_MPDU_INFO:
		cfg->pktid = dle_info->ple_free_pg;
		cfg->pg_num = rsvd_qt->mpdu_info_tbl;
		break;
	case DLE_RSVD_QT_B0_CSI:
		cfg->pktid = dle_info->ple_free_pg + rsvd_qt->mpdu_info_tbl;
		cfg->pg_num = rsvd_qt->b0_csi;
		break;
	case DLE_RSVD_QT_B1_CSI:
		cfg->pktid = dle_info->ple_free_pg +
			     rsvd_qt->mpdu_info_tbl + rsvd_qt->b0_csi;
		cfg->pg_num = rsvd_qt->b1_csi;
		break;
	case DLE_RSVD_QT_B0_LMR:
		cfg->pktid = dle_info->ple_free_pg +
			     rsvd_qt->mpdu_info_tbl + rsvd_qt->b0_csi + rsvd_qt->b1_csi;
		cfg->pg_num = rsvd_qt->b0_lmr;
		break;
	case DLE_RSVD_QT_B1_LMR:
		cfg->pktid = dle_info->ple_free_pg +
			     rsvd_qt->mpdu_info_tbl + rsvd_qt->b0_csi + rsvd_qt->b1_csi +
			     rsvd_qt->b0_lmr;
		cfg->pg_num = rsvd_qt->b1_lmr;
		break;
	case DLE_RSVD_QT_B0_FTM:
		cfg->pktid = dle_info->ple_free_pg +
			     rsvd_qt->mpdu_info_tbl + rsvd_qt->b0_csi + rsvd_qt->b1_csi +
			     rsvd_qt->b0_lmr + rsvd_qt->b1_lmr;
		cfg->pg_num = rsvd_qt->b0_ftm;
		break;
	case DLE_RSVD_QT_B1_FTM:
		cfg->pktid = dle_info->ple_free_pg +
			     rsvd_qt->mpdu_info_tbl + rsvd_qt->b0_csi + rsvd_qt->b1_csi +
			     rsvd_qt->b0_lmr + rsvd_qt->b1_lmr + rsvd_qt->b0_ftm;
		cfg->pg_num = rsvd_qt->b1_ftm;
		break;
	default:
		return -EINVAL;
	}

	cfg->size = (u32)cfg->pg_num * dle_info->ple_pg_size;

	return 0;
}

static bool mac_is_txq_empty_ax(struct rtw89_dev *rtwdev)
{
	struct rtw89_mac_dle_dfi_qempty qempty;
	u32 grpnum, qtmp, val32, msk32;
	int i, j, ret;

	grpnum = rtwdev->chip->wde_qempty_acq_grpnum;
	qempty.dle_type = DLE_CTRL_TYPE_WDE;

	for (i = 0; i < grpnum; i++) {
		qempty.grpsel = i;
		ret = rtw89_mac_dle_dfi_qempty_cfg(rtwdev, &qempty);
		if (ret) {
			rtw89_warn(rtwdev, "dle dfi acq empty %d\n", ret);
			return false;
		}
		qtmp = qempty.qempty;
		for (j = 0 ; j < QEMP_ACQ_GRP_MACID_NUM; j++) {
			val32 = u32_get_bits(qtmp, QEMP_ACQ_GRP_QSEL_MASK);
			if (val32 != QEMP_ACQ_GRP_QSEL_MASK)
				return false;
			qtmp >>= QEMP_ACQ_GRP_QSEL_SH;
		}
	}

	qempty.grpsel = rtwdev->chip->wde_qempty_mgq_grpsel;
	ret = rtw89_mac_dle_dfi_qempty_cfg(rtwdev, &qempty);
	if (ret) {
		rtw89_warn(rtwdev, "dle dfi mgq empty %d\n", ret);
		return false;
	}
	msk32 = B_CMAC0_MGQ_NORMAL | B_CMAC0_MGQ_NO_PWRSAV | B_CMAC0_CPUMGQ;
	if ((qempty.qempty & msk32) != msk32)
		return false;

	if (rtwdev->dbcc_en) {
		msk32 |= B_CMAC1_MGQ_NORMAL | B_CMAC1_MGQ_NO_PWRSAV | B_CMAC1_CPUMGQ;
		if ((qempty.qempty & msk32) != msk32)
			return false;
	}

	msk32 = B_AX_WDE_EMPTY_QTA_DMAC_WLAN_CPU | B_AX_WDE_EMPTY_QTA_DMAC_DATA_CPU |
		B_AX_PLE_EMPTY_QTA_DMAC_WLAN_CPU | B_AX_PLE_EMPTY_QTA_DMAC_H2C |
		B_AX_WDE_EMPTY_QUE_OTHERS | B_AX_PLE_EMPTY_QUE_DMAC_MPDU_TX |
		B_AX_WDE_EMPTY_QTA_DMAC_CPUIO | B_AX_PLE_EMPTY_QTA_DMAC_CPUIO |
		B_AX_WDE_EMPTY_QUE_DMAC_PKTIN | B_AX_WDE_EMPTY_QTA_DMAC_HIF |
		B_AX_PLE_EMPTY_QUE_DMAC_SEC_TX | B_AX_WDE_EMPTY_QTA_DMAC_PKTIN |
		B_AX_PLE_EMPTY_QTA_DMAC_B0_TXPL | B_AX_PLE_EMPTY_QTA_DMAC_B1_TXPL |
		B_AX_PLE_EMPTY_QTA_DMAC_MPDU_TX;
	val32 = rtw89_read32(rtwdev, R_AX_DLE_EMPTY0);

	return (val32 & msk32) == msk32;
}

static inline u32 dle_used_size(const struct rtw89_dle_mem *cfg)
{
	const struct rtw89_dle_size *wde = cfg->wde_size;
	const struct rtw89_dle_size *ple = cfg->ple_size;
	u32 used;

	used = wde->pge_size * (wde->lnk_pge_num + wde->unlnk_pge_num) +
	       ple->pge_size * (ple->lnk_pge_num + ple->unlnk_pge_num);

	if (cfg->rsvd0_size && cfg->rsvd1_size) {
		used += cfg->rsvd0_size->size;
		used += cfg->rsvd1_size->size;
	}

	return used;
}

static u32 dle_expected_used_size(struct rtw89_dev *rtwdev,
				  enum rtw89_qta_mode mode)
{
	u32 size = rtwdev->chip->fifo_size;

	if (mode == RTW89_QTA_SCC)
		size -= rtwdev->chip->dle_scc_rsvd_size;

	return size;
}

static void dle_func_en_ax(struct rtw89_dev *rtwdev, bool enable)
{
	if (enable)
		rtw89_write32_set(rtwdev, R_AX_DMAC_FUNC_EN,
				  B_AX_DLE_WDE_EN | B_AX_DLE_PLE_EN);
	else
		rtw89_write32_clr(rtwdev, R_AX_DMAC_FUNC_EN,
				  B_AX_DLE_WDE_EN | B_AX_DLE_PLE_EN);
}

static void dle_clk_en_ax(struct rtw89_dev *rtwdev, bool enable)
{
	u32 val = B_AX_DLE_WDE_CLK_EN | B_AX_DLE_PLE_CLK_EN;

	if (enable) {
		if (rtwdev->chip->chip_id == RTL8851B)
			val |= B_AX_AXIDMA_CLK_EN;
		rtw89_write32_set(rtwdev, R_AX_DMAC_CLK_EN, val);
	} else {
		rtw89_write32_clr(rtwdev, R_AX_DMAC_CLK_EN, val);
	}
}

static int dle_mix_cfg_ax(struct rtw89_dev *rtwdev, const struct rtw89_dle_mem *cfg)
{
	const struct rtw89_dle_size *size_cfg;
	u32 val;
	u8 bound = 0;

	val = rtw89_read32(rtwdev, R_AX_WDE_PKTBUF_CFG);
	size_cfg = cfg->wde_size;

	switch (size_cfg->pge_size) {
	default:
	case RTW89_WDE_PG_64:
		val = u32_replace_bits(val, S_AX_WDE_PAGE_SEL_64,
				       B_AX_WDE_PAGE_SEL_MASK);
		break;
	case RTW89_WDE_PG_128:
		val = u32_replace_bits(val, S_AX_WDE_PAGE_SEL_128,
				       B_AX_WDE_PAGE_SEL_MASK);
		break;
	case RTW89_WDE_PG_256:
		rtw89_err(rtwdev, "[ERR]WDE DLE doesn't support 256 byte!\n");
		return -EINVAL;
	}

	val = u32_replace_bits(val, bound, B_AX_WDE_START_BOUND_MASK);
	val = u32_replace_bits(val, size_cfg->lnk_pge_num,
			       B_AX_WDE_FREE_PAGE_NUM_MASK);
	rtw89_write32(rtwdev, R_AX_WDE_PKTBUF_CFG, val);

	val = rtw89_read32(rtwdev, R_AX_PLE_PKTBUF_CFG);
	bound = (size_cfg->lnk_pge_num + size_cfg->unlnk_pge_num)
				* size_cfg->pge_size / DLE_BOUND_UNIT;
	size_cfg = cfg->ple_size;

	switch (size_cfg->pge_size) {
	default:
	case RTW89_PLE_PG_64:
		rtw89_err(rtwdev, "[ERR]PLE DLE doesn't support 64 byte!\n");
		return -EINVAL;
	case RTW89_PLE_PG_128:
		val = u32_replace_bits(val, S_AX_PLE_PAGE_SEL_128,
				       B_AX_PLE_PAGE_SEL_MASK);
		break;
	case RTW89_PLE_PG_256:
		val = u32_replace_bits(val, S_AX_PLE_PAGE_SEL_256,
				       B_AX_PLE_PAGE_SEL_MASK);
		break;
	}

	val = u32_replace_bits(val, bound, B_AX_PLE_START_BOUND_MASK);
	val = u32_replace_bits(val, size_cfg->lnk_pge_num,
			       B_AX_PLE_FREE_PAGE_NUM_MASK);
	rtw89_write32(rtwdev, R_AX_PLE_PKTBUF_CFG, val);

	return 0;
}

static int chk_dle_rdy_ax(struct rtw89_dev *rtwdev, bool wde_or_ple)
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
		val = u32_encode_bits(_min_x, B_AX_ ## _module ## _MIN_SIZE_MASK) | \
		      u32_encode_bits(_max_x, B_AX_ ## _module ## _MAX_SIZE_MASK);  \
		rtw89_write32(rtwdev,					\
			      R_AX_ ## _module ## _QTA ## _idx ## _CFG,	\
			      val);					\
	} while (0)
#define SET_QUOTA(_x, _module, _idx)					\
	SET_QUOTA_VAL(min_cfg->_x, max_cfg->_x, _module, _idx)

static void wde_quota_cfg_ax(struct rtw89_dev *rtwdev,
			     const struct rtw89_wde_quota *min_cfg,
			     const struct rtw89_wde_quota *max_cfg,
			     u16 ext_wde_min_qt_wcpu)
{
	u16 min_qt_wcpu = ext_wde_min_qt_wcpu != INVALID_QT_WCPU ?
			  ext_wde_min_qt_wcpu : min_cfg->wcpu;
	u32 val;

	SET_QUOTA(hif, WDE, 0);
	SET_QUOTA_VAL(min_qt_wcpu, max_cfg->wcpu, WDE, 1);
	SET_QUOTA(pkt_in, WDE, 3);
	SET_QUOTA(cpu_io, WDE, 4);
}

static void ple_quota_cfg_ax(struct rtw89_dev *rtwdev,
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
	if (rtwdev->chip->chip_id == RTL8852C)
		SET_QUOTA(tx_rpt, PLE, 11);
}

int rtw89_mac_resize_ple_rx_quota(struct rtw89_dev *rtwdev, bool wow)
{
	const struct rtw89_ple_quota *min_cfg, *max_cfg;
	const struct rtw89_dle_mem *cfg;
	u32 val;

	if (rtwdev->chip->chip_id == RTL8852C)
		return 0;

	if (rtwdev->mac.qta_mode != RTW89_QTA_SCC) {
		rtw89_err(rtwdev, "[ERR]support SCC mode only\n");
		return -EINVAL;
	}

	if (wow)
		cfg = get_dle_mem_cfg(rtwdev, RTW89_QTA_WOW);
	else
		cfg = get_dle_mem_cfg(rtwdev, RTW89_QTA_SCC);
	if (!cfg) {
		rtw89_err(rtwdev, "[ERR]get_dle_mem_cfg\n");
		return -EINVAL;
	}

	min_cfg = cfg->ple_min_qt;
	max_cfg = cfg->ple_max_qt;
	SET_QUOTA(cma0_dma, PLE, 6);
	SET_QUOTA(cma1_dma, PLE, 7);

	return 0;
}
#undef SET_QUOTA

void rtw89_mac_hw_mgnt_sec(struct rtw89_dev *rtwdev, bool enable)
{
	u32 msk32 = B_AX_UC_MGNT_DEC | B_AX_BMC_MGNT_DEC;

	if (rtwdev->chip->chip_gen != RTW89_CHIP_AX)
		return;

	if (enable)
		rtw89_write32_set(rtwdev, R_AX_SEC_ENG_CTRL, msk32);
	else
		rtw89_write32_clr(rtwdev, R_AX_SEC_ENG_CTRL, msk32);
}

static void dle_quota_cfg(struct rtw89_dev *rtwdev,
			  const struct rtw89_dle_mem *cfg,
			  u16 ext_wde_min_qt_wcpu)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;

	mac->wde_quota_cfg(rtwdev, cfg->wde_min_qt, cfg->wde_max_qt, ext_wde_min_qt_wcpu);
	mac->ple_quota_cfg(rtwdev, cfg->ple_min_qt, cfg->ple_max_qt);
}

int rtw89_mac_dle_init(struct rtw89_dev *rtwdev, enum rtw89_qta_mode mode,
		       enum rtw89_qta_mode ext_mode)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_dle_mem *cfg, *ext_cfg;
	u16 ext_wde_min_qt_wcpu = INVALID_QT_WCPU;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	cfg = get_dle_mem_cfg(rtwdev, mode);
	if (!cfg) {
		rtw89_err(rtwdev, "[ERR]get_dle_mem_cfg\n");
		ret = -EINVAL;
		goto error;
	}

	if (mode == RTW89_QTA_DLFW) {
		ext_cfg = get_dle_mem_cfg(rtwdev, ext_mode);
		if (!ext_cfg) {
			rtw89_err(rtwdev, "[ERR]get_dle_ext_mem_cfg %d\n",
				  ext_mode);
			ret = -EINVAL;
			goto error;
		}
		ext_wde_min_qt_wcpu = ext_cfg->wde_min_qt->wcpu;
	}

	if (dle_used_size(cfg) != dle_expected_used_size(rtwdev, mode)) {
		rtw89_err(rtwdev, "[ERR]wd/dle mem cfg\n");
		ret = -EINVAL;
		goto error;
	}

	mac->dle_func_en(rtwdev, false);
	mac->dle_clk_en(rtwdev, true);

	ret = mac->dle_mix_cfg(rtwdev, cfg);
	if (ret) {
		rtw89_err(rtwdev, "[ERR] dle mix cfg\n");
		goto error;
	}
	dle_quota_cfg(rtwdev, cfg, ext_wde_min_qt_wcpu);

	mac->dle_func_en(rtwdev, true);

	ret = mac->chk_dle_rdy(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]WDE cfg ready\n");
		return ret;
	}

	ret = mac->chk_dle_rdy(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]PLE cfg ready\n");
		return ret;
	}

	return 0;
error:
	mac->dle_func_en(rtwdev, false);
	rtw89_err(rtwdev, "[ERR]trxcfg wde 0x8900 = %x\n",
		  rtw89_read32(rtwdev, R_AX_WDE_INI_STATUS));
	rtw89_err(rtwdev, "[ERR]trxcfg ple 0x8D00 = %x\n",
		  rtw89_read32(rtwdev, R_AX_PLE_INI_STATUS));

	return ret;
}

static int preload_init_set(struct rtw89_dev *rtwdev, enum rtw89_mac_idx mac_idx,
			    enum rtw89_qta_mode mode)
{
	u32 reg, max_preld_size, min_rsvd_size;

	max_preld_size = (mac_idx == RTW89_MAC_0 ?
			  PRELD_B0_ENT_NUM : PRELD_B1_ENT_NUM) * PRELD_AMSDU_SIZE;
	reg = mac_idx == RTW89_MAC_0 ?
	      R_AX_TXPKTCTL_B0_PRELD_CFG0 : R_AX_TXPKTCTL_B1_PRELD_CFG0;
	rtw89_write32_mask(rtwdev, reg, B_AX_B0_PRELD_USEMAXSZ_MASK, max_preld_size);
	rtw89_write32_set(rtwdev, reg, B_AX_B0_PRELD_FEN);

	min_rsvd_size = PRELD_AMSDU_SIZE;
	reg = mac_idx == RTW89_MAC_0 ?
	      R_AX_TXPKTCTL_B0_PRELD_CFG1 : R_AX_TXPKTCTL_B1_PRELD_CFG1;
	rtw89_write32_mask(rtwdev, reg, B_AX_B0_PRELD_NXT_TXENDWIN_MASK, PRELD_NEXT_WND);
	rtw89_write32_mask(rtwdev, reg, B_AX_B0_PRELD_NXT_RSVMINSZ_MASK, min_rsvd_size);

	return 0;
}

static bool is_qta_poh(struct rtw89_dev *rtwdev)
{
	return rtwdev->hci.type == RTW89_HCI_TYPE_PCIE;
}

int rtw89_mac_preload_init(struct rtw89_dev *rtwdev, enum rtw89_mac_idx mac_idx,
			   enum rtw89_qta_mode mode)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->chip_id == RTL8852A || rtw89_is_rtl885xb(rtwdev) ||
	    !is_qta_poh(rtwdev))
		return 0;

	return preload_init_set(rtwdev, mac_idx, mode);
}

static bool dle_is_txq_empty(struct rtw89_dev *rtwdev)
{
	u32 msk32;
	u32 val32;

	msk32 = B_AX_WDE_EMPTY_QUE_CMAC0_ALL_AC | B_AX_WDE_EMPTY_QUE_CMAC0_MBH |
		B_AX_WDE_EMPTY_QUE_CMAC1_MBH | B_AX_WDE_EMPTY_QUE_CMAC0_WMM0 |
		B_AX_WDE_EMPTY_QUE_CMAC0_WMM1 | B_AX_WDE_EMPTY_QUE_OTHERS |
		B_AX_PLE_EMPTY_QUE_DMAC_MPDU_TX | B_AX_PLE_EMPTY_QTA_DMAC_H2C |
		B_AX_PLE_EMPTY_QUE_DMAC_SEC_TX | B_AX_WDE_EMPTY_QUE_DMAC_PKTIN |
		B_AX_WDE_EMPTY_QTA_DMAC_HIF | B_AX_WDE_EMPTY_QTA_DMAC_WLAN_CPU |
		B_AX_WDE_EMPTY_QTA_DMAC_PKTIN | B_AX_WDE_EMPTY_QTA_DMAC_CPUIO |
		B_AX_PLE_EMPTY_QTA_DMAC_B0_TXPL |
		B_AX_PLE_EMPTY_QTA_DMAC_B1_TXPL |
		B_AX_PLE_EMPTY_QTA_DMAC_MPDU_TX |
		B_AX_PLE_EMPTY_QTA_DMAC_CPUIO |
		B_AX_WDE_EMPTY_QTA_DMAC_DATA_CPU |
		B_AX_PLE_EMPTY_QTA_DMAC_WLAN_CPU;
	val32 = rtw89_read32(rtwdev, R_AX_DLE_EMPTY0);

	if ((val32 & msk32) == msk32)
		return true;

	return false;
}

static void _patch_ss2f_path(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->chip_id == RTL8852A || rtw89_is_rtl885xb(rtwdev))
		return;

	rtw89_write32_mask(rtwdev, R_AX_SS2FINFO_PATH, B_AX_SS_DEST_QUEUE_MASK,
			   SS2F_PATH_WLCPU);
}

static int sta_sch_init_ax(struct rtw89_dev *rtwdev)
{
	u32 p_val;
	u8 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	val = rtw89_read8(rtwdev, R_AX_SS_CTRL);
	val |= B_AX_SS_EN;
	rtw89_write8(rtwdev, R_AX_SS_CTRL, val);

	ret = read_poll_timeout(rtw89_read32, p_val, p_val & B_AX_SS_INIT_DONE_1,
				1, TRXCFG_WAIT_CNT, false, rtwdev, R_AX_SS_CTRL);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]STA scheduler init\n");
		return ret;
	}

	rtw89_write32_set(rtwdev, R_AX_SS_CTRL, B_AX_SS_WARM_INIT_FLG);
	rtw89_write32_clr(rtwdev, R_AX_SS_CTRL, B_AX_SS_NONEMPTY_SS2FINFO_EN);

	_patch_ss2f_path(rtwdev);

	return 0;
}

static int mpdu_proc_init_ax(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	rtw89_write32(rtwdev, R_AX_ACTION_FWD0, TRXCFG_MPDU_PROC_ACT_FRWD);
	rtw89_write32(rtwdev, R_AX_TF_FWD, TRXCFG_MPDU_PROC_TF_FRWD);
	rtw89_write32_set(rtwdev, R_AX_MPDU_PROC,
			  B_AX_APPEND_FCS | B_AX_A_ICV_ERR);
	rtw89_write32(rtwdev, R_AX_CUT_AMSDU_CTRL, TRXCFG_MPDU_PROC_CUT_CTRL);

	return 0;
}

static int sec_eng_init_ax(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u32 val = 0;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret)
		return ret;

	val = rtw89_read32(rtwdev, R_AX_SEC_ENG_CTRL);
	/* init clock */
	val |= (B_AX_CLK_EN_CGCMP | B_AX_CLK_EN_WAPI | B_AX_CLK_EN_WEP_TKIP);
	/* init TX encryption */
	val |= (B_AX_SEC_TX_ENC | B_AX_SEC_RX_DEC);
	val |= (B_AX_MC_DEC | B_AX_BC_DEC);
	if (chip->chip_id == RTL8852A || chip->chip_id == RTL8852B ||
	    chip->chip_id == RTL8851B)
		val &= ~B_AX_TX_PARTIAL_MODE;
	rtw89_write32(rtwdev, R_AX_SEC_ENG_CTRL, val);

	/* init MIC ICV append */
	val = rtw89_read32(rtwdev, R_AX_SEC_MPDU_PROC);
	val |= (B_AX_APPEND_ICV | B_AX_APPEND_MIC);

	/* option init */
	rtw89_write32(rtwdev, R_AX_SEC_MPDU_PROC, val);

	if (chip->chip_id == RTL8852C)
		rtw89_write32_mask(rtwdev, R_AX_SEC_DEBUG1,
				   B_AX_TX_TIMEOUT_SEL_MASK, AX_TX_TO_VAL);

	return 0;
}

static int dmac_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	int ret;

	ret = rtw89_mac_dle_init(rtwdev, rtwdev->mac.qta_mode, RTW89_QTA_INVALID);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]DLE init %d\n", ret);
		return ret;
	}

	ret = rtw89_mac_preload_init(rtwdev, RTW89_MAC_0, rtwdev->mac.qta_mode);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]preload init %d\n", ret);
		return ret;
	}

	ret = rtw89_mac_hfc_init(rtwdev, true, true, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]HCI FC init %d\n", ret);
		return ret;
	}

	ret = sta_sch_init_ax(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]STA SCH init %d\n", ret);
		return ret;
	}

	ret = mpdu_proc_init_ax(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]MPDU Proc init %d\n", ret);
		return ret;
	}

	ret = sec_eng_init_ax(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]Security Engine init %d\n", ret);
		return ret;
	}

	return ret;
}

static int addr_cam_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 val, reg;
	u16 p_val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_ADDR_CAM_CTRL, mac_idx);

	val = rtw89_read32(rtwdev, reg);
	val |= u32_encode_bits(0x7f, B_AX_ADDR_CAM_RANGE_MASK) |
	       B_AX_ADDR_CAM_CLR | B_AX_ADDR_CAM_EN;
	rtw89_write32(rtwdev, reg, val);

	ret = read_poll_timeout(rtw89_read16, p_val, !(p_val & B_AX_ADDR_CAM_CLR),
				1, TRXCFG_WAIT_CNT, false, rtwdev, reg);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]ADDR_CAM reset\n");
		return ret;
	}

	return 0;
}

static int scheduler_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 ret;
	u32 reg;
	u32 val;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PREBKF_CFG_1, mac_idx);
	if (rtwdev->chip->chip_id == RTL8852C)
		rtw89_write32_mask(rtwdev, reg, B_AX_SIFS_MACTXEN_T1_MASK,
				   SIFS_MACTXEN_T1_V1);
	else
		rtw89_write32_mask(rtwdev, reg, B_AX_SIFS_MACTXEN_T1_MASK,
				   SIFS_MACTXEN_T1);

	if (rtw89_is_rtl885xb(rtwdev)) {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_SCH_EXT_CTRL, mac_idx);
		rtw89_write32_set(rtwdev, reg, B_AX_PORT_RST_TSF_ADV);
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_CCA_CFG_0, mac_idx);
	rtw89_write32_clr(rtwdev, reg, B_AX_BTCCA_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PREBKF_CFG_0, mac_idx);
	if (rtwdev->chip->chip_id == RTL8852C) {
		val = rtw89_read32_mask(rtwdev, R_AX_SEC_ENG_CTRL,
					B_AX_TX_PARTIAL_MODE);
		if (!val)
			rtw89_write32_mask(rtwdev, reg, B_AX_PREBKF_TIME_MASK,
					   SCH_PREBKF_24US);
	} else {
		rtw89_write32_mask(rtwdev, reg, B_AX_PREBKF_TIME_MASK,
				   SCH_PREBKF_24US);
	}

	return 0;
}

static int rtw89_mac_typ_fltr_opt_ax(struct rtw89_dev *rtwdev,
				     enum rtw89_machdr_frame_type type,
				     enum rtw89_mac_fwd_target fwd_target,
				     u8 mac_idx)
{
	u32 reg;
	u32 val;

	switch (fwd_target) {
	case RTW89_FWD_DONT_CARE:
		val = RX_FLTR_FRAME_DROP;
		break;
	case RTW89_FWD_TO_HOST:
		val = RX_FLTR_FRAME_TO_HOST;
		break;
	case RTW89_FWD_TO_WLAN_CPU:
		val = RX_FLTR_FRAME_TO_WLCPU;
		break;
	default:
		rtw89_err(rtwdev, "[ERR]set rx filter fwd target err\n");
		return -EINVAL;
	}

	switch (type) {
	case RTW89_MGNT:
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_MGNT_FLTR, mac_idx);
		break;
	case RTW89_CTRL:
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_CTRL_FLTR, mac_idx);
		break;
	case RTW89_DATA:
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_DATA_FLTR, mac_idx);
		break;
	default:
		rtw89_err(rtwdev, "[ERR]set rx filter type err\n");
		return -EINVAL;
	}
	rtw89_write32(rtwdev, reg, val);

	return 0;
}

static int rx_fltr_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	int ret, i;
	u32 mac_ftlr, plcp_ftlr;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	for (i = RTW89_MGNT; i <= RTW89_DATA; i++) {
		ret = rtw89_mac_typ_fltr_opt_ax(rtwdev, i, RTW89_FWD_TO_HOST,
						mac_idx);
		if (ret)
			return ret;
	}
	mac_ftlr = rtwdev->hal.rx_fltr;
	plcp_ftlr = B_AX_CCK_CRC_CHK | B_AX_CCK_SIG_CHK |
		    B_AX_LSIG_PARITY_CHK_EN | B_AX_SIGA_CRC_CHK |
		    B_AX_VHT_SU_SIGB_CRC_CHK | B_AX_VHT_MU_SIGB_CRC_CHK |
		    B_AX_HE_SIGB_CRC_CHK;
	rtw89_write32(rtwdev, rtw89_mac_reg_by_idx(rtwdev, R_AX_RX_FLTR_OPT, mac_idx),
		      mac_ftlr);
	rtw89_write16(rtwdev, rtw89_mac_reg_by_idx(rtwdev, R_AX_PLCP_HDR_FLTR, mac_idx),
		      plcp_ftlr);

	return 0;
}

static void _patch_dis_resp_chk(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg, val32;
	u32 b_rsp_chk_nav, b_rsp_chk_cca;

	b_rsp_chk_nav = B_AX_RSP_CHK_TXNAV | B_AX_RSP_CHK_INTRA_NAV |
			B_AX_RSP_CHK_BASIC_NAV;
	b_rsp_chk_cca = B_AX_RSP_CHK_SEC_CCA_80 | B_AX_RSP_CHK_SEC_CCA_40 |
			B_AX_RSP_CHK_SEC_CCA_20 | B_AX_RSP_CHK_BTCCA |
			B_AX_RSP_CHK_EDCCA | B_AX_RSP_CHK_CCA;

	switch (rtwdev->chip->chip_id) {
	case RTL8852A:
	case RTL8852B:
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_RSP_CHK_SIG, mac_idx);
		val32 = rtw89_read32(rtwdev, reg) & ~b_rsp_chk_nav;
		rtw89_write32(rtwdev, reg, val32);

		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_0, mac_idx);
		val32 = rtw89_read32(rtwdev, reg) & ~b_rsp_chk_cca;
		rtw89_write32(rtwdev, reg, val32);
		break;
	default:
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_RSP_CHK_SIG, mac_idx);
		val32 = rtw89_read32(rtwdev, reg) | b_rsp_chk_nav;
		rtw89_write32(rtwdev, reg, val32);

		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_0, mac_idx);
		val32 = rtw89_read32(rtwdev, reg) | b_rsp_chk_cca;
		rtw89_write32(rtwdev, reg, val32);
		break;
	}
}

static int cca_ctrl_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 val, reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_CCA_CONTROL, mac_idx);
	val = rtw89_read32(rtwdev, reg);
	val |= (B_AX_TB_CHK_BASIC_NAV | B_AX_TB_CHK_BTCCA |
		B_AX_TB_CHK_EDCCA | B_AX_TB_CHK_CCA_P20 |
		B_AX_SIFS_CHK_BTCCA | B_AX_SIFS_CHK_CCA_P20 |
		B_AX_CTN_CHK_INTRA_NAV |
		B_AX_CTN_CHK_BASIC_NAV | B_AX_CTN_CHK_BTCCA |
		B_AX_CTN_CHK_EDCCA | B_AX_CTN_CHK_CCA_S80 |
		B_AX_CTN_CHK_CCA_S40 | B_AX_CTN_CHK_CCA_S20 |
		B_AX_CTN_CHK_CCA_P20);
	val &= ~(B_AX_TB_CHK_TX_NAV | B_AX_TB_CHK_CCA_S80 |
		 B_AX_TB_CHK_CCA_S40 | B_AX_TB_CHK_CCA_S20 |
		 B_AX_SIFS_CHK_CCA_S80 | B_AX_SIFS_CHK_CCA_S40 |
		 B_AX_SIFS_CHK_CCA_S20 | B_AX_CTN_CHK_TXNAV |
		 B_AX_SIFS_CHK_EDCCA);

	rtw89_write32(rtwdev, reg, val);

	_patch_dis_resp_chk(rtwdev, mac_idx);

	return 0;
}

static int nav_ctrl_init_ax(struct rtw89_dev *rtwdev)
{
	rtw89_write32_set(rtwdev, R_AX_WMAC_NAV_CTL, B_AX_WMAC_PLCP_UP_NAV_EN |
						     B_AX_WMAC_TF_UP_NAV_EN |
						     B_AX_WMAC_NAV_UPPER_EN);
	rtw89_write32_mask(rtwdev, R_AX_WMAC_NAV_CTL, B_AX_WMAC_NAV_UPPER_MASK, NAV_25MS);

	return 0;
}

static int spatial_reuse_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;
	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_RX_SR_CTRL, mac_idx);
	rtw89_write8_clr(rtwdev, reg, B_AX_SR_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_BSSID_SRC_CTRL, mac_idx);
	rtw89_write8_set(rtwdev, reg, B_AX_PLCP_SRC_EN);

	return 0;
}

static int tmac_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_MAC_LOOPBACK, mac_idx);
	rtw89_write32_clr(rtwdev, reg, B_AX_MACLBK_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TCR0, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_TCR_UDF_THSD_MASK, TCR_UDF_THSD);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TXD_FIFO_CTRL, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_TXDFIFO_HIGH_MCS_THRE_MASK, TXDFIFO_HIGH_MCS_THRE);
	rtw89_write32_mask(rtwdev, reg, B_AX_TXDFIFO_LOW_MCS_THRE_MASK, TXDFIFO_LOW_MCS_THRE);

	return 0;
}

static int trxptcl_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_rrsr_cfgs *rrsr = chip->rrsr_cfgs;
	u32 reg, val, sifs;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_0, mac_idx);
	val = rtw89_read32(rtwdev, reg);
	val &= ~B_AX_WMAC_SPEC_SIFS_CCK_MASK;
	val |= FIELD_PREP(B_AX_WMAC_SPEC_SIFS_CCK_MASK, WMAC_SPEC_SIFS_CCK);

	switch (rtwdev->chip->chip_id) {
	case RTL8852A:
		sifs = WMAC_SPEC_SIFS_OFDM_52A;
		break;
	case RTL8851B:
	case RTL8852B:
	case RTL8852BT:
		sifs = WMAC_SPEC_SIFS_OFDM_52B;
		break;
	default:
		sifs = WMAC_SPEC_SIFS_OFDM_52C;
		break;
	}
	val &= ~B_AX_WMAC_SPEC_SIFS_OFDM_MASK;
	val |= FIELD_PREP(B_AX_WMAC_SPEC_SIFS_OFDM_MASK, sifs);
	rtw89_write32(rtwdev, reg, val);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_RXTRIG_TEST_USER_2, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_RXTRIG_FCSCHK_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, rrsr->ref_rate.addr, mac_idx);
	rtw89_write32_mask(rtwdev, reg, rrsr->ref_rate.mask, rrsr->ref_rate.data);
	reg = rtw89_mac_reg_by_idx(rtwdev, rrsr->rsc.addr, mac_idx);
	rtw89_write32_mask(rtwdev, reg, rrsr->rsc.mask, rrsr->rsc.data);

	return 0;
}

static void rst_bacam(struct rtw89_dev *rtwdev)
{
	u32 val32;
	int ret;

	rtw89_write32_mask(rtwdev, R_AX_RESPBA_CAM_CTRL, B_AX_BACAM_RST_MASK,
			   S_AX_BACAM_RST_ALL);

	ret = read_poll_timeout_atomic(rtw89_read32_mask, val32, val32 == 0,
				       1, 1000, false,
				       rtwdev, R_AX_RESPBA_CAM_CTRL, B_AX_BACAM_RST_MASK);
	if (ret)
		rtw89_warn(rtwdev, "failed to reset BA CAM\n");
}

static int rmac_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
#define TRXCFG_RMAC_CCA_TO	32
#define TRXCFG_RMAC_DATA_TO	15
#define RX_MAX_LEN_UNIT 512
#define PLD_RLS_MAX_PG 127
#define RX_SPEC_MAX_LEN (11454 + RX_MAX_LEN_UNIT)
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	int ret;
	u32 reg, rx_max_len, rx_qta;
	u16 val;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (mac_idx == RTW89_MAC_0)
		rst_bacam(rtwdev);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_RESPBA_CAM_CTRL, mac_idx);
	rtw89_write8_set(rtwdev, reg, B_AX_SSN_SEL);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_DLK_PROTECT_CTL, mac_idx);
	val = rtw89_read16(rtwdev, reg);
	val = u16_replace_bits(val, TRXCFG_RMAC_DATA_TO,
			       B_AX_RX_DLK_DATA_TIME_MASK);
	val = u16_replace_bits(val, TRXCFG_RMAC_CCA_TO,
			       B_AX_RX_DLK_CCA_TIME_MASK);
	if (chip_id == RTL8852BT)
		val |= B_AX_RX_DLK_RST_EN;
	rtw89_write16(rtwdev, reg, val);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_RCR, mac_idx);
	rtw89_write8_mask(rtwdev, reg, B_AX_CH_EN_MASK, 0x1);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_RX_FLTR_OPT, mac_idx);
	if (mac_idx == RTW89_MAC_0)
		rx_qta = rtwdev->mac.dle_info.c0_rx_qta;
	else
		rx_qta = rtwdev->mac.dle_info.c1_rx_qta;
	rx_qta = min_t(u32, rx_qta, PLD_RLS_MAX_PG);
	rx_max_len = rx_qta * rtwdev->mac.dle_info.ple_pg_size;
	rx_max_len = min_t(u32, rx_max_len, RX_SPEC_MAX_LEN);
	rx_max_len /= RX_MAX_LEN_UNIT;
	rtw89_write32_mask(rtwdev, reg, B_AX_RX_MPDU_MAX_LEN_MASK, rx_max_len);

	if (chip_id == RTL8852A && rtwdev->hal.cv == CHIP_CBV) {
		rtw89_write16_mask(rtwdev,
				   rtw89_mac_reg_by_idx(rtwdev, R_AX_DLK_PROTECT_CTL, mac_idx),
				   B_AX_RX_DLK_CCA_TIME_MASK, 0);
		rtw89_write16_set(rtwdev, rtw89_mac_reg_by_idx(rtwdev, R_AX_RCR, mac_idx),
				  BIT(12));
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PLCP_HDR_FLTR, mac_idx);
	rtw89_write8_clr(rtwdev, reg, B_AX_VHT_SU_SIGB_CRC_CHK);

	return ret;
}

static int cmac_com_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u32 val, reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TX_SUB_CARRIER_VALUE, mac_idx);
	val = rtw89_read32(rtwdev, reg);
	val = u32_replace_bits(val, 0, B_AX_TXSC_20M_MASK);
	val = u32_replace_bits(val, 0, B_AX_TXSC_40M_MASK);
	val = u32_replace_bits(val, 0, B_AX_TXSC_80M_MASK);
	rtw89_write32(rtwdev, reg, val);

	if (chip_id == RTL8852A || rtw89_is_rtl885xb(rtwdev)) {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PTCL_RRSR1, mac_idx);
		rtw89_write32_mask(rtwdev, reg, B_AX_RRSR_RATE_EN_MASK, RRSR_OFDM_CCK_EN);
	}

	return 0;
}

bool rtw89_mac_is_qta_dbcc(struct rtw89_dev *rtwdev, enum rtw89_qta_mode mode)
{
	const struct rtw89_dle_mem *cfg;

	cfg = get_dle_mem_cfg(rtwdev, mode);
	if (!cfg) {
		rtw89_err(rtwdev, "[ERR]get_dle_mem_cfg\n");
		return false;
	}

	return (cfg->ple_min_qt->cma1_dma && cfg->ple_max_qt->cma1_dma);
}

static int ptcl_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u32 val, reg;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (rtwdev->hci.type == RTW89_HCI_TYPE_PCIE) {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_SIFS_SETTING, mac_idx);
		val = rtw89_read32(rtwdev, reg);
		val = u32_replace_bits(val, S_AX_CTS2S_TH_1K,
				       B_AX_HW_CTS2SELF_PKT_LEN_TH_MASK);
		val = u32_replace_bits(val, S_AX_CTS2S_TH_SEC_256B,
				       B_AX_HW_CTS2SELF_PKT_LEN_TH_TWW_MASK);
		val |= B_AX_HW_CTS2SELF_EN;
		rtw89_write32(rtwdev, reg, val);

		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PTCL_FSM_MON, mac_idx);
		val = rtw89_read32(rtwdev, reg);
		val = u32_replace_bits(val, S_AX_PTCL_TO_2MS, B_AX_PTCL_TX_ARB_TO_THR_MASK);
		val &= ~B_AX_PTCL_TX_ARB_TO_MODE;
		rtw89_write32(rtwdev, reg, val);
	}

	if (mac_idx == RTW89_MAC_0) {
		rtw89_write8_set(rtwdev, R_AX_PTCL_COMMON_SETTING_0,
				 B_AX_CMAC_TX_MODE_0 | B_AX_CMAC_TX_MODE_1);
		rtw89_write8_clr(rtwdev, R_AX_PTCL_COMMON_SETTING_0,
				 B_AX_PTCL_TRIGGER_SS_EN_0 |
				 B_AX_PTCL_TRIGGER_SS_EN_1 |
				 B_AX_PTCL_TRIGGER_SS_EN_UL);
		rtw89_write8_mask(rtwdev, R_AX_PTCLRPT_FULL_HDL,
				  B_AX_SPE_RPT_PATH_MASK, FWD_TO_WLCPU);
	} else if (mac_idx == RTW89_MAC_1) {
		rtw89_write8_mask(rtwdev, R_AX_PTCLRPT_FULL_HDL_C1,
				  B_AX_SPE_RPT_PATH_MASK, FWD_TO_WLCPU);
	}

	if (chip_id == RTL8852A || rtw89_is_rtl885xb(rtwdev)) {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_AGG_LEN_VHT_0, mac_idx);
		rtw89_write32_mask(rtwdev, reg,
				   B_AX_AMPDU_MAX_LEN_VHT_MASK, 0x3FF80);
	}

	return 0;
}

static int cmac_dma_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;
	int ret;

	if (!rtw89_is_rtl885xb(rtwdev))
		return 0;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_RXDMA_CTRL_0, mac_idx);
	rtw89_write8_clr(rtwdev, reg, RX_FULL_MODE);

	return 0;
}

static int cmac_init_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	int ret;

	ret = scheduler_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d SCH init %d\n", mac_idx, ret);
		return ret;
	}

	ret = addr_cam_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d ADDR_CAM reset %d\n", mac_idx,
			  ret);
		return ret;
	}

	ret = rx_fltr_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d RX filter init %d\n", mac_idx,
			  ret);
		return ret;
	}

	ret = cca_ctrl_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d CCA CTRL init %d\n", mac_idx,
			  ret);
		return ret;
	}

	ret = nav_ctrl_init_ax(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d NAV CTRL init %d\n", mac_idx,
			  ret);
		return ret;
	}

	ret = spatial_reuse_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d Spatial Reuse init %d\n",
			  mac_idx, ret);
		return ret;
	}

	ret = tmac_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d TMAC init %d\n", mac_idx, ret);
		return ret;
	}

	ret = trxptcl_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d TRXPTCL init %d\n", mac_idx, ret);
		return ret;
	}

	ret = rmac_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d RMAC init %d\n", mac_idx, ret);
		return ret;
	}

	ret = cmac_com_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d Com init %d\n", mac_idx, ret);
		return ret;
	}

	ret = ptcl_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d PTCL init %d\n", mac_idx, ret);
		return ret;
	}

	ret = cmac_dma_init_ax(rtwdev, mac_idx);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d DMA init %d\n", mac_idx, ret);
		return ret;
	}

	return ret;
}

static int rtw89_mac_read_phycap(struct rtw89_dev *rtwdev,
				 struct rtw89_mac_c2h_info *c2h_info)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_mac_h2c_info h2c_info = {0};
	u32 ret;

	mac->cnv_efuse_state(rtwdev, false);

	h2c_info.id = RTW89_FWCMD_H2CREG_FUNC_GET_FEATURE;
	h2c_info.content_len = 0;

	ret = rtw89_fw_msg_reg(rtwdev, &h2c_info, c2h_info);
	if (ret)
		goto out;

	if (c2h_info->id != RTW89_FWCMD_C2HREG_FUNC_PHY_CAP)
		ret = -EINVAL;

out:
	mac->cnv_efuse_state(rtwdev, true);

	return ret;
}

int rtw89_mac_setup_phycap(struct rtw89_dev *rtwdev)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	struct rtw89_hal *hal = &rtwdev->hal;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_mac_c2h_info c2h_info = {0};
	const struct rtw89_c2hreg_phycap *phycap;
	u8 tx_nss;
	u8 rx_nss;
	u8 tx_ant;
	u8 rx_ant;
	u32 ret;

	ret = rtw89_mac_read_phycap(rtwdev, &c2h_info);
	if (ret)
		return ret;

	phycap = &c2h_info.u.phycap;

	tx_nss = u32_get_bits(phycap->w1, RTW89_C2HREG_PHYCAP_W1_TX_NSS);
	rx_nss = u32_get_bits(phycap->w0, RTW89_C2HREG_PHYCAP_W0_RX_NSS);
	tx_ant = u32_get_bits(phycap->w3, RTW89_C2HREG_PHYCAP_W3_ANT_TX_NUM);
	rx_ant = u32_get_bits(phycap->w3, RTW89_C2HREG_PHYCAP_W3_ANT_RX_NUM);

	hal->tx_nss = tx_nss ? min_t(u8, tx_nss, chip->tx_nss) : chip->tx_nss;
	hal->rx_nss = rx_nss ? min_t(u8, rx_nss, chip->rx_nss) : chip->rx_nss;

	if (tx_ant == 1)
		hal->antenna_tx = RF_B;
	if (rx_ant == 1)
		hal->antenna_rx = RF_B;

	if (tx_nss == 1 && tx_ant == 2 && rx_ant == 2) {
		hal->antenna_tx = RF_B;
		hal->tx_path_diversity = true;
	}

	if (chip->rf_path_num == 1) {
		hal->antenna_tx = RF_A;
		hal->antenna_rx = RF_A;
		if ((efuse->rfe_type % 3) == 2)
			hal->ant_diversity = true;
	}

	rtw89_debug(rtwdev, RTW89_DBG_FW,
		    "phycap hal/phy/chip: tx_nss=0x%x/0x%x/0x%x rx_nss=0x%x/0x%x/0x%x\n",
		    hal->tx_nss, tx_nss, chip->tx_nss,
		    hal->rx_nss, rx_nss, chip->rx_nss);
	rtw89_debug(rtwdev, RTW89_DBG_FW,
		    "ant num/bitmap: tx=%d/0x%x rx=%d/0x%x\n",
		    tx_ant, hal->antenna_tx, rx_ant, hal->antenna_rx);
	rtw89_debug(rtwdev, RTW89_DBG_FW, "TX path diversity=%d\n", hal->tx_path_diversity);
	rtw89_debug(rtwdev, RTW89_DBG_FW, "Antenna diversity=%d\n", hal->ant_diversity);

	return 0;
}

static int rtw89_hw_sch_tx_en_h2c(struct rtw89_dev *rtwdev, u8 band,
				  u16 tx_en_u16, u16 mask_u16)
{
	u32 ret;
	struct rtw89_mac_c2h_info c2h_info = {0};
	struct rtw89_mac_h2c_info h2c_info = {0};
	struct rtw89_h2creg_sch_tx_en *sch_tx_en = &h2c_info.u.sch_tx_en;

	h2c_info.id = RTW89_FWCMD_H2CREG_FUNC_SCH_TX_EN;
	h2c_info.content_len = sizeof(*sch_tx_en) - RTW89_H2CREG_HDR_LEN;

	u32p_replace_bits(&sch_tx_en->w0, tx_en_u16, RTW89_H2CREG_SCH_TX_EN_W0_EN);
	u32p_replace_bits(&sch_tx_en->w1, mask_u16, RTW89_H2CREG_SCH_TX_EN_W1_MASK);
	u32p_replace_bits(&sch_tx_en->w1, band, RTW89_H2CREG_SCH_TX_EN_W1_BAND);

	ret = rtw89_fw_msg_reg(rtwdev, &h2c_info, &c2h_info);
	if (ret)
		return ret;

	if (c2h_info.id != RTW89_FWCMD_C2HREG_FUNC_TX_PAUSE_RPT)
		return -EINVAL;

	return 0;
}

static int rtw89_set_hw_sch_tx_en(struct rtw89_dev *rtwdev, u8 mac_idx,
				  u16 tx_en, u16 tx_en_mask)
{
	u32 reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_CTN_TXEN, mac_idx);
	u16 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (test_bit(RTW89_FLAG_FW_RDY, rtwdev->flags))
		return rtw89_hw_sch_tx_en_h2c(rtwdev, mac_idx,
					      tx_en, tx_en_mask);

	val = rtw89_read16(rtwdev, reg);
	val = (val & ~tx_en_mask) | (tx_en & tx_en_mask);
	rtw89_write16(rtwdev, reg, val);

	return 0;
}

static int rtw89_set_hw_sch_tx_en_v1(struct rtw89_dev *rtwdev, u8 mac_idx,
				     u32 tx_en, u32 tx_en_mask)
{
	u32 reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_CTN_DRV_TXEN, mac_idx);
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

int rtw89_mac_stop_sch_tx(struct rtw89_dev *rtwdev, u8 mac_idx,
			  u32 *tx_en, enum rtw89_sch_tx_sel sel)
{
	int ret;

	*tx_en = rtw89_read16(rtwdev,
			      rtw89_mac_reg_by_idx(rtwdev, R_AX_CTN_TXEN, mac_idx));

	switch (sel) {
	case RTW89_SCH_TX_SEL_ALL:
		ret = rtw89_set_hw_sch_tx_en(rtwdev, mac_idx, 0,
					     B_AX_CTN_TXEN_ALL_MASK);
		if (ret)
			return ret;
		break;
	case RTW89_SCH_TX_SEL_HIQ:
		ret = rtw89_set_hw_sch_tx_en(rtwdev, mac_idx,
					     0, B_AX_CTN_TXEN_HGQ);
		if (ret)
			return ret;
		break;
	case RTW89_SCH_TX_SEL_MG0:
		ret = rtw89_set_hw_sch_tx_en(rtwdev, mac_idx,
					     0, B_AX_CTN_TXEN_MGQ);
		if (ret)
			return ret;
		break;
	case RTW89_SCH_TX_SEL_MACID:
		ret = rtw89_set_hw_sch_tx_en(rtwdev, mac_idx, 0,
					     B_AX_CTN_TXEN_ALL_MASK);
		if (ret)
			return ret;
		break;
	default:
		return 0;
	}

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_stop_sch_tx);

int rtw89_mac_stop_sch_tx_v1(struct rtw89_dev *rtwdev, u8 mac_idx,
			     u32 *tx_en, enum rtw89_sch_tx_sel sel)
{
	int ret;

	*tx_en = rtw89_read32(rtwdev,
			      rtw89_mac_reg_by_idx(rtwdev, R_AX_CTN_DRV_TXEN, mac_idx));

	switch (sel) {
	case RTW89_SCH_TX_SEL_ALL:
		ret = rtw89_set_hw_sch_tx_en_v1(rtwdev, mac_idx, 0,
						B_AX_CTN_TXEN_ALL_MASK_V1);
		if (ret)
			return ret;
		break;
	case RTW89_SCH_TX_SEL_HIQ:
		ret = rtw89_set_hw_sch_tx_en_v1(rtwdev, mac_idx,
						0, B_AX_CTN_TXEN_HGQ);
		if (ret)
			return ret;
		break;
	case RTW89_SCH_TX_SEL_MG0:
		ret = rtw89_set_hw_sch_tx_en_v1(rtwdev, mac_idx,
						0, B_AX_CTN_TXEN_MGQ);
		if (ret)
			return ret;
		break;
	case RTW89_SCH_TX_SEL_MACID:
		ret = rtw89_set_hw_sch_tx_en_v1(rtwdev, mac_idx, 0,
						B_AX_CTN_TXEN_ALL_MASK_V1);
		if (ret)
			return ret;
		break;
	default:
		return 0;
	}

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_stop_sch_tx_v1);

int rtw89_mac_resume_sch_tx(struct rtw89_dev *rtwdev, u8 mac_idx, u32 tx_en)
{
	int ret;

	ret = rtw89_set_hw_sch_tx_en(rtwdev, mac_idx, tx_en, B_AX_CTN_TXEN_ALL_MASK);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_resume_sch_tx);

int rtw89_mac_resume_sch_tx_v1(struct rtw89_dev *rtwdev, u8 mac_idx, u32 tx_en)
{
	int ret;

	ret = rtw89_set_hw_sch_tx_en_v1(rtwdev, mac_idx, tx_en,
					B_AX_CTN_TXEN_ALL_MASK_V1);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_resume_sch_tx_v1);

static int dle_buf_req_ax(struct rtw89_dev *rtwdev, u16 buf_len, bool wd, u16 *pkt_id)
{
	u32 val, reg;
	int ret;

	reg = wd ? R_AX_WD_BUF_REQ : R_AX_PL_BUF_REQ;
	val = buf_len;
	val |= B_AX_WD_BUF_REQ_EXEC;
	rtw89_write32(rtwdev, reg, val);

	reg = wd ? R_AX_WD_BUF_STATUS : R_AX_PL_BUF_STATUS;

	ret = read_poll_timeout(rtw89_read32, val, val & B_AX_WD_BUF_STAT_DONE,
				1, 2000, false, rtwdev, reg);
	if (ret)
		return ret;

	*pkt_id = FIELD_GET(B_AX_WD_BUF_STAT_PKTID_MASK, val);
	if (*pkt_id == S_WD_BUF_STAT_PKTID_INVALID)
		return -ENOENT;

	return 0;
}

static int set_cpuio_ax(struct rtw89_dev *rtwdev,
			struct rtw89_cpuio_ctrl *ctrl_para, bool wd)
{
	u32 val, cmd_type, reg;
	int ret;

	cmd_type = ctrl_para->cmd_type;

	reg = wd ? R_AX_WD_CPUQ_OP_2 : R_AX_PL_CPUQ_OP_2;
	val = 0;
	val = u32_replace_bits(val, ctrl_para->start_pktid,
			       B_AX_WD_CPUQ_OP_STRT_PKTID_MASK);
	val = u32_replace_bits(val, ctrl_para->end_pktid,
			       B_AX_WD_CPUQ_OP_END_PKTID_MASK);
	rtw89_write32(rtwdev, reg, val);

	reg = wd ? R_AX_WD_CPUQ_OP_1 : R_AX_PL_CPUQ_OP_1;
	val = 0;
	val = u32_replace_bits(val, ctrl_para->src_pid,
			       B_AX_CPUQ_OP_SRC_PID_MASK);
	val = u32_replace_bits(val, ctrl_para->src_qid,
			       B_AX_CPUQ_OP_SRC_QID_MASK);
	val = u32_replace_bits(val, ctrl_para->dst_pid,
			       B_AX_CPUQ_OP_DST_PID_MASK);
	val = u32_replace_bits(val, ctrl_para->dst_qid,
			       B_AX_CPUQ_OP_DST_QID_MASK);
	rtw89_write32(rtwdev, reg, val);

	reg = wd ? R_AX_WD_CPUQ_OP_0 : R_AX_PL_CPUQ_OP_0;
	val = 0;
	val = u32_replace_bits(val, cmd_type,
			       B_AX_CPUQ_OP_CMD_TYPE_MASK);
	val = u32_replace_bits(val, ctrl_para->macid,
			       B_AX_CPUQ_OP_MACID_MASK);
	val = u32_replace_bits(val, ctrl_para->pkt_num,
			       B_AX_CPUQ_OP_PKTNUM_MASK);
	val |= B_AX_WD_CPUQ_OP_EXEC;
	rtw89_write32(rtwdev, reg, val);

	reg = wd ? R_AX_WD_CPUQ_OP_STATUS : R_AX_PL_CPUQ_OP_STATUS;

	ret = read_poll_timeout(rtw89_read32, val, val & B_AX_WD_CPUQ_OP_STAT_DONE,
				1, 2000, false, rtwdev, reg);
	if (ret)
		return ret;

	if (cmd_type == CPUIO_OP_CMD_GET_1ST_PID ||
	    cmd_type == CPUIO_OP_CMD_GET_NEXT_PID)
		ctrl_para->pktid = FIELD_GET(B_AX_WD_CPUQ_OP_PKTID_MASK, val);

	return 0;
}

int rtw89_mac_dle_quota_change(struct rtw89_dev *rtwdev, enum rtw89_qta_mode mode,
			       bool band1_en)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_dle_mem *cfg;

	cfg = get_dle_mem_cfg(rtwdev, mode);
	if (!cfg) {
		rtw89_err(rtwdev, "[ERR]wd/dle mem cfg\n");
		return -EINVAL;
	}

	if (dle_used_size(cfg) != dle_expected_used_size(rtwdev, mode)) {
		rtw89_err(rtwdev, "[ERR]wd/dle mem cfg\n");
		return -EINVAL;
	}

	dle_quota_cfg(rtwdev, cfg, INVALID_QT_WCPU);

	return mac->dle_quota_change(rtwdev, band1_en);
}

static int dle_quota_change_ax(struct rtw89_dev *rtwdev, bool band1_en)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_cpuio_ctrl ctrl_para = {0};
	u16 pkt_id;
	int ret;

	ret = mac->dle_buf_req(rtwdev, 0x20, true, &pkt_id);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]WDE DLE buf req\n");
		return ret;
	}

	ctrl_para.cmd_type = CPUIO_OP_CMD_ENQ_TO_HEAD;
	ctrl_para.start_pktid = pkt_id;
	ctrl_para.end_pktid = pkt_id;
	ctrl_para.pkt_num = 0;
	ctrl_para.dst_pid = WDE_DLE_PORT_ID_WDRLS;
	ctrl_para.dst_qid = WDE_DLE_QUEID_NO_REPORT;
	ret = mac->set_cpuio(rtwdev, &ctrl_para, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]WDE DLE enqueue to head\n");
		return -EFAULT;
	}

	ret = mac->dle_buf_req(rtwdev, 0x20, false, &pkt_id);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]PLE DLE buf req\n");
		return ret;
	}

	ctrl_para.cmd_type = CPUIO_OP_CMD_ENQ_TO_HEAD;
	ctrl_para.start_pktid = pkt_id;
	ctrl_para.end_pktid = pkt_id;
	ctrl_para.pkt_num = 0;
	ctrl_para.dst_pid = PLE_DLE_PORT_ID_PLRLS;
	ctrl_para.dst_qid = PLE_DLE_QUEID_NO_REPORT;
	ret = mac->set_cpuio(rtwdev, &ctrl_para, false);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]PLE DLE enqueue to head\n");
		return -EFAULT;
	}

	return 0;
}

static int band_idle_ck_b(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	int ret;
	u32 reg;
	u8 val;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PTCL_TX_CTN_SEL, mac_idx);

	ret = read_poll_timeout(rtw89_read8, val,
				(val & B_AX_PTCL_TX_ON_STAT) == 0,
				SW_CVR_DUR_US,
				SW_CVR_DUR_US * PTCL_IDLE_POLL_CNT,
				false, rtwdev, reg);
	if (ret)
		return ret;

	return 0;
}

static int band1_enable_ax(struct rtw89_dev *rtwdev)
{
	int ret, i;
	u32 sleep_bak[4] = {0};
	u32 pause_bak[4] = {0};
	u32 tx_en;

	ret = rtw89_chip_stop_sch_tx(rtwdev, 0, &tx_en, RTW89_SCH_TX_SEL_ALL);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]stop sch tx %d\n", ret);
		return ret;
	}

	for (i = 0; i < 4; i++) {
		sleep_bak[i] = rtw89_read32(rtwdev, R_AX_MACID_SLEEP_0 + i * 4);
		pause_bak[i] = rtw89_read32(rtwdev, R_AX_SS_MACID_PAUSE_0 + i * 4);
		rtw89_write32(rtwdev, R_AX_MACID_SLEEP_0 + i * 4, U32_MAX);
		rtw89_write32(rtwdev, R_AX_SS_MACID_PAUSE_0 + i * 4, U32_MAX);
	}

	ret = band_idle_ck_b(rtwdev, 0);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]tx idle poll %d\n", ret);
		return ret;
	}

	ret = rtw89_mac_dle_quota_change(rtwdev, rtwdev->mac.qta_mode, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]DLE quota change %d\n", ret);
		return ret;
	}

	for (i = 0; i < 4; i++) {
		rtw89_write32(rtwdev, R_AX_MACID_SLEEP_0 + i * 4, sleep_bak[i]);
		rtw89_write32(rtwdev, R_AX_SS_MACID_PAUSE_0 + i * 4, pause_bak[i]);
	}

	ret = rtw89_chip_resume_sch_tx(rtwdev, 0, tx_en);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC1 resume sch tx %d\n", ret);
		return ret;
	}

	ret = cmac_func_en_ax(rtwdev, 1, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC1 func en %d\n", ret);
		return ret;
	}

	ret = cmac_init_ax(rtwdev, 1);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC1 init %d\n", ret);
		return ret;
	}

	rtw89_write32_set(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND,
			  B_AX_R_SYM_FEN_WLBBFUN_1 | B_AX_R_SYM_FEN_WLBBGLB_1);

	return 0;
}

static void rtw89_wdrls_imr_enable(struct rtw89_dev *rtwdev)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;

	rtw89_write32_clr(rtwdev, R_AX_WDRLS_ERR_IMR, B_AX_WDRLS_IMR_EN_CLR);
	rtw89_write32_set(rtwdev, R_AX_WDRLS_ERR_IMR, imr->wdrls_imr_set);
}

static void rtw89_wsec_imr_enable(struct rtw89_dev *rtwdev)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;

	rtw89_write32_set(rtwdev, imr->wsec_imr_reg, imr->wsec_imr_set);
}

static void rtw89_mpdu_trx_imr_enable(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;

	rtw89_write32_clr(rtwdev, R_AX_MPDU_TX_ERR_IMR,
			  B_AX_TX_GET_ERRPKTID_INT_EN |
			  B_AX_TX_NXT_ERRPKTID_INT_EN |
			  B_AX_TX_MPDU_SIZE_ZERO_INT_EN |
			  B_AX_TX_OFFSET_ERR_INT_EN |
			  B_AX_TX_HDR3_SIZE_ERR_INT_EN);
	if (chip_id == RTL8852C)
		rtw89_write32_clr(rtwdev, R_AX_MPDU_TX_ERR_IMR,
				  B_AX_TX_ETH_TYPE_ERR_EN |
				  B_AX_TX_LLC_PRE_ERR_EN |
				  B_AX_TX_NW_TYPE_ERR_EN |
				  B_AX_TX_KSRCH_ERR_EN);
	rtw89_write32_set(rtwdev, R_AX_MPDU_TX_ERR_IMR,
			  imr->mpdu_tx_imr_set);

	rtw89_write32_clr(rtwdev, R_AX_MPDU_RX_ERR_IMR,
			  B_AX_GETPKTID_ERR_INT_EN |
			  B_AX_MHDRLEN_ERR_INT_EN |
			  B_AX_RPT_ERR_INT_EN);
	rtw89_write32_set(rtwdev, R_AX_MPDU_RX_ERR_IMR,
			  imr->mpdu_rx_imr_set);
}

static void rtw89_sta_sch_imr_enable(struct rtw89_dev *rtwdev)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;

	rtw89_write32_clr(rtwdev, R_AX_STA_SCHEDULER_ERR_IMR,
			  B_AX_SEARCH_HANG_TIMEOUT_INT_EN |
			  B_AX_RPT_HANG_TIMEOUT_INT_EN |
			  B_AX_PLE_B_PKTID_ERR_INT_EN);
	rtw89_write32_set(rtwdev, R_AX_STA_SCHEDULER_ERR_IMR,
			  imr->sta_sch_imr_set);
}

static void rtw89_txpktctl_imr_enable(struct rtw89_dev *rtwdev)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;

	rtw89_write32_clr(rtwdev, imr->txpktctl_imr_b0_reg,
			  imr->txpktctl_imr_b0_clr);
	rtw89_write32_set(rtwdev, imr->txpktctl_imr_b0_reg,
			  imr->txpktctl_imr_b0_set);
	rtw89_write32_clr(rtwdev, imr->txpktctl_imr_b1_reg,
			  imr->txpktctl_imr_b1_clr);
	rtw89_write32_set(rtwdev, imr->txpktctl_imr_b1_reg,
			  imr->txpktctl_imr_b1_set);
}

static void rtw89_wde_imr_enable(struct rtw89_dev *rtwdev)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;

	rtw89_write32_clr(rtwdev, R_AX_WDE_ERR_IMR, imr->wde_imr_clr);
	rtw89_write32_set(rtwdev, R_AX_WDE_ERR_IMR, imr->wde_imr_set);
}

static void rtw89_ple_imr_enable(struct rtw89_dev *rtwdev)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;

	rtw89_write32_clr(rtwdev, R_AX_PLE_ERR_IMR, imr->ple_imr_clr);
	rtw89_write32_set(rtwdev, R_AX_PLE_ERR_IMR, imr->ple_imr_set);
}

static void rtw89_pktin_imr_enable(struct rtw89_dev *rtwdev)
{
	rtw89_write32_set(rtwdev, R_AX_PKTIN_ERR_IMR,
			  B_AX_PKTIN_GETPKTID_ERR_INT_EN);
}

static void rtw89_dispatcher_imr_enable(struct rtw89_dev *rtwdev)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;

	rtw89_write32_clr(rtwdev, R_AX_HOST_DISPATCHER_ERR_IMR,
			  imr->host_disp_imr_clr);
	rtw89_write32_set(rtwdev, R_AX_HOST_DISPATCHER_ERR_IMR,
			  imr->host_disp_imr_set);
	rtw89_write32_clr(rtwdev, R_AX_CPU_DISPATCHER_ERR_IMR,
			  imr->cpu_disp_imr_clr);
	rtw89_write32_set(rtwdev, R_AX_CPU_DISPATCHER_ERR_IMR,
			  imr->cpu_disp_imr_set);
	rtw89_write32_clr(rtwdev, R_AX_OTHER_DISPATCHER_ERR_IMR,
			  imr->other_disp_imr_clr);
	rtw89_write32_set(rtwdev, R_AX_OTHER_DISPATCHER_ERR_IMR,
			  imr->other_disp_imr_set);
}

static void rtw89_cpuio_imr_enable(struct rtw89_dev *rtwdev)
{
	rtw89_write32_clr(rtwdev, R_AX_CPUIO_ERR_IMR, B_AX_CPUIO_IMR_CLR);
	rtw89_write32_set(rtwdev, R_AX_CPUIO_ERR_IMR, B_AX_CPUIO_IMR_SET);
}

static void rtw89_bbrpt_imr_enable(struct rtw89_dev *rtwdev)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;

	rtw89_write32_set(rtwdev, imr->bbrpt_com_err_imr_reg,
			  B_AX_BBRPT_COM_NULL_PLPKTID_ERR_INT_EN);
	rtw89_write32_clr(rtwdev, imr->bbrpt_chinfo_err_imr_reg,
			  B_AX_BBRPT_CHINFO_IMR_CLR);
	rtw89_write32_set(rtwdev, imr->bbrpt_chinfo_err_imr_reg,
			  imr->bbrpt_err_imr_set);
	rtw89_write32_set(rtwdev, imr->bbrpt_dfs_err_imr_reg,
			  B_AX_BBRPT_DFS_TO_ERR_INT_EN);
	rtw89_write32_set(rtwdev, R_AX_LA_ERRFLAG, B_AX_LA_IMR_DATA_LOSS_ERR);
}

static void rtw89_scheduler_imr_enable(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_SCHEDULE_ERR_IMR, mac_idx);
	rtw89_write32_clr(rtwdev, reg, B_AX_SORT_NON_IDLE_ERR_INT_EN |
				       B_AX_FSM_TIMEOUT_ERR_INT_EN);
	rtw89_write32_set(rtwdev, reg, B_AX_FSM_TIMEOUT_ERR_INT_EN);
}

static void rtw89_ptcl_imr_enable(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PTCL_IMR0, mac_idx);
	rtw89_write32_clr(rtwdev, reg, imr->ptcl_imr_clr);
	rtw89_write32_set(rtwdev, reg, imr->ptcl_imr_set);
}

static void rtw89_cdma_imr_enable(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, imr->cdma_imr_0_reg, mac_idx);
	rtw89_write32_clr(rtwdev, reg, imr->cdma_imr_0_clr);
	rtw89_write32_set(rtwdev, reg, imr->cdma_imr_0_set);

	if (chip_id == RTL8852C) {
		reg = rtw89_mac_reg_by_idx(rtwdev, imr->cdma_imr_1_reg, mac_idx);
		rtw89_write32_clr(rtwdev, reg, imr->cdma_imr_1_clr);
		rtw89_write32_set(rtwdev, reg, imr->cdma_imr_1_set);
	}
}

static void rtw89_phy_intf_imr_enable(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, imr->phy_intf_imr_reg, mac_idx);
	rtw89_write32_clr(rtwdev, reg, imr->phy_intf_imr_clr);
	rtw89_write32_set(rtwdev, reg, imr->phy_intf_imr_set);
}

static void rtw89_rmac_imr_enable(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, imr->rmac_imr_reg, mac_idx);
	rtw89_write32_clr(rtwdev, reg, imr->rmac_imr_clr);
	rtw89_write32_set(rtwdev, reg, imr->rmac_imr_set);
}

static void rtw89_tmac_imr_enable(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	const struct rtw89_imr_info *imr = rtwdev->chip->imr_info;
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, imr->tmac_imr_reg, mac_idx);
	rtw89_write32_clr(rtwdev, reg, imr->tmac_imr_clr);
	rtw89_write32_set(rtwdev, reg, imr->tmac_imr_set);
}

static int enable_imr_ax(struct rtw89_dev *rtwdev, u8 mac_idx,
			 enum rtw89_mac_hwmod_sel sel)
{
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, sel);
	if (ret) {
		rtw89_err(rtwdev, "MAC%d mac_idx%d is not ready\n",
			  sel, mac_idx);
		return ret;
	}

	if (sel == RTW89_DMAC_SEL) {
		rtw89_wdrls_imr_enable(rtwdev);
		rtw89_wsec_imr_enable(rtwdev);
		rtw89_mpdu_trx_imr_enable(rtwdev);
		rtw89_sta_sch_imr_enable(rtwdev);
		rtw89_txpktctl_imr_enable(rtwdev);
		rtw89_wde_imr_enable(rtwdev);
		rtw89_ple_imr_enable(rtwdev);
		rtw89_pktin_imr_enable(rtwdev);
		rtw89_dispatcher_imr_enable(rtwdev);
		rtw89_cpuio_imr_enable(rtwdev);
		rtw89_bbrpt_imr_enable(rtwdev);
	} else if (sel == RTW89_CMAC_SEL) {
		rtw89_scheduler_imr_enable(rtwdev, mac_idx);
		rtw89_ptcl_imr_enable(rtwdev, mac_idx);
		rtw89_cdma_imr_enable(rtwdev, mac_idx);
		rtw89_phy_intf_imr_enable(rtwdev, mac_idx);
		rtw89_rmac_imr_enable(rtwdev, mac_idx);
		rtw89_tmac_imr_enable(rtwdev, mac_idx);
	} else {
		return -EINVAL;
	}

	return 0;
}

static void err_imr_ctrl_ax(struct rtw89_dev *rtwdev, bool en)
{
	rtw89_write32(rtwdev, R_AX_DMAC_ERR_IMR,
		      en ? DMAC_ERR_IMR_EN : DMAC_ERR_IMR_DIS);
	rtw89_write32(rtwdev, R_AX_CMAC_ERR_IMR,
		      en ? CMAC0_ERR_IMR_EN : CMAC0_ERR_IMR_DIS);
	if (!rtw89_is_rtl885xb(rtwdev) && rtwdev->mac.dle_info.c1_rx_qta)
		rtw89_write32(rtwdev, R_AX_CMAC_ERR_IMR_C1,
			      en ? CMAC1_ERR_IMR_EN : CMAC1_ERR_IMR_DIS);
}

static int dbcc_enable_ax(struct rtw89_dev *rtwdev, bool enable)
{
	int ret = 0;

	if (enable) {
		ret = band1_enable_ax(rtwdev);
		if (ret) {
			rtw89_err(rtwdev, "[ERR] band1_enable %d\n", ret);
			return ret;
		}

		ret = enable_imr_ax(rtwdev, RTW89_MAC_1, RTW89_CMAC_SEL);
		if (ret) {
			rtw89_err(rtwdev, "[ERR] enable CMAC1 IMR %d\n", ret);
			return ret;
		}
	} else {
		rtw89_err(rtwdev, "[ERR] disable dbcc is not implemented not\n");
		return -EINVAL;
	}

	return 0;
}

static int set_host_rpr_ax(struct rtw89_dev *rtwdev)
{
	if (rtwdev->hci.type == RTW89_HCI_TYPE_PCIE) {
		rtw89_write32_mask(rtwdev, R_AX_WDRLS_CFG,
				   B_AX_WDRLS_MODE_MASK, RTW89_RPR_MODE_POH);
		rtw89_write32_set(rtwdev, R_AX_RLSRPT0_CFG0,
				  B_AX_RLSRPT0_FLTR_MAP_MASK);
	} else {
		rtw89_write32_mask(rtwdev, R_AX_WDRLS_CFG,
				   B_AX_WDRLS_MODE_MASK, RTW89_RPR_MODE_STF);
		rtw89_write32_clr(rtwdev, R_AX_RLSRPT0_CFG0,
				  B_AX_RLSRPT0_FLTR_MAP_MASK);
	}

	rtw89_write32_mask(rtwdev, R_AX_RLSRPT0_CFG1, B_AX_RLSRPT0_AGGNUM_MASK, 30);
	rtw89_write32_mask(rtwdev, R_AX_RLSRPT0_CFG1, B_AX_RLSRPT0_TO_MASK, 255);

	return 0;
}

static int trx_init_ax(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	enum rtw89_qta_mode qta_mode = rtwdev->mac.qta_mode;
	int ret;

	ret = dmac_init_ax(rtwdev, 0);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]DMAC init %d\n", ret);
		return ret;
	}

	ret = cmac_init_ax(rtwdev, 0);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]CMAC%d init %d\n", 0, ret);
		return ret;
	}

	if (rtw89_mac_is_qta_dbcc(rtwdev, qta_mode)) {
		ret = dbcc_enable_ax(rtwdev, true);
		if (ret) {
			rtw89_err(rtwdev, "[ERR]dbcc_enable init %d\n", ret);
			return ret;
		}
	}

	ret = enable_imr_ax(rtwdev, RTW89_MAC_0, RTW89_DMAC_SEL);
	if (ret) {
		rtw89_err(rtwdev, "[ERR] enable DMAC IMR %d\n", ret);
		return ret;
	}

	ret = enable_imr_ax(rtwdev, RTW89_MAC_0, RTW89_CMAC_SEL);
	if (ret) {
		rtw89_err(rtwdev, "[ERR] to enable CMAC0 IMR %d\n", ret);
		return ret;
	}

	err_imr_ctrl_ax(rtwdev, true);

	ret = set_host_rpr_ax(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR] set host rpr %d\n", ret);
		return ret;
	}

	if (chip_id == RTL8852C)
		rtw89_write32_clr(rtwdev, R_AX_RSP_CHK_SIG,
				  B_AX_RSP_STATIC_RTS_CHK_SERV_BW_EN);

	return 0;
}

static int rtw89_mac_feat_init(struct rtw89_dev *rtwdev)
{
#define BACAM_1024BMP_OCC_ENTRY 4
#define BACAM_MAX_RU_SUPPORT_B0_STA 1
#define BACAM_MAX_RU_SUPPORT_B1_STA 1
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 users, offset;

	if (chip->bacam_ver != RTW89_BACAM_V1)
		return 0;

	offset = 0;
	users = BACAM_MAX_RU_SUPPORT_B0_STA;
	rtw89_fw_h2c_init_ba_cam_users(rtwdev, users, offset, RTW89_MAC_0);

	offset += users * BACAM_1024BMP_OCC_ENTRY;
	users = BACAM_MAX_RU_SUPPORT_B1_STA;
	rtw89_fw_h2c_init_ba_cam_users(rtwdev, users, offset, RTW89_MAC_1);

	return 0;
}

static void rtw89_disable_fw_watchdog(struct rtw89_dev *rtwdev)
{
	u32 val32;

	if (rtw89_is_rtl885xb(rtwdev)) {
		rtw89_write32_clr(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_APB_WRAP_EN);
		rtw89_write32_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_APB_WRAP_EN);
		return;
	}

	rtw89_mac_mem_write(rtwdev, R_AX_WDT_CTRL,
			    WDT_CTRL_ALL_DIS, RTW89_MAC_MEM_CPU_LOCAL);

	val32 = rtw89_mac_mem_read(rtwdev, R_AX_WDT_STATUS, RTW89_MAC_MEM_CPU_LOCAL);
	val32 |= B_AX_FS_WDT_INT;
	val32 &= ~B_AX_FS_WDT_INT_MSK;
	rtw89_mac_mem_write(rtwdev, R_AX_WDT_STATUS, val32, RTW89_MAC_MEM_CPU_LOCAL);
}

static void rtw89_mac_disable_cpu_ax(struct rtw89_dev *rtwdev)
{
	clear_bit(RTW89_FLAG_FW_RDY, rtwdev->flags);

	rtw89_write32_clr(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_WCPU_EN);
	rtw89_write32_clr(rtwdev, R_AX_WCPU_FW_CTRL, B_AX_WCPU_FWDL_EN |
			  B_AX_H2C_PATH_RDY | B_AX_FWDL_PATH_RDY);
	rtw89_write32_clr(rtwdev, R_AX_SYS_CLK_CTRL, B_AX_CPU_CLK_EN);

	rtw89_disable_fw_watchdog(rtwdev);

	rtw89_write32_clr(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	rtw89_write32_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
}

static int rtw89_mac_enable_cpu_ax(struct rtw89_dev *rtwdev, u8 boot_reason,
				   bool dlfw, bool include_bb)
{
	u32 val;
	int ret;

	if (rtw89_read32(rtwdev, R_AX_PLATFORM_ENABLE) & B_AX_WCPU_EN)
		return -EFAULT;

	rtw89_write32(rtwdev, R_AX_UDM1, 0);
	rtw89_write32(rtwdev, R_AX_UDM2, 0);
	rtw89_write32(rtwdev, R_AX_HALT_H2C_CTRL, 0);
	rtw89_write32(rtwdev, R_AX_HALT_C2H_CTRL, 0);
	rtw89_write32(rtwdev, R_AX_HALT_H2C, 0);
	rtw89_write32(rtwdev, R_AX_HALT_C2H, 0);

	rtw89_write32_set(rtwdev, R_AX_SYS_CLK_CTRL, B_AX_CPU_CLK_EN);

	val = rtw89_read32(rtwdev, R_AX_WCPU_FW_CTRL);
	val &= ~(B_AX_WCPU_FWDL_EN | B_AX_H2C_PATH_RDY | B_AX_FWDL_PATH_RDY);
	val = u32_replace_bits(val, RTW89_FWDL_INITIAL_STATE,
			       B_AX_WCPU_FWDL_STS_MASK);

	if (dlfw)
		val |= B_AX_WCPU_FWDL_EN;

	rtw89_write32(rtwdev, R_AX_WCPU_FW_CTRL, val);

	if (rtwdev->chip->chip_id == RTL8852B)
		rtw89_write32_mask(rtwdev, R_AX_SEC_CTRL,
				   B_AX_SEC_IDMEM_SIZE_CONFIG_MASK, 0x2);

	rtw89_write16_mask(rtwdev, R_AX_BOOT_REASON, B_AX_BOOT_REASON_MASK,
			   boot_reason);
	rtw89_write32_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_WCPU_EN);

	if (!dlfw) {
		mdelay(5);

		ret = rtw89_fw_check_rdy(rtwdev, RTW89_FWDL_CHECK_FREERTOS_DONE);
		if (ret)
			return ret;
	}

	return 0;
}

static void rtw89_mac_hci_func_en_ax(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u32 val;

	if (chip_id == RTL8852C)
		val = B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_DISPATCHER_EN |
		      B_AX_PKT_BUF_EN | B_AX_H_AXIDMA_EN;
	else
		val = B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_DISPATCHER_EN |
		      B_AX_PKT_BUF_EN;
	rtw89_write32(rtwdev, R_AX_DMAC_FUNC_EN, val);
}

static void rtw89_mac_dmac_func_pre_en_ax(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u32 val;

	if (chip_id == RTL8851B || chip_id == RTL8852BT)
		val = B_AX_DISPATCHER_CLK_EN | B_AX_AXIDMA_CLK_EN;
	else
		val = B_AX_DISPATCHER_CLK_EN;
	rtw89_write32(rtwdev, R_AX_DMAC_CLK_EN, val);

	if (chip_id != RTL8852C)
		return;

	val = rtw89_read32(rtwdev, R_AX_HAXI_INIT_CFG1);
	val &= ~(B_AX_DMA_MODE_MASK | B_AX_STOP_AXI_MST);
	val |= FIELD_PREP(B_AX_DMA_MODE_MASK, DMA_MOD_PCIE_1B) |
	       B_AX_TXHCI_EN_V1 | B_AX_RXHCI_EN_V1;
	rtw89_write32(rtwdev, R_AX_HAXI_INIT_CFG1, val);

	rtw89_write32_clr(rtwdev, R_AX_HAXI_DMA_STOP1,
			  B_AX_STOP_ACH0 | B_AX_STOP_ACH1 | B_AX_STOP_ACH3 |
			  B_AX_STOP_ACH4 | B_AX_STOP_ACH5 | B_AX_STOP_ACH6 |
			  B_AX_STOP_ACH7 | B_AX_STOP_CH8 | B_AX_STOP_CH9 |
			  B_AX_STOP_CH12 | B_AX_STOP_ACH2);
	rtw89_write32_clr(rtwdev, R_AX_HAXI_DMA_STOP2, B_AX_STOP_CH10 | B_AX_STOP_CH11);
	rtw89_write32_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_AXIDMA_EN);
}

static int rtw89_mac_dmac_pre_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	int ret;

	mac->hci_func_en(rtwdev);
	mac->dmac_func_pre_en(rtwdev);

	ret = rtw89_mac_dle_init(rtwdev, RTW89_QTA_DLFW, rtwdev->mac.qta_mode);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]DLE pre init %d\n", ret);
		return ret;
	}

	ret = rtw89_mac_hfc_init(rtwdev, true, false, true);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]HCI FC pre init %d\n", ret);
		return ret;
	}

	return ret;
}

int rtw89_mac_enable_bb_rf(struct rtw89_dev *rtwdev)
{
	rtw89_write8_set(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL,
			  B_AX_WLRF1_CTRL_7 | B_AX_WLRF1_CTRL_1 |
			  B_AX_WLRF_CTRL_7 | B_AX_WLRF_CTRL_1);
	rtw89_write8_set(rtwdev, R_AX_PHYREG_SET, PHYREG_SET_ALL_CYCLE);

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_enable_bb_rf);

int rtw89_mac_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL,
			  B_AX_WLRF1_CTRL_7 | B_AX_WLRF1_CTRL_1 |
			  B_AX_WLRF_CTRL_7 | B_AX_WLRF_CTRL_1);
	rtw89_write8_clr(rtwdev, R_AX_PHYREG_SET, PHYREG_SET_ALL_CYCLE);

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_disable_bb_rf);

int rtw89_mac_partial_init(struct rtw89_dev *rtwdev, bool include_bb)
{
	int ret;

	ret = rtw89_mac_power_switch(rtwdev, true);
	if (ret) {
		rtw89_mac_power_switch(rtwdev, false);
		ret = rtw89_mac_power_switch(rtwdev, true);
		if (ret)
			return ret;
	}

	rtw89_mac_ctrl_hci_dma_trx(rtwdev, true);

	if (include_bb) {
		rtw89_chip_bb_preinit(rtwdev, RTW89_PHY_0);
		if (rtwdev->dbcc_en)
			rtw89_chip_bb_preinit(rtwdev, RTW89_PHY_1);
	}

	ret = rtw89_mac_dmac_pre_init(rtwdev);
	if (ret)
		return ret;

	if (rtwdev->hci.ops->mac_pre_init) {
		ret = rtwdev->hci.ops->mac_pre_init(rtwdev);
		if (ret)
			return ret;
	}

	ret = rtw89_fw_download(rtwdev, RTW89_FW_NORMAL, include_bb);
	if (ret)
		return ret;

	return 0;
}

int rtw89_mac_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	bool include_bb = !!chip->bbmcu_nr;
	int ret;

	ret = rtw89_mac_partial_init(rtwdev, include_bb);
	if (ret)
		goto fail;

	ret = rtw89_chip_enable_bb_rf(rtwdev);
	if (ret)
		goto fail;

	ret = mac->sys_init(rtwdev);
	if (ret)
		goto fail;

	ret = mac->trx_init(rtwdev);
	if (ret)
		goto fail;

	ret = rtw89_mac_feat_init(rtwdev);
	if (ret)
		goto fail;

	if (rtwdev->hci.ops->mac_post_init) {
		ret = rtwdev->hci.ops->mac_post_init(rtwdev);
		if (ret)
			goto fail;
	}

	rtw89_fw_send_all_early_h2c(rtwdev);
	rtw89_fw_h2c_set_ofld_cfg(rtwdev);

	return ret;
fail:
	rtw89_mac_power_switch(rtwdev, false);

	return ret;
}

static void rtw89_mac_dmac_tbl_init(struct rtw89_dev *rtwdev, u8 macid)
{
	u8 i;

	if (rtwdev->chip->chip_gen != RTW89_CHIP_AX)
		return;

	for (i = 0; i < 4; i++) {
		rtw89_write32(rtwdev, R_AX_FILTER_MODEL_ADDR,
			      DMAC_TBL_BASE_ADDR + (macid << 4) + (i << 2));
		rtw89_write32(rtwdev, R_AX_INDIR_ACCESS_ENTRY, 0);
	}
}

static void rtw89_mac_cmac_tbl_init(struct rtw89_dev *rtwdev, u8 macid)
{
	if (rtwdev->chip->chip_gen != RTW89_CHIP_AX)
		return;

	rtw89_write32(rtwdev, R_AX_FILTER_MODEL_ADDR,
		      CMAC_TBL_BASE_ADDR + macid * CCTL_INFO_SIZE);
	rtw89_write32(rtwdev, R_AX_INDIR_ACCESS_ENTRY, 0x4);
	rtw89_write32(rtwdev, R_AX_INDIR_ACCESS_ENTRY + 4, 0x400A0004);
	rtw89_write32(rtwdev, R_AX_INDIR_ACCESS_ENTRY + 8, 0);
	rtw89_write32(rtwdev, R_AX_INDIR_ACCESS_ENTRY + 12, 0);
	rtw89_write32(rtwdev, R_AX_INDIR_ACCESS_ENTRY + 16, 0);
	rtw89_write32(rtwdev, R_AX_INDIR_ACCESS_ENTRY + 20, 0xE43000B);
	rtw89_write32(rtwdev, R_AX_INDIR_ACCESS_ENTRY + 24, 0);
	rtw89_write32(rtwdev, R_AX_INDIR_ACCESS_ENTRY + 28, 0xB8109);
}

int rtw89_mac_set_macid_pause(struct rtw89_dev *rtwdev, u8 macid, bool pause)
{
	u8 sh =  FIELD_GET(GENMASK(4, 0), macid);
	u8 grp = macid >> 5;
	int ret;

	/* If this is called by change_interface() in the case of P2P, it could
	 * be power-off, so ignore this operation.
	 */
	if (test_bit(RTW89_FLAG_CHANGING_INTERFACE, rtwdev->flags) &&
	    !test_bit(RTW89_FLAG_POWERON, rtwdev->flags))
		return 0;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_0, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	rtw89_fw_h2c_macid_pause(rtwdev, sh, grp, pause);

	return 0;
}

static const struct rtw89_port_reg rtw89_port_base_ax = {
	.port_cfg = R_AX_PORT_CFG_P0,
	.tbtt_prohib = R_AX_TBTT_PROHIB_P0,
	.bcn_area = R_AX_BCN_AREA_P0,
	.bcn_early = R_AX_BCNERLYINT_CFG_P0,
	.tbtt_early = R_AX_TBTTERLYINT_CFG_P0,
	.tbtt_agg = R_AX_TBTT_AGG_P0,
	.bcn_space = R_AX_BCN_SPACE_CFG_P0,
	.bcn_forcetx = R_AX_BCN_FORCETX_P0,
	.bcn_err_cnt = R_AX_BCN_ERR_CNT_P0,
	.bcn_err_flag = R_AX_BCN_ERR_FLAG_P0,
	.dtim_ctrl = R_AX_DTIM_CTRL_P0,
	.tbtt_shift = R_AX_TBTT_SHIFT_P0,
	.bcn_cnt_tmr = R_AX_BCN_CNT_TMR_P0,
	.tsftr_l = R_AX_TSFTR_LOW_P0,
	.tsftr_h = R_AX_TSFTR_HIGH_P0,
	.md_tsft = R_AX_MD_TSFT_STMP_CTL,
	.bss_color = R_AX_PTCL_BSS_COLOR_0,
	.mbssid = R_AX_MBSSID_CTRL,
	.mbssid_drop = R_AX_MBSSID_DROP_0,
	.tsf_sync = R_AX_PORT0_TSF_SYNC,
	.ptcl_dbg = R_AX_PTCL_DBG,
	.ptcl_dbg_info = R_AX_PTCL_DBG_INFO,
	.bcn_drop_all = R_AX_BCN_DROP_ALL0,
	.hiq_win = {R_AX_P0MB_HGQ_WINDOW_CFG_0, R_AX_PORT_HGQ_WINDOW_CFG,
		    R_AX_PORT_HGQ_WINDOW_CFG + 1, R_AX_PORT_HGQ_WINDOW_CFG + 2,
		    R_AX_PORT_HGQ_WINDOW_CFG + 3},
};

static void rtw89_mac_check_packet_ctrl(struct rtw89_dev *rtwdev,
					struct rtw89_vif *rtwvif, u8 type)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	u8 mask = B_AX_PTCL_DBG_INFO_MASK_BY_PORT(rtwvif->port);
	u32 reg_info, reg_ctrl;
	u32 val;
	int ret;

	reg_info = rtw89_mac_reg_by_idx(rtwdev, p->ptcl_dbg_info, rtwvif->mac_idx);
	reg_ctrl = rtw89_mac_reg_by_idx(rtwdev, p->ptcl_dbg, rtwvif->mac_idx);

	rtw89_write32_mask(rtwdev, reg_ctrl, B_AX_PTCL_DBG_SEL_MASK, type);
	rtw89_write32_set(rtwdev, reg_ctrl, B_AX_PTCL_DBG_EN);
	fsleep(100);

	ret = read_poll_timeout(rtw89_read32_mask, val, val == 0, 1000, 100000,
				true, rtwdev, reg_info, mask);
	if (ret)
		rtw89_warn(rtwdev, "Polling beacon packet empty fail\n");
}

static void rtw89_mac_bcn_drop(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	rtw89_write32_set(rtwdev, p->bcn_drop_all, BIT(rtwvif->port));
	rtw89_write32_port_mask(rtwdev, rtwvif, p->tbtt_prohib, B_AX_TBTT_SETUP_MASK, 1);
	rtw89_write32_port_mask(rtwdev, rtwvif, p->bcn_area, B_AX_BCN_MSK_AREA_MASK, 0);
	rtw89_write32_port_mask(rtwdev, rtwvif, p->tbtt_prohib, B_AX_TBTT_HOLD_MASK, 0);
	rtw89_write32_port_mask(rtwdev, rtwvif, p->bcn_early, B_AX_BCNERLY_MASK, 2);
	rtw89_write16_port_mask(rtwdev, rtwvif, p->tbtt_early, B_AX_TBTTERLY_MASK, 1);
	rtw89_write32_port_mask(rtwdev, rtwvif, p->bcn_space, B_AX_BCN_SPACE_MASK, 1);
	rtw89_write32_port_set(rtwdev, rtwvif, p->port_cfg, B_AX_BCNTX_EN);

	rtw89_mac_check_packet_ctrl(rtwdev, rtwvif, AX_PTCL_DBG_BCNQ_NUM0);
	if (rtwvif->port == RTW89_PORT_0)
		rtw89_mac_check_packet_ctrl(rtwdev, rtwvif, AX_PTCL_DBG_BCNQ_NUM1);

	rtw89_write32_clr(rtwdev, p->bcn_drop_all, BIT(rtwvif->port));
	rtw89_write32_port_clr(rtwdev, rtwvif, p->port_cfg, B_AX_TBTT_PROHIB_EN);
	fsleep(2000);
}

#define BCN_INTERVAL 100
#define BCN_ERLY_DEF 160
#define BCN_SETUP_DEF 2
#define BCN_HOLD_DEF 200
#define BCN_MASK_DEF 0
#define TBTT_ERLY_DEF 5
#define BCN_SET_UNIT 32
#define BCN_ERLY_SET_DLY (10 * 2)

static void rtw89_mac_port_cfg_func_sw(struct rtw89_dev *rtwdev,
				       struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	const struct rtw89_chip_info *chip = rtwdev->chip;
	bool need_backup = false;
	u32 backup_val;

	if (!rtw89_read32_port_mask(rtwdev, rtwvif, p->port_cfg, B_AX_PORT_FUNC_EN))
		return;

	if (chip->chip_id == RTL8852A && rtwvif->port != RTW89_PORT_0) {
		need_backup = true;
		backup_val = rtw89_read32_port(rtwdev, rtwvif, p->tbtt_prohib);
	}

	if (rtwvif->net_type == RTW89_NET_TYPE_AP_MODE)
		rtw89_mac_bcn_drop(rtwdev, rtwvif);

	if (chip->chip_id == RTL8852A) {
		rtw89_write32_port_clr(rtwdev, rtwvif, p->tbtt_prohib, B_AX_TBTT_SETUP_MASK);
		rtw89_write32_port_mask(rtwdev, rtwvif, p->tbtt_prohib, B_AX_TBTT_HOLD_MASK, 1);
		rtw89_write16_port_clr(rtwdev, rtwvif, p->tbtt_early, B_AX_TBTTERLY_MASK);
		rtw89_write16_port_clr(rtwdev, rtwvif, p->bcn_early, B_AX_BCNERLY_MASK);
	}

	msleep(vif->bss_conf.beacon_int + 1);
	rtw89_write32_port_clr(rtwdev, rtwvif, p->port_cfg, B_AX_PORT_FUNC_EN |
							    B_AX_BRK_SETUP);
	rtw89_write32_port_set(rtwdev, rtwvif, p->port_cfg, B_AX_TSFTR_RST);
	rtw89_write32_port(rtwdev, rtwvif, p->bcn_cnt_tmr, 0);

	if (need_backup)
		rtw89_write32_port(rtwdev, rtwvif, p->tbtt_prohib, backup_val);
}

static void rtw89_mac_port_cfg_tx_rpt(struct rtw89_dev *rtwdev,
				      struct rtw89_vif *rtwvif, bool en)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	if (en)
		rtw89_write32_port_set(rtwdev, rtwvif, p->port_cfg, B_AX_TXBCN_RPT_EN);
	else
		rtw89_write32_port_clr(rtwdev, rtwvif, p->port_cfg, B_AX_TXBCN_RPT_EN);
}

static void rtw89_mac_port_cfg_rx_rpt(struct rtw89_dev *rtwdev,
				      struct rtw89_vif *rtwvif, bool en)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	if (en)
		rtw89_write32_port_set(rtwdev, rtwvif, p->port_cfg, B_AX_RXBCN_RPT_EN);
	else
		rtw89_write32_port_clr(rtwdev, rtwvif, p->port_cfg, B_AX_RXBCN_RPT_EN);
}

static void rtw89_mac_port_cfg_net_type(struct rtw89_dev *rtwdev,
					struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	rtw89_write32_port_mask(rtwdev, rtwvif, p->port_cfg, B_AX_NET_TYPE_MASK,
				rtwvif->net_type);
}

static void rtw89_mac_port_cfg_bcn_prct(struct rtw89_dev *rtwdev,
					struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	bool en = rtwvif->net_type != RTW89_NET_TYPE_NO_LINK;
	u32 bits = B_AX_TBTT_PROHIB_EN | B_AX_BRK_SETUP;

	if (en)
		rtw89_write32_port_set(rtwdev, rtwvif, p->port_cfg, bits);
	else
		rtw89_write32_port_clr(rtwdev, rtwvif, p->port_cfg, bits);
}

static void rtw89_mac_port_cfg_rx_sw(struct rtw89_dev *rtwdev,
				     struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	bool en = rtwvif->net_type == RTW89_NET_TYPE_INFRA ||
		  rtwvif->net_type == RTW89_NET_TYPE_AD_HOC;
	u32 bit = B_AX_RX_BSSID_FIT_EN;

	if (en)
		rtw89_write32_port_set(rtwdev, rtwvif, p->port_cfg, bit);
	else
		rtw89_write32_port_clr(rtwdev, rtwvif, p->port_cfg, bit);
}

void rtw89_mac_port_cfg_rx_sync(struct rtw89_dev *rtwdev,
				struct rtw89_vif *rtwvif, bool en)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	if (en)
		rtw89_write32_port_set(rtwdev, rtwvif, p->port_cfg, B_AX_TSF_UDT_EN);
	else
		rtw89_write32_port_clr(rtwdev, rtwvif, p->port_cfg, B_AX_TSF_UDT_EN);
}

static void rtw89_mac_port_cfg_rx_sync_by_nettype(struct rtw89_dev *rtwdev,
						  struct rtw89_vif *rtwvif)
{
	bool en = rtwvif->net_type == RTW89_NET_TYPE_INFRA ||
		  rtwvif->net_type == RTW89_NET_TYPE_AD_HOC;

	rtw89_mac_port_cfg_rx_sync(rtwdev, rtwvif, en);
}

static void rtw89_mac_port_cfg_tx_sw(struct rtw89_dev *rtwdev,
				     struct rtw89_vif *rtwvif, bool en)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	if (en)
		rtw89_write32_port_set(rtwdev, rtwvif, p->port_cfg, B_AX_BCNTX_EN);
	else
		rtw89_write32_port_clr(rtwdev, rtwvif, p->port_cfg, B_AX_BCNTX_EN);
}

static void rtw89_mac_port_cfg_tx_sw_by_nettype(struct rtw89_dev *rtwdev,
						struct rtw89_vif *rtwvif)
{
	bool en = rtwvif->net_type == RTW89_NET_TYPE_AP_MODE ||
		  rtwvif->net_type == RTW89_NET_TYPE_AD_HOC;

	rtw89_mac_port_cfg_tx_sw(rtwdev, rtwvif, en);
}

void rtw89_mac_enable_beacon_for_ap_vifs(struct rtw89_dev *rtwdev, bool en)
{
	struct rtw89_vif *rtwvif;

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		if (rtwvif->net_type == RTW89_NET_TYPE_AP_MODE)
			rtw89_mac_port_cfg_tx_sw(rtwdev, rtwvif, en);
}

static void rtw89_mac_port_cfg_bcn_intv(struct rtw89_dev *rtwdev,
					struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	u16 bcn_int = vif->bss_conf.beacon_int ? vif->bss_conf.beacon_int : BCN_INTERVAL;

	rtw89_write32_port_mask(rtwdev, rtwvif, p->bcn_space, B_AX_BCN_SPACE_MASK,
				bcn_int);
}

static void rtw89_mac_port_cfg_hiq_win(struct rtw89_dev *rtwdev,
				       struct rtw89_vif *rtwvif)
{
	u8 win = rtwvif->net_type == RTW89_NET_TYPE_AP_MODE ? 16 : 0;
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	u8 port = rtwvif->port;
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, p->hiq_win[port], rtwvif->mac_idx);
	rtw89_write8(rtwdev, reg, win);
}

static void rtw89_mac_port_cfg_hiq_dtim(struct rtw89_dev *rtwdev,
					struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	u32 addr;

	addr = rtw89_mac_reg_by_idx(rtwdev, p->md_tsft, rtwvif->mac_idx);
	rtw89_write8_set(rtwdev, addr, B_AX_UPD_HGQMD | B_AX_UPD_TIMIE);

	rtw89_write16_port_mask(rtwdev, rtwvif, p->dtim_ctrl, B_AX_DTIM_NUM_MASK,
				vif->bss_conf.dtim_period);
}

static void rtw89_mac_port_cfg_bcn_setup_time(struct rtw89_dev *rtwdev,
					      struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	rtw89_write32_port_mask(rtwdev, rtwvif, p->tbtt_prohib,
				B_AX_TBTT_SETUP_MASK, BCN_SETUP_DEF);
}

static void rtw89_mac_port_cfg_bcn_hold_time(struct rtw89_dev *rtwdev,
					     struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	rtw89_write32_port_mask(rtwdev, rtwvif, p->tbtt_prohib,
				B_AX_TBTT_HOLD_MASK, BCN_HOLD_DEF);
}

static void rtw89_mac_port_cfg_bcn_mask_area(struct rtw89_dev *rtwdev,
					     struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	rtw89_write32_port_mask(rtwdev, rtwvif, p->bcn_area,
				B_AX_BCN_MSK_AREA_MASK, BCN_MASK_DEF);
}

static void rtw89_mac_port_cfg_tbtt_early(struct rtw89_dev *rtwdev,
					  struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	rtw89_write16_port_mask(rtwdev, rtwvif, p->tbtt_early,
				B_AX_TBTTERLY_MASK, TBTT_ERLY_DEF);
}

static void rtw89_mac_port_cfg_bss_color(struct rtw89_dev *rtwdev,
					 struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	static const u32 masks[RTW89_PORT_NUM] = {
		B_AX_BSS_COLOB_AX_PORT_0_MASK, B_AX_BSS_COLOB_AX_PORT_1_MASK,
		B_AX_BSS_COLOB_AX_PORT_2_MASK, B_AX_BSS_COLOB_AX_PORT_3_MASK,
		B_AX_BSS_COLOB_AX_PORT_4_MASK,
	};
	u8 port = rtwvif->port;
	u32 reg_base;
	u32 reg;
	u8 bss_color;

	bss_color = vif->bss_conf.he_bss_color.color;
	reg_base = port >= 4 ? p->bss_color + 4 : p->bss_color;
	reg = rtw89_mac_reg_by_idx(rtwdev, reg_base, rtwvif->mac_idx);
	rtw89_write32_mask(rtwdev, reg, masks[port], bss_color);
}

static void rtw89_mac_port_cfg_mbssid(struct rtw89_dev *rtwdev,
				      struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	u8 port = rtwvif->port;
	u32 reg;

	if (rtwvif->net_type == RTW89_NET_TYPE_AP_MODE)
		return;

	if (port == 0) {
		reg = rtw89_mac_reg_by_idx(rtwdev, p->mbssid, rtwvif->mac_idx);
		rtw89_write32_clr(rtwdev, reg, B_AX_P0MB_ALL_MASK);
	}
}

static void rtw89_mac_port_cfg_hiq_drop(struct rtw89_dev *rtwdev,
					struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	u8 port = rtwvif->port;
	u32 reg;
	u32 val;

	reg = rtw89_mac_reg_by_idx(rtwdev, p->mbssid_drop, rtwvif->mac_idx);
	val = rtw89_read32(rtwdev, reg);
	val &= ~FIELD_PREP(B_AX_PORT_DROP_4_0_MASK, BIT(port));
	if (port == 0)
		val &= ~BIT(0);
	rtw89_write32(rtwdev, reg, val);
}

static void rtw89_mac_port_cfg_func_en(struct rtw89_dev *rtwdev,
				       struct rtw89_vif *rtwvif, bool enable)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	if (enable)
		rtw89_write32_port_set(rtwdev, rtwvif, p->port_cfg,
				       B_AX_PORT_FUNC_EN);
	else
		rtw89_write32_port_clr(rtwdev, rtwvif, p->port_cfg,
				       B_AX_PORT_FUNC_EN);
}

static void rtw89_mac_port_cfg_bcn_early(struct rtw89_dev *rtwdev,
					 struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;

	rtw89_write32_port_mask(rtwdev, rtwvif, p->bcn_early, B_AX_BCNERLY_MASK,
				BCN_ERLY_DEF);
}

static void rtw89_mac_port_cfg_tbtt_shift(struct rtw89_dev *rtwdev,
					  struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	u16 val;

	if (rtwdev->chip->chip_id != RTL8852C)
		return;

	if (rtwvif->wifi_role != RTW89_WIFI_ROLE_P2P_CLIENT &&
	    rtwvif->wifi_role != RTW89_WIFI_ROLE_STATION)
		return;

	val = FIELD_PREP(B_AX_TBTT_SHIFT_OFST_MAG, 1) |
			 B_AX_TBTT_SHIFT_OFST_SIGN;

	rtw89_write16_port_mask(rtwdev, rtwvif, p->tbtt_shift,
				B_AX_TBTT_SHIFT_OFST_MASK, val);
}

void rtw89_mac_port_tsf_sync(struct rtw89_dev *rtwdev,
			     struct rtw89_vif *rtwvif,
			     struct rtw89_vif *rtwvif_src,
			     u16 offset_tu)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	u32 val, reg;

	val = RTW89_PORT_OFFSET_TU_TO_32US(offset_tu);
	reg = rtw89_mac_reg_by_idx(rtwdev, p->tsf_sync + rtwvif->port * 4,
				   rtwvif->mac_idx);

	rtw89_write32_mask(rtwdev, reg, B_AX_SYNC_PORT_SRC, rtwvif_src->port);
	rtw89_write32_mask(rtwdev, reg, B_AX_SYNC_PORT_OFFSET_VAL, val);
	rtw89_write32_set(rtwdev, reg, B_AX_SYNC_NOW);
}

static void rtw89_mac_port_tsf_sync_rand(struct rtw89_dev *rtwdev,
					 struct rtw89_vif *rtwvif,
					 struct rtw89_vif *rtwvif_src,
					 u8 offset, int *n_offset)
{
	if (rtwvif->net_type != RTW89_NET_TYPE_AP_MODE || rtwvif == rtwvif_src)
		return;

	/* adjust offset randomly to avoid beacon conflict */
	offset = offset - offset / 4 + get_random_u32() % (offset / 2);
	rtw89_mac_port_tsf_sync(rtwdev, rtwvif, rtwvif_src,
				(*n_offset) * offset);

	(*n_offset)++;
}

static void rtw89_mac_port_tsf_resync_all(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif *src = NULL, *tmp;
	u8 offset = 100, vif_aps = 0;
	int n_offset = 1;

	rtw89_for_each_rtwvif(rtwdev, tmp) {
		if (!src || tmp->net_type == RTW89_NET_TYPE_INFRA)
			src = tmp;
		if (tmp->net_type == RTW89_NET_TYPE_AP_MODE)
			vif_aps++;
	}

	if (vif_aps == 0)
		return;

	offset /= (vif_aps + 1);

	rtw89_for_each_rtwvif(rtwdev, tmp)
		rtw89_mac_port_tsf_sync_rand(rtwdev, tmp, src, offset, &n_offset);
}

int rtw89_mac_vif_init(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	int ret;

	ret = rtw89_mac_port_update(rtwdev, rtwvif);
	if (ret)
		return ret;

	rtw89_mac_dmac_tbl_init(rtwdev, rtwvif->mac_id);
	rtw89_mac_cmac_tbl_init(rtwdev, rtwvif->mac_id);

	ret = rtw89_mac_set_macid_pause(rtwdev, rtwvif->mac_id, false);
	if (ret)
		return ret;

	ret = rtw89_fw_h2c_role_maintain(rtwdev, rtwvif, NULL, RTW89_ROLE_CREATE);
	if (ret)
		return ret;

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif, NULL, true);
	if (ret)
		return ret;

	ret = rtw89_cam_init(rtwdev, rtwvif);
	if (ret)
		return ret;

	ret = rtw89_fw_h2c_cam(rtwdev, rtwvif, NULL, NULL);
	if (ret)
		return ret;

	ret = rtw89_chip_h2c_default_cmac_tbl(rtwdev, rtwvif, NULL);
	if (ret)
		return ret;

	ret = rtw89_chip_h2c_default_dmac_tbl(rtwdev, rtwvif, NULL);
	if (ret)
		return ret;

	return 0;
}

int rtw89_mac_vif_deinit(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	int ret;

	ret = rtw89_fw_h2c_role_maintain(rtwdev, rtwvif, NULL, RTW89_ROLE_REMOVE);
	if (ret)
		return ret;

	rtw89_cam_deinit(rtwdev, rtwvif);

	ret = rtw89_fw_h2c_cam(rtwdev, rtwvif, NULL, NULL);
	if (ret)
		return ret;

	return 0;
}

int rtw89_mac_port_update(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	u8 port = rtwvif->port;

	if (port >= RTW89_PORT_NUM)
		return -EINVAL;

	rtw89_mac_port_cfg_func_sw(rtwdev, rtwvif);
	rtw89_mac_port_cfg_tx_rpt(rtwdev, rtwvif, false);
	rtw89_mac_port_cfg_rx_rpt(rtwdev, rtwvif, false);
	rtw89_mac_port_cfg_net_type(rtwdev, rtwvif);
	rtw89_mac_port_cfg_bcn_prct(rtwdev, rtwvif);
	rtw89_mac_port_cfg_rx_sw(rtwdev, rtwvif);
	rtw89_mac_port_cfg_rx_sync_by_nettype(rtwdev, rtwvif);
	rtw89_mac_port_cfg_tx_sw_by_nettype(rtwdev, rtwvif);
	rtw89_mac_port_cfg_bcn_intv(rtwdev, rtwvif);
	rtw89_mac_port_cfg_hiq_win(rtwdev, rtwvif);
	rtw89_mac_port_cfg_hiq_dtim(rtwdev, rtwvif);
	rtw89_mac_port_cfg_hiq_drop(rtwdev, rtwvif);
	rtw89_mac_port_cfg_bcn_setup_time(rtwdev, rtwvif);
	rtw89_mac_port_cfg_bcn_hold_time(rtwdev, rtwvif);
	rtw89_mac_port_cfg_bcn_mask_area(rtwdev, rtwvif);
	rtw89_mac_port_cfg_tbtt_early(rtwdev, rtwvif);
	rtw89_mac_port_cfg_tbtt_shift(rtwdev, rtwvif);
	rtw89_mac_port_cfg_bss_color(rtwdev, rtwvif);
	rtw89_mac_port_cfg_mbssid(rtwdev, rtwvif);
	rtw89_mac_port_cfg_func_en(rtwdev, rtwvif, true);
	rtw89_mac_port_tsf_resync_all(rtwdev);
	fsleep(BCN_ERLY_SET_DLY);
	rtw89_mac_port_cfg_bcn_early(rtwdev, rtwvif);

	return 0;
}

int rtw89_mac_port_get_tsf(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			   u64 *tsf)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_port_reg *p = mac->port_base;
	u32 tsf_low, tsf_high;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, rtwvif->mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	tsf_low = rtw89_read32_port(rtwdev, rtwvif, p->tsftr_l);
	tsf_high = rtw89_read32_port(rtwdev, rtwvif, p->tsftr_h);
	*tsf = (u64)tsf_high << 32 | tsf_low;

	return 0;
}

static void rtw89_mac_check_he_obss_narrow_bw_ru_iter(struct wiphy *wiphy,
						      struct cfg80211_bss *bss,
						      void *data)
{
	const struct cfg80211_bss_ies *ies;
	const struct element *elem;
	bool *tolerated = data;

	rcu_read_lock();
	ies = rcu_dereference(bss->ies);
	elem = cfg80211_find_elem(WLAN_EID_EXT_CAPABILITY, ies->data,
				  ies->len);

	if (!elem || elem->datalen < 10 ||
	    !(elem->data[10] & WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT))
		*tolerated = false;
	rcu_read_unlock();
}

void rtw89_mac_set_he_obss_narrow_bw_ru(struct rtw89_dev *rtwdev,
					struct ieee80211_vif *vif)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct ieee80211_hw *hw = rtwdev->hw;
	bool tolerated = true;
	u32 reg;

	if (!vif->bss_conf.he_support || vif->type != NL80211_IFTYPE_STATION)
		return;

	if (!(vif->bss_conf.chanreq.oper.chan->flags & IEEE80211_CHAN_RADAR))
		return;

	cfg80211_bss_iter(hw->wiphy, &vif->bss_conf.chanreq.oper,
			  rtw89_mac_check_he_obss_narrow_bw_ru_iter,
			  &tolerated);

	reg = rtw89_mac_reg_by_idx(rtwdev, mac->narrow_bw_ru_dis.addr,
				   rtwvif->mac_idx);
	if (tolerated)
		rtw89_write32_clr(rtwdev, reg, mac->narrow_bw_ru_dis.mask);
	else
		rtw89_write32_set(rtwdev, reg, mac->narrow_bw_ru_dis.mask);
}

void rtw89_mac_stop_ap(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	rtw89_mac_port_cfg_func_sw(rtwdev, rtwvif);
}

int rtw89_mac_add_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	int ret;

	rtwvif->mac_id = rtw89_acquire_mac_id(rtwdev);
	if (rtwvif->mac_id == RTW89_MAX_MAC_ID_NUM)
		return -ENOSPC;

	ret = rtw89_mac_vif_init(rtwdev, rtwvif);
	if (ret)
		goto release_mac_id;

	return 0;

release_mac_id:
	rtw89_release_mac_id(rtwdev, rtwvif->mac_id);

	return ret;
}

int rtw89_mac_remove_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	int ret;

	ret = rtw89_mac_vif_deinit(rtwdev, rtwvif);
	rtw89_release_mac_id(rtwdev, rtwvif->mac_id);

	return ret;
}

static void
rtw89_mac_c2h_macid_pause(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
}

static bool rtw89_is_op_chan(struct rtw89_dev *rtwdev, u8 band, u8 channel)
{
	const struct rtw89_chan *op = &rtwdev->scan_info.op_chan;

	return band == op->band_type && channel == op->primary_channel;
}

static void
rtw89_mac_c2h_scanofld_rsp(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			   u32 len)
{
	const struct rtw89_c2h_scanofld *c2h =
		(const struct rtw89_c2h_scanofld *)skb->data;
	struct ieee80211_vif *vif = rtwdev->scan_info.scanning_vif;
	struct rtw89_vif *rtwvif = vif_to_rtwvif_safe(vif);
	struct rtw89_chan new;
	u8 reason, status, tx_fail, band, actual_period, expect_period;
	u32 last_chan = rtwdev->scan_info.last_chan_idx, report_tsf;
	u8 mac_idx, sw_def, fw_def;
	u16 chan;
	int ret;

	if (!rtwvif)
		return;

	tx_fail = le32_get_bits(c2h->w5, RTW89_C2H_SCANOFLD_W5_TX_FAIL);
	status = le32_get_bits(c2h->w2, RTW89_C2H_SCANOFLD_W2_STATUS);
	chan = le32_get_bits(c2h->w2, RTW89_C2H_SCANOFLD_W2_PRI_CH);
	reason = le32_get_bits(c2h->w2, RTW89_C2H_SCANOFLD_W2_RSN);
	band = le32_get_bits(c2h->w5, RTW89_C2H_SCANOFLD_W5_BAND);
	actual_period = le32_get_bits(c2h->w2, RTW89_C2H_SCANOFLD_W2_PERIOD);
	mac_idx = le32_get_bits(c2h->w5, RTW89_C2H_SCANOFLD_W5_MAC_IDX);


	if (!(rtwdev->chip->support_bands & BIT(NL80211_BAND_6GHZ)))
		band = chan > 14 ? RTW89_BAND_5G : RTW89_BAND_2G;

	rtw89_debug(rtwdev, RTW89_DBG_HW_SCAN,
		    "mac_idx[%d] band: %d, chan: %d, reason: %d, status: %d, tx_fail: %d, actual: %d\n",
		    mac_idx, band, chan, reason, status, tx_fail, actual_period);

	if (rtwdev->chip->chip_gen == RTW89_CHIP_BE) {
		sw_def = le32_get_bits(c2h->w6, RTW89_C2H_SCANOFLD_W6_SW_DEF);
		expect_period = le32_get_bits(c2h->w6, RTW89_C2H_SCANOFLD_W6_EXPECT_PERIOD);
		fw_def = le32_get_bits(c2h->w6, RTW89_C2H_SCANOFLD_W6_FW_DEF);
		report_tsf = le32_get_bits(c2h->w7, RTW89_C2H_SCANOFLD_W7_REPORT_TSF);

		rtw89_debug(rtwdev, RTW89_DBG_HW_SCAN,
			    "sw_def: %d, fw_def: %d, tsf: %x, expect: %d\n",
			    sw_def, fw_def, report_tsf, expect_period);
	}

	switch (reason) {
	case RTW89_SCAN_LEAVE_OP_NOTIFY:
	case RTW89_SCAN_LEAVE_CH_NOTIFY:
		if (rtw89_is_op_chan(rtwdev, band, chan)) {
			rtw89_mac_enable_beacon_for_ap_vifs(rtwdev, false);
			ieee80211_stop_queues(rtwdev->hw);
		}
		return;
	case RTW89_SCAN_END_SCAN_NOTIFY:
		if (rtwdev->scan_info.abort)
			return;

		if (rtwvif && rtwvif->scan_req &&
		    last_chan < rtwvif->scan_req->n_channels) {
			ret = rtw89_hw_scan_offload(rtwdev, vif, true);
			if (ret) {
				rtw89_hw_scan_abort(rtwdev, vif);
				rtw89_warn(rtwdev, "HW scan failed: %d\n", ret);
			}
		} else {
			rtw89_hw_scan_complete(rtwdev, vif, false);
		}
		break;
	case RTW89_SCAN_ENTER_OP_NOTIFY:
	case RTW89_SCAN_ENTER_CH_NOTIFY:
		if (rtw89_is_op_chan(rtwdev, band, chan)) {
			rtw89_assign_entity_chan(rtwdev, rtwvif->sub_entity_idx,
						 &rtwdev->scan_info.op_chan);
			rtw89_mac_enable_beacon_for_ap_vifs(rtwdev, true);
			ieee80211_wake_queues(rtwdev->hw);
		} else {
			rtw89_chan_create(&new, chan, chan, band,
					  RTW89_CHANNEL_WIDTH_20);
			rtw89_assign_entity_chan(rtwdev, rtwvif->sub_entity_idx,
						 &new);
		}
		break;
	default:
		return;
	}
}

static void
rtw89_mac_bcn_fltr_rpt(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		       struct sk_buff *skb)
{
	struct ieee80211_vif *vif = rtwvif_to_vif_safe(rtwvif);
	enum nl80211_cqm_rssi_threshold_event nl_event;
	const struct rtw89_c2h_mac_bcnfltr_rpt *c2h =
		(const struct rtw89_c2h_mac_bcnfltr_rpt *)skb->data;
	u8 type, event, mac_id;
	s8 sig;

	type = le32_get_bits(c2h->w2, RTW89_C2H_MAC_BCNFLTR_RPT_W2_TYPE);
	sig = le32_get_bits(c2h->w2, RTW89_C2H_MAC_BCNFLTR_RPT_W2_MA) - MAX_RSSI;
	event = le32_get_bits(c2h->w2, RTW89_C2H_MAC_BCNFLTR_RPT_W2_EVENT);
	mac_id = le32_get_bits(c2h->w2, RTW89_C2H_MAC_BCNFLTR_RPT_W2_MACID);

	if (mac_id != rtwvif->mac_id)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_FW,
		    "C2H bcnfltr rpt macid: %d, type: %d, ma: %d, event: %d\n",
		    mac_id, type, sig, event);

	switch (type) {
	case RTW89_BCN_FLTR_BEACON_LOSS:
		if (!rtwdev->scanning && !rtwvif->offchan)
			ieee80211_connection_loss(vif);
		else
			rtw89_fw_h2c_set_bcn_fltr_cfg(rtwdev, vif, true);
		return;
	case RTW89_BCN_FLTR_NOTIFY:
		nl_event = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
		break;
	case RTW89_BCN_FLTR_RSSI:
		if (event == RTW89_BCN_FLTR_RSSI_LOW)
			nl_event = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
		else if (event == RTW89_BCN_FLTR_RSSI_HIGH)
			nl_event = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
		else
			return;
		break;
	default:
		return;
	}

	ieee80211_cqm_rssi_notify(vif, nl_event, sig, GFP_KERNEL);
}

static void
rtw89_mac_c2h_bcn_fltr_rpt(struct rtw89_dev *rtwdev, struct sk_buff *c2h,
			   u32 len)
{
	struct rtw89_vif *rtwvif;

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_mac_bcn_fltr_rpt(rtwdev, rtwvif, c2h);
}

static void
rtw89_mac_c2h_rec_ack(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	/* N.B. This will run in interrupt context. */

	rtw89_debug(rtwdev, RTW89_DBG_FW,
		    "C2H rev ack recv, cat: %d, class: %d, func: %d, seq : %d\n",
		    RTW89_GET_MAC_C2H_REV_ACK_CAT(c2h->data),
		    RTW89_GET_MAC_C2H_REV_ACK_CLASS(c2h->data),
		    RTW89_GET_MAC_C2H_REV_ACK_FUNC(c2h->data),
		    RTW89_GET_MAC_C2H_REV_ACK_H2C_SEQ(c2h->data));
}

static void
rtw89_mac_c2h_done_ack(struct rtw89_dev *rtwdev, struct sk_buff *skb_c2h, u32 len)
{
	/* N.B. This will run in interrupt context. */
	struct rtw89_wait_info *fw_ofld_wait = &rtwdev->mac.fw_ofld_wait;
	const struct rtw89_c2h_done_ack *c2h =
		(const struct rtw89_c2h_done_ack *)skb_c2h->data;
	u8 h2c_cat = le32_get_bits(c2h->w2, RTW89_C2H_DONE_ACK_W2_CAT);
	u8 h2c_class = le32_get_bits(c2h->w2, RTW89_C2H_DONE_ACK_W2_CLASS);
	u8 h2c_func = le32_get_bits(c2h->w2, RTW89_C2H_DONE_ACK_W2_FUNC);
	u8 h2c_return = le32_get_bits(c2h->w2, RTW89_C2H_DONE_ACK_W2_H2C_RETURN);
	u8 h2c_seq = le32_get_bits(c2h->w2, RTW89_C2H_DONE_ACK_W2_H2C_SEQ);
	struct rtw89_completion_data data = {};
	unsigned int cond;

	rtw89_debug(rtwdev, RTW89_DBG_FW,
		    "C2H done ack recv, cat: %d, class: %d, func: %d, ret: %d, seq : %d\n",
		    h2c_cat, h2c_class, h2c_func, h2c_return, h2c_seq);

	if (h2c_cat != H2C_CAT_MAC)
		return;

	switch (h2c_class) {
	default:
		return;
	case H2C_CL_MAC_FW_OFLD:
		switch (h2c_func) {
		default:
			return;
		case H2C_FUNC_ADD_SCANOFLD_CH:
			cond = RTW89_SCANOFLD_WAIT_COND_ADD_CH;
			break;
		case H2C_FUNC_SCANOFLD:
			cond = RTW89_SCANOFLD_WAIT_COND_START;
			break;
		case H2C_FUNC_SCANOFLD_BE:
			cond = RTW89_SCANOFLD_BE_WAIT_COND_START;
			break;
		}

		data.err = !!h2c_return;
		rtw89_complete_cond(fw_ofld_wait, cond, &data);
		return;
	}
}

static void
rtw89_mac_c2h_log(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	rtw89_fw_log_dump(rtwdev, c2h->data, len);
}

static void
rtw89_mac_c2h_bcn_cnt(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
}

static void
rtw89_mac_c2h_pkt_ofld_rsp(struct rtw89_dev *rtwdev, struct sk_buff *skb_c2h,
			   u32 len)
{
	struct rtw89_wait_info *wait = &rtwdev->mac.fw_ofld_wait;
	const struct rtw89_c2h_pkt_ofld_rsp *c2h =
		(const struct rtw89_c2h_pkt_ofld_rsp *)skb_c2h->data;
	u16 pkt_len = le32_get_bits(c2h->w2, RTW89_C2H_PKT_OFLD_RSP_W2_PTK_LEN);
	u8 pkt_id = le32_get_bits(c2h->w2, RTW89_C2H_PKT_OFLD_RSP_W2_PTK_ID);
	u8 pkt_op = le32_get_bits(c2h->w2, RTW89_C2H_PKT_OFLD_RSP_W2_PTK_OP);
	struct rtw89_completion_data data = {};
	unsigned int cond;

	rtw89_debug(rtwdev, RTW89_DBG_FW, "pkt ofld rsp: id %d op %d len %d\n",
		    pkt_id, pkt_op, pkt_len);

	data.err = !pkt_len;
	cond = RTW89_FW_OFLD_WAIT_COND_PKT_OFLD(pkt_id, pkt_op);

	rtw89_complete_cond(wait, cond, &data);
}

static void
rtw89_mac_c2h_tsf32_toggle_rpt(struct rtw89_dev *rtwdev, struct sk_buff *c2h,
			       u32 len)
{
	rtw89_queue_chanctx_change(rtwdev, RTW89_CHANCTX_TSF32_TOGGLE_CHANGE);
}

static void
rtw89_mac_c2h_mcc_rcv_ack(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	u8 group = RTW89_GET_MAC_C2H_MCC_RCV_ACK_GROUP(c2h->data);
	u8 func = RTW89_GET_MAC_C2H_MCC_RCV_ACK_H2C_FUNC(c2h->data);

	switch (func) {
	case H2C_FUNC_ADD_MCC:
	case H2C_FUNC_START_MCC:
	case H2C_FUNC_STOP_MCC:
	case H2C_FUNC_DEL_MCC_GROUP:
	case H2C_FUNC_RESET_MCC_GROUP:
	case H2C_FUNC_MCC_REQ_TSF:
	case H2C_FUNC_MCC_MACID_BITMAP:
	case H2C_FUNC_MCC_SYNC:
	case H2C_FUNC_MCC_SET_DURATION:
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "invalid MCC C2H RCV ACK: func %d\n", func);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC C2H RCV ACK: group %d, func %d\n", group, func);
}

static void
rtw89_mac_c2h_mcc_req_ack(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	u8 group = RTW89_GET_MAC_C2H_MCC_REQ_ACK_GROUP(c2h->data);
	u8 func = RTW89_GET_MAC_C2H_MCC_REQ_ACK_H2C_FUNC(c2h->data);
	u8 retcode = RTW89_GET_MAC_C2H_MCC_REQ_ACK_H2C_RETURN(c2h->data);
	struct rtw89_completion_data data = {};
	unsigned int cond;
	bool next = false;

	switch (func) {
	case H2C_FUNC_MCC_REQ_TSF:
		next = true;
		break;
	case H2C_FUNC_MCC_MACID_BITMAP:
	case H2C_FUNC_MCC_SYNC:
	case H2C_FUNC_MCC_SET_DURATION:
		break;
	case H2C_FUNC_ADD_MCC:
	case H2C_FUNC_START_MCC:
	case H2C_FUNC_STOP_MCC:
	case H2C_FUNC_DEL_MCC_GROUP:
	case H2C_FUNC_RESET_MCC_GROUP:
	default:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "invalid MCC C2H REQ ACK: func %d\n", func);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC C2H REQ ACK: group %d, func %d, return code %d\n",
		    group, func, retcode);

	if (!retcode && next)
		return;

	data.err = !!retcode;
	cond = RTW89_MCC_WAIT_COND(group, func);
	rtw89_complete_cond(&rtwdev->mcc.wait, cond, &data);
}

static void
rtw89_mac_c2h_mcc_tsf_rpt(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	u8 group = RTW89_GET_MAC_C2H_MCC_TSF_RPT_GROUP(c2h->data);
	struct rtw89_completion_data data = {};
	struct rtw89_mac_mcc_tsf_rpt *rpt;
	unsigned int cond;

	rpt = (struct rtw89_mac_mcc_tsf_rpt *)data.buf;
	rpt->macid_x = RTW89_GET_MAC_C2H_MCC_TSF_RPT_MACID_X(c2h->data);
	rpt->macid_y = RTW89_GET_MAC_C2H_MCC_TSF_RPT_MACID_Y(c2h->data);
	rpt->tsf_x_low = RTW89_GET_MAC_C2H_MCC_TSF_RPT_TSF_LOW_X(c2h->data);
	rpt->tsf_x_high = RTW89_GET_MAC_C2H_MCC_TSF_RPT_TSF_HIGH_X(c2h->data);
	rpt->tsf_y_low = RTW89_GET_MAC_C2H_MCC_TSF_RPT_TSF_LOW_Y(c2h->data);
	rpt->tsf_y_high = RTW89_GET_MAC_C2H_MCC_TSF_RPT_TSF_HIGH_Y(c2h->data);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC C2H TSF RPT: macid %d> %llu, macid %d> %llu\n",
		    rpt->macid_x, (u64)rpt->tsf_x_high << 32 | rpt->tsf_x_low,
		    rpt->macid_y, (u64)rpt->tsf_y_high << 32 | rpt->tsf_y_low);

	cond = RTW89_MCC_WAIT_COND(group, H2C_FUNC_MCC_REQ_TSF);
	rtw89_complete_cond(&rtwdev->mcc.wait, cond, &data);
}

static void
rtw89_mac_c2h_mcc_status_rpt(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	u8 group = RTW89_GET_MAC_C2H_MCC_STATUS_RPT_GROUP(c2h->data);
	u8 macid = RTW89_GET_MAC_C2H_MCC_STATUS_RPT_MACID(c2h->data);
	u8 status = RTW89_GET_MAC_C2H_MCC_STATUS_RPT_STATUS(c2h->data);
	u32 tsf_low = RTW89_GET_MAC_C2H_MCC_STATUS_RPT_TSF_LOW(c2h->data);
	u32 tsf_high = RTW89_GET_MAC_C2H_MCC_STATUS_RPT_TSF_HIGH(c2h->data);
	struct rtw89_completion_data data = {};
	unsigned int cond;
	bool rsp = true;
	bool err;
	u8 func;

	switch (status) {
	case RTW89_MAC_MCC_ADD_ROLE_OK:
	case RTW89_MAC_MCC_ADD_ROLE_FAIL:
		func = H2C_FUNC_ADD_MCC;
		err = status == RTW89_MAC_MCC_ADD_ROLE_FAIL;
		break;
	case RTW89_MAC_MCC_START_GROUP_OK:
	case RTW89_MAC_MCC_START_GROUP_FAIL:
		func = H2C_FUNC_START_MCC;
		err = status == RTW89_MAC_MCC_START_GROUP_FAIL;
		break;
	case RTW89_MAC_MCC_STOP_GROUP_OK:
	case RTW89_MAC_MCC_STOP_GROUP_FAIL:
		func = H2C_FUNC_STOP_MCC;
		err = status == RTW89_MAC_MCC_STOP_GROUP_FAIL;
		break;
	case RTW89_MAC_MCC_DEL_GROUP_OK:
	case RTW89_MAC_MCC_DEL_GROUP_FAIL:
		func = H2C_FUNC_DEL_MCC_GROUP;
		err = status == RTW89_MAC_MCC_DEL_GROUP_FAIL;
		break;
	case RTW89_MAC_MCC_RESET_GROUP_OK:
	case RTW89_MAC_MCC_RESET_GROUP_FAIL:
		func = H2C_FUNC_RESET_MCC_GROUP;
		err = status == RTW89_MAC_MCC_RESET_GROUP_FAIL;
		break;
	case RTW89_MAC_MCC_SWITCH_CH_OK:
	case RTW89_MAC_MCC_SWITCH_CH_FAIL:
	case RTW89_MAC_MCC_TXNULL0_OK:
	case RTW89_MAC_MCC_TXNULL0_FAIL:
	case RTW89_MAC_MCC_TXNULL1_OK:
	case RTW89_MAC_MCC_TXNULL1_FAIL:
	case RTW89_MAC_MCC_SWITCH_EARLY:
	case RTW89_MAC_MCC_TBTT:
	case RTW89_MAC_MCC_DURATION_START:
	case RTW89_MAC_MCC_DURATION_END:
		rsp = false;
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "invalid MCC C2H STS RPT: status %d\n", status);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC C2H STS RPT: group %d, macid %d, status %d, tsf %llu\n",
		     group, macid, status, (u64)tsf_high << 32 | tsf_low);

	if (!rsp)
		return;

	data.err = err;
	cond = RTW89_MCC_WAIT_COND(group, func);
	rtw89_complete_cond(&rtwdev->mcc.wait, cond, &data);
}

static void
rtw89_mac_c2h_mrc_tsf_rpt(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	const struct rtw89_c2h_mrc_tsf_rpt *c2h_rpt;
	struct rtw89_completion_data data = {};
	struct rtw89_mac_mrc_tsf_rpt *rpt;
	unsigned int i;

	c2h_rpt = (const struct rtw89_c2h_mrc_tsf_rpt *)c2h->data;
	rpt = (struct rtw89_mac_mrc_tsf_rpt *)data.buf;
	rpt->num = min_t(u8, RTW89_MAC_MRC_MAX_REQ_TSF_NUM,
			 le32_get_bits(c2h_rpt->w2,
				       RTW89_C2H_MRC_TSF_RPT_W2_REQ_TSF_NUM));

	for (i = 0; i < rpt->num; i++) {
		u32 tsf_high = le32_to_cpu(c2h_rpt->infos[i].tsf_high);
		u32 tsf_low = le32_to_cpu(c2h_rpt->infos[i].tsf_low);

		rpt->tsfs[i] = (u64)tsf_high << 32 | tsf_low;

		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H TSF RPT: index %u> %llu\n",
			    i, rpt->tsfs[i]);
	}

	rtw89_complete_cond(wait, RTW89_MRC_WAIT_COND_REQ_TSF, &data);
}

static void
rtw89_mac_c2h_wow_aoac_rpt(struct rtw89_dev *rtwdev, struct sk_buff *skb, u32 len)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_aoac_report *aoac_rpt = &rtw_wow->aoac_rpt;
	struct rtw89_wait_info *wait = &rtw_wow->wait;
	const struct rtw89_c2h_wow_aoac_report *c2h =
		(const struct rtw89_c2h_wow_aoac_report *)skb->data;
	struct rtw89_completion_data data = {};

	aoac_rpt->rpt_ver = c2h->rpt_ver;
	aoac_rpt->sec_type = c2h->sec_type;
	aoac_rpt->key_idx = c2h->key_idx;
	aoac_rpt->pattern_idx = c2h->pattern_idx;
	aoac_rpt->rekey_ok = u8_get_bits(c2h->rekey_ok,
					 RTW89_C2H_WOW_AOAC_RPT_REKEY_IDX);
	memcpy(aoac_rpt->ptk_tx_iv, c2h->ptk_tx_iv, sizeof(aoac_rpt->ptk_tx_iv));
	memcpy(aoac_rpt->eapol_key_replay_count, c2h->eapol_key_replay_count,
	       sizeof(aoac_rpt->eapol_key_replay_count));
	memcpy(aoac_rpt->gtk, c2h->gtk, sizeof(aoac_rpt->gtk));
	memcpy(aoac_rpt->ptk_rx_iv, c2h->ptk_rx_iv, sizeof(aoac_rpt->ptk_rx_iv));
	memcpy(aoac_rpt->gtk_rx_iv, c2h->gtk_rx_iv, sizeof(aoac_rpt->gtk_rx_iv));
	aoac_rpt->igtk_key_id = le64_to_cpu(c2h->igtk_key_id);
	aoac_rpt->igtk_ipn = le64_to_cpu(c2h->igtk_ipn);
	memcpy(aoac_rpt->igtk, c2h->igtk, sizeof(aoac_rpt->igtk));

	rtw89_complete_cond(wait, RTW89_WOW_WAIT_COND_AOAC, &data);
}

static void
rtw89_mac_c2h_mrc_status_rpt(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	struct rtw89_wait_info *wait = &rtwdev->mcc.wait;
	const struct rtw89_c2h_mrc_status_rpt *c2h_rpt;
	struct rtw89_completion_data data = {};
	enum rtw89_mac_mrc_status status;
	unsigned int cond;
	bool next = false;
	u32 tsf_high;
	u32 tsf_low;
	u8 sch_idx;
	u8 func;

	c2h_rpt = (const struct rtw89_c2h_mrc_status_rpt *)c2h->data;
	sch_idx = le32_get_bits(c2h_rpt->w2, RTW89_C2H_MRC_STATUS_RPT_W2_SCH_IDX);
	status = le32_get_bits(c2h_rpt->w2, RTW89_C2H_MRC_STATUS_RPT_W2_STATUS);
	tsf_high = le32_to_cpu(c2h_rpt->tsf_high);
	tsf_low = le32_to_cpu(c2h_rpt->tsf_low);

	switch (status) {
	case RTW89_MAC_MRC_START_SCH_OK:
		func = H2C_FUNC_START_MRC;
		break;
	case RTW89_MAC_MRC_STOP_SCH_OK:
		/* H2C_FUNC_DEL_MRC without STOP_ONLY, so wait for DEL_SCH_OK */
		func = H2C_FUNC_DEL_MRC;
		next = true;
		break;
	case RTW89_MAC_MRC_DEL_SCH_OK:
		func = H2C_FUNC_DEL_MRC;
		break;
	case RTW89_MAC_MRC_EMPTY_SCH_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: empty sch fail\n");
		return;
	case RTW89_MAC_MRC_ROLE_NOT_EXIST_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: role not exist fail\n");
		return;
	case RTW89_MAC_MRC_DATA_NOT_FOUND_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: data not found fail\n");
		return;
	case RTW89_MAC_MRC_GET_NEXT_SLOT_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: get next slot fail\n");
		return;
	case RTW89_MAC_MRC_ALT_ROLE_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: alt role fail\n");
		return;
	case RTW89_MAC_MRC_ADD_PSTIMER_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: add ps timer fail\n");
		return;
	case RTW89_MAC_MRC_MALLOC_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: malloc fail\n");
		return;
	case RTW89_MAC_MRC_SWITCH_CH_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: switch ch fail\n");
		return;
	case RTW89_MAC_MRC_TXNULL0_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: tx null-0 fail\n");
		return;
	case RTW89_MAC_MRC_PORT_FUNC_EN_FAIL:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC C2H STS RPT: port func en fail\n");
		return;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "invalid MRC C2H STS RPT: status %d\n", status);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MRC C2H STS RPT: sch_idx %d, status %d, tsf %llu\n",
		    sch_idx, status, (u64)tsf_high << 32 | tsf_low);

	if (next)
		return;

	cond = RTW89_MRC_WAIT_COND(sch_idx, func);
	rtw89_complete_cond(wait, cond, &data);
}

static
void (* const rtw89_mac_c2h_ofld_handler[])(struct rtw89_dev *rtwdev,
					    struct sk_buff *c2h, u32 len) = {
	[RTW89_MAC_C2H_FUNC_EFUSE_DUMP] = NULL,
	[RTW89_MAC_C2H_FUNC_READ_RSP] = NULL,
	[RTW89_MAC_C2H_FUNC_PKT_OFLD_RSP] = rtw89_mac_c2h_pkt_ofld_rsp,
	[RTW89_MAC_C2H_FUNC_BCN_RESEND] = NULL,
	[RTW89_MAC_C2H_FUNC_MACID_PAUSE] = rtw89_mac_c2h_macid_pause,
	[RTW89_MAC_C2H_FUNC_SCANOFLD_RSP] = rtw89_mac_c2h_scanofld_rsp,
	[RTW89_MAC_C2H_FUNC_TSF32_TOGL_RPT] = rtw89_mac_c2h_tsf32_toggle_rpt,
	[RTW89_MAC_C2H_FUNC_BCNFLTR_RPT] = rtw89_mac_c2h_bcn_fltr_rpt,
};

static
void (* const rtw89_mac_c2h_info_handler[])(struct rtw89_dev *rtwdev,
					    struct sk_buff *c2h, u32 len) = {
	[RTW89_MAC_C2H_FUNC_REC_ACK] = rtw89_mac_c2h_rec_ack,
	[RTW89_MAC_C2H_FUNC_DONE_ACK] = rtw89_mac_c2h_done_ack,
	[RTW89_MAC_C2H_FUNC_C2H_LOG] = rtw89_mac_c2h_log,
	[RTW89_MAC_C2H_FUNC_BCN_CNT] = rtw89_mac_c2h_bcn_cnt,
};

static
void (* const rtw89_mac_c2h_mcc_handler[])(struct rtw89_dev *rtwdev,
					   struct sk_buff *c2h, u32 len) = {
	[RTW89_MAC_C2H_FUNC_MCC_RCV_ACK] = rtw89_mac_c2h_mcc_rcv_ack,
	[RTW89_MAC_C2H_FUNC_MCC_REQ_ACK] = rtw89_mac_c2h_mcc_req_ack,
	[RTW89_MAC_C2H_FUNC_MCC_TSF_RPT] = rtw89_mac_c2h_mcc_tsf_rpt,
	[RTW89_MAC_C2H_FUNC_MCC_STATUS_RPT] = rtw89_mac_c2h_mcc_status_rpt,
};

static
void (* const rtw89_mac_c2h_mrc_handler[])(struct rtw89_dev *rtwdev,
					   struct sk_buff *c2h, u32 len) = {
	[RTW89_MAC_C2H_FUNC_MRC_TSF_RPT] = rtw89_mac_c2h_mrc_tsf_rpt,
	[RTW89_MAC_C2H_FUNC_MRC_STATUS_RPT] = rtw89_mac_c2h_mrc_status_rpt,
};

static
void (* const rtw89_mac_c2h_wow_handler[])(struct rtw89_dev *rtwdev,
					   struct sk_buff *c2h, u32 len) = {
	[RTW89_MAC_C2H_FUNC_AOAC_REPORT] = rtw89_mac_c2h_wow_aoac_rpt,
};

static void rtw89_mac_c2h_scanofld_rsp_atomic(struct rtw89_dev *rtwdev,
					      struct sk_buff *skb)
{
	const struct rtw89_c2h_scanofld *c2h =
		(const struct rtw89_c2h_scanofld *)skb->data;
	struct rtw89_wait_info *fw_ofld_wait = &rtwdev->mac.fw_ofld_wait;
	struct rtw89_completion_data data = {};
	unsigned int cond;
	u8 status, reason;

	status = le32_get_bits(c2h->w2, RTW89_C2H_SCANOFLD_W2_STATUS);
	reason = le32_get_bits(c2h->w2, RTW89_C2H_SCANOFLD_W2_RSN);
	data.err = status != RTW89_SCAN_STATUS_SUCCESS;

	if (reason == RTW89_SCAN_END_SCAN_NOTIFY) {
		if (rtwdev->chip->chip_gen == RTW89_CHIP_BE)
			cond = RTW89_SCANOFLD_BE_WAIT_COND_STOP;
		else
			cond = RTW89_SCANOFLD_WAIT_COND_STOP;

		rtw89_complete_cond(fw_ofld_wait, cond, &data);
	}
}

bool rtw89_mac_c2h_chk_atomic(struct rtw89_dev *rtwdev, struct sk_buff *c2h,
			      u8 class, u8 func)
{
	switch (class) {
	default:
		return false;
	case RTW89_MAC_C2H_CLASS_INFO:
		switch (func) {
		default:
			return false;
		case RTW89_MAC_C2H_FUNC_REC_ACK:
		case RTW89_MAC_C2H_FUNC_DONE_ACK:
			return true;
		}
	case RTW89_MAC_C2H_CLASS_OFLD:
		switch (func) {
		default:
			return false;
		case RTW89_MAC_C2H_FUNC_SCANOFLD_RSP:
			rtw89_mac_c2h_scanofld_rsp_atomic(rtwdev, c2h);
			return false;
		case RTW89_MAC_C2H_FUNC_PKT_OFLD_RSP:
			return true;
		}
	case RTW89_MAC_C2H_CLASS_MCC:
		return true;
	case RTW89_MAC_C2H_CLASS_MRC:
		return true;
	case RTW89_MAC_C2H_CLASS_WOW:
		return true;
	}
}

void rtw89_mac_c2h_handle(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			  u32 len, u8 class, u8 func)
{
	void (*handler)(struct rtw89_dev *rtwdev,
			struct sk_buff *c2h, u32 len) = NULL;

	switch (class) {
	case RTW89_MAC_C2H_CLASS_INFO:
		if (func < RTW89_MAC_C2H_FUNC_INFO_MAX)
			handler = rtw89_mac_c2h_info_handler[func];
		break;
	case RTW89_MAC_C2H_CLASS_OFLD:
		if (func < RTW89_MAC_C2H_FUNC_OFLD_MAX)
			handler = rtw89_mac_c2h_ofld_handler[func];
		break;
	case RTW89_MAC_C2H_CLASS_MCC:
		if (func < NUM_OF_RTW89_MAC_C2H_FUNC_MCC)
			handler = rtw89_mac_c2h_mcc_handler[func];
		break;
	case RTW89_MAC_C2H_CLASS_MRC:
		if (func < NUM_OF_RTW89_MAC_C2H_FUNC_MRC)
			handler = rtw89_mac_c2h_mrc_handler[func];
		break;
	case RTW89_MAC_C2H_CLASS_WOW:
		if (func < NUM_OF_RTW89_MAC_C2H_FUNC_WOW)
			handler = rtw89_mac_c2h_wow_handler[func];
		break;
	case RTW89_MAC_C2H_CLASS_FWDBG:
		return;
	default:
		rtw89_info(rtwdev, "c2h class %d not support\n", class);
		return;
	}
	if (!handler) {
		rtw89_info(rtwdev, "c2h class %d func %d not support\n", class,
			   func);
		return;
	}
	handler(rtwdev, skb, len);
}

static
bool rtw89_mac_get_txpwr_cr_ax(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx,
			       u32 reg_base, u32 *cr)
{
	enum rtw89_qta_mode mode = rtwdev->mac.qta_mode;
	u32 addr = rtw89_mac_reg_by_idx(rtwdev, reg_base, phy_idx);

	if (addr < R_AX_PWR_RATE_CTRL || addr > CMAC1_END_ADDR_AX) {
		rtw89_err(rtwdev, "[TXPWR] addr=0x%x exceed txpwr cr\n",
			  addr);
		goto error;
	}

	if (addr >= CMAC1_START_ADDR_AX && addr <= CMAC1_END_ADDR_AX)
		if (mode == RTW89_QTA_SCC) {
			rtw89_err(rtwdev,
				  "[TXPWR] addr=0x%x but hw not enable\n",
				  addr);
			goto error;
		}

	*cr = addr;
	return true;

error:
	rtw89_err(rtwdev, "[TXPWR] check txpwr cr 0x%x(phy%d) fail\n",
		  addr, phy_idx);

	return false;
}

static
int rtw89_mac_cfg_ppdu_status_ax(struct rtw89_dev *rtwdev, u8 mac_idx, bool enable)
{
	u32 reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PPDU_STAT, mac_idx);
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if (!enable) {
		rtw89_write32_clr(rtwdev, reg, B_AX_PPDU_STAT_RPT_EN);
		return 0;
	}

	rtw89_write32(rtwdev, reg, B_AX_PPDU_STAT_RPT_EN |
				   B_AX_APP_MAC_INFO_RPT |
				   B_AX_APP_RX_CNT_RPT | B_AX_APP_PLCP_HDR_RPT |
				   B_AX_PPDU_STAT_RPT_CRC32);
	rtw89_write32_mask(rtwdev, R_AX_HW_RPT_FWD, B_AX_FWD_PPDU_STAT_MASK,
			   RTW89_PRPT_DEST_HOST);

	return 0;
}

void rtw89_mac_update_rts_threshold(struct rtw89_dev *rtwdev, u8 mac_idx)
{
#define MAC_AX_TIME_TH_SH  5
#define MAC_AX_LEN_TH_SH   4
#define MAC_AX_TIME_TH_MAX 255
#define MAC_AX_LEN_TH_MAX  255
#define MAC_AX_TIME_TH_DEF 88
#define MAC_AX_LEN_TH_DEF  4080
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct ieee80211_hw *hw = rtwdev->hw;
	u32 rts_threshold = hw->wiphy->rts_threshold;
	u32 time_th, len_th;
	u32 reg;

	if (rts_threshold == (u32)-1) {
		time_th = MAC_AX_TIME_TH_DEF;
		len_th = MAC_AX_LEN_TH_DEF;
	} else {
		time_th = MAC_AX_TIME_TH_MAX << MAC_AX_TIME_TH_SH;
		len_th = rts_threshold;
	}

	time_th = min_t(u32, time_th >> MAC_AX_TIME_TH_SH, MAC_AX_TIME_TH_MAX);
	len_th = min_t(u32, len_th >> MAC_AX_LEN_TH_SH, MAC_AX_LEN_TH_MAX);

	reg = rtw89_mac_reg_by_idx(rtwdev, mac->agg_len_ht, mac_idx);
	rtw89_write16_mask(rtwdev, reg, B_AX_RTS_TXTIME_TH_MASK, time_th);
	rtw89_write16_mask(rtwdev, reg, B_AX_RTS_LEN_TH_MASK, len_th);
}

void rtw89_mac_flush_txq(struct rtw89_dev *rtwdev, u32 queues, bool drop)
{
	bool empty;
	int ret;

	if (!test_bit(RTW89_FLAG_POWERON, rtwdev->flags))
		return;

	ret = read_poll_timeout(dle_is_txq_empty, empty, empty,
				10000, 200000, false, rtwdev);
	if (ret && !drop && (rtwdev->total_sta_assoc || rtwdev->scanning))
		rtw89_info(rtwdev, "timed out to flush queues\n");
}

int rtw89_mac_coex_init(struct rtw89_dev *rtwdev, const struct rtw89_mac_ax_coex *coex)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u8 val;
	u16 val16;
	u32 val32;
	int ret;

	rtw89_write8_set(rtwdev, R_AX_GPIO_MUXCFG, B_AX_ENBT);
	if (chip_id != RTL8851B && chip_id != RTL8852BT)
		rtw89_write8_set(rtwdev, R_AX_BTC_FUNC_EN, B_AX_PTA_WL_TX_EN);
	rtw89_write8_set(rtwdev, R_AX_BT_COEX_CFG_2 + 1, B_AX_GNT_BT_POLARITY >> 8);
	rtw89_write8_set(rtwdev, R_AX_CSR_MODE, B_AX_STATIS_BT_EN | B_AX_WL_ACT_MSK);
	rtw89_write8_set(rtwdev, R_AX_CSR_MODE + 2, B_AX_BT_CNT_RST >> 16);
	if (chip_id != RTL8851B && chip_id != RTL8852BT)
		rtw89_write8_clr(rtwdev, R_AX_TRXPTCL_RESP_0 + 3, B_AX_RSP_CHK_BTCCA >> 24);

	val16 = rtw89_read16(rtwdev, R_AX_CCA_CFG_0);
	val16 = (val16 | B_AX_BTCCA_EN) & ~B_AX_BTCCA_BRK_TXOP_EN;
	rtw89_write16(rtwdev, R_AX_CCA_CFG_0, val16);

	ret = rtw89_mac_read_lte(rtwdev, R_AX_LTE_SW_CFG_2, &val32);
	if (ret) {
		rtw89_err(rtwdev, "Read R_AX_LTE_SW_CFG_2 fail!\n");
		return ret;
	}
	val32 = val32 & B_AX_WL_RX_CTRL;
	ret = rtw89_mac_write_lte(rtwdev, R_AX_LTE_SW_CFG_2, val32);
	if (ret) {
		rtw89_err(rtwdev, "Write R_AX_LTE_SW_CFG_2 fail!\n");
		return ret;
	}

	switch (coex->pta_mode) {
	case RTW89_MAC_AX_COEX_RTK_MODE:
		val = rtw89_read8(rtwdev, R_AX_GPIO_MUXCFG);
		val &= ~B_AX_BTMODE_MASK;
		val |= FIELD_PREP(B_AX_BTMODE_MASK, MAC_AX_BT_MODE_0_3);
		rtw89_write8(rtwdev, R_AX_GPIO_MUXCFG, val);

		val = rtw89_read8(rtwdev, R_AX_TDMA_MODE);
		rtw89_write8(rtwdev, R_AX_TDMA_MODE, val | B_AX_RTK_BT_ENABLE);

		val = rtw89_read8(rtwdev, R_AX_BT_COEX_CFG_5);
		val &= ~B_AX_BT_RPT_SAMPLE_RATE_MASK;
		val |= FIELD_PREP(B_AX_BT_RPT_SAMPLE_RATE_MASK, MAC_AX_RTK_RATE);
		rtw89_write8(rtwdev, R_AX_BT_COEX_CFG_5, val);
		break;
	case RTW89_MAC_AX_COEX_CSR_MODE:
		val = rtw89_read8(rtwdev, R_AX_GPIO_MUXCFG);
		val &= ~B_AX_BTMODE_MASK;
		val |= FIELD_PREP(B_AX_BTMODE_MASK, MAC_AX_BT_MODE_2);
		rtw89_write8(rtwdev, R_AX_GPIO_MUXCFG, val);

		val16 = rtw89_read16(rtwdev, R_AX_CSR_MODE);
		val16 &= ~B_AX_BT_PRI_DETECT_TO_MASK;
		val16 |= FIELD_PREP(B_AX_BT_PRI_DETECT_TO_MASK, MAC_AX_CSR_PRI_TO);
		val16 &= ~B_AX_BT_TRX_INIT_DETECT_MASK;
		val16 |= FIELD_PREP(B_AX_BT_TRX_INIT_DETECT_MASK, MAC_AX_CSR_TRX_TO);
		val16 &= ~B_AX_BT_STAT_DELAY_MASK;
		val16 |= FIELD_PREP(B_AX_BT_STAT_DELAY_MASK, MAC_AX_CSR_DELAY);
		val16 |= B_AX_ENHANCED_BT;
		rtw89_write16(rtwdev, R_AX_CSR_MODE, val16);

		rtw89_write8(rtwdev, R_AX_BT_COEX_CFG_2, MAC_AX_CSR_RATE);
		break;
	default:
		return -EINVAL;
	}

	switch (coex->direction) {
	case RTW89_MAC_AX_COEX_INNER:
		val = rtw89_read8(rtwdev, R_AX_GPIO_MUXCFG + 1);
		val = (val & ~BIT(2)) | BIT(1);
		rtw89_write8(rtwdev, R_AX_GPIO_MUXCFG + 1, val);
		break;
	case RTW89_MAC_AX_COEX_OUTPUT:
		val = rtw89_read8(rtwdev, R_AX_GPIO_MUXCFG + 1);
		val = val | BIT(1) | BIT(0);
		rtw89_write8(rtwdev, R_AX_GPIO_MUXCFG + 1, val);
		break;
	case RTW89_MAC_AX_COEX_INPUT:
		val = rtw89_read8(rtwdev, R_AX_GPIO_MUXCFG + 1);
		val = val & ~(BIT(2) | BIT(1));
		rtw89_write8(rtwdev, R_AX_GPIO_MUXCFG + 1, val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_coex_init);

int rtw89_mac_coex_init_v1(struct rtw89_dev *rtwdev,
			   const struct rtw89_mac_ax_coex *coex)
{
	rtw89_write32_set(rtwdev, R_AX_BTC_CFG,
			  B_AX_BTC_EN | B_AX_BTG_LNA1_GAIN_SEL);
	rtw89_write32_set(rtwdev, R_AX_BT_CNT_CFG, B_AX_BT_CNT_EN);
	rtw89_write16_set(rtwdev, R_AX_CCA_CFG_0, B_AX_BTCCA_EN);
	rtw89_write16_clr(rtwdev, R_AX_CCA_CFG_0, B_AX_BTCCA_BRK_TXOP_EN);

	switch (coex->pta_mode) {
	case RTW89_MAC_AX_COEX_RTK_MODE:
		rtw89_write32_mask(rtwdev, R_AX_BTC_CFG, B_AX_BTC_MODE_MASK,
				   MAC_AX_RTK_MODE);
		rtw89_write32_mask(rtwdev, R_AX_RTK_MODE_CFG_V1,
				   B_AX_SAMPLE_CLK_MASK, MAC_AX_RTK_RATE);
		break;
	case RTW89_MAC_AX_COEX_CSR_MODE:
		rtw89_write32_mask(rtwdev, R_AX_BTC_CFG, B_AX_BTC_MODE_MASK,
				   MAC_AX_CSR_MODE);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_coex_init_v1);

int rtw89_mac_cfg_gnt(struct rtw89_dev *rtwdev,
		      const struct rtw89_mac_ax_coex_gnt *gnt_cfg)
{
	u32 val = 0, ret;

	if (gnt_cfg->band[0].gnt_bt)
		val |= B_AX_GNT_BT_RFC_S0_SW_VAL | B_AX_GNT_BT_BB_S0_SW_VAL;

	if (gnt_cfg->band[0].gnt_bt_sw_en)
		val |= B_AX_GNT_BT_RFC_S0_SW_CTRL | B_AX_GNT_BT_BB_S0_SW_CTRL;

	if (gnt_cfg->band[0].gnt_wl)
		val |= B_AX_GNT_WL_RFC_S0_SW_VAL | B_AX_GNT_WL_BB_S0_SW_VAL;

	if (gnt_cfg->band[0].gnt_wl_sw_en)
		val |= B_AX_GNT_WL_RFC_S0_SW_CTRL | B_AX_GNT_WL_BB_S0_SW_CTRL;

	if (gnt_cfg->band[1].gnt_bt)
		val |= B_AX_GNT_BT_RFC_S1_SW_VAL | B_AX_GNT_BT_BB_S1_SW_VAL;

	if (gnt_cfg->band[1].gnt_bt_sw_en)
		val |= B_AX_GNT_BT_RFC_S1_SW_CTRL | B_AX_GNT_BT_BB_S1_SW_CTRL;

	if (gnt_cfg->band[1].gnt_wl)
		val |= B_AX_GNT_WL_RFC_S1_SW_VAL | B_AX_GNT_WL_BB_S1_SW_VAL;

	if (gnt_cfg->band[1].gnt_wl_sw_en)
		val |= B_AX_GNT_WL_RFC_S1_SW_CTRL | B_AX_GNT_WL_BB_S1_SW_CTRL;

	ret = rtw89_mac_write_lte(rtwdev, R_AX_LTE_SW_CFG_1, val);
	if (ret) {
		rtw89_err(rtwdev, "Write LTE fail!\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_cfg_gnt);

int rtw89_mac_cfg_gnt_v1(struct rtw89_dev *rtwdev,
			 const struct rtw89_mac_ax_coex_gnt *gnt_cfg)
{
	u32 val = 0;

	if (gnt_cfg->band[0].gnt_bt)
		val |= B_AX_GNT_BT_RFC_S0_VAL | B_AX_GNT_BT_RX_VAL |
		       B_AX_GNT_BT_TX_VAL;
	else
		val |= B_AX_WL_ACT_VAL;

	if (gnt_cfg->band[0].gnt_bt_sw_en)
		val |= B_AX_GNT_BT_RFC_S0_SWCTRL | B_AX_GNT_BT_RX_SWCTRL |
		       B_AX_GNT_BT_TX_SWCTRL | B_AX_WL_ACT_SWCTRL;

	if (gnt_cfg->band[0].gnt_wl)
		val |= B_AX_GNT_WL_RFC_S0_VAL | B_AX_GNT_WL_RX_VAL |
		       B_AX_GNT_WL_TX_VAL | B_AX_GNT_WL_BB_VAL;

	if (gnt_cfg->band[0].gnt_wl_sw_en)
		val |= B_AX_GNT_WL_RFC_S0_SWCTRL | B_AX_GNT_WL_RX_SWCTRL |
		       B_AX_GNT_WL_TX_SWCTRL | B_AX_GNT_WL_BB_SWCTRL;

	if (gnt_cfg->band[1].gnt_bt)
		val |= B_AX_GNT_BT_RFC_S1_VAL | B_AX_GNT_BT_RX_VAL |
		       B_AX_GNT_BT_TX_VAL;
	else
		val |= B_AX_WL_ACT_VAL;

	if (gnt_cfg->band[1].gnt_bt_sw_en)
		val |= B_AX_GNT_BT_RFC_S1_SWCTRL | B_AX_GNT_BT_RX_SWCTRL |
		       B_AX_GNT_BT_TX_SWCTRL | B_AX_WL_ACT_SWCTRL;

	if (gnt_cfg->band[1].gnt_wl)
		val |= B_AX_GNT_WL_RFC_S1_VAL | B_AX_GNT_WL_RX_VAL |
		       B_AX_GNT_WL_TX_VAL | B_AX_GNT_WL_BB_VAL;

	if (gnt_cfg->band[1].gnt_wl_sw_en)
		val |= B_AX_GNT_WL_RFC_S1_SWCTRL | B_AX_GNT_WL_RX_SWCTRL |
		       B_AX_GNT_WL_TX_SWCTRL | B_AX_GNT_WL_BB_SWCTRL;

	rtw89_write32(rtwdev, R_AX_GNT_SW_CTRL, val);

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_cfg_gnt_v1);

static
int rtw89_mac_cfg_plt_ax(struct rtw89_dev *rtwdev, struct rtw89_mac_ax_plt *plt)
{
	u32 reg;
	u16 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, plt->band, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_BT_PLT, plt->band);
	val = (plt->tx & RTW89_MAC_AX_PLT_LTE_RX ? B_AX_TX_PLT_GNT_LTE_RX : 0) |
	      (plt->tx & RTW89_MAC_AX_PLT_GNT_BT_TX ? B_AX_TX_PLT_GNT_BT_TX : 0) |
	      (plt->tx & RTW89_MAC_AX_PLT_GNT_BT_RX ? B_AX_TX_PLT_GNT_BT_RX : 0) |
	      (plt->tx & RTW89_MAC_AX_PLT_GNT_WL ? B_AX_TX_PLT_GNT_WL : 0) |
	      (plt->rx & RTW89_MAC_AX_PLT_LTE_RX ? B_AX_RX_PLT_GNT_LTE_RX : 0) |
	      (plt->rx & RTW89_MAC_AX_PLT_GNT_BT_TX ? B_AX_RX_PLT_GNT_BT_TX : 0) |
	      (plt->rx & RTW89_MAC_AX_PLT_GNT_BT_RX ? B_AX_RX_PLT_GNT_BT_RX : 0) |
	      (plt->rx & RTW89_MAC_AX_PLT_GNT_WL ? B_AX_RX_PLT_GNT_WL : 0) |
	      B_AX_PLT_EN;
	rtw89_write16(rtwdev, reg, val);

	return 0;
}

void rtw89_mac_cfg_sb(struct rtw89_dev *rtwdev, u32 val)
{
	u32 fw_sb;

	fw_sb = rtw89_read32(rtwdev, R_AX_SCOREBOARD);
	fw_sb = FIELD_GET(B_MAC_AX_SB_FW_MASK, fw_sb);
	fw_sb = fw_sb & ~B_MAC_AX_BTGS1_NOTIFY;
	if (!test_bit(RTW89_FLAG_POWERON, rtwdev->flags))
		fw_sb = fw_sb | MAC_AX_NOTIFY_PWR_MAJOR;
	else
		fw_sb = fw_sb | MAC_AX_NOTIFY_TP_MAJOR;
	val = FIELD_GET(B_MAC_AX_SB_DRV_MASK, val);
	val = B_AX_TOGGLE |
	      FIELD_PREP(B_MAC_AX_SB_DRV_MASK, val) |
	      FIELD_PREP(B_MAC_AX_SB_FW_MASK, fw_sb);
	rtw89_write32(rtwdev, R_AX_SCOREBOARD, val);
	fsleep(1000); /* avoid BT FW loss information */
}

u32 rtw89_mac_get_sb(struct rtw89_dev *rtwdev)
{
	return rtw89_read32(rtwdev, R_AX_SCOREBOARD);
}

int rtw89_mac_cfg_ctrl_path(struct rtw89_dev *rtwdev, bool wl)
{
	u8 val = rtw89_read8(rtwdev, R_AX_SYS_SDIO_CTRL + 3);

	val = wl ? val | BIT(2) : val & ~BIT(2);
	rtw89_write8(rtwdev, R_AX_SYS_SDIO_CTRL + 3, val);

	return 0;
}
EXPORT_SYMBOL(rtw89_mac_cfg_ctrl_path);

int rtw89_mac_cfg_ctrl_path_v1(struct rtw89_dev *rtwdev, bool wl)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_mac_ax_gnt *g = dm->gnt.band;
	int i;

	if (wl)
		return 0;

	for (i = 0; i < RTW89_PHY_MAX; i++) {
		g[i].gnt_bt_sw_en = 1;
		g[i].gnt_bt = 1;
		g[i].gnt_wl_sw_en = 1;
		g[i].gnt_wl = 0;
	}

	return rtw89_mac_cfg_gnt_v1(rtwdev, &dm->gnt);
}
EXPORT_SYMBOL(rtw89_mac_cfg_ctrl_path_v1);

bool rtw89_mac_get_ctrl_path(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 val = 0;

	if (chip->chip_id == RTL8852C || chip->chip_id == RTL8922A)
		return false;
	else if (chip->chip_id == RTL8852A || rtw89_is_rtl885xb(rtwdev))
		val = rtw89_read8_mask(rtwdev, R_AX_SYS_SDIO_CTRL + 3,
				       B_AX_LTE_MUX_CTRL_PATH >> 24);

	return !!val;
}

static u16 rtw89_mac_get_plt_cnt_ax(struct rtw89_dev *rtwdev, u8 band)
{
	u32 reg;
	u16 cnt;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_BT_PLT, band);
	cnt = rtw89_read32_mask(rtwdev, reg, B_AX_BT_PLT_PKT_CNT_MASK);
	rtw89_write16_set(rtwdev, reg, B_AX_BT_PLT_RST);

	return cnt;
}

static void rtw89_mac_bfee_standby_timer(struct rtw89_dev *rtwdev, u8 mac_idx,
					 bool keep)
{
	u32 reg;

	if (rtwdev->chip->chip_gen != RTW89_CHIP_AX)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_BF, "set bfee standby_timer to %d\n", keep);
	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_BFMEE_RESP_OPTION, mac_idx);
	if (keep) {
		set_bit(RTW89_FLAG_BFEE_TIMER_KEEP, rtwdev->flags);
		rtw89_write32_mask(rtwdev, reg, B_AX_BFMEE_BFRP_RX_STANDBY_TIMER_MASK,
				   BFRP_RX_STANDBY_TIMER_KEEP);
	} else {
		clear_bit(RTW89_FLAG_BFEE_TIMER_KEEP, rtwdev->flags);
		rtw89_write32_mask(rtwdev, reg, B_AX_BFMEE_BFRP_RX_STANDBY_TIMER_MASK,
				   BFRP_RX_STANDBY_TIMER_RELEASE);
	}
}

void rtw89_mac_bfee_ctrl(struct rtw89_dev *rtwdev, u8 mac_idx, bool en)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u32 reg;
	u32 mask = mac->bfee_ctrl.mask;

	rtw89_debug(rtwdev, RTW89_DBG_BF, "set bfee ndpa_en to %d\n", en);
	reg = rtw89_mac_reg_by_idx(rtwdev, mac->bfee_ctrl.addr, mac_idx);
	if (en) {
		set_bit(RTW89_FLAG_BFEE_EN, rtwdev->flags);
		rtw89_write32_set(rtwdev, reg, mask);
	} else {
		clear_bit(RTW89_FLAG_BFEE_EN, rtwdev->flags);
		rtw89_write32_clr(rtwdev, reg, mask);
	}
}

static int rtw89_mac_init_bfee_ax(struct rtw89_dev *rtwdev, u8 mac_idx)
{
	u32 reg;
	u32 val32;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	/* AP mode set tx gid to 63 */
	/* STA mode set tx gid to 0(default) */
	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_BFMER_CTRL_0, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_BFMER_NDP_BFEN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_CSI_RRSC, mac_idx);
	rtw89_write32(rtwdev, reg, CSI_RRSC_BMAP);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_BFMEE_RESP_OPTION, mac_idx);
	val32 = FIELD_PREP(B_AX_BFMEE_NDP_RX_STANDBY_TIMER_MASK, NDP_RX_STANDBY_TIMER);
	rtw89_write32(rtwdev, reg, val32);
	rtw89_mac_bfee_standby_timer(rtwdev, mac_idx, true);
	rtw89_mac_bfee_ctrl(rtwdev, mac_idx, true);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_CSI_CTRL_0, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_BFMEE_BFPARAM_SEL |
				       B_AX_BFMEE_USE_NSTS |
				       B_AX_BFMEE_CSI_GID_SEL |
				       B_AX_BFMEE_CSI_FORCE_RETE_EN);
	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_CSI_RATE, mac_idx);
	rtw89_write32(rtwdev, reg,
		      u32_encode_bits(CSI_INIT_RATE_HT, B_AX_BFMEE_HT_CSI_RATE_MASK) |
		      u32_encode_bits(CSI_INIT_RATE_VHT, B_AX_BFMEE_VHT_CSI_RATE_MASK) |
		      u32_encode_bits(CSI_INIT_RATE_HE, B_AX_BFMEE_HE_CSI_RATE_MASK));

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_CSIRPT_OPTION, mac_idx);
	rtw89_write32_set(rtwdev, reg,
			  B_AX_CSIPRT_VHTSU_AID_EN | B_AX_CSIPRT_HESU_AID_EN);

	return 0;
}

static int rtw89_mac_set_csi_para_reg_ax(struct rtw89_dev *rtwdev,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	u8 mac_idx = rtwvif->mac_idx;
	u8 nc = 1, nr = 3, ng = 0, cb = 1, cs = 1, ldpc_en = 1, stbc_en = 1;
	u8 port_sel = rtwvif->port;
	u8 sound_dim = 3, t;
	u8 *phy_cap = sta->deflink.he_cap.he_cap_elem.phy_cap_info;
	u32 reg;
	u16 val;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	if ((phy_cap[3] & IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER) ||
	    (phy_cap[4] & IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER)) {
		ldpc_en &= !!(phy_cap[1] & IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD);
		stbc_en &= !!(phy_cap[2] & IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ);
		t = FIELD_GET(IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			      phy_cap[5]);
		sound_dim = min(sound_dim, t);
	}
	if ((sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE) ||
	    (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE)) {
		ldpc_en &= !!(sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC);
		stbc_en &= !!(sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_MASK);
		t = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
			      sta->deflink.vht_cap.cap);
		sound_dim = min(sound_dim, t);
	}
	nc = min(nc, sound_dim);
	nr = min(nr, sound_dim);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_CSI_CTRL_0, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_BFMEE_BFPARAM_SEL);

	val = FIELD_PREP(B_AX_BFMEE_CSIINFO0_NC_MASK, nc) |
	      FIELD_PREP(B_AX_BFMEE_CSIINFO0_NR_MASK, nr) |
	      FIELD_PREP(B_AX_BFMEE_CSIINFO0_NG_MASK, ng) |
	      FIELD_PREP(B_AX_BFMEE_CSIINFO0_CB_MASK, cb) |
	      FIELD_PREP(B_AX_BFMEE_CSIINFO0_CS_MASK, cs) |
	      FIELD_PREP(B_AX_BFMEE_CSIINFO0_LDPC_EN, ldpc_en) |
	      FIELD_PREP(B_AX_BFMEE_CSIINFO0_STBC_EN, stbc_en);

	if (port_sel == 0)
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_CSI_CTRL_0, mac_idx);
	else
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_CSI_CTRL_1, mac_idx);

	rtw89_write16(rtwdev, reg, val);

	return 0;
}

static int rtw89_mac_csi_rrsc_ax(struct rtw89_dev *rtwdev,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	u32 rrsc = BIT(RTW89_MAC_BF_RRSC_6M) | BIT(RTW89_MAC_BF_RRSC_24M);
	u32 reg;
	u8 mac_idx = rtwvif->mac_idx;
	int ret;

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
	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_CSI_CTRL_0, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_BFMEE_BFPARAM_SEL);
	rtw89_write32_clr(rtwdev, reg, B_AX_BFMEE_CSI_FORCE_RETE_EN);
	rtw89_write32(rtwdev,
		      rtw89_mac_reg_by_idx(rtwdev, R_AX_TRXPTCL_RESP_CSI_RRSC, mac_idx),
		      rrsc);

	return 0;
}

static void rtw89_mac_bf_assoc_ax(struct rtw89_dev *rtwdev,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	if (rtw89_sta_has_beamformer_cap(sta)) {
		rtw89_debug(rtwdev, RTW89_DBG_BF,
			    "initialize bfee for new association\n");
		rtw89_mac_init_bfee_ax(rtwdev, rtwvif->mac_idx);
		rtw89_mac_set_csi_para_reg_ax(rtwdev, vif, sta);
		rtw89_mac_csi_rrsc_ax(rtwdev, vif, sta);
	}
}

void rtw89_mac_bf_disassoc(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;

	rtw89_mac_bfee_ctrl(rtwdev, rtwvif->mac_idx, false);
}

void rtw89_mac_bf_set_gid_table(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *conf)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	u8 mac_idx = rtwvif->mac_idx;
	__le32 *p;

	rtw89_debug(rtwdev, RTW89_DBG_BF, "update bf GID table\n");

	p = (__le32 *)conf->mu_group.membership;
	rtw89_write32(rtwdev,
		      rtw89_mac_reg_by_idx(rtwdev, R_AX_GID_POSITION_EN0, mac_idx),
		      le32_to_cpu(p[0]));
	rtw89_write32(rtwdev,
		      rtw89_mac_reg_by_idx(rtwdev, R_AX_GID_POSITION_EN1, mac_idx),
		      le32_to_cpu(p[1]));

	p = (__le32 *)conf->mu_group.position;
	rtw89_write32(rtwdev, rtw89_mac_reg_by_idx(rtwdev, R_AX_GID_POSITION0, mac_idx),
		      le32_to_cpu(p[0]));
	rtw89_write32(rtwdev, rtw89_mac_reg_by_idx(rtwdev, R_AX_GID_POSITION1, mac_idx),
		      le32_to_cpu(p[1]));
	rtw89_write32(rtwdev, rtw89_mac_reg_by_idx(rtwdev, R_AX_GID_POSITION2, mac_idx),
		      le32_to_cpu(p[2]));
	rtw89_write32(rtwdev, rtw89_mac_reg_by_idx(rtwdev, R_AX_GID_POSITION3, mac_idx),
		      le32_to_cpu(p[3]));
}

struct rtw89_mac_bf_monitor_iter_data {
	struct rtw89_dev *rtwdev;
	struct ieee80211_sta *down_sta;
	int count;
};

static
void rtw89_mac_bf_monitor_calc_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_mac_bf_monitor_iter_data *iter_data =
				(struct rtw89_mac_bf_monitor_iter_data *)data;
	struct ieee80211_sta *down_sta = iter_data->down_sta;
	int *count = &iter_data->count;

	if (down_sta == sta)
		return;

	if (rtw89_sta_has_beamformer_cap(sta))
		(*count)++;
}

void rtw89_mac_bf_monitor_calc(struct rtw89_dev *rtwdev,
			       struct ieee80211_sta *sta, bool disconnect)
{
	struct rtw89_mac_bf_monitor_iter_data data;

	data.rtwdev = rtwdev;
	data.down_sta = disconnect ? sta : NULL;
	data.count = 0;
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_mac_bf_monitor_calc_iter,
					  &data);

	rtw89_debug(rtwdev, RTW89_DBG_BF, "bfee STA count=%d\n", data.count);
	if (data.count)
		set_bit(RTW89_FLAG_BFEE_MON, rtwdev->flags);
	else
		clear_bit(RTW89_FLAG_BFEE_MON, rtwdev->flags);
}

void _rtw89_mac_bf_monitor_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_traffic_stats *stats = &rtwdev->stats;
	struct rtw89_vif *rtwvif;
	bool en = stats->tx_tfc_lv <= stats->rx_tfc_lv;
	bool old = test_bit(RTW89_FLAG_BFEE_EN, rtwdev->flags);
	bool keep_timer = true;
	bool old_keep_timer;

	old_keep_timer = test_bit(RTW89_FLAG_BFEE_TIMER_KEEP, rtwdev->flags);

	if (stats->tx_tfc_lv <= RTW89_TFC_LOW && stats->rx_tfc_lv <= RTW89_TFC_LOW)
		keep_timer = false;

	if (keep_timer != old_keep_timer) {
		rtw89_for_each_rtwvif(rtwdev, rtwvif)
			rtw89_mac_bfee_standby_timer(rtwdev, rtwvif->mac_idx,
						     keep_timer);
	}

	if (en == old)
		return;

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_mac_bfee_ctrl(rtwdev, rtwvif->mac_idx, en);
}

static int
__rtw89_mac_set_tx_time(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta,
			u32 tx_time)
{
#define MAC_AX_DFLT_TX_TIME 5280
	u8 mac_idx = rtwsta->rtwvif->mac_idx;
	u32 max_tx_time = tx_time == 0 ? MAC_AX_DFLT_TX_TIME : tx_time;
	u32 reg;
	int ret = 0;

	if (rtwsta->cctl_tx_time) {
		rtwsta->ampdu_max_time = (max_tx_time - 512) >> 9;
		ret = rtw89_fw_h2c_txtime_cmac_tbl(rtwdev, rtwsta);
	} else {
		ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
		if (ret) {
			rtw89_warn(rtwdev, "failed to check cmac in set txtime\n");
			return ret;
		}

		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_AMPDU_AGG_LIMIT, mac_idx);
		rtw89_write32_mask(rtwdev, reg, B_AX_AMPDU_MAX_TIME_MASK,
				   max_tx_time >> 5);
	}

	return ret;
}

int rtw89_mac_set_tx_time(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta,
			  bool resume, u32 tx_time)
{
	int ret = 0;

	if (!resume) {
		rtwsta->cctl_tx_time = true;
		ret = __rtw89_mac_set_tx_time(rtwdev, rtwsta, tx_time);
	} else {
		ret = __rtw89_mac_set_tx_time(rtwdev, rtwsta, tx_time);
		rtwsta->cctl_tx_time = false;
	}

	return ret;
}

int rtw89_mac_get_tx_time(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta,
			  u32 *tx_time)
{
	u8 mac_idx = rtwsta->rtwvif->mac_idx;
	u32 reg;
	int ret = 0;

	if (rtwsta->cctl_tx_time) {
		*tx_time = (rtwsta->ampdu_max_time + 1) << 9;
	} else {
		ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
		if (ret) {
			rtw89_warn(rtwdev, "failed to check cmac in tx_time\n");
			return ret;
		}

		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_AMPDU_AGG_LIMIT, mac_idx);
		*tx_time = rtw89_read32_mask(rtwdev, reg, B_AX_AMPDU_MAX_TIME_MASK) << 5;
	}

	return ret;
}

int rtw89_mac_set_tx_retry_limit(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta,
				 bool resume, u8 tx_retry)
{
	int ret = 0;

	rtwsta->data_tx_cnt_lmt = tx_retry;

	if (!resume) {
		rtwsta->cctl_tx_retry_limit = true;
		ret = rtw89_fw_h2c_txtime_cmac_tbl(rtwdev, rtwsta);
	} else {
		ret = rtw89_fw_h2c_txtime_cmac_tbl(rtwdev, rtwsta);
		rtwsta->cctl_tx_retry_limit = false;
	}

	return ret;
}

int rtw89_mac_get_tx_retry_limit(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta, u8 *tx_retry)
{
	u8 mac_idx = rtwsta->rtwvif->mac_idx;
	u32 reg;
	int ret = 0;

	if (rtwsta->cctl_tx_retry_limit) {
		*tx_retry = rtwsta->data_tx_cnt_lmt;
	} else {
		ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
		if (ret) {
			rtw89_warn(rtwdev, "failed to check cmac in rty_lmt\n");
			return ret;
		}

		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_TXCNT, mac_idx);
		*tx_retry = rtw89_read32_mask(rtwdev, reg, B_AX_L_TXCNT_LMT_MASK);
	}

	return ret;
}

int rtw89_mac_set_hw_muedca_ctrl(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif, bool en)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u8 mac_idx = rtwvif->mac_idx;
	u16 set = mac->muedca_ctrl.mask;
	u32 reg;
	u32 ret;

	ret = rtw89_mac_check_mac_en(rtwdev, mac_idx, RTW89_CMAC_SEL);
	if (ret)
		return ret;

	reg = rtw89_mac_reg_by_idx(rtwdev, mac->muedca_ctrl.addr, mac_idx);
	if (en)
		rtw89_write16_set(rtwdev, reg, set);
	else
		rtw89_write16_clr(rtwdev, reg, set);

	return 0;
}

static
int rtw89_mac_write_xtal_si_ax(struct rtw89_dev *rtwdev, u8 offset, u8 val, u8 mask)
{
	u32 val32;
	int ret;

	val32 = FIELD_PREP(B_AX_WL_XTAL_SI_ADDR_MASK, offset) |
		FIELD_PREP(B_AX_WL_XTAL_SI_DATA_MASK, val) |
		FIELD_PREP(B_AX_WL_XTAL_SI_BITMASK_MASK, mask) |
		FIELD_PREP(B_AX_WL_XTAL_SI_MODE_MASK, XTAL_SI_NORMAL_WRITE) |
		FIELD_PREP(B_AX_WL_XTAL_SI_CMD_POLL, 1);
	rtw89_write32(rtwdev, R_AX_WLAN_XTAL_SI_CTRL, val32);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_AX_WL_XTAL_SI_CMD_POLL),
				50, 50000, false, rtwdev, R_AX_WLAN_XTAL_SI_CTRL);
	if (ret) {
		rtw89_warn(rtwdev, "xtal si not ready(W): offset=%x val=%x mask=%x\n",
			   offset, val, mask);
		return ret;
	}

	return 0;
}

static
int rtw89_mac_read_xtal_si_ax(struct rtw89_dev *rtwdev, u8 offset, u8 *val)
{
	u32 val32;
	int ret;

	val32 = FIELD_PREP(B_AX_WL_XTAL_SI_ADDR_MASK, offset) |
		FIELD_PREP(B_AX_WL_XTAL_SI_DATA_MASK, 0x00) |
		FIELD_PREP(B_AX_WL_XTAL_SI_BITMASK_MASK, 0x00) |
		FIELD_PREP(B_AX_WL_XTAL_SI_MODE_MASK, XTAL_SI_NORMAL_READ) |
		FIELD_PREP(B_AX_WL_XTAL_SI_CMD_POLL, 1);
	rtw89_write32(rtwdev, R_AX_WLAN_XTAL_SI_CTRL, val32);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_AX_WL_XTAL_SI_CMD_POLL),
				50, 50000, false, rtwdev, R_AX_WLAN_XTAL_SI_CTRL);
	if (ret) {
		rtw89_warn(rtwdev, "xtal si not ready(R): offset=%x\n", offset);
		return ret;
	}

	*val = rtw89_read8(rtwdev, R_AX_WLAN_XTAL_SI_CTRL + 1);

	return 0;
}

static
void rtw89_mac_pkt_drop_sta(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta)
{
	static const enum rtw89_pkt_drop_sel sels[] = {
		RTW89_PKT_DROP_SEL_MACID_BE_ONCE,
		RTW89_PKT_DROP_SEL_MACID_BK_ONCE,
		RTW89_PKT_DROP_SEL_MACID_VI_ONCE,
		RTW89_PKT_DROP_SEL_MACID_VO_ONCE,
	};
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	struct rtw89_pkt_drop_params params = {0};
	int i;

	params.mac_band = RTW89_MAC_0;
	params.macid = rtwsta->mac_id;
	params.port = rtwvif->port;
	params.mbssid = 0;
	params.tf_trs = rtwvif->trigger;

	for (i = 0; i < ARRAY_SIZE(sels); i++) {
		params.sel = sels[i];
		rtw89_fw_h2c_pkt_drop(rtwdev, &params);
	}
}

static void rtw89_mac_pkt_drop_vif_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	struct rtw89_dev *rtwdev = rtwvif->rtwdev;
	struct rtw89_vif *target = data;

	if (rtwvif != target)
		return;

	rtw89_mac_pkt_drop_sta(rtwdev, rtwsta);
}

void rtw89_mac_pkt_drop_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_mac_pkt_drop_vif_iter,
					  rtwvif);
}

int rtw89_mac_ptk_drop_by_band_and_wait(struct rtw89_dev *rtwdev,
					enum rtw89_mac_idx band)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_pkt_drop_params params = {0};
	bool empty;
	int i, ret = 0, try_cnt = 3;

	params.mac_band = band;
	params.sel = RTW89_PKT_DROP_SEL_BAND_ONCE;

	for (i = 0; i < try_cnt; i++) {
		ret = read_poll_timeout(mac->is_txq_empty, empty, empty, 50,
					50000, false, rtwdev);
		if (ret && !RTW89_CHK_FW_FEATURE(NO_PACKET_DROP, &rtwdev->fw))
			rtw89_fw_h2c_pkt_drop(rtwdev, &params);
		else
			return 0;
	}
	return ret;
}

int rtw89_mac_cpu_io_rx(struct rtw89_dev *rtwdev, bool wow_enable)
{
	struct rtw89_mac_h2c_info h2c_info = {};
	struct rtw89_mac_c2h_info c2h_info = {};
	u32 ret;

	h2c_info.id = RTW89_FWCMD_H2CREG_FUNC_WOW_CPUIO_RX_CTRL;
	h2c_info.content_len = sizeof(h2c_info.u.hdr);
	h2c_info.u.hdr.w0 = u32_encode_bits(wow_enable, RTW89_H2CREG_WOW_CPUIO_RX_CTRL_EN);

	ret = rtw89_fw_msg_reg(rtwdev, &h2c_info, &c2h_info);
	if (ret)
		return ret;

	if (c2h_info.id != RTW89_FWCMD_C2HREG_FUNC_WOW_CPUIO_RX_ACK)
		ret = -EINVAL;

	return ret;
}

static int rtw89_wow_config_mac_ax(struct rtw89_dev *rtwdev, bool enable_wow)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	int ret;

	if (enable_wow) {
		ret = rtw89_mac_resize_ple_rx_quota(rtwdev, true);
		if (ret) {
			rtw89_err(rtwdev, "[ERR]patch rx qta %d\n", ret);
			return ret;
		}

		rtw89_write32_set(rtwdev, R_AX_RX_FUNCTION_STOP, B_AX_HDR_RX_STOP);
		rtw89_mac_cpu_io_rx(rtwdev, enable_wow);
		rtw89_write32_clr(rtwdev, mac->rx_fltr, B_AX_SNIFFER_MODE);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
		rtw89_write32(rtwdev, R_AX_ACTION_FWD0, 0);
		rtw89_write32(rtwdev, R_AX_ACTION_FWD1, 0);
		rtw89_write32(rtwdev, R_AX_TF_FWD, 0);
		rtw89_write32(rtwdev, R_AX_HW_RPT_FWD, 0);

		if (chip->chip_id == RTL8852A || rtw89_is_rtl885xb(rtwdev))
			rtw89_write8(rtwdev, R_BE_DBG_WOW_READY, WOWLAN_NOT_READY);
		else
			rtw89_write32_set(rtwdev, R_AX_DBG_WOW,
					  B_AX_DBG_WOW_CPU_IO_RX_EN);
	} else {
		ret = rtw89_mac_resize_ple_rx_quota(rtwdev, false);
		if (ret) {
			rtw89_err(rtwdev, "[ERR]patch rx qta %d\n", ret);
			return ret;
		}

		rtw89_mac_cpu_io_rx(rtwdev, enable_wow);
		rtw89_write32_clr(rtwdev, R_AX_RX_FUNCTION_STOP, B_AX_HDR_RX_STOP);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
		rtw89_write32(rtwdev, R_AX_ACTION_FWD0, TRXCFG_MPDU_PROC_ACT_FRWD);
		rtw89_write32(rtwdev, R_AX_TF_FWD, TRXCFG_MPDU_PROC_TF_FRWD);
	}

	return 0;
}

static u8 rtw89_fw_get_rdy_ax(struct rtw89_dev *rtwdev, enum rtw89_fwdl_check_type type)
{
	u8 val = rtw89_read8(rtwdev, R_AX_WCPU_FW_CTRL);

	return FIELD_GET(B_AX_WCPU_FWDL_STS_MASK, val);
}

static
int rtw89_fwdl_check_path_ready_ax(struct rtw89_dev *rtwdev,
				   bool h2c_or_fwdl)
{
	u8 check = h2c_or_fwdl ? B_AX_H2C_PATH_RDY : B_AX_FWDL_PATH_RDY;
	u8 val;

	return read_poll_timeout_atomic(rtw89_read8, val, val & check,
					1, FWDL_WAIT_CNT, false,
					rtwdev, R_AX_WCPU_FW_CTRL);
}

const struct rtw89_mac_gen_def rtw89_mac_gen_ax = {
	.band1_offset = RTW89_MAC_AX_BAND_REG_OFFSET,
	.filter_model_addr = R_AX_FILTER_MODEL_ADDR,
	.indir_access_addr = R_AX_INDIR_ACCESS_ENTRY,
	.mem_base_addrs = rtw89_mac_mem_base_addrs_ax,
	.rx_fltr = R_AX_RX_FLTR_OPT,
	.port_base = &rtw89_port_base_ax,
	.agg_len_ht = R_AX_AGG_LEN_HT_0,
	.ps_status = R_AX_PPWRBIT_SETTING,

	.muedca_ctrl = {
		.addr = R_AX_MUEDCA_EN,
		.mask = B_AX_MUEDCA_EN_0 | B_AX_SET_MUEDCATIMER_TF_0,
	},
	.bfee_ctrl = {
		.addr = R_AX_BFMEE_RESP_OPTION,
		.mask = B_AX_BFMEE_HT_NDPA_EN | B_AX_BFMEE_VHT_NDPA_EN |
			B_AX_BFMEE_HE_NDPA_EN,
	},
	.narrow_bw_ru_dis = {
		.addr = R_AX_RXTRIG_TEST_USER_2,
		.mask = B_AX_RXTRIG_RU26_DIS,
	},
	.wow_ctrl = {.addr = R_AX_WOW_CTRL, .mask = B_AX_WOW_WOWEN,},

	.check_mac_en = rtw89_mac_check_mac_en_ax,
	.sys_init = sys_init_ax,
	.trx_init = trx_init_ax,
	.hci_func_en = rtw89_mac_hci_func_en_ax,
	.dmac_func_pre_en = rtw89_mac_dmac_func_pre_en_ax,
	.dle_func_en = dle_func_en_ax,
	.dle_clk_en = dle_clk_en_ax,
	.bf_assoc = rtw89_mac_bf_assoc_ax,

	.typ_fltr_opt = rtw89_mac_typ_fltr_opt_ax,
	.cfg_ppdu_status = rtw89_mac_cfg_ppdu_status_ax,

	.dle_mix_cfg = dle_mix_cfg_ax,
	.chk_dle_rdy = chk_dle_rdy_ax,
	.dle_buf_req = dle_buf_req_ax,
	.hfc_func_en = hfc_func_en_ax,
	.hfc_h2c_cfg = hfc_h2c_cfg_ax,
	.hfc_mix_cfg = hfc_mix_cfg_ax,
	.hfc_get_mix_info = hfc_get_mix_info_ax,
	.wde_quota_cfg = wde_quota_cfg_ax,
	.ple_quota_cfg = ple_quota_cfg_ax,
	.set_cpuio = set_cpuio_ax,
	.dle_quota_change = dle_quota_change_ax,

	.disable_cpu = rtw89_mac_disable_cpu_ax,
	.fwdl_enable_wcpu = rtw89_mac_enable_cpu_ax,
	.fwdl_get_status = rtw89_fw_get_rdy_ax,
	.fwdl_check_path_ready = rtw89_fwdl_check_path_ready_ax,
	.parse_efuse_map = rtw89_parse_efuse_map_ax,
	.parse_phycap_map = rtw89_parse_phycap_map_ax,
	.cnv_efuse_state = rtw89_cnv_efuse_state_ax,

	.cfg_plt = rtw89_mac_cfg_plt_ax,
	.get_plt_cnt = rtw89_mac_get_plt_cnt_ax,

	.get_txpwr_cr = rtw89_mac_get_txpwr_cr_ax,

	.write_xtal_si = rtw89_mac_write_xtal_si_ax,
	.read_xtal_si = rtw89_mac_read_xtal_si_ax,

	.dump_qta_lost = rtw89_mac_dump_qta_lost_ax,
	.dump_err_status = rtw89_mac_dump_err_status_ax,

	.is_txq_empty = mac_is_txq_empty_ax,

	.add_chan_list = rtw89_hw_scan_add_chan_list,
	.scan_offload = rtw89_fw_h2c_scan_offload,

	.wow_config_mac = rtw89_wow_config_mac_ax,
};
EXPORT_SYMBOL(rtw89_mac_gen_ax);
