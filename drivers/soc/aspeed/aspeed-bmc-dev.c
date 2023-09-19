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
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#include <linux/wait.h>
#include <linux/workqueue.h>

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

	/* Queue waiters for idle engine */
	wait_queue_head_t tx_wait0;
	wait_queue_head_t tx_wait1;
	wait_queue_head_t rx_wait0;
	wait_queue_head_t rx_wait1;
#ifdef CONFIG_MACH_ASPEED_G7
	struct regmap		*config;
	struct regmap		*device;
	struct regmap		*e2m;
#else
	struct regmap		*scu;
	int pcie_irq;
#endif
	struct kernfs_node	*kn0;
	struct kernfs_node	*kn1;

	int pcie2lpc;
	int irq;
};

#define BMC_MEM_BAR_SIZE		0x100000
/* =================== AST2600 SCU Define ================================================ */
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
					 struct bin_attribute *attr, char *buf,
					 loff_t off, size_t count)
{
	struct aspeed_bmc_device *bmc_device =
				  dev_get_drvdata(container_of(kobj, struct device, kobj));
	u32 *data = (u32 *)buf;
#ifndef CONFIG_MACH_ASPEED_G7
	u32 scu_id;
#endif
	int ret;

	ret = wait_event_interruptible(bmc_device->rx_wait0,
				       !(readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS) &
				       HOST2BMC_Q1_EMPTY));
	if (ret)
		return -EINTR;

	data[0] = readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_Q1);
#ifdef CONFIG_MACH_ASPEED_G7
	writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB,
	       bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
#else
	regmap_read(bmc_device->scu, ASPEED_SCU04, &scu_id);
	if (scu_id == AST2600A3_SCU04) {
		writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB,
		       bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
	} else {
		//A0 : BIT(12) A1 : BIT(15)
		regmap_update_bits(bmc_device->scu, 0x560, BIT(15), BIT(15));
		regmap_update_bits(bmc_device->scu, 0x560, BIT(15), 0);
	}
#endif
	return sizeof(u32);
}

static ssize_t aspeed_host2bmc_queue2_rx(struct file *filp, struct kobject *kobj,
					 struct bin_attribute *attr, char *buf,
					 loff_t off, size_t count)
{
	struct aspeed_bmc_device *bmc_device =
				  dev_get_drvdata(container_of(kobj, struct device, kobj));
	u32 *data = (u32 *)buf;
#ifndef CONFIG_MACH_ASPEED_G7
	u32 scu_id;
#endif
	int ret;

	ret = wait_event_interruptible(bmc_device->rx_wait1,
				       !(readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS) &
				       HOST2BMC_Q2_EMPTY));
	if (ret)
		return -EINTR;

	data[0] = readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_Q2);
#ifdef CONFIG_MACH_ASPEED_G7
	writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB,
	       bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
#else
	regmap_read(bmc_device->scu, ASPEED_SCU04, &scu_id);
	if (scu_id == AST2600A3_SCU04) {
		writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB,
		       bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
	} else {
		//A0 : BIT(12) A1 : BIT(15)
		regmap_update_bits(bmc_device->scu, 0x560, BIT(15), BIT(15));
		regmap_update_bits(bmc_device->scu, 0x560, BIT(15), 0);
	}
#endif
	return sizeof(u32);
}

static ssize_t aspeed_bmc2host_queue1_tx(struct file *filp, struct kobject *kobj,
					 struct bin_attribute *attr, char *buf,
					 loff_t off, size_t count)
{
	struct aspeed_bmc_device *bmc_device =
				  dev_get_drvdata(container_of(kobj, struct device, kobj));
	u32 tx_buff;
#ifndef CONFIG_MACH_ASPEED_G7
	u32 scu_id;
#endif
	int ret;

	if (count != sizeof(u32))
		return -EINVAL;

	ret = wait_event_interruptible(bmc_device->tx_wait0,
				       !(readl(bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS) &
				       BMC2HOST_Q1_FULL));
	if (ret)
		return -EINTR;

//	if (copy_from_user((void *)&tx_buff, buf, sizeof(u32)))
//		return -EFAULT;
	memcpy(&tx_buff, buf, 4);
	writel(tx_buff, bmc_device->reg_base + ASPEED_BMC_BMC2HOST_Q1);

#ifdef CONFIG_MACH_ASPEED_G7
	writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB,
	       bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
#else
	/* trigger to host
	 * Only After AST2600A3 support DoorBell MSI
	 */
	regmap_read(bmc_device->scu, ASPEED_SCU04, &scu_id);
	if (scu_id == AST2600A3_SCU04) {
		writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB,
		       bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
	} else {
		//A0 : BIT(12) A1 : BIT(15)
		regmap_update_bits(bmc_device->scu, 0x560, BIT(15), BIT(15));
		regmap_update_bits(bmc_device->scu, 0x560, BIT(15), 0);
	}
#endif
	return sizeof(u32);
}

static ssize_t aspeed_bmc2host_queue2_tx(struct file *filp, struct kobject *kobj,
					 struct bin_attribute *attr, char *buf,
					 loff_t off, size_t count)
{
	struct aspeed_bmc_device *bmc_device =
				  dev_get_drvdata(container_of(kobj, struct device, kobj));
	u32 tx_buff = 0;
#ifndef CONFIG_MACH_ASPEED_G7
	u32 scu_id;
#endif
	int ret;

	if (count != sizeof(u32))
		return -EINVAL;

	ret = wait_event_interruptible(bmc_device->tx_wait0,
				       !(readl(bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS) &
				       BMC2HOST_Q2_FULL));
	if (ret)
		return -EINTR;

//	if (copy_from_user((void *)&tx_buff, buf, sizeof(u32)))
//		return -EFAULT;
	memcpy(&tx_buff, buf, 4);
	writel(tx_buff, bmc_device->reg_base + ASPEED_BMC_BMC2HOST_Q2);
#ifdef CONFIG_MACH_ASPEED_G7
	writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB,
	       bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
#else
	/* trigger to host
	 * Only After AST2600A3 support DoorBell MSI
	 */
	regmap_read(bmc_device->scu, ASPEED_SCU04, &scu_id);
	if (scu_id == AST2600A3_SCU04) {
		writel(BMC2HOST_INT_STS_DOORBELL | BMC2HOST_ENABLE_INTB,
		       bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
	} else {
		//A0 : BIT(12) A1 : BIT(15)
		regmap_update_bits(bmc_device->scu, 0x560, BIT(15), BIT(15));
		regmap_update_bits(bmc_device->scu, 0x560, BIT(15), 0);
	}
#endif
	return sizeof(u32);
}

#ifndef CONFIG_MACH_ASPEED_G7
static irqreturn_t aspeed_bmc_dev_pcie_isr(int irq, void *dev_id)
{
	struct aspeed_bmc_device *bmc_device = dev_id;

	while (!(readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS) & HOST2BMC_Q1_EMPTY))
		readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_Q1);

	while (!(readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS) & HOST2BMC_Q2_EMPTY))
		readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_Q2);

	return IRQ_HANDLED;
}
#endif

static irqreturn_t aspeed_bmc_dev_isr(int irq, void *dev_id)
{
	struct aspeed_bmc_device *bmc_device = dev_id;

	u32 host2bmc_q_sts = readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS);

	if (host2bmc_q_sts & HOST2BMC_INT_STS_DOORBELL)
		writel(HOST2BMC_INT_STS_DOORBELL, bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS);

	if (host2bmc_q_sts & HOST2BMC_ENABLE_INTB)
		writel(HOST2BMC_ENABLE_INTB, bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS);

	if (host2bmc_q_sts & HOST2BMC_Q1_FULL)
		dev_info(bmc_device->dev, "Q1 Full\n");

	if (host2bmc_q_sts & HOST2BMC_Q2_FULL)
		dev_info(bmc_device->dev, "Q2 Full\n");

	if (!(readl(bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS) & BMC2HOST_Q1_FULL))
		wake_up_interruptible(&bmc_device->tx_wait0);

	if (!(readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS) & HOST2BMC_Q1_EMPTY))
		wake_up_interruptible(&bmc_device->rx_wait0);

	if (!(readl(bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS) & BMC2HOST_Q2_FULL))
		wake_up_interruptible(&bmc_device->tx_wait1);

	if (!(readl(bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS) & HOST2BMC_Q2_EMPTY))
		wake_up_interruptible(&bmc_device->rx_wait1);

	return IRQ_HANDLED;
}

static void aspeed_bmc_device_init(struct aspeed_bmc_device *bmc_device)
{
	u32 pcie_config_ctl = SCU_PCIE_CONF_BMC_DEV_EN_IRQ |
			      SCU_PCIE_CONF_BMC_DEV_EN_MMIO | SCU_PCIE_CONF_BMC_DEV_EN;
#ifndef CONFIG_MACH_ASPEED_G7
	u32 scu_id;
#endif

#ifdef CONFIG_MACH_ASPEED_G7
	int i;

	if (bmc_device->pcie2lpc)
		pcie_config_ctl |= SCU_PCIE_CONF_BMC_DEV_EN_E2L |
				   SCU_PCIE_CONF_BMC_DEV_EN_LPC_DECODE;

	regmap_update_bits(bmc_device->config, 0x10, pcie_config_ctl, pcie_config_ctl);

	/* update class code to others as it is a MFD device */
	regmap_write(bmc_device->device, 0x18, 0xff000027);

	//MSI
	regmap_update_bits(bmc_device->device, 0x74, GENMASK(7, 4), BIT(7) | (5 << 4));

	//EnPCIaMSI_EnPCIaIntA_EnPCIaMst_EnPCIaDev
	regmap_update_bits(bmc_device->device, 0x70,
			   BIT(25) | BIT(17) | BIT(9) | BIT(1),
			   BIT(25) | BIT(17) | BIT(9) | BIT(1));

	//bar size check for 4k align
	for (i = 1; i < 16; i++) {
		if ((BMC_MEM_BAR_SIZE / 4096) == (1 << (i - 1)))
			break;
	}
	if (i == 16) {
		i = 0;
		dev_warn(bmc_device->dev,
			 "Bar size not align for 4K : %dK\n", BMC_MEM_BAR_SIZE / 1024);
	}

	//BAR assign in scu
	regmap_write(bmc_device->device, 0x1c, ((bmc_device->bmc_mem_phy & ~BIT(34)) >> 8) | i);

	//BAR assign in e2m
	regmap_write(bmc_device->e2m, 0x108, ((bmc_device->bmc_mem_phy & ~BIT(34)) >> 8) | i);
#else
	if (bmc_device->pcie2lpc)
		pcie_config_ctl |= SCU_PCIE_CONF_BMC_DEV_EN_E2L |
				   SCU_PCIE_CONF_BMC_DEV_EN_LPC_DECODE;

	regmap_update_bits(bmc_device->scu, ASPEED_SCU_PCIE_CONF_CTRL,
			   pcie_config_ctl, pcie_config_ctl);

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
		regmap_update_bits(bmc_device->scu, ASPEED_SCUC24,
				   BIT(17) | BIT(14) | BIT(11), BIT(17) | BIT(14) | BIT(11));
#else
	//SCUC24[18]: Enable PCI device 1 INTx/MSI from Host-to-BMC controller.
	regmap_update_bits(bmc_device->scu, 0xc24, BIT(18) | BIT(14), BIT(18) | BIT(14));
#endif

	writel(~(BMC_MEM_BAR_SIZE - 1) | HOST2BMC_MEM_BAR_ENABLE,
	       bmc_device->reg_base + ASPEED_BMC_MEM_BAR);
	writel(bmc_device->bmc_mem_phy, bmc_device->reg_base + ASPEED_BMC_MEM_BAR_REMAP);
#endif

	//Setting BMC to Host Q register
	writel(BMC2HOST_Q2_FULL_UNMASK | BMC2HOST_Q1_FULL_UNMASK | BMC2HOST_ENABLE_INTB,
	       bmc_device->reg_base + ASPEED_BMC_BMC2HOST_STS);
	writel(HOST2BMC_Q2_FULL_UNMASK | HOST2BMC_Q1_FULL_UNMASK | HOST2BMC_ENABLE_INTB,
	       bmc_device->reg_base + ASPEED_BMC_HOST2BMC_STS);
}

static const struct of_device_id aspeed_bmc_device_of_matches[] = {
	{ .compatible = "aspeed,ast2600-bmc-device", },
	{ .compatible = "aspeed,ast2700-bmc-device", },
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

	init_waitqueue_head(&bmc_device->tx_wait0);
	init_waitqueue_head(&bmc_device->tx_wait1);
	init_waitqueue_head(&bmc_device->rx_wait0);
	init_waitqueue_head(&bmc_device->rx_wait1);

	bmc_device->dev = dev;
	bmc_device->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(bmc_device->reg_base))
		goto out_region;
#ifdef CONFIG_MACH_ASPEED_G7
	bmc_device->config = syscon_regmap_lookup_by_phandle(dev->of_node, "aspeed,config");
	if (IS_ERR(bmc_device->config)) {
		dev_err(&pdev->dev, "failed to find config regmap\n");
		goto out_region;
	}

	bmc_device->device = syscon_regmap_lookup_by_phandle(dev->of_node, "aspeed,device");
	if (IS_ERR(bmc_device->config)) {
		dev_err(&pdev->dev, "failed to find device regmap\n");
		goto out_region;
	}

	bmc_device->e2m = syscon_regmap_lookup_by_phandle(dev->of_node, "aspeed,e2m");
	if (IS_ERR(bmc_device->e2m)) {
		dev_err(&pdev->dev, "failed to find e2m regmap\n");
		goto out_region;
	}
#else
	bmc_device->scu = syscon_regmap_lookup_by_phandle(dev->of_node, "aspeed,scu");
	if (IS_ERR(bmc_device->scu)) {
		dev_err(&pdev->dev, "failed to find SCU regmap\n");
		goto out_region;
	}
#endif

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));

	if (of_property_read_bool(dev->of_node, "pcie2lpc"))
		bmc_device->pcie2lpc = 1;

	if (of_reserved_mem_device_init(dev))
		dev_err(dev, "can't get reserved memory\n");

	bmc_device->bmc_mem_virt = dma_alloc_coherent(&pdev->dev, BMC_MEM_BAR_SIZE,
						      &bmc_device->bmc_mem_phy, GFP_KERNEL);
	memset(bmc_device->bmc_mem_virt, 0, BMC_MEM_BAR_SIZE);

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

#ifndef CONFIG_MACH_ASPEED_G7
	bmc_device->pcie_irq =  platform_get_irq(pdev, 1);
	if (bmc_device->pcie_irq < 0) {
		dev_warn(&pdev->dev,
			 "platform get of pcie irq[=%d] failed!\n", bmc_device->pcie_irq);
	} else {
		ret = devm_request_irq(&pdev->dev, bmc_device->pcie_irq,
				       aspeed_bmc_dev_pcie_isr, IRQF_SHARED,
				       dev_name(&pdev->dev), bmc_device);
		if (ret < 0) {
			dev_warn(dev, "Failed to request PCI-E IRQ %d.\n", ret);
			bmc_device->pcie_irq = -1;
		}
	}
#endif

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
	dma_free_coherent(&pdev->dev, BMC_MEM_BAR_SIZE,
			  bmc_device->bmc_mem_virt, bmc_device->bmc_mem_phy);

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

	dma_free_coherent(&pdev->dev, BMC_MEM_BAR_SIZE,
			  bmc_device->bmc_mem_virt, bmc_device->bmc_mem_phy);

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
