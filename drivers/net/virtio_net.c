/* A network driver using virtio.
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
#include <linux/virtio_ids.h>
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

#define VIRTNET_SEND_COMMAND_SG_MAX    2

struct virtnet_info
{
	struct virtio_device *vdev;
	struct virtqueue *rvq, *svq, *cvq;
	struct net_device *dev;
	struct napi_struct napi;
	unsigned int status;

	/* Number of input buffers, and max we've ever had. */
	unsigned int num, max;

	/* I like... big packets and I cannot lie! */
	bool big_packets;

	/* Host will merge rx buffers for big packets (shake it! shake it!) */
	bool mergeable_rx_bufs;

	/* Receive & send queues. */
	struct sk_buff_head recv;
	struct sk_buff_head send;

	/* Work struct for refilling if we run low on memory. */
	struct delayed_work refill;

	/* Chain pages by the private ptr. */
	struct page *pages;
};

struct skb_vnet_hdr {
	union {
		struct virtio_net_hdr hdr;
		struct virtio_net_hdr_mrg_rxbuf mhdr;
	};
	unsigned int num_sg;
};

static inline struct skb_vnet_hdr *skb_vnet_hdr(struct sk_buff *skb)
{
	return (struct skb_vnet_hdr *)skb->cb;
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
}

static void receive_skb(struct net_device *dev, struct sk_buff *skb,
			unsigned len)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct skb_vnet_hdr *hdr = skb_vnet_hdr(skb);
	int err;
	int i;

	if (unlikely(len < sizeof(struct virtio_net_hdr) + ETH_HLEN)) {
		pr_debug("%s: short packet %i\n", dev->name, len);
		dev->stats.rx_length_errors++;
		goto drop;
	}

	if (vi->mergeable_rx_bufs) {
		unsigned int copy;
		char *p = page_address(skb_shinfo(skb)->frags[0].page);

		if (len > PAGE_SIZE)
			len = PAGE_SIZE;
		len -= sizeof(struct virtio_net_hdr_mrg_rxbuf);

		memcpy(&hdr->mhdr, p, sizeof(hdr->mhdr));
		p += sizeof(hdr->mhdr);

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
				sizeof(hdr->mhdr) + copy;
			skb_shinfo(skb)->frags[0].size = len;
			skb->data_len += len;
			skb->len += len;
		}

		while (--hdr->mhdr.num_buffers) {
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
					 dev->name, hdr->mhdr.num_buffers);
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
		len -= sizeof(hdr->hdr);

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

	if (hdr->hdr.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
		pr_debug("Needs csum!\n");
		if (!skb_partial_csum_set(skb,
					  hdr->hdr.csum_start,
					  hdr->hdr.csum_offset))
			goto frame_err;
	}

	skb->protocol = eth_type_trans(skb, dev);
	pr_debug("Receiving skb proto 0x%04x len %i type %i\n",
		 ntohs(skb->protocol), skb->len, skb->pkt_type);

	if (hdr->hdr.gso_type != VIRTIO_NET_HDR_GSO_NONE) {
		pr_debug("GSO!\n");
		switch (hdr->hdr.gso_type & ~VIRTIO_NET_HDR_GSO_ECN) {
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
				       dev->name, hdr->hdr.gso_type);
			goto frame_err;
		}

		if (hdr->hdr.gso_type & VIRTIO_NET_HDR_GSO_ECN)
			skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;

		skb_shinfo(skb)->gso_size = hdr->hdr.gso_size;
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

static bool try_fill_recv_maxbufs(struct virtnet_info *vi, gfp_t gfp)
{
	struct sk_buff *skb;
	struct scatterlist sg[2+MAX_SKB_FRAGS];
	int num, err, i;
	bool oom = false;

	sg_init_table(sg, 2+MAX_SKB_FRAGS);
	do {
		struct skb_vnet_hdr *hdr;

		skb = netdev_alloc_skb_ip_align(vi->dev, MAX_PACKET_LEN);
		if (unlikely(!skb)) {
			oom = true;
			break;
		}

		skb_put(skb, MAX_PACKET_LEN);

		hdr = skb_vnet_hdr(skb);
		sg_set_buf(sg, &hdr->hdr, sizeof(hdr->hdr));

		if (vi->big_packets) {
			for (i = 0; i < MAX_SKB_FRAGS; i++) {
				skb_frag_t *f = &skb_shinfo(skb)->frags[i];
				f->page = get_a_page(vi, gfp);
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
		if (err < 0) {
			skb_unlink(skb, &vi->recv);
			trim_pages(vi, skb);
			kfree_skb(skb);
			break;
		}
		vi->num++;
	} while (err >= num);
	if (unlikely(vi->num > vi->max))
		vi->max = vi->num;
	vi->rvq->vq_ops->kick(vi->rvq);
	return !oom;
}

/* Returns false if we couldn't fill entirely (OOM). */
static bool try_fill_recv(struct virtnet_info *vi, gfp_t gfp)
{
	struct sk_buff *skb;
	struct scatterlist sg[1];
	int err;
	bool oom = false;

	if (!vi->mergeable_rx_bufs)
		return try_fill_recv_maxbufs(vi, gfp);

	do {
		skb_frag_t *f;

		skb = netdev_alloc_skb_ip_align(vi->dev, GOOD_COPY_LEN);
		if (unlikely(!skb)) {
			oom = true;
			break;
		}

		f = &skb_shinfo(skb)->frags[0];
		f->page = get_a_page(vi, gfp);
		if (!f->page) {
			oom = true;
			kfree_skb(skb);
			break;
		}

		f->page_offset = 0;
		f->size = PAGE_SIZE;

		skb_shinfo(skb)->nr_frags++;

		sg_init_one(sg, page_address(f->page), PAGE_SIZE);
		skb_queue_head(&vi->recv, skb);

		err = vi->rvq->vq_ops->add_buf(vi->rvq, sg, 0, 1, skb);
		if (err < 0) {
			skb_unlink(skb, &vi->recv);
			kfree_skb(skb);
			break;
		}
		vi->num++;
	} while (err > 0);
	if (unlikely(vi->num > vi->max))
		vi->max = vi->num;
	vi->rvq->vq_ops->kick(vi->rvq);
	return !oom;
}

static void skb_recv_done(struct virtqueue *rvq)
{
	struct virtnet_info *vi = rvq->vdev->priv;
	/* Schedule NAPI, Suppress further interrupts if successful. */
	if (napi_schedule_prep(&vi->napi)) {
		rvq->vq_ops->disable_cb(rvq);
		__napi_schedule(&vi->napi);
	}
}

static void refill_work(struct work_struct *work)
{
	struct virtnet_info *vi;
	bool still_empty;

	vi = container_of(work, struct virtnet_info, refill.work);
	napi_disable(&vi->napi);
	try_fill_recv(vi, GFP_KERNEL);
	still_empty = (vi->num == 0);
	napi_enable(&vi->napi);

	/* In theory, this can happen: if we don't get any buffers in
	 * we will *never* try to fill again. */
	if (still_empty)
		schedule_delayed_work(&vi->refill, HZ/2);
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

	if (vi->num < vi->max / 2) {
		if (!try_fill_recv(vi, GFP_ATOMIC))
			schedule_delayed_work(&vi->refill, 0);
	}

	/* Out of packets? */
	if (received < budget) {
		napi_complete(napi);
		if (unlikely(!vi->rvq->vq_ops->enable_cb(vi->rvq))
		    && napi_schedule_prep(napi)) {
			vi->rvq->vq_ops->disable_cb(vi->rvq);
			__napi_schedule(napi);
			goto again;
		}
	}

	return received;
}

static unsigned int free_old_xmit_skbs(struct virtnet_info *vi)
{
	struct sk_buff *skb;
	unsigned int len, tot_sgs = 0;

	while ((skb = vi->svq->vq_ops->get_buf(vi->svq, &len)) != NULL) {
		pr_debug("Sent skb %p\n", skb);
		__skb_unlink(skb, &vi->send);
		vi->dev->stats.tx_bytes += skb->len;
		vi->dev->stats.tx_packets++;
		tot_sgs += skb_vnet_hdr(skb)->num_sg;
		dev_kfree_skb_any(skb);
	}
	return tot_sgs;
}

static int xmit_skb(struct virtnet_info *vi, struct sk_buff *skb)
{
	struct scatterlist sg[2+MAX_SKB_FRAGS];
	struct skb_vnet_hdr *hdr = skb_vnet_hdr(skb);
	const unsigned char *dest = ((struct ethhdr *)skb->data)->h_dest;

	sg_init_table(sg, 2+MAX_SKB_FRAGS);

	pr_debug("%s: xmit %p %pM\n", vi->dev->name, skb, dest);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		hdr->hdr.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
		hdr->hdr.csum_start = skb->csum_start - skb_headroom(skb);
		hdr->hdr.csum_offset = skb->csum_offset;
	} else {
		hdr->hdr.flags = 0;
		hdr->hdr.csum_offset = hdr->hdr.csum_start = 0;
	}

	if (skb_is_gso(skb)) {
		hdr->hdr.hdr_len = skb_headlen(skb);
		hdr->hdr.gso_size = skb_shinfo(skb)->gso_size;
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_UDP;
		else
			BUG();
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCP_ECN)
			hdr->hdr.gso_type |= VIRTIO_NET_HDR_GSO_ECN;
	} else {
		hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_NONE;
		hdr->hdr.gso_size = hdr->hdr.hdr_len = 0;
	}

	hdr->mhdr.num_buffers = 0;

	/* Encode metadata header at front. */
	if (vi->mergeable_rx_bufs)
		sg_set_buf(sg, &hdr->mhdr, sizeof(hdr->mhdr));
	else
		sg_set_buf(sg, &hdr->hdr, sizeof(hdr->hdr));

	hdr->num_sg = skb_to_sgvec(skb, sg+1, 0, skb->len) + 1;
	return vi->svq->vq_ops->add_buf(vi->svq, sg, hdr->num_sg, 0, skb);
}

static netdev_tx_t start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int capacity;

again:
	/* Free up any pending old buffers before queueing new ones. */
	free_old_xmit_skbs(vi);

	/* Try to transmit */
	capacity = xmit_skb(vi, skb);

	/* This can happen with OOM and indirect buffers. */
	if (unlikely(capacity < 0)) {
		netif_stop_queue(dev);
		dev_warn(&dev->dev, "Unexpected full queue\n");
		if (unlikely(!vi->svq->vq_ops->enable_cb(vi->svq))) {
			vi->svq->vq_ops->disable_cb(vi->svq);
			netif_start_queue(dev);
			goto again;
		}
		return NETDEV_TX_BUSY;
	}
	vi->svq->vq_ops->kick(vi->svq);

	/*
	 * Put new one in send queue.  You'd expect we'd need this before
	 * xmit_skb calls add_buf(), since the callback can be triggered
	 * immediately after that.  But since the callback just triggers
	 * another call back here, normal network xmit locking prevents the
	 * race.
	 */
	__skb_queue_head(&vi->send, skb);

	/* Don't wait up for transmitted skbs to be freed. */
	skb_orphan(skb);
	nf_reset(skb);

	/* Apparently nice girls don't return TX_BUSY; stop the queue
	 * before it gets out of hand.  Naturally, this wastes entries. */
	if (capacity < 2+MAX_SKB_FRAGS) {
		netif_stop_queue(dev);
		if (unlikely(!vi->svq->vq_ops->enable_cb(vi->svq))) {
			/* More just got used, free them then recheck. */
			capacity += free_old_xmit_skbs(vi);
			if (capacity >= 2+MAX_SKB_FRAGS) {
				netif_start_queue(dev);
				vi->svq->vq_ops->disable_cb(vi->svq);
			}
		}
	}

	return NETDEV_TX_OK;
}

static int virtnet_set_mac_address(struct net_device *dev, void *p)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct virtio_device *vdev = vi->vdev;
	int ret;

	ret = eth_mac_addr(dev, p);
	if (ret)
		return ret;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_MAC))
		vdev->config->set(vdev, offsetof(struct virtio_net_config, mac),
		                  dev->dev_addr, dev->addr_len);

	return 0;
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
	if (napi_schedule_prep(&vi->napi)) {
		vi->rvq->vq_ops->disable_cb(vi->rvq);
		__napi_schedule(&vi->napi);
	}
	return 0;
}

/*
 * Send command via the control virtqueue and check status.  Commands
 * supported by the hypervisor, as indicated by feature bits, should
 * never fail unless improperly formated.
 */
static bool virtnet_send_command(struct virtnet_info *vi, u8 class, u8 cmd,
				 struct scatterlist *data, int out, int in)
{
	struct scatterlist *s, sg[VIRTNET_SEND_COMMAND_SG_MAX + 2];
	struct virtio_net_ctrl_hdr ctrl;
	virtio_net_ctrl_ack status = ~0;
	unsigned int tmp;
	int i;

	/* Caller should know better */
	BUG_ON(!virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VQ) ||
		(out + in > VIRTNET_SEND_COMMAND_SG_MAX));

	out++; /* Add header */
	in++; /* Add return status */

	ctrl.class = class;
	ctrl.cmd = cmd;

	sg_init_table(sg, out + in);

	sg_set_buf(&sg[0], &ctrl, sizeof(ctrl));
	for_each_sg(data, s, out + in - 2, i)
		sg_set_buf(&sg[i + 1], sg_virt(s), s->length);
	sg_set_buf(&sg[out + in - 1], &status, sizeof(status));

	BUG_ON(vi->cvq->vq_ops->add_buf(vi->cvq, sg, out, in, vi) < 0);

	vi->cvq->vq_ops->kick(vi->cvq);

	/*
	 * Spin for a response, the kick causes an ioport write, trapping
	 * into the hypervisor, so the request should be handled immediately.
	 */
	while (!vi->cvq->vq_ops->get_buf(vi->cvq, &tmp))
		cpu_relax();

	return status == VIRTIO_NET_OK;
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

static void virtnet_set_rx_mode(struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct scatterlist sg[2];
	u8 promisc, allmulti;
	struct virtio_net_ctrl_mac *mac_data;
	struct dev_addr_list *addr;
	struct netdev_hw_addr *ha;
	void *buf;
	int i;

	/* We can't dynamicaly set ndo_set_rx_mode, so return gracefully */
	if (!virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_RX))
		return;

	promisc = ((dev->flags & IFF_PROMISC) != 0);
	allmulti = ((dev->flags & IFF_ALLMULTI) != 0);

	sg_init_one(sg, &promisc, sizeof(promisc));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_RX,
				  VIRTIO_NET_CTRL_RX_PROMISC,
				  sg, 1, 0))
		dev_warn(&dev->dev, "Failed to %sable promisc mode.\n",
			 promisc ? "en" : "dis");

	sg_init_one(sg, &allmulti, sizeof(allmulti));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_RX,
				  VIRTIO_NET_CTRL_RX_ALLMULTI,
				  sg, 1, 0))
		dev_warn(&dev->dev, "Failed to %sable allmulti mode.\n",
			 allmulti ? "en" : "dis");

	/* MAC filter - use one buffer for both lists */
	mac_data = buf = kzalloc(((dev->uc.count + dev->mc_count) * ETH_ALEN) +
				 (2 * sizeof(mac_data->entries)), GFP_ATOMIC);
	if (!buf) {
		dev_warn(&dev->dev, "No memory for MAC address buffer\n");
		return;
	}

	sg_init_table(sg, 2);

	/* Store the unicast list and count in the front of the buffer */
	mac_data->entries = dev->uc.count;
	i = 0;
	list_for_each_entry(ha, &dev->uc.list, list)
		memcpy(&mac_data->macs[i++][0], ha->addr, ETH_ALEN);

	sg_set_buf(&sg[0], mac_data,
		   sizeof(mac_data->entries) + (dev->uc.count * ETH_ALEN));

	/* multicast list and count fill the end */
	mac_data = (void *)&mac_data->macs[dev->uc.count][0];

	mac_data->entries = dev->mc_count;
	addr = dev->mc_list;
	for (i = 0; i < dev->mc_count; i++, addr = addr->next)
		memcpy(&mac_data->macs[i][0], addr->da_addr, ETH_ALEN);

	sg_set_buf(&sg[1], mac_data,
		   sizeof(mac_data->entries) + (dev->mc_count * ETH_ALEN));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_MAC,
				  VIRTIO_NET_CTRL_MAC_TABLE_SET,
				  sg, 2, 0))
		dev_warn(&dev->dev, "Failed to set MAC fitler table.\n");

	kfree(buf);
}

static void virtnet_vlan_rx_add_vid(struct net_device *dev, u16 vid)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct scatterlist sg;

	sg_init_one(&sg, &vid, sizeof(vid));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_VLAN,
				  VIRTIO_NET_CTRL_VLAN_ADD, &sg, 1, 0))
		dev_warn(&dev->dev, "Failed to add VLAN ID %d.\n", vid);
}

static void virtnet_vlan_rx_kill_vid(struct net_device *dev, u16 vid)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct scatterlist sg;

	sg_init_one(&sg, &vid, sizeof(vid));

	if (!virtnet_send_command(vi, VIRTIO_NET_CTRL_VLAN,
				  VIRTIO_NET_CTRL_VLAN_DEL, &sg, 1, 0))
		dev_warn(&dev->dev, "Failed to kill VLAN ID %d.\n", vid);
}

static const struct ethtool_ops virtnet_ethtool_ops = {
	.set_tx_csum = virtnet_set_tx_csum,
	.set_sg = ethtool_op_set_sg,
	.set_tso = ethtool_op_set_tso,
	.set_ufo = ethtool_op_set_ufo,
	.get_link = ethtool_op_get_link,
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
	.ndo_set_mac_address = virtnet_set_mac_address,
	.ndo_set_rx_mode     = virtnet_set_rx_mode,
	.ndo_change_mtu	     = virtnet_change_mtu,
	.ndo_vlan_rx_add_vid = virtnet_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = virtnet_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = virtnet_netpoll,
#endif
};

static void virtnet_update_status(struct virtnet_info *vi)
{
	u16 v;

	if (!virtio_has_feature(vi->vdev, VIRTIO_NET_F_STATUS))
		return;

	vi->vdev->config->get(vi->vdev,
			      offsetof(struct virtio_net_config, status),
			      &v, sizeof(v));

	/* Ignore unknown (future) status bits */
	v &= VIRTIO_NET_S_LINK_UP;

	if (vi->status == v)
		return;

	vi->status = v;

	if (vi->status & VIRTIO_NET_S_LINK_UP) {
		netif_carrier_on(vi->dev);
		netif_wake_queue(vi->dev);
	} else {
		netif_carrier_off(vi->dev);
		netif_stop_queue(vi->dev);
	}
}

static void virtnet_config_changed(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;

	virtnet_update_status(vi);
}

static int virtnet_probe(struct virtio_device *vdev)
{
	int err;
	struct net_device *dev;
	struct virtnet_info *vi;
	struct virtqueue *vqs[3];
	vq_callback_t *callbacks[] = { skb_recv_done, skb_xmit_done, NULL};
	const char *names[] = { "input", "output", "control" };
	int nvqs;

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
	INIT_DELAYED_WORK(&vi->refill, refill_work);

	/* If we can receive ANY GSO packets, we must allocate large ones. */
	if (virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_TSO4)
	    || virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_TSO6)
	    || virtio_has_feature(vdev, VIRTIO_NET_F_GUEST_ECN))
		vi->big_packets = true;

	if (virtio_has_feature(vdev, VIRTIO_NET_F_MRG_RXBUF))
		vi->mergeable_rx_bufs = true;

	/* We expect two virtqueues, receive then send,
	 * and optionally control. */
	nvqs = virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VQ) ? 3 : 2;

	err = vdev->config->find_vqs(vdev, nvqs, vqs, callbacks, names);
	if (err)
		goto free;

	vi->rvq = vqs[0];
	vi->svq = vqs[1];

	if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VQ)) {
		vi->cvq = vqs[2];

		if (virtio_has_feature(vi->vdev, VIRTIO_NET_F_CTRL_VLAN))
			dev->features |= NETIF_F_HW_VLAN_FILTER;
	}

	/* Initialize our empty receive and send queues. */
	skb_queue_head_init(&vi->recv);
	skb_queue_head_init(&vi->send);

	err = register_netdev(dev);
	if (err) {
		pr_debug("virtio_net: registering device failed\n");
		goto free_vqs;
	}

	/* Last of all, set up some receive buffers. */
	try_fill_recv(vi, GFP_KERNEL);

	/* If we didn't even get one input buffer, we're useless. */
	if (vi->num == 0) {
		err = -ENOMEM;
		goto unregister;
	}

	vi->status = VIRTIO_NET_S_LINK_UP;
	virtnet_update_status(vi);
	netif_carrier_on(dev);

	pr_debug("virtnet: registered device %s\n", dev->name);
	return 0;

unregister:
	unregister_netdev(dev);
	cancel_delayed_work_sync(&vi->refill);
free_vqs:
	vdev->config->del_vqs(vdev);
free:
	free_netdev(dev);
	return err;
}

static void __devexit virtnet_remove(struct virtio_device *vdev)
{
	struct virtnet_info *vi = vdev->priv;
	struct sk_buff *skb;

	/* Stop all the virtqueues. */
	vdev->config->reset(vdev);

	/* Free our skbs in send and recv queues, if any. */
	while ((skb = __skb_dequeue(&vi->recv)) != NULL) {
		kfree_skb(skb);
		vi->num--;
	}
	__skb_queue_purge(&vi->send);

	BUG_ON(vi->num != 0);

	unregister_netdev(vi->dev);
	cancel_delayed_work_sync(&vi->refill);

	vdev->config->del_vqs(vi->vdev);

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
	VIRTIO_NET_F_GUEST_ECN, VIRTIO_NET_F_GUEST_UFO,
	VIRTIO_NET_F_MRG_RXBUF, VIRTIO_NET_F_STATUS, VIRTIO_NET_F_CTRL_VQ,
	VIRTIO_NET_F_CTRL_RX, VIRTIO_NET_F_CTRL_VLAN,
};

static struct virtio_driver virtio_net = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtnet_probe,
	.remove =	__devexit_p(virtnet_remove),
	.config_changed = virtnet_config_changed,
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
