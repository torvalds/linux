/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 *
 * This file is free software; you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * License 2: Modified BSD
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Advanced Micro Devices, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/phy.h>
#include <linux/mdio.h>
#include <linux/clk.h>
#include <linux/bitrev.h>
#include <linux/crc32.h>

#include "xgbe.h"
#include "xgbe-common.h"

static unsigned int xgbe_usec_to_riwt(struct xgbe_prv_data *pdata,
				      unsigned int usec)
{
	unsigned long rate;
	unsigned int ret;

	DBGPR("-->xgbe_usec_to_riwt\n");

	rate = pdata->sysclk_rate;

	/*
	 * Convert the input usec value to the watchdog timer value. Each
	 * watchdog timer value is equivalent to 256 clock cycles.
	 * Calculate the required value as:
	 *   ( usec * ( system_clock_mhz / 10^6 ) / 256
	 */
	ret = (usec * (rate / 1000000)) / 256;

	DBGPR("<--xgbe_usec_to_riwt\n");

	return ret;
}

static unsigned int xgbe_riwt_to_usec(struct xgbe_prv_data *pdata,
				      unsigned int riwt)
{
	unsigned long rate;
	unsigned int ret;

	DBGPR("-->xgbe_riwt_to_usec\n");

	rate = pdata->sysclk_rate;

	/*
	 * Convert the input watchdog timer value to the usec value. Each
	 * watchdog timer value is equivalent to 256 clock cycles.
	 * Calculate the required value as:
	 *   ( riwt * 256 ) / ( system_clock_mhz / 10^6 )
	 */
	ret = (riwt * 256) / (rate / 1000000);

	DBGPR("<--xgbe_riwt_to_usec\n");

	return ret;
}

static int xgbe_config_pblx8(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++)
		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_CR, PBLX8,
				       pdata->pblx8);

	return 0;
}

static int xgbe_get_tx_pbl_val(struct xgbe_prv_data *pdata)
{
	return XGMAC_DMA_IOREAD_BITS(pdata->channel, DMA_CH_TCR, PBL);
}

static int xgbe_config_tx_pbl_val(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_TCR, PBL,
				       pdata->tx_pbl);
	}

	return 0;
}

static int xgbe_get_rx_pbl_val(struct xgbe_prv_data *pdata)
{
	return XGMAC_DMA_IOREAD_BITS(pdata->channel, DMA_CH_RCR, PBL);
}

static int xgbe_config_rx_pbl_val(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->rx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_RCR, PBL,
				       pdata->rx_pbl);
	}

	return 0;
}

static int xgbe_config_osp_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_TCR, OSP,
				       pdata->tx_osp_mode);
	}

	return 0;
}

static int xgbe_config_rsf_mode(struct xgbe_prv_data *pdata, unsigned int val)
{
	unsigned int i;

	for (i = 0; i < pdata->rx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_RQOMR, RSF, val);

	return 0;
}

static int xgbe_config_tsf_mode(struct xgbe_prv_data *pdata, unsigned int val)
{
	unsigned int i;

	for (i = 0; i < pdata->tx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_TQOMR, TSF, val);

	return 0;
}

static int xgbe_config_rx_threshold(struct xgbe_prv_data *pdata,
				    unsigned int val)
{
	unsigned int i;

	for (i = 0; i < pdata->rx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_RQOMR, RTC, val);

	return 0;
}

static int xgbe_config_tx_threshold(struct xgbe_prv_data *pdata,
				    unsigned int val)
{
	unsigned int i;

	for (i = 0; i < pdata->tx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_TQOMR, TTC, val);

	return 0;
}

static int xgbe_config_rx_coalesce(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->rx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_RIWT, RWT,
				       pdata->rx_riwt);
	}

	return 0;
}

static int xgbe_config_tx_coalesce(struct xgbe_prv_data *pdata)
{
	return 0;
}

static void xgbe_config_rx_buffer_size(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->rx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_RCR, RBSZ,
				       pdata->rx_buf_size);
	}
}

static void xgbe_config_tso_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_TCR, TSE, 1);
	}
}

static void xgbe_config_sph_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->rx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_CR, SPH, 1);
	}

	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, HDSMS, XGBE_SPH_HDSMS_SIZE);
}

static int xgbe_write_rss_reg(struct xgbe_prv_data *pdata, unsigned int type,
			      unsigned int index, unsigned int val)
{
	unsigned int wait;
	int ret = 0;

	mutex_lock(&pdata->rss_mutex);

	if (XGMAC_IOREAD_BITS(pdata, MAC_RSSAR, OB)) {
		ret = -EBUSY;
		goto unlock;
	}

	XGMAC_IOWRITE(pdata, MAC_RSSDR, val);

	XGMAC_IOWRITE_BITS(pdata, MAC_RSSAR, RSSIA, index);
	XGMAC_IOWRITE_BITS(pdata, MAC_RSSAR, ADDRT, type);
	XGMAC_IOWRITE_BITS(pdata, MAC_RSSAR, CT, 0);
	XGMAC_IOWRITE_BITS(pdata, MAC_RSSAR, OB, 1);

	wait = 1000;
	while (wait--) {
		if (!XGMAC_IOREAD_BITS(pdata, MAC_RSSAR, OB))
			goto unlock;

		usleep_range(1000, 1500);
	}

	ret = -EBUSY;

unlock:
	mutex_unlock(&pdata->rss_mutex);

	return ret;
}

static int xgbe_write_rss_hash_key(struct xgbe_prv_data *pdata)
{
	unsigned int key_regs = sizeof(pdata->rss_key) / sizeof(u32);
	unsigned int *key = (unsigned int *)&pdata->rss_key;
	int ret;

	while (key_regs--) {
		ret = xgbe_write_rss_reg(pdata, XGBE_RSS_HASH_KEY_TYPE,
					 key_regs, *key++);
		if (ret)
			return ret;
	}

	return 0;
}

static int xgbe_write_rss_lookup_table(struct xgbe_prv_data *pdata)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(pdata->rss_table); i++) {
		ret = xgbe_write_rss_reg(pdata,
					 XGBE_RSS_LOOKUP_TABLE_TYPE, i,
					 pdata->rss_table[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int xgbe_set_rss_hash_key(struct xgbe_prv_data *pdata, const u8 *key)
{
	memcpy(pdata->rss_key, key, sizeof(pdata->rss_key));

	return xgbe_write_rss_hash_key(pdata);
}

static int xgbe_set_rss_lookup_table(struct xgbe_prv_data *pdata,
				     const u32 *table)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pdata->rss_table); i++)
		XGMAC_SET_BITS(pdata->rss_table[i], MAC_RSSDR, DMCH, table[i]);

	return xgbe_write_rss_lookup_table(pdata);
}

static int xgbe_enable_rss(struct xgbe_prv_data *pdata)
{
	int ret;

	if (!pdata->hw_feat.rss)
		return -EOPNOTSUPP;

	/* Program the hash key */
	ret = xgbe_write_rss_hash_key(pdata);
	if (ret)
		return ret;

	/* Program the lookup table */
	ret = xgbe_write_rss_lookup_table(pdata);
	if (ret)
		return ret;

	/* Set the RSS options */
	XGMAC_IOWRITE(pdata, MAC_RSSCR, pdata->rss_options);

	/* Enable RSS */
	XGMAC_IOWRITE_BITS(pdata, MAC_RSSCR, RSSE, 1);

	return 0;
}

static int xgbe_disable_rss(struct xgbe_prv_data *pdata)
{
	if (!pdata->hw_feat.rss)
		return -EOPNOTSUPP;

	XGMAC_IOWRITE_BITS(pdata, MAC_RSSCR, RSSE, 0);

	return 0;
}

static void xgbe_config_rss(struct xgbe_prv_data *pdata)
{
	int ret;

	if (!pdata->hw_feat.rss)
		return;

	if (pdata->netdev->features & NETIF_F_RXHASH)
		ret = xgbe_enable_rss(pdata);
	else
		ret = xgbe_disable_rss(pdata);

	if (ret)
		netdev_err(pdata->netdev,
			   "error configuring RSS, RSS disabled\n");
}

static int xgbe_disable_tx_flow_control(struct xgbe_prv_data *pdata)
{
	unsigned int max_q_count, q_count;
	unsigned int reg, reg_val;
	unsigned int i;

	/* Clear MTL flow control */
	for (i = 0; i < pdata->rx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_RQOMR, EHFC, 0);

	/* Clear MAC flow control */
	max_q_count = XGMAC_MAX_FLOW_CONTROL_QUEUES;
	q_count = min_t(unsigned int, pdata->tx_q_count, max_q_count);
	reg = MAC_Q0TFCR;
	for (i = 0; i < q_count; i++) {
		reg_val = XGMAC_IOREAD(pdata, reg);
		XGMAC_SET_BITS(reg_val, MAC_Q0TFCR, TFE, 0);
		XGMAC_IOWRITE(pdata, reg, reg_val);

		reg += MAC_QTFCR_INC;
	}

	return 0;
}

static int xgbe_enable_tx_flow_control(struct xgbe_prv_data *pdata)
{
	unsigned int max_q_count, q_count;
	unsigned int reg, reg_val;
	unsigned int i;

	/* Set MTL flow control */
	for (i = 0; i < pdata->rx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_RQOMR, EHFC, 1);

	/* Set MAC flow control */
	max_q_count = XGMAC_MAX_FLOW_CONTROL_QUEUES;
	q_count = min_t(unsigned int, pdata->tx_q_count, max_q_count);
	reg = MAC_Q0TFCR;
	for (i = 0; i < q_count; i++) {
		reg_val = XGMAC_IOREAD(pdata, reg);

		/* Enable transmit flow control */
		XGMAC_SET_BITS(reg_val, MAC_Q0TFCR, TFE, 1);
		/* Set pause time */
		XGMAC_SET_BITS(reg_val, MAC_Q0TFCR, PT, 0xffff);

		XGMAC_IOWRITE(pdata, reg, reg_val);

		reg += MAC_QTFCR_INC;
	}

	return 0;
}

static int xgbe_disable_rx_flow_control(struct xgbe_prv_data *pdata)
{
	XGMAC_IOWRITE_BITS(pdata, MAC_RFCR, RFE, 0);

	return 0;
}

static int xgbe_enable_rx_flow_control(struct xgbe_prv_data *pdata)
{
	XGMAC_IOWRITE_BITS(pdata, MAC_RFCR, RFE, 1);

	return 0;
}

static int xgbe_config_tx_flow_control(struct xgbe_prv_data *pdata)
{
	struct ieee_pfc *pfc = pdata->pfc;

	if (pdata->tx_pause || (pfc && pfc->pfc_en))
		xgbe_enable_tx_flow_control(pdata);
	else
		xgbe_disable_tx_flow_control(pdata);

	return 0;
}

static int xgbe_config_rx_flow_control(struct xgbe_prv_data *pdata)
{
	struct ieee_pfc *pfc = pdata->pfc;

	if (pdata->rx_pause || (pfc && pfc->pfc_en))
		xgbe_enable_rx_flow_control(pdata);
	else
		xgbe_disable_rx_flow_control(pdata);

	return 0;
}

static void xgbe_config_flow_control(struct xgbe_prv_data *pdata)
{
	struct ieee_pfc *pfc = pdata->pfc;

	xgbe_config_tx_flow_control(pdata);
	xgbe_config_rx_flow_control(pdata);

	XGMAC_IOWRITE_BITS(pdata, MAC_RFCR, PFCE,
			   (pfc && pfc->pfc_en) ? 1 : 0);
}

static void xgbe_enable_dma_interrupts(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int dma_ch_isr, dma_ch_ier;
	unsigned int i;

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		/* Clear all the interrupts which are set */
		dma_ch_isr = XGMAC_DMA_IOREAD(channel, DMA_CH_SR);
		XGMAC_DMA_IOWRITE(channel, DMA_CH_SR, dma_ch_isr);

		/* Clear all interrupt enable bits */
		dma_ch_ier = 0;

		/* Enable following interrupts
		 *   NIE  - Normal Interrupt Summary Enable
		 *   AIE  - Abnormal Interrupt Summary Enable
		 *   FBEE - Fatal Bus Error Enable
		 */
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, NIE, 1);
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, AIE, 1);
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, FBEE, 1);

		if (channel->tx_ring) {
			/* Enable the following Tx interrupts
			 *   TIE  - Transmit Interrupt Enable (unless using
			 *          per channel interrupts)
			 */
			if (!pdata->per_channel_irq)
				XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, TIE, 1);
		}
		if (channel->rx_ring) {
			/* Enable following Rx interrupts
			 *   RBUE - Receive Buffer Unavailable Enable
			 *   RIE  - Receive Interrupt Enable (unless using
			 *          per channel interrupts)
			 */
			XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RBUE, 1);
			if (!pdata->per_channel_irq)
				XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RIE, 1);
		}

		XGMAC_DMA_IOWRITE(channel, DMA_CH_IER, dma_ch_ier);
	}
}

static void xgbe_enable_mtl_interrupts(struct xgbe_prv_data *pdata)
{
	unsigned int mtl_q_isr;
	unsigned int q_count, i;

	q_count = max(pdata->hw_feat.tx_q_cnt, pdata->hw_feat.rx_q_cnt);
	for (i = 0; i < q_count; i++) {
		/* Clear all the interrupts which are set */
		mtl_q_isr = XGMAC_MTL_IOREAD(pdata, i, MTL_Q_ISR);
		XGMAC_MTL_IOWRITE(pdata, i, MTL_Q_ISR, mtl_q_isr);

		/* No MTL interrupts to be enabled */
		XGMAC_MTL_IOWRITE(pdata, i, MTL_Q_IER, 0);
	}
}

static void xgbe_enable_mac_interrupts(struct xgbe_prv_data *pdata)
{
	unsigned int mac_ier = 0;

	/* Enable Timestamp interrupt */
	XGMAC_SET_BITS(mac_ier, MAC_IER, TSIE, 1);

	XGMAC_IOWRITE(pdata, MAC_IER, mac_ier);

	/* Enable all counter interrupts */
	XGMAC_IOWRITE_BITS(pdata, MMC_RIER, ALL_INTERRUPTS, 0xffffffff);
	XGMAC_IOWRITE_BITS(pdata, MMC_TIER, ALL_INTERRUPTS, 0xffffffff);
}

static int xgbe_set_gmii_speed(struct xgbe_prv_data *pdata)
{
	if (XGMAC_IOREAD_BITS(pdata, MAC_TCR, SS) == 0x3)
		return 0;

	XGMAC_IOWRITE_BITS(pdata, MAC_TCR, SS, 0x3);

	return 0;
}

static int xgbe_set_gmii_2500_speed(struct xgbe_prv_data *pdata)
{
	if (XGMAC_IOREAD_BITS(pdata, MAC_TCR, SS) == 0x2)
		return 0;

	XGMAC_IOWRITE_BITS(pdata, MAC_TCR, SS, 0x2);

	return 0;
}

static int xgbe_set_xgmii_speed(struct xgbe_prv_data *pdata)
{
	if (XGMAC_IOREAD_BITS(pdata, MAC_TCR, SS) == 0)
		return 0;

	XGMAC_IOWRITE_BITS(pdata, MAC_TCR, SS, 0);

	return 0;
}

static int xgbe_set_promiscuous_mode(struct xgbe_prv_data *pdata,
				     unsigned int enable)
{
	unsigned int val = enable ? 1 : 0;

	if (XGMAC_IOREAD_BITS(pdata, MAC_PFR, PR) == val)
		return 0;

	DBGPR("  %s promiscuous mode\n", enable ? "entering" : "leaving");
	XGMAC_IOWRITE_BITS(pdata, MAC_PFR, PR, val);

	return 0;
}

static int xgbe_set_all_multicast_mode(struct xgbe_prv_data *pdata,
				       unsigned int enable)
{
	unsigned int val = enable ? 1 : 0;

	if (XGMAC_IOREAD_BITS(pdata, MAC_PFR, PM) == val)
		return 0;

	DBGPR("  %s allmulti mode\n", enable ? "entering" : "leaving");
	XGMAC_IOWRITE_BITS(pdata, MAC_PFR, PM, val);

	return 0;
}

static void xgbe_set_mac_reg(struct xgbe_prv_data *pdata,
			     struct netdev_hw_addr *ha, unsigned int *mac_reg)
{
	unsigned int mac_addr_hi, mac_addr_lo;
	u8 *mac_addr;

	mac_addr_lo = 0;
	mac_addr_hi = 0;

	if (ha) {
		mac_addr = (u8 *)&mac_addr_lo;
		mac_addr[0] = ha->addr[0];
		mac_addr[1] = ha->addr[1];
		mac_addr[2] = ha->addr[2];
		mac_addr[3] = ha->addr[3];
		mac_addr = (u8 *)&mac_addr_hi;
		mac_addr[0] = ha->addr[4];
		mac_addr[1] = ha->addr[5];

		DBGPR("  adding mac address %pM at 0x%04x\n", ha->addr,
		      *mac_reg);

		XGMAC_SET_BITS(mac_addr_hi, MAC_MACA1HR, AE, 1);
	}

	XGMAC_IOWRITE(pdata, *mac_reg, mac_addr_hi);
	*mac_reg += MAC_MACA_INC;
	XGMAC_IOWRITE(pdata, *mac_reg, mac_addr_lo);
	*mac_reg += MAC_MACA_INC;
}

static void xgbe_set_mac_addn_addrs(struct xgbe_prv_data *pdata)
{
	struct net_device *netdev = pdata->netdev;
	struct netdev_hw_addr *ha;
	unsigned int mac_reg;
	unsigned int addn_macs;

	mac_reg = MAC_MACA1HR;
	addn_macs = pdata->hw_feat.addn_mac;

	if (netdev_uc_count(netdev) > addn_macs) {
		xgbe_set_promiscuous_mode(pdata, 1);
	} else {
		netdev_for_each_uc_addr(ha, netdev) {
			xgbe_set_mac_reg(pdata, ha, &mac_reg);
			addn_macs--;
		}

		if (netdev_mc_count(netdev) > addn_macs) {
			xgbe_set_all_multicast_mode(pdata, 1);
		} else {
			netdev_for_each_mc_addr(ha, netdev) {
				xgbe_set_mac_reg(pdata, ha, &mac_reg);
				addn_macs--;
			}
		}
	}

	/* Clear remaining additional MAC address entries */
	while (addn_macs--)
		xgbe_set_mac_reg(pdata, NULL, &mac_reg);
}

static void xgbe_set_mac_hash_table(struct xgbe_prv_data *pdata)
{
	struct net_device *netdev = pdata->netdev;
	struct netdev_hw_addr *ha;
	unsigned int hash_reg;
	unsigned int hash_table_shift, hash_table_count;
	u32 hash_table[XGBE_MAC_HASH_TABLE_SIZE];
	u32 crc;
	unsigned int i;

	hash_table_shift = 26 - (pdata->hw_feat.hash_table_size >> 7);
	hash_table_count = pdata->hw_feat.hash_table_size / 32;
	memset(hash_table, 0, sizeof(hash_table));

	/* Build the MAC Hash Table register values */
	netdev_for_each_uc_addr(ha, netdev) {
		crc = bitrev32(~crc32_le(~0, ha->addr, ETH_ALEN));
		crc >>= hash_table_shift;
		hash_table[crc >> 5] |= (1 << (crc & 0x1f));
	}

	netdev_for_each_mc_addr(ha, netdev) {
		crc = bitrev32(~crc32_le(~0, ha->addr, ETH_ALEN));
		crc >>= hash_table_shift;
		hash_table[crc >> 5] |= (1 << (crc & 0x1f));
	}

	/* Set the MAC Hash Table registers */
	hash_reg = MAC_HTR0;
	for (i = 0; i < hash_table_count; i++) {
		XGMAC_IOWRITE(pdata, hash_reg, hash_table[i]);
		hash_reg += MAC_HTR_INC;
	}
}

static int xgbe_add_mac_addresses(struct xgbe_prv_data *pdata)
{
	if (pdata->hw_feat.hash_table_size)
		xgbe_set_mac_hash_table(pdata);
	else
		xgbe_set_mac_addn_addrs(pdata);

	return 0;
}

static int xgbe_set_mac_address(struct xgbe_prv_data *pdata, u8 *addr)
{
	unsigned int mac_addr_hi, mac_addr_lo;

	mac_addr_hi = (addr[5] <<  8) | (addr[4] <<  0);
	mac_addr_lo = (addr[3] << 24) | (addr[2] << 16) |
		      (addr[1] <<  8) | (addr[0] <<  0);

	XGMAC_IOWRITE(pdata, MAC_MACA0HR, mac_addr_hi);
	XGMAC_IOWRITE(pdata, MAC_MACA0LR, mac_addr_lo);

	return 0;
}

static int xgbe_read_mmd_regs(struct xgbe_prv_data *pdata, int prtad,
			      int mmd_reg)
{
	unsigned int mmd_address;
	int mmd_data;

	if (mmd_reg & MII_ADDR_C45)
		mmd_address = mmd_reg & ~MII_ADDR_C45;
	else
		mmd_address = (pdata->mdio_mmd << 16) | (mmd_reg & 0xffff);

	/* The PCS registers are accessed using mmio. The underlying APB3
	 * management interface uses indirect addressing to access the MMD
	 * register sets. This requires accessing of the PCS register in two
	 * phases, an address phase and a data phase.
	 *
	 * The mmio interface is based on 32-bit offsets and values. All
	 * register offsets must therefore be adjusted by left shifting the
	 * offset 2 bits and reading 32 bits of data.
	 */
	mutex_lock(&pdata->xpcs_mutex);
	XPCS_IOWRITE(pdata, PCS_MMD_SELECT << 2, mmd_address >> 8);
	mmd_data = XPCS_IOREAD(pdata, (mmd_address & 0xff) << 2);
	mutex_unlock(&pdata->xpcs_mutex);

	return mmd_data;
}

static void xgbe_write_mmd_regs(struct xgbe_prv_data *pdata, int prtad,
				int mmd_reg, int mmd_data)
{
	unsigned int mmd_address;

	if (mmd_reg & MII_ADDR_C45)
		mmd_address = mmd_reg & ~MII_ADDR_C45;
	else
		mmd_address = (pdata->mdio_mmd << 16) | (mmd_reg & 0xffff);

	/* If the PCS is changing modes, match the MAC speed to it */
	if (((mmd_address >> 16) == MDIO_MMD_PCS) &&
	    ((mmd_address & 0xffff) == MDIO_CTRL2)) {
		struct phy_device *phydev = pdata->phydev;

		if (mmd_data & MDIO_PCS_CTRL2_TYPE) {
			/* KX mode */
			if (phydev->supported & SUPPORTED_1000baseKX_Full)
				xgbe_set_gmii_speed(pdata);
			else
				xgbe_set_gmii_2500_speed(pdata);
		} else {
			/* KR mode */
			xgbe_set_xgmii_speed(pdata);
		}
	}

	/* The PCS registers are accessed using mmio. The underlying APB3
	 * management interface uses indirect addressing to access the MMD
	 * register sets. This requires accessing of the PCS register in two
	 * phases, an address phase and a data phase.
	 *
	 * The mmio interface is based on 32-bit offsets and values. All
	 * register offsets must therefore be adjusted by left shifting the
	 * offset 2 bits and reading 32 bits of data.
	 */
	mutex_lock(&pdata->xpcs_mutex);
	XPCS_IOWRITE(pdata, PCS_MMD_SELECT << 2, mmd_address >> 8);
	XPCS_IOWRITE(pdata, (mmd_address & 0xff) << 2, mmd_data);
	mutex_unlock(&pdata->xpcs_mutex);
}

static int xgbe_tx_complete(struct xgbe_ring_desc *rdesc)
{
	return !XGMAC_GET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, OWN);
}

static int xgbe_disable_rx_csum(struct xgbe_prv_data *pdata)
{
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, IPC, 0);

	return 0;
}

static int xgbe_enable_rx_csum(struct xgbe_prv_data *pdata)
{
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, IPC, 1);

	return 0;
}

static int xgbe_enable_rx_vlan_stripping(struct xgbe_prv_data *pdata)
{
	/* Put the VLAN tag in the Rx descriptor */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, EVLRXS, 1);

	/* Don't check the VLAN type */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, DOVLTC, 1);

	/* Check only C-TAG (0x8100) packets */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, ERSVLM, 0);

	/* Don't consider an S-TAG (0x88A8) packet as a VLAN packet */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, ESVL, 0);

	/* Enable VLAN tag stripping */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, EVLS, 0x3);

	return 0;
}

static int xgbe_disable_rx_vlan_stripping(struct xgbe_prv_data *pdata)
{
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, EVLS, 0);

	return 0;
}

static int xgbe_enable_rx_vlan_filtering(struct xgbe_prv_data *pdata)
{
	/* Enable VLAN filtering */
	XGMAC_IOWRITE_BITS(pdata, MAC_PFR, VTFE, 1);

	/* Enable VLAN Hash Table filtering */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, VTHM, 1);

	/* Disable VLAN tag inverse matching */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, VTIM, 0);

	/* Only filter on the lower 12-bits of the VLAN tag */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, ETV, 1);

	/* In order for the VLAN Hash Table filtering to be effective,
	 * the VLAN tag identifier in the VLAN Tag Register must not
	 * be zero.  Set the VLAN tag identifier to "1" to enable the
	 * VLAN Hash Table filtering.  This implies that a VLAN tag of
	 * 1 will always pass filtering.
	 */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANTR, VL, 1);

	return 0;
}

static int xgbe_disable_rx_vlan_filtering(struct xgbe_prv_data *pdata)
{
	/* Disable VLAN filtering */
	XGMAC_IOWRITE_BITS(pdata, MAC_PFR, VTFE, 0);

	return 0;
}

#ifndef CRCPOLY_LE
#define CRCPOLY_LE 0xedb88320
#endif
static u32 xgbe_vid_crc32_le(__le16 vid_le)
{
	u32 poly = CRCPOLY_LE;
	u32 crc = ~0;
	u32 temp = 0;
	unsigned char *data = (unsigned char *)&vid_le;
	unsigned char data_byte = 0;
	int i, bits;

	bits = get_bitmask_order(VLAN_VID_MASK);
	for (i = 0; i < bits; i++) {
		if ((i % 8) == 0)
			data_byte = data[i / 8];

		temp = ((crc & 1) ^ data_byte) & 1;
		crc >>= 1;
		data_byte >>= 1;

		if (temp)
			crc ^= poly;
	}

	return crc;
}

static int xgbe_update_vlan_hash_table(struct xgbe_prv_data *pdata)
{
	u32 crc;
	u16 vid;
	__le16 vid_le;
	u16 vlan_hash_table = 0;

	/* Generate the VLAN Hash Table value */
	for_each_set_bit(vid, pdata->active_vlans, VLAN_N_VID) {
		/* Get the CRC32 value of the VLAN ID */
		vid_le = cpu_to_le16(vid);
		crc = bitrev32(~xgbe_vid_crc32_le(vid_le)) >> 28;

		vlan_hash_table |= (1 << crc);
	}

	/* Set the VLAN Hash Table filtering register */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANHTR, VLHT, vlan_hash_table);

	return 0;
}

static void xgbe_tx_desc_reset(struct xgbe_ring_data *rdata)
{
	struct xgbe_ring_desc *rdesc = rdata->rdesc;

	/* Reset the Tx descriptor
	 *   Set buffer 1 (lo) address to zero
	 *   Set buffer 1 (hi) address to zero
	 *   Reset all other control bits (IC, TTSE, B2L & B1L)
	 *   Reset all other control bits (OWN, CTXT, FD, LD, CPC, CIC, etc)
	 */
	rdesc->desc0 = 0;
	rdesc->desc1 = 0;
	rdesc->desc2 = 0;
	rdesc->desc3 = 0;

	/* Make sure ownership is written to the descriptor */
	dma_wmb();
}

static void xgbe_tx_desc_init(struct xgbe_channel *channel)
{
	struct xgbe_ring *ring = channel->tx_ring;
	struct xgbe_ring_data *rdata;
	int i;
	int start_index = ring->cur;

	DBGPR("-->tx_desc_init\n");

	/* Initialze all descriptors */
	for (i = 0; i < ring->rdesc_count; i++) {
		rdata = XGBE_GET_DESC_DATA(ring, i);

		/* Initialize Tx descriptor */
		xgbe_tx_desc_reset(rdata);
	}

	/* Update the total number of Tx descriptors */
	XGMAC_DMA_IOWRITE(channel, DMA_CH_TDRLR, ring->rdesc_count - 1);

	/* Update the starting address of descriptor ring */
	rdata = XGBE_GET_DESC_DATA(ring, start_index);
	XGMAC_DMA_IOWRITE(channel, DMA_CH_TDLR_HI,
			  upper_32_bits(rdata->rdesc_dma));
	XGMAC_DMA_IOWRITE(channel, DMA_CH_TDLR_LO,
			  lower_32_bits(rdata->rdesc_dma));

	DBGPR("<--tx_desc_init\n");
}

static void xgbe_rx_desc_reset(struct xgbe_ring_data *rdata)
{
	struct xgbe_ring_desc *rdesc = rdata->rdesc;

	/* Reset the Rx descriptor
	 *   Set buffer 1 (lo) address to header dma address (lo)
	 *   Set buffer 1 (hi) address to header dma address (hi)
	 *   Set buffer 2 (lo) address to buffer dma address (lo)
	 *   Set buffer 2 (hi) address to buffer dma address (hi) and
	 *     set control bits OWN and INTE
	 */
	rdesc->desc0 = cpu_to_le32(lower_32_bits(rdata->rx.hdr.dma));
	rdesc->desc1 = cpu_to_le32(upper_32_bits(rdata->rx.hdr.dma));
	rdesc->desc2 = cpu_to_le32(lower_32_bits(rdata->rx.buf.dma));
	rdesc->desc3 = cpu_to_le32(upper_32_bits(rdata->rx.buf.dma));

	XGMAC_SET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, INTE,
			  rdata->interrupt ? 1 : 0);

	/* Since the Rx DMA engine is likely running, make sure everything
	 * is written to the descriptor(s) before setting the OWN bit
	 * for the descriptor
	 */
	dma_wmb();

	XGMAC_SET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, OWN, 1);

	/* Make sure ownership is written to the descriptor */
	dma_wmb();
}

static void xgbe_rx_desc_init(struct xgbe_channel *channel)
{
	struct xgbe_prv_data *pdata = channel->pdata;
	struct xgbe_ring *ring = channel->rx_ring;
	struct xgbe_ring_data *rdata;
	unsigned int start_index = ring->cur;
	unsigned int rx_coalesce, rx_frames;
	unsigned int i;

	DBGPR("-->rx_desc_init\n");

	rx_coalesce = (pdata->rx_riwt || pdata->rx_frames) ? 1 : 0;
	rx_frames = pdata->rx_frames;

	/* Initialize all descriptors */
	for (i = 0; i < ring->rdesc_count; i++) {
		rdata = XGBE_GET_DESC_DATA(ring, i);

		/* Set interrupt on completion bit as appropriate */
		if (rx_coalesce && (!rx_frames || ((i + 1) % rx_frames)))
			rdata->interrupt = 0;
		else
			rdata->interrupt = 1;

		/* Initialize Rx descriptor */
		xgbe_rx_desc_reset(rdata);
	}

	/* Update the total number of Rx descriptors */
	XGMAC_DMA_IOWRITE(channel, DMA_CH_RDRLR, ring->rdesc_count - 1);

	/* Update the starting address of descriptor ring */
	rdata = XGBE_GET_DESC_DATA(ring, start_index);
	XGMAC_DMA_IOWRITE(channel, DMA_CH_RDLR_HI,
			  upper_32_bits(rdata->rdesc_dma));
	XGMAC_DMA_IOWRITE(channel, DMA_CH_RDLR_LO,
			  lower_32_bits(rdata->rdesc_dma));

	/* Update the Rx Descriptor Tail Pointer */
	rdata = XGBE_GET_DESC_DATA(ring, start_index + ring->rdesc_count - 1);
	XGMAC_DMA_IOWRITE(channel, DMA_CH_RDTR_LO,
			  lower_32_bits(rdata->rdesc_dma));

	DBGPR("<--rx_desc_init\n");
}

static void xgbe_update_tstamp_addend(struct xgbe_prv_data *pdata,
				      unsigned int addend)
{
	/* Set the addend register value and tell the device */
	XGMAC_IOWRITE(pdata, MAC_TSAR, addend);
	XGMAC_IOWRITE_BITS(pdata, MAC_TSCR, TSADDREG, 1);

	/* Wait for addend update to complete */
	while (XGMAC_IOREAD_BITS(pdata, MAC_TSCR, TSADDREG))
		udelay(5);
}

static void xgbe_set_tstamp_time(struct xgbe_prv_data *pdata, unsigned int sec,
				 unsigned int nsec)
{
	/* Set the time values and tell the device */
	XGMAC_IOWRITE(pdata, MAC_STSUR, sec);
	XGMAC_IOWRITE(pdata, MAC_STNUR, nsec);
	XGMAC_IOWRITE_BITS(pdata, MAC_TSCR, TSINIT, 1);

	/* Wait for time update to complete */
	while (XGMAC_IOREAD_BITS(pdata, MAC_TSCR, TSINIT))
		udelay(5);
}

static u64 xgbe_get_tstamp_time(struct xgbe_prv_data *pdata)
{
	u64 nsec;

	nsec = XGMAC_IOREAD(pdata, MAC_STSR);
	nsec *= NSEC_PER_SEC;
	nsec += XGMAC_IOREAD(pdata, MAC_STNR);

	return nsec;
}

static u64 xgbe_get_tx_tstamp(struct xgbe_prv_data *pdata)
{
	unsigned int tx_snr;
	u64 nsec;

	tx_snr = XGMAC_IOREAD(pdata, MAC_TXSNR);
	if (XGMAC_GET_BITS(tx_snr, MAC_TXSNR, TXTSSTSMIS))
		return 0;

	nsec = XGMAC_IOREAD(pdata, MAC_TXSSR);
	nsec *= NSEC_PER_SEC;
	nsec += tx_snr;

	return nsec;
}

static void xgbe_get_rx_tstamp(struct xgbe_packet_data *packet,
			       struct xgbe_ring_desc *rdesc)
{
	u64 nsec;

	if (XGMAC_GET_BITS_LE(rdesc->desc3, RX_CONTEXT_DESC3, TSA) &&
	    !XGMAC_GET_BITS_LE(rdesc->desc3, RX_CONTEXT_DESC3, TSD)) {
		nsec = le32_to_cpu(rdesc->desc1);
		nsec <<= 32;
		nsec |= le32_to_cpu(rdesc->desc0);
		if (nsec != 0xffffffffffffffffULL) {
			packet->rx_tstamp = nsec;
			XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
				       RX_TSTAMP, 1);
		}
	}
}

static int xgbe_config_tstamp(struct xgbe_prv_data *pdata,
			      unsigned int mac_tscr)
{
	/* Set one nano-second accuracy */
	XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSCTRLSSR, 1);

	/* Set fine timestamp update */
	XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSCFUPDT, 1);

	/* Overwrite earlier timestamps */
	XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TXTSSTSM, 1);

	XGMAC_IOWRITE(pdata, MAC_TSCR, mac_tscr);

	/* Exit if timestamping is not enabled */
	if (!XGMAC_GET_BITS(mac_tscr, MAC_TSCR, TSENA))
		return 0;

	/* Initialize time registers */
	XGMAC_IOWRITE_BITS(pdata, MAC_SSIR, SSINC, XGBE_TSTAMP_SSINC);
	XGMAC_IOWRITE_BITS(pdata, MAC_SSIR, SNSINC, XGBE_TSTAMP_SNSINC);
	xgbe_update_tstamp_addend(pdata, pdata->tstamp_addend);
	xgbe_set_tstamp_time(pdata, 0, 0);

	/* Initialize the timecounter */
	timecounter_init(&pdata->tstamp_tc, &pdata->tstamp_cc,
			 ktime_to_ns(ktime_get_real()));

	return 0;
}

static void xgbe_config_dcb_tc(struct xgbe_prv_data *pdata)
{
	struct ieee_ets *ets = pdata->ets;
	unsigned int total_weight, min_weight, weight;
	unsigned int i;

	if (!ets)
		return;

	/* Set Tx to deficit weighted round robin scheduling algorithm (when
	 * traffic class is using ETS algorithm)
	 */
	XGMAC_IOWRITE_BITS(pdata, MTL_OMR, ETSALG, MTL_ETSALG_DWRR);

	/* Set Traffic Class algorithms */
	total_weight = pdata->netdev->mtu * pdata->hw_feat.tc_cnt;
	min_weight = total_weight / 100;
	if (!min_weight)
		min_weight = 1;

	for (i = 0; i < pdata->hw_feat.tc_cnt; i++) {
		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_STRICT:
			DBGPR("  TC%u using SP\n", i);
			XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_TC_ETSCR, TSA,
					       MTL_TSA_SP);
			break;
		case IEEE_8021QAZ_TSA_ETS:
			weight = total_weight * ets->tc_tx_bw[i] / 100;
			weight = clamp(weight, min_weight, total_weight);

			DBGPR("  TC%u using DWRR (weight %u)\n", i, weight);
			XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_TC_ETSCR, TSA,
					       MTL_TSA_ETS);
			XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_TC_QWR, QW,
					       weight);
			break;
		}
	}
}

static void xgbe_config_dcb_pfc(struct xgbe_prv_data *pdata)
{
	struct ieee_pfc *pfc = pdata->pfc;
	struct ieee_ets *ets = pdata->ets;
	unsigned int mask, reg, reg_val;
	unsigned int tc, prio;

	if (!pfc || !ets)
		return;

	for (tc = 0; tc < pdata->hw_feat.tc_cnt; tc++) {
		mask = 0;
		for (prio = 0; prio < IEEE_8021QAZ_MAX_TCS; prio++) {
			if ((pfc->pfc_en & (1 << prio)) &&
			    (ets->prio_tc[prio] == tc))
				mask |= (1 << prio);
		}
		mask &= 0xff;

		DBGPR("  TC%u PFC mask=%#x\n", tc, mask);
		reg = MTL_TCPM0R + (MTL_TCPM_INC * (tc / MTL_TCPM_TC_PER_REG));
		reg_val = XGMAC_IOREAD(pdata, reg);

		reg_val &= ~(0xff << ((tc % MTL_TCPM_TC_PER_REG) << 3));
		reg_val |= (mask << ((tc % MTL_TCPM_TC_PER_REG) << 3));

		XGMAC_IOWRITE(pdata, reg, reg_val);
	}

	xgbe_config_flow_control(pdata);
}

static void xgbe_tx_start_xmit(struct xgbe_channel *channel,
			       struct xgbe_ring *ring)
{
	struct xgbe_prv_data *pdata = channel->pdata;
	struct xgbe_ring_data *rdata;

	/* Make sure everything is written before the register write */
	wmb();

	/* Issue a poll command to Tx DMA by writing address
	 * of next immediate free descriptor */
	rdata = XGBE_GET_DESC_DATA(ring, ring->cur);
	XGMAC_DMA_IOWRITE(channel, DMA_CH_TDTR_LO,
			  lower_32_bits(rdata->rdesc_dma));

	/* Start the Tx coalescing timer */
	if (pdata->tx_usecs && !channel->tx_timer_active) {
		channel->tx_timer_active = 1;
		hrtimer_start(&channel->tx_timer,
			      ktime_set(0, pdata->tx_usecs * NSEC_PER_USEC),
			      HRTIMER_MODE_REL);
	}

	ring->tx.xmit_more = 0;
}

static void xgbe_dev_xmit(struct xgbe_channel *channel)
{
	struct xgbe_prv_data *pdata = channel->pdata;
	struct xgbe_ring *ring = channel->tx_ring;
	struct xgbe_ring_data *rdata;
	struct xgbe_ring_desc *rdesc;
	struct xgbe_packet_data *packet = &ring->packet_data;
	unsigned int csum, tso, vlan;
	unsigned int tso_context, vlan_context;
	unsigned int tx_set_ic;
	int start_index = ring->cur;
	int cur_index = ring->cur;
	int i;

	DBGPR("-->xgbe_dev_xmit\n");

	csum = XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			      CSUM_ENABLE);
	tso = XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			     TSO_ENABLE);
	vlan = XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			      VLAN_CTAG);

	if (tso && (packet->mss != ring->tx.cur_mss))
		tso_context = 1;
	else
		tso_context = 0;

	if (vlan && (packet->vlan_ctag != ring->tx.cur_vlan_ctag))
		vlan_context = 1;
	else
		vlan_context = 0;

	/* Determine if an interrupt should be generated for this Tx:
	 *   Interrupt:
	 *     - Tx frame count exceeds the frame count setting
	 *     - Addition of Tx frame count to the frame count since the
	 *       last interrupt was set exceeds the frame count setting
	 *   No interrupt:
	 *     - No frame count setting specified (ethtool -C ethX tx-frames 0)
	 *     - Addition of Tx frame count to the frame count since the
	 *       last interrupt was set does not exceed the frame count setting
	 */
	ring->coalesce_count += packet->tx_packets;
	if (!pdata->tx_frames)
		tx_set_ic = 0;
	else if (packet->tx_packets > pdata->tx_frames)
		tx_set_ic = 1;
	else if ((ring->coalesce_count % pdata->tx_frames) <
		 packet->tx_packets)
		tx_set_ic = 1;
	else
		tx_set_ic = 0;

	rdata = XGBE_GET_DESC_DATA(ring, cur_index);
	rdesc = rdata->rdesc;

	/* Create a context descriptor if this is a TSO packet */
	if (tso_context || vlan_context) {
		if (tso_context) {
			DBGPR("  TSO context descriptor, mss=%u\n",
			      packet->mss);

			/* Set the MSS size */
			XGMAC_SET_BITS_LE(rdesc->desc2, TX_CONTEXT_DESC2,
					  MSS, packet->mss);

			/* Mark it as a CONTEXT descriptor */
			XGMAC_SET_BITS_LE(rdesc->desc3, TX_CONTEXT_DESC3,
					  CTXT, 1);

			/* Indicate this descriptor contains the MSS */
			XGMAC_SET_BITS_LE(rdesc->desc3, TX_CONTEXT_DESC3,
					  TCMSSV, 1);

			ring->tx.cur_mss = packet->mss;
		}

		if (vlan_context) {
			DBGPR("  VLAN context descriptor, ctag=%u\n",
			      packet->vlan_ctag);

			/* Mark it as a CONTEXT descriptor */
			XGMAC_SET_BITS_LE(rdesc->desc3, TX_CONTEXT_DESC3,
					  CTXT, 1);

			/* Set the VLAN tag */
			XGMAC_SET_BITS_LE(rdesc->desc3, TX_CONTEXT_DESC3,
					  VT, packet->vlan_ctag);

			/* Indicate this descriptor contains the VLAN tag */
			XGMAC_SET_BITS_LE(rdesc->desc3, TX_CONTEXT_DESC3,
					  VLTV, 1);

			ring->tx.cur_vlan_ctag = packet->vlan_ctag;
		}

		cur_index++;
		rdata = XGBE_GET_DESC_DATA(ring, cur_index);
		rdesc = rdata->rdesc;
	}

	/* Update buffer address (for TSO this is the header) */
	rdesc->desc0 =  cpu_to_le32(lower_32_bits(rdata->skb_dma));
	rdesc->desc1 =  cpu_to_le32(upper_32_bits(rdata->skb_dma));

	/* Update the buffer length */
	XGMAC_SET_BITS_LE(rdesc->desc2, TX_NORMAL_DESC2, HL_B1L,
			  rdata->skb_dma_len);

	/* VLAN tag insertion check */
	if (vlan)
		XGMAC_SET_BITS_LE(rdesc->desc2, TX_NORMAL_DESC2, VTIR,
				  TX_NORMAL_DESC2_VLAN_INSERT);

	/* Timestamp enablement check */
	if (XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES, PTP))
		XGMAC_SET_BITS_LE(rdesc->desc2, TX_NORMAL_DESC2, TTSE, 1);

	/* Mark it as First Descriptor */
	XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, FD, 1);

	/* Mark it as a NORMAL descriptor */
	XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, CTXT, 0);

	/* Set OWN bit if not the first descriptor */
	if (cur_index != start_index)
		XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, OWN, 1);

	if (tso) {
		/* Enable TSO */
		XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, TSE, 1);
		XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, TCPPL,
				  packet->tcp_payload_len);
		XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, TCPHDRLEN,
				  packet->tcp_header_len / 4);
	} else {
		/* Enable CRC and Pad Insertion */
		XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, CPC, 0);

		/* Enable HW CSUM */
		if (csum)
			XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3,
					  CIC, 0x3);

		/* Set the total length to be transmitted */
		XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, FL,
				  packet->length);
	}

	for (i = cur_index - start_index + 1; i < packet->rdesc_count; i++) {
		cur_index++;
		rdata = XGBE_GET_DESC_DATA(ring, cur_index);
		rdesc = rdata->rdesc;

		/* Update buffer address */
		rdesc->desc0 = cpu_to_le32(lower_32_bits(rdata->skb_dma));
		rdesc->desc1 = cpu_to_le32(upper_32_bits(rdata->skb_dma));

		/* Update the buffer length */
		XGMAC_SET_BITS_LE(rdesc->desc2, TX_NORMAL_DESC2, HL_B1L,
				  rdata->skb_dma_len);

		/* Set OWN bit */
		XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, OWN, 1);

		/* Mark it as NORMAL descriptor */
		XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, CTXT, 0);

		/* Enable HW CSUM */
		if (csum)
			XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3,
					  CIC, 0x3);
	}

	/* Set LAST bit for the last descriptor */
	XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, LD, 1);

	/* Set IC bit based on Tx coalescing settings */
	if (tx_set_ic)
		XGMAC_SET_BITS_LE(rdesc->desc2, TX_NORMAL_DESC2, IC, 1);

	/* Save the Tx info to report back during cleanup */
	rdata->tx.packets = packet->tx_packets;
	rdata->tx.bytes = packet->tx_bytes;

	/* In case the Tx DMA engine is running, make sure everything
	 * is written to the descriptor(s) before setting the OWN bit
	 * for the first descriptor
	 */
	dma_wmb();

	/* Set OWN bit for the first descriptor */
	rdata = XGBE_GET_DESC_DATA(ring, start_index);
	rdesc = rdata->rdesc;
	XGMAC_SET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, OWN, 1);

#ifdef XGMAC_ENABLE_TX_DESC_DUMP
	xgbe_dump_tx_desc(ring, start_index, packet->rdesc_count, 1);
#endif

	/* Make sure ownership is written to the descriptor */
	dma_wmb();

	ring->cur = cur_index + 1;
	if (!packet->skb->xmit_more ||
	    netif_xmit_stopped(netdev_get_tx_queue(pdata->netdev,
						   channel->queue_index)))
		xgbe_tx_start_xmit(channel, ring);
	else
		ring->tx.xmit_more = 1;

	DBGPR("  %s: descriptors %u to %u written\n",
	      channel->name, start_index & (ring->rdesc_count - 1),
	      (ring->cur - 1) & (ring->rdesc_count - 1));

	DBGPR("<--xgbe_dev_xmit\n");
}

static int xgbe_dev_read(struct xgbe_channel *channel)
{
	struct xgbe_ring *ring = channel->rx_ring;
	struct xgbe_ring_data *rdata;
	struct xgbe_ring_desc *rdesc;
	struct xgbe_packet_data *packet = &ring->packet_data;
	struct net_device *netdev = channel->pdata->netdev;
	unsigned int err, etlt, l34t;

	DBGPR("-->xgbe_dev_read: cur = %d\n", ring->cur);

	rdata = XGBE_GET_DESC_DATA(ring, ring->cur);
	rdesc = rdata->rdesc;

	/* Check for data availability */
	if (XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, OWN))
		return 1;

	/* Make sure descriptor fields are read after reading the OWN bit */
	dma_rmb();

#ifdef XGMAC_ENABLE_RX_DESC_DUMP
	xgbe_dump_rx_desc(ring, rdesc, ring->cur);
#endif

	if (XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, CTXT)) {
		/* Timestamp Context Descriptor */
		xgbe_get_rx_tstamp(packet, rdesc);

		XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
			       CONTEXT, 1);
		XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
			       CONTEXT_NEXT, 0);
		return 0;
	}

	/* Normal Descriptor, be sure Context Descriptor bit is off */
	XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES, CONTEXT, 0);

	/* Indicate if a Context Descriptor is next */
	if (XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, CDA))
		XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
			       CONTEXT_NEXT, 1);

	/* Get the header length */
	if (XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, FD))
		rdata->rx.hdr_len = XGMAC_GET_BITS_LE(rdesc->desc2,
						      RX_NORMAL_DESC2, HL);

	/* Get the RSS hash */
	if (XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, RSV)) {
		XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
			       RSS_HASH, 1);

		packet->rss_hash = le32_to_cpu(rdesc->desc1);

		l34t = XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, L34T);
		switch (l34t) {
		case RX_DESC3_L34T_IPV4_TCP:
		case RX_DESC3_L34T_IPV4_UDP:
		case RX_DESC3_L34T_IPV6_TCP:
		case RX_DESC3_L34T_IPV6_UDP:
			packet->rss_hash_type = PKT_HASH_TYPE_L4;
			break;
		default:
			packet->rss_hash_type = PKT_HASH_TYPE_L3;
		}
	}

	/* Get the packet length */
	rdata->rx.len = XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, PL);

	if (!XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, LD)) {
		/* Not all the data has been transferred for this packet */
		XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
			       INCOMPLETE, 1);
		return 0;
	}

	/* This is the last of the data for this packet */
	XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
		       INCOMPLETE, 0);

	/* Set checksum done indicator as appropriate */
	if (channel->pdata->netdev->features & NETIF_F_RXCSUM)
		XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
			       CSUM_DONE, 1);

	/* Check for errors (only valid in last descriptor) */
	err = XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, ES);
	etlt = XGMAC_GET_BITS_LE(rdesc->desc3, RX_NORMAL_DESC3, ETLT);
	DBGPR("  err=%u, etlt=%#x\n", err, etlt);

	if (!err || !etlt) {
		/* No error if err is 0 or etlt is 0 */
		if ((etlt == 0x09) &&
		    (netdev->features & NETIF_F_HW_VLAN_CTAG_RX)) {
			XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
				       VLAN_CTAG, 1);
			packet->vlan_ctag = XGMAC_GET_BITS_LE(rdesc->desc0,
							      RX_NORMAL_DESC0,
							      OVT);
			DBGPR("  vlan-ctag=0x%04x\n", packet->vlan_ctag);
		}
	} else {
		if ((etlt == 0x05) || (etlt == 0x06))
			XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
				       CSUM_DONE, 0);
		else
			XGMAC_SET_BITS(packet->errors, RX_PACKET_ERRORS,
				       FRAME, 1);
	}

	DBGPR("<--xgbe_dev_read: %s - descriptor=%u (cur=%d)\n", channel->name,
	      ring->cur & (ring->rdesc_count - 1), ring->cur);

	return 0;
}

static int xgbe_is_context_desc(struct xgbe_ring_desc *rdesc)
{
	/* Rx and Tx share CTXT bit, so check TDES3.CTXT bit */
	return XGMAC_GET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, CTXT);
}

static int xgbe_is_last_desc(struct xgbe_ring_desc *rdesc)
{
	/* Rx and Tx share LD bit, so check TDES3.LD bit */
	return XGMAC_GET_BITS_LE(rdesc->desc3, TX_NORMAL_DESC3, LD);
}

static int xgbe_enable_int(struct xgbe_channel *channel,
			   enum xgbe_int int_id)
{
	unsigned int dma_ch_ier;

	dma_ch_ier = XGMAC_DMA_IOREAD(channel, DMA_CH_IER);

	switch (int_id) {
	case XGMAC_INT_DMA_CH_SR_TI:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, TIE, 1);
		break;
	case XGMAC_INT_DMA_CH_SR_TPS:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, TXSE, 1);
		break;
	case XGMAC_INT_DMA_CH_SR_TBU:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, TBUE, 1);
		break;
	case XGMAC_INT_DMA_CH_SR_RI:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RIE, 1);
		break;
	case XGMAC_INT_DMA_CH_SR_RBU:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RBUE, 1);
		break;
	case XGMAC_INT_DMA_CH_SR_RPS:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RSE, 1);
		break;
	case XGMAC_INT_DMA_CH_SR_TI_RI:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, TIE, 1);
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RIE, 1);
		break;
	case XGMAC_INT_DMA_CH_SR_FBE:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, FBEE, 1);
		break;
	case XGMAC_INT_DMA_ALL:
		dma_ch_ier |= channel->saved_ier;
		break;
	default:
		return -1;
	}

	XGMAC_DMA_IOWRITE(channel, DMA_CH_IER, dma_ch_ier);

	return 0;
}

static int xgbe_disable_int(struct xgbe_channel *channel,
			    enum xgbe_int int_id)
{
	unsigned int dma_ch_ier;

	dma_ch_ier = XGMAC_DMA_IOREAD(channel, DMA_CH_IER);

	switch (int_id) {
	case XGMAC_INT_DMA_CH_SR_TI:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, TIE, 0);
		break;
	case XGMAC_INT_DMA_CH_SR_TPS:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, TXSE, 0);
		break;
	case XGMAC_INT_DMA_CH_SR_TBU:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, TBUE, 0);
		break;
	case XGMAC_INT_DMA_CH_SR_RI:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RIE, 0);
		break;
	case XGMAC_INT_DMA_CH_SR_RBU:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RBUE, 0);
		break;
	case XGMAC_INT_DMA_CH_SR_RPS:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RSE, 0);
		break;
	case XGMAC_INT_DMA_CH_SR_TI_RI:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, TIE, 0);
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, RIE, 0);
		break;
	case XGMAC_INT_DMA_CH_SR_FBE:
		XGMAC_SET_BITS(dma_ch_ier, DMA_CH_IER, FBEE, 0);
		break;
	case XGMAC_INT_DMA_ALL:
		channel->saved_ier = dma_ch_ier & XGBE_DMA_INTERRUPT_MASK;
		dma_ch_ier &= ~XGBE_DMA_INTERRUPT_MASK;
		break;
	default:
		return -1;
	}

	XGMAC_DMA_IOWRITE(channel, DMA_CH_IER, dma_ch_ier);

	return 0;
}

static int xgbe_exit(struct xgbe_prv_data *pdata)
{
	unsigned int count = 2000;

	DBGPR("-->xgbe_exit\n");

	/* Issue a software reset */
	XGMAC_IOWRITE_BITS(pdata, DMA_MR, SWR, 1);
	usleep_range(10, 15);

	/* Poll Until Poll Condition */
	while (count-- && XGMAC_IOREAD_BITS(pdata, DMA_MR, SWR))
		usleep_range(500, 600);

	if (!count)
		return -EBUSY;

	DBGPR("<--xgbe_exit\n");

	return 0;
}

static int xgbe_flush_tx_queues(struct xgbe_prv_data *pdata)
{
	unsigned int i, count;

	if (XGMAC_GET_BITS(pdata->hw_feat.version, MAC_VR, SNPSVER) < 0x21)
		return 0;

	for (i = 0; i < pdata->tx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_TQOMR, FTQ, 1);

	/* Poll Until Poll Condition */
	for (i = 0; i < pdata->tx_q_count; i++) {
		count = 2000;
		while (count-- && XGMAC_MTL_IOREAD_BITS(pdata, i,
							MTL_Q_TQOMR, FTQ))
			usleep_range(500, 600);

		if (!count)
			return -EBUSY;
	}

	return 0;
}

static void xgbe_config_dma_bus(struct xgbe_prv_data *pdata)
{
	/* Set enhanced addressing mode */
	XGMAC_IOWRITE_BITS(pdata, DMA_SBMR, EAME, 1);

	/* Set the System Bus mode */
	XGMAC_IOWRITE_BITS(pdata, DMA_SBMR, UNDEF, 1);
	XGMAC_IOWRITE_BITS(pdata, DMA_SBMR, BLEN_256, 1);
}

static void xgbe_config_dma_cache(struct xgbe_prv_data *pdata)
{
	unsigned int arcache, awcache;

	arcache = 0;
	XGMAC_SET_BITS(arcache, DMA_AXIARCR, DRC, pdata->arcache);
	XGMAC_SET_BITS(arcache, DMA_AXIARCR, DRD, pdata->axdomain);
	XGMAC_SET_BITS(arcache, DMA_AXIARCR, TEC, pdata->arcache);
	XGMAC_SET_BITS(arcache, DMA_AXIARCR, TED, pdata->axdomain);
	XGMAC_SET_BITS(arcache, DMA_AXIARCR, THC, pdata->arcache);
	XGMAC_SET_BITS(arcache, DMA_AXIARCR, THD, pdata->axdomain);
	XGMAC_IOWRITE(pdata, DMA_AXIARCR, arcache);

	awcache = 0;
	XGMAC_SET_BITS(awcache, DMA_AXIAWCR, DWC, pdata->awcache);
	XGMAC_SET_BITS(awcache, DMA_AXIAWCR, DWD, pdata->axdomain);
	XGMAC_SET_BITS(awcache, DMA_AXIAWCR, RPC, pdata->awcache);
	XGMAC_SET_BITS(awcache, DMA_AXIAWCR, RPD, pdata->axdomain);
	XGMAC_SET_BITS(awcache, DMA_AXIAWCR, RHC, pdata->awcache);
	XGMAC_SET_BITS(awcache, DMA_AXIAWCR, RHD, pdata->axdomain);
	XGMAC_SET_BITS(awcache, DMA_AXIAWCR, TDC, pdata->awcache);
	XGMAC_SET_BITS(awcache, DMA_AXIAWCR, TDD, pdata->axdomain);
	XGMAC_IOWRITE(pdata, DMA_AXIAWCR, awcache);
}

static void xgbe_config_mtl_mode(struct xgbe_prv_data *pdata)
{
	unsigned int i;

	/* Set Tx to weighted round robin scheduling algorithm */
	XGMAC_IOWRITE_BITS(pdata, MTL_OMR, ETSALG, MTL_ETSALG_WRR);

	/* Set Tx traffic classes to use WRR algorithm with equal weights */
	for (i = 0; i < pdata->hw_feat.tc_cnt; i++) {
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_TC_ETSCR, TSA,
				       MTL_TSA_ETS);
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_TC_QWR, QW, 1);
	}

	/* Set Rx to strict priority algorithm */
	XGMAC_IOWRITE_BITS(pdata, MTL_OMR, RAA, MTL_RAA_SP);
}

static unsigned int xgbe_calculate_per_queue_fifo(unsigned int fifo_size,
						  unsigned int queue_count)
{
	unsigned int q_fifo_size = 0;
	enum xgbe_mtl_fifo_size p_fifo = XGMAC_MTL_FIFO_SIZE_256;

	/* Calculate Tx/Rx fifo share per queue */
	switch (fifo_size) {
	case 0:
		q_fifo_size = XGBE_FIFO_SIZE_B(128);
		break;
	case 1:
		q_fifo_size = XGBE_FIFO_SIZE_B(256);
		break;
	case 2:
		q_fifo_size = XGBE_FIFO_SIZE_B(512);
		break;
	case 3:
		q_fifo_size = XGBE_FIFO_SIZE_KB(1);
		break;
	case 4:
		q_fifo_size = XGBE_FIFO_SIZE_KB(2);
		break;
	case 5:
		q_fifo_size = XGBE_FIFO_SIZE_KB(4);
		break;
	case 6:
		q_fifo_size = XGBE_FIFO_SIZE_KB(8);
		break;
	case 7:
		q_fifo_size = XGBE_FIFO_SIZE_KB(16);
		break;
	case 8:
		q_fifo_size = XGBE_FIFO_SIZE_KB(32);
		break;
	case 9:
		q_fifo_size = XGBE_FIFO_SIZE_KB(64);
		break;
	case 10:
		q_fifo_size = XGBE_FIFO_SIZE_KB(128);
		break;
	case 11:
		q_fifo_size = XGBE_FIFO_SIZE_KB(256);
		break;
	}

	/* The configured value is not the actual amount of fifo RAM */
	q_fifo_size = min_t(unsigned int, XGBE_FIFO_MAX, q_fifo_size);

	q_fifo_size = q_fifo_size / queue_count;

	/* Set the queue fifo size programmable value */
	if (q_fifo_size >= XGBE_FIFO_SIZE_KB(256))
		p_fifo = XGMAC_MTL_FIFO_SIZE_256K;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_KB(128))
		p_fifo = XGMAC_MTL_FIFO_SIZE_128K;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_KB(64))
		p_fifo = XGMAC_MTL_FIFO_SIZE_64K;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_KB(32))
		p_fifo = XGMAC_MTL_FIFO_SIZE_32K;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_KB(16))
		p_fifo = XGMAC_MTL_FIFO_SIZE_16K;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_KB(8))
		p_fifo = XGMAC_MTL_FIFO_SIZE_8K;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_KB(4))
		p_fifo = XGMAC_MTL_FIFO_SIZE_4K;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_KB(2))
		p_fifo = XGMAC_MTL_FIFO_SIZE_2K;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_KB(1))
		p_fifo = XGMAC_MTL_FIFO_SIZE_1K;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_B(512))
		p_fifo = XGMAC_MTL_FIFO_SIZE_512;
	else if (q_fifo_size >= XGBE_FIFO_SIZE_B(256))
		p_fifo = XGMAC_MTL_FIFO_SIZE_256;

	return p_fifo;
}

static void xgbe_config_tx_fifo_size(struct xgbe_prv_data *pdata)
{
	enum xgbe_mtl_fifo_size fifo_size;
	unsigned int i;

	fifo_size = xgbe_calculate_per_queue_fifo(pdata->hw_feat.tx_fifo_size,
						  pdata->tx_q_count);

	for (i = 0; i < pdata->tx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_TQOMR, TQS, fifo_size);

	netdev_notice(pdata->netdev,
		      "%d Tx hardware queues, %d byte fifo per queue\n",
		      pdata->tx_q_count, ((fifo_size + 1) * 256));
}

static void xgbe_config_rx_fifo_size(struct xgbe_prv_data *pdata)
{
	enum xgbe_mtl_fifo_size fifo_size;
	unsigned int i;

	fifo_size = xgbe_calculate_per_queue_fifo(pdata->hw_feat.rx_fifo_size,
						  pdata->rx_q_count);

	for (i = 0; i < pdata->rx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_RQOMR, RQS, fifo_size);

	netdev_notice(pdata->netdev,
		      "%d Rx hardware queues, %d byte fifo per queue\n",
		      pdata->rx_q_count, ((fifo_size + 1) * 256));
}

static void xgbe_config_queue_mapping(struct xgbe_prv_data *pdata)
{
	unsigned int qptc, qptc_extra, queue;
	unsigned int prio_queues;
	unsigned int ppq, ppq_extra, prio;
	unsigned int mask;
	unsigned int i, j, reg, reg_val;

	/* Map the MTL Tx Queues to Traffic Classes
	 *   Note: Tx Queues >= Traffic Classes
	 */
	qptc = pdata->tx_q_count / pdata->hw_feat.tc_cnt;
	qptc_extra = pdata->tx_q_count % pdata->hw_feat.tc_cnt;

	for (i = 0, queue = 0; i < pdata->hw_feat.tc_cnt; i++) {
		for (j = 0; j < qptc; j++) {
			DBGPR("  TXq%u mapped to TC%u\n", queue, i);
			XGMAC_MTL_IOWRITE_BITS(pdata, queue, MTL_Q_TQOMR,
					       Q2TCMAP, i);
			pdata->q2tc_map[queue++] = i;
		}

		if (i < qptc_extra) {
			DBGPR("  TXq%u mapped to TC%u\n", queue, i);
			XGMAC_MTL_IOWRITE_BITS(pdata, queue, MTL_Q_TQOMR,
					       Q2TCMAP, i);
			pdata->q2tc_map[queue++] = i;
		}
	}

	/* Map the 8 VLAN priority values to available MTL Rx queues */
	prio_queues = min_t(unsigned int, IEEE_8021QAZ_MAX_TCS,
			    pdata->rx_q_count);
	ppq = IEEE_8021QAZ_MAX_TCS / prio_queues;
	ppq_extra = IEEE_8021QAZ_MAX_TCS % prio_queues;

	reg = MAC_RQC2R;
	reg_val = 0;
	for (i = 0, prio = 0; i < prio_queues;) {
		mask = 0;
		for (j = 0; j < ppq; j++) {
			DBGPR("  PRIO%u mapped to RXq%u\n", prio, i);
			mask |= (1 << prio);
			pdata->prio2q_map[prio++] = i;
		}

		if (i < ppq_extra) {
			DBGPR("  PRIO%u mapped to RXq%u\n", prio, i);
			mask |= (1 << prio);
			pdata->prio2q_map[prio++] = i;
		}

		reg_val |= (mask << ((i++ % MAC_RQC2_Q_PER_REG) << 3));

		if ((i % MAC_RQC2_Q_PER_REG) && (i != prio_queues))
			continue;

		XGMAC_IOWRITE(pdata, reg, reg_val);
		reg += MAC_RQC2_INC;
		reg_val = 0;
	}

	/* Select dynamic mapping of MTL Rx queue to DMA Rx channel */
	reg = MTL_RQDCM0R;
	reg_val = 0;
	for (i = 0; i < pdata->rx_q_count;) {
		reg_val |= (0x80 << ((i++ % MTL_RQDCM_Q_PER_REG) << 3));

		if ((i % MTL_RQDCM_Q_PER_REG) && (i != pdata->rx_q_count))
			continue;

		XGMAC_IOWRITE(pdata, reg, reg_val);

		reg += MTL_RQDCM_INC;
		reg_val = 0;
	}
}

static void xgbe_config_flow_control_threshold(struct xgbe_prv_data *pdata)
{
	unsigned int i;

	for (i = 0; i < pdata->rx_q_count; i++) {
		/* Activate flow control when less than 4k left in fifo */
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_RQFCR, RFA, 2);

		/* De-activate flow control when more than 6k left in fifo */
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_RQFCR, RFD, 4);
	}
}

static void xgbe_config_mac_address(struct xgbe_prv_data *pdata)
{
	xgbe_set_mac_address(pdata, pdata->netdev->dev_addr);

	/* Filtering is done using perfect filtering and hash filtering */
	if (pdata->hw_feat.hash_table_size) {
		XGMAC_IOWRITE_BITS(pdata, MAC_PFR, HPF, 1);
		XGMAC_IOWRITE_BITS(pdata, MAC_PFR, HUC, 1);
		XGMAC_IOWRITE_BITS(pdata, MAC_PFR, HMC, 1);
	}
}

static void xgbe_config_jumbo_enable(struct xgbe_prv_data *pdata)
{
	unsigned int val;

	val = (pdata->netdev->mtu > XGMAC_STD_PACKET_MTU) ? 1 : 0;

	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, JE, val);
}

static void xgbe_config_mac_speed(struct xgbe_prv_data *pdata)
{
	switch (pdata->phy_speed) {
	case SPEED_10000:
		xgbe_set_xgmii_speed(pdata);
		break;

	case SPEED_2500:
		xgbe_set_gmii_2500_speed(pdata);
		break;

	case SPEED_1000:
		xgbe_set_gmii_speed(pdata);
		break;
	}
}

static void xgbe_config_checksum_offload(struct xgbe_prv_data *pdata)
{
	if (pdata->netdev->features & NETIF_F_RXCSUM)
		xgbe_enable_rx_csum(pdata);
	else
		xgbe_disable_rx_csum(pdata);
}

static void xgbe_config_vlan_support(struct xgbe_prv_data *pdata)
{
	/* Indicate that VLAN Tx CTAGs come from context descriptors */
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANIR, CSVL, 0);
	XGMAC_IOWRITE_BITS(pdata, MAC_VLANIR, VLTI, 1);

	/* Set the current VLAN Hash Table register value */
	xgbe_update_vlan_hash_table(pdata);

	if (pdata->netdev->features & NETIF_F_HW_VLAN_CTAG_FILTER)
		xgbe_enable_rx_vlan_filtering(pdata);
	else
		xgbe_disable_rx_vlan_filtering(pdata);

	if (pdata->netdev->features & NETIF_F_HW_VLAN_CTAG_RX)
		xgbe_enable_rx_vlan_stripping(pdata);
	else
		xgbe_disable_rx_vlan_stripping(pdata);
}

static u64 xgbe_mmc_read(struct xgbe_prv_data *pdata, unsigned int reg_lo)
{
	bool read_hi;
	u64 val;

	switch (reg_lo) {
	/* These registers are always 64 bit */
	case MMC_TXOCTETCOUNT_GB_LO:
	case MMC_TXOCTETCOUNT_G_LO:
	case MMC_RXOCTETCOUNT_GB_LO:
	case MMC_RXOCTETCOUNT_G_LO:
		read_hi = true;
		break;

	default:
		read_hi = false;
	};

	val = XGMAC_IOREAD(pdata, reg_lo);

	if (read_hi)
		val |= ((u64)XGMAC_IOREAD(pdata, reg_lo + 4) << 32);

	return val;
}

static void xgbe_tx_mmc_int(struct xgbe_prv_data *pdata)
{
	struct xgbe_mmc_stats *stats = &pdata->mmc_stats;
	unsigned int mmc_isr = XGMAC_IOREAD(pdata, MMC_TISR);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXOCTETCOUNT_GB))
		stats->txoctetcount_gb +=
			xgbe_mmc_read(pdata, MMC_TXOCTETCOUNT_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXFRAMECOUNT_GB))
		stats->txframecount_gb +=
			xgbe_mmc_read(pdata, MMC_TXFRAMECOUNT_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXBROADCASTFRAMES_G))
		stats->txbroadcastframes_g +=
			xgbe_mmc_read(pdata, MMC_TXBROADCASTFRAMES_G_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXMULTICASTFRAMES_G))
		stats->txmulticastframes_g +=
			xgbe_mmc_read(pdata, MMC_TXMULTICASTFRAMES_G_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TX64OCTETS_GB))
		stats->tx64octets_gb +=
			xgbe_mmc_read(pdata, MMC_TX64OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TX65TO127OCTETS_GB))
		stats->tx65to127octets_gb +=
			xgbe_mmc_read(pdata, MMC_TX65TO127OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TX128TO255OCTETS_GB))
		stats->tx128to255octets_gb +=
			xgbe_mmc_read(pdata, MMC_TX128TO255OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TX256TO511OCTETS_GB))
		stats->tx256to511octets_gb +=
			xgbe_mmc_read(pdata, MMC_TX256TO511OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TX512TO1023OCTETS_GB))
		stats->tx512to1023octets_gb +=
			xgbe_mmc_read(pdata, MMC_TX512TO1023OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TX1024TOMAXOCTETS_GB))
		stats->tx1024tomaxoctets_gb +=
			xgbe_mmc_read(pdata, MMC_TX1024TOMAXOCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXUNICASTFRAMES_GB))
		stats->txunicastframes_gb +=
			xgbe_mmc_read(pdata, MMC_TXUNICASTFRAMES_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXMULTICASTFRAMES_GB))
		stats->txmulticastframes_gb +=
			xgbe_mmc_read(pdata, MMC_TXMULTICASTFRAMES_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXBROADCASTFRAMES_GB))
		stats->txbroadcastframes_g +=
			xgbe_mmc_read(pdata, MMC_TXBROADCASTFRAMES_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXUNDERFLOWERROR))
		stats->txunderflowerror +=
			xgbe_mmc_read(pdata, MMC_TXUNDERFLOWERROR_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXOCTETCOUNT_G))
		stats->txoctetcount_g +=
			xgbe_mmc_read(pdata, MMC_TXOCTETCOUNT_G_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXFRAMECOUNT_G))
		stats->txframecount_g +=
			xgbe_mmc_read(pdata, MMC_TXFRAMECOUNT_G_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXPAUSEFRAMES))
		stats->txpauseframes +=
			xgbe_mmc_read(pdata, MMC_TXPAUSEFRAMES_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_TISR, TXVLANFRAMES_G))
		stats->txvlanframes_g +=
			xgbe_mmc_read(pdata, MMC_TXVLANFRAMES_G_LO);
}

static void xgbe_rx_mmc_int(struct xgbe_prv_data *pdata)
{
	struct xgbe_mmc_stats *stats = &pdata->mmc_stats;
	unsigned int mmc_isr = XGMAC_IOREAD(pdata, MMC_RISR);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXFRAMECOUNT_GB))
		stats->rxframecount_gb +=
			xgbe_mmc_read(pdata, MMC_RXFRAMECOUNT_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXOCTETCOUNT_GB))
		stats->rxoctetcount_gb +=
			xgbe_mmc_read(pdata, MMC_RXOCTETCOUNT_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXOCTETCOUNT_G))
		stats->rxoctetcount_g +=
			xgbe_mmc_read(pdata, MMC_RXOCTETCOUNT_G_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXBROADCASTFRAMES_G))
		stats->rxbroadcastframes_g +=
			xgbe_mmc_read(pdata, MMC_RXBROADCASTFRAMES_G_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXMULTICASTFRAMES_G))
		stats->rxmulticastframes_g +=
			xgbe_mmc_read(pdata, MMC_RXMULTICASTFRAMES_G_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXCRCERROR))
		stats->rxcrcerror +=
			xgbe_mmc_read(pdata, MMC_RXCRCERROR_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXRUNTERROR))
		stats->rxrunterror +=
			xgbe_mmc_read(pdata, MMC_RXRUNTERROR);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXJABBERERROR))
		stats->rxjabbererror +=
			xgbe_mmc_read(pdata, MMC_RXJABBERERROR);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXUNDERSIZE_G))
		stats->rxundersize_g +=
			xgbe_mmc_read(pdata, MMC_RXUNDERSIZE_G);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXOVERSIZE_G))
		stats->rxoversize_g +=
			xgbe_mmc_read(pdata, MMC_RXOVERSIZE_G);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RX64OCTETS_GB))
		stats->rx64octets_gb +=
			xgbe_mmc_read(pdata, MMC_RX64OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RX65TO127OCTETS_GB))
		stats->rx65to127octets_gb +=
			xgbe_mmc_read(pdata, MMC_RX65TO127OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RX128TO255OCTETS_GB))
		stats->rx128to255octets_gb +=
			xgbe_mmc_read(pdata, MMC_RX128TO255OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RX256TO511OCTETS_GB))
		stats->rx256to511octets_gb +=
			xgbe_mmc_read(pdata, MMC_RX256TO511OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RX512TO1023OCTETS_GB))
		stats->rx512to1023octets_gb +=
			xgbe_mmc_read(pdata, MMC_RX512TO1023OCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RX1024TOMAXOCTETS_GB))
		stats->rx1024tomaxoctets_gb +=
			xgbe_mmc_read(pdata, MMC_RX1024TOMAXOCTETS_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXUNICASTFRAMES_G))
		stats->rxunicastframes_g +=
			xgbe_mmc_read(pdata, MMC_RXUNICASTFRAMES_G_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXLENGTHERROR))
		stats->rxlengtherror +=
			xgbe_mmc_read(pdata, MMC_RXLENGTHERROR_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXOUTOFRANGETYPE))
		stats->rxoutofrangetype +=
			xgbe_mmc_read(pdata, MMC_RXOUTOFRANGETYPE_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXPAUSEFRAMES))
		stats->rxpauseframes +=
			xgbe_mmc_read(pdata, MMC_RXPAUSEFRAMES_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXFIFOOVERFLOW))
		stats->rxfifooverflow +=
			xgbe_mmc_read(pdata, MMC_RXFIFOOVERFLOW_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXVLANFRAMES_GB))
		stats->rxvlanframes_gb +=
			xgbe_mmc_read(pdata, MMC_RXVLANFRAMES_GB_LO);

	if (XGMAC_GET_BITS(mmc_isr, MMC_RISR, RXWATCHDOGERROR))
		stats->rxwatchdogerror +=
			xgbe_mmc_read(pdata, MMC_RXWATCHDOGERROR);
}

static void xgbe_read_mmc_stats(struct xgbe_prv_data *pdata)
{
	struct xgbe_mmc_stats *stats = &pdata->mmc_stats;

	/* Freeze counters */
	XGMAC_IOWRITE_BITS(pdata, MMC_CR, MCF, 1);

	stats->txoctetcount_gb +=
		xgbe_mmc_read(pdata, MMC_TXOCTETCOUNT_GB_LO);

	stats->txframecount_gb +=
		xgbe_mmc_read(pdata, MMC_TXFRAMECOUNT_GB_LO);

	stats->txbroadcastframes_g +=
		xgbe_mmc_read(pdata, MMC_TXBROADCASTFRAMES_G_LO);

	stats->txmulticastframes_g +=
		xgbe_mmc_read(pdata, MMC_TXMULTICASTFRAMES_G_LO);

	stats->tx64octets_gb +=
		xgbe_mmc_read(pdata, MMC_TX64OCTETS_GB_LO);

	stats->tx65to127octets_gb +=
		xgbe_mmc_read(pdata, MMC_TX65TO127OCTETS_GB_LO);

	stats->tx128to255octets_gb +=
		xgbe_mmc_read(pdata, MMC_TX128TO255OCTETS_GB_LO);

	stats->tx256to511octets_gb +=
		xgbe_mmc_read(pdata, MMC_TX256TO511OCTETS_GB_LO);

	stats->tx512to1023octets_gb +=
		xgbe_mmc_read(pdata, MMC_TX512TO1023OCTETS_GB_LO);

	stats->tx1024tomaxoctets_gb +=
		xgbe_mmc_read(pdata, MMC_TX1024TOMAXOCTETS_GB_LO);

	stats->txunicastframes_gb +=
		xgbe_mmc_read(pdata, MMC_TXUNICASTFRAMES_GB_LO);

	stats->txmulticastframes_gb +=
		xgbe_mmc_read(pdata, MMC_TXMULTICASTFRAMES_GB_LO);

	stats->txbroadcastframes_g +=
		xgbe_mmc_read(pdata, MMC_TXBROADCASTFRAMES_GB_LO);

	stats->txunderflowerror +=
		xgbe_mmc_read(pdata, MMC_TXUNDERFLOWERROR_LO);

	stats->txoctetcount_g +=
		xgbe_mmc_read(pdata, MMC_TXOCTETCOUNT_G_LO);

	stats->txframecount_g +=
		xgbe_mmc_read(pdata, MMC_TXFRAMECOUNT_G_LO);

	stats->txpauseframes +=
		xgbe_mmc_read(pdata, MMC_TXPAUSEFRAMES_LO);

	stats->txvlanframes_g +=
		xgbe_mmc_read(pdata, MMC_TXVLANFRAMES_G_LO);

	stats->rxframecount_gb +=
		xgbe_mmc_read(pdata, MMC_RXFRAMECOUNT_GB_LO);

	stats->rxoctetcount_gb +=
		xgbe_mmc_read(pdata, MMC_RXOCTETCOUNT_GB_LO);

	stats->rxoctetcount_g +=
		xgbe_mmc_read(pdata, MMC_RXOCTETCOUNT_G_LO);

	stats->rxbroadcastframes_g +=
		xgbe_mmc_read(pdata, MMC_RXBROADCASTFRAMES_G_LO);

	stats->rxmulticastframes_g +=
		xgbe_mmc_read(pdata, MMC_RXMULTICASTFRAMES_G_LO);

	stats->rxcrcerror +=
		xgbe_mmc_read(pdata, MMC_RXCRCERROR_LO);

	stats->rxrunterror +=
		xgbe_mmc_read(pdata, MMC_RXRUNTERROR);

	stats->rxjabbererror +=
		xgbe_mmc_read(pdata, MMC_RXJABBERERROR);

	stats->rxundersize_g +=
		xgbe_mmc_read(pdata, MMC_RXUNDERSIZE_G);

	stats->rxoversize_g +=
		xgbe_mmc_read(pdata, MMC_RXOVERSIZE_G);

	stats->rx64octets_gb +=
		xgbe_mmc_read(pdata, MMC_RX64OCTETS_GB_LO);

	stats->rx65to127octets_gb +=
		xgbe_mmc_read(pdata, MMC_RX65TO127OCTETS_GB_LO);

	stats->rx128to255octets_gb +=
		xgbe_mmc_read(pdata, MMC_RX128TO255OCTETS_GB_LO);

	stats->rx256to511octets_gb +=
		xgbe_mmc_read(pdata, MMC_RX256TO511OCTETS_GB_LO);

	stats->rx512to1023octets_gb +=
		xgbe_mmc_read(pdata, MMC_RX512TO1023OCTETS_GB_LO);

	stats->rx1024tomaxoctets_gb +=
		xgbe_mmc_read(pdata, MMC_RX1024TOMAXOCTETS_GB_LO);

	stats->rxunicastframes_g +=
		xgbe_mmc_read(pdata, MMC_RXUNICASTFRAMES_G_LO);

	stats->rxlengtherror +=
		xgbe_mmc_read(pdata, MMC_RXLENGTHERROR_LO);

	stats->rxoutofrangetype +=
		xgbe_mmc_read(pdata, MMC_RXOUTOFRANGETYPE_LO);

	stats->rxpauseframes +=
		xgbe_mmc_read(pdata, MMC_RXPAUSEFRAMES_LO);

	stats->rxfifooverflow +=
		xgbe_mmc_read(pdata, MMC_RXFIFOOVERFLOW_LO);

	stats->rxvlanframes_gb +=
		xgbe_mmc_read(pdata, MMC_RXVLANFRAMES_GB_LO);

	stats->rxwatchdogerror +=
		xgbe_mmc_read(pdata, MMC_RXWATCHDOGERROR);

	/* Un-freeze counters */
	XGMAC_IOWRITE_BITS(pdata, MMC_CR, MCF, 0);
}

static void xgbe_config_mmc(struct xgbe_prv_data *pdata)
{
	/* Set counters to reset on read */
	XGMAC_IOWRITE_BITS(pdata, MMC_CR, ROR, 1);

	/* Reset the counters */
	XGMAC_IOWRITE_BITS(pdata, MMC_CR, CR, 1);
}

static void xgbe_prepare_tx_stop(struct xgbe_prv_data *pdata,
				 struct xgbe_channel *channel)
{
	unsigned int tx_dsr, tx_pos, tx_qidx;
	unsigned int tx_status;
	unsigned long tx_timeout;

	/* Calculate the status register to read and the position within */
	if (channel->queue_index < DMA_DSRX_FIRST_QUEUE) {
		tx_dsr = DMA_DSR0;
		tx_pos = (channel->queue_index * DMA_DSR_Q_WIDTH) +
			 DMA_DSR0_TPS_START;
	} else {
		tx_qidx = channel->queue_index - DMA_DSRX_FIRST_QUEUE;

		tx_dsr = DMA_DSR1 + ((tx_qidx / DMA_DSRX_QPR) * DMA_DSRX_INC);
		tx_pos = ((tx_qidx % DMA_DSRX_QPR) * DMA_DSR_Q_WIDTH) +
			 DMA_DSRX_TPS_START;
	}

	/* The Tx engine cannot be stopped if it is actively processing
	 * descriptors. Wait for the Tx engine to enter the stopped or
	 * suspended state.  Don't wait forever though...
	 */
	tx_timeout = jiffies + (XGBE_DMA_STOP_TIMEOUT * HZ);
	while (time_before(jiffies, tx_timeout)) {
		tx_status = XGMAC_IOREAD(pdata, tx_dsr);
		tx_status = GET_BITS(tx_status, tx_pos, DMA_DSR_TPS_WIDTH);
		if ((tx_status == DMA_TPS_STOPPED) ||
		    (tx_status == DMA_TPS_SUSPENDED))
			break;

		usleep_range(500, 1000);
	}

	if (!time_before(jiffies, tx_timeout))
		netdev_info(pdata->netdev,
			    "timed out waiting for Tx DMA channel %u to stop\n",
			    channel->queue_index);
}

static void xgbe_enable_tx(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	/* Enable each Tx DMA channel */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_TCR, ST, 1);
	}

	/* Enable each Tx queue */
	for (i = 0; i < pdata->tx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_TQOMR, TXQEN,
				       MTL_Q_ENABLED);

	/* Enable MAC Tx */
	XGMAC_IOWRITE_BITS(pdata, MAC_TCR, TE, 1);
}

static void xgbe_disable_tx(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	/* Prepare for Tx DMA channel stop */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		xgbe_prepare_tx_stop(pdata, channel);
	}

	/* Disable MAC Tx */
	XGMAC_IOWRITE_BITS(pdata, MAC_TCR, TE, 0);

	/* Disable each Tx queue */
	for (i = 0; i < pdata->tx_q_count; i++)
		XGMAC_MTL_IOWRITE_BITS(pdata, i, MTL_Q_TQOMR, TXQEN, 0);

	/* Disable each Tx DMA channel */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_TCR, ST, 0);
	}
}

static void xgbe_enable_rx(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int reg_val, i;

	/* Enable each Rx DMA channel */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->rx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_RCR, SR, 1);
	}

	/* Enable each Rx queue */
	reg_val = 0;
	for (i = 0; i < pdata->rx_q_count; i++)
		reg_val |= (0x02 << (i << 1));
	XGMAC_IOWRITE(pdata, MAC_RQC0R, reg_val);

	/* Enable MAC Rx */
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, DCRCC, 1);
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, CST, 1);
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, ACS, 1);
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, RE, 1);
}

static void xgbe_disable_rx(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	/* Disable MAC Rx */
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, DCRCC, 0);
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, CST, 0);
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, ACS, 0);
	XGMAC_IOWRITE_BITS(pdata, MAC_RCR, RE, 0);

	/* Disable each Rx queue */
	XGMAC_IOWRITE(pdata, MAC_RQC0R, 0);

	/* Disable each Rx DMA channel */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->rx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_RCR, SR, 0);
	}
}

static void xgbe_powerup_tx(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	/* Enable each Tx DMA channel */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_TCR, ST, 1);
	}

	/* Enable MAC Tx */
	XGMAC_IOWRITE_BITS(pdata, MAC_TCR, TE, 1);
}

static void xgbe_powerdown_tx(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	/* Prepare for Tx DMA channel stop */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		xgbe_prepare_tx_stop(pdata, channel);
	}

	/* Disable MAC Tx */
	XGMAC_IOWRITE_BITS(pdata, MAC_TCR, TE, 0);

	/* Disable each Tx DMA channel */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->tx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_TCR, ST, 0);
	}
}

static void xgbe_powerup_rx(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	/* Enable each Rx DMA channel */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->rx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_RCR, SR, 1);
	}
}

static void xgbe_powerdown_rx(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	/* Disable each Rx DMA channel */
	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		if (!channel->rx_ring)
			break;

		XGMAC_DMA_IOWRITE_BITS(channel, DMA_CH_RCR, SR, 0);
	}
}

static int xgbe_init(struct xgbe_prv_data *pdata)
{
	struct xgbe_desc_if *desc_if = &pdata->desc_if;
	int ret;

	DBGPR("-->xgbe_init\n");

	/* Flush Tx queues */
	ret = xgbe_flush_tx_queues(pdata);
	if (ret)
		return ret;

	/*
	 * Initialize DMA related features
	 */
	xgbe_config_dma_bus(pdata);
	xgbe_config_dma_cache(pdata);
	xgbe_config_osp_mode(pdata);
	xgbe_config_pblx8(pdata);
	xgbe_config_tx_pbl_val(pdata);
	xgbe_config_rx_pbl_val(pdata);
	xgbe_config_rx_coalesce(pdata);
	xgbe_config_tx_coalesce(pdata);
	xgbe_config_rx_buffer_size(pdata);
	xgbe_config_tso_mode(pdata);
	xgbe_config_sph_mode(pdata);
	xgbe_config_rss(pdata);
	desc_if->wrapper_tx_desc_init(pdata);
	desc_if->wrapper_rx_desc_init(pdata);
	xgbe_enable_dma_interrupts(pdata);

	/*
	 * Initialize MTL related features
	 */
	xgbe_config_mtl_mode(pdata);
	xgbe_config_queue_mapping(pdata);
	xgbe_config_tsf_mode(pdata, pdata->tx_sf_mode);
	xgbe_config_rsf_mode(pdata, pdata->rx_sf_mode);
	xgbe_config_tx_threshold(pdata, pdata->tx_threshold);
	xgbe_config_rx_threshold(pdata, pdata->rx_threshold);
	xgbe_config_tx_fifo_size(pdata);
	xgbe_config_rx_fifo_size(pdata);
	xgbe_config_flow_control_threshold(pdata);
	/*TODO: Error Packet and undersized good Packet forwarding enable
		(FEP and FUP)
	 */
	xgbe_config_dcb_tc(pdata);
	xgbe_config_dcb_pfc(pdata);
	xgbe_enable_mtl_interrupts(pdata);

	/*
	 * Initialize MAC related features
	 */
	xgbe_config_mac_address(pdata);
	xgbe_config_jumbo_enable(pdata);
	xgbe_config_flow_control(pdata);
	xgbe_config_mac_speed(pdata);
	xgbe_config_checksum_offload(pdata);
	xgbe_config_vlan_support(pdata);
	xgbe_config_mmc(pdata);
	xgbe_enable_mac_interrupts(pdata);

	DBGPR("<--xgbe_init\n");

	return 0;
}

void xgbe_init_function_ptrs_dev(struct xgbe_hw_if *hw_if)
{
	DBGPR("-->xgbe_init_function_ptrs\n");

	hw_if->tx_complete = xgbe_tx_complete;

	hw_if->set_promiscuous_mode = xgbe_set_promiscuous_mode;
	hw_if->set_all_multicast_mode = xgbe_set_all_multicast_mode;
	hw_if->add_mac_addresses = xgbe_add_mac_addresses;
	hw_if->set_mac_address = xgbe_set_mac_address;

	hw_if->enable_rx_csum = xgbe_enable_rx_csum;
	hw_if->disable_rx_csum = xgbe_disable_rx_csum;

	hw_if->enable_rx_vlan_stripping = xgbe_enable_rx_vlan_stripping;
	hw_if->disable_rx_vlan_stripping = xgbe_disable_rx_vlan_stripping;
	hw_if->enable_rx_vlan_filtering = xgbe_enable_rx_vlan_filtering;
	hw_if->disable_rx_vlan_filtering = xgbe_disable_rx_vlan_filtering;
	hw_if->update_vlan_hash_table = xgbe_update_vlan_hash_table;

	hw_if->read_mmd_regs = xgbe_read_mmd_regs;
	hw_if->write_mmd_regs = xgbe_write_mmd_regs;

	hw_if->set_gmii_speed = xgbe_set_gmii_speed;
	hw_if->set_gmii_2500_speed = xgbe_set_gmii_2500_speed;
	hw_if->set_xgmii_speed = xgbe_set_xgmii_speed;

	hw_if->enable_tx = xgbe_enable_tx;
	hw_if->disable_tx = xgbe_disable_tx;
	hw_if->enable_rx = xgbe_enable_rx;
	hw_if->disable_rx = xgbe_disable_rx;

	hw_if->powerup_tx = xgbe_powerup_tx;
	hw_if->powerdown_tx = xgbe_powerdown_tx;
	hw_if->powerup_rx = xgbe_powerup_rx;
	hw_if->powerdown_rx = xgbe_powerdown_rx;

	hw_if->dev_xmit = xgbe_dev_xmit;
	hw_if->dev_read = xgbe_dev_read;
	hw_if->enable_int = xgbe_enable_int;
	hw_if->disable_int = xgbe_disable_int;
	hw_if->init = xgbe_init;
	hw_if->exit = xgbe_exit;

	/* Descriptor related Sequences have to be initialized here */
	hw_if->tx_desc_init = xgbe_tx_desc_init;
	hw_if->rx_desc_init = xgbe_rx_desc_init;
	hw_if->tx_desc_reset = xgbe_tx_desc_reset;
	hw_if->rx_desc_reset = xgbe_rx_desc_reset;
	hw_if->is_last_desc = xgbe_is_last_desc;
	hw_if->is_context_desc = xgbe_is_context_desc;
	hw_if->tx_start_xmit = xgbe_tx_start_xmit;

	/* For FLOW ctrl */
	hw_if->config_tx_flow_control = xgbe_config_tx_flow_control;
	hw_if->config_rx_flow_control = xgbe_config_rx_flow_control;

	/* For RX coalescing */
	hw_if->config_rx_coalesce = xgbe_config_rx_coalesce;
	hw_if->config_tx_coalesce = xgbe_config_tx_coalesce;
	hw_if->usec_to_riwt = xgbe_usec_to_riwt;
	hw_if->riwt_to_usec = xgbe_riwt_to_usec;

	/* For RX and TX threshold config */
	hw_if->config_rx_threshold = xgbe_config_rx_threshold;
	hw_if->config_tx_threshold = xgbe_config_tx_threshold;

	/* For RX and TX Store and Forward Mode config */
	hw_if->config_rsf_mode = xgbe_config_rsf_mode;
	hw_if->config_tsf_mode = xgbe_config_tsf_mode;

	/* For TX DMA Operating on Second Frame config */
	hw_if->config_osp_mode = xgbe_config_osp_mode;

	/* For RX and TX PBL config */
	hw_if->config_rx_pbl_val = xgbe_config_rx_pbl_val;
	hw_if->get_rx_pbl_val = xgbe_get_rx_pbl_val;
	hw_if->config_tx_pbl_val = xgbe_config_tx_pbl_val;
	hw_if->get_tx_pbl_val = xgbe_get_tx_pbl_val;
	hw_if->config_pblx8 = xgbe_config_pblx8;

	/* For MMC statistics support */
	hw_if->tx_mmc_int = xgbe_tx_mmc_int;
	hw_if->rx_mmc_int = xgbe_rx_mmc_int;
	hw_if->read_mmc_stats = xgbe_read_mmc_stats;

	/* For PTP config */
	hw_if->config_tstamp = xgbe_config_tstamp;
	hw_if->update_tstamp_addend = xgbe_update_tstamp_addend;
	hw_if->set_tstamp_time = xgbe_set_tstamp_time;
	hw_if->get_tstamp_time = xgbe_get_tstamp_time;
	hw_if->get_tx_tstamp = xgbe_get_tx_tstamp;

	/* For Data Center Bridging config */
	hw_if->config_dcb_tc = xgbe_config_dcb_tc;
	hw_if->config_dcb_pfc = xgbe_config_dcb_pfc;

	/* For Receive Side Scaling */
	hw_if->enable_rss = xgbe_enable_rss;
	hw_if->disable_rss = xgbe_disable_rss;
	hw_if->set_rss_hash_key = xgbe_set_rss_hash_key;
	hw_if->set_rss_lookup_table = xgbe_set_rss_lookup_table;

	DBGPR("<--xgbe_init_function_ptrs\n");
}
