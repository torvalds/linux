// SPDX-License-Identifier: GPL-2.0+
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/slab.h>
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


/*******************************************************
  * SysFS Attributes
  ******************************************************/
static ssize_t  show_attr(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pci_dev *pdev = to_pci_dev(dev);
    struct kp2000_device *pcard;

    if (!pdev)  return -ENXIO;
    pcard = pci_get_drvdata(pdev);
    if (!pcard)  return -ENXIO;
    
    if (strcmp("ssid", attr->attr.name) == 0){         return scnprintf(buf, PAGE_SIZE, "%016llx\n", pcard->ssid);  } else
    if (strcmp("ddna", attr->attr.name) == 0){         return scnprintf(buf, PAGE_SIZE, "%016llx\n", pcard->ddna);  } else
    if (strcmp("card_id", attr->attr.name) == 0){      return scnprintf(buf, PAGE_SIZE, "%08x\n", pcard->card_id);  } else
    if (strcmp("hw_rev", attr->attr.name) == 0){       return scnprintf(buf, PAGE_SIZE, "%08x\n", pcard->hardware_revision);  } else
    if (strcmp("build", attr->attr.name) == 0){        return scnprintf(buf, PAGE_SIZE, "%08x\n", pcard->build_version);  } else
    if (strcmp("build_date", attr->attr.name) == 0){   return scnprintf(buf, PAGE_SIZE, "%08x\n", pcard->build_datestamp);  } else
    if (strcmp("build_time", attr->attr.name) == 0){   return scnprintf(buf, PAGE_SIZE, "%08x\n", pcard->build_timestamp);  } else
    { return -ENXIO; }
}

static ssize_t  show_cpld_config_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct kp2000_device *pcard;
	u64 val;

	if (!pdev)
		return -ENXIO;

	pcard = pci_get_drvdata(pdev);
	if (!pcard)
		return -ENXIO;

	val = readq(pcard->sysinfo_regs_base + REG_CPLD_CONFIG);
	return scnprintf(buf, PAGE_SIZE, "%016llx\n", val);
}
static ssize_t cpld_reconfigure(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct pci_dev *pdev = to_pci_dev(dev);
    long wr_val;
    struct kp2000_device *pcard;
    int rv;

    if (!pdev)  return -ENXIO;
    pcard = pci_get_drvdata(pdev);
    if (!pcard)  return -ENXIO;
    
    rv = kstrtol(buf, 0, &wr_val);
    if (rv < 0)  return rv;
    if (wr_val > 7)  return -EINVAL;
    
    wr_val = wr_val << 8;
    wr_val |= 0x1; // Set the "Configure Go" bit
    writeq(wr_val, pcard->sysinfo_regs_base + REG_CPLD_CONFIG);
    return count;
}


DEVICE_ATTR(ssid,       0444, show_attr, NULL);
DEVICE_ATTR(ddna,       0444, show_attr, NULL);
DEVICE_ATTR(card_id,    0444, show_attr, NULL);
DEVICE_ATTR(hw_rev,     0444, show_attr, NULL);
DEVICE_ATTR(build,      0444, show_attr, NULL);
DEVICE_ATTR(build_date, 0444, show_attr, NULL);
DEVICE_ATTR(build_time, 0444, show_attr, NULL);
DEVICE_ATTR(cpld_reg,   0444, show_cpld_config_reg, NULL);
DEVICE_ATTR(cpld_reconfigure,   0220, NULL, cpld_reconfigure);

static const struct attribute *  kp_attr_list[] = {
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
    
    if (read_val & 0x8000000000000000){
        pcard->ssid = read_val;
        return;
    }
    
    timeout = jiffies + (HZ * 2);
    do {
        read_val = readq(pcard->sysinfo_regs_base + REG_FPGA_SSID);
        if (read_val & 0x8000000000000000){
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
    // Timed out waiting for the SSID to show up, stick all zeros in the value
    pcard->ssid = 0;
    #endif
}

static int  read_system_regs(struct kp2000_device *pcard)
{
    u64 read_val;
    
    read_val = readq(pcard->sysinfo_regs_base + REG_MAGIC_NUMBER);
    if (read_val != KP2000_MAGIC_VALUE){
        dev_err(&pcard->pdev->dev, "Invalid magic!  Got: 0x%016llx  Want: 0x%016lx\n", read_val, KP2000_MAGIC_VALUE);
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
    
    dev_info(&pcard->pdev->dev, "system_regs: %08x %08x %08x %08x  %02x  %d %d  %016llx  %016llx\n",
        pcard->card_id,
        pcard->build_version,
        pcard->build_datestamp,
        pcard->build_timestamp,
        pcard->hardware_revision,
        pcard->core_table_rev,
        pcard->core_table_length,
        pcard->ssid,
        pcard->ddna
    );
    
    if (pcard->core_table_rev > 1){
        dev_err(&pcard->pdev->dev, "core table entry revision is higher than we can deal with, cannot continue with this card!\n");
        return 1;
    }
    
    return 0;
}

irqreturn_t  kp2000_irq_handler(int irq, void *dev_id)
{
    struct kp2000_device  *pcard = (struct kp2000_device*)dev_id;
    SetBackEndControl(pcard->dma_common_regs, KPC_DMA_CARD_IRQ_ENABLE | KPC_DMA_CARD_USER_INTERRUPT_MODE | KPC_DMA_CARD_USER_INTERRUPT_ACTIVE);
    return IRQ_HANDLED;
}

int  kp2000_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
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
 
    dev_dbg(&pdev->dev, "kp2000_pcie_probe(pdev = [%p], id = [%p])\n", pdev, id);
    
    //{ Step 1: Allocate a struct for the pcard
    pcard = kzalloc(sizeof(struct kp2000_device), GFP_KERNEL);
    if (NULL == pcard){
        dev_err(&pdev->dev, "probe: failed to allocate private card data\n");
        return -ENOMEM;
    }
    dev_dbg(&pdev->dev, "probe: allocated struct kp2000_device @ %p\n", pcard);
    //}
    
    //{ Step 2: Initialize trivial pcard elements
    pcard->card_num = card_count;
    card_count++;
    scnprintf(pcard->name, 16, "kpcard%d", pcard->card_num);
    
    mutex_init(&pcard->sem);
    lock_card(pcard);
    
    pcard->pdev = pdev;
    pci_set_drvdata(pdev, pcard);
    //}
    
    //{ Step 3: Enable PCI device
    err = pci_enable_device(pcard->pdev);
    if (err){
        dev_err(&pcard->pdev->dev, "probe: failed to enable PCIE2000 PCIe device (%d)\n", err);
        goto out3;
    }
    //}
    
    //{ Step 4: Setup the Register BAR
    reg_bar_phys_addr = pci_resource_start(pcard->pdev, REG_BAR);
    reg_bar_phys_len = pci_resource_len(pcard->pdev, REG_BAR);
    
    pcard->regs_bar_base = ioremap_nocache(reg_bar_phys_addr, PAGE_SIZE);
    if (NULL == pcard->regs_bar_base){
        dev_err(&pcard->pdev->dev, "probe: REG_BAR could not remap memory to virtual space\n");
        err = -ENODEV;
        goto out4;
    }
    dev_dbg(&pcard->pdev->dev, "probe: REG_BAR virt hardware address start [%p]\n", pcard->regs_bar_base);
    
    err = pci_request_region(pcard->pdev, REG_BAR, KP_DRIVER_NAME_KP2000);
    if (err){
        iounmap(pcard->regs_bar_base);
        dev_err(&pcard->pdev->dev, "probe: failed to acquire PCI region (%d)\n", err);
        err = -ENODEV;
        goto out4;
    }
    
    pcard->regs_base_resource.start = reg_bar_phys_addr;
    pcard->regs_base_resource.end   = reg_bar_phys_addr + reg_bar_phys_len - 1;
    pcard->regs_base_resource.flags = IORESOURCE_MEM;
    //}
    
    //{ Step 5: Setup the DMA BAR
    dma_bar_phys_addr = pci_resource_start(pcard->pdev, DMA_BAR);
    dma_bar_phys_len = pci_resource_len(pcard->pdev, DMA_BAR);
    
    pcard->dma_bar_base = ioremap_nocache(dma_bar_phys_addr, dma_bar_phys_len);
    if (NULL == pcard->dma_bar_base){
        dev_err(&pcard->pdev->dev, "probe: DMA_BAR could not remap memory to virtual space\n");
        err = -ENODEV;
        goto out5;
    }
    dev_dbg(&pcard->pdev->dev, "probe: DMA_BAR virt hardware address start [%p]\n", pcard->dma_bar_base);
    
    pcard->dma_common_regs = pcard->dma_bar_base + KPC_DMA_COMMON_OFFSET;
    
    err = pci_request_region(pcard->pdev, DMA_BAR, "kp2000_pcie");
    if (err){
        iounmap(pcard->dma_bar_base);
        dev_err(&pcard->pdev->dev, "probe: failed to acquire PCI region (%d)\n", err);
        err = -ENODEV;
        goto out5;
    }
    
    pcard->dma_base_resource.start = dma_bar_phys_addr;
    pcard->dma_base_resource.end   = dma_bar_phys_addr + dma_bar_phys_len - 1;
    pcard->dma_base_resource.flags = IORESOURCE_MEM;
    //}
    
    //{ Step 6: System Regs
    pcard->sysinfo_regs_base = pcard->regs_bar_base;
    err = read_system_regs(pcard);
    if (err)
        goto out6;
    
    // Disable all "user" interrupts because they're not used yet.
    writeq(0xFFFFFFFFFFFFFFFF, pcard->sysinfo_regs_base + REG_INTERRUPT_MASK);
    //}
    
    //{ Step 7: Configure PCI thingies
    // let the card master PCIe
    pci_set_master(pcard->pdev);
    // enable IO and mem if not already done
    pci_read_config_word(pcard->pdev, PCI_COMMAND, &regval);
    regval |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
    pci_write_config_word(pcard->pdev, PCI_COMMAND, regval);
    
    // Clear relaxed ordering bit
    pcie_capability_clear_and_set_word(pcard->pdev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_RELAX_EN, 0);
    
    // Set Max_Payload_Size and Max_Read_Request_Size
    regval = (0x0) << 5; // Max_Payload_Size = 128 B
    pcie_capability_clear_and_set_word(pcard->pdev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_PAYLOAD, regval);
    regval = (0x0) << 12; // Max_Read_Request_Size = 128 B
    pcie_capability_clear_and_set_word(pcard->pdev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_READRQ, regval);
    
    // Enable error reporting for: Correctable Errors, Non-Fatal Errors, Fatal Errors, Unsupported Requests
    pcie_capability_clear_and_set_word(pcard->pdev, PCI_EXP_DEVCTL, 0, PCI_EXP_DEVCTL_CERE | PCI_EXP_DEVCTL_NFERE | PCI_EXP_DEVCTL_FERE | PCI_EXP_DEVCTL_URRE);
    
    err = dma_set_mask(PCARD_TO_DEV(pcard), DMA_BIT_MASK(64));
    if (err){
        dev_err(&pcard->pdev->dev, "CANNOT use DMA mask %0llx\n", DMA_BIT_MASK(64));
        goto out7;
    }
    dev_dbg(&pcard->pdev->dev, "Using DMA mask %0llx\n", dma_get_mask(PCARD_TO_DEV(pcard)));
    //}
    
    //{ Step 8: Configure IRQs
    err = pci_enable_msi(pcard->pdev);
    if (err < 0)
        goto out8a;
    
    rv = request_irq(pcard->pdev->irq, kp2000_irq_handler, IRQF_SHARED, pcard->name, pcard);
    if (rv){
        dev_err(&pcard->pdev->dev, "kp2000_pcie_probe: failed to request_irq: %d\n", rv);
        goto out8b;
    }
    //}
    
    //{ Step 9: Setup sysfs attributes
    err = sysfs_create_files(&(pdev->dev.kobj), kp_attr_list);
    if (err){
        dev_err(&pdev->dev, "Failed to add sysfs files: %d\n", err);
        goto out9;
    }
    //}
    
    //{ Step 10: Setup misc device
    pcard->miscdev.minor = MISC_DYNAMIC_MINOR;
    pcard->miscdev.fops = &kp2000_fops;
    pcard->miscdev.parent = &pcard->pdev->dev;
    pcard->miscdev.name = pcard->name;
    
    err = misc_register(&pcard->miscdev);
    if (err){
        dev_err(&pcard->pdev->dev, "kp2000_pcie_probe: misc_register failed: %d\n", err);
        goto out10;
    }
    //}
    
    //{ Step 11: Probe cores
    err = kp2000_probe_cores(pcard);
    if (err)
        goto out11;
    //}
    
    //{ Step 12: Enable IRQs in HW
    SetBackEndControl(pcard->dma_common_regs, KPC_DMA_CARD_IRQ_ENABLE | KPC_DMA_CARD_USER_INTERRUPT_MODE);
    //}
    
    dev_dbg(&pcard->pdev->dev, "kp2000_pcie_probe() complete!\n");
    unlock_card(pcard);
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
    unlock_card(pcard);
    kfree(pcard);
    return err;
}


void  kp2000_pcie_remove(struct pci_dev *pdev)
{
    struct kp2000_device *pcard = pci_get_drvdata(pdev);

    dev_dbg(&pdev->dev, "kp2000_pcie_remove(pdev=%p)\n", pdev);
    
    if (pcard == NULL)  return;
    
    lock_card(pcard);
    kp2000_remove_cores(pcard);
    mfd_remove_devices(PCARD_TO_DEV(pcard));
    misc_deregister(&pcard->miscdev);
    sysfs_remove_files(&(pdev->dev.kobj), kp_attr_list);
    free_irq(pcard->pdev->irq, pcard);
    pci_disable_msi(pcard->pdev);
    if (pcard->dma_bar_base != NULL){
        iounmap(pcard->dma_bar_base);
        pci_release_region(pdev, DMA_BAR);
        pcard->dma_bar_base = NULL;
    }
    if (pcard->regs_bar_base != NULL){
        iounmap(pcard->regs_bar_base);
        pci_release_region(pdev, REG_BAR);
        pcard->regs_bar_base = NULL;
    }
    pci_disable_device(pcard->pdev);
    pci_set_drvdata(pdev, NULL);
    unlock_card(pcard);
    kfree(pcard);
}
