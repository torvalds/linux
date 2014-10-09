/* sunvnet.c: Sun LDOM Virtual Network Driver.
 *
 * Copyright (C) 2007, 2008 David S. Miller <davem@davemloft.net>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/if_vlan.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <linux/icmpv6.h>
#endif

#include <net/icmp.h>
#include <net/route.h>

#include <asm/vio.h>
#include <asm/ldc.h>

#include "sunvnet.h"

#define DRV_MODULE_NAME		"sunvnet"
#define DRV_MODULE_VERSION	"1.0"
#define DRV_MODULE_RELDATE	"June 25, 2007"

static char version[] =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";
MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("Sun LDOM virtual network driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

/* Heuristic for the number of times to exponentially backoff and
 * retry sending an LDC trigger when EAGAIN is encountered
 */
#define	VNET_MAX_RETRIES	10

static int __vnet_tx_trigger(struct vnet_port *port, u32 start);

/* Ordered from largest major to lowest */
static struct vio_version vnet_versions[] = {
	{ .major = 1, .minor = 6 },
	{ .major = 1, .minor = 0 },
};

static inline u32 vnet_tx_dring_avail(struct vio_dring_state *dr)
{
	return vio_dring_avail(dr, VNET_TX_RING_SIZE);
}

static int vnet_handle_unknown(struct vnet_port *port, void *arg)
{
	struct vio_msg_tag *pkt = arg;

	pr_err("Received unknown msg [%02x:%02x:%04x:%08x]\n",
	       pkt->type, pkt->stype, pkt->stype_env, pkt->sid);
	pr_err("Resetting connection\n");

	ldc_disconnect(port->vio.lp);

	return -ECONNRESET;
}

static int vnet_send_attr(struct vio_driver_state *vio)
{
	struct vnet_port *port = to_vnet_port(vio);
	struct net_device *dev = port->vp->dev;
	struct vio_net_attr_info pkt;
	int framelen = ETH_FRAME_LEN;
	int i;

	memset(&pkt, 0, sizeof(pkt));
	pkt.tag.type = VIO_TYPE_CTRL;
	pkt.tag.stype = VIO_SUBTYPE_INFO;
	pkt.tag.stype_env = VIO_ATTR_INFO;
	pkt.tag.sid = vio_send_sid(vio);
	if (vio_version_before(vio, 1, 2))
		pkt.xfer_mode = VIO_DRING_MODE;
	else
		pkt.xfer_mode = VIO_NEW_DRING_MODE;
	pkt.addr_type = VNET_ADDR_ETHERMAC;
	pkt.ack_freq = 0;
	for (i = 0; i < 6; i++)
		pkt.addr |= (u64)dev->dev_addr[i] << ((5 - i) * 8);
	if (vio_version_after(vio, 1, 3)) {
		if (port->rmtu) {
			port->rmtu = min(VNET_MAXPACKET, port->rmtu);
			pkt.mtu = port->rmtu;
		} else {
			port->rmtu = VNET_MAXPACKET;
			pkt.mtu = port->rmtu;
		}
		if (vio_version_after_eq(vio, 1, 6))
			pkt.options = VIO_TX_DRING;
	} else if (vio_version_before(vio, 1, 3)) {
		pkt.mtu = framelen;
	} else { /* v1.3 */
		pkt.mtu = framelen + VLAN_HLEN;
	}

	pkt.plnk_updt = PHYSLINK_UPDATE_NONE;
	pkt.cflags = 0;

	viodbg(HS, "SEND NET ATTR xmode[0x%x] atype[0x%x] addr[%llx] "
	       "ackfreq[%u] plnk_updt[0x%02x] opts[0x%02x] mtu[%llu] "
	       "cflags[0x%04x] lso_max[%u]\n",
	       pkt.xfer_mode, pkt.addr_type,
	       (unsigned long long)pkt.addr,
	       pkt.ack_freq, pkt.plnk_updt, pkt.options,
	       (unsigned long long)pkt.mtu, pkt.cflags, pkt.ipv4_lso_maxlen);


	return vio_ldc_send(vio, &pkt, sizeof(pkt));
}

static int handle_attr_info(struct vio_driver_state *vio,
			    struct vio_net_attr_info *pkt)
{
	struct vnet_port *port = to_vnet_port(vio);
	u64	localmtu;
	u8	xfer_mode;

	viodbg(HS, "GOT NET ATTR xmode[0x%x] atype[0x%x] addr[%llx] "
	       "ackfreq[%u] plnk_updt[0x%02x] opts[0x%02x] mtu[%llu] "
	       " (rmtu[%llu]) cflags[0x%04x] lso_max[%u]\n",
	       pkt->xfer_mode, pkt->addr_type,
	       (unsigned long long)pkt->addr,
	       pkt->ack_freq, pkt->plnk_updt, pkt->options,
	       (unsigned long long)pkt->mtu, port->rmtu, pkt->cflags,
	       pkt->ipv4_lso_maxlen);

	pkt->tag.sid = vio_send_sid(vio);

	xfer_mode = pkt->xfer_mode;
	/* for version < 1.2, VIO_DRING_MODE = 0x3 and no bitmask */
	if (vio_version_before(vio, 1, 2) && xfer_mode == VIO_DRING_MODE)
		xfer_mode = VIO_NEW_DRING_MODE;

	/* MTU negotiation:
	 *	< v1.3 - ETH_FRAME_LEN exactly
	 *	> v1.3 - MIN(pkt.mtu, VNET_MAXPACKET, port->rmtu) and change
	 *			pkt->mtu for ACK
	 *	= v1.3 - ETH_FRAME_LEN + VLAN_HLEN exactly
	 */
	if (vio_version_before(vio, 1, 3)) {
		localmtu = ETH_FRAME_LEN;
	} else if (vio_version_after(vio, 1, 3)) {
		localmtu = port->rmtu ? port->rmtu : VNET_MAXPACKET;
		localmtu = min(pkt->mtu, localmtu);
		pkt->mtu = localmtu;
	} else { /* v1.3 */
		localmtu = ETH_FRAME_LEN + VLAN_HLEN;
	}
	port->rmtu = localmtu;

	/* for version >= 1.6, ACK packet mode we support */
	if (vio_version_after_eq(vio, 1, 6)) {
		pkt->xfer_mode = VIO_NEW_DRING_MODE;
		pkt->options = VIO_TX_DRING;
	}

	if (!(xfer_mode | VIO_NEW_DRING_MODE) ||
	    pkt->addr_type != VNET_ADDR_ETHERMAC ||
	    pkt->mtu != localmtu) {
		viodbg(HS, "SEND NET ATTR NACK\n");

		pkt->tag.stype = VIO_SUBTYPE_NACK;

		(void) vio_ldc_send(vio, pkt, sizeof(*pkt));

		return -ECONNRESET;
	} else {
		viodbg(HS, "SEND NET ATTR ACK xmode[0x%x] atype[0x%x] "
		       "addr[%llx] ackfreq[%u] plnk_updt[0x%02x] opts[0x%02x] "
		       "mtu[%llu] (rmtu[%llu]) cflags[0x%04x] lso_max[%u]\n",
		       pkt->xfer_mode, pkt->addr_type,
		       (unsigned long long)pkt->addr,
		       pkt->ack_freq, pkt->plnk_updt, pkt->options,
		       (unsigned long long)pkt->mtu, port->rmtu, pkt->cflags,
		       pkt->ipv4_lso_maxlen);

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
	if (unlikely(len < ETH_ZLEN || len > port->rmtu)) {
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
	int retries = 0;

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
		if (retries++ > VNET_MAX_RETRIES) {
			pr_info("ECONNRESET %x:%x:%x:%x:%x:%x\n",
				port->raddr[0], port->raddr[1],
				port->raddr[2], port->raddr[3],
				port->raddr[4], port->raddr[5]);
			break;
		}
	} while (err == -EAGAIN);

	if (err <= 0 && vio_dring_state == VIO_DRING_STOPPED) {
		port->stop_rx_idx = end;
		port->stop_rx = true;
	} else {
		port->stop_rx_idx = 0;
		port->stop_rx = false;
	}

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

	if (desc->hdr.state != VIO_DESC_READY)
		return 1;

	rmb();

	viodbg(DATA, "vio_walk_rx_one desc[%02x:%02x:%08x:%08x:%llx:%llx]\n",
	       desc->hdr.state, desc->hdr.ack,
	       desc->size, desc->ncookies,
	       desc->cookies[0].cookie_addr,
	       desc->cookies[0].cookie_size);

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

	viodbg(DATA, "vnet_rx stype_env[%04x] seq[%016llx] rcv_nxt[%016llx]\n",
	       pkt->tag.stype_env, pkt->seq, dr->rcv_nxt);

	if (unlikely(pkt->tag.stype_env != VIO_DRING_DATA))
		return 0;
	if (unlikely(pkt->seq != dr->rcv_nxt)) {
		pr_err("RX out of sequence seq[0x%llx] rcv_nxt[0x%llx]\n",
		       pkt->seq, dr->rcv_nxt);
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
	struct vio_net_desc *desc;
	if (unlikely(pkt->tag.stype_env != VIO_DRING_DATA))
		return 0;

	end = pkt->end_idx;
	if (unlikely(!idx_is_pending(dr, end)))
		return 0;

	/* sync for race conditions with vnet_start_xmit() and tell xmit it
	 * is time to send a trigger.
	 */
	dr->cons = next_idx(end, dr);
	desc = vio_dring_entry(dr, dr->cons);
	if (desc->hdr.state == VIO_DESC_READY && port->start_cons) {
		/* vnet_start_xmit() just populated this dring but missed
		 * sending the "start" LDC message to the consumer.
		 * Send a "start" trigger on its behalf.
		 */
		if (__vnet_tx_trigger(port, dr->cons) > 0)
			port->start_cons = false;
		else
			port->start_cons = true;
	} else {
		port->start_cons = true;
	}


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

static int handle_mcast(struct vnet_port *port, void *msgbuf)
{
	struct vio_net_mcast_info *pkt = msgbuf;

	if (pkt->tag.stype != VIO_SUBTYPE_ACK)
		pr_err("%s: Got unexpected MCAST reply [%02x:%02x:%04x:%08x]\n",
		       port->vp->dev->name,
		       pkt->tag.type,
		       pkt->tag.stype,
		       pkt->tag.stype_env,
		       pkt->tag.sid);

	return 0;
}

static void maybe_tx_wakeup(unsigned long param)
{
	struct vnet *vp = (struct vnet *)param;
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

		if (event == LDC_EVENT_RESET) {
			port->rmtu = 0;
			vio_port_up(vio);
		}
		return;
	}

	if (unlikely(event != LDC_EVENT_DATA_READY)) {
		pr_warn("Unexpected LDC event %d\n", event);
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
			if (msgbuf.tag.stype_env == VNET_MCAST_INFO)
				err = handle_mcast(port, &msgbuf);
			else
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
	/* Kick off a tasklet to wake the queue.  We cannot call
	 * maybe_tx_wakeup directly here because we could deadlock on
	 * netif_tx_lock() with dev_watchdog()
	 */
	if (unlikely(tx_wakeup && err != -ECONNRESET))
		tasklet_schedule(&port->vp->vnet_tx_wakeup);

	local_irq_restore(flags);
}

static int __vnet_tx_trigger(struct vnet_port *port, u32 start)
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
		.start_idx		= start,
		.end_idx		= (u32) -1,
	};
	int err, delay;
	int retries = 0;

	if (port->stop_rx) {
		err = vnet_send_ack(port,
				    &port->vio.drings[VIO_DRIVER_RX_RING],
				    port->stop_rx_idx, -1,
				    VIO_DRING_STOPPED);
		if (err <= 0)
			return err;
	}

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
		if (retries++ > VNET_MAX_RETRIES)
			break;
	} while (err == -EAGAIN);

	return err;
}

static inline bool port_is_up(struct vnet_port *vnet)
{
	struct vio_driver_state *vio = &vnet->vio;

	return !!(vio->hs_state & VIO_HS_COMPLETE);
}

struct vnet_port *__tx_port_find(struct vnet *vp, struct sk_buff *skb)
{
	unsigned int hash = vnet_hashfn(skb->data);
	struct hlist_head *hp = &vp->port_hash[hash];
	struct vnet_port *port;

	hlist_for_each_entry(port, hp, hash) {
		if (!port_is_up(port))
			continue;
		if (ether_addr_equal(port->raddr, skb->data))
			return port;
	}
	list_for_each_entry(port, &vp->port_list, list) {
		if (!port->switch_port)
			continue;
		if (!port_is_up(port))
			continue;
		return port;
	}
	return NULL;
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

static struct sk_buff *vnet_clean_tx_ring(struct vnet_port *port,
					  unsigned *pending)
{
	struct vio_dring_state *dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	struct sk_buff *skb = NULL;
	int i, txi;

	*pending = 0;

	txi = dr->prod-1;
	if (txi < 0)
		txi = VNET_TX_RING_SIZE-1;

	for (i = 0; i < VNET_TX_RING_SIZE; ++i) {
		struct vio_net_desc *d;

		d = vio_dring_entry(dr, txi);

		if (d->hdr.state == VIO_DESC_DONE) {
			if (port->tx_bufs[txi].skb) {
				BUG_ON(port->tx_bufs[txi].skb->next);

				port->tx_bufs[txi].skb->next = skb;
				skb = port->tx_bufs[txi].skb;
				port->tx_bufs[txi].skb = NULL;

				ldc_unmap(port->vio.lp,
					  port->tx_bufs[txi].cookies,
					  port->tx_bufs[txi].ncookies);
			}
			d->hdr.state = VIO_DESC_FREE;
		} else if (d->hdr.state == VIO_DESC_READY) {
			(*pending)++;
		} else if (d->hdr.state == VIO_DESC_FREE) {
			break;
		}
		--txi;
		if (txi < 0)
			txi = VNET_TX_RING_SIZE-1;
	}
	return skb;
}

static inline void vnet_free_skbs(struct sk_buff *skb)
{
	struct sk_buff *next;

	while (skb) {
		next = skb->next;
		skb->next = NULL;
		dev_kfree_skb(skb);
		skb = next;
	}
}

static void vnet_clean_timer_expire(unsigned long port0)
{
	struct vnet_port *port = (struct vnet_port *)port0;
	struct sk_buff *freeskbs;
	unsigned pending;
	unsigned long flags;

	spin_lock_irqsave(&port->vio.lock, flags);
	freeskbs = vnet_clean_tx_ring(port, &pending);
	spin_unlock_irqrestore(&port->vio.lock, flags);

	vnet_free_skbs(freeskbs);

	if (pending)
		(void)mod_timer(&port->clean_timer,
				jiffies + VNET_CLEAN_TIMEOUT);
	 else
		del_timer(&port->clean_timer);
}

static inline struct sk_buff *vnet_skb_shape(struct sk_buff *skb, void **pstart,
					     int *plen)
{
	struct sk_buff *nskb;
	int len, pad;

	len = skb->len;
	pad = 0;
	if (len < ETH_ZLEN) {
		pad += ETH_ZLEN - skb->len;
		len += pad;
	}
	len += VNET_PACKET_SKIP;
	pad += 8 - (len & 7);
	len += 8 - (len & 7);

	if (((unsigned long)skb->data & 7) != VNET_PACKET_SKIP ||
	    skb_tailroom(skb) < pad ||
	    skb_headroom(skb) < VNET_PACKET_SKIP) {
		nskb = alloc_and_align_skb(skb->dev, skb->len);
		skb_reserve(nskb, VNET_PACKET_SKIP);
		if (skb_copy_bits(skb, 0, nskb->data, skb->len)) {
			dev_kfree_skb(nskb);
			dev_kfree_skb(skb);
			return NULL;
		}
		(void)skb_put(nskb, skb->len);
		dev_kfree_skb(skb);
		skb = nskb;
	}

	*pstart = skb->data - VNET_PACKET_SKIP;
	*plen = len;
	return skb;
}

static int vnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct vnet *vp = netdev_priv(dev);
	struct vnet_port *port = tx_port_find(vp, skb);
	struct vio_dring_state *dr;
	struct vio_net_desc *d;
	unsigned long flags;
	unsigned int len;
	struct sk_buff *freeskbs = NULL;
	int i, err, txi;
	void *start = NULL;
	int nlen = 0;
	unsigned pending = 0;

	if (unlikely(!port))
		goto out_dropped;

	skb = vnet_skb_shape(skb, &start, &nlen);

	if (unlikely(!skb))
		goto out_dropped;

	if (skb->len > port->rmtu) {
		unsigned long localmtu = port->rmtu - ETH_HLEN;

		if (vio_version_after_eq(&port->vio, 1, 3))
			localmtu -= VLAN_HLEN;

		if (skb->protocol == htons(ETH_P_IP)) {
			struct flowi4 fl4;
			struct rtable *rt = NULL;

			memset(&fl4, 0, sizeof(fl4));
			fl4.flowi4_oif = dev->ifindex;
			fl4.flowi4_tos = RT_TOS(ip_hdr(skb)->tos);
			fl4.daddr = ip_hdr(skb)->daddr;
			fl4.saddr = ip_hdr(skb)->saddr;

			rt = ip_route_output_key(dev_net(dev), &fl4);
			if (!IS_ERR(rt)) {
				skb_dst_set(skb, &rt->dst);
				icmp_send(skb, ICMP_DEST_UNREACH,
					  ICMP_FRAG_NEEDED,
					  htonl(localmtu));
			}
		}
#if IS_ENABLED(CONFIG_IPV6)
		else if (skb->protocol == htons(ETH_P_IPV6))
			icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, localmtu);
#endif
		goto out_dropped;
	}

	spin_lock_irqsave(&port->vio.lock, flags);

	dr = &port->vio.drings[VIO_DRIVER_TX_RING];
	if (unlikely(vnet_tx_dring_avail(dr) < 2)) {
		if (!netif_queue_stopped(dev)) {
			netif_stop_queue(dev);

			/* This is a hard error, log it. */
			netdev_err(dev, "BUG! Tx Ring full when queue awake!\n");
			dev->stats.tx_errors++;
		}
		spin_unlock_irqrestore(&port->vio.lock, flags);
		return NETDEV_TX_BUSY;
	}

	d = vio_dring_cur(dr);

	txi = dr->prod;

	freeskbs = vnet_clean_tx_ring(port, &pending);

	BUG_ON(port->tx_bufs[txi].skb);

	len = skb->len;
	if (len < ETH_ZLEN)
		len = ETH_ZLEN;

	port->tx_bufs[txi].skb = skb;
	skb = NULL;

	err = ldc_map_single(port->vio.lp, start, nlen,
			     port->tx_bufs[txi].cookies, VNET_MAXCOOKIES,
			     (LDC_MAP_SHADOW | LDC_MAP_DIRECT | LDC_MAP_RW));
	if (err < 0) {
		netdev_info(dev, "tx buffer map error %d\n", err);
		goto out_dropped_unlock;
	}
	port->tx_bufs[txi].ncookies = err;

	/* We don't rely on the ACKs to free the skb in vnet_start_xmit(),
	 * thus it is safe to not set VIO_ACK_ENABLE for each transmission:
	 * the protocol itself does not require it as long as the peer
	 * sends a VIO_SUBTYPE_ACK for VIO_DRING_STOPPED.
	 *
	 * An ACK for every packet in the ring is expensive as the
	 * sending of LDC messages is slow and affects performance.
	 */
	d->hdr.ack = VIO_ACK_DISABLE;
	d->size = len;
	d->ncookies = port->tx_bufs[txi].ncookies;
	for (i = 0; i < d->ncookies; i++)
		d->cookies[i] = port->tx_bufs[txi].cookies[i];

	/* This has to be a non-SMP write barrier because we are writing
	 * to memory which is shared with the peer LDOM.
	 */
	wmb();

	d->hdr.state = VIO_DESC_READY;

	/* Exactly one ldc "start" trigger (for dr->cons) needs to be sent
	 * to notify the consumer that some descriptors are READY.
	 * After that "start" trigger, no additional triggers are needed until
	 * a DRING_STOPPED is received from the consumer. The dr->cons field
	 * (set up by vnet_ack()) has the value of the next dring index
	 * that has not yet been ack-ed. We send a "start" trigger here
	 * if, and only if, start_cons is true (reset it afterward). Conversely,
	 * vnet_ack() should check if the dring corresponding to cons
	 * is marked READY, but start_cons was false.
	 * If so, vnet_ack() should send out the missed "start" trigger.
	 *
	 * Note that the wmb() above makes sure the cookies et al. are
	 * not globally visible before the VIO_DESC_READY, and that the
	 * stores are ordered correctly by the compiler. The consumer will
	 * not proceed until the VIO_DESC_READY is visible assuring that
	 * the consumer does not observe anything related to descriptors
	 * out of order. The HV trap from the LDC start trigger is the
	 * producer to consumer announcement that work is available to the
	 * consumer
	 */
	if (!port->start_cons)
		goto ldc_start_done; /* previous trigger suffices */

	err = __vnet_tx_trigger(port, dr->cons);
	if (unlikely(err < 0)) {
		netdev_info(dev, "TX trigger error %d\n", err);
		d->hdr.state = VIO_DESC_FREE;
		dev->stats.tx_carrier_errors++;
		goto out_dropped_unlock;
	}

ldc_start_done:
	port->start_cons = false;

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += port->tx_bufs[txi].skb->len;

	dr->prod = (dr->prod + 1) & (VNET_TX_RING_SIZE - 1);
	if (unlikely(vnet_tx_dring_avail(dr) < 2)) {
		netif_stop_queue(dev);
		if (vnet_tx_dring_avail(dr) > VNET_TX_WAKEUP_THRESH(dr))
			netif_wake_queue(dev);
	}

	spin_unlock_irqrestore(&port->vio.lock, flags);

	vnet_free_skbs(freeskbs);

	(void)mod_timer(&port->clean_timer, jiffies + VNET_CLEAN_TIMEOUT);

	return NETDEV_TX_OK;

out_dropped_unlock:
	spin_unlock_irqrestore(&port->vio.lock, flags);

out_dropped:
	if (skb)
		dev_kfree_skb(skb);
	vnet_free_skbs(freeskbs);
	if (pending)
		(void)mod_timer(&port->clean_timer,
				jiffies + VNET_CLEAN_TIMEOUT);
	else if (port)
		del_timer(&port->clean_timer);
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

static struct vnet_mcast_entry *__vnet_mc_find(struct vnet *vp, u8 *addr)
{
	struct vnet_mcast_entry *m;

	for (m = vp->mcast_list; m; m = m->next) {
		if (ether_addr_equal(m->addr, addr))
			return m;
	}
	return NULL;
}

static void __update_mc_list(struct vnet *vp, struct net_device *dev)
{
	struct netdev_hw_addr *ha;

	netdev_for_each_mc_addr(ha, dev) {
		struct vnet_mcast_entry *m;

		m = __vnet_mc_find(vp, ha->addr);
		if (m) {
			m->hit = 1;
			continue;
		}

		if (!m) {
			m = kzalloc(sizeof(*m), GFP_ATOMIC);
			if (!m)
				continue;
			memcpy(m->addr, ha->addr, ETH_ALEN);
			m->hit = 1;

			m->next = vp->mcast_list;
			vp->mcast_list = m;
		}
	}
}

static void __send_mc_list(struct vnet *vp, struct vnet_port *port)
{
	struct vio_net_mcast_info info;
	struct vnet_mcast_entry *m, **pp;
	int n_addrs;

	memset(&info, 0, sizeof(info));

	info.tag.type = VIO_TYPE_CTRL;
	info.tag.stype = VIO_SUBTYPE_INFO;
	info.tag.stype_env = VNET_MCAST_INFO;
	info.tag.sid = vio_send_sid(&port->vio);
	info.set = 1;

	n_addrs = 0;
	for (m = vp->mcast_list; m; m = m->next) {
		if (m->sent)
			continue;
		m->sent = 1;
		memcpy(&info.mcast_addr[n_addrs * ETH_ALEN],
		       m->addr, ETH_ALEN);
		if (++n_addrs == VNET_NUM_MCAST) {
			info.count = n_addrs;

			(void) vio_ldc_send(&port->vio, &info,
					    sizeof(info));
			n_addrs = 0;
		}
	}
	if (n_addrs) {
		info.count = n_addrs;
		(void) vio_ldc_send(&port->vio, &info, sizeof(info));
	}

	info.set = 0;

	n_addrs = 0;
	pp = &vp->mcast_list;
	while ((m = *pp) != NULL) {
		if (m->hit) {
			m->hit = 0;
			pp = &m->next;
			continue;
		}

		memcpy(&info.mcast_addr[n_addrs * ETH_ALEN],
		       m->addr, ETH_ALEN);
		if (++n_addrs == VNET_NUM_MCAST) {
			info.count = n_addrs;
			(void) vio_ldc_send(&port->vio, &info,
					    sizeof(info));
			n_addrs = 0;
		}

		*pp = m->next;
		kfree(m);
	}
	if (n_addrs) {
		info.count = n_addrs;
		(void) vio_ldc_send(&port->vio, &info, sizeof(info));
	}
}

static void vnet_set_rx_mode(struct net_device *dev)
{
	struct vnet *vp = netdev_priv(dev);
	struct vnet_port *port;
	unsigned long flags;

	spin_lock_irqsave(&vp->lock, flags);
	if (!list_empty(&vp->port_list)) {
		port = list_entry(vp->port_list.next, struct vnet_port, list);

		if (port->switch_port) {
			__update_mc_list(vp, dev);
			__send_mc_list(vp, port);
		}
	}
	spin_unlock_irqrestore(&vp->lock, flags);
}

static int vnet_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < 68 || new_mtu > 65535)
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
	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
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
		struct vio_net_desc *d;
		void *skb = port->tx_bufs[i].skb;

		if (!skb)
			continue;

		d = vio_dring_entry(dr, i);
		if (d->hdr.state == VIO_DESC_READY)
			pr_warn("active transmit buffers freed\n");

		ldc_unmap(port->vio.lp,
			  port->tx_bufs[i].cookies,
			  port->tx_bufs[i].ncookies);
		dev_kfree_skb(skb);
		port->tx_bufs[i].skb = NULL;
		d->hdr.state = VIO_DESC_FREE;
	}
}

static int vnet_port_alloc_tx_bufs(struct vnet_port *port)
{
	struct vio_dring_state *dr;
	unsigned long len;
	int i, err, ncookies;
	void *dring;

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
	port->start_cons  = true; /* need an initial trigger */
	dr->pending = VNET_TX_RING_SIZE;
	dr->ncookies = ncookies;

	for (i = 0; i < VNET_TX_RING_SIZE; ++i) {
		struct vio_net_desc *d;

		d = vio_dring_entry(dr, i);
		d->hdr.state = VIO_DESC_FREE;
	}
	return 0;

err_out:
	vnet_port_free_tx_bufs(port);

	return err;
}

static LIST_HEAD(vnet_list);
static DEFINE_MUTEX(vnet_list_mutex);

static const struct net_device_ops vnet_ops = {
	.ndo_open		= vnet_open,
	.ndo_stop		= vnet_close,
	.ndo_set_rx_mode	= vnet_set_rx_mode,
	.ndo_set_mac_address	= vnet_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= vnet_tx_timeout,
	.ndo_change_mtu		= vnet_change_mtu,
	.ndo_start_xmit		= vnet_start_xmit,
};

static struct vnet *vnet_new(const u64 *local_mac)
{
	struct net_device *dev;
	struct vnet *vp;
	int err, i;

	dev = alloc_etherdev(sizeof(*vp));
	if (!dev)
		return ERR_PTR(-ENOMEM);
	dev->needed_headroom = VNET_PACKET_SKIP + 8;
	dev->needed_tailroom = 8;

	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = (*local_mac >> (5 - i) * 8) & 0xff;

	vp = netdev_priv(dev);

	spin_lock_init(&vp->lock);
	tasklet_init(&vp->vnet_tx_wakeup, maybe_tx_wakeup, (unsigned long)vp);
	vp->dev = dev;

	INIT_LIST_HEAD(&vp->port_list);
	for (i = 0; i < VNET_PORT_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&vp->port_hash[i]);
	INIT_LIST_HEAD(&vp->list);
	vp->local_mac = *local_mac;

	dev->netdev_ops = &vnet_ops;
	dev->ethtool_ops = &vnet_ethtool_ops;
	dev->watchdog_timeo = VNET_TX_TIMEOUT;

	err = register_netdev(dev);
	if (err) {
		pr_err("Cannot register net device, aborting\n");
		goto err_out_free_dev;
	}

	netdev_info(dev, "Sun LDOM vnet %pM\n", dev->dev_addr);

	list_add(&vp->list, &vnet_list);

	return vp;

err_out_free_dev:
	free_netdev(dev);

	return ERR_PTR(err);
}

static struct vnet *vnet_find_or_create(const u64 *local_mac)
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

static void vnet_cleanup(void)
{
	struct vnet *vp;
	struct net_device *dev;

	mutex_lock(&vnet_list_mutex);
	while (!list_empty(&vnet_list)) {
		vp = list_first_entry(&vnet_list, struct vnet, list);
		list_del(&vp->list);
		dev = vp->dev;
		tasklet_kill(&vp->vnet_tx_wakeup);
		/* vio_unregister_driver() should have cleaned up port_list */
		BUG_ON(!list_empty(&vp->port_list));
		unregister_netdev(dev);
		free_netdev(dev);
	}
	mutex_unlock(&vnet_list_mutex);
}

static const char *local_mac_prop = "local-mac-address";

static struct vnet *vnet_find_parent(struct mdesc_handle *hp,
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
	printk_once(KERN_INFO "%s", version);
}

const char *remote_macaddr_prop = "remote-mac-address";

static int vnet_port_probe(struct vio_dev *vdev, const struct vio_device_id *id)
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
		pr_err("Cannot find port parent vnet\n");
		err = PTR_ERR(vp);
		goto err_out_put_mdesc;
	}

	rmac = mdesc_get_property(hp, vdev->mp, remote_macaddr_prop, &len);
	err = -ENODEV;
	if (!rmac) {
		pr_err("Port lacks %s property\n", remote_macaddr_prop);
		goto err_out_put_mdesc;
	}

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	err = -ENOMEM;
	if (!port)
		goto err_out_put_mdesc;

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
	port->switch_port = switch_port;

	spin_lock_irqsave(&vp->lock, flags);
	if (switch_port)
		list_add(&port->list, &vp->port_list);
	else
		list_add_tail(&port->list, &vp->port_list);
	hlist_add_head(&port->hash, &vp->port_hash[vnet_hashfn(port->raddr)]);
	spin_unlock_irqrestore(&vp->lock, flags);

	dev_set_drvdata(&vdev->dev, port);

	pr_info("%s: PORT ( remote-mac %pM%s )\n",
		vp->dev->name, port->raddr, switch_port ? " switch-port" : "");

	setup_timer(&port->clean_timer, vnet_clean_timer_expire,
		    (unsigned long)port);

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
		del_timer_sync(&port->clean_timer);

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

static const struct vio_device_id vnet_port_match[] = {
	{
		.type = "vnet-port",
	},
	{},
};
MODULE_DEVICE_TABLE(vio, vnet_port_match);

static struct vio_driver vnet_port_driver = {
	.id_table	= vnet_port_match,
	.probe		= vnet_port_probe,
	.remove		= vnet_port_remove,
	.name		= "vnet_port",
};

static int __init vnet_init(void)
{
	return vio_register_driver(&vnet_port_driver);
}

static void __exit vnet_exit(void)
{
	vio_unregister_driver(&vnet_port_driver);
	vnet_cleanup();
}

module_init(vnet_init);
module_exit(vnet_exit);
