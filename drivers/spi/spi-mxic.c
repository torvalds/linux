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
#include <linux/mtd/nand.h>
#include <linux/mtd/nand-ecc-mxic.h>
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
#define DMAS_CTRL_EN		BIT(31)
#define DMAS_CTRL_DIR_READ	BIT(30)

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
	struct device *dev;
	struct clk *ps_clk;
	struct clk *send_clk;
	struct clk *send_dly_clk;
	void __iomem *regs;
	u32 cur_speed_hz;
	struct {
		void __iomem *map;
		dma_addr_t dma;
		size_t size;
	} linear;

	struct {
		bool use_pipelined_conf;
		struct nand_ecc_engine *pipelined_engine;
		void *ctx;
	} ecc;
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
	writel(HC_CFG_NIO(1) | HC_CFG_TYPE(0, HC_CFG_TYPE_SPI_NOR) |
	       HC_CFG_SLV_ACT(0) | HC_CFG_MAN_CS_EN | HC_CFG_IDLE_SIO_LVL(1),
	       mxic->regs + HC_CFG);
}

static u32 mxic_spi_prep_hc_cfg(struct spi_device *spi, u32 flags)
{
	int nio = 1;

	if (spi->mode & (SPI_TX_OCTAL | SPI_RX_OCTAL))
		nio = 8;
	else if (spi->mode & (SPI_TX_QUAD | SPI_RX_QUAD))
		nio = 4;
	else if (spi->mode & (SPI_TX_DUAL | SPI_RX_DUAL))
		nio = 2;

	return flags | HC_CFG_NIO(nio) |
	       HC_CFG_TYPE(spi_get_chipselect(spi, 0), HC_CFG_TYPE_SPI_NOR) |
	       HC_CFG_SLV_ACT(spi_get_chipselect(spi, 0)) | HC_CFG_IDLE_SIO_LVL(1);
}

static u32 mxic_spi_mem_prep_op_cfg(const struct spi_mem_op *op,
				    unsigned int data_len)
{
	u32 cfg = OP_CMD_BYTES(op->cmd.nbytes) |
		  OP_CMD_BUSW(fls(op->cmd.buswidth) - 1) |
		  (op->cmd.dtr ? OP_CMD_DDR : 0);

	if (op->addr.nbytes)
		cfg |= OP_ADDR_BYTES(op->addr.nbytes) |
		       OP_ADDR_BUSW(fls(op->addr.buswidth) - 1) |
		       (op->addr.dtr ? OP_ADDR_DDR : 0);

	if (op->dummy.nbytes)
		cfg |= OP_DUMMY_CYC(op->dummy.nbytes);

	/* Direct mapping data.nbytes field is not populated */
	if (data_len) {
		cfg |= OP_DATA_BUSW(fls(op->data.buswidth) - 1) |
		       (op->data.dtr ? OP_DATA_DDR : 0);
		if (op->data.dir == SPI_MEM_DATA_IN) {
			cfg |= OP_READ;
			if (op->data.dtr)
				cfg |= OP_DQS_EN;
		}
	}

	return cfg;
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

		ret = readl_poll_timeout(mxic->regs + INT_STS, sts,
					 sts & INT_TX_EMPTY, 0, USEC_PER_SEC);
		if (ret)
			return ret;

		ret = readl_poll_timeout(mxic->regs + INT_STS, sts,
					 sts & INT_RX_NOT_EMPTY, 0,
					 USEC_PER_SEC);
		if (ret)
			return ret;

		data = readl(mxic->regs + RXD);
		if (rxbuf) {
			data >>= (8 * (4 - nbytes));
			memcpy(rxbuf + pos, &data, nbytes);
		}
		WARN_ON(readl(mxic->regs + INT_STS) & INT_RX_NOT_EMPTY);

		pos += nbytes;
	}

	return 0;
}

static ssize_t mxic_spi_mem_dirmap_read(struct spi_mem_dirmap_desc *desc,
					u64 offs, size_t len, void *buf)
{
	struct mxic_spi *mxic = spi_controller_get_devdata(desc->mem->spi->controller);
	int ret;
	u32 sts;

	if (WARN_ON(offs + desc->info.offset + len > U32_MAX))
		return -EINVAL;

	writel(mxic_spi_prep_hc_cfg(desc->mem->spi, 0), mxic->regs + HC_CFG);

	writel(mxic_spi_mem_prep_op_cfg(&desc->info.op_tmpl, len),
	       mxic->regs + LRD_CFG);
	writel(desc->info.offset + offs, mxic->regs + LRD_ADDR);
	len = min_t(size_t, len, mxic->linear.size);
	writel(len, mxic->regs + LRD_RANGE);
	writel(LMODE_CMD0(desc->info.op_tmpl.cmd.opcode) |
	       LMODE_SLV_ACT(spi_get_chipselect(desc->mem->spi, 0)) |
	       LMODE_EN,
	       mxic->regs + LRD_CTRL);

	if (mxic->ecc.use_pipelined_conf && desc->info.op_tmpl.data.ecc) {
		ret = mxic_ecc_process_data_pipelined(mxic->ecc.pipelined_engine,
						      NAND_PAGE_READ,
						      mxic->linear.dma + offs);
		if (ret)
			return ret;
	} else {
		memcpy_fromio(buf, mxic->linear.map, len);
	}

	writel(INT_LRD_DIS, mxic->regs + INT_STS);
	writel(0, mxic->regs + LRD_CTRL);

	ret = readl_poll_timeout(mxic->regs + INT_STS, sts,
				 sts & INT_LRD_DIS, 0, USEC_PER_SEC);
	if (ret)
		return ret;

	return len;
}

static ssize_t mxic_spi_mem_dirmap_write(struct spi_mem_dirmap_desc *desc,
					 u64 offs, size_t len,
					 const void *buf)
{
	struct mxic_spi *mxic = spi_controller_get_devdata(desc->mem->spi->controller);
	u32 sts;
	int ret;

	if (WARN_ON(offs + desc->info.offset + len > U32_MAX))
		return -EINVAL;

	writel(mxic_spi_prep_hc_cfg(desc->mem->spi, 0), mxic->regs + HC_CFG);

	writel(mxic_spi_mem_prep_op_cfg(&desc->info.op_tmpl, len),
	       mxic->regs + LWR_CFG);
	writel(desc->info.offset + offs, mxic->regs + LWR_ADDR);
	len = min_t(size_t, len, mxic->linear.size);
	writel(len, mxic->regs + LWR_RANGE);
	writel(LMODE_CMD0(desc->info.op_tmpl.cmd.opcode) |
	       LMODE_SLV_ACT(spi_get_chipselect(desc->mem->spi, 0)) |
	       LMODE_EN,
	       mxic->regs + LWR_CTRL);

	if (mxic->ecc.use_pipelined_conf && desc->info.op_tmpl.data.ecc) {
		ret = mxic_ecc_process_data_pipelined(mxic->ecc.pipelined_engine,
						      NAND_PAGE_WRITE,
						      mxic->linear.dma + offs);
		if (ret)
			return ret;
	} else {
		memcpy_toio(mxic->linear.map, buf, len);
	}

	writel(INT_LWR_DIS, mxic->regs + INT_STS);
	writel(0, mxic->regs + LWR_CTRL);

	ret = readl_poll_timeout(mxic->regs + INT_STS, sts,
				 sts & INT_LWR_DIS, 0, USEC_PER_SEC);
	if (ret)
		return ret;

	return len;
}

static bool mxic_spi_mem_supports_op(struct spi_mem *mem,
				     const struct spi_mem_op *op)
{
	if (op->data.buswidth > 8 || op->addr.buswidth > 8 ||
	    op->dummy.buswidth > 8 || op->cmd.buswidth > 8)
		return false;

	if (op->data.nbytes && op->dummy.nbytes &&
	    op->data.buswidth != op->dummy.buswidth)
		return false;

	if (op->addr.nbytes > 7)
		return false;

	return spi_mem_default_supports_op(mem, op);
}

static int mxic_spi_mem_dirmap_create(struct spi_mem_dirmap_desc *desc)
{
	struct mxic_spi *mxic = spi_controller_get_devdata(desc->mem->spi->controller);

	if (!mxic->linear.map)
		return -EINVAL;

	if (desc->info.offset + desc->info.length > U32_MAX)
		return -EINVAL;

	if (!mxic_spi_mem_supports_op(desc->mem, &desc->info.op_tmpl))
		return -EOPNOTSUPP;

	return 0;
}

static int mxic_spi_mem_exec_op(struct spi_mem *mem,
				const struct spi_mem_op *op)
{
	struct mxic_spi *mxic = spi_controller_get_devdata(mem->spi->controller);
	int i, ret;
	u8 addr[8], cmd[2];

	ret = mxic_spi_set_freq(mxic, mem->spi->max_speed_hz);
	if (ret)
		return ret;

	writel(mxic_spi_prep_hc_cfg(mem->spi, HC_CFG_MAN_CS_EN),
	       mxic->regs + HC_CFG);

	writel(HC_EN_BIT, mxic->regs + HC_EN);

	writel(mxic_spi_mem_prep_op_cfg(op, op->data.nbytes),
	       mxic->regs + SS_CTRL(spi_get_chipselect(mem->spi, 0)));

	writel(readl(mxic->regs + HC_CFG) | HC_CFG_MAN_CS_ASSERT,
	       mxic->regs + HC_CFG);

	for (i = 0; i < op->cmd.nbytes; i++)
		cmd[i] = op->cmd.opcode >> (8 * (op->cmd.nbytes - i - 1));

	ret = mxic_spi_data_xfer(mxic, cmd, NULL, op->cmd.nbytes);
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
	.dirmap_create = mxic_spi_mem_dirmap_create,
	.dirmap_read = mxic_spi_mem_dirmap_read,
	.dirmap_write = mxic_spi_mem_dirmap_write,
};

static const struct spi_controller_mem_caps mxic_spi_mem_caps = {
	.dtr = true,
	.ecc = true,
};

static void mxic_spi_set_cs(struct spi_device *spi, bool lvl)
{
	struct mxic_spi *mxic = spi_controller_get_devdata(spi->controller);

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

static int mxic_spi_transfer_one(struct spi_controller *host,
				 struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct mxic_spi *mxic = spi_controller_get_devdata(host);
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

	spi_finalize_current_transfer(host);

	return 0;
}

/* ECC wrapper */
static int mxic_spi_mem_ecc_init_ctx(struct nand_device *nand)
{
	struct nand_ecc_engine_ops *ops = mxic_ecc_get_pipelined_ops();
	struct mxic_spi *mxic = nand->ecc.engine->priv;

	mxic->ecc.use_pipelined_conf = true;

	return ops->init_ctx(nand);
}

static void mxic_spi_mem_ecc_cleanup_ctx(struct nand_device *nand)
{
	struct nand_ecc_engine_ops *ops = mxic_ecc_get_pipelined_ops();
	struct mxic_spi *mxic = nand->ecc.engine->priv;

	mxic->ecc.use_pipelined_conf = false;

	ops->cleanup_ctx(nand);
}

static int mxic_spi_mem_ecc_prepare_io_req(struct nand_device *nand,
					   struct nand_page_io_req *req)
{
	struct nand_ecc_engine_ops *ops = mxic_ecc_get_pipelined_ops();

	return ops->prepare_io_req(nand, req);
}

static int mxic_spi_mem_ecc_finish_io_req(struct nand_device *nand,
					  struct nand_page_io_req *req)
{
	struct nand_ecc_engine_ops *ops = mxic_ecc_get_pipelined_ops();

	return ops->finish_io_req(nand, req);
}

static struct nand_ecc_engine_ops mxic_spi_mem_ecc_engine_pipelined_ops = {
	.init_ctx = mxic_spi_mem_ecc_init_ctx,
	.cleanup_ctx = mxic_spi_mem_ecc_cleanup_ctx,
	.prepare_io_req = mxic_spi_mem_ecc_prepare_io_req,
	.finish_io_req = mxic_spi_mem_ecc_finish_io_req,
};

static void mxic_spi_mem_ecc_remove(struct mxic_spi *mxic)
{
	if (mxic->ecc.pipelined_engine) {
		mxic_ecc_put_pipelined_engine(mxic->ecc.pipelined_engine);
		nand_ecc_unregister_on_host_hw_engine(mxic->ecc.pipelined_engine);
	}
}

static int mxic_spi_mem_ecc_probe(struct platform_device *pdev,
				  struct mxic_spi *mxic)
{
	struct nand_ecc_engine *eng;

	if (!mxic_ecc_get_pipelined_ops())
		return -EOPNOTSUPP;

	eng = mxic_ecc_get_pipelined_engine(pdev);
	if (IS_ERR(eng))
		return PTR_ERR(eng);

	eng->dev = &pdev->dev;
	eng->integration = NAND_ECC_ENGINE_INTEGRATION_PIPELINED;
	eng->ops = &mxic_spi_mem_ecc_engine_pipelined_ops;
	eng->priv = mxic;
	mxic->ecc.pipelined_engine = eng;
	nand_ecc_register_on_host_hw_engine(eng);

	return 0;
}

static int __maybe_unused mxic_spi_runtime_suspend(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mxic_spi *mxic = spi_controller_get_devdata(host);

	mxic_spi_clk_disable(mxic);
	clk_disable_unprepare(mxic->ps_clk);

	return 0;
}

static int __maybe_unused mxic_spi_runtime_resume(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mxic_spi *mxic = spi_controller_get_devdata(host);
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
	struct spi_controller *host;
	struct resource *res;
	struct mxic_spi *mxic;
	int ret;

	host = devm_spi_alloc_host(&pdev->dev, sizeof(struct mxic_spi));
	if (!host)
		return -ENOMEM;

	platform_set_drvdata(pdev, host);

	mxic = spi_controller_get_devdata(host);
	mxic->dev = &pdev->dev;

	host->dev.of_node = pdev->dev.of_node;

	mxic->ps_clk = devm_clk_get(&pdev->dev, "ps_clk");
	if (IS_ERR(mxic->ps_clk))
		return PTR_ERR(mxic->ps_clk);

	mxic->send_clk = devm_clk_get(&pdev->dev, "send_clk");
	if (IS_ERR(mxic->send_clk))
		return PTR_ERR(mxic->send_clk);

	mxic->send_dly_clk = devm_clk_get(&pdev->dev, "send_dly_clk");
	if (IS_ERR(mxic->send_dly_clk))
		return PTR_ERR(mxic->send_dly_clk);

	mxic->regs = devm_platform_ioremap_resource_byname(pdev, "regs");
	if (IS_ERR(mxic->regs))
		return PTR_ERR(mxic->regs);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dirmap");
	mxic->linear.map = devm_ioremap_resource(&pdev->dev, res);
	if (!IS_ERR(mxic->linear.map)) {
		mxic->linear.dma = res->start;
		mxic->linear.size = resource_size(res);
	} else {
		mxic->linear.map = NULL;
	}

	pm_runtime_enable(&pdev->dev);
	host->auto_runtime_pm = true;

	host->num_chipselect = 1;
	host->mem_ops = &mxic_spi_mem_ops;
	host->mem_caps = &mxic_spi_mem_caps;

	host->set_cs = mxic_spi_set_cs;
	host->transfer_one = mxic_spi_transfer_one;
	host->bits_per_word_mask = SPI_BPW_MASK(8);
	host->mode_bits = SPI_CPOL | SPI_CPHA |
			  SPI_RX_DUAL | SPI_TX_DUAL |
			  SPI_RX_QUAD | SPI_TX_QUAD |
			  SPI_RX_OCTAL | SPI_TX_OCTAL;

	mxic_spi_hw_init(mxic);

	ret = mxic_spi_mem_ecc_probe(pdev, mxic);
	if (ret == -EPROBE_DEFER) {
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	ret = spi_register_controller(host);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_controller failed\n");
		pm_runtime_disable(&pdev->dev);
		mxic_spi_mem_ecc_remove(mxic);
	}

	return ret;
}

static void mxic_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *host = platform_get_drvdata(pdev);
	struct mxic_spi *mxic = spi_controller_get_devdata(host);

	pm_runtime_disable(&pdev->dev);
	mxic_spi_mem_ecc_remove(mxic);
	spi_unregister_controller(host);
}

static const struct of_device_id mxic_spi_of_ids[] = {
	{ .compatible = "mxicy,mx25f0a-spi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxic_spi_of_ids);

static struct platform_driver mxic_spi_driver = {
	.probe = mxic_spi_probe,
	.remove_new = mxic_spi_remove,
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
