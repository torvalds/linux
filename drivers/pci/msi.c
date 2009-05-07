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

#ifndef arch_msi_check_device
int arch_msi_check_device(struct pci_dev *dev, int nvec, int type)
{
	return 0;
}
#endif

#ifndef arch_setup_msi_irqs
int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
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
#endif

#ifndef arch_teardown_msi_irqs
void arch_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *entry;

	list_for_each_entry(entry, &dev->msi_list, list) {
		int i, nvec;
		if (entry->irq == 0)
			continue;
		nvec = 1 << entry->msi_attrib.multiple;
		for (i = 0; i < nvec; i++)
			arch_teardown_msi_irq(entry->irq + i);
	}
}
#endif

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

static inline __attribute_const__ u32 msi_mask(unsigned x)
{
	/* Don't shift by >= width of type */
	if (x >= 5)
		return 0xffffffff;
	return (1 << (1 << x)) - 1;
}

static inline __attribute_const__ u32 msi_capable_mask(u16 control)
{
	return msi_mask((control >> 1) & 7);
}

static inline __attribute_const__ u32 msi_enabled_mask(u16 control)
{
	return msi_mask((control >> 4) & 7);
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
static void msi_mask_irq(struct msi_desc *desc, u32 mask, u32 flag)
{
	u32 mask_bits = desc->masked;

	if (!desc->msi_attrib.maskbit)
		return;

	mask_bits &= ~mask;
	mask_bits |= flag;
	pci_write_config_dword(desc->dev, desc->mask_pos, mask_bits);
	desc->masked = mask_bits;
}

/*
 * This internal function does not flush PCI writes to the device.
 * All users must ensure that they read from the device before either
 * assuming that the device state is up to date, or returning out of this
 * file.  This saves a few milliseconds when initialising devices with lots
 * of MSI-X interrupts.
 */
static void msix_mask_irq(struct msi_desc *desc, u32 flag)
{
	u32 mask_bits = desc->masked;
	unsigned offset = desc->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
					PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET;
	mask_bits &= ~1;
	mask_bits |= flag;
	writel(mask_bits, desc->mask_base + offset);
	desc->masked = mask_bits;
}

static void msi_set_mask_bit(unsigned irq, u32 flag)
{
	struct msi_desc *desc = get_irq_msi(irq);

	if (desc->msi_attrib.is_msix) {
		msix_mask_irq(desc, flag);
		readl(desc->mask_base);		/* Flush write to device */
	} else {
		unsigned offset = irq - desc->dev->irq;
		msi_mask_irq(desc, 1 << offset, flag << offset);
	}
}

void mask_msi_irq(unsigned int irq)
{
	msi_set_mask_bit(irq, 1);
}

void unmask_msi_irq(unsigned int irq)
{
	msi_set_mask_bit(irq, 0);
}

void read_msi_msg_desc(struct irq_desc *desc, struct msi_msg *msg)
{
	struct msi_desc *entry = get_irq_desc_msi(desc);
	if (entry->msi_attrib.is_msix) {
		void __iomem *base = entry->mask_base +
			entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE;

		msg->address_lo = readl(base + PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
		msg->address_hi = readl(base + PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
		msg->data = readl(base + PCI_MSIX_ENTRY_DATA_OFFSET);
	} else {
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
	if (entry->msi_attrib.is_msix) {
		void __iomem *base;
		base = entry->mask_base +
			entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE;

		writel(msg->address_lo,
			base + PCI_MSIX_ENTRY_LOWER_ADDR_OFFSET);
		writel(msg->address_hi,
			base + PCI_MSIX_ENTRY_UPPER_ADDR_OFFSET);
		writel(msg->data, base + PCI_MSIX_ENTRY_DATA_OFFSET);
	} else {
		struct pci_dev *dev = entry->dev;
		int pos = entry->msi_attrib.pos;
		u16 msgctl;

		pci_read_config_word(dev, msi_control_reg(pos), &msgctl);
		msgctl &= ~PCI_MSI_FLAGS_QSIZE;
		msgctl |= entry->msi_attrib.multiple << 4;
		pci_write_config_word(dev, msi_control_reg(pos), msgctl);

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
	}
	entry->msg = *msg;
}

void write_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct irq_desc *desc = irq_to_desc(irq);

	write_msi_msg_desc(desc, msg);
}

static int msi_free_irqs(struct pci_dev* dev);

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

	pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &control);
	msi_mask_irq(entry, msi_capable_mask(control), entry->masked);
	control &= ~PCI_MSI_FLAGS_QSIZE;
	control |= (entry->msi_attrib.multiple << 4) | PCI_MSI_FLAGS_ENABLE;
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
		write_msi_msg(entry->irq, &entry->msg);
		msix_mask_irq(entry, entry->masked);
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
	int pos, ret;
	u16 control;
	unsigned mask;

	msi_set_enable(dev, 0);	/* Ensure msi is disabled as I set it up */

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	pci_read_config_word(dev, msi_control_reg(pos), &control);
	/* MSI Entry Initialization */
	entry = alloc_msi_entry(dev);
	if (!entry)
		return -ENOMEM;

	entry->msi_attrib.is_msix = 0;
	entry->msi_attrib.is_64 = is_64bit_address(control);
	entry->msi_attrib.entry_nr = 0;
	entry->msi_attrib.maskbit = is_mask_bit_support(control);
	entry->msi_attrib.default_irq = dev->irq;	/* Save IOAPIC IRQ */
	entry->msi_attrib.pos = pos;

	entry->mask_pos = msi_mask_reg(pos, entry->msi_attrib.is_64);
	/* All MSIs are unmasked by default, Mask them all */
	if (entry->msi_attrib.maskbit)
		pci_read_config_dword(dev, entry->mask_pos, &entry->masked);
	mask = msi_capable_mask(control);
	msi_mask_irq(entry, mask, mask);

	list_add_tail(&entry->list, &dev->msi_list);

	/* Configure MSI capability structure */
	ret = arch_setup_msi_irqs(dev, nvec, PCI_CAP_ID_MSI);
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
		entry = alloc_msi_entry(dev);
		if (!entry)
			break;

 		j = entries[i].entry;
		entry->msi_attrib.is_msix = 1;
		entry->msi_attrib.is_64 = 1;
		entry->msi_attrib.entry_nr = j;
		entry->msi_attrib.default_irq = dev->irq;
		entry->msi_attrib.pos = pos;
		entry->mask_base = base;
		msix_mask_irq(entry, 1);

		list_add_tail(&entry->list, &dev->msi_list);
	}

	ret = arch_setup_msi_irqs(dev, nvec, PCI_CAP_ID_MSIX);
	if (ret < 0) {
		/* If we had some success report the number of irqs
		 * we succeeded in setting up. */
		int avail = 0;
		list_for_each_entry(entry, &dev->msi_list, list) {
			if (entry->irq != 0) {
				avail++;
			}
		}

		if (avail != 0)
			ret = avail;
	}

	if (ret) {
		msi_free_irqs(dev);
		return ret;
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

	list_for_each_entry(entry, &dev->msi_list, list) {
		int vector = entry->msi_attrib.entry_nr;
		entry->masked = readl(base + vector * PCI_MSIX_ENTRY_SIZE +
					PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET);
	}

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
 * pci_enable_msi_block - configure device's MSI capability structure
 * @dev: device to configure
 * @nvec: number of interrupts to configure
 *
 * Allocate IRQs for a device with the MSI capability.
 * This function returns a negative errno if an error occurs.  If it
 * is unable to allocate the number of interrupts requested, it returns
 * the number of interrupts it might be able to allocate.  If it successfully
 * allocates at least the number of interrupts requested, it returns 0 and
 * updates the @dev's irq member to the lowest new interrupt number; the
 * other interrupt numbers allocated to this device are consecutive.
 */
int pci_enable_msi_block(struct pci_dev *dev, unsigned int nvec)
{
	int status, pos, maxvec;
	u16 msgctl;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!pos)
		return -EINVAL;
	pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &msgctl);
	maxvec = 1 << ((msgctl & PCI_MSI_FLAGS_QMASK) >> 1);
	if (nvec > maxvec)
		return maxvec;

	status = pci_msi_check_device(dev, nvec, PCI_CAP_ID_MSI);
	if (status)
		return status;

	WARN_ON(!!dev->msi_enabled);

	/* Check whether driver already requested MSI-X irqs */
	if (dev->msix_enabled) {
		dev_info(&dev->dev, "can't enable MSI "
			 "(MSI-X already enabled)\n");
		return -EINVAL;
	}

	status = msi_capability_init(dev, nvec);
	return status;
}
EXPORT_SYMBOL(pci_enable_msi_block);

void pci_msi_shutdown(struct pci_dev *dev)
{
	struct msi_desc *desc;
	u32 mask;
	u16 ctrl;

	if (!pci_msi_enable || !dev || !dev->msi_enabled)
		return;

	msi_set_enable(dev, 0);
	pci_intx_for_msi(dev, 1);
	dev->msi_enabled = 0;

	BUG_ON(list_empty(&dev->msi_list));
	desc = list_first_entry(&dev->msi_list, struct msi_desc, list);
	pci_read_config_word(dev, desc->msi_attrib.pos + PCI_MSI_FLAGS, &ctrl);
	mask = msi_capable_mask(ctrl);
	msi_mask_irq(desc, mask, ~mask);

	/* Restore dev->irq to its default pin-assertion irq */
	dev->irq = desc->msi_attrib.default_irq;
}

void pci_disable_msi(struct pci_dev* dev)
{
	struct msi_desc *entry;

	if (!pci_msi_enable || !dev || !dev->msi_enabled)
		return;

	pci_msi_shutdown(dev);

	entry = list_entry(dev->msi_list.next, struct msi_desc, list);
	if (entry->msi_attrib.is_msix)
		return;

	msi_free_irqs(dev);
}
EXPORT_SYMBOL(pci_disable_msi);

static int msi_free_irqs(struct pci_dev* dev)
{
	struct msi_desc *entry, *tmp;

	list_for_each_entry(entry, &dev->msi_list, list) {
		int i, nvec;
		if (!entry->irq)
			continue;
		nvec = 1 << entry->msi_attrib.multiple;
		for (i = 0; i < nvec; i++)
			BUG_ON(irq_has_action(entry->irq + i));
	}

	arch_teardown_msi_irqs(dev);

	list_for_each_entry_safe(entry, tmp, &dev->msi_list, list) {
		if (entry->msi_attrib.is_msix) {
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
 * pci_msix_table_size - return the number of device's MSI-X table entries
 * @dev: pointer to the pci_dev data structure of MSI-X device function
 */
int pci_msix_table_size(struct pci_dev *dev)
{
	int pos;
	u16 control;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!pos)
		return 0;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	return multi_msix_capable(control);
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
 * of irqs or MSI-X vectors available. Driver should use the returned value to
 * re-send its request.
 **/
int pci_enable_msix(struct pci_dev* dev, struct msix_entry *entries, int nvec)
{
	int status, nr_entries;
	int i, j;

	if (!entries)
 		return -EINVAL;

	status = pci_msi_check_device(dev, nvec, PCI_CAP_ID_MSIX);
	if (status)
		return status;

	nr_entries = pci_msix_table_size(dev);
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
