// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2014, The Linux foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#define QUP_CONFIG			0x0000
#define QUP_STATE			0x0004
#define QUP_IO_M_MODES			0x0008
#define QUP_SW_RESET			0x000c
#define QUP_OPERATIONAL			0x0018
#define QUP_ERROR_FLAGS			0x001c
#define QUP_ERROR_FLAGS_EN		0x0020
#define QUP_OPERATIONAL_MASK		0x0028
#define QUP_HW_VERSION			0x0030
#define QUP_MX_OUTPUT_CNT		0x0100
#define QUP_OUTPUT_FIFO			0x0110
#define QUP_MX_WRITE_CNT		0x0150
#define QUP_MX_INPUT_CNT		0x0200
#define QUP_MX_READ_CNT			0x0208
#define QUP_INPUT_FIFO			0x0218

#define SPI_CONFIG			0x0300
#define SPI_IO_CONTROL			0x0304
#define SPI_ERROR_FLAGS			0x0308
#define SPI_ERROR_FLAGS_EN		0x030c

/* QUP_CONFIG fields */
#define QUP_CONFIG_SPI_MODE		(1 << 8)
#define QUP_CONFIG_CLOCK_AUTO_GATE	BIT(13)
#define QUP_CONFIG_NO_INPUT		BIT(7)
#define QUP_CONFIG_NO_OUTPUT		BIT(6)
#define QUP_CONFIG_N			0x001f

/* QUP_STATE fields */
#define QUP_STATE_VALID			BIT(2)
#define QUP_STATE_RESET			0
#define QUP_STATE_RUN			1
#define QUP_STATE_PAUSE			3
#define QUP_STATE_MASK			3
#define QUP_STATE_CLEAR			2

#define QUP_HW_VERSION_2_1_1		0x20010001

/* QUP_IO_M_MODES fields */
#define QUP_IO_M_PACK_EN		BIT(15)
#define QUP_IO_M_UNPACK_EN		BIT(14)
#define QUP_IO_M_INPUT_MODE_MASK_SHIFT	12
#define QUP_IO_M_OUTPUT_MODE_MASK_SHIFT	10
#define QUP_IO_M_INPUT_MODE_MASK	(3 << QUP_IO_M_INPUT_MODE_MASK_SHIFT)
#define QUP_IO_M_OUTPUT_MODE_MASK	(3 << QUP_IO_M_OUTPUT_MODE_MASK_SHIFT)

#define QUP_IO_M_OUTPUT_BLOCK_SIZE(x)	(((x) & (0x03 << 0)) >> 0)
#define QUP_IO_M_OUTPUT_FIFO_SIZE(x)	(((x) & (0x07 << 2)) >> 2)
#define QUP_IO_M_INPUT_BLOCK_SIZE(x)	(((x) & (0x03 << 5)) >> 5)
#define QUP_IO_M_INPUT_FIFO_SIZE(x)	(((x) & (0x07 << 7)) >> 7)

#define QUP_IO_M_MODE_FIFO		0
#define QUP_IO_M_MODE_BLOCK		1
#define QUP_IO_M_MODE_DMOV		2
#define QUP_IO_M_MODE_BAM		3

/* QUP_OPERATIONAL fields */
#define QUP_OP_IN_BLOCK_READ_REQ	BIT(13)
#define QUP_OP_OUT_BLOCK_WRITE_REQ	BIT(12)
#define QUP_OP_MAX_INPUT_DONE_FLAG	BIT(11)
#define QUP_OP_MAX_OUTPUT_DONE_FLAG	BIT(10)
#define QUP_OP_IN_SERVICE_FLAG		BIT(9)
#define QUP_OP_OUT_SERVICE_FLAG		BIT(8)
#define QUP_OP_IN_FIFO_FULL		BIT(7)
#define QUP_OP_OUT_FIFO_FULL		BIT(6)
#define QUP_OP_IN_FIFO_NOT_EMPTY	BIT(5)
#define QUP_OP_OUT_FIFO_NOT_EMPTY	BIT(4)

/* QUP_ERROR_FLAGS and QUP_ERROR_FLAGS_EN fields */
#define QUP_ERROR_OUTPUT_OVER_RUN	BIT(5)
#define QUP_ERROR_INPUT_UNDER_RUN	BIT(4)
#define QUP_ERROR_OUTPUT_UNDER_RUN	BIT(3)
#define QUP_ERROR_INPUT_OVER_RUN	BIT(2)

/* SPI_CONFIG fields */
#define SPI_CONFIG_HS_MODE		BIT(10)
#define SPI_CONFIG_INPUT_FIRST		BIT(9)
#define SPI_CONFIG_LOOPBACK		BIT(8)

/* SPI_IO_CONTROL fields */
#define SPI_IO_C_FORCE_CS		BIT(11)
#define SPI_IO_C_CLK_IDLE_HIGH		BIT(10)
#define SPI_IO_C_MX_CS_MODE		BIT(8)
#define SPI_IO_C_CS_N_POLARITY_0	BIT(4)
#define SPI_IO_C_CS_SELECT(x)		(((x) & 3) << 2)
#define SPI_IO_C_CS_SELECT_MASK		0x000c
#define SPI_IO_C_TRISTATE_CS		BIT(1)
#define SPI_IO_C_NO_TRI_STATE		BIT(0)

/* SPI_ERROR_FLAGS and SPI_ERROR_FLAGS_EN fields */
#define SPI_ERROR_CLK_OVER_RUN		BIT(1)
#define SPI_ERROR_CLK_UNDER_RUN		BIT(0)

#define SPI_NUM_CHIPSELECTS		4

#define SPI_MAX_XFER			(SZ_64K - 64)

/* high speed mode is when bus rate is greater then 26MHz */
#define SPI_HS_MIN_RATE			26000000
#define SPI_MAX_RATE			50000000

#define SPI_DELAY_THRESHOLD		1
#define SPI_DELAY_RETRY			10

struct spi_qup {
	void __iomem		*base;
	struct device		*dev;
	struct clk		*cclk;	/* core clock */
	struct clk		*iclk;	/* interface clock */
	int			irq;
	spinlock_t		lock;

	int			in_fifo_sz;
	int			out_fifo_sz;
	int			in_blk_sz;
	int			out_blk_sz;

	struct spi_transfer	*xfer;
	struct completion	done;
	int			error;
	int			w_size;	/* bytes per SPI word */
	int			n_words;
	int			tx_bytes;
	int			rx_bytes;
	const u8		*tx_buf;
	u8			*rx_buf;
	int			qup_v1;

	int			mode;
	struct dma_slave_config	rx_conf;
	struct dma_slave_config	tx_conf;
};

static int spi_qup_io_config(struct spi_device *spi, struct spi_transfer *xfer);

static inline bool spi_qup_is_flag_set(struct spi_qup *controller, u32 flag)
{
	u32 opflag = readl_relaxed(controller->base + QUP_OPERATIONAL);

	return (opflag & flag) != 0;
}

static inline bool spi_qup_is_dma_xfer(int mode)
{
	if (mode == QUP_IO_M_MODE_DMOV || mode == QUP_IO_M_MODE_BAM)
		return true;

	return false;
}

/* get's the transaction size length */
static inline unsigned int spi_qup_len(struct spi_qup *controller)
{
	return controller->n_words * controller->w_size;
}

static inline bool spi_qup_is_valid_state(struct spi_qup *controller)
{
	u32 opstate = readl_relaxed(controller->base + QUP_STATE);

	return opstate & QUP_STATE_VALID;
}

static int spi_qup_set_state(struct spi_qup *controller, u32 state)
{
	unsigned long loop;
	u32 cur_state;

	loop = 0;
	while (!spi_qup_is_valid_state(controller)) {

		usleep_range(SPI_DELAY_THRESHOLD, SPI_DELAY_THRESHOLD * 2);

		if (++loop > SPI_DELAY_RETRY)
			return -EIO;
	}

	if (loop)
		dev_dbg(controller->dev, "invalid state for %ld,us %d\n",
			loop, state);

	cur_state = readl_relaxed(controller->base + QUP_STATE);
	/*
	 * Per spec: for PAUSE_STATE to RESET_STATE, two writes
	 * of (b10) are required
	 */
	if (((cur_state & QUP_STATE_MASK) == QUP_STATE_PAUSE) &&
	    (state == QUP_STATE_RESET)) {
		writel_relaxed(QUP_STATE_CLEAR, controller->base + QUP_STATE);
		writel_relaxed(QUP_STATE_CLEAR, controller->base + QUP_STATE);
	} else {
		cur_state &= ~QUP_STATE_MASK;
		cur_state |= state;
		writel_relaxed(cur_state, controller->base + QUP_STATE);
	}

	loop = 0;
	while (!spi_qup_is_valid_state(controller)) {

		usleep_range(SPI_DELAY_THRESHOLD, SPI_DELAY_THRESHOLD * 2);

		if (++loop > SPI_DELAY_RETRY)
			return -EIO;
	}

	return 0;
}

static void spi_qup_read_from_fifo(struct spi_qup *controller, u32 num_words)
{
	u8 *rx_buf = controller->rx_buf;
	int i, shift, num_bytes;
	u32 word;

	for (; num_words; num_words--) {

		word = readl_relaxed(controller->base + QUP_INPUT_FIFO);

		num_bytes = min_t(int, spi_qup_len(controller) -
				       controller->rx_bytes,
				       controller->w_size);

		if (!rx_buf) {
			controller->rx_bytes += num_bytes;
			continue;
		}

		for (i = 0; i < num_bytes; i++, controller->rx_bytes++) {
			/*
			 * The data format depends on bytes per SPI word:
			 *  4 bytes: 0x12345678
			 *  2 bytes: 0x00001234
			 *  1 byte : 0x00000012
			 */
			shift = BITS_PER_BYTE;
			shift *= (controller->w_size - i - 1);
			rx_buf[controller->rx_bytes] = word >> shift;
		}
	}
}

static void spi_qup_read(struct spi_qup *controller, u32 *opflags)
{
	u32 remainder, words_per_block, num_words;
	bool is_block_mode = controller->mode == QUP_IO_M_MODE_BLOCK;

	remainder = DIV_ROUND_UP(spi_qup_len(controller) - controller->rx_bytes,
				 controller->w_size);
	words_per_block = controller->in_blk_sz >> 2;

	do {
		/* ACK by clearing service flag */
		writel_relaxed(QUP_OP_IN_SERVICE_FLAG,
			       controller->base + QUP_OPERATIONAL);

		if (!remainder)
			goto exit;

		if (is_block_mode) {
			num_words = (remainder > words_per_block) ?
					words_per_block : remainder;
		} else {
			if (!spi_qup_is_flag_set(controller,
						 QUP_OP_IN_FIFO_NOT_EMPTY))
				break;

			num_words = 1;
		}

		/* read up to the maximum transfer size available */
		spi_qup_read_from_fifo(controller, num_words);

		remainder -= num_words;

		/* if block mode, check to see if next block is available */
		if (is_block_mode && !spi_qup_is_flag_set(controller,
					QUP_OP_IN_BLOCK_READ_REQ))
			break;

	} while (remainder);

	/*
	 * Due to extra stickiness of the QUP_OP_IN_SERVICE_FLAG during block
	 * reads, it has to be cleared again at the very end.  However, be sure
	 * to refresh opflags value because MAX_INPUT_DONE_FLAG may now be
	 * present and this is used to determine if transaction is complete
	 */
exit:
	if (!remainder) {
		*opflags = readl_relaxed(controller->base + QUP_OPERATIONAL);
		if (is_block_mode && *opflags & QUP_OP_MAX_INPUT_DONE_FLAG)
			writel_relaxed(QUP_OP_IN_SERVICE_FLAG,
				       controller->base + QUP_OPERATIONAL);
	}
}

static void spi_qup_write_to_fifo(struct spi_qup *controller, u32 num_words)
{
	const u8 *tx_buf = controller->tx_buf;
	int i, num_bytes;
	u32 word, data;

	for (; num_words; num_words--) {
		word = 0;

		num_bytes = min_t(int, spi_qup_len(controller) -
				       controller->tx_bytes,
				       controller->w_size);
		if (tx_buf)
			for (i = 0; i < num_bytes; i++) {
				data = tx_buf[controller->tx_bytes + i];
				word |= data << (BITS_PER_BYTE * (3 - i));
			}

		controller->tx_bytes += num_bytes;

		writel_relaxed(word, controller->base + QUP_OUTPUT_FIFO);
	}
}

static void spi_qup_dma_done(void *data)
{
	struct spi_qup *qup = data;

	complete(&qup->done);
}

static void spi_qup_write(struct spi_qup *controller)
{
	bool is_block_mode = controller->mode == QUP_IO_M_MODE_BLOCK;
	u32 remainder, words_per_block, num_words;

	remainder = DIV_ROUND_UP(spi_qup_len(controller) - controller->tx_bytes,
				 controller->w_size);
	words_per_block = controller->out_blk_sz >> 2;

	do {
		/* ACK by clearing service flag */
		writel_relaxed(QUP_OP_OUT_SERVICE_FLAG,
			       controller->base + QUP_OPERATIONAL);

		/* make sure the interrupt is valid */
		if (!remainder)
			return;

		if (is_block_mode) {
			num_words = (remainder > words_per_block) ?
				words_per_block : remainder;
		} else {
			if (spi_qup_is_flag_set(controller,
						QUP_OP_OUT_FIFO_FULL))
				break;

			num_words = 1;
		}

		spi_qup_write_to_fifo(controller, num_words);

		remainder -= num_words;

		/* if block mode, check to see if next block is available */
		if (is_block_mode && !spi_qup_is_flag_set(controller,
					QUP_OP_OUT_BLOCK_WRITE_REQ))
			break;

	} while (remainder);
}

static int spi_qup_prep_sg(struct spi_master *master, struct scatterlist *sgl,
			   unsigned int nents, enum dma_transfer_direction dir,
			   dma_async_tx_callback callback)
{
	struct spi_qup *qup = spi_master_get_devdata(master);
	unsigned long flags = DMA_PREP_INTERRUPT | DMA_PREP_FENCE;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *chan;
	dma_cookie_t cookie;

	if (dir == DMA_MEM_TO_DEV)
		chan = master->dma_tx;
	else
		chan = master->dma_rx;

	desc = dmaengine_prep_slave_sg(chan, sgl, nents, dir, flags);
	if (IS_ERR_OR_NULL(desc))
		return desc ? PTR_ERR(desc) : -EINVAL;

	desc->callback = callback;
	desc->callback_param = qup;

	cookie = dmaengine_submit(desc);

	return dma_submit_error(cookie);
}

static void spi_qup_dma_terminate(struct spi_master *master,
				  struct spi_transfer *xfer)
{
	if (xfer->tx_buf)
		dmaengine_terminate_all(master->dma_tx);
	if (xfer->rx_buf)
		dmaengine_terminate_all(master->dma_rx);
}

static u32 spi_qup_sgl_get_nents_len(struct scatterlist *sgl, u32 max,
				     u32 *nents)
{
	struct scatterlist *sg;
	u32 total = 0;

	for (sg = sgl; sg; sg = sg_next(sg)) {
		unsigned int len = sg_dma_len(sg);

		/* check for overflow as well as limit */
		if (((total + len) < total) || ((total + len) > max))
			break;

		total += len;
		(*nents)++;
	}

	return total;
}

static int spi_qup_do_dma(struct spi_device *spi, struct spi_transfer *xfer,
			  unsigned long timeout)
{
	dma_async_tx_callback rx_done = NULL, tx_done = NULL;
	struct spi_master *master = spi->master;
	struct spi_qup *qup = spi_master_get_devdata(master);
	struct scatterlist *tx_sgl, *rx_sgl;
	int ret;

	if (xfer->rx_buf)
		rx_done = spi_qup_dma_done;
	else if (xfer->tx_buf)
		tx_done = spi_qup_dma_done;

	rx_sgl = xfer->rx_sg.sgl;
	tx_sgl = xfer->tx_sg.sgl;

	do {
		u32 rx_nents = 0, tx_nents = 0;

		if (rx_sgl)
			qup->n_words = spi_qup_sgl_get_nents_len(rx_sgl,
					SPI_MAX_XFER, &rx_nents) / qup->w_size;
		if (tx_sgl)
			qup->n_words = spi_qup_sgl_get_nents_len(tx_sgl,
					SPI_MAX_XFER, &tx_nents) / qup->w_size;
		if (!qup->n_words)
			return -EIO;

		ret = spi_qup_io_config(spi, xfer);
		if (ret)
			return ret;

		/* before issuing the descriptors, set the QUP to run */
		ret = spi_qup_set_state(qup, QUP_STATE_RUN);
		if (ret) {
			dev_warn(qup->dev, "cannot set RUN state\n");
			return ret;
		}
		if (rx_sgl) {
			ret = spi_qup_prep_sg(master, rx_sgl, rx_nents,
					      DMA_DEV_TO_MEM, rx_done);
			if (ret)
				return ret;
			dma_async_issue_pending(master->dma_rx);
		}

		if (tx_sgl) {
			ret = spi_qup_prep_sg(master, tx_sgl, tx_nents,
					      DMA_MEM_TO_DEV, tx_done);
			if (ret)
				return ret;

			dma_async_issue_pending(master->dma_tx);
		}

		if (!wait_for_completion_timeout(&qup->done, timeout))
			return -ETIMEDOUT;

		for (; rx_sgl && rx_nents--; rx_sgl = sg_next(rx_sgl))
			;
		for (; tx_sgl && tx_nents--; tx_sgl = sg_next(tx_sgl))
			;

	} while (rx_sgl || tx_sgl);

	return 0;
}

static int spi_qup_do_pio(struct spi_device *spi, struct spi_transfer *xfer,
			  unsigned long timeout)
{
	struct spi_master *master = spi->master;
	struct spi_qup *qup = spi_master_get_devdata(master);
	int ret, n_words, iterations, offset = 0;

	n_words = qup->n_words;
	iterations = n_words / SPI_MAX_XFER; /* round down */
	qup->rx_buf = xfer->rx_buf;
	qup->tx_buf = xfer->tx_buf;

	do {
		if (iterations)
			qup->n_words = SPI_MAX_XFER;
		else
			qup->n_words = n_words % SPI_MAX_XFER;

		if (qup->tx_buf && offset)
			qup->tx_buf = xfer->tx_buf + offset * SPI_MAX_XFER;

		if (qup->rx_buf && offset)
			qup->rx_buf = xfer->rx_buf + offset * SPI_MAX_XFER;

		/*
		 * if the transaction is small enough, we need
		 * to fallback to FIFO mode
		 */
		if (qup->n_words <= (qup->in_fifo_sz / sizeof(u32)))
			qup->mode = QUP_IO_M_MODE_FIFO;

		ret = spi_qup_io_config(spi, xfer);
		if (ret)
			return ret;

		ret = spi_qup_set_state(qup, QUP_STATE_RUN);
		if (ret) {
			dev_warn(qup->dev, "cannot set RUN state\n");
			return ret;
		}

		ret = spi_qup_set_state(qup, QUP_STATE_PAUSE);
		if (ret) {
			dev_warn(qup->dev, "cannot set PAUSE state\n");
			return ret;
		}

		if (qup->mode == QUP_IO_M_MODE_FIFO)
			spi_qup_write(qup);

		ret = spi_qup_set_state(qup, QUP_STATE_RUN);
		if (ret) {
			dev_warn(qup->dev, "cannot set RUN state\n");
			return ret;
		}

		if (!wait_for_completion_timeout(&qup->done, timeout))
			return -ETIMEDOUT;

		offset++;
	} while (iterations--);

	return 0;
}

static bool spi_qup_data_pending(struct spi_qup *controller)
{
	unsigned int remainder_tx, remainder_rx;

	remainder_tx = DIV_ROUND_UP(spi_qup_len(controller) -
				    controller->tx_bytes, controller->w_size);

	remainder_rx = DIV_ROUND_UP(spi_qup_len(controller) -
				    controller->rx_bytes, controller->w_size);

	return remainder_tx || remainder_rx;
}

static irqreturn_t spi_qup_qup_irq(int irq, void *dev_id)
{
	struct spi_qup *controller = dev_id;
	u32 opflags, qup_err, spi_err;
	unsigned long flags;
	int error = 0;

	qup_err = readl_relaxed(controller->base + QUP_ERROR_FLAGS);
	spi_err = readl_relaxed(controller->base + SPI_ERROR_FLAGS);
	opflags = readl_relaxed(controller->base + QUP_OPERATIONAL);

	writel_relaxed(qup_err, controller->base + QUP_ERROR_FLAGS);
	writel_relaxed(spi_err, controller->base + SPI_ERROR_FLAGS);

	if (qup_err) {
		if (qup_err & QUP_ERROR_OUTPUT_OVER_RUN)
			dev_warn(controller->dev, "OUTPUT_OVER_RUN\n");
		if (qup_err & QUP_ERROR_INPUT_UNDER_RUN)
			dev_warn(controller->dev, "INPUT_UNDER_RUN\n");
		if (qup_err & QUP_ERROR_OUTPUT_UNDER_RUN)
			dev_warn(controller->dev, "OUTPUT_UNDER_RUN\n");
		if (qup_err & QUP_ERROR_INPUT_OVER_RUN)
			dev_warn(controller->dev, "INPUT_OVER_RUN\n");

		error = -EIO;
	}

	if (spi_err) {
		if (spi_err & SPI_ERROR_CLK_OVER_RUN)
			dev_warn(controller->dev, "CLK_OVER_RUN\n");
		if (spi_err & SPI_ERROR_CLK_UNDER_RUN)
			dev_warn(controller->dev, "CLK_UNDER_RUN\n");

		error = -EIO;
	}

	spin_lock_irqsave(&controller->lock, flags);
	if (!controller->error)
		controller->error = error;
	spin_unlock_irqrestore(&controller->lock, flags);

	if (spi_qup_is_dma_xfer(controller->mode)) {
		writel_relaxed(opflags, controller->base + QUP_OPERATIONAL);
	} else {
		if (opflags & QUP_OP_IN_SERVICE_FLAG)
			spi_qup_read(controller, &opflags);

		if (opflags & QUP_OP_OUT_SERVICE_FLAG)
			spi_qup_write(controller);

		if (!spi_qup_data_pending(controller))
			complete(&controller->done);
	}

	if (error)
		complete(&controller->done);

	if (opflags & QUP_OP_MAX_INPUT_DONE_FLAG) {
		if (!spi_qup_is_dma_xfer(controller->mode)) {
			if (spi_qup_data_pending(controller))
				return IRQ_HANDLED;
		}
		complete(&controller->done);
	}

	return IRQ_HANDLED;
}

/* set clock freq ... bits per word, determine mode */
static int spi_qup_io_prep(struct spi_device *spi, struct spi_transfer *xfer)
{
	struct spi_qup *controller = spi_master_get_devdata(spi->master);
	int ret;

	if (spi->mode & SPI_LOOP && xfer->len > controller->in_fifo_sz) {
		dev_err(controller->dev, "too big size for loopback %d > %d\n",
			xfer->len, controller->in_fifo_sz);
		return -EIO;
	}

	ret = clk_set_rate(controller->cclk, xfer->speed_hz);
	if (ret) {
		dev_err(controller->dev, "fail to set frequency %d",
			xfer->speed_hz);
		return -EIO;
	}

	controller->w_size = DIV_ROUND_UP(xfer->bits_per_word, 8);
	controller->n_words = xfer->len / controller->w_size;

	if (controller->n_words <= (controller->in_fifo_sz / sizeof(u32)))
		controller->mode = QUP_IO_M_MODE_FIFO;
	else if (spi->master->can_dma &&
		 spi->master->can_dma(spi->master, spi, xfer) &&
		 spi->master->cur_msg_mapped)
		controller->mode = QUP_IO_M_MODE_BAM;
	else
		controller->mode = QUP_IO_M_MODE_BLOCK;

	return 0;
}

/* prep qup for another spi transaction of specific type */
static int spi_qup_io_config(struct spi_device *spi, struct spi_transfer *xfer)
{
	struct spi_qup *controller = spi_master_get_devdata(spi->master);
	u32 config, iomode, control;
	unsigned long flags;

	spin_lock_irqsave(&controller->lock, flags);
	controller->xfer     = xfer;
	controller->error    = 0;
	controller->rx_bytes = 0;
	controller->tx_bytes = 0;
	spin_unlock_irqrestore(&controller->lock, flags);


	if (spi_qup_set_state(controller, QUP_STATE_RESET)) {
		dev_err(controller->dev, "cannot set RESET state\n");
		return -EIO;
	}

	switch (controller->mode) {
	case QUP_IO_M_MODE_FIFO:
		writel_relaxed(controller->n_words,
			       controller->base + QUP_MX_READ_CNT);
		writel_relaxed(controller->n_words,
			       controller->base + QUP_MX_WRITE_CNT);
		/* must be zero for FIFO */
		writel_relaxed(0, controller->base + QUP_MX_INPUT_CNT);
		writel_relaxed(0, controller->base + QUP_MX_OUTPUT_CNT);
		break;
	case QUP_IO_M_MODE_BAM:
		writel_relaxed(controller->n_words,
			       controller->base + QUP_MX_INPUT_CNT);
		writel_relaxed(controller->n_words,
			       controller->base + QUP_MX_OUTPUT_CNT);
		/* must be zero for BLOCK and BAM */
		writel_relaxed(0, controller->base + QUP_MX_READ_CNT);
		writel_relaxed(0, controller->base + QUP_MX_WRITE_CNT);

		if (!controller->qup_v1) {
			void __iomem *input_cnt;

			input_cnt = controller->base + QUP_MX_INPUT_CNT;
			/*
			 * for DMA transfers, both QUP_MX_INPUT_CNT and
			 * QUP_MX_OUTPUT_CNT must be zero to all cases but one.
			 * That case is a non-balanced transfer when there is
			 * only a rx_buf.
			 */
			if (xfer->tx_buf)
				writel_relaxed(0, input_cnt);
			else
				writel_relaxed(controller->n_words, input_cnt);

			writel_relaxed(0, controller->base + QUP_MX_OUTPUT_CNT);
		}
		break;
	case QUP_IO_M_MODE_BLOCK:
		reinit_completion(&controller->done);
		writel_relaxed(controller->n_words,
			       controller->base + QUP_MX_INPUT_CNT);
		writel_relaxed(controller->n_words,
			       controller->base + QUP_MX_OUTPUT_CNT);
		/* must be zero for BLOCK and BAM */
		writel_relaxed(0, controller->base + QUP_MX_READ_CNT);
		writel_relaxed(0, controller->base + QUP_MX_WRITE_CNT);
		break;
	default:
		dev_err(controller->dev, "unknown mode = %d\n",
				controller->mode);
		return -EIO;
	}

	iomode = readl_relaxed(controller->base + QUP_IO_M_MODES);
	/* Set input and output transfer mode */
	iomode &= ~(QUP_IO_M_INPUT_MODE_MASK | QUP_IO_M_OUTPUT_MODE_MASK);

	if (!spi_qup_is_dma_xfer(controller->mode))
		iomode &= ~(QUP_IO_M_PACK_EN | QUP_IO_M_UNPACK_EN);
	else
		iomode |= QUP_IO_M_PACK_EN | QUP_IO_M_UNPACK_EN;

	iomode |= (controller->mode << QUP_IO_M_OUTPUT_MODE_MASK_SHIFT);
	iomode |= (controller->mode << QUP_IO_M_INPUT_MODE_MASK_SHIFT);

	writel_relaxed(iomode, controller->base + QUP_IO_M_MODES);

	control = readl_relaxed(controller->base + SPI_IO_CONTROL);

	if (spi->mode & SPI_CPOL)
		control |= SPI_IO_C_CLK_IDLE_HIGH;
	else
		control &= ~SPI_IO_C_CLK_IDLE_HIGH;

	writel_relaxed(control, controller->base + SPI_IO_CONTROL);

	config = readl_relaxed(controller->base + SPI_CONFIG);

	if (spi->mode & SPI_LOOP)
		config |= SPI_CONFIG_LOOPBACK;
	else
		config &= ~SPI_CONFIG_LOOPBACK;

	if (spi->mode & SPI_CPHA)
		config &= ~SPI_CONFIG_INPUT_FIRST;
	else
		config |= SPI_CONFIG_INPUT_FIRST;

	/*
	 * HS_MODE improves signal stability for spi-clk high rates,
	 * but is invalid in loop back mode.
	 */
	if ((xfer->speed_hz >= SPI_HS_MIN_RATE) && !(spi->mode & SPI_LOOP))
		config |= SPI_CONFIG_HS_MODE;
	else
		config &= ~SPI_CONFIG_HS_MODE;

	writel_relaxed(config, controller->base + SPI_CONFIG);

	config = readl_relaxed(controller->base + QUP_CONFIG);
	config &= ~(QUP_CONFIG_NO_INPUT | QUP_CONFIG_NO_OUTPUT | QUP_CONFIG_N);
	config |= xfer->bits_per_word - 1;
	config |= QUP_CONFIG_SPI_MODE;

	if (spi_qup_is_dma_xfer(controller->mode)) {
		if (!xfer->tx_buf)
			config |= QUP_CONFIG_NO_OUTPUT;
		if (!xfer->rx_buf)
			config |= QUP_CONFIG_NO_INPUT;
	}

	writel_relaxed(config, controller->base + QUP_CONFIG);

	/* only write to OPERATIONAL_MASK when register is present */
	if (!controller->qup_v1) {
		u32 mask = 0;

		/*
		 * mask INPUT and OUTPUT service flags to prevent IRQs on FIFO
		 * status change in BAM mode
		 */

		if (spi_qup_is_dma_xfer(controller->mode))
			mask = QUP_OP_IN_SERVICE_FLAG | QUP_OP_OUT_SERVICE_FLAG;

		writel_relaxed(mask, controller->base + QUP_OPERATIONAL_MASK);
	}

	return 0;
}

static int spi_qup_transfer_one(struct spi_master *master,
			      struct spi_device *spi,
			      struct spi_transfer *xfer)
{
	struct spi_qup *controller = spi_master_get_devdata(master);
	unsigned long timeout, flags;
	int ret = -EIO;

	ret = spi_qup_io_prep(spi, xfer);
	if (ret)
		return ret;

	timeout = DIV_ROUND_UP(xfer->speed_hz, MSEC_PER_SEC);
	timeout = DIV_ROUND_UP(min_t(unsigned long, SPI_MAX_XFER,
				     xfer->len) * 8, timeout);
	timeout = 100 * msecs_to_jiffies(timeout);

	reinit_completion(&controller->done);

	spin_lock_irqsave(&controller->lock, flags);
	controller->xfer     = xfer;
	controller->error    = 0;
	controller->rx_bytes = 0;
	controller->tx_bytes = 0;
	spin_unlock_irqrestore(&controller->lock, flags);

	if (spi_qup_is_dma_xfer(controller->mode))
		ret = spi_qup_do_dma(spi, xfer, timeout);
	else
		ret = spi_qup_do_pio(spi, xfer, timeout);

	if (ret)
		goto exit;

exit:
	spi_qup_set_state(controller, QUP_STATE_RESET);
	spin_lock_irqsave(&controller->lock, flags);
	if (!ret)
		ret = controller->error;
	spin_unlock_irqrestore(&controller->lock, flags);

	if (ret && spi_qup_is_dma_xfer(controller->mode))
		spi_qup_dma_terminate(master, xfer);

	return ret;
}

static bool spi_qup_can_dma(struct spi_master *master, struct spi_device *spi,
			    struct spi_transfer *xfer)
{
	struct spi_qup *qup = spi_master_get_devdata(master);
	size_t dma_align = dma_get_cache_alignment();
	int n_words;

	if (xfer->rx_buf) {
		if (!IS_ALIGNED((size_t)xfer->rx_buf, dma_align) ||
		    IS_ERR_OR_NULL(master->dma_rx))
			return false;
		if (qup->qup_v1 && (xfer->len % qup->in_blk_sz))
			return false;
	}

	if (xfer->tx_buf) {
		if (!IS_ALIGNED((size_t)xfer->tx_buf, dma_align) ||
		    IS_ERR_OR_NULL(master->dma_tx))
			return false;
		if (qup->qup_v1 && (xfer->len % qup->out_blk_sz))
			return false;
	}

	n_words = xfer->len / DIV_ROUND_UP(xfer->bits_per_word, 8);
	if (n_words <= (qup->in_fifo_sz / sizeof(u32)))
		return false;

	return true;
}

static void spi_qup_release_dma(struct spi_master *master)
{
	if (!IS_ERR_OR_NULL(master->dma_rx))
		dma_release_channel(master->dma_rx);
	if (!IS_ERR_OR_NULL(master->dma_tx))
		dma_release_channel(master->dma_tx);
}

static int spi_qup_init_dma(struct spi_master *master, resource_size_t base)
{
	struct spi_qup *spi = spi_master_get_devdata(master);
	struct dma_slave_config *rx_conf = &spi->rx_conf,
				*tx_conf = &spi->tx_conf;
	struct device *dev = spi->dev;
	int ret;

	/* allocate dma resources, if available */
	master->dma_rx = dma_request_slave_channel_reason(dev, "rx");
	if (IS_ERR(master->dma_rx))
		return PTR_ERR(master->dma_rx);

	master->dma_tx = dma_request_slave_channel_reason(dev, "tx");
	if (IS_ERR(master->dma_tx)) {
		ret = PTR_ERR(master->dma_tx);
		goto err_tx;
	}

	/* set DMA parameters */
	rx_conf->direction = DMA_DEV_TO_MEM;
	rx_conf->device_fc = 1;
	rx_conf->src_addr = base + QUP_INPUT_FIFO;
	rx_conf->src_maxburst = spi->in_blk_sz;

	tx_conf->direction = DMA_MEM_TO_DEV;
	tx_conf->device_fc = 1;
	tx_conf->dst_addr = base + QUP_OUTPUT_FIFO;
	tx_conf->dst_maxburst = spi->out_blk_sz;

	ret = dmaengine_slave_config(master->dma_rx, rx_conf);
	if (ret) {
		dev_err(dev, "failed to configure RX channel\n");
		goto err;
	}

	ret = dmaengine_slave_config(master->dma_tx, tx_conf);
	if (ret) {
		dev_err(dev, "failed to configure TX channel\n");
		goto err;
	}

	return 0;

err:
	dma_release_channel(master->dma_tx);
err_tx:
	dma_release_channel(master->dma_rx);
	return ret;
}

static void spi_qup_set_cs(struct spi_device *spi, bool val)
{
	struct spi_qup *controller;
	u32 spi_ioc;
	u32 spi_ioc_orig;

	controller = spi_master_get_devdata(spi->master);
	spi_ioc = readl_relaxed(controller->base + SPI_IO_CONTROL);
	spi_ioc_orig = spi_ioc;
	if (!val)
		spi_ioc |= SPI_IO_C_FORCE_CS;
	else
		spi_ioc &= ~SPI_IO_C_FORCE_CS;

	if (spi_ioc != spi_ioc_orig)
		writel_relaxed(spi_ioc, controller->base + SPI_IO_CONTROL);
}

static int spi_qup_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct clk *iclk, *cclk;
	struct spi_qup *controller;
	struct resource *res;
	struct device *dev;
	void __iomem *base;
	u32 max_freq, iomode, num_cs;
	int ret, irq, size;

	dev = &pdev->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	cclk = devm_clk_get(dev, "core");
	if (IS_ERR(cclk))
		return PTR_ERR(cclk);

	iclk = devm_clk_get(dev, "iface");
	if (IS_ERR(iclk))
		return PTR_ERR(iclk);

	/* This is optional parameter */
	if (of_property_read_u32(dev->of_node, "spi-max-frequency", &max_freq))
		max_freq = SPI_MAX_RATE;

	if (!max_freq || max_freq > SPI_MAX_RATE) {
		dev_err(dev, "invalid clock frequency %d\n", max_freq);
		return -ENXIO;
	}

	ret = clk_prepare_enable(cclk);
	if (ret) {
		dev_err(dev, "cannot enable core clock\n");
		return ret;
	}

	ret = clk_prepare_enable(iclk);
	if (ret) {
		clk_disable_unprepare(cclk);
		dev_err(dev, "cannot enable iface clock\n");
		return ret;
	}

	master = spi_alloc_master(dev, sizeof(struct spi_qup));
	if (!master) {
		clk_disable_unprepare(cclk);
		clk_disable_unprepare(iclk);
		dev_err(dev, "cannot allocate master\n");
		return -ENOMEM;
	}

	/* use num-cs unless not present or out of range */
	if (of_property_read_u32(dev->of_node, "num-cs", &num_cs) ||
	    num_cs > SPI_NUM_CHIPSELECTS)
		master->num_chipselect = SPI_NUM_CHIPSELECTS;
	else
		master->num_chipselect = num_cs;

	master->bus_num = pdev->id;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LOOP;
	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
	master->max_speed_hz = max_freq;
	master->transfer_one = spi_qup_transfer_one;
	master->dev.of_node = pdev->dev.of_node;
	master->auto_runtime_pm = true;
	master->dma_alignment = dma_get_cache_alignment();
	master->max_dma_len = SPI_MAX_XFER;

	platform_set_drvdata(pdev, master);

	controller = spi_master_get_devdata(master);

	controller->dev = dev;
	controller->base = base;
	controller->iclk = iclk;
	controller->cclk = cclk;
	controller->irq = irq;

	ret = spi_qup_init_dma(master, res->start);
	if (ret == -EPROBE_DEFER)
		goto error;
	else if (!ret)
		master->can_dma = spi_qup_can_dma;

	controller->qup_v1 = (uintptr_t)of_device_get_match_data(dev);

	if (!controller->qup_v1)
		master->set_cs = spi_qup_set_cs;

	spin_lock_init(&controller->lock);
	init_completion(&controller->done);

	iomode = readl_relaxed(base + QUP_IO_M_MODES);

	size = QUP_IO_M_OUTPUT_BLOCK_SIZE(iomode);
	if (size)
		controller->out_blk_sz = size * 16;
	else
		controller->out_blk_sz = 4;

	size = QUP_IO_M_INPUT_BLOCK_SIZE(iomode);
	if (size)
		controller->in_blk_sz = size * 16;
	else
		controller->in_blk_sz = 4;

	size = QUP_IO_M_OUTPUT_FIFO_SIZE(iomode);
	controller->out_fifo_sz = controller->out_blk_sz * (2 << size);

	size = QUP_IO_M_INPUT_FIFO_SIZE(iomode);
	controller->in_fifo_sz = controller->in_blk_sz * (2 << size);

	dev_info(dev, "IN:block:%d, fifo:%d, OUT:block:%d, fifo:%d\n",
		 controller->in_blk_sz, controller->in_fifo_sz,
		 controller->out_blk_sz, controller->out_fifo_sz);

	writel_relaxed(1, base + QUP_SW_RESET);

	ret = spi_qup_set_state(controller, QUP_STATE_RESET);
	if (ret) {
		dev_err(dev, "cannot set RESET state\n");
		goto error_dma;
	}

	writel_relaxed(0, base + QUP_OPERATIONAL);
	writel_relaxed(0, base + QUP_IO_M_MODES);

	if (!controller->qup_v1)
		writel_relaxed(0, base + QUP_OPERATIONAL_MASK);

	writel_relaxed(SPI_ERROR_CLK_UNDER_RUN | SPI_ERROR_CLK_OVER_RUN,
		       base + SPI_ERROR_FLAGS_EN);

	/* if earlier version of the QUP, disable INPUT_OVERRUN */
	if (controller->qup_v1)
		writel_relaxed(QUP_ERROR_OUTPUT_OVER_RUN |
			QUP_ERROR_INPUT_UNDER_RUN | QUP_ERROR_OUTPUT_UNDER_RUN,
			base + QUP_ERROR_FLAGS_EN);

	writel_relaxed(0, base + SPI_CONFIG);
	writel_relaxed(SPI_IO_C_NO_TRI_STATE, base + SPI_IO_CONTROL);

	ret = devm_request_irq(dev, irq, spi_qup_qup_irq,
			       IRQF_TRIGGER_HIGH, pdev->name, controller);
	if (ret)
		goto error_dma;

	pm_runtime_set_autosuspend_delay(dev, MSEC_PER_SEC);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = devm_spi_register_master(dev, master);
	if (ret)
		goto disable_pm;

	return 0;

disable_pm:
	pm_runtime_disable(&pdev->dev);
error_dma:
	spi_qup_release_dma(master);
error:
	clk_disable_unprepare(cclk);
	clk_disable_unprepare(iclk);
	spi_master_put(master);
	return ret;
}

#ifdef CONFIG_PM
static int spi_qup_pm_suspend_runtime(struct device *device)
{
	struct spi_master *master = dev_get_drvdata(device);
	struct spi_qup *controller = spi_master_get_devdata(master);
	u32 config;

	/* Enable clocks auto gaiting */
	config = readl(controller->base + QUP_CONFIG);
	config |= QUP_CONFIG_CLOCK_AUTO_GATE;
	writel_relaxed(config, controller->base + QUP_CONFIG);

	clk_disable_unprepare(controller->cclk);
	clk_disable_unprepare(controller->iclk);

	return 0;
}

static int spi_qup_pm_resume_runtime(struct device *device)
{
	struct spi_master *master = dev_get_drvdata(device);
	struct spi_qup *controller = spi_master_get_devdata(master);
	u32 config;
	int ret;

	ret = clk_prepare_enable(controller->iclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(controller->cclk);
	if (ret)
		return ret;

	/* Disable clocks auto gaiting */
	config = readl_relaxed(controller->base + QUP_CONFIG);
	config &= ~QUP_CONFIG_CLOCK_AUTO_GATE;
	writel_relaxed(config, controller->base + QUP_CONFIG);
	return 0;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
static int spi_qup_suspend(struct device *device)
{
	struct spi_master *master = dev_get_drvdata(device);
	struct spi_qup *controller = spi_master_get_devdata(master);
	int ret;

	ret = spi_master_suspend(master);
	if (ret)
		return ret;

	ret = spi_qup_set_state(controller, QUP_STATE_RESET);
	if (ret)
		return ret;

	if (!pm_runtime_suspended(device)) {
		clk_disable_unprepare(controller->cclk);
		clk_disable_unprepare(controller->iclk);
	}
	return 0;
}

static int spi_qup_resume(struct device *device)
{
	struct spi_master *master = dev_get_drvdata(device);
	struct spi_qup *controller = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(controller->iclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(controller->cclk);
	if (ret)
		return ret;

	ret = spi_qup_set_state(controller, QUP_STATE_RESET);
	if (ret)
		return ret;

	return spi_master_resume(master);
}
#endif /* CONFIG_PM_SLEEP */

static int spi_qup_remove(struct platform_device *pdev)
{
	struct spi_master *master = dev_get_drvdata(&pdev->dev);
	struct spi_qup *controller = spi_master_get_devdata(master);
	int ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		return ret;

	ret = spi_qup_set_state(controller, QUP_STATE_RESET);
	if (ret)
		return ret;

	spi_qup_release_dma(master);

	clk_disable_unprepare(controller->cclk);
	clk_disable_unprepare(controller->iclk);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id spi_qup_dt_match[] = {
	{ .compatible = "qcom,spi-qup-v1.1.1", .data = (void *)1, },
	{ .compatible = "qcom,spi-qup-v2.1.1", },
	{ .compatible = "qcom,spi-qup-v2.2.1", },
	{ }
};
MODULE_DEVICE_TABLE(of, spi_qup_dt_match);

static const struct dev_pm_ops spi_qup_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(spi_qup_suspend, spi_qup_resume)
	SET_RUNTIME_PM_OPS(spi_qup_pm_suspend_runtime,
			   spi_qup_pm_resume_runtime,
			   NULL)
};

static struct platform_driver spi_qup_driver = {
	.driver = {
		.name		= "spi_qup",
		.pm		= &spi_qup_dev_pm_ops,
		.of_match_table = spi_qup_dt_match,
	},
	.probe = spi_qup_probe,
	.remove = spi_qup_remove,
};
module_platform_driver(spi_qup_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:spi_qup");
