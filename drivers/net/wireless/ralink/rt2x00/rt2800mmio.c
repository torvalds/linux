/*	Copyright (C) 2009 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
 *	Copyright (C) 2009 Alban Browaeys <prahal@yahoo.com>
 *	Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
 *	Copyright (C) 2009 Luis Correia <luis.f.correia@gmail.com>
 *	Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
 *	Copyright (C) 2009 Mark Asselstine <asselsm@gmail.com>
 *	Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
 *	Copyright (C) 2009 Bart Zolnierkiewicz <bzolnier@gmail.com>
 *	<http://rt2x00.serialmonkey.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*	Module: rt2800mmio
 *	Abstract: rt2800 MMIO device routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>

#include "rt2x00.h"
#include "rt2x00mmio.h"
#include "rt2800.h"
#include "rt2800lib.h"
#include "rt2800mmio.h"

/*
 * TX descriptor initialization
 */
__le32 *rt2800mmio_get_txwi(struct queue_entry *entry)
{
	return (__le32 *) entry->skb->data;
}
EXPORT_SYMBOL_GPL(rt2800mmio_get_txwi);

void rt2800mmio_write_tx_desc(struct queue_entry *entry,
			      struct txentry_desc *txdesc)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	struct queue_entry_priv_mmio *entry_priv = entry->priv_data;
	__le32 *txd = entry_priv->desc;
	u32 word;
	const unsigned int txwi_size = entry->queue->winfo_size;

	/*
	 * The buffers pointed by SD_PTR0/SD_LEN0 and SD_PTR1/SD_LEN1
	 * must contains a TXWI structure + 802.11 header + padding + 802.11
	 * data. We choose to have SD_PTR0/SD_LEN0 only contains TXWI and
	 * SD_PTR1/SD_LEN1 contains 802.11 header + padding + 802.11
	 * data. It means that LAST_SEC0 is always 0.
	 */

	/*
	 * Initialize TX descriptor
	 */
	word = 0;
	rt2x00_set_field32(&word, TXD_W0_SD_PTR0, skbdesc->skb_dma);
	rt2x00_desc_write(txd, 0, word);

	word = 0;
	rt2x00_set_field32(&word, TXD_W1_SD_LEN1, entry->skb->len);
	rt2x00_set_field32(&word, TXD_W1_LAST_SEC1,
			   !test_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W1_BURST,
			   test_bit(ENTRY_TXD_BURST, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W1_SD_LEN0, txwi_size);
	rt2x00_set_field32(&word, TXD_W1_LAST_SEC0, 0);
	rt2x00_set_field32(&word, TXD_W1_DMA_DONE, 0);
	rt2x00_desc_write(txd, 1, word);

	word = 0;
	rt2x00_set_field32(&word, TXD_W2_SD_PTR1,
			   skbdesc->skb_dma + txwi_size);
	rt2x00_desc_write(txd, 2, word);

	word = 0;
	rt2x00_set_field32(&word, TXD_W3_WIV,
			   !test_bit(ENTRY_TXD_ENCRYPT_IV, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W3_QSEL, 2);
	rt2x00_desc_write(txd, 3, word);

	/*
	 * Register descriptor details in skb frame descriptor.
	 */
	skbdesc->desc = txd;
	skbdesc->desc_len = TXD_DESC_SIZE;
}
EXPORT_SYMBOL_GPL(rt2800mmio_write_tx_desc);

/*
 * RX control handlers
 */
void rt2800mmio_fill_rxdone(struct queue_entry *entry,
			    struct rxdone_entry_desc *rxdesc)
{
	struct queue_entry_priv_mmio *entry_priv = entry->priv_data;
	__le32 *rxd = entry_priv->desc;
	u32 word;

	word = rt2x00_desc_read(rxd, 3);

	if (rt2x00_get_field32(word, RXD_W3_CRC_ERROR))
		rxdesc->flags |= RX_FLAG_FAILED_FCS_CRC;

	/*
	 * Unfortunately we don't know the cipher type used during
	 * decryption. This prevents us from correct providing
	 * correct statistics through debugfs.
	 */
	rxdesc->cipher_status = rt2x00_get_field32(word, RXD_W3_CIPHER_ERROR);

	if (rt2x00_get_field32(word, RXD_W3_DECRYPTED)) {
		/*
		 * Hardware has stripped IV/EIV data from 802.11 frame during
		 * decryption. Unfortunately the descriptor doesn't contain
		 * any fields with the EIV/IV data either, so they can't
		 * be restored by rt2x00lib.
		 */
		rxdesc->flags |= RX_FLAG_IV_STRIPPED;

		/*
		 * The hardware has already checked the Michael Mic and has
		 * stripped it from the frame. Signal this to mac80211.
		 */
		rxdesc->flags |= RX_FLAG_MMIC_STRIPPED;

		if (rxdesc->cipher_status == RX_CRYPTO_SUCCESS) {
			rxdesc->flags |= RX_FLAG_DECRYPTED;
		} else if (rxdesc->cipher_status == RX_CRYPTO_FAIL_MIC) {
			/*
			 * In order to check the Michael Mic, the packet must have
			 * been decrypted.  Mac80211 doesnt check the MMIC failure 
			 * flag to initiate MMIC countermeasures if the decoded flag
			 * has not been set.
			 */
			rxdesc->flags |= RX_FLAG_DECRYPTED;

			rxdesc->flags |= RX_FLAG_MMIC_ERROR;
		}
	}

	if (rt2x00_get_field32(word, RXD_W3_MY_BSS))
		rxdesc->dev_flags |= RXDONE_MY_BSS;

	if (rt2x00_get_field32(word, RXD_W3_L2PAD))
		rxdesc->dev_flags |= RXDONE_L2PAD;

	/*
	 * Process the RXWI structure that is at the start of the buffer.
	 */
	rt2800_process_rxwi(entry, rxdesc);
}
EXPORT_SYMBOL_GPL(rt2800mmio_fill_rxdone);

/*
 * Interrupt functions.
 */
static void rt2800mmio_wakeup(struct rt2x00_dev *rt2x00dev)
{
	struct ieee80211_conf conf = { .flags = 0 };
	struct rt2x00lib_conf libconf = { .conf = &conf };

	rt2800_config(rt2x00dev, &libconf, IEEE80211_CONF_CHANGE_PS);
}

static inline void rt2800mmio_enable_interrupt(struct rt2x00_dev *rt2x00dev,
					       struct rt2x00_field32 irq_field)
{
	u32 reg;

	/*
	 * Enable a single interrupt. The interrupt mask register
	 * access needs locking.
	 */
	spin_lock_irq(&rt2x00dev->irqmask_lock);
	reg = rt2x00mmio_register_read(rt2x00dev, INT_MASK_CSR);
	rt2x00_set_field32(&reg, irq_field, 1);
	rt2x00mmio_register_write(rt2x00dev, INT_MASK_CSR, reg);
	spin_unlock_irq(&rt2x00dev->irqmask_lock);
}

void rt2800mmio_pretbtt_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;
	rt2x00lib_pretbtt(rt2x00dev);
	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2800mmio_enable_interrupt(rt2x00dev, INT_MASK_CSR_PRE_TBTT);
}
EXPORT_SYMBOL_GPL(rt2800mmio_pretbtt_tasklet);

void rt2800mmio_tbtt_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;
	struct rt2800_drv_data *drv_data = rt2x00dev->drv_data;
	u32 reg;

	rt2x00lib_beacondone(rt2x00dev);

	if (rt2x00dev->intf_ap_count) {
		/*
		 * The rt2800pci hardware tbtt timer is off by 1us per tbtt
		 * causing beacon skew and as a result causing problems with
		 * some powersaving clients over time. Shorten the beacon
		 * interval every 64 beacons by 64us to mitigate this effect.
		 */
		if (drv_data->tbtt_tick == (BCN_TBTT_OFFSET - 2)) {
			reg = rt2x00mmio_register_read(rt2x00dev, BCN_TIME_CFG);
			rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_INTERVAL,
					   (rt2x00dev->beacon_int * 16) - 1);
			rt2x00mmio_register_write(rt2x00dev, BCN_TIME_CFG, reg);
		} else if (drv_data->tbtt_tick == (BCN_TBTT_OFFSET - 1)) {
			reg = rt2x00mmio_register_read(rt2x00dev, BCN_TIME_CFG);
			rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_INTERVAL,
					   (rt2x00dev->beacon_int * 16));
			rt2x00mmio_register_write(rt2x00dev, BCN_TIME_CFG, reg);
		}
		drv_data->tbtt_tick++;
		drv_data->tbtt_tick %= BCN_TBTT_OFFSET;
	}

	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2800mmio_enable_interrupt(rt2x00dev, INT_MASK_CSR_TBTT);
}
EXPORT_SYMBOL_GPL(rt2800mmio_tbtt_tasklet);

void rt2800mmio_rxdone_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;
	if (rt2x00mmio_rxdone(rt2x00dev))
		tasklet_schedule(&rt2x00dev->rxdone_tasklet);
	else if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2800mmio_enable_interrupt(rt2x00dev, INT_MASK_CSR_RX_DONE);
}
EXPORT_SYMBOL_GPL(rt2800mmio_rxdone_tasklet);

void rt2800mmio_autowake_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;
	rt2800mmio_wakeup(rt2x00dev);
	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2800mmio_enable_interrupt(rt2x00dev,
					    INT_MASK_CSR_AUTO_WAKEUP);
}
EXPORT_SYMBOL_GPL(rt2800mmio_autowake_tasklet);

static void rt2800mmio_fetch_txstatus(struct rt2x00_dev *rt2x00dev)
{
	u32 status;
	unsigned long flags;

	/*
	 * The TX_FIFO_STATUS interrupt needs special care. We should
	 * read TX_STA_FIFO but we should do it immediately as otherwise
	 * the register can overflow and we would lose status reports.
	 *
	 * Hence, read the TX_STA_FIFO register and copy all tx status
	 * reports into a kernel FIFO which is handled in the txstatus
	 * tasklet. We use a tasklet to process the tx status reports
	 * because we can schedule the tasklet multiple times (when the
	 * interrupt fires again during tx status processing).
	 *
	 * We also read statuses from tx status timeout timer, use
	 * lock to prevent concurent writes to fifo.
	 */

	spin_lock_irqsave(&rt2x00dev->irqmask_lock, flags);

	while (!kfifo_is_full(&rt2x00dev->txstatus_fifo)) {
		status = rt2x00mmio_register_read(rt2x00dev, TX_STA_FIFO);
		if (!rt2x00_get_field32(status, TX_STA_FIFO_VALID))
			break;

		kfifo_put(&rt2x00dev->txstatus_fifo, status);
	}

	spin_unlock_irqrestore(&rt2x00dev->irqmask_lock, flags);
}

void rt2800mmio_txstatus_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;

	rt2800_txdone(rt2x00dev, 16);

	if (!kfifo_is_empty(&rt2x00dev->txstatus_fifo))
		tasklet_schedule(&rt2x00dev->txstatus_tasklet);

}
EXPORT_SYMBOL_GPL(rt2800mmio_txstatus_tasklet);

irqreturn_t rt2800mmio_interrupt(int irq, void *dev_instance)
{
	struct rt2x00_dev *rt2x00dev = dev_instance;
	u32 reg, mask;

	/* Read status and ACK all interrupts */
	reg = rt2x00mmio_register_read(rt2x00dev, INT_SOURCE_CSR);
	rt2x00mmio_register_write(rt2x00dev, INT_SOURCE_CSR, reg);

	if (!reg)
		return IRQ_NONE;

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return IRQ_HANDLED;

	/*
	 * Since INT_MASK_CSR and INT_SOURCE_CSR use the same bits
	 * for interrupts and interrupt masks we can just use the value of
	 * INT_SOURCE_CSR to create the interrupt mask.
	 */
	mask = ~reg;

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_TX_FIFO_STATUS)) {
		rt2x00_set_field32(&mask, INT_MASK_CSR_TX_FIFO_STATUS, 1);
		rt2800mmio_fetch_txstatus(rt2x00dev);
		if (!kfifo_is_empty(&rt2x00dev->txstatus_fifo))
			tasklet_schedule(&rt2x00dev->txstatus_tasklet);
	}

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_PRE_TBTT))
		tasklet_hi_schedule(&rt2x00dev->pretbtt_tasklet);

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_TBTT))
		tasklet_hi_schedule(&rt2x00dev->tbtt_tasklet);

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_RX_DONE))
		tasklet_schedule(&rt2x00dev->rxdone_tasklet);

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_AUTO_WAKEUP))
		tasklet_schedule(&rt2x00dev->autowake_tasklet);

	/*
	 * Disable all interrupts for which a tasklet was scheduled right now,
	 * the tasklet will reenable the appropriate interrupts.
	 */
	spin_lock(&rt2x00dev->irqmask_lock);
	reg = rt2x00mmio_register_read(rt2x00dev, INT_MASK_CSR);
	reg &= mask;
	rt2x00mmio_register_write(rt2x00dev, INT_MASK_CSR, reg);
	spin_unlock(&rt2x00dev->irqmask_lock);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(rt2800mmio_interrupt);

void rt2800mmio_toggle_irq(struct rt2x00_dev *rt2x00dev,
			   enum dev_state state)
{
	u32 reg;
	unsigned long flags;

	/*
	 * When interrupts are being enabled, the interrupt registers
	 * should clear the register to assure a clean state.
	 */
	if (state == STATE_RADIO_IRQ_ON) {
		reg = rt2x00mmio_register_read(rt2x00dev, INT_SOURCE_CSR);
		rt2x00mmio_register_write(rt2x00dev, INT_SOURCE_CSR, reg);
	}

	spin_lock_irqsave(&rt2x00dev->irqmask_lock, flags);
	reg = 0;
	if (state == STATE_RADIO_IRQ_ON) {
		rt2x00_set_field32(&reg, INT_MASK_CSR_RX_DONE, 1);
		rt2x00_set_field32(&reg, INT_MASK_CSR_TBTT, 1);
		rt2x00_set_field32(&reg, INT_MASK_CSR_PRE_TBTT, 1);
		rt2x00_set_field32(&reg, INT_MASK_CSR_TX_FIFO_STATUS, 1);
		rt2x00_set_field32(&reg, INT_MASK_CSR_AUTO_WAKEUP, 1);
	}
	rt2x00mmio_register_write(rt2x00dev, INT_MASK_CSR, reg);
	spin_unlock_irqrestore(&rt2x00dev->irqmask_lock, flags);

	if (state == STATE_RADIO_IRQ_OFF) {
		/*
		 * Wait for possibly running tasklets to finish.
		 */
		tasklet_kill(&rt2x00dev->txstatus_tasklet);
		tasklet_kill(&rt2x00dev->rxdone_tasklet);
		tasklet_kill(&rt2x00dev->autowake_tasklet);
		tasklet_kill(&rt2x00dev->tbtt_tasklet);
		tasklet_kill(&rt2x00dev->pretbtt_tasklet);
	}
}
EXPORT_SYMBOL_GPL(rt2800mmio_toggle_irq);

/*
 * Queue handlers.
 */
void rt2800mmio_start_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	u32 reg;

	switch (queue->qid) {
	case QID_RX:
		reg = rt2x00mmio_register_read(rt2x00dev, MAC_SYS_CTRL);
		rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 1);
		rt2x00mmio_register_write(rt2x00dev, MAC_SYS_CTRL, reg);
		break;
	case QID_BEACON:
		reg = rt2x00mmio_register_read(rt2x00dev, BCN_TIME_CFG);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_TICKING, 1);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TBTT_ENABLE, 1);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 1);
		rt2x00mmio_register_write(rt2x00dev, BCN_TIME_CFG, reg);

		reg = rt2x00mmio_register_read(rt2x00dev, INT_TIMER_EN);
		rt2x00_set_field32(&reg, INT_TIMER_EN_PRE_TBTT_TIMER, 1);
		rt2x00mmio_register_write(rt2x00dev, INT_TIMER_EN, reg);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(rt2800mmio_start_queue);

/* 200 ms */
#define TXSTATUS_TIMEOUT 200000000

void rt2800mmio_kick_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	struct queue_entry *entry;

	switch (queue->qid) {
	case QID_AC_VO:
	case QID_AC_VI:
	case QID_AC_BE:
	case QID_AC_BK:
		WARN_ON_ONCE(rt2x00queue_empty(queue));
		entry = rt2x00queue_get_entry(queue, Q_INDEX);
		rt2x00mmio_register_write(rt2x00dev, TX_CTX_IDX(queue->qid),
					  entry->entry_idx);
		hrtimer_start(&rt2x00dev->txstatus_timer,
			      TXSTATUS_TIMEOUT, HRTIMER_MODE_REL);
		break;
	case QID_MGMT:
		entry = rt2x00queue_get_entry(queue, Q_INDEX);
		rt2x00mmio_register_write(rt2x00dev, TX_CTX_IDX(5),
					  entry->entry_idx);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(rt2800mmio_kick_queue);

void rt2800mmio_flush_queue(struct data_queue *queue, bool drop)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	bool tx_queue = false;
	unsigned int i;

	switch (queue->qid) {
	case QID_AC_VO:
	case QID_AC_VI:
	case QID_AC_BE:
	case QID_AC_BK:
		tx_queue = true;
		break;
	case QID_RX:
		break;
	default:
		return;
	}

	for (i = 0; i < 5; i++) {
		/*
		 * Check if the driver is already done, otherwise we
		 * have to sleep a little while to give the driver/hw
		 * the oppurtunity to complete interrupt process itself.
		 */
		if (rt2x00queue_empty(queue))
			break;

		/*
		 * For TX queues schedule completion tasklet to catch
		 * tx status timeouts, othewise just wait.
		 */
		if (tx_queue)
			queue_work(rt2x00dev->workqueue, &rt2x00dev->txdone_work);

		/*
		 * Wait for a little while to give the driver
		 * the oppurtunity to recover itself.
		 */
		msleep(50);
	}
}
EXPORT_SYMBOL_GPL(rt2800mmio_flush_queue);

void rt2800mmio_stop_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	u32 reg;

	switch (queue->qid) {
	case QID_RX:
		reg = rt2x00mmio_register_read(rt2x00dev, MAC_SYS_CTRL);
		rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 0);
		rt2x00mmio_register_write(rt2x00dev, MAC_SYS_CTRL, reg);
		break;
	case QID_BEACON:
		reg = rt2x00mmio_register_read(rt2x00dev, BCN_TIME_CFG);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_TICKING, 0);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TBTT_ENABLE, 0);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 0);
		rt2x00mmio_register_write(rt2x00dev, BCN_TIME_CFG, reg);

		reg = rt2x00mmio_register_read(rt2x00dev, INT_TIMER_EN);
		rt2x00_set_field32(&reg, INT_TIMER_EN_PRE_TBTT_TIMER, 0);
		rt2x00mmio_register_write(rt2x00dev, INT_TIMER_EN, reg);

		/*
		 * Wait for current invocation to finish. The tasklet
		 * won't be scheduled anymore afterwards since we disabled
		 * the TBTT and PRE TBTT timer.
		 */
		tasklet_kill(&rt2x00dev->tbtt_tasklet);
		tasklet_kill(&rt2x00dev->pretbtt_tasklet);

		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(rt2800mmio_stop_queue);

void rt2800mmio_queue_init(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	unsigned short txwi_size, rxwi_size;

	rt2800_get_txwi_rxwi_size(rt2x00dev, &txwi_size, &rxwi_size);

	switch (queue->qid) {
	case QID_RX:
		queue->limit = 128;
		queue->data_size = AGGREGATION_SIZE;
		queue->desc_size = RXD_DESC_SIZE;
		queue->winfo_size = rxwi_size;
		queue->priv_size = sizeof(struct queue_entry_priv_mmio);
		break;

	case QID_AC_VO:
	case QID_AC_VI:
	case QID_AC_BE:
	case QID_AC_BK:
		queue->limit = 64;
		queue->data_size = AGGREGATION_SIZE;
		queue->desc_size = TXD_DESC_SIZE;
		queue->winfo_size = txwi_size;
		queue->priv_size = sizeof(struct queue_entry_priv_mmio);
		break;

	case QID_BEACON:
		queue->limit = 8;
		queue->data_size = 0; /* No DMA required for beacons */
		queue->desc_size = TXD_DESC_SIZE;
		queue->winfo_size = txwi_size;
		queue->priv_size = sizeof(struct queue_entry_priv_mmio);
		break;

	case QID_ATIM:
		/* fallthrough */
	default:
		BUG();
		break;
	}
}
EXPORT_SYMBOL_GPL(rt2800mmio_queue_init);

/*
 * Initialization functions.
 */
bool rt2800mmio_get_entry_state(struct queue_entry *entry)
{
	struct queue_entry_priv_mmio *entry_priv = entry->priv_data;
	u32 word;

	if (entry->queue->qid == QID_RX) {
		word = rt2x00_desc_read(entry_priv->desc, 1);

		return (!rt2x00_get_field32(word, RXD_W1_DMA_DONE));
	} else {
		word = rt2x00_desc_read(entry_priv->desc, 1);

		return (!rt2x00_get_field32(word, TXD_W1_DMA_DONE));
	}
}
EXPORT_SYMBOL_GPL(rt2800mmio_get_entry_state);

void rt2800mmio_clear_entry(struct queue_entry *entry)
{
	struct queue_entry_priv_mmio *entry_priv = entry->priv_data;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	u32 word;

	if (entry->queue->qid == QID_RX) {
		word = rt2x00_desc_read(entry_priv->desc, 0);
		rt2x00_set_field32(&word, RXD_W0_SDP0, skbdesc->skb_dma);
		rt2x00_desc_write(entry_priv->desc, 0, word);

		word = rt2x00_desc_read(entry_priv->desc, 1);
		rt2x00_set_field32(&word, RXD_W1_DMA_DONE, 0);
		rt2x00_desc_write(entry_priv->desc, 1, word);

		/*
		 * Set RX IDX in register to inform hardware that we have
		 * handled this entry and it is available for reuse again.
		 */
		rt2x00mmio_register_write(rt2x00dev, RX_CRX_IDX,
					  entry->entry_idx);
	} else {
		word = rt2x00_desc_read(entry_priv->desc, 1);
		rt2x00_set_field32(&word, TXD_W1_DMA_DONE, 1);
		rt2x00_desc_write(entry_priv->desc, 1, word);

		/* If last entry stop txstatus timer */
		if (entry->queue->length == 1)
			hrtimer_cancel(&rt2x00dev->txstatus_timer);
	}
}
EXPORT_SYMBOL_GPL(rt2800mmio_clear_entry);

int rt2800mmio_init_queues(struct rt2x00_dev *rt2x00dev)
{
	struct queue_entry_priv_mmio *entry_priv;

	/*
	 * Initialize registers.
	 */
	entry_priv = rt2x00dev->tx[0].entries[0].priv_data;
	rt2x00mmio_register_write(rt2x00dev, TX_BASE_PTR0,
				  entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, TX_MAX_CNT0,
				  rt2x00dev->tx[0].limit);
	rt2x00mmio_register_write(rt2x00dev, TX_CTX_IDX0, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_DTX_IDX0, 0);

	entry_priv = rt2x00dev->tx[1].entries[0].priv_data;
	rt2x00mmio_register_write(rt2x00dev, TX_BASE_PTR1,
				  entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, TX_MAX_CNT1,
				  rt2x00dev->tx[1].limit);
	rt2x00mmio_register_write(rt2x00dev, TX_CTX_IDX1, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_DTX_IDX1, 0);

	entry_priv = rt2x00dev->tx[2].entries[0].priv_data;
	rt2x00mmio_register_write(rt2x00dev, TX_BASE_PTR2,
				  entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, TX_MAX_CNT2,
				  rt2x00dev->tx[2].limit);
	rt2x00mmio_register_write(rt2x00dev, TX_CTX_IDX2, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_DTX_IDX2, 0);

	entry_priv = rt2x00dev->tx[3].entries[0].priv_data;
	rt2x00mmio_register_write(rt2x00dev, TX_BASE_PTR3,
				  entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, TX_MAX_CNT3,
				  rt2x00dev->tx[3].limit);
	rt2x00mmio_register_write(rt2x00dev, TX_CTX_IDX3, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_DTX_IDX3, 0);

	rt2x00mmio_register_write(rt2x00dev, TX_BASE_PTR4, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_MAX_CNT4, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_CTX_IDX4, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_DTX_IDX4, 0);

	rt2x00mmio_register_write(rt2x00dev, TX_BASE_PTR5, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_MAX_CNT5, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_CTX_IDX5, 0);
	rt2x00mmio_register_write(rt2x00dev, TX_DTX_IDX5, 0);

	entry_priv = rt2x00dev->rx->entries[0].priv_data;
	rt2x00mmio_register_write(rt2x00dev, RX_BASE_PTR,
				  entry_priv->desc_dma);
	rt2x00mmio_register_write(rt2x00dev, RX_MAX_CNT,
				  rt2x00dev->rx[0].limit);
	rt2x00mmio_register_write(rt2x00dev, RX_CRX_IDX,
				  rt2x00dev->rx[0].limit - 1);
	rt2x00mmio_register_write(rt2x00dev, RX_DRX_IDX, 0);

	rt2800_disable_wpdma(rt2x00dev);

	rt2x00mmio_register_write(rt2x00dev, DELAY_INT_CFG, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800mmio_init_queues);

int rt2800mmio_init_registers(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	/*
	 * Reset DMA indexes
	 */
	reg = rt2x00mmio_register_read(rt2x00dev, WPDMA_RST_IDX);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX0, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX1, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX2, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX3, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX4, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX5, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DRX_IDX0, 1);
	rt2x00mmio_register_write(rt2x00dev, WPDMA_RST_IDX, reg);

	rt2x00mmio_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00000e1f);
	rt2x00mmio_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00000e00);

	if (rt2x00_is_pcie(rt2x00dev) &&
	    (rt2x00_rt(rt2x00dev, RT3090) ||
	     rt2x00_rt(rt2x00dev, RT3390) ||
	     rt2x00_rt(rt2x00dev, RT3572) ||
	     rt2x00_rt(rt2x00dev, RT3593) ||
	     rt2x00_rt(rt2x00dev, RT5390) ||
	     rt2x00_rt(rt2x00dev, RT5392) ||
	     rt2x00_rt(rt2x00dev, RT5592))) {
		reg = rt2x00mmio_register_read(rt2x00dev, AUX_CTRL);
		rt2x00_set_field32(&reg, AUX_CTRL_FORCE_PCIE_CLK, 1);
		rt2x00_set_field32(&reg, AUX_CTRL_WAKE_PCIE_EN, 1);
		rt2x00mmio_register_write(rt2x00dev, AUX_CTRL, reg);
	}

	rt2x00mmio_register_write(rt2x00dev, PWR_PIN_CFG, 0x00000003);

	reg = 0;
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_RESET_CSR, 1);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_RESET_BBP, 1);
	rt2x00mmio_register_write(rt2x00dev, MAC_SYS_CTRL, reg);

	rt2x00mmio_register_write(rt2x00dev, MAC_SYS_CTRL, 0x00000000);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800mmio_init_registers);

/*
 * Device state switch handlers.
 */
int rt2800mmio_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	/* Wait for DMA, ignore error until we initialize queues. */
	rt2800_wait_wpdma_ready(rt2x00dev);

	if (unlikely(rt2800mmio_init_queues(rt2x00dev)))
		return -EIO;

	return rt2800_enable_radio(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2800mmio_enable_radio);

static void rt2800mmio_work_txdone(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, txdone_work);

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	while (!kfifo_is_empty(&rt2x00dev->txstatus_fifo) ||
	       rt2800_txstatus_timeout(rt2x00dev)) {

		tasklet_disable(&rt2x00dev->txstatus_tasklet);
		rt2800_txdone(rt2x00dev, UINT_MAX);
		rt2800_txdone_nostatus(rt2x00dev);
		tasklet_enable(&rt2x00dev->txstatus_tasklet);
	}

	if (rt2800_txstatus_pending(rt2x00dev))
		hrtimer_start(&rt2x00dev->txstatus_timer,
			      TXSTATUS_TIMEOUT, HRTIMER_MODE_REL);
}

static enum hrtimer_restart rt2800mmio_tx_sta_fifo_timeout(struct hrtimer *timer)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(timer, struct rt2x00_dev, txstatus_timer);

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		goto out;

	if (!rt2800_txstatus_pending(rt2x00dev))
		goto out;

	rt2800mmio_fetch_txstatus(rt2x00dev);
	if (!kfifo_is_empty(&rt2x00dev->txstatus_fifo))
		tasklet_schedule(&rt2x00dev->txstatus_tasklet);
	else
		queue_work(rt2x00dev->workqueue, &rt2x00dev->txdone_work);
out:
	return HRTIMER_NORESTART;
}

int rt2800mmio_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	retval = rt2800_probe_hw(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * Set txstatus timer function.
	 */
	rt2x00dev->txstatus_timer.function = rt2800mmio_tx_sta_fifo_timeout;

	/*
	 * Overwrite TX done handler
	 */
	INIT_WORK(&rt2x00dev->txdone_work, rt2800mmio_work_txdone);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2800mmio_probe_hw);

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2800 MMIO library");
MODULE_LICENSE("GPL");
