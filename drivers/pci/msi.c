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

static DEFINE_SPINLOCK(msi_lock);
static struct msi_desc* msi_desc[NR_IRQS] = { [0 ... NR_IRQS-1] = NULL };
static kmem_cache_t* msi_cachep;

static int pci_msi_enable = 1;

static int msi_cache_init(void)
{
	msi_cachep = kmem_cache_create("msi_cache", sizeof(struct msi_desc),
					0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!msi_cachep)
		return -ENOMEM;

	return 0;
}

static void msi_set_mask_bit(unsigned int irq, int flag)
{
	struct msi_desc *entry;

	entry = msi_desc[irq];
	BUG_ON(!entry || !entry->dev);
	switch (entry->msi_attrib.type) {
	case PCI_CAP_ID_MSI:
		if (entry->msi_attrib.maskbit) {
			int		pos;
			u32		mask_bits;

			pos = (long)entry->mask_base;
			pci_read_config_dword(entry->dev, pos, &mask_bits);
			mask_bits &= ~(1);
			mask_bits |= flag;
			pci_write_config_dword(entry->dev, pos, mask_bits);
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
	struct msi_desc *entry = get_irq_data(irq);
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
	struct msi_desc *entry = get_irq_data(irq);
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

	if (pci_msi_quirk) {
		pci_msi_enable = 0;
		printk(KERN_WARNING "PCI: MSI quirk detected. MSI disabled.\n");
		status = -EINVAL;
		return status;
	}

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

static void attach_msi_entry(struct msi_desc *entry, int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	msi_desc[irq] = entry;
	spin_unlock_irqrestore(&msi_lock, flags);
}

static int create_msi_irq(void)
{
	struct msi_desc *entry;
	int irq;

	entry = alloc_msi_entry();
	if (!entry)
		return -ENOMEM;

	irq = create_irq();
	if (irq < 0) {
		kmem_cache_free(msi_cachep, entry);
		return -EBUSY;
	}

	set_irq_data(irq, entry);

	return irq;
}

static void destroy_msi_irq(unsigned int irq)
{
	struct msi_desc *entry;

	entry = get_irq_data(irq);
	set_irq_chip(irq, NULL);
	set_irq_data(irq, NULL);
	destroy_irq(irq);
	kmem_cache_free(msi_cachep, entry);
}

static void enable_msi_mode(struct pci_dev *dev, int pos, int type)
{
	u16 control;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (type == PCI_CAP_ID_MSI) {
		/* Set enabled bits to single MSI & enable MSI_enable bit */
		msi_enable(control, 1);
		pci_write_config_word(dev, msi_control_reg(pos), control);
		dev->msi_enabled = 1;
	} else {
		msix_enable(control);
		pci_write_config_word(dev, msi_control_reg(pos), control);
		dev->msix_enabled = 1;
	}
    	if (pci_find_capability(dev, PCI_CAP_ID_EXP)) {
		/* PCI Express Endpoint device detected */
		pci_intx(dev, 0);  /* disable intx */
	}
}

void disable_msi_mode(struct pci_dev *dev, int pos, int type)
{
	u16 control;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (type == PCI_CAP_ID_MSI) {
		/* Set enabled bits to single MSI & enable MSI_enable bit */
		msi_disable(control);
		pci_write_config_word(dev, msi_control_reg(pos), control);
		dev->msi_enabled = 0;
	} else {
		msix_disable(control);
		pci_write_config_word(dev, msi_control_reg(pos), control);
		dev->msix_enabled = 0;
	}
    	if (pci_find_capability(dev, PCI_CAP_ID_EXP)) {
		/* PCI Express Endpoint device detected */
		pci_intx(dev, 1);  /* enable intx */
	}
}

static int msi_lookup_irq(struct pci_dev *dev, int type)
{
	int irq;
	unsigned long flags;

	spin_lock_irqsave(&msi_lock, flags);
	for (irq = 0; irq < NR_IRQS; irq++) {
		if (!msi_desc[irq] || msi_desc[irq]->dev != dev ||
			msi_desc[irq]->msi_attrib.type != type ||
			msi_desc[irq]->msi_attrib.default_irq != dev->irq)
			continue;
		spin_unlock_irqrestore(&msi_lock, flags);
		/* This pre-assigned MSI irq for this device
		   already exits. Override dev->irq with this irq */
		dev->irq = irq;
		return 0;
	}
	spin_unlock_irqrestore(&msi_lock, flags);

	return -EACCES;
}

void pci_scan_msi_device(struct pci_dev *dev)
{
	if (!dev)
		return;
}

#ifdef CONFIG_PM
int pci_save_msi_state(struct pci_dev *dev)
{
	int pos, i = 0;
	u16 control;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (pos <= 0 || dev->no_msi)
		return 0;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (!(control & PCI_MSI_FLAGS_ENABLE))
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

void pci_restore_msi_state(struct pci_dev *dev)
{
	int i = 0, pos;
	u16 control;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_MSI);
	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!save_state || pos <= 0)
		return;
	cap = &save_state->data[0];

	control = cap[i++] >> 16;
	pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_LO, cap[i++]);
	if (control & PCI_MSI_FLAGS_64BIT) {
		pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_HI, cap[i++]);
		pci_write_config_dword(dev, pos + PCI_MSI_DATA_64, cap[i++]);
	} else
		pci_write_config_dword(dev, pos + PCI_MSI_DATA_32, cap[i++]);
	if (control & PCI_MSI_FLAGS_MASKBIT)
		pci_write_config_dword(dev, pos + PCI_MSI_MASK_BIT, cap[i++]);
	pci_write_config_word(dev, pos + PCI_MSI_FLAGS, control);
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSI);
	pci_remove_saved_cap(save_state);
	kfree(save_state);
}

int pci_save_msix_state(struct pci_dev *dev)
{
	int pos;
	int temp;
	int irq, head, tail = 0;
	u16 control;
	struct pci_cap_saved_state *save_state;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos <= 0 || dev->no_msi)
		return 0;

	/* save the capability */
	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (!(control & PCI_MSIX_FLAGS_ENABLE))
		return 0;
	save_state = kzalloc(sizeof(struct pci_cap_saved_state) + sizeof(u16),
		GFP_KERNEL);
	if (!save_state) {
		printk(KERN_ERR "Out of memory in pci_save_msix_state\n");
		return -ENOMEM;
	}
	*((u16 *)&save_state->data[0]) = control;

	/* save the table */
	temp = dev->irq;
	if (msi_lookup_irq(dev, PCI_CAP_ID_MSIX)) {
		kfree(save_state);
		return -EINVAL;
	}

	irq = head = dev->irq;
	while (head != tail) {
		struct msi_desc *entry;

		entry = msi_desc[irq];
		read_msi_msg(irq, &entry->msg_save);

		tail = msi_desc[irq]->link.tail;
		irq = tail;
	}
	dev->irq = temp;

	save_state->cap_nr = PCI_CAP_ID_MSIX;
	pci_add_saved_cap(dev, save_state);
	return 0;
}

void pci_restore_msix_state(struct pci_dev *dev)
{
	u16 save;
	int pos;
	int irq, head, tail = 0;
	struct msi_desc *entry;
	int temp;
	struct pci_cap_saved_state *save_state;

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
	temp = dev->irq;
	if (msi_lookup_irq(dev, PCI_CAP_ID_MSIX))
		return;
	irq = head = dev->irq;
	while (head != tail) {
		entry = msi_desc[irq];
		write_msi_msg(irq, &entry->msg_save);

		tail = msi_desc[irq]->link.tail;
		irq = tail;
	}
	dev->irq = temp;

	pci_write_config_word(dev, msi_control_reg(pos), save);
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSIX);
}
#endif

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
	int status;
	struct msi_desc *entry;
	int pos, irq;
	u16 control;

   	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	pci_read_config_word(dev, msi_control_reg(pos), &control);
	/* MSI Entry Initialization */
	irq = create_msi_irq();
	if (irq < 0)
		return irq;

	entry = get_irq_data(irq);
	entry->link.head = irq;
	entry->link.tail = irq;
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
	status = arch_setup_msi_irq(irq, dev);
	if (status < 0) {
		destroy_msi_irq(irq);
		return status;
	}

	attach_msi_entry(entry, irq);
	/* Set MSI enabled bits	 */
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSI);

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
	int status;
	int irq, pos, i, j, nr_entries, temp = 0;
	unsigned long phys_addr;
	u32 table_offset;
 	u16 control;
	u8 bir;
	void __iomem *base;

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
		irq = create_msi_irq();
		if (irq < 0)
			break;

		entry = get_irq_data(irq);
 		j = entries[i].entry;
 		entries[i].vector = irq;
		entry->msi_attrib.type = PCI_CAP_ID_MSIX;
		entry->msi_attrib.is_64 = 1;
		entry->msi_attrib.entry_nr = j;
		entry->msi_attrib.maskbit = 1;
		entry->msi_attrib.default_irq = dev->irq;
		entry->msi_attrib.pos = pos;
		entry->dev = dev;
		entry->mask_base = base;
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
		/* Configure MSI-X capability structure */
		status = arch_setup_msi_irq(irq, dev);
		if (status < 0) {
			destroy_msi_irq(irq);
			break;
		}

		attach_msi_entry(entry, irq);
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
	/* Set MSI-X enabled bits */
	enable_msi_mode(dev, pos, PCI_CAP_ID_MSIX);

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
	int pos, temp, status;

	if (pci_msi_supported(dev) < 0)
		return -EINVAL;

	temp = dev->irq;

	status = msi_init();
	if (status < 0)
		return status;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!pos)
		return -EINVAL;

	WARN_ON(!msi_lookup_irq(dev, PCI_CAP_ID_MSI));

	/* Check whether driver already requested for MSI-X irqs */
	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos > 0 && !msi_lookup_irq(dev, PCI_CAP_ID_MSIX)) {
			printk(KERN_INFO "PCI: %s: Can't enable MSI.  "
			       "Device already has MSI-X irq assigned\n",
			       pci_name(dev));
			dev->irq = temp;
			return -EINVAL;
	}
	status = msi_capability_init(dev);
	return status;
}

void pci_disable_msi(struct pci_dev* dev)
{
	struct msi_desc *entry;
	int pos, default_irq;
	u16 control;
	unsigned long flags;

	if (!pci_msi_enable)
		return;
	if (!dev)
		return;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!pos)
		return;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (!(control & PCI_MSI_FLAGS_ENABLE))
		return;

	disable_msi_mode(dev, pos, PCI_CAP_ID_MSI);

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[dev->irq];
	if (!entry || !entry->dev || entry->msi_attrib.type != PCI_CAP_ID_MSI) {
		spin_unlock_irqrestore(&msi_lock, flags);
		return;
	}
	if (irq_has_action(dev->irq)) {
		spin_unlock_irqrestore(&msi_lock, flags);
		printk(KERN_WARNING "PCI: %s: pci_disable_msi() called without "
		       "free_irq() on MSI irq %d\n",
		       pci_name(dev), dev->irq);
		BUG_ON(irq_has_action(dev->irq));
	} else {
		default_irq = entry->msi_attrib.default_irq;
		spin_unlock_irqrestore(&msi_lock, flags);
		msi_free_irq(dev, dev->irq);

		/* Restore dev->irq to its default pin-assertion irq */
		dev->irq = default_irq;
	}
}

static int msi_free_irq(struct pci_dev* dev, int irq)
{
	struct msi_desc *entry;
	int head, entry_nr, type;
	void __iomem *base;
	unsigned long flags;

	arch_teardown_msi_irq(irq);

	spin_lock_irqsave(&msi_lock, flags);
	entry = msi_desc[irq];
	if (!entry || entry->dev != dev) {
		spin_unlock_irqrestore(&msi_lock, flags);
		return -EINVAL;
	}
	type = entry->msi_attrib.type;
	entry_nr = entry->msi_attrib.entry_nr;
	head = entry->link.head;
	base = entry->mask_base;
	msi_desc[entry->link.head]->link.tail = entry->link.tail;
	msi_desc[entry->link.tail]->link.head = entry->link.head;
	entry->dev = NULL;
	msi_desc[irq] = NULL;
	spin_unlock_irqrestore(&msi_lock, flags);

	destroy_msi_irq(irq);

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
	int i, j, temp;
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
	temp = dev->irq;
	WARN_ON(!msi_lookup_irq(dev, PCI_CAP_ID_MSIX));

	/* Check whether driver already requested for MSI irq */
   	if (pci_find_capability(dev, PCI_CAP_ID_MSI) > 0 &&
		!msi_lookup_irq(dev, PCI_CAP_ID_MSI)) {
		printk(KERN_INFO "PCI: %s: Can't enable MSI-X.  "
		       "Device already has an MSI irq assigned\n",
		       pci_name(dev));
		dev->irq = temp;
		return -EINVAL;
	}
	status = msix_capability_init(dev, entries, nvec);
	return status;
}

void pci_disable_msix(struct pci_dev* dev)
{
	int pos, temp;
	u16 control;

	if (!pci_msi_enable)
		return;
	if (!dev)
		return;

	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (!pos)
		return;

	pci_read_config_word(dev, msi_control_reg(pos), &control);
	if (!(control & PCI_MSIX_FLAGS_ENABLE))
		return;

	disable_msi_mode(dev, pos, PCI_CAP_ID_MSIX);

	temp = dev->irq;
	if (!msi_lookup_irq(dev, PCI_CAP_ID_MSIX)) {
		int irq, head, tail = 0, warning = 0;
		unsigned long flags;

		irq = head = dev->irq;
		dev->irq = temp;			/* Restore pin IRQ */
		while (head != tail) {
			spin_lock_irqsave(&msi_lock, flags);
			tail = msi_desc[irq]->link.tail;
			spin_unlock_irqrestore(&msi_lock, flags);
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
	}
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
	int pos, temp;
	unsigned long flags;

	if (!pci_msi_enable || !dev)
 		return;

	temp = dev->irq;		/* Save IOAPIC IRQ */
	pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (pos > 0 && !msi_lookup_irq(dev, PCI_CAP_ID_MSI)) {
		if (irq_has_action(dev->irq)) {
			printk(KERN_WARNING "PCI: %s: msi_remove_pci_irq_vectors() "
			       "called without free_irq() on MSI irq %d\n",
			       pci_name(dev), dev->irq);
			BUG_ON(irq_has_action(dev->irq));
		} else /* Release MSI irq assigned to this device */
			msi_free_irq(dev, dev->irq);
		dev->irq = temp;		/* Restore IOAPIC IRQ */
	}
	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos > 0 && !msi_lookup_irq(dev, PCI_CAP_ID_MSIX)) {
		int irq, head, tail = 0, warning = 0;
		void __iomem *base = NULL;

		irq = head = dev->irq;
		while (head != tail) {
			spin_lock_irqsave(&msi_lock, flags);
			tail = msi_desc[irq]->link.tail;
			base = msi_desc[irq]->mask_base;
			spin_unlock_irqrestore(&msi_lock, flags);
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
		dev->irq = temp;		/* Restore IOAPIC IRQ */
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
