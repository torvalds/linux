// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom SATA3 AHCI Controller Driver
 *
 * Copyright © 2009-2015 Broadcom Corporation
 */

#include <linux/ahci_platform.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/libata.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/string.h>

#include "ahci.h"

#define DRV_NAME					"brcm-ahci"

#define SATA_TOP_CTRL_VERSION				0x0
#define SATA_TOP_CTRL_BUS_CTRL				0x4
 #define MMIO_ENDIAN_SHIFT				0 /* CPU->AHCI */
 #define DMADESC_ENDIAN_SHIFT				2 /* AHCI->DDR */
 #define DMADATA_ENDIAN_SHIFT				4 /* AHCI->DDR */
 #define PIODATA_ENDIAN_SHIFT				6
  #define ENDIAN_SWAP_NONE				0
  #define ENDIAN_SWAP_FULL				2
#define SATA_TOP_CTRL_TP_CTRL				0x8
#define SATA_TOP_CTRL_PHY_CTRL				0xc
 #define SATA_TOP_CTRL_PHY_CTRL_1			0x0
  #define SATA_TOP_CTRL_1_PHY_DEFAULT_POWER_STATE	BIT(14)
 #define SATA_TOP_CTRL_PHY_CTRL_2			0x4
  #define SATA_TOP_CTRL_2_SW_RST_MDIOREG		BIT(0)
  #define SATA_TOP_CTRL_2_SW_RST_OOB			BIT(1)
  #define SATA_TOP_CTRL_2_SW_RST_RX			BIT(2)
  #define SATA_TOP_CTRL_2_SW_RST_TX			BIT(3)
  #define SATA_TOP_CTRL_2_PHY_GLOBAL_RESET		BIT(14)
 #define SATA_TOP_CTRL_PHY_OFFS				0x8
 #define SATA_TOP_MAX_PHYS				2

#define SATA_FIRST_PORT_CTRL				0x700
#define SATA_NEXT_PORT_CTRL_OFFSET			0x80
#define SATA_PORT_PCTRL6(reg_base)			(reg_base + 0x18)

/* On big-endian MIPS, buses are reversed to big endian, so switch them back */
#if defined(CONFIG_MIPS) && defined(__BIG_ENDIAN)
#define DATA_ENDIAN			 2 /* AHCI->DDR inbound accesses */
#define MMIO_ENDIAN			 2 /* CPU->AHCI outbound accesses */
#else
#define DATA_ENDIAN			 0
#define MMIO_ENDIAN			 0
#endif

#define BUS_CTRL_ENDIAN_CONF				\
	((DATA_ENDIAN << DMADATA_ENDIAN_SHIFT) |	\
	(DATA_ENDIAN << DMADESC_ENDIAN_SHIFT) |		\
	(MMIO_ENDIAN << MMIO_ENDIAN_SHIFT))

#define BUS_CTRL_ENDIAN_NSP_CONF			\
	(0x02 << DMADATA_ENDIAN_SHIFT | 0x02 << DMADESC_ENDIAN_SHIFT)

#define BUS_CTRL_ENDIAN_CONF_MASK			\
	(0x3 << MMIO_ENDIAN_SHIFT | 0x3 << DMADESC_ENDIAN_SHIFT |	\
	 0x3 << DMADATA_ENDIAN_SHIFT | 0x3 << PIODATA_ENDIAN_SHIFT)

enum brcm_ahci_version {
	BRCM_SATA_BCM7425 = 1,
	BRCM_SATA_BCM7445,
	BRCM_SATA_NSP,
};

enum brcm_ahci_quirks {
	BRCM_AHCI_QUIRK_NO_NCQ		= BIT(0),
	BRCM_AHCI_QUIRK_SKIP_PHY_ENABLE	= BIT(1),
};

struct brcm_ahci_priv {
	struct device *dev;
	void __iomem *top_ctrl;
	u32 port_mask;
	u32 quirks;
	enum brcm_ahci_version version;
	struct reset_control *rcdev;
};

static inline u32 brcm_sata_readreg(void __iomem *addr)
{
	/*
	 * MIPS endianness is configured by boot strap, which also reverses all
	 * bus endianness (i.e., big-endian CPU + big endian bus ==> native
	 * endian I/O).
	 *
	 * Other architectures (e.g., ARM) either do not support big endian, or
	 * else leave I/O in little endian mode.
	 */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		return __raw_readl(addr);
	else
		return readl_relaxed(addr);
}

static inline void brcm_sata_writereg(u32 val, void __iomem *addr)
{
	/* See brcm_sata_readreg() comments */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, addr);
	else
		writel_relaxed(val, addr);
}

static void brcm_sata_alpm_init(struct ahci_host_priv *hpriv)
{
	struct brcm_ahci_priv *priv = hpriv->plat_data;
	u32 port_ctrl, host_caps;
	int i;

	/* Enable support for ALPM */
	host_caps = readl(hpriv->mmio + HOST_CAP);
	if (!(host_caps & HOST_CAP_ALPM))
		hpriv->flags |= AHCI_HFLAG_YES_ALPM;

	/*
	 * Adjust timeout to allow PLL sufficient time to lock while waking
	 * up from slumber mode.
	 */
	for (i = 0, port_ctrl = SATA_FIRST_PORT_CTRL;
	     i < SATA_TOP_MAX_PHYS;
	     i++, port_ctrl += SATA_NEXT_PORT_CTRL_OFFSET) {
		if (priv->port_mask & BIT(i))
			writel(0xff1003fc,
			       hpriv->mmio + SATA_PORT_PCTRL6(port_ctrl));
	}
}

static void brcm_sata_phy_enable(struct brcm_ahci_priv *priv, int port)
{
	void __iomem *phyctrl = priv->top_ctrl + SATA_TOP_CTRL_PHY_CTRL +
				(port * SATA_TOP_CTRL_PHY_OFFS);
	void __iomem *p;
	u32 reg;

	if (priv->quirks & BRCM_AHCI_QUIRK_SKIP_PHY_ENABLE)
		return;

	/* clear PHY_DEFAULT_POWER_STATE */
	p = phyctrl + SATA_TOP_CTRL_PHY_CTRL_1;
	reg = brcm_sata_readreg(p);
	reg &= ~SATA_TOP_CTRL_1_PHY_DEFAULT_POWER_STATE;
	brcm_sata_writereg(reg, p);

	/* reset the PHY digital logic */
	p = phyctrl + SATA_TOP_CTRL_PHY_CTRL_2;
	reg = brcm_sata_readreg(p);
	reg &= ~(SATA_TOP_CTRL_2_SW_RST_MDIOREG | SATA_TOP_CTRL_2_SW_RST_OOB |
		 SATA_TOP_CTRL_2_SW_RST_RX);
	reg |= SATA_TOP_CTRL_2_SW_RST_TX;
	brcm_sata_writereg(reg, p);
	reg = brcm_sata_readreg(p);
	reg |= SATA_TOP_CTRL_2_PHY_GLOBAL_RESET;
	brcm_sata_writereg(reg, p);
	reg = brcm_sata_readreg(p);
	reg &= ~SATA_TOP_CTRL_2_PHY_GLOBAL_RESET;
	brcm_sata_writereg(reg, p);
	(void)brcm_sata_readreg(p);
}

static void brcm_sata_phy_disable(struct brcm_ahci_priv *priv, int port)
{
	void __iomem *phyctrl = priv->top_ctrl + SATA_TOP_CTRL_PHY_CTRL +
				(port * SATA_TOP_CTRL_PHY_OFFS);
	void __iomem *p;
	u32 reg;

	if (priv->quirks & BRCM_AHCI_QUIRK_SKIP_PHY_ENABLE)
		return;

	/* power-off the PHY digital logic */
	p = phyctrl + SATA_TOP_CTRL_PHY_CTRL_2;
	reg = brcm_sata_readreg(p);
	reg |= (SATA_TOP_CTRL_2_SW_RST_MDIOREG | SATA_TOP_CTRL_2_SW_RST_OOB |
		SATA_TOP_CTRL_2_SW_RST_RX | SATA_TOP_CTRL_2_SW_RST_TX |
		SATA_TOP_CTRL_2_PHY_GLOBAL_RESET);
	brcm_sata_writereg(reg, p);

	/* set PHY_DEFAULT_POWER_STATE */
	p = phyctrl + SATA_TOP_CTRL_PHY_CTRL_1;
	reg = brcm_sata_readreg(p);
	reg |= SATA_TOP_CTRL_1_PHY_DEFAULT_POWER_STATE;
	brcm_sata_writereg(reg, p);
}

static void brcm_sata_phys_enable(struct brcm_ahci_priv *priv)
{
	int i;

	for (i = 0; i < SATA_TOP_MAX_PHYS; i++)
		if (priv->port_mask & BIT(i))
			brcm_sata_phy_enable(priv, i);
}

static void brcm_sata_phys_disable(struct brcm_ahci_priv *priv)
{
	int i;

	for (i = 0; i < SATA_TOP_MAX_PHYS; i++)
		if (priv->port_mask & BIT(i))
			brcm_sata_phy_disable(priv, i);
}

static u32 brcm_ahci_get_portmask(struct platform_device *pdev,
				  struct brcm_ahci_priv *priv)
{
	void __iomem *ahci;
	struct resource *res;
	u32 impl;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ahci");
	ahci = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ahci))
		return 0;

	impl = readl(ahci + HOST_PORTS_IMPL);

	if (fls(impl) > SATA_TOP_MAX_PHYS)
		dev_warn(priv->dev, "warning: more ports than PHYs (%#x)\n",
			 impl);
	else if (!impl)
		dev_info(priv->dev, "no ports found\n");

	devm_iounmap(&pdev->dev, ahci);
	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));

	return impl;
}

static void brcm_sata_init(struct brcm_ahci_priv *priv)
{
	void __iomem *ctrl = priv->top_ctrl + SATA_TOP_CTRL_BUS_CTRL;
	u32 data;

	/* Configure endianness */
	data = brcm_sata_readreg(ctrl);
	data &= ~BUS_CTRL_ENDIAN_CONF_MASK;
	if (priv->version == BRCM_SATA_NSP)
		data |= BUS_CTRL_ENDIAN_NSP_CONF;
	else
		data |= BUS_CTRL_ENDIAN_CONF;
	brcm_sata_writereg(data, ctrl);
}

static unsigned int brcm_ahci_read_id(struct ata_device *dev,
				      struct ata_taskfile *tf, u16 *id)
{
	struct ata_port *ap = dev->link->ap;
	struct ata_host *host = ap->host;
	struct ahci_host_priv *hpriv = host->private_data;
	struct brcm_ahci_priv *priv = hpriv->plat_data;
	void __iomem *mmio = hpriv->mmio;
	unsigned int err_mask;
	unsigned long flags;
	int i, rc;
	u32 ctl;

	/* Try to read the device ID and, if this fails, proceed with the
	 * recovery sequence below
	 */
	err_mask = ata_do_dev_read_id(dev, tf, id);
	if (likely(!err_mask))
		return err_mask;

	/* Disable host interrupts */
	spin_lock_irqsave(&host->lock, flags);
	ctl = readl(mmio + HOST_CTL);
	ctl &= ~HOST_IRQ_EN;
	writel(ctl, mmio + HOST_CTL);
	readl(mmio + HOST_CTL); /* flush */
	spin_unlock_irqrestore(&host->lock, flags);

	/* Perform the SATA PHY reset sequence */
	brcm_sata_phy_disable(priv, ap->port_no);

	/* Bring the PHY back on */
	brcm_sata_phy_enable(priv, ap->port_no);

	/* Re-initialize and calibrate the PHY */
	for (i = 0; i < hpriv->nports; i++) {
		rc = phy_init(hpriv->phys[i]);
		if (rc)
			goto disable_phys;

		rc = phy_calibrate(hpriv->phys[i]);
		if (rc) {
			phy_exit(hpriv->phys[i]);
			goto disable_phys;
		}
	}

	/* Re-enable host interrupts */
	spin_lock_irqsave(&host->lock, flags);
	ctl = readl(mmio + HOST_CTL);
	ctl |= HOST_IRQ_EN;
	writel(ctl, mmio + HOST_CTL);
	readl(mmio + HOST_CTL); /* flush */
	spin_unlock_irqrestore(&host->lock, flags);

	return ata_do_dev_read_id(dev, tf, id);

disable_phys:
	while (--i >= 0) {
		phy_power_off(hpriv->phys[i]);
		phy_exit(hpriv->phys[i]);
	}

	return AC_ERR_OTHER;
}

static void brcm_ahci_host_stop(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;

	ahci_platform_disable_resources(hpriv);
}

static struct ata_port_operations ahci_brcm_platform_ops = {
	.inherits	= &ahci_ops,
	.host_stop	= brcm_ahci_host_stop,
	.read_id	= brcm_ahci_read_id,
};

static const struct ata_port_info ahci_brcm_port_info = {
	.flags		= AHCI_FLAG_COMMON | ATA_FLAG_NO_DIPM,
	.link_flags	= ATA_LFLAG_NO_DB_DELAY,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_brcm_platform_ops,
};

#ifdef CONFIG_PM_SLEEP
static int brcm_ahci_suspend(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	struct brcm_ahci_priv *priv = hpriv->plat_data;
	int ret;

	ret = ahci_platform_suspend(dev);
	brcm_sata_phys_disable(priv);
	return ret;
}

static int brcm_ahci_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	struct brcm_ahci_priv *priv = hpriv->plat_data;

	brcm_sata_init(priv);
	brcm_sata_phys_enable(priv);
	brcm_sata_alpm_init(hpriv);
	return ahci_platform_resume(dev);
}
#endif

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static const struct of_device_id ahci_of_match[] = {
	{.compatible = "brcm,bcm7425-ahci", .data = (void *)BRCM_SATA_BCM7425},
	{.compatible = "brcm,bcm7445-ahci", .data = (void *)BRCM_SATA_BCM7445},
	{.compatible = "brcm,bcm63138-ahci", .data = (void *)BRCM_SATA_BCM7445},
	{.compatible = "brcm,bcm-nsp-ahci", .data = (void *)BRCM_SATA_NSP},
	{},
};
MODULE_DEVICE_TABLE(of, ahci_of_match);

static int brcm_ahci_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;
	struct brcm_ahci_priv *priv;
	struct ahci_host_priv *hpriv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	of_id = of_match_node(ahci_of_match, pdev->dev.of_node);
	if (!of_id)
		return -ENODEV;

	priv->version = (enum brcm_ahci_version)of_id->data;
	priv->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "top-ctrl");
	priv->top_ctrl = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->top_ctrl))
		return PTR_ERR(priv->top_ctrl);

	/* Reset is optional depending on platform */
	priv->rcdev = devm_reset_control_get(&pdev->dev, "ahci");
	if (!IS_ERR_OR_NULL(priv->rcdev))
		reset_control_deassert(priv->rcdev);

	if ((priv->version == BRCM_SATA_BCM7425) ||
		(priv->version == BRCM_SATA_NSP)) {
		priv->quirks |= BRCM_AHCI_QUIRK_NO_NCQ;
		priv->quirks |= BRCM_AHCI_QUIRK_SKIP_PHY_ENABLE;
	}

	brcm_sata_init(priv);

	priv->port_mask = brcm_ahci_get_portmask(pdev, priv);
	if (!priv->port_mask)
		return -ENODEV;

	brcm_sata_phys_enable(priv);

	hpriv = ahci_platform_get_resources(pdev, 0);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);
	hpriv->plat_data = priv;
	hpriv->flags = AHCI_HFLAG_WAKE_BEFORE_STOP;

	brcm_sata_alpm_init(hpriv);

	ret = ahci_platform_enable_resources(hpriv);
	if (ret)
		return ret;

	if (priv->quirks & BRCM_AHCI_QUIRK_NO_NCQ)
		hpriv->flags |= AHCI_HFLAG_NO_NCQ;
	hpriv->flags |= AHCI_HFLAG_NO_WRITE_TO_RO;

	ret = ahci_platform_init_host(pdev, hpriv, &ahci_brcm_port_info,
				      &ahci_platform_sht);
	if (ret)
		return ret;

	dev_info(dev, "Broadcom AHCI SATA3 registered\n");

	return 0;
}

static int brcm_ahci_remove(struct platform_device *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	struct ahci_host_priv *hpriv = host->private_data;
	struct brcm_ahci_priv *priv = hpriv->plat_data;
	int ret;

	ret = ata_platform_remove_one(pdev);
	if (ret)
		return ret;

	brcm_sata_phys_disable(priv);

	return 0;
}

static SIMPLE_DEV_PM_OPS(ahci_brcm_pm_ops, brcm_ahci_suspend, brcm_ahci_resume);

static struct platform_driver brcm_ahci_driver = {
	.probe = brcm_ahci_probe,
	.remove = brcm_ahci_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ahci_of_match,
		.pm = &ahci_brcm_pm_ops,
	},
};
module_platform_driver(brcm_ahci_driver);

MODULE_DESCRIPTION("Broadcom SATA3 AHCI Controller Driver");
MODULE_AUTHOR("Brian Norris");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sata-brcmstb");
