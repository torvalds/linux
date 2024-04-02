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
#include "phy-qcom-qmp-qserdes-txrx-v6_n4.h"
#include "phy-qcom-qmp-qserdes-ln-shrd-v6.h"

#include "phy-qcom-qmp-qserdes-com-v7.h"
#include "phy-qcom-qmp-qserdes-txrx-v7.h"

#include "phy-qcom-qmp-qserdes-pll.h"

#include "phy-qcom-qmp-pcs-v2.h"

#include "phy-qcom-qmp-pcs-v3.h"

#include "phy-qcom-qmp-pcs-v4.h"

#include "phy-qcom-qmp-pcs-v4_20.h"

#include "phy-qcom-qmp-pcs-v5.h"

#include "phy-qcom-qmp-pcs-v5_20.h"

#include "phy-qcom-qmp-pcs-v6.h"

#include "phy-qcom-qmp-pcs-v6_20.h"

#include "phy-qcom-qmp-pcs-v7.h"

/* QPHY_SW_RESET bit */
#define SW_RESET				BIT(0)
/* QPHY_POWER_DOWN_CONTROL */
#define SW_PWRDN				BIT(0)
#define REFCLK_DRV_DSBL				BIT(1) /* PCIe */

/* QPHY_START_CONTROL bits */
#define SERDES_START				BIT(0)
#define PCS_START				BIT(1)

/* QPHY_PCS_STATUS bit */
#define PHYSTATUS				BIT(6)
#define PHYSTATUS_4_20				BIT(7)

/* QPHY_PCS_AUTONOMOUS_MODE_CTRL register bits */
#define ARCVR_DTCT_EN				BIT(0)
#define ALFPS_DTCT_EN				BIT(1)
#define ARCVR_DTCT_EVENT_SEL			BIT(4)

/* QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR register bits */
#define IRQ_CLEAR				BIT(0)

/* QPHY_PCS_MISC_CLAMP_ENABLE register bits */
#define CLAMP_EN				BIT(0) /* enables i/o clamp_n */

#endif
