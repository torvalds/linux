// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/mailbox/qmp.h>
#include <linux/ipc_logging.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/qcom_aoss.h>

#define QMP_MAGIC	0x4d41494c	/* MAIL */
#define QMP_VERSION	0x1
#define QMP_FEATURES	0x0
#define QMP_TOUT_MS	5000
#define QMP_TX_TOUT_MS	1000
#define QMP_SMEM_ID	629

#define QMP_MBOX_LINK_DOWN		0xFFFF0000
#define QMP_MBOX_LINK_UP		0x0000FFFF
#define QMP_MBOX_CH_DISCONNECTED	0xFFFF0000
#define QMP_MBOX_CH_CONNECTED		0x0000FFFF

#define MSG_RAM_ALIGN_BYTES 3

#define QMP_IPC_LOG_PAGE_CNT 2
#define QMP_INFO(ctxt, x, ...)						  \
	ipc_log_string(ctxt, "[%s]: "x, __func__, ##__VA_ARGS__)

#define QMP_ERR(ctxt, x, ...)						    \
do {									    \
	printk_ratelimited("%s[%s]: "x, KERN_ERR, __func__, ##__VA_ARGS__); \
	ipc_log_string(ctxt, "%s[%s]: "x, "", __func__, ##__VA_ARGS__);	    \
} while (0)

#ifdef CONFIG_QMP_DEBUGFS_CLIENT
#define QMP_BUG(x) BUG_ON(x)
#else
#define QMP_BUG(x) do {} while (0)
#endif

/**
 * enum qmp_local_state - definition of the local state machine
 * @LINK_DISCONNECTED:		Init state, waiting for ucore to start
 * @LINK_NEGOTIATION:		Set local link state to up, wait for ucore ack
 * @LINK_CONNECTED:		Link state up, channel not connected
 * @LOCAL_CONNECTING:		Channel opening locally, wait for ucore ack
 * @LOCAL_CONNECTED:		Channel opened locally
 * @CHANNEL_CONNECTED:		Channel fully opened
 * @LOCAL_DISCONNECTING:	Channel closing locally, wait for ucore ack
 */
enum qmp_local_state {
	LINK_DISCONNECTED,
	LINK_NEGOTIATION,
	LINK_CONNECTED,
	LOCAL_CONNECTING,
	LOCAL_CONNECTED,
	CHANNEL_CONNECTED,
	LOCAL_DISCONNECTING,
};

/**
 * struct channel_desc - description of a core's link, channel and mailbox state
 * @link_state		Current link state of core
 * @link_state_ack	Ack for other core to use when link state changes
 * @ch_state		Current channel state of core
 * @ch_state_ack	Ack for other core to use when channel state changes
 * @mailbox_size	Size of this core's mailbox
 * @mailbox_offset	Location of core's mailbox from a base smem location
 */
struct channel_desc {
	u32 link_state;
	u32 link_state_ack;
	u32 ch_state;
	u32 ch_state_ack;
	u32 mailbox_size;
	u32 mailbox_offset;
};

/**
 * struct mbox_desc - description of the protocol's mailbox state
 * @magic	Magic number field to be set by ucore
 * @version	Version field to be set by ucore
 * @features	Features field to be set by ucore
 * @ucore	Channel descriptor to hold state of ucore
 * @mcore	Channel descriptor to hold state of mcore
 * @reserved	Reserved in case of future use
 *
 * This structure resides in SMEM and contains the control information for the
 * mailbox channel. Each core in the link will have one channel descriptor
 */
struct mbox_desc {
	u32 magic;
	u32 version;
	u32 features;
	struct channel_desc ucore;
	struct channel_desc mcore;
	u32 reserved;
};

/**
 * struct qmp_core_version - local structure to hold version and features
 * @version	Version field to indicate what version the ucore supports
 * @features	Features field to indicate what features the ucore supports
 */
struct qmp_core_version {
	u32 version;
	u32 features;
};

/**
 * struct qmp_mbox - local information for managing a single mailbox
 * @list:		List head for adding mbox to linked list
 * @ctrl:		Controller for this mailbox
 * @priority:		Priority of mailbox in the linked list
 * @num_assigned:	Number of channels assigned for allocated pool
 * @num_shutdown:	Number of channels that have shutdown
 * @desc:		Reference to the mailbox descriptor in SMEM
 * @rx_disabled:	Disable rx if multiple client are sending from this mbox
 * @tx_sent:		True if tx is sent and remote proc has not sent ack
 * @idx_in_flight:	current channel idx whos tx is in flight
 * @mcore_mbox_offset:	Offset of mcore mbox from the msgram start
 * @mcore_mbox_size:	Size of the mcore mbox
 * @rx_pkt:		buffer to pass to client, holds copied data from mailbox
 * @version:		Version and features received during link negotiation
 * @local_state:	Current state of the mailbox protocol
 * @state_lock:		Serialize mailbox state changes
 * @tx_lock:		Serialize access for writes to mailbox
 * @link_complete:	Use to block until link negotiation with remote proc
 * @ch_complete:	Use to block until the channel is fully opened
 * @ch_in_use:		True if this mailbox's channel owned by a client
 * @dwork:		Delayed work to detect timed out tx
 */
struct qmp_mbox {
	struct list_head list;
	struct mbox_controller ctrl;
	int priority;
	u32 num_assigned;
	u32 num_shutdown;

	void __iomem *desc;
	bool rx_disabled;
	bool tx_sent;
	u32 idx_in_flight;
	u32 mcore_mbox_offset;
	u32 mcore_mbox_size;
	struct qmp_pkt rx_pkt;
	struct qmp_pkt tx_pkt;
	struct work_struct tx_work;

	struct qmp_core_version version;
	enum qmp_local_state local_state;
	struct mutex state_lock;
	spinlock_t tx_lock;

	struct completion link_complete;
	struct completion ch_complete;
	struct delayed_work dwork;
	struct qmp_device *mdev;
	bool suspend_flag;
};

/**
 * struct qmp_device - local information for managing a single qmp edge
 * @dev:		The device that corresponds to this edge
 * @name:		The name of this mailbox
 * @mboxes:		The mbox controller for this mailbox
 * @msgram:		Reference to the start of msgram
 * @tx_irq_reg:		Reference to the register to send an irq to remote proc
 * @rx_reset_reg:	Reference to the register to reset the rx irq, if
 *			applicable
 * @irq_mask:		Mask written to @tx_irq_reg to trigger irq
 * @rx_irq_line:	The incoming interrupt line
 * @tx_irq_count:	Number of tx interrupts triggered
 * @rx_irq_count:	Number of rx interrupts received
 * @ilc:		IPC logging context
 */
struct qmp_device {
	struct device *dev;
	const char *name;
	struct list_head mboxes;

	void __iomem *msgram;
	void __iomem *tx_irq_reg;
	void __iomem *rx_reset_reg;

	u32 irq_mask;
	u32 rx_irq_line;
	u32 tx_irq_count;
	u32 rx_irq_count;

	struct mbox_client mbox_client;
	struct mbox_chan *mbox_chan;

	struct qmp *qmp;

	void *ilc;
	bool early_boot;
};

/**
 * send_irq() - send an irq to a remote entity as an event signal.
 * @mdev:	Which remote entity that should receive the irq.
 */
static void send_irq(struct qmp_device *mdev)
{
	/*
	 * Any data associated with this event must be visable to the remote
	 * before the interrupt is triggered
	 */
	wmb();

	if (mdev->mbox_chan) {
		mbox_send_message(mdev->mbox_chan, NULL);
		mbox_client_txdone(mdev->mbox_chan, 0);
	} else {
		writel_relaxed(mdev->irq_mask, mdev->tx_irq_reg);
	}
	mdev->tx_irq_count++;
}

static void memcpy32_toio(void __iomem *dest, void *src, size_t size)
{
	u32 *dest_local = (u32 *)dest;
	u32 *src_local = (u32 *)src;

	WARN_ON(size & MSG_RAM_ALIGN_BYTES);
	size /= sizeof(u32);
	while (size--)
		iowrite32(*src_local++, dest_local++);
}

static void memcpy32_fromio(void *dest, void __iomem *src, size_t size)
{
	u32 *dest_local = (u32 *)dest;
	u32 *src_local = (u32 *)src;

	WARN_ON(size & MSG_RAM_ALIGN_BYTES);
	size /= sizeof(u32);
	while (size--)
		*dest_local++ = ioread32(src_local++);
}

/**
 * qmp_notify_timeout() - Notify client of tx timeout with -ETIME
 * @work:	Structure for work that was scheduled.
 */
static void qmp_notify_timeout(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qmp_mbox *mbox = container_of(dwork, struct qmp_mbox, dwork);
	struct mbox_chan *chan = &mbox->ctrl.chans[mbox->idx_in_flight];
	int err = -ETIME;
	unsigned long flags;

	spin_lock_irqsave(&mbox->tx_lock, flags);
	if (!mbox->tx_sent) {
		spin_unlock_irqrestore(&mbox->tx_lock, flags);
		return;
	}
	QMP_ERR(mbox->mdev->ilc, "tx timeout for %d\n", mbox->idx_in_flight);
	QMP_BUG(mbox->tx_sent);
	iowrite32(0, mbox->desc + mbox->mcore_mbox_offset);
	mbox->tx_sent = false;
	spin_unlock_irqrestore(&mbox->tx_lock, flags);
	mbox_chan_txdone(chan, err);
}

static inline void qmp_schedule_tx_timeout(struct qmp_mbox *mbox)
{
	schedule_delayed_work(&mbox->dwork, msecs_to_jiffies(QMP_TX_TOUT_MS));
}

/**
 * set_ucore_link_ack() - set the link ack in the ucore channel desc.
 * @mbox:	the mailbox for the field that is being set.
 * @state:	the value to set the ack field to.
 */
static void set_ucore_link_ack(struct qmp_mbox *mbox, u32 state)
{
	u32 offset;

	offset = offsetof(struct mbox_desc, ucore);
	offset += offsetof(struct channel_desc, link_state_ack);
	iowrite32(state, mbox->desc + offset);
}

/**
 * set_ucore_ch_ack() - set the channel ack in the ucore channel desc.
 * @mbox:	the mailbox for the field that is being set.
 * @state:	the value to set the ack field to.
 */
static void set_ucore_ch_ack(struct qmp_mbox *mbox, u32 state)
{
	u32 offset;

	offset = offsetof(struct mbox_desc, ucore);
	offset += offsetof(struct channel_desc, ch_state_ack);
	iowrite32(state, mbox->desc + offset);
}

/**
 * set_mcore_ch() - set the channel state in the mcore channel desc.
 * @mbox:	the mailbox for the field that is being set.
 * @state:	the value to set the channel field to.
 */
static void set_mcore_ch(struct qmp_mbox *mbox, u32 state)
{
	u32 offset;

	offset = offsetof(struct mbox_desc, mcore);
	offset += offsetof(struct channel_desc, ch_state);
	iowrite32(state, mbox->desc + offset);
}

/**
 * qmp_startup() - Start qmp mailbox channel for communication. Waits for
 *			remote subsystem to open channel if link is not
 *			initated or until timeout.
 * @chan:	mailbox channel that is being opened.
 *
 * Return: 0 on succes or standard Linux error code.
 */
static int qmp_startup(struct mbox_chan *chan)
{
	struct qmp_mbox *mbox = chan->con_priv;
	unsigned long ret;

	if (!mbox)
		return -EINVAL;

	ret = wait_for_completion_timeout(&mbox->link_complete,
					  msecs_to_jiffies(QMP_TOUT_MS));
	if (!ret)
		return -EAGAIN;

	mutex_lock(&mbox->state_lock);
	if (mbox->local_state == LINK_CONNECTED) {
		set_mcore_ch(mbox, QMP_MBOX_CH_CONNECTED);
		mbox->local_state = LOCAL_CONNECTING;
		send_irq(mbox->mdev);
	}
	mutex_unlock(&mbox->state_lock);

	ret = wait_for_completion_timeout(&mbox->ch_complete,
					msecs_to_jiffies(QMP_TOUT_MS));
	if (!ret)
		return -ETIME;

	return 0;
}

/**
 * qmp_send_data() - Copy the data to the channel's mailbox and notify
 *				remote subsystem of new data. This function will
 *				return an error if the previous message sent has
 *				not been read. Cannot Sleep.
 * @chan:	mailbox channel that data is to be sent over.
 * @data:	Data to be sent to remote processor, should be in the format of
 *		a qmp_pkt.
 *
 * Return: 0 on succes or standard Linux error code.
 */
static int qmp_send_data(struct mbox_chan *chan, void *data)
{
	struct qmp_mbox *mbox = chan->con_priv;
	struct qmp_device *mdev;
	struct qmp_pkt *pkt = (struct qmp_pkt *)data;
	void __iomem *addr;
	unsigned long flags;
	u32 size;
	int i;

	if (!mbox || !data || !completion_done(&mbox->ch_complete))
		return -EINVAL;

	mdev = mbox->mdev;

	spin_lock_irqsave(&mbox->tx_lock, flags);
	addr = mbox->desc + mbox->mcore_mbox_offset;
	if (mbox->tx_sent) {
		spin_unlock_irqrestore(&mbox->tx_lock, flags);
		return -EAGAIN;
	}

	if (pkt->size + sizeof(pkt->size) > mbox->mcore_mbox_size) {
		spin_unlock_irqrestore(&mbox->tx_lock, flags);
		return -EINVAL;
	}

	memcpy32_toio(addr + sizeof(pkt->size), pkt->data, pkt->size);
	iowrite32(pkt->size, addr);
	/* readback to ensure write reflects in msgram */
	size = ioread32(addr);
	mbox->tx_sent = true;
	for (i = 0; i < mbox->ctrl.num_chans; i++) {
		if (chan == &mbox->ctrl.chans[i])
			mbox->idx_in_flight = i;
	}
	QMP_INFO(mdev->ilc, "Copied buffer to msgram sz:%d i:%d\n",
		 size, mbox->idx_in_flight);
	send_irq(mdev);
	qmp_schedule_tx_timeout(mbox);
	spin_unlock_irqrestore(&mbox->tx_lock, flags);
	return 0;
}

/**
 * qmp_shutdown() - Disconnect this mailbox channel so the client does not
 *				receive anymore data and can reliquish control
 *				of the channel
 * @chan:	mailbox channel to be shutdown.
 */
static void qmp_shutdown(struct mbox_chan *chan)
{
	struct qmp_mbox *mbox = chan->con_priv;

	mutex_lock(&mbox->state_lock);
	if (mbox->local_state <= LINK_CONNECTED) {
		mbox->num_assigned--;
		goto out;
	}

	if (mbox->local_state == LOCAL_CONNECTING) {
		mbox->num_assigned--;
		mbox->local_state = LINK_CONNECTED;
		goto out;
	}

	mbox->num_shutdown++;
	if (mbox->num_shutdown < mbox->num_assigned)
		goto out;

	if (mbox->local_state != LINK_DISCONNECTED) {
		mbox->local_state = LOCAL_DISCONNECTING;
		set_mcore_ch(mbox, QMP_MBOX_CH_DISCONNECTED);
		send_irq(mbox->mdev);
	}
	mbox->num_shutdown = 0;
	mbox->num_assigned = 0;
out:
	mutex_unlock(&mbox->state_lock);
}

/**
 * qmp_last_tx_done() - qmp does not support polling operations, print
 *				error of unexpected usage and return true to
 *				resume operation.
 * @chan:	Corresponding mailbox channel for requested last tx.
 *
 * Return: true
 */
static bool qmp_last_tx_done(struct mbox_chan *chan)
{
	pr_err("In %s, unexpected usage of last_tx_done\n", __func__);
	return true;
}

/**
 * qmp_recv_data() - received notification that data is available in the
 *			mailbox. Copy data from mailbox and pass to client.
 * @mbox:		mailbox device that received the notification.
 * @mbox_of:	offset of mailbox from msgram start.
 */
static void qmp_recv_data(struct qmp_mbox *mbox, u32 mbox_of)
{
	void __iomem *addr;
	struct qmp_pkt *pkt;
	size_t read_size;

	addr = mbox->desc + mbox_of;
	pkt = &mbox->rx_pkt;
	pkt->size = ioread32(addr);

	read_size = (pkt->size + 0x3) & ~0x3;
	if (read_size > mbox->mcore_mbox_size)
		QMP_ERR(mbox->mdev->ilc, "Invalid mailbox packet\n");
	else {
		memcpy32_fromio(pkt->data, addr + sizeof(pkt->size), read_size);
		mbox_chan_received_data(&mbox->ctrl.chans[mbox->idx_in_flight],
					pkt);
	}
	iowrite32(0, addr);
	QMP_INFO(mbox->mdev->ilc, "recv sz:%d\n", pkt->size);
	send_irq(mbox->mdev);
}

/**
 * init_mcore_state() - initialize the mcore state of a mailbox.
 * @mdev:	mailbox device to be initialized.
 */
static void init_mcore_state(struct qmp_mbox *mbox)
{
	struct channel_desc mcore;
	u32 offset = offsetof(struct mbox_desc, mcore);

	mcore.link_state = QMP_MBOX_LINK_UP;
	mcore.link_state_ack = QMP_MBOX_LINK_DOWN;
	mcore.ch_state = QMP_MBOX_CH_DISCONNECTED;
	mcore.ch_state_ack = QMP_MBOX_CH_DISCONNECTED;
	mcore.mailbox_size = mbox->mcore_mbox_size;
	mcore.mailbox_offset = mbox->mcore_mbox_offset;
	memcpy32_toio(mbox->desc + offset, &mcore, sizeof(mcore));
}

/**
 * qmp_irq_handler() - handle irq from remote entitity.
 * @irq:	irq number for the trggered interrupt.
 * @priv:	private pointer to qmp mbox device.
 */
static irqreturn_t qmp_irq_handler(int irq, void *priv)
{
	struct qmp_device *mdev = (struct qmp_device *)priv;

	if (mdev->rx_reset_reg)
		writel_relaxed(mdev->irq_mask, mdev->rx_reset_reg);

	mdev->rx_irq_count++;

	return IRQ_WAKE_THREAD;
}

/**
 * __qmp_rx_worker() - Handle incoming messages from remote processor.
 * @mbox:	mailbox device that received notification.
 */
static void __qmp_rx_worker(struct qmp_mbox *mbox)
{
	u32 msg_len, idx;
	struct mbox_desc desc;
	struct qmp_device *mdev = mbox->mdev;
	unsigned long flags;

	memcpy_fromio(&desc, mbox->desc, sizeof(desc));
	if (desc.magic != QMP_MAGIC)
		return;

	mutex_lock(&mbox->state_lock);
	switch (mbox->local_state) {
	case LINK_DISCONNECTED:
		mbox->version.version = desc.version;
		mbox->version.features = desc.features;
		set_ucore_link_ack(mbox, desc.ucore.link_state);
		if (desc.mcore.mailbox_size) {
			mbox->mcore_mbox_size = desc.mcore.mailbox_size;
			mbox->mcore_mbox_offset = desc.mcore.mailbox_offset;
		}
		init_mcore_state(mbox);
		mbox->local_state = LINK_NEGOTIATION;
		mbox->rx_pkt.data = kzalloc(desc.ucore.mailbox_size,
					    GFP_KERNEL);
		if (!mbox->rx_pkt.data) {
			QMP_ERR(mdev->ilc, "Failed to allocate rx pkt\n");
			break;
		}
		QMP_INFO(mdev->ilc, "Set to link negotiation\n");
		send_irq(mdev);
		break;
	case LINK_NEGOTIATION:
		if (desc.mcore.link_state_ack != QMP_MBOX_LINK_UP ||
				desc.mcore.link_state != QMP_MBOX_LINK_UP) {
			QMP_ERR(mdev->ilc, "RX int without negotiation ack\n");
			break;
		}
		mbox->local_state = LINK_CONNECTED;
		complete_all(&mbox->link_complete);
		QMP_INFO(mdev->ilc, "Set to link connected\n");
		/*
		 * If link connection happened after hibernation
		 * manualy trigger the channel open procedure since client
		 * won't try to re-open the channel
		 */
		if (mbox->suspend_flag) {
			set_mcore_ch(mbox, QMP_MBOX_CH_CONNECTED);
			mbox->local_state = LOCAL_CONNECTING;
			send_irq(mbox->mdev);
		}
		break;
	case LINK_CONNECTED:
		if (desc.ucore.ch_state == desc.ucore.ch_state_ack) {
			QMP_ERR(mdev->ilc, "RX int without ch open\n");
			break;
		}
		set_ucore_ch_ack(mbox, desc.ucore.ch_state);
		send_irq(mdev);
		QMP_INFO(mdev->ilc, "Received remote ch open\n");
		break;
	case LOCAL_CONNECTING:
		if (desc.mcore.ch_state_ack == QMP_MBOX_CH_CONNECTED &&
				desc.mcore.ch_state == QMP_MBOX_CH_CONNECTED) {
			mbox->local_state = LOCAL_CONNECTED;
			QMP_INFO(mdev->ilc, "Received local ch open ack\n");
		}

		if (desc.ucore.ch_state != desc.ucore.ch_state_ack) {
			set_ucore_ch_ack(mbox, desc.ucore.ch_state);
			send_irq(mdev);
			QMP_INFO(mdev->ilc, "Received remote channel open\n");
		}
		if (mbox->local_state == LOCAL_CONNECTED &&
				desc.mcore.ch_state == QMP_MBOX_CH_CONNECTED &&
				desc.ucore.ch_state == QMP_MBOX_CH_CONNECTED) {
			mbox->local_state = CHANNEL_CONNECTED;
			complete_all(&mbox->ch_complete);
			QMP_INFO(mdev->ilc, "Set to channel connected\n");
		}
		break;
	case LOCAL_CONNECTED:
		if (desc.ucore.ch_state == desc.ucore.ch_state_ack) {
			QMP_ERR(mdev->ilc, "RX int without remote ch open\n");
			break;
		}
		set_ucore_ch_ack(mbox, desc.ucore.ch_state);
		mbox->local_state = CHANNEL_CONNECTED;
		send_irq(mdev);
		complete_all(&mbox->ch_complete);
		QMP_INFO(mdev->ilc, "Set to channel connected\n");
		break;
	case CHANNEL_CONNECTED:
		if (desc.ucore.ch_state == QMP_MBOX_CH_DISCONNECTED) {
			set_ucore_ch_ack(mbox, desc.ucore.ch_state);
			mbox->local_state = LOCAL_CONNECTED;
			QMP_INFO(mdev->ilc, "REMOTE DISCONNECT\n");
			send_irq(mdev);
		}

		msg_len = ioread32(mbox->desc + desc.ucore.mailbox_offset);
		if (msg_len && !mbox->rx_disabled)
			qmp_recv_data(mbox, desc.ucore.mailbox_offset);

		spin_lock_irqsave(&mbox->tx_lock, flags);
		idx = mbox->idx_in_flight;
		if (mbox->tx_sent) {
			msg_len = ioread32(mbox->desc +
						mbox->mcore_mbox_offset);
			if (msg_len == 0) {
				mbox->tx_sent = false;
				cancel_delayed_work(&mbox->dwork);
				QMP_INFO(mdev->ilc, "TX flag cleared, idx%d\n",
					 idx);

				spin_unlock_irqrestore(&mbox->tx_lock, flags);
				mbox_chan_txdone(&mbox->ctrl.chans[idx], 0);
				spin_lock_irqsave(&mbox->tx_lock, flags);
			}
		}
		spin_unlock_irqrestore(&mbox->tx_lock, flags);
		break;
	case LOCAL_DISCONNECTING:
		if (desc.mcore.ch_state_ack == QMP_MBOX_CH_DISCONNECTED &&
			desc.mcore.ch_state == desc.mcore.ch_state_ack) {

			mbox->local_state = LINK_CONNECTED;
			QMP_INFO(mdev->ilc, "Channel closed\n");
			reinit_completion(&mbox->ch_complete);
		}
		break;
	default:
		QMP_ERR(mdev->ilc, "Local Channel State corrupted\n");
	}
	mutex_unlock(&mbox->state_lock);
}

static irqreturn_t qmp_thread_irq_handler(int irq, void *priv)
{
	struct qmp_device *mdev = (struct qmp_device *)priv;
	struct qmp_mbox *mbox;

	list_for_each_entry(mbox, &mdev->mboxes, list) {
		__qmp_rx_worker(mbox);
	}

	return IRQ_HANDLED;
}

/**
 * qmp_mbox_of_xlate() - Returns a mailbox channel to be used for this mailbox
 *			device. Make sure the channel is not already in use.
 * @mbox:	Mailbox device controlls the requested channel.
 * @spec:	Device tree arguments to specify which channel is requested.
 */
static struct mbox_chan *qmp_mbox_of_xlate(struct mbox_controller *mbox,
		const struct of_phandle_args *spec)
{
	struct qmp_mbox *dev = container_of(mbox, struct qmp_mbox, ctrl);
	struct mbox_chan *chan;

	if (dev->num_assigned >= mbox->num_chans || !dev->ctrl.chans) {
		pr_err("%s: QMP out of channels\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	mutex_lock(&dev->state_lock);
	chan = &dev->ctrl.chans[dev->num_assigned++];
	mutex_unlock(&dev->state_lock);

	return chan;
}

/**
 * get_mbox_num_chans() - Find how many mbox channels need to be allocated
 *
 * @node:	device node for this mailbox.
 *
 * Return: the number of phandles referring to this device node
 */
static u32 get_mbox_num_chans(struct device_node *node)
{
	int i, j, ret;
	u32 num_chans = 0;
	struct device_node *np;
	struct of_phandle_args p;

	for_each_node_with_property(np, "mboxes") {
		if (!of_device_is_available(np))
			continue;
		i = of_count_phandle_with_args(np, "mboxes", "#mbox-cells");
		for (j = 0; j < i; j++) {
			ret = of_parse_phandle_with_args(np, "mboxes",
							"#mbox-cells", j, &p);
			if (!ret && p.np == node) {
				num_chans++;
				break;
			}
		}
	}
	if (num_chans)
		return num_chans;

	return 1;
}

/**
 * mdev_add_mbox() - Add a mailbox to qmp device based on priority
 *
 * @mdev:	qmp device to add mailbox to.
 * @new:	new mailbox to add to qmp device.
 */
static void mdev_add_mbox(struct qmp_device *mdev, struct qmp_mbox *new)
{
	struct qmp_mbox *mbox;

	list_for_each_entry(mbox, &mdev->mboxes, list) {
		if (mbox->priority > new->priority)
			continue;
		list_add_tail(&new->list, &mbox->list);
		return;
	}
	list_add_tail(&new->list, &mdev->mboxes);
}

static struct mbox_chan_ops qmp_mbox_ops = {
	.startup = qmp_startup,
	.shutdown = qmp_shutdown,
	.send_data = qmp_send_data,
	.last_tx_done = qmp_last_tx_done,
};

static int qmp_shim_startup(struct mbox_chan *chan)
{
	struct qmp_mbox *mbox = chan->con_priv;

	if (!mbox || !mbox->mdev)
		return -EINVAL;

	if (!mbox->mdev->qmp)
		return -EAGAIN;

	return 0;
}

static void qmp_shim_shutdown(struct mbox_chan *chan) { }

static void qmp_shim_worker(struct work_struct *work)
{
	struct qmp_mbox *mbox = container_of(work, struct qmp_mbox, tx_work);
	struct qmp_pkt *pkt = &mbox->tx_pkt;
	int rc;

	rc = qmp_send(mbox->mdev->qmp, pkt->data, pkt->size);
	mbox_chan_txdone(&mbox->ctrl.chans[mbox->idx_in_flight], rc);
}

static int qmp_shim_send_data(struct mbox_chan *chan, void *data)
{
	struct qmp_mbox *mbox = chan->con_priv;
	struct qmp_pkt *pkt = (struct qmp_pkt *)data;
	int i;

	if (!mbox || !mbox->mdev || !data)
		return -EINVAL;

	if (pkt->size > SZ_4K)
		return -EINVAL;

	for (i = 0; i < mbox->ctrl.num_chans; i++) {
		if (chan == &mbox->ctrl.chans[i]) {
			mbox->idx_in_flight = i;
			break;
		}
	}

	mbox->tx_pkt.size = pkt->size;
	memcpy(mbox->tx_pkt.data, pkt->data, pkt->size);
	schedule_work(&mbox->tx_work);
	return 0;
}

static struct mbox_chan_ops qmp_mbox_shim_ops = {
	.startup = qmp_shim_startup,
	.shutdown = qmp_shim_shutdown,
	.send_data = qmp_shim_send_data,
	.last_tx_done = qmp_last_tx_done,
};

/**
 * qmp_mbox_init() - Parse the device tree for qmp mailbox and init structure
 *
 * @n:		child device node representing a mailbox.
 * @mbox:	device structure for this edge.
 *
 * Return: 0 on succes or standard Linux error code.
 */
static int qmp_mbox_init(struct device_node *n, struct qmp_device *mdev)
{
	int rc, i;
	char *key;
	struct qmp_mbox *mbox;
	struct mbox_chan *chans;
	u32 mbox_of, mbox_size, desc_of, priority, num_chans;

	key = "mbox-desc-offset";
	rc = of_property_read_u32(n, key, &desc_of);
	if (rc) {
		pr_err("%s: missing key %s\n", __func__, key);
		return 0;
	}
	key = "priority";
	rc = of_property_read_u32(n, key, &priority);
	if (rc) {
		pr_err("%s: missing key %s\n", __func__, key);
		return 0;
	}
	mbox = devm_kzalloc(mdev->dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	rc = of_property_read_u32(n, "mbox-offset", &mbox_of);
	if (!rc)
		mbox->mcore_mbox_offset = mbox_of;
	rc = of_property_read_u32(n, "mbox-size", &mbox_size);
	if (!rc)
		mbox->mcore_mbox_size = mbox_size;

	mbox->mdev = mdev;
	mbox->priority = priority;
	mbox->desc = mdev->msgram + desc_of;
	num_chans = get_mbox_num_chans(n);
	mbox->rx_disabled = (num_chans > 1) ? true : false;
	chans = devm_kzalloc(mdev->dev, sizeof(*chans) * num_chans, GFP_KERNEL);
	if (!chans)
		return -ENOMEM;

	for (i = 0; i < num_chans; i++)
		chans[i].con_priv = mbox;

	mbox->ctrl.dev = mdev->dev;
	mbox->ctrl.ops = &qmp_mbox_ops;
	mbox->ctrl.chans = chans;
	mbox->ctrl.num_chans = num_chans;
	mbox->ctrl.txdone_irq = true;
	mbox->ctrl.txdone_poll = false;
	mbox->ctrl.of_xlate = qmp_mbox_of_xlate;

	rc = mbox_controller_register(&mbox->ctrl);
	if (rc) {
		pr_err("%s: failed to register mbox controller %d\n", __func__,
				rc);
		return rc;
	}
	spin_lock_init(&mbox->tx_lock);
	mutex_init(&mbox->state_lock);
	mbox->local_state = LINK_DISCONNECTED;
	init_completion(&mbox->link_complete);
	init_completion(&mbox->ch_complete);
	mbox->tx_sent = false;
	mbox->num_assigned = 0;
	INIT_DELAYED_WORK(&mbox->dwork, qmp_notify_timeout);
	mbox->suspend_flag = false;

	mdev_add_mbox(mdev, mbox);
	return 0;
}

static int qmp_parse_ipc(struct platform_device *pdev)
{
	struct qmp_device *mdev = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct resource *res;
	int rc;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "irq-reg-base");
	if (!res) {
		pr_err("%s: missing key irq-reg-base\n", __func__);
		return -ENODEV;
	}

	rc = of_property_read_u32(node, "qcom,irq-mask", &mdev->irq_mask);
	if (rc) {
		pr_err("%s: missing key qcom,irq-mask\n", __func__);
		return -ENODEV;
	}

	mdev->tx_irq_reg = devm_ioremap(&pdev->dev, res->start,
						resource_size(res));
	if (!mdev->tx_irq_reg) {
		pr_err("%s: unable to map tx irq reg\n", __func__);
		return -EIO;
	}
	return 0;
}

static int qmp_shim_init(struct platform_device *pdev, struct qmp_device *mdev)
{
	struct mbox_chan *chans;
	struct qmp_mbox *mbox;
	u32 num_chans;
	int rc;
	int i;

	mdev->qmp = qmp_get(&pdev->dev);
	if (IS_ERR(mdev->qmp))
		return PTR_ERR(mdev->qmp);

	mdev->name = of_get_property(pdev->dev.of_node, "label", NULL);
	if (!mdev->name) {
		pr_err("%s: missing label\n", __func__);
		return -ENODEV;
	}
	INIT_LIST_HEAD(&mdev->mboxes);
	mdev->dev = &pdev->dev;

	mbox = devm_kzalloc(mdev->dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	mbox->tx_pkt.data = devm_kzalloc(mdev->dev, SZ_4K, GFP_KERNEL);
	if (!mbox->tx_pkt.data)
		return -ENOMEM;

	num_chans = get_mbox_num_chans(pdev->dev.of_node);
	mbox->rx_disabled = (num_chans > 1) ? true : false;
	chans = devm_kzalloc(mdev->dev, sizeof(*chans) * num_chans, GFP_KERNEL);
	if (!chans)
		return -ENOMEM;

	for (i = 0; i < num_chans; i++)
		chans[i].con_priv = mbox;

	mbox->ctrl.dev = mdev->dev;
	mbox->ctrl.ops = &qmp_mbox_shim_ops;
	mbox->ctrl.chans = chans;
	mbox->ctrl.num_chans = num_chans;
	mbox->ctrl.txdone_irq = true;
	mbox->ctrl.txdone_poll = false;
	mbox->ctrl.of_xlate = qmp_mbox_of_xlate;

	mutex_init(&mbox->state_lock);
	mbox->num_assigned = 0;
	mbox->mdev = mdev;
	INIT_WORK(&mbox->tx_work, qmp_shim_worker);

	rc = mbox_controller_register(&mbox->ctrl);
	if (rc) {
		pr_err("%s: failed to register mbox ctrl %d\n", __func__, rc);
		return rc;
	}
	mdev_add_mbox(mdev, mbox);
	mdev->ilc = ipc_log_context_create(QMP_IPC_LOG_PAGE_CNT, mdev->name, 0);

	return 0;
}

static int qmp_smem_init(struct qmp_device *mdev, struct device_node *node)
{
	void __iomem *buf;
	u32 remote_pid;
	size_t size;
	int ret;

	ret = of_property_read_u32(node, "qcom,remote-pid", &remote_pid);
	if (ret) {
		pr_err("failed to parse qcom,remote-pid %d\n", ret);
		return ret;
	}

	buf = qcom_smem_get(remote_pid, QMP_SMEM_ID, &size);
	if (IS_ERR_OR_NULL(buf))
		return PTR_ERR(buf);

	mdev->msgram = buf;

	return 0;
}

/**
 * qmp_edge_init() - Parse the device tree information for QMP, map io
 *			memory and register for needed interrupts
 * @pdev:	platform device for this driver.
 *
 * Return: 0 on succes or standard Linux error code.
 */
static int qmp_edge_init(struct platform_device *pdev)
{
	struct qmp_device *mdev = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct resource *msgram_r;
	int rc;

	mdev->name = of_get_property(node, "label", NULL);
	if (!mdev->name) {
		pr_err("%s: missing label\n", __func__);
		return -ENODEV;
	}
	INIT_LIST_HEAD(&mdev->mboxes);
	mdev->dev = &pdev->dev;

	msgram_r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "msgram");
	if (!msgram_r) {
		/* fallback to smem mailbox */
		rc = qmp_smem_init(mdev, node);
		if (rc)
			return rc;
	} else {
		mdev->msgram = devm_ioremap(&pdev->dev, msgram_r->start,
					    resource_size(msgram_r));
		if (!mdev->msgram)
			return -EIO;
	}

	mdev->mbox_client.dev = &pdev->dev;
	mdev->mbox_client.knows_txdone = true;
	mdev->mbox_chan = mbox_request_channel(&mdev->mbox_client, 0);
	if (IS_ERR(mdev->mbox_chan)) {
		if (PTR_ERR(mdev->mbox_chan) != -ENODEV)
			return PTR_ERR(mdev->mbox_chan);

		mdev->mbox_chan = NULL;

		rc = qmp_parse_ipc(pdev);
		if (rc)
			return rc;
	}

	mdev->rx_irq_line = irq_of_parse_and_map(node, 0);
	if (!mdev->rx_irq_line) {
		pr_err("%s: missing interrupts\n", __func__);
		return -ENODEV;
	}

	return 0;
}

static int qmp_mbox_remove(struct platform_device *pdev)
{
	struct qmp_device *mdev = platform_get_drvdata(pdev);
	struct qmp_mbox *mbox = NULL;

	disable_irq(mdev->rx_irq_line);

	list_for_each_entry(mbox, &mdev->mboxes, list) {
		mbox_controller_unregister(&mbox->ctrl);
		kfree(mbox->rx_pkt.data);
	}
	return 0;
}

static int qmp_mbox_probe(struct platform_device *pdev)
{
	struct device_node *edge_node = pdev->dev.of_node;
	struct qmp_device *mdev;
	struct qmp_mbox *mbox;
	int ret = 0;

	mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;
	platform_set_drvdata(pdev, mdev);

	if (of_parse_phandle(edge_node, "qcom,qmp", 0))
		return qmp_shim_init(pdev, mdev);

	ret = qmp_edge_init(pdev);
	if (ret)
		return ret;

	ret = qmp_mbox_init(edge_node, mdev);
	if (ret)
		return ret;

	dev_set_drvdata(&pdev->dev, mdev);

	mdev->ilc = ipc_log_context_create(QMP_IPC_LOG_PAGE_CNT, mdev->name, 0);

	ret = devm_request_threaded_irq(&pdev->dev, mdev->rx_irq_line,
					qmp_irq_handler, qmp_thread_irq_handler,
					IRQF_TRIGGER_RISING,
					edge_node->name, (void *)mdev);
	if (ret < 0) {
		qmp_mbox_remove(pdev);
		QMP_ERR(mdev->ilc, "request threaded irq on %d failed: %d\n",
			mdev->rx_irq_line, ret);
		return ret;
	}
	ret = enable_irq_wake(mdev->rx_irq_line);
	if (ret < 0)
		QMP_ERR(mdev->ilc, "enable_irq_wake on %d failed: %d\n",
			mdev->rx_irq_line, ret);

	/* Trigger fake RX in case of missed interrupt */
	if (of_property_read_bool(edge_node, "qcom,early-boot")) {
		list_for_each_entry(mbox, &mdev->mboxes, list) {
			__qmp_rx_worker(mbox);
		}
		mdev->early_boot = true;
	}

	return 0;
}

static int qmp_mbox_freeze(struct device *dev)
{
	return 0;
}

static int qmp_mbox_restore(struct device *dev)
{
	struct qmp_device *mdev = dev_get_drvdata(dev);
	struct qmp_mbox *mbox;

	list_for_each_entry(mbox, &mdev->mboxes, list) {
		mbox->local_state = LINK_DISCONNECTED;
		init_completion(&mbox->link_complete);
		init_completion(&mbox->ch_complete);
		mbox->tx_sent = false;
		/*
		 * set suspend flag to indicate self channel open is required
		 * after restore operation
		 */
		mbox->suspend_flag = true;
		/* Release rx packet buffer */
		kfree(mbox->rx_pkt.data);
		mbox->rx_pkt.data = NULL;
		if (mdev->early_boot)
			__qmp_rx_worker(mbox);
	}

	return 0;
}

static const struct dev_pm_ops qmp_mbox_pm_ops = {
	.freeze_late = qmp_mbox_freeze,
	.restore_early = qmp_mbox_restore,
};

static const struct of_device_id qmp_mbox_dt_match[] = {
	{ .compatible = "qcom,qmp-mbox" },
	{},
};

static struct platform_driver qmp_mbox_driver = {
	.driver = {
		.name = "qmp_mbox",
		.of_match_table = qmp_mbox_dt_match,
		.pm = &qmp_mbox_pm_ops,
	},
	.probe = qmp_mbox_probe,
	.remove = qmp_mbox_remove,
};
module_platform_driver(qmp_mbox_driver);

MODULE_DESCRIPTION("MSM QTI Mailbox Protocol");
MODULE_LICENSE("GPL v2");
