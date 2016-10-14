/*
 * Copyright (c) 2009-2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014, Sony Mobile Communications AB.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>

/* QUP Registers */
#define QUP_CONFIG		0x000
#define QUP_STATE		0x004
#define QUP_IO_MODE		0x008
#define QUP_SW_RESET		0x00c
#define QUP_OPERATIONAL		0x018
#define QUP_ERROR_FLAGS		0x01c
#define QUP_ERROR_FLAGS_EN	0x020
#define QUP_OPERATIONAL_MASK	0x028
#define QUP_HW_VERSION		0x030
#define QUP_MX_OUTPUT_CNT	0x100
#define QUP_OUT_FIFO_BASE	0x110
#define QUP_MX_WRITE_CNT	0x150
#define QUP_MX_INPUT_CNT	0x200
#define QUP_MX_READ_CNT		0x208
#define QUP_IN_FIFO_BASE	0x218
#define QUP_I2C_CLK_CTL		0x400
#define QUP_I2C_STATUS		0x404
#define QUP_I2C_MASTER_GEN	0x408

/* QUP States and reset values */
#define QUP_RESET_STATE		0
#define QUP_RUN_STATE		1
#define QUP_PAUSE_STATE		3
#define QUP_STATE_MASK		3

#define QUP_STATE_VALID		BIT(2)
#define QUP_I2C_MAST_GEN	BIT(4)
#define QUP_I2C_FLUSH		BIT(6)

#define QUP_OPERATIONAL_RESET	0x000ff0
#define QUP_I2C_STATUS_RESET	0xfffffc

/* QUP OPERATIONAL FLAGS */
#define QUP_I2C_NACK_FLAG	BIT(3)
#define QUP_OUT_NOT_EMPTY	BIT(4)
#define QUP_IN_NOT_EMPTY	BIT(5)
#define QUP_OUT_FULL		BIT(6)
#define QUP_OUT_SVC_FLAG	BIT(8)
#define QUP_IN_SVC_FLAG		BIT(9)
#define QUP_MX_OUTPUT_DONE	BIT(10)
#define QUP_MX_INPUT_DONE	BIT(11)

/* I2C mini core related values */
#define QUP_CLOCK_AUTO_GATE	BIT(13)
#define I2C_MINI_CORE		(2 << 8)
#define I2C_N_VAL		15
#define I2C_N_VAL_V2		7

/* Most significant word offset in FIFO port */
#define QUP_MSW_SHIFT		(I2C_N_VAL + 1)

/* Packing/Unpacking words in FIFOs, and IO modes */
#define QUP_OUTPUT_BLK_MODE	(1 << 10)
#define QUP_OUTPUT_BAM_MODE	(3 << 10)
#define QUP_INPUT_BLK_MODE	(1 << 12)
#define QUP_INPUT_BAM_MODE	(3 << 12)
#define QUP_BAM_MODE		(QUP_OUTPUT_BAM_MODE | QUP_INPUT_BAM_MODE)
#define QUP_UNPACK_EN		BIT(14)
#define QUP_PACK_EN		BIT(15)

#define QUP_REPACK_EN		(QUP_UNPACK_EN | QUP_PACK_EN)
#define QUP_V2_TAGS_EN		1

#define QUP_OUTPUT_BLOCK_SIZE(x)(((x) >> 0) & 0x03)
#define QUP_OUTPUT_FIFO_SIZE(x)	(((x) >> 2) & 0x07)
#define QUP_INPUT_BLOCK_SIZE(x)	(((x) >> 5) & 0x03)
#define QUP_INPUT_FIFO_SIZE(x)	(((x) >> 7) & 0x07)

/* QUP tags */
#define QUP_TAG_START		(1 << 8)
#define QUP_TAG_DATA		(2 << 8)
#define QUP_TAG_STOP		(3 << 8)
#define QUP_TAG_REC		(4 << 8)
#define QUP_BAM_INPUT_EOT		0x93
#define QUP_BAM_FLUSH_STOP		0x96

/* QUP v2 tags */
#define QUP_TAG_V2_START               0x81
#define QUP_TAG_V2_DATAWR              0x82
#define QUP_TAG_V2_DATAWR_STOP         0x83
#define QUP_TAG_V2_DATARD              0x85
#define QUP_TAG_V2_DATARD_STOP         0x87

/* Status, Error flags */
#define I2C_STATUS_WR_BUFFER_FULL	BIT(0)
#define I2C_STATUS_BUS_ACTIVE		BIT(8)
#define I2C_STATUS_ERROR_MASK		0x38000fc
#define QUP_STATUS_ERROR_FLAGS		0x7c

#define QUP_READ_LIMIT			256
#define SET_BIT				0x1
#define RESET_BIT			0x0
#define ONE_BYTE			0x1
#define QUP_I2C_MX_CONFIG_DURING_RUN   BIT(31)

#define MX_TX_RX_LEN			SZ_64K
#define MX_BLOCKS			(MX_TX_RX_LEN / QUP_READ_LIMIT)

/* Max timeout in ms for 32k bytes */
#define TOUT_MAX			300

struct qup_i2c_block {
	int	count;
	int	pos;
	int	tx_tag_len;
	int	rx_tag_len;
	int	data_len;
	u8	tags[6];
};

struct qup_i2c_tag {
	u8 *start;
	dma_addr_t addr;
};

struct qup_i2c_bam {
	struct	qup_i2c_tag tag;
	struct	dma_chan *dma;
	struct	scatterlist *sg;
};

struct qup_i2c_dev {
	struct device		*dev;
	void __iomem		*base;
	int			irq;
	struct clk		*clk;
	struct clk		*pclk;
	struct i2c_adapter	adap;

	int			clk_ctl;
	int			out_fifo_sz;
	int			in_fifo_sz;
	int			out_blk_sz;
	int			in_blk_sz;

	unsigned long		one_byte_t;
	struct qup_i2c_block	blk;

	struct i2c_msg		*msg;
	/* Current posion in user message buffer */
	int			pos;
	/* I2C protocol errors */
	u32			bus_err;
	/* QUP core errors */
	u32			qup_err;

	/* To check if this is the last msg */
	bool			is_last;

	/* To configure when bus is in run state */
	int			config_run;

	/* dma parameters */
	bool			is_dma;
	struct			dma_pool *dpool;
	struct			qup_i2c_tag start_tag;
	struct			qup_i2c_bam brx;
	struct			qup_i2c_bam btx;

	struct completion	xfer;
};

static irqreturn_t qup_i2c_interrupt(int irq, void *dev)
{
	struct qup_i2c_dev *qup = dev;
	u32 bus_err;
	u32 qup_err;
	u32 opflags;

	bus_err = readl(qup->base + QUP_I2C_STATUS);
	qup_err = readl(qup->base + QUP_ERROR_FLAGS);
	opflags = readl(qup->base + QUP_OPERATIONAL);

	if (!qup->msg) {
		/* Clear Error interrupt */
		writel(QUP_RESET_STATE, qup->base + QUP_STATE);
		return IRQ_HANDLED;
	}

	bus_err &= I2C_STATUS_ERROR_MASK;
	qup_err &= QUP_STATUS_ERROR_FLAGS;

	/* Clear the error bits in QUP_ERROR_FLAGS */
	if (qup_err)
		writel(qup_err, qup->base + QUP_ERROR_FLAGS);

	/* Clear the error bits in QUP_I2C_STATUS */
	if (bus_err)
		writel(bus_err, qup->base + QUP_I2C_STATUS);

	/* Reset the QUP State in case of error */
	if (qup_err || bus_err) {
		writel(QUP_RESET_STATE, qup->base + QUP_STATE);
		goto done;
	}

	if (opflags & QUP_IN_SVC_FLAG)
		writel(QUP_IN_SVC_FLAG, qup->base + QUP_OPERATIONAL);

	if (opflags & QUP_OUT_SVC_FLAG)
		writel(QUP_OUT_SVC_FLAG, qup->base + QUP_OPERATIONAL);

done:
	qup->qup_err = qup_err;
	qup->bus_err = bus_err;
	complete(&qup->xfer);
	return IRQ_HANDLED;
}

static int qup_i2c_poll_state_mask(struct qup_i2c_dev *qup,
				   u32 req_state, u32 req_mask)
{
	int retries = 1;
	u32 state;

	/*
	 * State transition takes 3 AHB clocks cycles + 3 I2C master clock
	 * cycles. So retry once after a 1uS delay.
	 */
	do {
		state = readl(qup->base + QUP_STATE);

		if (state & QUP_STATE_VALID &&
		    (state & req_mask) == req_state)
			return 0;

		udelay(1);
	} while (retries--);

	return -ETIMEDOUT;
}

static int qup_i2c_poll_state(struct qup_i2c_dev *qup, u32 req_state)
{
	return qup_i2c_poll_state_mask(qup, req_state, QUP_STATE_MASK);
}

static void qup_i2c_flush(struct qup_i2c_dev *qup)
{
	u32 val = readl(qup->base + QUP_STATE);

	val |= QUP_I2C_FLUSH;
	writel(val, qup->base + QUP_STATE);
}

static int qup_i2c_poll_state_valid(struct qup_i2c_dev *qup)
{
	return qup_i2c_poll_state_mask(qup, 0, 0);
}

static int qup_i2c_poll_state_i2c_master(struct qup_i2c_dev *qup)
{
	return qup_i2c_poll_state_mask(qup, QUP_I2C_MAST_GEN, QUP_I2C_MAST_GEN);
}

static int qup_i2c_change_state(struct qup_i2c_dev *qup, u32 state)
{
	if (qup_i2c_poll_state_valid(qup) != 0)
		return -EIO;

	writel(state, qup->base + QUP_STATE);

	if (qup_i2c_poll_state(qup, state) != 0)
		return -EIO;
	return 0;
}

/**
 * qup_i2c_wait_ready - wait for a give number of bytes in tx/rx path
 * @qup: The qup_i2c_dev device
 * @op: The bit/event to wait on
 * @val: value of the bit to wait on, 0 or 1
 * @len: The length the bytes to be transferred
 */
static int qup_i2c_wait_ready(struct qup_i2c_dev *qup, int op, bool val,
			      int len)
{
	unsigned long timeout;
	u32 opflags;
	u32 status;
	u32 shift = __ffs(op);
	int ret = 0;

	len *= qup->one_byte_t;
	/* timeout after a wait of twice the max time */
	timeout = jiffies + len * 4;

	for (;;) {
		opflags = readl(qup->base + QUP_OPERATIONAL);
		status = readl(qup->base + QUP_I2C_STATUS);

		if (((opflags & op) >> shift) == val) {
			if ((op == QUP_OUT_NOT_EMPTY) && qup->is_last) {
				if (!(status & I2C_STATUS_BUS_ACTIVE)) {
					ret = 0;
					goto done;
				}
			} else {
				ret = 0;
				goto done;
			}
		}

		if (time_after(jiffies, timeout)) {
			ret = -ETIMEDOUT;
			goto done;
		}
		usleep_range(len, len * 2);
	}

done:
	if (qup->bus_err || qup->qup_err)
		ret =  (qup->bus_err & QUP_I2C_NACK_FLAG) ? -ENXIO : -EIO;

	return ret;
}

static void qup_i2c_set_write_mode_v2(struct qup_i2c_dev *qup,
				      struct i2c_msg *msg)
{
	/* Number of entries to shift out, including the tags */
	int total = msg->len + qup->blk.tx_tag_len;

	total |= qup->config_run;

	if (total < qup->out_fifo_sz) {
		/* FIFO mode */
		writel(QUP_REPACK_EN, qup->base + QUP_IO_MODE);
		writel(total, qup->base + QUP_MX_WRITE_CNT);
	} else {
		/* BLOCK mode (transfer data on chunks) */
		writel(QUP_OUTPUT_BLK_MODE | QUP_REPACK_EN,
		       qup->base + QUP_IO_MODE);
		writel(total, qup->base + QUP_MX_OUTPUT_CNT);
	}
}

static void qup_i2c_set_write_mode(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	/* Number of entries to shift out, including the start */
	int total = msg->len + 1;

	if (total < qup->out_fifo_sz) {
		/* FIFO mode */
		writel(QUP_REPACK_EN, qup->base + QUP_IO_MODE);
		writel(total, qup->base + QUP_MX_WRITE_CNT);
	} else {
		/* BLOCK mode (transfer data on chunks) */
		writel(QUP_OUTPUT_BLK_MODE | QUP_REPACK_EN,
		       qup->base + QUP_IO_MODE);
		writel(total, qup->base + QUP_MX_OUTPUT_CNT);
	}
}

static int check_for_fifo_space(struct qup_i2c_dev *qup)
{
	int ret;

	ret = qup_i2c_change_state(qup, QUP_PAUSE_STATE);
	if (ret)
		goto out;

	ret = qup_i2c_wait_ready(qup, QUP_OUT_FULL,
				 RESET_BIT, 4 * ONE_BYTE);
	if (ret) {
		/* Fifo is full. Drain out the fifo */
		ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
		if (ret)
			goto out;

		ret = qup_i2c_wait_ready(qup, QUP_OUT_NOT_EMPTY,
					 RESET_BIT, 256 * ONE_BYTE);
		if (ret) {
			dev_err(qup->dev, "timeout for fifo out full");
			goto out;
		}

		ret = qup_i2c_change_state(qup, QUP_PAUSE_STATE);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static int qup_i2c_issue_write(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	u32 addr = msg->addr << 1;
	u32 qup_tag;
	int idx;
	u32 val;
	int ret = 0;

	if (qup->pos == 0) {
		val = QUP_TAG_START | addr;
		idx = 1;
	} else {
		val = 0;
		idx = 0;
	}

	while (qup->pos < msg->len) {
		/* Check that there's space in the FIFO for our pair */
		ret = check_for_fifo_space(qup);
		if (ret)
			return ret;

		if (qup->pos == msg->len - 1)
			qup_tag = QUP_TAG_STOP;
		else
			qup_tag = QUP_TAG_DATA;

		if (idx & 1)
			val |= (qup_tag | msg->buf[qup->pos]) << QUP_MSW_SHIFT;
		else
			val = qup_tag | msg->buf[qup->pos];

		/* Write out the pair and the last odd value */
		if (idx & 1 || qup->pos == msg->len - 1)
			writel(val, qup->base + QUP_OUT_FIFO_BASE);

		qup->pos++;
		idx++;
	}

	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);

	return ret;
}

static void qup_i2c_set_blk_data(struct qup_i2c_dev *qup,
				 struct i2c_msg *msg)
{
	memset(&qup->blk, 0, sizeof(qup->blk));

	qup->blk.data_len = msg->len;
	qup->blk.count = (msg->len + QUP_READ_LIMIT - 1) / QUP_READ_LIMIT;

	/* 4 bytes for first block and 2 writes for rest */
	qup->blk.tx_tag_len = 4 + (qup->blk.count - 1) * 2;

	/* There are 2 tag bytes that are read in to fifo for every block */
	if (msg->flags & I2C_M_RD)
		qup->blk.rx_tag_len = qup->blk.count * 2;
}

static int qup_i2c_send_data(struct qup_i2c_dev *qup, int tlen, u8 *tbuf,
			     int dlen, u8 *dbuf)
{
	u32 val = 0, idx = 0, pos = 0, i = 0, t;
	int  len = tlen + dlen;
	u8 *buf = tbuf;
	int ret = 0;

	while (len > 0) {
		ret = check_for_fifo_space(qup);
		if (ret)
			return ret;

		t = (len >= 4) ? 4 : len;

		while (idx < t) {
			if (!i && (pos >= tlen)) {
				buf = dbuf;
				pos = 0;
				i = 1;
			}
			val |= buf[pos++] << (idx++ * 8);
		}

		writel(val, qup->base + QUP_OUT_FIFO_BASE);
		idx  = 0;
		val = 0;
		len -= 4;
	}

	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);

	return ret;
}

static int qup_i2c_get_data_len(struct qup_i2c_dev *qup)
{
	int data_len;

	if (qup->blk.data_len > QUP_READ_LIMIT)
		data_len = QUP_READ_LIMIT;
	else
		data_len = qup->blk.data_len;

	return data_len;
}

static int qup_i2c_set_tags(u8 *tags, struct qup_i2c_dev *qup,
			    struct i2c_msg *msg,  int is_dma)
{
	u16 addr = i2c_8bit_addr_from_msg(msg);
	int len = 0;
	int data_len;

	int last = (qup->blk.pos == (qup->blk.count - 1)) && (qup->is_last);

	if (qup->blk.pos == 0) {
		tags[len++] = QUP_TAG_V2_START;
		tags[len++] = addr & 0xff;

		if (msg->flags & I2C_M_TEN)
			tags[len++] = addr >> 8;
	}

	/* Send _STOP commands for the last block */
	if (last) {
		if (msg->flags & I2C_M_RD)
			tags[len++] = QUP_TAG_V2_DATARD_STOP;
		else
			tags[len++] = QUP_TAG_V2_DATAWR_STOP;
	} else {
		if (msg->flags & I2C_M_RD)
			tags[len++] = QUP_TAG_V2_DATARD;
		else
			tags[len++] = QUP_TAG_V2_DATAWR;
	}

	data_len = qup_i2c_get_data_len(qup);

	/* 0 implies 256 bytes */
	if (data_len == QUP_READ_LIMIT)
		tags[len++] = 0;
	else
		tags[len++] = data_len;

	if ((msg->flags & I2C_M_RD) && last && is_dma) {
		tags[len++] = QUP_BAM_INPUT_EOT;
		tags[len++] = QUP_BAM_FLUSH_STOP;
	}

	return len;
}

static int qup_i2c_issue_xfer_v2(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	int data_len = 0, tag_len, index;
	int ret;

	tag_len = qup_i2c_set_tags(qup->blk.tags, qup, msg, 0);
	index = msg->len - qup->blk.data_len;

	/* only tags are written for read */
	if (!(msg->flags & I2C_M_RD))
		data_len = qup_i2c_get_data_len(qup);

	ret = qup_i2c_send_data(qup, tag_len, qup->blk.tags,
				data_len, &msg->buf[index]);
	qup->blk.data_len -= data_len;

	return ret;
}

static void qup_i2c_bam_cb(void *data)
{
	struct qup_i2c_dev *qup = data;

	complete(&qup->xfer);
}

static int qup_sg_set_buf(struct scatterlist *sg, void *buf,
			  unsigned int buflen, struct qup_i2c_dev *qup,
			  int dir)
{
	int ret;

	sg_set_buf(sg, buf, buflen);
	ret = dma_map_sg(qup->dev, sg, 1, dir);
	if (!ret)
		return -EINVAL;

	return 0;
}

static void qup_i2c_rel_dma(struct qup_i2c_dev *qup)
{
	if (qup->btx.dma)
		dma_release_channel(qup->btx.dma);
	if (qup->brx.dma)
		dma_release_channel(qup->brx.dma);
	qup->btx.dma = NULL;
	qup->brx.dma = NULL;
}

static int qup_i2c_req_dma(struct qup_i2c_dev *qup)
{
	int err;

	if (!qup->btx.dma) {
		qup->btx.dma = dma_request_slave_channel_reason(qup->dev, "tx");
		if (IS_ERR(qup->btx.dma)) {
			err = PTR_ERR(qup->btx.dma);
			qup->btx.dma = NULL;
			dev_err(qup->dev, "\n tx channel not available");
			return err;
		}
	}

	if (!qup->brx.dma) {
		qup->brx.dma = dma_request_slave_channel_reason(qup->dev, "rx");
		if (IS_ERR(qup->brx.dma)) {
			dev_err(qup->dev, "\n rx channel not available");
			err = PTR_ERR(qup->brx.dma);
			qup->brx.dma = NULL;
			qup_i2c_rel_dma(qup);
			return err;
		}
	}
	return 0;
}

static int qup_i2c_bam_do_xfer(struct qup_i2c_dev *qup, struct i2c_msg *msg,
			       int num)
{
	struct dma_async_tx_descriptor *txd, *rxd = NULL;
	int ret = 0, idx = 0, limit = QUP_READ_LIMIT;
	dma_cookie_t cookie_rx, cookie_tx;
	u32 rx_nents = 0, tx_nents = 0, len, blocks, rem;
	u32 i, tlen, tx_len, tx_buf = 0, rx_buf = 0, off = 0;
	u8 *tags;

	while (idx < num) {
		tx_len = 0, len = 0, i = 0;

		qup->is_last = (idx == (num - 1));

		qup_i2c_set_blk_data(qup, msg);

		blocks = qup->blk.count;
		rem = msg->len - (blocks - 1) * limit;

		if (msg->flags & I2C_M_RD) {
			rx_nents += (blocks * 2) + 1;
			tx_nents += 1;

			while (qup->blk.pos < blocks) {
				tlen = (i == (blocks - 1)) ? rem : limit;
				tags = &qup->start_tag.start[off + len];
				len += qup_i2c_set_tags(tags, qup, msg, 1);
				qup->blk.data_len -= tlen;

				/* scratch buf to read the start and len tags */
				ret = qup_sg_set_buf(&qup->brx.sg[rx_buf++],
						     &qup->brx.tag.start[0],
						     2, qup, DMA_FROM_DEVICE);

				if (ret)
					return ret;

				ret = qup_sg_set_buf(&qup->brx.sg[rx_buf++],
						     &msg->buf[limit * i],
						     tlen, qup,
						     DMA_FROM_DEVICE);
				if (ret)
					return ret;

				i++;
				qup->blk.pos = i;
			}
			ret = qup_sg_set_buf(&qup->btx.sg[tx_buf++],
					     &qup->start_tag.start[off],
					     len, qup, DMA_TO_DEVICE);
			if (ret)
				return ret;

			off += len;
			/* scratch buf to read the BAM EOT and FLUSH tags */
			ret = qup_sg_set_buf(&qup->brx.sg[rx_buf++],
					     &qup->brx.tag.start[0],
					     2, qup, DMA_FROM_DEVICE);
			if (ret)
				return ret;
		} else {
			tx_nents += (blocks * 2);

			while (qup->blk.pos < blocks) {
				tlen = (i == (blocks - 1)) ? rem : limit;
				tags = &qup->start_tag.start[off + tx_len];
				len = qup_i2c_set_tags(tags, qup, msg, 1);
				qup->blk.data_len -= tlen;

				ret = qup_sg_set_buf(&qup->btx.sg[tx_buf++],
						     tags, len,
						     qup, DMA_TO_DEVICE);
				if (ret)
					return ret;

				tx_len += len;
				ret = qup_sg_set_buf(&qup->btx.sg[tx_buf++],
						     &msg->buf[limit * i],
						     tlen, qup, DMA_TO_DEVICE);
				if (ret)
					return ret;
				i++;
				qup->blk.pos = i;
			}
			off += tx_len;

			if (idx == (num - 1)) {
				len = 1;
				if (rx_nents) {
					qup->btx.tag.start[0] =
							QUP_BAM_INPUT_EOT;
					len++;
				}
				qup->btx.tag.start[len - 1] =
							QUP_BAM_FLUSH_STOP;
				ret = qup_sg_set_buf(&qup->btx.sg[tx_buf++],
						     &qup->btx.tag.start[0],
						     len, qup, DMA_TO_DEVICE);
				if (ret)
					return ret;
				tx_nents += 1;
			}
		}
		idx++;
		msg++;
	}

	txd = dmaengine_prep_slave_sg(qup->btx.dma, qup->btx.sg, tx_nents,
				      DMA_MEM_TO_DEV,
				      DMA_PREP_INTERRUPT | DMA_PREP_FENCE);
	if (!txd) {
		dev_err(qup->dev, "failed to get tx desc\n");
		ret = -EINVAL;
		goto desc_err;
	}

	if (!rx_nents) {
		txd->callback = qup_i2c_bam_cb;
		txd->callback_param = qup;
	}

	cookie_tx = dmaengine_submit(txd);
	if (dma_submit_error(cookie_tx)) {
		ret = -EINVAL;
		goto desc_err;
	}

	dma_async_issue_pending(qup->btx.dma);

	if (rx_nents) {
		rxd = dmaengine_prep_slave_sg(qup->brx.dma, qup->brx.sg,
					      rx_nents, DMA_DEV_TO_MEM,
					      DMA_PREP_INTERRUPT);
		if (!rxd) {
			dev_err(qup->dev, "failed to get rx desc\n");
			ret = -EINVAL;

			/* abort TX descriptors */
			dmaengine_terminate_all(qup->btx.dma);
			goto desc_err;
		}

		rxd->callback = qup_i2c_bam_cb;
		rxd->callback_param = qup;
		cookie_rx = dmaengine_submit(rxd);
		if (dma_submit_error(cookie_rx)) {
			ret = -EINVAL;
			goto desc_err;
		}

		dma_async_issue_pending(qup->brx.dma);
	}

	if (!wait_for_completion_timeout(&qup->xfer, TOUT_MAX * HZ)) {
		dev_err(qup->dev, "normal trans timed out\n");
		ret = -ETIMEDOUT;
	}

	if (ret || qup->bus_err || qup->qup_err) {
		if (qup_i2c_change_state(qup, QUP_RUN_STATE)) {
			dev_err(qup->dev, "change to run state timed out");
			goto desc_err;
		}

		if (rx_nents)
			writel(QUP_BAM_INPUT_EOT,
			       qup->base + QUP_OUT_FIFO_BASE);

		writel(QUP_BAM_FLUSH_STOP, qup->base + QUP_OUT_FIFO_BASE);

		qup_i2c_flush(qup);

		/* wait for remaining interrupts to occur */
		if (!wait_for_completion_timeout(&qup->xfer, HZ))
			dev_err(qup->dev, "flush timed out\n");

		qup_i2c_rel_dma(qup);

		ret =  (qup->bus_err & QUP_I2C_NACK_FLAG) ? -ENXIO : -EIO;
	}

desc_err:
	dma_unmap_sg(qup->dev, qup->btx.sg, tx_nents, DMA_TO_DEVICE);

	if (rx_nents)
		dma_unmap_sg(qup->dev, qup->brx.sg, rx_nents,
			     DMA_FROM_DEVICE);

	return ret;
}

static int qup_i2c_bam_xfer(struct i2c_adapter *adap, struct i2c_msg *msg,
			    int num)
{
	struct qup_i2c_dev *qup = i2c_get_adapdata(adap);
	int ret = 0;

	enable_irq(qup->irq);
	ret = qup_i2c_req_dma(qup);

	if (ret)
		goto out;

	writel(0, qup->base + QUP_MX_INPUT_CNT);
	writel(0, qup->base + QUP_MX_OUTPUT_CNT);

	/* set BAM mode */
	writel(QUP_REPACK_EN | QUP_BAM_MODE, qup->base + QUP_IO_MODE);

	/* mask fifo irqs */
	writel((0x3 << 8), qup->base + QUP_OPERATIONAL_MASK);

	/* set RUN STATE */
	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
	if (ret)
		goto out;

	writel(qup->clk_ctl, qup->base + QUP_I2C_CLK_CTL);

	qup->msg = msg;
	ret = qup_i2c_bam_do_xfer(qup, qup->msg, num);
out:
	disable_irq(qup->irq);

	qup->msg = NULL;
	return ret;
}

static int qup_i2c_wait_for_complete(struct qup_i2c_dev *qup,
				     struct i2c_msg *msg)
{
	unsigned long left;
	int ret = 0;

	left = wait_for_completion_timeout(&qup->xfer, HZ);
	if (!left) {
		writel(1, qup->base + QUP_SW_RESET);
		ret = -ETIMEDOUT;
	}

	if (qup->bus_err || qup->qup_err)
		ret =  (qup->bus_err & QUP_I2C_NACK_FLAG) ? -ENXIO : -EIO;

	return ret;
}

static int qup_i2c_write_one_v2(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	int ret = 0;

	qup->msg = msg;
	qup->pos = 0;
	enable_irq(qup->irq);
	qup_i2c_set_blk_data(qup, msg);
	qup_i2c_set_write_mode_v2(qup, msg);

	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
	if (ret)
		goto err;

	writel(qup->clk_ctl, qup->base + QUP_I2C_CLK_CTL);

	do {
		ret = qup_i2c_issue_xfer_v2(qup, msg);
		if (ret)
			goto err;

		ret = qup_i2c_wait_for_complete(qup, msg);
		if (ret)
			goto err;

		qup->blk.pos++;
	} while (qup->blk.pos < qup->blk.count);

	ret = qup_i2c_wait_ready(qup, QUP_OUT_NOT_EMPTY, RESET_BIT, ONE_BYTE);

err:
	disable_irq(qup->irq);
	qup->msg = NULL;

	return ret;
}

static int qup_i2c_write_one(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	int ret;

	qup->msg = msg;
	qup->pos = 0;

	enable_irq(qup->irq);

	qup_i2c_set_write_mode(qup, msg);

	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
	if (ret)
		goto err;

	writel(qup->clk_ctl, qup->base + QUP_I2C_CLK_CTL);

	do {
		ret = qup_i2c_change_state(qup, QUP_PAUSE_STATE);
		if (ret)
			goto err;

		ret = qup_i2c_issue_write(qup, msg);
		if (ret)
			goto err;

		ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
		if (ret)
			goto err;

		ret = qup_i2c_wait_for_complete(qup, msg);
		if (ret)
			goto err;
	} while (qup->pos < msg->len);

	/* Wait for the outstanding data in the fifo to drain */
	ret = qup_i2c_wait_ready(qup, QUP_OUT_NOT_EMPTY, RESET_BIT, ONE_BYTE);
err:
	disable_irq(qup->irq);
	qup->msg = NULL;

	return ret;
}

static void qup_i2c_set_read_mode(struct qup_i2c_dev *qup, int len)
{
	if (len < qup->in_fifo_sz) {
		/* FIFO mode */
		writel(QUP_REPACK_EN, qup->base + QUP_IO_MODE);
		writel(len, qup->base + QUP_MX_READ_CNT);
	} else {
		/* BLOCK mode (transfer data on chunks) */
		writel(QUP_INPUT_BLK_MODE | QUP_REPACK_EN,
		       qup->base + QUP_IO_MODE);
		writel(len, qup->base + QUP_MX_INPUT_CNT);
	}
}

static void qup_i2c_set_read_mode_v2(struct qup_i2c_dev *qup, int len)
{
	int tx_len = qup->blk.tx_tag_len;

	len += qup->blk.rx_tag_len;
	len |= qup->config_run;
	tx_len |= qup->config_run;

	if (len < qup->in_fifo_sz) {
		/* FIFO mode */
		writel(QUP_REPACK_EN, qup->base + QUP_IO_MODE);
		writel(tx_len, qup->base + QUP_MX_WRITE_CNT);
		writel(len, qup->base + QUP_MX_READ_CNT);
	} else {
		/* BLOCK mode (transfer data on chunks) */
		writel(QUP_INPUT_BLK_MODE | QUP_REPACK_EN,
		       qup->base + QUP_IO_MODE);
		writel(tx_len, qup->base + QUP_MX_OUTPUT_CNT);
		writel(len, qup->base + QUP_MX_INPUT_CNT);
	}
}

static void qup_i2c_issue_read(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	u32 addr, len, val;

	addr = i2c_8bit_addr_from_msg(msg);

	/* 0 is used to specify a length 256 (QUP_READ_LIMIT) */
	len = (msg->len == QUP_READ_LIMIT) ? 0 : msg->len;

	val = ((QUP_TAG_REC | len) << QUP_MSW_SHIFT) | QUP_TAG_START | addr;
	writel(val, qup->base + QUP_OUT_FIFO_BASE);
}


static int qup_i2c_read_fifo(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	u32 val = 0;
	int idx;
	int ret = 0;

	for (idx = 0; qup->pos < msg->len; idx++) {
		if ((idx & 1) == 0) {
			/* Check that FIFO have data */
			ret = qup_i2c_wait_ready(qup, QUP_IN_NOT_EMPTY,
						 SET_BIT, 4 * ONE_BYTE);
			if (ret)
				return ret;

			/* Reading 2 words at time */
			val = readl(qup->base + QUP_IN_FIFO_BASE);

			msg->buf[qup->pos++] = val & 0xFF;
		} else {
			msg->buf[qup->pos++] = val >> QUP_MSW_SHIFT;
		}
	}

	return ret;
}

static int qup_i2c_read_fifo_v2(struct qup_i2c_dev *qup,
				struct i2c_msg *msg)
{
	u32 val;
	int idx, pos = 0, ret = 0, total;

	total = qup_i2c_get_data_len(qup);

	/* 2 extra bytes for read tags */
	while (pos < (total + 2)) {
		/* Check that FIFO have data */
		ret = qup_i2c_wait_ready(qup, QUP_IN_NOT_EMPTY,
					 SET_BIT, 4 * ONE_BYTE);
		if (ret) {
			dev_err(qup->dev, "timeout for fifo not empty");
			return ret;
		}
		val = readl(qup->base + QUP_IN_FIFO_BASE);

		for (idx = 0; idx < 4; idx++, val >>= 8, pos++) {
			/* first 2 bytes are tag bytes */
			if (pos < 2)
				continue;

			if (pos >= (total + 2))
				goto out;

			msg->buf[qup->pos++] = val & 0xff;
		}
	}

out:
	qup->blk.data_len -= total;

	return ret;
}

static int qup_i2c_read_one_v2(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	int ret = 0;

	qup->msg = msg;
	qup->pos  = 0;
	enable_irq(qup->irq);
	qup_i2c_set_blk_data(qup, msg);
	qup_i2c_set_read_mode_v2(qup, msg->len);

	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
	if (ret)
		goto err;

	writel(qup->clk_ctl, qup->base + QUP_I2C_CLK_CTL);

	do {
		ret = qup_i2c_issue_xfer_v2(qup, msg);
		if (ret)
			goto err;

		ret = qup_i2c_wait_for_complete(qup, msg);
		if (ret)
			goto err;

		ret = qup_i2c_read_fifo_v2(qup, msg);
		if (ret)
			goto err;

		qup->blk.pos++;
	} while (qup->blk.pos < qup->blk.count);

err:
	disable_irq(qup->irq);
	qup->msg = NULL;

	return ret;
}

static int qup_i2c_read_one(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	int ret;

	qup->msg = msg;
	qup->pos  = 0;

	enable_irq(qup->irq);
	qup_i2c_set_read_mode(qup, msg->len);

	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
	if (ret)
		goto err;

	writel(qup->clk_ctl, qup->base + QUP_I2C_CLK_CTL);

	ret = qup_i2c_change_state(qup, QUP_PAUSE_STATE);
	if (ret)
		goto err;

	qup_i2c_issue_read(qup, msg);

	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
	if (ret)
		goto err;

	do {
		ret = qup_i2c_wait_for_complete(qup, msg);
		if (ret)
			goto err;

		ret = qup_i2c_read_fifo(qup, msg);
		if (ret)
			goto err;
	} while (qup->pos < msg->len);

err:
	disable_irq(qup->irq);
	qup->msg = NULL;

	return ret;
}

static int qup_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg msgs[],
			int num)
{
	struct qup_i2c_dev *qup = i2c_get_adapdata(adap);
	int ret, idx;

	ret = pm_runtime_get_sync(qup->dev);
	if (ret < 0)
		goto out;

	qup->bus_err = 0;
	qup->qup_err = 0;

	writel(1, qup->base + QUP_SW_RESET);
	ret = qup_i2c_poll_state(qup, QUP_RESET_STATE);
	if (ret)
		goto out;

	/* Configure QUP as I2C mini core */
	writel(I2C_MINI_CORE | I2C_N_VAL, qup->base + QUP_CONFIG);

	for (idx = 0; idx < num; idx++) {
		if (msgs[idx].len == 0) {
			ret = -EINVAL;
			goto out;
		}

		if (qup_i2c_poll_state_i2c_master(qup)) {
			ret = -EIO;
			goto out;
		}

		if (msgs[idx].flags & I2C_M_RD)
			ret = qup_i2c_read_one(qup, &msgs[idx]);
		else
			ret = qup_i2c_write_one(qup, &msgs[idx]);

		if (ret)
			break;

		ret = qup_i2c_change_state(qup, QUP_RESET_STATE);
		if (ret)
			break;
	}

	if (ret == 0)
		ret = num;
out:

	pm_runtime_mark_last_busy(qup->dev);
	pm_runtime_put_autosuspend(qup->dev);

	return ret;
}

static int qup_i2c_xfer_v2(struct i2c_adapter *adap,
			   struct i2c_msg msgs[],
			   int num)
{
	struct qup_i2c_dev *qup = i2c_get_adapdata(adap);
	int ret, len, idx = 0, use_dma = 0;

	qup->bus_err = 0;
	qup->qup_err = 0;

	ret = pm_runtime_get_sync(qup->dev);
	if (ret < 0)
		goto out;

	writel(1, qup->base + QUP_SW_RESET);
	ret = qup_i2c_poll_state(qup, QUP_RESET_STATE);
	if (ret)
		goto out;

	/* Configure QUP as I2C mini core */
	writel(I2C_MINI_CORE | I2C_N_VAL_V2, qup->base + QUP_CONFIG);
	writel(QUP_V2_TAGS_EN, qup->base + QUP_I2C_MASTER_GEN);

	if ((qup->is_dma)) {
		/* All i2c_msgs should be transferred using either dma or cpu */
		for (idx = 0; idx < num; idx++) {
			if (msgs[idx].len == 0) {
				ret = -EINVAL;
				goto out;
			}

			len = (msgs[idx].len > qup->out_fifo_sz) ||
			      (msgs[idx].len > qup->in_fifo_sz);

			if ((!is_vmalloc_addr(msgs[idx].buf)) && len) {
				use_dma = 1;
			 } else {
				use_dma = 0;
				break;
			}
		}
	}

	idx = 0;

	do {
		if (msgs[idx].len == 0) {
			ret = -EINVAL;
			goto out;
		}

		if (qup_i2c_poll_state_i2c_master(qup)) {
			ret = -EIO;
			goto out;
		}

		qup->is_last = (idx == (num - 1));
		if (idx)
			qup->config_run = QUP_I2C_MX_CONFIG_DURING_RUN;
		else
			qup->config_run = 0;

		reinit_completion(&qup->xfer);

		if (use_dma) {
			ret = qup_i2c_bam_xfer(adap, &msgs[idx], num);
		} else {
			if (msgs[idx].flags & I2C_M_RD)
				ret = qup_i2c_read_one_v2(qup, &msgs[idx]);
			else
				ret = qup_i2c_write_one_v2(qup, &msgs[idx]);
		}
	} while ((idx++ < (num - 1)) && !use_dma && !ret);

	if (!ret)
		ret = qup_i2c_change_state(qup, QUP_RESET_STATE);

	if (ret == 0)
		ret = num;
out:
	pm_runtime_mark_last_busy(qup->dev);
	pm_runtime_put_autosuspend(qup->dev);

	return ret;
}

static u32 qup_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static const struct i2c_algorithm qup_i2c_algo = {
	.master_xfer	= qup_i2c_xfer,
	.functionality	= qup_i2c_func,
};

static const struct i2c_algorithm qup_i2c_algo_v2 = {
	.master_xfer	= qup_i2c_xfer_v2,
	.functionality	= qup_i2c_func,
};

/*
 * The QUP block will issue a NACK and STOP on the bus when reaching
 * the end of the read, the length of the read is specified as one byte
 * which limits the possible read to 256 (QUP_READ_LIMIT) bytes.
 */
static struct i2c_adapter_quirks qup_i2c_quirks = {
	.max_read_len = QUP_READ_LIMIT,
};

static void qup_i2c_enable_clocks(struct qup_i2c_dev *qup)
{
	clk_prepare_enable(qup->clk);
	clk_prepare_enable(qup->pclk);
}

static void qup_i2c_disable_clocks(struct qup_i2c_dev *qup)
{
	u32 config;

	qup_i2c_change_state(qup, QUP_RESET_STATE);
	clk_disable_unprepare(qup->clk);
	config = readl(qup->base + QUP_CONFIG);
	config |= QUP_CLOCK_AUTO_GATE;
	writel(config, qup->base + QUP_CONFIG);
	clk_disable_unprepare(qup->pclk);
}

static int qup_i2c_probe(struct platform_device *pdev)
{
	static const int blk_sizes[] = {4, 16, 32};
	struct device_node *node = pdev->dev.of_node;
	struct qup_i2c_dev *qup;
	unsigned long one_bit_t;
	struct resource *res;
	u32 io_mode, hw_ver, size;
	int ret, fs_div, hs_div;
	int src_clk_freq;
	u32 clk_freq = 100000;
	int blocks;

	qup = devm_kzalloc(&pdev->dev, sizeof(*qup), GFP_KERNEL);
	if (!qup)
		return -ENOMEM;

	qup->dev = &pdev->dev;
	init_completion(&qup->xfer);
	platform_set_drvdata(pdev, qup);

	of_property_read_u32(node, "clock-frequency", &clk_freq);

	if (of_device_is_compatible(pdev->dev.of_node, "qcom,i2c-qup-v1.1.1")) {
		qup->adap.algo = &qup_i2c_algo;
		qup->adap.quirks = &qup_i2c_quirks;
	} else {
		qup->adap.algo = &qup_i2c_algo_v2;
		ret = qup_i2c_req_dma(qup);

		if (ret == -EPROBE_DEFER)
			goto fail_dma;
		else if (ret != 0)
			goto nodma;

		blocks = (MX_BLOCKS << 1) + 1;
		qup->btx.sg = devm_kzalloc(&pdev->dev,
					   sizeof(*qup->btx.sg) * blocks,
					   GFP_KERNEL);
		if (!qup->btx.sg) {
			ret = -ENOMEM;
			goto fail_dma;
		}
		sg_init_table(qup->btx.sg, blocks);

		qup->brx.sg = devm_kzalloc(&pdev->dev,
					   sizeof(*qup->brx.sg) * blocks,
					   GFP_KERNEL);
		if (!qup->brx.sg) {
			ret = -ENOMEM;
			goto fail_dma;
		}
		sg_init_table(qup->brx.sg, blocks);

		/* 2 tag bytes for each block + 5 for start, stop tags */
		size = blocks * 2 + 5;

		qup->start_tag.start = devm_kzalloc(&pdev->dev,
						    size, GFP_KERNEL);
		if (!qup->start_tag.start) {
			ret = -ENOMEM;
			goto fail_dma;
		}

		qup->brx.tag.start = devm_kzalloc(&pdev->dev, 2, GFP_KERNEL);
		if (!qup->brx.tag.start) {
			ret = -ENOMEM;
			goto fail_dma;
		}

		qup->btx.tag.start = devm_kzalloc(&pdev->dev, 2, GFP_KERNEL);
		if (!qup->btx.tag.start) {
			ret = -ENOMEM;
			goto fail_dma;
		}
		qup->is_dma = true;
	}

nodma:
	/* We support frequencies up to FAST Mode (400KHz) */
	if (!clk_freq || clk_freq > 400000) {
		dev_err(qup->dev, "clock frequency not supported %d\n",
			clk_freq);
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	qup->base = devm_ioremap_resource(qup->dev, res);
	if (IS_ERR(qup->base))
		return PTR_ERR(qup->base);

	qup->irq = platform_get_irq(pdev, 0);
	if (qup->irq < 0) {
		dev_err(qup->dev, "No IRQ defined\n");
		return qup->irq;
	}

	qup->clk = devm_clk_get(qup->dev, "core");
	if (IS_ERR(qup->clk)) {
		dev_err(qup->dev, "Could not get core clock\n");
		return PTR_ERR(qup->clk);
	}

	qup->pclk = devm_clk_get(qup->dev, "iface");
	if (IS_ERR(qup->pclk)) {
		dev_err(qup->dev, "Could not get iface clock\n");
		return PTR_ERR(qup->pclk);
	}

	qup_i2c_enable_clocks(qup);

	/*
	 * Bootloaders might leave a pending interrupt on certain QUP's,
	 * so we reset the core before registering for interrupts.
	 */
	writel(1, qup->base + QUP_SW_RESET);
	ret = qup_i2c_poll_state_valid(qup);
	if (ret)
		goto fail;

	ret = devm_request_irq(qup->dev, qup->irq, qup_i2c_interrupt,
			       IRQF_TRIGGER_HIGH, "i2c_qup", qup);
	if (ret) {
		dev_err(qup->dev, "Request %d IRQ failed\n", qup->irq);
		goto fail;
	}
	disable_irq(qup->irq);

	hw_ver = readl(qup->base + QUP_HW_VERSION);
	dev_dbg(qup->dev, "Revision %x\n", hw_ver);

	io_mode = readl(qup->base + QUP_IO_MODE);

	/*
	 * The block/fifo size w.r.t. 'actual data' is 1/2 due to 'tag'
	 * associated with each byte written/received
	 */
	size = QUP_OUTPUT_BLOCK_SIZE(io_mode);
	if (size >= ARRAY_SIZE(blk_sizes)) {
		ret = -EIO;
		goto fail;
	}
	qup->out_blk_sz = blk_sizes[size] / 2;

	size = QUP_INPUT_BLOCK_SIZE(io_mode);
	if (size >= ARRAY_SIZE(blk_sizes)) {
		ret = -EIO;
		goto fail;
	}
	qup->in_blk_sz = blk_sizes[size] / 2;

	size = QUP_OUTPUT_FIFO_SIZE(io_mode);
	qup->out_fifo_sz = qup->out_blk_sz * (2 << size);

	size = QUP_INPUT_FIFO_SIZE(io_mode);
	qup->in_fifo_sz = qup->in_blk_sz * (2 << size);

	src_clk_freq = clk_get_rate(qup->clk);
	fs_div = ((src_clk_freq / clk_freq) / 2) - 3;
	hs_div = 3;
	qup->clk_ctl = (hs_div << 8) | (fs_div & 0xff);

	/*
	 * Time it takes for a byte to be clocked out on the bus.
	 * Each byte takes 9 clock cycles (8 bits + 1 ack).
	 */
	one_bit_t = (USEC_PER_SEC / clk_freq) + 1;
	qup->one_byte_t = one_bit_t * 9;

	dev_dbg(qup->dev, "IN:block:%d, fifo:%d, OUT:block:%d, fifo:%d\n",
		qup->in_blk_sz, qup->in_fifo_sz,
		qup->out_blk_sz, qup->out_fifo_sz);

	i2c_set_adapdata(&qup->adap, qup);
	qup->adap.dev.parent = qup->dev;
	qup->adap.dev.of_node = pdev->dev.of_node;
	qup->is_last = true;

	strlcpy(qup->adap.name, "QUP I2C adapter", sizeof(qup->adap.name));

	pm_runtime_set_autosuspend_delay(qup->dev, MSEC_PER_SEC);
	pm_runtime_use_autosuspend(qup->dev);
	pm_runtime_set_active(qup->dev);
	pm_runtime_enable(qup->dev);

	ret = i2c_add_adapter(&qup->adap);
	if (ret)
		goto fail_runtime;

	return 0;

fail_runtime:
	pm_runtime_disable(qup->dev);
	pm_runtime_set_suspended(qup->dev);
fail:
	qup_i2c_disable_clocks(qup);
fail_dma:
	if (qup->btx.dma)
		dma_release_channel(qup->btx.dma);
	if (qup->brx.dma)
		dma_release_channel(qup->brx.dma);
	return ret;
}

static int qup_i2c_remove(struct platform_device *pdev)
{
	struct qup_i2c_dev *qup = platform_get_drvdata(pdev);

	if (qup->is_dma) {
		dma_release_channel(qup->btx.dma);
		dma_release_channel(qup->brx.dma);
	}

	disable_irq(qup->irq);
	qup_i2c_disable_clocks(qup);
	i2c_del_adapter(&qup->adap);
	pm_runtime_disable(qup->dev);
	pm_runtime_set_suspended(qup->dev);
	return 0;
}

#ifdef CONFIG_PM
static int qup_i2c_pm_suspend_runtime(struct device *device)
{
	struct qup_i2c_dev *qup = dev_get_drvdata(device);

	dev_dbg(device, "pm_runtime: suspending...\n");
	qup_i2c_disable_clocks(qup);
	return 0;
}

static int qup_i2c_pm_resume_runtime(struct device *device)
{
	struct qup_i2c_dev *qup = dev_get_drvdata(device);

	dev_dbg(device, "pm_runtime: resuming...\n");
	qup_i2c_enable_clocks(qup);
	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int qup_i2c_suspend(struct device *device)
{
	if (!pm_runtime_suspended(device))
		return qup_i2c_pm_suspend_runtime(device);
	return 0;
}

static int qup_i2c_resume(struct device *device)
{
	qup_i2c_pm_resume_runtime(device);
	pm_runtime_mark_last_busy(device);
	pm_request_autosuspend(device);
	return 0;
}
#endif

static const struct dev_pm_ops qup_i2c_qup_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(
		qup_i2c_suspend,
		qup_i2c_resume)
	SET_RUNTIME_PM_OPS(
		qup_i2c_pm_suspend_runtime,
		qup_i2c_pm_resume_runtime,
		NULL)
};

static const struct of_device_id qup_i2c_dt_match[] = {
	{ .compatible = "qcom,i2c-qup-v1.1.1" },
	{ .compatible = "qcom,i2c-qup-v2.1.1" },
	{ .compatible = "qcom,i2c-qup-v2.2.1" },
	{}
};
MODULE_DEVICE_TABLE(of, qup_i2c_dt_match);

static struct platform_driver qup_i2c_driver = {
	.probe  = qup_i2c_probe,
	.remove = qup_i2c_remove,
	.driver = {
		.name = "i2c_qup",
		.pm = &qup_i2c_qup_pm_ops,
		.of_match_table = qup_i2c_dt_match,
	},
};

module_platform_driver(qup_i2c_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2c_qup");
