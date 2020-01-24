// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 */

#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/if_arp.h>
#include <net/caif/caif_device.h>
#include <net/caif/cfcnfg.h>
#include <linux/err.h>
#include <linux/debugfs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sjur Brendeland");
MODULE_DESCRIPTION("CAIF serial device TTY line discipline");
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_CAIF);

#define SEND_QUEUE_LOW 10
#define SEND_QUEUE_HIGH 100
#define CAIF_SENDING	        1 /* Bit 1 = 0x02*/
#define CAIF_FLOW_OFF_SENT	4 /* Bit 4 = 0x10 */
#define MAX_WRITE_CHUNK	     4096
#define ON 1
#define OFF 0
#define CAIF_MAX_MTU 4096

static DEFINE_SPINLOCK(ser_lock);
static LIST_HEAD(ser_list);
static LIST_HEAD(ser_release_list);

static bool ser_loop;
module_param(ser_loop, bool, 0444);
MODULE_PARM_DESC(ser_loop, "Run in simulated loopback mode.");

static bool ser_use_stx = true;
module_param(ser_use_stx, bool, 0444);
MODULE_PARM_DESC(ser_use_stx, "STX enabled or not.");

static bool ser_use_fcs = true;

module_param(ser_use_fcs, bool, 0444);
MODULE_PARM_DESC(ser_use_fcs, "FCS enabled or not.");

static int ser_write_chunk = MAX_WRITE_CHUNK;
module_param(ser_write_chunk, int, 0444);

MODULE_PARM_DESC(ser_write_chunk, "Maximum size of data written to UART.");

static struct dentry *debugfsdir;

static int caif_net_open(struct net_device *dev);
static int caif_net_close(struct net_device *dev);

struct ser_device {
	struct caif_dev_common common;
	struct list_head node;
	struct net_device *dev;
	struct sk_buff_head head;
	struct tty_struct *tty;
	bool tx_started;
	unsigned long state;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_tty_dir;
	struct debugfs_blob_wrapper tx_blob;
	struct debugfs_blob_wrapper rx_blob;
	u8 rx_data[128];
	u8 tx_data[128];
	u8 tty_status;

#endif
};

static void caifdev_setup(struct net_device *dev);
static void ldisc_tx_wakeup(struct tty_struct *tty);
#ifdef CONFIG_DEBUG_FS
static inline void update_tty_status(struct ser_device *ser)
{
	ser->tty_status =
		ser->tty->stopped << 5 |
		ser->tty->flow_stopped << 3 |
		ser->tty->packet << 2 |
		ser->tty->port->low_latency << 1;
}
static inline void debugfs_init(struct ser_device *ser, struct tty_struct *tty)
{
	ser->debugfs_tty_dir = debugfs_create_dir(tty->name, debugfsdir);

	debugfs_create_blob("last_tx_msg", 0400, ser->debugfs_tty_dir,
			    &ser->tx_blob);

	debugfs_create_blob("last_rx_msg", 0400, ser->debugfs_tty_dir,
			    &ser->rx_blob);

	debugfs_create_xul("ser_state", 0400, ser->debugfs_tty_dir,
			   &ser->state);

	debugfs_create_x8("tty_status", 0400, ser->debugfs_tty_dir,
			  &ser->tty_status);

	ser->tx_blob.data = ser->tx_data;
	ser->tx_blob.size = 0;
	ser->rx_blob.data = ser->rx_data;
	ser->rx_blob.size = 0;
}

static inline void debugfs_deinit(struct ser_device *ser)
{
	debugfs_remove_recursive(ser->debugfs_tty_dir);
}

static inline void debugfs_rx(struct ser_device *ser, const u8 *data, int size)
{
	if (size > sizeof(ser->rx_data))
		size = sizeof(ser->rx_data);
	memcpy(ser->rx_data, data, size);
	ser->rx_blob.data = ser->rx_data;
	ser->rx_blob.size = size;
}

static inline void debugfs_tx(struct ser_device *ser, const u8 *data, int size)
{
	if (size > sizeof(ser->tx_data))
		size = sizeof(ser->tx_data);
	memcpy(ser->tx_data, data, size);
	ser->tx_blob.data = ser->tx_data;
	ser->tx_blob.size = size;
}
#else
static inline void debugfs_init(struct ser_device *ser, struct tty_struct *tty)
{
}

static inline void debugfs_deinit(struct ser_device *ser)
{
}

static inline void update_tty_status(struct ser_device *ser)
{
}

static inline void debugfs_rx(struct ser_device *ser, const u8 *data, int size)
{
}

static inline void debugfs_tx(struct ser_device *ser, const u8 *data, int size)
{
}

#endif

static void ldisc_receive(struct tty_struct *tty, const u8 *data,
			char *flags, int count)
{
	struct sk_buff *skb = NULL;
	struct ser_device *ser;
	int ret;

	ser = tty->disc_data;

	/*
	 * NOTE: flags may contain information about break or overrun.
	 * This is not yet handled.
	 */


	/*
	 * Workaround for garbage at start of transmission,
	 * only enable if STX handling is not enabled.
	 */
	if (!ser->common.use_stx && !ser->tx_started) {
		dev_info(&ser->dev->dev,
			"Bytes received before initial transmission -"
			"bytes discarded.\n");
		return;
	}

	BUG_ON(ser->dev == NULL);

	/* Get a suitable caif packet and copy in data. */
	skb = netdev_alloc_skb(ser->dev, count+1);
	if (skb == NULL)
		return;
	skb_put_data(skb, data, count);

	skb->protocol = htons(ETH_P_CAIF);
	skb_reset_mac_header(skb);
	debugfs_rx(ser, data, count);
	/* Push received packet up the stack. */
	ret = netif_rx_ni(skb);
	if (!ret) {
		ser->dev->stats.rx_packets++;
		ser->dev->stats.rx_bytes += count;
	} else
		++ser->dev->stats.rx_dropped;
	update_tty_status(ser);
}

static int handle_tx(struct ser_device *ser)
{
	struct tty_struct *tty;
	struct sk_buff *skb;
	int tty_wr, len, room;

	tty = ser->tty;
	ser->tx_started = true;

	/* Enter critical section */
	if (test_and_set_bit(CAIF_SENDING, &ser->state))
		return 0;

	/* skb_peek is safe because handle_tx is called after skb_queue_tail */
	while ((skb = skb_peek(&ser->head)) != NULL) {

		/* Make sure you don't write too much */
		len = skb->len;
		room = tty_write_room(tty);
		if (!room)
			break;
		if (room > ser_write_chunk)
			room = ser_write_chunk;
		if (len > room)
			len = room;

		/* Write to tty or loopback */
		if (!ser_loop) {
			tty_wr = tty->ops->write(tty, skb->data, len);
			update_tty_status(ser);
		} else {
			tty_wr = len;
			ldisc_receive(tty, skb->data, NULL, len);
		}
		ser->dev->stats.tx_packets++;
		ser->dev->stats.tx_bytes += tty_wr;

		/* Error on TTY ?! */
		if (tty_wr < 0)
			goto error;
		/* Reduce buffer written, and discard if empty */
		skb_pull(skb, tty_wr);
		if (skb->len == 0) {
			struct sk_buff *tmp = skb_dequeue(&ser->head);
			WARN_ON(tmp != skb);
			dev_consume_skb_any(skb);
		}
	}
	/* Send flow off if queue is empty */
	if (ser->head.qlen <= SEND_QUEUE_LOW &&
		test_and_clear_bit(CAIF_FLOW_OFF_SENT, &ser->state) &&
		ser->common.flowctrl != NULL)
				ser->common.flowctrl(ser->dev, ON);
	clear_bit(CAIF_SENDING, &ser->state);
	return 0;
error:
	clear_bit(CAIF_SENDING, &ser->state);
	return tty_wr;
}

static int caif_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ser_device *ser;

	BUG_ON(dev == NULL);
	ser = netdev_priv(dev);

	/* Send flow off once, on high water mark */
	if (ser->head.qlen > SEND_QUEUE_HIGH &&
		!test_and_set_bit(CAIF_FLOW_OFF_SENT, &ser->state) &&
		ser->common.flowctrl != NULL)

		ser->common.flowctrl(ser->dev, OFF);

	skb_queue_tail(&ser->head, skb);
	return handle_tx(ser);
}


static void ldisc_tx_wakeup(struct tty_struct *tty)
{
	struct ser_device *ser;

	ser = tty->disc_data;
	BUG_ON(ser == NULL);
	WARN_ON(ser->tty != tty);
	handle_tx(ser);
}


static void ser_release(struct work_struct *work)
{
	struct list_head list;
	struct ser_device *ser, *tmp;

	spin_lock(&ser_lock);
	list_replace_init(&ser_release_list, &list);
	spin_unlock(&ser_lock);

	if (!list_empty(&list)) {
		rtnl_lock();
		list_for_each_entry_safe(ser, tmp, &list, node) {
			dev_close(ser->dev);
			unregister_netdevice(ser->dev);
			debugfs_deinit(ser);
		}
		rtnl_unlock();
	}
}

static DECLARE_WORK(ser_release_work, ser_release);

static int ldisc_open(struct tty_struct *tty)
{
	struct ser_device *ser;
	struct net_device *dev;
	char name[64];
	int result;

	/* No write no play */
	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;
	if (!capable(CAP_SYS_ADMIN) && !capable(CAP_SYS_TTY_CONFIG))
		return -EPERM;

	/* release devices to avoid name collision */
	ser_release(NULL);

	result = snprintf(name, sizeof(name), "cf%s", tty->name);
	if (result >= IFNAMSIZ)
		return -EINVAL;
	dev = alloc_netdev(sizeof(*ser), name, NET_NAME_UNKNOWN,
			   caifdev_setup);
	if (!dev)
		return -ENOMEM;

	ser = netdev_priv(dev);
	ser->tty = tty_kref_get(tty);
	ser->dev = dev;
	debugfs_init(ser, tty);
	tty->receive_room = N_TTY_BUF_SIZE;
	tty->disc_data = ser;
	set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	rtnl_lock();
	result = register_netdevice(dev);
	if (result) {
		rtnl_unlock();
		free_netdev(dev);
		return -ENODEV;
	}

	spin_lock(&ser_lock);
	list_add(&ser->node, &ser_list);
	spin_unlock(&ser_lock);
	rtnl_unlock();
	netif_stop_queue(dev);
	update_tty_status(ser);
	return 0;
}

static void ldisc_close(struct tty_struct *tty)
{
	struct ser_device *ser = tty->disc_data;

	tty_kref_put(ser->tty);

	spin_lock(&ser_lock);
	list_move(&ser->node, &ser_release_list);
	spin_unlock(&ser_lock);
	schedule_work(&ser_release_work);
}

/* The line discipline structure. */
static struct tty_ldisc_ops caif_ldisc = {
	.owner =	THIS_MODULE,
	.magic =	TTY_LDISC_MAGIC,
	.name =		"n_caif",
	.open =		ldisc_open,
	.close =	ldisc_close,
	.receive_buf =	ldisc_receive,
	.write_wakeup =	ldisc_tx_wakeup
};

static int register_ldisc(void)
{
	int result;

	result = tty_register_ldisc(N_CAIF, &caif_ldisc);
	if (result < 0) {
		pr_err("cannot register CAIF ldisc=%d err=%d\n", N_CAIF,
			result);
		return result;
	}
	return result;
}
static const struct net_device_ops netdev_ops = {
	.ndo_open = caif_net_open,
	.ndo_stop = caif_net_close,
	.ndo_start_xmit = caif_xmit
};

static void caifdev_setup(struct net_device *dev)
{
	struct ser_device *serdev = netdev_priv(dev);

	dev->features = 0;
	dev->netdev_ops = &netdev_ops;
	dev->type = ARPHRD_CAIF;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu = CAIF_MAX_MTU;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->needs_free_netdev = true;
	skb_queue_head_init(&serdev->head);
	serdev->common.link_select = CAIF_LINK_LOW_LATENCY;
	serdev->common.use_frag = true;
	serdev->common.use_stx = ser_use_stx;
	serdev->common.use_fcs = ser_use_fcs;
	serdev->dev = dev;
}


static int caif_net_open(struct net_device *dev)
{
	netif_wake_queue(dev);
	return 0;
}

static int caif_net_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static int __init caif_ser_init(void)
{
	int ret;

	ret = register_ldisc();
	debugfsdir = debugfs_create_dir("caif_serial", NULL);
	return ret;
}

static void __exit caif_ser_exit(void)
{
	spin_lock(&ser_lock);
	list_splice(&ser_list, &ser_release_list);
	spin_unlock(&ser_lock);
	ser_release(NULL);
	cancel_work_sync(&ser_release_work);
	tty_unregister_ldisc(N_CAIF);
	debugfs_remove_recursive(debugfsdir);
}

module_init(caif_ser_init);
module_exit(caif_ser_exit);
