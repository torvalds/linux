/*
 * File:	msi.c
 * Purpose:	PCI Message Signaled Interrupt (MSI)
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 * Copyright (C) 2016 Christoph Hellwig.
 */

#include <linux/err.h>
#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/msi.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>

#include "pci.h"

static int pci_msi_enable = 1;
int pci_msi_ignore_mask;

#define msix_table_size(flags)	((flags & PCI_MSIX_FLAGS_QSIZE) + 1)

#ifdef CONFIG_PCI_MSI_IRQ_DOMAIN
static struct irq_domain *pci_msi_default_domain;
static DEFINE_MUTEX(pci_msi_domain_lock);

struct irq_domain * __weak arch_get_pci_msi_domain(struct pci_dev *dev)
{
	return pci_msi_default_domain;
}

static struct irq_domain *pci_msi_get_domain(struct pci_dev *dev)
{
	struct irq_domain *domain;

	domain = dev_get_msi_domain(&dev->dev);
	if (domain)
		return domain;

	return arch_get_pci_msi_domain(dev);
}

static int pci_msi_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct irq_domain *domain;

	domain = pci_msi_get_domain(dev);
	if (domain && irq_domain_is_hierarchy(domain))
		return pci_msi_domain_alloc_irqs(domain, dev, nvec, type);

	return arch_setup_msi_irqs(dev, nvec, type);
}

static void pci_msi_teardown_msi_irqs(struct pci_dev *dev)
{
	struct irq_domain *domain;

	domain = pci_msi_get_domain(dev);
	if (domain && irq_domain_is_hierarchy(domain))
		pci_msi_domain_free_irqs(domain, dev);
	else
		arch_teardown_msi_irqs(dev);
}
#else
#define pci_msi_setup_msi_irqs		arch_setup_msi_irqs
#define pci_msi_teardown_msi_irqs	arch_teardown_msi_irqs
#endif

/* Arch hooks */

int __weak arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc)
{
	struct msi_controller *chip = dev->bus->msi;
	int err;

	if (!chip || !chip->setup_irq)
		return -EINVAL;

	err = chip->setup_irq(chip, dev, desc);
	if (err < 0)
		return err;

	irq_set_chip_data(desc->irq, chip);

	return 0;
}

void __weak arch_teardown_msi_irq(unsigned int irq)
{
	struct msi_controller *chip = irq_get_chip_data(irq);

	if (!chip || !chip->teardown_irq)
		return;

	chip->teardown_irq(chip, irq);
}

int __weak arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_controller *chip = dev->bus->msi;
	struct msi_desc *entry;
	int ret;

	if (chip && chip->setup_irqs)
		return chip->setup_irqs(chip, dev, nvec, type);
	/*
	 * If an architecture wants to support multiple MSI, it needs to
	 * override arch_setup_msi_irqs()
	 */
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	for_each_pci_msi_entry(entry, dev) {
		ret = arch_setup_msi_irq(dev, entry);
		if (ret < 0)
			return ret;
		if (ret > 0)
			return -ENOSPC;
	}

	return 0;
}

/*
 * We have a default implementation available as a separate non-weak
 * function, as it is used by the Xen x86 PCI code
 */
void default_teardown_msi_irqs(struct pci_dev *dev)
{
	int i;
	struct msi_desc *entry;

	for_each_pci_msi_entry(entry, dev)
		if (entry->irq)
			for (i = 0; i < entry->nvec_used; i++)
				arch_teardown_msi_irq(entry->irq + i);
}

void __weak arch_teardown_msi_irqs(struct pci_dev *dev)
{
	return default_teardown_msi_irqs(dev);
}

static void default_restore_msi_irq(struct pci_dev *dev, int irq)
{
	struct msi_desc *entry;

	entry = NULL;
	if (dev->msix_enabled) {
		for_each_pci_msi_entry(entry, dev) {
			if (irq == entry->irq)
				break;
		}
	} else if (dev->msi_enabled)  {
		entry = irq_get_msi_desc(irq);
	}

	if (entry)
		__pci_write_msi_msg(entry, &entry->msg);
}

void __weak arch_restore_msi_irqs(struct pci_dev *dev)
{
	return default_restore_msi_irqs(dev);
}

static inline __attribute_const__ u32 msi_mask(unsigned x)
{
	/* Don't shift by >= width of type */
	if (x >= 5)
		return 0xffffffff;
	return (1 << (1 << x)) - 1;
}

/*
 * PCI 2.3 does not specify mask bits for each MSI interrupt.  Attempting to
 * mask all MSI interrupts by clearing the MSI enable bit does not work
 * reliably as devices without an INTx disable bit will then generate a
 * level IRQ which will never be cleared.
 */
u32 __pci_msi_desc_mask_irq(struct msi_desc *desc, u32 mask, u32 flag)
{
	u32 mask_bits = desc->masked;

	if (pci_msi_ignore_mask || !desc->msi_attrib.maskbit)
		return 0;

	mask_bits &= ~mask;
	mask_bits |= flag;
	pci_write_config_dword(msi_desc_to_pci_dev(desc), desc->mask_pos,
			       mask_bits);

	return mask_bits;
}

static void msi_mask_irq(struct msi_desc *desc, u32 mask, u32 flag)
{
	desc->masked = __pci_msi_desc_mask_irq(desc, mask, flag);
}

static void __iomem *pci_msix_desc_addr(struct msi_desc *desc)
{
	return desc->mask_base +
		desc->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE;
}

/*
 * This internal function does not flush PCI writes to the device.
 * All users must ensure that they read from the device before either
 * assuming that the device state is up to date, or returning out of this
 * file.  This saves a few milliseconds when initialising devices with lots
 * of MSI-X interrupts.
 */
u32 __pci_msix_desc_mask_irq(struct msi_desc *desc, u32 flag)
{
	u32 mask_bits = desc->masked;

	if (pci_msi_ignore_mask)
		return 0;

	mask_bits &= ~PCI_MSIX_ENTRY_CTRL_MASKBIT;
	if (flag)
		mask_bits |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
	writel(mask_bits, pci_msix_desc_addr(desc) + PCI_MSIX_ENTRY_VECTOR_CTRL);

	return mask_bits;
}

static void msix_mask_irq(struct msi_desc *desc, u32 flag)
{
	desc->masked = __pci_msix_desc_mask_irq(desc, flag);
}

static void msi_set_mask_bit(struct irq_data *data, u32 flag)
{
	struct msi_desc *desc = irq_data_get_msi_desc(data);

	if (desc->msi_attrib.is_msix) {
		msix_mask_irq(desc, flag);
		readl(desc->mask_base);		/* Flush write to device */
	} else {
		unsigned offset = data->irq - desc->irq;
		msi_mask_irq(desc, 1 << offset, flag << offset);
	}
}

/**
 * pci_msi_mask_irq - Generic irq chip callback to mask PCI/MSI interrupts
 * @data:	pointer to irqdata associated to that interrupt
 */
void pci_msi_mask_irq(struct irq_data *data)
{
	msi_set_mask_bit(data, 1);
}
EXPORT_SYMBOL_GPL(pci_msi_mask_irq);

/**
 * pci_msi_unmask_irq - Generic irq chip callback to unmask PCI/MSI interrupts
 * @data:	pointer to irqdata associated to that interrupt
 */
void pci_msi_unmask_irq(struct irq_data *data)
{
	msi_set_mask_bit(data, 0);
}
EXPORT_SYMBOL_GPL(pci_msi_unmask_irq);

void default_restore_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *entry;

	for_each_pci_msi_entry(entry, dev)
		default_restore_msi_irq(dev, entry->irq);
}

void __pci_read_msi_msg(struct msi_desc *entry, struct msi_msg *msg)
{
	struct pci_dev *dev = msi_desc_to_pci_dev(entry);

	BUG_ON(dev->current_state != PCI_D0);

	if (entry->msi_attrib.is_msix) {
		void __iomem *base = pci_msix_desc_addr(entry);

		msg->address_lo = readl(base + PCI_MSIX_ENTRY_LOWER_ADDR);
		msg->address_hi = readl(base + PCI_MSIX_ENTRY_UPPER_ADDR);
		msg->data = readl(base + PCI_MSIX_ENTRY_DATA);
	} else {
		int pos = dev->msi_cap;
		u16 data;

		pci_read_config_dword(dev, pos + PCI_MSI_ADDRESS_LO,
				      &msg->address_lo);
		if (entry->msi_attrib.is_64) {
			pci_read_config_dword(dev, pos + PCI_MSI_ADDRESS_HI,
					      &msg->address_hi);
			pci_read_config_word(dev, pos + PCI_MSI_DATA_64, &data);
		} else {
			msg->address_hi = 0;
			pci_read_config_word(dev, pos + PCI_MSI_DATA_32, &data);
		}
		msg->data = data;
	}
}

void __pci_write_msi_msg(struct msi_desc *entry, struct msi_msg *msg)
{
	struct pci_dev *dev = msi_desc_to_pci_dev(entry);

	if (dev->current_state != PCI_D0) {
		/* Don't touch the hardware now */
	} else if (entry->msi_attrib.is_msix) {
		void __iomem *base = pci_msix_desc_addr(entry);

		writel(msg->address_lo, base + PCI_MSIX_ENTRY_LOWER_ADDR);
		writel(msg->address_hi, base + PCI_MSIX_ENTRY_UPPER_ADDR);
		writel(msg->data, base + PCI_MSIX_ENTRY_DATA);
	} else {
		int pos = dev->msi_cap;
		u16 msgctl;

		pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &msgctl);
		msgctl &= ~PCI_MSI_FLAGS_QSIZE;
		msgctl |= entry->msi_attrib.multiple << 4;
		pci_write_config_word(dev, pos + PCI_MSI_FLAGS, msgctl);

		pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_LO,
				       msg->address_lo);
		if (entry->msi_attrib.is_64) {
			pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_HI,
					       msg->address_hi);
			pci_write_config_word(dev, pos + PCI_MSI_DATA_64,
					      msg->data);
		} else {
			pci_write_config_word(dev, pos + PCI_MSI_DATA_32,
					      msg->data);
		}
	}
	entry->msg = *msg;
}

void pci_write_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct msi_desc *entry = irq_get_msi_desc(irq);

	__pci_write_msi_msg(entry, msg);
}
EXPORT_SYMBOL_GPL(pci_write_msi_msg);

static void free_msi_irqs(struct pci_dev *dev)
{
	struct list_head *msi_list = dev_to_msi_list(&dev->dev);
	struct msi_desc *entry, *tmp;
	struct attribute **msi_attrs;
	struct device_attribute *dev_attr;
	int i, count = 0;

	for_each_pci_msi_entry(entry, dev)
		if (entry->irq)
			for (i = 0; i < entry->nvec_used; i++)
				BUG_ON(irq_has_action(entry->irq + i));

	pci_msi_teardown_msi_irqs(dev);

	list_for_each_entry_safe(entry, tmp, msi_list, list) {
		if (entry->msi_attrib.is_msix) {
			if (list_is_last(&entry->list, msi_list))
				iounmap(entry->mask_base);
		}

		list_del(&entry->list);
		kfree(entry);
	}

	if (dev->msi_irq_groups) {
		sysfs_remove_groups(&dev->dev.kobj, dev->msi_irq_groups);
		msi_attrs = dev->msi_irq_groups[0]->attrs;
		while (msi_attrs[count]) {
			dev_attr = container_of(msi_attrs[count],
						struct device_attribute, attr);
			kfree(dev_attr->attr.name);
			kfree(dev_attr);
			++count;
		}
		kfree(msi_attrs);
		kfree(dev->msi_irq_groups[0]);
		kfree(dev->msi_irq_groups);
		dev->msi_irq_groups = NULL;
	}
}

static void pci_intx_for_msi(struct pci_dev *dev, int enable)
{
	if (!(dev->dev_flags & PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG))
		pci_intx(dev, enable);
}

static void __pci_restore_msi_state(struct pci_dev *dev)
{
	u16 control;
	struct msi_desc *entry;

	if (!dev->msi_enabled)
		return;

	entry = irq_get_msi_desc(dev->irq);

	pci_intx_for_msi(dev, 0);
	pci_msi_set_enable(dev, 0);
	arch_restore_msi_irqs(dev);

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &control);
	msi_mask_irq(entry, msi_mask(entry->msi_attrib.multi_cap),
		     entry->masked);
	control &= ~PCI_MSI_FLAGS_QSIZE;
	control |= (entry->msi_attrib.multiple << 4) | PCI_MSI_FLAGS_ENABLE;
	pci_write_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, control);
}

static void __pci_restore_msix_state(struct pci_dev *dev)
{
	struct msi_desc *entry;

	if (!dev->msix_enabled)
		return;
	BUG_ON(list_empty(dev_to_msi_list(&dev->dev)));

	/* route the table */
	pci_intx_for_msi(dev, 0);
	pci_msix_clear_and_set_ctrl(dev, 0,
				PCI_MSIX_FLAGS_ENABLE | PCI_MSIX_FLAGS_MASKALL);

	arch_restore_msi_irqs(dev);
	for_each_pci_msi_entry(entry, dev)
		msix_mask_irq(entry, entry->masked);

	pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL, 0);
}

void pci_restore_msi_state(struct pci_dev *dev)
{
	__pci_restore_msi_state(dev);
	__pci_restore_msix_state(dev);
}
EXPORT_SYMBOL_GPL(pci_restore_msi_state);

static ssize_t msi_mode_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct msi_desc *entry;
	unsigned long irq;
	int retval;

	retval = kstrtoul(attr->attr.name, 10, &irq);
	if (retval)
		return retval;

	entry = irq_get_msi_desc(irq);
	if (entry)
		return sprintf(buf, "%s\n",
				entry->msi_attrib.is_msix ? "msix" : "msi");

	return -ENODEV;
}

static int populate_msi_sysfs(struct pci_dev *pdev)
{
	struct attribute **msi_attrs;
	struct attribute *msi_attr;
	struct device_attribute *msi_dev_attr;
	struct attribute_group *msi_irq_group;
	const struct attribute_group **msi_irq_groups;
	struct msi_desc *entry;
	int ret = -ENOMEM;
	int num_msi = 0;
	int count = 0;
	int i;

	/* Determine how many msi entries we have */
	for_each_pci_msi_entry(entry, pdev)
		num_msi += entry->nvec_used;
	if (!num_msi)
		return 0;

	/* Dynamically create the MSI attributes for the PCI device */
	msi_attrs = kzalloc(sizeof(void *) * (num_msi + 1), GFP_KERNEL);
	if (!msi_attrs)
		return -ENOMEM;
	for_each_pci_msi_entry(entry, pdev) {
		for (i = 0; i < entry->nvec_used; i++) {
			msi_dev_attr = kzalloc(sizeof(*msi_dev_attr), GFP_KERNEL);
			if (!msi_dev_attr)
				goto error_attrs;
			msi_attrs[count] = &msi_dev_attr->attr;

			sysfs_attr_init(&msi_dev_attr->attr);
			msi_dev_attr->attr.name = kasprintf(GFP_KERNEL, "%d",
							    entry->irq + i);
			if (!msi_dev_attr->attr.name)
				goto error_attrs;
			msi_dev_attr->attr.mode = S_IRUGO;
			msi_dev_attr->show = msi_mode_show;
			++count;
		}
	}

	msi_irq_group = kzalloc(sizeof(*msi_irq_group), GFP_KERNEL);
	if (!msi_irq_group)
		goto error_attrs;
	msi_irq_group->name = "msi_irqs";
	msi_irq_group->attrs = msi_attrs;

	msi_irq_groups = kzalloc(sizeof(void *) * 2, GFP_KERNEL);
	if (!msi_irq_groups)
		goto error_irq_group;
	msi_irq_groups[0] = msi_irq_group;

	ret = sysfs_create_groups(&pdev->dev.kobj, msi_irq_groups);
	if (ret)
		goto error_irq_groups;
	pdev->msi_irq_groups = msi_irq_groups;

	return 0;

error_irq_groups:
	kfree(msi_irq_groups);
error_irq_group:
	kfree(msi_irq_group);
error_attrs:
	count = 0;
	msi_attr = msi_attrs[count];
	while (msi_attr) {
		msi_dev_attr = container_of(msi_attr, struct device_attribute, attr);
		kfree(msi_attr->name);
		kfree(msi_dev_attr);
		++count;
		msi_attr = msi_attrs[count];
	}
	kfree(msi_attrs);
	return ret;
}

static struct msi_desc *msi_setup_entry(struct pci_dev *dev, int nvec)
{
	u16 control;
	struct msi_desc *entry;

	/* MSI Entry Initialization */
	entry = alloc_msi_entry(&dev->dev);
	if (!entry)
		return NULL;

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &control);

	entry->msi_attrib.is_msix	= 0;
	entry->msi_attrib.is_64		= !!(control & PCI_MSI_FLAGS_64BIT);
	entry->msi_attrib.entry_nr	= 0;
	entry->msi_attrib.maskbit	= !!(control & PCI_MSI_FLAGS_MASKBIT);
	entry->msi_attrib.default_irq	= dev->irq;	/* Save IOAPIC IRQ */
	entry->msi_attrib.multi_cap	= (control & PCI_MSI_FLAGS_QMASK) >> 1;
	entry->msi_attrib.multiple	= ilog2(__roundup_pow_of_two(nvec));
	entry->nvec_used		= nvec;
	entry->affinity			= dev->irq_affinity;

	if (control & PCI_MSI_FLAGS_64BIT)
		entry->mask_pos = dev->msi_cap + PCI_MSI_MASK_64;
	else
		entry->mask_pos = dev->msi_cap + PCI_MSI_MASK_32;

	/* Save the initial mask status */
	if (entry->msi_attrib.maskbit)
		pci_read_config_dword(dev, entry->mask_pos, &entry->masked);

	return entry;
}

static int msi_verify_entries(struct pci_dev *dev)
{
	struct msi_desc *entry;

	for_each_pci_msi_entry(entry, dev) {
		if (!dev->no_64bit_msi || !entry->msg.address_hi)
			continue;
		dev_err(&dev->dev, "Device has broken 64-bit MSI but arch"
			" tried to assign one above 4G\n");
		return -EIO;
	}
	return 0;
}

/**
 * msi_capability_init - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 * @nvec: number of interrupts to allocate
 *
 * Setup the MSI capability structure of the device with the requested
 * number of interrupts.  A return value of zero indicates the successful
 * setup of an entry with the new MSI irq.  A negative return value indicates
 * an error, and a positive return value indicates the number of interrupts
 * which could have been allocated.
 */
static int msi_capability_init(struct pci_dev *dev, int nvec)
{
	struct msi_desc *entry;
	int ret;
	unsigned mask;

	pci_msi_set_enable(dev, 0);	/* Disable MSI during set up */

	entry = msi_setup_entry(dev, nvec);
	if (!entry)
		return -ENOMEM;

	/* All MSIs are unmasked by default, Mask them all */
	mask = msi_mask(entry->msi_attrib.multi_cap);
	msi_mask_irq(entry, mask, mask);

	list_add_tail(&entry->list, dev_to_msi_list(&dev->dev));

	/* Configure MSI capability structure */
	ret = pci_msi_setup_msi_irqs(dev, nvec, PCI_CAP_ID_MSI);
	if (ret) {
		msi_mask_irq(entry, mask, ~mask);
		free_msi_irqs(dev);
		return ret;
	}

	ret = msi_verify_entries(dev);
	if (ret) {
		msi_mask_irq(entry, mask, ~mask);
		free_msi_irqs(dev);
		return ret;
	}

	ret = populate_msi_sysfs(dev);
	if (ret) {
		msi_mask_irq(entry, mask, ~mask);
		free_msi_irqs(dev);
		return ret;
	}

	/* Set MSI enabled bits	 */
	pci_intx_for_msi(dev, 0);
	pci_msi_set_enable(dev, 1);
	dev->msi_enabled = 1;

	pcibios_free_irq(dev);
	dev->irq = entry->irq;
	return 0;
}

static void __iomem *msix_map_region(struct pci_dev *dev, unsigned nr_entries)
{
	resource_size_t phys_addr;
	u32 table_offset;
	unsigned long flags;
	u8 bir;

	pci_read_config_dword(dev, dev->msix_cap + PCI_MSIX_TABLE,
			      &table_offset);
	bir = (u8)(table_offset & PCI_MSIX_TABLE_BIR);
	flags = pci_resource_flags(dev, bir);
	if (!flags || (flags & IORESOURCE_UNSET))
		return NULL;

	table_offset &= PCI_MSIX_TABLE_OFFSET;
	phys_addr = pci_resource_start(dev, bir) + table_offset;

	return ioremap_nocache(phys_addr, nr_entries * PCI_MSIX_ENTRY_SIZE);
}

static int msix_setup_entries(struct pci_dev *dev, void __iomem *base,
			      struct msix_entry *entries, int nvec)
{
	const struct cpumask *mask = NULL;
	struct msi_desc *entry;
	int cpu = -1, i;

	for (i = 0; i < nvec; i++) {
		if (dev->irq_affinity) {
			cpu = cpumask_next(cpu, dev->irq_affinity);
			if (cpu >= nr_cpu_ids)
				cpu = cpumask_first(dev->irq_affinity);
			mask = cpumask_of(cpu);
		}

		entry = alloc_msi_entry(&dev->dev);
		if (!entry) {
			if (!i)
				iounmap(base);
			else
				free_msi_irqs(dev);
			/* No enough memory. Don't try again */
			return -ENOMEM;
		}

		entry->msi_attrib.is_msix	= 1;
		entry->msi_attrib.is_64		= 1;
		if (entries)
			entry->msi_attrib.entry_nr = entries[i].entry;
		else
			entry->msi_attrib.entry_nr = i;
		entry->msi_attrib.default_irq	= dev->irq;
		entry->mask_base		= base;
		entry->nvec_used		= 1;
		entry->affinity			= mask;

		list_add_tail(&entry->list, dev_to_msi_list(&dev->dev));
	}

	return 0;
}

static void msix_program_entries(struct pci_dev *dev,
				 struct msix_entry *entries)
{
	struct msi_desc *entry;
	int i = 0;

	for_each_pci_msi_entry(entry, dev) {
		if (entries)
			entries[i++].vector = entry->irq;
		entry->masked = readl(pci_msix_desc_addr(entry) +
				PCI_MSIX_ENTRY_VECTOR_CTRL);
		msix_mask_irq(entry, 1);
	}
}

/**
 * msix_capability_init - configure device's MSI-X capability
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @entries: pointer to an array of struct msix_entry entries
 * @nvec: number of @entries
 *
 * Setup the MSI-X capability structure of device function with a
 * single MSI-X irq. A return of zero indicates the successful setup of
 * requested MSI-X entries with allocated irqs or non-zero for otherwise.
 **/
static int msix_capability_init(struct pci_dev *dev,
				struct msix_entry *entries, int nvec)
{
	int ret;
	u16 control;
	void __iomem *base;

	/* Ensure MSI-X is disabled while it is set up */
	pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, 0);

	pci_read_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, &control);
	/* Request & Map MSI-X table region */
	base = msix_map_region(dev, msix_table_size(control));
	if (!base)
		return -ENOMEM;

	ret = msix_setup_entries(dev, base, entries, nvec);
	if (ret)
		return ret;

	ret = pci_msi_setup_msi_irqs(dev, nvec, PCI_CAP_ID_MSIX);
	if (ret)
		goto out_avail;

	/* Check if all MSI entries honor device restrictions */
	ret = msi_verify_entries(dev);
	if (ret)
		goto out_free;

	/*
	 * Some devices require MSI-X to be enabled before we can touch the
	 * MSI-X registers.  We need to mask all the vectors to prevent
	 * interrupts coming in before they're fully set up.
	 */
	pci_msix_clear_and_set_ctrl(dev, 0,
				PCI_MSIX_FLAGS_MASKALL | PCI_MSIX_FLAGS_ENABLE);

	msix_program_entries(dev, entries);

	ret = populate_msi_sysfs(dev);
	if (ret)
		goto out_free;

	/* Set MSI-X enabled bits and unmask the function */
	pci_intx_for_msi(dev, 0);
	dev->msix_enabled = 1;
	pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL, 0);

	pcibios_free_irq(dev);
	return 0;

out_avail:
	if (ret < 0) {
		/*
		 * If we had some success, report the number of irqs
		 * we succeeded in setting up.
		 */
		struct msi_desc *entry;
		int avail = 0;

		for_each_pci_msi_entry(entry, dev) {
			if (entry->irq != 0)
				avail++;
		}
		if (avail != 0)
			ret = avail;
	}

out_free:
	free_msi_irqs(dev);

	return ret;
}

/**
 * pci_msi_supported - check whether MSI may be enabled on a device
 * @dev: pointer to the pci_dev data structure of MSI device function
 * @nvec: how many MSIs have been requested ?
 *
 * Look at global flags, the device itself, and its parent buses
 * to determine if MSI/-X are supported for the device. If MSI/-X is
 * supported return 1, else return 0.
 **/
static int pci_msi_supported(struct pci_dev *dev, int nvec)
{
	struct pci_bus *bus;

	/* MSI must be globally enabled and supported by the device */
	if (!pci_msi_enable)
		return 0;

	if (!dev || dev->no_msi || dev->current_state != PCI_D0)
		return 0;

	/*
	 * You can't ask to have 0 or less MSIs configured.
	 *  a) it's stupid ..
	 *  b) the list manipulation code assumes nvec >= 1.
	 */
	if (nvec < 1)
		return 0;

	/*
	 * Any bridge which does NOT route MSI transactions from its
	 * secondary bus to its primary bus must set NO_MSI flag on
	 * the secondary pci_bus.
	 * We expect only arch-specific PCI host bus controller driver
	 * or quirks for specific PCI bridges to be setting NO_MSI.
	 */
	for (bus = dev->bus; bus; bus = bus->parent)
		if (bus->bus_flags & PCI_BUS_FLAGS_NO_MSI)
			return 0;

	return 1;
}

/**
 * pci_msi_vec_count - Return the number of MSI vectors a device can send
 * @dev: device to report about
 *
 * This function returns the number of MSI vectors a device requested via
 * Multiple Message Capable register. It returns a negative errno if the
 * device is not capable sending MSI interrupts. Otherwise, the call succeeds
 * and returns a power of two, up to a maximum of 2^5 (32), according to the
 * MSI specification.
 **/
int pci_msi_vec_count(struct pci_dev *dev)
{
	int ret;
	u16 msgctl;

	if (!dev->msi_cap)
		return -EINVAL;

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &msgctl);
	ret = 1 << ((msgctl & PCI_MSI_FLAGS_QMASK) >> 1);

	return ret;
}
EXPORT_SYMBOL(pci_msi_vec_count);

void pci_msi_shutdown(struct pci_dev *dev)
{
	struct msi_desc *desc;
	u32 mask;

	if (!pci_msi_enable || !dev || !dev->msi_enabled)
		return;

	BUG_ON(list_empty(dev_to_msi_list(&dev->dev)));
	desc = first_pci_msi_entry(dev);

	pci_msi_set_enable(dev, 0);
	pci_intx_for_msi(dev, 1);
	dev->msi_enabled = 0;

	/* Return the device with MSI unmasked as initial states */
	mask = msi_mask(desc->msi_attrib.multi_cap);
	/* Keep cached state to be restored */
	__pci_msi_desc_mask_irq(desc, mask, ~mask);

	/* Restore dev->irq to its default pin-assertion irq */
	dev->irq = desc->msi_attrib.default_irq;
	pcibios_alloc_irq(dev);
}

void pci_disable_msi(struct pci_dev *dev)
{
	if (!pci_msi_enable || !dev || !dev->msi_enabled)
		return;

	pci_msi_shutdown(dev);
	free_msi_irqs(dev);
}
EXPORT_SYMBOL(pci_disable_msi);

/**
 * pci_msix_vec_count - return the number of device's MSI-X table entries
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * This function returns the number of device's MSI-X table entries and
 * therefore the number of MSI-X vectors device is capable of sending.
 * It returns a negative errno if the device is not capable of sending MSI-X
 * interrupts.
 **/
int pci_msix_vec_count(struct pci_dev *dev)
{
	u16 control;

	if (!dev->msix_cap)
		return -EINVAL;

	pci_read_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, &control);
	return msix_table_size(control);
}
EXPORT_SYMBOL(pci_msix_vec_count);

/**
 * pci_enable_msix - configure device's MSI-X capability structure
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @entries: pointer to an array of MSI-X entries (optional)
 * @nvec: number of MSI-X irqs requested for allocation by device driver
 *
 * Setup the MSI-X capability structure of device function with the number
 * of requested irqs upon its software driver call to request for
 * MSI-X mode enabled on its hardware device function. A return of zero
 * indicates the successful configuration of MSI-X capability structure
 * with new allocated MSI-X irqs. A return of < 0 indicates a failure.
 * Or a return of > 0 indicates that driver request is exceeding the number
 * of irqs or MSI-X vectors available. Driver should use the returned value to
 * re-send its request.
 **/
int pci_enable_msix(struct pci_dev *dev, struct msix_entry *entries, int nvec)
{
	int nr_entries;
	int i, j;

	if (!pci_msi_supported(dev, nvec))
		return -EINVAL;

	nr_entries = pci_msix_vec_count(dev);
	if (nr_entries < 0)
		return nr_entries;
	if (nvec > nr_entries)
		return nr_entries;

	if (entries) {
		/* Check for any invalid entries */
		for (i = 0; i < nvec; i++) {
			if (entries[i].entry >= nr_entries)
				return -EINVAL;		/* invalid entry */
			for (j = i + 1; j < nvec; j++) {
				if (entries[i].entry == entries[j].entry)
					return -EINVAL;	/* duplicate entry */
			}
		}
	}
	WARN_ON(!!dev->msix_enabled);

	/* Check whether driver already requested for MSI irq */
	if (dev->msi_enabled) {
		dev_info(&dev->dev, "can't enable MSI-X (MSI IRQ already assigned)\n");
		return -EINVAL;
	}
	return msix_capability_init(dev, entries, nvec);
}
EXPORT_SYMBOL(pci_enable_msix);

void pci_msix_shutdown(struct pci_dev *dev)
{
	struct msi_desc *entry;

	if (!pci_msi_enable || !dev || !dev->msix_enabled)
		return;

	/* Return the device with MSI-X masked as initial states */
	for_each_pci_msi_entry(entry, dev) {
		/* Keep cached states to be restored */
		__pci_msix_desc_mask_irq(entry, 1);
	}

	pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, 0);
	pci_intx_for_msi(dev, 1);
	dev->msix_enabled = 0;
	pcibios_alloc_irq(dev);
}

void pci_disable_msix(struct pci_dev *dev)
{
	if (!pci_msi_enable || !dev || !dev->msix_enabled)
		return;

	pci_msix_shutdown(dev);
	free_msi_irqs(dev);
}
EXPORT_SYMBOL(pci_disable_msix);

void pci_no_msi(void)
{
	pci_msi_enable = 0;
}

/**
 * pci_msi_enabled - is MSI enabled?
 *
 * Returns true if MSI has not been disabled by the command-line option
 * pci=nomsi.
 **/
int pci_msi_enabled(void)
{
	return pci_msi_enable;
}
EXPORT_SYMBOL(pci_msi_enabled);

static int __pci_enable_msi_range(struct pci_dev *dev, int minvec, int maxvec,
		unsigned int flags)
{
	int nvec;
	int rc;

	if (!pci_msi_supported(dev, minvec))
		return -EINVAL;

	WARN_ON(!!dev->msi_enabled);

	/* Check whether driver already requested MSI-X irqs */
	if (dev->msix_enabled) {
		dev_info(&dev->dev,
			 "can't enable MSI (MSI-X already enabled)\n");
		return -EINVAL;
	}

	if (maxvec < minvec)
		return -ERANGE;

	nvec = pci_msi_vec_count(dev);
	if (nvec < 0)
		return nvec;
	if (nvec < minvec)
		return -EINVAL;

	if (nvec > maxvec)
		nvec = maxvec;

	for (;;) {
		if (flags & PCI_IRQ_AFFINITY) {
			dev->irq_affinity = irq_create_affinity_mask(&nvec);
			if (nvec < minvec)
				return -ENOSPC;
		}

		rc = msi_capability_init(dev, nvec);
		if (rc == 0)
			return nvec;

		kfree(dev->irq_affinity);
		dev->irq_affinity = NULL;

		if (rc < 0)
			return rc;
		if (rc < minvec)
			return -ENOSPC;

		nvec = rc;
	}
}

/**
 * pci_enable_msi_range - configure device's MSI capability structure
 * @dev: device to configure
 * @minvec: minimal number of interrupts to configure
 * @maxvec: maximum number of interrupts to configure
 *
 * This function tries to allocate a maximum possible number of interrupts in a
 * range between @minvec and @maxvec. It returns a negative errno if an error
 * occurs. If it succeeds, it returns the actual number of interrupts allocated
 * and updates the @dev's irq member to the lowest new interrupt number;
 * the other interrupt numbers allocated to this device are consecutive.
 **/
int pci_enable_msi_range(struct pci_dev *dev, int minvec, int maxvec)
{
	return __pci_enable_msi_range(dev, minvec, maxvec, 0);
}
EXPORT_SYMBOL(pci_enable_msi_range);

static int __pci_enable_msix_range(struct pci_dev *dev,
		struct msix_entry *entries, int minvec, int maxvec,
		unsigned int flags)
{
	int nvec = maxvec;
	int rc;

	if (maxvec < minvec)
		return -ERANGE;

	for (;;) {
		if (flags & PCI_IRQ_AFFINITY) {
			dev->irq_affinity = irq_create_affinity_mask(&nvec);
			if (nvec < minvec)
				return -ENOSPC;
		}

		rc = pci_enable_msix(dev, entries, nvec);
		if (rc == 0)
			return nvec;

		kfree(dev->irq_affinity);
		dev->irq_affinity = NULL;

		if (rc < 0)
			return rc;
		if (rc < minvec)
			return -ENOSPC;

		nvec = rc;
	}
}

/**
 * pci_enable_msix_range - configure device's MSI-X capability structure
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @entries: pointer to an array of MSI-X entries
 * @minvec: minimum number of MSI-X irqs requested
 * @maxvec: maximum number of MSI-X irqs requested
 *
 * Setup the MSI-X capability structure of device function with a maximum
 * possible number of interrupts in the range between @minvec and @maxvec
 * upon its software driver call to request for MSI-X mode enabled on its
 * hardware device function. It returns a negative errno if an error occurs.
 * If it succeeds, it returns the actual number of interrupts allocated and
 * indicates the successful configuration of MSI-X capability structure
 * with new allocated MSI-X interrupts.
 **/
int pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries,
		int minvec, int maxvec)
{
	return __pci_enable_msix_range(dev, entries, minvec, maxvec, 0);
}
EXPORT_SYMBOL(pci_enable_msix_range);

/**
 * pci_alloc_irq_vectors - allocate multiple IRQs for a device
 * @dev:		PCI device to operate on
 * @min_vecs:		minimum number of vectors required (must be >= 1)
 * @max_vecs:		maximum (desired) number of vectors
 * @flags:		flags or quirks for the allocation
 *
 * Allocate up to @max_vecs interrupt vectors for @dev, using MSI-X or MSI
 * vectors if available, and fall back to a single legacy vector
 * if neither is available.  Return the number of vectors allocated,
 * (which might be smaller than @max_vecs) if successful, or a negative
 * error code on error. If less than @min_vecs interrupt vectors are
 * available for @dev the function will fail with -ENOSPC.
 *
 * To get the Linux IRQ number used for a vector that can be passed to
 * request_irq() use the pci_irq_vector() helper.
 */
int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
		unsigned int max_vecs, unsigned int flags)
{
	int vecs = -ENOSPC;

	if (flags & PCI_IRQ_MSIX) {
		vecs = __pci_enable_msix_range(dev, NULL, min_vecs, max_vecs,
				flags);
		if (vecs > 0)
			return vecs;
	}

	if (flags & PCI_IRQ_MSI) {
		vecs = __pci_enable_msi_range(dev, min_vecs, max_vecs, flags);
		if (vecs > 0)
			return vecs;
	}

	/* use legacy irq if allowed */
	if ((flags & PCI_IRQ_LEGACY) && min_vecs == 1) {
		pci_intx(dev, 1);
		return 1;
	}

	return vecs;
}
EXPORT_SYMBOL(pci_alloc_irq_vectors);

/**
 * pci_free_irq_vectors - free previously allocated IRQs for a device
 * @dev:		PCI device to operate on
 *
 * Undoes the allocations and enabling in pci_alloc_irq_vectors().
 */
void pci_free_irq_vectors(struct pci_dev *dev)
{
	pci_disable_msix(dev);
	pci_disable_msi(dev);
}
EXPORT_SYMBOL(pci_free_irq_vectors);

/**
 * pci_irq_vector - return Linux IRQ number of a device vector
 * @dev: PCI device to operate on
 * @nr: device-relative interrupt vector index (0-based).
 */
int pci_irq_vector(struct pci_dev *dev, unsigned int nr)
{
	if (dev->msix_enabled) {
		struct msi_desc *entry;
		int i = 0;

		for_each_pci_msi_entry(entry, dev) {
			if (i == nr)
				return entry->irq;
			i++;
		}
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	if (dev->msi_enabled) {
		struct msi_desc *entry = first_pci_msi_entry(dev);

		if (WARN_ON_ONCE(nr >= entry->nvec_used))
			return -EINVAL;
	} else {
		if (WARN_ON_ONCE(nr > 0))
			return -EINVAL;
	}

	return dev->irq + nr;
}
EXPORT_SYMBOL(pci_irq_vector);

struct pci_dev *msi_desc_to_pci_dev(struct msi_desc *desc)
{
	return to_pci_dev(desc->dev);
}
EXPORT_SYMBOL(msi_desc_to_pci_dev);

void *msi_desc_to_pci_sysdata(struct msi_desc *desc)
{
	struct pci_dev *dev = msi_desc_to_pci_dev(desc);

	return dev->bus->sysdata;
}
EXPORT_SYMBOL_GPL(msi_desc_to_pci_sysdata);

#ifdef CONFIG_PCI_MSI_IRQ_DOMAIN
/**
 * pci_msi_domain_write_msg - Helper to write MSI message to PCI config space
 * @irq_data:	Pointer to interrupt data of the MSI interrupt
 * @msg:	Pointer to the message
 */
void pci_msi_domain_write_msg(struct irq_data *irq_data, struct msi_msg *msg)
{
	struct msi_desc *desc = irq_data_get_msi_desc(irq_data);

	/*
	 * For MSI-X desc->irq is always equal to irq_data->irq. For
	 * MSI only the first interrupt of MULTI MSI passes the test.
	 */
	if (desc->irq == irq_data->irq)
		__pci_write_msi_msg(desc, msg);
}

/**
 * pci_msi_domain_calc_hwirq - Generate a unique ID for an MSI source
 * @dev:	Pointer to the PCI device
 * @desc:	Pointer to the msi descriptor
 *
 * The ID number is only used within the irqdomain.
 */
irq_hw_number_t pci_msi_domain_calc_hwirq(struct pci_dev *dev,
					  struct msi_desc *desc)
{
	return (irq_hw_number_t)desc->msi_attrib.entry_nr |
		PCI_DEVID(dev->bus->number, dev->devfn) << 11 |
		(pci_domain_nr(dev->bus) & 0xFFFFFFFF) << 27;
}

static inline bool pci_msi_desc_is_multi_msi(struct msi_desc *desc)
{
	return !desc->msi_attrib.is_msix && desc->nvec_used > 1;
}

/**
 * pci_msi_domain_check_cap - Verify that @domain supports the capabilities for @dev
 * @domain:	The interrupt domain to check
 * @info:	The domain info for verification
 * @dev:	The device to check
 *
 * Returns:
 *  0 if the functionality is supported
 *  1 if Multi MSI is requested, but the domain does not support it
 *  -ENOTSUPP otherwise
 */
int pci_msi_domain_check_cap(struct irq_domain *domain,
			     struct msi_domain_info *info, struct device *dev)
{
	struct msi_desc *desc = first_pci_msi_entry(to_pci_dev(dev));

	/* Special handling to support pci_enable_msi_range() */
	if (pci_msi_desc_is_multi_msi(desc) &&
	    !(info->flags & MSI_FLAG_MULTI_PCI_MSI))
		return 1;
	else if (desc->msi_attrib.is_msix && !(info->flags & MSI_FLAG_PCI_MSIX))
		return -ENOTSUPP;

	return 0;
}

static int pci_msi_domain_handle_error(struct irq_domain *domain,
				       struct msi_desc *desc, int error)
{
	/* Special handling to support pci_enable_msi_range() */
	if (pci_msi_desc_is_multi_msi(desc) && error == -ENOSPC)
		return 1;

	return error;
}

#ifdef GENERIC_MSI_DOMAIN_OPS
static void pci_msi_domain_set_desc(msi_alloc_info_t *arg,
				    struct msi_desc *desc)
{
	arg->desc = desc;
	arg->hwirq = pci_msi_domain_calc_hwirq(msi_desc_to_pci_dev(desc),
					       desc);
}
#else
#define pci_msi_domain_set_desc		NULL
#endif

static struct msi_domain_ops pci_msi_domain_ops_default = {
	.set_desc	= pci_msi_domain_set_desc,
	.msi_check	= pci_msi_domain_check_cap,
	.handle_error	= pci_msi_domain_handle_error,
};

static void pci_msi_domain_update_dom_ops(struct msi_domain_info *info)
{
	struct msi_domain_ops *ops = info->ops;

	if (ops == NULL) {
		info->ops = &pci_msi_domain_ops_default;
	} else {
		if (ops->set_desc == NULL)
			ops->set_desc = pci_msi_domain_set_desc;
		if (ops->msi_check == NULL)
			ops->msi_check = pci_msi_domain_check_cap;
		if (ops->handle_error == NULL)
			ops->handle_error = pci_msi_domain_handle_error;
	}
}

static void pci_msi_domain_update_chip_ops(struct msi_domain_info *info)
{
	struct irq_chip *chip = info->chip;

	BUG_ON(!chip);
	if (!chip->irq_write_msi_msg)
		chip->irq_write_msi_msg = pci_msi_domain_write_msg;
	if (!chip->irq_mask)
		chip->irq_mask = pci_msi_mask_irq;
	if (!chip->irq_unmask)
		chip->irq_unmask = pci_msi_unmask_irq;
}

/**
 * pci_msi_create_irq_domain - Create a MSI interrupt domain
 * @fwnode:	Optional fwnode of the interrupt controller
 * @info:	MSI domain info
 * @parent:	Parent irq domain
 *
 * Updates the domain and chip ops and creates a MSI interrupt domain.
 *
 * Returns:
 * A domain pointer or NULL in case of failure.
 */
struct irq_domain *pci_msi_create_irq_domain(struct fwnode_handle *fwnode,
					     struct msi_domain_info *info,
					     struct irq_domain *parent)
{
	struct irq_domain *domain;

	if (info->flags & MSI_FLAG_USE_DEF_DOM_OPS)
		pci_msi_domain_update_dom_ops(info);
	if (info->flags & MSI_FLAG_USE_DEF_CHIP_OPS)
		pci_msi_domain_update_chip_ops(info);

	info->flags |= MSI_FLAG_ACTIVATE_EARLY;

	domain = msi_create_irq_domain(fwnode, info, parent);
	if (!domain)
		return NULL;

	domain->bus_token = DOMAIN_BUS_PCI_MSI;
	return domain;
}
EXPORT_SYMBOL_GPL(pci_msi_create_irq_domain);

/**
 * pci_msi_domain_alloc_irqs - Allocate interrupts for @dev in @domain
 * @domain:	The interrupt domain to allocate from
 * @dev:	The device for which to allocate
 * @nvec:	The number of interrupts to allocate
 * @type:	Unused to allow simpler migration from the arch_XXX interfaces
 *
 * Returns:
 * A virtual interrupt number or an error code in case of failure
 */
int pci_msi_domain_alloc_irqs(struct irq_domain *domain, struct pci_dev *dev,
			      int nvec, int type)
{
	return msi_domain_alloc_irqs(domain, &dev->dev, nvec);
}

/**
 * pci_msi_domain_free_irqs - Free interrupts for @dev in @domain
 * @domain:	The interrupt domain
 * @dev:	The device for which to free interrupts
 */
void pci_msi_domain_free_irqs(struct irq_domain *domain, struct pci_dev *dev)
{
	msi_domain_free_irqs(domain, &dev->dev);
}

/**
 * pci_msi_create_default_irq_domain - Create a default MSI interrupt domain
 * @fwnode:	Optional fwnode of the interrupt controller
 * @info:	MSI domain info
 * @parent:	Parent irq domain
 *
 * Returns: A domain pointer or NULL in case of failure. If successful
 * the default PCI/MSI irqdomain pointer is updated.
 */
struct irq_domain *pci_msi_create_default_irq_domain(struct fwnode_handle *fwnode,
		struct msi_domain_info *info, struct irq_domain *parent)
{
	struct irq_domain *domain;

	mutex_lock(&pci_msi_domain_lock);
	if (pci_msi_default_domain) {
		pr_err("PCI: default irq domain for PCI MSI has already been created.\n");
		domain = NULL;
	} else {
		domain = pci_msi_create_irq_domain(fwnode, info, parent);
		pci_msi_default_domain = domain;
	}
	mutex_unlock(&pci_msi_domain_lock);

	return domain;
}

static int get_msi_id_cb(struct pci_dev *pdev, u16 alias, void *data)
{
	u32 *pa = data;

	*pa = alias;
	return 0;
}
/**
 * pci_msi_domain_get_msi_rid - Get the MSI requester id (RID)
 * @domain:	The interrupt domain
 * @pdev:	The PCI device.
 *
 * The RID for a device is formed from the alias, with a firmware
 * supplied mapping applied
 *
 * Returns: The RID.
 */
u32 pci_msi_domain_get_msi_rid(struct irq_domain *domain, struct pci_dev *pdev)
{
	struct device_node *of_node;
	u32 rid = 0;

	pci_for_each_dma_alias(pdev, get_msi_id_cb, &rid);

	of_node = irq_domain_get_of_node(domain);
	if (of_node)
		rid = of_msi_map_rid(&pdev->dev, of_node, rid);

	return rid;
}

/**
 * pci_msi_get_device_domain - Get the MSI domain for a given PCI device
 * @pdev:	The PCI device
 *
 * Use the firmware data to find a device-specific MSI domain
 * (i.e. not one that is ste as a default).
 *
 * Returns: The coresponding MSI domain or NULL if none has been found.
 */
struct irq_domain *pci_msi_get_device_domain(struct pci_dev *pdev)
{
	u32 rid = 0;

	pci_for_each_dma_alias(pdev, get_msi_id_cb, &rid);
	return of_msi_map_get_device_domain(&pdev->dev, rid);
}
#endif /* CONFIG_PCI_MSI_IRQ_DOMAIN */
