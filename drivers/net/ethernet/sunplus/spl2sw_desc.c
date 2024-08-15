// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>

#include "spl2sw_define.h"
#include "spl2sw_desc.h"

void spl2sw_rx_descs_flush(struct spl2sw_common *comm)
{
	struct spl2sw_skb_info *rx_skbinfo;
	struct spl2sw_mac_desc *rx_desc;
	u32 i, j;

	for (i = 0; i < RX_DESC_QUEUE_NUM; i++) {
		rx_desc = comm->rx_desc[i];
		rx_skbinfo = comm->rx_skb_info[i];
		for (j = 0; j < comm->rx_desc_num[i]; j++) {
			rx_desc[j].addr1 = rx_skbinfo[j].mapping;
			rx_desc[j].cmd2 = (j == comm->rx_desc_num[i] - 1) ?
					  RXD_EOR | comm->rx_desc_buff_size :
					  comm->rx_desc_buff_size;
			wmb();	/* Set RXD_OWN after other fields are ready. */
			rx_desc[j].cmd1 = RXD_OWN;
		}
	}
}

void spl2sw_tx_descs_clean(struct spl2sw_common *comm)
{
	u32 i;

	if (!comm->tx_desc)
		return;

	for (i = 0; i < TX_DESC_NUM; i++) {
		comm->tx_desc[i].cmd1 = 0;
		wmb();	/* Clear TXD_OWN and then set other fields. */
		comm->tx_desc[i].cmd2 = 0;
		comm->tx_desc[i].addr1 = 0;
		comm->tx_desc[i].addr2 = 0;

		if (comm->tx_temp_skb_info[i].mapping) {
			dma_unmap_single(&comm->pdev->dev, comm->tx_temp_skb_info[i].mapping,
					 comm->tx_temp_skb_info[i].skb->len, DMA_TO_DEVICE);
			comm->tx_temp_skb_info[i].mapping = 0;
		}

		if (comm->tx_temp_skb_info[i].skb) {
			dev_kfree_skb_any(comm->tx_temp_skb_info[i].skb);
			comm->tx_temp_skb_info[i].skb = NULL;
		}
	}
}

void spl2sw_rx_descs_clean(struct spl2sw_common *comm)
{
	struct spl2sw_skb_info *rx_skbinfo;
	struct spl2sw_mac_desc *rx_desc;
	u32 i, j;

	for (i = 0; i < RX_DESC_QUEUE_NUM; i++) {
		if (!comm->rx_skb_info[i])
			continue;

		rx_desc = comm->rx_desc[i];
		rx_skbinfo = comm->rx_skb_info[i];
		for (j = 0; j < comm->rx_desc_num[i]; j++) {
			rx_desc[j].cmd1 = 0;
			wmb();	/* Clear RXD_OWN and then set other fields. */
			rx_desc[j].cmd2 = 0;
			rx_desc[j].addr1 = 0;

			if (rx_skbinfo[j].skb) {
				dma_unmap_single(&comm->pdev->dev, rx_skbinfo[j].mapping,
						 comm->rx_desc_buff_size, DMA_FROM_DEVICE);
				dev_kfree_skb_any(rx_skbinfo[j].skb);
				rx_skbinfo[j].skb = NULL;
				rx_skbinfo[j].mapping = 0;
			}
		}

		kfree(rx_skbinfo);
		comm->rx_skb_info[i] = NULL;
	}
}

void spl2sw_descs_clean(struct spl2sw_common *comm)
{
	spl2sw_rx_descs_clean(comm);
	spl2sw_tx_descs_clean(comm);
}

void spl2sw_descs_free(struct spl2sw_common *comm)
{
	u32 i;

	spl2sw_descs_clean(comm);
	comm->tx_desc = NULL;
	for (i = 0; i < RX_DESC_QUEUE_NUM; i++)
		comm->rx_desc[i] = NULL;

	/*  Free descriptor area  */
	if (comm->desc_base) {
		dma_free_coherent(&comm->pdev->dev, comm->desc_size, comm->desc_base,
				  comm->desc_dma);
		comm->desc_base = NULL;
		comm->desc_dma = 0;
		comm->desc_size = 0;
	}
}

void spl2sw_tx_descs_init(struct spl2sw_common *comm)
{
	memset(comm->tx_desc, '\0', sizeof(struct spl2sw_mac_desc) *
	       (TX_DESC_NUM + MAC_GUARD_DESC_NUM));
}

int spl2sw_rx_descs_init(struct spl2sw_common *comm)
{
	struct spl2sw_skb_info *rx_skbinfo;
	struct spl2sw_mac_desc *rx_desc;
	struct sk_buff *skb;
	u32 mapping;
	u32 i, j;

	for (i = 0; i < RX_DESC_QUEUE_NUM; i++) {
		comm->rx_skb_info[i] = kcalloc(comm->rx_desc_num[i], sizeof(*rx_skbinfo),
					       GFP_KERNEL | GFP_DMA);
		if (!comm->rx_skb_info[i])
			goto mem_alloc_fail;

		rx_skbinfo = comm->rx_skb_info[i];
		rx_desc = comm->rx_desc[i];
		for (j = 0; j < comm->rx_desc_num[i]; j++) {
			skb = netdev_alloc_skb(NULL, comm->rx_desc_buff_size);
			if (!skb)
				goto mem_alloc_fail;

			rx_skbinfo[j].skb = skb;
			mapping = dma_map_single(&comm->pdev->dev, skb->data,
						 comm->rx_desc_buff_size,
						 DMA_FROM_DEVICE);
			if (dma_mapping_error(&comm->pdev->dev, mapping))
				goto mem_alloc_fail;

			rx_skbinfo[j].mapping = mapping;
			rx_desc[j].addr1 = mapping;
			rx_desc[j].addr2 = 0;
			rx_desc[j].cmd2 = (j == comm->rx_desc_num[i] - 1) ?
					  RXD_EOR | comm->rx_desc_buff_size :
					  comm->rx_desc_buff_size;
			wmb();	/* Set RXD_OWN after other fields are effective. */
			rx_desc[j].cmd1 = RXD_OWN;
		}
	}

	return 0;

mem_alloc_fail:
	spl2sw_rx_descs_clean(comm);
	return -ENOMEM;
}

int spl2sw_descs_alloc(struct spl2sw_common *comm)
{
	s32 desc_size;
	u32 i;

	/* Alloc descriptor area  */
	desc_size = (TX_DESC_NUM + MAC_GUARD_DESC_NUM) * sizeof(struct spl2sw_mac_desc);
	for (i = 0; i < RX_DESC_QUEUE_NUM; i++)
		desc_size += comm->rx_desc_num[i] * sizeof(struct spl2sw_mac_desc);

	comm->desc_base = dma_alloc_coherent(&comm->pdev->dev, desc_size, &comm->desc_dma,
					     GFP_KERNEL);
	if (!comm->desc_base)
		return -ENOMEM;

	comm->desc_size = desc_size;

	/* Setup Tx descriptor */
	comm->tx_desc = comm->desc_base;

	/* Setup Rx descriptor */
	comm->rx_desc[0] = &comm->tx_desc[TX_DESC_NUM + MAC_GUARD_DESC_NUM];
	for (i = 1; i < RX_DESC_QUEUE_NUM; i++)
		comm->rx_desc[i] = comm->rx_desc[i - 1] + comm->rx_desc_num[i - 1];

	return 0;
}

int spl2sw_descs_init(struct spl2sw_common *comm)
{
	u32 i, ret;

	/* Initialize rx descriptor's data */
	comm->rx_desc_num[0] = RX_QUEUE0_DESC_NUM;
	comm->rx_desc_num[1] = RX_QUEUE1_DESC_NUM;

	for (i = 0; i < RX_DESC_QUEUE_NUM; i++) {
		comm->rx_desc[i] = NULL;
		comm->rx_skb_info[i] = NULL;
		comm->rx_pos[i] = 0;
	}
	comm->rx_desc_buff_size = MAC_RX_LEN_MAX;

	/* Initialize tx descriptor's data */
	comm->tx_done_pos = 0;
	comm->tx_desc = NULL;
	comm->tx_pos = 0;
	comm->tx_desc_full = 0;
	for (i = 0; i < TX_DESC_NUM; i++)
		comm->tx_temp_skb_info[i].skb = NULL;

	/* Allocate tx & rx descriptors. */
	ret = spl2sw_descs_alloc(comm);
	if (ret)
		return ret;

	spl2sw_tx_descs_init(comm);

	return spl2sw_rx_descs_init(comm);
}
