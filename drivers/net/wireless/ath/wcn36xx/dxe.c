/*
 * Copyright (c) 2013 Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* DXE - DMA transfer engine
 * we have 2 channels(High prio and Low prio) for TX and 2 channels for RX.
 * through low channels data packets are transfered
 * through high channels managment packets are transfered
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/interrupt.h>
#include <linux/soc/qcom/smem_state.h>
#include "wcn36xx.h"
#include "txrx.h"

static void wcn36xx_ccu_write_register(struct wcn36xx *wcn, int addr, int data)
{
	wcn36xx_dbg(WCN36XX_DBG_DXE,
		    "wcn36xx_ccu_write_register: addr=%x, data=%x\n",
		    addr, data);

	writel(data, wcn->ccu_base + addr);
}

static void wcn36xx_dxe_write_register(struct wcn36xx *wcn, int addr, int data)
{
	wcn36xx_dbg(WCN36XX_DBG_DXE,
		    "wcn36xx_dxe_write_register: addr=%x, data=%x\n",
		    addr, data);

	writel(data, wcn->dxe_base + addr);
}

static void wcn36xx_dxe_read_register(struct wcn36xx *wcn, int addr, int *data)
{
	*data = readl(wcn->dxe_base + addr);

	wcn36xx_dbg(WCN36XX_DBG_DXE,
		    "wcn36xx_dxe_read_register: addr=%x, data=%x\n",
		    addr, *data);
}

static void wcn36xx_dxe_free_ctl_block(struct wcn36xx_dxe_ch *ch)
{
	struct wcn36xx_dxe_ctl *ctl = ch->head_blk_ctl, *next;
	int i;

	for (i = 0; i < ch->desc_num && ctl; i++) {
		next = ctl->next;
		kfree(ctl);
		ctl = next;
	}
}

static int wcn36xx_dxe_allocate_ctl_block(struct wcn36xx_dxe_ch *ch)
{
	struct wcn36xx_dxe_ctl *prev_ctl = NULL;
	struct wcn36xx_dxe_ctl *cur_ctl = NULL;
	int i;

	spin_lock_init(&ch->lock);
	for (i = 0; i < ch->desc_num; i++) {
		cur_ctl = kzalloc(sizeof(*cur_ctl), GFP_KERNEL);
		if (!cur_ctl)
			goto out_fail;

		cur_ctl->ctl_blk_order = i;
		if (i == 0) {
			ch->head_blk_ctl = cur_ctl;
			ch->tail_blk_ctl = cur_ctl;
		} else if (ch->desc_num - 1 == i) {
			prev_ctl->next = cur_ctl;
			cur_ctl->next = ch->head_blk_ctl;
		} else {
			prev_ctl->next = cur_ctl;
		}
		prev_ctl = cur_ctl;
	}

	return 0;

out_fail:
	wcn36xx_dxe_free_ctl_block(ch);
	return -ENOMEM;
}

int wcn36xx_dxe_alloc_ctl_blks(struct wcn36xx *wcn)
{
	int ret;

	wcn->dxe_tx_l_ch.ch_type = WCN36XX_DXE_CH_TX_L;
	wcn->dxe_tx_h_ch.ch_type = WCN36XX_DXE_CH_TX_H;
	wcn->dxe_rx_l_ch.ch_type = WCN36XX_DXE_CH_RX_L;
	wcn->dxe_rx_h_ch.ch_type = WCN36XX_DXE_CH_RX_H;

	wcn->dxe_tx_l_ch.desc_num = WCN36XX_DXE_CH_DESC_NUMB_TX_L;
	wcn->dxe_tx_h_ch.desc_num = WCN36XX_DXE_CH_DESC_NUMB_TX_H;
	wcn->dxe_rx_l_ch.desc_num = WCN36XX_DXE_CH_DESC_NUMB_RX_L;
	wcn->dxe_rx_h_ch.desc_num = WCN36XX_DXE_CH_DESC_NUMB_RX_H;

	wcn->dxe_tx_l_ch.dxe_wq =  WCN36XX_DXE_WQ_TX_L;
	wcn->dxe_tx_h_ch.dxe_wq =  WCN36XX_DXE_WQ_TX_H;

	wcn->dxe_tx_l_ch.ctrl_bd = WCN36XX_DXE_CTRL_TX_L_BD;
	wcn->dxe_tx_h_ch.ctrl_bd = WCN36XX_DXE_CTRL_TX_H_BD;

	wcn->dxe_tx_l_ch.ctrl_skb = WCN36XX_DXE_CTRL_TX_L_SKB;
	wcn->dxe_tx_h_ch.ctrl_skb = WCN36XX_DXE_CTRL_TX_H_SKB;

	wcn->dxe_tx_l_ch.reg_ctrl = WCN36XX_DXE_REG_CTL_TX_L;
	wcn->dxe_tx_h_ch.reg_ctrl = WCN36XX_DXE_REG_CTL_TX_H;

	wcn->dxe_tx_l_ch.def_ctrl = WCN36XX_DXE_CH_DEFAULT_CTL_TX_L;
	wcn->dxe_tx_h_ch.def_ctrl = WCN36XX_DXE_CH_DEFAULT_CTL_TX_H;

	/* DXE control block allocation */
	ret = wcn36xx_dxe_allocate_ctl_block(&wcn->dxe_tx_l_ch);
	if (ret)
		goto out_err;
	ret = wcn36xx_dxe_allocate_ctl_block(&wcn->dxe_tx_h_ch);
	if (ret)
		goto out_err;
	ret = wcn36xx_dxe_allocate_ctl_block(&wcn->dxe_rx_l_ch);
	if (ret)
		goto out_err;
	ret = wcn36xx_dxe_allocate_ctl_block(&wcn->dxe_rx_h_ch);
	if (ret)
		goto out_err;

	/* Initialize SMSM state  Clear TX Enable RING EMPTY STATE */
	ret = qcom_smem_state_update_bits(wcn->tx_enable_state,
					  WCN36XX_SMSM_WLAN_TX_ENABLE |
					  WCN36XX_SMSM_WLAN_TX_RINGS_EMPTY,
					  WCN36XX_SMSM_WLAN_TX_RINGS_EMPTY);
	if (ret)
		goto out_err;

	return 0;

out_err:
	wcn36xx_err("Failed to allocate DXE control blocks\n");
	wcn36xx_dxe_free_ctl_blks(wcn);
	return -ENOMEM;
}

void wcn36xx_dxe_free_ctl_blks(struct wcn36xx *wcn)
{
	wcn36xx_dxe_free_ctl_block(&wcn->dxe_tx_l_ch);
	wcn36xx_dxe_free_ctl_block(&wcn->dxe_tx_h_ch);
	wcn36xx_dxe_free_ctl_block(&wcn->dxe_rx_l_ch);
	wcn36xx_dxe_free_ctl_block(&wcn->dxe_rx_h_ch);
}

static int wcn36xx_dxe_init_descs(struct device *dev, struct wcn36xx_dxe_ch *wcn_ch)
{
	struct wcn36xx_dxe_desc *cur_dxe = NULL;
	struct wcn36xx_dxe_desc *prev_dxe = NULL;
	struct wcn36xx_dxe_ctl *cur_ctl = NULL;
	size_t size;
	int i;

	size = wcn_ch->desc_num * sizeof(struct wcn36xx_dxe_desc);
	wcn_ch->cpu_addr = dma_alloc_coherent(dev, size, &wcn_ch->dma_addr,
					      GFP_KERNEL);
	if (!wcn_ch->cpu_addr)
		return -ENOMEM;

	cur_dxe = (struct wcn36xx_dxe_desc *)wcn_ch->cpu_addr;
	cur_ctl = wcn_ch->head_blk_ctl;

	for (i = 0; i < wcn_ch->desc_num; i++) {
		cur_ctl->desc = cur_dxe;
		cur_ctl->desc_phy_addr = wcn_ch->dma_addr +
			i * sizeof(struct wcn36xx_dxe_desc);

		switch (wcn_ch->ch_type) {
		case WCN36XX_DXE_CH_TX_L:
			cur_dxe->ctrl = WCN36XX_DXE_CTRL_TX_L;
			cur_dxe->dst_addr_l = WCN36XX_DXE_WQ_TX_L;
			break;
		case WCN36XX_DXE_CH_TX_H:
			cur_dxe->ctrl = WCN36XX_DXE_CTRL_TX_H;
			cur_dxe->dst_addr_l = WCN36XX_DXE_WQ_TX_H;
			break;
		case WCN36XX_DXE_CH_RX_L:
			cur_dxe->ctrl = WCN36XX_DXE_CTRL_RX_L;
			cur_dxe->src_addr_l = WCN36XX_DXE_WQ_RX_L;
			break;
		case WCN36XX_DXE_CH_RX_H:
			cur_dxe->ctrl = WCN36XX_DXE_CTRL_RX_H;
			cur_dxe->src_addr_l = WCN36XX_DXE_WQ_RX_H;
			break;
		}
		if (0 == i) {
			cur_dxe->phy_next_l = 0;
		} else if ((0 < i) && (i < wcn_ch->desc_num - 1)) {
			prev_dxe->phy_next_l =
				cur_ctl->desc_phy_addr;
		} else if (i == (wcn_ch->desc_num - 1)) {
			prev_dxe->phy_next_l =
				cur_ctl->desc_phy_addr;
			cur_dxe->phy_next_l =
				wcn_ch->head_blk_ctl->desc_phy_addr;
		}
		cur_ctl = cur_ctl->next;
		prev_dxe = cur_dxe;
		cur_dxe++;
	}

	return 0;
}

static void wcn36xx_dxe_deinit_descs(struct device *dev, struct wcn36xx_dxe_ch *wcn_ch)
{
	size_t size;

	size = wcn_ch->desc_num * sizeof(struct wcn36xx_dxe_desc);
	dma_free_coherent(dev, size,wcn_ch->cpu_addr, wcn_ch->dma_addr);
}

static void wcn36xx_dxe_init_tx_bd(struct wcn36xx_dxe_ch *ch,
				   struct wcn36xx_dxe_mem_pool *pool)
{
	int i, chunk_size = pool->chunk_size;
	dma_addr_t bd_phy_addr = pool->phy_addr;
	void *bd_cpu_addr = pool->virt_addr;
	struct wcn36xx_dxe_ctl *cur = ch->head_blk_ctl;

	for (i = 0; i < ch->desc_num; i++) {
		/* Only every second dxe needs a bd pointer,
		   the other will point to the skb data */
		if (!(i & 1)) {
			cur->bd_phy_addr = bd_phy_addr;
			cur->bd_cpu_addr = bd_cpu_addr;
			bd_phy_addr += chunk_size;
			bd_cpu_addr += chunk_size;
		} else {
			cur->bd_phy_addr = 0;
			cur->bd_cpu_addr = NULL;
		}
		cur = cur->next;
	}
}

static int wcn36xx_dxe_enable_ch_int(struct wcn36xx *wcn, u16 wcn_ch)
{
	int reg_data = 0;

	wcn36xx_dxe_read_register(wcn,
				  WCN36XX_DXE_INT_MASK_REG,
				  &reg_data);

	reg_data |= wcn_ch;

	wcn36xx_dxe_write_register(wcn,
				   WCN36XX_DXE_INT_MASK_REG,
				   (int)reg_data);
	return 0;
}

static void wcn36xx_dxe_disable_ch_int(struct wcn36xx *wcn, u16 wcn_ch)
{
	int reg_data = 0;

	wcn36xx_dxe_read_register(wcn,
				  WCN36XX_DXE_INT_MASK_REG,
				  &reg_data);

	reg_data &= ~wcn_ch;

	wcn36xx_dxe_write_register(wcn,
				   WCN36XX_DXE_INT_MASK_REG,
				   (int)reg_data);
}

static int wcn36xx_dxe_fill_skb(struct device *dev,
				struct wcn36xx_dxe_ctl *ctl,
				gfp_t gfp)
{
	struct wcn36xx_dxe_desc *dxe = ctl->desc;
	struct sk_buff *skb;

	skb = alloc_skb(WCN36XX_PKT_SIZE, gfp);
	if (skb == NULL)
		return -ENOMEM;

	dxe->dst_addr_l = dma_map_single(dev,
					 skb_tail_pointer(skb),
					 WCN36XX_PKT_SIZE,
					 DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dxe->dst_addr_l)) {
		dev_err(dev, "unable to map skb\n");
		kfree_skb(skb);
		return -ENOMEM;
	}
	ctl->skb = skb;

	return 0;
}

static int wcn36xx_dxe_ch_alloc_skb(struct wcn36xx *wcn,
				    struct wcn36xx_dxe_ch *wcn_ch)
{
	int i;
	struct wcn36xx_dxe_ctl *cur_ctl = NULL;

	cur_ctl = wcn_ch->head_blk_ctl;

	for (i = 0; i < wcn_ch->desc_num; i++) {
		wcn36xx_dxe_fill_skb(wcn->dev, cur_ctl, GFP_KERNEL);
		cur_ctl = cur_ctl->next;
	}

	return 0;
}

static void wcn36xx_dxe_ch_free_skbs(struct wcn36xx *wcn,
				     struct wcn36xx_dxe_ch *wcn_ch)
{
	struct wcn36xx_dxe_ctl *cur = wcn_ch->head_blk_ctl;
	int i;

	for (i = 0; i < wcn_ch->desc_num; i++) {
		kfree_skb(cur->skb);
		cur = cur->next;
	}
}

void wcn36xx_dxe_tx_ack_ind(struct wcn36xx *wcn, u32 status)
{
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&wcn->dxe_lock, flags);
	skb = wcn->tx_ack_skb;
	wcn->tx_ack_skb = NULL;
	del_timer(&wcn->tx_ack_timer);
	spin_unlock_irqrestore(&wcn->dxe_lock, flags);

	if (!skb) {
		wcn36xx_warn("Spurious TX complete indication\n");
		return;
	}

	info = IEEE80211_SKB_CB(skb);

	if (status == 1)
		info->flags |= IEEE80211_TX_STAT_ACK;
	else
		info->flags &= ~IEEE80211_TX_STAT_ACK;

	wcn36xx_dbg(WCN36XX_DBG_DXE, "dxe tx ack status: %d\n", status);

	ieee80211_tx_status_irqsafe(wcn->hw, skb);
	ieee80211_wake_queues(wcn->hw);
}

static void wcn36xx_dxe_tx_timer(struct timer_list *t)
{
	struct wcn36xx *wcn = from_timer(wcn, t, tx_ack_timer);
	struct ieee80211_tx_info *info;
	unsigned long flags;
	struct sk_buff *skb;

	/* TX Timeout */
	wcn36xx_dbg(WCN36XX_DBG_DXE, "TX timeout\n");

	spin_lock_irqsave(&wcn->dxe_lock, flags);
	skb = wcn->tx_ack_skb;
	wcn->tx_ack_skb = NULL;
	spin_unlock_irqrestore(&wcn->dxe_lock, flags);

	if (!skb)
		return;

	info = IEEE80211_SKB_CB(skb);
	info->flags &= ~IEEE80211_TX_STAT_ACK;
	info->flags &= ~IEEE80211_TX_STAT_NOACK_TRANSMITTED;

	ieee80211_tx_status_irqsafe(wcn->hw, skb);
	ieee80211_wake_queues(wcn->hw);
}

static void reap_tx_dxes(struct wcn36xx *wcn, struct wcn36xx_dxe_ch *ch)
{
	struct wcn36xx_dxe_ctl *ctl;
	struct ieee80211_tx_info *info;
	unsigned long flags;

	/*
	 * Make at least one loop of do-while because in case ring is
	 * completely full head and tail are pointing to the same element
	 * and while-do will not make any cycles.
	 */
	spin_lock_irqsave(&ch->lock, flags);
	ctl = ch->tail_blk_ctl;
	do {
		if (READ_ONCE(ctl->desc->ctrl) & WCN36xx_DXE_CTRL_VLD)
			break;

		if (ctl->skb &&
		    READ_ONCE(ctl->desc->ctrl) & WCN36xx_DXE_CTRL_EOP) {
			dma_unmap_single(wcn->dev, ctl->desc->src_addr_l,
					 ctl->skb->len, DMA_TO_DEVICE);
			info = IEEE80211_SKB_CB(ctl->skb);
			if (info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS) {
				if (info->flags & IEEE80211_TX_CTL_NO_ACK) {
					info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
					ieee80211_tx_status_irqsafe(wcn->hw, ctl->skb);
				} else {
					/* Wait for the TX ack indication or timeout... */
					spin_lock(&wcn->dxe_lock);
					if (WARN_ON(wcn->tx_ack_skb))
						ieee80211_free_txskb(wcn->hw, wcn->tx_ack_skb);
					wcn->tx_ack_skb = ctl->skb; /* Tracking ref */
					mod_timer(&wcn->tx_ack_timer, jiffies + HZ / 10);
					spin_unlock(&wcn->dxe_lock);
				}
				/* do not free, ownership transferred to mac80211 status cb */
			} else {
				ieee80211_free_txskb(wcn->hw, ctl->skb);
			}

			if (wcn->queues_stopped) {
				wcn->queues_stopped = false;
				ieee80211_wake_queues(wcn->hw);
			}

			ctl->skb = NULL;
		}
		ctl = ctl->next;
	} while (ctl != ch->head_blk_ctl);

	ch->tail_blk_ctl = ctl;
	spin_unlock_irqrestore(&ch->lock, flags);
}

static irqreturn_t wcn36xx_irq_tx_complete(int irq, void *dev)
{
	struct wcn36xx *wcn = (struct wcn36xx *)dev;
	int int_src, int_reason;

	wcn36xx_dxe_read_register(wcn, WCN36XX_DXE_INT_SRC_RAW_REG, &int_src);

	if (int_src & WCN36XX_INT_MASK_CHAN_TX_H) {
		wcn36xx_dxe_read_register(wcn,
					  WCN36XX_DXE_CH_STATUS_REG_ADDR_TX_H,
					  &int_reason);

		wcn36xx_dxe_write_register(wcn,
					   WCN36XX_DXE_0_INT_CLR,
					   WCN36XX_INT_MASK_CHAN_TX_H);

		if (int_reason & WCN36XX_CH_STAT_INT_ERR_MASK ) {
			wcn36xx_dxe_write_register(wcn,
						   WCN36XX_DXE_0_INT_ERR_CLR,
						   WCN36XX_INT_MASK_CHAN_TX_H);

			wcn36xx_err("DXE IRQ reported error: 0x%x in high TX channel\n",
					int_src);
		}

		if (int_reason & WCN36XX_CH_STAT_INT_DONE_MASK) {
			wcn36xx_dxe_write_register(wcn,
						   WCN36XX_DXE_0_INT_DONE_CLR,
						   WCN36XX_INT_MASK_CHAN_TX_H);
		}

		if (int_reason & WCN36XX_CH_STAT_INT_ED_MASK) {
			wcn36xx_dxe_write_register(wcn,
						   WCN36XX_DXE_0_INT_ED_CLR,
						   WCN36XX_INT_MASK_CHAN_TX_H);
		}

		wcn36xx_dbg(WCN36XX_DBG_DXE, "dxe tx ready high, reason %08x\n",
			    int_reason);

		if (int_reason & (WCN36XX_CH_STAT_INT_DONE_MASK |
				  WCN36XX_CH_STAT_INT_ED_MASK)) {
			reap_tx_dxes(wcn, &wcn->dxe_tx_h_ch);
		}
	}

	if (int_src & WCN36XX_INT_MASK_CHAN_TX_L) {
		wcn36xx_dxe_read_register(wcn,
					  WCN36XX_DXE_CH_STATUS_REG_ADDR_TX_L,
					  &int_reason);

		wcn36xx_dxe_write_register(wcn,
					   WCN36XX_DXE_0_INT_CLR,
					   WCN36XX_INT_MASK_CHAN_TX_L);

		if (int_reason & WCN36XX_CH_STAT_INT_ERR_MASK ) {
			wcn36xx_dxe_write_register(wcn,
						   WCN36XX_DXE_0_INT_ERR_CLR,
						   WCN36XX_INT_MASK_CHAN_TX_L);

			wcn36xx_err("DXE IRQ reported error: 0x%x in low TX channel\n",
					int_src);
		}

		if (int_reason & WCN36XX_CH_STAT_INT_DONE_MASK) {
			wcn36xx_dxe_write_register(wcn,
						   WCN36XX_DXE_0_INT_DONE_CLR,
						   WCN36XX_INT_MASK_CHAN_TX_L);
		}

		if (int_reason & WCN36XX_CH_STAT_INT_ED_MASK) {
			wcn36xx_dxe_write_register(wcn,
						   WCN36XX_DXE_0_INT_ED_CLR,
						   WCN36XX_INT_MASK_CHAN_TX_L);
		}

		wcn36xx_dbg(WCN36XX_DBG_DXE, "dxe tx ready low, reason %08x\n",
			    int_reason);

		if (int_reason & (WCN36XX_CH_STAT_INT_DONE_MASK |
				  WCN36XX_CH_STAT_INT_ED_MASK)) {
			reap_tx_dxes(wcn, &wcn->dxe_tx_l_ch);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t wcn36xx_irq_rx_ready(int irq, void *dev)
{
	struct wcn36xx *wcn = (struct wcn36xx *)dev;

	wcn36xx_dxe_rx_frame(wcn);

	return IRQ_HANDLED;
}

static int wcn36xx_dxe_request_irqs(struct wcn36xx *wcn)
{
	int ret;

	ret = request_irq(wcn->tx_irq, wcn36xx_irq_tx_complete,
			  IRQF_TRIGGER_HIGH, "wcn36xx_tx", wcn);
	if (ret) {
		wcn36xx_err("failed to alloc tx irq\n");
		goto out_err;
	}

	ret = request_irq(wcn->rx_irq, wcn36xx_irq_rx_ready, IRQF_TRIGGER_HIGH,
			  "wcn36xx_rx", wcn);
	if (ret) {
		wcn36xx_err("failed to alloc rx irq\n");
		goto out_txirq;
	}

	enable_irq_wake(wcn->rx_irq);

	return 0;

out_txirq:
	free_irq(wcn->tx_irq, wcn);
out_err:
	return ret;

}

static int wcn36xx_rx_handle_packets(struct wcn36xx *wcn,
				     struct wcn36xx_dxe_ch *ch,
				     u32 ctrl,
				     u32 en_mask,
				     u32 int_mask,
				     u32 status_reg)
{
	struct wcn36xx_dxe_desc *dxe;
	struct wcn36xx_dxe_ctl *ctl;
	dma_addr_t  dma_addr;
	struct sk_buff *skb;
	u32 int_reason;
	int ret;

	wcn36xx_dxe_read_register(wcn, status_reg, &int_reason);
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_0_INT_CLR, int_mask);

	if (int_reason & WCN36XX_CH_STAT_INT_ERR_MASK) {
		wcn36xx_dxe_write_register(wcn,
					   WCN36XX_DXE_0_INT_ERR_CLR,
					   int_mask);

		wcn36xx_err("DXE IRQ reported error on RX channel\n");
	}

	if (int_reason & WCN36XX_CH_STAT_INT_DONE_MASK)
		wcn36xx_dxe_write_register(wcn,
					   WCN36XX_DXE_0_INT_DONE_CLR,
					   int_mask);

	if (int_reason & WCN36XX_CH_STAT_INT_ED_MASK)
		wcn36xx_dxe_write_register(wcn,
					   WCN36XX_DXE_0_INT_ED_CLR,
					   int_mask);

	if (!(int_reason & (WCN36XX_CH_STAT_INT_DONE_MASK |
			    WCN36XX_CH_STAT_INT_ED_MASK)))
		return 0;

	spin_lock(&ch->lock);

	ctl = ch->head_blk_ctl;
	dxe = ctl->desc;

	while (!(READ_ONCE(dxe->ctrl) & WCN36xx_DXE_CTRL_VLD)) {
		/* do not read until we own DMA descriptor */
		dma_rmb();

		/* read/modify DMA descriptor */
		skb = ctl->skb;
		dma_addr = dxe->dst_addr_l;
		ret = wcn36xx_dxe_fill_skb(wcn->dev, ctl, GFP_ATOMIC);
		if (0 == ret) {
			/* new skb allocation ok. Use the new one and queue
			 * the old one to network system.
			 */
			dma_unmap_single(wcn->dev, dma_addr, WCN36XX_PKT_SIZE,
					DMA_FROM_DEVICE);
			wcn36xx_rx_skb(wcn, skb);
		}
		/* else keep old skb not submitted and reuse it for rx DMA
		 * (dropping the packet that it contained)
		 */

		/* flush descriptor changes before re-marking as valid */
		dma_wmb();
		dxe->ctrl = ctrl;

		ctl = ctl->next;
		dxe = ctl->desc;
	}
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_ENCH_ADDR, en_mask);

	ch->head_blk_ctl = ctl;

	spin_unlock(&ch->lock);

	return 0;
}

void wcn36xx_dxe_rx_frame(struct wcn36xx *wcn)
{
	int int_src;

	wcn36xx_dxe_read_register(wcn, WCN36XX_DXE_INT_SRC_RAW_REG, &int_src);

	/* RX_LOW_PRI */
	if (int_src & WCN36XX_DXE_INT_CH1_MASK)
		wcn36xx_rx_handle_packets(wcn, &wcn->dxe_rx_l_ch,
					  WCN36XX_DXE_CTRL_RX_L,
					  WCN36XX_DXE_INT_CH1_MASK,
					  WCN36XX_INT_MASK_CHAN_RX_L,
					  WCN36XX_DXE_CH_STATUS_REG_ADDR_RX_L);

	/* RX_HIGH_PRI */
	if (int_src & WCN36XX_DXE_INT_CH3_MASK)
		wcn36xx_rx_handle_packets(wcn, &wcn->dxe_rx_h_ch,
					  WCN36XX_DXE_CTRL_RX_H,
					  WCN36XX_DXE_INT_CH3_MASK,
					  WCN36XX_INT_MASK_CHAN_RX_H,
					  WCN36XX_DXE_CH_STATUS_REG_ADDR_RX_H);

	if (!int_src)
		wcn36xx_warn("No DXE interrupt pending\n");
}

int wcn36xx_dxe_allocate_mem_pools(struct wcn36xx *wcn)
{
	size_t s;
	void *cpu_addr;

	/* Allocate BD headers for MGMT frames */

	/* Where this come from ask QC */
	wcn->mgmt_mem_pool.chunk_size =	WCN36XX_BD_CHUNK_SIZE +
		16 - (WCN36XX_BD_CHUNK_SIZE % 8);

	s = wcn->mgmt_mem_pool.chunk_size * WCN36XX_DXE_CH_DESC_NUMB_TX_H;
	cpu_addr = dma_alloc_coherent(wcn->dev, s,
				      &wcn->mgmt_mem_pool.phy_addr,
				      GFP_KERNEL);
	if (!cpu_addr)
		goto out_err;

	wcn->mgmt_mem_pool.virt_addr = cpu_addr;

	/* Allocate BD headers for DATA frames */

	/* Where this come from ask QC */
	wcn->data_mem_pool.chunk_size = WCN36XX_BD_CHUNK_SIZE +
		16 - (WCN36XX_BD_CHUNK_SIZE % 8);

	s = wcn->data_mem_pool.chunk_size * WCN36XX_DXE_CH_DESC_NUMB_TX_L;
	cpu_addr = dma_alloc_coherent(wcn->dev, s,
				      &wcn->data_mem_pool.phy_addr,
				      GFP_KERNEL);
	if (!cpu_addr)
		goto out_err;

	wcn->data_mem_pool.virt_addr = cpu_addr;

	return 0;

out_err:
	wcn36xx_dxe_free_mem_pools(wcn);
	wcn36xx_err("Failed to allocate BD mempool\n");
	return -ENOMEM;
}

void wcn36xx_dxe_free_mem_pools(struct wcn36xx *wcn)
{
	if (wcn->mgmt_mem_pool.virt_addr)
		dma_free_coherent(wcn->dev, wcn->mgmt_mem_pool.chunk_size *
				  WCN36XX_DXE_CH_DESC_NUMB_TX_H,
				  wcn->mgmt_mem_pool.virt_addr,
				  wcn->mgmt_mem_pool.phy_addr);

	if (wcn->data_mem_pool.virt_addr) {
		dma_free_coherent(wcn->dev, wcn->data_mem_pool.chunk_size *
				  WCN36XX_DXE_CH_DESC_NUMB_TX_L,
				  wcn->data_mem_pool.virt_addr,
				  wcn->data_mem_pool.phy_addr);
	}
}

int wcn36xx_dxe_tx_frame(struct wcn36xx *wcn,
			 struct wcn36xx_vif *vif_priv,
			 struct wcn36xx_tx_bd *bd,
			 struct sk_buff *skb,
			 bool is_low)
{
	struct wcn36xx_dxe_desc *desc_bd, *desc_skb;
	struct wcn36xx_dxe_ctl *ctl_bd, *ctl_skb;
	struct wcn36xx_dxe_ch *ch = NULL;
	unsigned long flags;
	int ret;

	ch = is_low ? &wcn->dxe_tx_l_ch : &wcn->dxe_tx_h_ch;

	spin_lock_irqsave(&ch->lock, flags);
	ctl_bd = ch->head_blk_ctl;
	ctl_skb = ctl_bd->next;

	/*
	 * If skb is not null that means that we reached the tail of the ring
	 * hence ring is full. Stop queues to let mac80211 back off until ring
	 * has an empty slot again.
	 */
	if (NULL != ctl_skb->skb) {
		ieee80211_stop_queues(wcn->hw);
		wcn->queues_stopped = true;
		spin_unlock_irqrestore(&ch->lock, flags);
		return -EBUSY;
	}

	if (unlikely(ctl_skb->bd_cpu_addr)) {
		wcn36xx_err("bd_cpu_addr cannot be NULL for skb DXE\n");
		ret = -EINVAL;
		goto unlock;
	}

	desc_bd = ctl_bd->desc;
	desc_skb = ctl_skb->desc;

	ctl_bd->skb = NULL;

	/* write buffer descriptor */
	memcpy(ctl_bd->bd_cpu_addr, bd, sizeof(*bd));

	/* Set source address of the BD we send */
	desc_bd->src_addr_l = ctl_bd->bd_phy_addr;
	desc_bd->dst_addr_l = ch->dxe_wq;
	desc_bd->fr_len = sizeof(struct wcn36xx_tx_bd);

	wcn36xx_dbg(WCN36XX_DBG_DXE, "DXE TX\n");

	wcn36xx_dbg_dump(WCN36XX_DBG_DXE_DUMP, "DESC1 >>> ",
			 (char *)desc_bd, sizeof(*desc_bd));
	wcn36xx_dbg_dump(WCN36XX_DBG_DXE_DUMP,
			 "BD   >>> ", (char *)ctl_bd->bd_cpu_addr,
			 sizeof(struct wcn36xx_tx_bd));

	desc_skb->src_addr_l = dma_map_single(wcn->dev,
					      skb->data,
					      skb->len,
					      DMA_TO_DEVICE);
	if (dma_mapping_error(wcn->dev, desc_skb->src_addr_l)) {
		dev_err(wcn->dev, "unable to DMA map src_addr_l\n");
		ret = -ENOMEM;
		goto unlock;
	}

	ctl_skb->skb = skb;
	desc_skb->dst_addr_l = ch->dxe_wq;
	desc_skb->fr_len = ctl_skb->skb->len;

	wcn36xx_dbg_dump(WCN36XX_DBG_DXE_DUMP, "DESC2 >>> ",
			 (char *)desc_skb, sizeof(*desc_skb));
	wcn36xx_dbg_dump(WCN36XX_DBG_DXE_DUMP, "SKB   >>> ",
			 (char *)ctl_skb->skb->data, ctl_skb->skb->len);

	/* Move the head of the ring to the next empty descriptor */
	ch->head_blk_ctl = ctl_skb->next;

	/* Commit all previous writes and set descriptors to VALID */
	wmb();
	desc_skb->ctrl = ch->ctrl_skb;
	wmb();
	desc_bd->ctrl = ch->ctrl_bd;

	/*
	 * When connected and trying to send data frame chip can be in sleep
	 * mode and writing to the register will not wake up the chip. Instead
	 * notify chip about new frame through SMSM bus.
	 */
	if (is_low &&  vif_priv->pw_state == WCN36XX_BMPS) {
		qcom_smem_state_update_bits(wcn->tx_rings_empty_state,
					    WCN36XX_SMSM_WLAN_TX_ENABLE,
					    WCN36XX_SMSM_WLAN_TX_ENABLE);
	} else {
		/* indicate End Of Packet and generate interrupt on descriptor
		 * done.
		 */
		wcn36xx_dxe_write_register(wcn,
			ch->reg_ctrl, ch->def_ctrl);
	}

	ret = 0;
unlock:
	spin_unlock_irqrestore(&ch->lock, flags);
	return ret;
}

int wcn36xx_dxe_init(struct wcn36xx *wcn)
{
	int reg_data = 0, ret;

	reg_data = WCN36XX_DXE_REG_RESET;
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_REG_CSR_RESET, reg_data);

	/* Select channels for rx avail and xfer done interrupts... */
	reg_data = (WCN36XX_DXE_INT_CH3_MASK | WCN36XX_DXE_INT_CH1_MASK) << 16 |
		    WCN36XX_DXE_INT_CH0_MASK | WCN36XX_DXE_INT_CH4_MASK;
	if (wcn->is_pronto)
		wcn36xx_ccu_write_register(wcn, WCN36XX_CCU_DXE_INT_SELECT_PRONTO, reg_data);
	else
		wcn36xx_ccu_write_register(wcn, WCN36XX_CCU_DXE_INT_SELECT_RIVA, reg_data);

	/***************************************/
	/* Init descriptors for TX LOW channel */
	/***************************************/
	ret = wcn36xx_dxe_init_descs(wcn->dev, &wcn->dxe_tx_l_ch);
	if (ret) {
		dev_err(wcn->dev, "Error allocating descriptor\n");
		return ret;
	}
	wcn36xx_dxe_init_tx_bd(&wcn->dxe_tx_l_ch, &wcn->data_mem_pool);

	/* Write channel head to a NEXT register */
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_CH_NEXT_DESC_ADDR_TX_L,
		wcn->dxe_tx_l_ch.head_blk_ctl->desc_phy_addr);

	/* Program DMA destination addr for TX LOW */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_CH_DEST_ADDR_TX_L,
		WCN36XX_DXE_WQ_TX_L);

	wcn36xx_dxe_read_register(wcn, WCN36XX_DXE_REG_CH_EN, &reg_data);

	/***************************************/
	/* Init descriptors for TX HIGH channel */
	/***************************************/
	ret = wcn36xx_dxe_init_descs(wcn->dev, &wcn->dxe_tx_h_ch);
	if (ret) {
		dev_err(wcn->dev, "Error allocating descriptor\n");
		goto out_err_txh_ch;
	}

	wcn36xx_dxe_init_tx_bd(&wcn->dxe_tx_h_ch, &wcn->mgmt_mem_pool);

	/* Write channel head to a NEXT register */
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_CH_NEXT_DESC_ADDR_TX_H,
		wcn->dxe_tx_h_ch.head_blk_ctl->desc_phy_addr);

	/* Program DMA destination addr for TX HIGH */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_CH_DEST_ADDR_TX_H,
		WCN36XX_DXE_WQ_TX_H);

	wcn36xx_dxe_read_register(wcn, WCN36XX_DXE_REG_CH_EN, &reg_data);

	/***************************************/
	/* Init descriptors for RX LOW channel */
	/***************************************/
	ret = wcn36xx_dxe_init_descs(wcn->dev, &wcn->dxe_rx_l_ch);
	if (ret) {
		dev_err(wcn->dev, "Error allocating descriptor\n");
		goto out_err_rxl_ch;
	}

	/* For RX we need to preallocated buffers */
	wcn36xx_dxe_ch_alloc_skb(wcn, &wcn->dxe_rx_l_ch);

	/* Write channel head to a NEXT register */
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_CH_NEXT_DESC_ADDR_RX_L,
		wcn->dxe_rx_l_ch.head_blk_ctl->desc_phy_addr);

	/* Write DMA source address */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_CH_SRC_ADDR_RX_L,
		WCN36XX_DXE_WQ_RX_L);

	/* Program preallocated destination address */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_CH_DEST_ADDR_RX_L,
		wcn->dxe_rx_l_ch.head_blk_ctl->desc->phy_next_l);

	/* Enable default control registers */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_REG_CTL_RX_L,
		WCN36XX_DXE_CH_DEFAULT_CTL_RX_L);

	/***************************************/
	/* Init descriptors for RX HIGH channel */
	/***************************************/
	ret = wcn36xx_dxe_init_descs(wcn->dev, &wcn->dxe_rx_h_ch);
	if (ret) {
		dev_err(wcn->dev, "Error allocating descriptor\n");
		goto out_err_rxh_ch;
	}

	/* For RX we need to prealocat buffers */
	wcn36xx_dxe_ch_alloc_skb(wcn, &wcn->dxe_rx_h_ch);

	/* Write chanel head to a NEXT register */
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_CH_NEXT_DESC_ADDR_RX_H,
		wcn->dxe_rx_h_ch.head_blk_ctl->desc_phy_addr);

	/* Write DMA source address */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_CH_SRC_ADDR_RX_H,
		WCN36XX_DXE_WQ_RX_H);

	/* Program preallocated destination address */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_CH_DEST_ADDR_RX_H,
		 wcn->dxe_rx_h_ch.head_blk_ctl->desc->phy_next_l);

	/* Enable default control registers */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_REG_CTL_RX_H,
		WCN36XX_DXE_CH_DEFAULT_CTL_RX_H);

	ret = wcn36xx_dxe_request_irqs(wcn);
	if (ret < 0)
		goto out_err_irq;

	timer_setup(&wcn->tx_ack_timer, wcn36xx_dxe_tx_timer, 0);

	/* Enable channel interrupts */
	wcn36xx_dxe_enable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_TX_L);
	wcn36xx_dxe_enable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_TX_H);
	wcn36xx_dxe_enable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_RX_L);
	wcn36xx_dxe_enable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_RX_H);

	return 0;

out_err_irq:
	wcn36xx_dxe_deinit_descs(wcn->dev, &wcn->dxe_rx_h_ch);
out_err_rxh_ch:
	wcn36xx_dxe_deinit_descs(wcn->dev, &wcn->dxe_rx_l_ch);
out_err_rxl_ch:
	wcn36xx_dxe_deinit_descs(wcn->dev, &wcn->dxe_tx_h_ch);
out_err_txh_ch:
	wcn36xx_dxe_deinit_descs(wcn->dev, &wcn->dxe_tx_l_ch);

	return ret;
}

void wcn36xx_dxe_deinit(struct wcn36xx *wcn)
{
	/* Disable channel interrupts */
	wcn36xx_dxe_disable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_RX_H);
	wcn36xx_dxe_disable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_RX_L);
	wcn36xx_dxe_disable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_TX_H);
	wcn36xx_dxe_disable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_TX_L);

	free_irq(wcn->tx_irq, wcn);
	free_irq(wcn->rx_irq, wcn);
	del_timer(&wcn->tx_ack_timer);

	if (wcn->tx_ack_skb) {
		ieee80211_tx_status_irqsafe(wcn->hw, wcn->tx_ack_skb);
		wcn->tx_ack_skb = NULL;
	}

	wcn36xx_dxe_ch_free_skbs(wcn, &wcn->dxe_rx_l_ch);
	wcn36xx_dxe_ch_free_skbs(wcn, &wcn->dxe_rx_h_ch);

	wcn36xx_dxe_deinit_descs(wcn->dev, &wcn->dxe_tx_l_ch);
	wcn36xx_dxe_deinit_descs(wcn->dev, &wcn->dxe_tx_h_ch);
	wcn36xx_dxe_deinit_descs(wcn->dev, &wcn->dxe_rx_l_ch);
	wcn36xx_dxe_deinit_descs(wcn->dev, &wcn->dxe_rx_h_ch);
}
