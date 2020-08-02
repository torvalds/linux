/*
 * Driver for EIP97 cryptographic accelerator.
 *
 * Copyright (c) 2016 Ryder Lee <ryder.lee@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include "mtk-platform.h"

#define MTK_BURST_SIZE_MSK		GENMASK(7, 4)
#define MTK_BURST_SIZE(x)		((x) << 4)
#define MTK_DESC_SIZE(x)		((x) << 0)
#define MTK_DESC_OFFSET(x)		((x) << 16)
#define MTK_DESC_FETCH_SIZE(x)		((x) << 0)
#define MTK_DESC_FETCH_THRESH(x)	((x) << 16)
#define MTK_DESC_OVL_IRQ_EN		BIT(25)
#define MTK_DESC_ATP_PRESENT		BIT(30)

#define MTK_DFSE_IDLE			GENMASK(3, 0)
#define MTK_DFSE_THR_CTRL_EN		BIT(30)
#define MTK_DFSE_THR_CTRL_RESET		BIT(31)
#define MTK_DFSE_RING_ID(x)		(((x) >> 12) & GENMASK(3, 0))
#define MTK_DFSE_MIN_DATA(x)		((x) << 0)
#define MTK_DFSE_MAX_DATA(x)		((x) << 8)
#define MTK_DFE_MIN_CTRL(x)		((x) << 16)
#define MTK_DFE_MAX_CTRL(x)		((x) << 24)

#define MTK_IN_BUF_MIN_THRESH(x)	((x) << 8)
#define MTK_IN_BUF_MAX_THRESH(x)	((x) << 12)
#define MTK_OUT_BUF_MIN_THRESH(x)	((x) << 0)
#define MTK_OUT_BUF_MAX_THRESH(x)	((x) << 4)
#define MTK_IN_TBUF_SIZE(x)		(((x) >> 4) & GENMASK(3, 0))
#define MTK_IN_DBUF_SIZE(x)		(((x) >> 8) & GENMASK(3, 0))
#define MTK_OUT_DBUF_SIZE(x)		(((x) >> 16) & GENMASK(3, 0))
#define MTK_CMD_FIFO_SIZE(x)		(((x) >> 8) & GENMASK(3, 0))
#define MTK_RES_FIFO_SIZE(x)		(((x) >> 12) & GENMASK(3, 0))

#define MTK_PE_TK_LOC_AVL		BIT(2)
#define MTK_PE_PROC_HELD		BIT(14)
#define MTK_PE_TK_TIMEOUT_EN		BIT(22)
#define MTK_PE_INPUT_DMA_ERR		BIT(0)
#define MTK_PE_OUTPUT_DMA_ERR		BIT(1)
#define MTK_PE_PKT_PORC_ERR		BIT(2)
#define MTK_PE_PKT_TIMEOUT		BIT(3)
#define MTK_PE_FATAL_ERR		BIT(14)
#define MTK_PE_INPUT_DMA_ERR_EN		BIT(16)
#define MTK_PE_OUTPUT_DMA_ERR_EN	BIT(17)
#define MTK_PE_PKT_PORC_ERR_EN		BIT(18)
#define MTK_PE_PKT_TIMEOUT_EN		BIT(19)
#define MTK_PE_FATAL_ERR_EN		BIT(30)
#define MTK_PE_INT_OUT_EN		BIT(31)

#define MTK_HIA_SIGNATURE		((u16)0x35ca)
#define MTK_HIA_DATA_WIDTH(x)		(((x) >> 25) & GENMASK(1, 0))
#define MTK_HIA_DMA_LENGTH(x)		(((x) >> 20) & GENMASK(4, 0))
#define MTK_CDR_STAT_CLR		GENMASK(4, 0)
#define MTK_RDR_STAT_CLR		GENMASK(7, 0)

#define MTK_AIC_INT_MSK			GENMASK(5, 0)
#define MTK_AIC_VER_MSK			(GENMASK(15, 0) | GENMASK(27, 20))
#define MTK_AIC_VER11			0x011036c9
#define MTK_AIC_VER12			0x012036c9
#define MTK_AIC_G_CLR			GENMASK(30, 20)

/**
 * EIP97 is an integrated security subsystem to accelerate cryptographic
 * functions and protocols to offload the host processor.
 * Some important hardware modules are briefly introduced below:
 *
 * Host Interface Adapter(HIA) - the main interface between the host
 * system and the hardware subsystem. It is responsible for attaching
 * processing engine to the specific host bus interface and provides a
 * standardized software view for off loading tasks to the engine.
 *
 * Command Descriptor Ring Manager(CDR Manager) - keeps track of how many
 * CD the host has prepared in the CDR. It monitors the fill level of its
 * CD-FIFO and if there's sufficient space for the next block of descriptors,
 * then it fires off a DMA request to fetch a block of CDs.
 *
 * Data fetch engine(DFE) - It is responsible for parsing the CD and
 * setting up the required control and packet data DMA transfers from
 * system memory to the processing engine.
 *
 * Result Descriptor Ring Manager(RDR Manager) - same as CDR Manager,
 * but target is result descriptors, Moreover, it also handles the RD
 * updates under control of the DSE. For each packet data segment
 * processed, the DSE triggers the RDR Manager to write the updated RD.
 * If triggered to update, the RDR Manager sets up a DMA operation to
 * copy the RD from the DSE to the correct location in the RDR.
 *
 * Data Store Engine(DSE) - It is responsible for parsing the prepared RD
 * and setting up the required control and packet data DMA transfers from
 * the processing engine to system memory.
 *
 * Advanced Interrupt Controllers(AICs) - receive interrupt request signals
 * from various sources and combine them into one interrupt output.
 * The AICs are used by:
 * - One for the HIA global and processing engine interrupts.
 * - The others for the descriptor ring interrupts.
 */

/* Cryptographic engine capabilities */
struct mtk_sys_cap {
	/* host interface adapter */
	u32 hia_ver;
	u32 hia_opt;
	/* packet engine */
	u32 pkt_eng_opt;
	/* global hardware */
	u32 hw_opt;
};

static void mtk_desc_ring_link(struct mtk_cryp *cryp, u32 mask)
{
	/* Assign rings to DFE/DSE thread and enable it */
	writel(MTK_DFSE_THR_CTRL_EN | mask, cryp->base + DFE_THR_CTRL);
	writel(MTK_DFSE_THR_CTRL_EN | mask, cryp->base + DSE_THR_CTRL);
}

static void mtk_dfe_dse_buf_setup(struct mtk_cryp *cryp,
				  struct mtk_sys_cap *cap)
{
	u32 width = MTK_HIA_DATA_WIDTH(cap->hia_opt) + 2;
	u32 len = MTK_HIA_DMA_LENGTH(cap->hia_opt) - 1;
	u32 ipbuf = min((u32)MTK_IN_DBUF_SIZE(cap->hw_opt) + width, len);
	u32 opbuf = min((u32)MTK_OUT_DBUF_SIZE(cap->hw_opt) + width, len);
	u32 itbuf = min((u32)MTK_IN_TBUF_SIZE(cap->hw_opt) + width, len);

	writel(MTK_DFSE_MIN_DATA(ipbuf - 1) |
	       MTK_DFSE_MAX_DATA(ipbuf) |
	       MTK_DFE_MIN_CTRL(itbuf - 1) |
	       MTK_DFE_MAX_CTRL(itbuf),
	       cryp->base + DFE_CFG);

	writel(MTK_DFSE_MIN_DATA(opbuf - 1) |
	       MTK_DFSE_MAX_DATA(opbuf),
	       cryp->base + DSE_CFG);

	writel(MTK_IN_BUF_MIN_THRESH(ipbuf - 1) |
	       MTK_IN_BUF_MAX_THRESH(ipbuf),
	       cryp->base + PE_IN_DBUF_THRESH);

	writel(MTK_IN_BUF_MIN_THRESH(itbuf - 1) |
	       MTK_IN_BUF_MAX_THRESH(itbuf),
	       cryp->base + PE_IN_TBUF_THRESH);

	writel(MTK_OUT_BUF_MIN_THRESH(opbuf - 1) |
	       MTK_OUT_BUF_MAX_THRESH(opbuf),
	       cryp->base + PE_OUT_DBUF_THRESH);

	writel(0, cryp->base + PE_OUT_TBUF_THRESH);
	writel(0, cryp->base + PE_OUT_BUF_CTRL);
}

static int mtk_dfe_dse_state_check(struct mtk_cryp *cryp)
{
	int ret = -EINVAL;
	u32 val;

	/* Check for completion of all DMA transfers */
	val = readl(cryp->base + DFE_THR_STAT);
	if (MTK_DFSE_RING_ID(val) == MTK_DFSE_IDLE) {
		val = readl(cryp->base + DSE_THR_STAT);
		if (MTK_DFSE_RING_ID(val) == MTK_DFSE_IDLE)
			ret = 0;
	}

	if (!ret) {
		/* Take DFE/DSE thread out of reset */
		writel(0, cryp->base + DFE_THR_CTRL);
		writel(0, cryp->base + DSE_THR_CTRL);
	} else {
		return -EBUSY;
	}

	return 0;
}

static int mtk_dfe_dse_reset(struct mtk_cryp *cryp)
{
	int err;

	/* Reset DSE/DFE and correct system priorities for all rings. */
	writel(MTK_DFSE_THR_CTRL_RESET, cryp->base + DFE_THR_CTRL);
	writel(0, cryp->base + DFE_PRIO_0);
	writel(0, cryp->base + DFE_PRIO_1);
	writel(0, cryp->base + DFE_PRIO_2);
	writel(0, cryp->base + DFE_PRIO_3);

	writel(MTK_DFSE_THR_CTRL_RESET, cryp->base + DSE_THR_CTRL);
	writel(0, cryp->base + DSE_PRIO_0);
	writel(0, cryp->base + DSE_PRIO_1);
	writel(0, cryp->base + DSE_PRIO_2);
	writel(0, cryp->base + DSE_PRIO_3);

	err = mtk_dfe_dse_state_check(cryp);
	if (err)
		return err;

	return 0;
}

static void mtk_cmd_desc_ring_setup(struct mtk_cryp *cryp,
				    int i, struct mtk_sys_cap *cap)
{
	/* Full descriptor that fits FIFO minus one */
	u32 count =
		((1 << MTK_CMD_FIFO_SIZE(cap->hia_opt)) / MTK_DESC_SZ) - 1;

	/* Temporarily disable external triggering */
	writel(0, cryp->base + CDR_CFG(i));

	/* Clear CDR count */
	writel(MTK_CNT_RST, cryp->base + CDR_PREP_COUNT(i));
	writel(MTK_CNT_RST, cryp->base + CDR_PROC_COUNT(i));

	writel(0, cryp->base + CDR_PREP_PNTR(i));
	writel(0, cryp->base + CDR_PROC_PNTR(i));
	writel(0, cryp->base + CDR_DMA_CFG(i));

	/* Configure CDR host address space */
	writel(0, cryp->base + CDR_BASE_ADDR_HI(i));
	writel(cryp->ring[i]->cmd_dma, cryp->base + CDR_BASE_ADDR_LO(i));

	writel(MTK_DESC_RING_SZ, cryp->base + CDR_RING_SIZE(i));

	/* Clear and disable all CDR interrupts */
	writel(MTK_CDR_STAT_CLR, cryp->base + CDR_STAT(i));

	/*
	 * Set command descriptor offset and enable additional
	 * token present in descriptor.
	 */
	writel(MTK_DESC_SIZE(MTK_DESC_SZ) |
		   MTK_DESC_OFFSET(MTK_DESC_OFF) |
	       MTK_DESC_ATP_PRESENT,
	       cryp->base + CDR_DESC_SIZE(i));

	writel(MTK_DESC_FETCH_SIZE(count * MTK_DESC_OFF) |
		   MTK_DESC_FETCH_THRESH(count * MTK_DESC_SZ),
		   cryp->base + CDR_CFG(i));
}

static void mtk_res_desc_ring_setup(struct mtk_cryp *cryp,
				    int i, struct mtk_sys_cap *cap)
{
	u32 rndup = 2;
	u32 count = ((1 << MTK_RES_FIFO_SIZE(cap->hia_opt)) / rndup) - 1;

	/* Temporarily disable external triggering */
	writel(0, cryp->base + RDR_CFG(i));

	/* Clear RDR count */
	writel(MTK_CNT_RST, cryp->base + RDR_PREP_COUNT(i));
	writel(MTK_CNT_RST, cryp->base + RDR_PROC_COUNT(i));

	writel(0, cryp->base + RDR_PREP_PNTR(i));
	writel(0, cryp->base + RDR_PROC_PNTR(i));
	writel(0, cryp->base + RDR_DMA_CFG(i));

	/* Configure RDR host address space */
	writel(0, cryp->base + RDR_BASE_ADDR_HI(i));
	writel(cryp->ring[i]->res_dma, cryp->base + RDR_BASE_ADDR_LO(i));

	writel(MTK_DESC_RING_SZ, cryp->base + RDR_RING_SIZE(i));
	writel(MTK_RDR_STAT_CLR, cryp->base + RDR_STAT(i));

	/*
	 * RDR manager generates update interrupts on a per-completed-packet,
	 * and the rd_proc_thresh_irq interrupt is fired when proc_pkt_count
	 * for the RDR exceeds the number of packets.
	 */
	writel(MTK_RDR_PROC_THRESH | MTK_RDR_PROC_MODE,
	       cryp->base + RDR_THRESH(i));

	/*
	 * Configure a threshold and time-out value for the processed
	 * result descriptors (or complete packets) that are written to
	 * the RDR.
	 */
	writel(MTK_DESC_SIZE(MTK_DESC_SZ) | MTK_DESC_OFFSET(MTK_DESC_OFF),
	       cryp->base + RDR_DESC_SIZE(i));

	/*
	 * Configure HIA fetch size and fetch threshold that are used to
	 * fetch blocks of multiple descriptors.
	 */
	writel(MTK_DESC_FETCH_SIZE(count * MTK_DESC_OFF) |
	       MTK_DESC_FETCH_THRESH(count * rndup) |
	       MTK_DESC_OVL_IRQ_EN,
		   cryp->base + RDR_CFG(i));
}

static int mtk_packet_engine_setup(struct mtk_cryp *cryp)
{
	struct mtk_sys_cap cap;
	int i, err;
	u32 val;

	cap.hia_ver = readl(cryp->base + HIA_VERSION);
	cap.hia_opt = readl(cryp->base + HIA_OPTIONS);
	cap.hw_opt = readl(cryp->base + EIP97_OPTIONS);

	if (!(((u16)cap.hia_ver) == MTK_HIA_SIGNATURE))
		return -EINVAL;

	/* Configure endianness conversion method for master (DMA) interface */
	writel(0, cryp->base + EIP97_MST_CTRL);

	/* Set HIA burst size */
	val = readl(cryp->base + HIA_MST_CTRL);
	val &= ~MTK_BURST_SIZE_MSK;
	val |= MTK_BURST_SIZE(5);
	writel(val, cryp->base + HIA_MST_CTRL);

	err = mtk_dfe_dse_reset(cryp);
	if (err) {
		dev_err(cryp->dev, "Failed to reset DFE and DSE.\n");
		return err;
	}

	mtk_dfe_dse_buf_setup(cryp, &cap);

	/* Enable the 4 rings for the packet engines. */
	mtk_desc_ring_link(cryp, 0xf);

	for (i = 0; i < MTK_RING_MAX; i++) {
		mtk_cmd_desc_ring_setup(cryp, i, &cap);
		mtk_res_desc_ring_setup(cryp, i, &cap);
	}

	writel(MTK_PE_TK_LOC_AVL | MTK_PE_PROC_HELD | MTK_PE_TK_TIMEOUT_EN,
	       cryp->base + PE_TOKEN_CTRL_STAT);

	/* Clear all pending interrupts */
	writel(MTK_AIC_G_CLR, cryp->base + AIC_G_ACK);
	writel(MTK_PE_INPUT_DMA_ERR | MTK_PE_OUTPUT_DMA_ERR |
	       MTK_PE_PKT_PORC_ERR | MTK_PE_PKT_TIMEOUT |
	       MTK_PE_FATAL_ERR | MTK_PE_INPUT_DMA_ERR_EN |
	       MTK_PE_OUTPUT_DMA_ERR_EN | MTK_PE_PKT_PORC_ERR_EN |
	       MTK_PE_PKT_TIMEOUT_EN | MTK_PE_FATAL_ERR_EN |
	       MTK_PE_INT_OUT_EN,
	       cryp->base + PE_INTERRUPT_CTRL_STAT);

	return 0;
}

static int mtk_aic_cap_check(struct mtk_cryp *cryp, int hw)
{
	u32 val;

	if (hw == MTK_RING_MAX)
		val = readl(cryp->base + AIC_G_VERSION);
	else
		val = readl(cryp->base + AIC_VERSION(hw));

	val &= MTK_AIC_VER_MSK;
	if (val != MTK_AIC_VER11 && val != MTK_AIC_VER12)
		return -ENXIO;

	if (hw == MTK_RING_MAX)
		val = readl(cryp->base + AIC_G_OPTIONS);
	else
		val = readl(cryp->base + AIC_OPTIONS(hw));

	val &= MTK_AIC_INT_MSK;
	if (!val || val > 32)
		return -ENXIO;

	return 0;
}

static int mtk_aic_init(struct mtk_cryp *cryp, int hw)
{
	int err;

	err = mtk_aic_cap_check(cryp, hw);
	if (err)
		return err;

	/* Disable all interrupts and set initial configuration */
	if (hw == MTK_RING_MAX) {
		writel(0, cryp->base + AIC_G_ENABLE_CTRL);
		writel(0, cryp->base + AIC_G_POL_CTRL);
		writel(0, cryp->base + AIC_G_TYPE_CTRL);
		writel(0, cryp->base + AIC_G_ENABLE_SET);
	} else {
		writel(0, cryp->base + AIC_ENABLE_CTRL(hw));
		writel(0, cryp->base + AIC_POL_CTRL(hw));
		writel(0, cryp->base + AIC_TYPE_CTRL(hw));
		writel(0, cryp->base + AIC_ENABLE_SET(hw));
	}

	return 0;
}

static int mtk_accelerator_init(struct mtk_cryp *cryp)
{
	int i, err;

	/* Initialize advanced interrupt controller(AIC) */
	for (i = 0; i < MTK_IRQ_NUM; i++) {
		err = mtk_aic_init(cryp, i);
		if (err) {
			dev_err(cryp->dev, "Failed to initialize AIC.\n");
			return err;
		}
	}

	/* Initialize packet engine */
	err = mtk_packet_engine_setup(cryp);
	if (err) {
		dev_err(cryp->dev, "Failed to configure packet engine.\n");
		return err;
	}

	return 0;
}

static void mtk_desc_dma_free(struct mtk_cryp *cryp)
{
	int i;

	for (i = 0; i < MTK_RING_MAX; i++) {
		dma_free_coherent(cryp->dev, MTK_DESC_RING_SZ,
				  cryp->ring[i]->res_base,
				  cryp->ring[i]->res_dma);
		dma_free_coherent(cryp->dev, MTK_DESC_RING_SZ,
				  cryp->ring[i]->cmd_base,
				  cryp->ring[i]->cmd_dma);
		kfree(cryp->ring[i]);
	}
}

static int mtk_desc_ring_alloc(struct mtk_cryp *cryp)
{
	struct mtk_ring **ring = cryp->ring;
	int i;

	for (i = 0; i < MTK_RING_MAX; i++) {
		ring[i] = kzalloc(sizeof(**ring), GFP_KERNEL);
		if (!ring[i])
			goto err_cleanup;

		ring[i]->cmd_base = dma_zalloc_coherent(cryp->dev,
					   MTK_DESC_RING_SZ,
					   &ring[i]->cmd_dma,
					   GFP_KERNEL);
		if (!ring[i]->cmd_base)
			goto err_cleanup;

		ring[i]->res_base = dma_zalloc_coherent(cryp->dev,
					   MTK_DESC_RING_SZ,
					   &ring[i]->res_dma,
					   GFP_KERNEL);
		if (!ring[i]->res_base)
			goto err_cleanup;

		ring[i]->cmd_next = ring[i]->cmd_base;
		ring[i]->res_next = ring[i]->res_base;
	}
	return 0;

err_cleanup:
	for (; i--; ) {
		dma_free_coherent(cryp->dev, MTK_DESC_RING_SZ,
				  ring[i]->res_base, ring[i]->res_dma);
		dma_free_coherent(cryp->dev, MTK_DESC_RING_SZ,
				  ring[i]->cmd_base, ring[i]->cmd_dma);
		kfree(ring[i]);
	}
	return -ENOMEM;
}

static int mtk_crypto_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct mtk_cryp *cryp;
	int i, err;

	cryp = devm_kzalloc(&pdev->dev, sizeof(*cryp), GFP_KERNEL);
	if (!cryp)
		return -ENOMEM;

	cryp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(cryp->base))
		return PTR_ERR(cryp->base);

	for (i = 0; i < MTK_IRQ_NUM; i++) {
		cryp->irq[i] = platform_get_irq(pdev, i);
		if (cryp->irq[i] < 0) {
			dev_err(cryp->dev, "no IRQ:%d resource info\n", i);
			return cryp->irq[i];
		}
	}

	cryp->clk_cryp = devm_clk_get(&pdev->dev, "cryp");
	if (IS_ERR(cryp->clk_cryp))
		return -EPROBE_DEFER;

	cryp->dev = &pdev->dev;
	pm_runtime_enable(cryp->dev);
	pm_runtime_get_sync(cryp->dev);

	err = clk_prepare_enable(cryp->clk_cryp);
	if (err)
		goto err_clk_cryp;

	/* Allocate four command/result descriptor rings */
	err = mtk_desc_ring_alloc(cryp);
	if (err) {
		dev_err(cryp->dev, "Unable to allocate descriptor rings.\n");
		goto err_resource;
	}

	/* Initialize hardware modules */
	err = mtk_accelerator_init(cryp);
	if (err) {
		dev_err(cryp->dev, "Failed to initialize cryptographic engine.\n");
		goto err_engine;
	}

	err = mtk_cipher_alg_register(cryp);
	if (err) {
		dev_err(cryp->dev, "Unable to register cipher algorithm.\n");
		goto err_cipher;
	}

	err = mtk_hash_alg_register(cryp);
	if (err) {
		dev_err(cryp->dev, "Unable to register hash algorithm.\n");
		goto err_hash;
	}

	platform_set_drvdata(pdev, cryp);
	return 0;

err_hash:
	mtk_cipher_alg_release(cryp);
err_cipher:
	mtk_dfe_dse_reset(cryp);
err_engine:
	mtk_desc_dma_free(cryp);
err_resource:
	clk_disable_unprepare(cryp->clk_cryp);
err_clk_cryp:
	pm_runtime_put_sync(cryp->dev);
	pm_runtime_disable(cryp->dev);

	return err;
}

static int mtk_crypto_remove(struct platform_device *pdev)
{
	struct mtk_cryp *cryp = platform_get_drvdata(pdev);

	mtk_hash_alg_release(cryp);
	mtk_cipher_alg_release(cryp);
	mtk_desc_dma_free(cryp);

	clk_disable_unprepare(cryp->clk_cryp);

	pm_runtime_put_sync(cryp->dev);
	pm_runtime_disable(cryp->dev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id of_crypto_id[] = {
	{ .compatible = "mediatek,eip97-crypto" },
	{},
};
MODULE_DEVICE_TABLE(of, of_crypto_id);

static struct platform_driver mtk_crypto_driver = {
	.probe = mtk_crypto_probe,
	.remove = mtk_crypto_remove,
	.driver = {
		   .name = "mtk-crypto",
		   .of_match_table = of_crypto_id,
	},
};
module_platform_driver(mtk_crypto_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ryder Lee <ryder.lee@mediatek.com>");
MODULE_DESCRIPTION("Cryptographic accelerator driver for EIP97");
