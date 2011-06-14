/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
 #include "iwl-dev.h"
#include "iwl-trans.h"

static int iwl_trans_rx_alloc(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct device *dev = priv->bus.dev;

	memset(&priv->rxq, 0, sizeof(priv->rxq));

	spin_lock_init(&rxq->lock);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	if (WARN_ON(rxq->bd || rxq->rb_stts))
		return -EINVAL;

	/* Allocate the circular buffer of Read Buffer Descriptors (RBDs) */
	/*Every descriptor is an __le32, hence its */
	rxq->bd = dma_alloc_coherent(dev, 4 * RX_QUEUE_SIZE, &rxq->bd_dma,
				     GFP_KERNEL);
	if (!rxq->bd)
		goto err_bd;
	memset(rxq->bd, 0, 4 * RX_QUEUE_SIZE);

	/*Allocate the driver's pointer to receive buffer status */
	rxq->rb_stts = dma_alloc_coherent(dev, sizeof(*rxq->rb_stts),
					  &rxq->rb_stts_dma, GFP_KERNEL);
	if (!rxq->rb_stts)
		goto err_rb_stts;
	memset(rxq->rb_stts, 0, sizeof(*rxq->rb_stts));

	return 0;

err_rb_stts:
	dma_free_coherent(dev, 4 * RX_QUEUE_SIZE, rxq->bd, rxq->bd_dma);
	memset(&rxq->bd_dma, 0, sizeof(rxq->bd_dma));
	rxq->bd = NULL;
err_bd:
	return -ENOMEM;
}

static int iwl_trans_rx_init(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	int i, err;
	unsigned long flags;

	if (!rxq->bd) {
		err = iwl_trans_rx_alloc(priv);
		if (err)
			return err;
	}

	spin_lock_irqsave(&rxq->lock, flags);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++) {
		/* In the reset function, these buffers may have been allocated
		 * to an SKB, so we need to unmap and free potential storage */
		if (rxq->pool[i].page != NULL) {
			dma_unmap_page(priv->bus.dev, rxq->pool[i].page_dma,
				PAGE_SIZE << priv->hw_params.rx_page_order,
				DMA_FROM_DEVICE);
			__iwl_free_pages(priv, rxq->pool[i].page);
			rxq->pool[i].page = NULL;
		}
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);
	}

	for (i = 0; i < RX_QUEUE_SIZE; i++)
		rxq->queue[i] = NULL;

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->write_actual = 0;
	rxq->free_count = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);

	return 0;
}

static const struct iwl_trans_ops trans_ops = {
	.rx_init = iwl_trans_rx_init,
};

void iwl_trans_register(struct iwl_trans *trans)
{
	trans->ops = &trans_ops;
}
