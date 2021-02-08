// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Xilinx, Inc.
 * CEVA AHCI SATA platform driver
 *
 * based on the AHCI SATA platform driver by Jeff Garzik and Anton Vorontsov
 */

#include <linux/ahci_platform.h>
#include <linux/kernel.h>
#include <linux/libata.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include "ahci.h"

/* Vendor Specific Register Offsets */
#define AHCI_VEND_PCFG  0xA4
#define AHCI_VEND_PPCFG 0xA8
#define AHCI_VEND_PP2C  0xAC
#define AHCI_VEND_PP3C  0xB0
#define AHCI_VEND_PP4C  0xB4
#define AHCI_VEND_PP5C  0xB8
#define AHCI_VEND_AXICC 0xBC
#define AHCI_VEND_PAXIC 0xC0
#define AHCI_VEND_PTC   0xC8

/* Vendor Specific Register bit definitions */
#define PAXIC_ADBW_BW64 0x1
#define PAXIC_MAWID(i)	(((i) * 2) << 4)
#define PAXIC_MARID(i)	(((i) * 2) << 12)
#define PAXIC_MARIDD(i)	((((i) * 2) + 1) << 16)
#define PAXIC_MAWIDD(i)	((((i) * 2) + 1) << 8)
#define PAXIC_OTL	(0x4 << 20)

/* Register bit definitions for cache control */
#define AXICC_ARCA_VAL  (0xF << 0)
#define AXICC_ARCF_VAL  (0xF << 4)
#define AXICC_ARCH_VAL  (0xF << 8)
#define AXICC_ARCP_VAL  (0xF << 12)
#define AXICC_AWCFD_VAL (0xF << 16)
#define AXICC_AWCD_VAL  (0xF << 20)
#define AXICC_AWCF_VAL  (0xF << 24)

#define PCFG_TPSS_VAL	(0x32 << 16)
#define PCFG_TPRS_VAL	(0x2 << 12)
#define PCFG_PAD_VAL	0x2

#define PPCFG_TTA	0x1FFFE
#define PPCFG_PSSO_EN	(1 << 28)
#define PPCFG_PSS_EN	(1 << 29)
#define PPCFG_ESDF_EN	(1 << 31)

#define PP5C_RIT	0x60216
#define PP5C_RCT	(0x7f0 << 20)

#define PTC_RX_WM_VAL	0x40
#define PTC_RSVD	(1 << 27)

#define PORT0_BASE	0x100
#define PORT1_BASE	0x180

/* Port Control Register Bit Definitions */
#define PORT_SCTL_SPD_GEN3	(0x3 << 4)
#define PORT_SCTL_SPD_GEN2	(0x2 << 4)
#define PORT_SCTL_SPD_GEN1	(0x1 << 4)
#define PORT_SCTL_IPM		(0x3 << 8)

#define PORT_BASE	0x100
#define PORT_OFFSET	0x80
#define NR_PORTS	2
#define DRV_NAME	"ahci-ceva"
#define CEVA_FLAG_BROKEN_GEN2	1

static unsigned int rx_watermark = PTC_RX_WM_VAL;
module_param(rx_watermark, uint, 0644);
MODULE_PARM_DESC(rx_watermark, "RxWaterMark value (0 - 0x80)");

struct ceva_ahci_priv {
	struct platform_device *ahci_pdev;
	/* Port Phy2Cfg Register */
	u32 pp2c[NR_PORTS];
	u32 pp3c[NR_PORTS];
	u32 pp4c[NR_PORTS];
	u32 pp5c[NR_PORTS];
	/* Axi Cache Control Register */
	u32 axicc;
	bool is_cci_enabled;
	int flags;
	struct reset_control *rst;
};

static unsigned int ceva_ahci_read_id(struct ata_device *dev,
					struct ata_taskfile *tf, u16 *id)
{
	u32 err_mask;

	err_mask = ata_do_dev_read_id(dev, tf, id);
	if (err_mask)
		return err_mask;
	/*
	 * Since CEVA controller does not support device sleep feature, we
	 * need to clear DEVSLP (bit 8) in word78 of the IDENTIFY DEVICE data.
	 */
	id[ATA_ID_FEATURE_SUPP] &= cpu_to_le16(~(1 << 8));

	return 0;
}

static struct ata_port_operations ahci_ceva_ops = {
	.inherits = &ahci_platform_ops,
	.read_id = ceva_ahci_read_id,
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

	/* Set AHCI Enable */
	tmp = readl(mmio + HOST_CTL);
	tmp |= HOST_AHCI_EN;
	writel(tmp, mmio + HOST_CTL);

	for (i = 0; i < NR_PORTS; i++) {
		/* TPSS TPRS scalars, CISE and Port Addr */
		tmp = PCFG_TPSS_VAL | PCFG_TPRS_VAL | (PCFG_PAD_VAL + i);
		writel(tmp, mmio + AHCI_VEND_PCFG);

		/*
		 * AXI Data bus width to 64
		 * Set Mem Addr Read, Write ID for data transfers
		 * Set Mem Addr Read ID, Write ID for non-data transfers
		 * Transfer limit to 72 DWord
		 */
		tmp = PAXIC_ADBW_BW64 | PAXIC_MAWIDD(i) | PAXIC_MARIDD(i) |
			PAXIC_MAWID(i) | PAXIC_MARID(i) | PAXIC_OTL;
		writel(tmp, mmio + AHCI_VEND_PAXIC);

		/* Set AXI cache control register if CCi is enabled */
		if (cevapriv->is_cci_enabled) {
			tmp = readl(mmio + AHCI_VEND_AXICC);
			tmp |= AXICC_ARCA_VAL | AXICC_ARCF_VAL |
				AXICC_ARCH_VAL | AXICC_ARCP_VAL |
				AXICC_AWCFD_VAL | AXICC_AWCD_VAL |
				AXICC_AWCF_VAL;
			writel(tmp, mmio + AHCI_VEND_AXICC);
		}

		/* Port Phy Cfg register enables */
		tmp = PPCFG_TTA | PPCFG_PSS_EN | PPCFG_ESDF_EN;
		writel(tmp, mmio + AHCI_VEND_PPCFG);

		/* Phy Control OOB timing parameters COMINIT */
		writel(cevapriv->pp2c[i], mmio + AHCI_VEND_PP2C);

		/* Phy Control OOB timing parameters COMWAKE */
		writel(cevapriv->pp3c[i], mmio + AHCI_VEND_PP3C);

		/* Phy Control Burst timing setting */
		writel(cevapriv->pp4c[i], mmio + AHCI_VEND_PP4C);

		/* Rate Change Timer and Retry Interval Timer setting */
		writel(cevapriv->pp5c[i], mmio + AHCI_VEND_PP5C);

		/* Rx Watermark setting  */
		tmp = rx_watermark | PTC_RSVD;
		writel(tmp, mmio + AHCI_VEND_PTC);

		/* Default to Gen 3 Speed and Gen 1 if Gen2 is broken */
		tmp = PORT_SCTL_SPD_GEN3 | PORT_SCTL_IPM;
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
	enum dev_dma_attr attr;
	int rc;

	cevapriv = devm_kzalloc(dev, sizeof(*cevapriv), GFP_KERNEL);
	if (!cevapriv)
		return -ENOMEM;

	cevapriv->ahci_pdev = pdev;

	cevapriv->rst = devm_reset_control_get_optional_exclusive(&pdev->dev,
								  NULL);
	if (IS_ERR(cevapriv->rst)) {
		if (PTR_ERR(cevapriv->rst) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get reset: %ld\n",
				PTR_ERR(cevapriv->rst));
	}

	hpriv = ahci_platform_get_resources(pdev, 0);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	if (!cevapriv->rst) {
		rc = ahci_platform_enable_resources(hpriv);
		if (rc)
			return rc;
	} else {
		int i;

		rc = ahci_platform_enable_clks(hpriv);
		if (rc)
			return rc;
		/* Assert the controller reset */
		reset_control_assert(cevapriv->rst);

		for (i = 0; i < hpriv->nports; i++) {
			rc = phy_init(hpriv->phys[i]);
			if (rc)
				return rc;
		}

		/* De-assert the controller reset */
		reset_control_deassert(cevapriv->rst);

		for (i = 0; i < hpriv->nports; i++) {
			rc = phy_power_on(hpriv->phys[i]);
			if (rc) {
				phy_exit(hpriv->phys[i]);
				return rc;
			}
		}
	}

	if (of_property_read_bool(np, "ceva,broken-gen2"))
		cevapriv->flags = CEVA_FLAG_BROKEN_GEN2;

	/* Read OOB timing value for COMINIT from device-tree */
	if (of_property_read_u8_array(np, "ceva,p0-cominit-params",
					(u8 *)&cevapriv->pp2c[0], 4) < 0) {
		dev_warn(dev, "ceva,p0-cominit-params property not defined\n");
		return -EINVAL;
	}

	if (of_property_read_u8_array(np, "ceva,p1-cominit-params",
					(u8 *)&cevapriv->pp2c[1], 4) < 0) {
		dev_warn(dev, "ceva,p1-cominit-params property not defined\n");
		return -EINVAL;
	}

	/* Read OOB timing value for COMWAKE from device-tree*/
	if (of_property_read_u8_array(np, "ceva,p0-comwake-params",
					(u8 *)&cevapriv->pp3c[0], 4) < 0) {
		dev_warn(dev, "ceva,p0-comwake-params property not defined\n");
		return -EINVAL;
	}

	if (of_property_read_u8_array(np, "ceva,p1-comwake-params",
					(u8 *)&cevapriv->pp3c[1], 4) < 0) {
		dev_warn(dev, "ceva,p1-comwake-params property not defined\n");
		return -EINVAL;
	}

	/* Read phy BURST timing value from device-tree */
	if (of_property_read_u8_array(np, "ceva,p0-burst-params",
					(u8 *)&cevapriv->pp4c[0], 4) < 0) {
		dev_warn(dev, "ceva,p0-burst-params property not defined\n");
		return -EINVAL;
	}

	if (of_property_read_u8_array(np, "ceva,p1-burst-params",
					(u8 *)&cevapriv->pp4c[1], 4) < 0) {
		dev_warn(dev, "ceva,p1-burst-params property not defined\n");
		return -EINVAL;
	}

	/* Read phy RETRY interval timing value from device-tree */
	if (of_property_read_u16_array(np, "ceva,p0-retry-params",
					(u16 *)&cevapriv->pp5c[0], 2) < 0) {
		dev_warn(dev, "ceva,p0-retry-params property not defined\n");
		return -EINVAL;
	}

	if (of_property_read_u16_array(np, "ceva,p1-retry-params",
					(u16 *)&cevapriv->pp5c[1], 2) < 0) {
		dev_warn(dev, "ceva,p1-retry-params property not defined\n");
		return -EINVAL;
	}

	/*
	 * Check if CCI is enabled for SATA. The DEV_DMA_COHERENT is returned
	 * if CCI is enabled, so check for DEV_DMA_COHERENT.
	 */
	attr = device_get_dma_attr(dev);
	cevapriv->is_cci_enabled = (attr == DEV_DMA_COHERENT);

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
	return ahci_platform_suspend(dev);
}

static int __maybe_unused ceva_ahci_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	/* Configure CEVA specific config before resuming HBA */
	ahci_ceva_setup(hpriv);

	rc = ahci_platform_resume_host(dev);
	if (rc)
		goto disable_resources;

	/* We resumed so update PM runtime state */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);

	return rc;
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
