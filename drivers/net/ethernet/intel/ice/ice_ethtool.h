/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _ICE_ETHTOOL_H_
#define _ICE_ETHTOOL_H_

struct ice_phy_type_to_ethtool {
	u64 aq_link_speed;
	u8 link_mode;
};

/* Macro to make PHY type to Ethtool link mode table entry.
 * The index is the PHY type.
 */
#define ICE_PHY_TYPE(LINK_SPEED, ETHTOOL_LINK_MODE) {\
	.aq_link_speed = ICE_AQ_LINK_SPEED_##LINK_SPEED, \
	.link_mode = ETHTOOL_LINK_MODE_##ETHTOOL_LINK_MODE##_BIT, \
}

/* Lookup table mapping PHY type low to link speed and Ethtool link modes.
 * Array index corresponds to HW PHY type bit, see
 * ice_adminq_cmd.h:ICE_PHY_TYPE_LOW_*.
 */
static const struct ice_phy_type_to_ethtool
phy_type_low_lkup[] = {
	[0] = ICE_PHY_TYPE(100MB, 100baseT_Full),
	[1] = ICE_PHY_TYPE(100MB, 100baseT_Full),
	[2] = ICE_PHY_TYPE(1000MB, 1000baseT_Full),
	[3] = ICE_PHY_TYPE(1000MB, 1000baseX_Full),
	[4] = ICE_PHY_TYPE(1000MB, 1000baseX_Full),
	[5] = ICE_PHY_TYPE(1000MB, 1000baseKX_Full),
	[6] = ICE_PHY_TYPE(1000MB, 1000baseT_Full),
	[7] = ICE_PHY_TYPE(2500MB, 2500baseT_Full),
	[8] = ICE_PHY_TYPE(2500MB, 2500baseX_Full),
	[9] = ICE_PHY_TYPE(2500MB, 2500baseX_Full),
	[10] = ICE_PHY_TYPE(5GB, 5000baseT_Full),
	[11] = ICE_PHY_TYPE(5GB, 5000baseT_Full),
	[12] = ICE_PHY_TYPE(10GB, 10000baseT_Full),
	[13] = ICE_PHY_TYPE(10GB, 10000baseCR_Full),
	[14] = ICE_PHY_TYPE(10GB, 10000baseSR_Full),
	[15] = ICE_PHY_TYPE(10GB, 10000baseLR_Full),
	[16] = ICE_PHY_TYPE(10GB, 10000baseKR_Full),
	[17] = ICE_PHY_TYPE(10GB, 10000baseCR_Full),
	[18] = ICE_PHY_TYPE(10GB, 10000baseKR_Full),
	[19] = ICE_PHY_TYPE(25GB, 25000baseCR_Full),
	[20] = ICE_PHY_TYPE(25GB, 25000baseCR_Full),
	[21] = ICE_PHY_TYPE(25GB, 25000baseCR_Full),
	[22] = ICE_PHY_TYPE(25GB, 25000baseCR_Full),
	[23] = ICE_PHY_TYPE(25GB, 25000baseSR_Full),
	[24] = ICE_PHY_TYPE(25GB, 25000baseSR_Full),
	[25] = ICE_PHY_TYPE(25GB, 25000baseKR_Full),
	[26] = ICE_PHY_TYPE(25GB, 25000baseKR_Full),
	[27] = ICE_PHY_TYPE(25GB, 25000baseKR_Full),
	[28] = ICE_PHY_TYPE(25GB, 25000baseSR_Full),
	[29] = ICE_PHY_TYPE(25GB, 25000baseCR_Full),
	[30] = ICE_PHY_TYPE(40GB, 40000baseCR4_Full),
	[31] = ICE_PHY_TYPE(40GB, 40000baseSR4_Full),
	[32] = ICE_PHY_TYPE(40GB, 40000baseLR4_Full),
	[33] = ICE_PHY_TYPE(40GB, 40000baseKR4_Full),
	[34] = ICE_PHY_TYPE(40GB, 40000baseSR4_Full),
	[35] = ICE_PHY_TYPE(40GB, 40000baseCR4_Full),
	[36] = ICE_PHY_TYPE(50GB, 50000baseCR2_Full),
	[37] = ICE_PHY_TYPE(50GB, 50000baseSR2_Full),
	[38] = ICE_PHY_TYPE(50GB, 50000baseSR2_Full),
	[39] = ICE_PHY_TYPE(50GB, 50000baseKR2_Full),
	[40] = ICE_PHY_TYPE(50GB, 50000baseSR2_Full),
	[41] = ICE_PHY_TYPE(50GB, 50000baseCR2_Full),
	[42] = ICE_PHY_TYPE(50GB, 50000baseSR2_Full),
	[43] = ICE_PHY_TYPE(50GB, 50000baseCR2_Full),
	[44] = ICE_PHY_TYPE(50GB, 50000baseCR_Full),
	[45] = ICE_PHY_TYPE(50GB, 50000baseSR_Full),
	[46] = ICE_PHY_TYPE(50GB, 50000baseLR_ER_FR_Full),
	[47] = ICE_PHY_TYPE(50GB, 50000baseLR_ER_FR_Full),
	[48] = ICE_PHY_TYPE(50GB, 50000baseKR_Full),
	[49] = ICE_PHY_TYPE(50GB, 50000baseSR_Full),
	[50] = ICE_PHY_TYPE(50GB, 50000baseCR_Full),
	[51] = ICE_PHY_TYPE(100GB, 100000baseCR4_Full),
	[52] = ICE_PHY_TYPE(100GB, 100000baseSR4_Full),
	[53] = ICE_PHY_TYPE(100GB, 100000baseLR4_ER4_Full),
	[54] = ICE_PHY_TYPE(100GB, 100000baseKR4_Full),
	[55] = ICE_PHY_TYPE(100GB, 100000baseCR4_Full),
	[56] = ICE_PHY_TYPE(100GB, 100000baseCR4_Full),
	[57] = ICE_PHY_TYPE(100GB, 100000baseSR4_Full),
	[58] = ICE_PHY_TYPE(100GB, 100000baseCR4_Full),
	[59] = ICE_PHY_TYPE(100GB, 100000baseCR4_Full),
	[60] = ICE_PHY_TYPE(100GB, 100000baseKR4_Full),
	[61] = ICE_PHY_TYPE(100GB, 100000baseCR2_Full),
	[62] = ICE_PHY_TYPE(100GB, 100000baseSR2_Full),
	[63] = ICE_PHY_TYPE(100GB, 100000baseLR4_ER4_Full),
};

/* Lookup table mapping PHY type high to link speed and Ethtool link modes.
 * Array index corresponds to HW PHY type bit, see
 * ice_adminq_cmd.h:ICE_PHY_TYPE_HIGH_*
 */
static const struct ice_phy_type_to_ethtool
phy_type_high_lkup[] = {
	[0] = ICE_PHY_TYPE(100GB, 100000baseKR2_Full),
	[1] = ICE_PHY_TYPE(100GB, 100000baseSR2_Full),
	[2] = ICE_PHY_TYPE(100GB, 100000baseCR2_Full),
	[3] = ICE_PHY_TYPE(100GB, 100000baseSR2_Full),
	[4] = ICE_PHY_TYPE(100GB, 100000baseCR2_Full),
	[5] = ICE_PHY_TYPE(200GB, 200000baseCR4_Full),
	[6] = ICE_PHY_TYPE(200GB, 200000baseSR4_Full),
	[7] = ICE_PHY_TYPE(200GB, 200000baseLR4_ER4_FR4_Full),
	[8] = ICE_PHY_TYPE(200GB, 200000baseLR4_ER4_FR4_Full),
	[9] = ICE_PHY_TYPE(200GB, 200000baseDR4_Full),
	[10] = ICE_PHY_TYPE(200GB, 200000baseKR4_Full),
	[11] = ICE_PHY_TYPE(200GB, 200000baseSR4_Full),
	[12] = ICE_PHY_TYPE(200GB, 200000baseCR4_Full),
};

#endif /* !_ICE_ETHTOOL_H_ */
