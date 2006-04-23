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
#include "bcm43xx_xmit.h"
#include "bcm43xx_power.h"

#include <linux/delay.h>


static void tx_start(struct bcm43xx_pioqueue *queue)
{
	bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
			  BCM43xx_PIO_TXCTL_INIT);
}

static void tx_octet(struct bcm43xx_pioqueue *queue,
		     u8 octet)
{
	if (queue->need_workarounds) {
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA,
				  octet);
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
				  BCM43xx_PIO_TXCTL_WRITELO);
	} else {
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
				  BCM43xx_PIO_TXCTL_WRITELO);
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA,
				  octet);
	}
}

static u16 tx_get_next_word(struct bcm43xx_txhdr *txhdr,
			    const u8 *packet,
			    unsigned int *pos)
{
	const u8 *source;
	unsigned int i = *pos;
	u16 ret;

	if (i < sizeof(*txhdr)) {
		source = (const u8 *)txhdr;
	} else {
		source = packet;
		i -= sizeof(*txhdr);
	}
	ret = le16_to_cpu( *((u16 *)(source + i)) );
	*pos += 2;

	return ret;
}

static void tx_data(struct bcm43xx_pioqueue *queue,
		    struct bcm43xx_txhdr *txhdr,
		    const u8 *packet,
		    unsigned int octets)
{
	u16 data;
	unsigned int i = 0;

	if (queue->need_workarounds) {
		data = tx_get_next_word(txhdr, packet, &i);
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA, data);
	}
	bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
			  BCM43xx_PIO_TXCTL_WRITELO |
			  BCM43xx_PIO_TXCTL_WRITEHI);
	while (i < octets - 1) {
		data = tx_get_next_word(txhdr, packet, &i);
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA, data);
	}
	if (octets % 2)
		tx_octet(queue, packet[octets - sizeof(*txhdr) - 1]);
}

static void tx_complete(struct bcm43xx_pioqueue *queue,
			struct sk_buff *skb)
{
	if (queue->need_workarounds) {
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXDATA,
				  skb->data[skb->len - 1]);
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
				  BCM43xx_PIO_TXCTL_WRITELO |
				  BCM43xx_PIO_TXCTL_COMPLETE);
	} else {
		bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
				  BCM43xx_PIO_TXCTL_COMPLETE);
	}
}

static u16 generate_cookie(struct bcm43xx_pioqueue *queue,
			   struct bcm43xx_pio_txpacket *packet)
{
	u16 cookie = 0x0000;
	int packetindex;

	/* We use the upper 4 bits for the PIO
	 * controller ID and the lower 12 bits
	 * for the packet index (in the cache).
	 */
	switch (queue->mmio_base) {
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
	default:
		assert(0);
	}
	packetindex = pio_txpacket_getindex(packet);
	assert(((u16)packetindex & 0xF000) == 0x0000);
	cookie |= (u16)packetindex;

	return cookie;
}

static
struct bcm43xx_pioqueue * parse_cookie(struct bcm43xx_private *bcm,
				       u16 cookie,
				       struct bcm43xx_pio_txpacket **packet)
{
	struct bcm43xx_pio *pio = bcm43xx_current_pio(bcm);
	struct bcm43xx_pioqueue *queue = NULL;
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
		assert(0);
	}
	packetindex = (cookie & 0x0FFF);
	assert(packetindex >= 0 && packetindex < BCM43xx_PIO_MAXTXPACKETS);
	*packet = &(queue->tx_packets_cache[packetindex]);

	return queue;
}

static void pio_tx_write_fragment(struct bcm43xx_pioqueue *queue,
				  struct sk_buff *skb,
				  struct bcm43xx_pio_txpacket *packet)
{
	struct bcm43xx_txhdr txhdr;
	unsigned int octets;

	assert(skb_shinfo(skb)->nr_frags == 0);
	bcm43xx_generate_txhdr(queue->bcm,
			       &txhdr, skb->data, skb->len,
			       (packet->xmitted_frags == 0),
			       generate_cookie(queue, packet));

	tx_start(queue);
	octets = skb->len + sizeof(txhdr);
	if (queue->need_workarounds)
		octets--;
	tx_data(queue, &txhdr, (u8 *)skb->data, octets);
	tx_complete(queue, skb);
}

static void free_txpacket(struct bcm43xx_pio_txpacket *packet,
			  int irq_context)
{
	struct bcm43xx_pioqueue *queue = packet->queue;

	ieee80211_txb_free(packet->txb);
	list_move(&packet->list, &queue->txfree);
	queue->nr_txfree++;

	assert(queue->tx_devq_used >= packet->xmitted_octets);
	assert(queue->tx_devq_packets >= packet->xmitted_frags);
	queue->tx_devq_used -= packet->xmitted_octets;
	queue->tx_devq_packets -= packet->xmitted_frags;
}

static int pio_tx_packet(struct bcm43xx_pio_txpacket *packet)
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
		 * TX queue. If not, return and let the TX tasklet
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

		assert(packet->xmitted_frags < packet->txb->nr_frags);
		packet->xmitted_frags++;
		packet->xmitted_octets += octets;
	}
	list_move_tail(&packet->list, &queue->txrunning);

	return 0;
}

static void tx_tasklet(unsigned long d)
{
	struct bcm43xx_pioqueue *queue = (struct bcm43xx_pioqueue *)d;
	struct bcm43xx_private *bcm = queue->bcm;
	unsigned long flags;
	struct bcm43xx_pio_txpacket *packet, *tmp_packet;
	int err;
	u16 txctl;

	bcm43xx_lock_mmio(bcm, flags);

	txctl = bcm43xx_pio_read(queue, BCM43xx_PIO_TXCTL);
	if (txctl & BCM43xx_PIO_TXCTL_SUSPEND)
		goto out_unlock;

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
							      "Dropping packet.\n");
					free_txpacket(packet, 1);
					goto next_packet;
				}
			}
		}
		/* Try to transmit the packet.
		 * This may not completely succeed.
		 */
		err = pio_tx_packet(packet);
		if (err)
			break;
	next_packet:
		continue;
	}
out_unlock:
	bcm43xx_unlock_mmio(bcm, flags);
}

static void setup_txqueues(struct bcm43xx_pioqueue *queue)
{
	struct bcm43xx_pio_txpacket *packet;
	int i;

	queue->nr_txfree = BCM43xx_PIO_MAXTXPACKETS;
	for (i = 0; i < BCM43xx_PIO_MAXTXPACKETS; i++) {
		packet = &(queue->tx_packets_cache[i]);

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

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		goto out;

	queue->bcm = bcm;
	queue->mmio_base = pio_mmio_base;
	queue->need_workarounds = (bcm->current_core->rev < 3);

	INIT_LIST_HEAD(&queue->txfree);
	INIT_LIST_HEAD(&queue->txqueue);
	INIT_LIST_HEAD(&queue->txrunning);
	tasklet_init(&queue->txtask, tx_tasklet,
		     (unsigned long)queue);

	value = bcm43xx_read32(bcm, BCM43xx_MMIO_STATUS_BITFIELD);
	value &= ~BCM43xx_SBF_XFER_REG_BYTESWAP;
	bcm43xx_write32(bcm, BCM43xx_MMIO_STATUS_BITFIELD, value);

	qsize = bcm43xx_read16(bcm, queue->mmio_base + BCM43xx_PIO_TXQBUFSIZE);
	if (qsize == 0) {
		printk(KERN_ERR PFX "ERROR: This card does not support PIO "
				    "operation mode. Please use DMA mode "
				    "(module parameter pio=0).\n");
		goto err_freequeue;
	}
	if (qsize <= BCM43xx_PIO_TXQADJUST) {
		printk(KERN_ERR PFX "PIO tx device-queue too small (%u)\n",
		       qsize);
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
	tasklet_disable(&queue->txtask);

	list_for_each_entry_safe(packet, tmp_packet, &queue->txrunning, list)
		free_txpacket(packet, 0);
	list_for_each_entry_safe(packet, tmp_packet, &queue->txqueue, list)
		free_txpacket(packet, 0);
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
	struct bcm43xx_pio *pio;

	if (!bcm43xx_using_pio(bcm))
		return;
	pio = bcm43xx_current_pio(bcm);

	bcm43xx_destroy_pioqueue(pio->queue3);
	pio->queue3 = NULL;
	bcm43xx_destroy_pioqueue(pio->queue2);
	pio->queue2 = NULL;
	bcm43xx_destroy_pioqueue(pio->queue1);
	pio->queue1 = NULL;
	bcm43xx_destroy_pioqueue(pio->queue0);
	pio->queue0 = NULL;
}

int bcm43xx_pio_init(struct bcm43xx_private *bcm)
{
	struct bcm43xx_pio *pio = bcm43xx_current_pio(bcm);
	struct bcm43xx_pioqueue *queue;
	int err = -ENOMEM;

	queue = bcm43xx_setup_pioqueue(bcm, BCM43xx_MMIO_PIO1_BASE);
	if (!queue)
		goto out;
	pio->queue0 = queue;

	queue = bcm43xx_setup_pioqueue(bcm, BCM43xx_MMIO_PIO2_BASE);
	if (!queue)
		goto err_destroy0;
	pio->queue1 = queue;

	queue = bcm43xx_setup_pioqueue(bcm, BCM43xx_MMIO_PIO3_BASE);
	if (!queue)
		goto err_destroy1;
	pio->queue2 = queue;

	queue = bcm43xx_setup_pioqueue(bcm, BCM43xx_MMIO_PIO4_BASE);
	if (!queue)
		goto err_destroy2;
	pio->queue3 = queue;

	if (bcm->current_core->rev < 3)
		bcm->irq_savedstate |= BCM43xx_IRQ_PIO_WORKAROUND;

	dprintk(KERN_INFO PFX "PIO initialized\n");
	err = 0;
out:
	return err;

err_destroy2:
	bcm43xx_destroy_pioqueue(pio->queue2);
	pio->queue2 = NULL;
err_destroy1:
	bcm43xx_destroy_pioqueue(pio->queue1);
	pio->queue1 = NULL;
err_destroy0:
	bcm43xx_destroy_pioqueue(pio->queue0);
	pio->queue0 = NULL;
	goto out;
}

int bcm43xx_pio_tx(struct bcm43xx_private *bcm,
		   struct ieee80211_txb *txb)
{
	struct bcm43xx_pioqueue *queue = bcm43xx_current_pio(bcm)->queue1;
	struct bcm43xx_pio_txpacket *packet;

	assert(!queue->tx_suspended);
	assert(!list_empty(&queue->txfree));

	packet = list_entry(queue->txfree.next, struct bcm43xx_pio_txpacket, list);
	packet->txb = txb;
	packet->xmitted_frags = 0;
	packet->xmitted_octets = 0;
	list_move_tail(&packet->list, &queue->txqueue);
	queue->nr_txfree--;
	assert(queue->nr_txfree < BCM43xx_PIO_MAXTXPACKETS);

	/* Suspend TX, if we are out of packets in the "free" queue. */
	if (list_empty(&queue->txfree)) {
		netif_stop_queue(queue->bcm->net_dev);
		queue->tx_suspended = 1;
	}

	tasklet_schedule(&queue->txtask);

	return 0;
}

void bcm43xx_pio_handle_xmitstatus(struct bcm43xx_private *bcm,
				   struct bcm43xx_xmitstatus *status)
{
	struct bcm43xx_pioqueue *queue;
	struct bcm43xx_pio_txpacket *packet;

	queue = parse_cookie(bcm, status->cookie, &packet);
	assert(queue);

	free_txpacket(packet, 1);
	if (queue->tx_suspended) {
		queue->tx_suspended = 0;
		netif_wake_queue(queue->bcm->net_dev);
	}
	/* If there are packets on the txqueue, poke the tasklet
	 * to transmit them.
	 */
	if (!list_empty(&queue->txqueue))
		tasklet_schedule(&queue->txtask);
}

static void pio_rx_error(struct bcm43xx_pioqueue *queue,
			 int clear_buffers,
			 const char *error)
{
	int i;

	printkl("PIO RX error: %s\n", error);
	bcm43xx_pio_write(queue, BCM43xx_PIO_RXCTL,
			  BCM43xx_PIO_RXCTL_READY);
	if (clear_buffers) {
		assert(queue->mmio_base == BCM43xx_MMIO_PIO1_BASE);
		for (i = 0; i < 15; i++) {
			/* Dummy read. */
			bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA);
		}
	}
}

void bcm43xx_pio_rx(struct bcm43xx_pioqueue *queue)
{
	u16 preamble[21] = { 0 };
	struct bcm43xx_rxhdr *rxhdr;
	u16 tmp, len, rxflags2;
	int i, preamble_readwords;
	struct sk_buff *skb;

	tmp = bcm43xx_pio_read(queue, BCM43xx_PIO_RXCTL);
	if (!(tmp & BCM43xx_PIO_RXCTL_DATAAVAILABLE))
		return;
	bcm43xx_pio_write(queue, BCM43xx_PIO_RXCTL,
			  BCM43xx_PIO_RXCTL_DATAAVAILABLE);

	for (i = 0; i < 10; i++) {
		tmp = bcm43xx_pio_read(queue, BCM43xx_PIO_RXCTL);
		if (tmp & BCM43xx_PIO_RXCTL_READY)
			goto data_ready;
		udelay(10);
	}
	dprintkl(KERN_ERR PFX "PIO RX timed out\n");
	return;
data_ready:

	len = bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA);
	if (unlikely(len > 0x700)) {
		pio_rx_error(queue, 0, "len > 0x700");
		return;
	}
	if (unlikely(len == 0 && queue->mmio_base != BCM43xx_MMIO_PIO4_BASE)) {
		pio_rx_error(queue, 0, "len == 0");
		return;
	}
	preamble[0] = cpu_to_le16(len);
	if (queue->mmio_base == BCM43xx_MMIO_PIO4_BASE)
		preamble_readwords = 14 / sizeof(u16);
	else
		preamble_readwords = 18 / sizeof(u16);
	for (i = 0; i < preamble_readwords; i++) {
		tmp = bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA);
		preamble[i + 1] = cpu_to_le16(tmp);
	}
	rxhdr = (struct bcm43xx_rxhdr *)preamble;
	rxflags2 = le16_to_cpu(rxhdr->flags2);
	if (unlikely(rxflags2 & BCM43xx_RXHDR_FLAGS2_INVALIDFRAME)) {
		pio_rx_error(queue,
			     (queue->mmio_base == BCM43xx_MMIO_PIO1_BASE),
			     "invalid frame");
		return;
	}
	if (queue->mmio_base == BCM43xx_MMIO_PIO4_BASE) {
		/* We received an xmit status. */
		struct bcm43xx_hwxmitstatus *hw;
		struct bcm43xx_xmitstatus stat;

		hw = (struct bcm43xx_hwxmitstatus *)(preamble + 1);
		stat.cookie = le16_to_cpu(hw->cookie);
		stat.flags = hw->flags;
		stat.cnt1 = hw->cnt1;
		stat.cnt2 = hw->cnt2;
		stat.seq = le16_to_cpu(hw->seq);
		stat.unknown = le16_to_cpu(hw->unknown);

		bcm43xx_debugfs_log_txstat(queue->bcm, &stat);
		bcm43xx_pio_handle_xmitstatus(queue->bcm, &stat);

		return;
	}

	skb = dev_alloc_skb(len);
	if (unlikely(!skb)) {
		pio_rx_error(queue, 1, "OOM");
		return;
	}
	skb_put(skb, len);
	for (i = 0; i < len - 1; i += 2) {
		tmp = bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA);
		*((u16 *)(skb->data + i)) = cpu_to_le16(tmp);
	}
	if (len % 2) {
		tmp = bcm43xx_pio_read(queue, BCM43xx_PIO_RXDATA);
		skb->data[len - 1] = (tmp & 0x00FF);
/* The specs say the following is required, but
 * it is wrong and corrupts the PLCP. If we don't do
 * this, the PLCP seems to be correct. So ifdef it out for now.
 */
#if 0
		if (rxflags2 & BCM43xx_RXHDR_FLAGS2_TYPE2FRAME)
			skb->data[2] = (tmp & 0xFF00) >> 8;
		else
			skb->data[0] = (tmp & 0xFF00) >> 8;
#endif
	}
	skb_trim(skb, len - IEEE80211_FCS_LEN);
	bcm43xx_rx(queue->bcm, skb, rxhdr);
}

void bcm43xx_pio_tx_suspend(struct bcm43xx_pioqueue *queue)
{
	bcm43xx_power_saving_ctl_bits(queue->bcm, -1, 1);
	bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
			  bcm43xx_pio_read(queue, BCM43xx_PIO_TXCTL)
			  | BCM43xx_PIO_TXCTL_SUSPEND);
}

void bcm43xx_pio_tx_resume(struct bcm43xx_pioqueue *queue)
{
	bcm43xx_pio_write(queue, BCM43xx_PIO_TXCTL,
			  bcm43xx_pio_read(queue, BCM43xx_PIO_TXCTL)
			  & ~BCM43xx_PIO_TXCTL_SUSPEND);
	bcm43xx_power_saving_ctl_bits(queue->bcm, -1, -1);
	tasklet_schedule(&queue->txtask);
}
