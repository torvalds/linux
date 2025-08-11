// SPDX-License-Identifier: GPL-2.0
//
// Mediatek SPI NOR controller driver
//
// Copyright (C) 2020 Chuanhong Guo <gch981213@gmail.com>

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/string.h>

#define DRIVER_NAME "mtk-spi-nor"

#define MTK_NOR_REG_CMD			0x00
#define MTK_NOR_CMD_WRITE		BIT(4)
#define MTK_NOR_CMD_PROGRAM		BIT(2)
#define MTK_NOR_CMD_READ		BIT(0)
#define MTK_NOR_CMD_MASK		GENMASK(5, 0)

#define MTK_NOR_REG_PRG_CNT		0x04
#define MTK_NOR_PRG_CNT_MAX		56
#define MTK_NOR_REG_RDATA		0x0c

#define MTK_NOR_REG_RADR0		0x10
#define MTK_NOR_REG_RADR(n)		(MTK_NOR_REG_RADR0 + 4 * (n))
#define MTK_NOR_REG_RADR3		0xc8

#define MTK_NOR_REG_WDATA		0x1c

#define MTK_NOR_REG_PRGDATA0		0x20
#define MTK_NOR_REG_PRGDATA(n)		(MTK_NOR_REG_PRGDATA0 + 4 * (n))
#define MTK_NOR_REG_PRGDATA_MAX		5

#define MTK_NOR_REG_SHIFT0		0x38
#define MTK_NOR_REG_SHIFT(n)		(MTK_NOR_REG_SHIFT0 + 4 * (n))
#define MTK_NOR_REG_SHIFT_MAX		9

#define MTK_NOR_REG_CFG1		0x60
#define MTK_NOR_FAST_READ		BIT(0)

#define MTK_NOR_REG_CFG2		0x64
#define MTK_NOR_WR_CUSTOM_OP_EN		BIT(4)
#define MTK_NOR_WR_BUF_EN		BIT(0)

#define MTK_NOR_REG_PP_DATA		0x98

#define MTK_NOR_REG_IRQ_STAT		0xa8
#define MTK_NOR_REG_IRQ_EN		0xac
#define MTK_NOR_IRQ_DMA			BIT(7)
#define MTK_NOR_IRQ_MASK		GENMASK(7, 0)

#define MTK_NOR_REG_CFG3		0xb4
#define MTK_NOR_DISABLE_WREN		BIT(7)
#define MTK_NOR_DISABLE_SR_POLL		BIT(5)

#define MTK_NOR_REG_WP			0xc4
#define MTK_NOR_ENABLE_SF_CMD		0x30

#define MTK_NOR_REG_BUSCFG		0xcc
#define MTK_NOR_4B_ADDR			BIT(4)
#define MTK_NOR_QUAD_ADDR		BIT(3)
#define MTK_NOR_QUAD_READ		BIT(2)
#define MTK_NOR_DUAL_ADDR		BIT(1)
#define MTK_NOR_DUAL_READ		BIT(0)
#define MTK_NOR_BUS_MODE_MASK		GENMASK(4, 0)

#define MTK_NOR_REG_DMA_CTL		0x718
#define MTK_NOR_DMA_START		BIT(0)

#define MTK_NOR_REG_DMA_FADR		0x71c
#define MTK_NOR_REG_DMA_DADR		0x720
#define MTK_NOR_REG_DMA_END_DADR	0x724
#define MTK_NOR_REG_CG_DIS		0x728
#define MTK_NOR_SFC_SW_RST		BIT(2)

#define MTK_NOR_REG_DMA_DADR_HB		0x738
#define MTK_NOR_REG_DMA_END_DADR_HB	0x73c

#define MTK_NOR_PRG_MAX_SIZE		6
// Reading DMA src/dst addresses have to be 16-byte aligned
#define MTK_NOR_DMA_ALIGN		16
#define MTK_NOR_DMA_ALIGN_MASK		(MTK_NOR_DMA_ALIGN - 1)
// and we allocate a bounce buffer if destination address isn't aligned.
#define MTK_NOR_BOUNCE_BUF_SIZE		PAGE_SIZE

// Buffered page program can do one 128-byte transfer
#define MTK_NOR_PP_SIZE			128

#define CLK_TO_US(sp, clkcnt)		DIV_ROUND_UP(clkcnt, sp->spi_freq / 1000000)

struct mtk_nor_caps {
	u8 dma_bits;

	/* extra_dummy_bit is adding for the IP of new SoCs.
	 * Some new SoCs modify the timing of fetching registers' values
	 * and IDs of nor flash, they need a extra_dummy_bit which can add
	 * more clock cycles for fetching data.
	 */
	u8 extra_dummy_bit;
};

struct mtk_nor {
	struct spi_controller *ctlr;
	struct device *dev;
	void __iomem *base;
	u8 *buffer;
	dma_addr_t buffer_dma;
	struct clk *spi_clk;
	struct clk *ctlr_clk;
	struct clk *axi_clk;
	struct clk *axi_s_clk;
	unsigned int spi_freq;
	bool wbuf_en;
	bool has_irq;
	bool high_dma;
	struct completion op_done;
	const struct mtk_nor_caps *caps;
};

static inline void mtk_nor_rmw(struct mtk_nor *sp, u32 reg, u32 set, u32 clr)
{
	u32 val = readl(sp->base + reg);

	val &= ~clr;
	val |= set;
	writel(val, sp->base + reg);
}

static inline int mtk_nor_cmd_exec(struct mtk_nor *sp, u32 cmd, ulong clk)
{
	ulong delay = CLK_TO_US(sp, clk);
	u32 reg;
	int ret;

	writel(cmd, sp->base + MTK_NOR_REG_CMD);
	ret = readl_poll_timeout(sp->base + MTK_NOR_REG_CMD, reg, !(reg & cmd),
				 delay / 3, (delay + 1) * 200);
	if (ret < 0)
		dev_err(sp->dev, "command %u timeout.\n", cmd);
	return ret;
}

static void mtk_nor_reset(struct mtk_nor *sp)
{
	mtk_nor_rmw(sp, MTK_NOR_REG_CG_DIS, 0, MTK_NOR_SFC_SW_RST);
	mb(); /* flush previous writes */
	mtk_nor_rmw(sp, MTK_NOR_REG_CG_DIS, MTK_NOR_SFC_SW_RST, 0);
	mb(); /* flush previous writes */
	writel(MTK_NOR_ENABLE_SF_CMD, sp->base + MTK_NOR_REG_WP);
}

static void mtk_nor_set_addr(struct mtk_nor *sp, const struct spi_mem_op *op)
{
	u32 addr = op->addr.val;
	int i;

	for (i = 0; i < 3; i++) {
		writeb(addr & 0xff, sp->base + MTK_NOR_REG_RADR(i));
		addr >>= 8;
	}
	if (op->addr.nbytes == 4) {
		writeb(addr & 0xff, sp->base + MTK_NOR_REG_RADR3);
		mtk_nor_rmw(sp, MTK_NOR_REG_BUSCFG, MTK_NOR_4B_ADDR, 0);
	} else {
		mtk_nor_rmw(sp, MTK_NOR_REG_BUSCFG, 0, MTK_NOR_4B_ADDR);
	}
}

static bool need_bounce(struct mtk_nor *sp, const struct spi_mem_op *op)
{
	return ((uintptr_t)op->data.buf.in & MTK_NOR_DMA_ALIGN_MASK);
}

static bool mtk_nor_match_read(const struct spi_mem_op *op)
{
	int dummy = 0;

	if (op->dummy.nbytes)
		dummy = op->dummy.nbytes * BITS_PER_BYTE / op->dummy.buswidth;

	if ((op->data.buswidth == 2) || (op->data.buswidth == 4)) {
		if (op->addr.buswidth == 1)
			return dummy == 8;
		else if (op->addr.buswidth == 2)
			return dummy == 4;
		else if (op->addr.buswidth == 4)
			return dummy == 6;
	} else if ((op->addr.buswidth == 1) && (op->data.buswidth == 1)) {
		if (op->cmd.opcode == 0x03)
			return dummy == 0;
		else if (op->cmd.opcode == 0x0b)
			return dummy == 8;
	}
	return false;
}

static bool mtk_nor_match_prg(const struct spi_mem_op *op)
{
	int tx_len, rx_len, prg_len, prg_left;

	// prg mode is spi-only.
	if ((op->cmd.buswidth > 1) || (op->addr.buswidth > 1) ||
	    (op->dummy.buswidth > 1) || (op->data.buswidth > 1))
		return false;

	tx_len = op->cmd.nbytes + op->addr.nbytes;

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		// count dummy bytes only if we need to write data after it
		tx_len += op->dummy.nbytes;

		// leave at least one byte for data
		if (tx_len > MTK_NOR_REG_PRGDATA_MAX)
			return false;

		// if there's no addr, meaning adjust_op_size is impossible,
		// check data length as well.
		if ((!op->addr.nbytes) &&
		    (tx_len + op->data.nbytes > MTK_NOR_REG_PRGDATA_MAX + 1))
			return false;
	} else if (op->data.dir == SPI_MEM_DATA_IN) {
		if (tx_len > MTK_NOR_REG_PRGDATA_MAX + 1)
			return false;

		rx_len = op->data.nbytes;
		prg_left = MTK_NOR_PRG_CNT_MAX / 8 - tx_len - op->dummy.nbytes;
		if (prg_left > MTK_NOR_REG_SHIFT_MAX + 1)
			prg_left = MTK_NOR_REG_SHIFT_MAX + 1;
		if (rx_len > prg_left) {
			if (!op->addr.nbytes)
				return false;
			rx_len = prg_left;
		}

		prg_len = tx_len + op->dummy.nbytes + rx_len;
		if (prg_len > MTK_NOR_PRG_CNT_MAX / 8)
			return false;
	} else {
		prg_len = tx_len + op->dummy.nbytes;
		if (prg_len > MTK_NOR_PRG_CNT_MAX / 8)
			return false;
	}
	return true;
}

static void mtk_nor_adj_prg_size(struct spi_mem_op *op)
{
	int tx_len, tx_left, prg_left;

	tx_len = op->cmd.nbytes + op->addr.nbytes;
	if (op->data.dir == SPI_MEM_DATA_OUT) {
		tx_len += op->dummy.nbytes;
		tx_left = MTK_NOR_REG_PRGDATA_MAX + 1 - tx_len;
		if (op->data.nbytes > tx_left)
			op->data.nbytes = tx_left;
	} else if (op->data.dir == SPI_MEM_DATA_IN) {
		prg_left = MTK_NOR_PRG_CNT_MAX / 8 - tx_len - op->dummy.nbytes;
		if (prg_left > MTK_NOR_REG_SHIFT_MAX + 1)
			prg_left = MTK_NOR_REG_SHIFT_MAX + 1;
		if (op->data.nbytes > prg_left)
			op->data.nbytes = prg_left;
	}
}

static int mtk_nor_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct mtk_nor *sp = spi_controller_get_devdata(mem->spi->controller);

	if (!op->data.nbytes)
		return 0;

	if ((op->addr.nbytes == 3) || (op->addr.nbytes == 4)) {
		if ((op->data.dir == SPI_MEM_DATA_IN) &&
		    mtk_nor_match_read(op)) {
			// limit size to prevent timeout calculation overflow
			if (op->data.nbytes > 0x400000)
				op->data.nbytes = 0x400000;

			if ((op->addr.val & MTK_NOR_DMA_ALIGN_MASK) ||
			    (op->data.nbytes < MTK_NOR_DMA_ALIGN))
				op->data.nbytes = 1;
			else if (!need_bounce(sp, op))
				op->data.nbytes &= ~MTK_NOR_DMA_ALIGN_MASK;
			else if (op->data.nbytes > MTK_NOR_BOUNCE_BUF_SIZE)
				op->data.nbytes = MTK_NOR_BOUNCE_BUF_SIZE;
			return 0;
		} else if (op->data.dir == SPI_MEM_DATA_OUT) {
			if (op->data.nbytes >= MTK_NOR_PP_SIZE)
				op->data.nbytes = MTK_NOR_PP_SIZE;
			else
				op->data.nbytes = 1;
			return 0;
		}
	}

	mtk_nor_adj_prg_size(op);
	return 0;
}

static bool mtk_nor_supports_op(struct spi_mem *mem,
				const struct spi_mem_op *op)
{
	if (!spi_mem_default_supports_op(mem, op))
		return false;

	if (op->cmd.buswidth != 1)
		return false;

	if ((op->addr.nbytes == 3) || (op->addr.nbytes == 4)) {
		switch (op->data.dir) {
		case SPI_MEM_DATA_IN:
			if (mtk_nor_match_read(op))
				return true;
			break;
		case SPI_MEM_DATA_OUT:
			if ((op->addr.buswidth == 1) &&
			    (op->dummy.nbytes == 0) &&
			    (op->data.buswidth == 1))
				return true;
			break;
		default:
			break;
		}
	}

	return mtk_nor_match_prg(op);
}

static void mtk_nor_setup_bus(struct mtk_nor *sp, const struct spi_mem_op *op)
{
	u32 reg = 0;

	if (op->addr.nbytes == 4)
		reg |= MTK_NOR_4B_ADDR;

	if (op->data.buswidth == 4) {
		reg |= MTK_NOR_QUAD_READ;
		writeb(op->cmd.opcode, sp->base + MTK_NOR_REG_PRGDATA(4));
		if (op->addr.buswidth == 4)
			reg |= MTK_NOR_QUAD_ADDR;
	} else if (op->data.buswidth == 2) {
		reg |= MTK_NOR_DUAL_READ;
		writeb(op->cmd.opcode, sp->base + MTK_NOR_REG_PRGDATA(3));
		if (op->addr.buswidth == 2)
			reg |= MTK_NOR_DUAL_ADDR;
	} else {
		if (op->cmd.opcode == 0x0b)
			mtk_nor_rmw(sp, MTK_NOR_REG_CFG1, MTK_NOR_FAST_READ, 0);
		else
			mtk_nor_rmw(sp, MTK_NOR_REG_CFG1, 0, MTK_NOR_FAST_READ);
	}
	mtk_nor_rmw(sp, MTK_NOR_REG_BUSCFG, reg, MTK_NOR_BUS_MODE_MASK);
}

static int mtk_nor_dma_exec(struct mtk_nor *sp, u32 from, unsigned int length,
			    dma_addr_t dma_addr)
{
	int ret = 0;
	u32 delay, timeout;
	u32 reg;

	writel(from, sp->base + MTK_NOR_REG_DMA_FADR);
	writel(dma_addr, sp->base + MTK_NOR_REG_DMA_DADR);
	writel(dma_addr + length, sp->base + MTK_NOR_REG_DMA_END_DADR);

	if (sp->high_dma) {
		writel(upper_32_bits(dma_addr),
		       sp->base + MTK_NOR_REG_DMA_DADR_HB);
		writel(upper_32_bits(dma_addr + length),
		       sp->base + MTK_NOR_REG_DMA_END_DADR_HB);
	}

	if (sp->has_irq) {
		reinit_completion(&sp->op_done);
		mtk_nor_rmw(sp, MTK_NOR_REG_IRQ_EN, MTK_NOR_IRQ_DMA, 0);
	}

	mtk_nor_rmw(sp, MTK_NOR_REG_DMA_CTL, MTK_NOR_DMA_START, 0);

	delay = CLK_TO_US(sp, (length + 5) * BITS_PER_BYTE);
	timeout = (delay + 1) * 100;

	if (sp->has_irq) {
		if (!wait_for_completion_timeout(&sp->op_done,
		    usecs_to_jiffies(max(timeout, 10000U))))
			ret = -ETIMEDOUT;
	} else {
		ret = readl_poll_timeout(sp->base + MTK_NOR_REG_DMA_CTL, reg,
					 !(reg & MTK_NOR_DMA_START), delay / 3,
					 timeout);
	}

	if (ret < 0)
		dev_err(sp->dev, "dma read timeout.\n");

	return ret;
}

static int mtk_nor_read_bounce(struct mtk_nor *sp, const struct spi_mem_op *op)
{
	unsigned int rdlen;
	int ret;

	if (op->data.nbytes & MTK_NOR_DMA_ALIGN_MASK)
		rdlen = (op->data.nbytes + MTK_NOR_DMA_ALIGN) & ~MTK_NOR_DMA_ALIGN_MASK;
	else
		rdlen = op->data.nbytes;

	ret = mtk_nor_dma_exec(sp, op->addr.val, rdlen, sp->buffer_dma);

	if (!ret)
		memcpy(op->data.buf.in, sp->buffer, op->data.nbytes);

	return ret;
}

static int mtk_nor_read_dma(struct mtk_nor *sp, const struct spi_mem_op *op)
{
	int ret;
	dma_addr_t dma_addr;

	if (need_bounce(sp, op))
		return mtk_nor_read_bounce(sp, op);

	dma_addr = dma_map_single(sp->dev, op->data.buf.in,
				  op->data.nbytes, DMA_FROM_DEVICE);

	if (dma_mapping_error(sp->dev, dma_addr))
		return -EINVAL;

	ret = mtk_nor_dma_exec(sp, op->addr.val, op->data.nbytes, dma_addr);

	dma_unmap_single(sp->dev, dma_addr, op->data.nbytes, DMA_FROM_DEVICE);

	return ret;
}

static int mtk_nor_read_pio(struct mtk_nor *sp, const struct spi_mem_op *op)
{
	u8 *buf = op->data.buf.in;
	int ret;

	ret = mtk_nor_cmd_exec(sp, MTK_NOR_CMD_READ, 6 * BITS_PER_BYTE);
	if (!ret)
		buf[0] = readb(sp->base + MTK_NOR_REG_RDATA);
	return ret;
}

static int mtk_nor_setup_write_buffer(struct mtk_nor *sp, bool on)
{
	int ret;
	u32 val;

	if (!(sp->wbuf_en ^ on))
		return 0;

	val = readl(sp->base + MTK_NOR_REG_CFG2);
	if (on) {
		writel(val | MTK_NOR_WR_BUF_EN, sp->base + MTK_NOR_REG_CFG2);
		ret = readl_poll_timeout(sp->base + MTK_NOR_REG_CFG2, val,
					 val & MTK_NOR_WR_BUF_EN, 0, 10000);
	} else {
		writel(val & ~MTK_NOR_WR_BUF_EN, sp->base + MTK_NOR_REG_CFG2);
		ret = readl_poll_timeout(sp->base + MTK_NOR_REG_CFG2, val,
					 !(val & MTK_NOR_WR_BUF_EN), 0, 10000);
	}

	if (!ret)
		sp->wbuf_en = on;

	return ret;
}

static int mtk_nor_pp_buffered(struct mtk_nor *sp, const struct spi_mem_op *op)
{
	const u8 *buf = op->data.buf.out;
	u32 val;
	int ret, i;

	ret = mtk_nor_setup_write_buffer(sp, true);
	if (ret < 0)
		return ret;

	for (i = 0; i < op->data.nbytes; i += 4) {
		val = buf[i + 3] << 24 | buf[i + 2] << 16 | buf[i + 1] << 8 |
		      buf[i];
		writel(val, sp->base + MTK_NOR_REG_PP_DATA);
	}
	return mtk_nor_cmd_exec(sp, MTK_NOR_CMD_WRITE,
				(op->data.nbytes + 5) * BITS_PER_BYTE);
}

static int mtk_nor_pp_unbuffered(struct mtk_nor *sp,
				 const struct spi_mem_op *op)
{
	const u8 *buf = op->data.buf.out;
	int ret;

	ret = mtk_nor_setup_write_buffer(sp, false);
	if (ret < 0)
		return ret;
	writeb(buf[0], sp->base + MTK_NOR_REG_WDATA);
	return mtk_nor_cmd_exec(sp, MTK_NOR_CMD_WRITE, 6 * BITS_PER_BYTE);
}

static int mtk_nor_spi_mem_prg(struct mtk_nor *sp, const struct spi_mem_op *op)
{
	int rx_len = 0;
	int reg_offset = MTK_NOR_REG_PRGDATA_MAX;
	int tx_len, prg_len;
	int i, ret;
	void __iomem *reg;
	u8 bufbyte;

	tx_len = op->cmd.nbytes + op->addr.nbytes;

	// count dummy bytes only if we need to write data after it
	if (op->data.dir == SPI_MEM_DATA_OUT)
		tx_len += op->dummy.nbytes + op->data.nbytes;
	else if (op->data.dir == SPI_MEM_DATA_IN)
		rx_len = op->data.nbytes;

	prg_len = op->cmd.nbytes + op->addr.nbytes + op->dummy.nbytes +
		  op->data.nbytes;

	// an invalid op may reach here if the caller calls exec_op without
	// adjust_op_size. return -EINVAL instead of -ENOTSUPP so that
	// spi-mem won't try this op again with generic spi transfers.
	if ((tx_len > MTK_NOR_REG_PRGDATA_MAX + 1) ||
	    (rx_len > MTK_NOR_REG_SHIFT_MAX + 1) ||
	    (prg_len > MTK_NOR_PRG_CNT_MAX / 8))
		return -EINVAL;

	// fill tx data
	for (i = op->cmd.nbytes; i > 0; i--, reg_offset--) {
		reg = sp->base + MTK_NOR_REG_PRGDATA(reg_offset);
		bufbyte = (op->cmd.opcode >> ((i - 1) * BITS_PER_BYTE)) & 0xff;
		writeb(bufbyte, reg);
	}

	for (i = op->addr.nbytes; i > 0; i--, reg_offset--) {
		reg = sp->base + MTK_NOR_REG_PRGDATA(reg_offset);
		bufbyte = (op->addr.val >> ((i - 1) * BITS_PER_BYTE)) & 0xff;
		writeb(bufbyte, reg);
	}

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		for (i = 0; i < op->dummy.nbytes; i++, reg_offset--) {
			reg = sp->base + MTK_NOR_REG_PRGDATA(reg_offset);
			writeb(0, reg);
		}

		for (i = 0; i < op->data.nbytes; i++, reg_offset--) {
			reg = sp->base + MTK_NOR_REG_PRGDATA(reg_offset);
			writeb(((const u8 *)(op->data.buf.out))[i], reg);
		}
	}

	for (; reg_offset >= 0; reg_offset--) {
		reg = sp->base + MTK_NOR_REG_PRGDATA(reg_offset);
		writeb(0, reg);
	}

	// trigger op
	if (rx_len)
		writel(prg_len * BITS_PER_BYTE + sp->caps->extra_dummy_bit,
		       sp->base + MTK_NOR_REG_PRG_CNT);
	else
		writel(prg_len * BITS_PER_BYTE, sp->base + MTK_NOR_REG_PRG_CNT);

	ret = mtk_nor_cmd_exec(sp, MTK_NOR_CMD_PROGRAM,
			       prg_len * BITS_PER_BYTE);
	if (ret)
		return ret;

	// fetch read data
	reg_offset = 0;
	if (op->data.dir == SPI_MEM_DATA_IN) {
		for (i = op->data.nbytes - 1; i >= 0; i--, reg_offset++) {
			reg = sp->base + MTK_NOR_REG_SHIFT(reg_offset);
			((u8 *)(op->data.buf.in))[i] = readb(reg);
		}
	}

	return 0;
}

static int mtk_nor_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct mtk_nor *sp = spi_controller_get_devdata(mem->spi->controller);
	int ret;

	if ((op->data.nbytes == 0) ||
	    ((op->addr.nbytes != 3) && (op->addr.nbytes != 4)))
		return mtk_nor_spi_mem_prg(sp, op);

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		mtk_nor_set_addr(sp, op);
		writeb(op->cmd.opcode, sp->base + MTK_NOR_REG_PRGDATA0);
		if (op->data.nbytes == MTK_NOR_PP_SIZE)
			return mtk_nor_pp_buffered(sp, op);
		return mtk_nor_pp_unbuffered(sp, op);
	}

	if ((op->data.dir == SPI_MEM_DATA_IN) && mtk_nor_match_read(op)) {
		ret = mtk_nor_setup_write_buffer(sp, false);
		if (ret < 0)
			return ret;
		mtk_nor_setup_bus(sp, op);
		if (op->data.nbytes == 1) {
			mtk_nor_set_addr(sp, op);
			return mtk_nor_read_pio(sp, op);
		} else {
			ret = mtk_nor_read_dma(sp, op);
			if (unlikely(ret)) {
				/* Handle rare bus glitch */
				mtk_nor_reset(sp);
				mtk_nor_setup_bus(sp, op);
				return mtk_nor_read_dma(sp, op);
			}

			return ret;
		}
	}

	return mtk_nor_spi_mem_prg(sp, op);
}

static int mtk_nor_setup(struct spi_device *spi)
{
	struct mtk_nor *sp = spi_controller_get_devdata(spi->controller);

	if (spi->max_speed_hz && (spi->max_speed_hz < sp->spi_freq)) {
		dev_err(&spi->dev, "spi clock should be %u Hz.\n",
			sp->spi_freq);
		return -EINVAL;
	}
	spi->max_speed_hz = sp->spi_freq;

	return 0;
}

static int mtk_nor_transfer_one_message(struct spi_controller *host,
					struct spi_message *m)
{
	struct mtk_nor *sp = spi_controller_get_devdata(host);
	struct spi_transfer *t = NULL;
	unsigned long trx_len = 0;
	int stat = 0;
	int reg_offset = MTK_NOR_REG_PRGDATA_MAX;
	void __iomem *reg;
	const u8 *txbuf;
	u8 *rxbuf;
	int i;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		txbuf = t->tx_buf;
		for (i = 0; i < t->len; i++, reg_offset--) {
			reg = sp->base + MTK_NOR_REG_PRGDATA(reg_offset);
			if (txbuf)
				writeb(txbuf[i], reg);
			else
				writeb(0, reg);
		}
		trx_len += t->len;
	}

	writel(trx_len * BITS_PER_BYTE, sp->base + MTK_NOR_REG_PRG_CNT);

	stat = mtk_nor_cmd_exec(sp, MTK_NOR_CMD_PROGRAM,
				trx_len * BITS_PER_BYTE);
	if (stat < 0)
		goto msg_done;

	reg_offset = trx_len - 1;
	list_for_each_entry(t, &m->transfers, transfer_list) {
		rxbuf = t->rx_buf;
		for (i = 0; i < t->len; i++, reg_offset--) {
			reg = sp->base + MTK_NOR_REG_SHIFT(reg_offset);
			if (rxbuf)
				rxbuf[i] = readb(reg);
		}
	}

	m->actual_length = trx_len;
msg_done:
	m->status = stat;
	spi_finalize_current_message(host);

	return 0;
}

static void mtk_nor_disable_clk(struct mtk_nor *sp)
{
	clk_disable_unprepare(sp->spi_clk);
	clk_disable_unprepare(sp->ctlr_clk);
	clk_disable_unprepare(sp->axi_clk);
	clk_disable_unprepare(sp->axi_s_clk);
}

static int mtk_nor_enable_clk(struct mtk_nor *sp)
{
	int ret;

	ret = clk_prepare_enable(sp->spi_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(sp->ctlr_clk);
	if (ret) {
		clk_disable_unprepare(sp->spi_clk);
		return ret;
	}

	ret = clk_prepare_enable(sp->axi_clk);
	if (ret) {
		clk_disable_unprepare(sp->spi_clk);
		clk_disable_unprepare(sp->ctlr_clk);
		return ret;
	}

	ret = clk_prepare_enable(sp->axi_s_clk);
	if (ret) {
		clk_disable_unprepare(sp->spi_clk);
		clk_disable_unprepare(sp->ctlr_clk);
		clk_disable_unprepare(sp->axi_clk);
		return ret;
	}

	return 0;
}

static void mtk_nor_init(struct mtk_nor *sp)
{
	writel(0, sp->base + MTK_NOR_REG_IRQ_EN);
	writel(MTK_NOR_IRQ_MASK, sp->base + MTK_NOR_REG_IRQ_STAT);

	writel(MTK_NOR_ENABLE_SF_CMD, sp->base + MTK_NOR_REG_WP);
	mtk_nor_rmw(sp, MTK_NOR_REG_CFG2, MTK_NOR_WR_CUSTOM_OP_EN, 0);
	mtk_nor_rmw(sp, MTK_NOR_REG_CFG3,
		    MTK_NOR_DISABLE_WREN | MTK_NOR_DISABLE_SR_POLL, 0);
}

static irqreturn_t mtk_nor_irq_handler(int irq, void *data)
{
	struct mtk_nor *sp = data;
	u32 irq_status, irq_enabled;

	irq_status = readl(sp->base + MTK_NOR_REG_IRQ_STAT);
	irq_enabled = readl(sp->base + MTK_NOR_REG_IRQ_EN);
	// write status back to clear interrupt
	writel(irq_status, sp->base + MTK_NOR_REG_IRQ_STAT);

	if (!(irq_status & irq_enabled))
		return IRQ_NONE;

	if (irq_status & MTK_NOR_IRQ_DMA) {
		complete(&sp->op_done);
		writel(0, sp->base + MTK_NOR_REG_IRQ_EN);
	}

	return IRQ_HANDLED;
}

static size_t mtk_max_msg_size(struct spi_device *spi)
{
	return MTK_NOR_PRG_MAX_SIZE;
}

static const struct spi_controller_mem_ops mtk_nor_mem_ops = {
	.adjust_op_size = mtk_nor_adjust_op_size,
	.supports_op = mtk_nor_supports_op,
	.exec_op = mtk_nor_exec_op
};

static const struct mtk_nor_caps mtk_nor_caps_mt8173 = {
	.dma_bits = 32,
	.extra_dummy_bit = 0,
};

static const struct mtk_nor_caps mtk_nor_caps_mt8186 = {
	.dma_bits = 32,
	.extra_dummy_bit = 1,
};

static const struct mtk_nor_caps mtk_nor_caps_mt8192 = {
	.dma_bits = 36,
	.extra_dummy_bit = 0,
};

static const struct of_device_id mtk_nor_match[] = {
	{ .compatible = "mediatek,mt8173-nor", .data = &mtk_nor_caps_mt8173 },
	{ .compatible = "mediatek,mt8186-nor", .data = &mtk_nor_caps_mt8186 },
	{ .compatible = "mediatek,mt8192-nor", .data = &mtk_nor_caps_mt8192 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_nor_match);

static int mtk_nor_probe(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	struct mtk_nor *sp;
	struct mtk_nor_caps *caps;
	void __iomem *base;
	struct clk *spi_clk, *ctlr_clk, *axi_clk, *axi_s_clk;
	int ret, irq;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	spi_clk = devm_clk_get(&pdev->dev, "spi");
	if (IS_ERR(spi_clk))
		return PTR_ERR(spi_clk);

	ctlr_clk = devm_clk_get(&pdev->dev, "sf");
	if (IS_ERR(ctlr_clk))
		return PTR_ERR(ctlr_clk);

	axi_clk = devm_clk_get_optional(&pdev->dev, "axi");
	if (IS_ERR(axi_clk))
		return PTR_ERR(axi_clk);

	axi_s_clk = devm_clk_get_optional(&pdev->dev, "axi_s");
	if (IS_ERR(axi_s_clk))
		return PTR_ERR(axi_s_clk);

	caps = (struct mtk_nor_caps *)of_device_get_match_data(&pdev->dev);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(caps->dma_bits));
	if (ret) {
		dev_err(&pdev->dev, "failed to set dma mask(%u)\n", caps->dma_bits);
		return ret;
	}

	ctlr = devm_spi_alloc_host(&pdev->dev, sizeof(*sp));
	if (!ctlr) {
		dev_err(&pdev->dev, "failed to allocate spi controller\n");
		return -ENOMEM;
	}

	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->max_message_size = mtk_max_msg_size;
	ctlr->mem_ops = &mtk_nor_mem_ops;
	ctlr->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD | SPI_TX_DUAL | SPI_TX_QUAD;
	ctlr->num_chipselect = 1;
	ctlr->setup = mtk_nor_setup;
	ctlr->transfer_one_message = mtk_nor_transfer_one_message;
	ctlr->auto_runtime_pm = true;

	dev_set_drvdata(&pdev->dev, ctlr);

	sp = spi_controller_get_devdata(ctlr);
	sp->base = base;
	sp->has_irq = false;
	sp->wbuf_en = false;
	sp->ctlr = ctlr;
	sp->dev = &pdev->dev;
	sp->spi_clk = spi_clk;
	sp->ctlr_clk = ctlr_clk;
	sp->axi_clk = axi_clk;
	sp->axi_s_clk = axi_s_clk;
	sp->caps = caps;
	sp->high_dma = caps->dma_bits > 32;
	sp->buffer = dmam_alloc_coherent(&pdev->dev,
				MTK_NOR_BOUNCE_BUF_SIZE + MTK_NOR_DMA_ALIGN,
				&sp->buffer_dma, GFP_KERNEL);
	if (!sp->buffer)
		return -ENOMEM;

	if ((uintptr_t)sp->buffer & MTK_NOR_DMA_ALIGN_MASK) {
		dev_err(sp->dev, "misaligned allocation of internal buffer.\n");
		return -ENOMEM;
	}

	ret = mtk_nor_enable_clk(sp);
	if (ret < 0)
		return ret;

	sp->spi_freq = clk_get_rate(sp->spi_clk);

	mtk_nor_init(sp);

	irq = platform_get_irq_optional(pdev, 0);

	if (irq < 0) {
		dev_warn(sp->dev, "IRQ not available.");
	} else {
		ret = devm_request_irq(sp->dev, irq, mtk_nor_irq_handler, 0,
				       pdev->name, sp);
		if (ret < 0) {
			dev_warn(sp->dev, "failed to request IRQ.");
		} else {
			init_completion(&sp->op_done);
			sp->has_irq = true;
		}
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, -1);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret < 0)
		goto err_probe;

	pm_runtime_put_autosuspend(&pdev->dev);

	dev_info(&pdev->dev, "spi frequency: %d Hz\n", sp->spi_freq);

	return 0;

err_probe:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);

	mtk_nor_disable_clk(sp);

	return ret;
}

static void mtk_nor_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = dev_get_drvdata(&pdev->dev);
	struct mtk_nor *sp = spi_controller_get_devdata(ctlr);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);

	mtk_nor_disable_clk(sp);
}

static int __maybe_unused mtk_nor_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct mtk_nor *sp = spi_controller_get_devdata(ctlr);

	mtk_nor_disable_clk(sp);

	return 0;
}

static int __maybe_unused mtk_nor_runtime_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct mtk_nor *sp = spi_controller_get_devdata(ctlr);

	return mtk_nor_enable_clk(sp);
}

static int __maybe_unused mtk_nor_suspend(struct device *dev)
{
	return pm_runtime_force_suspend(dev);
}

static int __maybe_unused mtk_nor_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct mtk_nor *sp = spi_controller_get_devdata(ctlr);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	mtk_nor_init(sp);

	return 0;
}

static const struct dev_pm_ops mtk_nor_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_nor_runtime_suspend,
			   mtk_nor_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(mtk_nor_suspend, mtk_nor_resume)
};

static struct platform_driver mtk_nor_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = mtk_nor_match,
		.pm = &mtk_nor_pm_ops,
	},
	.probe = mtk_nor_probe,
	.remove = mtk_nor_remove,
};

module_platform_driver(mtk_nor_driver);

MODULE_DESCRIPTION("Mediatek SPI NOR controller driver");
MODULE_AUTHOR("Chuanhong Guo <gch981213@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
