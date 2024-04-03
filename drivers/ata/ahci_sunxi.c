// SPDX-License-Identifier: GPL-2.0-only
/*
 * Allwinner sunxi AHCI SATA platform driver
 * Copyright 2013 Olliver Schinagl <oliver@schinagl.nl>
 * Copyright 2014 Hans de Goede <hdegoede@redhat.com>
 *
 * based on the AHCI SATA platform driver by Jeff Garzik and Anton Vorontsov
 * Based on code from Allwinner Technology Co., Ltd. <www.allwinnertech.com>,
 * Daniel Wang <danielwang@allwinnertech.com>
 */

#include <linux/ahci_platform.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include "ahci.h"

#define DRV_NAME "ahci-sunxi"

/* Insmod parameters */
static bool enable_pmp;
module_param(enable_pmp, bool, 0);
MODULE_PARM_DESC(enable_pmp,
	"Enable support for sata port multipliers, only use if you use a pmp!");

#define AHCI_BISTAFR	0x00a0
#define AHCI_BISTCR	0x00a4
#define AHCI_BISTFCTR	0x00a8
#define AHCI_BISTSR	0x00ac
#define AHCI_BISTDECR	0x00b0
#define AHCI_DIAGNR0	0x00b4
#define AHCI_DIAGNR1	0x00b8
#define AHCI_OOBR	0x00bc
#define AHCI_PHYCS0R	0x00c0
#define AHCI_PHYCS1R	0x00c4
#define AHCI_PHYCS2R	0x00c8
#define AHCI_TIMER1MS	0x00e0
#define AHCI_GPARAM1R	0x00e8
#define AHCI_GPARAM2R	0x00ec
#define AHCI_PPARAMR	0x00f0
#define AHCI_TESTR	0x00f4
#define AHCI_VERSIONR	0x00f8
#define AHCI_IDR	0x00fc
#define AHCI_RWCR	0x00fc
#define AHCI_P0DMACR	0x0170
#define AHCI_P0PHYCR	0x0178
#define AHCI_P0PHYSR	0x017c

static void sunxi_clrbits(void __iomem *reg, u32 clr_val)
{
	u32 reg_val;

	reg_val = readl(reg);
	reg_val &= ~(clr_val);
	writel(reg_val, reg);
}

static void sunxi_setbits(void __iomem *reg, u32 set_val)
{
	u32 reg_val;

	reg_val = readl(reg);
	reg_val |= set_val;
	writel(reg_val, reg);
}

static void sunxi_clrsetbits(void __iomem *reg, u32 clr_val, u32 set_val)
{
	u32 reg_val;

	reg_val = readl(reg);
	reg_val &= ~(clr_val);
	reg_val |= set_val;
	writel(reg_val, reg);
}

static u32 sunxi_getbits(void __iomem *reg, u8 mask, u8 shift)
{
	return (readl(reg) >> shift) & mask;
}

static int ahci_sunxi_phy_init(struct device *dev, void __iomem *reg_base)
{
	u32 reg_val;
	int timeout;

	/* This magic is from the original code */
	writel(0, reg_base + AHCI_RWCR);
	msleep(5);

	sunxi_setbits(reg_base + AHCI_PHYCS1R, BIT(19));
	sunxi_clrsetbits(reg_base + AHCI_PHYCS0R,
			 (0x7 << 24),
			 (0x5 << 24) | BIT(23) | BIT(18));
	sunxi_clrsetbits(reg_base + AHCI_PHYCS1R,
			 (0x3 << 16) | (0x1f << 8) | (0x3 << 6),
			 (0x2 << 16) | (0x6 << 8) | (0x2 << 6));
	sunxi_setbits(reg_base + AHCI_PHYCS1R, BIT(28) | BIT(15));
	sunxi_clrbits(reg_base + AHCI_PHYCS1R, BIT(19));
	sunxi_clrsetbits(reg_base + AHCI_PHYCS0R,
			 (0x7 << 20), (0x3 << 20));
	sunxi_clrsetbits(reg_base + AHCI_PHYCS2R,
			 (0x1f << 5), (0x19 << 5));
	msleep(5);

	sunxi_setbits(reg_base + AHCI_PHYCS0R, (0x1 << 19));

	timeout = 250; /* Power up takes aprox 50 us */
	do {
		reg_val = sunxi_getbits(reg_base + AHCI_PHYCS0R, 0x7, 28);
		if (reg_val == 0x02)
			break;

		if (--timeout == 0) {
			dev_err(dev, "PHY power up failed.\n");
			return -EIO;
		}
		udelay(1);
	} while (1);

	sunxi_setbits(reg_base + AHCI_PHYCS2R, (0x1 << 24));

	timeout = 100; /* Calibration takes aprox 10 us */
	do {
		reg_val = sunxi_getbits(reg_base + AHCI_PHYCS2R, 0x1, 24);
		if (reg_val == 0x00)
			break;

		if (--timeout == 0) {
			dev_err(dev, "PHY calibration failed.\n");
			return -EIO;
		}
		udelay(1);
	} while (1);

	msleep(15);

	writel(0x7, reg_base + AHCI_RWCR);

	return 0;
}

static void ahci_sunxi_start_engine(struct ata_port *ap)
{
	void __iomem *port_mmio = ahci_port_base(ap);
	struct ahci_host_priv *hpriv = ap->host->private_data;

	/* Setup DMA before DMA start
	 *
	 * NOTE: A similar SoC with SATA/AHCI by Texas Instruments documents
	 *   this Vendor Specific Port (P0DMACR, aka PxDMACR) in its
	 *   User's Guide document (TMS320C674x/OMAP-L1x Processor
	 *   Serial ATA (SATA) Controller, Literature Number: SPRUGJ8C,
	 *   March 2011, Chapter 4.33 Port DMA Control Register (P0DMACR),
	 *   p.68, https://www.ti.com/lit/ug/sprugj8c/sprugj8c.pdf)
	 *   as equivalent to the following struct:
	 *
	 *   struct AHCI_P0DMACR_t
	 *   {
	 *     unsigned TXTS     : 4;
	 *     unsigned RXTS     : 4;
	 *     unsigned TXABL    : 4;
	 *     unsigned RXABL    : 4;
	 *     unsigned Reserved : 16;
	 *   };
	 *
	 *   TXTS: Transmit Transaction Size (TX_TRANSACTION_SIZE).
	 *     This field defines the DMA transaction size in DWORDs for
	 *     transmit (system bus read, device write) operation. [...]
	 *
	 *   RXTS: Receive Transaction Size (RX_TRANSACTION_SIZE).
	 *     This field defines the Port DMA transaction size in DWORDs
	 *     for receive (system bus write, device read) operation. [...]
	 *
	 *   TXABL: Transmit Burst Limit.
	 *     This field allows software to limit the VBUSP master read
	 *     burst size. [...]
	 *
	 *   RXABL: Receive Burst Limit.
	 *     Allows software to limit the VBUSP master write burst
	 *     size. [...]
	 *
	 *   Reserved: Reserved.
	 *
	 *
	 * NOTE: According to the above document, the following alternative
	 *   to the code below could perhaps be a better option
	 *   (or preparation) for possible further improvements later:
	 *     sunxi_clrsetbits(hpriv->mmio + AHCI_P0DMACR, 0x0000ffff,
	 *		0x00000033);
	 */
	sunxi_clrsetbits(hpriv->mmio + AHCI_P0DMACR, 0x0000ffff, 0x00004433);

	/* Start DMA */
	sunxi_setbits(port_mmio + PORT_CMD, PORT_CMD_START);
}

static const struct ata_port_info ahci_sunxi_port_info = {
	.flags		= AHCI_FLAG_COMMON | ATA_FLAG_NCQ | ATA_FLAG_NO_DIPM,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_platform_ops,
};

static const struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static int ahci_sunxi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	int rc;

	hpriv = ahci_platform_get_resources(pdev, AHCI_PLATFORM_GET_RESETS);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	hpriv->start_engine = ahci_sunxi_start_engine;

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	rc = ahci_sunxi_phy_init(dev, hpriv->mmio);
	if (rc)
		goto disable_resources;

	hpriv->flags = AHCI_HFLAG_32BIT_ONLY | AHCI_HFLAG_NO_MSI |
		       AHCI_HFLAG_YES_NCQ;

	/*
	 * The sunxi sata controller seems to be unable to successfully do a
	 * soft reset if no pmp is attached, so disable pmp use unless
	 * requested, otherwise directly attached disks do not work.
	 */
	if (!enable_pmp)
		hpriv->flags |= AHCI_HFLAG_NO_PMP;

	rc = ahci_platform_init_host(pdev, hpriv, &ahci_sunxi_port_info,
				     &ahci_platform_sht);
	if (rc)
		goto disable_resources;

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);
	return rc;
}

#ifdef CONFIG_PM_SLEEP
static int ahci_sunxi_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int rc;

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	rc = ahci_sunxi_phy_init(dev, hpriv->mmio);
	if (rc)
		goto disable_resources;

	rc = ahci_platform_resume_host(dev);
	if (rc)
		goto disable_resources;

	return 0;

disable_resources:
	ahci_platform_disable_resources(hpriv);
	return rc;
}
#endif

static SIMPLE_DEV_PM_OPS(ahci_sunxi_pm_ops, ahci_platform_suspend,
			 ahci_sunxi_resume);

static const struct of_device_id ahci_sunxi_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-ahci", },
	{ .compatible = "allwinner,sun8i-r40-ahci", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ahci_sunxi_of_match);

static struct platform_driver ahci_sunxi_driver = {
	.probe = ahci_sunxi_probe,
	.remove_new = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ahci_sunxi_of_match,
		.pm = &ahci_sunxi_pm_ops,
	},
};
module_platform_driver(ahci_sunxi_driver);

MODULE_DESCRIPTION("Allwinner sunxi AHCI SATA driver");
MODULE_AUTHOR("Olliver Schinagl <oliver@schinagl.nl>");
MODULE_LICENSE("GPL");
