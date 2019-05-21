// SPDX-License-Identifier: GPL-2.0+
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/rwsem.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include "pcie.h"
#include "uapi.h"

/*******************************************************
 * SysFS Attributes
 ******************************************************/

static struct kp2000_device *get_pcard(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	if (!pdev)
		return NULL;

	return pci_get_drvdata(pdev);
}

static ssize_t ssid_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct kp2000_device *pcard = get_pcard(dev);

	if (!pcard)
		return -ENXIO;

	return sprintf(buf, "%016llx\n", pcard->ssid);
}
static DEVICE_ATTR_RO(ssid);

static ssize_t ddna_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct kp2000_device *pcard = get_pcard(dev);

	if (!pcard)
		return -ENXIO;

	return sprintf(buf, "%016llx\n", pcard->ddna);
}
static DEVICE_ATTR_RO(ddna);

static ssize_t card_id_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct kp2000_device *pcard = get_pcard(dev);

	if (!pcard)
		return -ENXIO;

	return sprintf(buf, "%08x\n", pcard->card_id);
}
static DEVICE_ATTR_RO(card_id);

static ssize_t hw_rev_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct kp2000_device *pcard = get_pcard(dev);

	if (!pcard)
		return -ENXIO;

	return sprintf(buf, "%08x\n", pcard->hardware_revision);
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t build_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct kp2000_device *pcard = get_pcard(dev);

	if (!pcard)
		return -ENXIO;

	return sprintf(buf, "%08x\n", pcard->build_version);
}
static DEVICE_ATTR_RO(build);

static ssize_t build_date_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct kp2000_device *pcard = get_pcard(dev);

	if (!pcard)
		return -ENXIO;

	return sprintf(buf, "%08x\n", pcard->build_datestamp);
}
static DEVICE_ATTR_RO(build_date);

static ssize_t build_time_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct kp2000_device *pcard = get_pcard(dev);

	if (!pcard)
		return -ENXIO;

	return sprintf(buf, "%08x\n", pcard->build_timestamp);
}
static DEVICE_ATTR_RO(build_time);

static ssize_t cpld_reg_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct kp2000_device *pcard = get_pcard(dev);
	u64 val;

	if (!pcard)
		return -ENXIO;

	val = readq(pcard->sysinfo_regs_base + REG_CPLD_CONFIG);
	return sprintf(buf, "%016llx\n", val);
}
static DEVICE_ATTR_RO(cpld_reg);

static ssize_t cpld_reconfigure(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct kp2000_device *pcard = get_pcard(dev);
	long wr_val;
	int rv;

	if (!pcard)
		return -ENXIO;

	rv = kstrtol(buf, 0, &wr_val);
	if (rv < 0)
		return rv;
	if (wr_val > 7)
		return -EINVAL;

	wr_val = wr_val << 8;
	wr_val |= 0x1; // Set the "Configure Go" bit
	writeq(wr_val, pcard->sysinfo_regs_base + REG_CPLD_CONFIG);
	return count;
}
static DEVICE_ATTR(cpld_reconfigure, 0220, NULL, cpld_reconfigure);

static const struct attribute *kp_attr_list[] = {
	&dev_attr_ssid.attr,
	&dev_attr_ddna.attr,
	&dev_attr_card_id.attr,
	&dev_attr_hw_rev.attr,
	&dev_attr_build.attr,
	&dev_attr_build_date.attr,
	&dev_attr_build_time.attr,
	&dev_attr_cpld_reg.attr,
	&dev_attr_cpld_reconfigure.attr,
	NULL,
};

/*******************************************************
 * Functions
 ******************************************************/

static void wait_and_read_ssid(struct kp2000_device *pcard)
{
	u64 read_val = readq(pcard->sysinfo_regs_base + REG_FPGA_SSID);
	unsigned long timeout;

	if (read_val & 0x8000000000000000) {
		pcard->ssid = read_val;
		return;
	}

	timeout = jiffies + (HZ * 2);
	do {
		read_val = readq(pcard->sysinfo_regs_base + REG_FPGA_SSID);
		if (read_val & 0x8000000000000000) {
			pcard->ssid = read_val;
			return;
		}
		cpu_relax();
		//schedule();
	} while (time_before(jiffies, timeout));

	dev_notice(&pcard->pdev->dev, "SSID didn't show up!\n");

#if 0
	// Timed out waiting for the SSID to show up, just use the DDNA instead?
	read_val = readq(pcard->sysinfo_regs_base + REG_FPGA_DDNA);
	pcard->ssid = read_val;
#else
	// Timed out waiting for the SSID to show up, stick all zeros in the
	// value
	pcard->ssid = 0;
#endif
}

static int  read_system_regs(struct kp2000_device *pcard)
{
	u64 read_val;

	read_val = readq(pcard->sysinfo_regs_base + REG_MAGIC_NUMBER);
	if (read_val != KP2000_MAGIC_VALUE) {
		dev_err(&pcard->pdev->dev,
			"Invalid magic!  Got: 0x%016llx  Want: 0x%016lx\n",
			read_val, KP2000_MAGIC_VALUE);
		return -EILSEQ;
	}

	read_val = readq(pcard->sysinfo_regs_base + REG_CARD_ID_AND_BUILD);
	pcard->card_id = (read_val & 0xFFFFFFFF00000000) >> 32;
	pcard->build_version = (read_val & 0x00000000FFFFFFFF) >> 0;

	read_val = readq(pcard->sysinfo_regs_base + REG_DATE_AND_TIME_STAMPS);
	pcard->build_datestamp = (read_val & 0xFFFFFFFF00000000) >> 32;
	pcard->build_timestamp = (read_val & 0x00000000FFFFFFFF) >> 0;

	read_val = readq(pcard->sysinfo_regs_base + REG_CORE_TABLE_OFFSET);
	pcard->core_table_length = (read_val & 0xFFFFFFFF00000000) >> 32;
	pcard->core_table_offset = (read_val & 0x00000000FFFFFFFF) >> 0;

	wait_and_read_ssid(pcard);

	read_val = readq(pcard->sysinfo_regs_base + REG_FPGA_HW_ID);
	pcard->core_table_rev    = (read_val & 0x0000000000000F00) >> 8;
	pcard->hardware_revision = (read_val & 0x000000000000001F);

	read_val = readq(pcard->sysinfo_regs_base + REG_FPGA_DDNA);
	pcard->ddna = read_val;

	dev_info(&pcard->pdev->dev,
		 "system_regs: %08x %08x %08x %08x  %02x  %d %d  %016llx  %016llx\n",
		 pcard->card_id,
		 pcard->build_version,
		 pcard->build_datestamp,
		 pcard->build_timestamp,
		 pcard->hardware_revision,
		 pcard->core_table_rev,
		 pcard->core_table_length,
		 pcard->ssid,
		 pcard->ddna);

	if (pcard->core_table_rev > 1) {
		dev_err(&pcard->pdev->dev,
			"core table entry revision is higher than we can deal with, cannot continue with this card!\n");
		return 1;
	}

	return 0;
}

static irqreturn_t kp2000_irq_handler(int irq, void *dev_id)
{
	struct kp2000_device *pcard = dev_id;

	writel(KPC_DMA_CARD_IRQ_ENABLE |
	       KPC_DMA_CARD_USER_INTERRUPT_MODE |
	       KPC_DMA_CARD_USER_INTERRUPT_ACTIVE,
	       pcard->dma_common_regs);
	return IRQ_HANDLED;
}

static int kp2000_cdev_open(struct inode *inode, struct file *filp)
{
	struct kp2000_device *pcard = container_of(filp->private_data, struct kp2000_device, miscdev);

	dev_dbg(&pcard->pdev->dev, "kp2000_cdev_open(filp = [%p], pcard = [%p])\n", filp, pcard);

	filp->private_data = pcard; /* so other methods can access it */

	return 0;
}

static int kp2000_cdev_close(struct inode *inode, struct file *filp)
{
	struct kp2000_device *pcard = filp->private_data;

	dev_dbg(&pcard->pdev->dev, "kp2000_cdev_close(filp = [%p], pcard = [%p])\n", filp, pcard);
	return 0;
}


static ssize_t kp2000_cdev_read(struct file *filp, char __user *buf,
				size_t count, loff_t *f_pos)
{
	struct kp2000_device *pcard = filp->private_data;
	int cnt = 0;
	int ret;
#define BUFF_CNT  1024
	char buff[BUFF_CNT] = {0}; //NOTE: Increase this so it is at least as large as all the scnprintfs.  And don't use unbounded strings. "%s"
	//NOTE: also, this is a really shitty way to implement the read() call, but it will work for any size 'count'.

	if (WARN(NULL == buf, "kp2000_cdev_read: buf is a NULL pointer!\n"))
		return -EINVAL;

	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Card ID                 : 0x%08x\n", pcard->card_id);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Build Version           : 0x%08x\n", pcard->build_version);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Build Date              : 0x%08x\n", pcard->build_datestamp);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Build Time              : 0x%08x\n", pcard->build_timestamp);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Core Table Offset       : 0x%08x\n", pcard->core_table_offset);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Core Table Length       : 0x%08x\n", pcard->core_table_length);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Hardware Revision       : 0x%08x\n", pcard->hardware_revision);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "SSID                    : 0x%016llx\n", pcard->ssid);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "DDNA                    : 0x%016llx\n", pcard->ddna);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "IRQ Mask                : 0x%016llx\n", readq(pcard->sysinfo_regs_base + REG_INTERRUPT_MASK));
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "IRQ Active              : 0x%016llx\n", readq(pcard->sysinfo_regs_base + REG_INTERRUPT_ACTIVE));
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "CPLD                    : 0x%016llx\n", readq(pcard->sysinfo_regs_base + REG_CPLD_CONFIG));

	if (*f_pos >= cnt)
		return 0;

	if (count > cnt)
		count = cnt;

	ret = copy_to_user(buf, buff + *f_pos, count);
	if (ret)
		return -EFAULT;
	*f_pos += count;
	return count;
}

static long kp2000_cdev_ioctl(struct file *filp, unsigned int ioctl_num,
			      unsigned long ioctl_param)
{
	struct kp2000_device *pcard = filp->private_data;

	dev_dbg(&pcard->pdev->dev, "kp2000_cdev_ioctl(filp = [%p], ioctl_num = 0x%08x, ioctl_param = 0x%016lx) pcard = [%p]\n", filp, ioctl_num, ioctl_param, pcard);

	switch (ioctl_num){
	case KP2000_IOCTL_GET_CPLD_REG:             return readq(pcard->sysinfo_regs_base + REG_CPLD_CONFIG);
	case KP2000_IOCTL_GET_PCIE_ERROR_REG:       return readq(pcard->sysinfo_regs_base + REG_PCIE_ERROR_COUNT);

	case KP2000_IOCTL_GET_EVERYTHING: {
		struct kp2000_regs temp;
		int ret;

		memset(&temp, 0, sizeof(temp));
		temp.card_id = pcard->card_id;
		temp.build_version = pcard->build_version;
		temp.build_datestamp = pcard->build_datestamp;
		temp.build_timestamp = pcard->build_timestamp;
		temp.hw_rev = pcard->hardware_revision;
		temp.ssid = pcard->ssid;
		temp.ddna = pcard->ddna;
		temp.cpld_reg = readq(pcard->sysinfo_regs_base + REG_CPLD_CONFIG);

		ret = copy_to_user((void*)ioctl_param, (void*)&temp, sizeof(temp));
		if (ret)
			return -EFAULT;

		return sizeof(temp);
		}

	default:
		return -ENOTTY;
	}
	return -ENOTTY;
}

static struct file_operations kp2000_fops = {
	.owner =		THIS_MODULE,
	.open =			kp2000_cdev_open,
	.release =		kp2000_cdev_close,
	.read =			kp2000_cdev_read,
	.llseek =		noop_llseek,
	.unlocked_ioctl =	kp2000_cdev_ioctl,
};

static int kp2000_pcie_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	int err = 0;
	struct kp2000_device *pcard;
	static int card_count = 1;
	int rv;
	unsigned long reg_bar_phys_addr;
	unsigned long reg_bar_phys_len;
	unsigned long dma_bar_phys_addr;
	unsigned long dma_bar_phys_len;
	u16 regval;

	dev_dbg(&pdev->dev, "kp2000_pcie_probe(pdev = [%p], id = [%p])\n",
		pdev, id);

	/*
	 * Step 1: Allocate a struct for the pcard
	 */
	pcard = kzalloc(sizeof(struct kp2000_device), GFP_KERNEL);
	if (NULL == pcard) {
		dev_err(&pdev->dev,
			"probe: failed to allocate private card data\n");
		return -ENOMEM;
	}
	dev_dbg(&pdev->dev, "probe: allocated struct kp2000_device @ %p\n",
		pcard);

	/*
	 * Step 2: Initialize trivial pcard elements
	 */
	pcard->card_num = card_count;
	card_count++;
	scnprintf(pcard->name, 16, "kpcard%d", pcard->card_num);

	mutex_init(&pcard->sem);
	mutex_lock(&pcard->sem);

	pcard->pdev = pdev;
	pci_set_drvdata(pdev, pcard);

	/*
	 * Step 3: Enable PCI device
	 */
	err = pci_enable_device(pcard->pdev);
	if (err) {
		dev_err(&pcard->pdev->dev,
			"probe: failed to enable PCIE2000 PCIe device (%d)\n",
			err);
		goto out3;
	}

	/*
	 * Step 4: Setup the Register BAR
	 */
	reg_bar_phys_addr = pci_resource_start(pcard->pdev, REG_BAR);
	reg_bar_phys_len = pci_resource_len(pcard->pdev, REG_BAR);

	pcard->regs_bar_base = ioremap_nocache(reg_bar_phys_addr, PAGE_SIZE);
	if (NULL == pcard->regs_bar_base) {
		dev_err(&pcard->pdev->dev,
			"probe: REG_BAR could not remap memory to virtual space\n");
		err = -ENODEV;
		goto out4;
	}
	dev_dbg(&pcard->pdev->dev,
		"probe: REG_BAR virt hardware address start [%p]\n",
		pcard->regs_bar_base);

	err = pci_request_region(pcard->pdev, REG_BAR, KP_DRIVER_NAME_KP2000);
	if (err) {
		iounmap(pcard->regs_bar_base);
		dev_err(&pcard->pdev->dev,
			"probe: failed to acquire PCI region (%d)\n",
			err);
		err = -ENODEV;
		goto out4;
	}

	pcard->regs_base_resource.start = reg_bar_phys_addr;
	pcard->regs_base_resource.end   = reg_bar_phys_addr +
					  reg_bar_phys_len - 1;
	pcard->regs_base_resource.flags = IORESOURCE_MEM;

	/*
	 * Step 5: Setup the DMA BAR
	 */
	dma_bar_phys_addr = pci_resource_start(pcard->pdev, DMA_BAR);
	dma_bar_phys_len = pci_resource_len(pcard->pdev, DMA_BAR);

	pcard->dma_bar_base = ioremap_nocache(dma_bar_phys_addr,
					      dma_bar_phys_len);
	if (NULL == pcard->dma_bar_base) {
		dev_err(&pcard->pdev->dev,
			"probe: DMA_BAR could not remap memory to virtual space\n");
		err = -ENODEV;
		goto out5;
	}
	dev_dbg(&pcard->pdev->dev,
		"probe: DMA_BAR virt hardware address start [%p]\n",
		pcard->dma_bar_base);

	pcard->dma_common_regs = pcard->dma_bar_base + KPC_DMA_COMMON_OFFSET;

	err = pci_request_region(pcard->pdev, DMA_BAR, "kp2000_pcie");
	if (err) {
		iounmap(pcard->dma_bar_base);
		dev_err(&pcard->pdev->dev,
			"probe: failed to acquire PCI region (%d)\n", err);
		err = -ENODEV;
		goto out5;
	}

	pcard->dma_base_resource.start = dma_bar_phys_addr;
	pcard->dma_base_resource.end   = dma_bar_phys_addr +
					 dma_bar_phys_len - 1;
	pcard->dma_base_resource.flags = IORESOURCE_MEM;

	/*
	 * Step 6: System Regs
	 */
	pcard->sysinfo_regs_base = pcard->regs_bar_base;
	err = read_system_regs(pcard);
	if (err)
		goto out6;

	// Disable all "user" interrupts because they're not used yet.
	writeq(0xFFFFFFFFFFFFFFFF,
	       pcard->sysinfo_regs_base + REG_INTERRUPT_MASK);

	/*
	 * Step 7: Configure PCI thingies
	 */
	// let the card master PCIe
	pci_set_master(pcard->pdev);
	// enable IO and mem if not already done
	pci_read_config_word(pcard->pdev, PCI_COMMAND, &regval);
	regval |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	pci_write_config_word(pcard->pdev, PCI_COMMAND, regval);

	// Clear relaxed ordering bit
	pcie_capability_clear_and_set_word(pcard->pdev, PCI_EXP_DEVCTL,
					   PCI_EXP_DEVCTL_RELAX_EN, 0);

	// Set Max_Payload_Size and Max_Read_Request_Size
	regval = (0x0) << 5; // Max_Payload_Size = 128 B
	pcie_capability_clear_and_set_word(pcard->pdev, PCI_EXP_DEVCTL,
					   PCI_EXP_DEVCTL_PAYLOAD, regval);
	regval = (0x0) << 12; // Max_Read_Request_Size = 128 B
	pcie_capability_clear_and_set_word(pcard->pdev, PCI_EXP_DEVCTL,
					   PCI_EXP_DEVCTL_READRQ, regval);

	// Enable error reporting for: Correctable Errors, Non-Fatal Errors,
	// Fatal Errors, Unsupported Requests
	pcie_capability_clear_and_set_word(pcard->pdev, PCI_EXP_DEVCTL, 0,
					   PCI_EXP_DEVCTL_CERE |
					   PCI_EXP_DEVCTL_NFERE |
					   PCI_EXP_DEVCTL_FERE |
					   PCI_EXP_DEVCTL_URRE);

	err = dma_set_mask(PCARD_TO_DEV(pcard), DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pcard->pdev->dev,
			"CANNOT use DMA mask %0llx\n", DMA_BIT_MASK(64));
		goto out7;
	}
	dev_dbg(&pcard->pdev->dev,
		"Using DMA mask %0llx\n", dma_get_mask(PCARD_TO_DEV(pcard)));

	/*
	 * Step 8: Configure IRQs
	 */
	err = pci_enable_msi(pcard->pdev);
	if (err < 0)
		goto out8a;

	rv = request_irq(pcard->pdev->irq, kp2000_irq_handler, IRQF_SHARED,
			 pcard->name, pcard);
	if (rv) {
		dev_err(&pcard->pdev->dev,
			"kp2000_pcie_probe: failed to request_irq: %d\n", rv);
		goto out8b;
	}

	/*
	 * Step 9: Setup sysfs attributes
	 */
	err = sysfs_create_files(&(pdev->dev.kobj), kp_attr_list);
	if (err) {
		dev_err(&pdev->dev, "Failed to add sysfs files: %d\n", err);
		goto out9;
	}

	/*
	 * Step 10: Setup misc device
	 */
	pcard->miscdev.minor = MISC_DYNAMIC_MINOR;
	pcard->miscdev.fops = &kp2000_fops;
	pcard->miscdev.parent = &pcard->pdev->dev;
	pcard->miscdev.name = pcard->name;

	err = misc_register(&pcard->miscdev);
	if (err) {
		dev_err(&pcard->pdev->dev,
			"kp2000_pcie_probe: misc_register failed: %d\n", err);
		goto out10;
	}

	/*
	 * Step 11: Probe cores
	 */
	err = kp2000_probe_cores(pcard);
	if (err)
		goto out11;

	/*
	 * Step 12: Enable IRQs in HW
	 */
	writel(KPC_DMA_CARD_IRQ_ENABLE | KPC_DMA_CARD_USER_INTERRUPT_MODE,
	       pcard->dma_common_regs);

	dev_dbg(&pcard->pdev->dev, "kp2000_pcie_probe() complete!\n");
	mutex_unlock(&pcard->sem);
	return 0;

out11:
	misc_deregister(&pcard->miscdev);
out10:
	sysfs_remove_files(&(pdev->dev.kobj), kp_attr_list);
out9:
	free_irq(pcard->pdev->irq, pcard);
out8b:
	pci_disable_msi(pcard->pdev);
out8a:
out7:
out6:
	iounmap(pcard->dma_bar_base);
	pci_release_region(pdev, DMA_BAR);
	pcard->dma_bar_base = NULL;
out5:
	iounmap(pcard->regs_bar_base);
	pci_release_region(pdev, REG_BAR);
	pcard->regs_bar_base = NULL;
out4:
	pci_disable_device(pcard->pdev);
out3:
	mutex_unlock(&pcard->sem);
	kfree(pcard);
	return err;
}

static void kp2000_pcie_remove(struct pci_dev *pdev)
{
	struct kp2000_device *pcard = pci_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "kp2000_pcie_remove(pdev=%p)\n", pdev);

	if (pcard == NULL)
		return;

	mutex_lock(&pcard->sem);
	kp2000_remove_cores(pcard);
	mfd_remove_devices(PCARD_TO_DEV(pcard));
	misc_deregister(&pcard->miscdev);
	sysfs_remove_files(&(pdev->dev.kobj), kp_attr_list);
	free_irq(pcard->pdev->irq, pcard);
	pci_disable_msi(pcard->pdev);
	if (pcard->dma_bar_base != NULL) {
		iounmap(pcard->dma_bar_base);
		pci_release_region(pdev, DMA_BAR);
		pcard->dma_bar_base = NULL;
	}
	if (pcard->regs_bar_base != NULL) {
		iounmap(pcard->regs_bar_base);
		pci_release_region(pdev, REG_BAR);
		pcard->regs_bar_base = NULL;
	}
	pci_disable_device(pcard->pdev);
	pci_set_drvdata(pdev, NULL);
	mutex_unlock(&pcard->sem);
	kfree(pcard);
}

struct class *kpc_uio_class;
ATTRIBUTE_GROUPS(kpc_uio_class);

static const struct pci_device_id kp2000_pci_device_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_DAKTRONICS, PCI_DEVICE_ID_DAKTRONICS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DAKTRONICS, PCI_DEVICE_ID_DAKTRONICS_KADOKA_P2KR0) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, kp2000_pci_device_ids);

static struct pci_driver kp2000_driver_inst = {
	.name =		"kp2000_pcie",
	.id_table =	kp2000_pci_device_ids,
	.probe =	kp2000_pcie_probe,
	.remove =	kp2000_pcie_remove,
};

static int __init  kp2000_pcie_init(void)
{
	kpc_uio_class = class_create(THIS_MODULE, "kpc_uio");
	if (IS_ERR(kpc_uio_class))
		return PTR_ERR(kpc_uio_class);

	kpc_uio_class->dev_groups = kpc_uio_class_groups;
	return pci_register_driver(&kp2000_driver_inst);
}
module_init(kp2000_pcie_init);

static void __exit  kp2000_pcie_exit(void)
{
	pci_unregister_driver(&kp2000_driver_inst);
	class_destroy(kpc_uio_class);
}
module_exit(kp2000_pcie_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lee.Brooke@Daktronics.com, Matt.Sickler@Daktronics.com");
MODULE_SOFTDEP("pre: uio post: kpc_nwl_dma kpc_i2c kpc_spi");
