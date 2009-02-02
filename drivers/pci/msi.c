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
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/msi.h>
#include <linux/smp.h>

#include <asm/errno.h>
#include <asm/io.h>

#include "pci.h"
#include "msi.h"

static int pci_msi_enable = 1;

/* Arch hooks */

int __attribute__ ((weak))
arch_msi_check_device(struct pci_dev *dev, int nvec, int type)
{
	return 0;
}

int __attribute__ ((weak))
arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *entry)
{
	return 0;
}

int __attribute__ ((weak))
arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *entry;
	int ret;

	list_for_each_entry(entry, &dev->msi_list, list) {
		ret = arch_setup_msi_irq(dev, entry);
		if (ret)
			return ret;
	}

	return 0;
}

void __attribute__ ((weak)) arch_teardown_msi_irq(unsigned int irq)
{
	return;
}

void __attribute__ ((weak))
arch_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *entry;

	list_for_each_entry(entry, &dev->msi_list, list) {
		if (entry->irq != 0)
			arch_teardown_msi_irq(entry->irq);
	}
}

static void __msi_set_enable(struct pci_dev *dev, int pos, int enable)
{
	u16 control;

	if (pos) {
		pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &control);
		control &= ~PCI_MSI_FLAGS_ENABLE;
		if (enable)
			control |= PCI_MSI_FLAGS_ENABLE;
		pci_write_config_word(dev, pos + PCI_MSI_FLAGS, control);
	}
}

static void msi_set_enable(struct pci_dev *dev, int enable)
{
	__msi_set_enable(dev, pci_find_capability(dev, PCI_CAP_ID_MSI), enable);
}

static void msix_set_enable(struct pci_dev *dev, int enable)
{
	int pos;
	u16 control;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos) {
		pci_read_config_word(dev, pos + PCI_MSIX_FLAGS, &control);
		control &= ~PCI_MSIX_FLAGS_ENABLE;
		if (enable)
			control |= PCI_MSIX_FLAGS_ENABLE;
		pci_write_config_word(dev, pos + PCI_MSIX_FLAGS, control);
	}
}

static void msix_flush_writes(struct irq_desc *desc)
{
	struct msi_desc *entry;

	entry = get_irq_desc_msi(desc);
	BUG_ON(!entry || !entry->dev);
	switch (entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
		/* nothing to do */
		break;
	case PCI_CAP_ID_MSIX:
	{
		int offset = entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET;
		readl(entry->mask_base + offset);
		break;
	}
	default:
		BUG();
		break;
	}
}

/*
 * PCI 2.3 does not specify mask bits for each MSI interrupt.  Attempting to
 * mask all MSI interrupts by clearing the MSI enable bit does not work
 * reliably as devices without an INTx disable bit will then generate a
 * level IRQ which will never be cleared.
 *
 * Returns 1 if it succeeded in masking the interrupt and 0 if the device
 * doesn't support MSI masking.
 */
static int msi_set_mask_bits(struct irq_desc *desc, u32 mask, u32 flag)
{
	struct msi_desc *entry;

	entry = get_irq_desc_msi(desc);
	BUG_ON(!entry || !entry->dev);
	switch (entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
		if (entry->msi_attrib.maskbit) {
			int pos;
			u32 mask_bits;

			pos = (long)entry->mask_base;
			pci_read_config_dword(entry->dev, pos, &mask_bits);
			mask_bits &= ~(mask);
			mask_bits |= flag & mask;
			pci_write_config_dword(entry->dev, pos, mask_bits);
		} else {
			return 0;
		}
		break;
	case PCI_CAP_ID_MSIX:
	{
		int offset = entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET;
		writel(flag, entry->mask_base + offset);
		readl(entry->mask_base + offset);
		break;
	}
	default:
		BUG();
		break;
	}
	entry->msi_attrib.masked = !!flag;
	return 1;
}

void read_msi_msg_desc(struct irq_desc *desc, struct msi_msg *msg)
{
	struct msi_desc *entry = get_irq_desc_msi(desc);
	switch(entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
	{
		struct pci_dev *dev = entry->dev;
		int pos = entry->msi_attrib.pos;
		u16 data;

		pci_read_config_dword(dev, msi_lower_address_reg(pos),
					&msg->address_lo);
		if (entry->msi_attrib.is_64) {
			pci_read_config_dword(dev, msi_upper_address_reg(pos),
						&msg->address_hi);
			pci_read_config_word(dev, msi_data_reg(pos, 1), &data);
		} else {
			msg->address_hi = 0;
			pci_read_config_word(dev, msi_data_reg(pos, 0), &data);
		}
		msg->data = data;
		break;
	}
	case PCI_CAP_ID_MSIX:
	{
		void __iomem *base;
		base = entry->mask_base +
			entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE;

		msg->address_lo = readl(base + PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
		msg->address_hi = readl(base + PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
		msg->data = readl(base + PCI_MSIX_ENTRY_DATA_OFFSET);
 		break;
 	}
 	default:
		BUG();
	}
}

void read_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct irq_desc *desc = irq_to_desc(irq);

	read_msi_msg_desc(desc, msg);
}

void write_msi_msg_desc(struct irq_desc *desc, struct msi_msg *msg)
{
	struct msi_desc *entry = get_irq_desc_msi(desc);
	switch (entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
	{
		struct pci_dev *dev = entry->dev;
		int pos = entry->msi_attrib.pos;

		pci_write_config_dword(dev, msi_lower_address_reg(pos),
					msg->address_lo);
		if (entry->msi_attrib.is_64) {
			pci_write_config_dword(dev, msi_upper_address_reg(pos),
						msg->address_hi);
			pci_write_config_word(dev, msi_data_reg(pos, 1),
						msg->data);
		} else {
			pci_write_config_word(dev, msi_data_reg(pos, 0),
						msg->data);
		}
		break;
	}
	case PCI_CAP_ID_MSIX:
	{
		void __iomem *base;
		base = entry->mask_base +
			entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE;

		writel(msg->address_lo,
			base + PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
		writel(msg->address_hi,
			base + PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
		writel(msg->data, base + PCI_MSIX_ENTRY_DATA_OFFSET);
		break;
	}
	default:
		BUG();
	}
	entry->msg = *msg;
}

void write_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct irq_desc *desc = irq_to_desc(irq);

	write_msi_msg_desc(desc, msg);
}

void mask_msi_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	msi_set_mask_bits(desc, 1, 1);
	msix_flush_writes(desc);
}

void unmask_msi_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	msi_set_mask_bits(desc, 1, 0);
	msix_flush_writes(desc);
}

static int msi_free_irqs(struct pci_dev* dev);

static struct msi_desc* alloc_msi_entry(void)
{
	struct msi_desc *entry;

	entry = kzalloc(sizeof(struct msi_desc), GFP_KERNEL);
	if (!entry)
		return NULL;

	INIT_LIST_HEAD(&entry->list);
	entry->irq = 0;
	entry->dev = NULL;

	return entry;
}

static void pci_intx_for_msi(struct pci_dev *dev, int enable)
{
	if (!(dev->dev_flags & PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG))
		pci_intx(dev, enable);
}

static void __pci_restore_msi_state(struct pci_dev *dev)
{
	int pos;
	u16 control;
	struct msi_desc *entry;

	if (!dev->msi_enabled)
		return;

	entry = get_irq_msi(dev->irq);
	pos = entry->msi_attrib.pos;

	pci_intx_for_msi(dev, 0);
	msi_set_enable(dev, 0);
	write_msi_msg(dev->irq, &entry->msg);
	if (entry->msi_attrib.maskbit) {
		struct irq_desc *desc = irq_to_desc(dev->irq);
		msi_set_mask_bits(desc, entry->msi_attrib.maskbits_mask,
				  entry->msi_attrib.masked);
	}

	pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &control);
	control &= ~PCI_MSI_FLAGS_QSIZE;
	control |= PCI_MSI_FLAGS_ENABLE;
	pci_write_config_word(dev, pos + PCI_MSI_FLAGS, control);
}

static void __pci_restore_msix_state(struct pci_dev *dev)
{
	int pos;
	struct msi_desc *entry;
	u16 control;

	if (!dev->msix_enabled)
		return;

	/* route the table */
	pci_intx_for_msi(dev, 0);
	msix_set_enable(dev, 0);

	list_for_each_entry(entry, &dev->msi_list, list) {
		struct irq_desc *desc = irq_to_desc(entry->irq);
		write_msi_msg(entry->irq, &entry->msg);
		msi_set_mask_bits(desc, 1, entry->msi_attrib.masked);
	}

	BUG_ON(list_empty(&dev->msi_list));
	entry = list_entry(dev->msi_list.next, struct msi_desc, list);
	pos = entry->msi_attrib.pos;
	pci_read_config_word(dev, pos + PCI_MSIX_FLAGS, &control);
	control &= ~PCI_MSIX_FLAGS_MASKALL;
	control |= PCI_MSIX_FLAGS_ENABLE;
	pci_write_config_word(dev, pos + PCI_MSIX_FLAGS, control);
}

void pci_restore_msi_state(struct pci_dev *dev)
{
	__pci_restore_msi_state(dev);
	__pci_restore_msix_state(dev);
}
EXPORT_SYMBOL_GPL(pci_restore_msi_state);

/**
 * msi_capability_init - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 *
 * Setup the MSI capability structure of device function with a single
 * MSI irq, regardless of device function is capable of handling
 * multiple messages. A return of zero indicates the successful setup
 * of an entry zero with the new MSI irq or non-zero for otherwise.
 **/
static int msi_capability_init(struct pci_dev *dev)
{
	struct msi_desc *entry;
	int pos, ret;
	u16 control;

	msi_set_enable(dev, 0);	/* Ensure msi is disabled as I set it up */

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	pci_read_config_word(dev, msi_control_reg(pos), &control);
	/* MSI Entry Initialization */
	entry = alloc_msi_entry();
	if (!entry)
		return -ENOMEM;

	entry->msi_attrib.type = PCI_CAP_ID_MSI;
	entry->msi_attrib.is_64 = is_64bit_address(control);
	entry->msi_attrib.entry_nr = 0;
	entry->msi_attrib.maskbit = is_mask_bit_support(control);
	entry->msi_attrib.masked = 1;
	entry->msi_attrib.default_irq = dev->irq;	/* Save IOAPIC IRQ */
	entry->msi_attrib.pos = pos;
	entry->dev = dev;
	if (entry->msi_attrib.maskbit) {
		unsigned int base, maskbits, temp;

		base = msi_mask_bits_reg(pos, entry->msi_attrib.is_64);
		entry->mask_base = (void __iomem *)(long)base;

		/* All MSIs are unmasked by default, Mask them all */
		pci_read_config_dword(dev, base, &maskbits);
		temp = (1 << multi_msi_capable(control));
		temp = ((temp - 1) & ~temp);
		maskbits |= temp;
		pci_write_config_dword(dev, base, maskbits);
		entry->msi_attrib.maskbits_mask = temp;
	}
	list_add_tail(&entry->list, &dev->msi_list);

	/* Configure MSI capability structure */
	ret = arch_setup_msi_irqs(dev, 1, PCI_CAP_ID_MSI);
	if (ret) {
		msi_free_irqs(dev);
		return ret;
	}

	/* Set MSI enabled bits	 */
	pci_intx_for_msi(dev, 0);
	msi_set_enable(dev, 1);
	dev->msi_enabled = 1;

	dev->irq = entry->irq;
	return 0;
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
	struct msi_desc *entry;
	int pos, i, j, nr_entries, ret;
	unsigned long phys_addr;
	u32 table_offset;
 	u16 control;
	u8 bir;
	void __iomem *base;

	msix_set_enable(dev, 0);/* Ensure msix is disabled as I set it up */

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	/* Request & Map MSI-X table region */
 	pci_read_config_word(dev, msi_control_reg(pos), &control);
	nr_entries = multi_msix_capable(control);

 	pci_read_config_dword(dev, msix_table_offset_reg(pos), &table_offset);
	bir = (u8)(table_offset & PCI_MSIX_FLAGS_BIRMASK);
	table_offset &= ~PCI_MSIX_FLAGS_BIRMASK;
	phys_addr = pci_resource_start (dev, bir) + table_offset;
	base = ioremap_nocache(phys_addr, nr_entries * PCI_MSIX_ENTRY_SIZE);
	if (base == NULL)
		return -ENOMEM;

	/* MSI-X Table Initialization */
	for (i = 0; i < nvec; i++) {
		entry = alloc_msi_entry();
		if (!entry)
			break;

 		j = entries[i].entry;
		entry->msi_attrib.type = PCI_CAP_ID_MSIX;
		entry->msi_attrib.is_64 = 1;
		entry->msi_attrib.entry_nr = j;
		entry->msi_attrib.maskbit = 1;
		entry->msi_attrib.masked = 1;
		entry->msi_attrib.default_irq = dev->irq;
		entry->msi_attrib.pos = pos;
		entry->dev = dev;
		entry->mask_base = base;

		list_add_tail(&entry->list, &dev->msi_list);
	}

	ret = arch_setup_msi_irqs(dev, nvec, PCI_CAP_ID_MSIX);
	if (ret) {
		int avail = 0;
		list_for_each_entry(entry, &dev->msi_list, list) {
			if (entry->irq != 0) {
				avail++;
			}
		}

		msi_free_irqs(dev);

		/* If we had some success report the number of irqs
		 * we succeeded in setting up.
		 */
		if (avail == 0)
			avail = ret;
		return avail;
	}

	i = 0;
	list_for_each_entry(entry, &dev->msi_list, list) {
		entries[i].vector = entry->irq;
		set_irq_msi(entry->irq, entry);
		i++;
	}
	/* Set MSI-X enabled bits */
	pci_intx_for_msi(dev, 0);
	msix_set_enable(dev, 1);
	dev->msix_enabled = 1;

	return 0;
}

/**
 * pci_msi_check_device - check whether MSI may be enabled on a device
 * @dev: pointer to the pci_dev data structure of MSI device function
 * @nvec: how many MSIs have been requested ?
 * @type: are we checking for MSI or MSI-X ?
 *
 * Look at global flags, the device itself, and its parent busses
 * to determine if MSI/-X are supported for the device. If MSI/-X is
 * supported return 0, else return an error code.
 **/
static int pci_msi_check_device(struct pci_dev* dev, int nvec, int type)
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

	/* Any bridge which does NOT route MSI transactions from it's
	 * secondary bus to it's primary bus must set NO_MSI flag on
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

	if (!pci_find_capability(dev, type))
		return -EINVAL;

	return 0;
}

/**
 * pci_enable_msi - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 *
 * Setup the MSI capability structure of device function with
 * a single MSI irq upon its software driver call to request for
 * MSI mode enabled on its hardware device function. A return of zero
 * indicates the successful setup of an entry zero with the new MSI
 * irq or non-zero for otherwise.
 **/
int pci_enable_msi(struct pci_dev* dev)
{
	int status;

	status = pci_msi_check_device(dev, 1, PCI_CAP_ID_MSI);
	if (status)
		return status;

	WARN_ON(!!dev->msi_enabled);

	/* Check whether driver already requested for MSI-X irqs */
	if (dev->msix_enabled) {
		dev_info(&dev->dev, "can't enable MSI "
			 "(MSI-X already enabled)\n");
		return -EINVAL;
	}
	status = msi_capability_init(dev);
	return status;
}
EXPORT_SYMBOL(pci_enable_msi);

void pci_msi_shutdown(struct pci_dev* dev)
{
	struct msi_desc *entry;

	if (!pci_msi_enable || !dev || !dev->msi_enabled)
		return;

	msi_set_enable(dev, 0);
	pci_intx_for_msi(dev, 1);
	dev->msi_enabled = 0;

	BUG_ON(list_empty(&dev->msi_list));
	entry = list_entry(dev->msi_list.next, struct msi_desc, list);
	/* Return the the pci reset with msi irqs unmasked */
	if (entry->msi_attrib.maskbit) {
		u32 mask = entry->msi_attrib.maskbits_mask;
		struct irq_desc *desc = irq_to_desc(dev->irq);
		msi_set_mask_bits(desc, mask, ~mask);
	}
	if (!entry->dev || entry->msi_attrib.type != PCI_CAP_ID_MSI)
		return;

	/* Restore dev->irq to its default pin-assertion irq */
	dev->irq = entry->msi_attrib.default_irq;
}
void pci_disable_msi(struct pci_dev* dev)
{
	struct msi_desc *entry;

	if (!pci_msi_enable || !dev || !dev->msi_enabled)
		return;

	pci_msi_shutdown(dev);

	entry = list_entry(dev->msi_list.next, struct msi_desc, list);
	if (!entry->dev || entry->msi_attrib.type != PCI_CAP_ID_MSI)
		return;

	msi_free_irqs(dev);
}
EXPORT_SYMBOL(pci_disable_msi);

static int msi_free_irqs(struct pci_dev* dev)
{
	struct msi_desc *entry, *tmp;

	list_for_each_entry(entry, &dev->msi_list, list) {
		if (entry->irq)
			BUG_ON(irq_has_action(entry->irq));
	}

	arch_teardown_msi_irqs(dev);

	list_for_each_entry_safe(entry, tmp, &dev->msi_list, list) {
		if (entry->msi_attrib.type == PCI_CAP_ID_MSIX) {
			writel(1, entry->mask_base + entry->msi_attrib.entry_nr
				  * PCI_MSIX_ENTRY_SIZE
				  + PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET);

			if (list_is_last(&entry->list, &dev->msi_list))
				iounmap(entry->mask_base);
		}
		list_del(&entry->list);
		kfree(entry);
	}

	return 0;
}

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
 * of irqs available. Driver should use the returned value to re-send
 * its request.
 **/
int pci_enable_msix(struct pci_dev* dev, struct msix_entry *entries, int nvec)
{
	int status, pos, nr_entries;
	int i, j;
	u16 control;

	if (!entries)
 		return -EINVAL;

	status = pci_msi_check_device(dev, nvec, PCI_CAP_ID_MSIX);
	if (status)
		return status;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	pci_read_config_word(dev, msi_control_reg(pos), &control);
	nr_entries = multi_msix_capable(control);
	if (nvec > nr_entries)
		return -EINVAL;

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
		dev_info(&dev->dev, "can't enable MSI-X "
		       "(MSI IRQ already assigned)\n");
		return -EINVAL;
	}
	status = msix_capability_init(dev, entries, nvec);
	return status;
}
EXPORT_SYMBOL(pci_enable_msix);

static void msix_free_all_irqs(struct pci_dev *dev)
{
	msi_free_irqs(dev);
}

void pci_msix_shutdown(struct pci_dev* dev)
{
	if (!pci_msi_enable || !dev || !dev->msix_enabled)
		return;

	msix_set_enable(dev, 0);
	pci_intx_for_msi(dev, 1);
	dev->msix_enabled = 0;
}
void pci_disable_msix(struct pci_dev* dev)
{
	if (!pci_msi_enable || !dev || !dev->msix_enabled)
		return;

	pci_msix_shutdown(dev);

	msix_free_all_irqs(dev);
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
void msi_remove_pci_irq_vectors(struct pci_dev* dev)
{
	if (!pci_msi_enable || !dev)
 		return;

	if (dev->msi_enabled)
		msi_free_irqs(dev);

	if (dev->msix_enabled)
		msix_free_all_irqs(dev);
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
}
