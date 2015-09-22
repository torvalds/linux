/* Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef UFS_QCOM_H_
#define UFS_QCOM_H_

#define MAX_UFS_QCOM_HOSTS	1
#define MAX_U32                 (~(u32)0)
#define MPHY_TX_FSM_STATE       0x41
#define TX_FSM_HIBERN8          0x1
#define HBRN8_POLL_TOUT_MS      100
#define DEFAULT_CLK_RATE_HZ     1000000
#define BUS_VECTOR_NAME_LEN     32

#define UFS_HW_VER_MAJOR_SHFT	(28)
#define UFS_HW_VER_MAJOR_MASK	(0x000F << UFS_HW_VER_MAJOR_SHFT)
#define UFS_HW_VER_MINOR_SHFT	(16)
#define UFS_HW_VER_MINOR_MASK	(0x0FFF << UFS_HW_VER_MINOR_SHFT)
#define UFS_HW_VER_STEP_SHFT	(0)
#define UFS_HW_VER_STEP_MASK	(0xFFFF << UFS_HW_VER_STEP_SHFT)

/* vendor specific pre-defined parameters */
#define SLOW 1
#define FAST 2

#define UFS_QCOM_LIMIT_NUM_LANES_RX	2
#define UFS_QCOM_LIMIT_NUM_LANES_TX	2
#define UFS_QCOM_LIMIT_HSGEAR_RX	UFS_HS_G2
#define UFS_QCOM_LIMIT_HSGEAR_TX	UFS_HS_G2
#define UFS_QCOM_LIMIT_PWMGEAR_RX	UFS_PWM_G4
#define UFS_QCOM_LIMIT_PWMGEAR_TX	UFS_PWM_G4
#define UFS_QCOM_LIMIT_RX_PWR_PWM	SLOW_MODE
#define UFS_QCOM_LIMIT_TX_PWR_PWM	SLOW_MODE
#define UFS_QCOM_LIMIT_RX_PWR_HS	FAST_MODE
#define UFS_QCOM_LIMIT_TX_PWR_HS	FAST_MODE
#define UFS_QCOM_LIMIT_HS_RATE		PA_HS_MODE_B
#define UFS_QCOM_LIMIT_DESIRED_MODE	FAST

/* QCOM UFS host controller vendor specific registers */
enum {
	REG_UFS_SYS1CLK_1US                 = 0xC0,
	REG_UFS_TX_SYMBOL_CLK_NS_US         = 0xC4,
	REG_UFS_LOCAL_PORT_ID_REG           = 0xC8,
	REG_UFS_PA_ERR_CODE                 = 0xCC,
	REG_UFS_RETRY_TIMER_REG             = 0xD0,
	REG_UFS_PA_LINK_STARTUP_TIMER       = 0xD8,
	REG_UFS_CFG1                        = 0xDC,
	REG_UFS_CFG2                        = 0xE0,
	REG_UFS_HW_VERSION                  = 0xE4,

	UFS_DBG_RD_REG_UAWM			= 0x100,
	UFS_DBG_RD_REG_UARM			= 0x200,
	UFS_DBG_RD_REG_TXUC			= 0x300,
	UFS_DBG_RD_REG_RXUC			= 0x400,
	UFS_DBG_RD_REG_DFC			= 0x500,
	UFS_DBG_RD_REG_TRLUT			= 0x600,
	UFS_DBG_RD_REG_TMRLUT			= 0x700,
	UFS_UFS_DBG_RD_REG_OCSC			= 0x800,

	UFS_UFS_DBG_RD_DESC_RAM			= 0x1500,
	UFS_UFS_DBG_RD_PRDT_RAM			= 0x1700,
	UFS_UFS_DBG_RD_RESP_RAM			= 0x1800,
	UFS_UFS_DBG_RD_EDTL_RAM			= 0x1900,
};

/* bit definitions for REG_UFS_CFG2 register */
#define UAWM_HW_CGC_EN		(1 << 0)
#define UARM_HW_CGC_EN		(1 << 1)
#define TXUC_HW_CGC_EN		(1 << 2)
#define RXUC_HW_CGC_EN		(1 << 3)
#define DFC_HW_CGC_EN		(1 << 4)
#define TRLUT_HW_CGC_EN		(1 << 5)
#define TMRLUT_HW_CGC_EN	(1 << 6)
#define OCSC_HW_CGC_EN		(1 << 7)

#define REG_UFS_CFG2_CGC_EN_ALL (UAWM_HW_CGC_EN | UARM_HW_CGC_EN |\
				 TXUC_HW_CGC_EN | RXUC_HW_CGC_EN |\
				 DFC_HW_CGC_EN | TRLUT_HW_CGC_EN |\
				 TMRLUT_HW_CGC_EN | OCSC_HW_CGC_EN)

/* bit offset */
enum {
	OFFSET_UFS_PHY_SOFT_RESET           = 1,
	OFFSET_CLK_NS_REG                   = 10,
};

/* bit masks */
enum {
	MASK_UFS_PHY_SOFT_RESET             = 0x2,
	MASK_TX_SYMBOL_CLK_1US_REG          = 0x3FF,
	MASK_CLK_NS_REG                     = 0xFFFC00,
};

enum ufs_qcom_phy_init_type {
	UFS_PHY_INIT_FULL,
	UFS_PHY_INIT_CFG_RESTORE,
};

static inline void
ufs_qcom_get_controller_revision(struct ufs_hba *hba,
				 u8 *major, u16 *minor, u16 *step)
{
	u32 ver = ufshcd_readl(hba, REG_UFS_HW_VERSION);

	*major = (ver & UFS_HW_VER_MAJOR_MASK) >> UFS_HW_VER_MAJOR_SHFT;
	*minor = (ver & UFS_HW_VER_MINOR_MASK) >> UFS_HW_VER_MINOR_SHFT;
	*step = (ver & UFS_HW_VER_STEP_MASK) >> UFS_HW_VER_STEP_SHFT;
};

static inline void ufs_qcom_assert_reset(struct ufs_hba *hba)
{
	ufshcd_rmwl(hba, MASK_UFS_PHY_SOFT_RESET,
			1 << OFFSET_UFS_PHY_SOFT_RESET, REG_UFS_CFG1);

	/*
	 * Make sure assertion of ufs phy reset is written to
	 * register before returning
	 */
	mb();
}

static inline void ufs_qcom_deassert_reset(struct ufs_hba *hba)
{
	ufshcd_rmwl(hba, MASK_UFS_PHY_SOFT_RESET,
			0 << OFFSET_UFS_PHY_SOFT_RESET, REG_UFS_CFG1);

	/*
	 * Make sure de-assertion of ufs phy reset is written to
	 * register before returning
	 */
	mb();
}

struct ufs_qcom_bus_vote {
	uint32_t client_handle;
	uint32_t curr_vote;
	int min_bw_vote;
	int max_bw_vote;
	int saved_vote;
	bool is_max_bw_needed;
	struct device_attribute max_bus_bw;
};

/* Host controller hardware version: major.minor.step */
struct ufs_hw_version {
	u16 step;
	u16 minor;
	u8 major;
};
struct ufs_qcom_host {

	/*
	 * Set this capability if host controller supports the QUniPro mode
	 * and if driver wants the Host controller to operate in QUniPro mode.
	 * Note: By default this capability will be kept enabled if host
	 * controller supports the QUniPro mode.
	 */
	#define UFS_QCOM_CAP_QUNIPRO	UFS_BIT(0)
	u32 caps;

	struct phy *generic_phy;
	struct ufs_hba *hba;
	struct ufs_qcom_bus_vote bus_vote;
	struct ufs_pa_layer_attr dev_req_params;
	struct clk *rx_l0_sync_clk;
	struct clk *tx_l0_sync_clk;
	struct clk *rx_l1_sync_clk;
	struct clk *tx_l1_sync_clk;
	bool is_lane_clks_enabled;

	struct ufs_hw_version hw_ver;
};

#define ufs_qcom_is_link_off(hba) ufshcd_is_link_off(hba)
#define ufs_qcom_is_link_active(hba) ufshcd_is_link_active(hba)
#define ufs_qcom_is_link_hibern8(hba) ufshcd_is_link_hibern8(hba)

static inline bool ufs_qcom_cap_qunipro(struct ufs_qcom_host *host)
{
	if (host->caps & UFS_QCOM_CAP_QUNIPRO)
		return true;
	else
		return false;
}

#endif /* UFS_QCOM_H_ */
