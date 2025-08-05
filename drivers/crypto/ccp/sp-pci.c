// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure Processor device driver
 *
 * Copyright (C) 2013,2019 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/ccp.h>

#include "ccp-dev.h"
#include "psp-dev.h"
#include "hsti.h"

/* used for version string AA.BB.CC.DD */
#define AA				GENMASK(31, 24)
#define BB				GENMASK(23, 16)
#define CC				GENMASK(15, 8)
#define DD				GENMASK(7, 0)

#define MSIX_VECTORS			2

struct sp_pci {
	int msix_count;
	struct msix_entry msix_entry[MSIX_VECTORS];
};
static struct sp_device *sp_dev_master;

#define version_attribute_show(name, _offset)					\
static ssize_t name##_show(struct device *d, struct device_attribute *attr,	\
			   char *buf)						\
{										\
	struct sp_device *sp = dev_get_drvdata(d);				\
	struct psp_device *psp = sp->psp_data;					\
	unsigned int val = ioread32(psp->io_regs + _offset);			\
	return sysfs_emit(buf, "%02lx.%02lx.%02lx.%02lx\n",			\
			  FIELD_GET(AA, val),			\
			  FIELD_GET(BB, val),			\
			  FIELD_GET(CC, val),			\
			  FIELD_GET(DD, val));			\
}

version_attribute_show(bootloader_version, psp->vdata->bootloader_info_reg)
static DEVICE_ATTR_RO(bootloader_version);
version_attribute_show(tee_version, psp->vdata->tee->info_reg)
static DEVICE_ATTR_RO(tee_version);

static struct attribute *psp_firmware_attrs[] = {
	&dev_attr_bootloader_version.attr,
	&dev_attr_tee_version.attr,
	NULL,
};

static umode_t psp_firmware_is_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct sp_device *sp = dev_get_drvdata(dev);
	struct psp_device *psp = sp->psp_data;
	unsigned int val = 0xffffffff;

	if (!psp)
		return 0;

	if (attr == &dev_attr_bootloader_version.attr &&
	    psp->vdata->bootloader_info_reg)
		val = ioread32(psp->io_regs + psp->vdata->bootloader_info_reg);

	if (attr == &dev_attr_tee_version.attr && psp->capability.tee &&
	    psp->vdata->tee->info_reg)
		val = ioread32(psp->io_regs + psp->vdata->tee->info_reg);

	/* If platform disallows accessing this register it will be all f's */
	if (val != 0xffffffff)
		return 0444;

	return 0;
}

static struct attribute_group psp_firmware_attr_group = {
	.attrs = psp_firmware_attrs,
	.is_visible = psp_firmware_is_visible,
};

static const struct attribute_group *psp_groups[] = {
#ifdef CONFIG_CRYPTO_DEV_SP_PSP
	&psp_security_attr_group,
#endif
	&psp_firmware_attr_group,
	NULL,
};

static int sp_get_msix_irqs(struct sp_device *sp)
{
	struct sp_pci *sp_pci = sp->dev_specific;
	struct device *dev = sp->dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	int v, ret;

	for (v = 0; v < ARRAY_SIZE(sp_pci->msix_entry); v++)
		sp_pci->msix_entry[v].entry = v;

	ret = pci_enable_msix_range(pdev, sp_pci->msix_entry, 1, v);
	if (ret < 0)
		return ret;

	sp_pci->msix_count = ret;
	sp->use_tasklet = true;

	sp->psp_irq = sp_pci->msix_entry[0].vector;
	sp->ccp_irq = (sp_pci->msix_count > 1) ? sp_pci->msix_entry[1].vector
					       : sp_pci->msix_entry[0].vector;
	return 0;
}

static int sp_get_msi_irq(struct sp_device *sp)
{
	struct device *dev = sp->dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = pci_enable_msi(pdev);
	if (ret)
		return ret;

	sp->ccp_irq = pdev->irq;
	sp->psp_irq = pdev->irq;

	return 0;
}

static int sp_get_irqs(struct sp_device *sp)
{
	struct device *dev = sp->dev;
	int ret;

	ret = sp_get_msix_irqs(sp);
	if (!ret)
		return 0;

	/* Couldn't get MSI-X vectors, try MSI */
	dev_notice(dev, "could not enable MSI-X (%d), trying MSI\n", ret);
	ret = sp_get_msi_irq(sp);
	if (!ret)
		return 0;

	/* Couldn't get MSI interrupt */
	dev_notice(dev, "could not enable MSI (%d)\n", ret);

	return ret;
}

static void sp_free_irqs(struct sp_device *sp)
{
	struct sp_pci *sp_pci = sp->dev_specific;
	struct device *dev = sp->dev;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (sp_pci->msix_count)
		pci_disable_msix(pdev);
	else if (sp->psp_irq)
		pci_disable_msi(pdev);

	sp->ccp_irq = 0;
	sp->psp_irq = 0;
}

static bool sp_pci_is_master(struct sp_device *sp)
{
	struct device *dev_cur, *dev_new;
	struct pci_dev *pdev_cur, *pdev_new;

	dev_new = sp->dev;
	dev_cur = sp_dev_master->dev;

	pdev_new = to_pci_dev(dev_new);
	pdev_cur = to_pci_dev(dev_cur);

	if (pci_domain_nr(pdev_new->bus) != pci_domain_nr(pdev_cur->bus))
		return pci_domain_nr(pdev_new->bus) < pci_domain_nr(pdev_cur->bus);

	if (pdev_new->bus->number != pdev_cur->bus->number)
		return pdev_new->bus->number < pdev_cur->bus->number;

	if (PCI_SLOT(pdev_new->devfn) != PCI_SLOT(pdev_cur->devfn))
		return PCI_SLOT(pdev_new->devfn) < PCI_SLOT(pdev_cur->devfn);

	if (PCI_FUNC(pdev_new->devfn) != PCI_FUNC(pdev_cur->devfn))
		return PCI_FUNC(pdev_new->devfn) < PCI_FUNC(pdev_cur->devfn);

	return false;
}

static void psp_set_master(struct sp_device *sp)
{
	if (!sp_dev_master) {
		sp_dev_master = sp;
		return;
	}

	if (sp_pci_is_master(sp))
		sp_dev_master = sp;
}

static struct sp_device *psp_get_master(void)
{
	return sp_dev_master;
}

static void psp_clear_master(struct sp_device *sp)
{
	if (sp == sp_dev_master) {
		sp_dev_master = NULL;
		dev_dbg(sp->dev, "Cleared sp_dev_master\n");
	}
}

static int sp_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct sp_device *sp;
	struct sp_pci *sp_pci;
	struct device *dev = &pdev->dev;
	void __iomem * const *iomap_table;
	int bar_mask;
	int ret;

	ret = -ENOMEM;
	sp = sp_alloc_struct(dev);
	if (!sp)
		goto e_err;

	sp_pci = devm_kzalloc(dev, sizeof(*sp_pci), GFP_KERNEL);
	if (!sp_pci)
		goto e_err;

	sp->dev_specific = sp_pci;
	sp->dev_vdata = (struct sp_dev_vdata *)id->driver_data;
	if (!sp->dev_vdata) {
		ret = -ENODEV;
		dev_err(dev, "missing driver data\n");
		goto e_err;
	}

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "pcim_enable_device failed (%d)\n", ret);
		goto e_err;
	}

	bar_mask = pci_select_bars(pdev, IORESOURCE_MEM);
	ret = pcim_iomap_regions(pdev, bar_mask, "ccp");
	if (ret) {
		dev_err(dev, "pcim_iomap_regions failed (%d)\n", ret);
		goto e_err;
	}

	iomap_table = pcim_iomap_table(pdev);
	if (!iomap_table) {
		dev_err(dev, "pcim_iomap_table failed\n");
		ret = -ENOMEM;
		goto e_err;
	}

	sp->io_map = iomap_table[sp->dev_vdata->bar];
	if (!sp->io_map) {
		dev_err(dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto e_err;
	}

	ret = sp_get_irqs(sp);
	if (ret)
		goto e_err;

	pci_set_master(pdev);
	sp->set_psp_master_device = psp_set_master;
	sp->get_psp_master_device = psp_get_master;
	sp->clear_psp_master_device = psp_clear_master;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (ret) {
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(dev, "dma_set_mask_and_coherent failed (%d)\n",
				ret);
			goto free_irqs;
		}
	}

	dev_set_drvdata(dev, sp);

	ret = sp_init(sp);
	if (ret)
		goto free_irqs;

	return 0;

free_irqs:
	sp_free_irqs(sp);
e_err:
	dev_notice(dev, "initialization failed\n");
	return ret;
}

static void sp_pci_shutdown(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct sp_device *sp = dev_get_drvdata(dev);

	if (!sp)
		return;

	sp_destroy(sp);
}

static void sp_pci_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct sp_device *sp = dev_get_drvdata(dev);

	if (!sp)
		return;

	sp_destroy(sp);

	sp_free_irqs(sp);
}

static int __maybe_unused sp_pci_suspend(struct device *dev)
{
	struct sp_device *sp = dev_get_drvdata(dev);

	return sp_suspend(sp);
}

static int __maybe_unused sp_pci_resume(struct device *dev)
{
	struct sp_device *sp = dev_get_drvdata(dev);

	return sp_resume(sp);
}

#ifdef CONFIG_CRYPTO_DEV_SP_PSP
static const struct sev_vdata sevv1 = {
	.cmdresp_reg		= 0x10580,	/* C2PMSG_32 */
	.cmdbuff_addr_lo_reg	= 0x105e0,	/* C2PMSG_56 */
	.cmdbuff_addr_hi_reg	= 0x105e4,	/* C2PMSG_57 */
};

static const struct sev_vdata sevv2 = {
	.cmdresp_reg		= 0x10980,	/* C2PMSG_32 */
	.cmdbuff_addr_lo_reg	= 0x109e0,	/* C2PMSG_56 */
	.cmdbuff_addr_hi_reg	= 0x109e4,	/* C2PMSG_57 */
};

static const struct tee_vdata teev1 = {
	.ring_wptr_reg          = 0x10550,	/* C2PMSG_20 */
	.ring_rptr_reg          = 0x10554,	/* C2PMSG_21 */
	.info_reg		= 0x109e8,	/* C2PMSG_58 */
};

static const struct tee_vdata teev2 = {
	.ring_wptr_reg		= 0x10950,	/* C2PMSG_20 */
	.ring_rptr_reg		= 0x10954,	/* C2PMSG_21 */
	.info_reg		= 0x109e8,	/* C2PMSG_58 */
};

static const struct platform_access_vdata pa_v1 = {
	.cmdresp_reg		= 0x10570,	/* C2PMSG_28 */
	.cmdbuff_addr_lo_reg	= 0x10574,	/* C2PMSG_29 */
	.cmdbuff_addr_hi_reg	= 0x10578,	/* C2PMSG_30 */
	.doorbell_button_reg	= 0x10a24,	/* C2PMSG_73 */
	.doorbell_cmd_reg	= 0x10a40,	/* C2PMSG_80 */
};

static const struct platform_access_vdata pa_v2 = {
	.doorbell_button_reg	= 0x10a24,	/* C2PMSG_73 */
	.doorbell_cmd_reg	= 0x10a40,	/* C2PMSG_80 */
};

static const struct psp_vdata pspv1 = {
	.sev			= &sevv1,
	.bootloader_info_reg	= 0x105ec,	/* C2PMSG_59 */
	.feature_reg		= 0x105fc,	/* C2PMSG_63 */
	.inten_reg		= 0x10610,	/* P2CMSG_INTEN */
	.intsts_reg		= 0x10614,	/* P2CMSG_INTSTS */
};

static const struct psp_vdata pspv2 = {
	.sev			= &sevv2,
	.platform_access	= &pa_v1,
	.bootloader_info_reg	= 0x109ec,	/* C2PMSG_59 */
	.feature_reg		= 0x109fc,	/* C2PMSG_63 */
	.inten_reg		= 0x10690,	/* P2CMSG_INTEN */
	.intsts_reg		= 0x10694,	/* P2CMSG_INTSTS */
	.platform_features	= PLATFORM_FEATURE_HSTI,
};

static const struct psp_vdata pspv3 = {
	.tee			= &teev1,
	.platform_access	= &pa_v1,
	.cmdresp_reg		= 0x10544,	/* C2PMSG_17 */
	.cmdbuff_addr_lo_reg	= 0x10548,	/* C2PMSG_18 */
	.cmdbuff_addr_hi_reg	= 0x1054c,	/* C2PMSG_19 */
	.bootloader_info_reg	= 0x109ec,	/* C2PMSG_59 */
	.feature_reg		= 0x109fc,	/* C2PMSG_63 */
	.inten_reg		= 0x10690,	/* P2CMSG_INTEN */
	.intsts_reg		= 0x10694,	/* P2CMSG_INTSTS */
	.platform_features	= PLATFORM_FEATURE_DBC |
				  PLATFORM_FEATURE_HSTI,
};

static const struct psp_vdata pspv4 = {
	.sev			= &sevv2,
	.tee			= &teev1,
	.cmdresp_reg		= 0x10544,	/* C2PMSG_17 */
	.cmdbuff_addr_lo_reg	= 0x10548,	/* C2PMSG_18 */
	.cmdbuff_addr_hi_reg	= 0x1054c,	/* C2PMSG_19 */
	.bootloader_info_reg	= 0x109ec,	/* C2PMSG_59 */
	.feature_reg		= 0x109fc,	/* C2PMSG_63 */
	.inten_reg		= 0x10690,	/* P2CMSG_INTEN */
	.intsts_reg		= 0x10694,	/* P2CMSG_INTSTS */
};

static const struct psp_vdata pspv5 = {
	.tee			= &teev2,
	.platform_access	= &pa_v2,
	.cmdresp_reg		= 0x10944,	/* C2PMSG_17 */
	.cmdbuff_addr_lo_reg	= 0x10948,	/* C2PMSG_18 */
	.cmdbuff_addr_hi_reg	= 0x1094c,	/* C2PMSG_19 */
	.bootloader_info_reg	= 0x109ec,	/* C2PMSG_59 */
	.feature_reg		= 0x109fc,	/* C2PMSG_63 */
	.inten_reg		= 0x10510,	/* P2CMSG_INTEN */
	.intsts_reg		= 0x10514,	/* P2CMSG_INTSTS */
};

static const struct psp_vdata pspv6 = {
	.sev                    = &sevv2,
	.tee                    = &teev2,
	.cmdresp_reg		= 0x10944,	/* C2PMSG_17 */
	.cmdbuff_addr_lo_reg	= 0x10948,	/* C2PMSG_18 */
	.cmdbuff_addr_hi_reg	= 0x1094c,	/* C2PMSG_19 */
	.bootloader_info_reg	= 0x109ec,	/* C2PMSG_59 */
	.feature_reg            = 0x109fc,	/* C2PMSG_63 */
	.inten_reg              = 0x10510,	/* P2CMSG_INTEN */
	.intsts_reg             = 0x10514,	/* P2CMSG_INTSTS */
};

#endif

static const struct sp_dev_vdata dev_vdata[] = {
	{	/* 0 */
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_CCP
		.ccp_vdata = &ccpv3,
#endif
	},
	{	/* 1 */
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_CCP
		.ccp_vdata = &ccpv5a,
#endif
#ifdef CONFIG_CRYPTO_DEV_SP_PSP
		.psp_vdata = &pspv1,
#endif
	},
	{	/* 2 */
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_CCP
		.ccp_vdata = &ccpv5b,
#endif
	},
	{	/* 3 */
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_CCP
		.ccp_vdata = &ccpv5a,
#endif
#ifdef CONFIG_CRYPTO_DEV_SP_PSP
		.psp_vdata = &pspv2,
#endif
	},
	{	/* 4 */
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_CCP
		.ccp_vdata = &ccpv5a,
#endif
#ifdef CONFIG_CRYPTO_DEV_SP_PSP
		.psp_vdata = &pspv3,
#endif
	},
	{	/* 5 */
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_PSP
		.psp_vdata = &pspv4,
#endif
	},
	{	/* 6 */
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_PSP
		.psp_vdata = &pspv3,
#endif
	},
	{	/* 7 */
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_PSP
		.psp_vdata = &pspv5,
#endif
	},
	{	/* 8 */
		.bar = 2,
#ifdef CONFIG_CRYPTO_DEV_SP_PSP
		.psp_vdata = &pspv6,
#endif
	},
};
static const struct pci_device_id sp_pci_table[] = {
	{ PCI_VDEVICE(AMD, 0x1537), (kernel_ulong_t)&dev_vdata[0] },
	{ PCI_VDEVICE(AMD, 0x1456), (kernel_ulong_t)&dev_vdata[1] },
	{ PCI_VDEVICE(AMD, 0x1468), (kernel_ulong_t)&dev_vdata[2] },
	{ PCI_VDEVICE(AMD, 0x1486), (kernel_ulong_t)&dev_vdata[3] },
	{ PCI_VDEVICE(AMD, 0x15DF), (kernel_ulong_t)&dev_vdata[4] },
	{ PCI_VDEVICE(AMD, 0x14CA), (kernel_ulong_t)&dev_vdata[5] },
	{ PCI_VDEVICE(AMD, 0x15C7), (kernel_ulong_t)&dev_vdata[6] },
	{ PCI_VDEVICE(AMD, 0x1649), (kernel_ulong_t)&dev_vdata[6] },
	{ PCI_VDEVICE(AMD, 0x1134), (kernel_ulong_t)&dev_vdata[7] },
	{ PCI_VDEVICE(AMD, 0x17E0), (kernel_ulong_t)&dev_vdata[7] },
	{ PCI_VDEVICE(AMD, 0x156E), (kernel_ulong_t)&dev_vdata[8] },
	{ PCI_VDEVICE(AMD, 0x17D8), (kernel_ulong_t)&dev_vdata[8] },
	/* Last entry must be zero */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sp_pci_table);

static SIMPLE_DEV_PM_OPS(sp_pci_pm_ops, sp_pci_suspend, sp_pci_resume);

static struct pci_driver sp_pci_driver = {
	.name = "ccp",
	.id_table = sp_pci_table,
	.probe = sp_pci_probe,
	.remove = sp_pci_remove,
	.shutdown = sp_pci_shutdown,
	.driver.pm = &sp_pci_pm_ops,
	.dev_groups = psp_groups,
};

int sp_pci_init(void)
{
	return pci_register_driver(&sp_pci_driver);
}

void sp_pci_exit(void)
{
	pci_unregister_driver(&sp_pci_driver);
}
