/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2018 Microchip Technology Inc. */

#ifndef _LAN743X_ETHTOOL_H
#define _LAN743X_ETHTOOL_H

#include "linux/ethtool.h"

#define LAN743X_ETH_REG_VERSION         1

enum {
	ETH_PRIV_FLAGS,
	ETH_ID_REV,
	ETH_FPGA_REV,
	ETH_STRAP_READ,
	ETH_INT_STS,
	ETH_HW_CFG,
	ETH_PMT_CTL,
	ETH_E2P_CMD,
	ETH_E2P_DATA,
	ETH_MAC_CR,
	ETH_MAC_RX,
	ETH_MAC_TX,
	ETH_FLOW,
	ETH_MII_ACC,
	ETH_MII_DATA,
	ETH_EEE_TX_LPI_REQ_DLY,
	ETH_WUCSR,
	ETH_WK_SRC,

	/* Add new registers above */
	MAX_LAN743X_ETH_REGS
};

extern const struct ethtool_ops lan743x_ethtool_ops;

#endif /* _LAN743X_ETHTOOL_H */
