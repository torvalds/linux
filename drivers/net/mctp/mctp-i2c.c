// SPDX-License-Identifier: GPL-2.0
/*
 * Management Controller Transport Protocol (MCTP)
 * Implements DMTF specification
 * "DSP0237 Management Component Transport Protocol (MCTP) SMBus/I2C
 * Transport Binding"
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0237_1.2.0.pdf
 *
 * A netdev is created for each I2C bus that handles MCTP. In the case of an I2C
 * mux topology a single I2C client is attached to the root of the mux topology,
 * shared between all mux I2C busses underneath. For non-mux cases an I2C client
 * is attached per netdev.
 *
 * mctp-i2c-controller.yml devicetree binding has further details.
 *
 * Copyright (c) 2022 Code Construct
 * Copyright (c) 2022 Google
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/if_arp.h>
#include <net/mctp.h>
#include <net/mctpdevice.h>

/* byte_count is limited to u8 */
#define MCTP_I2C_MAXBLOCK 255
/* One byte is taken by source_slave */
#define MCTP_I2C_MAXMTU (MCTP_I2C_MAXBLOCK - 1)
#define MCTP_I2C_MINMTU (64 + 4)
/* Allow space for dest_address, command, byte_count, data, PEC */
#define MCTP_I2C_BUFSZ (3 + MCTP_I2C_MAXBLOCK + 1)
#define MCTP_I2C_MINLEN 8
#define MCTP_I2C_COMMANDCODE 0x0f
#define MCTP_I2C_TX_WORK_LEN 100
/* Sufficient for 64kB at min mtu */
#define MCTP_I2C_TX_QUEUE_LEN 1100

#define MCTP_I2C_OF_PROP "mctp-controller"

enum {
	MCTP_I2C_FLOW_STATE_NEW = 0,
	MCTP_I2C_FLOW_STATE_ACTIVE,
};

/* List of all struct mctp_i2c_client
 * Lock protects driver_clients and also prevents adding/removing adapters
 * during mctp_i2c_client probe/remove.
 */
static DEFINE_MUTEX(driver_clients_lock);
static LIST_HEAD(driver_clients);

struct mctp_i2c_client;

/* The netdev structure. One of these per I2C adapter. */
struct mctp_i2c_dev {
	struct net_device *ndev;
	struct i2c_adapter *adapter;
	struct mctp_i2c_client *client;
	struct list_head list; /* For mctp_i2c_client.devs */

	size_t rx_pos;
	u8 rx_buffer[MCTP_I2C_BUFSZ];
	struct completion rx_done;

	struct task_struct *tx_thread;
	wait_queue_head_t tx_wq;
	struct sk_buff_head tx_queue;
	u8 tx_scratch[MCTP_I2C_BUFSZ];

	/* A fake entry in our tx queue to perform an unlock operation */
	struct sk_buff unlock_marker;

	/* Spinlock protects i2c_lock_count, release_count, allow_rx */
	spinlock_t lock;
	int i2c_lock_count;
	int release_count;
	/* Indicates that the netif is ready to receive incoming packets */
	bool allow_rx;

};

/* The i2c client structure. One per hardware i2c bus at the top of the
 * mux tree, shared by multiple netdevs
 */
struct mctp_i2c_client {
	struct i2c_client *client;
	u8 lladdr;

	struct mctp_i2c_dev *sel;
	struct list_head devs;
	spinlock_t sel_lock; /* Protects sel and devs */

	struct list_head list; /* For driver_clients */
};

/* Header on the wire. */
struct mctp_i2c_hdr {
	u8 dest_slave;
	u8 command;
	/* Count of bytes following byte_count, excluding PEC */
	u8 byte_count;
	u8 source_slave;
};

static int mctp_i2c_recv(struct mctp_i2c_dev *midev);
static int mctp_i2c_slave_cb(struct i2c_client *client,
			     enum i2c_slave_event event, u8 *val);
static void mctp_i2c_ndo_uninit(struct net_device *dev);
static int mctp_i2c_ndo_open(struct net_device *dev);

static struct i2c_adapter *mux_root_adapter(struct i2c_adapter *adap)
{
#if IS_ENABLED(CONFIG_I2C_MUX)
	return i2c_root_adapter(&adap->dev);
#else
	/* In non-mux config all i2c adapters are root adapters */
	return adap;
#endif
}

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
		dev_err(&client->dev, "failed, MCTP requires a 7-bit I2C address, addr=0x%x\n",
			client->addr);
		rc = -EINVAL;
		goto err;
	}

	root = mux_root_adapter(client->adapter);
	if (!root) {
		dev_err(&client->dev, "failed to find root adapter\n");
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
	spin_lock_init(&mcli->sel_lock);
	INIT_LIST_HEAD(&mcli->devs);
	INIT_LIST_HEAD(&mcli->list);
	mcli->lladdr = client->addr & 0xff;
	mcli->client = client;
	i2c_set_clientdata(client, mcli);

	rc = i2c_slave_register(mcli->client, mctp_i2c_slave_cb);
	if (rc < 0) {
		dev_err(&client->dev, "i2c register failed %d\n", rc);
		mcli->client = NULL;
		i2c_set_clientdata(client, NULL);
		goto err;
	}

	return mcli;
err:
	if (mcli) {
		if (mcli->client)
			i2c_unregister_device(mcli->client);
		kfree(mcli);
	}
	return ERR_PTR(rc);
}

static void mctp_i2c_free_client(struct mctp_i2c_client *mcli)
{
	int rc;

	WARN_ON(!mutex_is_locked(&driver_clients_lock));
	WARN_ON(!list_empty(&mcli->devs));
	WARN_ON(mcli->sel); /* sanity check, no locking */

	rc = i2c_slave_unregister(mcli->client);
	/* Leak if it fails, we can't propagate errors upwards */
	if (rc < 0)
		dev_err(&mcli->client->dev, "i2c unregister failed %d\n", rc);
	else
		kfree(mcli);
}

/* Switch the mctp i2c device to receive responses.
 * Call with sel_lock held
 */
static void __mctp_i2c_device_select(struct mctp_i2c_client *mcli,
				     struct mctp_i2c_dev *midev)
{
	assert_spin_locked(&mcli->sel_lock);
	if (midev)
		dev_hold(midev->ndev);
	if (mcli->sel)
		dev_put(mcli->sel->ndev);
	mcli->sel = midev;
}

/* Switch the mctp i2c device to receive responses */
static void mctp_i2c_device_select(struct mctp_i2c_client *mcli,
				   struct mctp_i2c_dev *midev)
{
	unsigned long flags;

	spin_lock_irqsave(&mcli->sel_lock, flags);
	__mctp_i2c_device_select(mcli, midev);
	spin_unlock_irqrestore(&mcli->sel_lock, flags);
}

static int mctp_i2c_slave_cb(struct i2c_client *client,
			     enum i2c_slave_event event, u8 *val)
{
	struct mctp_i2c_client *mcli = i2c_get_clientdata(client);
	struct mctp_i2c_dev *midev = NULL;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&mcli->sel_lock, flags);
	midev = mcli->sel;
	if (midev)
		dev_hold(midev->ndev);
	spin_unlock_irqrestore(&mcli->sel_lock, flags);

	if (!midev)
		return 0;

	switch (event) {
	case I2C_SLAVE_WRITE_RECEIVED:
		if (midev->rx_pos < MCTP_I2C_BUFSZ) {
			midev->rx_buffer[midev->rx_pos] = *val;
			midev->rx_pos++;
		} else {
			midev->ndev->stats.rx_over_errors++;
		}

		break;
	case I2C_SLAVE_WRITE_REQUESTED:
		/* dest_slave as first byte */
		midev->rx_buffer[0] = mcli->lladdr << 1;
		midev->rx_pos = 1;
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

/* Processes incoming data that has been accumulated by the slave cb */
static int mctp_i2c_recv(struct mctp_i2c_dev *midev)
{
	struct net_device *ndev = midev->ndev;
	struct mctp_i2c_hdr *hdr;
	struct mctp_skb_cb *cb;
	struct sk_buff *skb;
	unsigned long flags;
	u8 pec, calc_pec;
	size_t recvlen;
	int status;

	/* + 1 for the PEC */
	if (midev->rx_pos < MCTP_I2C_MINLEN + 1) {
		ndev->stats.rx_length_errors++;
		return -EINVAL;
	}
	/* recvlen excludes PEC */
	recvlen = midev->rx_pos - 1;

	hdr = (void *)midev->rx_buffer;
	if (hdr->command != MCTP_I2C_COMMANDCODE) {
		ndev->stats.rx_dropped++;
		return -EINVAL;
	}

	if (hdr->byte_count + offsetof(struct mctp_i2c_hdr, source_slave) != recvlen) {
		ndev->stats.rx_length_errors++;
		return -EINVAL;
	}

	pec = midev->rx_buffer[midev->rx_pos - 1];
	calc_pec = i2c_smbus_pec(0, midev->rx_buffer, recvlen);
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
	skb_put_data(skb, midev->rx_buffer, recvlen);
	skb_reset_mac_header(skb);
	skb_pull(skb, sizeof(struct mctp_i2c_hdr));
	skb_reset_network_header(skb);

	cb = __mctp_cb(skb);
	cb->halen = 1;
	cb->haddr[0] = hdr->source_slave >> 1;

	/* We need to ensure that the netif is not used once netdev
	 * unregister occurs
	 */
	spin_lock_irqsave(&midev->lock, flags);
	if (midev->allow_rx) {
		reinit_completion(&midev->rx_done);
		spin_unlock_irqrestore(&midev->lock, flags);

		status = netif_rx(skb);
		complete(&midev->rx_done);
	} else {
		status = NET_RX_DROP;
		spin_unlock_irqrestore(&midev->lock, flags);
	}

	if (status == NET_RX_SUCCESS) {
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += recvlen;
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
	/* If the key is present but invalid, we're unlikely to be able
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

	spin_lock_irqsave(&midev->lock, flags);
	lock = midev->i2c_lock_count == 0;
	midev->i2c_lock_count++;
	spin_unlock_irqrestore(&midev->lock, flags);

	if (lock)
		i2c_lock_bus(midev->adapter, I2C_LOCK_SEGMENT);
}

static void mctp_i2c_unlock_nest(struct mctp_i2c_dev *midev)
{
	unsigned long flags;
	bool unlock;

	spin_lock_irqsave(&midev->lock, flags);
	if (!WARN_ONCE(midev->i2c_lock_count == 0, "lock count underflow!"))
		midev->i2c_lock_count--;
	unlock = midev->i2c_lock_count == 0;
	spin_unlock_irqrestore(&midev->lock, flags);

	if (unlock)
		i2c_unlock_bus(midev->adapter, I2C_LOCK_SEGMENT);
}

/* Unlocks the bus if was previously locked, used for cleanup */
static void mctp_i2c_unlock_reset(struct mctp_i2c_dev *midev)
{
	unsigned long flags;
	bool unlock;

	spin_lock_irqsave(&midev->lock, flags);
	unlock = midev->i2c_lock_count > 0;
	midev->i2c_lock_count = 0;
	spin_unlock_irqrestore(&midev->lock, flags);

	if (unlock)
		i2c_unlock_bus(midev->adapter, I2C_LOCK_SEGMENT);
}

static void mctp_i2c_xmit(struct mctp_i2c_dev *midev, struct sk_buff *skb)
{
	struct net_device_stats *stats = &midev->ndev->stats;
	enum mctp_i2c_flow_state fs;
	struct mctp_i2c_hdr *hdr;
	struct i2c_msg msg = {0};
	u8 *pecp;
	int rc;

	fs = mctp_i2c_get_tx_flow_state(midev, skb);

	hdr = (void *)skb_mac_header(skb);
	/* Sanity check that packet contents matches skb length,
	 * and can't exceed MCTP_I2C_BUFSZ
	 */
	if (skb->len != hdr->byte_count + 3) {
		dev_warn_ratelimited(&midev->adapter->dev,
				     "Bad tx length %d vs skb %u\n",
				     hdr->byte_count + 3, skb->len);
		return;
	}

	if (skb_tailroom(skb) >= 1) {
		/* Linear case with space, we can just append the PEC */
		skb_put(skb, 1);
	} else {
		/* Otherwise need to copy the buffer */
		skb_copy_bits(skb, 0, midev->tx_scratch, skb->len);
		hdr = (void *)midev->tx_scratch;
	}

	pecp = (void *)&hdr->source_slave + hdr->byte_count;
	*pecp = i2c_smbus_pec(0, (u8 *)hdr, hdr->byte_count + 3);
	msg.buf = (void *)&hdr->command;
	/* command, bytecount, data, pec */
	msg.len = 2 + hdr->byte_count + 1;
	msg.addr = hdr->dest_slave >> 1;

	switch (fs) {
	case MCTP_I2C_TX_FLOW_NONE:
		/* no flow: full lock & unlock */
		mctp_i2c_lock_nest(midev);
		mctp_i2c_device_select(midev->client, midev);
		rc = __i2c_transfer(midev->adapter, &msg, 1);
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
		rc = __i2c_transfer(midev->adapter, &msg, 1);
		break;

	case MCTP_I2C_TX_FLOW_INVALID:
		return;
	}

	if (rc < 0) {
		dev_warn_ratelimited(&midev->adapter->dev,
				     "__i2c_transfer failed %d\n", rc);
		stats->tx_errors++;
	} else {
		stats->tx_bytes += skb->len;
		stats->tx_packets++;
	}
}

static void mctp_i2c_flow_release(struct mctp_i2c_dev *midev)
{
	unsigned long flags;
	bool unlock;

	spin_lock_irqsave(&midev->lock, flags);
	if (midev->release_count > midev->i2c_lock_count) {
		WARN_ONCE(1, "release count overflow");
		midev->release_count = midev->i2c_lock_count;
	}

	midev->i2c_lock_count -= midev->release_count;
	unlock = midev->i2c_lock_count == 0 && midev->release_count > 0;
	midev->release_count = 0;
	spin_unlock_irqrestore(&midev->lock, flags);

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

	if (len > MCTP_I2C_MAXMTU)
		return -EMSGSIZE;

	lldst = *((u8 *)daddr);
	llsrc = *((u8 *)saddr);

	skb_push(skb, sizeof(struct mctp_i2c_hdr));
	skb_reset_mac_header(skb);
	hdr = (void *)skb_mac_header(skb);
	mhdr = mctp_hdr(skb);
	hdr->dest_slave = (lldst << 1) & 0xff;
	hdr->command = MCTP_I2C_COMMANDCODE;
	hdr->byte_count = len + 1;
	hdr->source_slave = ((llsrc << 1) & 0xff) | 0x01;
	mhdr->ver = 0x01;

	return sizeof(struct mctp_i2c_hdr);
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
			wait_event_idle(midev->tx_wq,
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

	spin_lock_irqsave(&midev->lock, flags);
	midev->release_count++;
	spin_unlock_irqrestore(&midev->lock, flags);

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
	.ndo_uninit = mctp_i2c_ndo_uninit,
	.ndo_open = mctp_i2c_ndo_open,
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
}

/* Populates the mctp_i2c_dev priv struct for a netdev.
 * Returns an error pointer on failure.
 */
static struct mctp_i2c_dev *mctp_i2c_midev_init(struct net_device *dev,
						struct mctp_i2c_client *mcli,
						struct i2c_adapter *adap)
{
	struct mctp_i2c_dev *midev = netdev_priv(dev);
	unsigned long flags;

	midev->tx_thread = kthread_create(mctp_i2c_tx_thread, midev,
					  "%s/tx", dev->name);
	if (IS_ERR(midev->tx_thread))
		return ERR_CAST(midev->tx_thread);

	midev->ndev = dev;
	get_device(&adap->dev);
	midev->adapter = adap;
	get_device(&mcli->client->dev);
	midev->client = mcli;
	INIT_LIST_HEAD(&midev->list);
	spin_lock_init(&midev->lock);
	midev->i2c_lock_count = 0;
	midev->release_count = 0;
	init_completion(&midev->rx_done);
	complete(&midev->rx_done);
	init_waitqueue_head(&midev->tx_wq);
	skb_queue_head_init(&midev->tx_queue);

	/* Add to the parent mcli */
	spin_lock_irqsave(&mcli->sel_lock, flags);
	list_add(&midev->list, &mcli->devs);
	/* Select a device by default */
	if (!mcli->sel)
		__mctp_i2c_device_select(mcli, midev);
	spin_unlock_irqrestore(&mcli->sel_lock, flags);

	/* Start the worker thread */
	wake_up_process(midev->tx_thread);

	return midev;
}

/* Counterpart of mctp_i2c_midev_init */
static void mctp_i2c_midev_free(struct mctp_i2c_dev *midev)
{
	struct mctp_i2c_client *mcli = midev->client;
	unsigned long flags;

	if (midev->tx_thread) {
		kthread_stop(midev->tx_thread);
		midev->tx_thread = NULL;
	}

	/* Unconditionally unlock on close */
	mctp_i2c_unlock_reset(midev);

	/* Remove the netdev from the parent i2c client. */
	spin_lock_irqsave(&mcli->sel_lock, flags);
	list_del(&midev->list);
	if (mcli->sel == midev) {
		struct mctp_i2c_dev *first;

		first = list_first_entry_or_null(&mcli->devs, struct mctp_i2c_dev, list);
		__mctp_i2c_device_select(mcli, first);
	}
	spin_unlock_irqrestore(&mcli->sel_lock, flags);

	skb_queue_purge(&midev->tx_queue);
	put_device(&midev->adapter->dev);
	put_device(&mcli->client->dev);
}

/* Stops, unregisters, and frees midev */
static void mctp_i2c_unregister(struct mctp_i2c_dev *midev)
{
	unsigned long flags;

	/* Stop tx thread prior to unregister, it uses netif_() functions */
	kthread_stop(midev->tx_thread);
	midev->tx_thread = NULL;

	/* Prevent any new rx in mctp_i2c_recv(), let any pending work finish */
	spin_lock_irqsave(&midev->lock, flags);
	midev->allow_rx = false;
	spin_unlock_irqrestore(&midev->lock, flags);
	wait_for_completion(&midev->rx_done);

	mctp_unregister_netdev(midev->ndev);
	/* midev has been freed now by mctp_i2c_ndo_uninit callback */

	free_netdev(midev->ndev);
}

static void mctp_i2c_ndo_uninit(struct net_device *dev)
{
	struct mctp_i2c_dev *midev = netdev_priv(dev);

	/* Perform cleanup here to ensure that mcli->sel isn't holding
	 * a reference that would prevent unregister_netdevice()
	 * from completing.
	 */
	mctp_i2c_midev_free(midev);
}

static int mctp_i2c_ndo_open(struct net_device *dev)
{
	struct mctp_i2c_dev *midev = netdev_priv(dev);
	unsigned long flags;

	/* i2c rx handler can only pass packets once the netdev is registered */
	spin_lock_irqsave(&midev->lock, flags);
	midev->allow_rx = true;
	spin_unlock_irqrestore(&midev->lock, flags);

	return 0;
}

static int mctp_i2c_add_netdev(struct mctp_i2c_client *mcli,
			       struct i2c_adapter *adap)
{
	struct mctp_i2c_dev *midev = NULL;
	struct net_device *ndev = NULL;
	struct i2c_adapter *root;
	unsigned long flags;
	char namebuf[30];
	int rc;

	root = mux_root_adapter(adap);
	if (root != mcli->client->adapter) {
		dev_err(&mcli->client->dev,
			"I2C adapter %s is not a child bus of %s\n",
			mcli->client->adapter->name, root->name);
		return -EINVAL;
	}

	WARN_ON(!mutex_is_locked(&driver_clients_lock));
	snprintf(namebuf, sizeof(namebuf), "mctpi2c%d", adap->nr);
	ndev = alloc_netdev(sizeof(*midev), namebuf, NET_NAME_ENUM, mctp_i2c_net_setup);
	if (!ndev) {
		dev_err(&mcli->client->dev, "alloc netdev failed\n");
		rc = -ENOMEM;
		goto err;
	}
	dev_net_set(ndev, current->nsproxy->net_ns);
	SET_NETDEV_DEV(ndev, &adap->dev);
	dev_addr_set(ndev, &mcli->lladdr);

	midev = mctp_i2c_midev_init(ndev, mcli, adap);
	if (IS_ERR(midev)) {
		rc = PTR_ERR(midev);
		midev = NULL;
		goto err;
	}

	rc = mctp_register_netdev(ndev, &mctp_i2c_mctp_ops);
	if (rc < 0) {
		dev_err(&mcli->client->dev,
			"register netdev \"%s\" failed %d\n",
			ndev->name, rc);
		goto err;
	}

	spin_lock_irqsave(&midev->lock, flags);
	midev->allow_rx = false;
	spin_unlock_irqrestore(&midev->lock, flags);

	return 0;
err:
	if (midev)
		mctp_i2c_midev_free(midev);
	if (ndev)
		free_netdev(ndev);
	return rc;
}

/* Removes any netdev for adap. mcli is the parent root i2c client */
static void mctp_i2c_remove_netdev(struct mctp_i2c_client *mcli,
				   struct i2c_adapter *adap)
{
	struct mctp_i2c_dev *midev = NULL, *m = NULL;
	unsigned long flags;

	WARN_ON(!mutex_is_locked(&driver_clients_lock));
	spin_lock_irqsave(&mcli->sel_lock, flags);
	/* List size is limited by number of MCTP netdevs on a single hardware bus */
	list_for_each_entry(m, &mcli->devs, list)
		if (m->adapter == adap) {
			midev = m;
			break;
		}
	spin_unlock_irqrestore(&mcli->sel_lock, flags);

	if (midev)
		mctp_i2c_unregister(midev);
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
	WARN_ONCE(!root, "MCTP I2C failed to find root adapter for %s\n",
		  dev_name(dev));
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
	/* Must either have mctp-controller property on the adapter, or
	 * be a root adapter if it's non-devicetree
	 */
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
	/* Check for mctp-controller property on the adapter */
	if (!mctp_i2c_adapter_match(adap, false))
		return;

	/* Find an existing mcli for adap's root */
	mutex_lock(&driver_clients_lock);
	list_for_each_entry(m, &driver_clients, list) {
		if (m->client->adapter == root) {
			mcli = m;
			break;
		}
	}

	if (mcli) {
		rc = mctp_i2c_add_netdev(mcli, adap);
		if (rc < 0)
			dev_warn(dev, "Failed adding mctp-i2c net device\n");
	}
	mutex_unlock(&driver_clients_lock);
}

static void mctp_i2c_notify_del(struct device *dev)
{
	struct i2c_adapter *root = NULL, *adap = NULL;
	struct mctp_i2c_client *mcli = NULL;

	adap = mctp_i2c_get_adapter(dev, &root);
	if (!adap)
		return;

	mutex_lock(&driver_clients_lock);
	list_for_each_entry(mcli, &driver_clients, list) {
		if (mcli->client->adapter == root) {
			mctp_i2c_remove_netdev(mcli, adap);
			break;
		}
	}
	mutex_unlock(&driver_clients_lock);
}

static int mctp_i2c_probe(struct i2c_client *client)
{
	struct mctp_i2c_client *mcli = NULL;
	int rc;

	mutex_lock(&driver_clients_lock);
	mcli = mctp_i2c_new_client(client);
	if (IS_ERR(mcli)) {
		rc = PTR_ERR(mcli);
		mcli = NULL;
		goto out;
	} else {
		list_add(&mcli->list, &driver_clients);
	}

	/* Add a netdev for adapters that have a 'mctp-controller' property */
	i2c_for_each_dev(mcli, mctp_i2c_client_try_attach);
	rc = 0;
out:
	mutex_unlock(&driver_clients_lock);
	return rc;
}

static int mctp_i2c_remove(struct i2c_client *client)
{
	struct mctp_i2c_client *mcli = i2c_get_clientdata(client);
	struct mctp_i2c_dev *midev = NULL, *tmp = NULL;

	mutex_lock(&driver_clients_lock);
	list_del(&mcli->list);
	/* Remove all child adapter netdevs */
	list_for_each_entry_safe(midev, tmp, &mcli->devs, list)
		mctp_i2c_unregister(midev);

	mctp_i2c_free_client(mcli);
	mutex_unlock(&driver_clients_lock);
	/* Callers ignore return code */
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
	{ "mctp-i2c-interface", 0 },
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
		.name = "mctp-i2c-interface",
		.of_match_table = mctp_i2c_of_match,
	},
	.probe_new = mctp_i2c_probe,
	.remove = mctp_i2c_remove,
	.id_table = mctp_i2c_id,
};

static __init int mctp_i2c_mod_init(void)
{
	int rc;

	pr_info("MCTP I2C interface driver\n");
	rc = i2c_add_driver(&mctp_i2c_driver);
	if (rc < 0)
		return rc;
	rc = bus_register_notifier(&i2c_bus_type, &mctp_i2c_notifier);
	if (rc < 0) {
		i2c_del_driver(&mctp_i2c_driver);
		return rc;
	}
	return 0;
}

static __exit void mctp_i2c_mod_exit(void)
{
	int rc;

	rc = bus_unregister_notifier(&i2c_bus_type, &mctp_i2c_notifier);
	if (rc < 0)
		pr_warn("MCTP I2C could not unregister notifier, %d\n", rc);
	i2c_del_driver(&mctp_i2c_driver);
}

module_init(mctp_i2c_mod_init);
module_exit(mctp_i2c_mod_exit);

MODULE_DESCRIPTION("MCTP I2C device");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Matt Johnston <matt@codeconstruct.com.au>");
