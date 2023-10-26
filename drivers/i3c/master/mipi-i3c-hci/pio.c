// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/io.h>

#include "hci.h"
#include "cmd.h"
#include "ibi.h"


/*
 * PIO Access Area
 */

#define pio_reg_read(r)		readl(hci->PIO_regs + (PIO_##r))
#define pio_reg_write(r, v)	writel(v, hci->PIO_regs + (PIO_##r))

#define PIO_COMMAND_QUEUE_PORT		0x00
#define PIO_RESPONSE_QUEUE_PORT		0x04
#define PIO_XFER_DATA_PORT		0x08
#define PIO_IBI_PORT			0x0c

#define PIO_QUEUE_THLD_CTRL		0x10
#define QUEUE_IBI_STATUS_THLD		GENMASK(31, 24)
#define QUEUE_IBI_DATA_THLD		GENMASK(23, 16)
#define QUEUE_RESP_BUF_THLD		GENMASK(15, 8)
#define QUEUE_CMD_EMPTY_BUF_THLD	GENMASK(7, 0)

#define PIO_DATA_BUFFER_THLD_CTRL	0x14
#define DATA_RX_START_THLD		GENMASK(26, 24)
#define DATA_TX_START_THLD		GENMASK(18, 16)
#define DATA_RX_BUF_THLD		GENMASK(10, 8)
#define DATA_TX_BUF_THLD		GENMASK(2, 0)

#define PIO_QUEUE_SIZE			0x18
#define TX_DATA_BUFFER_SIZE		GENMASK(31, 24)
#define RX_DATA_BUFFER_SIZE		GENMASK(23, 16)
#define IBI_STATUS_SIZE			GENMASK(15, 8)
#define CR_QUEUE_SIZE			GENMASK(7, 0)

#define PIO_INTR_STATUS			0x20
#define PIO_INTR_STATUS_ENABLE		0x24
#define PIO_INTR_SIGNAL_ENABLE		0x28
#define PIO_INTR_FORCE			0x2c
#define STAT_TRANSFER_BLOCKED		BIT(25)
#define STAT_PERR_RESP_UFLOW		BIT(24)
#define STAT_PERR_CMD_OFLOW		BIT(23)
#define STAT_PERR_IBI_UFLOW		BIT(22)
#define STAT_PERR_RX_UFLOW		BIT(21)
#define STAT_PERR_TX_OFLOW		BIT(20)
#define STAT_ERR_RESP_QUEUE_FULL	BIT(19)
#define STAT_WARN_RESP_QUEUE_FULL	BIT(18)
#define STAT_ERR_IBI_QUEUE_FULL		BIT(17)
#define STAT_WARN_IBI_QUEUE_FULL	BIT(16)
#define STAT_ERR_RX_DATA_FULL		BIT(15)
#define STAT_WARN_RX_DATA_FULL		BIT(14)
#define STAT_ERR_TX_DATA_EMPTY		BIT(13)
#define STAT_WARN_TX_DATA_EMPTY		BIT(12)
#define STAT_TRANSFER_ERR		BIT(9)
#define STAT_WARN_INS_STOP_MODE		BIT(7)
#define STAT_TRANSFER_ABORT		BIT(5)
#define STAT_RESP_READY			BIT(4)
#define STAT_CMD_QUEUE_READY		BIT(3)
#define STAT_IBI_STATUS_THLD		BIT(2)
#define STAT_RX_THLD			BIT(1)
#define STAT_TX_THLD			BIT(0)

#define PIO_QUEUE_CUR_STATUS		0x38
#define CUR_IBI_Q_LEVEL			GENMASK(28, 20)
#define CUR_RESP_Q_LEVEL		GENMASK(18, 10)
#define CUR_CMD_Q_EMPTY_LEVEL		GENMASK(8, 0)

#define PIO_DATA_BUFFER_CUR_STATUS	0x3c
#define CUR_RX_BUF_LVL			GENMASK(26, 16)
#define CUR_TX_BUF_LVL			GENMASK(10, 0)

/*
 * Handy status bit combinations
 */

#define STAT_LATENCY_WARNINGS		(STAT_WARN_RESP_QUEUE_FULL | \
					 STAT_WARN_IBI_QUEUE_FULL | \
					 STAT_WARN_RX_DATA_FULL | \
					 STAT_WARN_TX_DATA_EMPTY | \
					 STAT_WARN_INS_STOP_MODE)

#define STAT_LATENCY_ERRORS		(STAT_ERR_RESP_QUEUE_FULL | \
					 STAT_ERR_IBI_QUEUE_FULL | \
					 STAT_ERR_RX_DATA_FULL | \
					 STAT_ERR_TX_DATA_EMPTY)

#define STAT_PROG_ERRORS		(STAT_TRANSFER_BLOCKED | \
					 STAT_PERR_RESP_UFLOW | \
					 STAT_PERR_CMD_OFLOW | \
					 STAT_PERR_IBI_UFLOW | \
					 STAT_PERR_RX_UFLOW | \
					 STAT_PERR_TX_OFLOW)

#define STAT_ALL_ERRORS			(STAT_TRANSFER_ABORT | \
					 STAT_TRANSFER_ERR | \
					 STAT_LATENCY_ERRORS | \
					 STAT_PROG_ERRORS)

struct hci_pio_dev_ibi_data {
	struct i3c_generic_ibi_pool *pool;
	unsigned int max_len;
};

struct hci_pio_ibi_data {
	struct i3c_ibi_slot *slot;
	void *data_ptr;
	unsigned int addr;
	unsigned int seg_len, seg_cnt;
	unsigned int max_len;
	bool last_seg;
};

struct hci_pio_data {
	spinlock_t lock;
	struct hci_xfer *curr_xfer, *xfer_queue;
	struct hci_xfer *curr_rx, *rx_queue;
	struct hci_xfer *curr_tx, *tx_queue;
	struct hci_xfer *curr_resp, *resp_queue;
	struct hci_pio_ibi_data ibi;
	unsigned int rx_thresh_size, tx_thresh_size;
	unsigned int max_ibi_thresh;
	u32 reg_queue_thresh;
	u32 enabled_irqs;
};

static int hci_pio_init(struct i3c_hci *hci)
{
	struct hci_pio_data *pio;
	u32 val, size_val, rx_thresh, tx_thresh, ibi_val;

	pio = kzalloc(sizeof(*pio), GFP_KERNEL);
	if (!pio)
		return -ENOMEM;

	hci->io_data = pio;
	spin_lock_init(&pio->lock);

	size_val = pio_reg_read(QUEUE_SIZE);
	dev_info(&hci->master.dev, "CMD/RESP FIFO = %ld entries\n",
		 FIELD_GET(CR_QUEUE_SIZE, size_val));
	dev_info(&hci->master.dev, "IBI FIFO = %ld bytes\n",
		 4 * FIELD_GET(IBI_STATUS_SIZE, size_val));
	dev_info(&hci->master.dev, "RX data FIFO = %d bytes\n",
		 4 * (2 << FIELD_GET(RX_DATA_BUFFER_SIZE, size_val)));
	dev_info(&hci->master.dev, "TX data FIFO = %d bytes\n",
		 4 * (2 << FIELD_GET(TX_DATA_BUFFER_SIZE, size_val)));

	/*
	 * Let's initialize data thresholds to half of the actual FIFO size.
	 * The start thresholds aren't used (set to 0) as the FIFO is always
	 * serviced before the corresponding command is queued.
	 */
	rx_thresh = FIELD_GET(RX_DATA_BUFFER_SIZE, size_val);
	tx_thresh = FIELD_GET(TX_DATA_BUFFER_SIZE, size_val);
	if (hci->version_major == 1) {
		/* those are expressed as 2^[n+1), so just sub 1 if not 0 */
		if (rx_thresh)
			rx_thresh -= 1;
		if (tx_thresh)
			tx_thresh -= 1;
		pio->rx_thresh_size = 2 << rx_thresh;
		pio->tx_thresh_size = 2 << tx_thresh;
	} else {
		/* size is 2^(n+1) and threshold is 2^n i.e. already halved */
		pio->rx_thresh_size = 1 << rx_thresh;
		pio->tx_thresh_size = 1 << tx_thresh;
	}
	val = FIELD_PREP(DATA_RX_BUF_THLD,   rx_thresh) |
	      FIELD_PREP(DATA_TX_BUF_THLD,   tx_thresh);
	pio_reg_write(DATA_BUFFER_THLD_CTRL, val);

	/*
	 * Let's raise an interrupt as soon as there is one free cmd slot
	 * or one available response or IBI. For IBI data let's use half the
	 * IBI queue size within allowed bounds.
	 */
	ibi_val = FIELD_GET(IBI_STATUS_SIZE, size_val);
	pio->max_ibi_thresh = clamp_val(ibi_val/2, 1, 63);
	val = FIELD_PREP(QUEUE_IBI_STATUS_THLD, 1) |
	      FIELD_PREP(QUEUE_IBI_DATA_THLD, pio->max_ibi_thresh) |
	      FIELD_PREP(QUEUE_RESP_BUF_THLD, 1) |
	      FIELD_PREP(QUEUE_CMD_EMPTY_BUF_THLD, 1);
	pio_reg_write(QUEUE_THLD_CTRL, val);
	pio->reg_queue_thresh = val;

	/* Disable all IRQs but allow all status bits */
	pio_reg_write(INTR_SIGNAL_ENABLE, 0x0);
	pio_reg_write(INTR_STATUS_ENABLE, 0xffffffff);

	/* Always accept error interrupts (will be activated on first xfer) */
	pio->enabled_irqs = STAT_ALL_ERRORS;

	return 0;
}

static void hci_pio_cleanup(struct i3c_hci *hci)
{
	struct hci_pio_data *pio = hci->io_data;

	pio_reg_write(INTR_SIGNAL_ENABLE, 0x0);

	if (pio) {
		DBG("status = %#x/%#x",
		    pio_reg_read(INTR_STATUS), pio_reg_read(INTR_SIGNAL_ENABLE));
		BUG_ON(pio->curr_xfer);
		BUG_ON(pio->curr_rx);
		BUG_ON(pio->curr_tx);
		BUG_ON(pio->curr_resp);
		kfree(pio);
		hci->io_data = NULL;
	}
}

static void hci_pio_write_cmd(struct i3c_hci *hci, struct hci_xfer *xfer)
{
	DBG("cmd_desc[%d] = 0x%08x", 0, xfer->cmd_desc[0]);
	DBG("cmd_desc[%d] = 0x%08x", 1, xfer->cmd_desc[1]);
	pio_reg_write(COMMAND_QUEUE_PORT, xfer->cmd_desc[0]);
	pio_reg_write(COMMAND_QUEUE_PORT, xfer->cmd_desc[1]);
	if (hci->cmd == &mipi_i3c_hci_cmd_v2) {
		DBG("cmd_desc[%d] = 0x%08x", 2, xfer->cmd_desc[2]);
		DBG("cmd_desc[%d] = 0x%08x", 3, xfer->cmd_desc[3]);
		pio_reg_write(COMMAND_QUEUE_PORT, xfer->cmd_desc[2]);
		pio_reg_write(COMMAND_QUEUE_PORT, xfer->cmd_desc[3]);
	}
}

static bool hci_pio_do_rx(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	struct hci_xfer *xfer = pio->curr_rx;
	unsigned int nr_words;
	u32 *p;

	p = xfer->data;
	p += (xfer->data_len - xfer->data_left) / 4;

	while (xfer->data_left >= 4) {
		/* bail out if FIFO hasn't reached the threshold value yet */
		if (!(pio_reg_read(INTR_STATUS) & STAT_RX_THLD))
			return false;
		nr_words = min(xfer->data_left / 4, pio->rx_thresh_size);
		/* extract data from FIFO */
		xfer->data_left -= nr_words * 4;
		DBG("now %d left %d", nr_words * 4, xfer->data_left);
		while (nr_words--)
			*p++ = pio_reg_read(XFER_DATA_PORT);
	}

	/* trailing data is retrieved upon response reception */
	return !xfer->data_left;
}

static void hci_pio_do_trailing_rx(struct i3c_hci *hci,
				   struct hci_pio_data *pio, unsigned int count)
{
	struct hci_xfer *xfer = pio->curr_rx;
	u32 *p;

	DBG("%d remaining", count);

	p = xfer->data;
	p += (xfer->data_len - xfer->data_left) / 4;

	if (count >= 4) {
		unsigned int nr_words = count / 4;
		/* extract data from FIFO */
		xfer->data_left -= nr_words * 4;
		DBG("now %d left %d", nr_words * 4, xfer->data_left);
		while (nr_words--)
			*p++ = pio_reg_read(XFER_DATA_PORT);
	}

	count &= 3;
	if (count) {
		/*
		 * There are trailing bytes in the last word.
		 * Fetch it and extract bytes in an endian independent way.
		 * Unlike the TX case, we must not write memory past the
		 * end of the destination buffer.
		 */
		u8 *p_byte = (u8 *)p;
		u32 data = pio_reg_read(XFER_DATA_PORT);

		xfer->data_word_before_partial = data;
		xfer->data_left -= count;
		data = (__force u32) cpu_to_le32(data);
		while (count--) {
			*p_byte++ = data;
			data >>= 8;
		}
	}
}

static bool hci_pio_do_tx(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	struct hci_xfer *xfer = pio->curr_tx;
	unsigned int nr_words;
	u32 *p;

	p = xfer->data;
	p += (xfer->data_len - xfer->data_left) / 4;

	while (xfer->data_left >= 4) {
		/* bail out if FIFO free space is below set threshold */
		if (!(pio_reg_read(INTR_STATUS) & STAT_TX_THLD))
			return false;
		/* we can fill up to that TX threshold */
		nr_words = min(xfer->data_left / 4, pio->tx_thresh_size);
		/* push data into the FIFO */
		xfer->data_left -= nr_words * 4;
		DBG("now %d left %d", nr_words * 4, xfer->data_left);
		while (nr_words--)
			pio_reg_write(XFER_DATA_PORT, *p++);
	}

	if (xfer->data_left) {
		/*
		 * There are trailing bytes to send. We can simply load
		 * them from memory as a word which will keep those bytes
		 * in their proper place even on a BE system. This will
		 * also get some bytes past the actual buffer but no one
		 * should care as they won't be sent out.
		 */
		if (!(pio_reg_read(INTR_STATUS) & STAT_TX_THLD))
			return false;
		DBG("trailing %d", xfer->data_left);
		pio_reg_write(XFER_DATA_PORT, *p);
		xfer->data_left = 0;
	}

	return true;
}

static bool hci_pio_process_rx(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	while (pio->curr_rx && hci_pio_do_rx(hci, pio))
		pio->curr_rx = pio->curr_rx->next_data;
	return !pio->curr_rx;
}

static bool hci_pio_process_tx(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	while (pio->curr_tx && hci_pio_do_tx(hci, pio))
		pio->curr_tx = pio->curr_tx->next_data;
	return !pio->curr_tx;
}

static void hci_pio_queue_data(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	struct hci_xfer *xfer = pio->curr_xfer;
	struct hci_xfer *prev_queue_tail;

	if (!xfer->data) {
		xfer->data_len = xfer->data_left = 0;
		return;
	}

	if (xfer->rnw) {
		prev_queue_tail = pio->rx_queue;
		pio->rx_queue = xfer;
		if (pio->curr_rx) {
			prev_queue_tail->next_data = xfer;
		} else {
			pio->curr_rx = xfer;
			if (!hci_pio_process_rx(hci, pio))
				pio->enabled_irqs |= STAT_RX_THLD;
		}
	} else {
		prev_queue_tail = pio->tx_queue;
		pio->tx_queue = xfer;
		if (pio->curr_tx) {
			prev_queue_tail->next_data = xfer;
		} else {
			pio->curr_tx = xfer;
			if (!hci_pio_process_tx(hci, pio))
				pio->enabled_irqs |= STAT_TX_THLD;
		}
	}
}

static void hci_pio_push_to_next_rx(struct i3c_hci *hci, struct hci_xfer *xfer,
				    unsigned int words_to_keep)
{
	u32 *from = xfer->data;
	u32 from_last;
	unsigned int received, count;

	received = (xfer->data_len - xfer->data_left) / 4;
	if ((xfer->data_len - xfer->data_left) & 3) {
		from_last = xfer->data_word_before_partial;
		received += 1;
	} else {
		from_last = from[received];
	}
	from += words_to_keep;
	count = received - words_to_keep;

	while (count) {
		unsigned int room, left, chunk, bytes_to_move;
		u32 last_word;

		xfer = xfer->next_data;
		if (!xfer) {
			dev_err(&hci->master.dev, "pushing RX data to unexistent xfer\n");
			return;
		}

		room = DIV_ROUND_UP(xfer->data_len, 4);
		left = DIV_ROUND_UP(xfer->data_left, 4);
		chunk = min(count, room);
		if (chunk > left) {
			hci_pio_push_to_next_rx(hci, xfer, chunk - left);
			left = chunk;
			xfer->data_left = left * 4;
		}

		bytes_to_move = xfer->data_len - xfer->data_left;
		if (bytes_to_move & 3) {
			/* preserve word  to become partial */
			u32 *p = xfer->data;

			xfer->data_word_before_partial = p[bytes_to_move / 4];
		}
		memmove(xfer->data + chunk, xfer->data, bytes_to_move);

		/* treat last word specially because of partial word issues */
		chunk -= 1;

		memcpy(xfer->data, from, chunk * 4);
		xfer->data_left -= chunk * 4;
		from += chunk;
		count -= chunk;

		last_word = (count == 1) ? from_last : *from++;
		if (xfer->data_left < 4) {
			/*
			 * Like in hci_pio_do_trailing_rx(), preserve original
			 * word to be stored partially then store bytes it
			 * in an endian independent way.
			 */
			u8 *p_byte = xfer->data;

			p_byte += chunk * 4;
			xfer->data_word_before_partial = last_word;
			last_word = (__force u32) cpu_to_le32(last_word);
			while (xfer->data_left--) {
				*p_byte++ = last_word;
				last_word >>= 8;
			}
		} else {
			u32 *p = xfer->data;

			p[chunk] = last_word;
			xfer->data_left -= 4;
		}
		count--;
	}
}

static void hci_pio_err(struct i3c_hci *hci, struct hci_pio_data *pio,
			u32 status);

static bool hci_pio_process_resp(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	while (pio->curr_resp &&
	       (pio_reg_read(INTR_STATUS) & STAT_RESP_READY)) {
		struct hci_xfer *xfer = pio->curr_resp;
		u32 resp = pio_reg_read(RESPONSE_QUEUE_PORT);
		unsigned int tid = RESP_TID(resp);

		DBG("resp = 0x%08x", resp);
		if (tid != xfer->cmd_tid) {
			dev_err(&hci->master.dev,
				"response tid=%d when expecting %d\n",
				tid, xfer->cmd_tid);
			/* let's pretend it is a prog error... any of them  */
			hci_pio_err(hci, pio, STAT_PROG_ERRORS);
			return false;
		}
		xfer->response = resp;

		if (pio->curr_rx == xfer) {
			/*
			 * Response availability implies RX completion.
			 * Retrieve trailing RX data if any.
			 * Note that short reads are possible.
			 */
			unsigned int received, expected, to_keep;

			received = xfer->data_len - xfer->data_left;
			expected = RESP_DATA_LENGTH(xfer->response);
			if (expected > received) {
				hci_pio_do_trailing_rx(hci, pio,
						       expected - received);
			} else if (received > expected) {
				/* we consumed data meant for next xfer */
				to_keep = DIV_ROUND_UP(expected, 4);
				hci_pio_push_to_next_rx(hci, xfer, to_keep);
			}

			/* then process the RX list pointer */
			if (hci_pio_process_rx(hci, pio))
				pio->enabled_irqs &= ~STAT_RX_THLD;
		}

		/*
		 * We're about to give back ownership of the xfer structure
		 * to the waiting instance. Make sure no reference to it
		 * still exists.
		 */
		if (pio->curr_rx == xfer) {
			DBG("short RX ?");
			pio->curr_rx = pio->curr_rx->next_data;
		} else if (pio->curr_tx == xfer) {
			DBG("short TX ?");
			pio->curr_tx = pio->curr_tx->next_data;
		} else if (xfer->data_left) {
			DBG("PIO xfer count = %d after response",
			    xfer->data_left);
		}

		pio->curr_resp = xfer->next_resp;
		if (xfer->completion)
			complete(xfer->completion);
	}
	return !pio->curr_resp;
}

static void hci_pio_queue_resp(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	struct hci_xfer *xfer = pio->curr_xfer;
	struct hci_xfer *prev_queue_tail;

	if (!(xfer->cmd_desc[0] & CMD_0_ROC))
		return;

	prev_queue_tail = pio->resp_queue;
	pio->resp_queue = xfer;
	if (pio->curr_resp) {
		prev_queue_tail->next_resp = xfer;
	} else {
		pio->curr_resp = xfer;
		if (!hci_pio_process_resp(hci, pio))
			pio->enabled_irqs |= STAT_RESP_READY;
	}
}

static bool hci_pio_process_cmd(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	while (pio->curr_xfer &&
	       (pio_reg_read(INTR_STATUS) & STAT_CMD_QUEUE_READY)) {
		/*
		 * Always process the data FIFO before sending the command
		 * so needed TX data or RX space is available upfront.
		 */
		hci_pio_queue_data(hci, pio);
		/*
		 * Then queue our response request. This will also process
		 * the response FIFO in case it got suddenly filled up
		 * with results from previous commands.
		 */
		hci_pio_queue_resp(hci, pio);
		/*
		 * Finally send the command.
		 */
		hci_pio_write_cmd(hci, pio->curr_xfer);
		/*
		 * And move on.
		 */
		pio->curr_xfer = pio->curr_xfer->next_xfer;
	}
	return !pio->curr_xfer;
}

static int hci_pio_queue_xfer(struct i3c_hci *hci, struct hci_xfer *xfer, int n)
{
	struct hci_pio_data *pio = hci->io_data;
	struct hci_xfer *prev_queue_tail;
	int i;

	DBG("n = %d", n);

	/* link xfer instances together and initialize data count */
	for (i = 0; i < n; i++) {
		xfer[i].next_xfer = (i + 1 < n) ? &xfer[i + 1] : NULL;
		xfer[i].next_data = NULL;
		xfer[i].next_resp = NULL;
		xfer[i].data_left = xfer[i].data_len;
	}

	spin_lock_irq(&pio->lock);
	prev_queue_tail = pio->xfer_queue;
	pio->xfer_queue = &xfer[n - 1];
	if (pio->curr_xfer) {
		prev_queue_tail->next_xfer = xfer;
	} else {
		pio->curr_xfer = xfer;
		if (!hci_pio_process_cmd(hci, pio))
			pio->enabled_irqs |= STAT_CMD_QUEUE_READY;
		pio_reg_write(INTR_SIGNAL_ENABLE, pio->enabled_irqs);
		DBG("status = %#x/%#x",
		    pio_reg_read(INTR_STATUS), pio_reg_read(INTR_SIGNAL_ENABLE));
	}
	spin_unlock_irq(&pio->lock);
	return 0;
}

static bool hci_pio_dequeue_xfer_common(struct i3c_hci *hci,
					struct hci_pio_data *pio,
					struct hci_xfer *xfer, int n)
{
	struct hci_xfer *p, **p_prev_next;
	int i;

	/*
	 * To safely dequeue a transfer request, it must be either entirely
	 * processed, or not yet processed at all. If our request tail is
	 * reachable from either the data or resp list that means the command
	 * was submitted and not yet completed.
	 */
	for (p = pio->curr_resp; p; p = p->next_resp)
		for (i = 0; i < n; i++)
			if (p == &xfer[i])
				goto pio_screwed;
	for (p = pio->curr_rx; p; p = p->next_data)
		for (i = 0; i < n; i++)
			if (p == &xfer[i])
				goto pio_screwed;
	for (p = pio->curr_tx; p; p = p->next_data)
		for (i = 0; i < n; i++)
			if (p == &xfer[i])
				goto pio_screwed;

	/*
	 * The command was completed, or wasn't yet submitted.
	 * Unlink it from the que if the later.
	 */
	p_prev_next = &pio->curr_xfer;
	for (p = pio->curr_xfer; p; p = p->next_xfer) {
		if (p == &xfer[0]) {
			*p_prev_next = xfer[n - 1].next_xfer;
			break;
		}
		p_prev_next = &p->next_xfer;
	}

	/* return true if we actually unqueued something */
	return !!p;

pio_screwed:
	/*
	 * Life is tough. We must invalidate the hardware state and
	 * discard everything that is still queued.
	 */
	for (p = pio->curr_resp; p; p = p->next_resp) {
		p->response = FIELD_PREP(RESP_ERR_FIELD, RESP_ERR_HC_TERMINATED);
		if (p->completion)
			complete(p->completion);
	}
	for (p = pio->curr_xfer; p; p = p->next_xfer) {
		p->response = FIELD_PREP(RESP_ERR_FIELD, RESP_ERR_HC_TERMINATED);
		if (p->completion)
			complete(p->completion);
	}
	pio->curr_xfer = pio->curr_rx = pio->curr_tx = pio->curr_resp = NULL;

	return true;
}

static bool hci_pio_dequeue_xfer(struct i3c_hci *hci, struct hci_xfer *xfer, int n)
{
	struct hci_pio_data *pio = hci->io_data;
	int ret;

	spin_lock_irq(&pio->lock);
	DBG("n=%d status=%#x/%#x", n,
	    pio_reg_read(INTR_STATUS), pio_reg_read(INTR_SIGNAL_ENABLE));
	DBG("main_status = %#x/%#x",
	    readl(hci->base_regs + 0x20), readl(hci->base_regs + 0x28));

	ret = hci_pio_dequeue_xfer_common(hci, pio, xfer, n);
	spin_unlock_irq(&pio->lock);
	return ret;
}

static void hci_pio_err(struct i3c_hci *hci, struct hci_pio_data *pio,
			u32 status)
{
	/* TODO: this ought to be more sophisticated eventually */

	if (pio_reg_read(INTR_STATUS) & STAT_RESP_READY) {
		/* this may happen when an error is signaled with ROC unset */
		u32 resp = pio_reg_read(RESPONSE_QUEUE_PORT);

		dev_err(&hci->master.dev,
			"orphan response (%#x) on error\n", resp);
	}

	/* dump states on programming errors */
	if (status & STAT_PROG_ERRORS) {
		u32 queue = pio_reg_read(QUEUE_CUR_STATUS);
		u32 data = pio_reg_read(DATA_BUFFER_CUR_STATUS);

		dev_err(&hci->master.dev,
			"prog error %#lx (C/R/I = %ld/%ld/%ld, TX/RX = %ld/%ld)\n",
			status & STAT_PROG_ERRORS,
			FIELD_GET(CUR_CMD_Q_EMPTY_LEVEL, queue),
			FIELD_GET(CUR_RESP_Q_LEVEL, queue),
			FIELD_GET(CUR_IBI_Q_LEVEL, queue),
			FIELD_GET(CUR_TX_BUF_LVL, data),
			FIELD_GET(CUR_RX_BUF_LVL, data));
	}

	/* just bust out everything with pending responses for now */
	hci_pio_dequeue_xfer_common(hci, pio, pio->curr_resp, 1);
	/* ... and half-way TX transfers if any */
	if (pio->curr_tx && pio->curr_tx->data_left != pio->curr_tx->data_len)
		hci_pio_dequeue_xfer_common(hci, pio, pio->curr_tx, 1);
	/* then reset the hardware */
	mipi_i3c_hci_pio_reset(hci);
	mipi_i3c_hci_resume(hci);

	DBG("status=%#x/%#x",
	    pio_reg_read(INTR_STATUS), pio_reg_read(INTR_SIGNAL_ENABLE));
}

static void hci_pio_set_ibi_thresh(struct i3c_hci *hci,
				   struct hci_pio_data *pio,
				   unsigned int thresh_val)
{
	u32 regval = pio->reg_queue_thresh;

	regval &= ~QUEUE_IBI_STATUS_THLD;
	regval |= FIELD_PREP(QUEUE_IBI_STATUS_THLD, thresh_val);
	/* write the threshold reg only if it changes */
	if (regval != pio->reg_queue_thresh) {
		pio_reg_write(QUEUE_THLD_CTRL, regval);
		pio->reg_queue_thresh = regval;
		DBG("%d", thresh_val);
	}
}

static bool hci_pio_get_ibi_segment(struct i3c_hci *hci,
				    struct hci_pio_data *pio)
{
	struct hci_pio_ibi_data *ibi = &pio->ibi;
	unsigned int nr_words, thresh_val;
	u32 *p;

	p = ibi->data_ptr;
	p += (ibi->seg_len - ibi->seg_cnt) / 4;

	while ((nr_words = ibi->seg_cnt/4)) {
		/* determine our IBI queue threshold value */
		thresh_val = min(nr_words, pio->max_ibi_thresh);
		hci_pio_set_ibi_thresh(hci, pio, thresh_val);
		/* bail out if we don't have that amount of data ready */
		if (!(pio_reg_read(INTR_STATUS) & STAT_IBI_STATUS_THLD))
			return false;
		/* extract the data from the IBI port */
		nr_words = thresh_val;
		ibi->seg_cnt -= nr_words * 4;
		DBG("now %d left %d", nr_words * 4, ibi->seg_cnt);
		while (nr_words--)
			*p++ = pio_reg_read(IBI_PORT);
	}

	if (ibi->seg_cnt) {
		/*
		 * There are trailing bytes in the last word.
		 * Fetch it and extract bytes in an endian independent way.
		 * Unlike the TX case, we must not write past the end of
		 * the destination buffer.
		 */
		u32 data;
		u8 *p_byte = (u8 *)p;

		hci_pio_set_ibi_thresh(hci, pio, 1);
		DBG("trailing %d", ibi->seg_cnt);
		data = pio_reg_read(IBI_PORT);
		data = (__force u32) cpu_to_le32(data);
		while (ibi->seg_cnt--) {
			*p_byte++ = data;
			data >>= 8;
		}
	}

	return true;
}

static bool hci_pio_prep_new_ibi(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	struct hci_pio_ibi_data *ibi = &pio->ibi;
	struct i3c_dev_desc *dev;
	struct i3c_hci_dev_data *dev_data;
	struct hci_pio_dev_ibi_data *dev_ibi;
	u32 ibi_status;

	/*
	 * We have a new IBI. Try to set up its payload retrieval.
	 * When returning true, the IBI data has to be consumed whether
	 * or not we are set up to capture it. If we return true with
	 * ibi->slot == NULL that means the data payload has to be
	 * drained out of the IBI port and dropped.
	 */

	ibi_status = pio_reg_read(IBI_PORT);
	DBG("status = %#x", ibi_status);
	ibi->addr = FIELD_GET(IBI_TARGET_ADDR, ibi_status);
	if (ibi_status & IBI_ERROR) {
		dev_err(&hci->master.dev, "IBI error from %#x\n", ibi->addr);
		return false;
	}

	ibi->last_seg = ibi_status & IBI_LAST_STATUS;
	ibi->seg_len = FIELD_GET(IBI_DATA_LENGTH, ibi_status);
	ibi->seg_cnt = ibi->seg_len;

	dev = i3c_hci_addr_to_dev(hci, ibi->addr);
	if (!dev) {
		dev_err(&hci->master.dev,
			"IBI for unknown device %#x\n", ibi->addr);
		return true;
	}

	dev_data = i3c_dev_get_master_data(dev);
	dev_ibi = dev_data->ibi_data;
	ibi->max_len = dev_ibi->max_len;

	if (ibi->seg_len > ibi->max_len) {
		dev_err(&hci->master.dev, "IBI payload too big (%d > %d)\n",
			ibi->seg_len, ibi->max_len);
		return true;
	}

	ibi->slot = i3c_generic_ibi_get_free_slot(dev_ibi->pool);
	if (!ibi->slot) {
		dev_err(&hci->master.dev, "no free slot for IBI\n");
	} else {
		ibi->slot->len = 0;
		ibi->data_ptr = ibi->slot->data;
	}
	return true;
}

static void hci_pio_free_ibi_slot(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	struct hci_pio_ibi_data *ibi = &pio->ibi;
	struct hci_pio_dev_ibi_data *dev_ibi;

	if (ibi->slot) {
		dev_ibi = ibi->slot->dev->common.master_priv;
		i3c_generic_ibi_recycle_slot(dev_ibi->pool, ibi->slot);
		ibi->slot = NULL;
	}
}

static bool hci_pio_process_ibi(struct i3c_hci *hci, struct hci_pio_data *pio)
{
	struct hci_pio_ibi_data *ibi = &pio->ibi;

	if (!ibi->slot && !ibi->seg_cnt && ibi->last_seg)
		if (!hci_pio_prep_new_ibi(hci, pio))
			return false;

	for (;;) {
		u32 ibi_status;
		unsigned int ibi_addr;

		if (ibi->slot) {
			if (!hci_pio_get_ibi_segment(hci, pio))
				return false;
			ibi->slot->len += ibi->seg_len;
			ibi->data_ptr += ibi->seg_len;
			if (ibi->last_seg) {
				/* was the last segment: submit it and leave */
				i3c_master_queue_ibi(ibi->slot->dev, ibi->slot);
				ibi->slot = NULL;
				hci_pio_set_ibi_thresh(hci, pio, 1);
				return true;
			}
		} else if (ibi->seg_cnt) {
			/*
			 * No slot but a non-zero count. This is the result
			 * of some error and the payload must be drained.
			 * This normally does not happen therefore no need
			 * to be extra optimized here.
			 */
			hci_pio_set_ibi_thresh(hci, pio, 1);
			do {
				if (!(pio_reg_read(INTR_STATUS) & STAT_IBI_STATUS_THLD))
					return false;
				pio_reg_read(IBI_PORT);
			} while (--ibi->seg_cnt);
			if (ibi->last_seg)
				return true;
		}

		/* try to move to the next segment right away */
		hci_pio_set_ibi_thresh(hci, pio, 1);
		if (!(pio_reg_read(INTR_STATUS) & STAT_IBI_STATUS_THLD))
			return false;
		ibi_status = pio_reg_read(IBI_PORT);
		ibi_addr = FIELD_GET(IBI_TARGET_ADDR, ibi_status);
		if (ibi->addr != ibi_addr) {
			/* target address changed before last segment */
			dev_err(&hci->master.dev,
				"unexp IBI address changed from %d to %d\n",
				ibi->addr, ibi_addr);
			hci_pio_free_ibi_slot(hci, pio);
		}
		ibi->last_seg = ibi_status & IBI_LAST_STATUS;
		ibi->seg_len = FIELD_GET(IBI_DATA_LENGTH, ibi_status);
		ibi->seg_cnt = ibi->seg_len;
		if (ibi->slot && ibi->slot->len + ibi->seg_len > ibi->max_len) {
			dev_err(&hci->master.dev,
				"IBI payload too big (%d > %d)\n",
				ibi->slot->len + ibi->seg_len, ibi->max_len);
			hci_pio_free_ibi_slot(hci, pio);
		}
	}

	return false;
}

static int hci_pio_request_ibi(struct i3c_hci *hci, struct i3c_dev_desc *dev,
			       const struct i3c_ibi_setup *req)
{
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
	struct i3c_generic_ibi_pool *pool;
	struct hci_pio_dev_ibi_data *dev_ibi;
	struct hci_pio_data *pio = hci->io_data;

	dev_ibi = kmalloc(sizeof(*dev_ibi), GFP_KERNEL);
	if (!dev_ibi)
		return -ENOMEM;
	pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(pool)) {
		kfree(dev_ibi);
		return PTR_ERR(pool);
	}
	dev_ibi->pool = pool;
	dev_ibi->max_len = req->max_payload_len;
	dev_data->ibi_data = dev_ibi;
	pio->enabled_irqs |= STAT_IBI_STATUS_THLD;
	pio_reg_write(INTR_SIGNAL_ENABLE, pio->enabled_irqs);
	return 0;
}

static void hci_pio_free_ibi(struct i3c_hci *hci, struct i3c_dev_desc *dev)
{
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
	struct hci_pio_dev_ibi_data *dev_ibi = dev_data->ibi_data;

	dev_data->ibi_data = NULL;
	i3c_generic_ibi_free_pool(dev_ibi->pool);
	kfree(dev_ibi);
}

static void hci_pio_recycle_ibi_slot(struct i3c_hci *hci,
				    struct i3c_dev_desc *dev,
				    struct i3c_ibi_slot *slot)
{
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
	struct hci_pio_dev_ibi_data *dev_ibi = dev_data->ibi_data;

	i3c_generic_ibi_recycle_slot(dev_ibi->pool, slot);
}

static bool hci_pio_irq_handler(struct i3c_hci *hci, unsigned int unused)
{
	struct hci_pio_data *pio = hci->io_data;
	u32 status;

	spin_lock(&pio->lock);
	status = pio_reg_read(INTR_STATUS);
	DBG("(in) status: %#x/%#x", status, pio->enabled_irqs);
	status &= pio->enabled_irqs | STAT_LATENCY_WARNINGS;
	if (!status) {
		spin_unlock(&pio->lock);
		return false;
	}

	if (status & STAT_IBI_STATUS_THLD)
		hci_pio_process_ibi(hci, pio);

	if (status & STAT_RX_THLD)
		if (hci_pio_process_rx(hci, pio))
			pio->enabled_irqs &= ~STAT_RX_THLD;
	if (status & STAT_TX_THLD)
		if (hci_pio_process_tx(hci, pio))
			pio->enabled_irqs &= ~STAT_TX_THLD;
	if (status & STAT_RESP_READY)
		if (hci_pio_process_resp(hci, pio))
			pio->enabled_irqs &= ~STAT_RESP_READY;

	if (unlikely(status & STAT_LATENCY_WARNINGS)) {
		pio_reg_write(INTR_STATUS, status & STAT_LATENCY_WARNINGS);
		dev_warn_ratelimited(&hci->master.dev,
				     "encountered warning condition %#lx\n",
				     status & STAT_LATENCY_WARNINGS);
	}

	if (unlikely(status & STAT_ALL_ERRORS)) {
		pio_reg_write(INTR_STATUS, status & STAT_ALL_ERRORS);
		hci_pio_err(hci, pio, status & STAT_ALL_ERRORS);
	}

	if (status & STAT_CMD_QUEUE_READY)
		if (hci_pio_process_cmd(hci, pio))
			pio->enabled_irqs &= ~STAT_CMD_QUEUE_READY;

	pio_reg_write(INTR_SIGNAL_ENABLE, pio->enabled_irqs);
	DBG("(out) status: %#x/%#x",
	    pio_reg_read(INTR_STATUS), pio_reg_read(INTR_SIGNAL_ENABLE));
	spin_unlock(&pio->lock);
	return true;
}

const struct hci_io_ops mipi_i3c_hci_pio = {
	.init			= hci_pio_init,
	.cleanup		= hci_pio_cleanup,
	.queue_xfer		= hci_pio_queue_xfer,
	.dequeue_xfer		= hci_pio_dequeue_xfer,
	.irq_handler		= hci_pio_irq_handler,
	.request_ibi		= hci_pio_request_ibi,
	.free_ibi		= hci_pio_free_ibi,
	.recycle_ibi_slot	= hci_pio_recycle_ibi_slot,
};
