/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 */

#ifndef QCOM_PHY_QMP_H_
#define QCOM_PHY_QMP_H_

#include "phy-qcom-qmp-qserdes-com.h"
#include "phy-qcom-qmp-qserdes-txrx.h"

#include "phy-qcom-qmp-qserdes-com-v3.h"
#include "phy-qcom-qmp-qserdes-txrx-v3.h"

#include "phy-qcom-qmp-qserdes-com-v4.h"
#include "phy-qcom-qmp-qserdes-txrx-v4.h"
#include "phy-qcom-qmp-qserdes-txrx-v4_20.h"

#include "phy-qcom-qmp-qserdes-com-v5.h"
#include "phy-qcom-qmp-qserdes-txrx-v5.h"
#include "phy-qcom-qmp-qserdes-txrx-v5_20.h"
#include "phy-qcom-qmp-qserdes-txrx-v5_5nm.h"

#include "phy-qcom-qmp-qserdes-com-v6.h"
#include "phy-qcom-qmp-qserdes-txrx-v6.h"
#include "phy-qcom-qmp-qserdes-txrx-v6_20.h"
#include "phy-qcom-qmp-qserdes-ln-shrd-v6.h"

#include "phy-qcom-qmp-qserdes-pll.h"

#include "phy-qcom-qmp-pcs-v2.h"

#include "phy-qcom-qmp-pcs-v3.h"

#include "phy-qcom-qmp-pcs-v4.h"

#include "phy-qcom-qmp-pcs-v4_20.h"

#include "phy-qcom-qmp-pcs-v5.h"

#include "phy-qcom-qmp-pcs-v5_20.h"

#include "phy-qcom-qmp-pcs-v6.h"

#include "phy-qcom-qmp-pcs-v6_20.h"

/* Only for QMP V3 & V4 PHY - DP COM registers */
#define QPHY_V3_DP_COM_PHY_MODE_CTRL			0x00
#define QPHY_V3_DP_COM_SW_RESET				0x04
#define QPHY_V3_DP_COM_POWER_DOWN_CTRL			0x08
#define QPHY_V3_DP_COM_SWI_CTRL				0x0c
#define QPHY_V3_DP_COM_TYPEC_CTRL			0x10
#define QPHY_V3_DP_COM_TYPEC_PWRDN_CTRL			0x14
#define QPHY_V3_DP_COM_RESET_OVRD_CTRL			0x1c

/* QSERDES V3 COM bits */
# define QSERDES_V3_COM_BIAS_EN				0x0001
# define QSERDES_V3_COM_BIAS_EN_MUX			0x0002
# define QSERDES_V3_COM_CLKBUF_R_EN			0x0004
# define QSERDES_V3_COM_CLKBUF_L_EN			0x0008
# define QSERDES_V3_COM_EN_SYSCLK_TX_SEL		0x0010
# define QSERDES_V3_COM_CLKBUF_RX_DRIVE_L		0x0020
# define QSERDES_V3_COM_CLKBUF_RX_DRIVE_R		0x0040

/* QSERDES V3 TX bits */
# define DP_PHY_TXn_TX_EMP_POST1_LVL_MASK		0x001f
# define DP_PHY_TXn_TX_EMP_POST1_LVL_MUX_EN		0x0020
# define DP_PHY_TXn_TX_DRV_LVL_MASK			0x001f
# define DP_PHY_TXn_TX_DRV_LVL_MUX_EN			0x0020

/* QMP PHY - DP PHY registers */
#define QSERDES_DP_PHY_REVISION_ID0			0x000
#define QSERDES_DP_PHY_REVISION_ID1			0x004
#define QSERDES_DP_PHY_REVISION_ID2			0x008
#define QSERDES_DP_PHY_REVISION_ID3			0x00c
#define QSERDES_DP_PHY_CFG				0x010
#define QSERDES_DP_PHY_PD_CTL				0x018
# define DP_PHY_PD_CTL_PWRDN				0x001
# define DP_PHY_PD_CTL_PSR_PWRDN			0x002
# define DP_PHY_PD_CTL_AUX_PWRDN			0x004
# define DP_PHY_PD_CTL_LANE_0_1_PWRDN			0x008
# define DP_PHY_PD_CTL_LANE_2_3_PWRDN			0x010
# define DP_PHY_PD_CTL_PLL_PWRDN			0x020
# define DP_PHY_PD_CTL_DP_CLAMP_EN			0x040
#define QSERDES_DP_PHY_MODE				0x01c
#define QSERDES_DP_PHY_AUX_CFG0				0x020
#define QSERDES_DP_PHY_AUX_CFG1				0x024
#define QSERDES_DP_PHY_AUX_CFG2				0x028
#define QSERDES_DP_PHY_AUX_CFG3				0x02c
#define QSERDES_DP_PHY_AUX_CFG4				0x030
#define QSERDES_DP_PHY_AUX_CFG5				0x034
#define QSERDES_DP_PHY_AUX_CFG6				0x038
#define QSERDES_DP_PHY_AUX_CFG7				0x03c
#define QSERDES_DP_PHY_AUX_CFG8				0x040
#define QSERDES_DP_PHY_AUX_CFG9				0x044

/* Only for QMP V3 PHY - DP PHY registers */
#define QSERDES_V3_DP_PHY_AUX_INTERRUPT_MASK		0x048
# define PHY_AUX_STOP_ERR_MASK				0x01
# define PHY_AUX_DEC_ERR_MASK				0x02
# define PHY_AUX_SYNC_ERR_MASK				0x04
# define PHY_AUX_ALIGN_ERR_MASK				0x08
# define PHY_AUX_REQ_ERR_MASK				0x10

#define QSERDES_V3_DP_PHY_AUX_INTERRUPT_CLEAR		0x04c
#define QSERDES_V3_DP_PHY_AUX_BIST_CFG			0x050

#define QSERDES_V3_DP_PHY_VCO_DIV			0x064
#define QSERDES_V3_DP_PHY_TX0_TX1_LANE_CTL		0x06c
#define QSERDES_V3_DP_PHY_TX2_TX3_LANE_CTL		0x088

#define QSERDES_V3_DP_PHY_SPARE0			0x0ac
#define DP_PHY_SPARE0_MASK				0x0f
#define DP_PHY_SPARE0_ORIENTATION_INFO_SHIFT		0x04(0x0004)

#define QSERDES_V3_DP_PHY_STATUS			0x0c0

/* Only for QMP V4 PHY - DP PHY registers */
#define QSERDES_V4_DP_PHY_CFG_1				0x014
#define QSERDES_V4_DP_PHY_AUX_INTERRUPT_MASK		0x054
#define QSERDES_V4_DP_PHY_AUX_INTERRUPT_CLEAR		0x058
#define QSERDES_V4_DP_PHY_VCO_DIV			0x070
#define QSERDES_V4_DP_PHY_TX0_TX1_LANE_CTL		0x078
#define QSERDES_V4_DP_PHY_TX2_TX3_LANE_CTL		0x09c
#define QSERDES_V4_DP_PHY_SPARE0			0x0c8
#define QSERDES_V4_DP_PHY_AUX_INTERRUPT_STATUS		0x0d8
#define QSERDES_V4_DP_PHY_STATUS			0x0dc

/* Only for QMP V4 PHY - PCS_MISC registers */
#define QPHY_V4_PCS_MISC_TYPEC_CTRL			0x00
#define QPHY_V4_PCS_MISC_TYPEC_PWRDN_CTRL		0x04
#define QPHY_V4_PCS_MISC_PCS_MISC_CONFIG1		0x08
#define QPHY_V4_PCS_MISC_CLAMP_ENABLE			0x0c
#define QPHY_V4_PCS_MISC_TYPEC_STATUS			0x10
#define QPHY_V4_PCS_MISC_PLACEHOLDER_STATUS		0x14

/* Only for QMP V6 PHY - DP PHY registers */
#define QSERDES_V6_DP_PHY_AUX_INTERRUPT_STATUS		0x0e0
#define QSERDES_V6_DP_PHY_STATUS			0x0e4

#endif
