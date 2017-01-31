/*
 * Copyright (c) 2015, Sony Mobile Communications AB.
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>
#include <linux/wait.h>
#include <linux/rpmsg.h>

#include "rpmsg_internal.h"

/*
 * The Qualcomm Shared Memory communication solution provides point-to-point
 * channels for clients to send and receive streaming or packet based data.
 *
 * Each channel consists of a control item (channel info) and a ring buffer
 * pair. The channel info carry information related to channel state, flow
 * control and the offsets within the ring buffer.
 *
 * All allocated channels are listed in an allocation table, identifying the
 * pair of items by name, type and remote processor.
 *
 * Upon creating a new channel the remote processor allocates channel info and
 * ring buffer items from the smem heap and populate the allocation table. An
 * interrupt is sent to the other end of the channel and a scan for new
 * channels should be done. A channel never goes away, it will only change
 * state.
 *
 * The remote processor signals it intent for bring up the communication
 * channel by setting the state of its end of the channel to "opening" and
 * sends out an interrupt. We detect this change and register a smd device to
 * consume the channel. Upon finding a consumer we finish the handshake and the
 * channel is up.
 *
 * Upon closing a channel, the remote processor will update the state of its
 * end of the channel and signal us, we will then unregister any attached
 * device and close our end of the channel.
 *
 * Devices attached to a channel can use the qcom_smd_send function to push
 * data to the channel, this is done by copying the data into the tx ring
 * buffer, updating the pointers in the channel info and signaling the remote
 * processor.
 *
 * The remote processor does the equivalent when it transfer data and upon
 * receiving the interrupt we check the channel info for new data and delivers
 * this to the attached device. If the device is not ready to receive the data
 * we leave it in the ring buffer for now.
 */

struct smd_channel_info;
struct smd_channel_info_pair;
struct smd_channel_info_word;
struct smd_channel_info_word_pair;

static const struct rpmsg_endpoint_ops qcom_smd_endpoint_ops;

#define SMD_ALLOC_TBL_COUNT	2
#define SMD_ALLOC_TBL_SIZE	64

/*
 * This lists the various smem heap items relevant for the allocation table and
 * smd channel entries.
 */
static const struct {
	unsigned alloc_tbl_id;
	unsigned info_base_id;
	unsigned fifo_base_id;
} smem_items[SMD_ALLOC_TBL_COUNT] = {
	{
		.alloc_tbl_id = 13,
		.info_base_id = 14,
		.fifo_base_id = 338
	},
	{
		.alloc_tbl_id = 266,
		.info_base_id = 138,
		.fifo_base_id = 202,
	},
};

/**
 * struct qcom_smd_edge - representing a remote processor
 * @of_node:		of_node handle for information related to this edge
 * @edge_id:		identifier of this edge
 * @remote_pid:		identifier of remote processor
 * @irq:		interrupt for signals on this edge
 * @ipc_regmap:		regmap handle holding the outgoing ipc register
 * @ipc_offset:		offset within @ipc_regmap of the register for ipc
 * @ipc_bit:		bit in the register at @ipc_offset of @ipc_regmap
 * @channels:		list of all channels detected on this edge
 * @channels_lock:	guard for modifications of @channels
 * @allocated:		array of bitmaps representing already allocated channels
 * @smem_available:	last available amount of smem triggering a channel scan
 * @scan_work:		work item for discovering new channels
 * @state_work:		work item for edge state changes
 */
struct qcom_smd_edge {
	struct device dev;

	struct device_node *of_node;
	unsigned edge_id;
	unsigned remote_pid;

	int irq;

	struct regmap *ipc_regmap;
	int ipc_offset;
	int ipc_bit;

	struct list_head channels;
	spinlock_t channels_lock;

	DECLARE_BITMAP(allocated[SMD_ALLOC_TBL_COUNT], SMD_ALLOC_TBL_SIZE);

	unsigned smem_available;

	wait_queue_head_t new_channel_event;

	struct work_struct scan_work;
	struct work_struct state_work;
};

/*
 * SMD channel states.
 */
enum smd_channel_state {
	SMD_CHANNEL_CLOSED,
	SMD_CHANNEL_OPENING,
	SMD_CHANNEL_OPENED,
	SMD_CHANNEL_FLUSHING,
	SMD_CHANNEL_CLOSING,
	SMD_CHANNEL_RESET,
	SMD_CHANNEL_RESET_OPENING
};

struct qcom_smd_device {
	struct rpmsg_device rpdev;

	struct qcom_smd_edge *edge;
};

struct qcom_smd_endpoint {
	struct rpmsg_endpoint ept;

	struct qcom_smd_channel *qsch;
};

#define to_smd_device(_rpdev)	container_of(_rpdev, struct qcom_smd_device, rpdev)
#define to_smd_edge(d)		container_of(d, struct qcom_smd_edge, dev)
#define to_smd_endpoint(ept)	container_of(ept, struct qcom_smd_endpoint, ept)

/**
 * struct qcom_smd_channel - smd channel struct
 * @edge:		qcom_smd_edge this channel is living on
 * @qsdev:		reference to a associated smd client device
 * @name:		name of the channel
 * @state:		local state of the channel
 * @remote_state:	remote state of the channel
 * @info:		byte aligned outgoing/incoming channel info
 * @info_word:		word aligned outgoing/incoming channel info
 * @tx_lock:		lock to make writes to the channel mutually exclusive
 * @fblockread_event:	wakeup event tied to tx fBLOCKREADINTR
 * @tx_fifo:		pointer to the outgoing ring buffer
 * @rx_fifo:		pointer to the incoming ring buffer
 * @fifo_size:		size of each ring buffer
 * @bounce_buffer:	bounce buffer for reading wrapped packets
 * @cb:			callback function registered for this channel
 * @recv_lock:		guard for rx info modifications and cb pointer
 * @pkt_size:		size of the currently handled packet
 * @list:		lite entry for @channels in qcom_smd_edge
 */
struct qcom_smd_channel {
	struct qcom_smd_edge *edge;

	struct qcom_smd_endpoint *qsept;
	bool registered;

	char *name;
	enum smd_channel_state state;
	enum smd_channel_state remote_state;

	struct smd_channel_info_pair *info;
	struct smd_channel_info_word_pair *info_word;

	struct mutex tx_lock;
	wait_queue_head_t fblockread_event;

	void *tx_fifo;
	void *rx_fifo;
	int fifo_size;

	void *bounce_buffer;

	spinlock_t recv_lock;

	int pkt_size;

	void *drvdata;

	struct list_head list;
};

/*
 * Format of the smd_info smem items, for byte aligned channels.
 */
struct smd_channel_info {
	__le32 state;
	u8  fDSR;
	u8  fCTS;
	u8  fCD;
	u8  fRI;
	u8  fHEAD;
	u8  fTAIL;
	u8  fSTATE;
	u8  fBLOCKREADINTR;
	__le32 tail;
	__le32 head;
};

struct smd_channel_info_pair {
	struct smd_channel_info tx;
	struct smd_channel_info rx;
};

/*
 * Format of the smd_info smem items, for word aligned channels.
 */
struct smd_channel_info_word {
	__le32 state;
	__le32 fDSR;
	__le32 fCTS;
	__le32 fCD;
	__le32 fRI;
	__le32 fHEAD;
	__le32 fTAIL;
	__le32 fSTATE;
	__le32 fBLOCKREADINTR;
	__le32 tail;
	__le32 head;
};

struct smd_channel_info_word_pair {
	struct smd_channel_info_word tx;
	struct smd_channel_info_word rx;
};

#define GET_RX_CHANNEL_FLAG(channel, param)				     \
	({								     \
		BUILD_BUG_ON(sizeof(channel->info->rx.param) != sizeof(u8)); \
		channel->info_word ?					     \
			le32_to_cpu(channel->info_word->rx.param) :	     \
			channel->info->rx.param;			     \
	})

#define GET_RX_CHANNEL_INFO(channel, param)				      \
	({								      \
		BUILD_BUG_ON(sizeof(channel->info->rx.param) != sizeof(u32)); \
		le32_to_cpu(channel->info_word ?			      \
			channel->info_word->rx.param :			      \
			channel->info->rx.param);			      \
	})

#define SET_RX_CHANNEL_FLAG(channel, param, value)			     \
	({								     \
		BUILD_BUG_ON(sizeof(channel->info->rx.param) != sizeof(u8)); \
		if (channel->info_word)					     \
			channel->info_word->rx.param = cpu_to_le32(value);   \
		else							     \
			channel->info->rx.param = value;		     \
	})

#define SET_RX_CHANNEL_INFO(channel, param, value)			      \
	({								      \
		BUILD_BUG_ON(sizeof(channel->info->rx.param) != sizeof(u32)); \
		if (channel->info_word)					      \
			channel->info_word->rx.param = cpu_to_le32(value);    \
		else							      \
			channel->info->rx.param = cpu_to_le32(value);	      \
	})

#define GET_TX_CHANNEL_FLAG(channel, param)				     \
	({								     \
		BUILD_BUG_ON(sizeof(channel->info->tx.param) != sizeof(u8)); \
		channel->info_word ?					     \
			le32_to_cpu(channel->info_word->tx.param) :          \
			channel->info->tx.param;			     \
	})

#define GET_TX_CHANNEL_INFO(channel, param)				      \
	({								      \
		BUILD_BUG_ON(sizeof(channel->info->tx.param) != sizeof(u32)); \
		le32_to_cpu(channel->info_word ?			      \
			channel->info_word->tx.param :			      \
			channel->info->tx.param);			      \
	})

#define SET_TX_CHANNEL_FLAG(channel, param, value)			     \
	({								     \
		BUILD_BUG_ON(sizeof(channel->info->tx.param) != sizeof(u8)); \
		if (channel->info_word)					     \
			channel->info_word->tx.param = cpu_to_le32(value);   \
		else							     \
			channel->info->tx.param = value;		     \
	})

#define SET_TX_CHANNEL_INFO(channel, param, value)			      \
	({								      \
		BUILD_BUG_ON(sizeof(channel->info->tx.param) != sizeof(u32)); \
		if (channel->info_word)					      \
			channel->info_word->tx.param = cpu_to_le32(value);   \
		else							      \
			channel->info->tx.param = cpu_to_le32(value);	      \
	})

/**
 * struct qcom_smd_alloc_entry - channel allocation entry
 * @name:	channel name
 * @cid:	channel index
 * @flags:	channel flags and edge id
 * @ref_count:	reference count of the channel
 */
struct qcom_smd_alloc_entry {
	u8 name[20];
	__le32 cid;
	__le32 flags;
	__le32 ref_count;
} __packed;

#define SMD_CHANNEL_FLAGS_EDGE_MASK	0xff
#define SMD_CHANNEL_FLAGS_STREAM	BIT(8)
#define SMD_CHANNEL_FLAGS_PACKET	BIT(9)

/*
 * Each smd packet contains a 20 byte header, with the first 4 being the length
 * of the packet.
 */
#define SMD_PACKET_HEADER_LEN	20

/*
 * Signal the remote processor associated with 'channel'.
 */
static void qcom_smd_signal_channel(struct qcom_smd_channel *channel)
{
	struct qcom_smd_edge *edge = channel->edge;

	regmap_write(edge->ipc_regmap, edge->ipc_offset, BIT(edge->ipc_bit));
}

/*
 * Initialize the tx channel info
 */
static void qcom_smd_channel_reset(struct qcom_smd_channel *channel)
{
	SET_TX_CHANNEL_INFO(channel, state, SMD_CHANNEL_CLOSED);
	SET_TX_CHANNEL_FLAG(channel, fDSR, 0);
	SET_TX_CHANNEL_FLAG(channel, fCTS, 0);
	SET_TX_CHANNEL_FLAG(channel, fCD, 0);
	SET_TX_CHANNEL_FLAG(channel, fRI, 0);
	SET_TX_CHANNEL_FLAG(channel, fHEAD, 0);
	SET_TX_CHANNEL_FLAG(channel, fTAIL, 0);
	SET_TX_CHANNEL_FLAG(channel, fSTATE, 1);
	SET_TX_CHANNEL_FLAG(channel, fBLOCKREADINTR, 1);
	SET_TX_CHANNEL_INFO(channel, head, 0);
	SET_RX_CHANNEL_INFO(channel, tail, 0);

	qcom_smd_signal_channel(channel);

	channel->state = SMD_CHANNEL_CLOSED;
	channel->pkt_size = 0;
}

/*
 * Set the callback for a channel, with appropriate locking
 */
static void qcom_smd_channel_set_callback(struct qcom_smd_channel *channel,
					  rpmsg_rx_cb_t cb)
{
	struct rpmsg_endpoint *ept = &channel->qsept->ept;
	unsigned long flags;

	spin_lock_irqsave(&channel->recv_lock, flags);
	ept->cb = cb;
	spin_unlock_irqrestore(&channel->recv_lock, flags);
};

/*
 * Calculate the amount of data available in the rx fifo
 */
static size_t qcom_smd_channel_get_rx_avail(struct qcom_smd_channel *channel)
{
	unsigned head;
	unsigned tail;

	head = GET_RX_CHANNEL_INFO(channel, head);
	tail = GET_RX_CHANNEL_INFO(channel, tail);

	return (head - tail) & (channel->fifo_size - 1);
}

/*
 * Set tx channel state and inform the remote processor
 */
static void qcom_smd_channel_set_state(struct qcom_smd_channel *channel,
				       int state)
{
	struct qcom_smd_edge *edge = channel->edge;
	bool is_open = state == SMD_CHANNEL_OPENED;

	if (channel->state == state)
		return;

	dev_dbg(&edge->dev, "set_state(%s, %d)\n", channel->name, state);

	SET_TX_CHANNEL_FLAG(channel, fDSR, is_open);
	SET_TX_CHANNEL_FLAG(channel, fCTS, is_open);
	SET_TX_CHANNEL_FLAG(channel, fCD, is_open);

	SET_TX_CHANNEL_INFO(channel, state, state);
	SET_TX_CHANNEL_FLAG(channel, fSTATE, 1);

	channel->state = state;
	qcom_smd_signal_channel(channel);
}

/*
 * Copy count bytes of data using 32bit accesses, if that's required.
 */
static void smd_copy_to_fifo(void __iomem *dst,
			     const void *src,
			     size_t count,
			     bool word_aligned)
{
	if (word_aligned) {
		__iowrite32_copy(dst, src, count / sizeof(u32));
	} else {
		memcpy_toio(dst, src, count);
	}
}

/*
 * Copy count bytes of data using 32bit accesses, if that is required.
 */
static void smd_copy_from_fifo(void *dst,
			       const void __iomem *src,
			       size_t count,
			       bool word_aligned)
{
	if (word_aligned) {
		__ioread32_copy(dst, src, count / sizeof(u32));
	} else {
		memcpy_fromio(dst, src, count);
	}
}

/*
 * Read count bytes of data from the rx fifo into buf, but don't advance the
 * tail.
 */
static size_t qcom_smd_channel_peek(struct qcom_smd_channel *channel,
				    void *buf, size_t count)
{
	bool word_aligned;
	unsigned tail;
	size_t len;

	word_aligned = channel->info_word;
	tail = GET_RX_CHANNEL_INFO(channel, tail);

	len = min_t(size_t, count, channel->fifo_size - tail);
	if (len) {
		smd_copy_from_fifo(buf,
				   channel->rx_fifo + tail,
				   len,
				   word_aligned);
	}

	if (len != count) {
		smd_copy_from_fifo(buf + len,
				   channel->rx_fifo,
				   count - len,
				   word_aligned);
	}

	return count;
}

/*
 * Advance the rx tail by count bytes.
 */
static void qcom_smd_channel_advance(struct qcom_smd_channel *channel,
				     size_t count)
{
	unsigned tail;

	tail = GET_RX_CHANNEL_INFO(channel, tail);
	tail += count;
	tail &= (channel->fifo_size - 1);
	SET_RX_CHANNEL_INFO(channel, tail, tail);
}

/*
 * Read out a single packet from the rx fifo and deliver it to the device
 */
static int qcom_smd_channel_recv_single(struct qcom_smd_channel *channel)
{
	struct rpmsg_endpoint *ept = &channel->qsept->ept;
	unsigned tail;
	size_t len;
	void *ptr;
	int ret;

	tail = GET_RX_CHANNEL_INFO(channel, tail);

	/* Use bounce buffer if the data wraps */
	if (tail + channel->pkt_size >= channel->fifo_size) {
		ptr = channel->bounce_buffer;
		len = qcom_smd_channel_peek(channel, ptr, channel->pkt_size);
	} else {
		ptr = channel->rx_fifo + tail;
		len = channel->pkt_size;
	}

	ret = ept->cb(ept->rpdev, ptr, len, ept->priv, RPMSG_ADDR_ANY);
	if (ret < 0)
		return ret;

	/* Only forward the tail if the client consumed the data */
	qcom_smd_channel_advance(channel, len);

	channel->pkt_size = 0;

	return 0;
}

/*
 * Per channel interrupt handling
 */
static bool qcom_smd_channel_intr(struct qcom_smd_channel *channel)
{
	bool need_state_scan = false;
	int remote_state;
	__le32 pktlen;
	int avail;
	int ret;

	/* Handle state changes */
	remote_state = GET_RX_CHANNEL_INFO(channel, state);
	if (remote_state != channel->remote_state) {
		channel->remote_state = remote_state;
		need_state_scan = true;
	}
	/* Indicate that we have seen any state change */
	SET_RX_CHANNEL_FLAG(channel, fSTATE, 0);

	/* Signal waiting qcom_smd_send() about the interrupt */
	if (!GET_TX_CHANNEL_FLAG(channel, fBLOCKREADINTR))
		wake_up_interruptible(&channel->fblockread_event);

	/* Don't consume any data until we've opened the channel */
	if (channel->state != SMD_CHANNEL_OPENED)
		goto out;

	/* Indicate that we've seen the new data */
	SET_RX_CHANNEL_FLAG(channel, fHEAD, 0);

	/* Consume data */
	for (;;) {
		avail = qcom_smd_channel_get_rx_avail(channel);

		if (!channel->pkt_size && avail >= SMD_PACKET_HEADER_LEN) {
			qcom_smd_channel_peek(channel, &pktlen, sizeof(pktlen));
			qcom_smd_channel_advance(channel, SMD_PACKET_HEADER_LEN);
			channel->pkt_size = le32_to_cpu(pktlen);
		} else if (channel->pkt_size && avail >= channel->pkt_size) {
			ret = qcom_smd_channel_recv_single(channel);
			if (ret)
				break;
		} else {
			break;
		}
	}

	/* Indicate that we have seen and updated tail */
	SET_RX_CHANNEL_FLAG(channel, fTAIL, 1);

	/* Signal the remote that we've consumed the data (if requested) */
	if (!GET_RX_CHANNEL_FLAG(channel, fBLOCKREADINTR)) {
		/* Ensure ordering of channel info updates */
		wmb();

		qcom_smd_signal_channel(channel);
	}

out:
	return need_state_scan;
}

/*
 * The edge interrupts are triggered by the remote processor on state changes,
 * channel info updates or when new channels are created.
 */
static irqreturn_t qcom_smd_edge_intr(int irq, void *data)
{
	struct qcom_smd_edge *edge = data;
	struct qcom_smd_channel *channel;
	unsigned available;
	bool kick_scanner = false;
	bool kick_state = false;

	/*
	 * Handle state changes or data on each of the channels on this edge
	 */
	spin_lock(&edge->channels_lock);
	list_for_each_entry(channel, &edge->channels, list) {
		spin_lock(&channel->recv_lock);
		kick_state |= qcom_smd_channel_intr(channel);
		spin_unlock(&channel->recv_lock);
	}
	spin_unlock(&edge->channels_lock);

	/*
	 * Creating a new channel requires allocating an smem entry, so we only
	 * have to scan if the amount of available space in smem have changed
	 * since last scan.
	 */
	available = qcom_smem_get_free_space(edge->remote_pid);
	if (available != edge->smem_available) {
		edge->smem_available = available;
		kick_scanner = true;
	}

	if (kick_scanner)
		schedule_work(&edge->scan_work);
	if (kick_state)
		schedule_work(&edge->state_work);

	return IRQ_HANDLED;
}

/*
 * Calculate how much space is available in the tx fifo.
 */
static size_t qcom_smd_get_tx_avail(struct qcom_smd_channel *channel)
{
	unsigned head;
	unsigned tail;
	unsigned mask = channel->fifo_size - 1;

	head = GET_TX_CHANNEL_INFO(channel, head);
	tail = GET_TX_CHANNEL_INFO(channel, tail);

	return mask - ((head - tail) & mask);
}

/*
 * Write count bytes of data into channel, possibly wrapping in the ring buffer
 */
static int qcom_smd_write_fifo(struct qcom_smd_channel *channel,
			       const void *data,
			       size_t count)
{
	bool word_aligned;
	unsigned head;
	size_t len;

	word_aligned = channel->info_word;
	head = GET_TX_CHANNEL_INFO(channel, head);

	len = min_t(size_t, count, channel->fifo_size - head);
	if (len) {
		smd_copy_to_fifo(channel->tx_fifo + head,
				 data,
				 len,
				 word_aligned);
	}

	if (len != count) {
		smd_copy_to_fifo(channel->tx_fifo,
				 data + len,
				 count - len,
				 word_aligned);
	}

	head += count;
	head &= (channel->fifo_size - 1);
	SET_TX_CHANNEL_INFO(channel, head, head);

	return count;
}

/**
 * qcom_smd_send - write data to smd channel
 * @channel:	channel handle
 * @data:	buffer of data to write
 * @len:	number of bytes to write
 *
 * This is a blocking write of len bytes into the channel's tx ring buffer and
 * signal the remote end. It will sleep until there is enough space available
 * in the tx buffer, utilizing the fBLOCKREADINTR signaling mechanism to avoid
 * polling.
 */
static int __qcom_smd_send(struct qcom_smd_channel *channel, const void *data,
			   int len, bool wait)
{
	__le32 hdr[5] = { cpu_to_le32(len), };
	int tlen = sizeof(hdr) + len;
	int ret;

	/* Word aligned channels only accept word size aligned data */
	if (channel->info_word && len % 4)
		return -EINVAL;

	/* Reject packets that are too big */
	if (tlen >= channel->fifo_size)
		return -EINVAL;

	ret = mutex_lock_interruptible(&channel->tx_lock);
	if (ret)
		return ret;

	while (qcom_smd_get_tx_avail(channel) < tlen) {
		if (!wait) {
			ret = -EAGAIN;
			goto out;
		}

		if (channel->state != SMD_CHANNEL_OPENED) {
			ret = -EPIPE;
			goto out;
		}

		SET_TX_CHANNEL_FLAG(channel, fBLOCKREADINTR, 0);

		ret = wait_event_interruptible(channel->fblockread_event,
				       qcom_smd_get_tx_avail(channel) >= tlen ||
				       channel->state != SMD_CHANNEL_OPENED);
		if (ret)
			goto out;

		SET_TX_CHANNEL_FLAG(channel, fBLOCKREADINTR, 1);
	}

	SET_TX_CHANNEL_FLAG(channel, fTAIL, 0);

	qcom_smd_write_fifo(channel, hdr, sizeof(hdr));
	qcom_smd_write_fifo(channel, data, len);

	SET_TX_CHANNEL_FLAG(channel, fHEAD, 1);

	/* Ensure ordering of channel info updates */
	wmb();

	qcom_smd_signal_channel(channel);

out:
	mutex_unlock(&channel->tx_lock);

	return ret;
}

/*
 * Helper for opening a channel
 */
static int qcom_smd_channel_open(struct qcom_smd_channel *channel,
				 rpmsg_rx_cb_t cb)
{
	size_t bb_size;

	/*
	 * Packets are maximum 4k, but reduce if the fifo is smaller
	 */
	bb_size = min(channel->fifo_size, SZ_4K);
	channel->bounce_buffer = kmalloc(bb_size, GFP_KERNEL);
	if (!channel->bounce_buffer)
		return -ENOMEM;

	qcom_smd_channel_set_callback(channel, cb);
	qcom_smd_channel_set_state(channel, SMD_CHANNEL_OPENING);
	qcom_smd_channel_set_state(channel, SMD_CHANNEL_OPENED);

	return 0;
}

/*
 * Helper for closing and resetting a channel
 */
static void qcom_smd_channel_close(struct qcom_smd_channel *channel)
{
	qcom_smd_channel_set_callback(channel, NULL);

	kfree(channel->bounce_buffer);
	channel->bounce_buffer = NULL;

	qcom_smd_channel_set_state(channel, SMD_CHANNEL_CLOSED);
	qcom_smd_channel_reset(channel);
}

static struct qcom_smd_channel *
qcom_smd_find_channel(struct qcom_smd_edge *edge, const char *name)
{
	struct qcom_smd_channel *channel;
	struct qcom_smd_channel *ret = NULL;
	unsigned long flags;
	unsigned state;

	spin_lock_irqsave(&edge->channels_lock, flags);
	list_for_each_entry(channel, &edge->channels, list) {
		if (strcmp(channel->name, name))
			continue;

		state = GET_RX_CHANNEL_INFO(channel, state);
		if (state != SMD_CHANNEL_OPENING &&
		    state != SMD_CHANNEL_OPENED)
			continue;

		ret = channel;
		break;
	}
	spin_unlock_irqrestore(&edge->channels_lock, flags);

	return ret;
}

static void __ept_release(struct kref *kref)
{
	struct rpmsg_endpoint *ept = container_of(kref, struct rpmsg_endpoint,
						  refcount);
	kfree(to_smd_endpoint(ept));
}

static struct rpmsg_endpoint *qcom_smd_create_ept(struct rpmsg_device *rpdev,
						  rpmsg_rx_cb_t cb, void *priv,
						  struct rpmsg_channel_info chinfo)
{
	struct qcom_smd_endpoint *qsept;
	struct qcom_smd_channel *channel;
	struct qcom_smd_device *qsdev = to_smd_device(rpdev);
	struct qcom_smd_edge *edge = qsdev->edge;
	struct rpmsg_endpoint *ept;
	const char *name = chinfo.name;
	int ret;

	/* Wait up to HZ for the channel to appear */
	ret = wait_event_interruptible_timeout(edge->new_channel_event,
			(channel = qcom_smd_find_channel(edge, name)) != NULL,
			HZ);
	if (!ret)
		return NULL;

	if (channel->state != SMD_CHANNEL_CLOSED) {
		dev_err(&rpdev->dev, "channel %s is busy\n", channel->name);
		return NULL;
	}

	qsept = kzalloc(sizeof(*qsept), GFP_KERNEL);
	if (!qsept)
		return NULL;

	ept = &qsept->ept;

	kref_init(&ept->refcount);

	ept->rpdev = rpdev;
	ept->cb = cb;
	ept->priv = priv;
	ept->ops = &qcom_smd_endpoint_ops;

	channel->qsept = qsept;
	qsept->qsch = channel;

	ret = qcom_smd_channel_open(channel, cb);
	if (ret)
		goto free_ept;

	return ept;

free_ept:
	channel->qsept = NULL;
	kref_put(&ept->refcount, __ept_release);
	return NULL;
}

static void qcom_smd_destroy_ept(struct rpmsg_endpoint *ept)
{
	struct qcom_smd_endpoint *qsept = to_smd_endpoint(ept);
	struct qcom_smd_channel *ch = qsept->qsch;

	qcom_smd_channel_close(ch);
	ch->qsept = NULL;
	kref_put(&ept->refcount, __ept_release);
}

static int qcom_smd_send(struct rpmsg_endpoint *ept, void *data, int len)
{
	struct qcom_smd_endpoint *qsept = to_smd_endpoint(ept);

	return __qcom_smd_send(qsept->qsch, data, len, true);
}

static int qcom_smd_trysend(struct rpmsg_endpoint *ept, void *data, int len)
{
	struct qcom_smd_endpoint *qsept = to_smd_endpoint(ept);

	return __qcom_smd_send(qsept->qsch, data, len, false);
}

/*
 * Finds the device_node for the smd child interested in this channel.
 */
static struct device_node *qcom_smd_match_channel(struct device_node *edge_node,
						  const char *channel)
{
	struct device_node *child;
	const char *name;
	const char *key;
	int ret;

	for_each_available_child_of_node(edge_node, child) {
		key = "qcom,smd-channels";
		ret = of_property_read_string(child, key, &name);
		if (ret)
			continue;

		if (strcmp(name, channel) == 0)
			return child;
	}

	return NULL;
}

static const struct rpmsg_device_ops qcom_smd_device_ops = {
	.create_ept = qcom_smd_create_ept,
};

static const struct rpmsg_endpoint_ops qcom_smd_endpoint_ops = {
	.destroy_ept = qcom_smd_destroy_ept,
	.send = qcom_smd_send,
	.trysend = qcom_smd_trysend,
};

/*
 * Create a smd client device for channel that is being opened.
 */
static int qcom_smd_create_device(struct qcom_smd_channel *channel)
{
	struct qcom_smd_device *qsdev;
	struct rpmsg_device *rpdev;
	struct qcom_smd_edge *edge = channel->edge;

	dev_dbg(&edge->dev, "registering '%s'\n", channel->name);

	qsdev = kzalloc(sizeof(*qsdev), GFP_KERNEL);
	if (!qsdev)
		return -ENOMEM;

	/* Link qsdev to our SMD edge */
	qsdev->edge = edge;

	/* Assign callbacks for rpmsg_device */
	qsdev->rpdev.ops = &qcom_smd_device_ops;

	/* Assign public information to the rpmsg_device */
	rpdev = &qsdev->rpdev;
	strncpy(rpdev->id.name, channel->name, RPMSG_NAME_SIZE);
	rpdev->src = RPMSG_ADDR_ANY;
	rpdev->dst = RPMSG_ADDR_ANY;

	rpdev->dev.of_node = qcom_smd_match_channel(edge->of_node, channel->name);
	rpdev->dev.parent = &edge->dev;

	return rpmsg_register_device(rpdev);
}

/*
 * Allocate the qcom_smd_channel object for a newly found smd channel,
 * retrieving and validating the smem items involved.
 */
static struct qcom_smd_channel *qcom_smd_create_channel(struct qcom_smd_edge *edge,
							unsigned smem_info_item,
							unsigned smem_fifo_item,
							char *name)
{
	struct qcom_smd_channel *channel;
	size_t fifo_size;
	size_t info_size;
	void *fifo_base;
	void *info;
	int ret;

	channel = devm_kzalloc(&edge->dev, sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return ERR_PTR(-ENOMEM);

	channel->edge = edge;
	channel->name = devm_kstrdup(&edge->dev, name, GFP_KERNEL);
	if (!channel->name)
		return ERR_PTR(-ENOMEM);

	mutex_init(&channel->tx_lock);
	spin_lock_init(&channel->recv_lock);
	init_waitqueue_head(&channel->fblockread_event);

	info = qcom_smem_get(edge->remote_pid, smem_info_item, &info_size);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto free_name_and_channel;
	}

	/*
	 * Use the size of the item to figure out which channel info struct to
	 * use.
	 */
	if (info_size == 2 * sizeof(struct smd_channel_info_word)) {
		channel->info_word = info;
	} else if (info_size == 2 * sizeof(struct smd_channel_info)) {
		channel->info = info;
	} else {
		dev_err(&edge->dev,
			"channel info of size %zu not supported\n", info_size);
		ret = -EINVAL;
		goto free_name_and_channel;
	}

	fifo_base = qcom_smem_get(edge->remote_pid, smem_fifo_item, &fifo_size);
	if (IS_ERR(fifo_base)) {
		ret =  PTR_ERR(fifo_base);
		goto free_name_and_channel;
	}

	/* The channel consist of a rx and tx fifo of equal size */
	fifo_size /= 2;

	dev_dbg(&edge->dev, "new channel '%s' info-size: %zu fifo-size: %zu\n",
			  name, info_size, fifo_size);

	channel->tx_fifo = fifo_base;
	channel->rx_fifo = fifo_base + fifo_size;
	channel->fifo_size = fifo_size;

	qcom_smd_channel_reset(channel);

	return channel;

free_name_and_channel:
	devm_kfree(&edge->dev, channel->name);
	devm_kfree(&edge->dev, channel);

	return ERR_PTR(ret);
}

/*
 * Scans the allocation table for any newly allocated channels, calls
 * qcom_smd_create_channel() to create representations of these and add
 * them to the edge's list of channels.
 */
static void qcom_channel_scan_worker(struct work_struct *work)
{
	struct qcom_smd_edge *edge = container_of(work, struct qcom_smd_edge, scan_work);
	struct qcom_smd_alloc_entry *alloc_tbl;
	struct qcom_smd_alloc_entry *entry;
	struct qcom_smd_channel *channel;
	unsigned long flags;
	unsigned fifo_id;
	unsigned info_id;
	int tbl;
	int i;
	u32 eflags, cid;

	for (tbl = 0; tbl < SMD_ALLOC_TBL_COUNT; tbl++) {
		alloc_tbl = qcom_smem_get(edge->remote_pid,
				    smem_items[tbl].alloc_tbl_id, NULL);
		if (IS_ERR(alloc_tbl))
			continue;

		for (i = 0; i < SMD_ALLOC_TBL_SIZE; i++) {
			entry = &alloc_tbl[i];
			eflags = le32_to_cpu(entry->flags);
			if (test_bit(i, edge->allocated[tbl]))
				continue;

			if (entry->ref_count == 0)
				continue;

			if (!entry->name[0])
				continue;

			if (!(eflags & SMD_CHANNEL_FLAGS_PACKET))
				continue;

			if ((eflags & SMD_CHANNEL_FLAGS_EDGE_MASK) != edge->edge_id)
				continue;

			cid = le32_to_cpu(entry->cid);
			info_id = smem_items[tbl].info_base_id + cid;
			fifo_id = smem_items[tbl].fifo_base_id + cid;

			channel = qcom_smd_create_channel(edge, info_id, fifo_id, entry->name);
			if (IS_ERR(channel))
				continue;

			spin_lock_irqsave(&edge->channels_lock, flags);
			list_add(&channel->list, &edge->channels);
			spin_unlock_irqrestore(&edge->channels_lock, flags);

			dev_dbg(&edge->dev, "new channel found: '%s'\n", channel->name);
			set_bit(i, edge->allocated[tbl]);

			wake_up_interruptible(&edge->new_channel_event);
		}
	}

	schedule_work(&edge->state_work);
}

/*
 * This per edge worker scans smem for any new channels and register these. It
 * then scans all registered channels for state changes that should be handled
 * by creating or destroying smd client devices for the registered channels.
 *
 * LOCKING: edge->channels_lock only needs to cover the list operations, as the
 * worker is killed before any channels are deallocated
 */
static void qcom_channel_state_worker(struct work_struct *work)
{
	struct qcom_smd_channel *channel;
	struct qcom_smd_edge *edge = container_of(work,
						  struct qcom_smd_edge,
						  state_work);
	struct rpmsg_channel_info chinfo;
	unsigned remote_state;
	unsigned long flags;

	/*
	 * Register a device for any closed channel where the remote processor
	 * is showing interest in opening the channel.
	 */
	spin_lock_irqsave(&edge->channels_lock, flags);
	list_for_each_entry(channel, &edge->channels, list) {
		if (channel->state != SMD_CHANNEL_CLOSED)
			continue;

		remote_state = GET_RX_CHANNEL_INFO(channel, state);
		if (remote_state != SMD_CHANNEL_OPENING &&
		    remote_state != SMD_CHANNEL_OPENED)
			continue;

		if (channel->registered)
			continue;

		spin_unlock_irqrestore(&edge->channels_lock, flags);
		qcom_smd_create_device(channel);
		channel->registered = true;
		spin_lock_irqsave(&edge->channels_lock, flags);

		channel->registered = true;
	}

	/*
	 * Unregister the device for any channel that is opened where the
	 * remote processor is closing the channel.
	 */
	list_for_each_entry(channel, &edge->channels, list) {
		if (channel->state != SMD_CHANNEL_OPENING &&
		    channel->state != SMD_CHANNEL_OPENED)
			continue;

		remote_state = GET_RX_CHANNEL_INFO(channel, state);
		if (remote_state == SMD_CHANNEL_OPENING ||
		    remote_state == SMD_CHANNEL_OPENED)
			continue;

		spin_unlock_irqrestore(&edge->channels_lock, flags);

		strncpy(chinfo.name, channel->name, sizeof(chinfo.name));
		chinfo.src = RPMSG_ADDR_ANY;
		chinfo.dst = RPMSG_ADDR_ANY;
		rpmsg_unregister_device(&edge->dev, &chinfo);
		channel->registered = false;
		spin_lock_irqsave(&edge->channels_lock, flags);
	}
	spin_unlock_irqrestore(&edge->channels_lock, flags);
}

/*
 * Parses an of_node describing an edge.
 */
static int qcom_smd_parse_edge(struct device *dev,
			       struct device_node *node,
			       struct qcom_smd_edge *edge)
{
	struct device_node *syscon_np;
	const char *key;
	int irq;
	int ret;

	INIT_LIST_HEAD(&edge->channels);
	spin_lock_init(&edge->channels_lock);

	INIT_WORK(&edge->scan_work, qcom_channel_scan_worker);
	INIT_WORK(&edge->state_work, qcom_channel_state_worker);

	edge->of_node = of_node_get(node);

	key = "qcom,smd-edge";
	ret = of_property_read_u32(node, key, &edge->edge_id);
	if (ret) {
		dev_err(dev, "edge missing %s property\n", key);
		return -EINVAL;
	}

	edge->remote_pid = QCOM_SMEM_HOST_ANY;
	key = "qcom,remote-pid";
	of_property_read_u32(node, key, &edge->remote_pid);

	syscon_np = of_parse_phandle(node, "qcom,ipc", 0);
	if (!syscon_np) {
		dev_err(dev, "no qcom,ipc node\n");
		return -ENODEV;
	}

	edge->ipc_regmap = syscon_node_to_regmap(syscon_np);
	if (IS_ERR(edge->ipc_regmap))
		return PTR_ERR(edge->ipc_regmap);

	key = "qcom,ipc";
	ret = of_property_read_u32_index(node, key, 1, &edge->ipc_offset);
	if (ret < 0) {
		dev_err(dev, "no offset in %s\n", key);
		return -EINVAL;
	}

	ret = of_property_read_u32_index(node, key, 2, &edge->ipc_bit);
	if (ret < 0) {
		dev_err(dev, "no bit in %s\n", key);
		return -EINVAL;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (irq < 0) {
		dev_err(dev, "required smd interrupt missing\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, irq,
			       qcom_smd_edge_intr, IRQF_TRIGGER_RISING,
			       node->name, edge);
	if (ret) {
		dev_err(dev, "failed to request smd irq\n");
		return ret;
	}

	edge->irq = irq;

	return 0;
}

/*
 * Release function for an edge.
  * Reset the state of each associated channel and free the edge context.
 */
static void qcom_smd_edge_release(struct device *dev)
{
	struct qcom_smd_channel *channel;
	struct qcom_smd_edge *edge = to_smd_edge(dev);

	list_for_each_entry(channel, &edge->channels, list) {
		SET_RX_CHANNEL_INFO(channel, state, SMD_CHANNEL_CLOSED);
		SET_RX_CHANNEL_INFO(channel, head, 0);
		SET_RX_CHANNEL_INFO(channel, tail, 0);
	}

	kfree(edge);
}

/**
 * qcom_smd_register_edge() - register an edge based on an device_node
 * @parent:    parent device for the edge
 * @node:      device_node describing the edge
 *
 * Returns an edge reference, or negative ERR_PTR() on failure.
 */
struct qcom_smd_edge *qcom_smd_register_edge(struct device *parent,
					     struct device_node *node)
{
	struct qcom_smd_edge *edge;
	int ret;

	edge = kzalloc(sizeof(*edge), GFP_KERNEL);
	if (!edge)
		return ERR_PTR(-ENOMEM);

	init_waitqueue_head(&edge->new_channel_event);

	edge->dev.parent = parent;
	edge->dev.release = qcom_smd_edge_release;
	dev_set_name(&edge->dev, "%s:%s", dev_name(parent), node->name);
	ret = device_register(&edge->dev);
	if (ret) {
		pr_err("failed to register smd edge\n");
		return ERR_PTR(ret);
	}

	ret = qcom_smd_parse_edge(&edge->dev, node, edge);
	if (ret) {
		dev_err(&edge->dev, "failed to parse smd edge\n");
		goto unregister_dev;
	}

	schedule_work(&edge->scan_work);

	return edge;

unregister_dev:
	put_device(&edge->dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(qcom_smd_register_edge);

static int qcom_smd_remove_device(struct device *dev, void *data)
{
	device_unregister(dev);

	return 0;
}

/**
 * qcom_smd_unregister_edge() - release an edge and its children
 * @edge:      edge reference acquired from qcom_smd_register_edge
 */
int qcom_smd_unregister_edge(struct qcom_smd_edge *edge)
{
	int ret;

	disable_irq(edge->irq);
	cancel_work_sync(&edge->scan_work);
	cancel_work_sync(&edge->state_work);

	ret = device_for_each_child(&edge->dev, NULL, qcom_smd_remove_device);
	if (ret)
		dev_warn(&edge->dev, "can't remove smd device: %d\n", ret);

	device_unregister(&edge->dev);

	return 0;
}
EXPORT_SYMBOL(qcom_smd_unregister_edge);

static int qcom_smd_probe(struct platform_device *pdev)
{
	struct device_node *node;
	void *p;

	/* Wait for smem */
	p = qcom_smem_get(QCOM_SMEM_HOST_ANY, smem_items[0].alloc_tbl_id, NULL);
	if (PTR_ERR(p) == -EPROBE_DEFER)
		return PTR_ERR(p);

	for_each_available_child_of_node(pdev->dev.of_node, node)
		qcom_smd_register_edge(&pdev->dev, node);

	return 0;
}

static int qcom_smd_remove_edge(struct device *dev, void *data)
{
	struct qcom_smd_edge *edge = to_smd_edge(dev);

	return qcom_smd_unregister_edge(edge);
}

/*
 * Shut down all smd clients by making sure that each edge stops processing
 * events and scanning for new channels, then call destroy on the devices.
 */
static int qcom_smd_remove(struct platform_device *pdev)
{
	int ret;

	ret = device_for_each_child(&pdev->dev, NULL, qcom_smd_remove_edge);
	if (ret)
		dev_warn(&pdev->dev, "can't remove smd device: %d\n", ret);

	return ret;
}

static const struct of_device_id qcom_smd_of_match[] = {
	{ .compatible = "qcom,smd" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_smd_of_match);

static struct platform_driver qcom_smd_driver = {
	.probe = qcom_smd_probe,
	.remove = qcom_smd_remove,
	.driver = {
		.name = "qcom-smd",
		.of_match_table = qcom_smd_of_match,
	},
};

static int __init qcom_smd_init(void)
{
	return platform_driver_register(&qcom_smd_driver);
}
subsys_initcall(qcom_smd_init);

static void __exit qcom_smd_exit(void)
{
	platform_driver_unregister(&qcom_smd_driver);
}
module_exit(qcom_smd_exit);

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@sonymobile.com>");
MODULE_DESCRIPTION("Qualcomm Shared Memory Driver");
MODULE_LICENSE("GPL v2");
