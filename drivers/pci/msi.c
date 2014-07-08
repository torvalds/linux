/*
 * File:	msi.c
 * Purpose:	PCI Message Signaled Interrupt (MSI)
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
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

#include "pci.h"

static int pci_msi_enable = 1;

#define msix_table_size(flags)	((flags & PCI_MSIX_FLAGS_QSIZE) + 1)


/* Arch hooks */

int __weak arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc)
{
	struct msi_chip *chip = dev->bus->msi;
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
	struct msi_chip *chip = irq_get_chip_data(irq);

	if (!chip || !chip->teardown_irq)
		return;

	chip->teardown_irq(chip, irq);
}

int __weak arch_msi_check_device(struct pci_dev *dev, int nvec, int type)
{
	struct msi_chip *chip = dev->bus->msi;

	if (!chip || !chip->check_device)
		return 0;

	return chip->check_device(chip, dev, nvec, type);
}

int __weak arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *entry;
	int ret;

	/*
	 * If an architecture wants to support multiple MSI, it needs to
	 * override arch_setup_msi_irqs()
	 */
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	list_for_each_entry(entry, &dev->msi_list, list) {
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
	struct msi_desc *entry;

	list_for_each_entry(entry, &dev->msi_list, list) {
		int i, nvec;
		if (entry->irq == 0)
			continue;
		if (entry->nvec_used)
			nvec = entry->nvec_used;
		else
			nvec = 1 << entry->msi_attrib.multiple;
		for (i = 0; i < nvec; i++)
			arch_teardown_msi_irq(entry->irq + i);
	}
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
		list_for_each_entry(entry, &dev->msi_list, list) {
			if (irq == entry->irq)
				break;
		}
	} else if (dev->msi_enabled)  {
		entry = irq_get_msi_desc(irq);
	}

	if (entry)
		write_msi_msg(irq, &entry->msg);
}

void __weak arch_restore_msi_irqs(struct pci_dev *dev)
{
	return default_restore_msi_irqs(dev);
}

static void msi_set_enable(struct pci_dev *dev, int enable)
{
	u16 control;

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &control);
	control &= ~PCI_MSI_FLAGS_ENABLE;
	if (enable)
		control |= PCI_MSI_FLAGS_ENABLE;
	pci_write_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, control);
}

static void msix_clear_and_set_ctrl(struct pci_dev *dev, u16 clear, u16 set)
{
	u16 ctrl;

	pci_read_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, &ctrl);
	ctrl &= ~clear;
	ctrl |= set;
	pci_write_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, ctrl);
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
u32 default_msi_mask_irq(struct msi_desc *desc, u32 mask, u32 flag)
{
	u32 mask_bits = desc->masked;

	if (!desc->msi_attrib.maskbit)
		return 0;

	mask_bits &= ~mask;
	mask_bits |= flag;
	pci_write_config_dword(desc->dev, desc->mask_pos, mask_bits);

	return mask_bits;
}

__weak u32 arch_msi_mask_irq(struct msi_desc *desc, u32 mask, u32 flag)
{
	return default_msi_mask_irq(desc, mask, flag);
}

static void msi_mask_irq(struct msi_desc *desc, u32 mask, u32 flag)
{
	desc->masked = arch_msi_mask_irq(desc, mask, flag);
}

/*
 * This internal function does not flush PCI writes to the device.
 * All users must ensure that they read from the device before either
 * assuming that the device state is up to date, or returning out of this
 * file.  This saves a few milliseconds when initialising devices with lots
 * of MSI-X interrupts.
 */
u32 default_msix_mask_irq(struct msi_desc *desc, u32 flag)
{
	u32 mask_bits = desc->masked;
	unsigned offset = desc->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
						PCI_MSIX_ENTRY_VECTOR_CTRL;
	mask_bits &= ~PCI_MSIX_ENTRY_CTRL_MASKBIT;
	if (flag)
		mask_bits |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
	writel(mask_bits, desc->mask_base + offset);

	return mask_bits;
}

__weak u32 arch_msix_mask_irq(struct msi_desc *desc, u32 flag)
{
	return default_msix_mask_irq(desc, flag);
}

static void msix_mask_irq(struct msi_desc *desc, u32 flag)
{
	desc->masked = arch_msix_mask_irq(desc, flag);
}

static void msi_set_mask_bit(struct irq_data *data, u32 flag)
{
	struct msi_desc *desc = irq_data_get_msi(data);

	if (desc->msi_attrib.is_msix) {
		msix_mask_irq(desc, flag);
		readl(desc->mask_base);		/* Flush write to device */
	} else {
		unsigned offset = data->irq - desc->dev->irq;
		msi_mask_irq(desc, 1 << offset, flag << offset);
	}
}

void mask_msi_irq(struct irq_data *data)
{
	msi_set_mask_bit(data, 1);
}

void unmask_msi_irq(struct irq_data *data)
{
	msi_set_mask_bit(data, 0);
}

void default_restore_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *entry;

	list_for_each_entry(entry, &dev->msi_list, list) {
		default_restore_msi_irq(dev, entry->irq);
	}
}

void __read_msi_msg(struct msi_desc *entry, struct msi_msg *msg)
{
	BUG_ON(entry->dev->current_state != PCI_D0);

	if (entry->msi_attrib.is_msix) {
		void __iomem *base = entry->mask_base +
			entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE;

		msg->address_lo = readl(base + PCI_MSIX_ENTRY_LOWER_ADDR);
		msg->address_hi = readl(base + PCI_MSIX_ENTRY_UPPER_ADDR);
		msg->data = readl(base + PCI_MSIX_ENTRY_DATA);
	} else {
		struct pci_dev *dev = entry->dev;
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

void read_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct msi_desc *entry = irq_get_msi_desc(irq);

	__read_msi_msg(entry, msg);
}

void __get_cached_msi_msg(struct msi_desc *entry, struct msi_msg *msg)
{
	/* Assert that the cache is valid, assuming that
	 * valid messages are not all-zeroes. */
	BUG_ON(!(entry->msg.address_hi | entry->msg.address_lo |
		 entry->msg.data));

	*msg = entry->msg;
}

void get_cached_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct msi_desc *entry = irq_get_msi_desc(irq);

	__get_cached_msi_msg(entry, msg);
}

void __write_msi_msg(struct msi_desc *entry, struct msi_msg *msg)
{
	if (entry->dev->current_state != PCI_D0) {
		/* Don't touch the hardware now */
	} else if (entry->msi_attrib.is_msix) {
		void __iomem *base;
		base = entry->mask_base +
			entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE;

		writel(msg->address_lo, base + PCI_MSIX_ENTRY_LOWER_ADDR);
		writel(msg->address_hi, base + PCI_MSIX_ENTRY_UPPER_ADDR);
		writel(msg->data, base + PCI_MSIX_ENTRY_DATA);
	} else {
		struct pci_dev *dev = entry->dev;
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

void write_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct msi_desc *entry = irq_get_msi_desc(irq);

	__write_msi_msg(entry, msg);
}

static void free_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *entry, *tmp;
	struct attribute **msi_attrs;
	struct device_attribute *dev_attr;
	int count = 0;

	list_for_each_entry(entry, &dev->msi_list, list) {
		int i, nvec;
		if (!entry->irq)
			continue;
		if (entry->nvec_used)
			nvec = entry->nvec_used;
		else
			nvec = 1 << entry->msi_attrib.multiple;
		for (i = 0; i < nvec; i++)
			BUG_ON(irq_has_action(entry->irq + i));
	}

	arch_teardown_msi_irqs(dev);

	list_for_each_entry_safe(entry, tmp, &dev->msi_list, list) {
		if (entry->msi_attrib.is_msix) {
			if (list_is_last(&entry->list, &dev->msi_list))
				iounmap(entry->mask_base);
		}

		/*
		 * Its possible that we get into this path
		 * When populate_msi_sysfs fails, which means the entries
		 * were not registered with sysfs.  In that case don't
		 * unregister them.
		 */
		if (entry->kobj.parent) {
			kobject_del(&entry->kobj);
			kobject_put(&entry->kobj);
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

static struct msi_desc *alloc_msi_entry(struct pci_dev *dev)
{
	struct msi_desc *desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	INIT_LIST_HEAD(&desc->list);
	desc->dev = dev;

	return desc;
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
	msi_set_enable(dev, 0);
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
	BUG_ON(list_empty(&dev->msi_list));
	entry = list_first_entry(&dev->msi_list, struct msi_desc, list);

	/* route the table */
	pci_intx_for_msi(dev, 0);
	msix_clear_and_set_ctrl(dev, 0,
				PCI_MSIX_FLAGS_ENABLE | PCI_MSIX_FLAGS_MASKALL);

	arch_restore_msi_irqs(dev);
	list_for_each_entry(entry, &dev->msi_list, list) {
		msix_mask_irq(entry, entry->masked);
	}

	msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL, 0);
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
	struct pci_dev *pdev = to_pci_dev(dev);
	struct msi_desc *entry;
	unsigned long irq;
	int retval;

	retval = kstrtoul(attr->attr.name, 10, &irq);
	if (retval)
		return retval;

	list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->irq == irq) {
			return sprintf(buf, "%s\n",
				       entry->msi_attrib.is_msix ? "msix" : "msi");
		}
	}
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

	/* Determine how many msi entries we have */
	list_for_each_entry(entry, &pdev->msi_list, list) {
		++num_msi;
	}
	if (!num_msi)
		return 0;

	/* Dynamically create the MSI attributes for the PCI device */
	msi_attrs = kzalloc(sizeof(void *) * (num_msi + 1), GFP_KERNEL);
	if (!msi_attrs)
		return -ENOMEM;
	list_for_each_entry(entry, &pdev->msi_list, list) {
		msi_dev_attr = kzalloc(sizeof(*msi_dev_attr), GFP_KERNEL);
		if (!msi_dev_attr)
			goto error_attrs;
		msi_attrs[count] = &msi_dev_attr->attr;

		sysfs_attr_init(&msi_dev_attr->attr);
		msi_dev_attr->attr.name = kasprintf(GFP_KERNEL, "%d",
						    entry->irq);
		if (!msi_dev_attr->attr.name)
			goto error_attrs;
		msi_dev_attr->attr.mode = S_IRUGO;
		msi_dev_attr->show = msi_mode_show;
		++count;
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

static struct msi_desc *msi_setup_entry(struct pci_dev *dev)
{
	u16 control;
	struct msi_desc *entry;

	/* MSI Entry Initialization */
	entry = alloc_msi_entry(dev);
	if (!entry)
		return NULL;

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &control);

	entry->msi_attrib.is_msix	= 0;
	entry->msi_attrib.is_64		= !!(control & PCI_MSI_FLAGS_64BIT);
	entry->msi_attrib.entry_nr	= 0;
	entry->msi_attrib.maskbit	= !!(control & PCI_MSI_FLAGS_MASKBIT);
	entry->msi_attrib.default_irq	= dev->irq;	/* Save IOAPIC IRQ */
	entry->msi_attrib.pos		= dev->msi_cap;
	entry->msi_attrib.multi_cap	= (control & PCI_MSI_FLAGS_QMASK) >> 1;

	if (control & PCI_MSI_FLAGS_64BIT)
		entry->mask_pos = dev->msi_cap + PCI_MSI_MASK_64;
	else
		entry->mask_pos = dev->msi_cap + PCI_MSI_MASK_32;

	/* Save the initial mask status */
	if (entry->msi_attrib.maskbit)
		pci_read_config_dword(dev, entry->mask_pos, &entry->masked);

	return entry;
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

	msi_set_enable(dev, 0);	/* Disable MSI during set up */

	entry = msi_setup_entry(dev);
	if (!entry)
		return -ENOMEM;

	/* All MSIs are unmasked by default, Mask them all */
	mask = msi_mask(entry->msi_attrib.multi_cap);
	msi_mask_irq(entry, mask, mask);

	list_add_tail(&entry->list, &dev->msi_list);

	/* Configure MSI capability structure */
	ret = arch_setup_msi_irqs(dev, nvec, PCI_CAP_ID_MSI);
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
	msi_set_enable(dev, 1);
	dev->msi_enabled = 1;

	dev->irq = entry->irq;
	return 0;
}

static void __iomem *msix_map_region(struct pci_dev *dev, unsigned nr_entries)
{
	resource_size_t phys_addr;
	u32 table_offset;
	u8 bir;

	pci_read_config_dword(dev, dev->msix_cap + PCI_MSIX_TABLE,
			      &table_offset);
	bir = (u8)(table_offset & PCI_MSIX_TABLE_BIR);
	table_offset &= PCI_MSIX_TABLE_OFFSET;
	phys_addr = pci_resource_start(dev, bir) + table_offset;

	return ioremap_nocache(phys_addr, nr_entries * PCI_MSIX_ENTRY_SIZE);
}

static int msix_setup_entries(struct pci_dev *dev, void __iomem *base,
			      struct msix_entry *entries, int nvec)
{
	struct msi_desc *entry;
	int i;

	for (i = 0; i < nvec; i++) {
		entry = alloc_msi_entry(dev);
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
		entry->msi_attrib.entry_nr	= entries[i].entry;
		entry->msi_attrib.default_irq	= dev->irq;
		entry->msi_attrib.pos		= dev->msix_cap;
		entry->mask_base		= base;

		list_add_tail(&entry->list, &dev->msi_list);
	}

	return 0;
}

static void msix_program_entries(struct pci_dev *dev,
				 struct msix_entry *entries)
{
	struct msi_desc *entry;
	int i = 0;

	list_for_each_entry(entry, &dev->msi_list, list) {
		int offset = entries[i].entry * PCI_MSIX_ENTRY_SIZE +
						PCI_MSIX_ENTRY_VECTOR_CTRL;

		entries[i].vector = entry->irq;
		irq_set_msi_desc(entry->irq, entry);
		entry->masked = readl(entry->mask_base + offset);
		msix_mask_irq(entry, 1);
		i++;
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
	msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, 0);

	pci_read_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, &control);
	/* Request & Map MSI-X table region */
	base = msix_map_region(dev, msix_table_size(control));
	if (!base)
		return -ENOMEM;

	ret = msix_setup_entries(dev, base, entries, nvec);
	if (ret)
		return ret;

	ret = arch_setup_msi_irqs(dev, nvec, PCI_CAP_ID_MSIX);
	if (ret)
		goto out_avail;

	/*
	 * Some devices require MSI-X to be enabled before we can touch the
	 * MSI-X registers.  We need to mask all the vectors to prevent
	 * interrupts coming in before they're fully set up.
	 */
	msix_clear_and_set_ctrl(dev, 0,
				PCI_MSIX_FLAGS_MASKALL | PCI_MSIX_FLAGS_ENABLE);

	msix_program_entries(dev, entries);

	ret = populate_msi_sysfs(dev);
	if (ret)
		goto out_free;

	/* Set MSI-X enabled bits and unmask the function */
	pci_intx_for_msi(dev, 0);
	dev->msix_enabled = 1;

	msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL, 0);

	return 0;

out_avail:
	if (ret < 0) {
		/*
		 * If we had some success, report the number of irqs
		 * we succeeded in setting up.
		 */
		struct msi_desc *entry;
		int avail = 0;

		list_for_each_entry(entry, &dev->msi_list, list) {
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
 * pci_msi_check_device - check whether MSI may be enabled on a device
 * @dev: pointer to the pci_dev data structure of MSI device function
 * @nvec: how many MSIs have been requested ?
 * @type: are we checking for MSI or MSI-X ?
 *
 * Look at global flags, the device itself, and its parent buses
 * to determine if MSI/-X are supported for the device. If MSI/-X is
 * supported return 0, else return an error code.
 **/
static int pci_msi_check_device(struct pci_dev *dev, int nvec, int type)
{
	struct pci_bus *bus;
	int ret;

	/* MSI must be globally enabled and supported by the device */
	if (!pci_msi_enable || !dev || dev->no_msi)
		return -EINVAL;

	/*
	 * You can't ask to have 0 or less MSIs configured.
	 *  a) it's stupid ..
	 *  b) the list manipulation code assumes nvec >= 1.
	 */
	if (nvec < 1)
		return -ERANGE;

	/*
	 * Any bridge which does NOT route MSI transactions from its
	 * secondary bus to its primary bus must set NO_MSI flag on
	 * the secondary pci_bus.
	 * We expect only arch-specific PCI host bus controller driver
	 * or quirks for specific PCI bridges to be setting NO_MSI.
	 */
	for (bus = dev->bus; bus; bus = bus->parent)
		if (bus->bus_flags & PCI_BUS_FLAGS_NO_MSI)
			return -EINVAL;

	ret = arch_msi_check_device(dev, nvec, type);
	if (ret)
		return ret;

	return 0;
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

	BUG_ON(list_empty(&dev->msi_list));
	desc = list_first_entry(&dev->msi_list, struct msi_desc, list);

	msi_set_enable(dev, 0);
	pci_intx_for_msi(dev, 1);
	dev->msi_enabled = 0;

	/* Return the device with MSI unmasked as initial states */
	mask = msi_mask(desc->msi_attrib.multi_cap);
	/* Keep cached state to be restored */
	arch_msi_mask_irq(desc, mask, ~mask);

	/* Restore dev->irq to its default pin-assertion irq */
	dev->irq = desc->msi_attrib.default_irq;
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
 * @entries: pointer to an array of MSI-X entries
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
	int status, nr_entries;
	int i, j;

	if (!entries || !dev->msix_cap || dev->current_state != PCI_D0)
		return -EINVAL;

	status = pci_msi_check_device(dev, nvec, PCI_CAP_ID_MSIX);
	if (status)
		return status;

	nr_entries = pci_msix_vec_count(dev);
	if (nr_entries < 0)
		return nr_entries;
	if (nvec > nr_entries)
		return nr_entries;

	/* Check for any invalid entries */
	for (i = 0; i < nvec; i++) {
		if (entries[i].entry >= nr_entries)
			return -EINVAL;		/* invalid entry */
		for (j = i + 1; j < nvec; j++) {
			if (entries[i].entry == entries[j].entry)
				return -EINVAL;	/* duplicate entry */
		}
	}
	WARN_ON(!!dev->msix_enabled);

	/* Check whether driver already requested for MSI irq */
	if (dev->msi_enabled) {
		dev_info(&dev->dev, "can't enable MSI-X (MSI IRQ already assigned)\n");
		return -EINVAL;
	}
	status = msix_capability_init(dev, entries, nvec);
	return status;
}
EXPORT_SYMBOL(pci_enable_msix);

void pci_msix_shutdown(struct pci_dev *dev)
{
	struct msi_desc *entry;

	if (!pci_msi_enable || !dev || !dev->msix_enabled)
		return;

	/* Return the device with MSI-X masked as initial states */
	list_for_each_entry(entry, &dev->msi_list, list) {
		/* Keep cached states to be restored */
		arch_msix_mask_irq(entry, 1);
	}

	msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, 0);
	pci_intx_for_msi(dev, 1);
	dev->msix_enabled = 0;
}

void pci_disable_msix(struct pci_dev *dev)
{
	if (!pci_msi_enable || !dev || !dev->msix_enabled)
		return;

	pci_msix_shutdown(dev);
	free_msi_irqs(dev);
}
EXPORT_SYMBOL(pci_disable_msix);

/**
 * msi_remove_pci_irq_vectors - reclaim MSI(X) irqs to unused state
 * @dev: pointer to the pci_dev data structure of MSI(X) device function
 *
 * Being called during hotplug remove, from which the device function
 * is hot-removed. All previous assigned MSI/MSI-X irqs, if
 * allocated for this device function, are reclaimed to unused state,
 * which may be used later on.
 **/
void msi_remove_pci_irq_vectors(struct pci_dev *dev)
{
	if (!pci_msi_enable || !dev)
		return;

	if (dev->msi_enabled || dev->msix_enabled)
		free_msi_irqs(dev);
}

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

void pci_msi_init_pci_dev(struct pci_dev *dev)
{
	INIT_LIST_HEAD(&dev->msi_list);

	/* Disable the msi hardware to avoid screaming interrupts
	 * during boot.  This is the power on reset default so
	 * usually this should be a noop.
	 */
	dev->msi_cap = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (dev->msi_cap)
		msi_set_enable(dev, 0);

	dev->msix_cap = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (dev->msix_cap)
		msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_ENABLE, 0);
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
	int nvec;
	int rc;

	if (dev->current_state != PCI_D0)
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
	else if (nvec < minvec)
		return -EINVAL;
	else if (nvec > maxvec)
		nvec = maxvec;

	do {
		rc = pci_msi_check_device(dev, nvec, PCI_CAP_ID_MSI);
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	do {
		rc = msi_capability_init(dev, nvec);
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	return nvec;
}
EXPORT_SYMBOL(pci_enable_msi_range);

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
	int nvec = maxvec;
	int rc;

	if (maxvec < minvec)
		return -ERANGE;

	do {
		rc = pci_enable_msix(dev, entries, nvec);
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	return nvec;
}
EXPORT_SYMBOL(pci_enable_msix_range);
