// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Serial Flash Controller Driver
 *
 * Copyright (c) 2017-2021, Rockchip Inc.
 * Author: Shawn Lin <shawn.lin@rock-chips.com>
 *	   Chris Morgan <macroalpha82@gmail.com>
 *	   Jon Lin <Jon.lin@rock-chips.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spi/spi-mem.h>

/* System control */
#define SFC_CTRL			0x0
#define  SFC_CTRL_PHASE_SEL_NEGETIVE	BIT(1)
#define  SFC_CTRL_CMD_BITS_SHIFT	8
#define  SFC_CTRL_ADDR_BITS_SHIFT	10
#define  SFC_CTRL_DATA_BITS_SHIFT	12

/* Interrupt mask */
#define SFC_IMR				0x4
#define  SFC_IMR_RX_FULL		BIT(0)
#define  SFC_IMR_RX_UFLOW		BIT(1)
#define  SFC_IMR_TX_OFLOW		BIT(2)
#define  SFC_IMR_TX_EMPTY		BIT(3)
#define  SFC_IMR_TRAN_FINISH		BIT(4)
#define  SFC_IMR_BUS_ERR		BIT(5)
#define  SFC_IMR_NSPI_ERR		BIT(6)
#define  SFC_IMR_DMA			BIT(7)

/* Interrupt clear */
#define SFC_ICLR			0x8
#define  SFC_ICLR_RX_FULL		BIT(0)
#define  SFC_ICLR_RX_UFLOW		BIT(1)
#define  SFC_ICLR_TX_OFLOW		BIT(2)
#define  SFC_ICLR_TX_EMPTY		BIT(3)
#define  SFC_ICLR_TRAN_FINISH		BIT(4)
#define  SFC_ICLR_BUS_ERR		BIT(5)
#define  SFC_ICLR_NSPI_ERR		BIT(6)
#define  SFC_ICLR_DMA			BIT(7)

/* FIFO threshold level */
#define SFC_FTLR			0xc
#define  SFC_FTLR_TX_SHIFT		0
#define  SFC_FTLR_TX_MASK		0x1f
#define  SFC_FTLR_RX_SHIFT		8
#define  SFC_FTLR_RX_MASK		0x1f

/* Reset FSM and FIFO */
#define SFC_RCVR			0x10
#define  SFC_RCVR_RESET			BIT(0)

/* Enhanced mode */
#define SFC_AX				0x14

/* Address Bit number */
#define SFC_ABIT			0x18

/* Interrupt status */
#define SFC_ISR				0x1c
#define  SFC_ISR_RX_FULL_SHIFT		BIT(0)
#define  SFC_ISR_RX_UFLOW_SHIFT		BIT(1)
#define  SFC_ISR_TX_OFLOW_SHIFT		BIT(2)
#define  SFC_ISR_TX_EMPTY_SHIFT		BIT(3)
#define  SFC_ISR_TX_FINISH_SHIFT	BIT(4)
#define  SFC_ISR_BUS_ERR_SHIFT		BIT(5)
#define  SFC_ISR_NSPI_ERR_SHIFT		BIT(6)
#define  SFC_ISR_DMA_SHIFT		BIT(7)

/* FIFO status */
#define SFC_FSR				0x20
#define  SFC_FSR_TX_IS_FULL		BIT(0)
#define  SFC_FSR_TX_IS_EMPTY		BIT(1)
#define  SFC_FSR_RX_IS_EMPTY		BIT(2)
#define  SFC_FSR_RX_IS_FULL		BIT(3)
#define  SFC_FSR_TXLV_MASK		GENMASK(12, 8)
#define  SFC_FSR_TXLV_SHIFT		8
#define  SFC_FSR_RXLV_MASK		GENMASK(20, 16)
#define  SFC_FSR_RXLV_SHIFT		16

/* FSM status */
#define SFC_SR				0x24
#define  SFC_SR_IS_IDLE			0x0
#define  SFC_SR_IS_BUSY			0x1

/* Raw interrupt status */
#define SFC_RISR			0x28
#define  SFC_RISR_RX_FULL		BIT(0)
#define  SFC_RISR_RX_UNDERFLOW		BIT(1)
#define  SFC_RISR_TX_OVERFLOW		BIT(2)
#define  SFC_RISR_TX_EMPTY		BIT(3)
#define  SFC_RISR_TRAN_FINISH		BIT(4)
#define  SFC_RISR_BUS_ERR		BIT(5)
#define  SFC_RISR_NSPI_ERR		BIT(6)
#define  SFC_RISR_DMA			BIT(7)

/* Version */
#define SFC_VER				0x2C
#define  SFC_VER_3			0x3
#define  SFC_VER_4			0x4
#define  SFC_VER_5			0x5
#define  SFC_VER_8			0x8

/* Delay line controller register */
#define SFC_DLL_CTRL0			0x3C
#define SFC_DLL_CTRL0_SCLK_SMP_DLL	BIT(15)
#define SFC_DLL_CTRL0_DLL_MAX_VER4	0xFFU
#define SFC_DLL_CTRL0_DLL_MAX_VER5	0x1FFU

/* Master trigger */
#define SFC_DMA_TRIGGER			0x80
#define SFC_DMA_TRIGGER_START		1

/* Src or Dst addr for master */
#define SFC_DMA_ADDR			0x84

/* Length control register extension 32GB */
#define SFC_LEN_CTRL			0x88
#define SFC_LEN_CTRL_TRB_SEL		1
#define SFC_LEN_EXT			0x8C

/* Command */
#define SFC_CMD				0x100
#define  SFC_CMD_IDX_SHIFT		0
#define  SFC_CMD_DUMMY_SHIFT		8
#define  SFC_CMD_DIR_SHIFT		12
#define  SFC_CMD_DIR_RD			0
#define  SFC_CMD_DIR_WR			1
#define  SFC_CMD_ADDR_SHIFT		14
#define  SFC_CMD_ADDR_0BITS		0
#define  SFC_CMD_ADDR_24BITS		1
#define  SFC_CMD_ADDR_32BITS		2
#define  SFC_CMD_ADDR_XBITS		3
#define  SFC_CMD_TRAN_BYTES_SHIFT	16
#define  SFC_CMD_CS_SHIFT		30

/* Address */
#define SFC_ADDR			0x104

/* Data */
#define SFC_DATA			0x108

#define SFC_CS1_REG_OFFSET		0x200

#define SFC_MAX_CHIPSELECT_NUM		2

#define SFC_MAX_IOSIZE_VER3		(512 * 31)
/* Although up to 4GB, 64KB is enough with less mem reserved */
#define SFC_MAX_IOSIZE_VER4		(0x10000U)

/* DMA is only enabled for large data transmission */
#define SFC_DMA_TRANS_THRETHOLD		(0x40)

/* Maximum clock values from datasheet suggest keeping clock value under
 * 150MHz. No minimum or average value is suggested.
 */
#define SFC_MAX_SPEED		(150 * 1000 * 1000)

#define ROCKCHIP_AUTOSUSPEND_DELAY	2000

struct rockchip_sfc {
	struct device *dev;
	void __iomem *regbase;
	struct clk *hclk;
	struct clk *clk;
	u32 speed[SFC_MAX_CHIPSELECT_NUM];
	/* virtual mapped addr for dma_buffer */
	void *buffer;
	dma_addr_t dma_buffer;
	struct completion cp;
	bool use_dma;
	u32 max_iosize;
	u16 version;
	struct spi_controller *host;
};

static int rockchip_sfc_reset(struct rockchip_sfc *sfc)
{
	int err;
	u32 status;

	writel_relaxed(SFC_RCVR_RESET, sfc->regbase + SFC_RCVR);

	err = readl_poll_timeout(sfc->regbase + SFC_RCVR, status,
				 !(status & SFC_RCVR_RESET), 20,
				 jiffies_to_usecs(HZ));
	if (err)
		dev_err(sfc->dev, "SFC reset never finished\n");

	/* Still need to clear the masked interrupt from RISR */
	writel_relaxed(0xFFFFFFFF, sfc->regbase + SFC_ICLR);

	dev_dbg(sfc->dev, "reset\n");

	return err;
}

static u16 rockchip_sfc_get_version(struct rockchip_sfc *sfc)
{
	return  (u16)(readl(sfc->regbase + SFC_VER) & 0xffff);
}

static u32 rockchip_sfc_get_max_iosize(struct rockchip_sfc *sfc)
{
	return SFC_MAX_IOSIZE_VER3;
}

static int rockchip_sfc_clk_set_rate(struct rockchip_sfc *sfc, unsigned long  speed)
{
	if (sfc->version >= SFC_VER_8)
		return clk_set_rate(sfc->clk, speed * 2);
	else
		return clk_set_rate(sfc->clk, speed);
}

static unsigned long rockchip_sfc_clk_get_rate(struct rockchip_sfc *sfc)
{
	if (sfc->version >= SFC_VER_8)
		return clk_get_rate(sfc->clk) / 2;
	else
		return clk_get_rate(sfc->clk);
}

static void rockchip_sfc_irq_unmask(struct rockchip_sfc *sfc, u32 mask)
{
	u32 reg;

	/* Enable transfer complete interrupt */
	reg = readl(sfc->regbase + SFC_IMR);
	reg &= ~mask;
	writel(reg, sfc->regbase + SFC_IMR);
}

static void rockchip_sfc_irq_mask(struct rockchip_sfc *sfc, u32 mask)
{
	u32 reg;

	/* Disable transfer finish interrupt */
	reg = readl(sfc->regbase + SFC_IMR);
	reg |= mask;
	writel(reg, sfc->regbase + SFC_IMR);
}

static int rockchip_sfc_init(struct rockchip_sfc *sfc)
{
	writel(0, sfc->regbase + SFC_CTRL);
	writel(0xFFFFFFFF, sfc->regbase + SFC_ICLR);
	rockchip_sfc_irq_mask(sfc, 0xFFFFFFFF);
	if (rockchip_sfc_get_version(sfc) >= SFC_VER_4)
		writel(SFC_LEN_CTRL_TRB_SEL, sfc->regbase + SFC_LEN_CTRL);

	return 0;
}

static int rockchip_sfc_wait_txfifo_ready(struct rockchip_sfc *sfc, u32 timeout_us)
{
	int ret = 0;
	u32 status;

	ret = readl_poll_timeout(sfc->regbase + SFC_FSR, status,
				 status & SFC_FSR_TXLV_MASK, 0,
				 timeout_us);
	if (ret) {
		dev_dbg(sfc->dev, "sfc wait tx fifo timeout\n");

		return -ETIMEDOUT;
	}

	return (status & SFC_FSR_TXLV_MASK) >> SFC_FSR_TXLV_SHIFT;
}

static int rockchip_sfc_wait_rxfifo_ready(struct rockchip_sfc *sfc, u32 timeout_us)
{
	int ret = 0;
	u32 status;

	ret = readl_poll_timeout(sfc->regbase + SFC_FSR, status,
				 status & SFC_FSR_RXLV_MASK, 0,
				 timeout_us);
	if (ret) {
		dev_dbg(sfc->dev, "sfc wait rx fifo timeout\n");

		return -ETIMEDOUT;
	}

	return (status & SFC_FSR_RXLV_MASK) >> SFC_FSR_RXLV_SHIFT;
}

static void rockchip_sfc_adjust_op_work(struct spi_mem_op *op)
{
	if (unlikely(op->dummy.nbytes && !op->addr.nbytes)) {
		/*
		 * SFC not support output DUMMY cycles right after CMD cycles, so
		 * treat it as ADDR cycles.
		 */
		op->addr.nbytes = op->dummy.nbytes;
		op->addr.buswidth = op->dummy.buswidth;
		op->addr.val = 0xFFFFFFFFF;

		op->dummy.nbytes = 0;
	}
}

static int rockchip_sfc_xfer_setup(struct rockchip_sfc *sfc,
				   struct spi_mem *mem,
				   const struct spi_mem_op *op,
				   u32 len)
{
	u32 ctrl = 0, cmd = 0;
	u8 cs = spi_get_chipselect(mem->spi, 0);

	/* set CMD */
	cmd = op->cmd.opcode;
	ctrl |= ((op->cmd.buswidth >> 1) << SFC_CTRL_CMD_BITS_SHIFT);

	/* set ADDR */
	if (op->addr.nbytes) {
		if (op->addr.nbytes == 4) {
			cmd |= SFC_CMD_ADDR_32BITS << SFC_CMD_ADDR_SHIFT;
		} else if (op->addr.nbytes == 3) {
			cmd |= SFC_CMD_ADDR_24BITS << SFC_CMD_ADDR_SHIFT;
		} else {
			cmd |= SFC_CMD_ADDR_XBITS << SFC_CMD_ADDR_SHIFT;
			writel(op->addr.nbytes * 8 - 1,
			       sfc->regbase + cs * SFC_CS1_REG_OFFSET + SFC_ABIT);
		}

		ctrl |= ((op->addr.buswidth >> 1) << SFC_CTRL_ADDR_BITS_SHIFT);
	}

	/* set DUMMY */
	if (op->dummy.nbytes) {
		if (op->dummy.buswidth == 4)
			cmd |= op->dummy.nbytes * 2 << SFC_CMD_DUMMY_SHIFT;
		else if (op->dummy.buswidth == 2)
			cmd |= op->dummy.nbytes * 4 << SFC_CMD_DUMMY_SHIFT;
		else
			cmd |= op->dummy.nbytes * 8 << SFC_CMD_DUMMY_SHIFT;
	}

	/* set DATA */
	if (sfc->version >= SFC_VER_4) /* Clear it if no data to transfer */
		writel(len, sfc->regbase + SFC_LEN_EXT);
	else
		cmd |= len << SFC_CMD_TRAN_BYTES_SHIFT;
	if (len) {
		if (op->data.dir == SPI_MEM_DATA_OUT)
			cmd |= SFC_CMD_DIR_WR << SFC_CMD_DIR_SHIFT;

		ctrl |= ((op->data.buswidth >> 1) << SFC_CTRL_DATA_BITS_SHIFT);
	}
	if (!len && op->addr.nbytes)
		cmd |= SFC_CMD_DIR_WR << SFC_CMD_DIR_SHIFT;

	/* set the Controller */
	ctrl |= SFC_CTRL_PHASE_SEL_NEGETIVE;
	cmd |= cs << SFC_CMD_CS_SHIFT;

	dev_dbg(sfc->dev, "sfc addr.nbytes=%x(x%d) dummy.nbytes=%x(x%d)\n",
		op->addr.nbytes, op->addr.buswidth,
		op->dummy.nbytes, op->dummy.buswidth);
	dev_dbg(sfc->dev, "sfc ctrl=%x cmd=%x addr=%llx len=%x\n",
		ctrl, cmd, op->addr.val, len);

	writel(ctrl, sfc->regbase + cs * SFC_CS1_REG_OFFSET + SFC_CTRL);
	writel(cmd, sfc->regbase + SFC_CMD);
	if (op->addr.nbytes)
		writel(op->addr.val, sfc->regbase + SFC_ADDR);

	return 0;
}

static int rockchip_sfc_write_fifo(struct rockchip_sfc *sfc, const u8 *buf, int len)
{
	u8 bytes = len & 0x3;
	u32 dwords;
	int tx_level;
	u32 write_words;
	u32 tmp = 0;

	dwords = len >> 2;
	while (dwords) {
		tx_level = rockchip_sfc_wait_txfifo_ready(sfc, 1000);
		if (tx_level < 0)
			return tx_level;
		write_words = min_t(u32, tx_level, dwords);
		iowrite32_rep(sfc->regbase + SFC_DATA, buf, write_words);
		buf += write_words << 2;
		dwords -= write_words;
	}

	/* write the rest non word aligned bytes */
	if (bytes) {
		tx_level = rockchip_sfc_wait_txfifo_ready(sfc, 1000);
		if (tx_level < 0)
			return tx_level;
		memcpy(&tmp, buf, bytes);
		writel(tmp, sfc->regbase + SFC_DATA);
	}

	return len;
}

static int rockchip_sfc_read_fifo(struct rockchip_sfc *sfc, u8 *buf, int len)
{
	u8 bytes = len & 0x3;
	u32 dwords;
	u8 read_words;
	int rx_level;
	int tmp;

	/* word aligned access only */
	dwords = len >> 2;
	while (dwords) {
		rx_level = rockchip_sfc_wait_rxfifo_ready(sfc, 1000);
		if (rx_level < 0)
			return rx_level;
		read_words = min_t(u32, rx_level, dwords);
		ioread32_rep(sfc->regbase + SFC_DATA, buf, read_words);
		buf += read_words << 2;
		dwords -= read_words;
	}

	/* read the rest non word aligned bytes */
	if (bytes) {
		rx_level = rockchip_sfc_wait_rxfifo_ready(sfc, 1000);
		if (rx_level < 0)
			return rx_level;
		tmp = readl(sfc->regbase + SFC_DATA);
		memcpy(buf, &tmp, bytes);
	}

	return len;
}

static int rockchip_sfc_fifo_transfer_dma(struct rockchip_sfc *sfc, dma_addr_t dma_buf, size_t len)
{
	writel(0xFFFFFFFF, sfc->regbase + SFC_ICLR);
	writel((u32)dma_buf, sfc->regbase + SFC_DMA_ADDR);
	writel(SFC_DMA_TRIGGER_START, sfc->regbase + SFC_DMA_TRIGGER);

	return len;
}

static int rockchip_sfc_xfer_data_poll(struct rockchip_sfc *sfc,
				       const struct spi_mem_op *op, u32 len)
{
	dev_dbg(sfc->dev, "sfc xfer_poll len=%x\n", len);

	if (op->data.dir == SPI_MEM_DATA_OUT)
		return rockchip_sfc_write_fifo(sfc, op->data.buf.out, len);
	else
		return rockchip_sfc_read_fifo(sfc, op->data.buf.in, len);
}

static int rockchip_sfc_xfer_data_dma(struct rockchip_sfc *sfc,
				      const struct spi_mem_op *op, u32 len)
{
	int ret;

	dev_dbg(sfc->dev, "sfc xfer_dma len=%x\n", len);

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		memcpy(sfc->buffer, op->data.buf.out, len);
		dma_sync_single_for_device(sfc->dev, sfc->dma_buffer, len, DMA_TO_DEVICE);
	}

	ret = rockchip_sfc_fifo_transfer_dma(sfc, sfc->dma_buffer, len);
	if (!wait_for_completion_timeout(&sfc->cp, msecs_to_jiffies(2000))) {
		dev_err(sfc->dev, "DMA wait for transfer finish timeout\n");
		ret = -ETIMEDOUT;
	}
	rockchip_sfc_irq_mask(sfc, SFC_IMR_DMA);

	if (op->data.dir == SPI_MEM_DATA_IN) {
		dma_sync_single_for_cpu(sfc->dev, sfc->dma_buffer, len, DMA_FROM_DEVICE);
		memcpy(op->data.buf.in, sfc->buffer, len);
	}

	return ret;
}

static int rockchip_sfc_xfer_done(struct rockchip_sfc *sfc, u32 timeout_us)
{
	int ret = 0;
	u32 status;

	/*
	 * There is very little data left in fifo, and the controller will
	 * complete the transmission in a short period of time.
	 */
	ret = readl_poll_timeout(sfc->regbase + SFC_SR, status,
				 !(status & SFC_SR_IS_BUSY),
				 0, 10);
	if (!ret)
		return 0;

	ret = readl_poll_timeout(sfc->regbase + SFC_SR, status,
				 !(status & SFC_SR_IS_BUSY),
				 20, timeout_us);
	if (ret) {
		dev_err(sfc->dev, "wait sfc idle timeout\n");
		rockchip_sfc_reset(sfc);

		ret = -EIO;
	}

	return ret;
}

static int rockchip_sfc_exec_mem_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct rockchip_sfc *sfc = spi_controller_get_devdata(mem->spi->controller);
	u32 len = op->data.nbytes;
	int ret;
	u8 cs = spi_get_chipselect(mem->spi, 0);

	ret = pm_runtime_get_sync(sfc->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(sfc->dev);
		return ret;
	}

	if (unlikely(op->max_freq != sfc->speed[cs]) &&
	    !has_acpi_companion(sfc->dev)) {
		ret = rockchip_sfc_clk_set_rate(sfc, op->max_freq);
		if (ret)
			goto out;
		sfc->speed[cs] = op->max_freq;
		dev_dbg(sfc->dev, "set_freq=%dHz real_freq=%ldHz\n",
			sfc->speed[cs], rockchip_sfc_clk_get_rate(sfc));
	}

	rockchip_sfc_adjust_op_work((struct spi_mem_op *)op);
	rockchip_sfc_xfer_setup(sfc, mem, op, len);
	if (len) {
		if (likely(sfc->use_dma) && len >= SFC_DMA_TRANS_THRETHOLD && !(len & 0x3)) {
			init_completion(&sfc->cp);
			rockchip_sfc_irq_unmask(sfc, SFC_IMR_DMA);
			ret = rockchip_sfc_xfer_data_dma(sfc, op, len);
		} else {
			ret = rockchip_sfc_xfer_data_poll(sfc, op, len);
		}

		if (ret != len) {
			dev_err(sfc->dev, "xfer data failed ret %d dir %d\n", ret, op->data.dir);

			ret = -EIO;
			goto out;
		}
	}

	ret = rockchip_sfc_xfer_done(sfc, 100000);
out:
	pm_runtime_put_autosuspend(sfc->dev);

	return ret;
}

static int rockchip_sfc_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct rockchip_sfc *sfc = spi_controller_get_devdata(mem->spi->controller);

	op->data.nbytes = min(op->data.nbytes, sfc->max_iosize);

	return 0;
}

static const struct spi_controller_mem_ops rockchip_sfc_mem_ops = {
	.exec_op = rockchip_sfc_exec_mem_op,
	.adjust_op_size = rockchip_sfc_adjust_op_size,
};

static const struct spi_controller_mem_caps rockchip_sfc_mem_caps = {
	.per_op_freq = true,
};

static irqreturn_t rockchip_sfc_irq_handler(int irq, void *dev_id)
{
	struct rockchip_sfc *sfc = dev_id;
	u32 reg;

	reg = readl(sfc->regbase + SFC_RISR);

	/* Clear interrupt */
	writel_relaxed(reg, sfc->regbase + SFC_ICLR);

	if (reg & SFC_RISR_DMA) {
		complete(&sfc->cp);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int rockchip_sfc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *host;
	struct rockchip_sfc *sfc;
	int ret;
	u32 i, val;

	host = devm_spi_alloc_host(&pdev->dev, sizeof(*sfc));
	if (!host)
		return -ENOMEM;

	host->flags = SPI_CONTROLLER_HALF_DUPLEX;
	host->mem_ops = &rockchip_sfc_mem_ops;
	host->mem_caps = &rockchip_sfc_mem_caps;
	host->dev.of_node = pdev->dev.of_node;
	host->mode_bits = SPI_TX_QUAD | SPI_TX_DUAL | SPI_RX_QUAD | SPI_RX_DUAL;
	host->max_speed_hz = SFC_MAX_SPEED;
	host->num_chipselect = SFC_MAX_CHIPSELECT_NUM;

	sfc = spi_controller_get_devdata(host);
	sfc->dev = dev;
	sfc->host = host;

	sfc->regbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sfc->regbase))
		return PTR_ERR(sfc->regbase);

	if (!has_acpi_companion(&pdev->dev))
		sfc->clk = devm_clk_get(&pdev->dev, "clk_sfc");
	if (IS_ERR(sfc->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(sfc->clk),
				     "Failed to get sfc interface clk\n");

	if (!has_acpi_companion(&pdev->dev))
		sfc->hclk = devm_clk_get(&pdev->dev, "hclk_sfc");
	if (IS_ERR(sfc->hclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(sfc->hclk),
				     "Failed to get sfc ahb clk\n");

	if (has_acpi_companion(&pdev->dev)) {
		ret = device_property_read_u32(&pdev->dev, "clock-frequency", &val);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "Failed to find clock-frequency in ACPI\n");
		for (i = 0; i < SFC_MAX_CHIPSELECT_NUM; i++)
			sfc->speed[i] = val;
	}

	sfc->use_dma = !of_property_read_bool(sfc->dev->of_node, "rockchip,sfc-no-dma");

	ret = clk_prepare_enable(sfc->hclk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable ahb clk\n");
		goto err_hclk;
	}

	ret = clk_prepare_enable(sfc->clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable interface clk\n");
		goto err_clk;
	}

	/* Find the irq */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_irq;

	ret = devm_request_irq(dev, ret, rockchip_sfc_irq_handler,
			       0, pdev->name, sfc);
	if (ret) {
		dev_err(dev, "Failed to request irq\n");
		goto err_irq;
	}

	platform_set_drvdata(pdev, sfc);

	ret = rockchip_sfc_init(sfc);
	if (ret)
		goto err_irq;

	sfc->version = rockchip_sfc_get_version(sfc);
	sfc->max_iosize = rockchip_sfc_get_max_iosize(sfc);

	pm_runtime_set_autosuspend_delay(dev, ROCKCHIP_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_noresume(dev);

	if (sfc->use_dma) {
		sfc->buffer = (u8 *)__get_free_pages(GFP_KERNEL | GFP_DMA32,
						     get_order(sfc->max_iosize));
		if (!sfc->buffer) {
			ret = -ENOMEM;
			goto err_dma;
		}
		sfc->dma_buffer = dma_map_single(dev, sfc->buffer,
					    sfc->max_iosize, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, sfc->dma_buffer)) {
			ret = -ENOMEM;
			goto err_dma_map;
		}
	}

	ret = devm_spi_register_controller(dev, host);
	if (ret)
		goto err_register;

	pm_runtime_put_autosuspend(dev);

	return 0;
err_register:
	dma_unmap_single(dev, sfc->dma_buffer, sfc->max_iosize,
			 DMA_BIDIRECTIONAL);
err_dma_map:
	free_pages((unsigned long)sfc->buffer, get_order(sfc->max_iosize));
err_dma:
	pm_runtime_get_sync(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_dont_use_autosuspend(dev);
err_irq:
	clk_disable_unprepare(sfc->clk);
err_clk:
	clk_disable_unprepare(sfc->hclk);
err_hclk:
	return ret;
}

static void rockchip_sfc_remove(struct platform_device *pdev)
{
	struct rockchip_sfc *sfc = platform_get_drvdata(pdev);
	struct spi_controller *host = sfc->host;

	spi_unregister_controller(host);
	dma_unmap_single(&pdev->dev, sfc->dma_buffer, sfc->max_iosize,
			 DMA_BIDIRECTIONAL);
	free_pages((unsigned long)sfc->buffer, get_order(sfc->max_iosize));

	clk_disable_unprepare(sfc->clk);
	clk_disable_unprepare(sfc->hclk);
}

#ifdef CONFIG_PM
static int rockchip_sfc_runtime_suspend(struct device *dev)
{
	struct rockchip_sfc *sfc = dev_get_drvdata(dev);

	clk_disable_unprepare(sfc->clk);
	clk_disable_unprepare(sfc->hclk);

	return 0;
}

static int rockchip_sfc_runtime_resume(struct device *dev)
{
	struct rockchip_sfc *sfc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(sfc->hclk);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(sfc->clk);
	if (ret < 0)
		clk_disable_unprepare(sfc->hclk);

	return ret;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
static int rockchip_sfc_suspend(struct device *dev)
{
	pinctrl_pm_select_sleep_state(dev);

	return pm_runtime_force_suspend(dev);
}

static int rockchip_sfc_resume(struct device *dev)
{
	struct rockchip_sfc *sfc = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	pinctrl_pm_select_default_state(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	rockchip_sfc_init(sfc);

	pm_runtime_put_autosuspend(dev);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops rockchip_sfc_pm_ops = {
	SET_RUNTIME_PM_OPS(rockchip_sfc_runtime_suspend,
			   rockchip_sfc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_sfc_suspend, rockchip_sfc_resume)
};

static const struct of_device_id rockchip_sfc_dt_ids[] = {
	{ .compatible = "rockchip,sfc"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rockchip_sfc_dt_ids);

static struct platform_driver rockchip_sfc_driver = {
	.driver = {
		.name	= "rockchip-sfc",
		.of_match_table = rockchip_sfc_dt_ids,
		.pm = &rockchip_sfc_pm_ops,
	},
	.probe	= rockchip_sfc_probe,
	.remove = rockchip_sfc_remove,
};
module_platform_driver(rockchip_sfc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Rockchip Serial Flash Controller Driver");
MODULE_AUTHOR("Shawn Lin <shawn.lin@rock-chips.com>");
MODULE_AUTHOR("Chris Morgan <macromorgan@hotmail.com>");
MODULE_AUTHOR("Jon Lin <Jon.lin@rock-chips.com>");
