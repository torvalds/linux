// SPDX-License-Identifier: GPL-2.0-only
//Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

/*
 * MHI Device Network interface
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/ipc_logging.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/ktime.h>
#include <linux/platform_device.h>
#include <linux/etherdevice.h>
#include <linux/of.h>
#include <linux/list.h>

#include "mhi.h"

#define MHI_NET_DRIVER_NAME  "mhi_dev_net_drv"
#define MHI_NET_DEV_NAME     "mhi_swip%d"
#define MHI_NET_DEFAULT_MTU   16384
#define MHI_NET_ETH_HEADER_SIZE	(18)
#define MHI_NET_IPC_PAGES     (100)
#define MHI_MAX_RX_REQ        (128)
#define MHI_MAX_TX_REQ        (128)
#define MHI_DEFAULT_NUM_OF_NW_CLIENTS 1
#define MAX_MHI_INSTANCES      17
#define MHI_PF_ID              0
#define MAX_NUM_OF_CLIENTS     16

enum mhi_dev_net_dbg_lvl {
	MHI_VERBOSE = 0x1,
	MHI_INFO = 0x2,
	MHI_DBG = 0x3,
	MHI_WARNING = 0x4,
	MHI_ERROR = 0x5,
	MHI_CRITICAL = 0x6,
	MSG_NET_reserved = 0x80000000
};

static enum mhi_dev_net_dbg_lvl mhi_net_msg_lvl = MHI_ERROR;
static enum mhi_dev_net_dbg_lvl mhi_net_ipc_log_lvl = MHI_VERBOSE;
static void *mhi_net_vf_ipc_log[MAX_MHI_INSTANCES];
enum mhi_chan_dir {
	MHI_DIR_INVALID = 0x0,
	MHI_DIR_OUT = 0x1,
	MHI_DIR_IN = 0x2,
	MHI_DIR__reserved = 0x80000000
};

struct mhi_dev_net_chan_attr {
	/* SW maintained channel id */
	enum mhi_client_channel chan_id;
	/* maximum buffer size for this channel */
	size_t max_packet_size;
	/* direction of the channel, see enum mhi_chan_dir */
	enum mhi_chan_dir dir;
};

static struct mhi_dev_net_chan_attr mhi_chan_attr_table_netdev[] = {
	{
		MHI_CLIENT_IP_SW_4_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_4_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_5_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_5_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_6_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_6_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_7_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_7_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_8_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_8_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_9_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_9_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_10_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_10_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_11_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_11_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_12_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_12_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_13_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_13_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_14_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_14_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_15_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_15_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_16_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_16_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_17_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_17_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_18_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_18_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
	{
		MHI_CLIENT_IP_SW_19_OUT,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_OUT,
	},
	{
		MHI_CLIENT_IP_SW_19_IN,
		TRB_MAX_DATA_SIZE,
		MHI_DIR_IN,
	},
};

#define CHAN_TO_CLIENT(_CHAN_NR) (_CHAN_NR / 2)

#define mhi_dev_net_log(vf_id, _msg_lvl, _msg, ...) do { \
	if (_msg_lvl >= mhi_net_msg_lvl) { \
		pr_err("[%s] "_msg, __func__, ##__VA_ARGS__); \
	} \
	if (mhi_net_vf_ipc_log[vf_id] && (_msg_lvl >= mhi_net_ipc_log_lvl)) { \
		ipc_log_string(mhi_net_vf_ipc_log[vf_id],                     \
			"[%s] " _msg, __func__, ##__VA_ARGS__);     \
	} \
} while (0)

struct mhi_dev_net_client {
	/* MHI instance id (mhi pf = 0, vf = 1..n) */
	u32 vf_id;
	/* write channel - always even*/
	u32 out_chan;
	/* read channel - always odd */
	u32 in_chan;
	bool eth_iface;
	u32 max_skb_length;
	struct mhi_dev_client *out_handle;
	struct mhi_dev_client *in_handle;
	struct mhi_dev_net_chan_attr *in_chan_attr;
	struct mhi_dev_net_chan_attr *out_chan_attr;
	/*process pendig packets */
	struct workqueue_struct *pending_pckt_wq;
	struct work_struct       xmit_work;
	/*Read data from host work queue*/
	atomic_t  rx_enabled;
	atomic_t  tx_enabled;
	struct net_device *dev;
	struct sk_buff_head tx_buffers;
	struct list_head rx_buffers;
	struct list_head wr_req_buffers;
	struct mhi_dev_net_ctxt *net_ctxt;
	/*To check write channel is empty or not*/
	spinlock_t wrt_lock;
	spinlock_t rd_lock;
	spinlock_t net_tx_q_state;
};

struct mhi_dev_net_ctxt {
	struct mhi_dev_net_chan_attr chan_attr[MHI_MAX_SOFTWARE_CHANNELS];
	struct mhi_dev_net_client **client_handles;
	struct platform_device		*pdev;
	void (*net_event_notifier)(struct mhi_dev_client_cb_reason *cb);
	uint32_t num_mhi_instances;
	struct mhi_dev_ops *dev_ops;
	/* outbound channel that uses eth interface */
	uint32_t *eth_iface_out_ch;
	/* TX and RX Reqs  */
	u32 tx_reqs;
	u32 rx_reqs;
	u32 mhi_num_nw_client_limit;
};

static struct mhi_dev_net_ctxt mhi_net_ctxt;
static ssize_t mhi_dev_net_client_read(struct mhi_dev_net_client *);

static struct mhi_dev_net_client *chan_to_net_client(u32 vf_id, u32 chan)
{
	struct mhi_dev_net_client *client_handle = NULL;
	u32 i, client = 0;

	for (i = 0; i < mhi_net_ctxt.mhi_num_nw_client_limit; i++) {
		client = i + (vf_id * mhi_net_ctxt.mhi_num_nw_client_limit);
		client_handle = mhi_net_ctxt.client_handles[client];
		if (chan == client_handle->in_chan || chan == client_handle->out_chan)
			return client_handle;
	}
	return NULL;
}

static int mhi_dev_net_init_ch_attributes(struct mhi_dev_net_client *client,
		struct mhi_dev_net_chan_attr *chan_attrib)
{
	int i = 0, num_mhi_eth_chan = 0;

	client->out_chan_attr = chan_attrib;
	client->in_chan_attr = ++chan_attrib;

	num_mhi_eth_chan = of_property_count_elems_of_size((&mhi_net_ctxt.pdev->dev)->of_node,
			"qcom,mhi-ethernet-interface-ch-list", sizeof(u32));

	if ((num_mhi_eth_chan < 0) ||
			(num_mhi_eth_chan > mhi_net_ctxt.mhi_num_nw_client_limit)) {
		mhi_dev_net_log(MHI_PF_ID, MHI_INFO,
				"size of qcom,mhi-ethernet-interface-ch-list is not valid\n");
	} else {
		for (i = 0; i < num_mhi_eth_chan; i++) {
			if (mhi_net_ctxt.eth_iface_out_ch[i] == client->out_chan_attr->chan_id) {
				client->eth_iface = true;
				break;
			}
		}
	}

	mhi_dev_net_log(client->vf_id, MHI_INFO, "Write ch attributes dir %d ch_id %d, %s\n",
			client->out_chan_attr->dir, client->out_chan_attr->chan_id,
			client->eth_iface ? "Uses eth i/f":"");
	mhi_dev_net_log(client->vf_id, MHI_INFO, "Read ch attributes dir %d ch_id %d\n",
			client->in_chan_attr->dir, client->in_chan_attr->chan_id);
	return 0;
}

static void mhi_dev_net_process_queue_packets(struct work_struct *work)
{
	struct mhi_dev_net_client *client = container_of(work,
			struct mhi_dev_net_client, xmit_work);
	unsigned long flags = 0;
	int xfer_data = 0;
	struct sk_buff *skb = NULL;
	struct mhi_req *wreq = NULL;

	spin_lock(&client->net_tx_q_state);
	if (mhi_net_ctxt.dev_ops->is_channel_empty(client->in_handle)) {
		mhi_dev_net_log(client->vf_id, MHI_INFO, "stop network xmmit\n");
		netif_stop_queue(client->dev);
		spin_unlock(&client->net_tx_q_state);
		return;
	}
	spin_unlock(&client->net_tx_q_state);

	while (!((skb_queue_empty(&client->tx_buffers)) ||
			(list_empty(&client->wr_req_buffers)))) {
		spin_lock_irqsave(&client->wrt_lock, flags);
		skb = skb_dequeue(&(client->tx_buffers));
		if (!skb) {
			mhi_dev_net_log(client->vf_id, MHI_INFO,
					"SKB is NULL from dequeue\n");
			spin_unlock_irqrestore(&client->wrt_lock, flags);
			return;
		}
		wreq = container_of(client->wr_req_buffers.next,
				struct mhi_req, list);
		list_del_init(&wreq->list);

		wreq->client = client->in_handle;
		wreq->vf_id = client->vf_id;
		wreq->context = skb;
		wreq->buf = skb->data;
		wreq->len = skb->len;
		wreq->chan = client->in_chan;
		wreq->mode = DMA_ASYNC;
		if (skb_queue_empty(&client->tx_buffers) ||
				list_empty(&client->wr_req_buffers)) {
			wreq->snd_cmpl = 1;
		} else
			wreq->snd_cmpl = 0;
		spin_unlock_irqrestore(&client->wrt_lock, flags);
		xfer_data = mhi_net_ctxt.dev_ops->write_channel(wreq);
		if (xfer_data <= 0) {
			mhi_dev_net_log(client->vf_id, MHI_ERROR,
					"Failed to write skb len %d\n",
					 skb->len);
			kfree_skb(skb);
			return;
		}
		client->dev->stats.tx_packets++;

		/* Check if free buffers are available*/
		spin_lock(&client->net_tx_q_state);
		if (mhi_net_ctxt.dev_ops->is_channel_empty(client->in_handle)) {
			mhi_dev_net_log(client->vf_id, MHI_INFO,
					"buffers are full stop xmit\n");
			netif_stop_queue(client->dev);
			spin_unlock(&client->net_tx_q_state);
			break;
		}
		spin_unlock(&client->net_tx_q_state);
	} /* While TX queue is not empty */
}

static void mhi_dev_net_event_notifier(struct mhi_dev_client_cb_reason *reason)
{
	struct mhi_dev_net_client *client_handle =
				chan_to_net_client(reason->vf_id, reason->ch_id);

	if (!client_handle) {
		mhi_dev_net_log(reason->vf_id, MHI_ERROR,
				"Failed to assign client handle\n");
		return;
	}

	if (reason->reason == MHI_DEV_TRE_AVAILABLE) {
		if (reason->ch_id % 2) {
			spin_lock(&client_handle->net_tx_q_state);
			if (netif_queue_stopped(client_handle->dev)) {
				netif_wake_queue(client_handle->dev);
				queue_work(client_handle->pending_pckt_wq,
						&client_handle->xmit_work);
			}
			spin_unlock(&client_handle->net_tx_q_state);
		} else
			mhi_dev_net_client_read(client_handle);
	}
}

static __be16 mhi_dev_net_eth_type_trans(struct sk_buff *skb)
{
	__be16 protocol = 0;
	/* Determine L3 protocol */
	switch (skb->data[0] & 0xf0) {
	case 0x40:
		protocol = htons(ETH_P_IP);
		break;
	case 0x60:
		protocol = htons(ETH_P_IPV6);
		break;
	default:
		/* Default is QMAP */
		protocol = htons(ETH_P_MAP);
		break;
	}
	return protocol;
}

static void mhi_dev_net_read_completion_cb(void *req)
{
	struct mhi_req *mreq = (struct mhi_req *)req;
	struct mhi_dev_net_client *net_handle;
	struct sk_buff *skb = mreq->context;
	unsigned long   flags;

	net_handle = chan_to_net_client(mreq->vf_id, mreq->chan);

	if (!net_handle) {
		mhi_dev_net_log(mreq->vf_id, MHI_ERROR,
				"Failed to assign client handle\n");
		return;
	}

	skb_put(skb, mreq->transfer_len);

	if (net_handle->eth_iface)
		skb->protocol = eth_type_trans(skb, net_handle->dev);
	else
		skb->protocol = mhi_dev_net_eth_type_trans(skb);

	net_handle->dev->stats.rx_packets++;
	skb->dev = net_handle->dev;
	netif_rx(skb);
	spin_lock_irqsave(&net_handle->rd_lock, flags);
	list_add_tail(&mreq->list, &net_handle->rx_buffers);
	spin_unlock_irqrestore(&net_handle->rd_lock, flags);
}

static ssize_t mhi_dev_net_client_read(struct mhi_dev_net_client *mhi_handle)
{
	int bytes_avail = 0;
	int ret_val = 0;
	u32 chan = 0;
	struct mhi_dev_client *client_handle = NULL;
	struct mhi_req *req;
	struct sk_buff *skb;
	unsigned long   flags;

	client_handle = mhi_handle->out_handle;
	chan = mhi_handle->out_chan;
	if (!atomic_read(&mhi_handle->rx_enabled))
		return -EPERM;
	while (1) {
		spin_lock_irqsave(&mhi_handle->rd_lock, flags);
		if (list_empty(&mhi_handle->rx_buffers)) {
			spin_unlock_irqrestore(&mhi_handle->rd_lock, flags);
			break;
		}

		req = container_of(mhi_handle->rx_buffers.next,
				struct mhi_req, list);
		list_del_init(&req->list);
		spin_unlock_irqrestore(&mhi_handle->rd_lock, flags);
		skb = alloc_skb(mhi_handle->max_skb_length, GFP_KERNEL);
		if (skb == NULL) {
			mhi_dev_net_log(mhi_handle->vf_id, MHI_ERROR, "skb alloc failed\n");
			spin_lock_irqsave(&mhi_handle->rd_lock, flags);
			list_add_tail(&req->list, &mhi_handle->rx_buffers);
			spin_unlock_irqrestore(&mhi_handle->rd_lock, flags);
			ret_val = -ENOMEM;
			return ret_val;
		}

		req->client = client_handle;
		req->vf_id = mhi_handle->vf_id;
		req->chan = chan;
		req->buf = skb->data;
		req->len = mhi_handle->max_skb_length;
		req->context = skb;
		req->mode = DMA_ASYNC;
		req->snd_cmpl = 0;
		bytes_avail = mhi_net_ctxt.dev_ops->read_channel(req);

		if (bytes_avail < 0) {
			mhi_dev_net_log(mhi_handle->vf_id, MHI_ERROR,
					"Failed to read ch_id:%d bytes_avail = %d\n",
					chan, bytes_avail);
			spin_lock_irqsave(&mhi_handle->rd_lock, flags);
			kfree_skb(skb);
			list_add_tail(&req->list, &mhi_handle->rx_buffers);
			spin_unlock_irqrestore(&mhi_handle->rd_lock, flags);
			ret_val = -EIO;
			return 0;
		}
		/* no data to send to network stack, break */
		if (!bytes_avail) {
			spin_lock_irqsave(&mhi_handle->rd_lock, flags);
			kfree_skb(skb);
			list_add_tail(&req->list, &mhi_handle->rx_buffers);
			spin_unlock_irqrestore(&mhi_handle->rd_lock, flags);
			return 0;
		}
	}
	/* coming out while only in case of no data or error */
	return ret_val;

}

static void mhi_dev_net_write_completion_cb(void *req)
{
	struct mhi_req *wreq = (struct mhi_req *)req;
	struct mhi_dev_net_client *client_handle =
				chan_to_net_client(wreq->vf_id, wreq->chan);
	struct sk_buff *skb = wreq->context;
	unsigned long   flags;

	kfree_skb(skb);

	if (!client_handle) {
		mhi_dev_net_log(wreq->vf_id, MHI_ERROR,
				"Failed to assign client handle\n");
		return;
	}
	spin_lock_irqsave(&client_handle->wrt_lock, flags);
	list_add_tail(&wreq->list, &client_handle->wr_req_buffers);
	spin_unlock_irqrestore(&client_handle->wrt_lock, flags);
}

static int mhi_dev_net_alloc_write_reqs(struct mhi_dev_net_client *client)
{
	int nreq = 0, rc = 0;
	struct mhi_req *wreq;

	while (nreq < mhi_net_ctxt.tx_reqs) {
		wreq = kzalloc(sizeof(struct mhi_req), GFP_ATOMIC);
		if (!wreq)
			return -ENOMEM;
		wreq->client_cb =  mhi_dev_net_write_completion_cb;
		list_add_tail(&wreq->list, &client->wr_req_buffers);
		nreq++;
	}
	mhi_dev_net_log(client->vf_id, MHI_INFO,
			"mhi write reqs allocation success\n");
	return rc;

}

static int mhi_dev_net_alloc_read_reqs(struct mhi_dev_net_client *client)
{
	int nreq = 0, rc = 0;
	struct mhi_req *mreq;

	while (nreq < mhi_net_ctxt.rx_reqs) {
		mreq = kzalloc(sizeof(struct mhi_req), GFP_ATOMIC);
		if (!mreq)
			return -ENOMEM;
		mreq->len =  TRB_MAX_DATA_SIZE;
		mreq->client_cb =  mhi_dev_net_read_completion_cb;
		list_add_tail(&mreq->list, &client->rx_buffers);
		nreq++;
	}
	mhi_dev_net_log(client->vf_id, MHI_INFO,
			"mhi read reqs allocation success\n");
	return rc;

}

static int mhi_dev_net_open(struct net_device *dev)
{
	struct mhi_dev_net_client *mhi_dev_net_ptr =
		*(struct mhi_dev_net_client **)netdev_priv(dev);
	mhi_dev_net_log(mhi_dev_net_ptr->vf_id,  MHI_INFO,
			"mhi_net_dev interface is up for IN %d OUT %d\n",
			mhi_dev_net_ptr->out_chan,
			mhi_dev_net_ptr->in_chan);
	netif_start_queue(dev);
	return 0;
}

static netdev_tx_t mhi_dev_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mhi_dev_net_client *mhi_dev_net_ptr =
			*(struct mhi_dev_net_client **)netdev_priv(dev);
	unsigned long flags;

	if (skb->len <= 0) {
		mhi_dev_net_log(mhi_dev_net_ptr->vf_id, MHI_ERROR,
				"Invalid skb received freeing skb\n");
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	spin_lock_irqsave(&mhi_dev_net_ptr->wrt_lock, flags);
	skb_queue_tail(&(mhi_dev_net_ptr->tx_buffers), skb);
	spin_unlock_irqrestore(&mhi_dev_net_ptr->wrt_lock, flags);

	queue_work(mhi_dev_net_ptr->pending_pckt_wq,
			&mhi_dev_net_ptr->xmit_work);

	return NETDEV_TX_OK;
}

static int mhi_dev_net_stop(struct net_device *dev)
{
	struct mhi_dev_net_client *mhi_dev_net_ptr =
			*(struct mhi_dev_net_client **)netdev_priv(dev);

	netif_stop_queue(dev);
	mhi_dev_net_log(mhi_dev_net_ptr->vf_id, MHI_VERBOSE,
			"mhi_dev_net interface is down\n");
	return 0;
}

static int mhi_dev_net_change_mtu(struct net_device *dev, int new_mtu)
{
	struct mhi_dev_net_client *mhi_dev_net_ptr;

	if (0 > new_mtu || MHI_NET_DEFAULT_MTU < new_mtu)
		return -EINVAL;
	mhi_dev_net_ptr = *((struct mhi_dev_net_client **)netdev_priv(dev));
	dev->mtu = new_mtu;

	if (mhi_dev_net_ptr->eth_iface)
		mhi_dev_net_ptr->max_skb_length = dev->mtu + MHI_NET_ETH_HEADER_SIZE;
	else
		mhi_dev_net_ptr->max_skb_length = dev->mtu;

	return 0;
}

static const struct net_device_ops mhi_dev_net_ops_ip = {
	.ndo_open = mhi_dev_net_open,
	.ndo_stop = mhi_dev_net_stop,
	.ndo_start_xmit = mhi_dev_net_xmit,
	.ndo_change_mtu = mhi_dev_net_change_mtu,
};

static void mhi_dev_net_rawip_setup(struct net_device *dev)
{
	dev->netdev_ops = &mhi_dev_net_ops_ip;
	ether_setup(dev);

	/* set this after calling ether_setup */
	dev->header_ops = NULL;
	dev->type = ARPHRD_RAWIP;
	dev->hard_header_len = 0;
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = ETH_MAX_MTU;
	dev->mtu = MHI_NET_DEFAULT_MTU;
	dev->addr_len = 0;
	dev->flags &= ~(IFF_BROADCAST | IFF_MULTICAST);
}

static void mhi_dev_net_ether_setup(struct net_device *dev)
{
	dev->netdev_ops = &mhi_dev_net_ops_ip;
	ether_setup(dev);
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = ETH_MAX_MTU;
	dev->mtu = MHI_NET_DEFAULT_MTU;
}

static int mhi_dev_net_enable_iface(struct mhi_dev_net_client *mhi_dev_net_ptr)
{
	int ret = 0;
	struct mhi_dev_net_client **mhi_dev_net_ctxt = NULL;
	struct net_device *netdev;
	char dev_name[64];
	u32 vf_id = 0;

	if (!mhi_dev_net_ptr)
		return -EINVAL;

	vf_id = mhi_dev_net_ptr->vf_id;
	if (vf_id)
		scnprintf(dev_name, sizeof(dev_name), "mhi%d_swip%%d", vf_id);
	else
		scnprintf(dev_name, sizeof(dev_name), "mhi_swip%%d");

	/* Initialize skb list head to queue the packets for mhi dev client */
	skb_queue_head_init(&(mhi_dev_net_ptr->tx_buffers));

	mhi_dev_net_log(vf_id, MHI_INFO,
			"mhi_dev_net interface registration\n");
	netdev = alloc_netdev(sizeof(struct mhi_dev_net_client),
			dev_name, NET_NAME_PREDICTABLE,
			mhi_dev_net_ptr->eth_iface ?
			mhi_dev_net_ether_setup :
			mhi_dev_net_rawip_setup);
	if (mhi_dev_net_ptr->eth_iface)
		mhi_dev_net_log(vf_id, MHI_INFO,
				"mhi_dev_net Ethernet setup\n");
	else
		mhi_dev_net_log(vf_id, MHI_INFO,
				"mhi_dev_net Raw IP setup\n");
	if (!netdev) {
		mhi_dev_net_log(vf_id, MHI_ERROR,
			"Failed to allocate netdev for mhi_dev_net\n");
		goto net_dev_alloc_fail;
	}

	if (mhi_dev_net_ptr->eth_iface) {
		mhi_dev_net_ptr->max_skb_length = netdev->mtu + MHI_NET_ETH_HEADER_SIZE;
		u8 temp_addr[ETH_ALEN];

		eth_random_addr(temp_addr);
		__dev_addr_set(netdev, temp_addr, ETH_ALEN);
		if (!is_valid_ether_addr(netdev->dev_addr))
			return -EADDRNOTAVAIL;
	} else
		mhi_dev_net_ptr->max_skb_length = netdev->mtu;

	mhi_dev_net_ctxt = netdev_priv(netdev);
	mhi_dev_net_ptr->dev = netdev;
	*mhi_dev_net_ctxt = mhi_dev_net_ptr;
	ret = register_netdev(mhi_dev_net_ptr->dev);
	if (ret) {
		mhi_dev_net_log(vf_id, MHI_ERROR,
				"Failed to register mhi_dev_net device\n");
		goto net_dev_reg_fail;
	}
	mhi_dev_net_log(vf_id, MHI_INFO, "Successfully registred mhi_dev_net\n");
	return 0;

net_dev_reg_fail:
	free_netdev(mhi_dev_net_ptr->dev);
net_dev_alloc_fail:
	mhi_net_ctxt.dev_ops->close_channel(mhi_dev_net_ptr->in_handle);
	mhi_net_ctxt.dev_ops->close_channel(mhi_dev_net_ptr->out_handle);
	mhi_dev_net_ptr->dev = NULL;
	return -ENOMEM;
}

static int mhi_dev_net_open_chan_create_netif(struct mhi_dev_net_client *client)
{
	int rc = 0;
	int ret = 0;
	struct list_head *cp, *q;
	struct mhi_req *mreq;
	u32 vf_id = client->vf_id;

	mhi_dev_net_log(vf_id, MHI_DBG, "opening OUT ch_id:%d IN ch_id:%d channels\n",
			client->out_chan,
			client->in_chan);
	mhi_dev_net_log(vf_id, MHI_DBG,
			"Initializing inbound ch_id:%d.\n",
			client->in_chan);

	rc = mhi_net_ctxt.dev_ops->open_channel(vf_id,
				     client->out_chan,
				     &client->out_handle,
				     mhi_net_ctxt.net_event_notifier);
	if (rc < 0) {
		mhi_dev_net_log(vf_id, MHI_ERROR,
				"Failed to open ch_id:%d, ret 0x%x\n",
				client->out_chan, rc);
		goto handle_not_rdy_err;
	} else
		atomic_set(&client->rx_enabled, 1);

	rc = mhi_net_ctxt.dev_ops->open_channel(vf_id,
				     client->in_chan,
				     &client->in_handle,
				     mhi_net_ctxt.net_event_notifier);
	if (rc < 0) {
		mhi_dev_net_log(vf_id, MHI_ERROR,
				"Failed to open ch_id:%d, ret 0x%x\n",
				client->in_chan, rc);
		goto handle_in_err;
	} else
		atomic_set(&client->tx_enabled, 1);

	mhi_dev_net_log(vf_id, MHI_INFO, "IN ch_id:%d, OUT ch_id:%d channels are opened",
			client->in_chan, client->out_chan);

	INIT_LIST_HEAD(&client->rx_buffers);
	INIT_LIST_HEAD(&client->wr_req_buffers);
	/* pre allocate read request buffer */

	ret = mhi_dev_net_alloc_read_reqs(client);
	if (ret) {
		mhi_dev_net_log(vf_id, MHI_ERROR,
			"failed to allocate rx req buffers\n");
		goto rx_req_failed;
	}
	ret = mhi_dev_net_alloc_write_reqs(client);
	if (ret) {
		mhi_dev_net_log(vf_id, MHI_ERROR,
			"failed to allocate write req buffers\n");
		goto tx_req_failed;
	}
	if (atomic_read(&client->tx_enabled)) {
		ret = mhi_dev_net_enable_iface(client);
		if (ret < 0)
			mhi_dev_net_log(vf_id, MHI_ERROR,
					"failed to enable mhi_dev_net iface\n");
	}
	return ret;
tx_req_failed:
	list_for_each_safe(cp, q, &client->rx_buffers);
	mreq = list_entry(cp, struct mhi_req, list);
	list_del(cp);
	kfree(mreq);
rx_req_failed:
	mhi_net_ctxt.dev_ops->close_channel(client->in_handle);
handle_in_err:
	mhi_net_ctxt.dev_ops->close_channel(client->out_handle);
handle_not_rdy_err:
	return rc;
}

static int mhi_dev_net_close(void)
{
	struct mhi_dev_net_client *client;
	u32 i, num_mhi = mhi_net_ctxt.num_mhi_instances;

	for (i = 0; i < mhi_net_ctxt.mhi_num_nw_client_limit * num_mhi; i++) {
		client = mhi_net_ctxt.client_handles[i];
		if (!client)
			continue;
		mhi_dev_net_log(client->vf_id, MHI_INFO,
				"mhi_dev_net module is removed for vf = %d\n",
				client->vf_id);
		mhi_net_ctxt.dev_ops->close_channel(client->out_handle);
		atomic_set(&client->tx_enabled, 0);
		mhi_net_ctxt.dev_ops->close_channel(client->in_handle);
		atomic_set(&client->rx_enabled, 0);
		if (client->dev != NULL) {
			netif_stop_queue(client->dev);
			unregister_netdev(client->dev);
			free_netdev(client->dev);
			client->dev = NULL;
		}
		/* freeing mhi client and IPC context */
		kfree(client);
	}
	for (i = 0; i < ARRAY_SIZE(mhi_net_vf_ipc_log); i++)
		kfree(mhi_net_vf_ipc_log[i]);

	return 0;
}

static int mhi_dev_net_rgstr_client(struct mhi_dev_net_client *client, int idx)
{
	client->out_chan = idx;
	client->in_chan = idx + 1;
	spin_lock_init(&client->wrt_lock);
	spin_lock_init(&client->net_tx_q_state);
	spin_lock_init(&client->rd_lock);
	mhi_dev_net_log(client->vf_id, MHI_INFO, "Registering OUT ch_id:%d\t"
			"IN ch_id:%d channels\n",
			client->out_chan, client->in_chan);
	return 0;
}

static void mhi_dev_net_free_reqs(struct list_head *buff)
{
	struct list_head *node, *next;
	struct mhi_req *mreq;

	list_for_each_safe(node, next, buff) {
		mreq = list_entry(node, struct mhi_req, list);
		list_del(&mreq->list);
		kfree(mreq);
	}
}

static void mhi_dev_net_state_cb(struct mhi_dev_client_cb_data *cb_data)
{
	struct mhi_dev_net_client *mhi_client;
	uint32_t info_in_ch = 0, info_out_ch = 0;
	int ret;

	if (!cb_data || !cb_data->user_data) {
		mhi_dev_net_log(MHI_PF_ID, MHI_ERROR, "invalid input received\n");
		return;
	}
	mhi_client = cb_data->user_data;

	ret = mhi_net_ctxt.dev_ops->ctrl_state_info(mhi_client->vf_id,
				     mhi_client->in_chan,
				     &info_in_ch);
	if (ret) {
		mhi_dev_net_log(mhi_client->vf_id, MHI_ERROR,
			"Failed to obtain IN ch_id:%d state\n",
			mhi_client->in_chan);
		return;
	}
	ret = mhi_net_ctxt.dev_ops->ctrl_state_info(mhi_client->vf_id,
				     mhi_client->out_chan,
				     &info_out_ch);
	if (ret) {
		mhi_dev_net_log(mhi_client->vf_id, MHI_ERROR,
			"Failed to obtain OUT ch_id:%d state\n",
			mhi_client->out_chan);
		return;
	}
	mhi_dev_net_log(mhi_client->vf_id, MHI_VERBOSE, "IN ch_id::%d, state :%d\n",
			mhi_client->in_chan, info_in_ch);
	mhi_dev_net_log(mhi_client->vf_id, MHI_VERBOSE, "OUT ch_id:%d, state :%d\n",
			mhi_client->out_chan, info_out_ch);
	if (info_in_ch == MHI_STATE_CONNECTED &&
		info_out_ch == MHI_STATE_CONNECTED) {
		/**
		 * Open IN and OUT channels for Network client
		 * and create Network Interface.
		 */
		ret = mhi_dev_net_open_chan_create_netif(mhi_client);
		if (ret) {
			mhi_dev_net_log(mhi_client->vf_id, MHI_ERROR,
				"Failed to open channels\n");
			return;
		}
	} else if (info_in_ch == MHI_STATE_DISCONNECTED ||
				info_out_ch == MHI_STATE_DISCONNECTED) {
		if (mhi_client->dev != NULL) {
			netif_stop_queue(mhi_client->dev);
			unregister_netdev(mhi_client->dev);
			mhi_net_ctxt.dev_ops->close_channel(mhi_client->out_handle);
			atomic_set(&mhi_client->tx_enabled, 0);
			mhi_net_ctxt.dev_ops->close_channel(mhi_client->in_handle);
			atomic_set(&mhi_client->rx_enabled, 0);
			mhi_dev_net_free_reqs(&mhi_client->rx_buffers);
			mhi_dev_net_free_reqs(&mhi_client->wr_req_buffers);
			free_netdev(mhi_client->dev);
			mhi_client->dev = NULL;
		}
	}
}

int mhi_dev_net_interface_init(struct mhi_dev_ops *dev_ops, uint32_t vf_id, uint32_t num_vfs)
{
	u32 i = 0, j = 0, idx = 0;
	int ret_val = 0;
	uint32_t info_out_ch = 0, max_clients = 0;
	struct mhi_dev_net_client **mhi_net_client = kcalloc(mhi_net_ctxt.mhi_num_nw_client_limit,
						sizeof(struct mhi_dev_net_client *), GFP_KERNEL);
	char mhi_net_vf_ipc_name[12] = "mhi-net-nn";

	if (!mhi_net_client) {
		mhi_dev_net_log(vf_id, MHI_ERROR, "Memory alloc failed for mhi_net_client\n");
		return -ENOMEM;
	}

	if (!mhi_net_ctxt.client_handles) {
		/*
		 * 2D array to hold handles of all net dev clients
		 * across mhi functions (pf and vfs).
		 */
		max_clients = (num_vfs + 1) * mhi_net_ctxt.mhi_num_nw_client_limit;
		mhi_net_ctxt.client_handles =
			kcalloc(max_clients, sizeof(struct mhi_dev_net_client *),
				GFP_KERNEL);
		if (!mhi_net_ctxt.client_handles)
			return -ENOMEM;

		mhi_net_ctxt.num_mhi_instances = num_vfs;

		/* TODO - make ipc logging MHI function specific */
	}

	mhi_net_ctxt.dev_ops = dev_ops;
	if (!mhi_net_vf_ipc_log[vf_id]) {
		snprintf(mhi_net_vf_ipc_name, sizeof(mhi_net_vf_ipc_name), "mhi-net-%d", vf_id);

		mhi_net_vf_ipc_log[vf_id] = ipc_log_context_create(MHI_NET_IPC_PAGES,
						mhi_net_vf_ipc_name, 0);
		if (!mhi_net_vf_ipc_log[vf_id])
			pr_err("Failed to create IPC logging for mhi_dev_net VF %d\n", vf_id);
	}

	/* Ensure net dev i/f for a given mhi function initailized only once */
	if (mhi_net_ctxt.client_handles[vf_id * mhi_net_ctxt.mhi_num_nw_client_limit]) {
		mhi_dev_net_log(vf_id, MHI_INFO,
			"MHI net-dev interface for %s-MHI = %d already initialized\n",
			(vf_id == 0) ? "physical":"virtual", vf_id);
		return ret_val;
	}

	for (i = 0; i < mhi_net_ctxt.mhi_num_nw_client_limit; i++) {

		mhi_net_client[i] =
			kzalloc(sizeof(struct mhi_dev_net_client), GFP_KERNEL);
		if (mhi_net_client[i] == NULL)
			goto mem_alloc_fail;

		idx = i + (vf_id * mhi_net_ctxt.mhi_num_nw_client_limit);
		mhi_net_ctxt.client_handles[idx] = mhi_net_client[i];

		/* Store mhi instance id for future usage */
		mhi_net_client[i]->vf_id = vf_id;

		/*Process pending packet work queue*/
		mhi_net_client[i]->pending_pckt_wq =
			alloc_ordered_workqueue("%s", __WQ_LEGACY |
				WQ_MEM_RECLAIM | WQ_HIGHPRI, "pending_xmit_pckt_wq");
		INIT_WORK(&mhi_net_client[i]->xmit_work,
			mhi_dev_net_process_queue_packets);

		mhi_dev_net_log(vf_id, MHI_INFO,
		"Registering for MHI transfer events from host for client=%d on vf=%d\n",
				 i, vf_id);
		mhi_net_ctxt.net_event_notifier = mhi_dev_net_event_notifier;

		ret_val = mhi_dev_net_init_ch_attributes(mhi_net_client[i],
				&mhi_chan_attr_table_netdev[i * 2]);
		if (ret_val < 0) {
			mhi_dev_net_log(vf_id, MHI_ERROR,
					"Failed to init client attributes\n");
			goto init_failed;
		}
		mhi_dev_net_log(vf_id, MHI_DBG, "Initializing client\n");

		ret_val = mhi_dev_net_rgstr_client(mhi_net_client[i],
				mhi_net_client[i]->out_chan_attr->chan_id);
		if (ret_val) {
			mhi_dev_net_log(vf_id, MHI_CRITICAL,
				"Failed to reg client %d ret 0\n", ret_val);
			goto init_failed;
		}

		ret_val = dev_ops->register_state_cb(mhi_dev_net_state_cb,
						   mhi_net_client[i],
						   mhi_net_client[i]->out_chan,
						   vf_id);
		if (ret_val < 0 && ret_val != -EEXIST)
			goto init_failed;

		ret_val = dev_ops->register_state_cb(mhi_dev_net_state_cb,
						   mhi_net_client[i],
						   mhi_net_client[i]->in_chan,
						   vf_id);
		/* -EEXIST indicates success and channel is already open */
		if (ret_val == -EEXIST) {
			/**
			 * If both in and out channels were opened by host at the
			 * time of registration proceed with opening channels and
			 * create network interface from device side.
			 * if the channels are not opened at the time of registration
			 * we will get a call back notification mhi_dev_net_state_cb()
			 * and proceed to open channels and create network interface
			 * with mhi_dev_net_open_chan_create_netif().
			 */
			ret_val = 0;
			if (!dev_ops->ctrl_state_info(vf_id,
						    mhi_net_client[i]->out_chan,
						    &info_out_ch)) {
				if (info_out_ch == MHI_STATE_CONNECTED) {
					ret_val = mhi_dev_net_open_chan_create_netif
							(mhi_net_client[i]);
					if (ret_val < 0) {
						mhi_dev_net_log(vf_id, MHI_ERROR,
							"Failed to open channels\n");
						goto init_failed;
					}
				}
			}
		} else if (ret_val < 0) {
			goto init_failed;
		}
	}

	return ret_val;

mem_alloc_fail:
	ret_val = -ENOMEM;
init_failed:
	for (j = i ; j > 0; j--) {
		destroy_workqueue(mhi_net_client[j]->pending_pckt_wq);
		kfree(mhi_net_client[j]);
	}
	return ret_val;
}
EXPORT_SYMBOL_GPL(mhi_dev_net_interface_init);

void mhi_dev_net_exit(void)
{
	mhi_dev_net_log(MHI_PF_ID, MHI_INFO,
			"MHI Network Interface Module exited\n");
	mhi_dev_net_close();
}
EXPORT_SYMBOL_GPL(mhi_dev_net_exit);

static int mhi_dev_net_probe(struct platform_device *pdev)
{
	int ret = 0, num_mhi_eth_chan = 0, i = 0;
	uint32_t reqs = 0;

	if (pdev->dev.of_node) {
		ret = of_property_read_u32((&pdev->dev)->of_node,
						"qcom,mhi-num-nw-client-limit",
						&mhi_net_ctxt.mhi_num_nw_client_limit);
		if (ret) {
			mhi_dev_net_log(MHI_PF_ID, MHI_INFO,
					"Network client limit is not supplied from the device tree\n");
			mhi_net_ctxt.mhi_num_nw_client_limit = MHI_DEFAULT_NUM_OF_NW_CLIENTS;
		}

		if (mhi_net_ctxt.mhi_num_nw_client_limit > MAX_NUM_OF_CLIENTS)
			mhi_dev_net_log(MHI_PF_ID, MHI_INFO,
					"Network client limit= %d should not be greater than %d\n",
					mhi_net_ctxt.mhi_num_nw_client_limit, MAX_NUM_OF_CLIENTS);
		else
			mhi_dev_net_log(MHI_PF_ID, MHI_INFO,
					"Network client limit= %d\n",
					mhi_net_ctxt.mhi_num_nw_client_limit);

		mhi_net_ctxt.pdev = pdev;

		num_mhi_eth_chan = of_property_count_elems_of_size((&pdev->dev)->of_node,
				"qcom,mhi-ethernet-interface-ch-list", sizeof(u32));

		if ((num_mhi_eth_chan < 0) ||
				(num_mhi_eth_chan > mhi_net_ctxt.mhi_num_nw_client_limit)) {
			mhi_dev_net_log(MHI_PF_ID, MHI_INFO,
					"size of qcom,mhi-ethernet-interface-ch-list is not valid\n");
		} else {
			mhi_net_ctxt.eth_iface_out_ch =
				kcalloc(num_mhi_eth_chan, sizeof(uint32_t), GFP_KERNEL);
			if (!mhi_net_ctxt.eth_iface_out_ch) {
				mhi_dev_net_log(MHI_PF_ID, MHI_ERROR,
						"Memory alloc failed for mhi_net_ctxt.eth_iface_out_ch\n");
				return -ENOMEM;
			}

			ret = of_property_read_u32_array((&pdev->dev)->of_node,
					"qcom,mhi-ethernet-interface-ch-list",
					mhi_net_ctxt.eth_iface_out_ch, num_mhi_eth_chan);

			if (ret)
				mhi_dev_net_log(MHI_PF_ID, MHI_INFO,
						"qcom,mhi-ethernet-interface-ch-list is invalid\n");
			else {
				for (i = 0; i < num_mhi_eth_chan; i++) {
					mhi_dev_net_log(MHI_PF_ID, MHI_INFO,
							"mhi_net_ctxt.eth_iface_out_ch[%d]=%d\n", i,
							mhi_net_ctxt.eth_iface_out_ch[i]);
					mhi_dev_net_log(MHI_PF_ID, MHI_ERROR,
							"mhi_net_ctxt.eth_iface_out_ch[%d]=%d\n", i,
							mhi_net_ctxt.eth_iface_out_ch[i]);
					if (mhi_net_ctxt.eth_iface_out_ch[i]) {
						mhi_dev_net_log(MHI_PF_ID, MHI_INFO,
								"Channel %d uses ethernet interface\n",
								mhi_net_ctxt.eth_iface_out_ch[i]);
						mhi_dev_net_log(MHI_PF_ID, MHI_ERROR,
								"Channel %d uses ethernet interface\n",
								mhi_net_ctxt.eth_iface_out_ch[i]);
					}
				}
			}
		}

		ret = of_property_read_u32((&mhi_net_ctxt.pdev->dev)->of_node,
						"qcom,tx_rx_reqs", &reqs);
		if (ret < 0) {
			mhi_net_ctxt.tx_reqs = MHI_MAX_TX_REQ;
			mhi_net_ctxt.rx_reqs = MHI_MAX_RX_REQ;
		} else {
			mhi_net_ctxt.tx_reqs = reqs;
			mhi_net_ctxt.rx_reqs = reqs;
		}

	}

	mhi_dev_net_log(MHI_PF_ID, MHI_INFO, "MHI Network probe success\n");
	return 0;
}

static int mhi_dev_net_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id mhi_dev_net_match_table[] = {
	{	.compatible = "qcom,msm-mhi-dev-net" },
	{}
};

static struct platform_driver mhi_dev_net_driver = {
	.driver		= {
		.name	= "qcom,msm-mhi-dev-net",
		.of_match_table = mhi_dev_net_match_table,
	},
	.probe		= mhi_dev_net_probe,
	.remove		= mhi_dev_net_remove,
};

static int __init mhi_dev_net_init(void)
{
	return platform_driver_register(&mhi_dev_net_driver);
}
subsys_initcall(mhi_dev_net_init);

static void __exit mhi_dev_exit(void)
{
	platform_driver_unregister(&mhi_dev_net_driver);
}
module_exit(mhi_dev_exit);

MODULE_DESCRIPTION("MHI net device driver");
MODULE_LICENSE("GPL");
