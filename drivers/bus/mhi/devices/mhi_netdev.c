// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/msm_rmnet.h>
#include <linux/if_arp.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/ipc_logging.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/of_device.h>
#include <linux/rtnetlink.h>
#include <linux/kthread.h>
#include <linux/mhi.h>
#include <linux/mhi_misc.h>

#define MHI_NETDEV_DRIVER_NAME "mhi_netdev"
#define WATCHDOG_TIMEOUT (30 * HZ)
#define IPC_LOG_PAGES (100)
#define MAX_NETBUF_SIZE (128)
#define MHI_NETDEV_NAPI_POLL_WEIGHT (64)

#ifdef CONFIG_MHI_BUS_DEBUG
#define MHI_NETDEV_LOG_LVL MHI_MSG_LVL_VERBOSE
#else
#define MHI_NETDEV_LOG_LVL MHI_MSG_LVL_ERROR
#endif

#define MSG_VERB(fmt, ...) do { \
	if (mhi_netdev->ipc_log && mhi_netdev->msg_lvl <= MHI_MSG_LVL_VERBOSE) \
		ipc_log_string(mhi_netdev->ipc_log, "%s[D][%s] " fmt, \
			      "",  __func__, ##__VA_ARGS__); \
} while (0)

#define MSG_LOG(fmt, ...) do { \
	if (mhi_netdev->ipc_log && mhi_netdev->msg_lvl <= MHI_MSG_LVL_INFO) \
		ipc_log_string(mhi_netdev->ipc_log, "%s[I][%s] " fmt, \
				"", __func__, ##__VA_ARGS__); \
} while (0)

#define MSG_ERR(fmt, ...) do { \
	pr_err("[E][%s] " fmt, __func__, ##__VA_ARGS__);\
	if (mhi_netdev->ipc_log && mhi_netdev->msg_lvl <= MHI_MSG_LVL_ERROR) \
		ipc_log_string(mhi_netdev->ipc_log, "%s[E][%s] " fmt, \
				"", __func__, ##__VA_ARGS__); \
} while (0)

static const char * const mhi_log_level_str[MHI_MSG_LVL_MAX] = {
	[MHI_MSG_LVL_VERBOSE] = "Verbose",
	[MHI_MSG_LVL_INFO] = "Info",
	[MHI_MSG_LVL_ERROR] = "Error",
	[MHI_MSG_LVL_CRITICAL] = "Critical",
	[MHI_MSG_LVL_MASK_ALL] = "Mask all",
};
#define MHI_NETDEV_LOG_LEVEL_STR(level) ((level >= MHI_MSG_LVL_MAX || \
					 !mhi_log_level_str[level]) ? \
					 "Mask all" : mhi_log_level_str[level])

struct mhi_net_chain {
	struct sk_buff *head, *tail; /* chained skb */
};

struct mhi_netdev {
	struct mhi_device *mhi_dev;
	struct mhi_netdev *rsc_dev; /* rsc linked node */
	struct mhi_netdev *rsc_parent;
	bool is_rsc_dev;
	int wake;

	u32 mru;
	u32 order;
	const char *interface_name;
	struct napi_struct *napi;
	struct net_device *ndev;

	struct list_head *recycle_pool;
	int pool_size;
	bool chain_skb;
	struct mhi_net_chain *chain;

	struct task_struct *alloc_task;
	wait_queue_head_t alloc_event;
	int bg_pool_limit; /* minimum pool size */
	int bg_pool_size; /* current size of the pool */
	struct list_head *bg_pool;
	spinlock_t bg_lock; /* lock to access list */


	struct dentry *dentry;
	enum MHI_DEBUG_LEVEL msg_lvl;
	void *ipc_log;

	/* debug stats */
	u32 abuffers, kbuffers, rbuffers;
	bool napi_scheduled;
};

struct mhi_netdev_priv {
	struct mhi_netdev *mhi_netdev;
};

/* Try not to make this structure bigger than 128 bytes, since this take space
 * in payload packet.
 * Example: If MRU = 16K, effective MRU = 16K - sizeof(mhi_netbuf)
 */
struct mhi_netbuf {
	struct mhi_buf mhi_buf; /* this must be first element */
	bool recycle;
	struct page *page;
	struct list_head node;
	void (*unmap)(struct device *dev, dma_addr_t addr, size_t size,
		      enum dma_data_direction dir);
};

struct mhi_netdev_driver_data {
	u32 mru;
	bool chain_skb;
	bool is_rsc_chan;
	bool has_rsc_child;
	const char *interface_name;
};

static struct mhi_netdev *rsc_parent_netdev;
static struct mhi_driver mhi_netdev_driver;
static void mhi_netdev_create_debugfs(struct mhi_netdev *mhi_netdev);

static __be16 mhi_netdev_ip_type_trans(u8 data)
{
	__be16 protocol = htons(ETH_P_MAP);

	/* determine L3 protocol */
	switch (data & 0xf0) {
	case 0x40:
		/* length must be 5 at a minimum to support 20 byte IP header */
		if ((data & 0x0f) > 4)
			protocol = htons(ETH_P_IP);
		break;
	case 0x60:
		protocol = htons(ETH_P_IPV6);
		break;
	default:
		/* default is already QMAP */
		break;
	}
	return protocol;
}

static struct mhi_netbuf *mhi_netdev_alloc(struct device *dev,
					   gfp_t gfp,
					   unsigned int order)
{
	struct page *page;
	struct mhi_netbuf *netbuf;
	struct mhi_buf *mhi_buf;
	void *vaddr;

	page = __dev_alloc_pages(gfp | __GFP_NOMEMALLOC, order);
	if (!page)
		return NULL;

	vaddr = page_address(page);

	/* we going to use the end of page to store cached data */
	netbuf = vaddr + (PAGE_SIZE << order) - sizeof(*netbuf);
	netbuf->recycle = false;
	netbuf->page = page;
	mhi_buf = (struct mhi_buf *)netbuf;
	mhi_buf->buf = vaddr;
	mhi_buf->len = (void *)netbuf - vaddr;

	if (!dev)
		return netbuf;

	mhi_buf->dma_addr = dma_map_page(dev, page, 0, mhi_buf->len,
					 DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, mhi_buf->dma_addr)) {
		__free_pages(netbuf->page, order);
		return NULL;
	}

	return netbuf;
}

static void mhi_netdev_unmap_page(struct device *dev,
				  dma_addr_t dma_addr,
				  size_t len,
				  enum dma_data_direction dir)
{
	dma_unmap_page(dev, dma_addr, len, dir);
}

static int mhi_netdev_tmp_alloc(struct mhi_netdev *mhi_netdev,
				struct mhi_device *mhi_dev,
				int nr_tre)
{
	struct device *dev = mhi_dev->dev.parent->parent;
	const u32 order = mhi_netdev->order;
	int i, ret;

	for (i = 0; i < nr_tre; i++) {
		struct mhi_buf *mhi_buf;
		struct mhi_netbuf *netbuf = mhi_netdev_alloc(dev, GFP_ATOMIC,
							     order);
		if (!netbuf)
			return -ENOMEM;

		mhi_buf = (struct mhi_buf *)netbuf;
		netbuf->unmap = mhi_netdev_unmap_page;

		ret = mhi_queue_dma(mhi_dev, DMA_FROM_DEVICE, mhi_buf,
				    mhi_buf->len, MHI_EOT);
		if (unlikely(ret)) {
			MSG_ERR("Failed to queue transfer, ret:%d\n", ret);
			mhi_netdev_unmap_page(dev, mhi_buf->dma_addr,
					      mhi_buf->len, DMA_FROM_DEVICE);
			__free_pages(netbuf->page, order);
			return ret;
		}
		mhi_netdev->abuffers++;
	}

	return 0;
}

static int mhi_netdev_queue_bg_pool(struct mhi_netdev *mhi_netdev,
				    struct mhi_device *mhi_dev,
				    int nr_tre)
{
	struct device *dev = mhi_dev->dev.parent->parent;
	int i, ret;
	LIST_HEAD(head);

	spin_lock_bh(&mhi_netdev->bg_lock);
	list_splice_init(mhi_netdev->bg_pool, &head);
	spin_unlock_bh(&mhi_netdev->bg_lock);

	for (i = 0; i < nr_tre; i++) {
		struct mhi_netbuf *net_buf =
			list_first_entry_or_null(&head, struct mhi_netbuf, node);
		struct mhi_buf *mhi_buf = (struct mhi_buf *)net_buf;

		if (!mhi_buf)
			break;

		mhi_buf->dma_addr = dma_map_page(dev, net_buf->page, 0,
						 mhi_buf->len, DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, mhi_buf->dma_addr))
			break;

		net_buf->unmap = mhi_netdev_unmap_page;
		ret = mhi_queue_dma(mhi_dev, DMA_FROM_DEVICE, mhi_buf,
				    mhi_buf->len, MHI_EOT);
		if (unlikely(ret)) {
			MSG_ERR("Failed to queue transfer, ret: %d\n", ret);
			mhi_netdev_unmap_page(dev, mhi_buf->dma_addr,
					      mhi_buf->len, DMA_FROM_DEVICE);
			break;
		}
		list_del(&net_buf->node);
		mhi_netdev->kbuffers++;
	}

	/* add remaining buffers back to main pool */
	spin_lock_bh(&mhi_netdev->bg_lock);
	list_splice(&head, mhi_netdev->bg_pool);
	mhi_netdev->bg_pool_size -= i;
	spin_unlock_bh(&mhi_netdev->bg_lock);


	/* wake up the bg thread to allocate more buffers */
	wake_up_interruptible(&mhi_netdev->alloc_event);

	return i;
}

static void mhi_netdev_queue(struct mhi_netdev *mhi_netdev,
			     struct mhi_device *mhi_dev)
{
	struct device *dev = mhi_dev->dev.parent->parent;
	struct mhi_netbuf *netbuf, *temp_buf;
	struct mhi_buf *mhi_buf;
	struct list_head *pool = mhi_netdev->recycle_pool;
	int nr_tre = mhi_get_free_desc_count(mhi_dev, DMA_FROM_DEVICE);
	int i, ret;
	const int  max_peek = 4;

	MSG_VERB("Enter free descriptors: %d\n", nr_tre);

	if (!nr_tre)
		return;

	/* try going thru reclaim pool first */
	for (i = 0; i < nr_tre; i++) {
		/* peek for the next buffer, we going to peak several times,
		 * and we going to give up if buffers are not yet free
		 */
		int peek = 0;

		netbuf = NULL;
		list_for_each_entry(temp_buf, pool, node) {
			mhi_buf = (struct mhi_buf *)temp_buf;
			/* page == 1 idle, buffer is free to reclaim */
			if (page_ref_count(temp_buf->page) == 1) {
				netbuf = temp_buf;
				break;
			}

			if (peek++ >= max_peek)
				break;
		}

		/* could not find a free buffer */
		if (!netbuf)
			break;

		/* increment reference count so when network stack is done
		 * with buffer, the buffer won't be freed
		 */
		page_ref_inc(temp_buf->page);
		list_del(&temp_buf->node);
		dma_sync_single_for_device(dev, mhi_buf->dma_addr, mhi_buf->len,
					   DMA_FROM_DEVICE);
		ret = mhi_queue_dma(mhi_dev, DMA_FROM_DEVICE, mhi_buf,
				    mhi_buf->len, MHI_EOT);
		if (unlikely(ret)) {
			MSG_ERR("Failed to queue buffer, ret: %d\n", ret);
			netbuf->unmap(dev, mhi_buf->dma_addr, mhi_buf->len,
				      DMA_FROM_DEVICE);
			page_ref_dec(temp_buf->page);
			list_add(&temp_buf->node, pool);
			return;
		}
		mhi_netdev->rbuffers++;
	}

	/* recycling did not work, buffers are still busy use bg pool */
	if (i < nr_tre)
		i += mhi_netdev_queue_bg_pool(mhi_netdev, mhi_dev, nr_tre - i);

	/* recyling did not work, buffers are still busy allocate temp pkts */
	if (i < nr_tre)
		mhi_netdev_tmp_alloc(mhi_netdev, mhi_dev, nr_tre - i);
}

/* allocating pool of memory */
static int mhi_netdev_alloc_pool(struct mhi_netdev *mhi_netdev)
{
	int i;
	struct mhi_netbuf *netbuf, *tmp;
	struct mhi_buf *mhi_buf;
	const u32 order = mhi_netdev->order;
	struct device *dev = mhi_netdev->mhi_dev->dev.parent->parent;
	struct list_head *pool = kmalloc(sizeof(*pool), GFP_KERNEL);

	if (!pool)
		return -ENOMEM;

	INIT_LIST_HEAD(pool);

	for (i = 0; i < mhi_netdev->pool_size; i++) {
		/* allocate paged data */
		netbuf = mhi_netdev_alloc(dev, GFP_KERNEL, order);
		if (!netbuf)
			goto error_alloc_page;

		netbuf->unmap = dma_sync_single_for_cpu;
		netbuf->recycle = true;
		mhi_buf = (struct mhi_buf *)netbuf;
		list_add(&netbuf->node, pool);
	}

	mhi_netdev->recycle_pool = pool;

	return 0;

error_alloc_page:
	list_for_each_entry_safe(netbuf, tmp, pool, node) {
		list_del(&netbuf->node);
		mhi_buf = (struct mhi_buf *)netbuf;
		dma_unmap_page(dev, mhi_buf->dma_addr, mhi_buf->len,
			       DMA_FROM_DEVICE);
		__free_pages(netbuf->page, order);
	}

	kfree(pool);

	return -ENOMEM;
}

static void mhi_netdev_free_pool(struct mhi_netdev *mhi_netdev)
{
	struct device *dev = mhi_netdev->mhi_dev->dev.parent->parent;
	struct mhi_netbuf *netbuf, *tmp;
	struct mhi_buf *mhi_buf;

	list_for_each_entry_safe(netbuf, tmp, mhi_netdev->recycle_pool, node) {
		list_del(&netbuf->node);
		mhi_buf = (struct mhi_buf *)netbuf;
		dma_unmap_page(dev, mhi_buf->dma_addr, mhi_buf->len,
			       DMA_FROM_DEVICE);
		__free_pages(netbuf->page, mhi_netdev->order);
	}
	kfree(mhi_netdev->recycle_pool);

	/* free the bg pool */
	list_for_each_entry_safe(netbuf, tmp, mhi_netdev->bg_pool, node) {
		list_del(&netbuf->node);
		__free_pages(netbuf->page, mhi_netdev->order);
		mhi_netdev->bg_pool_size--;
	}

	kfree(mhi_netdev->bg_pool);
}

static int mhi_netdev_alloc_thread(void *data)
{
	struct mhi_netdev *mhi_netdev = data;
	struct mhi_netbuf *netbuf, *tmp_buf;
	struct mhi_buf *mhi_buf;
	const u32 order = mhi_netdev->order;
	LIST_HEAD(head);

	while (!kthread_should_stop()) {
		while (mhi_netdev->bg_pool_size <= mhi_netdev->bg_pool_limit) {
			int buffers = 0, i;

			/* do a bulk allocation */
			for (i = 0; i < NAPI_POLL_WEIGHT; i++) {
				if (kthread_should_stop())
					goto exit_alloc;

				netbuf = mhi_netdev_alloc(NULL, GFP_KERNEL,
							  order);
				if (!netbuf)
					continue;

				mhi_buf = (struct mhi_buf *)netbuf;
				list_add(&netbuf->node, &head);
				buffers++;
			}

			/* add the list to main pool */
			spin_lock_bh(&mhi_netdev->bg_lock);
			list_splice_init(&head, mhi_netdev->bg_pool);
			mhi_netdev->bg_pool_size += buffers;
			spin_unlock_bh(&mhi_netdev->bg_lock);
		}

		/* replenish the ring */
		napi_schedule(mhi_netdev->napi);
		mhi_netdev->napi_scheduled = true;

		/* wait for buffers to run low or thread to stop */
		wait_event_interruptible(mhi_netdev->alloc_event,
			kthread_should_stop() ||
			mhi_netdev->bg_pool_size <= mhi_netdev->bg_pool_limit);
	}

exit_alloc:
	list_for_each_entry_safe(netbuf, tmp_buf, &head, node) {
		list_del(&netbuf->node);
		__free_pages(netbuf->page, order);
	}

	return 0;
}

static int mhi_netdev_poll(struct napi_struct *napi, int budget)
{
	struct net_device *dev = napi->dev;
	struct mhi_netdev_priv *mhi_netdev_priv = netdev_priv(dev);
	struct mhi_netdev *mhi_netdev = mhi_netdev_priv->mhi_netdev;
	struct mhi_device *mhi_dev = mhi_netdev->mhi_dev;
	struct mhi_netdev *rsc_dev = mhi_netdev->rsc_dev;
	struct mhi_net_chain *chain = mhi_netdev->chain;
	int rx_work = 0;

	MSG_VERB("Enter: %d\n", budget);

	rx_work = mhi_poll(mhi_dev, budget);

	/* chained skb, push it to stack */
	if (chain && chain->head) {
		netif_receive_skb(chain->head);
		chain->head = NULL;
	}

	if (rx_work < 0) {
		MSG_ERR("Error polling ret: %d\n", rx_work);
		napi_complete(napi);
		mhi_netdev->napi_scheduled = false;
		return 0;
	}

	/* queue new buffers */
	mhi_netdev_queue(mhi_netdev, mhi_dev);

	if (rsc_dev)
		mhi_netdev_queue(mhi_netdev, rsc_dev->mhi_dev);

	/* complete work if # of packet processed less than allocated budget */
	if (rx_work < budget) {
		napi_complete(napi);
		mhi_netdev->napi_scheduled = false;
	}

	MSG_VERB("Polled: %d\n", rx_work);

	return rx_work;
}

static int mhi_netdev_open(struct net_device *dev)
{
	struct mhi_netdev_priv *mhi_netdev_priv = netdev_priv(dev);
	struct mhi_netdev *mhi_netdev = mhi_netdev_priv->mhi_netdev;
	struct mhi_device *mhi_dev = mhi_netdev->mhi_dev;

	MSG_LOG("Opened netdev interface\n");

	/* tx queue may not necessarily be stopped already
	 * so stop the queue if tx path is not enabled
	 */
	if (!mhi_dev->ul_chan)
		netif_stop_queue(dev);
	else
		netif_start_queue(dev);

	return 0;

}

static int mhi_netdev_change_mtu(struct net_device *dev, int new_mtu)
{

	if (new_mtu < 0 || MHI_MAX_MTU < new_mtu)
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

static netdev_tx_t mhi_netdev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mhi_netdev_priv *mhi_netdev_priv = netdev_priv(dev);
	struct mhi_netdev *mhi_netdev = mhi_netdev_priv->mhi_netdev;
	struct mhi_device *mhi_dev = mhi_netdev->mhi_dev;
	netdev_tx_t res = NETDEV_TX_OK;
	int ret;

	MSG_VERB("Entered\n");

	ret = mhi_queue_skb(mhi_dev, DMA_TO_DEVICE, skb, skb->len,
			    MHI_EOT);
	if (ret) {
		MSG_VERB("Failed to queue with reason: %d\n", res);
		netif_stop_queue(dev);
		res = NETDEV_TX_BUSY;
	}

	MSG_VERB("Exited\n");

	return res;
}

static int mhi_netdev_ioctl_extended(struct net_device *dev, struct ifreq *ifr)
{
	struct rmnet_ioctl_extended_s ext_cmd;
	int rc = 0;
	struct mhi_netdev_priv *mhi_netdev_priv = netdev_priv(dev);
	struct mhi_netdev *mhi_netdev = mhi_netdev_priv->mhi_netdev;
	struct mhi_device *mhi_dev = mhi_netdev->mhi_dev;

	rc = copy_from_user(&ext_cmd, ifr->ifr_ifru.ifru_data,
			    sizeof(struct rmnet_ioctl_extended_s));
	if (rc)
		return rc;

	switch (ext_cmd.extended_ioctl) {
	case RMNET_IOCTL_GET_SUPPORTED_FEATURES:
		ext_cmd.u.data = 0;
		break;
	case RMNET_IOCTL_GET_DRIVER_NAME:
		strscpy(ext_cmd.u.if_name, mhi_netdev->interface_name,
			sizeof(ext_cmd.u.if_name));
		break;
	case RMNET_IOCTL_SET_SLEEP_STATE:
		if (ext_cmd.u.data && mhi_netdev->wake) {
			/* Request to enable LPM */
			MSG_VERB("Enable MHI LPM\n");
			mhi_netdev->wake--;
			mhi_device_put(mhi_dev);
		} else if (!ext_cmd.u.data && !mhi_netdev->wake) {
			/* Request to disable LPM */
			MSG_VERB("Disable MHI LPM\n");
			mhi_netdev->wake++;
			mhi_device_get(mhi_dev);
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}

	rc = copy_to_user(ifr->ifr_ifru.ifru_data, &ext_cmd,
			  sizeof(struct rmnet_ioctl_extended_s));
	return rc;
}

static int mhi_netdev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int rc = 0;
	struct rmnet_ioctl_data_s ioctl_data;

	switch (cmd) {
	case RMNET_IOCTL_SET_LLP_IP: /* set RAWIP protocol */
		break;
	case RMNET_IOCTL_GET_LLP: /* get link protocol state */
		ioctl_data.u.operation_mode = RMNET_MODE_LLP_IP;
		if (copy_to_user(ifr->ifr_ifru.ifru_data, &ioctl_data,
		    sizeof(struct rmnet_ioctl_data_s)))
			rc = -EFAULT;
		break;
	case RMNET_IOCTL_GET_OPMODE: /* get operation mode */
		ioctl_data.u.operation_mode = RMNET_MODE_LLP_IP;
		if (copy_to_user(ifr->ifr_ifru.ifru_data, &ioctl_data,
		    sizeof(struct rmnet_ioctl_data_s)))
			rc = -EFAULT;
		break;
	case RMNET_IOCTL_SET_QOS_ENABLE:
		rc = -EINVAL;
		break;
	case RMNET_IOCTL_SET_QOS_DISABLE:
		rc = 0;
		break;
	case RMNET_IOCTL_OPEN:
	case RMNET_IOCTL_CLOSE:
		/* we just ignore them and return success */
		rc = 0;
		break;
	case RMNET_IOCTL_EXTENDED:
		rc = mhi_netdev_ioctl_extended(dev, ifr);
		break;
	default:
		/* don't fail any IOCTL right now */
		rc = 0;
		break;
	}

	return rc;
}

static const struct net_device_ops mhi_netdev_ops_ip = {
	.ndo_open = mhi_netdev_open,
	.ndo_start_xmit = mhi_netdev_xmit,
	.ndo_do_ioctl = mhi_netdev_ioctl,
	.ndo_change_mtu = mhi_netdev_change_mtu,
	.ndo_set_mac_address = 0,
	.ndo_validate_addr = 0,
};

static void mhi_netdev_setup(struct net_device *dev)
{
	dev->netdev_ops = &mhi_netdev_ops_ip;
	ether_setup(dev);

	/* set this after calling ether_setup */
	dev->header_ops = 0;  /* No header */
	dev->type = ARPHRD_RAWIP;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->flags &= ~(IFF_BROADCAST | IFF_MULTICAST);
	dev->watchdog_timeo = WATCHDOG_TIMEOUT;
}

/* enable mhi_netdev netdev, call only after grabbing mhi_netdev.mutex */
static int mhi_netdev_enable_iface(struct mhi_netdev *mhi_netdev)
{
	int ret = 0;
	char ifname[IFNAMSIZ];
	struct mhi_device *mhi_dev = mhi_netdev->mhi_dev;
	struct mhi_netdev_priv *mhi_netdev_priv;

	snprintf(ifname, sizeof(ifname), "%s%%d", mhi_netdev->interface_name);

	rtnl_lock();
	mhi_netdev->ndev = alloc_netdev(sizeof(*mhi_netdev_priv),
					ifname, NET_NAME_PREDICTABLE,
					mhi_netdev_setup);
	if (!mhi_netdev->ndev) {
		rtnl_unlock();
		return -ENOMEM;
	}

	mhi_netdev->ndev->mtu = mhi_dev->mhi_cntrl->buffer_len;

	SET_NETDEV_DEV(mhi_netdev->ndev, &mhi_dev->dev);
	mhi_netdev_priv = netdev_priv(mhi_netdev->ndev);
	mhi_netdev_priv->mhi_netdev = mhi_netdev;
	rtnl_unlock();

	mhi_netdev->napi = devm_kzalloc(&mhi_dev->dev,
					sizeof(*mhi_netdev->napi), GFP_KERNEL);
	if (!mhi_netdev->napi) {
		ret = -ENOMEM;
		goto napi_alloc_fail;
	}

	netif_napi_add(mhi_netdev->ndev, mhi_netdev->napi,
		       mhi_netdev_poll, MHI_NETDEV_NAPI_POLL_WEIGHT);

	ret = register_netdev(mhi_netdev->ndev);
	if (ret) {
		MSG_ERR("Network device registration failed\n");
		goto net_dev_reg_fail;
	}

	napi_enable(mhi_netdev->napi);

	MSG_LOG("Exited\n");

	return 0;

net_dev_reg_fail:
	netif_napi_del(mhi_netdev->napi);

napi_alloc_fail:
	free_netdev(mhi_netdev->ndev);
	mhi_netdev->ndev = NULL;

	return ret;
}

static void mhi_netdev_xfer_ul_cb(struct mhi_device *mhi_dev,
				  struct mhi_result *mhi_result)
{
	struct mhi_netdev *mhi_netdev = dev_get_drvdata(&mhi_dev->dev);
	struct sk_buff *skb = mhi_result->buf_addr;
	struct net_device *ndev = mhi_netdev->ndev;

	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += skb->len;
	dev_kfree_skb(skb);

	if (netif_queue_stopped(ndev))
		netif_wake_queue(ndev);
}

static void mhi_netdev_push_skb(struct mhi_netdev *mhi_netdev,
				struct mhi_buf *mhi_buf,
				struct mhi_result *mhi_result)
{
	struct sk_buff *skb;
	struct mhi_netbuf *netbuf;

	netbuf = (struct mhi_netbuf *)mhi_buf;

	skb = alloc_skb(0, GFP_ATOMIC);
	if (!skb) {
		__free_pages(netbuf->page, mhi_netdev->order);
		return;
	}

	skb_add_rx_frag(skb, 0, netbuf->page, 0,
			mhi_result->bytes_xferd, mhi_netdev->mru);
	skb->dev = mhi_netdev->ndev;
	skb->protocol = mhi_netdev_ip_type_trans(*(u8 *)mhi_buf->buf);
	netif_receive_skb(skb);
}

static void mhi_netdev_xfer_dl_cb(struct mhi_device *mhi_dev,
				  struct mhi_result *mhi_result)
{
	struct mhi_netdev *mhi_netdev = dev_get_drvdata(&mhi_dev->dev);
	struct mhi_netbuf *netbuf = mhi_result->buf_addr;
	struct mhi_buf *mhi_buf = &netbuf->mhi_buf;
	struct sk_buff *skb;
	struct net_device *ndev = mhi_netdev->ndev;
	struct device *dev = mhi_dev->dev.parent->parent;
	struct mhi_net_chain *chain = mhi_netdev->chain;

	netbuf->unmap(dev, mhi_buf->dma_addr, mhi_buf->len, DMA_FROM_DEVICE);
	if (likely(netbuf->recycle))
		list_add_tail(&netbuf->node, mhi_netdev->recycle_pool);

	/* modem is down, drop the buffer */
	if (mhi_result->transaction_status == -ENOTCONN) {
		__free_pages(netbuf->page, mhi_netdev->order);
		return;
	}

	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += mhi_result->bytes_xferd;

	if (unlikely(!chain)) {
		mhi_netdev_push_skb(mhi_netdev, mhi_buf, mhi_result);
		return;
	}

	/* we support chaining */
	skb = alloc_skb(0, GFP_ATOMIC);
	if (likely(skb)) {
		skb_add_rx_frag(skb, 0, netbuf->page, 0,
				mhi_result->bytes_xferd, mhi_netdev->mru);
		/* this is first on list */
		if (!chain->head) {
			skb->dev = ndev;
			skb->protocol =
				mhi_netdev_ip_type_trans(*(u8 *)mhi_buf->buf);
			chain->head = skb;
		} else {
			skb_shinfo(chain->tail)->frag_list = skb;
		}

		chain->tail = skb;
	} else {
		__free_pages(netbuf->page, mhi_netdev->order);
	}
}

static void mhi_netdev_status_cb(struct mhi_device *mhi_dev,
				 enum mhi_callback mhi_cb)
{
	struct mhi_netdev *mhi_netdev = dev_get_drvdata(&mhi_dev->dev);

	if (mhi_cb != MHI_CB_PENDING_DATA)
		return;

	napi_schedule(mhi_netdev->napi);
	mhi_netdev->napi_scheduled = true;
}

#ifdef CONFIG_DEBUG_FS

struct dentry *dentry;

static int mhi_netdev_debugfs_stats_show(struct seq_file *m, void *d)
{
	struct mhi_netdev *mhi_netdev = m->private;

	seq_printf(m,
		   "mru:%u order:%u pool_size:%d, bg_pool_size:%d bg_pool_limit:%d abuf:%u kbuf:%u rbuf:%u\n",
		   mhi_netdev->mru, mhi_netdev->order, mhi_netdev->pool_size,
		   mhi_netdev->bg_pool_size, mhi_netdev->bg_pool_limit,
		   mhi_netdev->abuffers, mhi_netdev->kbuffers,
		   mhi_netdev->rbuffers);

	seq_printf(m, "chaining SKBs:%s\n", (mhi_netdev->chain) ?
		   "enabled" : "disabled");

	return 0;
}

static int mhi_netdev_debugfs_stats_open(struct inode *inode, struct file *fp)
{
	return single_open(fp, mhi_netdev_debugfs_stats_show, inode->i_private);
}

static const struct file_operations debugfs_stats = {
	.open = mhi_netdev_debugfs_stats_open,
	.release = single_release,
	.read = seq_read,
};

static int mhi_netdev_debugfs_chain(void *data, u64 val)
{
	struct mhi_netdev *mhi_netdev = data;
	struct mhi_netdev *rsc_dev = mhi_netdev->rsc_dev;

	mhi_netdev->chain = NULL;

	if (rsc_dev)
		rsc_dev->chain = NULL;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(debugfs_chain, NULL,
			 mhi_netdev_debugfs_chain, "%llu\n");

static void mhi_netdev_create_debugfs(struct mhi_netdev *mhi_netdev)
{
	char node_name[40];
	struct mhi_device *mhi_dev = mhi_netdev->mhi_dev;

	/* Both tx & rx client handle contain same device info */
	snprintf(node_name, sizeof(node_name), "%s_%s", dev_name(&mhi_dev->dev),
		 mhi_netdev->interface_name);

	if (IS_ERR_OR_NULL(dentry))
		return;

	mhi_netdev->dentry = debugfs_create_dir(node_name, dentry);
	if (IS_ERR_OR_NULL(mhi_netdev->dentry))
		return;

	debugfs_create_file_unsafe("stats", 0444, mhi_netdev->dentry,
				   mhi_netdev, &debugfs_stats);
	debugfs_create_file_unsafe("chain", 0444, mhi_netdev->dentry,
				   mhi_netdev, &debugfs_chain);
}

static void mhi_netdev_create_debugfs_dir(void)
{
	dentry = debugfs_create_dir(MHI_NETDEV_DRIVER_NAME, 0);
}

#else

static void mhi_netdev_create_debugfs(struct mhi_netdev *mhi_netdev)
{
}

static void mhi_netdev_create_debugfs_dir(void)
{
}

#endif

static ssize_t log_level_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct mhi_device *mhi_dev = to_mhi_device(dev);
	struct mhi_netdev *mhi_netdev = dev_get_drvdata(&mhi_dev->dev);

	if (!mhi_netdev)
		return -EIO;

	return scnprintf(buf, PAGE_SIZE,
			 "MHI network device IPC log level begins from: %s\n",
			 MHI_NETDEV_LOG_LEVEL_STR(mhi_netdev->msg_lvl));
}

static ssize_t log_level_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	struct mhi_device *mhi_dev = to_mhi_device(dev);
	struct mhi_netdev *mhi_netdev = dev_get_drvdata(&mhi_dev->dev);
	enum MHI_DEBUG_LEVEL log_level;

	if (kstrtou32(buf, 0, &log_level) < 0)
		return -EINVAL;

	if (!mhi_netdev)
		return -EIO;

	mhi_netdev->msg_lvl = log_level;

	/* set level for parent if RSC child netdev and vice versa */
	if (mhi_netdev->is_rsc_dev)
		mhi_netdev->rsc_parent->msg_lvl = log_level;
	else if (mhi_netdev->rsc_dev)
		mhi_netdev->rsc_dev->msg_lvl = log_level;

	MSG_LOG("MHI Network device IPC log level changed to: %s\n",
		MHI_NETDEV_LOG_LEVEL_STR(log_level));

	return count;
}
static DEVICE_ATTR_RW(log_level);

static struct attribute *mhi_netdev_attrs[] = {
	&dev_attr_log_level.attr,
	NULL,
};

static const struct attribute_group mhi_netdev_group = {
	.attrs = mhi_netdev_attrs,
};

static void mhi_netdev_remove(struct mhi_device *mhi_dev)
{
	struct mhi_netdev *mhi_netdev = dev_get_drvdata(&mhi_dev->dev);

	MSG_LOG("Remove notification received\n");

	/* rsc parent takes cares of the cleanup except buffer pool */
	if (mhi_netdev->is_rsc_dev) {
		mhi_netdev_free_pool(mhi_netdev);
		return;
	}

	sysfs_remove_group(&mhi_dev->dev.kobj, &mhi_netdev_group);
	kthread_stop(mhi_netdev->alloc_task);
	netif_stop_queue(mhi_netdev->ndev);
	napi_disable(mhi_netdev->napi);
	unregister_netdev(mhi_netdev->ndev);
	netif_napi_del(mhi_netdev->napi);
	free_netdev(mhi_netdev->ndev);
	mhi_netdev->ndev = NULL;

	if (!IS_ERR_OR_NULL(mhi_netdev->dentry))
		debugfs_remove_recursive(mhi_netdev->dentry);

	if (!mhi_netdev->rsc_parent)
		mhi_netdev_free_pool(mhi_netdev);
}

static void mhi_netdev_clone_dev(struct mhi_netdev *mhi_netdev,
				 struct mhi_netdev *parent)
{
	mhi_netdev->ndev = parent->ndev;
	mhi_netdev->napi = parent->napi;
	mhi_netdev->ipc_log = parent->ipc_log;
	mhi_netdev->msg_lvl = parent->msg_lvl;
	mhi_netdev->is_rsc_dev = true;
	mhi_netdev->chain = parent->chain;
	mhi_netdev->rsc_parent = parent;
	mhi_netdev->recycle_pool = parent->recycle_pool;
	mhi_netdev->bg_pool = parent->bg_pool;
}

static int mhi_netdev_probe(struct mhi_device *mhi_dev,
			    const struct mhi_device_id *id)
{
	struct mhi_netdev *mhi_netdev;
	struct mhi_netdev_driver_data *data;
	char node_name[40];
	int nr_tre, ret;

	data = (struct mhi_netdev_driver_data *)id->driver_data;

	mhi_netdev = devm_kzalloc(&mhi_dev->dev, sizeof(*mhi_netdev),
				  GFP_KERNEL);
	if (!mhi_netdev)
		return -ENOMEM;

	/* move mhi channels to start state */
	ret = mhi_prepare_for_transfer(mhi_dev);
	if (ret) {
		MSG_ERR("Failed to start channels, ret: %d\n", ret);
		return ret;
	}

	mhi_netdev->mhi_dev = mhi_dev;
	dev_set_drvdata(&mhi_dev->dev, mhi_netdev);

	mhi_netdev->mru = data->mru;
	mhi_netdev->rsc_parent = data->has_rsc_child ? mhi_netdev : NULL;
	mhi_netdev->rsc_dev = data->is_rsc_chan ? mhi_netdev : NULL;

	/* MRU must be multiplication of page size */
	mhi_netdev->order = __ilog2_u32(mhi_netdev->mru / PAGE_SIZE);
	if ((PAGE_SIZE << mhi_netdev->order) < mhi_netdev->mru)
		return -EINVAL;

	if (data->is_rsc_chan) {
		if (!rsc_parent_netdev || !rsc_parent_netdev->ndev)
			return -ENODEV;

		/* this device is shared with parent device. so we won't be
		 * creating a new network interface. Clone parent
		 * information to child node
		 */
		mhi_netdev_clone_dev(mhi_netdev, rsc_parent_netdev);
	} else {
		mhi_netdev->msg_lvl = MHI_NETDEV_LOG_LVL;

		ret = sysfs_create_group(&mhi_dev->dev.kobj, &mhi_netdev_group);
		if (ret)
			MSG_ERR("Failed to create MHI netdev sysfs group\n");

		if (data->chain_skb) {
			mhi_netdev->chain = devm_kzalloc(&mhi_dev->dev,
						sizeof(*mhi_netdev->chain),
						GFP_KERNEL);
			if (!mhi_netdev->chain)
				return -ENOMEM;
		}

		mhi_netdev->interface_name = data->interface_name;

		ret = mhi_netdev_enable_iface(mhi_netdev);
		if (ret)
			return ret;

		/* setup pool size ~2x ring length*/
		nr_tre = mhi_get_free_desc_count(mhi_dev, DMA_FROM_DEVICE);
		mhi_netdev->pool_size = 1 << __ilog2_u32(nr_tre);
		if (nr_tre > mhi_netdev->pool_size)
			mhi_netdev->pool_size <<= 1;
		mhi_netdev->pool_size <<= 1;

		/* if we expect child device to share then double the pool */
		if (data->has_rsc_child)
			mhi_netdev->pool_size <<= 1;

		/* allocate memory pool */
		ret = mhi_netdev_alloc_pool(mhi_netdev);
		if (ret)
			return -ENOMEM;

		/* create a background task to allocate memory */
		mhi_netdev->bg_pool = kmalloc(sizeof(*mhi_netdev->bg_pool),
					      GFP_KERNEL);
		if (!mhi_netdev->bg_pool)
			return -ENOMEM;

		init_waitqueue_head(&mhi_netdev->alloc_event);
		INIT_LIST_HEAD(mhi_netdev->bg_pool);
		spin_lock_init(&mhi_netdev->bg_lock);
		mhi_netdev->bg_pool_limit = mhi_netdev->pool_size / 4;
		mhi_netdev->alloc_task = kthread_run(mhi_netdev_alloc_thread,
						     mhi_netdev,
						     mhi_netdev->ndev->name);
		if (IS_ERR(mhi_netdev->alloc_task))
			return PTR_ERR(mhi_netdev->alloc_task);

		rsc_parent_netdev = mhi_netdev;

		/* create ipc log buffer */
		snprintf(node_name, sizeof(node_name),
			 "%s_%s", dev_name(&mhi_dev->dev),
			 mhi_netdev->interface_name);

		mhi_netdev->ipc_log = ipc_log_context_create(IPC_LOG_PAGES,
							     node_name, 0);

		mhi_netdev_create_debugfs(mhi_netdev);
	}

	/* now we have a pool of buffers allocated, queue to hardware
	 * by triggering a napi_poll
	 */
	napi_schedule(mhi_netdev->napi);
	mhi_netdev->napi_scheduled = true;

	return 0;
}

static const struct mhi_netdev_driver_data hw0_308_data = {
	.mru = 0x8000,
	.chain_skb = true,
	.is_rsc_chan = false,
	.has_rsc_child = false,
	.interface_name = "rmnet_mhi",
};

static const struct mhi_device_id mhi_netdev_match_table[] = {
	{ .chan = "IP_HW0", .driver_data = (kernel_ulong_t)&hw0_308_data },
	{},
};

static struct mhi_driver mhi_netdev_driver = {
	.id_table = mhi_netdev_match_table,
	.probe = mhi_netdev_probe,
	.remove = mhi_netdev_remove,
	.ul_xfer_cb = mhi_netdev_xfer_ul_cb,
	.dl_xfer_cb = mhi_netdev_xfer_dl_cb,
	.status_cb = mhi_netdev_status_cb,
	.driver = {
		.name = "mhi_netdev",
		.owner = THIS_MODULE,
	}
};

static int __init mhi_netdev_init(void)
{
	BUILD_BUG_ON(sizeof(struct mhi_netbuf) > MAX_NETBUF_SIZE);
	mhi_netdev_create_debugfs_dir();

	return mhi_driver_register(&mhi_netdev_driver);
}
module_init(mhi_netdev_init);

static void __exit mhi_netdev_exit(void)
{
	debugfs_remove_recursive(dentry);

	mhi_driver_unregister(&mhi_netdev_driver);
}
module_exit(mhi_netdev_exit);

MODULE_DESCRIPTION("MHI NETDEV Network Interface");
MODULE_LICENSE("GPL");
