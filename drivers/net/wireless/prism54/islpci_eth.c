/*
 *  Copyright (C) 2002 Intersil Americas Inc.
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include "prismcompat.h"
#include "isl_38xx.h"
#include "islpci_eth.h"
#include "islpci_mgt.h"
#include "oid_mgt.h"

/******************************************************************************
    Network Interface functions
******************************************************************************/
void
islpci_eth_cleanup_transmit(islpci_private *priv,
			    isl38xx_control_block *control_block)
{
	struct sk_buff *skb;
	u32 index;

	/* compare the control block read pointer with the free pointer */
	while (priv->free_data_tx !=
	       le32_to_cpu(control_block->
			   device_curr_frag[ISL38XX_CB_TX_DATA_LQ])) {
		/* read the index of the first fragment to be freed */
		index = priv->free_data_tx % ISL38XX_CB_TX_QSIZE;

		/* check for holes in the arrays caused by multi fragment frames
		 * searching for the last fragment of a frame */
		if (priv->pci_map_tx_address[index] != (dma_addr_t) NULL) {
			/* entry is the last fragment of a frame
			 * free the skb structure and unmap pci memory */
			skb = priv->data_low_tx[index];

#if VERBOSE > SHOW_ERROR_MESSAGES
			DEBUG(SHOW_TRACING,
			      "cleanup skb %p skb->data %p skb->len %u truesize %u\n ",
			      skb, skb->data, skb->len, skb->truesize);
#endif

			pci_unmap_single(priv->pdev,
					 priv->pci_map_tx_address[index],
					 skb->len, PCI_DMA_TODEVICE);
			dev_kfree_skb_irq(skb);
			skb = NULL;
		}
		/* increment the free data low queue pointer */
		priv->free_data_tx++;
	}
}

int
islpci_eth_transmit(struct sk_buff *skb, struct net_device *ndev)
{
	islpci_private *priv = netdev_priv(ndev);
	isl38xx_control_block *cb = priv->control_block;
	u32 index;
	dma_addr_t pci_map_address;
	int frame_size;
	isl38xx_fragment *fragment;
	int offset;
	struct sk_buff *newskb;
	int newskb_offset;
	unsigned long flags;
	unsigned char wds_mac[6];
	u32 curr_frag;
	int err = 0;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_FUNCTION_CALLS, "islpci_eth_transmit \n");
#endif

	/* lock the driver code */
	spin_lock_irqsave(&priv->slock, flags);

	/* check whether the destination queue has enough fragments for the frame */
	curr_frag = le32_to_cpu(cb->driver_curr_frag[ISL38XX_CB_TX_DATA_LQ]);
	if (unlikely(curr_frag - priv->free_data_tx >= ISL38XX_CB_TX_QSIZE)) {
		printk(KERN_ERR "%s: transmit device queue full when awake\n",
		       ndev->name);
		netif_stop_queue(ndev);

		/* trigger the device */
		isl38xx_w32_flush(priv->device_base, ISL38XX_DEV_INT_UPDATE,
				  ISL38XX_DEV_INT_REG);
		udelay(ISL38XX_WRITEIO_DELAY);

		err = -EBUSY;
		goto drop_free;
	}
	/* Check alignment and WDS frame formatting. The start of the packet should
	 * be aligned on a 4-byte boundary. If WDS is enabled add another 6 bytes
	 * and add WDS address information */
	if (likely(((long) skb->data & 0x03) | init_wds)) {
		/* get the number of bytes to add and re-allign */
		offset = (4 - (long) skb->data) & 0x03;
		offset += init_wds ? 6 : 0;

		/* check whether the current skb can be used  */
		if (!skb_cloned(skb) && (skb_tailroom(skb) >= offset)) {
			unsigned char *src = skb->data;

#if VERBOSE > SHOW_ERROR_MESSAGES
			DEBUG(SHOW_TRACING, "skb offset %i wds %i\n", offset,
			      init_wds);
#endif

			/* align the buffer on 4-byte boundary */
			skb_reserve(skb, (4 - (long) skb->data) & 0x03);
			if (init_wds) {
				/* wds requires an additional address field of 6 bytes */
				skb_put(skb, 6);
#ifdef ISLPCI_ETH_DEBUG
				printk("islpci_eth_transmit:wds_mac\n");
#endif
				memmove(skb->data + 6, src, skb->len);
				memcpy(skb->data, wds_mac, 6);
			} else {
				memmove(skb->data, src, skb->len);
			}

#if VERBOSE > SHOW_ERROR_MESSAGES
			DEBUG(SHOW_TRACING, "memmove %p %p %i \n", skb->data,
			      src, skb->len);
#endif
		} else {
			newskb =
			    dev_alloc_skb(init_wds ? skb->len + 6 : skb->len);
			if (unlikely(newskb == NULL)) {
				printk(KERN_ERR "%s: Cannot allocate skb\n",
				       ndev->name);
				err = -ENOMEM;
				goto drop_free;
			}
			newskb_offset = (4 - (long) newskb->data) & 0x03;

			/* Check if newskb->data is aligned */
			if (newskb_offset)
				skb_reserve(newskb, newskb_offset);

			skb_put(newskb, init_wds ? skb->len + 6 : skb->len);
			if (init_wds) {
				memcpy(newskb->data + 6, skb->data, skb->len);
				memcpy(newskb->data, wds_mac, 6);
#ifdef ISLPCI_ETH_DEBUG
				printk("islpci_eth_transmit:wds_mac\n");
#endif
			} else
				memcpy(newskb->data, skb->data, skb->len);

#if VERBOSE > SHOW_ERROR_MESSAGES
			DEBUG(SHOW_TRACING, "memcpy %p %p %i wds %i\n",
			      newskb->data, skb->data, skb->len, init_wds);
#endif

			newskb->dev = skb->dev;
			dev_kfree_skb_irq(skb);
			skb = newskb;
		}
	}
	/* display the buffer contents for debugging */
#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_BUFFER_CONTENTS, "\ntx %p ", skb->data);
	display_buffer((char *) skb->data, skb->len);
#endif

	/* map the skb buffer to pci memory for DMA operation */
	pci_map_address = pci_map_single(priv->pdev,
					 (void *) skb->data, skb->len,
					 PCI_DMA_TODEVICE);
	if (unlikely(pci_map_address == 0)) {
		printk(KERN_WARNING "%s: cannot map buffer to PCI\n",
		       ndev->name);

		err = -EIO;
		goto drop_free;
	}
	/* Place the fragment in the control block structure. */
	index = curr_frag % ISL38XX_CB_TX_QSIZE;
	fragment = &cb->tx_data_low[index];

	priv->pci_map_tx_address[index] = pci_map_address;
	/* store the skb address for future freeing  */
	priv->data_low_tx[index] = skb;
	/* set the proper fragment start address and size information */
	frame_size = skb->len;
	fragment->size = cpu_to_le16(frame_size);
	fragment->flags = cpu_to_le16(0);	/* set to 1 if more fragments */
	fragment->address = cpu_to_le32(pci_map_address);
	curr_frag++;

	/* The fragment address in the control block must have been
	 * written before announcing the frame buffer to device. */
	wmb();
	cb->driver_curr_frag[ISL38XX_CB_TX_DATA_LQ] = cpu_to_le32(curr_frag);

	if (curr_frag - priv->free_data_tx + ISL38XX_MIN_QTHRESHOLD
	    > ISL38XX_CB_TX_QSIZE) {
		/* stop sends from upper layers */
		netif_stop_queue(ndev);

		/* set the full flag for the transmission queue */
		priv->data_low_tx_full = 1;
	}

	/* set the transmission time */
	ndev->trans_start = jiffies;
	priv->statistics.tx_packets++;
	priv->statistics.tx_bytes += skb->len;

	/* trigger the device */
	islpci_trigger(priv);

	/* unlock the driver code */
	spin_unlock_irqrestore(&priv->slock, flags);

	return 0;

      drop_free:
	priv->statistics.tx_dropped++;
	spin_unlock_irqrestore(&priv->slock, flags);
	dev_kfree_skb(skb);
	return err;
}

static inline int
islpci_monitor_rx(islpci_private *priv, struct sk_buff **skb)
{
	/* The card reports full 802.11 packets but with a 20 bytes
	 * header and without the FCS. But there a is a bit that
	 * indicates if the packet is corrupted :-) */
	struct rfmon_header *hdr = (struct rfmon_header *) (*skb)->data;

	if (hdr->flags & 0x01)
		/* This one is bad. Drop it ! */
		return -1;
	if (priv->ndev->type == ARPHRD_IEEE80211_PRISM) {
		struct avs_80211_1_header *avs;
		/* extract the relevant data from the header */
		u32 clock = le32_to_cpu(hdr->clock);
		u8 rate = hdr->rate;
		u16 freq = le16_to_cpu(hdr->freq);
		u8 rssi = hdr->rssi;

		skb_pull(*skb, sizeof (struct rfmon_header));

		if (skb_headroom(*skb) < sizeof (struct avs_80211_1_header)) {
			struct sk_buff *newskb = skb_copy_expand(*skb,
								 sizeof (struct
									 avs_80211_1_header),
								 0, GFP_ATOMIC);
			if (newskb) {
				dev_kfree_skb_irq(*skb);
				*skb = newskb;
			} else
				return -1;
			/* This behavior is not very subtile... */
		}

		/* make room for the new header and fill it. */
		avs =
		    (struct avs_80211_1_header *) skb_push(*skb,
							   sizeof (struct
								   avs_80211_1_header));

		avs->version = cpu_to_be32(P80211CAPTURE_VERSION);
		avs->length = cpu_to_be32(sizeof (struct avs_80211_1_header));
		avs->mactime = cpu_to_be64(le64_to_cpu(clock));
		avs->hosttime = cpu_to_be64(jiffies);
		avs->phytype = cpu_to_be32(6);	/*OFDM: 6 for (g), 8 for (a) */
		avs->channel = cpu_to_be32(channel_of_freq(freq));
		avs->datarate = cpu_to_be32(rate * 5);
		avs->antenna = cpu_to_be32(0);	/*unknown */
		avs->priority = cpu_to_be32(0);	/*unknown */
		avs->ssi_type = cpu_to_be32(3);	/*2: dBm, 3: raw RSSI */
		avs->ssi_signal = cpu_to_be32(rssi & 0x7f);
		avs->ssi_noise = cpu_to_be32(priv->local_iwstatistics.qual.noise);	/*better than 'undefined', I assume */
		avs->preamble = cpu_to_be32(0);	/*unknown */
		avs->encoding = cpu_to_be32(0);	/*unknown */
	} else
		skb_pull(*skb, sizeof (struct rfmon_header));

	(*skb)->protocol = htons(ETH_P_802_2);
	(*skb)->mac.raw = (*skb)->data;
	(*skb)->pkt_type = PACKET_OTHERHOST;

	return 0;
}

int
islpci_eth_receive(islpci_private *priv)
{
	struct net_device *ndev = priv->ndev;
	isl38xx_control_block *control_block = priv->control_block;
	struct sk_buff *skb;
	u16 size;
	u32 index, offset;
	unsigned char *src;
	int discard = 0;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_FUNCTION_CALLS, "islpci_eth_receive \n");
#endif

	/* the device has written an Ethernet frame in the data area
	 * of the sk_buff without updating the structure, do it now */
	index = priv->free_data_rx % ISL38XX_CB_RX_QSIZE;
	size = le16_to_cpu(control_block->rx_data_low[index].size);
	skb = priv->data_low_rx[index];
	offset = ((unsigned long)
		  le32_to_cpu(control_block->rx_data_low[index].address) -
		  (unsigned long) skb->data) & 3;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_TRACING,
	      "frq->addr %x skb->data %p skb->len %u offset %u truesize %u\n ",
	      control_block->rx_data_low[priv->free_data_rx].address, skb->data,
	      skb->len, offset, skb->truesize);
#endif

	/* delete the streaming DMA mapping before processing the skb */
	pci_unmap_single(priv->pdev,
			 priv->pci_map_rx_address[index],
			 MAX_FRAGMENT_SIZE_RX + 2, PCI_DMA_FROMDEVICE);

	/* update the skb structure and allign the buffer */
	skb_put(skb, size);
	if (offset) {
		/* shift the buffer allocation offset bytes to get the right frame */
		skb_pull(skb, 2);
		skb_put(skb, 2);
	}
#if VERBOSE > SHOW_ERROR_MESSAGES
	/* display the buffer contents for debugging */
	DEBUG(SHOW_BUFFER_CONTENTS, "\nrx %p ", skb->data);
	display_buffer((char *) skb->data, skb->len);
#endif

	/* check whether WDS is enabled and whether the data frame is a WDS frame */

	if (init_wds) {
		/* WDS enabled, check for the wds address on the first 6 bytes of the buffer */
		src = skb->data + 6;
		memmove(skb->data, src, skb->len - 6);
		skb_trim(skb, skb->len - 6);
	}
#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_TRACING, "Fragment size %i in skb at %p\n", size, skb);
	DEBUG(SHOW_TRACING, "Skb data at %p, length %i\n", skb->data, skb->len);

	/* display the buffer contents for debugging */
	DEBUG(SHOW_BUFFER_CONTENTS, "\nrx %p ", skb->data);
	display_buffer((char *) skb->data, skb->len);
#endif

	/* do some additional sk_buff and network layer parameters */
	skb->dev = ndev;

	/* take care of monitor mode and spy monitoring. */
	if (unlikely(priv->iw_mode == IW_MODE_MONITOR))
		discard = islpci_monitor_rx(priv, &skb);
	else {
		if (unlikely(skb->data[2 * ETH_ALEN] == 0)) {
			/* The packet has a rx_annex. Read it for spy monitoring, Then
			 * remove it, while keeping the 2 leading MAC addr.
			 */
			struct iw_quality wstats;
			struct rx_annex_header *annex =
			    (struct rx_annex_header *) skb->data;
			wstats.level = annex->rfmon.rssi;
			/* The noise value can be a bit outdated if nobody's
			 * reading wireless stats... */
			wstats.noise = priv->local_iwstatistics.qual.noise;
			wstats.qual = wstats.level - wstats.noise;
			wstats.updated = 0x07;
			/* Update spy records */
			wireless_spy_update(ndev, annex->addr2, &wstats);

			memcpy(skb->data + sizeof (struct rfmon_header),
			       skb->data, 2 * ETH_ALEN);
			skb_pull(skb, sizeof (struct rfmon_header));
		}
		skb->protocol = eth_type_trans(skb, ndev);
	}
	skb->ip_summed = CHECKSUM_NONE;
	priv->statistics.rx_packets++;
	priv->statistics.rx_bytes += size;

	/* deliver the skb to the network layer */
#ifdef ISLPCI_ETH_DEBUG
	printk
	    ("islpci_eth_receive:netif_rx %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X\n",
	     skb->data[0], skb->data[1], skb->data[2], skb->data[3],
	     skb->data[4], skb->data[5]);
#endif
	if (unlikely(discard)) {
		dev_kfree_skb_irq(skb);
		skb = NULL;
	} else
		netif_rx(skb);

	/* increment the read index for the rx data low queue */
	priv->free_data_rx++;

	/* add one or more sk_buff structures */
	while (index =
	       le32_to_cpu(control_block->
			   driver_curr_frag[ISL38XX_CB_RX_DATA_LQ]),
	       index - priv->free_data_rx < ISL38XX_CB_RX_QSIZE) {
		/* allocate an sk_buff for received data frames storage
		 * include any required allignment operations */
		skb = dev_alloc_skb(MAX_FRAGMENT_SIZE_RX + 2);
		if (unlikely(skb == NULL)) {
			/* error allocating an sk_buff structure elements */
			DEBUG(SHOW_ERROR_MESSAGES, "Error allocating skb \n");
			break;
		}
		skb_reserve(skb, (4 - (long) skb->data) & 0x03);
		/* store the new skb structure pointer */
		index = index % ISL38XX_CB_RX_QSIZE;
		priv->data_low_rx[index] = skb;

#if VERBOSE > SHOW_ERROR_MESSAGES
		DEBUG(SHOW_TRACING,
		      "new alloc skb %p skb->data %p skb->len %u index %u truesize %u\n ",
		      skb, skb->data, skb->len, index, skb->truesize);
#endif

		/* set the streaming DMA mapping for proper PCI bus operation */
		priv->pci_map_rx_address[index] =
		    pci_map_single(priv->pdev, (void *) skb->data,
				   MAX_FRAGMENT_SIZE_RX + 2,
				   PCI_DMA_FROMDEVICE);
		if (unlikely(priv->pci_map_rx_address[index] == (dma_addr_t) NULL)) {
			/* error mapping the buffer to device accessable memory address */
			DEBUG(SHOW_ERROR_MESSAGES,
			      "Error mapping DMA address\n");

			/* free the skbuf structure before aborting */
			dev_kfree_skb_irq((struct sk_buff *) skb);
			skb = NULL;
			break;
		}
		/* update the fragment address */
		control_block->rx_data_low[index].address =
			cpu_to_le32((u32)priv->pci_map_rx_address[index]);
		wmb();

		/* increment the driver read pointer */
		add_le32p((u32 *) &control_block->
			  driver_curr_frag[ISL38XX_CB_RX_DATA_LQ], 1);
	}

	/* trigger the device */
	islpci_trigger(priv);

	return 0;
}

void
islpci_do_reset_and_wake(struct work_struct *work)
{
	islpci_private *priv = container_of(work, islpci_private, reset_task);

	islpci_reset(priv, 1);
	priv->reset_task_pending = 0;
	smp_wmb();
	netif_wake_queue(priv->ndev);
}

void
islpci_eth_tx_timeout(struct net_device *ndev)
{
	islpci_private *priv = netdev_priv(ndev);
	struct net_device_stats *statistics = &priv->statistics;

	/* increment the transmit error counter */
	statistics->tx_errors++;

	if (!priv->reset_task_pending) {
		printk(KERN_WARNING
			"%s: tx_timeout, scheduling reset", ndev->name);
		netif_stop_queue(ndev);
		priv->reset_task_pending = 1;
		schedule_work(&priv->reset_task);
	} else {
		printk(KERN_WARNING
			"%s: tx_timeout, waiting for reset", ndev->name);
	}
}
