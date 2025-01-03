/*
 * Freescale iMX PATA driver
 *
 * Copyright (C) 2011 Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 * Based on pata_platform - Copyright (C) 2006 - 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * TODO:
 * - dmaengine support
 */

#include <linux/ata.h>
#include <linux/clk.h>
#include <linux/libata.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#define DRV_NAME "pata_imx"

#define PATA_IMX_ATA_TIME_OFF		0x00
#define PATA_IMX_ATA_TIME_ON		0x01
#define PATA_IMX_ATA_TIME_1		0x02
#define PATA_IMX_ATA_TIME_2W		0x03
#define PATA_IMX_ATA_TIME_2R		0x04
#define PATA_IMX_ATA_TIME_AX		0x05
#define PATA_IMX_ATA_TIME_PIO_RDX	0x06
#define PATA_IMX_ATA_TIME_4		0x07
#define PATA_IMX_ATA_TIME_9		0x08

#define PATA_IMX_ATA_CONTROL		0x24
#define PATA_IMX_ATA_CTRL_FIFO_RST_B	(1<<7)
#define PATA_IMX_ATA_CTRL_ATA_RST_B	(1<<6)
#define PATA_IMX_ATA_CTRL_IORDY_EN	(1<<0)
#define PATA_IMX_ATA_INT_EN		0x2C
#define PATA_IMX_ATA_INTR_ATA_INTRQ2	(1<<3)
#define PATA_IMX_DRIVE_DATA		0xA0
#define PATA_IMX_DRIVE_CONTROL		0xD8

static u32 pio_t4[] = { 30,  20,  15,  10,  10 };
static u32 pio_t9[] = { 20,  15,  10,  10,  10 };
static u32 pio_tA[] = { 35,  35,  35,  35,  35 };

struct pata_imx_priv {
	struct clk *clk;
	/* timings/interrupt/control regs */
	void __iomem *host_regs;
	u32 ata_ctl;
};

static void pata_imx_set_timing(struct ata_device *adev,
				struct pata_imx_priv *priv)
{
	struct ata_timing timing;
	unsigned long clkrate;
	u32 T, mode;

	clkrate = clk_get_rate(priv->clk);

	if (adev->pio_mode < XFER_PIO_0 || adev->pio_mode > XFER_PIO_4 ||
	    !clkrate)
		return;

	T = 1000000000 / clkrate;
	ata_timing_compute(adev, adev->pio_mode, &timing, T * 1000, 0);

	mode = adev->pio_mode - XFER_PIO_0;

	writeb(3, priv->host_regs + PATA_IMX_ATA_TIME_OFF);
	writeb(3, priv->host_regs + PATA_IMX_ATA_TIME_ON);
	writeb(timing.setup, priv->host_regs + PATA_IMX_ATA_TIME_1);
	writeb(timing.act8b, priv->host_regs + PATA_IMX_ATA_TIME_2W);
	writeb(timing.act8b, priv->host_regs + PATA_IMX_ATA_TIME_2R);
	writeb(1, priv->host_regs + PATA_IMX_ATA_TIME_PIO_RDX);

	writeb(pio_t4[mode] / T + 1, priv->host_regs + PATA_IMX_ATA_TIME_4);
	writeb(pio_t9[mode] / T + 1, priv->host_regs + PATA_IMX_ATA_TIME_9);
	writeb(pio_tA[mode] / T + 1, priv->host_regs + PATA_IMX_ATA_TIME_AX);
}

static void pata_imx_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct pata_imx_priv *priv = ap->host->private_data;
	u32 val;

	pata_imx_set_timing(adev, priv);

	val = __raw_readl(priv->host_regs + PATA_IMX_ATA_CONTROL);
	if (ata_pio_need_iordy(adev))
		val |= PATA_IMX_ATA_CTRL_IORDY_EN;
	else
		val &= ~PATA_IMX_ATA_CTRL_IORDY_EN;
	__raw_writel(val, priv->host_regs + PATA_IMX_ATA_CONTROL);
}

static const struct scsi_host_template pata_imx_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations pata_imx_port_ops = {
	.inherits		= &ata_sff_port_ops,
	.sff_data_xfer		= ata_sff_data_xfer32,
	.cable_detect		= ata_cable_unknown,
	.set_piomode		= pata_imx_set_piomode,
};

static void pata_imx_setup_port(struct ata_ioports *ioaddr)
{
	/* Fixup the port shift for platforms that need it */
	ioaddr->data_addr	= ioaddr->cmd_addr + (ATA_REG_DATA    << 2);
	ioaddr->error_addr	= ioaddr->cmd_addr + (ATA_REG_ERR     << 2);
	ioaddr->feature_addr	= ioaddr->cmd_addr + (ATA_REG_FEATURE << 2);
	ioaddr->nsect_addr	= ioaddr->cmd_addr + (ATA_REG_NSECT   << 2);
	ioaddr->lbal_addr	= ioaddr->cmd_addr + (ATA_REG_LBAL    << 2);
	ioaddr->lbam_addr	= ioaddr->cmd_addr + (ATA_REG_LBAM    << 2);
	ioaddr->lbah_addr	= ioaddr->cmd_addr + (ATA_REG_LBAH    << 2);
	ioaddr->device_addr	= ioaddr->cmd_addr + (ATA_REG_DEVICE  << 2);
	ioaddr->status_addr	= ioaddr->cmd_addr + (ATA_REG_STATUS  << 2);
	ioaddr->command_addr	= ioaddr->cmd_addr + (ATA_REG_CMD     << 2);
}

static int pata_imx_probe(struct platform_device *pdev)
{
	struct ata_host *host;
	struct ata_port *ap;
	struct pata_imx_priv *priv;
	int irq = 0;
	struct resource *io_res;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	priv = devm_kzalloc(&pdev->dev,
				sizeof(struct pata_imx_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "Failed to get and enable clock\n");
		return PTR_ERR(priv->clk);
	}

	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		return -ENOMEM;

	host->private_data = priv;
	ap = host->ports[0];

	ap->ops = &pata_imx_port_ops;
	ap->pio_mask = ATA_PIO4;
	ap->flags |= ATA_FLAG_SLAVE_POSS;

	priv->host_regs = devm_platform_get_and_ioremap_resource(pdev, 0, &io_res);
	if (IS_ERR(priv->host_regs))
		return PTR_ERR(priv->host_regs);

	ap->ioaddr.cmd_addr = priv->host_regs + PATA_IMX_DRIVE_DATA;
	ap->ioaddr.ctl_addr = priv->host_regs + PATA_IMX_DRIVE_CONTROL;

	ap->ioaddr.altstatus_addr = ap->ioaddr.ctl_addr;

	pata_imx_setup_port(&ap->ioaddr);

	ata_port_desc(ap, "cmd 0x%llx ctl 0x%llx",
		(unsigned long long)io_res->start + PATA_IMX_DRIVE_DATA,
		(unsigned long long)io_res->start + PATA_IMX_DRIVE_CONTROL);

	/* deassert resets */
	__raw_writel(PATA_IMX_ATA_CTRL_FIFO_RST_B |
			PATA_IMX_ATA_CTRL_ATA_RST_B,
			priv->host_regs + PATA_IMX_ATA_CONTROL);
	/* enable interrupts */
	__raw_writel(PATA_IMX_ATA_INTR_ATA_INTRQ2,
			priv->host_regs + PATA_IMX_ATA_INT_EN);

	/* activate */
	ret = ata_host_activate(host, irq, ata_sff_interrupt, 0,
				&pata_imx_sht);

	if (ret)
		return ret;

	return 0;
}

static void pata_imx_remove(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);
	struct pata_imx_priv *priv = host->private_data;

	ata_host_detach(host);

	__raw_writel(0, priv->host_regs + PATA_IMX_ATA_INT_EN);
}

#ifdef CONFIG_PM_SLEEP
static int pata_imx_suspend(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct pata_imx_priv *priv = host->private_data;

	ata_host_suspend(host, PMSG_SUSPEND);

	__raw_writel(0, priv->host_regs + PATA_IMX_ATA_INT_EN);
	priv->ata_ctl = __raw_readl(priv->host_regs + PATA_IMX_ATA_CONTROL);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static int pata_imx_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct pata_imx_priv *priv = host->private_data;

	int ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	__raw_writel(priv->ata_ctl, priv->host_regs + PATA_IMX_ATA_CONTROL);

	__raw_writel(PATA_IMX_ATA_INTR_ATA_INTRQ2,
			priv->host_regs + PATA_IMX_ATA_INT_EN);

	ata_host_resume(host);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pata_imx_pm_ops, pata_imx_suspend, pata_imx_resume);

static const struct of_device_id imx_pata_dt_ids[] = {
	{
		.compatible = "fsl,imx27-pata",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, imx_pata_dt_ids);

static struct platform_driver pata_imx_driver = {
	.probe		= pata_imx_probe,
	.remove		= pata_imx_remove,
	.driver = {
		.name		= DRV_NAME,
		.of_match_table	= imx_pata_dt_ids,
		.pm		= &pata_imx_pm_ops,
	},
};

module_platform_driver(pata_imx_driver);

MODULE_AUTHOR("Arnaud Patard <arnaud.patard@rtp-net.org>");
MODULE_DESCRIPTION("low-level driver for iMX PATA");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
