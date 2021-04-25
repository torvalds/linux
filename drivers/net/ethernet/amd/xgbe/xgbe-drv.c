/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
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
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
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

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/tcp.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/if_ether.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <net/vxlan.h>

#include "xgbe.h"
#include "xgbe-common.h"

static unsigned int ecc_sec_info_threshold = 10;
static unsigned int ecc_sec_warn_threshold = 10000;
static unsigned int ecc_sec_period = 600;
static unsigned int ecc_ded_threshold = 2;
static unsigned int ecc_ded_period = 600;

#ifdef CONFIG_AMD_XGBE_HAVE_ECC
/* Only expose the ECC parameters if supported */
module_param(ecc_sec_info_threshold, uint, 0644);
MODULE_PARM_DESC(ecc_sec_info_threshold,
		 " ECC corrected error informational threshold setting");

module_param(ecc_sec_warn_threshold, uint, 0644);
MODULE_PARM_DESC(ecc_sec_warn_threshold,
		 " ECC corrected error warning threshold setting");

module_param(ecc_sec_period, uint, 0644);
MODULE_PARM_DESC(ecc_sec_period, " ECC corrected error period (in seconds)");

module_param(ecc_ded_threshold, uint, 0644);
MODULE_PARM_DESC(ecc_ded_threshold, " ECC detected error threshold setting");

module_param(ecc_ded_period, uint, 0644);
MODULE_PARM_DESC(ecc_ded_period, " ECC detected error period (in seconds)");
#endif

static int xgbe_one_poll(struct napi_struct *, int);
static int xgbe_all_poll(struct napi_struct *, int);
static void xgbe_stop(struct xgbe_prv_data *);

static void *xgbe_alloc_node(size_t size, int node)
{
	void *mem;

	mem = kzalloc_node(size, GFP_KERNEL, node);
	if (!mem)
		mem = kzalloc(size, GFP_KERNEL);

	return mem;
}

static void xgbe_free_channels(struct xgbe_prv_data *pdata)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pdata->channel); i++) {
		if (!pdata->channel[i])
			continue;

		kfree(pdata->channel[i]->rx_ring);
		kfree(pdata->channel[i]->tx_ring);
		kfree(pdata->channel[i]);

		pdata->channel[i] = NULL;
	}

	pdata->channel_count = 0;
}

static int xgbe_alloc_channels(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	struct xgbe_ring *ring;
	unsigned int count, i;
	unsigned int cpu;
	int node;

	count = max_t(unsigned int, pdata->tx_ring_count, pdata->rx_ring_count);
	for (i = 0; i < count; i++) {
		/* Attempt to use a CPU on the node the device is on */
		cpu = cpumask_local_spread(i, dev_to_node(pdata->dev));

		/* Set the allocation node based on the returned CPU */
		node = cpu_to_node(cpu);

		channel = xgbe_alloc_node(sizeof(*channel), node);
		if (!channel)
			goto err_mem;
		pdata->channel[i] = channel;

		snprintf(channel->name, sizeof(channel->name), "channel-%u", i);
		channel->pdata = pdata;
		channel->queue_index = i;
		channel->dma_regs = pdata->xgmac_regs + DMA_CH_BASE +
				    (DMA_CH_INC * i);
		channel->node = node;
		cpumask_set_cpu(cpu, &channel->affinity_mask);

		if (pdata->per_channel_irq)
			channel->dma_irq = pdata->channel_irq[i];

		if (i < pdata->tx_ring_count) {
			ring = xgbe_alloc_node(sizeof(*ring), node);
			if (!ring)
				goto err_mem;

			spin_lock_init(&ring->lock);
			ring->node = node;

			channel->tx_ring = ring;
		}

		if (i < pdata->rx_ring_count) {
			ring = xgbe_alloc_node(sizeof(*ring), node);
			if (!ring)
				goto err_mem;

			spin_lock_init(&ring->lock);
			ring->node = node;

			channel->rx_ring = ring;
		}

		netif_dbg(pdata, drv, pdata->netdev,
			  "%s: cpu=%u, node=%d\n", channel->name, cpu, node);

		netif_dbg(pdata, drv, pdata->netdev,
			  "%s: dma_regs=%p, dma_irq=%d, tx=%p, rx=%p\n",
			  channel->name, channel->dma_regs, channel->dma_irq,
			  channel->tx_ring, channel->rx_ring);
	}

	pdata->channel_count = count;

	return 0;

err_mem:
	xgbe_free_channels(pdata);

	return -ENOMEM;
}

static inline unsigned int xgbe_tx_avail_desc(struct xgbe_ring *ring)
{
	return (ring->rdesc_count - (ring->cur - ring->dirty));
}

static inline unsigned int xgbe_rx_dirty_desc(struct xgbe_ring *ring)
{
	return (ring->cur - ring->dirty);
}

static int xgbe_maybe_stop_tx_queue(struct xgbe_channel *channel,
				    struct xgbe_ring *ring, unsigned int count)
{
	struct xgbe_prv_data *pdata = channel->pdata;

	if (count > xgbe_tx_avail_desc(ring)) {
		netif_info(pdata, drv, pdata->netdev,
			   "Tx queue stopped, not enough descriptors available\n");
		netif_stop_subqueue(pdata->netdev, channel->queue_index);
		ring->tx.queue_stopped = 1;

		/* If we haven't notified the hardware because of xmit_more
		 * support, tell it now
		 */
		if (ring->tx.xmit_more)
			pdata->hw_if.tx_start_xmit(channel, ring);

		return NETDEV_TX_BUSY;
	}

	return 0;
}

static int xgbe_calc_rx_buf_size(struct net_device *netdev, unsigned int mtu)
{
	unsigned int rx_buf_size;

	rx_buf_size = mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	rx_buf_size = clamp_val(rx_buf_size, XGBE_RX_MIN_BUF_SIZE, PAGE_SIZE);

	rx_buf_size = (rx_buf_size + XGBE_RX_BUF_ALIGN - 1) &
		      ~(XGBE_RX_BUF_ALIGN - 1);

	return rx_buf_size;
}

static void xgbe_enable_rx_tx_int(struct xgbe_prv_data *pdata,
				  struct xgbe_channel *channel)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	enum xgbe_int int_id;

	if (channel->tx_ring && channel->rx_ring)
		int_id = XGMAC_INT_DMA_CH_SR_TI_RI;
	else if (channel->tx_ring)
		int_id = XGMAC_INT_DMA_CH_SR_TI;
	else if (channel->rx_ring)
		int_id = XGMAC_INT_DMA_CH_SR_RI;
	else
		return;

	hw_if->enable_int(channel, int_id);
}

static void xgbe_enable_rx_tx_ints(struct xgbe_prv_data *pdata)
{
	unsigned int i;

	for (i = 0; i < pdata->channel_count; i++)
		xgbe_enable_rx_tx_int(pdata, pdata->channel[i]);
}

static void xgbe_disable_rx_tx_int(struct xgbe_prv_data *pdata,
				   struct xgbe_channel *channel)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	enum xgbe_int int_id;

	if (channel->tx_ring && channel->rx_ring)
		int_id = XGMAC_INT_DMA_CH_SR_TI_RI;
	else if (channel->tx_ring)
		int_id = XGMAC_INT_DMA_CH_SR_TI;
	else if (channel->rx_ring)
		int_id = XGMAC_INT_DMA_CH_SR_RI;
	else
		return;

	hw_if->disable_int(channel, int_id);
}

static void xgbe_disable_rx_tx_ints(struct xgbe_prv_data *pdata)
{
	unsigned int i;

	for (i = 0; i < pdata->channel_count; i++)
		xgbe_disable_rx_tx_int(pdata, pdata->channel[i]);
}

static bool xgbe_ecc_sec(struct xgbe_prv_data *pdata, unsigned long *period,
			 unsigned int *count, const char *area)
{
	if (time_before(jiffies, *period)) {
		(*count)++;
	} else {
		*period = jiffies + (ecc_sec_period * HZ);
		*count = 1;
	}

	if (*count > ecc_sec_info_threshold)
		dev_warn_once(pdata->dev,
			      "%s ECC corrected errors exceed informational threshold\n",
			      area);

	if (*count > ecc_sec_warn_threshold) {
		dev_warn_once(pdata->dev,
			      "%s ECC corrected errors exceed warning threshold\n",
			      area);
		return true;
	}

	return false;
}

static bool xgbe_ecc_ded(struct xgbe_prv_data *pdata, unsigned long *period,
			 unsigned int *count, const char *area)
{
	if (time_before(jiffies, *period)) {
		(*count)++;
	} else {
		*period = jiffies + (ecc_ded_period * HZ);
		*count = 1;
	}

	if (*count > ecc_ded_threshold) {
		netdev_alert(pdata->netdev,
			     "%s ECC detected errors exceed threshold\n",
			     area);
		return true;
	}

	return false;
}

static void xgbe_ecc_isr_task(struct tasklet_struct *t)
{
	struct xgbe_prv_data *pdata = from_tasklet(pdata, t, tasklet_ecc);
	unsigned int ecc_isr;
	bool stop = false;

	/* Mask status with only the interrupts we care about */
	ecc_isr = XP_IOREAD(pdata, XP_ECC_ISR);
	ecc_isr &= XP_IOREAD(pdata, XP_ECC_IER);
	netif_dbg(pdata, intr, pdata->netdev, "ECC_ISR=%#010x\n", ecc_isr);

	if (XP_GET_BITS(ecc_isr, XP_ECC_ISR, TX_DED)) {
		stop |= xgbe_ecc_ded(pdata, &pdata->tx_ded_period,
				     &pdata->tx_ded_count, "TX fifo");
	}

	if (XP_GET_BITS(ecc_isr, XP_ECC_ISR, RX_DED)) {
		stop |= xgbe_ecc_ded(pdata, &pdata->rx_ded_period,
				     &pdata->rx_ded_count, "RX fifo");
	}

	if (XP_GET_BITS(ecc_isr, XP_ECC_ISR, DESC_DED)) {
		stop |= xgbe_ecc_ded(pdata, &pdata->desc_ded_period,
				     &pdata->desc_ded_count,
				     "descriptor cache");
	}

	if (stop) {
		pdata->hw_if.disable_ecc_ded(pdata);
		schedule_work(&pdata->stopdev_work);
		goto out;
	}

	if (XP_GET_BITS(ecc_isr, XP_ECC_ISR, TX_SEC)) {
		if (xgbe_ecc_sec(pdata, &pdata->tx_sec_period,
				 &pdata->tx_sec_count, "TX fifo"))
			pdata->hw_if.disable_ecc_sec(pdata, XGBE_ECC_SEC_TX);
	}

	if (XP_GET_BITS(ecc_isr, XP_ECC_ISR, RX_SEC))
		if (xgbe_ecc_sec(pdata, &pdata->rx_sec_period,
				 &pdata->rx_sec_count, "RX fifo"))
			pdata->hw_if.disable_ecc_sec(pdata, XGBE_ECC_SEC_RX);

	if (XP_GET_BITS(ecc_isr, XP_ECC_ISR, DESC_SEC))
		if (xgbe_ecc_sec(pdata, &pdata->desc_sec_period,
				 &pdata->desc_sec_count, "descriptor cache"))
			pdata->hw_if.disable_ecc_sec(pdata, XGBE_ECC_SEC_DESC);

out:
	/* Clear all ECC interrupts */
	XP_IOWRITE(pdata, XP_ECC_ISR, ecc_isr);

	/* Reissue interrupt if status is not clear */
	if (pdata->vdata->irq_reissue_support)
		XP_IOWRITE(pdata, XP_INT_REISSUE_EN, 1 << 1);
}

static irqreturn_t xgbe_ecc_isr(int irq, void *data)
{
	struct xgbe_prv_data *pdata = data;

	if (pdata->isr_as_tasklet)
		tasklet_schedule(&pdata->tasklet_ecc);
	else
		xgbe_ecc_isr_task(&pdata->tasklet_ecc);

	return IRQ_HANDLED;
}

static void xgbe_isr_task(struct tasklet_struct *t)
{
	struct xgbe_prv_data *pdata = from_tasklet(pdata, t, tasklet_dev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_channel *channel;
	unsigned int dma_isr, dma_ch_isr;
	unsigned int mac_isr, mac_tssr, mac_mdioisr;
	unsigned int i;

	/* The DMA interrupt status register also reports MAC and MTL
	 * interrupts. So for polling mode, we just need to check for
	 * this register to be non-zero
	 */
	dma_isr = XGMAC_IOREAD(pdata, DMA_ISR);
	if (!dma_isr)
		goto isr_done;

	netif_dbg(pdata, intr, pdata->netdev, "DMA_ISR=%#010x\n", dma_isr);

	for (i = 0; i < pdata->channel_count; i++) {
		if (!(dma_isr & (1 << i)))
			continue;

		channel = pdata->channel[i];

		dma_ch_isr = XGMAC_DMA_IOREAD(channel, DMA_CH_SR);
		netif_dbg(pdata, intr, pdata->netdev, "DMA_CH%u_ISR=%#010x\n",
			  i, dma_ch_isr);

		/* The TI or RI interrupt bits may still be set even if using
		 * per channel DMA interrupts. Check to be sure those are not
		 * enabled before using the private data napi structure.
		 */
		if (!pdata->per_channel_irq &&
		    (XGMAC_GET_BITS(dma_ch_isr, DMA_CH_SR, TI) ||
		     XGMAC_GET_BITS(dma_ch_isr, DMA_CH_SR, RI))) {
			if (napi_schedule_prep(&pdata->napi)) {
				/* Disable Tx and Rx interrupts */
				xgbe_disable_rx_tx_ints(pdata);

				/* Turn on polling */
				__napi_schedule(&pdata->napi);
			}
		} else {
			/* Don't clear Rx/Tx status if doing per channel DMA
			 * interrupts, these will be cleared by the ISR for
			 * per channel DMA interrupts.
			 */
			XGMAC_SET_BITS(dma_ch_isr, DMA_CH_SR, TI, 0);
			XGMAC_SET_BITS(dma_ch_isr, DMA_CH_SR, RI, 0);
		}

		if (XGMAC_GET_BITS(dma_ch_isr, DMA_CH_SR, RBU))
			pdata->ext_stats.rx_buffer_unavailable++;

		/* Restart the device on a Fatal Bus Error */
		if (XGMAC_GET_BITS(dma_ch_isr, DMA_CH_SR, FBE))
			schedule_work(&pdata->restart_work);

		/* Clear interrupt signals */
		XGMAC_DMA_IOWRITE(channel, DMA_CH_SR, dma_ch_isr);
	}

	if (XGMAC_GET_BITS(dma_isr, DMA_ISR, MACIS)) {
		mac_isr = XGMAC_IOREAD(pdata, MAC_ISR);

		netif_dbg(pdata, intr, pdata->netdev, "MAC_ISR=%#010x\n",
			  mac_isr);

		if (XGMAC_GET_BITS(mac_isr, MAC_ISR, MMCTXIS))
			hw_if->tx_mmc_int(pdata);

		if (XGMAC_GET_BITS(mac_isr, MAC_ISR, MMCRXIS))
			hw_if->rx_mmc_int(pdata);

		if (XGMAC_GET_BITS(mac_isr, MAC_ISR, TSIS)) {
			mac_tssr = XGMAC_IOREAD(pdata, MAC_TSSR);

			netif_dbg(pdata, intr, pdata->netdev,
				  "MAC_TSSR=%#010x\n", mac_tssr);

			if (XGMAC_GET_BITS(mac_tssr, MAC_TSSR, TXTSC)) {
				/* Read Tx Timestamp to clear interrupt */
				pdata->tx_tstamp =
					hw_if->get_tx_tstamp(pdata);
				queue_work(pdata->dev_workqueue,
					   &pdata->tx_tstamp_work);
			}
		}

		if (XGMAC_GET_BITS(mac_isr, MAC_ISR, SMI)) {
			mac_mdioisr = XGMAC_IOREAD(pdata, MAC_MDIOISR);

			netif_dbg(pdata, intr, pdata->netdev,
				  "MAC_MDIOISR=%#010x\n", mac_mdioisr);

			if (XGMAC_GET_BITS(mac_mdioisr, MAC_MDIOISR,
					   SNGLCOMPINT))
				complete(&pdata->mdio_complete);
		}
	}

isr_done:
	/* If there is not a separate AN irq, handle it here */
	if (pdata->dev_irq == pdata->an_irq)
		pdata->phy_if.an_isr(pdata);

	/* If there is not a separate ECC irq, handle it here */
	if (pdata->vdata->ecc_support && (pdata->dev_irq == pdata->ecc_irq))
		xgbe_ecc_isr_task(&pdata->tasklet_ecc);

	/* If there is not a separate I2C irq, handle it here */
	if (pdata->vdata->i2c_support && (pdata->dev_irq == pdata->i2c_irq))
		pdata->i2c_if.i2c_isr(pdata);

	/* Reissue interrupt if status is not clear */
	if (pdata->vdata->irq_reissue_support) {
		unsigned int reissue_mask;

		reissue_mask = 1 << 0;
		if (!pdata->per_channel_irq)
			reissue_mask |= 0xffff << 4;

		XP_IOWRITE(pdata, XP_INT_REISSUE_EN, reissue_mask);
	}
}

static irqreturn_t xgbe_isr(int irq, void *data)
{
	struct xgbe_prv_data *pdata = data;

	if (pdata->isr_as_tasklet)
		tasklet_schedule(&pdata->tasklet_dev);
	else
		xgbe_isr_task(&pdata->tasklet_dev);

	return IRQ_HANDLED;
}

static irqreturn_t xgbe_dma_isr(int irq, void *data)
{
	struct xgbe_channel *channel = data;
	struct xgbe_prv_data *pdata = channel->pdata;
	unsigned int dma_status;

	/* Per channel DMA interrupts are enabled, so we use the per
	 * channel napi structure and not the private data napi structure
	 */
	if (napi_schedule_prep(&channel->napi)) {
		/* Disable Tx and Rx interrupts */
		if (pdata->channel_irq_mode)
			xgbe_disable_rx_tx_int(pdata, channel);
		else
			disable_irq_nosync(channel->dma_irq);

		/* Turn on polling */
		__napi_schedule_irqoff(&channel->napi);
	}

	/* Clear Tx/Rx signals */
	dma_status = 0;
	XGMAC_SET_BITS(dma_status, DMA_CH_SR, TI, 1);
	XGMAC_SET_BITS(dma_status, DMA_CH_SR, RI, 1);
	XGMAC_DMA_IOWRITE(channel, DMA_CH_SR, dma_status);

	return IRQ_HANDLED;
}

static void xgbe_tx_timer(struct timer_list *t)
{
	struct xgbe_channel *channel = from_timer(channel, t, tx_timer);
	struct xgbe_prv_data *pdata = channel->pdata;
	struct napi_struct *napi;

	DBGPR("-->xgbe_tx_timer\n");

	napi = (pdata->per_channel_irq) ? &channel->napi : &pdata->napi;

	if (napi_schedule_prep(napi)) {
		/* Disable Tx and Rx interrupts */
		if (pdata->per_channel_irq)
			if (pdata->channel_irq_mode)
				xgbe_disable_rx_tx_int(pdata, channel);
			else
				disable_irq_nosync(channel->dma_irq);
		else
			xgbe_disable_rx_tx_ints(pdata);

		/* Turn on polling */
		__napi_schedule(napi);
	}

	channel->tx_timer_active = 0;

	DBGPR("<--xgbe_tx_timer\n");
}

static void xgbe_service(struct work_struct *work)
{
	struct xgbe_prv_data *pdata = container_of(work,
						   struct xgbe_prv_data,
						   service_work);

	pdata->phy_if.phy_status(pdata);
}

static void xgbe_service_timer(struct timer_list *t)
{
	struct xgbe_prv_data *pdata = from_timer(pdata, t, service_timer);

	queue_work(pdata->dev_workqueue, &pdata->service_work);

	mod_timer(&pdata->service_timer, jiffies + HZ);
}

static void xgbe_init_timers(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	timer_setup(&pdata->service_timer, xgbe_service_timer, 0);

	for (i = 0; i < pdata->channel_count; i++) {
		channel = pdata->channel[i];
		if (!channel->tx_ring)
			break;

		timer_setup(&channel->tx_timer, xgbe_tx_timer, 0);
	}
}

static void xgbe_start_timers(struct xgbe_prv_data *pdata)
{
	mod_timer(&pdata->service_timer, jiffies + HZ);
}

static void xgbe_stop_timers(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	del_timer_sync(&pdata->service_timer);

	for (i = 0; i < pdata->channel_count; i++) {
		channel = pdata->channel[i];
		if (!channel->tx_ring)
			break;

		del_timer_sync(&channel->tx_timer);
	}
}

void xgbe_get_all_hw_features(struct xgbe_prv_data *pdata)
{
	unsigned int mac_hfr0, mac_hfr1, mac_hfr2;
	struct xgbe_hw_features *hw_feat = &pdata->hw_feat;

	mac_hfr0 = XGMAC_IOREAD(pdata, MAC_HWF0R);
	mac_hfr1 = XGMAC_IOREAD(pdata, MAC_HWF1R);
	mac_hfr2 = XGMAC_IOREAD(pdata, MAC_HWF2R);

	memset(hw_feat, 0, sizeof(*hw_feat));

	hw_feat->version = XGMAC_IOREAD(pdata, MAC_VR);

	/* Hardware feature register 0 */
	hw_feat->gmii        = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, GMIISEL);
	hw_feat->vlhash      = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, VLHASH);
	hw_feat->sma         = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, SMASEL);
	hw_feat->rwk         = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, RWKSEL);
	hw_feat->mgk         = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, MGKSEL);
	hw_feat->mmc         = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, MMCSEL);
	hw_feat->aoe         = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, ARPOFFSEL);
	hw_feat->ts          = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, TSSEL);
	hw_feat->eee         = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, EEESEL);
	hw_feat->tx_coe      = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, TXCOESEL);
	hw_feat->rx_coe      = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, RXCOESEL);
	hw_feat->addn_mac    = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R,
					      ADDMACADRSEL);
	hw_feat->ts_src      = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, TSSTSSEL);
	hw_feat->sa_vlan_ins = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, SAVLANINS);
	hw_feat->vxn         = XGMAC_GET_BITS(mac_hfr0, MAC_HWF0R, VXN);

	/* Hardware feature register 1 */
	hw_feat->rx_fifo_size  = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R,
						RXFIFOSIZE);
	hw_feat->tx_fifo_size  = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R,
						TXFIFOSIZE);
	hw_feat->adv_ts_hi     = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R, ADVTHWORD);
	hw_feat->dma_width     = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R, ADDR64);
	hw_feat->dcb           = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R, DCBEN);
	hw_feat->sph           = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R, SPHEN);
	hw_feat->tso           = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R, TSOEN);
	hw_feat->dma_debug     = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R, DBGMEMA);
	hw_feat->rss           = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R, RSSEN);
	hw_feat->tc_cnt	       = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R, NUMTC);
	hw_feat->hash_table_size = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R,
						  HASHTBLSZ);
	hw_feat->l3l4_filter_num = XGMAC_GET_BITS(mac_hfr1, MAC_HWF1R,
						  L3L4FNUM);

	/* Hardware feature register 2 */
	hw_feat->rx_q_cnt     = XGMAC_GET_BITS(mac_hfr2, MAC_HWF2R, RXQCNT);
	hw_feat->tx_q_cnt     = XGMAC_GET_BITS(mac_hfr2, MAC_HWF2R, TXQCNT);
	hw_feat->rx_ch_cnt    = XGMAC_GET_BITS(mac_hfr2, MAC_HWF2R, RXCHCNT);
	hw_feat->tx_ch_cnt    = XGMAC_GET_BITS(mac_hfr2, MAC_HWF2R, TXCHCNT);
	hw_feat->pps_out_num  = XGMAC_GET_BITS(mac_hfr2, MAC_HWF2R, PPSOUTNUM);
	hw_feat->aux_snap_num = XGMAC_GET_BITS(mac_hfr2, MAC_HWF2R, AUXSNAPNUM);

	/* Translate the Hash Table size into actual number */
	switch (hw_feat->hash_table_size) {
	case 0:
		break;
	case 1:
		hw_feat->hash_table_size = 64;
		break;
	case 2:
		hw_feat->hash_table_size = 128;
		break;
	case 3:
		hw_feat->hash_table_size = 256;
		break;
	}

	/* Translate the address width setting into actual number */
	switch (hw_feat->dma_width) {
	case 0:
		hw_feat->dma_width = 32;
		break;
	case 1:
		hw_feat->dma_width = 40;
		break;
	case 2:
		hw_feat->dma_width = 48;
		break;
	default:
		hw_feat->dma_width = 32;
	}

	/* The Queue, Channel and TC counts are zero based so increment them
	 * to get the actual number
	 */
	hw_feat->rx_q_cnt++;
	hw_feat->tx_q_cnt++;
	hw_feat->rx_ch_cnt++;
	hw_feat->tx_ch_cnt++;
	hw_feat->tc_cnt++;

	/* Translate the fifo sizes into actual numbers */
	hw_feat->rx_fifo_size = 1 << (hw_feat->rx_fifo_size + 7);
	hw_feat->tx_fifo_size = 1 << (hw_feat->tx_fifo_size + 7);

	if (netif_msg_probe(pdata)) {
		dev_dbg(pdata->dev, "Hardware features:\n");

		/* Hardware feature register 0 */
		dev_dbg(pdata->dev, "  1GbE support              : %s\n",
			hw_feat->gmii ? "yes" : "no");
		dev_dbg(pdata->dev, "  VLAN hash filter          : %s\n",
			hw_feat->vlhash ? "yes" : "no");
		dev_dbg(pdata->dev, "  MDIO interface            : %s\n",
			hw_feat->sma ? "yes" : "no");
		dev_dbg(pdata->dev, "  Wake-up packet support    : %s\n",
			hw_feat->rwk ? "yes" : "no");
		dev_dbg(pdata->dev, "  Magic packet support      : %s\n",
			hw_feat->mgk ? "yes" : "no");
		dev_dbg(pdata->dev, "  Management counters       : %s\n",
			hw_feat->mmc ? "yes" : "no");
		dev_dbg(pdata->dev, "  ARP offload               : %s\n",
			hw_feat->aoe ? "yes" : "no");
		dev_dbg(pdata->dev, "  IEEE 1588-2008 Timestamp  : %s\n",
			hw_feat->ts ? "yes" : "no");
		dev_dbg(pdata->dev, "  Energy Efficient Ethernet : %s\n",
			hw_feat->eee ? "yes" : "no");
		dev_dbg(pdata->dev, "  TX checksum offload       : %s\n",
			hw_feat->tx_coe ? "yes" : "no");
		dev_dbg(pdata->dev, "  RX checksum offload       : %s\n",
			hw_feat->rx_coe ? "yes" : "no");
		dev_dbg(pdata->dev, "  Additional MAC addresses  : %u\n",
			hw_feat->addn_mac);
		dev_dbg(pdata->dev, "  Timestamp source          : %s\n",
			(hw_feat->ts_src == 1) ? "internal" :
			(hw_feat->ts_src == 2) ? "external" :
			(hw_feat->ts_src == 3) ? "internal/external" : "n/a");
		dev_dbg(pdata->dev, "  SA/VLAN insertion         : %s\n",
			hw_feat->sa_vlan_ins ? "yes" : "no");
		dev_dbg(pdata->dev, "  VXLAN/NVGRE support       : %s\n",
			hw_feat->vxn ? "yes" : "no");

		/* Hardware feature register 1 */
		dev_dbg(pdata->dev, "  RX fifo size              : %u\n",
			hw_feat->rx_fifo_size);
		dev_dbg(pdata->dev, "  TX fifo size              : %u\n",
			hw_feat->tx_fifo_size);
		dev_dbg(pdata->dev, "  IEEE 1588 high word       : %s\n",
			hw_feat->adv_ts_hi ? "yes" : "no");
		dev_dbg(pdata->dev, "  DMA width                 : %u\n",
			hw_feat->dma_width);
		dev_dbg(pdata->dev, "  Data Center Bridging      : %s\n",
			hw_feat->dcb ? "yes" : "no");
		dev_dbg(pdata->dev, "  Split header              : %s\n",
			hw_feat->sph ? "yes" : "no");
		dev_dbg(pdata->dev, "  TCP Segmentation Offload  : %s\n",
			hw_feat->tso ? "yes" : "no");
		dev_dbg(pdata->dev, "  Debug memory interface    : %s\n",
			hw_feat->dma_debug ? "yes" : "no");
		dev_dbg(pdata->dev, "  Receive Side Scaling      : %s\n",
			hw_feat->rss ? "yes" : "no");
		dev_dbg(pdata->dev, "  Traffic Class count       : %u\n",
			hw_feat->tc_cnt);
		dev_dbg(pdata->dev, "  Hash table size           : %u\n",
			hw_feat->hash_table_size);
		dev_dbg(pdata->dev, "  L3/L4 Filters             : %u\n",
			hw_feat->l3l4_filter_num);

		/* Hardware feature register 2 */
		dev_dbg(pdata->dev, "  RX queue count            : %u\n",
			hw_feat->rx_q_cnt);
		dev_dbg(pdata->dev, "  TX queue count            : %u\n",
			hw_feat->tx_q_cnt);
		dev_dbg(pdata->dev, "  RX DMA channel count      : %u\n",
			hw_feat->rx_ch_cnt);
		dev_dbg(pdata->dev, "  TX DMA channel count      : %u\n",
			hw_feat->rx_ch_cnt);
		dev_dbg(pdata->dev, "  PPS outputs               : %u\n",
			hw_feat->pps_out_num);
		dev_dbg(pdata->dev, "  Auxiliary snapshot inputs : %u\n",
			hw_feat->aux_snap_num);
	}
}

static int xgbe_vxlan_set_port(struct net_device *netdev, unsigned int table,
			       unsigned int entry, struct udp_tunnel_info *ti)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	pdata->vxlan_port = be16_to_cpu(ti->port);
	pdata->hw_if.enable_vxlan(pdata);

	return 0;
}

static int xgbe_vxlan_unset_port(struct net_device *netdev, unsigned int table,
				 unsigned int entry, struct udp_tunnel_info *ti)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	pdata->hw_if.disable_vxlan(pdata);
	pdata->vxlan_port = 0;

	return 0;
}

static const struct udp_tunnel_nic_info xgbe_udp_tunnels = {
	.set_port	= xgbe_vxlan_set_port,
	.unset_port	= xgbe_vxlan_unset_port,
	.flags		= UDP_TUNNEL_NIC_INFO_OPEN_ONLY,
	.tables		= {
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN, },
	},
};

const struct udp_tunnel_nic_info *xgbe_get_udp_tunnel_info(void)
{
	return &xgbe_udp_tunnels;
}

static void xgbe_napi_enable(struct xgbe_prv_data *pdata, unsigned int add)
{
	struct xgbe_channel *channel;
	unsigned int i;

	if (pdata->per_channel_irq) {
		for (i = 0; i < pdata->channel_count; i++) {
			channel = pdata->channel[i];
			if (add)
				netif_napi_add(pdata->netdev, &channel->napi,
					       xgbe_one_poll, NAPI_POLL_WEIGHT);

			napi_enable(&channel->napi);
		}
	} else {
		if (add)
			netif_napi_add(pdata->netdev, &pdata->napi,
				       xgbe_all_poll, NAPI_POLL_WEIGHT);

		napi_enable(&pdata->napi);
	}
}

static void xgbe_napi_disable(struct xgbe_prv_data *pdata, unsigned int del)
{
	struct xgbe_channel *channel;
	unsigned int i;

	if (pdata->per_channel_irq) {
		for (i = 0; i < pdata->channel_count; i++) {
			channel = pdata->channel[i];
			napi_disable(&channel->napi);

			if (del)
				netif_napi_del(&channel->napi);
		}
	} else {
		napi_disable(&pdata->napi);

		if (del)
			netif_napi_del(&pdata->napi);
	}
}

static int xgbe_request_irqs(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	struct net_device *netdev = pdata->netdev;
	unsigned int i;
	int ret;

	tasklet_setup(&pdata->tasklet_dev, xgbe_isr_task);
	tasklet_setup(&pdata->tasklet_ecc, xgbe_ecc_isr_task);

	ret = devm_request_irq(pdata->dev, pdata->dev_irq, xgbe_isr, 0,
			       netdev_name(netdev), pdata);
	if (ret) {
		netdev_alert(netdev, "error requesting irq %d\n",
			     pdata->dev_irq);
		return ret;
	}

	if (pdata->vdata->ecc_support && (pdata->dev_irq != pdata->ecc_irq)) {
		ret = devm_request_irq(pdata->dev, pdata->ecc_irq, xgbe_ecc_isr,
				       0, pdata->ecc_name, pdata);
		if (ret) {
			netdev_alert(netdev, "error requesting ecc irq %d\n",
				     pdata->ecc_irq);
			goto err_dev_irq;
		}
	}

	if (!pdata->per_channel_irq)
		return 0;

	for (i = 0; i < pdata->channel_count; i++) {
		channel = pdata->channel[i];
		snprintf(channel->dma_irq_name,
			 sizeof(channel->dma_irq_name) - 1,
			 "%s-TxRx-%u", netdev_name(netdev),
			 channel->queue_index);

		ret = devm_request_irq(pdata->dev, channel->dma_irq,
				       xgbe_dma_isr, 0,
				       channel->dma_irq_name, channel);
		if (ret) {
			netdev_alert(netdev, "error requesting irq %d\n",
				     channel->dma_irq);
			goto err_dma_irq;
		}

		irq_set_affinity_hint(channel->dma_irq,
				      &channel->affinity_mask);
	}

	return 0;

err_dma_irq:
	/* Using an unsigned int, 'i' will go to UINT_MAX and exit */
	for (i--; i < pdata->channel_count; i--) {
		channel = pdata->channel[i];

		irq_set_affinity_hint(channel->dma_irq, NULL);
		devm_free_irq(pdata->dev, channel->dma_irq, channel);
	}

	if (pdata->vdata->ecc_support && (pdata->dev_irq != pdata->ecc_irq))
		devm_free_irq(pdata->dev, pdata->ecc_irq, pdata);

err_dev_irq:
	devm_free_irq(pdata->dev, pdata->dev_irq, pdata);

	return ret;
}

static void xgbe_free_irqs(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	devm_free_irq(pdata->dev, pdata->dev_irq, pdata);

	if (pdata->vdata->ecc_support && (pdata->dev_irq != pdata->ecc_irq))
		devm_free_irq(pdata->dev, pdata->ecc_irq, pdata);

	if (!pdata->per_channel_irq)
		return;

	for (i = 0; i < pdata->channel_count; i++) {
		channel = pdata->channel[i];

		irq_set_affinity_hint(channel->dma_irq, NULL);
		devm_free_irq(pdata->dev, channel->dma_irq, channel);
	}
}

void xgbe_init_tx_coalesce(struct xgbe_prv_data *pdata)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;

	DBGPR("-->xgbe_init_tx_coalesce\n");

	pdata->tx_usecs = XGMAC_INIT_DMA_TX_USECS;
	pdata->tx_frames = XGMAC_INIT_DMA_TX_FRAMES;

	hw_if->config_tx_coalesce(pdata);

	DBGPR("<--xgbe_init_tx_coalesce\n");
}

void xgbe_init_rx_coalesce(struct xgbe_prv_data *pdata)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;

	DBGPR("-->xgbe_init_rx_coalesce\n");

	pdata->rx_riwt = hw_if->usec_to_riwt(pdata, XGMAC_INIT_DMA_RX_USECS);
	pdata->rx_usecs = XGMAC_INIT_DMA_RX_USECS;
	pdata->rx_frames = XGMAC_INIT_DMA_RX_FRAMES;

	hw_if->config_rx_coalesce(pdata);

	DBGPR("<--xgbe_init_rx_coalesce\n");
}

static void xgbe_free_tx_data(struct xgbe_prv_data *pdata)
{
	struct xgbe_desc_if *desc_if = &pdata->desc_if;
	struct xgbe_ring *ring;
	struct xgbe_ring_data *rdata;
	unsigned int i, j;

	DBGPR("-->xgbe_free_tx_data\n");

	for (i = 0; i < pdata->channel_count; i++) {
		ring = pdata->channel[i]->tx_ring;
		if (!ring)
			break;

		for (j = 0; j < ring->rdesc_count; j++) {
			rdata = XGBE_GET_DESC_DATA(ring, j);
			desc_if->unmap_rdata(pdata, rdata);
		}
	}

	DBGPR("<--xgbe_free_tx_data\n");
}

static void xgbe_free_rx_data(struct xgbe_prv_data *pdata)
{
	struct xgbe_desc_if *desc_if = &pdata->desc_if;
	struct xgbe_ring *ring;
	struct xgbe_ring_data *rdata;
	unsigned int i, j;

	DBGPR("-->xgbe_free_rx_data\n");

	for (i = 0; i < pdata->channel_count; i++) {
		ring = pdata->channel[i]->rx_ring;
		if (!ring)
			break;

		for (j = 0; j < ring->rdesc_count; j++) {
			rdata = XGBE_GET_DESC_DATA(ring, j);
			desc_if->unmap_rdata(pdata, rdata);
		}
	}

	DBGPR("<--xgbe_free_rx_data\n");
}

static int xgbe_phy_reset(struct xgbe_prv_data *pdata)
{
	pdata->phy_link = -1;
	pdata->phy_speed = SPEED_UNKNOWN;

	return pdata->phy_if.phy_reset(pdata);
}

int xgbe_powerdown(struct net_device *netdev, unsigned int caller)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	unsigned long flags;

	DBGPR("-->xgbe_powerdown\n");

	if (!netif_running(netdev) ||
	    (caller == XGMAC_IOCTL_CONTEXT && pdata->power_down)) {
		netdev_alert(netdev, "Device is already powered down\n");
		DBGPR("<--xgbe_powerdown\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&pdata->lock, flags);

	if (caller == XGMAC_DRIVER_CONTEXT)
		netif_device_detach(netdev);

	netif_tx_stop_all_queues(netdev);

	xgbe_stop_timers(pdata);
	flush_workqueue(pdata->dev_workqueue);

	hw_if->powerdown_tx(pdata);
	hw_if->powerdown_rx(pdata);

	xgbe_napi_disable(pdata, 0);

	pdata->power_down = 1;

	spin_unlock_irqrestore(&pdata->lock, flags);

	DBGPR("<--xgbe_powerdown\n");

	return 0;
}

int xgbe_powerup(struct net_device *netdev, unsigned int caller)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	unsigned long flags;

	DBGPR("-->xgbe_powerup\n");

	if (!netif_running(netdev) ||
	    (caller == XGMAC_IOCTL_CONTEXT && !pdata->power_down)) {
		netdev_alert(netdev, "Device is already powered up\n");
		DBGPR("<--xgbe_powerup\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&pdata->lock, flags);

	pdata->power_down = 0;

	xgbe_napi_enable(pdata, 0);

	hw_if->powerup_tx(pdata);
	hw_if->powerup_rx(pdata);

	if (caller == XGMAC_DRIVER_CONTEXT)
		netif_device_attach(netdev);

	netif_tx_start_all_queues(netdev);

	xgbe_start_timers(pdata);

	spin_unlock_irqrestore(&pdata->lock, flags);

	DBGPR("<--xgbe_powerup\n");

	return 0;
}

static void xgbe_free_memory(struct xgbe_prv_data *pdata)
{
	struct xgbe_desc_if *desc_if = &pdata->desc_if;

	/* Free the ring descriptors and buffers */
	desc_if->free_ring_resources(pdata);

	/* Free the channel and ring structures */
	xgbe_free_channels(pdata);
}

static int xgbe_alloc_memory(struct xgbe_prv_data *pdata)
{
	struct xgbe_desc_if *desc_if = &pdata->desc_if;
	struct net_device *netdev = pdata->netdev;
	int ret;

	if (pdata->new_tx_ring_count) {
		pdata->tx_ring_count = pdata->new_tx_ring_count;
		pdata->tx_q_count = pdata->tx_ring_count;
		pdata->new_tx_ring_count = 0;
	}

	if (pdata->new_rx_ring_count) {
		pdata->rx_ring_count = pdata->new_rx_ring_count;
		pdata->new_rx_ring_count = 0;
	}

	/* Calculate the Rx buffer size before allocating rings */
	pdata->rx_buf_size = xgbe_calc_rx_buf_size(netdev, netdev->mtu);

	/* Allocate the channel and ring structures */
	ret = xgbe_alloc_channels(pdata);
	if (ret)
		return ret;

	/* Allocate the ring descriptors and buffers */
	ret = desc_if->alloc_ring_resources(pdata);
	if (ret)
		goto err_channels;

	/* Initialize the service and Tx timers */
	xgbe_init_timers(pdata);

	return 0;

err_channels:
	xgbe_free_memory(pdata);

	return ret;
}

static int xgbe_start(struct xgbe_prv_data *pdata)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_phy_if *phy_if = &pdata->phy_if;
	struct net_device *netdev = pdata->netdev;
	unsigned int i;
	int ret;

	/* Set the number of queues */
	ret = netif_set_real_num_tx_queues(netdev, pdata->tx_ring_count);
	if (ret) {
		netdev_err(netdev, "error setting real tx queue count\n");
		return ret;
	}

	ret = netif_set_real_num_rx_queues(netdev, pdata->rx_ring_count);
	if (ret) {
		netdev_err(netdev, "error setting real rx queue count\n");
		return ret;
	}

	/* Set RSS lookup table data for programming */
	for (i = 0; i < XGBE_RSS_MAX_TABLE_SIZE; i++)
		XGMAC_SET_BITS(pdata->rss_table[i], MAC_RSSDR, DMCH,
			       i % pdata->rx_ring_count);

	ret = hw_if->init(pdata);
	if (ret)
		return ret;

	xgbe_napi_enable(pdata, 1);

	ret = xgbe_request_irqs(pdata);
	if (ret)
		goto err_napi;

	ret = phy_if->phy_start(pdata);
	if (ret)
		goto err_irqs;

	hw_if->enable_tx(pdata);
	hw_if->enable_rx(pdata);

	udp_tunnel_nic_reset_ntf(netdev);

	netif_tx_start_all_queues(netdev);

	xgbe_start_timers(pdata);
	queue_work(pdata->dev_workqueue, &pdata->service_work);

	clear_bit(XGBE_STOPPED, &pdata->dev_state);

	return 0;

err_irqs:
	xgbe_free_irqs(pdata);

err_napi:
	xgbe_napi_disable(pdata, 1);

	hw_if->exit(pdata);

	return ret;
}

static void xgbe_stop(struct xgbe_prv_data *pdata)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_phy_if *phy_if = &pdata->phy_if;
	struct xgbe_channel *channel;
	struct net_device *netdev = pdata->netdev;
	struct netdev_queue *txq;
	unsigned int i;

	DBGPR("-->xgbe_stop\n");

	if (test_bit(XGBE_STOPPED, &pdata->dev_state))
		return;

	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(pdata->netdev);

	xgbe_stop_timers(pdata);
	flush_workqueue(pdata->dev_workqueue);

	xgbe_vxlan_unset_port(netdev, 0, 0, NULL);

	hw_if->disable_tx(pdata);
	hw_if->disable_rx(pdata);

	phy_if->phy_stop(pdata);

	xgbe_free_irqs(pdata);

	xgbe_napi_disable(pdata, 1);

	hw_if->exit(pdata);

	for (i = 0; i < pdata->channel_count; i++) {
		channel = pdata->channel[i];
		if (!channel->tx_ring)
			continue;

		txq = netdev_get_tx_queue(netdev, channel->queue_index);
		netdev_tx_reset_queue(txq);
	}

	set_bit(XGBE_STOPPED, &pdata->dev_state);

	DBGPR("<--xgbe_stop\n");
}

static void xgbe_stopdev(struct work_struct *work)
{
	struct xgbe_prv_data *pdata = container_of(work,
						   struct xgbe_prv_data,
						   stopdev_work);

	rtnl_lock();

	xgbe_stop(pdata);

	xgbe_free_tx_data(pdata);
	xgbe_free_rx_data(pdata);

	rtnl_unlock();

	netdev_alert(pdata->netdev, "device stopped\n");
}

void xgbe_full_restart_dev(struct xgbe_prv_data *pdata)
{
	/* If not running, "restart" will happen on open */
	if (!netif_running(pdata->netdev))
		return;

	xgbe_stop(pdata);

	xgbe_free_memory(pdata);
	xgbe_alloc_memory(pdata);

	xgbe_start(pdata);
}

void xgbe_restart_dev(struct xgbe_prv_data *pdata)
{
	/* If not running, "restart" will happen on open */
	if (!netif_running(pdata->netdev))
		return;

	xgbe_stop(pdata);

	xgbe_free_tx_data(pdata);
	xgbe_free_rx_data(pdata);

	xgbe_start(pdata);
}

static void xgbe_restart(struct work_struct *work)
{
	struct xgbe_prv_data *pdata = container_of(work,
						   struct xgbe_prv_data,
						   restart_work);

	rtnl_lock();

	xgbe_restart_dev(pdata);

	rtnl_unlock();
}

static void xgbe_tx_tstamp(struct work_struct *work)
{
	struct xgbe_prv_data *pdata = container_of(work,
						   struct xgbe_prv_data,
						   tx_tstamp_work);
	struct skb_shared_hwtstamps hwtstamps;
	u64 nsec;
	unsigned long flags;

	spin_lock_irqsave(&pdata->tstamp_lock, flags);
	if (!pdata->tx_tstamp_skb)
		goto unlock;

	if (pdata->tx_tstamp) {
		nsec = timecounter_cyc2time(&pdata->tstamp_tc,
					    pdata->tx_tstamp);

		memset(&hwtstamps, 0, sizeof(hwtstamps));
		hwtstamps.hwtstamp = ns_to_ktime(nsec);
		skb_tstamp_tx(pdata->tx_tstamp_skb, &hwtstamps);
	}

	dev_kfree_skb_any(pdata->tx_tstamp_skb);

	pdata->tx_tstamp_skb = NULL;

unlock:
	spin_unlock_irqrestore(&pdata->tstamp_lock, flags);
}

static int xgbe_get_hwtstamp_settings(struct xgbe_prv_data *pdata,
				      struct ifreq *ifreq)
{
	if (copy_to_user(ifreq->ifr_data, &pdata->tstamp_config,
			 sizeof(pdata->tstamp_config)))
		return -EFAULT;

	return 0;
}

static int xgbe_set_hwtstamp_settings(struct xgbe_prv_data *pdata,
				      struct ifreq *ifreq)
{
	struct hwtstamp_config config;
	unsigned int mac_tscr;

	if (copy_from_user(&config, ifreq->ifr_data, sizeof(config)))
		return -EFAULT;

	if (config.flags)
		return -EINVAL;

	mac_tscr = 0;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		break;

	case HWTSTAMP_TX_ON:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;

	case HWTSTAMP_FILTER_NTP_ALL:
	case HWTSTAMP_FILTER_ALL:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENALL, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	/* PTP v2, UDP, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		fallthrough;	/* to PTP v1, UDP, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, SNAPTYPSEL, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	/* PTP v2, UDP, Sync packet */
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		fallthrough;	/* to PTP v1, UDP, Sync packet */
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	/* PTP v2, UDP, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		fallthrough;	/* to PTP v1, UDP, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSMSTRENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	/* 802.AS1, Ethernet, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, AV8021ASMEN, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, SNAPTYPSEL, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	/* 802.AS1, Ethernet, Sync packet */
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, AV8021ASMEN, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	/* 802.AS1, Ethernet, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, AV8021ASMEN, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSMSTRENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	/* PTP v2/802.AS1, any layer, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, SNAPTYPSEL, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	/* PTP v2/802.AS1, any layer, Sync packet */
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	/* PTP v2/802.AS1, any layer, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSMSTRENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	default:
		return -ERANGE;
	}

	pdata->hw_if.config_tstamp(pdata, mac_tscr);

	memcpy(&pdata->tstamp_config, &config, sizeof(config));

	return 0;
}

static void xgbe_prep_tx_tstamp(struct xgbe_prv_data *pdata,
				struct sk_buff *skb,
				struct xgbe_packet_data *packet)
{
	unsigned long flags;

	if (XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES, PTP)) {
		spin_lock_irqsave(&pdata->tstamp_lock, flags);
		if (pdata->tx_tstamp_skb) {
			/* Another timestamp in progress, ignore this one */
			XGMAC_SET_BITS(packet->attributes,
				       TX_PACKET_ATTRIBUTES, PTP, 0);
		} else {
			pdata->tx_tstamp_skb = skb_get(skb);
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		}
		spin_unlock_irqrestore(&pdata->tstamp_lock, flags);
	}

	skb_tx_timestamp(skb);
}

static void xgbe_prep_vlan(struct sk_buff *skb, struct xgbe_packet_data *packet)
{
	if (skb_vlan_tag_present(skb))
		packet->vlan_ctag = skb_vlan_tag_get(skb);
}

static int xgbe_prep_tso(struct sk_buff *skb, struct xgbe_packet_data *packet)
{
	int ret;

	if (!XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			    TSO_ENABLE))
		return 0;

	ret = skb_cow_head(skb, 0);
	if (ret)
		return ret;

	if (XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES, VXLAN)) {
		packet->header_len = skb_inner_transport_offset(skb) +
				     inner_tcp_hdrlen(skb);
		packet->tcp_header_len = inner_tcp_hdrlen(skb);
	} else {
		packet->header_len = skb_transport_offset(skb) +
				     tcp_hdrlen(skb);
		packet->tcp_header_len = tcp_hdrlen(skb);
	}
	packet->tcp_payload_len = skb->len - packet->header_len;
	packet->mss = skb_shinfo(skb)->gso_size;

	DBGPR("  packet->header_len=%u\n", packet->header_len);
	DBGPR("  packet->tcp_header_len=%u, packet->tcp_payload_len=%u\n",
	      packet->tcp_header_len, packet->tcp_payload_len);
	DBGPR("  packet->mss=%u\n", packet->mss);

	/* Update the number of packets that will ultimately be transmitted
	 * along with the extra bytes for each extra packet
	 */
	packet->tx_packets = skb_shinfo(skb)->gso_segs;
	packet->tx_bytes += (packet->tx_packets - 1) * packet->header_len;

	return 0;
}

static bool xgbe_is_vxlan(struct sk_buff *skb)
{
	if (!skb->encapsulation)
		return false;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return false;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		if (ip_hdr(skb)->protocol != IPPROTO_UDP)
			return false;
		break;

	case htons(ETH_P_IPV6):
		if (ipv6_hdr(skb)->nexthdr != IPPROTO_UDP)
			return false;
		break;

	default:
		return false;
	}

	if (skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
	    skb->inner_protocol != htons(ETH_P_TEB) ||
	    (skb_inner_mac_header(skb) - skb_transport_header(skb) !=
	     sizeof(struct udphdr) + sizeof(struct vxlanhdr)))
		return false;

	return true;
}

static int xgbe_is_tso(struct sk_buff *skb)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (!skb_is_gso(skb))
		return 0;

	DBGPR("  TSO packet to be processed\n");

	return 1;
}

static void xgbe_packet_info(struct xgbe_prv_data *pdata,
			     struct xgbe_ring *ring, struct sk_buff *skb,
			     struct xgbe_packet_data *packet)
{
	skb_frag_t *frag;
	unsigned int context_desc;
	unsigned int len;
	unsigned int i;

	packet->skb = skb;

	context_desc = 0;
	packet->rdesc_count = 0;

	packet->tx_packets = 1;
	packet->tx_bytes = skb->len;

	if (xgbe_is_tso(skb)) {
		/* TSO requires an extra descriptor if mss is different */
		if (skb_shinfo(skb)->gso_size != ring->tx.cur_mss) {
			context_desc = 1;
			packet->rdesc_count++;
		}

		/* TSO requires an extra descriptor for TSO header */
		packet->rdesc_count++;

		XGMAC_SET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			       TSO_ENABLE, 1);
		XGMAC_SET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			       CSUM_ENABLE, 1);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL)
		XGMAC_SET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			       CSUM_ENABLE, 1);

	if (xgbe_is_vxlan(skb))
		XGMAC_SET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			       VXLAN, 1);

	if (skb_vlan_tag_present(skb)) {
		/* VLAN requires an extra descriptor if tag is different */
		if (skb_vlan_tag_get(skb) != ring->tx.cur_vlan_ctag)
			/* We can share with the TSO context descriptor */
			if (!context_desc) {
				context_desc = 1;
				packet->rdesc_count++;
			}

		XGMAC_SET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			       VLAN_CTAG, 1);
	}

	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
	    (pdata->tstamp_config.tx_type == HWTSTAMP_TX_ON))
		XGMAC_SET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES,
			       PTP, 1);

	for (len = skb_headlen(skb); len;) {
		packet->rdesc_count++;
		len -= min_t(unsigned int, len, XGBE_TX_MAX_BUF_SIZE);
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		for (len = skb_frag_size(frag); len; ) {
			packet->rdesc_count++;
			len -= min_t(unsigned int, len, XGBE_TX_MAX_BUF_SIZE);
		}
	}
}

static int xgbe_open(struct net_device *netdev)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	int ret;

	/* Create the various names based on netdev name */
	snprintf(pdata->an_name, sizeof(pdata->an_name) - 1, "%s-pcs",
		 netdev_name(netdev));

	snprintf(pdata->ecc_name, sizeof(pdata->ecc_name) - 1, "%s-ecc",
		 netdev_name(netdev));

	snprintf(pdata->i2c_name, sizeof(pdata->i2c_name) - 1, "%s-i2c",
		 netdev_name(netdev));

	/* Create workqueues */
	pdata->dev_workqueue =
		create_singlethread_workqueue(netdev_name(netdev));
	if (!pdata->dev_workqueue) {
		netdev_err(netdev, "device workqueue creation failed\n");
		return -ENOMEM;
	}

	pdata->an_workqueue =
		create_singlethread_workqueue(pdata->an_name);
	if (!pdata->an_workqueue) {
		netdev_err(netdev, "phy workqueue creation failed\n");
		ret = -ENOMEM;
		goto err_dev_wq;
	}

	/* Reset the phy settings */
	ret = xgbe_phy_reset(pdata);
	if (ret)
		goto err_an_wq;

	/* Enable the clocks */
	ret = clk_prepare_enable(pdata->sysclk);
	if (ret) {
		netdev_alert(netdev, "dma clk_prepare_enable failed\n");
		goto err_an_wq;
	}

	ret = clk_prepare_enable(pdata->ptpclk);
	if (ret) {
		netdev_alert(netdev, "ptp clk_prepare_enable failed\n");
		goto err_sysclk;
	}

	INIT_WORK(&pdata->service_work, xgbe_service);
	INIT_WORK(&pdata->restart_work, xgbe_restart);
	INIT_WORK(&pdata->stopdev_work, xgbe_stopdev);
	INIT_WORK(&pdata->tx_tstamp_work, xgbe_tx_tstamp);

	ret = xgbe_alloc_memory(pdata);
	if (ret)
		goto err_ptpclk;

	ret = xgbe_start(pdata);
	if (ret)
		goto err_mem;

	clear_bit(XGBE_DOWN, &pdata->dev_state);

	return 0;

err_mem:
	xgbe_free_memory(pdata);

err_ptpclk:
	clk_disable_unprepare(pdata->ptpclk);

err_sysclk:
	clk_disable_unprepare(pdata->sysclk);

err_an_wq:
	destroy_workqueue(pdata->an_workqueue);

err_dev_wq:
	destroy_workqueue(pdata->dev_workqueue);

	return ret;
}

static int xgbe_close(struct net_device *netdev)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	/* Stop the device */
	xgbe_stop(pdata);

	xgbe_free_memory(pdata);

	/* Disable the clocks */
	clk_disable_unprepare(pdata->ptpclk);
	clk_disable_unprepare(pdata->sysclk);

	flush_workqueue(pdata->an_workqueue);
	destroy_workqueue(pdata->an_workqueue);

	flush_workqueue(pdata->dev_workqueue);
	destroy_workqueue(pdata->dev_workqueue);

	set_bit(XGBE_DOWN, &pdata->dev_state);

	return 0;
}

static netdev_tx_t xgbe_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_desc_if *desc_if = &pdata->desc_if;
	struct xgbe_channel *channel;
	struct xgbe_ring *ring;
	struct xgbe_packet_data *packet;
	struct netdev_queue *txq;
	netdev_tx_t ret;

	DBGPR("-->xgbe_xmit: skb->len = %d\n", skb->len);

	channel = pdata->channel[skb->queue_mapping];
	txq = netdev_get_tx_queue(netdev, channel->queue_index);
	ring = channel->tx_ring;
	packet = &ring->packet_data;

	ret = NETDEV_TX_OK;

	if (skb->len == 0) {
		netif_err(pdata, tx_err, netdev,
			  "empty skb received from stack\n");
		dev_kfree_skb_any(skb);
		goto tx_netdev_return;
	}

	/* Calculate preliminary packet info */
	memset(packet, 0, sizeof(*packet));
	xgbe_packet_info(pdata, ring, skb, packet);

	/* Check that there are enough descriptors available */
	ret = xgbe_maybe_stop_tx_queue(channel, ring, packet->rdesc_count);
	if (ret)
		goto tx_netdev_return;

	ret = xgbe_prep_tso(skb, packet);
	if (ret) {
		netif_err(pdata, tx_err, netdev,
			  "error processing TSO packet\n");
		dev_kfree_skb_any(skb);
		goto tx_netdev_return;
	}
	xgbe_prep_vlan(skb, packet);

	if (!desc_if->map_tx_skb(channel, skb)) {
		dev_kfree_skb_any(skb);
		goto tx_netdev_return;
	}

	xgbe_prep_tx_tstamp(pdata, skb, packet);

	/* Report on the actual number of bytes (to be) sent */
	netdev_tx_sent_queue(txq, packet->tx_bytes);

	/* Configure required descriptor fields for transmission */
	hw_if->dev_xmit(channel);

	if (netif_msg_pktdata(pdata))
		xgbe_print_pkt(netdev, skb, true);

	/* Stop the queue in advance if there may not be enough descriptors */
	xgbe_maybe_stop_tx_queue(channel, ring, XGBE_TX_MAX_DESCS);

	ret = NETDEV_TX_OK;

tx_netdev_return:
	return ret;
}

static void xgbe_set_rx_mode(struct net_device *netdev)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;

	DBGPR("-->xgbe_set_rx_mode\n");

	hw_if->config_rx_mode(pdata);

	DBGPR("<--xgbe_set_rx_mode\n");
}

static int xgbe_set_mac_address(struct net_device *netdev, void *addr)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct sockaddr *saddr = addr;

	DBGPR("-->xgbe_set_mac_address\n");

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, saddr->sa_data, netdev->addr_len);

	hw_if->set_mac_address(pdata, netdev->dev_addr);

	DBGPR("<--xgbe_set_mac_address\n");

	return 0;
}

static int xgbe_ioctl(struct net_device *netdev, struct ifreq *ifreq, int cmd)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	int ret;

	switch (cmd) {
	case SIOCGHWTSTAMP:
		ret = xgbe_get_hwtstamp_settings(pdata, ifreq);
		break;

	case SIOCSHWTSTAMP:
		ret = xgbe_set_hwtstamp_settings(pdata, ifreq);
		break;

	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int xgbe_change_mtu(struct net_device *netdev, int mtu)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	int ret;

	DBGPR("-->xgbe_change_mtu\n");

	ret = xgbe_calc_rx_buf_size(netdev, mtu);
	if (ret < 0)
		return ret;

	pdata->rx_buf_size = ret;
	netdev->mtu = mtu;

	xgbe_restart_dev(pdata);

	DBGPR("<--xgbe_change_mtu\n");

	return 0;
}

static void xgbe_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	netdev_warn(netdev, "tx timeout, device restarting\n");
	schedule_work(&pdata->restart_work);
}

static void xgbe_get_stats64(struct net_device *netdev,
			     struct rtnl_link_stats64 *s)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_mmc_stats *pstats = &pdata->mmc_stats;

	DBGPR("-->%s\n", __func__);

	pdata->hw_if.read_mmc_stats(pdata);

	s->rx_packets = pstats->rxframecount_gb;
	s->rx_bytes = pstats->rxoctetcount_gb;
	s->rx_errors = pstats->rxframecount_gb -
		       pstats->rxbroadcastframes_g -
		       pstats->rxmulticastframes_g -
		       pstats->rxunicastframes_g;
	s->multicast = pstats->rxmulticastframes_g;
	s->rx_length_errors = pstats->rxlengtherror;
	s->rx_crc_errors = pstats->rxcrcerror;
	s->rx_fifo_errors = pstats->rxfifooverflow;

	s->tx_packets = pstats->txframecount_gb;
	s->tx_bytes = pstats->txoctetcount_gb;
	s->tx_errors = pstats->txframecount_gb - pstats->txframecount_g;
	s->tx_dropped = netdev->stats.tx_dropped;

	DBGPR("<--%s\n", __func__);
}

static int xgbe_vlan_rx_add_vid(struct net_device *netdev, __be16 proto,
				u16 vid)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;

	DBGPR("-->%s\n", __func__);

	set_bit(vid, pdata->active_vlans);
	hw_if->update_vlan_hash_table(pdata);

	DBGPR("<--%s\n", __func__);

	return 0;
}

static int xgbe_vlan_rx_kill_vid(struct net_device *netdev, __be16 proto,
				 u16 vid)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;

	DBGPR("-->%s\n", __func__);

	clear_bit(vid, pdata->active_vlans);
	hw_if->update_vlan_hash_table(pdata);

	DBGPR("<--%s\n", __func__);

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void xgbe_poll_controller(struct net_device *netdev)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_channel *channel;
	unsigned int i;

	DBGPR("-->xgbe_poll_controller\n");

	if (pdata->per_channel_irq) {
		for (i = 0; i < pdata->channel_count; i++) {
			channel = pdata->channel[i];
			xgbe_dma_isr(channel->dma_irq, channel);
		}
	} else {
		disable_irq(pdata->dev_irq);
		xgbe_isr(pdata->dev_irq, pdata);
		enable_irq(pdata->dev_irq);
	}

	DBGPR("<--xgbe_poll_controller\n");
}
#endif /* End CONFIG_NET_POLL_CONTROLLER */

static int xgbe_setup_tc(struct net_device *netdev, enum tc_setup_type type,
			 void *type_data)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct tc_mqprio_qopt *mqprio = type_data;
	u8 tc;

	if (type != TC_SETUP_QDISC_MQPRIO)
		return -EOPNOTSUPP;

	mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;
	tc = mqprio->num_tc;

	if (tc > pdata->hw_feat.tc_cnt)
		return -EINVAL;

	pdata->num_tcs = tc;
	pdata->hw_if.config_tc(pdata);

	return 0;
}

static netdev_features_t xgbe_fix_features(struct net_device *netdev,
					   netdev_features_t features)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	netdev_features_t vxlan_base;

	vxlan_base = NETIF_F_GSO_UDP_TUNNEL | NETIF_F_RX_UDP_TUNNEL_PORT;

	if (!pdata->hw_feat.vxn)
		return features;

	/* VXLAN CSUM requires VXLAN base */
	if ((features & NETIF_F_GSO_UDP_TUNNEL_CSUM) &&
	    !(features & NETIF_F_GSO_UDP_TUNNEL)) {
		netdev_notice(netdev,
			      "forcing tx udp tunnel support\n");
		features |= NETIF_F_GSO_UDP_TUNNEL;
	}

	/* Can't do one without doing the other */
	if ((features & vxlan_base) != vxlan_base) {
		netdev_notice(netdev,
			      "forcing both tx and rx udp tunnel support\n");
		features |= vxlan_base;
	}

	if (features & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)) {
		if (!(features & NETIF_F_GSO_UDP_TUNNEL_CSUM)) {
			netdev_notice(netdev,
				      "forcing tx udp tunnel checksumming on\n");
			features |= NETIF_F_GSO_UDP_TUNNEL_CSUM;
		}
	} else {
		if (features & NETIF_F_GSO_UDP_TUNNEL_CSUM) {
			netdev_notice(netdev,
				      "forcing tx udp tunnel checksumming off\n");
			features &= ~NETIF_F_GSO_UDP_TUNNEL_CSUM;
		}
	}

	return features;
}

static int xgbe_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	netdev_features_t rxhash, rxcsum, rxvlan, rxvlan_filter;
	int ret = 0;

	rxhash = pdata->netdev_features & NETIF_F_RXHASH;
	rxcsum = pdata->netdev_features & NETIF_F_RXCSUM;
	rxvlan = pdata->netdev_features & NETIF_F_HW_VLAN_CTAG_RX;
	rxvlan_filter = pdata->netdev_features & NETIF_F_HW_VLAN_CTAG_FILTER;

	if ((features & NETIF_F_RXHASH) && !rxhash)
		ret = hw_if->enable_rss(pdata);
	else if (!(features & NETIF_F_RXHASH) && rxhash)
		ret = hw_if->disable_rss(pdata);
	if (ret)
		return ret;

	if ((features & NETIF_F_RXCSUM) && !rxcsum)
		hw_if->enable_rx_csum(pdata);
	else if (!(features & NETIF_F_RXCSUM) && rxcsum)
		hw_if->disable_rx_csum(pdata);

	if ((features & NETIF_F_HW_VLAN_CTAG_RX) && !rxvlan)
		hw_if->enable_rx_vlan_stripping(pdata);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_RX) && rxvlan)
		hw_if->disable_rx_vlan_stripping(pdata);

	if ((features & NETIF_F_HW_VLAN_CTAG_FILTER) && !rxvlan_filter)
		hw_if->enable_rx_vlan_filtering(pdata);
	else if (!(features & NETIF_F_HW_VLAN_CTAG_FILTER) && rxvlan_filter)
		hw_if->disable_rx_vlan_filtering(pdata);

	pdata->netdev_features = features;

	DBGPR("<--xgbe_set_features\n");

	return 0;
}

static netdev_features_t xgbe_features_check(struct sk_buff *skb,
					     struct net_device *netdev,
					     netdev_features_t features)
{
	features = vlan_features_check(skb, features);
	features = vxlan_features_check(skb, features);

	return features;
}

static const struct net_device_ops xgbe_netdev_ops = {
	.ndo_open		= xgbe_open,
	.ndo_stop		= xgbe_close,
	.ndo_start_xmit		= xgbe_xmit,
	.ndo_set_rx_mode	= xgbe_set_rx_mode,
	.ndo_set_mac_address	= xgbe_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= xgbe_ioctl,
	.ndo_change_mtu		= xgbe_change_mtu,
	.ndo_tx_timeout		= xgbe_tx_timeout,
	.ndo_get_stats64	= xgbe_get_stats64,
	.ndo_vlan_rx_add_vid	= xgbe_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= xgbe_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= xgbe_poll_controller,
#endif
	.ndo_setup_tc		= xgbe_setup_tc,
	.ndo_fix_features	= xgbe_fix_features,
	.ndo_set_features	= xgbe_set_features,
	.ndo_udp_tunnel_add	= udp_tunnel_nic_add_port,
	.ndo_udp_tunnel_del	= udp_tunnel_nic_del_port,
	.ndo_features_check	= xgbe_features_check,
};

const struct net_device_ops *xgbe_get_netdev_ops(void)
{
	return &xgbe_netdev_ops;
}

static void xgbe_rx_refresh(struct xgbe_channel *channel)
{
	struct xgbe_prv_data *pdata = channel->pdata;
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_desc_if *desc_if = &pdata->desc_if;
	struct xgbe_ring *ring = channel->rx_ring;
	struct xgbe_ring_data *rdata;

	while (ring->dirty != ring->cur) {
		rdata = XGBE_GET_DESC_DATA(ring, ring->dirty);

		/* Reset rdata values */
		desc_if->unmap_rdata(pdata, rdata);

		if (desc_if->map_rx_buffer(pdata, ring, rdata))
			break;

		hw_if->rx_desc_reset(pdata, rdata, ring->dirty);

		ring->dirty++;
	}

	/* Make sure everything is written before the register write */
	wmb();

	/* Update the Rx Tail Pointer Register with address of
	 * the last cleaned entry */
	rdata = XGBE_GET_DESC_DATA(ring, ring->dirty - 1);
	XGMAC_DMA_IOWRITE(channel, DMA_CH_RDTR_LO,
			  lower_32_bits(rdata->rdesc_dma));
}

static struct sk_buff *xgbe_create_skb(struct xgbe_prv_data *pdata,
				       struct napi_struct *napi,
				       struct xgbe_ring_data *rdata,
				       unsigned int len)
{
	struct sk_buff *skb;
	u8 *packet;

	skb = napi_alloc_skb(napi, rdata->rx.hdr.dma_len);
	if (!skb)
		return NULL;

	/* Pull in the header buffer which may contain just the header
	 * or the header plus data
	 */
	dma_sync_single_range_for_cpu(pdata->dev, rdata->rx.hdr.dma_base,
				      rdata->rx.hdr.dma_off,
				      rdata->rx.hdr.dma_len, DMA_FROM_DEVICE);

	packet = page_address(rdata->rx.hdr.pa.pages) +
		 rdata->rx.hdr.pa.pages_offset;
	skb_copy_to_linear_data(skb, packet, len);
	skb_put(skb, len);

	return skb;
}

static unsigned int xgbe_rx_buf1_len(struct xgbe_ring_data *rdata,
				     struct xgbe_packet_data *packet)
{
	/* Always zero if not the first descriptor */
	if (!XGMAC_GET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES, FIRST))
		return 0;

	/* First descriptor with split header, return header length */
	if (rdata->rx.hdr_len)
		return rdata->rx.hdr_len;

	/* First descriptor but not the last descriptor and no split header,
	 * so the full buffer was used
	 */
	if (!XGMAC_GET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES, LAST))
		return rdata->rx.hdr.dma_len;

	/* First descriptor and last descriptor and no split header, so
	 * calculate how much of the buffer was used
	 */
	return min_t(unsigned int, rdata->rx.hdr.dma_len, rdata->rx.len);
}

static unsigned int xgbe_rx_buf2_len(struct xgbe_ring_data *rdata,
				     struct xgbe_packet_data *packet,
				     unsigned int len)
{
	/* Always the full buffer if not the last descriptor */
	if (!XGMAC_GET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES, LAST))
		return rdata->rx.buf.dma_len;

	/* Last descriptor so calculate how much of the buffer was used
	 * for the last bit of data
	 */
	return rdata->rx.len - len;
}

static int xgbe_tx_poll(struct xgbe_channel *channel)
{
	struct xgbe_prv_data *pdata = channel->pdata;
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_desc_if *desc_if = &pdata->desc_if;
	struct xgbe_ring *ring = channel->tx_ring;
	struct xgbe_ring_data *rdata;
	struct xgbe_ring_desc *rdesc;
	struct net_device *netdev = pdata->netdev;
	struct netdev_queue *txq;
	int processed = 0;
	unsigned int tx_packets = 0, tx_bytes = 0;
	unsigned int cur;

	DBGPR("-->xgbe_tx_poll\n");

	/* Nothing to do if there isn't a Tx ring for this channel */
	if (!ring)
		return 0;

	cur = ring->cur;

	/* Be sure we get ring->cur before accessing descriptor data */
	smp_rmb();

	txq = netdev_get_tx_queue(netdev, channel->queue_index);

	while ((processed < XGBE_TX_DESC_MAX_PROC) &&
	       (ring->dirty != cur)) {
		rdata = XGBE_GET_DESC_DATA(ring, ring->dirty);
		rdesc = rdata->rdesc;

		if (!hw_if->tx_complete(rdesc))
			break;

		/* Make sure descriptor fields are read after reading the OWN
		 * bit */
		dma_rmb();

		if (netif_msg_tx_done(pdata))
			xgbe_dump_tx_desc(pdata, ring, ring->dirty, 1, 0);

		if (hw_if->is_last_desc(rdesc)) {
			tx_packets += rdata->tx.packets;
			tx_bytes += rdata->tx.bytes;
		}

		/* Free the SKB and reset the descriptor for re-use */
		desc_if->unmap_rdata(pdata, rdata);
		hw_if->tx_desc_reset(rdata);

		processed++;
		ring->dirty++;
	}

	if (!processed)
		return 0;

	netdev_tx_completed_queue(txq, tx_packets, tx_bytes);

	if ((ring->tx.queue_stopped == 1) &&
	    (xgbe_tx_avail_desc(ring) > XGBE_TX_DESC_MIN_FREE)) {
		ring->tx.queue_stopped = 0;
		netif_tx_wake_queue(txq);
	}

	DBGPR("<--xgbe_tx_poll: processed=%d\n", processed);

	return processed;
}

static int xgbe_rx_poll(struct xgbe_channel *channel, int budget)
{
	struct xgbe_prv_data *pdata = channel->pdata;
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_ring *ring = channel->rx_ring;
	struct xgbe_ring_data *rdata;
	struct xgbe_packet_data *packet;
	struct net_device *netdev = pdata->netdev;
	struct napi_struct *napi;
	struct sk_buff *skb;
	struct skb_shared_hwtstamps *hwtstamps;
	unsigned int last, error, context_next, context;
	unsigned int len, buf1_len, buf2_len, max_len;
	unsigned int received = 0;
	int packet_count = 0;

	DBGPR("-->xgbe_rx_poll: budget=%d\n", budget);

	/* Nothing to do if there isn't a Rx ring for this channel */
	if (!ring)
		return 0;

	last = 0;
	context_next = 0;

	napi = (pdata->per_channel_irq) ? &channel->napi : &pdata->napi;

	rdata = XGBE_GET_DESC_DATA(ring, ring->cur);
	packet = &ring->packet_data;
	while (packet_count < budget) {
		DBGPR("  cur = %d\n", ring->cur);

		/* First time in loop see if we need to restore state */
		if (!received && rdata->state_saved) {
			skb = rdata->state.skb;
			error = rdata->state.error;
			len = rdata->state.len;
		} else {
			memset(packet, 0, sizeof(*packet));
			skb = NULL;
			error = 0;
			len = 0;
		}

read_again:
		rdata = XGBE_GET_DESC_DATA(ring, ring->cur);

		if (xgbe_rx_dirty_desc(ring) > (XGBE_RX_DESC_CNT >> 3))
			xgbe_rx_refresh(channel);

		if (hw_if->dev_read(channel))
			break;

		received++;
		ring->cur++;

		last = XGMAC_GET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
				      LAST);
		context_next = XGMAC_GET_BITS(packet->attributes,
					      RX_PACKET_ATTRIBUTES,
					      CONTEXT_NEXT);
		context = XGMAC_GET_BITS(packet->attributes,
					 RX_PACKET_ATTRIBUTES,
					 CONTEXT);

		/* Earlier error, just drain the remaining data */
		if ((!last || context_next) && error)
			goto read_again;

		if (error || packet->errors) {
			if (packet->errors)
				netif_err(pdata, rx_err, netdev,
					  "error in received packet\n");
			dev_kfree_skb(skb);
			goto next_packet;
		}

		if (!context) {
			/* Get the data length in the descriptor buffers */
			buf1_len = xgbe_rx_buf1_len(rdata, packet);
			len += buf1_len;
			buf2_len = xgbe_rx_buf2_len(rdata, packet, len);
			len += buf2_len;

			if (!skb) {
				skb = xgbe_create_skb(pdata, napi, rdata,
						      buf1_len);
				if (!skb) {
					error = 1;
					goto skip_data;
				}
			}

			if (buf2_len) {
				dma_sync_single_range_for_cpu(pdata->dev,
							rdata->rx.buf.dma_base,
							rdata->rx.buf.dma_off,
							rdata->rx.buf.dma_len,
							DMA_FROM_DEVICE);

				skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
						rdata->rx.buf.pa.pages,
						rdata->rx.buf.pa.pages_offset,
						buf2_len,
						rdata->rx.buf.dma_len);
				rdata->rx.buf.pa.pages = NULL;
			}
		}

skip_data:
		if (!last || context_next)
			goto read_again;

		if (!skb)
			goto next_packet;

		/* Be sure we don't exceed the configured MTU */
		max_len = netdev->mtu + ETH_HLEN;
		if (!(netdev->features & NETIF_F_HW_VLAN_CTAG_RX) &&
		    (skb->protocol == htons(ETH_P_8021Q)))
			max_len += VLAN_HLEN;

		if (skb->len > max_len) {
			netif_err(pdata, rx_err, netdev,
				  "packet length exceeds configured MTU\n");
			dev_kfree_skb(skb);
			goto next_packet;
		}

		if (netif_msg_pktdata(pdata))
			xgbe_print_pkt(netdev, skb, false);

		skb_checksum_none_assert(skb);
		if (XGMAC_GET_BITS(packet->attributes,
				   RX_PACKET_ATTRIBUTES, CSUM_DONE))
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (XGMAC_GET_BITS(packet->attributes,
				   RX_PACKET_ATTRIBUTES, TNP)) {
			skb->encapsulation = 1;

			if (XGMAC_GET_BITS(packet->attributes,
					   RX_PACKET_ATTRIBUTES, TNPCSUM_DONE))
				skb->csum_level = 1;
		}

		if (XGMAC_GET_BITS(packet->attributes,
				   RX_PACKET_ATTRIBUTES, VLAN_CTAG))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       packet->vlan_ctag);

		if (XGMAC_GET_BITS(packet->attributes,
				   RX_PACKET_ATTRIBUTES, RX_TSTAMP)) {
			u64 nsec;

			nsec = timecounter_cyc2time(&pdata->tstamp_tc,
						    packet->rx_tstamp);
			hwtstamps = skb_hwtstamps(skb);
			hwtstamps->hwtstamp = ns_to_ktime(nsec);
		}

		if (XGMAC_GET_BITS(packet->attributes,
				   RX_PACKET_ATTRIBUTES, RSS_HASH))
			skb_set_hash(skb, packet->rss_hash,
				     packet->rss_hash_type);

		skb->dev = netdev;
		skb->protocol = eth_type_trans(skb, netdev);
		skb_record_rx_queue(skb, channel->queue_index);

		napi_gro_receive(napi, skb);

next_packet:
		packet_count++;
	}

	/* Check if we need to save state before leaving */
	if (received && (!last || context_next)) {
		rdata = XGBE_GET_DESC_DATA(ring, ring->cur);
		rdata->state_saved = 1;
		rdata->state.skb = skb;
		rdata->state.len = len;
		rdata->state.error = error;
	}

	DBGPR("<--xgbe_rx_poll: packet_count = %d\n", packet_count);

	return packet_count;
}

static int xgbe_one_poll(struct napi_struct *napi, int budget)
{
	struct xgbe_channel *channel = container_of(napi, struct xgbe_channel,
						    napi);
	struct xgbe_prv_data *pdata = channel->pdata;
	int processed = 0;

	DBGPR("-->xgbe_one_poll: budget=%d\n", budget);

	/* Cleanup Tx ring first */
	xgbe_tx_poll(channel);

	/* Process Rx ring next */
	processed = xgbe_rx_poll(channel, budget);

	/* If we processed everything, we are done */
	if ((processed < budget) && napi_complete_done(napi, processed)) {
		/* Enable Tx and Rx interrupts */
		if (pdata->channel_irq_mode)
			xgbe_enable_rx_tx_int(pdata, channel);
		else
			enable_irq(channel->dma_irq);
	}

	DBGPR("<--xgbe_one_poll: received = %d\n", processed);

	return processed;
}

static int xgbe_all_poll(struct napi_struct *napi, int budget)
{
	struct xgbe_prv_data *pdata = container_of(napi, struct xgbe_prv_data,
						   napi);
	struct xgbe_channel *channel;
	int ring_budget;
	int processed, last_processed;
	unsigned int i;

	DBGPR("-->xgbe_all_poll: budget=%d\n", budget);

	processed = 0;
	ring_budget = budget / pdata->rx_ring_count;
	do {
		last_processed = processed;

		for (i = 0; i < pdata->channel_count; i++) {
			channel = pdata->channel[i];

			/* Cleanup Tx ring first */
			xgbe_tx_poll(channel);

			/* Process Rx ring next */
			if (ring_budget > (budget - processed))
				ring_budget = budget - processed;
			processed += xgbe_rx_poll(channel, ring_budget);
		}
	} while ((processed < budget) && (processed != last_processed));

	/* If we processed everything, we are done */
	if ((processed < budget) && napi_complete_done(napi, processed)) {
		/* Enable Tx and Rx interrupts */
		xgbe_enable_rx_tx_ints(pdata);
	}

	DBGPR("<--xgbe_all_poll: received = %d\n", processed);

	return processed;
}

void xgbe_dump_tx_desc(struct xgbe_prv_data *pdata, struct xgbe_ring *ring,
		       unsigned int idx, unsigned int count, unsigned int flag)
{
	struct xgbe_ring_data *rdata;
	struct xgbe_ring_desc *rdesc;

	while (count--) {
		rdata = XGBE_GET_DESC_DATA(ring, idx);
		rdesc = rdata->rdesc;
		netdev_dbg(pdata->netdev,
			   "TX_NORMAL_DESC[%d %s] = %08x:%08x:%08x:%08x\n", idx,
			   (flag == 1) ? "QUEUED FOR TX" : "TX BY DEVICE",
			   le32_to_cpu(rdesc->desc0),
			   le32_to_cpu(rdesc->desc1),
			   le32_to_cpu(rdesc->desc2),
			   le32_to_cpu(rdesc->desc3));
		idx++;
	}
}

void xgbe_dump_rx_desc(struct xgbe_prv_data *pdata, struct xgbe_ring *ring,
		       unsigned int idx)
{
	struct xgbe_ring_data *rdata;
	struct xgbe_ring_desc *rdesc;

	rdata = XGBE_GET_DESC_DATA(ring, idx);
	rdesc = rdata->rdesc;
	netdev_dbg(pdata->netdev,
		   "RX_NORMAL_DESC[%d RX BY DEVICE] = %08x:%08x:%08x:%08x\n",
		   idx, le32_to_cpu(rdesc->desc0), le32_to_cpu(rdesc->desc1),
		   le32_to_cpu(rdesc->desc2), le32_to_cpu(rdesc->desc3));
}

void xgbe_print_pkt(struct net_device *netdev, struct sk_buff *skb, bool tx_rx)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;
	unsigned char buffer[128];
	unsigned int i;

	netdev_dbg(netdev, "\n************** SKB dump ****************\n");

	netdev_dbg(netdev, "%s packet of %d bytes\n",
		   (tx_rx ? "TX" : "RX"), skb->len);

	netdev_dbg(netdev, "Dst MAC addr: %pM\n", eth->h_dest);
	netdev_dbg(netdev, "Src MAC addr: %pM\n", eth->h_source);
	netdev_dbg(netdev, "Protocol: %#06hx\n", ntohs(eth->h_proto));

	for (i = 0; i < skb->len; i += 32) {
		unsigned int len = min(skb->len - i, 32U);

		hex_dump_to_buffer(&skb->data[i], len, 32, 1,
				   buffer, sizeof(buffer), false);
		netdev_dbg(netdev, "  %#06x: %s\n", i, buffer);
	}

	netdev_dbg(netdev, "\n************** SKB dump ****************\n");
}
