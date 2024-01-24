/*
 * TC956X ethernet driver.
 *
 * tc956x_pf_rsc_mng.c
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
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
 *  04 June 2020 : Initial Version
 *  VERSION      : 00-01
 *
 *  30 Nov 2021  : Base lined for SRIOV
 *  VERSION      : 01-02
 */

#include "tc956xmac.h"
#include "tc956x_pf_rsc_mng.h"

/**
 * tc956x_pf_rsc_mng_get_fn_id
 *
 * \brief API to get function ID of this function
 *
 * \details This function is called to get the function ID from
 * the resource manager function ID register
 *
 * \param[in] dev - Pointer to device structure
 * \param[in] fn_id_info - Pointer to function id info structure
 *
 * \return success or error
 */

static int tc956x_pf_rsc_mng_get_fn_id(struct tc956xmac_priv *priv, void __iomem *reg_pci_bridge_config_addr,
						struct fn_id *fn_id_info)
{
	void __iomem *ioaddr = reg_pci_bridge_config_addr;
	u32 rsc_mng_id = readl(ioaddr + RSCMNG_ID_REG);

	fn_id_info->fn_type = (rsc_mng_id & RSC_MNG_FN_TYPE) >> 16;
	fn_id_info->pf_no = (rsc_mng_id & RSC_MNG_PF_FN_NUM);
	fn_id_info->vf_no = ((rsc_mng_id & RSC_MNG_VF_FN_NUM) >> 8);

	/** Below error check for PF driver.
	 * In case of PF driver, vf_no should be 0 and function type to be 0
	 */
	if ((fn_id_info->vf_no != 0) || (fn_id_info->fn_type != 0))
		return -1;

	/* PF 0 and 1 are supported */
	if (fn_id_info->pf_no > 1)
		return -1;

	return 0;

}

/**
 * tc956x_pf_rsc_mng_set_rscs
 *
 * \brief API to set the DMA channel resources for the functions
 *
 * \details This function is used to set the resources (DMA channels)
 * for PF and it's associated VFs
 *
 * \param[in] dev - Pointer to device structure
 * \param[in] rscs - Pointer to array of PF0, VF0, VF1, VF2 having
 * DMA channel bit pattern <7,6,5,4,3,2,1,0>
 * Example: for case 0-1 following is the array
 * rscs[4] = {	0x18, --> PF0  DMACH 3, 4
 *		0x21, --> VF00 DMACH 0, 5
 *		0x02, --> VF01 DMACH 1
 *		0xc4, --> VF02 DMACH 2, 6, 7
 *	}
 * \return None
 */
static void tc956x_pf_rsc_mng_set_rscs(struct tc956xmac_priv *priv, struct net_device *dev, u8 *rscs)
{
	void __iomem *ioaddr = priv->tc956x_BRIDGE_CFG_pci_base_addr;
	u32 rsc_mng_rsc_ctl = 0;
	u8 i, buffer_value;

	for (i = 0; i < MAX_FUNCTIONS_PER_PF; i++) {
		buffer_value = rscs[i];
		if ((buffer_value & RSC_MNG_DMA_CH7_MASK) ==
					RSC_MNG_DMA_CH7_MASK) /*DMA ch7 bit*/
			rsc_mng_rsc_ctl |= (i << RSC_MNG_DMA_CH7_BIT_POS);

		if ((buffer_value & RSC_MNG_DMA_CH6_MASK) ==
					RSC_MNG_DMA_CH6_MASK) /*DMA ch6 bit*/
			rsc_mng_rsc_ctl |= (i << RSC_MNG_DMA_CH6_BIT_POS);

		if ((buffer_value & RSC_MNG_DMA_CH5_MASK) ==
					RSC_MNG_DMA_CH5_MASK) /*DMA ch5 bit*/
			rsc_mng_rsc_ctl |= (i << RSC_MNG_DMA_CH5_BIT_POS);

		if ((buffer_value & RSC_MNG_DMA_CH4_MASK) ==
					RSC_MNG_DMA_CH4_MASK) /*DMA ch4 bit*/
			rsc_mng_rsc_ctl |= (i << RSC_MNG_DMA_CH4_BIT_POS);

		if ((buffer_value & RSC_MNG_DMA_CH3_MASK) ==
					RSC_MNG_DMA_CH3_MASK) /*DMA ch3 bit*/
			rsc_mng_rsc_ctl |= (i << RSC_MNG_DMA_CH3_BIT_POS);

		if ((buffer_value & RSC_MNG_DMA_CH2_MASK) ==
					RSC_MNG_DMA_CH2_MASK) /*DMA ch2 bit*/
			rsc_mng_rsc_ctl |= (i << RSC_MNG_DMA_CH2_BIT_POS);

		if ((buffer_value & RSC_MNG_DMA_CH1_MASK) ==
					RSC_MNG_DMA_CH1_MASK) /*DMA ch1 bit*/
			rsc_mng_rsc_ctl |= (i << RSC_MNG_DMA_CH1_BIT_POS);

		if ((buffer_value & RSC_MNG_DMA_CH0_MASK) ==
					RSC_MNG_DMA_CH0_MASK) /*DMA ch0 bit*/
			rsc_mng_rsc_ctl |= (i << RSC_MNG_DMA_CH0_BIT_POS);
	}
	writel(rsc_mng_rsc_ctl, ioaddr + RSCMNG_RSC_CTRL_REG);
}

/**
 * tc956x_pf_rsc_mng_get_rscs
 *
 * \brief API to get the DMA channel resources allocated
 *
 * \details This function is used to get the resources (DMA channels)
 * allocated for this function
 *
 * \param[in] dev - Pointer to device structure
 * \param[in] rscs - Pointer to return DMA channel bit pattern in
 * order <7,6,5,4,3,2,1,0>
 * \return None
 */

static void tc956x_pf_rsc_mng_get_rscs(struct tc956xmac_priv *priv, struct net_device *dev, u8 *rscs)
{
	void __iomem *ioaddr = priv->tc956x_BRIDGE_CFG_pci_base_addr;
	*rscs = ((readl(ioaddr + RSCMNG_RSC_ST_REG)) & RSC_MNG_RSC_STATUS_MASK);
}

const struct mac_rsc_mng_ops tc956xmac_rsc_mng_ops = {
	.get_fn_id = tc956x_pf_rsc_mng_get_fn_id,
	.set_rscs = tc956x_pf_rsc_mng_set_rscs,
	.get_rscs = tc956x_pf_rsc_mng_get_rscs,
};
