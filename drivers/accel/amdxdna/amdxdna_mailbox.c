// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#include <drm/drm_device.h>
#include <drm/drm_managed.h>
#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>

#define CREATE_TRACE_POINTS
#include <trace/events/amdxdna.h>

#include "amdxdna_mailbox.h"

#define MB_ERR(chann, fmt, args...) \
({ \
	typeof(chann) _chann = chann; \
	dev_err((_chann)->mb->dev, "xdna_mailbox.%d: "fmt, \
		(_chann)->msix_irq, ##args); \
})
#define MB_DBG(chann, fmt, args...) \
({ \
	typeof(chann) _chann = chann; \
	dev_dbg((_chann)->mb->dev, "xdna_mailbox.%d: "fmt, \
		(_chann)->msix_irq, ##args); \
})
#define MB_WARN_ONCE(chann, fmt, args...) \
({ \
	typeof(chann) _chann = chann; \
	dev_warn_once((_chann)->mb->dev, "xdna_mailbox.%d: "fmt, \
		      (_chann)->msix_irq, ##args); \
})

#define MAGIC_VAL			0x1D000000U
#define MAGIC_VAL_MASK			0xFF000000
#define MAX_MSG_ID_ENTRIES		256
#define MSG_RX_TIMER			200 /* milliseconds */
#define MAILBOX_NAME			"xdna_mailbox"

enum channel_res_type {
	CHAN_RES_X2I,
	CHAN_RES_I2X,
	CHAN_RES_NUM
};

struct mailbox {
	struct device		*dev;
	struct xdna_mailbox_res	res;
};

struct mailbox_channel {
	struct mailbox			*mb;
	struct xdna_mailbox_chann_res	res[CHAN_RES_NUM];
	int				msix_irq;
	u32				iohub_int_addr;
	struct idr			chan_idr;
	spinlock_t			chan_idr_lock; /* protect chan_idr */
	u32				x2i_tail;

	/* Received msg related fields */
	struct workqueue_struct		*work_q;
	struct work_struct		rx_work;
	u32				i2x_head;
	bool				bad_state;
};

#define MSG_BODY_SZ		GENMASK(10, 0)
#define MSG_PROTO_VER		GENMASK(23, 16)
struct xdna_msg_header {
	__u32 total_size;
	__u32 sz_ver;
	__u32 id;
	__u32 opcode;
} __packed;

static_assert(sizeof(struct xdna_msg_header) == 16);

struct mailbox_pkg {
	struct xdna_msg_header	header;
	__u32			payload[];
};

/* The protocol version. */
#define MSG_PROTOCOL_VERSION	0x1
/* The tombstone value. */
#define TOMBSTONE		0xDEADFACE

struct mailbox_msg {
	void			*handle;
	int			(*notify_cb)(void *handle, const u32 *data, size_t size);
	size_t			pkg_size; /* package size in bytes */
	struct mailbox_pkg	pkg;
};

static void mailbox_reg_write(struct mailbox_channel *mb_chann, u32 mbox_reg, u32 data)
{
	struct xdna_mailbox_res *mb_res = &mb_chann->mb->res;
	u64 ringbuf_addr = mb_res->mbox_base + mbox_reg;

	writel(data, (void *)ringbuf_addr);
}

static u32 mailbox_reg_read(struct mailbox_channel *mb_chann, u32 mbox_reg)
{
	struct xdna_mailbox_res *mb_res = &mb_chann->mb->res;
	u64 ringbuf_addr = mb_res->mbox_base + mbox_reg;

	return readl((void *)ringbuf_addr);
}

static int mailbox_reg_read_non_zero(struct mailbox_channel *mb_chann, u32 mbox_reg, u32 *val)
{
	struct xdna_mailbox_res *mb_res = &mb_chann->mb->res;
	u64 ringbuf_addr = mb_res->mbox_base + mbox_reg;
	int ret, value;

	/* Poll till value is not zero */
	ret = readx_poll_timeout(readl, (void *)ringbuf_addr, value,
				 value, 1 /* us */, 100);
	if (ret < 0)
		return ret;

	*val = value;
	return 0;
}

static inline void
mailbox_set_headptr(struct mailbox_channel *mb_chann, u32 headptr_val)
{
	mailbox_reg_write(mb_chann, mb_chann->res[CHAN_RES_I2X].mb_head_ptr_reg, headptr_val);
	mb_chann->i2x_head = headptr_val;
}

static inline void
mailbox_set_tailptr(struct mailbox_channel *mb_chann, u32 tailptr_val)
{
	mailbox_reg_write(mb_chann, mb_chann->res[CHAN_RES_X2I].mb_tail_ptr_reg, tailptr_val);
	mb_chann->x2i_tail = tailptr_val;
}

static inline u32
mailbox_get_headptr(struct mailbox_channel *mb_chann, enum channel_res_type type)
{
	return mailbox_reg_read(mb_chann, mb_chann->res[type].mb_head_ptr_reg);
}

static inline u32
mailbox_get_tailptr(struct mailbox_channel *mb_chann, enum channel_res_type type)
{
	return mailbox_reg_read(mb_chann, mb_chann->res[type].mb_tail_ptr_reg);
}

static inline u32
mailbox_get_ringbuf_size(struct mailbox_channel *mb_chann, enum channel_res_type type)
{
	return mb_chann->res[type].rb_size;
}

static inline int mailbox_validate_msgid(int msg_id)
{
	return (msg_id & MAGIC_VAL_MASK) == MAGIC_VAL;
}

static int mailbox_acquire_msgid(struct mailbox_channel *mb_chann, struct mailbox_msg *mb_msg)
{
	unsigned long flags;
	int msg_id;

	spin_lock_irqsave(&mb_chann->chan_idr_lock, flags);
	msg_id = idr_alloc_cyclic(&mb_chann->chan_idr, mb_msg, 0,
				  MAX_MSG_ID_ENTRIES, GFP_NOWAIT);
	spin_unlock_irqrestore(&mb_chann->chan_idr_lock, flags);
	if (msg_id < 0)
		return msg_id;

	/*
	 * The IDR becomes less efficient when dealing with larger IDs.
	 * Thus, add MAGIC_VAL to the higher bits.
	 */
	msg_id |= MAGIC_VAL;
	return msg_id;
}

static void mailbox_release_msgid(struct mailbox_channel *mb_chann, int msg_id)
{
	unsigned long flags;

	msg_id &= ~MAGIC_VAL_MASK;
	spin_lock_irqsave(&mb_chann->chan_idr_lock, flags);
	idr_remove(&mb_chann->chan_idr, msg_id);
	spin_unlock_irqrestore(&mb_chann->chan_idr_lock, flags);
}

static int mailbox_release_msg(int id, void *p, void *data)
{
	struct mailbox_channel *mb_chann = data;
	struct mailbox_msg *mb_msg = p;

	MB_DBG(mb_chann, "msg_id 0x%x msg opcode 0x%x",
	       mb_msg->pkg.header.id, mb_msg->pkg.header.opcode);
	mb_msg->notify_cb(mb_msg->handle, NULL, 0);
	kfree(mb_msg);

	return 0;
}

static int
mailbox_send_msg(struct mailbox_channel *mb_chann, struct mailbox_msg *mb_msg)
{
	u32 ringbuf_size;
	u32 head, tail;
	u32 start_addr;
	u64 write_addr;
	u32 tmp_tail;

	head = mailbox_get_headptr(mb_chann, CHAN_RES_X2I);
	tail = mb_chann->x2i_tail;
	ringbuf_size = mailbox_get_ringbuf_size(mb_chann, CHAN_RES_X2I);
	start_addr = mb_chann->res[CHAN_RES_X2I].rb_start_addr;
	tmp_tail = tail + mb_msg->pkg_size;

	if (tail < head && tmp_tail >= head)
		goto no_space;

	if (tail >= head && (tmp_tail > ringbuf_size - sizeof(u32) &&
			     mb_msg->pkg_size >= head))
		goto no_space;

	if (tail >= head && tmp_tail > ringbuf_size - sizeof(u32)) {
		write_addr = mb_chann->mb->res.ringbuf_base + start_addr + tail;
		writel(TOMBSTONE, (void *)write_addr);

		/* tombstone is set. Write from the start of the ringbuf */
		tail = 0;
	}

	write_addr = mb_chann->mb->res.ringbuf_base + start_addr + tail;
	memcpy_toio((void *)write_addr, &mb_msg->pkg, mb_msg->pkg_size);
	mailbox_set_tailptr(mb_chann, tail + mb_msg->pkg_size);

	trace_mbox_set_tail(MAILBOX_NAME, mb_chann->msix_irq,
			    mb_msg->pkg.header.opcode,
			    mb_msg->pkg.header.id);

	return 0;

no_space:
	return -ENOSPC;
}

static int
mailbox_get_resp(struct mailbox_channel *mb_chann, struct xdna_msg_header *header,
		 void *data)
{
	struct mailbox_msg *mb_msg;
	unsigned long flags;
	int msg_id;
	int ret;

	msg_id = header->id;
	if (!mailbox_validate_msgid(msg_id)) {
		MB_ERR(mb_chann, "Bad message ID 0x%x", msg_id);
		return -EINVAL;
	}

	msg_id &= ~MAGIC_VAL_MASK;
	spin_lock_irqsave(&mb_chann->chan_idr_lock, flags);
	mb_msg = idr_find(&mb_chann->chan_idr, msg_id);
	if (!mb_msg) {
		MB_ERR(mb_chann, "Cannot find msg 0x%x", msg_id);
		spin_unlock_irqrestore(&mb_chann->chan_idr_lock, flags);
		return -EINVAL;
	}
	idr_remove(&mb_chann->chan_idr, msg_id);
	spin_unlock_irqrestore(&mb_chann->chan_idr_lock, flags);

	MB_DBG(mb_chann, "opcode 0x%x size %d id 0x%x",
	       header->opcode, header->total_size, header->id);
	ret = mb_msg->notify_cb(mb_msg->handle, data, header->total_size);
	if (unlikely(ret))
		MB_ERR(mb_chann, "Message callback ret %d", ret);

	kfree(mb_msg);
	return ret;
}

static int mailbox_get_msg(struct mailbox_channel *mb_chann)
{
	struct xdna_msg_header header;
	u32 msg_size, rest;
	u32 ringbuf_size;
	u32 head, tail;
	u32 start_addr;
	u64 read_addr;
	int ret;

	if (mailbox_reg_read_non_zero(mb_chann, mb_chann->res[CHAN_RES_I2X].mb_tail_ptr_reg, &tail))
		return -EINVAL;
	head = mb_chann->i2x_head;
	ringbuf_size = mailbox_get_ringbuf_size(mb_chann, CHAN_RES_I2X);
	start_addr = mb_chann->res[CHAN_RES_I2X].rb_start_addr;

	if (unlikely(tail > ringbuf_size || !IS_ALIGNED(tail, 4))) {
		MB_WARN_ONCE(mb_chann, "Invalid tail 0x%x", tail);
		return -EINVAL;
	}

	/* ringbuf empty */
	if (head == tail)
		return -ENOENT;

	if (head == ringbuf_size)
		head = 0;

	/* Peek size of the message or TOMBSTONE */
	read_addr = mb_chann->mb->res.ringbuf_base + start_addr + head;
	header.total_size = readl((void *)read_addr);
	/* size is TOMBSTONE, set next read from 0 */
	if (header.total_size == TOMBSTONE) {
		if (head < tail) {
			MB_WARN_ONCE(mb_chann, "Tombstone, head 0x%x tail 0x%x",
				     head, tail);
			return -EINVAL;
		}
		mailbox_set_headptr(mb_chann, 0);
		return 0;
	}

	if (unlikely(!header.total_size || !IS_ALIGNED(header.total_size, 4))) {
		MB_WARN_ONCE(mb_chann, "Invalid total size 0x%x", header.total_size);
		return -EINVAL;
	}
	msg_size = sizeof(header) + header.total_size;

	if (msg_size > ringbuf_size - head || msg_size > tail - head) {
		MB_WARN_ONCE(mb_chann, "Invalid message size %d, tail %d, head %d",
			     msg_size, tail, head);
		return -EINVAL;
	}

	rest = sizeof(header) - sizeof(u32);
	read_addr += sizeof(u32);
	memcpy_fromio((u32 *)&header + 1, (void *)read_addr, rest);
	read_addr += rest;

	ret = mailbox_get_resp(mb_chann, &header, (u32 *)read_addr);

	mailbox_set_headptr(mb_chann, head + msg_size);
	/* After update head, it can equal to ringbuf_size. This is expected. */
	trace_mbox_set_head(MAILBOX_NAME, mb_chann->msix_irq,
			    header.opcode, header.id);

	return ret;
}

static irqreturn_t mailbox_irq_handler(int irq, void *p)
{
	struct mailbox_channel *mb_chann = p;

	trace_mbox_irq_handle(MAILBOX_NAME, irq);
	/* Schedule a rx_work to call the callback functions */
	queue_work(mb_chann->work_q, &mb_chann->rx_work);
	/* Clear IOHUB register */
	mailbox_reg_write(mb_chann, mb_chann->iohub_int_addr, 0);

	return IRQ_HANDLED;
}

static void mailbox_rx_worker(struct work_struct *rx_work)
{
	struct mailbox_channel *mb_chann;
	int ret;

	mb_chann = container_of(rx_work, struct mailbox_channel, rx_work);

	if (READ_ONCE(mb_chann->bad_state)) {
		MB_ERR(mb_chann, "Channel in bad state, work aborted");
		return;
	}

	while (1) {
		/*
		 * If return is 0, keep consuming next message, until there is
		 * no messages or an error happened.
		 */
		ret = mailbox_get_msg(mb_chann);
		if (ret == -ENOENT)
			break;

		/* Other error means device doesn't look good, disable irq. */
		if (unlikely(ret)) {
			MB_ERR(mb_chann, "Unexpected ret %d, disable irq", ret);
			WRITE_ONCE(mb_chann->bad_state, true);
			disable_irq(mb_chann->msix_irq);
			break;
		}
	}
}

int xdna_mailbox_send_msg(struct mailbox_channel *mb_chann,
			  const struct xdna_mailbox_msg *msg, u64 tx_timeout)
{
	struct xdna_msg_header *header;
	struct mailbox_msg *mb_msg;
	size_t pkg_size;
	int ret;

	pkg_size = sizeof(*header) + msg->send_size;
	if (pkg_size > mailbox_get_ringbuf_size(mb_chann, CHAN_RES_X2I)) {
		MB_ERR(mb_chann, "Message size larger than ringbuf size");
		return -EINVAL;
	}

	if (unlikely(!IS_ALIGNED(msg->send_size, 4))) {
		MB_ERR(mb_chann, "Message must be 4 bytes align");
		return -EINVAL;
	}

	/* The fist word in payload can NOT be TOMBSTONE */
	if (unlikely(((u32 *)msg->send_data)[0] == TOMBSTONE)) {
		MB_ERR(mb_chann, "Tomb stone in data");
		return -EINVAL;
	}

	if (READ_ONCE(mb_chann->bad_state)) {
		MB_ERR(mb_chann, "Channel in bad state");
		return -EPIPE;
	}

	mb_msg = kzalloc(sizeof(*mb_msg) + pkg_size, GFP_KERNEL);
	if (!mb_msg)
		return -ENOMEM;

	mb_msg->handle = msg->handle;
	mb_msg->notify_cb = msg->notify_cb;
	mb_msg->pkg_size = pkg_size;

	header = &mb_msg->pkg.header;
	/*
	 * Hardware use total_size and size to split huge message.
	 * We do not support it here. Thus the values are the same.
	 */
	header->total_size = msg->send_size;
	header->sz_ver = FIELD_PREP(MSG_BODY_SZ, msg->send_size) |
			FIELD_PREP(MSG_PROTO_VER, MSG_PROTOCOL_VERSION);
	header->opcode = msg->opcode;
	memcpy(mb_msg->pkg.payload, msg->send_data, msg->send_size);

	ret = mailbox_acquire_msgid(mb_chann, mb_msg);
	if (unlikely(ret < 0)) {
		MB_ERR(mb_chann, "mailbox_acquire_msgid failed");
		goto msg_id_failed;
	}
	header->id = ret;

	MB_DBG(mb_chann, "opcode 0x%x size %d id 0x%x",
	       header->opcode, header->total_size, header->id);

	ret = mailbox_send_msg(mb_chann, mb_msg);
	if (ret) {
		MB_DBG(mb_chann, "Error in mailbox send msg, ret %d", ret);
		goto release_id;
	}

	return 0;

release_id:
	mailbox_release_msgid(mb_chann, header->id);
msg_id_failed:
	kfree(mb_msg);
	return ret;
}

struct mailbox_channel *
xdna_mailbox_create_channel(struct mailbox *mb,
			    const struct xdna_mailbox_chann_res *x2i,
			    const struct xdna_mailbox_chann_res *i2x,
			    u32 iohub_int_addr,
			    int mb_irq)
{
	struct mailbox_channel *mb_chann;
	int ret;

	if (!is_power_of_2(x2i->rb_size) || !is_power_of_2(i2x->rb_size)) {
		pr_err("Ring buf size must be power of 2");
		return NULL;
	}

	mb_chann = kzalloc(sizeof(*mb_chann), GFP_KERNEL);
	if (!mb_chann)
		return NULL;

	mb_chann->mb = mb;
	mb_chann->msix_irq = mb_irq;
	mb_chann->iohub_int_addr = iohub_int_addr;
	memcpy(&mb_chann->res[CHAN_RES_X2I], x2i, sizeof(*x2i));
	memcpy(&mb_chann->res[CHAN_RES_I2X], i2x, sizeof(*i2x));

	spin_lock_init(&mb_chann->chan_idr_lock);
	idr_init(&mb_chann->chan_idr);
	mb_chann->x2i_tail = mailbox_get_tailptr(mb_chann, CHAN_RES_X2I);
	mb_chann->i2x_head = mailbox_get_headptr(mb_chann, CHAN_RES_I2X);

	INIT_WORK(&mb_chann->rx_work, mailbox_rx_worker);
	mb_chann->work_q = create_singlethread_workqueue(MAILBOX_NAME);
	if (!mb_chann->work_q) {
		MB_ERR(mb_chann, "Create workqueue failed");
		goto free_and_out;
	}

	/* Everything look good. Time to enable irq handler */
	ret = request_irq(mb_irq, mailbox_irq_handler, 0, MAILBOX_NAME, mb_chann);
	if (ret) {
		MB_ERR(mb_chann, "Failed to request irq %d ret %d", mb_irq, ret);
		goto destroy_wq;
	}

	mb_chann->bad_state = false;

	MB_DBG(mb_chann, "Mailbox channel created (irq: %d)", mb_chann->msix_irq);
	return mb_chann;

destroy_wq:
	destroy_workqueue(mb_chann->work_q);
free_and_out:
	kfree(mb_chann);
	return NULL;
}

int xdna_mailbox_destroy_channel(struct mailbox_channel *mb_chann)
{
	MB_DBG(mb_chann, "IRQ disabled and RX work cancelled");
	free_irq(mb_chann->msix_irq, mb_chann);
	destroy_workqueue(mb_chann->work_q);
	/* We can clean up and release resources */

	idr_for_each(&mb_chann->chan_idr, mailbox_release_msg, mb_chann);
	idr_destroy(&mb_chann->chan_idr);

	MB_DBG(mb_chann, "Mailbox channel destroyed, irq: %d", mb_chann->msix_irq);
	kfree(mb_chann);
	return 0;
}

void xdna_mailbox_stop_channel(struct mailbox_channel *mb_chann)
{
	/* Disable an irq and wait. This might sleep. */
	disable_irq(mb_chann->msix_irq);

	/* Cancel RX work and wait for it to finish */
	cancel_work_sync(&mb_chann->rx_work);
	MB_DBG(mb_chann, "IRQ disabled and RX work cancelled");
}

struct mailbox *xdnam_mailbox_create(struct drm_device *ddev,
				     const struct xdna_mailbox_res *res)
{
	struct mailbox *mb;

	mb = drmm_kzalloc(ddev, sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return NULL;
	mb->dev = ddev->dev;

	/* mailbox and ring buf base and size information */
	memcpy(&mb->res, res, sizeof(*res));

	return mb;
}
