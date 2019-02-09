// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2009-2013, 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014, Sony Mobile Communications AB.
 *
 */

#include <linux/acpi.h>
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
#define OUT_BLOCK_WRITE_REQ	BIT(12)
#define IN_BLOCK_READ_REQ	BIT(13)

/* I2C mini core related values */
#define QUP_NO_INPUT		BIT(7)
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
#define QUP_TAG_V2_DATARD_NACK         0x86
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

/* Maximum transfer length for single DMA descriptor */
#define MX_TX_RX_LEN			SZ_64K
#define MX_BLOCKS			(MX_TX_RX_LEN / QUP_READ_LIMIT)
/* Maximum transfer length for all DMA descriptors */
#define MX_DMA_TX_RX_LEN		(2 * MX_TX_RX_LEN)
#define MX_DMA_BLOCKS			(MX_DMA_TX_RX_LEN / QUP_READ_LIMIT)

/*
 * Minimum transfer timeout for i2c transfers in seconds. It will be added on
 * the top of maximum transfer time calculated from i2c bus speed to compensate
 * the overheads.
 */
#define TOUT_MIN			2

/* I2C Frequency Modes */
#define I2C_STANDARD_FREQ		100000
#define I2C_FAST_MODE_FREQ		400000
#define I2C_FAST_MODE_PLUS_FREQ		1000000

/* Default values. Use these if FW query fails */
#define DEFAULT_CLK_FREQ I2C_STANDARD_FREQ
#define DEFAULT_SRC_CLK 20000000

/*
 * Max tags length (start, stop and maximum 2 bytes address) for each QUP
 * data transfer
 */
#define QUP_MAX_TAGS_LEN		4
/* Max data length for each DATARD tags */
#define RECV_MAX_DATA_LEN		254
/* TAG length for DATA READ in RX FIFO  */
#define READ_RX_TAGS_LEN		2

static unsigned int scl_freq;
module_param_named(scl_freq, scl_freq, uint, 0444);
MODULE_PARM_DESC(scl_freq, "SCL frequency override");

/*
 * count: no of blocks
 * pos: current block number
 * tx_tag_len: tx tag length for current block
 * rx_tag_len: rx tag length for current block
 * data_len: remaining data length for current message
 * cur_blk_len: data length for current block
 * total_tx_len: total tx length including tag bytes for current QUP transfer
 * total_rx_len: total rx length including tag bytes for current QUP transfer
 * tx_fifo_data_pos: current byte number in TX FIFO word
 * tx_fifo_free: number of free bytes in current QUP block write.
 * rx_fifo_data_pos: current byte number in RX FIFO word
 * fifo_available: number of available bytes in RX FIFO for current
 *		   QUP block read
 * tx_fifo_data: QUP TX FIFO write works on word basis (4 bytes). New byte write
 *		 to TX FIFO will be appended in this data and will be written to
 *		 TX FIFO when all the 4 bytes are available.
 * rx_fifo_data: QUP RX FIFO read works on word basis (4 bytes). This will
 *		 contains the 4 bytes of RX data.
 * cur_data: pointer to tell cur data position for current message
 * cur_tx_tags: pointer to tell cur position in tags
 * tx_tags_sent: all tx tag bytes have been written in FIFO word
 * send_last_word: for tx FIFO, last word send is pending in current block
 * rx_bytes_read: if all the bytes have been read from rx FIFO.
 * rx_tags_fetched: all the rx tag bytes have been fetched from rx fifo word
 * is_tx_blk_mode: whether tx uses block or FIFO mode in case of non BAM xfer.
 * is_rx_blk_mode: whether rx uses block or FIFO mode in case of non BAM xfer.
 * tags: contains tx tag bytes for current QUP transfer
 */
struct qup_i2c_block {
	int		count;
	int		pos;
	int		tx_tag_len;
	int		rx_tag_len;
	int		data_len;
	int		cur_blk_len;
	int		total_tx_len;
	int		total_rx_len;
	int		tx_fifo_data_pos;
	int		tx_fifo_free;
	int		rx_fifo_data_pos;
	int		fifo_available;
	u32		tx_fifo_data;
	u32		rx_fifo_data;
	u8		*cur_data;
	u8		*cur_tx_tags;
	bool		tx_tags_sent;
	bool		send_last_word;
	bool		rx_tags_fetched;
	bool		rx_bytes_read;
	bool		is_tx_blk_mode;
	bool		is_rx_blk_mode;
	u8		tags[6];
};

struct qup_i2c_tag {
	u8 *start;
	dma_addr_t addr;
};

struct qup_i2c_bam {
	struct	qup_i2c_tag tag;
	struct	dma_chan *dma;
	struct	scatterlist *sg;
	unsigned int sg_cnt;
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

	int			blk_xfer_limit;
	unsigned long		one_byte_t;
	unsigned long		xfer_timeout;
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
	bool			is_smbus_read;

	/* To configure when bus is in run state */
	u32			config_run;

	/* dma parameters */
	bool			is_dma;
	/* To check if the current transfer is using DMA */
	bool			use_dma;
	unsigned int		max_xfer_sg_len;
	unsigned int		tag_buf_pos;
	/* The threshold length above which block mode will be used */
	unsigned int		blk_mode_threshold;
	struct			dma_pool *dpool;
	struct			qup_i2c_tag start_tag;
	struct			qup_i2c_bam brx;
	struct			qup_i2c_bam btx;

	struct completion	xfer;
	/* function to write data in tx fifo */
	void (*write_tx_fifo)(struct qup_i2c_dev *qup);
	/* function to read data from rx fifo */
	void (*read_rx_fifo)(struct qup_i2c_dev *qup);
	/* function to write tags in tx fifo for i2c read transfer */
	void (*write_rx_tags)(struct qup_i2c_dev *qup);
};

static irqreturn_t qup_i2c_interrupt(int irq, void *dev)
{
	struct qup_i2c_dev *qup = dev;
	struct qup_i2c_block *blk = &qup->blk;
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

	/*
	 * Check for BAM mode and returns if already error has come for current
	 * transfer. In Error case, sometimes, QUP generates more than one
	 * interrupt.
	 */
	if (qup->use_dma && (qup->qup_err || qup->bus_err))
		return IRQ_HANDLED;

	/* Reset the QUP State in case of error */
	if (qup_err || bus_err) {
		/*
		 * Don’t reset the QUP state in case of BAM mode. The BAM
		 * flush operation needs to be scheduled in transfer function
		 * which will clear the remaining schedule descriptors in BAM
		 * HW FIFO and generates the BAM interrupt.
		 */
		if (!qup->use_dma)
			writel(QUP_RESET_STATE, qup->base + QUP_STATE);
		goto done;
	}

	if (opflags & QUP_OUT_SVC_FLAG) {
		writel(QUP_OUT_SVC_FLAG, qup->base + QUP_OPERATIONAL);

		if (opflags & OUT_BLOCK_WRITE_REQ) {
			blk->tx_fifo_free += qup->out_blk_sz;
			if (qup->msg->flags & I2C_M_RD)
				qup->write_rx_tags(qup);
			else
				qup->write_tx_fifo(qup);
		}
	}

	if (opflags & QUP_IN_SVC_FLAG) {
		writel(QUP_IN_SVC_FLAG, qup->base + QUP_OPERATIONAL);

		if (!blk->is_rx_blk_mode) {
			blk->fifo_available += qup->in_fifo_sz;
			qup->read_rx_fifo(qup);
		} else if (opflags & IN_BLOCK_READ_REQ) {
			blk->fifo_available += qup->in_blk_sz;
			qup->read_rx_fifo(qup);
		}
	}

	if (qup->msg->flags & I2C_M_RD) {
		if (!blk->rx_bytes_read)
			return IRQ_HANDLED;
	} else {
		/*
		 * Ideally, QUP_MAX_OUTPUT_DONE_FLAG should be checked
		 * for FIFO mode also. But, QUP_MAX_OUTPUT_DONE_FLAG lags
		 * behind QUP_OUTPUT_SERVICE_FLAG sometimes. The only reason
		 * of interrupt for write message in FIFO mode is
		 * QUP_MAX_OUTPUT_DONE_FLAG condition.
		 */
		if (blk->is_tx_blk_mode && !(opflags & QUP_MX_OUTPUT_DONE))
			return IRQ_HANDLED;
	}

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

/* Check if I2C bus returns to IDLE state */
static int qup_i2c_bus_active(struct qup_i2c_dev *qup, int len)
{
	unsigned long timeout;
	u32 status;
	int ret = 0;

	timeout = jiffies + len * 4;
	for (;;) {
		status = readl(qup->base + QUP_I2C_STATUS);
		if (!(status & I2C_STATUS_BUS_ACTIVE))
			break;

		if (time_after(jiffies, timeout))
			ret = -ETIMEDOUT;

		usleep_range(len, len * 2);
	}

	return ret;
}

static void qup_i2c_write_tx_fifo_v1(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;
	struct i2c_msg *msg = qup->msg;
	u32 addr = i2c_8bit_addr_from_msg(msg);
	u32 qup_tag;
	int idx;
	u32 val;

	if (qup->pos == 0) {
		val = QUP_TAG_START | addr;
		idx = 1;
		blk->tx_fifo_free--;
	} else {
		val = 0;
		idx = 0;
	}

	while (blk->tx_fifo_free && qup->pos < msg->len) {
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
		blk->tx_fifo_free--;
	}
}

static void qup_i2c_set_blk_data(struct qup_i2c_dev *qup,
				 struct i2c_msg *msg)
{
	qup->blk.pos = 0;
	qup->blk.data_len = msg->len;
	qup->blk.count = DIV_ROUND_UP(msg->len, qup->blk_xfer_limit);
}

static int qup_i2c_get_data_len(struct qup_i2c_dev *qup)
{
	int data_len;

	if (qup->blk.data_len > qup->blk_xfer_limit)
		data_len = qup->blk_xfer_limit;
	else
		data_len = qup->blk.data_len;

	return data_len;
}

static bool qup_i2c_check_msg_len(struct i2c_msg *msg)
{
	return ((msg->flags & I2C_M_RD) && (msg->flags & I2C_M_RECV_LEN));
}

static int qup_i2c_set_tags_smb(u16 addr, u8 *tags, struct qup_i2c_dev *qup,
			struct i2c_msg *msg)
{
	int len = 0;

	if (qup->is_smbus_read) {
		tags[len++] = QUP_TAG_V2_DATARD_STOP;
		tags[len++] = qup_i2c_get_data_len(qup);
	} else {
		tags[len++] = QUP_TAG_V2_START;
		tags[len++] = addr & 0xff;

		if (msg->flags & I2C_M_TEN)
			tags[len++] = addr >> 8;

		tags[len++] = QUP_TAG_V2_DATARD;
		/* Read 1 byte indicating the length of the SMBus message */
		tags[len++] = 1;
	}
	return len;
}

static int qup_i2c_set_tags(u8 *tags, struct qup_i2c_dev *qup,
			    struct i2c_msg *msg)
{
	u16 addr = i2c_8bit_addr_from_msg(msg);
	int len = 0;
	int data_len;

	int last = (qup->blk.pos == (qup->blk.count - 1)) && (qup->is_last);

	/* Handle tags for SMBus block read */
	if (qup_i2c_check_msg_len(msg))
		return qup_i2c_set_tags_smb(addr, tags, qup, msg);

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
			tags[len++] = qup->blk.pos == (qup->blk.count - 1) ?
				      QUP_TAG_V2_DATARD_NACK :
				      QUP_TAG_V2_DATARD;
		else
			tags[len++] = QUP_TAG_V2_DATAWR;
	}

	data_len = qup_i2c_get_data_len(qup);

	/* 0 implies 256 bytes */
	if (data_len == QUP_READ_LIMIT)
		tags[len++] = 0;
	else
		tags[len++] = data_len;

	return len;
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

static int qup_i2c_bam_make_desc(struct qup_i2c_dev *qup, struct i2c_msg *msg)
{
	int ret = 0, limit = QUP_READ_LIMIT;
	u32 len = 0, blocks, rem;
	u32 i = 0, tlen, tx_len = 0;
	u8 *tags;

	qup->blk_xfer_limit = QUP_READ_LIMIT;
	qup_i2c_set_blk_data(qup, msg);

	blocks = qup->blk.count;
	rem = msg->len - (blocks - 1) * limit;

	if (msg->flags & I2C_M_RD) {
		while (qup->blk.pos < blocks) {
			tlen = (i == (blocks - 1)) ? rem : limit;
			tags = &qup->start_tag.start[qup->tag_buf_pos + len];
			len += qup_i2c_set_tags(tags, qup, msg);
			qup->blk.data_len -= tlen;

			/* scratch buf to read the start and len tags */
			ret = qup_sg_set_buf(&qup->brx.sg[qup->brx.sg_cnt++],
					     &qup->brx.tag.start[0],
					     2, qup, DMA_FROM_DEVICE);

			if (ret)
				return ret;

			ret = qup_sg_set_buf(&qup->brx.sg[qup->brx.sg_cnt++],
					     &msg->buf[limit * i],
					     tlen, qup,
					     DMA_FROM_DEVICE);
			if (ret)
				return ret;

			i++;
			qup->blk.pos = i;
		}
		ret = qup_sg_set_buf(&qup->btx.sg[qup->btx.sg_cnt++],
				     &qup->start_tag.start[qup->tag_buf_pos],
				     len, qup, DMA_TO_DEVICE);
		if (ret)
			return ret;

		qup->tag_buf_pos += len;
	} else {
		while (qup->blk.pos < blocks) {
			tlen = (i == (blocks - 1)) ? rem : limit;
			tags = &qup->start_tag.start[qup->tag_buf_pos + tx_len];
			len = qup_i2c_set_tags(tags, qup, msg);
			qup->blk.data_len -= tlen;

			ret = qup_sg_set_buf(&qup->btx.sg[qup->btx.sg_cnt++],
					     tags, len,
					     qup, DMA_TO_DEVICE);
			if (ret)
				return ret;

			tx_len += len;
			ret = qup_sg_set_buf(&qup->btx.sg[qup->btx.sg_cnt++],
					     &msg->buf[limit * i],
					     tlen, qup, DMA_TO_DEVICE);
			if (ret)
				return ret;
			i++;
			qup->blk.pos = i;
		}

		qup->tag_buf_pos += tx_len;
	}

	return 0;
}

static int qup_i2c_bam_schedule_desc(struct qup_i2c_dev *qup)
{
	struct dma_async_tx_descriptor *txd, *rxd = NULL;
	int ret = 0;
	dma_cookie_t cookie_rx, cookie_tx;
	u32 len = 0;
	u32 tx_cnt = qup->btx.sg_cnt, rx_cnt = qup->brx.sg_cnt;

	/* schedule the EOT and FLUSH I2C tags */
	len = 1;
	if (rx_cnt) {
		qup->btx.tag.start[0] = QUP_BAM_INPUT_EOT;
		len++;

		/* scratch buf to read the BAM EOT FLUSH tags */
		ret = qup_sg_set_buf(&qup->brx.sg[rx_cnt++],
				     &qup->brx.tag.start[0],
				     1, qup, DMA_FROM_DEVICE);
		if (ret)
			return ret;
	}

	qup->btx.tag.start[len - 1] = QUP_BAM_FLUSH_STOP;
	ret = qup_sg_set_buf(&qup->btx.sg[tx_cnt++], &qup->btx.tag.start[0],
			     len, qup, DMA_TO_DEVICE);
	if (ret)
		return ret;

	txd = dmaengine_prep_slave_sg(qup->btx.dma, qup->btx.sg, tx_cnt,
				      DMA_MEM_TO_DEV,
				      DMA_PREP_INTERRUPT | DMA_PREP_FENCE);
	if (!txd) {
		dev_err(qup->dev, "failed to get tx desc\n");
		ret = -EINVAL;
		goto desc_err;
	}

	if (!rx_cnt) {
		txd->callback = qup_i2c_bam_cb;
		txd->callback_param = qup;
	}

	cookie_tx = dmaengine_submit(txd);
	if (dma_submit_error(cookie_tx)) {
		ret = -EINVAL;
		goto desc_err;
	}

	dma_async_issue_pending(qup->btx.dma);

	if (rx_cnt) {
		rxd = dmaengine_prep_slave_sg(qup->brx.dma, qup->brx.sg,
					      rx_cnt, DMA_DEV_TO_MEM,
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

	if (!wait_for_completion_timeout(&qup->xfer, qup->xfer_timeout)) {
		dev_err(qup->dev, "normal trans timed out\n");
		ret = -ETIMEDOUT;
	}

	if (ret || qup->bus_err || qup->qup_err) {
		reinit_completion(&qup->xfer);

		if (qup_i2c_change_state(qup, QUP_RUN_STATE)) {
			dev_err(qup->dev, "change to run state timed out");
			goto desc_err;
		}

		qup_i2c_flush(qup);

		/* wait for remaining interrupts to occur */
		if (!wait_for_completion_timeout(&qup->xfer, HZ))
			dev_err(qup->dev, "flush timed out\n");

		ret =  (qup->bus_err & QUP_I2C_NACK_FLAG) ? -ENXIO : -EIO;
	}

desc_err:
	dma_unmap_sg(qup->dev, qup->btx.sg, tx_cnt, DMA_TO_DEVICE);

	if (rx_cnt)
		dma_unmap_sg(qup->dev, qup->brx.sg, rx_cnt,
			     DMA_FROM_DEVICE);

	return ret;
}

static void qup_i2c_bam_clear_tag_buffers(struct qup_i2c_dev *qup)
{
	qup->btx.sg_cnt = 0;
	qup->brx.sg_cnt = 0;
	qup->tag_buf_pos = 0;
}

static int qup_i2c_bam_xfer(struct i2c_adapter *adap, struct i2c_msg *msg,
			    int num)
{
	struct qup_i2c_dev *qup = i2c_get_adapdata(adap);
	int ret = 0;
	int idx = 0;

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
	qup_i2c_bam_clear_tag_buffers(qup);

	for (idx = 0; idx < num; idx++) {
		qup->msg = msg + idx;
		qup->is_last = idx == (num - 1);

		ret = qup_i2c_bam_make_desc(qup, qup->msg);
		if (ret)
			break;

		/*
		 * Make DMA descriptor and schedule the BAM transfer if its
		 * already crossed the maximum length. Since the memory for all
		 * tags buffers have been taken for 2 maximum possible
		 * transfers length so it will never cross the buffer actual
		 * length.
		 */
		if (qup->btx.sg_cnt > qup->max_xfer_sg_len ||
		    qup->brx.sg_cnt > qup->max_xfer_sg_len ||
		    qup->is_last) {
			ret = qup_i2c_bam_schedule_desc(qup);
			if (ret)
				break;

			qup_i2c_bam_clear_tag_buffers(qup);
		}
	}

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

	left = wait_for_completion_timeout(&qup->xfer, qup->xfer_timeout);
	if (!left) {
		writel(1, qup->base + QUP_SW_RESET);
		ret = -ETIMEDOUT;
	}

	if (qup->bus_err || qup->qup_err)
		ret =  (qup->bus_err & QUP_I2C_NACK_FLAG) ? -ENXIO : -EIO;

	return ret;
}

static void qup_i2c_read_rx_fifo_v1(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;
	struct i2c_msg *msg = qup->msg;
	u32 val = 0;
	int idx = 0;

	while (blk->fifo_available && qup->pos < msg->len) {
		if ((idx & 1) == 0) {
			/* Reading 2 words at time */
			val = readl(qup->base + QUP_IN_FIFO_BASE);
			msg->buf[qup->pos++] = val & 0xFF;
		} else {
			msg->buf[qup->pos++] = val >> QUP_MSW_SHIFT;
		}
		idx++;
		blk->fifo_available--;
	}

	if (qup->pos == msg->len)
		blk->rx_bytes_read = true;
}

static void qup_i2c_write_rx_tags_v1(struct qup_i2c_dev *qup)
{
	struct i2c_msg *msg = qup->msg;
	u32 addr, len, val;

	addr = i2c_8bit_addr_from_msg(msg);

	/* 0 is used to specify a length 256 (QUP_READ_LIMIT) */
	len = (msg->len == QUP_READ_LIMIT) ? 0 : msg->len;

	val = ((QUP_TAG_REC | len) << QUP_MSW_SHIFT) | QUP_TAG_START | addr;
	writel(val, qup->base + QUP_OUT_FIFO_BASE);
}

static void qup_i2c_conf_v1(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;
	u32 qup_config = I2C_MINI_CORE | I2C_N_VAL;
	u32 io_mode = QUP_REPACK_EN;

	blk->is_tx_blk_mode =
		blk->total_tx_len > qup->out_fifo_sz ? true : false;
	blk->is_rx_blk_mode =
		blk->total_rx_len > qup->in_fifo_sz ? true : false;

	if (blk->is_tx_blk_mode) {
		io_mode |= QUP_OUTPUT_BLK_MODE;
		writel(0, qup->base + QUP_MX_WRITE_CNT);
		writel(blk->total_tx_len, qup->base + QUP_MX_OUTPUT_CNT);
	} else {
		writel(0, qup->base + QUP_MX_OUTPUT_CNT);
		writel(blk->total_tx_len, qup->base + QUP_MX_WRITE_CNT);
	}

	if (blk->total_rx_len) {
		if (blk->is_rx_blk_mode) {
			io_mode |= QUP_INPUT_BLK_MODE;
			writel(0, qup->base + QUP_MX_READ_CNT);
			writel(blk->total_rx_len, qup->base + QUP_MX_INPUT_CNT);
		} else {
			writel(0, qup->base + QUP_MX_INPUT_CNT);
			writel(blk->total_rx_len, qup->base + QUP_MX_READ_CNT);
		}
	} else {
		qup_config |= QUP_NO_INPUT;
	}

	writel(qup_config, qup->base + QUP_CONFIG);
	writel(io_mode, qup->base + QUP_IO_MODE);
}

static void qup_i2c_clear_blk_v1(struct qup_i2c_block *blk)
{
	blk->tx_fifo_free = 0;
	blk->fifo_available = 0;
	blk->rx_bytes_read = false;
}

static int qup_i2c_conf_xfer_v1(struct qup_i2c_dev *qup, bool is_rx)
{
	struct qup_i2c_block *blk = &qup->blk;
	int ret;

	qup_i2c_clear_blk_v1(blk);
	qup_i2c_conf_v1(qup);
	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
	if (ret)
		return ret;

	writel(qup->clk_ctl, qup->base + QUP_I2C_CLK_CTL);

	ret = qup_i2c_change_state(qup, QUP_PAUSE_STATE);
	if (ret)
		return ret;

	reinit_completion(&qup->xfer);
	enable_irq(qup->irq);
	if (!blk->is_tx_blk_mode) {
		blk->tx_fifo_free = qup->out_fifo_sz;

		if (is_rx)
			qup_i2c_write_rx_tags_v1(qup);
		else
			qup_i2c_write_tx_fifo_v1(qup);
	}

	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
	if (ret)
		goto err;

	ret = qup_i2c_wait_for_complete(qup, qup->msg);
	if (ret)
		goto err;

	ret = qup_i2c_bus_active(qup, ONE_BYTE);

err:
	disable_irq(qup->irq);
	return ret;
}

static int qup_i2c_write_one(struct qup_i2c_dev *qup)
{
	struct i2c_msg *msg = qup->msg;
	struct qup_i2c_block *blk = &qup->blk;

	qup->pos = 0;
	blk->total_tx_len = msg->len + 1;
	blk->total_rx_len = 0;

	return qup_i2c_conf_xfer_v1(qup, false);
}

static int qup_i2c_read_one(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;

	qup->pos = 0;
	blk->total_tx_len = 2;
	blk->total_rx_len = qup->msg->len;

	return qup_i2c_conf_xfer_v1(qup, true);
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

		if (qup_i2c_check_msg_len(&msgs[idx])) {
			ret = -EINVAL;
			goto out;
		}

		qup->msg = &msgs[idx];
		if (msgs[idx].flags & I2C_M_RD)
			ret = qup_i2c_read_one(qup);
		else
			ret = qup_i2c_write_one(qup);

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

/*
 * Configure registers related with reconfiguration during run and call it
 * before each i2c sub transfer.
 */
static void qup_i2c_conf_count_v2(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;
	u32 qup_config = I2C_MINI_CORE | I2C_N_VAL_V2;

	if (blk->is_tx_blk_mode)
		writel(qup->config_run | blk->total_tx_len,
		       qup->base + QUP_MX_OUTPUT_CNT);
	else
		writel(qup->config_run | blk->total_tx_len,
		       qup->base + QUP_MX_WRITE_CNT);

	if (blk->total_rx_len) {
		if (blk->is_rx_blk_mode)
			writel(qup->config_run | blk->total_rx_len,
			       qup->base + QUP_MX_INPUT_CNT);
		else
			writel(qup->config_run | blk->total_rx_len,
			       qup->base + QUP_MX_READ_CNT);
	} else {
		qup_config |= QUP_NO_INPUT;
	}

	writel(qup_config, qup->base + QUP_CONFIG);
}

/*
 * Configure registers related with transfer mode (FIFO/Block)
 * before starting of i2c transfer. It will be called only once in
 * QUP RESET state.
 */
static void qup_i2c_conf_mode_v2(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;
	u32 io_mode = QUP_REPACK_EN;

	if (blk->is_tx_blk_mode) {
		io_mode |= QUP_OUTPUT_BLK_MODE;
		writel(0, qup->base + QUP_MX_WRITE_CNT);
	} else {
		writel(0, qup->base + QUP_MX_OUTPUT_CNT);
	}

	if (blk->is_rx_blk_mode) {
		io_mode |= QUP_INPUT_BLK_MODE;
		writel(0, qup->base + QUP_MX_READ_CNT);
	} else {
		writel(0, qup->base + QUP_MX_INPUT_CNT);
	}

	writel(io_mode, qup->base + QUP_IO_MODE);
}

/* Clear required variables before starting of any QUP v2 sub transfer. */
static void qup_i2c_clear_blk_v2(struct qup_i2c_block *blk)
{
	blk->send_last_word = false;
	blk->tx_tags_sent = false;
	blk->tx_fifo_data = 0;
	blk->tx_fifo_data_pos = 0;
	blk->tx_fifo_free = 0;

	blk->rx_tags_fetched = false;
	blk->rx_bytes_read = false;
	blk->rx_fifo_data = 0;
	blk->rx_fifo_data_pos = 0;
	blk->fifo_available = 0;
}

/* Receive data from RX FIFO for read message in QUP v2 i2c transfer. */
static void qup_i2c_recv_data(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;
	int j;

	for (j = blk->rx_fifo_data_pos;
	     blk->cur_blk_len && blk->fifo_available;
	     blk->cur_blk_len--, blk->fifo_available--) {
		if (j == 0)
			blk->rx_fifo_data = readl(qup->base + QUP_IN_FIFO_BASE);

		*(blk->cur_data++) = blk->rx_fifo_data;
		blk->rx_fifo_data >>= 8;

		if (j == 3)
			j = 0;
		else
			j++;
	}

	blk->rx_fifo_data_pos = j;
}

/* Receive tags for read message in QUP v2 i2c transfer. */
static void qup_i2c_recv_tags(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;

	blk->rx_fifo_data = readl(qup->base + QUP_IN_FIFO_BASE);
	blk->rx_fifo_data >>= blk->rx_tag_len  * 8;
	blk->rx_fifo_data_pos = blk->rx_tag_len;
	blk->fifo_available -= blk->rx_tag_len;
}

/*
 * Read the data and tags from RX FIFO. Since in read case, the tags will be
 * preceded by received data bytes so
 * 1. Check if rx_tags_fetched is false i.e. the start of QUP block so receive
 *    all tag bytes and discard that.
 * 2. Read the data from RX FIFO. When all the data bytes have been read then
 *    set rx_bytes_read to true.
 */
static void qup_i2c_read_rx_fifo_v2(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;

	if (!blk->rx_tags_fetched) {
		qup_i2c_recv_tags(qup);
		blk->rx_tags_fetched = true;
	}

	qup_i2c_recv_data(qup);
	if (!blk->cur_blk_len)
		blk->rx_bytes_read = true;
}

/*
 * Write bytes in TX FIFO for write message in QUP v2 i2c transfer. QUP TX FIFO
 * write works on word basis (4 bytes). Append new data byte write for TX FIFO
 * in tx_fifo_data and write to TX FIFO when all the 4 bytes are present.
 */
static void
qup_i2c_write_blk_data(struct qup_i2c_dev *qup, u8 **data, unsigned int *len)
{
	struct qup_i2c_block *blk = &qup->blk;
	unsigned int j;

	for (j = blk->tx_fifo_data_pos; *len && blk->tx_fifo_free;
	     (*len)--, blk->tx_fifo_free--) {
		blk->tx_fifo_data |= *(*data)++ << (j * 8);
		if (j == 3) {
			writel(blk->tx_fifo_data,
			       qup->base + QUP_OUT_FIFO_BASE);
			blk->tx_fifo_data = 0x0;
			j = 0;
		} else {
			j++;
		}
	}

	blk->tx_fifo_data_pos = j;
}

/* Transfer tags for read message in QUP v2 i2c transfer. */
static void qup_i2c_write_rx_tags_v2(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;

	qup_i2c_write_blk_data(qup, &blk->cur_tx_tags, &blk->tx_tag_len);
	if (blk->tx_fifo_data_pos)
		writel(blk->tx_fifo_data, qup->base + QUP_OUT_FIFO_BASE);
}

/*
 * Write the data and tags in TX FIFO. Since in write case, both tags and data
 * need to be written and QUP write tags can have maximum 256 data length, so
 *
 * 1. Check if tx_tags_sent is false i.e. the start of QUP block so write the
 *    tags to TX FIFO and set tx_tags_sent to true.
 * 2. Check if send_last_word is true. It will be set when last few data bytes
 *    (less than 4 bytes) are reamining to be written in FIFO because of no FIFO
 *    space. All this data bytes are available in tx_fifo_data so write this
 *    in FIFO.
 * 3. Write the data to TX FIFO and check for cur_blk_len. If it is non zero
 *    then more data is pending otherwise following 3 cases can be possible
 *    a. if tx_fifo_data_pos is zero i.e. all the data bytes in this block
 *       have been written in TX FIFO so nothing else is required.
 *    b. tx_fifo_free is non zero i.e tx FIFO is free so copy the remaining data
 *       from tx_fifo_data to tx FIFO. Since, qup_i2c_write_blk_data do write
 *	 in 4 bytes and FIFO space is in multiple of 4 bytes so tx_fifo_free
 *       will be always greater than or equal to 4 bytes.
 *    c. tx_fifo_free is zero. In this case, last few bytes (less than 4
 *       bytes) are copied to tx_fifo_data but couldn't be sent because of
 *       FIFO full so make send_last_word true.
 */
static void qup_i2c_write_tx_fifo_v2(struct qup_i2c_dev *qup)
{
	struct qup_i2c_block *blk = &qup->blk;

	if (!blk->tx_tags_sent) {
		qup_i2c_write_blk_data(qup, &blk->cur_tx_tags,
				       &blk->tx_tag_len);
		blk->tx_tags_sent = true;
	}

	if (blk->send_last_word)
		goto send_last_word;

	qup_i2c_write_blk_data(qup, &blk->cur_data, &blk->cur_blk_len);
	if (!blk->cur_blk_len) {
		if (!blk->tx_fifo_data_pos)
			return;

		if (blk->tx_fifo_free)
			goto send_last_word;

		blk->send_last_word = true;
	}

	return;

send_last_word:
	writel(blk->tx_fifo_data, qup->base + QUP_OUT_FIFO_BASE);
}

/*
 * Main transfer function which read or write i2c data.
 * The QUP v2 supports reconfiguration during run in which multiple i2c sub
 * transfers can be scheduled.
 */
static int
qup_i2c_conf_xfer_v2(struct qup_i2c_dev *qup, bool is_rx, bool is_first,
		     bool change_pause_state)
{
	struct qup_i2c_block *blk = &qup->blk;
	struct i2c_msg *msg = qup->msg;
	int ret;

	/*
	 * Check if its SMBus Block read for which the top level read will be
	 * done into 2 QUP reads. One with message length 1 while other one is
	 * with actual length.
	 */
	if (qup_i2c_check_msg_len(msg)) {
		if (qup->is_smbus_read) {
			/*
			 * If the message length is already read in
			 * the first byte of the buffer, account for
			 * that by setting the offset
			 */
			blk->cur_data += 1;
			is_first = false;
		} else {
			change_pause_state = false;
		}
	}

	qup->config_run = is_first ? 0 : QUP_I2C_MX_CONFIG_DURING_RUN;

	qup_i2c_clear_blk_v2(blk);
	qup_i2c_conf_count_v2(qup);

	/* If it is first sub transfer, then configure i2c bus clocks */
	if (is_first) {
		ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
		if (ret)
			return ret;

		writel(qup->clk_ctl, qup->base + QUP_I2C_CLK_CTL);

		ret = qup_i2c_change_state(qup, QUP_PAUSE_STATE);
		if (ret)
			return ret;
	}

	reinit_completion(&qup->xfer);
	enable_irq(qup->irq);
	/*
	 * In FIFO mode, tx FIFO can be written directly while in block mode the
	 * it will be written after getting OUT_BLOCK_WRITE_REQ interrupt
	 */
	if (!blk->is_tx_blk_mode) {
		blk->tx_fifo_free = qup->out_fifo_sz;

		if (is_rx)
			qup_i2c_write_rx_tags_v2(qup);
		else
			qup_i2c_write_tx_fifo_v2(qup);
	}

	ret = qup_i2c_change_state(qup, QUP_RUN_STATE);
	if (ret)
		goto err;

	ret = qup_i2c_wait_for_complete(qup, msg);
	if (ret)
		goto err;

	/* Move to pause state for all the transfers, except last one */
	if (change_pause_state) {
		ret = qup_i2c_change_state(qup, QUP_PAUSE_STATE);
		if (ret)
			goto err;
	}

err:
	disable_irq(qup->irq);
	return ret;
}

/*
 * Transfer one read/write message in i2c transfer. It splits the message into
 * multiple of blk_xfer_limit data length blocks and schedule each
 * QUP block individually.
 */
static int qup_i2c_xfer_v2_msg(struct qup_i2c_dev *qup, int msg_id, bool is_rx)
{
	int ret = 0;
	unsigned int data_len, i;
	struct i2c_msg *msg = qup->msg;
	struct qup_i2c_block *blk = &qup->blk;
	u8 *msg_buf = msg->buf;

	qup->blk_xfer_limit = is_rx ? RECV_MAX_DATA_LEN : QUP_READ_LIMIT;
	qup_i2c_set_blk_data(qup, msg);

	for (i = 0; i < blk->count; i++) {
		data_len =  qup_i2c_get_data_len(qup);
		blk->pos = i;
		blk->cur_tx_tags = blk->tags;
		blk->cur_blk_len = data_len;
		blk->tx_tag_len =
			qup_i2c_set_tags(blk->cur_tx_tags, qup, qup->msg);

		blk->cur_data = msg_buf;

		if (is_rx) {
			blk->total_tx_len = blk->tx_tag_len;
			blk->rx_tag_len = 2;
			blk->total_rx_len = blk->rx_tag_len + data_len;
		} else {
			blk->total_tx_len = blk->tx_tag_len + data_len;
			blk->total_rx_len = 0;
		}

		ret = qup_i2c_conf_xfer_v2(qup, is_rx, !msg_id && !i,
					   !qup->is_last || i < blk->count - 1);
		if (ret)
			return ret;

		/* Handle SMBus block read length */
		if (qup_i2c_check_msg_len(msg) && msg->len == 1 &&
		    !qup->is_smbus_read) {
			if (msg->buf[0] > I2C_SMBUS_BLOCK_MAX)
				return -EPROTO;

			msg->len = msg->buf[0];
			qup->is_smbus_read = true;
			ret = qup_i2c_xfer_v2_msg(qup, msg_id, true);
			qup->is_smbus_read = false;
			if (ret)
				return ret;

			msg->len += 1;
		}

		msg_buf += data_len;
		blk->data_len -= qup->blk_xfer_limit;
	}

	return ret;
}

/*
 * QUP v2 supports 3 modes
 * Programmed IO using FIFO mode : Less than FIFO size
 * Programmed IO using Block mode : Greater than FIFO size
 * DMA using BAM : Appropriate for any transaction size but the address should
 *		   be DMA applicable
 *
 * This function determines the mode which will be used for this transfer. An
 * i2c transfer contains multiple message. Following are the rules to determine
 * the mode used.
 * 1. Determine complete length, maximum tx and rx length for complete transfer.
 * 2. If complete transfer length is greater than fifo size then use the DMA
 *    mode.
 * 3. In FIFO or block mode, tx and rx can operate in different mode so check
 *    for maximum tx and rx length to determine mode.
 */
static int
qup_i2c_determine_mode_v2(struct qup_i2c_dev *qup,
			  struct i2c_msg msgs[], int num)
{
	int idx;
	bool no_dma = false;
	unsigned int max_tx_len = 0, max_rx_len = 0, total_len = 0;

	/* All i2c_msgs should be transferred using either dma or cpu */
	for (idx = 0; idx < num; idx++) {
		if (msgs[idx].len == 0)
			return -EINVAL;

		if (msgs[idx].flags & I2C_M_RD)
			max_rx_len = max_t(unsigned int, max_rx_len,
					   msgs[idx].len);
		else
			max_tx_len = max_t(unsigned int, max_tx_len,
					   msgs[idx].len);

		if (is_vmalloc_addr(msgs[idx].buf))
			no_dma = true;

		total_len += msgs[idx].len;
	}

	if (!no_dma && qup->is_dma &&
	    (total_len > qup->out_fifo_sz || total_len > qup->in_fifo_sz)) {
		qup->use_dma = true;
	} else {
		qup->blk.is_tx_blk_mode = max_tx_len > qup->out_fifo_sz -
			QUP_MAX_TAGS_LEN ? true : false;
		qup->blk.is_rx_blk_mode = max_rx_len > qup->in_fifo_sz -
			READ_RX_TAGS_LEN ? true : false;
	}

	return 0;
}

static int qup_i2c_xfer_v2(struct i2c_adapter *adap,
			   struct i2c_msg msgs[],
			   int num)
{
	struct qup_i2c_dev *qup = i2c_get_adapdata(adap);
	int ret, idx = 0;

	qup->bus_err = 0;
	qup->qup_err = 0;

	ret = pm_runtime_get_sync(qup->dev);
	if (ret < 0)
		goto out;

	ret = qup_i2c_determine_mode_v2(qup, msgs, num);
	if (ret)
		goto out;

	writel(1, qup->base + QUP_SW_RESET);
	ret = qup_i2c_poll_state(qup, QUP_RESET_STATE);
	if (ret)
		goto out;

	/* Configure QUP as I2C mini core */
	writel(I2C_MINI_CORE | I2C_N_VAL_V2, qup->base + QUP_CONFIG);
	writel(QUP_V2_TAGS_EN, qup->base + QUP_I2C_MASTER_GEN);

	if (qup_i2c_poll_state_i2c_master(qup)) {
		ret = -EIO;
		goto out;
	}

	if (qup->use_dma) {
		reinit_completion(&qup->xfer);
		ret = qup_i2c_bam_xfer(adap, &msgs[0], num);
		qup->use_dma = false;
	} else {
		qup_i2c_conf_mode_v2(qup);

		for (idx = 0; idx < num; idx++) {
			qup->msg = &msgs[idx];
			qup->is_last = idx == (num - 1);

			ret = qup_i2c_xfer_v2_msg(qup, idx,
					!!(msgs[idx].flags & I2C_M_RD));
			if (ret)
				break;
		}
		qup->msg = NULL;
	}

	if (!ret)
		ret = qup_i2c_bus_active(qup, ONE_BYTE);

	if (!ret)
		qup_i2c_change_state(qup, QUP_RESET_STATE);

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
static const struct i2c_adapter_quirks qup_i2c_quirks = {
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

static const struct acpi_device_id qup_i2c_acpi_match[] = {
	{ "QCOM8010"},
	{ },
};
MODULE_DEVICE_TABLE(acpi, qup_i2c_acpi_match);

static int qup_i2c_probe(struct platform_device *pdev)
{
	static const int blk_sizes[] = {4, 16, 32};
	struct qup_i2c_dev *qup;
	unsigned long one_bit_t;
	struct resource *res;
	u32 io_mode, hw_ver, size;
	int ret, fs_div, hs_div;
	u32 src_clk_freq = DEFAULT_SRC_CLK;
	u32 clk_freq = DEFAULT_CLK_FREQ;
	int blocks;
	bool is_qup_v1;

	qup = devm_kzalloc(&pdev->dev, sizeof(*qup), GFP_KERNEL);
	if (!qup)
		return -ENOMEM;

	qup->dev = &pdev->dev;
	init_completion(&qup->xfer);
	platform_set_drvdata(pdev, qup);

	if (scl_freq) {
		dev_notice(qup->dev, "Using override frequency of %u\n", scl_freq);
		clk_freq = scl_freq;
	} else {
		ret = device_property_read_u32(qup->dev, "clock-frequency", &clk_freq);
		if (ret) {
			dev_notice(qup->dev, "using default clock-frequency %d",
				DEFAULT_CLK_FREQ);
		}
	}

	if (of_device_is_compatible(pdev->dev.of_node, "qcom,i2c-qup-v1.1.1")) {
		qup->adap.algo = &qup_i2c_algo;
		qup->adap.quirks = &qup_i2c_quirks;
		is_qup_v1 = true;
	} else {
		qup->adap.algo = &qup_i2c_algo_v2;
		is_qup_v1 = false;
		if (acpi_match_device(qup_i2c_acpi_match, qup->dev))
			goto nodma;
		else
			ret = qup_i2c_req_dma(qup);

		if (ret == -EPROBE_DEFER)
			goto fail_dma;
		else if (ret != 0)
			goto nodma;

		qup->max_xfer_sg_len = (MX_BLOCKS << 1);
		blocks = (MX_DMA_BLOCKS << 1) + 1;
		qup->btx.sg = devm_kcalloc(&pdev->dev,
					   blocks, sizeof(*qup->btx.sg),
					   GFP_KERNEL);
		if (!qup->btx.sg) {
			ret = -ENOMEM;
			goto fail_dma;
		}
		sg_init_table(qup->btx.sg, blocks);

		qup->brx.sg = devm_kcalloc(&pdev->dev,
					   blocks, sizeof(*qup->brx.sg),
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
	/* We support frequencies up to FAST Mode Plus (1MHz) */
	if (!clk_freq || clk_freq > I2C_FAST_MODE_PLUS_FREQ) {
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

	if (has_acpi_companion(qup->dev)) {
		ret = device_property_read_u32(qup->dev,
				"src-clock-hz", &src_clk_freq);
		if (ret) {
			dev_notice(qup->dev, "using default src-clock-hz %d",
				DEFAULT_SRC_CLK);
		}
		ACPI_COMPANION_SET(&qup->adap.dev, ACPI_COMPANION(qup->dev));
	} else {
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
		src_clk_freq = clk_get_rate(qup->clk);
	}

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
	qup->out_blk_sz = blk_sizes[size];

	size = QUP_INPUT_BLOCK_SIZE(io_mode);
	if (size >= ARRAY_SIZE(blk_sizes)) {
		ret = -EIO;
		goto fail;
	}
	qup->in_blk_sz = blk_sizes[size];

	if (is_qup_v1) {
		/*
		 * in QUP v1, QUP_CONFIG uses N as 15 i.e 16 bits constitutes a
		 * single transfer but the block size is in bytes so divide the
		 * in_blk_sz and out_blk_sz by 2
		 */
		qup->in_blk_sz /= 2;
		qup->out_blk_sz /= 2;
		qup->write_tx_fifo = qup_i2c_write_tx_fifo_v1;
		qup->read_rx_fifo = qup_i2c_read_rx_fifo_v1;
		qup->write_rx_tags = qup_i2c_write_rx_tags_v1;
	} else {
		qup->write_tx_fifo = qup_i2c_write_tx_fifo_v2;
		qup->read_rx_fifo = qup_i2c_read_rx_fifo_v2;
		qup->write_rx_tags = qup_i2c_write_rx_tags_v2;
	}

	size = QUP_OUTPUT_FIFO_SIZE(io_mode);
	qup->out_fifo_sz = qup->out_blk_sz * (2 << size);

	size = QUP_INPUT_FIFO_SIZE(io_mode);
	qup->in_fifo_sz = qup->in_blk_sz * (2 << size);

	hs_div = 3;
	if (clk_freq <= I2C_STANDARD_FREQ) {
		fs_div = ((src_clk_freq / clk_freq) / 2) - 3;
		qup->clk_ctl = (hs_div << 8) | (fs_div & 0xff);
	} else {
		/* 33%/66% duty cycle */
		fs_div = ((src_clk_freq / clk_freq) - 6) * 2 / 3;
		qup->clk_ctl = ((fs_div / 2) << 16) | (hs_div << 8) | (fs_div & 0xff);
	}

	/*
	 * Time it takes for a byte to be clocked out on the bus.
	 * Each byte takes 9 clock cycles (8 bits + 1 ack).
	 */
	one_bit_t = (USEC_PER_SEC / clk_freq) + 1;
	qup->one_byte_t = one_bit_t * 9;
	qup->xfer_timeout = TOUT_MIN * HZ +
		usecs_to_jiffies(MX_DMA_TX_RX_LEN * qup->one_byte_t);

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
		.acpi_match_table = ACPI_PTR(qup_i2c_acpi_match),
	},
};

module_platform_driver(qup_i2c_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2c_qup");
