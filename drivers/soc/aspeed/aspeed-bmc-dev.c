// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) ASPEED Technology Inc.

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>

#define DEVICE_NAME     "bmc-device"
#define SCU_TRIGGER_MSI
struct aspeed_bmc_device {
	unsigned char *host2bmc_base_virt;
	struct device *dev;
	struct miscdevice	miscdev;
	void __iomem	*reg_base;
	void __iomem	*bmc_mem_virt;
	dma_addr_t bmc_mem_phy;
	struct bin_attribute	bin0;
	struct bin_attribute	bin1;
	struct regmap			*scu;

//	phys_addr_t		mem_base;
//	resource_size_t		mem_size;

	struct kernfs_node	*kn0;
	struct kernfs_node	*kn1;

	int pcie2lpc;
	unsigned int irq;
};

#define BMC_MEM_BAR_SIZE		0x100000
#define BMC_QUEUE_SIZE			(16 * 4)
/* =================== SCU Define ================================================ */
#define ASPEED_SCU04				0x04
#define AST2600A3_SCU04	0x05030303
#define ASPEED_SCUC20				0xC20
#define ASPEED_SCUC24				0xC24
#define MSI_ROUTING_MASK		GENMASK(11, 10)
#define PCIDEV1_INTX_MSI_HOST2BMC_EN	BIT(18)
#define MSI_ROUTING_PCIe2LPC_PCIDEV0	(0x1 << 10)
#define MSI_ROUTING_PCIe2LPC_PCIDEV1	(0x2 << 10)
/* ================================================================================== */
#define ASPEED_BMC_MEM_BAR			0xF10
#define  PCIE2PCI_MEM_BAR_ENABLE		BIT(1)
#define  HOST2BMC_MEM_BAR_ENABLE		BIT(0)
#define ASPEED_BMC_MEM_BAR_REMAP	0xF18

#define ASPEED_BMC_SHADOW_CTRL		0xF50
#define  READ_ONLY_MASK					BIT(31)
#define  MASK_BAR1						BIT(2)
#define  MASK_BAR0						BIT(1)
#define  SHADOW_CFG						BIT(0)

#define ASPEED_BMC_HOST2BMC_Q1		0xA000
#define ASPEED_BMC_HOST2BMC_Q2		0xA010
#define ASPEED_BMC_BMC2HOST_Q1		0xA020
#define ASPEED_BMC_BMC2HOST_Q2		0xA030
#define ASPEED_BMC_BMC2HOST_STS		0xA040
#define	 BMC2HOST_INT_STS_DOORBELL		BIT(31)
#define	 BMC2HOST_ENABLE_INTB			BIT(30)
/* */
#define	 BMC2HOST_Q1_FULL				BIT(27)
#define	 BMC2HOST_Q1_EMPTY				BIT(26)
#define	 BMC2HOST_Q2_FULL				BIT(25)
#define	 BMC2HOST_Q2_EMPTY				BIT(24)
#define	 BMC2HOST_Q1_FULL_UNMASK		BIT(23)
#define	 BMC2HOST_Q1_EMPTY_UNMASK		BIT(22)
#define	 BMC2HOST_Q2_FULL_UNMASK		BIT(21)
#define	 BMC2HOST_Q2_EMPTY_UNMASK		BIT(20)

#define ASPEED_BMC_HOST2BMC_STS		0xA044
#define	 HOST2BMC_INT_STS_DOORBELL		BIT(31)
#define	 HOST2BMC_ENABLE_INTB			BIT(30)
#define	 HOST2BMC_Q1_FULL				BIT(27)
#define	 HOST2BMC_Q1_EMPTY				BIT(26)
#define	 HOST2BMC_Q2_FULL				BIT(25)
#define	 HOST2BMC_Q2_EMPTY				BIT(24)
#define	 HOST2BMC_Q1_FULL_UNMASK		BIT(23)
#define	 HOST2BMC_Q1_EMPTY_UNMASK		BIT(22)
#define	 HOST2BMC_Q2_FULL_UNMASK		BIT(21)
#define	 HOST2BMC_Q2_EMPTY_UNMASK		BIT(20)

#define ASPEED_SCU_PCIE_CONF_CTRL	0xC20
#define  SCU_PCIE_CONF_BMC_DEV_EN			 BIT(8)
#define  SCU_PCIE_CONF_BMC_DEV_EN_MMIO		 BIT(9)
#define  SCU_PCIE_CONF_BMC_DEV_EN_MSI		 BIT(11)
#define  SCU_PCIE_CONF_BMC_DEV_EN_IRQ		 BIT(13)
#define  SCU_PCIE_CONF_BMC_DEV_EN_DMA		 BIT(14)
#define  SCU_PCIE_CONF_BMC_DEV_EN_E2L		 BIT(15)
#define  SCU_PCIE_CONF_BMC_DEV_EN_LPC_DECODE BIT(21)

#define ASPEED_SCU_BMC_DEV_CLASS	0xC68

static struct aspeed_bmc_device *file_aspeed_bmc_device(struct file *file)
{
	return container_of(file->private_data, struct aspeed_bmc_device,
			miscdev);
}

static int aspeed_bmc_device_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct aspeed_bmc_device *bmc_device = file_aspeed_bmc_device(file);
	unsigned long vsize = vma->vm_end - vma->vm_start;
	pgprot_t prot = vma->vm_page_prot;

	if (vma->vm_pgoff + vsize > bmc_device->bmc_mem_phy + 0x100000)
		return -EINVAL;

	prot = pgprot_noncached(prot);

	if (remap_pfn_range(vma, vma->vm_start,
		(bmc_device->bmc_mem_phy >> PAGE_SHIFT) + vma->vm_pgoff,
		vsize, prot))
		return -EAGAIN;

	return 0;
}

static const struct file_operations aspeed_bmc_device_fops = {
	.owner		= THIS_MODULE,
	.mmap		= aspeed_bmc_device_mmap,
};

static ssize_t aspeed_host2bmc_queue1_rx(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct aspeed_bmc_device *bmc_device = dev_get_drvdata(container_of(kobj, struct device, kobj));
	u32 *data = (u32 *) buf;
	int rc;

	if (readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS) & HOST2BMC_Q1_EMPTY)
		rc = 0;
	else {
		data[0] = readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_Q1);
		rc = 4;
	}

	return rc;
}

static ssize_t aspeed_host2bmc_queue2_rx(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct aspeed_bmc_device *bmc_device = dev_get_drvdata(container_of(kobj, struct device, kobj));
	u32 *data = (u32 *) buf;
	int rc;

	if (!(readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS) & HOST2BMC_Q2_EMPTY)) {
		data[0] = readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_Q2);
		rc = 4;
	} else
		rc = 0;

	return rc;
}

static ssize_t aspeed_bmc2host_queue1_tx(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct aspeed_bmc_device *bmc_device = dev_get_drvdata(container_of(kobj, struct device, kobj));
	u32 tx_buff;
	u32 scu_id;
	int rc;

	if (count != 4)
		return -EINVAL;

	if (readl(bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS) & BMC2HOST_Q1_FULL)
		rc = -ENOSPC;
	else {
		memcpy(&tx_buff, buf, 4);
		writel(tx_buff, bmc_device->reg_base + ASPEED_BMC_BMC2HOST_Q1);
		/* trigger to host
		 * Only After AST2600A3 support DoorBell MSI
		 */
		regmap_read(bmc_device->scu, ASPEED_SCU04, &scu_id);
		if (scu_id == AST2600A3_SCU04) {
			writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB, bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
		} else {
			//A0 : BIT(12) A1 : BIT(15)
			regmap_update_bits(bmc_device->scu, 0x560, BIT(15), BIT(15));
			regmap_update_bits(bmc_device->scu, 0x560, BIT(15), 0);
		}
		rc = 4;
	}
	return rc;
}

static ssize_t aspeed_bmc2host_queue2_tx(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct aspeed_bmc_device *bmc_device = dev_get_drvdata(container_of(kobj, struct device, kobj));
	u32 tx_buff = 0;
	u32 scu_id;
	int rc;

	if (count != 4)
		return -EINVAL;

	if (readl(bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS) & BMC2HOST_Q2_FULL)
		rc = -ENOSPC;
	else {
		memcpy(&tx_buff, buf, 4);
		writel(tx_buff, bmc_device->reg_base + ASPEED_BMC_BMC2HOST_Q2);
		/* trigger to host
		 * Only After AST2600A3 support DoorBell MSI
		 */
		regmap_read(bmc_device->scu, ASPEED_SCU04, &scu_id);
		if (scu_id == AST2600A3_SCU04) {
			writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB, bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
		} else {
			//A0 : BIT(12) A1 : BIT(15)
			regmap_update_bits(bmc_device->scu, 0x560, BIT(15), BIT(15));
			regmap_update_bits(bmc_device->scu, 0x560, BIT(15), 0);
		}
		rc = 4;
	}
	return rc;
}

static irqreturn_t aspeed_bmc_dev_isr(int irq, void *dev_id)
{
	struct aspeed_bmc_device *bmc_device = dev_id;

	u32 host2bmc_q_sts = readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS);

//	printk("%s host2bmc_q_sts is %x\n", __FUNCTION__, host2bmc_q_sts);

	if (host2bmc_q_sts & HOST2BMC_INT_STS_DOORBELL)
		writel(HOST2BMC_INT_STS_DOORBELL, bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS);

	if (host2bmc_q_sts & HOST2BMC_ENABLE_INTB)
		writel(HOST2BMC_ENABLE_INTB, bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS);

	if (host2bmc_q_sts & HOST2BMC_Q1_FULL)
		dev_info(bmc_device->dev, "Q1 Full\n");

	if (host2bmc_q_sts & HOST2BMC_Q2_FULL)
		dev_info(bmc_device->dev, "Q2 Full\n");

	return IRQ_HANDLED;
}

static void aspeed_bmc_device_init(struct aspeed_bmc_device *bmc_device)
{
	u32 pcie_config_ctl = SCU_PCIE_CONF_BMC_DEV_EN_IRQ | SCU_PCIE_CONF_BMC_DEV_EN_MMIO | SCU_PCIE_CONF_BMC_DEV_EN;
	u32 scu_id;

	if (bmc_device->pcie2lpc)
		pcie_config_ctl |= SCU_PCIE_CONF_BMC_DEV_EN_E2L | SCU_PCIE_CONF_BMC_DEV_EN_LPC_DECODE;

	regmap_update_bits(bmc_device->scu, ASPEED_SCU_PCIE_CONF_CTRL, pcie_config_ctl,
			pcie_config_ctl);

	/* update class code to others as it is a MFD device */
	regmap_write(bmc_device->scu, ASPEED_SCU_BMC_DEV_CLASS, 0xff000000);

#ifdef SCU_TRIGGER_MSI
	//SCUC24[17]: Enable PCI device 1 INTx/MSI from SCU560[15]. Will be added in next version
	regmap_update_bits(bmc_device->scu, ASPEED_SCUC20, BIT(11) | BIT(14), BIT(11) | BIT(14));

	regmap_read(bmc_device->scu, ASPEED_SCU04, &scu_id);
	if (scu_id == AST2600A3_SCU04)
		regmap_update_bits(bmc_device->scu, ASPEED_SCUC24,
				PCIDEV1_INTX_MSI_HOST2BMC_EN | MSI_ROUTING_MASK,
				PCIDEV1_INTX_MSI_HOST2BMC_EN | MSI_ROUTING_PCIe2LPC_PCIDEV1);
	else
		regmap_update_bits(bmc_device->scu, ASPEED_SCUC24, BIT(17) | BIT(14) | BIT(11), BIT(17) | BIT(14) | BIT(11));
#else
	//SCUC24[18]: Enable PCI device 1 INTx/MSI from Host-to-BMC controller. Will be added in next version
	regmap_update_bits(bmc_device->scu, 0xc24, BIT(18) | BIT(14), BIT(18) | BIT(14));
#endif

	writel(~(BMC_MEM_BAR_SIZE - 1) | HOST2BMC_MEM_BAR_ENABLE, bmc_device->reg_base + ASPEED_BMC_MEM_BAR);
	writel(bmc_device->bmc_mem_phy, bmc_device->reg_base + ASPEED_BMC_MEM_BAR_REMAP);

	//Setting BMC to Host Q register
	writel(BMC2HOST_Q2_FULL_UNMASK | BMC2HOST_Q1_FULL_UNMASK | BMC2HOST_ENABLE_INTB, bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
	writel(HOST2BMC_Q2_FULL_UNMASK | HOST2BMC_Q1_FULL_UNMASK | HOST2BMC_ENABLE_INTB, bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS);
}

static const struct of_device_id aspeed_bmc_device_of_matches[] = {
	{ .compatible = "aspeed,ast2600-bmc-device", },
	{},
};
MODULE_DEVICE_TABLE(of, aspeed_bmc_device_of_matches);

static int aspeed_bmc_device_probe(struct platform_device *pdev)
{
	struct aspeed_bmc_device *bmc_device;
	struct device *dev = &pdev->dev;
	int ret = 0;

	bmc_device = devm_kzalloc(&pdev->dev, sizeof(struct aspeed_bmc_device), GFP_KERNEL);
	if (!bmc_device)
		return -ENOMEM;

	bmc_device->dev = dev;
	bmc_device->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(bmc_device->reg_base))
		goto out_region;

	bmc_device->scu = syscon_regmap_lookup_by_phandle(dev->of_node, "aspeed,scu");
	if (IS_ERR(bmc_device->scu)) {
		dev_err(&pdev->dev, "failed to find SCU regmap\n");
		goto out_region;
	}

	if (of_property_read_bool(dev->of_node, "pcie2lpc"))
		bmc_device->pcie2lpc = 1;

	bmc_device->bmc_mem_virt = dma_alloc_coherent(&pdev->dev, BMC_MEM_BAR_SIZE, &bmc_device->bmc_mem_phy, GFP_KERNEL);
	memset(bmc_device->bmc_mem_virt, 0, BMC_MEM_BAR_SIZE);

//	printk("virt=%p phy %x\n", bmc_device->bmc_mem_virt, bmc_device->bmc_mem_phy);

	sysfs_bin_attr_init(&bmc_device->bin0);
	sysfs_bin_attr_init(&bmc_device->bin1);

	bmc_device->bin0.attr.name = "bmc-dev-queue1";
	bmc_device->bin0.attr.mode = 0600;
	bmc_device->bin0.read = aspeed_host2bmc_queue1_rx;
	bmc_device->bin0.write = aspeed_bmc2host_queue1_tx;
	bmc_device->bin0.size = 4;

	ret = sysfs_create_bin_file(&pdev->dev.kobj, &bmc_device->bin0);
	if (ret) {
		dev_err(dev, "error for bin file\n");
		goto out_dma;
	}

	bmc_device->kn0 = kernfs_find_and_get(dev->kobj.sd, bmc_device->bin0.attr.name);
	if (!bmc_device->kn0) {
		sysfs_remove_bin_file(&dev->kobj, &bmc_device->bin0);
		goto out_dma;
	}

	bmc_device->bin1.attr.name = "bmc-dev-queue2";
	bmc_device->bin1.attr.mode = 0600;
	bmc_device->bin1.read = aspeed_host2bmc_queue2_rx;
	bmc_device->bin1.write = aspeed_bmc2host_queue2_tx;
	bmc_device->bin1.size = 4;

	ret = sysfs_create_bin_file(&pdev->dev.kobj, &bmc_device->bin1);
	if (ret) {
		dev_err(dev, "error for bin file ");
		goto out_dma;
	}

	bmc_device->kn1 = kernfs_find_and_get(dev->kobj.sd, bmc_device->bin1.attr.name);
	if (!bmc_device->kn1) {
		sysfs_remove_bin_file(&dev->kobj, &bmc_device->bin1);
		goto out_dma;
	}

	dev_set_drvdata(dev, bmc_device);

	aspeed_bmc_device_init(bmc_device);

	bmc_device->irq =  platform_get_irq(pdev, 0);
	if (bmc_device->irq < 0) {
		dev_err(&pdev->dev, "platform get of irq[=%d] failed!\n", bmc_device->irq);
		goto out_unmap;
	}

	ret = devm_request_irq(&pdev->dev, bmc_device->irq, aspeed_bmc_dev_isr,
							0, dev_name(&pdev->dev), bmc_device);
	if (ret) {
		dev_err(dev, "aspeed bmc device Unable to get IRQ");
		goto out_unmap;
	}

	bmc_device->miscdev.minor = MISC_DYNAMIC_MINOR;
	bmc_device->miscdev.name = DEVICE_NAME;
	bmc_device->miscdev.fops = &aspeed_bmc_device_fops;
	bmc_device->miscdev.parent = dev;
	ret = misc_register(&bmc_device->miscdev);
	if (ret) {
		dev_err(dev, "Unable to register device\n");
		goto out_irq;
	}

	dev_info(dev, "aspeed bmc device: driver successfully loaded.\n");

	return 0;

out_irq:
	devm_free_irq(&pdev->dev, bmc_device->irq, bmc_device);

out_unmap:
	iounmap(bmc_device->reg_base);

out_dma:
	dma_free_coherent(&pdev->dev, BMC_MEM_BAR_SIZE, bmc_device->bmc_mem_virt, bmc_device->bmc_mem_phy);

out_region:
	devm_kfree(&pdev->dev, bmc_device);
	dev_warn(dev, "aspeed bmc device: driver init failed (ret=%d)!\n", ret);
	return ret;
}

static int  aspeed_bmc_device_remove(struct platform_device *pdev)
{
	struct aspeed_bmc_device *bmc_device = platform_get_drvdata(pdev);

	misc_deregister(&bmc_device->miscdev);

	devm_free_irq(&pdev->dev, bmc_device->irq, bmc_device);

	iounmap(bmc_device->reg_base);

	dma_free_coherent(&pdev->dev, BMC_MEM_BAR_SIZE, bmc_device->bmc_mem_virt, bmc_device->bmc_mem_phy);

	devm_kfree(&pdev->dev, bmc_device);

	return 0;
}


static struct platform_driver aspeed_bmc_device_driver = {
	.probe		= aspeed_bmc_device_probe,
	.remove		= aspeed_bmc_device_remove,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table = aspeed_bmc_device_of_matches,
	},
};

module_platform_driver(aspeed_bmc_device_driver);

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED BMC DEVICE Driver");
MODULE_LICENSE("GPL");
