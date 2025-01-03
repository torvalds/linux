// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI QSPI driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - https://www.ti.com
 * Author: Sourav Poddar <sourav.poddar@ti.com>
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

	struct spi_controller	*host;
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

	u32 cmd;
	u32 dc;

	bool mmap_enabled;
	int current_cs;
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
	struct ti_qspi	*qspi = spi_controller_get_devdata(spi->controller);
	int ret;

	if (spi->controller->busy) {
		dev_dbg(qspi->dev, "host busy doing other transfers\n");
		return -EBUSY;
	}

	if (!qspi->host->max_speed_hz) {
		dev_err(qspi->dev, "spi max frequency not defined\n");
		return -EINVAL;
	}

	spi->max_speed_hz = min(spi->max_speed_hz, qspi->host->max_speed_hz);

	ret = pm_runtime_resume_and_get(qspi->dev);
	if (ret < 0) {
		dev_err(qspi->dev, "pm_runtime_get_sync() failed\n");
		return ret;
	}

	pm_runtime_mark_last_busy(qspi->dev);
	ret = pm_runtime_put_autosuspend(qspi->dev);
	if (ret < 0) {
		dev_err(qspi->dev, "pm_runtime_put_autosuspend() failed\n");
		return ret;
	}

	return 0;
}

static void ti_qspi_setup_clk(struct ti_qspi *qspi, u32 speed_hz)
{
	struct ti_qspi_regs *ctx_reg = &qspi->ctx_reg;
	int clk_div;
	u32 clk_ctrl_reg, clk_rate, clk_ctrl_new;

	clk_rate = clk_get_rate(qspi->fclk);
	clk_div = DIV_ROUND_UP(clk_rate, speed_hz) - 1;
	clk_div = clamp(clk_div, 0, QSPI_CLK_DIV_MAX);
	dev_dbg(qspi->dev, "hz: %d, clock divider %d\n", speed_hz, clk_div);

	pm_runtime_resume_and_get(qspi->dev);

	clk_ctrl_new = QSPI_CLK_EN | clk_div;
	if (ctx_reg->clkctrl != clk_ctrl_new) {
		clk_ctrl_reg = ti_qspi_read(qspi, QSPI_SPI_CLOCK_CNTRL_REG);

		clk_ctrl_reg &= ~QSPI_CLK_EN;

		/* disable SCLK */
		ti_qspi_write(qspi, clk_ctrl_reg, QSPI_SPI_CLOCK_CNTRL_REG);

		/* enable SCLK */
		ti_qspi_write(qspi, clk_ctrl_new, QSPI_SPI_CLOCK_CNTRL_REG);
		ctx_reg->clkctrl = clk_ctrl_new;
	}

	pm_runtime_mark_last_busy(qspi->dev);
	pm_runtime_put_autosuspend(qspi->dev);
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
	u32 rx;
	u8 rxlen, rx_wlen;
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
	rx_wlen = wlen;

	while (count) {
		dev_dbg(qspi->dev, "rx cmd %08x dc %08x\n", cmd, qspi->dc);
		if (qspi_is_busy(qspi))
			return -EBUSY;

		switch (wlen) {
		case 1:
			/*
			 * Optimize the 8-bit words transfers, as used by
			 * the SPI flash devices.
			 */
			if (count >= QSPI_WLEN_MAX_BYTES) {
				rxlen = QSPI_WLEN_MAX_BYTES;
			} else {
				rxlen = min(count, 4);
			}
			rx_wlen = rxlen << 3;
			cmd &= ~QSPI_WLEN_MASK;
			cmd |= QSPI_WLEN(rx_wlen);
			break;
		default:
			rxlen = wlen;
			break;
		}

		ti_qspi_write(qspi, cmd, QSPI_SPI_CMD_REG);
		if (ti_qspi_poll_wc(qspi)) {
			dev_err(qspi->dev, "read timed out\n");
			return -ETIMEDOUT;
		}

		switch (wlen) {
		case 1:
			/*
			 * Optimize the 8-bit words transfers, as used by
			 * the SPI flash devices.
			 */
			if (count >= QSPI_WLEN_MAX_BYTES) {
				u32 *rxp = (u32 *) rxbuf;
				rx = readl(qspi->base + QSPI_SPI_DATA_REG_3);
				*rxp++ = be32_to_cpu(rx);
				rx = readl(qspi->base + QSPI_SPI_DATA_REG_2);
				*rxp++ = be32_to_cpu(rx);
				rx = readl(qspi->base + QSPI_SPI_DATA_REG_1);
				*rxp++ = be32_to_cpu(rx);
				rx = readl(qspi->base + QSPI_SPI_DATA_REG);
				*rxp++ = be32_to_cpu(rx);
			} else {
				u8 *rxp = rxbuf;
				rx = readl(qspi->base + QSPI_SPI_DATA_REG);
				if (rx_wlen >= 8)
					*rxp++ = rx >> (rx_wlen - 8);
				if (rx_wlen >= 16)
					*rxp++ = rx >> (rx_wlen - 16);
				if (rx_wlen >= 24)
					*rxp++ = rx >> (rx_wlen - 24);
				if (rx_wlen >= 32)
					*rxp++ = rx;
			}
			break;
		case 2:
			*((u16 *)rxbuf) = readw(qspi->base + QSPI_SPI_DATA_REG);
			break;
		case 4:
			*((u32 *)rxbuf) = readl(qspi->base + QSPI_SPI_DATA_REG);
			break;
		}
		rxbuf += rxlen;
		count -= rxlen;
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
	unsigned long time_left;

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
	time_left = wait_for_completion_timeout(&qspi->transfer_complete,
					  msecs_to_jiffies(len));
	if (time_left == 0) {
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
	struct ti_qspi  *qspi = spi_controller_get_devdata(spi->controller);

	ti_qspi_write(qspi, MM_SWITCH, QSPI_SPI_SWITCH_REG);
	if (qspi->ctrl_base) {
		regmap_update_bits(qspi->ctrl_base, qspi->ctrl_reg,
				   MEM_CS_MASK,
				   MEM_CS_EN(spi_get_chipselect(spi, 0)));
	}
	qspi->mmap_enabled = true;
	qspi->current_cs = spi_get_chipselect(spi, 0);
}

static void ti_qspi_disable_memory_map(struct spi_device *spi)
{
	struct ti_qspi  *qspi = spi_controller_get_devdata(spi->controller);

	ti_qspi_write(qspi, 0, QSPI_SPI_SWITCH_REG);
	if (qspi->ctrl_base)
		regmap_update_bits(qspi->ctrl_base, qspi->ctrl_reg,
				   MEM_CS_MASK, 0);
	qspi->mmap_enabled = false;
	qspi->current_cs = -1;
}

static void ti_qspi_setup_mmap_read(struct spi_device *spi, u8 opcode,
				    u8 data_nbits, u8 addr_width,
				    u8 dummy_bytes)
{
	struct ti_qspi  *qspi = spi_controller_get_devdata(spi->controller);
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
		      QSPI_SPI_SETUP_REG(spi_get_chipselect(spi, 0)));
}

static int ti_qspi_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct ti_qspi *qspi = spi_controller_get_devdata(mem->spi->controller);
	size_t max_len;

	if (op->data.dir == SPI_MEM_DATA_IN) {
		if (op->addr.val < qspi->mmap_size) {
			/* Limit MMIO to the mmaped region */
			if (op->addr.val + op->data.nbytes > qspi->mmap_size) {
				max_len = qspi->mmap_size - op->addr.val;
				op->data.nbytes = min((size_t) op->data.nbytes,
						      max_len);
			}
		} else {
			/*
			 * Use fallback mode (SW generated transfers) above the
			 * mmaped region.
			 * Adjust size to comply with the QSPI max frame length.
			 */
			max_len = QSPI_FRAME;
			max_len -= 1 + op->addr.nbytes + op->dummy.nbytes;
			op->data.nbytes = min((size_t) op->data.nbytes,
					      max_len);
		}
	}

	return 0;
}

static int ti_qspi_exec_mem_op(struct spi_mem *mem,
			       const struct spi_mem_op *op)
{
	struct ti_qspi *qspi = spi_controller_get_devdata(mem->spi->controller);
	u32 from = 0;
	int ret = 0;

	/* Only optimize read path. */
	if (!op->data.nbytes || op->data.dir != SPI_MEM_DATA_IN ||
	    !op->addr.nbytes || op->addr.nbytes > 4)
		return -EOPNOTSUPP;

	/* Address exceeds MMIO window size, fall back to regular mode. */
	from = op->addr.val;
	if (from + op->data.nbytes > qspi->mmap_size)
		return -EOPNOTSUPP;

	mutex_lock(&qspi->list_lock);

	if (!qspi->mmap_enabled || qspi->current_cs != spi_get_chipselect(mem->spi, 0)) {
		ti_qspi_setup_clk(qspi, mem->spi->max_speed_hz);
		ti_qspi_enable_memory_map(mem->spi);
	}
	ti_qspi_setup_mmap_read(mem->spi, op->cmd.opcode, op->data.buswidth,
				op->addr.nbytes, op->dummy.nbytes);

	if (qspi->rx_chan) {
		struct sg_table sgt;

		if (virt_addr_valid(op->data.buf.in) &&
		    !spi_controller_dma_map_mem_op_data(mem->spi->controller, op,
							&sgt)) {
			ret = ti_qspi_dma_xfer_sg(qspi, sgt, from);
			spi_controller_dma_unmap_mem_op_data(mem->spi->controller,
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
	.adjust_op_size = ti_qspi_adjust_op_size,
};

static int ti_qspi_start_transfer_one(struct spi_controller *host,
		struct spi_message *m)
{
	struct ti_qspi *qspi = spi_controller_get_devdata(host);
	struct spi_device *spi = m->spi;
	struct spi_transfer *t;
	int status = 0, ret;
	unsigned int frame_len_words, transfer_len_words;
	int wlen;

	/* setup device control reg */
	qspi->dc = 0;

	if (spi->mode & SPI_CPHA)
		qspi->dc |= QSPI_CKPHA(spi_get_chipselect(spi, 0));
	if (spi->mode & SPI_CPOL)
		qspi->dc |= QSPI_CKPOL(spi_get_chipselect(spi, 0));
	if (spi->mode & SPI_CS_HIGH)
		qspi->dc |= QSPI_CSPOL(spi_get_chipselect(spi, 0));

	frame_len_words = 0;
	list_for_each_entry(t, &m->transfers, transfer_list)
		frame_len_words += t->len / (t->bits_per_word >> 3);
	frame_len_words = min_t(unsigned int, frame_len_words, QSPI_FRAME);

	/* setup command reg */
	qspi->cmd = 0;
	qspi->cmd |= QSPI_EN_CS(spi_get_chipselect(spi, 0));
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

		ti_qspi_setup_clk(qspi, t->speed_hz);
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
	spi_finalize_current_message(host);

	return status;
}

static int ti_qspi_runtime_resume(struct device *dev)
{
	struct ti_qspi      *qspi;

	qspi = dev_get_drvdata(dev);
	ti_qspi_restore_ctx(qspi);

	return 0;
}

static void ti_qspi_dma_cleanup(struct ti_qspi *qspi)
{
	if (qspi->rx_bb_addr)
		dma_free_coherent(qspi->dev, QSPI_DMA_BUFFER_SIZE,
				  qspi->rx_bb_addr,
				  qspi->rx_bb_dma_addr);

	if (qspi->rx_chan)
		dma_release_channel(qspi->rx_chan);
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
	struct spi_controller *host;
	struct resource         *r, *res_mmap;
	struct device_node *np = pdev->dev.of_node;
	u32 max_freq;
	int ret = 0, num_cs, irq;
	dma_cap_mask_t mask;

	host = spi_alloc_host(&pdev->dev, sizeof(*qspi));
	if (!host)
		return -ENOMEM;

	host->mode_bits = SPI_CPOL | SPI_CPHA | SPI_RX_DUAL | SPI_RX_QUAD;

	host->flags = SPI_CONTROLLER_HALF_DUPLEX;
	host->setup = ti_qspi_setup;
	host->auto_runtime_pm = true;
	host->transfer_one_message = ti_qspi_start_transfer_one;
	host->dev.of_node = pdev->dev.of_node;
	host->bits_per_word_mask = SPI_BPW_MASK(32) | SPI_BPW_MASK(16) |
				   SPI_BPW_MASK(8);
	host->mem_ops = &ti_qspi_mem_ops;

	if (!of_property_read_u32(np, "num-cs", &num_cs))
		host->num_chipselect = num_cs;

	qspi = spi_controller_get_devdata(host);
	qspi->host = host;
	qspi->dev = &pdev->dev;
	platform_set_drvdata(pdev, qspi);

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qspi_base");
	if (r == NULL) {
		r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (r == NULL) {
			dev_err(&pdev->dev, "missing platform data\n");
			ret = -ENODEV;
			goto free_host;
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

	if (res_mmap)
		qspi->mmap_size = resource_size(res_mmap);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto free_host;
	}

	mutex_init(&qspi->list_lock);

	qspi->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(qspi->base)) {
		ret = PTR_ERR(qspi->base);
		goto free_host;
	}


	if (of_property_present(np, "syscon-chipselects")) {
		qspi->ctrl_base =
		syscon_regmap_lookup_by_phandle(np,
						"syscon-chipselects");
		if (IS_ERR(qspi->ctrl_base)) {
			ret = PTR_ERR(qspi->ctrl_base);
			goto free_host;
		}
		ret = of_property_read_u32_index(np,
						 "syscon-chipselects",
						 1, &qspi->ctrl_reg);
		if (ret) {
			dev_err(&pdev->dev,
				"couldn't get ctrl_mod reg index\n");
			goto free_host;
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
		host->max_speed_hz = max_freq;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	qspi->rx_chan = dma_request_chan_by_mask(&mask);
	if (IS_ERR(qspi->rx_chan)) {
		dev_err(qspi->dev,
			"No Rx DMA available, trying mmap mode\n");
		qspi->rx_chan = NULL;
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
	host->dma_rx = qspi->rx_chan;
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
			host->mem_ops = NULL;
		}
	}
	qspi->mmap_enabled = false;
	qspi->current_cs = -1;

	ret = devm_spi_register_controller(&pdev->dev, host);
	if (!ret)
		return 0;

	ti_qspi_dma_cleanup(qspi);

	pm_runtime_disable(&pdev->dev);
free_host:
	spi_controller_put(host);
	return ret;
}

static void ti_qspi_remove(struct platform_device *pdev)
{
	struct ti_qspi *qspi = platform_get_drvdata(pdev);
	int rc;

	rc = spi_controller_suspend(qspi->host);
	if (rc) {
		dev_alert(&pdev->dev, "spi_controller_suspend() failed (%pe)\n",
			  ERR_PTR(rc));
		return;
	}

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	ti_qspi_dma_cleanup(qspi);
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
