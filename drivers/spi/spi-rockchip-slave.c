// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip SPI Slave Controller Driver
 *
 * Copyright (c) 2023, Rockchip Inc.
 * Author: Jon Lin <Jon.lin@rock-chips.com>
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>

#define DRIVER_NAME "rockchip-spi-slave"

/* SPI register offsets */
#define ROCKCHIP_SPI_CTRLR0			0x0000
#define ROCKCHIP_SPI_CTRLR1			0x0004
#define ROCKCHIP_SPI_SSIENR			0x0008
#define ROCKCHIP_SPI_SER			0x000c
#define ROCKCHIP_SPI_BAUDR			0x0010
#define ROCKCHIP_SPI_TXFTLR			0x0014
#define ROCKCHIP_SPI_RXFTLR			0x0018
#define ROCKCHIP_SPI_TXFLR			0x001c
#define ROCKCHIP_SPI_RXFLR			0x0020
#define ROCKCHIP_SPI_SR				0x0024
#define ROCKCHIP_SPI_IPR			0x0028
#define ROCKCHIP_SPI_IMR			0x002c
#define ROCKCHIP_SPI_ISR			0x0030
#define ROCKCHIP_SPI_RISR			0x0034
#define ROCKCHIP_SPI_ICR			0x0038
#define ROCKCHIP_SPI_DMACR			0x003c
#define ROCKCHIP_SPI_DMATDLR			0x0040
#define ROCKCHIP_SPI_DMARDLR			0x0044
#define ROCKCHIP_SPI_VERSION			0x0048
#define ROCKCHIP_SPI_TIMEOUT			0x004c
#define ROCKCHIP_SPI_BYPASS			0x0050
#define ROCKCHIP_SPI_BYPASS_ENR			0x0054
#define ROCKCHIP_SPI_TXDR			0x0400
#define ROCKCHIP_SPI_RXDR			0x0800

/* Bit fields in CTRLR0 */
#define CR0_DFS_OFFSET				0
#define CR0_DFS_4BIT				0x0
#define CR0_DFS_8BIT				0x1
#define CR0_DFS_16BIT				0x2

#define CR0_SCPH_OFFSET				6

#define CR0_EM_OFFSET				11
#define CR0_EM_LITTLE					0x0
#define CR0_EM_BIG					0x1

#define CR0_FBM_OFFSET				12
#define CR0_FBM_MSB					0x0
#define CR0_FBM_LSB					0x1

#define CR0_BHT_OFFSET				13
#define CR0_BHT_16BIT					0x0
#define CR0_BHT_8BIT					0x1

#define CR0_FRF_OFFSET				16
#define CR0_FRF_SPI					0x0
#define CR0_FRF_SSP					0x1
#define CR0_FRF_MICROWIRE				0x2

#define CR0_XFM_OFFSET				18
#define CR0_XFM_MASK					(0x03 << SPI_XFM_OFFSET)
#define CR0_XFM_TR					0x0
#define CR0_XFM_TO					0x1
#define CR0_XFM_RO					0x2

#define CR0_OPM_OFFSET				20
#define CR0_OPM_MASTER					0x0
#define CR0_OPM_SLAVE					0x1

#define CR0_SOI_OFFSET				23

/* Bit fields in SR, 6bit */
#define SR_MASK						0x3f
#define SR_BUSY						(1 << 0)
#define SR_TF_FULL					(1 << 1)
#define SR_TF_EMPTY					(1 << 2)
#define SR_RF_EMPTY					(1 << 3)
#define SR_RF_FULL					(1 << 4)
#define SR_SLAVE_TX_BUSY				(1 << 5)
#define SR_SS_IN_N					(1 << 6)

/* Bit fields in ISR, IMR, ISR, RISR, 5bit */
#define INT_MASK					0x1f
#define INT_TF_EMPTY					(1 << 0)
#define INT_TF_OVERFLOW					(1 << 1)
#define INT_RF_UNDERFLOW				(1 << 2)
#define INT_RF_OVERFLOW					(1 << 3)
#define INT_RF_FULL					(1 << 4)
#define INT_CS_INACTIVE					(1 << 6)

/* Bit fields in ICR, 4bit */
#define ICR_MASK					0x0f
#define ICR_ALL						(1 << 0)
#define ICR_RF_UNDERFLOW				(1 << 1)
#define ICR_RF_OVERFLOW					(1 << 2)
#define ICR_TF_OVERFLOW					(1 << 3)

/* Bit fields in DMACR */
#define RF_DMA_EN					(1 << 0)
#define TF_DMA_EN					(1 << 1)

/* Bit fields in TIMEOUT */
#define TIMEOUT_THRESHOLD_OFFSET		0x0
#define TIMEOUT_COUNTER_EN				(1 << 16)
#define TIMEOUT_SINGLE_REQUEST_EN			(1 << 18)

/* Bit fields in BYPASS */
#define BYPASS_EN					(1 << 0)
#define BYPASS_INT_TF_EN				(1 << 1)
#define CLOCK_GATING_NONE				(0x0 << 2)
#define CLOCK_GATING_SPICLK_INNER			(0x1 << 2)
#define CLOCK_GATING_SPICLK_EXT				(0x2 << 2)
#define CLOCK_GATING_ALL_IN_IDLE			(0x3 << 2)

/* Driver state flags */
#define RXDMA						(1 << 0)
#define TXDMA						(1 << 1)

/*
 * SPI_CTRLR1 is 16-bits, so we should support lengths of 0xffff + 1. However,
 * the controller seems to hang when given 0x10000, so stick with this for now.
 */
#define ROCKCHIP_SPI_MAX_TRANLEN		0xffff

#define ROCKCHIP_SPI_VER2_TYPE1			0x05EC0002
#define ROCKCHIP_SPI_VER2_TYPE2			0x00110002
#define ROCKCHIP_SPI_VER3			0x03110003

/*
 * The callback function may not be timely, and even cs has been released, so
 * some redundancy needs to be done for this.
 */
#define ROCKCHIP_SPI_DMA_CB_REDUNDANCY_US	10000

/*
 * Tclk-invalid-to-cs-deassert
 */
#define ROCKCHIP_SPI_CLK_TO_CS_DEASSERT_US	10000

enum rockchip_spi_slave_xfer_mode {
	ROCKCHIP_SPI_DMA,
	ROCKCHIP_SPI_IRQ,
};

struct rockchip_spi {
	struct device *dev;

	struct clk_bulk_data *clks;
	unsigned int clk_cnt;

	void __iomem *regs;
	dma_addr_t dma_addr_rx;
	dma_addr_t dma_addr_tx;
	u32 *dma_buf;
	dma_addr_t dma_phys;

	const void *tx;
	void *rx;
	unsigned int tx_left;
	unsigned int rx_left;

	atomic_t state;
	struct completion xfer_done;

	u32 version;
	/*depth of the FIFO buffer */
	u32 fifo_len;
	int max_transfer_size;
	u32 fixed_burst_size;
	u8 tx_idle_type; /* 0-SR_TF_EMPTY 1-SR_SLAVE_TX_BUSY */
	u32 dma_timeout;

	u8 n_bytes;

	bool slave_aborted;
	bool cs_inactive; /* spi slave tansmition stop when cs inactive */
	struct gpio_desc *ready; /* spi slave transmission ready */
	struct spi_transfer *xfer; /* Store xfer temporarily */
	enum rockchip_spi_slave_xfer_mode xfer_mode;
	bool ext_spi_clk;

	bool verbose;
	ktime_t dbg_time;
};

static inline void spi_enable_chip(struct rockchip_spi *rs, bool enable)
{
	if (rs->ext_spi_clk)
		writel_relaxed((enable ? 1U : 0U), rs->regs + ROCKCHIP_SPI_BYPASS_ENR);
	writel_relaxed((enable ? 1U : 0U), rs->regs + ROCKCHIP_SPI_SSIENR);
}

static inline void wait_for_tx_idle(struct rockchip_spi *rs)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5);
	u32 bit_filed = SR_TF_EMPTY;
	u32 idle_val = 1;

	/* When using external clock, tx clk can function normally without waiting for idle  */
	if (rs->ext_spi_clk)
		return;

	if (rs->tx_idle_type == 1) {
		bit_filed = SR_SLAVE_TX_BUSY;
		idle_val = 0;
	}

	do {
		if ((readl_relaxed(rs->regs + ROCKCHIP_SPI_SR) & bit_filed) == idle_val) {
			if (bit_filed == SR_TF_EMPTY)
				udelay(1);

			return;
		}
	} while (!time_after(jiffies, timeout));
	dev_warn(rs->dev, "spi controller is in busy state!\n");
}

static void rockchip_spi_slave_pio_writer(struct rockchip_spi *rs)
{
	u32 tx_free = rs->fifo_len - readl_relaxed(rs->regs + ROCKCHIP_SPI_TXFLR);
	u32 words = min(rs->tx_left, tx_free);

	rs->tx_left -= words;
	for (; words; words--) {
		u32 txw;

		if (rs->n_bytes == 1)
			txw = *(u8 *)rs->tx;
		else
			txw = *(u16 *)rs->tx;

		writel_relaxed(txw, rs->regs + ROCKCHIP_SPI_TXDR);
		rs->tx += rs->n_bytes;
	}
}

static void rockchip_spi_slave_pio_reader(struct rockchip_spi *rs)
{
	u32 rx_valid = readl_relaxed(rs->regs + ROCKCHIP_SPI_RXFLR);
	u32 words = min(rs->rx_left, rx_valid);

	rs->rx_left -= words;
	for (; words; words--) {
		u32 rxw = readl_relaxed(rs->regs + ROCKCHIP_SPI_RXDR);

		if (!rs->rx)
			continue;

		if (rs->n_bytes == 1)
			*(u8 *)rs->rx = (u8)rxw;
		else
			*(u16 *)rs->rx = (u16)rxw;
		rs->rx += rs->n_bytes;
	}
}

static int rockchop_spi_rx_fifo_flush(struct spi_controller *ctlr)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	u32 rx_fifo_left;

	/* Flush rx fifo */
	rx_fifo_left = readl_relaxed(rs->regs + ROCKCHIP_SPI_RXFLR);
	for (; rx_fifo_left; rx_fifo_left--)
		readl_relaxed(rs->regs + ROCKCHIP_SPI_RXDR);

	return 0;
}

static void rockchip_spi_slave_handle_err(struct spi_controller *ctlr)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	dev_err(rs->dev, "state=%x\n", atomic_read(&rs->state));
	dev_err(rs->dev, "tx_left=%x\n", rs->tx_left);
	dev_err(rs->dev, "rx_left=%x\n", rs->rx_left);
	print_hex_dump(KERN_ERR, "regs ", DUMP_PREFIX_OFFSET, 4, 4, rs->regs, 0x58, 0);

	rockchop_spi_rx_fifo_flush(ctlr);

	if (atomic_read(&rs->state) & TXDMA)
		dmaengine_terminate_async(ctlr->dma_tx);

	if (atomic_read(&rs->state) & RXDMA)
		dmaengine_terminate_async(ctlr->dma_rx);
	atomic_set(&rs->state, 0);
}

static irqreturn_t rockchip_spi_slave_isr(int irq, void *dev_id)
{
	struct spi_controller *ctlr = dev_id;
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	if (rs->xfer_mode == ROCKCHIP_SPI_IRQ) {
		if (rs->tx_left)
			rockchip_spi_slave_pio_writer(rs);

		rockchip_spi_slave_pio_reader(rs);
		if (!rs->rx_left) {
			writel_relaxed(0, rs->regs + ROCKCHIP_SPI_IMR);
			writel_relaxed(0xffffffff, rs->regs + ROCKCHIP_SPI_ICR);
			complete(&rs->xfer_done);
			return IRQ_HANDLED;
		}
	}

	/* When int_cs_inactive comes, spi slave abort */
	if (rs->cs_inactive && readl_relaxed(rs->regs + ROCKCHIP_SPI_ISR) & INT_CS_INACTIVE) {

		rs->slave_aborted = true;
		writel_relaxed(0, rs->regs + ROCKCHIP_SPI_IMR);
		writel_relaxed(0xffffffff, rs->regs + ROCKCHIP_SPI_ICR);
		complete(&rs->xfer_done);
	}

	return IRQ_HANDLED;
}

static int rockchip_spi_slave_prepare_irq(struct rockchip_spi *rs,
				    struct spi_controller *ctlr,
				    struct spi_transfer *xfer)
{
	rs->tx_left = rs->tx ? xfer->len / rs->n_bytes : 0;
	rs->rx_left = xfer->len / rs->n_bytes;

	writel_relaxed(0xffffffff, rs->regs + ROCKCHIP_SPI_ICR);

	spi_enable_chip(rs, true);

	if (rs->tx_left)
		rockchip_spi_slave_pio_writer(rs);

	if (rs->cs_inactive)
		writel_relaxed(INT_RF_FULL | INT_CS_INACTIVE, rs->regs + ROCKCHIP_SPI_IMR);
	else
		writel_relaxed(INT_RF_FULL, rs->regs + ROCKCHIP_SPI_IMR);
	/* 1 means the transfer is in progress */
	return 1;
}

static void rockchip_spi_slave_dma_rxcb(void *data)
{
	struct spi_controller *ctlr = data;
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	int state = atomic_fetch_andnot(RXDMA, &rs->state);

	if (state & TXDMA)
		return;

	complete(&rs->xfer_done);
}

static void rockchip_spi_slave_dma_txcb(void *data)
{
	struct spi_controller *ctlr = data;
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	int state = atomic_fetch_andnot(TXDMA, &rs->state);

	if (state & RXDMA)
		return;

	complete(&rs->xfer_done);
}

static u32 rockchip_spi_slave_calc_burst_size(struct rockchip_spi *rs, u32 data_len)
{
	u32 i;

	if (rs->dma_timeout)
		return rs->fixed_burst_size;

	/* burst size: 1, 2, 4, 8, 16 */
	for (i = 1; i < 16; i <<= 1) {
		if (data_len & i)
			break;
	}

	return i;
}

static int rockchip_spi_slave_prepare_dma(struct rockchip_spi *rs,
		struct spi_controller *ctlr, struct spi_transfer *xfer)
{
	struct dma_async_tx_descriptor *rxdesc = NULL, *txdesc = NULL;

	atomic_set(&rs->state, 0);

	if (xfer->rx_buf) {
		struct dma_slave_config rxconf = {
			.direction = DMA_DEV_TO_MEM,
			.src_addr = rs->dma_addr_rx,
			.src_addr_width = rs->n_bytes,
			.src_maxburst = rockchip_spi_slave_calc_burst_size(rs, xfer->len / rs->n_bytes),
		};

		dmaengine_slave_config(ctlr->dma_rx, &rxconf);

		rxdesc = dmaengine_prep_slave_single(ctlr->dma_rx, rs->dma_phys, xfer->len,
						     DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);
		if (!rxdesc)
			return -EINVAL;

		rxdesc->callback = rockchip_spi_slave_dma_rxcb;
		rxdesc->callback_param = ctlr;
	}

	if (xfer->tx_buf) {
		struct dma_slave_config txconf = {
			.direction = DMA_MEM_TO_DEV,
			.dst_addr = rs->dma_addr_tx,
			.dst_addr_width = rs->n_bytes,
			.dst_maxburst = 8,
		};

		dmaengine_slave_config(ctlr->dma_tx, &txconf);

		memcpy(rs->dma_buf, xfer->tx_buf, xfer->len);
		txdesc = dmaengine_prep_slave_single(ctlr->dma_tx, rs->dma_phys, xfer->len,
						     DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
		if (!txdesc) {
			if (rxdesc)
				dmaengine_terminate_sync(ctlr->dma_rx);
			return -EINVAL;
		}

		txdesc->callback = rockchip_spi_slave_dma_txcb;
		txdesc->callback_param = ctlr;
	}

	/* rx must be started before tx due to spi instinct */
	if (rxdesc) {
		atomic_or(RXDMA, &rs->state);
		ctlr->dma_rx->cookie = dmaengine_submit(rxdesc);
		dma_async_issue_pending(ctlr->dma_rx);
	}

	if (rs->cs_inactive)
		writel_relaxed(INT_CS_INACTIVE, rs->regs + ROCKCHIP_SPI_IMR);

	spi_enable_chip(rs, true);

	if (txdesc) {
		atomic_or(TXDMA, &rs->state);
		dmaengine_submit(txdesc);
		dma_async_issue_pending(ctlr->dma_tx);
	}

	/* 1 means the transfer is in progress */
	return 1;
}

static int rockchip_spi_slave_config(struct rockchip_spi *rs,
		struct spi_device *spi, struct spi_transfer *xfer)
{
	u32 cr0 = CR0_FRF_SPI  << CR0_FRF_OFFSET
		| CR0_BHT_8BIT << CR0_BHT_OFFSET
		| CR0_EM_BIG   << CR0_EM_OFFSET
		| CR0_OPM_SLAVE << CR0_OPM_OFFSET;
	u32 cr1;
	u32 dmacr = 0;
	u32 val = 0;

	rs->slave_aborted = false;

	cr0 |= (spi->mode & 0x3U) << CR0_SCPH_OFFSET;
	if (spi->mode & SPI_LSB_FIRST)
		cr0 |= CR0_FBM_LSB << CR0_FBM_OFFSET;

	if (xfer->rx_buf && xfer->tx_buf) {
		cr0 |= CR0_XFM_TR << CR0_XFM_OFFSET;
	} else if (xfer->rx_buf) {
		cr0 |= CR0_XFM_RO << CR0_XFM_OFFSET;
	} else if (xfer->tx_buf) {
		/*
		 * Use the water line of rx fifo in full duplex mode to trigger
		 * the interruption of tx irq transmission completion.
		 * For the new version of SPI witch support ext_spi_clk, TX under
		 * IRQ transmission can only be developed using TR.
		 */
		if (rs->xfer_mode == ROCKCHIP_SPI_IRQ)
			cr0 |= CR0_XFM_TR << CR0_XFM_OFFSET;
		else
			cr0 |= CR0_XFM_TO << CR0_XFM_OFFSET;
	} else {
		dev_err(rs->dev, "no transmission buffer\n");
		return -EINVAL;
	}

	switch (xfer->bits_per_word) {
	case 4:
		cr0 |= CR0_DFS_4BIT << CR0_DFS_OFFSET;
		cr1 = xfer->len - 1;
		break;
	case 8:
		cr0 |= CR0_DFS_8BIT << CR0_DFS_OFFSET;
		cr1 = xfer->len - 1;
		break;
	case 16:
		cr0 |= CR0_DFS_16BIT << CR0_DFS_OFFSET;
		cr1 = xfer->len / 2 - 1;
		if (xfer->len & 0x1) {
			dev_err(rs->dev, "xfer->len is not aligned with 16bits\n");
			return -EINVAL;
		}
		break;
	default:
		/* we only whitelist 4, 8 and 16 bit words in
		 * ctlr->bits_per_word_mask, so this shouldn't
		 * happen
		 */
		dev_err(rs->dev, "unknown bits per word: %d\n",
			xfer->bits_per_word);
		return -EINVAL;
	}

	if (rs->xfer_mode == ROCKCHIP_SPI_DMA) {
		if (xfer->tx_buf)
			dmacr |= TF_DMA_EN;
		if (xfer->rx_buf)
			dmacr |= RF_DMA_EN;
	}

	writel_relaxed(cr0, rs->regs + ROCKCHIP_SPI_CTRLR0);
	writel_relaxed(cr1, rs->regs + ROCKCHIP_SPI_CTRLR1);
	if (rs->ext_spi_clk) {
		val = BYPASS_EN | BYPASS_INT_TF_EN | CLOCK_GATING_NONE;
		writel_relaxed(val, rs->regs + ROCKCHIP_SPI_BYPASS);
	}
	if (rs->dma_timeout) {
		val = TIMEOUT_SINGLE_REQUEST_EN | TIMEOUT_COUNTER_EN |
			16 << TIMEOUT_THRESHOLD_OFFSET;
		writel_relaxed(val, rs->regs + ROCKCHIP_SPI_TIMEOUT);
	}

	/* unfortunately setting the fifo threshold level to generate an
	 * interrupt exactly when the fifo is full doesn't seem to work,
	 * so we need the strict inequality here
	 */
	if ((xfer->len / rs->n_bytes) < rs->fifo_len)
		writel_relaxed(xfer->len / rs->n_bytes - 1, rs->regs + ROCKCHIP_SPI_RXFTLR);
	else
		writel_relaxed(rs->fifo_len / 2 - 1, rs->regs + ROCKCHIP_SPI_RXFTLR);

	writel_relaxed(rs->fifo_len / 2 - 1, rs->regs + ROCKCHIP_SPI_DMATDLR);
	writel_relaxed(rockchip_spi_slave_calc_burst_size(rs, xfer->len / rs->n_bytes) - 1,
		       rs->regs + ROCKCHIP_SPI_DMARDLR);
	writel_relaxed(dmacr, rs->regs + ROCKCHIP_SPI_DMACR);

	return 0;
}

static size_t rockchip_spi_slave_max_transfer_size(struct spi_device *spi)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(spi->controller);

	return rs->max_transfer_size;
}

static int rockchip_spi_slave_transfer_wait(struct spi_controller *ctlr)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	int state;
	int ret = 0;

	if (wait_for_completion_interruptible(&rs->xfer_done)) {
		dev_dbg(rs->dev, "RK SPI transfer interrupted\n");
		ret = -EINTR;
		goto out;
	}

	if (rs->slave_aborted) {
		if (rs->xfer_mode == ROCKCHIP_SPI_DMA) {
			state = atomic_read(&rs->state);
			reinit_completion(&rs->xfer_done);

			if (state && state == atomic_read(&rs->state) &&
			    wait_for_completion_interruptible_timeout(&rs->xfer_done,
				ROCKCHIP_SPI_DMA_CB_REDUNDANCY_US) <= 0) {
				dev_err(rs->dev, "RK SPI transfer slave abort\n");
				ret = -EIO;
				goto out;
			} else {
				dev_dbg(rs->dev, "RK SPI transfer slave dma cb lately\n");
			}
		} else {
			dev_err(rs->dev, "RK SPI transfer slave abort\n");
			ret = -EIO;
			goto out;
		}

	}

	if (rs->xfer->tx_buf)
		wait_for_tx_idle(rs);
out:
	if (ret)
		rockchip_spi_slave_handle_err(ctlr);

	return 0;
}

static int rockchip_spi_slave_stop(struct spi_controller *ctlr)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	spi_enable_chip(rs, false);

	return 0;
}

static int rockchip_spi_slave_do_one_msg(struct spi_controller *ctlr, struct spi_message *m)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	struct spi_device *spi = m->spi;
	struct spi_transfer *xfer;
	int ret = -EINVAL;
	bool use_dma;
	u32 status;

	WARN_ON(readl_relaxed(rs->regs + ROCKCHIP_SPI_SSIENR) &&
		(readl_relaxed(rs->regs + ROCKCHIP_SPI_SR) & SR_BUSY));

	if (rs->cs_inactive) {
		ret = readl_poll_timeout(rs->regs + ROCKCHIP_SPI_SR, status,
					 status & SR_SS_IN_N, 20,
					 ROCKCHIP_SPI_CLK_TO_CS_DEASSERT_US);
		if (ret) {
			dev_err(rs->dev, "The cs of spi master is still asserted\n");
			ret = -EIO;
			goto out;
		}
	}

	list_for_each_entry(xfer, &m->transfers, transfer_list) {
		if (!xfer->len || (!xfer->tx_buf && !xfer->rx_buf)) {
			dev_err(rs->dev, "No buffer for transfer\n");
			ret = -EINVAL;
			goto out;
		}

		if (xfer->len > rs->max_transfer_size) {
			dev_err(rs->dev, "Transfer is too long (%d)\n", xfer->len);
			ret = -EINVAL;
			goto out;
		}

		rs->n_bytes = xfer->bits_per_word <= 8 ? 1 : 2;
		rs->xfer = xfer;

		use_dma = ctlr->can_dma(ctlr, spi, xfer);
		if (use_dma)
			rs->xfer_mode = ROCKCHIP_SPI_DMA;
		else
			rs->xfer_mode = ROCKCHIP_SPI_IRQ;

		ret = rockchip_spi_slave_config(rs, spi, xfer);
		if (ret)
			goto out;

		rs->tx = xfer->tx_buf;
		rs->rx = xfer->rx_buf;

		reinit_completion(&rs->xfer_done);

		switch (rs->xfer_mode) {
		case ROCKCHIP_SPI_DMA:
			ret = rockchip_spi_slave_prepare_dma(rs, ctlr, xfer);
			break;
		default:
			ret = rockchip_spi_slave_prepare_irq(rs, ctlr, xfer);
		}

		if (rs->ready) {
			gpiod_set_value(rs->ready, 0);
			gpiod_set_value(rs->ready, 1);
		}

		if (rs->verbose)
			dev_info(rs->dev, "cur_gap=%lldus\n",
				 ktime_to_us(ktime_sub(ktime_get(), rs->dbg_time)));
		if (ret > 0)
			ret = rockchip_spi_slave_transfer_wait(ctlr);
		if (rs->verbose)
			rs->dbg_time = ktime_get();

		rockchip_spi_slave_stop(ctlr);

		if (xfer->rx_buf && rs->xfer_mode == ROCKCHIP_SPI_DMA)
			memcpy(xfer->rx_buf, rs->dma_buf, xfer->len);

		m->actual_length += xfer->len;

		if (xfer->delay_usecs)
			udelay(xfer->delay_usecs);
	}

out:
	m->status = ret;

	spi_finalize_current_message(ctlr);
	return 0;
}

static bool rockchip_spi_slave_can_dma(struct spi_controller *ctlr,
				 struct spi_device *spi,
				 struct spi_transfer *xfer)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);
	unsigned int bytes_per_word = xfer->bits_per_word <= 8 ? 1 : 2;

	/* if the numbor of spi words to transfer is less than the fifo
	 * length we can just fill the fifo and wait for a single irq,
	 * so don't bother setting up dma
	 */
	return xfer->len / bytes_per_word >= rs->fifo_len;
}

static int rockchip_spi_slave_setup(struct spi_device *spi)
{
	struct rockchip_spi *rs = spi_controller_get_devdata(spi->controller);
	u32 cr0;

	pm_runtime_get_sync(rs->dev);

	cr0 = readl_relaxed(rs->regs + ROCKCHIP_SPI_CTRLR0);
	cr0 |= ((spi->mode & 0x3) << CR0_SCPH_OFFSET) | CR0_OPM_SLAVE << CR0_OPM_OFFSET;
	writel_relaxed(cr0, rs->regs + ROCKCHIP_SPI_CTRLR0);

	pm_runtime_put(rs->dev);

	return 0;
}

static int rockchip_spi_slave_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct rockchip_spi *rs;
	struct spi_controller *ctlr;
	struct resource *mem;
	struct device_node *sram_np;
	struct resource sram_res;
	u32 val;

	ctlr = spi_alloc_slave(&pdev->dev, sizeof(struct rockchip_spi));
	if (!ctlr)
		return -ENOMEM;

	ctlr->rt = true;
	platform_set_drvdata(pdev, ctlr);

	rs = spi_controller_get_devdata(ctlr);

	/* Get basic io resource and map it */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rs->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(rs->regs)) {
		ret =  PTR_ERR(rs->regs);
		goto err_put_ctlr;
	}

	if (!has_acpi_companion(&pdev->dev))
		ret = devm_clk_bulk_get_all(&pdev->dev, &rs->clks);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to get clks\n");
		goto err_put_ctlr;
	}
	rs->clk_cnt = ret;

	if (!has_acpi_companion(&pdev->dev))
		ret = clk_bulk_prepare_enable(rs->clk_cnt, rs->clks);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable clks\n");
		goto err_put_ctlr;
	}

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set dma mask\n");
		goto err_put_ctlr;
	}

	sram_np = of_parse_phandle(pdev->dev.of_node, "rockchip,sram", 0);
	if (sram_np) {
		/* get sram start and size */
		ret = of_address_to_resource(sram_np, 0, &sram_res);
		if (ret) {
			dev_err(&pdev->dev, "find sram res error\n");
			goto err_put_ctlr;
		}

		rs->max_transfer_size = resource_size(&sram_res);
		rs->dma_phys = sram_res.start;
		rs->dma_buf = devm_ioremap_resource(&pdev->dev, &sram_res);
		if (IS_ERR(rs->dma_buf)) {
			ret = PTR_ERR(rs->dma_buf);
			goto err_put_ctlr;
		}
		dev_err(&pdev->dev, "set sram_buf\n");
	} else {
		rs->max_transfer_size = ROCKCHIP_SPI_MAX_TRANLEN;
		rs->dma_buf = dma_alloc_coherent(&pdev->dev, rs->max_transfer_size + 1,
						&rs->dma_phys, GFP_KERNEL);
		if (!rs->dma_buf) {
			ret = -ENOMEM;
			goto err_put_ctlr;
		}
	}

	spi_enable_chip(rs, false);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_disable_spiclk;

	ret = devm_request_irq(&pdev->dev, ret, rockchip_spi_slave_isr, 0, dev_name(&pdev->dev), ctlr);
	if (ret)
		goto err_disable_spiclk;

	rs->dev = &pdev->dev;

	rs->version = readl_relaxed(rs->regs + ROCKCHIP_SPI_VERSION);
	rs->verbose = 0;

	device_property_read_u32(&pdev->dev, "rockchip,autosuspend-delay-ms", &val);
	if (val <= 0)
		val = (ROCKCHIP_SPI_CLK_TO_CS_DEASSERT_US + 999) / 1000;
	pm_runtime_set_autosuspend_delay(&pdev->dev, val);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	ctlr->auto_runtime_pm = true;

	ctlr->bus_num = pdev->id;
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;
	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->bits_per_word_mask = SPI_BPW_MASK(16) | SPI_BPW_MASK(8) | SPI_BPW_MASK(4);

	ctlr->setup = rockchip_spi_slave_setup;
	ctlr->transfer_one_message = rockchip_spi_slave_do_one_msg;
	ctlr->max_transfer_size = rockchip_spi_slave_max_transfer_size;

	ctlr->dma_tx = dma_request_chan(rs->dev, "tx");
	if (IS_ERR(ctlr->dma_tx)) {
		/* Check tx to see if we need defer probing driver */
		if (PTR_ERR(ctlr->dma_tx) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_disable_pm_runtime;
		}
		dev_warn(rs->dev, "Failed to request TX DMA channel\n");
		ret = -EINVAL;
		goto err_disable_spiclk;
	}
	ctlr->dma_rx = dma_request_chan(rs->dev, "rx");
	if (IS_ERR(ctlr->dma_rx)) {
		if (PTR_ERR(ctlr->dma_rx) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_free_dma_tx;
		}
		dev_warn(rs->dev, "Failed to request RX DMA channel\n");
		ret = -EINVAL;
		goto err_free_dma_tx;
	}
	rs->dma_addr_tx = mem->start + ROCKCHIP_SPI_TXDR;
	rs->dma_addr_rx = mem->start + ROCKCHIP_SPI_RXDR;
	ctlr->can_dma = rockchip_spi_slave_can_dma;

	init_completion(&rs->xfer_done);
	switch (rs->version) {
	case ROCKCHIP_SPI_VER3:
		rs->ext_spi_clk = true;
		rs->dma_timeout = 16;
		rs->fixed_burst_size = 16;
		rs->tx_idle_type = 1;
		rs->cs_inactive = true;
		rs->fifo_len = 64;
		break;
	case ROCKCHIP_SPI_VER2_TYPE2:
		rs->tx_idle_type = 1;
		rs->cs_inactive = true;
		rs->fifo_len = 64;
		break;
	case ROCKCHIP_SPI_VER2_TYPE1:
		rs->tx_idle_type = 0;
		rs->cs_inactive = true;
		rs->fifo_len = 64;
		break;
	default:
		rs->ext_spi_clk = false;
		rs->dma_timeout = 0;
		rs->fixed_burst_size = 0;
		rs->tx_idle_type = 0;
		rs->cs_inactive = false;
		rs->fifo_len = 32;
	}

	if (device_property_read_bool(&pdev->dev, "rockchip,cs-inactive-disable"))
		rs->cs_inactive = false;

	if (!device_property_read_bool(&pdev->dev, "rockchip,dma-support-req-mix"))
		rs->dma_timeout = 0;

	rs->ready = devm_gpiod_get_optional(&pdev->dev, "ready", GPIOD_OUT_HIGH);
	if (IS_ERR(rs->ready)) {
		ret = PTR_ERR(rs->ready);
		dev_err(&pdev->dev, "invalid ready-gpios property in node\n");
		goto err_free_dma_rx;
	}

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register controller\n");
		goto err_free_dma_rx;
	}

	dev_info(rs->dev, "slave probed, cs-inactive=%d, ready=%d, ext=%d, dam_buf=%x\n",
		 rs->cs_inactive, rs->ready ? 1 : 0, rs->ext_spi_clk, (u32)rs->dma_phys);

	return 0;

err_free_dma_rx:
	dma_release_channel(ctlr->dma_rx);
err_free_dma_tx:
	dma_release_channel(ctlr->dma_tx);
err_disable_pm_runtime:
	pm_runtime_disable(&pdev->dev);
err_disable_spiclk:
	clk_bulk_disable_unprepare(rs->clk_cnt, rs->clks);
err_put_ctlr:
	spi_controller_put(ctlr);

	return ret;
}

static int rockchip_spi_slave_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = spi_controller_get(platform_get_drvdata(pdev));
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	pm_runtime_get_sync(&pdev->dev);

	clk_bulk_disable_unprepare(rs->clk_cnt, rs->clks);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	if (ctlr->dma_tx)
		dma_release_channel(ctlr->dma_tx);
	if (ctlr->dma_rx)
		dma_release_channel(ctlr->dma_rx);

	spi_controller_put(ctlr);

	return 0;
}

#ifdef CONFIG_PM
static int rockchip_spi_slave_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	clk_bulk_disable_unprepare(rs->clk_cnt, rs->clks);

	return 0;
}

static int rockchip_spi_slave_runtime_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct rockchip_spi *rs = spi_controller_get_devdata(ctlr);

	return clk_bulk_prepare_enable(rs->clk_cnt, rs->clks);
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
static int rockchip_spi_slave_suspend(struct device *dev)
{
	int ret;
	struct spi_controller *ctlr = dev_get_drvdata(dev);

	ret = spi_controller_suspend(ctlr);
	if (ret < 0)
		return ret;

	/* Avoid redundant clock disable */
	if (!pm_runtime_status_suspended(dev))
		rockchip_spi_slave_runtime_suspend(dev);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int rockchip_spi_slave_resume(struct device *dev)
{
	int ret;
	struct spi_controller *ctlr = dev_get_drvdata(dev);

	pinctrl_pm_select_default_state(dev);

	if (!pm_runtime_status_suspended(dev)) {
		ret = rockchip_spi_slave_runtime_resume(dev);
		if (ret < 0)
			return ret;
	}

	ret = spi_controller_resume(ctlr);
	if (ret < 0)
		rockchip_spi_slave_runtime_suspend(dev);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops rockchip_spi_slave_pm = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(rockchip_spi_slave_suspend, rockchip_spi_slave_resume)
	SET_RUNTIME_PM_OPS(rockchip_spi_slave_runtime_suspend,
			   rockchip_spi_slave_runtime_resume, NULL)
};

static const struct of_device_id rockchip_spi_slave_dt_match[] = {
	{ .compatible = "rockchip,spi-slave", },
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_spi_slave_dt_match);

static struct platform_driver rockchip_spi_slave_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.pm = &rockchip_spi_slave_pm,
		.of_match_table = of_match_ptr(rockchip_spi_slave_dt_match),
	},
	.probe = rockchip_spi_slave_probe,
	.remove = rockchip_spi_slave_remove,
};

module_platform_driver(rockchip_spi_slave_driver);

MODULE_AUTHOR("Jon Lin <jon.lin@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP SPI Slave Controller Driver");
MODULE_LICENSE("GPL");
