// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 - 2019 Cambridge Greys Limited
 * Copyright (C) 2011 - 2014 Cisco Systems Inc
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org) and
 * James Leu (jleu@mindspring.net).
 * Copyright (C) 2001 by various other people who didn't put their name here.
 */

#include <linux/version.h>
#include <linux/memblock.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <uapi/linux/filter.h>
#include <init.h>
#include <irq_kern.h>
#include <irq_user.h>
#include <net_kern.h>
#include <os.h>
#include "mconsole_kern.h"
#include "vector_user.h"
#include "vector_kern.h"

/*
 * Adapted from network devices with the following major changes:
 * All transports are static - simplifies the code significantly
 * Multiple FDs/IRQs per device
 * Vector IO optionally used for read/write, falling back to legacy
 * based on configuration and/or availability
 * Configuration is no longer positional - L2TPv3 and GRE require up to
 * 10 parameters, passing this as positional is not fit for purpose.
 * Only socket transports are supported
 */


#define DRIVER_NAME "uml-vector"
struct vector_cmd_line_arg {
	struct list_head list;
	int unit;
	char *arguments;
};

struct vector_device {
	struct list_head list;
	struct net_device *dev;
	struct platform_device pdev;
	int unit;
	int opened;
};

static LIST_HEAD(vec_cmd_line);

static DEFINE_SPINLOCK(vector_devices_lock);
static LIST_HEAD(vector_devices);

static int driver_registered;

static void vector_eth_configure(int n, struct arglist *def);

/* Argument accessors to set variables (and/or set default values)
 * mtu, buffer sizing, default headroom, etc
 */

#define DEFAULT_HEADROOM 2
#define SAFETY_MARGIN 32
#define DEFAULT_VECTOR_SIZE 64
#define TX_SMALL_PACKET 128
#define MAX_IOV_SIZE (MAX_SKB_FRAGS + 1)
#define MAX_ITERATIONS 64

static const struct {
	const char string[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "rx_queue_max" },
	{ "rx_queue_running_average" },
	{ "tx_queue_max" },
	{ "tx_queue_running_average" },
	{ "rx_encaps_errors" },
	{ "tx_timeout_count" },
	{ "tx_restart_queue" },
	{ "tx_kicks" },
	{ "tx_flow_control_xon" },
	{ "tx_flow_control_xoff" },
	{ "rx_csum_offload_good" },
	{ "rx_csum_offload_errors"},
	{ "sg_ok"},
	{ "sg_linearized"},
};

#define VECTOR_NUM_STATS	ARRAY_SIZE(ethtool_stats_keys)

static void vector_reset_stats(struct vector_private *vp)
{
	vp->estats.rx_queue_max = 0;
	vp->estats.rx_queue_running_average = 0;
	vp->estats.tx_queue_max = 0;
	vp->estats.tx_queue_running_average = 0;
	vp->estats.rx_encaps_errors = 0;
	vp->estats.tx_timeout_count = 0;
	vp->estats.tx_restart_queue = 0;
	vp->estats.tx_kicks = 0;
	vp->estats.tx_flow_control_xon = 0;
	vp->estats.tx_flow_control_xoff = 0;
	vp->estats.sg_ok = 0;
	vp->estats.sg_linearized = 0;
}

static int get_mtu(struct arglist *def)
{
	char *mtu = uml_vector_fetch_arg(def, "mtu");
	long result;

	if (mtu != NULL) {
		if (kstrtoul(mtu, 10, &result) == 0)
			if ((result < (1 << 16) - 1) && (result >= 576))
				return result;
	}
	return ETH_MAX_PACKET;
}

static char *get_bpf_file(struct arglist *def)
{
	return uml_vector_fetch_arg(def, "bpffile");
}

static bool get_bpf_flash(struct arglist *def)
{
	char *allow = uml_vector_fetch_arg(def, "bpfflash");
	long result;

	if (allow != NULL) {
		if (kstrtoul(allow, 10, &result) == 0)
			return (allow > 0);
	}
	return false;
}

static int get_depth(struct arglist *def)
{
	char *mtu = uml_vector_fetch_arg(def, "depth");
	long result;

	if (mtu != NULL) {
		if (kstrtoul(mtu, 10, &result) == 0)
			return result;
	}
	return DEFAULT_VECTOR_SIZE;
}

static int get_headroom(struct arglist *def)
{
	char *mtu = uml_vector_fetch_arg(def, "headroom");
	long result;

	if (mtu != NULL) {
		if (kstrtoul(mtu, 10, &result) == 0)
			return result;
	}
	return DEFAULT_HEADROOM;
}

static int get_req_size(struct arglist *def)
{
	char *gro = uml_vector_fetch_arg(def, "gro");
	long result;

	if (gro != NULL) {
		if (kstrtoul(gro, 10, &result) == 0) {
			if (result > 0)
				return 65536;
		}
	}
	return get_mtu(def) + ETH_HEADER_OTHER +
		get_headroom(def) + SAFETY_MARGIN;
}


static int get_transport_options(struct arglist *def)
{
	char *transport = uml_vector_fetch_arg(def, "transport");
	char *vector = uml_vector_fetch_arg(def, "vec");

	int vec_rx = VECTOR_RX;
	int vec_tx = VECTOR_TX;
	long parsed;
	int result = 0;

	if (transport == NULL)
		return -EINVAL;

	if (vector != NULL) {
		if (kstrtoul(vector, 10, &parsed) == 0) {
			if (parsed == 0) {
				vec_rx = 0;
				vec_tx = 0;
			}
		}
	}

	if (get_bpf_flash(def))
		result = VECTOR_BPF_FLASH;

	if (strncmp(transport, TRANS_TAP, TRANS_TAP_LEN) == 0)
		return result;
	if (strncmp(transport, TRANS_HYBRID, TRANS_HYBRID_LEN) == 0)
		return (result | vec_rx | VECTOR_BPF);
	if (strncmp(transport, TRANS_RAW, TRANS_RAW_LEN) == 0)
		return (result | vec_rx | vec_tx | VECTOR_QDISC_BYPASS);
	return (result | vec_rx | vec_tx);
}


/* A mini-buffer for packet drop read
 * All of our supported transports are datagram oriented and we always
 * read using recvmsg or recvmmsg. If we pass a buffer which is smaller
 * than the packet size it still counts as full packet read and will
 * clean the incoming stream to keep sigio/epoll happy
 */

#define DROP_BUFFER_SIZE 32

static char *drop_buffer;

/* Array backed queues optimized for bulk enqueue/dequeue and
 * 1:N (small values of N) or 1:1 enqueuer/dequeuer ratios.
 * For more details and full design rationale see
 * http://foswiki.cambridgegreys.com/Main/EatYourTailAndEnjoyIt
 */


/*
 * Advance the mmsg queue head by n = advance. Resets the queue to
 * maximum enqueue/dequeue-at-once capacity if possible. Called by
 * dequeuers. Caller must hold the head_lock!
 */

static int vector_advancehead(struct vector_queue *qi, int advance)
{
	int queue_depth;

	qi->head =
		(qi->head + advance)
			% qi->max_depth;


	spin_lock(&qi->tail_lock);
	qi->queue_depth -= advance;

	/* we are at 0, use this to
	 * reset head and tail so we can use max size vectors
	 */

	if (qi->queue_depth == 0) {
		qi->head = 0;
		qi->tail = 0;
	}
	queue_depth = qi->queue_depth;
	spin_unlock(&qi->tail_lock);
	return queue_depth;
}

/*	Advance the queue tail by n = advance.
 *	This is called by enqueuers which should hold the
 *	head lock already
 */

static int vector_advancetail(struct vector_queue *qi, int advance)
{
	int queue_depth;

	qi->tail =
		(qi->tail + advance)
			% qi->max_depth;
	spin_lock(&qi->head_lock);
	qi->queue_depth += advance;
	queue_depth = qi->queue_depth;
	spin_unlock(&qi->head_lock);
	return queue_depth;
}

static int prep_msg(struct vector_private *vp,
	struct sk_buff *skb,
	struct iovec *iov)
{
	int iov_index = 0;
	int nr_frags, frag;
	skb_frag_t *skb_frag;

	nr_frags = skb_shinfo(skb)->nr_frags;
	if (nr_frags > MAX_IOV_SIZE) {
		if (skb_linearize(skb) != 0)
			goto drop;
	}
	if (vp->header_size > 0) {
		iov[iov_index].iov_len = vp->header_size;
		vp->form_header(iov[iov_index].iov_base, skb, vp);
		iov_index++;
	}
	iov[iov_index].iov_base = skb->data;
	if (nr_frags > 0) {
		iov[iov_index].iov_len = skb->len - skb->data_len;
		vp->estats.sg_ok++;
	} else
		iov[iov_index].iov_len = skb->len;
	iov_index++;
	for (frag = 0; frag < nr_frags; frag++) {
		skb_frag = &skb_shinfo(skb)->frags[frag];
		iov[iov_index].iov_base = skb_frag_address_safe(skb_frag);
		iov[iov_index].iov_len = skb_frag_size(skb_frag);
		iov_index++;
	}
	return iov_index;
drop:
	return -1;
}
/*
 * Generic vector enqueue with support for forming headers using transport
 * specific callback. Allows GRE, L2TPv3, RAW and other transports
 * to use a common enqueue procedure in vector mode
 */

static int vector_enqueue(struct vector_queue *qi, struct sk_buff *skb)
{
	struct vector_private *vp = netdev_priv(qi->dev);
	int queue_depth;
	int packet_len;
	struct mmsghdr *mmsg_vector = qi->mmsg_vector;
	int iov_count;

	spin_lock(&qi->tail_lock);
	spin_lock(&qi->head_lock);
	queue_depth = qi->queue_depth;
	spin_unlock(&qi->head_lock);

	if (skb)
		packet_len = skb->len;

	if (queue_depth < qi->max_depth) {

		*(qi->skbuff_vector + qi->tail) = skb;
		mmsg_vector += qi->tail;
		iov_count = prep_msg(
			vp,
			skb,
			mmsg_vector->msg_hdr.msg_iov
		);
		if (iov_count < 1)
			goto drop;
		mmsg_vector->msg_hdr.msg_iovlen = iov_count;
		mmsg_vector->msg_hdr.msg_name = vp->fds->remote_addr;
		mmsg_vector->msg_hdr.msg_namelen = vp->fds->remote_addr_size;
		queue_depth = vector_advancetail(qi, 1);
	} else
		goto drop;
	spin_unlock(&qi->tail_lock);
	return queue_depth;
drop:
	qi->dev->stats.tx_dropped++;
	if (skb != NULL) {
		packet_len = skb->len;
		dev_consume_skb_any(skb);
		netdev_completed_queue(qi->dev, 1, packet_len);
	}
	spin_unlock(&qi->tail_lock);
	return queue_depth;
}

static int consume_vector_skbs(struct vector_queue *qi, int count)
{
	struct sk_buff *skb;
	int skb_index;
	int bytes_compl = 0;

	for (skb_index = qi->head; skb_index < qi->head + count; skb_index++) {
		skb = *(qi->skbuff_vector + skb_index);
		/* mark as empty to ensure correct destruction if
		 * needed
		 */
		bytes_compl += skb->len;
		*(qi->skbuff_vector + skb_index) = NULL;
		dev_consume_skb_any(skb);
	}
	qi->dev->stats.tx_bytes += bytes_compl;
	qi->dev->stats.tx_packets += count;
	netdev_completed_queue(qi->dev, count, bytes_compl);
	return vector_advancehead(qi, count);
}

/*
 * Generic vector deque via sendmmsg with support for forming headers
 * using transport specific callback. Allows GRE, L2TPv3, RAW and
 * other transports to use a common dequeue procedure in vector mode
 */


static int vector_send(struct vector_queue *qi)
{
	struct vector_private *vp = netdev_priv(qi->dev);
	struct mmsghdr *send_from;
	int result = 0, send_len, queue_depth = qi->max_depth;

	if (spin_trylock(&qi->head_lock)) {
		if (spin_trylock(&qi->tail_lock)) {
			/* update queue_depth to current value */
			queue_depth = qi->queue_depth;
			spin_unlock(&qi->tail_lock);
			while (queue_depth > 0) {
				/* Calculate the start of the vector */
				send_len = queue_depth;
				send_from = qi->mmsg_vector;
				send_from += qi->head;
				/* Adjust vector size if wraparound */
				if (send_len + qi->head > qi->max_depth)
					send_len = qi->max_depth - qi->head;
				/* Try to TX as many packets as possible */
				if (send_len > 0) {
					result = uml_vector_sendmmsg(
						 vp->fds->tx_fd,
						 send_from,
						 send_len,
						 0
					);
					vp->in_write_poll =
						(result != send_len);
				}
				/* For some of the sendmmsg error scenarios
				 * we may end being unsure in the TX success
				 * for all packets. It is safer to declare
				 * them all TX-ed and blame the network.
				 */
				if (result < 0) {
					if (net_ratelimit())
						netdev_err(vp->dev, "sendmmsg err=%i\n",
							result);
					vp->in_error = true;
					result = send_len;
				}
				if (result > 0) {
					queue_depth =
						consume_vector_skbs(qi, result);
					/* This is equivalent to an TX IRQ.
					 * Restart the upper layers to feed us
					 * more packets.
					 */
					if (result > vp->estats.tx_queue_max)
						vp->estats.tx_queue_max = result;
					vp->estats.tx_queue_running_average =
						(vp->estats.tx_queue_running_average + result) >> 1;
				}
				netif_trans_update(qi->dev);
				netif_wake_queue(qi->dev);
				/* if TX is busy, break out of the send loop,
				 *  poll write IRQ will reschedule xmit for us
				 */
				if (result != send_len) {
					vp->estats.tx_restart_queue++;
					break;
				}
			}
		}
		spin_unlock(&qi->head_lock);
	} else {
		tasklet_schedule(&vp->tx_poll);
	}
	return queue_depth;
}

/* Queue destructor. Deliberately stateless so we can use
 * it in queue cleanup if initialization fails.
 */

static void destroy_queue(struct vector_queue *qi)
{
	int i;
	struct iovec *iov;
	struct vector_private *vp = netdev_priv(qi->dev);
	struct mmsghdr *mmsg_vector;

	if (qi == NULL)
		return;
	/* deallocate any skbuffs - we rely on any unused to be
	 * set to NULL.
	 */
	if (qi->skbuff_vector != NULL) {
		for (i = 0; i < qi->max_depth; i++) {
			if (*(qi->skbuff_vector + i) != NULL)
				dev_kfree_skb_any(*(qi->skbuff_vector + i));
		}
		kfree(qi->skbuff_vector);
	}
	/* deallocate matching IOV structures including header buffs */
	if (qi->mmsg_vector != NULL) {
		mmsg_vector = qi->mmsg_vector;
		for (i = 0; i < qi->max_depth; i++) {
			iov = mmsg_vector->msg_hdr.msg_iov;
			if (iov != NULL) {
				if ((vp->header_size > 0) &&
					(iov->iov_base != NULL))
					kfree(iov->iov_base);
				kfree(iov);
			}
			mmsg_vector++;
		}
		kfree(qi->mmsg_vector);
	}
	kfree(qi);
}

/*
 * Queue constructor. Create a queue with a given side.
 */
static struct vector_queue *create_queue(
	struct vector_private *vp,
	int max_size,
	int header_size,
	int num_extra_frags)
{
	struct vector_queue *result;
	int i;
	struct iovec *iov;
	struct mmsghdr *mmsg_vector;

	result = kmalloc(sizeof(struct vector_queue), GFP_KERNEL);
	if (result == NULL)
		return NULL;
	result->max_depth = max_size;
	result->dev = vp->dev;
	result->mmsg_vector = kmalloc(
		(sizeof(struct mmsghdr) * max_size), GFP_KERNEL);
	if (result->mmsg_vector == NULL)
		goto out_mmsg_fail;
	result->skbuff_vector = kmalloc(
		(sizeof(void *) * max_size), GFP_KERNEL);
	if (result->skbuff_vector == NULL)
		goto out_skb_fail;

	/* further failures can be handled safely by destroy_queue*/

	mmsg_vector = result->mmsg_vector;
	for (i = 0; i < max_size; i++) {
		/* Clear all pointers - we use non-NULL as marking on
		 * what to free on destruction
		 */
		*(result->skbuff_vector + i) = NULL;
		mmsg_vector->msg_hdr.msg_iov = NULL;
		mmsg_vector++;
	}
	mmsg_vector = result->mmsg_vector;
	result->max_iov_frags = num_extra_frags;
	for (i = 0; i < max_size; i++) {
		if (vp->header_size > 0)
			iov = kmalloc_array(3 + num_extra_frags,
					    sizeof(struct iovec),
					    GFP_KERNEL
			);
		else
			iov = kmalloc_array(2 + num_extra_frags,
					    sizeof(struct iovec),
					    GFP_KERNEL
			);
		if (iov == NULL)
			goto out_fail;
		mmsg_vector->msg_hdr.msg_iov = iov;
		mmsg_vector->msg_hdr.msg_iovlen = 1;
		mmsg_vector->msg_hdr.msg_control = NULL;
		mmsg_vector->msg_hdr.msg_controllen = 0;
		mmsg_vector->msg_hdr.msg_flags = MSG_DONTWAIT;
		mmsg_vector->msg_hdr.msg_name = NULL;
		mmsg_vector->msg_hdr.msg_namelen = 0;
		if (vp->header_size > 0) {
			iov->iov_base = kmalloc(header_size, GFP_KERNEL);
			if (iov->iov_base == NULL)
				goto out_fail;
			iov->iov_len = header_size;
			mmsg_vector->msg_hdr.msg_iovlen = 2;
			iov++;
		}
		iov->iov_base = NULL;
		iov->iov_len = 0;
		mmsg_vector++;
	}
	spin_lock_init(&result->head_lock);
	spin_lock_init(&result->tail_lock);
	result->queue_depth = 0;
	result->head = 0;
	result->tail = 0;
	return result;
out_skb_fail:
	kfree(result->mmsg_vector);
out_mmsg_fail:
	kfree(result);
	return NULL;
out_fail:
	destroy_queue(result);
	return NULL;
}

/*
 * We do not use the RX queue as a proper wraparound queue for now
 * This is not necessary because the consumption via netif_rx()
 * happens in-line. While we can try using the return code of
 * netif_rx() for flow control there are no drivers doing this today.
 * For this RX specific use we ignore the tail/head locks and
 * just read into a prepared queue filled with skbuffs.
 */

static struct sk_buff *prep_skb(
	struct vector_private *vp,
	struct user_msghdr *msg)
{
	int linear = vp->max_packet + vp->headroom + SAFETY_MARGIN;
	struct sk_buff *result;
	int iov_index = 0, len;
	struct iovec *iov = msg->msg_iov;
	int err, nr_frags, frag;
	skb_frag_t *skb_frag;

	if (vp->req_size <= linear)
		len = linear;
	else
		len = vp->req_size;
	result = alloc_skb_with_frags(
		linear,
		len - vp->max_packet,
		3,
		&err,
		GFP_ATOMIC
	);
	if (vp->header_size > 0)
		iov_index++;
	if (result == NULL) {
		iov[iov_index].iov_base = NULL;
		iov[iov_index].iov_len = 0;
		goto done;
	}
	skb_reserve(result, vp->headroom);
	result->dev = vp->dev;
	skb_put(result, vp->max_packet);
	result->data_len = len - vp->max_packet;
	result->len += len - vp->max_packet;
	skb_reset_mac_header(result);
	result->ip_summed = CHECKSUM_NONE;
	iov[iov_index].iov_base = result->data;
	iov[iov_index].iov_len = vp->max_packet;
	iov_index++;

	nr_frags = skb_shinfo(result)->nr_frags;
	for (frag = 0; frag < nr_frags; frag++) {
		skb_frag = &skb_shinfo(result)->frags[frag];
		iov[iov_index].iov_base = skb_frag_address_safe(skb_frag);
		if (iov[iov_index].iov_base != NULL)
			iov[iov_index].iov_len = skb_frag_size(skb_frag);
		else
			iov[iov_index].iov_len = 0;
		iov_index++;
	}
done:
	msg->msg_iovlen = iov_index;
	return result;
}


/* Prepare queue for recvmmsg one-shot rx - fill with fresh sk_buffs*/

static void prep_queue_for_rx(struct vector_queue *qi)
{
	struct vector_private *vp = netdev_priv(qi->dev);
	struct mmsghdr *mmsg_vector = qi->mmsg_vector;
	void **skbuff_vector = qi->skbuff_vector;
	int i;

	if (qi->queue_depth == 0)
		return;
	for (i = 0; i < qi->queue_depth; i++) {
		/* it is OK if allocation fails - recvmmsg with NULL data in
		 * iov argument still performs an RX, just drops the packet
		 * This allows us stop faffing around with a "drop buffer"
		 */

		*skbuff_vector = prep_skb(vp, &mmsg_vector->msg_hdr);
		skbuff_vector++;
		mmsg_vector++;
	}
	qi->queue_depth = 0;
}

static struct vector_device *find_device(int n)
{
	struct vector_device *device;
	struct list_head *ele;

	spin_lock(&vector_devices_lock);
	list_for_each(ele, &vector_devices) {
		device = list_entry(ele, struct vector_device, list);
		if (device->unit == n)
			goto out;
	}
	device = NULL;
 out:
	spin_unlock(&vector_devices_lock);
	return device;
}

static int vector_parse(char *str, int *index_out, char **str_out,
			char **error_out)
{
	int n, len, err;
	char *start = str;

	len = strlen(str);

	while ((*str != ':') && (strlen(str) > 1))
		str++;
	if (*str != ':') {
		*error_out = "Expected ':' after device number";
		return -EINVAL;
	}
	*str = '\0';

	err = kstrtouint(start, 0, &n);
	if (err < 0) {
		*error_out = "Bad device number";
		return err;
	}

	str++;
	if (find_device(n)) {
		*error_out = "Device already configured";
		return -EINVAL;
	}

	*index_out = n;
	*str_out = str;
	return 0;
}

static int vector_config(char *str, char **error_out)
{
	int err, n;
	char *params;
	struct arglist *parsed;

	err = vector_parse(str, &n, &params, error_out);
	if (err != 0)
		return err;

	/* This string is broken up and the pieces used by the underlying
	 * driver. We should copy it to make sure things do not go wrong
	 * later.
	 */

	params = kstrdup(params, GFP_KERNEL);
	if (params == NULL) {
		*error_out = "vector_config failed to strdup string";
		return -ENOMEM;
	}

	parsed = uml_parse_vector_ifspec(params);

	if (parsed == NULL) {
		*error_out = "vector_config failed to parse parameters";
		return -EINVAL;
	}

	vector_eth_configure(n, parsed);
	return 0;
}

static int vector_id(char **str, int *start_out, int *end_out)
{
	char *end;
	int n;

	n = simple_strtoul(*str, &end, 0);
	if ((*end != '\0') || (end == *str))
		return -1;

	*start_out = n;
	*end_out = n;
	*str = end;
	return n;
}

static int vector_remove(int n, char **error_out)
{
	struct vector_device *vec_d;
	struct net_device *dev;
	struct vector_private *vp;

	vec_d = find_device(n);
	if (vec_d == NULL)
		return -ENODEV;
	dev = vec_d->dev;
	vp = netdev_priv(dev);
	if (vp->fds != NULL)
		return -EBUSY;
	unregister_netdev(dev);
	platform_device_unregister(&vec_d->pdev);
	return 0;
}

/*
 * There is no shared per-transport initialization code, so
 * we will just initialize each interface one by one and
 * add them to a list
 */

static struct platform_driver uml_net_driver = {
	.driver = {
		.name = DRIVER_NAME,
	},
};


static void vector_device_release(struct device *dev)
{
	struct vector_device *device = dev_get_drvdata(dev);
	struct net_device *netdev = device->dev;

	list_del(&device->list);
	kfree(device);
	free_netdev(netdev);
}

/* Bog standard recv using recvmsg - not used normally unless the user
 * explicitly specifies not to use recvmmsg vector RX.
 */

static int vector_legacy_rx(struct vector_private *vp)
{
	int pkt_len;
	struct user_msghdr hdr;
	struct iovec iov[2 + MAX_IOV_SIZE]; /* header + data use case only */
	int iovpos = 0;
	struct sk_buff *skb;
	int header_check;

	hdr.msg_name = NULL;
	hdr.msg_namelen = 0;
	hdr.msg_iov = (struct iovec *) &iov;
	hdr.msg_control = NULL;
	hdr.msg_controllen = 0;
	hdr.msg_flags = 0;

	if (vp->header_size > 0) {
		iov[0].iov_base = vp->header_rxbuffer;
		iov[0].iov_len = vp->header_size;
	}

	skb = prep_skb(vp, &hdr);

	if (skb == NULL) {
		/* Read a packet into drop_buffer and don't do
		 * anything with it.
		 */
		iov[iovpos].iov_base = drop_buffer;
		iov[iovpos].iov_len = DROP_BUFFER_SIZE;
		hdr.msg_iovlen = 1;
		vp->dev->stats.rx_dropped++;
	}

	pkt_len = uml_vector_recvmsg(vp->fds->rx_fd, &hdr, 0);
	if (pkt_len < 0) {
		vp->in_error = true;
		return pkt_len;
	}

	if (skb != NULL) {
		if (pkt_len > vp->header_size) {
			if (vp->header_size > 0) {
				header_check = vp->verify_header(
					vp->header_rxbuffer, skb, vp);
				if (header_check < 0) {
					dev_kfree_skb_irq(skb);
					vp->dev->stats.rx_dropped++;
					vp->estats.rx_encaps_errors++;
					return 0;
				}
				if (header_check > 0) {
					vp->estats.rx_csum_offload_good++;
					skb->ip_summed = CHECKSUM_UNNECESSARY;
				}
			}
			pskb_trim(skb, pkt_len - vp->rx_header_size);
			skb->protocol = eth_type_trans(skb, skb->dev);
			vp->dev->stats.rx_bytes += skb->len;
			vp->dev->stats.rx_packets++;
			netif_rx(skb);
		} else {
			dev_kfree_skb_irq(skb);
		}
	}
	return pkt_len;
}

/*
 * Packet at a time TX which falls back to vector TX if the
 * underlying transport is busy.
 */



static int writev_tx(struct vector_private *vp, struct sk_buff *skb)
{
	struct iovec iov[3 + MAX_IOV_SIZE];
	int iov_count, pkt_len = 0;

	iov[0].iov_base = vp->header_txbuffer;
	iov_count = prep_msg(vp, skb, (struct iovec *) &iov);

	if (iov_count < 1)
		goto drop;

	pkt_len = uml_vector_writev(
		vp->fds->tx_fd,
		(struct iovec *) &iov,
		iov_count
	);

	if (pkt_len < 0)
		goto drop;

	netif_trans_update(vp->dev);
	netif_wake_queue(vp->dev);

	if (pkt_len > 0) {
		vp->dev->stats.tx_bytes += skb->len;
		vp->dev->stats.tx_packets++;
	} else {
		vp->dev->stats.tx_dropped++;
	}
	consume_skb(skb);
	return pkt_len;
drop:
	vp->dev->stats.tx_dropped++;
	consume_skb(skb);
	if (pkt_len < 0)
		vp->in_error = true;
	return pkt_len;
}

/*
 * Receive as many messages as we can in one call using the special
 * mmsg vector matched to an skb vector which we prepared earlier.
 */

static int vector_mmsg_rx(struct vector_private *vp)
{
	int packet_count, i;
	struct vector_queue *qi = vp->rx_queue;
	struct sk_buff *skb;
	struct mmsghdr *mmsg_vector = qi->mmsg_vector;
	void **skbuff_vector = qi->skbuff_vector;
	int header_check;

	/* Refresh the vector and make sure it is with new skbs and the
	 * iovs are updated to point to them.
	 */

	prep_queue_for_rx(qi);

	/* Fire the Lazy Gun - get as many packets as we can in one go. */

	packet_count = uml_vector_recvmmsg(
		vp->fds->rx_fd, qi->mmsg_vector, qi->max_depth, 0);

	if (packet_count < 0)
		vp->in_error = true;

	if (packet_count <= 0)
		return packet_count;

	/* We treat packet processing as enqueue, buffer refresh as dequeue
	 * The queue_depth tells us how many buffers have been used and how
	 * many do we need to prep the next time prep_queue_for_rx() is called.
	 */

	qi->queue_depth = packet_count;

	for (i = 0; i < packet_count; i++) {
		skb = (*skbuff_vector);
		if (mmsg_vector->msg_len > vp->header_size) {
			if (vp->header_size > 0) {
				header_check = vp->verify_header(
					mmsg_vector->msg_hdr.msg_iov->iov_base,
					skb,
					vp
				);
				if (header_check < 0) {
				/* Overlay header failed to verify - discard.
				 * We can actually keep this skb and reuse it,
				 * but that will make the prep logic too
				 * complex.
				 */
					dev_kfree_skb_irq(skb);
					vp->estats.rx_encaps_errors++;
					continue;
				}
				if (header_check > 0) {
					vp->estats.rx_csum_offload_good++;
					skb->ip_summed = CHECKSUM_UNNECESSARY;
				}
			}
			pskb_trim(skb,
				mmsg_vector->msg_len - vp->rx_header_size);
			skb->protocol = eth_type_trans(skb, skb->dev);
			/*
			 * We do not need to lock on updating stats here
			 * The interrupt loop is non-reentrant.
			 */
			vp->dev->stats.rx_bytes += skb->len;
			vp->dev->stats.rx_packets++;
			netif_rx(skb);
		} else {
			/* Overlay header too short to do anything - discard.
			 * We can actually keep this skb and reuse it,
			 * but that will make the prep logic too complex.
			 */
			if (skb != NULL)
				dev_kfree_skb_irq(skb);
		}
		(*skbuff_vector) = NULL;
		/* Move to the next buffer element */
		mmsg_vector++;
		skbuff_vector++;
	}
	if (packet_count > 0) {
		if (vp->estats.rx_queue_max < packet_count)
			vp->estats.rx_queue_max = packet_count;
		vp->estats.rx_queue_running_average =
			(vp->estats.rx_queue_running_average + packet_count) >> 1;
	}
	return packet_count;
}

static void vector_rx(struct vector_private *vp)
{
	int err;
	int iter = 0;

	if ((vp->options & VECTOR_RX) > 0)
		while (((err = vector_mmsg_rx(vp)) > 0) && (iter < MAX_ITERATIONS))
			iter++;
	else
		while (((err = vector_legacy_rx(vp)) > 0) && (iter < MAX_ITERATIONS))
			iter++;
	if ((err != 0) && net_ratelimit())
		netdev_err(vp->dev, "vector_rx: error(%d)\n", err);
	if (iter == MAX_ITERATIONS)
		netdev_err(vp->dev, "vector_rx: device stuck, remote end may have closed the connection\n");
}

static int vector_net_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct vector_private *vp = netdev_priv(dev);
	int queue_depth = 0;

	if (vp->in_error) {
		deactivate_fd(vp->fds->rx_fd, vp->rx_irq);
		if ((vp->fds->rx_fd != vp->fds->tx_fd) && (vp->tx_irq != 0))
			deactivate_fd(vp->fds->tx_fd, vp->tx_irq);
		return NETDEV_TX_BUSY;
	}

	if ((vp->options & VECTOR_TX) == 0) {
		writev_tx(vp, skb);
		return NETDEV_TX_OK;
	}

	/* We do BQL only in the vector path, no point doing it in
	 * packet at a time mode as there is no device queue
	 */

	netdev_sent_queue(vp->dev, skb->len);
	queue_depth = vector_enqueue(vp->tx_queue, skb);

	/* if the device queue is full, stop the upper layers and
	 * flush it.
	 */

	if (queue_depth >= vp->tx_queue->max_depth - 1) {
		vp->estats.tx_kicks++;
		netif_stop_queue(dev);
		vector_send(vp->tx_queue);
		return NETDEV_TX_OK;
	}
	if (netdev_xmit_more()) {
		mod_timer(&vp->tl, vp->coalesce);
		return NETDEV_TX_OK;
	}
	if (skb->len < TX_SMALL_PACKET) {
		vp->estats.tx_kicks++;
		vector_send(vp->tx_queue);
	} else
		tasklet_schedule(&vp->tx_poll);
	return NETDEV_TX_OK;
}

static irqreturn_t vector_rx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct vector_private *vp = netdev_priv(dev);

	if (!netif_running(dev))
		return IRQ_NONE;
	vector_rx(vp);
	return IRQ_HANDLED;

}

static irqreturn_t vector_tx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct vector_private *vp = netdev_priv(dev);

	if (!netif_running(dev))
		return IRQ_NONE;
	/* We need to pay attention to it only if we got
	 * -EAGAIN or -ENOBUFFS from sendmmsg. Otherwise
	 * we ignore it. In the future, it may be worth
	 * it to improve the IRQ controller a bit to make
	 * tweaking the IRQ mask less costly
	 */

	if (vp->in_write_poll)
		tasklet_schedule(&vp->tx_poll);
	return IRQ_HANDLED;

}

static int irq_rr;

static int vector_net_close(struct net_device *dev)
{
	struct vector_private *vp = netdev_priv(dev);
	unsigned long flags;

	netif_stop_queue(dev);
	del_timer(&vp->tl);

	if (vp->fds == NULL)
		return 0;

	/* Disable and free all IRQS */
	if (vp->rx_irq > 0) {
		um_free_irq(vp->rx_irq, dev);
		vp->rx_irq = 0;
	}
	if (vp->tx_irq > 0) {
		um_free_irq(vp->tx_irq, dev);
		vp->tx_irq = 0;
	}
	tasklet_kill(&vp->tx_poll);
	if (vp->fds->rx_fd > 0) {
		if (vp->bpf)
			uml_vector_detach_bpf(vp->fds->rx_fd, vp->bpf);
		os_close_file(vp->fds->rx_fd);
		vp->fds->rx_fd = -1;
	}
	if (vp->fds->tx_fd > 0) {
		os_close_file(vp->fds->tx_fd);
		vp->fds->tx_fd = -1;
	}
	if (vp->bpf != NULL)
		kfree(vp->bpf->filter);
	kfree(vp->bpf);
	vp->bpf = NULL;
	kfree(vp->fds->remote_addr);
	kfree(vp->transport_data);
	kfree(vp->header_rxbuffer);
	kfree(vp->header_txbuffer);
	if (vp->rx_queue != NULL)
		destroy_queue(vp->rx_queue);
	if (vp->tx_queue != NULL)
		destroy_queue(vp->tx_queue);
	kfree(vp->fds);
	vp->fds = NULL;
	spin_lock_irqsave(&vp->lock, flags);
	vp->opened = false;
	vp->in_error = false;
	spin_unlock_irqrestore(&vp->lock, flags);
	return 0;
}

/* TX tasklet */

static void vector_tx_poll(unsigned long data)
{
	struct vector_private *vp = (struct vector_private *)data;

	vp->estats.tx_kicks++;
	vector_send(vp->tx_queue);
}
static void vector_reset_tx(struct work_struct *work)
{
	struct vector_private *vp =
		container_of(work, struct vector_private, reset_tx);
	netdev_reset_queue(vp->dev);
	netif_start_queue(vp->dev);
	netif_wake_queue(vp->dev);
}

static int vector_net_open(struct net_device *dev)
{
	struct vector_private *vp = netdev_priv(dev);
	unsigned long flags;
	int err = -EINVAL;
	struct vector_device *vdevice;

	spin_lock_irqsave(&vp->lock, flags);
	if (vp->opened) {
		spin_unlock_irqrestore(&vp->lock, flags);
		return -ENXIO;
	}
	vp->opened = true;
	spin_unlock_irqrestore(&vp->lock, flags);

	vp->bpf = uml_vector_user_bpf(get_bpf_file(vp->parsed));

	vp->fds = uml_vector_user_open(vp->unit, vp->parsed);

	if (vp->fds == NULL)
		goto out_close;

	if (build_transport_data(vp) < 0)
		goto out_close;

	if ((vp->options & VECTOR_RX) > 0) {
		vp->rx_queue = create_queue(
			vp,
			get_depth(vp->parsed),
			vp->rx_header_size,
			MAX_IOV_SIZE
		);
		vp->rx_queue->queue_depth = get_depth(vp->parsed);
	} else {
		vp->header_rxbuffer = kmalloc(
			vp->rx_header_size,
			GFP_KERNEL
		);
		if (vp->header_rxbuffer == NULL)
			goto out_close;
	}
	if ((vp->options & VECTOR_TX) > 0) {
		vp->tx_queue = create_queue(
			vp,
			get_depth(vp->parsed),
			vp->header_size,
			MAX_IOV_SIZE
		);
	} else {
		vp->header_txbuffer = kmalloc(vp->header_size, GFP_KERNEL);
		if (vp->header_txbuffer == NULL)
			goto out_close;
	}

	/* READ IRQ */
	err = um_request_irq(
		irq_rr + VECTOR_BASE_IRQ, vp->fds->rx_fd,
			IRQ_READ, vector_rx_interrupt,
			IRQF_SHARED, dev->name, dev);
	if (err != 0) {
		netdev_err(dev, "vector_open: failed to get rx irq(%d)\n", err);
		err = -ENETUNREACH;
		goto out_close;
	}
	vp->rx_irq = irq_rr + VECTOR_BASE_IRQ;
	dev->irq = irq_rr + VECTOR_BASE_IRQ;
	irq_rr = (irq_rr + 1) % VECTOR_IRQ_SPACE;

	/* WRITE IRQ - we need it only if we have vector TX */
	if ((vp->options & VECTOR_TX) > 0) {
		err = um_request_irq(
			irq_rr + VECTOR_BASE_IRQ, vp->fds->tx_fd,
				IRQ_WRITE, vector_tx_interrupt,
				IRQF_SHARED, dev->name, dev);
		if (err != 0) {
			netdev_err(dev,
				"vector_open: failed to get tx irq(%d)\n", err);
			err = -ENETUNREACH;
			goto out_close;
		}
		vp->tx_irq = irq_rr + VECTOR_BASE_IRQ;
		irq_rr = (irq_rr + 1) % VECTOR_IRQ_SPACE;
	}

	if ((vp->options & VECTOR_QDISC_BYPASS) != 0) {
		if (!uml_raw_enable_qdisc_bypass(vp->fds->rx_fd))
			vp->options |= VECTOR_BPF;
	}
	if (((vp->options & VECTOR_BPF) != 0) && (vp->bpf == NULL))
		vp->bpf = uml_vector_default_bpf(dev->dev_addr);

	if (vp->bpf != NULL)
		uml_vector_attach_bpf(vp->fds->rx_fd, vp->bpf);

	netif_start_queue(dev);

	/* clear buffer - it can happen that the host side of the interface
	 * is full when we get here. In this case, new data is never queued,
	 * SIGIOs never arrive, and the net never works.
	 */

	vector_rx(vp);

	vector_reset_stats(vp);
	vdevice = find_device(vp->unit);
	vdevice->opened = 1;

	if ((vp->options & VECTOR_TX) != 0)
		add_timer(&vp->tl);
	return 0;
out_close:
	vector_net_close(dev);
	return err;
}


static void vector_net_set_multicast_list(struct net_device *dev)
{
	/* TODO: - we can do some BPF games here */
	return;
}

static void vector_net_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct vector_private *vp = netdev_priv(dev);

	vp->estats.tx_timeout_count++;
	netif_trans_update(dev);
	schedule_work(&vp->reset_tx);
}

static netdev_features_t vector_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	features &= ~(NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM);
	return features;
}

static int vector_set_features(struct net_device *dev,
	netdev_features_t features)
{
	struct vector_private *vp = netdev_priv(dev);
	/* Adjust buffer sizes for GSO/GRO. Unfortunately, there is
	 * no way to negotiate it on raw sockets, so we can change
	 * only our side.
	 */
	if (features & NETIF_F_GRO)
		/* All new frame buffers will be GRO-sized */
		vp->req_size = 65536;
	else
		/* All new frame buffers will be normal sized */
		vp->req_size = vp->max_packet + vp->headroom + SAFETY_MARGIN;
	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void vector_net_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	vector_rx_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static void vector_net_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRIVER_NAME, sizeof(info->driver));
}

static int vector_net_load_bpf_flash(struct net_device *dev,
				struct ethtool_flash *efl)
{
	struct vector_private *vp = netdev_priv(dev);
	struct vector_device *vdevice;
	const struct firmware *fw;
	int result = 0;

	if (!(vp->options & VECTOR_BPF_FLASH)) {
		netdev_err(dev, "loading firmware not permitted: %s\n", efl->data);
		return -1;
	}

	spin_lock(&vp->lock);

	if (vp->bpf != NULL) {
		if (vp->opened)
			uml_vector_detach_bpf(vp->fds->rx_fd, vp->bpf);
		kfree(vp->bpf->filter);
		vp->bpf->filter = NULL;
	} else {
		vp->bpf = kmalloc(sizeof(struct sock_fprog), GFP_KERNEL);
		if (vp->bpf == NULL) {
			netdev_err(dev, "failed to allocate memory for firmware\n");
			goto flash_fail;
		}
	}

	vdevice = find_device(vp->unit);

	if (request_firmware(&fw, efl->data, &vdevice->pdev.dev))
		goto flash_fail;

	vp->bpf->filter = kmemdup(fw->data, fw->size, GFP_KERNEL);
	if (!vp->bpf->filter)
		goto free_buffer;

	vp->bpf->len = fw->size / sizeof(struct sock_filter);
	release_firmware(fw);

	if (vp->opened)
		result = uml_vector_attach_bpf(vp->fds->rx_fd, vp->bpf);

	spin_unlock(&vp->lock);

	return result;

free_buffer:
	release_firmware(fw);

flash_fail:
	spin_unlock(&vp->lock);
	if (vp->bpf != NULL)
		kfree(vp->bpf->filter);
	kfree(vp->bpf);
	vp->bpf = NULL;
	return -1;
}

static void vector_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct vector_private *vp = netdev_priv(netdev);

	ring->rx_max_pending = vp->rx_queue->max_depth;
	ring->tx_max_pending = vp->tx_queue->max_depth;
	ring->rx_pending = vp->rx_queue->max_depth;
	ring->tx_pending = vp->tx_queue->max_depth;
}

static void vector_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_TEST:
		*buf = '\0';
		break;
	case ETH_SS_STATS:
		memcpy(buf, &ethtool_stats_keys, sizeof(ethtool_stats_keys));
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static int vector_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return 0;
	case ETH_SS_STATS:
		return VECTOR_NUM_STATS;
	default:
		return -EOPNOTSUPP;
	}
}

static void vector_get_ethtool_stats(struct net_device *dev,
	struct ethtool_stats *estats,
	u64 *tmp_stats)
{
	struct vector_private *vp = netdev_priv(dev);

	memcpy(tmp_stats, &vp->estats, sizeof(struct vector_estats));
}

static int vector_get_coalesce(struct net_device *netdev,
					struct ethtool_coalesce *ec)
{
	struct vector_private *vp = netdev_priv(netdev);

	ec->tx_coalesce_usecs = (vp->coalesce * 1000000) / HZ;
	return 0;
}

static int vector_set_coalesce(struct net_device *netdev,
					struct ethtool_coalesce *ec)
{
	struct vector_private *vp = netdev_priv(netdev);

	vp->coalesce = (ec->tx_coalesce_usecs * HZ) / 1000000;
	if (vp->coalesce == 0)
		vp->coalesce = 1;
	return 0;
}

static const struct ethtool_ops vector_net_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_TX_USECS,
	.get_drvinfo	= vector_net_get_drvinfo,
	.get_link	= ethtool_op_get_link,
	.get_ts_info	= ethtool_op_get_ts_info,
	.get_ringparam	= vector_get_ringparam,
	.get_strings	= vector_get_strings,
	.get_sset_count	= vector_get_sset_count,
	.get_ethtool_stats = vector_get_ethtool_stats,
	.get_coalesce	= vector_get_coalesce,
	.set_coalesce	= vector_set_coalesce,
	.flash_device	= vector_net_load_bpf_flash,
};


static const struct net_device_ops vector_netdev_ops = {
	.ndo_open		= vector_net_open,
	.ndo_stop		= vector_net_close,
	.ndo_start_xmit		= vector_net_start_xmit,
	.ndo_set_rx_mode	= vector_net_set_multicast_list,
	.ndo_tx_timeout		= vector_net_tx_timeout,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_fix_features	= vector_fix_features,
	.ndo_set_features	= vector_set_features,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = vector_net_poll_controller,
#endif
};


static void vector_timer_expire(struct timer_list *t)
{
	struct vector_private *vp = from_timer(vp, t, tl);

	vp->estats.tx_kicks++;
	vector_send(vp->tx_queue);
}

static void vector_eth_configure(
		int n,
		struct arglist *def
	)
{
	struct vector_device *device;
	struct net_device *dev;
	struct vector_private *vp;
	int err;

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (device == NULL) {
		printk(KERN_ERR "eth_configure failed to allocate struct "
				 "vector_device\n");
		return;
	}
	dev = alloc_etherdev(sizeof(struct vector_private));
	if (dev == NULL) {
		printk(KERN_ERR "eth_configure: failed to allocate struct "
				 "net_device for vec%d\n", n);
		goto out_free_device;
	}

	dev->mtu = get_mtu(def);

	INIT_LIST_HEAD(&device->list);
	device->unit = n;

	/* If this name ends up conflicting with an existing registered
	 * netdevice, that is OK, register_netdev{,ice}() will notice this
	 * and fail.
	 */
	snprintf(dev->name, sizeof(dev->name), "vec%d", n);
	uml_net_setup_etheraddr(dev, uml_vector_fetch_arg(def, "mac"));
	vp = netdev_priv(dev);

	/* sysfs register */
	if (!driver_registered) {
		platform_driver_register(&uml_net_driver);
		driver_registered = 1;
	}
	device->pdev.id = n;
	device->pdev.name = DRIVER_NAME;
	device->pdev.dev.release = vector_device_release;
	dev_set_drvdata(&device->pdev.dev, device);
	if (platform_device_register(&device->pdev))
		goto out_free_netdev;
	SET_NETDEV_DEV(dev, &device->pdev.dev);

	device->dev = dev;

	*vp = ((struct vector_private)
		{
		.list			= LIST_HEAD_INIT(vp->list),
		.dev			= dev,
		.unit			= n,
		.options		= get_transport_options(def),
		.rx_irq			= 0,
		.tx_irq			= 0,
		.parsed			= def,
		.max_packet		= get_mtu(def) + ETH_HEADER_OTHER,
		/* TODO - we need to calculate headroom so that ip header
		 * is 16 byte aligned all the time
		 */
		.headroom		= get_headroom(def),
		.form_header		= NULL,
		.verify_header		= NULL,
		.header_rxbuffer	= NULL,
		.header_txbuffer	= NULL,
		.header_size		= 0,
		.rx_header_size		= 0,
		.rexmit_scheduled	= false,
		.opened			= false,
		.transport_data		= NULL,
		.in_write_poll		= false,
		.coalesce		= 2,
		.req_size		= get_req_size(def),
		.in_error		= false,
		.bpf			= NULL
	});

	dev->features = dev->hw_features = (NETIF_F_SG | NETIF_F_FRAGLIST);
	tasklet_init(&vp->tx_poll, vector_tx_poll, (unsigned long)vp);
	INIT_WORK(&vp->reset_tx, vector_reset_tx);

	timer_setup(&vp->tl, vector_timer_expire, 0);
	spin_lock_init(&vp->lock);

	/* FIXME */
	dev->netdev_ops = &vector_netdev_ops;
	dev->ethtool_ops = &vector_net_ethtool_ops;
	dev->watchdog_timeo = (HZ >> 1);
	/* primary IRQ - fixme */
	dev->irq = 0; /* we will adjust this once opened */

	rtnl_lock();
	err = register_netdevice(dev);
	rtnl_unlock();
	if (err)
		goto out_undo_user_init;

	spin_lock(&vector_devices_lock);
	list_add(&device->list, &vector_devices);
	spin_unlock(&vector_devices_lock);

	return;

out_undo_user_init:
	return;
out_free_netdev:
	free_netdev(dev);
out_free_device:
	kfree(device);
}




/*
 * Invoked late in the init
 */

static int __init vector_init(void)
{
	struct list_head *ele;
	struct vector_cmd_line_arg *def;
	struct arglist *parsed;

	list_for_each(ele, &vec_cmd_line) {
		def = list_entry(ele, struct vector_cmd_line_arg, list);
		parsed = uml_parse_vector_ifspec(def->arguments);
		if (parsed != NULL)
			vector_eth_configure(def->unit, parsed);
	}
	return 0;
}


/* Invoked at initial argument parsing, only stores
 * arguments until a proper vector_init is called
 * later
 */

static int __init vector_setup(char *str)
{
	char *error;
	int n, err;
	struct vector_cmd_line_arg *new;

	err = vector_parse(str, &n, &str, &error);
	if (err) {
		printk(KERN_ERR "vector_setup - Couldn't parse '%s' : %s\n",
				 str, error);
		return 1;
	}
	new = memblock_alloc(sizeof(*new), SMP_CACHE_BYTES);
	if (!new)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      sizeof(*new));
	INIT_LIST_HEAD(&new->list);
	new->unit = n;
	new->arguments = str;
	list_add_tail(&new->list, &vec_cmd_line);
	return 1;
}

__setup("vec", vector_setup);
__uml_help(vector_setup,
"vec[0-9]+:<option>=<value>,<option>=<value>\n"
"	 Configure a vector io network device.\n\n"
);

late_initcall(vector_init);

static struct mc_device vector_mc = {
	.list		= LIST_HEAD_INIT(vector_mc.list),
	.name		= "vec",
	.config		= vector_config,
	.get_config	= NULL,
	.id		= vector_id,
	.remove		= vector_remove,
};

#ifdef CONFIG_INET
static int vector_inetaddr_event(
	struct notifier_block *this,
	unsigned long event,
	void *ptr)
{
	return NOTIFY_DONE;
}

static struct notifier_block vector_inetaddr_notifier = {
	.notifier_call		= vector_inetaddr_event,
};

static void inet_register(void)
{
	register_inetaddr_notifier(&vector_inetaddr_notifier);
}
#else
static inline void inet_register(void)
{
}
#endif

static int vector_net_init(void)
{
	mconsole_register_dev(&vector_mc);
	inet_register();
	return 0;
}

__initcall(vector_net_init);



