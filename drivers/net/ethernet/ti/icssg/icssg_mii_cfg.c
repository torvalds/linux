// SPDX-License-Identifier: GPL-2.0
/* Texas Instruments ICSSG Ethernet Driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#include <linux/etherdevice.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "icssg_mii_rt.h"
#include "icssg_prueth.h"

void icssg_mii_update_ipg(struct regmap *mii_rt, int mii, u32 ipg)
{
	u32 val;

	if (mii == ICSS_MII0) {
		regmap_write(mii_rt, PRUSS_MII_RT_TX_IPG0, ipg);
	} else {
		regmap_read(mii_rt, PRUSS_MII_RT_TX_IPG0, &val);
		regmap_write(mii_rt, PRUSS_MII_RT_TX_IPG1, ipg);
		regmap_write(mii_rt, PRUSS_MII_RT_TX_IPG0, val);
	}
}

void icssg_mii_update_mtu(struct regmap *mii_rt, int mii, int mtu)
{
	mtu += (ETH_HLEN + ETH_FCS_LEN);
	if (mii == ICSS_MII0) {
		regmap_update_bits(mii_rt,
				   PRUSS_MII_RT_RX_FRMS0,
				   PRUSS_MII_RT_RX_FRMS_MAX_FRM_MASK,
				   (mtu - 1) << PRUSS_MII_RT_RX_FRMS_MAX_FRM_SHIFT);
	} else {
		regmap_update_bits(mii_rt,
				   PRUSS_MII_RT_RX_FRMS1,
				   PRUSS_MII_RT_RX_FRMS_MAX_FRM_MASK,
				   (mtu - 1) << PRUSS_MII_RT_RX_FRMS_MAX_FRM_SHIFT);
	}
}
EXPORT_SYMBOL_GPL(icssg_mii_update_mtu);

void icssg_update_rgmii_cfg(struct regmap *miig_rt, struct prueth_emac *emac)
{
	u32 gig_en_mask, gig_val = 0, full_duplex_mask, full_duplex_val = 0;
	int slice = prueth_emac_slice(emac);
	u32 inband_en_mask, inband_val = 0;

	gig_en_mask = (slice == ICSS_MII0) ? RGMII_CFG_GIG_EN_MII0 :
					RGMII_CFG_GIG_EN_MII1;
	if (emac->speed == SPEED_1000)
		gig_val = gig_en_mask;
	regmap_update_bits(miig_rt, RGMII_CFG_OFFSET, gig_en_mask, gig_val);

	inband_en_mask = (slice == ICSS_MII0) ? RGMII_CFG_INBAND_EN_MII0 :
					RGMII_CFG_INBAND_EN_MII1;
	if (emac->speed == SPEED_10 && phy_interface_mode_is_rgmii(emac->phy_if))
		inband_val = inband_en_mask;
	regmap_update_bits(miig_rt, RGMII_CFG_OFFSET, inband_en_mask, inband_val);

	full_duplex_mask = (slice == ICSS_MII0) ? RGMII_CFG_FULL_DUPLEX_MII0 :
					   RGMII_CFG_FULL_DUPLEX_MII1;
	if (emac->duplex == DUPLEX_FULL)
		full_duplex_val = full_duplex_mask;
	regmap_update_bits(miig_rt, RGMII_CFG_OFFSET, full_duplex_mask,
			   full_duplex_val);
}
EXPORT_SYMBOL_GPL(icssg_update_rgmii_cfg);

void icssg_miig_set_interface_mode(struct regmap *miig_rt, int mii, phy_interface_t phy_if)
{
	u32 val, mask, shift;

	mask = mii == ICSS_MII0 ? ICSSG_CFG_MII0_MODE : ICSSG_CFG_MII1_MODE;
	shift =  mii == ICSS_MII0 ? ICSSG_CFG_MII0_MODE_SHIFT : ICSSG_CFG_MII1_MODE_SHIFT;

	val = MII_MODE_RGMII;
	if (phy_if == PHY_INTERFACE_MODE_MII)
		val = MII_MODE_MII;

	val <<= shift;
	regmap_update_bits(miig_rt, ICSSG_CFG_OFFSET, mask, val);
	regmap_read(miig_rt, ICSSG_CFG_OFFSET, &val);
}

u32 icssg_rgmii_cfg_get_bitfield(struct regmap *miig_rt, u32 mask, u32 shift)
{
	u32 val;

	regmap_read(miig_rt, RGMII_CFG_OFFSET, &val);
	val &= mask;
	val >>= shift;

	return val;
}

u32 icssg_rgmii_get_speed(struct regmap *miig_rt, int mii)
{
	u32 shift = RGMII_CFG_SPEED_MII0_SHIFT, mask = RGMII_CFG_SPEED_MII0;

	if (mii == ICSS_MII1) {
		shift = RGMII_CFG_SPEED_MII1_SHIFT;
		mask = RGMII_CFG_SPEED_MII1;
	}

	return icssg_rgmii_cfg_get_bitfield(miig_rt, mask, shift);
}
EXPORT_SYMBOL_GPL(icssg_rgmii_get_speed);

u32 icssg_rgmii_get_fullduplex(struct regmap *miig_rt, int mii)
{
	u32 shift = RGMII_CFG_FULLDUPLEX_MII0_SHIFT;
	u32 mask = RGMII_CFG_FULLDUPLEX_MII0;

	if (mii == ICSS_MII1) {
		shift = RGMII_CFG_FULLDUPLEX_MII1_SHIFT;
		mask = RGMII_CFG_FULLDUPLEX_MII1;
	}

	return icssg_rgmii_cfg_get_bitfield(miig_rt, mask, shift);
}
EXPORT_SYMBOL_GPL(icssg_rgmii_get_fullduplex);
