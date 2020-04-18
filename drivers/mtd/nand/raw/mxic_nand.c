// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Macronix International Co., Ltd.
 *
 * Author:
 *	Mason Yang <masonccyang@mxic.com.tw>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/platform_device.h>

#include "internals.h"

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

#define MXIC_NFC_MAX_CLK_HZ	50000000
#define IRQ_TIMEOUT		1000

struct mxic_nand_ctlr {
	struct clk *ps_clk;
	struct clk *send_clk;
	struct clk *send_dly_clk;
	struct completion complete;
	void __iomem *regs;
	struct nand_controller controller;
	struct device *dev;
	struct nand_chip chip;
};

static int mxic_nfc_clk_enable(struct mxic_nand_ctlr *nfc)
{
	int ret;

	ret = clk_prepare_enable(nfc->ps_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(nfc->send_clk);
	if (ret)
		goto err_ps_clk;

	ret = clk_prepare_enable(nfc->send_dly_clk);
	if (ret)
		goto err_send_dly_clk;

	return ret;

err_send_dly_clk:
	clk_disable_unprepare(nfc->send_clk);
err_ps_clk:
	clk_disable_unprepare(nfc->ps_clk);

	return ret;
}

static void mxic_nfc_clk_disable(struct mxic_nand_ctlr *nfc)
{
	clk_disable_unprepare(nfc->send_clk);
	clk_disable_unprepare(nfc->send_dly_clk);
	clk_disable_unprepare(nfc->ps_clk);
}

static void mxic_nfc_set_input_delay(struct mxic_nand_ctlr *nfc, u8 idly_code)
{
	writel(IDLY_CODE_VAL(0, idly_code) |
	       IDLY_CODE_VAL(1, idly_code) |
	       IDLY_CODE_VAL(2, idly_code) |
	       IDLY_CODE_VAL(3, idly_code),
	       nfc->regs + IDLY_CODE(0));
	writel(IDLY_CODE_VAL(4, idly_code) |
	       IDLY_CODE_VAL(5, idly_code) |
	       IDLY_CODE_VAL(6, idly_code) |
	       IDLY_CODE_VAL(7, idly_code),
	       nfc->regs + IDLY_CODE(1));
}

static int mxic_nfc_clk_setup(struct mxic_nand_ctlr *nfc, unsigned long freq)
{
	int ret;

	ret = clk_set_rate(nfc->send_clk, freq);
	if (ret)
		return ret;

	ret = clk_set_rate(nfc->send_dly_clk, freq);
	if (ret)
		return ret;

	/*
	 * A constant delay range from 0x0 ~ 0x1F for input delay,
	 * the unit is 78 ps, the max input delay is 2.418 ns.
	 */
	mxic_nfc_set_input_delay(nfc, 0xf);

	/*
	 * Phase degree = 360 * freq * output-delay
	 * where output-delay is a constant value 1 ns in FPGA.
	 *
	 * Get Phase degree = 360 * freq * 1 ns
	 *                  = 360 * freq * 1 sec / 1000000000
	 *                  = 9 * freq / 25000000
	 */
	ret = clk_set_phase(nfc->send_dly_clk, 9 * freq / 25000000);
	if (ret)
		return ret;

	return 0;
}

static int mxic_nfc_set_freq(struct mxic_nand_ctlr *nfc, unsigned long freq)
{
	int ret;

	if (freq > MXIC_NFC_MAX_CLK_HZ)
		freq = MXIC_NFC_MAX_CLK_HZ;

	mxic_nfc_clk_disable(nfc);
	ret = mxic_nfc_clk_setup(nfc, freq);
	if (ret)
		return ret;

	ret = mxic_nfc_clk_enable(nfc);
	if (ret)
		return ret;

	return 0;
}

static irqreturn_t mxic_nfc_isr(int irq, void *dev_id)
{
	struct mxic_nand_ctlr *nfc = dev_id;
	u32 sts;

	sts = readl(nfc->regs + INT_STS);
	if (sts & INT_RDY_PIN)
		complete(&nfc->complete);
	else
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static void mxic_nfc_hw_init(struct mxic_nand_ctlr *nfc)
{
	writel(HC_CFG_NIO(8) | HC_CFG_TYPE(1, HC_CFG_TYPE_RAW_NAND) |
	       HC_CFG_SLV_ACT(0) | HC_CFG_MAN_CS_EN |
	       HC_CFG_IDLE_SIO_LVL(1), nfc->regs + HC_CFG);
	writel(INT_STS_ALL, nfc->regs + INT_STS_EN);
	writel(INT_RDY_PIN, nfc->regs + INT_SIG_EN);
	writel(0x0, nfc->regs + ONFI_DIN_CNT(0));
	writel(0, nfc->regs + LRD_CFG);
	writel(0, nfc->regs + LRD_CTRL);
	writel(0x0, nfc->regs + HC_EN);
}

static void mxic_nfc_cs_enable(struct mxic_nand_ctlr *nfc)
{
	writel(readl(nfc->regs + HC_CFG) | HC_CFG_MAN_CS_EN,
	       nfc->regs + HC_CFG);
	writel(HC_CFG_MAN_CS_ASSERT | readl(nfc->regs + HC_CFG),
	       nfc->regs + HC_CFG);
}

static void mxic_nfc_cs_disable(struct mxic_nand_ctlr *nfc)
{
	writel(~HC_CFG_MAN_CS_ASSERT & readl(nfc->regs + HC_CFG),
	       nfc->regs + HC_CFG);
}

static int  mxic_nfc_wait_ready(struct nand_chip *chip)
{
	struct mxic_nand_ctlr *nfc = nand_get_controller_data(chip);
	int ret;

	ret = wait_for_completion_timeout(&nfc->complete,
					  msecs_to_jiffies(IRQ_TIMEOUT));
	if (!ret) {
		dev_err(nfc->dev, "nand device timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int mxic_nfc_data_xfer(struct mxic_nand_ctlr *nfc, const void *txbuf,
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

		ret = readl_poll_timeout(nfc->regs + INT_STS, sts,
					 sts & INT_TX_EMPTY, 0, USEC_PER_SEC);
		if (ret)
			return ret;

		writel(data, nfc->regs + TXD(nbytes % 4));

		ret = readl_poll_timeout(nfc->regs + INT_STS, sts,
					 sts & INT_TX_EMPTY, 0, USEC_PER_SEC);
		if (ret)
			return ret;

		ret = readl_poll_timeout(nfc->regs + INT_STS, sts,
					 sts & INT_RX_NOT_EMPTY, 0,
					 USEC_PER_SEC);
		if (ret)
			return ret;

		data = readl(nfc->regs + RXD);
		if (rxbuf) {
			data >>= (8 * (4 - nbytes));
			memcpy(rxbuf + pos, &data, nbytes);
		}
		if (readl(nfc->regs + INT_STS) & INT_RX_NOT_EMPTY)
			dev_warn(nfc->dev, "RX FIFO not empty\n");

		pos += nbytes;
	}

	return 0;
}

static int mxic_nfc_exec_op(struct nand_chip *chip,
			    const struct nand_operation *op, bool check_only)
{
	struct mxic_nand_ctlr *nfc = nand_get_controller_data(chip);
	const struct nand_op_instr *instr = NULL;
	int ret = 0;
	unsigned int op_id;

	if (check_only)
		return 0;

	mxic_nfc_cs_enable(nfc);
	init_completion(&nfc->complete);
	for (op_id = 0; op_id < op->ninstrs; op_id++) {
		instr = &op->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			writel(0, nfc->regs + HC_EN);
			writel(HC_EN_BIT, nfc->regs + HC_EN);
			writel(OP_CMD_BUSW(OP_BUSW_8) |  OP_DUMMY_CYC(0x3F) |
			       OP_CMD_BYTES(0), nfc->regs + SS_CTRL(0));

			ret = mxic_nfc_data_xfer(nfc,
						 &instr->ctx.cmd.opcode,
						 NULL, 1);
			break;

		case NAND_OP_ADDR_INSTR:
			writel(OP_ADDR_BUSW(OP_BUSW_8) | OP_DUMMY_CYC(0x3F) |
			       OP_ADDR_BYTES(instr->ctx.addr.naddrs),
			       nfc->regs + SS_CTRL(0));
			ret = mxic_nfc_data_xfer(nfc,
						 instr->ctx.addr.addrs, NULL,
						 instr->ctx.addr.naddrs);
			break;

		case NAND_OP_DATA_IN_INSTR:
			writel(0x0, nfc->regs + ONFI_DIN_CNT(0));
			writel(OP_DATA_BUSW(OP_BUSW_8) | OP_DUMMY_CYC(0x3F) |
			       OP_READ, nfc->regs + SS_CTRL(0));
			ret = mxic_nfc_data_xfer(nfc, NULL,
						 instr->ctx.data.buf.in,
						 instr->ctx.data.len);
			break;

		case NAND_OP_DATA_OUT_INSTR:
			writel(instr->ctx.data.len,
			       nfc->regs + ONFI_DIN_CNT(0));
			writel(OP_DATA_BUSW(OP_BUSW_8) | OP_DUMMY_CYC(0x3F),
			       nfc->regs + SS_CTRL(0));
			ret = mxic_nfc_data_xfer(nfc,
						 instr->ctx.data.buf.out, NULL,
						 instr->ctx.data.len);
			break;

		case NAND_OP_WAITRDY_INSTR:
			ret = mxic_nfc_wait_ready(chip);
			break;
		}
	}
	mxic_nfc_cs_disable(nfc);

	return ret;
}

static int mxic_nfc_setup_data_interface(struct nand_chip *chip, int chipnr,
					 const struct nand_data_interface *conf)
{
	struct mxic_nand_ctlr *nfc = nand_get_controller_data(chip);
	const struct nand_sdr_timings *sdr;
	unsigned long freq;
	int ret;

	sdr = nand_get_sdr_timings(conf);
	if (IS_ERR(sdr))
		return PTR_ERR(sdr);

	if (chipnr == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	freq = NSEC_PER_SEC / (sdr->tRC_min / 1000);

	ret =  mxic_nfc_set_freq(nfc, freq);
	if (ret)
		dev_err(nfc->dev, "set freq:%ld failed\n", freq);

	if (sdr->tRC_min < 30000)
		writel(DATA_STROB_EDO_EN, nfc->regs + DATA_STROB);

	return 0;
}

static const struct nand_controller_ops mxic_nand_controller_ops = {
	.exec_op = mxic_nfc_exec_op,
	.setup_data_interface = mxic_nfc_setup_data_interface,
};

static int mxic_nfc_probe(struct platform_device *pdev)
{
	struct device_node *nand_np, *np = pdev->dev.of_node;
	struct mtd_info *mtd;
	struct mxic_nand_ctlr *nfc;
	struct nand_chip *nand_chip;
	int err;
	int irq;

	nfc = devm_kzalloc(&pdev->dev, sizeof(struct mxic_nand_ctlr),
			   GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nfc->ps_clk = devm_clk_get(&pdev->dev, "ps");
	if (IS_ERR(nfc->ps_clk))
		return PTR_ERR(nfc->ps_clk);

	nfc->send_clk = devm_clk_get(&pdev->dev, "send");
	if (IS_ERR(nfc->send_clk))
		return PTR_ERR(nfc->send_clk);

	nfc->send_dly_clk = devm_clk_get(&pdev->dev, "send_dly");
	if (IS_ERR(nfc->send_dly_clk))
		return PTR_ERR(nfc->send_dly_clk);

	nfc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(nfc->regs))
		return PTR_ERR(nfc->regs);

	nand_chip = &nfc->chip;
	mtd = nand_to_mtd(nand_chip);
	mtd->dev.parent = &pdev->dev;

	for_each_child_of_node(np, nand_np)
		nand_set_flash_node(nand_chip, nand_np);

	nand_chip->priv = nfc;
	nfc->dev = &pdev->dev;
	nfc->controller.ops = &mxic_nand_controller_ops;
	nand_controller_init(&nfc->controller);
	nand_chip->controller = &nfc->controller;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	mxic_nfc_hw_init(nfc);

	err = devm_request_irq(&pdev->dev, irq, mxic_nfc_isr,
			       0, "mxic-nfc", nfc);
	if (err)
		goto fail;

	err = nand_scan(nand_chip, 1);
	if (err)
		goto fail;

	err = mtd_device_register(mtd, NULL, 0);
	if (err)
		goto fail;

	platform_set_drvdata(pdev, nfc);
	return 0;

fail:
	mxic_nfc_clk_disable(nfc);
	return err;
}

static int mxic_nfc_remove(struct platform_device *pdev)
{
	struct mxic_nand_ctlr *nfc = platform_get_drvdata(pdev);

	nand_release(&nfc->chip);
	mxic_nfc_clk_disable(nfc);
	return 0;
}

static const struct of_device_id mxic_nfc_of_ids[] = {
	{ .compatible = "mxic,multi-itfc-v009-nand-controller", },
	{},
};
MODULE_DEVICE_TABLE(of, mxic_nfc_of_ids);

static struct platform_driver mxic_nfc_driver = {
	.probe = mxic_nfc_probe,
	.remove = mxic_nfc_remove,
	.driver = {
		.name = "mxic-nfc",
		.of_match_table = mxic_nfc_of_ids,
	},
};
module_platform_driver(mxic_nfc_driver);

MODULE_AUTHOR("Mason Yang <masonccyang@mxic.com.tw>");
MODULE_DESCRIPTION("Macronix raw NAND controller driver");
MODULE_LICENSE("GPL v2");
