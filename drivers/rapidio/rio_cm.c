// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rio_cm - RapidIO Channelized Messaging Driver
 *
 * Copyright 2013-2016 Integrated Device Technology, Inc.
 * Copyright (c) 2015, Prodrive Technologies
 * Copyright (c) 2015, RapidIO Trade Association
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/reboot.h>
#include <linux/bitops.h>
#include <linux/printk.h>
#include <linux/rio_cm_cdev.h>

#define DRV_NAME        "rio_cm"
#define DRV_VERSION     "1.0.0"
#define DRV_AUTHOR      "Alexandre Bounine <alexandre.bounine@idt.com>"
#define DRV_DESC        "RapidIO Channelized Messaging Driver"
#define DEV_NAME	"rio_cm"

/* Debug output filtering masks */
enum {
	DBG_NONE	= 0,
	DBG_INIT	= BIT(0), /* driver init */
	DBG_EXIT	= BIT(1), /* driver exit */
	DBG_MPORT	= BIT(2), /* mport add/remove */
	DBG_RDEV	= BIT(3), /* RapidIO device add/remove */
	DBG_CHOP	= BIT(4), /* channel operations */
	DBG_WAIT	= BIT(5), /* waiting for events */
	DBG_TX		= BIT(6), /* message TX */
	DBG_TX_EVENT	= BIT(7), /* message TX event */
	DBG_RX_DATA	= BIT(8), /* inbound data messages */
	DBG_RX_CMD	= BIT(9), /* inbound REQ/ACK/NACK messages */
	DBG_ALL		= ~0,
};

#ifdef DEBUG
#define riocm_debug(level, fmt, arg...) \
	do { \
		if (DBG_##level & dbg_level) \
			pr_debug(DRV_NAME ": %s " fmt "\n", \
				__func__, ##arg); \
	} while (0)
#else
#define riocm_debug(level, fmt, arg...) \
		no_printk(KERN_DEBUG pr_fmt(DRV_NAME fmt "\n"), ##arg)
#endif

#define riocm_warn(fmt, arg...) \
	pr_warn(DRV_NAME ": %s WARNING " fmt "\n", __func__, ##arg)

#define riocm_error(fmt, arg...) \
	pr_err(DRV_NAME ": %s ERROR " fmt "\n", __func__, ##arg)


static int cmbox = 1;
module_param(cmbox, int, S_IRUGO);
MODULE_PARM_DESC(cmbox, "RapidIO Mailbox number (default 1)");

static int chstart = 256;
module_param(chstart, int, S_IRUGO);
MODULE_PARM_DESC(chstart,
		 "Start channel number for dynamic allocation (default 256)");

#ifdef DEBUG
static u32 dbg_level = DBG_NONE;
module_param(dbg_level, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(dbg_level, "Debugging output level (default 0 = none)");
#endif

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

#define RIOCM_TX_RING_SIZE	128
#define RIOCM_RX_RING_SIZE	128
#define RIOCM_CONNECT_TO	3 /* connect response TO (in sec) */

#define RIOCM_MAX_CHNUM		0xffff /* Use full range of u16 field */
#define RIOCM_CHNUM_AUTO	0
#define RIOCM_MAX_EP_COUNT	0x10000 /* Max number of endpoints */

enum rio_cm_state {
	RIO_CM_IDLE,
	RIO_CM_CONNECT,
	RIO_CM_CONNECTED,
	RIO_CM_DISCONNECT,
	RIO_CM_CHAN_BOUND,
	RIO_CM_LISTEN,
	RIO_CM_DESTROYING,
};

enum rio_cm_pkt_type {
	RIO_CM_SYS	= 0xaa,
	RIO_CM_CHAN	= 0x55,
};

enum rio_cm_chop {
	CM_CONN_REQ,
	CM_CONN_ACK,
	CM_CONN_CLOSE,
	CM_DATA_MSG,
};

struct rio_ch_base_bhdr {
	u32 src_id;
	u32 dst_id;
#define RIO_HDR_LETTER_MASK 0xffff0000
#define RIO_HDR_MBOX_MASK   0x0000ffff
	u8  src_mbox;
	u8  dst_mbox;
	u8  type;
} __attribute__((__packed__));

struct rio_ch_chan_hdr {
	struct rio_ch_base_bhdr bhdr;
	u8 ch_op;
	u16 dst_ch;
	u16 src_ch;
	u16 msg_len;
	u16 rsrvd;
} __attribute__((__packed__));

struct tx_req {
	struct list_head node;
	struct rio_dev   *rdev;
	void		 *buffer;
	size_t		 len;
};

struct cm_dev {
	struct list_head	list;
	struct rio_mport	*mport;
	void			*rx_buf[RIOCM_RX_RING_SIZE];
	int			rx_slots;
	struct mutex		rx_lock;

	void			*tx_buf[RIOCM_TX_RING_SIZE];
	int			tx_slot;
	int			tx_cnt;
	int			tx_ack_slot;
	struct list_head	tx_reqs;
	spinlock_t		tx_lock;

	struct list_head	peers;
	u32			npeers;
	struct workqueue_struct *rx_wq;
	struct work_struct	rx_work;
};

struct chan_rx_ring {
	void	*buf[RIOCM_RX_RING_SIZE];
	int	head;
	int	tail;
	int	count;

	/* Tracking RX buffers reported to upper level */
	void	*inuse[RIOCM_RX_RING_SIZE];
	int	inuse_cnt;
};

struct rio_channel {
	u16			id;	/* local channel ID */
	struct kref		ref;	/* channel refcount */
	struct file		*filp;
	struct cm_dev		*cmdev;	/* associated CM device object */
	struct rio_dev		*rdev;	/* remote RapidIO device */
	enum rio_cm_state	state;
	int			error;
	spinlock_t		lock;
	void			*context;
	u32			loc_destid;	/* local destID */
	u32			rem_destid;	/* remote destID */
	u16			rem_channel;	/* remote channel ID */
	struct list_head	accept_queue;
	struct list_head	ch_node;
	struct completion	comp;
	struct completion	comp_close;
	struct chan_rx_ring	rx_ring;
};

struct cm_peer {
	struct list_head node;
	struct rio_dev *rdev;
};

struct conn_req {
	struct list_head node;
	u32 destid;	/* requester destID */
	u16 chan;	/* requester channel ID */
	struct cm_dev *cmdev;
};

/*
 * A channel_dev structure represents a CM_CDEV
 * @cdev	Character device
 * @dev		Associated device object
 */
struct channel_dev {
	struct cdev	cdev;
	struct device	*dev;
};

static struct rio_channel *riocm_ch_alloc(u16 ch_num);
static void riocm_ch_free(struct kref *ref);
static int riocm_post_send(struct cm_dev *cm, struct rio_dev *rdev,
			   void *buffer, size_t len);
static int riocm_ch_close(struct rio_channel *ch);

static DEFINE_SPINLOCK(idr_lock);
static DEFINE_IDR(ch_idr);

static LIST_HEAD(cm_dev_list);
static DECLARE_RWSEM(rdev_sem);

static const struct class dev_class = {
	.name = DRV_NAME,
};
static unsigned int dev_major;
static unsigned int dev_minor_base;
static dev_t dev_number;
static struct channel_dev riocm_cdev;

#define is_msg_capable(src_ops, dst_ops)			\
			((src_ops & RIO_SRC_OPS_DATA_MSG) &&	\
			 (dst_ops & RIO_DST_OPS_DATA_MSG))
#define dev_cm_capable(dev) \
	is_msg_capable(dev->src_ops, dev->dst_ops)

static int riocm_cmp(struct rio_channel *ch, enum rio_cm_state cmp)
{
	int ret;

	spin_lock_bh(&ch->lock);
	ret = (ch->state == cmp);
	spin_unlock_bh(&ch->lock);
	return ret;
}

static int riocm_cmp_exch(struct rio_channel *ch,
			   enum rio_cm_state cmp, enum rio_cm_state exch)
{
	int ret;

	spin_lock_bh(&ch->lock);
	ret = (ch->state == cmp);
	if (ret)
		ch->state = exch;
	spin_unlock_bh(&ch->lock);
	return ret;
}

static enum rio_cm_state riocm_exch(struct rio_channel *ch,
				    enum rio_cm_state exch)
{
	enum rio_cm_state old;

	spin_lock_bh(&ch->lock);
	old = ch->state;
	ch->state = exch;
	spin_unlock_bh(&ch->lock);
	return old;
}

static struct rio_channel *riocm_get_channel(u16 nr)
{
	struct rio_channel *ch;

	spin_lock_bh(&idr_lock);
	ch = idr_find(&ch_idr, nr);
	if (ch)
		kref_get(&ch->ref);
	spin_unlock_bh(&idr_lock);
	return ch;
}

static void riocm_put_channel(struct rio_channel *ch)
{
	kref_put(&ch->ref, riocm_ch_free);
}

static void *riocm_rx_get_msg(struct cm_dev *cm)
{
	void *msg;
	int i;

	msg = rio_get_inb_message(cm->mport, cmbox);
	if (msg) {
		for (i = 0; i < RIOCM_RX_RING_SIZE; i++) {
			if (cm->rx_buf[i] == msg) {
				cm->rx_buf[i] = NULL;
				cm->rx_slots++;
				break;
			}
		}

		if (i == RIOCM_RX_RING_SIZE)
			riocm_warn("no record for buffer 0x%p", msg);
	}

	return msg;
}

/*
 * riocm_rx_fill - fills a ring of receive buffers for given cm device
 * @cm: cm_dev object
 * @nent: max number of entries to fill
 *
 * Returns: none
 */
static void riocm_rx_fill(struct cm_dev *cm, int nent)
{
	int i;

	if (cm->rx_slots == 0)
		return;

	for (i = 0; i < RIOCM_RX_RING_SIZE && cm->rx_slots && nent; i++) {
		if (cm->rx_buf[i] == NULL) {
			cm->rx_buf[i] = kmalloc(RIO_MAX_MSG_SIZE, GFP_KERNEL);
			if (cm->rx_buf[i] == NULL)
				break;
			rio_add_inb_buffer(cm->mport, cmbox, cm->rx_buf[i]);
			cm->rx_slots--;
			nent--;
		}
	}
}

/*
 * riocm_rx_free - frees all receive buffers associated with given cm device
 * @cm: cm_dev object
 *
 * Returns: none
 */
static void riocm_rx_free(struct cm_dev *cm)
{
	int i;

	for (i = 0; i < RIOCM_RX_RING_SIZE; i++) {
		if (cm->rx_buf[i] != NULL) {
			kfree(cm->rx_buf[i]);
			cm->rx_buf[i] = NULL;
		}
	}
}

/*
 * riocm_req_handler - connection request handler
 * @cm: cm_dev object
 * @req_data: pointer to the request packet
 *
 * Returns: 0 if success, or
 *          -EINVAL if channel is not in correct state,
 *          -ENODEV if cannot find a channel with specified ID,
 *          -ENOMEM if unable to allocate memory to store the request
 */
static int riocm_req_handler(struct cm_dev *cm, void *req_data)
{
	struct rio_channel *ch;
	struct conn_req *req;
	struct rio_ch_chan_hdr *hh = req_data;
	u16 chnum;

	chnum = ntohs(hh->dst_ch);

	ch = riocm_get_channel(chnum);

	if (!ch)
		return -ENODEV;

	if (ch->state != RIO_CM_LISTEN) {
		riocm_debug(RX_CMD, "channel %d is not in listen state", chnum);
		riocm_put_channel(ch);
		return -EINVAL;
	}

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req) {
		riocm_put_channel(ch);
		return -ENOMEM;
	}

	req->destid = ntohl(hh->bhdr.src_id);
	req->chan = ntohs(hh->src_ch);
	req->cmdev = cm;

	spin_lock_bh(&ch->lock);
	list_add_tail(&req->node, &ch->accept_queue);
	spin_unlock_bh(&ch->lock);
	complete(&ch->comp);
	riocm_put_channel(ch);

	return 0;
}

/*
 * riocm_resp_handler - response to connection request handler
 * @resp_data: pointer to the response packet
 *
 * Returns: 0 if success, or
 *          -EINVAL if channel is not in correct state,
 *          -ENODEV if cannot find a channel with specified ID,
 */
static int riocm_resp_handler(void *resp_data)
{
	struct rio_channel *ch;
	struct rio_ch_chan_hdr *hh = resp_data;
	u16 chnum;

	chnum = ntohs(hh->dst_ch);
	ch = riocm_get_channel(chnum);
	if (!ch)
		return -ENODEV;

	if (ch->state != RIO_CM_CONNECT) {
		riocm_put_channel(ch);
		return -EINVAL;
	}

	riocm_exch(ch, RIO_CM_CONNECTED);
	ch->rem_channel = ntohs(hh->src_ch);
	complete(&ch->comp);
	riocm_put_channel(ch);

	return 0;
}

/*
 * riocm_close_handler - channel close request handler
 * @req_data: pointer to the request packet
 *
 * Returns: 0 if success, or
 *          -ENODEV if cannot find a channel with specified ID,
 *            + error codes returned by riocm_ch_close.
 */
static int riocm_close_handler(void *data)
{
	struct rio_channel *ch;
	struct rio_ch_chan_hdr *hh = data;
	int ret;

	riocm_debug(RX_CMD, "for ch=%d", ntohs(hh->dst_ch));

	spin_lock_bh(&idr_lock);
	ch = idr_find(&ch_idr, ntohs(hh->dst_ch));
	if (!ch) {
		spin_unlock_bh(&idr_lock);
		return -ENODEV;
	}
	idr_remove(&ch_idr, ch->id);
	spin_unlock_bh(&idr_lock);

	riocm_exch(ch, RIO_CM_DISCONNECT);

	ret = riocm_ch_close(ch);
	if (ret)
		riocm_debug(RX_CMD, "riocm_ch_close() returned %d", ret);

	return 0;
}

/*
 * rio_cm_handler - function that services request (non-data) packets
 * @cm: cm_dev object
 * @data: pointer to the packet
 */
static void rio_cm_handler(struct cm_dev *cm, void *data)
{
	struct rio_ch_chan_hdr *hdr;

	if (!rio_mport_is_running(cm->mport))
		goto out;

	hdr = data;

	riocm_debug(RX_CMD, "OP=%x for ch=%d from %d",
		    hdr->ch_op, ntohs(hdr->dst_ch), ntohs(hdr->src_ch));

	switch (hdr->ch_op) {
	case CM_CONN_REQ:
		riocm_req_handler(cm, data);
		break;
	case CM_CONN_ACK:
		riocm_resp_handler(data);
		break;
	case CM_CONN_CLOSE:
		riocm_close_handler(data);
		break;
	default:
		riocm_error("Invalid packet header");
		break;
	}
out:
	kfree(data);
}

/*
 * rio_rx_data_handler - received data packet handler
 * @cm: cm_dev object
 * @buf: data packet
 *
 * Returns: 0 if success, or
 *          -ENODEV if cannot find a channel with specified ID,
 *          -EIO if channel is not in CONNECTED state,
 *          -ENOMEM if channel RX queue is full (packet discarded)
 */
static int rio_rx_data_handler(struct cm_dev *cm, void *buf)
{
	struct rio_ch_chan_hdr *hdr;
	struct rio_channel *ch;

	hdr = buf;

	riocm_debug(RX_DATA, "for ch=%d", ntohs(hdr->dst_ch));

	ch = riocm_get_channel(ntohs(hdr->dst_ch));
	if (!ch) {
		/* Discard data message for non-existing channel */
		kfree(buf);
		return -ENODEV;
	}

	/* Place pointer to the buffer into channel's RX queue */
	spin_lock(&ch->lock);

	if (ch->state != RIO_CM_CONNECTED) {
		/* Channel is not ready to receive data, discard a packet */
		riocm_debug(RX_DATA, "ch=%d is in wrong state=%d",
			    ch->id, ch->state);
		spin_unlock(&ch->lock);
		kfree(buf);
		riocm_put_channel(ch);
		return -EIO;
	}

	if (ch->rx_ring.count == RIOCM_RX_RING_SIZE) {
		/* If RX ring is full, discard a packet */
		riocm_debug(RX_DATA, "ch=%d is full", ch->id);
		spin_unlock(&ch->lock);
		kfree(buf);
		riocm_put_channel(ch);
		return -ENOMEM;
	}

	ch->rx_ring.buf[ch->rx_ring.head] = buf;
	ch->rx_ring.head++;
	ch->rx_ring.count++;
	ch->rx_ring.head %= RIOCM_RX_RING_SIZE;

	complete(&ch->comp);

	spin_unlock(&ch->lock);
	riocm_put_channel(ch);

	return 0;
}

/*
 * rio_ibmsg_handler - inbound message packet handler
 */
static void rio_ibmsg_handler(struct work_struct *work)
{
	struct cm_dev *cm = container_of(work, struct cm_dev, rx_work);
	void *data;
	struct rio_ch_chan_hdr *hdr;

	if (!rio_mport_is_running(cm->mport))
		return;

	while (1) {
		mutex_lock(&cm->rx_lock);
		data = riocm_rx_get_msg(cm);
		if (data)
			riocm_rx_fill(cm, 1);
		mutex_unlock(&cm->rx_lock);

		if (data == NULL)
			break;

		hdr = data;

		if (hdr->bhdr.type != RIO_CM_CHAN) {
			/* For now simply discard packets other than channel */
			riocm_error("Unsupported TYPE code (0x%x). Msg dropped",
				    hdr->bhdr.type);
			kfree(data);
			continue;
		}

		/* Process a channel message */
		if (hdr->ch_op == CM_DATA_MSG)
			rio_rx_data_handler(cm, data);
		else
			rio_cm_handler(cm, data);
	}
}

static void riocm_inb_msg_event(struct rio_mport *mport, void *dev_id,
				int mbox, int slot)
{
	struct cm_dev *cm = dev_id;

	if (rio_mport_is_running(cm->mport) && !work_pending(&cm->rx_work))
		queue_work(cm->rx_wq, &cm->rx_work);
}

/*
 * rio_txcq_handler - TX completion handler
 * @cm: cm_dev object
 * @slot: TX queue slot
 *
 * TX completion handler also ensures that pending request packets are placed
 * into transmit queue as soon as a free slot becomes available. This is done
 * to give higher priority to request packets during high intensity data flow.
 */
static void rio_txcq_handler(struct cm_dev *cm, int slot)
{
	int ack_slot;

	/* ATTN: Add TX completion notification if/when direct buffer
	 * transfer is implemented. At this moment only correct tracking
	 * of tx_count is important.
	 */
	riocm_debug(TX_EVENT, "for mport_%d slot %d tx_cnt %d",
		    cm->mport->id, slot, cm->tx_cnt);

	spin_lock(&cm->tx_lock);
	ack_slot = cm->tx_ack_slot;

	if (ack_slot == slot)
		riocm_debug(TX_EVENT, "slot == ack_slot");

	while (cm->tx_cnt && ((ack_slot != slot) ||
	       (cm->tx_cnt == RIOCM_TX_RING_SIZE))) {

		cm->tx_buf[ack_slot] = NULL;
		++ack_slot;
		ack_slot &= (RIOCM_TX_RING_SIZE - 1);
		cm->tx_cnt--;
	}

	if (cm->tx_cnt < 0 || cm->tx_cnt > RIOCM_TX_RING_SIZE)
		riocm_error("tx_cnt %d out of sync", cm->tx_cnt);

	WARN_ON((cm->tx_cnt < 0) || (cm->tx_cnt > RIOCM_TX_RING_SIZE));

	cm->tx_ack_slot = ack_slot;

	/*
	 * If there are pending requests, insert them into transmit queue
	 */
	if (!list_empty(&cm->tx_reqs) && (cm->tx_cnt < RIOCM_TX_RING_SIZE)) {
		struct tx_req *req, *_req;
		int rc;

		list_for_each_entry_safe(req, _req, &cm->tx_reqs, node) {
			list_del(&req->node);
			cm->tx_buf[cm->tx_slot] = req->buffer;
			rc = rio_add_outb_message(cm->mport, req->rdev, cmbox,
						  req->buffer, req->len);
			kfree(req->buffer);
			kfree(req);

			++cm->tx_cnt;
			++cm->tx_slot;
			cm->tx_slot &= (RIOCM_TX_RING_SIZE - 1);
			if (cm->tx_cnt == RIOCM_TX_RING_SIZE)
				break;
		}
	}

	spin_unlock(&cm->tx_lock);
}

static void riocm_outb_msg_event(struct rio_mport *mport, void *dev_id,
				 int mbox, int slot)
{
	struct cm_dev *cm = dev_id;

	if (cm && rio_mport_is_running(cm->mport))
		rio_txcq_handler(cm, slot);
}

static int riocm_queue_req(struct cm_dev *cm, struct rio_dev *rdev,
			   void *buffer, size_t len)
{
	unsigned long flags;
	struct tx_req *treq;

	treq = kzalloc(sizeof(*treq), GFP_KERNEL);
	if (treq == NULL)
		return -ENOMEM;

	treq->rdev = rdev;
	treq->buffer = buffer;
	treq->len = len;

	spin_lock_irqsave(&cm->tx_lock, flags);
	list_add_tail(&treq->node, &cm->tx_reqs);
	spin_unlock_irqrestore(&cm->tx_lock, flags);
	return 0;
}

/*
 * riocm_post_send - helper function that places packet into msg TX queue
 * @cm: cm_dev object
 * @rdev: target RapidIO device object (required by outbound msg interface)
 * @buffer: pointer to a packet buffer to send
 * @len: length of data to transfer
 * @req: request priority flag
 *
 * Returns: 0 if success, or error code otherwise.
 */
static int riocm_post_send(struct cm_dev *cm, struct rio_dev *rdev,
			   void *buffer, size_t len)
{
	int rc;
	unsigned long flags;

	spin_lock_irqsave(&cm->tx_lock, flags);

	if (cm->mport == NULL) {
		rc = -ENODEV;
		goto err_out;
	}

	if (cm->tx_cnt == RIOCM_TX_RING_SIZE) {
		riocm_debug(TX, "Tx Queue is full");
		rc = -EBUSY;
		goto err_out;
	}

	cm->tx_buf[cm->tx_slot] = buffer;
	rc = rio_add_outb_message(cm->mport, rdev, cmbox, buffer, len);

	riocm_debug(TX, "Add buf@%p destid=%x tx_slot=%d tx_cnt=%d",
		 buffer, rdev->destid, cm->tx_slot, cm->tx_cnt);

	++cm->tx_cnt;
	++cm->tx_slot;
	cm->tx_slot &= (RIOCM_TX_RING_SIZE - 1);

err_out:
	spin_unlock_irqrestore(&cm->tx_lock, flags);
	return rc;
}

/*
 * riocm_ch_send - sends a data packet to a remote device
 * @ch_id: local channel ID
 * @buf: pointer to a data buffer to send (including CM header)
 * @len: length of data to transfer (including CM header)
 *
 * ATTN: ASSUMES THAT THE HEADER SPACE IS RESERVED PART OF THE DATA PACKET
 *
 * Returns: 0 if success, or
 *          -EINVAL if one or more input parameters is/are not valid,
 *          -ENODEV if cannot find a channel with specified ID,
 *          -EAGAIN if a channel is not in CONNECTED state,
 *	    + error codes returned by HW send routine.
 */
static int riocm_ch_send(u16 ch_id, void *buf, int len)
{
	struct rio_channel *ch;
	struct rio_ch_chan_hdr *hdr;
	int ret;

	if (buf == NULL || ch_id == 0 || len == 0 || len > RIO_MAX_MSG_SIZE)
		return -EINVAL;

	if (len < sizeof(struct rio_ch_chan_hdr))
		return -EINVAL;		/* insufficient data from user */

	ch = riocm_get_channel(ch_id);
	if (!ch) {
		riocm_error("%s(%d) ch_%d not found", current->comm,
			    task_pid_nr(current), ch_id);
		return -ENODEV;
	}

	if (!riocm_cmp(ch, RIO_CM_CONNECTED)) {
		ret = -EAGAIN;
		goto err_out;
	}

	/*
	 * Fill buffer header section with corresponding channel data
	 */
	hdr = buf;

	hdr->bhdr.src_id = htonl(ch->loc_destid);
	hdr->bhdr.dst_id = htonl(ch->rem_destid);
	hdr->bhdr.src_mbox = cmbox;
	hdr->bhdr.dst_mbox = cmbox;
	hdr->bhdr.type = RIO_CM_CHAN;
	hdr->ch_op = CM_DATA_MSG;
	hdr->dst_ch = htons(ch->rem_channel);
	hdr->src_ch = htons(ch->id);
	hdr->msg_len = htons((u16)len);

	/* ATTN: the function call below relies on the fact that underlying
	 * HW-specific add_outb_message() routine copies TX data into its own
	 * internal transfer buffer (true for all RIONET compatible mport
	 * drivers). Must be reviewed if mport driver uses the buffer directly.
	 */

	ret = riocm_post_send(ch->cmdev, ch->rdev, buf, len);
	if (ret)
		riocm_debug(TX, "ch %d send_err=%d", ch->id, ret);
err_out:
	riocm_put_channel(ch);
	return ret;
}

static int riocm_ch_free_rxbuf(struct rio_channel *ch, void *buf)
{
	int i, ret = -EINVAL;

	spin_lock_bh(&ch->lock);

	for (i = 0; i < RIOCM_RX_RING_SIZE; i++) {
		if (ch->rx_ring.inuse[i] == buf) {
			ch->rx_ring.inuse[i] = NULL;
			ch->rx_ring.inuse_cnt--;
			ret = 0;
			break;
		}
	}

	spin_unlock_bh(&ch->lock);

	if (!ret)
		kfree(buf);

	return ret;
}

/*
 * riocm_ch_receive - fetch a data packet received for the specified channel
 * @ch: local channel ID
 * @buf: pointer to a packet buffer
 * @timeout: timeout to wait for incoming packet (in jiffies)
 *
 * Returns: 0 and valid buffer pointer if success, or NULL pointer and one of:
 *          -EAGAIN if a channel is not in CONNECTED state,
 *          -ENOMEM if in-use tracking queue is full,
 *          -ETIME if wait timeout expired,
 *	    -EINTR if wait was interrupted.
 */
static int riocm_ch_receive(struct rio_channel *ch, void **buf, long timeout)
{
	void *rxmsg = NULL;
	int i, ret = 0;
	long wret;

	if (!riocm_cmp(ch, RIO_CM_CONNECTED)) {
		ret = -EAGAIN;
		goto out;
	}

	if (ch->rx_ring.inuse_cnt == RIOCM_RX_RING_SIZE) {
		/* If we do not have entries to track buffers given to upper
		 * layer, reject request.
		 */
		ret = -ENOMEM;
		goto out;
	}

	wret = wait_for_completion_interruptible_timeout(&ch->comp, timeout);

	riocm_debug(WAIT, "wait on %d returned %ld", ch->id, wret);

	if (!wret)
		ret = -ETIME;
	else if (wret == -ERESTARTSYS)
		ret = -EINTR;
	else
		ret = riocm_cmp(ch, RIO_CM_CONNECTED) ? 0 : -ECONNRESET;

	if (ret)
		goto out;

	spin_lock_bh(&ch->lock);

	rxmsg = ch->rx_ring.buf[ch->rx_ring.tail];
	ch->rx_ring.buf[ch->rx_ring.tail] = NULL;
	ch->rx_ring.count--;
	ch->rx_ring.tail++;
	ch->rx_ring.tail %= RIOCM_RX_RING_SIZE;
	ret = -ENOMEM;

	for (i = 0; i < RIOCM_RX_RING_SIZE; i++) {
		if (ch->rx_ring.inuse[i] == NULL) {
			ch->rx_ring.inuse[i] = rxmsg;
			ch->rx_ring.inuse_cnt++;
			ret = 0;
			break;
		}
	}

	if (ret) {
		/* We have no entry to store pending message: drop it */
		kfree(rxmsg);
		rxmsg = NULL;
	}

	spin_unlock_bh(&ch->lock);
out:
	*buf = rxmsg;
	return ret;
}

/*
 * riocm_ch_connect - sends a connect request to a remote device
 * @loc_ch: local channel ID
 * @cm: CM device to send connect request
 * @peer: target RapidIO device
 * @rem_ch: remote channel ID
 *
 * Returns: 0 if success, or
 *          -EINVAL if the channel is not in IDLE state,
 *          -EAGAIN if no connection request available immediately,
 *          -ETIME if ACK response timeout expired,
 *          -EINTR if wait for response was interrupted.
 */
static int riocm_ch_connect(u16 loc_ch, struct cm_dev *cm,
			    struct cm_peer *peer, u16 rem_ch)
{
	struct rio_channel *ch = NULL;
	struct rio_ch_chan_hdr *hdr;
	int ret;
	long wret;

	ch = riocm_get_channel(loc_ch);
	if (!ch)
		return -ENODEV;

	if (!riocm_cmp_exch(ch, RIO_CM_IDLE, RIO_CM_CONNECT)) {
		ret = -EINVAL;
		goto conn_done;
	}

	ch->cmdev = cm;
	ch->rdev = peer->rdev;
	ch->context = NULL;
	ch->loc_destid = cm->mport->host_deviceid;
	ch->rem_channel = rem_ch;

	/*
	 * Send connect request to the remote RapidIO device
	 */

	hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
	if (hdr == NULL) {
		ret = -ENOMEM;
		goto conn_done;
	}

	hdr->bhdr.src_id = htonl(ch->loc_destid);
	hdr->bhdr.dst_id = htonl(peer->rdev->destid);
	hdr->bhdr.src_mbox = cmbox;
	hdr->bhdr.dst_mbox = cmbox;
	hdr->bhdr.type = RIO_CM_CHAN;
	hdr->ch_op = CM_CONN_REQ;
	hdr->dst_ch = htons(rem_ch);
	hdr->src_ch = htons(loc_ch);

	/* ATTN: the function call below relies on the fact that underlying
	 * HW-specific add_outb_message() routine copies TX data into its
	 * internal transfer buffer. Must be reviewed if mport driver uses
	 * this buffer directly.
	 */
	ret = riocm_post_send(cm, peer->rdev, hdr, sizeof(*hdr));

	if (ret != -EBUSY) {
		kfree(hdr);
	} else {
		ret = riocm_queue_req(cm, peer->rdev, hdr, sizeof(*hdr));
		if (ret)
			kfree(hdr);
	}

	if (ret) {
		riocm_cmp_exch(ch, RIO_CM_CONNECT, RIO_CM_IDLE);
		goto conn_done;
	}

	/* Wait for connect response from the remote device */
	wret = wait_for_completion_interruptible_timeout(&ch->comp,
							 RIOCM_CONNECT_TO * HZ);
	riocm_debug(WAIT, "wait on %d returns %ld", ch->id, wret);

	if (!wret)
		ret = -ETIME;
	else if (wret == -ERESTARTSYS)
		ret = -EINTR;
	else
		ret = riocm_cmp(ch, RIO_CM_CONNECTED) ? 0 : -1;

conn_done:
	riocm_put_channel(ch);
	return ret;
}

static int riocm_send_ack(struct rio_channel *ch)
{
	struct rio_ch_chan_hdr *hdr;
	int ret;

	hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
	if (hdr == NULL)
		return -ENOMEM;

	hdr->bhdr.src_id = htonl(ch->loc_destid);
	hdr->bhdr.dst_id = htonl(ch->rem_destid);
	hdr->dst_ch = htons(ch->rem_channel);
	hdr->src_ch = htons(ch->id);
	hdr->bhdr.src_mbox = cmbox;
	hdr->bhdr.dst_mbox = cmbox;
	hdr->bhdr.type = RIO_CM_CHAN;
	hdr->ch_op = CM_CONN_ACK;

	/* ATTN: the function call below relies on the fact that underlying
	 * add_outb_message() routine copies TX data into its internal transfer
	 * buffer. Review if switching to direct buffer version.
	 */
	ret = riocm_post_send(ch->cmdev, ch->rdev, hdr, sizeof(*hdr));

	if (ret == -EBUSY && !riocm_queue_req(ch->cmdev,
					      ch->rdev, hdr, sizeof(*hdr)))
		return 0;
	kfree(hdr);

	if (ret)
		riocm_error("send ACK to ch_%d on %s failed (ret=%d)",
			    ch->id, rio_name(ch->rdev), ret);
	return ret;
}

/*
 * riocm_ch_accept - accept incoming connection request
 * @ch_id: channel ID
 * @new_ch_id: local mport device
 * @timeout: wait timeout (if 0 non-blocking call, do not wait if connection
 *           request is not available).
 *
 * Returns: pointer to new channel struct if success, or error-valued pointer:
 *          -ENODEV - cannot find specified channel or mport,
 *          -EINVAL - the channel is not in IDLE state,
 *          -EAGAIN - no connection request available immediately (timeout=0),
 *          -ENOMEM - unable to allocate new channel,
 *          -ETIME - wait timeout expired,
 *          -EINTR - wait was interrupted.
 */
static struct rio_channel *riocm_ch_accept(u16 ch_id, u16 *new_ch_id,
					   long timeout)
{
	struct rio_channel *ch;
	struct rio_channel *new_ch;
	struct conn_req *req;
	struct cm_peer *peer;
	int found = 0;
	int err = 0;
	long wret;

	ch = riocm_get_channel(ch_id);
	if (!ch)
		return ERR_PTR(-EINVAL);

	if (!riocm_cmp(ch, RIO_CM_LISTEN)) {
		err = -EINVAL;
		goto err_put;
	}

	/* Don't sleep if this is a non blocking call */
	if (!timeout) {
		if (!try_wait_for_completion(&ch->comp)) {
			err = -EAGAIN;
			goto err_put;
		}
	} else {
		riocm_debug(WAIT, "on %d", ch->id);

		wret = wait_for_completion_interruptible_timeout(&ch->comp,
								 timeout);
		if (!wret) {
			err = -ETIME;
			goto err_put;
		} else if (wret == -ERESTARTSYS) {
			err = -EINTR;
			goto err_put;
		}
	}

	spin_lock_bh(&ch->lock);

	if (ch->state != RIO_CM_LISTEN) {
		err = -ECANCELED;
	} else if (list_empty(&ch->accept_queue)) {
		riocm_debug(WAIT, "on %d accept_queue is empty on completion",
			    ch->id);
		err = -EIO;
	}

	spin_unlock_bh(&ch->lock);

	if (err) {
		riocm_debug(WAIT, "on %d returns %d", ch->id, err);
		goto err_put;
	}

	/* Create new channel for this connection */
	new_ch = riocm_ch_alloc(RIOCM_CHNUM_AUTO);

	if (IS_ERR(new_ch)) {
		riocm_error("failed to get channel for new req (%ld)",
			PTR_ERR(new_ch));
		err = -ENOMEM;
		goto err_put;
	}

	spin_lock_bh(&ch->lock);

	req = list_first_entry(&ch->accept_queue, struct conn_req, node);
	list_del(&req->node);
	new_ch->cmdev = ch->cmdev;
	new_ch->loc_destid = ch->loc_destid;
	new_ch->rem_destid = req->destid;
	new_ch->rem_channel = req->chan;

	spin_unlock_bh(&ch->lock);
	riocm_put_channel(ch);
	ch = NULL;
	kfree(req);

	down_read(&rdev_sem);
	/* Find requester's device object */
	list_for_each_entry(peer, &new_ch->cmdev->peers, node) {
		if (peer->rdev->destid == new_ch->rem_destid) {
			riocm_debug(RX_CMD, "found matching device(%s)",
				    rio_name(peer->rdev));
			found = 1;
			break;
		}
	}
	up_read(&rdev_sem);

	if (!found) {
		/* If peer device object not found, simply ignore the request */
		err = -ENODEV;
		goto err_put_new_ch;
	}

	new_ch->rdev = peer->rdev;
	new_ch->state = RIO_CM_CONNECTED;
	spin_lock_init(&new_ch->lock);

	/* Acknowledge the connection request. */
	riocm_send_ack(new_ch);

	*new_ch_id = new_ch->id;
	return new_ch;

err_put_new_ch:
	spin_lock_bh(&idr_lock);
	idr_remove(&ch_idr, new_ch->id);
	spin_unlock_bh(&idr_lock);
	riocm_put_channel(new_ch);

err_put:
	if (ch)
		riocm_put_channel(ch);
	*new_ch_id = 0;
	return ERR_PTR(err);
}

/*
 * riocm_ch_listen - puts a channel into LISTEN state
 * @ch_id: channel ID
 *
 * Returns: 0 if success, or
 *          -EINVAL if the specified channel does not exists or
 *                  is not in CHAN_BOUND state.
 */
static int riocm_ch_listen(u16 ch_id)
{
	struct rio_channel *ch = NULL;
	int ret = 0;

	riocm_debug(CHOP, "(ch_%d)", ch_id);

	ch = riocm_get_channel(ch_id);
	if (!ch)
		return -EINVAL;
	if (!riocm_cmp_exch(ch, RIO_CM_CHAN_BOUND, RIO_CM_LISTEN))
		ret = -EINVAL;
	riocm_put_channel(ch);
	return ret;
}

/*
 * riocm_ch_bind - associate a channel object and an mport device
 * @ch_id: channel ID
 * @mport_id: local mport device ID
 * @context: pointer to the additional caller's context
 *
 * Returns: 0 if success, or
 *          -ENODEV if cannot find specified mport,
 *          -EINVAL if the specified channel does not exist or
 *                  is not in IDLE state.
 */
static int riocm_ch_bind(u16 ch_id, u8 mport_id, void *context)
{
	struct rio_channel *ch = NULL;
	struct cm_dev *cm;
	int rc = -ENODEV;

	riocm_debug(CHOP, "ch_%d to mport_%d", ch_id, mport_id);

	/* Find matching cm_dev object */
	down_read(&rdev_sem);
	list_for_each_entry(cm, &cm_dev_list, list) {
		if ((cm->mport->id == mport_id) &&
		     rio_mport_is_running(cm->mport)) {
			rc = 0;
			break;
		}
	}

	if (rc)
		goto exit;

	ch = riocm_get_channel(ch_id);
	if (!ch) {
		rc = -EINVAL;
		goto exit;
	}

	spin_lock_bh(&ch->lock);
	if (ch->state != RIO_CM_IDLE) {
		spin_unlock_bh(&ch->lock);
		rc = -EINVAL;
		goto err_put;
	}

	ch->cmdev = cm;
	ch->loc_destid = cm->mport->host_deviceid;
	ch->context = context;
	ch->state = RIO_CM_CHAN_BOUND;
	spin_unlock_bh(&ch->lock);
err_put:
	riocm_put_channel(ch);
exit:
	up_read(&rdev_sem);
	return rc;
}

/*
 * riocm_ch_alloc - channel object allocation helper routine
 * @ch_num: channel ID (1 ... RIOCM_MAX_CHNUM, 0 = automatic)
 *
 * Return value: pointer to newly created channel object,
 *               or error-valued pointer
 */
static struct rio_channel *riocm_ch_alloc(u16 ch_num)
{
	int id;
	int start, end;
	struct rio_channel *ch;

	ch = kzalloc(sizeof(*ch), GFP_KERNEL);
	if (!ch)
		return ERR_PTR(-ENOMEM);

	if (ch_num) {
		/* If requested, try to obtain the specified channel ID */
		start = ch_num;
		end = ch_num + 1;
	} else {
		/* Obtain channel ID from the dynamic allocation range */
		start = chstart;
		end = RIOCM_MAX_CHNUM + 1;
	}

	idr_preload(GFP_KERNEL);
	spin_lock_bh(&idr_lock);
	id = idr_alloc_cyclic(&ch_idr, ch, start, end, GFP_NOWAIT);
	spin_unlock_bh(&idr_lock);
	idr_preload_end();

	if (id < 0) {
		kfree(ch);
		return ERR_PTR(id == -ENOSPC ? -EBUSY : id);
	}

	ch->id = (u16)id;
	ch->state = RIO_CM_IDLE;
	spin_lock_init(&ch->lock);
	INIT_LIST_HEAD(&ch->accept_queue);
	INIT_LIST_HEAD(&ch->ch_node);
	init_completion(&ch->comp);
	init_completion(&ch->comp_close);
	kref_init(&ch->ref);
	ch->rx_ring.head = 0;
	ch->rx_ring.tail = 0;
	ch->rx_ring.count = 0;
	ch->rx_ring.inuse_cnt = 0;

	return ch;
}

/*
 * riocm_ch_create - creates a new channel object and allocates ID for it
 * @ch_num: channel ID (1 ... RIOCM_MAX_CHNUM, 0 = automatic)
 *
 * Allocates and initializes a new channel object. If the parameter ch_num > 0
 * and is within the valid range, riocm_ch_create tries to allocate the
 * specified ID for the new channel. If ch_num = 0, channel ID will be assigned
 * automatically from the range (chstart ... RIOCM_MAX_CHNUM).
 * Module parameter 'chstart' defines start of an ID range available for dynamic
 * allocation. Range below 'chstart' is reserved for pre-defined ID numbers.
 * Available channel numbers are limited by 16-bit size of channel numbers used
 * in the packet header.
 *
 * Return value: PTR to rio_channel structure if successful (with channel number
 *               updated via pointer) or error-valued pointer if error.
 */
static struct rio_channel *riocm_ch_create(u16 *ch_num)
{
	struct rio_channel *ch = NULL;

	ch = riocm_ch_alloc(*ch_num);

	if (IS_ERR(ch))
		riocm_debug(CHOP, "Failed to allocate channel %d (err=%ld)",
			    *ch_num, PTR_ERR(ch));
	else
		*ch_num = ch->id;

	return ch;
}

/*
 * riocm_ch_free - channel object release routine
 * @ref: pointer to a channel's kref structure
 */
static void riocm_ch_free(struct kref *ref)
{
	struct rio_channel *ch = container_of(ref, struct rio_channel, ref);
	int i;

	riocm_debug(CHOP, "(ch_%d)", ch->id);

	if (ch->rx_ring.inuse_cnt) {
		for (i = 0;
		     i < RIOCM_RX_RING_SIZE && ch->rx_ring.inuse_cnt; i++) {
			if (ch->rx_ring.inuse[i] != NULL) {
				kfree(ch->rx_ring.inuse[i]);
				ch->rx_ring.inuse_cnt--;
			}
		}
	}

	if (ch->rx_ring.count)
		for (i = 0; i < RIOCM_RX_RING_SIZE && ch->rx_ring.count; i++) {
			if (ch->rx_ring.buf[i] != NULL) {
				kfree(ch->rx_ring.buf[i]);
				ch->rx_ring.count--;
			}
		}

	complete(&ch->comp_close);
}

static int riocm_send_close(struct rio_channel *ch)
{
	struct rio_ch_chan_hdr *hdr;
	int ret;

	/*
	 * Send CH_CLOSE notification to the remote RapidIO device
	 */

	hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
	if (hdr == NULL)
		return -ENOMEM;

	hdr->bhdr.src_id = htonl(ch->loc_destid);
	hdr->bhdr.dst_id = htonl(ch->rem_destid);
	hdr->bhdr.src_mbox = cmbox;
	hdr->bhdr.dst_mbox = cmbox;
	hdr->bhdr.type = RIO_CM_CHAN;
	hdr->ch_op = CM_CONN_CLOSE;
	hdr->dst_ch = htons(ch->rem_channel);
	hdr->src_ch = htons(ch->id);

	/* ATTN: the function call below relies on the fact that underlying
	 * add_outb_message() routine copies TX data into its internal transfer
	 * buffer. Needs to be reviewed if switched to direct buffer mode.
	 */
	ret = riocm_post_send(ch->cmdev, ch->rdev, hdr, sizeof(*hdr));

	if (ret == -EBUSY && !riocm_queue_req(ch->cmdev, ch->rdev,
					      hdr, sizeof(*hdr)))
		return 0;
	kfree(hdr);

	if (ret)
		riocm_error("ch(%d) send CLOSE failed (ret=%d)", ch->id, ret);

	return ret;
}

/*
 * riocm_ch_close - closes a channel object with specified ID (by local request)
 * @ch: channel to be closed
 */
static int riocm_ch_close(struct rio_channel *ch)
{
	unsigned long tmo = msecs_to_jiffies(3000);
	enum rio_cm_state state;
	long wret;
	int ret = 0;

	riocm_debug(CHOP, "ch_%d by %s(%d)",
		    ch->id, current->comm, task_pid_nr(current));

	state = riocm_exch(ch, RIO_CM_DESTROYING);
	if (state == RIO_CM_CONNECTED)
		riocm_send_close(ch);

	complete_all(&ch->comp);

	riocm_put_channel(ch);
	wret = wait_for_completion_interruptible_timeout(&ch->comp_close, tmo);

	riocm_debug(WAIT, "wait on %d returns %ld", ch->id, wret);

	if (wret == 0) {
		/* Timeout on wait occurred */
		riocm_debug(CHOP, "%s(%d) timed out waiting for ch %d",
		       current->comm, task_pid_nr(current), ch->id);
		ret = -ETIMEDOUT;
	} else if (wret == -ERESTARTSYS) {
		/* Wait_for_completion was interrupted by a signal */
		riocm_debug(CHOP, "%s(%d) wait for ch %d was interrupted",
			current->comm, task_pid_nr(current), ch->id);
		ret = -EINTR;
	}

	if (!ret) {
		riocm_debug(CHOP, "ch_%d resources released", ch->id);
		kfree(ch);
	} else {
		riocm_debug(CHOP, "failed to release ch_%d resources", ch->id);
	}

	return ret;
}

/*
 * riocm_cdev_open() - Open character device
 */
static int riocm_cdev_open(struct inode *inode, struct file *filp)
{
	riocm_debug(INIT, "by %s(%d) filp=%p ",
		    current->comm, task_pid_nr(current), filp);

	if (list_empty(&cm_dev_list))
		return -ENODEV;

	return 0;
}

/*
 * riocm_cdev_release() - Release character device
 */
static int riocm_cdev_release(struct inode *inode, struct file *filp)
{
	struct rio_channel *ch, *_c;
	unsigned int i;
	LIST_HEAD(list);

	riocm_debug(EXIT, "by %s(%d) filp=%p",
		    current->comm, task_pid_nr(current), filp);

	/* Check if there are channels associated with this file descriptor */
	spin_lock_bh(&idr_lock);
	idr_for_each_entry(&ch_idr, ch, i) {
		if (ch && ch->filp == filp) {
			riocm_debug(EXIT, "ch_%d not released by %s(%d)",
				    ch->id, current->comm,
				    task_pid_nr(current));
			idr_remove(&ch_idr, ch->id);
			list_add(&ch->ch_node, &list);
		}
	}
	spin_unlock_bh(&idr_lock);

	if (!list_empty(&list)) {
		list_for_each_entry_safe(ch, _c, &list, ch_node) {
			list_del(&ch->ch_node);
			riocm_ch_close(ch);
		}
	}

	return 0;
}

/*
 * cm_ep_get_list_size() - Reports number of endpoints in the network
 */
static int cm_ep_get_list_size(void __user *arg)
{
	u32 __user *p = arg;
	u32 mport_id;
	u32 count = 0;
	struct cm_dev *cm;

	if (get_user(mport_id, p))
		return -EFAULT;
	if (mport_id >= RIO_MAX_MPORTS)
		return -EINVAL;

	/* Find a matching cm_dev object */
	down_read(&rdev_sem);
	list_for_each_entry(cm, &cm_dev_list, list) {
		if (cm->mport->id == mport_id) {
			count = cm->npeers;
			up_read(&rdev_sem);
			if (copy_to_user(arg, &count, sizeof(u32)))
				return -EFAULT;
			return 0;
		}
	}
	up_read(&rdev_sem);

	return -ENODEV;
}

/*
 * cm_ep_get_list() - Returns list of attached endpoints
 */
static int cm_ep_get_list(void __user *arg)
{
	struct cm_dev *cm;
	struct cm_peer *peer;
	u32 info[2];
	void *buf;
	u32 nent;
	u32 *entry_ptr;
	u32 i = 0;
	int ret = 0;

	if (copy_from_user(&info, arg, sizeof(info)))
		return -EFAULT;

	if (info[1] >= RIO_MAX_MPORTS || info[0] > RIOCM_MAX_EP_COUNT)
		return -EINVAL;

	/* Find a matching cm_dev object */
	down_read(&rdev_sem);
	list_for_each_entry(cm, &cm_dev_list, list)
		if (cm->mport->id == (u8)info[1])
			goto found;

	up_read(&rdev_sem);
	return -ENODEV;

found:
	nent = min(info[0], cm->npeers);
	buf = kcalloc(nent + 2, sizeof(u32), GFP_KERNEL);
	if (!buf) {
		up_read(&rdev_sem);
		return -ENOMEM;
	}

	entry_ptr = (u32 *)((uintptr_t)buf + 2*sizeof(u32));

	list_for_each_entry(peer, &cm->peers, node) {
		*entry_ptr = (u32)peer->rdev->destid;
		entry_ptr++;
		if (++i == nent)
			break;
	}
	up_read(&rdev_sem);

	((u32 *)buf)[0] = i; /* report an updated number of entries */
	((u32 *)buf)[1] = info[1]; /* put back an mport ID */
	if (copy_to_user(arg, buf, sizeof(u32) * (info[0] + 2)))
		ret = -EFAULT;

	kfree(buf);
	return ret;
}

/*
 * cm_mport_get_list() - Returns list of available local mport devices
 */
static int cm_mport_get_list(void __user *arg)
{
	int ret = 0;
	u32 entries;
	void *buf;
	struct cm_dev *cm;
	u32 *entry_ptr;
	int count = 0;

	if (copy_from_user(&entries, arg, sizeof(entries)))
		return -EFAULT;
	if (entries == 0 || entries > RIO_MAX_MPORTS)
		return -EINVAL;
	buf = kcalloc(entries + 1, sizeof(u32), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Scan all registered cm_dev objects */
	entry_ptr = (u32 *)((uintptr_t)buf + sizeof(u32));
	down_read(&rdev_sem);
	list_for_each_entry(cm, &cm_dev_list, list) {
		if (count++ < entries) {
			*entry_ptr = (cm->mport->id << 16) |
				      cm->mport->host_deviceid;
			entry_ptr++;
		}
	}
	up_read(&rdev_sem);

	*((u32 *)buf) = count; /* report a real number of entries */
	if (copy_to_user(arg, buf, sizeof(u32) * (count + 1)))
		ret = -EFAULT;

	kfree(buf);
	return ret;
}

/*
 * cm_chan_create() - Create a message exchange channel
 */
static int cm_chan_create(struct file *filp, void __user *arg)
{
	u16 __user *p = arg;
	u16 ch_num;
	struct rio_channel *ch;

	if (get_user(ch_num, p))
		return -EFAULT;

	riocm_debug(CHOP, "ch_%d requested by %s(%d)",
		    ch_num, current->comm, task_pid_nr(current));
	ch = riocm_ch_create(&ch_num);
	if (IS_ERR(ch))
		return PTR_ERR(ch);

	ch->filp = filp;
	riocm_debug(CHOP, "ch_%d created by %s(%d)",
		    ch_num, current->comm, task_pid_nr(current));
	return put_user(ch_num, p);
}

/*
 * cm_chan_close() - Close channel
 * @filp:	Pointer to file object
 * @arg:	Channel to close
 */
static int cm_chan_close(struct file *filp, void __user *arg)
{
	u16 __user *p = arg;
	u16 ch_num;
	struct rio_channel *ch;

	if (get_user(ch_num, p))
		return -EFAULT;

	riocm_debug(CHOP, "ch_%d by %s(%d)",
		    ch_num, current->comm, task_pid_nr(current));

	spin_lock_bh(&idr_lock);
	ch = idr_find(&ch_idr, ch_num);
	if (!ch) {
		spin_unlock_bh(&idr_lock);
		return 0;
	}
	if (ch->filp != filp) {
		spin_unlock_bh(&idr_lock);
		return -EINVAL;
	}
	idr_remove(&ch_idr, ch->id);
	spin_unlock_bh(&idr_lock);

	return riocm_ch_close(ch);
}

/*
 * cm_chan_bind() - Bind channel
 * @arg:	Channel number
 */
static int cm_chan_bind(void __user *arg)
{
	struct rio_cm_channel chan;

	if (copy_from_user(&chan, arg, sizeof(chan)))
		return -EFAULT;
	if (chan.mport_id >= RIO_MAX_MPORTS)
		return -EINVAL;

	return riocm_ch_bind(chan.id, chan.mport_id, NULL);
}

/*
 * cm_chan_listen() - Listen on channel
 * @arg:	Channel number
 */
static int cm_chan_listen(void __user *arg)
{
	u16 __user *p = arg;
	u16 ch_num;

	if (get_user(ch_num, p))
		return -EFAULT;

	return riocm_ch_listen(ch_num);
}

/*
 * cm_chan_accept() - Accept incoming connection
 * @filp:	Pointer to file object
 * @arg:	Channel number
 */
static int cm_chan_accept(struct file *filp, void __user *arg)
{
	struct rio_cm_accept param;
	long accept_to;
	struct rio_channel *ch;

	if (copy_from_user(&param, arg, sizeof(param)))
		return -EFAULT;

	riocm_debug(CHOP, "on ch_%d by %s(%d)",
		    param.ch_num, current->comm, task_pid_nr(current));

	accept_to = param.wait_to ?
			msecs_to_jiffies(param.wait_to) : 0;

	ch = riocm_ch_accept(param.ch_num, &param.ch_num, accept_to);
	if (IS_ERR(ch))
		return PTR_ERR(ch);
	ch->filp = filp;

	riocm_debug(CHOP, "new ch_%d for %s(%d)",
		    ch->id, current->comm, task_pid_nr(current));

	if (copy_to_user(arg, &param, sizeof(param)))
		return -EFAULT;
	return 0;
}

/*
 * cm_chan_connect() - Connect on channel
 * @arg:	Channel information
 */
static int cm_chan_connect(void __user *arg)
{
	struct rio_cm_channel chan;
	struct cm_dev *cm;
	struct cm_peer *peer;
	int ret = -ENODEV;

	if (copy_from_user(&chan, arg, sizeof(chan)))
		return -EFAULT;
	if (chan.mport_id >= RIO_MAX_MPORTS)
		return -EINVAL;

	down_read(&rdev_sem);

	/* Find matching cm_dev object */
	list_for_each_entry(cm, &cm_dev_list, list) {
		if (cm->mport->id == chan.mport_id) {
			ret = 0;
			break;
		}
	}

	if (ret)
		goto err_out;

	if (chan.remote_destid >= RIO_ANY_DESTID(cm->mport->sys_size)) {
		ret = -EINVAL;
		goto err_out;
	}

	/* Find corresponding RapidIO endpoint device object */
	ret = -ENODEV;

	list_for_each_entry(peer, &cm->peers, node) {
		if (peer->rdev->destid == chan.remote_destid) {
			ret = 0;
			break;
		}
	}

	if (ret)
		goto err_out;

	up_read(&rdev_sem);

	return riocm_ch_connect(chan.id, cm, peer, chan.remote_channel);
err_out:
	up_read(&rdev_sem);
	return ret;
}

/*
 * cm_chan_msg_send() - Send a message through channel
 * @arg:	Outbound message information
 */
static int cm_chan_msg_send(void __user *arg)
{
	struct rio_cm_msg msg;
	void *buf;
	int ret;

	if (copy_from_user(&msg, arg, sizeof(msg)))
		return -EFAULT;
	if (msg.size > RIO_MAX_MSG_SIZE)
		return -EINVAL;

	buf = memdup_user((void __user *)(uintptr_t)msg.msg, msg.size);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = riocm_ch_send(msg.ch_num, buf, msg.size);

	kfree(buf);
	return ret;
}

/*
 * cm_chan_msg_rcv() - Receive a message through channel
 * @arg:	Inbound message information
 */
static int cm_chan_msg_rcv(void __user *arg)
{
	struct rio_cm_msg msg;
	struct rio_channel *ch;
	void *buf;
	long rxto;
	int ret = 0, msg_size;

	if (copy_from_user(&msg, arg, sizeof(msg)))
		return -EFAULT;

	if (msg.ch_num == 0 || msg.size == 0)
		return -EINVAL;

	ch = riocm_get_channel(msg.ch_num);
	if (!ch)
		return -ENODEV;

	rxto = msg.rxto ? msecs_to_jiffies(msg.rxto) : MAX_SCHEDULE_TIMEOUT;

	ret = riocm_ch_receive(ch, &buf, rxto);
	if (ret)
		goto out;

	msg_size = min(msg.size, (u16)(RIO_MAX_MSG_SIZE));

	if (copy_to_user((void __user *)(uintptr_t)msg.msg, buf, msg_size))
		ret = -EFAULT;

	riocm_ch_free_rxbuf(ch, buf);
out:
	riocm_put_channel(ch);
	return ret;
}

/*
 * riocm_cdev_ioctl() - IOCTL requests handler
 */
static long
riocm_cdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case RIO_CM_EP_GET_LIST_SIZE:
		return cm_ep_get_list_size((void __user *)arg);
	case RIO_CM_EP_GET_LIST:
		return cm_ep_get_list((void __user *)arg);
	case RIO_CM_CHAN_CREATE:
		return cm_chan_create(filp, (void __user *)arg);
	case RIO_CM_CHAN_CLOSE:
		return cm_chan_close(filp, (void __user *)arg);
	case RIO_CM_CHAN_BIND:
		return cm_chan_bind((void __user *)arg);
	case RIO_CM_CHAN_LISTEN:
		return cm_chan_listen((void __user *)arg);
	case RIO_CM_CHAN_ACCEPT:
		return cm_chan_accept(filp, (void __user *)arg);
	case RIO_CM_CHAN_CONNECT:
		return cm_chan_connect((void __user *)arg);
	case RIO_CM_CHAN_SEND:
		return cm_chan_msg_send((void __user *)arg);
	case RIO_CM_CHAN_RECEIVE:
		return cm_chan_msg_rcv((void __user *)arg);
	case RIO_CM_MPORT_GET_LIST:
		return cm_mport_get_list((void __user *)arg);
	default:
		break;
	}

	return -EINVAL;
}

static const struct file_operations riocm_cdev_fops = {
	.owner		= THIS_MODULE,
	.open		= riocm_cdev_open,
	.release	= riocm_cdev_release,
	.unlocked_ioctl = riocm_cdev_ioctl,
};

/*
 * riocm_add_dev - add new remote RapidIO device into channel management core
 * @dev: device object associated with RapidIO device
 * @sif: subsystem interface
 *
 * Adds the specified RapidIO device (if applicable) into peers list of
 * the corresponding channel management device (cm_dev).
 */
static int riocm_add_dev(struct device *dev, struct subsys_interface *sif)
{
	struct cm_peer *peer;
	struct rio_dev *rdev = to_rio_dev(dev);
	struct cm_dev *cm;

	/* Check if the remote device has capabilities required to support CM */
	if (!dev_cm_capable(rdev))
		return 0;

	riocm_debug(RDEV, "(%s)", rio_name(rdev));

	peer = kmalloc(sizeof(*peer), GFP_KERNEL);
	if (!peer)
		return -ENOMEM;

	/* Find a corresponding cm_dev object */
	down_write(&rdev_sem);
	list_for_each_entry(cm, &cm_dev_list, list) {
		if (cm->mport == rdev->net->hport)
			goto found;
	}

	up_write(&rdev_sem);
	kfree(peer);
	return -ENODEV;

found:
	peer->rdev = rdev;
	list_add_tail(&peer->node, &cm->peers);
	cm->npeers++;

	up_write(&rdev_sem);
	return 0;
}

/*
 * riocm_remove_dev - remove remote RapidIO device from channel management core
 * @dev: device object associated with RapidIO device
 * @sif: subsystem interface
 *
 * Removes the specified RapidIO device (if applicable) from peers list of
 * the corresponding channel management device (cm_dev).
 */
static void riocm_remove_dev(struct device *dev, struct subsys_interface *sif)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	struct cm_dev *cm;
	struct cm_peer *peer;
	struct rio_channel *ch, *_c;
	unsigned int i;
	bool found = false;
	LIST_HEAD(list);

	/* Check if the remote device has capabilities required to support CM */
	if (!dev_cm_capable(rdev))
		return;

	riocm_debug(RDEV, "(%s)", rio_name(rdev));

	/* Find matching cm_dev object */
	down_write(&rdev_sem);
	list_for_each_entry(cm, &cm_dev_list, list) {
		if (cm->mport == rdev->net->hport) {
			found = true;
			break;
		}
	}

	if (!found) {
		up_write(&rdev_sem);
		return;
	}

	/* Remove remote device from the list of peers */
	found = false;
	list_for_each_entry(peer, &cm->peers, node) {
		if (peer->rdev == rdev) {
			riocm_debug(RDEV, "removing peer %s", rio_name(rdev));
			found = true;
			list_del(&peer->node);
			cm->npeers--;
			kfree(peer);
			break;
		}
	}

	up_write(&rdev_sem);

	if (!found)
		return;

	/*
	 * Release channels associated with this peer
	 */

	spin_lock_bh(&idr_lock);
	idr_for_each_entry(&ch_idr, ch, i) {
		if (ch && ch->rdev == rdev) {
			if (atomic_read(&rdev->state) != RIO_DEVICE_SHUTDOWN)
				riocm_exch(ch, RIO_CM_DISCONNECT);
			idr_remove(&ch_idr, ch->id);
			list_add(&ch->ch_node, &list);
		}
	}
	spin_unlock_bh(&idr_lock);

	if (!list_empty(&list)) {
		list_for_each_entry_safe(ch, _c, &list, ch_node) {
			list_del(&ch->ch_node);
			riocm_ch_close(ch);
		}
	}
}

/*
 * riocm_cdev_add() - Create rio_cm char device
 * @devno: device number assigned to device (MAJ + MIN)
 */
static int riocm_cdev_add(dev_t devno)
{
	int ret;

	cdev_init(&riocm_cdev.cdev, &riocm_cdev_fops);
	riocm_cdev.cdev.owner = THIS_MODULE;
	ret = cdev_add(&riocm_cdev.cdev, devno, 1);
	if (ret < 0) {
		riocm_error("Cannot register a device with error %d", ret);
		return ret;
	}

	riocm_cdev.dev = device_create(&dev_class, NULL, devno, NULL, DEV_NAME);
	if (IS_ERR(riocm_cdev.dev)) {
		cdev_del(&riocm_cdev.cdev);
		return PTR_ERR(riocm_cdev.dev);
	}

	riocm_debug(MPORT, "Added %s cdev(%d:%d)",
		    DEV_NAME, MAJOR(devno), MINOR(devno));

	return 0;
}

/*
 * riocm_add_mport - add new local mport device into channel management core
 * @dev: device object associated with mport
 *
 * When a new mport device is added, CM immediately reserves inbound and
 * outbound RapidIO mailboxes that will be used.
 */
static int riocm_add_mport(struct device *dev)
{
	int rc;
	int i;
	struct cm_dev *cm;
	struct rio_mport *mport = to_rio_mport(dev);

	riocm_debug(MPORT, "add mport %s", mport->name);

	cm = kzalloc(sizeof(*cm), GFP_KERNEL);
	if (!cm)
		return -ENOMEM;

	cm->mport = mport;

	rc = rio_request_outb_mbox(mport, cm, cmbox,
				   RIOCM_TX_RING_SIZE, riocm_outb_msg_event);
	if (rc) {
		riocm_error("failed to allocate OBMBOX_%d on %s",
			    cmbox, mport->name);
		kfree(cm);
		return -ENODEV;
	}

	rc = rio_request_inb_mbox(mport, cm, cmbox,
				  RIOCM_RX_RING_SIZE, riocm_inb_msg_event);
	if (rc) {
		riocm_error("failed to allocate IBMBOX_%d on %s",
			    cmbox, mport->name);
		rio_release_outb_mbox(mport, cmbox);
		kfree(cm);
		return -ENODEV;
	}

	cm->rx_wq = create_workqueue(DRV_NAME "/rxq");
	if (!cm->rx_wq) {
		rio_release_inb_mbox(mport, cmbox);
		rio_release_outb_mbox(mport, cmbox);
		kfree(cm);
		return -ENOMEM;
	}

	/*
	 * Allocate and register inbound messaging buffers to be ready
	 * to receive channel and system management requests
	 */
	for (i = 0; i < RIOCM_RX_RING_SIZE; i++)
		cm->rx_buf[i] = NULL;

	cm->rx_slots = RIOCM_RX_RING_SIZE;
	mutex_init(&cm->rx_lock);
	riocm_rx_fill(cm, RIOCM_RX_RING_SIZE);
	INIT_WORK(&cm->rx_work, rio_ibmsg_handler);

	cm->tx_slot = 0;
	cm->tx_cnt = 0;
	cm->tx_ack_slot = 0;
	spin_lock_init(&cm->tx_lock);

	INIT_LIST_HEAD(&cm->peers);
	cm->npeers = 0;
	INIT_LIST_HEAD(&cm->tx_reqs);

	down_write(&rdev_sem);
	list_add_tail(&cm->list, &cm_dev_list);
	up_write(&rdev_sem);

	return 0;
}

/*
 * riocm_remove_mport - remove local mport device from channel management core
 * @dev: device object associated with mport
 *
 * Removes a local mport device from the list of registered devices that provide
 * channel management services. Returns an error if the specified mport is not
 * registered with the CM core.
 */
static void riocm_remove_mport(struct device *dev)
{
	struct rio_mport *mport = to_rio_mport(dev);
	struct cm_dev *cm;
	struct cm_peer *peer, *temp;
	struct rio_channel *ch, *_c;
	unsigned int i;
	bool found = false;
	LIST_HEAD(list);

	riocm_debug(MPORT, "%s", mport->name);

	/* Find a matching cm_dev object */
	down_write(&rdev_sem);
	list_for_each_entry(cm, &cm_dev_list, list) {
		if (cm->mport == mport) {
			list_del(&cm->list);
			found = true;
			break;
		}
	}
	up_write(&rdev_sem);
	if (!found)
		return;

	flush_workqueue(cm->rx_wq);
	destroy_workqueue(cm->rx_wq);

	/* Release channels bound to this mport */
	spin_lock_bh(&idr_lock);
	idr_for_each_entry(&ch_idr, ch, i) {
		if (ch->cmdev == cm) {
			riocm_debug(RDEV, "%s drop ch_%d",
				    mport->name, ch->id);
			idr_remove(&ch_idr, ch->id);
			list_add(&ch->ch_node, &list);
		}
	}
	spin_unlock_bh(&idr_lock);

	if (!list_empty(&list)) {
		list_for_each_entry_safe(ch, _c, &list, ch_node) {
			list_del(&ch->ch_node);
			riocm_ch_close(ch);
		}
	}

	rio_release_inb_mbox(mport, cmbox);
	rio_release_outb_mbox(mport, cmbox);

	/* Remove and free peer entries */
	if (!list_empty(&cm->peers))
		riocm_debug(RDEV, "ATTN: peer list not empty");
	list_for_each_entry_safe(peer, temp, &cm->peers, node) {
		riocm_debug(RDEV, "removing peer %s", rio_name(peer->rdev));
		list_del(&peer->node);
		kfree(peer);
	}

	riocm_rx_free(cm);
	kfree(cm);
	riocm_debug(MPORT, "%s done", mport->name);
}

static int rio_cm_shutdown(struct notifier_block *nb, unsigned long code,
	void *unused)
{
	struct rio_channel *ch;
	unsigned int i;
	LIST_HEAD(list);

	riocm_debug(EXIT, ".");

	/*
	 * If there are any channels left in connected state send
	 * close notification to the connection partner.
	 * First build a list of channels that require a closing
	 * notification because function riocm_send_close() should
	 * be called outside of spinlock protected code.
	 */
	spin_lock_bh(&idr_lock);
	idr_for_each_entry(&ch_idr, ch, i) {
		if (ch->state == RIO_CM_CONNECTED) {
			riocm_debug(EXIT, "close ch %d", ch->id);
			idr_remove(&ch_idr, ch->id);
			list_add(&ch->ch_node, &list);
		}
	}
	spin_unlock_bh(&idr_lock);

	list_for_each_entry(ch, &list, ch_node)
		riocm_send_close(ch);

	return NOTIFY_DONE;
}

/*
 * riocm_interface handles addition/removal of remote RapidIO devices
 */
static struct subsys_interface riocm_interface = {
	.name		= "rio_cm",
	.subsys		= &rio_bus_type,
	.add_dev	= riocm_add_dev,
	.remove_dev	= riocm_remove_dev,
};

/*
 * rio_mport_interface handles addition/removal local mport devices
 */
static struct class_interface rio_mport_interface __refdata = {
	.class = &rio_mport_class,
	.add_dev = riocm_add_mport,
	.remove_dev = riocm_remove_mport,
};

static struct notifier_block rio_cm_notifier = {
	.notifier_call = rio_cm_shutdown,
};

static int __init riocm_init(void)
{
	int ret;

	/* Create device class needed by udev */
	ret = class_register(&dev_class);
	if (ret) {
		riocm_error("Cannot create " DRV_NAME " class");
		return ret;
	}

	ret = alloc_chrdev_region(&dev_number, 0, 1, DRV_NAME);
	if (ret) {
		class_unregister(&dev_class);
		return ret;
	}

	dev_major = MAJOR(dev_number);
	dev_minor_base = MINOR(dev_number);
	riocm_debug(INIT, "Registered class with %d major", dev_major);

	/*
	 * Register as rapidio_port class interface to get notifications about
	 * mport additions and removals.
	 */
	ret = class_interface_register(&rio_mport_interface);
	if (ret) {
		riocm_error("class_interface_register error: %d", ret);
		goto err_reg;
	}

	/*
	 * Register as RapidIO bus interface to get notifications about
	 * addition/removal of remote RapidIO devices.
	 */
	ret = subsys_interface_register(&riocm_interface);
	if (ret) {
		riocm_error("subsys_interface_register error: %d", ret);
		goto err_cl;
	}

	ret = register_reboot_notifier(&rio_cm_notifier);
	if (ret) {
		riocm_error("failed to register reboot notifier (err=%d)", ret);
		goto err_sif;
	}

	ret = riocm_cdev_add(dev_number);
	if (ret) {
		unregister_reboot_notifier(&rio_cm_notifier);
		ret = -ENODEV;
		goto err_sif;
	}

	return 0;
err_sif:
	subsys_interface_unregister(&riocm_interface);
err_cl:
	class_interface_unregister(&rio_mport_interface);
err_reg:
	unregister_chrdev_region(dev_number, 1);
	class_unregister(&dev_class);
	return ret;
}

static void __exit riocm_exit(void)
{
	riocm_debug(EXIT, "enter");
	unregister_reboot_notifier(&rio_cm_notifier);
	subsys_interface_unregister(&riocm_interface);
	class_interface_unregister(&rio_mport_interface);
	idr_destroy(&ch_idr);

	device_unregister(riocm_cdev.dev);
	cdev_del(&(riocm_cdev.cdev));

	class_unregister(&dev_class);
	unregister_chrdev_region(dev_number, 1);
}

late_initcall(riocm_init);
module_exit(riocm_exit);
