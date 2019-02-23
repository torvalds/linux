/* SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 */

#define PORTS_PER_CONTROLLER		4

struct xlr_net_data {
	int cpu_mask;
	u32 __iomem *mii_addr;
	u32 __iomem *serdes_addr;
	u32 __iomem *pcs_addr;
	u32 __iomem *gpio_addr;
	int phy_interface;
	int rfr_station;
	int tx_stnid[PORTS_PER_CONTROLLER];
	int *bucket_size;
	int phy_addr[PORTS_PER_CONTROLLER];
	struct xlr_fmn_info *gmac_fmn_info;
};
