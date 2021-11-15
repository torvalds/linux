// SPDX-License-Identifier: GPL-2.0
/*
 * Management Controller Transport Protocol (MCTP)
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/if_arp.h>
#include <net/mctp.h>
#include <net/mctpdevice.h>

/* SMBus 3.0 allows 255 data bytes (plus PEC), but the
 * first byte is taken for source slave address.
 */
#define MCTP_I2C_MAXBLOCK 255
#define MCTP_I2C_MAXMTU (MCTP_I2C_MAXBLOCK - 1)
#define MCTP_I2C_MINMTU (64 + 4)
/* Allow space for address, command, byte_count, databytes, PEC */
#define MCTP_I2C_RXBUFSZ (3 + MCTP_I2C_MAXBLOCK + 1)
#define MCTP_I2C_MINLEN 8
#define MCTP_I2C_COMMANDCODE 0x0f
#define MCTP_I2C_TX_WORK_LEN 100
// sufficient for 64kB at min mtu
#define MCTP_I2C_TX_QUEUE_LEN 1100

#define MCTP_I2C_OF_PROP "mctp-controller"

enum {
	MCTP_I2C_FLOW_STATE_NEW = 0,
	MCTP_I2C_FLOW_STATE_ACTIVE,
};

static struct {
	/* lock protects clients and also prevents adding/removing adapters
	 * during mctp_i2c_client probe/remove.
	 */
	struct mutex lock;
	// list of struct mctp_i2c_client
	struct list_head clients;
} mi_driver_state;

struct mctp_i2c_client;

// The netdev structure. One of these per I2C adapter.
struct mctp_i2c_dev {
	struct net_device *ndev;
	struct i2c_adapter *adapter;
	struct mctp_i2c_client *client;
	struct list_head list; // for mctp_i2c_client.devs

	size_t pos;
	u8 buffer[MCTP_I2C_RXBUFSZ];

	struct task_struct *tx_thread;
	wait_queue_head_t tx_wq;
	struct sk_buff_head tx_queue;

	// a fake entry in our tx queue to perform an unlock operation
	struct sk_buff unlock_marker;

	spinlock_t flow_lock; // protects i2c_lock_count and release_count
	int i2c_lock_count;
	int release_count;
};

/* The i2c client structure. One per hardware i2c bus at the top of the
 * mux tree, shared by multiple netdevs
 */
struct mctp_i2c_client {
	struct i2c_client *client;
	u8 lladdr;

	struct mctp_i2c_dev *sel;
	struct list_head devs;
	spinlock_t curr_lock; // protects sel

	struct list_head list; // for mi_driver_state.clients
};

// Header on the wire
struct mctp_i2c_hdr {
	u8 dest_slave;
	u8 command;
	u8 byte_count;
	u8 source_slave;
};

static int mctp_i2c_recv(struct mctp_i2c_dev *midev);
static int mctp_i2c_slave_cb(struct i2c_client *client,
			     enum i2c_slave_event event, u8 *val);

static struct i2c_adapter *mux_root_adapter(struct i2c_adapter *adap)
{
#if IS_ENABLED(CONFIG_I2C_MUX)
	return i2c_root_adapter(&adap->dev);
#else
	/* In non-mux config all i2c adapters are root adapters */
	return adap;
#endif
}

static ssize_t mctp_current_mux_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct mctp_i2c_client *mcli = i2c_get_clientdata(to_i2c_client(dev));
	struct net_device *ndev = NULL;
	unsigned long flags;
	ssize_t l;

	spin_lock_irqsave(&mcli->curr_lock, flags);
	if (mcli->sel) {
		ndev = mcli->sel->ndev;
		dev_hold(ndev);
	}
	spin_unlock_irqrestore(&mcli->curr_lock, flags);
	l = scnprintf(buf, PAGE_SIZE, "%s\n", ndev ? ndev->name : "(none)");
	if (ndev)
		dev_put(ndev);
	return l;
}
static DEVICE_ATTR_RO(mctp_current_mux);

/* Creates a new i2c slave device attached to the root adapter.
 * Sets up the slave callback.
 * Must be called with a client on a root adapter.
 */
static struct mctp_i2c_client *mctp_i2c_new_client(struct i2c_client *client)
{
	struct mctp_i2c_client *mcli = NULL;
	struct i2c_adapter *root = NULL;
	int rc;

	if (client->flags & I2C_CLIENT_TEN) {
		dev_err(&client->dev, "%s failed, MCTP requires a 7-bit I2C address, addr=0x%x",
			__func__, client->addr);
		rc = -EINVAL;
		goto err;
	}

	root = mux_root_adapter(client->adapter);
	if (!root) {
		dev_err(&client->dev, "%s failed to find root adapter\n", __func__);
		rc = -ENOENT;
		goto err;
	}
	if (root != client->adapter) {
		dev_err(&client->dev,
			"A mctp-i2c-controller client cannot be placed on an I2C mux adapter.\n"
			" It should be placed on the mux tree root adapter\n"
			" then set mctp-controller property on adapters to attach\n");
		rc = -EINVAL;
		goto err;
	}

	mcli = kzalloc(sizeof(*mcli), GFP_KERNEL);
	if (!mcli) {
		rc = -ENOMEM;
		goto err;
	}
	spin_lock_init(&mcli->curr_lock);
	INIT_LIST_HEAD(&mcli->devs);
	INIT_LIST_HEAD(&mcli->list);
	mcli->lladdr = client->addr & 0xff;
	mcli->client = client;
	i2c_set_clientdata(client, mcli);

	rc = i2c_slave_register(mcli->client, mctp_i2c_slave_cb);
	if (rc) {
		dev_err(&client->dev, "%s i2c register failed %d\n", __func__, rc);
		mcli->client = NULL;
		i2c_set_clientdata(client, NULL);
		goto err;
	}

	rc = device_create_file(&client->dev, &dev_attr_mctp_current_mux);
	if (rc) {
		dev_err(&client->dev, "%s adding sysfs \"%s\" failed %d\n", __func__,
			dev_attr_mctp_current_mux.attr.name, rc);
		// continue anyway
	}

	return mcli;
err:
	if (mcli) {
		if (mcli->client) {
			device_remove_file(&mcli->client->dev, &dev_attr_mctp_current_mux);
			i2c_unregister_device(mcli->client);
		}
		kfree(mcli);
	}
	return ERR_PTR(rc);
}

static void mctp_i2c_free_client(struct mctp_i2c_client *mcli)
{
	int rc;

	WARN_ON(!mutex_is_locked(&mi_driver_state.lock));
	WARN_ON(!list_empty(&mcli->devs));
	WARN_ON(mcli->sel); // sanity check, no locking

	device_remove_file(&mcli->client->dev, &dev_attr_mctp_current_mux);
	rc = i2c_slave_unregister(mcli->client);
	// leak if it fails, we can't propagate errors upwards
	if (rc)
		dev_err(&mcli->client->dev, "%s i2c unregister failed %d\n", __func__, rc);
	else
		kfree(mcli);
}

/* Switch the mctp i2c device to receive responses.
 * Call with curr_lock held
 */
static void __mctp_i2c_device_select(struct mctp_i2c_client *mcli,
				     struct mctp_i2c_dev *midev)
{
	assert_spin_locked(&mcli->curr_lock);
	if (midev)
		dev_hold(midev->ndev);
	if (mcli->sel)
		dev_put(mcli->sel->ndev);
	mcli->sel = midev;
}

// Switch the mctp i2c device to receive responses
static void mctp_i2c_device_select(struct mctp_i2c_client *mcli,
				   struct mctp_i2c_dev *midev)
{
	unsigned long flags;

	spin_lock_irqsave(&mcli->curr_lock, flags);
	__mctp_i2c_device_select(mcli, midev);
	spin_unlock_irqrestore(&mcli->curr_lock, flags);
}

static int mctp_i2c_slave_cb(struct i2c_client *client,
			     enum i2c_slave_event event, u8 *val)
{
	struct mctp_i2c_client *mcli = i2c_get_clientdata(client);
	struct mctp_i2c_dev *midev = NULL;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&mcli->curr_lock, flags);
	midev = mcli->sel;
	if (midev)
		dev_hold(midev->ndev);
	spin_unlock_irqrestore(&mcli->curr_lock, flags);

	if (!midev)
		return 0;

	switch (event) {
	case I2C_SLAVE_WRITE_RECEIVED:
		if (midev->pos < MCTP_I2C_RXBUFSZ) {
			midev->buffer[midev->pos] = *val;
			midev->pos++;
		} else {
			midev->ndev->stats.rx_over_errors++;
		}

		break;
	case I2C_SLAVE_WRITE_REQUESTED:
		/* dest_slave as first byte */
		midev->buffer[0] = mcli->lladdr << 1;
		midev->pos = 1;
		break;
	case I2C_SLAVE_STOP:
		rc = mctp_i2c_recv(midev);
		break;
	default:
		break;
	}

	dev_put(midev->ndev);
	return rc;
}

// Processes incoming data that has been accumulated by the slave cb
static int mctp_i2c_recv(struct mctp_i2c_dev *midev)
{
	struct net_device *ndev = midev->ndev;
	struct mctp_i2c_hdr *hdr;
	struct mctp_skb_cb *cb;
	struct sk_buff *skb;
	u8 pec, calc_pec;
	size_t recvlen;

	/* + 1 for the PEC */
	if (midev->pos < MCTP_I2C_MINLEN + 1) {
		ndev->stats.rx_length_errors++;
		return -EINVAL;
	}
	recvlen = midev->pos - 1;

	hdr = (void *)midev->buffer;
	if (hdr->command != MCTP_I2C_COMMANDCODE) {
		ndev->stats.rx_dropped++;
		return -EINVAL;
	}

	pec = midev->buffer[midev->pos - 1];
	calc_pec = i2c_smbus_pec(0, midev->buffer, recvlen);
	if (pec != calc_pec) {
		ndev->stats.rx_crc_errors++;
		return -EINVAL;
	}

	skb = netdev_alloc_skb(ndev, recvlen);
	if (!skb) {
		ndev->stats.rx_dropped++;
		return -ENOMEM;
	}

	skb->protocol = htons(ETH_P_MCTP);
	skb_put_data(skb, midev->buffer, recvlen);
	skb_reset_mac_header(skb);
	skb_pull(skb, sizeof(struct mctp_i2c_hdr));
	skb_reset_network_header(skb);

	cb = __mctp_cb(skb);
	cb->halen = 1;
	cb->haddr[0] = hdr->source_slave;

	if (netif_rx(skb) == NET_RX_SUCCESS) {
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += skb->len;
	} else {
		ndev->stats.rx_dropped++;
	}
	return 0;
}

enum mctp_i2c_flow_state {
	MCTP_I2C_TX_FLOW_INVALID,
	MCTP_I2C_TX_FLOW_NONE,
	MCTP_I2C_TX_FLOW_NEW,
	MCTP_I2C_TX_FLOW_EXISTING,
};

static enum mctp_i2c_flow_state
mctp_i2c_get_tx_flow_state(struct mctp_i2c_dev *midev, struct sk_buff *skb)
{
	enum mctp_i2c_flow_state state;
	struct mctp_sk_key *key;
	struct mctp_flow *flow;
	unsigned long flags;

	flow = skb_ext_find(skb, SKB_EXT_MCTP);
	if (!flow)
		return MCTP_I2C_TX_FLOW_NONE;

	key = flow->key;
	if (!key)
		return MCTP_I2C_TX_FLOW_NONE;

	spin_lock_irqsave(&key->lock, flags);
	/* if the key is present but invalid, we're unlikely to be able
	 * to handle the flow at all; just drop now
	 */
	if (!key->valid) {
		state = MCTP_I2C_TX_FLOW_INVALID;

	} else if (key->dev_flow_state == MCTP_I2C_FLOW_STATE_NEW) {
		key->dev_flow_state = MCTP_I2C_FLOW_STATE_ACTIVE;
		state = MCTP_I2C_TX_FLOW_NEW;
	} else {
		state = MCTP_I2C_TX_FLOW_EXISTING;
	}

	spin_unlock_irqrestore(&key->lock, flags);

	return state;
}

/* We're not contending with ourselves here; we only need to exclude other
 * i2c clients from using the bus. refcounts are simply to prevent
 * recursive locking.
 */
static void mctp_i2c_lock_nest(struct mctp_i2c_dev *midev)
{
	unsigned long flags;
	bool lock;

	spin_lock_irqsave(&midev->flow_lock, flags);
	lock = midev->i2c_lock_count == 0;
	midev->i2c_lock_count++;
	spin_unlock_irqrestore(&midev->flow_lock, flags);

	if (lock)
		i2c_lock_bus(midev->adapter, I2C_LOCK_SEGMENT);
}

static void mctp_i2c_unlock_nest(struct mctp_i2c_dev *midev)
{
	unsigned long flags;
	bool unlock;

	spin_lock_irqsave(&midev->flow_lock, flags);
	if (!WARN_ONCE(midev->i2c_lock_count == 0, "lock count underflow!"))
		midev->i2c_lock_count--;
	unlock = midev->i2c_lock_count == 0;
	spin_unlock_irqrestore(&midev->flow_lock, flags);

	if (unlock)
		i2c_unlock_bus(midev->adapter, I2C_LOCK_SEGMENT);
}

static void mctp_i2c_xmit(struct mctp_i2c_dev *midev, struct sk_buff *skb)
{
	struct net_device_stats *stats = &midev->ndev->stats;
	enum mctp_i2c_flow_state fs;
	union i2c_smbus_data *data;
	struct mctp_i2c_hdr *hdr;
	unsigned int len;
	u16 daddr;
	int rc;

	fs = mctp_i2c_get_tx_flow_state(midev, skb);

	len = skb->len;
	hdr = (void *)skb_mac_header(skb);
	data = (void *)&hdr->byte_count;
	daddr = hdr->dest_slave >> 1;

	switch (fs) {
	case MCTP_I2C_TX_FLOW_NONE:
		/* no flow: full lock & unlock */
		mctp_i2c_lock_nest(midev);
		mctp_i2c_device_select(midev->client, midev);
		rc = __i2c_smbus_xfer(midev->adapter, daddr, I2C_CLIENT_PEC,
				      I2C_SMBUS_WRITE, hdr->command,
				      I2C_SMBUS_BLOCK_DATA, data);
		mctp_i2c_unlock_nest(midev);
		break;

	case MCTP_I2C_TX_FLOW_NEW:
		/* new flow: lock, tx, but don't unlock; that will happen
		 * on flow release
		 */
		mctp_i2c_lock_nest(midev);
		mctp_i2c_device_select(midev->client, midev);
		fallthrough;

	case MCTP_I2C_TX_FLOW_EXISTING:
		/* existing flow: we already have the lock; just tx */
		rc = __i2c_smbus_xfer(midev->adapter, daddr, I2C_CLIENT_PEC,
				      I2C_SMBUS_WRITE, hdr->command,
				      I2C_SMBUS_BLOCK_DATA, data);
		break;

	case MCTP_I2C_TX_FLOW_INVALID:
		return;
	}

	if (rc) {
		dev_warn_ratelimited(&midev->adapter->dev,
				     "%s i2c_smbus_xfer failed %d", __func__, rc);
		stats->tx_errors++;
	} else {
		stats->tx_bytes += len;
		stats->tx_packets++;
	}
}

static void mctp_i2c_flow_release(struct mctp_i2c_dev *midev)
{
	unsigned long flags;
	bool unlock;

	spin_lock_irqsave(&midev->flow_lock, flags);
	if (midev->release_count > midev->i2c_lock_count) {
		WARN_ONCE(1, "release count overflow");
		midev->release_count = midev->i2c_lock_count;
	}

	midev->i2c_lock_count -= midev->release_count;
	unlock = midev->i2c_lock_count == 0 && midev->release_count > 0;
	midev->release_count = 0;
	spin_unlock_irqrestore(&midev->flow_lock, flags);

	if (unlock)
		i2c_unlock_bus(midev->adapter, I2C_LOCK_SEGMENT);
}

static int mctp_i2c_header_create(struct sk_buff *skb, struct net_device *dev,
				  unsigned short type, const void *daddr,
	   const void *saddr, unsigned int len)
{
	struct mctp_i2c_hdr *hdr;
	struct mctp_hdr *mhdr;
	u8 lldst, llsrc;

	lldst = *((u8 *)daddr);
	llsrc = *((u8 *)saddr);

	skb_push(skb, sizeof(struct mctp_i2c_hdr));
	skb_reset_mac_header(skb);
	hdr = (void *)skb_mac_header(skb);
	mhdr = mctp_hdr(skb);
	hdr->dest_slave = (lldst << 1) & 0xff;
	hdr->command = MCTP_I2C_COMMANDCODE;
	hdr->byte_count = len + 1;
	if (hdr->byte_count > MCTP_I2C_MAXBLOCK)
		return -EMSGSIZE;
	hdr->source_slave = ((llsrc << 1) & 0xff) | 0x01;
	mhdr->ver = 0x01;

	return 0;
}

static int mctp_i2c_tx_thread(void *data)
{
	struct mctp_i2c_dev *midev = data;
	struct sk_buff *skb;
	unsigned long flags;

	for (;;) {
		if (kthread_should_stop())
			break;

		spin_lock_irqsave(&midev->tx_queue.lock, flags);
		skb = __skb_dequeue(&midev->tx_queue);
		if (netif_queue_stopped(midev->ndev))
			netif_wake_queue(midev->ndev);
		spin_unlock_irqrestore(&midev->tx_queue.lock, flags);

		if (skb == &midev->unlock_marker) {
			mctp_i2c_flow_release(midev);

		} else if (skb) {
			mctp_i2c_xmit(midev, skb);
			kfree_skb(skb);

		} else {
			wait_event(midev->tx_wq,
				   !skb_queue_empty(&midev->tx_queue) ||
				   kthread_should_stop());
		}
	}

	return 0;
}

static netdev_tx_t mctp_i2c_start_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct mctp_i2c_dev *midev = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&midev->tx_queue.lock, flags);
	if (skb_queue_len(&midev->tx_queue) >= MCTP_I2C_TX_WORK_LEN) {
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&midev->tx_queue.lock, flags);
		netdev_err(dev, "BUG! Tx Ring full when queue awake!\n");
		return NETDEV_TX_BUSY;
	}

	__skb_queue_tail(&midev->tx_queue, skb);
	if (skb_queue_len(&midev->tx_queue) == MCTP_I2C_TX_WORK_LEN)
		netif_stop_queue(dev);
	spin_unlock_irqrestore(&midev->tx_queue.lock, flags);

	wake_up(&midev->tx_wq);
	return NETDEV_TX_OK;
}

static void mctp_i2c_release_flow(struct mctp_dev *mdev,
				  struct mctp_sk_key *key)

{
	struct mctp_i2c_dev *midev = netdev_priv(mdev->dev);
	unsigned long flags;

	spin_lock_irqsave(&midev->flow_lock, flags);
	midev->release_count++;
	spin_unlock_irqrestore(&midev->flow_lock, flags);

	/* Ensure we have a release operation queued, through the fake
	 * marker skb
	 */
	spin_lock(&midev->tx_queue.lock);
	if (!midev->unlock_marker.next)
		__skb_queue_tail(&midev->tx_queue, &midev->unlock_marker);
	spin_unlock(&midev->tx_queue.lock);

	wake_up(&midev->tx_wq);
}

static const struct net_device_ops mctp_i2c_ops = {
	.ndo_start_xmit = mctp_i2c_start_xmit,
};

static const struct header_ops mctp_i2c_headops = {
	.create = mctp_i2c_header_create,
};

static const struct mctp_netdev_ops mctp_i2c_mctp_ops = {
	.release_flow = mctp_i2c_release_flow,
};

static void mctp_i2c_net_setup(struct net_device *dev)
{
	dev->type = ARPHRD_MCTP;

	dev->mtu = MCTP_I2C_MAXMTU;
	dev->min_mtu = MCTP_I2C_MINMTU;
	dev->max_mtu = MCTP_I2C_MAXMTU;
	dev->tx_queue_len = MCTP_I2C_TX_QUEUE_LEN;

	dev->hard_header_len = sizeof(struct mctp_i2c_hdr);
	dev->addr_len = 1;

	dev->netdev_ops		= &mctp_i2c_ops;
	dev->header_ops		= &mctp_i2c_headops;
	dev->needs_free_netdev  = true;
}

static int mctp_i2c_add_netdev(struct mctp_i2c_client *mcli,
			       struct i2c_adapter *adap)
{
	unsigned long flags;
	struct mctp_i2c_dev *midev = NULL;
	struct net_device *ndev = NULL;
	struct i2c_adapter *root;
	char namebuf[30];
	int rc;

	root = mux_root_adapter(adap);
	if (root != mcli->client->adapter) {
		dev_err(&mcli->client->dev,
			"I2C adapter %s is not a child bus of %s",
			mcli->client->adapter->name, root->name);
		return -EINVAL;
	}

	WARN_ON(!mutex_is_locked(&mi_driver_state.lock));
	snprintf(namebuf, sizeof(namebuf), "mctpi2c%d", adap->nr);
	ndev = alloc_netdev(sizeof(*midev), namebuf, NET_NAME_ENUM, mctp_i2c_net_setup);
	if (!ndev) {
		dev_err(&mcli->client->dev, "%s alloc netdev failed\n", __func__);
		rc = -ENOMEM;
		goto err;
	}
	dev_net_set(ndev, current->nsproxy->net_ns);
	SET_NETDEV_DEV(ndev, &adap->dev);
	ndev->dev_addr = &mcli->lladdr;

	midev = netdev_priv(ndev);
	skb_queue_head_init(&midev->tx_queue);
	INIT_LIST_HEAD(&midev->list);
	midev->adapter = adap;
	midev->client = mcli;
	spin_lock_init(&midev->flow_lock);
	midev->i2c_lock_count = 0;
	midev->release_count = 0;
	/* Hold references */
	get_device(&midev->adapter->dev);
	get_device(&midev->client->client->dev);
	midev->ndev = ndev;
	init_waitqueue_head(&midev->tx_wq);
	midev->tx_thread = kthread_create(mctp_i2c_tx_thread, midev,
					  "%s/tx", namebuf);
	if (IS_ERR_OR_NULL(midev->tx_thread)) {
		rc = -ENOMEM;
		goto err_free;
	}

	rc = mctp_register_netdev(ndev, &mctp_i2c_mctp_ops);
	if (rc) {
		dev_err(&mcli->client->dev,
			"%s register netdev \"%s\" failed %d\n", __func__,
			ndev->name, rc);
		goto err_stop_kthread;
	}
	spin_lock_irqsave(&mcli->curr_lock, flags);
	list_add(&midev->list, &mcli->devs);
	// Select a device by default
	if (!mcli->sel)
		__mctp_i2c_device_select(mcli, midev);
	spin_unlock_irqrestore(&mcli->curr_lock, flags);

	wake_up_process(midev->tx_thread);

	return 0;

err_stop_kthread:
	kthread_stop(midev->tx_thread);

err_free:
	free_netdev(ndev);

err:
	return rc;
}

// Removes and unregisters a mctp-i2c netdev
static void mctp_i2c_free_netdev(struct mctp_i2c_dev *midev)
{
	struct mctp_i2c_client *mcli = midev->client;
	unsigned long flags;

	netif_stop_queue(midev->ndev);
	kthread_stop(midev->tx_thread);
	skb_queue_purge(&midev->tx_queue);

	/* Release references, used only for TX which has stopped */
	put_device(&midev->adapter->dev);
	put_device(&mcli->client->dev);

	/* Remove it from the parent mcli */
	spin_lock_irqsave(&mcli->curr_lock, flags);
	list_del(&midev->list);
	if (mcli->sel == midev) {
		struct mctp_i2c_dev *first;

		first = list_first_entry_or_null(&mcli->devs, struct mctp_i2c_dev, list);
		__mctp_i2c_device_select(mcli, first);
	}
	spin_unlock_irqrestore(&mcli->curr_lock, flags);

	/* Remove netdev. mctp_i2c_slave_cb() takes a dev_hold() so removing
	 * it now is safe. unregister_netdev() frees ndev and midev.
	 */
	mctp_unregister_netdev(midev->ndev);
}

// Removes any netdev for adap. mcli is the parent root i2c client
static void mctp_i2c_remove_netdev(struct mctp_i2c_client *mcli,
				   struct i2c_adapter *adap)
{
	unsigned long flags;
	struct mctp_i2c_dev *midev = NULL, *m = NULL;

	WARN_ON(!mutex_is_locked(&mi_driver_state.lock));
	spin_lock_irqsave(&mcli->curr_lock, flags);
	// list size is limited by number of MCTP netdevs on a single hardware bus
	list_for_each_entry(m, &mcli->devs, list)
		if (m->adapter == adap) {
			midev = m;
			break;
		}
	spin_unlock_irqrestore(&mcli->curr_lock, flags);

	if (midev)
		mctp_i2c_free_netdev(midev);
}

/* Determines whether a device is an i2c adapter.
 * Optionally returns the root i2c_adapter
 */
static struct i2c_adapter *mctp_i2c_get_adapter(struct device *dev,
						struct i2c_adapter **ret_root)
{
	struct i2c_adapter *root, *adap;

	if (dev->type != &i2c_adapter_type)
		return NULL;
	adap = to_i2c_adapter(dev);
	root = mux_root_adapter(adap);
	WARN_ONCE(!root, "%s failed to find root adapter for %s\n",
		  __func__, dev_name(dev));
	if (!root)
		return NULL;
	if (ret_root)
		*ret_root = root;
	return adap;
}

/* Determines whether a device is an i2c adapter with the "mctp-controller"
 * devicetree property set. If adap is not an OF node, returns match_no_of
 */
static bool mctp_i2c_adapter_match(struct i2c_adapter *adap, bool match_no_of)
{
	if (!adap->dev.of_node)
		return match_no_of;
	return of_property_read_bool(adap->dev.of_node, MCTP_I2C_OF_PROP);
}

/* Called for each existing i2c device (adapter or client) when a
 * new mctp-i2c client is probed.
 */
static int mctp_i2c_client_try_attach(struct device *dev, void *data)
{
	struct i2c_adapter *adap = NULL, *root = NULL;
	struct mctp_i2c_client *mcli = data;

	adap = mctp_i2c_get_adapter(dev, &root);
	if (!adap)
		return 0;
	if (mcli->client->adapter != root)
		return 0;
	// Must either have mctp-controller property on the adapter, or
	// be a root adapter if it's non-devicetree
	if (!mctp_i2c_adapter_match(adap, adap == root))
		return 0;

	return mctp_i2c_add_netdev(mcli, adap);
}

static void mctp_i2c_notify_add(struct device *dev)
{
	struct mctp_i2c_client *mcli = NULL, *m = NULL;
	struct i2c_adapter *root = NULL, *adap = NULL;
	int rc;

	adap = mctp_i2c_get_adapter(dev, &root);
	if (!adap)
		return;
	// Check for mctp-controller property on the adapter
	if (!mctp_i2c_adapter_match(adap, false))
		return;

	/* Find an existing mcli for adap's root */
	mutex_lock(&mi_driver_state.lock);
	list_for_each_entry(m, &mi_driver_state.clients, list) {
		if (m->client->adapter == root) {
			mcli = m;
			break;
		}
	}

	if (mcli) {
		rc = mctp_i2c_add_netdev(mcli, adap);
		if (rc)
			dev_warn(dev, "%s Failed adding mctp-i2c device",
				 __func__);
	}
	mutex_unlock(&mi_driver_state.lock);
}

static void mctp_i2c_notify_del(struct device *dev)
{
	struct i2c_adapter *root = NULL, *adap = NULL;
	struct mctp_i2c_client *mcli = NULL;

	adap = mctp_i2c_get_adapter(dev, &root);
	if (!adap)
		return;

	mutex_lock(&mi_driver_state.lock);
	list_for_each_entry(mcli, &mi_driver_state.clients, list) {
		if (mcli->client->adapter == root) {
			mctp_i2c_remove_netdev(mcli, adap);
			break;
		}
	}
	mutex_unlock(&mi_driver_state.lock);
}

static int mctp_i2c_probe(struct i2c_client *client)
{
	struct mctp_i2c_client *mcli = NULL;
	int rc;

	/* Check for >32 byte block support required for MCTP */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_V3_BLOCK)) {
		dev_err(&client->dev,
			"%s failed, I2C bus driver does not support 255 byte block transfer\n",
			__func__);
		return -EOPNOTSUPP;
	}

	mutex_lock(&mi_driver_state.lock);
	mcli = mctp_i2c_new_client(client);
	if (IS_ERR(mcli)) {
		rc = PTR_ERR(mcli);
		mcli = NULL;
		goto out;
	} else {
		list_add(&mcli->list, &mi_driver_state.clients);
	}

	// Add a netdev for adapters that have a 'mctp-controller' property
	i2c_for_each_dev(mcli, mctp_i2c_client_try_attach);
	rc = 0;
out:
	mutex_unlock(&mi_driver_state.lock);
	return rc;
}

static int mctp_i2c_remove(struct i2c_client *client)
{
	struct mctp_i2c_client *mcli = i2c_get_clientdata(client);
	struct mctp_i2c_dev *midev = NULL, *tmp = NULL;

	mutex_lock(&mi_driver_state.lock);
	list_del(&mcli->list);
	// Remove all child adapter netdevs
	list_for_each_entry_safe(midev, tmp, &mcli->devs, list)
		mctp_i2c_free_netdev(midev);

	mctp_i2c_free_client(mcli);
	mutex_unlock(&mi_driver_state.lock);
	// Callers ignore return code
	return 0;
}

/* We look for a 'mctp-controller' property on I2C busses as they are
 * added/deleted, creating/removing netdevs as required.
 */
static int mctp_i2c_notifier_call(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct device *dev = data;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		mctp_i2c_notify_add(dev);
		break;
	case BUS_NOTIFY_DEL_DEVICE:
		mctp_i2c_notify_del(dev);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block mctp_i2c_notifier = {
	.notifier_call = mctp_i2c_notifier_call,
};

static const struct i2c_device_id mctp_i2c_id[] = {
	{ "mctp-i2c", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, mctp_i2c_id);

static const struct of_device_id mctp_i2c_of_match[] = {
	{ .compatible = "mctp-i2c-controller" },
	{},
};
MODULE_DEVICE_TABLE(of, mctp_i2c_of_match);

static struct i2c_driver mctp_i2c_driver = {
	.driver = {
		.name = "mctp-i2c",
		.of_match_table = mctp_i2c_of_match,
	},
	.probe_new = mctp_i2c_probe,
	.remove = mctp_i2c_remove,
	.id_table = mctp_i2c_id,
};

static __init int mctp_i2c_init(void)
{
	int rc;

	INIT_LIST_HEAD(&mi_driver_state.clients);
	mutex_init(&mi_driver_state.lock);
	pr_info("MCTP SMBus/I2C transport driver\n");
	rc = i2c_add_driver(&mctp_i2c_driver);
	if (rc)
		return rc;
	rc = bus_register_notifier(&i2c_bus_type, &mctp_i2c_notifier);
	if (rc) {
		i2c_del_driver(&mctp_i2c_driver);
		return rc;
	}
	return 0;
}

static __exit void mctp_i2c_exit(void)
{
	int rc;

	rc = bus_unregister_notifier(&i2c_bus_type, &mctp_i2c_notifier);
	if (rc)
		pr_warn("%s Could not unregister notifier, %d", __func__, rc);
	i2c_del_driver(&mctp_i2c_driver);
}

module_init(mctp_i2c_init);
module_exit(mctp_i2c_exit);

MODULE_DESCRIPTION("MCTP SMBus/I2C device");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Matt Johnston <matt@codeconstruct.com.au>");
