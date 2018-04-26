/*
 * TI QSPI driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 * Author: Sourav Poddar <sourav.poddar@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GPLv2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR /PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/omap-dma.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/sizes.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

struct ti_qspi_regs {
	u32 clkctrl;
};

struct ti_qspi {
	struct completion	transfer_complete;

	/* list synchronization */
	struct mutex            list_lock;

	struct spi_master	*master;
	void __iomem            *base;
	void __iomem            *mmap_base;
	size_t			mmap_size;
	struct regmap		*ctrl_base;
	unsigned int		ctrl_reg;
	struct clk		*fclk;
	struct device           *dev;

	struct ti_qspi_regs     ctx_reg;

	dma_addr_t		mmap_phys_base;
	dma_addr_t		rx_bb_dma_addr;
	void			*rx_bb_addr;
	struct dma_chan		*rx_chan;

	u32 spi_max_frequency;
	u32 cmd;
	u32 dc;

	bool mmap_enabled;
};

#define QSPI_PID			(0x0)
#define QSPI_SYSCONFIG			(0x10)
#define QSPI_SPI_CLOCK_CNTRL_REG	(0x40)
#define QSPI_SPI_DC_REG			(0x44)
#define QSPI_SPI_CMD_REG		(0x48)
#define QSPI_SPI_STATUS_REG		(0x4c)
#define QSPI_SPI_DATA_REG		(0x50)
#define QSPI_SPI_SETUP_REG(n)		((0x54 + 4 * n))
#define QSPI_SPI_SWITCH_REG		(0x64)
#define QSPI_SPI_DATA_REG_1		(0x68)
#define QSPI_SPI_DATA_REG_2		(0x6c)
#define QSPI_SPI_DATA_REG_3		(0x70)

#define QSPI_COMPLETION_TIMEOUT		msecs_to_jiffies(2000)

#define QSPI_FCLK			192000000

/* Clock Control */
#define QSPI_CLK_EN			(1 << 31)
#define QSPI_CLK_DIV_MAX		0xffff

/* Command */
#define QSPI_EN_CS(n)			(n << 28)
#define QSPI_WLEN(n)			((n - 1) << 19)
#define QSPI_3_PIN			(1 << 18)
#define QSPI_RD_SNGL			(1 << 16)
#define QSPI_WR_SNGL			(2 << 16)
#define QSPI_RD_DUAL			(3 << 16)
#define QSPI_RD_QUAD			(7 << 16)
#define QSPI_INVAL			(4 << 16)
#define QSPI_FLEN(n)			((n - 1) << 0)
#define QSPI_WLEN_MAX_BITS		128
#define QSPI_WLEN_MAX_BYTES		16
#define QSPI_WLEN_MASK			QSPI_WLEN(QSPI_WLEN_MAX_BITS)

/* STATUS REGISTER */
#define BUSY				0x01
#define WC				0x02

/* Device Control */
#define QSPI_DD(m, n)			(m << (3 + n * 8))
#define QSPI_CKPHA(n)			(1 << (2 + n * 8))
#define QSPI_CSPOL(n)			(1 << (1 + n * 8))
#define QSPI_CKPOL(n)			(1 << (n * 8))

#define	QSPI_FRAME			4096

#define QSPI_AUTOSUSPEND_TIMEOUT         2000

#define MEM_CS_EN(n)			((n + 1) << 8)
#define MEM_CS_MASK			(7 << 8)

#define MM_SWITCH			0x1

#define QSPI_SETUP_RD_NORMAL		(0x0 << 12)
#define QSPI_SETUP_RD_DUAL		(0x1 << 12)
#define QSPI_SETUP_RD_QUAD		(0x3 << 12)
#define QSPI_SETUP_ADDR_SHIFT		8
#define QSPI_SETUP_DUMMY_SHIFT		10

#define QSPI_DMA_BUFFER_SIZE            SZ_64K

static inline unsigned long ti_qspi_read(struct ti_qspi *qspi,
		unsigned long reg)
{
	return readl(qspi->base + reg);
}

static inline void ti_qspi_write(struct ti_qspi *qspi,
		unsigned long val, unsigned long reg)
{
	writel(val, qspi->base + reg);
}

static int ti_qspi_setup(struct spi_device *spi)
{
	struct ti_qspi	*qspi = spi_master_get_devdata(spi->master);
	struct ti_qspi_regs *ctx_reg = &qspi->ctx_reg;
	int clk_div = 0, ret;
	u32 clk_ctrl_reg, clk_rate, clk_mask;

	if (spi->master->busy) {
		dev_dbg(qspi->dev, "master busy doing other transfers\n");
		return -EBUSY;
	}

	if (!qspi->spi_max_frequency) {
		dev_err(qspi->dev, "spi max frequency not defined\n");
		return -EINVAL;
	}

	clk_rate = clk_get_rate(qspi->fclk);

	clk_div = DIV_ROUND_UP(clk_rate, qspi->spi_max_frequency) - 1;

	if (clk_div < 0) {
		dev_dbg(qspi->dev, "clock divider < 0, using /1 divider\n");
		return -EINVAL;
	}

	if (clk_div > QSPI_CLK_DIV_MAX) {
		dev_dbg(qspi->dev, "clock divider >%d , using /%d divider\n",
				QSPI_CLK_DIV_MAX, QSPI_CLK_DIV_MAX + 1);
		return -EINVAL;
	}

	dev_dbg(qspi->dev, "hz: %d, clock divider %d\n",
			qspi->spi_max_frequency, clk_div);

	ret = pm_runtime_get_sync(qspi->dev);
	if (ret < 0) {
		dev_err(qspi->dev, "pm_runtime_get_sync() failed\n");
		return ret;
	}

	clk_ctrl_reg = ti_qspi_read(qspi, QSPI_SPI_CLOCK_CNTRL_REG);

	clk_ctrl_reg &= ~QSPI_CLK_EN;

	/* disable SCLK */
	ti_qspi_write(qspi, clk_ctrl_reg, QSPI_SPI_CLOCK_CNTRL_REG);

	/* enable SCLK */
	clk_mask = QSPI_CLK_EN | clk_div;
	ti_qspi_write(qspi, clk_mask, QSPI_SPI_CLOCK_CNTRL_REG);
	ctx_reg->clkctrl = clk_mask;

	pm_runtime_mark_last_busy(qspi->dev);
	ret = pm_runtime_put_autosuspend(qspi->dev);
	if (ret < 0) {
		dev_err(qspi->dev, "pm_runtime_put_autosuspend() failed\n");
		return ret;
	}

	return 0;
}

static void ti_qspi_restore_ctx(struct ti_qspi *qspi)
{
	struct ti_qspi_regs *ctx_reg = &qspi->ctx_reg;

	ti_qspi_write(qspi, ctx_reg->clkctrl, QSPI_SPI_CLOCK_CNTRL_REG);
}

static inline u32 qspi_is_busy(struct ti_qspi *qspi)
{
	u32 stat;
	unsigned long timeout = jiffies + QSPI_COMPLETION_TIMEOUT;

	stat = ti_qspi_read(qspi, QSPI_SPI_STATUS_REG);
	while ((stat & BUSY) && time_after(timeout, jiffies)) {
		cpu_relax();
		stat = ti_qspi_read(qspi, QSPI_SPI_STATUS_REG);
	}

	WARN(stat & BUSY, "qspi busy\n");
	return stat & BUSY;
}

static inline int ti_qspi_poll_wc(struct ti_qspi *qspi)
{
	u32 stat;
	unsigned long timeout = jiffies + QSPI_COMPLETION_TIMEOUT;

	do {
		stat = ti_qspi_read(qspi, QSPI_SPI_STATUS_REG);
		if (stat & WC)
			return 0;
		cpu_relax();
	} while (time_after(timeout, jiffies));

	stat = ti_qspi_read(qspi, QSPI_SPI_STATUS_REG);
	if (stat & WC)
		return 0;
	return  -ETIMEDOUT;
}

static int qspi_write_msg(struct ti_qspi *qspi, struct spi_transfer *t,
			  int count)
{
	int wlen, xfer_len;
	unsigned int cmd;
	const u8 *txbuf;
	u32 data;

	txbuf = t->tx_buf;
	cmd = qspi->cmd | QSPI_WR_SNGL;
	wlen = t->bits_per_word >> 3;	/* in bytes */
	xfer_len = wlen;

	while (count) {
		if (qspi_is_busy(qspi))
			return -EBUSY;

		switch (wlen) {
		case 1:
			dev_dbg(qspi->dev, "tx cmd %08x dc %08x data %02x\n",
					cmd, qspi->dc, *txbuf);
			if (count >= QSPI_WLEN_MAX_BYTES) {
				u32 *txp = (u32 *)txbuf;

				data = cpu_to_be32(*txp++);
				writel(data, qspi->base +
				       QSPI_SPI_DATA_REG_3);
				data = cpu_to_be32(*txp++);
				writel(data, qspi->base +
				       QSPI_SPI_DATA_REG_2);
				data = cpu_to_be32(*txp++);
				writel(data, qspi->base +
				       QSPI_SPI_DATA_REG_1);
				data = cpu_to_be32(*txp++);
				writel(data, qspi->base +
				       QSPI_SPI_DATA_REG);
				xfer_len = QSPI_WLEN_MAX_BYTES;
				cmd |= QSPI_WLEN(QSPI_WLEN_MAX_BITS);
			} else {
				writeb(*txbuf, qspi->base + QSPI_SPI_DATA_REG);
				cmd = qspi->cmd | QSPI_WR_SNGL;
				xfer_len = wlen;
				cmd |= QSPI_WLEN(wlen);
			}
			break;
		case 2:
			dev_dbg(qspi->dev, "tx cmd %08x dc %08x data %04x\n",
					cmd, qspi->dc, *txbuf);
			writew(*((u16 *)txbuf), qspi->base + QSPI_SPI_DATA_REG);
			break;
		case 4:
			dev_dbg(qspi->dev, "tx cmd %08x dc %08x data %08x\n",
					cmd, qspi->dc, *txbuf);
			writel(*((u32 *)txbuf), qspi->base + QSPI_SPI_DATA_REG);
			break;
		}

		ti_qspi_write(qspi, cmd, QSPI_SPI_CMD_REG);
		if (ti_qspi_poll_wc(qspi)) {
			dev_err(qspi->dev, "write timed out\n");
			return -ETIMEDOUT;
		}
		txbuf += xfer_len;
		count -= xfer_len;
	}

	return 0;
}

static int qspi_read_msg(struct ti_qspi *qspi, struct spi_transfer *t,
			 int count)
{
	int wlen;
	unsigned int cmd;
	u8 *rxbuf;

	rxbuf = t->rx_buf;
	cmd = qspi->cmd;
	switch (t->rx_nbits) {
	case SPI_NBITS_DUAL:
		cmd |= QSPI_RD_DUAL;
		break;
	case SPI_NBITS_QUAD:
		cmd |= QSPI_RD_QUAD;
		break;
	default:
		cmd |= QSPI_RD_SNGL;
		break;
	}
	wlen = t->bits_per_word >> 3;	/* in bytes */

	while (count) {
		dev_dbg(qspi->dev, "rx cmd %08x dc %08x\n", cmd, qspi->dc);
		if (qspi_is_busy(qspi))
			return -EBUSY;

		ti_qspi_write(qspi, cmd, QSPI_SPI_CMD_REG);
		if (ti_qspi_poll_wc(qspi)) {
			dev_err(qspi->dev, "read timed out\n");
			return -ETIMEDOUT;
		}
		switch (wlen) {
		case 1:
			*rxbuf = readb(qspi->base + QSPI_SPI_DATA_REG);
			break;
		case 2:
			*((u16 *)rxbuf) = readw(qspi->base + QSPI_SPI_DATA_REG);
			break;
		case 4:
			*((u32 *)rxbuf) = readl(qspi->base + QSPI_SPI_DATA_REG);
			break;
		}
		rxbuf += wlen;
		count -= wlen;
	}

	return 0;
}

static int qspi_transfer_msg(struct ti_qspi *qspi, struct spi_transfer *t,
			     int count)
{
	int ret;

	if (t->tx_buf) {
		ret = qspi_write_msg(qspi, t, count);
		if (ret) {
			dev_dbg(qspi->dev, "Error while writing\n");
			return ret;
		}
	}

	if (t->rx_buf) {
		ret = qspi_read_msg(qspi, t, count);
		if (ret) {
			dev_dbg(qspi->dev, "Error while reading\n");
			return ret;
		}
	}

	return 0;
}

static void ti_qspi_dma_callback(void *param)
{
	struct ti_qspi *qspi = param;

	complete(&qspi->transfer_complete);
}

static int ti_qspi_dma_xfer(struct ti_qspi *qspi, dma_addr_t dma_dst,
			    dma_addr_t dma_src, size_t len)
{
	struct dma_chan *chan = qspi->rx_chan;
	dma_cookie_t cookie;
	enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_async_tx_descriptor *tx;
	int ret;

	tx = dmaengine_prep_dma_memcpy(chan, dma_dst, dma_src, len, flags);
	if (!tx) {
		dev_err(qspi->dev, "device_prep_dma_memcpy error\n");
		return -EIO;
	}

	tx->callback = ti_qspi_dma_callback;
	tx->callback_param = qspi;
	cookie = tx->tx_submit(tx);
	reinit_completion(&qspi->transfer_complete);

	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(qspi->dev, "dma_submit_error %d\n", cookie);
		return -EIO;
	}

	dma_async_issue_pending(chan);
	ret = wait_for_completion_timeout(&qspi->transfer_complete,
					  msecs_to_jiffies(len));
	if (ret <= 0) {
		dmaengine_terminate_sync(chan);
		dev_err(qspi->dev, "DMA wait_for_completion_timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int ti_qspi_dma_bounce_buffer(struct ti_qspi *qspi, loff_t offs,
				     void *to, size_t readsize)
{
	dma_addr_t dma_src = qspi->mmap_phys_base + offs;
	int ret = 0;

	/*
	 * Use bounce buffer as FS like jffs2, ubifs may pass
	 * buffers that does not belong to kernel lowmem region.
	 */
	while (readsize != 0) {
		size_t xfer_len = min_t(size_t, QSPI_DMA_BUFFER_SIZE,
					readsize);

		ret = ti_qspi_dma_xfer(qspi, qspi->rx_bb_dma_addr,
				       dma_src, xfer_len);
		if (ret != 0)
			return ret;
		memcpy(to, qspi->rx_bb_addr, xfer_len);
		readsize -= xfer_len;
		dma_src += xfer_len;
		to += xfer_len;
	}

	return ret;
}

static int ti_qspi_dma_xfer_sg(struct ti_qspi *qspi, struct sg_table rx_sg,
			       loff_t from)
{
	struct scatterlist *sg;
	dma_addr_t dma_src = qspi->mmap_phys_base + from;
	dma_addr_t dma_dst;
	int i, len, ret;

	for_each_sg(rx_sg.sgl, sg, rx_sg.nents, i) {
		dma_dst = sg_dma_address(sg);
		len = sg_dma_len(sg);
		ret = ti_qspi_dma_xfer(qspi, dma_dst, dma_src, len);
		if (ret)
			return ret;
		dma_src += len;
	}

	return 0;
}

static void ti_qspi_enable_memory_map(struct spi_device *spi)
{
	struct ti_qspi  *qspi = spi_master_get_devdata(spi->master);

	ti_qspi_write(qspi, MM_SWITCH, QSPI_SPI_SWITCH_REG);
	if (qspi->ctrl_base) {
		regmap_update_bits(qspi->ctrl_base, qspi->ctrl_reg,
				   MEM_CS_EN(spi->chip_select),
				   MEM_CS_MASK);
	}
	qspi->mmap_enabled = true;
}

static void ti_qspi_disable_memory_map(struct spi_device *spi)
{
	struct ti_qspi  *qspi = spi_master_get_devdata(spi->master);

	ti_qspi_write(qspi, 0, QSPI_SPI_SWITCH_REG);
	if (qspi->ctrl_base)
		regmap_update_bits(qspi->ctrl_base, qspi->ctrl_reg,
				   0, MEM_CS_MASK);
	qspi->mmap_enabled = false;
}

static void ti_qspi_setup_mmap_read(struct spi_device *spi, u8 opcode,
				    u8 data_nbits, u8 addr_width,
				    u8 dummy_bytes)
{
	struct ti_qspi  *qspi = spi_master_get_devdata(spi->master);
	u32 memval = opcode;

	switch (data_nbits) {
	case SPI_NBITS_QUAD:
		memval |= QSPI_SETUP_RD_QUAD;
		break;
	case SPI_NBITS_DUAL:
		memval |= QSPI_SETUP_RD_DUAL;
		break;
	default:
		memval |= QSPI_SETUP_RD_NORMAL;
		break;
	}
	memval |= ((addr_width - 1) << QSPI_SETUP_ADDR_SHIFT |
		   dummy_bytes << QSPI_SETUP_DUMMY_SHIFT);
	ti_qspi_write(qspi, memval,
		      QSPI_SPI_SETUP_REG(spi->chip_select));
}

static bool ti_qspi_spi_flash_can_dma(struct spi_device *spi,
				      struct spi_flash_read_message *msg)
{
	return virt_addr_valid(msg->buf);
}

static int ti_qspi_spi_flash_read(struct spi_device *spi,
				  struct spi_flash_read_message *msg)
{
	struct ti_qspi *qspi = spi_master_get_devdata(spi->master);
	int ret = 0;

	mutex_lock(&qspi->list_lock);

	if (!qspi->mmap_enabled)
		ti_qspi_enable_memory_map(spi);
	ti_qspi_setup_mmap_read(spi, msg->read_opcode, msg->data_nbits,
				msg->addr_width, msg->dummy_bytes);

	if (qspi->rx_chan) {
		if (msg->cur_msg_mapped)
			ret = ti_qspi_dma_xfer_sg(qspi, msg->rx_sg, msg->from);
		else
			ret = ti_qspi_dma_bounce_buffer(qspi, msg->from,
							msg->buf, msg->len);
		if (ret)
			goto err_unlock;
	} else {
		memcpy_fromio(msg->buf, qspi->mmap_base + msg->from, msg->len);
	}
	msg->retlen = msg->len;

err_unlock:
	mutex_unlock(&qspi->list_lock);

	return ret;
}

static int ti_qspi_exec_mem_op(struct spi_mem *mem,
			       const struct spi_mem_op *op)
{
	struct ti_qspi *qspi = spi_master_get_devdata(mem->spi->master);
	u32 from = 0;
	int ret = 0;

	/* Only optimize read path. */
	if (!op->data.nbytes || op->data.dir != SPI_MEM_DATA_IN ||
	    !op->addr.nbytes || op->addr.nbytes > 4)
		return -ENOTSUPP;

	/* Address exceeds MMIO window size, fall back to regular mode. */
	from = op->addr.val;
	if (from + op->data.nbytes > qspi->mmap_size)
		return -ENOTSUPP;

	mutex_lock(&qspi->list_lock);

	if (!qspi->mmap_enabled)
		ti_qspi_enable_memory_map(mem->spi);
	ti_qspi_setup_mmap_read(mem->spi, op->cmd.opcode, op->data.buswidth,
				op->addr.nbytes, op->dummy.nbytes);

	if (qspi->rx_chan) {
		struct sg_table sgt;

		if (virt_addr_valid(op->data.buf.in) &&
		    !spi_controller_dma_map_mem_op_data(mem->spi->master, op,
							&sgt)) {
			ret = ti_qspi_dma_xfer_sg(qspi, sgt, from);
			spi_controller_dma_unmap_mem_op_data(mem->spi->master,
							     op, &sgt);
		} else {
			ret = ti_qspi_dma_bounce_buffer(qspi, from,
							op->data.buf.in,
							op->data.nbytes);
		}
	} else {
		memcpy_fromio(op->data.buf.in, qspi->mmap_base + from,
			      op->data.nbytes);
	}

	mutex_unlock(&qspi->list_lock);

	return ret;
}

static const struct spi_controller_mem_ops ti_qspi_mem_ops = {
	.exec_op = ti_qspi_exec_mem_op,
};

static int ti_qspi_start_transfer_one(struct spi_master *master,
		struct spi_message *m)
{
	struct ti_qspi *qspi = spi_master_get_devdata(master);
	struct spi_device *spi = m->spi;
	struct spi_transfer *t;
	int status = 0, ret;
	unsigned int frame_len_words, transfer_len_words;
	int wlen;

	/* setup device control reg */
	qspi->dc = 0;

	if (spi->mode & SPI_CPHA)
		qspi->dc |= QSPI_CKPHA(spi->chip_select);
	if (spi->mode & SPI_CPOL)
		qspi->dc |= QSPI_CKPOL(spi->chip_select);
	if (spi->mode & SPI_CS_HIGH)
		qspi->dc |= QSPI_CSPOL(spi->chip_select);

	frame_len_words = 0;
	list_for_each_entry(t, &m->transfers, transfer_list)
		frame_len_words += t->len / (t->bits_per_word >> 3);
	frame_len_words = min_t(unsigned int, frame_len_words, QSPI_FRAME);

	/* setup command reg */
	qspi->cmd = 0;
	qspi->cmd |= QSPI_EN_CS(spi->chip_select);
	qspi->cmd |= QSPI_FLEN(frame_len_words);

	ti_qspi_write(qspi, qspi->dc, QSPI_SPI_DC_REG);

	mutex_lock(&qspi->list_lock);

	if (qspi->mmap_enabled)
		ti_qspi_disable_memory_map(spi);

	list_for_each_entry(t, &m->transfers, transfer_list) {
		qspi->cmd = ((qspi->cmd & ~QSPI_WLEN_MASK) |
			     QSPI_WLEN(t->bits_per_word));

		wlen = t->bits_per_word >> 3;
		transfer_len_words = min(t->len / wlen, frame_len_words);

		ret = qspi_transfer_msg(qspi, t, transfer_len_words * wlen);
		if (ret) {
			dev_dbg(qspi->dev, "transfer message failed\n");
			mutex_unlock(&qspi->list_lock);
			return -EINVAL;
		}

		m->actual_length += transfer_len_words * wlen;
		frame_len_words -= transfer_len_words;
		if (frame_len_words == 0)
			break;
	}

	mutex_unlock(&qspi->list_lock);

	ti_qspi_write(qspi, qspi->cmd | QSPI_INVAL, QSPI_SPI_CMD_REG);
	m->status = status;
	spi_finalize_current_message(master);

	return status;
}

static int ti_qspi_runtime_resume(struct device *dev)
{
	struct ti_qspi      *qspi;

	qspi = dev_get_drvdata(dev);
	ti_qspi_restore_ctx(qspi);

	return 0;
}

static const struct of_device_id ti_qspi_match[] = {
	{.compatible = "ti,dra7xxx-qspi" },
	{.compatible = "ti,am4372-qspi" },
	{},
};
MODULE_DEVICE_TABLE(of, ti_qspi_match);

static int ti_qspi_probe(struct platform_device *pdev)
{
	struct  ti_qspi *qspi;
	struct spi_master *master;
	struct resource         *r, *res_mmap;
	struct device_node *np = pdev->dev.of_node;
	u32 max_freq;
	int ret = 0, num_cs, irq;
	dma_cap_mask_t mask;

	master = spi_alloc_master(&pdev->dev, sizeof(*qspi));
	if (!master)
		return -ENOMEM;

	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_RX_DUAL | SPI_RX_QUAD;

	master->flags = SPI_MASTER_HALF_DUPLEX;
	master->setup = ti_qspi_setup;
	master->auto_runtime_pm = true;
	master->transfer_one_message = ti_qspi_start_transfer_one;
	master->dev.of_node = pdev->dev.of_node;
	master->bits_per_word_mask = SPI_BPW_MASK(32) | SPI_BPW_MASK(16) |
				     SPI_BPW_MASK(8);
	master->spi_flash_read = ti_qspi_spi_flash_read;
	master->mem_ops = &ti_qspi_mem_ops;

	if (!of_property_read_u32(np, "num-cs", &num_cs))
		master->num_chipselect = num_cs;

	qspi = spi_master_get_devdata(master);
	qspi->master = master;
	qspi->dev = &pdev->dev;
	platform_set_drvdata(pdev, qspi);

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qspi_base");
	if (r == NULL) {
		r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (r == NULL) {
			dev_err(&pdev->dev, "missing platform data\n");
			ret = -ENODEV;
			goto free_master;
		}
	}

	res_mmap = platform_get_resource_byname(pdev,
			IORESOURCE_MEM, "qspi_mmap");
	if (res_mmap == NULL) {
		res_mmap = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (res_mmap == NULL) {
			dev_err(&pdev->dev,
				"memory mapped resource not required\n");
		}
	}
	qspi->mmap_size = resource_size(res_mmap);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		ret = irq;
		goto free_master;
	}

	mutex_init(&qspi->list_lock);

	qspi->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(qspi->base)) {
		ret = PTR_ERR(qspi->base);
		goto free_master;
	}


	if (of_property_read_bool(np, "syscon-chipselects")) {
		qspi->ctrl_base =
		syscon_regmap_lookup_by_phandle(np,
						"syscon-chipselects");
		if (IS_ERR(qspi->ctrl_base)) {
			ret = PTR_ERR(qspi->ctrl_base);
			goto free_master;
		}
		ret = of_property_read_u32_index(np,
						 "syscon-chipselects",
						 1, &qspi->ctrl_reg);
		if (ret) {
			dev_err(&pdev->dev,
				"couldn't get ctrl_mod reg index\n");
			goto free_master;
		}
	}

	qspi->fclk = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(qspi->fclk)) {
		ret = PTR_ERR(qspi->fclk);
		dev_err(&pdev->dev, "could not get clk: %d\n", ret);
	}

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, QSPI_AUTOSUSPEND_TIMEOUT);
	pm_runtime_enable(&pdev->dev);

	if (!of_property_read_u32(np, "spi-max-frequency", &max_freq))
		qspi->spi_max_frequency = max_freq;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	qspi->rx_chan = dma_request_chan_by_mask(&mask);
	if (IS_ERR(qspi->rx_chan)) {
		dev_err(qspi->dev,
			"No Rx DMA available, trying mmap mode\n");
		qspi->rx_chan = NULL;
		ret = 0;
		goto no_dma;
	}
	qspi->rx_bb_addr = dma_alloc_coherent(qspi->dev,
					      QSPI_DMA_BUFFER_SIZE,
					      &qspi->rx_bb_dma_addr,
					      GFP_KERNEL | GFP_DMA);
	if (!qspi->rx_bb_addr) {
		dev_err(qspi->dev,
			"dma_alloc_coherent failed, using PIO mode\n");
		dma_release_channel(qspi->rx_chan);
		goto no_dma;
	}
	master->spi_flash_can_dma = ti_qspi_spi_flash_can_dma;
	master->dma_rx = qspi->rx_chan;
	init_completion(&qspi->transfer_complete);
	if (res_mmap)
		qspi->mmap_phys_base = (dma_addr_t)res_mmap->start;

no_dma:
	if (!qspi->rx_chan && res_mmap) {
		qspi->mmap_base = devm_ioremap_resource(&pdev->dev, res_mmap);
		if (IS_ERR(qspi->mmap_base)) {
			dev_info(&pdev->dev,
				 "mmap failed with error %ld using PIO mode\n",
				 PTR_ERR(qspi->mmap_base));
			qspi->mmap_base = NULL;
			master->spi_flash_read = NULL;
			master->mem_ops = NULL;
		}
	}
	qspi->mmap_enabled = false;

	ret = devm_spi_register_master(&pdev->dev, master);
	if (!ret)
		return 0;

	pm_runtime_disable(&pdev->dev);
free_master:
	spi_master_put(master);
	return ret;
}

static int ti_qspi_remove(struct platform_device *pdev)
{
	struct ti_qspi *qspi = platform_get_drvdata(pdev);
	int rc;

	rc = spi_master_suspend(qspi->master);
	if (rc)
		return rc;

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	if (qspi->rx_bb_addr)
		dma_free_coherent(qspi->dev, QSPI_DMA_BUFFER_SIZE,
				  qspi->rx_bb_addr,
				  qspi->rx_bb_dma_addr);
	if (qspi->rx_chan)
		dma_release_channel(qspi->rx_chan);

	return 0;
}

static const struct dev_pm_ops ti_qspi_pm_ops = {
	.runtime_resume = ti_qspi_runtime_resume,
};

static struct platform_driver ti_qspi_driver = {
	.probe	= ti_qspi_probe,
	.remove = ti_qspi_remove,
	.driver = {
		.name	= "ti-qspi",
		.pm =   &ti_qspi_pm_ops,
		.of_match_table = ti_qspi_match,
	}
};

module_platform_driver(ti_qspi_driver);

MODULE_AUTHOR("Sourav Poddar <sourav.poddar@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI QSPI controller driver");
MODULE_ALIAS("platform:ti-qspi");
