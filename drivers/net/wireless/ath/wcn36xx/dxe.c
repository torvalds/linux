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
#include "wcn36xx.h"
#include "txrx.h"

void *wcn36xx_dxe_get_next_bd(struct wcn36xx *wcn, bool is_low)
{
	struct wcn36xx_dxe_ch *ch = is_low ?
		&wcn->dxe_tx_l_ch :
		&wcn->dxe_tx_h_ch;

	return ch->head_blk_ctl->bd_cpu_addr;
}

static void wcn36xx_dxe_write_register(struct wcn36xx *wcn, int addr, int data)
{
	wcn36xx_dbg(WCN36XX_DBG_DXE,
		    "wcn36xx_dxe_write_register: addr=%x, data=%x\n",
		    addr, data);

	writel(data, wcn->mmio + addr);
}

#define wcn36xx_dxe_write_register_x(wcn, reg, reg_data)		 \
do {									 \
	if (wcn->chip_version == WCN36XX_CHIP_3680)			 \
		wcn36xx_dxe_write_register(wcn, reg ## _3680, reg_data); \
	else								 \
		wcn36xx_dxe_write_register(wcn, reg ## _3660, reg_data); \
} while (0)								 \

static void wcn36xx_dxe_read_register(struct wcn36xx *wcn, int addr, int *data)
{
	*data = readl(wcn->mmio + addr);

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

	for (i = 0; i < ch->desc_num; i++) {
		cur_ctl = kzalloc(sizeof(*cur_ctl), GFP_KERNEL);
		if (!cur_ctl)
			goto out_fail;

		spin_lock_init(&cur_ctl->skb_lock);
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
	ret = wcn->ctrl_ops->smsm_change_state(
		WCN36XX_SMSM_WLAN_TX_ENABLE,
		WCN36XX_SMSM_WLAN_TX_RINGS_EMPTY);

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

static int wcn36xx_dxe_init_descs(struct wcn36xx_dxe_ch *wcn_ch)
{
	struct wcn36xx_dxe_desc *cur_dxe = NULL;
	struct wcn36xx_dxe_desc *prev_dxe = NULL;
	struct wcn36xx_dxe_ctl *cur_ctl = NULL;
	size_t size;
	int i;

	size = wcn_ch->desc_num * sizeof(struct wcn36xx_dxe_desc);
	wcn_ch->cpu_addr = dma_alloc_coherent(NULL, size, &wcn_ch->dma_addr,
					      GFP_KERNEL);
	if (!wcn_ch->cpu_addr)
		return -ENOMEM;

	memset(wcn_ch->cpu_addr, 0, size);

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

static int wcn36xx_dxe_fill_skb(struct wcn36xx_dxe_ctl *ctl)
{
	struct wcn36xx_dxe_desc *dxe = ctl->desc;
	struct sk_buff *skb;

	skb = alloc_skb(WCN36XX_PKT_SIZE, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	dxe->dst_addr_l = dma_map_single(NULL,
					 skb_tail_pointer(skb),
					 WCN36XX_PKT_SIZE,
					 DMA_FROM_DEVICE);
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
		wcn36xx_dxe_fill_skb(cur_ctl);
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
	spin_unlock_irqrestore(&wcn->dxe_lock, flags);

	if (!skb) {
		wcn36xx_warn("Spurious TX complete indication\n");
		return;
	}

	info = IEEE80211_SKB_CB(skb);

	if (status == 1)
		info->flags |= IEEE80211_TX_STAT_ACK;

	wcn36xx_dbg(WCN36XX_DBG_DXE, "dxe tx ack status: %d\n", status);

	ieee80211_tx_status_irqsafe(wcn->hw, skb);
	ieee80211_wake_queues(wcn->hw);
}

static void reap_tx_dxes(struct wcn36xx *wcn, struct wcn36xx_dxe_ch *ch)
{
	struct wcn36xx_dxe_ctl *ctl = ch->tail_blk_ctl;
	struct ieee80211_tx_info *info;
	unsigned long flags;

	/*
	 * Make at least one loop of do-while because in case ring is
	 * completely full head and tail are pointing to the same element
	 * and while-do will not make any cycles.
	 */
	do {
		if (ctl->desc->ctrl & WCN36XX_DXE_CTRL_VALID_MASK)
			break;
		if (ctl->skb) {
			dma_unmap_single(NULL, ctl->desc->src_addr_l,
					 ctl->skb->len, DMA_TO_DEVICE);
			info = IEEE80211_SKB_CB(ctl->skb);
			if (!(info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS)) {
				/* Keep frame until TX status comes */
				ieee80211_free_txskb(wcn->hw, ctl->skb);
			}
			spin_lock_irqsave(&ctl->skb_lock, flags);
			if (wcn->queues_stopped) {
				wcn->queues_stopped = false;
				ieee80211_wake_queues(wcn->hw);
			}
			spin_unlock_irqrestore(&ctl->skb_lock, flags);

			ctl->skb = NULL;
		}
		ctl = ctl->next;
	} while (ctl != ch->head_blk_ctl &&
	       !(ctl->desc->ctrl & WCN36XX_DXE_CTRL_VALID_MASK));

	ch->tail_blk_ctl = ctl;
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

		/* TODO: Check int_reason */

		wcn36xx_dxe_write_register(wcn,
					   WCN36XX_DXE_0_INT_CLR,
					   WCN36XX_INT_MASK_CHAN_TX_H);

		wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_0_INT_ED_CLR,
					   WCN36XX_INT_MASK_CHAN_TX_H);
		wcn36xx_dbg(WCN36XX_DBG_DXE, "dxe tx ready high\n");
		reap_tx_dxes(wcn, &wcn->dxe_tx_h_ch);
	}

	if (int_src & WCN36XX_INT_MASK_CHAN_TX_L) {
		wcn36xx_dxe_read_register(wcn,
					  WCN36XX_DXE_CH_STATUS_REG_ADDR_TX_L,
					  &int_reason);
		/* TODO: Check int_reason */

		wcn36xx_dxe_write_register(wcn,
					   WCN36XX_DXE_0_INT_CLR,
					   WCN36XX_INT_MASK_CHAN_TX_L);

		wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_0_INT_ED_CLR,
					   WCN36XX_INT_MASK_CHAN_TX_L);
		wcn36xx_dbg(WCN36XX_DBG_DXE, "dxe tx ready low\n");
		reap_tx_dxes(wcn, &wcn->dxe_tx_l_ch);
	}

	return IRQ_HANDLED;
}

static irqreturn_t wcn36xx_irq_rx_ready(int irq, void *dev)
{
	struct wcn36xx *wcn = (struct wcn36xx *)dev;

	disable_irq_nosync(wcn->rx_irq);
	wcn36xx_dxe_rx_frame(wcn);
	enable_irq(wcn->rx_irq);
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
				     struct wcn36xx_dxe_ch *ch)
{
	struct wcn36xx_dxe_ctl *ctl = ch->head_blk_ctl;
	struct wcn36xx_dxe_desc *dxe = ctl->desc;
	dma_addr_t  dma_addr;
	struct sk_buff *skb;

	while (!(dxe->ctrl & WCN36XX_DXE_CTRL_VALID_MASK)) {
		skb = ctl->skb;
		dma_addr = dxe->dst_addr_l;
		wcn36xx_dxe_fill_skb(ctl);

		switch (ch->ch_type) {
		case WCN36XX_DXE_CH_RX_L:
			dxe->ctrl = WCN36XX_DXE_CTRL_RX_L;
			wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_ENCH_ADDR,
						   WCN36XX_DXE_INT_CH1_MASK);
			break;
		case WCN36XX_DXE_CH_RX_H:
			dxe->ctrl = WCN36XX_DXE_CTRL_RX_H;
			wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_ENCH_ADDR,
						   WCN36XX_DXE_INT_CH3_MASK);
			break;
		default:
			wcn36xx_warn("Unknown channel\n");
		}

		dma_unmap_single(NULL, dma_addr, WCN36XX_PKT_SIZE,
				 DMA_FROM_DEVICE);
		wcn36xx_rx_skb(wcn, skb);
		ctl = ctl->next;
		dxe = ctl->desc;
	}

	ch->head_blk_ctl = ctl;

	return 0;
}

void wcn36xx_dxe_rx_frame(struct wcn36xx *wcn)
{
	int int_src;

	wcn36xx_dxe_read_register(wcn, WCN36XX_DXE_INT_SRC_RAW_REG, &int_src);

	/* RX_LOW_PRI */
	if (int_src & WCN36XX_DXE_INT_CH1_MASK) {
		wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_0_INT_CLR,
					   WCN36XX_DXE_INT_CH1_MASK);
		wcn36xx_rx_handle_packets(wcn, &(wcn->dxe_rx_l_ch));
	}

	/* RX_HIGH_PRI */
	if (int_src & WCN36XX_DXE_INT_CH3_MASK) {
		/* Clean up all the INT within this channel */
		wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_0_INT_CLR,
					   WCN36XX_DXE_INT_CH3_MASK);
		wcn36xx_rx_handle_packets(wcn, &(wcn->dxe_rx_h_ch));
	}

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
	cpu_addr = dma_alloc_coherent(NULL, s, &wcn->mgmt_mem_pool.phy_addr,
				      GFP_KERNEL);
	if (!cpu_addr)
		goto out_err;

	wcn->mgmt_mem_pool.virt_addr = cpu_addr;
	memset(cpu_addr, 0, s);

	/* Allocate BD headers for DATA frames */

	/* Where this come from ask QC */
	wcn->data_mem_pool.chunk_size = WCN36XX_BD_CHUNK_SIZE +
		16 - (WCN36XX_BD_CHUNK_SIZE % 8);

	s = wcn->data_mem_pool.chunk_size * WCN36XX_DXE_CH_DESC_NUMB_TX_L;
	cpu_addr = dma_alloc_coherent(NULL, s, &wcn->data_mem_pool.phy_addr,
				      GFP_KERNEL);
	if (!cpu_addr)
		goto out_err;

	wcn->data_mem_pool.virt_addr = cpu_addr;
	memset(cpu_addr, 0, s);

	return 0;

out_err:
	wcn36xx_dxe_free_mem_pools(wcn);
	wcn36xx_err("Failed to allocate BD mempool\n");
	return -ENOMEM;
}

void wcn36xx_dxe_free_mem_pools(struct wcn36xx *wcn)
{
	if (wcn->mgmt_mem_pool.virt_addr)
		dma_free_coherent(NULL, wcn->mgmt_mem_pool.chunk_size *
				  WCN36XX_DXE_CH_DESC_NUMB_TX_H,
				  wcn->mgmt_mem_pool.virt_addr,
				  wcn->mgmt_mem_pool.phy_addr);

	if (wcn->data_mem_pool.virt_addr) {
		dma_free_coherent(NULL, wcn->data_mem_pool.chunk_size *
				  WCN36XX_DXE_CH_DESC_NUMB_TX_L,
				  wcn->data_mem_pool.virt_addr,
				  wcn->data_mem_pool.phy_addr);
	}
}

int wcn36xx_dxe_tx_frame(struct wcn36xx *wcn,
			 struct wcn36xx_vif *vif_priv,
			 struct sk_buff *skb,
			 bool is_low)
{
	struct wcn36xx_dxe_ctl *ctl = NULL;
	struct wcn36xx_dxe_desc *desc = NULL;
	struct wcn36xx_dxe_ch *ch = NULL;
	unsigned long flags;

	ch = is_low ? &wcn->dxe_tx_l_ch : &wcn->dxe_tx_h_ch;

	ctl = ch->head_blk_ctl;

	spin_lock_irqsave(&ctl->next->skb_lock, flags);

	/*
	 * If skb is not null that means that we reached the tail of the ring
	 * hence ring is full. Stop queues to let mac80211 back off until ring
	 * has an empty slot again.
	 */
	if (NULL != ctl->next->skb) {
		ieee80211_stop_queues(wcn->hw);
		wcn->queues_stopped = true;
		spin_unlock_irqrestore(&ctl->next->skb_lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&ctl->next->skb_lock, flags);

	ctl->skb = NULL;
	desc = ctl->desc;

	/* Set source address of the BD we send */
	desc->src_addr_l = ctl->bd_phy_addr;

	desc->dst_addr_l = ch->dxe_wq;
	desc->fr_len = sizeof(struct wcn36xx_tx_bd);
	desc->ctrl = ch->ctrl_bd;

	wcn36xx_dbg(WCN36XX_DBG_DXE, "DXE TX\n");

	wcn36xx_dbg_dump(WCN36XX_DBG_DXE_DUMP, "DESC1 >>> ",
			 (char *)desc, sizeof(*desc));
	wcn36xx_dbg_dump(WCN36XX_DBG_DXE_DUMP,
			 "BD   >>> ", (char *)ctl->bd_cpu_addr,
			 sizeof(struct wcn36xx_tx_bd));

	/* Set source address of the SKB we send */
	ctl = ctl->next;
	ctl->skb = skb;
	desc = ctl->desc;
	if (ctl->bd_cpu_addr) {
		wcn36xx_err("bd_cpu_addr cannot be NULL for skb DXE\n");
		return -EINVAL;
	}

	desc->src_addr_l = dma_map_single(NULL,
					  ctl->skb->data,
					  ctl->skb->len,
					  DMA_TO_DEVICE);

	desc->dst_addr_l = ch->dxe_wq;
	desc->fr_len = ctl->skb->len;

	/* set dxe descriptor to VALID */
	desc->ctrl = ch->ctrl_skb;

	wcn36xx_dbg_dump(WCN36XX_DBG_DXE_DUMP, "DESC2 >>> ",
			 (char *)desc, sizeof(*desc));
	wcn36xx_dbg_dump(WCN36XX_DBG_DXE_DUMP, "SKB   >>> ",
			 (char *)ctl->skb->data, ctl->skb->len);

	/* Move the head of the ring to the next empty descriptor */
	 ch->head_blk_ctl = ctl->next;

	/*
	 * When connected and trying to send data frame chip can be in sleep
	 * mode and writing to the register will not wake up the chip. Instead
	 * notify chip about new frame through SMSM bus.
	 */
	if (is_low &&  vif_priv->pw_state == WCN36XX_BMPS) {
		wcn->ctrl_ops->smsm_change_state(
				  0,
				  WCN36XX_SMSM_WLAN_TX_ENABLE);
	} else {
		/* indicate End Of Packet and generate interrupt on descriptor
		 * done.
		 */
		wcn36xx_dxe_write_register(wcn,
			ch->reg_ctrl, ch->def_ctrl);
	}

	return 0;
}

int wcn36xx_dxe_init(struct wcn36xx *wcn)
{
	int reg_data = 0, ret;

	reg_data = WCN36XX_DXE_REG_RESET;
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_REG_CSR_RESET, reg_data);

	/* Setting interrupt path */
	reg_data = WCN36XX_DXE_CCU_INT;
	wcn36xx_dxe_write_register_x(wcn, WCN36XX_DXE_REG_CCU_INT, reg_data);

	/***************************************/
	/* Init descriptors for TX LOW channel */
	/***************************************/
	wcn36xx_dxe_init_descs(&wcn->dxe_tx_l_ch);
	wcn36xx_dxe_init_tx_bd(&wcn->dxe_tx_l_ch, &wcn->data_mem_pool);

	/* Write channel head to a NEXT register */
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_CH_NEXT_DESC_ADDR_TX_L,
		wcn->dxe_tx_l_ch.head_blk_ctl->desc_phy_addr);

	/* Program DMA destination addr for TX LOW */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_CH_DEST_ADDR_TX_L,
		WCN36XX_DXE_WQ_TX_L);

	wcn36xx_dxe_read_register(wcn, WCN36XX_DXE_REG_CH_EN, &reg_data);
	wcn36xx_dxe_enable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_TX_L);

	/***************************************/
	/* Init descriptors for TX HIGH channel */
	/***************************************/
	wcn36xx_dxe_init_descs(&wcn->dxe_tx_h_ch);
	wcn36xx_dxe_init_tx_bd(&wcn->dxe_tx_h_ch, &wcn->mgmt_mem_pool);

	/* Write channel head to a NEXT register */
	wcn36xx_dxe_write_register(wcn, WCN36XX_DXE_CH_NEXT_DESC_ADDR_TX_H,
		wcn->dxe_tx_h_ch.head_blk_ctl->desc_phy_addr);

	/* Program DMA destination addr for TX HIGH */
	wcn36xx_dxe_write_register(wcn,
		WCN36XX_DXE_CH_DEST_ADDR_TX_H,
		WCN36XX_DXE_WQ_TX_H);

	wcn36xx_dxe_read_register(wcn, WCN36XX_DXE_REG_CH_EN, &reg_data);

	/* Enable channel interrupts */
	wcn36xx_dxe_enable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_TX_H);

	/***************************************/
	/* Init descriptors for RX LOW channel */
	/***************************************/
	wcn36xx_dxe_init_descs(&wcn->dxe_rx_l_ch);

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

	/* Enable channel interrupts */
	wcn36xx_dxe_enable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_RX_L);

	/***************************************/
	/* Init descriptors for RX HIGH channel */
	/***************************************/
	wcn36xx_dxe_init_descs(&wcn->dxe_rx_h_ch);

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

	/* Enable channel interrupts */
	wcn36xx_dxe_enable_ch_int(wcn, WCN36XX_INT_MASK_CHAN_RX_H);

	ret = wcn36xx_dxe_request_irqs(wcn);
	if (ret < 0)
		goto out_err;

	return 0;

out_err:
	return ret;
}

void wcn36xx_dxe_deinit(struct wcn36xx *wcn)
{
	free_irq(wcn->tx_irq, wcn);
	free_irq(wcn->rx_irq, wcn);

	if (wcn->tx_ack_skb) {
		ieee80211_tx_status_irqsafe(wcn->hw, wcn->tx_ack_skb);
		wcn->tx_ack_skb = NULL;
	}

	wcn36xx_dxe_ch_free_skbs(wcn, &wcn->dxe_rx_l_ch);
	wcn36xx_dxe_ch_free_skbs(wcn, &wcn->dxe_rx_h_ch);
}
