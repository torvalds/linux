/* sunvnet.c: Sun LDOM Virtual Network Driver.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/mutex.h>

#include <asm/vio.h>
#include <asm/ldc.h>

#include "sunvnet.h"

#define DRV_MODULE_NAME		"sunvnet"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"1.0"
#define DRV_MODULE_RELDATE	"June 25, 2007"

static char version[] __devinitdata =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";
MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("Sun LDOM virtual network driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

/* Ordered from largest major to lowest */
static struct vio_version vnet_versions[] = {
	{ .major = 1, .minor = 0 },
};

static inline u32 vnet_tx_dring_avail(struct vio_dring_state *dr)
{
	return vio_dring_avail(dr, VNET_TX_RING_SIZE);
}

static int vnet_handle_unknown(struct vnet_port *port, void *arg)
{
	struct vio_msg_tag *pkt = arg;

	printk(KERN_ERR PFX "Received unknown msg [%02x:%02x:%04x:%08x]\n",
	       pkt->type, pkt->stype, pkt->stype_env, pkt->sid);
	printk(KERN_ERR PFX "Resetting connection.\n");

	ldc_disconnect(port->vio.lp);

	return -ECONNRESET;
}

static int vnet_send_attr(struct vio_driver_state *vio)
{
	struct vnet_port *port = to_vnet_port(vio);
	struct net_device *dev = port->vp->dev;
	struct vio_net_attr_info pkt;
	int i;

	memset(&pkt, 0, sizeof(pkt));
	pkt.tag.type = VIO_TYPE_CTRL;
	pkt.tag.stype = VIO_SUBTYPE_INFO;
	pkt.tag.stype_env = VIO_ATTR_INFO;
	pkt.tag.sid = vio_send_sid(vio);
	pkt.xfer_mode = VIO_DRING_MODE;
	pkt.addr_type = VNET_ADDR_ETHERMAC;
	pkt.ack_freq = 0;
	for (i = 0; i < 6; i++)
		pkt.addr |= (u64)dev->dev_addr[i] << ((5 - i) * 8);
	pkt.mtu = ETH_FRAME_LEN;

	viodbg(HS, "SEND NET ATTR xmode[0x%x] atype[0x%x] addr[%llx] "
	       "ackfreq[%u] mtu[%llu]\n",
	       pkt.xfer_mode, pkt.addr_type,
	       (unsigned long long) pkt.addr,
	       pkt.ack_freq,
	       (unsigned long long) pkt.mtu);

	return vio_ldc_send(vio, &pkt, sizeof(pkt));
}

static int handle_attr_info(struct vio_driver_state *vio,
			    struct vio_net_attr_info *pkt)
{
	viodbg(HS, "GOT NET ATTR INFO xmode[0x%x] atype[0x%x] addr[%llx] "
	       "ackfreq[%u] mtu[%llu]\n",
	       pkt->xfer_mode, pkt->addr_type,
	       (unsigned long long) pkt->addr,
	       pkt->ack_freq,
	       (unsigned long long) pkt->mtu);

	pkt->tag.sid = vio_send_sid(vio);

	if (pkt->xfer_mode != VIO_DRING_MODE ||
	    pkt->addr_type != VNET_ADDR_ETHERMAC ||
	    pkt->mtu != ETH_FRAME_LEN) {
		viodbg(HS, "SEND NET ATTR NACK\n");

		pkt->tag.stype = VIO_SUBTYPE_NACK;

		(void) vio_ldc_send(vio, pkt, sizeof(*pkt));

		return -ECONNRESET;
	} else {
		viodbg(HS, "SEND NET ATTR ACK\n");

		pkt->tag.stype = VIO_SUBTYPE_ACK;

		return vio_ldc_send(vio, pkt, sizeof(*pkt));
	}

}

static int handle_attr_ack(struct vio_driver_state *vio,
			   struct vio_net_attr_info *pkt)
{
	viodbg(HS, "GOT NET ATTR ACK\n");

	return 0;
}

static int handle_attr_nack(struct vio_driver_state *vio,
			    struct vio_net_attr_info *pkt)
{
	viodbg(HS, "GOT NET ATTR NACK\n");

	return -ECONNRESET;
}

static int vnet_handle_attr(struct vio_driver_state *vio, void *arg)
{
	struct vio_net_attr_info *pkt = arg;

	switch (pkt->tag.stype) {
	case VIO_SUBTYPE_INFO:
		return handle_attr_info(vio, pkt);

	case VIO_SUBTYPE_ACK:
		return handle_attr_ack(vio, pkt);

	case VIO_SUBTYPE_NACK:
		return handle_attr_nack(vio, pkt);

	default:
		return -ECONNRESET;
	}
}

static void vnet_handshake_complete(struct vio_driver_state *vio)
{
	struct vio_dring_state *dr;

	dr = &vio->drings[VIO_DRIVER_RX_RING];
	dr->snd_nxt = dr->rcv_nxt = 1;

	dr = &vio->drings[VIO_DRIVER_TX_RING];
	dr->snd_nxt = dr->rcv_nxt = 1;
}

/* The hypervisor interface that implements copying to/from imported
 * memory from another domain requires that copies are done to 8-byte
 * aligned buffers, and that the lengths of such copies are also 8-byte
 * multiples.
 *
 * So we align skb->data to an 8-byte multiple and pad-out the data
 * area so we can round the copy length up to the next multiple of
 * 8 for the copy.
 *
 * The transmitter puts the actual start of the packet 6 bytes into
 * the buffer it sends over, so that the IP headers after the ethernet
 * header are aligned properly.  These 6 bytes are not in the descriptor
 * length, they are simply implied.  This offset is represented using
 * the VNET_PACKET_SKIP macro.
 */
static struct sk_buff *alloc_and_align_skb(struct net_device *dev,
					   unsigned int len)
{
	struct sk_buff *skb = netdev_alloc_skb(dev, len+VNET_PACKET_SKIP+8+8);
	unsigned long addr, off;

	if (unlikely(!skb))
		return NULL;

	addr = (unsigned long) skb->data;
	off = ((addr + 7UL) & ~7UL) - addr;
	if (off)
		skb_reserve(skb, off);

	return skb;
}

static int vnet_rx_one(struct vnet_port *port, unsigned int len,
		       struct ldc_trans_cookie *cookies, int ncookies)
{
	struct net_device *dev = port->vp->dev;
	unsigned int copy_len;
	struct sk_buff *skb;
	int err;

	err = -EMSGSIZE;
	if (unlikely(len < ETH_ZLEN || len > ETH_FRAME_LEN)) {
		dev->stats.rx_length_errors++;
		goto out_dropped;
	}

	skb = alloc_and_align_skb(dev, len);
	err = -ENOMEM;
	if (unlikely(!skb)) {
		dev->stats.rx_missed_errors++;
		goto out_dropped;
	}

	copy_len = (len + VNET_PACKET_SKIP + 7U) & ~7U;
	skb_put(skb, copy_len);
	err = ldc_copy(port->vio.lp, LDC_COPY_IN,
		       skb->data, copy_len, 0,
		       cookies, ncookies);
	if (unlikely(err < 0)) {
		dev->stats.rx_frame_errors++;
		goto out_free_skb;
	}

	skb_pull(skb, VNET_PACKET_SKIP);
	skb_trim(skb, len);
	skb->protocol = eth_type_trans(skb, dev);

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;

	netif_rx(skb);

	return 0;

out_free_skb:
	kfree_skb(skb);

out_dropped:
	dev->stats.rx_dropped++;
	return err;
}

static int vnet_send_ack(struct vnet_port *port, struct vio_dring_state *dr,
			 u32 start, u32 end, u8 vio_dring_state)
{
	struct vio_dring_data hdr = {
		.tag = {
			.type		= VIO_TYPE_DATA,
			.stype		= VIO_SUBTYPE_ACK,
			.stype_env	= VIO_DRING_DATA,
			.sid		= vio_send_sid(&port->vio),
		},
		.dring_ident		= dr->ident,
		.start_idx		= start,
		.end_idx		= end,
		.state			= vio_dring_state,
	};
	int err, delay;

	hdr.seq = dr->snd_nxt;
	delay = 1;
	do {
		err = vio_ldc_send(&port->vio, &hdr, sizeof(hdr));
		if (err > 0) {
			dr->snd_nxt++;
			break;
		}
		udelay(delay);
		if ((delay <<= 1) > 128)
			delay = 128;
	} while (err == -EAGAIN);

	return err;
}

static u32 next_idx(u32 idx, struct vio_dring_state *dr)
{
	if (++idx == dr->num_entries)
		idx = 0;
	return idx;
}

static u32 prev_idx(u32 idx, struct vio_dring_state *dr)
{
	if (idx == 0)
		idx = dr->num_entries - 1;
	else
		idx--;

	return idx;
}

static struct vio_net_desc *get_rx_desc(struct vnet_port *port,
					struct vio_dring_state *dr,
					u32 index)
{
	struct vio_net_desc *desc = port->vio.desc_buf;
	int err;

	err = ldc_get_dring_entry(port->vio.lp, desc, dr->entry_size,
				  (index * dr->entry_size),
				  dr->cookies, dr->ncookies);
	if (err < 0)
		return ERR_PTR(err);

	return desc;
}

static int put_rx_desc(struct vnet_port *port,
		       struct vio_dring_state *dr,
		       struct vio_net_desc *desc,
		       u32 index)
{
	int err;

	err = ldc_put_dring_entry(port->vio.lp, desc, dr->entry_size,
				  (index * dr->entry_size),
				  dr->cookies, dr->ncookies);
	if (err < 0)
		return err;

	return 0;
}

static int vnet_walk_rx_one(struct vnet_port *port,
			    struct vio_dring_state *dr,
			    u32 index, int *needs_ack)
{
	struct vio_net_desc *desc = get_rx_desc(port, dr, index);
	struct vio_driver_state *vio = &port->vio;
	int err;

	if (IS_ERR(desc))
		return PTR_ERR(desc);

	viodbg(DATA, "vio_walk_rx_one desc[%02x:%02x:%08x:%08x:%lx:%lx]\n",
	       desc->hdr.state, desc->hdr.ack,
	       desc->size, desc->ncookies,
	       desc->cookies[0].cookie_addr,
	       desc->cookies[0].cookie_size);

	if (desc->hdr.state != VIO_DESC_READY)
		return 1;
	err = vnet_rx_one(port, desc->size, desc->cookies, desc->ncookies);
	if (err == -ECONNRESET)
		return err;
	desc->hdr.state = VIO_DESC_DONE;
	err = put_rx_desc(port, dr, desc, index);
	if (err < 0)
		return err;
	*needs_ack = desc->hdr.ack;
	return 0;
}

static int vnet_walk_rx(struct vnet_port *port, struct vio_dring_state *dr,
			u32 start, u32 end)
{
	struct vio_driver_state *vio = &port->vio;
	int ack_start = -1, ack_end = -1;

	end = (end == (u32) -1) ? prev_idx(start, dr) : next_idx(end, dr);

	viodbg(DATA, "vnet_walk_rx start[%08x] end[%08x]\n", start, end);

	while (start != end) {
		int ack = 0, err = vnet_walk_rx_one(port, dr, start, &ack);
		if (err == -ECONNRESET)
			return err;
		if (err != 0)
			break;
		if (ack_start == -1)
			ack_start = start;
		ack_end = start;
		start = next_idx(start, dr);
		if (ack && start != end) {
			err = vnet_send_ack(port, dr, ack_start, ack_end,
					    VIO_DRING_ACTIVE);
			if (err == -ECONNRESET)
				return err;
			ack_start = -1;
		}
	}
	if (unlikely(ack_start == -1))
		ack_start = ack_end = prev_idx(start, dr);
	return vnet_send_ack(port, dr, ack_start, ack_end, VIO_DRING_STOPPED);
}

static int vnet_rx(struct vnet_port *port, void *msgbuf)
{
	struct vio_dring_data *pkt = msgbuf;
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_RX_RING];
	struct vio_driver_state *vio = &port->vio;

	viodbg(DATA, "vnet_rx stype_env[%04x] seq[%016lx] rcv_nxt[%016lx]\n",
	       pkt->tag.stype_env, pkt->seq, dr->rcv_nxt);

	if (unlikely(pkt->tag.stype_env != VIO_DRING_DATA))
		return 0;
	if (unlikely(pkt->seq != dr->rcv_nxt)) {
		printk(KERN_ERR PFX "RX out of sequence seq[0x%lx] "
		       "rcv_nxt[0x%lx]\n", pkt->seq, dr->rcv_nxt);
		return 0;
	}

	dr->rcv_nxt++;

	/* XXX Validate pkt->start_idx and pkt->end_idx XXX */

	return vnet_walk_rx(port, dr, pkt->start_idx, pkt->end_idx);
}

static int idx_is_pending(struct vio_dring_state *dr, u32 end)
{
	u32 idx = dr->cons;
	int found = 0;

	while (idx != dr->prod) {
		if (idx == end) {
			found = 1;
			break;
		}
		idx = next_idx(idx, dr);
	}
	return found;
}

static int vnet_ack(struct vnet_port *port, void *msgbuf)
{
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	struct vio_dring_data *pkt = msgbuf;
	struct net_device *dev;
	struct vnet *vp;
	u32 end;

	if (unlikely(pkt->tag.stype_env != VIO_DRING_DATA))
		return 0;

	end = pkt->end_idx;
	if (unlikely(!idx_is_pending(dr, end)))
		return 0;

	dr->cons = next_idx(end, dr);

	vp = port->vp;
	dev = vp->dev;
	if (unlikely(netif_queue_stopped(dev) &&
		     vnet_tx_dring_avail(dr) >= VNET_TX_WAKEUP_THRESH(dr)))
		return 1;

	return 0;
}

static int vnet_nack(struct vnet_port *port, void *msgbuf)
{
	/* XXX just reset or similar XXX */
	return 0;
}

static void maybe_tx_wakeup(struct vnet *vp)
{
	struct net_device *dev = vp->dev;

	netif_tx_lock(dev);
	if (likely(netif_queue_stopped(dev))) {
		struct vnet_port *port;
		int wake = 1;

		list_for_each_entry(port, &vp->port_list, list) {
			struct vio_dring_state *dr;

			dr = &port->vio.drings[VIO_DRIVER_TX_RING];
			if (vnet_tx_dring_avail(dr) <
			    VNET_TX_WAKEUP_THRESH(dr)) {
				wake = 0;
				break;
			}
		}
		if (wake)
			netif_wake_queue(dev);
	}
	netif_tx_unlock(dev);
}

static void vnet_event(void *arg, int event)
{
	struct vnet_port *port = arg;
	struct vio_driver_state *vio = &port->vio;
	unsigned long flags;
	int tx_wakeup, err;

	spin_lock_irqsave(&vio->lock, flags);

	if (unlikely(event == LDC_EVENT_RESET ||
		     event == LDC_EVENT_UP)) {
		vio_link_state_change(vio, event);
		spin_unlock_irqrestore(&vio->lock, flags);

		if (event == LDC_EVENT_RESET)
			vio_port_up(vio);
		return;
	}

	if (unlikely(event != LDC_EVENT_DATA_READY)) {
		printk(KERN_WARNING PFX "Unexpected LDC event %d\n", event);
		spin_unlock_irqrestore(&vio->lock, flags);
		return;
	}

	tx_wakeup = err = 0;
	while (1) {
		union {
			struct vio_msg_tag tag;
			u64 raw[8];
		} msgbuf;

		err = ldc_read(vio->lp, &msgbuf, sizeof(msgbuf));
		if (unlikely(err < 0)) {
			if (err == -ECONNRESET)
				vio_conn_reset(vio);
			break;
		}
		if (err == 0)
			break;
		viodbg(DATA, "TAG [%02x:%02x:%04x:%08x]\n",
		       msgbuf.tag.type,
		       msgbuf.tag.stype,
		       msgbuf.tag.stype_env,
		       msgbuf.tag.sid);
		err = vio_validate_sid(vio, &msgbuf.tag);
		if (err < 0)
			break;

		if (likely(msgbuf.tag.type == VIO_TYPE_DATA)) {
			if (msgbuf.tag.stype == VIO_SUBTYPE_INFO) {
				err = vnet_rx(port, &msgbuf);
			} else if (msgbuf.tag.stype == VIO_SUBTYPE_ACK) {
				err = vnet_ack(port, &msgbuf);
				if (err > 0)
					tx_wakeup |= err;
			} else if (msgbuf.tag.stype == VIO_SUBTYPE_NACK) {
				err = vnet_nack(port, &msgbuf);
			}
		} else if (msgbuf.tag.type == VIO_TYPE_CTRL) {
			err = vio_control_pkt_engine(vio, &msgbuf);
			if (err)
				break;
		} else {
			err = vnet_handle_unknown(port, &msgbuf);
		}
		if (err == -ECONNRESET)
			break;
	}
	spin_unlock(&vio->lock);
	if (unlikely(tx_wakeup && err != -ECONNRESET))
		maybe_tx_wakeup(port->vp);
	local_irq_restore(flags);
}

static int __vnet_tx_trigger(struct vnet_port *port)
{
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	struct vio_dring_data hdr = {
		.tag = {
			.type		= VIO_TYPE_DATA,
			.stype		= VIO_SUBTYPE_INFO,
			.stype_env	= VIO_DRING_DATA,
			.sid		= vio_send_sid(&port->vio),
		},
		.dring_ident		= dr->ident,
		.start_idx		= dr->prod,
		.end_idx		= (u32) -1,
	};
	int err, delay;

	hdr.seq = dr->snd_nxt;
	delay = 1;
	do {
		err = vio_ldc_send(&port->vio, &hdr, sizeof(hdr));
		if (err > 0) {
			dr->snd_nxt++;
			break;
		}
		udelay(delay);
		if ((delay <<= 1) > 128)
			delay = 128;
	} while (err == -EAGAIN);

	return err;
}

struct vnet_port *__tx_port_find(struct vnet *vp, struct sk_buff *skb)
{
	unsigned int hash = vnet_hashfn(skb->data);
	struct hlist_head *hp = &vp->port_hash[hash];
	struct hlist_node *n;
	struct vnet_port *port;

	hlist_for_each_entry(port, n, hp, hash) {
		if (!compare_ether_addr(port->raddr, skb->data))
			return port;
	}
	port = NULL;
	if (!list_empty(&vp->port_list))
		port = list_entry(vp->port_list.next, struct vnet_port, list);

	return port;
}

struct vnet_port *tx_port_find(struct vnet *vp, struct sk_buff *skb)
{
	struct vnet_port *ret;
	unsigned long flags;

	spin_lock_irqsave(&vp->lock, flags);
	ret = __tx_port_find(vp, skb);
	spin_unlock_irqrestore(&vp->lock, flags);

	return ret;
}

static int vnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct vnet *vp = netdev_priv(dev);
	struct vnet_port *port = tx_port_find(vp, skb);
	struct vio_dring_state *dr;
	struct vio_net_desc *d;
	unsigned long flags;
	unsigned int len;
	void *tx_buf;
	int i, err;

	if (unlikely(!port))
		goto out_dropped;

	spin_lock_irqsave(&port->vio.lock, flags);

	dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	if (unlikely(vnet_tx_dring_avail(dr) < 2)) {
		if (!netif_queue_stopped(dev)) {
			netif_stop_queue(dev);

			/* This is a hard error, log it. */
			printk(KERN_ERR PFX "%s: BUG! Tx Ring full when "
			       "queue awake!\n", dev->name);
			dev->stats.tx_errors++;
		}
		spin_unlock_irqrestore(&port->vio.lock, flags);
		return NETDEV_TX_BUSY;
	}

	d = vio_dring_cur(dr);

	tx_buf = port->tx_bufs[dr->prod].buf;
	skb_copy_from_linear_data(skb, tx_buf + VNET_PACKET_SKIP, skb->len);

	len = skb->len;
	if (len < ETH_ZLEN) {
		len = ETH_ZLEN;
		memset(tx_buf+VNET_PACKET_SKIP+skb->len, 0, len - skb->len);
	}

	d->hdr.ack = VIO_ACK_ENABLE;
	d->size = len;
	d->ncookies = port->tx_bufs[dr->prod].ncookies;
	for (i = 0; i < d->ncookies; i++)
		d->cookies[i] = port->tx_bufs[dr->prod].cookies[i];

	/* This has to be a non-SMP write barrier because we are writing
	 * to memory which is shared with the peer LDOM.
	 */
	wmb();

	d->hdr.state = VIO_DESC_READY;

	err = __vnet_tx_trigger(port);
	if (unlikely(err < 0)) {
		printk(KERN_INFO PFX "%s: TX trigger error %d\n",
		       dev->name, err);
		d->hdr.state = VIO_DESC_FREE;
		dev->stats.tx_carrier_errors++;
		goto out_dropped_unlock;
	}

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	dr->prod = (dr->prod + 1) & (VNET_TX_RING_SIZE - 1);
	if (unlikely(vnet_tx_dring_avail(dr) < 2)) {
		netif_stop_queue(dev);
		if (vnet_tx_dring_avail(dr) > VNET_TX_WAKEUP_THRESH(dr))
			netif_wake_queue(dev);
	}

	spin_unlock_irqrestore(&port->vio.lock, flags);

	dev_kfree_skb(skb);

	dev->trans_start = jiffies;
	return NETDEV_TX_OK;

out_dropped_unlock:
	spin_unlock_irqrestore(&port->vio.lock, flags);

out_dropped:
	dev_kfree_skb(skb);
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static void vnet_tx_timeout(struct net_device *dev)
{
	/* XXX Implement me XXX */
}

static int vnet_open(struct net_device *dev)
{
	netif_carrier_on(dev);
	netif_start_queue(dev);

	return 0;
}

static int vnet_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	netif_carrier_off(dev);

	return 0;
}

static void vnet_set_rx_mode(struct net_device *dev)
{
	/* XXX Implement multicast support XXX */
}

static int vnet_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu != ETH_DATA_LEN)
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

static int vnet_set_mac_addr(struct net_device *dev, void *p)
{
	return -EINVAL;
}

static void vnet_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
}

static u32 vnet_get_msglevel(struct net_device *dev)
{
	struct vnet *vp = netdev_priv(dev);
	return vp->msg_enable;
}

static void vnet_set_msglevel(struct net_device *dev, u32 value)
{
	struct vnet *vp = netdev_priv(dev);
	vp->msg_enable = value;
}

static const struct ethtool_ops vnet_ethtool_ops = {
	.get_drvinfo		= vnet_get_drvinfo,
	.get_msglevel		= vnet_get_msglevel,
	.set_msglevel		= vnet_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_perm_addr		= ethtool_op_get_perm_addr,
};

static void vnet_port_free_tx_bufs(struct vnet_port *port)
{
	struct vio_dring_state *dr;
	int i;

	dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	if (dr->base) {
		ldc_free_exp_dring(port->vio.lp, dr->base,
				   (dr->entry_size * dr->num_entries),
				   dr->cookies, dr->ncookies);
		dr->base = NULL;
		dr->entry_size = 0;
		dr->num_entries = 0;
		dr->pending = 0;
		dr->ncookies = 0;
	}

	for (i = 0; i < VNET_TX_RING_SIZE; i++) {
		void *buf = port->tx_bufs[i].buf;

		if (!buf)
			continue;

		ldc_unmap(port->vio.lp,
			  port->tx_bufs[i].cookies,
			  port->tx_bufs[i].ncookies);

		kfree(buf);
		port->tx_bufs[i].buf = NULL;
	}
}

static int __devinit vnet_port_alloc_tx_bufs(struct vnet_port *port)
{
	struct vio_dring_state *dr;
	unsigned long len;
	int i, err, ncookies;
	void *dring;

	for (i = 0; i < VNET_TX_RING_SIZE; i++) {
		void *buf = kzalloc(ETH_FRAME_LEN + 8, GFP_KERNEL);
		int map_len = (ETH_FRAME_LEN + 7) & ~7;

		err = -ENOMEM;
		if (!buf) {
			printk(KERN_ERR "TX buffer allocation failure\n");
			goto err_out;
		}
		err = -EFAULT;
		if ((unsigned long)buf & (8UL - 1)) {
			printk(KERN_ERR "TX buffer misaligned\n");
			kfree(buf);
			goto err_out;
		}

		err = ldc_map_single(port->vio.lp, buf, map_len,
				     port->tx_bufs[i].cookies, 2,
				     (LDC_MAP_SHADOW |
				      LDC_MAP_DIRECT |
				      LDC_MAP_RW));
		if (err < 0) {
			kfree(buf);
			goto err_out;
		}
		port->tx_bufs[i].buf = buf;
		port->tx_bufs[i].ncookies = err;
	}

	dr = &port->vio.drings[VIO_DRIVER_TX_RING];

	len = (VNET_TX_RING_SIZE *
	       (sizeof(struct vio_net_desc) +
		(sizeof(struct ldc_trans_cookie) * 2)));

	ncookies = VIO_MAX_RING_COOKIES;
	dring = ldc_alloc_exp_dring(port->vio.lp, len,
				    dr->cookies, &ncookies,
				    (LDC_MAP_SHADOW |
				     LDC_MAP_DIRECT |
				     LDC_MAP_RW));
	if (IS_ERR(dring)) {
		err = PTR_ERR(dring);
		goto err_out;
	}

	dr->base = dring;
	dr->entry_size = (sizeof(struct vio_net_desc) +
			  (sizeof(struct ldc_trans_cookie) * 2));
	dr->num_entries = VNET_TX_RING_SIZE;
	dr->prod = dr->cons = 0;
	dr->pending = VNET_TX_RING_SIZE;
	dr->ncookies = ncookies;

	return 0;

err_out:
	vnet_port_free_tx_bufs(port);

	return err;
}

static LIST_HEAD(vnet_list);
static DEFINE_MUTEX(vnet_list_mutex);

static struct vnet * __devinit vnet_new(const u64 *local_mac)
{
	struct net_device *dev;
	struct vnet *vp;
	int err, i;

	dev = alloc_etherdev(sizeof(*vp));
	if (!dev) {
		printk(KERN_ERR PFX "Etherdev alloc failed, aborting.\n");
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = (*local_mac >> (5 - i) * 8) & 0xff;

	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

	vp = netdev_priv(dev);

	spin_lock_init(&vp->lock);
	vp->dev = dev;

	INIT_LIST_HEAD(&vp->port_list);
	for (i = 0; i < VNET_PORT_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&vp->port_hash[i]);
	INIT_LIST_HEAD(&vp->list);
	vp->local_mac = *local_mac;

	dev->open = vnet_open;
	dev->stop = vnet_close;
	dev->set_multicast_list = vnet_set_rx_mode;
	dev->set_mac_address = vnet_set_mac_addr;
	dev->tx_timeout = vnet_tx_timeout;
	dev->ethtool_ops = &vnet_ethtool_ops;
	dev->watchdog_timeo = VNET_TX_TIMEOUT;
	dev->change_mtu = vnet_change_mtu;
	dev->hard_start_xmit = vnet_start_xmit;

	err = register_netdev(dev);
	if (err) {
		printk(KERN_ERR PFX "Cannot register net device, "
		       "aborting.\n");
		goto err_out_free_dev;
	}

	printk(KERN_INFO "%s: Sun LDOM vnet ", dev->name);

	for (i = 0; i < 6; i++)
		printk("%2.2x%c", dev->dev_addr[i], i == 5 ? '\n' : ':');

	list_add(&vp->list, &vnet_list);

	return vp;

err_out_free_dev:
	free_netdev(dev);

	return ERR_PTR(err);
}

static struct vnet * __devinit vnet_find_or_create(const u64 *local_mac)
{
	struct vnet *iter, *vp;

	mutex_lock(&vnet_list_mutex);
	vp = NULL;
	list_for_each_entry(iter, &vnet_list, list) {
		if (iter->local_mac == *local_mac) {
			vp = iter;
			break;
		}
	}
	if (!vp)
		vp = vnet_new(local_mac);
	mutex_unlock(&vnet_list_mutex);

	return vp;
}

static const char *local_mac_prop = "local-mac-address";

static struct vnet * __devinit vnet_find_parent(struct mdesc_handle *hp,
						u64 port_node)
{
	const u64 *local_mac = NULL;
	u64 a;

	mdesc_for_each_arc(a, hp, port_node, MDESC_ARC_TYPE_BACK) {
		u64 target = mdesc_arc_target(hp, a);
		const char *name;

		name = mdesc_get_property(hp, target, "name", NULL);
		if (!name || strcmp(name, "network"))
			continue;

		local_mac = mdesc_get_property(hp, target,
					       local_mac_prop, NULL);
		if (local_mac)
			break;
	}
	if (!local_mac)
		return ERR_PTR(-ENODEV);

	return vnet_find_or_create(local_mac);
}

static struct ldc_channel_config vnet_ldc_cfg = {
	.event		= vnet_event,
	.mtu		= 64,
	.mode		= LDC_MODE_UNRELIABLE,
};

static struct vio_driver_ops vnet_vio_ops = {
	.send_attr		= vnet_send_attr,
	.handle_attr		= vnet_handle_attr,
	.handshake_complete	= vnet_handshake_complete,
};

static void print_version(void)
{
	static int version_printed;

	if (version_printed++ == 0)
		printk(KERN_INFO "%s", version);
}

const char *remote_macaddr_prop = "remote-mac-address";

static int __devinit vnet_port_probe(struct vio_dev *vdev,
				     const struct vio_device_id *id)
{
	struct mdesc_handle *hp;
	struct vnet_port *port;
	unsigned long flags;
	struct vnet *vp;
	const u64 *rmac;
	int len, i, err, switch_port;

	print_version();

	hp = mdesc_grab();

	vp = vnet_find_parent(hp, vdev->mp);
	if (IS_ERR(vp)) {
		printk(KERN_ERR PFX "Cannot find port parent vnet.\n");
		err = PTR_ERR(vp);
		goto err_out_put_mdesc;
	}

	rmac = mdesc_get_property(hp, vdev->mp, remote_macaddr_prop, &len);
	err = -ENODEV;
	if (!rmac) {
		printk(KERN_ERR PFX "Port lacks %s property.\n",
		       remote_macaddr_prop);
		goto err_out_put_mdesc;
	}

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	err = -ENOMEM;
	if (!port) {
		printk(KERN_ERR PFX "Cannot allocate vnet_port.\n");
		goto err_out_put_mdesc;
	}

	for (i = 0; i < ETH_ALEN; i++)
		port->raddr[i] = (*rmac >> (5 - i) * 8) & 0xff;

	port->vp = vp;

	err = vio_driver_init(&port->vio, vdev, VDEV_NETWORK,
			      vnet_versions, ARRAY_SIZE(vnet_versions),
			      &vnet_vio_ops, vp->dev->name);
	if (err)
		goto err_out_free_port;

	err = vio_ldc_alloc(&port->vio, &vnet_ldc_cfg, port);
	if (err)
		goto err_out_free_port;

	err = vnet_port_alloc_tx_bufs(port);
	if (err)
		goto err_out_free_ldc;

	INIT_HLIST_NODE(&port->hash);
	INIT_LIST_HEAD(&port->list);

	switch_port = 0;
	if (mdesc_get_property(hp, vdev->mp, "switch-port", NULL) != NULL)
		switch_port = 1;

	spin_lock_irqsave(&vp->lock, flags);
	if (switch_port)
		list_add(&port->list, &vp->port_list);
	else
		list_add_tail(&port->list, &vp->port_list);
	hlist_add_head(&port->hash, &vp->port_hash[vnet_hashfn(port->raddr)]);
	spin_unlock_irqrestore(&vp->lock, flags);

	dev_set_drvdata(&vdev->dev, port);

	printk(KERN_INFO "%s: PORT ( remote-mac ", vp->dev->name);
	for (i = 0; i < 6; i++)
		printk("%2.2x%c", port->raddr[i], i == 5 ? ' ' : ':');
	if (switch_port)
		printk("switch-port ");
	printk(")\n");

	vio_port_up(&port->vio);

	mdesc_release(hp);

	return 0;

err_out_free_ldc:
	vio_ldc_free(&port->vio);

err_out_free_port:
	kfree(port);

err_out_put_mdesc:
	mdesc_release(hp);
	return err;
}

static int vnet_port_remove(struct vio_dev *vdev)
{
	struct vnet_port *port = dev_get_drvdata(&vdev->dev);

	if (port) {
		struct vnet *vp = port->vp;
		unsigned long flags;

		del_timer_sync(&port->vio.timer);

		spin_lock_irqsave(&vp->lock, flags);
		list_del(&port->list);
		hlist_del(&port->hash);
		spin_unlock_irqrestore(&vp->lock, flags);

		vnet_port_free_tx_bufs(port);
		vio_ldc_free(&port->vio);

		dev_set_drvdata(&vdev->dev, NULL);

		kfree(port);
	}
	return 0;
}

static struct vio_device_id vnet_port_match[] = {
	{
		.type = "vnet-port",
	},
	{},
};
MODULE_DEVICE_TABLE(vio, vnet_match);

static struct vio_driver vnet_port_driver = {
	.id_table	= vnet_port_match,
	.probe		= vnet_port_probe,
	.remove		= vnet_port_remove,
	.driver		= {
		.name	= "vnet_port",
		.owner	= THIS_MODULE,
	}
};

static int __init vnet_init(void)
{
	return vio_register_driver(&vnet_port_driver);
}

static void __exit vnet_exit(void)
{
	vio_unregister_driver(&vnet_port_driver);
}

module_init(vnet_init);
module_exit(vnet_exit);
