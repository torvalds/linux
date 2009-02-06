/* A simple network driver using virtio.
 *
 * Copyright 2007 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
//#define DEBUG
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/scatterlist.h>
#include <linux/if_vlan.h>

static int napi_weight = 128;
module_param(napi_weight, int, 0444);

static int csum = 1, gso = 1;
module_param(csum, bool, 0444);
module_param(gso, bool, 0444);

/* FIXME: MTU in config. */
#define MAX_PACKET_LEN (ETH_HLEN + VLAN_HLEN + ETH_DATA_LEN)
#define GOOD_COPY_LEN	128

struct virtnet_info
{
	struct virtio_device *vdev;
	struct virtqueue *rvq, *svq;
	struct net_device *dev;
	struct napi_struct napi;

	/* The skb we couldn't send because buffers were full. */
	struct sk_buff *last_xmit_skb;

	/* If we need to free in a timer, this is it. */
	struct timer_list xmit_free_timer;

	/* Number of input buffers, and max we've ever had. */
	unsigned int num, max;

	/* For cleaning up after transmission. */
	struct tasklet_struct tasklet;
	bool free_in_tasklet;

	/* I like... big packets and I cannot lie! */
	bool big_packets;

	/* Host will merge rx buffers for big packets (shake it! shake it!) */
	bool mergeable_rx_bufs;

	/* Receive & send queues. */
	struct sk_buff_head recv;
	struct sk_buff_head send;

	/* Chain pages by the private ptr. */
	struct page *pages;
};

static inline void *skb_vnet_hdr(struct sk_buff *skb)
{
	return (struct virtio_net_hdr *)skb->cb;
}

static void give_a_page(struct virtnet_info *vi, struct page *page)
{
	page->private = (unsigned long)vi->pages;
	vi->pages = page;
}

static void trim_pages(struct virtnet_info *vi, struct sk_buff *skb)
{
	unsigned int i;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++)
		give_a_page(vi, skb_shinfo(skb)->frags[i].page);
	skb_shinfo(skb)->nr_frags = 0;
	skb->data_len = 0;
}

static struct page *get_a_page(struct virtnet_info *vi, gfp_t gfp_mask)
{
	struct page *p = vi->pages;

	if (p)
		vi->pages = (struct page *)p->private;
	else
		p = alloc_page(gfp_mask);
	return p;
}

static void skb_xmit_done(struct virtqueue *svq)
{
	struct virtnet_info *vi = svq->vdev->priv;

	/* Suppress further interrupts. */
	svq->vq_ops->disable_cb(svq);

	/* We were probably waiting for more output buffers. */
	netif_wake_queue(vi->dev);

	/* Make sure we re-xmit last_xmit_skb: if there are no more packets
	 * queued, start_xmit won't be called. */
	tasklet_schedule(&vi->tasklet);
}

static void receive_skb(struct net_device *dev, struct sk_buff *skb,
			unsigned len)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct virtio_net_hdr *hdr = skb_vnet_hdr(skb);
	int err;
	int i;

	if (unlikely(len < sizeof(struct virtio_net_hdr) + ETH_HLEN)) {
		pr_debug("%s: short packet %i\n", dev->name, len);
		dev->stats.rx_length_errors++;
		goto drop;
	}

	if (vi->mergeable_rx_bufs) {
		struct virtio_net_hdr_mrg_rxbuf *mhdr = skb_vnet_hdr(skb);
		unsigned int copy;
		char *p = page_address(skb_shinfo(skb)->frags[0].page);

		if (len > PAGE_SIZE)
			len = PAGE_SIZE;
		len -= sizeof(struct virtio_net_hdr_mrg_rxbuf);

		memcpy(hdr, p, sizeof(*mhdr));
		p += sizeof(*mhdr);

		copy = len;
		if (copy > skb_tailroom(skb))
			copy = skb_tailroom(skb);

		memcpy(skb_put(skb, copy), p, copy);

		len -= copy;

		if (!len) {
			give_a_page(vi, skb_shinfo(skb)->frags[0].page);
			skb_shinfo(skb)->nr_frags--;
		} else {
			skb_shinfo(skb)->frags[0].page_offset +=
				sizeof(*mhdr) + copy;
			skb_shinfo(skb)->frags[0].size = len;
			skb->data_len += len;
			skb->len += len;
		}

		while (--mhdr->num_buffers) {
			struct sk_buff *nskb;

			i = skb_shinfo(skb)->nr_frags;
			if (i >= MAX_SKB_FRAGS) {
				pr_debug("%s: packet too long %d\n", dev->name,
					 len);
				dev->stats.rx_length_errors++;
				goto drop;
			}

			nskb = vi->rvq->vq_ops->get_buf(vi->rvq, &len);
			if (!nskb) {
				pr_debug("%s: rx error: %d buffers missing\n",
					 dev->name, mhdr->num_buffers);
				dev->stats.rx_length_errors++;
				goto drop;
			}

			__skb_unlink(nskb, &vi->recv);
			vi->num--;

			skb_shinfo(skb)->frags[i] = skb_shinfo(nskb)->frags[0];
			skb_shinfo(nskb)->nr_frags = 0;
			kfree_skb(nskb);

			if (len > PAGE_SIZE)
				len = PAGE_SIZE;

			skb_shinfo(skb)->frags[i].size = len;
			skb_shinfo(skb)->nr_frags++;
			skb->data_len += len;
			skb->len += len;
		}
	} else {
		len -= sizeof(struct virtio_net_hdr);

		if (len <= MAX_PACKET_LEN)
			trim_pages(vi, skb);

		err = pskb_trim(skb, len);
		if (err) {
			pr_debug("%s: pskb_trim failed %i %d\n", dev->name,
				 len, err);
			dev->stats.rx_dropped++;
			goto drop;
		}
	}

	skb->truesize += skb->data_len;
	dev->stats.rx_bytes += skb->len;
	dev->stats.rx_packets++;

	if (hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
		pr_debug("Needs csum!\n");
		if (!skb_partial_csum_set(skb,hdr->csum_start,hdr->csum_offset))
			goto frame_err;
	}

	skb->protocol = eth_type_trans(skb, dev);
	pr_debug("Receiving skb proto 0x%04x len %i type %i\n",
		 ntohs(skb->protocol), skb->len, skb->pkt_type);

	if (hdr->gso_type != VIRTIO_NET_HDR_GSO_NONE) {
		pr_debug("GSO!\n");
		switch (hdr->gso_type & ~VIRTIO_NET_HDR_GSO_ECN) {
		case VIRTIO_NET_HDR_GSO_TCPV4:
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;
			break;
		case VIRTIO_NET_HDR_GSO_UDP:
			skb_shinfo(skb)->gso_type = SKB_GSO_UDP;
			break;
		case VIRTIO_NET_HDR_GSO_TCPV6:
			skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
			break;
		default:
			if (net_ratelimit())
				printk(KERN_WARNING "%s: bad gso type %u.\n",
				       dev->name, hdr->gso_type);
			goto frame_err;
		}

		if (hdr->gso_type & VIRTIO_NET_HDR_GSO_ECN)
			skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;

		skb_shinfo(skb)->gso_size = hdr->gso_size;
		if (skb_shinfo(skb)->gso_size == 0) {
			if (net_ratelimit())
				printk(KERN_WARNING "%s: zero gso size.\n",
				       dev->name);
			goto frame_err;
		}

		/* Header must be checked, and gso_segs computed. */
		skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
		skb_shinfo(skb)->gso_segs = 0;
	}

	netif_receive_skb(skb);
	return;

frame_err:
	dev->stats.rx_frame_errors++;
drop:
	dev_kfree_skb(skb);
}

static void try_fill_recv_maxbufs(struct virtnet_info *vi)
{
	struct sk_buff *skb;
	struct scatterlist sg[2+MAX_SKB_FRAGS];
	int num, err, i;

	sg_init_table(sg, 2+MAX_SKB_FRAGS);
	for (;;) {
		struct virtio_net_hdr *hdr;

		skb = netdev_alloc_skb(vi->dev, MAX_PACKET_LEN);
		if (unlikely(!skb))
			break;

		skb_put(skb, MAX_PACKET_LEN);

		hdr = skb_vnet_hdr(skb);
		sg_set_buf(sg, hdr, sizeof(*hdr));

		if (vi->big_packets) {
			for (i = 0; i < MAX_SKB_FRAGS; i++) {
				skb_frag_t *f = &skb_shinfo(skb)->frags[i];
				f->page = get_a_page(vi, GFP_ATOMIC);
				if (!f->page)
					break;

				f->page_offset = 0;
				f->size = PAGE_SIZE;

				skb->data_len += PAGE_SIZE;
				skb->len += PAGE_SIZE;

				skb_shinfo(skb)->nr_frags++;
			}
		}

		num = skb_to_sgvec(skb, sg+1, 0, skb->len) + 1;
		skb_queue_head(&vi->recv, skb);

		err = vi->rvq->vq_ops->add_buf(vi->rvq, sg, 0, num, skb);
		if (err) {
			skb_unlink(skb, &vi->recv);
			trim_pages(vi, skb);
			kfree_skb(skb);
			break;
		}
		vi->num++;
	}
	if (unlikely(vi->num > vi->max))
		vi->max = vi->num;
	vi->rvq->vq_ops->kick(vi->rvq);
}

static void try_fill_recv(struct virtnet_info *vi)
{
	struct sk_buff *skb;
	struct scatterlist sg[1];
	int err;

	if (!vi->mergeable_rx_bufs) {
		try_fill_recv_maxbufs(vi);
		return;
	}

	for (;;) {
		skb_frag_t *f;

		skb = netdev_alloc_skb(vi->dev, GOOD_COPY_LEN + NET_IP_ALIGN);
		if (unlikely(!skb))
			break;

		skb_reserve(skb, NET_IP_ALIGN);

		f = &skb_shinfo(skb)->frags[0];
		f->page = get_a_page(vi, GFP_ATOMIC);
		if (!f->page) {
			kfree_skb(skb);
			break;
		}

		f->page_offset = 0;
		f->size = PAGE_SIZE;

		skb_shinfo(skb)->nr_frags++;

		sg_init_one(sg, page_address(f->page), PAGE_SIZE);
		skb_queue_head(&vi->recv, skb);

		err = vi->rvq->vq_ops->add_buf(vi->rvq, sg, 0, 1, skb);
		if (err) {
			skb_unlink(skb, &vi->recv);
			kfree_skb(skb);
			break;
		}
		vi->num++;
	}
	if (unlikely(vi->num > vi->max))
		vi->max = vi->num;
	vi->rvq->vq_ops->kick(vi->rvq);
}

static void skb_recv_done(struct virtqueue *rvq)
{
	struct virtnet_info *vi = rvq->vdev->priv;
	/* Schedule NAPI, Suppress further interrupts if successful. */
	if (netif_rx_schedule_prep(&vi->napi)) {
		rvq->vq_ops->disable_cb(rvq);
		__netif_rx_schedule(&vi->napi);
	}
}

static int virtnet_poll(struct napi_struct *napi, int budget)
{
	struct virtnet_info *vi = container_of(napi, struct virtnet_info, napi);
	struct sk_buff *skb = NULL;
	unsigned int len, received = 0;

again:
	while (received < budget &&
	       (skb = vi->rvq->vq_ops->get_buf(vi->rvq, &len)) != NULL) {
		__skb_unlink(skb, &vi->recv);
		receive_skb(vi->dev, skb, len);
		vi->num--;
		received++;
	}

	/* FIXME: If we oom and completely run out of inbufs, we need
	 * to start a timer trying to fill more. */
	if (vi->num < vi->max / 2)
		try_fill_recv(vi);

	/* Out of packets? */
	if (received < budget) {
		netif_rx_complete(napi);
		if (unlikely(!vi->rvq->vq_ops->enable_cb(vi->rvq))
		    && napi_schedule_prep(napi)) {
			vi->rvq->vq_ops->disable_cb(vi->rvq);
			__netif_rx_schedule(napi);
			goto again;
		}
	}

	return received;
}

static void free_old_xmit_skbs(struct virtnet_info *vi)
{
	struct sk_buff *skb;
	unsigned int len;

	while ((skb = vi->svq->vq_ops->get_buf(vi->svq, &len)) != NULL) {
		pr_debug("Sent skb %p\n", skb);
		__skb_unlink(skb, &vi->send);
		vi->dev->stats.tx_bytes += skb->len;
		vi->dev->stats.tx_packets++;
		kfree_skb(skb);
	}
}

/* If the virtio transport doesn't always notify us when all in-flight packets
 * are consumed, we fall back to using this function on a timer to free them. */
static void xmit_free(unsigned long data)
{
	struct virtnet_info *vi = (void *)data;

	netif_tx_lock(vi->dev);

	free_old_xmit_skbs(vi);

	if (!skb_queue_empty(&vi->send))
		mod_timer(&vi->xmit_free_timer, jiffies + (HZ/10));

	netif_tx_unlock(vi->dev);
}

static int xmit_skb(struct virtnet_info *vi, struct sk_buff *skb)
{
	int num, err;
	struct scatterlist sg[2+MAX_SKB_FRAGS];
	struct virtio_net_hdr_mrg_rxbuf *mhdr = skb_vnet_hdr(skb);
	struct virtio_net_hdr *hdr = skb_vnet_hdr(skb);
	const unsigned char *dest = ((struct ethhdr *)skb->data)->h_dest;

	sg_init_table(sg, 2+MAX_SKB_FRAGS);

	pr_debug("%s: xmit %p %pM\n", vi->dev->name, skb, dest);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		hdr->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
		hdr->csum_start = skb->csum_start - skb_headroom(skb);
		hdr->csum_offset = skb->csum_offset;
	} else {
		hdr->flags = 0;
		hdr->csum_offset = hdr->csum_start = 0;
	}

	if (skb_is_gso(skb)) {
		hdr->hdr_len = skb_transport_header(skb) - skb->data;
		hdr->gso_size = skb_shinfo(skb)->gso_size;
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4)
			hdr->gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
			hdr->gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP)
			hdr->gso_type = VIRTIO_NET_HDR_GSO_UDP;
		else
			BUG();
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCP_ECN)
			hdr->gso_type |= VIRTIO_NET_HDR_GSO_ECN;
	} else {
		hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
		hdr->gso_size = hdr->hdr_len = 0;
	}

	mhdr->num_buffers = 0;

	/* Encode metadata header at front. */
	if (vi->mergeable_rx_bufs)
		sg_set_buf(sg, mhdr, sizeof(*mhdr));
	else
		sg_set_buf(sg, hdr, sizeof(*hdr));

	num = skb_to_sgvec(skb, sg+1, 0, skb->len) + 1;

	err = vi->svq->vq_ops->add_buf(vi->svq, sg, num, 0, skb);
	if (!err && !vi->free_in_tasklet)
		mod_timer(&vi->xmit_free_timer, jiffies + (HZ/10));

	return err;
}

static void xmit_tasklet(unsigned long data)
{
	struct virtnet_info *vi = (void *)data;

	netif_tx_lock_bh(vi->dev);
	if (vi->last_xmit_skb && xmit_skb(vi, vi->last_xmit_skb) == 0) {
		vi->svq->vq_ops->kick(vi->svq);
		vi->last_xmit_skb = NULL;
	}
	if (vi->free_in_tasklet)
		free_old_xmit_skbs(vi);
	netif_tx_unlock_bh(vi->dev);
}

static int start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);

again:
	/* Free up any pending old buffers before queueing new ones. */
	free_old_xmit_skbs(vi);

	/* If we has a buffer left over from last time, send it now. */
	if (unlikely(vi->last_xmit_skb) &&
	    xmit_skb(vi, vi->last_xmit_skb) != 0)
		goto stop_queue;

	vi->last_xmit_skb = NULL;

	/* Put new one in send queue and do transmit */
	if (likely(skb)) {
		__skb_queue_head(&vi->send, skb);
		if (xmit_skb(vi, skb) != 0) {
			vi->last_xmit_skb = skb;
			skb = NULL;
			goto stop_queue;
		}
	}
done:
	vi->svq->vq_ops->kick(vi->svq);
	return NETDEV_TX_OK;

stop_queue:
	pr_debug("%s: virtio not prepared to send\n", dev->name);
	netif_stop_queue(dev);

	/* Activate callback for using skbs: if this returns false it
	 * means some were used in the meantime. */
	if (unlikely(!vi->svq->vq_ops->enable_cb(vi->svq))) {
		vi->svq->vq_ops->disable_cb(vi->svq);
		netif_start_queue(dev);
		goto again;
	}
	if (skb) {
		/* Drop this skb: we only queue one. */
		vi->dev->stats.tx_dropped++;
		kfree_skb(skb);
	}
	goto done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void virtnet_netpoll(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);

	napi_schedule(&vi->napi);
}
#endif

static int virtnet_open(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);

	napi_enable(&vi->napi);

	/* If all buffers were filled by other side before we napi_enabled, we
	 * won't get another interrupt, so process any outstanding packets
	 * now.  virtnet_poll wants re-enable the queue, so we disable here.
	 * We synchronize against interrupts via NAPI_STATE_SCHED */
	if (netif_rx_schedule_prep(&vi->napi)) {
		vi->rvq->vq_ops->disable_cb(vi->rvq);
		__netif_rx_schedule(&vi->napi);
	}
	return 0;
}

static int virtnet_close(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);

	napi_disable(&vi->napi);

	return 0;
}

static int virtnet_set_tx_csum(struct net_device *dev, u32 data)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct virtio_device *vdev = vi->vdev;

	if (data && !virtio_has_feature(vdev, VIRTIO_NET_F_CSUM))
		return -ENOSYS;

	return ethtool_op_set_tx_hw_csum(dev, data);
}

static struct ethtool_ops virtnet_ethtool_ops = {
	.set_tx_csum = virtnet_set_tx_csum,
	.set_sg = ethtool_op_set_sg,
	.set_tso = ethtool_op_set_tso,
};

#define MIN_MTU 68
#define MAX_MTU 65535

static int virtnet_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < MIN_MTU || new_mtu > MAX_MTU)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops virtnet_netdev = {
	.ndo_open            = virtnet_open,
	.ndo_stop   	     = virtnet_close,
	.ndo_start_xmit      = start_xmit,
	.ndo_validate_addr   = eth_validate_addr,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_change_mtu	     = virtnet_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = virtnet_netpoll,
#endif
};

static int virtnet_probe(struct virtio_device *vdev)
{
	int err;
	struct net_device *dev;
	struct virtnet_info *vi;

	/* Allocate ourselves a network device with room for our info */
	dev = alloc_etherdev(sizeof(struct virtnet_info));
	if (!dev)
		return -ENOMEM;

	/* Set up network device as normal. */
	dev->netdev_ops = &virtnet_netdev;
	dev->features = NETIF_F_HIGHDMA;
	SET_ETHTOOL_OPS(dev, &virtnet_ethtool_ops);
	SET_NETDEV_DEV(dev, &vdev->dev);

	/* Do we support "hardware" checksums? */
	if (csum && virtio_has_feature(vdev, VIRTIO_NET_F_CSUM)) {
		/* This opens up the world of extra features. */
		dev->features |= NETIF_F_HW_CSUM|NETIF_F_SG|NETIF_F_FRAGLIST;
		if (gso && virtio_has_feature(vdev, VIRTIO_NET_F_GSO)) {
			dev->features |= NETIF_F_TSO | NETIF_F_UFO
				| NETIF_F_TSO_ECN | NETIF_F_TSO6;
		}
		/* Individual feature bits: what can host handle? */
		if (gso && virtio_has_feature(vdev, VIRTIO_NET_F_HOST_TSO4))
			dev->features |= NETIF_F_TSO;
		if (gso && virtio_has_feature(vdev, VIRTIO_NET_F_HOST_TSO6))
			dev->features |= NETIF_F_TSO6;
		if (gso && virtio_has_feature(vdev, VIRTIO_NET_F_HOST_ECN))
			dev->features |= NETIF_F_TSO_ECN;
		if (gso && virtio_has_feature(vdev, VIRTIO_NET_F_HOST_UFO))
			dev->features |= NETIF_F_UFO;
	}

	/* Configuration may specify what MAC to use.  Otherwise random. */
	if (virtio_has_feature(vdev, VIRTIO_NET_F_MAC)) {
		vdev->config->get(vdev,
				  offsetof(struct virtio_net_config, mac),
				  dev->dev_addr, dev->addr_len);
	} else
		random_ether_addr(dev->dev_addr);

	/* Set up our device-specific information */
	vi = netdev_priv(dev);
	netif_napi_add(dev, &vi->napi, virtnet_poll, napi_weight);
	vi->dev = dev;
	vi->vdev = vdev;
	vdev->priv = vi;
	vi->pages = NULL;

	/* If they give us a callback when all buffers are done, we don't need
	 * the timer. */
	vi->free_in_tasklet = virtio_has_feature(vdev,VIRTIO_F_NOTIFY_ON_EMPTY);

	/* If we can receive ANY GSO packets, we must allocate large ones. */
	if (virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_TSO4)
	    || virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_TSO6)
	    || virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_ECN))
		vi->big_packets = true;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_MRG_RXBUF))
		vi->mergeable_rx_bufs = true;

	/* We expect two virtqueues, receive then send. */
	vi->rvq = vdev->config->find_vq(vdev, 0, skb_recv_done);
	if (IS_ERR(vi->rvq)) {
		err = PTR_ERR(vi->rvq);
		goto free;
	}

	vi->svq = vdev->config->find_vq(vdev, 1, skb_xmit_done);
	if (IS_ERR(vi->svq)) {
		err = PTR_ERR(vi->svq);
		goto free_recv;
	}

	/* Initialize our empty receive and send queues. */
	skb_queue_head_init(&vi->recv);
	skb_queue_head_init(&vi->send);

	tasklet_init(&vi->tasklet, xmit_tasklet, (unsigned long)vi);

	if (!vi->free_in_tasklet)
		setup_timer(&vi->xmit_free_timer, xmit_free, (unsigned long)vi);

	err = register_netdev(dev);
	if (err) {
		pr_debug("virtio_net: registering device failed\n");
		goto free_send;
	}

	/* Last of all, set up some receive buffers. */
	try_fill_recv(vi);

	/* If we didn't even get one input buffer, we're useless. */
	if (vi->num == 0) {
		err = -ENOMEM;
		goto unregister;
	}

	pr_debug("virtnet: registered device %s\n", dev->name);
	return 0;

unregister:
	unregister_netdev(dev);
free_send:
	vdev->config->del_vq(vi->svq);
free_recv:
	vdev->config->del_vq(vi->rvq);
free:
	free_netdev(dev);
	return err;
}

static void virtnet_remove(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;
	struct sk_buff *skb;

	/* Stop all the virtqueues. */
	vdev->config->reset(vdev);

	if (!vi->free_in_tasklet)
		del_timer_sync(&vi->xmit_free_timer);

	/* Free our skbs in send and recv queues, if any. */
	while ((skb = __skb_dequeue(&vi->recv)) != NULL) {
		kfree_skb(skb);
		vi->num--;
	}
	__skb_queue_purge(&vi->send);

	BUG_ON(vi->num != 0);

	vdev->config->del_vq(vi->svq);
	vdev->config->del_vq(vi->rvq);
	unregister_netdev(vi->dev);

	while (vi->pages)
		__free_pages(get_a_page(vi, GFP_KERNEL), 0);

	free_netdev(vi->dev);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_NET_F_CSUM, VIRTIO_NET_F_GUEST_CSUM,
	VIRTIO_NET_F_GSO, VIRTIO_NET_F_MAC,
	VIRTIO_NET_F_HOST_TSO4, VIRTIO_NET_F_HOST_UFO, VIRTIO_NET_F_HOST_TSO6,
	VIRTIO_NET_F_HOST_ECN, VIRTIO_NET_F_GUEST_TSO4, VIRTIO_NET_F_GUEST_TSO6,
	VIRTIO_NET_F_GUEST_ECN, /* We don't yet handle UFO input. */
	VIRTIO_NET_F_MRG_RXBUF,
	VIRTIO_F_NOTIFY_ON_EMPTY,
};

static struct virtio_driver virtio_net = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtnet_probe,
	.remove =	__devexit_p(virtnet_remove),
};

static int __init init(void)
{
	return register_virtio_driver(&virtio_net);
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_net);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio network driver");
MODULE_LICENSE("GPL");
