// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <net/dsfield.h>
#include "cfg80211.h"
#include "wlan_cfg.h"

#define WAKE_UP_TRIAL_RETRY		10000

static inline bool is_wilc1000(u32 id)
{
	return (id & (~WILC_CHIP_REV_FIELD)) == WILC_1000_BASE_ID;
}

static inline void acquire_bus(struct wilc *wilc, enum bus_acquire acquire)
{
	mutex_lock(&wilc->hif_cs);
	if (acquire == WILC_BUS_ACQUIRE_AND_WAKEUP && wilc->power_save_mode)
		chip_wakeup(wilc);
}

static inline void release_bus(struct wilc *wilc, enum bus_release release)
{
	if (release == WILC_BUS_RELEASE_ALLOW_SLEEP && wilc->power_save_mode)
		chip_allow_sleep(wilc);
	mutex_unlock(&wilc->hif_cs);
}

static void wilc_wlan_txq_remove(struct wilc *wilc, u8 q_num,
				 struct txq_entry_t *tqe)
{
	list_del(&tqe->list);
	wilc->txq_entries -= 1;
	wilc->txq[q_num].count--;
}

static struct txq_entry_t *
wilc_wlan_txq_remove_from_head(struct wilc *wilc, u8 q_num)
{
	struct txq_entry_t *tqe = NULL;
	unsigned long flags;

	spin_lock_irqsave(&wilc->txq_spinlock, flags);

	if (!list_empty(&wilc->txq[q_num].txq_head.list)) {
		tqe = list_first_entry(&wilc->txq[q_num].txq_head.list,
				       struct txq_entry_t, list);
		list_del(&tqe->list);
		wilc->txq_entries -= 1;
		wilc->txq[q_num].count--;
	}
	spin_unlock_irqrestore(&wilc->txq_spinlock, flags);
	return tqe;
}

static void wilc_wlan_txq_add_to_tail(struct net_device *dev, u8 q_num,
				      struct txq_entry_t *tqe)
{
	unsigned long flags;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	spin_lock_irqsave(&wilc->txq_spinlock, flags);

	list_add_tail(&tqe->list, &wilc->txq[q_num].txq_head.list);
	wilc->txq_entries += 1;
	wilc->txq[q_num].count++;

	spin_unlock_irqrestore(&wilc->txq_spinlock, flags);

	complete(&wilc->txq_event);
}

static void wilc_wlan_txq_add_to_head(struct wilc_vif *vif, u8 q_num,
				      struct txq_entry_t *tqe)
{
	unsigned long flags;
	struct wilc *wilc = vif->wilc;

	mutex_lock(&wilc->txq_add_to_head_cs);

	spin_lock_irqsave(&wilc->txq_spinlock, flags);

	list_add(&tqe->list, &wilc->txq[q_num].txq_head.list);
	wilc->txq_entries += 1;
	wilc->txq[q_num].count++;

	spin_unlock_irqrestore(&wilc->txq_spinlock, flags);
	mutex_unlock(&wilc->txq_add_to_head_cs);
	complete(&wilc->txq_event);
}

#define NOT_TCP_ACK			(-1)

static inline void add_tcp_session(struct wilc_vif *vif, u32 src_prt,
				   u32 dst_prt, u32 seq)
{
	struct tcp_ack_filter *f = &vif->ack_filter;

	if (f->tcp_session < 2 * MAX_TCP_SESSION) {
		f->ack_session_info[f->tcp_session].seq_num = seq;
		f->ack_session_info[f->tcp_session].bigger_ack_num = 0;
		f->ack_session_info[f->tcp_session].src_port = src_prt;
		f->ack_session_info[f->tcp_session].dst_port = dst_prt;
		f->tcp_session++;
	}
}

static inline void update_tcp_session(struct wilc_vif *vif, u32 index, u32 ack)
{
	struct tcp_ack_filter *f = &vif->ack_filter;

	if (index < 2 * MAX_TCP_SESSION &&
	    ack > f->ack_session_info[index].bigger_ack_num)
		f->ack_session_info[index].bigger_ack_num = ack;
}

static inline void add_tcp_pending_ack(struct wilc_vif *vif, u32 ack,
				       u32 session_index,
				       struct txq_entry_t *txqe)
{
	struct tcp_ack_filter *f = &vif->ack_filter;
	u32 i = f->pending_base + f->pending_acks_idx;

	if (i < MAX_PENDING_ACKS) {
		f->pending_acks[i].ack_num = ack;
		f->pending_acks[i].txqe = txqe;
		f->pending_acks[i].session_index = session_index;
		txqe->ack_idx = i;
		f->pending_acks_idx++;
	}
}

static inline void tcp_process(struct net_device *dev, struct txq_entry_t *tqe)
{
	void *buffer = tqe->buffer;
	const struct ethhdr *eth_hdr_ptr = buffer;
	int i;
	unsigned long flags;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;
	struct tcp_ack_filter *f = &vif->ack_filter;
	const struct iphdr *ip_hdr_ptr;
	const struct tcphdr *tcp_hdr_ptr;
	u32 ihl, total_length, data_offset;

	spin_lock_irqsave(&wilc->txq_spinlock, flags);

	if (eth_hdr_ptr->h_proto != htons(ETH_P_IP))
		goto out;

	ip_hdr_ptr = buffer + ETH_HLEN;

	if (ip_hdr_ptr->protocol != IPPROTO_TCP)
		goto out;

	ihl = ip_hdr_ptr->ihl << 2;
	tcp_hdr_ptr = buffer + ETH_HLEN + ihl;
	total_length = ntohs(ip_hdr_ptr->tot_len);

	data_offset = tcp_hdr_ptr->doff << 2;
	if (total_length == (ihl + data_offset)) {
		u32 seq_no, ack_no;

		seq_no = ntohl(tcp_hdr_ptr->seq);
		ack_no = ntohl(tcp_hdr_ptr->ack_seq);
		for (i = 0; i < f->tcp_session; i++) {
			u32 j = f->ack_session_info[i].seq_num;

			if (i < 2 * MAX_TCP_SESSION &&
			    j == seq_no) {
				update_tcp_session(vif, i, ack_no);
				break;
			}
		}
		if (i == f->tcp_session)
			add_tcp_session(vif, 0, 0, seq_no);

		add_tcp_pending_ack(vif, ack_no, i, tqe);
	}

out:
	spin_unlock_irqrestore(&wilc->txq_spinlock, flags);
}

static void wilc_wlan_txq_filter_dup_tcp_ack(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;
	struct tcp_ack_filter *f = &vif->ack_filter;
	u32 i = 0;
	u32 dropped = 0;
	unsigned long flags;

	spin_lock_irqsave(&wilc->txq_spinlock, flags);
	for (i = f->pending_base;
	     i < (f->pending_base + f->pending_acks_idx); i++) {
		u32 index;
		u32 bigger_ack_num;

		if (i >= MAX_PENDING_ACKS)
			break;

		index = f->pending_acks[i].session_index;

		if (index >= 2 * MAX_TCP_SESSION)
			break;

		bigger_ack_num = f->ack_session_info[index].bigger_ack_num;

		if (f->pending_acks[i].ack_num < bigger_ack_num) {
			struct txq_entry_t *tqe;

			tqe = f->pending_acks[i].txqe;
			if (tqe) {
				wilc_wlan_txq_remove(wilc, tqe->q_num, tqe);
				tqe->status = 1;
				if (tqe->tx_complete_func)
					tqe->tx_complete_func(tqe->priv,
							      tqe->status);
				kfree(tqe);
				dropped++;
			}
		}
	}
	f->pending_acks_idx = 0;
	f->tcp_session = 0;

	if (f->pending_base == 0)
		f->pending_base = MAX_TCP_SESSION;
	else
		f->pending_base = 0;

	spin_unlock_irqrestore(&wilc->txq_spinlock, flags);

	while (dropped > 0) {
		wait_for_completion_timeout(&wilc->txq_event,
					    msecs_to_jiffies(1));
		dropped--;
	}
}

void wilc_enable_tcp_ack_filter(struct wilc_vif *vif, bool value)
{
	vif->ack_filter.enabled = value;
}

static int wilc_wlan_txq_add_cfg_pkt(struct wilc_vif *vif, u8 *buffer,
				     u32 buffer_size)
{
	struct txq_entry_t *tqe;
	struct wilc *wilc = vif->wilc;

	netdev_dbg(vif->ndev, "Adding config packet ...\n");
	if (wilc->quit) {
		netdev_dbg(vif->ndev, "Return due to clear function\n");
		complete(&wilc->cfg_event);
		return 0;
	}

	tqe = kmalloc(sizeof(*tqe), GFP_ATOMIC);
	if (!tqe) {
		complete(&wilc->cfg_event);
		return 0;
	}

	tqe->type = WILC_CFG_PKT;
	tqe->buffer = buffer;
	tqe->buffer_size = buffer_size;
	tqe->tx_complete_func = NULL;
	tqe->priv = NULL;
	tqe->q_num = AC_VO_Q;
	tqe->ack_idx = NOT_TCP_ACK;
	tqe->vif = vif;

	wilc_wlan_txq_add_to_head(vif, AC_VO_Q, tqe);

	return 1;
}

static bool is_ac_q_limit(struct wilc *wl, u8 q_num)
{
	u8 factors[NQUEUES] = {1, 1, 1, 1};
	u16 i;
	unsigned long flags;
	struct wilc_tx_queue_status *q = &wl->tx_q_limit;
	u8 end_index;
	u8 q_limit;
	bool ret = false;

	spin_lock_irqsave(&wl->txq_spinlock, flags);
	if (!q->initialized) {
		for (i = 0; i < AC_BUFFER_SIZE; i++)
			q->buffer[i] = i % NQUEUES;

		for (i = 0; i < NQUEUES; i++) {
			q->cnt[i] = AC_BUFFER_SIZE * factors[i] / NQUEUES;
			q->sum += q->cnt[i];
		}
		q->end_index = AC_BUFFER_SIZE - 1;
		q->initialized = 1;
	}

	end_index = q->end_index;
	q->cnt[q->buffer[end_index]] -= factors[q->buffer[end_index]];
	q->cnt[q_num] += factors[q_num];
	q->sum += (factors[q_num] - factors[q->buffer[end_index]]);

	q->buffer[end_index] = q_num;
	if (end_index > 0)
		q->end_index--;
	else
		q->end_index = AC_BUFFER_SIZE - 1;

	if (!q->sum)
		q_limit = 1;
	else
		q_limit = (q->cnt[q_num] * FLOW_CONTROL_UPPER_THRESHOLD / q->sum) + 1;

	if (wl->txq[q_num].count <= q_limit)
		ret = true;

	spin_unlock_irqrestore(&wl->txq_spinlock, flags);

	return ret;
}

static inline u8 ac_classify(struct wilc *wilc, struct sk_buff *skb)
{
	u8 q_num = AC_BE_Q;
	u8 dscp;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		dscp = ipv4_get_dsfield(ip_hdr(skb)) & 0xfc;
		break;
	case htons(ETH_P_IPV6):
		dscp = ipv6_get_dsfield(ipv6_hdr(skb)) & 0xfc;
		break;
	default:
		return q_num;
	}

	switch (dscp) {
	case 0x08:
	case 0x20:
	case 0x40:
		q_num = AC_BK_Q;
		break;
	case 0x80:
	case 0xA0:
	case 0x28:
		q_num = AC_VI_Q;
		break;
	case 0xC0:
	case 0xD0:
	case 0xE0:
	case 0x88:
	case 0xB8:
		q_num = AC_VO_Q;
		break;
	}

	return q_num;
}

static inline int ac_balance(struct wilc *wl, u8 *ratio)
{
	u8 i, max_count = 0;

	if (!ratio)
		return -EINVAL;

	for (i = 0; i < NQUEUES; i++)
		if (wl->txq[i].fw.count > max_count)
			max_count = wl->txq[i].fw.count;

	for (i = 0; i < NQUEUES; i++)
		ratio[i] = max_count - wl->txq[i].fw.count;

	return 0;
}

static inline void ac_update_fw_ac_pkt_info(struct wilc *wl, u32 reg)
{
	wl->txq[AC_BK_Q].fw.count = FIELD_GET(BK_AC_COUNT_FIELD, reg);
	wl->txq[AC_BE_Q].fw.count = FIELD_GET(BE_AC_COUNT_FIELD, reg);
	wl->txq[AC_VI_Q].fw.count = FIELD_GET(VI_AC_COUNT_FIELD, reg);
	wl->txq[AC_VO_Q].fw.count = FIELD_GET(VO_AC_COUNT_FIELD, reg);

	wl->txq[AC_BK_Q].fw.acm = FIELD_GET(BK_AC_ACM_STAT_FIELD, reg);
	wl->txq[AC_BE_Q].fw.acm = FIELD_GET(BE_AC_ACM_STAT_FIELD, reg);
	wl->txq[AC_VI_Q].fw.acm = FIELD_GET(VI_AC_ACM_STAT_FIELD, reg);
	wl->txq[AC_VO_Q].fw.acm = FIELD_GET(VO_AC_ACM_STAT_FIELD, reg);
}

static inline u8 ac_change(struct wilc *wilc, u8 *ac)
{
	do {
		if (wilc->txq[*ac].fw.acm == 0)
			return 0;
		(*ac)++;
	} while (*ac < NQUEUES);

	return 1;
}

int wilc_wlan_txq_add_net_pkt(struct net_device *dev,
			      struct tx_complete_data *tx_data, u8 *buffer,
			      u32 buffer_size,
			      void (*tx_complete_fn)(void *, int))
{
	struct txq_entry_t *tqe;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc;
	u8 q_num;

	wilc = vif->wilc;

	if (wilc->quit) {
		tx_complete_fn(tx_data, 0);
		return 0;
	}

	if (!wilc->initialized) {
		tx_complete_fn(tx_data, 0);
		return 0;
	}

	tqe = kmalloc(sizeof(*tqe), GFP_ATOMIC);

	if (!tqe) {
		tx_complete_fn(tx_data, 0);
		return 0;
	}
	tqe->type = WILC_NET_PKT;
	tqe->buffer = buffer;
	tqe->buffer_size = buffer_size;
	tqe->tx_complete_func = tx_complete_fn;
	tqe->priv = tx_data;
	tqe->vif = vif;

	q_num = ac_classify(wilc, tx_data->skb);
	tqe->q_num = q_num;
	if (ac_change(wilc, &q_num)) {
		tx_complete_fn(tx_data, 0);
		kfree(tqe);
		return 0;
	}

	if (is_ac_q_limit(wilc, q_num)) {
		tqe->ack_idx = NOT_TCP_ACK;
		if (vif->ack_filter.enabled)
			tcp_process(dev, tqe);
		wilc_wlan_txq_add_to_tail(dev, q_num, tqe);
	} else {
		tx_complete_fn(tx_data, 0);
		kfree(tqe);
	}

	return wilc->txq_entries;
}

int wilc_wlan_txq_add_mgmt_pkt(struct net_device *dev, void *priv, u8 *buffer,
			       u32 buffer_size,
			       void (*tx_complete_fn)(void *, int))
{
	struct txq_entry_t *tqe;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc;

	wilc = vif->wilc;

	if (wilc->quit) {
		tx_complete_fn(priv, 0);
		return 0;
	}

	if (!wilc->initialized) {
		tx_complete_fn(priv, 0);
		return 0;
	}
	tqe = kmalloc(sizeof(*tqe), GFP_ATOMIC);

	if (!tqe) {
		tx_complete_fn(priv, 0);
		return 0;
	}
	tqe->type = WILC_MGMT_PKT;
	tqe->buffer = buffer;
	tqe->buffer_size = buffer_size;
	tqe->tx_complete_func = tx_complete_fn;
	tqe->priv = priv;
	tqe->q_num = AC_BE_Q;
	tqe->ack_idx = NOT_TCP_ACK;
	tqe->vif = vif;
	wilc_wlan_txq_add_to_tail(dev, AC_VO_Q, tqe);
	return 1;
}

static struct txq_entry_t *wilc_wlan_txq_get_first(struct wilc *wilc, u8 q_num)
{
	struct txq_entry_t *tqe = NULL;
	unsigned long flags;

	spin_lock_irqsave(&wilc->txq_spinlock, flags);

	if (!list_empty(&wilc->txq[q_num].txq_head.list))
		tqe = list_first_entry(&wilc->txq[q_num].txq_head.list,
				       struct txq_entry_t, list);

	spin_unlock_irqrestore(&wilc->txq_spinlock, flags);

	return tqe;
}

static struct txq_entry_t *wilc_wlan_txq_get_next(struct wilc *wilc,
						  struct txq_entry_t *tqe,
						  u8 q_num)
{
	unsigned long flags;

	spin_lock_irqsave(&wilc->txq_spinlock, flags);

	if (!list_is_last(&tqe->list, &wilc->txq[q_num].txq_head.list))
		tqe = list_next_entry(tqe, list);
	else
		tqe = NULL;
	spin_unlock_irqrestore(&wilc->txq_spinlock, flags);

	return tqe;
}

static void wilc_wlan_rxq_add(struct wilc *wilc, struct rxq_entry_t *rqe)
{
	if (wilc->quit)
		return;

	mutex_lock(&wilc->rxq_cs);
	list_add_tail(&rqe->list, &wilc->rxq_head.list);
	mutex_unlock(&wilc->rxq_cs);
}

static struct rxq_entry_t *wilc_wlan_rxq_remove(struct wilc *wilc)
{
	struct rxq_entry_t *rqe = NULL;

	mutex_lock(&wilc->rxq_cs);
	if (!list_empty(&wilc->rxq_head.list)) {
		rqe = list_first_entry(&wilc->rxq_head.list, struct rxq_entry_t,
				       list);
		list_del(&rqe->list);
	}
	mutex_unlock(&wilc->rxq_cs);
	return rqe;
}

void chip_allow_sleep(struct wilc *wilc)
{
	u32 reg = 0;
	const struct wilc_hif_func *hif_func = wilc->hif_func;
	u32 wakeup_reg, wakeup_bit;
	u32 to_host_from_fw_reg, to_host_from_fw_bit;
	u32 from_host_to_fw_reg, from_host_to_fw_bit;
	u32 trials = 100;
	int ret;

	if (wilc->io_type == WILC_HIF_SDIO) {
		wakeup_reg = WILC_SDIO_WAKEUP_REG;
		wakeup_bit = WILC_SDIO_WAKEUP_BIT;
		from_host_to_fw_reg = WILC_SDIO_HOST_TO_FW_REG;
		from_host_to_fw_bit = WILC_SDIO_HOST_TO_FW_BIT;
		to_host_from_fw_reg = WILC_SDIO_FW_TO_HOST_REG;
		to_host_from_fw_bit = WILC_SDIO_FW_TO_HOST_BIT;
	} else {
		wakeup_reg = WILC_SPI_WAKEUP_REG;
		wakeup_bit = WILC_SPI_WAKEUP_BIT;
		from_host_to_fw_reg = WILC_SPI_HOST_TO_FW_REG;
		from_host_to_fw_bit = WILC_SPI_HOST_TO_FW_BIT;
		to_host_from_fw_reg = WILC_SPI_FW_TO_HOST_REG;
		to_host_from_fw_bit = WILC_SPI_FW_TO_HOST_BIT;
	}

	while (--trials) {
		ret = hif_func->hif_read_reg(wilc, to_host_from_fw_reg, &reg);
		if (ret)
			return;
		if ((reg & to_host_from_fw_bit) == 0)
			break;
	}
	if (!trials)
		pr_warn("FW not responding\n");

	/* Clear bit 1 */
	ret = hif_func->hif_read_reg(wilc, wakeup_reg, &reg);
	if (ret)
		return;
	if (reg & wakeup_bit) {
		reg &= ~wakeup_bit;
		ret = hif_func->hif_write_reg(wilc, wakeup_reg, reg);
		if (ret)
			return;
	}

	ret = hif_func->hif_read_reg(wilc, from_host_to_fw_reg, &reg);
	if (ret)
		return;
	if (reg & from_host_to_fw_bit) {
		reg &= ~from_host_to_fw_bit;
		ret = hif_func->hif_write_reg(wilc, from_host_to_fw_reg, reg);
		if (ret)
			return;

	}
}
EXPORT_SYMBOL_GPL(chip_allow_sleep);

void chip_wakeup(struct wilc *wilc)
{
	u32 ret = 0;
	u32 clk_status_val = 0, trials = 0;
	u32 wakeup_reg, wakeup_bit;
	u32 clk_status_reg, clk_status_bit;
	u32 from_host_to_fw_reg, from_host_to_fw_bit;
	const struct wilc_hif_func *hif_func = wilc->hif_func;

	if (wilc->io_type == WILC_HIF_SDIO) {
		wakeup_reg = WILC_SDIO_WAKEUP_REG;
		wakeup_bit = WILC_SDIO_WAKEUP_BIT;
		clk_status_reg = WILC_SDIO_CLK_STATUS_REG;
		clk_status_bit = WILC_SDIO_CLK_STATUS_BIT;
		from_host_to_fw_reg = WILC_SDIO_HOST_TO_FW_REG;
		from_host_to_fw_bit = WILC_SDIO_HOST_TO_FW_BIT;
	} else {
		wakeup_reg = WILC_SPI_WAKEUP_REG;
		wakeup_bit = WILC_SPI_WAKEUP_BIT;
		clk_status_reg = WILC_SPI_CLK_STATUS_REG;
		clk_status_bit = WILC_SPI_CLK_STATUS_BIT;
		from_host_to_fw_reg = WILC_SPI_HOST_TO_FW_REG;
		from_host_to_fw_bit = WILC_SPI_HOST_TO_FW_BIT;
	}

	/* indicate host wakeup */
	ret = hif_func->hif_write_reg(wilc, from_host_to_fw_reg,
				      from_host_to_fw_bit);
	if (ret)
		return;

	/* Set wake-up bit */
	ret = hif_func->hif_write_reg(wilc, wakeup_reg,
				      wakeup_bit);
	if (ret)
		return;

	while (trials < WAKE_UP_TRIAL_RETRY) {
		ret = hif_func->hif_read_reg(wilc, clk_status_reg,
					     &clk_status_val);
		if (ret) {
			pr_err("Bus error %d %x\n", ret, clk_status_val);
			return;
		}
		if (clk_status_val & clk_status_bit)
			break;

		trials++;
	}
	if (trials >= WAKE_UP_TRIAL_RETRY) {
		pr_err("Failed to wake-up the chip\n");
		return;
	}
	/* Sometimes spi fail to read clock regs after reading
	 * writing clockless registers
	 */
	if (wilc->io_type == WILC_HIF_SPI)
		wilc->hif_func->hif_reset(wilc);
}
EXPORT_SYMBOL_GPL(chip_wakeup);

void host_wakeup_notify(struct wilc *wilc)
{
	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
	wilc->hif_func->hif_write_reg(wilc, WILC_CORTUS_INTERRUPT_2, 1);
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);
}
EXPORT_SYMBOL_GPL(host_wakeup_notify);

void host_sleep_notify(struct wilc *wilc)
{
	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
	wilc->hif_func->hif_write_reg(wilc, WILC_CORTUS_INTERRUPT_1, 1);
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);
}
EXPORT_SYMBOL_GPL(host_sleep_notify);

int wilc_wlan_handle_txq(struct wilc *wilc, u32 *txq_count)
{
	int i, entries = 0;
	u8 k, ac;
	u32 sum;
	u32 reg;
	u8 ac_desired_ratio[NQUEUES] = {0, 0, 0, 0};
	u8 ac_preserve_ratio[NQUEUES] = {1, 1, 1, 1};
	u8 *num_pkts_to_add;
	u8 vmm_entries_ac[WILC_VMM_TBL_SIZE];
	u32 offset = 0;
	bool max_size_over = 0, ac_exist = 0;
	int vmm_sz = 0;
	struct txq_entry_t *tqe_q[NQUEUES];
	int ret = 0;
	int counter;
	int timeout;
	u32 *vmm_table = wilc->vmm_table;
	u8 ac_pkt_num_to_chip[NQUEUES] = {0, 0, 0, 0};
	const struct wilc_hif_func *func;
	int srcu_idx;
	u8 *txb = wilc->tx_buffer;
	struct wilc_vif *vif;

	if (wilc->quit)
		goto out_update_cnt;

	if (ac_balance(wilc, ac_desired_ratio))
		return -EINVAL;

	mutex_lock(&wilc->txq_add_to_head_cs);

	srcu_idx = srcu_read_lock(&wilc->srcu);
	list_for_each_entry_rcu(vif, &wilc->vif_list, list)
		wilc_wlan_txq_filter_dup_tcp_ack(vif->ndev);
	srcu_read_unlock(&wilc->srcu, srcu_idx);

	for (ac = 0; ac < NQUEUES; ac++)
		tqe_q[ac] = wilc_wlan_txq_get_first(wilc, ac);

	i = 0;
	sum = 0;
	max_size_over = 0;
	num_pkts_to_add = ac_desired_ratio;
	do {
		ac_exist = 0;
		for (ac = 0; (ac < NQUEUES) && (!max_size_over); ac++) {
			if (!tqe_q[ac])
				continue;

			ac_exist = 1;
			for (k = 0; (k < num_pkts_to_add[ac]) &&
			     (!max_size_over) && tqe_q[ac]; k++) {
				if (i >= (WILC_VMM_TBL_SIZE - 1)) {
					max_size_over = 1;
					break;
				}

				if (tqe_q[ac]->type == WILC_CFG_PKT)
					vmm_sz = ETH_CONFIG_PKT_HDR_OFFSET;
				else if (tqe_q[ac]->type == WILC_NET_PKT)
					vmm_sz = ETH_ETHERNET_HDR_OFFSET;
				else
					vmm_sz = HOST_HDR_OFFSET;

				vmm_sz += tqe_q[ac]->buffer_size;
				vmm_sz = ALIGN(vmm_sz, 4);

				if ((sum + vmm_sz) > WILC_TX_BUFF_SIZE) {
					max_size_over = 1;
					break;
				}
				vmm_table[i] = vmm_sz / 4;
				if (tqe_q[ac]->type == WILC_CFG_PKT)
					vmm_table[i] |= BIT(10);

				cpu_to_le32s(&vmm_table[i]);
				vmm_entries_ac[i] = ac;

				i++;
				sum += vmm_sz;
				tqe_q[ac] = wilc_wlan_txq_get_next(wilc,
								   tqe_q[ac],
								   ac);
			}
		}
		num_pkts_to_add = ac_preserve_ratio;
	} while (!max_size_over && ac_exist);

	if (i == 0)
		goto out_unlock;
	vmm_table[i] = 0x0;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);
	counter = 0;
	func = wilc->hif_func;
	do {
		ret = func->hif_read_reg(wilc, WILC_HOST_TX_CTRL, &reg);
		if (ret)
			break;

		if ((reg & 0x1) == 0) {
			ac_update_fw_ac_pkt_info(wilc, reg);
			break;
		}

		counter++;
		if (counter > 200) {
			counter = 0;
			ret = func->hif_write_reg(wilc, WILC_HOST_TX_CTRL, 0);
			break;
		}
	} while (!wilc->quit);

	if (ret)
		goto out_release_bus;

	timeout = 200;
	do {
		ret = func->hif_block_tx(wilc,
					 WILC_VMM_TBL_RX_SHADOW_BASE,
					 (u8 *)vmm_table,
					 ((i + 1) * 4));
		if (ret)
			break;

		ret = func->hif_write_reg(wilc, WILC_HOST_VMM_CTL, 0x2);
		if (ret)
			break;

		do {
			ret = func->hif_read_reg(wilc, WILC_HOST_VMM_CTL, &reg);
			if (ret)
				break;
			if (FIELD_GET(WILC_VMM_ENTRY_AVAILABLE, reg)) {
				entries = FIELD_GET(WILC_VMM_ENTRY_COUNT, reg);
				break;
			}
		} while (--timeout);
		if (timeout <= 0) {
			ret = func->hif_write_reg(wilc, WILC_HOST_VMM_CTL, 0x0);
			break;
		}

		if (ret)
			break;

		if (entries == 0) {
			ret = func->hif_read_reg(wilc, WILC_HOST_TX_CTRL, &reg);
			if (ret)
				break;
			reg &= ~BIT(0);
			ret = func->hif_write_reg(wilc, WILC_HOST_TX_CTRL, reg);
		}
	} while (0);

	if (ret)
		goto out_release_bus;

	if (entries == 0) {
		/*
		 * No VMM space available in firmware so retry to transmit
		 * the packet from tx queue.
		 */
		ret = WILC_VMM_ENTRY_FULL_RETRY;
		goto out_release_bus;
	}

	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

	offset = 0;
	i = 0;
	do {
		struct txq_entry_t *tqe;
		u32 header, buffer_offset;
		char *bssid;
		u8 mgmt_ptk = 0;

		if (vmm_table[i] == 0 || vmm_entries_ac[i] >= NQUEUES)
			break;

		tqe = wilc_wlan_txq_remove_from_head(wilc, vmm_entries_ac[i]);
		if (!tqe)
			break;

		ac_pkt_num_to_chip[vmm_entries_ac[i]]++;
		vif = tqe->vif;

		le32_to_cpus(&vmm_table[i]);
		vmm_sz = FIELD_GET(WILC_VMM_BUFFER_SIZE, vmm_table[i]);
		vmm_sz *= 4;

		if (tqe->type == WILC_MGMT_PKT)
			mgmt_ptk = 1;

		header = (FIELD_PREP(WILC_VMM_HDR_TYPE, tqe->type) |
			  FIELD_PREP(WILC_VMM_HDR_MGMT_FIELD, mgmt_ptk) |
			  FIELD_PREP(WILC_VMM_HDR_PKT_SIZE, tqe->buffer_size) |
			  FIELD_PREP(WILC_VMM_HDR_BUFF_SIZE, vmm_sz));

		cpu_to_le32s(&header);
		memcpy(&txb[offset], &header, 4);
		if (tqe->type == WILC_CFG_PKT) {
			buffer_offset = ETH_CONFIG_PKT_HDR_OFFSET;
		} else if (tqe->type == WILC_NET_PKT) {
			int prio = tqe->q_num;

			bssid = tqe->vif->bssid;
			buffer_offset = ETH_ETHERNET_HDR_OFFSET;
			memcpy(&txb[offset + 4], &prio, sizeof(prio));
			memcpy(&txb[offset + 8], bssid, 6);
		} else {
			buffer_offset = HOST_HDR_OFFSET;
		}

		memcpy(&txb[offset + buffer_offset],
		       tqe->buffer, tqe->buffer_size);
		offset += vmm_sz;
		i++;
		tqe->status = 1;
		if (tqe->tx_complete_func)
			tqe->tx_complete_func(tqe->priv, tqe->status);
		if (tqe->ack_idx != NOT_TCP_ACK &&
		    tqe->ack_idx < MAX_PENDING_ACKS)
			vif->ack_filter.pending_acks[tqe->ack_idx].txqe = NULL;
		kfree(tqe);
	} while (--entries);
	for (i = 0; i < NQUEUES; i++)
		wilc->txq[i].fw.count += ac_pkt_num_to_chip[i];

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

	ret = func->hif_clear_int_ext(wilc, ENABLE_TX_VMM);
	if (ret)
		goto out_release_bus;

	ret = func->hif_block_tx_ext(wilc, 0, txb, offset);

out_release_bus:
	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

out_unlock:
	mutex_unlock(&wilc->txq_add_to_head_cs);

out_update_cnt:
	*txq_count = wilc->txq_entries;
	return ret;
}

static void wilc_wlan_handle_rx_buff(struct wilc *wilc, u8 *buffer, int size)
{
	int offset = 0;
	u32 header;
	u32 pkt_len, pkt_offset, tp_len;
	int is_cfg_packet;
	u8 *buff_ptr;

	do {
		buff_ptr = buffer + offset;
		header = get_unaligned_le32(buff_ptr);

		is_cfg_packet = FIELD_GET(WILC_PKT_HDR_CONFIG_FIELD, header);
		pkt_offset = FIELD_GET(WILC_PKT_HDR_OFFSET_FIELD, header);
		tp_len = FIELD_GET(WILC_PKT_HDR_TOTAL_LEN_FIELD, header);
		pkt_len = FIELD_GET(WILC_PKT_HDR_LEN_FIELD, header);

		if (pkt_len == 0 || tp_len == 0)
			break;

		if (pkt_offset & IS_MANAGMEMENT) {
			buff_ptr += HOST_HDR_OFFSET;
			wilc_wfi_mgmt_rx(wilc, buff_ptr, pkt_len,
					 pkt_offset & IS_MGMT_AUTH_PKT);
		} else {
			if (!is_cfg_packet) {
				wilc_frmw_to_host(wilc, buff_ptr, pkt_len,
						  pkt_offset);
			} else {
				struct wilc_cfg_rsp rsp;

				buff_ptr += pkt_offset;

				wilc_wlan_cfg_indicate_rx(wilc, buff_ptr,
							  pkt_len,
							  &rsp);
				if (rsp.type == WILC_CFG_RSP) {
					if (wilc->cfg_seq_no == rsp.seq_no)
						complete(&wilc->cfg_event);
				} else if (rsp.type == WILC_CFG_RSP_STATUS) {
					wilc_mac_indicate(wilc);
				}
			}
		}
		offset += tp_len;
	} while (offset < size);
}

static void wilc_wlan_handle_rxq(struct wilc *wilc)
{
	int size;
	u8 *buffer;
	struct rxq_entry_t *rqe;

	while (!wilc->quit) {
		rqe = wilc_wlan_rxq_remove(wilc);
		if (!rqe)
			break;

		buffer = rqe->buffer;
		size = rqe->buffer_size;
		wilc_wlan_handle_rx_buff(wilc, buffer, size);

		kfree(rqe);
	}
	if (wilc->quit)
		complete(&wilc->cfg_event);
}

static void wilc_unknown_isr_ext(struct wilc *wilc)
{
	wilc->hif_func->hif_clear_int_ext(wilc, 0);
}

static void wilc_wlan_handle_isr_ext(struct wilc *wilc, u32 int_status)
{
	u32 offset = wilc->rx_buffer_offset;
	u8 *buffer = NULL;
	u32 size;
	u32 retries = 0;
	int ret = 0;
	struct rxq_entry_t *rqe;

	size = FIELD_GET(WILC_INTERRUPT_DATA_SIZE, int_status) << 2;

	while (!size && retries < 10) {
		wilc->hif_func->hif_read_size(wilc, &size);
		size = FIELD_GET(WILC_INTERRUPT_DATA_SIZE, size) << 2;
		retries++;
	}

	if (size <= 0)
		return;

	if (WILC_RX_BUFF_SIZE - offset < size)
		offset = 0;

	buffer = &wilc->rx_buffer[offset];

	wilc->hif_func->hif_clear_int_ext(wilc, DATA_INT_CLR | ENABLE_RX_VMM);
	ret = wilc->hif_func->hif_block_rx_ext(wilc, 0, buffer, size);
	if (ret)
		return;

	offset += size;
	wilc->rx_buffer_offset = offset;
	rqe = kmalloc(sizeof(*rqe), GFP_KERNEL);
	if (!rqe)
		return;

	rqe->buffer = buffer;
	rqe->buffer_size = size;
	wilc_wlan_rxq_add(wilc, rqe);
	wilc_wlan_handle_rxq(wilc);
}

void wilc_handle_isr(struct wilc *wilc)
{
	u32 int_status;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);
	wilc->hif_func->hif_read_int(wilc, &int_status);

	if (int_status & DATA_INT_EXT)
		wilc_wlan_handle_isr_ext(wilc, int_status);

	if (!(int_status & (ALL_INT_EXT)))
		wilc_unknown_isr_ext(wilc);

	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
}
EXPORT_SYMBOL_GPL(wilc_handle_isr);

int wilc_wlan_firmware_download(struct wilc *wilc, const u8 *buffer,
				u32 buffer_size)
{
	u32 offset;
	u32 addr, size, size2, blksz;
	u8 *dma_buffer;
	int ret = 0;
	u32 reg = 0;

	blksz = BIT(12);

	dma_buffer = kmalloc(blksz, GFP_KERNEL);
	if (!dma_buffer)
		return -EIO;

	offset = 0;
	pr_debug("%s: Downloading firmware size = %d\n", __func__, buffer_size);

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

	wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);
	reg &= ~BIT(10);
	ret = wilc->hif_func->hif_write_reg(wilc, WILC_GLB_RESET_0, reg);
	wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);
	if (reg & BIT(10))
		pr_err("%s: Failed to reset\n", __func__);

	release_bus(wilc, WILC_BUS_RELEASE_ONLY);
	do {
		addr = get_unaligned_le32(&buffer[offset]);
		size = get_unaligned_le32(&buffer[offset + 4]);
		acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);
		offset += 8;
		while (((int)size) && (offset < buffer_size)) {
			if (size <= blksz)
				size2 = size;
			else
				size2 = blksz;

			memcpy(dma_buffer, &buffer[offset], size2);
			ret = wilc->hif_func->hif_block_tx(wilc, addr,
							   dma_buffer, size2);
			if (ret)
				break;

			addr += size2;
			offset += size2;
			size -= size2;
		}
		release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

		if (ret) {
			pr_err("%s Bus error\n", __func__);
			goto fail;
		}
		pr_debug("%s Offset = %d\n", __func__, offset);
	} while (offset < buffer_size);

fail:

	kfree(dma_buffer);

	return ret;
}

int wilc_wlan_start(struct wilc *wilc)
{
	u32 reg = 0;
	int ret;
	u32 chipid;

	if (wilc->io_type == WILC_HIF_SDIO) {
		reg = 0;
		reg |= BIT(3);
	} else if (wilc->io_type == WILC_HIF_SPI) {
		reg = 1;
	}
	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
	ret = wilc->hif_func->hif_write_reg(wilc, WILC_VMM_CORE_CFG, reg);
	if (ret)
		goto release;

	reg = 0;
	if (wilc->io_type == WILC_HIF_SDIO && wilc->dev_irq_num)
		reg |= WILC_HAVE_SDIO_IRQ_GPIO;

	ret = wilc->hif_func->hif_write_reg(wilc, WILC_GP_REG_1, reg);
	if (ret)
		goto release;

	wilc->hif_func->hif_sync_ext(wilc, NUM_INT_EXT);

	ret = wilc->hif_func->hif_read_reg(wilc, WILC_CHIPID, &chipid);
	if (ret)
		goto release;

	wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);
	if ((reg & BIT(10)) == BIT(10)) {
		reg &= ~BIT(10);
		wilc->hif_func->hif_write_reg(wilc, WILC_GLB_RESET_0, reg);
		wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);
	}

	reg |= BIT(10);
	ret = wilc->hif_func->hif_write_reg(wilc, WILC_GLB_RESET_0, reg);
	wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);

release:
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);
	return ret;
}

int wilc_wlan_stop(struct wilc *wilc, struct wilc_vif *vif)
{
	u32 reg = 0;
	int ret;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

	ret = wilc->hif_func->hif_read_reg(wilc, WILC_GP_REG_0, &reg);
	if (ret) {
		netdev_err(vif->ndev, "Error while reading reg\n");
		goto release;
	}

	ret = wilc->hif_func->hif_write_reg(wilc, WILC_GP_REG_0,
					(reg | WILC_ABORT_REQ_BIT));
	if (ret) {
		netdev_err(vif->ndev, "Error while writing reg\n");
		goto release;
	}

	ret = wilc->hif_func->hif_read_reg(wilc, WILC_FW_HOST_COMM, &reg);
	if (ret) {
		netdev_err(vif->ndev, "Error while reading reg\n");
		goto release;
	}
	reg = BIT(0);

	ret = wilc->hif_func->hif_write_reg(wilc, WILC_FW_HOST_COMM, reg);
	if (ret) {
		netdev_err(vif->ndev, "Error while writing reg\n");
		goto release;
	}

	ret = 0;
release:
	/* host comm is disabled - we can't issue sleep command anymore: */
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);

	return ret;
}

void wilc_wlan_cleanup(struct net_device *dev)
{
	struct txq_entry_t *tqe;
	struct rxq_entry_t *rqe;
	u8 ac;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	wilc->quit = 1;
	for (ac = 0; ac < NQUEUES; ac++) {
		while ((tqe = wilc_wlan_txq_remove_from_head(wilc, ac))) {
			if (tqe->tx_complete_func)
				tqe->tx_complete_func(tqe->priv, 0);
			kfree(tqe);
		}
	}

	while ((rqe = wilc_wlan_rxq_remove(wilc)))
		kfree(rqe);

	kfree(wilc->vmm_table);
	wilc->vmm_table = NULL;
	kfree(wilc->rx_buffer);
	wilc->rx_buffer = NULL;
	kfree(wilc->tx_buffer);
	wilc->tx_buffer = NULL;
	wilc->hif_func->hif_deinit(wilc);
}

static int wilc_wlan_cfg_commit(struct wilc_vif *vif, int type,
				u32 drv_handler)
{
	struct wilc *wilc = vif->wilc;
	struct wilc_cfg_frame *cfg = &wilc->cfg_frame;
	int t_len = wilc->cfg_frame_offset + sizeof(struct wilc_cfg_cmd_hdr);

	if (type == WILC_CFG_SET)
		cfg->hdr.cmd_type = 'W';
	else
		cfg->hdr.cmd_type = 'Q';

	cfg->hdr.seq_no = wilc->cfg_seq_no % 256;
	cfg->hdr.total_len = cpu_to_le16(t_len);
	cfg->hdr.driver_handler = cpu_to_le32(drv_handler);
	wilc->cfg_seq_no = cfg->hdr.seq_no;

	if (!wilc_wlan_txq_add_cfg_pkt(vif, (u8 *)&cfg->hdr, t_len))
		return -1;

	return 0;
}

int wilc_wlan_cfg_set(struct wilc_vif *vif, int start, u16 wid, u8 *buffer,
		      u32 buffer_size, int commit, u32 drv_handler)
{
	u32 offset;
	int ret_size;
	struct wilc *wilc = vif->wilc;

	mutex_lock(&wilc->cfg_cmd_lock);

	if (start)
		wilc->cfg_frame_offset = 0;

	offset = wilc->cfg_frame_offset;
	ret_size = wilc_wlan_cfg_set_wid(wilc->cfg_frame.frame, offset,
					 wid, buffer, buffer_size);
	offset += ret_size;
	wilc->cfg_frame_offset = offset;

	if (!commit) {
		mutex_unlock(&wilc->cfg_cmd_lock);
		return ret_size;
	}

	netdev_dbg(vif->ndev, "%s: seqno[%d]\n", __func__, wilc->cfg_seq_no);

	if (wilc_wlan_cfg_commit(vif, WILC_CFG_SET, drv_handler))
		ret_size = 0;

	if (!wait_for_completion_timeout(&wilc->cfg_event,
					 WILC_CFG_PKTS_TIMEOUT)) {
		netdev_dbg(vif->ndev, "%s: Timed Out\n", __func__);
		ret_size = 0;
	}

	wilc->cfg_frame_offset = 0;
	wilc->cfg_seq_no += 1;
	mutex_unlock(&wilc->cfg_cmd_lock);

	return ret_size;
}

int wilc_wlan_cfg_get(struct wilc_vif *vif, int start, u16 wid, int commit,
		      u32 drv_handler)
{
	u32 offset;
	int ret_size;
	struct wilc *wilc = vif->wilc;

	mutex_lock(&wilc->cfg_cmd_lock);

	if (start)
		wilc->cfg_frame_offset = 0;

	offset = wilc->cfg_frame_offset;
	ret_size = wilc_wlan_cfg_get_wid(wilc->cfg_frame.frame, offset, wid);
	offset += ret_size;
	wilc->cfg_frame_offset = offset;

	if (!commit) {
		mutex_unlock(&wilc->cfg_cmd_lock);
		return ret_size;
	}

	if (wilc_wlan_cfg_commit(vif, WILC_CFG_QUERY, drv_handler))
		ret_size = 0;

	if (!wait_for_completion_timeout(&wilc->cfg_event,
					 WILC_CFG_PKTS_TIMEOUT)) {
		netdev_dbg(vif->ndev, "%s: Timed Out\n", __func__);
		ret_size = 0;
	}
	wilc->cfg_frame_offset = 0;
	wilc->cfg_seq_no += 1;
	mutex_unlock(&wilc->cfg_cmd_lock);

	return ret_size;
}

int wilc_send_config_pkt(struct wilc_vif *vif, u8 mode, struct wid *wids,
			 u32 count)
{
	int i;
	int ret = 0;
	u32 drv = wilc_get_vif_idx(vif);

	if (mode == WILC_GET_CFG) {
		for (i = 0; i < count; i++) {
			if (!wilc_wlan_cfg_get(vif, !i,
					       wids[i].id,
					       (i == count - 1),
					       drv)) {
				ret = -ETIMEDOUT;
				break;
			}
		}
		for (i = 0; i < count; i++) {
			wids[i].size = wilc_wlan_cfg_get_val(vif->wilc,
							     wids[i].id,
							     wids[i].val,
							     wids[i].size);
		}
	} else if (mode == WILC_SET_CFG) {
		for (i = 0; i < count; i++) {
			if (!wilc_wlan_cfg_set(vif, !i,
					       wids[i].id,
					       wids[i].val,
					       wids[i].size,
					       (i == count - 1),
					       drv)) {
				ret = -ETIMEDOUT;
				break;
			}
		}
	}

	return ret;
}

static int init_chip(struct net_device *dev)
{
	u32 chipid;
	u32 reg;
	int ret = 0;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);

	chipid = wilc_get_chipid(wilc, true);

	if ((chipid & 0xfff) != 0xa0) {
		ret = wilc->hif_func->hif_read_reg(wilc,
						   WILC_CORTUS_RESET_MUX_SEL,
						   &reg);
		if (ret) {
			netdev_err(dev, "fail read reg 0x1118\n");
			goto release;
		}
		reg |= BIT(0);
		ret = wilc->hif_func->hif_write_reg(wilc,
						    WILC_CORTUS_RESET_MUX_SEL,
						    reg);
		if (ret) {
			netdev_err(dev, "fail write reg 0x1118\n");
			goto release;
		}
		ret = wilc->hif_func->hif_write_reg(wilc,
						    WILC_CORTUS_BOOT_REGISTER,
						    WILC_CORTUS_BOOT_FROM_IRAM);
		if (ret) {
			netdev_err(dev, "fail write reg 0xc0000\n");
			goto release;
		}
	}

release:
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);

	return ret;
}

u32 wilc_get_chipid(struct wilc *wilc, bool update)
{
	u32 chipid = 0;
	u32 rfrevid = 0;

	if (wilc->chipid == 0 || update) {
		wilc->hif_func->hif_read_reg(wilc, WILC_CHIPID, &chipid);
		wilc->hif_func->hif_read_reg(wilc, WILC_RF_REVISION_ID,
					     &rfrevid);
		if (!is_wilc1000(chipid)) {
			wilc->chipid = 0;
			return wilc->chipid;
		}
		if (chipid == WILC_1000_BASE_ID_2A) { /* 0x1002A0 */
			if (rfrevid != 0x1)
				chipid = WILC_1000_BASE_ID_2A_REV1;
		} else if (chipid == WILC_1000_BASE_ID_2B) { /* 0x1002B0 */
			if (rfrevid == 0x4)
				chipid = WILC_1000_BASE_ID_2B_REV1;
			else if (rfrevid != 0x3)
				chipid = WILC_1000_BASE_ID_2B_REV2;
		}

		wilc->chipid = chipid;
	}
	return wilc->chipid;
}

int wilc_wlan_init(struct net_device *dev)
{
	int ret = 0;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc;

	wilc = vif->wilc;

	wilc->quit = 0;

	if (!wilc->hif_func->hif_is_init(wilc)) {
		acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
		ret = wilc->hif_func->hif_init(wilc, false);
		release_bus(wilc, WILC_BUS_RELEASE_ONLY);
		if (ret)
			goto fail;
	}

	if (!wilc->vmm_table)
		wilc->vmm_table = kcalloc(WILC_VMM_TBL_SIZE, sizeof(u32), GFP_KERNEL);

	if (!wilc->vmm_table) {
		ret = -ENOBUFS;
		goto fail;
	}

	if (!wilc->tx_buffer)
		wilc->tx_buffer = kmalloc(WILC_TX_BUFF_SIZE, GFP_KERNEL);

	if (!wilc->tx_buffer) {
		ret = -ENOBUFS;
		goto fail;
	}

	if (!wilc->rx_buffer)
		wilc->rx_buffer = kmalloc(WILC_RX_BUFF_SIZE, GFP_KERNEL);

	if (!wilc->rx_buffer) {
		ret = -ENOBUFS;
		goto fail;
	}

	if (init_chip(dev)) {
		ret = -EIO;
		goto fail;
	}

	return 0;

fail:
	kfree(wilc->vmm_table);
	wilc->vmm_table = NULL;
	kfree(wilc->rx_buffer);
	wilc->rx_buffer = NULL;
	kfree(wilc->tx_buffer);
	wilc->tx_buffer = NULL;

	return ret;
}
