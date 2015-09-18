/*
 * Copyright (C) 2015 Xilinx, Inc.
 * CEVA AHCI SATA platform driver
 *
 * based on the AHCI SATA platform driver by Jeff Garzik and Anton Vorontsov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/ahci_platform.h>
#include <linux/kernel.h>
#include <linux/libata.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "ahci.h"

/* Vendor Specific Register Offsets */
#define AHCI_VEND_PCFG  0xA4
#define AHCI_VEND_PPCFG 0xA8
#define AHCI_VEND_PP2C  0xAC
#define AHCI_VEND_PP3C  0xB0
#define AHCI_VEND_PP4C  0xB4
#define AHCI_VEND_PP5C  0xB8
#define AHCI_VEND_PAXIC 0xC0
#define AHCI_VEND_PTC   0xC8

/* Vendor Specific Register bit definitions */
#define PAXIC_ADBW_BW64 0x1
#define PAXIC_MAWIDD	(1 << 8)
#define PAXIC_MARIDD	(1 << 16)
#define PAXIC_OTL	(0x4 << 20)

#define PCFG_TPSS_VAL	(0x32 << 16)
#define PCFG_TPRS_VAL	(0x2 << 12)
#define PCFG_PAD_VAL	0x2

#define PPCFG_TTA	0x1FFFE
#define PPCFG_PSSO_EN	(1 << 28)
#define PPCFG_PSS_EN	(1 << 29)
#define PPCFG_ESDF_EN	(1 << 31)

#define PP2C_CIBGMN	0x0F
#define PP2C_CIBGMX	(0x25 << 8)
#define PP2C_CIBGN	(0x18 << 16)
#define PP2C_CINMP	(0x29 << 24)

#define PP3C_CWBGMN	0x04
#define PP3C_CWBGMX	(0x0B << 8)
#define PP3C_CWBGN	(0x08 << 16)
#define PP3C_CWNMP	(0x0F << 24)

#define PP4C_BMX	0x0a
#define PP4C_BNM	(0x08 << 8)
#define PP4C_SFD	(0x4a << 16)
#define PP4C_PTST	(0x06 << 24)

#define PP5C_RIT	0x60216
#define PP5C_RCT	(0x7f0 << 20)

#define PTC_RX_WM_VAL	0x40
#define PTC_RSVD	(1 << 27)

#define PORT0_BASE	0x100
#define PORT1_BASE	0x180

/* Port Control Register Bit Definitions */
#define PORT_SCTL_SPD_GEN2	(0x2 << 4)
#define PORT_SCTL_SPD_GEN1	(0x1 << 4)
#define PORT_SCTL_IPM		(0x3 << 8)

#define PORT_BASE	0x100
#define PORT_OFFSET	0x80
#define NR_PORTS	2
#define DRV_NAME	"ahci-ceva"
#define CEVA_FLAG_BROKEN_GEN2	1

struct ceva_ahci_priv {
	struct platform_device *ahci_pdev;
	int flags;
};

static struct ata_port_operations ahci_ceva_ops = {
	.inherits = &ahci_platform_ops,
};

static const struct ata_port_info ahci_ceva_port_info = {
	.flags          = AHCI_FLAG_COMMON,
	.pio_mask       = ATA_PIO4,
	.udma_mask      = ATA_UDMA6,
	.port_ops	= &ahci_ceva_ops,
};

static void ahci_ceva_setup(struct ahci_host_priv *hpriv)
{
	void __iomem *mmio = hpriv->mmio;
	struct ceva_ahci_priv *cevapriv = hpriv->plat_data;
	u32 tmp;
	int i;

	/*
	 * AXI Data bus width to 64
	 * Set Mem Addr Read, Write ID for data transfers
	 * Transfer limit to 72 DWord
	 */
	tmp = PAXIC_ADBW_BW64 | PAXIC_MAWIDD | PAXIC_MARIDD | PAXIC_OTL;
	writel(tmp, mmio + AHCI_VEND_PAXIC);

	/* Set AHCI Enable */
	tmp = readl(mmio + HOST_CTL);
	tmp |= HOST_AHCI_EN;
	writel(tmp, mmio + HOST_CTL);

	for (i = 0; i < NR_PORTS; i++) {
		/* TPSS TPRS scalars, CISE and Port Addr */
		tmp = PCFG_TPSS_VAL | PCFG_TPRS_VAL | (PCFG_PAD_VAL + i);
		writel(tmp, mmio + AHCI_VEND_PCFG);

		/* Port Phy Cfg register enables */
		tmp = PPCFG_TTA | PPCFG_PSS_EN | PPCFG_ESDF_EN;
		writel(tmp, mmio + AHCI_VEND_PPCFG);

		/* Phy Control OOB timing parameters COMINIT */
		tmp = PP2C_CIBGMN | PP2C_CIBGMX | PP2C_CIBGN | PP2C_CINMP;
		writel(tmp, mmio + AHCI_VEND_PP2C);

		/* Phy Control OOB timing parameters COMWAKE */
		tmp = PP3C_CWBGMN | PP3C_CWBGMX | PP3C_CWBGN | PP3C_CWNMP;
		writel(tmp, mmio + AHCI_VEND_PP3C);

		/* Phy Control Burst timing setting */
		tmp = PP4C_BMX | PP4C_BNM | PP4C_SFD | PP4C_PTST;
		writel(tmp, mmio + AHCI_VEND_PP4C);

		/* Rate Change Timer and Retry Interval Timer setting */
		tmp = PP5C_RIT | PP5C_RCT;
		writel(tmp, mmio + AHCI_VEND_PP5C);

		/* Rx Watermark setting  */
		tmp = PTC_RX_WM_VAL | PTC_RSVD;
		writel(tmp, mmio + AHCI_VEND_PTC);

		/* Default to Gen 2 Speed and Gen 1 if Gen2 is broken */
		tmp = PORT_SCTL_SPD_GEN2 | PORT_SCTL_IPM;
		if (cevapriv->flags & CEVA_FLAG_BROKEN_GEN2)
			tmp = PORT_SCTL_SPD_GEN1 | PORT_SCTL_IPM;
		writel(tmp, mmio + PORT_SCR_CTL + PORT_BASE + PORT_OFFSET * i);
	}
}

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static int ceva_ahci_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	struct ceva_ahci_priv *cevapriv;
	int rc;

	cevapriv = devm_kzalloc(dev, sizeof(*cevapriv), GFP_KERNEL);
	if (!cevapriv)
		return -ENOMEM;

	cevapriv->ahci_pdev = pdev;

	hpriv = ahci_platform_get_resources(pdev);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	if (of_property_read_bool(np, "ceva,broken-gen2"))
		cevapriv->flags = CEVA_FLAG_BROKEN_GEN2;

	hpriv->plat_data = cevapriv;

	/* CEVA specific initialization */
	ahci_ceva_setup(hpriv);

	rc = ahci_platform_init_host(pdev, hpriv, &ahci_ceva_port_info,
					&ahci_platform_sht);
	if (rc)
		goto disable_resources;

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);
	return rc;
}

static int __maybe_unused ceva_ahci_suspend(struct device *dev)
{
	return ahci_platform_suspend_host(dev);
}

static int __maybe_unused ceva_ahci_resume(struct device *dev)
{
	return ahci_platform_resume_host(dev);
}

static SIMPLE_DEV_PM_OPS(ahci_ceva_pm_ops, ceva_ahci_suspend, ceva_ahci_resume);

static const struct of_device_id ceva_ahci_of_match[] = {
	{ .compatible = "ceva,ahci-1v84" },
	{},
};
MODULE_DEVICE_TABLE(of, ceva_ahci_of_match);

static struct platform_driver ceva_ahci_driver = {
	.probe = ceva_ahci_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ceva_ahci_of_match,
		.pm = &ahci_ceva_pm_ops,
	},
};
module_platform_driver(ceva_ahci_driver);

MODULE_DESCRIPTION("CEVA AHCI SATA platform driver");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_LICENSE("GPL v2");
