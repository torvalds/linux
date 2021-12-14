// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Message Signaled Interrupt (MSI)
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 * Copyright (C) 2016 Christoph Hellwig.
 */
#include <linux/err.h>
#include <linux/export.h>
#include <linux/irq.h>

#include "../pci.h"
#include "msi.h"

static int pci_msi_enable = 1;
int pci_msi_ignore_mask;

static noinline void pci_msi_update_mask(struct msi_desc *desc, u32 clear, u32 set)
{
	raw_spinlock_t *lock = &to_pci_dev(desc->dev)->msi_lock;
	unsigned long flags;

	if (!desc->pci.msi_attrib.can_mask)
		return;

	raw_spin_lock_irqsave(lock, flags);
	desc->pci.msi_mask &= ~clear;
	desc->pci.msi_mask |= set;
	pci_write_config_dword(msi_desc_to_pci_dev(desc), desc->pci.mask_pos,
			       desc->pci.msi_mask);
	raw_spin_unlock_irqrestore(lock, flags);
}

static inline void pci_msi_mask(struct msi_desc *desc, u32 mask)
{
	pci_msi_update_mask(desc, 0, mask);
}

static inline void pci_msi_unmask(struct msi_desc *desc, u32 mask)
{
	pci_msi_update_mask(desc, mask, 0);
}

static inline void __iomem *pci_msix_desc_addr(struct msi_desc *desc)
{
	return desc->pci.mask_base + desc->pci.msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE;
}

/*
 * This internal function does not flush PCI writes to the device.  All
 * users must ensure that they read from the device before either assuming
 * that the device state is up to date, or returning out of this file.
 * It does not affect the msi_desc::msix_ctrl cache either. Use with care!
 */
static void pci_msix_write_vector_ctrl(struct msi_desc *desc, u32 ctrl)
{
	void __iomem *desc_addr = pci_msix_desc_addr(desc);

	if (desc->pci.msi_attrib.can_mask)
		writel(ctrl, desc_addr + PCI_MSIX_ENTRY_VECTOR_CTRL);
}

static inline void pci_msix_mask(struct msi_desc *desc)
{
	desc->pci.msix_ctrl |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
	pci_msix_write_vector_ctrl(desc, desc->pci.msix_ctrl);
	/* Flush write to device */
	readl(desc->pci.mask_base);
}

static inline void pci_msix_unmask(struct msi_desc *desc)
{
	desc->pci.msix_ctrl &= ~PCI_MSIX_ENTRY_CTRL_MASKBIT;
	pci_msix_write_vector_ctrl(desc, desc->pci.msix_ctrl);
}

static void __pci_msi_mask_desc(struct msi_desc *desc, u32 mask)
{
	if (desc->pci.msi_attrib.is_msix)
		pci_msix_mask(desc);
	else
		pci_msi_mask(desc, mask);
}

static void __pci_msi_unmask_desc(struct msi_desc *desc, u32 mask)
{
	if (desc->pci.msi_attrib.is_msix)
		pci_msix_unmask(desc);
	else
		pci_msi_unmask(desc, mask);
}

/**
 * pci_msi_mask_irq - Generic IRQ chip callback to mask PCI/MSI interrupts
 * @data:	pointer to irqdata associated to that interrupt
 */
void pci_msi_mask_irq(struct irq_data *data)
{
	struct msi_desc *desc = irq_data_get_msi_desc(data);

	__pci_msi_mask_desc(desc, BIT(data->irq - desc->irq));
}
EXPORT_SYMBOL_GPL(pci_msi_mask_irq);

/**
 * pci_msi_unmask_irq - Generic IRQ chip callback to unmask PCI/MSI interrupts
 * @data:	pointer to irqdata associated to that interrupt
 */
void pci_msi_unmask_irq(struct irq_data *data)
{
	struct msi_desc *desc = irq_data_get_msi_desc(data);

	__pci_msi_unmask_desc(desc, BIT(data->irq - desc->irq));
}
EXPORT_SYMBOL_GPL(pci_msi_unmask_irq);

void __pci_read_msi_msg(struct msi_desc *entry, struct msi_msg *msg)
{
	struct pci_dev *dev = msi_desc_to_pci_dev(entry);

	BUG_ON(dev->current_state != PCI_D0);

	if (entry->pci.msi_attrib.is_msix) {
		void __iomem *base = pci_msix_desc_addr(entry);

		if (WARN_ON_ONCE(entry->pci.msi_attrib.is_virtual))
			return;

		msg->address_lo = readl(base + PCI_MSIX_ENTRY_LOWER_ADDR);
		msg->address_hi = readl(base + PCI_MSIX_ENTRY_UPPER_ADDR);
		msg->data = readl(base + PCI_MSIX_ENTRY_DATA);
	} else {
		int pos = dev->msi_cap;
		u16 data;

		pci_read_config_dword(dev, pos + PCI_MSI_ADDRESS_LO,
				      &msg->address_lo);
		if (entry->pci.msi_attrib.is_64) {
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

	if (dev->current_state != PCI_D0 || pci_dev_is_disconnected(dev)) {
		/* Don't touch the hardware now */
	} else if (entry->pci.msi_attrib.is_msix) {
		void __iomem *base = pci_msix_desc_addr(entry);
		u32 ctrl = entry->pci.msix_ctrl;
		bool unmasked = !(ctrl & PCI_MSIX_ENTRY_CTRL_MASKBIT);

		if (entry->pci.msi_attrib.is_virtual)
			goto skip;

		/*
		 * The specification mandates that the entry is masked
		 * when the message is modified:
		 *
		 * "If software changes the Address or Data value of an
		 * entry while the entry is unmasked, the result is
		 * undefined."
		 */
		if (unmasked)
			pci_msix_write_vector_ctrl(entry, ctrl | PCI_MSIX_ENTRY_CTRL_MASKBIT);

		writel(msg->address_lo, base + PCI_MSIX_ENTRY_LOWER_ADDR);
		writel(msg->address_hi, base + PCI_MSIX_ENTRY_UPPER_ADDR);
		writel(msg->data, base + PCI_MSIX_ENTRY_DATA);

		if (unmasked)
			pci_msix_write_vector_ctrl(entry, ctrl);

		/* Ensure that the writes are visible in the device */
		readl(base + PCI_MSIX_ENTRY_DATA);
	} else {
		int pos = dev->msi_cap;
		u16 msgctl;

		pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &msgctl);
		msgctl &= ~PCI_MSI_FLAGS_QSIZE;
		msgctl |= entry->pci.msi_attrib.multiple << 4;
		pci_write_config_word(dev, pos + PCI_MSI_FLAGS, msgctl);

		pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_LO,
				       msg->address_lo);
		if (entry->pci.msi_attrib.is_64) {
			pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_HI,
					       msg->address_hi);
			pci_write_config_word(dev, pos + PCI_MSI_DATA_64,
					      msg->data);
		} else {
			pci_write_config_word(dev, pos + PCI_MSI_DATA_32,
					      msg->data);
		}
		/* Ensure that the writes are visible in the device */
		pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &msgctl);
	}

skip:
	entry->msg = *msg;

	if (entry->write_msi_msg)
		entry->write_msi_msg(entry, entry->write_msi_msg_data);

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
	int i;

	for_each_pci_msi_entry(entry, dev)
		if (entry->irq)
			for (i = 0; i < entry->nvec_used; i++)
				BUG_ON(irq_has_action(entry->irq + i));

	if (dev->msi_irq_groups) {
		msi_destroy_sysfs(&dev->dev, dev->msi_irq_groups);
		dev->msi_irq_groups = NULL;
	}

	pci_msi_teardown_msi_irqs(dev);

	list_for_each_entry_safe(entry, tmp, msi_list, list) {
		list_del(&entry->list);
		free_msi_entry(entry);
	}

	if (dev->msix_base) {
		iounmap(dev->msix_base);
		dev->msix_base = NULL;
	}
}

static void pci_intx_for_msi(struct pci_dev *dev, int enable)
{
	if (!(dev->dev_flags & PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG))
		pci_intx(dev, enable);
}

static void pci_msi_set_enable(struct pci_dev *dev, int enable)
{
	u16 control;

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &control);
	control &= ~PCI_MSI_FLAGS_ENABLE;
	if (enable)
		control |= PCI_MSI_FLAGS_ENABLE;
	pci_write_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, control);
}

/*
 * Architecture override returns true when the PCI MSI message should be
 * written by the generic restore function.
 */
bool __weak arch_restore_msi_irqs(struct pci_dev *dev)
{
	return true;
}

static void __pci_restore_msi_state(struct pci_dev *dev)
{
	struct msi_desc *entry;
	u16 control;

	if (!dev->msi_enabled)
		return;

	entry = irq_get_msi_desc(dev->irq);

	pci_intx_for_msi(dev, 0);
	pci_msi_set_enable(dev, 0);
	if (arch_restore_msi_irqs(dev))
		__pci_write_msi_msg(entry, &entry->msg);

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &control);
	pci_msi_update_mask(entry, 0, 0);
	control &= ~PCI_MSI_FLAGS_QSIZE;
	control |= (entry->pci.msi_attrib.multiple << 4) | PCI_MSI_FLAGS_ENABLE;
	pci_write_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, control);
}

static void pci_msix_clear_and_set_ctrl(struct pci_dev *dev, u16 clear, u16 set)
{
	u16 ctrl;

	pci_read_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, &ctrl);
	ctrl &= ~clear;
	ctrl |= set;
	pci_write_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, ctrl);
}

static void __pci_restore_msix_state(struct pci_dev *dev)
{
	struct msi_desc *entry;
	bool write_msg;

	if (!dev->msix_enabled)
		return;
	BUG_ON(list_empty(dev_to_msi_list(&dev->dev)));

	/* route the table */
	pci_intx_for_msi(dev, 0);
	pci_msix_clear_and_set_ctrl(dev, 0,
				PCI_MSIX_FLAGS_ENABLE | PCI_MSIX_FLAGS_MASKALL);

	write_msg = arch_restore_msi_irqs(dev);

	for_each_pci_msi_entry(entry, dev) {
		if (write_msg)
			__pci_write_msi_msg(entry, &entry->msg);
		pci_msix_write_vector_ctrl(entry, entry->pci.msix_ctrl);
	}

	pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL, 0);
}

void pci_restore_msi_state(struct pci_dev *dev)
{
	__pci_restore_msi_state(dev);
	__pci_restore_msix_state(dev);
}
EXPORT_SYMBOL_GPL(pci_restore_msi_state);

static struct msi_desc *
msi_setup_entry(struct pci_dev *dev, int nvec, struct irq_affinity *affd)
{
	struct irq_affinity_desc *masks = NULL;
	struct msi_desc *entry;
	u16 control;

	if (affd)
		masks = irq_create_affinity_masks(nvec, affd);

	/* MSI Entry Initialization */
	entry = alloc_msi_entry(&dev->dev, nvec, masks);
	if (!entry)
		goto out;

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &control);
	/* Lies, damned lies, and MSIs */
	if (dev->dev_flags & PCI_DEV_FLAGS_HAS_MSI_MASKING)
		control |= PCI_MSI_FLAGS_MASKBIT;

	entry->pci.msi_attrib.is_msix	= 0;
	entry->pci.msi_attrib.is_64		= !!(control & PCI_MSI_FLAGS_64BIT);
	entry->pci.msi_attrib.is_virtual    = 0;
	entry->pci.msi_attrib.entry_nr	= 0;
	entry->pci.msi_attrib.can_mask	= !pci_msi_ignore_mask &&
					  !!(control & PCI_MSI_FLAGS_MASKBIT);
	entry->pci.msi_attrib.default_irq	= dev->irq;	/* Save IOAPIC IRQ */
	entry->pci.msi_attrib.multi_cap	= (control & PCI_MSI_FLAGS_QMASK) >> 1;
	entry->pci.msi_attrib.multiple	= ilog2(__roundup_pow_of_two(nvec));

	if (control & PCI_MSI_FLAGS_64BIT)
		entry->pci.mask_pos = dev->msi_cap + PCI_MSI_MASK_64;
	else
		entry->pci.mask_pos = dev->msi_cap + PCI_MSI_MASK_32;

	/* Save the initial mask status */
	if (entry->pci.msi_attrib.can_mask)
		pci_read_config_dword(dev, entry->pci.mask_pos, &entry->pci.msi_mask);

out:
	kfree(masks);
	return entry;
}

static int msi_verify_entries(struct pci_dev *dev)
{
	struct msi_desc *entry;

	if (!dev->no_64bit_msi)
		return 0;

	for_each_pci_msi_entry(entry, dev) {
		if (entry->msg.address_hi) {
			pci_err(dev, "arch assigned 64-bit MSI address %#x%08x but device only supports 32 bits\n",
				entry->msg.address_hi, entry->msg.address_lo);
			return -EIO;
		}
	}
	return 0;
}

/**
 * msi_capability_init - configure device's MSI capability structure
 * @dev: pointer to the pci_dev data structure of MSI device function
 * @nvec: number of interrupts to allocate
 * @affd: description of automatic IRQ affinity assignments (may be %NULL)
 *
 * Setup the MSI capability structure of the device with the requested
 * number of interrupts.  A return value of zero indicates the successful
 * setup of an entry with the new MSI IRQ.  A negative return value indicates
 * an error, and a positive return value indicates the number of interrupts
 * which could have been allocated.
 */
static int msi_capability_init(struct pci_dev *dev, int nvec,
			       struct irq_affinity *affd)
{
	const struct attribute_group **groups;
	struct msi_desc *entry;
	int ret;

	pci_msi_set_enable(dev, 0);	/* Disable MSI during set up */

	entry = msi_setup_entry(dev, nvec, affd);
	if (!entry)
		return -ENOMEM;

	/* All MSIs are unmasked by default; mask them all */
	pci_msi_mask(entry, msi_multi_mask(entry));

	list_add_tail(&entry->list, dev_to_msi_list(&dev->dev));

	/* Configure MSI capability structure */
	ret = pci_msi_setup_msi_irqs(dev, nvec, PCI_CAP_ID_MSI);
	if (ret)
		goto err;

	ret = msi_verify_entries(dev);
	if (ret)
		goto err;

	groups = msi_populate_sysfs(&dev->dev);
	if (IS_ERR(groups)) {
		ret = PTR_ERR(groups);
		goto err;
	}

	dev->msi_irq_groups = groups;

	/* Set MSI enabled bits	*/
	pci_intx_for_msi(dev, 0);
	pci_msi_set_enable(dev, 1);
	dev->msi_enabled = 1;

	pcibios_free_irq(dev);
	dev->irq = entry->irq;
	return 0;

err:
	pci_msi_unmask(entry, msi_multi_mask(entry));
	free_msi_irqs(dev);
	return ret;
}

static void __iomem *msix_map_region(struct pci_dev *dev,
				     unsigned int nr_entries)
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

	return ioremap(phys_addr, nr_entries * PCI_MSIX_ENTRY_SIZE);
}

static int msix_setup_entries(struct pci_dev *dev, void __iomem *base,
			      struct msix_entry *entries, int nvec,
			      struct irq_affinity *affd)
{
	struct irq_affinity_desc *curmsk, *masks = NULL;
	struct msi_desc *entry;
	void __iomem *addr;
	int ret, i;
	int vec_count = pci_msix_vec_count(dev);

	if (affd)
		masks = irq_create_affinity_masks(nvec, affd);

	for (i = 0, curmsk = masks; i < nvec; i++) {
		entry = alloc_msi_entry(&dev->dev, 1, curmsk);
		if (!entry) {
			/* No enough memory. Don't try again */
			ret = -ENOMEM;
			goto out;
		}

		entry->pci.msi_attrib.is_msix	= 1;
		entry->pci.msi_attrib.is_64	= 1;

		if (entries)
			entry->pci.msi_attrib.entry_nr = entries[i].entry;
		else
			entry->pci.msi_attrib.entry_nr = i;

		entry->pci.msi_attrib.is_virtual =
			entry->pci.msi_attrib.entry_nr >= vec_count;

		entry->pci.msi_attrib.can_mask	= !pci_msi_ignore_mask &&
						  !entry->pci.msi_attrib.is_virtual;

		entry->pci.msi_attrib.default_irq	= dev->irq;
		entry->pci.mask_base			= base;

		if (entry->pci.msi_attrib.can_mask) {
			addr = pci_msix_desc_addr(entry);
			entry->pci.msix_ctrl = readl(addr + PCI_MSIX_ENTRY_VECTOR_CTRL);
		}

		list_add_tail(&entry->list, dev_to_msi_list(&dev->dev));
		if (masks)
			curmsk++;
	}
	ret = 0;
out:
	kfree(masks);
	return ret;
}

static void msix_update_entries(struct pci_dev *dev, struct msix_entry *entries)
{
	struct msi_desc *entry;

	if (entries) {
		for_each_pci_msi_entry(entry, dev) {
			entries->vector = entry->irq;
			entries++;
		}
	}
}

static void msix_mask_all(void __iomem *base, int tsize)
{
	u32 ctrl = PCI_MSIX_ENTRY_CTRL_MASKBIT;
	int i;

	if (pci_msi_ignore_mask)
		return;

	for (i = 0; i < tsize; i++, base += PCI_MSIX_ENTRY_SIZE)
		writel(ctrl, base + PCI_MSIX_ENTRY_VECTOR_CTRL);
}

/**
 * msix_capability_init - configure device's MSI-X capability
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 * @entries: pointer to an array of struct msix_entry entries
 * @nvec: number of @entries
 * @affd: Optional pointer to enable automatic affinity assignment
 *
 * Setup the MSI-X capability structure of device function with a
 * single MSI-X IRQ. A return of zero indicates the successful setup of
 * requested MSI-X entries with allocated IRQs or non-zero for otherwise.
 **/
static int msix_capability_init(struct pci_dev *dev, struct msix_entry *entries,
				int nvec, struct irq_affinity *affd)
{
	const struct attribute_group **groups;
	void __iomem *base;
	int ret, tsize;
	u16 control;

	/*
	 * Some devices require MSI-X to be enabled before the MSI-X
	 * registers can be accessed.  Mask all the vectors to prevent
	 * interrupts coming in before they're fully set up.
	 */
	pci_msix_clear_and_set_ctrl(dev, 0, PCI_MSIX_FLAGS_MASKALL |
				    PCI_MSIX_FLAGS_ENABLE);

	pci_read_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, &control);
	/* Request & Map MSI-X table region */
	tsize = msix_table_size(control);
	base = msix_map_region(dev, tsize);
	if (!base) {
		ret = -ENOMEM;
		goto out_disable;
	}

	dev->msix_base = base;

	ret = msix_setup_entries(dev, base, entries, nvec, affd);
	if (ret)
		goto out_free;

	ret = pci_msi_setup_msi_irqs(dev, nvec, PCI_CAP_ID_MSIX);
	if (ret)
		goto out_free;

	/* Check if all MSI entries honor device restrictions */
	ret = msi_verify_entries(dev);
	if (ret)
		goto out_free;

	msix_update_entries(dev, entries);

	groups = msi_populate_sysfs(&dev->dev);
	if (IS_ERR(groups)) {
		ret = PTR_ERR(groups);
		goto out_free;
	}

	dev->msi_irq_groups = groups;

	/* Set MSI-X enabled bits and unmask the function */
	pci_intx_for_msi(dev, 0);
	dev->msix_enabled = 1;

	/*
	 * Ensure that all table entries are masked to prevent
	 * stale entries from firing in a crash kernel.
	 *
	 * Done late to deal with a broken Marvell NVME device
	 * which takes the MSI-X mask bits into account even
	 * when MSI-X is disabled, which prevents MSI delivery.
	 */
	msix_mask_all(base, tsize);
	pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL, 0);

	pcibios_free_irq(dev);
	return 0;

out_free:
	free_msi_irqs(dev);

out_disable:
	pci_msix_clear_and_set_ctrl(dev, PCI_MSIX_FLAGS_MASKALL | PCI_MSIX_FLAGS_ENABLE, 0);

	return ret;
}

/**
 * pci_msi_supported - check whether MSI may be enabled on a device
 * @dev: pointer to the pci_dev data structure of MSI device function
 * @nvec: how many MSIs have been requested?
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

	if (!dev || dev->no_msi)
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
	 *
	 * The NO_MSI flag can either be set directly by:
	 * - arch-specific PCI host bus controller drivers (deprecated)
	 * - quirks for specific PCI bridges
	 *
	 * or indirectly by platform-specific PCI host bridge drivers by
	 * advertising the 'msi_domain' property, which results in
	 * the NO_MSI flag when no MSI domain is found for this bridge
	 * at probe time.
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

static void pci_msi_shutdown(struct pci_dev *dev)
{
	struct msi_desc *desc;

	if (!pci_msi_enable || !dev || !dev->msi_enabled)
		return;

	BUG_ON(list_empty(dev_to_msi_list(&dev->dev)));
	desc = first_pci_msi_entry(dev);

	pci_msi_set_enable(dev, 0);
	pci_intx_for_msi(dev, 1);
	dev->msi_enabled = 0;

	/* Return the device with MSI unmasked as initial states */
	pci_msi_unmask(desc, msi_multi_mask(desc));

	/* Restore dev->irq to its default pin-assertion IRQ */
	dev->irq = desc->pci.msi_attrib.default_irq;
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

static int __pci_enable_msix(struct pci_dev *dev, struct msix_entry *entries,
			     int nvec, struct irq_affinity *affd, int flags)
{
	int nr_entries;
	int i, j;

	if (!pci_msi_supported(dev, nvec) || dev->current_state != PCI_D0)
		return -EINVAL;

	nr_entries = pci_msix_vec_count(dev);
	if (nr_entries < 0)
		return nr_entries;
	if (nvec > nr_entries && !(flags & PCI_IRQ_VIRTUAL))
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

	/* Check whether driver already requested for MSI IRQ */
	if (dev->msi_enabled) {
		pci_info(dev, "can't enable MSI-X (MSI IRQ already assigned)\n");
		return -EINVAL;
	}
	return msix_capability_init(dev, entries, nvec, affd);
}

static void pci_msix_shutdown(struct pci_dev *dev)
{
	struct msi_desc *entry;

	if (!pci_msi_enable || !dev || !dev->msix_enabled)
		return;

	if (pci_dev_is_disconnected(dev)) {
		dev->msix_enabled = 0;
		return;
	}

	/* Return the device with MSI-X masked as initial states */
	for_each_pci_msi_entry(entry, dev)
		pci_msix_mask(entry);

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

static int __pci_enable_msi_range(struct pci_dev *dev, int minvec, int maxvec,
				  struct irq_affinity *affd)
{
	int nvec;
	int rc;

	if (!pci_msi_supported(dev, minvec) || dev->current_state != PCI_D0)
		return -EINVAL;

	/* Check whether driver already requested MSI-X IRQs */
	if (dev->msix_enabled) {
		pci_info(dev, "can't enable MSI (MSI-X already enabled)\n");
		return -EINVAL;
	}

	if (maxvec < minvec)
		return -ERANGE;

	if (WARN_ON_ONCE(dev->msi_enabled))
		return -EINVAL;

	nvec = pci_msi_vec_count(dev);
	if (nvec < 0)
		return nvec;
	if (nvec < minvec)
		return -ENOSPC;

	if (nvec > maxvec)
		nvec = maxvec;

	for (;;) {
		if (affd) {
			nvec = irq_calc_affinity_vectors(minvec, nvec, affd);
			if (nvec < minvec)
				return -ENOSPC;
		}

		rc = msi_capability_init(dev, nvec, affd);
		if (rc == 0)
			return nvec;

		if (rc < 0)
			return rc;
		if (rc < minvec)
			return -ENOSPC;

		nvec = rc;
	}
}

/* deprecated, don't use */
int pci_enable_msi(struct pci_dev *dev)
{
	int rc = __pci_enable_msi_range(dev, 1, 1, NULL);
	if (rc < 0)
		return rc;
	return 0;
}
EXPORT_SYMBOL(pci_enable_msi);

static int __pci_enable_msix_range(struct pci_dev *dev,
				   struct msix_entry *entries, int minvec,
				   int maxvec, struct irq_affinity *affd,
				   int flags)
{
	int rc, nvec = maxvec;

	if (maxvec < minvec)
		return -ERANGE;

	if (WARN_ON_ONCE(dev->msix_enabled))
		return -EINVAL;

	for (;;) {
		if (affd) {
			nvec = irq_calc_affinity_vectors(minvec, nvec, affd);
			if (nvec < minvec)
				return -ENOSPC;
		}

		rc = __pci_enable_msix(dev, entries, nvec, affd, flags);
		if (rc == 0)
			return nvec;

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
 * @minvec: minimum number of MSI-X IRQs requested
 * @maxvec: maximum number of MSI-X IRQs requested
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
	return __pci_enable_msix_range(dev, entries, minvec, maxvec, NULL, 0);
}
EXPORT_SYMBOL(pci_enable_msix_range);

/**
 * pci_alloc_irq_vectors_affinity - allocate multiple IRQs for a device
 * @dev:		PCI device to operate on
 * @min_vecs:		minimum number of vectors required (must be >= 1)
 * @max_vecs:		maximum (desired) number of vectors
 * @flags:		flags or quirks for the allocation
 * @affd:		optional description of the affinity requirements
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
int pci_alloc_irq_vectors_affinity(struct pci_dev *dev, unsigned int min_vecs,
				   unsigned int max_vecs, unsigned int flags,
				   struct irq_affinity *affd)
{
	struct irq_affinity msi_default_affd = {0};
	int nvecs = -ENOSPC;

	if (flags & PCI_IRQ_AFFINITY) {
		if (!affd)
			affd = &msi_default_affd;
	} else {
		if (WARN_ON(affd))
			affd = NULL;
	}

	if (flags & PCI_IRQ_MSIX) {
		nvecs = __pci_enable_msix_range(dev, NULL, min_vecs, max_vecs,
						affd, flags);
		if (nvecs > 0)
			return nvecs;
	}

	if (flags & PCI_IRQ_MSI) {
		nvecs = __pci_enable_msi_range(dev, min_vecs, max_vecs, affd);
		if (nvecs > 0)
			return nvecs;
	}

	/* use legacy IRQ if allowed */
	if (flags & PCI_IRQ_LEGACY) {
		if (min_vecs == 1 && dev->irq) {
			/*
			 * Invoke the affinity spreading logic to ensure that
			 * the device driver can adjust queue configuration
			 * for the single interrupt case.
			 */
			if (affd)
				irq_create_affinity_masks(1, affd);
			pci_intx(dev, 1);
			return 1;
		}
	}

	return nvecs;
}
EXPORT_SYMBOL(pci_alloc_irq_vectors_affinity);

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
 * @dev:	PCI device to operate on
 * @nr:		Interrupt vector index (0-based)
 *
 * @nr has the following meanings depending on the interrupt mode:
 *   MSI-X:	The index in the MSI-X vector table
 *   MSI:	The index of the enabled MSI vectors
 *   INTx:	Must be 0
 *
 * Return: The Linux interrupt number or -EINVAl if @nr is out of range.
 */
int pci_irq_vector(struct pci_dev *dev, unsigned int nr)
{
	if (dev->msix_enabled) {
		struct msi_desc *entry;

		for_each_pci_msi_entry(entry, dev) {
			if (entry->pci.msi_attrib.entry_nr == nr)
				return entry->irq;
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

/**
 * pci_irq_get_affinity - return the affinity of a particular MSI vector
 * @dev:	PCI device to operate on
 * @nr:		device-relative interrupt vector index (0-based).
 *
 * @nr has the following meanings depending on the interrupt mode:
 *   MSI-X:	The index in the MSI-X vector table
 *   MSI:	The index of the enabled MSI vectors
 *   INTx:	Must be 0
 *
 * Return: A cpumask pointer or NULL if @nr is out of range
 */
const struct cpumask *pci_irq_get_affinity(struct pci_dev *dev, int nr)
{
	if (dev->msix_enabled) {
		struct msi_desc *entry;

		for_each_pci_msi_entry(entry, dev) {
			if (entry->pci.msi_attrib.entry_nr == nr)
				return &entry->affinity->mask;
		}
		WARN_ON_ONCE(1);
		return NULL;
	} else if (dev->msi_enabled) {
		struct msi_desc *entry = first_pci_msi_entry(dev);

		if (WARN_ON_ONCE(!entry || !entry->affinity ||
				 nr >= entry->nvec_used))
			return NULL;

		return &entry->affinity[nr].mask;
	} else {
		return cpu_possible_mask;
	}
}
EXPORT_SYMBOL(pci_irq_get_affinity);

struct pci_dev *msi_desc_to_pci_dev(struct msi_desc *desc)
{
	return to_pci_dev(desc->dev);
}
EXPORT_SYMBOL(msi_desc_to_pci_dev);

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
