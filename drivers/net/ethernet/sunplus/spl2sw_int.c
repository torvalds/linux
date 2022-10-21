// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>
#include <linux/spinlock.h>
#include <linux/of_mdio.h>

#include "spl2sw_register.h"
#include "spl2sw_define.h"
#include "spl2sw_int.h"

int spl2sw_rx_poll(struct napi_struct *napi, int budget)
{
	struct spl2sw_common *comm = container_of(napi, struct spl2sw_common, rx_napi);
	struct spl2sw_mac_desc *desc, *h_desc;
	struct net_device_stats *stats;
	struct sk_buff *skb, *new_skb;
	struct spl2sw_skb_info *sinfo;
	int budget_left = budget;
	unsigned long flags;
	u32 rx_pos, pkg_len;
	u32 num, rx_count;
	s32 queue;
	u32 mask;
	int port;
	u32 cmd;
	u32 len;

	/* Process high-priority queue and then low-priority queue. */
	for (queue = 0; queue < RX_DESC_QUEUE_NUM; queue++) {
		rx_pos = comm->rx_pos[queue];
		rx_count = comm->rx_desc_num[queue];

		for (num = 0; num < rx_count && budget_left; num++) {
			sinfo = comm->rx_skb_info[queue] + rx_pos;
			desc = comm->rx_desc[queue] + rx_pos;
			cmd = desc->cmd1;

			if (cmd & RXD_OWN)
				break;

			port = FIELD_GET(RXD_PKT_SP, cmd);
			if (port < MAX_NETDEV_NUM && comm->ndev[port])
				stats = &comm->ndev[port]->stats;
			else
				goto spl2sw_rx_poll_rec_err;

			pkg_len = FIELD_GET(RXD_PKT_LEN, cmd);
			if (unlikely((cmd & RXD_ERR_CODE) || pkg_len < ETH_ZLEN + 4)) {
				stats->rx_length_errors++;
				stats->rx_dropped++;
				goto spl2sw_rx_poll_rec_err;
			}

			dma_unmap_single(&comm->pdev->dev, sinfo->mapping,
					 comm->rx_desc_buff_size, DMA_FROM_DEVICE);

			skb = sinfo->skb;
			skb_put(skb, pkg_len - 4); /* Minus FCS */
			skb->ip_summed = CHECKSUM_NONE;
			skb->protocol = eth_type_trans(skb, comm->ndev[port]);
			len = skb->len;
			netif_receive_skb(skb);

			stats->rx_packets++;
			stats->rx_bytes += len;

			/* Allocate a new skb for receiving. */
			new_skb = netdev_alloc_skb(NULL, comm->rx_desc_buff_size);
			if (unlikely(!new_skb)) {
				desc->cmd2 = (rx_pos == comm->rx_desc_num[queue] - 1) ?
					     RXD_EOR : 0;
				sinfo->skb = NULL;
				sinfo->mapping = 0;
				desc->addr1 = 0;
				goto spl2sw_rx_poll_alloc_err;
			}

			sinfo->mapping = dma_map_single(&comm->pdev->dev, new_skb->data,
							comm->rx_desc_buff_size,
							DMA_FROM_DEVICE);
			if (dma_mapping_error(&comm->pdev->dev, sinfo->mapping)) {
				dev_kfree_skb_irq(new_skb);
				desc->cmd2 = (rx_pos == comm->rx_desc_num[queue] - 1) ?
					     RXD_EOR : 0;
				sinfo->skb = NULL;
				sinfo->mapping = 0;
				desc->addr1 = 0;
				goto spl2sw_rx_poll_alloc_err;
			}

			sinfo->skb = new_skb;
			desc->addr1 = sinfo->mapping;

spl2sw_rx_poll_rec_err:
			desc->cmd2 = (rx_pos == comm->rx_desc_num[queue] - 1) ?
				     RXD_EOR | comm->rx_desc_buff_size :
				     comm->rx_desc_buff_size;

			wmb();	/* Set RXD_OWN after other fields are effective. */
			desc->cmd1 = RXD_OWN;

spl2sw_rx_poll_alloc_err:
			/* Move rx_pos to next position */
			rx_pos = ((rx_pos + 1) == comm->rx_desc_num[queue]) ? 0 : rx_pos + 1;

			budget_left--;

			/* If there are packets in high-priority queue,
			 * stop processing low-priority queue.
			 */
			if (queue == 1 && !(h_desc->cmd1 & RXD_OWN))
				break;
		}

		comm->rx_pos[queue] = rx_pos;

		/* Save pointer to last rx descriptor of high-priority queue. */
		if (queue == 0)
			h_desc = comm->rx_desc[queue] + rx_pos;
	}

	spin_lock_irqsave(&comm->int_mask_lock, flags);
	mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
	mask &= ~MAC_INT_RX;
	writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
	spin_unlock_irqrestore(&comm->int_mask_lock, flags);

	napi_complete(napi);
	return budget - budget_left;
}

int spl2sw_tx_poll(struct napi_struct *napi, int budget)
{
	struct spl2sw_common *comm = container_of(napi, struct spl2sw_common, tx_napi);
	struct spl2sw_skb_info *skbinfo;
	struct net_device_stats *stats;
	int budget_left = budget;
	unsigned long flags;
	u32 tx_done_pos;
	u32 mask;
	u32 cmd;
	int i;

	spin_lock(&comm->tx_lock);

	tx_done_pos = comm->tx_done_pos;
	while (((tx_done_pos != comm->tx_pos) || (comm->tx_desc_full == 1)) && budget_left) {
		cmd = comm->tx_desc[tx_done_pos].cmd1;
		if (cmd & TXD_OWN)
			break;

		skbinfo = &comm->tx_temp_skb_info[tx_done_pos];
		if (unlikely(!skbinfo->skb))
			goto spl2sw_tx_poll_next;

		i = ffs(FIELD_GET(TXD_VLAN, cmd)) - 1;
		if (i < MAX_NETDEV_NUM && comm->ndev[i])
			stats = &comm->ndev[i]->stats;
		else
			goto spl2sw_tx_poll_unmap;

		if (unlikely(cmd & (TXD_ERR_CODE))) {
			stats->tx_errors++;
		} else {
			stats->tx_packets++;
			stats->tx_bytes += skbinfo->len;
		}

spl2sw_tx_poll_unmap:
		dma_unmap_single(&comm->pdev->dev, skbinfo->mapping, skbinfo->len,
				 DMA_TO_DEVICE);
		skbinfo->mapping = 0;
		dev_kfree_skb_irq(skbinfo->skb);
		skbinfo->skb = NULL;

spl2sw_tx_poll_next:
		/* Move tx_done_pos to next position */
		tx_done_pos = ((tx_done_pos + 1) == TX_DESC_NUM) ? 0 : tx_done_pos + 1;

		if (comm->tx_desc_full == 1)
			comm->tx_desc_full = 0;

		budget_left--;
	}

	comm->tx_done_pos = tx_done_pos;
	if (!comm->tx_desc_full)
		for (i = 0; i < MAX_NETDEV_NUM; i++)
			if (comm->ndev[i])
				if (netif_queue_stopped(comm->ndev[i]))
					netif_wake_queue(comm->ndev[i]);

	spin_unlock(&comm->tx_lock);

	spin_lock_irqsave(&comm->int_mask_lock, flags);
	mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
	mask &= ~MAC_INT_TX;
	writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
	spin_unlock_irqrestore(&comm->int_mask_lock, flags);

	napi_complete(napi);
	return budget - budget_left;
}

irqreturn_t spl2sw_ethernet_interrupt(int irq, void *dev_id)
{
	struct spl2sw_common *comm = (struct spl2sw_common *)dev_id;
	u32 status;
	u32 mask;
	int i;

	status = readl(comm->l2sw_reg_base + L2SW_SW_INT_STATUS_0);
	if (unlikely(!status)) {
		dev_dbg(&comm->pdev->dev, "Interrupt status is null!\n");
		goto spl2sw_ethernet_int_out;
	}
	writel(status, comm->l2sw_reg_base + L2SW_SW_INT_STATUS_0);

	if (status & MAC_INT_RX) {
		/* Disable RX interrupts. */
		spin_lock(&comm->int_mask_lock);
		mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
		mask |= MAC_INT_RX;
		writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
		spin_unlock(&comm->int_mask_lock);

		if (unlikely(status & MAC_INT_RX_DES_ERR)) {
			for (i = 0; i < MAX_NETDEV_NUM; i++)
				if (comm->ndev[i]) {
					comm->ndev[i]->stats.rx_fifo_errors++;
					break;
				}
			dev_dbg(&comm->pdev->dev, "Illegal RX Descriptor!\n");
		}

		napi_schedule(&comm->rx_napi);
	}

	if (status & MAC_INT_TX) {
		/* Disable TX interrupts. */
		spin_lock(&comm->int_mask_lock);
		mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
		mask |= MAC_INT_TX;
		writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
		spin_unlock(&comm->int_mask_lock);

		if (unlikely(status & MAC_INT_TX_DES_ERR)) {
			for (i = 0; i < MAX_NETDEV_NUM; i++)
				if (comm->ndev[i]) {
					comm->ndev[i]->stats.tx_fifo_errors++;
					break;
				}
			dev_dbg(&comm->pdev->dev, "Illegal TX Descriptor Error\n");

			spin_lock(&comm->int_mask_lock);
			mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
			mask &= ~MAC_INT_TX;
			writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
			spin_unlock(&comm->int_mask_lock);
		} else {
			napi_schedule(&comm->tx_napi);
		}
	}

spl2sw_ethernet_int_out:
	return IRQ_HANDLED;
}
