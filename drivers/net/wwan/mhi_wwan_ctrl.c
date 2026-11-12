// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, Linaro Ltd <loic.poulain@linaro.org> */
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mhi.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/termios.h>
#include <linux/workqueue.h>
#include <linux/wwan.h>

/* MHI wwan flags */
enum mhi_wwan_flags {
	MHI_WWAN_DL_CAP,
	MHI_WWAN_UL_CAP,
	MHI_WWAN_RX_REFILL,
};

#define MHI_WWAN_MAX_MTU	0x8000

/* IP_CTRL channel message that sets the modem's DTR/RTS control lines */
struct mhi_dtr_ctrl_msg {
	__le32 preamble;
	__le32 msg_id;
	__le32 dest_id;
	__le32 size;
	__le32 msg;
} __packed;

#define MHI_DTR_CTRL_MAGIC	0x4C525443	/* 'CTRL' */
#define MHI_DTR_MSG_DTR		BIT(0)
#define MHI_DTR_MSG_RTS		BIT(1)
#define MHI_DTR_HOST_STATE	0x10

/* Per-controller IP_CTRL channel, used to signal DTR/RTS to that modem */
struct mhi_wwan_dtr {
	struct mhi_controller *cntrl;
	struct mhi_device *mhi_dev;
	struct list_head node;
	struct mhi_dtr_ctrl_msg *rx_buf; /* DL sink, separate allocation for DMA safety */
	struct delayed_work rx_refill; /* retries a failed sink buffer requeue */
};

static LIST_HEAD(mhi_wwan_dtr_list);
static DEFINE_MUTEX(mhi_wwan_dtr_lock);

struct mhi_wwan_dev {
	/* Lower level is a mhi dev, upper level is a wwan port */
	struct mhi_device *mhi_dev;
	struct wwan_port *wwan_port;

	/* State and capabilities */
	unsigned long flags;
	size_t mtu;

	/* Protect against concurrent TX and TX-completion (bh) */
	spinlock_t tx_lock;

	/* Protect RX budget and rx_refill scheduling */
	spinlock_t rx_lock;
	struct work_struct rx_refill;

	/* RX budget is initially set to the size of the MHI RX queue and is
	 * used to limit the number of allocated and queued packets. It is
	 * decremented on data queueing and incremented on data release.
	 */
	unsigned int rx_budget;

	/* Latest DTR/RTS state for the modem, sent from dtr_work so a burst of
	 * changes is coalesced and only the final state has to get through
	 */
	unsigned int dtr_mdmbits;
	struct delayed_work dtr_work;
};

/* Increment RX budget and schedule RX refill if necessary */
static void mhi_wwan_rx_budget_inc(struct mhi_wwan_dev *mhiwwan)
{
	spin_lock_bh(&mhiwwan->rx_lock);

	mhiwwan->rx_budget++;

	if (test_bit(MHI_WWAN_RX_REFILL, &mhiwwan->flags))
		schedule_work(&mhiwwan->rx_refill);

	spin_unlock_bh(&mhiwwan->rx_lock);
}

/* Decrement RX budget if non-zero and return true on success */
static bool mhi_wwan_rx_budget_dec(struct mhi_wwan_dev *mhiwwan)
{
	bool ret = false;

	spin_lock_bh(&mhiwwan->rx_lock);

	if (mhiwwan->rx_budget) {
		mhiwwan->rx_budget--;
		if (test_bit(MHI_WWAN_RX_REFILL, &mhiwwan->flags))
			ret = true;
	}

	spin_unlock_bh(&mhiwwan->rx_lock);

	return ret;
}

static void __mhi_skb_destructor(struct sk_buff *skb)
{
	/* RX buffer has been consumed, increase the allowed budget */
	mhi_wwan_rx_budget_inc(skb_shinfo(skb)->destructor_arg);
}

static void mhi_wwan_ctrl_refill_work(struct work_struct *work)
{
	struct mhi_wwan_dev *mhiwwan = container_of(work, struct mhi_wwan_dev, rx_refill);
	struct mhi_device *mhi_dev = mhiwwan->mhi_dev;

	while (mhi_wwan_rx_budget_dec(mhiwwan)) {
		struct sk_buff *skb;

		skb = alloc_skb(mhiwwan->mtu, GFP_KERNEL);
		if (!skb) {
			mhi_wwan_rx_budget_inc(mhiwwan);
			break;
		}

		/* To prevent unlimited buffer allocation if nothing consumes
		 * the RX buffers (passed to WWAN core), track their lifespan
		 * to not allocate more than allowed budget.
		 */
		skb->destructor = __mhi_skb_destructor;
		skb_shinfo(skb)->destructor_arg = mhiwwan;

		if (mhi_queue_skb(mhi_dev, DMA_FROM_DEVICE, skb, mhiwwan->mtu, MHI_EOT)) {
			dev_err(&mhi_dev->dev, "Failed to queue buffer\n");
			kfree_skb(skb);
			break;
		}
	}
}

/* Signal the modem's DTR/RTS lines over its own controller's IP_CTRL channel */
static int mhi_wwan_ctrl_send_dtr(struct mhi_wwan_dev *mhiwwan, unsigned int mdmbits)
{
	struct mhi_controller *cntrl = mhiwwan->mhi_dev->mhi_cntrl;
	struct mhi_device *ctrl_dev = NULL;
	struct mhi_dtr_ctrl_msg *dtr_msg;
	struct mhi_wwan_dtr *dtr;
	u32 msg = 0;
	int ret;

	guard(mutex)(&mhi_wwan_dtr_lock);

	list_for_each_entry(dtr, &mhi_wwan_dtr_list, node) {
		if (dtr->cntrl == cntrl) {
			ctrl_dev = dtr->mhi_dev;
			break;
		}
	}
	if (!ctrl_dev) {
		dev_dbg(&mhiwwan->mhi_dev->dev,
			"IP_CTRL not enumerated; DTR/RTS not signalled to modem\n");
		return 0;
	}

	dtr_msg = kzalloc_obj(*dtr_msg);
	if (!dtr_msg)
		return -ENOMEM;

	if (mdmbits & TIOCM_DTR)
		msg |= MHI_DTR_MSG_DTR;
	if (mdmbits & TIOCM_RTS)
		msg |= MHI_DTR_MSG_RTS;

	dtr_msg->preamble = cpu_to_le32(MHI_DTR_CTRL_MAGIC);
	dtr_msg->msg_id = cpu_to_le32(MHI_DTR_HOST_STATE);
	dtr_msg->dest_id = cpu_to_le32(mhiwwan->mhi_dev->ul_chan_id);
	dtr_msg->size = cpu_to_le32(sizeof(__le32));
	dtr_msg->msg = cpu_to_le32(msg);

	ret = mhi_queue_buf(ctrl_dev, DMA_TO_DEVICE, dtr_msg, sizeof(*dtr_msg),
			    MHI_EOT);
	if (ret)
		kfree(dtr_msg);

	return ret;
}

/* Send the latest DTR/RTS state. A full UL ring or a failed allocation is
 * retried shortly, with whatever state is latest by then, so the modem ends
 * up with the final state. Other errors mean the channel is going away.
 */
static void mhi_wwan_ctrl_dtr_work(struct work_struct *work)
{
	struct mhi_wwan_dev *mhiwwan = container_of(to_delayed_work(work),
						    struct mhi_wwan_dev, dtr_work);
	int ret;

	ret = mhi_wwan_ctrl_send_dtr(mhiwwan, READ_ONCE(mhiwwan->dtr_mdmbits));
	if (ret == -EAGAIN || ret == -ENOMEM)
		schedule_delayed_work(&mhiwwan->dtr_work, msecs_to_jiffies(10));
	else if (ret)
		dev_warn_ratelimited(&mhiwwan->mhi_dev->dev,
				     "failed to send DTR/RTS state to modem: %d\n",
				     ret);
}

static void mhi_wwan_ctrl_dtr_rts(struct wwan_port *port, unsigned int mdmbits)
{
	struct mhi_wwan_dev *mhiwwan = wwan_port_get_drvdata(port);
	bool drop = (READ_ONCE(mhiwwan->dtr_mdmbits) & TIOCM_DTR) &&
		    !(mdmbits & TIOCM_DTR);

	WRITE_ONCE(mhiwwan->dtr_mdmbits, mdmbits);
	schedule_delayed_work(&mhiwwan->dtr_work, 0);

	/* A DTR drop matters on its own, e.g. to hang up a modem set to AT&D2,
	 * so send it now rather than let a following raise coalesce it away
	 */
	if (drop)
		flush_delayed_work(&mhiwwan->dtr_work);
}

static int mhi_wwan_ctrl_start(struct wwan_port *port)
{
	struct mhi_wwan_dev *mhiwwan = wwan_port_get_drvdata(port);
	int ret;

	/* Start mhi device's channel(s) */
	ret = mhi_prepare_for_transfer(mhiwwan->mhi_dev);
	if (ret)
		return ret;

	/* Don't allocate more buffers than MHI channel queue size */
	mhiwwan->rx_budget = mhi_get_free_desc_count(mhiwwan->mhi_dev, DMA_FROM_DEVICE);

	/* Add buffers to the MHI inbound queue */
	if (test_bit(MHI_WWAN_DL_CAP, &mhiwwan->flags)) {
		set_bit(MHI_WWAN_RX_REFILL, &mhiwwan->flags);
		mhi_wwan_ctrl_refill_work(&mhiwwan->rx_refill);
	}

	return 0;
}

static void mhi_wwan_ctrl_stop(struct wwan_port *port)
{
	struct mhi_wwan_dev *mhiwwan = wwan_port_get_drvdata(port);

	spin_lock_bh(&mhiwwan->rx_lock);
	clear_bit(MHI_WWAN_RX_REFILL, &mhiwwan->flags);
	spin_unlock_bh(&mhiwwan->rx_lock);

	cancel_work_sync(&mhiwwan->rx_refill);

	/* Send any pending DTR/RTS state before the channel stops */
	flush_delayed_work(&mhiwwan->dtr_work);

	mhi_unprepare_from_transfer(mhiwwan->mhi_dev);
}

static int mhi_wwan_ctrl_tx(struct wwan_port *port, struct sk_buff *skb)
{
	struct mhi_wwan_dev *mhiwwan = wwan_port_get_drvdata(port);
	int ret;

	if (skb->len > mhiwwan->mtu)
		return -EMSGSIZE;

	if (!test_bit(MHI_WWAN_UL_CAP, &mhiwwan->flags))
		return -EOPNOTSUPP;

	/* Queue the packet for MHI transfer and check fullness of the queue */
	spin_lock_bh(&mhiwwan->tx_lock);
	ret = mhi_queue_skb(mhiwwan->mhi_dev, DMA_TO_DEVICE, skb, skb->len, MHI_EOT);
	if (mhi_queue_is_full(mhiwwan->mhi_dev, DMA_TO_DEVICE))
		wwan_port_txoff(port);
	spin_unlock_bh(&mhiwwan->tx_lock);

	return ret;
}

static const struct wwan_port_ops wwan_pops = {
	.start = mhi_wwan_ctrl_start,
	.stop = mhi_wwan_ctrl_stop,
	.tx = mhi_wwan_ctrl_tx,
	.dtr_rts = mhi_wwan_ctrl_dtr_rts,
};

static void mhi_ul_xfer_cb(struct mhi_device *mhi_dev,
			   struct mhi_result *mhi_result)
{
	struct mhi_wwan_dev *mhiwwan = dev_get_drvdata(&mhi_dev->dev);
	struct wwan_port *port = mhiwwan->wwan_port;
	struct sk_buff *skb = mhi_result->buf_addr;

	dev_dbg(&mhi_dev->dev, "%s: status: %d xfer_len: %zu\n", __func__,
		mhi_result->transaction_status, mhi_result->bytes_xferd);

	/* MHI core has done with the buffer, release it */
	consume_skb(skb);

	/* There is likely new slot available in the MHI queue, re-allow TX */
	spin_lock_bh(&mhiwwan->tx_lock);
	if (!mhi_queue_is_full(mhiwwan->mhi_dev, DMA_TO_DEVICE))
		wwan_port_txon(port);
	spin_unlock_bh(&mhiwwan->tx_lock);
}

static void mhi_dl_xfer_cb(struct mhi_device *mhi_dev,
			   struct mhi_result *mhi_result)
{
	struct mhi_wwan_dev *mhiwwan = dev_get_drvdata(&mhi_dev->dev);
	struct wwan_port *port = mhiwwan->wwan_port;
	struct sk_buff *skb = mhi_result->buf_addr;

	dev_dbg(&mhi_dev->dev, "%s: status: %d receive_len: %zu\n", __func__,
		mhi_result->transaction_status, mhi_result->bytes_xferd);

	if (mhi_result->transaction_status &&
	    mhi_result->transaction_status != -EOVERFLOW) {
		kfree_skb(skb);
		return;
	}

	/* MHI core does not update skb->len, do it before forward */
	skb_put(skb, mhi_result->bytes_xferd);
	wwan_port_rx(port, skb);

	/* Do not increment rx budget nor refill RX buffers now, wait for the
	 * buffer to be consumed. Done from __mhi_skb_destructor().
	 */
}

static int mhi_wwan_ctrl_probe(struct mhi_device *mhi_dev,
			       const struct mhi_device_id *id)
{
	struct mhi_controller *cntrl = mhi_dev->mhi_cntrl;
	struct mhi_wwan_dev *mhiwwan;
	struct wwan_port *port;

	mhiwwan = kzalloc_obj(*mhiwwan);
	if (!mhiwwan)
		return -ENOMEM;

	mhiwwan->mhi_dev = mhi_dev;
	mhiwwan->mtu = MHI_WWAN_MAX_MTU;
	INIT_WORK(&mhiwwan->rx_refill, mhi_wwan_ctrl_refill_work);
	INIT_DELAYED_WORK(&mhiwwan->dtr_work, mhi_wwan_ctrl_dtr_work);
	spin_lock_init(&mhiwwan->tx_lock);
	spin_lock_init(&mhiwwan->rx_lock);

	if (mhi_dev->dl_chan)
		set_bit(MHI_WWAN_DL_CAP, &mhiwwan->flags);
	if (mhi_dev->ul_chan)
		set_bit(MHI_WWAN_UL_CAP, &mhiwwan->flags);

	dev_set_drvdata(&mhi_dev->dev, mhiwwan);

	/* Register as a wwan port, id->driver_data contains wwan port type */
	port = wwan_create_port(&cntrl->mhi_dev->dev, id->driver_data,
				&wwan_pops, NULL, mhiwwan);
	if (IS_ERR(port)) {
		kfree(mhiwwan);
		return PTR_ERR(port);
	}

	mhiwwan->wwan_port = port;

	return 0;
};

static void mhi_wwan_ctrl_remove(struct mhi_device *mhi_dev)
{
	struct mhi_wwan_dev *mhiwwan = dev_get_drvdata(&mhi_dev->dev);

	wwan_remove_port(mhiwwan->wwan_port);
	/* Give the DTR/RTS drop from removal one attempt, then stop the work */
	flush_delayed_work(&mhiwwan->dtr_work);
	disable_delayed_work_sync(&mhiwwan->dtr_work);
	kfree(mhiwwan);
}

/* IP_CTRL channel driver, bound separately so the data-port path is untouched */
static void mhi_wwan_dtr_ul_xfer_cb(struct mhi_device *mhi_dev,
				    struct mhi_result *mhi_result)
{
	/* MHI core has done with the buffer, release it */
	kfree(mhi_result->buf_addr);
}

/* Requeue the single DL sink buffer so the IP_CTRL DL ring never runs empty.
 * This only runs from the work item, so remove() can stop it before the
 * channel is torn down. A transient mapping failure is retried, other errors
 * mean the channel is going away and the next probe queues a fresh buffer.
 */
static void mhi_wwan_dtr_refill_work(struct work_struct *work)
{
	struct mhi_wwan_dtr *dtr = container_of(to_delayed_work(work),
						struct mhi_wwan_dtr, rx_refill);
	int ret;

	ret = mhi_queue_buf(dtr->mhi_dev, DMA_FROM_DEVICE, dtr->rx_buf,
			    sizeof(*dtr->rx_buf), MHI_EOT);
	if (ret == -ENOMEM) {
		dev_warn_ratelimited(&dtr->mhi_dev->dev,
				     "failed to requeue IP_CTRL RX buffer, retrying\n");
		schedule_delayed_work(&dtr->rx_refill, msecs_to_jiffies(100));
	} else if (ret) {
		dev_dbg(&dtr->mhi_dev->dev,
			"failed to requeue IP_CTRL RX buffer: %d\n", ret);
	}
}

static void mhi_wwan_dtr_dl_xfer_cb(struct mhi_device *mhi_dev,
				    struct mhi_result *mhi_result)
{
	struct mhi_wwan_dtr *dtr = dev_get_drvdata(&mhi_dev->dev);

	/* Channel is being torn down (e.g. -ENOTCONN), do not requeue */
	if (mhi_result->transaction_status &&
	    mhi_result->transaction_status != -EOVERFLOW)
		return;

	/* Modem serial state not needed, requeue the sink buffer to keep DL ring live */
	schedule_delayed_work(&dtr->rx_refill, 0);
}

static int mhi_wwan_dtr_probe(struct mhi_device *mhi_dev,
			      const struct mhi_device_id *id)
{
	struct mhi_wwan_dtr *dtr;
	int ret;

	dtr = kzalloc_obj(*dtr);
	if (!dtr)
		return -ENOMEM;

	dtr->rx_buf = kmalloc_obj(*dtr->rx_buf);
	if (!dtr->rx_buf) {
		ret = -ENOMEM;
		goto err_free_dtr;
	}
	INIT_DELAYED_WORK(&dtr->rx_refill, mhi_wwan_dtr_refill_work);

	ret = mhi_prepare_for_transfer(mhi_dev);
	if (ret)
		goto err_free_buf;

	dtr->cntrl = mhi_dev->mhi_cntrl;
	dtr->mhi_dev = mhi_dev;
	dev_set_drvdata(&mhi_dev->dev, dtr);

	/* On failure mhi_probe() unprepares the channels itself */
	ret = mhi_queue_buf(mhi_dev, DMA_FROM_DEVICE, dtr->rx_buf,
			    sizeof(*dtr->rx_buf), MHI_EOT);
	if (ret)
		goto err_free_buf;

	mutex_lock(&mhi_wwan_dtr_lock);
	list_add(&dtr->node, &mhi_wwan_dtr_list);
	mutex_unlock(&mhi_wwan_dtr_lock);

	return 0;

err_free_buf:
	kfree(dtr->rx_buf);
err_free_dtr:
	kfree(dtr);
	return ret;
}

static void mhi_wwan_dtr_remove(struct mhi_device *mhi_dev)
{
	struct mhi_wwan_dtr *dtr = dev_get_drvdata(&mhi_dev->dev);

	mutex_lock(&mhi_wwan_dtr_lock);
	list_del(&dtr->node);
	mutex_unlock(&mhi_wwan_dtr_lock);

	/* Stop the refill work, and stop a late DL callback re-arming it, before
	 * the channel is torn down so no requeue can race with the teardown
	 */
	disable_delayed_work_sync(&dtr->rx_refill);
	mhi_unprepare_from_transfer(mhi_dev);
	kfree(dtr->rx_buf);
	kfree(dtr);
}

static const struct mhi_device_id mhi_wwan_ctrl_match_table[] = {
	{ .chan = "DUN", .driver_data = WWAN_PORT_AT },
	{ .chan = "DUN2", .driver_data = WWAN_PORT_AT },
	{ .chan = "MBIM", .driver_data = WWAN_PORT_MBIM },
	{ .chan = "QMI", .driver_data = WWAN_PORT_QMI },
	{ .chan = "DIAG", .driver_data = WWAN_PORT_QCDM },
	{ .chan = "FIREHOSE", .driver_data = WWAN_PORT_FIREHOSE },
	{ .chan = "NMEA", .driver_data = WWAN_PORT_NMEA },
	{},
};
MODULE_DEVICE_TABLE(mhi, mhi_wwan_ctrl_match_table);

static struct mhi_driver mhi_wwan_ctrl_driver = {
	.id_table = mhi_wwan_ctrl_match_table,
	.remove = mhi_wwan_ctrl_remove,
	.probe = mhi_wwan_ctrl_probe,
	.ul_xfer_cb = mhi_ul_xfer_cb,
	.dl_xfer_cb = mhi_dl_xfer_cb,
	.driver = {
		.name = "mhi_wwan_ctrl",
	},
};

static const struct mhi_device_id mhi_wwan_dtr_match_table[] = {
	{ .chan = "IP_CTRL" },
	{},
};
MODULE_DEVICE_TABLE(mhi, mhi_wwan_dtr_match_table);

static struct mhi_driver mhi_wwan_dtr_driver = {
	.id_table = mhi_wwan_dtr_match_table,
	.remove = mhi_wwan_dtr_remove,
	.probe = mhi_wwan_dtr_probe,
	.ul_xfer_cb = mhi_wwan_dtr_ul_xfer_cb,
	.dl_xfer_cb = mhi_wwan_dtr_dl_xfer_cb,
	.driver = {
		.name = "mhi_wwan_dtr",
	},
};

static int __init mhi_wwan_ctrl_init(void)
{
	int ret;

	ret = mhi_driver_register(&mhi_wwan_dtr_driver);
	if (ret)
		return ret;

	ret = mhi_driver_register(&mhi_wwan_ctrl_driver);
	if (ret)
		mhi_driver_unregister(&mhi_wwan_dtr_driver);

	return ret;
}
module_init(mhi_wwan_ctrl_init);

static void __exit mhi_wwan_ctrl_exit(void)
{
	mhi_driver_unregister(&mhi_wwan_ctrl_driver);
	mhi_driver_unregister(&mhi_wwan_dtr_driver);
}
module_exit(mhi_wwan_ctrl_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MHI WWAN CTRL Driver");
MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
