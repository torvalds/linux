// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 Macronix International Co., Ltd.
//
// Authors:
//	Mason Yang <masonccyang@mxic.com.tw>
//	zhengxunli <zhengxunli@mxic.com.tw>
//	Boris Brezillon <boris.brezillon@bootlin.com>
//

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#define HC_CFG			0x0
#define HC_CFG_IF_CFG(x)	((x) << 27)
#define HC_CFG_DUAL_SLAVE	BIT(31)
#define HC_CFG_INDIVIDUAL	BIT(30)
#define HC_CFG_NIO(x)		(((x) / 4) << 27)
#define HC_CFG_TYPE(s, t)	((t) << (23 + ((s) * 2)))
#define HC_CFG_TYPE_SPI_NOR	0
#define HC_CFG_TYPE_SPI_NAND	1
#define HC_CFG_TYPE_SPI_RAM	2
#define HC_CFG_TYPE_RAW_NAND	3
#define HC_CFG_SLV_ACT(x)	((x) << 21)
#define HC_CFG_CLK_PH_EN	BIT(20)
#define HC_CFG_CLK_POL_INV	BIT(19)
#define HC_CFG_BIG_ENDIAN	BIT(18)
#define HC_CFG_DATA_PASS	BIT(17)
#define HC_CFG_IDLE_SIO_LVL(x)	((x) << 16)
#define HC_CFG_MAN_START_EN	BIT(3)
#define HC_CFG_MAN_START	BIT(2)
#define HC_CFG_MAN_CS_EN	BIT(1)
#define HC_CFG_MAN_CS_ASSERT	BIT(0)

#define INT_STS			0x4
#define INT_STS_EN		0x8
#define INT_SIG_EN		0xc
#define INT_STS_ALL		GENMASK(31, 0)
#define INT_RDY_PIN		BIT(26)
#define INT_RDY_SR		BIT(25)
#define INT_LNR_SUSP		BIT(24)
#define INT_ECC_ERR		BIT(17)
#define INT_CRC_ERR		BIT(16)
#define INT_LWR_DIS		BIT(12)
#define INT_LRD_DIS		BIT(11)
#define INT_SDMA_INT		BIT(10)
#define INT_DMA_FINISH		BIT(9)
#define INT_RX_NOT_FULL		BIT(3)
#define INT_RX_NOT_EMPTY	BIT(2)
#define INT_TX_NOT_FULL		BIT(1)
#define INT_TX_EMPTY		BIT(0)

#define HC_EN			0x10
#define HC_EN_BIT		BIT(0)

#define TXD(x)			(0x14 + ((x) * 4))
#define RXD			0x24

#define SS_CTRL(s)		(0x30 + ((s) * 4))
#define LRD_CFG			0x44
#define LWR_CFG			0x80
#define RWW_CFG			0x70
#define OP_READ			BIT(23)
#define OP_DUMMY_CYC(x)		((x) << 17)
#define OP_ADDR_BYTES(x)	((x) << 14)
#define OP_CMD_BYTES(x)		(((x) - 1) << 13)
#define OP_OCTA_CRC_EN		BIT(12)
#define OP_DQS_EN		BIT(11)
#define OP_ENHC_EN		BIT(10)
#define OP_PREAMBLE_EN		BIT(9)
#define OP_DATA_DDR		BIT(8)
#define OP_DATA_BUSW(x)		((x) << 6)
#define OP_ADDR_DDR		BIT(5)
#define OP_ADDR_BUSW(x)		((x) << 3)
#define OP_CMD_DDR		BIT(2)
#define OP_CMD_BUSW(x)		(x)
#define OP_BUSW_1		0
#define OP_BUSW_2		1
#define OP_BUSW_4		2
#define OP_BUSW_8		3

#define OCTA_CRC		0x38
#define OCTA_CRC_IN_EN(s)	BIT(3 + ((s) * 16))
#define OCTA_CRC_CHUNK(s, x)	((fls((x) / 32)) << (1 + ((s) * 16)))
#define OCTA_CRC_OUT_EN(s)	BIT(0 + ((s) * 16))

#define ONFI_DIN_CNT(s)		(0x3c + (s))

#define LRD_CTRL		0x48
#define RWW_CTRL		0x74
#define LWR_CTRL		0x84
#define LMODE_EN		BIT(31)
#define LMODE_SLV_ACT(x)	((x) << 21)
#define LMODE_CMD1(x)		((x) << 8)
#define LMODE_CMD0(x)		(x)

#define LRD_ADDR		0x4c
#define LWR_ADDR		0x88
#define LRD_RANGE		0x50
#define LWR_RANGE		0x8c

#define AXI_SLV_ADDR		0x54

#define DMAC_RD_CFG		0x58
#define DMAC_WR_CFG		0x94
#define DMAC_CFG_PERIPH_EN	BIT(31)
#define DMAC_CFG_ALLFLUSH_EN	BIT(30)
#define DMAC_CFG_LASTFLUSH_EN	BIT(29)
#define DMAC_CFG_QE(x)		(((x) + 1) << 16)
#define DMAC_CFG_BURST_LEN(x)	(((x) + 1) << 12)
#define DMAC_CFG_BURST_SZ(x)	((x) << 8)
#define DMAC_CFG_DIR_READ	BIT(1)
#define DMAC_CFG_START		BIT(0)

#define DMAC_RD_CNT		0x5c
#define DMAC_WR_CNT		0x98

#define SDMA_ADDR		0x60

#define DMAM_CFG		0x64
#define DMAM_CFG_START		BIT(31)
#define DMAM_CFG_CONT		BIT(30)
#define DMAM_CFG_SDMA_GAP(x)	(fls((x) / 8192) << 2)
#define DMAM_CFG_DIR_READ	BIT(1)
#define DMAM_CFG_EN		BIT(0)

#define DMAM_CNT		0x68

#define LNR_TIMER_TH		0x6c

#define RDM_CFG0		0x78
#define RDM_CFG0_POLY(x)	(x)

#define RDM_CFG1		0x7c
#define RDM_CFG1_RDM_EN		BIT(31)
#define RDM_CFG1_SEED(x)	(x)

#define LWR_SUSP_CTRL		0x90
#define LWR_SUSP_CTRL_EN	BIT(31)

#define DMAS_CTRL		0x9c
#define DMAS_CTRL_DIR_READ	BIT(31)
#define DMAS_CTRL_EN		BIT(30)

#define DATA_STROB		0xa0
#define DATA_STROB_EDO_EN	BIT(2)
#define DATA_STROB_INV_POL	BIT(1)
#define DATA_STROB_DELAY_2CYC	BIT(0)

#define IDLY_CODE(x)		(0xa4 + ((x) * 4))
#define IDLY_CODE_VAL(x, v)	((v) << (((x) % 4) * 8))

#define GPIO			0xc4
#define GPIO_PT(x)		BIT(3 + ((x) * 16))
#define GPIO_RESET(x)		BIT(2 + ((x) * 16))
#define GPIO_HOLDB(x)		BIT(1 + ((x) * 16))
#define GPIO_WPB(x)		BIT((x) * 16)

#define HC_VER			0xd0

#define HW_TEST(x)		(0xe0 + ((x) * 4))

struct mxic_spi {
	struct clk *ps_clk;
	struct clk *send_clk;
	struct clk *send_dly_clk;
	void __iomem *regs;
	u32 cur_speed_hz;
};

static int mxic_spi_clk_enable(struct mxic_spi *mxic)
{
	int ret;

	ret = clk_prepare_enable(mxic->send_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(mxic->send_dly_clk);
	if (ret)
		goto err_send_dly_clk;

	return ret;

err_send_dly_clk:
	clk_disable_unprepare(mxic->send_clk);

	return ret;
}

static void mxic_spi_clk_disable(struct mxic_spi *mxic)
{
	clk_disable_unprepare(mxic->send_clk);
	clk_disable_unprepare(mxic->send_dly_clk);
}

static void mxic_spi_set_input_delay_dqs(struct mxic_spi *mxic, u8 idly_code)
{
	writel(IDLY_CODE_VAL(0, idly_code) |
	       IDLY_CODE_VAL(1, idly_code) |
	       IDLY_CODE_VAL(2, idly_code) |
	       IDLY_CODE_VAL(3, idly_code),
	       mxic->regs + IDLY_CODE(0));
	writel(IDLY_CODE_VAL(4, idly_code) |
	       IDLY_CODE_VAL(5, idly_code) |
	       IDLY_CODE_VAL(6, idly_code) |
	       IDLY_CODE_VAL(7, idly_code),
	       mxic->regs + IDLY_CODE(1));
}

static int mxic_spi_clk_setup(struct mxic_spi *mxic, unsigned long freq)
{
	int ret;

	ret = clk_set_rate(mxic->send_clk, freq);
	if (ret)
		return ret;

	ret = clk_set_rate(mxic->send_dly_clk, freq);
	if (ret)
		return ret;

	/*
	 * A constant delay range from 0x0 ~ 0x1F for input delay,
	 * the unit is 78 ps, the max input delay is 2.418 ns.
	 */
	mxic_spi_set_input_delay_dqs(mxic, 0xf);

	/*
	 * Phase degree = 360 * freq * output-delay
	 * where output-delay is a constant value 1 ns in FPGA.
	 *
	 * Get Phase degree = 360 * freq * 1 ns
	 *                  = 360 * freq * 1 sec / 1000000000
	 *                  = 9 * freq / 25000000
	 */
	ret = clk_set_phase(mxic->send_dly_clk, 9 * freq / 25000000);
	if (ret)
		return ret;

	return 0;
}

static int mxic_spi_set_freq(struct mxic_spi *mxic, unsigned long freq)
{
	int ret;

	if (mxic->cur_speed_hz == freq)
		return 0;

	mxic_spi_clk_disable(mxic);
	ret = mxic_spi_clk_setup(mxic, freq);
	if (ret)
		return ret;

	ret = mxic_spi_clk_enable(mxic);
	if (ret)
		return ret;

	mxic->cur_speed_hz = freq;

	return 0;
}

static void mxic_spi_hw_init(struct mxic_spi *mxic)
{
	writel(0, mxic->regs + DATA_STROB);
	writel(INT_STS_ALL, mxic->regs + INT_STS_EN);
	writel(0, mxic->regs + HC_EN);
	writel(0, mxic->regs + LRD_CFG);
	writel(0, mxic->regs + LRD_CTRL);
	writel(HC_CFG_NIO(1) | HC_CFG_TYPE(0, HC_CFG_TYPE_SPI_NAND) |
	       HC_CFG_SLV_ACT(0) | HC_CFG_MAN_CS_EN | HC_CFG_IDLE_SIO_LVL(1),
	       mxic->regs + HC_CFG);
}

static int mxic_spi_data_xfer(struct mxic_spi *mxic, const void *txbuf,
			      void *rxbuf, unsigned int len)
{
	unsigned int pos = 0;

	while (pos < len) {
		unsigned int nbytes = len - pos;
		u32 data = 0xffffffff;
		u32 sts;
		int ret;

		if (nbytes > 4)
			nbytes = 4;

		if (txbuf)
			memcpy(&data, txbuf + pos, nbytes);

		ret = readl_poll_timeout(mxic->regs + INT_STS, sts,
					 sts & INT_TX_EMPTY, 0, USEC_PER_SEC);
		if (ret)
			return ret;

		writel(data, mxic->regs + TXD(nbytes % 4));

		if (rxbuf) {
			ret = readl_poll_timeout(mxic->regs + INT_STS, sts,
						 sts & INT_TX_EMPTY, 0,
						 USEC_PER_SEC);
			if (ret)
				return ret;

			ret = readl_poll_timeout(mxic->regs + INT_STS, sts,
						 sts & INT_RX_NOT_EMPTY, 0,
						 USEC_PER_SEC);
			if (ret)
				return ret;

			data = readl(mxic->regs + RXD);
			data >>= (8 * (4 - nbytes));
			memcpy(rxbuf + pos, &data, nbytes);
			WARN_ON(readl(mxic->regs + INT_STS) & INT_RX_NOT_EMPTY);
		} else {
			readl(mxic->regs + RXD);
		}
		WARN_ON(readl(mxic->regs + INT_STS) & INT_RX_NOT_EMPTY);

		pos += nbytes;
	}

	return 0;
}

static bool mxic_spi_mem_supports_op(struct spi_mem *mem,
				     const struct spi_mem_op *op)
{
	if (op->data.buswidth > 4 || op->addr.buswidth > 4 ||
	    op->dummy.buswidth > 4 || op->cmd.buswidth > 4)
		return false;

	if (op->data.nbytes && op->dummy.nbytes &&
	    op->data.buswidth != op->dummy.buswidth)
		return false;

	if (op->addr.nbytes > 7)
		return false;

	return true;
}

static int mxic_spi_mem_exec_op(struct spi_mem *mem,
				const struct spi_mem_op *op)
{
	struct mxic_spi *mxic = spi_master_get_devdata(mem->spi->master);
	int nio = 1, i, ret;
	u32 ss_ctrl;
	u8 addr[8];

	ret = mxic_spi_set_freq(mxic, mem->spi->max_speed_hz);
	if (ret)
		return ret;

	if (mem->spi->mode & (SPI_TX_QUAD | SPI_RX_QUAD))
		nio = 4;
	else if (mem->spi->mode & (SPI_TX_DUAL | SPI_RX_DUAL))
		nio = 2;

	writel(HC_CFG_NIO(nio) |
	       HC_CFG_TYPE(mem->spi->chip_select, HC_CFG_TYPE_SPI_NOR) |
	       HC_CFG_SLV_ACT(mem->spi->chip_select) | HC_CFG_IDLE_SIO_LVL(1) |
	       HC_CFG_MAN_CS_EN,
	       mxic->regs + HC_CFG);
	writel(HC_EN_BIT, mxic->regs + HC_EN);

	ss_ctrl = OP_CMD_BYTES(1) | OP_CMD_BUSW(fls(op->cmd.buswidth) - 1);

	if (op->addr.nbytes)
		ss_ctrl |= OP_ADDR_BYTES(op->addr.nbytes) |
			   OP_ADDR_BUSW(fls(op->addr.buswidth) - 1);

	if (op->dummy.nbytes)
		ss_ctrl |= OP_DUMMY_CYC(op->dummy.nbytes);

	if (op->data.nbytes) {
		ss_ctrl |= OP_DATA_BUSW(fls(op->data.buswidth) - 1);
		if (op->data.dir == SPI_MEM_DATA_IN)
			ss_ctrl |= OP_READ;
	}

	writel(ss_ctrl, mxic->regs + SS_CTRL(mem->spi->chip_select));

	writel(readl(mxic->regs + HC_CFG) | HC_CFG_MAN_CS_ASSERT,
	       mxic->regs + HC_CFG);

	ret = mxic_spi_data_xfer(mxic, &op->cmd.opcode, NULL, 1);
	if (ret)
		goto out;

	for (i = 0; i < op->addr.nbytes; i++)
		addr[i] = op->addr.val >> (8 * (op->addr.nbytes - i - 1));

	ret = mxic_spi_data_xfer(mxic, addr, NULL, op->addr.nbytes);
	if (ret)
		goto out;

	ret = mxic_spi_data_xfer(mxic, NULL, NULL, op->dummy.nbytes);
	if (ret)
		goto out;

	ret = mxic_spi_data_xfer(mxic,
				 op->data.dir == SPI_MEM_DATA_OUT ?
				 op->data.buf.out : NULL,
				 op->data.dir == SPI_MEM_DATA_IN ?
				 op->data.buf.in : NULL,
				 op->data.nbytes);

out:
	writel(readl(mxic->regs + HC_CFG) & ~HC_CFG_MAN_CS_ASSERT,
	       mxic->regs + HC_CFG);
	writel(0, mxic->regs + HC_EN);

	return ret;
}

static const struct spi_controller_mem_ops mxic_spi_mem_ops = {
	.supports_op = mxic_spi_mem_supports_op,
	.exec_op = mxic_spi_mem_exec_op,
};

static void mxic_spi_set_cs(struct spi_device *spi, bool lvl)
{
	struct mxic_spi *mxic = spi_master_get_devdata(spi->master);

	if (!lvl) {
		writel(readl(mxic->regs + HC_CFG) | HC_CFG_MAN_CS_EN,
		       mxic->regs + HC_CFG);
		writel(HC_EN_BIT, mxic->regs + HC_EN);
		writel(readl(mxic->regs + HC_CFG) | HC_CFG_MAN_CS_ASSERT,
		       mxic->regs + HC_CFG);
	} else {
		writel(readl(mxic->regs + HC_CFG) & ~HC_CFG_MAN_CS_ASSERT,
		       mxic->regs + HC_CFG);
		writel(0, mxic->regs + HC_EN);
	}
}

static int mxic_spi_transfer_one(struct spi_master *master,
				 struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct mxic_spi *mxic = spi_master_get_devdata(master);
	unsigned int busw = OP_BUSW_1;
	int ret;

	if (t->rx_buf && t->tx_buf) {
		if (((spi->mode & SPI_TX_QUAD) &&
		     !(spi->mode & SPI_RX_QUAD)) ||
		    ((spi->mode & SPI_TX_DUAL) &&
		     !(spi->mode & SPI_RX_DUAL)))
			return -ENOTSUPP;
	}

	ret = mxic_spi_set_freq(mxic, t->speed_hz);
	if (ret)
		return ret;

	if (t->tx_buf) {
		if (spi->mode & SPI_TX_QUAD)
			busw = OP_BUSW_4;
		else if (spi->mode & SPI_TX_DUAL)
			busw = OP_BUSW_2;
	} else if (t->rx_buf) {
		if (spi->mode & SPI_RX_QUAD)
			busw = OP_BUSW_4;
		else if (spi->mode & SPI_RX_DUAL)
			busw = OP_BUSW_2;
	}

	writel(OP_CMD_BYTES(1) | OP_CMD_BUSW(busw) |
	       OP_DATA_BUSW(busw) | (t->rx_buf ? OP_READ : 0),
	       mxic->regs + SS_CTRL(0));

	ret = mxic_spi_data_xfer(mxic, t->tx_buf, t->rx_buf, t->len);
	if (ret)
		return ret;

	spi_finalize_current_transfer(master);

	return 0;
}

static int __maybe_unused mxic_spi_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct mxic_spi *mxic = spi_master_get_devdata(master);

	mxic_spi_clk_disable(mxic);
	clk_disable_unprepare(mxic->ps_clk);

	return 0;
}

static int __maybe_unused mxic_spi_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct mxic_spi *mxic = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(mxic->ps_clk);
	if (ret) {
		dev_err(dev, "Cannot enable ps_clock.\n");
		return ret;
	}

	return mxic_spi_clk_enable(mxic);
}

static const struct dev_pm_ops mxic_spi_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(mxic_spi_runtime_suspend,
			   mxic_spi_runtime_resume, NULL)
};

static int mxic_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct resource *res;
	struct mxic_spi *mxic;
	int ret;

	master = spi_alloc_master(&pdev->dev, sizeof(struct mxic_spi));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	mxic = spi_master_get_devdata(master);

	master->dev.of_node = pdev->dev.of_node;

	mxic->ps_clk = devm_clk_get(&pdev->dev, "ps_clk");
	if (IS_ERR(mxic->ps_clk))
		return PTR_ERR(mxic->ps_clk);

	mxic->send_clk = devm_clk_get(&pdev->dev, "send_clk");
	if (IS_ERR(mxic->send_clk))
		return PTR_ERR(mxic->send_clk);

	mxic->send_dly_clk = devm_clk_get(&pdev->dev, "send_dly_clk");
	if (IS_ERR(mxic->send_dly_clk))
		return PTR_ERR(mxic->send_dly_clk);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	mxic->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mxic->regs))
		return PTR_ERR(mxic->regs);

	pm_runtime_enable(&pdev->dev);
	master->auto_runtime_pm = true;

	master->num_chipselect = 1;
	master->mem_ops = &mxic_spi_mem_ops;

	master->set_cs = mxic_spi_set_cs;
	master->transfer_one = mxic_spi_transfer_one;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->mode_bits = SPI_CPOL | SPI_CPHA |
			SPI_RX_DUAL | SPI_TX_DUAL |
			SPI_RX_QUAD | SPI_TX_QUAD;

	mxic_spi_hw_init(mxic);

	ret = spi_register_master(master);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_master failed\n");
		goto err_put_master;
	}

	return 0;

err_put_master:
	spi_master_put(master);
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int mxic_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	spi_unregister_master(master);

	return 0;
}

static const struct of_device_id mxic_spi_of_ids[] = {
	{ .compatible = "mxicy,mx25f0a-spi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxic_spi_of_ids);

static struct platform_driver mxic_spi_driver = {
	.probe = mxic_spi_probe,
	.remove = mxic_spi_remove,
	.driver = {
		.name = "mxic-spi",
		.of_match_table = mxic_spi_of_ids,
		.pm = &mxic_spi_dev_pm_ops,
	},
};
module_platform_driver(mxic_spi_driver);

MODULE_AUTHOR("Mason Yang <masonccyang@mxic.com.tw>");
MODULE_DESCRIPTION("MX25F0A SPI controller driver");
MODULE_LICENSE("GPL v2");
