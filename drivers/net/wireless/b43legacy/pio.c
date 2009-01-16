/*

  Broadcom B43legacy wireless driver

  PIO Transmission

  Copyright (c) 2005 Michael Buesch <mb@bu3sch.de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include "b43legacy.h"
#include "pio.h"
#include "main.h"
#include "xmit.h"

#include <linux/delay.h>


static void tx_start(struct b43legacy_pioqueue *queue)
{
	b43legacy_pio_write(queue, B43legacy_PIO_TXCTL,
			    B43legacy_PIO_TXCTL_INIT);
}

static void tx_octet(struct b43legacy_pioqueue *queue,
		     u8 octet)
{
	if (queue->need_workarounds) {
		b43legacy_pio_write(queue, B43legacy_PIO_TXDATA, octet);
		b43legacy_pio_write(queue, B43legacy_PIO_TXCTL,
				    B43legacy_PIO_TXCTL_WRITELO);
	} else {
		b43legacy_pio_write(queue, B43legacy_PIO_TXCTL,
				    B43legacy_PIO_TXCTL_WRITELO);
		b43legacy_pio_write(queue, B43legacy_PIO_TXDATA, octet);
	}
}

static u16 tx_get_next_word(const u8 *txhdr,
			    const u8 *packet,
			    size_t txhdr_size,
			    unsigned int *pos)
{
	const u8 *source;
	unsigned int i = *pos;
	u16 ret;

	if (i < txhdr_size)
		source = txhdr;
	else {
		source = packet;
		i -= txhdr_size;
	}
	ret = le16_to_cpu(*((__le16 *)(source + i)));
	*pos += 2;

	return ret;
}

static void tx_data(struct b43legacy_pioqueue *queue,
		    u8 *txhdr,
		    const u8 *packet,
		    unsigned int octets)
{
	u16 data;
	unsigned int i = 0;

	if (queue->need_workarounds) {
		data = tx_get_next_word(txhdr, packet,
					sizeof(struct b43legacy_txhdr_fw3), &i);
		b43legacy_pio_write(queue, B43legacy_PIO_TXDATA, data);
	}
	b43legacy_pio_write(queue, B43legacy_PIO_TXCTL,
			    B43legacy_PIO_TXCTL_WRITELO |
			    B43legacy_PIO_TXCTL_WRITEHI);
	while (i < octets - 1) {
		data = tx_get_next_word(txhdr, packet,
					sizeof(struct b43legacy_txhdr_fw3), &i);
		b43legacy_pio_write(queue, B43legacy_PIO_TXDATA, data);
	}
	if (octets % 2)
		tx_octet(queue, packet[octets -
			 sizeof(struct b43legacy_txhdr_fw3) - 1]);
}

static void tx_complete(struct b43legacy_pioqueue *queue,
			struct sk_buff *skb)
{
	if (queue->need_workarounds) {
		b43legacy_pio_write(queue, B43legacy_PIO_TXDATA,
				    skb->data[skb->len - 1]);
		b43legacy_pio_write(queue, B43legacy_PIO_TXCTL,
				    B43legacy_PIO_TXCTL_WRITELO |
				    B43legacy_PIO_TXCTL_COMPLETE);
	} else
		b43legacy_pio_write(queue, B43legacy_PIO_TXCTL,
				    B43legacy_PIO_TXCTL_COMPLETE);
}

static u16 generate_cookie(struct b43legacy_pioqueue *queue,
			   struct b43legacy_pio_txpacket *packet)
{
	u16 cookie = 0x0000;
	int packetindex;

	/* We use the upper 4 bits for the PIO
	 * controller ID and the lower 12 bits
	 * for the packet index (in the cache).
	 */
	switch (queue->mmio_base) {
	case B43legacy_MMIO_PIO1_BASE:
		break;
	case B43legacy_MMIO_PIO2_BASE:
		cookie = 0x1000;
		break;
	case B43legacy_MMIO_PIO3_BASE:
		cookie = 0x2000;
		break;
	case B43legacy_MMIO_PIO4_BASE:
		cookie = 0x3000;
		break;
	default:
		B43legacy_WARN_ON(1);
	}
	packetindex = pio_txpacket_getindex(packet);
	B43legacy_WARN_ON(!(((u16)packetindex & 0xF000) == 0x0000));
	cookie |= (u16)packetindex;

	return cookie;
}

static
struct b43legacy_pioqueue *parse_cookie(struct b43legacy_wldev *dev,
					u16 cookie,
					struct b43legacy_pio_txpacket **packet)
{
	struct b43legacy_pio *pio = &dev->pio;
	struct b43legacy_pioqueue *queue = NULL;
	int packetindex;

	switch (cookie & 0xF000) {
	case 0x0000:
		queue = pio->queue0;
		break;
	case 0x1000:
		queue = pio->queue1;
		break;
	case 0x2000:
		queue = pio->queue2;
		break;
	case 0x3000:
		queue = pio->queue3;
		break;
	default:
		B43legacy_WARN_ON(1);
	}
	packetindex = (cookie & 0x0FFF);
	B43legacy_WARN_ON(!(packetindex >= 0 && packetindex
			  < B43legacy_PIO_MAXTXPACKETS));
	*packet = &(queue->tx_packets_cache[packetindex]);

	return queue;
}

union txhdr_union {
	struct b43legacy_txhdr_fw3 txhdr_fw3;
};

static int pio_tx_write_fragment(struct b43legacy_pioqueue *queue,
				  struct sk_buff *skb,
				  struct b43legacy_pio_txpacket *packet,
				  size_t txhdr_size)
{
	union txhdr_union txhdr_data;
	u8 *txhdr = NULL;
	unsigned int octets;
	int err;

	txhdr = (u8 *)(&txhdr_data.txhdr_fw3);

	B43legacy_WARN_ON(skb_shinfo(skb)->nr_frags != 0);
	err = b43legacy_generate_txhdr(queue->dev,
				 txhdr, skb->data, skb->len,
				 IEEE80211_SKB_CB(skb),
				 generate_cookie(queue, packet));
	if (err)
		return err;

	tx_start(queue);
	octets = skb->len + txhdr_size;
	if (queue->need_workarounds)
		octets--;
	tx_data(queue, txhdr, (u8 *)skb->data, octets);
	tx_complete(queue, skb);

	return 0;
}

static void free_txpacket(struct b43legacy_pio_txpacket *packet,
			  int irq_context)
{
	struct b43legacy_pioqueue *queue = packet->queue;

	if (packet->skb) {
		if (irq_context)
			dev_kfree_skb_irq(packet->skb);
		else
			dev_kfree_skb(packet->skb);
	}
	list_move(&packet->list, &queue->txfree);
	queue->nr_txfree++;
}

static int pio_tx_packet(struct b43legacy_pio_txpacket *packet)
{
	struct b43legacy_pioqueue *queue = packet->queue;
	struct sk_buff *skb = packet->skb;
	u16 octets;
	int err;

	octets = (u16)skb->len + sizeof(struct b43legacy_txhdr_fw3);
	if (queue->tx_devq_size < octets) {
		b43legacywarn(queue->dev->wl, "PIO queue too small. "
			"Dropping packet.\n");
		/* Drop it silently (return success) */
		free_txpacket(packet, 1);
		return 0;
	}
	B43legacy_WARN_ON(queue->tx_devq_packets >
			  B43legacy_PIO_MAXTXDEVQPACKETS);
	B43legacy_WARN_ON(queue->tx_devq_used > queue->tx_devq_size);
	/* Check if there is sufficient free space on the device
	 * TX queue. If not, return and let the TX tasklet
	 * retry later.
	 */
	if (queue->tx_devq_packets == B43legacy_PIO_MAXTXDEVQPACKETS)
		return -EBUSY;
	if (queue->tx_devq_used + octets > queue->tx_devq_size)
		return -EBUSY;
	/* Now poke the device. */
	err = pio_tx_write_fragment(queue, skb, packet,
			      sizeof(struct b43legacy_txhdr_fw3));
	if (unlikely(err == -ENOKEY)) {
		/* Drop this packet, as we don't have the encryption key
		 * anymore and must not transmit it unencrypted. */
		free_txpacket(packet, 1);
		return 0;
	}

	/* Account for the packet size.
	 * (We must not overflow the device TX queue)
	 */
	queue->tx_devq_packets++;
	queue->tx_devq_used += octets;

	/* Transmission started, everything ok, move the
	 * packet to the txrunning list.
	 */
	list_move_tail(&packet->list, &queue->txrunning);

	return 0;
}

static void tx_tasklet(unsigned long d)
{
	struct b43legacy_pioqueue *queue = (struct b43legacy_pioqueue *)d;
	struct b43legacy_wldev *dev = queue->dev;
	unsigned long flags;
	struct b43legacy_pio_txpacket *packet, *tmp_packet;
	int err;
	u16 txctl;

	spin_lock_irqsave(&dev->wl->irq_lock, flags);
	if (queue->tx_frozen)
		goto out_unlock;
	txctl = b43legacy_pio_read(queue, B43legacy_PIO_TXCTL);
	if (txctl & B43legacy_PIO_TXCTL_SUSPEND)
		goto out_unlock;

	list_for_each_entry_safe(packet, tmp_packet, &queue->txqueue, list) {
		/* Try to transmit the packet. This can fail, if
		 * the device queue is full. In case of failure, the
		 * packet is left in the txqueue.
		 * If transmission succeed, the packet is moved to txrunning.
		 * If it is impossible to transmit the packet, it
		 * is dropped.
		 */
		err = pio_tx_packet(packet);
		if (err)
			break;
	}
out_unlock:
	spin_unlock_irqrestore(&dev->wl->irq_lock, flags);
}

static void setup_txqueues(struct b43legacy_pioqueue *queue)
{
	struct b43legacy_pio_txpacket *packet;
	int i;

	queue->nr_txfree = B43legacy_PIO_MAXTXPACKETS;
	for (i = 0; i < B43legacy_PIO_MAXTXPACKETS; i++) {
		packet = &(queue->tx_packets_cache[i]);

		packet->queue = queue;
		INIT_LIST_HEAD(&packet->list);

		list_add(&packet->list, &queue->txfree);
	}
}

static
struct b43legacy_pioqueue *b43legacy_setup_pioqueue(struct b43legacy_wldev *dev,
						    u16 pio_mmio_base)
{
	struct b43legacy_pioqueue *queue;
	u32 value;
	u16 qsize;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		goto out;

	queue->dev = dev;
	queue->mmio_base = pio_mmio_base;
	queue->need_workarounds = (dev->dev->id.revision < 3);

	INIT_LIST_HEAD(&queue->txfree);
	INIT_LIST_HEAD(&queue->txqueue);
	INIT_LIST_HEAD(&queue->txrunning);
	tasklet_init(&queue->txtask, tx_tasklet,
		     (unsigned long)queue);

	value = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	value &= ~B43legacy_MACCTL_BE;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, value);

	qsize = b43legacy_read16(dev, queue->mmio_base
				 + B43legacy_PIO_TXQBUFSIZE);
	if (qsize == 0) {
		b43legacyerr(dev->wl, "This card does not support PIO "
		       "operation mode. Please use DMA mode "
		       "(module parameter pio=0).\n");
		goto err_freequeue;
	}
	if (qsize <= B43legacy_PIO_TXQADJUST) {
		b43legacyerr(dev->wl, "PIO tx device-queue too small (%u)\n",
		       qsize);
		goto err_freequeue;
	}
	qsize -= B43legacy_PIO_TXQADJUST;
	queue->tx_devq_size = qsize;

	setup_txqueues(queue);

out:
	return queue;

err_freequeue:
	kfree(queue);
	queue = NULL;
	goto out;
}

static void cancel_transfers(struct b43legacy_pioqueue *queue)
{
	struct b43legacy_pio_txpacket *packet, *tmp_packet;

	tasklet_disable(&queue->txtask);

	list_for_each_entry_safe(packet, tmp_packet, &queue->txrunning, list)
		free_txpacket(packet, 0);
	list_for_each_entry_safe(packet, tmp_packet, &queue->txqueue, list)
		free_txpacket(packet, 0);
}

static void b43legacy_destroy_pioqueue(struct b43legacy_pioqueue *queue)
{
	if (!queue)
		return;

	cancel_transfers(queue);
	kfree(queue);
}

void b43legacy_pio_free(struct b43legacy_wldev *dev)
{
	struct b43legacy_pio *pio;

	if (!b43legacy_using_pio(dev))
		return;
	pio = &dev->pio;

	b43legacy_destroy_pioqueue(pio->queue3);
	pio->queue3 = NULL;
	b43legacy_destroy_pioqueue(pio->queue2);
	pio->queue2 = NULL;
	b43legacy_destroy_pioqueue(pio->queue1);
	pio->queue1 = NULL;
	b43legacy_destroy_pioqueue(pio->queue0);
	pio->queue0 = NULL;
}

int b43legacy_pio_init(struct b43legacy_wldev *dev)
{
	struct b43legacy_pio *pio = &dev->pio;
	struct b43legacy_pioqueue *queue;
	int err = -ENOMEM;

	queue = b43legacy_setup_pioqueue(dev, B43legacy_MMIO_PIO1_BASE);
	if (!queue)
		goto out;
	pio->queue0 = queue;

	queue = b43legacy_setup_pioqueue(dev, B43legacy_MMIO_PIO2_BASE);
	if (!queue)
		goto err_destroy0;
	pio->queue1 = queue;

	queue = b43legacy_setup_pioqueue(dev, B43legacy_MMIO_PIO3_BASE);
	if (!queue)
		goto err_destroy1;
	pio->queue2 = queue;

	queue = b43legacy_setup_pioqueue(dev, B43legacy_MMIO_PIO4_BASE);
	if (!queue)
		goto err_destroy2;
	pio->queue3 = queue;

	if (dev->dev->id.revision < 3)
		dev->irq_savedstate |= B43legacy_IRQ_PIO_WORKAROUND;

	b43legacydbg(dev->wl, "PIO initialized\n");
	err = 0;
out:
	return err;

err_destroy2:
	b43legacy_destroy_pioqueue(pio->queue2);
	pio->queue2 = NULL;
err_destroy1:
	b43legacy_destroy_pioqueue(pio->queue1);
	pio->queue1 = NULL;
err_destroy0:
	b43legacy_destroy_pioqueue(pio->queue0);
	pio->queue0 = NULL;
	goto out;
}

int b43legacy_pio_tx(struct b43legacy_wldev *dev,
		     struct sk_buff *skb)
{
	struct b43legacy_pioqueue *queue = dev->pio.queue1;
	struct b43legacy_pio_txpacket *packet;

	B43legacy_WARN_ON(queue->tx_suspended);
	B43legacy_WARN_ON(list_empty(&queue->txfree));

	packet = list_entry(queue->txfree.next, struct b43legacy_pio_txpacket,
			    list);
	packet->skb = skb;

	list_move_tail(&packet->list, &queue->txqueue);
	queue->nr_txfree--;
	queue->nr_tx_packets++;
	B43legacy_WARN_ON(queue->nr_txfree >= B43legacy_PIO_MAXTXPACKETS);

	tasklet_schedule(&queue->txtask);

	return 0;
}

void b43legacy_pio_handle_txstatus(struct b43legacy_wldev *dev,
				   const struct b43legacy_txstatus *status)
{
	struct b43legacy_pioqueue *queue;
	struct b43legacy_pio_txpacket *packet;
	struct ieee80211_tx_info *info;
	int retry_limit;

	queue = parse_cookie(dev, status->cookie, &packet);
	B43legacy_WARN_ON(!queue);

	if (!packet->skb)
		return;

	queue->tx_devq_packets--;
	queue->tx_devq_used -= (packet->skb->len +
				sizeof(struct b43legacy_txhdr_fw3));

	info = IEEE80211_SKB_CB(packet->skb);

	/* preserve the confiured retry limit before clearing the status
	 * The xmit function has overwritten the rc's value with the actual
	 * retry limit done by the hardware */
	retry_limit = info->status.rates[0].count;
	ieee80211_tx_info_clear_status(info);

	if (status->acked)
		info->flags |= IEEE80211_TX_STAT_ACK;

	if (status->rts_count > dev->wl->hw->conf.short_frame_max_tx_count) {
		/*
		 * If the short retries (RTS, not data frame) have exceeded
		 * the limit, the hw will not have tried the selected rate,
		 * but will have used the fallback rate instead.
		 * Don't let the rate control count attempts for the selected
		 * rate in this case, otherwise the statistics will be off.
		 */
		info->status.rates[0].count = 0;
		info->status.rates[1].count = status->frame_count;
	} else {
		if (status->frame_count > retry_limit) {
			info->status.rates[0].count = retry_limit;
			info->status.rates[1].count = status->frame_count -
					retry_limit;

		} else {
			info->status.rates[0].count = status->frame_count;
			info->status.rates[1].idx = -1;
		}
	}
	ieee80211_tx_status_irqsafe(dev->wl->hw, packet->skb);
	packet->skb = NULL;

	free_txpacket(packet, 1);
	/* If there are packets on the txqueue, poke the tasklet
	 * to transmit them.
	 */
	if (!list_empty(&queue->txqueue))
		tasklet_schedule(&queue->txtask);
}

void b43legacy_pio_get_tx_stats(struct b43legacy_wldev *dev,
				struct ieee80211_tx_queue_stats *stats)
{
	struct b43legacy_pio *pio = &dev->pio;
	struct b43legacy_pioqueue *queue;

	queue = pio->queue1;
	stats[0].len = B43legacy_PIO_MAXTXPACKETS - queue->nr_txfree;
	stats[0].limit = B43legacy_PIO_MAXTXPACKETS;
	stats[0].count = queue->nr_tx_packets;
}

static void pio_rx_error(struct b43legacy_pioqueue *queue,
			 int clear_buffers,
			 const char *error)
{
	int i;

	b43legacyerr(queue->dev->wl, "PIO RX error: %s\n", error);
	b43legacy_pio_write(queue, B43legacy_PIO_RXCTL,
			    B43legacy_PIO_RXCTL_READY);
	if (clear_buffers) {
		B43legacy_WARN_ON(queue->mmio_base != B43legacy_MMIO_PIO1_BASE);
		for (i = 0; i < 15; i++) {
			/* Dummy read. */
			b43legacy_pio_read(queue, B43legacy_PIO_RXDATA);
		}
	}
}

void b43legacy_pio_rx(struct b43legacy_pioqueue *queue)
{
	__le16 preamble[21] = { 0 };
	struct b43legacy_rxhdr_fw3 *rxhdr;
	u16 tmp;
	u16 len;
	u16 macstat;
	int i;
	int preamble_readwords;
	struct sk_buff *skb;

	tmp = b43legacy_pio_read(queue, B43legacy_PIO_RXCTL);
	if (!(tmp & B43legacy_PIO_RXCTL_DATAAVAILABLE))
		return;
	b43legacy_pio_write(queue, B43legacy_PIO_RXCTL,
			    B43legacy_PIO_RXCTL_DATAAVAILABLE);

	for (i = 0; i < 10; i++) {
		tmp = b43legacy_pio_read(queue, B43legacy_PIO_RXCTL);
		if (tmp & B43legacy_PIO_RXCTL_READY)
			goto data_ready;
		udelay(10);
	}
	b43legacydbg(queue->dev->wl, "PIO RX timed out\n");
	return;
data_ready:

	len = b43legacy_pio_read(queue, B43legacy_PIO_RXDATA);
	if (unlikely(len > 0x700)) {
		pio_rx_error(queue, 0, "len > 0x700");
		return;
	}
	if (unlikely(len == 0 && queue->mmio_base !=
		     B43legacy_MMIO_PIO4_BASE)) {
		pio_rx_error(queue, 0, "len == 0");
		return;
	}
	preamble[0] = cpu_to_le16(len);
	if (queue->mmio_base == B43legacy_MMIO_PIO4_BASE)
		preamble_readwords = 14 / sizeof(u16);
	else
		preamble_readwords = 18 / sizeof(u16);
	for (i = 0; i < preamble_readwords; i++) {
		tmp = b43legacy_pio_read(queue, B43legacy_PIO_RXDATA);
		preamble[i + 1] = cpu_to_le16(tmp);
	}
	rxhdr = (struct b43legacy_rxhdr_fw3 *)preamble;
	macstat = le16_to_cpu(rxhdr->mac_status);
	if (macstat & B43legacy_RX_MAC_FCSERR) {
		pio_rx_error(queue,
			     (queue->mmio_base == B43legacy_MMIO_PIO1_BASE),
			     "Frame FCS error");
		return;
	}
	if (queue->mmio_base == B43legacy_MMIO_PIO4_BASE) {
		/* We received an xmit status. */
		struct b43legacy_hwtxstatus *hw;

		hw = (struct b43legacy_hwtxstatus *)(preamble + 1);
		b43legacy_handle_hwtxstatus(queue->dev, hw);

		return;
	}

	skb = dev_alloc_skb(len);
	if (unlikely(!skb)) {
		pio_rx_error(queue, 1, "OOM");
		return;
	}
	skb_put(skb, len);
	for (i = 0; i < len - 1; i += 2) {
		tmp = b43legacy_pio_read(queue, B43legacy_PIO_RXDATA);
		*((__le16 *)(skb->data + i)) = cpu_to_le16(tmp);
	}
	if (len % 2) {
		tmp = b43legacy_pio_read(queue, B43legacy_PIO_RXDATA);
		skb->data[len - 1] = (tmp & 0x00FF);
	}
	b43legacy_rx(queue->dev, skb, rxhdr);
}

void b43legacy_pio_tx_suspend(struct b43legacy_pioqueue *queue)
{
	b43legacy_power_saving_ctl_bits(queue->dev, -1, 1);
	b43legacy_pio_write(queue, B43legacy_PIO_TXCTL,
			    b43legacy_pio_read(queue, B43legacy_PIO_TXCTL)
			    | B43legacy_PIO_TXCTL_SUSPEND);
}

void b43legacy_pio_tx_resume(struct b43legacy_pioqueue *queue)
{
	b43legacy_pio_write(queue, B43legacy_PIO_TXCTL,
			    b43legacy_pio_read(queue, B43legacy_PIO_TXCTL)
			    & ~B43legacy_PIO_TXCTL_SUSPEND);
	b43legacy_power_saving_ctl_bits(queue->dev, -1, -1);
	tasklet_schedule(&queue->txtask);
}

void b43legacy_pio_freeze_txqueues(struct b43legacy_wldev *dev)
{
	struct b43legacy_pio *pio;

	B43legacy_WARN_ON(!b43legacy_using_pio(dev));
	pio = &dev->pio;
	pio->queue0->tx_frozen = 1;
	pio->queue1->tx_frozen = 1;
	pio->queue2->tx_frozen = 1;
	pio->queue3->tx_frozen = 1;
}

void b43legacy_pio_thaw_txqueues(struct b43legacy_wldev *dev)
{
	struct b43legacy_pio *pio;

	B43legacy_WARN_ON(!b43legacy_using_pio(dev));
	pio = &dev->pio;
	pio->queue0->tx_frozen = 0;
	pio->queue1->tx_frozen = 0;
	pio->queue2->tx_frozen = 0;
	pio->queue3->tx_frozen = 0;
	if (!list_empty(&pio->queue0->txqueue))
		tasklet_schedule(&pio->queue0->txtask);
	if (!list_empty(&pio->queue1->txqueue))
		tasklet_schedule(&pio->queue1->txtask);
	if (!list_empty(&pio->queue2->txqueue))
		tasklet_schedule(&pio->queue2->txtask);
	if (!list_empty(&pio->queue3->txqueue))
		tasklet_schedule(&pio->queue3->txtask);
}
