/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999-2009 Silicon Graphics, Inc. All rights reserved.
 */

/*
 * Cross Partition Network Interface (XPNET) support
 *
 *	XPNET provides a virtual network layered on top of the Cross
 *	Partition communication layer.
 *
 *	XPNET provides direct point-to-point and broadcast-like support
 *	for an ethernet-like device.  The ethernet broadcast medium is
 *	replaced with a point-to-point message structure which passes
 *	pointers to a DMA-capable block that a remote partition should
 *	retrieve and pass to the upper level networking layer.
 *
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "xp.h"

/*
 * The message payload transferred by XPC.
 *
 * buf_pa is the physical address where the DMA should pull from.
 *
 * NOTE: for performance reasons, buf_pa should _ALWAYS_ begin on a
 * cacheline boundary.  To accomplish this, we record the number of
 * bytes from the beginning of the first cacheline to the first useful
 * byte of the skb (leadin_ignore) and the number of bytes from the
 * last useful byte of the skb to the end of the last cacheline
 * (tailout_ignore).
 *
 * size is the number of bytes to transfer which includes the skb->len
 * (useful bytes of the senders skb) plus the leadin and tailout
 */
struct xpnet_message {
	u16 version;		/* Version for this message */
	u16 embedded_bytes;	/* #of bytes embedded in XPC message */
	u32 magic;		/* Special number indicating this is xpnet */
	unsigned long buf_pa;	/* phys address of buffer to retrieve */
	u32 size;		/* #of bytes in buffer */
	u8 leadin_ignore;	/* #of bytes to ignore at the beginning */
	u8 tailout_ignore;	/* #of bytes to ignore at the end */
	unsigned char data;	/* body of small packets */
};

/*
 * Determine the size of our message, the cacheline aligned size,
 * and then the number of message will request from XPC.
 *
 * XPC expects each message to exist in an individual cacheline.
 */
#define XPNET_MSG_SIZE		XPC_MSG_PAYLOAD_MAX_SIZE
#define XPNET_MSG_DATA_MAX	\
		(XPNET_MSG_SIZE - offsetof(struct xpnet_message, data))
#define XPNET_MSG_NENTRIES	(PAGE_SIZE / XPC_MSG_MAX_SIZE)

#define XPNET_MAX_KTHREADS	(XPNET_MSG_NENTRIES + 1)
#define XPNET_MAX_IDLE_KTHREADS	(XPNET_MSG_NENTRIES + 1)

/*
 * Version number of XPNET implementation. XPNET can always talk to versions
 * with same major #, and never talk to versions with a different version.
 */
#define _XPNET_VERSION(_major, _minor)	(((_major) << 4) | (_minor))
#define XPNET_VERSION_MAJOR(_v)		((_v) >> 4)
#define XPNET_VERSION_MINOR(_v)		((_v) & 0xf)

#define	XPNET_VERSION _XPNET_VERSION(1, 0)	/* version 1.0 */
#define	XPNET_VERSION_EMBED _XPNET_VERSION(1, 1)	/* version 1.1 */
#define XPNET_MAGIC	0x88786984	/* "XNET" */

#define XPNET_VALID_MSG(_m)						     \
   ((XPNET_VERSION_MAJOR(_m->version) == XPNET_VERSION_MAJOR(XPNET_VERSION)) \
    && (msg->magic == XPNET_MAGIC))

#define XPNET_DEVICE_NAME		"xp0"

/*
 * When messages are queued with xpc_send_notify, a kmalloc'd buffer
 * of the following type is passed as a notification cookie.  When the
 * notification function is called, we use the cookie to decide
 * whether all outstanding message sends have completed.  The skb can
 * then be released.
 */
struct xpnet_pending_msg {
	struct sk_buff *skb;
	atomic_t use_count;
};

struct net_device *xpnet_device;

/*
 * When we are notified of other partitions activating, we add them to
 * our bitmask of partitions to which we broadcast.
 */
static unsigned long *xpnet_broadcast_partitions;
/* protect above */
static DEFINE_SPINLOCK(xpnet_broadcast_lock);

/*
 * Since the Block Transfer Engine (BTE) is being used for the transfer
 * and it relies upon cache-line size transfers, we need to reserve at
 * least one cache-line for head and tail alignment.  The BTE is
 * limited to 8MB transfers.
 *
 * Testing has shown that changing MTU to greater than 64KB has no effect
 * on TCP as the two sides negotiate a Max Segment Size that is limited
 * to 64K.  Other protocols May use packets greater than this, but for
 * now, the default is 64KB.
 */
#define XPNET_MAX_MTU (0x800000UL - L1_CACHE_BYTES)
/* 68 comes from min TCP+IP+MAC header */
#define XPNET_MIN_MTU 68
/* 32KB has been determined to be the ideal */
#define XPNET_DEF_MTU (0x8000UL)

/*
 * The partid is encapsulated in the MAC address beginning in the following
 * octet and it consists of two octets.
 */
#define XPNET_PARTID_OCTET	2

/* Define the XPNET debug device structures to be used with dev_dbg() et al */

struct device_driver xpnet_dbg_name = {
	.name = "xpnet"
};

struct device xpnet_dbg_subname = {
	.init_name = "",	/* set to "" */
	.driver = &xpnet_dbg_name
};

struct device *xpnet = &xpnet_dbg_subname;

/*
 * Packet was recevied by XPC and forwarded to us.
 */
static void
xpnet_receive(short partid, int channel, struct xpnet_message *msg)
{
	struct sk_buff *skb;
	void *dst;
	enum xp_retval ret;

	if (!XPNET_VALID_MSG(msg)) {
		/*
		 * Packet with a different XPC version.  Ignore.
		 */
		xpc_received(partid, channel, (void *)msg);

		xpnet_device->stats.rx_errors++;

		return;
	}
	dev_dbg(xpnet, "received 0x%lx, %d, %d, %d\n", msg->buf_pa, msg->size,
		msg->leadin_ignore, msg->tailout_ignore);

	/* reserve an extra cache line */
	skb = dev_alloc_skb(msg->size + L1_CACHE_BYTES);
	if (!skb) {
		dev_err(xpnet, "failed on dev_alloc_skb(%d)\n",
			msg->size + L1_CACHE_BYTES);

		xpc_received(partid, channel, (void *)msg);

		xpnet_device->stats.rx_errors++;

		return;
	}

	/*
	 * The allocated skb has some reserved space.
	 * In order to use xp_remote_memcpy(), we need to get the
	 * skb->data pointer moved forward.
	 */
	skb_reserve(skb, (L1_CACHE_BYTES - ((u64)skb->data &
					    (L1_CACHE_BYTES - 1)) +
			  msg->leadin_ignore));

	/*
	 * Update the tail pointer to indicate data actually
	 * transferred.
	 */
	skb_put(skb, (msg->size - msg->leadin_ignore - msg->tailout_ignore));

	/*
	 * Move the data over from the other side.
	 */
	if ((XPNET_VERSION_MINOR(msg->version) == 1) &&
	    (msg->embedded_bytes != 0)) {
		dev_dbg(xpnet, "copying embedded message. memcpy(0x%p, 0x%p, "
			"%lu)\n", skb->data, &msg->data,
			(size_t)msg->embedded_bytes);

		skb_copy_to_linear_data(skb, &msg->data,
					(size_t)msg->embedded_bytes);
	} else {
		dst = (void *)((u64)skb->data & ~(L1_CACHE_BYTES - 1));
		dev_dbg(xpnet, "transferring buffer to the skb->data area;\n\t"
			"xp_remote_memcpy(0x%p, 0x%p, %hu)\n", dst,
					  (void *)msg->buf_pa, msg->size);

		ret = xp_remote_memcpy(xp_pa(dst), msg->buf_pa, msg->size);
		if (ret != xpSuccess) {
			/*
			 * !!! Need better way of cleaning skb.  Currently skb
			 * !!! appears in_use and we can't just call
			 * !!! dev_kfree_skb.
			 */
			dev_err(xpnet, "xp_remote_memcpy(0x%p, 0x%p, 0x%hx) "
				"returned error=0x%x\n", dst,
				(void *)msg->buf_pa, msg->size, ret);

			xpc_received(partid, channel, (void *)msg);

			xpnet_device->stats.rx_errors++;

			return;
		}
	}

	dev_dbg(xpnet, "<skb->head=0x%p skb->data=0x%p skb->tail=0x%p "
		"skb->end=0x%p skb->len=%d\n", (void *)skb->head,
		(void *)skb->data, skb_tail_pointer(skb), skb_end_pointer(skb),
		skb->len);

	skb->protocol = eth_type_trans(skb, xpnet_device);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	dev_dbg(xpnet, "passing skb to network layer\n"
		"\tskb->head=0x%p skb->data=0x%p skb->tail=0x%p "
		"skb->end=0x%p skb->len=%d\n",
		(void *)skb->head, (void *)skb->data, skb_tail_pointer(skb),
		skb_end_pointer(skb), skb->len);

	xpnet_device->stats.rx_packets++;
	xpnet_device->stats.rx_bytes += skb->len + ETH_HLEN;

	netif_rx_ni(skb);
	xpc_received(partid, channel, (void *)msg);
}

/*
 * This is the handler which XPC calls during any sort of change in
 * state or message reception on a connection.
 */
static void
xpnet_connection_activity(enum xp_retval reason, short partid, int channel,
			  void *data, void *key)
{
	DBUG_ON(partid < 0 || partid >= xp_max_npartitions);
	DBUG_ON(channel != XPC_NET_CHANNEL);

	switch (reason) {
	case xpMsgReceived:	/* message received */
		DBUG_ON(data == NULL);

		xpnet_receive(partid, channel, (struct xpnet_message *)data);
		break;

	case xpConnected:	/* connection completed to a partition */
		spin_lock_bh(&xpnet_broadcast_lock);
		__set_bit(partid, xpnet_broadcast_partitions);
		spin_unlock_bh(&xpnet_broadcast_lock);

		netif_carrier_on(xpnet_device);

		dev_dbg(xpnet, "%s connected to partition %d\n",
			xpnet_device->name, partid);
		break;

	default:
		spin_lock_bh(&xpnet_broadcast_lock);
		__clear_bit(partid, xpnet_broadcast_partitions);
		spin_unlock_bh(&xpnet_broadcast_lock);

		if (bitmap_empty((unsigned long *)xpnet_broadcast_partitions,
				 xp_max_npartitions)) {
			netif_carrier_off(xpnet_device);
		}

		dev_dbg(xpnet, "%s disconnected from partition %d\n",
			xpnet_device->name, partid);
		break;
	}
}

static int
xpnet_dev_open(struct net_device *dev)
{
	enum xp_retval ret;

	dev_dbg(xpnet, "calling xpc_connect(%d, 0x%p, NULL, %ld, %ld, %ld, "
		"%ld)\n", XPC_NET_CHANNEL, xpnet_connection_activity,
		(unsigned long)XPNET_MSG_SIZE,
		(unsigned long)XPNET_MSG_NENTRIES,
		(unsigned long)XPNET_MAX_KTHREADS,
		(unsigned long)XPNET_MAX_IDLE_KTHREADS);

	ret = xpc_connect(XPC_NET_CHANNEL, xpnet_connection_activity, NULL,
			  XPNET_MSG_SIZE, XPNET_MSG_NENTRIES,
			  XPNET_MAX_KTHREADS, XPNET_MAX_IDLE_KTHREADS);
	if (ret != xpSuccess) {
		dev_err(xpnet, "ifconfig up of %s failed on XPC connect, "
			"ret=%d\n", dev->name, ret);

		return -ENOMEM;
	}

	dev_dbg(xpnet, "ifconfig up of %s; XPC connected\n", dev->name);

	return 0;
}

static int
xpnet_dev_stop(struct net_device *dev)
{
	xpc_disconnect(XPC_NET_CHANNEL);

	dev_dbg(xpnet, "ifconfig down of %s; XPC disconnected\n", dev->name);

	return 0;
}

/*
 * Notification that the other end has received the message and
 * DMA'd the skb information.  At this point, they are done with
 * our side.  When all recipients are done processing, we
 * release the skb and then release our pending message structure.
 */
static void
xpnet_send_completed(enum xp_retval reason, short partid, int channel,
		     void *__qm)
{
	struct xpnet_pending_msg *queued_msg = (struct xpnet_pending_msg *)__qm;

	DBUG_ON(queued_msg == NULL);

	dev_dbg(xpnet, "message to %d notified with reason %d\n",
		partid, reason);

	if (atomic_dec_return(&queued_msg->use_count) == 0) {
		dev_dbg(xpnet, "all acks for skb->head=-x%p\n",
			(void *)queued_msg->skb->head);

		dev_kfree_skb_any(queued_msg->skb);
		kfree(queued_msg);
	}
}

static void
xpnet_send(struct sk_buff *skb, struct xpnet_pending_msg *queued_msg,
	   u64 start_addr, u64 end_addr, u16 embedded_bytes, int dest_partid)
{
	u8 msg_buffer[XPNET_MSG_SIZE];
	struct xpnet_message *msg = (struct xpnet_message *)&msg_buffer;
	u16 msg_size = sizeof(struct xpnet_message);
	enum xp_retval ret;

	msg->embedded_bytes = embedded_bytes;
	if (unlikely(embedded_bytes != 0)) {
		msg->version = XPNET_VERSION_EMBED;
		dev_dbg(xpnet, "calling memcpy(0x%p, 0x%p, 0x%lx)\n",
			&msg->data, skb->data, (size_t)embedded_bytes);
		skb_copy_from_linear_data(skb, &msg->data,
					  (size_t)embedded_bytes);
		msg_size += embedded_bytes - 1;
	} else {
		msg->version = XPNET_VERSION;
	}
	msg->magic = XPNET_MAGIC;
	msg->size = end_addr - start_addr;
	msg->leadin_ignore = (u64)skb->data - start_addr;
	msg->tailout_ignore = end_addr - (u64)skb_tail_pointer(skb);
	msg->buf_pa = xp_pa((void *)start_addr);

	dev_dbg(xpnet, "sending XPC message to %d:%d\n"
		"msg->buf_pa=0x%lx, msg->size=%u, "
		"msg->leadin_ignore=%u, msg->tailout_ignore=%u\n",
		dest_partid, XPC_NET_CHANNEL, msg->buf_pa, msg->size,
		msg->leadin_ignore, msg->tailout_ignore);

	atomic_inc(&queued_msg->use_count);

	ret = xpc_send_notify(dest_partid, XPC_NET_CHANNEL, XPC_NOWAIT, msg,
			      msg_size, xpnet_send_completed, queued_msg);
	if (unlikely(ret != xpSuccess))
		atomic_dec(&queued_msg->use_count);
}

/*
 * Network layer has formatted a packet (skb) and is ready to place it
 * "on the wire".  Prepare and send an xpnet_message to all partitions
 * which have connected with us and are targets of this packet.
 *
 * MAC-NOTE:  For the XPNET driver, the MAC address contains the
 * destination partid.  If the destination partid octets are 0xffff,
 * this packet is to be broadcast to all connected partitions.
 */
static netdev_tx_t
xpnet_dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct xpnet_pending_msg *queued_msg;
	u64 start_addr, end_addr;
	short dest_partid;
	u16 embedded_bytes = 0;

	dev_dbg(xpnet, ">skb->head=0x%p skb->data=0x%p skb->tail=0x%p "
		"skb->end=0x%p skb->len=%d\n", (void *)skb->head,
		(void *)skb->data, skb_tail_pointer(skb), skb_end_pointer(skb),
		skb->len);

	if (skb->data[0] == 0x33) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;	/* nothing needed to be done */
	}

	/*
	 * The xpnet_pending_msg tracks how many outstanding
	 * xpc_send_notifies are relying on this skb.  When none
	 * remain, release the skb.
	 */
	queued_msg = kmalloc(sizeof(struct xpnet_pending_msg), GFP_ATOMIC);
	if (queued_msg == NULL) {
		dev_warn(xpnet, "failed to kmalloc %ld bytes; dropping "
			 "packet\n", sizeof(struct xpnet_pending_msg));

		dev->stats.tx_errors++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	/* get the beginning of the first cacheline and end of last */
	start_addr = ((u64)skb->data & ~(L1_CACHE_BYTES - 1));
	end_addr = L1_CACHE_ALIGN((u64)skb_tail_pointer(skb));

	/* calculate how many bytes to embed in the XPC message */
	if (unlikely(skb->len <= XPNET_MSG_DATA_MAX)) {
		/* skb->data does fit so embed */
		embedded_bytes = skb->len;
	}

	/*
	 * Since the send occurs asynchronously, we set the count to one
	 * and begin sending.  Any sends that happen to complete before
	 * we are done sending will not free the skb.  We will be left
	 * with that task during exit.  This also handles the case of
	 * a packet destined for a partition which is no longer up.
	 */
	atomic_set(&queued_msg->use_count, 1);
	queued_msg->skb = skb;

	if (skb->data[0] == 0xff) {
		/* we are being asked to broadcast to all partitions */
		for_each_set_bit(dest_partid, xpnet_broadcast_partitions,
			     xp_max_npartitions) {

			xpnet_send(skb, queued_msg, start_addr, end_addr,
				   embedded_bytes, dest_partid);
		}
	} else {
		dest_partid = (short)skb->data[XPNET_PARTID_OCTET + 1];
		dest_partid |= (short)skb->data[XPNET_PARTID_OCTET + 0] << 8;

		if (dest_partid >= 0 &&
		    dest_partid < xp_max_npartitions &&
		    test_bit(dest_partid, xpnet_broadcast_partitions) != 0) {

			xpnet_send(skb, queued_msg, start_addr, end_addr,
				   embedded_bytes, dest_partid);
		}
	}

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	if (atomic_dec_return(&queued_msg->use_count) == 0) {
		dev_kfree_skb(skb);
		kfree(queued_msg);
	}

	return NETDEV_TX_OK;
}

/*
 * Deal with transmit timeouts coming from the network layer.
 */
static void
xpnet_dev_tx_timeout(struct net_device *dev)
{
	dev->stats.tx_errors++;
}

static const struct net_device_ops xpnet_netdev_ops = {
	.ndo_open		= xpnet_dev_open,
	.ndo_stop		= xpnet_dev_stop,
	.ndo_start_xmit		= xpnet_dev_hard_start_xmit,
	.ndo_tx_timeout		= xpnet_dev_tx_timeout,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int __init
xpnet_init(void)
{
	int result;

	if (!is_shub() && !is_uv())
		return -ENODEV;

	dev_info(xpnet, "registering network device %s\n", XPNET_DEVICE_NAME);

	xpnet_broadcast_partitions = kcalloc(BITS_TO_LONGS(xp_max_npartitions),
					     sizeof(long),
					     GFP_KERNEL);
	if (xpnet_broadcast_partitions == NULL)
		return -ENOMEM;

	/*
	 * use ether_setup() to init the majority of our device
	 * structure and then override the necessary pieces.
	 */
	xpnet_device = alloc_netdev(0, XPNET_DEVICE_NAME, NET_NAME_UNKNOWN,
				    ether_setup);
	if (xpnet_device == NULL) {
		kfree(xpnet_broadcast_partitions);
		return -ENOMEM;
	}

	netif_carrier_off(xpnet_device);

	xpnet_device->netdev_ops = &xpnet_netdev_ops;
	xpnet_device->mtu = XPNET_DEF_MTU;
	xpnet_device->min_mtu = XPNET_MIN_MTU;
	xpnet_device->max_mtu = XPNET_MAX_MTU;

	/*
	 * Multicast assumes the LSB of the first octet is set for multicast
	 * MAC addresses.  We chose the first octet of the MAC to be unlikely
	 * to collide with any vendor's officially issued MAC.
	 */
	xpnet_device->dev_addr[0] = 0x02;     /* locally administered, no OUI */

	xpnet_device->dev_addr[XPNET_PARTID_OCTET + 1] = xp_partition_id;
	xpnet_device->dev_addr[XPNET_PARTID_OCTET + 0] = (xp_partition_id >> 8);

	/*
	 * ether_setup() sets this to a multicast device.  We are
	 * really not supporting multicast at this time.
	 */
	xpnet_device->flags &= ~IFF_MULTICAST;

	/*
	 * No need to checksum as it is a DMA transfer.  The BTE will
	 * report an error if the data is not retrievable and the
	 * packet will be dropped.
	 */
	xpnet_device->features = NETIF_F_HW_CSUM;

	result = register_netdev(xpnet_device);
	if (result != 0) {
		free_netdev(xpnet_device);
		kfree(xpnet_broadcast_partitions);
	}

	return result;
}

module_init(xpnet_init);

static void __exit
xpnet_exit(void)
{
	dev_info(xpnet, "unregistering network device %s\n",
		 xpnet_device[0].name);

	unregister_netdev(xpnet_device);
	free_netdev(xpnet_device);
	kfree(xpnet_broadcast_partitions);
}

module_exit(xpnet_exit);

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("Cross Partition Network adapter (XPNET)");
MODULE_LICENSE("GPL");
