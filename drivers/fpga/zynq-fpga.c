/*
 * Copyright (c) 2011-2015 Xilinx Inc.
 * Copyright (c) 2015, National Instruments Corp.
 *
 * FPGA Manager Driver for Xilinx Zynq, heavily based on xdevcfg driver
 * in their vendor tree.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/string.h>

/* Offsets into SLCR regmap */

/* FPGA Software Reset Control */
#define SLCR_FPGA_RST_CTRL_OFFSET	0x240
/* Level Shifters Enable */
#define SLCR_LVL_SHFTR_EN_OFFSET	0x900

/* Constant Definitions */

/* Control Register */
#define CTRL_OFFSET			0x00
/* Lock Register */
#define LOCK_OFFSET			0x04
/* Interrupt Status Register */
#define INT_STS_OFFSET			0x0c
/* Interrupt Mask Register */
#define INT_MASK_OFFSET			0x10
/* Status Register */
#define STATUS_OFFSET			0x14
/* DMA Source Address Register */
#define DMA_SRC_ADDR_OFFSET		0x18
/* DMA Destination Address Reg */
#define DMA_DST_ADDR_OFFSET		0x1c
/* DMA Source Transfer Length */
#define DMA_SRC_LEN_OFFSET		0x20
/* DMA Destination Transfer */
#define DMA_DEST_LEN_OFFSET		0x24
/* Unlock Register */
#define UNLOCK_OFFSET			0x34
/* Misc. Control Register */
#define MCTRL_OFFSET			0x80

/* Control Register Bit definitions */

/* Signal to reset FPGA */
#define CTRL_PCFG_PROG_B_MASK		BIT(30)
/* Enable PCAP for PR */
#define CTRL_PCAP_PR_MASK		BIT(27)
/* Enable PCAP */
#define CTRL_PCAP_MODE_MASK		BIT(26)

/* Miscellaneous Control Register bit definitions */
/* Internal PCAP loopback */
#define MCTRL_PCAP_LPBK_MASK		BIT(4)

/* Status register bit definitions */

/* FPGA init status */
#define STATUS_DMA_Q_F			BIT(31)
#define STATUS_PCFG_INIT_MASK		BIT(4)

/* Interrupt Status/Mask Register Bit definitions */
/* DMA command done */
#define IXR_DMA_DONE_MASK		BIT(13)
/* DMA and PCAP cmd done */
#define IXR_D_P_DONE_MASK		BIT(12)
 /* FPGA programmed */
#define IXR_PCFG_DONE_MASK		BIT(2)
#define IXR_ERROR_FLAGS_MASK		0x00F0F860
#define IXR_ALL_MASK			0xF8F7F87F

/* Miscellaneous constant values */

/* Invalid DMA addr */
#define DMA_INVALID_ADDRESS		GENMASK(31, 0)
/* Used to unlock the dev */
#define UNLOCK_MASK			0x757bdf0d
/* Timeout for DMA to complete */
#define DMA_DONE_TIMEOUT		msecs_to_jiffies(1000)
/* Timeout for polling reset bits */
#define INIT_POLL_TIMEOUT		2500000
/* Delay for polling reset bits */
#define INIT_POLL_DELAY			20

/* Masks for controlling stuff in SLCR */
/* Disable all Level shifters */
#define LVL_SHFTR_DISABLE_ALL_MASK	0x0
/* Enable Level shifters from PS to PL */
#define LVL_SHFTR_ENABLE_PS_TO_PL	0xa
/* Enable Level shifters from PL to PS */
#define LVL_SHFTR_ENABLE_PL_TO_PS	0xf
/* Enable global resets */
#define FPGA_RST_ALL_MASK		0xf
/* Disable global resets */
#define FPGA_RST_NONE_MASK		0x0

struct zynq_fpga_priv {
	struct device *dev;
	int irq;
	struct clk *clk;

	void __iomem *io_base;
	struct regmap *slcr;

	struct completion dma_done;
};

static inline void zynq_fpga_write(struct zynq_fpga_priv *priv, u32 offset,
				   u32 val)
{
	writel(val, priv->io_base + offset);
}

static inline u32 zynq_fpga_read(const struct zynq_fpga_priv *priv,
				 u32 offset)
{
	return readl(priv->io_base + offset);
}

#define zynq_fpga_poll_timeout(priv, addr, val, cond, sleep_us, timeout_us) \
	readl_poll_timeout(priv->io_base + addr, val, cond, sleep_us, \
			   timeout_us)

static void zynq_fpga_mask_irqs(struct zynq_fpga_priv *priv)
{
	u32 intr_mask;

	intr_mask = zynq_fpga_read(priv, INT_MASK_OFFSET);
	zynq_fpga_write(priv, INT_MASK_OFFSET,
			intr_mask | IXR_DMA_DONE_MASK | IXR_ERROR_FLAGS_MASK);
}

static void zynq_fpga_unmask_irqs(struct zynq_fpga_priv *priv)
{
	u32 intr_mask;

	intr_mask = zynq_fpga_read(priv, INT_MASK_OFFSET);
	zynq_fpga_write(priv, INT_MASK_OFFSET,
			intr_mask
			& ~(IXR_D_P_DONE_MASK | IXR_ERROR_FLAGS_MASK));
}

static irqreturn_t zynq_fpga_isr(int irq, void *data)
{
	struct zynq_fpga_priv *priv = data;

	/* disable DMA and error IRQs */
	zynq_fpga_mask_irqs(priv);

	complete(&priv->dma_done);

	return IRQ_HANDLED;
}

static int zynq_fpga_ops_write_init(struct fpga_manager *mgr, u32 flags,
				    const char *buf, size_t count)
{
	struct zynq_fpga_priv *priv;
	u32 ctrl, status;
	int err;

	priv = mgr->priv;

	err = clk_enable(priv->clk);
	if (err)
		return err;

	/* don't globally reset PL if we're doing partial reconfig */
	if (!(flags & FPGA_MGR_PARTIAL_RECONFIG)) {
		/* assert AXI interface resets */
		regmap_write(priv->slcr, SLCR_FPGA_RST_CTRL_OFFSET,
			     FPGA_RST_ALL_MASK);

		/* disable all level shifters */
		regmap_write(priv->slcr, SLCR_LVL_SHFTR_EN_OFFSET,
			     LVL_SHFTR_DISABLE_ALL_MASK);
		/* enable level shifters from PS to PL */
		regmap_write(priv->slcr, SLCR_LVL_SHFTR_EN_OFFSET,
			     LVL_SHFTR_ENABLE_PS_TO_PL);

		/* create a rising edge on PCFG_INIT. PCFG_INIT follows
		 * PCFG_PROG_B, so we need to poll it after setting PCFG_PROG_B
		 * to make sure the rising edge actually happens.
		 * Note: PCFG_PROG_B is low active, sequence as described in
		 * UG585 v1.10 page 211
		 */
		ctrl = zynq_fpga_read(priv, CTRL_OFFSET);
		ctrl |= CTRL_PCFG_PROG_B_MASK;

		zynq_fpga_write(priv, CTRL_OFFSET, ctrl);

		err = zynq_fpga_poll_timeout(priv, STATUS_OFFSET, status,
					     status & STATUS_PCFG_INIT_MASK,
					     INIT_POLL_DELAY,
					     INIT_POLL_TIMEOUT);
		if (err) {
			dev_err(priv->dev, "Timeout waiting for PCFG_INIT");
			goto out_err;
		}

		ctrl = zynq_fpga_read(priv, CTRL_OFFSET);
		ctrl &= ~CTRL_PCFG_PROG_B_MASK;

		zynq_fpga_write(priv, CTRL_OFFSET, ctrl);

		err = zynq_fpga_poll_timeout(priv, STATUS_OFFSET, status,
					     !(status & STATUS_PCFG_INIT_MASK),
					     INIT_POLL_DELAY,
					     INIT_POLL_TIMEOUT);
		if (err) {
			dev_err(priv->dev, "Timeout waiting for !PCFG_INIT");
			goto out_err;
		}

		ctrl = zynq_fpga_read(priv, CTRL_OFFSET);
		ctrl |= CTRL_PCFG_PROG_B_MASK;

		zynq_fpga_write(priv, CTRL_OFFSET, ctrl);

		err = zynq_fpga_poll_timeout(priv, STATUS_OFFSET, status,
					     status & STATUS_PCFG_INIT_MASK,
					     INIT_POLL_DELAY,
					     INIT_POLL_TIMEOUT);
		if (err) {
			dev_err(priv->dev, "Timeout waiting for PCFG_INIT");
			goto out_err;
		}
	}

	/* set configuration register with following options:
	 * - enable PCAP interface
	 * - set throughput for maximum speed
	 * - set CPU in user mode
	 */
	ctrl = zynq_fpga_read(priv, CTRL_OFFSET);
	zynq_fpga_write(priv, CTRL_OFFSET,
			(CTRL_PCAP_PR_MASK | CTRL_PCAP_MODE_MASK | ctrl));

	/* check that we have room in the command queue */
	status = zynq_fpga_read(priv, STATUS_OFFSET);
	if (status & STATUS_DMA_Q_F) {
		dev_err(priv->dev, "DMA command queue full");
		err = -EBUSY;
		goto out_err;
	}

	/* ensure internal PCAP loopback is disabled */
	ctrl = zynq_fpga_read(priv, MCTRL_OFFSET);
	zynq_fpga_write(priv, MCTRL_OFFSET, (~MCTRL_PCAP_LPBK_MASK & ctrl));

	clk_disable(priv->clk);

	return 0;

out_err:
	clk_disable(priv->clk);

	return err;
}

static int zynq_fpga_ops_write(struct fpga_manager *mgr,
			       const char *buf, size_t count)
{
	struct zynq_fpga_priv *priv;
	int err;
	char *kbuf;
	size_t in_count;
	dma_addr_t dma_addr;
	u32 transfer_length;
	u32 intr_status;

	in_count = count;
	priv = mgr->priv;

	kbuf = dma_alloc_coherent(priv->dev, count, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	memcpy(kbuf, buf, count);

	/* enable clock */
	err = clk_enable(priv->clk);
	if (err)
		goto out_free;

	zynq_fpga_write(priv, INT_STS_OFFSET, IXR_ALL_MASK);

	reinit_completion(&priv->dma_done);

	/* enable DMA and error IRQs */
	zynq_fpga_unmask_irqs(priv);

	/* the +1 in the src addr is used to hold off on DMA_DONE IRQ
	 * until both AXI and PCAP are done ...
	 */
	zynq_fpga_write(priv, DMA_SRC_ADDR_OFFSET, (u32)(dma_addr) + 1);
	zynq_fpga_write(priv, DMA_DST_ADDR_OFFSET, (u32)DMA_INVALID_ADDRESS);

	/* convert #bytes to #words */
	transfer_length = (count + 3) / 4;

	zynq_fpga_write(priv, DMA_SRC_LEN_OFFSET, transfer_length);
	zynq_fpga_write(priv, DMA_DEST_LEN_OFFSET, 0);

	wait_for_completion(&priv->dma_done);

	intr_status = zynq_fpga_read(priv, INT_STS_OFFSET);
	zynq_fpga_write(priv, INT_STS_OFFSET, intr_status);

	if (!((intr_status & IXR_D_P_DONE_MASK) == IXR_D_P_DONE_MASK)) {
		dev_err(priv->dev, "Error configuring FPGA");
		err = -EFAULT;
	}

	clk_disable(priv->clk);

out_free:
	dma_free_coherent(priv->dev, in_count, kbuf, dma_addr);

	return err;
}

static int zynq_fpga_ops_write_complete(struct fpga_manager *mgr, u32 flags)
{
	struct zynq_fpga_priv *priv = mgr->priv;
	int err;
	u32 intr_status;

	err = clk_enable(priv->clk);
	if (err)
		return err;

	err = zynq_fpga_poll_timeout(priv, INT_STS_OFFSET, intr_status,
				     intr_status & IXR_PCFG_DONE_MASK,
				     INIT_POLL_DELAY,
				     INIT_POLL_TIMEOUT);

	clk_disable(priv->clk);

	if (err)
		return err;

	/* for the partial reconfig case we didn't touch the level shifters */
	if (!(flags & FPGA_MGR_PARTIAL_RECONFIG)) {
		/* enable level shifters from PL to PS */
		regmap_write(priv->slcr, SLCR_LVL_SHFTR_EN_OFFSET,
			     LVL_SHFTR_ENABLE_PL_TO_PS);

		/* deassert AXI interface resets */
		regmap_write(priv->slcr, SLCR_FPGA_RST_CTRL_OFFSET,
			     FPGA_RST_NONE_MASK);
	}

	return 0;
}

static enum fpga_mgr_states zynq_fpga_ops_state(struct fpga_manager *mgr)
{
	int err;
	u32 intr_status;
	struct zynq_fpga_priv *priv;

	priv = mgr->priv;

	err = clk_enable(priv->clk);
	if (err)
		return FPGA_MGR_STATE_UNKNOWN;

	intr_status = zynq_fpga_read(priv, INT_STS_OFFSET);
	clk_disable(priv->clk);

	if (intr_status & IXR_PCFG_DONE_MASK)
		return FPGA_MGR_STATE_OPERATING;

	return FPGA_MGR_STATE_UNKNOWN;
}

static const struct fpga_manager_ops zynq_fpga_ops = {
	.state = zynq_fpga_ops_state,
	.write_init = zynq_fpga_ops_write_init,
	.write = zynq_fpga_ops_write,
	.write_complete = zynq_fpga_ops_write_complete,
};

static int zynq_fpga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zynq_fpga_priv *priv;
	struct resource *res;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->io_base))
		return PTR_ERR(priv->io_base);

	priv->slcr = syscon_regmap_lookup_by_phandle(dev->of_node,
		"syscon");
	if (IS_ERR(priv->slcr)) {
		dev_err(dev, "unable to get zynq-slcr regmap");
		return PTR_ERR(priv->slcr);
	}

	init_completion(&priv->dma_done);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(dev, "No IRQ available");
		return priv->irq;
	}

	err = devm_request_irq(dev, priv->irq, zynq_fpga_isr, 0,
			       dev_name(dev), priv);
	if (err) {
		dev_err(dev, "unable to request IRQ");
		return err;
	}

	priv->clk = devm_clk_get(dev, "ref_clk");
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "input clock not found");
		return PTR_ERR(priv->clk);
	}

	err = clk_prepare_enable(priv->clk);
	if (err) {
		dev_err(dev, "unable to enable clock");
		return err;
	}

	/* unlock the device */
	zynq_fpga_write(priv, UNLOCK_OFFSET, UNLOCK_MASK);

	clk_disable(priv->clk);

	err = fpga_mgr_register(dev, "Xilinx Zynq FPGA Manager",
				&zynq_fpga_ops, priv);
	if (err) {
		dev_err(dev, "unable to register FPGA manager");
		clk_unprepare(priv->clk);
		return err;
	}

	return 0;
}

static int zynq_fpga_remove(struct platform_device *pdev)
{
	struct zynq_fpga_priv *priv;
	struct fpga_manager *mgr;

	mgr = platform_get_drvdata(pdev);
	priv = mgr->priv;

	fpga_mgr_unregister(&pdev->dev);

	clk_unprepare(priv->clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id zynq_fpga_of_match[] = {
	{ .compatible = "xlnx,zynq-devcfg-1.0", },
	{},
};

MODULE_DEVICE_TABLE(of, zynq_fpga_of_match);
#endif

static struct platform_driver zynq_fpga_driver = {
	.probe = zynq_fpga_probe,
	.remove = zynq_fpga_remove,
	.driver = {
		.name = "zynq_fpga_manager",
		.of_match_table = of_match_ptr(zynq_fpga_of_match),
	},
};

module_platform_driver(zynq_fpga_driver);

MODULE_AUTHOR("Moritz Fischer <moritz.fischer@ettus.com>");
MODULE_AUTHOR("Michal Simek <michal.simek@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Zynq FPGA Manager");
MODULE_LICENSE("GPL v2");
