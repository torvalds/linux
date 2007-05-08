/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999,2001-2005 Silicon Graphics, Inc. All rights reserved.
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


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <asm/sn/bte.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_sal.h>
#include <asm/types.h>
#include <asm/atomic.h>
#include <asm/sn/xp.h>


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
	u64 buf_pa;		/* phys address of buffer to retrieve */
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
#define XPNET_MSG_SIZE		(L1_CACHE_BYTES - XPC_MSG_PAYLOAD_OFFSET)
#define XPNET_MSG_DATA_MAX	\
		(XPNET_MSG_SIZE - (u64)(&((struct xpnet_message *)0)->data))
#define XPNET_MSG_ALIGNED_SIZE	(L1_CACHE_ALIGN(XPNET_MSG_SIZE))
#define XPNET_MSG_NENTRIES	(PAGE_SIZE / XPNET_MSG_ALIGNED_SIZE)


#define XPNET_MAX_KTHREADS	(XPNET_MSG_NENTRIES + 1)
#define XPNET_MAX_IDLE_KTHREADS	(XPNET_MSG_NENTRIES + 1)

/*
 * Version number of XPNET implementation. XPNET can always talk to versions
 * with same major #, and never talk to versions with a different version.
 */
#define _XPNET_VERSION(_major, _minor)	(((_major) << 4) | (_minor))
#define XPNET_VERSION_MAJOR(_v)		((_v) >> 4)
#define XPNET_VERSION_MINOR(_v)		((_v) & 0xf)

#define	XPNET_VERSION _XPNET_VERSION(1,0)		/* version 1.0 */
#define	XPNET_VERSION_EMBED _XPNET_VERSION(1,1)		/* version 1.1 */
#define XPNET_MAGIC	0x88786984 /* "XNET" */

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
	struct list_head free_list;
	struct sk_buff *skb;
	atomic_t use_count;
};

/* driver specific structure pointed to by the device structure */
struct xpnet_dev_private {
	struct net_device_stats stats;
};

struct net_device *xpnet_device;

/*
 * When we are notified of other partitions activating, we add them to
 * our bitmask of partitions to which we broadcast.
 */
static u64 xpnet_broadcast_partitions;
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
/* 32KB has been determined to be the ideal */
#define XPNET_DEF_MTU (0x8000UL)


/*
 * The partition id is encapsulated in the MAC address.  The following
 * define locates the octet the partid is in.
 */
#define XPNET_PARTID_OCTET	1
#define XPNET_LICENSE_OCTET	2


/*
 * Define the XPNET debug device structure that is to be used with dev_dbg(),
 * dev_err(), dev_warn(), and dev_info().
 */
struct device_driver xpnet_dbg_name = {
	.name = "xpnet"
};

struct device xpnet_dbg_subname = {
	.bus_id = {0},			/* set to "" */
	.driver = &xpnet_dbg_name
};

struct device *xpnet = &xpnet_dbg_subname;

/*
 * Packet was recevied by XPC and forwarded to us.
 */
static void
xpnet_receive(partid_t partid, int channel, struct xpnet_message *msg)
{
	struct sk_buff *skb;
	bte_result_t bret;
	struct xpnet_dev_private *priv =
		(struct xpnet_dev_private *) xpnet_device->priv;


	if (!XPNET_VALID_MSG(msg)) {
		/*
		 * Packet with a different XPC version.  Ignore.
		 */
		xpc_received(partid, channel, (void *) msg);

		priv->stats.rx_errors++;

		return;
	}
	dev_dbg(xpnet, "received 0x%lx, %d, %d, %d\n", msg->buf_pa, msg->size,
		msg->leadin_ignore, msg->tailout_ignore);


	/* reserve an extra cache line */
	skb = dev_alloc_skb(msg->size + L1_CACHE_BYTES);
	if (!skb) {
		dev_err(xpnet, "failed on dev_alloc_skb(%d)\n",
			msg->size + L1_CACHE_BYTES);

		xpc_received(partid, channel, (void *) msg);

		priv->stats.rx_errors++;

		return;
	}

	/*
	 * The allocated skb has some reserved space.
	 * In order to use bte_copy, we need to get the
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
			(size_t) msg->embedded_bytes);

		skb_copy_to_linear_data(skb, &msg->data, (size_t)msg->embedded_bytes);
	} else {
		dev_dbg(xpnet, "transferring buffer to the skb->data area;\n\t"
			"bte_copy(0x%p, 0x%p, %hu)\n", (void *)msg->buf_pa,
			(void *)__pa((u64)skb->data & ~(L1_CACHE_BYTES - 1)),
			msg->size);

		bret = bte_copy(msg->buf_pa,
				__pa((u64)skb->data & ~(L1_CACHE_BYTES - 1)),
				msg->size, (BTE_NOTIFY | BTE_WACQUIRE), NULL);

		if (bret != BTE_SUCCESS) {
			// >>> Need better way of cleaning skb.  Currently skb
			// >>> appears in_use and we can't just call
			// >>> dev_kfree_skb.
			dev_err(xpnet, "bte_copy(0x%p, 0x%p, 0x%hx) returned "
				"error=0x%x\n", (void *)msg->buf_pa,
				(void *)__pa((u64)skb->data &
							~(L1_CACHE_BYTES - 1)),
				msg->size, bret);

			xpc_received(partid, channel, (void *) msg);

			priv->stats.rx_errors++;

			return;
		}
	}

	dev_dbg(xpnet, "<skb->head=0x%p skb->data=0x%p skb->tail=0x%p "
		"skb->end=0x%p skb->len=%d\n", (void *) skb->head,
		(void *)skb->data, skb_tail_pointer(skb), skb_end_pointer(skb),
		skb->len);

	skb->protocol = eth_type_trans(skb, xpnet_device);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	dev_dbg(xpnet, "passing skb to network layer; \n\tskb->head=0x%p "
		"skb->data=0x%p skb->tail=0x%p skb->end=0x%p skb->len=%d\n",
		(void *)skb->head, (void *)skb->data, skb_tail_pointer(skb),
		skb_end_pointer(skb), skb->len);


	xpnet_device->last_rx = jiffies;
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += skb->len + ETH_HLEN;

	netif_rx_ni(skb);
	xpc_received(partid, channel, (void *) msg);
}


/*
 * This is the handler which XPC calls during any sort of change in
 * state or message reception on a connection.
 */
static void
xpnet_connection_activity(enum xpc_retval reason, partid_t partid, int channel,
			  void *data, void *key)
{
	long bp;


	DBUG_ON(partid <= 0 || partid >= XP_MAX_PARTITIONS);
	DBUG_ON(channel != XPC_NET_CHANNEL);

	switch(reason) {
	case xpcMsgReceived:	/* message received */
		DBUG_ON(data == NULL);

		xpnet_receive(partid, channel, (struct xpnet_message *) data);
		break;

	case xpcConnected:	/* connection completed to a partition */
		spin_lock_bh(&xpnet_broadcast_lock);
		xpnet_broadcast_partitions |= 1UL << (partid -1 );
		bp = xpnet_broadcast_partitions;
		spin_unlock_bh(&xpnet_broadcast_lock);

		netif_carrier_on(xpnet_device);

		dev_dbg(xpnet, "%s connection created to partition %d; "
			"xpnet_broadcast_partitions=0x%lx\n",
			xpnet_device->name, partid, bp);
		break;

	default:
		spin_lock_bh(&xpnet_broadcast_lock);
		xpnet_broadcast_partitions &= ~(1UL << (partid -1 ));
		bp = xpnet_broadcast_partitions;
		spin_unlock_bh(&xpnet_broadcast_lock);

		if (bp == 0) {
			netif_carrier_off(xpnet_device);
		}

		dev_dbg(xpnet, "%s disconnected from partition %d; "
			"xpnet_broadcast_partitions=0x%lx\n",
			xpnet_device->name, partid, bp);
		break;

	}
}


static int
xpnet_dev_open(struct net_device *dev)
{
	enum xpc_retval ret;


	dev_dbg(xpnet, "calling xpc_connect(%d, 0x%p, NULL, %ld, %ld, %d, "
		"%d)\n", XPC_NET_CHANNEL, xpnet_connection_activity,
		XPNET_MSG_SIZE, XPNET_MSG_NENTRIES, XPNET_MAX_KTHREADS,
		XPNET_MAX_IDLE_KTHREADS);

	ret = xpc_connect(XPC_NET_CHANNEL, xpnet_connection_activity, NULL,
			  XPNET_MSG_SIZE, XPNET_MSG_NENTRIES,
			  XPNET_MAX_KTHREADS, XPNET_MAX_IDLE_KTHREADS);
	if (ret != xpcSuccess) {
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


static int
xpnet_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	/* 68 comes from min TCP+IP+MAC header */
	if ((new_mtu < 68) || (new_mtu > XPNET_MAX_MTU)) {
		dev_err(xpnet, "ifconfig %s mtu %d failed; value must be "
			"between 68 and %ld\n", dev->name, new_mtu,
			XPNET_MAX_MTU);
		return -EINVAL;
	}

	dev->mtu = new_mtu;
	dev_dbg(xpnet, "ifconfig %s mtu set to %d\n", dev->name, new_mtu);
	return 0;
}


/*
 * Required for the net_device structure.
 */
static int
xpnet_dev_set_config(struct net_device *dev, struct ifmap *new_map)
{
	return 0;
}


/*
 * Return statistics to the caller.
 */
static struct net_device_stats *
xpnet_dev_get_stats(struct net_device *dev)
{
	struct xpnet_dev_private *priv;


	priv = (struct xpnet_dev_private *) dev->priv;

	return &priv->stats;
}


/*
 * Notification that the other end has received the message and
 * DMA'd the skb information.  At this point, they are done with
 * our side.  When all recipients are done processing, we
 * release the skb and then release our pending message structure.
 */
static void
xpnet_send_completed(enum xpc_retval reason, partid_t partid, int channel,
			void *__qm)
{
	struct xpnet_pending_msg *queued_msg =
		(struct xpnet_pending_msg *) __qm;


	DBUG_ON(queued_msg == NULL);

	dev_dbg(xpnet, "message to %d notified with reason %d\n",
		partid, reason);

	if (atomic_dec_return(&queued_msg->use_count) == 0) {
		dev_dbg(xpnet, "all acks for skb->head=-x%p\n",
			(void *) queued_msg->skb->head);

		dev_kfree_skb_any(queued_msg->skb);
		kfree(queued_msg);
	}
}


/*
 * Network layer has formatted a packet (skb) and is ready to place it
 * "on the wire".  Prepare and send an xpnet_message to all partitions
 * which have connected with us and are targets of this packet.
 *
 * MAC-NOTE:  For the XPNET driver, the MAC address contains the
 * destination partition_id.  If the destination partition id word
 * is 0xff, this packet is to broadcast to all partitions.
 */
static int
xpnet_dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct xpnet_pending_msg *queued_msg;
	enum xpc_retval ret;
	struct xpnet_message *msg;
	u64 start_addr, end_addr;
	long dp;
	u8 second_mac_octet;
	partid_t dest_partid;
	struct xpnet_dev_private *priv;
	u16 embedded_bytes;


	priv = (struct xpnet_dev_private *) dev->priv;


	dev_dbg(xpnet, ">skb->head=0x%p skb->data=0x%p skb->tail=0x%p "
		"skb->end=0x%p skb->len=%d\n", (void *) skb->head,
		(void *)skb->data, skb_tail_pointer(skb), skb_end_pointer(skb),
		skb->len);


	/*
	 * The xpnet_pending_msg tracks how many outstanding
	 * xpc_send_notifies are relying on this skb.  When none
	 * remain, release the skb.
	 */
	queued_msg = kmalloc(sizeof(struct xpnet_pending_msg), GFP_ATOMIC);
	if (queued_msg == NULL) {
		dev_warn(xpnet, "failed to kmalloc %ld bytes; dropping "
			"packet\n", sizeof(struct xpnet_pending_msg));

		priv->stats.tx_errors++;

		return -ENOMEM;
	}


	/* get the beginning of the first cacheline and end of last */
	start_addr = ((u64) skb->data & ~(L1_CACHE_BYTES - 1));
	end_addr = L1_CACHE_ALIGN((u64)skb_tail_pointer(skb));

	/* calculate how many bytes to embed in the XPC message */
	embedded_bytes = 0;
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


	second_mac_octet = skb->data[XPNET_PARTID_OCTET];
	if (second_mac_octet == 0xff) {
		/* we are being asked to broadcast to all partitions */
		dp = xpnet_broadcast_partitions;
	} else if (second_mac_octet != 0) {
		dp = xpnet_broadcast_partitions &
					(1UL << (second_mac_octet - 1));
	} else {
		/* 0 is an invalid partid.  Ignore */
		dp = 0;
	}
	dev_dbg(xpnet, "destination Partitions mask (dp) = 0x%lx\n", dp);

	/*
	 * If we wanted to allow promiscous mode to work like an
	 * unswitched network, this would be a good point to OR in a
	 * mask of partitions which should be receiving all packets.
	 */

	/*
	 * Main send loop.
	 */
	for (dest_partid = 1; dp && dest_partid < XP_MAX_PARTITIONS;
	     dest_partid++) {


		if (!(dp & (1UL << (dest_partid - 1)))) {
			/* not destined for this partition */
			continue;
		}

		/* remove this partition from the destinations mask */
		dp &= ~(1UL << (dest_partid - 1));


		/* found a partition to send to */

		ret = xpc_allocate(dest_partid, XPC_NET_CHANNEL,
				   XPC_NOWAIT, (void **)&msg);
		if (unlikely(ret != xpcSuccess)) {
			continue;
		}

		msg->embedded_bytes = embedded_bytes;
		if (unlikely(embedded_bytes != 0)) {
			msg->version = XPNET_VERSION_EMBED;
			dev_dbg(xpnet, "calling memcpy(0x%p, 0x%p, 0x%lx)\n",
				&msg->data, skb->data, (size_t) embedded_bytes);
			skb_copy_from_linear_data(skb, &msg->data,
						  (size_t)embedded_bytes);
		} else {
			msg->version = XPNET_VERSION;
		}
		msg->magic = XPNET_MAGIC;
		msg->size = end_addr - start_addr;
		msg->leadin_ignore = (u64) skb->data - start_addr;
		msg->tailout_ignore = end_addr - (u64)skb_tail_pointer(skb);
		msg->buf_pa = __pa(start_addr);

		dev_dbg(xpnet, "sending XPC message to %d:%d\nmsg->buf_pa="
			"0x%lx, msg->size=%u, msg->leadin_ignore=%u, "
			"msg->tailout_ignore=%u\n", dest_partid,
			XPC_NET_CHANNEL, msg->buf_pa, msg->size,
			msg->leadin_ignore, msg->tailout_ignore);


		atomic_inc(&queued_msg->use_count);

		ret = xpc_send_notify(dest_partid, XPC_NET_CHANNEL, msg,
				      xpnet_send_completed, queued_msg);
		if (unlikely(ret != xpcSuccess)) {
			atomic_dec(&queued_msg->use_count);
			continue;
		}

	}

	if (atomic_dec_return(&queued_msg->use_count) == 0) {
		dev_dbg(xpnet, "no partitions to receive packet destined for "
			"%d\n", dest_partid);


		dev_kfree_skb(skb);
		kfree(queued_msg);
	}

	priv->stats.tx_packets++;
	priv->stats.tx_bytes += skb->len;

	return 0;
}


/*
 * Deal with transmit timeouts coming from the network layer.
 */
static void
xpnet_dev_tx_timeout (struct net_device *dev)
{
	struct xpnet_dev_private *priv;


	priv = (struct xpnet_dev_private *) dev->priv;

	priv->stats.tx_errors++;
	return;
}


static int __init
xpnet_init(void)
{
	int i;
	u32 license_num;
	int result = -ENOMEM;


	if (!ia64_platform_is("sn2")) {
		return -ENODEV;
	}

	dev_info(xpnet, "registering network device %s\n", XPNET_DEVICE_NAME);

	/*
	 * use ether_setup() to init the majority of our device
	 * structure and then override the necessary pieces.
	 */
	xpnet_device = alloc_netdev(sizeof(struct xpnet_dev_private),
				    XPNET_DEVICE_NAME, ether_setup);
	if (xpnet_device == NULL) {
		return -ENOMEM;
	}

	netif_carrier_off(xpnet_device);

	xpnet_device->mtu = XPNET_DEF_MTU;
	xpnet_device->change_mtu = xpnet_dev_change_mtu;
	xpnet_device->open = xpnet_dev_open;
	xpnet_device->get_stats = xpnet_dev_get_stats;
	xpnet_device->stop = xpnet_dev_stop;
	xpnet_device->hard_start_xmit = xpnet_dev_hard_start_xmit;
	xpnet_device->tx_timeout = xpnet_dev_tx_timeout;
	xpnet_device->set_config = xpnet_dev_set_config;

	/*
	 * Multicast assumes the LSB of the first octet is set for multicast
	 * MAC addresses.  We chose the first octet of the MAC to be unlikely
	 * to collide with any vendor's officially issued MAC.
	 */
	xpnet_device->dev_addr[0] = 0xfe;
	xpnet_device->dev_addr[XPNET_PARTID_OCTET] = sn_partition_id;
	license_num = sn_partition_serial_number_val();
	for (i = 3; i >= 0; i--) {
		xpnet_device->dev_addr[XPNET_LICENSE_OCTET + i] =
							license_num & 0xff;
		license_num = license_num >> 8;
	}

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
	xpnet_device->features = NETIF_F_NO_CSUM;

	result = register_netdev(xpnet_device);
	if (result != 0) {
		free_netdev(xpnet_device);
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
}
module_exit(xpnet_exit);


MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("Cross Partition Network adapter (XPNET)");
MODULE_LICENSE("GPL");

