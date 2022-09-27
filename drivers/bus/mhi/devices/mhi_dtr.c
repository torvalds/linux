// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mhi.h>
#include <linux/mhi_misc.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/termios.h>
#include <linux/wait.h>

struct dtr_ctrl_msg {
	u32 preamble;
	u32 msg_id;
	u32 dest_id;
	u32 size;
	u32 msg;
} __packed;

static struct dtr_info {
	struct completion completion;
	struct mhi_device *mhi_dev;
	void *ipc_log;
} *dtr_info;

static enum MHI_DEBUG_LEVEL dtr_log_level = MHI_MSG_LVL_INFO;

#define MHI_DTR_IPC_LOG_PAGES (5)
#define DTR_LOG(fmt, ...) do { \
		dev_dbg(dev, "[I][%s] " fmt, __func__, ##__VA_ARGS__); \
		if (dtr_info->ipc_log && dtr_log_level <= MHI_MSG_LVL_INFO) \
			ipc_log_string(dtr_info->ipc_log, "[I][%s] " fmt, \
				       __func__, ##__VA_ARGS__); \
	} while (0)

#define DTR_ERR(fmt, ...) do { \
		dev_err(dev, "[E][%s] " fmt, __func__, ##__VA_ARGS__); \
		if (dtr_info->ipc_log && dtr_log_level <= MHI_MSG_LVL_ERROR) \
			ipc_log_string(dtr_info->ipc_log, "[E][%s] " fmt, \
				       __func__, ##__VA_ARGS__); \
	} while (0)

#define CTRL_MAGIC (0x4C525443)
#define CTRL_MSG_DTR BIT(0)
#define CTRL_MSG_RTS BIT(1)
#define CTRL_MSG_DCD BIT(0)
#define CTRL_MSG_DSR BIT(1)
#define CTRL_MSG_RI BIT(3)
#define CTRL_HOST_STATE (0x10)
#define CTRL_DEVICE_STATE (0x11)
#define CTRL_GET_CHID(dtr) ((dtr)->dest_id & 0xFF)

static int mhi_dtr_tiocmset(struct mhi_controller *mhi_cntrl,
			    struct mhi_device *mhi_dev,
			    u32 tiocm)
{
	struct device *dev = &mhi_dev->dev;
	struct dtr_ctrl_msg *dtr_msg = NULL;
	/* protects state changes for MHI device termios states */
	spinlock_t *res_lock = &mhi_dev->dev.devres_lock;
	u32 cur_tiocm;
	int ret = 0;

	cur_tiocm = mhi_dev->tiocm & ~(TIOCM_CD | TIOCM_DSR | TIOCM_RI);

	tiocm &= (TIOCM_DTR | TIOCM_RTS);

	/* state did not change */
	if (cur_tiocm == tiocm)
		return 0;

	dtr_msg = kzalloc(sizeof(*dtr_msg), GFP_KERNEL);
	if (!dtr_msg) {
		ret = -ENOMEM;
		goto tiocm_exit;
	}

	dtr_msg->preamble = CTRL_MAGIC;
	dtr_msg->msg_id = CTRL_HOST_STATE;
	dtr_msg->dest_id = mhi_dev->ul_chan_id;
	dtr_msg->size = sizeof(u32);
	if (tiocm & TIOCM_DTR)
		dtr_msg->msg |= CTRL_MSG_DTR;
	if (tiocm & TIOCM_RTS)
		dtr_msg->msg |= CTRL_MSG_RTS;

	reinit_completion(&dtr_info->completion);
	ret = mhi_queue_buf(dtr_info->mhi_dev, DMA_TO_DEVICE, dtr_msg,
			    sizeof(*dtr_msg), MHI_EOT);
	if (ret)
		goto tiocm_exit;

	ret = wait_for_completion_timeout(&dtr_info->completion,
				msecs_to_jiffies(mhi_cntrl->timeout_ms));
	if (!ret) {
		DTR_ERR("Failed to receive transfer callback\n");
		ret = -EIO;
		goto tiocm_exit;
	}

	DTR_LOG("DTR TIOCMSET update done for %s\n", mhi_dev->name);
	ret = 0;
	spin_lock_irq(res_lock);
	mhi_dev->tiocm &= ~(TIOCM_DTR | TIOCM_RTS);
	mhi_dev->tiocm |= tiocm;
	spin_unlock_irq(res_lock);

tiocm_exit:
	kfree(dtr_msg);

	return ret;
}

long mhi_device_ioctl(struct mhi_device *mhi_dev, unsigned int cmd,
		      unsigned long arg)
{
	struct device *dev = &mhi_dev->dev;
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	int ret;

	/* ioctl not supported by this controller */
	if (!dtr_info->mhi_dev) {
		DTR_ERR("%s request denied. DTR channels not running\n",
			mhi_dev->name);
		return -EIO;
	}

	switch (cmd) {
	case TIOCMGET:
		return mhi_dev->tiocm;
	case TIOCMSET:
	{
		u32 tiocm;

		ret = get_user(tiocm, (u32 __user *)arg);
		if (ret)
			return ret;

		return mhi_dtr_tiocmset(mhi_cntrl, mhi_dev, tiocm);
	}
	default:
		break;
	}

	return -ENOIOCTLCMD;
}
EXPORT_SYMBOL(mhi_device_ioctl);

static void mhi_dtr_dl_xfer_cb(struct mhi_device *mhi_dev,
			       struct mhi_result *mhi_result)
{
	struct device *dev = &mhi_dev->dev;
	struct mhi_controller *mhi_cntrl = mhi_dev->mhi_cntrl;
	struct dtr_ctrl_msg *dtr_msg = mhi_result->buf_addr;
	/* protects state changes for MHI device termios states */
	spinlock_t *res_lock;

	if (mhi_result->bytes_xferd != sizeof(*dtr_msg)) {
		DTR_ERR("Unexpected length %zu received\n",
			mhi_result->bytes_xferd);
		return;
	}

	DTR_LOG("preamble: 0x%x msg_id: %u dest_id: %u msg: 0x%x\n",
		dtr_msg->preamble, dtr_msg->msg_id, dtr_msg->dest_id,
		dtr_msg->msg);

	mhi_dev = mhi_get_device_for_channel(mhi_cntrl, CTRL_GET_CHID(dtr_msg));
	if (!mhi_dev)
		return;

	res_lock = &mhi_dev->dev.devres_lock;
	spin_lock_irq(res_lock);
	mhi_dev->tiocm &= ~(TIOCM_CD | TIOCM_DSR | TIOCM_RI);

	if (dtr_msg->msg & CTRL_MSG_DCD)
		mhi_dev->tiocm |= TIOCM_CD;

	if (dtr_msg->msg & CTRL_MSG_DSR)
		mhi_dev->tiocm |= TIOCM_DSR;

	if (dtr_msg->msg & CTRL_MSG_RI)
		mhi_dev->tiocm |= TIOCM_RI;
	spin_unlock_irq(res_lock);

	/* Notify the update */
	mhi_notify(mhi_dev, MHI_CB_DTR_SIGNAL);
}

static void mhi_dtr_ul_xfer_cb(struct mhi_device *mhi_dev,
			       struct mhi_result *mhi_result)
{
	struct device *dev = &mhi_dev->dev;

	DTR_LOG("Received with status: %d\n", mhi_result->transaction_status);
	if (!mhi_result->transaction_status)
		complete(&dtr_info->completion);
}

static void mhi_dtr_status_cb(struct mhi_device *mhi_dev, enum mhi_callback cb)
{
	struct device *dev = &mhi_dev->dev;
	int ret;

	if (cb != MHI_CB_DTR_START_CHANNELS)
		return;

	ret = mhi_prepare_for_transfer(mhi_dev);
	if (!ret)
		dtr_info->mhi_dev = mhi_dev;

	DTR_LOG("DTR channels start attempt returns: %d\n", ret);
}

static void mhi_dtr_remove(struct mhi_device *mhi_dev)
{
	dtr_info->mhi_dev = NULL;
}

static int mhi_dtr_probe(struct mhi_device *mhi_dev,
			 const struct mhi_device_id *id)
{
	struct device *dev = &mhi_dev->dev;

	dtr_info->ipc_log = ipc_log_context_create(MHI_DTR_IPC_LOG_PAGES,
						   dev_name(&mhi_dev->dev), 0);

	DTR_LOG("Probe complete\n");

	return 0;
}

static const struct mhi_device_id mhi_dtr_table[] = {
	{ .chan = "IP_CTRL" },
	{},
};

static struct mhi_driver mhi_dtr_driver = {
	.id_table = mhi_dtr_table,
	.remove = mhi_dtr_remove,
	.probe = mhi_dtr_probe,
	.ul_xfer_cb = mhi_dtr_ul_xfer_cb,
	.dl_xfer_cb = mhi_dtr_dl_xfer_cb,
	.status_cb = mhi_dtr_status_cb,
	.driver = {
		.name = "MHI_DTR",
		.owner = THIS_MODULE,
	}
};

static int __init mhi_dtr_init(void)
{
	dtr_info = kzalloc(sizeof(*dtr_info), GFP_KERNEL);
	if (!dtr_info)
		return -ENOMEM;

	init_completion(&dtr_info->completion);

	return mhi_driver_register(&mhi_dtr_driver);
}
module_init(mhi_dtr_init);

static void __exit mhi_dtr_exit(void)
{
	mhi_driver_unregister(&mhi_dtr_driver);
	kfree(dtr_info);
}
module_exit(mhi_dtr_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("MHI_DTR");
MODULE_DESCRIPTION("MHI DTR Driver");
