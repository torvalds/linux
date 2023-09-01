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

const struct rtw89_mac_gen_def rtw89_mac_gen_be = {
	.band1_offset = RTW89_MAC_BE_BAND_REG_OFFSET,
	.filter_model_addr = R_BE_FILTER_MODEL_ADDR,
	.indir_access_addr = R_BE_INDIR_ACCESS_ENTRY,
	.mem_base_addrs = rtw89_mac_mem_base_addrs_be,
	.rx_fltr = R_BE_RX_FLTR_OPT,

	.disable_cpu = rtw89_mac_disable_cpu_be,
	.fwdl_enable_wcpu = rtw89_mac_fwdl_enable_wcpu_be,
	.fwdl_get_status = fwdl_get_status_be,
	.fwdl_check_path_ready = rtw89_fwdl_check_path_ready_be,
};
EXPORT_SYMBOL(rtw89_mac_gen_be);
