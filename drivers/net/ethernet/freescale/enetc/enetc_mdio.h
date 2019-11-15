/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2019 NXP */

#include <linux/phy.h>
#include "enetc_pf.h"

struct enetc_mdio_priv {
	struct enetc_hw *hw;
};

int enetc_mdio_write(struct mii_bus *bus, int phy_id, int regnum, u16 value);
int enetc_mdio_read(struct mii_bus *bus, int phy_id, int regnum);
