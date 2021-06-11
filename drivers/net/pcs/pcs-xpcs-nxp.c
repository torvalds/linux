// SPDX-License-Identifier: GPL-2.0
/* Copyright 2021 NXP Semiconductors
 */
#include <linux/pcs/pcs-xpcs.h>
#include "pcs-xpcs.h"

/* In NXP SJA1105, the PCS is integrated with a PMA that has the TX lane
 * polarity inverted by default (PLUS is MINUS, MINUS is PLUS). To obtain
 * normal non-inverted behavior, the TX lane polarity must be inverted in the
 * PCS, via the DIGITAL_CONTROL_2 register.
 */
int nxp_sja1105_sgmii_pma_config(struct dw_xpcs *xpcs)
{
	return xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_DIG_CTRL2,
			  DW_VR_MII_DIG_CTRL2_TX_POL_INV);
}
