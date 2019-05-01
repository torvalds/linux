/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/irq.h>

#include "mt76x02.h"
#include "mt76x02_mcu.h"
#include "mt76x02_trace.h"

static void mt76x02_pre_tbtt_tasklet(unsigned long arg)
{
	struct mt76x02_dev *dev = (struct mt76x02_dev *)arg;
	struct mt76_queue *q = dev->mt76.q_tx[MT_TXQ_PSD].q;
	struct beacon_bc_data data = {};
	struct sk_buff *skb;
	int i;

	if (mt76_hw(dev)->conf.flags & IEEE80211_CONF_OFFCHANNEL)
		return;

	mt76x02_resync_beacon_timer(dev);

	ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt76x02_update_beacon_iter, dev);

	mt76_csa_check(&dev->mt76);

	if (dev->mt76.csa_complete)
		return;

	mt76x02_enqueue_buffered_bc(dev, &data, 8);

	if (!skb_queue_len(&data.q))
		return;

	for (i = 0; i < ARRAY_SIZE(data.tail); i++) {
		if (!data.tail[i])
			continue;

		mt76_skb_set_moredata(data.tail[i], false);
	}

	spin_lock_bh(&q->lock);
	while ((skb = __skb_dequeue(&data.q)) != NULL) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct ieee80211_vif *vif = info->control.vif;
		struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;

		mt76_tx_queue_skb(dev, MT_TXQ_PSD, skb, &mvif->group_wcid,
				  NULL);
	}
	spin_unlock_bh(&q->lock);
}

static void mt76x02e_pre_tbtt_enable(struct mt76x02_dev *dev, bool en)
{
	if (en)
		tasklet_enable(&dev->mt76.pre_tbtt_tasklet);
	else
		tasklet_disable(&dev->mt76.pre_tbtt_tasklet);
}

static void mt76x02e_beacon_enable(struct mt76x02_dev *dev, bool en)
{
	mt76_rmw_field(dev, MT_INT_TIMER_EN, MT_INT_TIMER_EN_PRE_TBTT_EN, en);
	if (en)
		mt76x02_irq_enable(dev, MT_INT_PRE_TBTT | MT_INT_TBTT);
	else
		mt76x02_irq_disable(dev, MT_INT_PRE_TBTT | MT_INT_TBTT);
}

void mt76x02e_init_beacon_config(struct mt76x02_dev *dev)
{
	static const struct mt76x02_beacon_ops beacon_ops = {
		.nslots = 8,
		.slot_size = 1024,
		.pre_tbtt_enable = mt76x02e_pre_tbtt_enable,
		.beacon_enable = mt76x02e_beacon_enable,
	};

	dev->beacon_ops = &beacon_ops;

	/* Fire a pre-TBTT interrupt 8 ms before TBTT */
	mt76_rmw_field(dev, MT_INT_TIMER_CFG, MT_INT_TIMER_CFG_PRE_TBTT, 8 << 4);
	mt76_rmw_field(dev, MT_INT_TIMER_CFG, MT_INT_TIMER_CFG_GP_TIMER,
		       MT_DFS_GP_INTERVAL);
	mt76_wr(dev, MT_INT_TIMER_EN, 0);

	mt76x02_init_beacon_config(dev);
}
EXPORT_SYMBOL_GPL(mt76x02e_init_beacon_config);

static int
mt76x02_init_tx_queue(struct mt76x02_dev *dev, struct mt76_sw_queue *q,
		      int idx, int n_desc)
{
	struct mt76_queue *hwq;
	int err;

	hwq = devm_kzalloc(dev->mt76.dev, sizeof(*hwq), GFP_KERNEL);
	if (!hwq)
		return -ENOMEM;

	err = mt76_queue_alloc(dev, hwq, idx, n_desc, 0, MT_TX_RING_BASE);
	if (err < 0)
		return err;

	INIT_LIST_HEAD(&q->swq);
	q->q = hwq;

	mt76x02_irq_enable(dev, MT_INT_TX_DONE(idx));

	return 0;
}

static int
mt76x02_init_rx_queue(struct mt76x02_dev *dev, struct mt76_queue *q,
		      int idx, int n_desc, int bufsize)
{
	int err;

	err = mt76_queue_alloc(dev, q, idx, n_desc, bufsize,
			       MT_RX_RING_BASE);
	if (err < 0)
		return err;

	mt76x02_irq_enable(dev, MT_INT_RX_DONE(idx));

	return 0;
}

static void mt76x02_process_tx_status_fifo(struct mt76x02_dev *dev)
{
	struct mt76x02_tx_status stat;
	u8 update = 1;

	while (kfifo_get(&dev->txstatus_fifo, &stat))
		mt76x02_send_tx_status(dev, &stat, &update);
}

static void mt76x02_tx_tasklet(unsigned long data)
{
	struct mt76x02_dev *dev = (struct mt76x02_dev *)data;

	mt76x02_mac_poll_tx_status(dev, false);
	mt76x02_process_tx_status_fifo(dev);

	mt76_txq_schedule_all(&dev->mt76);
}

static int mt76x02_poll_tx(struct napi_struct *napi, int budget)
{
	struct mt76x02_dev *dev = container_of(napi, struct mt76x02_dev,
					       mt76.tx_napi);
	int i;

	mt76x02_mac_poll_tx_status(dev, false);

	for (i = MT_TXQ_MCU; i >= 0; i--)
		mt76_queue_tx_cleanup(dev, i, false);

	if (napi_complete_done(napi, 0))
		mt76x02_irq_enable(dev, MT_INT_TX_DONE_ALL);

	for (i = MT_TXQ_MCU; i >= 0; i--)
		mt76_queue_tx_cleanup(dev, i, false);

	tasklet_schedule(&dev->mt76.tx_tasklet);

	return 0;
}

int mt76x02_dma_init(struct mt76x02_dev *dev)
{
	struct mt76_txwi_cache __maybe_unused *t;
	int i, ret, fifo_size;
	struct mt76_queue *q;
	void *status_fifo;

	BUILD_BUG_ON(sizeof(struct mt76x02_rxwi) > MT_RX_HEADROOM);

	fifo_size = roundup_pow_of_two(32 * sizeof(struct mt76x02_tx_status));
	status_fifo = devm_kzalloc(dev->mt76.dev, fifo_size, GFP_KERNEL);
	if (!status_fifo)
		return -ENOMEM;

	tasklet_init(&dev->mt76.tx_tasklet, mt76x02_tx_tasklet,
		     (unsigned long) dev);
	tasklet_init(&dev->mt76.pre_tbtt_tasklet, mt76x02_pre_tbtt_tasklet,
		     (unsigned long)dev);

	spin_lock_init(&dev->txstatus_fifo_lock);
	kfifo_init(&dev->txstatus_fifo, status_fifo, fifo_size);

	mt76_dma_attach(&dev->mt76);

	mt76_wr(dev, MT_WPDMA_RST_IDX, ~0);

	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		ret = mt76x02_init_tx_queue(dev, &dev->mt76.q_tx[i],
					    mt76_ac_to_hwq(i),
					    MT_TX_RING_SIZE);
		if (ret)
			return ret;
	}

	ret = mt76x02_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_PSD],
				    MT_TX_HW_QUEUE_MGMT, MT_TX_RING_SIZE);
	if (ret)
		return ret;

	ret = mt76x02_init_tx_queue(dev, &dev->mt76.q_tx[MT_TXQ_MCU],
				    MT_TX_HW_QUEUE_MCU, MT_MCU_RING_SIZE);
	if (ret)
		return ret;

	ret = mt76x02_init_rx_queue(dev, &dev->mt76.q_rx[MT_RXQ_MCU], 1,
				    MT_MCU_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	q = &dev->mt76.q_rx[MT_RXQ_MAIN];
	q->buf_offset = MT_RX_HEADROOM - sizeof(struct mt76x02_rxwi);
	ret = mt76x02_init_rx_queue(dev, q, 0, MT76X02_RX_RING_SIZE,
				    MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	ret = mt76_init_queues(dev);
	if (ret)
		return ret;

	netif_tx_napi_add(&dev->mt76.napi_dev, &dev->mt76.tx_napi,
			  mt76x02_poll_tx, NAPI_POLL_WEIGHT);
	napi_enable(&dev->mt76.tx_napi);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_dma_init);

void mt76x02_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	struct mt76x02_dev *dev;

	dev = container_of(mdev, struct mt76x02_dev, mt76);
	mt76x02_irq_enable(dev, MT_INT_RX_DONE(q));
}
EXPORT_SYMBOL_GPL(mt76x02_rx_poll_complete);

irqreturn_t mt76x02_irq_handler(int irq, void *dev_instance)
{
	struct mt76x02_dev *dev = dev_instance;
	u32 intr;

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mt76.state))
		return IRQ_NONE;

	trace_dev_irq(dev, intr, dev->mt76.mmio.irqmask);

	intr &= dev->mt76.mmio.irqmask;

	if (intr & MT_INT_RX_DONE(0)) {
		mt76x02_irq_disable(dev, MT_INT_RX_DONE(0));
		napi_schedule(&dev->mt76.napi[0]);
	}

	if (intr & MT_INT_RX_DONE(1)) {
		mt76x02_irq_disable(dev, MT_INT_RX_DONE(1));
		napi_schedule(&dev->mt76.napi[1]);
	}

	if (intr & MT_INT_PRE_TBTT)
		tasklet_schedule(&dev->mt76.pre_tbtt_tasklet);

	/* send buffered multicast frames now */
	if (intr & MT_INT_TBTT) {
		if (dev->mt76.csa_complete)
			mt76_csa_finish(&dev->mt76);
		else
			mt76_queue_kick(dev, dev->mt76.q_tx[MT_TXQ_PSD].q);
	}

	if (intr & MT_INT_TX_STAT)
		mt76x02_mac_poll_tx_status(dev, true);

	if (intr & (MT_INT_TX_STAT | MT_INT_TX_DONE_ALL)) {
		mt76x02_irq_disable(dev, MT_INT_TX_DONE_ALL);
		napi_schedule(&dev->mt76.tx_napi);
	}

	if (intr & MT_INT_GPTIMER) {
		mt76x02_irq_disable(dev, MT_INT_GPTIMER);
		tasklet_schedule(&dev->dfs_pd.dfs_tasklet);
	}

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(mt76x02_irq_handler);

static void mt76x02_dma_enable(struct mt76x02_dev *dev)
{
	u32 val;

	mt76_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);
	mt76x02_wait_for_wpdma(&dev->mt76, 1000);
	usleep_range(50, 100);

	val = FIELD_PREP(MT_WPDMA_GLO_CFG_DMA_BURST_SIZE, 3) |
	      MT_WPDMA_GLO_CFG_TX_DMA_EN |
	      MT_WPDMA_GLO_CFG_RX_DMA_EN;
	mt76_set(dev, MT_WPDMA_GLO_CFG, val);
	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);
}

void mt76x02_dma_cleanup(struct mt76x02_dev *dev)
{
	tasklet_kill(&dev->mt76.tx_tasklet);
	mt76_dma_cleanup(&dev->mt76);
}
EXPORT_SYMBOL_GPL(mt76x02_dma_cleanup);

void mt76x02_dma_disable(struct mt76x02_dev *dev)
{
	u32 val = mt76_rr(dev, MT_WPDMA_GLO_CFG);

	val &= MT_WPDMA_GLO_CFG_DMA_BURST_SIZE |
	       MT_WPDMA_GLO_CFG_BIG_ENDIAN |
	       MT_WPDMA_GLO_CFG_HDR_SEG_LEN;
	val |= MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE;
	mt76_wr(dev, MT_WPDMA_GLO_CFG, val);
}
EXPORT_SYMBOL_GPL(mt76x02_dma_disable);

void mt76x02_mac_start(struct mt76x02_dev *dev)
{
	mt76x02_dma_enable(dev);
	mt76_wr(dev, MT_RX_FILTR_CFG, dev->mt76.rxfilter);
	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);
	mt76x02_irq_enable(dev,
			   MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL |
			   MT_INT_TX_STAT);
}
EXPORT_SYMBOL_GPL(mt76x02_mac_start);

static bool mt76x02_tx_hang(struct mt76x02_dev *dev)
{
	u32 dma_idx, prev_dma_idx;
	struct mt76_queue *q;
	int i;

	for (i = 0; i < 4; i++) {
		q = dev->mt76.q_tx[i].q;

		if (!q->queued)
			continue;

		prev_dma_idx = dev->mt76.tx_dma_idx[i];
		dma_idx = readl(&q->regs->dma_idx);
		dev->mt76.tx_dma_idx[i] = dma_idx;

		if (prev_dma_idx == dma_idx)
			break;
	}

	return i < 4;
}

static void mt76x02_key_sync(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct ieee80211_key_conf *key, void *data)
{
	struct mt76x02_dev *dev = hw->priv;
	struct mt76_wcid *wcid;

	if (!sta)
	    return;

	wcid = (struct mt76_wcid *) sta->drv_priv;

	if (wcid->hw_key_idx != key->keyidx || wcid->sw_iv)
	    return;

	mt76x02_mac_wcid_sync_pn(dev, wcid->idx, key);
}

static void mt76x02_reset_state(struct mt76x02_dev *dev)
{
	int i;

	lockdep_assert_held(&dev->mt76.mutex);

	clear_bit(MT76_STATE_RUNNING, &dev->mt76.state);

	rcu_read_lock();
	ieee80211_iter_keys_rcu(dev->mt76.hw, NULL, mt76x02_key_sync, NULL);
	rcu_read_unlock();

	for (i = 0; i < ARRAY_SIZE(dev->mt76.wcid); i++) {
		struct ieee80211_sta *sta;
		struct ieee80211_vif *vif;
		struct mt76x02_sta *msta;
		struct mt76_wcid *wcid;
		void *priv;

		wcid = rcu_dereference_protected(dev->mt76.wcid[i],
					lockdep_is_held(&dev->mt76.mutex));
		if (!wcid)
			continue;

		priv = msta = container_of(wcid, struct mt76x02_sta, wcid);
		sta = container_of(priv, struct ieee80211_sta, drv_priv);

		priv = msta->vif;
		vif = container_of(priv, struct ieee80211_vif, drv_priv);

		__mt76_sta_remove(&dev->mt76, vif, sta);
		memset(msta, 0, sizeof(*msta));
	}

	dev->vif_mask = 0;
	dev->mt76.beacon_mask = 0;
}

static void mt76x02_watchdog_reset(struct mt76x02_dev *dev)
{
	u32 mask = dev->mt76.mmio.irqmask;
	bool restart = dev->mt76.mcu_ops->mcu_restart;
	int i;

	ieee80211_stop_queues(dev->mt76.hw);
	set_bit(MT76_RESET, &dev->mt76.state);

	tasklet_disable(&dev->mt76.pre_tbtt_tasklet);
	tasklet_disable(&dev->mt76.tx_tasklet);
	napi_disable(&dev->mt76.tx_napi);

	for (i = 0; i < ARRAY_SIZE(dev->mt76.napi); i++)
		napi_disable(&dev->mt76.napi[i]);

	mutex_lock(&dev->mt76.mutex);

	if (restart)
		mt76x02_reset_state(dev);

	if (dev->mt76.beacon_mask)
		mt76_clear(dev, MT_BEACON_TIME_CFG,
			   MT_BEACON_TIME_CFG_BEACON_TX |
			   MT_BEACON_TIME_CFG_TBTT_EN);

	mt76x02_irq_disable(dev, mask);

	/* perform device reset */
	mt76_clear(dev, MT_TXOP_CTRL_CFG, MT_TXOP_ED_CCA_EN);
	mt76_wr(dev, MT_MAC_SYS_CTRL, 0);
	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_TX_DMA_EN | MT_WPDMA_GLO_CFG_RX_DMA_EN);
	usleep_range(5000, 10000);
	mt76_wr(dev, MT_INT_SOURCE_CSR, 0xffffffff);

	/* let fw reset DMA */
	mt76_set(dev, 0x734, 0x3);

	if (restart)
		mt76_mcu_restart(dev);

	for (i = 0; i < ARRAY_SIZE(dev->mt76.q_tx); i++)
		mt76_queue_tx_cleanup(dev, i, true);

	for (i = 0; i < ARRAY_SIZE(dev->mt76.q_rx); i++)
		mt76_queue_rx_reset(dev, i);

	mt76x02_mac_start(dev);

	if (dev->ed_monitor)
		mt76_set(dev, MT_TXOP_CTRL_CFG, MT_TXOP_ED_CCA_EN);

	if (dev->mt76.beacon_mask && !restart)
		mt76_set(dev, MT_BEACON_TIME_CFG,
			 MT_BEACON_TIME_CFG_BEACON_TX |
			 MT_BEACON_TIME_CFG_TBTT_EN);

	mt76x02_irq_enable(dev, mask);

	mutex_unlock(&dev->mt76.mutex);

	clear_bit(MT76_RESET, &dev->mt76.state);

	tasklet_enable(&dev->mt76.tx_tasklet);
	napi_enable(&dev->mt76.tx_napi);
	napi_schedule(&dev->mt76.tx_napi);

	tasklet_enable(&dev->mt76.pre_tbtt_tasklet);

	for (i = 0; i < ARRAY_SIZE(dev->mt76.napi); i++) {
		napi_enable(&dev->mt76.napi[i]);
		napi_schedule(&dev->mt76.napi[i]);
	}

	if (restart) {
		mt76x02_mcu_function_select(dev, Q_SELECT, 1);
		ieee80211_restart_hw(dev->mt76.hw);
	} else {
		ieee80211_wake_queues(dev->mt76.hw);
		mt76_txq_schedule_all(&dev->mt76);
	}
}

static void mt76x02_check_tx_hang(struct mt76x02_dev *dev)
{
	if (mt76x02_tx_hang(dev)) {
		if (++dev->tx_hang_check >= MT_TX_HANG_TH)
			goto restart;
	} else {
		dev->tx_hang_check = 0;
	}

	if (dev->mcu_timeout)
		goto restart;

	return;

restart:
	mt76x02_watchdog_reset(dev);

	mutex_lock(&dev->mt76.mmio.mcu.mutex);
	dev->mcu_timeout = 0;
	mutex_unlock(&dev->mt76.mmio.mcu.mutex);

	dev->tx_hang_reset++;
	dev->tx_hang_check = 0;
	memset(dev->mt76.tx_dma_idx, 0xff,
	       sizeof(dev->mt76.tx_dma_idx));
}

void mt76x02_wdt_work(struct work_struct *work)
{
	struct mt76x02_dev *dev = container_of(work, struct mt76x02_dev,
					       wdt_work.work);

	mt76x02_check_tx_hang(dev);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->wdt_work,
				     MT_WATCHDOG_TIME);
}
