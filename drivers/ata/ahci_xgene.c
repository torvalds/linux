/*
 * AppliedMicro X-Gene SoC SATA Host Controller Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Author: Loc Ho <lho@apm.com>
 *         Tuan Phan <tphan@apm.com>
 *         Suman Tripathi <stripathi@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * NOTE: PM support is not currently available.
 *
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ahci_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/phy/phy.h>
#include "ahci.h"

/* Max # of disk per a controller */
#define MAX_AHCI_CHN_PERCTR		2

/* MUX CSR */
#define SATA_ENET_CONFIG_REG		0x00000000
#define  CFG_SATA_ENET_SELECT_MASK	0x00000001

/* SATA core host controller CSR */
#define SLVRDERRATTRIBUTES		0x00000000
#define SLVWRERRATTRIBUTES		0x00000004
#define MSTRDERRATTRIBUTES		0x00000008
#define MSTWRERRATTRIBUTES		0x0000000c
#define BUSCTLREG			0x00000014
#define IOFMSTRWAUX			0x00000018
#define INTSTATUSMASK			0x0000002c
#define ERRINTSTATUS			0x00000030
#define ERRINTSTATUSMASK		0x00000034

/* SATA host AHCI CSR */
#define PORTCFG				0x000000a4
#define  PORTADDR_SET(dst, src) \
		(((dst) & ~0x0000003f) | (((u32)(src)) & 0x0000003f))
#define PORTPHY1CFG		0x000000a8
#define PORTPHY1CFG_FRCPHYRDY_SET(dst, src) \
		(((dst) & ~0x00100000) | (((u32)(src) << 0x14) & 0x00100000))
#define PORTPHY2CFG			0x000000ac
#define PORTPHY3CFG			0x000000b0
#define PORTPHY4CFG			0x000000b4
#define PORTPHY5CFG			0x000000b8
#define SCTL0				0x0000012C
#define PORTPHY5CFG_RTCHG_SET(dst, src) \
		(((dst) & ~0xfff00000) | (((u32)(src) << 0x14) & 0xfff00000))
#define PORTAXICFG_EN_CONTEXT_SET(dst, src) \
		(((dst) & ~0x01000000) | (((u32)(src) << 0x18) & 0x01000000))
#define PORTAXICFG			0x000000bc
#define PORTAXICFG_OUTTRANS_SET(dst, src) \
		(((dst) & ~0x00f00000) | (((u32)(src) << 0x14) & 0x00f00000))
#define PORTRANSCFG			0x000000c8
#define PORTRANSCFG_RXWM_SET(dst, src)		\
		(((dst) & ~0x0000007f) | (((u32)(src)) & 0x0000007f))

/* SATA host controller AXI CSR */
#define INT_SLV_TMOMASK			0x00000010

/* SATA diagnostic CSR */
#define CFG_MEM_RAM_SHUTDOWN		0x00000070
#define BLOCK_MEM_RDY			0x00000074

struct xgene_ahci_context {
	struct ahci_host_priv *hpriv;
	struct device *dev;
	u8 last_cmd[MAX_AHCI_CHN_PERCTR]; /* tracking the last command issued*/
	void __iomem *csr_core;		/* Core CSR address of IP */
	void __iomem *csr_diag;		/* Diag CSR address of IP */
	void __iomem *csr_axi;		/* AXI CSR address of IP */
	void __iomem *csr_mux;		/* MUX CSR address of IP */
};

static int xgene_ahci_init_memram(struct xgene_ahci_context *ctx)
{
	dev_dbg(ctx->dev, "Release memory from shutdown\n");
	writel(0x0, ctx->csr_diag + CFG_MEM_RAM_SHUTDOWN);
	readl(ctx->csr_diag + CFG_MEM_RAM_SHUTDOWN); /* Force a barrier */
	msleep(1);	/* reset may take up to 1ms */
	if (readl(ctx->csr_diag + BLOCK_MEM_RDY) != 0xFFFFFFFF) {
		dev_err(ctx->dev, "failed to release memory from shutdown\n");
		return -ENODEV;
	}
	return 0;
}

/**
 * xgene_ahci_restart_engine - Restart the dma engine.
 * @ap : ATA port of interest
 *
 * Restarts the dma engine inside the controller.
 */
static int xgene_ahci_restart_engine(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;

	ahci_stop_engine(ap);
	ahci_start_fis_rx(ap);
	hpriv->start_engine(ap);

	return 0;
}

/**
 * xgene_ahci_qc_issue - Issue commands to the device
 * @qc: Command to issue
 *
 * Due to Hardware errata for IDENTIFY DEVICE command, the controller cannot
 * clear the BSY bit after receiving the PIO setup FIS. This results in the dma
 * state machine goes into the CMFatalErrorUpdate state and locks up. By
 * restarting the dma engine, it removes the controller out of lock up state.
 */
static unsigned int xgene_ahci_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct xgene_ahci_context *ctx = hpriv->plat_data;
	int rc = 0;

	if (unlikely(ctx->last_cmd[ap->port_no] == ATA_CMD_ID_ATA))
		xgene_ahci_restart_engine(ap);

	rc = ahci_qc_issue(qc);

	/* Save the last command issued */
	ctx->last_cmd[ap->port_no] = qc->tf.command;

	return rc;
}

/**
 * xgene_ahci_read_id - Read ID data from the specified device
 * @dev: device
 * @tf: proposed taskfile
 * @id: data buffer
 *
 * This custom read ID function is required due to the fact that the HW
 * does not support DEVSLP.
 */
static unsigned int xgene_ahci_read_id(struct ata_device *dev,
				       struct ata_taskfile *tf, u16 *id)
{
	u32 err_mask;

	err_mask = ata_do_dev_read_id(dev, tf, id);
	if (err_mask)
		return err_mask;

	/*
	 * Mask reserved area. Word78 spec of Link Power Management
	 * bit15-8: reserved
	 * bit7: NCQ autosence
	 * bit6: Software settings preservation supported
	 * bit5: reserved
	 * bit4: In-order sata delivery supported
	 * bit3: DIPM requests supported
	 * bit2: DMA Setup FIS Auto-Activate optimization supported
	 * bit1: DMA Setup FIX non-Zero buffer offsets supported
	 * bit0: Reserved
	 *
	 * Clear reserved bit 8 (DEVSLP bit) as we don't support DEVSLP
	 */
	id[ATA_ID_FEATURE_SUPP] &= ~(1 << 8);

	return 0;
}

static void xgene_ahci_set_phy_cfg(struct xgene_ahci_context *ctx, int channel)
{
	void __iomem *mmio = ctx->hpriv->mmio;
	u32 val;

	dev_dbg(ctx->dev, "port configure mmio 0x%p channel %d\n",
		mmio, channel);
	val = readl(mmio + PORTCFG);
	val = PORTADDR_SET(val, channel == 0 ? 2 : 3);
	writel(val, mmio + PORTCFG);
	readl(mmio + PORTCFG);  /* Force a barrier */
	/* Disable fix rate */
	writel(0x0001fffe, mmio + PORTPHY1CFG);
	readl(mmio + PORTPHY1CFG); /* Force a barrier */
	writel(0x28183219, mmio + PORTPHY2CFG);
	readl(mmio + PORTPHY2CFG); /* Force a barrier */
	writel(0x13081008, mmio + PORTPHY3CFG);
	readl(mmio + PORTPHY3CFG); /* Force a barrier */
	writel(0x00480815, mmio + PORTPHY4CFG);
	readl(mmio + PORTPHY4CFG); /* Force a barrier */
	/* Set window negotiation */
	val = readl(mmio + PORTPHY5CFG);
	val = PORTPHY5CFG_RTCHG_SET(val, 0x300);
	writel(val, mmio + PORTPHY5CFG);
	readl(mmio + PORTPHY5CFG); /* Force a barrier */
	val = readl(mmio + PORTAXICFG);
	val = PORTAXICFG_EN_CONTEXT_SET(val, 0x1); /* Enable context mgmt */
	val = PORTAXICFG_OUTTRANS_SET(val, 0xe); /* Set outstanding */
	writel(val, mmio + PORTAXICFG);
	readl(mmio + PORTAXICFG); /* Force a barrier */
	/* Set the watermark threshold of the receive FIFO */
	val = readl(mmio + PORTRANSCFG);
	val = PORTRANSCFG_RXWM_SET(val, 0x30);
	writel(val, mmio + PORTRANSCFG);
}

/**
 * xgene_ahci_do_hardreset - Issue the actual COMRESET
 * @link: link to reset
 * @deadline: deadline jiffies for the operation
 * @online: Return value to indicate if device online
 *
 * Due to the limitation of the hardware PHY, a difference set of setting is
 * required for each supported disk speed - Gen3 (6.0Gbps), Gen2 (3.0Gbps),
 * and Gen1 (1.5Gbps). Otherwise during long IO stress test, the PHY will
 * report disparity error and etc. In addition, during COMRESET, there can
 * be error reported in the register PORT_SCR_ERR. For SERR_DISPARITY and
 * SERR_10B_8B_ERR, the PHY receiver line must be reseted. The following
 * algorithm is followed to proper configure the hardware PHY during COMRESET:
 *
 * Alg Part 1:
 * 1. Start the PHY at Gen3 speed (default setting)
 * 2. Issue the COMRESET
 * 3. If no link, go to Alg Part 3
 * 4. If link up, determine if the negotiated speed matches the PHY
 *    configured speed
 * 5. If they matched, go to Alg Part 2
 * 6. If they do not matched and first time, configure the PHY for the linked
 *    up disk speed and repeat step 2
 * 7. Go to Alg Part 2
 *
 * Alg Part 2:
 * 1. On link up, if there are any SERR_DISPARITY and SERR_10B_8B_ERR error
 *    reported in the register PORT_SCR_ERR, then reset the PHY receiver line
 * 2. Go to Alg Part 3
 *
 * Alg Part 3:
 * 1. Clear any pending from register PORT_SCR_ERR.
 *
 * NOTE: For the initial version, we will NOT support Gen1/Gen2. In addition
 *       and until the underlying PHY supports an method to reset the receiver
 *       line, on detection of SERR_DISPARITY or SERR_10B_8B_ERR errors,
 *       an warning message will be printed.
 */
static int xgene_ahci_do_hardreset(struct ata_link *link,
				   unsigned long deadline, bool *online)
{
	const unsigned long *timing = sata_ehc_deb_timing(&link->eh_context);
	struct ata_port *ap = link->ap;
	struct ahci_host_priv *hpriv = ap->host->private_data;
	struct xgene_ahci_context *ctx = hpriv->plat_data;
	struct ahci_port_priv *pp = ap->private_data;
	u8 *d2h_fis = pp->rx_fis + RX_FIS_D2H_REG;
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ata_taskfile tf;
	int rc;
	u32 val;

	/* clear D2H reception area to properly wait for D2H FIS */
	ata_tf_init(link->device, &tf);
	tf.command = ATA_BUSY;
	ata_tf_to_fis(&tf, 0, 0, d2h_fis);
	rc = sata_link_hardreset(link, timing, deadline, online,
				 ahci_check_ready);

	val = readl(port_mmio + PORT_SCR_ERR);
	if (val & (SERR_DISPARITY | SERR_10B_8B_ERR))
		dev_warn(ctx->dev, "link has error\n");

	/* clear all errors if any pending */
	val = readl(port_mmio + PORT_SCR_ERR);
	writel(val, port_mmio + PORT_SCR_ERR);

	return rc;
}

static int xgene_ahci_hardreset(struct ata_link *link, unsigned int *class,
				unsigned long deadline)
{
	struct ata_port *ap = link->ap;
        struct ahci_host_priv *hpriv = ap->host->private_data;
	void __iomem *port_mmio = ahci_port_base(ap);
	bool online;
	int rc;
	u32 portcmd_saved;
	u32 portclb_saved;
	u32 portclbhi_saved;
	u32 portrxfis_saved;
	u32 portrxfishi_saved;

	/* As hardreset resets these CSR, save it to restore later */
	portcmd_saved = readl(port_mmio + PORT_CMD);
	portclb_saved = readl(port_mmio + PORT_LST_ADDR);
	portclbhi_saved = readl(port_mmio + PORT_LST_ADDR_HI);
	portrxfis_saved = readl(port_mmio + PORT_FIS_ADDR);
	portrxfishi_saved = readl(port_mmio + PORT_FIS_ADDR_HI);

	ahci_stop_engine(ap);

	rc = xgene_ahci_do_hardreset(link, deadline, &online);

	/* As controller hardreset clears them, restore them */
	writel(portcmd_saved, port_mmio + PORT_CMD);
	writel(portclb_saved, port_mmio + PORT_LST_ADDR);
	writel(portclbhi_saved, port_mmio + PORT_LST_ADDR_HI);
	writel(portrxfis_saved, port_mmio + PORT_FIS_ADDR);
	writel(portrxfishi_saved, port_mmio + PORT_FIS_ADDR_HI);

	hpriv->start_engine(ap);

	if (online)
		*class = ahci_dev_classify(ap);

	return rc;
}

static void xgene_ahci_host_stop(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;

	ahci_platform_disable_resources(hpriv);
}

static struct ata_port_operations xgene_ahci_ops = {
	.inherits = &ahci_ops,
	.host_stop = xgene_ahci_host_stop,
	.hardreset = xgene_ahci_hardreset,
	.read_id = xgene_ahci_read_id,
	.qc_issue = xgene_ahci_qc_issue,
};

static const struct ata_port_info xgene_ahci_port_info = {
	.flags = AHCI_FLAG_COMMON | ATA_FLAG_NCQ,
	.pio_mask = ATA_PIO4,
	.udma_mask = ATA_UDMA6,
	.port_ops = &xgene_ahci_ops,
};

static int xgene_ahci_hw_init(struct ahci_host_priv *hpriv)
{
	struct xgene_ahci_context *ctx = hpriv->plat_data;
	int i;
	int rc;
	u32 val;

	/* Remove IP RAM out of shutdown */
	rc = xgene_ahci_init_memram(ctx);
	if (rc)
		return rc;

	for (i = 0; i < MAX_AHCI_CHN_PERCTR; i++)
		xgene_ahci_set_phy_cfg(ctx, i);

	/* AXI disable Mask */
	writel(0xffffffff, hpriv->mmio + HOST_IRQ_STAT);
	readl(hpriv->mmio + HOST_IRQ_STAT); /* Force a barrier */
	writel(0, ctx->csr_core + INTSTATUSMASK);
	val = readl(ctx->csr_core + INTSTATUSMASK); /* Force a barrier */
	dev_dbg(ctx->dev, "top level interrupt mask 0x%X value 0x%08X\n",
		INTSTATUSMASK, val);

	writel(0x0, ctx->csr_core + ERRINTSTATUSMASK);
	readl(ctx->csr_core + ERRINTSTATUSMASK); /* Force a barrier */
	writel(0x0, ctx->csr_axi + INT_SLV_TMOMASK);
	readl(ctx->csr_axi + INT_SLV_TMOMASK);

	/* Enable AXI Interrupt */
	writel(0xffffffff, ctx->csr_core + SLVRDERRATTRIBUTES);
	writel(0xffffffff, ctx->csr_core + SLVWRERRATTRIBUTES);
	writel(0xffffffff, ctx->csr_core + MSTRDERRATTRIBUTES);
	writel(0xffffffff, ctx->csr_core + MSTWRERRATTRIBUTES);

	/* Enable coherency */
	val = readl(ctx->csr_core + BUSCTLREG);
	val &= ~0x00000002;     /* Enable write coherency */
	val &= ~0x00000001;     /* Enable read coherency */
	writel(val, ctx->csr_core + BUSCTLREG);

	val = readl(ctx->csr_core + IOFMSTRWAUX);
	val |= (1 << 3);        /* Enable read coherency */
	val |= (1 << 9);        /* Enable write coherency */
	writel(val, ctx->csr_core + IOFMSTRWAUX);
	val = readl(ctx->csr_core + IOFMSTRWAUX);
	dev_dbg(ctx->dev, "coherency 0x%X value 0x%08X\n",
		IOFMSTRWAUX, val);

	return rc;
}

static int xgene_ahci_mux_select(struct xgene_ahci_context *ctx)
{
	u32 val;

	/* Check for optional MUX resource */
	if (!ctx->csr_mux)
		return 0;

	val = readl(ctx->csr_mux + SATA_ENET_CONFIG_REG);
	val &= ~CFG_SATA_ENET_SELECT_MASK;
	writel(val, ctx->csr_mux + SATA_ENET_CONFIG_REG);
	val = readl(ctx->csr_mux + SATA_ENET_CONFIG_REG);
	return val & CFG_SATA_ENET_SELECT_MASK ? -1 : 0;
}

static int xgene_ahci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct xgene_ahci_context *ctx;
	struct resource *res;
	int rc;

	hpriv = ahci_platform_get_resources(pdev);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	hpriv->plat_data = ctx;
	ctx->hpriv = hpriv;
	ctx->dev = dev;

	/* Retrieve the IP core resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	ctx->csr_core = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->csr_core))
		return PTR_ERR(ctx->csr_core);

	/* Retrieve the IP diagnostic resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	ctx->csr_diag = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->csr_diag))
		return PTR_ERR(ctx->csr_diag);

	/* Retrieve the IP AXI resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	ctx->csr_axi = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->csr_axi))
		return PTR_ERR(ctx->csr_axi);

	/* Retrieve the optional IP mux resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 4);
	if (res) {
		void __iomem *csr = devm_ioremap_resource(dev, res);
		if (IS_ERR(csr))
			return PTR_ERR(csr);

		ctx->csr_mux = csr;
	}

	dev_dbg(dev, "VAddr 0x%p Mmio VAddr 0x%p\n", ctx->csr_core,
		hpriv->mmio);

	/* Select ATA */
	if ((rc = xgene_ahci_mux_select(ctx))) {
		dev_err(dev, "SATA mux selection failed error %d\n", rc);
		return -ENODEV;
	}

	/* Due to errata, HW requires full toggle transition */
	rc = ahci_platform_enable_clks(hpriv);
	if (rc)
		goto disable_resources;
	ahci_platform_disable_clks(hpriv);

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		goto disable_resources;

	/* Configure the host controller */
	xgene_ahci_hw_init(hpriv);

	hpriv->flags = AHCI_HFLAG_NO_PMP | AHCI_HFLAG_YES_NCQ;

	rc = ahci_platform_init_host(pdev, hpriv, &xgene_ahci_port_info);
	if (rc)
		goto disable_resources;

	dev_dbg(dev, "X-Gene SATA host controller initialized\n");
	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);
	return rc;
}

static const struct of_device_id xgene_ahci_of_match[] = {
	{.compatible = "apm,xgene-ahci"},
	{},
};
MODULE_DEVICE_TABLE(of, xgene_ahci_of_match);

static struct platform_driver xgene_ahci_driver = {
	.probe = xgene_ahci_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = "xgene-ahci",
		.owner = THIS_MODULE,
		.of_match_table = xgene_ahci_of_match,
	},
};

module_platform_driver(xgene_ahci_driver);

MODULE_DESCRIPTION("APM X-Gene AHCI SATA driver");
MODULE_AUTHOR("Loc Ho <lho@apm.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.4");
