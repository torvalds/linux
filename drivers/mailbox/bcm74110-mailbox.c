// SPDX-License-Identifier: GPL-2.0
/*
 * Broadcom BCM74110 Mailbox Driver
 *
 * Copyright (c) 2025 Broadcom
 */
#include <linux/list.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/mailbox_controller.h>
#include <linux/bitfield.h>
#include <linux/slab.h>

#define BCM_MBOX_BASE(sel)		((sel) * 0x40)
#define BCM_MBOX_IRQ_BASE(sel)		(((sel) * 0x20) + 0x800)

#define BCM_MBOX_CFGA			0x0
#define BCM_MBOX_CFGB			0x4
#define BCM_MBOX_CFGC			0x8
#define BCM_MBOX_CFGD			0xc
#define BCM_MBOX_CTRL			0x10
#define  BCM_MBOX_CTRL_EN		BIT(0)
#define  BCM_MBOX_CTRL_CLR		BIT(1)
#define BCM_MBOX_STATUS0		0x14
#define  BCM_MBOX_STATUS0_NOT_EMPTY	BIT(28)
#define  BCM_MBOX_STATUS0_FULL		BIT(29)
#define BCM_MBOX_STATUS1		0x18
#define BCM_MBOX_STATUS2		0x1c
#define BCM_MBOX_WDATA			0x20
#define BCM_MBOX_RDATA			0x28

#define BCM_MBOX_IRQ_STATUS		0x0
#define BCM_MBOX_IRQ_SET		0x4
#define BCM_MBOX_IRQ_CLEAR		0x8
#define BCM_MBOX_IRQ_MASK_STATUS	0xc
#define BCM_MBOX_IRQ_MASK_SET		0x10
#define BCM_MBOX_IRQ_MASK_CLEAR		0x14
#define  BCM_MBOX_IRQ_TIMEOUT		BIT(0)
#define  BCM_MBOX_IRQ_NOT_EMPTY		BIT(1)
#define  BCM_MBOX_IRQ_FULL		BIT(2)
#define  BCM_MBOX_IRQ_LOW_WM		BIT(3)
#define  BCM_MBOX_IRQ_HIGH_WM		BIT(4)

#define BCM_LINK_CODE0			0xbe0
#define BCM_LINK_CODE1			0xbe1
#define BCM_LINK_CODE2			0xbe2

enum {
	BCM_MSG_FUNC_LINK_START = 0,
	BCM_MSG_FUNC_LINK_STOP,
	BCM_MSG_FUNC_SHMEM_TX,
	BCM_MSG_FUNC_SHMEM_RX,
	BCM_MSG_FUNC_SHMEM_STOP,
	BCM_MSG_FUNC_MAX,
};

enum {
	BCM_MSG_SVC_INIT = 0,
	BCM_MSG_SVC_PMC,
	BCM_MSG_SVC_SCMI,
	BCM_MSG_SVC_DPFE,
	BCM_MSG_SVC_MAX,
};

struct bcm74110_mbox_msg {
	struct list_head		list_entry;
#define BCM_MSG_VERSION_MASK		GENMASK(31, 29)
#define  BCM_MSG_VERSION		0x1
#define BCM_MSG_REQ_MASK		BIT(28)
#define BCM_MSG_RPLY_MASK		BIT(27)
#define BCM_MSG_SVC_MASK		GENMASK(26, 24)
#define BCM_MSG_FUNC_MASK		GENMASK(23, 16)
#define BCM_MSG_LENGTH_MASK		GENMASK(15, 4)
#define BCM_MSG_SLOT_MASK		GENMASK(3, 0)

#define BCM_MSG_SET_FIELD(hdr, field, val)			\
	do {							\
		hdr &= ~BCM_MSG_##field##_MASK;			\
		hdr |= FIELD_PREP(BCM_MSG_##field##_MASK, val);	\
	} while (0)

#define BCM_MSG_GET_FIELD(hdr, field)				\
		FIELD_GET(BCM_MSG_##field##_MASK, hdr)
	u32				msg;
};

struct bcm74110_mbox_chan {
	struct bcm74110_mbox		*mbox;
	bool				en;
	int				slot;
	int				type;
};

struct bcm74110_mbox {
	struct platform_device		*pdev;
	void __iomem			*base;

	int				tx_chan;
	int				rx_chan;
	int				rx_irq;
	struct list_head		rx_svc_init_list;
	spinlock_t			rx_svc_list_lock;

	struct mbox_controller		controller;
	struct bcm74110_mbox_chan	*mbox_chan;
};

#define BCM74110_OFFSET_IO_WRITEL_MACRO(name, offset_base)	\
static void bcm74110_##name##_writel(struct bcm74110_mbox *mbox,\
				     u32 val, u32 off)		\
{								\
	writel_relaxed(val, mbox->base + offset_base + off);	\
}
BCM74110_OFFSET_IO_WRITEL_MACRO(tx, BCM_MBOX_BASE(mbox->tx_chan));
BCM74110_OFFSET_IO_WRITEL_MACRO(irq, BCM_MBOX_IRQ_BASE(mbox->rx_chan));

#define BCM74110_OFFSET_IO_READL_MACRO(name, offset_base)	\
static u32 bcm74110_##name##_readl(struct bcm74110_mbox *mbox,	\
				   u32 off)			\
{								\
	return readl_relaxed(mbox->base + offset_base + off);	\
}
BCM74110_OFFSET_IO_READL_MACRO(tx, BCM_MBOX_BASE(mbox->tx_chan));
BCM74110_OFFSET_IO_READL_MACRO(rx, BCM_MBOX_BASE(mbox->rx_chan));
BCM74110_OFFSET_IO_READL_MACRO(irq, BCM_MBOX_IRQ_BASE(mbox->rx_chan));

static inline struct bcm74110_mbox *bcm74110_mbox_from_cntrl(
					struct mbox_controller *cntrl)
{
	return container_of(cntrl, struct bcm74110_mbox, controller);
}

static void bcm74110_rx_push_init_msg(struct bcm74110_mbox *mbox, u32 val)
{
	struct bcm74110_mbox_msg *msg;

	msg = kzalloc(sizeof(*msg), GFP_ATOMIC);
	if (!msg)
		return;

	INIT_LIST_HEAD(&msg->list_entry);
	msg->msg = val;

	spin_lock(&mbox->rx_svc_list_lock);
	list_add_tail(&msg->list_entry, &mbox->rx_svc_init_list);
	spin_unlock(&mbox->rx_svc_list_lock);
}

static void bcm74110_rx_process_msg(struct bcm74110_mbox *mbox)
{
	struct device *dev = &mbox->pdev->dev;
	struct bcm74110_mbox_chan *chan_priv;
	struct mbox_chan *chan;
	u32 msg, status;
	int type;

	do {
		msg = bcm74110_rx_readl(mbox, BCM_MBOX_RDATA);
		status = bcm74110_rx_readl(mbox, BCM_MBOX_STATUS0);

		dev_dbg(dev, "rx: [{req=%lu|rply=%lu|srv=%lu|fn=%lu|length=%lu|slot=%lu]\n",
			BCM_MSG_GET_FIELD(msg, REQ), BCM_MSG_GET_FIELD(msg, RPLY),
			BCM_MSG_GET_FIELD(msg, SVC), BCM_MSG_GET_FIELD(msg, FUNC),
			BCM_MSG_GET_FIELD(msg, LENGTH), BCM_MSG_GET_FIELD(msg, SLOT));

		type = BCM_MSG_GET_FIELD(msg, SVC);
		switch (type) {
		case BCM_MSG_SVC_INIT:
			bcm74110_rx_push_init_msg(mbox, msg);
			break;
		case BCM_MSG_SVC_PMC:
		case BCM_MSG_SVC_SCMI:
		case BCM_MSG_SVC_DPFE:
			chan = &mbox->controller.chans[type];
			chan_priv = chan->con_priv;
			if (chan_priv->en)
				mbox_chan_received_data(chan, NULL);
			else
				dev_warn(dev, "Channel not enabled\n");
			break;
		default:
			dev_warn(dev, "Unsupported msg received\n");
		}
	} while (status & BCM_MBOX_STATUS0_NOT_EMPTY);
}

static irqreturn_t bcm74110_mbox_isr(int irq, void *data)
{
	struct bcm74110_mbox *mbox = data;
	u32 status;

	status = bcm74110_irq_readl(mbox, BCM_MBOX_IRQ_STATUS);

	bcm74110_irq_writel(mbox, 0xffffffff, BCM_MBOX_IRQ_CLEAR);

	if (status & BCM_MBOX_IRQ_NOT_EMPTY)
		bcm74110_rx_process_msg(mbox);
	else
		dev_warn(&mbox->pdev->dev, "Spurious interrupt\n");

	return IRQ_HANDLED;
}

static void bcm74110_mbox_mask_and_clear(struct bcm74110_mbox *mbox)
{
	bcm74110_irq_writel(mbox, 0xffffffff, BCM_MBOX_IRQ_MASK_SET);
	bcm74110_irq_writel(mbox, 0xffffffff, BCM_MBOX_IRQ_CLEAR);
}

static int bcm74110_rx_pop_init_msg(struct bcm74110_mbox *mbox, u32 func_type,
				    u32 *val)
{
	struct bcm74110_mbox_msg *msg, *msg_tmp;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&mbox->rx_svc_list_lock, flags);
	list_for_each_entry_safe(msg, msg_tmp, &mbox->rx_svc_init_list,
				 list_entry) {
		if (BCM_MSG_GET_FIELD(msg->msg, FUNC) == func_type) {
			list_del(&msg->list_entry);
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&mbox->rx_svc_list_lock, flags);

	if (!found)
		return -EINVAL;

	*val = msg->msg;
	kfree(msg);

	return 0;
}

static void bcm74110_rx_flush_msg(struct bcm74110_mbox *mbox)
{
	struct bcm74110_mbox_msg *msg, *msg_tmp;
	LIST_HEAD(list_temp);
	unsigned long flags;

	spin_lock_irqsave(&mbox->rx_svc_list_lock, flags);
	list_splice_init(&mbox->rx_svc_init_list, &list_temp);
	spin_unlock_irqrestore(&mbox->rx_svc_list_lock, flags);

	list_for_each_entry_safe(msg, msg_tmp, &list_temp, list_entry) {
		list_del(&msg->list_entry);
		kfree(msg);
	}
}

#define BCM_DEQUEUE_TIMEOUT_MS 30
static int bcm74110_rx_pop_init_msg_block(struct bcm74110_mbox *mbox, u32 func_type,
					  u32 *val)
{
	int ret, timeout = 0;

	do {
		ret = bcm74110_rx_pop_init_msg(mbox, func_type, val);

		if (!ret)
			return 0;

		/* TODO: Figure out what is a good sleep here. */
		usleep_range(1000, 2000);
		timeout++;
	} while (timeout < BCM_DEQUEUE_TIMEOUT_MS);

	dev_warn(&mbox->pdev->dev, "Timeout waiting for service init response\n");
	return -ETIMEDOUT;
}

static int bcm74110_mbox_create_msg(int req, int rply, int svc, int func,
				    int length, int slot)
{
	u32 msg = 0;

	BCM_MSG_SET_FIELD(msg, REQ, req);
	BCM_MSG_SET_FIELD(msg, RPLY, rply);
	BCM_MSG_SET_FIELD(msg, SVC, svc);
	BCM_MSG_SET_FIELD(msg, FUNC, func);
	BCM_MSG_SET_FIELD(msg, LENGTH, length);
	BCM_MSG_SET_FIELD(msg, SLOT, slot);

	return msg;
}

static int bcm74110_mbox_tx_msg(struct bcm74110_mbox *mbox, u32 msg)
{
	int val;

	/* We can potentially poll with timeout here instead */
	val = bcm74110_tx_readl(mbox, BCM_MBOX_STATUS0);
	if (val & BCM_MBOX_STATUS0_FULL) {
		dev_err(&mbox->pdev->dev, "Mailbox full\n");
		return -EINVAL;
	}

	dev_dbg(&mbox->pdev->dev, "tx: [{req=%lu|rply=%lu|srv=%lu|fn=%lu|length=%lu|slot=%lu]\n",
		BCM_MSG_GET_FIELD(msg, REQ), BCM_MSG_GET_FIELD(msg, RPLY),
		BCM_MSG_GET_FIELD(msg, SVC), BCM_MSG_GET_FIELD(msg, FUNC),
		BCM_MSG_GET_FIELD(msg, LENGTH), BCM_MSG_GET_FIELD(msg, SLOT));

	bcm74110_tx_writel(mbox, msg, BCM_MBOX_WDATA);

	return 0;
}

#define BCM_MBOX_LINK_TRAINING_RETRIES	5
static int bcm74110_mbox_link_training(struct bcm74110_mbox *mbox)
{
	int ret, retries = 0;
	u32 msg = 0, orig_len = 0, len = BCM_LINK_CODE0;

	do {
		switch (len) {
		case 0:
			retries++;
			dev_warn(&mbox->pdev->dev,
				 "Link train failed, trying again... %d\n",
				 retries);
			if (retries > BCM_MBOX_LINK_TRAINING_RETRIES)
				return -EINVAL;
			len = BCM_LINK_CODE0;
			fallthrough;
		case BCM_LINK_CODE0:
		case BCM_LINK_CODE1:
		case BCM_LINK_CODE2:
			msg = bcm74110_mbox_create_msg(1, 0, BCM_MSG_SVC_INIT,
						       BCM_MSG_FUNC_LINK_START,
						       len, BCM_MSG_SVC_INIT);
			break;
		default:
			break;
		}

		bcm74110_mbox_tx_msg(mbox, msg);

		/* No response expected for LINK_CODE2 */
		if (len == BCM_LINK_CODE2)
			return 0;

		orig_len = len;

		ret = bcm74110_rx_pop_init_msg_block(mbox,
						     BCM_MSG_GET_FIELD(msg, FUNC),
						     &msg);
		if (ret) {
			len = 0;
			continue;
		}

		if ((BCM_MSG_GET_FIELD(msg, SVC) != BCM_MSG_SVC_INIT) ||
		    (BCM_MSG_GET_FIELD(msg, FUNC) != BCM_MSG_FUNC_LINK_START) ||
		    (BCM_MSG_GET_FIELD(msg, SLOT) != 0) ||
		    (BCM_MSG_GET_FIELD(msg, RPLY) != 1) ||
		    (BCM_MSG_GET_FIELD(msg, REQ) != 0)) {
			len = 0;
			continue;
		}

		len = BCM_MSG_GET_FIELD(msg, LENGTH);

		/* Make sure sequence is good */
		if (len != (orig_len + 1)) {
			len = 0;
			continue;
		}
	} while (1);

	return -EINVAL;
}

static int bcm74110_mbox_tx_msg_and_wait_ack(struct bcm74110_mbox *mbox, u32 msg)
{
	int ret;
	u32 recv_msg;

	ret = bcm74110_mbox_tx_msg(mbox, msg);
	if (ret)
		return ret;

	ret = bcm74110_rx_pop_init_msg_block(mbox, BCM_MSG_GET_FIELD(msg, FUNC),
					     &recv_msg);
	if (ret)
		return ret;

	/*
	 * Modify tx message to verify rx ack.
	 * Flip RPLY/REQ for synchronous messages
	 */
	if (BCM_MSG_GET_FIELD(msg, REQ) == 1) {
		BCM_MSG_SET_FIELD(msg, RPLY, 1);
		BCM_MSG_SET_FIELD(msg, REQ, 0);
	}

	if (msg != recv_msg) {
		dev_err(&mbox->pdev->dev, "Found ack, but ack is invalid\n");
		return -EINVAL;
	}

	return 0;
}

/* Each index points to 0x100 of HAB MEM. IDX size counts from 0 */
#define BCM_MBOX_HAB_MEM_IDX_START	0x30
#define BCM_MBOX_HAB_MEM_IDX_SIZE	0x0
static int bcm74110_mbox_shmem_init(struct bcm74110_mbox *mbox)
{
	u32 msg = 0;
	int ret;

	msg = bcm74110_mbox_create_msg(1, 0, BCM_MSG_SVC_INIT,
				       BCM_MSG_FUNC_SHMEM_STOP,
				       0, BCM_MSG_SVC_INIT);
	ret = bcm74110_mbox_tx_msg_and_wait_ack(mbox, msg);
	if (ret)
		return -EINVAL;

	msg = bcm74110_mbox_create_msg(1, 0, BCM_MSG_SVC_INIT,
				       BCM_MSG_FUNC_SHMEM_TX,
				       BCM_MBOX_HAB_MEM_IDX_START,
				       BCM_MBOX_HAB_MEM_IDX_SIZE);
	ret = bcm74110_mbox_tx_msg_and_wait_ack(mbox, msg);
	if (ret)
		return -EINVAL;

	msg = bcm74110_mbox_create_msg(1, 0, BCM_MSG_SVC_INIT,
				       BCM_MSG_FUNC_SHMEM_RX,
				       BCM_MBOX_HAB_MEM_IDX_START,
				       BCM_MBOX_HAB_MEM_IDX_SIZE);
	ret = bcm74110_mbox_tx_msg_and_wait_ack(mbox, msg);
	if (ret)
		return -EINVAL;

	return 0;
}

static int bcm74110_mbox_init(struct bcm74110_mbox *mbox)
{
	int ret = 0;

	/* Disable queues tx/rx */
	bcm74110_tx_writel(mbox, 0x0, BCM_MBOX_CTRL);

	/* Clear status & restart tx/rx*/
	bcm74110_tx_writel(mbox, BCM_MBOX_CTRL_EN | BCM_MBOX_CTRL_CLR,
			   BCM_MBOX_CTRL);

	/* Unmask irq */
	bcm74110_irq_writel(mbox, BCM_MBOX_IRQ_NOT_EMPTY, BCM_MBOX_IRQ_MASK_CLEAR);

	ret = bcm74110_mbox_link_training(mbox);
	if (ret) {
		dev_err(&mbox->pdev->dev, "Training failed\n");
		return ret;
	}

	return bcm74110_mbox_shmem_init(mbox);
}

static int bcm74110_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct bcm74110_mbox_chan *chan_priv = chan->con_priv;
	u32 msg;

	switch (chan_priv->type) {
	case BCM_MSG_SVC_PMC:
	case BCM_MSG_SVC_SCMI:
	case BCM_MSG_SVC_DPFE:
		msg = bcm74110_mbox_create_msg(1, 0, chan_priv->type, 0,
					       128 + 28, chan_priv->slot);
		break;
	default:
		return -EINVAL;
	}

	return bcm74110_mbox_tx_msg(chan_priv->mbox, msg);
}

static int bcm74110_mbox_chan_startup(struct mbox_chan *chan)
{
	struct bcm74110_mbox_chan *chan_priv = chan->con_priv;

	chan_priv->en = true;

	return 0;
}

static void bcm74110_mbox_chan_shutdown(struct mbox_chan *chan)
{
	struct bcm74110_mbox_chan *chan_priv = chan->con_priv;

	chan_priv->en = false;
}

static const struct mbox_chan_ops bcm74110_mbox_chan_ops = {
	.send_data = bcm74110_mbox_send_data,
	.startup = bcm74110_mbox_chan_startup,
	.shutdown = bcm74110_mbox_chan_shutdown,
};

static void bcm74110_mbox_shutdown(struct platform_device *pdev)
{
	struct bcm74110_mbox *mbox = dev_get_drvdata(&pdev->dev);
	u32 msg;

	msg = bcm74110_mbox_create_msg(1, 0, BCM_MSG_SVC_INIT,
				       BCM_MSG_FUNC_LINK_STOP,
				       0, 0);

	bcm74110_mbox_tx_msg_and_wait_ack(mbox, msg);

	/* Even if we don't receive ACK, lets shut it down */

	bcm74110_mbox_mask_and_clear(mbox);

	/* Disable queues tx/rx */
	bcm74110_tx_writel(mbox, 0x0, BCM_MBOX_CTRL);

	/* Flush queues */
	bcm74110_rx_flush_msg(mbox);
}

static struct mbox_chan *bcm74110_mbox_of_xlate(struct mbox_controller *cntrl,
						const struct of_phandle_args *p)
{
	struct bcm74110_mbox *mbox = bcm74110_mbox_from_cntrl(cntrl);
	struct device *dev = &mbox->pdev->dev;
	struct bcm74110_mbox_chan *chan_priv;
	int slot, type;

	if (p->args_count != 2) {
		dev_err(dev, "Invalid arguments\n");
		return ERR_PTR(-EINVAL);
	}

	type = p->args[0];
	slot = p->args[1];

	switch (type) {
	case BCM_MSG_SVC_PMC:
	case BCM_MSG_SVC_SCMI:
	case BCM_MSG_SVC_DPFE:
		if (slot > BCM_MBOX_HAB_MEM_IDX_SIZE) {
			dev_err(dev, "Not enough shared memory\n");
			return ERR_PTR(-EINVAL);
		}
		chan_priv = cntrl->chans[type].con_priv;
		chan_priv->slot = slot;
		chan_priv->type = type;
		break;
	default:
		dev_err(dev, "Invalid channel type: %d\n", type);
		return ERR_PTR(-EINVAL);
	}

	return &cntrl->chans[type];
}

static int bcm74110_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm74110_mbox *mbox;
	int i, ret;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	mbox->pdev = pdev;
	platform_set_drvdata(pdev, mbox);

	mbox->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mbox->base))
		return dev_err_probe(dev, PTR_ERR(mbox->base), "Failed to iomap\n");

	ret = of_property_read_u32(dev->of_node, "brcm,tx", &mbox->tx_chan);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to find tx channel\n");

	ret = of_property_read_u32(dev->of_node, "brcm,rx", &mbox->rx_chan);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to find rx channel\n");

	mbox->rx_irq = platform_get_irq(pdev, 0);
	if (mbox->rx_irq < 0)
		return mbox->rx_irq;

	INIT_LIST_HEAD(&mbox->rx_svc_init_list);
	spin_lock_init(&mbox->rx_svc_list_lock);
	bcm74110_mbox_mask_and_clear(mbox);

	ret = devm_request_irq(dev, mbox->rx_irq, bcm74110_mbox_isr,
			       IRQF_NO_SUSPEND, pdev->name, mbox);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request irq\n");

	mbox->controller.ops = &bcm74110_mbox_chan_ops;
	mbox->controller.dev = dev;
	mbox->controller.num_chans = BCM_MSG_SVC_MAX;
	mbox->controller.of_xlate = &bcm74110_mbox_of_xlate;
	mbox->controller.chans = devm_kcalloc(dev, BCM_MSG_SVC_MAX,
					      sizeof(*mbox->controller.chans),
					      GFP_KERNEL);
	if (!mbox->controller.chans)
		return -ENOMEM;

	mbox->mbox_chan = devm_kcalloc(dev, BCM_MSG_SVC_MAX,
				       sizeof(*mbox->mbox_chan),
				       GFP_KERNEL);
	if (!mbox->mbox_chan)
		return -ENOMEM;

	for (i = 0; i < BCM_MSG_SVC_MAX; i++) {
		mbox->mbox_chan[i].mbox = mbox;
		mbox->controller.chans[i].con_priv = &mbox->mbox_chan[i];
	}

	ret = devm_mbox_controller_register(dev, &mbox->controller);
	if (ret)
		return ret;

	ret = bcm74110_mbox_init(mbox);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id bcm74110_mbox_of_match[] = {
	{ .compatible = "brcm,bcm74110-mbox", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, bcm74110_mbox_of_match);

static struct platform_driver bcm74110_mbox_driver = {
	.driver = {
		.name = "bcm74110-mbox",
		.of_match_table = bcm74110_mbox_of_match,
		},
	.probe = bcm74110_mbox_probe,
	.shutdown = bcm74110_mbox_shutdown,
};
module_platform_driver(bcm74110_mbox_driver);

MODULE_AUTHOR("Justin Chen <justin.chen@broadcom.com>");
MODULE_DESCRIPTION("BCM74110 mailbox driver");
MODULE_LICENSE("GPL");
