/*
 * TC956X ethernet driver.
 *
 * tc956x_msigen.c
 *
 * Copyright (C) 2022 Toshiba Electronic Devices & Storage Corporation
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
 *  14 SEP 2020 : Initial Version
 *  VERSION     : 00-01
 *
 *  30 Nov 2021 : Base lined for SRIOV
 *  VERSION     : 01-02
 *
 *  20 May 2022 : 1. Automotive Driver, CPE fixes merged and IPA Features supported
 *                2. Base lined version
 *  VERSION     : 03-00
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include "hwif.h"
#include "common.h"
#include "tc956xmac.h"

#ifdef TC956X_SRIOV_PF
/**
 * tc956x_msigen_init
 *
 * \brief API to Initialize and configure MSIGEN module
 *
 * \details This function is used to configures clock, reset, sets mask and
 * interrupt source to MSI vector mapping.
 *
 * \param[in] dev - Pointer to net device structure
 *
 * \return None
 */
static void tc956x_msigen_init(struct tc956xmac_priv *priv, struct net_device *dev)
{
	u32 rd_val;

	/* Enable MSIGEN Module */
#ifdef TC956X
	rd_val = readl(priv->ioaddr + NCLKCTRL0_OFFSET);
	rd_val |= (1 << TC956X_MSIGENCEN);
	writel(rd_val, priv->ioaddr + NCLKCTRL0_OFFSET);
	rd_val = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
	rd_val &= ~(1 << TC956X_MSIGENCEN);
	writel(rd_val, priv->ioaddr + NRSTCTRL0_OFFSET);
#else
	rd_val = readl(priv->ioaddr + NCLKCTRL_OFFSET);
	rd_val |= (1 << TC956X_MSIGENCEN);
	writel(rd_val, priv->ioaddr + NCLKCTRL_OFFSET);
	rd_val = readl(priv->ioaddr + NRSTCTRL_OFFSET);
	rd_val &= ~(1 << TC956X_MSIGENCEN);
	writel(rd_val, priv->ioaddr + NRSTCTRL_OFFSET);
#endif
	/* Initialize MSIGEN */

	writel(TC956X_MSI_OUT_EN_CLR, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	writel(TC956X_MSI_MASK_SET, priv->ioaddr + TC956X_MSI_MASK_SET_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	writel(TC956X_MSI_MASK_CLR, priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	/* DMA Ch Tx-Rx Interrupt sources are assigned to Vector 0,
	 * All other Interrupt sources are assigned to Vector 1 */
	writel(TC956X_MSI_SET0, priv->ioaddr + TC956X_MSI_VECT_SET0_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	writel(TC956X_MSI_SET1, priv->ioaddr + TC956X_MSI_VECT_SET1_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	writel(TC956X_MSI_SET2, priv->ioaddr + TC956X_MSI_VECT_SET2_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	writel(TC956X_MSI_SET3, priv->ioaddr + TC956X_MSI_VECT_SET3_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	writel(TC956X_MSI_SET4, priv->ioaddr + TC956X_MSI_VECT_SET4_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	writel(TC956X_MSI_SET5, priv->ioaddr + TC956X_MSI_VECT_SET5_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	writel(TC956X_MSI_SET6, priv->ioaddr + TC956X_MSI_VECT_SET6_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	writel(TC956X_MSI_SET7, priv->ioaddr + TC956X_MSI_VECT_SET7_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
}

/**
 * tc956x_interrupt_en
 *
 * \brief API to enable disable MSI interrupts
 *
 * \details This function is used to set/clear interrupts
 *
 * \param[in] dev - Pointer to net device structure
 * \param[in] en -	1 - Enable interrupts
 * 0 - Disable interrupts
 * \return None
 */
static void tc956x_interrupt_en(struct tc956xmac_priv *priv, struct net_device *dev, u32 en)
{
	u32 chan, mask_val = 0;

	if (en) {

	/* Disable MSI for Tx/Rx channels that is not enabled in the Function */

	for (chan = 0; chan < priv->plat->tx_queues_to_use; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->tx_ch_in_use[chan] != TC956X_ENABLE_CHNL)
#endif
			mask_val |= (1 << (MSI_INT_TX_CH0 + chan));
	}

	for (chan = 0; chan < priv->plat->rx_queues_to_use; chan++) {
#ifdef TC956X_SRIOV_PF
		if (priv->plat->rx_ch_in_use[chan] != TC956X_ENABLE_CHNL)
#endif
			mask_val |= (1 << (MSI_INT_RX_CH0 + chan));
	}
#ifdef TC956X_SRIOV_PF
		/* PHY MSI interrupt enabled */
		mask_val &= ~(1 << MSI_INT_EXT_PHY);
#else
		/* PHY MSI interrupt diabled */
		mask_val |= (1 << MSI_INT_EXT_PHY);
#endif
		mask_val = TC956X_MSI_OUT_EN & (~mask_val);

#ifdef TC956X_SW_MSI
		/* Enable SW MSI interrupt */
		KPRINT_INFO("%s Enable SW MSI", __func__);
		mask_val |=  (1 << MSI_INT_SW_MSI);

		/*Clear SW MSI*/
		writel(1, priv->ioaddr + TC956X_MSI_SW_MSI_CLR(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));

#endif
		writel(mask_val, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));
	} else
		writel(TC956X_MSI_OUT_EN_CLR, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));

	netdev_dbg(priv->dev, "%s mask_val : %x\n", __func__, mask_val);
}

/**
 * tc956x_interrupt_clr
 *
 * \brief API to enable clear MSI vector
 *
 * \details This function is used to clear MSI vector. To be called
 * after handling the interrupts.
 *
 * \param[in] dev - Pointer to net device structure
 * \param[in] vector - Supported values TC956X_MSI_VECTOR_0, TC956X_MSI_VECTOR_1
 *
 * \return None
 */
static void tc956x_interrupt_clr(struct tc956xmac_priv *priv, struct net_device *dev, u32 vector)
{
	writel((1<<vector), priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(priv->fn_id_info.pf_no, priv->fn_id_info.vf_no));

	netdev_dbg(priv->dev, "%s %d\n", __func__, vector);
}
#elif defined TC956X_SRIOV_VF
/**
 * tc956x_msigen_init
 *
 * \brief API to Initialize and configure MSIGEN module
 *
 * \details This function is used to configures clock, reset, sets mask and
 *		interrupt source to MSI vector mapping.
 *
 * \param[in] dev - Pointer to net device structure
 *
 * \return None
 */
static void tc956x_msigen_init(struct tc956xmac_priv *priv, struct net_device *dev,
					struct fn_id *fn_id_info)
{
	//struct tc956xmac_priv *priv = netdev_priv(dev);
	u8 pf_id = fn_id_info->pf_no;
	u8 vf_id = fn_id_info->vf_no;

#ifndef TC956X_SRIOV_VF
	/* Enable MSIGEN Module */
	rd_val = readl(priv->ioaddr + NCLKCTRL0_OFFSET);
	rd_val |= (1 << TC956X_MSIGENCEN);
	writel(rd_val, priv->ioaddr + NCLKCTRL0_OFFSET);
	rd_val = readl(priv->ioaddr + NRSTCTRL0_OFFSET);
	rd_val &= ~(1 << TC956X_MSIGENCEN);
	writel(rd_val, priv->ioaddr + NRSTCTRL0_OFFSET);
#endif

	/* Initialize MSIGEN */
	writel(TC956X_MSI_OUT_EN_CLR, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_MASK_SET, priv->ioaddr + TC956X_MSI_MASK_SET_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_MASK_CLR, priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_SET0, priv->ioaddr + TC956X_MSI_VECT_SET0_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_SET1, priv->ioaddr + TC956X_MSI_VECT_SET1_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_SET2, priv->ioaddr + TC956X_MSI_VECT_SET2_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_SET3, priv->ioaddr + TC956X_MSI_VECT_SET3_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_SET4, priv->ioaddr + TC956X_MSI_VECT_SET4_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_SET5, priv->ioaddr + TC956X_MSI_VECT_SET5_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_SET6, priv->ioaddr + TC956X_MSI_VECT_SET6_OFFSET(pf_id, vf_id));
	writel(TC956X_MSI_SET7, priv->ioaddr + TC956X_MSI_VECT_SET7_OFFSET(pf_id, vf_id));


}

/**
 * tc956x_interrupt_en
 *
 * \brief API to enable disable MSI interrupts
 *
 * \details This function is used to set/clear interrupts
 *
 * \param[in] dev - Pointer to net device structure
 * \param[in] en -	1 - Enable interrupts
 * 0 - Disable interrupts
 * \return None
 */
static void tc956x_interrupt_en(struct tc956xmac_priv *priv, struct net_device *dev, u32 en,
						struct fn_id *fn_id_info)
{
	//struct tc956xmac_priv *priv = netdev_priv(dev);
	u8 pf_id = fn_id_info->pf_no;
	u8 vf_id = fn_id_info->vf_no;

	u32 tx_queues_cnt = priv->plat->tx_queues_to_use;
	u32 ch;
	u32 msi_out_en = 0;

	for (ch = 0; ch < tx_queues_cnt; ch++) {
#ifdef TC956X_SRIOV_VF
		/* skip configuring for unallocated channel */
		if (priv->plat->ch_in_use[ch] == 0)
			continue;
#endif
		msi_out_en |= (1 << (ch + TC956X_MSI_INT_TXDMA_CH0)); /* tx dma channel setting */
		msi_out_en |= (1 << (ch + TC956X_MSI_INT_RXDMA_CH0)); /* rx dma channel setting */
	}

#ifdef TC956X_SRIOV_VF
	msi_out_en |= (1 << TC956X_MSI_INT_MBX); /* Mailbox interrupt setting */
#endif

	msi_out_en = TC956X_MSI_OUT_EN & (msi_out_en);

	if (en)
		writel(msi_out_en, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(pf_id, vf_id));
	else
		writel(TC956X_MSI_OUT_EN_CLR, priv->ioaddr + TC956X_MSI_OUT_EN_OFFSET(pf_id, vf_id));
}

/**
 * tc956x_interrupt_clr
 *
 * \brief API to enable clear MSI vector
 *
 * \details This function is used to clear MSI vector. To be called
 * after handling the interrupts.
 *
 * \param[in] dev - Pointer to net device structure
 * \param[in] vector - Supported values TC956X_MSI_VECTOR_0, TC956X_MSI_VECTOR_1
 *
 * \return None
 */
static void tc956x_interrupt_clr(struct tc956xmac_priv *priv, struct net_device *dev, u32 vector,
						struct fn_id *fn_id_info)
{
	u8 pf_id = fn_id_info->pf_no;
	u8 vf_id = fn_id_info->vf_no;

	writel((1 << vector), priv->ioaddr + TC956X_MSI_MASK_CLR_OFFSET(pf_id, vf_id));

}
#endif
const struct tc956x_msi_ops tc956x_msigen_ops = {
	.init = tc956x_msigen_init,
	.interrupt_en = tc956x_interrupt_en,
	.interrupt_clr = tc956x_interrupt_clr,
};
