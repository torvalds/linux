// SPDX-License-Identifier: GPL-2.0-only
/*
 * SG2044 SPI NOR controller driver
 *
 * Copyright (c) 2025 Longbin Li <looong.bin@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi-mem.h>

/* Hardware register definitions */
#define SPIFMC_CTRL				0x00
#define SPIFMC_CTRL_CPHA			BIT(12)
#define SPIFMC_CTRL_CPOL			BIT(13)
#define SPIFMC_CTRL_HOLD_OL			BIT(14)
#define SPIFMC_CTRL_WP_OL			BIT(15)
#define SPIFMC_CTRL_LSBF			BIT(20)
#define SPIFMC_CTRL_SRST			BIT(21)
#define SPIFMC_CTRL_SCK_DIV_SHIFT		0
#define SPIFMC_CTRL_FRAME_LEN_SHIFT		16
#define SPIFMC_CTRL_SCK_DIV_MASK		0x7FF

#define SPIFMC_CE_CTRL				0x04
#define SPIFMC_CE_CTRL_CEMANUAL			BIT(0)
#define SPIFMC_CE_CTRL_CEMANUAL_EN		BIT(1)

#define SPIFMC_DLY_CTRL				0x08
#define SPIFMC_CTRL_FM_INTVL_MASK		0x000f
#define SPIFMC_CTRL_FM_INTVL			BIT(0)
#define SPIFMC_CTRL_CET_MASK			0x0f00
#define SPIFMC_CTRL_CET				BIT(8)

#define SPIFMC_DMMR				0x0c

#define SPIFMC_TRAN_CSR				0x10
#define SPIFMC_TRAN_CSR_TRAN_MODE_MASK		GENMASK(1, 0)
#define SPIFMC_TRAN_CSR_TRAN_MODE_RX		BIT(0)
#define SPIFMC_TRAN_CSR_TRAN_MODE_TX		BIT(1)
#define SPIFMC_TRAN_CSR_FAST_MODE		BIT(3)
#define SPIFMC_TRAN_CSR_BUS_WIDTH_1_BIT		(0x00 << 4)
#define SPIFMC_TRAN_CSR_BUS_WIDTH_2_BIT		(0x01 << 4)
#define SPIFMC_TRAN_CSR_BUS_WIDTH_4_BIT		(0x02 << 4)
#define SPIFMC_TRAN_CSR_DMA_EN			BIT(6)
#define SPIFMC_TRAN_CSR_MISO_LEVEL		BIT(7)
#define SPIFMC_TRAN_CSR_ADDR_BYTES_MASK		GENMASK(10, 8)
#define SPIFMC_TRAN_CSR_ADDR_BYTES_SHIFT	8
#define SPIFMC_TRAN_CSR_WITH_CMD		BIT(11)
#define SPIFMC_TRAN_CSR_FIFO_TRG_LVL_MASK	GENMASK(13, 12)
#define SPIFMC_TRAN_CSR_FIFO_TRG_LVL_1_BYTE	(0x00 << 12)
#define SPIFMC_TRAN_CSR_FIFO_TRG_LVL_2_BYTE	(0x01 << 12)
#define SPIFMC_TRAN_CSR_FIFO_TRG_LVL_4_BYTE	(0x02 << 12)
#define SPIFMC_TRAN_CSR_FIFO_TRG_LVL_8_BYTE	(0x03 << 12)
#define SPIFMC_TRAN_CSR_GO_BUSY			BIT(15)
#define SPIFMC_TRAN_CSR_ADDR4B_SHIFT		20
#define SPIFMC_TRAN_CSR_CMD4B_SHIFT		21

#define SPIFMC_TRAN_NUM				0x14
#define SPIFMC_FIFO_PORT			0x18
#define SPIFMC_FIFO_PT				0x20

#define SPIFMC_INT_STS				0x28
#define SPIFMC_INT_TRAN_DONE			BIT(0)
#define SPIFMC_INT_RD_FIFO			BIT(2)
#define SPIFMC_INT_WR_FIFO			BIT(3)
#define SPIFMC_INT_RX_FRAME			BIT(4)
#define SPIFMC_INT_TX_FRAME			BIT(5)

#define SPIFMC_INT_EN				0x2c
#define SPIFMC_INT_TRAN_DONE_EN			BIT(0)
#define SPIFMC_INT_RD_FIFO_EN			BIT(2)
#define SPIFMC_INT_WR_FIFO_EN			BIT(3)
#define SPIFMC_INT_RX_FRAME_EN			BIT(4)
#define SPIFMC_INT_TX_FRAME_EN			BIT(5)

#define SPIFMC_OPT				0x030
#define SPIFMC_OPT_DISABLE_FIFO_FLUSH		BIT(1)

#define SPIFMC_MAX_FIFO_DEPTH			8

#define SPIFMC_MAX_READ_SIZE			0x10000

struct sg204x_spifmc_chip_info {
	bool has_opt_reg;
	u32 rd_fifo_int_trigger_level;
};

struct sg2044_spifmc {
	struct spi_controller *ctrl;
	void __iomem *io_base;
	struct device *dev;
	struct mutex lock;
	struct clk *clk;
	const struct sg204x_spifmc_chip_info *chip_info;
};

static int sg2044_spifmc_wait_int(struct sg2044_spifmc *spifmc, u8 int_type)
{
	u32 stat;

	return readl_poll_timeout(spifmc->io_base + SPIFMC_INT_STS, stat,
				  (stat & int_type), 0, 1000000);
}

static int sg2044_spifmc_wait_xfer_size(struct sg2044_spifmc *spifmc,
					int xfer_size)
{
	u8 stat;

	return readl_poll_timeout(spifmc->io_base + SPIFMC_FIFO_PT, stat,
				  ((stat & 0xf) == xfer_size), 1, 1000000);
}

static u32 sg2044_spifmc_init_reg(struct sg2044_spifmc *spifmc)
{
	u32 reg;

	reg = readl(spifmc->io_base + SPIFMC_TRAN_CSR);
	reg &= ~(SPIFMC_TRAN_CSR_TRAN_MODE_MASK |
		 SPIFMC_TRAN_CSR_FAST_MODE |
		 SPIFMC_TRAN_CSR_BUS_WIDTH_2_BIT |
		 SPIFMC_TRAN_CSR_BUS_WIDTH_4_BIT |
		 SPIFMC_TRAN_CSR_DMA_EN |
		 SPIFMC_TRAN_CSR_ADDR_BYTES_MASK |
		 SPIFMC_TRAN_CSR_WITH_CMD |
		 SPIFMC_TRAN_CSR_FIFO_TRG_LVL_MASK);

	writel(reg, spifmc->io_base + SPIFMC_TRAN_CSR);

	return reg;
}

static ssize_t sg2044_spifmc_read_64k(struct sg2044_spifmc *spifmc,
				      const struct spi_mem_op *op, loff_t from,
				      size_t len, u_char *buf)
{
	int xfer_size, offset;
	u32 reg;
	int ret;
	int i;

	reg = sg2044_spifmc_init_reg(spifmc);
	reg |= (op->addr.nbytes + op->dummy.nbytes) << SPIFMC_TRAN_CSR_ADDR_BYTES_SHIFT;
	reg |= spifmc->chip_info->rd_fifo_int_trigger_level;
	reg |= SPIFMC_TRAN_CSR_WITH_CMD;
	reg |= SPIFMC_TRAN_CSR_TRAN_MODE_RX;

	writel(0, spifmc->io_base + SPIFMC_FIFO_PT);
	writeb(op->cmd.opcode, spifmc->io_base + SPIFMC_FIFO_PORT);

	for (i = op->addr.nbytes - 1; i >= 0; i--)
		writeb((from >> i * 8) & 0xff, spifmc->io_base + SPIFMC_FIFO_PORT);

	for (i = 0; i < op->dummy.nbytes; i++)
		writeb(0xff, spifmc->io_base + SPIFMC_FIFO_PORT);

	writel(len, spifmc->io_base + SPIFMC_TRAN_NUM);
	writel(0, spifmc->io_base + SPIFMC_INT_STS);
	reg |= SPIFMC_TRAN_CSR_GO_BUSY;
	writel(reg, spifmc->io_base + SPIFMC_TRAN_CSR);

	ret = sg2044_spifmc_wait_int(spifmc, SPIFMC_INT_RD_FIFO);
	if (ret < 0)
		return ret;

	offset = 0;
	while (offset < len) {
		xfer_size = min_t(size_t, SPIFMC_MAX_FIFO_DEPTH, len - offset);

		ret = sg2044_spifmc_wait_xfer_size(spifmc, xfer_size);
		if (ret < 0)
			return ret;

		for (i = 0; i < xfer_size; i++)
			buf[i + offset] = readb(spifmc->io_base + SPIFMC_FIFO_PORT);

		offset += xfer_size;
	}

	ret = sg2044_spifmc_wait_int(spifmc, SPIFMC_INT_TRAN_DONE);
	if (ret < 0)
		return ret;

	writel(0, spifmc->io_base + SPIFMC_FIFO_PT);

	return len;
}

static ssize_t sg2044_spifmc_read(struct sg2044_spifmc *spifmc,
				  const struct spi_mem_op *op)
{
	size_t xfer_size;
	size_t offset;
	loff_t from = op->addr.val;
	size_t len = op->data.nbytes;
	int ret;
	u8 *din = op->data.buf.in;

	offset = 0;
	while (offset < len) {
		xfer_size = min_t(size_t, SPIFMC_MAX_READ_SIZE, len - offset);

		ret = sg2044_spifmc_read_64k(spifmc, op, from, xfer_size, din);
		if (ret < 0)
			return ret;

		offset += xfer_size;
		din += xfer_size;
		from += xfer_size;
	}

	return 0;
}

static ssize_t sg2044_spifmc_write(struct sg2044_spifmc *spifmc,
				   const struct spi_mem_op *op)
{
	size_t xfer_size;
	const u8 *dout = op->data.buf.out;
	int i, offset;
	int ret;
	u32 reg;

	reg = sg2044_spifmc_init_reg(spifmc);
	reg |= (op->addr.nbytes + op->dummy.nbytes) << SPIFMC_TRAN_CSR_ADDR_BYTES_SHIFT;
	reg |= SPIFMC_TRAN_CSR_FIFO_TRG_LVL_8_BYTE;
	reg |= SPIFMC_TRAN_CSR_WITH_CMD;
	reg |= SPIFMC_TRAN_CSR_TRAN_MODE_TX;

	writel(0, spifmc->io_base + SPIFMC_FIFO_PT);
	writeb(op->cmd.opcode, spifmc->io_base + SPIFMC_FIFO_PORT);

	for (i = op->addr.nbytes - 1; i >= 0; i--)
		writeb((op->addr.val >> i * 8) & 0xff, spifmc->io_base + SPIFMC_FIFO_PORT);

	for (i = 0; i < op->dummy.nbytes; i++)
		writeb(0xff, spifmc->io_base + SPIFMC_FIFO_PORT);

	writel(0, spifmc->io_base + SPIFMC_INT_STS);
	writel(op->data.nbytes, spifmc->io_base + SPIFMC_TRAN_NUM);
	reg |= SPIFMC_TRAN_CSR_GO_BUSY;
	writel(reg, spifmc->io_base + SPIFMC_TRAN_CSR);

	ret = sg2044_spifmc_wait_xfer_size(spifmc, 0);
	if (ret < 0)
		return ret;

	writel(0, spifmc->io_base + SPIFMC_FIFO_PT);

	offset = 0;
	while (offset < op->data.nbytes) {
		xfer_size = min_t(size_t, SPIFMC_MAX_FIFO_DEPTH, op->data.nbytes - offset);

		ret = sg2044_spifmc_wait_xfer_size(spifmc, 0);
		if (ret < 0)
			return ret;

		for (i = 0; i < xfer_size; i++)
			writeb(dout[i + offset], spifmc->io_base + SPIFMC_FIFO_PORT);

		offset += xfer_size;
	}

	ret = sg2044_spifmc_wait_int(spifmc, SPIFMC_INT_TRAN_DONE);
	if (ret < 0)
		return ret;

	writel(0, spifmc->io_base + SPIFMC_FIFO_PT);

	return 0;
}

static ssize_t sg2044_spifmc_tran_cmd(struct sg2044_spifmc *spifmc,
				      const struct spi_mem_op *op)
{
	int i, ret;
	u32 reg;

	reg = sg2044_spifmc_init_reg(spifmc);
	reg |= (op->addr.nbytes + op->dummy.nbytes) << SPIFMC_TRAN_CSR_ADDR_BYTES_SHIFT;
	reg |= SPIFMC_TRAN_CSR_FIFO_TRG_LVL_1_BYTE;
	reg |= SPIFMC_TRAN_CSR_WITH_CMD;

	writel(0, spifmc->io_base + SPIFMC_FIFO_PT);
	writeb(op->cmd.opcode, spifmc->io_base + SPIFMC_FIFO_PORT);

	for (i = op->addr.nbytes - 1; i >= 0; i--)
		writeb((op->addr.val >> i * 8) & 0xff, spifmc->io_base + SPIFMC_FIFO_PORT);

	for (i = 0; i < op->dummy.nbytes; i++)
		writeb(0xff, spifmc->io_base + SPIFMC_FIFO_PORT);

	writel(0, spifmc->io_base + SPIFMC_INT_STS);
	reg |= SPIFMC_TRAN_CSR_GO_BUSY;
	writel(reg, spifmc->io_base + SPIFMC_TRAN_CSR);

	ret = sg2044_spifmc_wait_int(spifmc, SPIFMC_INT_TRAN_DONE);
	if (ret < 0)
		return ret;

	writel(0, spifmc->io_base + SPIFMC_FIFO_PT);

	return 0;
}

static void sg2044_spifmc_trans(struct sg2044_spifmc *spifmc,
				const struct spi_mem_op *op)
{
	if (op->data.dir == SPI_MEM_DATA_IN)
		sg2044_spifmc_read(spifmc, op);
	else if (op->data.dir == SPI_MEM_DATA_OUT)
		sg2044_spifmc_write(spifmc, op);
	else
		sg2044_spifmc_tran_cmd(spifmc, op);
}

static ssize_t sg2044_spifmc_trans_reg(struct sg2044_spifmc *spifmc,
				       const struct spi_mem_op *op)
{
	const u8 *dout = NULL;
	u8 *din = NULL;
	size_t len = op->data.nbytes;
	int ret, i;
	u32 reg;

	if (op->data.dir == SPI_MEM_DATA_IN)
		din = op->data.buf.in;
	else
		dout = op->data.buf.out;

	reg = sg2044_spifmc_init_reg(spifmc);
	reg |= SPIFMC_TRAN_CSR_FIFO_TRG_LVL_1_BYTE;
	reg |= SPIFMC_TRAN_CSR_WITH_CMD;

	if (din) {
		reg |= SPIFMC_TRAN_CSR_BUS_WIDTH_1_BIT;
		reg |= SPIFMC_TRAN_CSR_TRAN_MODE_RX;
		reg |= SPIFMC_TRAN_CSR_TRAN_MODE_TX;

		if (spifmc->chip_info->has_opt_reg)
			writel(SPIFMC_OPT_DISABLE_FIFO_FLUSH, spifmc->io_base + SPIFMC_OPT);
	} else {
		/*
		 * If write values to the Status Register,
		 * configure TRAN_CSR register as the same as
		 * sg2044_spifmc_read_reg.
		 */
		if (op->cmd.opcode == 0x01) {
			reg |= SPIFMC_TRAN_CSR_TRAN_MODE_RX;
			reg |= SPIFMC_TRAN_CSR_TRAN_MODE_TX;
			writel(len, spifmc->io_base + SPIFMC_TRAN_NUM);
		}
	}

	writel(0, spifmc->io_base + SPIFMC_FIFO_PT);
	writeb(op->cmd.opcode, spifmc->io_base + SPIFMC_FIFO_PORT);

	for (i = 0; i < len; i++) {
		if (din)
			writeb(0xff, spifmc->io_base + SPIFMC_FIFO_PORT);
		else
			writeb(dout[i], spifmc->io_base + SPIFMC_FIFO_PORT);
	}

	writel(0, spifmc->io_base + SPIFMC_INT_STS);
	writel(len, spifmc->io_base + SPIFMC_TRAN_NUM);
	reg |= SPIFMC_TRAN_CSR_GO_BUSY;
	writel(reg, spifmc->io_base + SPIFMC_TRAN_CSR);

	ret = sg2044_spifmc_wait_int(spifmc, SPIFMC_INT_TRAN_DONE);
	if (ret < 0)
		return ret;

	if (din) {
		while (len--)
			*din++ = readb(spifmc->io_base + SPIFMC_FIFO_PORT);
	}

	writel(0, spifmc->io_base + SPIFMC_FIFO_PT);

	return 0;
}

static int sg2044_spifmc_exec_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	struct sg2044_spifmc *spifmc;

	spifmc = spi_controller_get_devdata(mem->spi->controller);

	mutex_lock(&spifmc->lock);

	if (op->addr.nbytes == 0)
		sg2044_spifmc_trans_reg(spifmc, op);
	else
		sg2044_spifmc_trans(spifmc, op);

	mutex_unlock(&spifmc->lock);

	return 0;
}

static const struct spi_controller_mem_ops sg2044_spifmc_mem_ops = {
	.exec_op = sg2044_spifmc_exec_op,
};

static void sg2044_spifmc_init(struct sg2044_spifmc *spifmc)
{
	u32 tran_csr;
	u32 reg;

	writel(0, spifmc->io_base + SPIFMC_DMMR);

	reg = readl(spifmc->io_base + SPIFMC_CTRL);
	reg |= SPIFMC_CTRL_SRST;
	reg &= ~(SPIFMC_CTRL_SCK_DIV_MASK);
	reg |= 1;
	writel(reg, spifmc->io_base + SPIFMC_CTRL);

	writel(0, spifmc->io_base + SPIFMC_CE_CTRL);

	tran_csr = readl(spifmc->io_base + SPIFMC_TRAN_CSR);
	tran_csr |= (0 << SPIFMC_TRAN_CSR_ADDR_BYTES_SHIFT);
	tran_csr |= SPIFMC_TRAN_CSR_FIFO_TRG_LVL_4_BYTE;
	tran_csr |= SPIFMC_TRAN_CSR_WITH_CMD;
	writel(tran_csr, spifmc->io_base + SPIFMC_TRAN_CSR);
}

static int sg2044_spifmc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *ctrl;
	struct sg2044_spifmc *spifmc;
	int ret;

	ctrl = devm_spi_alloc_host(&pdev->dev, sizeof(*spifmc));
	if (!ctrl)
		return -ENOMEM;

	spifmc = spi_controller_get_devdata(ctrl);

	spifmc->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(spifmc->clk))
		return dev_err_probe(dev, PTR_ERR(spifmc->clk), "Cannot get and enable AHB clock\n");

	spifmc->dev = &pdev->dev;
	spifmc->ctrl = ctrl;

	spifmc->io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spifmc->io_base))
		return PTR_ERR(spifmc->io_base);

	ctrl->num_chipselect = 1;
	ctrl->dev.of_node = pdev->dev.of_node;
	ctrl->bits_per_word_mask = SPI_BPW_MASK(8);
	ctrl->auto_runtime_pm = false;
	ctrl->mem_ops = &sg2044_spifmc_mem_ops;
	ctrl->mode_bits = SPI_RX_DUAL | SPI_TX_DUAL | SPI_RX_QUAD | SPI_TX_QUAD;

	ret = devm_mutex_init(dev, &spifmc->lock);
	if (ret)
		return ret;
	spifmc->chip_info = device_get_match_data(&pdev->dev);
	if (!spifmc->chip_info) {
		dev_err(&pdev->dev, "Failed to get specific chip info\n");
		return -EINVAL;
	}

	sg2044_spifmc_init(spifmc);
	sg2044_spifmc_init_reg(spifmc);

	ret = devm_spi_register_controller(&pdev->dev, ctrl);
	if (ret)
		return dev_err_probe(dev, ret, "spi_register_controller failed\n");

	return 0;
}

static const struct sg204x_spifmc_chip_info sg2044_chip_info = {
	.has_opt_reg = true,
	.rd_fifo_int_trigger_level = SPIFMC_TRAN_CSR_FIFO_TRG_LVL_8_BYTE,
};

static const struct sg204x_spifmc_chip_info sg2042_chip_info = {
	.has_opt_reg = false,
	.rd_fifo_int_trigger_level = SPIFMC_TRAN_CSR_FIFO_TRG_LVL_1_BYTE,
};

static const struct of_device_id sg2044_spifmc_match[] = {
	{ .compatible = "sophgo,sg2044-spifmc-nor", .data = &sg2044_chip_info },
	{ .compatible = "sophgo,sg2042-spifmc-nor", .data = &sg2042_chip_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sg2044_spifmc_match);

static struct platform_driver sg2044_nor_driver = {
	.driver = {
		.name = "sg2044,spifmc-nor",
		.of_match_table = sg2044_spifmc_match,
	},
	.probe = sg2044_spifmc_probe,
};
module_platform_driver(sg2044_nor_driver);

MODULE_DESCRIPTION("SG2044 SPI NOR controller driver");
MODULE_AUTHOR("Longbin Li <looong.bin@gmail.com>");
MODULE_LICENSE("GPL");
