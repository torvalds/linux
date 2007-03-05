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
#include <linux/smp_lock.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/msi.h>

#include <asm/errno.h>
#include <asm/io.h>
#include <asm/smp.h>

#include "pci.h"
#include "msi.h"

static struct kmem_cache* msi_cachep;

static int pci_msi_enable = 1;

static int msi_cache_init(void)
{
	msi_cachep = kmem_cache_create("msi_cache", sizeof(struct msi_desc),
					0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!msi_cachep)
		return -ENOMEM;

	return 0;
}

static void msi_set_enable(struct pci_dev *dev, int enable)
{
	int pos;
	u16 control;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (pos) {
		pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &control);
		control &= ~PCI_MSI_FLAGS_ENABLE;
		if (enable)
			control |= PCI_MSI_FLAGS_ENABLE;
		pci_write_config_word(dev, pos + PCI_MSI_FLAGS, control);
	}
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

static void msi_set_mask_bit(unsigned int irq, int flag)
{
	struct msi_desc *entry;

	entry = get_irq_msi(irq);
	BUG_ON(!entry || !entry->dev);
	switch (entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
		if (entry->msi_attrib.maskbit) {
			int pos;
			u32 mask_bits;

			pos = (long)entry->mask_base;
			pci_read_config_dword(entry->dev, pos, &mask_bits);
			mask_bits &= ~(1);
			mask_bits |= flag;
			pci_write_config_dword(entry->dev, pos, mask_bits);
		} else {
			msi_set_enable(entry->dev, !flag);
		}
		break;
	case PCI_CAP_ID_MSIX:
	{
		int offset = entry->msi_attrib.entry_nr * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET;
		writel(flag, entry->mask_base + offset);
		break;
	}
	default:
		BUG();
		break;
	}
}

void read_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct msi_desc *entry = get_irq_msi(irq);
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
			pci_read_config_word(dev, msi_data_reg(pos, 1), &data);
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

void write_msi_msg(unsigned int irq, struct msi_msg *msg)
{
	struct msi_desc *entry = get_irq_msi(irq);
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
}

void mask_msi_irq(unsigned int irq)
{
	msi_set_mask_bit(irq, 1);
}

void unmask_msi_irq(unsigned int irq)
{
	msi_set_mask_bit(irq, 0);
}

static int msi_free_irq(struct pci_dev* dev, int irq);

static int msi_init(void)
{
	static int status = -ENOMEM;

	if (!status)
		return status;

	status = msi_cache_init();
	if (status < 0) {
		pci_msi_enable = 0;
		printk(KERN_WARNING "PCI: MSI cache init failed\n");
		return status;
	}

	return status;
}

static struct msi_desc* alloc_msi_entry(void)
{
	struct msi_desc *entry;

	entry = kmem_cache_zalloc(msi_cachep, GFP_KERNEL);
	if (!entry)
		return NULL;

	entry->link.tail = entry->link.head = 0;	/* single message */
	entry->dev = NULL;

	return entry;
}

#ifdef CONFIG_PM
static int __pci_save_msi_state(struct pci_dev *dev)
{
	int pos, i = 0;
	u16 control;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	if (!dev->msi_enabled)
		return 0;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (pos <= 0)
		return 0;

	save_state = kzalloc(sizeof(struct pci_cap_saved_state) + sizeof(u32) * 5,
		GFP_KERNEL);
	if (!save_state) {
		printk(KERN_ERR "Out of memory in pci_save_msi_state\n");
		return -ENOMEM;
	}
	cap = &save_state->data[0];

	pci_read_config_dword(dev, pos, &cap[i++]);
	control = cap[0] >> 16;
	pci_read_config_dword(dev, pos + PCI_MSI_ADDRESS_LO, &cap[i++]);
	if (control & PCI_MSI_FLAGS_64BIT) {
		pci_read_config_dword(dev, pos + PCI_MSI_ADDRESS_HI, &cap[i++]);
		pci_read_config_dword(dev, pos + PCI_MSI_DATA_64, &cap[i++]);
	} else
		pci_read_config_dword(dev, pos + PCI_MSI_DATA_32, &cap[i++]);
	if (control & PCI_MSI_FLAGS_MASKBIT)
		pci_read_config_dword(dev, pos + PCI_MSI_MASK_BIT, &cap[i++]);
	save_state->cap_nr = PCI_CAP_ID_MSI;
	pci_add_saved_cap(dev, save_state);
	return 0;
}

static void __pci_restore_msi_state(struct pci_dev *dev)
{
	int i = 0, pos;
	u16 control;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	if (!dev->msi_enabled)
		return;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_MSI);
	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!save_state || pos <= 0)
		return;
	cap = &save_state->data[0];

	pci_intx(dev, 0);		/* disable intx */
	control = cap[i++] >> 16;
	msi_set_enable(dev, 0);
	pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_LO, cap[i++]);
	if (control & PCI_MSI_FLAGS_64BIT) {
		pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_HI, cap[i++]);
		pci_write_config_dword(dev, pos + PCI_MSI_DATA_64, cap[i++]);
	} else
		pci_write_config_dword(dev, pos + PCI_MSI_DATA_32, cap[i++]);
	if (control & PCI_MSI_FLAGS_MASKBIT)
		pci_write_config_dword(dev, pos + PCI_MSI_MASK_BIT, cap[i++]);
	pci_write_config_word(dev, pos + PCI_MSI_FLAGS, control);
	pci_remove_saved_cap(save_state);
	kfree(save_state);
}

static int __pci_save_msix_state(struct pci_dev *dev)
{
	int pos;
	int irq, head, tail = 0;
	u16 control;
	struct pci_cap_saved_state *save_state;

	if (!dev->msix_enabled)
		return 0;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos <= 0)
		return 0;

	/* save the capability */
	pci_read_config_word(dev, msi_control_reg(pos), &control);
	save_state = kzalloc(sizeof(struct pci_cap_saved_state) + sizeof(u16),
		GFP_KERNEL);
	if (!save_state) {
		printk(KERN_ERR "Out of memory in pci_save_msix_state\n");
		return -ENOMEM;
	}
	*((u16 *)&save_state->data[0]) = control;

	/* save the table */
	irq = head = dev->first_msi_irq;
	while (head != tail) {
		struct msi_desc *entry;

		entry = get_irq_msi(irq);
		read_msi_msg(irq, &entry->msg_save);

		tail = entry->link.tail;
		irq = tail;
	}

	save_state->cap_nr = PCI_CAP_ID_MSIX;
	pci_add_saved_cap(dev, save_state);
	return 0;
}

int pci_save_msi_state(struct pci_dev *dev)
{
	int rc;

	rc = __pci_save_msi_state(dev);
	if (rc)
		return rc;

	rc = __pci_save_msix_state(dev);

	return rc;
}

static void __pci_restore_msix_state(struct pci_dev *dev)
{
	u16 save;
	int pos;
	int irq, head, tail = 0;
	struct msi_desc *entry;
	struct pci_cap_saved_state *save_state;

	if (!dev->msix_enabled)
		return;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_MSIX);
	if (!save_state)
		return;
	save = *((u16 *)&save_state->data[0]);
	pci_remove_saved_cap(save_state);
	kfree(save_state);

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos <= 0)
		return;

	/* route the table */
	pci_intx(dev, 0);		/* disable intx */
	msix_set_enable(dev, 0);
	irq = head = dev->first_msi_irq;
	while (head != tail) {
		entry = get_irq_msi(irq);
		write_msi_msg(irq, &entry->msg_save);

		tail = entry->link.tail;
		irq = tail;
	}

	pci_write_config_word(dev, msi_control_reg(pos), save);
}

void pci_restore_msi_state(struct pci_dev *dev)
{
	__pci_restore_msi_state(dev);
	__pci_restore_msix_state(dev);
}
#endif	/* CONFIG_PM */

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
	int pos, irq;
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
	entry->msi_attrib.default_irq = dev->irq;	/* Save IOAPIC IRQ */
	entry->msi_attrib.pos = pos;
	if (is_mask_bit_support(control)) {
		entry->mask_base = (void __iomem *)(long)msi_mask_bits_reg(pos,
				is_64bit_address(control));
	}
	entry->dev = dev;
	if (entry->msi_attrib.maskbit) {
		unsigned int maskbits, temp;
		/* All MSIs are unmasked by default, Mask them all */
		pci_read_config_dword(dev,
			msi_mask_bits_reg(pos, is_64bit_address(control)),
			&maskbits);
		temp = (1 << multi_msi_capable(control));
		temp = ((temp - 1) & ~temp);
		maskbits |= temp;
		pci_write_config_dword(dev,
			msi_mask_bits_reg(pos, is_64bit_address(control)),
			maskbits);
	}
	/* Configure MSI capability structure */
	irq = arch_setup_msi_irq(dev, entry);
	if (irq < 0) {
		kmem_cache_free(msi_cachep, entry);
		return irq;
	}
	entry->link.head = irq;
	entry->link.tail = irq;
	dev->first_msi_irq = irq;
	set_irq_msi(irq, entry);

	/* Set MSI enabled bits	 */
	pci_intx(dev, 0);		/* disable intx */
	msi_set_enable(dev, 1);
	dev->msi_enabled = 1;

	dev->irq = irq;
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
	struct msi_desc *head = NULL, *tail = NULL, *entry = NULL;
	int irq, pos, i, j, nr_entries, temp = 0;
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
		entry->msi_attrib.default_irq = dev->irq;
		entry->msi_attrib.pos = pos;
		entry->dev = dev;
		entry->mask_base = base;

		/* Configure MSI-X capability structure */
		irq = arch_setup_msi_irq(dev, entry);
		if (irq < 0) {
			kmem_cache_free(msi_cachep, entry);
			break;
		}
 		entries[i].vector = irq;
		if (!head) {
			entry->link.head = irq;
			entry->link.tail = irq;
			head = entry;
		} else {
			entry->link.head = temp;
			entry->link.tail = tail->link.tail;
			tail->link.tail = irq;
			head->link.head = irq;
		}
		temp = irq;
		tail = entry;

		set_irq_msi(irq, entry);
	}
	if (i != nvec) {
		int avail = i - 1;
		i--;
		for (; i >= 0; i--) {
			irq = (entries + i)->vector;
			msi_free_irq(dev, irq);
			(entries + i)->vector = 0;
		}
		/* If we had some success report the number of irqs
		 * we succeeded in setting up.
		 */
		if (avail <= 0)
			avail = -EBUSY;
		return avail;
	}
	dev->first_msi_irq = entries[0].vector;
	/* Set MSI-X enabled bits */
	pci_intx(dev, 0);		/* disable intx */
	msix_set_enable(dev, 1);
	dev->msix_enabled = 1;

	return 0;
}

/**
 * pci_msi_supported - check whether MSI may be enabled on device
 * @dev: pointer to the pci_dev data structure of MSI device function
 *
 * Look at global flags, the device itself, and its parent busses
 * to return 0 if MSI are supported for the device.
 **/
static
int pci_msi_supported(struct pci_dev * dev)
{
	struct pci_bus *bus;

	/* MSI must be globally enabled and supported by the device */
	if (!pci_msi_enable || !dev || dev->no_msi)
		return -EINVAL;

	/* Any bridge which does NOT route MSI transactions from it's
	 * secondary bus to it's primary bus must set NO_MSI flag on
	 * the secondary pci_bus.
	 * We expect only arch-specific PCI host bus controller driver
	 * or quirks for specific PCI bridges to be setting NO_MSI.
	 */
	for (bus = dev->bus; bus; bus = bus->parent)
		if (bus->bus_flags & PCI_BUS_FLAGS_NO_MSI)
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
	int pos, status;

	if (pci_msi_supported(dev) < 0)
		return -EINVAL;

	status = msi_init();
	if (status < 0)
		return status;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!pos)
		return -EINVAL;

	WARN_ON(!!dev->msi_enabled);

	/* Check whether driver already requested for MSI-X irqs */
	if (dev->msix_enabled) {
		printk(KERN_INFO "PCI: %s: Can't enable MSI.  "
			"Device already has MSI-X enabled\n",
			pci_name(dev));
		return -EINVAL;
	}
	status = msi_capability_init(dev);
	return status;
}

void pci_disable_msi(struct pci_dev* dev)
{
	struct msi_desc *entry;
	int default_irq;

	if (!pci_msi_enable)
		return;
	if (!dev)
		return;

	if (!dev->msi_enabled)
		return;

	msi_set_enable(dev, 0);
	pci_intx(dev, 1);		/* enable intx */
	dev->msi_enabled = 0;

	entry = get_irq_msi(dev->first_msi_irq);
	if (!entry || !entry->dev || entry->msi_attrib.type != PCI_CAP_ID_MSI) {
		return;
	}
	if (irq_has_action(dev->first_msi_irq)) {
		printk(KERN_WARNING "PCI: %s: pci_disable_msi() called without "
		       "free_irq() on MSI irq %d\n",
		       pci_name(dev), dev->first_msi_irq);
		BUG_ON(irq_has_action(dev->first_msi_irq));
	} else {
		default_irq = entry->msi_attrib.default_irq;
		msi_free_irq(dev, dev->first_msi_irq);

		/* Restore dev->irq to its default pin-assertion irq */
		dev->irq = default_irq;
	}
	dev->first_msi_irq = 0;
}

static int msi_free_irq(struct pci_dev* dev, int irq)
{
	struct msi_desc *entry;
	int head, entry_nr, type;
	void __iomem *base;

	entry = get_irq_msi(irq);
	if (!entry || entry->dev != dev) {
		return -EINVAL;
	}
	type = entry->msi_attrib.type;
	entry_nr = entry->msi_attrib.entry_nr;
	head = entry->link.head;
	base = entry->mask_base;
	get_irq_msi(entry->link.head)->link.tail = entry->link.tail;
	get_irq_msi(entry->link.tail)->link.head = entry->link.head;

	arch_teardown_msi_irq(irq);
	kmem_cache_free(msi_cachep, entry);

	if (type == PCI_CAP_ID_MSIX) {
		writel(1, base + entry_nr * PCI_MSIX_ENTRY_SIZE +
			PCI_MSIX_ENTRY_VECTOR_CTRL_OFFSET);

		if (head == irq)
			iounmap(base);
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

	if (!entries || pci_msi_supported(dev) < 0)
 		return -EINVAL;

	status = msi_init();
	if (status < 0)
		return status;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!pos)
 		return -EINVAL;

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
		printk(KERN_INFO "PCI: %s: Can't enable MSI-X.  "
		       "Device already has an MSI irq assigned\n",
		       pci_name(dev));
		return -EINVAL;
	}
	status = msix_capability_init(dev, entries, nvec);
	return status;
}

void pci_disable_msix(struct pci_dev* dev)
{
	int irq, head, tail = 0, warning = 0;

	if (!pci_msi_enable)
		return;
	if (!dev)
		return;

	if (!dev->msix_enabled)
		return;

	msix_set_enable(dev, 0);
	pci_intx(dev, 1);		/* enable intx */
	dev->msix_enabled = 0;

	irq = head = dev->first_msi_irq;
	while (head != tail) {
		tail = get_irq_msi(irq)->link.tail;
		if (irq_has_action(irq))
			warning = 1;
		else if (irq != head)	/* Release MSI-X irq */
			msi_free_irq(dev, irq);
		irq = tail;
	}
	msi_free_irq(dev, irq);
	if (warning) {
		printk(KERN_WARNING "PCI: %s: pci_disable_msix() called without "
			"free_irq() on all MSI-X irqs\n",
			pci_name(dev));
		BUG_ON(warning > 0);
	}
	dev->first_msi_irq = 0;
}

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

	if (dev->msi_enabled) {
		if (irq_has_action(dev->first_msi_irq)) {
			printk(KERN_WARNING "PCI: %s: msi_remove_pci_irq_vectors() "
			       "called without free_irq() on MSI irq %d\n",
			       pci_name(dev), dev->first_msi_irq);
			BUG_ON(irq_has_action(dev->first_msi_irq));
		} else /* Release MSI irq assigned to this device */
			msi_free_irq(dev, dev->first_msi_irq);
	}
	if (dev->msix_enabled) {
		int irq, head, tail = 0, warning = 0;
		void __iomem *base = NULL;

		irq = head = dev->first_msi_irq;
		while (head != tail) {
			tail = get_irq_msi(irq)->link.tail;
			base = get_irq_msi(irq)->mask_base;
			if (irq_has_action(irq))
				warning = 1;
			else if (irq != head) /* Release MSI-X irq */
				msi_free_irq(dev, irq);
			irq = tail;
		}
		msi_free_irq(dev, irq);
		if (warning) {
			iounmap(base);
			printk(KERN_WARNING "PCI: %s: msi_remove_pci_irq_vectors() "
			       "called without free_irq() on all MSI-X irqs\n",
			       pci_name(dev));
			BUG_ON(warning > 0);
		}
	}
}

void pci_no_msi(void)
{
	pci_msi_enable = 0;
}

EXPORT_SYMBOL(pci_enable_msi);
EXPORT_SYMBOL(pci_disable_msi);
EXPORT_SYMBOL(pci_enable_msix);
EXPORT_SYMBOL(pci_disable_msix);
