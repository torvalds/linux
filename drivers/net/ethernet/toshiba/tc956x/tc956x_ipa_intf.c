/*
 * TC956x IPA I/F layer
 *
 * tc956x_ipa_intf.c
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
 *  20 Jan 2021 : Initial Version
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
 *  20 Jul 2021 : 1. IPA statistics print function removed
 *  VERSION     : 01-00-03
 *  22 Jul 2021 : 1. Dynamic CM3 TAMAP configuration
 *  VERSION     : 01-00-05
 *  23 Jul 2021 : 1. Add support for contiguous allocation of memory
 *  VERSION     : 01-00-06
 *  29 Jul 2021 : 1. Add support to set MAC Address register
 *  VERSION     : 01-00-07
 *  05 Aug 2021 : Store and use Port0 pci_dev for all DMA allocation/mapping for IPA path
 *  VERSION     : 01-00-08
 *  09 Sep 2021 : Reverted changes related to usage of Port-0 pci_dev for all DMA allocation/mapping for IPA path
 *  VERSION     : 01-00-12
 *  31 Jan 2022 : 1. Common used macro moved to common.h file.
 *  VERSION     : 01-00-39
 *  29 Apr 2022 : 1. Triggering Power saving at Link down after release of offloaded DMA channels
 *  VERSION     : 01-00-51
 */

#include <linux/dma-mapping.h>
#include "common.h"
#include "tc956xmac.h"
#include "tc956xmac_ioctl.h"
#ifdef TC956X
#include "dwxgmac2.h"
#endif
#include "tc956x_ipa_intf.h"

#define IPA_INTF_MAJOR_VERSION 0
#define IPA_INTF_MINOR_VERSION 1

#define CM3_PCIE_REGION_LOW_BOUND	0x60000000
#define CM3_PCIE_REGION_UP_BOUND	0xC0000000

/* At 10Gbps speed, only 72 entries can be parsed */
#define MAX_PARSABLE_FRP_ENTRIES 72

#define IPA_MAX_BUFFER_SIZE (9*1024) /* 9KBytes */
#define IPA_MAX_DESC_CNT    512
#define MAX_WDT		0xFF

#define MAC_ADDR_INDEX 1
#define MAC_ADDR_AE 1
#define MAC_ADDR_MBC 0x3F
#define MAC_ADDR_DCS 0x1
static u8 mac_addr_default[6] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

static DEFINE_SPINLOCK(cm3_tamap_lock);

extern int tc956xmac_rx_parser_configuration(struct tc956xmac_priv *priv);
extern void tc956x_config_CM3_tamap(struct device *dev,
				void __iomem *reg_pci_base_addr,
				struct tc956xmac_cm3_tamap *tamap,
				u8 table_entry);
/*!
 * \brief This API will return the version of IPA I/F maintained by Toshiba
 *	  The API will check for NULL pointers
 *
 * \param[in] ndev : TC956x netdev data structure
 *
 * \return : Correct Major and Minor number of the IPA I/F version
 *	     Major Number = Minor Number = 0xFF incase ndev is NULL or
 *	     tc956xmac_priv extracted from ndev is NULL
 */

struct tc956x_ipa_version get_ipa_intf_version(struct net_device *ndev)
{
	struct tc956x_ipa_version version;

	version.major = 0xFF;
	version.minor = 0xFF;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return version;
	}

	version.major = IPA_INTF_MAJOR_VERSION;
	version.minor = IPA_INTF_MINOR_VERSION;

	return version;
}
EXPORT_SYMBOL_GPL(get_ipa_intf_version);

/*!
 * \brief This API will store the client private structure inside TC956x private structure.
 *	  The API will check for NULL pointers. client_priv == NULL will be considered as a valid argument
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in]  client_priv : Client private data structure
 *
 * \return : 0 on success
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 */

int set_client_priv_data(struct net_device *ndev, void *client_priv)
{
	struct tc956xmac_priv *priv;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	priv->client_priv = client_priv;

	return 0;
}
EXPORT_SYMBOL_GPL(set_client_priv_data);

/*!
 * \brief This API will return the client private data structure
 *	  The API will check for NULL pointers
 *
 * \param[in] ndev : TC956x netdev data structure
 *
 * \return : Pointer to the client private data structure
 *	     NULL if ndev or tc956xmac_priv extracted from ndev is NULL
 */

void* get_client_priv_data(struct net_device *ndev)
{
	struct tc956xmac_priv *priv;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return NULL;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return NULL;
	}

	return priv->client_priv;
}
EXPORT_SYMBOL_GPL(get_client_priv_data);

static void free_ipa_tx_resources(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[channel->channel_num];
	u32 i;

	if (channel->ch_flags == TC956X_CONTIG_BUFS) {
		dma_free_coherent(priv->device,
				  channel->desc_size * channel->desc_cnt,
				  channel->desc_addr.desc_virt_addrs_base,
				  tx_q->dma_tx_phy);
		dma_free_coherent(priv->device,
				  channel->buf_size * channel->desc_cnt,
				  channel->buff_pool_addr.buff_pool_va_addrs_base[0],
				  tx_q->buff_tx_phy);
	} else {
		for (i = 0; i < channel->desc_cnt; i++) {
			dma_unmap_single(priv->device,
					 tx_q->tx_offload_skbuff_dma[i],
					 channel->buf_size, DMA_TO_DEVICE);

			if (tx_q->tx_offload_skbuff[i])
				dev_kfree_skb_any(tx_q->tx_offload_skbuff[i]);

			channel->buff_pool_addr.buff_pool_dma_addrs_base[i] = 0;
			channel->buff_pool_addr.buff_pool_va_addrs_base[i] = NULL;
		}
		dma_free_coherent(priv->device, channel->desc_size * channel->desc_cnt,
				  channel->desc_addr.desc_virt_addrs_base,
				  tx_q->dma_tx_phy);
		kfree(tx_q->tx_offload_skbuff);
		kfree(tx_q->tx_offload_skbuff_dma);
	}
}

static void free_ipa_rx_resources(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[channel->channel_num];
	u32 i;

	if (channel->ch_flags == TC956X_CONTIG_BUFS) {
		dma_free_coherent(priv->device,
				  channel->desc_size * channel->desc_cnt,
				  channel->desc_addr.desc_virt_addrs_base,
				  rx_q->dma_rx_phy);
		dma_free_coherent(priv->device,
				  channel->buf_size * channel->desc_cnt,
				  channel->buff_pool_addr.buff_pool_va_addrs_base[0],
				  rx_q->buff_rx_phy);
	} else {
		for (i = 0; i < channel->desc_cnt; i++) {
			dma_unmap_single(priv->device,
					 rx_q->rx_offload_skbuff_dma[i],
					 channel->buf_size, DMA_FROM_DEVICE);

			if (rx_q->rx_offload_skbuff[i])
				dev_kfree_skb_any(rx_q->rx_offload_skbuff[i]);

			channel->buff_pool_addr.buff_pool_dma_addrs_base[i] = 0;
			channel->buff_pool_addr.buff_pool_va_addrs_base[i] = NULL;
		}
		dma_free_coherent(priv->device, channel->desc_size * channel->desc_cnt,
				  channel->desc_addr.desc_virt_addrs_base,
				  rx_q->dma_rx_phy);
		kfree(rx_q->rx_offload_skbuff);
		kfree(rx_q->rx_offload_skbuff_dma);
	}
}

static int find_free_tx_channel(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	u32 ch;
	struct tc956xmac_tx_queue *tx_q;

	for (ch = 0; ch < MAX_TX_QUEUES_TO_USE; ch++) {
		/* Find a free channel to use for IPA */
		if (priv->plat->tx_dma_ch_owner[ch] == NOT_USED) {
			channel->channel_num = ch;
			break;
		}
	}

	if (ch >= MAX_TX_QUEUES_TO_USE) {
		netdev_err(priv->dev, "%s: ERROR: No valid Tx channel available\n", __func__);
		return -EINVAL;
	}

	priv->plat->tx_dma_ch_owner[ch] = USE_IN_OFFLOADER;

	tx_q = &priv->tx_queue[ch];
	tx_q->priv_data = priv;

	return 0;
}

static int find_free_rx_channel(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	u32 ch;
	struct tc956xmac_rx_queue *rx_q;

	for (ch = 0; ch < MAX_RX_QUEUES_TO_USE; ch++) {
		/* Find a free channel to use for IPA */
		if (priv->plat->rx_dma_ch_owner[ch] == NOT_USED) {
			channel->channel_num = ch;
			break;
		}
	}

	if (ch >= MAX_RX_QUEUES_TO_USE) {
		netdev_err(priv->dev, "%s: ERROR: No valid Rx channel available\n", __func__);
		return -EINVAL;
	}

	priv->plat->rx_dma_ch_owner[ch] = USE_IN_OFFLOADER;

	rx_q = &priv->rx_queue[ch];
	rx_q->priv_data = priv;

	return 0;
}

static int alloc_ipa_tx_resources(struct net_device *ndev, struct channel_info *channel, gfp_t flags)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct tc956xmac_tx_queue *tx_q;
	struct sk_buff *skb;
	u32 i;
	int ret = -EINVAL;

	ret = find_free_tx_channel(ndev, channel);

	if (ret)
		return ret;

	tx_q = &priv->tx_queue[channel->channel_num];

	channel->desc_addr.desc_virt_addrs_base = dma_alloc_coherent(priv->device,
								channel->desc_size * channel->desc_cnt,
								&tx_q->dma_tx_phy, flags);

	if (!channel->desc_addr.desc_virt_addrs_base) {
		netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
		goto err_mem;
	}

	tx_q->dma_tx = channel->desc_addr.desc_virt_addrs_base;
	channel->desc_addr.desc_dma_addrs_base = tx_q->dma_tx_phy;

	if (channel->ch_flags == TC956X_CONTIG_BUFS) {
		channel->buff_pool_addr.buff_pool_va_addrs_base[0] = dma_alloc_coherent(priv->device, channel->buf_size * channel->desc_cnt,
								&tx_q->buff_tx_phy, flags);
		if (!channel->buff_pool_addr.buff_pool_va_addrs_base[0]) {
			netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
			goto err_mem;
		}
		channel->buff_pool_addr.buff_pool_dma_addrs_base[0] = tx_q->buff_tx_phy;
		tx_q->buffer_tx_va_addr = channel->buff_pool_addr.buff_pool_va_addrs_base[0];
		return 0;
	}

	tx_q->tx_offload_skbuff_dma = kcalloc(channel->desc_cnt,
				      sizeof(*tx_q->tx_offload_skbuff_dma), flags);
	if (!tx_q->tx_offload_skbuff_dma) {

		netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
		goto err_mem;
	}

	tx_q->tx_offload_skbuff = kcalloc(channel->desc_cnt, sizeof(struct sk_buff *), flags);
	if (!tx_q->tx_offload_skbuff) {
		netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
		goto err_mem;
	}

	for (i = 0; i < channel->desc_cnt; i++) {
		skb = __netdev_alloc_skb_ip_align(priv->dev, channel->buf_size, flags);

		if (!skb) {
			netdev_err(priv->dev,
					"%s: Rx init fails; skb is NULL\n", __func__);
			goto err_mem;
		}

		tx_q->tx_offload_skbuff[i] = skb;
		tx_q->tx_offload_skbuff_dma[i] = dma_map_single(priv->device, skb->data,
							channel->buf_size, DMA_TO_DEVICE);

		if (dma_mapping_error(priv->device, tx_q->tx_offload_skbuff_dma[i])) {
			netdev_err(priv->dev, "%s: DMA mapping error\n", __func__);
			dev_kfree_skb_any(skb);
			goto err_mem;
		}

		channel->buff_pool_addr.buff_pool_va_addrs_base[i] = (void *)tx_q->tx_offload_skbuff[i]->data;
		channel->buff_pool_addr.buff_pool_dma_addrs_base[i] = tx_q->tx_offload_skbuff_dma[i];

	}

	return 0;

err_mem:
	free_ipa_tx_resources(ndev, channel);
	return -ENOMEM;
}

static int alloc_ipa_rx_resources(struct net_device *ndev, struct channel_info *channel, gfp_t flags)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct tc956xmac_rx_queue *rx_q;
	struct sk_buff *skb;
	u32 i;
	int ret = -EINVAL;

	ret = find_free_rx_channel(ndev, channel);

	if (ret)
		return ret;

	rx_q = &priv->rx_queue[channel->channel_num];

	channel->desc_addr.desc_virt_addrs_base = dma_alloc_coherent(priv->device, channel->desc_size * channel->desc_cnt,
								&rx_q->dma_rx_phy, flags);

	if (!channel->desc_addr.desc_virt_addrs_base) {
		netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
		goto err_mem;
	}

	rx_q->dma_rx = channel->desc_addr.desc_virt_addrs_base;
	channel->desc_addr.desc_dma_addrs_base = rx_q->dma_rx_phy;

	if (channel->ch_flags == TC956X_CONTIG_BUFS) {
		channel->buff_pool_addr.buff_pool_va_addrs_base[0] = dma_alloc_coherent(priv->device, channel->buf_size * channel->desc_cnt,
								&rx_q->buff_rx_phy, flags);
		if (!channel->buff_pool_addr.buff_pool_va_addrs_base[0]) {
			netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
			goto err_mem;
		}
		channel->buff_pool_addr.buff_pool_dma_addrs_base[0] = rx_q->buff_rx_phy;
		rx_q->buffer_rx_va_addr = channel->buff_pool_addr.buff_pool_va_addrs_base[0];
		return 0;
	}

	rx_q->rx_offload_skbuff_dma = kcalloc(channel->desc_cnt,
				      sizeof(*rx_q->rx_offload_skbuff_dma), flags);

	if (!rx_q->rx_offload_skbuff_dma) {

		netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
		goto err_mem;
	}

	rx_q->rx_offload_skbuff = kcalloc(channel->desc_cnt, sizeof(struct sk_buff *), flags);
	if (!rx_q->rx_offload_skbuff) {
		netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
		goto err_mem;
	}

	for (i = 0; i < channel->desc_cnt; i++) {
		skb = __netdev_alloc_skb_ip_align(priv->dev, channel->buf_size, flags);

		if (!skb) {
			netdev_err(priv->dev,
					"%s: Rx init fails; skb is NULL\n", __func__);
			goto err_mem;
		}

		rx_q->rx_offload_skbuff[i] = skb;
		rx_q->rx_offload_skbuff_dma[i] = dma_map_single(priv->device, skb->data,
							channel->buf_size, DMA_FROM_DEVICE);

		if (dma_mapping_error(priv->device, rx_q->rx_offload_skbuff_dma[i])) {
			netdev_err(priv->dev, "%s: DMA mapping error\n", __func__);
			dev_kfree_skb_any(skb);
			goto err_mem;
		}

		channel->buff_pool_addr.buff_pool_va_addrs_base[i] = (void *)rx_q->rx_offload_skbuff[i]->data;
		channel->buff_pool_addr.buff_pool_dma_addrs_base[i] = rx_q->rx_offload_skbuff_dma[i];

	}

	return 0;

err_mem:
	free_ipa_rx_resources(ndev, channel);
	return -ENOMEM;
}

static void tc956xmac_init_ipa_tx_ch(struct tc956xmac_priv *priv, struct channel_info *channel)
{
	u32 i;
	u32 chan = channel->channel_num;
	struct tc956xmac_tx_queue *tx_q = &priv->tx_queue[chan];

	for (i = 0; i < channel->desc_cnt; i++) {

		struct dma_desc *p;

		p = tx_q->dma_tx + i;

		tc956xmac_clear_desc(priv, p);
		if (channel->ch_flags == TC956X_CONTIG_BUFS) {
			tc956xmac_set_desc_addr(priv, p, (tx_q->buff_tx_phy + (i * channel->buf_size)));
		} else {
			tc956xmac_set_desc_addr(priv, p, channel->buff_pool_addr.buff_pool_dma_addrs_base[i]);
		}
	}

	tc956xmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, chan);
	tc956xmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				tx_q->dma_tx_phy, chan);

	tx_q->tx_tail_addr = tx_q->dma_tx_phy;
	tc956xmac_set_tx_tail_ptr(priv, priv->ioaddr,
				       tx_q->tx_tail_addr, chan);

	tc956xmac_set_tx_ring_len(priv, priv->ioaddr, channel->desc_cnt - 1, chan);

	tc956xmac_set_mtl_tx_queue_weight(priv, priv->hw,
					priv->plat->tx_queues_cfg[chan].weight, chan);

}

static void tc956xmac_init_ipa_rx_ch(struct tc956xmac_priv *priv, struct channel_info *channel)
{
	u32 i;
	u32 chan = channel->channel_num;
	struct tc956xmac_rx_queue *rx_q = &priv->rx_queue[chan];

	for (i = 0; i < channel->desc_cnt; i++) {
		struct dma_desc *p;

		p = rx_q->dma_rx + i;

		tc956xmac_init_rx_desc(priv, &rx_q->dma_rx[i],
					priv->use_riwt, priv->mode,
					(i == channel->desc_cnt - 1),
					channel->buf_size);

		if (channel->ch_flags == TC956X_CONTIG_BUFS) {
			tc956xmac_set_desc_addr(priv, p, (rx_q->buff_rx_phy + (i * channel->buf_size)));
		} else {
			tc956xmac_set_desc_addr(priv, p, channel->buff_pool_addr.buff_pool_dma_addrs_base[i]);
		}
	}

	tc956xmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, chan);
	tc956xmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    rx_q->dma_rx_phy, chan);

	rx_q->rx_tail_addr = rx_q->dma_rx_phy +
				 (channel->desc_cnt * sizeof(struct dma_desc));
	tc956xmac_set_rx_tail_ptr(priv, priv->ioaddr, rx_q->rx_tail_addr, chan);

	tc956xmac_set_rx_ring_len(priv, priv->ioaddr, (channel->desc_cnt - 1), chan);
	tc956xmac_set_dma_bfsize(priv, priv->ioaddr, channel->buf_size, chan);

}

static void dealloc_ipa_tx_resources(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct tc956xmac_tx_queue *tx_q;
	u32 ch = channel->channel_num;

	priv->plat->tx_dma_ch_owner[ch] = NOT_USED;

	tx_q = &priv->tx_queue[ch];
	tx_q->priv_data = NULL;

	free_ipa_tx_resources(ndev, channel);

	tx_q->dma_tx = NULL;

}

static void dealloc_ipa_rx_resources(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv = netdev_priv(ndev);
	struct tc956xmac_rx_queue *rx_q;
	u32 ch = channel->channel_num;

	priv->plat->rx_dma_ch_owner[ch] = NOT_USED;

	rx_q = &priv->rx_queue[ch];
	rx_q->priv_data = NULL;

	free_ipa_rx_resources(ndev, channel);

	rx_q->dma_rx = NULL;
}


/*!
 * \brief API to allocate a channel for IPA  Tx/Rx datapath,
 *	  allocate memory and buffers for the DMA channel, setup the
 *	  descriptors and configure the the require registers and
 *	  mark the channel as used by IPA in the TC956x driver
 *
 *	  The API will check for NULL pointers and Invalid arguments such as,
 *	  out of bounds buf size > 9K bytes, descriptor count > 512
 *
 * \param[in] channel_input : data structure specifying all input needed to request a channel
 *
 * \return channel_info : Allocate memory for channel_info structure and initialize the structure members
 *			  NULL on fail
 * \remarks :In case of Tx, only TDES0 and TDES1 will be updated with buffer addresses. TDES2 and TDES3
 *	    must be updated by the offloading driver.
 */
struct channel_info* request_channel(struct request_channel_input *channel_input)
{
	struct channel_info *channel;
	struct tc956xmac_priv *priv;
	struct tc956xmac_tx_queue *tx_q;
	struct tc956xmac_rx_queue *rx_q;

	if (!channel_input) {
		pr_err("%s: ERROR: Invalid channel_input pointer\n", __func__);
		return NULL;
	}

	if (!channel_input->ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return NULL;
	}

	priv = netdev_priv(channel_input->ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return NULL;
	}

	if (channel_input->desc_cnt > IPA_MAX_DESC_CNT) {
		netdev_err(priv->dev, "%s: ERROR: Descriptor count greater than %d\n", __func__, IPA_MAX_DESC_CNT);
		return NULL;
	}

	if (channel_input->buf_size > IPA_MAX_BUFFER_SIZE) {
		netdev_err(priv->dev, "%s: ERROR: Buffer size greater than %d bytes\n", __func__, IPA_MAX_BUFFER_SIZE);

		return NULL;
	}

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel) {
		netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
		return NULL;
	}

	channel->buf_size = channel_input->buf_size;
	channel->client_ch_priv = channel_input->client_ch_priv;
	channel->desc_cnt = channel_input->desc_cnt;
	channel->desc_size = sizeof(struct dma_desc);
	channel->direction = channel_input->ch_dir;
	channel->mem_ops = channel_input->mem_ops;
	channel->ch_flags = channel_input->ch_flags;

	channel->buff_pool_addr.buff_pool_va_addrs_base = kcalloc(channel_input->desc_cnt, sizeof(void *),
								  (gfp_t)channel_input->flags);

	if (!channel->buff_pool_addr.buff_pool_va_addrs_base) {
		netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
		goto err_buff_mem_alloc;
	}

	channel->buff_pool_addr.buff_pool_dma_addrs_base = kcalloc(channel_input->desc_cnt, sizeof(dma_addr_t),
								   (gfp_t)channel_input->flags);

	if (!channel->buff_pool_addr.buff_pool_dma_addrs_base) {
		netdev_err(priv->dev, "%s: ERROR: allocating memory\n", __func__);
		goto err_buff_dma_mem_alloc;
	}

	channel->dma_pdev = (struct pci_dev *)priv->device;

	if (channel->mem_ops) {
		/*if mem_ops is a valid, memory resrouces will be allocated by IPA */

		if (channel->mem_ops->alloc_descs) {
			channel->mem_ops->alloc_descs(channel_input->ndev, channel->desc_size, NULL,
							    (gfp_t)channel_input->flags,
							    channel->mem_ops, channel);
		} else {
			netdev_err(priv->dev,
				"%s: ERROR: mem_ops is valid but alloc_descs is invalid\n", __func__);
			goto err_invalid_mem_ops;
		}

		if (channel->mem_ops->alloc_buf) {
			channel->mem_ops->alloc_buf(channel_input->ndev, channel->desc_size, NULL,
							  (gfp_t)channel_input->flags,
							  channel->mem_ops, channel);
		} else {
			netdev_err(priv->dev,
				"%s: ERROR: mem_ops is valid but alloc_buff is invalid\n", __func__);

			if (channel->mem_ops->free_descs)
				channel->mem_ops->free_descs(channel_input->ndev, NULL,
								   sizeof(struct dma_desc), NULL,
								   channel->mem_ops, channel);

			goto err_invalid_mem_ops;
		}

		if (channel_input->ch_dir == CH_DIR_TX) {
			find_free_tx_channel(channel_input->ndev, channel);
			tx_q = &priv->tx_queue[channel->channel_num];
			tx_q->dma_tx = channel->desc_addr.desc_virt_addrs_base;
			tx_q->dma_tx_phy = channel->desc_addr.desc_dma_addrs_base;
		} else if (channel_input->ch_dir == CH_DIR_RX) {
			find_free_rx_channel(channel_input->ndev, channel);
			rx_q = &priv->rx_queue[channel->channel_num];
			rx_q->dma_rx = channel->desc_addr.desc_virt_addrs_base;
			rx_q->dma_rx_phy = channel->desc_addr.desc_dma_addrs_base;
		} else {
			netdev_err(priv->dev,
					"%s: ERROR: Invalid channel direction\n", __func__);
			goto err_invalid_ch_dir;
		}
	} else {
		/* Allocate resources for descriptor and buffer */

		if (channel_input->ch_dir == CH_DIR_TX) {
			if (alloc_ipa_tx_resources(channel_input->ndev, channel, (gfp_t)channel_input->flags)) {
				netdev_err(priv->dev,
						"%s: ERROR: allocating Tx resources\n", __func__);
				goto err_invalid_mem_ops;
			}
		} else if (channel_input->ch_dir == CH_DIR_RX) {
			if (alloc_ipa_rx_resources(channel_input->ndev, channel, (gfp_t)channel_input->flags)) {
				netdev_err(priv->dev,
						"%s: ERROR: allocating Rx resources\n", __func__);
				goto err_invalid_mem_ops;
			}
		} else {
			netdev_err(priv->dev,
					"%s: ERROR: Invalid channel direction\n", __func__);

			goto err_invalid_ch_dir;
		}

	}

	/* Configure DMA registers */
	if (channel_input->ch_dir == CH_DIR_TX) {
		tc956xmac_init_ipa_tx_ch(priv, channel);
		tc956xmac_stop_tx(priv, priv->ioaddr, channel->channel_num);
		channel_input->tail_ptr_addr = XGMAC_DMA_CH_TxDESC_TAIL_LPTR(channel->channel_num);
	} else if (channel_input->ch_dir == CH_DIR_RX) {
		tc956xmac_init_ipa_rx_ch(priv, channel);
		tc956xmac_stop_rx(priv, priv->ioaddr, channel->channel_num);
		channel_input->tail_ptr_addr = XGMAC_DMA_CH_RxDESC_TAIL_LPTR(channel->channel_num);
	} else {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel direction\n", __func__);

		goto err_invalid_ch_dir;
	}

	return channel;

err_invalid_ch_dir:
err_invalid_mem_ops:
err_buff_dma_mem_alloc:
	kfree(channel->buff_pool_addr.buff_pool_va_addrs_base);
err_buff_mem_alloc:
	channel->desc_addr.desc_dma_addrs_base = 0;
	kfree(channel);

	return NULL;

}
EXPORT_SYMBOL_GPL(request_channel);


/*!
 * \brief Release the resources associated with the channel
 *	  and mark the channel as free in the TC956x driver,
 *	  reset the descriptors and registers
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer or memory buffers in channel pointer are NULL
 *
 * \remarks : DMA Channel has to be stopped prior to invoking this API
 */
int release_channel(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv;
	struct mem_ops *mem_ops;
	struct tc956xmac_tx_queue *tx_q;
	struct tc956xmac_rx_queue *rx_q;
	u32 ch;
	u32 offload_release_sts = true;

	int ret = -EINVAL;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!channel) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel info structure\n", __func__);
		return -EINVAL;
	}

	if ((!channel->desc_addr.desc_virt_addrs_base) || (!channel->desc_addr.desc_dma_addrs_base) ||
		(!channel->buff_pool_addr.buff_pool_dma_addrs_base) || (!channel->buff_pool_addr.buff_pool_va_addrs_base)) {

		netdev_err(priv->dev,
				"%s: ERROR: Invalid memory pointers\n", __func__);
		return -EINVAL;
	}

	if ((channel->direction == CH_DIR_RX &&
		priv->plat->rx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if ((channel->direction == CH_DIR_TX &&
		priv->plat->tx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	priv = netdev_priv(ndev);
	mem_ops = channel->mem_ops;

	if (mem_ops) {
		if (mem_ops->free_descs) {
			mem_ops->free_descs(ndev, NULL, channel->desc_size, NULL, mem_ops, channel);
			ret = 0;
		} else {
			netdev_err(priv->dev,
					"%s: ERROR: mem_ops is valid but free_descs is invalid\n", __func__);
			ret = -EINVAL;
			goto err_free_desc;
		}

		if (mem_ops->free_buf) {
			mem_ops->free_buf(ndev, NULL, channel->desc_size, NULL, mem_ops, channel);
			ret = 0;
		} else {
			netdev_err(priv->dev,
					"%s: ERROR: mem_ops is valid but free_buf is invalid\n", __func__);
			ret = -EINVAL;
			goto err_free_buff;
		}

		if (channel->direction == CH_DIR_TX) {
			priv->plat->tx_dma_ch_owner[channel->channel_num] = NOT_USED;

			tx_q = &priv->tx_queue[channel->channel_num];
			tx_q->priv_data = NULL;
			tx_q->dma_tx = NULL;
		} else if (channel->direction == CH_DIR_RX) {
			priv->plat->rx_dma_ch_owner[channel->channel_num] = NOT_USED;

			rx_q = &priv->rx_queue[channel->channel_num];
			rx_q->priv_data = NULL;
			rx_q->dma_rx = NULL;
		} else {
			netdev_err(priv->dev,
					"%s: ERROR: Invalid channel direction\n", __func__);
			ret = -EINVAL;
			goto err_invalid_ch_dir;
		}

	} else {

		if (channel->direction == CH_DIR_TX) {
			tc956xmac_stop_tx(priv, priv->ioaddr, channel->channel_num);
			dealloc_ipa_tx_resources(ndev, channel);
			ret = 0;
		} else if (channel->direction == CH_DIR_RX) {
			tc956xmac_stop_rx(priv, priv->ioaddr, channel->channel_num);
			dealloc_ipa_rx_resources(ndev, channel);
			ret = 0;
		} else {
			netdev_err(priv->dev,
					"%s: ERROR: Invalid channel direction\n", __func__);
			ret = -EINVAL;
			goto err_invalid_ch_dir;
		}
	}

	mutex_lock(&priv->port_ld_release_lock);
	/* Checking whether any Tx channel enabled for offload or not*/
	for (ch = 0; ch < MAX_TX_QUEUES_TO_USE; ch++) {
		/* If offload channels are not freed, update the flag, so that power saving API will not be called*/
		if (priv->plat->tx_dma_ch_owner[ch] == USE_IN_OFFLOADER) {
			offload_release_sts = false;
			break;
		}
	}
	/* Checking whether any Rx channel enabled for offload or not*/
	for (ch = 0; ch < MAX_RX_QUEUES_TO_USE; ch++) {
		/* If offload channels are not freed, update the flag, so that power saving API will not be called*/
		if (priv->plat->rx_dma_ch_owner[ch] == USE_IN_OFFLOADER) {
			offload_release_sts = false;
			break;
		}
	}

	/* If all channels are freed, call API for power saving*/
	if(priv->port_release == true && offload_release_sts == true) {
		tc956xmac_link_change_set_power(priv, LINK_DOWN); /* Save, Assert and Disable Reset and Clock */
	}
	mutex_unlock(&priv->port_ld_release_lock);

err_free_desc:
err_free_buff:
err_invalid_ch_dir:
	kfree(channel->buff_pool_addr.buff_pool_va_addrs_base);
	channel->desc_addr.desc_dma_addrs_base = 0;
	kfree(channel);

	return ret;


}
EXPORT_SYMBOL_GPL(release_channel);


/*!
 * \brief Update the location in CM3 SRAM with a PCIe Write Address and
 *	  value for the associated channel. When Tx/Rx interrupts occur,
 *	  the FW will write the value to the PCIe location
 *
 *	  The API will check for NULL pointers and Invalid arguments such as,
 *	  non IPA channel, out of range CM3 accesesible PCIe address
 *
 * \param[in] ndev : TC956x netdev  data structure
 * \param[in] channel : Pointer to channel info containing the channel information
 * \param[in] addr : PCIe Address location to which the PCIe write is to be performed from CM3 FW
 *
 * \return : O for success
 *	     -EPERM if non IPA channels are accessed, out of range PCIe access location for CM3
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 *
 * \remarks :
 *	     If this API is invoked for a channel without calling release_event(),
 *	     then the PCIe address and value for that channel will be overwritten
 *	     Mask = 2 ^ (CM3_TAMAP_ATR_SIZE + 1) - 1
 *	     TRSL_ADDR = DMA_PCIe_ADDR & ~((2 ^ (ATR_SIZE + 1) - 1) = TRSL_ADDR = DMA_PCIe_ADDR & ~Mask
 *	     CM3 Target Address = DMA_PCIe_ADDR & Mask | SRC_ADDR
 */
int request_event(struct net_device *ndev, struct channel_info *channel, dma_addr_t addr)
{
	struct tc956xmac_priv *priv;
	u8 table_entry = 1; /* Table entry 0 is for eMAC */
	struct tc956xmac_cm3_tamap tamap;
	u32 val, cm3_target_addr;
	dma_addr_t trsl_addr;
	unsigned long flags;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!channel) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel info structure\n", __func__);
		return -EINVAL;
	}

	if ((channel->direction == CH_DIR_RX &&
		priv->plat->rx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if ((channel->direction == CH_DIR_TX &&
		priv->plat->tx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	spin_lock_irqsave(&cm3_tamap_lock, flags);
	while (table_entry <= MAX_CM3_TAMAP_ENTRIES) {
		val = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry));

		KPRINT_INFO("SL0%d TRSL_ADDR HI = 0x%08x\n", table_entry,
			readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + TC956X_AXI4_SLV_TRSL_ADDR_HI(0, table_entry)));
		KPRINT_INFO("SL0%d TRSL_ADDR LO = 0x%08x\n", table_entry,
			readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + TC956X_AXI4_SLV_TRSL_ADDR_LO(0, table_entry)));
		KPRINT_INFO("SL0%d SRC_ADDR HI = 0x%08x\n", table_entry,
			readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_HI(0, table_entry)));
		KPRINT_INFO("SL0%d SRC_ADDR LO = 0x%08x\n", table_entry,
			readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry)));

		if (((val & TC956X_ATR_SIZE_MASK) >> TC956x_ATR_SIZE_SHIFT) != 0x3F) {
			/* If a entry already exists, then check the range */
			trsl_addr = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + TC956X_AXI4_SLV_TRSL_ADDR_LO(0, table_entry));
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
			trsl_addr |= (dma_addr_t)readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + TC956X_AXI4_SLV_TRSL_ADDR_HI(0, table_entry)) << 32U;
#endif
			if ((trsl_addr <= addr) && (addr <= (trsl_addr + CM3_TAMAP_SIZE - 1))) {
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
				netdev_info(priv->dev, "TAMAP Table %d manages address = 0x%x\n", table_entry, addr);
#else
				netdev_info(priv->dev, "TAMAP Table %d manages address = 0x%llx\n", table_entry, addr);
#endif
				break;
			}
		} else {

			/* Create a new tamap entry */
			tamap.src_addr_hi = 0x0;
			tamap.src_addr_low = CM3_TAMAP_SRC_ADDR_START + (CM3_TAMAP_SIZE * (table_entry - 1));
			trsl_addr = addr & ~CM3_TAMAP_MASK;
			tamap.trsl_addr_hi = upper_32_bits(trsl_addr);
			tamap.trsl_addr_low = lower_32_bits(trsl_addr);
			tamap.atr_size = CM3_TAMAP_ATR_SIZE;

			tc956x_config_CM3_tamap(priv->device, priv->tc956x_BRIDGE_CFG_pci_base_addr,
							&tamap, table_entry);
			break;
		}
		table_entry++;
	}
	spin_unlock_irqrestore(&cm3_tamap_lock, flags);

	if (table_entry > MAX_CM3_TAMAP_ENTRIES) {
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
		netdev_err(priv->dev,
				"%s: ERROR: TAMAP Table full. Cannot map PCIe address = 0x%x\n", __func__, addr);
#else
		netdev_err(priv->dev,
				"%s: ERROR: TAMAP Table full. Cannot map PCIe address = 0x%llx\n", __func__, addr);
#endif
		return -EPERM;
	}

	/* Derive CM3 target address */
	cm3_target_addr = readl(priv->tc956x_BRIDGE_CFG_pci_base_addr + TC956X_AXI4_SLV_SRC_ADDR_LO(0, table_entry)) & TC956X_SRC_LO_MASK;
	cm3_target_addr |= (addr & CM3_TAMAP_MASK);

	if (cm3_target_addr < CM3_PCIE_REGION_LOW_BOUND || cm3_target_addr >= CM3_PCIE_REGION_UP_BOUND) {
		netdev_err(priv->dev,
				"%s: ERROR: PCIe address out of range\n", __func__);
		return -EPERM;
	}

	if (channel->direction == CH_DIR_TX) {
#ifdef TC956X
		writel(cm3_target_addr, priv->tc956x_SRAM_pci_base_addr +
			SRAM_TX_PCIE_ADDR_LOC + (priv->port_num * TC956XMAC_CH_MAX * 4) +
			(channel->channel_num * 4));
#endif
	} else if (channel->direction == CH_DIR_RX) {
#ifdef TC956X
		writel(cm3_target_addr, priv->tc956x_SRAM_pci_base_addr +
			SRAM_RX_PCIE_ADDR_LOC + (priv->port_num * TC956XMAC_CH_MAX * 4) +
			(channel->channel_num * 4));
#endif
	} else {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel direction\n", __func__);
		return -EINVAL;

	}
	return 0;
}
EXPORT_SYMBOL_GPL(request_event);


/*!
 * \brief Update the location in CM3 SRAM with a PCIe Write Address and
 *	  value for the associated channel to zero
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 */
int release_event(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!channel) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel info structure\n", __func__);
		return -EINVAL;
	}

	if ((channel->direction == CH_DIR_RX &&
		priv->plat->rx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if ((channel->direction == CH_DIR_TX &&
		priv->plat->tx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if (channel->direction == CH_DIR_TX) {
#ifdef TC956X
		writel(0, priv->tc956x_SRAM_pci_base_addr +
			SRAM_TX_PCIE_ADDR_LOC + (priv->port_num * TC956XMAC_CH_MAX * 4) +
			(channel->channel_num * 4));
#endif

	} else if (channel->direction == CH_DIR_RX) {
#ifdef TC956X
		writel(0, priv->tc956x_SRAM_pci_base_addr +
			SRAM_RX_PCIE_ADDR_LOC + (priv->port_num * TC956XMAC_CH_MAX * 4) +
			(channel->channel_num * 4));
#endif
	} else {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel direction\n", __func__);
		return -EINVAL;

	}
	return 0;
}
EXPORT_SYMBOL_GPL(release_event);


/*!
 * \brief Enable interrupt generation for given channel
 *
 * The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 */
int enable_event(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv;
	u32 reg;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!channel) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel info structure\n", __func__);
		return -EINVAL;
	}

	if ((channel->direction == CH_DIR_RX &&
		priv->plat->rx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if ((channel->direction == CH_DIR_TX &&
		priv->plat->tx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if (channel->direction == CH_DIR_TX) {
#ifdef TC956X
		if (priv->port_num == RM_PF0_ID) {
			reg = readl(priv->ioaddr + INTMCUMASK0);
			reg &= ~(1 << (INTMCUMASK_TX_CH0 + channel->channel_num));
			writel(reg, priv->ioaddr + INTMCUMASK0);
		}

		if (priv->port_num == RM_PF1_ID) {
			reg = readl(priv->ioaddr + INTMCUMASK1);
			reg &= ~(1 << (INTMCUMASK_TX_CH0 + channel->channel_num));
			writel(reg, priv->ioaddr + INTMCUMASK1);
		}
#endif

	} else if (channel->direction == CH_DIR_RX) {
#ifdef TC956X
		if (priv->port_num == RM_PF0_ID) {
			reg = readl(priv->ioaddr + INTMCUMASK0);
			reg &= ~(1 << (INTMCUMASK_RX_CH0 + channel->channel_num));
			writel(reg, priv->ioaddr + INTMCUMASK0);
		}

		if (priv->port_num == RM_PF1_ID) {
			reg = readl(priv->ioaddr + INTMCUMASK1);
			reg &= ~(1 << (INTMCUMASK_RX_CH0 + channel->channel_num));
			writel(reg, priv->ioaddr + INTMCUMASK1);
		}
#endif

	} else {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel direction\n", __func__);
		return -EINVAL;

	}
	return 0;
}
EXPORT_SYMBOL_GPL(enable_event);

/*!
 * \brief Disable interrupt generation for given channel
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 */
int disable_event(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv;
	u32 reg;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!channel) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel info structure\n", __func__);
		return -EINVAL;
	}

	if ((channel->direction == CH_DIR_RX &&
		priv->plat->rx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if ((channel->direction == CH_DIR_TX &&
		priv->plat->tx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if (channel->direction == CH_DIR_TX) {
		if (priv->port_num == RM_PF0_ID) {
			reg = readl(priv->ioaddr + INTMCUMASK0);
			reg |= (1 << (INTMCUMASK_TX_CH0 + channel->channel_num));
			writel(reg, priv->ioaddr + INTMCUMASK0);
		}

		if (priv->port_num == RM_PF1_ID) {
			reg = readl(priv->ioaddr + INTMCUMASK1);
			reg |= (1 << (INTMCUMASK_TX_CH0 + channel->channel_num));
			writel(reg, priv->ioaddr + INTMCUMASK1);
		}

	} else if (channel->direction == CH_DIR_RX) {
		if (priv->port_num == RM_PF0_ID) {
			reg = readl(priv->ioaddr + INTMCUMASK0);
			reg |= (1 << (INTMCUMASK_RX_CH0 + channel->channel_num));
			writel(reg, priv->ioaddr + INTMCUMASK0);
		}

		if (priv->port_num == RM_PF1_ID) {
			reg = readl(priv->ioaddr + INTMCUMASK1);
			reg |= (1 << (INTMCUMASK_RX_CH0 + channel->channel_num));
			writel(reg, priv->ioaddr + INTMCUMASK1);
		}

	} else {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel direction\n", __func__);
		return -EINVAL;

	}
	return 0;
}
EXPORT_SYMBOL_GPL(disable_event);

/*!
 * \brief Control the Rx DMA interrupt generation by modfying the Rx WDT timer
 *
 *	  The API will check for NULL pointers and Invalid arguments such as,
 *	  non IPA channel, event moderation for Tx path
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 * \param[in] wdt : Watchdog timeout value in clock cycles
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed, IPA Tx channel
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 */
int set_event_mod(struct net_device *ndev, struct channel_info *channel, unsigned int wdt)
{
	struct tc956xmac_priv *priv;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!channel) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel info structure\n", __func__);
		return -EINVAL;
	}

	if ((priv->plat->rx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER) ||
		channel->direction == CH_DIR_TX) {

		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
		return -EPERM;
	}

	if (wdt > MAX_WDT) {
		netdev_err(priv->dev,
				"%s: ERROR: Timeout value Out of range\n", __func__);
		return -EINVAL;
	}
#ifdef TC956X
	writel(wdt & XGMAC_RWT, priv->ioaddr + XGMAC_DMA_CH_Rx_WATCHDOG(channel->channel_num));
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(set_event_mod);

/*!
 * \brief This API will configure the FRP table with the parameters passed through rx_filter_info.
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel,
 *	  number of filter entries > 72
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] filter_params: filter_params containig the parameters based on which packet will pass or drop
 * \return : Return 0 on success, -ve value on error
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL filter_params, if number of entries > 72
 *
 * \remarks : The entries should be prepared considering the filtering and routing to CortexA also
 *	      MAC Rx will be stopped while updating FRP table dynamically.
 */
int set_rx_filter(struct net_device *ndev, struct rx_filter_info *filter_params)
{
	struct tc956xmac_priv *priv;
	struct tc956xmac_rx_parser_cfg *cfg;
	u32 ret = -EINVAL;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!filter_params) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid filter parameters structure\n", __func__);
		return -EINVAL;
	}

	if (filter_params->npe > MAX_PARSABLE_FRP_ENTRIES) {
		netdev_err(priv->dev,
				"%s: ERROR: No. of FRP entries exceed maximum parsable entries of %d\n",
				__func__, MAX_PARSABLE_FRP_ENTRIES);
		return -EINVAL;
	}

	cfg = &priv->plat->rxp_cfg;

	cfg->nve = filter_params->nve;
	cfg->npe = filter_params->npe;
	memcpy(cfg->entries, filter_params->entries, filter_params->nve * sizeof(filter_params->entries[0]));

	priv->plat->rxp_cfg.enable = true;
	ret = tc956xmac_rx_parser_configuration(priv);

	return ret;

}
EXPORT_SYMBOL_GPL(set_rx_filter);

/*!
 * \brief This API will clear the FRP filters and route all packets to RxCh0
 *
 *	 The API will check for NULL pointers
 *
 * \param[in] ndev : TC956x netdev data structure
 * \return : Return 0 on success, -ve value on error
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *
 * \remarks : MAC Rx will be stopped while updating FRP table dynamically.

 */
int clear_rx_filter(struct net_device *ndev)
{
	struct tc956xmac_priv *priv;
	struct tc956xmac_rx_parser_cfg *cfg;
	struct rxp_filter_entry filter_entries;
	u32 ret = -EINVAL;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}
#ifdef TC956X
	/* Create FRP entries to route all packets to RxCh0 */
	filter_entries.match_data = 0x00000000;
	filter_entries.match_en = 0x00000000;
	filter_entries.af = 1;
	filter_entries.rf = 0;
	filter_entries.im = 0;
	filter_entries.nc = 0;
	filter_entries.res1 = 0;
	filter_entries.frame_offset = 0;
	filter_entries.res2 = 0;
	filter_entries.ok_index = 0;
	filter_entries.res3 = 0;
	filter_entries.dma_ch_no = 1;
	filter_entries.res4 = 0;
#endif

	cfg = &priv->plat->rxp_cfg;

	cfg->nve = 1;
	cfg->npe = 1;

	memcpy(cfg->entries, &filter_entries, cfg->nve * sizeof(struct rxp_filter_entry));

	ret = tc956xmac_rx_parser_configuration(priv);

	return ret;
}
EXPORT_SYMBOL_GPL(clear_rx_filter);

/*!
 * \brief Start the DMA channel. channel_dir member variable
 *	  will be used to start the Tx/Rx channel
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL

 */
int start_channel(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv;
	struct mac_addr_list mac_addr;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!channel) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel info structure\n", __func__);
		return -EINVAL;
	}

	if ((channel->direction == CH_DIR_RX &&
		priv->plat->rx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if ((channel->direction == CH_DIR_TX &&
		priv->plat->tx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if (channel->direction == CH_DIR_TX) {
		netdev_dbg(priv->dev, "DMA Tx process started in channel = %d\n", channel->channel_num);
		tc956xmac_start_tx(priv, priv->ioaddr, channel->channel_num);
	} else if (channel->direction == CH_DIR_RX) {
		mac_addr.ae = MAC_ADDR_AE;
		mac_addr.mbc = MAC_ADDR_MBC;
		mac_addr.dcs = MAC_ADDR_DCS;
		memcpy(&mac_addr.addr[0], &mac_addr_default[0], sizeof(mac_addr_default));
		set_mac_addr(ndev, &mac_addr, MAC_ADDR_INDEX);

		netdev_dbg(priv->dev, "DMA Rx process started in channel = %d\n", channel->channel_num);
		tc956xmac_start_rx(priv, priv->ioaddr, channel->channel_num);
	} else {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
		return -EPERM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(start_channel);

/*!
 * \brief Stop the DMA channel. channel_dir member variable will be
 *	  used to stop the Tx/Rx channel. In case of Rx, clear the
 *	  MTL queue associated with the channel and this will result in packet drops
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer  NULL
 */
int stop_channel(struct net_device *ndev, struct channel_info *channel)
{
	struct tc956xmac_priv *priv;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!channel) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel info structure\n", __func__);
		return -EINVAL;
	}

	if ((channel->direction == CH_DIR_RX &&
		priv->plat->rx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if ((channel->direction == CH_DIR_TX &&
		priv->plat->tx_dma_ch_owner[channel->channel_num] != USE_IN_OFFLOADER)) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
			return -EPERM;
	}

	if (channel->direction == CH_DIR_TX) {
		netdev_dbg(priv->dev, "DMA Tx process stopped in channel = %d\n", channel->channel_num);
		tc956xmac_stop_tx(priv, priv->ioaddr, channel->channel_num);
	} else if (channel->direction == CH_DIR_RX) {
		netdev_dbg(priv->dev, "DMA Rx process stopped in channel = %d\n", channel->channel_num);
		tc956xmac_stop_rx(priv, priv->ioaddr, channel->channel_num);
	} else {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid channel\n", __func__);
		return -EPERM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(stop_channel);

/*!
 * \brief Configure MAC registers at a particular index in the MAC Address list
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] mac_addr : Pointer to structure containing mac_addr_list that needs to updated
 *		     in MAC_Address_High and MAC_Address_Low registers
 * \param[in] index : Index in the MAC Address Register list
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if index 0 used
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if mac_addr NULL
 *
 * \remarks : Do not use the API to set register at index 0.
 *	      There is possibilty of kernel network subsytem overwriting these registers
 *	      when " tc956xmac_set_rx_mode" is invoked via "ndo_set_rx_mode" callback.
 */
int set_mac_addr(struct net_device *ndev, struct mac_addr_list *mac_addr, u8 index)
{
	struct tc956xmac_priv *priv;
	u32 data;

	if (!ndev) {
		pr_err("%s: ERROR: Invalid netdevice pointer\n", __func__);
		return -ENODEV;
	}

	priv = netdev_priv(ndev);
	if (!priv) {
		pr_err("%s: ERROR: Invalid private data pointer\n", __func__);
		return -ENODEV;
	}

	if (!mac_addr) {
		netdev_err(priv->dev,
				"%s: ERROR: Invalid mac addr list structure\n", __func__);
		return -EINVAL;
	}

	if (index == 0) {
		netdev_err(priv->dev,
				"%s: ERROR: Do not use index 0\n", __func__);
		return -EPERM;
	}

	if (index >= TC956X_MAX_PERFECT_ADDRESSES) {
		netdev_err(priv->dev,
				"%s: ERROR: Index out of range\n", __func__);
		return -EPERM;
	}

	data = (mac_addr->addr[5] << 8) | (mac_addr->addr[4]) |
		(mac_addr->ae << XGMAC_AE_SHIFT) | (mac_addr->mbc << XGMAC_MBC_SHIFT);
	writel(data, priv->ioaddr + XGMAC_ADDRx_HIGH(index));

	data = (mac_addr->addr[3] << 24) | (mac_addr->addr[2] << 16) |
		(mac_addr->addr[1] << 8) | mac_addr->addr[0];
	writel(data, priv->ioaddr + XGMAC_ADDRx_LOW(index));

	return 0;
}
EXPORT_SYMBOL_GPL(set_mac_addr);

