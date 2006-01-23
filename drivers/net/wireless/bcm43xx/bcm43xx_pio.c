/*

  Broadcom BCM43xx wireless driver

  PIO Transmission

  Copyright (c) 2005 Michael Buesch <mbuesch@freenet.de>

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

#include "bcm43xx.h"
#include "bcm43xx_pio.h"
#include "bcm43xx_main.h"

#include <linux/delay.h>


static inline
u16 bcm43xx_pio_read(struct bcm43xx_pioqueue *queue,
		     u16 offset)
{
	return bcm43xx_read16(queue->bcm, queue->mmio_base + offset);
}

static inline
void bcm43xx_pio_write(struct bcm43xx_pioqueue *queue,
		       u16 offset, u16 value)
{
	bcm43xx_write16(queue->bcm, queue->mmio_base + offset, value);
}

static inline
void tx_start(struct bcm43xx_pioqueue *queue)
{
	bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL, BCM43xx_PIO_TXCTL_INIT);
}

static inline
void tx_octet(struct bcm43xx_pioqueue *queue,
	      u8 octet)
{
	if (queue->bcm->current_core->rev < 3) {
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA, octet);
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL, BCM43xx_PIO_TXCTL_WRITEHI);
	} else {
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL, BCM43xx_PIO_TXCTL_WRITEHI);
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA, octet);
	}
}

static inline
void tx_data(struct bcm43xx_pioqueue *queue,
	     u8 *packet,
	     unsigned int octets)
{
	u16 data;
	unsigned int i = 0;

	if (queue->bcm->current_core->rev < 3) {
		data = be16_to_cpu( *((u16 *)packet) );
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA, data);
		i += 2;
	}
	bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
			  BCM43xx_PIO_TXCTL_WRITELO | BCM43xx_PIO_TXCTL_WRITEHI);
	for ( ; i < octets - 1; i += 2) {
		data = be16_to_cpu( *((u16 *)(packet + i)) );
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA, data);
	}
	if (octets % 2)
		tx_octet(queue, packet[octets - 1]);
}

static inline
void tx_complete(struct bcm43xx_pioqueue *queue,
		 struct sk_buff *skb)
{
	u16 data;

	if (queue->bcm->current_core->rev < 3) {
		data = be16_to_cpu( *((u16 *)(skb->data + skb->len - 2)) );
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA, data);
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
				  BCM43xx_PIO_TXCTL_WRITEHI | BCM43xx_PIO_TXCTL_COMPLETE);
	} else {
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL, BCM43xx_PIO_TXCTL_COMPLETE);
	}
}

static inline
u16 generate_cookie(struct bcm43xx_pioqueue *queue,
		    int packetindex)
{
	u16 cookie = 0x0000;

	/* We use the upper 4 bits for the PIO
	 * controller ID and the lower 12 bits
	 * for the packet index (in the cache).
	 */
	switch (queue->mmio_base) {
	default:
		assert(0);
	case BCM43xx_MMIO_PIO1_BASE:
		break;
	case BCM43xx_MMIO_PIO2_BASE:
		cookie = 0x1000;
		break;
	case BCM43xx_MMIO_PIO3_BASE:
		cookie = 0x2000;
		break;
	case BCM43xx_MMIO_PIO4_BASE:
		cookie = 0x3000;
		break;
	}
	assert(((u16)packetindex & 0xF000) == 0x0000);
	cookie |= (u16)packetindex;

	return cookie;
}

static inline
struct bcm43xx_pioqueue * parse_cookie(struct bcm43xx_private *bcm,
				       u16 cookie,
				       struct bcm43xx_pio_txpacket **packet)
{
	struct bcm43xx_pioqueue *queue = NULL;
	int packetindex;

	switch (cookie & 0xF000) {
	case 0x0000:
		queue = bcm->current_core->pio->queue0;
		break;
	case 0x1000:
		queue = bcm->current_core->pio->queue1;
		break;
	case 0x2000:
		queue = bcm->current_core->pio->queue2;
		break;
	case 0x3000:
		queue = bcm->current_core->pio->queue3;
		break;
	default:
		assert(0);
	}

	packetindex = (cookie & 0x0FFF);
	assert(packetindex >= 0 && packetindex < BCM43xx_PIO_MAXTXPACKETS);
	*packet = queue->__tx_packets_cache + packetindex;

	return queue;
}

static inline
void pio_tx_write_fragment(struct bcm43xx_pioqueue *queue,
			   struct sk_buff *skb,
			   struct bcm43xx_pio_txpacket *packet)
{
	unsigned int octets;

	assert(skb_shinfo(skb)->nr_frags == 0);
	assert(skb_headroom(skb) >= sizeof(struct bcm43xx_txhdr));

	__skb_push(skb, sizeof(struct bcm43xx_txhdr));
	bcm43xx_generate_txhdr(queue->bcm,
			       (struct bcm43xx_txhdr *)skb->data,
			       skb->data + sizeof(struct bcm43xx_txhdr),
			       skb->len - sizeof(struct bcm43xx_txhdr),
			       (packet->xmitted_frags == 0),
			       generate_cookie(queue, pio_txpacket_getindex(packet)));

	tx_start(queue);
	octets = skb->len;
	if (queue->bcm->current_core->rev < 3) //FIXME: && this is the last packet in the queue.
		octets -= 2;
	tx_data(queue, (u8 *)skb->data, octets);
	tx_complete(queue, skb);
}

static inline
int pio_tx_packet(struct bcm43xx_pio_txpacket *packet)
{
	struct bcm43xx_pioqueue *queue = packet->queue;
	struct ieee80211_txb *txb = packet->txb;
	struct sk_buff *skb;
	u16 octets;
	int i;

	for (i = packet->xmitted_frags; i < txb->nr_frags; i++) {
		skb = txb->fragments[i];

		octets = (u16)skb->len + sizeof(struct bcm43xx_txhdr);

		assert(queue->tx_devq_size >= octets);
		assert(queue->tx_devq_packets <= BCM43xx_PIO_MAXTXDEVQPACKETS);
		assert(queue->tx_devq_used <= queue->tx_devq_size);
		/* Check if there is sufficient free space on the device
		 * TX queue. If not, return and let the TX-work-handler
		 * retry later.
		 */
		if (queue->tx_devq_packets == BCM43xx_PIO_MAXTXDEVQPACKETS)
			return -EBUSY;
		if (queue->tx_devq_used + octets > queue->tx_devq_size)
			return -EBUSY;
		/* Now poke the device. */
		pio_tx_write_fragment(queue, skb, packet);

		/* Account for the packet size.
		 * (We must not overflow the device TX queue)
		 */
		queue->tx_devq_packets++;
		queue->tx_devq_used += octets;

		assert(packet->xmitted_frags <= packet->txb->nr_frags);
		packet->xmitted_frags++;
		packet->xmitted_octets += octets;
	}
	list_move_tail(&packet->list, &queue->txrunning);

	return 0;
}

static void free_txpacket(struct bcm43xx_pio_txpacket *packet)
{
	struct bcm43xx_pioqueue *queue = packet->queue;

	ieee80211_txb_free(packet->txb);

	list_move(&packet->list, &packet->queue->txfree);

	assert(queue->tx_devq_used >= packet->xmitted_octets);
	queue->tx_devq_used -= packet->xmitted_octets;
	assert(queue->tx_devq_packets >= packet->xmitted_frags);
	queue->tx_devq_packets -= packet->xmitted_frags;
}

static void txwork_handler(void *d)
{
	struct bcm43xx_pioqueue *queue = d;
	unsigned long flags;
	struct bcm43xx_pio_txpacket *packet, *tmp_packet;
	int err;

	spin_lock_irqsave(&queue->txlock, flags);
	list_for_each_entry_safe(packet, tmp_packet, &queue->txqueue, list) {
		assert(packet->xmitted_frags < packet->txb->nr_frags);
		if (packet->xmitted_frags == 0) {
			int i;
			struct sk_buff *skb;

			/* Check if the device queue is big
			 * enough for every fragment. If not, drop the
			 * whole packet.
			 */
			for (i = 0; i < packet->txb->nr_frags; i++) {
				skb = packet->txb->fragments[i];
				if (unlikely(skb->len > queue->tx_devq_size)) {
					dprintkl(KERN_ERR PFX "PIO TX device queue too small. "
							      "Dropping packet...\n");
					free_txpacket(packet);
					goto next_packet;
				}
			}
		}
		/* Now try to transmit the packet.
		 * This may not completely succeed.
		 */
		err = pio_tx_packet(packet);
		if (err)
			break;
next_packet:
		continue;
	}
	spin_unlock_irqrestore(&queue->txlock, flags);
}

static void setup_txqueues(struct bcm43xx_pioqueue *queue)
{
	struct bcm43xx_pio_txpacket *packet;
	int i;

	for (i = 0; i < BCM43xx_PIO_MAXTXPACKETS; i++) {
		packet = queue->__tx_packets_cache + i;

		packet->queue = queue;
		INIT_LIST_HEAD(&packet->list);

		list_add(&packet->list, &queue->txfree);
	}
}

static
struct bcm43xx_pioqueue * bcm43xx_setup_pioqueue(struct bcm43xx_private *bcm,
						 u16 pio_mmio_base)
{
	struct bcm43xx_pioqueue *queue;
	u32 value;
	u16 qsize;

	queue = kmalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		goto out;
	memset(queue, 0, sizeof(*queue));

	queue->bcm = bcm;
	queue->mmio_base = pio_mmio_base;

	INIT_LIST_HEAD(&queue->txfree);
	INIT_LIST_HEAD(&queue->txqueue);
	INIT_LIST_HEAD(&queue->txrunning);
	spin_lock_init(&queue->txlock);
	INIT_WORK(&queue->txwork, txwork_handler, queue);

	value = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	value |= BCM43xx_SBF_XFER_REG_BYTESWAP;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, value);

	qsize = bcm43xx_read16(bcm, queue->mmio_base + BCM43xx_PIO_TXQBUFSIZE);
	if (qsize <= BCM43xx_PIO_TXQADJUST) {
		printk(KERN_ERR PFX "PIO tx queue too small (%u)\n", qsize);
		goto err_freequeue;
	}
	qsize -= BCM43xx_PIO_TXQADJUST;
	queue->tx_devq_size = qsize;

	setup_txqueues(queue);

out:
	return queue;

err_freequeue:
	kfree(queue);
	queue = NULL;
	goto out;
}

static void cancel_transfers(struct bcm43xx_pioqueue *queue)
{
	struct bcm43xx_pio_txpacket *packet, *tmp_packet;

	netif_tx_disable(queue->bcm->net_dev);
	assert(queue->bcm->shutting_down);
	cancel_delayed_work(&queue->txwork);
	flush_workqueue(queue->bcm->workqueue);

	list_for_each_entry_safe(packet, tmp_packet, &queue->txrunning, list)
		free_txpacket(packet);
	list_for_each_entry_safe(packet, tmp_packet, &queue->txqueue, list)
		free_txpacket(packet);
}

static void bcm43xx_destroy_pioqueue(struct bcm43xx_pioqueue *queue)
{
	if (!queue)
		return;

	cancel_transfers(queue);
	kfree(queue);
}

void bcm43xx_pio_free(struct bcm43xx_private *bcm)
{
	bcm43xx_destroy_pioqueue(bcm->current_core->pio->queue3);
	bcm->current_core->pio->queue3 = NULL;
	bcm43xx_destroy_pioqueue(bcm->current_core->pio->queue2);
	bcm->current_core->pio->queue2 = NULL;
	bcm43xx_destroy_pioqueue(bcm->current_core->pio->queue1);
	bcm->current_core->pio->queue1 = NULL;
	bcm43xx_destroy_pioqueue(bcm->current_core->pio->queue0);
	bcm->current_core->pio->queue0 = NULL;
}

int bcm43xx_pio_init(struct bcm43xx_private *bcm)
{
	struct bcm43xx_pioqueue *queue;
	int err = -ENOMEM;

	queue = bcm43xx_setup_pioqueue(bcm, BCM43xx_MMIO_PIO1_BASE);
	if (!queue)
		goto out;
	bcm->current_core->pio->queue0 = queue;

	queue = bcm43xx_setup_pioqueue(bcm, BCM43xx_MMIO_PIO2_BASE);
	if (!queue)
		goto err_destroy0;
	bcm->current_core->pio->queue1 = queue;

	queue = bcm43xx_setup_pioqueue(bcm, BCM43xx_MMIO_PIO3_BASE);
	if (!queue)
		goto err_destroy1;
	bcm->current_core->pio->queue2 = queue;

	queue = bcm43xx_setup_pioqueue(bcm, BCM43xx_MMIO_PIO4_BASE);
	if (!queue)
		goto err_destroy2;
	bcm->current_core->pio->queue3 = queue;

	if (bcm->current_core->rev < 3)
		bcm->irq_savedstate |= BCM43xx_IRQ_PIO_WORKAROUND;

	dprintk(KERN_INFO PFX "PIO initialized\n");
	err = 0;
out:
	return err;

err_destroy2:
	bcm43xx_destroy_pioqueue(bcm->current_core->pio->queue2);
	bcm->current_core->pio->queue2 = NULL;
err_destroy1:
	bcm43xx_destroy_pioqueue(bcm->current_core->pio->queue1);
	bcm->current_core->pio->queue1 = NULL;
err_destroy0:
	bcm43xx_destroy_pioqueue(bcm->current_core->pio->queue0);
	bcm->current_core->pio->queue0 = NULL;
	goto out;
}

static inline
int pio_transfer_txb(struct bcm43xx_pioqueue *queue,
		     struct ieee80211_txb *txb)
{
	struct bcm43xx_pio_txpacket *packet;
	unsigned long flags;
	u16 tmp;

	spin_lock_irqsave(&queue->txlock, flags);
	assert(!queue->tx_suspended);
	assert(!list_empty(&queue->txfree));

	tmp = bcm43xx_pio_read(queue, BCM43xx_PIO_TXCTL);
	if (tmp & BCM43xx_PIO_TXCTL_SUSPEND) {
		spin_unlock_irqrestore(&queue->txlock, flags);
		return -EBUSY;
	}

	packet = list_entry(queue->txfree.next, struct bcm43xx_pio_txpacket, list);

	packet->txb = txb;
	list_move_tail(&packet->list, &queue->txqueue);
	packet->xmitted_octets = 0;
	packet->xmitted_frags = 0;

	/* Suspend TX, if we are out of packets in the "free" queue. */
	if (unlikely(list_empty(&queue->txfree))) {
		netif_stop_queue(queue->bcm->net_dev);
		queue->tx_suspended = 1;
	}

	spin_unlock_irqrestore(&queue->txlock, flags);
	queue_work(queue->bcm->workqueue, &queue->txwork);

	return 0;
}

int fastcall bcm43xx_pio_transfer_txb(struct bcm43xx_private *bcm,
				      struct ieee80211_txb *txb)
{
	return pio_transfer_txb(bcm->current_core->pio->queue1, txb);
}

void fastcall
bcm43xx_pio_handle_xmitstatus(struct bcm43xx_private *bcm,
			      struct bcm43xx_xmitstatus *status)
{
	struct bcm43xx_pioqueue *queue;
	struct bcm43xx_pio_txpacket *packet;
	unsigned long flags;

	queue = parse_cookie(bcm, status->cookie, &packet);
	assert(queue);
	spin_lock_irqsave(&queue->txlock, flags);
	free_txpacket(packet);
	if (unlikely(queue->tx_suspended)) {
		queue->tx_suspended = 0;
		netif_wake_queue(queue->bcm->net_dev);
	}

	/* If there are packets on the txqueue,
	 * start the work handler again.
	 */
	if (!list_empty(&queue->txqueue)) {
		queue_work(queue->bcm->workqueue,
			   &queue->txwork);
	}
	spin_unlock_irqrestore(&queue->txlock, flags);
}

static void pio_rx_error(struct bcm43xx_pioqueue *queue,
			 const char *error)
{
	printk("PIO RX error: %s\n", error);
	bcm43xx_pio_write(queue, BCM43xx_PIO_RXCTL, BCM43xx_PIO_RXCTL_READY);
}

void fastcall
bcm43xx_pio_rx(struct bcm43xx_pioqueue *queue)
{
	u16 preamble[21] = { 0 };
	struct bcm43xx_rxhdr *rxhdr;
	u16 tmp;
	u16 len;
	int i, err;
	int preamble_readwords;
	struct sk_buff *skb;

	tmp = bcm43xx_pio_read(queue, BCM43xx_PIO_RXCTL);
	if (!(tmp & BCM43xx_PIO_RXCTL_DATAAVAILABLE)) {
		dprintkl(KERN_ERR PFX "PIO RX: No data available\n");
		return;
	}
	bcm43xx_pio_write(queue, BCM43xx_PIO_RXCTL, BCM43xx_PIO_RXCTL_DATAAVAILABLE);

	for (i = 0; i < 10; i++) {
		tmp = bcm43xx_pio_read(queue, BCM43xx_PIO_RXCTL);
		if (tmp & BCM43xx_PIO_RXCTL_READY)
			goto data_ready;
		udelay(10);
	}
	dprintkl(KERN_ERR PFX "PIO RX timed out\n");
	return;
data_ready:

	len = le16_to_cpu(bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA));
	if (unlikely(len > 0x700)) {
		pio_rx_error(queue, "len > 0x700");
		return;
	}
	if (unlikely(len == 0 && queue->mmio_base != BCM43xx_MMIO_PIO4_BASE)) {
		pio_rx_error(queue, "len == 0");
		return;
	}
	preamble[0] = cpu_to_le16(len);
	if (queue->mmio_base == BCM43xx_MMIO_PIO4_BASE)
		preamble_readwords = 14 / sizeof(u16);
	else
		preamble_readwords = 18 / sizeof(u16);
	for (i = 0; i < preamble_readwords; i++) {
		tmp = bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA);
		preamble[i + 1] = cpu_to_be16(tmp);
	}
	rxhdr = (struct bcm43xx_rxhdr *)preamble;
	if (unlikely(rxhdr->flags2 & BCM43xx_RXHDR_FLAGS2_INVALIDFRAME)) {
		pio_rx_error(queue, "invalid frame");
		if (queue->mmio_base == BCM43xx_MMIO_PIO1_BASE) {
			for (i = 0; i < 15; i++)
				bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA); /* dummy read. */
		}
		return;
	}
//FIXME
#if 0
	if (queue->mmio_base == BCM43xx_MMIO_PIO4_BASE) {
		bcm43xx_rx_transmitstatus(queue->bcm,
					  (const struct bcm43xx_hwxmitstatus *)(preamble + 1));
		return;
	}
#endif
	skb = dev_alloc_skb(len);
	if (unlikely(!skb)) {
		pio_rx_error(queue, "out of memory");
		return;
	}
	skb_put(skb, len);
	for (i = 0; i < len - 1; i += 2) {
		tmp = cpu_to_be16(bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA));
		*((u16 *)(skb->data + i)) = tmp;
	}
	if (len % 2) {
		tmp = cpu_to_be16(bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA));
		skb->data[len - 1] = (tmp & 0x00FF);
		skb->data[0] = (tmp & 0xFF00) >> 8;
	}
	err = bcm43xx_rx(queue->bcm, skb, rxhdr);
	if (unlikely(err))
		dev_kfree_skb_irq(skb);
}

/* vim: set ts=8 sw=8 sts=8: */
