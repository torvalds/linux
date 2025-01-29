// SPDX-License-Identifier: GPL-2.0-only
//Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/ipc_logging.h>
#include <linux/dma-mapping.h>
#include <uapi/linux/mhi.h>
#include "mhi.h"

#define MHI_SOFTWARE_CLIENT_START	0
#define MHI_SOFTWARE_CLIENT_LIMIT	(MHI_MAX_SOFTWARE_CHANNELS/2)
#define MHI_UCI_IPC_LOG_PAGES		(100)

/* Max number of MHI read/write request structs (used in async transfers) */
#define MHI_UCI_NUM_REQ_DEFAULT		10
#define MAX_NR_TRBS_PER_CHAN		9
#define MHI_QTI_IFACE_ID		4
#define MHI_ADPL_IFACE_ID		5
#define MHI_CV2X_IFACE_ID		6
#define DEVICE_NAME			"mhi"
#define MAX_DEVICE_NAME_SIZE		80

#define MHI_UCI_ASYNC_READ_TIMEOUT	msecs_to_jiffies(100)
#define MHI_UCI_ASYNC_WRITE_TIMEOUT	msecs_to_jiffies(100)
#define MHI_UCI_AT_CTRL_READ_TIMEOUT	msecs_to_jiffies(1000)
#define MHI_UCI_WRITE_REQ_AVAIL_TIMEOUT msecs_to_jiffies(1000)

#define MHI_UCI_RELEASE_TIMEOUT_MIN	5000
#define MHI_UCI_RELEASE_TIMEOUT_MAX	5100
#define MHI_UCI_RELEASE_TIMEOUT_COUNT	30

#define MHI_UCI_IS_CHAN_DIR_IN(n) ((n % 2) ? true : false)

enum uci_dbg_level {
	UCI_DBG_VERBOSE = 0x0,
	UCI_DBG_INFO = 0x1,
	UCI_DBG_DBG = 0x2,
	UCI_DBG_WARNING = 0x3,
	UCI_DBG_ERROR = 0x4,
	UCI_DBG_CRITICAL = 0x5,
	UCI_DBG_reserved = 0x80000000
};

static enum uci_dbg_level mhi_uci_msg_lvl = UCI_DBG_ERROR;
static enum uci_dbg_level mhi_uci_ipc_log_lvl = UCI_DBG_INFO;
static void *mhi_uci_ipc_log;


enum mhi_chan_dir {
	MHI_DIR_INVALID = 0x0,
	MHI_DIR_OUT = 0x1,
	MHI_DIR_IN = 0x2,
	MHI_DIR__reserved = 0x80000000
};

struct chan_attr {
	/* SW maintained channel id */
	enum mhi_client_channel chan_id;
	/* maximum buffer size for this channel */
	size_t max_packet_size;
	/* number of buffers supported in this channel */
	u32 nr_trbs;
	/* direction of the channel, see enum mhi_chan_dir */
	enum mhi_chan_dir dir;
	/* Optional mhi channel state change callback func pointer */
	void (*chan_state_cb)(struct mhi_dev_client_cb_data *cb_data);
	/* Name of char device */
	char *device_name;
	/* Client-specific TRE handler */
	void (*tre_notif_cb)(struct mhi_dev_client_cb_reason *reason);
	/* Write completion - false if not needed */
	bool wr_cmpl;
	/* Uevent broadcast of channel state */
	bool state_bcast;
	/* Skip node creation if not needed */
	bool skip_node;
	/* Number of write request structs to allocate */
	u32 num_reqs;
};

static void mhi_uci_generic_client_cb(struct mhi_dev_client_cb_data *cb_data);
static void mhi_uci_at_ctrl_client_cb(struct mhi_dev_client_cb_data *cb_data);
static void mhi_uci_at_ctrl_tre_cb(struct mhi_dev_client_cb_reason *reason);

/* MHI channel attributes table
 * Skip node creation for IPCR channels but still allow uevent broadcast to
 * QRTR client by setting the broadcast flag
 */
static const struct chan_attr mhi_chan_attr_table[] = {
	{
		MHI_CLIENT_LOOPBACK_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		mhi_uci_generic_client_cb,
		NULL
	},
	{
		MHI_CLIENT_LOOPBACK_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		mhi_uci_generic_client_cb,
		NULL
	},
	{
		MHI_CLIENT_SAHARA_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		NULL,
		NULL
	},
	{
		MHI_CLIENT_SAHARA_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		NULL,
		NULL
	},
	{
		MHI_CLIENT_EFS_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		NULL,
		NULL
	},
	{
		MHI_CLIENT_EFS_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		NULL,
		NULL
	},
	{
		MHI_CLIENT_MBIM_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		NULL,
		NULL,
		NULL,
		false,
		true
	},
	{
		MHI_CLIENT_MBIM_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		NULL,
		NULL,
		NULL,
		false,
		true
	},
	{
		MHI_CLIENT_DIAG_OUT,
		TRB_MAX_DATA_SIZE_16K,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		NULL,
		NULL,
		NULL,
		false,
		true
	},
	{
		MHI_CLIENT_DIAG_IN,
		TRB_MAX_DATA_SIZE_16K,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		NULL,
		NULL,
		NULL,
		false,
		true,
		false,
		50
	},
	{
		MHI_CLIENT_QMI_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		NULL,
		NULL,
		NULL,
		false,
		true
	},
	{
		MHI_CLIENT_QMI_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		NULL,
		NULL,
		NULL,
		false,
		true
	},
	{
		MHI_CLIENT_IP_CTRL_0_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		NULL,
		NULL
	},
	{
		MHI_CLIENT_IP_CTRL_0_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		NULL,
		NULL
	},
	{
		MHI_CLIENT_IP_CTRL_1_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		mhi_uci_at_ctrl_client_cb,
		NULL,
		mhi_uci_at_ctrl_tre_cb
	},
	{
		MHI_CLIENT_IP_CTRL_1_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		mhi_uci_at_ctrl_client_cb,
		NULL,
		NULL,
		true
	},
	{
		MHI_CLIENT_IPCR_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		NULL,
		NULL,
		NULL,
		false,
		true
	},
	{
		MHI_CLIENT_IPCR_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		NULL,
		NULL,
		NULL,
		false,
		true,
		true
	},
	{
		MHI_CLIENT_DUN_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		mhi_uci_generic_client_cb,
		NULL,
		NULL,
		false,
		true
	},
	{
		MHI_CLIENT_DUN_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		mhi_uci_generic_client_cb,
		NULL,
		NULL,
		false,
		true,
		true,
		50
	},
	{
		MHI_CLIENT_ADB_OUT,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_OUT,
		mhi_uci_generic_client_cb,
		NULL,
		NULL,
		false,
		true,
		50
	},
	{
		MHI_CLIENT_ADB_IN,
		TRB_MAX_DATA_SIZE,
		MAX_NR_TRBS_PER_CHAN,
		MHI_DIR_IN,
		mhi_uci_generic_client_cb,
		"android_adb",
		NULL,
		false,
		true,
		50
	},
};

/* Defines for AT messages */
#define MHI_UCI_CTRL_MSG_MAGIC		(0x4354524C)
#define MHI_UCI_CTRL_MSG_DTR		BIT(0)
#define MHI_UCI_CTRL_MSG_RTS		BIT(1)
#define MHI_UCI_CTRL_MSG_DCD		BIT(0)
#define MHI_UCI_CTRL_MSG_DSR		BIT(1)
#define MHI_UCI_CTRL_MSG_RI		BIT(3)

#define MHI_UCI_CTRL_MSGID_SET_CTRL_LINE	0x10
#define MHI_UCI_CTRL_MSGID_SERIAL_STATE		0x11
#define MHI_UCI_TIOCM_GET			TIOCMGET
#define MHI_UCI_TIOCM_SET			TIOCMSET

/* AT message format */
struct __packed mhi_uci_ctrl_msg {
	u32 preamble;
	u32 msg_id;
	u32 dest_id;
	u32 size;
	u32 msg;
};

struct uci_ctrl {
	wait_queue_head_t	ctrl_wq;
	struct mhi_uci_ctxt_t	*uci_ctxt;
	atomic_t		ctrl_data_update;
};

struct uci_client {
	u32 client_index;
	/* write channel - always odd*/
	u32 out_chan;
	/* read channel - always even */
	u32 in_chan;
	struct mhi_dev_client *out_handle;
	struct mhi_dev_client *in_handle;
	const struct chan_attr *in_chan_attr;
	const struct chan_attr *out_chan_attr;
	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;
	atomic_t read_data_ready;
	struct device *dev;
	atomic_t ref_count;
	int mhi_status;
	void *pkt_loc;
	size_t pkt_size;
	struct mhi_dev_iov *in_buf_list;
	atomic_t write_data_ready;
	atomic_t mhi_chans_open;
	struct mhi_uci_ctxt_t *uci_ctxt;
	struct mutex in_chan_lock;
	struct mutex out_chan_lock;
	struct mutex client_lock;
	spinlock_t req_lock;
	unsigned int f_flags;
	/* Pointer to dynamically allocated mhi_req structs */
	struct mhi_req *reqs;
	/* Pointer to available (free) reqs */
	struct list_head req_list;
	/* Pointer to in-use reqs */
	struct list_head in_use_list;
	struct completion read_done;
	struct completion at_ctrl_read_done;
	struct completion *write_done;
	int (*send)(struct uci_client *h, void *data_loc, u32 size);
	int (*read)(struct uci_client *h, int *bytes);
	unsigned int tiocm;
	unsigned int at_ctrl_mask;
	int tre_len;
};

struct mhi_uci_ctxt_t {
	struct uci_client client_handles[MHI_SOFTWARE_CLIENT_LIMIT];
	struct uci_ctrl ctrl_handle;
	void (*event_notifier)(struct mhi_dev_client_cb_reason *cb);
	dev_t start_ctrl_nr;
	struct cdev cdev[MHI_MAX_SOFTWARE_CHANNELS];
	dev_t ctrl_nr;
	struct cdev *cdev_ctrl;
	struct device *dev;
	struct class *mhi_uci_class;
	atomic_t mhi_disabled;
	atomic_t mhi_enable_notif_wq_active;
	struct workqueue_struct *at_ctrl_wq;
	struct work_struct at_ctrl_work;
};

#define CHAN_TO_CLIENT(_CHAN_NR) (_CHAN_NR / 2)
#define CLIENT_TO_CHAN(_CLIENT_NR) (_CLIENT_NR * 2)

#define uci_log_ratelimit(_msg_lvl, _msg, ...) do { \
	if (_msg_lvl >= mhi_uci_msg_lvl) { \
		pr_err_ratelimited("[%s] "_msg, __func__, ##__VA_ARGS__); \
	} \
	if (mhi_uci_ipc_log && (_msg_lvl >= mhi_uci_ipc_log_lvl)) { \
		ipc_log_string(mhi_uci_ipc_log,                     \
			"[%s] " _msg, __func__, ##__VA_ARGS__);     \
	} \
} while (0)

#define uci_log(_msg_lvl, _msg, ...) do { \
	if (_msg_lvl >= mhi_uci_msg_lvl) { \
		pr_err("[%s] "_msg, __func__, ##__VA_ARGS__); \
	} \
	if (mhi_uci_ipc_log && (_msg_lvl >= mhi_uci_ipc_log_lvl)) { \
		ipc_log_string(mhi_uci_ipc_log,                     \
			"[%s] " _msg, __func__, ##__VA_ARGS__);     \
	} \
} while (0)


static ssize_t mhi_uci_client_read(struct file *file, char __user *buf,
		size_t count, loff_t *offp);
static ssize_t mhi_uci_ctrl_client_read(struct file *file, char __user *buf,
		size_t count, loff_t *offp);
static ssize_t mhi_uci_client_write(struct file *file,
		const char __user *buf, size_t count, loff_t *offp);
static ssize_t mhi_uci_client_write_iter(struct kiocb *iocb,
					 struct iov_iter *buf);
static int mhi_uci_client_open(struct inode *mhi_inode, struct file*);
static int mhi_uci_ctrl_open(struct inode *mhi_inode, struct file*);
static int mhi_uci_client_release(struct inode *mhi_inode,
		struct file *file_handle);
static unsigned int mhi_uci_client_poll(struct file *file, poll_table *wait);
static unsigned int mhi_uci_ctrl_poll(struct file *file, poll_table *wait);
static struct mhi_uci_ctxt_t uci_ctxt;

static bool mhi_uci_are_channels_connected(struct uci_client *uci_client)
{
	uint32_t info_ch_in, info_ch_out;
	int rc;

	/*
	 * Check channel states and return true only if channel
	 * information is available and in connected state.
	 * For all other failure conditions return false.
	 */
	rc = mhi_ctrl_state_info(uci_client->in_chan, &info_ch_in);
	if (rc) {
		uci_log(UCI_DBG_DBG,
			"ch_id:%d is not available with %d\n",
			uci_client->out_chan, rc);
		return false;
	}

	rc = mhi_ctrl_state_info(uci_client->out_chan, &info_ch_out);
	if (rc) {
		uci_log(UCI_DBG_DBG,
			"ch_id:%d is not available with %d\n",
			uci_client->out_chan, rc);
		return false;
	}

	if ((info_ch_in != MHI_STATE_CONNECTED) ||
		(info_ch_out != MHI_STATE_CONNECTED)) {
		uci_log(UCI_DBG_DBG,
			"ch_id:%d or %d are not connected\n",
			uci_client->in_chan, uci_client->out_chan);
		return false;
	}

	return true;
}

static int mhi_init_read_chan(struct uci_client *client_handle,
		enum mhi_client_channel chan)
{
	int rc = 0;
	u32 i, j;
	const struct chan_attr *in_chan_attr;
	size_t buf_size;
	void *data_loc;

	if (client_handle == NULL) {
		uci_log(UCI_DBG_ERROR, "Bad Input data, quitting\n");
		return -EINVAL;
	}
	if (chan >= MHI_MAX_SOFTWARE_CHANNELS) {
		uci_log(UCI_DBG_ERROR, "Incorrect ch_id:%d\n", chan);
		return -EINVAL;
	}

	in_chan_attr = client_handle->in_chan_attr;
	if (!in_chan_attr) {
		uci_log(UCI_DBG_ERROR, "Null channel attributes for ch_id:%d\n",
				client_handle->in_chan);
		return -EINVAL;
	}

	/* Init the completion event for read */
	init_completion(&client_handle->read_done);

	buf_size = in_chan_attr->max_packet_size;
	for (i = 0; i < (in_chan_attr->nr_trbs); i++) {
		data_loc = kmalloc(buf_size, GFP_KERNEL);
		if (!data_loc) {
			rc = -ENOMEM;
			goto free_memory;
		}
		client_handle->in_buf_list[i].addr = data_loc;
		client_handle->in_buf_list[i].buf_size = buf_size;
	}

	return rc;

free_memory:
	for (j = 0; j < i; j++)
		kfree(client_handle->in_buf_list[j].addr);

	return rc;
}

static struct mhi_req *mhi_uci_get_req(struct uci_client *uci_handle)
{
	struct mhi_req *req;
	unsigned long flags;

	spin_lock_irqsave(&uci_handle->req_lock, flags);
	if (list_empty(&uci_handle->req_list)) {
		uci_log(UCI_DBG_ERROR, "Request pool empty for ch_id:%d, %d\n",
			uci_handle->in_chan, uci_handle->out_chan);
		spin_unlock_irqrestore(&uci_handle->req_lock, flags);
		return NULL;
	}
	/* Remove from free list and add to in-use list */
	req = container_of(uci_handle->req_list.next,
			struct mhi_req, list);
	list_del_init(&req->list);
	/*
	 * If req is marked stale and if it was used for the write channel
	 * to host, free the previously allocated input buffer before the
	 * req is re-used
	 */
	if (req->is_stale && req->buf && MHI_UCI_IS_CHAN_DIR_IN(req->chan)) {
		uci_log(UCI_DBG_VERBOSE, "Freeing write buf for ch_id:%d\n",
			req->chan);
		kfree(req->buf);
	}
	req->is_stale = false;
	uci_log(UCI_DBG_VERBOSE, "Adding req to in-use list\n");
	list_add_tail(&req->list, &uci_handle->in_use_list);
	spin_unlock_irqrestore(&uci_handle->req_lock, flags);

	return req;
}

static int mhi_uci_put_req(struct uci_client *uci_handle, struct mhi_req *req)
{
	unsigned long flags;

	spin_lock_irqsave(&uci_handle->req_lock, flags);
	if (req->is_stale) {
		uci_log(UCI_DBG_VERBOSE,
			"Got stale completion for ch_id:%d, ignoring\n",
			req->chan);
		spin_unlock_irqrestore(&uci_handle->req_lock, flags);
		return -EINVAL;
	}

	/* Remove from in-use list and add back to free list */
	list_del_init(&req->list);
	list_add_tail(&req->list, &uci_handle->req_list);
	spin_unlock_irqrestore(&uci_handle->req_lock, flags);

	return 0;
}

static void mhi_uci_write_completion_cb(void *req)
{
	struct mhi_req *ureq = req;
	struct uci_client *uci_handle = (struct uci_client *)ureq->context;

	kfree(ureq->buf);
	ureq->buf = NULL;

	/*
	 * If this is a delayed write completion, just clear
	 * the stale flag and return. The ureq was added to
	 * the free list when client called release function.
	 */
	if (mhi_uci_put_req(uci_handle, ureq))
		return;

	if (uci_handle->write_done)
		complete(uci_handle->write_done);

	/* Write queue may be waiting for write request structs */
	wake_up(&uci_handle->write_wq);
}

static void mhi_uci_read_completion_cb(void *req)
{
	struct mhi_req *ureq = req;
	struct uci_client *uci_handle;

	uci_handle = (struct uci_client *)ureq->context;

	uci_handle->pkt_loc = (void *)ureq->buf;
	uci_handle->pkt_size = ureq->transfer_len;

	 /*
	  * If this is a delayed read completion, just clear
	  * the stale flag and return. The ureq was added to
	  * the free list when client called release function.
	  */
	if (mhi_uci_put_req(uci_handle, ureq))
		return;

	complete(&uci_handle->read_done);
}

static int mhi_uci_send_sync(struct uci_client *uci_handle,
			void *data_loc, u32 size)
{
	struct mhi_req ureq;
	int ret_val;

	uci_log(UCI_DBG_DBG,
		"Sync write for ch_id:%d size %d\n",
		uci_handle->out_chan, size);

	if (size > TRB_MAX_DATA_SIZE) {
		uci_log(UCI_DBG_ERROR,
				"Too big write size: %u, max supported size is %d\n",
				size, TRB_MAX_DATA_SIZE);
			return -EFBIG;
	}

	ureq.client = uci_handle->out_handle;
	ureq.buf = data_loc;
	ureq.len = size;
	ureq.chan = uci_handle->out_chan;
	ureq.mode = DMA_SYNC;

	ret_val = mhi_dev_write_channel(&ureq);

	if (ret_val == size)
		kfree(data_loc);
	return ret_val;
}

static int mhi_uci_send_async(struct uci_client *uci_handle,
			void *data_loc, u32 size)
{
	int bytes_to_write;
	struct mhi_req *ureq;

	uci_log(UCI_DBG_DBG,
		"Async write for ch_id:%d size %d\n",
		uci_handle->out_chan, size);

	ureq = mhi_uci_get_req(uci_handle);
	if (!ureq)
		return -EBUSY;

	ureq->client = uci_handle->out_handle;
	ureq->context = uci_handle;
	ureq->buf = data_loc;
	ureq->len = size;
	ureq->chan = uci_handle->out_chan;
	ureq->mode = DMA_ASYNC;
	ureq->client_cb = mhi_uci_write_completion_cb;
	ureq->snd_cmpl = 1;

	bytes_to_write = mhi_dev_write_channel(ureq);
	if (bytes_to_write != size)
		goto error_async_transfer;

	return bytes_to_write;

error_async_transfer:
	ureq->buf = NULL;
	mhi_uci_put_req(uci_handle, ureq);

	return bytes_to_write;
}

static int mhi_uci_send_packet(struct uci_client *uci_handle, void *data_loc,
				u32 size)
{
	int ret_val;

	mutex_lock(&uci_handle->out_chan_lock);
	do {
		ret_val = uci_handle->send(uci_handle, data_loc, size);
		if (!ret_val) {
			uci_log(UCI_DBG_VERBOSE,
				"No descriptors available, did we poll, ch_id:%d?\n",
				uci_handle->out_chan);
			mutex_unlock(&uci_handle->out_chan_lock);
			if (uci_handle->f_flags & (O_NONBLOCK | O_NDELAY))
				return -EAGAIN;
			ret_val = wait_event_interruptible(uci_handle->write_wq,
					!mhi_dev_channel_isempty(
					uci_handle->out_handle));
			if (-ERESTARTSYS == ret_val) {
				uci_log(UCI_DBG_WARNING,
					"Waitqueue cancelled by system\n");
				return ret_val;
			}
			mutex_lock(&uci_handle->out_chan_lock);
		} else if (ret_val == -EBUSY) {
			/*
			 * All write requests structs have been exhausted.
			 * Wait till pending writes complete or a timeout.
			 */
			uci_log(UCI_DBG_VERBOSE,
				"Write req list empty for ch_id:%d\n",
				uci_handle->out_chan);
			mutex_unlock(&uci_handle->out_chan_lock);
			if (uci_handle->f_flags & (O_NONBLOCK | O_NDELAY))
				return -EAGAIN;
			ret_val = wait_event_interruptible_timeout(
					uci_handle->write_wq,
					!list_empty(&uci_handle->req_list),
					MHI_UCI_WRITE_REQ_AVAIL_TIMEOUT);
			if (ret_val > 0) {
				/*
				 * Write request struct became available,
				 * retry the write.
				 */
				uci_log(UCI_DBG_VERBOSE,
				"Write req struct available for ch_id:%d\n",
					uci_handle->out_chan);
				mutex_lock(&uci_handle->out_chan_lock);
				ret_val = 0;
				continue;
			} else if (!ret_val) {
				uci_log(UCI_DBG_ERROR,
				"Timed out waiting for write req, ch_id:%d\n",
					uci_handle->out_chan);
				return -EIO;
			} else if (-ERESTARTSYS == ret_val) {
				uci_log(UCI_DBG_WARNING,
					"Waitqueue cancelled by system\n");
				return ret_val;
			}
		} else if (ret_val < 0) {
			uci_log(UCI_DBG_ERROR,
				"Err sending data: ch_id:%d, buf %pK, size %d\n",
				uci_handle->out_chan, data_loc, size);
			ret_val = -EIO;
			break;
		}
	} while (!ret_val);
	mutex_unlock(&uci_handle->out_chan_lock);

	return ret_val;
}

static unsigned int mhi_uci_ctrl_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;
	struct uci_ctrl *uci_ctrl_handle;

	uci_ctrl_handle = file->private_data;

	if (!uci_ctrl_handle)
		return -ENODEV;

	poll_wait(file, &uci_ctrl_handle->ctrl_wq, wait);
	if (!atomic_read(&uci_ctxt.mhi_disabled) &&
		atomic_read(&uci_ctrl_handle->ctrl_data_update)) {
		uci_log(UCI_DBG_VERBOSE, "Client can read ctrl_state");
		mask |= POLLIN | POLLRDNORM;
	}

	uci_log(UCI_DBG_VERBOSE,
		"Client attempted to poll ctrl returning mask 0x%x\n",
		mask);

	return mask;
}

static unsigned int mhi_uci_client_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;
	struct uci_client *uci_handle;

	uci_handle = file->private_data;

	if (!uci_handle)
		return -ENODEV;

	mutex_lock(&uci_handle->client_lock);

	poll_wait(file, &uci_handle->read_wq, wait);
	poll_wait(file, &uci_handle->write_wq, wait);
	/*
	 * Check if the channels on which the clients are trying
	 * to poll are in connected state and return with the
	 * appropriate mask if channels are disconnected.
	 */
	if (!atomic_read(&uci_handle->mhi_chans_open) ||
		!mhi_uci_are_channels_connected(uci_handle)) {
		mask = POLLHUP;
		mutex_unlock(&uci_handle->client_lock);
		return mask;
	}
	mask = uci_handle->at_ctrl_mask;
	if (!atomic_read(&uci_ctxt.mhi_disabled) &&
		!mhi_dev_channel_isempty(uci_handle->in_handle)) {
		uci_log(UCI_DBG_VERBOSE,
		"Client can read ch_id:%d\n", uci_handle->in_chan);
		mask |= POLLIN | POLLRDNORM;
	}
	if (!atomic_read(&uci_ctxt.mhi_disabled) &&
		!mhi_dev_channel_isempty(uci_handle->out_handle)) {
		uci_log(UCI_DBG_VERBOSE,
		"Client can write ch_id:%d\n", uci_handle->out_chan);
		mask |= POLLOUT | POLLWRNORM;
	}

	uci_log(UCI_DBG_VERBOSE,
		"Client attempted to poll ch_id:%d, returning mask 0x%x\n",
		uci_handle->in_chan, mask);
	mutex_unlock(&uci_handle->client_lock);

	return mask;
}

static int mhi_uci_alloc_reqs(struct uci_client *client)
{
	int i;
	u32 num_reqs;

	if (client->reqs) {
		uci_log(UCI_DBG_VERBOSE, "Reqs already allocated\n");
		return 0;
	}

	num_reqs = client->in_chan_attr->num_reqs;
	if (!num_reqs)
		num_reqs = MHI_UCI_NUM_REQ_DEFAULT;

	client->reqs = kcalloc(num_reqs,
				sizeof(struct mhi_req),
				GFP_KERNEL);
	if (!client->reqs) {
		uci_log(UCI_DBG_ERROR, "Reqs alloc failed\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&client->req_list);
	INIT_LIST_HEAD(&client->in_use_list);
	for (i = 0; i < num_reqs; ++i)
		list_add_tail(&client->reqs[i].list, &client->req_list);

	uci_log(UCI_DBG_INFO,
		"Allocated %d write reqs for ch_id:%d\n",
		num_reqs, client->out_chan);
	return 0;
}

static int mhi_uci_read_async(struct uci_client *uci_handle, int *bytes_avail)
{
	int ret_val = 0;
	unsigned long compl_ret;
	struct mhi_req *ureq;
	struct mhi_dev_client *client_handle;

	uci_log(UCI_DBG_DBG,
		"Async read for ch_id:%d\n", uci_handle->in_chan);

	ureq = mhi_uci_get_req(uci_handle);
	if (!ureq) {
		uci_log(UCI_DBG_ERROR,
			"Out of reqs for ch_id:%d\n", uci_handle->in_chan);
		return -EBUSY;
	}

	client_handle = uci_handle->in_handle;
	ureq->chan = uci_handle->in_chan;
	ureq->client = client_handle;
	ureq->buf = uci_handle->in_buf_list[0].addr;
	ureq->len = uci_handle->in_buf_list[0].buf_size;

	ureq->mode = DMA_ASYNC;
	ureq->client_cb = mhi_uci_read_completion_cb;
	ureq->snd_cmpl = 1;
	ureq->context = uci_handle;

	reinit_completion(&uci_handle->read_done);

	*bytes_avail = mhi_dev_read_channel(ureq);
	if (*bytes_avail < 0) {
		uci_log_ratelimit(UCI_DBG_ERROR, "Failed to read channel ret %dlu\n",
			*bytes_avail);
		if (uci_handle->in_chan == MHI_CLIENT_ADB_OUT) {
			uci_log(UCI_DBG_ERROR,
				"Read failed CH 36 free req from list\n");
			uci_handle->pkt_loc = NULL;
			uci_handle->pkt_size = 0;
			mhi_uci_put_req(uci_handle, ureq);
			*bytes_avail = 0;
			return ret_val;
		}
		mhi_uci_put_req(uci_handle, ureq);
		return -EIO;
	}
	uci_log(UCI_DBG_VERBOSE, "buf_size = 0x%lx bytes_read = 0x%x\n",
		ureq->len, *bytes_avail);
	if (*bytes_avail > 0) {
		uci_log(UCI_DBG_VERBOSE,
			"Waiting for async read completion!\n");
		compl_ret =
			wait_for_completion_interruptible_timeout(
			&uci_handle->read_done,
			MHI_UCI_ASYNC_READ_TIMEOUT);
		if (compl_ret == -ERESTARTSYS) {
			uci_log(UCI_DBG_ERROR, "Exit signal caught\n");
			return compl_ret;
		} else if (compl_ret == 0) {
			uci_log(UCI_DBG_ERROR, "Read timed out for ch_id:%d\n",
				uci_handle->in_chan);
			return -EIO;
		}
		uci_log(UCI_DBG_VERBOSE,
			"wk up Read completed on ch_id:%d\n", uci_handle->in_chan);
		uci_log(UCI_DBG_VERBOSE,
			"Got pkt of sz 0x%lx at adr %pK, ch_id:%d\n",
			uci_handle->pkt_size,
			uci_handle->pkt_loc, uci_handle->in_chan);
	} else {
		uci_handle->pkt_loc = NULL;
		uci_handle->pkt_size = 0;
		uci_log(UCI_DBG_VERBOSE,
			"No read data available, return req to free liat\n");
		mhi_uci_put_req(uci_handle, ureq);
	}

	return ret_val;
}

static int mhi_uci_read_sync(struct uci_client *uci_handle, int *bytes_avail)
{
	int ret_val = 0;
	struct mhi_req ureq;
	struct mhi_dev_client *client_handle;

	uci_log(UCI_DBG_DBG,
		"Sync read for ch_id:%d\n", uci_handle->in_chan);

	client_handle = uci_handle->in_handle;
	ureq.chan = uci_handle->in_chan;
	ureq.client = client_handle;
	ureq.buf = uci_handle->in_buf_list[0].addr;
	ureq.len = uci_handle->in_buf_list[0].buf_size;
	ureq.mode = DMA_SYNC;

	*bytes_avail = mhi_dev_read_channel(&ureq);
	uci_log(UCI_DBG_VERBOSE, "buf_size = 0x%lx bytes_read = 0x%x\n",
		ureq.len, *bytes_avail);

	if (*bytes_avail < 0) {
		uci_log(UCI_DBG_ERROR, "Failed to read channel ret %d\n",
			*bytes_avail);
		return -EIO;
	}

	if (*bytes_avail > 0) {
		uci_handle->pkt_loc = (void *)ureq.buf;
		uci_handle->pkt_size = ureq.transfer_len;

		uci_log(UCI_DBG_VERBOSE,
			"Got pkt of sz 0x%lx at adr %pK, ch_id:%d\n",
			uci_handle->pkt_size,
			ureq.buf, ureq.chan);
	} else {
		uci_handle->pkt_loc = NULL;
		uci_handle->pkt_size = 0;
	}

	return ret_val;
}

static int open_client_mhi_channels(struct uci_client *uci_client)
{
	int rc = 0;

	if (!mhi_uci_are_channels_connected(uci_client)) {
		uci_log(UCI_DBG_VERBOSE, "Channels are not connected\n");
		return -ENODEV;
	}

	uci_log(UCI_DBG_DBG,
			"Starting channels OUT ch_id:%d IN ch_id:%d\n",
			uci_client->out_chan,
			uci_client->in_chan);
	mutex_lock(&uci_client->out_chan_lock);
	mutex_lock(&uci_client->in_chan_lock);

	/* Allocate write requests for async operations */
	if (!(uci_client->f_flags & O_SYNC)) {
		rc = mhi_uci_alloc_reqs(uci_client);
		if (rc)
			goto handle_not_rdy_err;
		uci_client->send = mhi_uci_send_async;
		uci_client->read = mhi_uci_read_async;
	} else {
		uci_client->send = mhi_uci_send_sync;
		uci_client->read = mhi_uci_read_sync;
	}

	uci_log(UCI_DBG_DBG,
			"Initializing inbound ch_id:%d.\n",
			uci_client->in_chan);
	rc = mhi_init_read_chan(uci_client, uci_client->in_chan);
	if (rc < 0) {
		uci_log(UCI_DBG_ERROR,
			"Failed to init inbound 0x%x, ret 0x%x\n",
			uci_client->in_chan, rc);
		goto handle_not_rdy_err;
	}

	rc = mhi_dev_open_channel(uci_client->out_chan,
			&uci_client->out_handle,
			uci_ctxt.event_notifier);
	if (rc < 0)
		goto handle_not_rdy_err;

	rc = mhi_dev_open_channel(uci_client->in_chan,
			&uci_client->in_handle,
			uci_ctxt.event_notifier);
	if (rc < 0) {
		uci_log(UCI_DBG_ERROR,
			"Failed to open ch_id:%d, ret %d\n",
			uci_client->out_chan, rc);
		goto handle_in_err;
	}
	atomic_set(&uci_client->mhi_chans_open, 1);
	mutex_unlock(&uci_client->in_chan_lock);
	mutex_unlock(&uci_client->out_chan_lock);

	return 0;

handle_in_err:
	mhi_dev_close_channel(uci_client->out_handle);
handle_not_rdy_err:
	mutex_unlock(&uci_client->in_chan_lock);
	mutex_unlock(&uci_client->out_chan_lock);
	return rc;
}

static int mhi_uci_ctrl_open(struct inode *inode,
			struct file *file_handle)
{
	struct uci_ctrl *uci_ctrl_handle;

	uci_log(UCI_DBG_DBG, "Client opened ctrl file device node\n");

	uci_ctrl_handle = &uci_ctxt.ctrl_handle;
	if (!uci_ctrl_handle)
		return -EINVAL;

	file_handle->private_data = uci_ctrl_handle;

	return 0;
}

static int mhi_uci_client_open(struct inode *mhi_inode,
				struct file *file_handle)
{
	struct uci_client *uci_handle;
	int rc = 0;

	rc = iminor(mhi_inode);
	if (rc < MHI_SOFTWARE_CLIENT_LIMIT) {
		uci_handle =
			&uci_ctxt.client_handles[iminor(mhi_inode)];
	} else {
		uci_log(UCI_DBG_DBG,
		"Cannot open struct device node 0x%x\n", iminor(mhi_inode));
		return -EINVAL;
	}

	if (!uci_handle) {
		uci_log(UCI_DBG_DBG, "No memory, returning failure\n");
		return -ENOMEM;
	}

	mutex_lock(&uci_handle->client_lock);
	uci_log(UCI_DBG_DBG,
		"Client opened struct device node 0x%x, ref count 0x%x\n",
		iminor(mhi_inode), atomic_read(&uci_handle->ref_count));
	if (atomic_add_return(1, &uci_handle->ref_count) == 1) {
		uci_handle->uci_ctxt = &uci_ctxt;
		uci_handle->f_flags = file_handle->f_flags;
		if (!atomic_read(&uci_handle->mhi_chans_open)) {
			uci_log(UCI_DBG_INFO,
				"Opening channels client %d\n",
				iminor(mhi_inode));
			rc = open_client_mhi_channels(uci_handle);
			if (rc < 0) {
				uci_log(UCI_DBG_INFO,
					"Failed to open channels ret %d\n", rc);
				if (atomic_sub_return(1, &uci_handle->ref_count)
									== 0) {
					uci_log(UCI_DBG_INFO,
						"Closing failed channel\n");
				}
				mutex_unlock(&uci_handle->client_lock);
				return rc;
			}
		}
	}
	file_handle->private_data = uci_handle;
	mutex_unlock(&uci_handle->client_lock);

	return 0;

}

static int mhi_uci_client_release(struct inode *mhi_inode,
		struct file *file_handle)
{
	struct uci_client *uci_handle = file_handle->private_data;
	const struct chan_attr *in_chan_attr;
	int count = 0, i;
	struct mhi_req *ureq;
	unsigned long flags;

	if (!uci_handle)
		return -EINVAL;

	mutex_lock(&uci_handle->client_lock);
	in_chan_attr = uci_handle->in_chan_attr;
	if (!in_chan_attr) {
		uci_log(UCI_DBG_ERROR, "Null channel attributes for ch_id:%d\n",
				uci_handle->in_chan);
		mutex_unlock(&uci_handle->client_lock);
		return -EINVAL;
	}

	if (atomic_sub_return(1, &uci_handle->ref_count)) {
		uci_log(UCI_DBG_DBG, "Client close ch_id:%d, ref count 0x%x\n",
			iminor(mhi_inode),
			atomic_read(&uci_handle->ref_count));
		mutex_unlock(&uci_handle->client_lock);
		return 0;
	}

	uci_log(UCI_DBG_DBG,
			"Last client left, closing ch 0x%x\n",
			iminor(mhi_inode));

	do {
		if (mhi_dev_channel_has_pending_write(uci_handle->out_handle))
			usleep_range(MHI_UCI_RELEASE_TIMEOUT_MIN,
				MHI_UCI_RELEASE_TIMEOUT_MAX);
		else
			break;
	} while (++count < MHI_UCI_RELEASE_TIMEOUT_COUNT);

	if (count == MHI_UCI_RELEASE_TIMEOUT_COUNT) {
		uci_log(UCI_DBG_DBG, "ch_id:%d has pending writes\n",
			iminor(mhi_inode));
	}

	if (atomic_read(&uci_handle->mhi_chans_open)) {
		atomic_set(&uci_handle->mhi_chans_open, 0);
		mutex_lock(&uci_handle->out_chan_lock);
		mhi_dev_close_channel(uci_handle->out_handle);
		wake_up(&uci_handle->write_wq);
		mutex_unlock(&uci_handle->out_chan_lock);

		mutex_lock(&uci_handle->in_chan_lock);
		mhi_dev_close_channel(uci_handle->in_handle);
		wake_up(&uci_handle->read_wq);
		mutex_unlock(&uci_handle->in_chan_lock);
		/*
		 * Add back reqs for in-use list, if any, to free list.
		 * Mark the ureq stale to avoid returning stale data
		 * to client if the transfer completes later.
		 */
		count = 0;

		spin_lock_irqsave(&uci_handle->req_lock, flags);
		if (!(uci_handle->f_flags & O_SYNC)) {
			while (!(list_empty(&uci_handle->in_use_list))) {
				ureq = container_of(
					uci_handle->in_use_list.next,
					struct mhi_req, list);
				list_del_init(&ureq->list);
				ureq->is_stale = true;
				uci_log(UCI_DBG_VERBOSE,
					"Adding back req for ch_id:%d to free list\n",
					ureq->chan);
				list_add_tail(&ureq->list, &uci_handle->req_list);
				count++;
			}
		}
		spin_unlock_irqrestore(&uci_handle->req_lock, flags);
		if (count)
			uci_log(UCI_DBG_DBG,
				"Client %d closed with %d transfers pending\n",
				iminor(mhi_inode), count);
	}

	for (i = 0; i < (in_chan_attr->nr_trbs); i++) {
		kfree(uci_handle->in_buf_list[i].addr);
		uci_handle->in_buf_list[i].addr = NULL;
		uci_handle->in_buf_list[i].buf_size = 0;
	}

	atomic_set(&uci_handle->read_data_ready, 0);
	atomic_set(&uci_handle->write_data_ready, 0);
	file_handle->private_data = NULL;
	mutex_unlock(&uci_handle->client_lock);

	return 0;
}

static void  mhi_parse_state(char *buf, int *nbytes, uint32_t info)
{
	switch (info) {
	case MHI_STATE_CONNECTED:
		*nbytes = scnprintf(buf, MHI_CTRL_STATE,
			"CONNECTED");
		break;
	case MHI_STATE_DISCONNECTED:
		*nbytes = scnprintf(buf, MHI_CTRL_STATE,
			"DISCONNECTED");
		break;
	case MHI_STATE_CONFIGURED:
	default:
		*nbytes = scnprintf(buf, MHI_CTRL_STATE,
			"CONFIGURED");
		break;
	}
}

static int mhi_state_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	int rc, nbytes = 0;
	uint32_t info = 0, i;
	char buf[MHI_CTRL_STATE];
	const struct chan_attr *chan_attrib;

	rc = mhi_ctrl_state_info(MHI_DEV_UEVENT_CTRL, &info);
	if (rc) {
		uci_log(UCI_DBG_ERROR, "Failed to obtain MHI_STATE\n");
		return -EINVAL;
	}

	mhi_parse_state(buf, &nbytes, info);
	add_uevent_var(env, "MHI_STATE=%s", buf);

	for (i = 0; i < ARRAY_SIZE(mhi_chan_attr_table); i++) {
		chan_attrib = &mhi_chan_attr_table[i];
		if (chan_attrib->state_bcast) {
			uci_log(UCI_DBG_INFO, "Calling notify for ch_id:%d\n",
					chan_attrib->chan_id);
			rc = mhi_ctrl_state_info(chan_attrib->chan_id, &info);
			if (rc) {
				uci_log(UCI_DBG_ERROR,
					"Failed to obtain ch_id:%d state\n",
					chan_attrib->chan_id);
				return -EINVAL;
			}
			nbytes = 0;
			mhi_parse_state(buf, &nbytes, info);
			add_uevent_var(env, "MHI_CHANNEL_STATE_%d=%s",
					chan_attrib->chan_id, buf);
		}
	}

	return 0;
}

static ssize_t mhi_uci_ctrl_client_read(struct file *file,
		char __user *user_buf,
		size_t count, loff_t *offp)
{
	uint32_t rc = 0, info;
	int nbytes, size;
	char buf[MHI_CTRL_STATE];
	struct uci_ctrl *uci_ctrl_handle = NULL;

	if (!file || !user_buf || !count ||
		(count < MHI_CTRL_STATE) || !file->private_data)
		return -EINVAL;

	uci_ctrl_handle = file->private_data;
	rc = mhi_ctrl_state_info(MHI_CLIENT_QMI_OUT, &info);
	if (rc)
		return -EINVAL;

	switch (info) {
	case MHI_STATE_CONFIGURED:
		nbytes = scnprintf(buf, sizeof(buf),
			"MHI_STATE=CONFIGURED");
		break;
	case MHI_STATE_CONNECTED:
		nbytes = scnprintf(buf, sizeof(buf),
			"MHI_STATE=CONNECTED");
		break;
	case MHI_STATE_DISCONNECTED:
		nbytes = scnprintf(buf, sizeof(buf),
			"MHI_STATE=DISCONNECTED");
		break;
	default:
		uci_log(UCI_DBG_ERROR, "invalid info:%d\n", info);
		return -EINVAL;
	}


	size = simple_read_from_buffer(user_buf, count, offp, buf, nbytes);

	atomic_set(&uci_ctrl_handle->ctrl_data_update, 0);

	if (size == 0)
		*offp = 0;

	return size;
}

static int __mhi_uci_client_read(struct uci_client *uci_handle,
		int *bytes_avail)
{
	int ret_val = 0;

	while (!uci_handle->pkt_loc) {
		if (!mhi_uci_are_channels_connected(uci_handle)) {
			uci_log_ratelimit(UCI_DBG_ERROR, "Channels are not connected\n");
			return -ENODEV;
		}

		if (!uci_handle->pkt_loc &&
			!atomic_read(&uci_ctxt.mhi_disabled)) {
			ret_val = uci_handle->read(uci_handle, bytes_avail);
			if (ret_val)
				return ret_val;
		}
		if (*bytes_avail == 0) {

			/* If nothing was copied yet, wait for data */
			uci_log(UCI_DBG_VERBOSE,
				"No data read_data_ready %d, ch_id:%d\n",
				atomic_read(&uci_handle->read_data_ready),
				uci_handle->in_chan);
			if (uci_handle->f_flags & (O_NONBLOCK | O_NDELAY))
				return -EAGAIN;

			ret_val = wait_event_interruptible(uci_handle->read_wq,
				(!mhi_dev_channel_isempty(
					uci_handle->in_handle)));

			if (ret_val == -ERESTARTSYS) {
				uci_log(UCI_DBG_ERROR, "Exit signal caught\n");
				return ret_val;
			}

			uci_log(UCI_DBG_VERBOSE,
				"wk up Got data on ch_id:%d read_data_ready %d\n",
				uci_handle->in_chan,
				atomic_read(&uci_handle->read_data_ready));
		} else if (*bytes_avail > 0) {
			/* A valid packet was returned from MHI */
			uci_log(UCI_DBG_VERBOSE,
				"Got packet: avail pkts %d phy_adr %pK, ch_id:%d\n",
				atomic_read(&uci_handle->read_data_ready),
				uci_handle->pkt_loc,
				uci_handle->in_chan);
			break;
		}
	}

	return ret_val;
}

static ssize_t mhi_uci_client_read(struct file *file, char __user *ubuf,
	size_t uspace_buf_size, loff_t *bytes_pending)
{
	struct uci_client *uci_handle = NULL;
	int bytes_avail = 0, ret_val = 0;
	struct mutex *mutex;
	ssize_t bytes_copied = 0;
	u32 addr_offset = 0;

	if (!file || !ubuf || !file->private_data) {
		uci_log(UCI_DBG_DBG, "Invalid access to read\n");
		return -EINVAL;
	}

	uci_handle = file->private_data;
	if (!uci_handle->read || !uci_handle->in_handle) {
		uci_log(UCI_DBG_DBG, "Invalid inhandle or read\n");
		return -EINVAL;
	}
	mutex = &uci_handle->in_chan_lock;
	mutex_lock(mutex);

	uci_log(UCI_DBG_VERBOSE, "Client attempted read on ch_id:%d\n",
		uci_handle->in_chan);

	ret_val = __mhi_uci_client_read(uci_handle, &bytes_avail);
	if (ret_val)
		goto error;

	if (bytes_avail > 0)
		*bytes_pending = (loff_t)uci_handle->pkt_size;

	if (uspace_buf_size >= *bytes_pending) {
		addr_offset = uci_handle->pkt_size - *bytes_pending;
		if (copy_to_user(ubuf, uci_handle->pkt_loc + addr_offset,
			*bytes_pending)) {
			ret_val = -EIO;
			goto error;
		}

		bytes_copied = *bytes_pending;
		*bytes_pending = 0;
		uci_log(UCI_DBG_VERBOSE, "Copied 0x%lx of 0x%x, ch_id:%d\n",
			bytes_copied, (u32)*bytes_pending, uci_handle->in_chan);
	} else {
		addr_offset = uci_handle->pkt_size - *bytes_pending;
		if (copy_to_user(ubuf, (void *) (uintptr_t)uci_handle->pkt_loc +
			addr_offset, uspace_buf_size)) {
			ret_val = -EIO;
			goto error;
		}
		bytes_copied = uspace_buf_size;
		*bytes_pending -= uspace_buf_size;
		uci_log(UCI_DBG_VERBOSE, "Copied 0x%lx of 0x%x,ch_id:%d\n",
			bytes_copied,
			(u32)*bytes_pending,
			uci_handle->in_chan);
	}
	/* We finished with this buffer, map it back */
	if (*bytes_pending == 0) {
		uci_log(UCI_DBG_VERBOSE,
			"All data consumed. Pkt loc %p ,ch_id:%d\n",
			uci_handle->pkt_loc, uci_handle->in_chan);
		uci_handle->pkt_loc = 0;
		uci_handle->pkt_size = 0;
	}
	uci_log(UCI_DBG_VERBOSE,
		"Returning 0x%lx bytes, 0x%x bytes left\n",
		bytes_copied, (u32)*bytes_pending);
	mutex_unlock(mutex);
	return bytes_copied;
error:
	mutex_unlock(mutex);

	uci_log_ratelimit(UCI_DBG_ERROR, "Returning %d\n", ret_val);
	return ret_val;
}

static ssize_t mhi_uci_client_write(struct file *file,
			const char __user *buf, size_t count, loff_t *offp)
{
	struct uci_client *uci_handle = NULL;
	void *data_loc;
	const char __user *cur_buf;
	unsigned long memcpy_result;
	int rc = 0, tre_len, cur_rc = 0, count_left, cur_txfr_len;

	if (!file || !buf || !count || !file->private_data) {
		uci_log(UCI_DBG_DBG, "Invalid access to write\n");
		return -EINVAL;
	}

	uci_handle = file->private_data;
	tre_len = uci_handle->tre_len;

	if (!uci_handle->send || !uci_handle->out_handle) {
		uci_log(UCI_DBG_DBG, "Invalid handle or send\n");
		return -EINVAL;
	}
	if (atomic_read(&uci_ctxt.mhi_disabled)) {
		uci_log(UCI_DBG_ERROR,
			"Client %d attempted to write while MHI is disabled\n",
			uci_handle->out_chan);
		return -EIO;
	}

	if (!mhi_uci_are_channels_connected(uci_handle)) {
		uci_log(UCI_DBG_ERROR, "Channels are not connected\n");
		return -ENODEV;
	}

	if (count > uci_handle->out_chan_attr->max_packet_size) {
		uci_log(UCI_DBG_DBG,
			"Warning: big write size: %lu, max supported size is %zu\n",
			count, uci_handle->out_chan_attr->max_packet_size);
	}

	cur_txfr_len = count;

	if (!tre_len)
		/*
		 * If the requested transfer size is more that the TRE size, then split
		 * the write into multiple transfers, each transfer with a size equal to
		 * TRE size (except the last one).
		 */
		uci_log(UCI_DBG_ERROR, "tre_len is 0, not updated yet\n");
	else if (count > tre_len) {
		uci_log(UCI_DBG_DBG, "Write req size (%zu) > tre_len (%d)\n", count, tre_len);
		cur_txfr_len = tre_len;
	}

	count_left = count;
	cur_buf = buf;

	do {
		data_loc = kmalloc(cur_txfr_len, GFP_KERNEL);
		if (!data_loc) {
			uci_log(UCI_DBG_ERROR, "Memory allocation failed\n");
			return -ENOMEM;
		}

		memcpy_result = copy_from_user(data_loc, cur_buf, cur_txfr_len);
		if (memcpy_result) {
			uci_log(UCI_DBG_ERROR, "Mem copy failed\n");
			rc = -EFAULT;
			goto error_memcpy;
		}

		cur_rc = mhi_uci_send_packet(uci_handle, data_loc, cur_txfr_len);
		if (cur_rc != cur_txfr_len) {
			uci_log(UCI_DBG_ERROR,
					"Send failed with error %d, after sending %d data\n",
					cur_rc, rc);
			rc = cur_rc;
			goto error_memcpy;
		}
		rc += cur_rc;
		cur_buf += cur_txfr_len;
		count_left -= cur_txfr_len;

		if (count_left < tre_len)
			cur_txfr_len = count_left;
	} while (count_left);
	if (rc == count)
		return rc;

error_memcpy:
	kfree(data_loc);
	return rc;

}

static ssize_t mhi_uci_client_write_iter(struct kiocb *iocb,
					 struct iov_iter *buf)
{
	struct uci_client *uci_handle = NULL;
	void *data_loc;
	unsigned long memcpy_result;
	int rc = 0, tre_len, cur_rc = 0, count_left, cur_txfr_len;
	struct file *file = iocb->ki_filp;
	ssize_t count;

	if (!buf) {
		uci_log(UCI_DBG_DBG, "Invalid access to write-iter, buf is NULL\n");
		return -EINVAL;
	}
	count = iov_iter_count(buf);

	if (!file || !count || !file->private_data) {
		uci_log(UCI_DBG_DBG, "Invalid access to write-iter\n");
		return -EINVAL;
	}

	uci_handle = file->private_data;
	tre_len = uci_handle->tre_len;

	if (!uci_handle->send || !uci_handle->out_handle) {
		uci_log(UCI_DBG_DBG, "Invalid handle or send\n");
		return -EINVAL;
	}
	if (atomic_read(&uci_ctxt.mhi_disabled)) {
		uci_log(UCI_DBG_ERROR,
			"Client %d attempted to write while MHI is disabled\n",
			uci_handle->out_chan);
		return -EIO;
	}

	if (!mhi_uci_are_channels_connected(uci_handle)) {
		uci_log(UCI_DBG_ERROR, "Channels are not connected\n");
		return -ENODEV;
	}

	if (count > uci_handle->out_chan_attr->max_packet_size) {
		uci_log(UCI_DBG_DBG,
			"Warning: big write size: %lu, max supported size is %zu\n",
			count, uci_handle->out_chan_attr->max_packet_size);
	}

	cur_txfr_len = count;

	if (!tre_len)
		/*
		 * If the requested transfer size is more that the TRE size, then split
		 * the write into multiple transfers, each transfer with a size equal to
		 * TRE size (except the last one).
		 */
		uci_log(UCI_DBG_ERROR, "tre_len is 0, not updated yet\n");
	else if (count > tre_len) {
		uci_log(UCI_DBG_DBG, "Write req size (%zd) > tre_len (%d)\n", count, tre_len);
		cur_txfr_len = tre_len;
	}

	count_left = count;

	do {
		data_loc = kmalloc(cur_txfr_len, GFP_KERNEL);
		if (!data_loc) {
			uci_log(UCI_DBG_ERROR, "Memory allocation failed\n");
			return -ENOMEM;
		}

		memcpy_result = copy_from_iter_full(data_loc, cur_txfr_len, buf);
		if (!memcpy_result) {
			uci_log(UCI_DBG_ERROR, "Mem copy failed\n");
			rc = -EFAULT;
			goto error_memcpy;
		}
		cur_rc = mhi_uci_send_packet(uci_handle, data_loc, cur_txfr_len);
		if (cur_rc != cur_txfr_len) {
			uci_log(UCI_DBG_ERROR,
					"Send failed with error %d, after sending %d data\n",
					cur_rc, rc);
			rc = cur_rc;
			goto error_memcpy;
		}
		rc += cur_rc;
		count_left -= cur_txfr_len;
		if (count_left < tre_len)
			cur_txfr_len = count_left;
	} while (count_left);

	if (rc == count)
		return rc;

error_memcpy:
	kfree(data_loc);
	return rc;
}

void mhi_uci_chan_state_notify_all(struct mhi_dev *mhi,
		enum mhi_ctrl_info ch_state)
{
	unsigned int i;
	const struct chan_attr *chan_attrib;

	for (i = 0; i < ARRAY_SIZE(mhi_chan_attr_table); i++) {
		chan_attrib = &mhi_chan_attr_table[i];
		if (chan_attrib->state_bcast) {
			uci_log(UCI_DBG_ERROR, "Calling notify for ch_id:%d\n",
					chan_attrib->chan_id);
			mhi_uci_chan_state_notify(mhi, chan_attrib->chan_id,
					ch_state);
		}
	}
}
EXPORT_SYMBOL_GPL(mhi_uci_chan_state_notify_all);

void mhi_uci_chan_state_notify(struct mhi_dev *mhi,
		enum mhi_client_channel ch_id, enum mhi_ctrl_info ch_state)
{
	struct uci_client *uci_handle;
	char *buf[2];
	int rc;

	if (ch_id < 0 || ch_id >= MHI_MAX_SOFTWARE_CHANNELS) {
		uci_log(UCI_DBG_VERBOSE, "Invalid ch_id:%d\n", ch_id);
		return;
	}

	uci_handle = &uci_ctxt.client_handles[CHAN_TO_CLIENT(ch_id)];
	if (!uci_handle->out_chan_attr ||
		!uci_handle->out_chan_attr->state_bcast) {
		uci_log(UCI_DBG_VERBOSE, "Uevents not enabled for ch_id:%d\n",
				ch_id);
		return;
	}

	if (ch_state == MHI_STATE_CONNECTED) {
		buf[0] = kasprintf(GFP_KERNEL,
				"MHI_CHANNEL_STATE_%d=CONNECTED", ch_id);
		buf[1] = NULL;
	} else if (ch_state == MHI_STATE_DISCONNECTED) {
		buf[0] = kasprintf(GFP_KERNEL,
				"MHI_CHANNEL_STATE_%d=DISCONNECTED", ch_id);
		buf[1] = NULL;
	} else {
		uci_log(UCI_DBG_ERROR, "Unsupported chan state %d\n", ch_state);
		return;
	}

	if (!buf[0]) {
		uci_log(UCI_DBG_ERROR, "kasprintf failed for uevent buf!\n");
		return;
	}

	rc = kobject_uevent_env(&mhi->mhi_hw_ctx->dev->kobj, KOBJ_CHANGE, buf);
	if (rc)
		uci_log(UCI_DBG_ERROR,
				"Sending uevent failed for ch_id:%d\n", ch_id);

	if (ch_state == MHI_STATE_DISCONNECTED &&
			!atomic_read(&uci_handle->ref_count)) {
		/* Issue wake only if there is an active client */
		wake_up(&uci_handle->read_wq);
		wake_up(&uci_handle->write_wq);
	}

	kfree(buf[0]);
}
EXPORT_SYMBOL_GPL(mhi_uci_chan_state_notify);

void uci_ctrl_update(struct mhi_dev_client_cb_reason *reason)
{
	struct uci_ctrl *uci_ctrl_handle = NULL;

	if (reason->reason == MHI_DEV_CTRL_UPDATE) {
		uci_ctrl_handle = &uci_ctxt.ctrl_handle;
		if (!uci_ctrl_handle) {
			uci_log(UCI_DBG_ERROR, "Invalid uci ctrl handle\n");
			return;
		}

		uci_log(UCI_DBG_DBG, "received state change update\n");
		wake_up(&uci_ctrl_handle->ctrl_wq);
		atomic_set(&uci_ctrl_handle->ctrl_data_update, 1);
	}
}
EXPORT_SYMBOL_GPL(uci_ctrl_update);

static void uci_event_notifier(struct mhi_dev_client_cb_reason *reason)
{
	int client_index = 0;
	struct uci_client *uci_handle = NULL;

	client_index = reason->ch_id / 2;
	uci_handle = &uci_ctxt.client_handles[client_index];
	/*
	 * If this client has its own TRE event handler, call that
	 * else use the default handler.
	 */
	if (uci_handle->out_chan_attr->tre_notif_cb) {
		uci_handle->out_chan_attr->tre_notif_cb(reason);
	} else if (reason->reason == MHI_DEV_TRE_AVAILABLE) {
		uci_log(UCI_DBG_DBG,
			"recived TRE available event for ch_id:%d\n",
			uci_handle->in_chan);
		if (reason->ch_id % 2) {
			atomic_set(&uci_handle->write_data_ready, 1);
			uci_handle->tre_len = uci_handle->out_handle->channel->tre_size;
			wake_up(&uci_handle->write_wq);
		} else {
			atomic_set(&uci_handle->read_data_ready, 1);
			wake_up(&uci_handle->read_wq);
		}
	}
}

static int mhi_register_client(struct uci_client *mhi_client, int index)
{
	init_waitqueue_head(&mhi_client->read_wq);
	init_waitqueue_head(&mhi_client->write_wq);
	mhi_client->client_index = index;

	mutex_init(&mhi_client->in_chan_lock);
	mutex_init(&mhi_client->out_chan_lock);
	mutex_init(&mhi_client->client_lock);
	spin_lock_init(&mhi_client->req_lock);
	/* Init the completion event for AT ctrl read */
	init_completion(&mhi_client->at_ctrl_read_done);

	uci_log(UCI_DBG_DBG, "Registering ch_id:%d.\n", mhi_client->out_chan);
	return 0;
}

static int mhi_uci_ctrl_set_tiocm(struct uci_client *client,
				unsigned int ser_state)
{
	unsigned int cur_ser_state;
	unsigned long compl_ret;
	struct mhi_uci_ctrl_msg *ctrl_msg;
	int ret_val;
	struct uci_client *ctrl_client =
		&uci_ctxt.client_handles[CHAN_TO_CLIENT
					(MHI_CLIENT_IP_CTRL_1_OUT)];
	unsigned int info = 0;

	uci_log(UCI_DBG_VERBOSE, "Rcvd ser_state = 0x%x\n", ser_state);

	/* Check if the IP_CTRL channels were started by host */
	mhi_ctrl_state_info(MHI_CLIENT_IP_CTRL_1_IN, &info);
	if (info != MHI_STATE_CONNECTED) {
		uci_log(UCI_DBG_VERBOSE,
			"IP_CTRL channels not started by host yet\n");
		return -EAGAIN;
	}

	cur_ser_state = client->tiocm & ~(TIOCM_DTR | TIOCM_RTS);
	ser_state &= (TIOCM_CD | TIOCM_DSR | TIOCM_RI);

	if (cur_ser_state == ser_state)
		return 0;

	ctrl_msg = kzalloc(sizeof(*ctrl_msg), GFP_KERNEL);
	if (!ctrl_msg)
		return -ENOMEM;

	ctrl_msg->preamble = MHI_UCI_CTRL_MSG_MAGIC;
	ctrl_msg->msg_id = MHI_UCI_CTRL_MSGID_SERIAL_STATE;
	ctrl_msg->dest_id = client->out_chan;
	ctrl_msg->size = sizeof(unsigned int);
	if (ser_state & TIOCM_CD)
		ctrl_msg->msg |= MHI_UCI_CTRL_MSG_DCD;
	if (ser_state & TIOCM_DSR)
		ctrl_msg->msg |= MHI_UCI_CTRL_MSG_DSR;
	if (ser_state & TIOCM_RI)
		ctrl_msg->msg |= MHI_UCI_CTRL_MSG_RI;

	reinit_completion(ctrl_client->write_done);
	ret_val = mhi_uci_send_packet(ctrl_client, ctrl_msg, sizeof(*ctrl_msg));
	if (ret_val != sizeof(*ctrl_msg)) {
		uci_log(UCI_DBG_ERROR, "Failed to send ctrl msg\n");
		kfree(ctrl_msg);
		ctrl_msg = NULL;
		goto tiocm_error;
	}
	compl_ret = wait_for_completion_interruptible_timeout(
			ctrl_client->write_done,
			MHI_UCI_ASYNC_WRITE_TIMEOUT);
	if (compl_ret == -ERESTARTSYS) {
		uci_log(UCI_DBG_ERROR, "Exit signal caught\n");
		ret_val = compl_ret;
		goto tiocm_error;
	} else if (compl_ret == 0) {
		uci_log(UCI_DBG_ERROR, "Timed out trying to send ctrl msg\n");
		ret_val = -EIO;
		goto tiocm_error;
	}

	client->tiocm &= ~(TIOCM_CD | TIOCM_DSR | TIOCM_RI);
	client->tiocm |= ser_state;
	return 0;

tiocm_error:
	return ret_val;
}

static void mhi_uci_at_ctrl_read(struct work_struct *work)
{
	int ret_val;
	int msg_size = 0;
	struct uci_client *ctrl_client =
		&uci_ctxt.client_handles[CHAN_TO_CLIENT
		(MHI_CLIENT_IP_CTRL_1_OUT)];
	struct uci_client *tgt_client;
	struct mhi_uci_ctrl_msg *ctrl_msg;
	unsigned int chan;
	unsigned long compl_ret;

	while (!mhi_dev_channel_isempty(ctrl_client->in_handle)) {

		ctrl_client->pkt_loc = NULL;
		ctrl_client->pkt_size = 0;

		ret_val = __mhi_uci_client_read(ctrl_client, &msg_size);
		if (ret_val) {
			uci_log(UCI_DBG_ERROR,
				"Ctrl msg read failed, ret_val is %d\n",
				ret_val);
			return;
		}
		if (msg_size != sizeof(*ctrl_msg)) {
			uci_log(UCI_DBG_ERROR, "Invalid ctrl msg size\n");
			return;
		}
		if (!ctrl_client->pkt_loc) {
			uci_log(UCI_DBG_ERROR, "ctrl msg pkt_loc null\n");
			return;
		}
		ctrl_msg = ctrl_client->pkt_loc;
		chan = ctrl_msg->dest_id;

		if (chan >= MHI_MAX_SOFTWARE_CHANNELS) {
			uci_log(UCI_DBG_ERROR,
				"Invalid channel number in ctrl msg\n");
			return;
		}

		uci_log(UCI_DBG_VERBOSE, "preamble: 0x%x\n",
				ctrl_msg->preamble);
		uci_log(UCI_DBG_VERBOSE, "msg_id: 0x%x\n", ctrl_msg->msg_id);
		uci_log(UCI_DBG_VERBOSE, "dest_id: 0x%x\n", ctrl_msg->dest_id);
		uci_log(UCI_DBG_VERBOSE, "size: 0x%x\n", ctrl_msg->size);
		uci_log(UCI_DBG_VERBOSE, "msg: 0x%x\n", ctrl_msg->msg);

		tgt_client = &uci_ctxt.client_handles[CHAN_TO_CLIENT(chan)];
		tgt_client->tiocm &= ~(TIOCM_DTR | TIOCM_RTS);

		if (ctrl_msg->msg & MHI_UCI_CTRL_MSG_DTR)
			tgt_client->tiocm |= TIOCM_DTR;
		if (ctrl_msg->msg & MHI_UCI_CTRL_MSG_RTS)
			tgt_client->tiocm |= TIOCM_RTS;

		uci_log(UCI_DBG_VERBOSE, "Rcvd tiocm %d\n", tgt_client->tiocm);

		/* Wait till client reads the new state */
		reinit_completion(&tgt_client->at_ctrl_read_done);

		tgt_client->at_ctrl_mask = POLLPRI;
		wake_up(&tgt_client->read_wq);

		uci_log(UCI_DBG_VERBOSE, "Waiting for at_ctrl_read_done");
		compl_ret = wait_for_completion_interruptible_timeout(
					&tgt_client->at_ctrl_read_done,
					MHI_UCI_AT_CTRL_READ_TIMEOUT);
		if (compl_ret == -ERESTARTSYS) {
			uci_log(UCI_DBG_ERROR, "Exit signal caught\n");
			return;
		} else if (compl_ret == 0) {
			uci_log(UCI_DBG_ERROR,
			"Timed out waiting for client to read ctrl state\n");
		}
	}
}

static long mhi_uci_client_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	struct uci_client *uci_handle = NULL;
	int rc = 0;
	struct ep_info epinfo;
	unsigned int tiocm;

	if (file == NULL || file->private_data == NULL)
		return -EINVAL;

	uci_handle = file->private_data;

	uci_log(UCI_DBG_DBG, "Received command %d for client:%d\n",
		cmd, uci_handle->client_index);

	if (cmd == MHI_UCI_EP_LOOKUP) {
		uci_log(UCI_DBG_DBG, "EP_LOOKUP for client:%d\n",
						uci_handle->client_index);
		epinfo.ph_ep_info.ep_type = DATA_EP_TYPE_PCIE;
		epinfo.ph_ep_info.peripheral_iface_id = MHI_QTI_IFACE_ID;
		epinfo.ipa_ep_pair.cons_pipe_num =
			mhi_dev_get_ep_mapping(IPA_CLIENT_MHI_PROD);
		epinfo.ipa_ep_pair.prod_pipe_num =
			mhi_dev_get_ep_mapping(IPA_CLIENT_MHI_CONS);

		uci_log(UCI_DBG_DBG, "client:%d ep_type:%d intf:%d\n",
			uci_handle->client_index,
			epinfo.ph_ep_info.ep_type,
			epinfo.ph_ep_info.peripheral_iface_id);

		uci_log(UCI_DBG_DBG, "ipa_cons_idx:%d ipa_prod_idx:%d\n",
			epinfo.ipa_ep_pair.cons_pipe_num,
			epinfo.ipa_ep_pair.prod_pipe_num);

		rc = copy_to_user((void __user *)arg, &epinfo,
			sizeof(epinfo));
		if (rc)
			uci_log(UCI_DBG_ERROR, "copying to user space failed");
	} else if (cmd == MHI_UCI_TIOCM_GET) {
		rc = copy_to_user((void __user *)arg, &uci_handle->tiocm,
			sizeof(uci_handle->tiocm));
		if (rc) {
			uci_log(UCI_DBG_ERROR,
				"copying ctrl state to user space failed");
			rc = -EFAULT;
		}
		uci_handle->at_ctrl_mask = 0;
		uci_log(UCI_DBG_VERBOSE, "Completing at_ctrl_read_done");
		complete(&uci_handle->at_ctrl_read_done);
	} else if (cmd == MHI_UCI_TIOCM_SET) {
		rc = get_user(tiocm, (unsigned int __user *)arg);
		if (rc)
			return rc;
		rc = mhi_uci_ctrl_set_tiocm(uci_handle, tiocm);
	} else if (cmd == MHI_UCI_DPL_EP_LOOKUP) {
		uci_log(UCI_DBG_DBG, "DPL EP_LOOKUP for client:%d\n",
			uci_handle->client_index);
		epinfo.ph_ep_info.ep_type = DATA_EP_TYPE_PCIE;
		epinfo.ph_ep_info.peripheral_iface_id = MHI_ADPL_IFACE_ID;
		epinfo.ipa_ep_pair.prod_pipe_num =
			mhi_dev_get_ep_mapping(IPA_CLIENT_MHI_DPL_CONS);
		/* For DPL set cons pipe to -1 to indicate it is unused */
		epinfo.ipa_ep_pair.cons_pipe_num = -1;

		uci_log(UCI_DBG_DBG, "client:%d ep_type:%d intf:%d\n",
			uci_handle->client_index,
			epinfo.ph_ep_info.ep_type,
			epinfo.ph_ep_info.peripheral_iface_id);

		uci_log(UCI_DBG_DBG, "DPL ipa_prod_idx:%d\n",
			epinfo.ipa_ep_pair.prod_pipe_num);

		rc = copy_to_user((void __user *)arg, &epinfo,
			sizeof(epinfo));
		if (rc)
			uci_log(UCI_DBG_ERROR, "copying to user space failed");
	} else if (cmd == MHI_UCI_CV2X_EP_LOOKUP) {
		uci_log(UCI_DBG_DBG, "CV2X EP_LOOKUP for client:%d\n",
						uci_handle->client_index);
		epinfo.ph_ep_info.ep_type = DATA_EP_TYPE_PCIE;
		epinfo.ph_ep_info.peripheral_iface_id = MHI_CV2X_IFACE_ID;
		epinfo.ipa_ep_pair.cons_pipe_num =
			mhi_dev_get_ep_mapping(IPA_CLIENT_MHI2_PROD);
		epinfo.ipa_ep_pair.prod_pipe_num =
			mhi_dev_get_ep_mapping(IPA_CLIENT_MHI2_CONS);

		uci_log(UCI_DBG_DBG, "client:%d ep_type:%d intf:%d\n",
			uci_handle->client_index,
			epinfo.ph_ep_info.ep_type,
			epinfo.ph_ep_info.peripheral_iface_id);

		uci_log(UCI_DBG_DBG, "ipa_cons2_idx:%d ipa_prod2_idx:%d\n",
			epinfo.ipa_ep_pair.cons_pipe_num,
			epinfo.ipa_ep_pair.prod_pipe_num);

		rc = copy_to_user((void __user *)arg, &epinfo,
			sizeof(epinfo));
		if (rc)
			uci_log(UCI_DBG_ERROR, "copying to user space failed");
	} else {
		uci_log(UCI_DBG_ERROR, "wrong parameter:%d\n", cmd);
		rc = -EINVAL;
	}

	return rc;
}

static const struct file_operations mhi_uci_ctrl_client_fops = {
	.open = mhi_uci_ctrl_open,
	.read = mhi_uci_ctrl_client_read,
	.poll = mhi_uci_ctrl_poll,
};

static const struct file_operations mhi_uci_client_fops = {
	.read = mhi_uci_client_read,
	.write = mhi_uci_client_write,
	.write_iter = mhi_uci_client_write_iter,
	.open = mhi_uci_client_open,
	.release = mhi_uci_client_release,
	.poll = mhi_uci_client_poll,
	.unlocked_ioctl = mhi_uci_client_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = mhi_uci_client_ioctl,
#endif
};

static int uci_device_create(struct uci_client *client)
{
	unsigned long r;
	int n;
	ssize_t dst_size;
	unsigned int client_index;
	static char device_name[MAX_DEVICE_NAME_SIZE];

	client_index = CHAN_TO_CLIENT(client->out_chan);
	if (uci_ctxt.client_handles[client_index].dev)
		return -EEXIST;

	cdev_init(&uci_ctxt.cdev[client_index], &mhi_uci_client_fops);
	uci_ctxt.cdev[client_index].owner = THIS_MODULE;
	r = cdev_add(&uci_ctxt.cdev[client_index],
		uci_ctxt.start_ctrl_nr + client_index, 1);
	if (IS_ERR_VALUE(r)) {
		uci_log(UCI_DBG_ERROR,
			"Failed to add cdev for client %d, ret 0x%lx\n",
			client_index, r);
		return r;
	}
	if (!client->in_chan_attr->device_name) {
		n = scnprintf(device_name, sizeof(device_name),
			DEVICE_NAME "_pipe_%d", CLIENT_TO_CHAN(client_index));
		if (n >= sizeof(device_name)) {
			uci_log(UCI_DBG_ERROR, "Device name buf too short\n");
			r = -E2BIG;
			goto error;
		}
	} else {
		dst_size = strscpy(device_name,
				client->in_chan_attr->device_name,
				sizeof(device_name));
		if (dst_size <= 0) {
			uci_log(UCI_DBG_ERROR, "Device name buf too short\n");
			r = dst_size;
			goto error;
		}
	}

	uci_ctxt.client_handles[client_index].dev =
		device_create(uci_ctxt.mhi_uci_class, NULL,
				uci_ctxt.start_ctrl_nr + client_index,
				NULL, device_name);
	if (IS_ERR(uci_ctxt.client_handles[client_index].dev)) {
		uci_log(UCI_DBG_ERROR,
			"Failed to create device for client %d\n",
			client_index);
		r = -EIO;
		goto error;
	}

	uci_log(UCI_DBG_INFO,
		"Created device with class 0x%pK and ctrl number %d\n",
		uci_ctxt.mhi_uci_class,
		uci_ctxt.start_ctrl_nr + client_index);

	return 0;

error:
	cdev_del(&uci_ctxt.cdev[client_index]);
	return r;
}

static void mhi_uci_at_ctrl_tre_cb(struct mhi_dev_client_cb_reason *reason)
{
	int client_index;
	struct uci_client *uci_handle;

	client_index = reason->ch_id / 2;
	uci_handle = &uci_ctxt.client_handles[client_index];

	if (reason->reason == MHI_DEV_TRE_AVAILABLE) {
		if (reason->ch_id % 2) {
			atomic_set(&uci_handle->write_data_ready, 1);
			wake_up(&uci_handle->write_wq);
		} else {
			queue_work(uci_ctxt.at_ctrl_wq, &uci_ctxt.at_ctrl_work);
		}
	}
}

static void mhi_uci_at_ctrl_client_cb(struct mhi_dev_client_cb_data *cb_data)
{
	struct uci_client *client = cb_data->user_data;
	int rc, i;
	struct mhi_req *ureq;

	uci_log(UCI_DBG_VERBOSE, " Rcvd MHI cb for ch_id:%d, state %d\n",
		cb_data->channel, cb_data->ctrl_info);

	if (cb_data->ctrl_info == MHI_STATE_CONNECTED) {
		/* Open the AT ctrl channels */
		rc = open_client_mhi_channels(client);
		if (rc) {
			uci_log(UCI_DBG_INFO,
				"Failed to open channels ret %d\n", rc);
			return;
		}
		/* Init the completion event for AT ctrl writes */
		init_completion(client->write_done);
		/* Create a work queue to process AT commands */
		uci_ctxt.at_ctrl_wq =
			create_singlethread_workqueue("mhi_at_ctrl_wq");
		INIT_WORK(&uci_ctxt.at_ctrl_work, mhi_uci_at_ctrl_read);
	} else if (cb_data->ctrl_info == MHI_STATE_DISCONNECTED) {
		if (uci_ctxt.at_ctrl_wq == NULL) {
			uci_log(UCI_DBG_VERBOSE,
				"Disconnect already processed for at ctrl channels\n");
			return;
		}
		destroy_workqueue(uci_ctxt.at_ctrl_wq);
		uci_ctxt.at_ctrl_wq = NULL;
		mhi_dev_close_channel(client->out_handle);
		mhi_dev_close_channel(client->in_handle);

		/* Add back reqs from in-use list, if any, to free list */
		if (!(client->f_flags & O_SYNC)) {
			while (!(list_empty(&client->in_use_list))) {
				ureq = container_of(client->in_use_list.next,
							struct mhi_req, list);
				list_del_init(&ureq->list);
				/* Add to in-use list */
				list_add_tail(&ureq->list, &client->req_list);
			}
		}

		for (i = 0; i < (client->in_chan_attr->nr_trbs); i++) {
			kfree(client->in_buf_list[i].addr);
			client->in_buf_list[i].addr = NULL;
			client->in_buf_list[i].buf_size = 0;
		}
	}
}

static void mhi_uci_generic_client_cb(struct mhi_dev_client_cb_data *cb_data)
{
	struct uci_client *client = cb_data->user_data;

	uci_log(UCI_DBG_DBG, "Rcvd MHI cb for ch_id:%d, state %d\n",
		cb_data->channel, cb_data->ctrl_info);

	if (cb_data->ctrl_info == MHI_STATE_CONNECTED)
		uci_device_create(client);
}

static int uci_init_client_attributes(struct mhi_uci_ctxt_t *uci_ctxt)
{
	u32 i;
	u32 index;
	struct uci_client *client;
	const struct chan_attr *chan_attrib;

	for (i = 0; i < ARRAY_SIZE(mhi_chan_attr_table); i += 2) {
		chan_attrib = &mhi_chan_attr_table[i];
		index = CHAN_TO_CLIENT(chan_attrib->chan_id);
		client = &uci_ctxt->client_handles[index];
		client->out_chan_attr = chan_attrib;
		client->in_chan_attr = ++chan_attrib;
		client->in_chan = index * 2;
		client->out_chan = index * 2 + 1;
		client->at_ctrl_mask = 0;
		client->in_buf_list =
			kcalloc(chan_attrib->nr_trbs,
			sizeof(struct mhi_dev_iov),
			GFP_KERNEL);
		if (!client->in_buf_list)
			return -ENOMEM;
		/* Register channel state change cb with MHI if requested */
		if (client->out_chan_attr->chan_state_cb)
			mhi_register_state_cb(
					client->out_chan_attr->chan_state_cb,
					client,
					client->out_chan);
		if (client->in_chan_attr->wr_cmpl) {
			client->write_done = kzalloc(
					sizeof(*client->write_done),
					GFP_KERNEL);
			if (!client->write_done)
				return -ENOMEM;
		}
	}
	return 0;
}

int mhi_uci_init(void)
{
	u32 i = 0;
	int ret_val = 0;
	struct uci_client *mhi_client = NULL;
	unsigned long r = 0;

	mhi_uci_ipc_log = ipc_log_context_create(MHI_UCI_IPC_LOG_PAGES,
						"mhi-uci", 0);
	if (mhi_uci_ipc_log == NULL) {
		uci_log(UCI_DBG_WARNING,
				"Failed to create IPC logging context\n");
	}
	uci_ctxt.event_notifier = uci_event_notifier;

	uci_log(UCI_DBG_DBG, "Setting up channel attributes.\n");

	ret_val = uci_init_client_attributes(&uci_ctxt);
	if (ret_val < 0) {
		uci_log(UCI_DBG_ERROR,
				"Failed to init client attributes\n");
		return -EIO;
	}

	uci_log(UCI_DBG_DBG, "Initializing clients\n");
	uci_log(UCI_DBG_INFO, "Registering for MHI events.\n");

	for (i = 0; i < MHI_SOFTWARE_CLIENT_LIMIT; i++) {
		mhi_client = &uci_ctxt.client_handles[i];
		if (!mhi_client->in_chan_attr)
			continue;
		r = mhi_register_client(mhi_client, i);
		if (r) {
			uci_log(UCI_DBG_CRITICAL,
				"Failed to reg client %lu ret %d\n",
				r, i);
		}
	}

	init_waitqueue_head(&uci_ctxt.ctrl_handle.ctrl_wq);
	uci_log(UCI_DBG_INFO, "Allocating char devices.\n");
	r = alloc_chrdev_region(&uci_ctxt.start_ctrl_nr,
			0, MHI_MAX_SOFTWARE_CHANNELS,
			DEVICE_NAME);
	if (IS_ERR_VALUE(r)) {
		uci_log(UCI_DBG_ERROR,
				"Failed to alloc char devs, ret 0x%lx\n", r);
		goto failed_char_alloc;
	}

	r = alloc_chrdev_region(&uci_ctxt.ctrl_nr, 0, 1, DEVICE_NAME);
	if (IS_ERR_VALUE(r)) {
		uci_log(UCI_DBG_ERROR,
				"Failed to alloc char ctrl devs, 0x%lx\n", r);
		goto failed_char_alloc;
	}

	uci_log(UCI_DBG_INFO, "Creating class\n");
	uci_ctxt.mhi_uci_class = class_create(DEVICE_NAME);
	if (IS_ERR(uci_ctxt.mhi_uci_class)) {
		uci_log(UCI_DBG_ERROR,
			"Failed to instantiate class, ret 0x%lx\n", r);
		r = -ENOMEM;
		goto failed_class_add;
	}

	uci_log(UCI_DBG_INFO, "Setting up device nodes.\n");
	for (i = 0; i < MHI_SOFTWARE_CLIENT_LIMIT; i++) {
		mhi_client = &uci_ctxt.client_handles[i];
		if (!mhi_client->in_chan_attr)
			continue;
		/*
		 * Delay device node creation until the callback for
		 * this client's channels is called by the MHI driver,
		 * if one is registered.
		 */
		if (mhi_client->in_chan_attr->chan_state_cb ||
				mhi_client->in_chan_attr->skip_node)
			continue;
		ret_val = uci_device_create(mhi_client);
		if (ret_val)
			goto failed_device_create;
	}

	/* Control node */
	uci_ctxt.cdev_ctrl = cdev_alloc();
	if (uci_ctxt.cdev_ctrl == NULL) {
		uci_log(UCI_DBG_ERROR, "ctrl cdev alloc failed\n");
		return 0;
	}

	cdev_init(uci_ctxt.cdev_ctrl, &mhi_uci_ctrl_client_fops);
	uci_ctxt.cdev_ctrl->owner = THIS_MODULE;
	r = cdev_add(uci_ctxt.cdev_ctrl, uci_ctxt.ctrl_nr, 1);
	if (IS_ERR_VALUE(r)) {
		uci_log(UCI_DBG_ERROR,
		"Failed to add ctrl cdev %d, ret 0x%lx\n", i, r);
		kfree(uci_ctxt.cdev_ctrl);
		uci_ctxt.cdev_ctrl = NULL;
		return 0;
	}

	uci_ctxt.dev =
		device_create(uci_ctxt.mhi_uci_class, NULL,
				uci_ctxt.ctrl_nr,
				NULL, DEVICE_NAME "_ctrl");
	if (IS_ERR(uci_ctxt.dev)) {
		uci_log(UCI_DBG_ERROR,
				"Failed to add ctrl cdev %d\n", i);
		cdev_del(uci_ctxt.cdev_ctrl);
		kfree(uci_ctxt.cdev_ctrl);
		uci_ctxt.cdev_ctrl = NULL;
	}

	uci_ctxt.mhi_uci_class->dev_uevent = mhi_state_uevent;

	return 0;

failed_device_create:
	while (--i >= 0) {
		cdev_del(&uci_ctxt.cdev[i]);
		device_destroy(uci_ctxt.mhi_uci_class,
		MKDEV(MAJOR(uci_ctxt.start_ctrl_nr), i * 2));
	}
	class_destroy(uci_ctxt.mhi_uci_class);
failed_class_add:
	unregister_chrdev_region(MAJOR(uci_ctxt.start_ctrl_nr),
			MHI_MAX_SOFTWARE_CHANNELS);
failed_char_alloc:
	return r;
}
