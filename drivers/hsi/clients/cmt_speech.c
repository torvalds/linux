/*
 * cmt_speech.c - HSI CMT speech driver
 *
 * Copyright (C) 2008,2009,2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Kai Vehmanen <kai.vehmanen@nokia.com>
 * Original author: Peter Ujfalusi <peter.ujfalusi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/pm_qos.h>
#include <linux/hsi/hsi.h>
#include <linux/hsi/ssi_protocol.h>
#include <linux/hsi/cs-protocol.h>

#define CS_MMAP_SIZE	PAGE_SIZE

struct char_queue {
	struct list_head	list;
	u32			msg;
};

struct cs_char {
	unsigned int		opened;
	struct hsi_client	*cl;
	struct cs_hsi_iface	*hi;
	struct list_head	chardev_queue;
	struct list_head	dataind_queue;
	int			dataind_pending;
	/* mmap things */
	unsigned long		mmap_base;
	unsigned long		mmap_size;
	spinlock_t		lock;
	struct fasync_struct	*async_queue;
	wait_queue_head_t	wait;
	/* hsi channel ids */
	int                     channel_id_cmd;
	int                     channel_id_data;
};

#define SSI_CHANNEL_STATE_READING	1
#define SSI_CHANNEL_STATE_WRITING	(1 << 1)
#define SSI_CHANNEL_STATE_POLL		(1 << 2)
#define SSI_CHANNEL_STATE_ERROR		(1 << 3)

#define TARGET_MASK			0xf000000
#define TARGET_REMOTE			(1 << CS_DOMAIN_SHIFT)
#define TARGET_LOCAL			0

/* Number of pre-allocated commands buffers */
#define CS_MAX_CMDS		        4

/*
 * During data transfers, transactions must be handled
 * within 20ms (fixed value in cmtspeech HSI protocol)
 */
#define CS_QOS_LATENCY_FOR_DATA_USEC	20000

/* Timeout to wait for pending HSI transfers to complete */
#define CS_HSI_TRANSFER_TIMEOUT_MS      500


#define RX_PTR_BOUNDARY_SHIFT		8
#define RX_PTR_MAX_SHIFT		(RX_PTR_BOUNDARY_SHIFT + \
						CS_MAX_BUFFERS_SHIFT)
struct cs_hsi_iface {
	struct hsi_client		*cl;
	struct hsi_client		*master;

	unsigned int			iface_state;
	unsigned int			wakeline_state;
	unsigned int			control_state;
	unsigned int			data_state;

	/* state exposed to application */
	struct cs_mmap_config_block	*mmap_cfg;

	unsigned long			mmap_base;
	unsigned long			mmap_size;

	unsigned int			rx_slot;
	unsigned int			tx_slot;

	/* note: for security reasons, we do not trust the contents of
	 * mmap_cfg, but instead duplicate the variables here */
	unsigned int			buf_size;
	unsigned int			rx_bufs;
	unsigned int			tx_bufs;
	unsigned int			rx_ptr_boundary;
	unsigned int			rx_offsets[CS_MAX_BUFFERS];
	unsigned int			tx_offsets[CS_MAX_BUFFERS];

	/* size of aligned memory blocks */
	unsigned int			slot_size;
	unsigned int			flags;

	struct list_head		cmdqueue;

	struct hsi_msg			*data_rx_msg;
	struct hsi_msg			*data_tx_msg;
	wait_queue_head_t		datawait;

	struct pm_qos_request           pm_qos_req;

	spinlock_t			lock;
};

static struct cs_char cs_char_data;

static void cs_hsi_read_on_control(struct cs_hsi_iface *hi);
static void cs_hsi_read_on_data(struct cs_hsi_iface *hi);

static inline void rx_ptr_shift_too_big(void)
{
	BUILD_BUG_ON((1LLU << RX_PTR_MAX_SHIFT) > UINT_MAX);
}

static void cs_notify(u32 message, struct list_head *head)
{
	struct char_queue *entry;

	spin_lock(&cs_char_data.lock);

	if (!cs_char_data.opened) {
		spin_unlock(&cs_char_data.lock);
		goto out;
	}

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry) {
		dev_err(&cs_char_data.cl->device,
			"Can't allocate new entry for the queue.\n");
		spin_unlock(&cs_char_data.lock);
		goto out;
	}

	entry->msg = message;
	list_add_tail(&entry->list, head);

	spin_unlock(&cs_char_data.lock);

	wake_up_interruptible(&cs_char_data.wait);
	kill_fasync(&cs_char_data.async_queue, SIGIO, POLL_IN);

out:
	return;
}

static u32 cs_pop_entry(struct list_head *head)
{
	struct char_queue *entry;
	u32 data;

	entry = list_entry(head->next, struct char_queue, list);
	data = entry->msg;
	list_del(&entry->list);
	kfree(entry);

	return data;
}

static void cs_notify_control(u32 message)
{
	cs_notify(message, &cs_char_data.chardev_queue);
}

static void cs_notify_data(u32 message, int maxlength)
{
	cs_notify(message, &cs_char_data.dataind_queue);

	spin_lock(&cs_char_data.lock);
	cs_char_data.dataind_pending++;
	while (cs_char_data.dataind_pending > maxlength &&
				!list_empty(&cs_char_data.dataind_queue)) {
		dev_dbg(&cs_char_data.cl->device, "data notification "
		"queue overrun (%u entries)\n", cs_char_data.dataind_pending);

		cs_pop_entry(&cs_char_data.dataind_queue);
		cs_char_data.dataind_pending--;
	}
	spin_unlock(&cs_char_data.lock);
}

static inline void cs_set_cmd(struct hsi_msg *msg, u32 cmd)
{
	u32 *data = sg_virt(msg->sgt.sgl);
	*data = cmd;
}

static inline u32 cs_get_cmd(struct hsi_msg *msg)
{
	u32 *data = sg_virt(msg->sgt.sgl);
	return *data;
}

static void cs_release_cmd(struct hsi_msg *msg)
{
	struct cs_hsi_iface *hi = msg->context;

	list_add_tail(&msg->link, &hi->cmdqueue);
}

static void cs_cmd_destructor(struct hsi_msg *msg)
{
	struct cs_hsi_iface *hi = msg->context;

	spin_lock(&hi->lock);

	dev_dbg(&cs_char_data.cl->device, "control cmd destructor\n");

	if (hi->iface_state != CS_STATE_CLOSED)
		dev_err(&hi->cl->device, "Cmd flushed while driver active\n");

	if (msg->ttype == HSI_MSG_READ)
		hi->control_state &=
			~(SSI_CHANNEL_STATE_POLL | SSI_CHANNEL_STATE_READING);
	else if (msg->ttype == HSI_MSG_WRITE &&
			hi->control_state & SSI_CHANNEL_STATE_WRITING)
		hi->control_state &= ~SSI_CHANNEL_STATE_WRITING;

	cs_release_cmd(msg);

	spin_unlock(&hi->lock);
}

static struct hsi_msg *cs_claim_cmd(struct cs_hsi_iface* ssi)
{
	struct hsi_msg *msg;

	BUG_ON(list_empty(&ssi->cmdqueue));

	msg = list_first_entry(&ssi->cmdqueue, struct hsi_msg, link);
	list_del(&msg->link);
	msg->destructor = cs_cmd_destructor;

	return msg;
}

static void cs_free_cmds(struct cs_hsi_iface *ssi)
{
	struct hsi_msg *msg, *tmp;

	list_for_each_entry_safe(msg, tmp, &ssi->cmdqueue, link) {
		list_del(&msg->link);
		msg->destructor = NULL;
		kfree(sg_virt(msg->sgt.sgl));
		hsi_free_msg(msg);
	}
}

static int cs_alloc_cmds(struct cs_hsi_iface *hi)
{
	struct hsi_msg *msg;
	u32 *buf;
	unsigned int i;

	INIT_LIST_HEAD(&hi->cmdqueue);

	for (i = 0; i < CS_MAX_CMDS; i++) {
		msg = hsi_alloc_msg(1, GFP_KERNEL);
		if (!msg)
			goto out;
		buf = kmalloc(sizeof(*buf), GFP_KERNEL);
		if (!buf) {
			hsi_free_msg(msg);
			goto out;
		}
		sg_init_one(msg->sgt.sgl, buf, sizeof(*buf));
		msg->channel = cs_char_data.channel_id_cmd;
		msg->context = hi;
		list_add_tail(&msg->link, &hi->cmdqueue);
	}

	return 0;

out:
	cs_free_cmds(hi);
	return -ENOMEM;
}

static void cs_hsi_data_destructor(struct hsi_msg *msg)
{
	struct cs_hsi_iface *hi = msg->context;
	const char *dir = (msg->ttype == HSI_MSG_READ) ? "TX" : "RX";

	dev_dbg(&cs_char_data.cl->device, "Freeing data %s message\n", dir);

	spin_lock(&hi->lock);
	if (hi->iface_state != CS_STATE_CLOSED)
		dev_err(&cs_char_data.cl->device,
				"Data %s flush while device active\n", dir);
	if (msg->ttype == HSI_MSG_READ)
		hi->data_state &=
			~(SSI_CHANNEL_STATE_POLL | SSI_CHANNEL_STATE_READING);
	else
		hi->data_state &= ~SSI_CHANNEL_STATE_WRITING;

	msg->status = HSI_STATUS_COMPLETED;
	if (unlikely(waitqueue_active(&hi->datawait)))
		wake_up_interruptible(&hi->datawait);

	spin_unlock(&hi->lock);
}

static int cs_hsi_alloc_data(struct cs_hsi_iface *hi)
{
	struct hsi_msg *txmsg, *rxmsg;
	int res = 0;

	rxmsg = hsi_alloc_msg(1, GFP_KERNEL);
	if (!rxmsg) {
		res = -ENOMEM;
		goto out1;
	}
	rxmsg->channel = cs_char_data.channel_id_data;
	rxmsg->destructor = cs_hsi_data_destructor;
	rxmsg->context = hi;

	txmsg = hsi_alloc_msg(1, GFP_KERNEL);
	if (!txmsg) {
		res = -ENOMEM;
		goto out2;
	}
	txmsg->channel = cs_char_data.channel_id_data;
	txmsg->destructor = cs_hsi_data_destructor;
	txmsg->context = hi;

	hi->data_rx_msg = rxmsg;
	hi->data_tx_msg = txmsg;

	return 0;

out2:
	hsi_free_msg(rxmsg);
out1:
	return res;
}

static void cs_hsi_free_data_msg(struct hsi_msg *msg)
{
	WARN_ON(msg->status != HSI_STATUS_COMPLETED &&
					msg->status != HSI_STATUS_ERROR);
	hsi_free_msg(msg);
}

static void cs_hsi_free_data(struct cs_hsi_iface *hi)
{
	cs_hsi_free_data_msg(hi->data_rx_msg);
	cs_hsi_free_data_msg(hi->data_tx_msg);
}

static inline void __cs_hsi_error_pre(struct cs_hsi_iface *hi,
					struct hsi_msg *msg, const char *info,
					unsigned int *state)
{
	spin_lock(&hi->lock);
	dev_err(&hi->cl->device, "HSI %s error, msg %d, state %u\n",
		info, msg->status, *state);
}

static inline void __cs_hsi_error_post(struct cs_hsi_iface *hi)
{
	spin_unlock(&hi->lock);
}

static inline void __cs_hsi_error_read_bits(unsigned int *state)
{
	*state |= SSI_CHANNEL_STATE_ERROR;
	*state &= ~(SSI_CHANNEL_STATE_READING | SSI_CHANNEL_STATE_POLL);
}

static inline void __cs_hsi_error_write_bits(unsigned int *state)
{
	*state |= SSI_CHANNEL_STATE_ERROR;
	*state &= ~SSI_CHANNEL_STATE_WRITING;
}

static void cs_hsi_control_read_error(struct cs_hsi_iface *hi,
							struct hsi_msg *msg)
{
	__cs_hsi_error_pre(hi, msg, "control read", &hi->control_state);
	cs_release_cmd(msg);
	__cs_hsi_error_read_bits(&hi->control_state);
	__cs_hsi_error_post(hi);
}

static void cs_hsi_control_write_error(struct cs_hsi_iface *hi,
							struct hsi_msg *msg)
{
	__cs_hsi_error_pre(hi, msg, "control write", &hi->control_state);
	cs_release_cmd(msg);
	__cs_hsi_error_write_bits(&hi->control_state);
	__cs_hsi_error_post(hi);

}

static void cs_hsi_data_read_error(struct cs_hsi_iface *hi, struct hsi_msg *msg)
{
	__cs_hsi_error_pre(hi, msg, "data read", &hi->data_state);
	__cs_hsi_error_read_bits(&hi->data_state);
	__cs_hsi_error_post(hi);
}

static void cs_hsi_data_write_error(struct cs_hsi_iface *hi,
							struct hsi_msg *msg)
{
	__cs_hsi_error_pre(hi, msg, "data write", &hi->data_state);
	__cs_hsi_error_write_bits(&hi->data_state);
	__cs_hsi_error_post(hi);
}

static void cs_hsi_read_on_control_complete(struct hsi_msg *msg)
{
	u32 cmd = cs_get_cmd(msg);
	struct cs_hsi_iface *hi = msg->context;

	spin_lock(&hi->lock);
	hi->control_state &= ~SSI_CHANNEL_STATE_READING;
	if (msg->status == HSI_STATUS_ERROR) {
		dev_err(&hi->cl->device, "Control RX error detected\n");
		spin_unlock(&hi->lock);
		cs_hsi_control_read_error(hi, msg);
		goto out;
	}
	dev_dbg(&hi->cl->device, "Read on control: %08X\n", cmd);
	cs_release_cmd(msg);
	if (hi->flags & CS_FEAT_TSTAMP_RX_CTRL) {
		struct timespec tspec;
		struct cs_timestamp *tstamp =
			&hi->mmap_cfg->tstamp_rx_ctrl;

		ktime_get_ts(&tspec);

		tstamp->tv_sec = (__u32) tspec.tv_sec;
		tstamp->tv_nsec = (__u32) tspec.tv_nsec;
	}
	spin_unlock(&hi->lock);

	cs_notify_control(cmd);

out:
	cs_hsi_read_on_control(hi);
}

static void cs_hsi_peek_on_control_complete(struct hsi_msg *msg)
{
	struct cs_hsi_iface *hi = msg->context;
	int ret;

	if (msg->status == HSI_STATUS_ERROR) {
		dev_err(&hi->cl->device, "Control peek RX error detected\n");
		cs_hsi_control_read_error(hi, msg);
		return;
	}

	WARN_ON(!(hi->control_state & SSI_CHANNEL_STATE_READING));

	dev_dbg(&hi->cl->device, "Peek on control complete, reading\n");
	msg->sgt.nents = 1;
	msg->complete = cs_hsi_read_on_control_complete;
	ret = hsi_async_read(hi->cl, msg);
	if (ret)
		cs_hsi_control_read_error(hi, msg);
}

static void cs_hsi_read_on_control(struct cs_hsi_iface *hi)
{
	struct hsi_msg *msg;
	int ret;

	spin_lock(&hi->lock);
	if (hi->control_state & SSI_CHANNEL_STATE_READING) {
		dev_err(&hi->cl->device, "Control read already pending (%d)\n",
			hi->control_state);
		spin_unlock(&hi->lock);
		return;
	}
	if (hi->control_state & SSI_CHANNEL_STATE_ERROR) {
		dev_err(&hi->cl->device, "Control read error (%d)\n",
			hi->control_state);
		spin_unlock(&hi->lock);
		return;
	}
	hi->control_state |= SSI_CHANNEL_STATE_READING;
	dev_dbg(&hi->cl->device, "Issuing RX on control\n");
	msg = cs_claim_cmd(hi);
	spin_unlock(&hi->lock);

	msg->sgt.nents = 0;
	msg->complete = cs_hsi_peek_on_control_complete;
	ret = hsi_async_read(hi->cl, msg);
	if (ret)
		cs_hsi_control_read_error(hi, msg);
}

static void cs_hsi_write_on_control_complete(struct hsi_msg *msg)
{
	struct cs_hsi_iface *hi = msg->context;
	if (msg->status == HSI_STATUS_COMPLETED) {
		spin_lock(&hi->lock);
		hi->control_state &= ~SSI_CHANNEL_STATE_WRITING;
		cs_release_cmd(msg);
		spin_unlock(&hi->lock);
	} else if (msg->status == HSI_STATUS_ERROR) {
		cs_hsi_control_write_error(hi, msg);
	} else {
		dev_err(&hi->cl->device,
			"unexpected status in control write callback %d\n",
			msg->status);
	}
}

static int cs_hsi_write_on_control(struct cs_hsi_iface *hi, u32 message)
{
	struct hsi_msg *msg;
	int ret;

	spin_lock(&hi->lock);
	if (hi->control_state & SSI_CHANNEL_STATE_ERROR) {
		spin_unlock(&hi->lock);
		return -EIO;
	}
	if (hi->control_state & SSI_CHANNEL_STATE_WRITING) {
		dev_err(&hi->cl->device,
			"Write still pending on control channel.\n");
		spin_unlock(&hi->lock);
		return -EBUSY;
	}
	hi->control_state |= SSI_CHANNEL_STATE_WRITING;
	msg = cs_claim_cmd(hi);
	spin_unlock(&hi->lock);

	cs_set_cmd(msg, message);
	msg->sgt.nents = 1;
	msg->complete = cs_hsi_write_on_control_complete;
	dev_dbg(&hi->cl->device,
		"Sending control message %08X\n", message);
	ret = hsi_async_write(hi->cl, msg);
	if (ret) {
		dev_err(&hi->cl->device,
			"async_write failed with %d\n", ret);
		cs_hsi_control_write_error(hi, msg);
	}

	/*
	 * Make sure control read is always pending when issuing
	 * new control writes. This is needed as the controller
	 * may flush our messages if e.g. the peer device reboots
	 * unexpectedly (and we cannot directly resubmit a new read from
	 * the message destructor; see cs_cmd_destructor()).
	 */
	if (!(hi->control_state & SSI_CHANNEL_STATE_READING)) {
		dev_err(&hi->cl->device, "Restarting control reads\n");
		cs_hsi_read_on_control(hi);
	}

	return 0;
}

static void cs_hsi_read_on_data_complete(struct hsi_msg *msg)
{
	struct cs_hsi_iface *hi = msg->context;
	u32 payload;

	if (unlikely(msg->status == HSI_STATUS_ERROR)) {
		cs_hsi_data_read_error(hi, msg);
		return;
	}

	spin_lock(&hi->lock);
	WARN_ON(!(hi->data_state & SSI_CHANNEL_STATE_READING));
	hi->data_state &= ~SSI_CHANNEL_STATE_READING;
	payload = CS_RX_DATA_RECEIVED;
	payload |= hi->rx_slot;
	hi->rx_slot++;
	hi->rx_slot %= hi->rx_ptr_boundary;
	/* expose current rx ptr in mmap area */
	hi->mmap_cfg->rx_ptr = hi->rx_slot;
	if (unlikely(waitqueue_active(&hi->datawait)))
		wake_up_interruptible(&hi->datawait);
	spin_unlock(&hi->lock);

	cs_notify_data(payload, hi->rx_bufs);
	cs_hsi_read_on_data(hi);
}

static void cs_hsi_peek_on_data_complete(struct hsi_msg *msg)
{
	struct cs_hsi_iface *hi = msg->context;
	u32 *address;
	int ret;

	if (unlikely(msg->status == HSI_STATUS_ERROR)) {
		cs_hsi_data_read_error(hi, msg);
		return;
	}
	if (unlikely(hi->iface_state != CS_STATE_CONFIGURED)) {
		dev_err(&hi->cl->device, "Data received in invalid state\n");
		cs_hsi_data_read_error(hi, msg);
		return;
	}

	spin_lock(&hi->lock);
	WARN_ON(!(hi->data_state & SSI_CHANNEL_STATE_POLL));
	hi->data_state &= ~SSI_CHANNEL_STATE_POLL;
	hi->data_state |= SSI_CHANNEL_STATE_READING;
	spin_unlock(&hi->lock);

	address = (u32 *)(hi->mmap_base +
				hi->rx_offsets[hi->rx_slot % hi->rx_bufs]);
	sg_init_one(msg->sgt.sgl, address, hi->buf_size);
	msg->sgt.nents = 1;
	msg->complete = cs_hsi_read_on_data_complete;
	ret = hsi_async_read(hi->cl, msg);
	if (ret)
		cs_hsi_data_read_error(hi, msg);
}

/*
 * Read/write transaction is ongoing. Returns false if in
 * SSI_CHANNEL_STATE_POLL state.
 */
static inline int cs_state_xfer_active(unsigned int state)
{
	return (state & SSI_CHANNEL_STATE_WRITING) ||
		(state & SSI_CHANNEL_STATE_READING);
}

/*
 * No pending read/writes
 */
static inline int cs_state_idle(unsigned int state)
{
	return !(state & ~SSI_CHANNEL_STATE_ERROR);
}

static void cs_hsi_read_on_data(struct cs_hsi_iface *hi)
{
	struct hsi_msg *rxmsg;
	int ret;

	spin_lock(&hi->lock);
	if (hi->data_state &
		(SSI_CHANNEL_STATE_READING | SSI_CHANNEL_STATE_POLL)) {
		dev_dbg(&hi->cl->device, "Data read already pending (%u)\n",
			hi->data_state);
		spin_unlock(&hi->lock);
		return;
	}
	hi->data_state |= SSI_CHANNEL_STATE_POLL;
	spin_unlock(&hi->lock);

	rxmsg = hi->data_rx_msg;
	sg_init_one(rxmsg->sgt.sgl, (void *)hi->mmap_base, 0);
	rxmsg->sgt.nents = 0;
	rxmsg->complete = cs_hsi_peek_on_data_complete;

	ret = hsi_async_read(hi->cl, rxmsg);
	if (ret)
		cs_hsi_data_read_error(hi, rxmsg);
}

static void cs_hsi_write_on_data_complete(struct hsi_msg *msg)
{
	struct cs_hsi_iface *hi = msg->context;

	if (msg->status == HSI_STATUS_COMPLETED) {
		spin_lock(&hi->lock);
		hi->data_state &= ~SSI_CHANNEL_STATE_WRITING;
		if (unlikely(waitqueue_active(&hi->datawait)))
			wake_up_interruptible(&hi->datawait);
		spin_unlock(&hi->lock);
	} else {
		cs_hsi_data_write_error(hi, msg);
	}
}

static int cs_hsi_write_on_data(struct cs_hsi_iface *hi, unsigned int slot)
{
	u32 *address;
	struct hsi_msg *txmsg;
	int ret;

	spin_lock(&hi->lock);
	if (hi->iface_state != CS_STATE_CONFIGURED) {
		dev_err(&hi->cl->device, "Not configured, aborting\n");
		ret = -EINVAL;
		goto error;
	}
	if (hi->data_state & SSI_CHANNEL_STATE_ERROR) {
		dev_err(&hi->cl->device, "HSI error, aborting\n");
		ret = -EIO;
		goto error;
	}
	if (hi->data_state & SSI_CHANNEL_STATE_WRITING) {
		dev_err(&hi->cl->device, "Write pending on data channel.\n");
		ret = -EBUSY;
		goto error;
	}
	hi->data_state |= SSI_CHANNEL_STATE_WRITING;
	spin_unlock(&hi->lock);

	hi->tx_slot = slot;
	address = (u32 *)(hi->mmap_base + hi->tx_offsets[hi->tx_slot]);
	txmsg = hi->data_tx_msg;
	sg_init_one(txmsg->sgt.sgl, address, hi->buf_size);
	txmsg->complete = cs_hsi_write_on_data_complete;
	ret = hsi_async_write(hi->cl, txmsg);
	if (ret)
		cs_hsi_data_write_error(hi, txmsg);

	return ret;

error:
	spin_unlock(&hi->lock);
	if (ret == -EIO)
		cs_hsi_data_write_error(hi, hi->data_tx_msg);

	return ret;
}

static unsigned int cs_hsi_get_state(struct cs_hsi_iface *hi)
{
	return hi->iface_state;
}

static int cs_hsi_command(struct cs_hsi_iface *hi, u32 cmd)
{
	int ret = 0;

	local_bh_disable();
	switch (cmd & TARGET_MASK) {
	case TARGET_REMOTE:
		ret = cs_hsi_write_on_control(hi, cmd);
		break;
	case TARGET_LOCAL:
		if ((cmd & CS_CMD_MASK) == CS_TX_DATA_READY)
			ret = cs_hsi_write_on_data(hi, cmd & CS_PARAM_MASK);
		else
			ret = -EINVAL;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	local_bh_enable();

	return ret;
}

static void cs_hsi_set_wakeline(struct cs_hsi_iface *hi, bool new_state)
{
	int change = 0;

	spin_lock_bh(&hi->lock);
	if (hi->wakeline_state != new_state) {
		hi->wakeline_state = new_state;
		change = 1;
		dev_dbg(&hi->cl->device, "setting wake line to %d (%p)\n",
			new_state, hi->cl);
	}
	spin_unlock_bh(&hi->lock);

	if (change) {
		if (new_state)
			ssip_slave_start_tx(hi->master);
		else
			ssip_slave_stop_tx(hi->master);
	}

	dev_dbg(&hi->cl->device, "wake line set to %d (%p)\n",
		new_state, hi->cl);
}

static void set_buffer_sizes(struct cs_hsi_iface *hi, int rx_bufs, int tx_bufs)
{
	hi->rx_bufs = rx_bufs;
	hi->tx_bufs = tx_bufs;
	hi->mmap_cfg->rx_bufs = rx_bufs;
	hi->mmap_cfg->tx_bufs = tx_bufs;

	if (hi->flags & CS_FEAT_ROLLING_RX_COUNTER) {
		/*
		 * For more robust overrun detection, let the rx
		 * pointer run in range 0..'boundary-1'. Boundary
		 * is a multiple of rx_bufs, and limited in max size
		 * by RX_PTR_MAX_SHIFT to allow for fast ptr-diff
		 * calculation.
		 */
		hi->rx_ptr_boundary = (rx_bufs << RX_PTR_BOUNDARY_SHIFT);
		hi->mmap_cfg->rx_ptr_boundary = hi->rx_ptr_boundary;
	} else {
		hi->rx_ptr_boundary = hi->rx_bufs;
	}
}

static int check_buf_params(struct cs_hsi_iface *hi,
					const struct cs_buffer_config *buf_cfg)
{
	size_t buf_size_aligned = L1_CACHE_ALIGN(buf_cfg->buf_size) *
					(buf_cfg->rx_bufs + buf_cfg->tx_bufs);
	size_t ctrl_size_aligned = L1_CACHE_ALIGN(sizeof(*hi->mmap_cfg));
	int r = 0;

	if (buf_cfg->rx_bufs > CS_MAX_BUFFERS ||
					buf_cfg->tx_bufs > CS_MAX_BUFFERS) {
		r = -EINVAL;
	} else if ((buf_size_aligned + ctrl_size_aligned) >= hi->mmap_size) {
		dev_err(&hi->cl->device, "No space for the requested buffer "
			"configuration\n");
		r = -ENOBUFS;
	}

	return r;
}

/**
 * Block until pending data transfers have completed.
 */
static int cs_hsi_data_sync(struct cs_hsi_iface *hi)
{
	int r = 0;

	spin_lock_bh(&hi->lock);

	if (!cs_state_xfer_active(hi->data_state)) {
		dev_dbg(&hi->cl->device, "hsi_data_sync break, idle\n");
		goto out;
	}

	for (;;) {
		int s;
		DEFINE_WAIT(wait);
		if (!cs_state_xfer_active(hi->data_state))
			goto out;
		if (signal_pending(current)) {
			r = -ERESTARTSYS;
			goto out;
		}
		/**
		 * prepare_to_wait must be called with hi->lock held
		 * so that callbacks can check for waitqueue_active()
		 */
		prepare_to_wait(&hi->datawait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_bh(&hi->lock);
		s = schedule_timeout(
			msecs_to_jiffies(CS_HSI_TRANSFER_TIMEOUT_MS));
		spin_lock_bh(&hi->lock);
		finish_wait(&hi->datawait, &wait);
		if (!s) {
			dev_dbg(&hi->cl->device,
				"hsi_data_sync timeout after %d ms\n",
				CS_HSI_TRANSFER_TIMEOUT_MS);
			r = -EIO;
			goto out;
		}
	}

out:
	spin_unlock_bh(&hi->lock);
	dev_dbg(&hi->cl->device, "hsi_data_sync done with res %d\n", r);

	return r;
}

static void cs_hsi_data_enable(struct cs_hsi_iface *hi,
					struct cs_buffer_config *buf_cfg)
{
	unsigned int data_start, i;

	BUG_ON(hi->buf_size == 0);

	set_buffer_sizes(hi, buf_cfg->rx_bufs, buf_cfg->tx_bufs);

	hi->slot_size = L1_CACHE_ALIGN(hi->buf_size);
	dev_dbg(&hi->cl->device,
			"setting slot size to %u, buf size %u, align %u\n",
			hi->slot_size, hi->buf_size, L1_CACHE_BYTES);

	data_start = L1_CACHE_ALIGN(sizeof(*hi->mmap_cfg));
	dev_dbg(&hi->cl->device,
			"setting data start at %u, cfg block %u, align %u\n",
			data_start, sizeof(*hi->mmap_cfg), L1_CACHE_BYTES);

	for (i = 0; i < hi->mmap_cfg->rx_bufs; i++) {
		hi->rx_offsets[i] = data_start + i * hi->slot_size;
		hi->mmap_cfg->rx_offsets[i] = hi->rx_offsets[i];
		dev_dbg(&hi->cl->device, "DL buf #%u at %u\n",
					i, hi->rx_offsets[i]);
	}
	for (i = 0; i < hi->mmap_cfg->tx_bufs; i++) {
		hi->tx_offsets[i] = data_start +
			(i + hi->mmap_cfg->rx_bufs) * hi->slot_size;
		hi->mmap_cfg->tx_offsets[i] = hi->tx_offsets[i];
		dev_dbg(&hi->cl->device, "UL buf #%u at %u\n",
					i, hi->rx_offsets[i]);
	}

	hi->iface_state = CS_STATE_CONFIGURED;
}

static void cs_hsi_data_disable(struct cs_hsi_iface *hi, int old_state)
{
	if (old_state == CS_STATE_CONFIGURED) {
		dev_dbg(&hi->cl->device,
			"closing data channel with slot size 0\n");
		hi->iface_state = CS_STATE_OPENED;
	}
}

static int cs_hsi_buf_config(struct cs_hsi_iface *hi,
					struct cs_buffer_config *buf_cfg)
{
	int r = 0;
	unsigned int old_state = hi->iface_state;

	spin_lock_bh(&hi->lock);
	/* Prevent new transactions during buffer reconfig */
	if (old_state == CS_STATE_CONFIGURED)
		hi->iface_state = CS_STATE_OPENED;
	spin_unlock_bh(&hi->lock);

	/*
	 * make sure that no non-zero data reads are ongoing before
	 * proceeding to change the buffer layout
	 */
	r = cs_hsi_data_sync(hi);
	if (r < 0)
		return r;

	WARN_ON(cs_state_xfer_active(hi->data_state));

	spin_lock_bh(&hi->lock);
	r = check_buf_params(hi, buf_cfg);
	if (r < 0)
		goto error;

	hi->buf_size = buf_cfg->buf_size;
	hi->mmap_cfg->buf_size = hi->buf_size;
	hi->flags = buf_cfg->flags;

	hi->rx_slot = 0;
	hi->tx_slot = 0;
	hi->slot_size = 0;

	if (hi->buf_size)
		cs_hsi_data_enable(hi, buf_cfg);
	else
		cs_hsi_data_disable(hi, old_state);

	spin_unlock_bh(&hi->lock);

	if (old_state != hi->iface_state) {
		if (hi->iface_state == CS_STATE_CONFIGURED) {
			pm_qos_add_request(&hi->pm_qos_req,
				PM_QOS_CPU_DMA_LATENCY,
				CS_QOS_LATENCY_FOR_DATA_USEC);
			local_bh_disable();
			cs_hsi_read_on_data(hi);
			local_bh_enable();
		} else if (old_state == CS_STATE_CONFIGURED) {
			pm_qos_remove_request(&hi->pm_qos_req);
		}
	}
	return r;

error:
	spin_unlock_bh(&hi->lock);
	return r;
}

static int cs_hsi_start(struct cs_hsi_iface **hi, struct hsi_client *cl,
			unsigned long mmap_base, unsigned long mmap_size)
{
	int err = 0;
	struct cs_hsi_iface *hsi_if = kzalloc(sizeof(*hsi_if), GFP_KERNEL);

	dev_dbg(&cl->device, "cs_hsi_start\n");

	if (!hsi_if) {
		err = -ENOMEM;
		goto leave0;
	}
	spin_lock_init(&hsi_if->lock);
	hsi_if->cl = cl;
	hsi_if->iface_state = CS_STATE_CLOSED;
	hsi_if->mmap_cfg = (struct cs_mmap_config_block *)mmap_base;
	hsi_if->mmap_base = mmap_base;
	hsi_if->mmap_size = mmap_size;
	memset(hsi_if->mmap_cfg, 0, sizeof(*hsi_if->mmap_cfg));
	init_waitqueue_head(&hsi_if->datawait);
	err = cs_alloc_cmds(hsi_if);
	if (err < 0) {
		dev_err(&cl->device, "Unable to alloc HSI messages\n");
		goto leave1;
	}
	err = cs_hsi_alloc_data(hsi_if);
	if (err < 0) {
		dev_err(&cl->device, "Unable to alloc HSI messages for data\n");
		goto leave2;
	}
	err = hsi_claim_port(cl, 1);
	if (err < 0) {
		dev_err(&cl->device,
				"Could not open, HSI port already claimed\n");
		goto leave3;
	}
	hsi_if->master = ssip_slave_get_master(cl);
	if (IS_ERR(hsi_if->master)) {
		err = PTR_ERR(hsi_if->master);
		dev_err(&cl->device, "Could not get HSI master client\n");
		goto leave4;
	}
	if (!ssip_slave_running(hsi_if->master)) {
		err = -ENODEV;
		dev_err(&cl->device,
				"HSI port not initialized\n");
		goto leave4;
	}

	hsi_if->iface_state = CS_STATE_OPENED;
	local_bh_disable();
	cs_hsi_read_on_control(hsi_if);
	local_bh_enable();

	dev_dbg(&cl->device, "cs_hsi_start...done\n");

	BUG_ON(!hi);
	*hi = hsi_if;

	return 0;

leave4:
	hsi_release_port(cl);
leave3:
	cs_hsi_free_data(hsi_if);
leave2:
	cs_free_cmds(hsi_if);
leave1:
	kfree(hsi_if);
leave0:
	dev_dbg(&cl->device, "cs_hsi_start...done/error\n\n");

	return err;
}

static void cs_hsi_stop(struct cs_hsi_iface *hi)
{
	dev_dbg(&hi->cl->device, "cs_hsi_stop\n");
	cs_hsi_set_wakeline(hi, 0);
	ssip_slave_put_master(hi->master);

	/* hsi_release_port() needs to be called with CS_STATE_CLOSED */
	hi->iface_state = CS_STATE_CLOSED;
	hsi_release_port(hi->cl);

	/*
	 * hsi_release_port() should flush out all the pending
	 * messages, so cs_state_idle() should be true for both
	 * control and data channels.
	 */
	WARN_ON(!cs_state_idle(hi->control_state));
	WARN_ON(!cs_state_idle(hi->data_state));

	if (pm_qos_request_active(&hi->pm_qos_req))
		pm_qos_remove_request(&hi->pm_qos_req);

	spin_lock_bh(&hi->lock);
	cs_hsi_free_data(hi);
	cs_free_cmds(hi);
	spin_unlock_bh(&hi->lock);
	kfree(hi);
}

static int cs_char_vma_fault(struct vm_fault *vmf)
{
	struct cs_char *csdata = vmf->vma->vm_private_data;
	struct page *page;

	page = virt_to_page(csdata->mmap_base);
	get_page(page);
	vmf->page = page;

	return 0;
}

static const struct vm_operations_struct cs_char_vm_ops = {
	.fault	= cs_char_vma_fault,
};

static int cs_char_fasync(int fd, struct file *file, int on)
{
	struct cs_char *csdata = file->private_data;

	if (fasync_helper(fd, file, on, &csdata->async_queue) < 0)
		return -EIO;

	return 0;
}

static __poll_t cs_char_poll(struct file *file, poll_table *wait)
{
	struct cs_char *csdata = file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &cs_char_data.wait, wait);
	spin_lock_bh(&csdata->lock);
	if (!list_empty(&csdata->chardev_queue))
		ret = POLLIN | POLLRDNORM;
	else if (!list_empty(&csdata->dataind_queue))
		ret = POLLIN | POLLRDNORM;
	spin_unlock_bh(&csdata->lock);

	return ret;
}

static ssize_t cs_char_read(struct file *file, char __user *buf, size_t count,
								loff_t *unused)
{
	struct cs_char *csdata = file->private_data;
	u32 data;
	ssize_t retval;

	if (count < sizeof(data))
		return -EINVAL;

	for (;;) {
		DEFINE_WAIT(wait);

		spin_lock_bh(&csdata->lock);
		if (!list_empty(&csdata->chardev_queue)) {
			data = cs_pop_entry(&csdata->chardev_queue);
		} else if (!list_empty(&csdata->dataind_queue)) {
			data = cs_pop_entry(&csdata->dataind_queue);
			csdata->dataind_pending--;
		} else {
			data = 0;
		}
		spin_unlock_bh(&csdata->lock);

		if (data)
			break;
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		} else if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}
		prepare_to_wait_exclusive(&csdata->wait, &wait,
						TASK_INTERRUPTIBLE);
		schedule();
		finish_wait(&csdata->wait, &wait);
	}

	retval = put_user(data, (u32 __user *)buf);
	if (!retval)
		retval = sizeof(data);

out:
	return retval;
}

static ssize_t cs_char_write(struct file *file, const char __user *buf,
						size_t count, loff_t *unused)
{
	struct cs_char *csdata = file->private_data;
	u32 data;
	int err;
	ssize_t	retval;

	if (count < sizeof(data))
		return -EINVAL;

	if (get_user(data, (u32 __user *)buf))
		retval = -EFAULT;
	else
		retval = count;

	err = cs_hsi_command(csdata->hi, data);
	if (err < 0)
		retval = err;

	return retval;
}

static long cs_char_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct cs_char *csdata = file->private_data;
	int r = 0;

	switch (cmd) {
	case CS_GET_STATE: {
		unsigned int state;

		state = cs_hsi_get_state(csdata->hi);
		if (copy_to_user((void __user *)arg, &state, sizeof(state)))
			r = -EFAULT;

		break;
	}
	case CS_SET_WAKELINE: {
		unsigned int state;

		if (copy_from_user(&state, (void __user *)arg, sizeof(state))) {
			r = -EFAULT;
			break;
		}

		if (state > 1) {
			r = -EINVAL;
			break;
		}

		cs_hsi_set_wakeline(csdata->hi, !!state);

		break;
	}
	case CS_GET_IF_VERSION: {
		unsigned int ifver = CS_IF_VERSION;

		if (copy_to_user((void __user *)arg, &ifver, sizeof(ifver)))
			r = -EFAULT;

		break;
	}
	case CS_CONFIG_BUFS: {
		struct cs_buffer_config buf_cfg;

		if (copy_from_user(&buf_cfg, (void __user *)arg,
							sizeof(buf_cfg)))
			r = -EFAULT;
		else
			r = cs_hsi_buf_config(csdata->hi, &buf_cfg);

		break;
	}
	default:
		r = -ENOTTY;
		break;
	}

	return r;
}

static int cs_char_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (vma->vm_end < vma->vm_start)
		return -EINVAL;

	if (vma_pages(vma) != 1)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_DONTDUMP | VM_DONTEXPAND;
	vma->vm_ops = &cs_char_vm_ops;
	vma->vm_private_data = file->private_data;

	return 0;
}

static int cs_char_open(struct inode *unused, struct file *file)
{
	int ret = 0;
	unsigned long p;

	spin_lock_bh(&cs_char_data.lock);
	if (cs_char_data.opened) {
		ret = -EBUSY;
		spin_unlock_bh(&cs_char_data.lock);
		goto out1;
	}
	cs_char_data.opened = 1;
	cs_char_data.dataind_pending = 0;
	spin_unlock_bh(&cs_char_data.lock);

	p = get_zeroed_page(GFP_KERNEL);
	if (!p) {
		ret = -ENOMEM;
		goto out2;
	}

	ret = cs_hsi_start(&cs_char_data.hi, cs_char_data.cl, p, CS_MMAP_SIZE);
	if (ret) {
		dev_err(&cs_char_data.cl->device, "Unable to initialize HSI\n");
		goto out3;
	}

	/* these are only used in release so lock not needed */
	cs_char_data.mmap_base = p;
	cs_char_data.mmap_size = CS_MMAP_SIZE;

	file->private_data = &cs_char_data;

	return 0;

out3:
	free_page(p);
out2:
	spin_lock_bh(&cs_char_data.lock);
	cs_char_data.opened = 0;
	spin_unlock_bh(&cs_char_data.lock);
out1:
	return ret;
}

static void cs_free_char_queue(struct list_head *head)
{
	struct char_queue *entry;
	struct list_head *cursor, *next;

	if (!list_empty(head)) {
		list_for_each_safe(cursor, next, head) {
			entry = list_entry(cursor, struct char_queue, list);
			list_del(&entry->list);
			kfree(entry);
		}
	}

}

static int cs_char_release(struct inode *unused, struct file *file)
{
	struct cs_char *csdata = file->private_data;

	cs_hsi_stop(csdata->hi);
	spin_lock_bh(&csdata->lock);
	csdata->hi = NULL;
	free_page(csdata->mmap_base);
	cs_free_char_queue(&csdata->chardev_queue);
	cs_free_char_queue(&csdata->dataind_queue);
	csdata->opened = 0;
	spin_unlock_bh(&csdata->lock);

	return 0;
}

static const struct file_operations cs_char_fops = {
	.owner		= THIS_MODULE,
	.read		= cs_char_read,
	.write		= cs_char_write,
	.poll		= cs_char_poll,
	.unlocked_ioctl	= cs_char_ioctl,
	.mmap		= cs_char_mmap,
	.open		= cs_char_open,
	.release	= cs_char_release,
	.fasync		= cs_char_fasync,
};

static struct miscdevice cs_char_miscdev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "cmt_speech",
	.fops	= &cs_char_fops
};

static int cs_hsi_client_probe(struct device *dev)
{
	int err = 0;
	struct hsi_client *cl = to_hsi_client(dev);

	dev_dbg(dev, "hsi_client_probe\n");
	init_waitqueue_head(&cs_char_data.wait);
	spin_lock_init(&cs_char_data.lock);
	cs_char_data.opened = 0;
	cs_char_data.cl = cl;
	cs_char_data.hi = NULL;
	INIT_LIST_HEAD(&cs_char_data.chardev_queue);
	INIT_LIST_HEAD(&cs_char_data.dataind_queue);

	cs_char_data.channel_id_cmd = hsi_get_channel_id_by_name(cl,
		"speech-control");
	if (cs_char_data.channel_id_cmd < 0) {
		err = cs_char_data.channel_id_cmd;
		dev_err(dev, "Could not get cmd channel (%d)\n", err);
		return err;
	}

	cs_char_data.channel_id_data = hsi_get_channel_id_by_name(cl,
		"speech-data");
	if (cs_char_data.channel_id_data < 0) {
		err = cs_char_data.channel_id_data;
		dev_err(dev, "Could not get data channel (%d)\n", err);
		return err;
	}

	err = misc_register(&cs_char_miscdev);
	if (err)
		dev_err(dev, "Failed to register: %d\n", err);

	return err;
}

static int cs_hsi_client_remove(struct device *dev)
{
	struct cs_hsi_iface *hi;

	dev_dbg(dev, "hsi_client_remove\n");
	misc_deregister(&cs_char_miscdev);
	spin_lock_bh(&cs_char_data.lock);
	hi = cs_char_data.hi;
	cs_char_data.hi = NULL;
	spin_unlock_bh(&cs_char_data.lock);
	if (hi)
		cs_hsi_stop(hi);

	return 0;
}

static struct hsi_client_driver cs_hsi_driver = {
	.driver = {
		.name	= "cmt-speech",
		.owner	= THIS_MODULE,
		.probe	= cs_hsi_client_probe,
		.remove	= cs_hsi_client_remove,
	},
};

static int __init cs_char_init(void)
{
	pr_info("CMT speech driver added\n");
	return hsi_register_client_driver(&cs_hsi_driver);
}
module_init(cs_char_init);

static void __exit cs_char_exit(void)
{
	hsi_unregister_client_driver(&cs_hsi_driver);
	pr_info("CMT speech driver removed\n");
}
module_exit(cs_char_exit);

MODULE_ALIAS("hsi:cmt-speech");
MODULE_AUTHOR("Kai Vehmanen <kai.vehmanen@nokia.com>");
MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@nokia.com>");
MODULE_DESCRIPTION("CMT speech driver");
MODULE_LICENSE("GPL v2");
