/*
 * TC956x XPCS layer
 *
 * tc956x_xpcs.c
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  05 Nov 2020 : Initial version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *
 *  05 Jul 2021 : 1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
 *                2. XFI interface support and module parameters for selection of Port0 and Port1 interface
 *  VERSION     : 01-00-01
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  22 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported with module parameters
 *  VERSION     : 01-00-04
 *  26 Oct 2021 : 1. Added EEE configration for PHY and MAC Controlled Mode.
 *  VERSION     : 01-00-19
 *  25 Feb 2022 : 1. Helper function added for XPCS Rx LPI enable/disable
 *  VERSION     : 01-00-44
 */

#include "common.h"
#include "tc956xmac.h"
#include "tc956x_xpcs.h"
#ifdef TC956X
u32 tc956x_xpcs_read(void __iomem *xpcsaddr, u32 pcs_reg_num)
{
	u32 reg_value;
	u16 base_address, offset;

	base_address = pcs_reg_num >> XPCS_REG_BASE_ADDR;
	offset = pcs_reg_num & XPCS_REG_OFFSET;

	KPRINT_INFO("XPCS Indirect Access Base Register : %x, offset : %x", base_address, offset);
	/*write base address to (PCS address + 0x3FC) register*/
	writel(base_address, (xpcsaddr + XPCS_IND_ACCESS));

	/*Access to offset address (PCS address + offset)*/
	reg_value = readl(xpcsaddr + offset);
	KPRINT_INFO("XPCS register %x indirect read access value : %x", pcs_reg_num, reg_value);

	return reg_value;
}

u32 tc956x_xpcs_write(void __iomem *xpcsaddr, u32 pcs_reg_num, u32 value)
{
	u16 base_address, offset;

	base_address = pcs_reg_num >> XPCS_REG_BASE_ADDR;
	offset = pcs_reg_num & XPCS_REG_OFFSET;

	KPRINT_INFO("XPCS Indirect Access Base Register : %x, offset : %x", base_address, offset);
	/*write base address to (PCS address + 0x3FC) register*/
	writel(base_address, (xpcsaddr + XPCS_IND_ACCESS));

	/*Access to offset address (PCS address + offset)*/
	writel(value, xpcsaddr + offset);
	KPRINT_INFO("XPCS register %x indirect write access value : %x", pcs_reg_num, value);

	return 0;
}


int tc956x_xpcs_init(struct tc956xmac_priv *priv, void __iomem *xpcsaddr)
{
	u32 reg_value;

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_SR_MII_CTRL);
	if (reg_value & XGMAC_SOFT_RST)
		return -1;

#ifdef TC956X_MAGIC_PACKET_WOL_CONF
	if (priv->wol_config_enabled != true) {
#endif /* #ifdef TC956X_MAGIC_PACKET_WOL_CONF */
		/*Clause 37 autoneg related settings*/
		if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII) {
			//DK2
			//PCS Type Select SR_XS_PCS_CTRL2  PCS_TYPE_SEL -> 1
			reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_SR_XS_PCS_CTRL2);
			reg_value &= XGMAC_PCS_TYPE_SEL;
			reg_value |= 0x1;
			tc956x_xpcs_write(xpcsaddr, XGMAC_SR_XS_PCS_CTRL2, reg_value);

			reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_MII_AN_CTRL);
			reg_value &= XGMAC_PCS_MODE_MASK;
			reg_value |= XGMAC_SGMII_MODE;/*SGMII PCS MODE*/
			tc956x_xpcs_write(xpcsaddr, XGMAC_VR_MII_AN_CTRL, reg_value);

			if (priv->is_sgmii_2p5g == true) {
				reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1);
				reg_value &= ~(0x4);
				/* Enable only if SGMII 2.5G is enabled */
				reg_value |= 0x4; /*EN_2_5G_MODE*/
				tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1, reg_value);
			}
		}
		if ((priv->plat->interface == PHY_INTERFACE_MODE_USXGMII) ||
			(priv->plat->interface == PHY_INTERFACE_MODE_10GKR)) {
			reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_SR_XS_PCS_CTRL2);
			reg_value &= XGMAC_PCS_TYPE_SEL;/*PCS_TYPE_SEL as 10GBASE-R PCS */
			tc956x_xpcs_write(xpcsaddr, XGMAC_SR_XS_PCS_CTRL2, reg_value);

			reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1);
			if (priv->plat->interface == PHY_INTERFACE_MODE_10GKR) {
				reg_value &= (~XGMAC_USXG_EN); /*Disable USXG_EN*/
			} else {
				reg_value |= XGMAC_USXG_EN; /*set USXG_EN*/
			}

			tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1, reg_value);

			reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_KR_CTRL);
			reg_value &= ~XGMAC_USXG_MODE;/*USXG_MODE : 0x000*/
			tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_KR_CTRL, reg_value);

			reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1);
			reg_value |= XGMAC_VR_RST;/*set VR_RST*/
			tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1, reg_value);

			/*Wait for Reset to clear*/
			do {
				reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_DIG_CTRL1);
			} while ((XGMAC_VR_RST & reg_value) == XGMAC_VR_RST);

		}
#ifdef TC956X_MAGIC_PACKET_WOL_CONF
	} else { /* SerDES Configuration for WOL SGMII 1G when native interface other than SGMII. */
		KPRINT_INFO("%s Port %d : Entered with flag priv->wol_config_enabled %d", __func__, priv->port_num, priv->wol_config_enabled);
		reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_SR_XS_PCS_CTRL2);
			reg_value &= XGMAC_PCS_TYPE_SEL;
			reg_value |= 0x1;
			tc956x_xpcs_write(xpcsaddr, XGMAC_SR_XS_PCS_CTRL2, reg_value);

			reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_MII_AN_CTRL);
			reg_value &= XGMAC_PCS_MODE_MASK;
			reg_value |= XGMAC_SGMII_MODE;/*SGMII PCS MODE*/
			tc956x_xpcs_write(xpcsaddr, XGMAC_VR_MII_AN_CTRL, reg_value);
	}
#endif /* #ifdef TC956X_MAGIC_PACKET_WOL_CONF */
#ifdef EEE
	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_SR_XS_PCS_CTRL1);
	reg_value |= XGMAC_LPI_ENABLE;/* LPM : power down */
	tc956x_xpcs_write(xpcsaddr, XGMAC_SR_XS_PCS_CTRL1, reg_value);

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_DIG_STS);
	reg_value &= ~(XGMAC_PSEQ_STATE);/* PSEQ_STATE(B4:2)=3'b000 */
	tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_DIG_STS, reg_value);

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_SR_XS_PCS_CTRL1);
	reg_value &= ~(XGMAC_LPI_ENABLE);/* LPM : Normal Operation */
	tc956x_xpcs_write(xpcsaddr, XGMAC_SR_XS_PCS_CTRL1, reg_value);

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_SR_XS_PCS_EEE_ABL);
	reg_value |= XGMAC_KXEEE;/* KXEEE */
	tc956x_xpcs_write(xpcsaddr, XGMAC_SR_XS_PCS_EEE_ABL, reg_value);

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_EEE_MCTRL0);
	reg_value &= ~(XGMAC_MULT_FACT_100NS);
#ifdef EEE_MAC_CONTROLLED_MODE
	reg_value |= XGMAC_MULT_FACT_100NS_MAC; /* MULT_FACT_100NS */
#else
	reg_value |= XGMAC_MULT_FACT_100NS_PHY; /* MULT_FACT_100NS */
#endif
	reg_value |= XGMAC_SIGN_BIT;/* SIGN_BIT */
	reg_value |= XGMAC_TX_RX_EN;/* TX_EN_CTRL, RX_EN_CTRL */
	tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_EEE_MCTRL0, reg_value);

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_EEE_TXTIMER);
	reg_value &= ~(XGMAC_EEE_TX_TIMER);
#ifdef EEE_MAC_CONTROLLED_MODE
	reg_value |= XGMAC_EEE_TX_TIMER_MAC_CONT; /* TWL_RES=0x5, T1U_RES=0x1, TSL_RES=0x3 */
#else
	reg_value |= XGMAC_EEE_TX_TIMER_PHY_CONT; /* TWL_RES=0xe, T1U_RES=0x8, TSL_RES=0x1c */
#endif
	tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_EEE_TXTIMER, reg_value);

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_EEE_RXTIMER);
	reg_value &= ~(XGMAC_EEE_RX_TIMER);
#ifdef EEE_MAC_CONTROLLED_MODE
	reg_value |= XGMAC_EEE_RX_TIMER_MAC_CONT; /* TWR_RES=0x6, RES_100U=0x42 */
#else
	reg_value |= XGMAC_EEE_RX_TIMER_PHY_CONT; /* TWR_RES=0x88, RES_100U=0x28 */
#endif
	tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_EEE_RXTIMER, reg_value);

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_EEE_MCTRL1);
	reg_value |= XGMAC_TRN_LPI;
	tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_EEE_MCTRL1, reg_value);

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_XS_PCS_EEE_MCTRL0);

	reg_value &= ~XGMAC_TX_RX_QUIET_EN;
	reg_value |= XGMAC_TX_RX_QUIET_EN; /* RX_QUIET_EN, TX_QUIET_EN, LRX_EN, LTX_EN */

	tc956x_xpcs_write(xpcsaddr, XGMAC_VR_XS_PCS_EEE_MCTRL0, reg_value);
#endif

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_MII_AN_CTRL);
	reg_value &= XGMAC_TX_CFIG_INTR_EN_MASK;/*TX_CONFIG MAC SIDE*/
	reg_value |= XGMAC_MII_AN_INTR_EN;/*MII_AN_INTR_EN enabe*/
	tc956x_xpcs_write(xpcsaddr, XGMAC_VR_MII_AN_CTRL, reg_value);

	reg_value = tc956x_xpcs_read(xpcsaddr, XGMAC_VR_MII_DIG_CTRL1);
	reg_value &= ~XGMAC_MAC_AUTO_SW_EN;/*MAC_AUTO_SW enable*/
	if (priv->is_sgmii_2p5g != true)
		/* Enable only if SGMII 2.5G is not enabled. */
		reg_value |= XGMAC_MAC_AUTO_SW_EN;
	tc956x_xpcs_write(xpcsaddr, XGMAC_VR_MII_DIG_CTRL1, reg_value);

	return 0;
}

void tc956x_xpcs_ctrl_ane(struct tc956xmac_priv *priv, bool ane)
{
	u32 reg_value;

	reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_MII_CTRL);
#ifdef TC956X_MAGIC_PACKET_WOL_CONF
	if (priv->wol_config_enabled != true) {
#endif /* #ifdef TC956X_MAGIC_PACKET_WOL_CONF */
		if (ane) {
			reg_value |= XGMAC_AN_37_ENABLE;
			KPRINT_INFO("%s Enable AN", __func__);
		} else {
			reg_value &= (~XGMAC_AN_37_ENABLE);
			KPRINT_INFO("%s Disable AN", __func__);
			}
#ifdef TC956X_MAGIC_PACKET_WOL_CONF
	} else {
		/* Configure SGMII 1Gbps when WOL flag is enabled and native interface is other than SGMII. */
		KPRINT_INFO("%s Port %d : Entered with flag priv->wol_config_enabled %d", __func__, priv->port_num, priv->wol_config_enabled);
		reg_value |= XGMAC_AN_37_ENABLE;
		KPRINT_INFO("%s Enable AN", __func__);
	}
#endif /* #ifdef TC956X_MAGIC_PACKET_WOL_CONF */
	tc956x_xpcs_write(priv->xpcsaddr, XGMAC_SR_MII_CTRL, reg_value);
}

/**
 *  tc956x_xpcs_ctrl0_lrx - to configure XPCS LPI Rx Enable bit
 *  @priv: driver private structure
 *  @lrx : true to enable, false to disable
 *  @remarks : -
 */
void tc956x_xpcs_ctrl0_lrx(struct tc956xmac_priv *priv, bool lrx)
{
	u32 reg_value;

	reg_value = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_XS_PCS_EEE_MCTRL0);
	if (lrx) {
		reg_value |= XGMAC_EEE_LRX_EN;
		KPRINT_INFO("%s Enable XPCS LPI Rx\n", __func__);
	} else {
		reg_value &= (~XGMAC_EEE_LRX_EN);
		KPRINT_INFO("%s Disable XPCS LPI Rx\n", __func__);
	}

	tc956x_xpcs_write(priv->xpcsaddr, XGMAC_VR_XS_PCS_EEE_MCTRL0, reg_value);
}
#endif
